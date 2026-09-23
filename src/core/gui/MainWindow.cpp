#include "MainWindow.h"

#include <regex>

#include <gdk-pixbuf/gdk-pixbuf.h>  // for gdk_pixbuf_new_fr...
#include <gdk/gdk.h>                // for gdk_screen_get_de...
#include <gio/gio.h>                // for g_cancellable_is_...
#include <gtk/gtkcssprovider.h>     // for gtk_css_provider_...

#include "control/AudioController.h"                    // for AudioController
#include "control/Control.h"                            // for Control
#include "control/DeviceListHelper.h"                   // for getSourceMapping
#include "control/RecentManager.h"                      // for the recent files
#include "control/ScrollHandler.h"                      // for ScrollHandler
#include "control/ToolConfigAdapter.h"                  // for ToolConfigAdapter (Plan 003)
#include "control/ToolEnums.h"                          // for ToolType
#include "control/ToolHandler.h"                        // for ToolHandler
#include "control/ToolPreset.h"                         // for ToolPresetList
#include "control/actions/ActionDatabase.h"             // for ActionDatabase
#include "control/jobs/XournalScheduler.h"              // for XournalScheduler
#include "control/layer/LayerController.h"              // for LayerController
#include "control/settings/Settings.h"                  // for Settings
#include "control/settings/SettingsEnums.h"             // for SCROLLBAR_HIDE_HO...
#include "control/zoom/ZoomControl.h"                   // for ZoomControl
#include "dashboard/FileWatcher.h"                      // for FileWatcher
#include "dashboard/RecoveryActions.h"                  // for the recovery file work
#include "dashboard/ThumbnailCache.h"                   // for ThumbnailCache
#include "enums/Action.enum.h"                          // for Action_toString
#include "gui/CommandPalette.h"                         // for CommandPalette (Plan 007)
#include "gui/FloatingToolbox.h"                        // for FloatingToolbox
#include "gui/GladeGui.h"                               // for GladeGui
#include "gui/PdfFloatingToolbox.h"                     // for PdfFloatingToolbox
#include "gui/QuickPalette.h"                           // for QuickPalette (Plan 008)
#include "gui/QuickPaletteContents.h"                   // for buildQuickPaletteSlots (Plan 008)
#include "gui/SafetyStatusBar.h"                        // for SafetyStatusBar (Plan 004)
#include "gui/SearchBar.h"                              // for SearchBar
#include "gui/ShortcutReference.h"                      // for ShortcutReference (Plan 007)
#include "gui/TipService.h"                             // for TipService (Plan 007)
#include "gui/WorkspaceGuidance.h"                      // for WorkspaceGuidance (Plan 007)
#include "gui/dashboard/DashboardPage.h"                // for DashboardPage
#include "gui/dashboard/SurfaceStack.h"                 // for SurfaceStack
#include "gui/dialog/XojOpenDlg.h"                      // for the file and folder choosers
#include "gui/dialog/XojSaveDlg.h"                      // for the save-as chooser
#include "gui/inputdevices/InputEvents.h"               // for INPUT_DEVICE_TOUC...
#include "gui/menus/menubar/Menubar.h"                  // for Menubar
#include "gui/menus/menubar/ToolbarSelectionSubmenu.h"  // for ToolbarSelectionSubmenu
#include "gui/menus/menubar/WorkspaceSubmenu.h"         // for WorkspaceSubmenu
#include "gui/scroll/ScrollHandling.h"                  // for ScrollHandling
#include "gui/sidebar/Sidebar.h"                        // for Sidebar
#include "gui/toolbarMenubar/ToolMenuHandler.h"         // for ToolMenuHandler
#include "gui/toolbarMenubar/model/ToolbarData.h"       // for ToolbarData
#include "gui/toolbarMenubar/model/ToolbarModel.h"      // for ToolbarModel
#include "gui/widgets/SpinPageAdapter.h"                // for SpinPageAdapter
#include "gui/widgets/XournalWidget.h"                  // for gtk_xournal_get_l...
#include "model/Document.h"                             // for Document (the open document's path)
#include "util/GListView.h"                             // for GListView, GListV...
#include "util/GtkUtil.h"                               // for getWidgetDPI
#include "util/PathUtil.h"                              // for getConfigFile
#include "util/StringUtils.h"                           // for char_cast
#include "util/Util.h"                                  // for execInUiThread, npos
#include "util/XojMsgBox.h"                             // for XojMsgBox
#include "util/glib_casts.h"                            // for wrap_for_once_v
#include "util/gtk4_helper.h"                           // for gtk_widget_get_width
#include "util/i18n.h"                                  // for FS, _F
#include "util/raii/CStringWrapper.h"                   // for OwnedCString

#include "GladeSearchpath.h"     // for GladeSearchpath
#include "ToolbarDefinitions.h"  // for TOOLBAR_DEFINITIO...
#include "XournalView.h"         // for XournalView
#include "config-dev.h"          // for TOOLBAR_CONFIG
#include "filesystem.h"          // for path, exists

#ifdef __APPLE__
// the following header file contains a definition of struct Point that conflicts with model/Point.h
#define Point Point_CF
#include <CoreFoundation/CoreFoundation.h>
#undef Point
#endif

using std::string;


static void themeCallback(GObject*, GParamSpec*, gpointer data) { static_cast<MainWindow*>(data)->updateColorscheme(); }

MainWindow::MainWindow(GladeSearchpath* gladeSearchPath, Control* control, GtkApplication* parent):
        GladeGui(gladeSearchPath, "main.glade", "mainWindow"),
        control(control),
        toolbar(std::make_unique<ToolMenuHandler>(control, this)),
        menubar(std::make_unique<Menubar>()) {
    gtk_window_set_application(GTK_WINDOW(getWindow()), parent);

    panedContainerWidget.reset(get("panelMainContents"), xoj::util::ref);
    boxContainerWidget.reset(get("mainContentContainer"), xoj::util::ref);
    mainContentWidget.reset(get("boxContents"), xoj::util::ref);
    sidebarWidget.reset(get("sidebar"), xoj::util::ref);

    /*
     * Plan 004: the document-safety row goes under the top toolbars, next to the page controls,
     * rather than floating over the page. It is packed into the box that already holds the canvas
     * as its only child, so it never competes with a toolbar for a slot and never covers content.
     */
    this->safetyStatusBar = std::make_unique<SafetyStatusBar>(SafetyStatusBar::Callbacks{
            .showDetails = [control](const std::string& details,
                                     bool retryable) { control->showSafetyDetails(details, retryable); },
            .retry = [control]() { control->retryFailedSafetyOperation(); },
            .refresh = [this]() { this->control->pushSafetyState(); }});

    GtkWidget* safetyRow = this->safetyStatusBar->getWidget();
    gtk_box_pack_start(GTK_BOX(boxContainerWidget.get()), safetyRow, FALSE, FALSE, 0);
    gtk_box_reorder_child(GTK_BOX(boxContainerWidget.get()), safetyRow, 0);

    loadMainCSS(gladeSearchPath, "xournalpp.css");

    GtkOverlay* overlay = GTK_OVERLAY(get("mainOverlay"));
    this->pdfFloatingToolBox = std::make_unique<PdfFloatingToolbox>(this, overlay);
    this->floatingToolbox = std::make_unique<FloatingToolbox>(this, overlay);
    // Plan 008: the quick palette lives in the same overlay; it is hidden unless the user has bound
    // it and the gesture preferences say so, so Classic behaviour is untouched.
    this->quickPalette = std::make_unique<xoj::gui::QuickPalette>(overlay, GTK_WINDOW(get("mainWindow")));

    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        this->toolbarWidgets[i].reset(get(TOOLBAR_DEFINITIONS[i].guiName), xoj::util::ref);
    }

    // Plan 006: the home surface, built before the stack that holds it.
    buildDashboard();

    initXournalWidget();

    setSidebarVisible(control->getSettings()->isSidebarVisible());

    // Window handler
    g_signal_connect(this->window, "delete-event", xoj::util::wrap_for_g_callback_v<deleteEventCallback>,
                     this->control);
#if GTK_MAJOR_VERSION == 3
    g_signal_connect(this->window, "notify::is-maximized", xoj::util::wrap_for_g_callback_v<windowMaximizedCallback>,
                     this);
#else
    g_signal_connect(this->window, "notify::maximized", xoj::util::wrap_for_g_callback_v<windowMaximizedCallback>,
                     this);
#endif

    g_signal_connect(
            this->window, "configure-event", G_CALLBACK(+[](GtkWidget* widget, GdkEvent*, gpointer self) -> gboolean {
                auto win = static_cast<MainWindow*>(self);
                GdkWindow* gdkWindow = gtk_widget_get_window(widget);
                GdkDisplay* display = gdkWindow ? gdk_window_get_display(gdkWindow) : nullptr;
                GdkMonitor* monitor = display ? gdk_display_get_monitor_at_window(display, gdkWindow) : nullptr;
                if (monitor && monitor != win->lastMonitor) {
                    win->lastMonitor = monitor;
                    const char* monitorName = gdk_monitor_get_model(monitor);
                    g_debug("Window moved to monitor \"%s\"", monitorName);
                    win->setDPI();
                }
                return false;
            }),
            this);

    // "watch over" all key events
    auto keyPropagate = +[](GtkWidget* w, GdkEvent* e, gpointer) {
        return gtk_window_propagate_key_event(GTK_WINDOW(w), (GdkEventKey*)(e));
    };
    g_signal_connect(this->window, "key-press-event", G_CALLBACK(keyPropagate), nullptr);
    g_signal_connect(this->window, "key-release-event", G_CALLBACK(keyPropagate), nullptr);

    updateScrollbarSidebarPosition();

    gtk_window_set_default_size(GTK_WINDOW(this->window), control->getSettings()->getMainWndWidth(),
                                control->getSettings()->getMainWndHeight());

    if (control->getSettings()->isMainWndMaximized()) {
        gtk_window_maximize(GTK_WINDOW(this->window));
    } else {
        gtk_window_unmaximize(GTK_WINDOW(this->window));
    }

    Util::execInUiThread([=]() {
        // Execute after the window is visible, else the check won't work
        control->setShowMenubar(control->getSettings()->isMenubarVisible());
    });

    // Drag and Drop
    g_signal_connect(this->window, "drag-data-received", G_CALLBACK(dragDataRecived), this);

    gtk_drag_dest_set(this->window, GTK_DEST_DEFAULT_ALL, nullptr, 0, GDK_ACTION_COPY);
    gtk_drag_dest_add_uri_targets(this->window);
    gtk_drag_dest_add_image_targets(this->window);
    gtk_drag_dest_add_text_targets(this->window);

    auto* settings = gtk_widget_get_settings(this->window);
    this->themeNameHandlerId = g_signal_connect(settings, "notify::gtk-theme-name", G_CALLBACK(themeCallback), this);
    this->darkThemeHandlerId =
            g_signal_connect(settings, "notify::gtk-application-prefer-dark-theme", G_CALLBACK(themeCallback), this);

    updateColorscheme();
}

void MainWindow::populate(GladeSearchpath* gladeSearchPath) {

    toolbar->populate(gladeSearchPath);
    menubar->populate(gladeSearchPath, this);

    // need to create tool buttons registered in plugins, so they can be added to toolbars
    control->registerPluginToolButtons(this->toolbar.get());

    createToolbar();

    // Plan 002: the workspace decides the initial toolbar and menubar chrome. The view mode
    // (fullscreen, presentation) and the sidebar stay user controlled.
    applyWorkspaceChrome();

    setToolbarVisible(control->getSettings()->isToolbarVisible());

    // Plan 007: the palette, over the window it belongs to. Built here, once the editor and the
    // toolbars it reads the commands from are there.
    buildCommandPalette();
    buildWorkspaceGuidance();
    buildShortcutReference();
    buildTipService();
}

auto MainWindow::buildCommandRegistry() const -> xoj::command::CommandRegistry {
    using xoj::command::CommandEntry;
    using xoj::command::CommandRegistry;

    GtkApplication* app = GTK_APPLICATION(gtk_window_get_application(GTK_WINDOW(this->getWindow())));
    ActionDatabase* db = this->control->getActionDatabase();

    CommandRegistry commands(
            [app](const std::string& detailedActionName) -> std::vector<std::string> {
                std::vector<std::string> accelerators;
                gchar** held = gtk_application_get_accels_for_action(app, detailedActionName.c_str());
                for (gchar** one = held; one != nullptr && *one != nullptr; one++) {
                    accelerators.emplace_back(*one);
                }
                g_strfreev(held);
                return accelerators;
            },
            [db](const CommandEntry& entry) -> std::optional<std::string> {
                return entry.knownAction ? db->getDisabledReason(*entry.knownAction) : std::nullopt;
            },
            [db](const CommandEntry& entry) -> std::vector<std::string> {
                return entry.knownAction ? db->getKeywords(*entry.knownAction) : std::vector<std::string>{};
            });

    commands.addFromMenuModel(this->getMenuModel());
    commands.addFromToolItems(this->getToolMenuHandler()->getToolItems());
    return commands;
}

void MainWindow::buildCommandPalette() {
    using xoj::command::CommandPalette;
    using xoj::command::CommandRegistry;

    CommandRegistry::Maps maps;
    maps.window = G_ACTION_MAP(this->getWindow());
    GtkApplication* app = GTK_APPLICATION(gtk_window_get_application(GTK_WINDOW(this->getWindow())));
    maps.application = app != nullptr ? G_ACTION_MAP(app) : nullptr;

    this->commandPalette = std::make_unique<CommandPalette>(
            GTK_WINDOW(this->getWindow()), this->xournal->getWidget(), this->control->getSettings(),
            [this] { return this->buildCommandRegistry(); }, maps);
}

void MainWindow::showCommandPalette() {
    if (this->commandPalette == nullptr) {
        return;
    }
    // Ctrl+K again, while it is open, puts it away: the shortcut that opened it closes it.
    this->commandPalette->toggle();
}

auto MainWindow::getCommandPalette() const -> xoj::command::CommandPalette* { return this->commandPalette.get(); }

void MainWindow::buildWorkspaceGuidance() {
    using xoj::gui::WorkspaceGuidance;

    this->workspaceGuidance =
            std::make_unique<WorkspaceGuidance>(GTK_WINDOW(this->getWindow()), this->control->getSettings());

    /*
     * A popover needs a widget that is on screen to be anchored to, so the explanation is offered
     * when the window is up - and it is offered on the editor surface, because that is the workspace
     * it explains, and a dashboard the user is reading files from is not the place for it.
     */
    g_signal_connect_swapped(this->getWindow(), "map",
                             G_CALLBACK(+[](MainWindow* self) { self->showWorkspaceGuidanceIfFresh(); }), this);
}

void MainWindow::showWorkspaceGuidanceIfFresh() {
    if (this->workspaceGuidance == nullptr || !gtk_widget_get_mapped(this->getWindow())) {
        return;
    }
    if (this->surfaces != nullptr && this->surfaces->isHomeShown()) {
        return;
    }
    this->workspaceGuidance->showIfFresh();
}

auto MainWindow::getWorkspaceGuidance() const -> xoj::gui::WorkspaceGuidance* { return this->workspaceGuidance.get(); }

void MainWindow::buildShortcutReference() {
    using xoj::gui::ShortcutReference;

    // The registry is read when the reference is opened, so what it lists is what the application
    // offers at that moment - a command that has become unavailable is not still listed as available.
    this->shortcutReference = std::make_unique<ShortcutReference>(GTK_WINDOW(this->getWindow()),
                                                                  [this] { return this->buildCommandRegistry(); });
}

void MainWindow::showShortcutReference() {
    if (this->shortcutReference == nullptr) {
        return;
    }
    this->shortcutReference->toggle();
}

auto MainWindow::getShortcutReference() const -> xoj::gui::ShortcutReference* { return this->shortcutReference.get(); }

void MainWindow::buildTipService() {
    using xoj::gui::TipService;

    /*
     * The service is built over the window and registered on it, because that is where the widgets a
     * tip is about find it: the tool property popover knows the window it is anchored in and not
     * this object. Built here, with the palette, once the window has contents to point at.
     */
    this->tipService = std::make_unique<TipService>(GTK_WINDOW(this->getWindow()), this->control->getSettings());
}

auto MainWindow::getTipService() const -> xoj::gui::TipService* { return this->tipService.get(); }

GMenuModel* MainWindow::getMenuModel() const { return menubar->getModel(); }

MainWindow::~MainWindow() {
    /*
     * Plan 006: the dashboard's watches and its outstanding previews go with the window that asked
     * for them. A watcher that outlived the window would call into a dashboard that is gone, and a
     * preview that arrived afterwards would be handed to a page that no longer exists.
     */
    if (this->dashboardWatcher != nullptr) {
        this->dashboardWatcher->stop();
    }
    if (this->dashboardThumbnails != nullptr) {
        this->dashboardThumbnails->cancelAll();
    }

    // The settings outlive the window and this window is their handler data: a subscription left
    // behind has them call into a window that is gone (the next window's colorscheme update is
    // enough to trigger it). GtkSettings are per screen and never released, so the handlers have
    // to be taken off here.
    auto* settings = gtk_widget_get_settings(this->window);
    for (gulong id: {this->themeNameHandlerId, this->darkThemeHandlerId}) {
        if (id != 0 && g_signal_handler_is_connected(settings, id)) {
            g_signal_handler_disconnect(settings, id);
        }
    }
}

struct ThemeProperties {
    bool dark;
    bool darkSuffix;
    std::string rootname;  ///< Name without any putative -dark suffix
};
static ThemeProperties getThemeProperties(GtkWidget* w) {
    xoj::util::OwnedCString name;
    [[maybe_unused]] bool useEnv = false;
    // Gtk prioritizes GTK_THEME over GtkSettings content
    // cf https://gitlab.gnome.org/GNOME/gtk/blob/90d84a2af8b367bd5a5312b3fa3b67563462c0ef/gtk/gtksettings.c#L1567-L1622
    if (auto* p = g_getenv("GTK_THEME")) {
        *(name.contentReplacer()) = g_strdup(p);
        useEnv = true;
    } else {
        g_object_get(gtk_widget_get_settings(w), "gtk-theme-name", name.contentReplacer(), nullptr);
    }

    // Try to figure out if the theme is dark or light
    // Some themes handle their dark variant via "gtk-application-prefer-dark-theme" while other just append "-dark"
    const std::regex nameparser("([a-zA-Z0-9_\\.-]+?)([:-][dD]ark)?");
    std::cmatch sm;
    std::regex_match(name.get(), sm, nameparser);

    ThemeProperties props;
    if (sm.size() < 3) {
        g_warning("Fails to extract theme root name from: \"%s\"", name.get());
        props.rootname = name.get();
        props.darkSuffix = false;
    } else {
        props.rootname = sm[1];
        props.darkSuffix = sm[2].length() > 0;
    }
    gboolean dark = false;

#ifdef __APPLE__
    if (!useEnv) {
        CFStringRef interfaceStyle =
                (CFStringRef)CFPreferencesCopyAppValue(CFSTR("AppleInterfaceStyle"), kCFPreferencesCurrentApplication);
        if (interfaceStyle) {
            char buffer[128];
            if (CFStringGetCString(interfaceStyle, buffer, sizeof(buffer), kCFStringEncodingUTF8)) {
                std::string style = buffer;
                if (auto pos = style.find("Dark"); pos != std::string::npos) {
                    dark = true;
                }
            }
        }
    }
#else
    g_object_get(gtk_widget_get_settings(w), "gtk-application-prefer-dark-theme", &dark, nullptr);
#endif

    g_debug("Extracted theme info: Name = %s, rootname = %s, dark = %s", name.get(), props.rootname.c_str(),
            dark ? "true" : "false");

    props.dark = props.darkSuffix || dark;  // Some themes handle their dark variant via this setting

    return props;
}

void MainWindow::updateColorscheme() {
    g_signal_handlers_block_by_func(gtk_widget_get_settings(this->window),
                                    reinterpret_cast<gpointer>(G_CALLBACK(themeCallback)), this);
    auto variant = control->getSettings()->getThemeVariant();
    if (variant == THEME_VARIANT_USE_SYSTEM) {
        gtk_settings_reset_property(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme");
        if (modifiedGtkSettingsTheme) {
            // Some bug in Gtk makes an infinite loop despite us blocking the signals
            gtk_settings_reset_property(gtk_widget_get_settings(this->window), "gtk-theme-name");
            modifiedGtkSettingsTheme = false;
        }
    }
    auto props = getThemeProperties(this->window);

    this->darkMode = (props.dark && variant != THEME_VARIANT_FORCE_LIGHT) || variant == THEME_VARIANT_FORCE_DARK;

    // Set up icons
    {
        const auto uiPath = this->getGladeSearchPath()->getFirstSearchPath();
        const auto lightColorIcons = (uiPath / "iconsColor-light");
        const auto darkColorIcons = (uiPath / "iconsColor-dark");
        const auto lightLucideIcons = (uiPath / "iconsLucide-light");
        const auto darkLucideIcons = (uiPath / "iconsLucide-dark");

        // icon load order from lowest priority to highest priority
        std::vector<fs::path> iconLoadOrder = {};
        const auto chosenTheme = control->getSettings()->getIconTheme();
        switch (chosenTheme) {
            case ICON_THEME_COLOR:
                iconLoadOrder = {darkLucideIcons, lightLucideIcons, darkColorIcons, lightColorIcons};
                break;
            case ICON_THEME_LUCIDE:
                iconLoadOrder = {darkColorIcons, lightColorIcons, darkLucideIcons, lightLucideIcons};
                break;
            default:
                g_message("Unknown icon theme!");
        }

        if (this->darkMode) {
            for (size_t i = 0; 2 * i + 1 < iconLoadOrder.size(); ++i) {
                std::swap(iconLoadOrder[2 * i], iconLoadOrder[2 * i + 1]);
            }
        }

        for (auto& p: iconLoadOrder) {
            gtk_icon_theme_prepend_search_path(gtk_icon_theme_get_default(), char_cast(p.u8string().c_str()));
        }
    }

    GtkStyleContext* context = gtk_widget_get_style_context(GTK_WIDGET(this->window));

    if (this->darkMode) {
        gtk_style_context_add_class(context, "darkMode");
        g_object_set(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", true, nullptr);
    } else {
        gtk_style_context_remove_class(context, "darkMode");
        g_object_set(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", false, nullptr);
        if (props.darkSuffix) {  // The active theme is all dark. Remove the trailing "-dark"
            g_object_set(gtk_widget_get_settings(this->window), "gtk-theme-name", props.rootname.c_str(), nullptr);
            modifiedGtkSettingsTheme = true;
        }
    }

    {
        gchar* name = nullptr;
        g_object_get(gtk_widget_get_settings(this->window), "gtk-theme-name", &name, nullptr);
        g_debug("Theme name: %s", name);
        g_debug("Modified in GtkSettings: %s", modifiedGtkSettingsTheme ? "true" : "false");
        g_free(name);
        gboolean gtkdark = true;
        g_object_get(gtk_widget_get_settings(this->window), "gtk-application-prefer-dark-theme", &gtkdark, nullptr);
        g_debug("Theme variant: %s", gtkdark ? "dark" : "light");
        g_debug("Icon theme: %s", iconThemeToString(control->getSettings()->getIconTheme()));
    }
    g_signal_handlers_unblock_by_func(gtk_widget_get_settings(this->window), reinterpret_cast<gpointer>(themeCallback),
                                      this);
}

void MainWindow::initXournalWidget() {
    winXournal = gtk_scrolled_window_new();

    setGtkTouchscreenScrollingForDeviceMapping();

    // Plan 006: the editor is one page of the window's two surfaces. It is the widget it always
    // was, put in a stack so the home surface can be shown beside it: what changes when the surface
    // changes is what is visible, and nothing about the document.
    buildSurfaceStack();

    scrollHandling = std::make_unique<ScrollHandling>(GTK_SCROLLED_WINDOW(winXournal));

    this->xournal = std::make_unique<XournalView>(winXournal, control, scrollHandling.get());

    control->getZoomControl()->initZoomHandler(this->window, winXournal, xournal.get(), control);
    gtk_widget_show_all(winXournal);
}

void MainWindow::setGtkTouchscreenScrollingForDeviceMapping() {
    InputDeviceClass touchscreenClass =
            DeviceListHelper::getSourceMapping(GDK_SOURCE_TOUCHSCREEN, this->getControl()->getSettings());

    setGtkTouchscreenScrollingEnabled(touchscreenClass == INPUT_DEVICE_TOUCHSCREEN &&
                                      !control->getSettings()->getTouchDrawingEnabled());
}

void MainWindow::setGtkTouchscreenScrollingEnabled(bool enabled) {
    if (!control->getSettings()->getGtkTouchInertialScrollingEnabled()) {
        enabled = false;
    }
    gtk_scrolled_window_set_kinetic_scrolling(GTK_SCROLLED_WINDOW(winXournal), enabled);
}

auto MainWindow::getLayout() const -> Layout* { return this->xournal->getLayout(); }

auto cancellable_cancel(GCancellable* cancel) -> bool {
    g_cancellable_cancel(cancel);

    g_warning("Timeout... Cancel loading URL");

    return false;
}

void MainWindow::dragDataRecived(GtkWidget* widget, GdkDragContext* dragContext, gint x, gint y, GtkSelectionData* data,
                                 guint info, guint time, MainWindow* win) {
    GtkWidget* source = gtk_drag_get_source_widget(dragContext);
    if (source && widget == gtk_widget_get_toplevel(source)) {
        gtk_drag_finish(dragContext, false, false, time);
        return;
    }

    guchar* text = gtk_selection_data_get_text(data);
    if (text) {
        win->control->clipboardPasteText(reinterpret_cast<const char*>(text));

        g_free(text);
        gtk_drag_finish(dragContext, true, false, time);
        return;
    }

    xoj::util::GObjectSPtr<GdkPixbuf> image(gtk_selection_data_get_pixbuf(data), xoj::util::adopt);
    if (image) {
        win->control->clipboardPasteImage(image.get());

        gtk_drag_finish(dragContext, true, false, time);
        return;
    }

    gchar** uris = gtk_selection_data_get_uris(data);
    if (uris) {
        for (int i = 0; uris[i] != nullptr && i < 3; i++) {
            const char* uri = uris[i];

            GCancellable* cancel = g_cancellable_new();
            auto cancelTimeout = g_timeout_add(3000, xoj::util::wrap_for_once_v<cancellable_cancel>, cancel);

            xoj::util::GObjectSPtr<GFile> file(g_file_new_for_uri(uri), xoj::util::adopt);
            GError* err = nullptr;
            GFileInputStream* in = g_file_read(file.get(), cancel, &err);
            if (g_cancellable_is_cancelled(cancel)) {
                continue;
            }

            if (err == nullptr) {
                xoj::util::GObjectSPtr<GdkPixbuf> pixbuf(
                        gdk_pixbuf_new_from_stream(G_INPUT_STREAM(in), cancel, nullptr), xoj::util::adopt);
                if (g_cancellable_is_cancelled(cancel)) {
                    continue;
                }
                g_input_stream_close(G_INPUT_STREAM(in), cancel, nullptr);
                if (g_cancellable_is_cancelled(cancel)) {
                    continue;
                }

                if (pixbuf) {
                    win->control->clipboardPasteImage(pixbuf.get());
                }
            } else {
                g_error_free(err);
            }

            if (!g_cancellable_is_cancelled(cancel)) {
                g_source_remove(cancelTimeout);
            }
            g_object_unref(cancel);
        }

        gtk_drag_finish(dragContext, true, false, time);

        g_strfreev(uris);
    }

    gtk_drag_finish(dragContext, false, false, time);
}

auto MainWindow::getControl() const -> Control* { return control; }

void MainWindow::updateScrollbarSidebarPosition() {
    // Part 1: update scrollbar position
    if (winXournal != nullptr) {
        GtkScrolledWindow* scrolledWindow = GTK_SCROLLED_WINDOW(winXournal);

        ScrollbarHideType type = this->getControl()->getSettings()->getScrollbarHideType();

        bool scrollbarOnLeft = control->getSettings()->isScrollbarOnLeft();
        if (scrollbarOnLeft) {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_RIGHT);
        } else {
            gtk_scrolled_window_set_placement(scrolledWindow, GTK_CORNER_TOP_LEFT);
        }

        gtk_widget_set_visible(gtk_scrolled_window_get_hscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_HORIZONTAL));
        gtk_widget_set_visible(gtk_scrolled_window_get_vscrollbar(scrolledWindow), !(type & SCROLLBAR_HIDE_VERTICAL));

        gtk_scrolled_window_set_overlay_scrolling(scrolledWindow,
                                                  !control->getSettings()->isScrollbarFadeoutDisabled());
    }

    // Part 2: update sidebar position
    GtkPaned* paned = GTK_PANED(this->panedContainerWidget.get());

    // Allocation is reset when we switch up the contained elements. Fetch the
    // width here in case we need it afterwards.
    int contentWidth = gtk_widget_get_width(this->boxContainerWidget.get());

    bool sidebarRight = control->getSettings()->isSidebarOnRight();
    if (sidebarRight != (gtk_paned_get_child2(paned) == this->sidebarWidget.get())) {
        // switch sidebar and main content
        GtkWidget* sidebar = this->sidebarWidget.get();
        GtkWidget* mainContent = this->sidebarVisible ? this->mainContentWidget.get() : nullptr;
#if GTK_MAJOR_VERSION == 3
        if (this->sidebarVisible) {
            gtk_container_remove(GTK_CONTAINER(paned), sidebar);
            gtk_container_remove(GTK_CONTAINER(paned), mainContent);

            if (sidebarRight) {
                gtk_paned_pack1(paned, mainContent, true, false);
                gtk_paned_pack2(paned, sidebar, false, false);
            } else {
                gtk_paned_pack1(paned, sidebar, false, false);
                gtk_paned_pack2(paned, mainContent, true, false);
            }
        } else {
            // The sidebar is hidden. That means the paned widget only contains the
            // sidebar while the main contents are shown alone in the box container.
            gtk_container_remove(GTK_CONTAINER(paned), sidebar);

            if (sidebarRight) {
                gtk_paned_pack2(paned, sidebar, false, false);
            } else {
                gtk_paned_pack1(paned, sidebar, false, false);
            }
        }
#else
        gtk_paned_set_start_child(paned, nullptr);
        gtk_paned_set_end_child(paned, nullptr);
        if (sidebarRight) {
            gtk_paned_set_start_child(paned, mainContent);
            gtk_paned_set_resize_start_child(paned, true);
            gtk_paned_set_shrink_start_child(paned, false);
            gtk_paned_set_end_child(paned, sidebar);
            gtk_paned_set_resize_end_child(paned, false);
            gtk_paned_set_shrink_end_child(paned, false);
        } else {
            gtk_paned_set_end_child(paned, mainContent);
            gtk_paned_set_resize_end_child(paned, true);
            gtk_paned_set_shrink_end_child(paned, false);
            gtk_paned_set_start_child(paned, sidebar);
            gtk_paned_set_resize_start_child(paned, false);
            gtk_paned_set_shrink_start_child(paned, false);
        }
#endif
    }

    if (this->sidebarVisible) {
        updatePanedPosition(contentWidth);
    }
}

auto MainWindow::deleteEventCallback(GtkWidget* widget, GdkEvent* event, Control* control) -> bool {
    control->quit();

    return true;
}

void MainWindow::setSidebarVisible(bool visible) {
    if (!visible && (this->control->getSidebar() != nullptr)) {
        this->control->getSidebar()->saveSize();
    }

    if (visible != this->sidebarVisible) {
        // Due to a GTK bug, we can't just hide the sidebar widget in the GtkPaned.
        // If we do this, we create a dead region where the pane separator was previously.
        // In this region, we can't use the touchscreen to start horizontal strokes.
        // As such:
        if (!visible) {
            // hide sidebar
#if GTK_MAJOR_VERSION == 3
            gtk_container_remove(GTK_CONTAINER(panedContainerWidget.get()), mainContentWidget.get());
#else
            if (control->getSettings()->isSidebarOnRight()) {
                gtk_paned_set_start_child(GTK_PANED(panedContainerWidget.get()), nullptr);
            } else {
                gtk_paned_set_end_child(GTK_PANED(panedContainerWidget.get()), nullptr);
            }
#endif
            gtk_box_remove(GTK_BOX(boxContainerWidget.get()), panedContainerWidget.get());
            gtk_box_append(GTK_BOX(boxContainerWidget.get()), mainContentWidget.get());
            this->sidebarVisible = false;
        } else {
            // show sidebar

            // Allocation is reset when we switch up the contained elements. Fetch the
            // width here in case we need it afterwards.
            int contentWidth = gtk_widget_get_width(boxContainerWidget.get());

            gtk_box_remove(GTK_BOX(boxContainerWidget.get()), mainContentWidget.get());

#if GTK_MAJOR_VERSION == 3
            if (control->getSettings()->isSidebarOnRight()) {
                gtk_paned_pack1(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get(), true, false);
            } else {
                gtk_paned_pack2(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get(), true, false);
            }
#else
            if (control->getSettings()->isSidebarOnRight()) {
                gtk_paned_set_start_child(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get());
            } else {
                gtk_paned_set_end_child(GTK_PANED(panedContainerWidget.get()), mainContentWidget.get());
            }
#endif

            gtk_box_append(GTK_BOX(boxContainerWidget.get()), panedContainerWidget.get());
            this->sidebarVisible = true;

            updatePanedPosition(contentWidth);
        }
    }

    gtk_widget_set_visible(sidebarWidget.get(), visible);
}

/**
 * Invert the position of the paned widget and disconnect from the signal.
 * @param handlerId should be the ID of the signal handler that should be disconnected.
 */
static void invertPanedPosition(GtkWidget* widget, GtkAllocation* allocation, gulong* handlerId) {
    int newDividerPos = allocation->width - gtk_paned_get_position(GTK_PANED(widget));
    gtk_paned_set_position(GTK_PANED(widget), newDividerPos);

    // We only need to switch the position once, so disconnect the signal right away.
    g_signal_handler_disconnect(widget, *handlerId);
}

void MainWindow::updatePanedPosition(int contentWidth) {
    if (!this->control->getSettings()->isSidebarOnRight()) {
        // Sidebar is on the left side.
        gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()),
                               this->control->getSettings()->getSidebarWidth());
    } else {
        // Sidebar is on the right side.
        if (contentWidth > 0) {
            int dividerPos = contentWidth - this->control->getSettings()->getSidebarWidth();
            gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()), dividerPos);
        } else {
            // Allocation is unkown (window hasn't been shown yet). We have to wait for the signal.
            // Set position as if the sidebar was on the left side, and let the signal handler
            // simply invert the position when the allocation is known.
            gtk_paned_set_position(GTK_PANED(this->panedContainerWidget.get()),
                                   this->control->getSettings()->getSidebarWidth());
            gulong* signal_id = new gulong{};
            *signal_id = g_signal_connect_data(
                    this->panedContainerWidget.get(), "size-allocate",
                    xoj::util::wrap_for_g_callback_v<invertPanedPosition>, signal_id,
                    [](gpointer d, GClosure*) { delete reinterpret_cast<gulong*>(d); }, GConnectFlags(0));
        }
    }
}

void MainWindow::setToolbarVisible(bool visible) {
    Settings* settings = control->getSettings();

    settings->setToolbarVisible(visible);
    for (auto& w: this->toolbarWidgets) {
        if (!visible || (gtk_toolbar_get_n_items(GTK_TOOLBAR(w.get())) != 0)) {
            gtk_widget_set_visible(w.get(), visible);
        }
    }
}

void MainWindow::setMenubarVisible(bool visible) {
    gtk_application_window_set_show_menubar(GTK_APPLICATION_WINDOW(this->getWindow()), visible);
}

void MainWindow::setMaximized(bool maximized) { this->maximized = maximized; }

auto MainWindow::isMaximized() const -> bool { return this->maximized; }

auto MainWindow::setFullscreen(bool enabled) const -> void {
    if (enabled) {
        gtk_window_fullscreen(GTK_WINDOW(this->getWindow()));
    } else {
        gtk_window_unfullscreen(GTK_WINDOW(this->getWindow()));
    }
}

auto MainWindow::isDarkTheme() const -> bool { return this->darkMode; }

void MainWindow::buildSurfaceStack() {
    xoj::dashboard::SurfaceStack::Callbacks callbacks;
    // The index is made current before the surface is shown, and the dashboard is told which
    // surface is on screen so it only asks for previews while it is the one being looked at.
    callbacks.prepareHome = [this]() { this->loadDashboardSources(); };
    callbacks.shown = [this](bool home) {
        this->dashboardPage->setActive(home);
        /*
         * Plan 007: a workspace the user has just entered for the first time is where the explanation
         * of it belongs, so it is offered when the editor surface comes up rather than over the
         * dashboard the user is reading files from.
         */
        if (!home) {
            this->showWorkspaceGuidanceIfFresh();
        }
    };

    this->surfaces = std::make_unique<xoj::dashboard::SurfaceStack>(
            GTK_WINDOW(getWindow()), winXournal, this->dashboardPage->getWidget(), std::move(callbacks));

    // The bar goes in first: it carries the one way from the editor to the dashboard, and the stack
    // under it holds both surfaces.
    gtk_box_append(GTK_BOX(get("boxContents")), this->surfaces->getBar());
    gtk_box_append(GTK_BOX(get("boxContents")), this->surfaces->getWidget());
}

void MainWindow::buildDashboard() {
    this->dashboardThumbnailCache =
            std::make_shared<xoj::dashboard::ThumbnailCache>(Util::getCacheSubfolder("thumbnails", true));
    this->dashboardThumbnails = std::make_unique<xoj::dashboard::ThumbnailService>(this->dashboardThumbnailCache);
    this->dashboardModel = std::make_unique<xoj::dashboard::DashboardModel>();

    /*
     * Plan 006, step 7: the files the dashboard shows are watched, and a burst of changes is one
     * rebuild rather than one per event. The rebuild itself happens here, on the main loop: the
     * model is read by widgets, so what the watch does is say that something changed.
     */
    this->dashboardWatcher = std::make_unique<xoj::dashboard::FileWatcher>();
    this->dashboardWatcher->setCallback([this](const std::vector<fs::path>&) { this->loadDashboardSources(); });

    xoj::dashboard::DashboardPage::Callbacks callbacks;

    /*
     * Opening a card opens the file it stands for: the same call the open dialog, the recent menu
     * and the editor use. When it succeeds the editor is shown, because a file the user opened is a
     * file they want to see; when it does not, the dashboard stays and is rebuilt so its card can
     * say what is wrong with the file.
     */
    callbacks.open = [this](const fs::path& path) {
        this->control->openFile(path, [this](bool success) {
            if (success) {
                this->showEditor();
            } else {
                this->loadDashboardSources();
            }
        });
    };
    callbacks.openFile = [this]() { this->askForDashboardOpen(); };
    callbacks.newNote = [this]() {
        this->control->clearSelectionEndText();
        this->control->newFile();
    };
    /*
     * A quick note is a note started without choosing anything: the established way to start a blank
     * document, and then the editor, so that writing something down is what happens next. It asks
     * for no path, because where a note lives is the user's decision and not something to put
     * between them and the thing they wanted to write.
     */
    callbacks.quickNote = [this]() {
        this->control->clearSelectionEndText();
        this->control->newFile();
        this->showEditor();
    };
    callbacks.annotatePdf = [this]() {
        this->control->clearSelectionEndText();
        this->control->askToAnnotatePdf();
    };
    callbacks.addFolder = [this]() { this->askForDashboardFolder(); };

    /*
     * Pinning, forgetting and folder changes write the dashboard's own settings and then rebuild
     * from them. No file is created, moved, renamed or rewritten by any of it: what changes is
     * which paths this list holds.
     */
    callbacks.setPinned = [this](const fs::path& path, bool pinned) {
        Settings* settings = this->control->getSettings();
        const bool changed = pinned ? settings->pinDashboardFile(path) : settings->unpinDashboardFile(path);
        if (changed) {
            this->loadDashboardSources();
        }
    };
    callbacks.forget = [this](const fs::path& path) {
        Settings* settings = this->control->getSettings();
        settings->unpinDashboardFile(path);
        // The desktop's own recent list drops the path as well, so the card does not come back the
        // next time the window is opened. The file itself is left where it is.
        RecentManager::removeRecentFileFilename(path);
        this->loadDashboardSources();
    };
    callbacks.locate = [this](const fs::path& path) {
        // A file that moved: the user points at where it is now, and that path takes its place.
        xoj::OpenDlg::showOpenFileDialog(this->control->getGtkWindow(), this->control->getSettings(),
                                         [this, path](fs::path found) {
                                             if (found.empty()) {
                                                 return;
                                             }
                                             Settings* settings = this->control->getSettings();
                                             settings->unpinDashboardFile(path);
                                             RecentManager::removeRecentFileFilename(path);
                                             settings->pinDashboardFile(found);
                                             this->loadDashboardSources();
                                         });
    };
    callbacks.removeFolder = [this](const fs::path& folder) {
        if (this->control->getSettings()->removeDashboardFolder(folder)) {
            this->loadDashboardSources();
        }
    };
    callbacks.setFolderRecursive = [this](const fs::path& folder, bool recursive) {
        if (this->control->getSettings()->setDashboardFolderRecursive(folder, recursive)) {
            this->loadDashboardSources();
        }
    };

    /*
     * Plan 006, step 6: what Plan 004's inventory found, offered as cards rather than as a question
     * at startup. Reading the inventory parses no document, and nothing here writes to the document
     * a copy came from: the copy is opened, copied or deleted, and the original is left alone.
     */
    callbacks.openRecovery = [this](const xoj::dashboard::RecoveryCard& card) { this->openRecoveredCopy(card); };
    callbacks.saveRecoveryAs = [this](const xoj::dashboard::RecoveryCard& card) { this->saveRecoveredCopyAs(card); };
    callbacks.revealRecovery = [this](const xoj::dashboard::RecoveryCard& card) { this->revealRecoveredCopy(card); };
    callbacks.deleteRecovery = [this](const xoj::dashboard::RecoveryCard& card) { this->deleteRecoveredCopy(card); };
    /*
     * The one thing the dashboard asks about before doing it: deleting a recovered copy cannot be
     * undone. The question is the page's - it is what makes "Delete requires an explicit
     * confirmation" a property of the page - and showing it is this window's.
     */
    callbacks.confirm = [this](const std::string& title, const std::string& message, std::function<void()> confirmed) {
        XojMsgBox::askQuestion(this->control->getGtkWindow(), title, message, {{_("Cancel"), 0}, {_("Delete"), 1}},
                               [confirmed = std::move(confirmed)](int response) {
                                   if (response == 1 && confirmed) {
                                       confirmed();
                                   }
                               });
    };

    callbacks.backToDocument = [this]() { this->showEditor(); };

    this->dashboardPage = std::make_unique<xoj::dashboard::DashboardPage>(*this->dashboardModel, std::move(callbacks),
                                                                          this->dashboardThumbnails.get());
}

void MainWindow::askForDashboardOpen() {
    /*
     * File > Open, and then the editor. The dialog, the question about the document that is open
     * now and the loader are all the established ones; the only thing added is where the user ends
     * up once a file has been opened.
     */
    this->control->close(
            [this](bool closed) {
                if (!closed) {
                    return;
                }
                xoj::OpenDlg::showOpenFileDialog(this->control->getGtkWindow(), this->control->getSettings(),
                                                 [this](fs::path path) {
                                                     if (path.empty()) {
                                                         return;
                                                     }
                                                     this->control->openFileWithoutSavingTheCurrentDocument(
                                                             std::move(path), false, -1, [this](bool success) {
                                                                 if (success) {
                                                                     this->showEditor();
                                                                 }
                                                             });
                                                 });
            },
            true);
}

void MainWindow::askForDashboardFolder() {
    xoj::OpenDlg::showOpenFolderDialog(this->control->getGtkWindow(), this->control->getSettings(),
                                       [this](fs::path folder) {
                                           if (folder.empty()) {
                                               return;
                                           }
                                           if (this->control->getSettings()->addDashboardFolder(folder, false)) {
                                               this->loadDashboardSources();
                                           }
                                       });
}

void MainWindow::loadDashboardSources() {
    Settings* settings = this->control->getSettings();

    this->dashboardModel->setPinnedFiles(settings->getDashboardPinnedFiles());

    std::vector<xoj::dashboard::LibraryFolder> folders;
    for (const DashboardFolder& listed: settings->getDashboardFolders()) {
        xoj::dashboard::LibraryFolder folder;
        folder.path = listed.path;
        folder.displayName = folder.path.filename().string();
        folder.recursive = listed.recursive;
        folder.enabled = true;
        folder.state = xoj::dashboard::LibraryFolder::inspect(folder.path);
        folders.emplace_back(std::move(folder));
    }
    this->dashboardModel->setLibraryFolders(std::move(folders));

    /*
     * What the user was working on comes from the desktop's recent list, whose entries are the
     * files' own paths: a card here is a file the user has, not a copy of one.
     */
    std::vector<fs::path> recent;
    const RecentManager::RecentFiles recentFiles = RecentManager::getRecentFiles();
    for (const auto& group: {std::ref(recentFiles.recentXoppFiles), std::ref(recentFiles.recentPdfFiles)}) {
        for (const auto& info: group.get()) {
            const char* uri = gtk_recent_info_get_uri(info.get());
            if (uri == nullptr) {
                continue;
            }
            if (std::optional<fs::path> path = Util::fromUri(uri); path.has_value()) {
                recent.emplace_back(*path);
            }
        }
    }
    this->dashboardModel->setRecentFiles(std::move(recent));

    // What can be recovered: Plan 004's inventory, read as metadata only, so a refresh costs one
    // look at the autosave folder rather than a document load.
    this->dashboardModel->setRecoveryCandidates(this->control->getRecoveryCandidates());

    this->dashboardModel->refresh();

    /*
     * The dashboard has just been rebuilt from these files, so these are the files to watch: a path
     * that is no longer shown is no longer watched, and one that is new to the list is watched from
     * now on. The value the following watch was built from is passed on so a driver that never
     * changes does not have to be rebuilt for nothing.
     */
    std::vector<fs::path> documents;
    for (const xoj::dashboard::DocumentCard& card: this->dashboardModel->getCards()) {
        if (card.location == xoj::dashboard::DocumentCard::Location::Present) {
            documents.emplace_back(card.path);
        }
    }
    this->dashboardWatcher->watch(documents, this->dashboardModel->getLibraryFolders());

    // The way back says which document it goes back to, so the button is worth pressing before
    // reading what it does not say.
    const fs::path documentPath = this->control->getDocument()->getFilepath();
    this->dashboardPage->setOpenDocument(documentPath.empty() ? _("an unsaved note") :
                                                                documentPath.filename().string());

    this->dashboardPage->refresh();
}

void MainWindow::refreshDashboard() { this->loadDashboardSources(); }

void MainWindow::openRecoveredCopy(const xoj::dashboard::RecoveryCard& card) {
    /*
     * The copy is opened, not restored: the document it came from is not written to, so the user can
     * look at what was recovered, and saving it somewhere is their decision. A copy that cannot be
     * opened leaves the dashboard as it is, with the card saying why.
     */
    this->control->openFile(card.recoveryPath, [this](bool success) {
        if (success) {
            this->showEditor();
        } else {
            this->loadDashboardSources();
        }
    });
}

void MainWindow::saveRecoveredCopyAs(const xoj::dashboard::RecoveryCard& card) {
    // Where the copy came from is what the user thinks of as "the file", so that is the name the
    // chooser starts with; the copy's own name is the fallback for a recovery with no document.
    const fs::path suggested = card.originalPath.empty() ? card.recoveryPath : card.originalPath;
    xoj::SaveExportDialog::showSaveFileDialog(this->control->getGtkWindow(), this->control->getSettings(), suggested,
                                              [this, card](std::optional<fs::path> target) {
                                                  if (!target.has_value()) {
                                                      return;
                                                  }
                                                  this->writeRecoveredCopyTo(card, *target, false);
                                              });
}

void MainWindow::writeRecoveredCopyTo(const xoj::dashboard::RecoveryCard& card, const fs::path& target,
                                      bool allowOriginal) {
    /*
     * Writing over the document a copy came from is the only thing here that replaces something the
     * user has, so it is the only thing that is asked about: every other destination is a place the
     * user named themselves.
     */
    if (!allowOriginal && xoj::dashboard::RecoveryActions::isOriginal(card, target)) {
        XojMsgBox::askQuestion(
                this->control->getGtkWindow(), _("Replace the document?"),
                FS(_F("\"{1}\" is the document this copy was recovered from. Writing the copy there replaces "
                      "what the document holds now.") %
                   target.u8string()),
                {{_("Cancel"), 0}, {_("Replace it"), 1}}, [this, card, target](int response) {
                    if (response == 1) {
                        this->writeRecoveredCopyTo(card, target, true);
                    }
                });
        return;
    }

    std::string error;
    if (!xoj::dashboard::RecoveryActions::copyTo(card, target, allowOriginal, error)) {
        XojMsgBox::showErrorToUser(this->control->getGtkWindow(), error);
        return;
    }

    // The copy is written and the recovery copy is still there: it is deleted when the user says so,
    // never as a side effect of having saved it once.
    this->loadDashboardSources();
}

void MainWindow::revealRecoveredCopy(const xoj::dashboard::RecoveryCard& card) {
    /*
     * Where the recovered work is: the folder the document is in when it is still there, and the
     * folder the copy itself is in otherwise. The folder is opened rather than the file, because
     * opening the file would open a document, which is not what "show me where it is" means.
     */
    std::error_code code;
    const bool original = !card.originalPath.empty() && fs::exists(card.originalPath, code);
    const fs::path file = original ? card.originalPath : card.recoveryPath;
    const fs::path folder = file.parent_path();
    if (folder.empty()) {
        return;
    }

    const std::optional<std::string> uri = Util::toUri(folder);
    if (!uri.has_value()) {
        XojMsgBox::showErrorToUser(this->control->getGtkWindow(),
                                   FS(_F("Cannot open the folder of \"{1}\"") % file.u8string()));
        return;
    }

    GError* error = nullptr;
    if (!gtk_show_uri_on_window(this->control->getGtkWindow(), uri->c_str(), GDK_CURRENT_TIME, &error)) {
        const std::string message =
                error != nullptr ?
                        FS(_F("Cannot open the folder of \"{1}\":\n{2}") % file.u8string() % error->message) :
                        FS(_F("Cannot open the folder of \"{1}\"") % file.u8string());
        if (error != nullptr) {
            g_error_free(error);
        }
        XojMsgBox::showErrorToUser(this->control->getGtkWindow(), message);
    }
}

void MainWindow::deleteRecoveredCopy(const xoj::dashboard::RecoveryCard& card) {
    std::string error;
    if (!xoj::dashboard::RecoveryActions::removeCopy(card, error)) {
        // A copy that could not be deleted must not look deleted: the failure is said out loud and
        // the card is rebuilt from what is actually there.
        XojMsgBox::showErrorToUser(this->control->getGtkWindow(), error);
    }

    this->loadDashboardSources();
}

void MainWindow::showHome() {
    // What the surface does is the stack's: this is only the entry point the action, the tests and
    // the rest of the window use.
    this->surfaces->showHome();
}

void MainWindow::showEditor() { this->surfaces->showEditor(); }

auto MainWindow::isHomeShown() const -> bool { return this->surfaces != nullptr && this->surfaces->isHomeShown(); }

auto MainWindow::getDashboardPage() const -> xoj::dashboard::DashboardPage* { return this->dashboardPage.get(); }

auto MainWindow::getSurfaceStack() const -> GtkWidget* {
    return this->surfaces != nullptr ? this->surfaces->getWidget() : nullptr;
}

auto MainWindow::getHomeButton() const -> GtkWidget* {
    return this->surfaces != nullptr ? this->surfaces->getHomeButton() : nullptr;
}

auto MainWindow::getXournal() const -> XournalView* { return xournal.get(); }

auto MainWindow::windowMaximizedCallback(GObject* window, GParamSpec*, MainWindow* win) -> void {
    win->setMaximized(gtk_window_is_maximized(GTK_WINDOW(window)));
}

void MainWindow::toolbarSelected(const std::string& id) {
    const auto& toolbars = toolbar->getModel()->getToolbars();
    auto it = std::find_if(toolbars.begin(), toolbars.end(), [&](const auto& d) { return d->getId() == id; });
    toolbarSelected(it == toolbars.end() ? nullptr : it->get());
}

void MainWindow::toolbarSelected(ToolbarData* d) {
    if (!d || this->selectedToolbar == d) {
        return;
    }

    Settings* settings = control->getSettings();
    settings->setSelectedToolbar(d->getId());

    this->clearToolbar();
    this->loadToolbar(d);
}

auto MainWindow::clearToolbar() -> const ToolbarData* {
    if (this->selectedToolbar != nullptr) {
        for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
            ToolMenuHandler::unloadToolbar(this->toolbarWidgets[i].get());
        }

        this->toolbar->freeDynamicToolbarItems();
    }
    return std::exchange(this->selectedToolbar, nullptr);
}

void MainWindow::loadToolbar(ToolbarData* d) {
    this->selectedToolbar = d;

    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        this->toolbar->load(d, this->toolbarWidgets[i].get(), TOOLBAR_DEFINITIONS[i].propName,
                            TOOLBAR_DEFINITIONS[i].horizontal);
    }

    this->floatingToolbox->flagRecalculateSizeRequired();
}

void MainWindow::reloadToolbars() {
    ToolbarData* d = getSelectedToolbar();
    this->clearToolbar();
    this->toolbarSelected(d);
}

auto MainWindow::getSelectedToolbar() const -> ToolbarData* { return this->selectedToolbar; }

auto MainWindow::getToolbarWidgets() const -> const ToolbarWidgetArray& { return toolbarWidgets; }

auto MainWindow::getToolbarName(GtkToolbar* toolbar) const -> const char* {
    for (size_t i = 0; i < TOOLBAR_DEFINITIONS_LEN; i++) {
        if (static_cast<void*>(this->toolbarWidgets[i].get()) == static_cast<void*>(toolbar)) {
            return TOOLBAR_DEFINITIONS[i].propName;
        }
    }

    return "";
}

void MainWindow::setDynamicallyGeneratedSubmenuDisabled(bool disabled) { menubar->setDisabled(disabled); }

void MainWindow::updateToolbarMenu() {
    menubar->getToolbarSelectionSubmenu().update(toolbar.get(), this->selectedToolbar);
}

void MainWindow::updateWorkspaceMenu() { menubar->getWorkspaceSubmenu().update(); }

void MainWindow::setWorkspace(WorkspaceMode mode) {
    control->getSettings()->setWorkspaceMode(mode);
    applyWorkspaceChrome();
}

void MainWindow::applyWorkspaceChrome() {
    Settings* settings = control->getSettings();

    // The toolbar of the active workspace: the Focus preset, unless the user picked another
    // layout while in Focus. Classic restores the toolbar it was left with.
    toolbarSelected(settings->getSelectedToolbar());
    // toolbarSelected() loads the layout but leaves the View > Toolbar radio on the previous
    // workspace's choice, so the menu is refreshed to match the layout just loaded.
    updateToolbarMenu();

    // Focus hides the traditional menubar; it stays reachable with F10, the View menu and
    // (while the menubar is hidden) the Settings dialog.
    control->setShowMenubar(settings->isMenubarVisible());

    updateWorkspaceMenu();
}

void MainWindow::createToolbar() {
    toolbarSelected(control->getSettings()->getSelectedToolbar());

    this->control->getScheduler()->unblockRerenderZoom();
}

void MainWindow::updatePageNumbers(size_t page, size_t pagecount, size_t pdfpage) {
    toolbar->setPageInfo(page, pagecount, pdfpage);
}

void MainWindow::updateSafetyStatus(const xoj::safety::SafetySnapshot& snapshot) {
    if (this->safetyStatusBar) {
        this->safetyStatusBar->update(snapshot);
    }
}

auto MainWindow::getMenubar() const -> Menubar* { return menubar.get(); }

void MainWindow::show(GtkWindow* parent) { gtk_widget_show(this->window); }

void MainWindow::setUndoDescription(const string& description) { menubar->setUndoDescription(description); }

void MainWindow::setRedoDescription(const string& description) { menubar->setRedoDescription(description); }

auto MainWindow::getToolbarModel() const -> ToolbarModel* { return this->toolbar->getModel(); }

auto MainWindow::getToolMenuHandler() const -> ToolMenuHandler* { return this->toolbar.get(); }

void MainWindow::loadMainCSS(GladeSearchpath* gladeSearchPath, const gchar* cssFilename) {
    auto filepath = gladeSearchPath->findFile("", cssFilename);
    xoj::util::GObjectSPtr<GtkCssProvider> provider(gtk_css_provider_new(), xoj::util::adopt);
    gtk_css_provider_load_from_path(provider.get(), char_cast(filepath.u8string().c_str()), nullptr);
    gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider.get()),
                                              GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

PdfFloatingToolbox* MainWindow::getPdfToolbox() const { return this->pdfFloatingToolBox.get(); }

FloatingToolbox* MainWindow::getFloatingToolbox() const { return this->floatingToolbox.get(); }

xoj::gui::QuickPalette* MainWindow::getQuickPalette() const { return this->quickPalette.get(); }

namespace {
/// Plan 008: the palette's own labels. This tree has no shared tool display-name helper, so the
/// few tools the palette offers are named here, and anything else falls back to its stable id.
auto quickPaletteToolLabel(ToolType tool) -> std::string {
    switch (tool) {
        case TOOL_PEN:
            return _("Pen");
        case TOOL_HIGHLIGHTER:
            return _("Highlighter");
        case TOOL_ERASER:
            return _("Eraser");
        case TOOL_SELECT_RECT:
            return _("Select rectangle");
        case TOOL_SELECT_REGION:
            return _("Select region");
        case TOOL_HAND:
            return _("Hand");
        default:
            return std::string(toolTypeToString(tool));
    }
}
}  // namespace

void MainWindow::showQuickPaletteAt(int x, int y) {
    if (this->quickPalette == nullptr) {
        return;
    }
    // Read the contents now: the favourites may have changed since the last time it was shown.
    this->quickPalette->setButtons(this->buildQuickPaletteButtons());
    this->quickPalette->showAt(static_cast<double>(x), static_cast<double>(y));
}

auto MainWindow::buildQuickPaletteButtons() -> std::vector<xoj::gui::QuickPaletteButton> {
    ToolHandler* handler = this->control->getToolHandler();
    Settings* settings = this->control->getSettings();

    xoj::gui::QuickPaletteSlotsInput input;
    input.currentTool = handler != nullptr ? handler->getToolType() : TOOL_NONE;
    // This tree keeps no record of the tool the user had before this one, so there is nothing to
    // offer to go back to; the slot exists and is tested, the source does not exist here yet.
    input.previousTool = TOOL_NONE;
    input.favorites = settings != nullptr ? &settings->getToolPresets() : nullptr;

    std::vector<xoj::gui::QuickPaletteButton> buttons;
    for (const xoj::gui::QuickPaletteSlot& slot: xoj::gui::buildQuickPaletteSlots(input)) {
        xoj::gui::QuickPaletteButton button;
        button.id = slot.id;

        switch (slot.kind) {
            case xoj::gui::QuickPaletteSlot::Kind::Undo:
                button.label = _("Undo");
                button.actionName = std::string("win.") + Action_toString(Action::UNDO);
                break;
            case xoj::gui::QuickPaletteSlot::Kind::Favorite: {
                // A preset is applied through the preset adapter, the same path the toolbar's
                // favourite items take; there is no action carrying a preset id.
                button.label = slot.name;
                const std::string presetId = slot.presetId;
                button.onActivate = [this, presetId]() {
                    ToolConfigAdapter* adapter = this->control->getToolConfigAdapter();
                    const ToolPreset* preset = this->control->getSettings()->getToolPresets().findById(presetId);
                    if (adapter != nullptr && preset != nullptr) {
                        adapter->applyPreset(*preset);
                    }
                };
                break;
            }
            default:
                // Tool slots go through the same action the toolbar's tool buttons use, with the
                // tool type as the parameter, so selection takes the normal path.
                button.label = quickPaletteToolLabel(slot.toolType);
                button.actionName = std::string("win.") + Action_toString(Action::SELECT_TOOL);
                button.parameter = g_variant_new_uint64(static_cast<guint64>(slot.toolType));
                break;
        }

        buttons.push_back(std::move(button));
    }
    return buttons;
}


void MainWindow::setDPI() const {
    if (auto dpi = this->getControl()->getSettings()->getDisplayDpi(); dpi == -1) {
        auto res = xoj::util::gtk::getWidgetDPI(this->window);
        this->getControl()->getZoomControl()->setZoom100Value(res.value_or(Util::DPI_NORMALIZATION_FACTOR) /
                                                              Util::DPI_NORMALIZATION_FACTOR);
    } else {
        this->getControl()->getZoomControl()->setZoom100Value(dpi / Util::DPI_NORMALIZATION_FACTOR);
    }
}

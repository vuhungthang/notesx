/*
 * Xournal++
 *
 * The Main window
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>    // for array
#include <atomic>   // for atomic_bool
#include <cstddef>  // for size_t
#include <memory>   // for unique_ptr
#include <string>   // for string

#include <gdk/gdk.h>      // for GdkDragContext, GdkEvent
#include <glib-object.h>  // for GClosure
#include <glib.h>         // for gpointer, gboolean, gint
#include <gtk/gtk.h>      // for GtkWidget, GtkCheckMenu...

#include "control/DocumentSafetyState.h"     // for SafetySnapshot
#include "control/commands/CommandRegistry.h"  // for CommandRegistry, the commands the palette lists
#include "control/settings/SettingsEnums.h"  // for WorkspaceMode
#include "dashboard/DashboardModel.h"        // for DashboardModel
#include "dashboard/DashboardPage.h"         // for DashboardPage
#include "dashboard/FileWatcher.h"           // for FileWatcher
#include "dashboard/ThumbnailService.h"      // for ThumbnailService
#include "gui/dashboard/SurfaceStack.h"      // for SurfaceStack
#include "util/Point.h"
#include "util/raii/GObjectSPtr.h"

#include "GladeGui.h"            // for GladeGui
#include "ToolbarDefinitions.h"  // for TOOLBAR_DEFINITIONS_LEN

class Control;
class Layout;
class SpinPageAdapter;
class ScrollHandling;
class ToolMenuHandler;
class ToolbarData;
class ToolbarModel;
class XournalView;
class PdfFloatingToolbox;
class FloatingToolbox;
class GladeSearchpath;
class SafetyStatusBar;

class Menubar;

namespace xoj::command {
class CommandPalette;
}

namespace xoj::gui {
class WorkspaceGuidance;
class ShortcutReference;
class TipService;
}  // namespace xoj::gui

typedef std::array<xoj::util::WidgetSPtr, TOOLBAR_DEFINITIONS_LEN> ToolbarWidgetArray;

class MainWindow: public GladeGui {
public:
    MainWindow(GladeSearchpath* gladeSearchPath, Control* control, GtkApplication* parent);
    ~MainWindow() override;

    void populate(GladeSearchpath* gladeSearchPath);

public:
    GMenuModel* getMenuModel() const;

    void show(GtkWindow* parent) override;

    void toolbarSelected(const std::string& id);
    void toolbarSelected(ToolbarData* d);
    ToolbarData* getSelectedToolbar() const;

private:
    const ToolbarData* clearToolbar();
    void loadToolbar(ToolbarData* d);

public:
    /**
     * reloadToolbars reloads the currently selected toolbar
     *
     * This is especially useful when a change in the SettingsDialog should be reflected right away
     */
    void reloadToolbars();

    void updatePageNumbers(size_t page, size_t pagecount, size_t pdfpage);

    void setMaximized(bool maximized);
    bool isMaximized() const;

    void setFullscreen(bool enabled) const;

    bool isDarkTheme() const;

    XournalView* getXournal() const;

    void setMenubarVisible(bool visible);
    void setSidebarVisible(bool visible);
    void setToolbarVisible(bool visible);

    Control* getControl() const;

    PdfFloatingToolbox* getPdfToolbox() const;
    FloatingToolbox* getFloatingToolbox() const;

    void updateScrollbarSidebarPosition();

    void setUndoDescription(const std::string& description);
    void setRedoDescription(const std::string& description);

    /**
     * Plan 004: show the document's safety state. The window renders what the safety model says
     * and decides nothing about it.
     */
    void updateSafetyStatus(const xoj::safety::SafetySnapshot& snapshot);

    ToolbarModel* getToolbarModel() const;
    ToolMenuHandler* getToolMenuHandler() const;

    void setDynamicallyGeneratedSubmenuDisabled(bool disabled);

    void updateToolbarMenu();
    void updateWorkspaceMenu();
    void updateColorscheme();

    /**
     * Plan 002: switch the workspace and apply its chrome. The switch is immediate: no
     * restart is needed and the previous workspace's selections are remembered.
     */
    void setWorkspace(WorkspaceMode mode);

    /**
     * Apply the chrome of the active workspace: its toolbar selection and its menubar
     * preference. The sidebar, fullscreen and presentation behaviour are left alone; they
     * stay user controlled.
     */
    void applyWorkspaceChrome();

    const ToolbarWidgetArray& getToolbarWidgets() const;
    const char* getToolbarName(GtkToolbar* toolbar) const;

    Layout* getLayout() const;

    [[maybe_unused]] Menubar* getMenubar() const;

    /**
     * Disable kinetic scrolling if there is a touchscreen device that was manually mapped to another enabled input
     * device class. This is required so the GtkScrolledWindow does not swallow all the events.
     */
    void setGtkTouchscreenScrollingForDeviceMapping();
    void setGtkTouchscreenScrollingEnabled(bool enabled);

    /**
     * Plan 006: show the home surface.
     *
     * The dashboard is an index over the files the user has, so showing it re-reads the recent
     * list, the pins, the folders and the recovery inventory and rebuilds its cards. Switching
     * between the two surfaces changes which one is visible and nothing else: the editor keeps its
     * document, its page, its zoom and its unsaved changes because neither surface is created or
     * destroyed by a switch.
     */
    void showHome();
    /// Go back to the document the editor holds, exactly as it was left.
    void showEditor();
    auto isHomeShown() const -> bool;
    /// Rebuild the dashboard's cards from the files the user has, and ask for the previews it lacks.
    void refreshDashboard();

    /// Plan 006: the dashboard, so its sections and cards can be looked at without a screen.
    [[maybe_unused]] auto getDashboardPage() const -> xoj::dashboard::DashboardPage*;
    /// Plan 006: the stack the editor and the dashboard are the two pages of.
    [[maybe_unused]] auto getSurfaceStack() const -> GtkWidget*;
    /// Plan 006: the button that leads from the editor to the dashboard.
    [[maybe_unused]] auto getHomeButton() const -> GtkWidget*;

    /**
     * Plan 007: every command the application offers, read where it already lives - the menu model
     * GTK built from ui/mainmenubar.xml, the toolbar items of the tool menu handler, the
     * accelerators the application holds - and nothing written down a second time. The palette and
     * the shortcut reference both read it, so neither can drift from what the application does.
     */
    auto buildCommandRegistry() const -> xoj::command::CommandRegistry;
    /// Plan 007: show the command palette. Ctrl+K reaches this through the action of the same name.
    void showCommandPalette();
    /// Plan 007: the palette itself, so a test can press the keys a user would.
    [[maybe_unused]] auto getCommandPalette() const -> xoj::command::CommandPalette*;

    /**
     * Plan 007: what a fresh profile is told about the workspace it starts in, so a test can read it
     * and put it away the way a user would.
     */
    [[maybe_unused]] auto getWorkspaceGuidance() const -> xoj::gui::WorkspaceGuidance*;

    /// Plan 007, step 5: the shortcut reference. The action of the same name opens it.
    void showShortcutReference();
    /// Plan 007: the reference itself, so a test can read what it shows.
    [[maybe_unused]] auto getShortcutReference() const -> xoj::gui::ShortcutReference*;

    /**
     * Plan 007, step 4: the tips of this window.
     *
     * The service belongs to the window because that is what the widgets a tip is about have in
     * common: a tool's property popover does not know this object, and the window it is anchored in
     * is where the service is found from it.
     */
    [[maybe_unused]] auto getTipService() const -> xoj::gui::TipService*;

    /// Infer the window's DPI from available monitor info and use it to set the default zoom value.
    void setDPI() const;

private:
    void initXournalWidget();

    /// Plan 006: the editor and the home surface as two pages of one stack.
    void buildSurfaceStack();
    /// Plan 007: the palette, over the window it belongs to.
    void buildCommandPalette();
    /// Plan 007: the one-time explanation of the workspace, and when it is shown.
    void buildWorkspaceGuidance();
    void showWorkspaceGuidanceIfFresh();
    /// Plan 007, step 5: the shortcut reference, over the window it describes.
    void buildShortcutReference();
    /// Plan 007, step 4: the one-time tips, told against whatever they are about.
    void buildTipService();
    /// Plan 006: the dashboard's model, preview service and page, wired to the application.
    void buildDashboard();
    /// Read what the user has into the model and rebuild the page.
    void loadDashboardSources();
    /// Ask the user for a file to open, and show the editor once one has been opened.
    void askForDashboardOpen();
    /// Ask the user for a folder to list on the dashboard.
    void askForDashboardFolder();

    /*
     * Plan 006, step 6: what a recovery card does. Every one of these leaves the document a copy was
     * recovered from exactly as it is, except for the one path the user themselves chose as the
     * destination of a save, which is asked about first.
     */
    void openRecoveredCopy(const xoj::dashboard::RecoveryCard& card);
    void saveRecoveredCopyAs(const xoj::dashboard::RecoveryCard& card);
    void writeRecoveredCopyTo(const xoj::dashboard::RecoveryCard& card, const fs::path& target, bool allowOriginal);
    void revealRecoveredCopy(const xoj::dashboard::RecoveryCard& card);
    void deleteRecoveredCopy(const xoj::dashboard::RecoveryCard& card);

    void createToolbar();

    /**
     * Update the position of the separator in the paned container, adjusting it to the saved sidebar width.
     * @param contentWidth should be the width of the paned container. The caller should retrieve the width
     * of the container before any modifications to it, as that will reset its allocation.
     */
    void updatePanedPosition(int contentWidth);

    /**
     * Window close Button is pressed
     */
    static bool deleteEventCallback(GtkWidget* widget, GdkEvent* event, Control* control);

    /**
     * Window is maximized/minimized
     */
    static void windowMaximizedCallback(GObject* window, GParamSpec*, MainWindow* win);

    /**
     * Callback for drag & drop files
     */
    static void dragDataRecived(GtkWidget* widget, GdkDragContext* dragContext, gint x, gint y, GtkSelectionData* data,
                                guint info, guint time, MainWindow* win);

    /**
     * Load Overall CSS file with custom icons, other styling and potentially, user changes
     */
    static void loadMainCSS(GladeSearchpath* gladeSearchPath, const gchar* cssFilename);

private:
    Control* control;

    /// The two GtkSettings subscriptions `updateColorscheme()` needs. The settings outlive this
    /// window and hold it as their handler data, so the handlers have to come off them before the
    /// window goes: see the destructor.
    gulong themeNameHandlerId = 0;
    gulong darkThemeHandlerId = 0;

    std::unique_ptr<XournalView> xournal;
    GtkWidget* winXournal = nullptr;
    std::unique_ptr<ScrollHandling> scrollHandling;

    std::atomic_bool gtkTouchscreenScrollingEnabled{true};

    std::unique_ptr<PdfFloatingToolbox> pdfFloatingToolBox;
    std::unique_ptr<FloatingToolbox> floatingToolbox;

    // Toolbars
    std::unique_ptr<ToolMenuHandler> toolbar;
    ToolbarData* selectedToolbar = nullptr;

    std::unique_ptr<Menubar> menubar;

    /// Plan 007: the command palette, created once and shown and hidden from then on.
    std::unique_ptr<xoj::command::CommandPalette> commandPalette;

    /// Plan 007: the one-time explanation of the workspace, created once with the palette.
    std::unique_ptr<xoj::gui::WorkspaceGuidance> workspaceGuidance;

    /// Plan 007, step 5: the shortcut reference, created once with the palette.
    std::unique_ptr<xoj::gui::ShortcutReference> shortcutReference;

    /// Plan 007, step 4: the tips, created once with the palette and owned by the window.
    std::unique_ptr<xoj::gui::TipService> tipService;

    /// Plan 004: the document-safety row, directly under the top toolbars.
    std::unique_ptr<SafetyStatusBar> safetyStatusBar;

    /*
     * Plan 006: the home surface. The stack holds the editor widget and the dashboard as its two
     * pages, the bar under it holds the one button that leads from the editor to the dashboard, and
     * the model, the watcher, the preview service and the page hold nothing but the paths the user's
     * own files are reached by.
     */
    std::unique_ptr<xoj::dashboard::SurfaceStack> surfaces;
    /// Plan 006, step 7: the files the dashboard shows, and when one of them changed.
    std::unique_ptr<xoj::dashboard::FileWatcher> dashboardWatcher;
    std::shared_ptr<xoj::dashboard::ThumbnailCache> dashboardThumbnailCache;
    std::unique_ptr<xoj::dashboard::ThumbnailService> dashboardThumbnails;
    std::unique_ptr<xoj::dashboard::DashboardModel> dashboardModel;
    std::unique_ptr<xoj::dashboard::DashboardPage> dashboardPage;

    bool maximized = false;
    bool darkMode = false;
    bool modifiedGtkSettingsTheme = false;

    ToolbarWidgetArray toolbarWidgets;

    bool sidebarVisible = true;

    /// The last monitor the window has been moved to -- used for setting dpi
    GdkMonitor* lastMonitor = nullptr;

    xoj::util::WidgetSPtr boxContainerWidget;
    xoj::util::WidgetSPtr panedContainerWidget;
    xoj::util::WidgetSPtr mainContentWidget;
    xoj::util::WidgetSPtr sidebarWidget;
};

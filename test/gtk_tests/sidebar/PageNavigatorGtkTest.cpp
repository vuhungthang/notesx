/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "gui/Builder.h"
#include "gui/GladeSearchpath.h"
#include "gui/dialog/ExportDialog.h"
#include "gui/sidebar/previews/page/SidebarPreviewPages.h"  // for SidebarPreviewPages

#include "../dialog/GtkTest.h"
#include "config-test.h"

/*
 * Plan 005, step 6 and the done criteria of Plan 005: the page navigator's toolbar, its context
 * menu and the export range it hands to the export dialog.
 *
 * The navigator itself needs a Control, a document and a canvas, so what is checked here is the
 * part that is reachable without them: the controls of ui/sidebar.ui, the actions they are wired
 * to, the words they carry, and the range the export dialog starts from when several pages are
 * selected. The selection, reorder and duplication arithmetic is covered by the unit tests of
 * PageSelectionModel.
 */

namespace {

/// The widths the plan asks the navigator to stay usable at.
constexpr int NARROW_SIDEBAR_WIDTH = 150;
constexpr int MEDIUM_SIDEBAR_WIDTH = 280;
constexpr int WIDE_SIDEBAR_WIDTH = 500;

constexpr auto PAGE_TOOLBAR_ID = "PreviewPagesToolbar";
constexpr auto PAGE_MENU_ID = "PreviewPagesContextMenu";

/// The prominent, pen-sized Add page control of Plan 009.
constexpr auto ADD_PAGE_BUTTON_ID = "btPagesAdd";

/// Every widget at or below `root`, in tree order.
void collectWidgets(GtkWidget* root, std::vector<GtkWidget*>& out) {
    out.emplace_back(root);
    if (GTK_IS_CONTAINER(root)) {
        for (GList* children = gtk_container_get_children(GTK_CONTAINER(root)); children != nullptr;
             children = children->next) {
            collectWidgets(GTK_WIDGET(children->data), out);
        }
    }
}

auto allWidgets(GtkWidget* root) -> std::vector<GtkWidget*> {
    std::vector<GtkWidget*> widgets;
    collectWidgets(root, widgets);
    return widgets;
}

auto accessibleName(GtkWidget* widget) -> std::string {
    const char* name = atk_object_get_name(gtk_widget_get_accessible(widget));
    return name != nullptr ? std::string(name) : std::string();
}

auto hasCssClass(GtkWidget* widget, const char* name) -> bool {
    return gtk_style_context_has_class(gtk_widget_get_style_context(widget), name);
}

/// The controls of the toolbar, in the order they are laid out.
auto buttonsOf(GtkWidget* root) -> std::vector<GtkWidget*> {
    std::vector<GtkWidget*> buttons;
    for (GtkWidget* widget: allWidgets(root)) {
        if (GTK_IS_BUTTON(widget)) {
            buttons.emplace_back(widget);
        }
    }
    return buttons;
}

/**
 * The widget GtkBuilder named `id`.
 *
 * GtkBuilder names every widget it builds after the id it was given, which is what lets the test
 * reach a control that the code keeps private - the export dialog's page range field, for instance.
 */
auto byId(GtkWidget* root, const char* id) -> GtkWidget* {
    for (GtkWidget* widget: allWidgets(root)) {
        const char* name = gtk_widget_get_name(widget);
        if (name != nullptr && std::string(name) == id) {
            return widget;
        }
    }
    return nullptr;
}

/// Lets GTK finish what it queued, so the widget tree settles into the state a user would see.
void settle() {
    while (g_main_context_iteration(nullptr, FALSE)) {}
}

auto minimumWidth(GtkWidget* widget) -> int {
    int minimum = 0;
    gtk_widget_get_preferred_width(widget, &minimum, nullptr);
    return minimum;
}

struct MenuItem {
    std::string label;
    std::string action;
};

void collectMenuItems(GMenuModel* model, std::vector<MenuItem>& out) {
    const int count = g_menu_model_get_n_items(model);
    for (int i = 0; i < count; i++) {
        GMenuModel* section = g_menu_model_get_item_link(model, i, G_MENU_LINK_SECTION);
        if (section != nullptr) {
            collectMenuItems(section, out);
            g_object_unref(section);
            continue;
        }

        MenuItem item;
        char* label = nullptr;
        char* action = nullptr;
        if (g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_LABEL, "s", &label)) {
            item.label = label;
            g_free(label);
        }
        if (g_menu_model_get_item_attribute(model, i, G_MENU_ATTRIBUTE_ACTION, "s", &action)) {
            item.action = action;
            g_free(action);
        }
        out.emplace_back(std::move(item));
    }
}

auto menuItems(GMenuModel* model) -> std::vector<MenuItem> {
    std::vector<MenuItem> items;
    collectMenuItems(model, items);
    return items;
}

/// The whole of `ui/xournalpp.css`, which is where the navigator's semantic classes are defined.
auto applicationStylesheet() -> std::string {
    const std::filesystem::path path = std::filesystem::path(GET_UI_FOLDER) / "xournalpp.css";
    std::ifstream file(path);
    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

/// Counts the GLib criticals logged while it is alive, so a teardown that unrefs too much fails.
class CriticalWatch {
public:
    CriticalWatch() { this->previous = g_log_set_default_handler(onLog, this); }
    ~CriticalWatch() { g_log_set_default_handler(this->previous, nullptr); }

    CriticalWatch(const CriticalWatch&) = delete;
    auto operator=(const CriticalWatch&) -> CriticalWatch& = delete;

    auto count() const -> std::size_t { return this->criticals.size(); }

    auto report() const -> std::string {
        std::string all;
        for (const std::string& message: this->criticals) {
            all += message;
            all += '\n';
        }
        return all.empty() ? std::string("no critical was logged") : all;
    }

private:
    static void onLog(const gchar* domain, GLogLevelFlags levels, const gchar* message, gpointer data) {
        auto* self = static_cast<CriticalWatch*>(data);
        if ((levels & G_LOG_LEVEL_CRITICAL) != 0) {
            self->criticals.emplace_back(message != nullptr ? message : "");
        }
        if (self->previous != nullptr) {
            self->previous(domain, levels, message, nullptr);
        }
    }

    std::vector<std::string> criticals;
    GLogFunc previous = nullptr;
};

/**
 * The actions the page navigator's controls name, as the window offers them.
 *
 * Every activation is counted, so a test can press a control and see which action the application
 * would have run - the same path a click or the keyboard takes.
 */
class ActionSpy {
public:
    /**
     * Offer the page actions on `window`, the way MainWindow does: a GtkApplicationWindow is a
     * GActionMap, and what is added to it is what the "win." prefix of a control resolves to.
     */
    void installOn(GtkApplicationWindow* window) {
        static constexpr const char* names[] = {"new-page-before",
                                                "new-page-after",
                                                "duplicate-page",
                                                "move-page-towards-beginning",
                                                "move-page-towards-end",
                                                "delete-page",
                                                "export-as"};

        for (const char* name: names) {
            GSimpleAction* action = g_simple_action_new(name, nullptr);
            g_signal_connect(action, "activate",
                             G_CALLBACK(+[](GSimpleAction* activated, GVariant*, gpointer data) {
                                 auto* self = static_cast<ActionSpy*>(data);
                                 self->activations[g_action_get_name(G_ACTION(activated))]++;
                             }),
                             this);
            g_action_map_add_action(G_ACTION_MAP(window), G_ACTION(action));
            g_object_unref(action);
        }
    }

    auto count(const std::string& action) const -> int {
        auto it = this->activations.find(action);
        return it == this->activations.end() ? 0 : it->second;
    }

private:
    std::map<std::string, int> activations;
};

/// The page toolbar, built from the application's own ui file.
struct PageToolbar {
    GladeSearchpath gladeSearchPath;
    std::unique_ptr<Builder> builder;

    PageToolbar() {
        this->gladeSearchPath.addSearchDirectory(GET_UI_FOLDER);
        this->builder = std::make_unique<Builder>(&this->gladeSearchPath, "sidebar.ui");
    }

    auto toolbar() -> GtkWidget* { return this->builder->get(PAGE_TOOLBAR_ID); }

    auto menu() -> GMenuModel* { return G_MENU_MODEL(this->builder->get<GObject>(PAGE_MENU_ID)); }
};

}  // namespace

/*
 * Plan 005, step 6: the toolbar carries the page actions and the density choice, and every
 * control is one the keyboard can reach and a screen reader can name.
 */
class PageToolbarStructureTest: public GtkTest {
    void runTest(GtkApplication*) override {
        PageToolbar pageToolbar;
        GtkWidget* toolbar = pageToolbar.toolbar();
        ASSERT_NE(toolbar, nullptr);
        ASSERT_TRUE(GTK_IS_BOX(toolbar));

        // Two rows: the page actions, then the density choice. One row would clip at 150px.
        EXPECT_EQ(gtk_orientable_get_orientation(GTK_ORIENTABLE(toolbar)), GTK_ORIENTATION_VERTICAL)
                << "the toolbar has to fit the narrowest sidebar";
        EXPECT_TRUE(hasCssClass(toolbar, "toolbar"));

        const std::vector<GtkWidget*> controls = buttonsOf(toolbar);
        ASSERT_EQ(controls.size(), 7U) << "four page actions, two density modes and the Add page control";

        // The Add page control (Plan 009) is the one control that is not a compact icon button:
        // it is the pen-sized row at the bottom, and it stays that size for the reason above.
        GtkWidget* addPage = byId(toolbar, ADD_PAGE_BUTTON_ID);
        ASSERT_NE(addPage, nullptr) << "the toolbar has to carry the Add page control";

        // The navigator names its own controls, which is what a screen reader reads.
        SidebarPreviewPages::applyToolbarAccessibility(toolbar);

        for (GtkWidget* control: controls) {
            // A keyboard user has to be able to reach every one of them.
            EXPECT_TRUE(gtk_widget_get_can_focus(control))
                    << "control " << gtk_widget_get_name(control) << " is not reachable by keyboard";

            // Icon-only controls: the accessible name is the only thing a reader has.
            EXPECT_FALSE(accessibleName(control).empty())
                    << "control " << gtk_widget_get_name(control) << " has no accessible name";

            // ... and it is the words the user sees on hover, so the two cannot drift apart.
            const char* tooltip = gtk_widget_get_tooltip_text(control);
            ASSERT_NE(tooltip, nullptr) << gtk_widget_get_name(control) << " has no tooltip";
            EXPECT_EQ(accessibleName(control), std::string(tooltip));
            EXPECT_TRUE(gtk_widget_get_has_tooltip(control)) << "a tooltip that never shows helps nobody";

            // Plan 001's dense layout class, which is what makes a row of four fit 150px. The
            // Add page control is deliberately not compact: it is the large touch target.
            if (control != addPage) {
                EXPECT_TRUE(hasCssClass(control, "xoj-control-compact"));
            }
        }

        // The density choice is two toggles, and it is the only choice in the toolbar.
        std::vector<GtkWidget*> toggles;
        for (GtkWidget* control: controls) {
            if (GTK_IS_TOGGLE_BUTTON(control)) {
                toggles.emplace_back(control);
            }
        }
        ASSERT_EQ(toggles.size(), 2U);

        // The density toggles are wired in code, not through an action: they are a UI preference.
        for (GtkWidget* toggle: toggles) {
            EXPECT_EQ(gtk_actionable_get_action_name(GTK_ACTIONABLE(toggle)), nullptr);
        }
    }
};
TEST_F(PageToolbarStructureTest, theToolbarOffersThePageActionsAndTheDensityChoice) {}

/*
 * Plan 005, step 4 and the done criterion "existing page actions still work for one page": the
 * toolbar's controls are wired to the actions the application offers, and activating one runs it.
 */
class PageToolbarActionsTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);

        ActionSpy spy;
        spy.installOn(GTK_APPLICATION_WINDOW(window));

        PageToolbar pageToolbar;
        gtk_container_add(GTK_CONTAINER(window), pageToolbar.toolbar());
        gtk_widget_show_all(window);
        settle();

        const std::vector<std::pair<const char*, const char*>> expected = {
                {"btPagesMoveUp", "move-page-towards-beginning"},
                {"btPagesMoveDown", "move-page-towards-end"},
                {"btPagesDuplicate", "duplicate-page"},
                {"btPagesDelete", "delete-page"}};

        for (const auto& [id, action]: expected) {
            GtkWidget* control = byId(window, id);
            ASSERT_NE(control, nullptr) << id;

            const char* actionName = gtk_actionable_get_action_name(GTK_ACTIONABLE(control));
            ASSERT_NE(actionName, nullptr) << id;
            EXPECT_EQ(std::string(actionName), std::string("win.") + action) << id;

            // The action the control names has to be one the window can resolve, or the control
            // would do nothing at all.
            ASSERT_NE(g_action_map_lookup_action(G_ACTION_MAP(window), action), nullptr)
                    << id << ": the window does not offer " << actionName;

            // A click and a keyboard activation both end in "clicked", which is where a GtkButton
            // runs the action it names.
            g_signal_emit_by_name(control, "clicked");
            settle();
            EXPECT_EQ(spy.count(action), 1) << id << ": the control runs its page action";
        }

        gtk_widget_destroy(window);
        settle();
    }
};
TEST_F(PageToolbarActionsTest, everyToolbarControlRunsThePageActionItNames) {}

/*
 * Plan 005, step 6: "resizes cleanly at 150px, 280px, and 500px sidebar widths". The toolbar has
 * to fit the narrowest of them without clipping a control.
 */
class PageToolbarNarrowTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        PageToolbar pageToolbar;
        GtkWidget* toolbar = pageToolbar.toolbar();

        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), NARROW_SIDEBAR_WIDTH, 600);
        gtk_container_add(GTK_CONTAINER(window), toolbar);
        gtk_widget_show_all(window);
        settle();

        const int toolbarMinimum = minimumWidth(toolbar);
        EXPECT_LE(toolbarMinimum, NARROW_SIDEBAR_WIDTH)
                << "the toolbar asks for " << toolbarMinimum << "px, which the narrowest sidebar cannot give";

        for (GtkWidget* control: buttonsOf(toolbar)) {
            EXPECT_LE(minimumWidth(control), NARROW_SIDEBAR_WIDTH) << gtk_widget_get_name(control);
        }

        // A control whose width comes from the theme's own button metrics would defeat the
        // compact class, so the class has to be what sizes them.
        for (int width: {NARROW_SIDEBAR_WIDTH, MEDIUM_SIDEBAR_WIDTH, WIDE_SIDEBAR_WIDTH}) {
            gtk_window_resize(GTK_WINDOW(window), width, 600);
            settle();
            EXPECT_LE(minimumWidth(toolbar), width) << "the toolbar must fit a " << width << "px sidebar";
        }

        gtk_widget_destroy(window);
        settle();
    }
};
TEST_F(PageToolbarNarrowTest, theToolbarFitsTheNarrowestSidebar) {}

/*
 * Plan 009: adding a page with a pen must not need a keyboard, a menu hunt or a mouse. The page
 * toolbar is anchored below the scrolling thumbnails, so a control placed in it stays on screen
 * while the pages scroll. What is checked here is that such a control exists: it is the bottom,
 * full-width row, it says the words "Add page" beside the existing page-add icon rather than
 * being an icon to discover, and it runs the canonical insert-after action, so undo and page
 * navigation behave exactly as they do from the context menu.
 */
class PageToolbarAddPageTest: public GtkTest {
    void runTest(GtkApplication*) override {
        PageToolbar pageToolbar;
        GtkWidget* toolbar = pageToolbar.toolbar();
        ASSERT_NE(toolbar, nullptr);

        GtkWidget* addPage = byId(toolbar, ADD_PAGE_BUTTON_ID);
        ASSERT_NE(addPage, nullptr) << "the page toolbar has no Add page control";
        ASSERT_TRUE(GTK_IS_BUTTON(addPage));

        // The canonical action, not a new one: insert after the current page.
        const char* actionName = gtk_actionable_get_action_name(GTK_ACTIONABLE(addPage));
        ASSERT_NE(actionName, nullptr) << "the Add page control runs no action";
        EXPECT_EQ(std::string(actionName), "win.new-page-after");

        // The pen target class: the geometry convention Plan 001 set for touch input.
        EXPECT_TRUE(hasCssClass(addPage, "xoj-control-touch")) << "the control is not sized for a stylus";

        // Full width, so a stylus cannot miss it.
        EXPECT_TRUE(gtk_widget_get_hexpand(addPage)) << "the Add page control has to fill the sidebar width";

        // The bottom-most row: the last child row of the toolbar holds it.
        GList* rows = gtk_container_get_children(GTK_CONTAINER(toolbar));
        ASSERT_NE(rows, nullptr);
        GtkWidget* lastRow = GTK_WIDGET(g_list_last(rows)->data);
        bool onBottomRow = false;
        for (GtkWidget* widget: allWidgets(lastRow)) {
            if (widget == addPage) {
                onBottomRow = true;
            }
        }
        EXPECT_TRUE(onBottomRow) << "the Add page control is not the bottom row of the toolbar";
        g_list_free(rows);

        // A visible, translatable label beside the page-add icon: words a user reads, not an icon
        // they have to guess at.
        bool hasLabel = false;
        bool hasIcon = false;
        for (GtkWidget* widget: allWidgets(addPage)) {
            if (GTK_IS_LABEL(widget) &&
                std::string(gtk_label_get_text(GTK_LABEL(widget))) == std::string("Add page")) {
                hasLabel = true;
            }
            if (GTK_IS_IMAGE(widget)) {
                const gchar* icon = nullptr;
                GtkIconSize size = GTK_ICON_SIZE_INVALID;
                gtk_image_get_icon_name(GTK_IMAGE(widget), &icon, &size);
                if (icon != nullptr && std::string(icon) == "xopp-page-add") {
                    hasIcon = true;
                }
            }
        }
        EXPECT_TRUE(hasLabel) << "the control has to say \"Add page\"";
        EXPECT_TRUE(hasIcon) << "the control keeps the application's page-add icon";

        // Named and reachable, like every other control of the toolbar.
        SidebarPreviewPages::applyToolbarAccessibility(toolbar);
        EXPECT_TRUE(gtk_widget_get_can_focus(addPage)) << "the Add page control is not reachable by keyboard";
        const char* tooltip = gtk_widget_get_tooltip_text(addPage);
        ASSERT_NE(tooltip, nullptr) << "the Add page control has no tooltip";
        EXPECT_EQ(accessibleName(addPage), std::string(tooltip));
        EXPECT_FALSE(accessibleName(addPage).empty()) << "the Add page control has no accessible name";
    }
};
TEST_F(PageToolbarAddPageTest, theToolbarOffersAProminentPenSizedAddPageControl) {}

/*
 * Plan 009: the Add page control runs the canonical action the window offers, so pressing it is
 * the same path a click or the keyboard takes, and it inserts after the current page only.
 */
class PageToolbarAddPageActivationTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);

        ActionSpy spy;
        spy.installOn(GTK_APPLICATION_WINDOW(window));

        PageToolbar pageToolbar;
        gtk_container_add(GTK_CONTAINER(window), pageToolbar.toolbar());
        gtk_widget_show_all(window);
        settle();

        GtkWidget* addPage = byId(window, ADD_PAGE_BUTTON_ID);
        ASSERT_NE(addPage, nullptr);

        ASSERT_NE(g_action_map_lookup_action(G_ACTION_MAP(window), "new-page-after"), nullptr)
                << "the window does not offer new-page-after";

        g_signal_emit_by_name(addPage, "clicked");
        settle();
        EXPECT_EQ(spy.count("new-page-after"), 1) << "the Add page control inserts a page after this one";
        EXPECT_EQ(spy.count("new-page-before"), 0) << "Add page inserts after the current page, not before it";

        gtk_widget_destroy(window);
        settle();
    }
};
TEST_F(PageToolbarAddPageActivationTest, theAddPageControlRunsTheCanonicalInsertAfterAction) {}

/*
 * Plan 009: the Add page control is a pen target of at least 44px, and it gets that size from the
 * stylesheet's own touch class rather than from a painted height. The compact controls above it
 * keep their compact size: the new row must not grow them.
 */
class PageToolbarAddPageSizeTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        const std::string css = applicationStylesheet();
        ASSERT_FALSE(css.empty()) << "ui/xournalpp.css has to be readable";
        // Plan 001's touch geometry class, and the scoped rule that gives the row its padding.
        EXPECT_NE(css.find(".xoj-control-touch"), std::string::npos)
                << "the touch control class is not in the application stylesheet";
        EXPECT_NE(css.find("#btPagesAdd"), std::string::npos)
                << "the Add page control has no scoped rule in the application stylesheet";

        CriticalWatch criticals;

        GtkCssProvider* provider = gtk_css_provider_new();
        const std::string path = (std::filesystem::path(GET_UI_FOLDER) / "xournalpp.css").string();
        GError* error = nullptr;
        ASSERT_TRUE(gtk_css_provider_load_from_path(provider, path.c_str(), &error))
                << (error != nullptr ? error->message : "the stylesheet did not load");
        g_clear_error(&error);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), NARROW_SIDEBAR_WIDTH, 600);

        PageToolbar pageToolbar;
        gtk_container_add(GTK_CONTAINER(window), pageToolbar.toolbar());
        gtk_widget_show_all(window);
        settle();

        GtkWidget* addPage = byId(window, ADD_PAGE_BUTTON_ID);
        ASSERT_NE(addPage, nullptr);

        int minHeight = 0;
        gtk_widget_get_preferred_height(addPage, &minHeight, nullptr);
        EXPECT_GE(minHeight, 44)
                << "the Add page control is a pen target, so it is at least 44px tall; it asked for " << minHeight;

        // It still fits the narrowest sidebar it has to be usable at.
        int minWidth = 0;
        gtk_widget_get_preferred_width(addPage, &minWidth, nullptr);
        EXPECT_LE(minWidth, NARROW_SIDEBAR_WIDTH)
                << "the Add page control asks for " << minWidth << "px, which the narrowest sidebar cannot give";

        // The compact controls above it keep their own size: the new row must not resize them.
        for (const char* id: {"btPagesMoveUp", "btPagesMoveDown", "btPagesDuplicate", "btPagesDelete"}) {
            GtkWidget* control = byId(window, id);
            ASSERT_NE(control, nullptr) << id;
            int controlHeight = 0;
            gtk_widget_get_preferred_height(control, &controlHeight, nullptr);
            EXPECT_LE(controlHeight, 36) << id << " grew with the Add page row";
        }

        gtk_widget_destroy(window);
        settle();
        EXPECT_EQ(criticals.count(), 0U) << criticals.report();

        gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider));
        g_object_unref(provider);
    }
};
TEST_F(PageToolbarAddPageSizeTest, theAddPageControlIsAPenSizedFullWidthRowThatLeavesTheCompactsAlone) {}

/*
 * Plan 005, step 4: the page actions are in the sidebar's context menu, next to the export of the
 * selected pages.
 */
class PageContextMenuTest: public GtkTest {
    void runTest(GtkApplication*) override {
        PageToolbar pageToolbar;
        GMenuModel* menu = pageToolbar.menu();
        ASSERT_NE(menu, nullptr);

        const std::vector<MenuItem> items = menuItems(menu);
        ASSERT_FALSE(items.empty());

        std::map<std::string, std::string> byAction;
        for (const MenuItem& item: items) {
            EXPECT_FALSE(item.label.empty()) << item.action << " has no label to read";
            byAction[item.action] = item.label;
        }

        for (const char* action: {"win.new-page-before", "win.new-page-after", "win.duplicate-page",
                                  "win.move-page-towards-beginning", "win.move-page-towards-end",
                                  "win.delete-page"}) {
            EXPECT_EQ(byAction.count(action), 1U) << action << " is missing from the page menu";
        }

        // The one action the plan adds to the menu: exporting what the navigator has selected.
        ASSERT_EQ(byAction.count("win.export-as"), 1U);
        EXPECT_EQ(byAction.at("win.export-as"), "Export");
    }
};
TEST_F(PageContextMenuTest, theMenuOffersThePageActionsAndTheExportOfTheSelection) {}

/*
 * Plan 005, step 4: "exporting selected noncontiguous pages affects exactly the selected indices".
 *
 * The range the navigator builds is handed to the export dialog as the range it starts from. The
 * dialog is built here exactly as the export action builds it, so what is checked is that a
 * selection arrives as a usable range: the pages field is filled in, the range is the one
 * selected, the radio that says so is the active one, and the dialog will accept it.
 */
class ExportSelectionRangeTest: public GtkTest {
    void runTest(GtkApplication*) override {
        constexpr size_t PAGE_COUNT = 8;
        // Pages 1, 3, 4 and 7 of the document, as xoj::model::formatPageRange() writes them.
        const std::string range = "1,3-4,7";

        GladeSearchpath gladeSearchPath;
        gladeSearchPath.addSearchDirectory(GET_UI_FOLDER);

        bool answered = false;
        xoj::popup::ExportDialog dialog{&gladeSearchPath, EXPORT_GRAPHICS_PDF, 1, PAGE_COUNT, false, range,
                                        [&answered](const xoj::popup::ExportDialog&) { answered = true; }};

        GtkWidget* window = GTK_WIDGET(dialog.getWindow());
        ASSERT_NE(window, nullptr);

        GtkWidget* txtPages = byId(window, "txtPages");
        GtkWidget* rdRangePages = byId(window, "rdRangePages");
        GtkWidget* btOk = byId(window, "btOk");
        ASSERT_NE(txtPages, nullptr);
        ASSERT_NE(rdRangePages, nullptr);
        ASSERT_NE(btOk, nullptr);

        EXPECT_EQ(std::string(gtk_editable_get_text(GTK_EDITABLE(txtPages))), range)
                << "the selection is what the dialog starts from";
        EXPECT_TRUE(gtk_check_button_get_active(GTK_CHECK_BUTTON(rdRangePages)))
                << "and the range is what it exports";

        // The dialog validated the range it was given: no error, and the export can be confirmed.
        EXPECT_FALSE(gtk_style_context_has_class(gtk_widget_get_style_context(txtPages), "error"));
        EXPECT_TRUE(gtk_widget_get_sensitive(txtPages));
        EXPECT_TRUE(gtk_widget_get_sensitive(btOk));
        EXPECT_FALSE(answered) << "nothing is exported before the user confirms";
    }
};
TEST_F(ExportSelectionRangeTest, aNoncontiguousSelectionArrivesAsTheExportRange) {}

/*
 * The range is only a starting point: a user can change it, and the dialog refuses what it cannot
 * parse rather than exporting something else.
 */
class ExportInvalidRangeTest: public GtkTest {
    void runTest(GtkApplication*) override {
        constexpr size_t PAGE_COUNT = 4;

        GladeSearchpath gladeSearchPath;
        gladeSearchPath.addSearchDirectory(GET_UI_FOLDER);

        xoj::popup::ExportDialog dialog{&gladeSearchPath, EXPORT_GRAPHICS_PDF, 1, PAGE_COUNT, false, "9",
                                        [](const xoj::popup::ExportDialog&) {}};

        GtkWidget* window = GTK_WIDGET(dialog.getWindow());
        GtkWidget* txtPages = byId(window, "txtPages");
        GtkWidget* btOk = byId(window, "btOk");
        ASSERT_NE(txtPages, nullptr);
        ASSERT_NE(btOk, nullptr);

        EXPECT_TRUE(gtk_style_context_has_class(gtk_widget_get_style_context(txtPages), "error"));
        EXPECT_FALSE(gtk_widget_get_sensitive(btOk)) << "an export that cannot be parsed is not confirmed";
    }
};
TEST_F(ExportInvalidRangeTest, aRangeOutsideTheDocumentIsRefusedNotExported) {}

/*
 * The toolbar and the menu outlive the window they were put in, and they are built and dropped
 * many times over a session: neither may unref what it does not own.
 */
class PageNavigatorTeardownTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), NARROW_SIDEBAR_WIDTH, 600);

        CriticalWatch criticals;

        {
            PageToolbar pageToolbar;
            ActionSpy spy;
            spy.installOn(GTK_APPLICATION_WINDOW(window));

            gtk_container_add(GTK_CONTAINER(window), pageToolbar.toolbar());
            gtk_widget_show_all(window);
            settle();

            // A toolbar that is built, used and rebuilt is what a session does.
            GtkWidget* toolbar = pageToolbar.toolbar();
            ASSERT_NE(toolbar, nullptr);
            for (GtkWidget* control: buttonsOf(toolbar)) {
                gtk_widget_activate(control);
                settle();
            }
            gtk_widget_queue_draw(toolbar);
            settle();
        }

        gtk_widget_destroy(window);
        settle();

        EXPECT_EQ(criticals.count(), 0U) << criticals.report();
    }
};
TEST_F(PageNavigatorTeardownTest, theToolbarIsReleasedOnceAfterItsWindowIsGone) {}

/*
 * Plan 005, step 2: one entry model, two presentations. The cards are moved between the two
 * containers rather than rebuilt, which only works if a container lets go of a card - and of
 * nothing else - when the card is taken out of it. A card is held by its entry, the way the
 * navigator holds one, so what is checked here is that the card and everything in it survive the
 * move: a card that lost its thumbnail and its metadata to the move would be an empty box in the
 * other density.
 */
class PageCardContainerTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(window), box);

        GtkWidget* flowBox = gtk_flow_box_new();
        GtkWidget* listBox = gtk_list_box_new();
        gtk_box_append(GTK_BOX(box), flowBox);
        gtk_box_append(GTK_BOX(box), listBox);

        // A card is what an entry owns, thumbnail and metadata inside it.
        xoj::util::WidgetSPtr card(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2), xoj::util::adopt);
        GtkWidget* thumbnail = gtk_label_new("a page");
        gtk_box_append(GTK_BOX(card.get()), thumbnail);

        gtk_widget_show_all(window);
        settle();

        auto childCount = [](GtkWidget* container) -> unsigned {
            return g_list_length(gtk_container_get_children(GTK_CONTAINER(container)));
        };
        // The card and what it holds: the whole of what has to survive a move.
        auto cardIsWhole = [&card, thumbnail]() -> bool {
            return GTK_IS_WIDGET(card.get()) && GTK_IS_WIDGET(thumbnail) &&
                   gtk_widget_get_parent(thumbnail) == card.get();
        };

        // A card in a wrapped container.
        gtk_flow_box_insert(GTK_FLOW_BOX(flowBox), card.get(), -1);
        settle();
        EXPECT_EQ(childCount(flowBox), 1U);

        SidebarPreviewBase::detachFromContainer(card.get());
        settle();
        EXPECT_EQ(childCount(flowBox), 0U) << "the flow box has to let go of a card that leaves it";
        EXPECT_EQ(gtk_widget_get_parent(card.get()), nullptr) << "the card is out of the container";
        EXPECT_TRUE(cardIsWhole()) << "taking a card out must not take its contents with it";

        // The same card in the list container, and back into the flow box: one card, two
        // presentations, never two cards.
        gtk_list_box_insert(GTK_LIST_BOX(listBox), card.get(), -1);
        settle();
        EXPECT_EQ(childCount(listBox), 1U);
        EXPECT_EQ(childCount(flowBox), 0U);

        SidebarPreviewBase::detachFromContainer(card.get());
        settle();
        EXPECT_EQ(childCount(listBox), 0U);
        EXPECT_TRUE(cardIsWhole());

        gtk_flow_box_insert(GTK_FLOW_BOX(flowBox), card.get(), -1);
        settle();
        EXPECT_EQ(childCount(flowBox), 1U);
        EXPECT_EQ(childCount(listBox), 0U) << "moving a card is not copying it";
        EXPECT_TRUE(cardIsWhole());

        // Detaching something that is not in a container is not an error: the entry destructor
        // calls this for every way an entry can go.
        SidebarPreviewBase::detachFromContainer(card.get());
        SidebarPreviewBase::detachFromContainer(card.get());
        SidebarPreviewBase::detachFromContainer(nullptr);
        settle();
        EXPECT_EQ(childCount(flowBox), 0U);
        EXPECT_TRUE(cardIsWhole());

        gtk_widget_destroy(window);
        settle();
    }
};
TEST_F(PageCardContainerTest, aCardMovesBetweenTheTwoContainersInsteadOfBeingCopied) {}

/*
 * Plan 005, step 3: the state of a card is expressed as semantic classes, and Plan 001 put those
 * classes in one stylesheet. A class the code sets but the stylesheet does not define is a state
 * the user cannot see, so the two are checked against each other here.
 */
class PageCardStyleTest: public GtkTest {
    void runTest(GtkApplication*) override {
        const std::string css = applicationStylesheet();
        ASSERT_FALSE(css.empty()) << "ui/xournalpp.css has to be readable";
        // The classes SidebarPreviewPageEntry puts on a card, in the order it sets them.
        for (const char* cssClass: {"xoj-page-card",
                                    "xoj-page-card-selected",
                                    "xoj-page-card-current",
                                    "xoj-page-card-loading",
                                    "xoj-page-card-error",
                                    "xoj-page-card-list",
                                    "xoj-page-card-overview",
                                    "xoj-page-card-drop-before",
                                    "xoj-page-card-drop-after",
                                    "xoj-page-card-metadata",
                                    "xoj-page-selection-marker",
                                    "xoj-focus-ring"}) {
            EXPECT_NE(css.find(std::string(".") + cssClass), std::string::npos)
                    << cssClass << " is set by the navigator but not styled";
        }

        // And the whole stylesheet still parses, which a class name cannot tell. GTK3 loads a
        // stylesheet from a path through an out-parameter, so the load itself is what is checked.
        CriticalWatch criticals;
        GtkCssProvider* provider = gtk_css_provider_new();
        const std::string path = (std::filesystem::path(GET_UI_FOLDER) / "xournalpp.css").string();
        GError* error = nullptr;
        const gboolean loaded = gtk_css_provider_load_from_path(provider, path.c_str(), &error);
        EXPECT_TRUE(loaded) << (error != nullptr ? error->message : "the stylesheet did not load");
        g_clear_error(&error);
        gtk_style_context_add_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider),
                                                  GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        settle();
        gtk_style_context_remove_provider_for_screen(gdk_screen_get_default(), GTK_STYLE_PROVIDER(provider));
        g_object_unref(provider);

        EXPECT_EQ(criticals.count(), 0U) << criticals.report();
    }
};
TEST_F(PageCardStyleTest, everyClassACardSetsIsStyledByTheApplicationStylesheet) {}

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

#include <algorithm>  // for find_if, sort
#include <cstddef>    // for size_t
#include <initializer_list>
#include <memory>  // for make_unique, unique_ptr
#include <string>  // for string
#include <vector>  // for vector

#include <gtest/gtest.h>
#include <gtk/gtk.h>  // for GtkWidget, GtkRadioButton, GtkApplication

#include "../dialog/GtkTest.h"
#include "control/ToolConfigAdapter.h"  // for ToolConfigAdapter, ToolConfigState
#include "control/ToolHandler.h"        // for ToolHandler
#include "control/ToolPreset.h"         // for ToolPreset, ToolPresetList
#include "control/settings/Settings.h"  // for Settings
#include "enums/Action.enum.h"          // for Action, Action_toString
#include "gui/IconNameHelper.h"         // for IconNameHelper
#include "gui/toolbarMenubar/AbstractToolItem.h"
#include "gui/toolbarMenubar/ActiveToolSummary.h"
#include "gui/toolbarMenubar/PresetFavoritesItem.h"
#include "gui/toolbarMenubar/ToolButton.h"
#include "gui/toolbarMenubar/ToolPropertyPanel.h"
#include "gui/toolbarMenubar/ToolPropertyPopover.h"
#include "gui/toolbarMenubar/ToolPropertyProvider.h"
#include "gui/toolbarMenubar/ToolPropertyProviders.h"
#include "util/Color.h"  // for Colors
#include "util/GVariantTemplate.h"

#include "config-test.h"

/*
 * Plan 003: the GTK side of the contextual tool properties.
 *
 * The harness runs a real GtkApplication, so a widget tree can be built and inspected. It does not
 * build a Control or a MainWindow: the tests use ToolHandler/ToolConfigAdapter directly, the same
 * way ToolConfigAdapterTest does, and register the GActions a panel is bound to on a plain
 * GtkApplicationWindow.
 *
 * The rows of a panel are found by the semantic label they carry. In GTK3 a radio row is wired to
 * its action through signals rather than through the action-name property (see
 * xoj::util::gtk::setRadioButtonActionName), so the label is both the only stable handle and one of
 * the things the plan asks for.
 *
 * What this file deliberately does not claim: a real pointer press, or a real Escape key on a
 * popover that GTK has grabbed. GtkTest has no way to inject input events, so those are covered by
 * the manual matrix in test/README.md rather than by a test that would only be asserting its own
 * synthesis.
 */

namespace {

/// ToolHandler reports changes to a listener; these tests do not look at the reports.
class StubToolListener: public ToolListener {
public:
    void toolColorChanged() override {}
    void changeColorOfSelection() override {}
    void toolSizeChanged() override {}
    void toolFillChanged() override {}
    void toolLineStyleChanged() override {}
    void toolChanged() override {}
};

/// Records how often the stored preset list was reported as changed.
class StubPresetListListener: public PresetListListener {
public:
    void presetListChanged() override { this->changes++; }
    int changes = 0;
};

/// A row set that is not one of the built-in ones, so the registry is exercised as an extension
/// point rather than as a lookup table of known tools.
class DummyRows: public ToolPropertyRows {
public:
    DummyRows() { this->widget = gtk_label_new("dummy rows"); }
    ~DummyRows() override { gtk_widget_destroy(this->widget); }

    GtkWidget* getWidget() const override { return this->widget; }
    void toolConfigChanged(const ToolConfigState& state) override {
        this->updates++;
        this->last = state;
    }

    GtkWidget* widget = nullptr;
    int updates = 0;
    ToolConfigState last{};
};

/// Plan 003, step 6: shape and selection register a provider later; this stands in for them.
class DummyProvider: public ToolPropertyProvider {
public:
    ToolType getToolType() const override { return TOOL_DRAW_RECT; }
    std::string getTitle() const override { return "Shape"; }
    std::string getIconName() const override { return "xopp-tool-properties"; }
    std::unique_ptr<ToolPropertyRows> createRows(ToolConfigAdapter&, GtkWindow*) const override {
        auto rows = std::make_unique<DummyRows>();
        this->lastRows = rows.get();
        return rows;
    }

    /// The rows of the last createRows() call, so a test can drive them.
    mutable DummyRows* lastRows = nullptr;
};

/// ToolHandler without a Control and without an ActionDatabase, plus the adapter around it.
struct ToolFixture {
    StubToolListener listener;
    ToolHandler handler{&listener, nullptr, nullptr};
    ToolConfigAdapter adapter{handler};
};

/** A fresh, empty directory to hold one test's settings file. */
auto freshSettingsFile(const char* name) -> fs::path {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir / "settings.xml";
}

/// Every widget at or below `root`.
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

auto childCount(GtkWidget* container) -> std::size_t {
    GList* children = gtk_container_get_children(GTK_CONTAINER(container));
    const std::size_t count = g_list_length(children);
    g_list_free(children);
    return count;
}

/// Every button at or below `container`, in tree order.
auto buttonsOf(GtkWidget* container) -> std::vector<GtkWidget*> {
    std::vector<GtkWidget*> buttons;
    for (GtkWidget* widget: allWidgets(container)) {
        if (GTK_IS_BUTTON(widget) && !GTK_IS_MENU_BUTTON(widget)) {
            buttons.emplace_back(widget);
        }
    }
    return buttons;
}

auto accessibleName(GtkWidget* widget) -> std::string {
    const char* name = atk_object_get_name(gtk_widget_get_accessible(widget));
    return name != nullptr ? std::string(name) : std::string();
}

/// The radio rows at or below `root`, in tree order.
auto radioButtons(GtkWidget* root) -> std::vector<GtkRadioButton*> {
    std::vector<GtkRadioButton*> result;
    for (GtkWidget* widget: allWidgets(root)) {
        if (GTK_IS_RADIO_BUTTON(widget)) {
            result.emplace_back(GTK_RADIO_BUTTON(widget));
        }
    }
    return result;
}

/// The radio row at or below `root` whose visible, semantic label is `label`.
auto radioButtonNamed(GtkWidget* root, const char* label) -> GtkRadioButton* {
    for (GtkRadioButton* button: radioButtons(root)) {
        if (accessibleName(GTK_WIDGET(button)) == label) {
            return button;
        }
    }
    return nullptr;
}

/// How many rows share the radio group of `button`. One group means one value at a time.
auto radioGroupSize(GtkRadioButton* button) -> std::size_t {
    // gtk_radio_button_get_group() is transfer-none: the list belongs to GTK and must not be
    // freed.
    return g_slist_length(gtk_radio_button_get_group(button));
}

/// The value a row of an enum-valued action is "on" for.
auto enumTargetOf(GtkRadioButton* button) -> guint64 {
    GVariant* target = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(button));
    return target == nullptr ? 0U : g_variant_get_uint64(target);
}

/// Whether any control below `root` is bound to `actionName` through its action-name property.
auto hasControlForAction(GtkWidget* root, const std::string& actionName) -> bool {
    for (GtkWidget* widget: allWidgets(root)) {
        if (!GTK_IS_ACTIONABLE(widget)) {
            continue;
        }
        const char* name = gtk_actionable_get_action_name(GTK_ACTIONABLE(widget));
        if (name != nullptr && actionName == name) {
            return true;
        }
    }
    return false;
}

auto actionNameOf(Action action) -> std::string { return std::string("win.") + Action_toString(action); }

auto hasCssClass(GtkWidget* widget, const char* name) -> bool {
    return gtk_style_context_has_class(gtk_widget_get_style_context(widget), name);
}

/// The one row of `buttons` that is currently shown as chosen, or nullptr.
auto activeRadioButton(const std::vector<GtkRadioButton*>& buttons) -> GtkRadioButton* {
    auto it = std::find_if(buttons.begin(), buttons.end(), [](GtkRadioButton* button) {
        return gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(button));
    });
    return it == buttons.end() ? nullptr : *it;
}

/// The width rows of a panel, by their semantic labels.
constexpr auto WIDTH_LABELS = {"Very fine", "Fine", "Medium", "Thick", "Very thick"};

/// The drawing types the pen and highlighter panels offer, by their semantic labels.
constexpr auto DRAWING_TYPE_LABELS = {"Draw Line",  "Draw Rectangle", "Draw Ellipse",
                                      "Draw Arrow", "Draw Spline",    "Stroke recognizer"};

/// The pen's line styles, by their semantic labels.
constexpr auto LINE_STYLE_LABELS = {"standard", "dashed", "dash-/ dotted", "dotted"};

/// The eraser's modes, by their semantic labels.
constexpr auto ERASER_TYPE_LABELS = {"standard", "whiteout", "delete stroke"};

/// The rows a panel offers for `labels`; a missing row is reported as a failure.
auto labelledRows(GtkWidget* panel, std::initializer_list<const char*> labels) -> std::vector<GtkRadioButton*> {
    std::vector<GtkRadioButton*> rows;
    for (const char* label: labels) {
        GtkRadioButton* row = radioButtonNamed(panel, label);
        EXPECT_NE(row, nullptr) << label << " is not offered by the panel";
        if (row != nullptr) {
            rows.emplace_back(row);
        }
    }
    return rows;
}

/// Lets GTK finish what it queued, so the widget tree settles into the state a user would see.
void settle() {
    while (g_main_context_iteration(nullptr, FALSE)) {}
}

/**
 * Emits a key press on `widget`.
 *
 * GtkTest cannot inject real input, so this is as close as the harness gets to a key. It is enough
 * to see whether GTK itself handles the key, which is all the plan needs from Escape: the popover
 * is the one the tool's own control owns, so GTK pops it down and gives the focus back.
 */
auto pressKey(GtkWidget* widget, guint keyval) -> bool {
    GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
    GdkWindow* widgetWindow = gtk_widget_get_window(widget);
    event->key.window = widgetWindow != nullptr ? GDK_WINDOW(g_object_ref(widgetWindow)) : nullptr;
    event->key.send_event = TRUE;
    event->key.time = GDK_CURRENT_TIME;
    event->key.keyval = keyval;
    gboolean handled = FALSE;
    g_signal_emit_by_name(widget, "key-press-event", event, &handled);
    gdk_event_free(event);
    return handled;
}

/**
 * Counts the GLib criticals logged while it is alive.
 *
 * A widget that unrefs memory it does not own - a popover released after the window that owned it
 * is gone, say - does not crash a normal build: GLib logs "g_object_unref: assertion
 * 'G_IS_OBJECT (object)' failed" and carries on. A test that only looks at the widget tree would
 * never see it, so the teardown is watched here and a critical is a failure.
 */
class CriticalWatch {
public:
    CriticalWatch() { this->previous = g_log_set_default_handler(onLog, this); }
    ~CriticalWatch() { g_log_set_default_handler(this->previous, nullptr); }

    CriticalWatch(const CriticalWatch&) = delete;
    auto operator=(const CriticalWatch&) -> CriticalWatch& = delete;

    auto count() const -> std::size_t { return this->criticals.size(); }

    /// What was logged, for the failure message of the test that uses this.
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
        // Everything is still handed on, so a critical this test is not about stays in the log.
        // The default handler ignores the data it is given, which is why passing nullptr is fine.
        if (self->previous != nullptr) {
            self->previous(domain, levels, message, nullptr);
        }
    }

    std::vector<std::string> criticals;
    GLogFunc previous = nullptr;
};

/**
 * The popovers GTK has anchored to a widget of `window`, in the order the accessible tree holds
 * them.
 *
 * A popover is not part of the widget tree of the toolbar that built it - GTK makes it a child of
 * the toplevel's own bookkeeping, and gtk_container_get_children() does not report it - so the
 * accessible tree is the only place where a test can see one from the outside.
 */
auto anchoredPopovers(GtkWidget* window) -> std::vector<GtkWidget*> {
    std::vector<GtkWidget*> popovers;
    AtkObject* accessible = gtk_widget_get_accessible(window);
    for (int i = 0; i < atk_object_get_n_accessible_children(accessible); i++) {
        AtkObject* child = atk_object_ref_accessible_child(accessible, i);
        if (child == nullptr) {
            continue;
        }
        if (GTK_IS_ACCESSIBLE(child)) {
            GtkWidget* widget = gtk_accessible_get_widget(GTK_ACCESSIBLE(child));
            if (widget != nullptr && GTK_IS_POPOVER(widget)) {
                popovers.emplace_back(widget);
            }
        }
        g_object_unref(child);
    }
    return popovers;
}

}  // namespace

/*
 * Plan 003, step 6: a provider registers, is found by its tool type, exposes a title and a widget
 * and receives state updates.
 */
class ToolPropertyRegistryTest: public GtkTest {
    void runTest(GtkApplication*) override {
        ToolFixture fixture;

        ToolPropertyRegistry registry;
        EXPECT_EQ(registry.find(TOOL_DRAW_RECT), nullptr) << "an empty registry offers no provider";

        auto provider = std::make_unique<DummyProvider>();
        DummyProvider* dummy = provider.get();
        registry.add(std::move(provider));

        ASSERT_EQ(registry.getProviders().size(), 1U);
        EXPECT_EQ(registry.find(TOOL_DRAW_RECT), dummy) << "a provider is found by its tool type";
        EXPECT_EQ(registry.find(TOOL_PEN), nullptr) << "a provider answers only for its own tool";
        EXPECT_EQ(dummy->getTitle(), "Shape");
        EXPECT_FALSE(dummy->getIconName().empty());

        // A dummy provider can expose a widget and be told about a state change.
        std::unique_ptr<ToolPropertyRows> rows = dummy->createRows(fixture.adapter, nullptr);
        ASSERT_NE(rows, nullptr);
        ASSERT_NE(rows->getWidget(), nullptr);
        ASSERT_EQ(dummy->lastRows, static_cast<DummyRows*>(rows.get()));

        ToolConfigState state = fixture.adapter.getState();
        state.toolType = TOOL_DRAW_RECT;
        state.hasColor = true;
        state.color = Colors::red;
        rows->toolConfigChanged(state);

        EXPECT_EQ(dummy->lastRows->updates, 1) << "the provider's rows received the update";
        EXPECT_EQ(dummy->lastRows->last.toolType, TOOL_DRAW_RECT);
        EXPECT_EQ(dummy->lastRows->last.color, Colors::red);
    }
};
TEST_F(ToolPropertyRegistryTest, aDummyProviderRegistersExposesItsWidgetAndReceivesStateUpdates) {}

/*
 * Plan 003, step 3: the pen, highlighter and eraser popovers. One level, semantic labels, and one
 * radio group per value so a value can only be chosen one way at a time.
 */
class ToolPropertyPanelStructureTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        ToolFixture fixture;
        const fs::path settingsFile = freshSettingsFile("xournalpp-test-gtk_tool_properties_structure");
        Settings settings{settingsFile};
        IconNameHelper icons{&settings};

        ToolPropertyRegistry registry;
        xoj::toolbar::addBuiltInToolPropertyProviders(registry, icons);

        // Shape and selection are extension hooks, not panels of this plan.
        ASSERT_NE(registry.find(TOOL_PEN), nullptr);
        EXPECT_NE(registry.find(TOOL_HIGHLIGHTER), nullptr);
        EXPECT_NE(registry.find(TOOL_ERASER), nullptr);
        EXPECT_EQ(registry.find(TOOL_DRAW_RECT), nullptr) << "the shape panel is not part of this plan";
        EXPECT_EQ(registry.find(TOOL_SELECT_RECT), nullptr) << "the selection panel is not part of this plan";

        GtkWidget* window = gtk_application_window_new(app);
        StubPresetListListener presetsListener;

        ToolPropertyPopoverFactory factory{fixture.adapter, settings, *registry.find(TOOL_PEN), GTK_WINDOW(window),
                                           &presetsListener};
        EXPECT_EQ(factory.getToolType(), TOOL_PEN);

        GtkWidget* popover = factory.createPopover();
        ASSERT_NE(popover, nullptr);
        EXPECT_TRUE(GTK_IS_POPOVER(popover));
        EXPECT_TRUE(hasCssClass(popover, "xoj-tool-property-popover"));
        EXPECT_TRUE(hasCssClass(popover, "toolbar"));

        GtkWidget* content = gtk_bin_get_child(GTK_BIN(popover));
        ASSERT_NE(content, nullptr);
        EXPECT_TRUE(hasCssClass(content, "xoj-tool-properties"));

        // The panel is owned by the popover that owns its widgets.
        EXPECT_NE(g_object_get_data(G_OBJECT(popover), "xoj-tool-property-panel"), nullptr);

        const std::vector<GtkWidget*> widgets = allWidgets(content);

        // One level: no nested popover, no nested menu.
        for (GtkWidget* widget: widgets) {
            EXPECT_FALSE(GTK_IS_POPOVER(widget)) << "a property popover must not nest another popover";
        }

        // Semantic labels, not icon-only mystery controls.
        std::size_t buttons = 0;
        for (GtkWidget* widget: widgets) {
            if (!GTK_IS_BUTTON(widget)) {
                continue;
            }
            buttons++;
            EXPECT_FALSE(accessibleName(widget).empty()) << "a property control has no accessible name";
        }
        EXPECT_GE(buttons, 5U) << "width, colour, fill and preset controls are present";

        // A reader who cannot see the list move learns the result from a status line.
        bool hasStatusbar = false;
        for (GtkWidget* widget: widgets) {
            if (atk_object_get_role(gtk_widget_get_accessible(widget)) == ATK_ROLE_STATUSBAR) {
                hasStatusbar = true;
            }
        }
        EXPECT_TRUE(hasStatusbar) << "the panel announces the result of a preset action";

        // Width: five labels, one radio group, each row carrying the width it stands for.
        const std::vector<GtkRadioButton*> widthRows = labelledRows(content, WIDTH_LABELS);
        ASSERT_EQ(widthRows.size(), 5U);
        std::vector<guint64> widthTargets;
        for (GtkRadioButton* row: widthRows) {
            EXPECT_EQ(radioGroupSize(row), 5U) << "the widths are one group: one width at a time";
            widthTargets.emplace_back(enumTargetOf(row));
        }
        std::vector<guint64> expectedWidths{
                static_cast<guint64>(TOOL_SIZE_VERY_FINE), static_cast<guint64>(TOOL_SIZE_FINE),
                static_cast<guint64>(TOOL_SIZE_MEDIUM), static_cast<guint64>(TOOL_SIZE_THICK),
                static_cast<guint64>(TOOL_SIZE_VERY_THICK)};
        std::sort(widthTargets.begin(), widthTargets.end());
        std::sort(expectedWidths.begin(), expectedWidths.end());
        EXPECT_EQ(widthTargets, expectedWidths) << "every width is offered exactly once";

        // Line style: the pen's four, each carrying the string the action uses.
        const std::vector<GtkRadioButton*> lineStyleRows = labelledRows(content, LINE_STYLE_LABELS);
        ASSERT_EQ(lineStyleRows.size(), 4U);
        for (GtkRadioButton* row: lineStyleRows) {
            EXPECT_EQ(radioGroupSize(row), 4U);
            GVariant* target = gtk_actionable_get_action_target_value(GTK_ACTIONABLE(row));
            ASSERT_NE(target, nullptr);
            EXPECT_TRUE(g_variant_is_of_type(target, G_VARIANT_TYPE_STRING))
                    << "a line style row carries the string the action uses";
        }

        // Drawing type: the pen has one, and it is not a line style or a width.
        for (const char* label: DRAWING_TYPE_LABELS) {
            GtkRadioButton* row = radioButtonNamed(content, label);
            ASSERT_NE(row, nullptr) << label << " is not offered as a drawing type";
            EXPECT_EQ(radioGroupSize(row), 6U) << "the drawing types are one group of their own";
        }

        // The palette and the fill controls keep using the actions that already own them.
        EXPECT_TRUE(hasControlForAction(content, actionNameOf(Action::SELECT_COLOR)))
                << "the palette is reached through the existing colour chooser";
        EXPECT_TRUE(hasControlForAction(content, actionNameOf(Action::TOOL_FILL)));
        EXPECT_TRUE(hasControlForAction(content, actionNameOf(Action::TOOL_FILL_OPACITY)));

        // The eraser panel offers its own mode and none of the pen's fields.
        ToolPropertyPopoverFactory eraserFactory{fixture.adapter, settings, *registry.find(TOOL_ERASER),
                                                 GTK_WINDOW(window), &presetsListener};
        GtkWidget* eraserPopover = eraserFactory.createPopover();
        ASSERT_NE(eraserPopover, nullptr);
        GtkWidget* eraserContent = gtk_bin_get_child(GTK_BIN(eraserPopover));
        ASSERT_NE(eraserContent, nullptr);
        ASSERT_EQ(labelledRows(eraserContent, ERASER_TYPE_LABELS).size(), 3U);
        ASSERT_EQ(labelledRows(eraserContent, WIDTH_LABELS).size(), 5U);
        EXPECT_EQ(radioButtonNamed(eraserContent, "dashed"), nullptr)
                << "an eraser panel must not offer the pen's line styles";
        EXPECT_EQ(radioButtonNamed(eraserContent, "Draw Line"), nullptr)
                << "an eraser panel must not offer a drawing type";

        // The highlighter has a drawing type and no eraser mode.
        ToolPropertyPopoverFactory highlighterFactory{fixture.adapter, settings, *registry.find(TOOL_HIGHLIGHTER),
                                                      GTK_WINDOW(window), &presetsListener};
        GtkWidget* highlighterPopover = highlighterFactory.createPopover();
        ASSERT_NE(highlighterPopover, nullptr);
        GtkWidget* highlighterContent = gtk_bin_get_child(GTK_BIN(highlighterPopover));
        ASSERT_NE(highlighterContent, nullptr);
        EXPECT_EQ(radioButtonNamed(highlighterContent, "whiteout"), nullptr);
        EXPECT_EQ(radioButtonNamed(highlighterContent, "dashed"), nullptr);

        gtk_widget_destroy(highlighterPopover);
        gtk_widget_destroy(eraserPopover);
        gtk_widget_destroy(popover);
        gtk_widget_destroy(window);
        fs::remove_all(settingsFile.parent_path());
    }
};
TEST_F(ToolPropertyPanelStructureTest, thePenPanelIsOneLevelLabelledAndBoundToTheExistingActions) {}

/*
 * Plan 003, step 2 and step 3: the panel and the GActions that already own each value stay in step,
 * in both directions. A change made with a keyboard shortcut, a stylus button or a legacy toolbar
 * control has to reach the panel, and a change made in the panel has to reach the action.
 */
class ToolPropertyActionSyncTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        ToolFixture fixture;
        const fs::path settingsFile = freshSettingsFile("xournalpp-test-gtk_tool_properties_actions");
        Settings settings{settingsFile};
        IconNameHelper icons{&settings};

        ToolPropertyRegistry registry;
        xoj::toolbar::addBuiltInToolPropertyProviders(registry, icons);
        ToolPropertyProvider* penProvider = registry.find(TOOL_PEN);
        ASSERT_NE(penProvider, nullptr);

        GtkWidget* window = gtk_application_window_new(app);

        /*
         * The actions the rows are bound to, registered the way ActionDatabase registers them:
         * "tool-size" carries the current width, and each drawing type has its own boolean-state
         * action that takes no parameter.
         */
        auto* actions = g_simple_action_group_new();
        auto* sizeAction = g_simple_action_new_stateful("tool-size", G_VARIANT_TYPE_UINT64,
                                                        g_variant_new_uint64(TOOL_SIZE_VERY_THICK));
        g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(sizeAction));
        auto* lineStyleAction = g_simple_action_new_stateful("tool-pen-line-style", G_VARIANT_TYPE_STRING,
                                                             g_variant_new_string("plain"));
        g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(lineStyleAction));

        const std::vector<Action> drawingTypeActions{Action::TOOL_DRAW_LINE,    Action::TOOL_DRAW_RECTANGLE,
                                                     Action::TOOL_DRAW_ELLIPSE, Action::TOOL_DRAW_ARROW,
                                                     Action::TOOL_DRAW_SPLINE,  Action::TOOL_DRAW_SHAPE_RECOGNIZER};
        for (Action action: drawingTypeActions) {
            auto* drawingTypeAction = g_simple_action_new_stateful(Action_toString(action), G_VARIANT_TYPE_BOOLEAN,
                                                                   g_variant_new_boolean(FALSE));
            g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(drawingTypeAction));
            g_object_unref(drawingTypeAction);
        }

        // The group has to be in place before the panel joins the window: that is when each row
        // looks its action up and wires itself to it.
        gtk_widget_insert_action_group(window, "win", G_ACTION_GROUP(actions));

        auto panel = std::make_unique<ToolPropertyPanel>(fixture.adapter, settings, *penProvider, GTK_WINDOW(window),
                                                         nullptr);
        GtkWidget* content = panel->createWidget();
        ASSERT_NE(content, nullptr);
        gtk_container_add(GTK_CONTAINER(window), content);

        // The rows read the action state when they are attached: the width the action holds is the
        // one shown as chosen, and only that one.
        const std::vector<GtkRadioButton*> widthRows = labelledRows(content, WIDTH_LABELS);
        ASSERT_EQ(widthRows.size(), 5U);
        GtkRadioButton* active = activeRadioButton(widthRows);
        ASSERT_NE(active, nullptr) << "exactly one width must be shown as chosen";
        EXPECT_EQ(enumTargetOf(active), static_cast<guint64>(TOOL_SIZE_VERY_THICK));

        // A change made in the panel reaches the action.
        GtkRadioButton* fineRow = radioButtonNamed(content, "Fine");
        ASSERT_NE(fineRow, nullptr);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fineRow), TRUE);
        EXPECT_EQ(g_variant_get_uint64(g_action_get_state(G_ACTION(sizeAction))), static_cast<guint64>(TOOL_SIZE_FINE))
                << "choosing a width in the panel changes the size action";

        // A change made elsewhere reaches the panel.
        g_action_change_state(G_ACTION(sizeAction), g_variant_new_uint64(TOOL_SIZE_MEDIUM));
        ToolConfigState state = fixture.adapter.getState();
        state.size = TOOL_SIZE_MEDIUM;
        panel->toolConfigChanged(state);
        active = activeRadioButton(widthRows);
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(enumTargetOf(active), static_cast<guint64>(TOOL_SIZE_MEDIUM))
                << "the panel follows the action state, not its own copy";

        // Line styles follow their action the same way.
        GtkRadioButton* dottedRow = radioButtonNamed(content, "dotted");
        ASSERT_NE(dottedRow, nullptr);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(dottedRow), TRUE);
        EXPECT_STREQ(g_variant_get_string(g_action_get_state(G_ACTION(lineStyleAction)), nullptr), "dot")
                << "choosing a line style in the panel changes the line style action";

        // The six drawing types are one radio group, so the panel can only ever show one of them as
        // chosen. GTK3 cannot clear a radio group - gtk_toggle_button_set_active(..., FALSE) on the
        // active member is a no-op - so while the pen is on freehand, which has no action and
        // therefore no row, the panel still shows the first row as chosen. That is a limitation of
        // the widget and not of the state: the tool really has no drawing type. It is recorded in
        // test/README.md with the manual matrix.
        std::vector<GtkRadioButton*> drawingTypeRows;
        for (const char* label: DRAWING_TYPE_LABELS) {
            GtkRadioButton* row = radioButtonNamed(content, label);
            ASSERT_NE(row, nullptr) << label << " is not offered as a drawing type";
            EXPECT_EQ(radioGroupSize(row), 6U) << "the drawing types are one group of their own";
            drawingTypeRows.emplace_back(row);
        }

        // Making one current reaches its row, and only its row. This is what the boolean target each
        // row carries is for: without it every row whose own action is off would compare equal to
        // its own false state and the group would settle on the wrong member.
        auto setDrawingTypeState = [&actions](Action action, bool on) {
            g_action_change_state(g_action_map_lookup_action(G_ACTION_MAP(actions), Action_toString(action)),
                                  g_variant_new_boolean(on));
        };

        setDrawingTypeState(Action::TOOL_DRAW_RECTANGLE, true);
        state.drawingType = DRAWING_TYPE_RECTANGLE;
        state.hasDrawingType = true;
        panel->toolConfigChanged(state);

        GtkRadioButton* rectangleRow = radioButtonNamed(content, "Draw Rectangle");
        ASSERT_NE(rectangleRow, nullptr);
        EXPECT_TRUE(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rectangleRow)))
                << "the current drawing type is the one shown as chosen";
        for (GtkRadioButton* row: drawingTypeRows) {
            if (row != rectangleRow) {
                EXPECT_FALSE(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(row)))
                        << "a drawing type that is not current must not be shown as chosen";
            }
        }

        // Switching to another drawing type moves the choice with it.
        setDrawingTypeState(Action::TOOL_DRAW_RECTANGLE, false);
        setDrawingTypeState(Action::TOOL_DRAW_LINE, true);
        state.drawingType = DRAWING_TYPE_LINE;
        panel->toolConfigChanged(state);

        GtkRadioButton* lineRow = radioButtonNamed(content, "Draw Line");
        ASSERT_NE(lineRow, nullptr);
        EXPECT_TRUE(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(lineRow)));
        EXPECT_FALSE(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(rectangleRow)))
                << "the previous drawing type must not stay chosen";

        // The panel unregisters from the adapter before the window that owns its widgets goes.
        panel.reset();
        gtk_widget_destroy(window);
        g_object_unref(actions);
        fs::remove_all(settingsFile.parent_path());
    }
};
TEST_F(ToolPropertyActionSyncTest, thePanelAndTheActionsItIsBoundToStayInStepBothWays) {}

/*
 * Plan 003, step 3: a tool's own control opens its property popover when that tool is already
 * active, and keeps its old meaning when it is not.
 */
class ActiveToolPopoverGateTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        ToolFixture fixture;
        const fs::path settingsFile = freshSettingsFile("xournalpp-test-gtk_active_tool_gate");
        Settings settings{settingsFile};
        IconNameHelper icons{&settings};

        ToolPropertyRegistry registry;
        xoj::toolbar::addBuiltInToolPropertyProviders(registry, icons);
        ToolPropertyProvider* penProvider = registry.find(TOOL_PEN);
        ASSERT_NE(penProvider, nullptr);

        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);
        StubPresetListListener presetsListener;

        ToolPropertyPopoverFactory factory{fixture.adapter, settings, *penProvider, GTK_WINDOW(window),
                                           &presetsListener};

        ToolButton button{
                "PEN", AbstractToolItem::Category::TOOLS, Action::SELECT_TOOL, makeGVariant(TOOL_PEN), "tool-pencil",
                "Pen"};
        button.setPopoverFactory(&factory);

        // createItem() is protected on ToolButton but public on the base, which is what the
        // toolbar uses it through.
        auto item = static_cast<AbstractToolItem&>(button).createItem(true);
        ASSERT_NE(item.get(), nullptr);

        GtkWidget* toolbar = gtk_toolbar_new();
        gtk_container_add(GTK_CONTAINER(window), toolbar);
        gtk_container_add(GTK_CONTAINER(toolbar), GTK_WIDGET(item.get()));
        gtk_widget_show_all(window);

        // The item is the tool's main button plus the menu button that owns its popover.
        GtkWidget* mainButton = nullptr;
        GtkMenuButton* menuButton = nullptr;
        for (GtkWidget* widget: allWidgets(GTK_WIDGET(item.get()))) {
            if (GTK_IS_MENU_BUTTON(widget)) {
                menuButton = GTK_MENU_BUTTON(widget);
            } else if (mainButton == nullptr && GTK_IS_BUTTON(widget)) {
                mainButton = widget;
            }
        }
        ASSERT_NE(mainButton, nullptr);
        ASSERT_NE(menuButton, nullptr);

        GtkPopover* popover = gtk_menu_button_get_popover(menuButton);
        ASSERT_NE(popover, nullptr);
        ASSERT_FALSE(gtk_widget_get_visible(GTK_WIDGET(popover)));

        // A click on a tool that is not active keeps its old meaning: select it, and nothing else.
        fixture.adapter.selectTool(TOOL_ERASER);
        g_signal_emit_by_name(mainButton, "clicked");
        EXPECT_FALSE(gtk_widget_get_visible(GTK_WIDGET(popover)))
                << "a click on an inactive tool must not open its properties";

        // Clicking the tool that is already active opens its property popover.
        fixture.adapter.selectTool(TOOL_PEN);
        g_signal_emit_by_name(mainButton, "clicked");
        settle();
        EXPECT_TRUE(gtk_widget_get_visible(GTK_WIDGET(popover)))
                << "clicking the active tool opens its property popover";

        // Escape, a click outside and the focus return are GTK's, and all three need the popover to
        // be anchored to the tool's own control and to be modal.
        EXPECT_EQ(gtk_popover_get_relative_to(popover), GTK_WIDGET(menuButton))
                << "the popover is anchored to the tool's control, so Escape and the focus return land on it";
        EXPECT_TRUE(gtk_popover_get_modal(popover)) << "GTK pops a modal popover down on Escape";

        // GTK itself consumes Escape on the popover and leaves a plain key alone, which is the
        // mechanism the plan relies on. The popdown that follows cannot be observed from here: the
        // harness never maps the popover through a real window manager, so see the manual matrix in
        // test/README.md for what a user sees.
        EXPECT_FALSE(pressKey(GTK_WIDGET(popover), GDK_KEY_a)) << "a plain key is not GTK's to handle";
        EXPECT_TRUE(pressKey(GTK_WIDGET(popover), GDK_KEY_Escape)) << "Escape reaches GTK's popover handler";

        // The popover's own show handler makes its tool active, so a panel can never show one
        // tool's properties while another tool is in the user's hand. Hiding is done with
        // gtk_widget_hide() rather than gtk_popover_popdown(): the latter fades out over a
        // transition, and gtk_widget_show() would then be a no-op that emits no "show".
        gtk_widget_hide(GTK_WIDGET(popover));
        fixture.adapter.selectTool(TOOL_ERASER);
        gtk_popover_popup(popover);
        EXPECT_EQ(fixture.adapter.getState().toolType, TOOL_PEN) << "opening a tool's popover makes that tool active";

        gtk_widget_hide(GTK_WIDGET(popover));
        gtk_widget_destroy(window);
        fs::remove_all(settingsFile.parent_path());
    }
};
TEST_F(ActiveToolPopoverGateTest, clickingTheActiveToolOpensItsPopoverAndAnInactiveOneDoesNot) {}

/*
 * Plan 003, step 5: the favourite strip shows as many presets as the profile asks for and applies
 * the preset its button carries.
 */
class PresetFavoritesStripTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        ToolFixture fixture;
        const fs::path settingsFile = freshSettingsFile("xournalpp-test-gtk_preset_favorites");
        Settings settings{settingsFile};
        IconNameHelper icons{&settings};

        ToolPresetList presets;
        const std::string penId = presets.add(ToolPreset{
                .name = "Black fine pen", .toolType = TOOL_PEN, .color = Colors::black, .size = TOOL_SIZE_FINE});
        const std::string highlighterId = presets.add(ToolPreset{.name = "Yellow highlighter",
                                                                 .toolType = TOOL_HIGHLIGHTER,
                                                                 .color = Colors::yellow,
                                                                 .size = TOOL_SIZE_THICK});
        const std::string eraserId =
                presets.add(ToolPreset{.name = "Big eraser", .toolType = TOOL_ERASER, .size = TOOL_SIZE_VERY_THICK});
        ASSERT_TRUE(presets.setFavorite(penId, true));
        ASSERT_TRUE(presets.setFavorite(highlighterId, true));
        settings.setToolPresets(std::move(presets));

        PresetFavoritesItem item{"PRESET_FAVORITES", fixture.adapter, settings, icons};
        EXPECT_EQ(item.getToolDisplayName(), "Favourite presets");
        EXPECT_NE(item.getNewToolIcon(), nullptr);

        // createItem() is protected on the item but public on the base, which is what the toolbar
        // uses it through.
        auto toolItem = static_cast<AbstractToolItem&>(item).createItem(true);
        ASSERT_NE(toolItem.get(), nullptr);

        GtkWidget* strip = gtk_bin_get_child(GTK_BIN(toolItem.get()));
        ASSERT_NE(strip, nullptr);
        EXPECT_TRUE(hasCssClass(strip, "xoj-preset-strip"));

        // The profile decides how many favourites the toolbar shows.
        settings.setFavoritePresetCount(1);
        item.presetListChanged();
        EXPECT_EQ(childCount(strip), 1U) << "one favourite is shown when the profile asks for one";

        settings.setFavoritePresetCount(2);
        item.presetListChanged();
        ASSERT_EQ(childCount(strip), 2U);

        // The strip never shows more than the stored favourites, however large the count is.
        settings.setFavoritePresetCount(static_cast<int>(ToolPresetList::MAX_FAVORITES));
        item.presetListChanged();
        EXPECT_EQ(childCount(strip), 2U) << "the strip is capped by the number of favourites";

        // With no favourites the strip says so instead of collapsing silently.
        settings.setFavoritePresetCount(0);
        item.presetListChanged();
        ASSERT_EQ(childCount(strip), 1U);
        GList* children = gtk_container_get_children(GTK_CONTAINER(strip));
        ASSERT_NE(children, nullptr);
        EXPECT_TRUE(GTK_IS_LABEL(GTK_WIDGET(children->data)))
                << "an empty strip carries an explanatory label, not a blank space";
        g_list_free(children);

        // Move off any preset first: selecting a tool rebuilds the strip, and the button pointers
        // collected below would then be dangling.
        fixture.adapter.selectTool(TOOL_ERASER);

        settings.setFavoritePresetCount(2);
        item.presetListChanged();
        std::vector<GtkWidget*> buttons = buttonsOf(strip);
        ASSERT_EQ(buttons.size(), 2U);
        EXPECT_STREQ(gtk_button_get_label(GTK_BUTTON(buttons[0])), "Black fine pen")
                << "the favourites are shown in the order the user chose";
        EXPECT_STREQ(gtk_button_get_label(GTK_BUTTON(buttons[1])), "Yellow highlighter");
        EXPECT_STREQ(accessibleName(buttons[0]).c_str(), "Black fine pen")
                << "a favourite button carries the preset name as its accessible name";

        // A favourite button applies its own preset.
        g_signal_emit_by_name(buttons[0], "clicked");
        const ToolConfigState state = fixture.adapter.getState();
        EXPECT_EQ(state.toolType, TOOL_PEN) << "applying a favourite selects its tool";
        EXPECT_EQ(state.size, TOOL_SIZE_FINE);
        EXPECT_EQ(state.color, Colors::black);

        // Applying the other favourite through the strip reaches the same state as applying it
        // directly, so the strip is a second entry point and not a second code path. The click
        // above rebuilt the strip, so the buttons are collected again.
        buttons = buttonsOf(strip);
        ASSERT_EQ(buttons.size(), 2U);
        g_signal_emit_by_name(buttons[1], "clicked");

        const ToolPreset* highlighterPreset = settings.getToolPresets().findById(highlighterId);
        ASSERT_NE(highlighterPreset, nullptr);
        ToolFixture reference;
        ASSERT_TRUE(reference.adapter.applyPreset(*highlighterPreset));
        EXPECT_EQ(fixture.adapter.getState(), reference.adapter.getState());

        // The eraser preset stays in the list even though it is not a favourite.
        EXPECT_NE(settings.getToolPresets().findById(eraserId), nullptr);

        fs::remove_all(settingsFile.parent_path());
    }
};
TEST_F(PresetFavoritesStripTest, theStripShowsTheConfiguredNumberOfFavouritesAndAppliesThem) {}

/*
 * Plan 003, step 4: the active tool summary follows the tool, and it is the one toolbar control
 * that takes the focus on a click - because the popover it opens has to give the focus back to it
 * when it is closed with Escape.
 *
 * The popover itself is not reachable from here: the summary owns it privately and GTK does not
 * make it a child of the window in the widget tree, so what the popover does when it is shown is
 * asserted in ActiveToolPopoverGateTest, where the tool button hands its popover out. (The
 * accessible tree is the one place GTK does report it, which is how
 * ActiveToolSummaryPopoverOwnershipTest can watch it.)
 */
class ActiveToolSummaryFocusTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        ToolFixture fixture;
        const fs::path settingsFile = freshSettingsFile("xournalpp-test-gtk_active_tool_summary");
        Settings settings{settingsFile};
        IconNameHelper icons{&settings};

        ToolPropertyRegistry registry;
        xoj::toolbar::addBuiltInToolPropertyProviders(registry, icons);

        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);
        StubPresetListListener presetsListener;

        ActiveToolSummaryItem summary{"ACTIVE_TOOL_SUMMARY", fixture.adapter,  registry, settings,
                                      GTK_WINDOW(window),    &presetsListener, icons};
        auto item = static_cast<AbstractToolItem&>(summary).createItem(true);
        ASSERT_NE(item.get(), nullptr);

        GtkWidget* toolbar = gtk_toolbar_new();
        gtk_container_add(GTK_CONTAINER(window), toolbar);
        gtk_container_add(GTK_CONTAINER(toolbar), GTK_WIDGET(item.get()));
        gtk_widget_show_all(window);
        settle();

        GtkWidget* button = nullptr;
        for (GtkWidget* widget: allWidgets(GTK_WIDGET(item.get()))) {
            if (GTK_IS_BUTTON(widget)) {
                button = widget;
                break;
            }
        }
        ASSERT_NE(button, nullptr);

        auto summaryLabel = [&item]() -> std::string {
            for (GtkWidget* widget: allWidgets(GTK_WIDGET(item.get()))) {
                if (GTK_IS_LABEL(widget)) {
                    return gtk_label_get_text(GTK_LABEL(widget));
                }
            }
            return {};
        };

        // The summary names the active tool, for the eye and for a screen reader.
        fixture.adapter.selectTool(TOOL_PEN);
        const std::string penLabel = summaryLabel();
        EXPECT_FALSE(penLabel.empty()) << "the summary names the active tool";
        EXPECT_FALSE(accessibleName(button).empty()) << "and says so to a screen reader too";

        // It follows a change of tool, without anyone telling it to.
        fixture.adapter.selectTool(TOOL_ERASER);
        EXPECT_NE(summaryLabel(), penLabel) << "the summary follows the tool";

        // A tool without a panel has nothing to open, so the control that would open it says so
        // instead of being a dead control. A text tool is used rather than a selection tool: the
        // fixture has no Control, and switching to a selection tool needs one.
        fixture.adapter.selectTool(TOOL_TEXT);
        EXPECT_FALSE(gtk_widget_get_sensitive(button)) << "a tool without a panel has nothing to open";
        fixture.adapter.selectTool(TOOL_PEN);
        EXPECT_TRUE(gtk_widget_get_sensitive(button));

        // The focus return: GTK gives the focus back to the control the popover belongs to, and it
        // can only do that if the control was allowed to take the focus in the first place. This is
        // the one toolbar control that is, which is why the line is asserted here.
        EXPECT_TRUE(gtk_widget_get_focus_on_click(button))
                << "the summary takes the focus on a click so its popover can return it";

        // Clicking it opens the active tool's properties, and doing so makes its tool active: a
        // panel must never show one tool's configuration while another tool is in the user's hand.
        g_signal_emit_by_name(button, "clicked");
        settle();
        EXPECT_EQ(fixture.adapter.getState().toolType, TOOL_PEN);

        gtk_widget_destroy(window);
        fs::remove_all(settingsFile.parent_path());
    }
};
TEST_F(ActiveToolSummaryFocusTest, theSummaryFollowsTheToolAndTakesTheFocusItsPopoverReturnsTo) {}

/*
 * Plan 003, step 4: the summary caches one popover per tool. A popover anchored to a widget of a
 * window is owned by that window in GTK3, and the summary outlives its window in the application -
 * the toolbar is torn down with the window, and not always before it. Destroying the summary after
 * its window must therefore release only what the summary itself holds, and hold something in the
 * first place: a cache that believes it owns a reference it was never given unrefs memory the
 * window's teardown already freed.
 */
class ActiveToolSummaryPopoverOwnershipTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        ToolFixture fixture;
        const fs::path settingsFile = freshSettingsFile("xournalpp-test-gtk_active_tool_summary_ownership");
        Settings settings{settingsFile};
        IconNameHelper icons{&settings};

        ToolPropertyRegistry registry;
        xoj::toolbar::addBuiltInToolPropertyProviders(registry, icons);
        ASSERT_NE(registry.find(TOOL_PEN), nullptr) << "the pen is the tool whose popover this test opens";

        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 400, 200);
        StubPresetListListener presetsListener;

        // Everything the item does while it is taken down is watched from here.
        CriticalWatch criticals;

        {
            ActiveToolSummaryItem summary{"ACTIVE_TOOL_SUMMARY", fixture.adapter,  registry, settings,
                                          GTK_WINDOW(window),    &presetsListener, icons};
            auto item = static_cast<AbstractToolItem&>(summary).createItem(true);
            ASSERT_NE(item.get(), nullptr);

            GtkWidget* toolbar = gtk_toolbar_new();
            gtk_container_add(GTK_CONTAINER(window), toolbar);
            gtk_container_add(GTK_CONTAINER(toolbar), GTK_WIDGET(item.get()));
            gtk_widget_show_all(window);
            settle();

            const std::vector<GtkWidget*> buttons = buttonsOf(GTK_WIDGET(item.get()));
            ASSERT_GE(buttons.size(), 1U);
            GtkWidget* button = buttons.front();
            ASSERT_TRUE(gtk_widget_get_sensitive(button)) << "the active tool has properties to open";

            EXPECT_TRUE(anchoredPopovers(window).empty()) << "nothing is anchored before the first click";

            // The click builds the active tool's popover and caches it.
            fixture.adapter.selectTool(TOOL_PEN);
            g_signal_emit_by_name(button, "clicked");
            settle();

            const std::vector<GtkWidget*> afterFirstClick = anchoredPopovers(window);
            ASSERT_EQ(afterFirstClick.size(), 1U) << "the click builds the active tool's popover";
            EXPECT_EQ(gtk_popover_get_relative_to(GTK_POPOVER(afterFirstClick.front())), button)
                    << "and anchors it to the summary's own button";

            // A second click reuses what the cache holds instead of building another popover.
            g_signal_emit_by_name(button, "clicked");
            settle();
            EXPECT_EQ(anchoredPopovers(window), afterFirstClick) << "the popovers are built once and then cached";

            // The window goes first and takes the anchor - and with it everything GTK holds of the
            // popover - down. The summary is destroyed after it, which is where the cache is
            // released: it must free the popover and nothing else.
            gtk_widget_destroy(window);
            settle();
        }

        EXPECT_EQ(criticals.count(), 0U) << criticals.report();
        fs::remove_all(settingsFile.parent_path());
    }
};
TEST_F(ActiveToolSummaryPopoverOwnershipTest, theCachedPopoverIsReleasedWithoutTouchingWhatTheWindowFreed) {}

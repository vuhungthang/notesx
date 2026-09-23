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

#include <algorithm>  // for find_if
#include <memory>     // for unique_ptr, make_unique
#include <optional>
#include <string>
#include <vector>

#include <gio/gio.h>  // for GMenu, GSimpleAction, GSimpleActionGroup
#include <gtest/gtest.h>

#include "control/commands/CommandRegistry.h"       // for CommandRegistry, CommandEntry
#include "enums/Action.enum.h"                      // for Action
#include "gui/toolbarMenubar/AbstractToolItem.h"    // for AbstractToolItem
#include "util/raii/GObjectSPtr.h"                  // for GObjectSPtr

/*
 * Plan 007, step 1: where the commands come from.
 *
 * These tests describe the registry as a reader of things that already exist - the menu model GTK
 * builds from ui/mainmenubar.xml, the toolbar items the tool menu handler owns, the GAction the
 * action database put in the window's action map - so that nothing in it is a second list of
 * labels or accelerators. The GTK test CommandRegistryGtkTest reads the real menu with it; here the
 * model is built by hand so that every shape a menu can have is exercised on its own.
 */

namespace {

using xoj::command::ActionScope;
using xoj::command::CommandRegistry;

/// A menu item that carries a label and an action, the way ui/mainmenubar.xml writes them.
auto item(const char* label, const char* action) -> GMenuItem* { return g_menu_item_new(label, action); }

auto itemWithAccel(const char* label, const char* action, const char* accel) -> GMenuItem* {
    GMenuItem* entry = g_menu_item_new(label, action);
    g_menu_item_set_attribute(entry, "accel", "s", accel);
    return entry;
}

/// The id of the only command that is not the one given, for assertions that read like a query.
auto ids(const CommandRegistry& registry) -> std::vector<std::string> {
    std::vector<std::string> out;
    for (const auto& command: registry.all()) {
        out.emplace_back(command.metadata.id);
    }
    return out;
}

/// A toolbar item that activates an action, as every ToolButton in ToolMenuHandler does.
class StubToolItem final: public AbstractToolItem {
public:
    StubToolItem(std::string id, Category category, std::string displayName, std::optional<Action> action,
                 GVariant* target = nullptr, std::string icon = {}):
            AbstractToolItem(std::move(id), category),
            displayName(std::move(displayName)),
            action(action),
            iconName(std::move(icon)) {
        if (target != nullptr) {
            this->target.reset(target, xoj::util::refsink);
        }
    }

    auto getToolDisplayName() const -> std::string override { return this->displayName; }
    auto getNewToolIcon() const -> GtkWidget* override { return nullptr; }
    /// No widget: these tests are about what the item offers, not about what it draws.
    auto createItem(bool) -> xoj::util::WidgetSPtr override { return {}; }

    auto getCommandAction() const -> std::optional<Action> override { return this->action; }
    auto getCommandTarget() const -> GVariant* override { return this->target.get(); }
    auto getCommandIconName() const -> std::string override { return this->iconName; }

private:
    std::string displayName;
    std::optional<Action> action;
    xoj::util::GVariantSPtr target;
    std::string iconName;
};

/// An action map holding one action, as the window's map and the application's map hold theirs.
struct ActionMap {
    /// A string action with the given initial state. An empty state makes it an action without a
    /// parameter at all, which is what an action like app.quit is.
    ActionMap(std::string name, std::string initialState = {}, bool enabled = true) {
        group.reset(g_simple_action_group_new(), xoj::util::adopt);
        action = initialState.empty() ? g_simple_action_new(name.c_str(), nullptr)
                                      : g_simple_action_new_stateful(name.c_str(), G_VARIANT_TYPE_STRING,
                                                                     g_variant_new_string(initialState.c_str()));
        g_object_ref(action);
        g_simple_action_set_enabled(action, enabled);
        g_action_map_add_action(G_ACTION_MAP(group.get()), G_ACTION(action));
    }

    ~ActionMap() {
        if (action) {
            g_object_unref(action);
        }
    }

    auto map() const -> GActionMap* { return G_ACTION_MAP(group.get()); }

    auto state() const -> std::string {
        xoj::util::GVariantSPtr value(g_action_get_state(G_ACTION(action)), xoj::util::ref);
        return value ? std::string(g_variant_get_string(value.get(), nullptr)) : std::string();
    }

    xoj::util::GObjectSPtr<GSimpleActionGroup> group;
    GSimpleAction* action = nullptr;
};

}  // namespace

TEST(CommandRegistryTest, testEveryMenuEntryBecomesACommand) {
    xoj::util::GObjectSPtr<GMenu> file(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(file.get(), item("New", "win.new-file"));
    g_menu_append_item(file.get(), itemWithAccel("Open", "win.open", "<Ctrl>o"));
    xoj::util::GObjectSPtr<GMenu> edit(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(edit.get(), item("Undo", "win.undo"));
    xoj::util::GObjectSPtr<GMenuItem> fileSub( g_menu_item_new_submenu("_File", G_MENU_MODEL(file.get())),
                                              xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenuItem> editSub(g_menu_item_new_submenu("_Edit", G_MENU_MODEL(edit.get())),
                                             xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> root(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(root.get(), fileSub.get());
    g_menu_append_item(root.get(), editSub.get());

    CommandRegistry registry;
    registry.addFromMenuModel(G_MENU_MODEL(root.get()));

    EXPECT_EQ(ids(registry), (std::vector<std::string>{"win.new-file", "win.open", "win.undo"}));
    EXPECT_TRUE(registry.problems().empty()) << registry.problems().front();

    const auto* open = registry.findById("win.open");
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(open->metadata.title, "Open");
    EXPECT_EQ(open->metadata.category, "File");
    EXPECT_EQ(open->metadata.accelerator, "Ctrl+O");
    EXPECT_EQ(open->metadata.actionName, "win.open");
    EXPECT_EQ(open->action, "open");
    EXPECT_EQ(open->scope, ActionScope::WINDOW);

    const auto* undo = registry.findById("win.undo");
    ASSERT_NE(undo, nullptr);
    EXPECT_EQ(undo->metadata.category, "Edit") << "the top level submenu names the category";
}

TEST(CommandRegistryTest, testTheMenuIsWalkedThroughItsSectionsAndSubmenus) {
    xoj::util::GObjectSPtr<GMenu> recent(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(recent.get(), item("a.xopp", "win.open-recent('a.xopp')"));
    xoj::util::GObjectSPtr<GMenu> section(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(section.get(), item("Save", "win.save"));
    xoj::util::GObjectSPtr<GMenuItem> sectionLink(g_menu_item_new_section(nullptr, G_MENU_MODEL(section.get())),
                                                 xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenuItem> recentLink(g_menu_item_new_submenu("Recent", G_MENU_MODEL(recent.get())),
                                                xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> file(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(file.get(), sectionLink.get());
    g_menu_append_item(file.get(), recentLink.get());
    xoj::util::GObjectSPtr<GMenuItem> fileLink(g_menu_item_new_submenu("File", G_MENU_MODEL(file.get())),
                                               xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> root(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(root.get(), fileLink.get());

    CommandRegistry registry;
    registry.addFromMenuModel(G_MENU_MODEL(root.get()));

    ASSERT_EQ(registry.all().size(), 2u);
    for (const auto& command: registry.all()) {
        EXPECT_EQ(command.metadata.category, "File") << "a section and a submenu keep their owner's category";
    }
    EXPECT_EQ(registry.all()[0].action, "save");
    EXPECT_EQ(registry.all()[0].metadata.id, "win.save");
    EXPECT_EQ(registry.all()[1].metadata.title, "a.xopp");
}

TEST(CommandRegistryTest, testAMnemonicIsNotPartOfTheTitle) {
    xoj::util::GObjectSPtr<GMenu> file(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(file.get(), item("_Annotate PDF", "win.annotate-pdf"));
    g_menu_append_item(file.get(), item("Ex__port", "win.export-as"));
    xoj::util::GObjectSPtr<GMenuItem> fileLink(g_menu_item_new_submenu("_File", G_MENU_MODEL(file.get())),
                                               xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> root(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(root.get(), fileLink.get());

    CommandRegistry registry;
    registry.addFromMenuModel(G_MENU_MODEL(root.get()));

    ASSERT_EQ(registry.all().size(), 2u);
    EXPECT_EQ(registry.all()[0].metadata.title, "Annotate PDF");
    EXPECT_EQ(registry.all()[1].metadata.title, "Ex_port") << "a doubled underscore is an escaped one";
}

TEST(CommandRegistryTest, testAnEntryWithoutALabelOrAnActionIsNotACommand) {
    xoj::util::GObjectSPtr<GMenu> file(g_menu_new(), xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenuItem> noLabel(g_menu_item_new(nullptr, "win.save"), xoj::util::adopt);
    g_menu_append_item(file.get(), noLabel.get());
    xoj::util::GObjectSPtr<GMenuItem> noAction(g_menu_item_new("A placeholder", nullptr), xoj::util::adopt);
    g_menu_append_item(file.get(), noAction.get());
    g_menu_append_item(file.get(), item("Print", "win.print"));
    xoj::util::GObjectSPtr<GMenuItem> fileLink(g_menu_item_new_submenu("File", G_MENU_MODEL(file.get())),
                                               xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> root(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(root.get(), fileLink.get());

    CommandRegistry registry;
    registry.addFromMenuModel(G_MENU_MODEL(root.get()));

    EXPECT_EQ(ids(registry), (std::vector<std::string>{"win.print"}));
}

TEST(CommandRegistryTest, testTheAcceleratorsTheApplicationHoldsComeBeforeTheMenuEntrysOwn) {
    xoj::util::GObjectSPtr<GMenu> file(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(file.get(), itemWithAccel("New", "win.new-file", "<Ctrl>n"));
    g_menu_append_item(file.get(), itemWithAccel("Open", "win.open", "<Ctrl>o"));
    xoj::util::GObjectSPtr<GMenuItem> fileLink(g_menu_item_new_submenu("File", G_MENU_MODEL(file.get())),
                                               xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> root(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(root.get(), fileLink.get());

    CommandRegistry registry([](const std::string& action) -> std::vector<std::string> {
        return action == "win.new-file" ? std::vector<std::string>{"<Ctrl>K", "<Ctrl>N"} : std::vector<std::string>{};
    });
    registry.addFromMenuModel(G_MENU_MODEL(root.get()));

    const auto* newFile = registry.findById("win.new-file");
    ASSERT_NE(newFile, nullptr);
    EXPECT_EQ(newFile->metadata.accelerator, "Ctrl+K") << "the application's accelerators are the live ones";

    const auto* open = registry.findById("win.open");
    ASSERT_NE(open, nullptr);
    EXPECT_EQ(open->metadata.accelerator, "Ctrl+O") << "a menu entry that has no registered accelerator keeps its own";
}

TEST(CommandRegistryTest, testAMenuEntryWithAParameterKeepsIt) {
    xoj::util::GObjectSPtr<GMenu> view(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(view.get(), item("Portrait", "win.select-toolbar('portrait')"));
    g_menu_append_item(view.get(), item("Zoom to fit", "win.zoom-fit"));
    xoj::util::GObjectSPtr<GMenu> viewSub(g_menu_new(), xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenuItem> targeted(g_menu_item_new("Page 3", nullptr), xoj::util::adopt);
    g_menu_item_set_action_and_target_value(targeted.get(), "win.goto-page", g_variant_new_int32(3));
    g_menu_append_item(viewSub.get(), targeted.get());
    xoj::util::GObjectSPtr<GMenuItem> viewLink(g_menu_item_new_submenu("View", G_MENU_MODEL(view.get())),
                                               xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenuItem> subLink(g_menu_item_new_submenu("Page 3", G_MENU_MODEL(viewSub.get())),
                                              xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> root(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(root.get(), viewLink.get());
    g_menu_append_item(root.get(), subLink.get());

    CommandRegistry registry;
    registry.addFromMenuModel(G_MENU_MODEL(root.get()));

    EXPECT_TRUE(registry.problems().empty()) << registry.problems().front();

    const auto* portrait = registry.findById("win.select-toolbar('portrait')");
    ASSERT_NE(portrait, nullptr);
    EXPECT_EQ(portrait->action, "select-toolbar");
    ASSERT_TRUE(portrait->target);
    EXPECT_EQ(std::string(g_variant_get_string(portrait->target.get(), nullptr)), "portrait");

    const auto* gotoPage = registry.findById("win.goto-page(3)");
    ASSERT_NE(gotoPage, nullptr);
    ASSERT_TRUE(gotoPage->target);
    EXPECT_EQ(g_variant_get_int32(gotoPage->target.get()), 3);
}

TEST(CommandRegistryTest, testAPluginMenuEntryIsACommandLikeAnyOther) {
    xoj::util::GObjectSPtr<GMenu> plugin(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(plugin.get(), itemWithAccel("Export to LaTeX", "win.plugin-3", "<Ctrl>L"));
    xoj::util::GObjectSPtr<GMenuItem> pluginSub(g_menu_item_new_submenu("Plugin", G_MENU_MODEL(plugin.get())),
                                                xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> root(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(root.get(), pluginSub.get());

    CommandRegistry registry;
    registry.addFromMenuModel(G_MENU_MODEL(root.get()));

    const auto* command = registry.findById("win.plugin-3");
    ASSERT_NE(command, nullptr) << "a plugin entry needs no plugin API of its own to be a command";
    EXPECT_EQ(command->metadata.title, "Export to LaTeX");
    EXPECT_EQ(command->metadata.category, "Plugin");
    EXPECT_EQ(command->metadata.accelerator, "Ctrl+L");
    EXPECT_FALSE(command->knownAction) << "a plugin action is not one of the action database's";
}

TEST(CommandRegistryTest, testAToolbarItemContributesItsActionAndTarget) {
    std::vector<std::unique_ptr<AbstractToolItem>> items;
    items.emplace_back(std::make_unique<StubToolItem>("PEN", AbstractToolItem::Category::TOOLS, "Pen",
                                                      Action::SELECT_TOOL, g_variant_new_string("pen"),
                                                      "xopp-tool-pencil"));
    items.emplace_back(std::make_unique<StubToolItem>("SELECT_REGION", AbstractToolItem::Category::SELECTION,
                                                      "Select Region", Action::SELECT_TOOL,
                                                      g_variant_new_string("select-region")));
    // A plugin toolbar button: it calls a Lua callback and has no action of its own.
    items.emplace_back(std::make_unique<StubToolItem>("\"Plugin::x\"", AbstractToolItem::Category::PLUGINS,
                                                      "Plugin Button", std::nullopt));

    CommandRegistry registry;
    registry.addFromToolItems(items);

    EXPECT_TRUE(registry.problems().empty()) << registry.problems().front();
    ASSERT_EQ(registry.all().size(), 2u) << "an item that has no action has no command";

    const auto* pen = registry.findById("win.select-tool:PEN");
    ASSERT_NE(pen, nullptr);
    EXPECT_EQ(pen->metadata.title, "Pen");
    EXPECT_EQ(pen->metadata.category, "Tools");
    EXPECT_EQ(pen->metadata.icon, "xopp-tool-pencil");
    EXPECT_EQ(pen->metadata.actionName, "win.select-tool");
    EXPECT_EQ(pen->knownAction, Action::SELECT_TOOL);
    ASSERT_TRUE(pen->target);
    EXPECT_EQ(std::string(g_variant_get_string(pen->target.get(), nullptr)), "pen");

    // Two items reached through the same action stay two commands with two ids.
    EXPECT_NE(registry.findById("win.select-tool:SELECT_REGION"), nullptr);
}

TEST(CommandRegistryTest, testAnItemAndAMenuEntryOnOneActionBothGetTheirOwnId) {
    xoj::util::GObjectSPtr<GMenu> tools(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(tools.get(), item("Select Region", "win.select-tool('select-region')"));
    xoj::util::GObjectSPtr<GMenuItem> toolsSub(g_menu_item_new_submenu("Tools", G_MENU_MODEL(tools.get())),
                                               xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> root(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(root.get(), toolsSub.get());

    std::vector<std::unique_ptr<AbstractToolItem>> items;
    items.emplace_back(std::make_unique<StubToolItem>("SELECT_REGION", AbstractToolItem::Category::SELECTION,
                                                      "Select Region", Action::SELECT_TOOL,
                                                      g_variant_new_string("select-region")));

    CommandRegistry registry;
    registry.addFromMenuModel(G_MENU_MODEL(root.get()));
    registry.addFromToolItems(items);

    EXPECT_EQ(registry.all().size(), 2u);
    EXPECT_TRUE(registry.problems().empty()) << registry.problems().front();
}

TEST(CommandRegistryTest, testTheExtensionPointTakesACommandOfItsOwn) {
    CommandRegistry registry;
    xoj::command::CommandEntry entry;
    entry.metadata.id = "win.shortcut-reference";
    entry.metadata.title = "Keyboard Shortcuts";
    entry.metadata.category = "Help";
    entry.metadata.actionName = "win.shortcut-reference";
    entry.action = "shortcut-reference";
    entry.scope = ActionScope::WINDOW;
    registry.addCommand(std::move(entry));

    const auto* command = registry.findById("win.shortcut-reference");
    ASSERT_NE(command, nullptr);
    EXPECT_EQ(command->metadata.title, "Keyboard Shortcuts");
    EXPECT_TRUE(registry.problems().empty()) << registry.problems().front();
}

TEST(CommandRegistryTest, testTheSameIdTwiceIsAProblem) {
    CommandRegistry registry;
    xoj::command::CommandEntry entry;
    entry.metadata.id = "win.save";
    entry.metadata.title = "Save";
    entry.metadata.category = "File";
    entry.action = "save";
    registry.addCommand(entry);
    registry.addCommand(std::move(entry));

    auto problems = registry.problems();
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems.front().find("win.save"), std::string::npos) << problems.front();
}

TEST(CommandRegistryTest, testActivationGoesThroughTheActionOfTheWindow) {
    ActionMap map("pick", "none");
    CommandRegistry::Maps maps;
    maps.window = map.map();

    xoj::util::GObjectSPtr<GMenu> view(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(view.get(), item("Portrait", "win.pick('portrait')"));
    xoj::util::GObjectSPtr<GMenuItem> viewLink(g_menu_item_new_submenu("View", G_MENU_MODEL(view.get())),
                                               xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> root(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(root.get(), viewLink.get());

    CommandRegistry registry;
    registry.addFromMenuModel(G_MENU_MODEL(root.get()));

    const auto* command = registry.findById("win.pick('portrait')");
    ASSERT_NE(command, nullptr);
    EXPECT_TRUE(CommandRegistry::isEnabled(*command, maps));
    EXPECT_TRUE(CommandRegistry::activate(*command, maps));
    EXPECT_EQ(map.state(), "portrait");
}

TEST(CommandRegistryTest, testADisabledActionIsShownAsUnavailableAndIsNotActivated) {
    ActionMap map("pick", "none", /*enabled=*/false);
    CommandRegistry::Maps maps;
    maps.window = map.map();

    xoj::util::GObjectSPtr<GMenu> view(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(view.get(), item("Portrait", "win.pick('portrait')"));
    xoj::util::GObjectSPtr<GMenuItem> viewLink(g_menu_item_new_submenu("View", G_MENU_MODEL(view.get())),
                                               xoj::util::adopt);
    xoj::util::GObjectSPtr<GMenu> root(g_menu_new(), xoj::util::adopt);
    g_menu_append_item(root.get(), viewLink.get());

    CommandRegistry registry;
    registry.addFromMenuModel(G_MENU_MODEL(root.get()));

    const auto* command = registry.findById("win.pick('portrait')");
    ASSERT_NE(command, nullptr);
    EXPECT_FALSE(CommandRegistry::isEnabled(*command, maps));
    EXPECT_FALSE(CommandRegistry::activate(*command, maps));
    EXPECT_EQ(map.state(), "none") << "a disabled action cannot be activated";
}

TEST(CommandRegistryTest, testACommandWhoseActionIsNotThereIsUnavailable) {
    ActionMap map("other", "none");
    CommandRegistry::Maps maps;
    maps.window = map.map();

    CommandRegistry registry;
    xoj::command::CommandEntry entry;
    entry.metadata.id = "win.gone";
    entry.metadata.title = "Gone";
    entry.metadata.category = "File";
    entry.action = "gone";
    registry.addCommand(std::move(entry));

    const auto* command = registry.findById("win.gone");
    ASSERT_NE(command, nullptr);
    EXPECT_EQ(CommandRegistry::lookupAction(*command, maps), nullptr);
    EXPECT_FALSE(CommandRegistry::isEnabled(*command, maps));
    EXPECT_FALSE(CommandRegistry::activate(*command, maps));
}

TEST(CommandRegistryTest, testAnApplicationActionIsLookedUpInTheApplication) {
    ActionMap map("quit");
    CommandRegistry::Maps maps;
    maps.application = map.map();

    CommandRegistry registry;
    xoj::command::CommandEntry entry;
    entry.metadata.id = "app.quit";
    entry.metadata.title = "Quit";
    entry.metadata.category = "File";
    entry.action = "quit";
    entry.scope = ActionScope::APPLICATION;
    registry.addCommand(std::move(entry));

    const auto* command = registry.findById("app.quit");
    ASSERT_NE(command, nullptr);
    EXPECT_TRUE(CommandRegistry::isEnabled(*command, maps));
    EXPECT_TRUE(CommandRegistry::activate(*command, maps));
}

TEST(CommandRegistryTest, testACommandCanSayWhyItIsUnavailable) {
    ActionMap map("pick", "none", /*enabled=*/false);
    CommandRegistry::Maps maps;
    maps.window = map.map();

    CommandRegistry registry({}, [](const xoj::command::CommandEntry&) -> std::optional<std::string> {
        return std::string("There is only one page");
    });
    xoj::command::CommandEntry entry;
    entry.metadata.id = "win.pick";
    entry.metadata.title = "Pick";
    entry.metadata.category = "View";
    entry.action = "pick";
    registry.addCommand(std::move(entry));

    const auto* command = registry.findById("win.pick");
    ASSERT_NE(command, nullptr);
    EXPECT_FALSE(CommandRegistry::isEnabled(*command, maps));
    EXPECT_EQ(registry.disabledReason(*command), std::optional<std::string>("There is only one page"));

    // A command that is available has nothing to explain.
    ActionMap enabled("pick", "none");
    CommandRegistry::Maps enabledMaps;
    enabledMaps.window = enabled.map();
    EXPECT_FALSE(registry.disabledReason(*command, enabledMaps).has_value());
}

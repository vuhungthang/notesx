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

#include <algorithm>   // for find_if
#include <functional>  // for function
#include <memory>      // for unique_ptr, make_unique, make_shared
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>  // for GtkApplication, GtkApplicationWindow

#include "../dialog/GtkTest.h"
#include "control/Control.h"                     // for Control
#include "control/ToolHandler.h"                 // for ToolHandler
#include "control/actions/ActionDatabase.h"      // for ActionDatabase
#include "control/commands/CommandRegistry.h"    // for CommandRegistry
#include "gui/GladeSearchpath.h"                 // for GladeSearchpath
#include "gui/MainWindow.h"                      // for MainWindow
#include "gui/toolbarMenubar/ToolMenuHandler.h"  // for ToolMenuHandler
#include "model/PageRef.h"                       // for PageRef
#include "model/XojPage.h"                       // for XojPage

#include "config-test.h"

/*
 * Plan 007, steps 1 and 5: the registry as the application itself builds it.
 *
 * The unit tests next door build their menu models by hand, one shape at a time; here the registry
 * reads the real menu - the one GTK loads from ui/mainmenubar.xml - and the real toolbar items
 * ToolMenuHandler built. That is what makes the promise of step 1 checkable: a menu entry or an
 * accelerator changed in the application shows up in the palette and in the reference without
 * anyone editing a second list.
 */

using xoj::command::CommandEntry;
using xoj::command::CommandRegistry;

namespace {

/// Lets GTK finish what it queued, so the window is settled before anything is read off it.
void settle() {
    for (int i = 0; i < 4; i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(20000);
    }
}

/// Runs what the main context has ready until it has nothing left.
///
/// A widget that queues a one-shot callback and is gone before it runs leaves a source in the main
/// context holding a pointer to the dead widget, which the next test's first settle() then calls.
/// Draining here, while the window is still alive, is what keeps a window that is being torn down
/// from leaving anything of itself behind for the next one.
void drain() {
    for (int i = 0; i < 50 && g_main_context_pending(nullptr); i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(10000);
    }
}

}  // namespace

class ApplicationCommands;

/// One test: what the window and the registry are asked to do once the application is up.
struct CommandScenario {
    const char* name;
    std::function<void(ApplicationCommands&)> run;
};

/// Named by the case, not by the bytes of the struct a gtest failure would print.
void PrintTo(const CommandScenario& scenario, std::ostream* out) { *out << scenario.name; }

constexpr size_t TEST_PAGES = 3;

class ApplicationCommands: public GtkTest, public ::testing::WithParamInterface<CommandScenario> {
public:
    /// The registry, built the way the window builds it: out of what the application holds.
    auto registry() const -> CommandRegistry {
        ActionDatabase* db = this->control->getActionDatabase();
        GtkApplication* app = GTK_APPLICATION(gtk_window_get_application(GTK_WINDOW(this->win->getWindow())));
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
        commands.addFromMenuModel(this->win->getMenuModel());
        commands.addFromToolItems(this->win->getToolMenuHandler()->getToolItems());
        return commands;
    }

    auto maps() const -> CommandRegistry::Maps {
        CommandRegistry::Maps maps;
        maps.window = G_ACTION_MAP(this->win->getWindow());
        maps.application = G_ACTION_MAP(gtk_window_get_application(GTK_WINDOW(this->win->getWindow())));
        return maps;
    }

    /// The one command a menu offers under this title, or nullptr when it offers none.
    static auto byTitle(const CommandRegistry& commands, const std::string& title, const std::string& action)
            -> const CommandEntry* {
        for (const CommandEntry& command: commands.all()) {
            if (command.metadata.title == title && command.action == action) {
                return &command;
            }
        }
        return nullptr;
    }

private:
    void runTest(GtkApplication* app) final {
        this->glade = std::make_unique<GladeSearchpath>();
        this->glade->addSearchDirectory(GET_UI_FOLDER);
        // The page templates a Control reads; without them it tells the user about the missing file
        // through a message box, in the middle of a test.
        this->glade->addSearchDirectory(GET_PAGE_TEMPLATE_FOLDER);

        this->control = std::make_unique<Control>(G_APPLICATION(app), this->glade.get(), true);
        this->win = std::make_unique<MainWindow>(this->glade.get(), this->control.get(), GTK_APPLICATION(app));
        this->control->initWindow(this->win.get());
        this->win->populate(this->glade.get());
        this->win->show(nullptr);
        settle();

        for (size_t i = 0; i < TEST_PAGES; i++) {
            this->control->insertPage(std::make_shared<XojPage>(595.28, 841.89), i, false);
        }
        settle();
        this->control->getPageSelection().reset(TEST_PAGES);

        this->GetParam().run(*this);

        // Everything this window queued has to have run before it goes: see drain().
        drain();

        this->win.reset();
        this->control.reset();
        this->glade.reset();
    }

public:
    std::unique_ptr<GladeSearchpath> glade;
    std::unique_ptr<Control> control;
    std::unique_ptr<MainWindow> win;
};

void realMenuIsComplete(ApplicationCommands& test) {
    CommandRegistry commands = test.registry();

    const std::vector<std::string> problems = commands.problems();
    EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems.front());
    EXPECT_GT(commands.all().size(), 40u) << "the menus and the toolbars offer more than a handful of commands";

    for (const CommandEntry& command: commands.all()) {
        EXPECT_FALSE(command.metadata.id.empty());
        EXPECT_FALSE(command.metadata.title.empty()) << command.metadata.id;
        EXPECT_FALSE(command.metadata.category.empty()) << command.metadata.id;
    }

    const CommandEntry* exportPdf = commands.findById("win.export-as-pdf");
    ASSERT_NE(exportPdf, nullptr);
    EXPECT_EQ(exportPdf->metadata.title, "Export as PDF");
    EXPECT_EQ(exportPdf->metadata.category, "File");

    /*
     * One action with one target is one command. The toolbars offer the same actions as the menus,
     * so without this the palette would show two Undos and the reference would call the shortcut of
     * a command a conflict with itself.
     */
    for (size_t i = 0; i < commands.all().size(); i++) {
        for (size_t j = i + 1; j < commands.all().size(); j++) {
            EXPECT_NE(CommandRegistry::commandKey(commands.all()[i]), CommandRegistry::commandKey(commands.all()[j]))
                    << commands.all()[i].metadata.id << " and " << commands.all()[j].metadata.id
                    << " are the same command offered twice";
        }
    }

    // Undo is a menu entry and a tool button; it is one command, and it is the menu's.
    const CommandEntry* undo = commands.findById("win.undo");
    ASSERT_NE(undo, nullptr);
    EXPECT_EQ(undo->metadata.category, "Edit") << "the menu entry is the command, not the tool button";
    EXPECT_EQ(commands.findById("win.undo:UNDO"), nullptr) << "the tool button added no second Undo";

    // The palette is opened by Ctrl+K: the palette itself is the one command allowed to hold it, and
    // nothing else may, or one of the two would quietly win over the other.
    for (const CommandEntry& command: commands.all()) {
        if (command.metadata.id == "win.command-palette") {
            EXPECT_EQ(command.metadata.accelerator, "Ctrl+K") << "the palette holds its own shortcut";
        } else {
            EXPECT_NE(command.metadata.accelerator, "Ctrl+K") << command.metadata.id;
        }
    }

    // The toolbar items are in there too, with the tool they select as their target.
    const CommandEntry* pen = ApplicationCommands::byTitle(commands, "Pen", "select-tool");
    ASSERT_NE(pen, nullptr) << "the pen of the Focus toolbar is a command as well";
    EXPECT_EQ(pen->metadata.category, "Tools");
}

void acceleratorFollowsTheLiveAction(ApplicationCommands& test) {
    GtkApplication* app = GTK_APPLICATION(gtk_window_get_application(GTK_WINDOW(test.win->getWindow())));

    // Ctrl+N is written on the menu entry itself; Ctrl+Z is registered for the action in
    // control/actions/ActionProperties.h. Both are read where they live, neither is repeated.
    CommandRegistry commands = test.registry();
    const CommandEntry* newFile = commands.findById("win.new-file");
    ASSERT_NE(newFile, nullptr);
    EXPECT_EQ(newFile->metadata.accelerator, "Ctrl+N");

    const CommandEntry* undo = commands.findById("win.undo");
    ASSERT_NE(undo, nullptr);
    EXPECT_EQ(undo->metadata.accelerator, "Ctrl+Z");

    // Changing the accelerator of an action changes what the registry reports, without anyone
    // touching the command description: this is what step 5 promises the reference is built on.
    const char* changed[] = {"<Ctrl><Shift>U", nullptr};
    gtk_application_set_accels_for_action(app, "win.undo", changed);

    CommandRegistry afterChange = test.registry();
    const CommandEntry* undoAfter = afterChange.findById("win.undo");
    ASSERT_NE(undoAfter, nullptr);
    EXPECT_EQ(undoAfter->metadata.accelerator, "Ctrl+Shift+U");

    const char* original[] = {"<Ctrl>Z", nullptr};
    gtk_application_set_accels_for_action(app, "win.undo", original);
}

void activatingAMenuCommandRunsTheToolItNames(ApplicationCommands& test) {
    CommandRegistry commands = test.registry();
    const CommandRegistry::Maps actionMaps = test.maps();

    test.control->selectTool(TOOL_PEN);
    ASSERT_EQ(test.control->getToolHandler()->getActiveTool()->getToolType(), TOOL_PEN);

    // The eraser entry of the Tools menu: win.select-tool with the eraser as its target.
    const CommandEntry* eraser = ApplicationCommands::byTitle(commands, "Eraser", "select-tool");
    ASSERT_NE(eraser, nullptr) << "the Tools menu offers the eraser";
    ASSERT_TRUE(CommandRegistry::isEnabled(*eraser, actionMaps)) << eraser->metadata.id;

    EXPECT_TRUE(CommandRegistry::activate(*eraser, actionMaps));
    EXPECT_EQ(test.control->getToolHandler()->getActiveTool()->getToolType(), TOOL_ERASER)
            << "activating a command goes through the action the menu entry itself uses";
}

void aCommandSaysWhyItCannotBeUsed(ApplicationCommands& test) {
    CommandRegistry commands = test.registry();
    const CommandRegistry::Maps actionMaps = test.maps();

    const CommandEntry* deletePage = commands.findById("win.delete-page");
    ASSERT_NE(deletePage, nullptr);
    ASSERT_TRUE(deletePage->knownAction.has_value());

    // One page of three selected: the command is available and has nothing to explain.
    test.control->getPageSelection().setSelection({1});
    test.control->updatePageActions();
    EXPECT_TRUE(CommandRegistry::isEnabled(*deletePage, actionMaps));
    EXPECT_FALSE(commands.disabledReason(*deletePage, actionMaps).has_value());

    // Every page selected: deleting them would leave the document without a page, and the action
    // says so.
    test.control->getPageSelection().selectAll(TEST_PAGES);
    test.control->updatePageActions();
    EXPECT_FALSE(CommandRegistry::isEnabled(*deletePage, actionMaps));
    const std::optional<std::string> reason = commands.disabledReason(*deletePage, actionMaps);
    ASSERT_TRUE(reason.has_value());
    EXPECT_FALSE(reason->empty());
}

void aKeywordFindsAnActionCalledSomethingElse(ApplicationCommands& test) {
    CommandRegistry commands = test.registry();

    const CommandEntry* preferences = commands.findById("app.preferences");
    ASSERT_NE(preferences, nullptr) << "the preferences dialog is a command like any other";
    const std::vector<std::string>& keywords = preferences->metadata.keywords;
    EXPECT_NE(std::find(keywords.begin(), keywords.end(), "settings"), keywords.end())
            << "a keyword is declared next to the action it belongs to, not in a list of its own";

    // And the search uses it: nothing in the title says "settings".
    const std::vector<size_t> ranked = xoj::command::rankCommands(commands.metadata(), "settings");
    ASSERT_FALSE(ranked.empty());
    EXPECT_EQ(commands.all()[ranked.front()].metadata.id, "app.preferences");
}

INSTANTIATE_TEST_SUITE_P(
        CommandScenarios, ApplicationCommands,
        ::testing::Values(CommandScenario{"realMenuIsComplete", &realMenuIsComplete},
                          CommandScenario{"acceleratorFollowsTheLiveAction", &acceleratorFollowsTheLiveAction},
                          CommandScenario{"activatingAMenuCommandRunsTheToolItNames",
                                          &activatingAMenuCommandRunsTheToolItNames},
                          CommandScenario{"aCommandSaysWhyItCannotBeUsed", &aCommandSaysWhyItCannotBeUsed},
                          CommandScenario{"aKeywordFindsAnActionCalledSomethingElse",
                                          &aKeywordFindsAnActionCalledSomethingElse}));

/*
 * The whole test runs inside `ApplicationCommands::runTest()`, which GtkTest invokes once the
 * GtkApplication is up - a Control and a MainWindow need it to be. The body below is empty on
 * purpose; what a scenario does and checks is the function handed to INSTANTIATE_TEST_SUITE_P.
 */
TEST_P(ApplicationCommands, theScenario) {}

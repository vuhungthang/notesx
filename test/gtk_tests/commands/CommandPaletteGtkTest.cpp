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

#include <algorithm>   // for count
#include <cstring>     // for strcmp
#include <functional>  // for function
#include <memory>      // for unique_ptr, make_unique, make_shared
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>  // for GtkApplication, GtkApplicationWindow

#include "../dialog/GtkTest.h"
#include "control/Control.h"                   // for Control
#include "control/commands/CommandRegistry.h"  // for CommandRegistry
#include "control/settings/Settings.h"         // for Settings
#include "gui/CommandPalette.h"                // for CommandPalette
#include "gui/GladeSearchpath.h"               // for GladeSearchpath
#include "gui/MainWindow.h"                    // for MainWindow
#include "gui/XournalView.h"                   // for XournalView, the canvas
#include "model/Document.h"                    // for Document
#include "model/PageRef.h"                     // for PageRef
#include "model/XojPage.h"                     // for XojPage
#include "undo/UndoRedoHandler.h"              // for UndoRedoHandler

#include "config-test.h"

/*
 * Plan 007, step 2: the command palette, as a keyboard-only user meets it.
 *
 * The palette is opened by the action Ctrl+K is registered for - not by calling open() - and driven
 * through the same widgets a user presses keys on, so what these tests show is the path the shortcut
 * takes: the action, the registry read fresh from the menus, the filtering, the row the keyboard is
 * on, and the GAction the row runs.
 *
 * Where the keyboard is is read as the window's focus widget rather than with
 * gtk_widget_has_focus(): a window that has never been given the focus by a window manager - which
 * is every window in a headless test - reports has_focus() false for all of its widgets, even the
 * one it would deliver key events to.
 */

using xoj::command::CommandEntry;
using xoj::command::CommandPalette;
using xoj::command::CommandRegistry;

namespace {

/// Lets GTK finish what it queued, so the window and the popover have settled.
void settle() {
    for (int i = 0; i < 4; i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(20000);
    }
}

/// Runs what the main context has ready until it has nothing left: see the note in
/// CommandRegistryGtkTest, where a window that was torn down left a queued callback behind for the
/// next case.
void drain() {
    for (int i = 0; i < 50 && g_main_context_pending(nullptr); i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(10000);
    }
}

/// The widget the window would deliver a key press to: what "the keyboard is here" means.
auto focusedWidget(GtkWidget* window) -> GtkWidget* { return gtk_window_get_focus(GTK_WINDOW(window)); }

/// What a widget is, for an assertion message that says where the keyboard actually ended up.
auto describeWidget(GtkWidget* widget) -> std::string {
    if (widget == nullptr) {
        return "(no widget at all)";
    }
    std::string description = G_OBJECT_TYPE_NAME(widget);
    const char* name = gtk_widget_get_name(widget);
    if (name != nullptr && *name != '\0') {
        description += " \"";
        description += name;
        description += "\"";
    }
    return description;
}

}  // namespace

class CommandPaletteKeyboard;

/// One test: what the palette and the window are asked to do once the application is up.
struct PaletteScenario {
    const char* name;
    std::function<void(CommandPaletteKeyboard&)> run;
};

/// Named by the case, not by the bytes of the struct a gtest failure would print.
void PrintTo(const PaletteScenario& scenario, std::ostream* out) { *out << scenario.name; }

constexpr size_t PALETTE_TEST_PAGES = 3;

class CommandPaletteKeyboard: public GtkTest, public ::testing::WithParamInterface<PaletteScenario> {
public:
    /// The palette the window built, as the user reaches it.
    auto palette() const -> CommandPalette* { return this->win->getCommandPalette(); }
    /// The widget the window would give a key press to.
    auto focused() const -> GtkWidget* { return focusedWidget(this->win->getWindow()); }

    /**
     * Press a key on the focused widget, the way a key press arrives: aimed at the window of the
     * toplevel, delivered to the widget the window would deliver it to.
     *
     * The event takes its own reference to the window, because freeing it takes one: without the
     * g_object_ref() below the event's window is a borrowed one, and dropping that reference here
     * frees a window the widget hierarchy is still using - which is how a test turns into a
     * dangling pointer three key presses later.
     */
    static void pressKey(GtkWidget* widget, guint keyval) {
        GtkWidget* toplevel = gtk_widget_get_toplevel(widget);
        ASSERT_TRUE(GTK_IS_WINDOW(toplevel));
        GdkWindow* window = gtk_widget_get_window(toplevel);
        ASSERT_NE(window, nullptr) << "the window the key press arrives at";

        GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
        event->key.window = static_cast<GdkWindow*>(g_object_ref(window));
        event->key.send_event = TRUE;
        event->key.time = GDK_CURRENT_TIME;
        event->key.state = static_cast<GdkModifierType>(0);
        event->key.keyval = keyval;
        event->key.hardware_keycode = 0;
        event->key.group = 0;
        event->key.is_modifier = 0;
        gtk_widget_event(widget, event);
        gdk_event_free(event);
    }

    /// Type a query into the entry, the way the search entry delivers it.
    static void type(GtkWidget* entry, const std::string& query) {
        gtk_entry_set_text(GTK_ENTRY(entry), query.c_str());
    }

private:
    void runTest(GtkApplication* app) final {
        this->glade = std::make_unique<GladeSearchpath>();
        this->glade->addSearchDirectory(GET_UI_FOLDER);
        this->glade->addSearchDirectory(GET_PAGE_TEMPLATE_FOLDER);

        this->control = std::make_unique<Control>(G_APPLICATION(app), this->glade.get(), true);
        this->win = std::make_unique<MainWindow>(this->glade.get(), this->control.get(), GTK_APPLICATION(app));
        this->control->initWindow(this->win.get());
        this->win->populate(this->glade.get());
        this->win->show(nullptr);
        settle();

        for (size_t i = 0; i < PALETTE_TEST_PAGES; i++) {
            this->control->insertPage(std::make_shared<XojPage>(595.28, 841.89), i, false);
        }
        settle();
        this->control->getPageSelection().reset(PALETTE_TEST_PAGES);

        this->GetParam().run(*this);

        /*
         * What a scenario leaves behind is asserted here rather than assumed. Every scenario puts
         * the palette away the way its user would - Escape, or picking a command - so that no case
         * can pass by leaving a popover up over the window.
         */
        CommandPalette* palette = this->palette();
        ASSERT_NE(palette, nullptr);
        EXPECT_FALSE(palette->isOpen()) << "a scenario leaves the palette closed behind it";

        /*
         * And then the window is destroyed with the palette up anyway, because a caller may destroy
         * the window while the palette is visible and the palette has to survive that on its own.
         * G_DEBUG=fatal-criticals turns a GTK warning along that path into a failed test, and a
         * crash takes the whole suite with it.
         */
        palette->open();
        settle();
        EXPECT_TRUE(palette->isOpen()) << "the window is destroyed while the palette is visible";
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

namespace {

/// Open the palette the way the shortcut does: through the action it is registered for.
void openThroughTheShortcut(CommandPaletteKeyboard& test) {
    GtkApplication* app = GTK_APPLICATION(gtk_window_get_application(GTK_WINDOW(test.win->getWindow())));
    ASSERT_TRUE(g_action_group_has_action(G_ACTION_GROUP(test.win->getWindow()), "command-palette"))
            << "Ctrl+K is registered for an action of the window";

    // The accelerator the application holds for the action is what a key press would run.
    gchar** accels = gtk_application_get_accels_for_action(app, "win.command-palette");
    ASSERT_NE(accels, nullptr);
    ASSERT_NE(accels[0], nullptr) << "the palette has an accelerator";
    EXPECT_EQ(std::string(accels[0]), "<Primary>k") << "Ctrl+K, as GTK spells the control key here";
    g_strfreev(accels);

    g_action_group_activate_action(G_ACTION_GROUP(test.win->getWindow()), "command-palette", nullptr);
    settle();
}

void ctrlKOpensAndTypingFilters(CommandPaletteKeyboard& test) {
    openThroughTheShortcut(test);

    CommandPalette* palette = test.palette();
    ASSERT_NE(palette, nullptr);
    EXPECT_TRUE(palette->isOpen());
    EXPECT_EQ(test.focused(), palette->getEntry()) << "the user types without reaching for the mouse";

    CommandPaletteKeyboard::type(palette->getEntry(), "undo");
    const std::vector<std::string> shown = palette->shownIds();
    ASSERT_FALSE(shown.empty());
    EXPECT_EQ(shown.front(), "win.undo") << "one row for Undo, and the title comes first";
    EXPECT_EQ(std::count(shown.begin(), shown.end(), std::string("win.undo")), 1)
            << "the menu entry and the tool button are one command, not two rows";
    EXPECT_EQ(palette->selectedId(), std::optional<std::string>("win.undo"));
    EXPECT_EQ(palette->acceleratorOf("win.undo"), "Ctrl+Z") << "the row shows the shortcut";
    EXPECT_EQ(palette->categoryOf("win.undo"), "Edit");

    // A query that matches nothing leaves nothing to run.
    CommandPaletteKeyboard::type(palette->getEntry(), "zzzqqq");
    EXPECT_TRUE(palette->shownIds().empty());
    EXPECT_FALSE(palette->selectedId().has_value());
    EXPECT_FALSE(palette->activateSelected()) << "there is nothing to run";

    // The keyboard walks the rows: Down moves on, Up moves back, and neither runs anything.
    CommandPaletteKeyboard::type(palette->getEntry(), "page");
    const std::vector<std::string> pages = palette->shownIds();
    ASSERT_GE(pages.size(), 2u) << "the query leaves more than one command to choose between";
    EXPECT_EQ(palette->selectedId(), std::optional<std::string>(pages.front()));

    CommandPaletteKeyboard::pressKey(palette->getEntry(), GDK_KEY_Down);
    EXPECT_EQ(palette->selectedId(), std::optional<std::string>(pages[1]));
    CommandPaletteKeyboard::pressKey(palette->getEntry(), GDK_KEY_Up);
    EXPECT_EQ(palette->selectedId(), std::optional<std::string>(pages.front()));

    // Up from the first row stays on it rather than wrapping around to the last.
    CommandPaletteKeyboard::pressKey(palette->getEntry(), GDK_KEY_Up);
    EXPECT_EQ(palette->selectedId(), std::optional<std::string>(pages.front()));
    EXPECT_TRUE(palette->isOpen()) << "navigating runs nothing";

    // Escape, as the user leaves it; that the focus then comes back is the next case.
    CommandPaletteKeyboard::pressKey(palette->getEntry(), GDK_KEY_Escape);
    settle();
    EXPECT_FALSE(palette->isOpen());
}

void escapeClosesAndFocusComesBack(CommandPaletteKeyboard& test) {
    /*
     * The canvas is where the user writes, so it is what has the keyboard when Ctrl+K is pressed -
     * and it is a widget that can hold it: XournalWidget sets GTK_CAN_FOCUS on it. Its focus is read
     * as the window's focus widget, because that is what a window with no window manager behind it
     * (which every headless test window is) can be asked about: gtk_widget_has_focus() is false for
     * every widget of such a window, even the one it would deliver key events to.
     */
    GtkWidget* canvas = test.win->getXournal()->getWidget();
    ASSERT_NE(canvas, nullptr);
    gtk_widget_grab_focus(canvas);
    settle();
    ASSERT_EQ(test.focused(), canvas) << "the test starts with the canvas focused";

    openThroughTheShortcut(test);
    CommandPalette* palette = test.palette();
    ASSERT_TRUE(palette->isOpen());
    EXPECT_EQ(test.focused(), palette->getEntry()) << "the palette takes the keyboard while it is open";

    CommandPaletteKeyboard::pressKey(palette->getEntry(), GDK_KEY_Escape);
    settle();

    EXPECT_FALSE(palette->isOpen()) << "Escape closes the palette";
    EXPECT_EQ(test.focused(), canvas) << "and the keyboard goes back exactly where the user was writing, not to "
                                      << describeWidget(test.focused());
    EXPECT_NE(test.focused(), palette->getEntry());
}

void aDisabledCommandIsShownAndCannotRun(CommandPaletteKeyboard& test) {
    // Every page selected: deleting them would leave the document without a page.
    test.control->getPageSelection().selectAll(PALETTE_TEST_PAGES);
    test.control->updatePageActions();
    const size_t pages = test.control->getDocument()->getPageCount();

    openThroughTheShortcut(test);
    CommandPalette* palette = test.palette();
    ASSERT_TRUE(palette->isOpen());

    CommandPaletteKeyboard::type(palette->getEntry(), "delete page");
    ASSERT_FALSE(palette->shownIds().empty()) << "a command that cannot be run is still offered";
    EXPECT_FALSE(palette->isSelectedEnabled());
    EXPECT_FALSE(palette->reasonOf(palette->selectedId().value_or("")).empty()) << "the row says why it cannot be run";

    EXPECT_FALSE(palette->activateSelected()) << "a disabled command cannot be activated";
    EXPECT_TRUE(palette->isOpen()) << "and the palette stays where the user can pick another one";
    EXPECT_EQ(test.control->getDocument()->getPageCount(), pages) << "nothing was deleted";

    CommandPaletteKeyboard::pressKey(palette->getEntry(), GDK_KEY_Escape);
    settle();
}

void runningACommandRemembersItAndChangesTheDocument(CommandPaletteKeyboard& test) {
    // The three inserted pages are what Undo takes back.
    ASSERT_TRUE(test.control->getUndoRedoHandler()->canUndo());

    openThroughTheShortcut(test);
    CommandPalette* palette = test.palette();
    ASSERT_TRUE(palette->isOpen());

    CommandPaletteKeyboard::type(palette->getEntry(), "undo");
    ASSERT_EQ(palette->selectedId(), std::optional<std::string>("win.undo"));
    EXPECT_TRUE(palette->activateSelected());
    settle();

    EXPECT_FALSE(palette->isOpen()) << "the palette closes once it has run something";
    EXPECT_EQ(test.control->getDocument()->getPageCount(), PALETTE_TEST_PAGES - 1)
            << "the command ran through the action the menu uses";

    // The palette remembers it for this profile, and offers it first the next time it is empty.
    Settings* settings = test.control->getSettings();
    ASSERT_FALSE(settings->getRecentCommands().empty());
    EXPECT_EQ(settings->getRecentCommands().front(), "win.undo");

    openThroughTheShortcut(test);
    const std::vector<std::string> shown = palette->shownIds();
    ASSERT_FALSE(shown.empty());
    EXPECT_EQ(shown.front(), "win.undo") << "an empty query offers what was just run";

    CommandPaletteKeyboard::pressKey(palette->getEntry(), GDK_KEY_Escape);
    settle();
}

void everyRowIsNamedForAccessibility(CommandPaletteKeyboard& test) {
    openThroughTheShortcut(test);
    CommandPalette* palette = test.palette();

    CommandPaletteKeyboard::type(palette->getEntry(), "undo");
    ASSERT_EQ(palette->selectedId(), std::optional<std::string>("win.undo"));

    AtkObject* entry = gtk_widget_get_accessible(palette->getEntry());
    ASSERT_NE(entry, nullptr);
    EXPECT_NE(atk_object_get_name(entry), nullptr);
    EXPECT_STREQ(atk_object_get_name(entry), "Search commands");

    AtkObject* list = gtk_widget_get_accessible(palette->getList());
    ASSERT_NE(list, nullptr);
    ASSERT_NE(atk_object_get_name(list), nullptr);
    EXPECT_STREQ(atk_object_get_name(list), "Commands");

    // The selected row is named after the command it runs, so a screen reader reads the command
    // rather than a row number. The name is compared with the title the registry gives that command
    // rather than with a label written down here: the menu's own label is live - Undo carries the
    // description of what it would take back - and the row has to follow it.
    const CommandRegistry commands = test.win->buildCommandRegistry();
    const CommandEntry* undo = commands.findById("win.undo");
    ASSERT_NE(undo, nullptr);
    AtkObject* row = gtk_widget_get_accessible(palette->getSelectedRow());
    ASSERT_NE(row, nullptr);
    ASSERT_NE(atk_object_get_name(row), nullptr);
    EXPECT_EQ(std::string(atk_object_get_name(row)), undo->metadata.title)
            << "the row is read out as the command it runs";
    EXPECT_NE(std::string(atk_object_get_name(row)), "") << "and it is read out as something";

    /*
     * The popover carries no transition: this is what keeps the window's teardown from having an
     * animation callback of the palette's to run - the callback that used to arrive at a window that
     * was already going away. It is asserted here because it is a promise the widget makes, not an
     * implementation detail of one code path.
     */
    GtkWidget* popover = palette->getPopover();
    ASSERT_NE(popover, nullptr);
    ASSERT_TRUE(GTK_IS_POPOVER(popover));
    EXPECT_FALSE(gtk_popover_get_transitions_enabled(GTK_POPOVER(popover)))
            << "the palette shows and hides without an animation to finish";

    CommandPaletteKeyboard::pressKey(palette->getEntry(), GDK_KEY_Escape);
    settle();
}

}  // namespace

INSTANTIATE_TEST_SUITE_P(
        PaletteScenarios, CommandPaletteKeyboard,
        ::testing::Values(PaletteScenario{"ctrlKOpensAndTypingFilters", &ctrlKOpensAndTypingFilters},
                          PaletteScenario{"escapeClosesAndFocusComesBack", &escapeClosesAndFocusComesBack},
                          PaletteScenario{"aDisabledCommandIsShownAndCannotRun", &aDisabledCommandIsShownAndCannotRun},
                          PaletteScenario{"runningACommandRemembersItAndChangesTheDocument",
                                          &runningACommandRemembersItAndChangesTheDocument},
                          PaletteScenario{"everyRowIsNamedForAccessibility", &everyRowIsNamedForAccessibility}));

TEST_P(CommandPaletteKeyboard, theScenario) {}

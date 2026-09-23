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

#include <algorithm>   // for find
#include <functional>  // for function
#include <memory>      // for unique_ptr
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>  // for GtkApplication, GtkApplicationWindow

#include "../dialog/GtkTest.h"
#include "control/Control.h"                   // for Control
#include "control/commands/CommandRegistry.h"  // for CommandRegistry
#include "control/settings/Settings.h"         // for Settings
#include "gui/GladeSearchpath.h"               // for GladeSearchpath
#include "gui/MainWindow.h"                    // for MainWindow
#include "gui/ShortcutReference.h"             // for ShortcutReference
#include "model/PageRef.h"                     // for PageRef
#include "model/XojPage.h"                     // for XojPage

#include "config-test.h"

/*
 * Plan 007, step 5: the shortcut reference.
 *
 * What these tests are about is where the reference gets what it shows: not from a list written down
 * for it, but from the registry the window builds - the menus, the toolbar items and the accelerators
 * the application actually holds. So the reference is opened through its action, and then the
 * application's own accelerators are changed underneath it: what it shows has to follow, because
 * there is nowhere else for it to have read them from.
 */

using xoj::gui::ShortcutReference;

namespace {

/// Lets GTK finish what it queued, so the window and the popover have settled.
void settle() {
    for (int i = 0; i < 4; i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(20000);
    }
}

/// Runs what the main context has ready until it has nothing left.
void drain() {
    for (int i = 0; i < 50 && g_main_context_pending(nullptr); i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(10000);
    }
}

auto focusedWidget(GtkWidget* window) -> GtkWidget* { return gtk_window_get_focus(GTK_WINDOW(window)); }

}  // namespace

class ShortcutReferenceFixture;

/// One test: what is done to the application before the reference is opened, and then what it shows.
struct ReferenceScenario {
    const char* name;
    /// Runs before the reference is opened: the application's accelerators, as they are at that point.
    std::function<void(GtkApplication*, MainWindow&)> prepare;
    std::function<void(ShortcutReferenceFixture&)> run;
};

void PrintTo(const ReferenceScenario& scenario, std::ostream* out) { *out << scenario.name; }

class ShortcutReferenceFixture: public GtkTest, public ::testing::WithParamInterface<ReferenceScenario> {
public:
    auto reference() const -> ShortcutReference* { return this->win->getShortcutReference(); }
    auto focused() const -> GtkWidget* { return focusedWidget(this->win->getWindow()); }

    /// The application whose accelerators the reference reads.
    auto application() const -> GtkApplication* {
        return GTK_APPLICATION(gtk_window_get_application(GTK_WINDOW(this->win->getWindow())));
    }

    /// Open the reference the way its menu entry does: through the action it is registered for.
    void openThroughTheAction() const {
        ASSERT_TRUE(g_action_group_has_action(G_ACTION_GROUP(this->win->getWindow()), "shortcut-reference"))
                << "the Help menu's entry is an action of the window";
        g_action_group_activate_action(G_ACTION_GROUP(this->win->getWindow()), "shortcut-reference", nullptr);
        settle();
    }

    /// Type a query into the search field, the way the entry delivers it.
    static void type(GtkWidget* entry, const std::string& query) {
        gtk_entry_set_text(GTK_ENTRY(entry), query.c_str());
    }

    /// Press a key where the keyboard is, the way the window delivers it.
    static void pressKeyWhereTheKeyboardIs(GtkWidget* window, guint keyval) {
        GtkWidget* toplevel = gtk_widget_get_toplevel(window);
        ASSERT_TRUE(GTK_IS_WINDOW(toplevel));
        GdkWindow* gdkWindow = gtk_widget_get_window(toplevel);
        ASSERT_NE(gdkWindow, nullptr) << "the window the key press arrives at";

        GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
        event->key.window = static_cast<GdkWindow*>(g_object_ref(gdkWindow));
        event->key.send_event = TRUE;
        event->key.time = GDK_CURRENT_TIME;
        event->key.state = static_cast<GdkModifierType>(0);
        event->key.keyval = keyval;
        event->key.hardware_keycode = 0;
        event->key.group = 0;
        event->key.is_modifier = 0;
        gtk_window_propagate_key_event(GTK_WINDOW(toplevel), &event->key);
        gdk_event_free(event);
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

        for (size_t i = 0; i < 2; i++) {
            this->control->insertPage(std::make_shared<XojPage>(595.28, 841.89), i, false);
        }
        this->win->showEditor();
        settle();

        this->GetParam().prepare(app, *this->win);
        settle();

        this->GetParam().run(*this);

        // Every scenario puts the reference away the way its user would.
        ShortcutReference* reference = this->reference();
        ASSERT_NE(reference, nullptr);
        EXPECT_FALSE(reference->isOpen()) << "a scenario leaves the reference closed behind it";

        /*
         * And then the window is destroyed while the reference is up anyway: a caller may destroy the
         * window with something still over it, and the reference has to survive that on its own.
         * G_DEBUG=fatal-criticals turns a GTK warning along that path into a failed test.
         */
        this->openThroughTheAction();
        EXPECT_TRUE(reference->isOpen()) << "the window is destroyed while the reference is visible";
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

/// The reference lists the commands the application has, with the keys they are on.
void listsWhatTheApplicationHas(ShortcutReferenceFixture& test) {
    ShortcutReference* reference = test.reference();
    test.openThroughTheAction();
    ASSERT_TRUE(reference->isOpen());

    // What it covers is what the registry holds: not a list of its own, and not a shorter one.
    const xoj::command::CommandRegistry commands = test.win->buildCommandRegistry();
    EXPECT_EQ(reference->coveredIds().size(), commands.metadata().size())
            << "the reference covers every command the application has, no more and no fewer";

    EXPECT_EQ(reference->titleOf("win.command-palette"), "Command Palette");
    EXPECT_EQ(reference->categoryOf("win.command-palette"), "Help");
    EXPECT_EQ(reference->acceleratorOf("win.command-palette"), "Ctrl+K")
            << "the keys are the ones the application holds for the action";
    EXPECT_FALSE(reference->isConflicted("win.command-palette"));

    // And what is shown before anything is typed is all of it.
    EXPECT_EQ(reference->shownCount(), reference->coveredIds().size());

    reference->close();
    settle();
}

/// Typing narrows the reference, and a query that matches nothing says so.
void typingNarrowsTheReference(ShortcutReferenceFixture& test) {
    ShortcutReference* reference = test.reference();
    test.openThroughTheAction();

    ShortcutReferenceFixture::type(reference->getEntry(), "export");
    settle();
    const std::vector<std::string> shown = reference->shownIds();
    EXPECT_NE(std::find(shown.begin(), shown.end(), "win.export-as-pdf"), shown.end())
            << "the export command is what the query is about";
    EXPECT_LT(reference->shownCount(), reference->coveredIds().size()) << "and the rest is out of the way";

    // A command that the application holds keys for shows them; one it does not is still listed.
    EXPECT_EQ(reference->acceleratorOf("win.undo"), "Ctrl+Z");

    ShortcutReferenceFixture::type(reference->getEntry(), "nothing-like-this-at-all");
    settle();
    EXPECT_EQ(reference->shownCount(), 0u) << "a query that matches nothing shows nothing";

    reference->close();
    settle();
}

/// The keys come from the application, so a changed accelerator is what the reference shows.
void changingAnAcceleratorChangesTheReference(ShortcutReferenceFixture& test) {
    ShortcutReference* reference = test.reference();
    test.openThroughTheAction();

    EXPECT_EQ(reference->acceleratorOf("win.command-palette"), "Ctrl+Shift+K")
            << "the reference shows what the action is on now, not what it was on when it was written";
    reference->close();
    settle();
}

/// Two commands on the same keys are marked, because that is what the user needs to know.
void conflictsAreMarked(ShortcutReferenceFixture& test) {
    ShortcutReference* reference = test.reference();
    test.openThroughTheAction();

    EXPECT_TRUE(reference->isConflicted("win.command-palette"));
    EXPECT_TRUE(reference->isConflicted("win.help"));
    EXPECT_EQ(reference->acceleratorOf("win.help"), "Ctrl+Shift+J") << "the keys are still shown";
    EXPECT_FALSE(reference->isConflicted("win.export-as-pdf")) << "a command on its own is not a conflict";

    reference->close();
    settle();
}

/// Escape puts the reference away from the keyboard, which is where its search field has the focus.
void theKeyboardPutsItAway(ShortcutReferenceFixture& test) {
    ShortcutReference* reference = test.reference();
    test.openThroughTheAction();
    ASSERT_TRUE(reference->isOpen());
    EXPECT_EQ(test.focused(), reference->getEntry()) << "the query field takes the keyboard while it is open";

    ShortcutReferenceFixture::pressKeyWhereTheKeyboardIs(test.win->getWindow(), GDK_KEY_Escape);
    settle();
    EXPECT_FALSE(reference->isOpen()) << "Escape closes the reference";
    EXPECT_NE(test.focused(), reference->getEntry()) << "and the keyboard does not stay in a closed reference";
}

/// Sets the keys of an action of the window, the way an accelerator change arrives.
void setAccelerator(GtkApplication* app, const char* action, const char** accelerators) {
    gtk_application_set_accels_for_action(app, action, accelerators);
}

const ReferenceScenario REFERENCE_SCENARIOS[] = {
        {"listsWhatTheApplicationHas", [](GtkApplication*, MainWindow&) {}, listsWhatTheApplicationHas},
        {"typingNarrowsTheReference", [](GtkApplication*, MainWindow&) {}, typingNarrowsTheReference},
        {"changingAnAcceleratorChangesTheReference",
         [](GtkApplication* app, MainWindow&) {
             const char* accels[] = {"<Control><Shift>k", nullptr};
             setAccelerator(app, "win.command-palette", accels);
         },
         changingAnAcceleratorChangesTheReference},
        {"conflictsAreMarked",
         [](GtkApplication* app, MainWindow&) {
             const char* shared[] = {"<Control><Shift>j", nullptr};
             setAccelerator(app, "win.command-palette", shared);
             setAccelerator(app, "win.help", shared);
         },
         conflictsAreMarked},
        {"theKeyboardPutsItAway", [](GtkApplication*, MainWindow&) {}, theKeyboardPutsItAway},
};

}  // namespace

INSTANTIATE_TEST_SUITE_P(ShortcutReferenceScenarios, ShortcutReferenceFixture,
                         ::testing::ValuesIn(REFERENCE_SCENARIOS));

/// The scenarios are the tests: each one runs the window, the application and what the user does.
TEST_P(ShortcutReferenceFixture, theScenario) {}

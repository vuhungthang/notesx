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

#include <functional>  // for function
#include <memory>      // for unique_ptr
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>  // for GtkApplication, GtkApplicationWindow, GtkPopover

#include "../dialog/GtkTest.h"
#include "control/Control.h"            // for Control
#include "control/settings/Settings.h"  // for Settings
#include "gui/GladeSearchpath.h"        // for GladeSearchpath
#include "gui/MainWindow.h"             // for MainWindow
#include "gui/WorkspaceGuidance.h"      // for WorkspaceGuidance
#include "gui/XournalView.h"            // for XournalView, the canvas
#include "model/PageRef.h"              // for PageRef
#include "model/XojPage.h"              // for XojPage

#include "config-test.h"

/*
 * Plan 007, step 3: what a fresh profile is told about the workspace, once.
 *
 * The explanation is shown when the window comes up - the editor surface is the one a fresh profile
 * starts on - and it must not be in the way of the writing: it is nonmodal and it leaves the
 * keyboard where it was, on the canvas. It is remembered as seen when the user puts it away, so the
 * next start has nothing to dismiss, and the setting that forgets everything ("show interface tips
 * again") brings it back.
 *
 * The keyboard is read as the window's focus widget: in a window no window manager ever focused,
 * gtk_widget_has_focus() is false for every widget, including the one the window delivers key
 * presses to.
 */

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

/// The widget the window would deliver a key press to.
auto focusedWidget(GtkWidget* window) -> GtkWidget* { return gtk_window_get_focus(GTK_WINDOW(window)); }

/// What a widget is, for an assertion message.
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

class WorkspaceGuidanceFixture;

/// One test: what the profile looks like before the window comes up, and what the user then does.
struct GuidanceScenario {
    const char* name;
    /// The profile the window is built on. Runs before the window is shown, because that is when the
    /// explanation decides whether this profile has already had it.
    std::function<void(Settings&)> prepare;
    std::function<void(WorkspaceGuidanceFixture&)> run;
};

/// Named by the case, not by the bytes of the struct a gtest failure would print.
void PrintTo(const GuidanceScenario& scenario, std::ostream* out) { *out << scenario.name; }

constexpr size_t GUIDANCE_TEST_PAGES = 2;

class WorkspaceGuidanceFixture: public GtkTest, public ::testing::WithParamInterface<GuidanceScenario> {
public:
    auto guidance() const -> xoj::gui::WorkspaceGuidance* { return this->win->getWorkspaceGuidance(); }
    auto settings() const -> Settings* { return this->control->getSettings(); }
    auto canvas() const -> GtkWidget* { return this->win->getXournal()->getWidget(); }
    /// The widget the window would give a key press to.
    auto focused() const -> GtkWidget* { return focusedWidget(this->win->getWindow()); }

    /**
     * Press a key where the keyboard is, the way the window delivers it: from the widget that has the
     * focus, up through the widgets above it, until one of them handles it. That path is the point -
     * a handler above the focused widget is reached because the window walks that way, not because
     * the event was aimed at it.
     *
     * The event holds its own reference to the window it is aimed at, because freeing the event
     * drops the reference it holds.
     */
    static void pressKeyWhereTheKeyboardIs(GtkWidget* window, guint keyval) {
        GtkWidget* toplevel = gtk_widget_get_toplevel(window);
        ASSERT_TRUE(GTK_IS_WINDOW(toplevel));
        ASSERT_EQ(focusedWidget(toplevel), gtk_window_get_focus(GTK_WINDOW(toplevel)));
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

        // The profile is what the window decides on, so it is set up before the window is shown.
        this->GetParam().prepare(*this->settings());

        this->win->show(nullptr);
        settle();

        for (size_t i = 0; i < GUIDANCE_TEST_PAGES; i++) {
            this->control->insertPage(std::make_shared<XojPage>(595.28, 841.89), i, false);
        }
        /*
         * The explanation is about the editor workspace, and a profile that has just started has
         * nothing open - which is the dashboard, not the editor. Entering the editor is what the
         * user does next, and what the explanation waits for.
         */
        this->win->showEditor();
        settle();

        this->GetParam().run(*this);

        /*
         * What a scenario leaves behind is asserted here: every scenario puts the explanation away
         * the way its user would, so that no case can pass by leaving a popover over the window.
         */
        xoj::gui::WorkspaceGuidance* guidance = this->guidance();
        ASSERT_NE(guidance, nullptr);
        EXPECT_FALSE(guidance->isShown()) << "a scenario leaves the explanation dismissed behind it";

        /*
         * And then the window is destroyed while the explanation is up anyway: a caller may close
         * the window with something still over it, and the explanation has to survive that on its
         * own. G_DEBUG=fatal-criticals turns a GTK warning along that path into a failed test.
         */
        this->settings()->setInterfaceGuidanceSeen(false);
        guidance->showIfFresh();
        settle();
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

/// A profile that has never been told anything.
void freshProfile(Settings& settings) {
    settings.resetInterfaceTips();
    settings.setInterfaceTipsEnabled(true);
    settings.setInterfaceGuidanceSeen(false);
}

/// The three things a fresh profile is told, and nothing else.
void showsTheWorkspaceExplanation(WorkspaceGuidanceFixture& test) {
    xoj::gui::WorkspaceGuidance* guidance = test.guidance();
    ASSERT_TRUE(guidance->isShown()) << "a fresh profile is told about the workspace it starts in";

    // Nothing is blocked: the explanation is not a question that has to be answered first.
    EXPECT_FALSE(gtk_popover_get_modal(GTK_POPOVER(guidance->getPopover())));

    // And it does not take the keyboard: what the user was doing keeps receiving what they type.
    EXPECT_EQ(test.focused(), test.canvas())
            << "the explanation leaves the keyboard where it was, not on " << describeWidget(test.focused());

    // It says what the workspace does, in words, and offers one way out of it that a keyboard
    // reaches.
    GtkWidget* dismiss = guidance->getDismissButton();
    ASSERT_NE(dismiss, nullptr);
    EXPECT_TRUE(gtk_widget_get_can_focus(dismiss)) << "a keyboard user can reach the button";
    const char* accessible = atk_object_get_name(gtk_widget_get_accessible(dismiss));
    ASSERT_NE(accessible, nullptr) << "the button says what it does to a screen reader";
    EXPECT_STRNE(accessible, "");

    guidance->dismiss();
    settle();
    EXPECT_FALSE(guidance->isShown());
    EXPECT_TRUE(test.settings()->hasSeenInterfaceGuidance()) << "putting it away is remembered";
}

/// Once it has been put away, a later start has nothing to show.
void isNotShownAgainOnceDismissed(WorkspaceGuidanceFixture& test) {
    ASSERT_TRUE(test.guidance()->isShown());

    test.guidance()->dismiss();
    settle();
    EXPECT_FALSE(test.guidance()->isShown());
    EXPECT_TRUE(test.settings()->hasSeenInterfaceGuidance());

    // The next window reads the same profile: this is what it would decide on.
    test.guidance()->showIfFresh();
    settle();
    EXPECT_FALSE(test.guidance()->isShown()) << "a profile that has seen it is not told again";
}

/// A profile that has already put it away sees nothing at all.
void aProfileThatHasSeenItShowsNothing(WorkspaceGuidanceFixture& test) {
    EXPECT_FALSE(test.guidance()->isShown()) << "the window is up and there is nothing over it";
}

/// "Show interface tips again" in the settings brings the explanation back.
void resettingBringsItBack(WorkspaceGuidanceFixture& test) {
    EXPECT_TRUE(test.guidance()->isShown()) << "forgetting what was shown makes it appear once more";
    EXPECT_FALSE(test.settings()->hasSeenInterfaceGuidance());

    test.guidance()->dismiss();
    settle();
}

/// The global switch turns the explanation off along with the tips.
void theOffSwitchMeansNothingIsShown(WorkspaceGuidanceFixture& test) {
    EXPECT_FALSE(test.guidance()->isShown()) << "tips are turned off, so nothing is offered";

    test.guidance()->showIfFresh();
    settle();
    EXPECT_FALSE(test.guidance()->isShown()) << "and asking for it does not override the switch";
}

/// The keyboard puts it away: Escape over the explanation, and the button a keyboard user reaches.
void theKeyboardPutsItAway(WorkspaceGuidanceFixture& test) {
    xoj::gui::WorkspaceGuidance* guidance = test.guidance();
    ASSERT_TRUE(guidance->isShown());

    // A keyboard user reaches the button the explanation offers, and Escape puts it away from there.
    GtkWidget* dismiss = guidance->getDismissButton();
    gtk_widget_grab_focus(dismiss);
    settle();
    ASSERT_EQ(test.focused(), dismiss) << "the button can be reached with the keyboard";

    WorkspaceGuidanceFixture::pressKeyWhereTheKeyboardIs(test.win->getWindow(), GDK_KEY_Escape);
    settle();
    EXPECT_FALSE(guidance->isShown()) << "Escape puts the explanation away";
    EXPECT_TRUE(test.settings()->hasSeenInterfaceGuidance()) << "and it is remembered as seen";

    // Shown once more, the button itself is what presses it away: a keyboard user reaches it (as
    // asserted above) and activating it is what Enter, Space or an assistive device does.
    test.settings()->setInterfaceGuidanceSeen(false);
    guidance->showIfFresh();
    settle();
    ASSERT_TRUE(guidance->isShown());
    ASSERT_EQ(gtk_widget_get_ancestor(dismiss, GTK_TYPE_POPOVER), guidance->getPopover());

    gtk_widget_grab_focus(dismiss);
    settle();
    gtk_button_clicked(GTK_BUTTON(dismiss));
    settle();
    EXPECT_FALSE(guidance->isShown()) << "pressing the button puts it away";
    EXPECT_TRUE(test.settings()->hasSeenInterfaceGuidance());
}

const GuidanceScenario GUIDANCE_SCENARIOS[] = {
        {"freshProfileIsToldAboutTheWorkspace", freshProfile, showsTheWorkspaceExplanation},
        {"itDoesNotComeBackOnceDismissed", freshProfile, isNotShownAgainOnceDismissed},
        {"aProfileThatHasSeenItShowsNothing",
         [](Settings& settings) {
             freshProfile(settings);
             settings.setInterfaceGuidanceSeen(true);
         },
         aProfileThatHasSeenItShowsNothing},
        {"resettingBringsItBack",
         [](Settings& settings) {
             freshProfile(settings);
             settings.setInterfaceGuidanceSeen(true);
             settings.resetInterfaceTips();
         },
         resettingBringsItBack},
        {"theOffSwitchMeansNothingIsShown",
         [](Settings& settings) {
             freshProfile(settings);
             settings.setInterfaceTipsEnabled(false);
         },
         theOffSwitchMeansNothingIsShown},
        {"theKeyboardPutsItAway", freshProfile, theKeyboardPutsItAway},
};

}  // namespace

INSTANTIATE_TEST_SUITE_P(WorkspaceGuidanceScenarios, WorkspaceGuidanceFixture, ::testing::ValuesIn(GUIDANCE_SCENARIOS));

/*
 * The scenarios are the tests: each one already runs the whole thing - the window, the profile, what
 * the user does - inside the fixture, because a GTK test has one application and one window per
 * case.
 */
TEST_P(WorkspaceGuidanceFixture, theScenario) {}

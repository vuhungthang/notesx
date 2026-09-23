/*
 * Xournal++
 *
 * Plan 008, step 2: the gesture preferences in the settings dialog
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
#include <gtk/gtk.h>  // for GtkApplication, GtkWindow, GtkSpinButton

#include "control/Control.h"                   // for Control
#include "control/gestures/GestureSettings.h"  // for GestureSettings (Plan 008)
#include "control/settings/Settings.h"         // for Settings
#include "gui/GladeSearchpath.h"               // for GladeSearchpath
#include "gui/MainWindow.h"                    // for MainWindow, which the dialog saves into
#include "gui/dialog/SettingsDialog.h"         // for SettingsDialog

#include "GtkTest.h"
#include "config-test.h"
#include "filesystem.h"  // for fs::path

/*
 * Plan 008, step 2: what the dialog shows, stores and forgets about gestures.
 *
 * The plan asks for the gesture preferences to be reachable in the settings - an area with a control
 * for each independent choice, a sensitivity beside the gesture it belongs to, an example of what the
 * gesture is, and a way back to the defaults - and for the destructive ones to be off unless the user
 * turns them on. So the dialog is built the way the application builds it and its own widgets are
 * used, which is what makes this a test of the dialog rather than of a copy of its logic.
 */

namespace {

using xoj::gesture::GestureSettings;

/// Runs what the main context has ready until it has nothing left, so one test does not end in the
/// next one's first iteration (see SettingsDialogTipsTest).
void drain() {
    for (int i = 0; i < 50 && g_main_context_pending(nullptr); i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(10000);
    }
}

/// Let GTK finish what it queued, so a widget rebuilt by a signal has settled.
void settle() {
    for (int i = 0; i < 4; i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(20000);
    }
}

/// The widget whose glade id this is, wherever it sits in the dialog.
///
/// A builder id is not the widget's GObject name - glade only sets that when the file also gives a
/// name - so the id is looked for where GtkBuilder keeps it, which is what the dialog's own
/// builder.get(id) reaches.
auto byName(GtkWidget* root, const char* id) -> GtkWidget* {
    if (root == nullptr) {
        return nullptr;
    }
    const char* buildable = gtk_buildable_get_name(GTK_BUILDABLE(root));
    if (buildable != nullptr && std::string(buildable) == id) {
        return root;
    }
    if (std::string(gtk_widget_get_name(root)) == id) {
        return root;
    }
    if (GTK_IS_CONTAINER(root)) {
        GtkWidget* found = nullptr;
        GList* children = gtk_container_get_children(GTK_CONTAINER(root));
        for (GList* child = children; child != nullptr && found == nullptr; child = child->next) {
            found = byName(GTK_WIDGET(child->data), id);
        }
        g_list_free(children);
        return found;
    }
    return nullptr;
}

auto byLabel(GtkWidget* root, const char* label) -> GtkWidget* {
    if (root == nullptr) {
        return nullptr;
    }
    if (GTK_IS_BUTTON(root) && gtk_button_get_label(GTK_BUTTON(root)) != nullptr &&
        std::string(gtk_button_get_label(GTK_BUTTON(root))) == label) {
        return root;
    }
    if (GTK_IS_CONTAINER(root)) {
        GtkWidget* found = nullptr;
        GList* children = gtk_container_get_children(GTK_CONTAINER(root));
        for (GList* child = children; child != nullptr && found == nullptr; child = child->next) {
            found = byLabel(GTK_WIDGET(child->data), label);
        }
        g_list_free(children);
        return found;
    }
    return nullptr;
}

/// The notebook page a widget sits on, without depending on the order the tabs are built in.
auto notebookPageOf(GtkWidget* widget) -> GtkWidget* {
    GtkWidget* page = widget;
    while (page != nullptr && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) {
        page = gtk_widget_get_parent(page);
    }
    return page;
}

auto isDescendantOf(GtkWidget* widget, GtkWidget* ancestor) -> bool {
    for (GtkWidget* node = widget; node != nullptr; node = gtk_widget_get_parent(node)) {
        if (node == ancestor) {
            return true;
        }
    }
    return false;
}

/// A settings dialog over a real profile and the real glade file.
struct SettingsDialogCase {
    GladeSearchpath glade;
    std::unique_ptr<Control> control;
    std::unique_ptr<MainWindow> win;
    std::unique_ptr<SettingsDialog> dialog;
    int callbacks = 0;

    explicit SettingsDialogCase(GtkApplication* app, const std::function<void(Settings*)>& prepare = {}) {
        this->glade.addSearchDirectory(GET_UI_FOLDER);
        this->glade.addSearchDirectory(GET_PAGE_TEMPLATE_FOLDER);
        this->control = std::make_unique<Control>(G_APPLICATION(app), &this->glade, true);
        this->win = std::make_unique<MainWindow>(&this->glade, this->control.get(), GTK_APPLICATION(app));
        this->control->initWindow(this->win.get());
        this->win->populate(&this->glade);
        if (prepare) {
            prepare(this->control->getSettings());
        }
        const std::vector<fs::path> paletteDirectories{fs::path(GET_TESTFILE(u8"palettes"))};
        this->dialog = std::make_unique<SettingsDialog>(&this->glade, this->control->getSettings(), this->control.get(),
                                                        paletteDirectories, [this]() { this->callbacks++; });
    }

    ~SettingsDialogCase() { drain(); }

    auto settings() const -> Settings* { return this->control->getSettings(); }
    auto window() const -> GtkWidget* { return GTK_WIDGET(this->dialog->getWindow()); }

    auto widget(const char* id) const -> GtkWidget* { return byName(this->window(), id); }
    auto checkbox(const char* id) const -> GtkCheckButton* {
        GtkWidget* found = byName(this->window(), id);
        return GTK_IS_CHECK_BUTTON(found) ? GTK_CHECK_BUTTON(found) : nullptr;
    }
    auto spin(const char* id) const -> GtkSpinButton* {
        GtkWidget* found = byName(this->window(), id);
        return GTK_IS_SPIN_BUTTON(found) ? GTK_SPIN_BUTTON(found) : nullptr;
    }
    auto ok() const -> GtkWidget* { return byLabel(this->window(), "Ok"); }
};

constexpr const char* CIRCLE_TOGGLE = "cbGestureCircleToSelect";
constexpr const char* CIRCLE_CONFIDENCE = "spGestureCircleConfidence";
constexpr const char* SCRIBBLE_TOGGLE = "cbGestureScribbleToErase";
constexpr const char* SCRIBBLE_CONFIDENCE = "spGestureScribbleConfidence";
constexpr const char* QUICK_PALETTE_TOGGLE = "cbGestureQuickPalette";
constexpr const char* FEEDBACK_TOGGLE = "cbGestureFeedback";
constexpr const char* RESET = "btGestureReset";

}  // namespace

/*
 * Each case says what the profile already holds before the dialog is built. A dialog that is saved
 * writes the profile, and the profile outlives the case, so a case that assumed a fresh profile
 * would be reading the one the case before it left behind.
 */

/// A control for each independent choice, on a tab of the settings, with an example of the gesture.
class SettingsDialogGestureSurfaceTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(
                app, [](Settings* settings) { settings->setGestureSettings(GestureSettings::defaults()); });
        ASSERT_NE(dialogCase.ok(), nullptr);

        GtkWidget* circle = dialogCase.widget(CIRCLE_TOGGLE);
        GtkWidget* scribble = dialogCase.widget(SCRIBBLE_TOGGLE);
        GtkWidget* quickPalette = dialogCase.widget(QUICK_PALETTE_TOGGLE);
        GtkWidget* feedback = dialogCase.widget(FEEDBACK_TOGGLE);
        ASSERT_NE(circle, nullptr) << "the settings have a switch for circle-to-select";
        ASSERT_NE(scribble, nullptr) << "the settings have a switch for scribble-to-erase";
        ASSERT_NE(quickPalette, nullptr) << "the settings have a switch for the quick palette";
        ASSERT_NE(feedback, nullptr) << "the settings have a switch for the first-gesture notice";

        ASSERT_NE(dialogCase.widget(CIRCLE_CONFIDENCE), nullptr) << "the circle has a sensitivity";
        ASSERT_NE(dialogCase.widget(SCRIBBLE_CONFIDENCE), nullptr) << "the scribble has a sensitivity";
        ASSERT_NE(dialogCase.widget(RESET), nullptr) << "the settings have a way back to the defaults";

        GtkWidget* page = notebookPageOf(circle);
        ASSERT_NE(page, nullptr) << "the gesture controls sit on a tab of the settings";
        EXPECT_TRUE(isDescendantOf(scribble, page)) << "the whole area is on that tab";

        // An example of what each gesture is, so the choice is not made blind.
        GtkWidget* circleDemo = dialogCase.widget("lbGestureCircleDemo");
        GtkWidget* scribbleDemo = dialogCase.widget("lbGestureScribbleDemo");
        ASSERT_NE(circleDemo, nullptr) << "circle-to-select has an example";
        ASSERT_NE(scribbleDemo, nullptr) << "scribble-to-erase has an example";
        EXPECT_TRUE(isDescendantOf(circleDemo, page));

        /*
         * Plan 008, step 4 hit its stop condition: the circle is recognised and then always left as
         * ink, so this page must not offer to turn it on - an enabled switch would invite the user
         * to enable a gesture that cannot act. It says why instead.
         */
        EXPECT_FALSE(gtk_widget_get_sensitive(circle)) << "the circle cannot be switched on in this build";
        const std::string circleLabel = gtk_button_get_label(GTK_BUTTON(circle));
        EXPECT_NE(circleLabel.find("not available yet"), std::string::npos) << "the control says so";
        const std::string circleDemoText = gtk_label_get_text(GTK_LABEL(circleDemo));
        EXPECT_NE(circleDemoText.find("Not available yet"), std::string::npos) << "and so does its example";
    }
};
TEST_F(SettingsDialogGestureSurfaceTest, everyGestureHasAControlOnATabOfTheSettings) {}

/// A fresh profile shows the conservative defaults: the destructive gestures off and their
/// sensitivities inert, because a sensitivity for a gesture that is off means nothing.
class SettingsDialogGestureDefaultLoadTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(
                app, [](Settings* settings) { settings->setGestureSettings(GestureSettings::defaults()); });
        const GestureSettings& stored = dialogCase.settings()->getGestureSettings();
        ASSERT_FALSE(stored.circleToSelectEnabled) << "the profile this dialog was built for";
        ASSERT_FALSE(stored.scribbleToEraseEnabled);

        ASSERT_NE(dialogCase.checkbox(CIRCLE_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.checkbox(SCRIBBLE_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.widget(CIRCLE_CONFIDENCE), nullptr);
        ASSERT_NE(dialogCase.widget(SCRIBBLE_CONFIDENCE), nullptr);

        EXPECT_FALSE(gtk_check_button_get_active(dialogCase.checkbox(CIRCLE_TOGGLE)));
        EXPECT_FALSE(gtk_check_button_get_active(dialogCase.checkbox(SCRIBBLE_TOGGLE)));
        EXPECT_FALSE(gtk_widget_get_sensitive(dialogCase.widget(CIRCLE_TOGGLE)))
                << "the circle cannot be switched on: this build recognises it and leaves it as ink";
        EXPECT_FALSE(gtk_widget_get_sensitive(dialogCase.widget(CIRCLE_CONFIDENCE)))
                << "its sensitivity is inert with it";
        EXPECT_FALSE(gtk_widget_get_sensitive(dialogCase.widget(SCRIBBLE_CONFIDENCE)))
                << "the scribble's sensitivity cannot be set while the scribble is off";
    }
};
TEST_F(SettingsDialogGestureDefaultLoadTest, aFreshProfileShowsTheConservativeDefaults) {}

/// And a profile that has a gesture on shows it on, with its stored sensitivity editable.
class SettingsDialogGestureStoredLoadTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(app, [](Settings* settings) {
            GestureSettings gesture = settings->getGestureSettings();
            gesture.scribbleToEraseEnabled = true;
            gesture.scribbleConfidenceFloor = 0.8;
            settings->setGestureSettings(gesture);
        });

        ASSERT_NE(dialogCase.checkbox(SCRIBBLE_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.spin(SCRIBBLE_CONFIDENCE), nullptr);
        ASSERT_NE(dialogCase.checkbox(CIRCLE_TOGGLE), nullptr);

        EXPECT_TRUE(gtk_check_button_get_active(dialogCase.checkbox(SCRIBBLE_TOGGLE))) << "the profile turns it on";
        EXPECT_TRUE(gtk_widget_get_sensitive(dialogCase.widget(SCRIBBLE_CONFIDENCE)))
                << "its sensitivity is editable once it is on";
        EXPECT_NEAR(gtk_spin_button_get_value(dialogCase.spin(SCRIBBLE_CONFIDENCE)), 0.8, 1e-9);
        EXPECT_FALSE(gtk_check_button_get_active(dialogCase.checkbox(CIRCLE_TOGGLE)))
                << "the gesture that cannot be carried out is still shown off";
    }
};
TEST_F(SettingsDialogGestureStoredLoadTest, aProfileWithAGestureOnShowsItOn) {}

/*
 * Plan 008, step 4 hit its stop condition: the circle is recognised and then left as ink, because a
 * selection cannot be one undo step. A profile that still holds the gesture on (an older one, or
 * one written by a build that offered it) must not make this page promise a selection: the control
 * is inert and says it is not available yet, and saving leaves the stored slot exactly as it was.
 */
class SettingsDialogGestureUnavailableCircleTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(app, [](Settings* settings) {
            GestureSettings gesture = settings->getGestureSettings();
            gesture.circleToSelectEnabled = true;  // what the profile holds
            settings->setGestureSettings(gesture);
        });
        ASSERT_NE(dialogCase.checkbox(CIRCLE_TOGGLE), nullptr);
        ASSERT_TRUE(dialogCase.settings()->getGestureSettings().circleToSelectEnabled)
                << "the profile this dialog was built for holds the circle on";

        EXPECT_FALSE(gtk_check_button_get_active(dialogCase.checkbox(CIRCLE_TOGGLE)))
                << "the page does not show a gesture it cannot carry out as on";
        EXPECT_FALSE(gtk_widget_get_sensitive(dialogCase.widget(CIRCLE_TOGGLE)));
        EXPECT_FALSE(gtk_widget_get_sensitive(dialogCase.widget(CIRCLE_CONFIDENCE)));

        GtkWidget* ok = dialogCase.ok();
        ASSERT_NE(ok, nullptr);
        gtk_button_clicked(GTK_BUTTON(ok));

        EXPECT_TRUE(dialogCase.settings()->getGestureSettings().circleToSelectEnabled)
                << "the stored slot this page cannot offer is left untouched by a save";
    }
};
TEST_F(SettingsDialogGestureUnavailableCircleTest, aStoredCircleIsShownAsNotAvailableAndLeftAlone) {}

/// Saving the dialog stores every control, and leaves the slots the dialog does not offer alone.
class SettingsDialogGestureSaveTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(app, [](Settings* settings) {
            GestureSettings gesture = GestureSettings::defaults();
            // A stored value this page cannot offer: it must survive a save untouched.
            gesture.circleToSelectEnabled = true;
            settings->setGestureSettings(gesture);
        });
        ASSERT_FALSE(dialogCase.settings()->getGestureSettings().tapUndoRedoEnabled)
                << "the touch tap slot is off and this page does not offer it";
        ASSERT_TRUE(dialogCase.settings()->getGestureSettings().circleToSelectEnabled)
                << "the stored circle slot is on and this page cannot offer it";
        ASSERT_NE(dialogCase.checkbox(SCRIBBLE_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.checkbox(QUICK_PALETTE_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.checkbox(FEEDBACK_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.spin(SCRIBBLE_CONFIDENCE), nullptr);

        gtk_check_button_set_active(dialogCase.checkbox(SCRIBBLE_TOGGLE), true);
        gtk_check_button_set_active(dialogCase.checkbox(QUICK_PALETTE_TOGGLE), true);
        gtk_check_button_set_active(dialogCase.checkbox(FEEDBACK_TOGGLE), false);
        gtk_spin_button_set_value(dialogCase.spin(SCRIBBLE_CONFIDENCE), 0.75);

        GtkWidget* ok = dialogCase.ok();
        ASSERT_NE(ok, nullptr);
        gtk_button_clicked(GTK_BUTTON(ok));

        const GestureSettings& stored = dialogCase.settings()->getGestureSettings();
        EXPECT_TRUE(stored.scribbleToEraseEnabled) << "saving the dialog stores the scribble switch";
        EXPECT_TRUE(stored.quickPaletteEnabled);
        EXPECT_FALSE(stored.feedbackEnabled);
        EXPECT_NEAR(stored.scribbleConfidenceFloor, 0.75, 1e-9);
        EXPECT_TRUE(stored.circleToSelectEnabled)
                << "the stored circle slot is left exactly as it was, not written from an inert control";
        EXPECT_FALSE(stored.tapUndoRedoEnabled) << "and the slot this page does not offer is untouched";
        EXPECT_EQ(dialogCase.callbacks, 1) << "and the caller is told the settings were accepted";
    }
};
TEST_F(SettingsDialogGestureSaveTest, savingTheDialogStoresTheGestureSwitches) {}

/// The reset button puts the controls back to the conservative defaults without storing them: the
/// profile keeps what it had until the dialog is saved, as every other control in the dialog does.
class SettingsDialogGestureResetTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(app, [](Settings* settings) {
            GestureSettings gesture = settings->getGestureSettings();
            gesture.circleToSelectEnabled = true;  // stored, and not this page's to change
            gesture.scribbleToEraseEnabled = true;
            gesture.scribbleConfidenceFloor = 0.9;
            settings->setGestureSettings(gesture);
        });
        ASSERT_NE(dialogCase.checkbox(CIRCLE_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.checkbox(SCRIBBLE_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.spin(SCRIBBLE_CONFIDENCE), nullptr);
        ASSERT_NE(dialogCase.widget(RESET), nullptr);
        ASSERT_TRUE(gtk_check_button_get_active(dialogCase.checkbox(SCRIBBLE_TOGGLE)))
                << "the profile turns the scribble on";
        ASSERT_FALSE(gtk_check_button_get_active(dialogCase.checkbox(CIRCLE_TOGGLE)))
                << "the circle cannot be carried out, so the page never shows it on";

        gtk_button_clicked(GTK_BUTTON(dialogCase.widget(RESET)));

        EXPECT_FALSE(gtk_check_button_get_active(dialogCase.checkbox(SCRIBBLE_TOGGLE)))
                << "the scribble goes back to off, the conservative default";
        EXPECT_NEAR(gtk_spin_button_get_value(dialogCase.spin(SCRIBBLE_CONFIDENCE)),
                    GestureSettings::defaults().scribbleConfidenceFloor, 1e-9);
        EXPECT_FALSE(gtk_widget_get_sensitive(dialogCase.widget(SCRIBBLE_CONFIDENCE)))
                << "and its sensitivity is inert again";
        EXPECT_FALSE(gtk_widget_get_sensitive(dialogCase.widget(CIRCLE_TOGGLE)));
        EXPECT_TRUE(dialogCase.settings()->getGestureSettings().circleToSelectEnabled)
                << "the stored circle slot is not this page's to change, and nothing is stored yet anyway";
        EXPECT_TRUE(dialogCase.settings()->getGestureSettings().scribbleToEraseEnabled)
                << "nothing is stored until the dialog is saved";

        GtkWidget* ok = dialogCase.ok();
        ASSERT_NE(ok, nullptr);
        gtk_button_clicked(GTK_BUTTON(ok));

        const GestureSettings& stored = dialogCase.settings()->getGestureSettings();
        EXPECT_FALSE(stored.scribbleToEraseEnabled) << "saving after a reset stores the defaults";
        EXPECT_NEAR(stored.scribbleConfidenceFloor, GestureSettings::defaults().scribbleConfidenceFloor, 1e-9);
        EXPECT_TRUE(stored.circleToSelectEnabled)
                << "and the stored slot this page cannot offer is left exactly as it was";
    }
};
TEST_F(SettingsDialogGestureResetTest, theResetButtonGoesBackToTheDefaultsWithoutStoringThem) {}

/*
 * Plan 008, step 7: the gesture reference, on the page the gestures are set on, read from the live
 * settings rather than written down - so it says what the application would do, and it moves when a
 * control moves.
 */
class SettingsDialogGestureReferenceTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(
                app, [](Settings* settings) { settings->setGestureSettings(GestureSettings::defaults()); });

        ASSERT_NE(dialogCase.widget("gestureReferenceList"), nullptr) << "the page has a reference";
        ASSERT_NE(dialogCase.widget("gestureReferenceRow-circle"), nullptr) << "the circle has a line";
        ASSERT_NE(dialogCase.widget("gestureReferenceRow-scribble"), nullptr) << "the scribble has a line";

        auto stateOf = [&dialogCase](const char* gesture) {
            GtkWidget* label = dialogCase.widget(gesture);
            return label == nullptr ? std::string{} : std::string(gtk_label_get_text(GTK_LABEL(label)));
        };

        /*
         * Plan 008, step 4 hit its stop condition: the circle is recognised and then always left as
         * ink, so the reference must not report a stored "On" for a selection that never happens. It
         * says the gesture is not available yet.
         */
        EXPECT_EQ(stateOf("gestureReferenceState-circle"), "Not available yet")
                << "the circle cannot be carried out, so the reference does not promise it";
        EXPECT_EQ(stateOf("gestureReferenceState-scribble"), "Off") << "a gesture nobody can reach says so";

        // Live: turning a gesture on on the page turns its line on too, without a save.
        gtk_check_button_set_active(dialogCase.checkbox(SCRIBBLE_TOGGLE), true);
        settle();

        EXPECT_EQ(stateOf("gestureReferenceState-scribble"), "On") << "the reference follows the controls";
        EXPECT_EQ(stateOf("gestureReferenceState-circle"), "Not available yet")
                << "and the circle says what it is however the page changes";
    }
};
TEST_F(SettingsDialogGestureReferenceTest, theReferenceSaysWhatTheLiveSettingsSay) {}

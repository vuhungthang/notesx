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
        EXPECT_FALSE(gtk_widget_get_sensitive(dialogCase.widget(CIRCLE_CONFIDENCE)))
                << "the circle's sensitivity cannot be set while the circle is off";
        EXPECT_FALSE(gtk_widget_get_sensitive(dialogCase.widget(SCRIBBLE_CONFIDENCE)));
    }
};
TEST_F(SettingsDialogGestureDefaultLoadTest, aFreshProfileShowsTheConservativeDefaults) {}

/// And a profile that has a gesture on shows it on, with its stored sensitivity editable.
class SettingsDialogGestureStoredLoadTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(app, [](Settings* settings) {
            GestureSettings gesture = settings->getGestureSettings();
            gesture.circleToSelectEnabled = true;
            gesture.circleConfidenceFloor = 0.8;
            settings->setGestureSettings(gesture);
        });

        ASSERT_NE(dialogCase.checkbox(CIRCLE_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.spin(CIRCLE_CONFIDENCE), nullptr);
        ASSERT_NE(dialogCase.checkbox(SCRIBBLE_TOGGLE), nullptr);

        EXPECT_TRUE(gtk_check_button_get_active(dialogCase.checkbox(CIRCLE_TOGGLE))) << "the profile turns it on";
        EXPECT_TRUE(gtk_widget_get_sensitive(dialogCase.widget(CIRCLE_CONFIDENCE)))
                << "its sensitivity is editable once it is on";
        EXPECT_NEAR(gtk_spin_button_get_value(dialogCase.spin(CIRCLE_CONFIDENCE)), 0.8, 1e-9);
        EXPECT_FALSE(gtk_check_button_get_active(dialogCase.checkbox(SCRIBBLE_TOGGLE)))
                << "the gesture that is off is still shown off";
    }
};
TEST_F(SettingsDialogGestureStoredLoadTest, aProfileWithAGestureOnShowsItOn) {}

/// Saving the dialog stores every control, and leaves the slot the dialog does not offer alone.
class SettingsDialogGestureSaveTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(
                app, [](Settings* settings) { settings->setGestureSettings(GestureSettings::defaults()); });
        ASSERT_FALSE(dialogCase.settings()->getGestureSettings().tapUndoRedoEnabled)
                << "the touch tap slot is off and this page does not offer it";
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
        EXPECT_FALSE(stored.circleToSelectEnabled) << "a control that was left alone is stored as it was shown";
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
            gesture.circleToSelectEnabled = true;
            gesture.scribbleToEraseEnabled = true;
            gesture.circleConfidenceFloor = 0.9;
            settings->setGestureSettings(gesture);
        });
        ASSERT_NE(dialogCase.checkbox(CIRCLE_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.checkbox(SCRIBBLE_TOGGLE), nullptr);
        ASSERT_NE(dialogCase.spin(CIRCLE_CONFIDENCE), nullptr);
        ASSERT_NE(dialogCase.widget(RESET), nullptr);
        ASSERT_TRUE(gtk_check_button_get_active(dialogCase.checkbox(CIRCLE_TOGGLE)));

        gtk_button_clicked(GTK_BUTTON(dialogCase.widget(RESET)));

        EXPECT_FALSE(gtk_check_button_get_active(dialogCase.checkbox(CIRCLE_TOGGLE)))
                << "the circle goes back to off, the conservative default";
        EXPECT_FALSE(gtk_check_button_get_active(dialogCase.checkbox(SCRIBBLE_TOGGLE))) << "so does the scribble";
        EXPECT_NEAR(gtk_spin_button_get_value(dialogCase.spin(CIRCLE_CONFIDENCE)),
                    GestureSettings::defaults().circleConfidenceFloor, 1e-9);
        EXPECT_FALSE(gtk_widget_get_sensitive(dialogCase.widget(CIRCLE_CONFIDENCE)))
                << "and its sensitivity is inert again";
        EXPECT_TRUE(dialogCase.settings()->getGestureSettings().circleToSelectEnabled)
                << "nothing is stored until the dialog is saved";

        GtkWidget* ok = dialogCase.ok();
        ASSERT_NE(ok, nullptr);
        gtk_button_clicked(GTK_BUTTON(ok));

        const GestureSettings& stored = dialogCase.settings()->getGestureSettings();
        EXPECT_FALSE(stored.circleToSelectEnabled) << "saving after a reset stores the defaults";
        EXPECT_FALSE(stored.scribbleToEraseEnabled);
        EXPECT_NEAR(stored.circleConfidenceFloor, GestureSettings::defaults().circleConfidenceFloor, 1e-9);
    }
};
TEST_F(SettingsDialogGestureResetTest, theResetButtonGoesBackToTheDefaultsWithoutStoringThem) {}

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
#include <gtk/gtk.h>  // for GtkApplication, GtkWindow

#include "control/Control.h"            // for Control
#include "control/settings/Settings.h"  // for Settings
#include "gui/GladeSearchpath.h"        // for GladeSearchpath
#include "gui/MainWindow.h"             // for MainWindow, which the dialog saves into
#include "gui/TipService.h"             // for TipService, the ids the tips are remembered by
#include "gui/dialog/SettingsDialog.h"  // for SettingsDialog

#include "GtkTest.h"
#include "config-test.h"
#include "filesystem.h"  // for fs::path

/*
 * Plan 007, step 4: the settings the tips are switched off with, and switched back on with.
 *
 * The plan asks for this in the Settings dialog - not only behind a menu entry - so the dialog is
 * built the way the application builds it and the two controls are used: the switch is what the
 * profile says and what saving writes, and the button beside it forgets what has been shown, so the
 * workspace explanation and every tip appear once more.
 */

using xoj::gui::TipService;

namespace {

constexpr const char* TIPS_SWITCH_LABEL = "Show interface tips";
constexpr const char* TIPS_REPLAY_LABEL = "Show interface tips again";

/// The button label of a widget, or an empty string for a widget that is not a button.
auto buttonLabel(GtkWidget* widget) -> std::string {
    if (!GTK_IS_BUTTON(widget)) {
        return {};
    }
    const char* label = gtk_button_get_label(GTK_BUTTON(widget));
    return label == nullptr ? std::string{} : std::string(label);
}

/// The first widget of the tree the predicate accepts, including the root itself.
auto findWidget(GtkWidget* root, const std::function<bool(GtkWidget*)>& accept) -> GtkWidget* {
    if (root == nullptr) {
        return nullptr;
    }
    if (accept(root)) {
        return root;
    }
    if (GTK_IS_CONTAINER(root)) {
        GtkWidget* found = nullptr;
        GList* children = gtk_container_get_children(GTK_CONTAINER(root));
        for (GList* child = children; child != nullptr && found == nullptr; child = child->next) {
            found = findWidget(GTK_WIDGET(child->data), accept);
        }
        g_list_free(children);
        return found;
    }
    return nullptr;
}

/// The button with this label, wherever it sits in the dialog.
auto buttonWithLabel(GtkWidget* root, const char* label) -> GtkWidget* {
    return findWidget(root, [label](GtkWidget* widget) { return buttonLabel(widget) == label; });
}

/// The notebook page a widget sits on: "in the same tab as the workspace options" without depending
/// on the order the tabs are built in.
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

/**
 * A settings dialog over a real profile and the real glade file, built the way the application builds
 * it: the dialog needs the controller - for the palettes, the devices and the button bindings - and
 * nothing else.
 */
struct SettingsDialogCase {
    GladeSearchpath glade;
    std::unique_ptr<Control> control;
    std::unique_ptr<MainWindow> win;
    std::unique_ptr<SettingsDialog> dialog;
    int callbacks = 0;

    /// `prepare` is what the profile already holds when the dialog is built - the state the load
    /// path has to show.
    explicit SettingsDialogCase(GtkApplication* app, const std::function<void(Settings*)>& prepare = {}) {
        this->glade.addSearchDirectory(GET_UI_FOLDER);
        this->glade.addSearchDirectory(GET_PAGE_TEMPLATE_FOLDER);
        this->control = std::make_unique<Control>(G_APPLICATION(app), &this->glade, true);
        /*
         * Saving the dialog goes through the window as well: it has the touchscreen mapping re-read
         * and the view told that the settings changed. So the dialog is built over a window the way
         * the application builds it, and not over a controller without one.
         */
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

    auto settings() const -> Settings* { return this->control->getSettings(); }
    auto window() const -> GtkWidget* { return GTK_WIDGET(this->dialog->getWindow()); }

    auto tipsSwitch() const -> GtkWidget* { return buttonWithLabel(this->window(), TIPS_SWITCH_LABEL); }
    auto tipsReplay() const -> GtkWidget* { return buttonWithLabel(this->window(), TIPS_REPLAY_LABEL); }
};

}  // namespace

/// The switch is in the dialog, beside the workspace options, and shows what the profile says.
class SettingsDialogTipsSurfaceTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(app);
        ASSERT_NE(dialogCase.settings(), nullptr);

        GtkWidget* tipsSwitch = dialogCase.tipsSwitch();
        GtkWidget* replay = dialogCase.tipsReplay();
        ASSERT_NE(tipsSwitch, nullptr) << "the settings have a switch for the interface tips";
        ASSERT_NE(replay, nullptr) << "the settings have a way to show the tips again";
        EXPECT_TRUE(GTK_IS_CHECK_BUTTON(tipsSwitch)) << "the switch is a checkbox";

        // The plan asks for this in the settings dialog and not only behind a menu entry, so the
        // switch is checked to be on a tab of the dialog, next to the button that goes with it.
        GtkWidget* page = notebookPageOf(tipsSwitch);
        ASSERT_NE(page, nullptr) << "the switch sits on a tab of the settings";
        EXPECT_TRUE(isDescendantOf(replay, page)) << "the replay button is on the same tab";
    }
};
TEST_F(SettingsDialogTipsSurfaceTest, theTipsHaveASwitchInTheSettingsNextToTheWorkspaceOptions) {}

/*
 * A second window and controller in one test does not come back - the application is built once per
 * test here, so every case is one test.
 */

/// The dialog shows the stored switch off when the profile has the tips off.
class SettingsDialogTipsLoadOffTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase switchedOff(app, [](Settings* settings) { settings->setInterfaceTipsEnabled(false); });
        ASSERT_NE(switchedOff.tipsSwitch(), nullptr);
        ASSERT_FALSE(switchedOff.settings()->isInterfaceTipsEnabled()) << "the profile this dialog was built for";
        EXPECT_FALSE(gtk_check_button_get_active(GTK_CHECK_BUTTON(switchedOff.tipsSwitch())))
                << "the dialog shows the tips switched off";
    }
};
TEST_F(SettingsDialogTipsLoadOffTest, theDialogShowsTheTipsSwitchedOff) {}

/// And on when it has them on.
class SettingsDialogTipsLoadOnTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase switchedOn(app, [](Settings* settings) { settings->setInterfaceTipsEnabled(true); });
        ASSERT_NE(switchedOn.tipsSwitch(), nullptr);
        ASSERT_TRUE(switchedOn.settings()->isInterfaceTipsEnabled()) << "the profile this dialog was built for";
        EXPECT_TRUE(gtk_check_button_get_active(GTK_CHECK_BUTTON(switchedOn.tipsSwitch())))
                << "the dialog shows the tips switched on";
    }
};
TEST_F(SettingsDialogTipsLoadOnTest, theDialogShowsTheTipsSwitchedOn) {}

/// Saving the dialog writes the switch, the way every other checkbox in it is written.
class SettingsDialogTipsSaveTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(app);
        dialogCase.settings()->setInterfaceTipsEnabled(true);

        GtkWidget* tipsSwitch = dialogCase.tipsSwitch();
        GtkWidget* ok = buttonWithLabel(dialogCase.window(), "Ok");
        ASSERT_NE(tipsSwitch, nullptr);
        ASSERT_NE(ok, nullptr) << "the dialog has an OK button";

        gtk_check_button_set_active(GTK_CHECK_BUTTON(tipsSwitch), false);
        ASSERT_FALSE(gtk_check_button_get_active(GTK_CHECK_BUTTON(tipsSwitch)));
        gtk_button_clicked(GTK_BUTTON(ok));

        EXPECT_FALSE(dialogCase.settings()->isInterfaceTipsEnabled()) << "saving the dialog switches the tips off";
        EXPECT_EQ(dialogCase.callbacks, 1) << "and the caller is told the settings were accepted";
    }
};
TEST_F(SettingsDialogTipsSaveTest, savingTheDialogStoresTheTipsSwitch) {}

/// The replay button forgets what has been shown and switches the tips back on, at once.
class SettingsDialogTipsReplayTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        SettingsDialogCase dialogCase(app, [](Settings* settings) {
            settings->setInterfaceGuidanceSeen(true);
            settings->markTipSeen(TipService::idOf(TipService::Tip::ToolProperties));
            settings->setInterfaceTipsEnabled(false);
        });
        GtkWidget* replay = dialogCase.tipsReplay();
        GtkWidget* tipsSwitch = dialogCase.tipsSwitch();
        ASSERT_NE(replay, nullptr);
        ASSERT_NE(tipsSwitch, nullptr);
        ASSERT_TRUE(dialogCase.settings()->hasSeenInterfaceGuidance());
        ASSERT_TRUE(dialogCase.settings()->hasSeenTip(TipService::idOf(TipService::Tip::ToolProperties)));
        ASSERT_FALSE(gtk_check_button_get_active(GTK_CHECK_BUTTON(tipsSwitch)));

        gtk_button_clicked(GTK_BUTTON(replay));

        EXPECT_FALSE(dialogCase.settings()->hasSeenInterfaceGuidance())
                << "the workspace explanation is one of the things the button covers";
        EXPECT_FALSE(dialogCase.settings()->hasSeenTip(TipService::idOf(TipService::Tip::ToolProperties)))
                << "and every tip is shown once more";
        EXPECT_TRUE(dialogCase.settings()->isInterfaceTipsEnabled()) << "the button switches the tips back on";
        EXPECT_TRUE(gtk_check_button_get_active(GTK_CHECK_BUTTON(dialogCase.tipsSwitch())))
                << "and the checkbox beside it says so";
    }
};
TEST_F(SettingsDialogTipsReplayTest, showingTheTipsAgainForgetsWhatHasBeenShownAndSwitchesThemBackOn) {}

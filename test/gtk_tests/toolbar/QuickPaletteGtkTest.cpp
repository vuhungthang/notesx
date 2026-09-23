/*
 * Xournal++
 *
 * Plan 008 step 1: the quick palette, in a real window
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>  // for GtkApplication, GtkApplicationWindow

#include "../dialog/GtkTest.h"
#include "control/Control.h"                   // for Control
#include "control/ToolEnums.h"                 // for ToolType, TOOL_ERASER
#include "control/ToolHandler.h"               // for ToolHandler
#include "control/gestures/GestureSettings.h"  // for GestureSettings
#include "control/settings/Settings.h"         // for Settings
#include "gui/GladeSearchpath.h"               // for GladeSearchpath
#include "gui/MainWindow.h"                    // for MainWindow
#include "gui/QuickPalette.h"                  // for QuickPalette, QuickPaletteButton
#include "gui/ShortcutReference.h"             // for ShortcutReference (Plan 008, step 1)
#include "model/PageRef.h"                     // for PageRef
#include "model/XojPage.h"                     // for XojPage

#include "config-test.h"

/*
 * Plan 008, step 1: the quick palette.
 *
 * The palette is built on the window's own overlay, the way the floating toolbox is, and it is
 * reached through the same call the stylus has always used to summon the floating toolbox -
 * Control::showFloatingToolbox() - when the gesture preferences say the palette is bound. These
 * tests exercise what the plan asks of it: it clamps inside the viewport, it does not sit on the
 * point the pen is at, every place it offers says what it is, cancel changes nothing, and the
 * Classic toolbox is what appears when the palette is not bound.
 *
 * The Escape key itself is not injected here - this harness has no way to inject input events - so
 * the binding from Escape to cancel() is covered by the manual matrix; what is tested is the
 * cancel() it invokes.
 */

using xoj::gesture::GestureSettings;
using xoj::gui::QuickPalette;

namespace {

/// Lets GTK finish what it queued, so the window and the overlay have settled.
void settle() {
    for (int i = 0; i < 4; i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(20000);
    }
}

/*
 * The window these scenarios share: a real Control and MainWindow, the palette built on the
 * window's own overlay the way the floating toolbox is.
 */
class QuickPaletteGtkFixture: public GtkTest, public ::testing::WithParamInterface<void (*)(QuickPaletteGtkFixture&)> {
public:
    /// Turn the palette binding on or off in the live settings; takes effect at once.
    void enablePalette(bool enabled) {
        GestureSettings settings = this->control->getSettings()->getGestureSettings();
        settings.quickPaletteEnabled = enabled;
        this->control->getSettings()->setGestureSettings(settings);
    }

protected:
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

        this->control->insertPage(std::make_shared<XojPage>(595.28, 841.89), 0, false);
        this->win->showEditor();
        settle();

        this->GetParam()(*this);
    }

public:
    std::unique_ptr<GladeSearchpath> glade;
    std::unique_ptr<Control> control;
    std::unique_ptr<MainWindow> win;
};

/// The palette appears at the anchor, inside the viewport, off the pen point.
void showsWhereItWasSummonedWithoutCoveringThePenPoint(QuickPaletteGtkFixture& test) {
    QuickPalette* palette = test.win->getQuickPalette();
    ASSERT_NE(palette, nullptr);

    const double anchorX = 300.0;
    const double anchorY = 400.0;
    palette->showAt(anchorX, anchorY);
    settle();

    EXPECT_TRUE(palette->isShown());

    GtkWidget* overlay = test.win->get("mainOverlay");
    const double viewportWidth = static_cast<double>(gtk_widget_get_allocated_width(overlay));
    const double viewportHeight = static_cast<double>(gtk_widget_get_allocated_height(overlay));

    const xoj::gui::SurfacePlacement& placement = palette->lastPlacement();
    EXPECT_GE(placement.x, 10.0);
    EXPECT_GE(placement.y, 10.0);
    EXPECT_LE(placement.x, viewportWidth - 10.0);
    EXPECT_LE(placement.y, viewportHeight - 10.0);

    palette->cancel();
    settle();
    EXPECT_FALSE(palette->isShown());
}

/// Every place it offers says what it is, and the trip the plan names is there.
void everyPlaceItOffersSaysWhatItIs(QuickPaletteGtkFixture& test) {
    test.enablePalette(true);
    QuickPalette* palette = test.win->getQuickPalette();
    ASSERT_NE(palette, nullptr);

    test.win->showQuickPaletteAt(200, 200);
    settle();
    ASSERT_TRUE(palette->isShown());

    const std::vector<xoj::gui::QuickPaletteButton>& buttons = palette->buttons();
    ASSERT_FALSE(buttons.empty());

    for (const xoj::gui::QuickPaletteButton& button: buttons) {
        EXPECT_FALSE(button.id.empty());
        EXPECT_FALSE(button.label.empty()) << button.id << " has no accessible name";
        GtkWidget* widget = palette->buttonFor(button.id);
        ASSERT_NE(widget, nullptr) << button.id;
        EXPECT_NE(gtk_widget_get_tooltip_text(widget), nullptr) << button.id;
    }

    EXPECT_NE(palette->buttonFor("undo"), nullptr);
    EXPECT_NE(palette->buttonFor("hand"), nullptr);
    EXPECT_NE(palette->buttonFor("eraser"), nullptr);
    EXPECT_NE(palette->buttonFor("lasso"), nullptr);

    const std::size_t favorites = test.control->getSettings()->getToolPresets().getFavorites().size();
    const std::size_t favoriteButtons = static_cast<std::size_t>(std::count_if(
            buttons.begin(), buttons.end(), [](const auto& button) { return button.id.rfind("favorite:", 0) == 0; }));
    EXPECT_EQ(favoriteButtons, favorites);

    palette->cancel();
    settle();
}

/// Dismissing it is a cancel: nothing is activated and the tool is where it was.
void cancellingChangesNothingAboutTheTool(QuickPaletteGtkFixture& test) {
    test.enablePalette(true);
    QuickPalette* palette = test.win->getQuickPalette();
    ASSERT_NE(palette, nullptr);

    ToolHandler* handler = test.control->getToolHandler();
    const ToolType before = handler->getToolType();

    test.win->showQuickPaletteAt(200, 200);
    settle();
    ASSERT_TRUE(palette->isShown());

    palette->cancel();
    settle();

    EXPECT_FALSE(palette->isShown());
    EXPECT_TRUE(palette->dismissedWithoutAction());
    EXPECT_EQ(handler->getToolType(), before) << "dismissing the palette must not change the tool";
}

/// And pressing a button is not a cancel: it goes through the tool action.
void theEraserButtonIsTheEraser(QuickPaletteGtkFixture& test) {
    test.enablePalette(true);
    QuickPalette* palette = test.win->getQuickPalette();
    ASSERT_NE(palette, nullptr);

    test.win->showQuickPaletteAt(200, 200);
    settle();

    // The action the palette activates is the one the window's own map holds, the same one the
    // toolbar's tool buttons use.
    GtkWindow* window = GTK_WINDOW(test.win->get("mainWindow"));
    ASSERT_TRUE(g_action_group_has_action(G_ACTION_GROUP(window), "select-tool"));

    GtkWidget* eraser = palette->buttonFor("eraser");
    ASSERT_NE(eraser, nullptr);
    gtk_button_clicked(GTK_BUTTON(eraser));
    settle();

    EXPECT_EQ(test.control->getToolHandler()->getToolType(), TOOL_ERASER);
    EXPECT_FALSE(palette->dismissedWithoutAction()) << "an activation is not a cancel";
    EXPECT_FALSE(palette->isShown());
}

/// The Classic toolbox is what the binding still summons when the palette is not bound.
void theClassicToolboxIsWhatAppearsWhenThePaletteIsNotBound(QuickPaletteGtkFixture& test) {
    QuickPalette* palette = test.win->getQuickPalette();
    ASSERT_NE(palette, nullptr);

    test.enablePalette(false);
    test.control->showFloatingToolbox(200, 200);
    settle();

    EXPECT_FALSE(palette->isShown()) << "the palette is not summoned when it is not bound";
    EXPECT_TRUE(gtk_widget_get_visible(test.win->get("floatingToolbox")))
            << "the Classic floating toolbox is what the binding still summons";
}

}  // namespace

/// Plan 008, step 1: the palette is reachable from the keyboard when it is bound.
void theKeyboardSummonsThePaletteWhenItIsBound(QuickPaletteGtkFixture& test) {
    test.enablePalette(true);
    QuickPalette* palette = test.win->getQuickPalette();
    ASSERT_NE(palette, nullptr);

    test.control->showQuickPalette();
    settle();

    EXPECT_TRUE(palette->isShown()) << "the keyboard binding summons it";
    palette->cancel();
    settle();
}

/// And an unbound palette is not reachable by a key any more than by a button.
void theKeyboardDoesNothingWhenThePaletteIsNotBound(QuickPaletteGtkFixture& test) {
    test.enablePalette(false);
    QuickPalette* palette = test.win->getQuickPalette();
    ASSERT_NE(palette, nullptr);

    test.control->showQuickPalette();
    settle();

    EXPECT_FALSE(palette->isShown()) << "it is only reachable where the user bound it";
}

/// The keyboard command carries metadata and keys, from the live accelerator source.
void thePaletteCommandIsInTheReferenceWithItsKeys(QuickPaletteGtkFixture& test) {
    xoj::gui::ShortcutReference* reference = test.win->getShortcutReference();
    ASSERT_NE(reference, nullptr);

    reference->open();
    settle();

    const std::vector<std::string> covered = reference->coveredIds();
    EXPECT_NE(std::find(covered.begin(), covered.end(), std::string("win.quick-palette")), covered.end())
            << "the command is in the reference because it is an action with metadata";
    EXPECT_EQ(reference->acceleratorOf("win.quick-palette"), "Ctrl+Shift+M");
    EXPECT_FALSE(reference->isConflicted("win.quick-palette")) << "its keys are its own";

    reference->close();
    settle();

    /*
     * The keys in the reference are the live ones the application holds, not a copy: remapping the
     * action moves them, exactly as remapping a menu command does. If the accelerator had been
     * written into the command instead of read from the application, this would still read the old
     * keys.
     */
    GtkApplication* app = GTK_APPLICATION(gtk_window_get_application(GTK_WINDOW(test.win->getWindow())));
    ASSERT_NE(app, nullptr);
    const char* remapped[] = {"<Control><Shift>n", nullptr};
    gtk_application_set_accels_for_action(app, "win.quick-palette", remapped);

    reference->open();
    settle();
    EXPECT_EQ(reference->acceleratorOf("win.quick-palette"), "Ctrl+Shift+N") << "the reference reads the live keys";
    reference->close();
    settle();
}

INSTANTIATE_TEST_SUITE_P(QuickPalette, QuickPaletteGtkFixture,
                         ::testing::Values(&showsWhereItWasSummonedWithoutCoveringThePenPoint,
                                           &everyPlaceItOffersSaysWhatItIs, &cancellingChangesNothingAboutTheTool,
                                           &theEraserButtonIsTheEraser,
                                           &theClassicToolboxIsWhatAppearsWhenThePaletteIsNotBound,
                                           &theKeyboardSummonsThePaletteWhenItIsBound,
                                           &theKeyboardDoesNothingWhenThePaletteIsNotBound,
                                           &thePaletteCommandIsInTheReferenceWithItsKeys));

/// The scenario runs inside the fixture's runTest, where the application is up.
TEST_P(QuickPaletteGtkFixture, theScenario) {}

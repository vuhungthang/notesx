/*
 * Xournal++
 *
 * The quick palette surface (Plan 008, step 1)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>  // for GtkOverlay, GtkWidget

#include "gui/QuickPaletteContents.h"   // for QuickPaletteSlot
#include "gui/QuickPalettePlacement.h"  // for SurfacePlacement

namespace xoj::gui {

/**
 * Plan 008: one button of the quick palette.
 *
 * A slot turned into something the widget layer can put on screen: either an application action,
 * named the way every other control names it, or - for a favourite preset, which is applied through
 * the preset adapter rather than an action - a callback. Nothing in the palette reaches into a
 * controller directly: activating a button does exactly what activating the toolbar item would.
 */
struct QuickPaletteButton {
    /// Stable id from the slot: "current-tool", "favorite:<preset id>", ...
    std::string id;
    /// The accessible name of the button. This is what makes the palette usable without seeing it.
    std::string label;
    /// The action to activate, or empty when the button has `onActivate` instead.
    std::string actionName;
    /**
     * The parameter the action takes, or nullptr.
     *
     * A tool selection is `win.select-tool` with the tool type as its parameter, which is how the
     * toolbar's own tool buttons are wired; a button here uses the same action and the same
     * parameter, so it takes the same path through the action layer. The reference is not owned by
     * the palette: activation sinks and unrefs its own.
     */
    GVariant* parameter = nullptr;
    /// Invoked when there is no action to activate.
    std::function<void()> onActivate;
    /// Optional icon name; the label is still set as the accessible name.
    std::string iconName;
};

/**
 * Plan 008: the quick palette.
 *
 * A surface of its own, with its own lifecycle, that lives in the main overlay the floating toolbox
 * lives in. Where it goes comes from placeSurface(), so it clamps to the viewport and does not sit
 * on the nib, and what it holds is handed to it as buttons - the palette does not know about tools,
 * presets or undo beyond the button it was given.
 *
 * Dismissing it, by Escape or by the pointer leaving it, is a cancel: nothing is activated and
 * nothing about the current tool changes. Only a button activation counts as an action, which is
 * what `dismissedWithoutAction()` distinguishes - and it is the reason the palette itself can never
 * change a tool just by being opened and closed.
 */
class QuickPalette {
public:
    QuickPalette(GtkOverlay* overlay, GtkWindow* window);
    ~QuickPalette();

    QuickPalette(const QuickPalette&) = delete;
    QuickPalette& operator=(const QuickPalette&) = delete;

    /// Replace the buttons. Rebuilds the widget; an open palette stays open.
    void setButtons(std::vector<QuickPaletteButton> buttons);

    /// Show the palette near (x, y), in the overlay's coordinates.
    void showAt(double x, double y);

    /// Hide it. Not a cancel: used when the caller is taking the palette away itself.
    void hide();

    /// Dismiss it as a cancel: nothing was activated.
    void cancel();

    auto isShown() const -> bool;
    auto dismissedWithoutAction() const -> bool;
    /// The placement the last showAt() computed; the tests check it against the viewport.
    auto lastPlacement() const -> const SurfacePlacement&;

    auto getWidget() const -> GtkWidget*;
    /// The button with this id, or nullptr.
    auto buttonFor(const std::string& id) const -> GtkWidget*;
    auto buttons() const -> const std::vector<QuickPaletteButton>&;

private:
    static bool onKeyPress(GtkWidget* widget, GdkEvent* event, QuickPalette* self);
    static bool onLeave(GtkWidget* widget, GdkEvent* event, QuickPalette* self);
    static bool onOverlayPosition(GtkOverlay* overlay, GtkWidget* widget, GdkRectangle* allocation, QuickPalette* self);
    static void onButtonClicked(GtkButton* button, gpointer data);
    static void destroyButtonContext(gpointer data, GClosure* closure);
    void rebuild();
    void activate(const QuickPaletteButton& button);

    GtkOverlay* overlay = nullptr;
    GtkWindow* window = nullptr;

    GtkWidget* revealer = nullptr;
    GtkWidget* box = nullptr;

    std::vector<QuickPaletteButton> buttonList;
    std::vector<std::pair<std::string, GtkWidget*>> widgets;  // id -> button, in order

    SurfacePlacement placement;
    bool shown = false;
    bool cancelled = true;
};

}  // namespace xoj::gui

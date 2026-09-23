/*
 * Xournal++
 *
 * What a fresh profile is told about the workspace it starts in.
 *
 * Plan 007, step 3. The Focus workspace hides most of the interface, which only works if what is
 * hidden stays findable: this is the one-time explanation of where the controls went and how to get
 * the old workspace back. It is deliberately not a question - it does not take the keyboard, it does
 * not block anything, and the page can be written on while it is up. Putting it away is remembered
 * in the profile, and "show interface tips again" in the settings brings it back.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

#include "util/raii/GObjectSPtr.h"  // for WidgetSPtr

class Settings;

namespace xoj::gui {

/**
 * The explanation of the workspace, over the window it explains.
 */
class WorkspaceGuidance {
public:
    /// Wide enough for the three lines to read as sentences rather than as a column of words.
    static constexpr int WIDTH = 360;

    WorkspaceGuidance(GtkWindow* window, Settings* settings);
    ~WorkspaceGuidance();

    /// Show it, unless this profile has already put it away or has turned the tips off.
    void showIfFresh();
    /// Put it away, and remember that this profile has seen it.
    void dismiss();
    auto isShown() const -> bool;

    /// The surface itself, and the button a keyboard user reaches to put it away.
    [[maybe_unused]] auto getPopover() const -> GtkWidget*;
    auto getDismissButton() const -> GtkWidget*;

private:
    GtkWindow* window;
    Settings* settings;

    /// The popover, which owns every widget below it.
    xoj::util::WidgetSPtr popover;
    xoj::util::WidgetSPtr dismissButton;

    gulong keyHandlerId = 0;
    gulong clickedHandlerId = 0;
};

};  // namespace xoj::gui

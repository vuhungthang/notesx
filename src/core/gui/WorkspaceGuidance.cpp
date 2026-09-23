#include "WorkspaceGuidance.h"

#include <utility>  // for move

#include <atk/atk.h>  // for atk_object_set_name

#include "control/settings/Settings.h"  // for Settings
#include "util/gtk4_helper.h"           // for gtk_widget_add_css_class
#include "util/i18n.h"                  // for _
#include "util/raii/GObjectSPtr.h"      // for WidgetSPtr

using xoj::gui::WorkspaceGuidance;

/*
 * Plan 007, step 3: what a fresh profile is told about the workspace, once.
 *
 * Three sentences and one button. What it must not be is a dialog: the workspace it explains is the
 * one that is already there, so the explanation sits over it rather than in front of it - nonmodal,
 * without the keyboard, and gone for good once it has been read.
 */

namespace {

/// What a screen reader reads for a widget: a button labelled "Got it" says nothing on its own.
void setAccessibleName(GtkWidget* widget, const char* name) {
    atk_object_set_name(gtk_widget_get_accessible(widget), name);
}

/// A line of the explanation: it wraps, because a translated sentence is longer than an English one.
auto addLine(GtkWidget* box, const char* text) -> GtkWidget* {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_bottom(label, 6);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    return label;
}
}  // namespace

WorkspaceGuidance::WorkspaceGuidance(GtkWindow* window, Settings* settings): window(window), settings(settings) {
    /*
     * A popover is anchored to a widget *inside* its window: GTK asserts when it is handed the window
     * itself. The window's own child fills it, so anchoring there places the explanation along the
     * bottom of the window's area - away from the command palette, which opens along the top.
     */
    GtkWidget* anchor = gtk_bin_get_child(GTK_BIN(window));
    g_assert(anchor != nullptr);

    GtkWidget* popover = gtk_popover_new(anchor);
    // Not a question: nothing is blocked while it is up, and the keyboard stays where it was.
    gtk_popover_set_modal(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    /*
     * No transition. An explanation that appears over the page the user is writing on does not need
     * a fade, and a fade is a callback GTK keeps on the widget until the animation ends - which is a
     * callback that can still arrive while the window it belongs to is being destroyed.
     */
    gtk_popover_set_transitions_enabled(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_size_request(popover, WIDTH, -1);
    this->popover.reset(popover, xoj::util::refsink);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(popover), box);

    GtkWidget* title = gtk_label_new(nullptr);
    gtk_label_set_markup(GTK_LABEL(title), _("<b>The Focus workspace</b>"));
    gtk_label_set_xalign(GTK_LABEL(title), 0.0);
    gtk_widget_set_margin_start(title, 12);
    gtk_widget_set_margin_end(title, 12);
    gtk_widget_set_margin_top(title, 12);
    gtk_widget_set_margin_bottom(title, 6);
    gtk_box_pack_start(GTK_BOX(box), title, FALSE, FALSE, 0);

    addLine(box, _("Focus keeps fewer controls in sight, so there is less between you and the page."));
    addLine(box, _("Click the active tool again to open its properties."));
    addLine(box, _("The workspace button switches to Classic at any time."));

    GtkWidget* dismiss = gtk_button_new_with_label(_("Got it"));
    gtk_widget_add_css_class(dismiss, "suggested-action");
    gtk_widget_set_margin_start(dismiss, 12);
    gtk_widget_set_margin_end(dismiss, 12);
    gtk_widget_set_margin_bottom(dismiss, 12);
    gtk_widget_set_halign(dismiss, GTK_ALIGN_END);
    setAccessibleName(dismiss, _("Dismiss the workspace explanation"));
    gtk_box_pack_start(GTK_BOX(box), dismiss, FALSE, FALSE, 0);
    this->dismissButton.reset(dismiss, xoj::util::ref);

    this->clickedHandlerId = g_signal_connect_swapped(
            dismiss, "clicked", G_CALLBACK(+[](WorkspaceGuidance* self) { self->dismiss(); }), this);
    // Escape puts it away for a keyboard user who has not tabbed to the button yet.
    this->keyHandlerId = g_signal_connect_swapped(
            popover, "key-press-event", G_CALLBACK(+[](WorkspaceGuidance* self, GdkEventKey* event) -> gboolean {
                if (event->keyval == GDK_KEY_Escape) {
                    self->dismiss();
                    return TRUE;
                }
                return FALSE;
            }),
            this);

    /*
     * The content is shown before the popover is: GTK lays out an unshown child as an empty box, and
     * gtk_popover_popup() only raises the popover, not what is inside it.
     */
    gtk_widget_show_all(box);
}

WorkspaceGuidance::~WorkspaceGuidance() {
    /*
     * A still-shown explanation is put away here, while its widgets are alive, by hiding it rather
     * than popping it down: a pop-down is a transition, and a destructor must not leave an animation
     * to finish on a window that is going away.
     */
    if (this->popover) {
        gtk_widget_set_visible(this->popover.get(), FALSE);
    }
}

void WorkspaceGuidance::showIfFresh() {
    if (this->popover == nullptr) {
        return;
    }
    // A profile that has put it away is not told again, and the global switch means nothing is
    // offered at all.
    if (this->settings->hasSeenInterfaceGuidance() || !this->settings->isInterfaceTipsEnabled()) {
        return;
    }
    gtk_popover_popup(GTK_POPOVER(this->popover.get()));
}

void WorkspaceGuidance::dismiss() {
    if (this->popover != nullptr && gtk_widget_is_visible(this->popover.get())) {
        gtk_popover_popdown(GTK_POPOVER(this->popover.get()));
    }
    this->settings->setInterfaceGuidanceSeen(true);
}

auto WorkspaceGuidance::isShown() const -> bool { return this->popover && gtk_widget_is_visible(this->popover.get()); }

auto WorkspaceGuidance::getPopover() const -> GtkWidget* { return this->popover.get(); }

auto WorkspaceGuidance::getDismissButton() const -> GtkWidget* { return this->dismissButton.get(); }

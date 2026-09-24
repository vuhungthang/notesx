#include "TipService.h"

#include <utility>  // for move

#include <atk/atk.h>  // for atk_object_set_name

#include "control/settings/Settings.h"  // for Settings
#include "util/i18n.h"                  // for _
#include "util/raii/GObjectSPtr.h"      // for WidgetSPtr

using xoj::gui::TipService;

/*
 * Plan 007, step 4: the tips.
 *
 * One popover, no transitions, shown against whatever the tip is about. What the service is for is
 * the decision, not the surface: whether this user has been told this already, whether they have
 * turned the tips off, and that only one tip is ever up at a time.
 */

namespace {

/// Where the service of a window is kept, so a widget that knows the window can find it.
constexpr const char* TIP_SERVICE_DATA_KEY = "xoj-tip-service";

constexpr int TIP_WIDTH = 320;

struct TipText {
    const char* id;
    const char* text;
};

/*
 * The tips there are, each with the id the profile remembers. The ids are what "do not tell me again"
 * is stored as, so they must not change; the sentences may.
 */
constexpr TipText TIPS[] = {
        {"tool-properties", "This is where a tool's own settings live - the same place every time."},
        {"favorite-presets", "Keep the presets you use most here, and they are one click away."},
        {"page-multi-select", "Several pages are selected: what you do next happens to all of them."},
        /*
         * Plan 008, step 7: what a gesture says the first time it acts. Each one names the gesture,
         * says what it just did and that it can be undone, because the notice is only ever offered
         * where there is an undo record - and offers, beside it, to turn the gesture off.
         */
        {"gesture-circle-to-select",
         "Circled: what was inside the circle is selected. Undo puts it back the way it was."},
        {"gesture-scribble-to-erase",
         "Scribbled: the covered strokes are gone. Undo brings them all back in one step."},
};

auto textFor(TipService::Tip tip) -> const char* {
    for (const TipText& t: TIPS) {
        if (t.id == TipService::idOf(tip)) {
            return t.text;
        }
    }
    return "";
}

}  // namespace

auto TipService::idOf(Tip tip) -> const char* {
    switch (tip) {
        case Tip::ToolProperties:
            return "tool-properties";
        case Tip::FavoritePresets:
            return "favorite-presets";
        case Tip::PageMultiSelect:
            return "page-multi-select";
        case Tip::GestureCircleToSelect:
            return "gesture-circle-to-select";
        case Tip::GestureScribbleToErase:
            return "gesture-scribble-to-erase";
    }
    return "";
}

auto TipService::textOf(Tip tip) -> const char* { return _(textFor(tip)); }

auto TipService::of(GtkWindow* window) -> TipService* {
    if (window == nullptr) {
        return nullptr;
    }
    return static_cast<TipService*>(g_object_get_data(G_OBJECT(window), TIP_SERVICE_DATA_KEY));
}

void TipService::offerAt(GtkWidget* widget, Tip tip, TurnOffAction turnOff) {
    if (widget == nullptr) {
        return;
    }
    GtkWidget* toplevel = gtk_widget_get_toplevel(widget);
    if (toplevel == nullptr || !GTK_IS_WINDOW(toplevel)) {
        return;
    }
    TipService* tips = of(GTK_WINDOW(toplevel));
    if (tips != nullptr) {
        tips->offer(tip, widget, std::move(turnOff));
    }
}

TipService::TipService(GtkWindow* window, Settings* settings): window(window), settings(settings) {
    // A popover is anchored to a widget inside its window: GTK asserts when it is handed the window.
    GtkWidget* anchor = gtk_bin_get_child(GTK_BIN(window));
    g_assert(anchor != nullptr);

    GtkWidget* popover = gtk_popover_new(anchor);
    gtk_popover_set_modal(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_BOTTOM);
    // No transition: a tip is read, and a running animation is a callback that can still arrive while
    // the window it belongs to is being destroyed.
    gtk_popover_set_transitions_enabled(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_size_request(popover, TIP_WIDTH, -1);
    this->popover.reset(popover, xoj::util::refsink);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(popover), box);

    GtkWidget* label = gtk_label_new("");
    gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_widget_set_margin_start(label, 12);
    gtk_widget_set_margin_end(label, 12);
    gtk_widget_set_margin_top(label, 12);
    gtk_widget_set_margin_bottom(label, 6);
    gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
    this->text.reset(label, xoj::util::ref);

    GtkWidget* dismiss = gtk_button_new_with_label(_("Got it"));
    gtk_widget_set_margin_start(dismiss, 12);
    gtk_widget_set_margin_end(dismiss, 12);
    gtk_widget_set_margin_bottom(dismiss, 12);
    gtk_widget_set_halign(dismiss, GTK_ALIGN_END);
    setAccessibleName(dismiss, _("Dismiss this tip"));
    gtk_box_pack_start(GTK_BOX(box), dismiss, FALSE, FALSE, 0);
    this->dismissButton.reset(dismiss, xoj::util::ref);

    /*
     * Plan 008, step 7: the second button a gesture notice offers - turning that gesture off. It is
     * built with the popover and hidden, because which tips offer it is decided per offer; a tip
     * about anything else never shows it, and never shows a button that would do nothing.
     */
    GtkWidget* turnOff = gtk_button_new_with_label(_("Turn this gesture off"));
    gtk_widget_set_margin_start(turnOff, 12);
    gtk_widget_set_margin_end(turnOff, 6);
    gtk_widget_set_margin_bottom(turnOff, 12);
    gtk_widget_set_halign(turnOff, GTK_ALIGN_END);
    setAccessibleName(turnOff, _("Turn the gesture this notice is about off"));
    gtk_box_pack_start(GTK_BOX(box), turnOff, FALSE, FALSE, 0);
    gtk_widget_set_visible(turnOff, FALSE);
    this->turnOffButton.reset(turnOff, xoj::util::ref);

    this->clickedHandlerId =
            g_signal_connect_swapped(dismiss, "clicked", G_CALLBACK(+[](TipService* self) { self->dismiss(); }), this);
    g_signal_connect_swapped(turnOff, "clicked", G_CALLBACK(+[](TipService* self) {
                                 // The action first, while what it changes still has an undo record
                                 // behind it; then the notice goes and is not shown again.
                                 if (self->turnOff) {
                                     self->turnOff();
                                 }
                                 self->dismiss();
                             }),
                             this);
    // Escape puts it away for a keyboard user who has not tabbed to the button yet.
    this->keyHandlerId = g_signal_connect_swapped(popover, "key-press-event",
                                                  G_CALLBACK(+[](TipService* self, GdkEventKey* event) -> gboolean {
                                                      if (event->keyval == GDK_KEY_Escape) {
                                                          self->dismiss();
                                                          return TRUE;
                                                      }
                                                      return FALSE;
                                                  }),
                                                  this);
    /*
     * A tip whose anchor is gone is a tip with nothing to point at: GTK pops the popover down when
     * the widget it is anchored to goes away, and this keeps the service's own idea of what is up in
     * step with what is on screen.
     */
    this->closedHandlerId = g_signal_connect_swapped(popover, "closed",
                                                     G_CALLBACK(+[](TipService* self) { self->shown.reset(); }), this);

    g_object_set_data(G_OBJECT(window), TIP_SERVICE_DATA_KEY, this);
    gtk_widget_show_all(box);
}

TipService::~TipService() {
    /*
     * A tip that is still up when its window goes is put away here, while its widgets are alive, by
     * hiding it rather than popping it down: a pop-down is a transition, and a destructor must not
     * leave an animation to finish on a window that is going away. A popup that never ran is
     * cancelled for the same reason - an idle that fires after this destructor would pop up a
     * popover whose service is gone.
     */
    if (this->pendingPopup != 0) {
        g_source_remove(this->pendingPopup);
        this->pendingPopup = 0;
    }
    if (this->window != nullptr && of(this->window) == this) {
        g_object_set_data(G_OBJECT(this->window), TIP_SERVICE_DATA_KEY, nullptr);
    }
    if (this->popover) {
        gtk_widget_set_visible(this->popover.get(), FALSE);
    }
}

void TipService::offer(Tip tip, GtkWidget* anchor, TurnOffAction turnOff) {
    if (this->popover == nullptr || this->settings == nullptr) {
        return;
    }
    // A user who has been told this, or who has turned the tips off, is not told anything.
    if (this->settings->hasSeenTip(idOf(tip)) || !this->settings->isInterfaceTipsEnabled()) {
        return;
    }

    // At most one: a tip offered while another is up takes its place rather than sitting next to it.
    if (gtk_widget_is_visible(this->popover.get())) {
        gtk_popover_popdown(GTK_POPOVER(this->popover.get()));
    }
    // And a popup that has not run yet cannot outlive the offer it belonged to: the idle below is
    // the only one on its way to the screen.
    if (this->pendingPopup != 0) {
        g_source_remove(this->pendingPopup);
        this->pendingPopup = 0;
    }

    this->turnOff = std::move(turnOff);
    if (this->turnOffButton) {
        gtk_widget_set_visible(this->turnOffButton.get(), this->turnOff != nullptr);
    }

    this->shown = tip;
    gtk_label_set_text(GTK_LABEL(this->text.get()), textOf(tip));
    this->placeAgainst(anchor);
    /*
     * Popped up from the main context, not from inside the caller: a tip's moments are the moments
     * something is being shown - a tool's property popover mapping, a preset being applied - and
     * those arrive inside GTK's own event handling (the "map" of a popover, a button's "clicked").
     * Popping a second popover up from within that delivery hands GTK a grab and a pointer event
     * sequence it cannot finish consistently, which surfaces as
     * "gtk_widget_event: assertion 'WIDGET_REALIZED_FOR_EVENT (widget, event)' failed" the moment
     * the tip appears. An idle callback runs once GTK is back in charge of its own event loop, and
     * the pointer events around the tip are then ordinary events for a realized, mapped popover.
     *
     * The callback holds the service itself, not its popover: the id is cleared the moment the
     * callback runs, and a service that is destroyed before then has the source cancelled in its
     * destructor - the callback can never run on a service that is gone.
     */
    this->pendingPopup = g_idle_add_full(G_PRIORITY_HIGH_IDLE,
                                         +[](gpointer data) -> gboolean {
                                             TipService* self = static_cast<TipService*>(data);
                                             // The source is running, so it is already gone from
                                             // the context: forget the id before anything dismisses
                                             // the tip and tries to remove it a second time.
                                             self->pendingPopup = 0;
                                             if (!gtk_widget_get_visible(self->popover.get())) {
                                                 gtk_popover_popup(GTK_POPOVER(self->popover.get()));
                                             }
                                             return G_SOURCE_REMOVE;
                                         },
                                         this, nullptr);
}

void TipService::placeAgainst(GtkWidget* anchor) {
    if (anchor == nullptr) {
        return;
    }
    /*
     * A tip points at the thing it is about, and follows it: the anchor is what it is anchored to,
     * whenever it is handed over. Whether GTK has mapped that widget yet is not this service's
     * question and not a reason to leave the tip pointing at whatever was there before: the moments a
     * tip is about are the moments a surface appears, and a caller that offers a tip for a widget it
     * has just put in its window is offering it for that widget and not for an earlier one.
     */
    gtk_popover_set_relative_to(GTK_POPOVER(this->popover.get()), anchor);
    gtk_popover_set_position(GTK_POPOVER(this->popover.get()), GTK_POS_BOTTOM);
}

void TipService::dismiss() {
    // A popup that has not run yet is not a tip any more: cancelled here, it cannot appear after
    // the user has already put it away (or after the offer was replaced by a newer one).
    if (this->pendingPopup != 0) {
        g_source_remove(this->pendingPopup);
        this->pendingPopup = 0;
    }
    const std::optional<Tip> tip = this->shown;
    if (this->popover != nullptr && gtk_widget_is_visible(this->popover.get())) {
        gtk_popover_popdown(GTK_POPOVER(this->popover.get()));
    }
    this->shown.reset();
    if (tip.has_value()) {
        this->settings->markTipSeen(idOf(*tip));
    }
}

auto TipService::isShown() const -> bool { return this->popover && gtk_widget_is_visible(this->popover.get()); }

auto TipService::shownTip() const -> std::optional<Tip> { return this->shown; }

auto TipService::getPopover() const -> GtkWidget* { return this->popover.get(); }

auto TipService::getDismissButton() const -> GtkWidget* { return this->dismissButton.get(); }

auto TipService::getTurnOffButton() const -> GtkWidget* { return this->turnOffButton.get(); }

void TipService::setAccessibleName(GtkWidget* widget, const char* name) {
    atk_object_set_name(gtk_widget_get_accessible(widget), name);
}

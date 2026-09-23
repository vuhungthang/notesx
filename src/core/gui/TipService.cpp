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

void TipService::offerAt(GtkWidget* widget, Tip tip) {
    if (widget == nullptr) {
        return;
    }
    GtkWidget* toplevel = gtk_widget_get_toplevel(widget);
    if (toplevel == nullptr || !GTK_IS_WINDOW(toplevel)) {
        return;
    }
    TipService* tips = of(GTK_WINDOW(toplevel));
    if (tips != nullptr) {
        tips->offer(tip, widget);
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

    this->clickedHandlerId =
            g_signal_connect_swapped(dismiss, "clicked", G_CALLBACK(+[](TipService* self) { self->dismiss(); }), this);
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
     * leave an animation to finish on a window that is going away.
     */
    if (this->window != nullptr && of(this->window) == this) {
        g_object_set_data(G_OBJECT(this->window), TIP_SERVICE_DATA_KEY, nullptr);
    }
    if (this->popover) {
        gtk_widget_set_visible(this->popover.get(), FALSE);
    }
}

void TipService::offer(Tip tip, GtkWidget* anchor) {
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

    this->shown = tip;
    gtk_label_set_text(GTK_LABEL(this->text.get()), textOf(tip));
    this->placeAgainst(anchor);
    gtk_popover_popup(GTK_POPOVER(this->popover.get()));
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

void TipService::setAccessibleName(GtkWidget* widget, const char* name) {
    atk_object_set_name(gtk_widget_get_accessible(widget), name);
}

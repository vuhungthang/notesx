/*
 * Xournal++
 *
 * The tips: one thing said once, when the user first gets somewhere (Plan 007, step 4).
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

#include "util/raii/GObjectSPtr.h"  // for WidgetSPtr

class Settings;

namespace xoj::gui {

/**
 * The tips (Plan 007, step 4).
 *
 * A tip is a sentence about something the user has just reached for the first time. Whatever knows
 * the moment offers one - a tool's properties being opened, a preset being picked - and the service
 * decides whether this user is told: at most one tip is ever up, a tip that has been put away is
 * never shown again, and turning the tips off means nothing is offered at all.
 *
 * A tip is identified by a stable id, which is what the profile remembers; the sentence itself can
 * be reworded without turning it into a different tip.
 *
 * The tips that exist are the ones the application can currently offer, and every one of them is
 * offered by the code that already knows its moment - a tool's properties being opened, a favourite
 * preset being picked. That code says so with offerAt(), which is the whole extension path: a tip
 * the application learns to offer later is an id in `idOf()` and an offer where its moment is
 * recognised, and the service needs nothing new. The gesture hint Plan 008 is about is that third
 * thing and no more; the page multi-select below has its id and no trigger yet, because the
 * multi-select itself does not exist in this tree (see the note on `PageMultiSelect`).
 */
class TipService {
public:
    enum class Tip {
        /// The first time a tool's own properties are opened.
        ToolProperties,
        /// The first time the user reaches for the favorite presets.
        FavoritePresets,
        /**
         * The first time the user selects more than one page.
         *
         * Nothing offers this one yet, on purpose: there is no page multi-select in this tree, and a
         * tip offered from a trigger that does not exist would be a sentence about a feature nobody
         * can reach. The id is here because ids are what profiles store, so whoever adds the
         * multi-select - a selection over several pages in the page list - offers this tip from the
         * code that recognises the second page being added, with offerAt() and no change here.
         */
        PageMultiSelect,
    };

    /// The stable id of a tip: what the profile remembers, and what a caller offers.
    static auto idOf(Tip tip) -> const char*;
    /// What the tip says, translated.
    static auto textOf(Tip tip) -> const char*;

    /**
     * The tip service of a window, if it has one.
     *
     * A widget that knows the moment a tip is about knows its window - the popover of a tool, say -
     * and not the object the window belongs to. The window is where the service is found, and where
     * it is looked up when it is offered.
     */
    static auto of(GtkWindow* window) -> TipService*;

    /**
     * Offer a tip about @p widget, in the window that widget is in.
     *
     * How a widget that knows its own moment offers a tip: it does not know the window's owner, and
     * it should not have to. The window is looked up from the widget, the tip is anchored to the
     * widget, and a widget in a window whose service has nothing to say - or in no window at all -
     * does nothing.
     */
    static void offerAt(GtkWidget* widget, Tip tip);

    TipService(GtkWindow* window, Settings* settings);
    ~TipService();

    TipService(const TipService&) = delete;
    TipService& operator=(const TipService&) = delete;

    /**
     * Offer a tip, shown against @p anchor - the widget the tip is about.
     *
     * Nothing happens when the user has already put this tip away, when the tips are turned off, or
     * when this window has no service. A tip that is offered while another one is up takes its
     * place: tips never stack.
     */
    void offer(Tip tip, GtkWidget* anchor);
    /// Put the tip away, and remember that this user has seen it.
    void dismiss();
    auto isShown() const -> bool;
    auto shownTip() const -> std::optional<Tip>;

    [[maybe_unused]] auto getPopover() const -> GtkWidget*;
    /// The button a keyboard user reaches to put the tip away.
    auto getDismissButton() const -> GtkWidget*;

private:
    void placeAgainst(GtkWidget* anchor);
    static void setAccessibleName(GtkWidget* widget, const char* name);

    GtkWindow* window = nullptr;
    Settings* settings = nullptr;

    xoj::util::WidgetSPtr popover;
    xoj::util::WidgetSPtr text;
    xoj::util::WidgetSPtr dismissButton;

    /// Which tip is up, which is not the same as what the popover shows: the popover is built once.
    std::optional<Tip> shown;

    gulong clickedHandlerId = 0;
    gulong keyHandlerId = 0;
    gulong closedHandlerId = 0;
};

};  // namespace xoj::gui

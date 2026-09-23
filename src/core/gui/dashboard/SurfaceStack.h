/*
 * Xournal++
 *
 * The window's two surfaces: the editor and the home dashboard, as two pages of one stack, plus the
 * one way from the editor to the dashboard.
 *
 * Plan 006, step 3. Switching surfaces changes which page is visible and nothing else. Nothing is
 * created, destroyed or re-parented by a switch, so the editor keeps its document, its page, its
 * zoom, its selection and its unsaved changes simply because it is never taken out of the window.
 * The dashboard is an index over files, so the only thing a switch asks its owner for is to make
 * that index current.
 *
 * Both the button and the keyboard shortcut go through the same window action, so there is one way
 * in rather than two, and the button is deliberately not a neighbour of undo and redo: leaving the
 * document should not be one slip away from a hand that has just undone something.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>  // for function

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

#include "util/raii/GObjectSPtr.h"  // for WidgetSPtr

namespace xoj::dashboard {

/**
 * The stack the editor and the dashboard are the two pages of.
 */
class SurfaceStack {
public:
    /// The names of the two surfaces, as the stack knows them.
    static constexpr const char* EDITOR = "editor";
    static constexpr const char* HOME = "home";
    /// The window action both the Home button and its shortcut use.
    static constexpr const char* SHOW_HOME_ACTION = "show-home";
    static constexpr const char* SHOW_HOME_FULL_ACTION = "win.show-home";

    struct Callbacks {
        /**
         * The home surface is about to be shown: make it say what the user's files are now.
         *
         * Called before the switch, because what the dashboard shows is read from those files and
         * the surface the user sees must be the one that was just built, not the one from last time.
         */
        std::function<void()> prepareHome;
        /// A surface is now shown; `home` says which. The owner keeps its own state in step here.
        std::function<void(bool home)> shown;
    };

    /**
     * @param window the window the action belongs to and the shortcut is registered on
     * @param editor the editor's widget, which stays the window's editor whatever is shown
     * @param home the dashboard's widget
     */
    SurfaceStack(GtkWindow* window, GtkWidget* editor, GtkWidget* home, Callbacks callbacks);
    ~SurfaceStack();

    SurfaceStack(const SurfaceStack&) = delete;
    auto operator=(const SurfaceStack&) -> SurfaceStack& = delete;

    /// The stack itself. Add it to the window's layout; it owns both surfaces.
    auto getWidget() const -> GtkWidget*;
    /// The bar that holds the way from the editor to the dashboard. Shown with the editor only.
    auto getBar() const -> GtkWidget*;
    auto getHomeButton() const -> GtkWidget*;

    /// Show the dashboard.
    void showHome();
    /// Show the document again, exactly as it was left.
    void showEditor();
    auto isHomeShown() const -> bool;

private:
    void setShown(const char* surface);

    GtkWindow* window = nullptr;
    Callbacks callbacks;
    xoj::util::WidgetSPtr stack;
    xoj::util::WidgetSPtr bar;
    GtkWidget* homeButton = nullptr;
    /// Whether this object registered the window action, so it only removes its own.
    bool actionAdded = false;
};

}  // namespace xoj::dashboard

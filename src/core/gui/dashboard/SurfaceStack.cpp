#include "SurfaceStack.h"

#include <string>   // for string
#include <utility>  // for move

#include "util/gtk4_helper.h"  // IWYU pragma: keep (GTK3/4 helpers)
#include "util/i18n.h"         // for _

using namespace xoj::dashboard;

SurfaceStack::SurfaceStack(GtkWindow* window, GtkWidget* editor, GtkWidget* home, Callbacks callbacks):
        window(window), callbacks(std::move(callbacks)) {
    this->stack.reset(gtk_stack_new(), xoj::util::adopt);
    gtk_widget_set_name(this->stack.get(), "mainSurfaceStack");
    // No transition: a surface change is the user changing where they are, not a decoration, and the
    // editor must not be animated away from under a hand that is drawing.
    gtk_stack_set_transition_type(GTK_STACK(this->stack.get()), GTK_STACK_TRANSITION_TYPE_NONE);
    gtk_widget_set_vexpand(this->stack.get(), TRUE);
    gtk_widget_set_hexpand(this->stack.get(), TRUE);

    gtk_stack_add_named(GTK_STACK(this->stack.get()), editor, EDITOR);
    gtk_stack_add_named(GTK_STACK(this->stack.get()), home, HOME);
    gtk_stack_set_visible_child_name(GTK_STACK(this->stack.get()), EDITOR);

    /*
     * The one way from the editor to the home surface.
     *
     * It is deliberately not a neighbour of undo and redo: a hand that has just undone something is
     * a hand that is close to those buttons, and leaving the document must not be one slip away
     * from them. The button and the keyboard shortcut both go through the same window action, so
     * there is one way in rather than two.
     */
    this->homeButton = gtk_button_new_with_label(_("Home"));
    gtk_widget_set_name(this->homeButton, "homeButton");
    gtk_widget_set_tooltip_text(this->homeButton, _("Recent notes, pinned notes, folders and recovered work"));
    atk_object_set_name(gtk_widget_get_accessible(this->homeButton),
                        _("Home: the dashboard of your notes and recovered work"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(this->homeButton), SHOW_HOME_FULL_ACTION);
    gtk_widget_set_valign(this->homeButton, GTK_ALIGN_CENTER);

    this->bar.reset(gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6), xoj::util::adopt);
    gtk_widget_set_name(this->bar.get(), "homeBar");
    gtk_style_context_add_class(gtk_widget_get_style_context(this->bar.get()), "xoj-home-bar");
    gtk_widget_set_margin_start(this->bar.get(), 6);
    gtk_widget_set_margin_end(this->bar.get(), 6);
    gtk_widget_set_margin_top(this->bar.get(), 4);
    gtk_widget_set_margin_bottom(this->bar.get(), 2);
    gtk_box_append(GTK_BOX(this->bar.get()), this->homeButton);

    GSimpleAction* showHomeAction = g_simple_action_new(SHOW_HOME_ACTION, nullptr);
    g_signal_connect(showHomeAction, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer data) {
                         static_cast<SurfaceStack*>(data)->showHome();
                     }),
                     this);
    g_action_map_add_action(G_ACTION_MAP(this->window), G_ACTION(showHomeAction));
    this->actionAdded = true;
    g_object_unref(showHomeAction);

    /*
     * The window may be destroyed before this object - a window that is closed, a test that destroys
     * it - and the action belongs to the window. A weak pointer says when the window is gone, so the
     * destructor below never reaches into a window that no longer exists.
     */
    g_object_add_weak_pointer(G_OBJECT(this->window), reinterpret_cast<gpointer*>(&this->window));

    if (GtkApplication* application = gtk_window_get_application(this->window); application != nullptr) {
        const char* accels[] = {"<Control>Home", nullptr};
        gtk_application_set_accels_for_action(application, SHOW_HOME_FULL_ACTION, accels);
    }
}

SurfaceStack::~SurfaceStack() {
    /*
     * The action belongs to the window and points at this object, so it goes with it: a window that
     * outlives its surfaces must not have a button that calls into freed memory. A window that was
     * destroyed first needs nothing: the weak pointer above has already cleared it, and the action
     * went with it.
     */
    if (this->window == nullptr) {
        return;
    }

    if (this->actionAdded) {
        g_action_map_remove_action(G_ACTION_MAP(this->window), SHOW_HOME_ACTION);
    }
    this->actionAdded = false;
    g_object_remove_weak_pointer(G_OBJECT(this->window), reinterpret_cast<gpointer*>(&this->window));
}

auto SurfaceStack::getWidget() const -> GtkWidget* { return this->stack.get(); }
auto SurfaceStack::getBar() const -> GtkWidget* { return this->bar.get(); }
auto SurfaceStack::getHomeButton() const -> GtkWidget* { return this->homeButton; }

void SurfaceStack::showHome() {
    // Before the switch, so the page the user is about to see was built from the files as they are
    // now rather than from what they were when the dashboard was last looked at.
    if (this->callbacks.prepareHome) {
        this->callbacks.prepareHome();
    }
    this->setShown(HOME);
}

void SurfaceStack::showEditor() { this->setShown(EDITOR); }

auto SurfaceStack::isHomeShown() const -> bool {
    const char* shown = gtk_stack_get_visible_child_name(GTK_STACK(this->stack.get()));
    return shown != nullptr && std::string(shown) == HOME;
}

void SurfaceStack::setShown(const char* surface) {
    gtk_stack_set_visible_child_name(GTK_STACK(this->stack.get()), surface);

    // The dashboard carries its own way back, so the editor's own button would only be in the way
    // while the dashboard is on screen.
    const bool home = std::string(surface) == HOME;
    if (home) {
        gtk_widget_hide(this->bar.get());
    } else {
        gtk_widget_show(this->bar.get());
    }

    if (this->callbacks.shown) {
        this->callbacks.shown(home);
    }
}

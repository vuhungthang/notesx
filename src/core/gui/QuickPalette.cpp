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

#include "QuickPalette.h"

#include <algorithm>
#include <utility>

#include "util/glib_casts.h"

namespace xoj::gui {

namespace {
/// Ties a button widget to the palette button it activates; owned by the widget's closure.
struct ButtonContext {
    QuickPalette* palette = nullptr;
    std::size_t index = 0;
};

/**
 * The name the window's own action group knows an action by.
 *
 * The window's map holds its actions under their bare names - that is what g_action_group_* looks
 * up, and what the rest of the test suite asserts about the window. "win." is the prefix GTK
 * resolves for widgets through gtk_actionable_set_action_name, not part of the stored name, so it
 * is stripped here rather than being handed to g_action_group_activate_action and failing.
 */
auto actionNameForWindow(const std::string& name) -> std::string {
    constexpr const char* WINDOW_PREFIX = "win.";
    constexpr std::size_t PREFIX_LENGTH = 4;
    if (name.rfind(WINDOW_PREFIX, 0) == 0) {
        return name.substr(PREFIX_LENGTH);
    }
    return name;
}
}  // namespace

QuickPalette::QuickPalette(GtkOverlay* overlay, GtkWindow* window): overlay(overlay), window(window) {
    // A revealer, like the floating toolbox, because a plain box over the overlay has trouble with
    // leave-notify.
    this->revealer = gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(this->revealer), GTK_REVEALER_TRANSITION_TYPE_NONE);
    gtk_widget_set_name(this->revealer, "quickPalette");
    gtk_widget_set_no_show_all(this->revealer, TRUE);

    this->box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_container_add(GTK_CONTAINER(this->revealer), this->box);
    gtk_widget_add_events(this->box, GDK_LEAVE_NOTIFY_MASK);
    gtk_widget_set_can_focus(this->box, TRUE);

    gtk_overlay_add_overlay(this->overlay, this->revealer);
    // The palette is interactive, unlike the pass-through floating toolbox shell.
    gtk_overlay_set_overlay_pass_through(this->overlay, this->revealer, false);

    g_signal_connect(this->box, "key-press-event", xoj::util::wrap_for_g_callback_v<onKeyPress>, this);
    g_signal_connect(this->box, "leave-notify-event", xoj::util::wrap_for_g_callback_v<onLeave>, this);
    g_signal_connect(this->overlay, "get-child-position", xoj::util::wrap_for_g_callback_v<onOverlayPosition>, this);
    if (this->window != nullptr) {
        // Escape may reach the window rather than the palette; the palette only acts when it is up.
        g_signal_connect(this->window, "key-press-event", xoj::util::wrap_for_g_callback_v<onKeyPress>, this);
    }

    this->rebuild();
}

QuickPalette::~QuickPalette() {
    if (this->window != nullptr) {
        g_signal_handlers_disconnect_by_data(this->window, this);
    }
    if (this->overlay != nullptr) {
        g_signal_handlers_disconnect_by_data(this->overlay, this);
    }
    if (this->box != nullptr) {
        g_signal_handlers_disconnect_by_data(this->box, this);
    }
}

void QuickPalette::setButtons(std::vector<QuickPaletteButton> buttons) {
    this->buttonList = std::move(buttons);
    this->rebuild();
}

void QuickPalette::rebuild() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(this->box));
    for (GList* node = children; node != nullptr; node = node->next) {
        gtk_container_remove(GTK_CONTAINER(this->box), GTK_WIDGET(node->data));
    }
    g_list_free(children);
    this->widgets.clear();

    for (std::size_t i = 0; i < this->buttonList.size(); i++) {
        const QuickPaletteButton& button = this->buttonList[i];
        GtkWidget* widget = gtk_button_new();
        gtk_button_set_relief(GTK_BUTTON(widget), GTK_RELIEF_NONE);
        if (!button.iconName.empty()) {
            GtkWidget* image = gtk_image_new_from_icon_name(button.iconName.c_str(), GTK_ICON_SIZE_SMALL_TOOLBAR);
            gtk_button_set_image(GTK_BUTTON(widget), image);
        }
        // The label is the accessible name, so the palette is usable by a screen reader and by a
        // test that cannot see it.
        gtk_widget_set_tooltip_text(widget, button.label.c_str());
        gtk_button_set_label(GTK_BUTTON(widget), button.label.c_str());
        gtk_widget_set_can_focus(widget, TRUE);

        auto* context = new ButtonContext{this, i};
        g_signal_connect_data(widget, "clicked", G_CALLBACK(&QuickPalette::onButtonClicked), context,
                              &QuickPalette::destroyButtonContext, GConnectFlags(0));

        this->widgets.emplace_back(button.id, widget);
        gtk_box_pack_start(GTK_BOX(this->box), widget, FALSE, FALSE, 0);
    }

    gtk_widget_show_all(this->box);
    if (!this->shown) {
        gtk_widget_hide(this->revealer);
    }
}

void QuickPalette::onButtonClicked(GtkButton*, gpointer data) {
    auto* context = static_cast<ButtonContext*>(data);
    if (context == nullptr || context->palette == nullptr) {
        return;
    }
    if (context->index >= context->palette->buttonList.size()) {
        return;
    }
    context->palette->activate(context->palette->buttonList[context->index]);
}

void QuickPalette::destroyButtonContext(gpointer data, GClosure*) { delete static_cast<ButtonContext*>(data); }

void QuickPalette::activate(const QuickPaletteButton& button) {
    this->cancelled = false;
    this->hide();

    if (!button.actionName.empty() && this->window != nullptr && G_IS_ACTION_GROUP(G_OBJECT(this->window))) {
        GVariant* parameter = button.parameter;
        if (parameter != nullptr) {
            // activate_action consumes a floating reference; hold our own across the call.
            g_variant_ref_sink(parameter);
        }
        const std::string actionName = actionNameForWindow(button.actionName);
        g_action_group_activate_action(G_ACTION_GROUP(this->window), actionName.c_str(), parameter);
        if (parameter != nullptr) {
            g_variant_unref(parameter);
        }
        return;
    }
    if (button.onActivate) {
        button.onActivate();
    }
}

void QuickPalette::showAt(double x, double y) {
    SurfacePlacementInput input;
    input.viewportWidth = static_cast<double>(gtk_widget_get_allocated_width(GTK_WIDGET(this->overlay)));
    input.viewportHeight = static_cast<double>(gtk_widget_get_allocated_height(GTK_WIDGET(this->overlay)));
    input.anchorX = x;
    input.anchorY = y;
    input.avoidAnchor = true;
    input.vertical = SurfaceAnchorVertical::Above;

    GtkRequisition natural;
    gtk_widget_get_preferred_size(this->revealer, nullptr, &natural);
    input.surfaceWidth = std::max(1.0, static_cast<double>(natural.width));
    input.surfaceHeight = std::max(1.0, static_cast<double>(natural.height));

    this->placement = placeSurface(input);

    this->shown = true;
    this->cancelled = true;  // until something is activated, this appearance is a cancel
    gtk_revealer_set_reveal_child(GTK_REVEALER(this->revealer), TRUE);
    gtk_widget_show(this->revealer);
    gtk_widget_grab_focus(this->box);
}

void QuickPalette::hide() {
    this->shown = false;
    gtk_revealer_set_reveal_child(GTK_REVEALER(this->revealer), FALSE);
    gtk_widget_hide(this->revealer);
}

void QuickPalette::cancel() {
    this->cancelled = true;
    this->hide();
}

auto QuickPalette::isShown() const -> bool { return this->shown && gtk_widget_get_visible(this->revealer) != FALSE; }

auto QuickPalette::dismissedWithoutAction() const -> bool { return this->cancelled; }

auto QuickPalette::lastPlacement() const -> const SurfacePlacement& { return this->placement; }

auto QuickPalette::getWidget() const -> GtkWidget* { return this->revealer; }

auto QuickPalette::buttonFor(const std::string& id) const -> GtkWidget* {
    const auto it = std::find_if(this->widgets.begin(), this->widgets.end(),
                                 [&id](const std::pair<std::string, GtkWidget*>& entry) { return entry.first == id; });
    return it == this->widgets.end() ? nullptr : it->second;
}

auto QuickPalette::buttons() const -> const std::vector<QuickPaletteButton>& { return this->buttonList; }

bool QuickPalette::onKeyPress(GtkWidget*, GdkEvent* event, QuickPalette* self) {
    if (!self->isShown()) {
        return false;
    }
    if (event->type == GDK_KEY_PRESS && event->key.keyval == GDK_KEY_Escape) {
        self->cancel();
        return true;
    }
    return false;
}

bool QuickPalette::onLeave(GtkWidget*, GdkEvent* event, QuickPalette* self) {
    if (!self->isShown()) {
        return false;
    }
    if (event->type == GDK_LEAVE_NOTIFY) {
        self->cancel();
        return true;
    }
    return false;
}

bool QuickPalette::onOverlayPosition(GtkOverlay*, GtkWidget* widget, GdkRectangle* allocation, QuickPalette* self) {
    if (widget != self->revealer) {
        return false;
    }
    allocation->x = static_cast<int>(self->placement.x);
    allocation->y = static_cast<int>(self->placement.y);
    return true;
}

}  // namespace xoj::gui

#include "ToolPropertyPopover.h"

#include "control/ToolConfigAdapter.h"  // for ToolConfigAdapter
#include "gui/TipService.h"             // for TipService (Plan 007)
#include "util/gtk4_helper.h"           // for gtk_popover_new, gtk_popover_set_child, gtk_widget_add_css_class

ToolPropertyPopoverFactory::ToolPropertyPopoverFactory(ToolConfigAdapter& adapter, Settings& settings,
                                                       ToolPropertyProvider& provider, GtkWindow* parent,
                                                       PresetListListener* presetsListener):
        adapter(adapter), settings(settings), provider(provider), parent(parent), presetsListener(presetsListener) {}

ToolPropertyPopoverFactory::~ToolPropertyPopoverFactory() = default;

auto ToolPropertyPopoverFactory::getToolType() const -> ToolType { return this->provider.getToolType(); }

auto ToolPropertyPopoverFactory::isActiveTool() const -> bool {
    return this->adapter.getState().toolType == this->provider.getToolType();
}

void ToolPropertyPopoverFactory::destroyPanel(gpointer data) { delete static_cast<ToolPropertyPanel*>(data); }

void ToolPropertyPopoverFactory::offerPropertiesTipOnce(GtkWidget* popover, gpointer parentWindow) {
    xoj::gui::TipService* tips = xoj::gui::TipService::of(GTK_WINDOW(parentWindow));
    if (tips != nullptr) {
        /*
         * The tip points at the control that opened the popover, never at the popover itself: GTK3
         * cannot anchor a popover to another popover, and a tip handed one renders as a misplaced
         * dark slab over the panel it is about. The anchor the popover was attached to is the
         * toolbar control the user clicked, which is on the window's own surface and is what the
         * tip should point at anyway.
         */
        GtkWidget* anchor = gtk_popover_get_relative_to(GTK_POPOVER(popover));
        tips->offer(xoj::gui::TipService::Tip::ToolProperties, anchor != nullptr ? anchor : popover);
    }
}

auto ToolPropertyPopoverFactory::createPopover() const -> GtkWidget* {
    auto* panel =
            new ToolPropertyPanel(this->adapter, this->settings, this->provider, this->parent, this->presetsListener);

    GtkWidget* popover = gtk_popover_new();
    gtk_widget_add_css_class(popover, "toolbar");
    gtk_widget_add_css_class(popover, "xoj-tool-property-popover");
    GtkWidget* content = panel->createWidget();

    /*
     * The panel is capped to the screen rather than trusted to fit it: a tool's properties stack
     * up (five widths, a colour, a fill, six drawing types, four line styles, a preset row each),
     * and a popover that asks for more height than the workarea has is clamped by GTK instead of
     * laid out - every row is compressed into the space of a fraction of itself, which is how the
     * panel came to be squeezed too tight to read, let alone click. A scrolled window gives the
     * panel its natural height and hands the overflow a scrollbar, so no tool ever grows a panel
     * the screen cannot show.
     *
     * The cap is a ratio of the workarea, not a hard number: the same 80% is generous on a tall
     * monitor and honest on a small laptop screen, and the width cap keeps a long preset name
     * from pushing the popover off the side of the screen. Policy NONE/NONE: the scrollbar is
     * shown only while it is needed, so a panel that fits shows no scrollbar at all.
     */
    GtkWidget* scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    // Natural height propagates, so a panel that fits the cap keeps exactly its own height; the
    // max-content height below is what caps one that does not.
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll), true);
    if (this->parent != nullptr) {
        GdkDisplay* display = gtk_widget_get_display(GTK_WIDGET(this->parent));
        // The parent's own window is not there before it is realized - a popover can be built
        // first - so the first monitor of the display is the fallback, and its workarea is what
        // the cap uses then.
        GdkWindow* window = gtk_widget_get_window(GTK_WIDGET(this->parent));
        GdkMonitor* monitor = window != nullptr ? gdk_display_get_monitor_at_window(display, window) :
                                                  gdk_display_get_monitor(display, 0);
        if (monitor != nullptr) {
            GdkRectangle workarea{};
            gdk_monitor_get_workarea(monitor, &workarea);
            if (workarea.height > 0) {
                gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll),
                                                           static_cast<int>(workarea.height * 0.8));
            }
        }
    }
    gtk_container_add(GTK_CONTAINER(scroll), content);
    gtk_popover_set_child(GTK_POPOVER(popover), scroll);

    /*
     * A popover is not a descendant of the widget it is anchored to - GTK gives it to the window -
     * so the gtk_widget_show_all() that shows the toolbar never reaches this content, and
     * gtk_popover_popup() only raises the popover, not its child. A GTK3 widget that was never
     * shown has no size at all, so the popover would lay out an empty box of the minimum and GTK
     * would then draw its own frame around it (gtk_render_frame_gap asserts on a collapsed
     * allocation). Every other popover factory in the code base shows its content for this reason.
     */
    gtk_widget_show_all(scroll);

    /*
     * Shown first, state applied second: gtk_widget_show_all() is recursive, so it undoes the rows
     * the panel hides for a tool that does not have that property (an eraser has no colour and no
     * fill). The panel has the last word on what is visible.
     */
    panel->toolConfigChanged(this->adapter.getState());

    // The panel lives exactly as long as the popover that owns its widgets, so the popover talks to
    // it and not to this factory: a factory is only a builder and can be gone by the time the
    // popover is shown (the active tool summary builds its popovers from one that lives on the
    // stack).
    g_object_set_data_full(G_OBJECT(popover), "xoj-tool-property-panel", panel, destroyPanel);
    g_signal_connect(popover, "show", G_CALLBACK(ToolPropertyPanel::onPopoverShown), panel);

    /*
     * Plan 007, step 4: opening a tool's properties is the moment the tip is about, and this is the
     * one place every tool's properties are built - the toolbar button, the active tool summary and
     * the popover of a tool that is opened from a menu all come through here. It is connected to
     * "map" and not to "show", because a tip is anchored to the surface it is about and a widget
     * that is not on screen yet is nothing to anchor to.
     */
    g_signal_connect(popover, "map", G_CALLBACK(offerPropertiesTipOnce), this->parent);
    return popover;
}

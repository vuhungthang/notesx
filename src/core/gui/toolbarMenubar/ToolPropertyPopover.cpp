#include "ToolPropertyPopover.h"

#include "control/ToolConfigAdapter.h"  // for ToolConfigAdapter
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

auto ToolPropertyPopoverFactory::createPopover() const -> GtkWidget* {
    auto* panel =
            new ToolPropertyPanel(this->adapter, this->settings, this->provider, this->parent, this->presetsListener);

    GtkWidget* popover = gtk_popover_new();
    gtk_widget_add_css_class(popover, "toolbar");
    gtk_widget_add_css_class(popover, "xoj-tool-property-popover");
    GtkWidget* content = panel->createWidget();
    gtk_popover_set_child(GTK_POPOVER(popover), content);

    /*
     * A popover is not a descendant of the widget it is anchored to - GTK gives it to the window -
     * so the gtk_widget_show_all() that shows the toolbar never reaches this content, and
     * gtk_popover_popup() only raises the popover, not its child. A GTK3 widget that was never
     * shown has no size at all, so the popover would lay out an empty box of the minimum and GTK
     * would then draw its own frame around it (gtk_render_frame_gap asserts on a collapsed
     * allocation). Every other popover factory in the code base shows its content for this reason.
     */
    gtk_widget_show_all(content);

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
    return popover;
}

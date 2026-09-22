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
    gtk_popover_set_child(GTK_POPOVER(popover), panel->createWidget());

    // The panel lives exactly as long as the popover that owns its widgets, so the popover talks to
    // it and not to this factory: a factory is only a builder and can be gone by the time the
    // popover is shown (the active tool summary builds its popovers from one that lives on the
    // stack).
    g_object_set_data_full(G_OBJECT(popover), "xoj-tool-property-panel", panel, destroyPanel);
    g_signal_connect(popover, "show", G_CALLBACK(ToolPropertyPanel::onPopoverShown), panel);
    return popover;
}

#include "ActiveToolSummary.h"

#include <utility>  // for move

#include "control/ToolEnums.h"   // for ToolType
#include "gui/IconNameHelper.h"  // for IconNameHelper
#include "util/gtk4_helper.h"    // for gtk_box_append, gtk_widget_add_css_class
#include "util/i18n.h"           // for _, _F, FS

#include "ToolPropertyPopover.h"  // for ToolPropertyPopoverFactory

using xoj::toolbar::makeStrokePreview;
using xoj::toolbar::updateStrokePreview;

namespace {

/**
 * The name of a tool that has no property panel yet.
 *
 * The same user facing names the toolbar uses. Kept here rather than read out of ToolMenuHandler
 * because the summary is built before the toolbar items are.
 */
auto toolDisplayName(ToolType type) -> std::string {
    switch (type) {
        case TOOL_PEN:
            return _("Pen");
        case TOOL_ERASER:
            return _("Eraser");
        case TOOL_HIGHLIGHTER:
            return _("Highlighter");
        case TOOL_TEXT:
            return _("Text");
        case TOOL_IMAGE:
            return _("Image");
        case TOOL_SELECT_RECT:
            return _("Select Rectangle");
        case TOOL_SELECT_REGION:
            return _("Select Region");
        case TOOL_SELECT_OBJECT:
            return _("Select Object");
        case TOOL_VERTICAL_SPACE:
            return _("Vertical Space");
        case TOOL_HAND:
            return _("Hand");
        case TOOL_FLOATING_TOOLBOX:
            return _("Show Floating Toolbox");
        case TOOL_DRAW_RECT:
            return _("Draw Rectangle");
        case TOOL_DRAW_ELLIPSE:
            return _("Draw Ellipse");
        case TOOL_DRAW_ARROW:
            return _("Draw Arrow");
        case TOOL_DRAW_DOUBLE_ARROW:
            return _("Draw Double Arrow");
        case TOOL_DRAW_COORDINATE_SYSTEM:
            return _("Draw coordinate system");
        default:
            return std::string(toolTypeToString(type));
    }
}

}  // namespace

ActiveToolSummaryItem::ActiveToolSummaryItem(std::string id, ToolConfigAdapter& adapter,
                                             const ToolPropertyRegistry& registry, Settings& settings,
                                             GtkWindow* parent, PresetListListener* presetsListener,
                                             IconNameHelper& icons):
        AbstractToolItem(std::move(id), Category::TOOLS),
        adapter(adapter),
        registry(registry),
        settings(settings),
        parent(parent),
        presetsListener(presetsListener),
        icons(icons) {
    this->adapter.addObserver(this);
}

ActiveToolSummaryItem::~ActiveToolSummaryItem() {
    this->adapter.removeObserver(this);
    // The popovers own their panels, which unregister themselves here.
    this->popovers.clear();
}

auto ActiveToolSummaryItem::getToolDisplayName() const -> std::string { return _("Active tool properties"); }

auto ActiveToolSummaryItem::getNewToolIcon() const -> GtkWidget* {
    return gtk_image_new_from_icon_name(this->icons.iconName("tool-properties").c_str(), GTK_ICON_SIZE_LARGE_TOOLBAR);
}

auto ActiveToolSummaryItem::createItem(bool) -> xoj::util::WidgetSPtr {
    this->button = gtk_button_new();
    gtk_widget_add_css_class(this->button, "xoj-control");
    gtk_widget_add_css_class(this->button, "xoj-focus-ring");
    gtk_widget_add_css_class(this->button, "xoj-tool-summary");
    // The one toolbar control that does take the focus on a click: the popover it opens has to
    // give the focus back to it when it is closed with Escape.
    gtk_widget_set_focus_on_click(this->button, true);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    this->iconWidget =
            gtk_image_new_from_icon_name(this->icons.iconName("default").c_str(), GTK_ICON_SIZE_SMALL_TOOLBAR);
    gtk_box_append(GTK_BOX(box), this->iconWidget);

    this->preview = makeStrokePreview(Color(), 1.0, false, 28, 14);
    gtk_box_append(GTK_BOX(box), this->preview);

    this->nameLabel = gtk_label_new("");
    gtk_box_append(GTK_BOX(box), this->nameLabel);

    gtk_button_set_child(GTK_BUTTON(this->button), box);
    g_signal_connect(this->button, "clicked", G_CALLBACK(onButtonClicked), this);

    GtkToolItem* it = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(it), this->button);

    this->updateContents(this->adapter.getState());

    return xoj::util::WidgetSPtr(GTK_WIDGET(it), xoj::util::adopt);
}

void ActiveToolSummaryItem::onButtonClicked(GtkButton*, gpointer self) {
    auto* item = static_cast<ActiveToolSummaryItem*>(self);
    GtkWidget* popover = item->getPopoverFor(item->adapter.getState().toolType);
    if (popover != nullptr) {
        gtk_popover_popup(GTK_POPOVER(popover));
    }
}

auto ActiveToolSummaryItem::getPopoverFor(ToolType toolType) -> GtkWidget* {
    if (auto it = this->popovers.find(toolType); it != this->popovers.end()) {
        return it->second.get();
    }

    ToolPropertyProvider* provider = this->registry.find(toolType);
    if (provider == nullptr) {
        return nullptr;
    }

    // The factory is only a builder: the popover and its panel hold what they need, so the
    // factory can go away as soon as the popover exists.
    ToolPropertyPopoverFactory factory(this->adapter, this->settings, *provider, this->parent, this->presetsListener);
    GtkWidget* popover = factory.createPopover();
    gtk_popover_set_relative_to(GTK_POPOVER(popover), this->button);

    auto inserted = this->popovers.emplace(toolType, xoj::util::WidgetSPtr(popover, xoj::util::adopt));
    return inserted.first->second.get();
}

void ActiveToolSummaryItem::updateContents(const ToolConfigState& state) {
    if (this->button == nullptr) {
        return;
    }

    ToolPropertyProvider* provider = this->registry.find(state.toolType);
    const std::string name = provider != nullptr ? provider->getTitle() : toolDisplayName(state.toolType);

    gtk_label_set_text(GTK_LABEL(this->nameLabel), name.c_str());
    gtk_image_set_from_icon_name(
            GTK_IMAGE(this->iconWidget),
            provider != nullptr ? provider->getIconName().c_str() : this->icons.iconName("default").c_str(),
            GTK_ICON_SIZE_SMALL_TOOLBAR);

    updateStrokePreview(this->preview, state.hasColor ? state.color : Color(), state.thickness,
                        state.hasLineStyle && state.lineStyle != "plain");

    // The summary is the one place the active tool is named, so the name goes to the tooltip and
    // to the accessible name too.
    const std::string description =
            provider != nullptr ? FS(_F("{1} properties") % name) : FS(_F("{1} (no properties)") % name);
    gtk_widget_set_tooltip_text(this->button, description.c_str());
    atk_object_set_name(gtk_widget_get_accessible(this->button), description.c_str());

    // No panel, nothing to open: a control that does nothing when clicked would be a lie.
    gtk_widget_set_sensitive(this->button, provider != nullptr);
}

void ActiveToolSummaryItem::toolConfigChanged(const ToolConfigState& state) { this->updateContents(state); }

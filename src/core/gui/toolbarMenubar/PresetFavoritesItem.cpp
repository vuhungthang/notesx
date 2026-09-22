#include "PresetFavoritesItem.h"

#include <algorithm>  // for min
#include <utility>    // for move

#include "control/settings/Settings.h"  // for Settings
#include "gui/IconNameHelper.h"         // for IconNameHelper
#include "util/gtk4_helper.h"           // for gtk_box_append, gtk_widget_add_css_class
#include "util/i18n.h"                  // for _

namespace {

/// What a favourite button needs in order to apply its preset.
struct PresetButtonData {
    PresetFavoritesItem* item;
    std::string presetId;
};

void freePresetButtonData(gpointer data) { delete static_cast<PresetButtonData*>(data); }

}  // namespace

PresetFavoritesItem::PresetFavoritesItem(std::string id, ToolConfigAdapter& adapter, Settings& settings,
                                         IconNameHelper& icons):
        AbstractToolItem(std::move(id), Category::TOOLS), adapter(adapter), settings(settings), icons(icons) {
    this->adapter.addObserver(this);
}

PresetFavoritesItem::~PresetFavoritesItem() { this->adapter.removeObserver(this); }

auto PresetFavoritesItem::getToolDisplayName() const -> std::string { return _("Favourite presets"); }

auto PresetFavoritesItem::getNewToolIcon() const -> GtkWidget* {
    return gtk_image_new_from_icon_name(this->icons.iconName("star").c_str(), GTK_ICON_SIZE_LARGE_TOOLBAR);
}

auto PresetFavoritesItem::createItem(bool horizontal) -> xoj::util::WidgetSPtr {
    this->strip = gtk_box_new(horizontal ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(this->strip, "xoj-preset-strip");
    this->rebuildStrip();

    GtkToolItem* it = gtk_tool_item_new();
    gtk_container_add(GTK_CONTAINER(it), this->strip);
    return xoj::util::WidgetSPtr(GTK_WIDGET(it), xoj::util::adopt);
}

void PresetFavoritesItem::onPresetClicked(GtkButton*, gpointer data) {
    auto* buttonData = static_cast<PresetButtonData*>(data);
    const ToolPreset* preset = buttonData->item->settings.getToolPresets().findById(buttonData->presetId);
    if (preset == nullptr) {
        return;  // The preset went away between the click and the lookup.
    }
    buttonData->item->adapter.applyPreset(*preset);
}

void PresetFavoritesItem::rebuildStrip() {
    if (this->strip == nullptr || this->rebuilding) {
        return;
    }
    this->rebuilding = true;

    // GTK3 has no gtk_widget_get_first_child(), so the children are collected first.
    for (GList* children = gtk_container_get_children(GTK_CONTAINER(this->strip)); children != nullptr;
         children = children->next) {
        gtk_container_remove(GTK_CONTAINER(this->strip), GTK_WIDGET(children->data));
    }

    const std::vector<const ToolPreset*> favorites = this->settings.getToolPresets().getFavorites();
    // The profile decides how many favourites the toolbar shows; the rest stay in the popover.
    const std::size_t wanted =
            std::min(static_cast<std::size_t>(std::max(0, this->settings.getFavoritePresetCount())), favorites.size());

    if (wanted == 0) {
        GtkWidget* empty = gtk_label_new(_("No favourite presets"));
        gtk_widget_add_css_class(empty, "xoj-preset-strip-empty");
        gtk_box_append(GTK_BOX(this->strip), empty);
    }

    for (std::size_t i = 0; i < wanted; i++) {
        const ToolPreset* preset = favorites[i];

        GtkWidget* btn = gtk_button_new_with_label(preset->name.c_str());
        gtk_widget_add_css_class(btn, "xoj-control");
        gtk_widget_add_css_class(btn, "xoj-focus-ring");
        gtk_widget_add_css_class(btn, "xoj-preset-button");
        gtk_widget_set_tooltip_text(btn, preset->name.c_str());
        atk_object_set_name(gtk_widget_get_accessible(btn), preset->name.c_str());

        auto* data = new PresetButtonData{this, preset->id};
        g_object_set_data_full(G_OBJECT(btn), "xoj-preset-button-data", data, freePresetButtonData);
        g_signal_connect(btn, "clicked", G_CALLBACK(onPresetClicked), data);

        gtk_box_append(GTK_BOX(this->strip), btn);
    }

    this->rebuilding = false;
}

void PresetFavoritesItem::presetListChanged() { this->rebuildStrip(); }

void PresetFavoritesItem::toolConfigChanged(const ToolConfigState&) {
    // A preset that was just applied may have changed the favourite order or the names, and the
    // strip is cheap to rebuild.
    this->rebuildStrip();
}

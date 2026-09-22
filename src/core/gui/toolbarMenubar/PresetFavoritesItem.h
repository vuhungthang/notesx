/*
 * Xournal++
 *
 * The toolbar strip that shows the favourite presets
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string

#include <gtk/gtk.h>  // for GtkWidget

#include "control/ToolConfigAdapter.h"  // for ToolConfigAdapter, ToolConfigObserver, ToolConfigState
#include "gui/toolbarMenubar/AbstractToolItem.h"
#include "util/raii/GObjectSPtr.h"  // for WidgetSPtr

#include "ToolPropertyPanel.h"  // for PresetListListener

class IconNameHelper;
class Settings;

/**
 * Plan 003, step 5: the favourite preset strip.
 *
 * Shows as many favourites as the profile asks for, in the order the user chose, and applies a
 * preset when its button is clicked. The complete list, including the presets that are not
 * favourites, stays in the property popover.
 */
class PresetFavoritesItem final: public AbstractToolItem, public PresetListListener, public ToolConfigObserver {
public:
    PresetFavoritesItem(std::string id, ToolConfigAdapter& adapter, Settings& settings, IconNameHelper& icons);
    ~PresetFavoritesItem() override;

    PresetFavoritesItem(const PresetFavoritesItem&) = delete;
    PresetFavoritesItem& operator=(const PresetFavoritesItem&) = delete;

    void presetListChanged() override;
    void toolConfigChanged(const ToolConfigState& state) override;

    std::string getToolDisplayName() const override;
    GtkWidget* getNewToolIcon() const override;

protected:
    xoj::util::WidgetSPtr createItem(bool horizontal) override;

private:
    static void onPresetClicked(GtkButton* button, gpointer data);
    /// Rebuild the strip from the stored presets and the configured count.
    void rebuildStrip();

    ToolConfigAdapter& adapter;
    Settings& settings;
    IconNameHelper& icons;

    GtkWidget* strip = nullptr;
    /// Set while the strip is being rebuilt, so a change made by a button does not recurse.
    bool rebuilding = false;
};

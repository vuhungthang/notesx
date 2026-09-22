/*
 * Xournal++
 *
 * The one-level property panel of a tool
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>  // for function
#include <memory>      // for unique_ptr
#include <string>      // for string
#include <vector>      // for vector

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

#include "control/ToolConfigAdapter.h"  // for ToolConfigAdapter, ToolConfigObserver, ToolConfigState
#include "control/ToolPreset.h"         // for ToolPresetList
#include "util/raii/GVariantSPtr.h"     // for GVariantSPtr

#include "ToolPropertyProvider.h"  // for ToolPropertyProvider, ToolPropertyRows

class Settings;

/**
 * A representation that shows the stored presets and has to be rebuilt when they change.
 */
class PresetListListener {
public:
    virtual ~PresetListListener();

    /// The stored preset list changed. The listener re-reads it; no data is passed.
    virtual void presetListChanged() = 0;
};

/**
 * Plan 003, steps 3 and 5: the property panel of one tool.
 *
 * One level, no nested menus. The shared chrome - header, width, colour, fill and the preset
 * section - lives here; the tool specific rows come from a ToolPropertyProvider, so adding the
 * shape or selection panel later does not touch this class.
 *
 * The panel is a ToolConfigObserver, so a change made anywhere (a keyboard shortcut, a stylus
 * button, a legacy toolbar control, a preset) updates it.
 */
class ToolPropertyPanel final: public ToolConfigObserver {
public:
    ToolPropertyPanel(ToolConfigAdapter& adapter, Settings& settings, ToolPropertyProvider& provider, GtkWindow* parent,
                      PresetListListener* presetsListener);
    ~ToolPropertyPanel() override;

    ToolPropertyPanel(const ToolPropertyPanel&) = delete;
    ToolPropertyPanel& operator=(const ToolPropertyPanel&) = delete;

    /**
     * @brief Build the panel.
     * @return a floating widget, owned by the caller
     */
    GtkWidget* createWidget();

    /**
     * @brief Make this panel's tool the active one.
     *
     * The popover calls this when it is shown: the panel shows the configuration of one tool, so
     * opening it for a tool that is not active has to make that tool active. Otherwise its width
     * and colour controls would edit the tool in the user's hand and its preview would be a lie.
     */
    void selectOwnTool();

    /// The "show" handler of the popover that carries this panel.
    static void onPopoverShown(GtkWidget* popover, gpointer self);

    void toolConfigChanged(const ToolConfigState& state) override;

    /**
     * @brief The preset actions the rows trigger.
     *
     * Public so that the row callbacks can reach them; they are also what the GTK probe drives,
     * since a probe cannot click a button that lives inside a popover.
     */
    void applyStoredPreset(const std::string& presetId);
    void saveCurrentAsPreset();
    bool isPresetFavorite(const std::string& presetId) const;
    void setPresetFavorite(const std::string& presetId, bool favorite);
    void movePresetFavorite(const std::string& presetId, int delta);
    void renamePreset(const std::string& presetId);
    void removePreset(const std::string& presetId);
    void restoreExamplePresets();
    /// How many favourites the toolbar shows. The complete list stays in this panel.
    void setFavoritePresetCount(int count);

private:
    void buildHeader(GtkWidget* box);
    void buildSizeRow(GtkWidget* box, const ToolConfigState& state);
    void buildColorRow(GtkWidget* box);
    void buildFillRow(GtkWidget* box);
    void buildPresetSection(GtkWidget* box);
    /// Draw the swatch that shows the current stroke colour.
    static gboolean drawColorSwatch(GtkWidget* widget, cairo_t* cr, gpointer self);
    /// Rebuild the preset rows; called after any change to the stored list.
    void rebuildPresetRows();
    /// Apply a change to the stored list, persist it, and tell the listeners.
    void changePresets(const std::function<void(ToolPresetList&)>& change, const std::string& status);
    /// Show the result of the last preset action, for a reader that cannot see the list move.
    void setStatus(const std::string& text);
    /// Ask the user for a preset name.
    std::string promptForName(const std::string& title, const std::string& initial) const;
    /// A preset row: apply, favourite, reorder, rename, remove.
    GtkWidget* createPresetRow(const ToolPreset& preset, std::size_t favoriteIndex, bool isFavorite);

    ToolConfigAdapter& adapter;
    Settings& settings;
    ToolPropertyProvider& provider;
    GtkWindow* parent;
    PresetListListener* presetsListener;

    GtkWidget* strokePreview = nullptr;
    GtkWidget* sizeRow = nullptr;
    GtkWidget* colorRow = nullptr;
    GtkWidget* colorSwatch = nullptr;
    GtkWidget* fillRow = nullptr;
    GtkWidget* providerRows = nullptr;
    GtkWidget* presetRows = nullptr;
    GtkWidget* statusLabel = nullptr;

    /// The colour the swatch draws; kept here because the draw callback has no state.
    Color currentColor{};
    /// The width radio buttons, kept in step with win.tool-size.
    xoj::toolbar::ActionRadioGroup sizeGroup;
    /// The rows of the tool, or nullptr when the tool has none.
    std::unique_ptr<ToolPropertyRows> rows;
    /// Guard so that a rebuild triggered by a change does not recurse.
    bool rebuildingPresets = false;
};

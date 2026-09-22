/*
 * Xournal++
 *
 * Named tool presets: a stored tool configuration that can be applied again
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>   // for size_t
#include <map>       // for map
#include <optional>  // for optional
#include <string>    // for string
#include <string_view>
#include <vector>  // for vector

#include "control/ToolEnums.h"  // for ToolType, ToolSize, DrawingType, EraserType
#include "util/Color.h"         // for Color

/**
 * Plan 003: a named tool configuration the user can apply again.
 *
 * A preset refers to semantic tool settings, never to toolbar positions, so it keeps
 * working when the toolbar layout changes. Only the fields that apply to `toolType` are
 * present: an eraser preset carries no colour, a highlighter preset carries no drawing
 * type. This keeps "not applicable" and "explicitly set to the default value"
 * distinguishable, which matters when a preset is applied to a tool whose current value
 * differs from the default.
 */
struct ToolPreset {
    /// Stable identifier, never shown to the user and never handed out twice.
    std::string id;
    /// User-visible name, unique within the list the preset belongs to.
    std::string name;
    /// The tool the preset selects.
    ToolType toolType = TOOL_NONE;

    /// Stroke colour, alpha included.
    std::optional<Color> color;
    /// Stroke width.
    std::optional<ToolSize> size;
    /// Drawing type (line, rectangle, ...) for tools that have one.
    std::optional<DrawingType> drawingType;
    /// Fill opacity in [0, 255]; absent when the tool has no fill or fill is switched off.
    std::optional<int> fill;
    /// Eraser mode, for eraser presets.
    std::optional<EraserType> eraserType;

    /// Position in the favourite list; absent when the preset is not a favourite.
    std::optional<int> favoriteOrder;
    /// True for the examples seeded into a fresh profile, which can be restored later.
    bool builtin = false;

    /**
     * Whether the preset carries enough information to be applied at all.
     *
     * The id is deliberately not part of this: a preset captured from the current configuration
     * is valid before it is stored, and ToolPresetList::add() is what gives it an id. Storage
     * does require one, so ToolPresetList::fromStored() drops entries without it - they could not
     * be found, renamed or made a favourite afterwards.
     */
    bool isValid() const { return this->toolType != TOOL_NONE && !this->name.empty(); }

    /**
     * Flat key/value form used for persistence. Absent optionals are omitted, and reading
     * ignores keys it does not know, so a settings file written by a newer version still
     * loads and a file written by an older one simply lacks the newer fields.
     */
    std::map<std::string, std::string> toAttributes() const;
    static ToolPreset fromAttributes(const std::map<std::string, std::string>& attributes);

    bool operator==(const ToolPreset& other) const = default;
};

/**
 * An ordered collection of presets plus the favourite selection.
 *
 * Ordering, uniqueness of names and the favourite order are enforced here rather than by
 * each caller, so the property popover, the settings file and the toolbar all see the
 * same list.
 */
class ToolPresetList {
public:
    /// Number of favourites Focus shows directly; the rest stay in the popover.
    static constexpr std::size_t MAX_FAVORITES = 5U;
    /// Version of the persisted layout, so a later format change can be detected.
    static constexpr int STORAGE_VERSION = 1;

    const std::vector<ToolPreset>& getPresets() const { return this->presets; }
    bool empty() const { return this->presets.empty(); }

    const ToolPreset* findById(std::string_view id) const;
    const ToolPreset* findByName(std::string_view name) const;

    /// Every favourite, in display order.
    std::vector<const ToolPreset*> getFavorites() const;

    /**
     * Append a preset. The stored preset gets a fresh stable id and a unique variant of
     * its name, so two "Black fine pen" entries never become indistinguishable.
     *
     * @return the id of the stored preset
     */
    std::string add(ToolPreset preset);

    /// Rename a preset. The new name is made unique; an empty name is rejected.
    bool rename(std::string_view id, const std::string& newName);

    bool remove(std::string_view id);

    bool isFavorite(std::string_view id) const;
    /// Append to or remove from the favourites. Adding fails once MAX_FAVORITES is reached.
    bool setFavorite(std::string_view id, bool favorite);
    /// Move a favourite to `newIndex` in the favourite order.
    bool moveFavorite(std::string_view id, std::size_t newIndex);

    /**
     * Re-add the built-in examples that are missing. Presets the user created, renamed or
     * removed are left exactly as they are.
     */
    void restoreBuiltins();

    /// The examples a fresh profile starts with.
    static std::vector<ToolPreset> builtinPresets();
    /// A fresh list, containing the built-in examples and their favourites.
    static ToolPresetList seedDefaults();

    /**
     * Build a list from presets read out of a settings file.
     *
     * Incomplete entries are dropped, a repeated id keeps only its first occurrence, and the
     * favourite order is closed up and limited to MAX_FAVORITES. Reading never rejects the whole
     * file because of one bad entry.
     */
    static ToolPresetList fromStored(const std::vector<ToolPreset>& stored);

    bool operator==(const ToolPresetList& other) const = default;

private:
    std::string makeUniqueId() const;
    /// A name that no other preset uses; `ignoreId` is skipped so a rename can keep its name.
    std::string makeUniqueName(std::string_view wanted, std::string_view ignoreId = {}) const;
    /// Close gaps and duplicates in the favourite order without changing the relative order.
    void normalizeFavoriteOrder();
    ToolPreset* findMutable(std::string_view id);

    std::vector<ToolPreset> presets;
};

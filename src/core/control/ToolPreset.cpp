#include "ToolPreset.h"

#include <algorithm>  // for find_if, max_element, stable_sort
#include <cstdint>    // for uint32_t
#include <string_view>

#include <glib.h>  // for g_ascii_strtoull, g_strdup_printf

#include "util/i18n.h"  // for _ (translatable built-in preset names)

namespace {

constexpr auto ATTR_ID = "id";
constexpr auto ATTR_NAME = "name";
constexpr auto ATTR_TOOL = "tool";
constexpr auto ATTR_COLOR = "color";
constexpr auto ATTR_SIZE = "size";
constexpr auto ATTR_DRAWING_TYPE = "drawingType";
constexpr auto ATTR_FILL = "fill";
constexpr auto ATTR_ERASER_TYPE = "eraserType";
constexpr auto ATTR_LINE_STYLE = "lineStyle";
constexpr auto ATTR_FAVORITE = "favorite";
constexpr auto ATTR_BUILTIN = "builtin";

/// An attribute value, or nullopt when the key is missing or empty.
auto findValue(const std::map<std::string, std::string>& attributes, const char* key) -> std::optional<std::string> {
    auto it = attributes.find(key);
    if (it == attributes.end() || it->second.empty()) {
        return std::nullopt;
    }
    return it->second;
}

/**
 * Parse an enum attribute. An unrecognised value is treated as absent rather than as the
 * enum's fallback value, so a file written by a newer version does not silently turn an
 * unknown drawing type into "default". The round trip through the enum's own name table is
 * what rejects the unknown value.
 */
template <typename Enum, auto toString>
auto findEnum(const std::map<std::string, std::string>& attributes, const char* key, auto fromString)
        -> std::optional<Enum> {
    auto value = findValue(attributes, key);
    if (!value) {
        return std::nullopt;
    }
    Enum parsed = fromString(*value);
    if (std::string(toString(parsed)) != *value) {
        return std::nullopt;
    }
    return parsed;
}

auto findInt(const std::map<std::string, std::string>& attributes, const char* key, int min, int max)
        -> std::optional<int> {
    auto value = findValue(attributes, key);
    if (!value) {
        return std::nullopt;
    }
    char* end = nullptr;
    const long long parsed = g_ascii_strtoll(value->c_str(), &end, 10);
    if (end == value->c_str() || *end != '\0' || parsed < min || parsed > max) {
        return std::nullopt;
    }
    return static_cast<int>(parsed);
}

auto findColor(const std::map<std::string, std::string>& attributes, const char* key) -> std::optional<Color> {
    auto value = findValue(attributes, key);
    if (!value) {
        return std::nullopt;
    }
    char* end = nullptr;
    const unsigned long long parsed = g_ascii_strtoull(value->c_str(), &end, 16);
    if (end == value->c_str() || *end != '\0' || parsed > 0xffffffffULL) {
        return std::nullopt;
    }
    return Color(static_cast<uint32_t>(parsed));
}

auto colorToHex(Color color) -> std::string {
    char* text = g_strdup_printf("%08x", static_cast<uint32_t>(color));
    std::string result(text);
    g_free(text);
    return result;
}

}  // namespace

auto ToolPreset::toAttributes() const -> std::map<std::string, std::string> {
    std::map<std::string, std::string> attributes{
            {ATTR_ID, this->id}, {ATTR_NAME, this->name}, {ATTR_TOOL, std::string(toolTypeToString(this->toolType))}};

    if (this->color) {
        attributes[ATTR_COLOR] = colorToHex(*this->color);
    }
    if (this->size) {
        attributes[ATTR_SIZE] = std::string(toolSizeToString(*this->size));
    }
    if (this->drawingType) {
        attributes[ATTR_DRAWING_TYPE] = std::string(drawingTypeToString(*this->drawingType));
    }
    if (this->fill) {
        attributes[ATTR_FILL] = std::to_string(*this->fill);
    }
    if (this->eraserType) {
        attributes[ATTR_ERASER_TYPE] = std::string(eraserTypeToString(*this->eraserType));
    }
    if (this->lineStyle) {
        attributes[ATTR_LINE_STYLE] = *this->lineStyle;
    }
    if (this->favoriteOrder) {
        attributes[ATTR_FAVORITE] = std::to_string(*this->favoriteOrder);
    }
    if (this->builtin) {
        attributes[ATTR_BUILTIN] = "true";
    }

    return attributes;
}

auto ToolPreset::fromAttributes(const std::map<std::string, std::string>& attributes) -> ToolPreset {
    ToolPreset preset;

    if (auto value = findValue(attributes, ATTR_ID)) {
        preset.id = *value;
    }
    if (auto value = findValue(attributes, ATTR_NAME)) {
        preset.name = *value;
    }
    if (auto value = findValue(attributes, ATTR_TOOL)) {
        preset.toolType = toolTypeFromString(*value);
    }

    preset.color = findColor(attributes, ATTR_COLOR);
    preset.size = findEnum<ToolSize, toolSizeToString>(attributes, ATTR_SIZE, toolSizeFromString);
    preset.drawingType =
            findEnum<DrawingType, drawingTypeToString>(attributes, ATTR_DRAWING_TYPE, drawingTypeFromString);
    preset.fill = findInt(attributes, ATTR_FILL, 0, 255);
    preset.eraserType = findEnum<EraserType, eraserTypeToString>(attributes, ATTR_ERASER_TYPE, eraserTypeFromString);
    preset.lineStyle = findValue(attributes, ATTR_LINE_STYLE);
    preset.favoriteOrder = findInt(attributes, ATTR_FAVORITE, 0, 10000);

    if (auto value = findValue(attributes, ATTR_BUILTIN)) {
        preset.builtin = *value == "true";
    }

    return preset;
}

auto ToolPresetList::findById(std::string_view id) const -> const ToolPreset* {
    auto it = std::find_if(this->presets.begin(), this->presets.end(),
                           [id](const ToolPreset& p) { return std::string_view(p.id) == id; });
    return it == this->presets.end() ? nullptr : &*it;
}

auto ToolPresetList::findMutable(std::string_view id) -> ToolPreset* {
    auto it = std::find_if(this->presets.begin(), this->presets.end(),
                           [id](const ToolPreset& p) { return std::string_view(p.id) == id; });
    return it == this->presets.end() ? nullptr : &*it;
}

auto ToolPresetList::findByName(std::string_view name) const -> const ToolPreset* {
    auto it = std::find_if(this->presets.begin(), this->presets.end(),
                           [name](const ToolPreset& p) { return std::string_view(p.name) == name; });
    return it == this->presets.end() ? nullptr : &*it;
}

auto ToolPresetList::getFavorites() const -> std::vector<const ToolPreset*> {
    std::vector<const ToolPreset*> favorites;
    for (const ToolPreset& preset: this->presets) {
        if (preset.favoriteOrder) {
            favorites.emplace_back(&preset);
        }
    }

    std::stable_sort(favorites.begin(), favorites.end(),
                     [](const ToolPreset* a, const ToolPreset* b) { return *a->favoriteOrder < *b->favoriteOrder; });

    return favorites;
}

auto ToolPresetList::makeUniqueId() const -> std::string {
    // Ids are derived from the highest number in use rather than from a counter, so a list
    // that survived a restart never hands out an id it has already given away.
    std::size_t next = 1;
    constexpr std::string_view PREFIX = "preset-";
    for (const ToolPreset& preset: this->presets) {
        if (preset.id.rfind(PREFIX, 0) != 0) {
            continue;
        }
        const std::string suffix = preset.id.substr(PREFIX.size());
        char* end = nullptr;
        const unsigned long long number = g_ascii_strtoull(suffix.c_str(), &end, 10);
        if (end == suffix.c_str() || *end != '\0') {
            continue;
        }
        next = std::max(next, static_cast<std::size_t>(number) + 1);
    }
    return std::string(PREFIX) + std::to_string(next);
}

auto ToolPresetList::makeUniqueName(std::string_view wanted, std::string_view ignoreId) const -> std::string {
    auto taken = [this, ignoreId](const std::string& candidate) {
        return std::any_of(this->presets.begin(), this->presets.end(), [&candidate, ignoreId](const ToolPreset& p) {
            return std::string_view(p.id) != ignoreId && p.name == candidate;
        });
    };

    const std::string base(wanted);
    if (!taken(base)) {
        return base;
    }

    for (std::size_t suffix = 2;; suffix++) {
        std::string candidate = base + " (" + std::to_string(suffix) + ")";
        if (!taken(candidate)) {
            return candidate;
        }
    }
}

auto ToolPresetList::add(ToolPreset preset) -> std::string {
    preset.id = this->makeUniqueId();
    preset.name = this->makeUniqueName(preset.name);
    // A preset added from the current state never keeps a favourite slot by itself; the
    // caller decides whether it becomes a favourite.
    preset.favoriteOrder = std::nullopt;
    preset.builtin = false;

    const std::string id = preset.id;
    this->presets.emplace_back(std::move(preset));
    return id;
}

auto ToolPresetList::rename(std::string_view id, const std::string& newName) -> bool {
    ToolPreset* preset = this->findMutable(id);
    if (preset == nullptr || newName.empty()) {
        return false;
    }

    preset->name = this->makeUniqueName(newName, id);
    return true;
}

auto ToolPresetList::remove(std::string_view id) -> bool {
    auto it =
            std::find_if(this->presets.begin(), this->presets.end(), [id](const ToolPreset& p) { return p.id == id; });
    if (it == this->presets.end()) {
        return false;
    }

    this->presets.erase(it);
    this->normalizeFavoriteOrder();
    return true;
}

auto ToolPresetList::isFavorite(std::string_view id) const -> bool {
    const ToolPreset* preset = this->findById(id);
    return preset != nullptr && preset->favoriteOrder.has_value();
}

auto ToolPresetList::setFavorite(std::string_view id, bool favorite) -> bool {
    ToolPreset* preset = this->findMutable(id);
    if (preset == nullptr) {
        return false;
    }

    if (!favorite) {
        preset->favoriteOrder = std::nullopt;
        this->normalizeFavoriteOrder();
        return true;
    }

    if (preset->favoriteOrder) {
        return true;
    }
    if (this->getFavorites().size() >= MAX_FAVORITES) {
        return false;
    }

    std::size_t order = 0;
    for (const ToolPreset* other: this->getFavorites()) {
        order = std::max(order, static_cast<std::size_t>(*other->favoriteOrder) + 1);
    }
    preset->favoriteOrder = static_cast<int>(order);
    return true;
}

auto ToolPresetList::moveFavorite(std::string_view id, std::size_t newIndex) -> bool {
    std::vector<const ToolPreset*> favorites = this->getFavorites();

    auto it = std::find_if(favorites.begin(), favorites.end(), [id](const ToolPreset* p) { return p->id == id; });
    if (it == favorites.end()) {
        return false;
    }

    const ToolPreset* moved = *it;
    favorites.erase(it);
    favorites.insert(favorites.begin() + std::min(newIndex, favorites.size()), moved);

    for (std::size_t i = 0; i < favorites.size(); i++) {
        this->findMutable(favorites[i]->id)->favoriteOrder = static_cast<int>(i);
    }
    return true;
}

void ToolPresetList::normalizeFavoriteOrder() {
    const std::vector<const ToolPreset*> favorites = this->getFavorites();
    for (std::size_t i = 0; i < favorites.size(); i++) {
        // A file may carry more favourites than Focus can show; the surplus is dropped rather
        // than silently displayed.
        this->findMutable(favorites[i]->id)->favoriteOrder =
                i < MAX_FAVORITES ? std::make_optional(static_cast<int>(i)) : std::nullopt;
    }
}

auto ToolPresetList::fromStored(const std::vector<ToolPreset>& stored) -> ToolPresetList {
    ToolPresetList list;
    for (const ToolPreset& preset: stored) {
        // An entry without an id cannot be addressed afterwards, so it is dropped together with
        // the incomplete ones.
        if (!preset.isValid() || preset.id.empty()) {
            continue;
        }
        if (list.findById(preset.id) != nullptr) {
            continue;
        }
        list.presets.emplace_back(preset);
    }
    list.normalizeFavoriteOrder();
    return list;
}

void ToolPresetList::restoreBuiltins() {
    for (const ToolPreset& builtin: builtinPresets()) {
        if (this->findById(builtin.id) != nullptr) {
            continue;
        }
        this->presets.emplace_back(builtin);
    }
}

auto ToolPresetList::builtinPresets() -> std::vector<ToolPreset> {
    // The ids are part of the settings format: they are what lets "restore the examples"
    // find an example that is already there. Do not change them.
    std::vector<ToolPreset> builtins;

    builtins.emplace_back(ToolPreset{.id = "builtin-pen-fine-black",
                                     .name = _("Black fine pen"),
                                     .toolType = TOOL_PEN,
                                     .color = Colors::black,
                                     .size = TOOL_SIZE_FINE,
                                     .favoriteOrder = 0,
                                     .builtin = true});
    builtins.emplace_back(ToolPreset{.id = "builtin-pen-medium-red",
                                     .name = _("Red medium pen"),
                                     .toolType = TOOL_PEN,
                                     .color = Colors::red,
                                     .size = TOOL_SIZE_MEDIUM,
                                     .favoriteOrder = 1,
                                     .builtin = true});
    builtins.emplace_back(ToolPreset{.id = "builtin-highlighter-yellow",
                                     .name = _("Yellow highlighter"),
                                     .toolType = TOOL_HIGHLIGHTER,
                                     .color = Colors::yellow,
                                     .size = TOOL_SIZE_MEDIUM,
                                     .favoriteOrder = 2,
                                     .builtin = true});
    builtins.emplace_back(ToolPreset{.id = "builtin-eraser-standard",
                                     .name = _("Standard eraser"),
                                     .toolType = TOOL_ERASER,
                                     .size = TOOL_SIZE_MEDIUM,
                                     .eraserType = ERASER_TYPE_DEFAULT,
                                     .builtin = true});
    builtins.emplace_back(ToolPreset{.id = "builtin-eraser-delete-stroke",
                                     .name = _("Delete stroke eraser"),
                                     .toolType = TOOL_ERASER,
                                     .size = TOOL_SIZE_MEDIUM,
                                     .eraserType = ERASER_TYPE_DELETE_STROKE,
                                     .builtin = true});

    return builtins;
}

auto ToolPresetList::seedDefaults() -> ToolPresetList {
    ToolPresetList list;
    list.presets = builtinPresets();
    return list;
}

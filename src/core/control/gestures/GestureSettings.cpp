/*
 * Xournal++
 *
 * The gesture preferences: versioned, migrated, off by default (Plan 008, step 2)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "GestureSettings.h"

#include <algorithm>
#include <cstdlib>

namespace xoj::gesture {

namespace {
constexpr const char* KEY_QUICK_PALETTE = "quickPaletteEnabled";
constexpr const char* KEY_TAP_UNDO_REDO = "tapUndoRedoEnabled";
constexpr const char* KEY_CIRCLE = "circleToSelectEnabled";
constexpr const char* KEY_SCRIBBLE = "scribbleToEraseEnabled";
constexpr const char* KEY_CIRCLE_FLOOR = "circleConfidenceFloor";
constexpr const char* KEY_SCRIBBLE_FLOOR = "scribbleConfidenceFloor";
constexpr const char* KEY_FEEDBACK = "feedbackEnabled";

auto readBool(const std::map<std::string, std::string>& attributes, const char* key, bool fallback) -> bool {
    const auto it = attributes.find(key);
    if (it == attributes.end()) {
        return fallback;
    }
    return it->second == "true";
}

auto readDouble(const std::map<std::string, std::string>& attributes, const char* key, double fallback) -> double {
    const auto it = attributes.find(key);
    if (it == attributes.end()) {
        return fallback;
    }
    char* end = nullptr;
    const double parsed = std::strtod(it->second.c_str(), &end);
    if (end == it->second.c_str()) {
        return fallback;  // not a number: keep the conservative default rather than a garbage value
    }
    return std::clamp(parsed, 0.0, 1.0);
}
}  // namespace

auto GestureSettings::defaults() -> GestureSettings { return GestureSettings{}; }

auto GestureSettings::toAttributes() const -> std::map<std::string, std::string> {
    return {
            {KEY_QUICK_PALETTE, this->quickPaletteEnabled ? "true" : "false"},
            {KEY_TAP_UNDO_REDO, this->tapUndoRedoEnabled ? "true" : "false"},
            {KEY_CIRCLE, this->circleToSelectEnabled ? "true" : "false"},
            {KEY_SCRIBBLE, this->scribbleToEraseEnabled ? "true" : "false"},
            {KEY_CIRCLE_FLOOR, std::to_string(this->circleConfidenceFloor)},
            {KEY_SCRIBBLE_FLOOR, std::to_string(this->scribbleConfidenceFloor)},
            {KEY_FEEDBACK, this->feedbackEnabled ? "true" : "false"},
    };
}

auto GestureSettings::fromAttributes(const std::map<std::string, std::string>& attributes) -> GestureSettings {
    const GestureSettings defaults;
    GestureSettings settings;
    settings.quickPaletteEnabled = readBool(attributes, KEY_QUICK_PALETTE, defaults.quickPaletteEnabled);
    settings.tapUndoRedoEnabled = readBool(attributes, KEY_TAP_UNDO_REDO, defaults.tapUndoRedoEnabled);
    settings.circleToSelectEnabled = readBool(attributes, KEY_CIRCLE, defaults.circleToSelectEnabled);
    settings.scribbleToEraseEnabled = readBool(attributes, KEY_SCRIBBLE, defaults.scribbleToEraseEnabled);
    settings.circleConfidenceFloor = readDouble(attributes, KEY_CIRCLE_FLOOR, defaults.circleConfidenceFloor);
    settings.scribbleConfidenceFloor = readDouble(attributes, KEY_SCRIBBLE_FLOOR, defaults.scribbleConfidenceFloor);
    settings.feedbackEnabled = readBool(attributes, KEY_FEEDBACK, defaults.feedbackEnabled);
    return settings;
}

auto GestureSettings::migrated(int storedVersion, const std::map<std::string, std::string>& attributes)
        -> GestureSettings {
    if (storedVersion > STORAGE_VERSION) {
        // Written by a newer version: keep every conservative default rather than half-read a
        // format this build does not know. The dangerous settings are off, so this is safe.
        return defaults();
    }
    return fromAttributes(attributes);
}

auto gestureEnabled(const GestureSettings& settings, GestureKind kind) -> bool {
    switch (kind) {
        case GestureKind::Circle:
            return settings.circleToSelectEnabled;
        case GestureKind::Scribble:
            return settings.scribbleToEraseEnabled;
    }
    return false;
}

}  // namespace xoj::gesture

/*
 * Xournal++
 *
 * The gesture recognition boundary (Plan 008, step 3)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "GestureRecognizer.h"

namespace xoj::gesture {

auto gestureKindId(GestureKind kind) -> const char* {
    switch (kind) {
        case GestureKind::Circle:
            return "circle";
        case GestureKind::Scribble:
            return "scribble";
    }
    return "circle";
}

auto gestureKindFromId(const std::string& id) -> std::optional<GestureKind> {
    if (id == "circle") {
        return GestureKind::Circle;
    }
    if (id == "scribble") {
        return GestureKind::Scribble;
    }
    return std::nullopt;
}

}  // namespace xoj::gesture

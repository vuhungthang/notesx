/*
 * Xournal++
 *
 * The gesture reference (Plan 008, step 7)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "GestureReference.h"

#include "util/i18n.h"

namespace xoj::gesture {

namespace {
GestureReferenceRow rowFor(const GestureSettings& settings, GestureKind kind) {
    GestureReferenceRow row;
    row.gestureId = gestureKindId(kind);
    row.enabled = gestureEnabled(settings, kind);

    switch (kind) {
        case GestureKind::Circle:
            row.title = _("Circle to select");
            row.detail = _("Draw a large circle around what you want to select. It has to close, be round, and be "
                           "much bigger than a letter - ordinary loops are left as ink.");
            row.binding = settings.circleToSelectEnabled ?
                                  _("On. Watch for the preview before anything is selected; one Undo takes it back.") :
                                  _("Off. Turn it on in the gesture settings when you want it.");
            row.settingId = "circleToSelectEnabled";
            break;
        case GestureKind::Scribble:
            row.title = _("Scribble to erase");
            row.detail = _("Scrub densely back and forth over what you want to erase. Shading, hatching and a "
                           "single cross-out are left as ink.");
            row.binding = settings.scribbleToEraseEnabled ?
                                  _("On. Watch for the region preview before anything is erased; one Undo takes it "
                                    "back.") :
                                  _("Off. This gesture stays off until you turn it on yourself.");
            row.settingId = "scribbleToEraseEnabled";
            break;
    }
    return row;
}
}  // namespace

auto buildGestureReference(const GestureSettings& settings) -> std::vector<GestureReferenceRow> {
    std::vector<GestureReferenceRow> rows;
    rows.push_back(rowFor(settings, GestureKind::Circle));
    rows.push_back(rowFor(settings, GestureKind::Scribble));
    return rows;
}

auto gestureReferenceRow(const GestureSettings& settings, GestureKind kind) -> std::optional<GestureReferenceRow> {
    const std::string id = gestureKindId(kind);
    for (const GestureReferenceRow& row: buildGestureReference(settings)) {
        if (row.gestureId == id) {
            return row;
        }
    }
    return std::nullopt;
}

}  // namespace xoj::gesture

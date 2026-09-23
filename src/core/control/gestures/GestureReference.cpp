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
            /*
             * Plan 008, step 4 hit its stop condition: a selection is not a document edit in this
             * application, so the action cannot be one undo group, and the policy refuses it - the
             * circle is recognised and then always left as ink (see CircleGestureRecognizer and
             * GestureCommitGtkTest::circleToSelectIsRefusedForWantOfAnUndoGroup). The reference must
             * not offer a selection this build does not make.
             */
            row.available = false;
            row.detail = _("Draw a large circle around what you want to select. This build recognises the "
                           "circle, but a selection cannot yet be undone as one step, so the circle is left "
                           "as ordinary ink.");
            row.binding = _("Not available yet. The circle is recognised and then left as ink - nothing here "
                            "switches it on - until a selection can be taken back in a single step.");
            row.settingId = "circleToSelectEnabled";
            break;
        case GestureKind::Scribble:
            row.title = _("Scribble to erase");
            row.detail = _("Scrub densely back and forth over what you want to erase. Shading, hatching and a "
                           "single cross-out are left as ink.");
            row.binding = settings.scribbleToEraseEnabled ?
                                  _("On. Only a clear scribble is acted on, and only whole covered strokes go; one "
                                    "Undo brings them all back in a single step.") :
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

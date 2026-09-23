/*
 * Xournal++
 *
 * Where an overlay surface goes, as arithmetic (Plan 008, step 1)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include "QuickPalettePlacement.h"

#include <algorithm>
#include <cmath>

namespace xoj::gui {

namespace {
/// The largest coordinate the top-left corner may take on one axis without crossing the margin.
auto maxCoordinate(double viewport, double surface, double margin) -> double {
    const double limit = viewport - surface - margin;
    return std::max(limit, margin);  // a surface too large to fit is pinned at the margin
}

auto clampCoordinate(double value, double viewport, double surface, double margin) -> double {
    return std::clamp(value, margin, maxCoordinate(viewport, surface, margin));
}

bool covers(const SurfacePlacement& placement, double width, double height, double x, double y) {
    return x >= placement.x && x <= placement.x + width && y >= placement.y && y <= placement.y + height;
}
}  // namespace

auto placeSurface(const SurfacePlacementInput& input) -> SurfacePlacement {
    const double width = std::max(0.0, input.surfaceWidth);
    const double height = std::max(0.0, input.surfaceHeight);

    // Centred on the anchor horizontally, so the palette appears where the pen is, then clamped:
    // near a side edge the clamped result no longer covers the anchor, which is what we want.
    const double centredX = input.anchorX - width / 2.0;
    SurfacePlacement placement{clampCoordinate(centredX, input.viewportWidth, width, input.margin), input.margin};

    // Above the anchor by default: the hand is below the nib, so above is the least obstructed.
    // The Classic toolbox has always centred on the anchor instead, and keeps doing so.
    const double above = input.vertical == SurfaceAnchorVertical::Centered ? input.anchorY - height / 2.0 :
                                                                             input.anchorY - height - input.margin;
    const double below = input.anchorY + input.margin;
    const double preferred = (input.vertical == SurfaceAnchorVertical::Above && above < input.margin) ? below : above;

    placement.y = clampCoordinate(preferred, input.viewportHeight, height, input.margin);

    if (!input.avoidAnchor || covers(placement, width, height, input.anchorX, input.anchorY)) {
        // Try the other side of the anchor when the preferred one ended up over it - which happens
        // when the anchor is near an edge and the clamp pulled the surface back across the nib.
        const double alternativeAbove = clampCoordinate(above, input.viewportHeight, height, input.margin);
        const double alternativeBelow = clampCoordinate(below, input.viewportHeight, height, input.margin);

        if (!input.avoidAnchor) {
            return placement;
        }

        const bool aboveCovers =
                covers(SurfacePlacement{placement.x, alternativeAbove}, width, height, input.anchorX, input.anchorY);
        const bool belowCovers =
                covers(SurfacePlacement{placement.x, alternativeBelow}, width, height, input.anchorX, input.anchorY);

        if (!aboveCovers) {
            placement.y = alternativeAbove;
        } else if (!belowCovers) {
            placement.y = alternativeBelow;
        }
        // If both cover the anchor the viewport is simply too small for this surface; the margin
        // constraint above already won, and covering the anchor is the acceptable outcome.
    }

    return placement;
}

}  // namespace xoj::gui

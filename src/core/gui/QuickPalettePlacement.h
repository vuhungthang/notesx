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

#pragma once

namespace xoj::gui {

/// Which way the surface prefers to sit relative to the anchor.
enum class SurfaceAnchorVertical {
    /// Above the anchor, clear of the hand that summoned it. What the quick palette uses.
    Above,
    /// Centred on the anchor. What the Classic floating toolbox has always done, and still does.
    Centered,
};

/**
 * Plan 008: what the placement needs to know.
 *
 * All of it is numbers: the viewport the surface lives in, the size the surface wants, where the
 * pen is, and the two policies - how far to keep off the edges, and whether the surface may cover
 * the point the user is writing at.
 *
 * Nothing here knows about GTK. The floating toolbox and the quick palette both get their position
 * from placeSurface(); a unit test gets the same answer without a window, which is how edge
 * clamping and "does not cover the pen point" are tested rather than eyeballed.
 */
struct SurfacePlacementInput {
    /// The area the surface must stay inside, in the same coordinates as the anchor.
    double viewportWidth = 0.0;
    double viewportHeight = 0.0;
    /// The size the surface wants. The caller measures it; this does not guess.
    double surfaceWidth = 0.0;
    double surfaceHeight = 0.0;
    /// The pen point (or pointer) the surface is being summoned near.
    double anchorX = 0.0;
    double anchorY = 0.0;
    /// How far the surface keeps from the viewport edges.
    double margin = 10.0;
    /**
     * Whether the surface must avoid covering the anchor.
     *
     * True for a palette summoned by the stylus, where covering the nib hides what the user is
     * about to write. When it can be honoured the result does not contain the anchor; when the
     * surface is too large for the viewport for that to be possible, the edges win - the surface
     * stays inside the viewport and the anchor may be covered, because a surface pushed off screen
     * is unusable.
     */
    bool avoidAnchor = true;
    /// Which side of the anchor the surface prefers.
    SurfaceAnchorVertical vertical = SurfaceAnchorVertical::Above;
};

/// The top-left corner of the surface, in viewport coordinates.
struct SurfacePlacement {
    double x = 0.0;
    double y = 0.0;
};

/**
 * Plan 008: place a surface near the anchor, inside the viewport.
 *
 * The surface is centred on the anchor horizontally and put above it, where there is room, so the
 * hand that summoned it does not sit on top of it. When there is no room above, it goes below;
 * when there is room for neither, it is clamped to the viewport and the anchor is allowed to be
 * covered. The result always lies within [margin, viewport - size - margin], and when the surface
 * is larger than the viewport allows, it is pinned at the margin rather than pushed off screen.
 */
auto placeSurface(const SurfacePlacementInput& input) -> SurfacePlacement;

}  // namespace xoj::gui

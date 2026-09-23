/*
 * Xournal++
 *
 * Plan 008 step 1: where a summoned surface goes, tested without a window
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <gtest/gtest.h>

#include "gui/QuickPalettePlacement.h"

using namespace xoj::gui;

namespace {
SurfacePlacementInput input(double anchorX, double anchorY) {
    SurfacePlacementInput in;
    in.viewportWidth = 1000.0;
    in.viewportHeight = 800.0;
    in.surfaceWidth = 200.0;
    in.surfaceHeight = 100.0;
    in.anchorX = anchorX;
    in.anchorY = anchorY;
    in.margin = 10.0;
    return in;
}

constexpr double MARGIN = 10.0;
}  // namespace

TEST(QuickPalettePlacementTest, sitsAboveTheAnchorAndCentredOnIt) {
    const SurfacePlacement placement = placeSurface(input(500.0, 400.0));
    EXPECT_DOUBLE_EQ(placement.x, 400.0);
    EXPECT_DOUBLE_EQ(placement.y, 290.0);
}

TEST(QuickPalettePlacementTest, doesNotCoverThePenPoint) {
    const SurfacePlacement placement = placeSurface(input(500.0, 400.0));
    const bool coversPoint = 500.0 >= placement.x && 500.0 <= placement.x + 200.0 && 400.0 >= placement.y &&
                             400.0 <= placement.y + 100.0;
    EXPECT_FALSE(coversPoint) << "the palette must not sit on the nib";
}

TEST(QuickPalettePlacementTest, clampsInsideEveryEdge) {
    SurfacePlacementInput in = input(0.0, 0.0);
    in.anchorX = 0.0;
    in.anchorY = 0.0;
    const SurfacePlacement topLeft = placeSurface(in);
    EXPECT_GE(topLeft.x, MARGIN);
    EXPECT_GE(topLeft.y, MARGIN);

    in.anchorX = in.viewportWidth;
    in.anchorY = in.viewportHeight;
    const SurfacePlacement bottomRight = placeSurface(in);
    EXPECT_LE(bottomRight.x, in.viewportWidth - in.surfaceWidth - MARGIN);
    EXPECT_LE(bottomRight.y, in.viewportHeight - in.surfaceHeight - MARGIN);
}

TEST(QuickPalettePlacementTest, goesBelowTheAnchorWhenThereIsNoRoomAbove) {
    SurfacePlacementInput in = input(500.0, 50.0);
    const SurfacePlacement placement = placeSurface(in);
    EXPECT_GE(placement.y, 50.0) << "no room above: the surface goes below the anchor";
}

TEST(QuickPalettePlacementTest, aSurfaceTooLargeForTheViewportStaysAtTheMargin) {
    SurfacePlacementInput in = input(500.0, 400.0);
    in.viewportWidth = 100.0;
    in.viewportHeight = 100.0;
    in.surfaceWidth = 400.0;
    in.surfaceHeight = 300.0;

    const SurfacePlacement placement = placeSurface(in);
    // Pinned at the margin rather than pushed off screen, even though it cannot fit.
    EXPECT_DOUBLE_EQ(placement.x, MARGIN);
    EXPECT_DOUBLE_EQ(placement.y, MARGIN);
}

TEST(QuickPalettePlacementTest, theAnchorMayBeCoveredOnlyWhenTheSurfaceCannotAvoidIt) {
    SurfacePlacementInput in = input(500.0, 400.0);
    in.surfaceHeight = 900.0;  // taller than the viewport

    const SurfacePlacement placement = placeSurface(in);
    EXPECT_DOUBLE_EQ(placement.y, MARGIN);
    // The interesting property is that it is still inside the viewport at all.
    EXPECT_GE(placement.y, MARGIN);
}

TEST(QuickPalettePlacementTest, thePenClearanceCanBeTurnedOff) {
    SurfacePlacementInput in = input(500.0, 400.0);
    in.avoidAnchor = false;
    const SurfacePlacement placement = placeSurface(in);
    EXPECT_DOUBLE_EQ(placement.y, 290.0);  // unchanged: the preferred side is used as-is
}

TEST(QuickPalettePlacementTest, theClassicToolboxKeepsCentringOnThePoint) {
    // The Classic floating toolbox has always centred on the point and clamped to the viewport;
    // it must keep doing exactly that through the shared placement.
    SurfacePlacementInput in = input(500.0, 400.0);
    in.avoidAnchor = false;
    in.vertical = SurfaceAnchorVertical::Centered;

    const SurfacePlacement placement = placeSurface(in);
    EXPECT_DOUBLE_EQ(placement.x, 400.0);  // 500 - 200/2
    EXPECT_DOUBLE_EQ(placement.y, 350.0);  // 400 - 100/2
}

TEST(QuickPalettePlacementTest, isStableForTheSameInput) {
    const SurfacePlacement first = placeSurface(input(321.0, 654.0));
    const SurfacePlacement second = placeSurface(input(321.0, 654.0));
    EXPECT_DOUBLE_EQ(first.x, second.x);
    EXPECT_DOUBLE_EQ(first.y, second.y);
}

TEST(QuickPalettePlacementTest, staysInsideForAMatrixOfAnchors) {
    for (double x = 0.0; x <= 1000.0; x += 111.0) {
        for (double y = 0.0; y <= 800.0; y += 97.0) {
            const SurfacePlacement placement = placeSurface(input(x, y));
            EXPECT_GE(placement.x, MARGIN) << "anchor " << x << "," << y;
            EXPECT_GE(placement.y, MARGIN) << "anchor " << x << "," << y;
            EXPECT_LE(placement.x, 1000.0 - 200.0 - MARGIN) << "anchor " << x << "," << y;
            EXPECT_LE(placement.y, 800.0 - 100.0 - MARGIN) << "anchor " << x << "," << y;
        }
    }
}

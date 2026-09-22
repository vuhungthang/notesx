/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <cstddef>
#include <string>

#include <gtest/gtest.h>

#include "gui/sidebar/previews/page/PageCardLabels.h"

/*
 * Plan 005, step 3: a page card says what page it is and what state it is in - as text, not only
 * as colour. The card's accessible name is what a screen reader reads, and the metadata line is
 * what list mode shows, so both are checked here without a widget in the way.
 */

namespace {

using xoj::pagecard::buildAccessibleName;
using xoj::pagecard::buildMetadata;

constexpr bool SELECTED = true;
constexpr bool UNSELECTED = false;
constexpr bool CURRENT = true;
constexpr bool NOT_CURRENT = false;
constexpr bool LOADING = true;
constexpr bool LOADED = false;
constexpr bool ERROR = true;
constexpr bool NO_ERROR = false;

}  // namespace

TEST(PageCardLabels, APageNamesItsNumber) {
    EXPECT_EQ(buildAccessibleName(12, UNSELECTED, NOT_CURRENT, LOADED, NO_ERROR), "Page 12");
}

TEST(PageCardLabels, TheSelectionAndTheCurrentPageAreNamed) {
    EXPECT_EQ(buildAccessibleName(12, SELECTED, NOT_CURRENT, LOADED, NO_ERROR), "Page 12, selected");
    EXPECT_EQ(buildAccessibleName(12, UNSELECTED, CURRENT, LOADED, NO_ERROR), "Page 12, current page");

    // The name the plan asks for, for a selected card that is also the page the editor shows.
    EXPECT_EQ(buildAccessibleName(12, SELECTED, CURRENT, LOADED, NO_ERROR), "Page 12, selected, current page");
}

TEST(PageCardLabels, AThumbnailThatIsNotReadyYetSaysSo) {
    EXPECT_EQ(buildAccessibleName(3, UNSELECTED, NOT_CURRENT, LOADING, NO_ERROR), "Page 3, loading");
}

TEST(PageCardLabels, AFailedThumbnailIsNotReportedAsLoading) {
    const std::string name = buildAccessibleName(3, SELECTED, CURRENT, LOADING, ERROR);

    EXPECT_EQ(name, "Page 3, selected, current page, preview unavailable");
    EXPECT_EQ(name.find("loading"), std::string::npos) << "a render that failed is not still running";
}

TEST(PageCardLabels, EveryStateReadsDifferently) {
    // The four states a card can be in must not read the same, or the name says nothing.
    const std::string plain = buildAccessibleName(7, UNSELECTED, NOT_CURRENT, LOADED, NO_ERROR);
    const std::string selected = buildAccessibleName(7, SELECTED, NOT_CURRENT, LOADED, NO_ERROR);
    const std::string current = buildAccessibleName(7, UNSELECTED, CURRENT, LOADED, NO_ERROR);
    const std::string loading = buildAccessibleName(7, UNSELECTED, NOT_CURRENT, LOADING, NO_ERROR);
    const std::string error = buildAccessibleName(7, UNSELECTED, NOT_CURRENT, LOADED, ERROR);

    for (const std::string& other: {selected, current, loading, error}) {
        EXPECT_NE(plain, other);
    }
    EXPECT_NE(loading, error);
}

TEST(PageCardLabels, MetadataIsThePageNumber) {
    EXPECT_EQ(buildMetadata(12, "", NO_ERROR), "Page 12");
}

TEST(PageCardLabels, MetadataCarriesThePageNameWhenItSaysMore) {
    EXPECT_EQ(buildMetadata(12, "Chapter 3", NO_ERROR), "Page 12 \u00b7 Chapter 3");
}

TEST(PageCardLabels, MetadataIgnoresANameThatOnlyRepeatsTheNumber) {
    // A PDF page label that is just the page number adds nothing the line does not already say.
    EXPECT_EQ(buildMetadata(12, "12", NO_ERROR), "Page 12");
}

TEST(PageCardLabels, MetadataReportsAFailedThumbnail) {
    EXPECT_EQ(buildMetadata(12, "Chapter 3", ERROR), "Page 12 \u00b7 Chapter 3 \u00b7 preview unavailable");
}

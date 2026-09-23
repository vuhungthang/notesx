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

#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "dashboard/DashboardTypes.h"
#include "gui/dashboard/DashboardCardLabels.h"

/*
 * Plan 006, step 4: a dashboard card says what it is and what is wrong with it - as text, not only
 * as an icon or a colour. The card's accessible name is what a screen reader reads and the metadata
 * line is what the card shows, so both are checked here without a widget in the way: a missing
 * file, one the editor cannot open, one that cannot be read, a document without a preview and a
 * corrupt one all have to say so, because that is the whole of what a user can do about them.
 */

namespace {

using xoj::dashboard::DashboardSection;
using xoj::dashboard::DocumentCard;
using xoj::dashboard::RecoveryCard;
using xoj::dashboardcard::buildAccessibleName;
using xoj::dashboardcard::buildMetadata;
using xoj::dashboardcard::buildRecoveryAccessibleName;
using xoj::dashboardcard::buildRecoveryMetadata;
using xoj::dashboardcard::buildTitle;

const auto NOW = std::chrono::system_clock::time_point{std::chrono::seconds(1'800'000'000)};

auto secondsAgo(int seconds) -> std::chrono::system_clock::time_point { return NOW - std::chrono::seconds(seconds); }

auto document(const char* path) -> DocumentCard {
    DocumentCard card;
    card.path = path;
    card.displayName = card.path.filename().string();
    card.type = DocumentCard::Type::Xournal;
    card.location = DocumentCard::Location::Present;
    return card;
}

auto contains(const std::string& text, const char* needle) -> bool { return text.find(needle) != std::string::npos; }

}  // namespace

TEST(DashboardCardLabels, aCardIsNamedByItsFileWithoutTheExtension) {
    DocumentCard card = document("/home/user/Notes/chapter 1.xopp");

    EXPECT_EQ(buildTitle(card), "chapter 1");
    EXPECT_TRUE(contains(buildAccessibleName(card, NOW), "chapter 1.xopp"))
            << "the announced name is the file, so it matches what a file manager shows";
}

TEST(DashboardCardLabels, aCardSaysWhenTheFileChanged) {
    DocumentCard card = document("/notes/notes.xopp");
    card.modifiedTime = secondsAgo(2 * 60 * 60);

    EXPECT_EQ(buildAccessibleName(card, NOW), "notes.xopp, modified 2 hours ago");
    EXPECT_EQ(buildMetadata(card, NOW), "/notes \u00b7 modified 2 hours ago");

    card.modifiedTime = secondsAgo(30);
    EXPECT_TRUE(contains(buildAccessibleName(card, NOW), "just now"));
}

TEST(DashboardCardLabels, anOldFileIsDatedRatherThanCounted) {
    DocumentCard card = document("/notes/notes.xopp");
    card.modifiedTime = NOW - std::chrono::hours(24 * 30);

    const std::string name = buildAccessibleName(card, NOW);
    EXPECT_TRUE(contains(name, "notes.xopp"));
    EXPECT_FALSE(contains(name, "days ago")) << "a month is a date, not an interval";
}

TEST(DashboardCardLabels, aMissingFileSaysSoAndOffersNoPreview) {
    DocumentCard card = document("/notes/gone.xopp");
    card.location = DocumentCard::Location::Missing;
    card.preview = DocumentCard::Preview::Corrupt;

    const std::string name = buildAccessibleName(card, NOW);
    EXPECT_TRUE(contains(name, "the file is not there any more"));
    EXPECT_EQ(buildMetadata(card, NOW), "the file is not there any more")
            << "the state replaces the folder line rather than sitting beside it";
}

TEST(DashboardCardLabels, anUnreadableFileSaysSo) {
    DocumentCard card = document("/notes/locked.xopp");
    card.location = DocumentCard::Location::Unreadable;

    EXPECT_TRUE(contains(buildAccessibleName(card, NOW), "the file cannot be read"));
    EXPECT_TRUE(contains(buildMetadata(card, NOW), "the file cannot be read"));
}

TEST(DashboardCardLabels, aFileTheEditorCannotOpenSaysSo) {
    DocumentCard card = document("/notes/photo.png");
    card.type = DocumentCard::Type::Unsupported;

    EXPECT_TRUE(contains(buildAccessibleName(card, NOW), "the editor cannot open this kind of file"));
    EXPECT_TRUE(contains(buildMetadata(card, NOW), "the editor cannot open this kind of file"));
}

TEST(DashboardCardLabels, aPinnedCardSaysThatItIsPinnedAndAPdfIsAPdf) {
    DocumentCard card = document("/notes/paper.pdf");
    card.type = DocumentCard::Type::Pdf;
    card.pinned = true;
    card.preview = DocumentCard::Preview::None;

    const std::string name = buildAccessibleName(card, NOW);
    EXPECT_TRUE(contains(name, "pinned"));
    EXPECT_TRUE(contains(name, "PDF"));
    EXPECT_TRUE(contains(name, "no preview"));
}

TEST(DashboardCardLabels, aPreviewThatIsThereIsNotAnnounced) {
    DocumentCard card = document("/notes/notes.xopp");
    card.preview = DocumentCard::Preview::Available;

    EXPECT_EQ(buildAccessibleName(card, NOW), "notes.xopp") << "a card that is exactly what it looks like adds nothing";

    card.preview = DocumentCard::Preview::Corrupt;
    EXPECT_TRUE(contains(buildAccessibleName(card, NOW), "preview unavailable"));
}

TEST(DashboardCardLabels, aRecoveryCardSaysWhereTheCopyCameFrom) {
    RecoveryCard card;
    card.recoveryPath = "/notes/.notes.autosave.xopp";
    card.originalPath = "/notes/notes.xopp";
    card.originalExists = true;
    card.newerThanOriginal = true;
    card.recoveryTime = secondsAgo(10 * 60);
    card.displayName = ".notes.autosave.xopp";
    card.validation = xoj::safety::RecoveryValidation::Ok;

    const std::string name = buildRecoveryAccessibleName(card, NOW);
    EXPECT_TRUE(contains(name, "Recovered copy of notes.xopp"));
    EXPECT_TRUE(contains(name, "10 minutes ago"));

    EXPECT_TRUE(contains(buildRecoveryMetadata(card, NOW), "newer than the file it came from"));
    EXPECT_FALSE(contains(name, "cannot be opened")) << "a readable copy offers to be opened";
}

TEST(DashboardCardLabels, aRecoveryCardThatCannotBeOpenedSaysWhy) {
    RecoveryCard card;
    card.recoveryPath = "/notes/.notes.autosave.xopp";
    card.displayName = ".notes.autosave.xopp";
    card.validation = xoj::safety::RecoveryValidation::UnrecognizedFormat;
    card.error = "the file does not start like a Xournal++ document";

    const std::string name = buildRecoveryAccessibleName(card, NOW);
    EXPECT_TRUE(contains(name, "cannot be opened"));
    EXPECT_EQ(buildRecoveryMetadata(card, NOW), "the file does not start like a Xournal++ document");
}

TEST(DashboardCardLabels, anUnnamedRecoveryCopyIsStillDescribed) {
    RecoveryCard card;
    card.recoveryPath = "/cache/autosaves/1234.xopp";
    card.displayName = "1234.xopp";

    EXPECT_TRUE(contains(buildRecoveryAccessibleName(card, NOW), "Unsaved work"));
}

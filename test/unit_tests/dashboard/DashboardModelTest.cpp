/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * Plan 006, step 1: the dashboard data models.
 *
 * The dashboard is an index over file paths, not a document database: everything here reads
 * metadata (and, for a library folder, the names in a directory) and nothing moves, renames or
 * rewrites a user file. These tests pin that down: what a card says about a path that is there,
 * gone, unreadable or of a kind the editor cannot open, that the same file reached through a
 * symlink, a pin and a folder is one card carrying all three memberships, and that the sections
 * come out in the order the dashboard shows them.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <unistd.h>  // for geteuid

#include "control/RecoveryInventory.h"
#include "dashboard/DashboardModel.h"

#include "filesystem.h"

using namespace xoj::dashboard;

namespace {

/// A fresh, empty directory to build one test's files in.
auto freshDir(const char* name) -> fs::path {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

/// Puts a folder's permissions back however the test ends: a failed assertion in the middle of a
/// permission test must not leave a tree the next run cannot even clean up.
struct PermissionsGuard {
    fs::path path;
    fs::perms perms;

    ~PermissionsGuard() {
        std::error_code error;
        fs::permissions(path, perms, error);
    }
};

/// A document of `bytes` bytes, last written `minutesAgo` minutes ago.
auto writeDocument(const fs::path& path, std::size_t bytes = 64, int minutesAgo = 0) -> fs::path {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << "PK\x03\x04";
    out << std::string(bytes > 4 ? bytes - 4 : 0, 'x');
    out.close();

    if (minutesAgo > 0) {
        std::error_code error;
        const auto time = fs::file_time_type::clock::now() - std::chrono::minutes(minutesAgo);
        fs::last_write_time(path, time, error);
    }
    return path;
}

auto cardFor(const DashboardModel& model, const fs::path& path) -> const DocumentCard* {
    for (const DocumentCard& card: model.getCards()) {
        if (card.path == path || card.path == DashboardModel::canonicalize(path)) {
            return &card;
        }
    }
    return nullptr;
}

auto namesIn(const std::vector<DocumentCard>& cards) -> std::string {
    std::string out;
    for (const DocumentCard& card: cards) {
        out += card.displayName;
        out += " ";
    }
    return out;
}

/// Sections are compared by name so a failure reads as the order the user sees.
auto sectionNames(const std::vector<DashboardSection>& sections) -> std::string {
    std::string out;
    for (DashboardSection section: sections) {
        out += sectionKey(section);
        out += " ";
    }
    return out;
}

}  // namespace

TEST(DashboardModel, oneCardForTheSameFileReachedFourWays) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardDedupe");
    const fs::path library = dir / "library";
    fs::create_directories(library);
    const fs::path document = writeDocument(library / "notes.xopp");
    const fs::path link = library / "link-to-notes.xopp";
    std::error_code error;
    fs::create_symlink(document, link, error);
    ASSERT_FALSE(error) << "the test needs a symlink";

    DashboardModel model;
    // The same file four times over: by its real name, through a symlink, through a path that is
    // not in its normal form, and as a listing of the folder it sits in.
    model.setRecentFiles({link, library / "." / "notes.xopp"});
    model.setPinnedFiles({document, link});
    model.setLibraryFolders({LibraryFolder{library, "library", true, false, LibraryFolder::State::Ok}});
    model.refresh();

    ASSERT_EQ(model.getCards().size(), 1U) << namesIn(model.getCards());
    const DocumentCard* card = cardFor(model, document);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->path, DashboardModel::canonicalize(document));
    EXPECT_EQ(card->displayName, "notes.xopp");
    EXPECT_TRUE(card->has(DashboardSection::ContinueWorking));
    EXPECT_TRUE(card->has(DashboardSection::Pinned));
    EXPECT_TRUE(card->has(DashboardSection::Library));
    EXPECT_EQ(card->location, DocumentCard::Location::Present);
    EXPECT_TRUE(card->openable());

    // Every spelling of the path leads back to that one card.
    EXPECT_EQ(cardFor(model, link), card);
    EXPECT_EQ(cardFor(model, library / "." / "notes.xopp"), card);
    EXPECT_EQ(model.findCard(link), card);

    fs::remove_all(dir);
}

TEST(DashboardModel, sectionsKeepTheirOrderAndTheirOwnOrderingWithin) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardOrder");
    const fs::path newest = writeDocument(dir / "newest.xopp", 64, 1);
    const fs::path middle = writeDocument(dir / "middle.xopp", 64, 10);
    const fs::path oldest = writeDocument(dir / "oldest.xopp", 64, 60);

    DashboardModel model;
    // Handed over oldest first: the model orders the section by modification time itself, so a
    // static recent list from the platform cannot decide what "continue working" shows first.
    model.setRecentFiles({oldest, middle, newest});
    model.setPinnedFiles({oldest, newest});
    model.refresh();

    EXPECT_EQ(namesIn(model.cardsIn(DashboardSection::ContinueWorking)), "newest.xopp middle.xopp oldest.xopp ");
    // Pinned keeps the order the user arranged, not the modification time.
    EXPECT_EQ(namesIn(model.cardsIn(DashboardSection::Pinned)), "oldest.xopp newest.xopp ");
    EXPECT_EQ(sectionNames(model.sections()), "continue-working pinned library templates ");

    fs::remove_all(dir);
}

TEST(DashboardModel, aFileThatIsGoneStaysListedAsMissing) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardMissing");
    const fs::path gone = dir / "deleted.xopp";
    const fs::path renamedAway = writeDocument(dir / "renamed.xopp");
    fs::rename(renamedAway, dir / "elsewhere.xopp");

    DashboardModel model;
    model.setRecentFiles({gone, renamedAway});
    model.setPinnedFiles({gone});
    model.refresh();

    ASSERT_EQ(model.getCards().size(), 2U) << namesIn(model.getCards());
    const DocumentCard* card = cardFor(model, gone);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->location, DocumentCard::Location::Missing);
    EXPECT_FALSE(card->openable());
    EXPECT_FALSE(card->modifiedTime.has_value());
    EXPECT_TRUE(card->has(DashboardSection::Pinned)) << "a missing pinned file is still a pin to remove";

    // A rename leaves the card pointing at the path the user had, not at the new one.
    const DocumentCard* moved = cardFor(model, renamedAway);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->location, DocumentCard::Location::Missing);
    EXPECT_EQ(cardFor(model, dir / "elsewhere.xopp"), nullptr);

    fs::remove_all(dir);
}

TEST(DashboardModel, unsupportedFilesAreListedOnlyWhenTheUserPinnedThem) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardUnsupported");
    const fs::path text = dir / "notes.txt";
    {
        std::ofstream out(text);
        out << "not a document\n";
    }
    const fs::path document = writeDocument(dir / "notes.xopp");

    DashboardModel model;
    model.setPinnedFiles({text});
    model.setLibraryFolders({LibraryFolder{dir, "dir", true, false, LibraryFolder::State::Ok}});
    model.refresh();

    const DocumentCard* card = cardFor(model, text);
    ASSERT_NE(card, nullptr);
    EXPECT_EQ(card->type, DocumentCard::Type::Unsupported);
    EXPECT_FALSE(card->openable());
    EXPECT_TRUE(card->has(DashboardSection::Pinned));
    EXPECT_FALSE(card->has(DashboardSection::Library)) << "a folder listing ignores what the editor cannot open";
    ASSERT_NE(cardFor(model, document), nullptr);

    fs::remove_all(dir);
}

TEST(DashboardModel, autosaveAndTemporaryFilesAreNotDocuments) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardTemporary");
    const fs::path document = writeDocument(dir / "notes.xopp");
    const fs::path autosave = writeDocument(dir / ".notes.autosave.xopp");
    const fs::path swap = writeDocument(dir / "notes.xopp.swap");
    const fs::path temporary = writeDocument(dir / "notes.xopp~");

    DashboardModel model;
    model.setRecentFiles({autosave, swap, temporary, document});
    model.setPinnedFiles({autosave});
    model.setLibraryFolders({LibraryFolder{dir, "dir", true, false, LibraryFolder::State::Ok}});
    model.refresh();

    ASSERT_EQ(model.getCards().size(), 1U) << namesIn(model.getCards());
    EXPECT_NE(cardFor(model, document), nullptr);
    EXPECT_EQ(cardFor(model, autosave), nullptr);

    fs::remove_all(dir);
}

TEST(DashboardModel, aFolderListingIsShallowUnlessTheFolderAsksForRecursion) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardFolders");
    const fs::path nested = dir / "nested";
    fs::create_directories(nested);
    writeDocument(dir / "top.xopp");
    writeDocument(nested / "deep.xopp");

    DashboardModel shallow;
    shallow.setLibraryFolders({LibraryFolder{dir, "dir", true, false, LibraryFolder::State::Ok}});
    shallow.refresh();
    EXPECT_EQ(namesIn(shallow.cardsIn(DashboardSection::Library)), "top.xopp ");

    DashboardModel recursive;
    recursive.setLibraryFolders({LibraryFolder{dir, "dir", true, true, LibraryFolder::State::Ok}});
    recursive.refresh();
    EXPECT_EQ(namesIn(recursive.cardsIn(DashboardSection::Library)), "deep.xopp top.xopp ");

    fs::remove_all(dir);
}

TEST(DashboardModel, anUnreadableFolderIsReportedRatherThanScanned) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardUnreadable");
    const fs::path locked = dir / "locked";
    fs::create_directories(locked);
    writeDocument(locked / "hidden.xopp");

    if (geteuid() == 0) {
        GTEST_SKIP() << "root can read a directory whatever its mode says";
    }
    fs::permissions(locked, fs::perms::none);
    const PermissionsGuard restoreOnExit{locked, fs::perms::owner_all};
    std::error_code probeError;
    ASSERT_FALSE(fs::exists(locked / "hidden.xopp", probeError) && !probeError)
            << "the test needs a folder it cannot read";

    DashboardModel model;
    model.setLibraryFolders({LibraryFolder{locked, "locked", true, false, LibraryFolder::State::Ok}});
    model.refresh();

    const std::vector<LibraryFolder>& folders = model.getLibraryFolders();
    ASSERT_EQ(folders.size(), 1U);
    EXPECT_NE(folders.front().state, LibraryFolder::State::Ok);
    EXPECT_TRUE(model.cardsIn(DashboardSection::Library).empty());

    // The state is re-read, so the folder comes back after the permission is restored.
    fs::permissions(locked, fs::perms::owner_all);
    model.refresh();
    EXPECT_EQ(model.getLibraryFolders().front().state, LibraryFolder::State::Ok);
    EXPECT_EQ(namesIn(model.cardsIn(DashboardSection::Library)), "hidden.xopp ");

    fs::remove_all(dir);
}

TEST(DashboardModel, recoveryComesFirstAndOnlyWhenThereAreCandidates) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardRecovery");
    const fs::path autosave;
    {
        // Named like an autosave and in an autosave folder, so the inventory recognises it.
        std::ofstream out(dir / ".notes.autosave.xopp", std::ios::binary);
        out << "PK\x03\x04" << "payload";
    }
    {
        std::ofstream out(dir / "notes.xopp", std::ios::binary);
        out << "PK\x03\x04" << "payload";
    }

    DashboardModel model;
    model.setRecentFiles({dir / "notes.xopp"});
    model.setRecoveryCandidates(xoj::safety::RecoveryInventory::scanFolder(dir));

    model.refresh();
    ASSERT_EQ(model.getRecoveryCards().size(), 1U);
    EXPECT_EQ(sectionNames(model.sections()), "recovery continue-working pinned library templates ");

    // A recovery card is not a document card: the autosave file is an internal artefact.
    EXPECT_EQ(model.getCards().size(), 1U) << namesIn(model.getCards());
    EXPECT_EQ(cardFor(model, dir / ".notes.autosave.xopp"), nullptr);

    DashboardModel without;
    without.refresh();
    EXPECT_EQ(sectionNames(without.sections()), "continue-working pinned library templates ");

    fs::remove_all(dir);
}

TEST(DashboardModel, aFolderEntryThatIsNotAFolderIsReported) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardNotAFolder");
    const fs::path file = writeDocument(dir / "notes.xopp");

    DashboardModel model;
    model.setLibraryFolders({LibraryFolder{file, "notes.xopp", true, false, LibraryFolder::State::Unknown}});
    model.refresh();

    EXPECT_EQ(model.getLibraryFolders().front().state, LibraryFolder::State::NotADirectory);
    EXPECT_TRUE(model.cardsIn(DashboardSection::Library).empty());

    fs::remove_all(dir);
}

TEST(DashboardModel, aDisabledFolderIsNotListed) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardDisabledFolder");
    writeDocument(dir / "notes.xopp");

    DashboardModel model;
    model.setLibraryFolders({LibraryFolder{dir, "dir", false, false, LibraryFolder::State::Ok}});
    model.refresh();

    EXPECT_TRUE(model.cardsIn(DashboardSection::Library).empty());

    fs::remove_all(dir);
}

TEST(DashboardModel, anExtractedPreviewSurvivesTheNextRefresh) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardPreview");
    const fs::path document = writeDocument(dir / "notes.xopp");

    DashboardModel model;
    model.setRecentFiles({document});
    model.refresh();
    ASSERT_EQ(model.cardsIn(DashboardSection::ContinueWorking).front().preview, DocumentCard::Preview::Unknown);

    model.setPreview(document, DocumentCard::Preview::Available);
    EXPECT_EQ(model.cardsIn(DashboardSection::ContinueWorking).front().preview, DocumentCard::Preview::Available);

    // The dashboard refreshes on file events; a preview that was paid for is not thrown away.
    model.refresh();
    EXPECT_EQ(model.cardsIn(DashboardSection::ContinueWorking).front().preview, DocumentCard::Preview::Available);

    fs::remove_all(dir);
}

TEST(DashboardModel, forgettingAPathDropsItsMembership) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardForget");
    const fs::path document = writeDocument(dir / "notes.xopp");
    const fs::path other = writeDocument(dir / "other.xopp");

    DashboardModel model;
    model.setRecentFiles({document, other});
    model.setPinnedFiles({document});
    model.refresh();
    ASSERT_TRUE(model.isPinned(document));

    EXPECT_TRUE(model.forget(document));
    EXPECT_FALSE(model.isPinned(document));
    EXPECT_EQ(model.getRecentFiles().size(), 1U);
    model.refresh();

    const std::vector<DocumentCard> recent = model.cardsIn(DashboardSection::ContinueWorking);
    ASSERT_EQ(recent.size(), 1U);
    EXPECT_EQ(recent.front().displayName, "other.xopp");
    EXPECT_TRUE(model.cardsIn(DashboardSection::Pinned).empty());

    EXPECT_FALSE(model.forget(document)) << "forgetting something that is not listed changes nothing";

    fs::remove_all(dir);
}

TEST(RecoveryCard, mirrorsWhatTheInventoryFound) {
    const fs::path dir = freshDir("xournalpp-test-units_dashboardRecoveryCard");
    {
        std::ofstream out(dir / "notes.xopp", std::ios::binary);
        out << "PK\x03\x04" << "original";
    }
    {
        std::ofstream out(dir / ".notes.autosave.xopp", std::ios::binary);
        out << "PK\x03\x04" << "recovered longer payload";
    }

    std::vector<xoj::safety::RecoveryCandidate> candidates = xoj::safety::RecoveryInventory::scanFolder(dir);
    ASSERT_EQ(candidates.size(), 1U);

    const RecoveryCard card = RecoveryCard::fromCandidate(candidates.front());
    EXPECT_EQ(card.displayName, ".notes.autosave.xopp");
    EXPECT_EQ(card.recoveryPath, dir / ".notes.autosave.xopp");
    EXPECT_EQ(card.originalPath, dir / "notes.xopp");
    EXPECT_TRUE(card.originalExists);
    EXPECT_EQ(card.validation, xoj::safety::RecoveryValidation::Ok);
    EXPECT_TRUE(card.openable());
    EXPECT_GT(card.size, 0U);
    EXPECT_TRUE(card.recoveryTime.has_value());

    // A candidate that is not a Xournal++ document must not offer to be opened.
    xoj::safety::RecoveryCandidate broken;
    broken.recoveryPath = dir / "broken.autosave.xopp";
    broken.validation = xoj::safety::RecoveryValidation::UnrecognizedFormat;
    const RecoveryCard brokenCard = RecoveryCard::fromCandidate(broken);
    EXPECT_FALSE(brokenCard.openable());
    EXPECT_EQ(brokenCard.displayName, "broken.autosave.xopp");

    fs::remove_all(dir);
}

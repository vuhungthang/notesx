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

#include <chrono>  // for seconds
#include <cstddef>
#include <fstream>  // for ofstream
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "control/RecoveryInventory.h"

#ifndef _WIN32
#include <sys/stat.h>  // for chmod
#include <unistd.h>    // for geteuid
#endif

/*
 * Plan 004, step 4: the recovery inventory.
 *
 * Everything here runs on fixtures in a temporary directory, so the tests say what the
 * conventions are instead of depending on whatever happens to be in the user's cache folder.
 * The formats are represented by their magic bytes: the inventory must decide whether a
 * candidate is plausible without reading the document, so the fixtures only need to start
 * like one.
 */

namespace {

using xoj::safety::RecoveryCandidate;
using xoj::safety::RecoveryInventory;
using xoj::safety::RecoveryValidation;

/// The first bytes of a `.xopp`, which is a zip archive.
constexpr std::string_view XOPP_MAGIC = "PK\x03\x04";

/// A directory of its own for one test, removed by the fixture.
class RecoveryInventoryTest: public ::testing::Test {
protected:
    void SetUp() override {
        this->root = fs::temp_directory_path() / ("xournalpp-test-recovery-" + std::to_string(::testing::UnitTest::GetInstance()->current_test_info()->line()));
        fs::remove_all(this->root);
        fs::create_directories(this->root);
    }

    void TearDown() override { fs::remove_all(this->root); }

    auto writeFile(const fs::path& path, std::string_view content) const -> fs::path {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        return path;
    }

    /// A plausible recovery copy.
    auto writeRecovery(const fs::path& path) const -> fs::path { return writeFile(path, XOPP_MAGIC); }

    /// Make `path` look `age` old, so newer/older comparisons are decided by the test and not by
    /// how fast the filesystem happened to write two files.
    static void setAge(const fs::path& path, std::chrono::seconds age) {
        fs::last_write_time(path, fs::file_time_type::clock::now() - age);
    }

    fs::path root;
};

/// The one candidate whose recovery path is `path`, or a failure.
auto candidateFor(const std::vector<RecoveryCandidate>& candidates, const fs::path& path) -> const RecoveryCandidate* {
    for (const RecoveryCandidate& candidate: candidates) {
        if (candidate.recoveryPath == path) {
            return &candidate;
        }
    }
    return nullptr;
}

}  // namespace

/*
 * Plan 004, step 4: the name conventions. A named autosave is a hidden file next to the document
 * it came from; an unnamed one is a pid-named file in the autosave folder and belongs to no
 * document.
 */
TEST(RecoveryInventoryNamingTest, testTheAutosaveNameConventionsAreInversesOfEachOther) {
    EXPECT_EQ(RecoveryInventory::getRecoveryFor("/notes/foo.xopp"), fs::path("/notes/.foo.autosave.xopp"));
    EXPECT_EQ(RecoveryInventory::getRecoveryFor("/notes/foo.xoj"), fs::path("/notes/.foo.autosave.xopp"));
    // `Util::clearExtensions` only strips the two Xournal++ extensions, so a PDF-backed document
    // keeps its extension in the autosave's name.
    EXPECT_EQ(RecoveryInventory::getRecoveryFor("/notes/foo.pdf"), fs::path("/notes/.foo.pdf.autosave.xopp"));
    EXPECT_TRUE(RecoveryInventory::getRecoveryFor("").empty());

    EXPECT_EQ(RecoveryInventory::getOriginalFor("/notes/.foo.autosave.xopp"), fs::path("/notes/foo"));
    EXPECT_EQ(RecoveryInventory::getOriginalFor("/notes/.foo.pdf.autosave.xopp"), fs::path("/notes/foo.pdf"));
    // An unnamed autosave has no document to point at.
    EXPECT_TRUE(RecoveryInventory::getOriginalFor("/cache/autosaves/12345.xopp").empty());
    // Nor has a file that is not an autosave at all.
    EXPECT_TRUE(RecoveryInventory::getOriginalFor("/notes/foo.xopp").empty());

    EXPECT_TRUE(RecoveryInventory::looksLikeAutosave("/notes/.foo.autosave.xopp"));
    EXPECT_FALSE(RecoveryInventory::looksLikeAutosave("/notes/foo.xopp"));
    EXPECT_FALSE(RecoveryInventory::looksLikeAutosave("/notes/.foo.autosave.xopp~"))
            << "a file being written is not a candidate";
    EXPECT_FALSE(RecoveryInventory::looksLikeAutosave("/notes/.foo.autosave.xopp.swap"));
}

/*
 * Plan 004, step 4: an unnamed autosave is a candidate, and it is honest about not knowing which
 * document it belongs to.
 */
TEST_F(RecoveryInventoryTest, testAnUnnamedAutosaveHasNoOriginal) {
    const fs::path recovery = this->writeRecovery(this->root / "autosaves" / "4242.xopp");

    const auto candidate = RecoveryInventory::inspect(recovery);
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->validation, RecoveryValidation::Ok);
    EXPECT_TRUE(candidate->originalPath.empty());
    EXPECT_FALSE(candidate->originalExists);
    EXPECT_FALSE(candidate->newerThanOriginal);
    EXPECT_TRUE(candidate->recoveryTime.has_value());
    EXPECT_EQ(candidate->size, XOPP_MAGIC.size());
}

/*
 * Plan 004, step 4: a named autosave next to its document names it, and says whether the copy is
 * newer than what is on disk.
 */
TEST_F(RecoveryInventoryTest, testANamedAutosaveNamesItsOriginalAndComparesTimes) {
    const fs::path document = this->writeFile(this->root / "notes.xopp", XOPP_MAGIC);
    const fs::path recovery = this->writeRecovery(this->root / ".notes.autosave.xopp");
    setAge(document, std::chrono::seconds(600));
    setAge(recovery, std::chrono::seconds(60));

    const auto candidate = RecoveryInventory::inspect(recovery);
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->validation, RecoveryValidation::Ok);
    EXPECT_EQ(candidate->originalPath, document);
    EXPECT_TRUE(candidate->originalExists);
    EXPECT_TRUE(candidate->newerThanOriginal) << "the copy holds work the file does not";
    ASSERT_TRUE(candidate->originalTime.has_value());
    EXPECT_LT(*candidate->originalTime, *candidate->recoveryTime);
}

TEST_F(RecoveryInventoryTest, testAnOlderAutosaveIsNotReportedAsNewer) {
    const fs::path document = this->writeFile(this->root / "notes.xopp", XOPP_MAGIC);
    const fs::path recovery = this->writeRecovery(this->root / ".notes.autosave.xopp");
    setAge(recovery, std::chrono::seconds(600));
    setAge(document, std::chrono::seconds(60));

    const auto candidate = RecoveryInventory::inspect(recovery);
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->validation, RecoveryValidation::Ok);
    EXPECT_TRUE(candidate->originalExists);
    EXPECT_FALSE(candidate->newerThanOriginal) << "the file on disk is the newer one";
}

TEST_F(RecoveryInventoryTest, testAnAutosaveOfAnXojDocumentFindsTheOriginalUnderItsOwnExtension) {
    const fs::path document = this->writeFile(this->root / "notes.xoj", "<?xml version=\"1.0\"?><xournal/>");

    const fs::path recovery = this->writeRecovery(this->root / ".notes.autosave.xopp");
    const auto candidate = RecoveryInventory::inspect(recovery);
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->originalPath, document) << "the original is found under either Xournal++ extension";
    EXPECT_TRUE(candidate->originalExists);
}

TEST_F(RecoveryInventoryTest, testAMissingOriginalIsReportedRatherThanGuessedAt) {
    const fs::path recovery = this->writeRecovery(this->root / ".gone.autosave.xopp");

    const auto candidate = RecoveryInventory::inspect(recovery);
    ASSERT_TRUE(candidate.has_value());
    EXPECT_EQ(candidate->validation, RecoveryValidation::Ok) << "the copy itself is fine";
    EXPECT_EQ(candidate->originalPath, this->root / "gone") << "the name it was derived from";
    EXPECT_FALSE(candidate->originalExists);
    EXPECT_FALSE(candidate->newerThanOriginal);
    EXPECT_FALSE(candidate->originalTime.has_value());
}

/*
 * Plan 004, step 4: "Invalid candidates must be reported, not crash enumeration". A corrupt file
 * is a candidate whose validation says so, and it does not stop the others from being listed.
 */
TEST_F(RecoveryInventoryTest, testACorruptCandidateIsReportedAndDoesNotHideTheOthers) {
    const fs::path corrupt = this->writeFile(this->root / ".broken.autosave.xopp", "this is not a document");
    const fs::path empty = this->writeFile(this->root / ".empty.autosave.xopp", "");
    const fs::path good = this->writeRecovery(this->root / ".good.autosave.xopp");

    const auto corruptCandidate = RecoveryInventory::inspect(corrupt);
    ASSERT_TRUE(corruptCandidate.has_value());
    EXPECT_EQ(corruptCandidate->validation, RecoveryValidation::UnrecognizedFormat);
    EXPECT_FALSE(corruptCandidate->error.empty());

    const auto emptyCandidate = RecoveryInventory::inspect(empty);
    ASSERT_TRUE(emptyCandidate.has_value());
    EXPECT_EQ(emptyCandidate->validation, RecoveryValidation::UnrecognizedFormat);

    const auto candidates = RecoveryInventory::scanFolder(this->root);
    EXPECT_NE(candidateFor(candidates, corrupt), nullptr);
    EXPECT_NE(candidateFor(candidates, empty), nullptr);
    EXPECT_NE(candidateFor(candidates, good), nullptr);
    EXPECT_EQ(candidates.size(), 3U);
}

/*
 * Plan 004, step 4: an inaccessible path is reported, not thrown over. A path that is not a
 * regular file is one kind of inaccessible, an unreadable file is another, and a folder that
 * cannot be listed is a third.
 */
TEST_F(RecoveryInventoryTest, testAnInaccessiblePathIsReportedWithoutThrowing) {
    const fs::path directory = this->root / ".adir.autosave.xopp";
    fs::create_directories(directory);
    const auto directoryCandidate = RecoveryInventory::inspect(directory);
    ASSERT_TRUE(directoryCandidate.has_value());
    EXPECT_EQ(directoryCandidate->validation, RecoveryValidation::NotARegularFile);

    const fs::path missing = this->root / ".never-written.autosave.xopp";
    const auto missingCandidate = RecoveryInventory::inspect(missing);
    ASSERT_TRUE(missingCandidate.has_value());
    EXPECT_EQ(missingCandidate->validation, RecoveryValidation::Missing);

    // A folder that does not exist is not an error: it simply holds no candidates.
    EXPECT_TRUE(RecoveryInventory::scanFolder(this->root / "no-such-folder").empty());

    // A file that is there but cannot be opened is Unreadable. As root nothing is unreadable,
    // which is why the check is skipped there rather than asserted falsely.
#ifndef _WIN32
    if (::geteuid() != 0) {
        const fs::path locked = this->writeRecovery(this->root / ".locked.autosave.xopp");
        ASSERT_EQ(::chmod(locked.c_str(), 0), 0);
        const auto lockedCandidate = RecoveryInventory::inspect(locked);
        ASSERT_TRUE(lockedCandidate.has_value());
        EXPECT_EQ(lockedCandidate->validation, RecoveryValidation::Unreadable);
        EXPECT_FALSE(lockedCandidate->error.empty());
        ::chmod(locked.c_str(), 0644);

        const fs::path lockedFolder = this->root / "locked-folder";
        fs::create_directories(lockedFolder);
        this->writeRecovery(lockedFolder / ".hidden.autosave.xopp");
        ASSERT_EQ(::chmod(lockedFolder.c_str(), 0), 0);
        EXPECT_TRUE(RecoveryInventory::scanFolder(lockedFolder).empty())
                << "a folder that cannot be read yields no candidates instead of an exception";
        ::chmod(lockedFolder.c_str(), 0755);
    }
#endif
}

/*
 * Plan 004, step 4: the temporary files a save or an autosave leaves behind while it swaps files
 * are not candidates, and the listing is newest first.
 */
TEST_F(RecoveryInventoryTest, testTemporariesAreSkippedAndTheListingIsNewestFirst) {
    const fs::path old = this->writeRecovery(this->root / ".old.autosave.xopp");
    const fs::path recent = this->writeRecovery(this->root / ".recent.autosave.xopp");
    setAge(old, std::chrono::seconds(600));
    setAge(recent, std::chrono::seconds(30));

    // The swap dance: a `~` file and a `.swap` file hold the same content but no candidate.
    this->writeRecovery(this->root / ".old.autosave.xopp~");
    this->writeRecovery(this->root / ".old.autosave.xopp.swap");

    const auto candidates = RecoveryInventory::scanFolder(this->root);
    ASSERT_EQ(candidates.size(), 2U);
    EXPECT_EQ(candidates[0].recoveryPath, recent);
    EXPECT_EQ(candidates[1].recoveryPath, old);
}

/*
 * Plan 004, step 4: a folder outside the autosave cache holds named autosaves only. The user's own
 * documents in the same folder are not candidates.
 */
TEST_F(RecoveryInventoryTest, testOnlyAutosaveNamesCountOutsideTheAutosaveFolder) {
    const fs::path document = this->writeFile(this->root / "notes.xopp", XOPP_MAGIC);
    const fs::path otherDocument = this->writeFile(this->root / "other.xopp", XOPP_MAGIC);
    const fs::path recovery = this->writeRecovery(this->root / ".notes.autosave.xopp");

    const auto candidates = RecoveryInventory::scanFolder(this->root);
    ASSERT_EQ(candidates.size(), 1U);
    EXPECT_EQ(candidates[0].recoveryPath, recovery);
    EXPECT_EQ(candidateFor(candidates, document), nullptr) << "a document is not a recovery copy";
    EXPECT_EQ(candidateFor(candidates, otherDocument), nullptr);
}

/*
 * Plan 004, step 4: scanning several folders never reports the same file twice, and the default
 * search folder is the one the autosave convention writes to.
 */
/*
 * Plan 004, step 4: the inventory is read-only.
 *
 * Looking for recovery copies must not write anything, and neither must looking in a folder that
 * is not there: the autosave folder does not exist until the first autosave, and asking where it
 * would be is not a reason to create it.
 */
TEST(RecoveryInventoryReadOnlyTest, testLookingForRecoveryCopiesWritesNothing) {
    const fs::path root = fs::temp_directory_path() / "xournalpp-test-read-only-scan";
    fs::remove_all(root);
    const fs::path folder = root / "not-there";

    EXPECT_EQ(RecoveryInventory::getAutosaveFolder().filename(), "autosaves")
            << "the folder recovery copies are searched in is the one the convention names";

    const auto candidates = RecoveryInventory::scanFolder(folder);
    EXPECT_TRUE(candidates.empty());
    EXPECT_FALSE(fs::exists(root)) << "looking in a folder that is not there is not creating it";

    const auto scanned = RecoveryInventory::scan({folder});
    EXPECT_TRUE(scanned.empty());
    EXPECT_FALSE(fs::exists(root));

    fs::remove_all(root);
}

TEST_F(RecoveryInventoryTest, testScanningSeveralFoldersDeduplicates) {
    const fs::path recovery = this->writeRecovery(this->root / ".notes.autosave.xopp");

    const auto candidates = RecoveryInventory::scan({this->root, this->root, this->root / "no-such-folder"});
    ASSERT_EQ(candidates.size(), 1U);
    EXPECT_EQ(candidates[0].recoveryPath, recovery);

    const auto folders = RecoveryInventory::defaultSearchFolders();
    ASSERT_EQ(folders.size(), 1U);
    EXPECT_EQ(folders[0].filename(), "autosaves");
}

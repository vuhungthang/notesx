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

#include <filesystem>  // for path, remove_all, create_directories
#include <fstream>     // for ofstream, ifstream
#include <iterator>    // for istreambuf_iterator
#include <string>
#include <system_error>  // for error_code

#include <gtest/gtest.h>

#include "dashboard/DashboardTypes.h"   // for RecoveryCard
#include "dashboard/RecoveryActions.h"  // for RecoveryActions

#include "filesystem.h"  // for fs
using xoj::dashboard::RecoveryActions;
using xoj::dashboard::RecoveryCard;

/*
 * Plan 006, step 6: what a recovery card does to files.
 *
 * The two operations a recovery card offers are checked here with real files in a temporary
 * folder, because this is where "recovering never overwrites the original automatically" and
 * "deleting a copy that cannot be deleted is reported" are decided.
 */

namespace {

/// A fresh folder per test, so nothing a test does is visible to another one.
auto freshDir(const std::string& name) -> fs::path {
    const fs::path dir = fs::temp_directory_path() / ("xournalpp-test-units_" + name);
    std::error_code error;
    fs::remove_all(dir, error);
    fs::create_directories(dir, error);
    return dir;
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

auto readText(const fs::path& path) -> std::string {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

/// A copy of a document, written beside it the way Xournal++ writes a named autosave.
auto candidateFor(const fs::path& document) -> RecoveryCard {
    RecoveryCard card;
    card.originalPath = document;
    card.recoveryPath = document.parent_path() / (".test_" + document.filename().string() + ".autosave.xopp");
    card.displayName = card.recoveryPath.filename().string();
    return card;
}

}  // namespace

TEST(RecoveryActionsTest, aCopyIsWrittenWhereTheUserAskedAndTheOriginalIsUntouched) {
    const fs::path dir = freshDir("recoveryCopy");
    const fs::path document = dir / "notes.xopp";
    writeText(document, "the document");
    const fs::path target = dir / "recovered.xopp";

    RecoveryCard card = candidateFor(document);
    writeText(card.recoveryPath, "the recovered work");

    std::string error = "not cleared";
    ASSERT_TRUE(RecoveryActions::copyTo(card, target, false, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(readText(target), "the recovered work");
    EXPECT_EQ(readText(document), "the document") << "the document is not written to";
    EXPECT_TRUE(fs::exists(card.recoveryPath)) << "writing a copy does not delete the copy";

    fs::remove_all(dir);
}

TEST(RecoveryActionsTest, aCopyIsNeverWrittenOverTheDocumentItCameFromByItself) {
    const fs::path dir = freshDir("recoveryOriginal");
    const fs::path document = dir / "notes.xopp";
    writeText(document, "the document");

    RecoveryCard card = candidateFor(document);
    writeText(card.recoveryPath, "the recovered work");

    std::string error;
    EXPECT_FALSE(RecoveryActions::copyTo(card, document, false, error))
            << "the dashboard does not replace a document with a recovery copy on its own";
    EXPECT_FALSE(error.empty()) << "and it says why";
    EXPECT_EQ(readText(document), "the document");

    // A spelling of the same path is the same path: a link or a relative spelling is not a way
    // around this.
    EXPECT_TRUE(RecoveryActions::isOriginal(card, document));
    EXPECT_FALSE(RecoveryActions::isOriginal(card, dir / "other.xopp"));
    EXPECT_FALSE(RecoveryActions::isOriginal(card, {})) << "choosing nothing is not choosing the original";

    // When the user did choose it, it is written: that is the decision, not an accident.
    ASSERT_TRUE(RecoveryActions::copyTo(card, document, true, error));
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(readText(document), "the recovered work");

    fs::remove_all(dir);
}

TEST(RecoveryActionsTest, aCopyThatCannotBeReadIsReportedRatherThanWrittenAsAnEmptyFile) {
    const fs::path dir = freshDir("recoveryMissing");
    const fs::path document = dir / "notes.xopp";
    writeText(document, "the document");

    RecoveryCard card = candidateFor(document);  // no such recovery file

    std::string error;
    EXPECT_FALSE(RecoveryActions::copyTo(card, dir / "target.xopp", false, error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(fs::exists(dir / "target.xopp")) << "nothing is created when there is nothing to copy";

    fs::remove_all(dir);
}

TEST(RecoveryActionsTest, deletingACopyRemovesTheCopyAndNothingElse) {
    const fs::path dir = freshDir("recoveryDelete");
    const fs::path document = dir / "notes.xopp";
    writeText(document, "the document");

    RecoveryCard card = candidateFor(document);
    writeText(card.recoveryPath, "the recovered work");

    std::string error = "not cleared";
    ASSERT_TRUE(RecoveryActions::removeCopy(card, error));
    EXPECT_TRUE(error.empty());
    EXPECT_FALSE(fs::exists(card.recoveryPath));
    EXPECT_TRUE(fs::exists(document)) << "deleting a copy never deletes the document";
    EXPECT_EQ(readText(document), "the document");

    fs::remove_all(dir);
}

TEST(RecoveryActionsTest, deletingSomethingThatIsNotThereIsReportedWithAReason) {
    const fs::path dir = freshDir("recoveryDeleteFailure");
    const fs::path document = dir / "notes.xopp";
    writeText(document, "the document");

    RecoveryCard card = candidateFor(document);  // the copy does not exist
    std::string error;
    EXPECT_FALSE(RecoveryActions::removeCopy(card, error));
    EXPECT_FALSE(error.empty()) << "a failure to delete says what happened";
    EXPECT_TRUE(fs::exists(document));

    // A recovery list without a path cannot be deleted either, and says so instead of throwing.
    RecoveryCard empty;
    error.clear();
    EXPECT_FALSE(RecoveryActions::removeCopy(empty, error));
    EXPECT_FALSE(error.empty());

    fs::remove_all(dir);
}

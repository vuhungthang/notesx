/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * Plan 006, step 2: the reusable preview reading.
 *
 * `xournalpp-thumbnailer` used to be the only caller of the extractor and the only place that knew
 * what its failure codes mean. The application now reads previews through the same function, so
 * these tests fix the meaning of every answer - a preview, a document without one, a file that is
 * not a document, a document that cannot be read and a file that is not there - and that reading
 * a document leaves it exactly as it was.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "util/PreviewExtraction.h"

#include "config-test.h"
#include "filesystem.h"

using namespace xoj::preview;

namespace {

auto freshDir(const char* name) -> fs::path {
    const fs::path dir = fs::temp_directory_path() / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

auto bytesAsString(const std::vector<std::uint8_t>& data) -> std::string {
    return {data.begin(), data.end()};
}

}  // namespace

TEST(PreviewExtraction, readsThePreviewOfAGzippedDocument) {
    const PreviewResult result = extractPreview(GET_TESTFILE(u8"preview-test.xoj"));

    EXPECT_EQ(result.status, PreviewStatus::Extracted);
    EXPECT_TRUE(result.extracted());
    EXPECT_EQ(bytesAsString(result.data), "CppUnitTestString");
    EXPECT_TRUE(result.error.empty());
}

TEST(PreviewExtraction, readsThePreviewOfAPackagedDocument) {
    const PreviewResult result = extractPreview(GET_TESTFILE(u8"packaged_xopp/testPreview.xopp"));

    EXPECT_EQ(result.status, PreviewStatus::Extracted);
    EXPECT_EQ(bytesAsString(result.data), "CppUnitTestString \n");
}

TEST(PreviewExtraction, aDocumentWithoutAPreviewIsNotAnError) {
    const PreviewResult result = extractPreview(GET_TESTFILE(u8"preview-test-no-preview.unzipped.xoj"));

    EXPECT_EQ(result.status, PreviewStatus::NoPreview);
    EXPECT_FALSE(result.extracted());
    EXPECT_TRUE(result.data.empty());
    EXPECT_FALSE(result.error.empty()) << "the reason is reported, so a card can explain itself";
}

TEST(PreviewExtraction, aDocumentThatCannotBeReadIsCorrupt) {
    const PreviewResult result = extractPreview(GET_TESTFILE(u8"preview-test-invalid.xoj"));

    EXPECT_EQ(result.status, PreviewStatus::Corrupt);
    EXPECT_FALSE(result.error.empty());
}

TEST(PreviewExtraction, aFileThatIsNotThereCannotBeRead) {
    const PreviewResult result = extractPreview(GET_TESTFILE(u8"THIS FILE DOES NOT EXIST.xoj"));

    EXPECT_EQ(result.status, PreviewStatus::Unreadable);
    EXPECT_TRUE(result.data.empty()) << "an unreadable file yields no bytes at all";
}

TEST(PreviewExtraction, aFileKindTheEditorDoesNotReadIsUnsupported) {
    const PreviewResult result = extractPreview(GET_TESTFILE(u8"test.xoi"));

    EXPECT_EQ(result.status, PreviewStatus::Unsupported);
}

TEST(PreviewExtraction, readingADocumentDoesNotWriteToIt) {
    const fs::path dir = freshDir("xournalpp-test-units_previewReadOnly");
    const fs::path document = dir / "notes.xopp";
    fs::copy_file(GET_TESTFILE(u8"packaged_xopp/testPreview.xopp"), document);

    const auto before = fs::last_write_time(document);
    const auto sizeBefore = fs::file_size(document);

    ASSERT_EQ(extractPreview(document).status, PreviewStatus::Extracted);

    // A dashboard shows previews of files the user owns; showing one must not touch it, not even
    // its modification time.
    EXPECT_EQ(fs::last_write_time(document), before);
    EXPECT_EQ(fs::file_size(document), sizeBefore);

    fs::remove_all(dir);
}

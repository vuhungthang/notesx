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

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "control/commands/AcceleratorDisplay.h"  // for formatAcceleratorForDisplay
#include "control/commands/CommandMetadata.h"     // for CommandMetadata, rankCommands

/*
 * Plan 007, step 1: the metadata a command carries and what can be said about it.
 *
 * These tests are about the model behind the command palette and the shortcut reference, not about
 * any widget: they define what "every palette-visible command has a unique ID, a nonempty
 * translated title and a valid category" means, how a query ranks the commands, how an accelerator
 * is rendered for a platform, and when two commands disagree about one accelerator.
 */

namespace {

using xoj::command::AcceleratorPlatform;
using xoj::command::CommandMetadata;

constexpr auto MAC = AcceleratorPlatform::MACOS;
constexpr auto LINUX = AcceleratorPlatform::LINUX;

auto command(std::string id, std::string title, std::string category, std::string accelerator = "",
             std::vector<std::string> keywords = {}) -> CommandMetadata {
    CommandMetadata c;
    c.id = std::move(id);
    c.title = std::move(title);
    c.category = std::move(category);
    c.accelerator = std::move(accelerator);
    c.actionName = "win." + c.id;
    c.keywords = std::move(keywords);
    return c;
}

/// The order the ranking put the given ids in, for an assertion that reads like the palette.
auto rankedIds(const std::vector<CommandMetadata>& commands, const std::string& query) -> std::vector<std::string> {
    std::vector<std::string> ids;
    for (size_t index: xoj::command::rankCommands(commands, query)) {
        ids.emplace_back(commands[index].id);
    }
    return ids;
}

}  // namespace

TEST(CommandMetadataTest, testACompleteRegistryHasNoProblem) {
    std::vector<CommandMetadata> commands{command("save", "Save", "File"), command("export-as-pdf", "Export as PDF",
                                                                                  "File", "Ctrl+E")};
    EXPECT_TRUE(xoj::command::validateCommands(commands).empty());
}

TEST(CommandMetadataTest, testADuplicateIdIsReported) {
    std::vector<CommandMetadata> commands{command("save", "Save", "File"), command("save", "Save as", "File")};

    auto problems = xoj::command::validateCommands(commands);
    ASSERT_EQ(problems.size(), 1u);
    EXPECT_NE(problems.front().find("save"), std::string::npos) << problems.front();
}

TEST(CommandMetadataTest, testACommandWithoutTitleOrCategoryIsReported) {
    std::vector<CommandMetadata> commands{command("save", "", "File"), command("open", "Open", "")};

    auto problems = xoj::command::validateCommands(commands);
    ASSERT_EQ(problems.size(), 2u);
    EXPECT_NE(problems[0].find("save"), std::string::npos) << problems[0];
    EXPECT_NE(problems[1].find("open"), std::string::npos) << problems[1];
}

TEST(CommandMetadataTest, testAnEmptyQueryKeepsTheRegistryOrder) {
    std::vector<CommandMetadata> commands{command("new-file", "New", "File"), command("save", "Save", "File"),
                                          command("print", "Print", "File")};

    EXPECT_EQ(rankedIds(commands, ""), (std::vector<std::string>{"new-file", "save", "print"}));
    EXPECT_EQ(rankedIds(commands, "   "), (std::vector<std::string>{"new-file", "save", "print"}));
}

TEST(CommandMetadataTest, testAnExactTitleComesFirst) {
    std::vector<CommandMetadata> commands{command("export-as-pdf", "Export as PDF", "File"),
                                          command("save-as", "Save As", "File"), command("print", "Print", "File")};

    auto ranked = rankedIds(commands, "Print");
    ASSERT_FALSE(ranked.empty());
    EXPECT_EQ(ranked.front(), "print");
}

TEST(CommandMetadataTest, testAWordPrefixOutranksAWordThatMerelyContainsTheQuery) {
    std::vector<CommandMetadata> commands{command("reprint", "Reprint", "File"), command("print", "Print", "File")};

    EXPECT_EQ(rankedIds(commands, "print"), (std::vector<std::string>{"print", "reprint"}));
}

TEST(CommandMetadataTest, testAnyTitleWordCanMatch) {
    std::vector<CommandMetadata> commands{command("export-as-pdf", "Export as PDF", "File"),
                                          command("save", "Save", "File")};

    EXPECT_EQ(rankedIds(commands, "pdf"), (std::vector<std::string>{"export-as-pdf"}));
}

TEST(CommandMetadataTest, testAKeywordMatchesAndRanksBelowATitle) {
    std::vector<CommandMetadata> commands{command("annotate-pdf", "Annotate PDF", "File", "", {"import"}),
                                          command("import-image", "Import Image", "Edit")};

    auto ranked = rankedIds(commands, "import");
    ASSERT_EQ(ranked.size(), 2u);
    EXPECT_EQ(ranked.front(), "import-image") << "a title word beats a keyword";
}

TEST(CommandMetadataTest, testACategoryMatches) {
    std::vector<CommandMetadata> commands{command("save", "Save", "File"), command("grid-snapping", "Grid Snapping",
                                                                                  "View")};

    EXPECT_EQ(rankedIds(commands, "view"), (std::vector<std::string>{"grid-snapping"}));
}

TEST(CommandMetadataTest, testEveryTokenHasToMatch) {
    std::vector<CommandMetadata> commands{command("export-as-pdf", "Export as PDF", "File"),
                                          command("export-as", "Export as", "File"), command("print", "Print", "File")};

    EXPECT_EQ(rankedIds(commands, "export pdf"), (std::vector<std::string>{"export-as-pdf"}));
    EXPECT_TRUE(rankedIds(commands, "export nothing-like-this").empty());
}

TEST(CommandMetadataTest, testTheQueryIsMatchedInAnyCaseAndWithoutAccents) {
    std::vector<CommandMetadata> commands{command("export-as-pdf", "\u00c9xporter en PDF", "Fichier"),
                                          command("print", "Imprimer", "Fichier")};

    EXPECT_EQ(rankedIds(commands, "exporter"), (std::vector<std::string>{"export-as-pdf"}));
    EXPECT_EQ(rankedIds(commands, "EXPORTER"), (std::vector<std::string>{"export-as-pdf"}));
    EXPECT_EQ(rankedIds(commands, "\u00e9xporter"), (std::vector<std::string>{"export-as-pdf"}));
}

TEST(CommandMetadataTest, testAFuzzySubsequenceMatchesLast) {
    std::vector<CommandMetadata> commands{command("export-as-pdf", "Export as PDF", "File"),
                                          command("expect-pdf", "Expected PDF", "File")};

    auto ranked = rankedIds(commands, "exppdf");
    ASSERT_EQ(ranked.size(), 2u);
    EXPECT_EQ(ranked.front(), "expect-pdf");
    EXPECT_EQ(ranked.back(), "export-as-pdf");
}

TEST(CommandMetadataTest, testTheWholeTitleOutranksAWordMatch) {
    std::vector<CommandMetadata> commands{command("paper-format", "Paper Format", "Journal"),
                                          command("paper", "Paper", "Journal")};

    EXPECT_EQ(rankedIds(commands, "paper").front(), "paper");
}

TEST(AcceleratorDisplayTest, testModifiersAreRenderedForThePlatform) {
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Ctrl><Shift>s", LINUX), "Ctrl+Shift+S");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Ctrl>Q", LINUX), "Ctrl+Q");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Alt>Left", LINUX), "Alt+Left");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Meta>Z", LINUX), "Super+Z");

    // macOS reads control, option, shift, command, whatever order the accelerator was written in.
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Meta><Shift>Z", MAC), "\u21e7\u2318Z");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Ctrl><Shift>s", MAC), "\u2303\u21e7S");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Alt>Left", MAC), "\u2325Left");
}

/**
 * GTK hands back "<Primary>z" for anything registered as "<Ctrl>Z" - it spells the control key of
 * this platform as the primary modifier - so the reference has to read it the way the menu does and
 * not as the Super key. On macOS the same modifier is the command key.
 */
TEST(AcceleratorDisplayTest, testThePrimaryModifierIsSpeltForThePlatform) {
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Primary>z", LINUX), "Ctrl+Z");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Primary><Shift>u", LINUX), "Ctrl+Shift+U");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Primary>Z", LINUX), "Ctrl+Z");

    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Primary>z", MAC), "\u2318Z");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Primary><Shift>u", MAC), "\u21e7\u2318U");
}

TEST(AcceleratorDisplayTest, testASingleKeyIsRenderedAsItself) {
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("Delete", LINUX), "Delete");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("F5", LINUX), "F5");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Ctrl>Page_Up", LINUX), "Ctrl+PageUp");
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("", LINUX), "");
}

TEST(AcceleratorDisplayTest, testWhyNothingIsRenderedWhenTheAcceleratorDoesNotParse) {
    // A GDK accelerator that GTK itself would reject is shown as it stands rather than dropped: the
    // reference is a report of what is registered, not a validator of it.
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Ctrl", LINUX), "<Ctrl");
}

TEST(AcceleratorDisplayTest, testOnlyTheFirstAcceleratorIsTheDisplayedOne) {
    EXPECT_EQ(xoj::command::formatAcceleratorForDisplay("<Ctrl><Shift>z", LINUX), "Ctrl+Shift+Z");
}

TEST(AcceleratorConflictTest, testTwoCommandsOnOneAcceleratorAreReported) {
    std::vector<CommandMetadata> commands{command("save", "Save", "File", "Ctrl+S"),
                                          command("save-current", "Save Current", "File", "Ctrl+S"),
                                          command("print", "Print", "File", "Ctrl+P")};

    auto conflicts = xoj::command::findAcceleratorConflicts(commands);
    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts.front().accelerator, "Ctrl+S");
    EXPECT_EQ(conflicts.front().commandIds, (std::vector<std::string>{"save", "save-current"}));
}

TEST(AcceleratorConflictTest, testOneActionReachedTwiceIsNotAConflict) {
    CommandMetadata fromTools = command("ruler", "Draw Line", "Tools");
    CommandMetadata fromMenu = command("draw-line", "Draw Line", "Tools");
    fromMenu.accelerator = "Ctrl+L";
    fromTools.accelerator = "Ctrl+L";
    fromMenu.actionName = "win.tool-draw-line";
    fromTools.actionName = "win.tool-draw-line";

    EXPECT_TRUE(xoj::command::findAcceleratorConflicts({fromMenu, fromTools}).empty());
}

TEST(AcceleratorConflictTest, testCommandsWithoutAnAcceleratorDoNotConflict) {
    std::vector<CommandMetadata> commands{command("save", "Save", "File"), command("print", "Print", "File")};

    EXPECT_TRUE(xoj::command::findAcceleratorConflicts(commands).empty());
}

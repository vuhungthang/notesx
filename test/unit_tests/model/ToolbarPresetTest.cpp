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

#include <algorithm>  // for find_if
#include <cstddef>    // for size_t
#include <memory>     // for unique_ptr
#include <string>     // for string
#include <vector>     // for vector

#include <config-test.h>
#include <glib.h>  // for g_key_file_*, g_strfreev
#include <gtest/gtest.h>

#include "gui/toolbarMenubar/model/ColorPalette.h"
#include "gui/toolbarMenubar/model/ToolbarData.h"
#include "gui/toolbarMenubar/model/ToolbarModel.h"

/*
 * Plan 002: the [Focus] preset is the toolbar a fresh profile starts with.
 *
 * These tests use the same parser as the application (ToolbarModel/ToolbarData), so a
 * preset that refers to an item the model cannot handle at all fails here. Whether the
 * item identifiers are known to the *running* toolbar is checked by the live probe of the
 * Focus workspace.
 *
 * The template holds no placeholders: it is copied into the build verbatim, so testing the
 * checked-in template tests the file that is loaded at runtime.
 */

namespace {

/// Toolbar a fresh profile starts with.
constexpr auto FOCUS_TOOLBAR_ID = "Focus";
/// Plan 002, step 2: the primary row is limited to a dozen actions.
constexpr std::size_t FOCUS_PRIMARY_ACTION_LIMIT = 12U;
/// Rows the Focus preset is allowed to define: one primary row and a compact footer.
constexpr auto FOCUS_ALLOWED_ROWS = {"toolbarTop1", "toolbarBottom1"};

/// Actions the Focus workspace must keep reachable (Plan 002, step 2).
constexpr auto FOCUS_REQUIRED_ACTIONS = {"UNDO",           "REDO",           "PEN",
                                         "ERASER",         "HIGHLIGHTER",    "SELECT",
                                         "HAND",           "INSERT_NEW_PAGE", "COLOR_SELECT",
                                         "SHOW_SIDEBAR",   "MANAGE_TOOLBAR", "CUSTOMIZE_TOOLBAR"};

/// Presets that existed before Plan 002 and must keep working unchanged.
constexpr auto PRE_PLAN_002_PRESETS = {"All in",      "Portrait",  "Minimal Left",  "Minimal Top",
                                       "Xournal++",   "Tablet mode", "Right hand Note Taking",
                                       "Toolbar Left", "Toolbar Right", "Floating Toolbox (experimental)",
                                       "Empty Toolbar"};

struct KeyFileDeleter {
    void operator()(GKeyFile* keyFile) const { g_key_file_free(keyFile); }
};
using KeyFileUPtr = std::unique_ptr<GKeyFile, KeyFileDeleter>;

auto toolbarTemplatePath() -> fs::path {
    return std::u8string(PROJECT_SOURCE_DIR) + u8"/resources-templates/toolbar.ini.in";
}

/// Parse the shipped toolbar definitions the same way ToolMenuHandler::populate() does.
void parsePredefinedToolbars(ToolbarModel& model) {
    Palette palette{GET_TESTFILE(u8"palettes/xournalpp.gpl")};
    palette.load();

    EXPECT_TRUE(model.parse(toolbarTemplatePath(), true, palette));
}

auto findToolbar(const std::vector<std::unique_ptr<ToolbarData>>& toolbars, const std::string& id)
        -> const ToolbarData* {
    auto it = std::find_if(toolbars.begin(), toolbars.end(), [&id](const auto& data) { return data->getId() == id; });
    return it == toolbars.end() ? nullptr : it->get();
}

/// Keys of a toolbar group. ToolbarData keeps its parsed contents private, so the model's
/// own serialization is used to inspect them.
auto toolbarRows(const ToolbarData& data) -> std::vector<std::string> {
    KeyFileUPtr keyFile{g_key_file_new()};
    data.saveToKeyFile(keyFile.get());

    gsize length = 0;
    gchar** keys = g_key_file_get_keys(keyFile.get(), data.getId().c_str(), &length, nullptr);

    std::vector<std::string> rows;
    for (gsize i = 0; i < length; i++) {
        if (std::string(keys[i]) != "name") {
            rows.emplace_back(keys[i]);
        }
    }
    g_strfreev(keys);
    return rows;
}

/// Items of one toolbar row, in order.
auto toolbarRowItems(const ToolbarData& data, const std::string& row) -> std::vector<std::string> {
    KeyFileUPtr keyFile{g_key_file_new()};
    g_key_file_set_list_separator(keyFile.get(), ',');
    data.saveToKeyFile(keyFile.get());

    gsize length = 0;
    gchar** items = g_key_file_get_string_list(keyFile.get(), data.getId().c_str(), row.c_str(), &length, nullptr);

    std::vector<std::string> result;
    for (gsize i = 0; i < length; i++) {
        result.emplace_back(items[i]);
    }
    g_strfreev(items);
    return result;
}

auto contains(const std::vector<std::string>& haystack, const std::string& needle) -> bool {
    return std::find(haystack.begin(), haystack.end(), needle) != haystack.end();
}

/// Every item of the Focus preset, separators excluded.
auto focusItems(const ToolbarData& focus) -> std::vector<std::string> {
    std::vector<std::string> items;
    for (const std::string& row: toolbarRows(focus)) {
        for (const std::string& item: toolbarRowItems(focus, row)) {
            if (item != "SEPARATOR" && item != "SPACER") {
                items.emplace_back(item);
            }
        }
    }
    return items;
}

}  // namespace

TEST(ToolbarPresetTest, testFocusPresetIsPredefined) {
    ToolbarModel model;
    parsePredefinedToolbars(model);
    const ToolbarData* focus = findToolbar(model.getToolbars(), FOCUS_TOOLBAR_ID);

    ASSERT_NE(focus, nullptr) << "the Focus preset is missing from toolbar.ini.in";
    EXPECT_TRUE(focus->isPredefined());
    EXPECT_EQ(focus->getName(), "Focus");
}

TEST(ToolbarPresetTest, testFocusPresetKeepsTheCoreActionsReachable) {
    ToolbarModel model;
    parsePredefinedToolbars(model);
    const ToolbarData* focus = findToolbar(model.getToolbars(), FOCUS_TOOLBAR_ID);
    ASSERT_NE(focus, nullptr);

    const std::vector<std::string> primaryRow = toolbarRowItems(*focus, "toolbarTop1");
    for (const char* action: FOCUS_REQUIRED_ACTIONS) {
        EXPECT_TRUE(contains(primaryRow, action)) << action << " is not reachable from the Focus toolbar";
    }
}

TEST(ToolbarPresetTest, testFocusPresetStaysCompact) {
    ToolbarModel model;
    parsePredefinedToolbars(model);
    const ToolbarData* focus = findToolbar(model.getToolbars(), FOCUS_TOOLBAR_ID);
    ASSERT_NE(focus, nullptr);

    std::vector<std::string> primaryActions;
    for (const std::string& item: toolbarRowItems(*focus, "toolbarTop1")) {
        if (item != "SEPARATOR") {
            primaryActions.emplace_back(item);
        }
    }
    EXPECT_LE(primaryActions.size(), FOCUS_PRIMARY_ACTION_LIMIT);

    // No second row, no side toolbars: Focus is one compact row plus a footer.
    for (const std::string& row: toolbarRows(*focus)) {
        EXPECT_TRUE(contains(std::vector<std::string>(FOCUS_ALLOWED_ROWS.begin(), FOCUS_ALLOWED_ROWS.end()), row))
                << row << " is not part of the compact Focus layout";
    }

    // The full palette and the size list stay behind the pen, eraser and highlighter
    // dropdowns (Plan 003 has not happened yet).
    for (const std::string& item: focusItems(*focus)) {
        EXPECT_NE(item.rfind("COLOR(", 0), 0) << item << " exposes the palette directly";
        EXPECT_FALSE(item == "FINE" || item == "MEDIUM" || item == "THICK" || item == "VERY_FINE" ||
                     item == "VERY_THICK")
                << item << " exposes pen sizes directly";
    }
}

TEST(ToolbarPresetTest, testFocusPresetOnlyUsesKnownItemIdentifiers) {
    ToolbarModel model;
    parsePredefinedToolbars(model);

    // The pre-existing presets were written against the registered item identifiers, so the
    // identifiers they use are the reference set a new preset may draw from.
    std::vector<std::string> knownItems;
    for (const auto& data: model.getToolbars()) {
        if (data->getId() == FOCUS_TOOLBAR_ID) {
            continue;
        }
        for (const std::string& item: focusItems(*data)) {
            if (item.rfind("COLOR(", 0) == 0) {
                continue;  // palette entries are handled by the color item, not by name
            }
            knownItems.emplace_back(item);
        }
    }

    const ToolbarData* focus = findToolbar(model.getToolbars(), FOCUS_TOOLBAR_ID);
    ASSERT_NE(focus, nullptr);
    for (const std::string& item: focusItems(*focus)) {
        EXPECT_TRUE(contains(knownItems, item)) << item << " is not used by any pre-existing toolbar preset";
    }
}

TEST(ToolbarPresetTest, testPrePlan002PresetsArePreserved) {
    ToolbarModel model;
    parsePredefinedToolbars(model);

    for (const char* id: PRE_PLAN_002_PRESETS) {
        EXPECT_NE(findToolbar(model.getToolbars(), id), nullptr) << id << " disappeared from toolbar.ini.in";
    }

    // Spot check the default Classic toolbar: Portrait keeps its two rows.
    const ToolbarData* portrait = findToolbar(model.getToolbars(), "Portrait");
    ASSERT_NE(portrait, nullptr);
    EXPECT_EQ(toolbarRowItems(*portrait, "toolbarTop1").size(), 35U);
    EXPECT_EQ(toolbarRowItems(*portrait, "toolbarBottom1").size(), 11U);
}

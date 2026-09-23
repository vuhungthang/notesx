/*
 * Xournal++
 *
 * Part of the customizable toolbars
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>  // for optional
#include <string>    // for string
#include <vector>

#include <gtk/gtk.h>  // for GtkWidget

#include "enums/Action.enum.h"
#include "util/raii/GObjectSPtr.h"
#include "util/raii/GVariantSPtr.h"

class AbstractToolItem {
public:
    // If you add a category, don't forget to give it a label in ToolbarCustomizeDialog.cpp.
    // Keep the enum contiguous
    enum class Category : unsigned char {
        FILES,
        TOOLS,
        COLORS,
        NAVIGATION,
        MISC,
        SELECTION,
        AUDIO,
        SEPARATORS,
        PLUGINS,
        ENUMERATOR_COUNT  // Keep last
    };
    AbstractToolItem(std::string id, Category cat);
    virtual ~AbstractToolItem();

    AbstractToolItem(AbstractToolItem const&) = delete;
    auto operator=(AbstractToolItem const&) -> AbstractToolItem& = delete;
    AbstractToolItem(AbstractToolItem&&) = delete;                     // Implement if desired
    auto operator=(AbstractToolItem&&) -> AbstractToolItem& = delete;  // Implement if desired

public:
    virtual xoj::util::WidgetSPtr createItem(bool horizontal) = 0;

    xoj::util::WidgetSPtr createToolItem(bool horizontal);

    const std::string& getId() const;
    Category getCategory() const;
    virtual std::string getToolDisplayName() const = 0;

    /*
     * Plan 007: what the command palette needs to offer a toolbar item as a command.
     *
     * An item that activates a GAction says so here and needs nothing else; an item that runs
     * something of its own - a plugin button calling into Lua, a slider, a spacer - says nothing
     * and is left out of the palette rather than guessed at. A target is borrowed: it belongs to
     * the item that owns it, and lives as long as the item does.
     */
    virtual auto getCommandAction() const -> std::optional<Action> { return std::nullopt; }
    /// @brief The value the action is activated with, or nullptr when it takes none
    virtual auto getCommandTarget() const -> GVariant* { return nullptr; }
    /// @brief The icon the command palette shows for this item, or an empty name
    virtual auto getCommandIconName() const -> std::string { return {}; }

    /**
     * Returns: (transfer floating)
     */
    virtual GtkWidget* getNewToolIcon() const = 0;

protected:
    std::string id;
    const Category category;
};

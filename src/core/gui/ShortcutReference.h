/*
 * Xournal++
 *
 * The shortcut reference: what the application can do, and the keys it is on (Plan 007, step 5).
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>  // for function
#include <string>
#include <vector>

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

#include "control/commands/CommandMetadata.h"  // for ReferenceRow
#include "control/commands/CommandRegistry.h"  // for CommandRegistry
#include "util/raii/GObjectSPtr.h"             // for WidgetSPtr

namespace xoj::gui {

/// The reference reads what the command layer says, and says nothing of its own about it.
using xoj::command::CommandRegistry;
using xoj::command::ReferenceRow;

/**
 * The reference (Plan 007, step 5).
 *
 * A nonmodal popover that lists the commands the application has and the keys they are on, with a
 * search field over it. Every line is read from the registry the window builds - the menus, the tool
 * items, the accelerators the application holds - each time it is opened, so there is no list of
 * shortcuts kept anywhere that could drift from what the actions actually do: changing an
 * accelerator in ActionProperties changes this, and two commands that end up on the same keys are
 * marked rather than silently listed as if both worked.
 */
class ShortcutReference {
public:
    /// The registry, read fresh every time the reference opens.
    using RegistryProvider = std::function<CommandRegistry()>;

    ShortcutReference(GtkWindow* window, RegistryProvider registryProvider);
    ~ShortcutReference();

    ShortcutReference(const ShortcutReference&) = delete;
    ShortcutReference& operator=(const ShortcutReference&) = delete;

    void open();
    void close();
    void toggle();
    auto isOpen() const -> bool;

    /// The lines on screen, in order: the ids of the commands the query left.
    auto shownIds() const -> std::vector<std::string>;
    /// What a shown line says: its title, its category, the keys it is on, and whether they clash.
    auto titleOf(const std::string& commandId) const -> std::string;
    auto categoryOf(const std::string& commandId) const -> std::string;
    auto acceleratorOf(const std::string& commandId) const -> std::string;
    auto isConflicted(const std::string& commandId) const -> bool;
    /// Every line the reference holds, before the query is applied.
    auto coveredIds() const -> std::vector<std::string>;

    [[maybe_unused]] auto getPopover() const -> GtkWidget*;
    /// The widget the user types in; the tests press its keys.
    [[maybe_unused]] auto getEntry() const -> GtkWidget*;
    [[maybe_unused]] auto getList() const -> GtkWidget*;
    /// How many lines the reference is showing, category headings aside.
    auto shownCount() const -> size_t;

private:
    void rebuild();
    void setQuery(const std::string& query);
    static bool matches(const ReferenceRow& row, const std::string& foldedQuery);
    static void setAccessibleName(GtkWidget* widget, const char* name);

    GtkWindow* window = nullptr;
    RegistryProvider registryProvider;

    xoj::util::WidgetSPtr popover;
    xoj::util::WidgetSPtr entry;
    xoj::util::WidgetSPtr list;
    xoj::util::WidgetSPtr scrolled;
    xoj::util::WidgetSPtr status;

    std::vector<ReferenceRow> rows;
    std::vector<std::string> shown;

    std::string query;
    gulong queryHandlerId = 0;
    gulong keyHandlerId = 0;
};

};  // namespace xoj::gui

/*
 * Xournal++
 *
 * The command palette: every command, found by typing (Plan 007, step 2)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <functional>  // for function
#include <memory>      // for unique_ptr
#include <optional>
#include <string>
#include <vector>

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

#include "control/commands/CommandRegistry.h"  // for CommandRegistry
#include "util/raii/GObjectSPtr.h"             // for WidgetSPtr

class Settings;

namespace xoj::command {

/**
 * The palette (Plan 007, step 2).
 *
 * It is a nonmodal popover of the window it belongs to, opened by Ctrl+K: a search entry over a list
 * of the commands the registry holds, filtered as the user types, driven entirely from the keyboard.
 * Running a row goes through the GAction the command already has, so the palette is another way in to
 * the menus and the toolbars and never a second implementation of what they do.
 *
 * It is deliberately not the document's SearchBar: that one searches the text of the open document,
 * this one searches what the application can do. Keeping them apart is what lets each answer the
 * question it is asked.
 */
class CommandPalette {
public:
    /// The registry, read fresh every time the palette opens: enabled state changes as the user works.
    using RegistryProvider = std::function<CommandRegistry()>;

    /**
     * @param window the window the popover belongs to
     * @param fallbackFocus what to focus when the widget that had the focus is gone by the time the
     *        palette closes - the canvas, so a keyboard-only user is never left with no focus at all
     * @param settings where the recent commands of this profile are kept
     */
    CommandPalette(GtkWindow* window, GtkWidget* fallbackFocus, Settings* settings, RegistryProvider registryProvider,
                   CommandRegistry::Maps maps);
    ~CommandPalette();

    CommandPalette(const CommandPalette&) = delete;
    CommandPalette& operator=(const CommandPalette&) = delete;

    /// Show the palette and put the keyboard in its entry. Doing it while it is open reads the
    /// registry again, which is what makes a command that has just become available reachable.
    void open();
    /// Hide it and give the focus back to the widget the user was on.
    void close();
    auto isOpen() const -> bool;
    /// Open it if it is closed, close it if it is open: what the shortcut does.
    void toggle();

    /// Run the command the keyboard is on. False - and nothing happens - when there is none to run,
    /// when it is disabled, or when its action refuses.
    auto activateSelected() -> bool;
    /// Move the selection by @p delta rows, clamped to what is shown.
    void moveSelection(int delta);

    /// What the palette is showing, in order: the ids of the commands the query left.
    auto shownIds() const -> std::vector<std::string>;
    /// The id of the command the keyboard is on, if any.
    auto selectedId() const -> std::optional<std::string>;
    auto isSelectedEnabled() const -> bool;
    /// What the row of this command shows, or an empty string when it is not shown.
    auto acceleratorOf(const std::string& commandId) const -> std::string;
    auto categoryOf(const std::string& commandId) const -> std::string;
    auto reasonOf(const std::string& commandId) const -> std::string;

    [[maybe_unused]] auto getPopover() const -> GtkWidget*;
    /// The widget the user types in; the tests press its keys.
    [[maybe_unused]] auto getEntry() const -> GtkWidget*;
    [[maybe_unused]] auto getList() const -> GtkWidget*;
    /// The row the keyboard is on, or the search entry when nothing is selected.
    [[maybe_unused]] auto getSelectedRow() const -> GtkWidget*;

    /// The commands the palette offers for @p query, in the order it shows them.
    auto orderFor(const CommandRegistry& commands, const std::string& query) const -> std::vector<size_t>;

private:
    void onQueryChanged();
    void rebuild();
    void selectRow(size_t row);
    /// Give the keyboard back to the widget the user was on when the palette opened.
    void giveFocusBack();
    auto rowCount() const -> size_t;
    auto selectedRowIndex() const -> std::optional<size_t>;
    auto commandFor(const std::string& commandId) const -> const CommandEntry*;
    static void setAccessibleName(GtkWidget* widget, const char* name);

    struct Row {
        std::string id;
        bool enabled = false;
        std::string title;
        std::string category;
        std::string accelerator;
        std::string reason;
    };

    GtkWindow* window = nullptr;
    xoj::util::WidgetSPtr fallbackFocus;
    Settings* settings = nullptr;
    RegistryProvider registryProvider;
    CommandRegistry::Maps maps;

    xoj::util::WidgetSPtr popover;
    xoj::util::WidgetSPtr entry;
    xoj::util::WidgetSPtr list;
    xoj::util::WidgetSPtr scrolled;

    CommandRegistry registry;
    std::vector<Row> rows;

    /// What had the focus when the palette opened. Held for as long as the palette is open, so the
    /// widget cannot go away underneath it, and released when the focus has been given back.
    xoj::util::WidgetSPtr previousFocus;

    gulong queryHandlerId = 0;
    gulong activateHandlerId = 0;
    gulong keyHandlerId = 0;
    gulong rowActivatedHandlerId = 0;
    gulong closedHandlerId = 0;
};

}  // namespace xoj::command

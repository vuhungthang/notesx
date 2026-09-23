/*
 * Xournal++
 *
 * The commands the command palette and the shortcut reference offer (Plan 007)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>  // for size_t
#include <functional>
#include <memory>  // for unique_ptr
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gio/gio.h>  // for GMenuModel, GActionMap, GAction

#include "enums/Action.enum.h"       // for Action
#include "util/raii/GVariantSPtr.h"  // for GVariantSPtr

#include "CommandMetadata.h"

class AbstractToolItem;

namespace xoj::command {

struct CommandEntry;

/// Which of the window's action maps an action is looked up in.
enum class ActionScope {
    WINDOW,       ///< the GtkApplicationWindow's own actions: "win.save"
    APPLICATION,  ///< the GtkApplication's actions: "app.quit"
};

/// The accelerators GTK currently holds for one (detailed) action name, e.g. "win.new-file".
using AcceleratorLookup = std::function<std::vector<std::string>(const std::string& detailedActionName)>;

/// Why a command cannot be run right now, when the action it activates can say.
using DisabledReasonLookup = std::function<std::optional<std::string>(const CommandEntry& entry)>;

/// The synonyms an action declares, empty when it declares none.
using KeywordLookup = std::function<std::vector<std::string>(const CommandEntry& entry)>;

/**
 * Plan 007, step 1: one command - what it is and what activating it does.
 *
 * The metadata is what the user reads; the rest is how the command reaches the action that already
 * exists for it, so the palette is an alternate way in and never a second implementation.
 */
struct CommandEntry {
    CommandMetadata metadata;
    ActionScope scope = ActionScope::WINDOW;
    std::string action;                 ///< the action's name inside its map, without the namespace
    xoj::util::GVariantSPtr target;     ///< what it is activated with; null when it takes nothing
    std::optional<Action> knownAction;  ///< the ActionDatabase entry, when the action is one of them
};

/**
 * Where the commands come from and where they go.
 *
 * Everything in it is read from something that already exists: the menu model GTK builds from
 * ui/mainmenubar.xml (which holds the plugin entries too), the toolbar items the tool menu handler
 * owns, the accelerators the application holds for an action. addCommand() is the extension point
 * for a command that is not in a menu - the shortcut reference is one - and it is the same door a
 * future plugin API would use, so no plugin has to change for its menu entries to appear here.
 */
class CommandRegistry {
public:
    /// The action maps a command is looked up in and activated through.
    struct Maps {
        GActionMap* window = nullptr;
        GActionMap* application = nullptr;

        auto mapFor(ActionScope scope) const -> GActionMap*;
    };

    explicit CommandRegistry(AcceleratorLookup acceleratorLookup = {}, DisabledReasonLookup disabledReasonLookup = {},
                             KeywordLookup keywordLookup = {});

    /// Every command the menu names, in menu order, under the categories its submenus are.
    void addFromMenuModel(GMenuModel* menu);
    /// Every toolbar item that activates an action.
    void addFromToolItems(const std::vector<std::unique_ptr<AbstractToolItem>>& items);
    /// The extension point: a command that is not offered by a menu.
    void addCommand(CommandEntry entry);

    /**
     * The accelerator the application currently holds for an action, as the reference displays it,
     * or an empty string when it holds none.
     *
     * A command that came from a menu or a tool item has its accelerator filled in as it is read;
     * a command added directly has to ask. It asks the same live lookup - what GTK holds for the
     * action right now - so a directly added command is remappable and shown exactly like one from
     * a menu, and the answer is never a copy baked into the command.
     */
    auto acceleratorFor(const std::string& detailedActionName) const -> std::string;

    const std::vector<CommandEntry>& all() const;
    /// The metadata of every command, for the search and the reference.
    auto metadata() const -> std::vector<CommandMetadata>;
    auto findById(std::string_view id) const -> const CommandEntry*;
    /// What is wrong with the registry - ids, titles, categories. Empty when nothing is.
    auto problems() const -> std::vector<std::string>;

    /**
     * What makes two commands the same command: the action they run and the value it is run with,
     * in the map they are looked up in.
     *
     * The same action with the same target offered from two places - the menu entry and the tool
     * button that reaches it - is one command, not two, and it must be one however it is reached:
     * the palette would otherwise list "Undo" twice, and the shortcut reference would report an
     * accelerator conflict with itself.
     */
    static auto commandKey(const CommandEntry& entry) -> std::string;

    /// The GAction a command activates, or nullptr when its map does not hold one.
    static auto lookupAction(const CommandEntry& entry, const Maps& maps) -> GAction*;
    /// Whether the command can be run now. A command with no action cannot.
    static auto isEnabled(const CommandEntry& entry, const Maps& maps) -> bool;
    /**
     * Run the command through its own GAction.
     *
     * @return false, without touching anything, when the action is not there or is disabled
     */
    static auto activate(const CommandEntry& entry, const Maps& maps) -> bool;

    /// Why the action says it is unavailable, whether or not it is right now.
    auto disabledReason(const CommandEntry& entry) const -> std::optional<std::string>;
    /// The explanation to show: only for a command that cannot be run now.
    auto disabledReason(const CommandEntry& entry, const Maps& maps) const -> std::optional<std::string>;

private:
    /// Walk one level of a menu model: items with an action become commands, links are followed.
    void addMenuLevel(GMenuModel* model, const std::string& category);
    /// Add the command unless the action it runs is already there with the target it runs it with.
    void appendCommand(CommandEntry entry);

    std::vector<CommandEntry> commands;
    AcceleratorLookup acceleratorLookup;
    DisabledReasonLookup disabledReasonLookup;
    KeywordLookup keywordLookup;
};

}  // namespace xoj::command

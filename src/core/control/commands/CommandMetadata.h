/*
 * Xournal++
 *
 * The user-visible metadata of a command (Plan 007)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <cstddef>  // for size_t
#include <string>   // for string
#include <string_view>
#include <vector>  // for vector

namespace xoj::command {

/**
 * Plan 007, step 1: what the command palette and the shortcut reference say about a command.
 *
 * Every field is a description of something that already exists elsewhere: the label and the
 * accelerator of a menu entry of ui/mainmenubar.xml, the accelerators GTK holds for an action, the
 * name the action is registered under, the display name of a toolbar item. Nothing here is a second
 * list of labels or accelerators to keep in step with the menus.
 */
struct CommandMetadata {
    std::string id;                     ///< stable identity of the command; unique in a registry
    std::string title;                  ///< translated title
    std::vector<std::string> keywords;  ///< optional synonyms; empty is the normal case
    std::string category;               ///< translated group the command belongs to
    std::string accelerator;            ///< display form of the primary accelerator; empty if none
    std::string icon;                   ///< icon name; empty if the command does not carry one
    std::string actionName;             ///< the GAction it activates, e.g. "win.save"
};

/**
 * What is wrong with a set of commands, in the order they were given; empty when nothing is.
 *
 * A command that the palette can show needs a unique id, a nonempty title and a nonempty category,
 * and this is where that is said out loud rather than assumed.
 */
auto validateCommands(const std::vector<CommandMetadata>& commands) -> std::vector<std::string>;

/**
 * The indices of the commands that match `query`, best first.
 *
 * An empty or blank query matches everything, in the order the registry holds it. Otherwise every
 * token of the query has to match, and the score of a command is the sum of the best score of each
 * of its tokens. Only ties keep the registry order.
 */
auto rankCommands(const std::vector<CommandMetadata>& commands, std::string_view query) -> std::vector<size_t>;

/// One accelerator that more than one command claims.
struct AcceleratorConflict {
    std::string accelerator;              ///< the display form the commands share
    std::vector<std::string> commandIds;  ///< the distinct actions that claim it, registry order
};

/**
 * The accelerators that more than one action claims, in first-appearance order.
 *
 * Two entries for the same action - a menu entry and the tool button that reach it - are not a
 * conflict: the value is the same action, offered twice.
 */
auto findAcceleratorConflicts(const std::vector<CommandMetadata>& commands) -> std::vector<AcceleratorConflict>;

/**
 * The form of `text` the matcher compares against: lower case and without accents, so that a query
 * typed without a diacritic still finds a translated title that carries one.
 */
auto foldedForSearch(std::string_view text) -> std::string;

/// One line of the shortcut reference: a command, and the keys the application holds for it.
struct ReferenceRow {
    std::string id;           ///< the command the line is about
    std::string title;        ///< translated title
    std::string category;     ///< translated group, so the reference can group the way the menus do
    std::string accelerator;  ///< display form of the keys; empty when the command is on none
    bool conflicted = false;  ///< more than one command claims those keys
};

/**
 * The shortcut reference, generated from the command metadata: the commands in the order the registry
 * holds them, each with the keys the application holds for it, marked where those keys are claimed
 * twice.
 *
 * Nothing in the reference is written down a second time. What an accelerator is here is what the
 * action says it is, so changing it in ActionProperties changes the reference - there is no list to
 * keep in step.
 */
auto buildShortcutReference(const std::vector<CommandMetadata>& commands) -> std::vector<ReferenceRow>;

}  // namespace xoj::command

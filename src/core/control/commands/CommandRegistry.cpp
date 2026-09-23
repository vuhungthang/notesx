#include "CommandRegistry.h"

#include <algorithm>  // for find_if
#include <set>        // for set
#include <utility>    // for move

#include <gobject/gobject.h>  // for g_object_ref

#include "gui/toolbarMenubar/AbstractToolItem.h"       // for AbstractToolItem
#include "gui/toolbarMenubar/ToolItemCategoryLabel.h"  // for toolItemCategoryLabel
#include "util/Assert.h"
#include "util/i18n.h"  // for _

#include "AcceleratorDisplay.h"

namespace xoj::command {

namespace {

/// The namespace a command's action lives under, as ui/mainmenubar.xml writes it.
constexpr auto WINDOW_NAMESPACE = "win.";
constexpr auto APPLICATION_NAMESPACE = "app.";

/// The item attribute `name` as a string, or nothing when the item does not carry it.
auto stringAttribute(GMenuModel* model, int index, const char* name) -> std::optional<std::string> {
    xoj::util::GVariantSPtr value(g_menu_model_get_item_attribute_value(model, index, name, G_VARIANT_TYPE_STRING),
                                  xoj::util::adopt);
    if (!value) {
        return std::nullopt;
    }
    return std::string(g_variant_get_string(value.get(), nullptr));
}

/**
 * The title a menu label asks for: the mnemonic underscore is not part of what the user reads, and
 * a doubled one is the escaped single one.
 */
auto stripMnemonics(const std::string& label) -> std::string {
    std::string title;
    title.reserve(label.size());
    for (size_t i = 0; i < label.size(); i++) {
        if (label[i] == '_' && i + 1 < label.size() && label[i + 1] == '_') {
            title.push_back('_');
            i++;
        } else if (label[i] != '_') {
            title.push_back(label[i]);
        }
    }
    return title;
}

/// One command's worth of a menu entry, before the accelerator is filled in.
struct MenuAction {
    bool valid = false;
    std::string id;         ///< the action attribute as written, plus a target when it has one of its own
    std::string qualified;  ///< "win.save"
    ActionScope scope = ActionScope::WINDOW;
    std::string name;       ///< "save"
    xoj::util::GVariantSPtr target;
    bool targetInName = false;
};

auto parseMenuAction(const std::string& attribute, GMenuModel* model, int index) -> MenuAction {
    MenuAction parsed;

    gchar* name = nullptr;
    GVariant* target = nullptr;
    GError* error = nullptr;
    if (!g_action_parse_detailed_name(attribute.c_str(), &name, &target, &error)) {
        // A menu entry whose action name GTK cannot make sense of is not a command: it is a
        // placeholder, like the "No recent files" entry that names an action nobody has.
        if (error != nullptr) {
            g_error_free(error);
        }
        return parsed;
    }
    parsed.qualified = name;
    g_free(name);

    parsed.targetInName = target != nullptr;
    if (target == nullptr) {
        // A menu item can also carry its target as an attribute of its own.
        target = g_menu_model_get_item_attribute_value(model, index, G_MENU_ATTRIBUTE_TARGET, nullptr);
    }
    if (target != nullptr) {
        parsed.target.reset(target, xoj::util::adopt);
    }

    if (parsed.qualified.starts_with(APPLICATION_NAMESPACE)) {
        parsed.scope = ActionScope::APPLICATION;
        parsed.name = parsed.qualified.substr(std::string(APPLICATION_NAMESPACE).size());
    } else if (parsed.qualified.starts_with(WINDOW_NAMESPACE)) {
        parsed.scope = ActionScope::WINDOW;
        parsed.name = parsed.qualified.substr(std::string(WINDOW_NAMESPACE).size());
    } else {
        // No namespace: the action is named as the window's, which is where an unqualified menu
        // action is looked up as well.
        parsed.name = parsed.qualified;
    }

    if (parsed.targetInName) {
        parsed.id = attribute;
    } else if (parsed.target) {
        std::string printed(g_variant_print(parsed.target.get(), FALSE));
        parsed.id = parsed.qualified + "(" + printed + ")";
    } else {
        parsed.id = parsed.qualified;
    }

    parsed.valid = !parsed.name.empty();
    return parsed;
}

}  // namespace

auto CommandRegistry::Maps::mapFor(ActionScope scope) const -> GActionMap* {
    return scope == ActionScope::APPLICATION ? this->application : this->window;
}

CommandRegistry::CommandRegistry(AcceleratorLookup acceleratorLookup, DisabledReasonLookup disabledReasonLookup,
                                 KeywordLookup keywordLookup):
        acceleratorLookup(std::move(acceleratorLookup)),
        disabledReasonLookup(std::move(disabledReasonLookup)),
        keywordLookup(std::move(keywordLookup)) {}

void CommandRegistry::addFromMenuModel(GMenuModel* menu) {
    if (menu == nullptr) {
        return;
    }
    addMenuLevel(menu, std::string());
}

void CommandRegistry::addMenuLevel(GMenuModel* model, const std::string& category) {
    const int count = g_menu_model_get_n_items(model);

    for (int index = 0; index < count; index++) {
        const std::optional<std::string> label = stringAttribute(model, index, G_MENU_ATTRIBUTE_LABEL);

        /*
         * The links come first: what is inside a top level submenu is grouped under that submenu's
         * name, and a section holds items of the level it sits in rather than a group of its own.
         */
        for (const char* linkType: {G_MENU_LINK_SUBMENU, G_MENU_LINK_SECTION}) {
            GMenuModel* link = g_menu_model_get_item_link(model, index, linkType);
            if (link == nullptr) {
                continue;
            }
            const bool namesItsOwnGroup = std::string_view(linkType) == G_MENU_LINK_SUBMENU && category.empty() && label;
            addMenuLevel(link, namesItsOwnGroup ? stripMnemonics(*label) : category);
            g_object_unref(link);
        }

        const std::optional<std::string> action = stringAttribute(model, index, G_MENU_ATTRIBUTE_ACTION);
        if (!action || !label) {
            continue;  // a separator, a heading, a placeholder: nothing to run
        }

        MenuAction parsed = parseMenuAction(*action, model, index);
        if (!parsed.valid) {
            continue;
        }

        CommandEntry entry;
        entry.metadata.id = parsed.id;
        entry.metadata.title = stripMnemonics(*label);
        entry.metadata.category = category.empty() ? _("Other") : category;
        entry.metadata.actionName = parsed.qualified;
        entry.scope = parsed.scope;
        entry.action = parsed.name;
        entry.target = std::move(parsed.target);
        /*
         * Whether the action lives in the window's namespace or the application's is written on the
         * action database, not on the menu entry; what a known action buys a command here is its
         * keywords and the reason it gives for being unavailable.
         */
        if (const std::optional<Action> known = Action_fromString(entry.action)) {
            entry.knownAction = known;
        }

        if (this->acceleratorLookup) {
            for (const std::string& accelerator: this->acceleratorLookup(parsed.qualified)) {
                entry.metadata.accelerator = formatAcceleratorForDisplay(accelerator);
                break;
            }
        }
        if (entry.metadata.accelerator.empty()) {
            // Nothing is registered for this action, so the accelerator the menu entry itself
            // carries is the one there is.
            if (const std::optional<std::string> accel = stringAttribute(model, index, "accel")) {
                entry.metadata.accelerator = formatAcceleratorForDisplay(*accel);
            }
        }

        if (this->keywordLookup) {
            entry.metadata.keywords = this->keywordLookup(entry);
        }

        this->commands.emplace_back(std::move(entry));
    }
}

void CommandRegistry::addFromToolItems(const std::vector<std::unique_ptr<AbstractToolItem>>& items) {
    for (const std::unique_ptr<AbstractToolItem>& item: items) {
        const std::optional<Action> action = item->getCommandAction();
        if (!action) {
            // An item that runs something of its own - a plugin button calling into Lua, a slider,
            // a spacer - is not a command and is left out rather than guessed at.
            continue;
        }

        CommandEntry entry;
        entry.metadata.actionName = std::string(WINDOW_NAMESPACE) + Action_toString(*action);
        entry.metadata.id = entry.metadata.actionName + ":" + item->getId();
        entry.metadata.title = item->getToolDisplayName();
        entry.metadata.category = xoj::gui::toolItemCategoryLabel(item->getCategory());
        entry.metadata.icon = item->getCommandIconName();
        entry.scope = ActionScope::WINDOW;
        entry.action = Action_toString(*action);
        entry.knownAction = *action;
        if (GVariant* target = item->getCommandTarget(); target != nullptr) {
            entry.target.reset(target, xoj::util::ref);
        }

        if (this->acceleratorLookup) {
            for (const std::string& accelerator: this->acceleratorLookup(entry.metadata.actionName)) {
                entry.metadata.accelerator = formatAcceleratorForDisplay(accelerator);
                break;
            }
        }
        if (this->keywordLookup) {
            entry.metadata.keywords = this->keywordLookup(entry);
        }

        this->commands.emplace_back(std::move(entry));
    }
}

void CommandRegistry::addCommand(CommandEntry entry) { this->commands.emplace_back(std::move(entry)); }

const std::vector<CommandEntry>& CommandRegistry::all() const { return this->commands; }

auto CommandRegistry::metadata() const -> std::vector<CommandMetadata> {
    std::vector<CommandMetadata> out;
    out.reserve(this->commands.size());
    for (const CommandEntry& command: this->commands) {
        out.emplace_back(command.metadata);
    }
    return out;
}

auto CommandRegistry::findById(std::string_view id) const -> const CommandEntry* {
    auto found = std::find_if(this->commands.begin(), this->commands.end(),
                              [id](const CommandEntry& command) { return command.metadata.id == id; });
    return found == this->commands.end() ? nullptr : &*found;
}

auto CommandRegistry::problems() const -> std::vector<std::string> { return validateCommands(this->metadata()); }

auto CommandRegistry::lookupAction(const CommandEntry& entry, const Maps& maps) -> GAction* {
    GActionMap* map = maps.mapFor(entry.scope);
    if (map == nullptr || entry.action.empty()) {
        return nullptr;
    }
    return g_action_map_lookup_action(map, entry.action.c_str());
}

auto CommandRegistry::isEnabled(const CommandEntry& entry, const Maps& maps) -> bool {
    GAction* action = lookupAction(entry, maps);
    return action != nullptr && g_action_get_enabled(action);
}

auto CommandRegistry::activate(const CommandEntry& entry, const Maps& maps) -> bool {
    GAction* action = lookupAction(entry, maps);
    if (action == nullptr || !g_action_get_enabled(action)) {
        // The action is gone or is not available: the command is not run, and nothing else happens.
        return false;
    }
    g_action_activate(action, entry.target.get());
    return true;
}

auto CommandRegistry::disabledReason(const CommandEntry& entry) const -> std::optional<std::string> {
    if (!this->disabledReasonLookup) {
        return std::nullopt;
    }
    return this->disabledReasonLookup(entry);
}

auto CommandRegistry::disabledReason(const CommandEntry& entry, const Maps& maps) const -> std::optional<std::string> {
    if (isEnabled(entry, maps)) {
        return std::nullopt;
    }
    return this->disabledReason(entry);
}

}  // namespace xoj::command

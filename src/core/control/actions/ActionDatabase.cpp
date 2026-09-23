#include "ActionDatabase.h"

#include <array>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <gobject/gsignal.h>

#include "control/Control.h"
#include "gui/MainWindow.h"
#include "util/GVariantTemplate.h"
#include "util/safe_casts.h"  // for to_underlying Todo(cpp20) remove

#include "ActionProperties.h"

#ifdef DEBUG_ACTION_DB
#define START_ROW "   * " << std::left << std::setw(30) << std::boolalpha << Action_toString(a)
#endif

template <Action a, class U = void>
struct InitiallyEnabled {
    static inline void setup(ActionDatabase* db, Control*) { db->enableAction(a, true); }
};
template <Action a>
struct InitiallyEnabled<a, std::void_t<decltype(&ActionProperties<a>::initiallyEnabled)>> {
    static inline void setup(ActionDatabase* db, Control* ctrl) {
        db->enableAction(a, ActionProperties<a>::initiallyEnabled(ctrl));
    }
};

/// Plan 007: the words an action declares for a command search, beside the title the menu shows it under.
template <Action a>
static auto declaredKeywords() -> std::vector<std::string> {
    std::vector<std::string> words;
    if constexpr (has_keywords<a>::value) {
        for (const char* const* word = ActionProperties<a>::keywords; *word != nullptr; word++) {
            words.emplace_back(*word);
        }
    }
    return words;
}

/// Plan 007: why an action says it cannot be used at the moment, for the actions that say anything.
template <Action a>
static auto declaredDisabledReason(Control* ctrl) -> std::optional<std::string> {
    if constexpr (has_disabled_reason<a>::value) {
        return ActionProperties<a>::disabledReason(ctrl);
    } else {
        return std::nullopt;
    }
}

/// One entry per action, gathered at compile time so that reading one is a lookup and not a walk.
template <size_t... As>
static auto keywordTable(std::index_sequence<As...>) {
    using Getter = std::vector<std::string> (*)();
    return std::array<Getter, sizeof...(As)>{&declaredKeywords<static_cast<Action>(As)>...};
}
template <size_t... As>
static auto disabledReasonTable(std::index_sequence<As...>) {
    using Getter = std::optional<std::string> (*)(Control*);
    return std::array<Getter, sizeof...(As)>{&declaredDisabledReason<static_cast<Action>(As)>...};
}

static constexpr size_t ACTION_COUNT = xoj::to_underlying(Action::ENUMERATOR_COUNT);

auto ActionDatabase::getKeywords(Action a) const -> std::vector<std::string> {
    static const auto table = keywordTable(std::make_index_sequence<ACTION_COUNT>());
    return table[xoj::to_underlying(a)]();
}

auto ActionDatabase::getDisabledReason(Action a) const -> std::optional<std::string> {
    static const auto table = disabledReasonTable(std::make_index_sequence<ACTION_COUNT>());
    return table[xoj::to_underlying(a)](this->control);
}


class ActionDatabase::Populator {
    /// Choose the right action namespace
    template <Action a, class U = void>
    struct ActionNamespace {
        static constexpr auto ACTION_NAMESPACE = "win.";
        static void addToActionMap(ActionDatabase* db) {
            g_action_map_add_action(G_ACTION_MAP(db->win), G_ACTION(db->gActions[a].get()));
        }
    };
    template <Action a>
    struct ActionNamespace<
            a, std::enable_if_t<std::is_same_v<typename ActionProperties<a>::app_namespace, std::true_type>, void>> {
        static constexpr auto ACTION_NAMESPACE = "app.";
        static void addToActionMap(ActionDatabase* db) {
            g_action_map_add_action(G_ACTION_MAP(gtk_window_get_application(GTK_WINDOW(db->win))),
                                    G_ACTION(db->gActions[a].get()));
        }
    };

    /**
     * Accelerators<a>::setup(ctrl); will setup the accelerators listed in ActionProperties<a>::accelerators (if any)
     */
    template <Action a, class U = void>
    struct Accelerators {
        static inline void setup(Control*) {}
    };
    template <Action a>
    struct Accelerators<a, std::void_t<decltype(&ActionProperties<a>::accelerators)>> {
        static_assert(std::is_array_v<decltype(ActionProperties<a>::accelerators)>);
        static_assert(
                std::is_same_v<std::remove_extent_t<decltype(ActionProperties<a>::accelerators)>, const char* const>);
        static_assert(
                ActionProperties<a>::accelerators[std::extent_v<decltype(ActionProperties<a>::accelerators)> - 1] ==
                        nullptr,
                "ActionProperties<a>::accelerators must be null terminated");

        static inline void setup(Control* ctrl) {
            // Todo(cpp20) constexpr this concatenation
            std::string fullActionName = ActionNamespace<a>::ACTION_NAMESPACE;
            fullActionName += Action_toString(a);
            gtk_application_set_accels_for_action(GTK_APPLICATION(gtk_window_get_application(ctrl->getGtkWindow())),
                                                  fullActionName.c_str(), ActionProperties<a>::accelerators);
        }
    };

    template <Action a>
    static inline void finishSetup(ActionDatabase* db, const char* signal) {
        db->signalIds[a] = g_signal_connect(G_OBJECT(db->gActions[a].get()), signal,
                                            G_CALLBACK(ActionProperties<a>::callback), db->control);
        ActionNamespace<a>::addToActionMap(db);
        // g_action_map_add_action(G_ACTION_MAP(db->win), G_ACTION(db->gActions[a].get()));
        Accelerators<a>::setup(db->control);
        InitiallyEnabled<a>::setup(db, db->control);
    }

    // Actions without state or parameter
    template <Action a, std::enable_if_t<!has_param<a>() && !has_state<a>(), bool> = true>
    static void assign(ActionDatabase* db) {

        ACTIONDB_PRINT_DEBUG(START_ROW << " |               |            |");

        db->gActions[a].reset(g_simple_action_new(Action_toString(a), nullptr), xoj::util::adopt);

        finishSetup<a>(db, "activate");
    }

    // Actions with parameter but no state
    template <Action a, std::enable_if_t<has_param<a>() && !has_state<a>(), bool> = true>
    static void assign(ActionDatabase* db) {

        ACTIONDB_PRINT_DEBUG(START_ROW << " |               | type = \""
                                       << (const char*)gVariantType<typename ActionProperties<a>::parameter_type>()
                                       << "\" |");

        db->gActions[a].reset(
                g_simple_action_new(Action_toString(a), gVariantType<typename ActionProperties<a>::parameter_type>()),
                xoj::util::adopt);

        finishSetup<a>(db, "activate");
    }

    // Actions with a state but no parameter
    template <Action a, std::enable_if_t<!has_param<a>() && has_state<a>(), bool> = true>
    static void assign(ActionDatabase* db) {

        ACTIONDB_PRINT_DEBUG(START_ROW << " | \""
                                       << (const char*)gVariantType<typename ActionProperties<a>::state_type>()
                                       << "\" = " << std::setw(7) << ActionProperties<a>::initialState(db->control)
                                       << " |            |");

        db->gActions[a].reset(g_simple_action_new_stateful(Action_toString(a), nullptr,
                                                           makeGVariant<typename ActionProperties<a>::state_type>(
                                                                   ActionProperties<a>::initialState(db->control))),
                              xoj::util::adopt);

        finishSetup<a>(db, "change-state");
    }

    // Actions with both state and parameter (with matching type)
    template <Action a, std::enable_if_t<has_param<a>() && has_state<a>(), bool> = true>
    static void assign(ActionDatabase* db) {
        static_assert(
                std::is_same_v<typename ActionProperties<a>::state_type, typename ActionProperties<a>::parameter_type>);

        ACTIONDB_PRINT_DEBUG(
                START_ROW << " | \"" << (const char*)gVariantType<typename ActionProperties<a>::state_type>()
                          << "\" = " << std::setw(7) << ActionProperties<a>::initialState(db->control) << " | type = \""
                          << (const char*)gVariantType<typename ActionProperties<a>::state_type>() << "\" |");

        db->gActions[a].reset(g_simple_action_new_stateful(Action_toString(a),
                                                           gVariantType<typename ActionProperties<a>::state_type>(),
                                                           makeGVariant<typename ActionProperties<a>::state_type>(
                                                                   ActionProperties<a>::initialState(db->control))),
                              xoj::util::adopt);

        finishSetup<a>(db, "change-state");
    }

    template <size_t... As>
    static void populateImpl(std::index_sequence<As...>, ActionDatabase* db) {
        ((assign<static_cast<Action>(As)>(db)), ...);
    }


public:
    static void populate(ActionDatabase* db);
};

void ActionDatabase::Populator::populate(ActionDatabase* db) {
    ACTIONDB_PRINT_DEBUG("Populating ActionDatabase:");
    ACTIONDB_PRINT_DEBUG("        ACTION NAME:                |  STATE INIT   | PARAM TYPE |");

    populateImpl(std::make_index_sequence<xoj::to_underlying(Action::ENUMERATOR_COUNT)>(), db);
}

ActionDatabase::ActionDatabase(Control* control):
        control(control), win(GTK_APPLICATION_WINDOW(control->getWindow()->getWindow())) {
    Populator::populate(this);
}

ActionDatabase::~ActionDatabase() {
    auto sig = signalIds.begin();
    for (auto& a: gActions) {
        /**
         * The GAction's might still be referenced elsewhere (e.g. by the GtkApplicationWindow), so we need to
         * disconnect the signals in case a callback is called and the Control instance has been destroyed
         */
        g_signal_handler_disconnect(a.get(), *sig++);
    }
}


void ActionDatabase::enableAction(Action action, bool enable) {
    xoj_assert(gActions[action]);
    g_simple_action_set_enabled(gActions[action].get(), enable);
    ACTIONDB_PRINT_DEBUG((enable ? "Enabling Action \"" : "Disabling Action\"") << Action_toString(action) << "\"");
}

auto ActionDatabase::getAction(Action a) const -> ActionRef { return gActions[a]; }

bool ActionDatabase::isActionEnabled(Action a) const {
    xoj_assert(gActions[a]);
    return g_action_get_enabled(G_ACTION(gActions[a].get()));
}

void ActionDatabase::disableAll() {
    for (auto&& a: gActions) {
        g_simple_action_set_enabled(a.get(), false);
    }
}

template <size_t... As>
static void resetEnableStatusImpl(std::index_sequence<As...>, ActionDatabase* db, Control* ctrl) {
    ((InitiallyEnabled<static_cast<Action>(As)>::setup(db, ctrl)), ...);
}

void ActionDatabase::resetEnableStatus() {
    resetEnableStatusImpl(std::make_index_sequence<xoj::to_underlying(Action::ENUMERATOR_COUNT)>(), this, control);
}

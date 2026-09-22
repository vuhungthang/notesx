#include "WorkspaceSubmenu.h"

#include <string>  // for string

#include <glib-object.h>  // for G_CALLBACK, g_signal_connect

#include "control/settings/Settings.h"  // for Settings
#include "gui/MainWindow.h"             // for MainWindow
#include "gui/menus/StaticAssertActionNamespace.h"
#include "util/i18n.h"  // for _

#include "Menubar.h"

namespace {
constexpr auto G_ACTION_NAMESPACE = "win.";
constexpr auto SELECT_ACTION_NAME = "select-workspace";
constexpr auto USE_CLASSIC_ACTION_NAME = "use-classic-layout";
/// id from ui/mainmenubar.xml
constexpr auto SUBMENU_ID = "menuViewWorkspace";

auto createWorkspaceMenuItem(const char* label, WorkspaceMode mode) {
    std::string action = G_ACTION_NAMESPACE;
    action += SELECT_ACTION_NAME;
    action += "('";
    action += workspaceModeToString(mode);
    action += "')";

    return xoj::util::GObjectSPtr<GMenuItem>(g_menu_item_new(label, action.c_str()), xoj::util::adopt);
}

void workspaceSelectionMenuChangeStateCallback(GSimpleAction* ga, GVariant* parameter, MainWindow* window) {
    g_simple_action_set_state(ga, parameter);
    window->setWorkspace(workspaceModeFromString(g_variant_get_string(parameter, nullptr)));
}

void useClassicLayoutCallback(GSimpleAction*, GVariant*, MainWindow* window) {
    window->setWorkspace(WorkspaceMode::CLASSIC);
}
};  // namespace

WorkspaceSubmenu::WorkspaceSubmenu(MainWindow* win, Settings* settings):
        settings(settings),
        selectAction(g_simple_action_new_stateful(SELECT_ACTION_NAME, G_VARIANT_TYPE_STRING,
                                                  g_variant_new_string(
                                                          workspaceModeToString(settings->getWorkspaceMode()))),
                     xoj::util::adopt),
        useClassicAction(g_simple_action_new(USE_CLASSIC_ACTION_NAME, nullptr), xoj::util::adopt),
        workspaceSection(g_menu_new(), xoj::util::adopt),
        escapeRouteSection(g_menu_new(), xoj::util::adopt) {
    g_menu_append_item(this->workspaceSection.get(), createWorkspaceMenuItem(_("Focus"), WorkspaceMode::FOCUS).get());
    g_menu_append_item(this->workspaceSection.get(),
                       createWorkspaceMenuItem(_("Classic"), WorkspaceMode::CLASSIC).get());

    g_signal_connect(G_OBJECT(selectAction.get()), "change-state", G_CALLBACK(workspaceSelectionMenuChangeStateCallback),
                     win);
    g_signal_connect(G_OBJECT(useClassicAction.get()), "activate", G_CALLBACK(useClassicLayoutCallback), win);
    static_assert(is_action_namespace_match<decltype(win)>(G_ACTION_NAMESPACE));
    g_action_map_add_action(G_ACTION_MAP(win->getWindow()), G_ACTION(selectAction.get()));
    g_action_map_add_action(G_ACTION_MAP(win->getWindow()), G_ACTION(useClassicAction.get()));

    update();
}

WorkspaceSubmenu::~WorkspaceSubmenu() = default;

void WorkspaceSubmenu::setDisabled(bool disabled) {
    g_simple_action_set_enabled(selectAction.get(), !disabled);
    g_simple_action_set_enabled(useClassicAction.get(), !disabled);
}

void WorkspaceSubmenu::addToMenubar(Menubar& menubar) {
    GMenu* submenu = menubar.get<GMenu>(SUBMENU_ID, [](auto* p) { return G_MENU(p); });
    g_menu_append_section(submenu, nullptr, G_MENU_MODEL(workspaceSection.get()));
    g_menu_append_section(submenu, nullptr, G_MENU_MODEL(escapeRouteSection.get()));
}

void WorkspaceSubmenu::update() {
    // Does not fire a "change-state" signal
    g_simple_action_set_state(selectAction.get(),
                              g_variant_new_string(workspaceModeToString(settings->getWorkspaceMode())));

    // The escape route only makes sense while Focus is active: in Classic it would be a no-op.
    g_menu_remove_all(escapeRouteSection.get());
    if (settings->getWorkspaceMode() == WorkspaceMode::FOCUS) {
        g_menu_append(escapeRouteSection.get(), _("Use Classic layout"), "win.use-classic-layout");
    }
}

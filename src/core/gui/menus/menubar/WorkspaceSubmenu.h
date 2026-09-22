/*
 * Xournal++
 *
 * Submenu for the workspace switch (Plan 002)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gio/gio.h>  // for GMenu, GSimpleAction

#include "util/raii/GObjectSPtr.h"

#include "AbstractSubmenu.h"

class MainWindow;
class Settings;

/**
 * Plan 002: View > Workspace.
 *
 * The two radio entries select the Focus or the Classic workspace, and while Focus is active
 * an explicit "Use Classic layout" entry is offered as the escape route back to the fully
 * configurable layouts. Both entries drive MainWindow::setWorkspace(), so the switch takes
 * effect immediately and can be undone from the same menu.
 */
class WorkspaceSubmenu final: public Submenu {
public:
    WorkspaceSubmenu(MainWindow* win, Settings* settings);
    ~WorkspaceSubmenu();

    /** Reflect the active workspace and whether the escape route is offered. */
    void update();

    void setDisabled(bool disabled) override;
    void addToMenubar(Menubar& menubar) override;

private:
    Settings* settings = nullptr;

    xoj::util::GObjectSPtr<GSimpleAction> selectAction;      ///< win.select-workspace(mode)
    xoj::util::GObjectSPtr<GSimpleAction> useClassicAction;  ///< win.use-classic-layout

    xoj::util::GObjectSPtr<GMenu> workspaceSection;
    xoj::util::GObjectSPtr<GMenu> escapeRouteSection;
};

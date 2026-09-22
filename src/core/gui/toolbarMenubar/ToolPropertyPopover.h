/*
 * Xournal++
 *
 * The popover that carries a tool's property panel
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

#include "gui/PopoverFactory.h"  // for ToolPopoverFactory

#include "ToolPropertyPanel.h"     // for PresetListListener, ToolPropertyPanel
#include "ToolPropertyProvider.h"  // for ToolPropertyProvider

class Settings;
class ToolConfigAdapter;

/**
 * Plan 003, step 3: builds the property popover of one tool.
 *
 * One factory per tool. It creates a fresh panel for every popover, because the toolbar can be
 * rebuilt (a toolbar switch, a customisation) and the old popovers are destroyed with it.
 */
class ToolPropertyPopoverFactory final: public ToolPopoverFactory {
public:
    ToolPropertyPopoverFactory(ToolConfigAdapter& adapter, Settings& settings, ToolPropertyProvider& provider,
                               GtkWindow* parent, PresetListListener* presetsListener);
    ~ToolPropertyPopoverFactory() override;

    ToolType getToolType() const override;
    bool isActiveTool() const override;
    GtkWidget* createPopover() const override;

private:
    /// Free the panel together with the popover that owns it.
    static void destroyPanel(gpointer data);

    ToolConfigAdapter& adapter;
    Settings& settings;
    ToolPropertyProvider& provider;
    GtkWindow* parent;
    PresetListListener* presetsListener;
};

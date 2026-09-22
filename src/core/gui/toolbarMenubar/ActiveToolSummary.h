/*
 * Xournal++
 *
 * The toolbar widget that names the active tool and shows its properties
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <map>     // for map
#include <string>  // for string

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

#include "control/ToolConfigAdapter.h"  // for ToolConfigAdapter, ToolConfigObserver, ToolConfigState
#include "gui/toolbarMenubar/AbstractToolItem.h"
#include "util/raii/GObjectSPtr.h"  // for WidgetSPtr

#include "ToolPropertyPanel.h"     // for PresetListListener
#include "ToolPropertyProvider.h"  // for ToolPropertyRegistry

class IconNameHelper;
class Settings;

/**
 * Plan 003, step 4: the active tool summary.
 *
 * Shows the active tool's icon, name and a sample of its stroke, and opens that tool's property
 * popover when clicked. Tools that have no panel yet show their name only, and the widget is not
 * clickable: there is nothing to open, and pretending otherwise would be a dead control.
 */
class ActiveToolSummaryItem final: public AbstractToolItem, public ToolConfigObserver {
public:
    ActiveToolSummaryItem(std::string id, ToolConfigAdapter& adapter, const ToolPropertyRegistry& registry,
                          Settings& settings, GtkWindow* parent, PresetListListener* presetsListener,
                          IconNameHelper& icons);
    ~ActiveToolSummaryItem() override;

    ActiveToolSummaryItem(const ActiveToolSummaryItem&) = delete;
    ActiveToolSummaryItem& operator=(const ActiveToolSummaryItem&) = delete;

    void toolConfigChanged(const ToolConfigState& state) override;

    std::string getToolDisplayName() const override;
    GtkWidget* getNewToolIcon() const override;

protected:
    xoj::util::WidgetSPtr createItem(bool horizontal) override;

private:
    static void onButtonClicked(GtkButton* button, gpointer self);
    void updateContents(const ToolConfigState& state);
    /// The popover of `toolType`, built the first time it is needed.
    GtkWidget* getPopoverFor(ToolType toolType);

    ToolConfigAdapter& adapter;
    const ToolPropertyRegistry& registry;
    Settings& settings;
    GtkWindow* parent;
    PresetListListener* presetsListener;
    IconNameHelper& icons;

    GtkWidget* button = nullptr;
    GtkWidget* iconWidget = nullptr;
    GtkWidget* preview = nullptr;
    GtkWidget* nameLabel = nullptr;

    /**
     * One popover per tool, built on first use.
     *
     * Nothing is ever destroyed while a change is being reported, so an open popover cannot be
     * torn down from inside the notification that reaches it.
     */
    std::map<ToolType, xoj::util::WidgetSPtr> popovers;
};

/*
 * Xournal++
 *
 * Abstract class for creating popovers
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <gtk/gtk.h>  // for GtkWidget

#include "control/ToolEnums.h"  // for ToolType

class PopoverFactory {
public:
    PopoverFactory() = default;
    virtual ~PopoverFactory() = default;

    /// Creates a GtkPopover. The returned ref is floating
    virtual GtkWidget* createPopover() const = 0;
};

/**
 * Plan 003: a popover that belongs to a tool.
 *
 * A tool's own control opens its property popover when that tool is already the active one, so
 * the properties of the tool in the user's hand are one click away without a second target to
 * aim at. That is what this interface adds to PopoverFactory.
 */
class ToolPopoverFactory: public PopoverFactory {
public:
    ~ToolPopoverFactory() override = default;

    /// The tool whose properties this popover shows.
    virtual ToolType getToolType() const = 0;
    /// Whether that tool is the active one right now.
    virtual bool isActiveTool() const = 0;
};

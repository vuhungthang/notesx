/*
 * Xournal++
 *
 * The extension point through which a tool contributes its own property rows
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <memory>  // for unique_ptr
#include <string>  // for string
#include <vector>  // for vector

#include <gtk/gtk.h>  // for GtkWidget, GtkRadioButton, GtkWindow, GVariant

#include "control/ToolConfigAdapter.h"  // for ToolConfigAdapter, ToolConfigState
#include "control/ToolEnums.h"          // for ToolType
#include "util/raii/GVariantSPtr.h"     // for GVariantSPtr

/**
 * @brief The rows a provider contributed to one property panel.
 *
 * A panel owns its row set for as long as it lives. Nothing may be shared between two panels: two
 * popovers of the same tool exist at the same time whenever the toolbar is rebuilt, and a shared
 * row set would leave the destroyed one wired to live widgets.
 */
class ToolPropertyRows {
public:
    virtual ~ToolPropertyRows();

    /// The widget to place in the panel. Ownership stays with the row set.
    virtual GtkWidget* getWidget() const = 0;

    /// Refresh after a change. Rows bound to an action also resynchronise from it.
    virtual void toolConfigChanged(const ToolConfigState& state) = 0;
};

/**
 * Plan 003, step 6: what a tool has to provide to get a property panel.
 *
 * Pen, highlighter and eraser implement this here. Shape and selection can be added the same way
 * later, without the panel growing a switch statement over tool types.
 */
class ToolPropertyProvider {
public:
    virtual ~ToolPropertyProvider();

    /// The tool whose properties this provider describes.
    virtual ToolType getToolType() const = 0;
    /// Human readable title of the panel.
    virtual std::string getTitle() const = 0;
    /// Icon of the tool, used by the panel header and by the active tool summary.
    virtual std::string getIconName() const = 0;

    /**
     * @brief Build the rows that are specific to this tool.
     *
     * The panel supplies the shared chrome - header, stroke preview, width row, colour row, fill
     * row and the preset section - and adds whatever this returns below it. A provider therefore
     * adds only the fields its own tool has: the eraser mode for the eraser, the drawing type and
     * line style for the pen.
     *
     * @param adapter to apply changes through
     * @param parent for dialogs opened from a row
     * @return the rows, or nullptr when the tool has no extra fields
     */
    virtual std::unique_ptr<ToolPropertyRows> createRows(ToolConfigAdapter& adapter, GtkWindow* parent) const = 0;
};

/**
 * The property providers the UI can ask for a tool.
 *
 * Registering is what keeps the property popover from having to know which tools exist.
 */
class ToolPropertyRegistry {
public:
    void add(std::unique_ptr<ToolPropertyProvider> provider);
    /// The provider for `toolType`, or nullptr when the tool has no property panel yet.
    ToolPropertyProvider* find(ToolType toolType) const;

    const std::vector<std::unique_ptr<ToolPropertyProvider>>& getProviders() const { return this->providers; }

private:
    std::vector<std::unique_ptr<ToolPropertyProvider>> providers;
};

namespace xoj::toolbar {

/**
 * @brief A section heading inside a property popover.
 * @return a floating widget
 */
GtkWidget* makeSectionHeading(const std::string& text);

/**
 * @brief A row of a label and a control, with the label above the control.
 * @return a floating widget
 */
GtkWidget* makeLabelledRow(const std::string& label, GtkWidget* control);

/**
 * @brief A radio button bound to a GAction state.
 *
 * GTK3 radio buttons and GActions interact badly (an unselected button still writes the action
 * state, which loops), so the buttons are grouped and wired through
 * xoj::util::gtk::setRadioButtonActionName() exactly like the existing style popover.
 *
 * @param group group to join, or nullptr to start a new one; set to the created button
 * @param action action name without the "win." namespace
 * @param target the action target the button is "on" for
 * @param label the visible, semantic label - never an icon on its own
 * @param leading optional widget shown before the label, e.g. a stroke preview
 * @return a floating widget
 */
GtkWidget* makeActionRadioRow(GtkRadioButton*& group, const char* action, GVariant* target, const std::string& label,
                              GtkWidget* leading);

/**
 * @brief One row of radio buttons bound to the same GAction, kept in sync with its state.
 *
 * GTK3 radio buttons write the action state but do not follow it. Without the explicit sync a
 * change made with a keyboard shortcut, a stylus button or a legacy toolbar control would leave
 * the popover showing the previous value.
 */
class ActionRadioGroup {
public:
    /**
     * @brief Append a radio button.
     *
     * @param action action name without the "win." namespace
     * @param targetValue the state the button is "on" for
     */
    template <typename T>
    GtkWidget* addRow(const char* action, T targetValue, const std::string& label, GtkWidget* leading = nullptr) {
        auto target = xoj::util::makeGVariantSPtr<T>(targetValue);
        GtkWidget* btn = makeActionRadioRow(this->group, action, target.get(), label, leading);
        this->rows.emplace_back(Row{btn, action, std::move(target)});
        return btn;
    }

    /// Make every button show the state its action currently has.
    void syncFromActions() const;

private:
    struct Row {
        GtkWidget* button;
        std::string action;
        xoj::util::GVariantSPtr target;
    };

    std::vector<Row> rows;
    GtkRadioButton* group = nullptr;
};

/**
 * @brief A horizontal stroke sample: the colour, the thickness and the dash pattern of a tool.
 *
 * @param color stroke colour
 * @param thickness stroke thickness in the unit ToolHandler reports
 * @param dashed whether to draw a dashed sample
 * @return a floating widget
 */
GtkWidget* makeStrokePreview(Color color, double thickness, bool dashed, int width, int height);

/**
 * @brief Change what a stroke preview draws.
 *
 * The sample keeps the state it was given, so a change does not have to replace the widget and
 * lose its place in the row.
 */
void updateStrokePreview(GtkWidget* preview, Color color, double thickness, bool dashed);

}  // namespace xoj::toolbar

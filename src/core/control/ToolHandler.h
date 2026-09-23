/*
 * Xournal++
 *
 * Handles Tools
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>       // for array
#include <functional>  // for function
#include <memory>      // for unique_ptr
#include <vector>      // for vector

#include "control/ToolEnums.h"               // for ToolSize, ToolType, Draw...
#include "control/settings/SettingsEnums.h"  // for Button
#include "util/Color.h"                      // for Color

#include "Tool.h"  // for Tool

class LineStyle;
class Settings;
class ActionDatabase;
class TextAlignment;


// enum for ptrs that are dynamically pointing to different tools
/**
 * @brief Enum for ptrs that are dynamically pointing to different tools
 *  - active: describes the currently active tool used for drawing
 *  - toolbar: describes the tool currently selected in the toolbar
 *
 * These tools are to be distinguished from ButtonTools which are mostly static
 * apart from changes to the Config
 *
 */
enum SelectedTool { active, toolbar };

class ToolListener {
public:
    /**
     * @brief Update the Cursor and the Toolbar based on the active color
     *
     */
    virtual void toolColorChanged() = 0;
    /**
     * @brief Change the color of the current selection based on the active Tool
     *
     */
    virtual void changeColorOfSelection() = 0;
    virtual void toolSizeChanged() = 0;
    virtual void toolFillChanged() = 0;
    virtual void toolLineStyleChanged() = 0;
    virtual void toolChanged() = 0;

    virtual ~ToolListener();
};

/**
 * Plan 003: a listener for the *effective tool configuration*.
 *
 * ToolHandler has exactly one ToolListener, which Control owns. Observers that only mirror
 * the configuration - the contextual property popovers and the active tool summary - cannot
 * use that slot without displacing Control, so they register here instead.
 *
 * A listener is notified once per change, after the change has been applied, and never while
 * the notification is being delivered to another listener.
 */
class ToolConfigListener {
public:
    virtual void toolConfigChanged() = 0;
    virtual ~ToolConfigListener();
};

class ToolHandler {
public:
    using ToolChangedCallback = std::function<void(ToolType)>;

    /**
     * Plan 003: coalesce the state change notifications emitted while this object is alive.
     *
     * Applying a preset changes several parts of the tool configuration at once. Without
     * coalescing, every part would notify on its own, so the UI would briefly show a mixture
     * of the old and the new configuration and would be rebuilt several times. While a guard
     * is alive the notification methods only record what changed; when the outermost guard
     * goes out of scope the recorded notifications are delivered once each.
     *
     * Guards nest, and only the outermost one delivers.
     */
    class CoalescedUpdate {
    public:
        explicit CoalescedUpdate(ToolHandler& handler);
        ~CoalescedUpdate();

        CoalescedUpdate(const CoalescedUpdate&) = delete;
        CoalescedUpdate& operator=(const CoalescedUpdate&) = delete;
        CoalescedUpdate(CoalescedUpdate&&) = delete;
        CoalescedUpdate& operator=(CoalescedUpdate&&) = delete;

    private:
        ToolHandler& handler;
    };

    ToolHandler(ToolListener* stateChangedListener, ActionDatabase* actionDB, Settings* settings);
    virtual ~ToolHandler();

    /**
     * @brief Reset the Button tool with a new tooltype
     *
     * @param type Tooltype to be set for the button
     * @param button button which should be set
     */
    void resetButtonTool(ToolType type, Button button);

    /**
     * Select the color for the active tool and under certain circumstances toolbar selected tool
     *
     * If the current tool does not have the color capability but the toolbar selected tool has
     * the color can be set for the toolbar selected tool.
     * This is indicated by a little pen shown on top of the color in the UI.
     *
     * @param color Color
     * @param userSelection
     * 			true if the user selected the color
     * 			false if the color is selected by a tool change
     * 			and therefore should not be applied to a selection
     */
    void setColor(Color color, bool userSelection);

    /**
     * @brief Set the color for a Button
     * This is a separate function from `setColor` to prevent mixup of different usecases.
     *
     * @param color color to be set
     * @param button button to set color for
     */
    void setButtonColor(Color color, Button button);

    /**
     * @brief Get the Color of the active tool
     *
     * @return Color of active tool
     */
    Color getColor() const;

    /**
     * @brief Get the Color of the active tool except the alpha value is replaced by 0xFF
     */
    Color getColorMaskAlpha() const;

    /**
     * @brief Enable/disable fill for the tool selected in the toolbar
     *
     * @param fill whether fill should be enabled
     * @param fireEvent whether a toolFillChanged event should be fired
     */
    void setFillEnabled(bool fill);

    /**
     * @brief Get the Fill of the active tool
     *
     * @return -1 if fill is disabled
     * @return int > 0 otherwise
     */
    int getFill() const;

    /**
     * @brief Get the Drawing Type one of selected tools
     *
     * @param selectedTool by the default the active one
     * @return DrawingType
     */
    DrawingType getDrawingType(SelectedTool selectedTool = SelectedTool::active) const;

    /**
     * @brief Set the Drawing Type of the toolbar selected tool
     * @note It is safer to always set the toolbar tool as the active tool could be pointing to a button tool which
     * could lead to hard to debug behaviour
     *
     * @param drawingType
     */
    void setDrawingType(DrawingType drawingType);

    /**
     * @brief Set the Button Drawing Type  of the button tool
     *
     * @param drawingType
     * @param button button tool to be selected
     */
    void setButtonDrawingType(DrawingType drawingType, Button button);

    /**
     * @brief Set the Stroke Type of a button tool
     *
     * @param strokeType The stroke type to apply
     * @param button The button tool to change
     */
    void setButtonStrokeType(StrokeType strokeType, Button button);
    void setButtonStrokeType(const LineStyle& lineStyle, Button button);

    /**
     * @brief Get the Line Style of active tool
     *
     * @return const LineStyle&
     */
    const LineStyle& getLineStyle() const;

    /**
     * @brief Get the Size of one of the selected tools
     *
     * @param selectedTool
     * @return ToolSize
     */
    ToolSize getSize(SelectedTool selectedTool = SelectedTool::active) const;

    /**
     * @brief Set the Size of toolbar selected tool
     * @note It is safer to always set the toolbar tool as the active tool could be pointing to a button tool which
     * could lead to hard to debug behaviour
     *
     * @param size is clamped to be within the defined range [0,5)
     */
    void setSize(ToolSize size);

    /**
     * @brief Set the Button Size
     *
     * @param size is clamped to be within the defined range [0,5)
     * @param button size will be applied to
     */
    void setButtonSize(ToolSize size, Button button);

    /**
     * @brief Get the Thickness of the active tool
     *
     * @return double
     */
    double getThickness() const;

    void setLineStyle(const LineStyle& style);

    ToolSize getPenSize() const;
    ToolSize getEraserSize() const;
    ToolSize getHighlighterSize() const;
    void setPenSize(ToolSize size);
    void setEraserSize(ToolSize size);
    void setHighlighterSize(ToolSize size);

    void setPenFillEnabled(bool fill);
    bool getPenFillEnabled() const;
    void setPenFill(int alpha);
    int getPenFill() const;

    void setHighlighterFillEnabled(bool fill);
    bool getHighlighterFillEnabled() const;
    void setHighlighterFill(int alpha);
    int getHighlighterFill() const;

    void setSelectPDFTextMarkerOpacity(int alpha);
    int getSelectPDFTextMarkerOpacity() const;

    TextAlignment getTextAlignment() const;
    void setTextAlignment(TextAlignment a);
    bool getTextJustify() const;
    void setTextJustify(bool j);

    /**
     * @brief Set the toolbar selected tool to the type
     * This will also point the active tool to the same tool as the toolbar selected tool.
     * This ensure that the toolbar and the cursor are correctly updated right after selecting the tool in the toolbar.
     *
     * @param type
     */
    void selectTool(ToolType type);

    /**
     * @brief Get the Tool Type of active tool
     *
     * @return ToolType
     */
    ToolType getToolType() const;

    /**
     * @brief Update the Toolbar and the cursor based on the active Tool
     *
     */
    void fireToolChanged() const;

    /**
     * @brief Listen for tool changes.
     *
     * Different from the listener given to the constructor -- [listener]
     * here only listens for when the current tool is changed to another.
     *
     * @param listener A callback, called when the user/client
     *  changes tools.
     */
    void addToolChangedListener(ToolChangedCallback listener);

    /**
     * @brief Listen for changes of the effective tool configuration (Plan 003).
     *
     * @param listener Listener to notify. It must outlive the registration; a listener must
     *                 not add or remove listeners from inside its callback.
     */
    void addToolConfigListener(ToolConfigListener* listener);
    void removeToolConfigListener(ToolConfigListener* listener);

    /**
     * @brief Get the Tool of a certain type
     *
     * @param type
     * @return Tool&
     */
    Tool& getTool(ToolType type) const;

    /**
     * @brief Get the active Tool, returns nullptr if no tool is active
     *
     * @return Tool*
     */
    Tool* getActiveTool() const;

    /**
     * @brief Set the Eraser Type of the Eraser in the toolbar
     * @note Here the Eraser Tool in the toolbar is changed regardless of the the tool currently selected.
     * This is necessary to allow users to change the Eraser type for their Button Tools while having another tool
     * active. This is relevant in case of Eraser Type being set to "Don't Change" for the button.
     *
     * @param eraserType
     */
    void setEraserType(EraserType eraserType);

    /**
     * @brief Set the Button Eraser Type
     *
     * @param eraserType
     * @param button
     */
    void setButtonEraserType(EraserType eraserType, Button button);

    /**
     * @brief Get the Eraser Type
     * If the currently active Tool is an eraser it's type is returned (relevant for Buttontools).
     * If the currently active Tool is not a eraser the erasertype of the eraser in the toolbar is obtained.
     *
     * @param selectedTool
     * @return EraserType
     */
    EraserType getEraserType() const;

    /**
     * @brief Update the toolbar based on the Eraser type of the active tool
     *
     */
    void eraserTypeChanged();

    /**
     * @brief Check whether the selectedTool has a certain capability
     *
     * @param cap
     * @param selectedTool
     * @return true if tool has the capability
     * @return false if tool does not have the capability
     */
    bool hasCapability(ToolCapabilities cap, SelectedTool selectedTool = SelectedTool::active) const;

    /**
     * @brief Check whether the active tool is a Drawing tool.
     * Drawing tools are considered all tools that directly change the canvas.
     * Right now these are:
     *  - Highlighter
     *  - Pen
     *  - Eraser
     *
     * @return true if active tool is a drawing tool
     * @return false if active tool is no drawing tool
     */
    bool isDrawingTool() const;

    void saveSettings() const;
    void loadSettings();

    /**
     * @brief Point the active tool to the corresponding button tool if it is not already pointing to it
     *
     * @param button Button tool which should be pointed to
     * @return true if the active toolpointer was changed
     * @return false if the active toolpointer was not changed (it was already pointing to the right button)
     */
    bool pointActiveToolToButtonTool(Button button);
    /**
     * @brief Point the active tool to the tool of the given type
     *
     * @param type ToolType the tool type to switch to
     * @return true if the active toolpointer was changed
     * @return false if the active toolpointer was not changed (it was already pointing to the given tool type)
     */
    bool pointActiveToolToToolType(ToolType type);
    /**
     * @brief Point the active tool to tool selected in the toolbar
     *
     * @return true if the active toolpointer was changed
     * @return false if the active toolpointer was not changed (it was already pointing to the toolbar-tool)
     */
    bool pointActiveToolToToolbarTool();

    [[maybe_unused]] std::array<std::unique_ptr<Tool>, TOOL_COUNT> const& getTools() const;

    /**
     * Plan 008, step 1: the tool the user was on before the current one, or TOOL_NONE.
     *
     * The quick palette offers to go back to it. It is recorded when the effective active tool
     * changes to a different tool, and never remembers TOOL_NONE or the current tool, so the
     * palette's previous-tool slot is only ever a tool the user can actually return to.
     */
    auto getPreviousToolType() const -> ToolType;

    /**
     * Change the selection tools capabilities, depending on the selected elements
     */
    void setSelectionEditTools(bool setColor, bool setSize, bool setFill, bool setLineStyle);

    const double* getToolThickness(ToolType type) const;

    /**
     * Returns whether the current tool will create an element that may only reside on a single page even when the
     * pointer moves to another
     * @return
     */
    bool isSinglePageTool() const;

    /**
     * Returns whether the current tool accepts events from outside the page input started in (i.e. events
     * should not be clamped to the page)
     */
    bool acceptsOutOfPageEvents() const;

    /**
     * @brief Whether the tool supports short taps filtering (for floating toolbox or selection)
     * see Preferences->Drawing Area->Action on Tool tap
     */
    bool supportsTapFilter() const;

protected:
    void initTools();

private:
    std::array<std::unique_ptr<Tool>, TOOL_COUNT> tools;

    /**
     * @brief The parts of the tool configuration a change can be reported for (Plan 003).
     *
     * Kept as a bit set so a coalesced update can report each changed part exactly once.
     */
    enum Notification : unsigned int {
        NOTIFY_TOOL = 1U << 0,
        NOTIFY_COLOR = 1U << 1,
        NOTIFY_SIZE = 1U << 2,
        NOTIFY_FILL = 1U << 3,
        NOTIFY_LINE_STYLE = 1U << 4,
        /// The eraser mode changed; it is carried by an action state rather than by the tool
        /// configuration, so nothing has to be re-synced on the ToolListener side.
        NOTIFY_ERASER_TYPE = 1U << 5,
    };

    /**
     * @brief Report a change, or record it while a CoalescedUpdate is alive.
     */
    void emit(Notification notification) const;
    /// Report the recorded changes to the ToolListener and to the config listeners.
    void deliver(unsigned int notifications) const;

    void emitToolChanged() const;
    void emitToolColorChanged() const;
    void emitToolSizeChanged() const;
    void emitToolFillChanged() const;
    void emitToolLineStyleChanged() const;

    /**
     * Plan 008, step 1: remember the tool the user is leaving, for the quick palette.
     *
     * A plain record, called before the active tool changes: it only writes a field, calls nothing,
     * and cannot loop. It never records TOOL_NONE (nothing to go back to) nor the tool being
     * selected (no change to remember).
     */
    void rememberToolChange(ToolType nextType);

    /**
     * @brief Get the Button Tool pointer based on enum
     *
     * @param button
     * @return Tool*
     */
    Tool* getButtonTool(Button button) const;

    /**
     * @brief Get the Selected Tool pointer based on enum
     *
     * @param selectedTool
     * @return Tool*
     */
    Tool* getSelectedTool(SelectedTool selectedTool) const;

    // active Tool which is used for drawing
    Tool* activeTool = nullptr;

    // tool which is selected in the toolbar
    Tool* toolbarSelectedTool = nullptr;

    /// Plan 008, step 1: the tool the active tool was before its current one; TOOL_NONE at start.
    ToolType previousToolType = TOOL_NONE;

    // tools set for the different Buttons
    std::unique_ptr<Tool> stylusButton1Tool;
    std::unique_ptr<Tool> stylusButton2Tool;
    std::unique_ptr<Tool> eraserButtonTool;
    std::unique_ptr<Tool> mouseLeftButtonTool;
    std::unique_ptr<Tool> mouseMiddleButtonTool;
    std::unique_ptr<Tool> mouseRightButtonTool;
    std::unique_ptr<Tool> mouseButton4Tool;
    std::unique_ptr<Tool> mouseButton5Tool;
    std::unique_ptr<Tool> touchDrawingButtonTool;

    std::vector<ToolChangedCallback> toolChangeListeners;

    /// Observers of the effective tool configuration (Plan 003). Not owned.
    std::vector<ToolConfigListener*> toolConfigListeners;

    /// Nesting depth of the active CoalescedUpdate guards. Notification batching state: it
    /// describes how a change is reported, not what the configuration is.
    mutable unsigned int coalesceDepth = 0;
    /// Notifications recorded while coalesceDepth > 0, as a Notification bit set.
    mutable unsigned int pendingNotifications = 0;

    ToolListener* stateChangeListener = nullptr;
    ActionDatabase* actionDB = nullptr;
    Settings* settings = nullptr;
};

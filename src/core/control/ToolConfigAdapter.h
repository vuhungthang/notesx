/*
 * Xournal++
 *
 * The single adapter between ToolHandler and every representation of the tool configuration
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>  // for string
#include <vector>  // for vector

#include "control/ToolEnums.h"  // for ToolType, ToolSize, DrawingType, EraserType
#include "util/Color.h"         // for Color

#include "ToolHandler.h"  // for ToolHandler, ToolConfigListener
#include "ToolPreset.h"   // for ToolPreset

/**
 * Plan 003: the effective configuration of the active tool.
 *
 * The `has*` flags say whether the field applies to the tool at all; the field itself is only
 * meaningful when its flag is set. A representation therefore never has to ask ToolHandler
 * which capabilities a tool has in order to decide what to show.
 */
struct ToolConfigState {
    ToolType toolType = TOOL_NONE;

    bool hasColor = false;
    /// Stroke colour, alpha included.
    Color color{};

    bool hasSize = false;
    ToolSize size = TOOL_SIZE_NONE;
    /// Stroke thickness in the unit ToolHandler reports, for drawing a preview.
    double thickness = 0.0;

    bool hasFill = false;
    /// Fill opacity in [0, 255], or -1 when fill is switched off.
    int fill = -1;

    bool hasDrawingType = false;
    DrawingType drawingType = DRAWING_TYPE_DONT_CHANGE;

    bool hasEraserType = false;
    EraserType eraserType = ERASER_TYPE_NONE;

    bool hasLineStyle = false;
    /// The StrokeStyle::formatStyle() name - the same value the win.tool-pen-line-style action
    /// carries, so a preview and the existing line style control agree.
    std::string lineStyle;

    bool operator==(const ToolConfigState& other) const = default;
};

/**
 * A representation that mirrors the tool configuration: a property popover, the active tool
 * summary, a preset list.
 */
class ToolConfigObserver {
public:
    /**
     * @brief Called once after a change has been applied.
     *
     * @param state the effective configuration *after* the change, so an observer never has to
     *              read ToolHandler itself and never sees an intermediate state.
     */
    virtual void toolConfigChanged(const ToolConfigState& state) = 0;
    virtual ~ToolConfigObserver();
};

/**
 * Plan 003, step 2: one place that reads the effective tool configuration, one place that
 * applies a whole configuration, and one place that tells every representation about it.
 *
 * Nothing else may synchronise colour, width, fill and drawing type by hand: the legacy
 * toolbar controls, the property popovers, the active tool summary and a preset all end up
 * here, so they cannot drift apart.
 */
class ToolConfigAdapter final: public ToolConfigListener {
public:
    explicit ToolConfigAdapter(ToolHandler& toolHandler);
    ~ToolConfigAdapter() override;

    ToolConfigAdapter(const ToolConfigAdapter&) = delete;
    ToolConfigAdapter& operator=(const ToolConfigAdapter&) = delete;

    /// The effective configuration of the active tool.
    ToolConfigState getState() const;

    /**
     * @brief Stroke thickness of `toolType` at `size`.
     *
     * Used to draw the width previews of a property popover, so that the sample a user picks is
     * the sample they get.
     *
     * @return the thickness, or 0 when the tool has no thickness table at all
     */
    double getThickness(ToolType toolType, ToolSize size) const;

    /**
     * @brief Switch to a tool without changing anything else.
     *
     * A property popover shows the configuration of one tool, so opening it for a tool that is
     * not the active one has to make that tool active; otherwise its controls would edit the
     * previous tool and its preview would be wrong.
     */
    void selectTool(ToolType toolType);

    /**
     * @brief Apply a stored preset.
     *
     * Every part goes through the existing ToolHandler path, so the toolbar toggles, the
     * cursor, the derived action states and every observer see the same result. The whole
     * change is coalesced into one update; a preset that does not apply is rejected and
     * changes nothing.
     *
     * @return whether the preset was applied
     */
    bool applyPreset(const ToolPreset& preset);

    /**
     * @brief Capture the current configuration as a preset the user can name and keep.
     *
     * Only the fields the active tool actually supports are filled in.
     */
    ToolPreset capturePreset(const std::string& name) const;

    /**
     * @brief Register an observer.
     *
     * Registering does not notify; the observer reads getState() once and is notified from then
     * on. That lets an observer register itself from its own constructor.
     */
    void addObserver(ToolConfigObserver* observer);
    void removeObserver(ToolConfigObserver* observer);

    /// ToolHandler notification. Called by ToolHandler, not by the UI.
    void toolConfigChanged() override;

private:
    /// Whether the active tool has a drawing type (line, rectangle, ...).
    bool activeToolHasDrawingType() const;
    /// Apply the fill opacity to the active tool's own fill setting.
    void applyFill(int fill);
    void notifyObservers(const ToolConfigState& state);

    /**
     * How many notification rounds one change may take. A well behaved observer only reads the
     * state; the bound exists so that an observer which writes back cannot hang the UI.
     */
    static constexpr unsigned int MAX_NOTIFICATION_ROUNDS = 4U;

    ToolHandler& toolHandler;
    std::vector<ToolConfigObserver*> observers;
    /// True while observers are being notified.
    bool notifying = false;
    /// Set when a change happened while observers were being notified: the change is applied,
    /// but it is reported in the next round instead of by recursing into the notification.
    bool notificationPending = false;
};

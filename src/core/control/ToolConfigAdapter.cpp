#include "ToolConfigAdapter.h"

#include <algorithm>  // for find, erase
#include <utility>    // for move

#include <glib.h>  // for g_warning

#include "model/StrokeStyle.h"  // for StrokeStyle::formatStyle

namespace {

/**
 * Capabilities that make a drawing type meaningful. The same set ToolHandler::saveSettings()
 * uses to decide whether to persist a drawing type.
 */
constexpr unsigned int SHAPE_CAPABILITIES = TOOL_CAP_RULER | TOOL_CAP_RECTANGLE | TOOL_CAP_ELLIPSE | TOOL_CAP_ARROW |
                                            TOOL_CAP_DOUBLE_ARROW | TOOL_CAP_RECOGNIZER | TOOL_CAP_SPLINE;

}  // namespace

ToolConfigObserver::~ToolConfigObserver() = default;

ToolConfigAdapter::ToolConfigAdapter(ToolHandler& toolHandler): toolHandler(toolHandler) {
    this->toolHandler.addToolConfigListener(this);
}

ToolConfigAdapter::~ToolConfigAdapter() { this->toolHandler.removeToolConfigListener(this); }

auto ToolConfigAdapter::activeToolHasDrawingType() const -> bool {
    return this->toolHandler.hasCapability(static_cast<ToolCapabilities>(SHAPE_CAPABILITIES));
}

auto ToolConfigAdapter::getState() const -> ToolConfigState {
    ToolConfigState state;
    state.toolType = this->toolHandler.getToolType();

    state.hasColor = this->toolHandler.hasCapability(TOOL_CAP_COLOR);
    if (state.hasColor) {
        state.color = this->toolHandler.getColor();
    }

    state.hasSize = this->toolHandler.hasCapability(TOOL_CAP_SIZE);
    if (state.hasSize) {
        state.size = this->toolHandler.getSize();
        state.thickness = this->toolHandler.getThickness();
    }

    state.hasFill = this->toolHandler.hasCapability(TOOL_CAP_FILL);
    if (state.hasFill) {
        state.fill = this->toolHandler.getFill();
    }

    state.hasDrawingType = this->activeToolHasDrawingType();
    if (state.hasDrawingType) {
        state.drawingType = this->toolHandler.getDrawingType();
    }

    state.hasEraserType = state.toolType == TOOL_ERASER;
    if (state.hasEraserType) {
        state.eraserType = this->toolHandler.getEraserType();
    }

    state.hasLineStyle = this->toolHandler.hasCapability(TOOL_CAP_LINE_STYLE);
    if (state.hasLineStyle) {
        state.lineStyle = StrokeStyle::formatStyle(this->toolHandler.getLineStyle());
    }

    return state;
}

auto ToolConfigAdapter::getThickness(ToolType toolType, ToolSize size) const -> double {
    if (size < TOOL_SIZE_VERY_FINE || size >= TOOL_SIZE_NONE) {
        return 0.0;
    }

    // ToolHandler only has a thickness table for the tools that draw a stroke of a size. Asking
    // it for any other tool asserts, so the list is spelled out here.
    switch (toolType) {
        case TOOL_PEN:
        case TOOL_ERASER:
        case TOOL_HIGHLIGHTER:
        case TOOL_LASER_POINTER_PEN:
        case TOOL_LASER_POINTER_HIGHLIGHTER:
            return this->toolHandler.getToolThickness(toolType)[static_cast<size_t>(size)];
        default:
            return 0.0;
    }
}

void ToolConfigAdapter::selectTool(ToolType toolType) {
    if (this->toolHandler.getToolType() == toolType) {
        return;  // Already there: no change, and so no notification either.
    }

    ToolHandler::CoalescedUpdate update(this->toolHandler);
    this->toolHandler.selectTool(toolType);
    this->toolHandler.fireToolChanged();
}

auto ToolConfigAdapter::applyPreset(const ToolPreset& preset) -> bool {
    if (!preset.isValid()) {
        g_warning("ToolConfigAdapter: refusing to apply an incomplete preset");
        return false;
    }

    // One guard for the whole change: the parts below report themselves, but only once each
    // and only after all of them have been applied (see ToolHandler::CoalescedUpdate).
    ToolHandler::CoalescedUpdate update(this->toolHandler);

    // Selecting the tool first, so that the parts that follow land on the tool the preset is
    // about. fireToolChanged() re-syncs the toolbar toggles, the cursor and every action state
    // derived from the tool; it is queued rather than delivered immediately, so it reports the
    // finished configuration.
    this->toolHandler.selectTool(preset.toolType);
    this->toolHandler.fireToolChanged();

    if (preset.color) {
        // userSelection=false: applying a preset is a tool change, not a colour choice, so it
        // must not be written into the current selection (that would create an undo step the
        // user did not ask for).
        this->toolHandler.setColor(*preset.color, false);
    }

    if (preset.size) {
        this->toolHandler.setSize(*preset.size);
    }

    if (preset.drawingType) {
        this->toolHandler.setDrawingType(*preset.drawingType);
    }

    if (preset.fill) {
        this->applyFill(*preset.fill);
    }

    if (preset.eraserType) {
        this->toolHandler.setEraserType(*preset.eraserType);
    }

    if (preset.lineStyle) {
        this->toolHandler.setLineStyle(StrokeStyle::parseStyle(*preset.lineStyle));
    }

    return true;
}

void ToolConfigAdapter::applyFill(int fill) {
    const bool enabled = fill >= 0;

    switch (this->toolHandler.getToolType()) {
        case TOOL_PEN:
            if (enabled) {
                this->toolHandler.setPenFill(fill);
            }
            this->toolHandler.setPenFillEnabled(enabled);
            break;
        case TOOL_HIGHLIGHTER:
            if (enabled) {
                this->toolHandler.setHighlighterFill(fill);
            }
            this->toolHandler.setHighlighterFillEnabled(enabled);
            break;
        default:
            // The other tools carry their fill on the toolbar selected tool; ToolHandler has no
            // per-tool accessor for them, and no preset field is stored for them either.
            break;
    }
}

auto ToolConfigAdapter::capturePreset(const std::string& name) const -> ToolPreset {
    ToolPreset preset;
    preset.name = name;
    preset.toolType = this->toolHandler.getToolType();

    if (this->toolHandler.hasCapability(TOOL_CAP_COLOR)) {
        preset.color = this->toolHandler.getColor();
    }
    if (this->toolHandler.hasCapability(TOOL_CAP_SIZE)) {
        preset.size = this->toolHandler.getSize();
    }
    if (this->toolHandler.hasCapability(TOOL_CAP_FILL)) {
        preset.fill = this->toolHandler.getFill();
    }
    if (this->activeToolHasDrawingType()) {
        preset.drawingType = this->toolHandler.getDrawingType();
    }
    if (preset.toolType == TOOL_ERASER) {
        preset.eraserType = this->toolHandler.getEraserType();
    }
    if (this->toolHandler.hasCapability(TOOL_CAP_LINE_STYLE)) {
        preset.lineStyle = StrokeStyle::formatStyle(this->toolHandler.getLineStyle());
    }

    return preset;
}

void ToolConfigAdapter::addObserver(ToolConfigObserver* observer) {
    this->observers.emplace_back(observer);
    // Registering does not notify: a representation is normally created from inside its own
    // constructor, where calling its virtual callback would be undefined behaviour. It reads
    // getState() once instead and is notified from then on.
}

void ToolConfigAdapter::removeObserver(ToolConfigObserver* observer) { std::erase(this->observers, observer); }

void ToolConfigAdapter::toolConfigChanged() {
    if (this->notifying) {
        // An observer changed the configuration while the previous change was being reported.
        // The change itself is already applied; it is reported in the next round instead of by
        // recursing into the notification that is still running.
        this->notificationPending = true;
        return;
    }

    this->notifying = true;
    for (unsigned int round = 0; round < MAX_NOTIFICATION_ROUNDS; round++) {
        this->notificationPending = false;
        this->notifyObservers(this->getState());
        if (!this->notificationPending) {
            break;
        }
    }
    if (this->notificationPending) {
        g_warning("ToolConfigAdapter: observers keep changing the tool configuration, "
                  "dropping the remaining notification");
        this->notificationPending = false;
    }
    this->notifying = false;
}

void ToolConfigAdapter::notifyObservers(const ToolConfigState& state) {
    // A copy: an observer may remove itself while being notified, for instance when the popover
    // it lives in is destroyed by the change it is reacting to.
    const std::vector<ToolConfigObserver*> observers = this->observers;
    for (ToolConfigObserver* observer: observers) {
        observer->toolConfigChanged(state);
    }
}

#include "ToolPropertyProviders.h"

#include <memory>  // for make_unique, unique_ptr
#include <string>  // for string

#include <gtk/gtk.h>  // for GtkWidget, GtkBox, GtkWindow

#include "control/ToolEnums.h"  // for TOOL_ERASER, TOOL_HIGHLIGHTER, TOOL_PEN
#include "enums/Action.enum.h"  // for Action, Action_toString
#include "gui/IconNameHelper.h"
#include "util/gtk4_helper.h"  // for gtk_box_append
#include "util/i18n.h"         // for _

namespace {

/**
 * @brief The drawing types the popover offers.
 *
 * Every entry has a GAction, which is what keeps the row in step with the DRAW combo and with the
 * keyboard shortcuts. Freehand (no drawing type at all) has no action, so it is not offered here:
 * an entry that only this row could set would silently break the synchronisation rule. Presets
 * still store and restore it.
 */
struct DrawingTypeEntry {
    Action action;
    const char* label;
};

constexpr DrawingTypeEntry DRAWING_TYPES[] = {
        {Action::TOOL_DRAW_LINE, "Draw Line"},       {Action::TOOL_DRAW_RECTANGLE, "Draw Rectangle"},
        {Action::TOOL_DRAW_ELLIPSE, "Draw Ellipse"}, {Action::TOOL_DRAW_ARROW, "Draw Arrow"},
        {Action::TOOL_DRAW_SPLINE, "Draw Spline"},   {Action::TOOL_DRAW_SHAPE_RECOGNIZER, "Stroke recognizer"},
};

/// The line styles of the pen, in the same order and with the same targets as the toolbar popover.
struct LineStyleEntry {
    const char* value;
    const char* label;
};

constexpr LineStyleEntry LINE_STYLES[] = {
        {"plain", "standard"},
        {"dash", "dashed"},
        {"dashdot", "dash-/ dotted"},
        {"dot", "dotted"},
};

/// The eraser modes, matching Action::TOOL_ERASER_TYPE.
struct EraserTypeEntry {
    EraserType type;
    const char* label;
};

constexpr EraserTypeEntry ERASER_TYPES[] = {
        {ERASER_TYPE_DEFAULT, "standard"},
        {ERASER_TYPE_WHITEOUT, "whiteout"},
        {ERASER_TYPE_DELETE_STROKE, "delete stroke"},
};

/// A row set of drawing type buttons.
class DrawingTypeRows: public ToolPropertyRows {
public:
    explicit DrawingTypeRows(ToolConfigAdapter&) {
        this->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_append(GTK_BOX(this->box), xoj::toolbar::makeSectionHeading(_("Drawing type")));

        GtkWidget* rows = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_append(GTK_BOX(this->box), rows);

        for (const DrawingTypeEntry& entry: DRAWING_TYPES) {
            /*
             * Every drawing type has its own boolean-state action that takes no parameter (see
             * ActionPropDrawingTypes), so the state a row is "on" for is `true` - its own action is
             * on exactly when this drawing type is the current one. Handing the "is this the
             * current drawing type" flag over instead would put `false` on every other row, and
             * every one of those rows would then compare equal to its own action's false state and
             * light up as well; clicking one would ask its action to change to a state that does
             * not mean "this drawing type".
             */
            GtkWidget* btn = this->group.addRow(Action_toString(entry.action), true, _(entry.label));
            gtk_box_append(GTK_BOX(rows), btn);
        }
    }

    GtkWidget* getWidget() const override { return this->box; }

    void toolConfigChanged(const ToolConfigState&) override { this->group.syncFromActions(); }

private:
    GtkWidget* box = nullptr;
    xoj::toolbar::ActionRadioGroup group;
};

/// A row set of pen line style buttons.
class LineStyleRows: public ToolPropertyRows {
public:
    LineStyleRows() {
        this->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_append(GTK_BOX(this->box), xoj::toolbar::makeSectionHeading(_("Line style")));

        GtkWidget* rows = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_append(GTK_BOX(this->box), rows);

        // A const char* variable rather than the literal: addRow() takes the target by value and
        // has to deduce G_VARIANT_TYPE_STRING, which a char array would not.
        for (const LineStyleEntry& entry: LINE_STYLES) {
            const char* value = entry.value;
            GtkWidget* btn = this->group.addRow(Action_toString(Action::TOOL_PEN_LINE_STYLE), value, _(entry.label));
            gtk_box_append(GTK_BOX(rows), btn);
        }
    }

    GtkWidget* getWidget() const override { return this->box; }

    void toolConfigChanged(const ToolConfigState&) override { this->group.syncFromActions(); }

private:
    GtkWidget* box = nullptr;
    xoj::toolbar::ActionRadioGroup group;
};

/// A row set of eraser mode buttons.
class EraserTypeRows: public ToolPropertyRows {
public:
    EraserTypeRows() {
        this->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        gtk_box_append(GTK_BOX(this->box), xoj::toolbar::makeSectionHeading(_("Eraser mode")));

        GtkWidget* rows = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_box_append(GTK_BOX(this->box), rows);

        for (const EraserTypeEntry& entry: ERASER_TYPES) {
            GtkWidget* btn = this->group.addRow(Action_toString(Action::TOOL_ERASER_TYPE), entry.type, _(entry.label));
            gtk_box_append(GTK_BOX(rows), btn);
        }
    }

    GtkWidget* getWidget() const override { return this->box; }

    void toolConfigChanged(const ToolConfigState&) override { this->group.syncFromActions(); }

private:
    GtkWidget* box = nullptr;
    xoj::toolbar::ActionRadioGroup group;
};

class PenToolPropertyProvider final: public ToolPropertyProvider {
public:
    explicit PenToolPropertyProvider(const IconNameHelper& icons): icon(icons.iconName("tool-pencil")) {}

    ToolType getToolType() const override { return TOOL_PEN; }
    std::string getTitle() const override { return _("Pen"); }
    std::string getIconName() const override { return this->icon; }

    std::unique_ptr<ToolPropertyRows> createRows(ToolConfigAdapter& adapter, GtkWindow*) const override {
        auto rows = std::make_unique<PenRows>(adapter);
        return rows;
    }

private:
    /// The pen has both a drawing type and a line style.
    class PenRows: public ToolPropertyRows {
    public:
        explicit PenRows(ToolConfigAdapter& adapter): drawingType(adapter) {
            this->box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
            gtk_box_append(GTK_BOX(this->box), this->drawingType.getWidget());
            gtk_box_append(GTK_BOX(this->box), this->lineStyle.getWidget());
        }

        GtkWidget* getWidget() const override { return this->box; }

        void toolConfigChanged(const ToolConfigState& state) override {
            this->drawingType.toolConfigChanged(state);
            this->lineStyle.toolConfigChanged(state);
        }

    private:
        GtkWidget* box = nullptr;
        DrawingTypeRows drawingType;
        LineStyleRows lineStyle;
    };

    std::string icon;
};

class HighlighterToolPropertyProvider final: public ToolPropertyProvider {
public:
    explicit HighlighterToolPropertyProvider(const IconNameHelper& icons): icon(icons.iconName("tool-highlighter")) {}

    ToolType getToolType() const override { return TOOL_HIGHLIGHTER; }
    std::string getTitle() const override { return _("Highlighter"); }
    std::string getIconName() const override { return this->icon; }

    std::unique_ptr<ToolPropertyRows> createRows(ToolConfigAdapter& adapter, GtkWindow*) const override {
        return std::make_unique<DrawingTypeRows>(adapter);
    }

private:
    std::string icon;
};

class EraserToolPropertyProvider final: public ToolPropertyProvider {
public:
    explicit EraserToolPropertyProvider(const IconNameHelper& icons): icon(icons.iconName("tool-eraser")) {}

    ToolType getToolType() const override { return TOOL_ERASER; }
    std::string getTitle() const override { return _("Eraser"); }
    std::string getIconName() const override { return this->icon; }

    std::unique_ptr<ToolPropertyRows> createRows(ToolConfigAdapter&, GtkWindow*) const override {
        return std::make_unique<EraserTypeRows>();
    }

private:
    std::string icon;
};

}  // namespace

namespace xoj::toolbar {

void addBuiltInToolPropertyProviders(ToolPropertyRegistry& registry, const IconNameHelper& icons) {
    registry.add(std::make_unique<PenToolPropertyProvider>(icons));
    registry.add(std::make_unique<HighlighterToolPropertyProvider>(icons));
    registry.add(std::make_unique<EraserToolPropertyProvider>(icons));
}

}  // namespace xoj::toolbar

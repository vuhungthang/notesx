#include "ToolPropertyProvider.h"

#include <algorithm>  // for clamp
#include <utility>    // for move

#include <cairo.h>  // for cairo_set_dash, cairo_set_line_width, ...

#include "util/GtkUtil.h"      // for setRadioButtonActionName
#include "util/gtk4_helper.h"  // for gtk_widget_add_css_class

ToolPropertyProvider::~ToolPropertyProvider() = default;

ToolPropertyRows::~ToolPropertyRows() = default;

void ToolPropertyRegistry::add(std::unique_ptr<ToolPropertyProvider> provider) {
    if (provider == nullptr) {
        return;
    }
    this->providers.emplace_back(std::move(provider));
}

auto ToolPropertyRegistry::find(ToolType toolType) const -> ToolPropertyProvider* {
    for (const auto& provider: this->providers) {
        if (provider->getToolType() == toolType) {
            return provider.get();
        }
    }
    return nullptr;
}

namespace xoj::toolbar {

GtkWidget* makeSectionHeading(const std::string& text) {
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_widget_set_halign(label, GTK_ALIGN_START);
    gtk_widget_add_css_class(label, "xoj-section-heading");
    return label;
}

GtkWidget* makeLabelledRow(const std::string& label, GtkWidget* control) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(box, 6);
    gtk_widget_set_margin_end(box, 6);

    GtkWidget* heading = gtk_label_new(label.c_str());
    gtk_widget_set_halign(heading, GTK_ALIGN_START);
    gtk_widget_add_css_class(heading, "xoj-property-label");

    gtk_box_append(GTK_BOX(box), heading);
    gtk_box_append(GTK_BOX(box), control);
    return box;
}

GtkWidget* makeActionRadioRow(GtkRadioButton*& group, const char* action, GVariant* target, const std::string& label,
                              GtkWidget* leading) {
    GtkWidget* btn = gtk_radio_button_new_from_widget(group);
    group = GTK_RADIO_BUTTON(btn);

    // The group has to be set before the action: without it every button is simply ticked, and
    // with it an unselected button still writes the action state, which loops. The same order and
    // the same helper are used by the existing style popover.
    xoj::util::gtk::setRadioButtonActionName(GTK_RADIO_BUTTON(btn), "win", action);
    gtk_actionable_set_action_target_value(GTK_ACTIONABLE(btn), target);

    gtk_widget_add_css_class(btn, "xoj-control");
    gtk_widget_add_css_class(btn, "xoj-focus-ring");

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    if (leading != nullptr) {
        gtk_box_append(GTK_BOX(box), leading);
    }
    gtk_box_append(GTK_BOX(box), gtk_label_new(label.c_str()));
    gtk_button_set_child(GTK_BUTTON(btn), box);

    // The child is a box, not a label, so the accessible name is set explicitly. The visible
    // label carries the meaning: no row is an icon on its own.
    gtk_widget_set_tooltip_text(btn, label.c_str());
    atk_object_set_name(gtk_widget_get_accessible(btn), label.c_str());

    return btn;
}

namespace {

void syncActionRow(const GtkWidget* button, const std::string& actionName, const GVariant* target) {
    GActionGroup* group = gtk_widget_get_action_group(const_cast<GtkWidget*>(button), "win");
    if (group == nullptr) {
        return;  // Not attached to the window's action group yet.
    }

    GAction* action = g_action_map_lookup_action(G_ACTION_MAP(group), actionName.c_str());
    if (action == nullptr) {
        g_warning("Tool property row: the window has no action \"win.%s\"", actionName.c_str());
        return;
    }

    xoj::util::GVariantSPtr state(g_action_get_state(action), xoj::util::adopt);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button), g_variant_equal(target, state.get()));
}

struct StrokePreviewData {
    Color color;
    double thickness;
    bool dashed;
};

constexpr auto STROKE_PREVIEW_KEY = "xoj-stroke-preview-data";

gboolean drawStrokePreview(GtkWidget* widget, cairo_t* cr, gpointer data) {
    const auto* preview = static_cast<const StrokePreviewData*>(data);

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);

    const double centre = allocation.height / 2.0;
    // ToolHandler reports thicknesses from 0.42 to 30. The sample has a fixed size, so the
    // thickness is mapped into it rather than drawn to scale: a very fine pen would otherwise be
    // invisible and a very thick highlighter would fill the whole row.
    const double lineWidth = std::clamp(preview->thickness, 1.0, allocation.height - 2.0);

    cairo_set_line_width(cr, lineWidth);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    if (preview->dashed) {
        const double dashes[] = {4.0, 3.0};
        cairo_set_dash(cr, dashes, 2, 0.0);
    }

    cairo_set_source_rgba(cr, preview->color.red / 255.0, preview->color.green / 255.0, preview->color.blue / 255.0,
                          preview->color.alpha / 255.0);

    cairo_move_to(cr, 2.0, centre);
    cairo_line_to(cr, allocation.width - 2.0, centre);
    cairo_stroke(cr);
    return TRUE;
}

void freeStrokePreviewData(gpointer data) { delete static_cast<StrokePreviewData*>(data); }

}  // namespace

GtkWidget* makeStrokePreview(Color color, double thickness, bool dashed, int width, int height) {
    GtkWidget* area = gtk_drawing_area_new();
    gtk_widget_set_size_request(area, width, height);
    gtk_widget_add_css_class(area, "xoj-stroke-preview");

    // The sample data belongs to the widget and goes away with it.
    auto* data = new StrokePreviewData{color, thickness, dashed};
    g_object_set_data_full(G_OBJECT(area), STROKE_PREVIEW_KEY, data, freeStrokePreviewData);
    g_signal_connect(area, "draw", G_CALLBACK(drawStrokePreview), data);

    return area;
}

void updateStrokePreview(GtkWidget* preview, Color color, double thickness, bool dashed) {
    auto* data = static_cast<StrokePreviewData*>(g_object_get_data(G_OBJECT(preview), STROKE_PREVIEW_KEY));
    if (data == nullptr) {
        return;
    }

    data->color = color;
    data->thickness = thickness;
    data->dashed = dashed;
    gtk_widget_queue_draw(preview);
}

void ActionRadioGroup::syncFromActions() const {
    for (const Row& row: this->rows) {
        syncActionRow(row.button, row.action, row.target.get());
    }
}

}  // namespace xoj::toolbar

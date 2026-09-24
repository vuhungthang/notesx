#include "ToolPropertyPanel.h"

#include <algorithm>  // for find_if
#include <string>
#include <utility>  // for move

#include <cairo.h>  // for cairo_arc, cairo_fill_preserve
#include <glib.h>   // for g_warning
#include <gtk/gtk.h>

#include "control/settings/Settings.h"  // for Settings
#include "enums/Action.enum.h"          // for Action, Action_toString
#include "util/gtk4_helper.h"           // for gtk_box_append, gtk_widget_add_css_class
#include "util/i18n.h"                  // for _

using xoj::toolbar::makeLabelledRow;
using xoj::toolbar::makeSectionHeading;
using xoj::toolbar::makeStrokePreview;
using xoj::toolbar::updateStrokePreview;

namespace {

/// The widths the popover offers, with a semantic label rather than a bare icon.
struct SizeEntry {
    ToolSize size;
    const char* label;
};

constexpr SizeEntry SIZE_ENTRIES[] = {
        {TOOL_SIZE_VERY_FINE, "Very fine"},   {TOOL_SIZE_FINE, "Fine"},
        {TOOL_SIZE_MEDIUM, "Medium"},         {TOOL_SIZE_THICK, "Thick"},
        {TOOL_SIZE_VERY_THICK, "Very thick"},
};

/// What a preset row button needs to act on its preset.
struct RowData {
    ToolPropertyPanel* panel;
    std::string presetId;
};

void freeRowData(gpointer data, GObject*) { delete static_cast<RowData*>(data); }

RowData* attachRowData(GtkWidget* widget, ToolPropertyPanel* panel, const std::string& presetId) {
    auto* data = new RowData{panel, presetId};
    g_object_weak_ref(G_OBJECT(widget), freeRowData, data);
    return data;
}

void onApplyPresetClicked(GtkButton*, gpointer data) {
    static_cast<RowData*>(data)->panel->applyStoredPreset(static_cast<RowData*>(data)->presetId);
}

void onFavoriteToggled(GtkToggleButton* btn, gpointer data) {
    auto* rowData = static_cast<RowData*>(data);
    const bool wanted = gtk_toggle_button_get_active(btn);
    // A programmatic update already agrees with the stored list; only a real click differs.
    if (rowData->panel->isPresetFavorite(rowData->presetId) == wanted) {
        return;
    }
    rowData->panel->setPresetFavorite(rowData->presetId, wanted);
}

struct MoveData {
    ToolPropertyPanel* panel;
    std::string presetId;
    int delta;
};

void freeMoveData(gpointer data, GObject*) { delete static_cast<MoveData*>(data); }

void onMovePresetClicked(GtkButton*, gpointer data) {
    auto* moveData = static_cast<MoveData*>(data);
    moveData->panel->movePresetFavorite(moveData->presetId, moveData->delta);
}

void onRenamePresetClicked(GtkButton*, gpointer data) {
    static_cast<RowData*>(data)->panel->renamePreset(static_cast<RowData*>(data)->presetId);
}

void onRemovePresetClicked(GtkButton*, gpointer data) {
    static_cast<RowData*>(data)->panel->removePreset(static_cast<RowData*>(data)->presetId);
}

void onSavePresetClicked(GtkButton*, gpointer self) { static_cast<ToolPropertyPanel*>(self)->saveCurrentAsPreset(); }

void onRestorePresetsClicked(GtkButton*, gpointer self) {
    static_cast<ToolPropertyPanel*>(self)->restoreExamplePresets();
}

void onFavoriteCountChanged(GtkSpinButton* spin, gpointer self) {
    static_cast<ToolPropertyPanel*>(self)->setFavoritePresetCount(gtk_spin_button_get_value_as_int(spin));
}

}  // namespace

PresetListListener::~PresetListListener() = default;

gboolean ToolPropertyPanel::drawColorSwatch(GtkWidget* widget, cairo_t* cr, gpointer self) {
    const auto* panel = static_cast<const ToolPropertyPanel*>(self);

    GtkAllocation allocation;
    gtk_widget_get_allocation(widget, &allocation);
    const double radius = std::min(allocation.width, allocation.height) / 2.0 - 1.0;
    if (radius <= 0.0) {
        return TRUE;
    }

    cairo_arc(cr, allocation.width / 2.0, allocation.height / 2.0, radius, 0.0, 2.0 * G_PI);
    cairo_set_source_rgba(cr, panel->currentColor.red / 255.0, panel->currentColor.green / 255.0,
                          panel->currentColor.blue / 255.0, panel->currentColor.alpha / 255.0);
    cairo_fill_preserve(cr);

    // A hairline outline, so a white or transparent stroke is still visible on a light surface.
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.0, 0.4);
    cairo_set_line_width(cr, 1.0);
    cairo_stroke(cr);
    return TRUE;
}

ToolPropertyPanel::ToolPropertyPanel(ToolConfigAdapter& adapter, Settings& settings, ToolPropertyProvider& provider,
                                     GtkWindow* parent, PresetListListener* presetsListener):
        adapter(adapter), settings(settings), provider(provider), parent(parent), presetsListener(presetsListener) {
    // Registering from the constructor is safe: addObserver() does not notify, it only records
    // the observer, and the panel reads the state once while it builds itself.
    this->adapter.addObserver(this);
}

ToolPropertyPanel::~ToolPropertyPanel() {
    this->adapter.removeObserver(this);
    // The row set holds widgets that live in the panel, so it goes away with it.
    this->rows.reset();
}

void ToolPropertyPanel::selectOwnTool() { this->adapter.selectTool(this->provider.getToolType()); }

void ToolPropertyPanel::onPopoverShown(GtkWidget*, gpointer self) {
    static_cast<ToolPropertyPanel*>(self)->selectOwnTool();
}

auto ToolPropertyPanel::createWidget() -> GtkWidget* {
    const ToolConfigState state = this->adapter.getState();

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_add_css_class(box, "xoj-tool-properties");

    this->buildHeader(box);
    this->buildSizeRow(box, state);
    this->buildColorRow(box);
    this->buildFillRow(box);

    // The tool specific rows come from the provider, so the shape and selection panels can be
    // added later without touching this class.
    this->rows = this->provider.createRows(this->adapter, this->parent);
    if (this->rows != nullptr) {
        this->providerRows = this->rows->getWidget();
        gtk_box_append(GTK_BOX(box), this->providerRows);
    }

    this->buildPresetSection(box);

    this->toolConfigChanged(state);
    return box;
}

void ToolPropertyPanel::buildHeader(GtkWidget* box) {
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_add_css_class(header, "xoj-property-header");

    GtkWidget* icon = gtk_image_new_from_icon_name(this->provider.getIconName().c_str(), GTK_ICON_SIZE_LARGE_TOOLBAR);
    gtk_box_append(GTK_BOX(header), icon);

    GtkWidget* title = gtk_label_new(this->provider.getTitle().c_str());
    gtk_widget_add_css_class(title, "xoj-property-title");
    gtk_widget_set_halign(title, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(header), title);

    gtk_box_append(GTK_BOX(box), header);

    // A large sample of the current stroke: colour, thickness and dash pattern at once.
    this->strokePreview = makeStrokePreview(Color(), 1.0, false, 160, 22);
    gtk_widget_set_halign(this->strokePreview, GTK_ALIGN_FILL);
    gtk_box_append(GTK_BOX(box), this->strokePreview);
}

void ToolPropertyPanel::buildSizeRow(GtkWidget* box, const ToolConfigState& state) {
    GtkWidget* rows = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

    const ToolType toolType = this->provider.getToolType();
    for (const SizeEntry& entry: SIZE_ENTRIES) {
        // The sample is drawn with the thickness this width actually gives, so the choice is made
        // on the result and not on a word.
        GtkWidget* preview =
                makeStrokePreview(state.color, this->adapter.getThickness(toolType, entry.size), false, 40, 16);
        GtkWidget* btn =
                this->sizeGroup.addRow(Action_toString(Action::TOOL_SIZE), entry.size, _(entry.label), preview);
        gtk_box_append(GTK_BOX(rows), btn);
    }

    this->sizeRow = makeLabelledRow(_("Width"), rows);
    gtk_box_append(GTK_BOX(box), this->sizeRow);
}

void ToolPropertyPanel::buildColorRow(GtkWidget* box) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    // The current colour, as a swatch rather than a word.
    this->colorSwatch = gtk_drawing_area_new();
    gtk_widget_set_size_request(this->colorSwatch, 24, 24);
    gtk_widget_add_css_class(this->colorSwatch, "xoj-color-swatch");
    g_signal_connect(this->colorSwatch, "draw", G_CALLBACK(drawColorSwatch), this);
    gtk_box_append(GTK_BOX(row), this->colorSwatch);

    // The full palette is the existing colour chooser: the popover does not grow a second one.
    GtkWidget* more = gtk_button_new_with_label(_("More colours…"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(more),
                                   ("win." + std::string(Action_toString(Action::SELECT_COLOR))).c_str());
    gtk_widget_add_css_class(more, "xoj-control");
    gtk_widget_add_css_class(more, "xoj-focus-ring");
    gtk_box_append(GTK_BOX(row), more);

    this->colorRow = makeLabelledRow(_("Colour"), row);
    gtk_box_append(GTK_BOX(box), this->colorRow);
}

void ToolPropertyPanel::buildFillRow(GtkWidget* box) {
    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);

    GtkWidget* fillToggle = gtk_check_button_new_with_label(_("Fill"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(fillToggle),
                                   ("win." + std::string(Action_toString(Action::TOOL_FILL))).c_str());
    gtk_widget_add_css_class(fillToggle, "xoj-control");
    gtk_widget_add_css_class(fillToggle, "xoj-focus-ring");
    gtk_box_append(GTK_BOX(row), fillToggle);

    // The fill opacity keeps its existing palette instead of a second slider that would have to be
    // kept in step with it.
    GtkWidget* opacity = gtk_button_new_with_label(_("Fill opacity…"));
    gtk_actionable_set_action_name(GTK_ACTIONABLE(opacity),
                                   ("win." + std::string(Action_toString(Action::TOOL_FILL_OPACITY))).c_str());
    gtk_widget_add_css_class(opacity, "xoj-control");
    gtk_widget_add_css_class(opacity, "xoj-focus-ring");
    gtk_box_append(GTK_BOX(row), opacity);

    this->fillRow = makeLabelledRow(_("Fill"), row);
    gtk_box_append(GTK_BOX(box), this->fillRow);
}

void ToolPropertyPanel::buildPresetSection(GtkWidget* box) {
    /*
     * The panel itself is the callback data, and it is not a GObject. g_signal_connect_object()
     * would refuse the connection outright (g_return_val_if_fail(G_IS_OBJECT(gobject)) returns
     * before connecting), leaving every one of these controls silently dead. A plain
     * g_signal_connect() is what the rest of this panel uses for the same reason - see
     * drawColorSwatch - and it is safe here because these widgets are created by, and destroyed
     * with, this panel's own content.
     */
    gtk_box_append(GTK_BOX(box), makeSectionHeading(_("Presets")));

    GtkWidget* save = gtk_button_new_with_label(_("Save current as preset…"));
    gtk_widget_add_css_class(save, "xoj-control");
    gtk_widget_add_css_class(save, "xoj-focus-ring");
    g_signal_connect(save, "clicked", G_CALLBACK(onSavePresetClicked), this);
    gtk_box_append(GTK_BOX(box), save);

    this->presetRows = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_box_append(GTK_BOX(box), this->presetRows);
    this->rebuildPresetRows();

    GtkWidget* restore = gtk_button_new_with_label(_("Restore examples"));
    gtk_widget_add_css_class(restore, "xoj-control");
    gtk_widget_add_css_class(restore, "xoj-focus-ring");
    g_signal_connect(restore, "clicked", G_CALLBACK(onRestorePresetsClicked), this);
    gtk_box_append(GTK_BOX(box), restore);

    {  // How many favourites the toolbar shows. A spin button, so the value is also typeable.
        GtkWidget* count = gtk_spin_button_new_with_range(0.0, static_cast<double>(ToolPresetList::MAX_FAVORITES), 1.0);
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(count), this->settings.getFavoritePresetCount());
        gtk_widget_add_css_class(count, "xoj-control");
        g_signal_connect(count, "value-changed", G_CALLBACK(onFavoriteCountChanged), this);
        gtk_box_append(GTK_BOX(box), makeLabelledRow(_("Favourites in the toolbar"), count));
    }

    this->statusLabel = gtk_label_new("");
    gtk_widget_set_halign(this->statusLabel, GTK_ALIGN_START);
    gtk_widget_add_css_class(this->statusLabel, "xoj-status-line");
    // A status line, so a reader who cannot see the list change still learns the result.
    atk_object_set_role(gtk_widget_get_accessible(this->statusLabel), ATK_ROLE_STATUSBAR);
    gtk_box_append(GTK_BOX(box), this->statusLabel);
}

void ToolPropertyPanel::rebuildPresetRows() {
    if (this->rebuildingPresets || this->presetRows == nullptr) {
        return;
    }
    this->rebuildingPresets = true;

    // GTK3 has no gtk_widget_get_first_child(), so the children are collected first.
    for (GList* children = gtk_container_get_children(GTK_CONTAINER(this->presetRows)); children != nullptr;
         children = children->next) {
        gtk_container_remove(GTK_CONTAINER(this->presetRows), GTK_WIDGET(children->data));
    }

    const ToolPresetList& presets = this->settings.getToolPresets();
    // Favourites first, in their own order; the rest follows in list order. The popover shows the
    // complete list, the toolbar only the favourites.
    std::size_t favoriteIndex = 0;
    for (const ToolPreset* favorite: presets.getFavorites()) {
        gtk_box_append(GTK_BOX(this->presetRows), this->createPresetRow(*favorite, favoriteIndex, true));
        favoriteIndex++;
    }
    for (const ToolPreset& preset: presets.getPresets()) {
        if (preset.favoriteOrder) {
            continue;
        }
        gtk_box_append(GTK_BOX(this->presetRows), this->createPresetRow(preset, 0, false));
    }

    this->rebuildingPresets = false;
}

auto ToolPropertyPanel::createPresetRow(const ToolPreset& preset, std::size_t favoriteIndex, bool isFavorite)
        -> GtkWidget* {
    const std::size_t favoriteCount = this->settings.getToolPresets().getFavorites().size();

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    gtk_widget_add_css_class(row, "xoj-preset-row");

    {  // Apply: the preset's own name is the row's primary control, so it takes the room - and
       // ellipsizes rather than demands it, so a long name widens nothing.
        GtkWidget* apply = gtk_button_new_with_label(preset.name.c_str());
        gtk_widget_set_hexpand(apply, true);
        if (GtkWidget* label = gtk_bin_get_child(GTK_BIN(apply)); GTK_IS_LABEL(label)) {
            // Ellipsize caps the minimum; max-width-chars caps the natural width too, which is the
            // one the popover grows to. Together a long name reads as "Black fine p…" instead of
            // widening the panel; the full name is in the tooltip.
            gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
            gtk_label_set_max_width_chars(GTK_LABEL(label), 12);
        }
        gtk_widget_add_css_class(apply, "xoj-control");
        gtk_widget_add_css_class(apply, "xoj-focus-ring");
        gtk_widget_set_tooltip_text(apply, preset.name.c_str());
        atk_object_set_name(gtk_widget_get_accessible(apply), preset.name.c_str());

        RowData* data = attachRowData(apply, this, preset.id);
        g_signal_connect_data(apply, "clicked", G_CALLBACK(onApplyPresetClicked), data, nullptr, GConnectFlags(0));
        gtk_box_append(GTK_BOX(row), apply);
    }

    /*
     * The row's other controls are icon buttons, not labelled ones: five text buttons a row - this
     * one plus "Favourite", "Up", "Down", "Rename" and "Remove" - asked for more width than the
     * popover has, which squeezed the whole panel to its minimum. An icon says the same thing to a
     * screen reader through the accessible name, and to a sighted user through the tooltip.
     */
    auto iconButton = [](const char* icon, const char* name, const char* tooltip) {
        GtkWidget* btn = gtk_button_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
        gtk_widget_add_css_class(btn, "xoj-control");
        gtk_widget_add_css_class(btn, "xoj-focus-ring");
        gtk_widget_set_tooltip_text(btn, tooltip);
        atk_object_set_name(gtk_widget_get_accessible(btn), name);
        return btn;
    };

    {  // Favourite: a toggle, so the star shows the state the preset is in.
        GtkWidget* favorite = gtk_toggle_button_new();
        gtk_widget_add_css_class(favorite, "xoj-control");
        gtk_widget_add_css_class(favorite, "xoj-focus-ring");
        gtk_widget_add_css_class(favorite, "xoj-preset-favorite");
        GtkWidget* star = gtk_image_new_from_icon_name("xopp-star", GTK_ICON_SIZE_BUTTON);
        gtk_button_set_image(GTK_BUTTON(favorite), star);
        gtk_widget_set_tooltip_text(favorite, isFavorite ? _("Remove from favourites") : _("Add to favourites"));
        atk_object_set_name(gtk_widget_get_accessible(favorite),
                            isFavorite ? _("Remove from favourites") : _("Add to favourites"));

        RowData* data = attachRowData(favorite, this, preset.id);
        g_signal_connect_data(favorite, "toggled", G_CALLBACK(onFavoriteToggled), data, nullptr, GConnectFlags(0));
        // Set after the handler is connected: the handler compares against the stored list, so a
        // programmatic update is recognised and does not change anything.
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(favorite), isFavorite);
        gtk_box_append(GTK_BOX(row), favorite);
    }

    {  // Reorder: only the favourite order is displayed, so only favourites can be moved.
        GtkWidget* up = iconButton("xopp-preset-up", _("Move up"), _("Move up in the favourites"));
        gtk_widget_set_sensitive(up, isFavorite && favoriteIndex > 0);
        auto* moveUp = new MoveData{this, preset.id, -1};
        g_object_weak_ref(G_OBJECT(up), freeMoveData, moveUp);
        g_signal_connect_data(up, "clicked", G_CALLBACK(onMovePresetClicked), moveUp, nullptr, GConnectFlags(0));
        gtk_box_append(GTK_BOX(row), up);

        GtkWidget* down = iconButton("xopp-preset-down", _("Move down"), _("Move down in the favourites"));
        gtk_widget_set_sensitive(down, isFavorite && favoriteIndex + 1 < favoriteCount);
        auto* moveDown = new MoveData{this, preset.id, 1};
        g_object_weak_ref(G_OBJECT(down), freeMoveData, moveDown);
        g_signal_connect_data(down, "clicked", G_CALLBACK(onMovePresetClicked), moveDown, nullptr, GConnectFlags(0));
        gtk_box_append(GTK_BOX(row), down);
    }

    {  // Rename
        GtkWidget* rename = iconButton("xopp-preset-rename", _("Rename preset"), _("Rename preset"));
        RowData* data = attachRowData(rename, this, preset.id);
        g_signal_connect_data(rename, "clicked", G_CALLBACK(onRenamePresetClicked), data, nullptr, GConnectFlags(0));
        gtk_box_append(GTK_BOX(row), rename);
    }

    {  // Remove, with a confirmation: there is no undo for a preset.
        GtkWidget* remove = iconButton("xopp-preset-remove", _("Remove preset"), _("Remove preset"));
        RowData* data = attachRowData(remove, this, preset.id);
        g_signal_connect_data(remove, "clicked", G_CALLBACK(onRemovePresetClicked), data, nullptr, GConnectFlags(0));
        gtk_box_append(GTK_BOX(row), remove);
    }

    return row;
}

void ToolPropertyPanel::changePresets(const std::function<void(ToolPresetList&)>& change, const std::string& status) {
    ToolPresetList presets = this->settings.getToolPresets();
    change(presets);
    this->settings.setToolPresets(std::move(presets));

    this->setStatus(status);
    this->rebuildPresetRows();

    if (this->presetsListener != nullptr) {
        this->presetsListener->presetListChanged();
    }
}

void ToolPropertyPanel::setStatus(const std::string& text) {
    if (this->statusLabel != nullptr) {
        gtk_label_set_text(GTK_LABEL(this->statusLabel), text.c_str());
    }
}

auto ToolPropertyPanel::promptForName(const std::string& title, const std::string& initial) const -> std::string {
    GtkWidget* dialog = gtk_dialog_new_with_buttons(title.c_str(), this->parent, GTK_DIALOG_MODAL, _("Cancel"),
                                                    GTK_RESPONSE_CANCEL, _("OK"), GTK_RESPONSE_OK, nullptr);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), initial.c_str());
    gtk_box_append(GTK_BOX(content), entry);
    gtk_widget_show_all(dialog);

    std::string result;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
        result = gtk_entry_get_text(GTK_ENTRY(entry));
    }
    gtk_widget_destroy(dialog);
    return result;
}

auto ToolPropertyPanel::isPresetFavorite(const std::string& presetId) const -> bool {
    return this->settings.getToolPresets().isFavorite(presetId);
}

void ToolPropertyPanel::applyStoredPreset(const std::string& presetId) {
    const ToolPreset* preset = this->settings.getToolPresets().findById(presetId);
    if (preset == nullptr) {
        g_warning("Tool property panel: no preset with id \"%s\"", presetId.c_str());
        return;
    }

    if (!this->adapter.applyPreset(*preset)) {
        this->setStatus(_("That preset could not be applied"));
        return;
    }
    this->setStatus(FS(_F("Applied preset \"{1}\"") % preset->name));
}

void ToolPropertyPanel::saveCurrentAsPreset() {
    const std::string name = this->promptForName(_("Save preset"), this->provider.getTitle());
    if (name.empty()) {
        return;  // Cancelled, or an empty name, which the list would reject anyway.
    }

    const ToolPreset captured = this->adapter.capturePreset(name);
    this->changePresets([&captured](ToolPresetList& presets) { presets.add(captured); },
                        FS(_F("Saved preset \"{1}\"") % name));
}

void ToolPropertyPanel::setPresetFavorite(const std::string& presetId, bool favorite) {
    const ToolPreset* preset = this->settings.getToolPresets().findById(presetId);
    const std::string name = preset != nullptr ? preset->name : std::string();

    bool accepted = false;
    this->changePresets([&presetId, favorite,
                         &accepted](ToolPresetList& presets) { accepted = presets.setFavorite(presetId, favorite); },
                        favorite ? FS(_F("Added \"{1}\" to the favourites") % name) :
                                   FS(_F("Removed \"{1}\" from the favourites") % name));

    if (!accepted && favorite) {
        // The row has to go back to where it was: the list refused the change.
        this->setStatus(
                FS(_F("At most {1} presets can be favourites") % std::to_string(ToolPresetList::MAX_FAVORITES)));
    }
}

void ToolPropertyPanel::movePresetFavorite(const std::string& presetId, int delta) {
    const ToolPresetList& presets = this->settings.getToolPresets();
    const std::vector<const ToolPreset*> favorites = presets.getFavorites();

    auto it = std::find_if(favorites.begin(), favorites.end(),
                           [&presetId](const ToolPreset* p) { return p->id == presetId; });
    if (it == favorites.end()) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(it - favorites.begin());
    if (delta < 0 && index == 0) {
        return;
    }
    const std::size_t target = delta < 0 ? index - 1 : index + 1;
    if (target >= favorites.size()) {
        return;
    }

    this->changePresets([&presetId, target](ToolPresetList& list) { list.moveFavorite(presetId, target); },
                        _("Favourite order updated"));
}

void ToolPropertyPanel::renamePreset(const std::string& presetId) {
    const ToolPreset* preset = this->settings.getToolPresets().findById(presetId);
    if (preset == nullptr) {
        return;
    }

    const std::string name = this->promptForName(_("Rename preset"), preset->name);
    if (name.empty()) {
        return;
    }

    this->changePresets([&presetId, &name](ToolPresetList& presets) { presets.rename(presetId, name); },
                        FS(_F("Renamed preset to \"{1}\"") % name));
}

void ToolPropertyPanel::removePreset(const std::string& presetId) {
    const ToolPreset* preset = this->settings.getToolPresets().findById(presetId);
    if (preset == nullptr) {
        return;
    }
    const std::string name = preset->name;

    // There is no undo for a preset, so the removal is confirmed first.
    GtkWidget* dialog = gtk_message_dialog_new(this->parent, GTK_DIALOG_MODAL, GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE,
                                               _("Remove the preset \"%s\"?"), name.c_str());
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), _("Cancel"), GTK_RESPONSE_CANCEL, _("Remove"), GTK_RESPONSE_OK, nullptr);
    const int response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);

    if (response != GTK_RESPONSE_OK) {
        return;
    }

    this->changePresets([&presetId](ToolPresetList& presets) { presets.remove(presetId); },
                        FS(_F("Removed preset \"{1}\"") % name));
}

void ToolPropertyPanel::restoreExamplePresets() {
    this->changePresets([](ToolPresetList& presets) { presets.restoreBuiltins(); }, _("Examples restored"));
}

void ToolPropertyPanel::setFavoritePresetCount(int count) {
    const int bounded = std::clamp(count, 0, static_cast<int>(ToolPresetList::MAX_FAVORITES));
    if (bounded == this->settings.getFavoritePresetCount()) {
        return;
    }

    this->settings.setFavoritePresetCount(bounded);
    this->setStatus(FS(_F("The toolbar now shows {1} favourite presets") % std::to_string(bounded)));

    if (this->presetsListener != nullptr) {
        this->presetsListener->presetListChanged();
    }
}

void ToolPropertyPanel::toolConfigChanged(const ToolConfigState& state) {
    if (state.hasColor) {
        this->currentColor = state.color;
    }

    if (this->strokePreview != nullptr) {
        const bool dashed = state.hasLineStyle && state.lineStyle != "plain";
        updateStrokePreview(this->strokePreview, state.hasColor ? state.color : Color(), state.thickness, dashed);
    }

    if (this->sizeRow != nullptr) {
        gtk_widget_set_visible(this->sizeRow, state.hasSize);
        this->sizeGroup.syncFromActions();
    }
    if (this->colorRow != nullptr) {
        gtk_widget_set_visible(this->colorRow, state.hasColor);
    }
    if (this->colorSwatch != nullptr) {
        // The swatch draws the colour itself, so it needs no palette lookup.
        gtk_widget_queue_draw(this->colorSwatch);
    }
    if (this->fillRow != nullptr) {
        gtk_widget_set_visible(this->fillRow, state.hasFill);
    }

    if (this->rows != nullptr) {
        this->rows->toolConfigChanged(state);
    }
}

#include "CommandPalette.h"

#include <algorithm>  // for find_if, clamp
#include <utility>    // for move

#include <atk/atk.h>  // for atk_object_set_name
#include <gtk/gtk.h>  // for gtk_popover_*, gtk_list_box_*

#include "control/settings/Settings.h"  // for Settings
#include "util/i18n.h"                  // for _

using xoj::command::CommandEntry;
using xoj::command::CommandMetadata;

namespace xoj::command {

namespace {

constexpr int ROW_MARGIN = 6;
constexpr int PALETTE_WIDTH = 560;
constexpr int LIST_MAX_HEIGHT = 380;
/// How many rows the palette builds at most: a query that matches everything must not build a row
/// per command of the application. What is past this is one more keystroke away.
constexpr size_t ORDER_LIMIT = 200;

/*
 * Whether the keyboard may be handed back to this widget. A widget that is gone, or that takes no
 * focus at all - the canvas is one, it receives the pen and the keyboard through the window, not by
 * being the focus widget - would leave the user typing into nothing.
 */
auto canTakeFocus(GtkWidget* widget) -> bool {
    return widget != nullptr && gtk_widget_get_visible(widget) && gtk_widget_get_can_focus(widget);
}

/// A label of the second rank of a row: the category, the shortcut, the reason.
auto dimLabel(const char* text) -> GtkWidget* {
    GtkWidget* label = gtk_label_new(text);
    gtk_widget_set_opacity(label, 0.7);
    gtk_label_set_xalign(GTK_LABEL(label), 1.0);
    return label;
}

/// The line that says why a command cannot be run, under the title it belongs to.
auto reasonLabel(const char* text) -> GtkWidget* {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    gtk_widget_set_opacity(label, 0.6);

    PangoAttrList* attributes = pango_attr_list_new();
    pango_attr_list_insert(attributes, pango_attr_scale_new(PANGO_SCALE_SMALL));
    gtk_label_set_attributes(GTK_LABEL(label), attributes);
    pango_attr_list_unref(attributes);
    return label;
}

}  // namespace

CommandPalette::CommandPalette(GtkWindow* window, GtkWidget* fallbackFocus, Settings* settings,
                               RegistryProvider registryProvider, CommandRegistry::Maps maps):
        window(window),
        fallbackFocus(fallbackFocus, xoj::util::ref),
        settings(settings),
        registryProvider(std::move(registryProvider)),
        maps(maps) {
    /*
     * A popover is anchored to a widget *inside* its window: GTK asserts when it is handed the window
     * itself ("gtk_widget_is_ancestor (parent, window)"). The window's own child fills it, so
     * anchoring there places the palette over the window's area, along its top edge.
     */
    GtkWidget* anchor = gtk_bin_get_child(GTK_BIN(window));
    if (anchor == nullptr) {
        anchor = fallbackFocus;
    }
    GtkWidget* popover = gtk_popover_new(anchor);
    // Nonmodal: the palette is a way of finding a command, not a question that has to be answered
    // before the application can be used again.
    gtk_popover_set_modal(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_TOP);
    /*
     * No transition. What a popover fade buys a palette opened by a keystroke is a frame of the
     * window underneath; what it costs is a callback GTK keeps on the widget for the length of the
     * animation, which is a callback that can still arrive when the window around the palette is
     * being destroyed - the palette is not the only thing that ends then. Showing and hiding it now
     * is both what the keyboard expects and what keeps the teardown free of animations to finish.
     */
    gtk_popover_set_transitions_enabled(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_size_request(popover, PALETTE_WIDTH, -1);
    this->popover.reset(popover, xoj::util::refsink);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(popover), box);

    GtkWidget* entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Type a command"));
    gtk_widget_set_margin_top(entry, ROW_MARGIN);
    gtk_widget_set_margin_bottom(entry, ROW_MARGIN);
    gtk_widget_set_margin_start(entry, ROW_MARGIN);
    gtk_widget_set_margin_end(entry, ROW_MARGIN);
    setAccessibleName(entry, _("Search commands"));
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    this->entry.reset(entry, xoj::util::refsink);

    GtkWidget* list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_SINGLE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list), TRUE);
    gtk_widget_set_margin_bottom(list, ROW_MARGIN);
    setAccessibleName(list, _("Commands"));

    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, LIST_MAX_HEIGHT);
    gtk_container_add(GTK_CONTAINER(scrolled), list);
    gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);
    this->list.reset(list, xoj::util::ref);
    this->scrolled.reset(scrolled, xoj::util::ref);

    this->queryHandlerId = g_signal_connect_swapped(
            entry, "changed", G_CALLBACK(+[](CommandPalette* self) { self->onQueryChanged(); }), this);
    // Enter runs the command the keyboard is on; the list never takes the focus away from the entry,
    // so typing and choosing are the same keystrokes.
    this->activateHandlerId = g_signal_connect_swapped(
            entry, "activate", G_CALLBACK(+[](CommandPalette* self) { self->activateSelected(); }), this);
    this->keyHandlerId = g_signal_connect_swapped(
            entry, "key-press-event", G_CALLBACK(+[](CommandPalette* self, GdkEventKey* event) -> gboolean {
                if (event->keyval == GDK_KEY_Escape) {
                    self->close();
                    return TRUE;
                }
                if (event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_Up ||
                    event->keyval == GDK_KEY_Page_Down || event->keyval == GDK_KEY_Page_Up) {
                    const int rows = event->keyval == GDK_KEY_Page_Down || event->keyval == GDK_KEY_Page_Up ? 10 : 1;
                    const int direction =
                            event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_Page_Down ? rows : -rows;
                    self->moveSelection(direction);
                    return TRUE;
                }
                return FALSE;
            }),
            this);
    // A click or a double click on a row runs it, with the same refusal for a disabled command.
    this->rowActivatedHandlerId =
            g_signal_connect_swapped(list, "row-activated", G_CALLBACK(+[](CommandPalette* self, GtkListBoxRow* row) {
                                         const int index = gtk_list_box_row_get_index(row);
                                         if (index >= 0) {
                                             self->selectRow(static_cast<size_t>(index));
                                             self->activateSelected();
                                         }
                                     }),
                                     this);
    /*
     * The keyboard is given back when the popover has been put away, not before: GTK moves the focus
     * to the window's default widget as soon as the widget that had it is unmapped, and the entry
     * inside the popover is that widget. Giving the focus back on "closed" - which is emitted once
     * the popover is really gone - is what makes the focus land where the user was instead of on the
     * canvas behind the palette.
     */
    this->closedHandlerId = g_signal_connect_swapped(
            popover, "closed", G_CALLBACK(+[](CommandPalette* self) { self->giveFocusBack(); }), this);

    gtk_widget_show_all(box);
}

CommandPalette::~CommandPalette() {
    /*
     * A palette that is still open when its window goes is put away here, while the widgets it is
     * made of are still there: a popover left visible is a popover GTK keeps working on, and the
     * window it is anchored to is not necessarily the thing that is destroyed first. Hiding it
     * outright rather than popping it down is deliberate - the pop-down is a transition, and a
     * destructor must not leave an animation behind to finish on a window that is going away.
     *
     * Taking the "closed" handler off first: that one hands the keyboard back, and there is no
     * keyboard to hand it back to once the window this palette belongs to is going away.
     */
    if (this->popover) {
        if (this->closedHandlerId != 0) {
            g_signal_handler_disconnect(this->popover.get(), this->closedHandlerId);
            this->closedHandlerId = 0;
        }
        gtk_widget_set_visible(this->popover.get(), FALSE);
    }

    // The handlers are on widgets this object owned; take them off so that a signal that is already
    // queued cannot arrive at an object that is going away.
    if (this->entry) {
        g_signal_handler_disconnect(this->entry.get(), this->queryHandlerId);
        g_signal_handler_disconnect(this->entry.get(), this->activateHandlerId);
        g_signal_handler_disconnect(this->entry.get(), this->keyHandlerId);
    }
    if (this->list && this->rowActivatedHandlerId != 0) {
        g_signal_handler_disconnect(this->list.get(), this->rowActivatedHandlerId);
    }
}

void CommandPalette::setAccessibleName(GtkWidget* widget, const char* name) {
    AtkObject* accessible = gtk_widget_get_accessible(widget);
    if (accessible != nullptr) {
        atk_object_set_name(accessible, name);
    }
}

auto CommandPalette::isOpen() const -> bool { return this->popover && gtk_widget_is_visible(this->popover.get()); }

void CommandPalette::toggle() {
    if (this->isOpen()) {
        this->close();
    } else {
        this->open();
    }
}

void CommandPalette::open() {
    if (this->popover == nullptr) {
        return;
    }
    this->registry = this->registryProvider ? this->registryProvider() : CommandRegistry();

    // Where the user was: the palette gives the focus back there when it closes, which is what keeps
    // writing - the pen, the keyboard, a text box - exactly where it was interrupted.
    if (GtkWidget* focused = gtk_window_get_focus(this->window)) {
        if (!gtk_widget_is_ancestor(focused, this->popover.get())) {
            this->previousFocus.reset(focused, xoj::util::ref);
        }
    }

    gtk_entry_set_text(GTK_ENTRY(this->entry.get()), "");
    this->rebuild();
    gtk_popover_popup(GTK_POPOVER(this->popover.get()));
    gtk_widget_grab_focus(this->entry.get());
}

void CommandPalette::close() {
    if (this->popover == nullptr || !gtk_widget_is_visible(this->popover.get())) {
        return;
    }
    // Putting it away is all that is left to do here: the keyboard is given back by giveFocusBack(),
    // which "closed" calls once the popover is really gone.
    gtk_popover_popdown(GTK_POPOVER(this->popover.get()));
}

void CommandPalette::giveFocusBack() {
    /*
     * The widget the user was on, when it is still there and can hold the keyboard; nothing
     * otherwise - and never the palette itself, whatever happens: whatever the user was writing at
     * keeps receiving their input.
     */
    GtkWidget* target = this->previousFocus.get();
    if (!canTakeFocus(target)) {
        target = this->fallbackFocus.get();
    }
    if (canTakeFocus(target)) {
        gtk_widget_grab_focus(target);
    } else {
        gtk_window_set_focus(this->window, nullptr);
    }
    this->previousFocus.reset();
}

auto CommandPalette::commandFor(const std::string& commandId) const -> const CommandEntry* {
    return this->registry.findById(commandId);
}

void CommandPalette::onQueryChanged() { this->rebuild(); }

auto CommandPalette::orderFor(const CommandRegistry& commands, const std::string& query) const -> std::vector<size_t> {
    const std::vector<CommandMetadata>& metadata = commands.metadata();
    if (!query.empty()) {
        return rankCommands(metadata, query);
    }

    /*
     * With nothing typed the palette shows what this user ran last, in the order they ran it, and
     * then everything else in registry order. The list is the profile's own memory of the commands;
     * nothing about it is sent anywhere.
     */
    std::vector<size_t> order;
    order.reserve(metadata.size());
    if (this->settings != nullptr) {
        for (const std::string& id: this->settings->getRecentCommands()) {
            for (size_t index = 0; index < metadata.size(); index++) {
                if (metadata[index].id == id && std::find(order.begin(), order.end(), index) == order.end()) {
                    order.emplace_back(index);
                }
            }
        }
    }
    for (size_t index = 0; index < metadata.size(); index++) {
        if (std::find(order.begin(), order.end(), index) == order.end()) {
            order.emplace_back(index);
        }
    }
    return order;
}

void CommandPalette::rebuild() {
    const std::vector<CommandMetadata>& metadata = this->registry.metadata();
    const std::string query(gtk_entry_get_text(GTK_ENTRY(this->entry.get())));

    std::vector<size_t> order = this->orderFor(this->registry, query);
    while (order.size() > ORDER_LIMIT) {
        order.pop_back();
    }

    GtkListBox* list = GTK_LIST_BOX(this->list.get());
    gtk_container_foreach(
            GTK_CONTAINER(list), [](GtkWidget* widget, gpointer) { gtk_widget_destroy(widget); }, nullptr);

    this->rows.clear();
    this->rows.reserve(order.size());

    for (size_t index: order) {
        const CommandEntry& command = this->registry.all()[index];
        const CommandMetadata& meta = metadata[index];

        Row row;
        row.id = meta.id;
        row.title = meta.title;
        row.category = meta.category;
        row.accelerator = meta.accelerator;
        row.enabled = CommandRegistry::isEnabled(command, this->maps);
        if (!row.enabled) {
            if (const std::optional<std::string> reason = this->registry.disabledReason(command, this->maps)) {
                row.reason = *reason;
            }
        }

        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        GtkWidget* line = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ROW_MARGIN);

        GtkWidget* title = gtk_label_new(meta.title.c_str());
        gtk_label_set_xalign(GTK_LABEL(title), 0.0);
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_END);
        gtk_box_pack_start(GTK_BOX(line), title, TRUE, TRUE, 0);

        if (!meta.category.empty()) {
            gtk_box_pack_start(GTK_BOX(line), dimLabel(meta.category.c_str()), FALSE, FALSE, 0);
        }
        if (!meta.accelerator.empty()) {
            gtk_box_pack_start(GTK_BOX(line), dimLabel(meta.accelerator.c_str()), FALSE, FALSE, 0);
        }

        gtk_box_pack_start(GTK_BOX(box), line, FALSE, FALSE, 0);
        if (!row.reason.empty()) {
            gtk_box_pack_start(GTK_BOX(box), reasonLabel(row.reason.c_str()), FALSE, FALSE, 0);
        }

        gtk_widget_set_margin_top(box, ROW_MARGIN);
        gtk_widget_set_margin_bottom(box, ROW_MARGIN);
        gtk_widget_set_margin_start(box, ROW_MARGIN);
        gtk_widget_set_margin_end(box, ROW_MARGIN);

        GtkWidget* listRow = gtk_list_box_row_new();
        gtk_container_add(GTK_CONTAINER(listRow), box);
        // A command that cannot be run is still shown, greyed and explained: hiding it would leave
        // the user wondering whether the command exists at all. It is refused when it is run.
        gtk_widget_set_opacity(listRow, row.enabled ? 1.0 : 0.5);
        setAccessibleName(listRow, meta.title.c_str());
        gtk_list_box_insert(list, listRow, -1);
        gtk_widget_show_all(listRow);

        this->rows.emplace_back(std::move(row));
    }

    if (!this->rows.empty()) {
        this->selectRow(0);
    }
}

auto CommandPalette::rowCount() const -> size_t { return this->rows.size(); }

void CommandPalette::selectRow(size_t row) {
    if (this->rows.empty() || row >= this->rows.size()) {
        return;
    }
    GtkListBoxRow* listRow = gtk_list_box_get_row_at_index(GTK_LIST_BOX(this->list.get()), static_cast<int>(row));
    if (listRow != nullptr) {
        gtk_list_box_select_row(GTK_LIST_BOX(this->list.get()), listRow);
    }
}

auto CommandPalette::selectedRowIndex() const -> std::optional<size_t> {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(this->list.get()));
    if (row == nullptr) {
        return std::nullopt;
    }
    const int index = gtk_list_box_row_get_index(row);
    if (index < 0 || static_cast<size_t>(index) >= this->rows.size()) {
        return std::nullopt;
    }
    return static_cast<size_t>(index);
}

void CommandPalette::moveSelection(int delta) {
    if (this->rows.empty()) {
        return;
    }
    const size_t current = this->selectedRowIndex().value_or(0);
    const int wanted = static_cast<int>(current) + delta;
    const int clamped = std::clamp(wanted, 0, static_cast<int>(this->rows.size()) - 1);
    this->selectRow(static_cast<size_t>(clamped));
}

auto CommandPalette::activateSelected() -> bool {
    const std::optional<size_t> index = this->selectedRowIndex();
    if (!index) {
        return false;
    }
    const Row& row = this->rows[*index];
    const CommandEntry* command = this->commandFor(row.id);
    if (command == nullptr || !CommandRegistry::isEnabled(*command, this->maps)) {
        return false;  // a disabled command cannot be activated, however it was reached
    }
    if (!CommandRegistry::activate(*command, this->maps)) {
        return false;
    }

    if (this->settings != nullptr) {
        this->settings->addRecentCommand(row.id);
    }
    this->close();
    return true;
}

auto CommandPalette::shownIds() const -> std::vector<std::string> {
    std::vector<std::string> ids;
    ids.reserve(this->rows.size());
    for (const Row& row: this->rows) {
        ids.emplace_back(row.id);
    }
    return ids;
}

auto CommandPalette::selectedId() const -> std::optional<std::string> {
    const std::optional<size_t> index = this->selectedRowIndex();
    if (!index) {
        return std::nullopt;
    }
    return this->rows[*index].id;
}

auto CommandPalette::isSelectedEnabled() const -> bool {
    const std::optional<size_t> index = this->selectedRowIndex();
    return index && this->rows[*index].enabled;
}

auto CommandPalette::acceleratorOf(const std::string& commandId) const -> std::string {
    for (const Row& row: this->rows) {
        if (row.id == commandId) {
            return row.accelerator;
        }
    }
    return {};
}

auto CommandPalette::categoryOf(const std::string& commandId) const -> std::string {
    for (const Row& row: this->rows) {
        if (row.id == commandId) {
            return row.category;
        }
    }
    return {};
}

auto CommandPalette::reasonOf(const std::string& commandId) const -> std::string {
    for (const Row& row: this->rows) {
        if (row.id == commandId) {
            return row.reason;
        }
    }
    return {};
}

auto CommandPalette::getPopover() const -> GtkWidget* { return this->popover.get(); }
auto CommandPalette::getEntry() const -> GtkWidget* { return this->entry.get(); }
auto CommandPalette::getList() const -> GtkWidget* { return this->list.get(); }

auto CommandPalette::getSelectedRow() const -> GtkWidget* {
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(this->list.get()));
    return row != nullptr ? GTK_WIDGET(row) : this->entry.get();
}

}  // namespace xoj::command

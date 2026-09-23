#include "ShortcutReference.h"

#include <algorithm>  // for find
#include <cctype>     // for tolower
#include <utility>    // for move

#include <atk/atk.h>  // for atk_object_set_name

#include "util/gtk4_helper.h"  // for gtk_widget_add_css_class
#include "util/i18n.h"         // for _

using xoj::command::CommandRegistry;
using xoj::command::ReferenceRow;
using xoj::gui::ShortcutReference;

/*
 * Plan 007, step 5: the shortcut reference.
 *
 * The shape of it is the palette's - a popover anchored to a widget of the window, a search entry,
 * a list under it - because that is what "searchable" looks like in this window. What it does with
 * what it finds is the opposite: the palette runs a command, the reference tells the user what is
 * there to run and what it is on. Nothing is written down here: every line comes from the registry
 * the window builds out of the menus, the toolbar items and the accelerators the application holds.
 */

namespace {

constexpr int ROW_MARGIN = 6;
constexpr int REFERENCE_WIDTH = 560;
constexpr int LIST_MAX_HEIGHT = 420;
constexpr size_t ORDER_LIMIT = 400;

/// The widget a row draws to: the title on the left, the keys on the right.
auto addRowLine(GtkWidget* rowBox, const std::string& title, const std::string& accelerator, bool conflicted,
                const char* titleCssClass) -> void {
    GtkWidget* label = gtk_label_new(title.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);
    if (titleCssClass != nullptr) {
        gtk_widget_add_css_class(label, titleCssClass);
    }
    gtk_box_pack_start(GTK_BOX(rowBox), label, TRUE, TRUE, 0);

    if (!accelerator.empty()) {
        GtkWidget* keys = gtk_label_new(accelerator.c_str());
        gtk_widget_add_css_class(keys, "dim-label");
        /*
         * Keys that more than one command claims are marked: the reference is what the user checks
         * before trusting a shortcut, so it says which of them are not going to do one thing.
         */
        if (conflicted) {
            gtk_widget_add_css_class(keys, "error");
            gtk_widget_set_tooltip_text(keys, _("Another command is on these keys too"));
            atk_object_set_name(gtk_widget_get_accessible(keys), _("These keys are also claimed by another command"));
        }
        gtk_box_pack_start(GTK_BOX(rowBox), keys, FALSE, FALSE, 0);
    }
}

/// A heading of the reference: the group the menus put the commands below it in.
auto addCategoryHeading(GtkWidget* list, const std::string& category) -> void {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ROW_MARGIN);
    gtk_widget_set_margin_top(box, 2 * ROW_MARGIN);
    gtk_widget_set_margin_start(box, ROW_MARGIN);
    gtk_widget_set_margin_end(box, ROW_MARGIN);
    gtk_widget_add_css_class(box, "dim-label");
    gtk_box_pack_start(GTK_BOX(box), gtk_label_new(category.c_str()), FALSE, FALSE, 0);

    GtkWidget* row = gtk_list_box_row_new();
    gtk_container_add(GTK_CONTAINER(row), box);
    // A heading is not something the user can pick: it is a word, not a command.
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(row), FALSE);
    gtk_widget_set_can_focus(row, FALSE);
    gtk_list_box_insert(GTK_LIST_BOX(list), row, -1);
}

/// A command: what it is called and the keys it is on.
auto addCommandRow(GtkWidget* list, const ReferenceRow& row) -> void {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ROW_MARGIN);
    gtk_widget_set_margin_start(box, 2 * ROW_MARGIN);
    gtk_widget_set_margin_end(box, 2 * ROW_MARGIN);
    addRowLine(box, row.title, row.accelerator, row.conflicted, nullptr);

    GtkWidget* listRow = gtk_list_box_row_new();
    gtk_container_add(GTK_CONTAINER(listRow), box);
    gtk_list_box_row_set_selectable(GTK_LIST_BOX_ROW(listRow), FALSE);
    gtk_widget_set_can_focus(listRow, FALSE);
    // What a screen reader reads for the line: the command and the keys it is on.
    std::string accessible = row.title;
    if (!row.accelerator.empty()) {
        accessible += " — ";
        accessible += row.accelerator;
        if (row.conflicted) {
            accessible += ", ";
            accessible += _("also claimed by another command");
        }
    }
    atk_object_set_name(gtk_widget_get_accessible(listRow), accessible.c_str());
    gtk_list_box_insert(GTK_LIST_BOX(list), listRow, -1);
}

}  // namespace

ShortcutReference::ShortcutReference(GtkWindow* window, RegistryProvider registryProvider):
        window(window), registryProvider(std::move(registryProvider)) {
    // Anchored to a widget inside the window: GTK asserts when a popover is handed the window itself.
    GtkWidget* anchor = gtk_bin_get_child(GTK_BIN(window));
    g_assert(anchor != nullptr);

    GtkWidget* popover = gtk_popover_new(anchor);
    gtk_popover_set_modal(GTK_POPOVER(popover), FALSE);
    gtk_popover_set_position(GTK_POPOVER(popover), GTK_POS_TOP);
    // No transition: the reference is read, not waited for, and a running animation is a callback
    // that can still arrive while the window it belongs to is being destroyed.
    gtk_popover_set_transitions_enabled(GTK_POPOVER(popover), FALSE);
    gtk_widget_set_size_request(popover, REFERENCE_WIDTH, -1);
    this->popover.reset(popover, xoj::util::refsink);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(popover), box);

    GtkWidget* entry = gtk_search_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), _("Search shortcuts"));
    gtk_widget_set_margin_top(entry, ROW_MARGIN);
    gtk_widget_set_margin_bottom(entry, ROW_MARGIN);
    gtk_widget_set_margin_start(entry, ROW_MARGIN);
    gtk_widget_set_margin_end(entry, ROW_MARGIN);
    setAccessibleName(entry, _("Search the shortcuts"));
    gtk_box_pack_start(GTK_BOX(box), entry, FALSE, FALSE, 0);
    this->entry.reset(entry, xoj::util::refsink);

    GtkWidget* list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_set_margin_bottom(list, ROW_MARGIN);
    setAccessibleName(list, _("Shortcuts"));
    GtkWidget* scrolled = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(scrolled, -1, LIST_MAX_HEIGHT);
    gtk_container_add(GTK_CONTAINER(scrolled), list);
    gtk_box_pack_start(GTK_BOX(box), scrolled, TRUE, TRUE, 0);
    this->list.reset(list, xoj::util::ref);
    this->scrolled.reset(scrolled, xoj::util::ref);

    GtkWidget* status = gtk_label_new("");
    gtk_widget_set_margin_start(status, 2 * ROW_MARGIN);
    gtk_widget_set_margin_end(status, 2 * ROW_MARGIN);
    gtk_widget_set_margin_bottom(status, ROW_MARGIN);
    gtk_label_set_xalign(GTK_LABEL(status), 0.0);
    gtk_widget_add_css_class(status, "dim-label");
    gtk_box_pack_start(GTK_BOX(box), status, FALSE, FALSE, 0);
    this->status.reset(status, xoj::util::ref);

    this->queryHandlerId =
            g_signal_connect_swapped(entry, "changed", G_CALLBACK(+[](ShortcutReference* self) {
                                         self->setQuery(gtk_entry_get_text(GTK_ENTRY(self->entry.get())));
                                     }),
                                     this);
    this->keyHandlerId = g_signal_connect_swapped(
            entry, "key-press-event", G_CALLBACK(+[](ShortcutReference* self, GdkEventKey* event) -> gboolean {
                if (event->keyval == GDK_KEY_Escape) {
                    self->close();
                    return TRUE;
                }
                return FALSE;
            }),
            this);

    gtk_widget_show_all(box);
}

ShortcutReference::~ShortcutReference() {
    /*
     * A reference that is still up when its window goes is put away here, while its widgets are
     * alive, by hiding it rather than popping it down: a pop-down is a transition, and a destructor
     * must not leave an animation to finish on a window that is going away.
     */
    if (this->popover) {
        gtk_widget_set_visible(this->popover.get(), FALSE);
    }
}

void ShortcutReference::open() {
    if (!this->popover) {
        return;
    }
    const CommandRegistry commands = this->registryProvider();
    this->rows = xoj::command::buildShortcutReference(commands.metadata());
    gtk_entry_set_text(GTK_ENTRY(this->entry.get()), "");
    this->query.clear();
    this->rebuild();
    gtk_popover_popup(GTK_POPOVER(this->popover.get()));
    gtk_widget_grab_focus(this->entry.get());
}

void ShortcutReference::close() {
    if (!this->popover || !gtk_widget_is_visible(this->popover.get())) {
        return;
    }
    gtk_popover_popdown(GTK_POPOVER(this->popover.get()));
}

void ShortcutReference::toggle() {
    if (this->isOpen()) {
        this->close();
    } else {
        this->open();
    }
}

auto ShortcutReference::isOpen() const -> bool { return this->popover && gtk_widget_is_visible(this->popover.get()); }

auto ShortcutReference::matches(const ReferenceRow& row, const std::string& foldedQuery) -> bool {
    if (foldedQuery.empty()) {
        return true;
    }
    const std::string haystack = xoj::command::foldedForSearch(row.title + " " + row.category + " " + row.accelerator);
    return haystack.find(foldedQuery) != std::string::npos;
}

void ShortcutReference::setQuery(const std::string& newQuery) {
    this->query = newQuery;
    this->rebuild();
}

void ShortcutReference::rebuild() {
    GList* children = gtk_container_get_children(GTK_CONTAINER(this->list.get()));
    for (GList* child = children; child != nullptr; child = child->next) {
        gtk_widget_destroy(GTK_WIDGET(child->data));
    }
    g_list_free(children);

    const std::string foldedQuery = xoj::command::foldedForSearch(this->query);
    this->shown.clear();

    std::string currentCategory;
    for (const ReferenceRow& row: this->rows) {
        if (!matches(row, foldedQuery)) {
            continue;
        }
        if (this->shown.size() >= ORDER_LIMIT) {
            break;
        }
        /*
         * Grouped the way the menus group them, because that is how a user looks for a shortcut:
         * the headings are what the registry says the categories are, not a list of our own.
         */
        if (row.category != currentCategory) {
            currentCategory = row.category;
            addCategoryHeading(this->list.get(), row.category);
        }
        addCommandRow(this->list.get(), row);
        this->shown.emplace_back(row.id);
    }

    if (this->shown.empty()) {
        gtk_label_set_text(GTK_LABEL(this->status.get()),
                           this->rows.empty() ? _("This application has no commands") : _("No shortcut matches"));
    } else {
        const std::string text = std::to_string(this->shown.size()) + " " + _("commands");
        gtk_label_set_text(GTK_LABEL(this->status.get()), text.c_str());
    }
    gtk_widget_show_all(this->list.get());
}

auto ShortcutReference::shownIds() const -> std::vector<std::string> { return this->shown; }

auto ShortcutReference::coveredIds() const -> std::vector<std::string> {
    std::vector<std::string> ids;
    ids.reserve(this->rows.size());
    for (const ReferenceRow& row: this->rows) {
        ids.emplace_back(row.id);
    }
    return ids;
}

auto ShortcutReference::titleOf(const std::string& commandId) const -> std::string {
    for (const ReferenceRow& row: this->rows) {
        if (row.id == commandId) {
            return row.title;
        }
    }
    return {};
}

auto ShortcutReference::categoryOf(const std::string& commandId) const -> std::string {
    for (const ReferenceRow& row: this->rows) {
        if (row.id == commandId) {
            return row.category;
        }
    }
    return {};
}

auto ShortcutReference::acceleratorOf(const std::string& commandId) const -> std::string {
    for (const ReferenceRow& row: this->rows) {
        if (row.id == commandId) {
            return row.accelerator;
        }
    }
    return {};
}

auto ShortcutReference::isConflicted(const std::string& commandId) const -> bool {
    for (const ReferenceRow& row: this->rows) {
        if (row.id == commandId) {
            return row.conflicted;
        }
    }
    return false;
}

auto ShortcutReference::shownCount() const -> size_t { return this->shown.size(); }

auto ShortcutReference::getPopover() const -> GtkWidget* { return this->popover.get(); }

auto ShortcutReference::getEntry() const -> GtkWidget* { return this->entry.get(); }

auto ShortcutReference::getList() const -> GtkWidget* { return this->list.get(); }

void ShortcutReference::setAccessibleName(GtkWidget* widget, const char* name) {
    atk_object_set_name(gtk_widget_get_accessible(widget), name);
}

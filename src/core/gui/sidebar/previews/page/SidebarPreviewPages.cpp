#include "SidebarPreviewPages.h"

#include <algorithm>  // for max, find
#include <memory>     // for uniqu...
#include <optional>   // for optional
#include <utility>    // for pair, move

#include <atk/atk.h>      // for atk_object_set_name
#include <gdk/gdk.h>      // for GdkDevice, gdk_seat_get_pointer
#include <glib-object.h>  // for g_obj...
#include <gtk/gtk.h>      // for GTK_TOGGLE_BUTTON

#include "control/Control.h"                                    // for Control
#include "control/ScrollHandler.h"                              // for ScrollHandler
#include "control/settings/Settings.h"                          // for Settings
#include "gui/sidebar/previews/base/SidebarPreviewBaseEntry.h"  // for Sideb...
#include "model/Document.h"                                     // for Document
#include "model/PageRef.h"                                      // for PageRef
#include "model/PageSelectionModel.h"                           // for PageSelectionModel
#include "util/Assert.h"                                        // for xoj_assert
#include "util/Util.h"                                          // for npos
#include "util/gtk4_helper.h"
#include "util/i18n.h"        // for _
#include "util/safe_casts.h"  // for as_signed

#include "SidebarPreviewPageEntry.h"  // for Sideb...

constexpr auto MENU_ID = "PreviewPagesContextMenu";
constexpr auto TOOLBAR_ID = "PreviewPagesToolbar";

/// Thumbnails wrap into as many columns as the sidebar is wide.
constexpr int OVERVIEW_ZOOM_NUM = 15;  ///< percent
/// List rows keep the thumbnail small, so the metadata has the room.
constexpr int LIST_ZOOM_NUM = 9;  ///< percent

namespace {
constexpr auto OVERVIEW_BUTTON_ID = "btPagesOverview";
constexpr auto LIST_BUTTON_ID = "btPagesList";
constexpr auto DELETE_BUTTON_ID = "btPagesDelete";

/// How close to an edge of the scrolled area a drag starts scrolling it.
constexpr int AUTOSCROLL_MARGIN = 24;
constexpr double AUTOSCROLL_STEP = 24.;

auto toDensityMode(SidebarPageLayoutMode mode) -> SidebarPreviewPages::DensityMode {
    return mode == SidebarPageLayoutMode::LIST ? SidebarPreviewPages::DensityMode::LIST
                                               : SidebarPreviewPages::DensityMode::OVERVIEW;
}

auto toSettingsMode(SidebarPreviewPages::DensityMode mode) -> SidebarPageLayoutMode {
    return mode == SidebarPreviewPages::DensityMode::LIST ? SidebarPageLayoutMode::LIST
                                                          : SidebarPageLayoutMode::OVERVIEW;
}
}  // namespace

SidebarPreviewPages::SidebarPreviewPages(Control* control):
        SidebarPreviewBase(control, MENU_ID, TOOLBAR_ID),
        iconNameHelper(control->getSettings()),
        container(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), xoj::util::adopt) {
    this->overviewWidget = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(this->overviewWidget), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(this->overviewWidget), false);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(this->overviewWidget), 2);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(this->overviewWidget), 2);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(this->overviewWidget), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(this->overviewWidget), 64);
    gtk_widget_set_valign(this->overviewWidget, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(this->container.get()), this->overviewWidget);

    this->listWidget = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(this->listWidget), GTK_SELECTION_NONE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(this->listWidget), false);
    gtk_widget_set_valign(this->listWidget, GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(this->container.get()), this->listWidget);

    // The base class laid its previews out in a GtkFixed of its own; the page navigator owns its
    // layout now.
    setMiniaturesWidget(this->container.get());

    this->densityMode = toDensityMode(control->getSettings()->getSidebarPageLayoutMode());
    gtk_widget_set_visible(this->overviewWidget, this->densityMode == DensityMode::OVERVIEW);
    gtk_widget_set_visible(this->listWidget, this->densityMode == DensityMode::LIST);
    gtk_widget_show_all(this->container.get());
    gtk_widget_set_visible(this->overviewWidget, this->densityMode == DensityMode::OVERVIEW);
    gtk_widget_set_visible(this->listWidget, this->densityMode == DensityMode::LIST);

    connectToolbarControls();
}

SidebarPreviewPages::~SidebarPreviewPages() {
    // A card calls back into this class as it goes, so the entries are released while it is still
    // whole; the base class then finds nothing left to destroy.
    this->dropTarget = nullptr;
    this->previews.clear();
}

void SidebarPreviewPages::applyToolbarAccessibility(GtkWidget* root) {
    if (root == nullptr) {
        return;
    }

    if (GTK_IS_BUTTON(root)) {
        const char* tooltip = gtk_widget_get_tooltip_text(root);
        if (tooltip != nullptr) {
            atk_object_set_name(gtk_widget_get_accessible(root), tooltip);
            gtk_widget_set_has_tooltip(root, TRUE);
        }
    }

    if (GTK_IS_CONTAINER(root)) {
        for (GList* children = gtk_container_get_children(GTK_CONTAINER(root)); children != nullptr;
             children = children->next) {
            applyToolbarAccessibility(GTK_WIDGET(children->data));
        }
    }
}

void SidebarPreviewPages::connectToolbarControls() {
    this->overviewButton = this->builder->get(OVERVIEW_BUTTON_ID);
    this->listButton = this->builder->get(LIST_BUTTON_ID);
    this->deleteButton = this->builder->get(DELETE_BUTTON_ID);

    applyToolbarAccessibility(this->builder->get(TOOLBAR_ID));

    this->overviewHandler = g_signal_connect(this->overviewButton, "toggled",
                                             G_CALLBACK(+[](GtkToggleButton* button, gpointer data) {
                                                 auto* self = static_cast<SidebarPreviewPages*>(data);
                                                 if (gtk_toggle_button_get_active(button)) {
                                                     self->setDensityMode(DensityMode::OVERVIEW);
                                                 }
                                             }),
                                             this);
    this->listHandler = g_signal_connect(this->listButton, "toggled",
                                         G_CALLBACK(+[](GtkToggleButton* button, gpointer data) {
                                             auto* self = static_cast<SidebarPreviewPages*>(data);
                                             if (gtk_toggle_button_get_active(button)) {
                                                 self->setDensityMode(DensityMode::LIST);
                                             }
                                         }),
                                         this);

    // The two buttons behave as one choice: the one of the current mode is the one that is on.
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->overviewButton), this->densityMode == DensityMode::OVERVIEW);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->listButton), this->densityMode == DensityMode::LIST);

    updateActionLabels();
}

void SidebarPreviewPages::enableSidebar() {
    SidebarPreviewBase::enableSidebar();

    refreshSelection();
}

auto SidebarPreviewPages::getName() -> std::string { return _("Page Preview"); }

auto SidebarPreviewPages::getIconName() -> std::string { return this->iconNameHelper.iconName("sidebar-page-preview"); }

auto SidebarPreviewPages::getZoom() const -> double {
    return static_cast<double>(this->densityMode == DensityMode::LIST ? LIST_ZOOM_NUM : OVERVIEW_ZOOM_NUM) / 100.;
}

void SidebarPreviewPages::layout() {
    // The GtkFlowBox and the GtkListBox lay their children out; there is nothing to position by
    // hand, and nothing to do when the sidebar is resized.
}

void SidebarPreviewPages::updatePreviews() {
    // Every entry takes its own card out of the container as it goes, so the containers never keep
    // a card whose page is gone.
    this->previews.clear();

    Document* doc = this->getControl()->getDocument();
    doc->lock_shared();
    size_t len = doc->getPageCount();
    this->previews.reserve(len);
    for (size_t i = 0; i < len; i++) {
        this->previews.emplace_back(std::make_unique<SidebarPreviewPageEntry>(this, doc->getPage(i), i));
    }
    doc->unlock_shared();

    // A new document means a new selection.
    getSelectionModel().reset(len);

    rebuildContainer();
    updateIndices();
    refreshSelection();
    updateActionLabels();
}

void SidebarPreviewPages::rebuildContainer() {
    const bool list = this->densityMode == DensityMode::LIST;
    GtkWidget* target = list ? this->listWidget : this->overviewWidget;

    for (auto& p: this->previews) {
        GtkWidget* card = p->getWidget();
        SidebarPreviewBase::detachFromContainer(card);
        static_cast<SidebarPreviewPageEntry*>(p.get())->setListMode(list);

        if (list) {
            gtk_list_box_insert(GTK_LIST_BOX(target), card, -1);
        } else {
            gtk_flow_box_insert(GTK_FLOW_BOX(target), card, -1);
        }
    }

    gtk_widget_show_all(target);
    gtk_widget_set_visible(this->overviewWidget, !list);
    gtk_widget_set_visible(this->listWidget, list);
}

void SidebarPreviewPages::pageSizeChanged(size_t page) {
    if (page == npos || page >= this->previews.size()) {
        return;
    }
    SidebarPreviewPageEntry* entry = getEntry(page);
    if (entry == nullptr) {
        return;
    }
    entry->updateSize();
    entry->refresh();
    entry->repaint();
}

void SidebarPreviewPages::pageChanged(size_t page) {
    if (page == npos || page >= this->previews.size()) {
        return;
    }

    auto& p = this->previews[page];
    p->repaint();
}

void SidebarPreviewPages::pageDeleted(size_t page) {
    if (page >= previews.size()) {
        return;
    }

    // The entry takes its card out of the container as it goes.
    previews.erase(previews.begin() + as_signed(page));

    updateIndices();
    refreshSelection();
    updateActionLabels();
}

void SidebarPreviewPages::pageInserted(size_t page) {
    if (page > previews.size()) {
        return;
    }

    Document* doc = control->getDocument();
    doc->lock_shared();
    auto p = std::make_unique<SidebarPreviewPageEntry>(this, doc->getPage(page), page);
    doc->unlock_shared();

    p->setListMode(this->densityMode == DensityMode::LIST);

    GtkWidget* card = p->getWidget();
    if (this->densityMode == DensityMode::LIST) {
        gtk_list_box_insert(GTK_LIST_BOX(this->listWidget), card, as_signed(page));
    } else {
        gtk_flow_box_insert(GTK_FLOW_BOX(this->overviewWidget), card, as_signed(page));
    }
    gtk_widget_show_all(card);

    this->previews.insert(this->previews.begin() + as_signed(page), std::move(p));

    updateIndices();
    refreshSelection();
    updateActionLabels();
}

void SidebarPreviewPages::pagesReordered() {
    // The cards themselves are already in the right places: the reorder was reported as a delete
    // and an insert per moved page. What a delete/insert pair cannot say is which pages are still
    // selected, so the cards take that from the model again.
    refreshSelection();
    updateActionLabels();
}

void SidebarPreviewPages::forgetEntry(const SidebarPreviewPageEntry* entry) {
    // A card that is going away cannot be where a drag would land.
    if (this->dropTarget == entry) {
        this->dropTarget = nullptr;
        this->dropBefore = false;
    }
}

void SidebarPreviewPages::updateIndices() {
    size_t index = 0;
    for (auto& preview: this->previews) {
        dynamic_cast<SidebarPreviewPageEntry*>(preview.get())->setIndex(index++);
    }
}

void SidebarPreviewPages::refreshSelection() {
    const auto& selection = getSelectionModel();

    // The page the sidebar scrolls to when it is shown is the page the editor is on.
    this->selectedEntry = selection.getCurrentPage();

    for (auto& preview: this->previews) {
        auto* entry = dynamic_cast<SidebarPreviewPageEntry*>(preview.get());
        if (entry == nullptr) {
            continue;
        }
        entry->setSelected(selection.isSelected(entry->getIndex()));
        entry->setCurrentPage(selection.isCurrentPage(entry->getIndex()));
    }
}

void SidebarPreviewPages::pageSelected(size_t page) {
    refreshSelection();

    if (!this->enabled) {
        return;
    }
    if (page != npos && page < this->previews.size()) {
        scrollToPreview(this);
    }
}

auto SidebarPreviewPages::getSelectionModel() -> xoj::model::PageSelectionModel& {
    return this->control->getPageSelection();
}

auto SidebarPreviewPages::getEntry(size_t page) const -> SidebarPreviewPageEntry* {
    if (page >= this->previews.size()) {
        return nullptr;
    }
    return dynamic_cast<SidebarPreviewPageEntry*>(this->previews[page].get());
}

void SidebarPreviewPages::onCardClicked(SidebarPreviewPageEntry* entry, bool controlPressed, bool shiftPressed) {
    if (entry == nullptr || this->dragging) {
        return;
    }

    const size_t page = entry->getIndex();
    this->control->pageSelectionClicked(page, controlPressed, shiftPressed);

    // Plain navigation, unless the selection click already went there.
    if (this->control->getCurrentPageNo() != page) {
        this->control->getScrollHandler()->jumpToPage(entry->getPage());
    }
    this->control->firePageSelected(page);

    refreshSelection();
    updateActionLabels();
}

void SidebarPreviewPages::onCardContextMenu(SidebarPreviewPageEntry* entry) {
    if (entry == nullptr) {
        return;
    }
    // Right-clicking a page that is not selected makes it the selection, so the menu acts on
    // something the user can see. Right-clicking inside a selection leaves it alone.
    if (!getSelectionModel().isSelected(entry->getIndex())) {
        this->control->pageSelectionClicked(entry->getIndex(), false, false);
        refreshSelection();
    }
}

void SidebarPreviewPages::updateActionLabels() {
    if (this->deleteButton == nullptr) {
        return;
    }

    const size_t count = getSelectionModel().count();
    if (count > 1) {
        // Plan 005, step 4: a destructive multi page action says how many pages it would take.
        // It is one undoable operation, so Undo puts them all back.
        setActionLabel(this->deleteButton, FS(_F("Delete the {1} selected pages") % count));
    } else {
        setActionLabel(this->deleteButton, _("Delete this page"));
    }
}

void SidebarPreviewPages::setActionLabel(GtkWidget* button, const std::string& label) {
    gtk_widget_set_tooltip_text(button, label.c_str());
    atk_object_set_name(gtk_widget_get_accessible(button), label.c_str());
}

void SidebarPreviewPages::setDensityMode(DensityMode mode) {
    if (this->densityMode == mode) {
        return;
    }
    this->densityMode = mode;

    // The buttons mirror the mode; blocking their handlers keeps that from re-entering here.
    if (this->overviewButton != nullptr) {
        g_signal_handler_block(this->overviewButton, this->overviewHandler);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->overviewButton), mode == DensityMode::OVERVIEW);
        g_signal_handler_unblock(this->overviewButton, this->overviewHandler);
    }
    if (this->listButton != nullptr) {
        g_signal_handler_block(this->listButton, this->listHandler);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(this->listButton), mode == DensityMode::LIST);
        g_signal_handler_unblock(this->listButton, this->listHandler);
    }

    rebuildContainer();

    // The thumbnail is a different size in the two modes. Moving a card into the other container
    // told it which mode it is in, which threw its thumbnail away; measuring it again and asking
    // for a repaint is what puts a thumbnail of the right size back.
    for (auto& p: this->previews) {
        auto* entry = dynamic_cast<SidebarPreviewPageEntry*>(p.get());
        if (entry == nullptr) {
            continue;
        }
        entry->updateSize();
        entry->refresh();
        entry->repaint();
    }

    // A UI preference, never document data.
    this->control->getSettings()->setSidebarPageLayoutMode(toSettingsMode(mode));
}

auto SidebarPreviewPages::getDensityMode() const -> DensityMode { return this->densityMode; }

auto SidebarPreviewPages::getDropDestination(const SidebarPreviewPageEntry* entry, bool before) const -> size_t {
    if (entry == nullptr) {
        return 0;
    }
    const size_t index = entry->getIndex();
    return before ? index : index + 1;
}

void SidebarPreviewPages::moveSelectedPages(size_t destination) { this->control->moveSelectedPages(destination); }

void SidebarPreviewPages::beginPageDrag(SidebarPreviewPageEntry* entry) {
    this->dragging = true;
    this->dropTarget = nullptr;

    // A drag that starts on a page outside the selection takes that page with it, so a user can
    // always drag the page they grabbed.
    if (entry != nullptr && !getSelectionModel().isSelected(entry->getIndex())) {
        this->control->pageSelectionClicked(entry->getIndex(), false, false);
        refreshSelection();
    }
}

void SidebarPreviewPages::updateDropIndicator(SidebarPreviewPageEntry* entry, bool before) {
    if (entry == nullptr) {
        return;
    }
    if (this->dropTarget == entry && this->dropBefore == before) {
        return;
    }
    if (this->dropTarget != nullptr && this->dropTarget != entry) {
        this->dropTarget->setDropIndicator(std::nullopt);
    }
    this->dropTarget = entry;
    this->dropBefore = before;
    entry->setDropIndicator(before);
}

void SidebarPreviewPages::clearDropIndicator() {
    if (this->dropTarget != nullptr) {
        this->dropTarget->setDropIndicator(std::nullopt);
    }
    this->dropTarget = nullptr;
    this->dragging = false;
}

void SidebarPreviewPages::dropPages(SidebarPreviewPageEntry* entry, bool before) {
    const size_t destination = getDropDestination(entry, before);
    clearDropIndicator();
    moveSelectedPages(destination);
}

auto SidebarPreviewPages::isDragging() const -> bool { return this->dragging; }

void SidebarPreviewPages::autoScrollDuringDrag() {
    GtkWidget* scrollable = getScrollableWidget();
    GdkWindow* window = gtk_widget_get_window(scrollable);
    if (window == nullptr) {
        return;
    }

    GdkDisplay* display = gtk_widget_get_display(scrollable);
    GdkSeat* seat = gdk_display_get_default_seat(display);
    GdkDevice* device = gdk_seat_get_pointer(seat);

    int x = 0;
    int y = 0;
    gdk_window_get_device_position(window, device, &x, &y, nullptr);

    const int height = gtk_widget_get_allocated_height(scrollable);
    if (height <= 0) {
        return;
    }

    GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrollable));
    if (y < AUTOSCROLL_MARGIN) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_value(vadj) - AUTOSCROLL_STEP);
    } else if (y > height - AUTOSCROLL_MARGIN) {
        gtk_adjustment_set_value(vadj, gtk_adjustment_get_value(vadj) + AUTOSCROLL_STEP);
    }
}

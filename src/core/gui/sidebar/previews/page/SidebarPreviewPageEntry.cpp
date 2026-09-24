#include "SidebarPreviewPageEntry.h"

#include <string>  // for string

#include <atk/atk.h>      // for atk_object_set_name
#include <gdk/gdk.h>      // for GdkEventButton, GDK_CONTROL_MASK, ...
#include <glib-object.h>  // for G_CALLBACK, g_object_set_data
#include <gtk/gtk.h>      // for gtk_widget_add_css_class, ...

#include "control/Control.h"                                // for Control
#include "control/ScrollHandler.h"                          // for ScrollHan...
#include "control/settings/Settings.h"                      // for Settings
#include "gui/PagePreviewDecoration.h"                      // for Drawing  ...
#include "gui/sidebar/previews/page/SidebarPreviewPages.h"  // for SidebarPr...
#include "model/XojPage.h"                                  // for XojPage
#include "util/gtk4_helper.h"
#include "util/i18n.h"  // for _, _F, FS

#include "PageCardLabels.h"  // for buildAccessibleName, buildMetadata

namespace {
constexpr auto CSS_CARD = "xoj-page-card";
constexpr auto CSS_SELECTED = "xoj-page-card-selected";
constexpr auto CSS_CURRENT = "xoj-page-card-current";
constexpr auto CSS_LOADING = "xoj-page-card-loading";
constexpr auto CSS_ERROR = "xoj-page-card-error";
constexpr auto CSS_LIST = "xoj-page-card-list";
constexpr auto CSS_OVERVIEW = "xoj-page-card-overview";
constexpr auto CSS_DROP_BEFORE = "xoj-page-card-drop-before";
constexpr auto CSS_DROP_AFTER = "xoj-page-card-drop-after";
constexpr auto CSS_MARKER = "xoj-page-selection-marker";
constexpr auto CSS_METADATA = "xoj-page-card-metadata";

/// Where the caption's text sits inside its label. Overview mode shows the caption under the card,
/// centred like the checkmark, the thumbnail and the drawn page number; list mode puts it beside a
/// thumbnail, where a line of text is read from the left.
constexpr double CAPTION_XALIGN_OVERVIEW = 0.5;
constexpr double CAPTION_XALIGN_LIST = 0.;

/// The selection marker. A glyph, not a colour: it stays readable in every theme and to a user who
/// cannot tell the selected border from the unselected one.
constexpr auto SELECTION_GLYPH = "\u2713";

#if GTK_MAJOR_VERSION == 3
/// The page order of this sidebar, dragged from one card to another. Private to the application:
/// the drop reads the selection from the model, so the payload only has to make the drag valid.
constexpr auto PAGE_DRAG_TARGET = "application/x-xournalpp-pages";

GtkTargetEntry pageDragTargets[] = {{const_cast<gchar*>(PAGE_DRAG_TARGET), 0, 0}};
#endif
}  // namespace

SidebarPreviewPageEntry::SidebarPreviewPageEntry(SidebarPreviewPages* sidebar, const PageRef& page, size_t index):
        SidebarPreviewBaseEntry(sidebar, page), sidebar(sidebar), index(index) {
    this->card.reset(gtk_box_new(GTK_ORIENTATION_VERTICAL, 2), xoj::util::adopt);
    gtk_widget_add_css_class(this->card.get(), CSS_CARD);

    this->marker.reset(gtk_label_new(nullptr), xoj::util::adopt);
    gtk_widget_add_css_class(this->marker.get(), CSS_MARKER);
    gtk_widget_set_halign(this->marker.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_can_focus(this->marker.get(), false);

    this->metadata = gtk_label_new(nullptr);
    gtk_widget_add_css_class(this->metadata, CSS_METADATA);
    // Overview is the density a card is born in; setListMode() moves the text to the left edge for
    // the rows of the other one.
    gtk_label_set_xalign(GTK_LABEL(this->metadata), CAPTION_XALIGN_OVERVIEW);
    // Centring the text never lets it overflow: a caption too long for the card is cut at its end.
    gtk_label_set_ellipsize(GTK_LABEL(this->metadata), PANGO_ELLIPSIZE_END);
    gtk_widget_set_can_focus(this->metadata, false);
    gtk_widget_set_visible(this->metadata, false);

    gtk_box_append(GTK_BOX(this->card.get()), this->marker.get());
    gtk_box_append(GTK_BOX(this->card.get()), this->button.get());
    gtk_box_append(GTK_BOX(this->card.get()), this->metadata);

    // Plan 005, step 3: the thumbnail is the control the user focuses and activates, so it is the
    // one that carries the accessible name. The card is a label for the same thing.
    gtk_widget_set_focus_on_click(this->button.get(), true);
    gtk_widget_add_css_class(this->button.get(), "xoj-focus-ring");

    // Remember the modifiers of the press that is about to become a `clicked` signal, so a
    // Ctrl/Shift click reaches the selection model. A keyboard activation has no press and keeps
    // both false.
    g_signal_connect(this->button.get(), "button-press-event",
                     G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, gpointer data) -> gboolean {
                         auto* self = static_cast<SidebarPreviewPageEntry*>(data);
                         if (event->button == 1) {
                             self->pressControl = (event->state & GDK_CONTROL_MASK) != 0;
                             self->pressShift = (event->state & GDK_SHIFT_MASK) != 0;
                         }
                         return false;
                     }),
                     this);

    setListMode(sidebar->getDensityMode() == SidebarPreviewPages::DensityMode::LIST);

#if GTK_MAJOR_VERSION == 3
    // Plan 005, step 5: the thumbnail is the drag handle and the card is the drop target, so a
    // drag can start anywhere on the card and end anywhere on another one.
    gtk_drag_source_set(this->button.get(), GDK_BUTTON1_MASK, pageDragTargets, G_N_ELEMENTS(pageDragTargets),
                        GDK_ACTION_MOVE);
    g_signal_connect(this->button.get(), "drag-begin",
                     G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer data) {
                         auto* self = static_cast<SidebarPreviewPageEntry*>(data);
                         self->sidebar->beginPageDrag(self);
                     }),
                     this);
    g_signal_connect(this->button.get(), "drag-end",
                     G_CALLBACK(+[](GtkWidget*, GdkDragContext*, gpointer data) {
                         auto* self = static_cast<SidebarPreviewPageEntry*>(data);
                         self->sidebar->clearDropIndicator();
                     }),
                     this);
    g_signal_connect(this->button.get(), "drag-data-get",
                     G_CALLBACK(+[](GtkWidget*, GdkDragContext*, GtkSelectionData* data, guint, guint time,
                                    gpointer self) {
                         const std::string payload = std::to_string(static_cast<SidebarPreviewPageEntry*>(self)->index);
                         gtk_selection_data_set_text(data, payload.c_str(), -1);
                         (void)time;
                     }),
                     this);

    gtk_drag_dest_set(this->card.get(), GTK_DEST_DEFAULT_MOTION, pageDragTargets, G_N_ELEMENTS(pageDragTargets),
                      GDK_ACTION_MOVE);
    g_signal_connect(this->card.get(), "drag-motion",
                     G_CALLBACK(+[](GtkWidget*, GdkDragContext* context, gint x, gint y, guint time,
                                    gpointer data) -> gboolean {
                         auto* self = static_cast<SidebarPreviewPageEntry*>(data);
                         self->sidebar->updateDropIndicator(self, self->isDropBefore(x, y));
                         self->sidebar->autoScrollDuringDrag();
                         gdk_drag_status(context, GDK_ACTION_MOVE, time);
                         return true;
                     }),
                     this);
    g_signal_connect(this->card.get(), "drag-leave",
                     G_CALLBACK(+[](GtkWidget*, GdkDragContext*, guint, gpointer data) {
                         auto* self = static_cast<SidebarPreviewPageEntry*>(data);
                         self->sidebar->clearDropIndicator();
                     }),
                     this);
    g_signal_connect(this->card.get(), "drag-drop",
                     G_CALLBACK(+[](GtkWidget*, GdkDragContext* context, gint x, gint y, guint time,
                                    gpointer data) -> gboolean {
                         auto* self = static_cast<SidebarPreviewPageEntry*>(data);
                         const bool before = self->isDropBefore(x, y);
                         self->sidebar->dropPages(self, before);
                         gtk_drag_finish(context, true, false, time);
                         return true;
                     }),
                     this);
#endif

    updateCssClasses();
    updateMetadata();
}

SidebarPreviewPageEntry::~SidebarPreviewPageEntry() {
    // A container holds a reference to its children, so a card that is only unref'd would stay in
    // the sidebar with nothing behind it. Taking it out here covers every way an entry can go -
    // a deleted page, a document change, the sidebar itself being destroyed.
    this->sidebar->forgetEntry(this);
    SidebarPreviewBase::detachFromContainer(this->card.get());
}

auto SidebarPreviewPageEntry::getWidget() const -> GtkWidget* { return this->card.get(); }

auto SidebarPreviewPageEntry::getRenderType() const -> PreviewRenderType { return RENDER_TYPE_PAGE_PREVIEW; }

void SidebarPreviewPageEntry::mouseButtonPressCallback() {
    bool control = this->pressControl;
    bool shift = this->pressShift;
    this->pressControl = false;
    this->pressShift = false;

    sidebar->onCardClicked(this, control, shift);
}

void SidebarPreviewPageEntry::paint(cairo_t* cr) {
    SidebarPreviewBaseEntry::paint(cr);

    // In list mode the page number is written in the metadata label instead, next to a thumbnail
    // that is too small for a drawn number to stay readable.
    if (this->listMode) {
        return;
    }
    if (sidebar->getControl()->getSettings()->getSidebarNumberingStyle() == SidebarNumberingStyle::NONE) {
        return;
    }
    PagePreviewDecoration::drawDecoration(cr, this, this->sidebar->getControl());
}

auto SidebarPreviewPageEntry::getHeight() const -> int {
    if (this->listMode) {
        return imageHeight;
    }
    if (sidebar->getControl()->getSettings()->getSidebarNumberingStyle() ==
        SidebarNumberingStyle::NUMBER_BELOW_PREVIEW) {
        return imageHeight + PagePreviewDecoration::MARGIN_BOTTOM;
    }
    return imageHeight;
}

void SidebarPreviewPageEntry::setIndex(size_t index) {
    if (this->index == index) {
        return;
    }
    this->index = index;
    refresh();
}

auto SidebarPreviewPageEntry::getIndex() const -> size_t { return this->index; }

auto SidebarPreviewPageEntry::getPageNumber() const -> size_t { return this->index + 1; }

auto SidebarPreviewPageEntry::isSelected() const -> bool { return this->selected; }

auto SidebarPreviewPageEntry::isCurrentPage() const -> bool { return this->current; }

auto SidebarPreviewPageEntry::getZoom() const -> double { return this->sidebar->getZoom(); }

auto SidebarPreviewPageEntry::getPage() const -> const PageRef& { return this->page; }

void SidebarPreviewPageEntry::setSelected(bool selected) {
    if (this->selected == selected) {
        return;
    }
    SidebarPreviewBaseEntry::setSelected(selected);
    refresh();
}

void SidebarPreviewPageEntry::setCurrentPage(bool current) {
    if (this->current == current) {
        return;
    }
    this->current = current;
    refresh();
}

void SidebarPreviewPageEntry::setListMode(bool listMode) {
    if (this->listMode == listMode) {
        return;
    }
    this->listMode = listMode;

    gtk_orientable_set_orientation(GTK_ORIENTABLE(this->card.get()),
                                   listMode ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL);
    gtk_widget_set_visible(this->metadata, listMode);
    // The caption follows the shape of the card: centred under a thumbnail, read from the left of
    // the label when it sits beside one.
    gtk_label_set_xalign(GTK_LABEL(this->metadata), listMode ? CAPTION_XALIGN_LIST : CAPTION_XALIGN_OVERVIEW);
    gtk_widget_set_valign(this->metadata, listMode ? GTK_ALIGN_CENTER : GTK_ALIGN_FILL);

    // The thumbnail was rendered at the other mode's zoom, so it is not the size this mode draws
    // it at: it is thrown away and rendered again rather than stretched.
    invalidateThumbnail();

    refresh();
}

auto SidebarPreviewPageEntry::isListMode() const -> bool { return this->listMode; }

void SidebarPreviewPageEntry::setDropIndicator(std::optional<bool> before) {
    if (this->dropBefore == before) {
        return;
    }
    this->dropBefore = before;
    updateCssClasses();
}

void SidebarPreviewPageEntry::thumbnailStateChanged() { refresh(); }

void SidebarPreviewPageEntry::refresh() {
    updateCssClasses();
    updateMetadata();

    const int extra = listMode ? 0 : (getHeight() - imageHeight);
    gtk_widget_set_size_request(this->button.get(), imageWidth, imageHeight + extra);

    atk_object_set_name(gtk_widget_get_accessible(this->card.get()), buildAccessibleName().c_str());
    atk_object_set_name(gtk_widget_get_accessible(this->button.get()), buildAccessibleName().c_str());

    gtk_widget_queue_resize(this->card.get());
}

void SidebarPreviewPageEntry::updateCssClasses() {
    const auto set = [this](const char* cssClass, bool on) {
        if (on) {
            gtk_widget_add_css_class(this->card.get(), cssClass);
        } else {
            gtk_widget_remove_css_class(this->card.get(), cssClass);
        }
    };

    set(CSS_SELECTED, this->selected);
    set(CSS_CURRENT, this->current);
    set(CSS_LIST, this->listMode);
    set(CSS_OVERVIEW, !this->listMode);
    set(CSS_LOADING, isLoading());
    set(CSS_ERROR, hasRenderError());
    set(CSS_DROP_BEFORE, this->dropBefore.has_value() && *this->dropBefore);
    set(CSS_DROP_AFTER, this->dropBefore.has_value() && !*this->dropBefore);

    gtk_label_set_text(GTK_LABEL(this->marker.get()), this->selected ? SELECTION_GLYPH : "");
}

void SidebarPreviewPageEntry::updateMetadata() {
    const std::string pageName = this->page ? this->page->getBackgroundName() : std::string();
    const std::string text = xoj::pagecard::buildMetadata(getPageNumber(), pageName, hasRenderError());

    gtk_label_set_text(GTK_LABEL(this->metadata), text.c_str());
    gtk_widget_set_tooltip_text(this->card.get(), text.c_str());
}

auto SidebarPreviewPageEntry::buildAccessibleName() const -> std::string {
    return xoj::pagecard::buildAccessibleName(getPageNumber(), this->selected, this->current, isLoading(),
                                              hasRenderError());
}

auto SidebarPreviewPageEntry::isDropBefore(int x, int y) const -> bool {
    GtkAllocation allocation;
    gtk_widget_get_allocation(this->card.get(), &allocation);

    // List rows are read top to bottom, wrapped thumbnails left to right.
    if (this->listMode) {
        return allocation.height <= 0 || y < allocation.height / 2;
    }
    return allocation.width <= 0 || x < allocation.width / 2;
}

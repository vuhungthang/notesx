#include "SidebarPreviewBase.h"

#include <cstdlib>  // for abs, size_t

#include <glib-object.h>  // for g_object_ref, G_CALLBACK, g_sig...
#include <glib.h>         // for g_idle_add

#include "control/Control.h"   // for Control
#include "control/PdfCache.h"  // for PdfCache
#include "gui/Builder.h"       // for Builder
#include "gui/MainWindow.h"    // for MainWindow
#include "model/Document.h"    // for Document
#include "util/Util.h"         // for npos
#include "util/glib_casts.h"   // for wrap_for_once_v
#include "util/gtk4_helper.h"

#include "SidebarLayout.h"            // for SidebarLayout
#include "SidebarPreviewBaseEntry.h"  // for SidebarPreviewBaseEntry

constexpr auto XML_FILE = "sidebar.ui";

SidebarPreviewBase::SidebarPreviewBase(Control* control, const char* menuId, const char* toolbarId):
        AbstractSidebarPage(control),
        scrollableBox(gtk_scrolled_window_new(), xoj::util::adopt),
        mainBox(gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), xoj::util::adopt),
        miniaturesContainer(GTK_FIXED(gtk_fixed_new()), xoj::util::adopt) {
    gtk_box_append(GTK_BOX(mainBox.get()), scrollableBox.get());
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrollableBox.get()), GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrollableBox.get()), GTK_WIDGET(miniaturesContainer.get()));
    gtk_widget_set_vexpand(scrollableBox.get(), true);

    Document* doc = this->control->getDocument();
    doc->lock_shared();
    if (doc->getPdfPageCount() != 0) {
        this->cache = std::make_unique<PdfCache>(doc->getPdfDocument(), control->getSettings());
    }
    doc->unlock_shared();


    registerListener(this->control);
    this->control->addChangedDocumentListener(this);

    auto* adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrollableBox.get()));
    g_signal_connect(
            adj, "notify::page-size", G_CALLBACK(+[](GObject* adj, GParamSpec*, gpointer d) {
                static_cast<SidebarPreviewBase*>(d)->newWidth(gtk_adjustment_get_page_size(GTK_ADJUSTMENT(adj)));
            }),
            this);

    this->builder = std::make_unique<Builder>(control->getGladeSearchPath(), XML_FILE);
    GMenuModel* menu = G_MENU_MODEL(this->builder->get<GObject>(menuId));
    contextMenu.reset(GTK_MENU(gtk_menu_new_from_model(menu)), xoj::util::adopt);
    gtk_menu_attach_to_widget(contextMenu.get(), mainBox.get(), nullptr);

    gtk_box_append(GTK_BOX(mainBox.get()), this->builder->get(toolbarId));

    gtk_widget_show_all(mainBox.get());
}

SidebarPreviewBase::~SidebarPreviewBase() {
    // The scroll below is queued as a one-shot idle source holding this sidebar, and nothing else
    // takes it out of the main context: left pending, the main loop calls it once this object is
    // gone.
    if (this->previewScrollId != 0) {
        g_source_remove(this->previewScrollId);
    }

    this->control->removeChangedDocumentListener(this);
}

void SidebarPreviewBase::enableSidebar() {
    if (!this->enabled) {
        enabled = true;
        layout();
    }
}

void SidebarPreviewBase::disableSidebar() { enabled = false; }

void SidebarPreviewBase::newWidth(double width) {
    static constexpr double TRIGGER = 20.;

    if (std::abs(lastWidth - width) > TRIGGER) {
        this->layout();
        lastWidth = width;
    }
}

auto SidebarPreviewBase::getZoom() const -> double { return this->zoom; }

auto SidebarPreviewBase::getCache() -> PdfCache* { return this->cache.get(); }

void SidebarPreviewBase::layout() {
    if (enabled) {
        SidebarLayout::layout(this);
    }
}

auto SidebarPreviewBase::hasData() -> bool { return true; }

auto SidebarPreviewBase::getWidget() -> GtkWidget* { return this->mainBox.get(); }

void SidebarPreviewBase::setMiniaturesWidget(GtkWidget* widget) {
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(this->scrollableBox.get()), widget);
}

void SidebarPreviewBase::detachFromContainer(GtkWidget* widget) {
    if (widget == nullptr) {
        return;
    }
    GtkWidget* parent = gtk_widget_get_parent(widget);
    if (parent == nullptr) {
        return;
    }

    // A GtkFlowBox and a GtkListBox own their children through a wrapper; the widget has to be
    // taken out of the wrapper, not out of the box. Removing the wrapper while the widget is still
    // in it destroys the wrapper, and a destroyed wrapper takes its child - and the whole subtree
    // of that child, thumbnail and metadata included - down with it.
    if (GTK_IS_FLOW_BOX_CHILD(parent) || GTK_IS_LIST_BOX_ROW(parent)) {
        GtkWidget* box = gtk_widget_get_parent(parent);
        // Out of the wrapper first: the widget and everything it holds stay alive.
        gtk_container_remove(GTK_CONTAINER(parent), widget);
        if (box != nullptr) {
            gtk_container_remove(GTK_CONTAINER(box), parent);
        }
        return;
    }

    gtk_container_remove(GTK_CONTAINER(parent), widget);
}

void SidebarPreviewBase::documentChanged(DocumentChangeType type) {
    if (type == DOCUMENT_CHANGE_COMPLETE || type == DOCUMENT_CHANGE_CLEARED) {
        this->cache.reset();

        Document* doc = control->getDocument();
        doc->lock_shared();
        if (doc->getPdfPageCount() != 0) {
            this->cache = std::make_unique<PdfCache>(doc->getPdfDocument(), control->getSettings());
        }
        doc->unlock_shared();
        updatePreviews();
    }
}

auto SidebarPreviewBase::scrollToPreview(SidebarPreviewBase* sidebar) -> bool {
    if (!sidebar->enabled) {
        return false;
    }

    MainWindow* win = sidebar->control->getWindow();
    if (win == nullptr) {
        return false;
    }

    GtkWidget* w = win->get("sidebar");
    if (!gtk_widget_get_visible(w)) {
        return false;
    }

    if (sidebar->selectedEntry != npos && sidebar->selectedEntry < sidebar->previews.size()) {
        auto& p = sidebar->previews[sidebar->selectedEntry];

        // scroll to preview
        GtkAdjustment* vadj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(sidebar->scrollableBox.get()));
        GtkWidget* widget = p->getWidget();

        GtkAllocation allocation;
        gtk_widget_get_allocation(widget, &allocation);
        int x = allocation.x;
        int y = allocation.y;

        if (x == -1) {
            // Only one source at a time: it re-queues itself until the preview is laid out, and a
            // second one would leave an id nobody can take out of the main context again.
            if (sidebar->previewScrollId == 0) {
                sidebar->previewScrollId = g_idle_add(xoj::util::wrap_for_once_v<scrollToPreviewFromIdle>, sidebar);
            }
            return false;
        }

        gtk_adjustment_clamp_page(vadj, y, y + allocation.height);
    }
    return false;
}

void SidebarPreviewBase::pageDeleted(size_t page) {}

void SidebarPreviewBase::pageInserted(size_t page) {}

void SidebarPreviewBase::openPreviewContextMenu(GdkEvent* currentEvent) {
    gtk_menu_popup_at_pointer(contextMenu.get(), currentEvent);
}

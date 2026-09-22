/*
 * Xournal++
 *
 * Base class for prviews in the sidebar
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>  // for size_t
#include <memory>   // for unique_ptr
#include <vector>   // for vector

#include <gtk/gtk.h>  // for GtkWidget, GtkAllocation

#include "gui/Builder.h"                      // for Builder
#include "gui/sidebar/AbstractSidebarPage.h"  // for AbstractSidebarPage
#include "model/DocumentChangeType.h"         // for DocumentChangeType
#include "util/Util.h"
#include "util/raii/GObjectSPtr.h"

class PdfCache;
class SidebarLayout;
class SidebarPreviewBaseEntry;
class Control;

class SidebarPreviewBase: public AbstractSidebarPage {
public:
    SidebarPreviewBase(Control* control, const char* menuId, const char* toolbarId);
    ~SidebarPreviewBase() override;

public:
    void enableSidebar() override;
    void disableSidebar() override;

    /**
     * Layout the pages to the current size of the sidebar
     */
    void layout() override;

    /**
     * Update the preview images
     */
    virtual void updatePreviews() = 0;

    /**
     * @overwrite
     */
    bool hasData() override;

    /**
     * @overwrite
     */
    GtkWidget* getWidget() override;

    /**
     * Gets the zoom factor for the previews
     */
    virtual double getZoom() const;

    /**
     * Gets the PDF cache for preview rendering
     */
    PdfCache* getCache();

public:
    // DocumentListener interface (only the part handled by SidebarPreviewBase)
    void documentChanged(DocumentChangeType type) override;
    void pageInserted(size_t page) override;
    void pageDeleted(size_t page) override;

protected:
    /**
     * Timeout callback to scroll to a page
     */
    static bool scrollToPreview(SidebarPreviewBase* sidebar);

    /// The width of the sidebar has changed
    void newWidth(double width);

    /**
     * Hand the scrolled area a container of its own.
     *
     * The base class builds a `GtkFixed` for the sidebars that lay their previews out by hand; a
     * subclass that uses a container with its own layout (the page navigator uses a `GtkFlowBox`
     * and a `GtkListBox`) calls this from its constructor to take over the ownership of the
     * miniatures. The previous container is left to its own reference count.
     */
    void setMiniaturesWidget(GtkWidget* widget);

public:
    /// Detach `widget` from whatever container owns it, leaving the widget itself alive.
    static void detachFromContainer(GtkWidget* widget);

    /// The scrolled area the previews live in. The page navigator auto-scrolls it while dragging.
    auto getScrollableWidget() const -> GtkWidget* { return this->scrollableBox.get(); }

public:
    /**
     * Opens a context menu, at the current cursor position.
     */
    void openPreviewContextMenu(GdkEvent* currentEvent);

protected:
    /**
     * The ui file the sidebar's context menu and toolbar come from.
     *
     * Derived classes add their own controls to that toolbar and look them up here by id.
     */
    std::unique_ptr<Builder> builder;

private:
    /**
     * The Zoom of the previews
     */
    double zoom = 0.15;

    /// last recorded width of the sidebar
    double lastWidth = -1;

    /**
     * For preview rendering
     */
    std::unique_ptr<PdfCache> cache;

protected:
    /// The scrollable area with the miniatures
    xoj::util::WidgetSPtr scrollableBox;

    /// Main box, containing the scrollable area and the toolbar.
    xoj::util::WidgetSPtr mainBox;

    /// The widget within the scrollable area with the page miniatures
    xoj::util::GObjectSPtr<GtkFixed> miniaturesContainer;

    /**
     * The context menu to display when a miniature is right-clicked.
     * This must be populated by the derived classes constructors.
     * It must be a GtkPopover parented (gtk_widget_set_parent()) by this->miniaturesContainer
     */
    xoj::util::GObjectSPtr<GtkMenu> contextMenu;

    /**
     * The currently selected entry in the sidebar, starting from 0
     * -1 means no valid selection
     */
    size_t selectedEntry = npos;

    /**
     * The previews
     */
    std::vector<std::unique_ptr<SidebarPreviewBaseEntry>> previews;

    /**
     * The sidebar is enabled
     */
    bool enabled = false;

    friend class SidebarLayout;
};

/*
 * Xournal++
 *
 * A preview entry in a sidebar
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <mutex>  // for mutex

#include <cairo.h>    // for cairo_t, cairo_surface_t
#include <glib.h>     // for gboolean
#include <gtk/gtk.h>  // for GtkWidget

#include "model/PageRef.h"  // for PageRef
#include "util/raii/CairoWrappers.h"
#include "util/raii/GObjectSPtr.h"

class SidebarPreviewBase;

typedef enum {
    /**
     * Render the whole page
     */
    RENDER_TYPE_PAGE_PREVIEW = 1,

    /**
     * Render only a layer
     */
    RENDER_TYPE_PAGE_LAYER,

    /**
     * Render the stack up to a layer
     */
    RENDER_TYPE_PAGE_LAYERSTACK
} PreviewRenderType;


class SidebarPreviewBaseEntry {
public:
    SidebarPreviewBaseEntry(SidebarPreviewBase* sidebar, const PageRef& page);
    virtual ~SidebarPreviewBaseEntry();

public:
    virtual GtkWidget* getWidget() const;
    virtual int getWidth() const;
    virtual int getHeight() const;

    virtual void setSelected(bool selected);

    virtual void repaint();
    virtual void updateSize();

    /**
     * Throw the rendered thumbnail away, so the next paint draws the placeholder and schedules the
     * thumbnail to be rendered again.
     *
     * The buffer is rendered at the zoom the sidebar had when the job ran, so anything that
     * changes that zoom - the page navigator's density modes do - has to invalidate it, or the
     * card would show a thumbnail of the wrong size.
     */
    void invalidateThumbnail();

    /**
     * @return What should be rendered
     */
    virtual PreviewRenderType getRenderType() const = 0;

    /**
     * The thumbnail of this entry, or nullptr if it is gone.
     *
     * The widget outlives the entry: a rendering job holds a reference to the widget while it
     * runs, so the job has to ask whether the entry is still there before it touches it.
     */
    static SidebarPreviewBaseEntry* fromWidget(GtkWidget* widget);

    /// A thumbnail was rendered: the entry is no longer loading and is not in an error state.
    void thumbnailReady();
    /// The thumbnail could not be rendered.
    void markRenderError();
    /// The thumbnail is still being rendered.
    bool isLoading() const;
    /// The thumbnail could not be rendered.
    bool hasRenderError() const;

private:
    static gboolean drawCallback(GtkWidget* widget, cairo_t* cr, SidebarPreviewBaseEntry* preview);

protected:
    virtual void mouseButtonPressCallback() = 0;

    virtual void drawLoadingPage();
    virtual void paint(cairo_t* cr);

    /**
     * What is drawn in place of a thumbnail: "Loading..." normally, and the reason when the
     * render failed.
     */
    virtual const char* getPlaceholderText() const;

    /// Called when the thumbnail state changed, so a card can refresh what it shows.
    virtual void thumbnailStateChanged();

protected:
    /**
     * If this page is currently selected
     */
    bool selected = false;

    /// Thumbnail state, only ever touched from the UI thread.
    bool loading = true;
    bool renderError = false;

    int imageWidth;
    int imageHeight;
    int DPIscaling;  ///< 1, maybe 2 in HiDPI setups

    /**
     * The sidebar which displays the previews
     */
    SidebarPreviewBase* sidebar;

    /**
     * The page which is representated
     */
    PageRef page;

    /// Mutex protecting the buffer
    std::mutex drawingMutex{};

    /// Buffer because of performance reasons
    xoj::util::CairoSurfaceSPtr buffer;

    /// The main widget, containing the miniature
    xoj::util::WidgetSPtr button;

    friend class PreviewJob;
};

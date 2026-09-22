/*
 * Xournal++
 *
 * A Sidebar preview widget
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>   // for size_t
#include <optional>  // for optional
#include <string>    // for string

#include <gtk/gtk.h>  // for GtkWidget

#include "gui/sidebar/previews/base/SidebarPreviewBaseEntry.h"  // for Previ...
#include "model/PageRef.h"                                      // for PageRef
#include "util/raii/GObjectSPtr.h"                              // for WidgetSPtr

class SidebarPreviewPages;

/**
 * One page in the page navigator: the thumbnail, the page number, and the state of the page.
 *
 * The card is one widget that both density modes use, rather than two widgets per page: the
 * thumbnail is drawn by the base class either way, and only the way the metadata is presented
 * changes with the mode. The state the user sees - selected, current, loading, error - is
 * expressed as CSS classes and as an accessible name, so a test can read it back without looking
 * at pixels.
 */
class SidebarPreviewPageEntry: public SidebarPreviewBaseEntry {
public:
    SidebarPreviewPageEntry(SidebarPreviewPages* sidebar, const PageRef& page, size_t index);
    ~SidebarPreviewPageEntry() override;

public:
    /// The card, not the thumbnail: this is what the navigator puts into its containers.
    GtkWidget* getWidget() const override;

    int getHeight() const override;

    PreviewRenderType getRenderType() const override;

    void setIndex(size_t index);
    size_t getIndex() const;

    /// The page number shown to the user, 1-based.
    size_t getPageNumber() const;

    bool isSelected() const;
    bool isCurrentPage() const;
    double getZoom() const;

    /// The page this card shows.
    const PageRef& getPage() const;

public:
    // State, all of it mirrored into the CSS classes and the accessible name of the card.
    void setSelected(bool selected) override;
    void setCurrentPage(bool current);

    /**
     * Switch the card between the navigator's density modes.
     *
     * List mode writes the metadata in a label next to the thumbnail and leaves the page number
     * out of the drawing; overview mode draws the page number the way the sidebar always has.
     */
    void setListMode(bool listMode);
    bool isListMode() const;

    /**
     * The drag/drop insertion indicator: `true` marks the card as the one the dragged pages would
     * land before, `false` after it, and `std::nullopt` clears the indicator.
     */
    void setDropIndicator(std::optional<bool> before);

    /// Refresh the card's size request, its CSS classes and its accessible name.
    void refresh();

    /// What a screen reader announces for this page.
    auto buildAccessibleName() const -> std::string;

    /**
     * Where a pointer inside the card asks for a dropped page to land.
     *
     * Rows stack downwards, thumbnails wrap left to right, so the half that means "before" is the
     * first half along the direction the cards are read in.
     *
     * @param x,y A position in the card's own coordinates
     * @return true when the drop belongs before this page
     */
    auto isDropBefore(int x, int y) const -> bool;

protected:
    void mouseButtonPressCallback() override;
    void paint(cairo_t* cr) override;

    /// The thumbnail arrived, or could not be rendered: the card follows.
    void thumbnailStateChanged() override;

private:
    void updateCssClasses();
    void updateMetadata();

protected:
    SidebarPreviewPages* sidebar;

private:
    size_t index;

    /// The card: the thumbnail plus the metadata of list mode.
    xoj::util::WidgetSPtr card;
    /// The selection marker: a glyph, so the selection is not carried by colour alone.
    xoj::util::WidgetSPtr marker;
    /// The metadata label, shown in list mode only.
    GtkWidget* metadata = nullptr;

    bool current = false;
    bool listMode = false;
    std::optional<bool> dropBefore;

    /// Modifiers of the press that is about to turn into a `clicked` signal.
    bool pressControl = false;
    bool pressShift = false;

    friend class PreviewJob;
};

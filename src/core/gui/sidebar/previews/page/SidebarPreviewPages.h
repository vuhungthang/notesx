/*
 * Xournal++
 *
 * Previews of the pages in the document
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>  // for size_t
#include <memory>   // for unique_ptr
#include <string>   // for string
#include <vector>   // for vector

#include <gdk/gdk.h>  // for GdkEvent
#include <glib.h>     // for gulong
#include <gtk/gtk.h>  // for GtkWidget

#include "gui/IconNameHelper.h"                            // for IconNameHe...
#include "gui/sidebar/previews/base/SidebarPreviewBase.h"  // for SidebarPre...
#include "util/raii/GObjectSPtr.h"

class Control;
class GladeGui;
class SidebarPreviewPageEntry;
namespace xoj::model {
class PageSelectionModel;
}

/**
 * The page navigator: one card per page, in a container that lays them out and that owns them.
 *
 * Two containers, one model: a `GtkFlowBox` that wraps the thumbnails into as many columns as the
 * sidebar is wide, and a `GtkListBox` whose rows give the metadata more room. Both hold the same
 * `SidebarPreviewPageEntry` widgets, so switching the density moves the cards rather than building
 * a second set of them, and no page state is duplicated.
 *
 * The selection itself lives in the model `Control` owns, so the page actions in the menu and the
 * toolbar act on exactly what the navigator shows as selected.
 */
class SidebarPreviewPages: public SidebarPreviewBase {
public:
    /// How much of a card goes to the thumbnail and how much to the metadata (Plan 005, step 6).
    enum class DensityMode { OVERVIEW, LIST };

public:
    SidebarPreviewPages(Control* control);
    ~SidebarPreviewPages() override;

public:
    void enableSidebar() override;

    /**
     * @overwrite
     */
    std::string getName() override;

    /**
     * @overwrite
     */
    std::string getIconName() override;

    /**
     * Update the preview images
     * @overwrite
     */
    void updatePreviews() override;

    /**
     * The containers lay their own children out; there is nothing left to position by hand.
     */
    void layout() override;

    /**
     * The thumbnails are smaller in list mode, where the metadata has the room.
     */
    double getZoom() const override;

public:
    // DocumentListener interface (only the part which is not handled by SidebarPreviewBase)
    void pageSizeChanged(size_t page) override;
    void pageChanged(size_t page) override;
    void pageSelected(size_t page) override;
    void pageInserted(size_t page) override;
    void pageDeleted(size_t page) override;
    void pagesReordered() override;

public:
    /// Plan 005, step 6: the density the user picked, persisted as a UI preference.
    void setDensityMode(DensityMode mode);
    auto getDensityMode() const -> DensityMode;

    /// The model `Control` owns, which holds what the navigator shows as selected.
    auto getSelectionModel() -> xoj::model::PageSelectionModel&;

    /**
     * Give every control of `root` the tooltip the user sees as its accessible name.
     *
     * The page toolbar is icon-only, so the accessible name is the only thing a screen reader has
     * to read; taking it from the tooltip keeps the two from drifting apart. A declared tooltip is
     * also switched on, because one that never shows helps nobody.
     */
    static void applyToolbarAccessibility(GtkWidget* root);

    /**
     * A card was clicked or activated from the keyboard.
     *
     * @param controlPressed Ctrl: add or remove this one page
     * @param shiftPressed Shift: select the range from the anchor to this page
     */
    void onCardClicked(SidebarPreviewPageEntry* entry, bool controlPressed, bool shiftPressed);

    /// A card was right-clicked: the page is selected before the menu opens.
    void onCardContextMenu(SidebarPreviewPageEntry* entry);

    /// Plan 005, step 5: the page order a drop before/after `entry` asks for.
    auto getDropDestination(const SidebarPreviewPageEntry* entry, bool before) const -> size_t;

    /// Move the selected pages so that they land before `destination`.
    void moveSelectedPages(size_t destination);

    /// A drag started on `entry`.
    void beginPageDrag(SidebarPreviewPageEntry* entry);
    /// The pointer is over `entry`, in its first or its second half.
    void updateDropIndicator(SidebarPreviewPageEntry* entry, bool before);
    /// The drag ended without a drop, or the pointer left every card.
    void clearDropIndicator();
    /// The dragged pages are dropped before/after `entry`.
    void dropPages(SidebarPreviewPageEntry* entry, bool before);
    auto isDragging() const -> bool;

    /// The page the pointer is over, from a position in the scrolled area. Used by the auto-scroll.
    void autoScrollDuringDrag();

    // -- Accessors for the tests and for the toolbar controls.
    auto getOverviewWidget() const -> GtkWidget* { return this->overviewWidget; }
    auto getListWidget() const -> GtkWidget* { return this->listWidget; }
    auto getContainerWidget() const -> GtkWidget* { return this->container.get(); }
    /// The card showing `page`, or nullptr.
    auto getEntry(size_t page) const -> SidebarPreviewPageEntry*;

private:
    /**
     * Updates the indices of the pages
     */
    void updateIndices();

    /// Re-apply the model to the cards: which page is current, which are selected.
    void refreshSelection();

    /// A card is going away: it must not stay the drop target of a drag.
    void forgetEntry(const SidebarPreviewPageEntry* entry);

    /// Put every card into the container of the current density, in page order.
    void rebuildContainer();

    /// Move the page count into the tooltip of the destructive action.
    void updateActionLabels();

    /**
     * The tooltip and the accessible name of a toolbar control, set together so the words the user
     * sees and the words a screen reader reads cannot drift apart.
     */
    static void setActionLabel(GtkWidget* button, const std::string& label);

    /// Connect the density toggle buttons of the toolbar.
    void connectToolbarControls();

private:
    IconNameHelper iconNameHelper;

    /// The box the two containers live in; only the one of the current density is visible.
    xoj::util::WidgetSPtr container;
    /// Wraps the thumbnails into the width of the sidebar.
    GtkWidget* overviewWidget = nullptr;
    /// Rows, with room for the metadata.
    GtkWidget* listWidget = nullptr;

    /// The toolbar controls, kept so their state can follow the density and the selection.
    GtkWidget* overviewButton = nullptr;
    GtkWidget* listButton = nullptr;
    GtkWidget* deleteButton = nullptr;
    /// The handler ids of the density toggles, so their state can be set without re-entering
    /// setDensityMode().
    gulong overviewHandler = 0;
    gulong listHandler = 0;

    DensityMode densityMode = DensityMode::OVERVIEW;

    /// Drag and drop state.
    bool dragging = false;
    SidebarPreviewPageEntry* dropTarget = nullptr;
    bool dropBefore = false;

    friend class SidebarPreviewPageEntry;
};

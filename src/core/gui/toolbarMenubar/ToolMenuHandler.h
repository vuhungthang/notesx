/*
 * Xournal++
 *
 * Part of the customizable toolbars
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>   // for size_t
#include <memory>    // for unique_ptr
#include <optional>  // for optional
#include <string>    // for string
#include <vector>    // for vector

#include <gio/gio.h>
#include <glib-object.h>  // for GObject, GConnectFlags
#include <glib.h>         // for gchar
#include <gtk/gtk.h>      // for GtkWidget, GtkWindow, GtkBuilder

#include "gui/IconNameHelper.h"  // for IconNameHelper
#include "model/PaperSize.h"
#include "util/raii/GObjectSPtr.h"

#include "ToolPropertyPanel.h"     // for PresetListListener
#include "ToolPropertyProvider.h"  // for ToolPropertyRegistry

class AbstractToolItem;
class ActiveToolSummaryItem;
class GladeGui;
class GladeSearchpath;
class ToolbarData;
class ToolbarModel;
class ToolButton;
class ToolHandler;
class ToolPageLayer;
class ToolPageSpinner;
class PageTypeMenu;
class SpinPageAdapter;
class ZoomControl;
class Control;
class PageBackgroundChangeController;
class ColorToolItem;
struct ToolbarButtonEntry;
struct ToolbarPlaceholderEntry;
class PageTypeSelectionPopover;
class PageType;
struct Palette;
class StylePopoverFactory;
class Recolor;
class PresetFavoritesItem;
class ToolPropertyPopoverFactory;

class ToolMenuHandler: public PresetListListener {
public:
    ToolMenuHandler(Control* control, GladeGui* gui);
    virtual ~ToolMenuHandler();

    void populate(const GladeSearchpath* gladeSearchPath);

public:
    void freeDynamicToolbarItems();
    static void unloadToolbar(GtkWidget* toolbar);

    /**
     * @brief Load the toolbar.ini file
     * This file persists the customized toolbars and is loaded upon starting the application.
     *
     * @param d Data Object representing the selected toolbars (e.g Portrait)
     * @param toolbar reference to the widget representing the toolbar
     * @param toolbarName toolbarName which should be read from the file
     * @param horizontal whether the toolbar is horizontal
     */
    void load(const ToolbarData* d, GtkWidget* toolbar, const char* toolbarName, bool horizontal);

    /**
     * @brief Update all ColorToolItems based on palette
     *
     * @param palette
     */
    void updateColorToolItems(const Palette& palette);

    /**
     * @brief Update all ColorToolItems based on recolor settings
     *
     * @param recolor
     */
    void updateColorToolItemsRecoloring(const std::optional<Recolor>& recolor);

    void initToolItems();
    void addPluginItem(ToolbarButtonEntry* t);
    void addPluginPlaceholderItem(ToolbarPlaceholderEntry* entry);

    void setPageInfo(size_t currentPage, size_t pageCount, size_t pdfpage);

    [[maybe_unused]] void removeColorToolItem(AbstractToolItem* it);
    void addColorToolItem(std::unique_ptr<ColorToolItem> it);

    ToolbarModel* getModel();

    const std::vector<std::unique_ptr<AbstractToolItem>>& getToolItems() const;
    const std::vector<std::unique_ptr<ColorToolItem>>& getColorToolItems() const;

    Control* getControl();

    void hideAudioMenuItems();

    std::string iconName(const char* icon);

    void setDefaultNewPageType(const std::optional<PageType>& pt);
    void setDefaultNewPaperSize(const std::optional<PaperSize>& paperSize);

    /// Plan 003: the stored preset list changed, so the favourite strip has to be rebuilt.
    void presetListChanged() override;

private:
    template <class tool_item, class... Args>
    tool_item& emplaceItem(Args&&... args);

    void initPenToolItem();
    void initEraserToolItem();

private:
    std::vector<std::unique_ptr<ColorToolItem>> toolbarColorItems;
    GtkWindow* parent = nullptr;

    std::vector<std::unique_ptr<AbstractToolItem>> toolItems;

    ToolPageSpinner* toolPageSpinner = nullptr;

    Control* control = nullptr;
    ZoomControl* zoom = nullptr;
    GladeGui* gui = nullptr;
    ToolHandler* toolHandler = nullptr;

    std::unique_ptr<ToolbarModel> tbModel;

    PageTypeMenu* newPageType = nullptr;
    PageBackgroundChangeController* pageBackgroundChangeController = nullptr;
    IconNameHelper iconNameHelper;

    std::unique_ptr<PageTypeSelectionPopover> pageTypeSelectionPopup;

    /// Plan 003: which tools have a property panel. Shape and selection register one later.
    ToolPropertyRegistry propertyRegistry;
    std::unique_ptr<ToolPropertyPopoverFactory> penPropertyPopover;
    std::unique_ptr<ToolPropertyPopoverFactory> highlighterPropertyPopover;
    std::unique_ptr<ToolPropertyPopoverFactory> eraserPropertyPopover;

    /// The two items that show the plan's new state. Owned by the toolbar item list, like every
    /// other item; the pointers are here so that a change can reach them.
    ActiveToolSummaryItem* activeToolSummary = nullptr;
    PresetFavoritesItem* presetFavorites = nullptr;

    xoj::util::GObjectSPtr<GSimpleAction> gAction;
};

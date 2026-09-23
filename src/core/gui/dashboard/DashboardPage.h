/*
 * Xournal++
 *
 * The home surface: what the user was working on, what they pinned, the folders they list, what can
 * be recovered and the ways to start something new.
 *
 * Plan 006, steps 3 to 6. The page is a view of `xoj::dashboard::DashboardModel` and holds no state
 * of its own beyond the previews it has been handed: every section is rebuilt from the model, so
 * the dashboard stays an index over files rather than a second place that remembers them. It never
 * touches a user file - it opens one through the same path File > Open takes, and everything else
 * it does is a change to the settings.
 *
 * The page has no `Control`: what it asks for, it asks for through its callbacks, which is what
 * lets its sections, its card states and its actions be tested without a document.
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>     // for size_t
#include <functional>  // for function
#include <map>         // for map
#include <string>      // for string
#include <vector>      // for vector

#include <glib.h>  // for gpointer
#include <gtk/gtk.h>

#include "dashboard/DashboardModel.h"    // for DashboardModel, DashboardSection, DocumentCard
#include "dashboard/ThumbnailService.h"  // for ThumbnailService
#include "util/raii/GObjectSPtr.h"       // for WidgetSPtr

#include "filesystem.h"  // for path

namespace xoj::dashboard {

class DashboardPage {
public:
    /// What the page asks its owner to do. The page itself has no Control and no dialog.
    struct Callbacks {
        /// Open a document. This is the established file-open path, not a second one.
        std::function<void(const fs::path&)> open;
        /// Pin or unpin a file.
        std::function<void(const fs::path&, bool)> setPinned;
        /// Stop showing a path in the recent list. The file itself is left alone.
        std::function<void(const fs::path&)> forget;
        /// Ask the user for a file that moved, starting from the path they had.
        std::function<void(const fs::path&)> locate;
        /// Ask the user for a folder to list.
        std::function<void()> addFolder;
        /// Stop listing a folder. Nothing inside it is touched.
        std::function<void(const fs::path&)> removeFolder;
        /// List (or stop listing) a folder's subtree.
        std::function<void(const fs::path&, bool)> setFolderRecursive;
        /// Start something new, or open something the user already has (File > Open).
        std::function<void()> newNote;
        std::function<void()> annotatePdf;
        /// A note started without choosing anything, for writing something down right away.
        std::function<void()> quickNote;
        std::function<void()> openFile;
        /// What to do with a recovery copy.
        std::function<void(const RecoveryCard&)> openRecovery;
        std::function<void(const RecoveryCard&)> saveRecoveryAs;
        std::function<void(const RecoveryCard&)> revealRecovery;
        std::function<void(const RecoveryCard&)> deleteRecovery;
        /// Go back to the document in the editor.
        std::function<void()> backToDocument;
        /**
         * Ask the user before something is done that cannot be undone.
         *
         * The page never deletes anything itself: it asks, and runs `confirmed` only if the user
         * said so. That is what makes "Delete requires an explicit confirmation" a property of the
         * page rather than of whoever wired it up.
         */
        std::function<void(const std::string& title, const std::string& message, std::function<void()> confirmed)>
                confirm;
    };

    /**
     * @param model the dashboard's cards. Read by this page, never written to except for the
     *              preview state, which is what a card learned about its file.
     * @param callbacks what the user's actions mean
     * @param thumbnails the preview reader. Null means cards show placeholders, which is what a
     *                   test that is not about previews wants.
     */
    DashboardPage(DashboardModel& model, Callbacks callbacks, ThumbnailService* thumbnails = nullptr);
    ~DashboardPage();

    DashboardPage(const DashboardPage&) = delete;
    auto operator=(const DashboardPage&) -> DashboardPage& = delete;

    /// The page. Add it to a container; it is owned by this object.
    auto getWidget() const -> GtkWidget*;

    /// Rebuild every section from the model.
    void refresh();

    /// The document the editor holds, which "Back" returns to. Empty hides the button.
    void setOpenDocument(const std::string& name);

    /// Whether the page is on screen. Used to keep asking for previews only while it is.
    void setActive(bool active);
    auto isActive() const -> bool;

    /// The sections the page shows, in the order it shows them.
    auto getSections() const -> std::vector<DashboardSection>;
    /// The widget of a section, nullptr when the section is not shown.
    auto getSectionWidget(DashboardSection section) const -> GtkWidget*;
    /// The text of a section's title, empty when the section is not shown.
    auto getSectionTitle(DashboardSection section) const -> std::string;
    /// What a section says when it has no cards, empty when it has some or has no hint.
    auto getSectionHint(DashboardSection section) const -> std::string;

    /// How many cards a section shows.
    auto getCardCount(DashboardSection section) const -> std::size_t;
    /// A card's widget, nullptr when there is no such card.
    auto getCard(DashboardSection section, std::size_t index) const -> GtkWidget*;
    /// The button that opens a card's file.
    auto getCardOpenButton(DashboardSection section, std::size_t index) const -> GtkWidget*;
    /// The image that shows a card's preview.
    auto getCardPreview(DashboardSection section, std::size_t index) const -> GtkWidget*;
    /// A card's pin toggle.
    auto getCardPinButton(DashboardSection section, std::size_t index) const -> GtkWidget*;
    /// One of a card's menu items by name: "open", "pin", "locate", "forget". Null when absent.
    auto getCardMenuItem(DashboardSection section, std::size_t index, const char* action) const -> GtkWidget*;
    /// The path a card stands for.
    auto getCardPath(DashboardSection section, std::size_t index) const -> fs::path;

    /// One of the page's own buttons by name: "new-note", "quick-note", "annotate-pdf",
    /// "add-folder", "back". Null when absent.
    auto getButton(const char* action) const -> GtkWidget*;

    /// A recovery card's button by name: "open", "save-as", "reveal", "delete".
    auto getRecoveryButton(std::size_t index, const char* action) const -> GtkWidget*;
    /// How many recovery cards the page shows.
    auto getRecoveryCount() const -> std::size_t;

    /// One of the rows a listed folder is shown with, by name: "remove", "recursive", "name".
    auto getFolderRow(const fs::path& folder, const char* part) const -> GtkWidget*;
    /// How many folders the library section lists.
    auto getFolderRowCount() const -> std::size_t;

private:
    /// The widgets one document card is made of.
    struct CardWidgets {
        GtkWidget* root = nullptr;
        GtkWidget* open = nullptr;
        GtkWidget* preview = nullptr;
        GtkWidget* title = nullptr;
        GtkWidget* metadata = nullptr;
        GtkWidget* pin = nullptr;
        GtkWidget* menuButton = nullptr;
        GtkWidget* openItem = nullptr;
        GtkWidget* pinItem = nullptr;
        GtkWidget* locateItem = nullptr;
        GtkWidget* forgetItem = nullptr;
        fs::path path;
    };

    /// The widgets one recovery card is made of.
    struct RecoveryWidgets {
        GtkWidget* open = nullptr;
        GtkWidget* saveAs = nullptr;
        GtkWidget* reveal = nullptr;
        GtkWidget* remove = nullptr;
    };

    /// The page's own header: what it is, and the way back to the open document.
    void buildHeader();
    void buildSections();
    /// The section's box, with its title and the widget its cards go into.
    auto buildSection(DashboardSection section) -> GtkWidget*;
    void buildCards(DashboardSection section, GtkWidget* flow);
    void buildFolders(GtkWidget* box);
    void buildRecoveryCards(GtkWidget* flow);
    void buildTemplates(GtkWidget* box);
    /// Empty every section, so a rebuild leaves nothing of the old one behind.
    void clearContent();

    auto buildDocumentCard(const DocumentCard& card) -> CardWidgets;
    auto buildRecoveryCard(const RecoveryCard& card) -> RecoveryWidgets;

    /// Show the file's first page if it is known, and its state if there is nothing to show.
    void applyPreview(GtkWidget* image, const DocumentCard& card);
    /// Ask for the previews of the cards that have none yet, and drop the ones that went away.
    void requestPreviews();

    static void onCardOpened(GtkButton* button, gpointer data);
    static void onPinToggled(GtkToggleButton* button, gpointer data);
    static void onMenuAction(GtkMenuItem* item, gpointer data);
    static void onCardButtonPressed(GtkWidget* widget, GdkEventButton* event, gpointer data);

    /// What a menu item or a card button means. Kept as data so the handlers stay one line each.
    enum class Action {
        Open,
        TogglePin,
        Locate,
        Forget,
        OpenRecovery,
        SaveRecoveryAs,
        RevealRecovery,
        DeleteRecovery,
        NewNote,
        AnnotatePdf,
        QuickNote,
        OpenFile,
        AddFolder,
        RemoveFolder,
        ToggleRecursive,
        Back
    };

    struct ItemData {
        DashboardPage* page = nullptr;
        Action action = Action::Open;
        /// The document or the recovery copy the action is about.
        fs::path path;
        RecoveryCard recovery;
        bool flag = false;
    };

    void run(const ItemData& item);
    /// Register the data a handler needs, so the handlers themselves stay one line each.
    auto makeItem(Action action, const fs::path& path, bool flag = false, const RecoveryCard& recovery = {})
            -> ItemData*;
    /// A card learned something about its file: say it on every card that shows it.
    void updateCardsFor(const fs::path& path);
    /// A preview arrived. The page only shows it; the model is told what it learned.
    void onPreviewReady(const std::string& key, const fs::path& path, const ThumbnailResult& result);

    DashboardModel& model;
    Callbacks callbacks;
    ThumbnailService* thumbnails;

    xoj::util::WidgetSPtr page;
    /**
     * The box the sections go in.
     *
     * The page holds a reference to it in addition to the one the scroller has, because GTK
     * finalizes a container's children when the window above them goes away: without the reference
     * the pointer below would dangle the moment a test (or a closed window) destroyed the tree,
     * and the page has to be able to empty itself after that without touching freed memory.
     */
    xoj::util::WidgetSPtr contentHolder;
    GtkWidget* content = nullptr;
    GtkWidget* header = nullptr;
    GtkWidget* backButton = nullptr;
    GtkWidget* title = nullptr;
    GtkWidget* documentLabel = nullptr;

    std::vector<DashboardSection> shownSections;
    std::map<std::string, GtkWidget*> sectionWidgets;
    std::map<std::string, GtkWidget*> sectionTitles;
    std::map<std::string, GtkWidget*> sectionHints;
    std::map<std::string, GtkWidget*> sectionFlows;
    std::map<std::string, GtkWidget*> sectionBodies;
    std::map<std::string, GtkWidget*> buttons;
    std::map<std::string, std::vector<CardWidgets>> cards;
    /// A listed folder's row, by folder path and then by part name.
    std::vector<std::pair<fs::path, std::map<std::string, GtkWidget*>>> folderRows;
    std::vector<RecoveryWidgets> recoveryCards;
    /// The cards a file is shown as, by canonical path, so a preview that arrives finds its card.
    std::map<std::string, std::vector<CardWidgets*>> cardsByPath;
    /// The pressed-key/right-click handlers' data, freed with the cards they belong to.
    std::vector<ItemData*> itemData;
    /// The menus this page has built, so they are not shown with the page's other children.
    std::vector<GtkWidget*> menus;

    /// The previews this page has been handed, by canonical path.
    std::map<std::string, std::vector<std::uint8_t>> previewBytes;
    /// The pixbufs built from them, owned here.
    std::map<std::string, GdkPixbuf*> previewPixbufs;
    /// The requests that are still out, so one that goes away is cancelled rather than answered.
    std::map<std::string, ThumbnailRequestId> pendingRequests;
    bool active = false;
    /// The document the editor holds, kept across rebuilds so the way back survives them.
    std::string openDocumentName;
};

}  // namespace xoj::dashboard

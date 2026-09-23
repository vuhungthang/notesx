/*
 * Xournal++
 *
 * This file is part of the Xournal UnitTests
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#include <chrono>  // for seconds, milliseconds
#include <cstddef>
#include <cstdint>
#include <memory>  // for shared_ptr
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "../dialog/GtkTest.h"
#include "dashboard/DashboardModel.h"     // for DashboardModel
#include "dashboard/ThumbnailCache.h"     // for ThumbnailCache
#include "dashboard/ThumbnailService.h"   // for ThumbnailService
#include "gui/dashboard/DashboardPage.h"  // for DashboardPage

#include "config-test.h"  // for GET_TESTFILE
#include "filesystem.h"   // for path

/*
 * Plan 006, steps 3 to 6: the home surface.
 *
 * The page is built from a model without a Control and without a MainWindow: what it does with a
 * card, it does through its callbacks, so the tests can see exactly what a click asks for. That is
 * how the plan's requirements are checked here - the sections in their order and each one either
 * carrying cards or saying what to do, cards that open through the established path, a pin that is
 * a change of settings and nothing more, a delete that only happens after a confirmation, previews
 * that arrive from the worker and land in the card, and cards that keep their size while a row of
 * them wraps on a narrow window.
 */

namespace {

using xoj::dashboard::DashboardModel;
using xoj::dashboard::DashboardPage;
using xoj::dashboard::DashboardSection;
using xoj::dashboard::DocumentCard;
using xoj::dashboard::RecoveryCard;
using xoj::dashboard::ThumbnailCache;
using xoj::dashboard::ThumbnailService;
using xoj::safety::RecoveryCandidate;
using xoj::safety::RecoveryValidation;

/// The test files this page is exercised with.
auto validDocument() -> fs::path { return fs::path{GET_TESTFILE(u8"packaged_xopp/test.xopp")}; }
/// A document whose stored preview is not a picture.
auto corruptPreviewDocument() -> fs::path { return fs::path{GET_TESTFILE(u8"packaged_xopp/testPreview.xopp")}; }
/// A document that holds no preview at all: an unzipped note with no preview section.
auto unpreviewableDocument() -> fs::path { return fs::path{GET_TESTFILE(u8"preview-test-no-preview.unzipped.xoj")}; }

/**
 * A folder of this test's own, outside the source tree so nothing a test writes can be mistaken for
 * something the project keeps, and empty at the start of every test that asks for it.
 */
auto scratchFolder(const char* name) -> fs::path {
    fs::path folder = fs::path(g_get_tmp_dir()) / "xournalpp-test-units_dashboardPage" / name;
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

/// A copy of a real document in the scratch folder, with the time it was last written set.
auto documentWrittenAt(const fs::path& folder, const char* name, int hoursAgo) -> fs::path {
    const fs::path copy = folder / name;
    fs::copy_file(validDocument(), copy, fs::copy_options::overwrite_existing);
    fs::last_write_time(copy, fs::file_time_type::clock::now() - std::chrono::hours(hoursAgo));
    return copy;
}

/// Take the scratch folder away again, so a test run leaves nothing behind.
void clearScratchFolder() {
    std::error_code error;
    fs::remove_all(fs::path(g_get_tmp_dir()) / "xournalpp-test-units_dashboardPage", error);
}

/// Every widget at or below `root`, in tree order.
void collectWidgets(GtkWidget* root, std::vector<GtkWidget*>& out) {
    out.emplace_back(root);
    if (GTK_IS_CONTAINER(root)) {
        for (GList* children = gtk_container_get_children(GTK_CONTAINER(root)); children != nullptr;
             children = children->next) {
            collectWidgets(GTK_WIDGET(children->data), out);
        }
    }
}

auto allWidgets(GtkWidget* root) -> std::vector<GtkWidget*> {
    std::vector<GtkWidget*> widgets;
    collectWidgets(root, widgets);
    return widgets;
}

auto accessibleName(GtkWidget* widget) -> std::string {
    const char* name = atk_object_get_name(gtk_widget_get_accessible(widget));
    return name != nullptr ? std::string(name) : std::string();
}

/// The named descendant of `root`, so a test finds a widget the way a user's screen reader does.
auto widgetNamed(GtkWidget* root, const std::string& name) -> GtkWidget* {
    for (GtkWidget* widget: allWidgets(root)) {
        if (name == gtk_widget_get_name(widget)) {
            return widget;
        }
    }
    return nullptr;
}

/// The width a card's preview is drawn at, which is the card's own size in every one of these tests.
auto previewSizeOf(DashboardPage& page, std::size_t index) -> std::pair<int, int> {
    GtkWidget* preview = page.getCardPreview(DashboardSection::Pinned, index);
    if (preview == nullptr) {
        return {0, 0};
    }
    int width = 0;
    int height = 0;
    gtk_widget_get_size_request(preview, &width, &height);
    return {width, height};
}

/// Lets GTK finish what it queued, so the widget tree settles into the state a user would see.
void settle() {
    while (g_main_context_iteration(nullptr, FALSE)) {}
}

/**
 * Counts the GLib criticals logged while it is alive.
 *
 * A widget that unrefs memory it does not own, or that is rebuilt over its own handler data, does
 * not crash a normal build: GLib logs a critical and carries on. The rebuilds below are watched
 * here, because a critical is a failure.
 */
class CriticalWatch {
public:
    CriticalWatch() { this->previous = g_log_set_default_handler(onLog, this); }
    ~CriticalWatch() { g_log_set_default_handler(this->previous, nullptr); }

    CriticalWatch(const CriticalWatch&) = delete;
    auto operator=(const CriticalWatch&) -> CriticalWatch& = delete;

    auto count() const -> std::size_t { return this->criticals.size(); }

    auto report() const -> std::string {
        std::string all;
        for (const std::string& message: this->criticals) {
            all += message;
            all += '\n';
        }
        return all.empty() ? std::string("no critical was logged") : all;
    }

private:
    static void onLog(const gchar* domain, GLogLevelFlags levels, const gchar* message, gpointer data) {
        auto* self = static_cast<CriticalWatch*>(data);
        if ((levels & G_LOG_LEVEL_CRITICAL) != 0) {
            self->criticals.emplace_back(message != nullptr ? message : "");
        }
        if (self->previous != nullptr) {
            self->previous(domain, levels, message, nullptr);
        }
    }

    std::vector<std::string> criticals;
    GLogFunc previous = nullptr;
};

/// What a page asked for, so a test can check the action rather than the widget.
struct Recorder {
    std::vector<fs::path> opened;
    std::vector<std::pair<fs::path, bool>> pinned;
    std::vector<fs::path> forgotten;
    std::vector<fs::path> toLocate;
    std::vector<fs::path> foldersRemoved;
    std::vector<std::pair<fs::path, bool>> foldersRecursive;
    int addedFolder = 0;
    int newNotes = 0;
    int quickNotes = 0;
    int pdfs = 0;
    int backs = 0;
    std::vector<RecoveryCard> recoveryOpened;
    std::vector<RecoveryCard> recoverySavedAs;
    std::vector<RecoveryCard> recoveryRevealed;
    std::vector<RecoveryCard> recoveryDeleted;

    /// What was asked to be confirmed, and the confirmation itself, if it was offered.
    std::vector<std::string> confirmations;
    std::vector<std::function<void()>> confirmationsToRun;

    auto callbacks() -> DashboardPage::Callbacks {
        DashboardPage::Callbacks callbacks;
        callbacks.open = [this](const fs::path& path) { this->opened.emplace_back(path); };
        callbacks.setPinned = [this](const fs::path& path, bool value) { this->pinned.emplace_back(path, value); };
        callbacks.forget = [this](const fs::path& path) { this->forgotten.emplace_back(path); };
        callbacks.locate = [this](const fs::path& path) { this->toLocate.emplace_back(path); };
        callbacks.addFolder = [this]() { this->addedFolder++; };
        callbacks.removeFolder = [this](const fs::path& path) { this->foldersRemoved.emplace_back(path); };
        callbacks.setFolderRecursive = [this](const fs::path& path, bool value) {
            this->foldersRecursive.emplace_back(path, value);
        };
        callbacks.newNote = [this]() { this->newNotes++; };
        callbacks.quickNote = [this]() { this->quickNotes++; };
        callbacks.annotatePdf = [this]() { this->pdfs++; };
        callbacks.openRecovery = [this](const RecoveryCard& card) { this->recoveryOpened.emplace_back(card); };
        callbacks.saveRecoveryAs = [this](const RecoveryCard& card) { this->recoverySavedAs.emplace_back(card); };
        callbacks.revealRecovery = [this](const RecoveryCard& card) { this->recoveryRevealed.emplace_back(card); };
        callbacks.deleteRecovery = [this](const RecoveryCard& card) { this->recoveryDeleted.emplace_back(card); };
        callbacks.backToDocument = [this]() { this->backs++; };
        callbacks.confirm = [this](const std::string& title, const std::string& message,
                                   std::function<void()> confirmed) {
            this->confirmations.emplace_back(title + ": " + message);
            this->confirmationsToRun.emplace_back(std::move(confirmed));
        };
        return callbacks;
    }
};

/// The model a test starts from: what the user was working on, what they pinned and what they list.
auto dashboardModel() -> std::unique_ptr<DashboardModel> {
    auto model = std::make_unique<DashboardModel>();
    model->setRecentFiles({validDocument()});
    model->setPinnedFiles({validDocument()});
    model->setLibraryFolders({});
    return model;
}

auto recoveryCandidate(const fs::path& path, RecoveryValidation validation) -> RecoveryCandidate {
    RecoveryCandidate candidate;
    candidate.recoveryPath = path;
    candidate.originalPath = path.parent_path() / "notes.xopp";
    candidate.originalExists = true;
    candidate.newerThanOriginal = true;
    candidate.recoveryTime = std::chrono::system_clock::now();
    candidate.size = 1024;
    candidate.validation = validation;
    return candidate;
}

}  // namespace

/*
 * Plan 006, step 3: "Dashboard sections in a fixed order: Recovery, Continue, Pinned, Library,
 * Templates", and one file is one card however many ways the user reached it.
 */
class DashboardPageSectionsTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 1280, 800);

        Recorder recorder;
        const fs::path folder = scratchFolder("sections");
        const fs::path workedOnLast = documentWrittenAt(folder, "worked-on-last.xopp", 1);
        const fs::path workedOnBefore = documentWrittenAt(folder, "worked-on-before.xopp", 5);

        auto model = dashboardModel();
        model->setRecentFiles({workedOnBefore, workedOnLast});
        model->setPinnedFiles({validDocument(), corruptPreviewDocument()});
        model->setRecoveryCandidates({recoveryCandidate(folder / ".notes.autosave.xopp", RecoveryValidation::Ok)});
        model->refresh();

        DashboardPage page{*model, recorder.callbacks(), nullptr};
        gtk_container_add(GTK_CONTAINER(window), page.getWidget());
        gtk_widget_show_all(window);
        settle();

        // The sections are the plan's, in the plan's order, and Recovery is there because there is
        // something to recover.
        const std::vector<DashboardSection> sections = page.getSections();
        ASSERT_EQ(sections.size(), 5U);
        EXPECT_EQ(sections[0], DashboardSection::Recovery);
        EXPECT_EQ(sections[1], DashboardSection::ContinueWorking);
        EXPECT_EQ(sections[2], DashboardSection::Pinned);
        EXPECT_EQ(sections[3], DashboardSection::Library);
        EXPECT_EQ(sections[4], DashboardSection::Templates);

        // Every section is on screen, titled, and reachable as a section of a list of them.
        GtkWidget* content = gtk_widget_get_parent(page.getWidget());
        for (DashboardSection section: sections) {
            GtkWidget* widget = page.getSectionWidget(section);
            ASSERT_NE(widget, nullptr);
            EXPECT_TRUE(gtk_widget_get_visible(widget));
            EXPECT_FALSE(page.getSectionTitle(section).empty()) << "a section without a title is a pile of cards";
            EXPECT_EQ(accessibleName(widget), page.getSectionTitle(section));
        }

        // The cards are the model's, and a file that is recent and pinned is one card in each of the
        // two sections rather than two cards in one.
        EXPECT_EQ(page.getCardCount(DashboardSection::ContinueWorking), 2U);
        EXPECT_EQ(page.getCardCount(DashboardSection::Pinned), 2U);
        EXPECT_EQ(page.getCardCount(DashboardSection::Library), 0U);
        EXPECT_EQ(page.getRecoveryCount(), 1U);

        EXPECT_EQ(page.getCardPath(DashboardSection::Pinned, 0), validDocument());
        EXPECT_EQ(page.getCardPath(DashboardSection::Pinned, 1), corruptPreviewDocument());
        // What was worked on last comes first, whichever order the paths were handed over in.
        EXPECT_EQ(page.getCardPath(DashboardSection::ContinueWorking, 0), workedOnLast);
        EXPECT_EQ(page.getCardPath(DashboardSection::ContinueWorking, 1), workedOnBefore);

        // The cards are in the section's flow box, which is what makes them wrap rather than stretch.
        GtkWidget* flow = widgetNamed(content, "dashboard-flow-pinned");
        ASSERT_NE(flow, nullptr);
        EXPECT_TRUE(GTK_IS_FLOW_BOX(flow));
        EXPECT_EQ(gtk_flow_box_get_max_children_per_line(GTK_FLOW_BOX(flow)), 5)
                << "a wide window shows several cards in a row";
        EXPECT_EQ(gtk_flow_box_get_min_children_per_line(GTK_FLOW_BOX(flow)), 1)
                << "a narrow window shows one per row rather than clipping them";
        EXPECT_TRUE(gtk_flow_box_get_homogeneous(GTK_FLOW_BOX(flow))) << "cards of one kind are one size";

        // Nothing is empty here, so no section explains itself instead of showing cards.
        for (DashboardSection section: {DashboardSection::Recovery, DashboardSection::ContinueWorking,
                                        DashboardSection::Pinned, DashboardSection::Templates}) {
            EXPECT_TRUE(page.getSectionHint(section).empty()) << "a section with cards does not need to say what to do";
        }

        gtk_widget_destroy(window);
        settle();
        clearScratchFolder();
    }
};
TEST_F(DashboardPageSectionsTest, theSectionsAreOrderedAndOneFileIsOneCardPerSection) {}

/*
 * Plan 006, step 3: an empty dashboard is not a blank window. Every section that has nothing says
 * what it is for, and Recovery is not there at all when there is nothing to recover.
 */
class DashboardPageEmptyStateTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 1280, 800);

        Recorder recorder;
        DashboardModel model;
        model.refresh();

        DashboardPage page{model, recorder.callbacks(), nullptr};
        gtk_container_add(GTK_CONTAINER(window), page.getWidget());
        gtk_widget_show_all(window);
        settle();

        const std::vector<DashboardSection> sections = page.getSections();
        ASSERT_EQ(sections.size(), 4U) << "there is nothing to recover, so Recovery is not shown";
        EXPECT_EQ(sections[0], DashboardSection::ContinueWorking);

        for (DashboardSection section: sections) {
            EXPECT_EQ(page.getCardCount(section), 0U);

            // Templates is the one section that is never empty: it is the ways to start something.
            if (section == DashboardSection::Templates) {
                for (const char* button: {"new-note", "quick-note", "annotate-pdf", "add-folder"}) {
                    GtkWidget* widget = page.getButton(button);
                    ASSERT_NE(widget, nullptr) << button << " is always offered";
                    EXPECT_TRUE(gtk_widget_get_visible(widget));
                }
                continue;
            }

            const std::string hint = page.getSectionHint(section);
            EXPECT_FALSE(hint.empty()) << "an empty section says what to do instead of showing nothing";
            for (GtkWidget* widget: allWidgets(page.getSectionWidget(section))) {
                if (GTK_IS_FLOW_BOX(widget)) {
                    EXPECT_FALSE(gtk_widget_get_visible(widget)) << "an empty row of cards is not shown";
                }
            }
        }

        gtk_widget_destroy(window);
        settle();
        clearScratchFolder();
    }
};
TEST_F(DashboardPageEmptyStateTest, everyEmptySectionSaysWhatToDoAndRecoveryIsAbsent) {}

/*
 * Plan 006, steps 4 and 5: a card opens its file through the established path, a missing file is
 * reported and can be found again, and the pin and the "remove" are changes to the lists only.
 */
class DashboardPageCardActionsTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 1280, 800);

        Recorder recorder;
        DashboardModel model;
        const fs::path gone = scratchFolder("actions") / "moved-away.xopp";
        model.setRecentFiles({validDocument(), gone});
        model.setPinnedFiles({validDocument()});
        model.refresh();

        DashboardPage page{model, recorder.callbacks(), nullptr};
        gtk_container_add(GTK_CONTAINER(window), page.getWidget());
        gtk_widget_show_all(window);
        settle();

        // Opening a card goes through the same door as File > Open: the page asks for the file, it
        // does not open anything itself.
        GtkWidget* openButton = page.getCardOpenButton(DashboardSection::Pinned, 0);
        ASSERT_NE(openButton, nullptr);
        EXPECT_TRUE(gtk_widget_get_sensitive(openButton));
        EXPECT_NE(accessibleName(openButton).find("test.xopp"), std::string::npos)
                << "a reader who cannot see the preview still hears which file the card is";
        g_signal_emit_by_name(openButton, "clicked");
        ASSERT_EQ(recorder.opened.size(), 1U);
        EXPECT_EQ(recorder.opened.front(), validDocument());

        // The card's own menu offers the same thing.
        GtkWidget* openItem = page.getCardMenuItem(DashboardSection::Pinned, 0, "open");
        ASSERT_NE(openItem, nullptr);
        g_signal_emit_by_name(openItem, "activate");
        EXPECT_EQ(recorder.opened.size(), 2U);

        // Pinning is a toggle that says what it will do, and it changes the list, not the file.
        GtkWidget* pin = page.getCardPinButton(DashboardSection::Pinned, 0);
        ASSERT_NE(pin, nullptr);
        ASSERT_TRUE(GTK_IS_TOGGLE_BUTTON(pin));
        EXPECT_TRUE(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(pin))) << "the card is pinned, so the toggle is on";
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pin), FALSE);
        ASSERT_EQ(recorder.pinned.size(), 1U);
        EXPECT_EQ(recorder.pinned.front().first, validDocument());
        EXPECT_FALSE(recorder.pinned.front().second) << "unpinning is asked for as unpinning";

        // Removing a card from the dashboard is offered on the cards that are in a list at all.
        GtkWidget* forget = page.getCardMenuItem(DashboardSection::ContinueWorking, 0, "forget");
        ASSERT_NE(forget, nullptr);
        g_signal_emit_by_name(forget, "activate");
        ASSERT_EQ(recorder.forgotten.size(), 1U);
        EXPECT_EQ(recorder.forgotten.front(), validDocument());

        // A file that moved: the card says so, cannot be opened, and can be looked for.
        const DocumentCard* missingCard = model.findCard(gone);
        ASSERT_NE(missingCard, nullptr);
        EXPECT_EQ(missingCard->location, DocumentCard::Location::Missing);

        GtkWidget* missingOpen = page.getCardOpenButton(DashboardSection::ContinueWorking, 1);
        ASSERT_NE(missingOpen, nullptr);
        EXPECT_FALSE(gtk_widget_get_sensitive(missingOpen)) << "a card whose file is gone does not offer to open it";
        EXPECT_NE(accessibleName(missingOpen).find("not there any more"), std::string::npos)
                << "the card says why it cannot be opened";

        GtkWidget* locate = page.getCardMenuItem(DashboardSection::ContinueWorking, 1, "locate");
        ASSERT_NE(locate, nullptr);
        EXPECT_TRUE(gtk_widget_get_sensitive(locate));
        g_signal_emit_by_name(locate, "activate");
        ASSERT_EQ(recorder.toLocate.size(), 1U);
        EXPECT_EQ(recorder.toLocate.front(), gone);

        // The ways to start something new ask for exactly that and nothing else.
        for (const auto& [button, count]:
             std::vector<std::pair<const char*, int*>>{{"new-note", &recorder.newNotes},
                                                       {"quick-note", &recorder.quickNotes},
                                                       {"annotate-pdf", &recorder.pdfs},
                                                       {"add-folder", &recorder.addedFolder}}) {
            GtkWidget* widget = page.getButton(button);
            ASSERT_NE(widget, nullptr) << button;
            g_signal_emit_by_name(widget, "clicked");
            EXPECT_EQ(*count, 1) << button;
        }

        gtk_widget_destroy(window);
        settle();
        clearScratchFolder();
    }
};
TEST_F(DashboardPageCardActionsTest, cardsOpenThroughTheEstablishedPathAndPinsChangeOnlyTheList) {}

/*
 * Plan 006, step 5: the folders the user lists. Adding and removing a folder changes the dashboard's
 * settings and never the folder: listing its subtree is asked for explicitly and is off by default.
 */
class DashboardPageFoldersTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 1280, 800);

        Recorder recorder;
        const fs::path folder = scratchFolder("folders");
        DashboardModel model;
        model.setLibraryFolders({xoj::dashboard::LibraryFolder{.path = folder, .displayName = "Notes"}});
        model.refresh();

        // The folder is listed without asking for its contents, which is what makes listing a big
        // folder cheap.
        ASSERT_EQ(model.getLibraryFolders().size(), 1U);
        EXPECT_FALSE(model.getLibraryFolders().front().recursive);

        DashboardPage page{model, recorder.callbacks(), nullptr};
        gtk_container_add(GTK_CONTAINER(window), page.getWidget());
        gtk_widget_show_all(window);
        settle();

        ASSERT_EQ(page.getFolderRowCount(), 1U);
        GtkWidget* name = page.getFolderRow(folder, "name");
        ASSERT_NE(name, nullptr);
        EXPECT_TRUE(GTK_IS_LABEL(name));
        EXPECT_EQ(std::string(gtk_label_get_text(GTK_LABEL(name))), "Notes");

        GtkWidget* recursive = page.getFolderRow(folder, "recursive");
        ASSERT_NE(recursive, nullptr);
        EXPECT_FALSE(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(recursive)))
                << "a folder is shallow until the user says otherwise";
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(recursive), TRUE);
        ASSERT_EQ(recorder.foldersRecursive.size(), 1U);
        EXPECT_EQ(recorder.foldersRecursive.front().first, folder);
        EXPECT_TRUE(recorder.foldersRecursive.front().second);

        GtkWidget* remove = page.getFolderRow(folder, "remove");
        ASSERT_NE(remove, nullptr);
        EXPECT_NE(accessibleName(remove).find("Stop listing"), std::string::npos);
        g_signal_emit_by_name(remove, "clicked");
        ASSERT_EQ(recorder.foldersRemoved.size(), 1U);
        EXPECT_EQ(recorder.foldersRemoved.front(), folder);

        gtk_widget_destroy(window);
        settle();
        clearScratchFolder();
    }
};
TEST_F(DashboardPageFoldersTest, aFolderIsListedShallowUnlessTheUserAsksForItsSubtree) {}

/*
 * Plan 006, step 6: recovery actions that never overwrite anything and a deletion that only happens
 * after the user has said so.
 */
class DashboardPageRecoveryTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 1280, 800);

        Recorder recorder;
        const fs::path good = scratchFolder("recovery") / ".notes.autosave.xopp";
        const fs::path doubtful = scratchFolder("recovery") / ".other.autosave.xopp";

        DashboardModel model;
        model.setRecoveryCandidates({recoveryCandidate(good, RecoveryValidation::Ok),
                                     recoveryCandidate(doubtful, RecoveryValidation::UnrecognizedFormat)});
        model.refresh();
        ASSERT_EQ(model.getRecoveryCards().size(), 2U);

        DashboardPage page{model, recorder.callbacks(), nullptr};
        gtk_container_add(GTK_CONTAINER(window), page.getWidget());
        gtk_widget_show_all(window);
        settle();

        ASSERT_EQ(page.getRecoveryCount(), 2U);
        ASSERT_TRUE(page.getSections().front() == DashboardSection::Recovery)
                << "work at risk is the first thing the home surface says";

        // A copy that can be read offers to be opened, and opening it does not touch the original.
        GtkWidget* open = page.getRecoveryButton(0, "open");
        ASSERT_NE(open, nullptr);
        EXPECT_TRUE(gtk_widget_get_sensitive(open));
        EXPECT_NE(accessibleName(open).find("Recovered copy"), std::string::npos);
        g_signal_emit_by_name(open, "clicked");
        ASSERT_EQ(recorder.recoveryOpened.size(), 1U);
        EXPECT_EQ(recorder.recoveryOpened.front().recoveryPath, good);

        // Writing it out is a save-as, never a write over the document it came from.
        GtkWidget* saveAs = page.getRecoveryButton(0, "save-as");
        ASSERT_NE(saveAs, nullptr);
        g_signal_emit_by_name(saveAs, "clicked");
        ASSERT_EQ(recorder.recoverySavedAs.size(), 1U);
        EXPECT_EQ(recorder.recoverySavedAs.front().recoveryPath, good);
        EXPECT_EQ(recorder.recoverySavedAs.front().originalPath, good.parent_path() / "notes.xopp");

        g_signal_emit_by_name(page.getRecoveryButton(0, "reveal"), "clicked");
        ASSERT_EQ(recorder.recoveryRevealed.size(), 1U);

        // A copy that does not look like a document cannot be opened, and says so.
        GtkWidget* doubtfulOpen = page.getRecoveryButton(1, "open");
        ASSERT_NE(doubtfulOpen, nullptr);
        EXPECT_FALSE(gtk_widget_get_sensitive(doubtfulOpen)) << "an unreadable copy is not offered as a document";
        EXPECT_NE(accessibleName(doubtfulOpen).find("cannot be opened"), std::string::npos);

        // Deleting a copy is asked about first, and nothing is deleted until the user says so.
        GtkWidget* remove = page.getRecoveryButton(0, "delete");
        ASSERT_NE(remove, nullptr);
        g_signal_emit_by_name(remove, "clicked");
        ASSERT_EQ(recorder.confirmations.size(), 1U) << "a deletion is confirmed before it happens";
        EXPECT_TRUE(recorder.recoveryDeleted.empty()) << "and it has not happened yet";
        EXPECT_NE(recorder.confirmations.front().find("notes.autosave.xopp"), std::string::npos)
                << "the confirmation names what will be deleted";
        EXPECT_NE(recorder.confirmations.front().find("left exactly as it is"), std::string::npos)
                << "and says that the document it came from is not touched";

        ASSERT_EQ(recorder.confirmationsToRun.size(), 1U);
        recorder.confirmationsToRun.front()();
        ASSERT_EQ(recorder.recoveryDeleted.size(), 1U);
        EXPECT_EQ(recorder.recoveryDeleted.front().recoveryPath, good);

        gtk_widget_destroy(window);
        settle();
        clearScratchFolder();
    }
};
TEST_F(DashboardPageRecoveryTest, deletingARecoveredCopyAsksFirstAndThenDeletesOnlyTheCopy) {}

/*
 * Plan 006, step 4: previews are read in the background and land in the card; a document whose
 * stored preview is not a picture is reported rather than shown as a broken image; and a page that
 * is not on screen does not read anything at all.
 */
class DashboardPagePreviewTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 1280, 800);

        Recorder recorder;
        auto service = std::make_shared<ThumbnailService>(std::make_shared<ThumbnailCache>(scratchFolder("previews")));

        DashboardModel model;
        model.setPinnedFiles({validDocument(), corruptPreviewDocument(), unpreviewableDocument()});
        model.refresh();

        DashboardPage page{model, recorder.callbacks(), service.get()};
        gtk_container_add(GTK_CONTAINER(window), page.getWidget());
        gtk_widget_show_all(window);
        settle();

        // Nothing is read for a page the user is not looking at.
        EXPECT_EQ(service->pendingCount(), 0U) << "a page that is not on screen reads nothing";
        EXPECT_EQ(gtk_image_get_storage_type(GTK_IMAGE(page.getCardPreview(DashboardSection::Pinned, 0))),
                  GTK_IMAGE_ICON_NAME);

        // Shown, it asks for the cards whose preview it does not have yet.
        page.setActive(true);
        page.refresh();
        settle();
        service->waitIdle();

        // The worker answers on the main context, which is where the card may be touched.
        for (int i = 0; i < 400; i++) {
            settle();
            g_usleep(2000);
            if (gtk_image_get_storage_type(GTK_IMAGE(page.getCardPreview(DashboardSection::Pinned, 0))) ==
                GTK_IMAGE_PIXBUF) {
                break;
            }
        }

        GtkWidget* preview = page.getCardPreview(DashboardSection::Pinned, 0);
        ASSERT_NE(preview, nullptr);
        ASSERT_EQ(gtk_image_get_storage_type(GTK_IMAGE(preview)), GTK_IMAGE_PIXBUF)
                << "a document with a preview shows it";
        GdkPixbuf* pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(preview));
        ASSERT_NE(pixbuf, nullptr);
        EXPECT_GT(gdk_pixbuf_get_width(pixbuf), 0);
        EXPECT_LE(gdk_pixbuf_get_width(pixbuf), 168) << "a card shows a preview the size of a card";
        EXPECT_LE(gdk_pixbuf_get_height(pixbuf), 120);

        // The card knows what it learned, so a later rebuild does not read the file again.
        EXPECT_EQ(model.getPreview(validDocument()), DocumentCard::Preview::Available);

        // A preview that is not a picture is a state, not a broken image.
        for (int i = 0; i < 400; i++) {
            settle();
            g_usleep(2000);
            if (model.getPreview(corruptPreviewDocument()) == DocumentCard::Preview::Corrupt) {
                break;
            }
        }
        EXPECT_EQ(model.getPreview(corruptPreviewDocument()), DocumentCard::Preview::Corrupt);
        GtkWidget* corrupt = page.getCardPreview(DashboardSection::Pinned, 1);
        ASSERT_NE(corrupt, nullptr);
        EXPECT_EQ(gtk_image_get_storage_type(GTK_IMAGE(corrupt)), GTK_IMAGE_ICON_NAME);
        const gchar* corruptIcon = nullptr;
        gtk_image_get_icon_name(GTK_IMAGE(corrupt), &corruptIcon, nullptr);
        ASSERT_NE(corruptIcon, nullptr);
        EXPECT_STREQ(corruptIcon, "dialog-warning");

        // And a document with nothing to preview says so instead of looking broken.
        for (int i = 0; i < 400; i++) {
            settle();
            g_usleep(2000);
            if (model.getPreview(unpreviewableDocument()) != DocumentCard::Preview::Unknown) {
                break;
            }
        }
        EXPECT_EQ(model.getPreview(unpreviewableDocument()), DocumentCard::Preview::None);
        EXPECT_EQ(gtk_image_get_storage_type(GTK_IMAGE(page.getCardPreview(DashboardSection::Pinned, 2))),
                  GTK_IMAGE_ICON_NAME);

        // Rebuilding the page neither re-reads a file it already knows nor loses what it knows.
        page.refresh();
        settle();
        service->waitIdle();
        EXPECT_EQ(service->pendingCount(), 0U) << "a rebuild does not leave requests behind";
        EXPECT_EQ(gtk_image_get_storage_type(GTK_IMAGE(page.getCardPreview(DashboardSection::Pinned, 0))),
                  GTK_IMAGE_PIXBUF)
                << "a preview that has been read is shown again without being read again";

        gtk_widget_destroy(window);
        settle();
        clearScratchFolder();
    }
};
TEST_F(DashboardPagePreviewTest, previewsArriveFromTheWorkerAndACorruptOneIsAStateNotABrokenImage) {}

/*
 * Plan 006, step 5: the page outlives the window it was put in, and a rebuild over the same page
 * leaves no handler behind that would call into freed memory.
 */
class DashboardPageTeardownTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);

        CriticalWatch criticals;

        Recorder recorder;
        DashboardModel model;
        model.setRecentFiles({validDocument()});
        model.setPinnedFiles({validDocument()});
        model.setRecoveryCandidates(
                {recoveryCandidate(scratchFolder("teardown") / ".notes.autosave.xopp", RecoveryValidation::Ok)});

        {
            DashboardPage page{model, recorder.callbacks(), nullptr};
            gtk_container_add(GTK_CONTAINER(window), page.getWidget());
            gtk_widget_show_all(window);
            settle();

            // A card the user clicks after a rebuild is the card of the new page, not of the old one.
            for (int i = 0; i < 5; i++) {
                model.refresh();
                page.refresh();
                settle();
            }

            g_signal_emit_by_name(page.getCardOpenButton(DashboardSection::Pinned, 0), "clicked");
            EXPECT_EQ(recorder.opened.size(), 1U) << "one click is one open, however often the page was rebuilt";

            gtk_widget_destroy(window);
            settle();
        }

        EXPECT_EQ(criticals.count(), 0U) << criticals.report();
    }
};
TEST_F(DashboardPageTeardownTest, aRebuildLeavesNoHandlerBehindAndThePageIsReleasedOnce) {}

/*
 * Plan 006, step 3: "responsive cards with a maximum content width". A card is the size its preview
 * needs whatever the window does, and it is the rows that multiply: a wide screen shows five to a
 * line and no more, a narrow one shows one, and nothing is ever squeezed below the size a preview
 * is drawn at. The measurements are taken from GTK's own layout for a given width, so they do not
 * depend on a window manager honouring a resize.
 */
class DashboardPageResponsivenessTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 1600, 900);

        Recorder recorder;
        const fs::path folder = scratchFolder("responsive");
        std::vector<fs::path> pinned;
        for (int index = 0; index < 6; index++) {
            const std::string name = "note-" + std::to_string(index) + ".xopp";
            pinned.emplace_back(documentWrittenAt(folder, name.c_str(), index + 1));
        }

        DashboardModel model;
        model.setPinnedFiles(pinned);
        model.refresh();

        DashboardPage page{model, recorder.callbacks(), nullptr};
        gtk_container_add(GTK_CONTAINER(window), page.getWidget());
        gtk_widget_show_all(window);
        settle();

        GtkWidget* content = gtk_widget_get_parent(page.getWidget());
        GtkWidget* flow = widgetNamed(content, "dashboard-flow-pinned");
        ASSERT_NE(flow, nullptr);
        ASSERT_EQ(page.getCardCount(DashboardSection::Pinned), 6U);

        // Every card shows a preview at the size a card is built around.
        GtkWidget* preview = page.getCardPreview(DashboardSection::Pinned, 0);
        ASSERT_NE(preview, nullptr);
        int previewWidth = 0;
        int previewHeight = 0;
        gtk_widget_get_size_request(preview, &previewWidth, &previewHeight);
        EXPECT_EQ(previewWidth, 168);
        EXPECT_EQ(previewHeight, 120);
        EXPECT_EQ(previewSizeOf(page, 5).first, previewWidth) << "every card is the same size, whatever its file is";

        int cardMinimum = 0;
        int cardWidth = 0;
        gtk_widget_get_preferred_width(page.getCard(DashboardSection::Pinned, 0), &cardMinimum, &cardWidth);
        EXPECT_GE(cardWidth, previewWidth) << "a card is at least as wide as the preview it shows";

        // Five to a line at most: the content has a maximum width instead of stretching across a
        // wide screen, however many cards there are.
        int flowMinimum = 0;
        int flowNatural = 0;
        gtk_widget_get_preferred_width(flow, &flowMinimum, &flowNatural);
        EXPECT_LE(flowNatural, 5 * (cardWidth + 14) + 14) << "a wide screen does not make an endless row";
        EXPECT_LT(flowMinimum, 480) << "and the row still fits a narrow window";

        auto heightForWidth = [&flow](int width) {
            int minimum = 0;
            int natural = 0;
            gtk_widget_get_preferred_height_for_width(flow, width, &minimum, &natural);
            return natural;
        };

        // Six cards never become one row of six, however wide the screen is: the row is capped at
        // five, which is the dashboard's maximum content width. A narrow window then turns those
        // two rows into more rows rather than into narrower cards.
        int cardHeight = 0;
        {
            GtkRequisition preferred{};
            gtk_widget_get_preferred_size(page.getCard(DashboardSection::Pinned, 0), nullptr, &preferred);
            cardHeight = preferred.height;
        }
        ASSERT_GT(cardHeight, 0);

        const int heightOnAWideScreen = heightForWidth(1600);
        const int heightOnANarrowScreen = heightForWidth(260);
        EXPECT_GE(heightOnAWideScreen, 2 * cardHeight)
                << "the six cards are five and one on a wide screen, not one row of six";
        EXPECT_LT(heightOnAWideScreen, 3 * cardHeight) << "and a wide screen does not stack them either";
        EXPECT_GT(heightOnANarrowScreen, heightOnAWideScreen)
                << "a narrow window wraps the row rather than squeezing the cards into it";

        // And the cards keep their size at every width: what wraps is the row, not the card.
        EXPECT_EQ(previewSizeOf(page, 5).first, 168);
        gtk_window_resize(GTK_WINDOW(window), 480, 900);
        settle();
        EXPECT_EQ(previewSizeOf(page, 5).first, 168) << "a narrow window leaves the cards as they were";

        gtk_widget_destroy(window);
        settle();
        clearScratchFolder();
    }
};
TEST_F(DashboardPageResponsivenessTest, cardsKeepTheirSizeAndTheRowsMultiply) {}

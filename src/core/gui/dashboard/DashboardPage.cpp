#include "DashboardPage.h"

#include <algorithm>  // for max, min
#include <chrono>     // for system_clock
#include <cstddef>    // for size_t
#include <cstdint>    // for uint8_t
#include <map>        // for map
#include <string>     // for string
#include <utility>    // for pair
#include <vector>     // for vector

#include <atk/atk.h>                // for atk_object_set_name
#include <gdk-pixbuf/gdk-pixbuf.h>  // for gdk_pixbuf_loader_new
#include <glib-object.h>            // for g_object_unref
#include <gtk/gtk.h>                // for gtk_*

#include "gui/dashboard/DashboardCardLabels.h"  // for buildAccessibleName
#include "util/i18n.h"                          // for _
#include "util/raii/GObjectSPtr.h"              // for GObjectSPtr

#include "filesystem.h"  // for path

using namespace xoj::dashboard;
using xoj::dashboardcard::buildAccessibleName;
using xoj::dashboardcard::buildMetadata;
using xoj::dashboardcard::buildRecoveryAccessibleName;
using xoj::dashboardcard::buildRecoveryMetadata;
using xoj::dashboardcard::buildTitle;

namespace {

/// The size a card's preview is shown at, which is what a card is sized from.
constexpr int PREVIEW_WIDTH = 168;
constexpr int PREVIEW_HEIGHT = 120;

auto now() -> std::chrono::system_clock::time_point { return std::chrono::system_clock::now(); }

/// The key a path is filed under: the canonical path, so a card is found whatever route reached it.
auto canonicalKey(const fs::path& path) -> std::string { return DashboardModel::canonicalize(path).string(); }

void addStyleClass(GtkWidget* widget, const char* styleClass) {
    gtk_style_context_add_class(gtk_widget_get_style_context(widget), styleClass);
}

void setAccessibleText(GtkWidget* widget, const std::string& text) {
    atk_object_set_name(gtk_widget_get_accessible(widget), text.c_str());
}

auto makeLabel(const std::string& text, const char* styleClass) -> GtkWidget* {
    GtkWidget* label = gtk_label_new(text.c_str());
    gtk_label_set_xalign(GTK_LABEL(label), 0.0);
    if (styleClass != nullptr) {
        addStyleClass(label, styleClass);
    }
    return label;
}

/// Remove everything a container holds, so a rebuild leaves nothing behind.
void clearContainer(GtkWidget* container) {
    if (container == nullptr) {
        return;
    }
    GList* children = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList* child = children; child != nullptr; child = child->next) {
        gtk_widget_destroy(GTK_WIDGET(child->data));
    }
    g_list_free(children);
}

auto containerChildren(GtkWidget* container) -> std::vector<GtkWidget*> {
    std::vector<GtkWidget*> widgets;
    if (container == nullptr) {
        return widgets;
    }
    GList* children = gtk_container_get_children(GTK_CONTAINER(container));
    for (GList* child = children; child != nullptr; child = child->next) {
        widgets.push_back(GTK_WIDGET(child->data));
    }
    g_list_free(children);
    return widgets;
}

/**
 * A preview's bytes as something the dashboard can show, scaled down to a card.
 *
 * @return a new pixbuf, or nullptr when the bytes are not an image - a document whose stored
 *         preview is not a picture is reported as a corrupt preview rather than shown as a
 *         broken image.
 */
auto decodePreview(const std::vector<std::uint8_t>& bytes) -> GdkPixbuf* {
    if (bytes.empty()) {
        return nullptr;
    }

    GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
    GError* error = nullptr;
    bool written = gdk_pixbuf_loader_write(loader, bytes.data(), bytes.size(), &error) != FALSE;
    if (written && error == nullptr) {
        written = gdk_pixbuf_loader_close(loader, &error) != FALSE;
    }

    GdkPixbuf* scaled = nullptr;
    if (written && error == nullptr) {
        if (GdkPixbuf* loaded = gdk_pixbuf_loader_get_pixbuf(loader); loaded != nullptr) {
            const int width = gdk_pixbuf_get_width(loaded);
            const int height = gdk_pixbuf_get_height(loaded);
            if (width > 0 && height > 0) {
                const double scale = std::min(static_cast<double>(PREVIEW_WIDTH) / width,
                                              static_cast<double>(PREVIEW_HEIGHT) / height);
                const int scaledWidth = std::max(1, static_cast<int>(width * scale));
                const int scaledHeight = std::max(1, static_cast<int>(height * scale));
                scaled = gdk_pixbuf_scale_simple(loaded, scaledWidth, scaledHeight, GDK_INTERP_BILINEAR);
            }
        }
    }

    if (error != nullptr) {
        g_error_free(error);
    }
    g_object_unref(loader);
    return scaled;
}

}  // namespace

DashboardPage::DashboardPage(DashboardModel& model, Callbacks callbacks, ThumbnailService* thumbnails):
        model(model), callbacks(std::move(callbacks)), thumbnails(thumbnails) {
    xoj::util::WidgetSPtr scroller(gtk_scrolled_window_new(nullptr, nullptr), xoj::util::adopt);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroller.get()), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    addStyleClass(scroller.get(), "xoj-dashboard");

    xoj::util::WidgetSPtr contentBox(gtk_box_new(GTK_ORIENTATION_VERTICAL, 20), xoj::util::adopt);
    gtk_widget_set_margin_start(contentBox.get(), 28);
    gtk_widget_set_margin_end(contentBox.get(), 28);
    gtk_widget_set_margin_top(contentBox.get(), 24);
    gtk_widget_set_margin_bottom(contentBox.get(), 32);
    // The dashboard grows from the top of its own surface and never wider than the cards need: on a
    // wide screen the sections stay centred rather than stretching to the window's edges.
    gtk_widget_set_halign(contentBox.get(), GTK_ALIGN_CENTER);
    gtk_widget_set_vexpand(contentBox.get(), TRUE);
    addStyleClass(contentBox.get(), "xoj-dashboard-content");
    this->content = contentBox.get();
    this->contentHolder = contentBox;

    gtk_container_add(GTK_CONTAINER(scroller.get()), this->content);
    this->page = std::move(scroller);

    // Built once here and rebuilt on every refresh, so the page a caller gets is never an empty
    // shell it has to know to fill in.
    refresh();
}

DashboardPage::~DashboardPage() {
    /*
     * The page may go away while an answer it asked for is still on its way: the worker may have
     * posted it already, in which case cancelling by id is the only way to reach it, and that is
     * what happens here, before anything of the page is taken down. `alive` is the other half: an
     * answer that is being handed over while the page goes away is dropped by its own callback
     * rather than run against a page that is gone.
     */
    this->alive->store(false);
    if (this->thumbnails != nullptr) {
        for (const auto& pending: this->pendingRequests) {
            this->thumbnails->cancel(pending.second);
        }
    }
    this->pendingRequests.clear();

    // Destroy the widgets before the data their handlers point at: the page may outlive the window
    // that put it in a stack, and a button that outlives the page would call into freed memory.
    clearContainer(this->content);
    for (ItemData* item: this->itemData) {
        delete item;
    }
    this->itemData.clear();
}

auto DashboardPage::getWidget() const -> GtkWidget* { return this->page.get(); }

void DashboardPage::setActive(bool active) {
    if (this->active == active) {
        return;
    }
    this->active = active;

    if (active) {
        /*
         * Becoming the surface the user looks at is also the moment the previews of what it shows
         * are asked for. The page is rebuilt from the model before it is shown, so its cards exist
         * by now and this is the first chance to read them; asking from the rebuild alone would
         * leave the first visit to the dashboard showing placeholders until something else happened
         * to rebuild the page.
         */
        requestPreviews();
        return;
    }

    /*
     * Off screen nothing is read and nothing that was asked for is still wanted: what is still out
     * is cancelled, so an answer is never handed to cards the user has left behind. The reading
     * itself is still finished and cached, so coming back costs no second read.
     */
    if (this->thumbnails != nullptr) {
        for (const auto& pending: this->pendingRequests) {
            this->thumbnails->cancel(pending.second);
        }
    }
    this->pendingRequests.clear();
}
auto DashboardPage::isActive() const -> bool { return this->active; }

void DashboardPage::setOpenDocument(const std::string& name) {
    this->openDocumentName = name;

    if (name.empty()) {
        gtk_widget_hide(this->backButton);
        gtk_label_set_text(GTK_LABEL(this->documentLabel), "");
        gtk_widget_hide(this->documentLabel);
        return;
    }

    gtk_label_set_text(GTK_LABEL(this->documentLabel), FS(_F("Your document: {1}") % name).c_str());
    gtk_widget_show(this->documentLabel);
    gtk_widget_show(this->backButton);
}

void DashboardPage::clearContent() {
    clearContainer(this->content);

    // The header goes too, and it is rebuilt with the sections: the page is a view of the model and
    // keeps nothing of its own, which is what makes a rebuild complete rather than a patch-up.
    this->header = nullptr;
    this->title = nullptr;
    this->documentLabel = nullptr;

    this->shownSections.clear();
    this->sectionWidgets.clear();
    this->sectionTitles.clear();
    this->sectionHints.clear();
    this->sectionFlows.clear();
    this->buttons.clear();
    this->cards.clear();
    this->cardsByPath.clear();
    this->recoveryCards.clear();
    this->folderRows.clear();
    this->menus.clear();

    this->pendingRequests.clear();

    for (ItemData* item: this->itemData) {
        delete item;
    }
    this->itemData.clear();
}

void DashboardPage::buildHeader() {
    GtkWidget* header = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    addStyleClass(header, "xoj-dashboard-header");

    this->title = makeLabel(_("Home"), "xoj-dashboard-title");
    atk_object_set_role(gtk_widget_get_accessible(this->title), ATK_ROLE_HEADING);
    gtk_box_pack_start(GTK_BOX(header), this->title, FALSE, FALSE, 0);

    this->documentLabel = makeLabel({}, "xoj-dashboard-document");
    gtk_widget_set_no_show_all(this->documentLabel, TRUE);
    gtk_box_pack_start(GTK_BOX(header), this->documentLabel, TRUE, TRUE, 0);

    this->backButton = gtk_button_new_with_label(_("Back to the document"));
    addStyleClass(this->backButton, "xoj-dashboard-back");
    setAccessibleText(this->backButton, _("Back to the document in the editor, with your place kept"));
    gtk_widget_set_tooltip_text(this->backButton, _("Go back to the document you have open, with your place kept"));
    gtk_widget_set_no_show_all(this->backButton, TRUE);
    this->buttons["back"] = this->backButton;
    ItemData* back = makeItem(Action::Back, {});
    g_signal_connect(this->backButton, "clicked", G_CALLBACK(onMenuAction), back);
    gtk_box_pack_end(GTK_BOX(header), this->backButton, FALSE, FALSE, 0);

    this->header = header;
    gtk_box_pack_start(GTK_BOX(this->content), header, FALSE, FALSE, 0);
}

void DashboardPage::refresh() {
    // A rebuild starts from nothing: the page keeps no card of its own, so what is on screen after
    // a refresh is exactly what the model says now.
    if (this->thumbnails != nullptr) {
        this->thumbnails->cancelAll();
    }
    clearContent();
    buildHeader();
    buildSections();

    gtk_widget_show_all(this->page.get());
    // A menu is popped up at a card, never shown in place: showing the page must not leave a row of
    // menus lying around.
    for (GtkWidget* menu: this->menus) {
        gtk_widget_hide(menu);
    }

    if (!this->openDocumentName.empty()) {
        setOpenDocument(this->openDocumentName);
    }

    // A preview is kept for the cards that are on screen and dropped with the cards that are gone,
    // so what the page remembers is what it shows.
    std::vector<std::string> forgotten;
    for (const auto& [key, pixbuf]: this->previewPixbufs) {
        if (this->cardsByPath.count(key) == 0) {
            forgotten.push_back(key);
        }
    }
    for (const std::string& key: forgotten) {
        g_object_unref(this->previewPixbufs[key]);
        this->previewPixbufs.erase(key);
        this->previewBytes.erase(key);
    }

    requestPreviews();
}

auto DashboardPage::buildSection(DashboardSection section) -> GtkWidget* {
    const std::string key = sectionKey(section);
    const std::string title = sectionTitle(section);

    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_name(box, ("dashboard-section-" + key).c_str());
    atk_object_set_role(gtk_widget_get_accessible(box), ATK_ROLE_SECTION);
    atk_object_set_name(gtk_widget_get_accessible(box), title.c_str());
    addStyleClass(box, "xoj-dashboard-section");

    GtkWidget* heading = makeLabel(title, "xoj-dashboard-section-title");
    atk_object_set_role(gtk_widget_get_accessible(heading), ATK_ROLE_HEADING);
    gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 0);

    // What an empty section says, so an empty dashboard explains the next action instead of being
    // blank. Shown only when the section has nothing.
    GtkWidget* hint = makeLabel(sectionHint(section), "xoj-dashboard-section-hint");
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_widget_set_name(hint, ("dashboard-hint-" + key).c_str());
    gtk_widget_set_no_show_all(hint, TRUE);
    gtk_widget_hide(hint);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_box_pack_start(GTK_BOX(box), body, FALSE, FALSE, 0);

    GtkWidget* flow = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow), GTK_SELECTION_NONE);
    gtk_flow_box_set_homogeneous(GTK_FLOW_BOX(flow), TRUE);
    gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow), 1);
    gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow), 5);
    gtk_flow_box_set_row_spacing(GTK_FLOW_BOX(flow), 14);
    gtk_flow_box_set_column_spacing(GTK_FLOW_BOX(flow), 14);
    gtk_widget_set_halign(flow, GTK_ALIGN_START);
    gtk_widget_set_name(flow, ("dashboard-flow-" + key).c_str());
    atk_object_set_role(gtk_widget_get_accessible(flow), ATK_ROLE_LIST);
    atk_object_set_name(gtk_widget_get_accessible(flow), title.c_str());
    addStyleClass(flow, "xoj-dashboard-flow");
    // Hidden until it has cards, and hidden from show-all too: an empty section shows its hint
    // rather than an empty row.
    gtk_widget_set_no_show_all(flow, TRUE);
    gtk_widget_hide(flow);
    gtk_box_pack_start(GTK_BOX(box), flow, FALSE, FALSE, 0);

    this->sectionWidgets[key] = box;
    this->sectionTitles[key] = heading;
    this->sectionHints[key] = hint;
    this->sectionBodies[key] = body;
    this->sectionFlows[key] = flow;

    gtk_box_pack_start(GTK_BOX(this->content), box, FALSE, FALSE, 0);
    return box;
}

void DashboardPage::buildSections() {
    for (DashboardSection section: this->model.sections()) {
        const std::string key = sectionKey(section);
        this->shownSections.push_back(section);
        buildSection(section);

        GtkWidget* body = this->sectionBodies[key];
        GtkWidget* flow = this->sectionFlows[key];

        switch (section) {
            case DashboardSection::Templates:
                buildTemplates(body);
                break;
            case DashboardSection::Library:
                buildFolders(body);
                buildCards(section, flow);
                break;
            case DashboardSection::Recovery:
                buildRecoveryCards(flow);
                break;
            case DashboardSection::ContinueWorking:
            case DashboardSection::Pinned:
                buildCards(section, flow);
                break;
        }

        // A section shows either its cards or what to do to get some.
        const bool hasCards = getCardCount(section) > 0;
        const bool hasExtras = !containerChildren(body).empty();
        if (hasCards) {
            gtk_widget_set_no_show_all(flow, FALSE);
            gtk_widget_show(flow);
        } else {
            gtk_widget_hide(flow);
        }
        if (!hasCards && !hasExtras) {
            gtk_widget_show(this->sectionHints[key]);
        }
    }
}

auto DashboardPage::makeItem(Action action, const fs::path& path, bool flag, const RecoveryCard& recovery)
        -> ItemData* {
    auto* data = new ItemData{this, action, path, recovery, flag};
    this->itemData.push_back(data);
    return data;
}

void DashboardPage::buildCards(DashboardSection section, GtkWidget* flow) {
    const std::string key = sectionKey(section);
    std::vector<CardWidgets> built;

    for (const DocumentCard& card: this->model.cardsIn(section)) {
        built.push_back(buildDocumentCard(card));
        CardWidgets& widgets = built.back();

        const int index = static_cast<int>(built.size()) - 1;
        gtk_flow_box_insert(GTK_FLOW_BOX(flow), widgets.root, -1);

        // The flow box wraps what it is given in a child of its own, and that child is what the
        // card is seen as: it carries the card's style and its state.
        GtkWidget* child = GTK_WIDGET(gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), index));
        if (child != nullptr) {
            widgets.root = child;
            addStyleClass(child, "xoj-dashboard-card-child");
            gtk_widget_set_name(child, ("dashboard-card-" + key).c_str());
            if (!card.openable()) {
                addStyleClass(child, "xoj-dashboard-card-unavailable");
            }
            if (card.pinned) {
                addStyleClass(child, "xoj-dashboard-card-pinned");
            }
        }
    }

    this->cards[key] = std::move(built);

    // The pointers a preview finds its cards through are into the section's vector: it was filled
    // once above and is never resized again, so they hold until the next rebuild.
    for (CardWidgets& widgets: this->cards[key]) {
        this->cardsByPath[canonicalKey(widgets.path)].push_back(&widgets);
    }
}

auto DashboardPage::buildDocumentCard(const DocumentCard& card) -> CardWidgets {
    CardWidgets widgets;
    widgets.path = card.path;

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    addStyleClass(root, "xoj-dashboard-card");
    gtk_widget_set_size_request(root, PREVIEW_WIDTH + 16, -1);

    // The card's body is the button that opens the file, exactly the way the file manager does.
    GtkWidget* open = gtk_button_new();
    widgets.open = open;
    gtk_button_set_relief(GTK_BUTTON(open), GTK_RELIEF_NONE);
    addStyleClass(open, "xoj-dashboard-card-open");
    setAccessibleText(open, buildAccessibleName(card, now()));
    gtk_widget_set_tooltip_text(open, card.openable() ? _("Open this note") : _("This note cannot be opened"));
    gtk_widget_set_sensitive(open, card.openable());
    g_signal_connect(open, "clicked", G_CALLBACK(onCardOpened), makeItem(Action::Open, card.path));

    GtkWidget* inner = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget* preview = gtk_image_new();
    widgets.preview = preview;
    gtk_widget_set_size_request(preview, PREVIEW_WIDTH, PREVIEW_HEIGHT);
    addStyleClass(preview, "xoj-dashboard-card-preview");
    gtk_box_pack_start(GTK_BOX(inner), preview, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(open), inner);

    GtkWidget* title = makeLabel(buildTitle(card), "xoj-dashboard-card-title");
    gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);
    gtk_label_set_max_width_chars(GTK_LABEL(title), 24);
    widgets.title = title;

    GtkWidget* metadata = makeLabel(buildMetadata(card, now()), "xoj-dashboard-card-metadata");
    gtk_label_set_ellipsize(GTK_LABEL(metadata), PANGO_ELLIPSIZE_END);
    gtk_label_set_max_width_chars(GTK_LABEL(metadata), 24);
    if (!card.openable()) {
        addStyleClass(metadata, "xoj-dashboard-card-state");
    }
    widgets.metadata = metadata;

    gtk_box_pack_start(GTK_BOX(root), open, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), title, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), metadata, FALSE, FALSE, 0);

    GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    addStyleClass(actions, "xoj-dashboard-card-actions");

    // Pinning is a plain, visible toggle: it is the one thing about a card the user changes here.
    GtkWidget* pin = gtk_toggle_button_new_with_label(card.pinned ? _("Unpin") : _("Pin"));
    widgets.pin = pin;
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(pin), card.pinned);
    addStyleClass(pin, "xoj-dashboard-card-pin");
    gtk_widget_set_tooltip_text(pin, _("Keep this note on the dashboard, in this order"));
    setAccessibleText(pin, card.pinned ? FS(_F("Unpin {1}") % card.displayName) :
                                         FS(_F("Pin {1} to the dashboard") % card.displayName));
    g_signal_connect(pin, "toggled", G_CALLBACK(onPinToggled), makeItem(Action::TogglePin, card.path, !card.pinned));
    gtk_box_pack_start(GTK_BOX(actions), pin, FALSE, FALSE, 0);

    GtkWidget* menu = gtk_menu_new();
    auto addMenuItem = [&](const std::string& label, Action action, bool sensitive, bool flag) -> GtkWidget* {
        GtkWidget* item = gtk_menu_item_new_with_label(label.c_str());
        gtk_widget_set_sensitive(item, sensitive);
        g_signal_connect(item, "activate", G_CALLBACK(onMenuAction), makeItem(action, card.path, flag));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        return item;
    };

    widgets.openItem = addMenuItem(_("Open"), Action::Open, card.openable(), false);
    widgets.pinItem = addMenuItem(card.pinned ? _("Unpin") : _("Pin"), Action::TogglePin, true, !card.pinned);
    if (card.location == DocumentCard::Location::Missing || card.location == DocumentCard::Location::NotARegularFile) {
        widgets.locateItem = addMenuItem(_("Find this file\u2026"), Action::Locate, true, false);
    }
    if (card.has(DashboardSection::ContinueWorking) || card.has(DashboardSection::Pinned)) {
        widgets.forgetItem = addMenuItem(_("Remove from the dashboard"), Action::Forget, true, false);
    }
    gtk_widget_show_all(menu);

    GtkWidget* menuButton = gtk_menu_button_new();
    widgets.menuButton = menuButton;
    addStyleClass(menuButton, "xoj-dashboard-card-menu");
    gtk_button_set_image(GTK_BUTTON(menuButton),
                         gtk_image_new_from_icon_name("view-more-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(menuButton), menu);
    gtk_widget_set_tooltip_text(menuButton, _("More actions for this note"));
    setAccessibleText(menuButton, FS(_F("More actions for {1}") % card.displayName));
    gtk_box_pack_start(GTK_BOX(actions), menuButton, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(root), actions, FALSE, FALSE, 0);

    this->menus.push_back(menu);
    // A right-click on the card opens the same menu, so the actions are where a user looks for them.
    g_object_set_data(G_OBJECT(root), "dashboard-menu", menu);
    g_signal_connect(root, "button-press-event", G_CALLBACK(onCardButtonPressed), nullptr);

    applyPreview(preview, card);
    widgets.root = root;
    return widgets;
}

namespace {

/// How a folder that is not simply a readable folder is described, empty when there is nothing to say.
auto folderStateText(LibraryFolder::State state) -> std::string {
    switch (state) {
        case LibraryFolder::State::Unknown:
        case LibraryFolder::State::Ok:
            return {};
        case LibraryFolder::State::Missing:
            return _("this folder is not there any more");
        case LibraryFolder::State::NotADirectory:
            return _("this is not a folder");
        case LibraryFolder::State::Inaccessible:
            return _("this folder cannot be listed");
    }
    return {};
}

}  // namespace

void DashboardPage::buildFolders(GtkWidget* box) {
    for (const LibraryFolder& folder: this->model.getLibraryFolders()) {
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        addStyleClass(row, "xoj-dashboard-folder");
        gtk_widget_set_name(row, ("dashboard-folder-row-" + folder.path.filename().string()).c_str());

        std::map<std::string, GtkWidget*> parts;
        GtkWidget* name = makeLabel(folder.displayName.empty() ? folder.path.string() : folder.displayName,
                                    "xoj-dashboard-folder-name");
        gtk_widget_set_tooltip_text(name, folder.path.string().c_str());
        parts["name"] = name;
        gtk_box_pack_start(GTK_BOX(row), name, TRUE, TRUE, 0);

        const std::string state = folderStateText(folder.state);
        if (!state.empty()) {
            GtkWidget* note = makeLabel(state, "xoj-dashboard-folder-state");
            parts["state"] = note;
            gtk_box_pack_start(GTK_BOX(row), note, FALSE, FALSE, 0);
        }

        // Listing a folder's subtree is the one thing about a folder that is off by default: a
        // folder that is deep, or on a slow drive, is only walked when the user asks for it.
        GtkWidget* recursive = gtk_check_button_new_with_label(_("Include subfolders"));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(recursive), folder.recursive);
        gtk_widget_set_sensitive(recursive, folder.listable());
        gtk_widget_set_tooltip_text(recursive,
                                    _("List the notes in this folder's subfolders too, up to a few levels deep"));
        parts["recursive"] = recursive;
        g_signal_connect(recursive, "toggled", G_CALLBACK(onMenuAction),
                         makeItem(Action::ToggleRecursive, folder.path, folder.recursive));
        gtk_box_pack_start(GTK_BOX(row), recursive, FALSE, FALSE, 0);

        // Stopping here changes the dashboard and nothing else: no folder is ever moved or emptied.
        GtkWidget* remove = gtk_button_new_with_label(_("Stop listing"));
        addStyleClass(remove, "xoj-dashboard-folder-remove");
        setAccessibleText(remove, FS(_F("Stop listing {1} on the dashboard") % folder.path.filename().string()));
        gtk_widget_set_tooltip_text(remove, _("Take this folder off the dashboard. Nothing in it is changed."));
        parts["remove"] = remove;
        g_signal_connect(remove, "clicked", G_CALLBACK(onMenuAction), makeItem(Action::RemoveFolder, folder.path));
        gtk_box_pack_end(GTK_BOX(row), remove, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
        this->folderRows.emplace_back(folder.path, std::move(parts));
    }
}

void DashboardPage::buildRecoveryCards(GtkWidget* flow) {
    for (const RecoveryCard& card: this->model.getRecoveryCards()) {
        RecoveryWidgets widgets;
        const std::string name = card.displayName.empty() ? card.recoveryPath.filename().string() : card.displayName;

        GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        addStyleClass(root, "xoj-dashboard-recovery-card");
        gtk_widget_set_size_request(root, PREVIEW_WIDTH + 60, -1);

        GtkWidget* title = makeLabel(name, "xoj-dashboard-recovery-title");
        gtk_label_set_ellipsize(GTK_LABEL(title), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_pack_start(GTK_BOX(root), title, FALSE, FALSE, 0);

        GtkWidget* metadata = makeLabel(buildRecoveryMetadata(card, now()), "xoj-dashboard-recovery-metadata");
        gtk_label_set_line_wrap(GTK_LABEL(metadata), TRUE);
        gtk_label_set_max_width_chars(GTK_LABEL(metadata), 32);
        gtk_box_pack_start(GTK_BOX(root), metadata, FALSE, FALSE, 0);

        GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
        addStyleClass(actions, "xoj-dashboard-recovery-actions");

        GtkWidget* open = gtk_button_new_with_label(_("Open the copy"));
        widgets.open = open;
        setAccessibleText(open, buildRecoveryAccessibleName(card, now()));
        gtk_widget_set_sensitive(open, card.openable());
        gtk_widget_set_tooltip_text(open, card.openable() ? _("Read the recovered work without touching the original") :
                                                            _("This file cannot be opened as a note"));
        g_signal_connect(open, "clicked", G_CALLBACK(onMenuAction), makeItem(Action::OpenRecovery, {}, false, card));
        gtk_box_pack_start(GTK_BOX(actions), open, FALSE, FALSE, 0);

        // Whatever the user does with a recovered copy, the file it came from is only ever read.
        GtkWidget* saveAs = gtk_button_new_with_label(_("Save as\u2026"));
        widgets.saveAs = saveAs;
        gtk_widget_set_sensitive(saveAs, card.openable());
        gtk_widget_set_tooltip_text(saveAs, _("Write the recovered work to a file of your choosing"));
        g_signal_connect(saveAs, "clicked", G_CALLBACK(onMenuAction),
                         makeItem(Action::SaveRecoveryAs, {}, false, card));
        gtk_box_pack_start(GTK_BOX(actions), saveAs, FALSE, FALSE, 0);

        GtkWidget* reveal = gtk_button_new_with_label(_("Show in folder"));
        widgets.reveal = reveal;
        gtk_widget_set_tooltip_text(reveal, _("Show where this copy is kept"));
        g_signal_connect(reveal, "clicked", G_CALLBACK(onMenuAction),
                         makeItem(Action::RevealRecovery, {}, false, card));
        gtk_box_pack_start(GTK_BOX(actions), reveal, FALSE, FALSE, 0);

        GtkWidget* remove = gtk_button_new_with_label(_("Delete\u2026"));
        widgets.remove = remove;
        addStyleClass(remove, "xoj-dashboard-recovery-delete");
        setAccessibleText(remove, FS(_F("Delete the recovered copy {1}") % name));
        gtk_widget_set_tooltip_text(remove, _("Delete this copy. The document it came from is not touched."));
        g_signal_connect(remove, "clicked", G_CALLBACK(onMenuAction),
                         makeItem(Action::DeleteRecovery, {}, false, card));
        gtk_box_pack_start(GTK_BOX(actions), remove, FALSE, FALSE, 0);

        gtk_box_pack_start(GTK_BOX(root), actions, FALSE, FALSE, 0);
        gtk_flow_box_insert(GTK_FLOW_BOX(flow), root, -1);
        this->recoveryCards.push_back(widgets);
    }
}

void DashboardPage::buildTemplates(GtkWidget* box) {
    struct ActionButton {
        const char* key;
        const char* label;
        const char* tooltip;
        Action action;
    };

    const ActionButton entries[] = {
            {"new-note", _("New note"), _("Start a blank note"), Action::NewNote},
            {"open-file", _("Open file"), _("Open a note or a PDF you already have"), Action::OpenFile},
            {"quick-note", _("Quick note"), _("Write something down without choosing a file first"), Action::QuickNote},
            {"annotate-pdf", _("Annotate PDF"), _("Open a PDF to write on"), Action::AnnotatePdf},
            {"add-folder", _("Add folder"), _("List the notes in a folder on this page"), Action::AddFolder},
    };

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    addStyleClass(row, "xoj-dashboard-templates");
    for (const ActionButton& entry: entries) {
        GtkWidget* button = gtk_button_new_with_label(entry.label);
        addStyleClass(button, "xoj-dashboard-template");
        gtk_widget_set_name(button, entry.key);
        setAccessibleText(button, entry.label);
        gtk_widget_set_tooltip_text(button, entry.tooltip);
        g_signal_connect(button, "clicked", G_CALLBACK(onMenuAction), makeItem(entry.action, {}));
        this->buttons[entry.key] = button;
        gtk_box_pack_start(GTK_BOX(row), button, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);
}

void DashboardPage::applyPreview(GtkWidget* image, const DocumentCard& card) {
    GtkStyleContext* style = gtk_widget_get_style_context(image);
    for (const char* name:
         {"xoj-dashboard-preview-pending", "xoj-dashboard-preview-none", "xoj-dashboard-preview-corrupt"}) {
        gtk_style_context_remove_class(style, name);
    }

    if (card.preview == DocumentCard::Preview::Available) {
        if (auto pixbuf = this->previewPixbufs.find(canonicalKey(card.path));
            pixbuf != this->previewPixbufs.end() && pixbuf->second != nullptr) {
            gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf->second);
            gtk_widget_set_tooltip_text(image, _("The first page of this note"));
            return;
        }
    }

    switch (card.preview) {
        case DocumentCard::Preview::Corrupt:
            gtk_image_set_from_icon_name(GTK_IMAGE(image), "dialog-warning", GTK_ICON_SIZE_DIALOG);
            gtk_widget_set_tooltip_text(image, _("The stored preview cannot be shown"));
            gtk_style_context_add_class(style, "xoj-dashboard-preview-corrupt");
            break;
        case DocumentCard::Preview::None:
            gtk_image_set_from_icon_name(GTK_IMAGE(image), "x-office-document", GTK_ICON_SIZE_DIALOG);
            gtk_widget_set_tooltip_text(image, _("This note has no preview"));
            gtk_style_context_add_class(style, "xoj-dashboard-preview-none");
            break;
        case DocumentCard::Preview::Unknown:
        case DocumentCard::Preview::Available:
            gtk_image_set_from_icon_name(GTK_IMAGE(image), "x-office-document", GTK_ICON_SIZE_DIALOG);
            gtk_widget_set_tooltip_text(image, _("Reading the first page\u2026"));
            gtk_style_context_add_class(style, "xoj-dashboard-preview-pending");
            break;
    }
}

void DashboardPage::requestPreviews() {
    if (this->thumbnails == nullptr || !this->active) {
        return;
    }

    for (const DocumentCard& card: this->model.getCards()) {
        if (card.preview != DocumentCard::Preview::Unknown || card.location != DocumentCard::Location::Present) {
            // A file that is not there, or one that has already answered, is not asked about again.
            continue;
        }

        const std::string key = canonicalKey(card.path);
        if (this->pendingRequests.count(key) != 0) {
            continue;
        }

        const fs::path path = card.path;
        /*
         * The callback holds the page and says so with `alive`, which the page clears as it goes
         * away. Normally this page's cancellation is what keeps an answer out - the page cancels
         * what it asks for as it is deactivated, rebuilt or destroyed, and the service reaches an
         * answer that is already on the main context with it - and the check here is what makes
         * "an answer that was already being handed over" a dropped answer rather than a call into
         * a page that is gone.
         */
        const std::shared_ptr<std::atomic_bool> alive = this->alive;
        const ThumbnailRequestId id =
                this->thumbnails->request(path, [this, alive, key, path](const ThumbnailResult& result) {
                    if (!alive->load()) {
                        return;
                    }
                    this->onPreviewReady(key, path, result);
                });
        if (id != 0) {
            this->pendingRequests[key] = id;
        }
    }
}

void DashboardPage::onPreviewReady(const std::string& key, const fs::path& path, const ThumbnailResult& result) {
    this->pendingRequests.erase(key);

    DocumentCard::Preview state = result.state;
    if (result.available()) {
        GdkPixbuf* pixbuf = decodePreview(result.png);
        if (pixbuf == nullptr) {
            // The document holds something as its preview, but not a picture: the card says so
            // rather than showing a broken image.
            state = DocumentCard::Preview::Corrupt;
        } else {
            if (auto existing = this->previewPixbufs.find(key); existing != this->previewPixbufs.end()) {
                g_object_unref(existing->second);
                existing->second = pixbuf;
            } else {
                this->previewPixbufs[key] = pixbuf;
                this->previewBytes[key] = result.png;
            }
        }
    } else {
        this->previewBytes.erase(key);
    }

    this->model.setPreview(path, state);
    updateCardsFor(path);
}

void DashboardPage::updateCardsFor(const fs::path& path) {
    const DocumentCard* card = this->model.findCard(path);
    if (card == nullptr) {
        return;
    }

    auto found = this->cardsByPath.find(canonicalKey(path));
    if (found == this->cardsByPath.end()) {
        return;
    }

    for (CardWidgets* widgets: found->second) {
        if (widgets == nullptr) {
            continue;
        }
        applyPreview(widgets->preview, *card);
        gtk_label_set_text(GTK_LABEL(widgets->metadata), buildMetadata(*card, now()).c_str());
        setAccessibleText(widgets->open, buildAccessibleName(*card, now()));
    }
}

void DashboardPage::run(const ItemData& item) {
    switch (item.action) {
        case Action::Open:
            // The one way the dashboard opens a document is the way the rest of the application
            // does: whatever "open" means elsewhere, it means the same thing here.
            if (this->callbacks.open) {
                this->callbacks.open(item.path);
            }
            break;
        case Action::TogglePin:
            if (this->callbacks.setPinned) {
                this->callbacks.setPinned(item.path, item.flag);
            }
            break;
        case Action::Locate:
            if (this->callbacks.locate) {
                this->callbacks.locate(item.path);
            }
            break;
        case Action::Forget:
            if (this->callbacks.forget) {
                this->callbacks.forget(item.path);
            }
            break;
        case Action::OpenRecovery:
            if (this->callbacks.openRecovery) {
                this->callbacks.openRecovery(item.recovery);
            }
            break;
        case Action::SaveRecoveryAs:
            if (this->callbacks.saveRecoveryAs) {
                this->callbacks.saveRecoveryAs(item.recovery);
            }
            break;
        case Action::RevealRecovery:
            if (this->callbacks.revealRecovery) {
                this->callbacks.revealRecovery(item.recovery);
            }
            break;
        case Action::DeleteRecovery: {
            if (!this->callbacks.confirm || !this->callbacks.deleteRecovery) {
                break;
            }
            // Deleting a copy is the one thing here that cannot be undone, so it is asked about and
            // it is the only thing the page asks about.
            const RecoveryCard card = item.recovery;
            const std::string name =
                    card.displayName.empty() ? card.recoveryPath.filename().string() : card.displayName;
            this->callbacks.confirm(
                    _("Delete the recovered copy?"),
                    FS(_F("{1} will be deleted. The document it came from is left exactly as it is.") % name),
                    [this, card]() {
                        if (this->callbacks.deleteRecovery) {
                            this->callbacks.deleteRecovery(card);
                        }
                    });
            break;
        }
        case Action::NewNote:
            if (this->callbacks.newNote) {
                this->callbacks.newNote();
            }
            break;
        case Action::OpenFile:
            if (this->callbacks.openFile) {
                this->callbacks.openFile();
            }
            break;
        case Action::QuickNote:
            if (this->callbacks.quickNote) {
                this->callbacks.quickNote();
            }
            break;
        case Action::AnnotatePdf:
            if (this->callbacks.annotatePdf) {
                this->callbacks.annotatePdf();
            }
            break;
        case Action::AddFolder:
            if (this->callbacks.addFolder) {
                this->callbacks.addFolder();
            }
            break;
        case Action::RemoveFolder:
            if (this->callbacks.removeFolder) {
                this->callbacks.removeFolder(item.path);
            }
            break;
        case Action::ToggleRecursive:
            if (this->callbacks.setFolderRecursive) {
                this->callbacks.setFolderRecursive(item.path, !item.flag);
            }
            break;
        case Action::Back:
            if (this->callbacks.backToDocument) {
                this->callbacks.backToDocument();
            }
            break;
    }
}

void DashboardPage::onCardOpened(GtkButton* button, gpointer data) {
    (void)button;
    auto* item = static_cast<ItemData*>(data);
    item->page->run(*item);
}

void DashboardPage::onPinToggled(GtkToggleButton* button, gpointer data) {
    auto* item = static_cast<ItemData*>(data);
    // The toggle's new state is the pin state the user asked for; what the item was built with
    // says which state that is, so the handler never has to read it back off the button.
    if (gtk_toggle_button_get_active(button) != item->flag) {
        // Back where it started: the user toggled it twice, and nothing has been asked for.
        return;
    }
    item->page->run(*item);
}

void DashboardPage::onMenuAction(GtkMenuItem* item, gpointer data) {
    (void)item;
    auto* data_ = static_cast<ItemData*>(data);
    data_->page->run(*data_);
}

void DashboardPage::onCardButtonPressed(GtkWidget* widget, GdkEventButton* event, gpointer data) {
    (void)data;
    if (event == nullptr || event->type != GDK_BUTTON_PRESS || event->button != 3) {
        return;
    }
    auto* menu = GTK_WIDGET(g_object_get_data(G_OBJECT(widget), "dashboard-menu"));
    if (menu != nullptr) {
        gtk_menu_popup_at_pointer(GTK_MENU(menu), reinterpret_cast<GdkEvent*>(event));
    }
}

/* The page as the tests see it: what it shows, not what it is made of. */

auto DashboardPage::getSections() const -> std::vector<DashboardSection> { return this->shownSections; }

auto DashboardPage::getSectionWidget(DashboardSection section) const -> GtkWidget* {
    const auto found = this->sectionWidgets.find(sectionKey(section));
    return found == this->sectionWidgets.end() ? nullptr : found->second;
}

auto DashboardPage::getSectionTitle(DashboardSection section) const -> std::string {
    const auto found = this->sectionTitles.find(sectionKey(section));
    if (found == this->sectionTitles.end() || found->second == nullptr) {
        return {};
    }
    return gtk_label_get_text(GTK_LABEL(found->second));
}

auto DashboardPage::getSectionHint(DashboardSection section) const -> std::string {
    const auto found = this->sectionHints.find(sectionKey(section));
    if (found == this->sectionHints.end() || found->second == nullptr) {
        return {};
    }
    if (!gtk_widget_get_visible(found->second)) {
        return {};
    }
    return gtk_label_get_text(GTK_LABEL(found->second));
}

auto DashboardPage::getCardCount(DashboardSection section) const -> std::size_t {
    const auto found = this->cards.find(sectionKey(section));
    return found == this->cards.end() ? 0 : found->second.size();
}

auto DashboardPage::getCard(DashboardSection section, std::size_t index) const -> GtkWidget* {
    const auto found = this->cards.find(sectionKey(section));
    if (found == this->cards.end() || index >= found->second.size()) {
        return nullptr;
    }
    return found->second[index].root;
}

auto DashboardPage::getCardOpenButton(DashboardSection section, std::size_t index) const -> GtkWidget* {
    const auto found = this->cards.find(sectionKey(section));
    if (found == this->cards.end() || index >= found->second.size()) {
        return nullptr;
    }
    return found->second[index].open;
}

auto DashboardPage::getCardPreview(DashboardSection section, std::size_t index) const -> GtkWidget* {
    const auto found = this->cards.find(sectionKey(section));
    if (found == this->cards.end() || index >= found->second.size()) {
        return nullptr;
    }
    return found->second[index].preview;
}

auto DashboardPage::getCardPinButton(DashboardSection section, std::size_t index) const -> GtkWidget* {
    const auto found = this->cards.find(sectionKey(section));
    if (found == this->cards.end() || index >= found->second.size()) {
        return nullptr;
    }
    return found->second[index].pin;
}

auto DashboardPage::getCardMenuItem(DashboardSection section, std::size_t index, const char* action) const
        -> GtkWidget* {
    const auto found = this->cards.find(sectionKey(section));
    if (found == this->cards.end() || index >= found->second.size()) {
        return nullptr;
    }
    const CardWidgets& widgets = found->second[index];
    const std::string name = action == nullptr ? "" : action;
    if (name == "open") {
        return widgets.openItem;
    }
    if (name == "pin") {
        return widgets.pinItem;
    }
    if (name == "locate") {
        return widgets.locateItem;
    }
    if (name == "forget") {
        return widgets.forgetItem;
    }
    return nullptr;
}

auto DashboardPage::getCardPath(DashboardSection section, std::size_t index) const -> fs::path {
    const auto found = this->cards.find(sectionKey(section));
    if (found == this->cards.end() || index >= found->second.size()) {
        return {};
    }
    return found->second[index].path;
}

auto DashboardPage::getButton(const char* action) const -> GtkWidget* {
    const auto found = this->buttons.find(action == nullptr ? "" : action);
    return found == this->buttons.end() ? nullptr : found->second;
}

auto DashboardPage::getRecoveryCount() const -> std::size_t { return this->recoveryCards.size(); }

auto DashboardPage::getRecoveryButton(std::size_t index, const char* action) const -> GtkWidget* {
    if (index >= this->recoveryCards.size()) {
        return nullptr;
    }
    const RecoveryWidgets& widgets = this->recoveryCards[index];
    const std::string name = action == nullptr ? "" : action;
    if (name == "open") {
        return widgets.open;
    }
    if (name == "save-as") {
        return widgets.saveAs;
    }
    if (name == "reveal") {
        return widgets.reveal;
    }
    if (name == "delete") {
        return widgets.remove;
    }
    return nullptr;
}

auto DashboardPage::getFolderRowCount() const -> std::size_t { return this->folderRows.size(); }

auto DashboardPage::getFolderRow(const fs::path& folder, const char* part) const -> GtkWidget* {
    for (const auto& [path, parts]: this->folderRows) {
        if (path != folder) {
            continue;
        }
        const auto found = parts.find(part == nullptr ? "" : part);
        return found == parts.end() ? nullptr : found->second;
    }
    return nullptr;
}

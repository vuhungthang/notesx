#include "SafetyStatusBar.h"

#include <algorithm>  // for min
#include <chrono>     // for milliseconds
#include <ctime>      // for strftime, localtime_r
#include <utility>    // for move

#include "util/i18n.h"  // for _, _F, FS

using namespace xoj::safety;

namespace {

/// Every semantic class the row may carry, so exactly one of them is ever applied.
constexpr const char* STATE_CSS_CLASSES[] = {"xoj-status-neutral", "xoj-status-success", "xoj-status-warning",
                                             "xoj-status-error"};

/// A time as the user's locale writes it, for a tooltip or a details view.
auto formatTime(TimePoint time) -> std::string {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[64] = {};
    if (std::strftime(buffer, sizeof(buffer), "%X", &local) == 0) {
        return {};
    }
    return buffer;
}

/// `label: value`, or an empty string when there is no value yet.
auto detailLine(const char* label, const std::optional<TimePoint>& time, const fs::path& path) -> std::string {
    if (!time.has_value()) {
        return {};
    }
    if (path.empty()) {
        return FS(_F("{1}: {2}") % _(label) % formatTime(*time)) + "\n";
    }
    return FS(_F("{1}: {2} at {3}") % _(label) % path.u8string() % formatTime(*time)) + "\n";
}

}  // namespace

SafetyStatusBar::SafetyStatusBar(Callbacks callbacks): callbacks(std::move(callbacks)) {
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    this->row.reset(box, xoj::util::ref);
    gtk_style_context_add_class(gtk_widget_get_style_context(box), "xoj-safety-status");
    // A reader who cannot see the row change still needs to be told what it says.
    atk_object_set_role(gtk_widget_get_accessible(box), ATK_ROLE_STATUSBAR);

    this->icon = GTK_IMAGE(gtk_image_new());
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(this->icon)), "xoj-safety-status-icon");

    this->label = GTK_LABEL(gtk_label_new(nullptr));
    gtk_label_set_ellipsize(this->label, PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(this->label, TRUE);
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(this->label)), "xoj-safety-status-text");

    /*
     * The banner is where a failure becomes something the user can act on without a modal dialog
     * interrupting them. It slides in beside the state instead of above it, so revealing it never
     * changes the height of the row the canvas is laid out under.
     */
    GtkWidget* banner = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_style_context_add_class(gtk_widget_get_style_context(banner), "xoj-safety-status-banner");

    this->bannerLabel = GTK_LABEL(gtk_label_new(nullptr));
    gtk_label_set_ellipsize(this->bannerLabel, PANGO_ELLIPSIZE_END);
    gtk_label_set_single_line_mode(this->bannerLabel, TRUE);
    gtk_label_set_max_width_chars(this->bannerLabel, 60);

    this->retryButton = GTK_BUTTON(gtk_button_new_with_label(_("Retry")));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(this->retryButton)), "xoj-safety-status-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(this->retryButton)), "xoj-focus-ring");
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(this->retryButton)), _("Retry the failed operation"));

    gtk_box_pack_start(GTK_BOX(banner), GTK_WIDGET(this->bannerLabel), TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(banner), GTK_WIDGET(this->retryButton), FALSE, FALSE, 0);

    /*
     * The banner's own widgets are shown before the revealer takes it: a revealer keeps its child
     * out of the visible tree until it is revealed, and a widget that was never shown would stay
     * invisible even once it is.
     */
    gtk_widget_show_all(banner);

    this->bannerRevealer = GTK_REVEALER(gtk_revealer_new());
    gtk_revealer_set_transition_type(this->bannerRevealer, GTK_REVEALER_TRANSITION_TYPE_SLIDE_RIGHT);
    gtk_revealer_set_transition_duration(this->bannerRevealer, 150);
    gtk_container_add(GTK_CONTAINER(this->bannerRevealer), banner);

    this->detailsButton = GTK_BUTTON(gtk_button_new());
    gtk_button_set_image(this->detailsButton,
                         gtk_image_new_from_icon_name("dialog-information", GTK_ICON_SIZE_MENU));
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(this->detailsButton)),
                                "xoj-safety-status-action");
    gtk_style_context_add_class(gtk_widget_get_style_context(GTK_WIDGET(this->detailsButton)), "xoj-focus-ring");
    atk_object_set_name(gtk_widget_get_accessible(GTK_WIDGET(this->detailsButton)), _("Show save details"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(this->detailsButton),
                                _("When this document was last saved, autosaved and exported"));

    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(this->icon), FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(this->label), TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(this->bannerRevealer), FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(box), GTK_WIDGET(this->detailsButton), FALSE, FALSE, 0);

    g_signal_connect(this->detailsButton, "clicked", G_CALLBACK(onDetailsClicked), this);
    g_signal_connect(this->retryButton, "clicked", G_CALLBACK(onRetryClicked), this);

    // Everything is shown once, and then the banner is closed again: a revealer keeps its own
    // child's visibility, and showing the tree is the only way the child is there to reveal.
    gtk_widget_show_all(box);
    gtk_revealer_set_reveal_child(this->bannerRevealer, FALSE);
}

SafetyStatusBar::~SafetyStatusBar() {
    if (this->confirmationTimeout != 0) {
        g_source_remove(this->confirmationTimeout);
        this->confirmationTimeout = 0;
    }
    /*
     * The row is held by a reference of its own, so it is still alive even when the window that
     * contained it has already been destroyed. Destroying it here releases exactly what this
     * object holds.
     */
    this->row.reset();
}

GtkWidget* SafetyStatusBar::getWidget() const { return this->row.get(); }

auto SafetyStatusBar::getStateIconName(SafetyState state) -> const char* {
    switch (state) {
        case SafetyState::Clean:
        case SafetyState::Saved:
            return "object-select";
        case SafetyState::Modified:
        case SafetyState::Autosaved:
            return "document-save";
        case SafetyState::Saving:
        case SafetyState::Autosaving:
            return "view-refresh";
        case SafetyState::Exporting:
        case SafetyState::Exported:
            return "document-send";
        case SafetyState::Error:
            return "dialog-error";
    }
    return "dialog-information";
}

auto SafetyStatusBar::getStateCssClass(SafetyState state) -> const char* {
    switch (state) {
        case SafetyState::Clean:
        case SafetyState::Saving:
        case SafetyState::Autosaving:
            return "xoj-status-neutral";
        case SafetyState::Saved:
            return "xoj-status-success";
        /*
         * An autosaved document is recoverable, not saved: the copy is a second chance and not
         * the user's own file, so it does not get the success colour.
         */
        case SafetyState::Modified:
        case SafetyState::Autosaved:
            return "xoj-status-warning";
        case SafetyState::Exporting:
        case SafetyState::Exported:
            return "xoj-status-neutral";
        case SafetyState::Error:
            return "xoj-status-error";
    }
    return "xoj-status-neutral";
}

auto SafetyStatusBar::getStateText(const SafetySnapshot& snapshot) -> std::string {
    std::string text;
    switch (snapshot.state) {
        case SafetyState::Clean:
            text = _("All changes saved");
            break;
        case SafetyState::Modified:
            text = _("Unsaved changes");
            break;
        case SafetyState::Saving:
            text = _("Saving…");
            break;
        case SafetyState::Saved:
            text = _("Saved");
            break;
        case SafetyState::Autosaving:
            text = _("Autosaving…");
            break;
        // Deliberately not "Saved": an autosave is a recovery copy, not the user's file.
        case SafetyState::Autosaved:
            text = _("Recovery copy saved");
            break;
        case SafetyState::Exporting:
            text = _("Exporting…");
            break;
        case SafetyState::Exported:
            text = _("Exported");
            break;
        case SafetyState::Error:
            switch (snapshot.failedOperation) {
                case SafetyOperation::Save:
                    text = _("Save failed");
                    break;
                case SafetyOperation::Autosave:
                    text = _("Autosave failed");
                    break;
                case SafetyOperation::Export:
                    text = _("Export failed");
                    break;
                case SafetyOperation::None:
                    text = _("Error");
                    break;
            }
            break;
    }

    /*
     * An export is a state of the operation, so while it is shown the document's own state has to
     * be said as well: otherwise "Exported" would read as "your work is safe", which is the one
     * thing an export does not mean.
     */
    if (snapshot.documentModified &&
        (snapshot.state == SafetyState::Exporting || snapshot.state == SafetyState::Exported)) {
        text += " - ";
        text += _("document not saved");
    }

    return text;
}

auto SafetyStatusBar::isRetryable(const SafetySnapshot& snapshot) -> bool {
    if (snapshot.state != SafetyState::Error) {
        return false;
    }
    // An export is a one-off action with its own dialog, so there is nothing to run again here.
    return snapshot.failedOperation == SafetyOperation::Save || snapshot.failedOperation == SafetyOperation::Autosave;
}

auto SafetyStatusBar::getDetailsText(const SafetySnapshot& snapshot) -> std::string {
    std::string details;

    details += detailLine("Last save", snapshot.lastSaveTime, snapshot.lastSavePath);
    details += detailLine("Last autosave", snapshot.recoveryTime, snapshot.recoveryPath);

    if (snapshot.recoveryCopyExists) {
        details += snapshot.recoveryCopyIsCurrent ? _("The recovery copy covers the current edits") :
                                                    _("The recovery copy is older than the last edit");
        details += "\n";
    }

    details += detailLine("Last export", snapshot.lastExportTime, snapshot.lastExportPath);

    if (snapshot.state == SafetyState::Error && !snapshot.lastError.empty()) {
        details += FS(_F("Last error: {1}") % snapshot.lastError) + "\n";
    }

    if (details.empty()) {
        details = _("Nothing has been saved yet.");
    }

    return details;
}

void SafetyStatusBar::update(const SafetySnapshot& snapshot) {
    this->lastSnapshot = snapshot;

    const std::string text = getStateText(snapshot);

    gtk_label_set_text(this->label, text.c_str());
    gtk_image_set_from_icon_name(this->icon, getStateIconName(snapshot.state), GTK_ICON_SIZE_MENU);
    atk_object_set_name(gtk_widget_get_accessible(this->row.get()), text.c_str());

    GtkStyleContext* context = gtk_widget_get_style_context(this->row.get());
    for (const char* name: STATE_CSS_CLASSES) {
        gtk_style_context_remove_class(context, name);
    }
    gtk_style_context_add_class(context, getStateCssClass(snapshot.state));

    const std::string details = getDetailsText(snapshot);
    gtk_widget_set_tooltip_text(this->row.get(), details.c_str());

    const bool failed = snapshot.state == SafetyState::Error;
    if (failed) {
        gtk_label_set_text(this->bannerLabel, snapshot.lastError.c_str());
        gtk_widget_set_visible(GTK_WIDGET(this->retryButton), isRetryable(snapshot));
    }
    gtk_revealer_set_reveal_child(this->bannerRevealer, failed);

    this->scheduleConfirmationTimeout(snapshot);
}

void SafetyStatusBar::scheduleConfirmationTimeout(const SafetySnapshot& snapshot) {
    if (this->confirmationTimeout != 0) {
        g_source_remove(this->confirmationTimeout);
        this->confirmationTimeout = 0;
    }
    if (!snapshot.confirmationDeadline.has_value()) {
        return;
    }

    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(*snapshot.confirmationDeadline -
                                                                                std::chrono::system_clock::now());
    const auto bounded = std::clamp<long long>(remaining.count(), 1, 3600000);
    this->confirmationTimeout = g_timeout_add(static_cast<guint>(bounded), onConfirmationTimeout, this);
}

auto SafetyStatusBar::onConfirmationTimeout(gpointer data) -> gboolean {
    auto* self = static_cast<SafetyStatusBar*>(data);
    self->confirmationTimeout = 0;
    if (self->callbacks.refresh) {
        self->callbacks.refresh();
    }
    return G_SOURCE_REMOVE;
}

auto SafetyStatusBar::onDetailsClicked(GtkButton*, gpointer data) -> void {
    auto* self = static_cast<SafetyStatusBar*>(data);
    if (!self->callbacks.showDetails) {
        return;
    }
    // The details of the last state this row was told about are the ones it shows.
    const SafetySnapshot snapshot = self->lastSnapshot;
    self->callbacks.showDetails(getDetailsText(snapshot), isRetryable(snapshot));
}

auto SafetyStatusBar::onRetryClicked(GtkButton*, gpointer data) -> void {
    auto* self = static_cast<SafetyStatusBar*>(data);
    if (self->callbacks.retry) {
        self->callbacks.retry();
    }
}

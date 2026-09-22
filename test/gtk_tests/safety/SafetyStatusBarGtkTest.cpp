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
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "../dialog/GtkTest.h"
#include "control/DocumentSafetyState.h"  // for SafetySnapshot, SafetyState
#include "gui/SafetyStatusBar.h"          // for SafetyStatusBar

/*
 * Plan 004, step 3: the document-safety row.
 *
 * The row is built without a Control and without a MainWindow: it takes the callbacks it needs,
 * so the tests can see exactly what it asks its owner to do and can drive it through every state.
 * That is also how the plan's requirements are checked here - an icon and a text for every state,
 * one height in every state, the error in a revealed banner rather than a modal dialog, and a
 * teardown that leaves no critical behind.
 */

namespace {

using xoj::safety::SafetyOperation;
using xoj::safety::SafetySnapshot;
using xoj::safety::SafetyState;
using xoj::safety::TimePoint;

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

auto accessibleRole(GtkWidget* widget) -> AtkRole { return atk_object_get_role(gtk_widget_get_accessible(widget)); }

auto hasCssClass(GtkWidget* widget, const char* name) -> bool {
    return gtk_style_context_has_class(gtk_widget_get_style_context(widget), name);
}

auto widgetsWithClass(GtkWidget* root, const char* name) -> std::vector<GtkWidget*> {
    std::vector<GtkWidget*> matches;
    for (GtkWidget* widget: allWidgets(root)) {
        if (hasCssClass(widget, name)) {
            matches.emplace_back(widget);
        }
    }
    return matches;
}

/// The one control of the row whose accessible name is `name`.
auto controlNamed(GtkWidget* root, const char* name) -> GtkWidget* {
    for (GtkWidget* widget: allWidgets(root)) {
        if (GTK_IS_BUTTON(widget) && accessibleName(widget) == name) {
            return widget;
        }
    }
    return nullptr;
}

/// Lets GTK finish what it queued, so the widget tree settles into the state a user would see.
void settle() {
    while (g_main_context_iteration(nullptr, FALSE)) {}
}

/// The height the row asks for, which is what the canvas below it is laid out against.
auto rowHeight(GtkWidget* row) -> int {
    GtkRequisition preferred{};
    gtk_widget_get_preferred_size(row, nullptr, &preferred);
    return preferred.height;
}

/// The states a user can be shown, with the snapshots that produce them.
auto snapshots() -> std::vector<std::pair<std::string, SafetySnapshot>> {
    std::vector<std::pair<std::string, SafetySnapshot>> all;

    auto add = [&all](const char* name, SafetySnapshot snapshot) {
        all.emplace_back(name, std::move(snapshot));
    };

    SafetySnapshot clean;
    add("clean", clean);

    SafetySnapshot modified;
    modified.state = SafetyState::Modified;
    modified.documentModified = true;
    add("modified", modified);

    SafetySnapshot saving;
    saving.state = SafetyState::Saving;
    saving.documentModified = true;
    add("saving", saving);

    SafetySnapshot saved;
    saved.state = SafetyState::Saved;
    saved.lastSaveTime = std::chrono::system_clock::now();
    saved.lastSavePath = "/tmp/notes.xopp";
    add("saved", saved);

    SafetySnapshot autosaving;
    autosaving.state = SafetyState::Autosaving;
    autosaving.documentModified = true;
    add("autosaving", autosaving);

    SafetySnapshot autosaved;
    autosaved.state = SafetyState::Autosaved;
    autosaved.documentModified = true;
    autosaved.recoveryCopyExists = true;
    autosaved.recoveryCopyIsCurrent = true;
    autosaved.recoveryTime = std::chrono::system_clock::now();
    autosaved.recoveryPath = "/tmp/.notes.autosave.xopp";
    add("autosaved", autosaved);

    SafetySnapshot exporting;
    exporting.state = SafetyState::Exporting;
    exporting.documentModified = true;
    add("exporting", exporting);

    SafetySnapshot exported;
    exported.state = SafetyState::Exported;
    exported.lastExportTime = std::chrono::system_clock::now();
    exported.lastExportPath = "/tmp/notes.pdf";
    add("exported", exported);

    SafetySnapshot exportedWhileModified;
    exportedWhileModified.state = SafetyState::Exported;
    exportedWhileModified.documentModified = true;
    exportedWhileModified.lastExportTime = std::chrono::system_clock::now();
    exportedWhileModified.lastExportPath = "/tmp/notes.pdf";
    add("exported while modified", exportedWhileModified);

    for (auto [name, operation]: {std::pair{"save error", SafetyOperation::Save},
                                  {"autosave error", SafetyOperation::Autosave},
                                  {"export error", SafetyOperation::Export}}) {
        SafetySnapshot failed;
        failed.state = SafetyState::Error;
        failed.documentModified = true;
        failed.failedOperation = operation;
        failed.lastError = std::string("the ") + name + " could not be written";
        add(name, failed);
    }

    return all;
}

/**
 * Counts the GLib criticals logged while it is alive.
 *
 * A widget that unrefs memory it does not own does not crash a normal build: GLib logs a critical
 * and carries on. The teardown below is watched here, so a critical is a failure.
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

}  // namespace

/*
 * Plan 004, step 3: every state is an icon, a text label and a semantic class, and the row keeps
 * one height whatever it says - so a change of state never moves the canvas below it.
 */
class SafetyStatusRowStatesTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);

        SafetyStatusBar bar{SafetyStatusBar::Callbacks{}};
        GtkWidget* row = bar.getWidget();
        gtk_container_add(GTK_CONTAINER(window), row);
        gtk_widget_show_all(window);
        settle();

        ASSERT_EQ(accessibleRole(row), ATK_ROLE_STATUSBAR) << "a reader who cannot see the row reads it";

        const int height = rowHeight(row);
        std::vector<std::string> texts;

        for (const auto& [name, snapshot]: snapshots()) {
            bar.update(snapshot);
            settle();

            // An icon, by name, so the row is never a bare text and never colour alone.
            const std::vector<GtkWidget*> icons = widgetsWithClass(row, "xoj-safety-status-icon");
            ASSERT_EQ(icons.size(), 1U) << name << ": the row carries one state icon";
            ASSERT_TRUE(GTK_IS_IMAGE(icons.front())) << name;
            ASSERT_EQ(gtk_image_get_storage_type(GTK_IMAGE(icons.front())), GTK_IMAGE_ICON_NAME) << name;
            const gchar* iconName = nullptr;
            gtk_image_get_icon_name(GTK_IMAGE(icons.front()), &iconName, nullptr);
            ASSERT_NE(iconName, nullptr) << name << ": the icon is named";
            EXPECT_NE(std::string(iconName).size(), 0U) << name;

            // A text label, with the state in words.
            const std::vector<GtkWidget*> labels = widgetsWithClass(row, "xoj-safety-status-text");
            ASSERT_EQ(labels.size(), 1U) << name << ": the row carries one state label";
            ASSERT_TRUE(GTK_IS_LABEL(labels.front())) << name;
            const std::string text = gtk_label_get_text(GTK_LABEL(labels.front()));
            EXPECT_FALSE(text.empty()) << name << ": the state is written out, not only coloured";
            texts.emplace_back(text);

            // And the state's semantic class from Plan 001, exactly one of the four.
            std::size_t classes = 0;
            for (const char* cssClass:
                 {"xoj-status-neutral", "xoj-status-success", "xoj-status-warning", "xoj-status-error"}) {
                classes += hasCssClass(row, cssClass) ? 1 : 0;
            }
            EXPECT_EQ(classes, 1U) << name << ": the row carries exactly one semantic status class";

            // The accessible name follows the state, so a screen reader hears the change.
            EXPECT_EQ(accessibleName(row), text) << name;

            // And the row's height is the one the canvas was laid out against.
            EXPECT_EQ(rowHeight(row), height) << name << ": a state change must not move the canvas";

            // The details are always reachable, whatever the row says.
            GtkWidget* details = controlNamed(row, "Show save details");
            ASSERT_NE(details, nullptr) << name;
            EXPECT_TRUE(gtk_widget_get_visible(details)) << name;
            EXPECT_TRUE(gtk_widget_get_sensitive(details)) << name;
        }

        // Every state reads differently, so the four the plan names cannot be confused.
        auto textOf = [&texts](std::size_t index) { return texts.at(index); };
        EXPECT_NE(textOf(0), textOf(1)) << "Clean and Modified must read differently";
        EXPECT_NE(textOf(3), textOf(5)) << "Saved and Autosaved must read differently";
        EXPECT_NE(textOf(1), textOf(5)) << "Modified and Autosaved must read differently";
        EXPECT_NE(textOf(5), textOf(11)) << "Autosaved and Error must read differently";
        EXPECT_EQ(textOf(3).find("Recovery"), std::string::npos) << "an explicit save is simply Saved";
        EXPECT_NE(textOf(5).find("Recovery"), std::string::npos)
                << "an autosave is named as a recovery copy, never as Saved";

        gtk_widget_destroy(window);
        settle();
    }
};
TEST_F(SafetyStatusRowStatesTest, everyStateIsAnIconATextAndOneHeight) {}

/*
 * Plan 004, step 3: "Clicking Error opens details/retry where supported" and "narrow windows
 * retain access to the status details". The failure is in a revealed banner and not in a modal
 * dialog, and the text shrinks before the controls do.
 */
class SafetyStatusErrorBannerTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 360, 240);

        std::string detailsShown;
        bool detailsRetryable = false;
        int retries = 0;
        int refreshes = 0;

        SafetyStatusBar bar{SafetyStatusBar::Callbacks{
                .showDetails = [&](const std::string& details, bool retryable) {
                    detailsShown = details;
                    detailsRetryable = retryable;
                },
                .retry = [&]() { retries++; },
                .refresh = [&]() { refreshes++; }}};

        GtkWidget* row = bar.getWidget();
        gtk_container_add(GTK_CONTAINER(window), row);
        gtk_widget_show_all(window);
        settle();

        auto revealer = [&row]() -> GtkRevealer* {
            for (GtkWidget* widget: allWidgets(row)) {
                if (GTK_IS_REVEALER(widget)) {
                    return GTK_REVEALER(widget);
                }
            }
            return nullptr;
        };
        GtkRevealer* bannerRevealer = revealer();
        ASSERT_NE(bannerRevealer, nullptr) << "a transient failure is revealed rather than shown outright";

        // Nothing is in the way while the document is fine.
        SafetySnapshot modified;
        modified.state = SafetyState::Modified;
        modified.documentModified = true;
        bar.update(modified);
        settle();
        EXPECT_FALSE(gtk_revealer_get_reveal_child(bannerRevealer));

        // A failed save reveals the banner and offers a way back to the operation.
        SafetySnapshot failed;
        failed.state = SafetyState::Error;
        failed.documentModified = true;
        failed.failedOperation = SafetyOperation::Save;
        failed.lastError = "Permission denied";
        failed.lastSaveTime = std::chrono::system_clock::now();
        failed.lastSavePath = "/tmp/notes.xopp";
        bar.update(failed);
        settle();

        EXPECT_TRUE(gtk_revealer_get_reveal_child(bannerRevealer));
        GtkWidget* banner = gtk_bin_get_child(GTK_BIN(bannerRevealer));
        ASSERT_NE(banner, nullptr);
        EXPECT_TRUE(gtk_widget_is_visible(banner)) << "the failure is on screen, not only in the state";

        const std::vector<GtkWidget*> bannerLabels = widgetsWithClass(banner, "xoj-safety-status-banner");
        ASSERT_EQ(bannerLabels.size(), 1U);
        bool bannerSaysWhy = false;
        for (GtkWidget* widget: allWidgets(banner)) {
            if (GTK_IS_LABEL(widget) && std::string(gtk_label_get_text(GTK_LABEL(widget))) == "Permission denied") {
                bannerSaysWhy = true;
            }
        }
        EXPECT_TRUE(bannerSaysWhy) << "the banner carries the reason, not just a colour";

        GtkWidget* retry = controlNamed(row, "Retry the failed operation");
        ASSERT_NE(retry, nullptr);
        EXPECT_TRUE(gtk_widget_get_visible(retry)) << "a failed save can be run again from the row";
        g_signal_emit_by_name(retry, "clicked");
        EXPECT_EQ(retries, 1);

        // The details behind the row name the file and the reason.
        GtkWidget* details = controlNamed(row, "Show save details");
        ASSERT_NE(details, nullptr);
        g_signal_emit_by_name(details, "clicked");
        EXPECT_NE(detailsShown.find("/tmp/notes.xopp"), std::string::npos);
        EXPECT_NE(detailsShown.find("Permission denied"), std::string::npos);
        EXPECT_TRUE(detailsRetryable) << "the details know the operation can be run again";

        // An export failure has no retry: an export is a one-off action with its own dialog.
        SafetySnapshot exportFailed;
        exportFailed.state = SafetyState::Error;
        exportFailed.failedOperation = SafetyOperation::Export;
        exportFailed.lastError = "Disk quota exceeded";
        bar.update(exportFailed);
        settle();
        EXPECT_TRUE(gtk_revealer_get_reveal_child(bannerRevealer));
        EXPECT_FALSE(gtk_widget_get_visible(retry)) << "an export has nothing to run again";
        g_signal_emit_by_name(details, "clicked");
        EXPECT_FALSE(detailsRetryable);

        // A recovered state closes the banner again.
        bar.update(modified);
        settle();
        EXPECT_FALSE(gtk_revealer_get_reveal_child(bannerRevealer));

        // A narrow window: the text is what shrinks, the controls keep their place.
        const std::vector<GtkWidget*> labels = widgetsWithClass(row, "xoj-safety-status-text");
        ASSERT_EQ(labels.size(), 1U);
        EXPECT_EQ(gtk_label_get_ellipsize(GTK_LABEL(labels.front())), PANGO_ELLIPSIZE_END)
                << "a narrow window takes the width out of the text, not out of the controls";

        gboolean labelExpands = FALSE;
        gtk_container_child_get(GTK_CONTAINER(row), labels.front(), "expand", &labelExpands, nullptr);
        EXPECT_TRUE(labelExpands) << "the state text is what gives way when the row is squeezed";
        gboolean detailsExpands = TRUE;
        gtk_container_child_get(GTK_CONTAINER(row), details, "expand", &detailsExpands, nullptr);
        EXPECT_FALSE(detailsExpands) << "the details stay in place at their own size";

        int minimumWidth = 0;
        gtk_widget_get_preferred_width(row, &minimumWidth, nullptr);
        EXPECT_LT(minimumWidth, 360) << "the row still fits a narrow window with its controls intact";

        gtk_widget_destroy(window);
        settle();
    }
};
TEST_F(SafetyStatusErrorBannerTest, aFailureIsRevealedBesideTheStateWithADetailsAndRetryPath) {}

/*
 * Plan 004, step 3: the row outlives the window it was put in.
 *
 * MainWindow is torn down after the window it owns, so a row that destroyed itself twice - once
 * with the window and once with its own object - would unref memory the window already freed. The
 * pending confirmation timer has to be dropped too, or GLib complains about a source it cannot
 * find.
 */
class SafetyStatusTeardownTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 900, 600);

        CriticalWatch criticals;

        {
            SafetyStatusBar bar{SafetyStatusBar::Callbacks{}};
            GtkWidget* row = bar.getWidget();
            gtk_container_add(GTK_CONTAINER(window), row);
            gtk_widget_show_all(window);
            settle();

            SafetySnapshot saved;
            saved.state = SafetyState::Saved;
            saved.lastSaveTime = std::chrono::system_clock::now();
            saved.lastSavePath = "/tmp/notes.xopp";
            // A confirmation that is still running when everything is torn down.
            saved.confirmationDeadline = std::chrono::system_clock::now() + std::chrono::seconds(30);
            bar.update(saved);
            settle();

            gtk_widget_destroy(window);
            settle();
        }

        EXPECT_EQ(criticals.count(), 0U) << criticals.report();
    }
};
TEST_F(SafetyStatusTeardownTest, theRowIsReleasedOnceAfterItsWindowIsGone) {}

/*
 * Plan 004, step 3: "Short-lived Saved confirmation". The row asks to be refreshed when the
 * confirmation it was given runs out, so the document state comes back on its own.
 */
class SafetyStatusConfirmationTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);

        int refreshes = 0;
        SafetyStatusBar bar{SafetyStatusBar::Callbacks{.refresh = [&]() { refreshes++; }}};
        gtk_container_add(GTK_CONTAINER(window), bar.getWidget());
        gtk_widget_show_all(window);
        settle();

        SafetySnapshot saved;
        saved.state = SafetyState::Saved;
        saved.lastSaveTime = std::chrono::system_clock::now();
        saved.lastSavePath = "/tmp/notes.xopp";
        saved.confirmationDeadline = std::chrono::system_clock::now() + std::chrono::milliseconds(20);

        bar.update(saved);
        EXPECT_EQ(refreshes, 0) << "nothing is refreshed before the confirmation runs out";

        // A confirmation that has already run out is not scheduled again.
        SafetySnapshot expired = saved;
        expired.confirmationDeadline = std::chrono::system_clock::now() - std::chrono::seconds(1);
        bar.update(expired);

        for (int i = 0; i < 200 && refreshes == 0; i++) {
            settle();
            g_usleep(5000);
        }

        EXPECT_EQ(refreshes, 1) << "the row asks for the current state once the confirmation has run out";

        gtk_widget_destroy(window);
        settle();
    }
};
TEST_F(SafetyStatusConfirmationTest, aTransientConfirmationEndsOnItsOwn) {}

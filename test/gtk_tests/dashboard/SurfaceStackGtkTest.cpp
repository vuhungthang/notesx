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

#include <cstddef>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "../dialog/GtkTest.h"
#include "gui/dashboard/SurfaceStack.h"  // for SurfaceStack

#include "filesystem.h"

/*
 * Plan 006, step 3 and the STOP condition it guards: "Home/editor switching risks discarding
 * unsaved state".
 *
 * This is the window's actual surface stack, driven the way the window drives it. What the editor
 * holds is not available without a whole application, so the editor is stood in for by a widget
 * that carries state of its own - a text field with something written in it and an object the
 * switching has no reason to touch. If switching ever destroyed, rebuilt or re-parented the editor,
 * every one of these assertions would fail: the widget would be a different object, its destroy
 * handler would have run, and what it held would be gone.
 */

namespace {

using xoj::dashboard::SurfaceStack;

/// The number of children a container has.
auto childCount(GtkWidget* container) -> std::size_t {
    GList* children = gtk_container_get_children(GTK_CONTAINER(container));
    const std::size_t count = g_list_length(children);
    g_list_free(children);
    return count;
}

/// How many times a widget was destroyed, so a test can say nothing was rebuilt.
struct DestroyCounter {
    int count = 0;
};

}  // namespace

class SurfaceStackGtkTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GtkWidget* window = gtk_application_window_new(app);
        gtk_window_set_default_size(GTK_WINDOW(window), 1024, 600);

        /*
         * The editor: a widget with state in it. `entry`'s text is the stand-in for the document,
         * the page and the unsaved changes a real editor would keep across a surface change.
         */
        GtkWidget* editor = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_name(editor, "editorSurface");
        GtkWidget* entry = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(entry), "something that has not been saved yet");
        gtk_box_pack_start(GTK_BOX(editor), entry, FALSE, FALSE, 0);

        DestroyCounter editorDestroyed;
        g_signal_connect(editor, "destroy",
                         G_CALLBACK(+[](GtkWidget*, gpointer data) { static_cast<DestroyCounter*>(data)->count++; }),
                         &editorDestroyed);

        // The home surface, standing in for the dashboard.
        GtkWidget* home = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_widget_set_name(home, "homeSurface");

        int prepared = 0;
        std::vector<bool> shownStates;
        SurfaceStack::Callbacks callbacks;
        callbacks.prepareHome = [&prepared]() { prepared++; };
        callbacks.shown = [&shownStates](bool homeShown) { shownStates.push_back(homeShown); };

        SurfaceStack surfaces{GTK_WINDOW(window), editor, home, callbacks};
        GtkWidget* contents = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        gtk_container_add(GTK_CONTAINER(window), contents);
        gtk_box_pack_start(GTK_BOX(contents), surfaces.getBar(), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(contents), surfaces.getWidget(), TRUE, TRUE, 0);
        gtk_widget_show_all(window);

        GtkWidget* stack = surfaces.getWidget();
        ASSERT_EQ(childCount(stack), 2U) << "both surfaces are pages of the one stack";

        // The editor is what is shown first, and the way to the dashboard is on screen with it.
        EXPECT_FALSE(surfaces.isHomeShown());
        EXPECT_TRUE(gtk_widget_get_child_visible(editor));
        EXPECT_FALSE(gtk_widget_get_child_visible(home));
        EXPECT_TRUE(gtk_widget_get_visible(surfaces.getBar()));

        // The button goes through the window action, so the shortcut and the button are one way in.
        GtkWidget* homeButton = surfaces.getHomeButton();
        ASSERT_NE(homeButton, nullptr);
        EXPECT_STREQ(gtk_actionable_get_action_name(GTK_ACTIONABLE(homeButton)), SurfaceStack::SHOW_HOME_FULL_ACTION);
        EXPECT_EQ(gtk_widget_get_parent(homeButton), surfaces.getBar())
                << "the way home sits in its own bar beside the editor, not among the editor's controls";
        EXPECT_EQ(childCount(editor), 1U) << "nothing of the dashboard is put inside the editor";

        // Activating the action is what the button does; it is also what the shortcut does.
        GSimpleAction* action =
                G_SIMPLE_ACTION(g_action_map_lookup_action(G_ACTION_MAP(window), SurfaceStack::SHOW_HOME_ACTION));
        ASSERT_NE(action, nullptr) << "the window carries the action the button names";
        ASSERT_TRUE(g_action_group_has_action(G_ACTION_GROUP(window), SurfaceStack::SHOW_HOME_ACTION));
        g_action_group_activate_action(G_ACTION_GROUP(window), SurfaceStack::SHOW_HOME_ACTION, nullptr);

        EXPECT_EQ(prepared, 1) << "the dashboard is made current before it is shown";
        EXPECT_TRUE(surfaces.isHomeShown());
        EXPECT_TRUE(gtk_widget_get_child_visible(home));
        EXPECT_FALSE(gtk_widget_get_child_visible(editor)) << "the editor is hidden, not removed";
        EXPECT_FALSE(gtk_widget_get_visible(surfaces.getBar())) << "the dashboard carries its own way back";
        ASSERT_EQ(shownStates.size(), 1U);
        EXPECT_TRUE(shownStates.front());

        // The other direction: back to the document, exactly as it was left.
        surfaces.showEditor();
        EXPECT_FALSE(surfaces.isHomeShown());
        ASSERT_EQ(shownStates.size(), 2U);
        EXPECT_FALSE(shownStates.back());
        EXPECT_TRUE(gtk_widget_get_visible(surfaces.getBar()));

        // Several changes in a row: the editor is still the same widget, still a page of the same
        // stack, still holding what it held, and it was never destroyed.
        for (int i = 0; i < 4; i++) {
            surfaces.showHome();
            surfaces.showEditor();
        }
        EXPECT_EQ(editorDestroyed.count, 0) << "switching surfaces never touches the editor's lifetime";
        EXPECT_EQ(gtk_widget_get_parent(editor), stack) << "the editor was not re-parented by a switch";
        EXPECT_EQ(childCount(stack), 2U) << "no page was added or removed by a switch";
        EXPECT_EQ(std::string(gtk_entry_get_text(GTK_ENTRY(entry))), "something that has not been saved yet")
                << "what the editor holds survives every switch";
        EXPECT_EQ(prepared, 5) << "each visit to the dashboard makes it current again";

        // The action goes away with the surfaces: a window that outlives them has no button that
        // calls into freed memory.
        {
            SurfaceStack temporary{GTK_WINDOW(window),
                                   gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                                   gtk_box_new(GTK_ORIENTATION_VERTICAL, 0),
                                   {}};
            EXPECT_TRUE(g_action_group_has_action(G_ACTION_GROUP(window), SurfaceStack::SHOW_HOME_ACTION));
        }
        EXPECT_FALSE(g_action_group_has_action(G_ACTION_GROUP(window), SurfaceStack::SHOW_HOME_ACTION))
                << "the action is the surfaces' and goes with them";

        gtk_widget_destroy(window);
    }
};
TEST_F(SurfaceStackGtkTest, switchingSurfacesKeepsTheEditorAndItsState) {}

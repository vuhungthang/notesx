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

#include <algorithm>   // for find
#include <functional>  // for function
#include <memory>      // for unique_ptr
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <gtk/gtk.h>  // for GtkApplication, GtkApplicationWindow

#include "../dialog/GtkTest.h"
#include "control/Control.h"                           // for Control
#include "control/ToolConfigAdapter.h"                 // for ToolConfigAdapter
#include "control/ToolEnums.h"                         // for ToolType, TOOL_PEN
#include "control/ToolHandler.h"                       // for ToolHandler
#include "control/ToolPreset.h"                        // for PresetListListener
#include "control/settings/Settings.h"                 // for Settings
#include "gui/GladeSearchpath.h"                       // for GladeSearchpath
#include "gui/IconNameHelper.h"                        // for IconNameHelper
#include "gui/MainWindow.h"                            // for MainWindow
#include "gui/TipService.h"                            // for TipService
#include "gui/XournalView.h"                           // for XournalView, an anchor inside the window
#include "gui/toolbarMenubar/AbstractToolItem.h"       // for AbstractToolItem
#include "gui/toolbarMenubar/PresetFavoritesItem.h"    // for PresetFavoritesItem
#include "gui/toolbarMenubar/ToolPropertyPopover.h"    // for ToolPropertyPopoverFactory
#include "gui/toolbarMenubar/ToolPropertyProvider.h"   // for ToolPropertyProvider, ToolPropertyRegistry
#include "gui/toolbarMenubar/ToolPropertyProviders.h"  // for addBuiltInToolPropertyProviders
#include "model/PageRef.h"                             // for PageRef
#include "model/XojPage.h"                             // for XojPage
#include "util/gtk4_helper.h"                          // for gtk_box_append
#include "util/raii/GObjectSPtr.h"                     // for WidgetSPtr

#include "config-test.h"

/*
 * Plan 007, step 4: the tips.
 *
 * A tip is offered by whatever knows the moment it is about - here the tool property popover, which
 * is built and shown the way the toolbar builds and shows it - and the service decides whether this
 * user is told: once per tip, never two at a time, never at all when the tips are turned off, and
 * remembered in the profile so putting one away is the end of it.
 */

using xoj::gui::TipService;

namespace {

/// Lets GTK finish what it queued, so the window and the popovers have settled.
void settle() {
    for (int i = 0; i < 4; i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(20000);
    }
}

/// Runs what the main context has ready until it has nothing left.
void drain() {
    for (int i = 0; i < 50 && g_main_context_pending(nullptr); i++) {
        while (g_main_context_iteration(nullptr, FALSE)) {}
        g_usleep(10000);
    }
}

auto focusedWidget(GtkWidget* window) -> GtkWidget* { return gtk_window_get_focus(GTK_WINDOW(window)); }

/// The sentence the tip surface is showing: the label in the tip's own popover.
auto shownSentence(const TipService* tips) -> std::string {
    GtkWidget* box = gtk_bin_get_child(GTK_BIN(tips->getPopover()));
    if (box == nullptr) {
        return {};
    }
    std::string text;
    GList* children = gtk_container_get_children(GTK_CONTAINER(box));
    for (GList* child = children; child != nullptr; child = child->next) {
        if (GTK_IS_LABEL(child->data)) {
            text = gtk_label_get_text(GTK_LABEL(child->data));
            break;
        }
    }
    g_list_free(children);
    return text;
}

/// The first favourite button of a strip, or nullptr when the strip has none.
auto favouriteButton(GtkWidget* strip) -> GtkWidget* {
    GList* children = gtk_container_get_children(GTK_CONTAINER(strip));
    GtkWidget* button = nullptr;
    for (GList* child = children; child != nullptr; child = child->next) {
        if (GTK_IS_BUTTON(child->data)) {
            button = GTK_WIDGET(child->data);
            break;
        }
    }
    g_list_free(children);
    return button;
}

/// ToolHandler reports changes to a listener; these tests do not look at the reports.
class StubToolListener: public ToolListener {
public:
    void toolColorChanged() override {}
    void changeColorOfSelection() override {}
    void toolSizeChanged() override {}
    void toolFillChanged() override {}
    void toolLineStyleChanged() override {}
    void toolChanged() override {}
};

/// The preset list is not what these tests are about either.
class StubPresetListListener: public PresetListListener {
public:
    void presetListChanged() override {}
};

}  // namespace

class TipServiceFixture;

/// One test: what the profile looks like first, and then what the user does.
struct TipScenario {
    const char* name;
    std::function<void(Settings&)> prepare;
    std::function<void(TipServiceFixture&)> run;
};

void PrintTo(const TipScenario& scenario, std::ostream* out) { *out << scenario.name; }

class TipServiceFixture: public GtkTest, public ::testing::WithParamInterface<TipScenario> {
public:
    auto tips() const -> TipService* { return this->win->getTipService(); }
    auto settings() const -> Settings* { return this->control->getSettings(); }
    auto focused() const -> GtkWidget* { return focusedWidget(this->win->getWindow()); }
    /// A widget of the window to hang a popover off, the way a tool button is.
    auto anchor() const -> GtkWidget* { return this->win->getXournal()->getWidget(); }

    /**
     * Build and show a tool's property popover the way the toolbar does: a factory over the window,
     * which is where a service that wants to be found by the widgets under it lives.
     */
    auto showToolProperties() -> GtkWidget* {
        ToolPropertyRegistry registry;
        IconNameHelper icons{this->settings()};
        xoj::toolbar::addBuiltInToolPropertyProviders(registry, icons);
        ToolPropertyProvider* pen = registry.find(TOOL_PEN);
        if (pen == nullptr) {
            ADD_FAILURE() << "the pen has properties";
            return nullptr;
        }

        this->factory = std::make_unique<ToolPropertyPopoverFactory>(
                *this->adapter, *this->settings(), *pen, GTK_WINDOW(this->win->getWindow()), &this->presetsListener);
        GtkWidget* popover = this->factory->createPopover();
        gtk_popover_set_relative_to(GTK_POPOVER(popover), this->anchor());
        gtk_popover_popup(GTK_POPOVER(popover));
        settle();
        this->toolPopover.reset(popover, xoj::util::ref);
        return popover;
    }

    void hideToolProperties() {
        if (this->toolPopover) {
            gtk_popover_popdown(GTK_POPOVER(this->toolPopover.get()));
            settle();
        }
    }

    /**
     * The favourite strip of the Focus toolbar - the profile set up to have one - in the window, so
     * that a click on it is a click in this window and not in a window of its own.
     */
    auto showFavouritePresets() -> GtkWidget* {
        ToolPresetList presets;
        const std::string penId =
                presets.add(ToolPreset{.name = "Fine pen", .toolType = TOOL_PEN, .size = TOOL_SIZE_FINE});
        if (!presets.setFavorite(penId, true)) {
            ADD_FAILURE() << "the preset can be a favourite";
            return nullptr;
        }
        this->settings()->setToolPresets(std::move(presets));
        this->settings()->setFavoritePresetCount(1);

        this->icons = std::make_unique<IconNameHelper>(this->settings());
        this->favorites = std::make_unique<PresetFavoritesItem>("PRESET_FAVORITES", *this->adapter, *this->settings(),
                                                                *this->icons);

        // The item is built the way the toolbar builds it, and put in a toolbar the way the toolbar
        // holds it: a GtkToolItem belongs in one, and the strip is its child.
        this->favoritesItem = static_cast<AbstractToolItem&>(*this->favorites).createItem(true);
        GtkWidget* toolbar = gtk_toolbar_new();
        /*
         * A toolbar that is allocated less room than its items need hides them - GTK's own rule, and
         * one that would take the strip off screen in the middle of a test. The window's own toolbar
         * area has the room, so this one is given it.
         */
        gtk_widget_set_size_request(toolbar, -1, 40);
        gtk_toolbar_insert(GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(this->favoritesItem.get()), -1);
        gtk_box_append(GTK_BOX(this->win->get("boxContents")), toolbar);
        gtk_widget_show_all(toolbar);
        settle();
        return gtk_bin_get_child(GTK_BIN(this->favoritesItem.get()));
    }

    /// Press a key where the keyboard is, the way the window delivers it.
    static void pressKeyWhereTheKeyboardIs(GtkWidget* window, guint keyval) {
        GtkWidget* toplevel = gtk_widget_get_toplevel(window);
        ASSERT_TRUE(GTK_IS_WINDOW(toplevel));
        GdkWindow* gdkWindow = gtk_widget_get_window(toplevel);
        ASSERT_NE(gdkWindow, nullptr) << "the window the key press arrives at";

        GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
        event->key.window = static_cast<GdkWindow*>(g_object_ref(gdkWindow));
        event->key.send_event = TRUE;
        event->key.time = GDK_CURRENT_TIME;
        event->key.state = static_cast<GdkModifierType>(0);
        event->key.keyval = keyval;
        event->key.hardware_keycode = 0;
        event->key.group = 0;
        event->key.is_modifier = 0;
        gtk_window_propagate_key_event(GTK_WINDOW(toplevel), &event->key);
        gdk_event_free(event);
    }

private:
    void runTest(GtkApplication* app) final {
        this->glade = std::make_unique<GladeSearchpath>();
        this->glade->addSearchDirectory(GET_UI_FOLDER);
        this->glade->addSearchDirectory(GET_PAGE_TEMPLATE_FOLDER);

        this->control = std::make_unique<Control>(G_APPLICATION(app), this->glade.get(), true);
        this->win = std::make_unique<MainWindow>(this->glade.get(), this->control.get(), GTK_APPLICATION(app));
        this->control->initWindow(this->win.get());
        this->win->populate(this->glade.get());
        this->win->show(nullptr);
        settle();

        for (size_t i = 0; i < 2; i++) {
            this->control->insertPage(std::make_shared<XojPage>(595.28, 841.89), i, false);
        }
        this->win->showEditor();
        settle();

        this->handler = std::make_unique<ToolHandler>(&this->toolListener, nullptr, this->settings());
        this->adapter = std::make_unique<ToolConfigAdapter>(*this->handler);

        this->GetParam().prepare(*this->settings());
        settle();

        this->GetParam().run(*this);

        // Every scenario leaves the tip and the popover it came with put away.
        ASSERT_NE(this->tips(), nullptr);
        EXPECT_FALSE(this->tips()->isShown()) << "a scenario leaves its tip dismissed behind it";
        this->hideToolProperties();

        /*
         * And then the window is destroyed while a tip is up anyway: a caller may close the window
         * with one still over it, and the tip has to survive that on its own. G_DEBUG=fatal-criticals
         * turns a GTK warning along that path into a failed test.
         */
        this->tips()->offer(TipService::Tip::ToolProperties, this->anchor());
        settle();
        drain();

        this->toolPopover.reset();
        this->factory.reset();
        this->favoritesItem.reset();
        this->favorites.reset();
        this->icons.reset();
        this->handler.reset();
        this->win.reset();
        this->control.reset();
        this->glade.reset();
    }

public:
    std::unique_ptr<GladeSearchpath> glade;
    std::unique_ptr<Control> control;
    std::unique_ptr<MainWindow> win;

    StubToolListener toolListener;
    StubPresetListListener presetsListener;
    std::unique_ptr<ToolHandler> handler;
    std::unique_ptr<ToolConfigAdapter> adapter;
    std::unique_ptr<ToolPropertyPopoverFactory> factory;
    xoj::util::WidgetSPtr toolPopover;

    /// Plan 007, step 4: the favourite strip a scenario reaches for.
    std::unique_ptr<IconNameHelper> icons;
    std::unique_ptr<PresetFavoritesItem> favorites;
    xoj::util::WidgetSPtr favoritesItem;
};

namespace {

/// A profile that has never been told anything.
void freshProfile(Settings& settings) {
    settings.resetInterfaceTips();
    settings.setInterfaceTipsEnabled(true);
}

/// Opening a tool's properties for the first time tells the user once, and the tip points at them.
void theTriggerOffersTheTipOnce(TipServiceFixture& test) {
    TipService* tips = test.tips();
    ASSERT_FALSE(tips->isShown()) << "nothing is offered before anything happens";

    GtkWidget* properties = test.showToolProperties();
    ASSERT_TRUE(tips->isShown()) << "opening a tool's properties is what the tip is about";
    ASSERT_TRUE(tips->shownTip().has_value());
    EXPECT_EQ(*tips->shownTip(), TipService::Tip::ToolProperties);

    // Anchored: the tip is shown against the thing it is about, not in a corner.
    EXPECT_EQ(gtk_popover_get_relative_to(GTK_POPOVER(tips->getPopover())), properties);

    // Short, and put away with the button it offers; a keyboard user reaches that button.
    GtkWidget* dismiss = tips->getDismissButton();
    ASSERT_NE(dismiss, nullptr);
    EXPECT_TRUE(gtk_widget_get_can_focus(dismiss));
    const char* accessible = atk_object_get_name(gtk_widget_get_accessible(dismiss));
    ASSERT_NE(accessible, nullptr);
    EXPECT_STRNE(accessible, "");

    tips->dismiss();
    settle();
    EXPECT_FALSE(tips->isShown());
    EXPECT_TRUE(test.settings()->hasSeenTip(TipService::idOf(TipService::Tip::ToolProperties)))
            << "putting a tip away is remembered in the profile";
}

/// The same tip is not offered again once it has been put away.
void aTipOnceDismissedDoesNotComeBack(TipServiceFixture& test) {
    test.showToolProperties();
    EXPECT_FALSE(test.tips()->isShown()) << "a user who has been told this tip is not told it again";

    test.hideToolProperties();
    test.showToolProperties();
    EXPECT_FALSE(test.tips()->isShown()) << "and not by opening the properties a second time either";
}

/// Tips can be turned off, and then nothing is offered at all.
void tipsCanBeTurnedOffGlobally(TipServiceFixture& test) {
    test.showToolProperties();
    EXPECT_FALSE(test.tips()->isShown());

    test.tips()->offer(TipService::Tip::ToolProperties, test.anchor());
    settle();
    EXPECT_FALSE(test.tips()->isShown()) << "and a tip offered directly is not shown either";
}

/// "Show interface tips again" makes every tip available once more.
void resettingBringsTheTipsBack(TipServiceFixture& test) {
    test.showToolProperties();
    ASSERT_TRUE(test.tips()->isShown()) << "forgetting what was shown makes the tip appear again";
    EXPECT_FALSE(test.settings()->hasSeenTip(TipService::idOf(TipService::Tip::ToolProperties)));

    test.tips()->dismiss();
    settle();
}

/// Two tips never stack: the newest one is the one on screen.
void theNewestTipIsTheOnlyOneUp(TipServiceFixture& test) {
    test.showToolProperties();
    ASSERT_TRUE(test.tips()->isShown());
    GtkWidget* first = test.tips()->getPopover();
    const std::string firstSentence = shownSentence(test.tips());
    ASSERT_EQ(firstSentence, TipService::textOf(TipService::Tip::ToolProperties));

    test.tips()->offer(TipService::Tip::FavoritePresets, test.anchor());
    settle();
    ASSERT_TRUE(test.tips()->isShown());
    EXPECT_EQ(*test.tips()->shownTip(), TipService::Tip::FavoritePresets);
    /*
     * There is one tip surface, and the second tip takes the first one's place on it rather than
     * opening a second one beside it: the surface is the same widget, it now points at the tip that
     * is up, and what it says is that tip's sentence and no longer the previous one's.
     */
    EXPECT_EQ(first, test.tips()->getPopover()) << "a tip is one surface, reused, not a pile of them";
    EXPECT_EQ(gtk_popover_get_relative_to(GTK_POPOVER(first)), test.anchor());
    EXPECT_EQ(shownSentence(test.tips()), TipService::textOf(TipService::Tip::FavoritePresets));

    test.tips()->dismiss();
    settle();
}

/// Escape puts the tip away from the keyboard.
void theKeyboardPutsTheTipAway(TipServiceFixture& test) {
    test.showToolProperties();
    ASSERT_TRUE(test.tips()->isShown());

    GtkWidget* dismiss = test.tips()->getDismissButton();
    gtk_widget_grab_focus(dismiss);
    settle();
    ASSERT_EQ(test.focused(), dismiss) << "the button can be reached with the keyboard";

    TipServiceFixture::pressKeyWhereTheKeyboardIs(test.win->getWindow(), GDK_KEY_Escape);
    settle();
    EXPECT_FALSE(test.tips()->isShown()) << "Escape puts the tip away";
    EXPECT_TRUE(test.settings()->hasSeenTip(TipService::idOf(TipService::Tip::ToolProperties)));
}

/// Reaching for a favourite preset is the moment the tip about the strip is about, and it happens once.
void pickingAFavouritePresetOffersItsTipOnce(TipServiceFixture& test) {
    TipService* tips = test.tips();
    ASSERT_FALSE(tips->isShown()) << "nothing is offered before anything happens";

    GtkWidget* strip = test.showFavouritePresets();
    ASSERT_NE(strip, nullptr);
    ASSERT_EQ(gtk_widget_get_toplevel(strip), test.win->getWindow())
            << "the strip is in the window whose tips are looked up from it";
    EXPECT_FALSE(tips->isShown()) << "having favourites is not the moment; reaching for one is";

    GtkWidget* button = favouriteButton(strip);
    ASSERT_NE(button, nullptr) << "the strip has a favourite to reach for";
    gtk_button_clicked(GTK_BUTTON(button));
    settle();
    EXPECT_EQ(test.adapter->getState().toolType, TOOL_PEN) << "the click applied the favourite preset";

    ASSERT_TRUE(tips->isShown()) << "picking a favourite is what the tip is about";
    EXPECT_EQ(*tips->shownTip(), TipService::Tip::FavoritePresets);
    // Anchored to the strip, which is what the tip is about: applying the preset rebuilds the strip's
    // buttons, so a tip pointing at the button that was clicked would point at a widget that is gone.
    EXPECT_EQ(gtk_popover_get_relative_to(GTK_POPOVER(tips->getPopover())), strip);

    tips->dismiss();
    settle();
    EXPECT_TRUE(test.settings()->hasSeenTip(TipService::idOf(TipService::Tip::FavoritePresets)));

    // Told once: the next favourite the user picks says nothing.
    GtkWidget* afterRebuild = favouriteButton(strip);
    ASSERT_NE(afterRebuild, nullptr) << "the strip still has its favourite after applying it";
    gtk_button_clicked(GTK_BUTTON(afterRebuild));
    settle();
    EXPECT_FALSE(tips->isShown()) << "a user who has been told this tip is not told it again";
}

/// The tips switch covers this one too: with the tips off, reaching for a favourite says nothing.
void favouritesSayNothingWhenTheTipsAreOff(TipServiceFixture& test) {
    GtkWidget* strip = test.showFavouritePresets();
    ASSERT_NE(strip, nullptr);

    GtkWidget* button = favouriteButton(strip);
    ASSERT_NE(button, nullptr);
    gtk_button_clicked(GTK_BUTTON(button));
    settle();
    EXPECT_FALSE(test.tips()->isShown()) << "the global switch means nothing is offered at all";
}

const TipScenario TIP_SCENARIOS[] = {
        {"theTriggerOffersTheTipOnce", freshProfile, theTriggerOffersTheTipOnce},
        {"aTipOnceDismissedDoesNotComeBack",
         [](Settings& settings) {
             freshProfile(settings);
             settings.markTipSeen(TipService::idOf(TipService::Tip::ToolProperties));
         },
         aTipOnceDismissedDoesNotComeBack},
        {"tipsCanBeTurnedOffGlobally",
         [](Settings& settings) {
             freshProfile(settings);
             settings.setInterfaceTipsEnabled(false);
         },
         tipsCanBeTurnedOffGlobally},
        {"resettingBringsTheTipsBack",
         [](Settings& settings) {
             freshProfile(settings);
             settings.markTipSeen(TipService::idOf(TipService::Tip::ToolProperties));
             settings.resetInterfaceTips();
         },
         resettingBringsTheTipsBack},
        {"theNewestTipIsTheOnlyOneUp", freshProfile, theNewestTipIsTheOnlyOneUp},
        {"theKeyboardPutsTheTipAway", freshProfile, theKeyboardPutsTheTipAway},
        {"pickingAFavouritePresetOffersItsTipOnce", freshProfile, pickingAFavouritePresetOffersItsTipOnce},
        {"favouritesSayNothingWhenTheTipsAreOff",
         [](Settings& settings) {
             freshProfile(settings);
             settings.setInterfaceTipsEnabled(false);
         },
         favouritesSayNothingWhenTheTipsAreOff},
};

}  // namespace

INSTANTIATE_TEST_SUITE_P(TipScenarios, TipServiceFixture, ::testing::ValuesIn(TIP_SCENARIOS));

/// The scenarios are the tests: each one runs the window, the profile and what the user does.
TEST_P(TipServiceFixture, theScenario) {}

/*
 * A tip's id is what the profile remembers, so it is the part that must not change: rewording a tip
 * is not telling the user something new. These are the ids that were written into profiles, and
 * every tip has one of its own.
 */
TEST(TipIds, everyTipHasAStableIdOfItsOwnAndASentence) {
    const TipService::Tip tips[] = {TipService::Tip::ToolProperties, TipService::Tip::FavoritePresets,
                                    TipService::Tip::PageMultiSelect};

    std::vector<std::string> ids;
    for (TipService::Tip tip: tips) {
        const std::string id = TipService::idOf(tip);
        EXPECT_FALSE(id.empty());
        EXPECT_EQ(std::find(ids.begin(), ids.end(), id), ids.end()) << id << " is the id of two tips";
        ids.emplace_back(id);
        EXPECT_STRNE(TipService::textOf(tip), "") << id << " says something";
    }

    EXPECT_STREQ(TipService::idOf(TipService::Tip::ToolProperties), "tool-properties");
    EXPECT_STREQ(TipService::idOf(TipService::Tip::FavoritePresets), "favorite-presets");
    EXPECT_STREQ(TipService::idOf(TipService::Tip::PageMultiSelect), "page-multi-select");
}

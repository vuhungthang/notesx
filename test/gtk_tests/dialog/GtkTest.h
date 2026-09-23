//
// Created by hermannt on 11.06.23.
//

#ifndef XOURNALPP_GTKTEST_H
#define XOURNALPP_GTKTEST_H

#include <memory>  // for unique_ptr
#include <string>

#include <gtest/gtest.h>
#include <gtk/gtk.h>

#include "filesystem.h"  // for fs::path

class GtkTest: public ::testing::Test {
protected:
    GtkApplication* app;
    int argn;
    char** argv;

    GtkTest();
    ~GtkTest() override;

    virtual void runTest(GtkApplication* app) = 0;

    // Setting up the testing environment
    void SetUp() override;

    /*
     * Checks that the case was kept out of the user's profile (see ProfileSandbox) and removes the
     * folder it was given, before the next case runs.
     */
    void TearDown() override;

    // This the callback in which the actual test is run
    // It needs to be a callback because it requires the GtkApplication to be running already.
    static void applicationCallback(GtkApplication* app, gpointer userData);

    /*
     * The settings file of the private configuration folder this case runs against. A case that
     * wants to say what the application stored reads this file; the user's own profile is never it.
     */
    [[nodiscard]] auto testSettingsFile() const -> fs::path;

    /*
     * The user's own settings file, as it was before this case redirected the configuration folder.
     * A case that has to speak about the profile the case must not touch reads this.
     */
    [[nodiscard]] auto userSettingsFile() const -> const fs::path&;

    /// Whether the user's own settings file still holds what it held before the case started.
    [[nodiscard]] auto userProfileUntouched() const -> bool;

private:
    class ProfileSandbox;
    std::unique_ptr<ProfileSandbox> profileSandbox;
};

#endif  // XOURNALPP_GTKTEST_H

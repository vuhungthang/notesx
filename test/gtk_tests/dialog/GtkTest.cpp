//
// Created by hermannt on 11.06.23.
//

#include "GtkTest.h"

#include <cctype>    // for isalnum
#include <fstream>   // for ifstream
#include <iterator>  // for istreambuf_iterator
#include <optional>  // for optional
#include <string>
#include <system_error>
#include <utility>

#include "util/PathUtil.h"  // for getConfigFolder, setConfigFolderOverride
#include "util/Util.h"      // for getPid

#include "config-dev.h"  // for SETTINGS_XML_FILE

namespace {
namespace fs = std::filesystem;

/// The bytes of a file, or nothing when it is not there.
auto readIfExists(const fs::path& path) -> std::optional<std::string> {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

/// A name that can be part of a directory name whatever the case is called.
auto sanitize(std::string name) -> std::string {
    for (char& c: name) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0) {
            c = '_';
        }
    }
    return name;
}
}  // namespace

/*
 * The private configuration folder one case runs against.
 *
 * A GTK case starts a real Control, and a real Control reads and writes the profile. A case that
 * ran against the user's own profile would leave its test values in it - a scribble-to-erase case
 * turned the gesture on in the real ~/.config/xournalpp/settings.xml - which is exactly what must
 * not happen to a setting that is meant to stay off until the user turns it on.
 *
 * Each case therefore gets a folder of its own under the temporary directory and hands it to the
 * application through Util::setConfigFolderOverride(). The environment is not used: GLib caches the
 * user configuration directory for the lifetime of the process, so an XDG_CONFIG_HOME set after
 * start-up is not honoured and would leave the case reading and writing the real profile.
 *
 * Before the redirect, the user's own settings file is recorded, and the tear-down compares it
 * against that record, so a case that somehow reached the real profile says so rather than passing
 * quietly. The folder is made in the constructor and removed in the destructor, which run at the
 * start and the end of the case whatever the case's assertions do.
 */
class GtkTest::ProfileSandbox {
public:
    explicit ProfileSandbox(const std::string& caseName) {
        this->userSettings = Util::getConfigFolder() / SETTINGS_XML_FILE;
        this->userSettingsBefore = readIfExists(this->userSettings);

        this->folder = fs::temp_directory_path() /
                       ("xournalpp-gtk-test-" + sanitize(caseName) + "-" + std::to_string(Util::getPid()));
        std::error_code ec;
        fs::remove_all(this->folder, ec);
        fs::create_directories(this->folder, ec);
        Util::setConfigFolderOverride(this->folder);
    }

    ~ProfileSandbox() {
        Util::setConfigFolderOverride({});
        std::error_code ec;
        fs::remove_all(this->folder, ec);
    }

    ProfileSandbox(const ProfileSandbox&) = delete;
    auto operator=(const ProfileSandbox&) -> ProfileSandbox& = delete;
    ProfileSandbox(ProfileSandbox&&) = delete;
    auto operator=(ProfileSandbox&&) -> ProfileSandbox& = delete;

    /// The settings file of the folder this case runs against; the application writes here.
    [[nodiscard]] auto settingsFile() const -> fs::path { return this->folder / SETTINGS_XML_FILE; }

    /// The user's own settings file, as it was before the case redirected the configuration folder.
    [[nodiscard]] auto userSettingsFile() const -> const fs::path& { return this->userSettings; }

    /// Whether the user's own settings file still holds exactly what it held before the case.
    [[nodiscard]] auto userProfileUntouched() const -> bool {
        return readIfExists(this->userSettings) == this->userSettingsBefore;
    }

private:
    fs::path folder;
    fs::path userSettings;
    std::optional<std::string> userSettingsBefore;
};

// Out of line: the profile sandbox is only defined in this file, so the unique_ptr cannot destroy it
// from the header.
GtkTest::GtkTest() = default;
GtkTest::~GtkTest() = default;

// Setting up the testing environment
void GtkTest::SetUp() {
    // Before anything is built: the profile this case is allowed to write is its own.
    const ::testing::TestInfo* info = ::testing::UnitTest::GetInstance()->current_test_info();
    const std::string caseName =
            info != nullptr ? std::string(info->test_suite_name()) + "." + info->name() : std::string("unnamed");
    this->profileSandbox = std::make_unique<ProfileSandbox>(caseName);

    argn = 1;
    argv = new char*[2];
    argv[0] = strdup("xournalpp_test");
    argv[1] = nullptr;
    app = gtk_application_new("com.github.xournalpp.xournalpp.test", G_APPLICATION_FLAGS_NONE);
    g_signal_connect(app, "activate", G_CALLBACK(applicationCallback), this);
    g_application_run(G_APPLICATION(app), argn, argv);
}

// This the callback in which the actual test is run
// It needs to be a callback because it requires the GtkApplication to be running already.
void GtkTest::applicationCallback(GtkApplication* app, gpointer userData) {
    auto* test = static_cast<GtkTest*>(userData);

    // run the actual test
    test->runTest(app);

    // Quit the application to avoid waiting indefinitely fo the test to finish
    g_application_quit(G_APPLICATION(app));
}

void GtkTest::TearDown() {
    if (this->profileSandbox != nullptr) {
        EXPECT_TRUE(this->profileSandbox->userProfileUntouched())
                << "this case left the user's profile changed: " << this->profileSandbox->userSettingsFile()
                << ". A GTK test must run against the configuration folder this fixture gives it.";
    }
    // Puts the platform's folder back and removes the case's own folder, whatever the assertions did.
    this->profileSandbox.reset();
}

auto GtkTest::testSettingsFile() const -> fs::path {
    return this->profileSandbox != nullptr ? this->profileSandbox->settingsFile() : fs::path{};
}

auto GtkTest::userSettingsFile() const -> const fs::path& {
    static const fs::path none;
    return this->profileSandbox != nullptr ? this->profileSandbox->userSettingsFile() : none;
}

auto GtkTest::userProfileUntouched() const -> bool {
    return this->profileSandbox == nullptr || this->profileSandbox->userProfileUntouched();
}

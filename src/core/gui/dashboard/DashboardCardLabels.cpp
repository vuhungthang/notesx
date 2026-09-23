#include "DashboardCardLabels.h"

#include <chrono>  // for duration_cast
#include <ctime>   // for localtime_r, strftime
#include <string>  // for string

#include "util/PathUtil.h"  // for clearExtensions
#include "util/i18n.h"      // for _, _F, FS

using namespace xoj::dashboard;
using namespace xoj::dashboardcard;

namespace {

/// A time as a date, for a file that is older than an interval would be useful for.
auto formatDate(const TimePoint& time) -> std::string {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char buffer[64] = {};
    if (std::strftime(buffer, sizeof(buffer), "%x", &local) == 0) {
        return {};
    }
    return buffer;
}

/// How the card describes a time, or an empty string when there is none to describe.
auto describeTime(const std::optional<TimePoint>& time, const TimePoint& now) -> std::string {
    if (!time.has_value()) {
        return {};
    }

    const auto age = now - *time;
    if (age < std::chrono::seconds(0)) {
        return _("in the future");
    }
    if (age < std::chrono::minutes(1)) {
        return _("just now");
    }
    if (age < std::chrono::hours(1)) {
        return FS(_F("{1} minutes ago") %
                  std::to_string(std::chrono::duration_cast<std::chrono::minutes>(age).count()));
    }
    if (age < std::chrono::hours(24)) {
        return FS(_F("{1} hours ago") % std::to_string(std::chrono::duration_cast<std::chrono::hours>(age).count()));
    }
    if (age < std::chrono::hours(24 * 7)) {
        return FS(_F("{1} days ago") %
                  std::to_string(std::chrono::duration_cast<std::chrono::hours>(age / 24).count()));
    }
    return formatDate(*time);
}

/// Why a file cannot be opened, or an empty string when it can.
auto describeLocation(DocumentCard::Location location) -> std::string {
    switch (location) {
        case DocumentCard::Location::Present:
            return {};
        case DocumentCard::Location::Missing:
            return _("the file is not there any more");
        case DocumentCard::Location::NotARegularFile:
            return _("this is not a file");
        case DocumentCard::Location::Unreadable:
            return _("the file cannot be read");
    }
    return {};
}

/// What the dashboard knows about the file's first page.
auto describePreview(DocumentCard::Preview preview) -> std::string {
    switch (preview) {
        case DocumentCard::Preview::Unknown:
            return {};
        case DocumentCard::Preview::Available:
            return {};
        case DocumentCard::Preview::None:
            return _("no preview");
        case DocumentCard::Preview::Corrupt:
            return _("preview unavailable");
    }
    return {};
}

/// The name without its extension: cards are read at a glance, and ".xopp" is the same for all.
auto withoutExtension(const fs::path& path) -> fs::path {
    fs::path name = path.filename();
    Util::clearExtensions(name);
    return name;
}

}  // namespace

auto xoj::dashboardcard::formatWhen(const TimePoint& time, const TimePoint& now) -> std::string {
    return describeTime(time, now);
}

auto xoj::dashboardcard::buildTitle(const DocumentCard& card) -> std::string {
    const fs::path stem = withoutExtension(card.path);
    const std::string name = stem.empty() ? card.displayName : std::string(char_cast(stem.u8string()));
    return name.empty() ? card.displayName : name;
}

auto xoj::dashboardcard::buildStateText(const DocumentCard& card) -> std::string {
    const std::string location = describeLocation(card.location);
    if (!location.empty()) {
        return location;
    }
    if (card.type == DocumentCard::Type::Unsupported) {
        return _("the editor cannot open this kind of file");
    }
    return {};
}

auto xoj::dashboardcard::buildFolderText(const DocumentCard& card) -> std::string {
    const fs::path folder = card.path.parent_path();
    if (folder.empty()) {
        return {};
    }
    return std::string(char_cast(folder.u8string()));
}

auto xoj::dashboardcard::buildAccessibleName(const DocumentCard& card, const TimePoint& now) -> std::string {
    std::string name = card.displayName.empty() ? buildTitle(card) : card.displayName;

    if (card.pinned) {
        name += ", " + std::string(_("pinned"));
    }

    if (card.type == DocumentCard::Type::Pdf) {
        name += ", " + std::string(_("PDF"));
    }

    const std::string state = buildStateText(card);
    if (!state.empty()) {
        name += ", " + state;
    }

    if (const std::string when = describeTime(card.modifiedTime, now); !when.empty()) {
        name += ", " + std::string(FS(_F("modified {1}") % when));
    }

    if (const std::string preview = describePreview(card.preview); !preview.empty()) {
        name += ", " + preview;
    }

    return name;
}

auto xoj::dashboardcard::buildMetadata(const DocumentCard& card, const TimePoint& now) -> std::string {
    if (const std::string state = buildStateText(card); !state.empty()) {
        return state;
    }

    std::string text = buildFolderText(card);
    if (const std::string when = describeTime(card.modifiedTime, now); !when.empty()) {
        const std::string modified = FS(_F("modified {1}") % when);
        text = text.empty() ? modified : text + " \u00b7 " + modified;
    }
    return text;
}

auto xoj::dashboardcard::buildRecoveryAccessibleName(const RecoveryCard& card, const TimePoint& now) -> std::string {
    std::string name;
    if (card.originalExists) {
        name = FS(_F("Recovered copy of {1}") % std::string(char_cast(card.originalPath.filename().u8string())));
    } else if (!card.originalPath.empty()) {
        name = FS(_F("Recovered copy of {1}, which is not there any more") %
                  std::string(char_cast(card.originalPath.filename().u8string())));
    } else {
        name = _("Unsaved work");
    }

    if (const std::string when = describeTime(card.recoveryTime, now); !when.empty()) {
        name += ", " + when;
    }

    if (!card.openable()) {
        name += ", " + std::string(_("cannot be opened"));
    }
    return name;
}

auto xoj::dashboardcard::buildRecoveryMetadata(const RecoveryCard& card, const TimePoint& now) -> std::string {
    std::string text;
    if (!card.originalPath.empty()) {
        text = buildFolderText(DocumentCard{.path = card.originalPath, .displayName = {}});
    }

    if (!card.openable()) {
        text = card.error.empty() ? std::string(_("cannot be opened")) : card.error;
        return text;
    }

    if (card.newerThanOriginal) {
        text += (text.empty() ? "" : " \u00b7 ") + std::string(_("newer than the file it came from"));
    }
    return text;
}

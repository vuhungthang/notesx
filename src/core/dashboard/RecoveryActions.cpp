#include "RecoveryActions.h"

#include <system_error>  // for error_code
#include <utility>       // for move

#include "dashboard/DashboardTypes.h"  // for canonicalPath
#include "util/i18n.h"                 // for _, _F, FS

using namespace xoj::dashboard;

namespace {

/// Whether a path is there, without letting a failure to look throw.
auto pathExists(const fs::path& path) -> bool {
    std::error_code error;
    return fs::exists(path, error) && !error;
}

}  // namespace

auto RecoveryActions::isOriginal(const RecoveryCard& card, const fs::path& target) -> bool {
    if (card.originalPath.empty() || target.empty()) {
        return false;
    }
    // Compared as the filesystem sees them, so a link to the document is not read as a different
    // file from the document itself.
    return canonicalPath(card.originalPath) == canonicalPath(target);
}

auto RecoveryActions::copyTo(const RecoveryCard& card, const fs::path& target, bool allowOriginal, std::string& error)
        -> bool {
    error.clear();

    if (target.empty()) {
        error = _("No destination was chosen.");
        return false;
    }

    if (!allowOriginal && isOriginal(card, target)) {
        error = FS(_F("That is the document this copy was recovered from:\n\"{1}\"\n"
                      "Use \"Save as\" and choose another name, or open the copy and save it "
                      "where you want it.") %
                   card.originalPath.u8string());
        return false;
    }

    std::error_code code;
    if (!fs::is_regular_file(card.recoveryPath, code) || code) {
        error = FS(_F("The recovered copy is not there any more:\n\"{1}\"") % card.recoveryPath.u8string());
        return false;
    }

    // The copy is written under the name the user chose; nothing about the original is read or
    // written here.
    fs::copy_file(card.recoveryPath, target, fs::copy_options::overwrite_existing, code);
    if (code) {
        error = FS(_F("Failed to write \"{1}\":\n{2}") % target.u8string() % code.message());
        return false;
    }

    return true;
}

auto RecoveryActions::removeCopy(const RecoveryCard& card, std::string& error) -> bool {
    error.clear();

    if (card.recoveryPath.empty()) {
        error = _("There is no recovery file to delete.");
        return false;
    }

    if (!pathExists(card.recoveryPath)) {
        error = FS(_F("The recovered copy is not there any more:\n\"{1}\"") % card.recoveryPath.u8string());
        return false;
    }

    std::error_code code;
    if (!fs::remove(card.recoveryPath, code) || code) {
        error = code ? FS(_F("Failed to delete \"{1}\":\n{2}") % card.recoveryPath.u8string() % code.message()) :
                       FS(_F("Failed to delete \"{1}\"") % card.recoveryPath.u8string());
        return false;
    }

    return true;
}

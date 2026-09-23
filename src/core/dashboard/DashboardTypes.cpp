#include "DashboardTypes.h"

#include <array>         // for array
#include <cstddef>       // for size_t
#include <fstream>       // for ifstream
#include <system_error>  // for error_code

#include "control/RecoveryInventory.h"  // for RecoveryInventory
#include "util/PathUtil.h"              // for hasXournalFileExt, hasPdfFileExt
#include "util/i18n.h"                  // for _

using namespace xoj::dashboard;

namespace {

/// A type as a set of three, which is what the dashboard has to tell apart.
auto typeOf(const fs::path& path) -> DocumentCard::Type {
    if (Util::hasXournalFileExt(path)) {
        return DocumentCard::Type::Xournal;
    }
    if (Util::hasPdfFileExt(path)) {
        return DocumentCard::Type::Pdf;
    }
    return DocumentCard::Type::Unsupported;
}

/// Whether the file can actually be read, as opposed to merely being there.
auto isReadable(const fs::path& path) -> bool {
    std::ifstream stream(path, std::ios::binary);
    return stream.is_open();
}

auto displayNameOf(const fs::path& path) -> std::string {
    const std::string name = path.filename().string();
    return name.empty() ? path.string() : name;
}

}  // namespace

auto xoj::dashboard::allSections() -> const std::array<DashboardSection, DASHBOARD_SECTION_COUNT>& {
    static const std::array<DashboardSection, DASHBOARD_SECTION_COUNT> sections = {
            DashboardSection::Recovery,   DashboardSection::ContinueWorking, DashboardSection::Pinned,
            DashboardSection::Library,    DashboardSection::Templates};
    return sections;
}

auto xoj::dashboard::sectionKey(DashboardSection section) -> const char* {
    switch (section) {
        case DashboardSection::Recovery:
            return "recovery";
        case DashboardSection::ContinueWorking:
            return "continue-working";
        case DashboardSection::Pinned:
            return "pinned";
        case DashboardSection::Library:
            return "library";
        case DashboardSection::Templates:
            return "templates";
    }
    return "unknown";
}

auto xoj::dashboard::sectionTitle(DashboardSection section) -> std::string {
    switch (section) {
        case DashboardSection::Recovery:
            return _("Recover unsaved work");
        case DashboardSection::ContinueWorking:
            return _("Continue working");
        case DashboardSection::Pinned:
            return _("Pinned");
        case DashboardSection::Library:
            return _("Folders");
        case DashboardSection::Templates:
            return _("Start something new");
    }
    return {};
}

auto xoj::dashboard::sectionHint(DashboardSection section) -> std::string {
    switch (section) {
        case DashboardSection::Recovery:
            return {};
        case DashboardSection::ContinueWorking:
            return _("Notes you open or save are listed here. Open a file to start.");
        case DashboardSection::Pinned:
            return _("Pin a note to keep it here. Right-click a card, or use the pin button on it.");
        case DashboardSection::Library:
            return _("Add a folder to list the notes in it. The dashboard never changes what is in a folder.");
        case DashboardSection::Templates:
            return {};
    }
    return {};
}

auto xoj::dashboard::canonicalPath(const fs::path& path) -> fs::path {
    if (path.empty()) {
        return {};
    }

    std::error_code error;
    const fs::path absolute = fs::absolute(path, error);
    const fs::path& base = error ? path : absolute;

    error.clear();
    fs::path resolved = fs::weakly_canonical(base, error);
    if (!error && !resolved.empty()) {
        return resolved;
    }
    // A path that cannot be resolved - a broken link's target, a path on a volume that went away -
    // is still usable as long as it is spelled the same way every time.
    return base.lexically_normal();
}

auto xoj::dashboard::looksLikeDocumentName(const fs::path& path) -> bool {
    const std::string name = path.filename().string();
    if (name.empty()) {
        return false;
    }
    if (name.ends_with(xoj::safety::RecoveryInventory::TEMP_SUFFIX) ||
        name.ends_with(xoj::safety::RecoveryInventory::SWAP_SUFFIX)) {
        return false;
    }
    return !xoj::safety::RecoveryInventory::looksLikeAutosave(path);
}

auto xoj::dashboard::isSupportedDocument(const fs::path& path) -> bool {
    return Util::hasXournalFileExt(path) || Util::hasPdfFileExt(path);
}

auto DocumentCard::has(DashboardSection section) const -> bool {
    return std::find(sections.begin(), sections.end(), section) != sections.end();
}

auto DocumentCard::openable() const -> bool { return type != Type::Unsupported && location == Location::Present; }

auto DocumentCard::isXournalDocument() const -> bool { return type == Type::Xournal; }

auto DocumentCard::inspect(const fs::path& path) -> DocumentCard {
    DocumentCard card;
    card.path = canonicalPath(path);
    card.displayName = displayNameOf(card.path);
    card.type = typeOf(card.path);

    std::error_code error;
    const fs::file_status status = fs::symlink_status(card.path, error);
    if (error) {
        card.location = Location::Missing;
        return card;
    }
    if (!fs::exists(status)) {
        card.location = Location::Missing;
        return card;
    }
    if (!fs::is_regular_file(status)) {
        card.location = Location::NotARegularFile;
        return card;
    }

    error.clear();
    if (const auto writeTime = fs::last_write_time(card.path, error); !error) {
        card.modifiedTime = std::chrono::clock_cast<std::chrono::system_clock>(writeTime);
    }

    /*
     * A file that is there but cannot be read is reported as such rather than silently dropped: the
     * card has to be able to say why it will not open, and a permission that comes back is enough
     * for the next refresh to find the file again.
     */
    card.location = isReadable(card.path) ? Location::Present : Location::Unreadable;
    return card;
}

auto LibraryFolder::listable() const -> bool { return enabled && state == State::Ok; }

auto LibraryFolder::inspect(const fs::path& path) -> State {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error || !fs::exists(status)) {
        return State::Missing;
    }
    if (!fs::is_directory(status)) {
        return State::NotADirectory;
    }

    /*
     * Opening the folder is the only way to know whether it can be listed, and it has to be asked
     * without `skip_permission_denied`: the option means "do not report a folder I may not read",
     * which is exactly the answer wanted here.
     */
    error.clear();
    const fs::directory_iterator probe(path, error);
    if (error) {
        return State::Inaccessible;
    }
    return State::Ok;
}

auto RecoveryCard::openable() const -> bool { return validation == xoj::safety::RecoveryValidation::Ok; }

auto RecoveryCard::fromCandidate(const xoj::safety::RecoveryCandidate& candidate) -> RecoveryCard {
    RecoveryCard card;
    card.recoveryPath = candidate.recoveryPath;
    card.originalPath = candidate.originalPath;
    card.originalExists = candidate.originalExists;
    card.newerThanOriginal = candidate.newerThanOriginal;
    card.recoveryTime = candidate.recoveryTime;
    card.size = candidate.size;
    card.validation = candidate.validation;
    card.error = candidate.error;
    card.displayName = displayNameOf(candidate.recoveryPath);
    return card;
}

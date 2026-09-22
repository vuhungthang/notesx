#include "PageCardLabels.h"

#include "util/i18n.h"  // for _, _F, FS

using namespace xoj::pagecard;

auto xoj::pagecard::buildAccessibleName(size_t pageNumber, bool selected, bool current, bool loading, bool error)
        -> std::string {
    std::string name = FS(_F("Page {1}") % pageNumber);

    if (selected) {
        name += ", ";
        name += _("selected");
    }
    if (current) {
        name += ", ";
        name += _("current page");
    }

    // A thumbnail that could not be rendered is not loading any more, and it is worth saying so.
    if (error) {
        name += ", ";
        name += _("preview unavailable");
    } else if (loading) {
        name += ", ";
        name += _("loading");
    }

    return name;
}

auto xoj::pagecard::buildMetadata(size_t pageNumber, const std::string& pageName, bool error) -> std::string {
    std::string text = FS(_F("Page {1}") % pageNumber);

    // A page can carry a name - a PDF page label, for instance. Show it when it says more than the
    // number already does.
    if (!pageName.empty() && pageName != std::to_string(pageNumber)) {
        text += " \u00b7 " + pageName;
    }

    if (error) {
        text += " \u00b7 " + std::string(_("preview unavailable"));
    }

    return text;
}

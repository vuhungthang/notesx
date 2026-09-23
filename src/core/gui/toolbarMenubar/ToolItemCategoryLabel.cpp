#include "ToolItemCategoryLabel.h"

#include "util/i18n.h"  // for C_

namespace xoj::gui {

auto toolItemCategoryLabel(AbstractToolItem::Category category) -> const char* {
    using Cat = AbstractToolItem::Category;
    switch (category) {
        case Cat::AUDIO:
            return C_("Item category in toolbar customization dialog", "Audio");
        case Cat::COLORS:
            return C_("Item category in toolbar customization dialog", "Colors");
        case Cat::FILES:
            return C_("Item category in toolbar customization dialog", "Files");
        case Cat::MISC:
            return C_("Item category in toolbar customization dialog", "Miscellaneous");
        case Cat::NAVIGATION:
            return C_("Item category in toolbar customization dialog", "Navigation");
        case Cat::SELECTION:
            return C_("Item category in toolbar customization dialog", "Selection");
        case Cat::TOOLS:
            return C_("Item category in toolbar customization dialog", "Tools");
        case Cat::SEPARATORS:
            return C_("Item category in toolbar customization dialog", "Separators");
        case Cat::PLUGINS:
            return C_("Item category in toolbar customization dialog", "Plugins");
        case Cat::ENUMERATOR_COUNT:
            break;
    }
    return C_("Item category in toolbar customization dialog", "Miscellaneous");
}

}  // namespace xoj::gui

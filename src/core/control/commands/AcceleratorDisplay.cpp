#include "AcceleratorDisplay.h"

#include <algorithm>  // for find_if
#include <cctype>     // for tolower
#include <string>
#include <vector>

namespace xoj::command {

namespace {

/// A GDK key name that is not spelled the way a user reads it.
struct KeyName {
    const char* gdk;
    const char* display;
};

constexpr KeyName KEY_NAMES[] = {
        {"plus", "+"},
        {"minus", "-"},
        {"equal", "="},
        {"kp_add", "Num +"},
        {"kp_subtract", "Num -"},
        {"kp_multiply", "Num *"},
        {"kp_divide", "Num /"},
        {"kp_enter", "Num Enter"},
        {"page_up", "PageUp"},
        {"page_down", "PageDown"},
        {"return", "Enter"},
        {"escape", "Esc"},
        {"space", "Space"},
};

auto lower(std::string text) -> std::string {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

auto capitalize(std::string text) -> std::string {
    if (!text.empty()) {
        text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
    }
    return text;
}

/// A single key of an accelerator, as it is written on a keyboard.
auto displayKey(const std::string& gdkKey) -> std::string {
    if (gdkKey.empty()) {
        return {};
    }
    const std::string folded = lower(gdkKey);
    if (folded.size() == 1) {
        return capitalize(folded);
    }
    for (const KeyName& name: KEY_NAMES) {
        if (folded == name.gdk) {
            return name.display;
        }
    }
    // A key with no convention of its own is shown the way the accelerator wrote it - "F5" stays
    // "F5" - with its first letter capitalized when it has one. Something that is not a key at all
    // ("<Ctrl" with its '>' missing) is shown as it stands rather than mangled.
    std::string shown = gdkKey;
    const auto firstLetter = std::find_if(shown.begin(), shown.end(), [](unsigned char c) { return std::isalpha(c); });
    if (firstLetter != shown.end()) {
        *firstLetter = static_cast<char>(std::toupper(*firstLetter));
    }
    return shown;
}

/**
 * The modifiers of an accelerator, as the canonical names below rather than as the strings GTK was
 * given, so that a platform can render them in its own order and with its own spelling.
 */
struct Modifiers {
    bool control = false;
    bool shift = false;
    bool alt = false;
    bool command = false;  ///< Meta / Super / Mod4, the platform's "the other main" modifier
    bool hyper = false;
    std::vector<std::string> others;  ///< modifiers with no convention of their own, in order
};

auto modifiersOf(const std::vector<std::string>& names, AcceleratorPlatform platform) -> Modifiers {
    Modifiers modifiers;
    for (const std::string& name: names) {
        if (name == "ctrl" || name == "control") {
            modifiers.control = true;
        } else if (name == "shift") {
            modifiers.shift = true;
        } else if (name == "alt" || name == "mod1") {
            modifiers.alt = true;
        } else if (name == "primary") {
            /*
             * "Primary" is the modifier GTK registers this platform's control key under: an action
             * whose accelerator is written "<Ctrl>Z" is handed back by GTK as "<Primary>z". On
             * macOS the same modifier is the command key, which is why this is the one modifier
             * that cannot be decided without the platform.
             */
            if (platform == AcceleratorPlatform::MACOS) {
                modifiers.command = true;
            } else {
                modifiers.control = true;
            }
        } else if (name == "meta" || name == "super" || name == "mod4") {
            modifiers.command = true;
        } else if (name == "hyper") {
            modifiers.hyper = true;
        } else {
            modifiers.others.emplace_back(capitalize(name));
        }
    }
    return modifiers;
}

}  // namespace

auto formatAcceleratorForDisplay(std::string_view gtkAccelerator, AcceleratorPlatform platform) -> std::string {
    // <Ctrl><Shift>s: the modifiers are the leading <...> groups, everything after them is the key.
    std::vector<std::string> modifierNames;
    size_t position = 0;
    while (position < gtkAccelerator.size() && gtkAccelerator[position] == '<') {
        const size_t end = gtkAccelerator.find('>', position);
        if (end == std::string_view::npos) {
            break;
        }
        modifierNames.emplace_back(lower(std::string(gtkAccelerator.substr(position + 1, end - position - 1))));
        position = end + 1;
    }
    const std::string key(gtkAccelerator.substr(position));

    if (modifierNames.empty() && key.empty()) {
        return {};
    }

    const Modifiers modifiers = modifiersOf(modifierNames, platform);
    const std::string shownKey = displayKey(key);

    if (platform == AcceleratorPlatform::MACOS) {
        // The order macOS users read: control, option, shift, command.
        std::string shown;
        if (modifiers.control) {
            shown += "\u2303";
        }
        if (modifiers.alt) {
            shown += "\u2325";
        }
        if (modifiers.shift) {
            shown += "\u21e7";
        }
        if (modifiers.command) {
            shown += "\u2318";
        }
        if (modifiers.hyper) {
            shown += "\u2303\u2325\u21e7\u2318";
        }
        for (const std::string& other: modifiers.others) {
            shown += other + "+";
        }
        return shown + shownKey;
    }

    std::string shown;
    const auto append = [&shown](const std::string& part) {
        if (!shown.empty()) {
            shown += "+";
        }
        shown += part;
    };
    if (modifiers.control) {
        append("Ctrl");
    }
    if (modifiers.alt) {
        append("Alt");
    }
    if (modifiers.shift) {
        append("Shift");
    }
    if (modifiers.command) {
        append("Super");
    }
    if (modifiers.hyper) {
        append("Hyper");
    }
    for (const std::string& other: modifiers.others) {
        append(other);
    }
    append(shownKey);
    return shown;
}

}  // namespace xoj::command

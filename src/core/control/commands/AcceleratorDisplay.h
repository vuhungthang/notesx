/*
 * Xournal++
 *
 * How a GTK accelerator is spelled for the user (Plan 007)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */
#pragma once

#include <string>
#include <string_view>

namespace xoj::command {

/**
 * The platform whose conventions an accelerator is rendered with.
 *
 * It is a parameter rather than a compile time constant so that both spellings can be tested
 * wherever the tests run, and so that a reference that is exported or shown in a screenshot can be
 * rendered for the platform the reader is on.
 */
enum class AcceleratorPlatform {
    LINUX,  ///< "Ctrl+Shift+S"
    MACOS,  ///< the symbols macOS users read, e.g. the shift and command symbols before "S"
};

/// The conventions of the platform this binary runs on.
constexpr auto nativeAcceleratorPlatform() -> AcceleratorPlatform {
#ifdef __APPLE__
    return AcceleratorPlatform::MACOS;
#else
    return AcceleratorPlatform::LINUX;
#endif
}

/**
 * A GDK accelerator string as the user reads it, e.g. "<Ctrl><Shift>s" as "Ctrl+Shift+S".
 *
 * The input is the string GTK itself works with - the one written in ui/mainmenubar.xml or passed
 * to gtk_application_set_accels_for_action() - so the reference is a report of what is registered
 * and not a second list that has to be kept in step. A string that is not a well formed GDK
 * accelerator is returned as it stands: showing it beats dropping a shortcut the user can press.
 */
auto formatAcceleratorForDisplay(std::string_view gtkAccelerator,
                                 AcceleratorPlatform platform = nativeAcceleratorPlatform()) -> std::string;

}  // namespace xoj::command

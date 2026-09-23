/*
 * Xournal++
 *
 * The gesture preferences: versioned, migrated, off by default (Plan 008, step 2)
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <map>
#include <string>

#include "control/gestures/GestureRecognizer.h"

namespace xoj::gesture {

/**
 * Plan 008: what the profile remembers about gestures.
 *
 * One value type, one version, and one place that decides what a file written by an older or
 * newer version means. The defaults are the conservative ones and are the same for a fresh
 * profile and for a migrated one - in particular:
 *
 *  - Circle-to-select is off, and stays off, for every profile. The plan permits turning it on for
 *    new Focus profiles once validation says so; this tree has no such validation beyond the
 *    fixture corpus, which is a local gate and not evidence about handwriting in general, so it
 *    stays off (see the plan's note that off is the acceptable and safer default).
 *  - Scribble-to-erase is off. The plan keeps it off until its false-positive threshold is
 *    approved, which has not happened.
 *  - Touch tap undo/redo is off, because the touch infrastructure was not shown to distinguish a
 *    tap from a pan or a zoom reliably (Plan 008, step 6 stop condition).
 *
 * Nothing here is read from a background thread and nothing here touches a document: these are
 * plain values, read by GesturePolicy where the document is.
 */
struct GestureSettings {
    /// Version of the persisted shape. A later field addition should raise this and teach
    /// migrate() what the older version meant.
    static constexpr int STORAGE_VERSION = 1;

    /**
     * Whether the quick palette can be summoned.
     *
     * The plan says "on when explicitly bound": a button or a key that is not bound to it cannot
     * summon it, and this flag records that the user has bound one. The binding itself lives with
     * the other bindings (ButtonConfig / the action's accelerator), not here.
     */
    bool quickPaletteEnabled = false;

    /// Whether a two-/three-finger tap undoes/redoes. Off: the tap test did not pass.
    bool tapUndoRedoEnabled = false;

    /// Whether circling selects what it encloses. Off by default.
    bool circleToSelectEnabled = false;

    /// Whether scribbling erases the strokes it covers. Off by default.
    bool scribbleToEraseEnabled = false;

    /**
     * How sure a recognizer must be before its candidate is acted on, in [0, 1].
     *
     * This is the one sensitivity the user is given, and it only raises the floor the recognizers
     * already set: it can make a gesture harder to trigger, never easier than the recognizer's own
     * minimum. GesturePolicy applies it.
     */
    double circleConfidenceFloor = 0.5;
    double scribbleConfidenceFloor = 0.5;

    /// Whether the first successful gesture says so, briefly and undoably (Plan 008, step 7).
    bool feedbackEnabled = true;

    /// A fresh profile's settings: everything dangerous off.
    static auto defaults() -> GestureSettings;

    /**
     * Flat key/value form used for persistence. Absent keys fall back to the default, reading
     * ignores keys it does not know, and a file written by a newer version is not read at all -
     * migrate() returns the defaults instead, exactly as the tool presets do.
     */
    auto toAttributes() const -> std::map<std::string, std::string>;
    static auto fromAttributes(const std::map<std::string, std::string>& attributes) -> GestureSettings;

    /**
     * Read a stored profile.
     *
     * `storedVersion` is what the element claimed. A version this build does not understand means
     * the rest of the element is not read: guessing at a newer format is how a profile loses the
     * very safety this plan is about. Anything else is read field by field, with every field that
     * is missing left at its conservative default - which is what migrating an older file, written
     * before a field existed, means.
     */
    static auto migrated(int storedVersion, const std::map<std::string, std::string>& attributes) -> GestureSettings;

    bool operator==(const GestureSettings& other) const = default;
};

/// Whether the recognizer of a gesture may run at all, per the settings.
auto gestureEnabled(const GestureSettings& settings, GestureKind kind) -> bool;

}  // namespace xoj::gesture

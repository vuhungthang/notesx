/*
 * Xournal++
 *
 * Settings Dialog
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>  // for string
#include <vector>  // for vector

#include <gtk/gtk.h>  // for GtkWidget, GtkWindow

#include "audio/DeviceInfo.h"                    // for DeviceInfo
#include "control/gestures/GestureReference.h"   // for buildGestureReference (Plan 008, step 7)
#include "control/gestures/GestureSettings.h"    // for GestureSettings (Plan 008, step 2)
#include "control/settings/SettingsEnums.h"      // for WorkspaceMode
#include "control/tools/StrokeStabilizerEnum.h"  // for AveragingMethod, Pre...
#include "gui/Builder.h"
#include "util/raii/GtkWindowUPtr.h"

#include "ButtonConfigGui.h"
#include "LanguageConfigGui.h"
#include "LatexSettingsPanel.h"
#include "SettingsDialogPaletteTab.h"
#include "config-features.h"  // for ENABLE_AUDIO
#include "filesystem.h"       // for path

class Control;
class Settings;
class DeviceTestingArea;

struct Palette;

class SettingsDialog {
public:
    SettingsDialog(GladeSearchpath* gladeSearchPath, Settings* settings, Control* control,
                   const std::vector<fs::path>& paletteDirectories, std::function<void()> callback);
    ~SettingsDialog();

    inline GtkWindow* getWindow() const { return window.get(); }

private:
    void save();
    void setDpi(int dpi);

    /**
     * Set active regions
     */
    void enableWithCheckbox(const std::string& checkbox, const std::string& widget);
    void disableWithCheckbox(const std::string& checkbox, const std::string& widget);
    void enableWithEnabledCheckbox(const std::string& checkbox, const std::string& widget);

    /*
     * Listeners for changes to settings.
     */
    void customHandRecognitionToggled();
    void customStylusIconTypeChanged();

    /**
     * Update whether options can be selected, tooltips, etc. for
     * pressure sensitivity options (e.g. pressure multiplier).
     */
    void updatePressureSensitivityOptions();

    /**
     * Plan 002: the workspace the user picked in the dialog.
     */
    WorkspaceMode getWorkspaceMode();

    /**
     * Plan 002: show the menubar preference of the workspace the user just selected.
     */
    void updateWorkspaceMenubarCheckbox();

private:
    void load();
    void loadCheckbox(const char* name, bool value);
    bool getCheckbox(const char* name);

    /**
     * Plan 008, step 2: the gesture preferences.
     *
     * The widgets carry a value each and are read back on save. The two confidence fields are only
     * meaningful for a gesture that is on, so they follow their own toggle rather than sitting
     * there editable while doing nothing. The reset button puts the widget values back to the
     * conservative defaults without touching the profile: nothing is stored until the dialog is
     * saved, exactly as the other pages behave.
     */
    void showGestureSettings(const xoj::gesture::GestureSettings& gesture);
    void loadGestureSettings();
    void saveGestureSettings();
    void updateGestureSensitivity();
    void resetGestureSettingsToDefaults();

    /**
     * Plan 008, step 7: the gesture reference, on the page the gestures are set on.
     *
     * Built from buildGestureReference() over the live state - what the controls on this page say
     * right now - so what the user reads and what the application will do are the same thing, and it
     * moves when a control moves. It is a reference rather than a second set of controls: nothing in
     * it is editable, and it says for each gesture whether it is on, how it is reached and what it
     * will not do.
     */
    void renderGestureReference();

    void loadSlider(const char* name, double value);
    double getSlider(const char* name);

    void initMouseButtonEvents(GladeSearchpath* gladeSearchPath);

    void showStabilizerAvMethodOptions(StrokeStabilizer::AveragingMethod method);
    void showStabilizerPreprocessorOptions(StrokeStabilizer::Preprocessor preprocessor);

private:
    Settings* settings = nullptr;
    Control* control = nullptr;
    GtkWidget* callib = nullptr;
    int dpi = 72;

#ifdef ENABLE_AUDIO
    std::vector<DeviceInfo> audioInputDevices;
    std::vector<DeviceInfo> audioOutputDevices;
#endif

    Builder builder;
    xoj::util::GtkWindowUPtr window;

    std::unique_ptr<DeviceTestingArea> deviceTestingArea;

    LanguageConfigGui languageConfig;
    std::vector<std::unique_ptr<ButtonConfigGui>> buttonConfigs;

    LatexSettingsPanel latexPanel;
    SettingsDialogPaletteTab paletteTab;

    std::function<void()> callback;
};

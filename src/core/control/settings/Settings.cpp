#include "Settings.h"

#include <algorithm>    // for max
#include <cstdint>      // for uint32_t, int32_t
#include <cstdio>       // for sscanf, size_t
#include <cstdlib>      // for atoi
#include <cstring>      // for strcmp
#include <exception>    // for exception
#include <type_traits>  // for add_const<>::type
#include <utility>      // for pair, move, make_...

#include <libxml/globals.h>    // for xmlFree, xmlInden...
#include <libxml/parser.h>     // for xmlKeepBlanksDefault
#include <libxml/xmlstring.h>  // for xmlStrcmp, xmlChar

#include "control/DeviceListHelper.h"               // for InputDevice
#include "control/ToolEnums.h"                      // for ERASER_TYPE_NONE
#include "control/settings/LatexSettings.h"         // for LatexSettings
#include "control/settings/PageTemplateSettings.h"  // for PageTemplateSettings
#include "control/settings/SettingsEnums.h"         // for InputDeviceTypeOp...
#include "gui/toolbarMenubar/model/ColorPalette.h"  // for Palette
#include "model/FormatDefinitions.h"                // for FormatUnits, XOJ_...
#include "util/Color.h"
#include "util/PathUtil.h"    // for getConfigFile
#include "util/Util.h"        // for PRECISION_FORMAT_...
#include "util/i18n.h"        // for _
#include "util/safe_casts.h"  // for as_unsigned
#include "util/utf8_view.h"   // for utf8_view

#include "ButtonConfig.h"  // for ButtonConfig
#include "config-dev.h"    // for PALETTE_FILE
#include "config-dev.h"
#include "filesystem.h"  // for path, exists


using std::string;

constexpr auto const* DEFAULT_FONT = "Sans";
constexpr auto DEFAULT_FONT_SIZE = 12;
constexpr auto DEFAULT_TOOLBAR = "Portrait";
/// Plan 002: the compact toolbar a fresh profile starts with (see toolbar.ini.in)
constexpr auto FOCUS_TOOLBAR = "Focus";

#define SAVE_BOOL_PROP(var) xmlNode = saveProperty((const char*)#var, (var) ? "true" : "false", root)
#define SAVE_STRING_PROP(var) xmlNode = saveProperty((const char*)#var, (var).empty() ? "" : (var).data(), root)
#define SAVE_FONT_PROP(var) xmlNode = saveProperty((const char*)#var, var.asString().c_str(), root)
#define SAVE_INT_PROP(var) xmlNode = saveProperty((const char*)#var, var, root)
#define SAVE_UINT_PROP(var) xmlNode = savePropertyUnsigned((const char*)#var, var, root)
#define SAVE_DOUBLE_PROP(var) xmlNode = savePropertyDouble((const char*)#var, var, root)
#define ATTACH_COMMENT(var)                     \
    com = xmlNewComment((const xmlChar*)(var)); \
    xmlAddPrevSibling(xmlNode, com);

Settings::Settings(fs::path filepath): filepath(std::move(filepath)) { loadDefault(); }

Settings::~Settings() = default;

void Settings::loadDefault() {
    this->pressureSensitivity = true;
    this->minimumPressure = 0.05;
    this->pressureMultiplier = 1.0;
    this->pressureGuessing = false;
    this->zoomGesturesEnabled = true;

    this->maximized = false;
    this->showPairedPages = false;
    this->showPageShadow = true;
    this->presentationMode = false;

    this->numColumns = 1;  // only one of these applies at a time
    this->numRows = 1;
    this->viewFixedRows = false;

    this->layoutVertical = false;
    this->layoutRightToLeft = false;
    this->layoutBottomToTop = false;

    this->numPairsOffset = 1;

    this->emptyLastPageAppend = EmptyLastPageAppendType::Disabled;

    this->edgePanSpeed = 20.0;
    this->edgePanMaxMult = 5.0;

    this->zoomStep = 10.0;
    this->zoomStepScroll = 2.0;

    this->displayDpi = -1;  // Automatic detection

    this->font.setName(DEFAULT_FONT);
    this->font.setSize(DEFAULT_FONT_SIZE);

    this->mainWndWidth = 800;
    this->mainWndHeight = 600;

    this->fullscreenActive = false;

    this->showSidebar = true;
    this->sidebarWidth = 150;
    this->sidebarNumberingStyle = SidebarNumberingStyle::DEFAULT;
    this->sidebarPageLayoutMode = SidebarPageLayoutMode::DEFAULT;

    this->showToolbar = true;
    // Plan 002: a fresh profile starts in the Focus workspace. Existing profiles are
    // migrated to Classic in resolveWorkspaceAfterLoad() so an update never silently
    // changes an established user's layout.
    this->workspaceMode = WorkspaceMode::FOCUS;
    this->focusToolbar = FOCUS_TOOLBAR;
    this->classicToolbar = DEFAULT_TOOLBAR;
    this->selectedToolbar = this->focusToolbar;

    this->sidebarOnRight = false;

    this->scrollbarOnLeft = false;

    // Plan 002: Focus starts without the traditional menubar; Classic keeps the
    // pre-existing default of a visible menubar.
    this->focusMenubarVisible = false;
    this->classicMenubarVisible = true;
    this->menubarVisible = this->focusMenubarVisible;

    this->autoloadMostRecent = false;
    this->autoloadPdfXoj = true;

    this->stylusCursorType = STYLUS_CURSOR_DOT;
    this->eraserVisibility = ERASER_VISIBILITY_ALWAYS;
    // Lucide is the default for new profiles only; an existing saved preference
    // is loaded from the settings file and left untouched.
    this->iconTheme = ICON_THEME_LUCIDE;
    this->themeVariant = THEME_VARIANT_USE_SYSTEM;
    this->highlightPosition = false;
    this->cursorHighlightColor = 0x80FFFF00;  // Yellow with 50% opacity
    this->cursorHighlightRadius = 30.0;
    this->cursorHighlightBorderColor = 0x800000FF;  // Blue with 50% opacity
    this->cursorHighlightBorderWidth = 0.0;
    this->useStockIcons = false;
    this->scrollbarHideType = SCROLLBAR_HIDE_NONE;
    this->disableScrollbarFadeout = false;
    this->disableAudio = false;

    // Set this for autosave frequency in minutes.
    this->autosaveTimeout = 3;
    this->autosaveEnabled = true;

    this->addHorizontalSpace = false;
    this->addHorizontalSpaceAmountRight = 150;
    this->addHorizontalSpaceAmountLeft = 150;
    this->addVerticalSpace = false;
    this->addVerticalSpaceAmountAbove = 150;
    this->addVerticalSpaceAmountBelow = 150;

    this->unlimitedScrolling = false;

    // Drawing direction emulates modifier keys
    this->drawDirModsRadius = 50;
    this->drawDirModsEnabled = false;

    this->snapRotation = true;
    this->snapRotationTolerance = 0.30;

    this->snapGrid = true;
    this->snapGridTolerance = 0.50;
    this->snapGridSize = DEFAULT_GRID_SIZE;

    this->strokeRecognizerMinSize = 40;

    this->touchDrawing = false;
    this->gtkTouchInertialScrolling = true;

    this->defaultSaveName = xoj::util::utf8(_("%F-Note-%H-%M")).str();
    this->defaultPdfExportName = xoj::util::utf8(_("%{name}_annotated")).str();

    // Eraser
    this->buttonConfig[BUTTON_ERASER] = std::make_unique<ButtonConfig>(TOOL_ERASER, Colors::black, TOOL_SIZE_NONE,
                                                                       DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);
    // Left button
    this->buttonConfig[BUTTON_MOUSE_LEFT] = std::make_unique<ButtonConfig>(TOOL_NONE, Colors::black, TOOL_SIZE_NONE,
                                                                           DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);
    // Middle button
    this->buttonConfig[BUTTON_MOUSE_MIDDLE] = std::make_unique<ButtonConfig>(TOOL_HAND, Colors::black, TOOL_SIZE_NONE,
                                                                             DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);
    // Right button
    this->buttonConfig[BUTTON_MOUSE_RIGHT] = std::make_unique<ButtonConfig>(TOOL_NONE, Colors::black, TOOL_SIZE_NONE,
                                                                            DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);
    // 4th button
    this->buttonConfig[BUTTON_MOUSE_4] = std::make_unique<ButtonConfig>(TOOL_NONE, Colors::black, TOOL_SIZE_NONE,
                                                                        DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);
    // 5th button
    this->buttonConfig[BUTTON_MOUSE_5] = std::make_unique<ButtonConfig>(TOOL_NONE, Colors::black, TOOL_SIZE_NONE,
                                                                        DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);
    // Touch
    this->buttonConfig[BUTTON_TOUCH] = std::make_unique<ButtonConfig>(TOOL_NONE, Colors::black, TOOL_SIZE_NONE,
                                                                      DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);
    // Default config
    this->buttonConfig[BUTTON_DEFAULT] = std::make_unique<ButtonConfig>(TOOL_PEN, Colors::black, TOOL_SIZE_FINE,
                                                                        DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);
    // Pen button 1
    this->buttonConfig[BUTTON_STYLUS_ONE] = std::make_unique<ButtonConfig>(TOOL_NONE, Colors::black, TOOL_SIZE_NONE,
                                                                           DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);
    // Pen button 2
    this->buttonConfig[BUTTON_STYLUS_TWO] = std::make_unique<ButtonConfig>(TOOL_NONE, Colors::black, TOOL_SIZE_NONE,
                                                                           DRAWING_TYPE_DEFAULT, ERASER_TYPE_NONE);

    // default view modes
    this->activeViewMode = PresetViewModeIds::VIEW_MODE_DEFAULT;
    this->viewModes =
            std::vector<ViewMode>{VIEW_MODE_STRUCT_DEFAULT, VIEW_MODE_STRUCT_FULLSCREEN, VIEW_MODE_STRUCT_PRESENTATION};

    this->touchZoomStartThreshold = 0.0;

    this->pageRerenderThreshold = 5.0;
    this->pdfPageCacheSize = 10;
    this->preloadPagesBefore = 3U;
    this->preloadPagesAfter = 5U;
    this->eagerPageCleanup = true;

    this->selectionBorderColor = Colors::red;
    this->selectionMarkerColor = Colors::xopp_cornflowerblue;
    this->activeSelectionColor = Colors::lawngreen;

    this->recolorParameters = {false, false, Recolor(ColorU8{198, 208, 245}, ColorU8{48, 52, 70})};

    this->backgroundColor = Colors::xopp_gainsboro02;

    // clang-format off
	this->pageTemplateSettings.parse("xoj/template\ncopyLastPageSettings=true\nsize=595.275591x841.889764\nbackgroundType=lined\nbackgroundColor=#ffffff\n");
    // clang-format on

#ifdef ENABLE_AUDIO
    this->audioSampleRate = 44100.0;
    this->audioInputDevice = AUDIO_INPUT_SYSTEM_DEFAULT;
    this->audioOutputDevice = AUDIO_OUTPUT_SYSTEM_DEFAULT;
    this->audioGain = 1.0;
    this->defaultSeekTime = 5;
#endif

    this->pluginEnabled = "";
    this->pluginDisabled = "";

    this->numIgnoredStylusEvents = 0;

#ifdef _WIN32
    // This option should be on on Windows:
    // GTK (at least until 3.24.49) only creates GDK_BUTTON_PRESS events on mouse-events or stylus-down-events
    this->inputSystemTPCButton = true;
#else
    this->inputSystemTPCButton = false;
#endif

    this->inputSystemDrawOutsideWindow = true;

    this->strokeFilterIgnoreTime = 150;
    this->strokeFilterIgnoreLength = 1;
    this->strokeFilterSuccessiveTime = 500;
    this->strokeFilterEnabled = false;
    this->doActionOnStrokeFiltered = false;
    this->trySelectOnStrokeFiltered = false;

    this->snapRecognizedShapesEnabled = false;
    this->restoreLineWidthEnabled = false;

    this->inTransaction = false;

    /**
     * Stabilizer related settings
     */
    this->stabilizerAveragingMethod = StrokeStabilizer::AveragingMethod::NONE;
    this->stabilizerPreprocessor = StrokeStabilizer::Preprocessor::NONE;
    this->stabilizerBuffersize = 20;
    this->stabilizerSigma = 0.5;
    this->stabilizerDeadzoneRadius = 1.3;
    this->stabilizerCuspDetection = true;
    this->stabilizerDrag = 0.4;
    this->stabilizerMass = 5.0;
    this->stabilizerFinalizeStroke = true;
    /**/

    this->useSpacesForTab = false;
    this->numberOfSpacesForTab = 4;

    this->laserPointerFadeOutTime = 500;

    this->colorPaletteSetting = Util::getBuiltInPaletteDirectoryPath() / DEFAULT_PALETTE_FILE;
}

auto Settings::loadViewMode(ViewModeId mode) -> bool {
    if (mode < 0 || mode >= viewModes.size()) {
        return false;
    }
    auto viewMode = viewModes.at(mode);
    fullscreenActive = viewMode.goFullscreen;
    menubarVisible = viewMode.showMenubar;
    showToolbar = viewMode.showToolbar;
    showSidebar = viewMode.showSidebar;
    this->activeViewMode = mode;
    return true;
}

auto Settings::getViewModes() const -> const std::vector<ViewMode>& { return this->viewModes; }

auto Settings::getActiveViewMode() const -> ViewModeId { return this->activeViewMode; }

/**
 * tempg_ascii_strtod
 * 	Transition to using g_ascii_strtod to minimize disruption. May, 2019.
 *  Delete this and replace calls to this function with calls to g_ascii_strtod() in 2020.
 * 	See: https://developer.gnome.org/glib/stable/glib-String-Utility-Functions.html#g-strtod
 */
auto tempg_ascii_strtod(const gchar* txt, gchar** endptr) -> double {
    return g_strtod(txt,
                    endptr);  //  makes best guess between locale formatted and C formatted numbers. See link above.
}


void Settings::parseData(xmlNodePtr cur, SElement& elem) {
    for (xmlNodePtr x = cur->children; x != nullptr; x = x->next) {
        if (!xmlStrcmp(x->name, reinterpret_cast<const xmlChar*>("data"))) {
            xmlChar* name = xmlGetProp(x, reinterpret_cast<const xmlChar*>("name"));
            parseData(x, elem.child(reinterpret_cast<const char*>(name)));
            xmlFree(name);
        } else if (!xmlStrcmp(x->name, reinterpret_cast<const xmlChar*>("attribute"))) {
            xmlChar* name = xmlGetProp(x, reinterpret_cast<const xmlChar*>("name"));
            xmlChar* value = xmlGetProp(x, reinterpret_cast<const xmlChar*>("value"));
            xmlChar* type = xmlGetProp(x, reinterpret_cast<const xmlChar*>("type"));

            string sType = reinterpret_cast<const char*>(type);

            if (sType == "int") {
                int i = atoi(reinterpret_cast<const char*>(value));
                elem.setInt(reinterpret_cast<const char*>(name), i);
            } else if (sType == "double") {
                double d = tempg_ascii_strtod(reinterpret_cast<const char*>(value),
                                              nullptr);  // g_ascii_strtod ignores locale setting.
                elem.setDouble(reinterpret_cast<const char*>(name), d);
            } else if (sType == "hex") {
                int i = 0;
                if (sscanf(reinterpret_cast<const char*>(value), "%x", &i)) {
                    elem.setIntHex(reinterpret_cast<const char*>(name), i);
                } else {
                    g_warning("Settings::Unknown hex value: %s:%s\n", name, value);
                }
            } else if (sType == "string") {
                elem.setString(reinterpret_cast<const char*>(name), reinterpret_cast<const char*>(value));
            } else if (sType == "boolean") {
                elem.setBool(reinterpret_cast<const char*>(name),
                             strcmp(reinterpret_cast<const char*>(value), "true") == 0);
            } else {
                g_warning("Settings::Unknown datatype: %s\n", sType.c_str());
            }

            xmlFree(name);
            xmlFree(type);
            xmlFree(value);
        } else {
            g_warning("Settings::parseData: Unknown XML node: %s\n", x->name);
            continue;
        }
    }
}

void Settings::parseToolPresets(xmlNodePtr cur) {
    xmlChar* version = xmlGetProp(cur, reinterpret_cast<const xmlChar*>("version"));
    const int storedVersion = version == nullptr ? 0 : atoi(reinterpret_cast<const char*>(version));
    xmlFree(version);

    if (storedVersion > ToolPresetList::STORAGE_VERSION) {
        // Written by a newer version of the application: keep the examples rather than reading a
        // format this version does not understand and writing it back in a lossy shape.
        g_warning("Settings: tool presets use format version %i, this version understands %i. "
                  "Keeping the built-in examples.",
                  storedVersion, ToolPresetList::STORAGE_VERSION);
        return;
    }

    std::vector<ToolPreset> stored;
    for (xmlNodePtr x = cur->children; x != nullptr; x = x->next) {
        if (x->type != XML_ELEMENT_NODE || xmlStrcmp(x->name, reinterpret_cast<const xmlChar*>("preset"))) {
            continue;
        }

        std::map<std::string, std::string> attributes;
        for (xmlAttrPtr attribute = x->properties; attribute != nullptr; attribute = attribute->next) {
            xmlChar* value = xmlNodeListGetString(x->doc, attribute->children, 1);
            if (value == nullptr) {
                continue;
            }
            attributes.emplace(reinterpret_cast<const char*>(attribute->name), reinterpret_cast<const char*>(value));
            xmlFree(value);
        }

        ToolPreset preset = ToolPreset::fromAttributes(attributes);
        if (!preset.isValid() || preset.id.empty()) {
            g_warning("Settings: ignoring an incomplete tool preset");
            continue;
        }
        stored.emplace_back(std::move(preset));
    }

    this->toolPresets = ToolPresetList::fromStored(stored);
}

void Settings::saveToolPresets(xmlNodePtr root) {
    xmlNodePtr presets = xmlNewChild(root, nullptr, reinterpret_cast<const xmlChar*>("toolPresets"), nullptr);

    char version[16];
    g_snprintf(version, sizeof(version), "%i", ToolPresetList::STORAGE_VERSION);
    xmlSetProp(presets, reinterpret_cast<const xmlChar*>("version"), reinterpret_cast<const xmlChar*>(version));

    for (const ToolPreset& preset: this->toolPresets.getPresets()) {
        xmlNodePtr node = xmlNewChild(presets, nullptr, reinterpret_cast<const xmlChar*>("preset"), nullptr);
        for (const auto& [name, value]: preset.toAttributes()) {
            xmlSetProp(node, reinterpret_cast<const xmlChar*>(name.c_str()),
                       reinterpret_cast<const xmlChar*>(value.c_str()));
        }
    }
}

/**
 * Plan 006: the dashboard's own element.
 *
 * The whole element is read at once, because the two lists it holds are lists: their order is what
 * the user arranged, and an entry that cannot be read is skipped rather than allowed to abort the
 * rest of the profile.
 */
void Settings::parseDashboard(xmlNodePtr cur) {
    std::vector<std::string> pinned;
    std::vector<DashboardFolder> folders;

    for (xmlNodePtr x = cur->children; x != nullptr; x = x->next) {
        if (x->type != XML_ELEMENT_NODE) {
            continue;
        }

        xmlChar* path = xmlGetProp(x, reinterpret_cast<const xmlChar*>("path"));
        if (path == nullptr) {
            g_warning("Settings::parseDashboard: a %s without a path", x->name);
            continue;
        }
        const std::string stored = reinterpret_cast<const char*>(path);
        xmlFree(path);

        if (!xmlStrcmp(x->name, reinterpret_cast<const xmlChar*>("pinned"))) {
            if (!stored.empty() && std::find(pinned.begin(), pinned.end(), stored) == pinned.end()) {
                pinned.emplace_back(stored);
            }
            continue;
        }

        if (!xmlStrcmp(x->name, reinterpret_cast<const xmlChar*>("folder"))) {
            xmlChar* recursive = xmlGetProp(x, reinterpret_cast<const xmlChar*>("recursive"));
            const bool recursiveFlag =
                    recursive != nullptr && strcmp(reinterpret_cast<const char*>(recursive), "true") == 0;
            xmlFree(recursive);

            const auto known = [&stored](const DashboardFolder& listed) { return listed.path == stored; };
            if (!stored.empty() && std::find_if(folders.begin(), folders.end(), known) == folders.end()) {
                folders.push_back(DashboardFolder{stored, recursiveFlag});
            }
            continue;
        }

        g_warning("Settings::parseDashboard: unknown XML node: %s", x->name);
    }

    this->dashboardPinnedFiles = std::move(pinned);
    this->dashboardFolders = std::move(folders);
}

void Settings::saveDashboard(xmlNodePtr root) {
    xmlNodePtr dashboard = xmlNewChild(root, nullptr, reinterpret_cast<const xmlChar*>("dashboard"), nullptr);

    for (const std::string& file: this->dashboardPinnedFiles) {
        xmlNodePtr node = xmlNewChild(dashboard, nullptr, reinterpret_cast<const xmlChar*>("pinned"), nullptr);
        xmlSetProp(node, reinterpret_cast<const xmlChar*>("path"), reinterpret_cast<const xmlChar*>(file.c_str()));
    }

    for (const DashboardFolder& folder: this->dashboardFolders) {
        xmlNodePtr node = xmlNewChild(dashboard, nullptr, reinterpret_cast<const xmlChar*>("folder"), nullptr);
        xmlSetProp(node, reinterpret_cast<const xmlChar*>("path"),
                   reinterpret_cast<const xmlChar*>(folder.path.c_str()));
        xmlSetProp(node, reinterpret_cast<const xmlChar*>("recursive"),
                   reinterpret_cast<const xmlChar*>(folder.recursive ? "true" : "false"));
    }
}

/**
 * Plan 007: the palette's recent commands and what the guidance and the tips have shown.
 *
 * Every id is written as it stands and nothing else is written beside it: no timestamps, no counts,
 * nothing that would say when or how often the user ran something.
 */
void Settings::saveInterface(xmlNodePtr root) {
    xmlNodePtr interface = xmlNewChild(root, nullptr, reinterpret_cast<const xmlChar*>("interface"), nullptr);
    xmlSetProp(interface, reinterpret_cast<const xmlChar*>("guidanceSeen"),
               reinterpret_cast<const xmlChar*>(this->interfaceGuidanceSeen ? "true" : "false"));
    xmlSetProp(interface, reinterpret_cast<const xmlChar*>("tipsEnabled"),
               reinterpret_cast<const xmlChar*>(this->interfaceTipsEnabled ? "true" : "false"));

    for (const std::string& id: this->recentCommands) {
        xmlNodePtr node = xmlNewChild(interface, nullptr, reinterpret_cast<const xmlChar*>("recentCommand"), nullptr);
        xmlSetProp(node, reinterpret_cast<const xmlChar*>("id"), reinterpret_cast<const xmlChar*>(id.c_str()));
    }
    for (const std::string& id: this->seenInterfaceTips) {
        xmlNodePtr node = xmlNewChild(interface, nullptr, reinterpret_cast<const xmlChar*>("seenTip"), nullptr);
        xmlSetProp(node, reinterpret_cast<const xmlChar*>("id"), reinterpret_cast<const xmlChar*>(id.c_str()));
    }
}

void Settings::parseInterface(xmlNodePtr cur) {
    auto attribute = [cur](const char* name) -> std::string {
        xmlChar* value = xmlGetProp(cur, reinterpret_cast<const xmlChar*>(name));
        if (value == nullptr) {
            return {};
        }
        std::string text(reinterpret_cast<const char*>(value));
        xmlFree(value);
        return text;
    };

    const std::string guidanceSeen = attribute("guidanceSeen");
    if (!guidanceSeen.empty()) {
        this->interfaceGuidanceSeen = guidanceSeen == "true";
    }
    const std::string tipsEnabled = attribute("tipsEnabled");
    if (!tipsEnabled.empty()) {
        this->interfaceTipsEnabled = tipsEnabled == "true";
    }

    this->recentCommands.clear();
    this->seenInterfaceTips.clear();
    for (xmlNodePtr node = cur->children; node != nullptr; node = node->next) {
        if (node->type != XML_ELEMENT_NODE) {
            continue;
        }
        xmlChar* id = xmlGetProp(node, reinterpret_cast<const xmlChar*>("id"));
        if (id == nullptr) {
            continue;
        }
        const std::string value(reinterpret_cast<const char*>(id));
        xmlFree(id);
        if (value.empty()) {
            continue;
        }

        if (!xmlStrcmp(node->name, reinterpret_cast<const xmlChar*>("recentCommand"))) {
            this->recentCommands.emplace_back(value);
        } else if (!xmlStrcmp(node->name, reinterpret_cast<const xmlChar*>("seenTip"))) {
            this->seenInterfaceTips.emplace_back(value);
        }
    }
}

void Settings::parseItem(xmlDocPtr doc, xmlNodePtr cur) {
    // Parse data map
    if (!xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("data"))) {
        xmlChar* name = xmlGetProp(cur, reinterpret_cast<const xmlChar*>("name"));
        if (name == nullptr) {
            g_warning("Settings::%s:No name property!\n", cur->name);
            return;
        }

        parseData(cur, data[reinterpret_cast<const char*>(name)]);

        xmlFree(name);
        return;
    }

    // Plan 003: the named tool presets, in their own element so their order is kept.
    if (!xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("toolPresets"))) {
        parseToolPresets(cur);
        return;
    }

    // Plan 006: the dashboard's pinned files and watched folders, in their own element.
    if (!xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("dashboard"))) {
        parseDashboard(cur);
        return;
    }

    // Plan 007: the palette's recent commands and what the guidance and the tips have shown.
    if (!xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("interface"))) {
        parseInterface(cur);
        return;
    }

    if (cur->type == XML_COMMENT_NODE) {
        return;
    }

    if (xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("property"))) {
        g_warning("Settings::Unknown XML node: %s\n", cur->name);
        return;
    }

    xmlChar* name = xmlGetProp(cur, reinterpret_cast<const xmlChar*>("name"));
    if (name == nullptr) {
        g_warning("Settings::%s:No name property!\n", cur->name);
        return;
    }

    if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("font")) == 0) {
        xmlFree(name);
        xmlChar* font = nullptr;
        xmlChar* size = nullptr;

        font = xmlGetProp(cur, reinterpret_cast<const xmlChar*>("font"));
        if (font) {
            this->font.setName(reinterpret_cast<const char*>(font));
            xmlFree(font);
        }

        size = xmlGetProp(cur, reinterpret_cast<const xmlChar*>("size"));
        if (size) {
            double dSize = DEFAULT_FONT_SIZE;
            if (sscanf(reinterpret_cast<const char*>(size), "%lf", &dSize) == 1) {
                this->font.setSize(dSize);
            }
            xmlFree(size);
        }
        return;
    }

    xmlChar* value = xmlGetProp(cur, reinterpret_cast<const xmlChar*>("value"));
    if (value == nullptr) {
        xmlFree(name);
        g_warning("Settings::No value property!\n");
        return;
    }

    // TODO(fabian): remove this typo fix in 2-3 release cycles
    if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("presureSensitivity")) == 0) {
        this->pressureSensitivity = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    }
    if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("pressureSensitivity")) == 0) {
        this->pressureSensitivity = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("minimumPressure")) == 0) {
        // std::max is for backwards compatibility for users who might have set this value too small
        this->minimumPressure = std::max(0.01, g_ascii_strtod(reinterpret_cast<const char*>(value), nullptr));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("pressureMultiplier")) == 0) {
        this->pressureMultiplier = g_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("zoomGesturesEnabled")) == 0) {
        this->zoomGesturesEnabled = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("selectedToolbar")) == 0) {
        this->selectedToolbar = reinterpret_cast<const char*>(value);
        this->selectedToolbarLoaded = true;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("workspaceMode")) == 0) {
        this->workspaceMode = workspaceModeFromString(reinterpret_cast<const char*>(value));
        this->workspaceModeLoaded = true;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("favoritePresetCount")) == 0) {
        // Plan 003: clamp on read as well, so a hand-edited or foreign file cannot ask for more
        // favourites than the toolbar can hold.
        this->favoritePresetCount = std::clamp(atoi(reinterpret_cast<const char*>(value)), 0,
                                               static_cast<int>(ToolPresetList::MAX_FAVORITES));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("focusToolbar")) == 0) {
        this->focusToolbar = reinterpret_cast<const char*>(value);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("classicToolbar")) == 0) {
        this->classicToolbar = reinterpret_cast<const char*>(value);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("focusMenubarVisible")) == 0) {
        this->focusMenubarVisible = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("classicMenubarVisible")) == 0) {
        this->classicMenubarVisible = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("lastSavePath")) == 0) {
        this->lastSavePath = fs::path(xoj::util::utf8(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("lastOpenPath")) == 0) {
        this->lastOpenPath = fs::path(xoj::util::utf8(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("lastImagePath")) == 0) {
        this->lastImagePath = fs::path(xoj::util::utf8(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("edgePanSpeed")) == 0) {
        this->edgePanSpeed = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("edgePanMaxMult")) == 0) {
        this->edgePanMaxMult = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("zoomStep")) == 0) {
        this->zoomStep = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("zoomStepScroll")) == 0) {
        this->zoomStepScroll = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("forceZoomToFitOnLoad")) == 0) {
        this->forceZoomToFitOnLoad = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("displayDpi")) == 0) {
        this->displayDpi = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("mainWndWidth")) == 0) {
        this->mainWndWidth = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("mainWndHeight")) == 0) {
        this->mainWndHeight = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("maximized")) == 0) {
        this->maximized = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("showToolbar")) == 0) {
        this->showToolbar = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("filepathShownInTitlebar")) == 0) {
        this->filepathShownInTitlebar = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("pageNumberShownInTitlebar")) == 0) {
        this->pageNumberShownInTitlebar = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("showSidebar")) == 0) {
        this->showSidebar = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("sidebarNumberingStyle")) == 0) {
        int num = std::stoi(reinterpret_cast<char*>(value));
        if (num < static_cast<int>(SidebarNumberingStyle::MIN) || static_cast<int>(SidebarNumberingStyle::MAX) < num) {
            num = static_cast<int>(SidebarNumberingStyle::DEFAULT);
            g_warning("Settings::Invalid sidebarNumberingStyle value. Reset to default.");
        }
        this->sidebarNumberingStyle = static_cast<SidebarNumberingStyle>(num);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("sidebarPageLayoutMode")) == 0) {
        int num = std::stoi(reinterpret_cast<char*>(value));
        if (num < static_cast<int>(SidebarPageLayoutMode::MIN) || static_cast<int>(SidebarPageLayoutMode::MAX) < num) {
            num = static_cast<int>(SidebarPageLayoutMode::DEFAULT);
            g_warning("Settings::Invalid sidebarPageLayoutMode value. Reset to default.");
        }
        this->sidebarPageLayoutMode = static_cast<SidebarPageLayoutMode>(num);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("sidebarWidth")) == 0) {
        this->sidebarWidth = std::max<int>(g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10), 50);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("sidebarOnRight")) == 0) {
        this->sidebarOnRight = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("scrollbarOnLeft")) == 0) {
        this->scrollbarOnLeft = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("menubarVisible")) == 0) {
        this->menubarVisible = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
        this->menubarVisibleLoaded = true;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("numColumns")) == 0) {
        this->numColumns = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("numRows")) == 0) {
        this->numRows = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("viewFixedRows")) == 0) {
        this->viewFixedRows = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("layoutVertical")) == 0) {
        this->layoutVertical = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("layoutRightToLeft")) == 0) {
        this->layoutRightToLeft = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("layoutBottomToTop")) == 0) {
        this->layoutBottomToTop = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("showPairedPages")) == 0) {
        this->showPairedPages = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("showPageShadow")) == 0) {
        this->showPageShadow = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("numPairsOffset")) == 0) {
        this->numPairsOffset = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("presentationMode")) == 0) {
        this->presentationMode = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("autoloadMostRecent")) == 0) {
        this->autoloadMostRecent = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("autoloadPdfXoj")) == 0) {
        this->autoloadPdfXoj = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("stylusCursorType")) == 0) {
        this->stylusCursorType = stylusCursorTypeFromString(reinterpret_cast<const char*>(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("eraserVisibility")) == 0) {
        this->eraserVisibility = eraserVisibilityFromString(reinterpret_cast<const char*>(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("iconTheme")) == 0) {
        this->iconTheme = iconThemeFromString(reinterpret_cast<const char*>(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("themeVariant")) == 0) {
        this->themeVariant = themeVariantFromString(reinterpret_cast<const char*>(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("highlightPosition")) == 0) {
        this->highlightPosition = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("cursorHighlightColor")) == 0) {
        this->cursorHighlightColor = g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("cursorHighlightRadius")) == 0) {
        this->cursorHighlightRadius = g_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("cursorHighlightBorderColor")) == 0) {
        this->cursorHighlightBorderColor = g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("cursorHighlightBorderWidth")) == 0) {
        this->cursorHighlightBorderWidth = g_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("useStockIcons")) == 0) {
        this->useStockIcons = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("defaultSaveName")) == 0) {
        this->defaultSaveName = xoj::util::utf8(value).str();
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("defaultPdfExportName")) == 0) {
        this->defaultPdfExportName = xoj::util::utf8(value).str();
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("pluginEnabled")) == 0) {
        this->pluginEnabled = reinterpret_cast<const char*>(value);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("pluginDisabled")) == 0) {
        this->pluginDisabled = reinterpret_cast<const char*>(value);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("pageTemplate")) == 0) {
        this->pageTemplateSettings.parse(reinterpret_cast<const char*>(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("sizeUnit")) == 0) {
        this->sizeUnit = reinterpret_cast<const char*>(value);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("audioFolder")) == 0) {
        this->audioFolder = fs::path(xoj::util::utf8(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("autosaveEnabled")) == 0) {
        this->autosaveEnabled = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("autosaveTimeout")) == 0) {
        this->autosaveTimeout = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("defaultViewModeAttributes")) == 0) {
        this->viewModes.at(PresetViewModeIds::VIEW_MODE_DEFAULT) =
                settingsStringToViewMode(reinterpret_cast<const char*>(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("fullscreenViewModeAttributes")) == 0) {
        this->viewModes.at(PresetViewModeIds::VIEW_MODE_FULLSCREEN) =
                settingsStringToViewMode(reinterpret_cast<const char*>(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("presentationViewModeAttributes")) == 0) {
        this->viewModes.at(PresetViewModeIds::VIEW_MODE_PRESENTATION) =
                settingsStringToViewMode(reinterpret_cast<const char*>(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("touchZoomStartThreshold")) == 0) {
        this->touchZoomStartThreshold = g_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("pageRerenderThreshold")) == 0) {
        this->pageRerenderThreshold = g_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("pdfPageCacheSize")) == 0) {
        this->pdfPageCacheSize = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("preloadPagesBefore")) == 0) {
        this->preloadPagesBefore = g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("preloadPagesAfter")) == 0) {
        this->preloadPagesAfter = g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("eagerPageCleanup")) == 0) {
        this->eagerPageCleanup = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("selectionBorderColor")) == 0) {
        this->selectionBorderColor = Color(g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("selectionMarkerColor")) == 0) {
        this->selectionMarkerColor = Color(g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("activeSelectionColor")) == 0) {
        this->activeSelectionColor = Color(g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10));

    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("recolor.enabled")) == 0) {
        this->recolorParameters.recolorizeMainView = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("recolor.sidebar")) == 0) {
        this->recolorParameters.recolorizeSidebarMiniatures =
                xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("recolor.light")) == 0) {
        this->recolorParameters.recolor =
                Recolor(ColorU8(g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10)),
                        this->recolorParameters.recolor.getDark());
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("recolor.dark")) == 0) {
        this->recolorParameters.recolor =
                Recolor(this->recolorParameters.recolor.getLight(),
                        ColorU8(g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10)));

    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("backgroundColor")) == 0) {
        this->backgroundColor = Color(g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("addHorizontalSpace")) == 0) {
        this->addHorizontalSpace = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("addHorizontalSpaceAmount")) == 0) {
        const int oldHorizontalAmount =
                static_cast<int>(g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10));
        this->addHorizontalSpaceAmountLeft = oldHorizontalAmount;
        this->addHorizontalSpaceAmountRight = oldHorizontalAmount;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("addHorizontalSpaceAmountRight")) == 0) {
        this->addHorizontalSpaceAmountRight =
                static_cast<int>(g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("addVerticalSpace")) == 0) {
        this->addVerticalSpace = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("addVerticalSpaceAmount")) == 0) {
        const int oldVerticalAmount =
                static_cast<int>(g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10));
        this->addHorizontalSpaceAmountLeft = oldVerticalAmount;
        this->addHorizontalSpaceAmountRight = oldVerticalAmount;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("addVerticalSpaceAmountAbove")) == 0) {
        this->addVerticalSpaceAmountAbove =
                static_cast<int>(g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("addHorizontalSpaceAmountLeft")) == 0) {
        this->addHorizontalSpaceAmountLeft =
                static_cast<int>(g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("addVerticalSpaceAmountBelow")) == 0) {
        this->addVerticalSpaceAmountBelow =
                static_cast<int>(g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("unlimitedScrolling")) == 0) {
        this->unlimitedScrolling = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("drawDirModsEnabled")) == 0) {
        this->drawDirModsEnabled = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("drawDirModsRadius")) == 0) {
        this->drawDirModsRadius = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("snapRotation")) == 0) {
        this->snapRotation = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("snapRotationTolerance")) == 0) {
        this->snapRotationTolerance = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("snapGrid")) == 0) {
        this->snapGrid = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("snapGridSize")) == 0) {
        this->snapGridSize = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("snapGridTolerance")) == 0) {
        this->snapGridTolerance = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("strokeRecognizerMinSize")) == 0) {
        this->strokeRecognizerMinSize = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("touchDrawing")) == 0) {
        this->touchDrawing = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("gtkTouchInertialScrolling")) == 0) {
        this->gtkTouchInertialScrolling = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("pressureGuessing")) == 0) {
        this->pressureGuessing = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("scrollbarHideType")) == 0) {
        if (xmlStrcmp(value, reinterpret_cast<const xmlChar*>("both")) == 0) {
            this->scrollbarHideType = SCROLLBAR_HIDE_BOTH;
        } else if (xmlStrcmp(value, reinterpret_cast<const xmlChar*>("horizontal")) == 0) {
            this->scrollbarHideType = SCROLLBAR_HIDE_HORIZONTAL;
        } else if (xmlStrcmp(value, reinterpret_cast<const xmlChar*>("vertical")) == 0) {
            this->scrollbarHideType = SCROLLBAR_HIDE_VERTICAL;
        } else {
            this->scrollbarHideType = SCROLLBAR_HIDE_NONE;
        }
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("disableScrollbarFadeout")) == 0) {
        this->disableScrollbarFadeout = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("disableAudio")) == 0) {
        this->disableAudio = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
#ifdef ENABLE_AUDIO
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("audioSampleRate")) == 0) {
        this->audioSampleRate = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("audioGain")) == 0) {
        this->audioGain = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("defaultSeekTime")) == 0) {
        this->defaultSeekTime = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("audioInputDevice")) == 0) {
        this->audioInputDevice = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("audioOutputDevice")) == 0) {
        this->audioOutputDevice = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
#endif
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("numIgnoredStylusEvents")) == 0) {
        this->numIgnoredStylusEvents =
                std::max<int>(g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10), 0);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("inputSystemTPCButton")) == 0) {
        this->inputSystemTPCButton = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("inputSystemDrawOutsideWindow")) == 0) {
        this->inputSystemDrawOutsideWindow = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("emptyLastPageAppend")) == 0) {
        this->emptyLastPageAppend = emptyLastPageAppendFromString(reinterpret_cast<char*>(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("strokeFilterIgnoreTime")) == 0) {
        this->strokeFilterIgnoreTime = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("strokeFilterIgnoreLength")) == 0) {
        this->strokeFilterIgnoreLength = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("strokeFilterSuccessiveTime")) == 0) {
        this->strokeFilterSuccessiveTime = g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("strokeFilterEnabled")) == 0) {
        this->strokeFilterEnabled = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("doActionOnStrokeFiltered")) == 0) {
        this->doActionOnStrokeFiltered = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("trySelectOnStrokeFiltered")) == 0) {
        this->trySelectOnStrokeFiltered = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.autoCheckDependencies")) == 0) {
        this->latexSettings.autoCheckDependencies = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.defaultText")) == 0) {
        this->latexSettings.defaultText = reinterpret_cast<char*>(value);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.globalTemplatePath")) == 0) {
        this->latexSettings.globalTemplatePath = fs::path(xoj::util::utf8(value));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.genCmd")) == 0) {
        this->latexSettings.genCmd = reinterpret_cast<char*>(value);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.sourceViewThemeId")) == 0) {
        this->latexSettings.sourceViewThemeId = reinterpret_cast<char*>(value);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.editorFont")) == 0) {
        this->latexSettings.editorFont = std::string{reinterpret_cast<char*>(value)};
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.useCustomEditorFont")) == 0) {
        this->latexSettings.useCustomEditorFont = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.editorWordWrap")) == 0) {
        this->latexSettings.editorWordWrap = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.sourceViewAutoIndent")) == 0) {
        this->latexSettings.sourceViewAutoIndent = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.sourceViewSyntaxHighlight")) == 0) {
        this->latexSettings.sourceViewSyntaxHighlight = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.sourceViewShowLineNumbers")) == 0) {
        this->latexSettings.sourceViewShowLineNumbers = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.useExternalEditor")) == 0) {
        this->latexSettings.useExternalEditor = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.externalEditorAutoConfirm")) == 0) {
        this->latexSettings.externalEditorAutoConfirm = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.externalEditorCmd")) == 0) {
        this->latexSettings.externalEditorCmd = std::string{reinterpret_cast<char*>(value)};
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("latexSettings.temporaryFileExt")) == 0) {
        this->latexSettings.temporaryFileExt = std::string{reinterpret_cast<char*>(value)};
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("snapRecognizedShapesEnabled")) == 0) {
        this->snapRecognizedShapesEnabled = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("restoreLineWidthEnabled")) == 0) {
        this->restoreLineWidthEnabled = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("preferredLocale")) == 0) {
        this->preferredLocale = reinterpret_cast<char*>(value);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("useSpacesForTab")) == 0) {
        this->setUseSpacesAsTab(xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("numberOfSpacesForTab")) == 0) {
        this->setNumberOfSpacesForTab(
                static_cast<unsigned int>(g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10)));
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("laserPointerFadeOutTime")) == 0) {
        this->laserPointerFadeOutTime =
                static_cast<unsigned int>(g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10));
        /**
         * Stabilizer related settings
         */
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("stabilizerAveragingMethod")) == 0) {
        this->stabilizerAveragingMethod =
                (StrokeStabilizer::AveragingMethod)g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("stabilizerPreprocessor")) == 0) {
        this->stabilizerPreprocessor =
                (StrokeStabilizer::Preprocessor)g_ascii_strtoll(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("stabilizerBuffersize")) == 0) {
        this->stabilizerBuffersize = g_ascii_strtoull(reinterpret_cast<const char*>(value), nullptr, 10);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("stabilizerSigma")) == 0) {
        this->stabilizerSigma = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("stabilizerDeadzoneRadius")) == 0) {
        this->stabilizerDeadzoneRadius = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("stabilizerDrag")) == 0) {
        this->stabilizerDrag = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("stabilizerMass")) == 0) {
        this->stabilizerMass = tempg_ascii_strtod(reinterpret_cast<const char*>(value), nullptr);
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("stabilizerCuspDetection")) == 0) {
        this->stabilizerCuspDetection = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("stabilizerFinalizeStroke")) == 0) {
        this->stabilizerFinalizeStroke = xmlStrcmp(value, reinterpret_cast<const xmlChar*>("true")) == 0;
    } else if (xmlStrcmp(name, reinterpret_cast<const xmlChar*>("colorPalette")) == 0) {
        std::string_view paletteConfig = std::string_view{reinterpret_cast<const char*>(value)};
        if (!paletteConfig.empty()) {
            this->colorPaletteSetting = fs::path(xoj::util::utf8(paletteConfig));
        }
    }
    /**/

    xmlFree(name);
    xmlFree(value);
}

void Settings::loadDeviceClasses() {
    SElement& s = getCustomElement("deviceClasses");
    for (auto device: s.children()) {
        SElement& deviceNode = device.second;
        int deviceClass = 0;
        int deviceSource = 0;
        deviceNode.getInt("deviceClass", deviceClass);
        deviceNode.getInt("deviceSource", deviceSource);
        auto devClass = static_cast<InputDeviceTypeOption>(deviceClass);
        if (devClass == InputDeviceTypeOption::MouseKeyboardCombo) {
            // This extra class is no longer handled differently from Mouse. Merge them.
            devClass = InputDeviceTypeOption::Mouse;
        }
        inputDeviceClasses.emplace(device.first, std::make_pair(devClass, static_cast<GdkInputSource>(deviceSource)));
    }
}

void Settings::loadButtonConfig() {
    SElement& s = getCustomElement("buttonConfig");

    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        SElement& e = s.child(buttonToString(static_cast<Button>(i)));
        const auto& cfg = buttonConfig[i];

        string sType;
        if (e.getString("tool", sType)) {
            ToolType type = toolTypeFromString(sType);
            cfg->action = type;

            if (type != TOOL_NONE) {
                if (type == TOOL_PEN) {
                    string strokeType;
                    cfg->strokeType =
                            e.getString("strokeType", strokeType) ? strokeTypeFromString(strokeType) : STROKE_TYPE_NONE;
                }

                if (type == TOOL_PEN || type == TOOL_HIGHLIGHTER) {
                    string drawingType;
                    if (e.getString("drawingType", drawingType)) {
                        cfg->drawingType = drawingTypeFromString(drawingType);
                    }
                }

                if (type == TOOL_ERASER) {
                    std::string sEraserMode;
                    if (e.getString("eraserMode", sEraserMode)) {
                        cfg->eraserMode = eraserTypeFromString(sEraserMode);
                    } else {
                        // If not specified: do not change
                        cfg->eraserMode = ERASER_TYPE_NONE;
                    }
                }

                if (xoj::tool::hasCapability(type, TOOL_CAP_SIZE)) {
                    std::string sSize;
                    if (e.getString("size", sSize)) {
                        cfg->size = toolSizeFromString(sSize);
                    } else {
                        // If not specified: do not change
                        cfg->size = TOOL_SIZE_NONE;
                    }
                }

                if (xoj::tool::hasCapability(type, TOOL_CAP_COLOR)) {
                    if (int iColor; e.getInt("color", iColor)) {
                        cfg->color = Color(as_unsigned(iColor));
                    } else {
                        // If not specified: do not change
                        cfg->color = std::nullopt;
                    }
                }
            }

            // Touch device
            if (i == BUTTON_TOUCH) {
                if (!e.getString("device", cfg->device)) {
                    cfg->device = "";
                }

                e.getBool("disableDrawing", cfg->disableDrawing);
            }
        } else {
            continue;
        }
    }
}

auto Settings::load() -> bool {
    xmlKeepBlanksDefault(0);

    // Plan 002: whether the profile already existed decides how the workspace is resolved
    // when the file carries no workspace field (see resolveWorkspaceAfterLoad()).
    const bool profileExisted = fs::exists(filepath);

    if (!profileExisted) {
        g_warning("Settings file %s does not exist. Regenerating. ", filepath.string().c_str());
        save();
    }

    xmlDocPtr doc = xmlParseFile(char_cast(filepath.u8string().c_str()));

    if (doc == nullptr) {
        g_warning("Settings::load:: doc == null, could not load Settings!\n");
        return false;
    }

    xmlNodePtr cur = xmlDocGetRootElement(doc);
    if (cur == nullptr) {
        g_message("The settings file \"%s\" is empty", filepath.string().c_str());
        xmlFreeDoc(doc);

        return false;
    }

    if (xmlStrcmp(cur->name, reinterpret_cast<const xmlChar*>("settings"))) {
        g_message("File \"%s\" is of the wrong type", filepath.string().c_str());
        xmlFreeDoc(doc);

        return false;
    }

    cur = xmlDocGetRootElement(doc);
    cur = cur->xmlChildrenNode;

    while (cur != nullptr) {
        parseItem(doc, cur);

        cur = cur->next;
    }

    xmlFreeDoc(doc);

    resolveWorkspaceAfterLoad(profileExisted);

    loadButtonConfig();
    loadDeviceClasses();

    // This must be done before the color palette to ensure the color names are translated properly
#ifdef _WIN32
    _putenv_s("LANGUAGE", this->preferredLocale.c_str());
#else
    setenv("LANGUAGE", this->preferredLocale.c_str(), 1);
#endif

    return true;
}

auto Settings::savePropertyDouble(const gchar* key, double value, xmlNodePtr parent) -> xmlNodePtr {
    char text[G_ASCII_DTOSTR_BUF_SIZE];
    //  g_ascii_ version uses C locale always.
    g_ascii_formatd(text, G_ASCII_DTOSTR_BUF_SIZE, Util::PRECISION_FORMAT_STRING, value);
    xmlNodePtr xmlNode = saveProperty(key, text, parent);
    return xmlNode;
}

auto Settings::saveProperty(const gchar* key, int value, xmlNodePtr parent) -> xmlNodePtr {
    char* text = g_strdup_printf("%i", value);
    xmlNodePtr xmlNode = saveProperty(key, text, parent);
    g_free(text);
    return xmlNode;
}

auto Settings::savePropertyUnsigned(const gchar* key, unsigned int value, xmlNodePtr parent) -> xmlNodePtr {
    char* text = g_strdup_printf("%u", value);
    xmlNodePtr xmlNode = saveProperty(key, text, parent);
    g_free(text);
    return xmlNode;
}

auto Settings::saveProperty(const gchar* key, const gchar* value, xmlNodePtr parent) -> xmlNodePtr {
    xmlNodePtr xmlNode = xmlNewChild(parent, nullptr, reinterpret_cast<const xmlChar*>("property"), nullptr);

    xmlSetProp(xmlNode, reinterpret_cast<const xmlChar*>("name"), reinterpret_cast<const xmlChar*>(key));

    xmlSetProp(xmlNode, reinterpret_cast<const xmlChar*>("value"), reinterpret_cast<const xmlChar*>(value));

    return xmlNode;
}

void Settings::saveDeviceClasses() {
    SElement& s = getCustomElement("deviceClasses");

    for (auto& device: inputDeviceClasses) {
        const std::string& name = device.first;
        InputDeviceTypeOption& deviceClass = device.second.first;
        GdkInputSource& source = device.second.second;
        SElement& e = s.child(name);
        e.setInt("deviceClass", static_cast<int>(deviceClass));
        e.setInt("deviceSource", source);
    }
}

void Settings::saveButtonConfig() {
    SElement& s = getCustomElement("buttonConfig");
    s.clear();

    for (size_t i = 0; i < BUTTON_COUNT; i++) {
        SElement& e = s.child(buttonToString(static_cast<Button>(i)));
        const auto& cfg = buttonConfig[i];

        ToolType const type = cfg->action;
        e.setString("tool", toolTypeToString(type).data());

        if (type != TOOL_NONE) {
            if (type == TOOL_PEN) {
                e.setString("strokeType", strokeTypeToString(cfg->strokeType).data());
            }

            if (type == TOOL_PEN || type == TOOL_HIGHLIGHTER) {
                e.setString("drawingType", drawingTypeToString(cfg->drawingType).data());
            }

            if (type == TOOL_ERASER) {
                e.setString("eraserMode", eraserTypeToString(cfg->eraserMode).data());
            }

            if (xoj::tool::hasCapability(type, TOOL_CAP_SIZE)) {
                e.setString("size", toolSizeToString(cfg->size).data());
            }

            if (xoj::tool::hasCapability(type, TOOL_CAP_COLOR) && cfg->color) {
                e.setIntHex("color", int32_t(uint32_t(*cfg->color)));
            }
        }

        // Touch device
        if (i == BUTTON_TOUCH) {
            e.setString("device", cfg->device);
            e.setBool("disableDrawing", cfg->disableDrawing);
        }
    }
}

/**
 * Do not save settings until transactionEnd() is called
 */
void Settings::transactionStart() { inTransaction = true; }

/**
 * Stop transaction and save settings
 */
void Settings::transactionEnd() {
    inTransaction = false;
    save();
}

void Settings::save() {
    if (inTransaction) {
        return;
    }

    xmlDocPtr doc = nullptr;
    xmlNodePtr root = nullptr;
    xmlNodePtr xmlNode = nullptr;

    doc = xmlNewDoc(reinterpret_cast<const xmlChar*>("1.0"));
    if (doc == nullptr) {
        return;
    }

    saveButtonConfig();
    saveDeviceClasses();

    /* Create metadata root */
    root = xmlNewDocNode(doc, nullptr, reinterpret_cast<const xmlChar*>("settings"), nullptr);
    xmlDocSetRootElement(doc, root);
    xmlNodePtr com = xmlNewComment(
            reinterpret_cast<const xmlChar*>("The Xournal++ settings file. Do not edit this file! "
                                             "Most settings are available in the Settings dialog, "
                                             "the others are commented in this file, but handle with care!"));
    xmlAddPrevSibling(root, com);

    SAVE_BOOL_PROP(pressureSensitivity);
    SAVE_DOUBLE_PROP(minimumPressure);
    SAVE_DOUBLE_PROP(pressureMultiplier);

    SAVE_BOOL_PROP(zoomGesturesEnabled);

    SAVE_STRING_PROP(selectedToolbar);

    // Plan 002: workspace (presentation state, also used by the Workspace menu)
    xmlNode = saveProperty("workspaceMode", workspaceModeToString(this->workspaceMode), root);
    ATTACH_COMMENT("Active workspace, allowed values are \"focus\" and \"classic\"");
    SAVE_STRING_PROP(focusToolbar);
    SAVE_STRING_PROP(classicToolbar);
    SAVE_BOOL_PROP(focusMenubarVisible);
    SAVE_BOOL_PROP(classicMenubarVisible);

    // Plan 003: the named tool presets and how many of their favourites Focus shows directly.
    SAVE_INT_PROP(favoritePresetCount);
    saveToolPresets(root);

    // Plan 006: the dashboard's pinned files and watched folders.
    saveDashboard(root);
    saveInterface(root);

    saveProperty("lastSavePath", char_cast(this->lastSavePath.u8string().c_str()), root);
    saveProperty("lastOpenPath", char_cast(this->lastOpenPath.u8string().c_str()), root);
    saveProperty("lastImagePath", char_cast(this->lastImagePath.u8string().c_str()), root);

    SAVE_DOUBLE_PROP(edgePanSpeed);
    SAVE_DOUBLE_PROP(edgePanMaxMult);
    SAVE_DOUBLE_PROP(zoomStep);
    SAVE_DOUBLE_PROP(zoomStepScroll);
    SAVE_BOOL_PROP(forceZoomToFitOnLoad);
    SAVE_INT_PROP(displayDpi);
    SAVE_INT_PROP(mainWndWidth);
    SAVE_INT_PROP(mainWndHeight);
    SAVE_BOOL_PROP(maximized);

    SAVE_BOOL_PROP(showToolbar);

    SAVE_BOOL_PROP(showSidebar);
    SAVE_INT_PROP(sidebarWidth);
    xmlNode = saveProperty("sidebarNumberingStyle", static_cast<int>(sidebarNumberingStyle), root);
    xmlNode = saveProperty("sidebarPageLayoutMode", static_cast<int>(sidebarPageLayoutMode), root);

    SAVE_BOOL_PROP(sidebarOnRight);
    SAVE_BOOL_PROP(scrollbarOnLeft);
    SAVE_BOOL_PROP(menubarVisible);
    SAVE_BOOL_PROP(filepathShownInTitlebar);
    SAVE_BOOL_PROP(pageNumberShownInTitlebar);
    SAVE_INT_PROP(numColumns);
    SAVE_INT_PROP(numRows);
    SAVE_BOOL_PROP(viewFixedRows);
    SAVE_BOOL_PROP(showPairedPages);
    SAVE_BOOL_PROP(showPageShadow);
    SAVE_BOOL_PROP(layoutVertical);
    SAVE_BOOL_PROP(layoutRightToLeft);
    SAVE_BOOL_PROP(layoutBottomToTop);
    SAVE_INT_PROP(numPairsOffset);
    xmlNode = saveProperty("emptyLastPageAppend", emptyLastPageAppendToString(this->emptyLastPageAppend), root);
    ATTACH_COMMENT("The icon theme, allowed values are \"disabled\", \"onDrawOfLastPage\", and \"onScrollOfLastPage\"");
    SAVE_BOOL_PROP(presentationMode);

    auto defaultViewModeAttributes = viewModeToSettingsString(viewModes.at(PresetViewModeIds::VIEW_MODE_DEFAULT));
    auto fullscreenViewModeAttributes = viewModeToSettingsString(viewModes.at(PresetViewModeIds::VIEW_MODE_FULLSCREEN));
    auto presentationViewModeAttributes =
            viewModeToSettingsString(viewModes.at(PresetViewModeIds::VIEW_MODE_PRESENTATION));
    SAVE_STRING_PROP(defaultViewModeAttributes);
    ATTACH_COMMENT("Which GUI elements are shown in default view mode, separated by a colon (,)");
    SAVE_STRING_PROP(fullscreenViewModeAttributes);
    ATTACH_COMMENT("Which GUI elements are shown in fullscreen view mode, separated by a colon (,)");
    SAVE_STRING_PROP(presentationViewModeAttributes);
    ATTACH_COMMENT("Which GUI elements are shown in presentation view mode, separated by a colon (,)");

    xmlNode = saveProperty("stylusCursorType", stylusCursorTypeToString(this->stylusCursorType), root);
    ATTACH_COMMENT("The cursor icon used with a stylus, allowed values are \"none\", \"dot\", \"big\", \"arrow\"");

    xmlNode = saveProperty("eraserVisibility", eraserVisibilityToString(this->eraserVisibility), root);
    ATTACH_COMMENT("The eraser cursor visibility used with a stylus, allowed values are \"never\", \"always\", "
                   "\"hover\", \"touch\"");

    xmlNode = saveProperty("iconTheme", iconThemeToString(this->iconTheme), root);
    ATTACH_COMMENT("The icon theme, allowed values are \"iconsColor\", \"iconsLucide\"");

    xmlNode = saveProperty("themeVariant", themeVariantToString(this->themeVariant), root);
    ATTACH_COMMENT("Dark/light mode, allowed values are \"useSystem\", \"forceLight\", \"forceDark\"");

    SAVE_BOOL_PROP(highlightPosition);
    xmlNode = savePropertyUnsigned("cursorHighlightColor", uint32_t(cursorHighlightColor), root);
    xmlNode = savePropertyUnsigned("cursorHighlightBorderColor", uint32_t(cursorHighlightBorderColor), root);
    SAVE_DOUBLE_PROP(cursorHighlightRadius);
    SAVE_DOUBLE_PROP(cursorHighlightBorderWidth);
    SAVE_BOOL_PROP(useStockIcons);

    SAVE_BOOL_PROP(disableScrollbarFadeout);
    SAVE_BOOL_PROP(disableAudio);

    if (this->scrollbarHideType == SCROLLBAR_HIDE_BOTH) {
        xmlNode = saveProperty("scrollbarHideType", "both", root);
    } else if (this->scrollbarHideType == SCROLLBAR_HIDE_HORIZONTAL) {
        xmlNode = saveProperty("scrollbarHideType", "horizontal", root);
    } else if (this->scrollbarHideType == SCROLLBAR_HIDE_VERTICAL) {
        xmlNode = saveProperty("scrollbarHideType", "vertical", root);
    } else {
        xmlNode = saveProperty("scrollbarHideType", "none", root);
    }
    ATTACH_COMMENT("Hides scroolbars in the main window, allowed values: \"none\", \"horizontal\", \"vertical\", "
                   "\"both\"");

    SAVE_BOOL_PROP(autoloadMostRecent);
    SAVE_BOOL_PROP(autoloadPdfXoj);
    saveProperty("defaultSaveName", defaultSaveName.empty() ? "" : char_cast(defaultSaveName.c_str()), root);
    saveProperty("defaultPdfExportName", defaultPdfExportName.empty() ? "" : char_cast(defaultPdfExportName.c_str()),
                 root);

    SAVE_BOOL_PROP(autosaveEnabled);
    SAVE_INT_PROP(autosaveTimeout);

    SAVE_BOOL_PROP(addHorizontalSpace);
    SAVE_INT_PROP(addHorizontalSpaceAmountRight);
    SAVE_INT_PROP(addHorizontalSpaceAmountLeft);
    SAVE_BOOL_PROP(addVerticalSpace);
    SAVE_INT_PROP(addVerticalSpaceAmountAbove);
    SAVE_INT_PROP(addVerticalSpaceAmountBelow);

    SAVE_BOOL_PROP(unlimitedScrolling);

    SAVE_BOOL_PROP(drawDirModsEnabled);
    SAVE_INT_PROP(drawDirModsRadius);


    SAVE_BOOL_PROP(snapRotation);
    SAVE_DOUBLE_PROP(snapRotationTolerance);
    SAVE_BOOL_PROP(snapGrid);
    SAVE_DOUBLE_PROP(snapGridTolerance);
    SAVE_DOUBLE_PROP(snapGridSize);

    SAVE_DOUBLE_PROP(strokeRecognizerMinSize);

    SAVE_BOOL_PROP(touchDrawing);
    SAVE_BOOL_PROP(gtkTouchInertialScrolling);
    SAVE_BOOL_PROP(pressureGuessing);

    xmlNode = saveProperty("recolor.enabled", recolorParameters.recolorizeMainView ? "true" : "false", root);
    xmlNode = saveProperty("recolor.sidebar", recolorParameters.recolorizeSidebarMiniatures ? "true" : "false", root);
    xmlNode = savePropertyUnsigned("recolor.dark", uint32_t(recolorParameters.recolor.getDark()), root);
    xmlNode = savePropertyUnsigned("recolor.light", uint32_t(recolorParameters.recolor.getLight()), root);

    xmlNode = savePropertyUnsigned("selectionBorderColor", uint32_t(selectionBorderColor), root);
    xmlNode = savePropertyUnsigned("backgroundColor", uint32_t(backgroundColor), root);
    xmlNode = savePropertyUnsigned("selectionMarkerColor", uint32_t(selectionMarkerColor), root);
    xmlNode = savePropertyUnsigned("activeSelectionColor", uint32_t(activeSelectionColor), root);

    SAVE_DOUBLE_PROP(touchZoomStartThreshold);
    SAVE_DOUBLE_PROP(pageRerenderThreshold);

    SAVE_INT_PROP(pdfPageCacheSize);
    ATTACH_COMMENT("The count of rendered PDF pages which will be cached.");
    SAVE_UINT_PROP(preloadPagesBefore);
    SAVE_UINT_PROP(preloadPagesAfter);
    SAVE_BOOL_PROP(eagerPageCleanup);

    const auto pageTemplate = pageTemplateSettings.toString();
    SAVE_STRING_PROP(pageTemplate);
    ATTACH_COMMENT("Config for new pages");

    SAVE_STRING_PROP(sizeUnit);

#ifdef ENABLE_AUDIO
    saveProperty("audioFolder", char_cast(this->audioFolder.u8string().c_str()), root);
    SAVE_INT_PROP(audioInputDevice);
    SAVE_INT_PROP(audioOutputDevice);
    SAVE_DOUBLE_PROP(audioSampleRate);
    SAVE_DOUBLE_PROP(audioGain);
    SAVE_INT_PROP(defaultSeekTime);
#endif

    SAVE_STRING_PROP(pluginEnabled);
    SAVE_STRING_PROP(pluginDisabled);

    SAVE_INT_PROP(strokeFilterIgnoreTime);
    SAVE_DOUBLE_PROP(strokeFilterIgnoreLength);
    SAVE_INT_PROP(strokeFilterSuccessiveTime);
    SAVE_BOOL_PROP(strokeFilterEnabled);
    SAVE_BOOL_PROP(doActionOnStrokeFiltered);
    SAVE_BOOL_PROP(trySelectOnStrokeFiltered);

    SAVE_BOOL_PROP(snapRecognizedShapesEnabled);
    SAVE_BOOL_PROP(restoreLineWidthEnabled);

    SAVE_INT_PROP(numIgnoredStylusEvents);

    SAVE_BOOL_PROP(inputSystemTPCButton);
    SAVE_BOOL_PROP(inputSystemDrawOutsideWindow);

    SAVE_STRING_PROP(preferredLocale);

    SAVE_BOOL_PROP(useSpacesForTab);
    SAVE_UINT_PROP(numberOfSpacesForTab);

    SAVE_UINT_PROP(laserPointerFadeOutTime);

    /**
     * Stabilizer related settings
     */
    saveProperty("stabilizerAveragingMethod", static_cast<int>(stabilizerAveragingMethod), root);
    saveProperty("stabilizerPreprocessor", static_cast<int>(stabilizerPreprocessor), root);
    SAVE_UINT_PROP(stabilizerBuffersize);
    SAVE_DOUBLE_PROP(stabilizerSigma);
    SAVE_DOUBLE_PROP(stabilizerDeadzoneRadius);
    SAVE_DOUBLE_PROP(stabilizerDrag);
    SAVE_DOUBLE_PROP(stabilizerMass);
    SAVE_BOOL_PROP(stabilizerCuspDetection);
    SAVE_BOOL_PROP(stabilizerFinalizeStroke);

    if (!this->colorPaletteSetting.empty()) {
        saveProperty("colorPalette", char_cast(this->colorPaletteSetting.u8string().c_str()), root);
    }

    /**/

    SAVE_BOOL_PROP(latexSettings.autoCheckDependencies);
    SAVE_STRING_PROP(latexSettings.defaultText);
    // Inline SAVE_STRING_PROP(latexSettings.globalTemplatePath) since it
    // breaks on Windows due to the native character representation being
    // wchar_t instead of char
    fs::path& p = latexSettings.globalTemplatePath;
    xmlNode = saveProperty("latexSettings.globalTemplatePath", p.empty() ? "" : char_cast(p.u8string().c_str()), root);
    SAVE_STRING_PROP(latexSettings.genCmd);
    SAVE_STRING_PROP(latexSettings.sourceViewThemeId);
    SAVE_FONT_PROP(latexSettings.editorFont);
    SAVE_BOOL_PROP(latexSettings.useCustomEditorFont);
    SAVE_BOOL_PROP(latexSettings.editorWordWrap);
    SAVE_BOOL_PROP(latexSettings.sourceViewAutoIndent);
    SAVE_BOOL_PROP(latexSettings.sourceViewSyntaxHighlight);
    SAVE_BOOL_PROP(latexSettings.sourceViewShowLineNumbers);
    SAVE_BOOL_PROP(latexSettings.useExternalEditor);
    SAVE_BOOL_PROP(latexSettings.externalEditorAutoConfirm);
    SAVE_STRING_PROP(latexSettings.externalEditorCmd);
    SAVE_STRING_PROP(latexSettings.temporaryFileExt);

    xmlNodePtr xmlFont = nullptr;
    xmlFont = xmlNewChild(root, nullptr, reinterpret_cast<const xmlChar*>("property"), nullptr);
    xmlSetProp(xmlFont, reinterpret_cast<const xmlChar*>("name"), reinterpret_cast<const xmlChar*>("font"));
    xmlSetProp(xmlFont, reinterpret_cast<const xmlChar*>("font"),
               reinterpret_cast<const xmlChar*>(this->font.getName().c_str()));

    char sSize[G_ASCII_DTOSTR_BUF_SIZE];

    g_ascii_formatd(sSize, G_ASCII_DTOSTR_BUF_SIZE, Util::PRECISION_FORMAT_STRING,
                    this->font.getSize());  // no locale
    xmlSetProp(xmlFont, reinterpret_cast<const xmlChar*>("size"), reinterpret_cast<const xmlChar*>(sSize));


    for (std::map<string, SElement>::value_type p: data) {
        saveData(root, p.first, p.second);
    }

    xmlSaveFormatFileEnc(char_cast(filepath.u8string().c_str()), doc, "UTF-8", true);
    xmlFreeDoc(doc);
}

void Settings::saveData(xmlNodePtr root, const string& name, SElement& elem) {
    xmlNodePtr xmlNode = xmlNewChild(root, nullptr, reinterpret_cast<const xmlChar*>("data"), nullptr);

    xmlSetProp(xmlNode, reinterpret_cast<const xmlChar*>("name"), reinterpret_cast<const xmlChar*>(name.c_str()));

    for (auto const& [aname, attrib]: elem.attributes()) {
        string type;
        string value;

        if (attrib.type == ATTRIBUTE_TYPE_BOOLEAN) {
            type = "boolean";

            if (attrib.iValue) {
                value = "true";
            } else {
                value = "false";
            }
        } else if (attrib.type == ATTRIBUTE_TYPE_INT) {
            type = "int";

            char* tmp = g_strdup_printf("%i", attrib.iValue);
            value = tmp;
            g_free(tmp);
        } else if (attrib.type == ATTRIBUTE_TYPE_DOUBLE) {
            type = "double";

            char tmp[G_ASCII_DTOSTR_BUF_SIZE];
            g_ascii_formatd(tmp, G_ASCII_DTOSTR_BUF_SIZE, Util::PRECISION_FORMAT_STRING, attrib.dValue);
            value = tmp;
        } else if (attrib.type == ATTRIBUTE_TYPE_INT_HEX) {
            type = "hex";

            char* tmp = g_strdup_printf("%06x", attrib.iValue);
            value = tmp;
            g_free(tmp);
        } else if (attrib.type == ATTRIBUTE_TYPE_STRING) {
            type = "string";
            value = attrib.sValue;
        } else {
            // Unknown type or empty attribute
            continue;
        }

        xmlNodePtr at = nullptr;
        at = xmlNewChild(xmlNode, nullptr, reinterpret_cast<const xmlChar*>("attribute"), nullptr);

        xmlSetProp(at, reinterpret_cast<const xmlChar*>("name"), reinterpret_cast<const xmlChar*>(aname.c_str()));
        xmlSetProp(at, reinterpret_cast<const xmlChar*>("type"), reinterpret_cast<const xmlChar*>(type.c_str()));
        xmlSetProp(at, reinterpret_cast<const xmlChar*>("value"), reinterpret_cast<const xmlChar*>(value.c_str()));

        if (!attrib.comment.empty()) {
            xmlNodePtr com = xmlNewComment(reinterpret_cast<const xmlChar*>(attrib.comment.c_str()));
            xmlAddPrevSibling(xmlNode, com);
        }
    }

    for (std::map<string, SElement>::value_type p: elem.children()) {
        saveData(xmlNode, p.first, p.second);
    }
}

// Getter- / Setter
auto Settings::isPressureSensitivity() const -> bool { return this->pressureSensitivity; }

auto Settings::isZoomGesturesEnabled() const -> bool { return this->zoomGesturesEnabled; }

void Settings::setZoomGesturesEnabled(bool enable) {
    if (this->zoomGesturesEnabled == enable) {
        return;
    }
    this->zoomGesturesEnabled = enable;
    save();
}

auto Settings::isSidebarOnRight() const -> bool { return this->sidebarOnRight; }

void Settings::setSidebarOnRight(bool right) {
    if (this->sidebarOnRight == right) {
        return;
    }

    this->sidebarOnRight = right;

    save();
}

auto Settings::isScrollbarOnLeft() const -> bool { return this->scrollbarOnLeft; }

void Settings::setScrollbarOnLeft(bool right) {
    if (this->scrollbarOnLeft == right) {
        return;
    }

    this->scrollbarOnLeft = right;

    save();
}

auto Settings::isMenubarVisible() const -> bool { return this->menubarVisible; }

void Settings::setMenubarVisible(bool visible) {
    if (this->menubarVisible == visible) {
        return;
    }

    this->menubarVisible = visible;
    // Remember the preference for the workspace it was made in (Plan 002).
    (this->workspaceMode == WorkspaceMode::FOCUS ? this->focusMenubarVisible : this->classicMenubarVisible) = visible;

    save();
}

const bool Settings::isFilepathInTitlebarShown() const { return this->filepathShownInTitlebar; }

void Settings::setFilepathInTitlebarShown(const bool shown) {
    if (this->filepathShownInTitlebar == shown) {
        return;
    }

    this->filepathShownInTitlebar = shown;

    save();
}

const bool Settings::isPageNumberInTitlebarShown() const { return this->pageNumberShownInTitlebar; }

void Settings::setPageNumberInTitlebarShown(const bool shown) {
    if (this->pageNumberShownInTitlebar == shown) {
        return;
    }

    this->pageNumberShownInTitlebar = shown;

    save();
}

auto Settings::getAutosaveTimeout() const -> int { return this->autosaveTimeout; }

void Settings::setAutosaveTimeout(int autosave) {
    if (this->autosaveTimeout == autosave) {
        return;
    }

    this->autosaveTimeout = autosave;

    save();
}

auto Settings::isAutosaveEnabled() const -> bool { return this->autosaveEnabled; }

void Settings::setAutosaveEnabled(bool autosave) {
    if (this->autosaveEnabled == autosave) {
        return;
    }

    this->autosaveEnabled = autosave;

    save();
}

auto Settings::getAddVerticalSpace() const -> bool { return this->addVerticalSpace; }

void Settings::setAddVerticalSpace(bool space) { this->addVerticalSpace = space; }

auto Settings::getAddVerticalSpaceAmountAbove() const -> int { return this->addVerticalSpaceAmountAbove; }

void Settings::setAddVerticalSpaceAmountAbove(int pixels) {
    if (this->addVerticalSpaceAmountAbove == pixels) {
        return;
    }

    this->addVerticalSpaceAmountAbove = pixels;
}

auto Settings::getAddVerticalSpaceAmountBelow() const -> int { return this->addVerticalSpaceAmountBelow; }

void Settings::setAddVerticalSpaceAmountBelow(int pixels) {
    if (this->addVerticalSpaceAmountBelow == pixels) {
        return;
    }

    this->addVerticalSpaceAmountBelow = pixels;
}


auto Settings::getAddHorizontalSpace() const -> bool { return this->addHorizontalSpace; }

void Settings::setAddHorizontalSpace(bool space) { this->addHorizontalSpace = space; }

auto Settings::getAddHorizontalSpaceAmountRight() const -> int { return this->addHorizontalSpaceAmountRight; }

void Settings::setAddHorizontalSpaceAmountRight(int pixels) {
    if (this->addHorizontalSpaceAmountRight == pixels) {
        return;
    }

    this->addHorizontalSpaceAmountRight = pixels;
}

auto Settings::getAddHorizontalSpaceAmountLeft() const -> int { return this->addHorizontalSpaceAmountLeft; }

void Settings::setAddHorizontalSpaceAmountLeft(int pixels) {
    if (this->addHorizontalSpaceAmountLeft == pixels) {
        return;
    }

    this->addHorizontalSpaceAmountLeft = pixels;
}

auto Settings::getUnlimitedScrolling() const -> bool { return this->unlimitedScrolling; }

void Settings::setUnlimitedScrolling(bool enable) {
    if (enable == this->unlimitedScrolling) {
        return;
    }

    this->unlimitedScrolling = enable;
}

auto Settings::getDrawDirModsEnabled() const -> bool { return this->drawDirModsEnabled; }

void Settings::setDrawDirModsEnabled(bool enable) { this->drawDirModsEnabled = enable; }

auto Settings::getDrawDirModsRadius() const -> int { return this->drawDirModsRadius; }

void Settings::setDrawDirModsRadius(int pixels) {
    if (this->drawDirModsRadius == pixels) {
        return;
    }

    this->drawDirModsRadius = pixels;
    save();
}

auto Settings::getStylusCursorType() const -> StylusCursorType { return this->stylusCursorType; }

void Settings::setStylusCursorType(StylusCursorType type) {
    if (this->stylusCursorType == type) {
        return;
    }

    this->stylusCursorType = type;

    save();
}

auto Settings::getEraserVisibility() const -> EraserVisibility { return this->eraserVisibility; }

void Settings::setEraserVisibility(EraserVisibility eraserVisibility) {
    if (this->eraserVisibility == eraserVisibility) {
        return;
    }

    this->eraserVisibility = eraserVisibility;

    save();
}

auto Settings::getIconTheme() const -> IconTheme { return this->iconTheme; }

void Settings::setIconTheme(IconTheme iconTheme) {
    if (this->iconTheme == iconTheme) {
        return;
    }

    this->iconTheme = iconTheme;

    save();
}

auto Settings::getThemeVariant() const -> ThemeVariant { return this->themeVariant; }

void Settings::setThemeVariant(ThemeVariant theme) {
    if (this->themeVariant == theme) {
        return;
    }
    this->themeVariant = theme;
    save();
}

auto Settings::getSidebarNumberingStyle() const -> SidebarNumberingStyle { return this->sidebarNumberingStyle; };

void Settings::setSidebarNumberingStyle(SidebarNumberingStyle numberingStyle) {
    if (this->sidebarNumberingStyle == numberingStyle) {
        return;
    }

    this->sidebarNumberingStyle = numberingStyle;

    save();
}

auto Settings::getSidebarPageLayoutMode() const -> SidebarPageLayoutMode { return this->sidebarPageLayoutMode; }

void Settings::setSidebarPageLayoutMode(SidebarPageLayoutMode layoutMode) {
    if (this->sidebarPageLayoutMode == layoutMode) {
        return;
    }

    this->sidebarPageLayoutMode = layoutMode;

    save();
}

/* Plan 006: the dashboard's pinned files and watched folders. */

namespace {
/// The stored form of a path. UTF-8, so a profile written on one platform reads the same on another.
auto storedPath(const fs::path& path) -> std::string { return std::string(char_cast(path.u8string())); }
/// The path a stored string means. Nothing is resolved here: the stored path is what the user chose.
auto pathFromStored(const std::string& stored) -> fs::path {
    return fs::path(std::u8string(stored.begin(), stored.end()));
}
}  // namespace

auto Settings::getDashboardPinnedFiles() const -> std::vector<fs::path> {
    std::vector<fs::path> files;
    files.reserve(this->dashboardPinnedFiles.size());
    for (const std::string& path: this->dashboardPinnedFiles) {
        files.emplace_back(pathFromStored(path));
    }
    return files;
}

void Settings::setDashboardPinnedFiles(const std::vector<fs::path>& files) {
    std::vector<std::string> stored;
    stored.reserve(files.size());
    for (const fs::path& file: files) {
        const std::string path = storedPath(file);
        // One file, one pin: pinning the same note twice must not make the dashboard show it twice.
        if (path.empty() || std::find(stored.begin(), stored.end(), path) != stored.end()) {
            continue;
        }
        stored.emplace_back(path);
    }

    if (stored == this->dashboardPinnedFiles) {
        return;
    }
    this->dashboardPinnedFiles = std::move(stored);
    save();
}

auto Settings::pinDashboardFile(const fs::path& file) -> bool {
    const std::string path = storedPath(file);
    if (path.empty() || std::find(this->dashboardPinnedFiles.begin(), this->dashboardPinnedFiles.end(), path) !=
                                this->dashboardPinnedFiles.end()) {
        return false;
    }

    this->dashboardPinnedFiles.emplace_back(path);
    save();
    return true;
}

auto Settings::unpinDashboardFile(const fs::path& file) -> bool {
    const std::string path = storedPath(file);
    const auto first = std::remove(this->dashboardPinnedFiles.begin(), this->dashboardPinnedFiles.end(), path);
    if (first == this->dashboardPinnedFiles.end()) {
        return false;
    }

    this->dashboardPinnedFiles.erase(first, this->dashboardPinnedFiles.end());
    save();
    return true;
}

auto Settings::setDashboardFolderRecursive(const fs::path& folder, bool recursive) -> bool {
    const std::string path = storedPath(folder);
    for (DashboardFolder& listed: this->dashboardFolders) {
        if (listed.path != path) {
            continue;
        }
        if (listed.recursive == recursive) {
            return false;
        }
        listed.recursive = recursive;
        save();
        return true;
    }
    return false;
}

auto Settings::getDashboardFolders() const -> const std::vector<DashboardFolder>& { return this->dashboardFolders; }

auto Settings::getRecentCommands() const -> const std::vector<std::string>& { return this->recentCommands; }

void Settings::addRecentCommand(const std::string& commandId) {
    if (commandId.empty()) {
        return;
    }

    auto alreadyThere = std::find(this->recentCommands.begin(), this->recentCommands.end(), commandId);
    if (alreadyThere != this->recentCommands.end()) {
        if (alreadyThere == this->recentCommands.begin()) {
            return;  // already the most recent one: nothing about the list changes
        }
        this->recentCommands.erase(alreadyThere);
    }
    this->recentCommands.insert(this->recentCommands.begin(), commandId);
    if (this->recentCommands.size() > RECENT_COMMANDS_MAX) {
        this->recentCommands.resize(RECENT_COMMANDS_MAX);
    }
    save();
}

void Settings::setRecentCommands(std::vector<std::string> commandIds) {
    commandIds.resize(std::min(commandIds.size(), RECENT_COMMANDS_MAX));
    if (commandIds == this->recentCommands) {
        return;
    }
    this->recentCommands = std::move(commandIds);
    save();
}

auto Settings::hasSeenInterfaceGuidance() const -> bool { return this->interfaceGuidanceSeen; }

void Settings::setInterfaceGuidanceSeen(bool seen) {
    if (this->interfaceGuidanceSeen == seen) {
        return;
    }
    this->interfaceGuidanceSeen = seen;
    save();
}

auto Settings::isInterfaceTipsEnabled() const -> bool { return this->interfaceTipsEnabled; }

void Settings::setInterfaceTipsEnabled(bool enabled) {
    if (this->interfaceTipsEnabled == enabled) {
        return;
    }
    this->interfaceTipsEnabled = enabled;
    save();
}

auto Settings::hasSeenTip(const std::string& tipId) const -> bool {
    return std::find(this->seenInterfaceTips.begin(), this->seenInterfaceTips.end(), tipId) !=
           this->seenInterfaceTips.end();
}

void Settings::markTipSeen(const std::string& tipId) {
    if (tipId.empty() || hasSeenTip(tipId)) {
        return;
    }
    this->seenInterfaceTips.emplace_back(tipId);
    save();
}

void Settings::resetInterfaceTips() {
    this->interfaceGuidanceSeen = false;
    this->interfaceTipsEnabled = true;
    this->seenInterfaceTips.clear();
    save();
}

void Settings::setDashboardFolders(const std::vector<DashboardFolder>& folders) {
    if (folders == this->dashboardFolders) {
        return;
    }
    this->dashboardFolders = folders;
    save();
}

auto Settings::addDashboardFolder(const fs::path& folder, bool recursive) -> bool {
    const std::string path = storedPath(folder);
    if (path.empty()) {
        return false;
    }

    const auto known = [&path](const DashboardFolder& listed) { return listed.path == path; };
    if (std::find_if(this->dashboardFolders.begin(), this->dashboardFolders.end(), known) !=
        this->dashboardFolders.end()) {
        return false;
    }

    this->dashboardFolders.push_back(DashboardFolder{path, recursive});
    save();
    return true;
}

auto Settings::removeDashboardFolder(const fs::path& folder) -> bool {
    const std::string path = storedPath(folder);
    const auto known = [&path](const DashboardFolder& listed) { return listed.path == path; };
    const auto first = std::remove_if(this->dashboardFolders.begin(), this->dashboardFolders.end(), known);
    if (first == this->dashboardFolders.end()) {
        return false;
    }

    this->dashboardFolders.erase(first, this->dashboardFolders.end());
    save();
    return true;
}

auto Settings::isHighlightPosition() const -> bool { return this->highlightPosition; }

void Settings::setHighlightPosition(bool highlight) {
    if (this->highlightPosition == highlight) {
        return;
    }

    this->highlightPosition = highlight;
    save();
}

auto Settings::getCursorHighlightColor() const -> Color { return this->cursorHighlightColor; }

void Settings::setCursorHighlightColor(Color color) {
    if (this->cursorHighlightColor != color) {
        this->cursorHighlightColor = color;
        save();
    }
}

auto Settings::getCursorHighlightRadius() const -> double { return this->cursorHighlightRadius; }

void Settings::setCursorHighlightRadius(double radius) {
    if (this->cursorHighlightRadius != radius) {
        this->cursorHighlightRadius = radius;
        save();
    }
}

auto Settings::getCursorHighlightBorderColor() const -> Color { return this->cursorHighlightBorderColor; }

void Settings::setCursorHighlightBorderColor(Color color) {
    if (this->cursorHighlightBorderColor != color) {
        this->cursorHighlightBorderColor = color;
        save();
    }
}

auto Settings::getCursorHighlightBorderWidth() const -> double { return this->cursorHighlightBorderWidth; }

void Settings::setCursorHighlightBorderWidth(double radius) {
    if (this->cursorHighlightBorderWidth != radius) {
        this->cursorHighlightBorderWidth = radius;
        save();
    }
}

auto Settings::isSnapRotation() const -> bool { return this->snapRotation; }

void Settings::setSnapRotation(bool b) {
    if (this->snapRotation == b) {
        return;
    }

    this->snapRotation = b;
    save();
}

auto Settings::getSnapRotationTolerance() const -> double { return this->snapRotationTolerance; }

void Settings::setSnapRotationTolerance(double tolerance) {
    this->snapRotationTolerance = tolerance;
    save();
}

auto Settings::isSnapGrid() const -> bool { return this->snapGrid; }

void Settings::setSnapGrid(bool b) {
    if (this->snapGrid == b) {
        return;
    }

    this->snapGrid = b;
    save();
}

void Settings::setSnapGridTolerance(double tolerance) {
    this->snapGridTolerance = tolerance;
    save();
}

auto Settings::getSnapGridTolerance() const -> double { return this->snapGridTolerance; }
auto Settings::getSnapGridSize() const -> double { return this->snapGridSize; };
void Settings::setSnapGridSize(double gridSize) {
    if (this->snapGridSize == gridSize) {
        return;
    }
    this->snapGridSize = gridSize;
    save();
}

auto Settings::getStrokeRecognizerMinSize() const -> double { return this->strokeRecognizerMinSize; };
void Settings::setStrokeRecognizerMinSize(double value) {
    if (this->strokeRecognizerMinSize == value) {
        return;
    }

    this->strokeRecognizerMinSize = value;
    save();
};

auto Settings::getTouchDrawingEnabled() const -> bool { return this->touchDrawing; }

void Settings::setTouchDrawingEnabled(bool b) {
    if (this->touchDrawing == b) {
        return;
    }

    this->touchDrawing = b;
    save();
}

auto Settings::getGtkTouchInertialScrollingEnabled() const -> bool { return this->gtkTouchInertialScrolling; };

void Settings::setGtkTouchInertialScrollingEnabled(bool b) {
    if (this->gtkTouchInertialScrolling == b) {
        return;
    }

    this->gtkTouchInertialScrolling = b;
    save();
}

auto Settings::isPressureGuessingEnabled() const -> bool { return this->pressureGuessing; }
void Settings::setPressureGuessingEnabled(bool b) {
    if (this->pressureGuessing == b) {
        return;
    }

    this->pressureGuessing = b;
    save();
}

double Settings::getMinimumPressure() const { return this->minimumPressure; }
void Settings::setMinimumPressure(double minimumPressure) {
    if (this->minimumPressure == minimumPressure) {
        return;
    }

    this->minimumPressure = minimumPressure;
    save();
}

double Settings::getPressureMultiplier() const { return this->pressureMultiplier; }
void Settings::setPressureMultiplier(double multiplier) {
    if (this->pressureMultiplier == multiplier) {
        return;
    }

    this->pressureMultiplier = multiplier;
    save();
}

auto Settings::getScrollbarHideType() const -> ScrollbarHideType { return this->scrollbarHideType; }

void Settings::setScrollbarHideType(ScrollbarHideType type) {
    if (this->scrollbarHideType == type) {
        return;
    }

    this->scrollbarHideType = type;

    save();
}

auto Settings::isAutoloadMostRecent() const -> bool { return this->autoloadMostRecent; }

void Settings::setAutoloadMostRecent(bool load) {
    if (this->autoloadMostRecent == load) {
        return;
    }
    this->autoloadMostRecent = load;
    save();
}

auto Settings::isAutoloadPdfXoj() const -> bool { return this->autoloadPdfXoj; }

void Settings::setAutoloadPdfXoj(bool load) {
    if (this->autoloadPdfXoj == load) {
        return;
    }
    this->autoloadPdfXoj = load;
    save();
}

auto Settings::getDefaultSaveName() const -> std::u8string const& { return this->defaultSaveName; }

void Settings::setDefaultSaveName(const std::u8string& name) {
    if (this->defaultSaveName == name) {
        return;
    }

    this->defaultSaveName = name;

    save();
}

auto Settings::getDefaultPdfExportName() const -> std::u8string const& { return this->defaultPdfExportName; }

void Settings::setDefaultPdfExportName(const std::u8string& name) {
    if (this->defaultPdfExportName == name) {
        return;
    }

    this->defaultPdfExportName = name;

    save();
}

auto Settings::getPageTemplateSettings() const -> PageTemplateSettings const& { return this->pageTemplateSettings; }

void Settings::setPageTemplateSettings(const PageTemplateSettings& newSettings) {
    if (this->pageTemplateSettings == newSettings) {
        return;
    }

    this->pageTemplateSettings = newSettings;

    save();
}

auto Settings::getSizeUnit() const -> string const& { return sizeUnit; }

void Settings::setSizeUnit(const string& sizeUnit) {
    if (this->sizeUnit == sizeUnit) {
        return;
    }

    this->sizeUnit = sizeUnit;

    save();
}

/**
 * Get size index in XOJ_UNITS
 */
auto Settings::getSizeUnitIndex() const -> int {
    string unit = getSizeUnit();

    for (int i = 0; i < XOJ_UNIT_COUNT; i++) {
        if (unit == XOJ_UNITS[i].name) {
            return i;
        }
    }

    return 0;
}

/**
 * Set size index in XOJ_UNITS
 */
void Settings::setSizeUnitIndex(int sizeUnitId) {
    if (sizeUnitId < 0 || sizeUnitId >= XOJ_UNIT_COUNT) {
        sizeUnitId = 0;
    }

    setSizeUnit(XOJ_UNITS[sizeUnitId].name);
}

void Settings::setShowPairedPages(bool showPairedPages) {
    if (this->showPairedPages == showPairedPages) {
        return;
    }

    this->showPairedPages = showPairedPages;
    save();
}

auto Settings::isShowPairedPages() const -> bool { return this->showPairedPages; }

void Settings::setShowPageShadow(bool showPageShadow) {
    if (this->showPageShadow == showPageShadow) {
        return;
    }

    this->showPageShadow = showPageShadow;
    save();
}

auto Settings::isShowPageShadow() const -> bool { return this->showPageShadow; }

void Settings::setPresentationMode(bool presentationMode) {
    if (this->presentationMode == presentationMode) {
        return;
    }
    if (presentationMode) {
        this->activeViewMode = PresetViewModeIds::VIEW_MODE_PRESENTATION;
    }
    this->presentationMode = presentationMode;
    save();
}

auto Settings::isPresentationMode() const -> bool {
    return this->activeViewMode == PresetViewModeIds::VIEW_MODE_PRESENTATION;
}

void Settings::setPressureSensitivity(gboolean presureSensitivity) {
    if (this->pressureSensitivity == presureSensitivity) {
        return;
    }
    this->pressureSensitivity = presureSensitivity;

    save();
}

void Settings::setPairsOffset(int numOffset) {
    if (this->numPairsOffset == numOffset) {
        return;
    }

    this->numPairsOffset = numOffset;
    save();
}

auto Settings::getPairsOffset() const -> int { return this->numPairsOffset; }

void Settings::setEmptyLastPageAppend(EmptyLastPageAppendType emptyLastPageAppend) {
    if (this->emptyLastPageAppend == emptyLastPageAppend) {
        return;
    }

    this->emptyLastPageAppend = emptyLastPageAppend;
    save();
}

auto Settings::getEmptyLastPageAppend() const -> EmptyLastPageAppendType { return this->emptyLastPageAppend; }

void Settings::setViewColumns(int numColumns) {
    if (this->numColumns == numColumns) {
        return;
    }

    this->numColumns = numColumns;
    save();
}

auto Settings::getViewColumns() const -> int { return this->numColumns; }


void Settings::setViewRows(int numRows) {
    if (this->numRows == numRows) {
        return;
    }

    this->numRows = numRows;
    save();
}

auto Settings::getViewRows() const -> int { return this->numRows; }

void Settings::setViewFixedRows(bool viewFixedRows) {
    if (this->viewFixedRows == viewFixedRows) {
        return;
    }

    this->viewFixedRows = viewFixedRows;
    save();
}

auto Settings::isViewFixedRows() const -> bool { return this->viewFixedRows; }

void Settings::setViewLayoutVert(bool vert) {
    if (this->layoutVertical == vert) {
        return;
    }

    this->layoutVertical = vert;
    save();
}

auto Settings::getViewLayoutVert() const -> bool { return this->layoutVertical; }

void Settings::setViewLayoutR2L(bool r2l) {
    if (this->layoutRightToLeft == r2l) {
        return;
    }

    this->layoutRightToLeft = r2l;
    save();
}

auto Settings::getViewLayoutR2L() const -> bool { return this->layoutRightToLeft; }

void Settings::setViewLayoutB2T(bool b2t) {
    if (this->layoutBottomToTop == b2t) {
        return;
    }

    this->layoutBottomToTop = b2t;
    save();
}

auto Settings::getViewLayoutB2T() const -> bool { return this->layoutBottomToTop; }

void Settings::setLastSavePath(fs::path p) {
    this->lastSavePath = std::move(p);
    save();
}

auto Settings::getLastSavePath() const -> fs::path const& { return this->lastSavePath; }

void Settings::setLastOpenPath(fs::path p) {
    this->lastOpenPath = std::move(p);
    save();
}

auto Settings::getLastOpenPath() const -> fs::path const& { return this->lastOpenPath; }

void Settings::setLastImagePath(const fs::path& path) {
    if (this->lastImagePath == path) {
        return;
    }
    this->lastImagePath = path;
    save();
}

auto Settings::getLastImagePath() const -> fs::path const& { return this->lastImagePath; }

void Settings::setZoomStep(double zoomStep) {
    if (this->zoomStep == zoomStep) {
        return;
    }
    this->zoomStep = zoomStep;
    save();
}

auto Settings::getZoomStep() const -> double { return this->zoomStep; }

void Settings::setZoomStepScroll(double zoomStepScroll) {
    if (this->zoomStepScroll == zoomStepScroll) {
        return;
    }
    this->zoomStepScroll = zoomStepScroll;
    save();
}

auto Settings::getZoomStepScroll() const -> double { return this->zoomStepScroll; }

void Settings::setForceZoomToFitOnLoad(bool force) {
    if (this->forceZoomToFitOnLoad == force) {
        return;
    }

    this->forceZoomToFitOnLoad = force;

    save();
}

auto Settings::getForceZoomToFitOnLoad() const -> bool { return this->forceZoomToFitOnLoad; }

void Settings::setEdgePanSpeed(double speed) {
    if (this->edgePanSpeed == speed) {
        return;
    }
    this->edgePanSpeed = speed;
    save();
}

auto Settings::getEdgePanSpeed() const -> double { return this->edgePanSpeed; }

void Settings::setEdgePanMaxMult(double maxMult) {
    if (this->edgePanMaxMult == maxMult) {
        return;
    }
    this->edgePanMaxMult = maxMult;
    save();
}

auto Settings::getEdgePanMaxMult() const -> double { return this->edgePanMaxMult; }

void Settings::setDisplayDpi(int dpi) {
    if (this->displayDpi == dpi) {
        return;
    }
    this->displayDpi = dpi;
    save();
}

auto Settings::getDisplayDpi() const -> int { return this->displayDpi; }

void Settings::setAreStockIconsUsed(bool use) {
    if (this->useStockIcons == use) {
        return;
    }
    this->useStockIcons = use;
    save();
}

auto Settings::areStockIconsUsed() const -> bool { return this->useStockIcons; }

auto Settings::isFullscreen() const -> bool { return this->fullscreenActive; }

auto Settings::isSidebarVisible() const -> bool { return this->showSidebar; }

void Settings::setSidebarVisible(bool visible) {
    if (this->showSidebar == visible) {
        return;
    }
    this->showSidebar = visible;
    save();
}

auto Settings::isToolbarVisible() const -> bool { return this->showToolbar; }

void Settings::setToolbarVisible(bool visible) {
    if (this->showToolbar == visible) {
        return;
    }
    this->showToolbar = visible;
    save();
}

auto Settings::getSidebarWidth() const -> int { return this->sidebarWidth; }

void Settings::setSidebarWidth(int width) {
    width = std::max(width, 50);

    if (this->sidebarWidth == width) {
        return;
    }
    this->sidebarWidth = width;
    save();
}

void Settings::setMainWndSize(int width, int height) {
    this->mainWndWidth = width;
    this->mainWndHeight = height;

    save();
}

auto Settings::getMainWndWidth() const -> int { return this->mainWndWidth; }

auto Settings::getMainWndHeight() const -> int { return this->mainWndHeight; }

auto Settings::isMainWndMaximized() const -> bool { return this->maximized; }

void Settings::setMainWndMaximized(bool max) {
    if (this->maximized == max) {
        return;
    }
    this->maximized = max;
    save();
}

void Settings::setSelectedToolbar(const string& name) {
    if (this->selectedToolbar == name) {
        return;
    }
    this->selectedToolbar = name;
    // Remember the choice for the workspace it was made in, so that switching workspaces
    // does not overwrite the other workspace's toolbar.
    auto& workspaceToolbar = this->workspaceMode == WorkspaceMode::FOCUS ? this->focusToolbar : this->classicToolbar;
    workspaceToolbar = name;
    save();
}

auto Settings::getSelectedToolbar() const -> string const& { return this->selectedToolbar; }

auto Settings::getDefaultWorkspaceToolbar(WorkspaceMode mode) -> string const& {
    static const string focusToolbar = FOCUS_TOOLBAR;
    static const string classicToolbar = DEFAULT_TOOLBAR;
    return mode == WorkspaceMode::FOCUS ? focusToolbar : classicToolbar;
}

auto Settings::getWorkspaceToolbar(WorkspaceMode mode) const -> string const& {
    const string& toolbar = mode == WorkspaceMode::FOCUS ? this->focusToolbar : this->classicToolbar;
    return toolbar.empty() ? getDefaultWorkspaceToolbar(mode) : toolbar;
}

auto Settings::isWorkspaceMenubarVisible(WorkspaceMode mode) const -> bool {
    return mode == WorkspaceMode::FOCUS ? this->focusMenubarVisible : this->classicMenubarVisible;
}

auto Settings::getToolPresets() const -> const ToolPresetList& { return this->toolPresets; }

void Settings::setToolPresets(ToolPresetList presets) {
    this->toolPresets = std::move(presets);
    save();
}

auto Settings::getFavoritePresetCount() const -> int { return this->favoritePresetCount; }

void Settings::setFavoritePresetCount(int count) {
    const int clamped = std::clamp(count, 0, static_cast<int>(ToolPresetList::MAX_FAVORITES));
    if (this->favoritePresetCount == clamped) {
        return;
    }

    this->favoritePresetCount = clamped;
    save();
}

auto Settings::getWorkspaceMode() const -> WorkspaceMode { return this->workspaceMode; }

void Settings::setWorkspaceMode(WorkspaceMode mode) {
    if (this->workspaceMode == mode) {
        return;
    }

    // The live values are swapped for the new workspace's remembered ones. They are kept in
    // sync with the active workspace by setSelectedToolbar()/setMenubarVisible().
    this->workspaceMode = mode;
    this->selectedToolbar = this->getWorkspaceToolbar(mode);
    this->menubarVisible = this->isWorkspaceMenubarVisible(mode);
    save();
}

void Settings::resolveWorkspaceAfterLoad(bool profileExisted) {
    if (!this->workspaceModeLoaded) {
        // No workspace field: either a profile written before workspaces existed, or a
        // freshly regenerated one. An established profile keeps its layout (Classic) so the
        // update does not silently change it; only a fresh profile starts in Focus.
        this->workspaceMode = profileExisted ? WorkspaceMode::CLASSIC : WorkspaceMode::FOCUS;
    }

    // The flat keys describe the workspace that was in use when the file was written.
    if (this->selectedToolbarLoaded) {
        (this->workspaceMode == WorkspaceMode::FOCUS ? this->focusToolbar : this->classicToolbar) =
                this->selectedToolbar;
    }
    if (this->menubarVisibleLoaded) {
        (this->workspaceMode == WorkspaceMode::FOCUS ? this->focusMenubarVisible : this->classicMenubarVisible) =
                this->menubarVisible;
    }

    // The live values always mirror the active workspace.
    this->selectedToolbar = this->getWorkspaceToolbar(this->workspaceMode);
    this->menubarVisible = this->isWorkspaceMenubarVisible(this->workspaceMode);
}

auto Settings::getCustomElement(const string& name) -> SElement& { return this->data[name]; }

void Settings::customSettingsChanged() { save(); }

auto Settings::getButtonConfig(unsigned int id) -> ButtonConfig* {
    if (id >= this->buttonConfig.size()) {
        g_error("Settings::getButtonConfig try to get id=%i out of range!", id);
        return nullptr;
    }
    return this->buttonConfig[id].get();
}

void Settings::setViewMode(ViewModeId mode, ViewMode viewMode) {
    if (this->viewModes[mode] == viewMode) {
        return;
    }
    this->viewModes.at(mode) = viewMode;
    save();
}

auto Settings::getTouchZoomStartThreshold() const -> double { return this->touchZoomStartThreshold; }
void Settings::setTouchZoomStartThreshold(double threshold) {
    if (this->touchZoomStartThreshold == threshold) {
        return;
    }

    this->touchZoomStartThreshold = threshold;
    save();
}


auto Settings::getPDFPageRerenderThreshold() const -> double { return this->pageRerenderThreshold; }
void Settings::setPDFPageRerenderThreshold(double threshold) {
    if (this->pageRerenderThreshold == threshold) {
        return;
    }

    this->pageRerenderThreshold = threshold;
    save();
}

auto Settings::getPdfPageCacheSize() const -> int { return this->pdfPageCacheSize; }

void Settings::setPdfPageCacheSize(int size) {
    if (this->pdfPageCacheSize == size) {
        return;
    }
    this->pdfPageCacheSize = size;
    save();
}

auto Settings::getPreloadPagesBefore() const -> unsigned int { return this->preloadPagesBefore; }

void Settings::setPreloadPagesBefore(unsigned int n) {
    if (this->preloadPagesBefore == n) {
        return;
    }
    this->preloadPagesBefore = n;
    save();
}

auto Settings::getPreloadPagesAfter() const -> unsigned int { return this->preloadPagesAfter; }

void Settings::setPreloadPagesAfter(unsigned int n) {
    if (this->preloadPagesAfter == n) {
        return;
    }
    this->preloadPagesAfter = n;
    save();
}

auto Settings::isEagerPageCleanup() const -> bool { return this->eagerPageCleanup; }

void Settings::setEagerPageCleanup(bool b) {
    if (this->eagerPageCleanup == b) {
        return;
    }
    this->eagerPageCleanup = b;
    save();
}

auto Settings::getBorderColor() const -> Color { return this->selectionBorderColor; }

void Settings::setBorderColor(Color color) {
    if (this->selectionBorderColor == color) {
        return;
    }
    this->selectionBorderColor = color;
    save();
}

auto Settings::getSelectionColor() const -> Color { return this->selectionMarkerColor; }

void Settings::setSelectionColor(Color color) {
    if (this->selectionMarkerColor == color) {
        return;
    }
    this->selectionMarkerColor = color;
    save();
}

auto Settings::getActiveSelectionColor() const -> Color { return this->activeSelectionColor; }

void Settings::setActiveSelectionColor(Color color) {
    if (this->activeSelectionColor == color) {
        return;
    }
    this->activeSelectionColor = color;
    save();
}

auto Settings::getRecolorParameters() const -> const RecolorParameters& { return this->recolorParameters; }

void Settings::setRecolorParameters(RecolorParameters&& recolor) {
    if (this->recolorParameters == recolor) {
        return;
    }
    this->recolorParameters = recolor;
    save();
}

auto Settings::getBackgroundColor() const -> Color { return this->backgroundColor; }

void Settings::setBackgroundColor(Color color) {
    if (this->backgroundColor == color) {
        return;
    }
    this->backgroundColor = color;
    save();
}

auto Settings::getFont() -> XojFont& { return this->font; }

void Settings::setFont(const XojFont& font) {
    this->font = font;
    save();
}

#ifdef ENABLE_AUDIO
auto Settings::getAudioFolder() const -> fs::path const& { return this->audioFolder; }

void Settings::setAudioFolder(fs::path audioFolder) {
    if (this->audioFolder == audioFolder) {
        return;
    }

    this->audioFolder = std::move(audioFolder);

    save();
}

auto Settings::getAudioInputDevice() const -> PaDeviceIndex { return this->audioInputDevice; }

void Settings::setAudioInputDevice(PaDeviceIndex deviceIndex) {
    if (this->audioInputDevice == deviceIndex) {
        return;
    }
    this->audioInputDevice = deviceIndex;
    save();
}

auto Settings::getAudioOutputDevice() const -> PaDeviceIndex { return this->audioOutputDevice; }

void Settings::setAudioOutputDevice(PaDeviceIndex deviceIndex) {
    if (this->audioOutputDevice == deviceIndex) {
        return;
    }
    this->audioOutputDevice = deviceIndex;
    save();
}

auto Settings::getAudioSampleRate() const -> double { return this->audioSampleRate; }

void Settings::setAudioSampleRate(double sampleRate) {
    if (this->audioSampleRate == sampleRate) {
        return;
    }
    this->audioSampleRate = sampleRate;
    save();
}

auto Settings::getAudioGain() const -> double { return this->audioGain; }

void Settings::setAudioGain(double gain) {
    if (this->audioGain == gain) {
        return;
    }
    this->audioGain = gain;
    save();
}

auto Settings::getDefaultSeekTime() const -> unsigned int { return this->defaultSeekTime; }

void Settings::setDefaultSeekTime(unsigned int t) {
    if (this->defaultSeekTime == t) {
        return;
    }
    this->defaultSeekTime = t;
    save();
}
#endif

auto Settings::getPluginEnabled() const -> string const& { return this->pluginEnabled; }

void Settings::setPluginEnabled(const string& pluginEnabled) {
    if (this->pluginEnabled == pluginEnabled) {
        return;
    }
    this->pluginEnabled = pluginEnabled;
    save();
}

auto Settings::getPluginDisabled() const -> string const& { return this->pluginDisabled; }

void Settings::setPluginDisabled(const string& pluginDisabled) {
    if (this->pluginDisabled == pluginDisabled) {
        return;
    }
    this->pluginDisabled = pluginDisabled;
    save();
}


void Settings::getStrokeFilter(int* ignoreTime, double* ignoreLength, int* successiveTime) const {
    *ignoreTime = this->strokeFilterIgnoreTime;
    *ignoreLength = this->strokeFilterIgnoreLength;
    *successiveTime = this->strokeFilterSuccessiveTime;
}

void Settings::setStrokeFilter(int ignoreTime, double ignoreLength, int successiveTime) {
    this->strokeFilterIgnoreTime = ignoreTime;
    this->strokeFilterIgnoreLength = ignoreLength;
    this->strokeFilterSuccessiveTime = successiveTime;
}

void Settings::setStrokeFilterEnabled(bool enabled) { this->strokeFilterEnabled = enabled; }

auto Settings::getStrokeFilterEnabled() const -> bool { return this->strokeFilterEnabled; }

void Settings::setDoActionOnStrokeFiltered(bool enabled) { this->doActionOnStrokeFiltered = enabled; }

auto Settings::getDoActionOnStrokeFiltered() const -> bool { return this->doActionOnStrokeFiltered; }

void Settings::setTrySelectOnStrokeFiltered(bool enabled) { this->trySelectOnStrokeFiltered = enabled; }

auto Settings::getTrySelectOnStrokeFiltered() const -> bool { return this->trySelectOnStrokeFiltered; }

void Settings::setSnapRecognizedShapesEnabled(bool enabled) { this->snapRecognizedShapesEnabled = enabled; }

auto Settings::getSnapRecognizedShapesEnabled() const -> bool { return this->snapRecognizedShapesEnabled; }


void Settings::setRestoreLineWidthEnabled(bool enabled) { this->restoreLineWidthEnabled = enabled; }

auto Settings::getRestoreLineWidthEnabled() const -> bool { return this->restoreLineWidthEnabled; }

auto Settings::setPreferredLocale(std::string const& locale) -> void { this->preferredLocale = locale; }

auto Settings::getPreferredLocale() const -> std::string { return this->preferredLocale; }

void Settings::setIgnoredStylusEvents(int numEvents) {
    if (this->numIgnoredStylusEvents == numEvents) {
        return;
    }
    this->numIgnoredStylusEvents = std::max<int>(numEvents, 0);
    save();
}

auto Settings::getIgnoredStylusEvents() const -> int { return this->numIgnoredStylusEvents; }

void Settings::setInputSystemTPCButtonEnabled(bool tpcButtonEnabled) {
    if (this->inputSystemTPCButton == tpcButtonEnabled) {
        return;
    }
    this->inputSystemTPCButton = tpcButtonEnabled;
    save();
}

auto Settings::getInputSystemTPCButtonEnabled() const -> bool { return this->inputSystemTPCButton; }

void Settings::setInputSystemDrawOutsideWindowEnabled(bool drawOutsideWindowEnabled) {
    if (this->inputSystemDrawOutsideWindow == drawOutsideWindowEnabled) {
        return;
    }
    this->inputSystemDrawOutsideWindow = drawOutsideWindowEnabled;
    save();
}

auto Settings::getInputSystemDrawOutsideWindowEnabled() const -> bool { return this->inputSystemDrawOutsideWindow; }

void Settings::setDeviceClassForDevice(GdkDevice* device, InputDeviceTypeOption deviceClass) {
    this->setDeviceClassForDevice(gdk_device_get_name(device), gdk_device_get_source(device), deviceClass);
}

void Settings::setDeviceClassForDevice(const string& deviceName, GdkInputSource deviceSource,
                                       InputDeviceTypeOption deviceClass) {
    auto it = inputDeviceClasses.find(deviceName);
    if (it != inputDeviceClasses.end()) {
        it->second.first = deviceClass;
        it->second.second = deviceSource;
    } else {
        inputDeviceClasses.emplace(deviceName, std::make_pair(deviceClass, deviceSource));
    }
}

auto Settings::getKnownInputDevices() const -> std::vector<InputDevice> {
    std::vector<InputDevice> inputDevices;
    for (auto pair: inputDeviceClasses) {
        const std::string& name = pair.first;
        GdkInputSource& source = pair.second.second;
        inputDevices.emplace_back(name, source);
    }
    return inputDevices;
}

auto Settings::getDeviceClassForDevice(GdkDevice* device) const -> InputDeviceTypeOption {
    return this->getDeviceClassForDevice(gdk_device_get_name(device), gdk_device_get_source(device));
}

auto Settings::getDeviceClassForDevice(const string& deviceName, GdkInputSource deviceSource) const
        -> InputDeviceTypeOption {
    auto search = inputDeviceClasses.find(deviceName);
    if (search != inputDeviceClasses.end()) {
        return search->second.first;
    }


    InputDeviceTypeOption deviceType = InputDeviceTypeOption::Disabled;
    switch (deviceSource) {
        case GDK_SOURCE_CURSOR:
#if (GDK_MAJOR_VERSION >= 3 && GDK_MINOR_VERSION >= 22)
        case GDK_SOURCE_TABLET_PAD:
#endif
        case GDK_SOURCE_KEYBOARD:
            deviceType = InputDeviceTypeOption::Disabled;
            break;
        case GDK_SOURCE_MOUSE:
        case GDK_SOURCE_TOUCHPAD:
#if (GDK_MAJOR_VERSION >= 3 && GDK_MINOR_VERSION >= 22)
        case GDK_SOURCE_TRACKPOINT:
#endif
            deviceType = InputDeviceTypeOption::Mouse;
            break;
        case GDK_SOURCE_PEN:
            deviceType = InputDeviceTypeOption::Pen;
            break;
        case GDK_SOURCE_ERASER:
            deviceType = InputDeviceTypeOption::Eraser;
            break;
        case GDK_SOURCE_TOUCHSCREEN:
            deviceType = InputDeviceTypeOption::Touchscreen;
            break;
        default:
            deviceType = InputDeviceTypeOption::Disabled;
    }
    return deviceType;
}

auto Settings::isScrollbarFadeoutDisabled() const -> bool { return disableScrollbarFadeout; }

void Settings::setScrollbarFadeoutDisabled(bool disable) {
    if (disableScrollbarFadeout == disable) {
        return;
    }
    disableScrollbarFadeout = disable;
    save();
}

auto Settings::isAudioDisabled() const -> bool { return disableAudio; }

void Settings::setAudioDisabled(bool disable) {
    if (disableAudio == disable) {
        return;
    }
    disableAudio = disable;
    save();
}

//////////////////////////////////////////////////

SAttribute::SAttribute() {
    this->dValue = 0;
    this->iValue = 0;
    this->type = ATTRIBUTE_TYPE_NONE;
}

SAttribute::SAttribute(const SAttribute& attrib) { *this = attrib; }

SAttribute::~SAttribute() {
    this->iValue = 0;
    this->type = ATTRIBUTE_TYPE_NONE;
}

//////////////////////////////////////////////////

auto SElement::attributes() -> std::map<string, SAttribute>& { return this->element->attributes; }

auto SElement::children() -> std::map<string, SElement>& { return this->element->children; }

void SElement::clear() {
    this->element->attributes.clear();
    this->element->children.clear();
}

auto SElement::child(const string& name) -> SElement& { return this->element->children[name]; }

void SElement::setComment(const string& name, const string& comment) {
    SAttribute& attrib = this->element->attributes[name];
    attrib.comment = comment;
}

void SElement::setIntHex(const string& name, const int value) {
    SAttribute& attrib = this->element->attributes[name];
    attrib.iValue = value;
    attrib.type = ATTRIBUTE_TYPE_INT_HEX;
}

void SElement::setInt(const string& name, const int value) {
    SAttribute& attrib = this->element->attributes[name];
    attrib.iValue = value;
    attrib.type = ATTRIBUTE_TYPE_INT;
}

void SElement::setBool(const string& name, const bool value) {
    SAttribute& attrib = this->element->attributes[name];
    attrib.iValue = value;
    attrib.type = ATTRIBUTE_TYPE_BOOLEAN;
}

void SElement::setString(const string& name, const string& value) {
    SAttribute& attrib = this->element->attributes[name];
    attrib.sValue = value;
    attrib.type = ATTRIBUTE_TYPE_STRING;
}

void SElement::setDouble(const string& name, const double value) {
    SAttribute& attrib = this->element->attributes[name];
    attrib.dValue = value;
    attrib.type = ATTRIBUTE_TYPE_DOUBLE;
}

auto SElement::getDouble(const string& name, double& value) -> bool {
    SAttribute& attrib = this->element->attributes[name];
    if (attrib.type == ATTRIBUTE_TYPE_NONE) {
        this->element->attributes.erase(name);
        return false;
    }

    if (attrib.type != ATTRIBUTE_TYPE_DOUBLE) {
        return false;
    }

    value = attrib.dValue;

    return true;
}

auto SElement::getInt(const string& name, int& value) -> bool {
    SAttribute& attrib = this->element->attributes[name];
    if (attrib.type == ATTRIBUTE_TYPE_NONE) {
        this->element->attributes.erase(name);
        return false;
    }

    if (attrib.type != ATTRIBUTE_TYPE_INT && attrib.type != ATTRIBUTE_TYPE_INT_HEX) {
        return false;
    }

    value = attrib.iValue;

    return true;
}

auto SElement::getBool(const string& name, bool& value) -> bool {
    SAttribute& attrib = this->element->attributes[name];
    if (attrib.type == ATTRIBUTE_TYPE_NONE) {
        this->element->attributes.erase(name);
        return false;
    }

    if (attrib.type != ATTRIBUTE_TYPE_BOOLEAN) {
        return false;
    }

    value = attrib.iValue;

    return true;
}

auto SElement::getString(const string& name, string& value) -> bool {
    SAttribute& attrib = this->element->attributes[name];
    if (attrib.type == ATTRIBUTE_TYPE_NONE) {
        this->element->attributes.erase(name);
        return false;
    }

    if (attrib.type != ATTRIBUTE_TYPE_STRING) {
        return false;
    }

    value = attrib.sValue;

    return true;
}

/**
 * Stabilizer related getters and setters
 */
auto Settings::getStabilizerCuspDetection() const -> bool { return stabilizerCuspDetection; }
auto Settings::getStabilizerFinalizeStroke() const -> bool { return stabilizerFinalizeStroke; }
auto Settings::getStabilizerBuffersize() const -> size_t { return stabilizerBuffersize; }
auto Settings::getStabilizerDeadzoneRadius() const -> double { return stabilizerDeadzoneRadius; }
auto Settings::getStabilizerDrag() const -> double { return stabilizerDrag; }
auto Settings::getStabilizerMass() const -> double { return stabilizerMass; }
auto Settings::getStabilizerSigma() const -> double { return stabilizerSigma; }
auto Settings::getStabilizerAveragingMethod() const -> StrokeStabilizer::AveragingMethod {
    return stabilizerAveragingMethod;
}
auto Settings::getStabilizerPreprocessor() const -> StrokeStabilizer::Preprocessor { return stabilizerPreprocessor; }

void Settings::setStabilizerCuspDetection(bool cuspDetection) {
    if (stabilizerCuspDetection == cuspDetection) {
        return;
    }
    stabilizerCuspDetection = cuspDetection;
    save();
}
void Settings::setStabilizerFinalizeStroke(bool finalizeStroke) {
    if (stabilizerFinalizeStroke == finalizeStroke) {
        return;
    }
    stabilizerFinalizeStroke = finalizeStroke;
    save();
}
void Settings::setStabilizerBuffersize(size_t buffersize) {
    if (stabilizerBuffersize == buffersize) {
        return;
    }
    stabilizerBuffersize = buffersize;
    save();
}
void Settings::setStabilizerDeadzoneRadius(double deadzoneRadius) {
    if (stabilizerDeadzoneRadius == deadzoneRadius) {
        return;
    }
    stabilizerDeadzoneRadius = deadzoneRadius;
    save();
}
void Settings::setStabilizerDrag(double drag) {
    if (stabilizerDrag == drag) {
        return;
    }
    stabilizerDrag = drag;
    save();
}
void Settings::setStabilizerMass(double mass) {
    if (stabilizerMass == mass) {
        return;
    }
    stabilizerMass = mass;
    save();
}
void Settings::setStabilizerSigma(double sigma) {
    if (stabilizerSigma == sigma) {
        return;
    }
    stabilizerSigma = sigma;
    save();
}
void Settings::setStabilizerAveragingMethod(StrokeStabilizer::AveragingMethod averagingMethod) {
    const StrokeStabilizer::AveragingMethod method =
            StrokeStabilizer::isValid(averagingMethod) ? averagingMethod : StrokeStabilizer::AveragingMethod::NONE;

    if (stabilizerAveragingMethod == method) {
        return;
    }
    stabilizerAveragingMethod = method;
    save();
}
void Settings::setStabilizerPreprocessor(StrokeStabilizer::Preprocessor preprocessor) {
    const StrokeStabilizer::Preprocessor p =
            StrokeStabilizer::isValid(preprocessor) ? preprocessor : StrokeStabilizer::Preprocessor::NONE;

    if (stabilizerPreprocessor == p) {
        return;
    }
    stabilizerPreprocessor = p;
    save();
}


auto Settings::getColorPaletteSetting() -> fs::path const& { return this->colorPaletteSetting; }

void Settings::setColorPaletteSetting(fs::path palettePath) { this->colorPaletteSetting = palettePath; }


void Settings::setUseSpacesAsTab(bool useSpaces) { this->useSpacesForTab = useSpaces; }
bool Settings::getUseSpacesAsTab() const { return this->useSpacesForTab; }

void Settings::setNumberOfSpacesForTab(unsigned int numberOfSpaces) {
    if (this->numberOfSpacesForTab == numberOfSpaces) {
        return;
    }

    // For performance reasons the number of spaces for a tab should be limited
    // if this limit is exceeded use a default value
    if (numberOfSpaces < 0 || numberOfSpaces > MAX_SPACES_FOR_TAB) {
        g_warning("Settings::Invalid number of spaces for tab. Reset to default!");
        numberOfSpaces = 4;
    }
    this->numberOfSpacesForTab = numberOfSpaces;
    save();
}

unsigned int Settings::getNumberOfSpacesForTab() const { return this->numberOfSpacesForTab; }

void Settings::setLaserPointerFadeOutTime(unsigned int timeInMs) {
    if (this->laserPointerFadeOutTime == timeInMs) {
        return;
    }
    this->laserPointerFadeOutTime = timeInMs;
    save();
}

unsigned int Settings::getLaserPointerFadeOutTime() const { return this->laserPointerFadeOutTime; }

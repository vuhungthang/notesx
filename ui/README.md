# Icons

The `.svg` icons contained in this folder were downsized with

    svgo -f ./actions ./actions_new

in `fe5d254f2e22e213b2e2b1945e83432c1e36806e`.

The original icons, before downsizing, are still present in the git history in the last commit before the downsizing `86e1939ef4f6aeccffc66395fcf820f023316450`.
In case these files are preferable when working with the icon files please consider checking out this commit.

## Icon resolution

Four icon sets are bundled, each a GTK icon theme in its own search path:

| Directory | Role |
| --- | --- |
| `iconsLucide-light` | Lucide-style action set, light stroke colour |
| `iconsLucide-dark` | Lucide-style action set, dark stroke colour |
| `iconsColor-light` | Original custom action set, light |
| `iconsColor-dark` | Original custom action set, dark |

`MainWindow::setThemeVariant()` prepends the four directories to the GTK icon theme search path
in a theme-dependent priority order (`src/core/gui/MainWindow.cpp`):

- `ICON_THEME_LUCIDE`: color sets are lowest priority, Lucide sets highest.
- `ICON_THEME_COLOR`: Lucide sets are lowest priority, color sets highest.
- Within each pair the light/dark member matching the active theme variant is preferred.

Custom icons are requested as `xopp-<name>` (see `IconNameHelper::iconName()`). A name that is
missing from every bundled set is resolved by the user's system icon theme, which is the
"fallback" state this inventory tracks.

### Inventory

Every custom icon name requested by a toolbar/tool definition, and whether each bundled set
supplies it. "system" in the last column means the name is absent from **all four** directories,
so it can only come from the system theme.

| Icon name | Lucide | Color | System fallback |
| --- | --- | --- | --- |
| `xopp-audio-playback-pause` | yes | **no** | — |
| `xopp-audio-playback-stop` | yes | **no** | — |
| `xopp-audio-record` | yes | **no** | — |
| `xopp-audio-seek-backwards` | yes | **no** | — |
| `xopp-audio-seek-forwards` | yes | **no** | — |
| `xopp-combo-drawing-type` | yes | yes | — |
| `xopp-combo-layer` | yes | yes | — |
| `xopp-combo-selection` | yes | **no** | — |
| `xopp-compass` | yes | **no** | — |
| `xopp-default` | yes | **no** | — |
| `xopp-document-export-pdf` | yes | **no** | — |
| `xopp-document-new` | yes | **no** | — |
| `xopp-document-open` | yes | **no** | — |
| `xopp-document-print` | yes | **no** | — |
| `xopp-document-save` | yes | **no** | — |
| `xopp-draw-arrow` | yes | yes | — |
| `xopp-draw-coordinate-system` | yes | yes | — |
| `xopp-draw-double-arrow` | yes | yes | — |
| `xopp-draw-ellipse` | yes | yes | — |
| `xopp-draw-line` | yes | yes | — |
| `xopp-draw-rect` | yes | yes | — |
| `xopp-draw-spline` | yes | yes | — |
| `xopp-edit-copy` | yes | **no** | — |
| `xopp-edit-cut` | yes | **no** | — |
| `xopp-edit-paste` | yes | **no** | — |
| `xopp-edit-redo` | yes | **no** | — |
| `xopp-edit-undo` | yes | **no** | — |
| `xopp-fill` | yes | **no** | — |
| `xopp-fill-opacity` | yes | **no** | — |
| `xopp-floating-toolbox` | yes | **no** | — |
| `xopp-font-button` | yes | **no** | — |
| `xopp-fullscreen` | yes | **no** | — |
| `xopp-go-to` | yes | **no** | — |
| `xopp-hand` | yes | **no** | — |
| `xopp-laser-pointer` | yes | yes | — |
| `xopp-line-style-dash` | yes | yes | — |
| `xopp-line-style-dash-dot` | yes | yes | — |
| `xopp-line-style-dash-dot-with-pen` | yes | yes | — |
| `xopp-line-style-dash-with-pen` | yes | yes | — |
| `xopp-line-style-dot` | yes | yes | — |
| `xopp-line-style-dot-with-pen` | yes | yes | — |
| `xopp-line-style-plain` | yes | yes | — |
| `xopp-line-style-plain-with-pen` | yes | yes | — |
| `xopp-move` | yes | **no** | — |
| `xopp-navigate-back` | yes | yes | — |
| `xopp-navigate-forward` | yes | yes | — |
| `xopp-object-play` | yes | **no** | — |
| `xopp-object-select` | yes | **no** | — |
| `xopp-orientation-landscape` | yes | yes | — |
| `xopp-orientation-portrait` | yes | yes | — |
| `xopp-page-add` | yes | **no** | — |
| `xopp-page-annotated-next` | yes | **no** | — |
| `xopp-page-delete` | yes | **no** | — |
| `xopp-page-spinner` | yes | **no** | — |
| `xopp-presentation-mode` | yes | yes | — |
| `xopp-select-lasso` | yes | **no** | — |
| `xopp-select-multilayer-lasso` | yes | **no** | — |
| `xopp-select-multilayer-rect` | yes | **no** | — |
| `xopp-select-pdf-text-area` | yes | yes | — |
| `xopp-select-pdf-text-ht` | yes | yes | — |
| `xopp-select-rect` | yes | **no** | — |
| `xopp-separator` | yes | **no** | — |
| `xopp-setsquare` | yes | **no** | — |
| `xopp-shape-recognizer` | yes | **no** | — |
| `xopp-show-paired-pages` | yes | **no** | — |
| `xopp-sidebar-index` | yes | **no** | — |
| `xopp-sidebar-layer` | yes | yes | — |
| `xopp-sidebar-layerstack` | yes | yes | — |
| `xopp-sidebar-page-preview` | yes | **no** | — |
| `xopp-sidebar-show` | yes | yes | — |
| `xopp-snapping-grid` | yes | **no** | — |
| `xopp-snapping-rotation` | yes | yes | — |
| `xopp-spacer` | yes | **no** | — |
| `xopp-star` | yes | **no** | — |
| `xopp-thickness-fine` | yes | yes | — |
| `xopp-thickness-finer` | yes | yes | — |
| `xopp-thickness-medium` | yes | yes | — |
| `xopp-thickness-thick` | yes | yes | — |
| `xopp-thickness-thicker` | yes | yes | — |
| `xopp-tool-eraser` | yes | **no** | — |
| `xopp-tool-highlighter` | yes | **no** | — |
| `xopp-tool-image` | yes | **no** | — |
| `xopp-tool-link` | yes | yes | — |
| `xopp-tool-math-tex` | yes | yes | — |
| `xopp-tool-pencil` | yes | **no** | — |
| `xopp-tool-properties` | yes | **no** | — |
| `xopp-tool-text` | yes | yes | — |
| `xopp-toolbars-customize` | yes | **no** | — |
| `xopp-toolbars-manage` | yes | **no** | — |
| `xopp-touch-drawing` | yes | yes | — |
| `xopp-transparent` | yes | **no** | — |
| `xopp-vertical-space` | yes | **no** | — |
| `xopp-wrap` | yes | **no** | — |
| `xopp-zoom-slider` | yes | **no** | — |

The list was derived from `src/`: `iconName("...")` arguments, `emplaceCustomItem*()` icon
arguments, hard-coded `"xopp-..."` constants, the `addTypeCB()` table in
`src/core/gui/dialog/ButtonConfigGui.cpp`, and the `stacked ? "sidebar-layerstack" :
"sidebar-layer"` ternary in `src/core/gui/sidebar/previews/layer/SidebarPreviewLayers.cpp`.
Adding a new toolbar item means adding a row here.

### Gaps closed by plan 001

Six names were absent from both Lucide sets and are added by plan 001 to
`ui/iconsLucide-light` and `ui/iconsLucide-dark`:

| Icon name | Previously resolved from | Upstream source |
| --- | --- | --- |
| `xopp-orientation-landscape` | `iconsColor-*` only | Lucide `rectangle-horizontal` |
| `xopp-orientation-portrait` | `iconsColor-*` only | Lucide `rectangle-vertical` |
| `xopp-page-spinner` | system theme (a stale 24x24 copy existed in `iconsLucide-dark` only) | Lucide `file` outline plus authored chevrons |
| `xopp-select-multilayer-lasso` | `iconsColor-dark` only | Lucide `lasso-select` |
| `xopp-select-multilayer-rect` | `iconsColor-dark` only | Lucide `square-dashed-mouse-pointer` |
| `xopp-tool-link` | `iconsColor-light` only | Lucide `link` |

All of them reuse upstream Lucide artwork (ISC, Lucide Contributors), which is already covered
by the `ui/iconsLucide-light/*` and `ui/iconsLucide-dark/*` entries in `copyright.txt`. The only
authored part is the chevron pair inside `xopp-page-spinner`, drawn to the same 24x24 / 2 px
stroke conventions as the rest of the set. Each icon exists in both the light (`#566d86`) and
the dark (`#bdbdbd`) variant, matching the existing files, because the set encodes its stroke
colour per file.

The stray fixed-size copy at
`ui/iconsLucide-dark/hicolor/24x24/actions/xopp-page-spinner.svg` (the only file in that
directory, inconsistent with the rest of the scalable set) is removed.

### Resolution check

Directory membership is not the whole story. GTK prefers a fixed-size icon directory whose size
matches the request over a scalable one, and it does so regardless of search-path priority. The
inventory command below therefore checks file presence only.

Checking against real GTK (prepend the four directories in the `MainWindow` order, then
`gtk_icon_theme_lookup_icon()` / `gtk_icon_info_get_filename()`) gives, for the Lucide theme:

| Requested size | Light ordering | Dark ordering |
| --- | --- | --- |
| 16 px (`GTK_ICON_SIZE_SMALL_TOOLBAR`) | 92/92 from `iconsLucide-light`, 0 system fallbacks | 92/92 from `iconsLucide-dark`, 0 system fallbacks |
| 24 px (`GTK_ICON_SIZE_LARGE_TOOLBAR`) | 91/92 from Lucide, 0 system fallbacks | 91/92 from Lucide, 0 system fallbacks |
| 32 px | 92/92 from Lucide, 0 system fallbacks | 92/92 from Lucide, 0 system fallbacks |

The single 24 px exception is `xopp-page-spinner`, served by the pre-existing
`ui/iconsColor-dark/hicolor/24x24/actions/xopp-page-spinner.svg`. That file is outside this
plan's scope; deleting it is a follow-up. It is not a system-theme fallback, and it does not
affect the Focus-target toolbar icons.

### Verify the inventory

Run from the repository root. The command reads the table above and checks it against the four
bundled sets. It is deterministic: two consecutive runs print identical output.

```sh
LC_ALL=C
awk '/^### Inventory/,/^The list was derived/' ui/README.md |
    awk -F'|' '/^[|] *`xopp-[a-z0-9-]+`/ {n=$2; gsub(/[ `]/,"",n); sub(/^xopp-/,"",n); print n}' |
    sort > /tmp/xoj-requested.txt
for t in Lucide-light Lucide-dark Color-light Color-dark; do
    find "ui/icons$t" -name 'xopp-*.svg' -printf '%f\n' 2>/dev/null |
        sed 's/^xopp-//; s/[.]svg$//' | sort -u > "/tmp/xoj-$t.txt"
done
while read -r n; do
    l=no; c=no
    [ -f "ui/iconsLucide-light/hicolor/scalable/actions/xopp-$n.svg" ] && l=yes
    [ -f "ui/iconsColor-light/hicolor/scalable/actions/xopp-$n.svg" ] && c=yes
    printf '%-28s lucide=%-3s color=%-3s %s\n' "$n" "$l" "$c" "$(if [ "$l" = no ] && [ "$c" = no ]; then echo SYSTEM-FALLBACK; else echo ok; fi)"
done < /tmp/xoj-requested.txt
```

Names that no bundled set supplies are printed as `SYSTEM-FALLBACK`; the current inventory has
none. To list only the Lucide gaps (must be empty):

```sh
for n in $(cat /tmp/xoj-requested.txt); do
    [ -f "ui/iconsLucide-light/hicolor/scalable/actions/xopp-$n.svg" ] || echo "$n"
done
```

The derivation is line-oriented `grep`/`sed` in the repository's existing style; multi-line
`emplaceCustomItem*()` calls and names built from variables are therefore listed here by hand.
No extra scripting dependency is introduced.

## Semantic CSS classes

`ui/xournalpp.css` starts with a shared visual language. GTK3 CSS has no custom properties, so
these are classes that widgets opt into with `gtk_widget_add_css_class()`, plus a couple of
selector lists that must agree on one literal value (GTK3 cannot alias values, so grouping them
into a single rule is the only way to keep them in sync).

| Class | Purpose | Applied by |
| --- | --- | --- |
| `.xoj-surface` | Base themed surface (`@theme_bg_color` / `@theme_fg_color`) | available for new widgets |
| `.xoj-surface-elevated` | Surface with a theme-derived drop shadow | available for new widgets |
| `.xoj-control-compact` | 32 x 32 logical px minimum | available for new widgets |
| `.xoj-control` | 36 x 36 logical px minimum, the standard toolbar density | `ToolButton::createItem()` |
| `.xoj-control-touch` | 44 x 44 logical px minimum | available for new widgets |
| `.xoj-tool-active` | Selected-tool state: filled background, border, and an inset underline indicator | grouped with the toolbar / floating-toolbox `:checked` selectors |
| `.xoj-status-neutral` | Neutral status text and border | available for new widgets |
| `.xoj-status-success` | Success status (`@success_color`) | available for new widgets |
| `.xoj-status-warning` | Warning status (`@warning_color`) | available for new widgets |
| `.xoj-status-error` | Error status (`@error_color`) | available for new widgets |
| `.xoj-focus-ring` | Visible keyboard focus ring (2 px outline, 2 px offset) | `ToolButton::createItem()` |
| `.xoj-tool-property-popover` | Padding of a tool's property popover (keeps `.toolbar` for the surface) | `ToolPropertyPopoverFactory::createPopover()` |
| `.xoj-tool-properties` | The property panel container and its minimum width | `ToolPropertyPanel::createWidget()` |
| `.xoj-property-header` | Header row of the property panel | `ToolPropertyPanel::buildHeader()` |
| `.xoj-property-title` | Tool name in the property panel header | `ToolPropertyPanel::buildHeader()` |
| `.xoj-section-heading` | Section heading inside a property panel | `xoj::toolbar::makeSectionHeading()` |
| `.xoj-property-label` | Label above a control in a property panel | `xoj::toolbar::makeLabelledRow()` |
| `.xoj-stroke-preview` | A stroke sample drawn by `xoj::toolbar::makeStrokePreview()` | the same helper |
| `.xoj-color-swatch` | The current-colour disc of a property panel | `ToolPropertyPanel::buildColorRow()` |
| `.xoj-preset-row` | One row of the preset list | `ToolPropertyPanel::createPresetRow()` |
| `.xoj-preset-strip` | The favourite preset strip in the toolbar | `PresetFavoritesItem::createItem()` |
| `.xoj-preset-strip-empty` | The strip's "no favourite presets" note | the same |
| `.xoj-preset-button` | A favourite preset button (adds nothing today; the hook exists so a selected-preset state can be added without a new selector) | `PresetFavoritesItem::rebuildStrip()` |
| `.xoj-tool-summary` | The active tool summary in the toolbar | `ActiveToolSummaryItem::createItem()` |
| `.xoj-status-line` | Result of the last preset action, also `ATK_ROLE_STATUSBAR` | `ToolPropertyPanel::buildPresetSection()` |

Review rules for changes to this file:

- A selected control must differ from an unselected one by at least two cues - a background or
  border plus a shape or indicator - never by colour alone.
- New state colours belong in the first section of `ui/xournalpp.css`, expressed in terms of
  `@theme_*` / `@success_color` / `@warning_color` / `@error_color` so they follow the active
  GTK theme, and must keep their contrast in both light and dark variants.
- Every icon-only control provides an accessible name, a tooltip, a focus treatment, and either
  a Lucide asset or a fallback that is deliberate and documented above.

Toolbar buttons are part of the keyboard focus chain: `ToolButton::createItem()` no longer calls
`gtk_widget_set_can_focus(btn, FALSE)`, and adds `.xoj-control` plus `.xoj-focus-ring`, an
accessible name derived from the tool's display name, and a tooltip. The menu-button item stays
non-focusable so that Tab does not stop twice on one control.

Pointer activation deliberately leaves the focus where it is: `gtk_widget_set_focus_on_click(btn,
FALSE)` keeps a mouse click on a tool from pulling the focus out of the canvas, without changing
`can-focus`, so Tab plus Space/Enter still reach and activate the same button.

Dark-mode selected state: the forced-dark floating-toolbox surface rules carry two type selectors,
so the grouped `.xoj-tool-active` rule lists those selectors too. Keep the two in step when either
rule changes - dropping the dark selectors silently reduces the selected tool to one cue.

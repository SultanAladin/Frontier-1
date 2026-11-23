# SceneDirectory — Outliner Validation Surface

A standalone Win32 + Direct3D 11 window that reproduces the outliner (`Outliner.html`) against the
real Interface theme. It owns a small in-memory scene tree of `RecordEntry` items and draws the whole
panel — header, search, filter chips, animated tree, and menus — entirely through `ImDrawList`, so the
rows / twisties / tinted icons / dropdowns match the mockup one-to-one.

It is fully decoupled from the modelling / paint / bake applications: it links only `EngineContext.lib`
(for the shared theme) plus the vendored ImGui core + Win32/DX11 backends + system D3D11. Its data types
live in the app-local namespace `SceneDirectoryValidation`, so its `RecordEntry` / `RecordToken` /
`RecordClassification` never collide with the pillar's `Frontier::RecordEntry`.

---

## Build & Run

Run the build through the **PowerShell** tool, never Bash (the `.bat` shells out to `cmd`-style parsing):

```powershell
& "C:\Users\OS\Documents\Projects\Frontier\Executables\Validation\SceneDirectory\Build.bat"
```

The output lands at `Binaries\Validation\SceneDirectory.exe`. If the link fails with
`LNK1168: cannot open … for writing`, a previous `SceneDirectory.exe` is still running — close it (or
`Get-Process SceneDirectory | Stop-Process -Force`) and rebuild.

Launch by running the `.exe` directly. The window opens at 520 × 1000 and can be resized.

---

## Layout

| Region | Contents |
|---|---|
| **Header** | `[stacked icon]  SCENE`, with a `+` (add object) and a filter button right-aligned. |
| **Search** | Live name-substring filter (case-insensitive). |
| **Filters** | The `FILTERS` label, active filter chips, and a `+ Filters` button that opens the filter dropdown. |
| **Tree** | The scrollable scene tree — rows with a twisty, tinted classification icon, label, and a visibility eye. |
| **Footer** | A `Live` dot and the object count (or `N selected` when a multi-selection is active). |

---

## Interactions

### Selecting
| Action | Result |
|---|---|
| **Click** a row | Select it (replaces the selection). |
| **Ctrl + Click** | Toggle that row in/out of the selection. |
| **Shift + Click** | Select the range from the anchor to the clicked row. |
| **Click empty tree space** | Clear the selection. |
| **Ctrl + A** | Select every currently-visible row. |
| **Esc** | Clear the selection. |

### Renaming
- **Double-click** a row's label, or press **F2** on the selected row, to open the inline editor.
- **Enter** commits; **Esc** cancels; clicking away also commits.
- New objects auto-open the rename editor so you can name them immediately.

### Folding (animated)
- **Click a twisty** to expand / collapse a container. The chevron rotates ▶→▼ in sync with the reveal.
- The subtree glides open and compacts closed — rows below slide to follow, with a soft accordion feel
  and no end-of-collapse snap.

### Visibility
- **Click the eye** on the right of a row to hide / show it. Hiding a container cascades to its subtree.
  Hidden rows dim and show a struck-through eye.

### Reordering / grouping
- **Drag a row onto a container** to reparent it there. If the row is part of a multi-selection, the
  whole selection moves. Dropping into a **collapsed** folder leaves it collapsed.

### Scrolling
- The **mouse wheel** scrolls the tree with an eased "lag" (the content chases the wheel rather than
  snapping) and a rubber-band overscroll that springs back when you flick past the top or bottom.

---

## Menus

All three menus use the shared `Controls/Dropdown` look — a sharp grey hover fill with a full-height
blue accent bar, an eased 14→20 px text indent on hover (the micro-animation), and icon-plus-text rows.

### `+` / Add-object menu
Sections **Geometry / Lights / Scene**, each listing creatable items:

| Item | Creates |
|---|---|
| Mesh | A `PolygonSurface` leaf |
| Folder | An `EnclosureFolder` container |
| Sun Light / Area Light | A `LightEmitter` leaf |
| Camera | A `CameraLens` leaf |
| Environment | An `EnvironmentDome` leaf |

New items land inside the anchor container (if the menu was opened over a folder) or at the Scene root,
and are named by **stem**: the first instance is the bare stem (`Folder`, `Light`, `Mesh`, `Camera`,
`Environment`); duplicates get a zero-padded suffix (`Folder_01`, `Folder_02`, `Light_01`, …).

### Filter menu (`+ Filters`)
- **TYPES** — a chip per classification (glyph + tick when active).
- **COLOURS** — a chip per icon tint (colour swatch + tick when active).
- **Only visible** — keep only rows whose visibility is on.

A row survives the filter if it matches **every** active chip of each facet; while filtering, the whole
tree is forced open (no fold animation) so all matches are visible. Clear a chip from the filters row, or
toggle it off in the menu.

### Row context menu (right-click)
`Add Object` · `Rename` · `Duplicate` · `Toggle Visibility` · `Group into Folder` · `Delete`.
`Group into Folder` wraps the selection in a new, open `Folder`. `Delete` is shown in danger-red.

---

## Keyboard summary

| Key | Action |
|---|---|
| **F2** | Rename the selected row |
| **Delete** | Delete the selection |
| **Ctrl + A** | Select all visible rows |
| **Esc** | Cancel a rename, or clear the selection |
| **Enter** | Commit a rename |

(Keyboard actions are suppressed while the rename editor or the search box owns text input.)

---

## Source map

| File | Role |
|---|---|
| `SceneDirectoryPanel.h` | Public types (`RecordEntry`, `RecordClassification`, `FilterChip`, `SceneDirectoryState`) + the two entry points. |
| `SceneDirectoryPanel.cpp` | The whole panel: sample content, traversal, filters, glyphs, selection, mutations, animated folding, smooth scrolling, and all three menus. |
| `SceneDirectoryHost.cpp` | The Win32 + D3D11 host — resolves the theme, mirrors it into ImGui's style, and draws `ConstructSceneDirectoryPanel` each frame. |
| `Build.bat` | Builds `SceneDirectory.exe`. Run via PowerShell. |

The public surface is two functions in namespace `SceneDirectoryValidation`:

```cpp
void InitializeSceneDirectorySample(SceneDirectoryState& State);                                   // build the demo tree once
void ConstructSceneDirectoryPanel(const Frontier::ThemeConfiguration& Theme, SceneDirectoryState&); // draw one frame
```

State is caller-owned (one `SceneDirectoryState` lives in `main()` for the window's lifetime); the panel
reads and writes only through it, so the drawing stays stateless.

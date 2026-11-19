# 🧩 `Engine/Workspaces/Fluid/` — Fluid & Pyro Simulation Workspace (`FrontierFluid`)

> **TL;DR**: `FluidWorkspace` couples the GPU `.slang` MacCormack + vorticity + combustion Eulerian solver (`Engine/SimulationSpace/FluidSpace/`) with a studio-styled Outliner, 3D volumetric raymarched Viewport + Timeline, Preset Library (`20+` scenes), `FluidAtlasMinimap`, 3-tab Inspector, and PNG + TOML Flipbook exporter.

---

## 🧩 1. Module Architecture

| File | Role |
| :--- | :--- |
| `FluidWorkspaceSession.{h,cpp}` | Simulation lifecycle, active presets, burst patterns, playback speed, camera presets, dirty tracking |
| `FluidWorkspaceHost.{h,cpp}` | Dock host (`Outliner`, `Viewport`, `Presets`, `Inspector`), `Bake flipbook` modal, status bar |
| `FluidOutlinerPanel.{h,cpp}` | `Enabled`/`Disabled` census tiles, `Search scene...`, category chips (`All`/`Gas`/`Geometry`/`Lights`), 7 scene items + embedded `Preset library` (`All`/`Fire`/`Smoke`/`Blast`) |
| `FluidViewportHost.{h,cpp}` | Camera preset pill, debug channel pill (`9` channels), tool rail (`Orbit`/`PyroBrush`/`DetonateBlast`/`CrossSection`), transport bar + 30s simulation ruler + `Trigger D` burst |
| `FluidInspectorPanel.{h,cpp}` | Active entity header card + toggle, 3-tab pill (`Source`/`Simulation`/`Rendering`), parameter cards & sliders |
| `FluidPresetBrowser.{h,cpp}` | Dedicated dockable gallery of fluid/pyro presets |
| `FluidAtlasMinimap.{h,cpp}` | Optional 3D grid / sparse-brick atlas diagnostic minimap |
| `FluidFlipbookExporter.{h,cpp}` | Exports `FluidFlipbookAtlas.png` spritesheet + `FluidFlipbookAtlas.toml` frame UVs & timing |

---

## 🏷️ 2. UI Controls

| Pane | Control Group | Options / Actions |
| :--- | :--- | :--- |
| **`FluidViewportHost`** | **Top Bar Left** | `Scene view` · Camera preset cycler (`Perspective`, `Front`, `Side`, `Top`) · `Presets` · `Open` · `Save` · `Bake` (opens Flipbook modal) |
| **`FluidViewportHost`** | **Top Bar Right** | `Diag` popover (toggles `Debug field` & `Atlas minimap`) · Channel cycler (`Lit volume`, `Voxel DDA`, `Smoke density`, `Temperature`, `Fuel`, `Velocity`, `Vorticity`, `Pressure`, `Cross-section`) · `Bounds` · `Focus` |
| **`FluidViewportHost`** | **Tool Rail** | `OrbitCamera` · `PyroBrush` · `DetonateBlast` · `CrossSection` |
| **`FluidViewportHost`** | **Transport & Ruler** | Rewind `|<` · Play/Pause · Step `>|` · Timecode · Speed (`0.25x`–`2x`) · Burst Pattern cycler (`Single burst`, `Shrapnel`, `Cluster salvo`, `Colliding waves`, `Mushroom cloud`) · `Trigger D` · `0–30s` Simulation Ruler |
| **`FluidOutlinerPanel`** | **Scene Collection** | `Gas domain` · `Fire emitter` · `Directional light` · `Scene collider` · `ForceFields wind` · `World drift grid` · `Embers & GI`, plus `All`/`Fire`/`Smoke`/`Blast` preset list |
| **`FluidInspectorPanel`** | **3 Tabs** | **`Source`** (Emission, Fuel & temperature), **`Simulation`** (Domain, Voxel grid & Forces), **`Rendering`** (Volume shading, Blackbody palette, Directional light & GI) |
| **Flipbook Modal** | **`Bake flipbook`** | `Frames` (`4×4` / `8×8`), `Tile resolution` (`128` / `256`), `FPS` (`24`), `Warm-up`, `Transparent background`, `Start with a burst`, `Bake frames` |

---

## 📝 3. Hotkeys & Mouse Controls

| Input | Action | Notes |
| :--- | :--- | :--- |
| `LMB` Drag | Orbit 3D volume camera (when `OrbitCamera` active), paint density/fuel (`PyroBrush`), or trigger explosion (`DetonateBlast`) | Tool-dependent |
| `Mouse Wheel` | Zoom / dolly camera | — |
| `Trigger D` button | Fire active burst pattern immediately (`Session.TriggerBlastNow()`) | Labelled `D` on transport bar |

*(No dedicated `ImGuiKey_*` global keyboard handlers are wired in `FluidViewportHost` yet — transport, presets, channels, and tools operate via UI buttons and pointer gestures.)*

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `16` files under `Engine/Workspaces/Fluid/`. **Zero C++ edits were made.**

| Category | Count | Offending Examples (File & Symbol) | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **Prohibited `<K-suffix>`** | **0** | None detected in `Engine/Workspaces/Fluid/` | 🟢 |
| **`Card` / `Pill` / `Badge` / `Glyph`** | 268 | `Card` (`167`), `Pill` (`80`), `Glyph` (`13`), `Badge` (`8`) in `FluidInspectorPanel.cpp`, `FluidOutlinerPanel.cpp` | `SectionBlock`, `CapsuleButton`, `Icon`/`Symbol`, `Indicator` |
| **`Node` / `Draw*` / `Frame` / `Palette`** | 154 | `Node` (`62`, `FluidOutlinerNode`), `Draw*` (`44`, `DrawNodeIcon`), `Frame` (`28`), `Palette` (`20`) | `Entry`/`Slot`, `Construct*`, `Step`/`Extent`, `ColorCodex` |
| **`Emitter` / `Get*`/`Set*` / `Is*/Has*` / `Map` / `Value` / `Flag` / `Base` / `Data` / `Context` / `State` / `Item`** | 65 | `Emitter` (`17`), `Get/Set` (`11`), `Is/Has` (`10`), `Map` (`7`), `Value` (`5`), `Flag` (`4`), `Base` (`4`), `Data`/`Context`/`State` (`2` each), `Item` (`1`) | `Source`, `Query*`/`Assign*`, direct booleans, `Register`, `ScalarReading`, `DirectiveMask`, `Primary`, `Payload`, `Session`, concrete enum, `Entry` |

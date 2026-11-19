# 🧩 `Engine/Workspaces/Particle/` — GPU Particle & VFX Studio (`FrontierParticle`)

> **TL;DR**: `ParticleWorkspace` provides GPU-coupled particle, ribbon, lightning, and flock authoring (`93+` / `102` presets across `9` categories) with live `ForceFields` wind/zone visualization, a 5-tool viewport rail, `SimulationPlaybackControls`, `SimulationTimeRuler`, 4-tab Inspector, and `.ParticleSpace` TOML manifest export.

---

## 🧩 1. Module Architecture

| File | Role |
| :--- | :--- |
| `ParticleWorkspaceSession.{h,cpp}` | Multi-tab scene state, `Outliner` filters, tool category, overlay channels, preset browser state & log stream |
| `ParticleWorkspaceHost.{h,cpp}` | 56px header (`Workspace | Assets | Log` capsule, `Export TOML`, `?` help), 3 floating panels, modal `Assets` preset browser, status bar |
| `ParticleOutlinerPanel.{h,cpp}` | `Visible`/`Hidden` census tiles, `ENVIRONMENT` (`Force Field`, `Ambient Wind`, wind & damp zones) + `PARTICLE EFFECTS` (`SILK & CURL`, filter chips) |
| `ParticleViewportHost.{h,cpp}` | Topbar (`Throw Ball · T`, `Field · V`, projection/overlay dropdowns, arrows/minimap/log/max), 5-tool rail, `SimulationPlaybackControls` + `SimulationTimeRuler` |
| `ParticleInspectorPanel.{h,cpp}` | 4 tabs (`Overview`, `Source`, `Motion & FX`, `Metrics & Codec`) + Force Field source inspector |
| `ParticleExchange.{h,cpp}` | Simulation step & GPU/CPU particle metrics exchange |
| `ParticlePaneAppearance.h` | Shared dark studio palette & panel helpers |

---

## 🏷️ 2. UI Controls

| Panel / Region | Controls & Sections | Function |
| :--- | :--- | :--- |
| **Header (`ParticleWorkspaceHost`)** | Scene tabs · `Workspace | Assets | Log` capsule · `?` Help · `Export TOML` | Switch scenes, open 93-preset `Assets` modal browser (9 category tabs), toggle live log, export `.ParticleSpace` TOML |
| **`ParticleOutlinerPanel`** | `Visible` / `Hidden` census · `ENVIRONMENT` (`Force Field`, `Ambient Wind`, `WIND & AIRFLOW SOURCES`, `ACCELERATE & DAMP ZONES`) · `PARTICLE EFFECTS` | Toggle visibility and select emitters or spatial force sources |
| **`ParticleViewportHost` Topbar** | Projection dropdown (`Perspective orbit`, `Top lattice`, `Front axis`, `Side elevation`) · Field Overlay (`Clean viewport`, `Field · Drift`, `Field · Accelerate`, `Field · Damp`) · `Throw Ball · T` · `Field · V` · Arrows / Minimap / Log / Maximize | Inspect camera/field overlays, launch physics test ball, toggle overlays |
| **`ParticleViewportHost` Rail & Timeline** | 5-tool rail (`Orbit`, `Move`, `Throw Ball`, etc.) · `SimulationPlaybackControls` · `SimulationTimeRuler` | Author source transforms (`G` handles, `Shift` Y-drag), scrub & play/pause simulation |
| **`ParticleInspectorPanel` (4 Tabs)** | **`Overview`** · **`Source`** · **`Motion & FX`** · **`Metrics & Codec`** | Edit emitter, colour gradient (`ColourSpace::MultiStopGradient`), motion/vortex/curl, ForceFields coupling, and `.ParticleSpace` metrics |

---

## 📝 3. Hotkeys & Mouse Controls

| Input | Action | Notes |
| :--- | :--- | :--- |
| `T` | Toggle `Throw Ball` interaction tool | Labelled `Throw Ball · T` in topbar |
| `V` | Toggle `Field` vector-field overlay | Labelled `Field · V` in topbar |
| `G` | Show/manipulate 3D transform handles | Tool rail (`Shift` constrains Y-drag) |
| `Space` | Play / Pause particle simulation | Transport controls |
| `LMB` Drag | Orbit camera (or throw ball / drag transform gizmo when active) | — |
| `Mouse Wheel` | Zoom / dolly camera | — |

---

## 🔍 4. C++ Naming Violations Audit (Read-Only)

⚠️ Scanned `13` files under `Engine/Workspaces/Particle/`. **Zero C++ edits were made.**

| Category | Count | Offending Examples (File & Symbol) | Compliant Replacement |
| :--- | :---: | :--- | :--- |
| **Prohibited `<K-suffix>`** | 18 | `ParticleWorkspaceSession.{h,cpp}`, `ParticleOutlinerPanel.cpp`, `ParticleViewportHost.cpp`, `ParticleInspectorPanel.cpp` (`Selection<K-suffix>`) | `SelectionCategory` / `SelectionTarget` |
| **`Card` / `Pill` / `Glyph` / `Badge`** | 335 | `Card` (`169`), `Pill` (`113`), `Glyph` (`30`), `Badge` (`23`) | `SectionBlock`, `CapsuleButton`, `Icon`/`Symbol`, `Indicator` |
| **`Record` / `Info` / `Draw*` / `Is*/Has*` / `Map` / `Make*`** | 106 | `Record` (`46`), `Info` (`23`), `Draw*` (`20`), `Is/Has` (`9`), `Map` (`6`), `Make*` (`2`) | `RecordEntry`, `Metrics`, `Construct*`, direct booleans, `Register`, `Construct*` |

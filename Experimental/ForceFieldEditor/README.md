# 🧩 `Experimental/ForceFieldEditor/` — ForceField Editor HTML Reference Studio

> **TL;DR**: Clean ground-up redo of the ForceField editor as a self-contained HTML/JS/CSS reference studio in the
> Project-Zero dock chrome. Author force generators from a 19-law repository, inspect every law parameter, and read the
> composite field through heatmap slices, lattice arrows, animated streamlines, advected particles, envelope shells, and
> a live field meter. **+Z up · zero rigid-body physics · browser-local, no engine connection.**

This tree replaces the retired `ForceFieldEditor` studio removed in `daed305` (CLAUDE.md §1.2: Fluid / ForceField /
Particle queued for un-simplified redo) and is the exact-1:1 UI + algorithmic specification for the future
`Engine/SimulationSpace/ForceFields/` C++20 + `.slang` port (CLAUDE.md §2.1 gates apply to that port).

---

## 1. Files

| File | Scope |
| :--- | :--- |
| `index.html` | Studio shell; loads the five script units in dependency order (plain classic scripts, works from `file://`). |
| `Studio.css` | Project-Zero chrome translation: OLED strip, docks, registry rows, Construct drawer, HUD, inspector rows, footer, toast. |
| `ForceLawRepository.js` | 19 canonical force laws across `VectorField` / `SlipstreamField` / `TurbulenceField` domains, value resolution, 12 curated engine presets, default wind-tunnel diagnostic rig, per-law icon paths. |
| `FieldMath.js` | Composite field evaluation per law, envelope/attenuation/duration gating, motion paths, curl-noise potential, finite-difference quantities (Speed / Acceleration / Curl / Divergence / Pressure), colour ramps, Beaufort + compass, bidirectional slice streamline tracing. |
| `Viewport.js` | Canvas-2D world viewport: Top XY / Front XZ / Side YZ planes with movable slice depth, adaptive grid, heatmap slices, envelope outlines, lattice arrows, animated streamlines, advected particle transport with trails and depth fade, generator markers, cursor crosshair. |
| `Inspector.js` | Right-dock inspector: identity / law / placement / law parameters / envelope & timing / motion path / actions, plus the global Overlay tab. |
| `Studio.js` | Shell boot, generator life-cycle (construct / duplicate / remove), registry + drawer, HUD + playback, field meter, pointer & keyboard interaction, adaptive quality, persistence (autosave, export/import rig JSON), main loop. |

## 2. Editor Capabilities

- **Construct force generators**: drawer lists all 19 laws grouped by domain plus 12 curated presets; each construct
  spawns at the view centre with law defaults. Registry rows support enable / mute / remove; click to select.
- **Per-law parameters**: Strength, Reach Radius, Inner Radius, Swirl Ratio, Falloff Exponent, Turbulence Scale,
  Noise Octaves, Pulse Frequency, Azimuth / Elevation, Reach envelope (Sphere / Column / Box / Infinite), Attenuation
  curve (SmoothStep / Linear / InverseSquare / Constant), Duration mode (Continuous / Modulated / Impulse), motion
  patterns (Static / LinearAB / SplineCurve / CircularOrbit / Figure8) with loop modes and waypoint shaping.
- **Force + flow visualization**: composite-field heatmap slice (quantity + colour ramp + fixed maximum), lattice
  arrows, bidirectional RK2 streamlines with animated drift, up to 9000 advected particles with trails and slice-depth
  fade, envelope shells with selection outline, live field meter (force vector, |F| m/s, Beaufort scale, curl,
  divergence, compass bearing).
- **Playback**: play/pause (Space), reset clock & particles, time scale 0–3×, adaptive slice density with auto
  quality (drops heatmap resolution / particle count under load, restores when headroom returns).
- **Persistence**: debounced autosave to `localStorage`, `Export Rig…` / `Import Rig…` JSON documents, Reset Scene.

## 3. Keyboard

| Key | Effect |
| :-- | :--- |
| `Space` | Play / pause simulation |
| `1` / `2` / `3` | Top XY / Front XZ / Side YZ slice plane |
| `C` | Open Construct drawer |
| `F` | Focus selected generator |
| `Delete` / `Backspace` | Remove selected generator |
| `Escape` | Close drawer |

## 4. Naming Compliance

Law identifiers follow `Documentation/BannedWordsVocabulary.md`: the retired trees' `FlowStream` → **`SlipstreamJet`**
and display name "Vortex Column" → **"Cyclone Column"** (Rankine law); the domain `FlowField` → **`SlipstreamField`**;
`Catalog` → `Repository`; `Probe` → `Field Meter`; `Palette` → `Colour Ramp`; `Topology` → `Motion Pattern`;
`On*/Is*/Has*` booleans → direct booleans. Studio identifiers avoid all banned vocabulary.

## 5. Status

| Status | Scope |
| :--- | :--- |
| 🟢 | HTML reference studio complete: 19 laws, composite evaluation, full visualization stack, inspector, drawer, persistence. |
| 🟢 | Interaction & performance pass: device-pixel/CSS-pixel coordinate parity (overlays, meter, picking and dragging now correct on scaled displays), cursor-anchored zoom, `touch-action: none`, heatmap refresh cadence decoupled from the frame clock (90 ms wall-clock for time-varying scenes), streamlines traced from a seed queue (14 lines/frame) and rendered via 8 colour-bucket batched strokes, particle transport forces cached at 40 Hz with all particle geometry batched through `Path2D` buckets (~35 canvas submissions/frame), `EnsureParticleCapacity` wired into the render tick (particles previously never allocated), live particle telemetry. |
| 🟢 | Validation: `node --check` on all units, 48-check jsdom smoke suite (DOM wiring, math sanity, life-cycle, persistence, vocabulary gates), render-pipeline suite at `devicePixelRatio = 2` (dpr coordinate parity, marker pick + drag delta, cursor-anchored zoom stability, 90 frames through heatmap/arrows/streamlines/particles/meter with submission budget). |
| 🔴 | Real-browser screenshot sheet (CLAUDE.md §2.1 Step 0) pending — no Chromium available in the authoring sandbox; capture on first Windows/native run. |
| 🔴 | C++20 / `.slang` port to `Engine/SimulationSpace/ForceFields/` not started; must reproduce this studio exactly (§2.1: screenshot-first, `Engine/Components/` UI parity, zero simplifications, side-by-side `.png` proof). |
| 🔴 | **Particle Fluid editor** redo queued next in this series (same Project-Zero chrome, same validation protocol). |

## 6. Run

Open `index.html` directly, or serve the folder (`python3 -m http.server 8123`) and browse to it. No build step,
no external dependencies; DM Sans loads from Google Fonts with a system-font fallback when offline.

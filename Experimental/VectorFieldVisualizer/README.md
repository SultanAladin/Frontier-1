# Frontier VectorFieldVisualizer (Project Zero GPU Studio)

Interactive **WebGL2 GPU + Software 2D/Iso** spatial vector-field workbench uniting **19 built-in physics force laws** (`GpuId 0..18`), **zoom-adaptive full-viewport field baking**, **bidirectional RK4 streamlines with directional chevrons**, and a **live symbolic vector-calculus formula compiler** (`VectorFieldLang`) that emits both JavaScript and GLSL ES 3.00 shaders at runtime.

---

## 1. Quick Start

```bash
# Serve the VectorFieldVisualizer locally on port 8094
python3 Experimental/VectorFieldVisualizer/Serve.py 8094

# Run the full 12-suite verification harness (from repo root)
node Experimental/VectorFieldVisualizer/VectorFieldValidation.mjs
```

All fonts, Three.js ESM modules, shaders, and UI scripts are bundled locally (`Fonts/`, `Vendor/`, `Engine/`) with zero runtime CDN requests.

---

## 2. Viewport Navigation & Controls

| Action | 3D Perspective / Isometric | 2D Ortho Slice / Split Right Pane |
| :--- | :--- | :--- |
| **Orbit** | Left-drag | — |
| **Pan** | Right-drag | Right-drag or Middle-drag |
| **Zoom Field** | Mouse wheel, `+` / `-`, or `#ZoomHud` | Mouse wheel, `+` / `-`, or `#ZoomHud` |
| **Fit / Reset** | `0` hotkey, **Fit** button, or **Reset Camera** | `0` hotkey or **Fit** button |
| **Move Probe** | `Shift + Left-click / drag` | `Left-click / drag` |
| **Select / Move Source** | `Left-click / drag` octagon beacon (WebGL2) | Outliner or Inspector coordinates |
| **Construct Source** | `+ Construct` button or `Ctrl+A` | `+ Construct` button or `Ctrl+A` |

### View Modes & Slice Planes
- **Projection Selector**: switch seamlessly between `3D Perspective`, `Split 3D | 2D`, and `2D Slice Ortho`.
- **Slice Axes**: independently toggle horizontal `XZ`, front `XY`, side `YZ`, or `All 3` orthogonal planes. In **3D** mode, heatmaps, arrows, bounds, and streamlines strictly follow the selected plane(s), unlocking full volumetric 3D seeding when all 3 axes are active.
- **Viewport Layers**: independently toggle **Heatmap**, **Vectors**, **Streams** (bidirectional RK4 ribbons + directional chevrons), and **Bounds** (source reach envelopes & wave rings).
- **Zoom HUD (`#ZoomHud`)**: live percentage readout (`30% – 850%`) with `−`, `+`, and `Fit` controls. Zooming dynamically adjusts the `128×128` bake window, streamline spacing, and vector arrow grid across the full edge-to-edge viewport in both WebGL2 and Software 2D/Iso modes.

---

## 3. Built-In 19 Force Models & Proving-Ground Tabs

Top scenario tabs provide five curated presets:
1. **Wind & Vortex Lab** (`lab`) — prevailing wind, mesoscale vortex, radial thermal, and directional drag zone.
2. **All 19 ForceModels** (`all19`) — full showcase grid containing every GPU force law (`GpuId 0..18`) simultaneously.
3. **Maritime Wake** (`maritime`) — ocean swell, Kelvin wave wake, coastal jet, and wind-drag corridor.
4. **Gusts & Thermals** (`turbulence`) — multi-scale incompressible `CurlNoise`, buoyant plume, and pulsing `RadialBurst` wave.
5. **Magnetic & Dipole** (`electromagnetic`) — point/dipole attractors, Lorentz swirl, and nested containment shell.

Use the left **Outliner** to search, filter by Runtime System or Active/Muted state, solo (`S`) a single force source, mute (`M`), focus (`Focus` in Notch), or reorder sources.

---

## 4. Symbolic Vector Formula Compiler (`Formula` Tab)

Click **`+ Formula`** in the top toolbar or select any source and open the **`Formula`** inspector tab to author custom 3D vector fields in mathematical notation.

### Language Syntax & Built-In Symbols
Every equation assigns a 3D vector expression to either **`a = ...`** (acceleration channel) or **`v = ...`** (velocity channel):

```text
a = -S · A(r) · r̂ + ω_swirl · (ŷ × r̂)
```

| Token / Helper | Meaning |
| :--- | :--- |
| `r̂`, `x̂`, `ŷ`, `ẑ`, `θ̂` | Unit radial (`normalize(p)`), Cartesian basis axes, and azimuthal swirl (`ŷ × r̂`) vectors |
| `p`, `r`, `R` | Local offset vector `p = pos - centre` (m), radial distance `r = \|p\|` (m), and source reach `R` (m) |
| `A(r)` | Normalized attenuation envelope (`Linear`, `SmoothStep`, `Exponential`, `Gaussian`, `Constant`) |
| `S`, `ω_swirl`, `t` | Per-source **Strength** slider, **Swirl** slider, and simulation clock time `t` (seconds) |
| `·`, `×`, `*`, `+`, `-`, `/` | Scalar multiplication / dot product (`·`), 3D cross product (`×`), and vector arithmetic |
| `sin`, `cos`, `sqrt`, `exp`, `pow`, `clamp`, `mix`, `dot`, `cross`, `length`, `normalize` | Built-in math & vector functions |
| `curlNoise(p, scale)` | Divergence-free (`∇·F = 0`) 3D procedural curl turbulence helper |

- **Live Syntax Highlighting & Diagnostics**: tokenizes unit vectors, operators, parameters, and functions in real time with line/column static type checking (`scalar` vs `vec3`).
- **Dual Target Emission**: valid formulas immediately update both the CPU JS evaluator (for Probe telemetry, RK4 streamlines, and Software Fallback) and inject GLSL ES 3.00 kernels (`vfEvalCustomFormula0..3`) into `SLICE_BAKE_FRAG` with zero page reload.

---

## 5. Inspector Tabs & Probe Telemetry

- **Field Tab**: edit active source `Model`, `Attenuation`, `Duration` (`Permanent` / `Impulse`), `Reach` shape (`Sphere`, `Column`, `Box`), coordinates, `Strength`, `Swirl`, and inspect the live attenuation falloff curve.
- **Formula Tab**: interactive symbolic editor, presets (`Swirl + Pull`, `Magnetic Dipole`, `Helical Updraft`, `Curl Turbulence`, `Harmonic Repulsor`), symbol palette, and collapsible GLSL ES 3.00 kernel preview.
- **Layers Tab**: switch visualization `Quantity` (`Speed`, `Acceleration`, `Curl`, `Divergence`, `Pressure`), `Colormap` (`Engineering`, `Turbo`, `Plasma`, `Diverging`), `Slice Plane Offset`, `Fixed Scale Ceiling`, `Arrow Grid Spacing`, `Heatmap Opacity`, and `3D Arrow Layers`.
- **Cache Tab**: displays the 4-tier cascade clipmap memory budget (`16.0 MB` total) and live `[VectorFieldWorkspace]` TOML snapshot with one-click copy.
- **Wind Anemometer & Footer**: tracks the movable probe's `[X, Y, Z]` position, vector speed (`m/s`), Beaufort force grade (`0–12`), compass heading, divergence (`∇·F`), curl (`|∇×F|`), and dynamic pressure (`Pa`).

---

## 6. Architecture & Automated Verification

| File | Responsibility |
| :--- | :--- |
| `index.html`, `ProjectZero.css`, `VectorFieldVisualizer.js` | Project Zero workstation layout, Outliner, Notch, Construct modal, Inspector tabs, and live Anemometer |
| `Engine/VectorFieldCatalogue.js` | 19 `ForceModel` laws, `EForceDuration`, 5 proving-ground scenarios, RK4 bidirectional streamlines, GPU packing, and TOML encoder |
| `Engine/VectorFieldLang.js` | Symbolic vector calculus lexer, parser/type-checker, syntax highlighter, presets, and JS + GLSL ES 3.00 code generators |
| `Engine/VectorFieldShaders.js` | 4-stage GLSL ES 3.00 pipeline (`SLICE_BAKE`, `SLICE_DISPLAY`, `GPU_ARROWS`, `STREAMLINE`) |
| `Engine/VectorFieldGPUViewport.js` | Single-kernel `128×128` WebGL2 bake viewport (`Float16`/`RGBA8`), adaptive zoom windowing, and automatic Software Canvas 2D/Iso fallback |
| `Engine/InspectorTelemetry.js`, `Engine/ForceIcons.js` | Probe trace graph, Beaufort telemetry, SVG attenuation curve, and bespoke SVG icons |
| `VectorFieldValidation.mjs` | 12-suite Node ESM verification suite (physics, GLSL3 shaders, UI IDs, `VectorFieldLang`, full-viewport zoom coverage, streamlines) |

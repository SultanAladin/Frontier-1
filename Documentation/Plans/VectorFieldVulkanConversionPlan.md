# Frontier C++20 + Vulkan 1.3 Vector Field & `.VectorFieldSpace` Architecture Plan

> 🔴 **SUPERSEDED — REMOVED PENDING CLEAN 1:1 GROUND-UP REDO (`daed305`, `2026-11-20`):** `Engine/SimulationSpace/ForceFields/` and `ForceFieldWorkspace` were removed from `Engine/` pending an un-simplified 1:1 ground-up redo (`BKL-013`). Retained strictly as historical reference — see **[`CLAUDE.md` §1.2 & §2.1](../../CLAUDE.md)**.

**Target Repository / Branch:** `unassignedinbox/Frontier` (`arena/429d7dc7-frontier`)  
**Target Workspace UI Baseline:** `Exhibits/Images/WorkspacePanes/04-EntryInspectorSplitAndDiagnostics.png` (`OutlinerPanel` + `ViewportPanel` + `InspectorPanel` in `Engine/Components/Panes/`)  
**Reference WebGL2 Prototype:** `Experimental/VectorFieldVisualizer/` *(17/17 validation suites passing on `:8094`)*  
**Target Standards:** ISO C++20 (`cxx_std_20`) · Vulkan `1.3` (`DynamicRendering`, `Synchronization2`, `volk`, `.slang` -> SPIR-V) · Strict Right-Hand Coordinate Rule (`+X` Forward, `+Y` Left, `+Z` Up, Reverse-Z `[1.0 -> 0.0]`)  
**Status:** Authoritative Implementation Blueprint *(Zero C++ files authored yet)*

---

## 1. Executive Summary & Core Scope Boundaries

This plan defines the native **ISO C++20 + Vulkan 1.3 `.slang`** implementation of the Force Field runtime and Editor visualizer inside **`arena/429d7dc7-frontier`**, mounting directly into Frontier's existing **3-Dock Workspace (`OutlinerPanel` + `ViewportPanel` + `InspectorPanel`)** shown in `04-EntryInspectorSplitAndDiagnostics.png`.

### Non-Negotiable Architectural Rules

1. **Runtime Physics Backbone (`Editor` + `Development` + `Shipping`):**
   - All **19 Force Laws (`ForceModel`)**, **Temporal Duration Envelopes** (`EForceDuration`: `Continuous`, `Modulated`, `Impulse`), **Spatial Attenuations** (`ForceAttenuation`: `SmoothStep`, `Linear`, `InverseSquare`, `None`), **`FlowField` / `FlowStream` / `NavigationField` profiles**, **Curve / Line / Orbit Motion Paths** (`LineAB`, 3D Catmull-Rom `SplineCurve`, `Orbit` with `PingPong`/`Loop`/`Once`), **Formula Compiler (`FieldFormulaCompiler` / `VectorFieldLang`)**, and **Cascaded 3D GPU Velocity Lattices (`FieldLatticeCascade`)** compile into **all three build profiles** (`Editor`, `Development`, `Shipping`).
   - They act as the shared upstream physical prerequisite for **Fluids, Volumetric Clouds, Particles, Jolt Physics Bodies/Vehicles, and Dynamic Weather**.
2. **Debug Visualizer (`FRONTIER_PROFILE_EDITOR` Only — Never Shipped in Game):**
   - All visual diagnostics (instanced 3D directional arrows, volumetric RK4 streamlines, cross-section heatmaps, semi-transparent reach volumes, source circle markers, and 3D curve/waypoint splines) live under `Engine/Editor/` + `Engine/Shaders/VectorField/` and compile **only when `FRONTIER_EDITOR_ENABLED == 1`**.
   - **Renders Even in `Lit` Shading Mode:** When toggled ON in the **Viewport Header**, the overlay renders inside `ViewportPanel`'s Vulkan canvas right after `Lit` surface shading using **100% GPU `.slang` raster units** (never CPU lines, never ImGui `ImDrawList`).
3. **Jolt Physics v5.6.0 & Particle Decoupling:**
   - **Collision belongs strictly to Jolt Physics v5.6.0 (`Automation/JoltPhysics.cmake`).** `SimulationSpace::ForceFields` does not implement solid collision detection and does not wait for full particle/fluid bringup.
   - `ForceFieldAccumulator` exposes deterministic CPU SIMD queries (`PhysicsThread` -> `Jolt::BodyInterface::AddForce / AddTorque / AddImpulse`) alongside GPU `StructuredBuffer<ForceSourceGPURecord>` + `RWTexture3D` cascades so Jolt vehicles/ragdolls, particles, clouds, fluids, and weather plug in seamlessly when needed.
4. **Reuse Shared Workspace Panes & `TransformGizmo` (Zero Dedicated Outliner / Zero Custom Gizmo):**
   - Mount directly into the exact **`OutlinerPanel` + `ViewportPanel` + `InspectorPanel`** workspace (`04-EntryInspectorSplitAndDiagnostics.png`) across any editor that uses forces (`World`, `Simulation`, `Particle`, `Fluid`, `Volumetric`, `Vehicle`).
   - Reuse **`Frontier::Editor::TransformGizmo`** (`GizmoRasterization.slang`) and **`ViewportPickQuery`** (`PickIDs` `R32G32B32A32_UINT`) for both Force Centers and Motion Curve Waypoints (`A`, `P1`, …, `B`).
   - Wire **`ViewportPanel::RenderCanvasPane`** cleanly to Frontier's Vulkan offscreen raster texture (`ViewportProjectionPane`-style Vulkan texture sync) so the existing viewport renders live 3D/2D GPU content instead of staying an empty checkerboard.
   - **`Edit` / `Simulate` / `Play` (`PlayPaused`)** update cleanly via existing `ViewportExecutionMode`.
   - **Only UI Header Addition:** Adding the **`Force Viz` Toggle & Style Popover** in `ViewportPanel`'s header bar.

---

## 2. Audit of `arena/429d7dc7-frontier` Infrastructure (What Exists vs. What We Wire)

| Existing Engine Subsystem on `arena/429d7dc7-frontier` | Exact Repo Location | Concrete Role in VectorField Conversion |
| :--- | :--- | :--- |
| **3-Dock Studio Workspace (`04-EntryInspectorSplitAndDiagnostics.png`)** | `Engine/Components/Panes/OutlinerPanel.{h,cpp}`<br>`Engine/Components/Panes/ViewportPanel.{h,cpp}`<br>`Engine/Components/Panes/InspectorPanel.{h,cpp}`<br>`Engine/Components/Panes/WorkspaceAppearance.{h,cpp}` | Base UI host. Already provides Showcase folders, `+ Construct (Ctrl+A)`, `Edit / Simulate / Play-Pause`, Split Viewport (`Perspective \| Top`), Diagnostics F3 overlay, Settings sheet, 2×2 Capability Tiles (`Enabled`, `Cast shadows`, `Dynamic`, `Locked`), and `Transform` (`Position`, `Rotation`, `Scale`). |
| **Reusable GPU Pane Texture Pattern** | `Engine/Components/Panes/ViewportProjectionPane.{h,cpp}`<br>`Engine/GeometricRaster/UV/UVWorkspaceRasterizer.h` | Proven template showing how offscreen Vulkan framebuffer pixels / `ImTextureID` mount directly inside a PixelSpace canvas rect and dispatch interactive pointer rays (`ViewportInteractionReading`). |
| **Vulkan 1.3 Interchange & Timeline Sync** | `Engine/GeometricRaster/VulkanInterchange.{h,cpp}`<br>`Engine/GeometricRaster/Compute/ComputeDispatchEncoder.{h,cpp}`<br>`Engine/GeometricRaster/Compute/TimelineTransferRegister.{h,cpp}` | Uploads `ForceSourceGPURecord` + spline knot SSBOs + dispatches `FieldLatticeCompute.slang` with Timeline Semaphores (zero fences). |
| **Shared 3D/2D `TransformGizmo` & GPU Pick** | `Engine/Editor/Viewports/TransformGizmo.{h,cpp}`<br>`Engine/GeometricRaster/Shaders/GizmoRasterization.slang`<br>`Engine/GeometricRaster/Picking/ViewportPickQuery.{h,cpp}`<br>`Engine/GeometricRaster/Shaders/ViewportPickReduction.slang` | Provides 13-handle `Spatial3D` / 6-handle `Planar2D` manipulators (`TranslateX/Y/Z`, `RotateX/Y/Z`, `ScaleX/Y/Z`, `PlaneYZ/XZ/XY`, `CenterRing`) + `1×1` `PickIDs` picking for **Force Circles and Curve Waypoints (`A, P1, B`)**. |
| **Hybrid `.Space` Container Codec** | `Engine/Codecs/SpaceContainerCodec.{h,cpp}` | Existing TOML + 64B-aligned binary container framing used to implement **`VectorFieldCodec` (`.VectorFieldSpace`)** and optional **`.ForceModel`** single-force presets. |
| **Jolt Physics v5.6.0 & Build Profiles** | `Automation/JoltPhysics.cmake`<br>`Engine/Configuration/BuildConfiguration.h`<br>`Engine/Lifecycle/{EditorLifecycle,RuntimeLifecycle,WorkspaceLifecycle}.h` | `FRONTIER_EDITOR_ENABLED` cleanly excludes Editor overlays from `Development` & `Shipping`; `PhysicsThread` reads `ForceFieldAccumulator` on fixed steps alongside Jolt collision. |

---

## 3. Solving the Existing Viewport (`ViewportPanel::RenderCanvasPane` -> Live Vulkan 1.3)

Today in `arena/429d7dc7-frontier`, **`ViewportPanel::RenderCanvasPane`** (`Engine/Components/Panes/ViewportPanel.cpp`) renders a placeholder checkerboard grid (`"N constructed entries in workspace"`), whereas **`ViewportProjectionPane`** (`Engine/Components/Panes/ViewportProjectionPane.{h,cpp}`) already demonstrates how Frontier blits live offscreen GPU framebuffers into a `PixelSpace` pane via `PixelSpace::FillTexturedRectangle` / `ImTextureData`.

### How to Use the Existing `ViewportPanel` Without Breaking `04-EntryInspectorSplitAndDiagnostics.png`

Extend `ViewportPanel` with the exact same lightweight GPU Texture & Interaction hook pattern used by `ViewportProjectionPane`:

```cpp
// Non-intrusive extension on Frontier::ViewportPanel (Engine/Components/Panes/ViewportPanel.h)
struct ViewportPaneRenderContext
{
    uint32_t                 PaneIndex      = 0u;  // 0 = Primary (Perspective), 1 = Split Secondary (Top/Ortho)
    uint32_t                 WidthPx        = 0u;
    uint32_t                 HeightPx       = 0u;
    ViewportProjectionPreset Projection     = ViewportProjectionPreset::Perspective;
    ViewportExecutionMode    ExecutionMode  = ViewportExecutionMode::Edit;
    bool                     PlayPaused     = false;
    ViewportDisplaySettings  Display        {};    // QualityTier, GI, Reflections, AA, RT, DebugChannelIndex
    ForceOverlaySettings     ForceOverlay   {};    // Editor-only debug overlay flags & streamline controls
};

// Returns either a zero-copy GPU texture handle (Vulkan ImTextureID) OR RGBA8 pixels for headless Exhibits/ PNG verification
using ViewportPaneRasterCallback      = std::function<ViewportRasterSurface(const ViewportPaneRenderContext&)>;
using ViewportPaneInteractionCallback = std::function<void(uint32_t PaneIndex, Rectangle2D PaneBounds, const PaneInputReading& Input)>;
```

#### Exact Runtime Behavior Inside `ViewportPanel::RenderCanvasPane(...)`:
1. **Unbound (`RasterCallback == nullptr`):** Preserves the existing checkerboard + `"N constructed entries in workspace"` fallback — keeping `WorkspacePanes` visual regression exhibits (`04-EntryInspectorSplitAndDiagnostics.png`) **100% green**.
2. **Bound to Vulkan (`RasterCallback != nullptr`):**
   - For **Single View** OR **Split View (`PrimaryProjection` Left | `SecondaryProjection` Right)**:
     - Passes `PaneBounds` dimensions, camera projection preset, `ExecutionMode` (`Edit`/`Simulate`/`Play`), and `ForceOverlaySettings` to the Vulkan renderer (`VectorFieldOverlaySequence` + scene Lit renderer).
     - Vulkan renders `Lit` scene + *(if `FRONTIER_EDITOR_ENABLED` && `ForceOverlay.Enabled`)* GPU Force `.slang` overlays + `TransformGizmo` into offscreen Vulkan image(s) (plus MRT `PickIDs` for `ViewportPickQuery`).
     - `ViewportPanel::RenderCanvasPane` blits the rendered image cleanly across `PaneBounds`, then draws `ViewportPanel`'s crisp overlays on top:
       - Per-pane **`Perspective ▾` / `Top ▾` selector chip** (`PaneBounds.X + 10, PaneBounds.Y + 10`)
       - Floating **`Diagnostics` (`F3`) window** (`Timing` / `Scene` / `Render`, also displaying real Force Evaluation time & active force count in `Scene`!)
       - **`Construct (Ctrl+A)`** modal & **`Settings`** sheet
       - Bottom-right **3D `X/Y/Z` Orientation Triad** & **Status Footer** (`PLAYING - CONSTRUCTED N - M forces active - Standard - SPLIT VIEW`)!
   - **Input Routing:** Pointer clicks/drags inside `PaneBounds` (whenever no floating sheet/dropdown covers the cursor) invoke `InteractionCallback`:
     - Shift+Click / Middle-Click or Ortho Pick -> places `PointSampleCursor` *(Anemometer)*;
     - Left-Click -> `ViewportPickQuery` 1×1 readback at `(x, y)` -> picks **`GizmoHandleOrdinal`**, **`ForceCircle`**, or **`MotionWaypoint (A, P1, B)`**;
     - Right-Drag / Alt-Drag / Wheel -> orbits/pans/zooms the active pane's camera (`Primary` or `Secondary` independent cameras in Split View!).

---

## 4. Exact Mapping onto the `04-EntryInspectorSplitAndDiagnostics.png` Workspace UI

```text
+-----------------------+---------------------------------------------------------------+-----------------------+
| OUTLINER (Left Dock)  | VIEWPORT (Center Dock — ViewportPanel + Vulkan GPU Canvas)    | INSPECTOR (Right Dock)|
| Shared OutlinerPanel  | Row 1: [Scene]  [Edit | Simulate | Play/Pause] [⏮] [t 1.8s]    | Shared InspectorPanel |
|                       | Row 2: [+ Construct Ctrl+A] [Focus] | [Perspective ▾]         | • Entry Title & Notes |
| • Visible / Hidden    |        [Force Viz ✓ ▾]  ──► (Header Overlay Toggle & Popover) | • PROPERTIES (2×2):   |
| • Search (Ctrl+F)     |        ...                    [Split] [Diagnostics F3] [Gear] |   [Enabled] [Solo/fx] |
| • Showcase Folders:   |                                                               |   [Dynamic] [Locked]  |
|   ▾ Force Fields      |  ┌─────────────────────────────┬───────────────────────────┐  |   (Dynamic = Curve!)  |
|     • Linear Wind     |  │ [Perspective ▾]             │ [Top ▾]   [Diagnostics]   │  | • Transform:          |
|     • Cyclonic Vortex │  │  Lit 3D Scene               │  Top Ortho Cross-Section  │  |   Position (X, Y, Z)  |
|       (Spline 32 m/s) │  │  + GPU .slang Streamlines   │  + Slice Heatmap & Arrows │  |   Rotation (Bearing)  |
|     • Flow Stream     |  │  + Shaded Reach Bounds      │  + Spline A─P1─B Path     │  |   Scale    (Radius)   |
|     • Item 1 (Mesh)   |  │  + TransformGizmo (Force/Wp)│  + Probe Cursor           │  | ───────────────────── |
|     • Item 3 (Light)  |  └─────────────────────────────┴───────────────────────────┘  | (When Force Selected) |
|                       | Footer: PLAYING - CONSTRUCTED 6 - 3 forces - SPLIT VIEW       | • ForceModel & Bounds |
| REALTIME | QUAL | SUN |         PROBE [-95, 16, 42] |F| 14.5 m/s |∇×F| 0.312 /s      | • Curve & Waypoints   |
+-----------------------+---------------------------------------------------------------+ • VectorFieldLang fx  |
                                                                                        | • Wind Anemometer     |
                                                                                        +-----------------------+
```

### Detailed Pane Extensions (Reusing Existing Panes — Zero Dedicated Outliner)

1. **`OutlinerPanel` (`Engine/Components/Panes/OutlinerPanel.{h,cpp}`):**
   - **Shared Across All Editors:** World Editor, Vehicle Proving Ground, Particle Editor, Fluid Editor, Volumetric Cloud Editor all host Force entries directly inside their existing `OutlinerPanel` (`OutlinerRecordEntry`).
   - Add `ConstructSlotCategory::ForceField = 5` in `WorkspaceAppearance.h` (renders clean Force Wind/Field icon glyph and filter category chip).
   - **Row Bindings:**
     - Eye (`VisibilityEnabled`) -> toggles force debug visibility & active preview;
     - `DynamicMotion` badge -> indicates active `ForceMotionPath` (`A↔B`, `Spline`, `Orbit`);
     - Reorder / Showcase Folder nesting -> uses `OperationRecorder` undo/redo (`215 MiB` per document, `750 MiB` global).
   - **`+ Construct (Ctrl+A)`:** Adds a **Force Fields** tab/section in `ViewportPanel::RenderConstructDialog` listing all **19 `ForceModel` templates** alongside Mesh/Layer/Light templates (or instantiated by dragging a `.ForceModel` preset from `ContentBrowser`).

2. **`ViewportPanel` Header & Modes (`Engine/Components/Panes/ViewportPanel.{h,cpp}`):**
   - **Only Viewport Addition:** Compact **`Force Viz` Toggle Pill + `▾` Style Popover** placed cleanly in the open toolbar gap immediately right of `Perspective ▾`:
     - **Primary Toggle:** Instantly switches `DisplaySettings.ForceOverlay.Enabled` ON/OFF even while `Lit` shading / Global Illumination / Raytracing are active.
     - **Dropdown Popover (`ForceVizPopover`):** Mirrors every finalized layer & streamline control from `Experimental/VectorFieldVisualizer/`:
       - Layers: `Heatmap` · `Vectors` · `Streamlines` · `Bounds` · `Motion Curves`
       - Slice Planes: `XY` (Horizontal `+Z` slice) · `XZ` · `YZ` · `All 3` + `Slice Offset (m)` slider
       - Streamline Color Mode: `Speed Color` (`FieldPalette`) · `Single Cyan (#68dcff)` · `Custom Color` · `Altitude`
       - Streamline Tuning: `Thickness` (`0.62×` default) · `Emissive` (`1.0×` default, boosts HDR Lit visibility) · `Opacity` (`0.92`) · `Curve Resolution` (`8..72`) · `Number of Lines` (`24..520`)
   - **Transport Extensions Next to `Edit | Simulate | Pause`:** Add compact **`⏮ Rewind (t=0)`** capsule + **`t X.Xs`** clock readout so curve animations can be rewound cleanly to Waypoint `A`.

3. **`InspectorPanel` (`Engine/Components/Panes/InspectorPanel.{h,cpp}`):**
   - Keeps the exact layout of `04-EntryInspectorSplitAndDiagnostics.png`:
     - Header Title + Subtitle (`"Wind & Vortex · FlowField · 32 m/s"`) + `Notes` editor
     - **`PROPERTIES` 2×2 Capability Tiles:** `Enabled` · `Solo / GPU fx` · **`Dynamic`** *(toggles `ForceMotionPath` curve animation!)* · `Locked`
     - **`Transform` (`Position X/Y/Z` · `Rotation X/Y/Z` · `Scale X/Y/Z`):** Mapped live to Force `Centre`, `Bearing/Tilt`, and `Radius/HalfExtents` (or active Waypoint when a waypoint `A`/`P1`/`B` is selected on `TransformGizmo`!).
   - **Appended Scrollable Force Inspector Sections (Shown Whenever Selected Entry Has a Force Component):**
     1. **`ForceModel & Envelope`:** `ForceModel` selector (`19` laws), `ReachEnvelope` (`Sphere`/`Column`/`OrientedBox`), `ForceAttenuation` (`SmoothStep`/`Linear`/`InverseSquare`/`None`), `EForceDuration` (`Continuous`/`Modulated`/`Impulse` + Impulse Trigger button), `Radius`/`Strength`/Law parameters, and radial `f(r)` attenuation preview chart.
     2. **`Curve & Motion Path Animation`:** Quick presets (`Add A → B Line`, `Add 3D Spline`, `Add Orbit`, `Clear`), `MotionCycleBehavior` (`PingPong`, `WrapLoop`, `PlayOnce`), `Speed (m/s)`, `Phase`, `Path Scrub [0..100%]` slider (live preview in `Edit` mode!), `+ Knot`, and editable Waypoints (`A`, `P1`, ..., `B`) with click-to-focus **`TransformGizmo`** on any waypoint.
     3. **`Formula (VectorFieldLang)`:** `Evaluate Custom Formula on GPU` switch, 5 built-in presets, quick symbol buttons (`· × r̂ x̂ ŷ ẑ A(r) S curlNoise(p)`), live syntax-highlighted editor, inline diagnostics, and generated `.slang` preview.
     4. **`Anemometer & Probe Telemetry` (`PointSampleCursor`):** Rolling 60s wind trace, Beaufort `0..12`, compass azimuth, dynamic pressure $q$, $|\nabla \times F|$, and $\nabla \cdot F$.

---

## 5. Scoped Vocabulary Allowance & Governance Rules (`BannedWordsVocabulary.md`)

Per explicit user approval, **`ForceModel`**, **`FlowField`**, **`FlowStream`**, and **`NavigationField`** are **explicitly permitted strictly within the VectorField / ForceField subsystems** (they remain banned everywhere else in `Engine/`). All other Frontier governance rules (`ImportantNotes.md`, `BannedWordsVocabulary.md`, `SKILL-Naming.md`) stay strictly enforced:

| Prototype Symbol | Frontier C++20 / `.slang` Symbol (VectorField Scope) | Governance Status |
| :--- | :--- | :--- |
| `ForceModel` (19-law enum & single-force preset `.ForceModel`) | **`ForceModel`** (`Engine/SimulationSpace/ForceFields/`) | 🟢 **Explicit Scoped Exemption** *(VectorField only)* |
| `FlowField`, `FlowStream`, `NavigationField` | **`FlowField`**, **`FlowStream`**, **`NavigationField`** | 🟢 **Explicit Scoped Exemption** *(VectorField only)* |
| `VectorField`, `TurbulenceField`, `StreamlineVisualizer`, `VectorFieldLang` | **`VectorField`**, **`TurbulenceField`**, **`StreamlineRasterization`**, **`FieldFormulaCompiler`** | 🟢 **Clean** |
| `Vortex`, `GustEmitter`, `SurfaceAdhesion` | **`RotationalColumn`** (`"Vortex"` UI text only), **`AtmosphericGustSource`**, **`BoundaryAdhesion`** | 🟢 **Compliant** (`BannedWordsVocabulary`) |
| `ForceEvaluator`, `Probe`, `TelemetryCard`, `Pass`/`Stage`, `Draw*` | **`ForceFieldAccumulator`**, **`PointSampleCursor`**, **`AnemometerTelemetryPanel`**, **`…Rasterization` / `…Sequence`**, **`Construct*`** | 🟢 **Compliant** (`BannedWordsVocabulary`) |
| `Motion` (`LineAB`, `SplineCurve`, `Orbit`, `PingPong`/`Loop`/`Once`) | **`ForceMotionPath`**, **`MotionPathTopology`** (`Static`, `LinearSpan`, `CatmullSpline`, `HelicalOrbit`), **`MotionCycleBehavior`** (`PingPong`, `WrapLoop`, `PlayOnce`) | 🟢 **Compliant** |

---

## 6. Coordinate System Conversion (`Prototype Three.js` -> `Frontier Right-Handed +Z Up`)

Frontier (`Engine/Constants/EngineConstants.h`) enforces the **Strict Right-Hand Rule with `+Z` Up** (`+X` Forward, `+Y` Left, `+Z` Up; $\hat{\mathbf{X}} \times \hat{\mathbf{Y}} = +\hat{\mathbf{Z}}$) and **Reverse-Z `[1.0 -> 0.0]`**:

| Quantity | WebGL2 Prototype (`Three.js`) | Frontier C++20 + Vulkan 1.3 | Exact Transform |
| :--- | :--- | :--- | :--- |
| **Forward Axis** | `-Z_proto` (`Bearing 0°`) | **`+X_fwd`** | `X_fwd = -Z_proto` |
| **Left Axis** | `-X_proto` | **`+Y_left`** | `Y_left = -X_proto` |
| **Up / Altitude Axis** | `+Y_up` | **`+Z_up`** | `Z_up = +Y_proto` |
| **Horizontal Slice Plane** | `XZ` at `Y = SliceHeight` | **`XY` at `Z = SliceOffsetM`** | Vertical slices become **`XZ`** (`Y = offset`) and **`YZ`** (`X = offset`) |

All 19 `ForceModel` laws, `VectorFieldLang` unit vectors (`x̂`, `ŷ`, `ẑ`, `r̂`, `θ̂`), curl potential $\nabla \times \psi$, and spline waypoints share this identical `+Z` Up right-handed math across both C++20 SIMD and `.slang` shaders.

---

## 7. Locked File Format Architecture (**Option A** — `.VectorFieldSpace` + `.ForceModel` + Inline/Referenced `ForceSourceRecord`)

Per `Documentation/Plans/DocumentFormatsInstancePlan.md` and `Engine/Codecs/SpaceContainerCodec.{h,cpp}`, Frontier uses **Two-Tier Hybrid `.Space` Containers** (readable TOML manifest + optional 64B-aligned binary sections).

### Locked Option A Structure (Explicit User Confirmation)

1. **`.VectorFieldSpace` (`Engine/Codecs/VectorFieldCodec.{h,cpp}`):**
   - Native composable Vector Field asset container (kept separate from 2D graphic `.VectorSpace`!).
   - Stores complete Force Field setups, weather fronts, wind-tunnel scenarios, motion curve rigs, and formulas:
     - **TOML Manifest:**
       - `[VectorFieldHeader]`: `FormatVersion`, `CoordinateSystem = "RightHandZUp"`, `CascadePreset`
       - `[[ForceSources]]`: Full `ForceSourceRecord` entries (`ForceModel`, `System = "VectorField" | "FlowField" | "TurbulenceField"`, `ForceDomain`, `ForceAttenuation`, `EForceDuration`, `ReachEnvelope`, spatial extents, physics parameters, `UseFormula`, `FormulaSourceText`)
       - `[[MotionPaths]]`: `MotionPathTopology` (`LinearSpan` `A->B`, `CatmullSpline`, `HelicalOrbit`), `MotionCycleBehavior`, `SpeedMetresPerSecond`, `PhaseOffset`, `Waypoints = [[x,y,z], ...]`
       - `[EditorOverlaySettings]` *(Stripped / ignored in `Development` & `Shipping`!)*: Slice axes, colormap, `StreamColorMode`, `StreamThickness`, `StreamEmissive`, `StreamOpacity`, `StreamResolution`, `StreamLineCount`.
     - **Optional 64-Byte-Aligned Binary Sections:**
       - `FORCES_PACKED`: Pre-aligned `ForceSourceGPURecord[]` for zero-parse DMA upload to Vulkan SSBOs;
       - `FORMULA_IR`: Precompiled `FormulaInstructionStream` bytecode + SPIR-V cache;
       - `SPLINE_LUT`: Arc-length lookup tables for uniform-speed spline traversal.

2. **Inline / Referenced `ForceSourceRecord` inside `.SimulationSpace` / `.WorldSpace` / `.ParticleSpace` / `.VolumetricSpace`:**
   - World scenes, rigid/fluid simulations, cloud layers, and particle systems do **not** require duplicating external files for every individual force:
     - Embed `ForceSourceRecord` (`[[ForceSources]]` + `[[MotionPaths]]`) inline directly inside the `.SimulationSpace` / `.WorldSpace` / `.ParticleSpace` / `.VolumetricSpace` manifest, **OR**
     - Reference a shared reusable rig (`VectorFieldAsset = "Content/Weather/HurricaneEye.VectorFieldSpace"`).

3. **Optional `.ForceModel` Single-Force Preset (`Engine/Codecs/ForceModelPresetCodec.{h,cpp}`):**
   - Lightweight pure-TOML single-force preset file (e.g., `CoastalCrosswind.ForceModel`, `JetEngineExhaust.ForceModel`, `ShockwaveGrenade.ForceModel`) storing one configured `ForceModel` + optional `FormulaSourceText` + optional relative `MotionPath`.
   - Indexed in **`ContentBrowser`** with auto-rendered thumbnail glyphs so artists can drag-and-drop any `.ForceModel` preset straight into **`OutlinerPanel`** or **`ViewportPanel`**!

```text
ContentBrowser / AssetStreamThread
  ├── HurricaneEye.VectorFieldSpace   ──► VectorFieldCodec        (Full multi-source rig + curves + GPU binary cache)
  ├── JetExhaust.ForceModel           ──► ForceModelPresetCodec   (Optional reusable single-force preset TOML)
  └── ProvingGround.SimulationSpace   ──► SimulationCodec         (Inline ForceSourceRecord[] + .VectorFieldSpace links)
```

---

## 8. Runtime Engine Architecture (`Editor` + `Development` + `Shipping`)

Lives strictly under **`Engine/SimulationSpace/ForceFields/`** and **`Engine/Shaders/Compute/`** — zero `Engine/Editor/` dependencies.

### 8.1 C++20 Runtime Modules (`Engine/SimulationSpace/ForceFields/`)

| File | Responsibility | Execution Context |
| :--- | :--- | :--- |
| `ForceFieldDescriptors.h` | `ForceModel` (`0..18`), `EForceDuration`, `ForceAttenuation`, `ForceDomain`, `ReachEnvelope`, `MotionPathTopology`, `MotionCycleBehavior`, `ForceSourceRecord`, `ForceSourceGPURecord` (`alignas(16)`), concept `SpatialForceSampleSource`. | Header-only POD |
| `ForceMotionPath.{h,cpp}` | Deterministic arc-length evaluation (`ComputePathArcLength`, `EvaluateCatmullRom3D`, `EvaluateHelicalOrbit`, `ResolveNormalizedPhase` for `PingPong`/`WrapLoop`/`PlayOnce`, `StepAnimatedForceSources(TimeSeconds)`). | `GameThread` / `PhysicsThread` |
| `FieldFormulaCompiler.{h,cpp}` | Full port of `VectorFieldLang`: Unicode/ASCII lexer, parser, dimensional type checker (`Scalar`, `Vec3`, `HatVec`), constant folding -> emits:<br>1. **CPU Bytecode (`FormulaInstructionStream`)** for deterministic physics;<br>2. **`.slang` Compute/Eval snippet (`vfEvalCustomFormulaN`)** compiled to SPIR-V via `ShaderCompiler` (`slangc`). | `WorkspaceThread` (on edit) / Precompiled on load |
| `VectorField.{h,cpp}` | SIMD analytical laws: `LinearWind`, `Buoyancy`, `RadialBurst`, `Attractor`, `Repulsor`, `AxisAttractor`, `Containment`, `RotationalColumn`, `Torque`, `Drag`, `LorentzField`, `MagneticDipole`. | `PhysicsThread` / `ExecutionCluster` |
| `FlowField.{h,cpp}` | SIMD directed flow laws: `FlowStream` (`Current`, `ThermalUpdraft`, `SlipstreamJet`, `NavigationField`, `VesselWake`), `SplineForce`, `BoundaryAdhesion`, `MagnusForce`, `Lift`. | `PhysicsThread` / `ExecutionCluster` |
| `TurbulenceField.{h,cpp}` | Exact divergence-free $\nabla \times \psi$ `CurlNoise`, `AtmosphericGustSource`, `VorticityConfinement`, and deterministic PCG `BrownianNoise`. | `PhysicsThread` / `ExecutionCluster` |
| `ForceFieldAccumulator.{h,cpp}` | Broad-phase finite-bound culling (`DynamicAABBTree`) + channel accumulation (`AdvectionVelocity [m/s]`, `LinearAcceleration [m/s²]`, `AngularTorque [rad/s²]`, `DragDecay [1/s]`). | `PhysicsThread` / `GameThread` |
| `FieldLatticeCascade.{h,cpp}` | 4-tier cascaded 3D velocity/curl grid (`2 m` finest -> `100 km` weather) + `ForceSourceGPURecord` SSBO uploader via `TimelineTransferRegister`. | `RenderThread` / `ComputeDispatchEncoder` |

### 8.2 Coupling Jolt Physics v5.6.0 & GPU Subsystems Cleanly

```text
                     +------------------------------------------------+
                     |      SimulationSpace::ForceFields (CORE)       |
                     |  ForceModel · FlowField · ForceMotionPath · fx |
                     +--------+------------------------------+--------+
                              |                              |
          (CPU Fixed Step)    |                              |  (GPU SSBO + RWTexture3D Cascades)
                              v                              v
      +-----------------------------------+    +---------------------------------------------+
      | Jolt Physics v5.6.0 (Collision &  |    | Downstream GPU Subsystems (Opt-In Readers)  |
      | Rigid Body Dynamics)              |    |  • ParticleSimulation  (ParticleIntegration)  |
      |  • ForceFieldAccumulator samples  |    |  • FluidSimulation     (FluidAdvection)       |
      |    at body CoM / aero panels      |    |  • VolumetricSpace     (CloudWindAdvection)   |
      |  • Calls BodyInterface::AddForce  |    |  • Weather / Foliage   (Sky & Rain Drift)     |
      |  • Jolt owns ALL solid collisions |    +---------------------------------------------+
      +-----------------------------------+
```

- **Collision Clarification:** `SimulationSpace::ForceFields` never builds its own collision detector; **Jolt Physics v5.6.0** handles all rigid/vehicle/collider contacts cleanly on `PhysicsThread` while consuming wind, buoyancy, shockwave impulses, drag, and spline/attractor forces from `ForceFieldAccumulator`.

---

## 9. Editor-Only Vulkan `.slang` Lit-View Overlay (`FRONTIER_PROFILE_EDITOR` Only)

Compiled **exclusively when `FRONTIER_EDITOR_ENABLED == 1`**. Executes on GPU via `ComputeDispatchEncoder` + `DynamicRendering` directly after **`Lit` PBR / Global Illumination shading** (`SurfaceShading.slang`), **even when Viewport shading is `Lit`**, so forces remain clearly visible around shaded terrain, buildings, and vehicles:

1. **`FieldLatticeCompute.slang`** *(Runtime + Editor)*: Updates `RWTexture3D<float4>` cascades (`Velocity.xyz`, `|∇×F|`) when animated sources move along curves or parameters change.
2. **`InfluenceEnvelopeRasterization.slang`**: Renders semi-transparent shaded 3D Sphere / Column / OrientedBox bounds (**zero wireframes**) + expanding `RadialBurst` wavefront shells using Reverse-Z soft depth fade.
3. **`SliceHeatmapRasterization.slang`**: Orthogonal `XY` / `XZ` / `YZ` cross-section heatmaps (`Speed`, `Accel`, `Curl`, `Divergence`, `Pressure`) + analytical isoline contours.
4. **`VectorArrowRasterization.slang`**: Instanced 3D directional arrows colored by LUT & culled smoothly by quiet threshold.
5. **`StreamlineRasterization.slang`**: GPU RK4 volumetric 3D advection ribbons with `FieldPalette` / `CyanSingle (#68dcff)` / `CustomSingle` / `Elevation` color modes, instant `RibbonThickness`, `EmissiveBoost`, `RibbonOpacity` uniforms, and phase pulse sparks (zero white washout).
6. **`ForcePathAndBeaconRasterization.slang`**: Renders 3D motion paths (`A->B` lines, Catmull-Rom curves, orbits), clickable `A/P1/B` waypoint spheres, and selectable Force Circles writing directly to `PickIDs` (`R32G32B32A32_UINT`) so clicking any circle or waypoint in the viewport triggers `ViewportPickQuery` -> attaches **`Frontier::Editor::TransformGizmo`** (`GizmoRasterization.slang`).

---

## 10. Canonical Target File Layout (`arena/429d7dc7-frontier`)

```text
Engine/
├── Codecs/
│   ├── VectorFieldCodec.{h,cpp}                // Hybrid TOML + 64B binary .VectorFieldSpace Encoder/Decoder
│   └── ForceModelPresetCodec.{h,cpp}           // Optional TOML .ForceModel single-force preset Encoder/Decoder
├── SimulationSpace/ForceFields/                // Compiled in Editor, Development & Shipping Game
│   ├── ForceFieldDescriptors.h
│   ├── ForceMotionPath.{h,cpp}
│   ├── FieldFormulaCompiler.{h,cpp}
│   ├── VectorField.{h,cpp}
│   ├── FlowField.{h,cpp}
│   ├── TurbulenceField.{h,cpp}
│   ├── ForceFieldAccumulator.{h,cpp}
│   └── FieldLatticeCascade.{h,cpp}
├── Shaders/
│   ├── Compute/
│   │   ├── ForceFieldEvaluationCommon.slang
│   │   └── FieldLatticeCompute.slang
│   └── VectorField/                            // FRONTIER_PROFILE_EDITOR ONLY (Compiled out of Shipping)
│       ├── InfluenceEnvelopeRasterization.slang
│       ├── SliceHeatmapRasterization.slang
│       ├── VectorArrowRasterization.slang
│       ├── StreamlineRasterization.slang
│       └── ForcePathAndBeaconRasterization.slang
├── Components/Panes/
│   ├── WorkspaceAppearance.{h,cpp}             // + ConstructSlotCategory::ForceField icon glyph
│   ├── OutlinerPanel.{h,cpp}                   // Shared Outliner mounting ForceSource entries & Construct templates
│   ├── InspectorPanel.{h,cpp}                  // Shared Inspector mounting ForceModel, Curve/Waypoints, Formula & Anemometer
│   └── ViewportPanel.{h,cpp}                   // + RasterSurfaceCallback / InteractionCallback + Header Force Viz Toggle & Popover
└── Editor/Workspaces/Simulation/               // FRONTIER_PROFILE_EDITOR ONLY
    ├── VectorFieldOverlaySequence.{h,cpp}      // Lit-view post-shading Vulkan overlay + PickID pass
    └── VectorFieldViewportController.{h,cpp}   // Connects ViewportPanel, ViewportPickQuery, TransformGizmo & Edit/Simulate/Play loop

Exhibits/
└── VectorFieldValidation.cpp                   // Deterministic CPU/SIMD + SwiftShader Vulkan PNG verification
```

---

## 11. Phased Implementation Sequence & Verification Gates

| Phase | Deliverable | Mandatory Verification Gate (`Exhibits/`) |
| :---: | :--- | :--- |
| **Phase 1: Viewport GPU Bridge (`ViewportPanel`)** | Add `ViewportPaneRasterCallback` & `ViewportPaneInteractionCallback` to existing `ViewportPanel::RenderCanvasPane` (preserving checkerboard fallback when unbound). | Existing `WorkspacePanes` exhibits (`04-EntryInspectorSplitAndDiagnostics.png`) remain pixel-identical when unbound; live Vulkan RGBA texture blits cleanly in Single & Split panes when bound. |
| **Phase 2: Core Runtime, Motion Curves & `FieldFormulaCompiler`** | `SimulationSpace/ForceFields/` (`VectorField`, `FlowField`, `TurbulenceField`, `ForceMotionPath`, `FieldFormulaCompiler`, `ForceFieldAccumulator`, `FieldLatticeCascade`) in `+Z` Up Right-Handed coordinates. | `Exhibits/VectorFieldValidation`: 19 `ForceModel` laws exact zero outside bounds, `|∇·v| < 1e-4` CurlNoise, `PingPong`/`Loop`/`Once` splines, CPU bytecode vs `.slang` parity within `1e-4`, and Jolt `AddForce` integration check. |
| **Phase 3: `.VectorFieldSpace` & `.ForceModel` Codecs** | `VectorFieldCodec` (`.VectorFieldSpace`) + `ForceModelPresetCodec` (`.ForceModel`) + inline `ForceSourceRecord` in `.SimulationSpace`. | Round-trip TOML + 64B binary encode/decode parity test + `ContentBrowser` thumbnail & drag-drop spawn check. |
| **Phase 4: Vulkan `.slang` Lit-View Overlay & Shared Gizmo/Outliner/Inspector** | `VectorFieldOverlaySequence` + 5 `.slang` units + `OutlinerPanel` Force entries + `InspectorPanel` Force/Curve/Formula/Anemometer cards + `ViewportPanel` Header Toggle + `TransformGizmo` Force/Waypoint attach. | Deterministic Vulkan/SwiftShader `.png` snapshots in `Exhibits/Images/VectorField/`: (a) `Lit` view with Force Viz ON (thin Cyan & Speed streamlines, shaded bounds, `A->B` spline & waypoints over shaded scene); (b) `Edit` / `Simulate` / `Play` curve stepping & `PickIDs` gizmo drag test. |
| **Phase 5: 3-Profile Build Audit (`Editor` · `Development` · `Shipping`)** | Full CMake build across all 3 profiles (`-DFRONTIER_PROFILE=Editor|Development|Shipping`). | Confirm **zero `Engine/Editor/` or `Shaders/VectorField/` symbols linked in `Development`/`Shipping`**, while `.VectorFieldSpace`, `.ForceModel`, `ForceMotionPath`, `FieldFormulaCompiler`, and Jolt/GPU consumers run at full performance. |

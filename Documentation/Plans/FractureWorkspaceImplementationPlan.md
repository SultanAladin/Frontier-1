# Fracture Workspace & Geometry/Physics Fracture C++20 Implementation Plan

**Date:** 2026-11-15  
**Target Domains:** `Engine/Components/` · `Engine/GeometrySpace/Fracture/` · `Engine/SimulationSpace/PhysicsSpace/` · `Engine/Shaders/GeometricRaster/` · `Engine/Workspaces/Fracture/` · `Experimental/FractureEditor/` · `Exhibits/`

---

## 1. Authoritative Scope & Ground Truth

| Area | Reference Source | C++20 / Target Deliverable |
| :--- | :--- | :--- |
| **Fracture & Solver Algorithms** | `c7egoist/Frontier` (`arena/eb353da2-frontier`, mirrored in `Experimental/FractureEditor/Engine/`) | `Engine/GeometrySpace/Fracture/` (reusing `GeometrySpace` primitives & `PolygonNetwork`) + `Engine/SimulationSpace/PhysicsSpace/FractureSimulationRunner.{h,cpp}` (JoltPhysics v5.6.0 & `BreakableBond`) |
| **UI Layout & Studio Workflow** | `egoistm1inbox/Frontier` (`arena/9f5191f2-frontier`: Outliner, Viewport, Inspector, notches, Diagnostics, Playback footer) | `Engine/Workspaces/Fracture/` (`FractureWorkspaceHost`, `FractureViewportHost`, `FractureOutlinerPanel`, `FractureInspectorPanel`, `FractureWorkspaceSession`) |
| **Rendering Pipeline** | `MaterialPreviewRenderer` (`Engine/Workspaces/Material/` + `OpenPbrSurfaceScattering.slang` + `StudioEnvironmentRadiance.slang` — Direct Studio 10-lobe OpenPBR + 3 Softboxes + IBL, **no GI**) | `FractureViewportRenderer.{h,cpp}` + `Engine/Shaders/GeometricRaster/FractureSurfaceResolve.slang` (OpenPBR + Softbox/IBL lighting for fractured convex polyhedra + `Energy`/`Cells`/`Normals` debug modes) |
| **Sliders (C++ & HTML)** | `Engine/Components/Controls/ScalarSlider.{h,cpp}` | Reuse `ScalarSlider::Construct` / `ConstructWithReadout` in C++; replace raw `<input type="range">` in `Experimental/FractureEditor/` HTML with a matching Frontier slider control |
| **Standalone App & Editor Tab** | `FrontierMaterial` / `FrontierParticle` / `FrontierEditor` (`CMakeLists.txt`, `Engine/Lifecycle/`) | `FrontierFracture` executable + `EditorLifecycle` Fracture workspace tab |

---

## 2. Phase A — Reusable UI Components (`Engine/Components/`)

1. **`ExecutionModeStrip.{h,cpp}` (`Engine/Components/Controls/`)**:
   - Extract the `Edit` / `Simulate` / `Play`–`Pause` capsule strip (plus optional `Rewind` button) from `ViewportPanel.cpp` into `ExecutionModeStrip::Construct(...)` returning `ExecutionModeStripResult`.
   - Refactor `ViewportPanel.cpp` to consume `ExecutionModeStrip::Construct(...)`, and reuse it in `FractureViewportHost`.
2. **`DiagnosticsOverlay.{h,cpp}` (`Engine/Components/Diagnostics/`)**:
   - Extract and generalize the floating `Diagnostics` widget (`F3` toggle, draggable header, bottom-right resize grip, layout reset, metric/category selector, `Plot + Stats` / `Plot only` / `Stats only` view selector, `15 s / 30 s / 60 s` history span, oversized split-fraction readout, sparkline/plot canvas, stats grid, and `Pause/Resume samples`).
   - Refactor `ViewportPanel` to host `DiagnosticsOverlay` (`Timing`, `Scene`, `Render`) and reuse it in `FractureViewportHost` (`Frame rate`, `Frame interval`, `Bake timing`, `Fracture result`, `Simulation · kinetic energy`).
3. **`SimulationTimelineFooter.{h,cpp}` (`Engine/Components/Controls/`)**:
   - Build a reusable playback footer strip alongside `SimulationPlaybackControls` and `SimulationTimeRuler`:
     - Left transport cluster (`To Start`, `Play/Pause`, `Step One`).
     - Digital clock (`0.00 / 4.00 s`) + phase caption (`EDIT · NOT BAKED` / `SIMULATED` / `PLAYING`).
     - Center scrubbable timeline track with kinetic energy curve, impact/event markers, and unbaked prompt.
     - Right telemetry & controls (`Kinetic` J + moving count, `¼× / ½× / 1× / 2×` speed segmented strip, `Loop` toggle).

---

## 3. Phase B — HTML Slider Alignment (`Experimental/FractureEditor/`)

- Replace raw `<input type="range">` controls (`#Explode` and Inspector `.field` sliders) with a custom Frontier slider control matching `ScalarSlider` (track rail, active fill bar, circular thumb, split readout capsule `[ value | unit ]`, pointer-capture dragging, and click-to-type numeric entry).

---

## 4. Phase C — Core Algorithms (`Engine/GeometrySpace/Fracture/` + `Engine/SimulationSpace/PhysicsSpace/`)

Port `c7egoist`'s four modules into portable ISO C++20 under `Engine/GeometrySpace/Fracture/` (reusing `PolygonNetwork` and `AnalyticPrimitives`) and `Engine/SimulationSpace/PhysicsSpace/` (strictly compliant with `Documentation/BannedWordsVocabulary.md` — no `Complex`, `Catalogue`, `Record`, `Coordinator`, `State`, `Value`, `And`):

1. **`Engine/GeometrySpace/Fracture/FractureMaterialRepository.{h,cpp}`, `FracturePartitionTypes.h`, `ConvexPartitioning.{h,cpp}`** *(from `ConvexPartitioning.js`)*:
   - Concrete, Granite, Annealed Glass, Tempered Glass, Timber, Ductile Polymer properties ($E$, $\nu$, $G_c$, Rayleigh wave speed, Weibull modulus, fractography parameters).
   - `ConvexPolyhedron` + `PolygonNetwork` conversion bridges, analytical solid constructors via `AnalyticPrimitives`, `ClipConvexPolyhedron`, exact volume/centroid/inertia calculation, `ConstructSharedCutBonds`, and the 6 partition solvers (`FastRuntimeBoussinesq`, `EngineeringBakedSolid`, `GriffithStressBudget`, `LefmWavefront`, `ClusteredVoronoi`, `DendriticSpallHull`) + `ComputePieceSdfSection`.
2. **`Engine/GeometrySpace/Fracture/FastRuntimeFracture.{h,cpp}`** *(from `FastRuntimeFracture.js`)*:
   - Pre-allocated `FlatClipScratch` buffers + allocation-free angular cap-polygon insertion sort.
   - Closed-form Boussinesq–Hertz impact stress field and $O(N)$ `CutToken`-bucketed strain bonds.
3. **`Engine/GeometrySpace/Fracture/EngineeringBakedFracture.{h,cpp}`** *(from `EngineeringBakedFracture.js`)*:
   - 3D Navier–Cauchy elastostatic lattice (Hertzian contact traction + foundation support + Hopkinson spall).
   - Analytical 3×3 symmetric principal stress eigensolver ($\sigma_1 \ge \sigma_2 \ge \sigma_3$, Von Mises, strain energy density).
   - Mode-I Rankine & Mode-II Mohr–Coulomb failure with 3-parameter Weibull flaws, sequential stress-shadow wake relaxation, and watertight matched non-planar fractographic tessellation (Bouchaud $H \approx 0.78$ + aggregate pullout sockets, Mirror–Mist–Hackle + Wallner arcs, wood flutes, cup-and-cone).
4. **`Engine/SimulationSpace/PhysicsSpace/FractureSimulationRunner.{h,cpp}`** *(from `RigidBodyDynamics.js` + `PhysicsSpace`)*:
   - Couples fractured `ConvexPolyhedron` rigid bodies and `FractureBondEntry` bonds to `PhysicsExtension` (JoltPhysics v5.6.0 `CollisionShapeCategory::ConvexHull` + `ConstraintCategory::BreakableBond`), plus baked `FractureTimestepSnapshot` pose-cache playback and scrubbing.
5. **`Exhibits/FractureGeometryValidation.cpp`**:
   - Verifies all 6 solvers × primitives (`VolumeDriftRatio < 1e-4`), `FastRuntimeFracture` sub-4 ms & `< 1e-12` volume drift, 3×3 eigensolver $I_1$ conservation, and JoltPhysics `PhysicsExtension` + `BreakableBond` simulation.

---

## 5. Phase D — Material-Parity OpenPBR + Debug Renders (`Energy` & `Cells`)

- **`Engine/Shaders/GeometricRaster/FractureSurfaceResolve.slang`** & **`Engine/Workspaces/Fracture/FractureViewportRenderer.{h,cpp}`**:
  - Reuses `OpenPbrSurfaceScattering.slang` (10-lobe OpenPBR BSDF) and `StudioEnvironmentRadiance.slang` (3 Studio Softboxes + IBL, **no GI**) via Vulkan 1.3 compute dispatch + deterministic CPU reference fallback, outputting RGBA8 into `ImTextureData`.
  - **Shading / Debug Modes**:
    - `Material`: Maps each fracture material preset to a `DynamicParameterBlock` rendered with full OpenPBR + softbox lighting and distinct cut-face roughness/albedo.
    - `Energy` (**blue → red**): Colours fragments by impact energy share (`Edit`) or live kinetic energy (`Sim`/`Play`) across the blue → cyan → green → yellow → red ramp with linear or `LOG` scaling and floating colorbar legend.
    - `Cells`: Deterministic per-cell palette matching Outliner swatches for unmistakable partition inspection.
    - `Normals`: World-space / exact-normal diagnostic shading.
  - **Viewport Overlays**: Wireframe (`W`), seed points + impactor sphere/travel vector/damage ring (`V`), ground grid (`G`), and fragment selection leader callout.

---

## 6. Phase E — Fracture Workspace UI, Executables & 3-Profile Verification

- Implement `FractureWorkspaceSession`, `FractureOutlinerPanel`, `FractureViewportHost`, `FractureInspectorPanel`, and `FractureWorkspaceHost` (`ScalarSlider` everywhere, `ExecutionModeStrip`, `DiagnosticsOverlay`, `SimulationTimelineFooter`).
- Wire `FrontierFracture` standalone app (`Engine/Lifecycle/FrontierFractureLaunch.cpp`) + `EditorLifecycle` tab in `CMakeLists.txt`.
- Verify `Editor`, `Development`, and `Shipping` builds, CTest (`FractureGeometryValidation`, `FractureWorkspaceUIValidation`, `WorkspacePanesValidation`), and visual proof PNGs.

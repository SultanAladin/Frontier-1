# UV Workspace Sync, Single-Concept Naming & Reusable Viewport Report

**Branch:** `arena/4c9015d5-frontier`  
**Synced Upstream:** `unassignedinbox/arena/429d7dc7-frontier` (`534b6277` — *Sync workspace tab strips with ThemeRepository, unify segmented tracks, and sharpen category list selection*)  
**Verification Status:** 🟢 **12/12 CTest Suites Passing (`36/36` runs) across `Editor`, `Development`, and `Shipping`**

---

## 1. Approved Single-Concept Module, Shader & Enum Unification

All `"And"` compound names and `*Kind` / `*Preset` duplicates have been eliminated across `Engine/`, `Exhibits/`, and `CMakeLists.txt`:

| Domain | Previous Identifier / Path | Canonical Replacement |
| ------ | -------------------------- | --------------------- |
| **Geometry Partitioning** | `Engine/GeometrySpace/Network/FaceGroupReference.{h,cpp}` | `Engine/GeometrySpace/Network/FacePartitionRegister.{h,cpp}` (`struct FacePartitionSpan`, `class FacePartitionRegister`) |
| **GPU Timeline Transfers** | `Engine/GeometricRaster/Compute/AsyncReadbackRing.{h,cpp}` | `Engine/GeometricRaster/Compute/TimelineTransferRegister.{h,cpp}` (`class TimelineTransferRegister`, `struct TransferSlot`) |
| **GPU UV Inspection** | `Engine/GeometricRaster/Shaders/StretchAndOverlapCompute.slang` | `Engine/GeometricRaster/Shaders/UVDistortionInspection.slang` (`UVDistortionInspection.comp.spv`, `GPUUVDistortionSummary`, `EvaluateUVDistortionInspection`) |
| **Texel Density Solver** | `Engine/Workspaces/UV/Solvers/TexelDensityAndUDIMSolver.{h,cpp}` | `Engine/Workspaces/UV/Solvers/TexelDensityEqualizer.{h,cpp}` (`class TexelDensityEqualizer`) |
| **Parameterization Exhibit** | `Exhibits/UVSolversAndCodecValidation.cpp` | `Exhibits/UVParameterizationValidation.cpp` (`UVParameterizationValidation`) |
| **Content Categories** | `Frontier::ContentCategoryKind` | `Frontier::ContentCategory` |
| **Workspace Categories** | `WorkspaceDomainKind` + `WorkspaceDomainPreset` | Unified `Frontier::WorkspaceCategory` (`SceneWorld`, `UV`, `GeometryCad`, `Material`, `TexturePaint`, `Image`, `Vector`, `Simulation`, `Particle`, `Audio`) |

---

## 2. Reusable `ViewportProjection` & `ViewportProjectionPane` Architecture

Viewports are now domain-neutral reusable engine components decoupled from UV-specific UI panels:

1. **`Engine/Editor/Viewports/ViewportProjection.{h,cpp}`** (`FRONTIER_ENGINE_SOURCES`, zero ImGui coupling):
   - Encapsulates `ViewportSplitMode` (`SplitPlanarAndSpatial`, `Planar2DOnly`, `Spatial3DOnly`), normalized split ratio, `SpatialCameraRig` (`OrbitSpatialCamera`, `DollySpatialCamera`, `EvaluateCameraEyePosition`), and `PlanarWindowRig` (`EvaluateAspectLockedPlanarBounds` guaranteeing 1:1 square UDIM tiles and circular 2D gizmo rings across arbitrary pane aspect ratios).
2. **`Engine/Workspaces/WorkspaceDocument.{h,cpp}`**:
   - Owns a live `Editor::ViewportProjection` alongside its `WorkspaceToken`, `.Space` container payload, and `215 MiB` `RevisionTracker`, exposing `AccessViewportProjection()` / `InspectViewportProjection()` to every open `.Space` document (`UVSpace`, `GeometrySpace`, `TextureSpace`, `MaterialSpace`, `VectorSpace`, `SimulationSpace`).
3. **`Engine/Components/Panes/ViewportProjectionPane.{h,cpp}`** (`FRONTIER_UI_COMPONENT_SOURCES`):
   - Binds directly to `WorkspaceDocument*`, manages dual `ImTextureData` surfaces, renders reusable split-mode and domain action capsules, and presents both 2D planar and 3D spatial GPU framebuffers exclusively via upstream `PixelSpace::FillTexturedRectangle`.
4. **`UVViewportHost` & `UVWorkspaceHost` (`ContentBrowser` Integration)**:
   - `UVViewportHost` delegates layout, toolbar capsules (`Unwrap`, `Relax GPU`, `Pack UVs`), texture synchronization, and blitting to `ViewportProjectionPane`.
   - `UVWorkspaceHost::AttachContentBrowser` configures `WorkspaceCategory::UV` (strictly exposing `6` UV-permitted categories while hiding `Simulations`, `Particles`, and `Audio`) and registers `"uv.priority_layer.tint"` so `ColourCodex` swatches live-tint the selected UV Priority Layer.

---

## 3. Multi-Profile Verification Matrix

| Build Profile | CMake Flag | CTest Result | Key Verified Invariants |
| ------------- | ---------- | ------------ | ----------------------- |
| **`Editor`** | `-DFRONTIER_PROFILE=Editor` | ✔️ `12/12` Passed | Full `ContentBrowser` (`WorkspaceCategory::UV` + `ColourCodex` slot tinting), `ThemeRepository`-synced workspace tabs, Vulkan 1.2 `.slang` UV solvers & rasterizer |
| **`Development`** | `-DFRONTIER_PROFILE=Development` | ✔️ `12/12` Passed | Standalone `FrontierUV` (`--standalone=uv`) & `FeatureSet` docked panes, `215 MiB` `RevisionTracker` bit-exact Undo/Redo |
| **`Shipping`** | `-DFRONTIER_PROFILE=Shipping` | ✔️ `12/12` Passed | Zero `Engine/Editor/Explorer/` linkage in `FrontierEngine`, headless batch clamp, `0` flips & `0` SAT overlaps on `ControlledIntegrator` (`9.65 ms`) |

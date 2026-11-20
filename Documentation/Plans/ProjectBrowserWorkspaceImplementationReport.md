# Project Browser, Live World Editor Wiring & C++20 Project Generator — Implementation & Verification Report

## 1. Summary of Completed Work

All four phases of `Documentation/Plans/ProjectBrowserWorkspacePlan.md` have been implemented using **strictly the existing Frontier C++ UI component kit** (`PixelSpace`, `CapsuleButton`, `DropdownSelector`, `ExclusiveSelector`, `TextInputField`, `OutlinerPanel`, `ViewportPanel`, `InspectorPanel`, `ControlCentrePanel`, `ContentBrowser`, `WorkspaceDockContainer`) and the **canonical C++ Global Illumination renderer** (`TieredWorldRenderSequence` + `ShowcaseGalleryScene`), with **zero invented UI frameworks** and **zero `SDK` naming**.

---

## 2. Architecture & Delivered Units

### 2.1 Live World Editor Wiring (`EditorLifecycle` + `VulkanWorldRenderSequence` + `ShowcaseGalleryScene` + `WorldGizmoOverlayRasterization`)
- **`Engine/GeometricRaster/Shaders/WorldViewportRadiometricCompute.slang` & `Engine/GeometricRaster/GlobalIllumination/VulkanWorldRenderSequence.{h,cpp}`**
  - Hardware Vulkan 1.2/1.3 `.slang` compute world renderer (`[numthreads(8, 8, 1)]`, descriptor slots `0..6`) replacing single-threaded CPU per-pixel `TieredWorldRenderSequence` evaluation on both `EditorLifecycle` and `ProjectBrowser` whenever a Vulkan device is attached:
    - Front-to-back ordered BVH4 (`WideTreeEntry`) closest-hit & shadow-occlusion ray traversal with Phong-interpolated vertex normals and geometric-normal shadow-terminator protection (`SafePrimaryPos`).
    - 64-byte OpenPBR surface scattering (Oren-Nayar diffuse, anisotropic GGX VNDF specular, F82 conductor edge tint, clearcoat layer).
    - Heitz 2016 Inverse-LTC upper-hemisphere Sutherland-Hodgman clipped polygonal quad area lights (`EvaluateAreaQuadLight`) with 2-strata BVH visibility gating.
    - 2-scale SDF cone ambient occlusion (`0.32 m` chassis + `1.85 m` room) + Lagarde specular AO, 4-cone SDF Cornell bounce GI (`Tier1GtxCalibrated`) / trilinear 64-texel octahedral irradiance lattice (`Tier2RtxDeterministic`), secondary glossy conductor/clearcoat reflections, and 2-layer Abbe-dispersed crown glass raytracing (`CrownIor = 1.52`, `Abbe = 26`).
    - Hardware-adapter detection (`VulkanInterchange::QueryHardwareAccelerated()`) scaling interactive resolution automatically (`960 px` base on hardware GPUs at `120–240+ fps` vs compact fallback on software ICDs) with exact `CanvasBounds` aspect preservation.
- **`Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.cpp`**
  - Added `SynchronizeApplicationTexture` so dynamically resized `ImTextureData` viewports safely retire existing `VkImage`/`VkDescriptorSet` allocations (`ImTextureStatus_WantDestroy`) prior to `ImTextureStatus_WantCreate`.
- **`Engine/Components/Panes/WorkspaceAppearance.{h,cpp}` & `Engine/Components/ContentBrowser/ContentBrowserPanel.{h,cpp}`**
  - Added foreground overlay pointer shielding (`AssignOverlayPointerShield` / `ClearOverlayPointerShield`), `SynchronizeHostBounds`, `QueryDrawerPhysicalBounds`, `QueryNotchPhysicalBounds`, unified `ControlCentre` `UiScale` propagation, `Escape` close, and `Ctrl+Space` drawer toggle so `ContentBrowser` closes reliably from its top-right `X` button (over `InspectorPanel`), top notch (over `ViewportPanel`), or keyboard at any UI scale.
- **`Engine/GeometricRaster/Gizmo/WorldGizmoOverlayRasterization.{h,cpp}`**
  - Executes compiled Vulkan `.slang` `GizmoRasterization.vert.spv` / `GizmoRasterization.frag.spv` over `TieredWorldRenderSequence` RGBA8 buffers (`VK_ATTACHMENT_LOAD_OP_LOAD`) with an MRT `VK_FORMAT_R32G32B32A32_UINT` handle-pick attachment (`GizmoHandleOrdinal`) and CPU readback (`QueryLastGpuPickAtPixel`), strictly satisfying `CLAUDE.md` §2.1.
- **`Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.{h,cpp}`**
  - Added `ShowcasePrimitiveShape` (`Box`, `Sphere`, `Cylinder`, `Cone`, `Torus`) backed by `AnalyticPrimitives` and heap-stable `DynamicGeometryAssets` (`std::unique_ptr`).
  - Added `PreserveConstructTemplates` on `RegisterShowcaseGalleryInOutliner` so all 27 unified Construct templates remain active.
  - Added `SynchronizeShowcaseGallerySceneLive`, `RebuildShowcaseSceneAcceleration` (`WideBoundingTree` + `SignedDistanceVolume`), `ConstructShowcasePrimitiveInOutliner`, `AssignShowcaseEntryBaseColor` (`ColourCodex` swatch -> linear OpenPBR `Diffuse.BaseColor`), `PickShowcaseGalleryEntryAtUv`, and `RaycastShowcasePlacementAtUv`.
- **`Engine/Lifecycle/EditorLifecycle.{h,cpp}` & `Engine/Lifecycle/DesktopWorkspaceLaunch.{h,cpp}`**
  - Wired Z-up (`+Z`) viewport navigation (RMB mouse-look + WASD/Q/E fly, MMB / Shift+RMB planar pan, Alt+LMB orbit, wheel dolly, `+`/`-`/`Fit` navigation capsule, `F` frame selection).
  - Wired `W`/`E`/`R`/`U` `Editor::TransformGizmo` handle-filter shortcuts, GPU gizmo handle hover/drag (`BeginDragInteraction` / `EvaluateDragInteraction` / `EndDragInteraction` with `Ctrl` snap), and 3D ray click-to-select.
  - Registered live `ColourSlotRegistration` (`"ActiveWorldSurfaceColour"`) on `ContentBrowser` so `ColourCodex` swatches apply directly to the selected World specimen's OpenPBR `BaseColor`, plus `ContentCategory::Geometry` / `Simulations` spawning.
  - Added `Projects` capsule button in the top workspace bar to return cleanly to `ProjectBrowser` at any time.

### 2.2 Capability-Driven Project Browser & C++20 Project Scaffolding
- **`Engine/Workspaces/WorkspaceCapabilityRegister.{h,cpp}`**
  - Advertises strictly the active Frontier host capabilities: **`Frontier`** (*"Open with Frontier"*) and **`UVWorkspace`** (*"Open with UVWorkspace"*), prioritizing `.projectspace` for Frontier World and `.obj` / `.GeometrySpace` / `.UVSpace` for UVWorkspace while keeping both available.
- **`Engine/Configuration/ProjectGenerator.{h,cpp}`**
  - Scaffolds standalone C++20 projects at any user-chosen `DestinationDirectory` (`<DestinationDirectory>/<PascalName>/`):
    - `<PascalName>.projectspace` (via `ProjectSpaceRepository`)
    - `CMakeLists.txt` defining 3 targets: **`<PascalName>`** (`Development`), **`<PascalName>Editor`** (`Editor`), and **`<PascalName>Shipping`** (`Shipping`, linking strictly `FrontierEngine` with zero Editor/UI symbols)
    - `Engine/<PascalName>.h`, `Engine/Lifecycle/<PascalName>Application.{h,cpp}`, `Engine/SimulationSpace/<PascalName>WorldSetup.{h,cpp}`, and `Engine/UserInterface/<PascalName>ViewportOverlay.{h,cpp}` under `namespace <PascalName>::*`
    - `Content/Worlds/MainWorld.WorldSpace`, `Content/UV/MainSurface.UVSpace`, `Shaders/MaterialSpace/<PascalName>Surface.slang`, `Binaries/Win64/`, and `SessionOutput/`
- **`Engine/Lifecycle/ProjectExecutionRunner.{h,cpp}`**
  - Exports `FrontierRunShippingProjectLoop` from `FrontierEngine` (`Binaries/Win64/FrontierEngine.{dll,lib,so}`, zero Editor/ImGui symbols) and `FrontierRunDevelopmentProjectLoop` / `FrontierRunEditorProjectLoop` from `FrontierWorkspaceUi` (`Binaries/Win64/FrontierWorkspaceUi.{dll,lib,so}`).
- **`Engine/Editor/Explorer/ProjectBrowser.{h,cpp}` & `Engine/Lifecycle/FrontierHostLaunch.cpp`**
  - Built 100% from `PixelSpace` + `CapsuleButton` + `DropdownSelector` + `ExclusiveSelector` + `TextInputField` + `TieredWorldRenderSequence` live thumbnails and Reader sequence views (`OLED | Dark | Light` themes, `+ New Project` modal, `Open from disk` + Win32 `IFileOpenDialog` folder/file picker, `Details | Comments | History` inspector, and `Open with Frontier` / `Open with UVWorkspace` hand-off).

---

## 3. Build, CTest & ABI Verification Matrix

| Verification Gate | Profile / Target | Result |
|---|---|---|
| CMake + CTest Suite | `FRONTIER_PROFILE=Editor` | **26 / 26 Passed (100%)** |
| CMake + CTest Suite | `FRONTIER_PROFILE=Development` | **25 / 25 Passed (100%)** |
| CMake + CTest Suite | `FRONTIER_PROFILE=Shipping` | **24 / 24 Passed (100%)** |
| External Generated Project Build & Run (`ProjectBrowserValidation`) | `AeroGallery`, `AeroGalleryEditor`, `AeroGalleryShipping --verify-ticks` | **3 / 3 Built & Executed (`exit 0`)** |
| Shared Binary Export Hygiene (`nm -D --defined-only`) | `Binaries/Win64/libFrontierEngine.so` | Exports strictly `FrontierRunShippingProjectLoop` (0 ImGui / Editor symbols) |
| Shared Binary Export Hygiene (`nm -D --defined-only`) | `Binaries/Win64/libFrontierWorkspaceUi.so` | Exports strictly `FrontierRunDevelopmentProjectLoop` & `FrontierRunEditorProjectLoop` |
| Banned-Words Vocabulary Audit | All new & modified `.h` / `.cpp` files | **0 violations** against `Documentation/BannedWordsVocabulary.md` |

---

## 4. Deterministic Visual Proofs (`Exhibits/Images/`)

1. **`Exhibits/Images/Workspaces/01-ProjectBrowserLibrary.png`** — Native C++ `Frontier` Project Browser Library (`OLED | Dark | Light`, `+ New Project`, `Open from disk`, 10-vertex SVG star toggles, and live `TieredWorldRenderSequence` thumbnails).
2. **`Exhibits/Images/Workspaces/02-ProjectBrowserReaderAndOpenWith.png`** — Native C++ `Frontier` Reader view with per-level `Studio` / `Courtyard` / `Workshop` camera thumbnails, zoomable `TieredWorldRenderSequence` preview, `Open with Frontier` + `Open with UVWorkspace`, and `Details | Comments | History` inspector.
3. **`Exhibits/Images/Workspaces/03-ProjectBrowserNewProjectModal.png`** — Native C++ `Create Project` scaffolder modal (`AeroGallery | AeroGalleryEditor | AeroGalleryShipping`, `SessionOutput/`, capability switches, and `Create & Open in Frontier`).
4. **`Exhibits/Images/GlobalIllumination/03-WorldEditorGizmoAndContentBrowser.png`** — Native C++ `FrontierEditor` showing the GPU `.slang` 3D `TransformGizmo` anchored on `Mirror Chrome Sphere`, `Projects` return button, `OutlinerPanel`, `InspectorPanel`, and `ContentBrowser` (`Apply to Mirror Chrome Sphere BaseColor`).
5. **`Exhibits/Images/GlobalIllumination/04-WorldEditorFullCanvasWithGizmo.png`** — Native C++ `FrontierEditor` full-height Cornell-Studio Gallery viewport with GPU `.slang` 3D `TransformGizmo` + `InspectorPanel` SpatialTransform controls.

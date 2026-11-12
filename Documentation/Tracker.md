# Frontier Engineering Tracker

Single authoritative roadmap and milestone status board for **Frontier**.
Deferred items live in [Backlog.md](Backlog.md); architectural invariants live in
[ImportantNotes.md](ImportantNotes.md).

---

## Milestone Overview

| Phase | Milestone | Status | Primary Domains |
| ----- | --------- | ------ | --------------- |
| **Phase 0** | Governance, Pinned Dependencies, ImGui Patch Stack & CMake Profiles | 🟢 Complete | `Documentation/` · `Automation/` · `ExternalPackages/` · `Engine/Configuration/` |
| **Phase 1** | **Input Foundation & Cross-Platform Windowing** | 🟢 Complete | `Engine/Input/` · `Engine/Windowing/` · `Exhibits/` |
| **Phase 2** | **Shared UI Components & Multi-Document Workspace Groundwork** | 🟢 Complete | `Engine/Components/` · `Engine/Workspaces/` · `Engine/Codecs/` |
| **Phase 3** | **`.slang` Shader Compiler, Vulkan 1.2 Timeline Interchange, GPU Picking & `TransformGizmo`** | 🟢 Complete | `Engine/GeometricRaster/` · `Engine/Editor/Viewports/` |
| **Phase 4** | **`GeometrySpace::PolygonNetwork` & Native UV Workspace (`FrontierUV` + `FrontierEditor`)** | 🟢 Complete | `Engine/GeometrySpace/` · `Engine/Interchange/` · `Engine/Workspaces/UV/` · `Engine/Lifecycle/` · `Exhibits/` |
| **Phase 5** | **Simulation, Audio & Networking Spaces** | 🏳️ Planned | `Engine/SimulationSpace/` · `Engine/AudioSpace/` · `Engine/NetworkingSpace/` |

---

## Phase 0 — Governance, Dependencies & Build Architecture (🟢 Complete)

- ✔️ Canonical layout established in [FolderStructure.md](FolderStructure.md).
- ✔️ Enforced naming blacklist & rulings established in [BannedWordsVocabulary.md](BannedWordsVocabulary.md).
- ✔️ Agentic skill protocols imported and harmonized in `Documentation/AgenticInstructions/`.
- ✔️ Modern C++20 modular coding standards established in [CodingStandards.md](CodingStandards.md).
- ✔️ Third-party packages (`imgui`, `thorvg`, `tomlpp`, `glfw`, `vulkan-headers`, `volk`, `stb`, `JoltPhysics`), `.slang` compiler (`slangc`), `SwiftShader`, and OFL font repositories pinned and verified via `Automation/`.
- ✔️ All 8 ImGui patches (`PatchA`–`PatchH`) applied and verified against `ExternalPackages/imgui`.
- ✔️ Three-profile build architecture (`Editor` / `Development` / `Shipping`) wired in `CMakeLists.txt` and `Engine/Configuration/BuildConfiguration.h`.

---

## Phase 1 — Input Foundation & Cross-Platform Windowing (🟢 Groundwork Active)

Starting focus agreed with user: **`Input/` + `Editor/` groundwork**.

1. **`Engine/Configuration/BuildConfiguration.h`**:
   - Compile-time mutually exclusive `FRONTIER_PROFILE_EDITOR`, `FRONTIER_PROFILE_DEVELOPMENT`, and `FRONTIER_PROFILE_SHIPPING` policy with `static_assert` validation and `constexpr` booleans.
2. **`Engine/Input/InputExchange.{h,cpp}` & `Engine/Input/InputExtension.{h,cpp}`**:
   - Platform-neutral C++20 input signal packets covering keyboard scancodes, pointer motion/buttons/scroll, gamepad axes/buttons, and multi-touch/pen contact points (supporting Windows, Linux, and Android).
   - Bi-directional fixed-capacity deterministic exchange (`InputExchange`) covering both OS→Engine signal intake (`InputSignalSink`) and Engine→OS cursor confinement & haptic rumble directives (`InputFeedbackTarget`) with zero heap churn per tick.
   - Lifecycle coordination via `InputExtension` (`Initialize` → `TickExchange` / `FlushOutboundDirectives` → `Finalize`).
3. **`Engine/Windowing/PlatformWindow.h`, `PlatformWindowDesktop.cpp` & `PlatformWindowStub.cpp`**:
   - Composable `WindowAttributeMask` (`EnableBorderDecoration`, `EnableInteractiveResize`, `AssignMonitorCenter`, `EnableTopmostOrder`, `EnableFocusMinimize`, `EnableDpiScaling`, `EnableAlphaCompositing`, `EnableExternalDrop`, `EnableInitialVisibility`, `AssignInitialFocus`), `WindowPresentationMode` (`SplashBorderless`, `Windowed`, `MaximizedWindowed`, `BorderlessFullscreen`, `ExclusiveFullscreen`), application presets (`ConstructSplashWindow`, `ConstructApplicationWindow`, `ConstructGameWindow`, `ConstructDetachedPanelWindow`), and dynamic runtime transitions restoring saved windowed bounds across GLFW 3.4 (`Win32`, `X11`, `Wayland`, `GlfwNull`) and `HeadlessPlatformWindow`.
4. **`Engine/Components/` & `Engine/Configuration/{InputSettings,ProjectSpaceRepository}.{h,cpp}`**:
   - Complete native C++20 UI component library: `PixelSpace`, `VectorIconRepository`, `ThemeRepository`, `TypographyRepository` (`EngineContent/FontsArchives/FontRepository.FontSpace`), `SpringIntegrator`, `PanelExpansionTab` (`"Frontier"` pull-down notch), `ControlAppearance` (`OptionalTooltip`), `ControlTooltip` (light/dark, 4-direction speech tails, and split-header diagnostic tooltips with icons, metrics, and suggestion buttons), `ScalarSlider`, `ExclusiveSelector`, `CapsuleButton`, `DropdownSelector`, `TextInputField`, `ThemePreviewTile`, `NotificationSequence`, `ConfirmationDialogue` (`Rounded` & `SemanticHeader`), and `ControlCentrePanel`.
   - Deterministic `Engine/Lifecycle/` subsystem: `LifecycleSequence.h` (`Editor` · `Development` · `Shipping`), `RuntimeLifecycle.{h,cpp}` (`Development` · `Shipping` Game), `WorkspaceLifecycle.{h,cpp}` (`Editor` · `Development`), and `EditorLifecycle.{h,cpp}` (`FRONTIER_PROFILE_EDITOR` only).
   - Generic reusable workspace panes (`Engine/Components/Panes/`): `WorkspaceAppearance` (`VisualToken`, `VectorGraphic`, `ConstructSlotCategory`), `OutlinerPanel` (supporting both `Outliner` and `LayerStack` with empty initial scene, `+` generic construct dropdown, header settings dropdown with `Import...`, `Visible`/`Hidden` counters, `Search` + `Filter` chips, Showcase folders, nested tree attachment, drag order repositioning, mouse-wheel scroll, `Shift` multi-select, right-click directive sheet, and 5-slot footer), `InspectorPanel` (exact header font sizing, Showcase Folder Inspector, and expandable `+ Add notes` / `Notes` box), and `ViewportPanel` (empty initial scene, `Edit` / `Simulate` / `Play`-`Pause` strip, `Construct` sheet, `Split Viewport`, `Diagnostics` sparkline overlay, `Viewport Settings`, and footer).
   - Sharp unrounded trapezoidal docking (`WorkspaceTabStrip` with `0.0f` corner rounding + `WorkspaceDockContainer`) over `Outliner`/`LayerStack`, `Viewport`, and `Inspector` panes (zero Content Browser).
   - Atomic 100% `PascalCase` `.ProjectSpace` persistence (`['.visualappearance']`, `['.FontSpace']`, `['.workspacelayout']`, `['.inputsettings']`, `['.notificationsettings']`; Option 2A case-insensitive load and zero `.toml` configuration/font files).
5. **Validation & Visual Snapshots (`Exhibits/{Input,Window,Docking,ControlCentre,WorkspacePanes,Lifecycle,WorkspaceRevision}Validation.cpp` + `Exhibits/Images/`)**:
   - Deterministic CTest verification across `Editor`, `Development`, and `Shipping` profiles with 28 CPU-rasterized `.png` snapshots emitted under `Exhibits/Images/{Input,Windowing,Docking,ControlCentre,WorkspacePanes,Lifecycle,Workspaces}/`.
6. **HTML-Only Reference Editor UIs & Studio Prototypes (`Experimental/`)**:
   - Staged HTML/JS `ProjectZeroEditor` (with embedded render in `Experimental/ProjectZeroEditor/README.md`), HTML/Vite `FrontierEditor` (`arena/f3437631-frontier`), and 8 domain studios (`FluidEditor`, `FractureEditor`, `ParticleEditor`, `PhotometricEditor`, `ForceFieldEditor`, `VehicleLab`, `UVEditor`, `TextureStudio`) under `Experimental/` (zero C++).

---

## Phase 2 — Workspaces, `.Space` Codecs, Parametric Revision & `WorkspaceThread` (🟢 Active)

1. **`Engine/Constants/`**:
   - ✔️ `MathConstants.h`, `PhysicsConstants.h`, `EngineConstants.h` (`64B` container alignment, `215 MiB` per-document revision cap, `750 MiB` global revision cap), and `TimeConstants.h`.
2. **`Engine/Threading/` (Option A — Priority-Budgeted `ExecutionChannels` + `WorkspaceThread` Only)**:
   - ✔️ `Sync/`: `SpinLock.h`, `AtomicQueue.h` (bounded Vyukov MPMC ring), `CriticalSection.h`, `CompletionCounter.h` (non-blocking continuation trigger), and `ThreadProgressionTracker.{h,cpp}` (`64B` cache-isolated liveness slots).
   - ✔️ `Tasks/`: `TaskQueue.h`, `TaskChannel.{h,cpp}`, `ExecutionChannels.{h,cpp}` (cooperative `AssistUntilComplete` preventing dependent-task sleep hangs), and `TaskScheduler.{h,cpp}` (`ImmediateCritical` -> `DeterministicTick` -> `WorkspaceAuthoring` -> remaining-budget `OpportunisticSlack`).
   - ✔️ `Threads/Workspace/WorkspaceThread.{h,cpp}`: Dedicated asynchronous workspace authoring & `.Space` codec thread publishing lock-free triple-buffered snapshots (`GameThread` and other domain threads deferred for dedicated design).
3. **`Engine/Codecs/` (Two-Tier Hybrid `.Space` Containers — Option 1A + Option 2A)**:
   - ✔️ `SpaceContainerCodec.{h,cpp}` (`SpaceContainerEncoder` + `SpaceContainerDecoder`: canonical PascalCase `toml++` Tier 1 manifest + `64B`-aligned Tier 2 binary sections + 64-bit FNV-1a checksums; rejects `.toml`, `.UVChart`, and `.Surface`), `RevisionCodec.{h,cpp}`, `ProjectCodec.{h,cpp}`, `ColourCodec.{h,cpp}`, `FontCodec.{h,cpp}`, `UVCodec.{h,cpp}` (`.UDIM` + `.UVTile`), `ImageCodec.{h,cpp}`, `TextureCodec.{h,cpp}`, `MaterialCodec.{h,cpp}` (`DirectionalLitShading.slang`), and `VectorCodec.{h,cpp}` (geometry codec deferred).
4. **`Engine/Workspaces/` (`215 MiB` / `750 MiB` Resolution-Independent Revision & Session Coordination)**:
   - ✔️ `Revision/`: `ContentRegion.{h,cpp}`, `NonLinearHistory.{h,cpp}` (`EnclosingIndex`, `FirstNestedIndex`, `NextPeerIndex`), `OperationRecorder.{h,cpp}` (`Redo Purge -> Prefix Fold`), `CursorResolution.{h,cpp}` (arbitrary-resolution replay), `RevisionTracker.{h,cpp}`, and `RevisionExtension.{h,cpp}` (`750 MiB` global ceiling & cross-tab sequence undo/redo).
   - ✔️ Session & Exchange: `WorkspaceToken.h`, `WorkspaceDocument.{h,cpp}`, `WorkspaceRegistry.{h,cpp}`, `WorkspaceExchange.{h,cpp}`, and `WorkspaceExtension.{h,cpp}` wired to `WorkspaceThread` and verified across `Editor`, `Development`, and `Shipping` profiles (`7/7` CTest suites passing).
5. **Phase 3 & Phase 4 — `PolygonNetwork`, Vulkan `.slang` Compute/Raster, Slate `TransformGizmo` & Native UV Workspace (🟢 Complete)**:
   - ✔️ `Engine/Configuration/{EngineConfiguration,WorkspaceConfiguration}.{h,cpp}` & `Engine/Mathematics/AxisCoordinates.h` (`HostLaunchMode::FeatureSet` / `StandaloneWorkspace` / `HeadlessBatch`, `--standalone=uv`, `Axis3D`/`Axis2D`, `Scalar3D`/`Scalar2D` aliases, separate `UVAxes`, default-off `SeamOverlayEnabled = false`).
   - ✔️ `Engine/GeometrySpace/`: Half-edge `PolygonNetwork` (`Vertex`, `DirectedEdge`, `Face`), non-kinship `GeometryInstanceRegistry`, O(1) `FacePartitionRegister` (`FacePartitionSpan` cached look-up in `0.005 us`), and 5 multi-part studio presets (`ControlledIntegrator`, `AeroCoupeGT`, `TacticalCrate`, `ArmoredBust`, `UDIMArchKit`).
   - ✔️ `Engine/GeometricRaster/` & `Engine/Editor/Viewports/{TransformGizmo,ViewportProjection}.{h,cpp}`: 9 `.slang` graphics & compute shaders compiled to SPIR-V (`-fvk-use-entrypoint-name`, including `UVDistortionInspection.slang`), Vulkan 1.2 Timeline Semaphore `VulkanInterchange` + `TimelineTransferRegister` (zero `VkFence` stalls), `ViewportPickQuery` (`R32G32B32A32_UINT` 1x1 + GPU marquee/brush reduction), 16-slot procedural MatCap + Cook-Torrance `UVWorkspaceRasterizer`, reusable `ViewportProjection` bound to `WorkspaceDocument`, and 3D/2D Slate `TransformGizmo` (`Ctrl` snap, constant scale handle length).
   - ✔️ `Engine/Components/Panes/ViewportProjectionPane.{h,cpp}`, `Engine/Interchange/GeometryInterchange.{h,cpp}`, `Engine/Lifecycle/{DesktopWorkspaceLaunch,FrontierUVLaunch,FrontierEditorLaunch}` & `Engine/Workspaces/UV/`:
     - Runnable desktop executables `FrontierUV` (`HostLaunchMode::StandaloneWorkspace`) and `FrontierEditor` (`HostLaunchMode::FeatureSet` wired through `EditorLifecycle::AttachUVWorkspaceHost`) backed by `ImGuiOverlayRasterization` + `ImGuiOverlayRasterization.slang`, `ControlAppearance` theme synchronization, and the `ControlCentrePanel` top notch.
     - Exact pixel-for-pixel UI reproduction of `Experimental/UVEditor/` across `UVOutlinerPanel` (`UV Space`), `UVViewportHost` (dual/single 2D+3D canvas, floating 11-icon tool rail, bottom `Sample Assets` drawer), and `UVInspectorPanel` (`Pack`, `Unwrap`, `UDIM`, `Shading` with 16 procedural MatCap orbs, and `Check`), defaulting to the `Controlled Integrator` vehicle (`EngineContent/Geometry/ControlledIntegrator.GeometrySpace`, `12,572` vertices, `25,068` faces).
     - Interactive 2D/3D `UVViewportInteractor` (MMB/Alt+LMB orbit & pan, cursor-centered wheel zoom, `F` fit view, GPU `R32G32B32A32_UINT` hover/click/marquee/brush picking, live 2D/3D `TransformGizmo` drag with `Ctrl` snapping, and modal `G`/`R`/`S` + `U`/`V` axis lock + numeric entry).
     - Complete `Engine/GeometrySpace/Selection/` (`ComponentSelectionMask` & `GeometrySelection` edge loops, edge rings, Dijkstra geodesic shortest path, face loops, coplanar flood, island flood, checker & random) and `UVAuthoringOperations` (mark/clear/weld/brush seams, `EnablePinLock` with crimson-gold GPU beacons, similarity-transform stitch `V`, split island `Y`, UV mirror/align, and quad rectification).
     - Full parameterization & layout suite: pin-aware parallel LSCM (`ConformalUnwrapSolver`), ABF++, Smart UV Project, Box/Cylindrical/Spherical/CameraView projections, Follow Active Quads (`UVProjectionSolvers`), pin-aware GPU `.slang` `ProgressiveStretchRelaxer`, `ConcaveSkylinePacker` (`ConcaveSkylineCompute.slang`), Stack Similar / Unstack Overlaps / Fill Empty Pockets (`UVIslandLayoutOperations`), and `TexelDensityEqualizer`.
     - Wavefront `.obj` and Khronos glTF 2.0 `.gltf` multi-UDIM import/export (`GeometryInterchange`), `.UVSpace` hybrid codec, OS file-drop intake (`UVWorkspaceHost::IntakeExternalFileDrop`), layer create/rename/duplicate/delete, per-UDIM resolution & procedural backdrops (`ColorGrid`, `TrimSheetAtlas`), and `215 MiB` `RevisionTracker` Undo/Redo — verified with **0 warnings** and **100% CTest pass rate (`15/15` Editor, `15/15` Development, `13/13` Shipping = `43/43` runs)**.
6. **Remaining Domain Threads & Simulation Spaces (🚩 Next)**:
   - Design `GameThread` and remaining Option A domain threads before implementation.
   - Convert remaining `Experimental/` domain studios (`TextureStudio`, `FluidEditor`, `FractureEditor`, `ParticleEditor`, `VehicleLab`) onto the shared `PolygonNetwork` + Vulkan `.slang` + `TransformGizmo` infrastructure.

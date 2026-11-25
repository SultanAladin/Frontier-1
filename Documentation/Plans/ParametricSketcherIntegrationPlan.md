# Parametric Sketcher Integration Plan (SolidArc → Frontier-1)

Source reference: `unassignedinbox/Frontier-OLD` (`arena/255c3b21-frontier` @ `30728d450f6a97e4d4e7b7f4a8f0016e3fa1ebc8`).
Target branch: `SultanAladin/Frontier-1` (`arena/39c6910b-frontier-1`).

## 1. Naming & Identity Mapping
- Rebrand all `SolidArc` symbols, filenames, namespaces, shaders, and UI labels to **Parametric Sketcher** / **Sketcher**:
  - Kernel namespace: `Frontier::GeometrySpace::Parametric`
  - Workspace namespace: `Frontier::Workspaces` (`SketcherWorkspaceHost`, `SketcherWorkspaceSession`, `SketcherOutlinerPanel`, `SketcherViewportHost`, `SketcherInspectorPanel`)
  - Document extension: `.sketcher` / `.sketcher.toml` (with import compatibility for `.step`, `.stl`, `.svg`, `.dxf`)
  - Standalone launcher: `Engine/Lifecycle/FrontierSketcherLaunch.cpp` → `FrontierSketcher` (`--workspace=sketcher`, `--sketcher`, `WorkspaceCategory::GeometryCad`)

## 2. Folder Structure & Architecture Mapping
1. **Parametric B-Rep & 2D Sketch Kernel (`Engine/GeometrySpace/Parametric/`)**
   - Port all pure ISO C++20 modules from upstream `Engine/GeometryProcessing/`:
     - `Precision/`, `Diagnostics/`, `Geometry/`, `Containment/`, `Tessellation/`, `Boolean/`, `Intersection/`, `Topology/`, `Construction/`, `Blend/`, `Local/`, `Sketch/`, `Feature/`, `Surfacing/`, `Measurement/`, `Drawing/`, `Exchange/`, `Audit/`, `Assembly/`, `Inspection/`
   - Add bridge utilities converting `ModelPartition` tessellations into `Frontier::GeometrySpace::PolygonNetwork` for ProjectBrowser thumbnails and engine interchange.

2. **Vulkan Rendering & Shaders (`Engine/GeometricRaster/Sketcher/` + `Engine/Shaders/Sketcher/`)**
   - Replace upstream `SolidArcDevice` (`VkInstance`/`VkDevice`) with Frontier-1's shared `GeometricRaster::VulkanInterchange`.
   - Replace Node/wasm `SolidArcShaderArchive.h` generation with `.slang` units compiled by `CMakeLists.txt` (`frontier_compile_slang_shader_unit`) into `CompiledShaders/*.spv`:
     - `SketcherProjection.slang`, `SketcherSurface.slang`, `MatCapLookup.slang`, `SketcherEdge.slang`, `CadStroke.slang`, `SketcherGrid.slang`, `SketcherImagePlane.slang`, `SketcherGizmo.slang`.
   - Implement `SketcherRasterizer` with offscreen RGBA (+ MSAA resolve), `R32_UINT` identity picking buffer, depth buffer, 1–4 split camera panes, MatCap `2D_ARRAY`, Zebra stripes, X-Ray ghosting, Section capping, and CPU-picked transform gizmo.

3. **Document, Session & Dockable UI (`Engine/Workspaces/ParametricSketcher/`)**
   - Document & exchange layer: `SketcherDocument`, `SketcherDocumentCodec`, `SketcherScene`, `SketcherImagePlane`, `SketcherMatcap`, `SketcherMeasurement`, `SketcherDrawingSheet`, `SketcherExchange`, `SketcherKnife`.
   - Session & panels using Frontier-1's `PixelSpace`, `VectorIconRepository`, `WorkspaceAppearance`, `ContentBrowserPanel`, and `ControlCentrePanel`.
   - Implement `SketcherWorkspaceHost::RegisterDockPanes(Components::HeadlessWorkspaceDockContainer&)` with **no fixed non-dockable top strip**:
     - Left (`SplitLeft`): **`Outliner`**
     - Center (`CenterTabGroup`): **`Viewport`** (active default), **`Drawing Sheet`**, **`Construct`**
     - Right (`SplitRight`): **`Inspector`**

4. **Launcher, Browser & Validation (`Engine/Lifecycle/`, `Engine/Editor/Explorer/`, `Exhibits/ParametricSketcher/`)**
   - Register `ActiveHostSurface::ParametricSketcherSurface` + `SketcherDock` in `DesktopWorkspaceLaunch.cpp`, add `FrontierSketcherLaunch.cpp`, wire `.sketcher` previews in `ProjectBrowser`, and include `FrontierSketcher` in CI workflows.
   - Add CTest validation targets: `SketcherKernelValidation`, `SketcherDocumentValidation`, `SketcherCurveWorkflowValidation`, `SketcherViewportValidation`, `SketcherWorkspaceUIValidation`.

## 3. Phased Implementation
- **Phase 1**: Parametric B-Rep & 2D Sketch Kernel (`Engine/GeometrySpace/Parametric/`) + `SketcherKernelValidation` across Editor/Development/Shipping.
- **Phase 2**: Document, Codec, Drawing Sheet, Exchange, MatCap & ImagePlane + `SketcherDocumentValidation`.
- **Phase 3**: Slang Shaders & `SketcherRasterizer` on `VulkanInterchange` + `SketcherViewportValidation`.
- **Phase 4**: Dockable UI (`Outliner`, `Viewport`, `Drawing Sheet`, `Construct`, `Inspector`), `DesktopWorkspaceLaunch.cpp`, `FrontierSketcher`, `ProjectBrowser`, `SketcherWorkspaceUIValidation`, and CI packaging updates.

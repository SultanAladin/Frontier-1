# Native VectorGraphics Workspace (Pattern & Stamp Studio) — GPU ThorVG + Vulkan `.slang` Architecture Plan

> **Scope:** Implementation blueprint for transforming the **Pattern & Stamp Studio (`PatternStamp*` / `WorkspaceCategory::Vector`)** from UI-only chrome into a full **GPU-rendered VectorGraphics Viewport**, powered by **ThorVG 1.0.0** (SVG DOM + TTF/OTF typography + Bezier path extraction) feeding a native **Vulkan 1.3 + `.slang` GPU Vector Rasterizer**, wired to **`InputExtension`** (UI + hotkeys + external drops), **`TransformGizmo` (`Planar2D`)**, and **`ContentBrowser`** (Project saving + live drag-and-drop for Colours, SVG code/presets/projects, and Fonts).
>
> **Status:** Implemented & Verified — Phases 1–4 Complete (ThorVG 1.0.0 + `stb_truetype` CFF + 7 Vulkan `.slang` Compute/Graphics Shaders + `Planar2D` Gizmo + `VectorViewportInteractor` + `.VectorSpace` Codec + `ContentBrowser` Multi-Type Drag-and-Drop + Live `FrontierEditor --workspace=pattern|stamp`).

---

## 1. Current Repository Baseline vs. Missing Pieces

| Subsystem | What Exists Today in `Frontier` | What Must Be Wired / Built |
| :--- | :--- | :--- |
| **Pattern & Stamp Panes** | `Engine/Workspaces/Texturing/PatternStamp{StackSession,LayerStackPane,ToolHeaderPane,InspectorPane,WorkspaceHost}.{h,cpp}` implement the 3-column dock (`Layers`, `Pattern`/`Stamp`, `Inspector`), 18 motif primitives, 8 modifier specs, masks, 22 PBR channels, and undo/redo UI chrome. | Replace the deferred placeholder box in `PatternStampToolHeaderPane` with a live **`ViewportProjectionPane`** backed by **`VectorCanvasRasterizer`** (GPU ThorVG + Vulkan `.slang`), plus drag-drop hitboxes and live interactive vector authoring. |
| **ThorVG `1.0.0` Pin** | `ExternalPackages/thorvg/` (`1.0.0`, `6715f99ac1`) includes `loaders/svg`, `loaders/ttf`, `sw_engine`, `gl_engine`, `wg_engine`, and abstract `tvg::RenderMethod` (`src/renderer/tvgRender.h`). `Automation/ThorVGSoftware.cmake` exists but is **not yet included in `CMakeLists.txt`** and currently excludes `loaders/ttf`. | Include a clean `Automation/ThorVGVector.cmake` target in `CMakeLists.txt` enabling `loaders/svg` + `loaders/ttf` + `src/renderer` (`RenderMethod` SPI), while keeping **zero CPU pixel rasterization (`SwCanvas` unused for viewport rendering)**. |
| **UV Workspace GPU Pattern** | `Engine/Workspaces/UV/UV{WorkspaceHost,ViewportHost,ViewportInteractor}` + `Engine/Components/Panes/ViewportProjectionPane` + `Engine/GeometricRaster/UV/UVWorkspaceRasterizer` + `Engine/Editor/Viewports/TransformGizmo` (`Planar2D`) + `Engine/GeometricRaster/Picking/ViewportPickQuery` (`R32G32B32A32_UINT`). | Mirror this exact modular architecture under **`Engine/GeometricRaster/Vector/`** + **`Engine/Shaders/VectorGraphics/`** + **`VectorViewportInteractor`**. |
| **`InputExtension`** | `Engine/Input/InputExtension.{h,cpp}` + `EngineContent/Input/FrontierStudio.InputSpace` route keyboard chords, pointer clicks/scrolls, and OS file drops (`ConsumeDroppedFilePaths()`). | Route all tool shortcuts, modal `G`/`R`/`S` transforms, Bezier knot editing, pan/zoom, clipboard SVG paste (`Ctrl+V`), and OS drag-drop intake cleanly through `InputExtension`. |
| **Project Browser (`ContentBrowser`)** | `ContentBrowserPanel.{h,cpp}` + `ThumbnailSheetRepository.{h,cpp}` already permit `Colours`, `Vectors` (`.svg`, `.VectorSpace`), `Fonts` (`.ttf`, `.otf`, `.FontSpace`), `Images`, and `Materials` in `WorkspaceCategory::Vector`. Today only `DraggingColour` and `DraggingMatCap` exist as live drag payloads. | Generalize `ContentBrowserPanel` drag payloads to also drag **`ContentCategory::Vectors` (SVG / `.VectorSpace`)** and **`ContentCategory::Fonts` (`.FontSpace` / `.ttf` / `.otf`)**, add project-save thumbnail baking into `ThumbnailSheet`, and unlock external `.svg` / SVG snippet / font drops. |

---

## 2. How ThorVG + 100% GPU Rendering Work Together on Vulkan 1.3

### 2.1 Honest Constraint Analysis of ThorVG `1.0.0` Backends

Inside `ExternalPackages/thorvg/src/renderer/`:
- `sw_engine` (`tvg::SwCanvas`) rasterizes scanlines on CPU — **not GPU**.
- `gl_engine` (`tvg::GlCanvas`) requires OpenGL / OpenGL ES — **absent in Frontier** (`VulkanInterchange` is strict Vulkan 1.2/1.3 + `.slang` SPIR-V + SwiftShader fallback).
- `wg_engine` (`tvg::WgCanvas`) requires `wgpu_native` — **absent in Frontier**.

However, ThorVG cleanly separates **Vector Parsing & Scene Graph Assembly** (`tvg::Picture`, `tvg::Shape`, `tvg::Text`, `tvg::Scene`, `loaders/svg`, `loaders/ttf`) from **Raster Execution** via its internal **`tvg::RenderMethod` SPI (`src/renderer/tvgRender.h`)** and tessellation/stroking utilities (`src/renderer/gl_engine/tvgGlTessellator.{h,cpp}` / `wg_engine/tvgWgTessellator.{h,cpp}`).

### 2.2 Selected Architecture: Symbolic ThorVG Frontend + Native Vulkan `.slang` GPU Renderer

> **Core Rule:** ThorVG runs strictly on the symbolic vector graph (SVG XML/CSS parsing, TTF glyph contour loading, `MoveTo`/`LineTo`/`CubicTo` path flattening, stroke expansion, fill gradients, clip/mask hierarchy). **Every visual pixel, fill, gradient, procedural modifier, mask, 2×2 ghost tile, curve handle, grid/ruler overlay, PBR channel synthesis, anti-aliasing, and 2D `TransformGizmo` executes 100% on the GPU via Vulkan 1.3 `.slang` pipelines.**

```text
  [1] SYMBOLIC VECTOR GRAPH (ThorVG 1.0.0 — Zero CPU Pixel Drawing)
      • tvg::Picture::load(svg)       ──► SVG DOM / CSS / <defs> / Transforms / Clips
      • tvg::Text (TTF/OTF Loader)    ──► Typeface glyph RenderPath contours
      • PatternStampLayerSpec Shapes  ──► Procedural 18 motifs + Pen/Curve cubic knots
                     │
                     ▼  (VulkanVectorRenderMethod : public tvg::RenderMethod)
  [2] GPU RESOURCE STREAMING (VulkanInterchange + TimelineTransferRegister)
      • Flattened/tessellated triangle & cubic Bezier curve SSBOs
      • VectorLayerGPURecord[] (Transform, Blend, Opacity, Fill/Stroke stops, Mask, PBR)
      • VectorModifierGPURecord[] (Grid/Hex, Radial, Mirror, Echo, Noise, DomainWarp, Wear)
      • PointGradientPin[] (up to 12 RGB+Position+Radius anchors per layer/ground)
                     │
                     ▼  (100% GPU — Vulkan Compute + Dynamic/MRT Raster .slang)
  [3] GPU VECTOR PIPELINE (VectorCanvasRasterizer — Hardware Vulkan or SwiftShader CI)
      ├─► Pass A (Compute): VectorModifierExpand.slang   ──► GPU instance & warp expansion
      ├─► Pass B (Compute): VectorCurveBake.slang        ──► Analytic SDF / Loop-Blinn / Winding fill + stroke coverage
      ├─► Pass C (Raster MRT): VectorCompositeRasterization.slang
      │     • Color Target (RGBA16F / RGBA8): Ground + 2×2 Ghost Tiles + Pattern/Stamp Layers + PointField + PBR Lit Preview
      │     • Pick Target  (R32G32B32A32_UINT): R=DocSlot, G=LayerIdentity, B=SubPathOrKnotIdx, A=GizmoHandleOrdinal
      ├─► Pass D (Raster MRT): VectorGuideOverlayRasterization.slang (Grid, Axes, Rulers, Pen/Curve Knots & Tangents, Point-Gradient Pins)
      └─► Pass E (Raster MRT): GizmoRasterization.slang (Existing TransformGizmo in Planar2D mode)
                     │
                     ▼
  [4] VIEWPORT PRESENTATION & PICKING
      • Zero-copy Vulkan ImTextureData blit into ViewportProjectionPane (Planar2DOnly)
      • 1×1 async ViewportPickQuery::PickPixel() for instant layer, knot, pin & gizmo picking
```

### 2.3 Why Every Visual Requirement Lives on GPU (Including 22 PBR Channels & Modifiers)

- **Linear, Radial & 12-Point Field Gradients (`VectorCompositeRasterization.slang`):** Evaluated per-pixel via GPU weighted radial inverse-distance falloff (`PointField`), so dragging any Point-Gradient pin on the canvas updates at `> 240 FPS` on GPU without baking textures on CPU.
- **Non-Destructive Modifiers (`VectorModifierExpand.slang`):** `GridArray` (`Grid`/`Brick`/`Hex`/`Half-Drop`), `RadialArray`, `MirrorSymmetry`, `InsetEcho`, `ScatterDropout`, `NoiseScale` (`FBM`/`Perlin`/`Worley`/`Ridged`), `DomainWarp` (`Wave`/`Swirl`/`Curl`/`Pinch`), and `BevelEdgeWear` run in parallel GPU compute workgroups, writing expanded `VectorInstanceRecord[]` transforms + colour/scale modulations straight into VRAM indirect draw/dispatch buffers and feeding the exact KPI readbacks (`InstancesEvaluated`, `MinScaleReading`, `MaxScaleReading`, `SurfaceFractionPct`) via `TimelineTransferRegister`.
- **Seamless `2×2` Tiling & `StackedPbrTile` / 22-Channel View Modes:**
  - `VectorSVG`: Crisp anti-aliased vector canvas + subtle `2×2` ghost border repeats in Pattern mode (or alpha checkerboard in Stamp mode).
  - `Seamless2x2`: GPU `fract()` seamless wrap preview across a `2×2` grid.
  - `StackedPbrTile` & `Albedo`..`SubsurfTint`: `VectorPbrSynthesisCompute.slang` resolves height bevels (`DistanceFromContour`), Sobel/Scharr tangent normals, hemisphere AO, roughness, metalness, clearcoat, sheen, iridescence, and transmission into mipmapped GPU surfaces, then shades `StackedPbrTile` with Frontier's directional + IBL BRDF in `VectorSurfacePreviewRasterization.slang`.
- **GPU Thumbnail Generation for `ContentBrowser`:** On save (`Ctrl+S` / `Save to Project`), a `128×128` offscreen dispatch of the exact GPU pipeline populates `ThumbnailSheetRepository` directly without any duplicate CPU software thumbnailizer.

---

## 3. Similar Architecture to `UV` Workspace + Shared 2D `TransformGizmo`

Mirroring `Engine/Workspaces/UV/` cleanly separates concerns across four classes:

| `UV` Reference Class | Parallel `Vector` Class | Responsibility |
| :--- | :--- | :--- |
| `UVWorkspaceRasterizer.{h,cpp}` | **`VectorCanvasRasterizer.{h,cpp}`** (`Engine/GeometricRaster/Vector/`) | Owns ThorVG `VulkanVectorRenderMethod`, Vulkan SSBOs, `.slang` compute/MRT pipelines, `128×128` thumbnail bake, and `ViewportPickQuery` (`Pick2DPixel`, `Reduce2DRectangle`). |
| `UVViewportInteractor.{h,cpp}` | **`VectorViewportInteractor.{h,cpp}`** (`Engine/Workspaces/Texturing/`) | Drives 2D camera pan/zoom, layer hit-picking, Bezier knot/handle editing, Point-Gradient pin dragging, shape click-drag creation, `Planar2D` `TransformGizmo` interaction, and modal `G`/`R`/`S`. |
| `UVViewportHost.{h,cpp}` | **`VectorViewportHost.{h,cpp}`** (`Engine/Workspaces/Texturing/`, evolving `PatternStampToolHeaderPane`) | Combines Band-1/Band-2 headers, View Channel selector, snap/zoom pills, and mounts **`ViewportProjectionPane`** (`ViewportSplitMode::Planar2DOnly`) with drop-target highlight overlay. |
| `UVWorkspaceHost.{h,cpp}` | **`PatternStampWorkspaceHost.{h,cpp}`** | Registers 3 docked panes (`Layers` `0.21f` \| `Pattern`/`Stamp` `0.54f` \| `Inspector` `0.25f`), binds `ContentBrowser`, wires `InputExtension`, and handles OS / browser drops. |

### 3.1 Reusing Modular `TransformGizmo` in `GizmoDimensionMode::Planar2D`

`Engine/Editor/Viewports/TransformGizmo.{h,cpp}` + `GizmoRasterization.slang` already implement `GizmoDimensionMode::Planar2D` with:
- **`TranslateX` (`+U/+X`, Red Cone `#E01414`)** & **`TranslateY` (`+V/+Y`, Green Cone `#12D40A`)**
- **`ScaleX` / `ScaleY` (Coaxial Scale Cylinders)**
- **`PlaneXY` (Translucent Yellow UV Free-Move Quad `#E0CD12`)**
- **`RotateZ` (31° Blue Planar Rotation Arc `#1560E0`)**
- **`CenterRing` (White Screen-Plane Ring `#FFFFFF`)**
- **Built-in `Ctrl` Snap & Axis Constraints:** `TransformGizmo::EvaluateDragInteraction` respects `GizmoSnapSettings` (bound to `Session->SnapStepPx` for position, `5°` rotation increments, `0.05` scale steps) and streams 48-byte `GizmoGPUVertex` buffers into `GizmoRasterization.slang` during `VectorCanvasRasterizer` MRT Pass E so `PickIDs.A` returns `GizmoHandleOrdinal` directly in `< 0.02 ms`.
- **Dual Manipulator Sync:** Moving/scaling/rotating via `Planar2D` `TransformGizmo` updates `Layer->CenterX`, `CenterY`, `WidthPx`, `HeightPx`, `RotationDeg`, scales nested `VectorPath` knots/handles when editing in Object Space, triggers instant GPU uniform/SSBO re-upload, and commits an undo step to `OperationRecorder` / `PushUndoSnapshot()` on pointer release.
- **8-Point Bounds Frame + Bezier Anchor Editing:** When `ActiveTool == EditPoints`, `Planar2D` `TransformGizmo` switches focus onto the active selected Bezier knot (`TransformOnly` or `Universal` on tangent handles) while GPU overlay Pass D draws corner/smooth knots (`#E8C27A` / `#4ADE80`) and tangent handles (`#7DD3FC`) into `PickIDs` (`B = KnotIndex`, `A = HandleTag`).

---

## 4. `InputExtension` Integration (UI, Tools, Modal Transforms & Hotkeys)

All pointer, keyboard, modifier, and OS file-drop events route through **`InputExtension::InspectActiveEditorSnapshot()`** and **`EngineContent/Input/FrontierStudio.InputSpace`** (zero raw GLFW key polling):

### 4.1 Complete Hotkey & Gesture Matrix (`VectorViewportInteractor`)

| Input Chord / Gesture | Scope | Behavior |
| :--- | :--- | :--- |
| **`V`** | Select Tool | Activates `PatternStampToolCategory::Select` + `Planar2D` `TransformGizmo`. |
| **`P` / `C` / `A` / `G` / `T` / `H`** | Creation & Edit Tools | `P` Pen (click corner, drag cubic Bezier), `C` Curve (smooth Catmull/auto spline), `A` Edit Points (sub-select knots/tangents, double-click toggles Smooth/Corner), `G` Point Gradient (add/drag up to 12 gradient pins), `T` Text Block, `H` Hand Pan. |
| **`R` / `O` / `D` / `Y` / `S` / `X` / `L`** | Shape Primitives | `R` Square, `O` Circle, `D` Diamond, `Y` Triangle, `S` Star, `X` Hexagon, `L` Stripe; `Shift+Drag` constrains `1:1` aspect, `Alt+Drag` draws from center. |
| **Modal `M` / `R` / `S` (`Transform` Grab/Rotate/Scale)** | Selected Layer *(when Select active)* | Mirrors `UVViewportInteractor` modal transform: `X`/`Y` locks axis, `Ctrl` snaps to `SnapStepPx`, typed digits set exact pixels/degrees, `Enter`/`Click` commits, `Esc`/`Right-Click` cancels. |
| **`Space+Drag` / `Middle-Drag` / `Wheel`** | Viewport Camera | Pans `CanvasOriginPx` and performs cursor-anchored smooth zoom (`0.15x .. 12.0x`), `F` fits document `100%` centered, `Ctrl+0` resets zoom. |
| **`Ctrl+Z` / `Ctrl+Shift+Z` (`Ctrl+Y`)** | Revision History | Step undo/redo across both `PatternStampDocumentSnapshot` and GPU `VectorCanvasRasterizer` dirty state. |
| **`Ctrl+D` / `Delete` / `Enter`** | Layer & Path Edit | `Ctrl+D` duplicates active layer; `Delete` deletes active Bezier knot/Point-Gradient pin or active layer; `Enter` closes/commits active `Pen`/`Curve` draw session. |
| **`Ctrl+V` / `Ctrl+S`** | Clipboard & Project Save | `Ctrl+V` inspects OS clipboard text — if valid `<svg ...>` XML or SVG `d="..."` path string, parses instantly via ThorVG and inserts onto the canvas; `Ctrl+S` saves `.VectorSpace` project + `128×128` GPU thumbnail into Project Browser. |
| **OS External Drops (`ConsumeDroppedFilePaths`)** | `InputExtension` -> `ContentBrowser` | `.svg`, `.VectorSpace`, `.ttf`/`.otf`/`.FontSpace`, `.ColourSwatch`/`.ColourSpace` flow through `ContentBrowser::IntakeExternalAssetDrop` into quarantine & active drop handler. |

---

## 5. Project Browser (`ContentBrowser`) Saving & Full Multi-Type Drag-and-Drop

### 5.1 Saving Authored Pattern & Stamp Assets into the Project Browser

1. **Extend `Engine/Codecs/VectorCodec.{h,cpp}` (`.VectorSpace`):**
   - Preserve existing `.StrokeAppearance`, `.PathCurve`, and 64B-aligned Bezier knots while adding explicit `[VectorWorkspace]` attributes (`WorkspaceProfile = "Pattern" | "Stamp"`, `ResolutionPx`, `Ground`, `ShowGhostTileBorders`), `[[VectorLayers]]` (ordered hierarchy, `MotifShape`, `Typographic` font asset path + kerning/size, `FillSource`, `FillStops`, `PointGradientPins`, `Stroke`, `FinishPreset`, `PbrChannels`, `Mask`, `Modifiers`), and optional cached `.SvgXml` section for rapid SVG fidelity round-tripping.
2. **`Save to Project Browser` Workflow (`Save` menu & `Ctrl+S`):**
   - Encodes `<Project>/Content/Vectors/<Slug>.VectorSpace` via `VectorEncoder::EncodeToFile`.
   - Captures the `128×128` GPU render from `VectorCanvasRasterizer` and registers/updates the tile inside `ThumbnailSheetRepository` (`ContentCategory::Vectors`, `ContentOriginFilter::Project`), so saved Patterns and Stamps appear immediately in the `Vectors` tab of `ContentBrowserPanel` with crisp vector thumbnails.
   - `Export SVG` writes standalone `<svg xmlns="http://www.w3.org/2000/svg">` via ThorVG / document serializer; `Export PBR Textures` dispatches GPU mip generation into `TextureCodec` (`.TextureSpace`) / `.png` files under `<Project>/Content/Textures/`.

### 5.2 Generalizing `ContentBrowserPanel` Drag-and-Drop Payloads

Currently `ContentBrowserPanel` (`Engine/Components/ContentBrowser/ContentBrowserPanel.{h,cpp}`) tracks `DraggingColour` and `DraggingMatCap`. Extend that exact mechanism with a generic **`ContentBrowserDragSession`** supporting drop zones registered by workspaces (`VectorDropSlotRegistration`):

| Dragged Asset Source | Supported Drop Targets | Exact Runtime Action |
| :--- | :--- | :--- |
| **1. Colours**<br>*(`.ColourSwatch`, `.ColourSpace` from `Colours` rail)* | **a) Inspector Colour Swatches** (`Fill Tint`, `Stroke Tint`, `Tint B`, `Ground Tint A/B`, `PBR Colour`, `Noise Tint B`)<br>**b) Layer Row in `Layers` Pane**<br>**c) Direct Drop on GPU Viewport Canvas** | **a)** Assigns target slot + pushes Undo.<br>**b)** Assigns `FillTint` (`Shift`-drop assigns `StrokeTint`).<br>**c)** Performs **`ViewportPickQuery::Pick2DPixel(x, y)`**: if hit on Point-Gradient pin -> tints that pin; if hit on layer geometry -> tints that layer (`Shift`-drop = stroke); if dropped on empty canvas -> sets `Document.GroundTintA`. |
| **2. Existing SVG Code / SVG Presets / Saved Projects**<br>*(Clipboard `Ctrl+V` SVG `<svg>`/`<path d=...>`, OS `.svg`/`.VectorSpace` file drop, or `ContentCategory::Vectors` drag from `ContentBrowser`)* | **a) Direct Drop on GPU Viewport Canvas**<br>**b) Drop onto `Layers` Stack Divider / Folder**<br>**c) Double-Click in Browser (or `Alt`-Drop on Canvas)** | **a)** Parses SVG DOM via **ThorVG `tvg::Picture::load`** + `tvg::Accessor` scene walk into structured `PatternStampLayerSpec` entries *(or decodes `.VectorSpace` via `VectorDecoder`)* and places them centered at drop `(U, V)` coordinates!<br>**b)** Inserts imported SVG/stamp layers cleanly at the exact target row/folder depth in `PatternStampLayerStackPane`.<br>**c)** Replaces/loads the full project document (preserving existing Undo snapshot so `Ctrl+Z` recovers prior work!). |
| **3. Fonts**<br>*(Engine `Archivo`, `DMSans`, `Inter`, `JetBrainsMono`, `SpaceGrotesk` or Project/Imported `.ttf`/`.otf`/`.FontSpace` in `ContentCategory::Fonts`)* | **a) Inspector Font Family Box** *(when `Typographic` layer active)*<br>**b) Any `Typographic` Layer Row in `Layers` Pane**<br>**c) Direct Drop on GPU Viewport Canvas** | **a/b)** Assigns `Layer.FontFilePath` & `Layer.TypefaceTitle`, loads glyphs through **ThorVG TTF Loader (`tvg::Text`)**, rebuilds GPU Bezier/triangle buffers instantaneously, and flashes drop highlight.<br>**c)** Uses `ViewportPickQuery`: if dropped over an existing `Typographic` layer -> updates its font in place; if dropped over empty canvas or non-text shape -> creates a new `Typographic` layer (`"FRONTIER"`) centered at drop `(U, V)` using that font! |
| **4. Materials (Bonus Parity with JS Reference)**<br>*(`.MaterialSpace` in `ContentCategory::Materials`)* | **Layer Row** or **Canvas Shape Hit** | Copies PBR look (`Albedo`, `Roughness`, `Metalness`, `Clearcoat`, `Sheen`, `Iridescence`, `Anisotropy`, `Transmission`, `Emissive`) onto the hit/selected layer's surface channels. |

---

## 6. Architectural & Governance Guarantees

- **Naming Compliance (`Documentation/BannedWordsVocabulary.md`):** Zero barred words (`mesh`, `map`, `slice`, `flag`, `state`, `value`, `kind`, `item`, `frame`, `pill`, `popover`, `palette`, `menu`, `card`, `badge`, `draw`, `helper`, `stage`, `pass`, `flow`, `artwork`, `atlas`, `backend`, `handler`, `manager`, `base`, `data`, `info`, `node`). Vector curve control points use **`VectorPathKnot` / `BezierKnot`**, drag payloads use **`ContentDragCarrier`**, GPU stages use **`…Sequence` / `…Rasterization` / `…Compute`**, and overlay chrome uses **`…Sheet` / `…Container`**.
- **All-GPU Visual Discipline:** No CPU per-pixel canvas rasterizers (`tvg::SwCanvas` and CPU `PixelSpace` shape loops are excluded from viewport surface pixels). Even headless `Exhibits/PatternStampWorkspaceUIValidation.cpp` executes the exact **Vulkan 1.3 `.slang` pipeline via SwiftShader ICD** to capture `1600×980` golden PNGs!
- **Profile Discipline (`Editor`, `Development`, `Shipping`):** `VectorCodec` (`.VectorSpace`), `FontCodec` (`.FontSpace`), and ThorVG symbolic loaders remain clean under `Engine/Codecs/` & `Engine/VectorSpace/`; interactive `PatternStamp*` editor panes, `TransformGizmo`, and `ContentBrowser` remain cleanly gated under `FRONTIER_EDITOR_ENABLED`.

---

## 7. Phased Implementation Sequence & Verification Gates

| Phase | Target Modules | Verification Gate (`Exhibits/` + CTest) |
| :---: | :--- | :--- |
| **Phase 1: ThorVG + `VulkanVectorRenderMethod` Core** | `Automation/ThorVGVector.cmake` (`loaders/svg`, `loaders/ttf`, `RenderMethod`), `Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.{h,cpp}`, `VectorCanvasRasterizer.{h,cpp}`, initial `.slang` shaders (`VectorModifierExpand`, `VectorCurveBake`, `VectorCompositeRasterization`, `VectorGuideOverlayRasterization`). | New `VectorGraphicsGPUValidation` CTest: verifies SVG XML parsing via ThorVG, `.ttf`/`.otf` glyph outline extraction from `EngineContent/FontsArchives/`, GPU curve + linear/radial/12-point gradient rendering on SwiftShader Vulkan, and `R32G32B32A32_UINT` MRT pick hit accuracy. |
| **Phase 2: GPU Modifiers, Masks & 22-Channel PBR Synthesis** | Expand `VectorModifierExpand.slang` + `VectorPbrSynthesisCompute.slang` + `VectorSurfacePreviewRasterization.slang` covering all 18 motifs, 8 modifiers (`GridArray`..`BevelEdgeWear`), 3 mask categories, and all 22 PBR channel views + `StackedPbrTile` + `Seamless2x2`. | Headless GPU golden PNG checks verifying `Seamless2x2` edge wrap continuity (`delta == 0` across tile borders), `StackedPbrTile` lighting, `NoiseScale`/`DomainWarp`/`RadialArray` instance KPI readback, and `< 2 ms` frame times on Vulkan. |
| **Phase 3: `VectorViewportInteractor` + `Planar2D` `TransformGizmo` + `InputExtension`** | `VectorViewportHost.{h,cpp}`, `VectorViewportInteractor.{h,cpp}`, `ViewportProjectionPane` integration inside `PatternStampWorkspaceHost`, Pen/Curve Bezier knot editing, Point-Gradient pin manipulation, modal `G`/`R`/`S`, and `InputExtension` hotkeys (`FrontierStudio.InputSpace`). | Interactive simulation CTest feeding synthetic `InputSignalPacket`s (pan/zoom, Pen cubic creation, `TransformGizmo` drag with `Ctrl` snap, `EditPoints` tangent handle drag, `Ctrl+V` SVG paste) verifying geometry transforms and MRT `PickIDs` capture. |
| **Phase 4: `VectorCodec` Project Save + Multi-Type `ContentBrowser` Drag-and-Drop** | Extended `VectorCodec` (`.VectorSpace` Layer/Modifier/SVG round-trip), GPU `128×128` thumbnail injection into `ThumbnailSheetRepository`, generalized `ContentBrowserPanel` drag carriers for `Colours`, `Vectors` (`SVG`/`.VectorSpace`), `Fonts` (`.ttf`/`.otf`/`.FontSpace`), and `Materials`, wired into `PatternStampWorkspaceHost`. | Round-trip `.VectorSpace` save/load test + `ContentBrowser` drag-drop CTest dropping: (1) Colour swatch onto GPU canvas layer & Inspector slot; (2) SVG snippet & `.VectorSpace` asset onto canvas & layer stack; (3) `Inter`/`JetBrainsMono` font onto `Typographic` layer & empty canvas — updating updated `Exhibits/Images/Texturing/05..08*.png` with live GPU renders! |

---

## 8. Branch Audit (`c7egoist/Frontier` `arena/36d6f048-frontier`), Remediation & Merge Verification

Before merging `c7egoist/Frontier` (`0d29117`) into `arena/429d7dc7-frontier`, a full line-by-line architectural, shader, UI, and vocabulary audit was executed against `Experimental/TextureStudio` (`PatternWorkspace.js`, `StampWorkspace.js`, `ArtworkStackWorkspace.js`, `StarterCatalogBridge.js`), `CLAUDE.md`, and `Documentation/BannedWordsVocabulary.md`:

| Audit Finding | Root Cause in `0d29117` | Remediation Applied Before Merge |
| :--- | :--- | :--- |
| **1. Workspace Scope Hygiene & Stray Artifacts** | `0d29117` introduced non-Pattern/Stamp `Material`/`Baking`/`TexturePaint` placeholder panes plus root `Experimental Remastered/` and `01a1631b-*.patch`. | Synced `Experimental/TextureStudio/` with the updated JS ground truth and removed `Experimental Remastered/`, root `.patch`, and all out-of-scope `Material*`, `Baking*`, and `TexturePaint*` stub files so `Engine/Workspaces/Texturing/` is 100% dedicated to **Pattern Studio** and **Stamp Studio**. |
| **2. Duplicate CPU/GPU Guide Overlays** | Both `VectorGuideOverlay.slang` (GPU) and `PatternStampToolHeaderPane::RenderVectorGuideOverlays` (CPU `ImDrawList`) drew tile borders, symmetry axes, Bezier knots, and gradient pins simultaneously, causing visible double-lines. | Deleted CPU `RenderVectorGuideOverlays` completely; all tile borders, symmetry axes, Bezier knots/tangent handles, gradient pins, and marquee boxes render exclusively through GPU `VectorGuideOverlay.slang`. |
| **3. Rotated Bezier Handle & Marquee Space Mismatch** | `VectorCanvasRasterizer` uploaded unrotated local-space `KnotPositions` / `KnotInHandles` / `KnotOutHandles` and raw document-space `MarqueeRectPx` to `VectorGuideOverlay.slang`, while `VectorViewportInteractor` applied unrotated pointer deltas when `Sel->RotationDeg != 0`. | Rotated Bezier knot positions and tangent handles by `Sel->RotationDeg` around `(CenterX, CenterY)` during GPU upload, inverse-rotated pointer coordinates by `-Sel->RotationDeg` during `DraggingKnotControl`, and transformed `MarqueeRectPx` into screen pixels via `TileLayout`. |
| **4. GPU Render-Pass Thrashing (`2,160` Passes/Frame)** | `SyncAndReopenLayerTargets()` ended and restarted the `VkRenderPass` (`LOAD` + `STORE`) between stencil, cover, and stroke draws for every ghost-tile copy (`9 × 120 × 2` passes on Hex Matrix). | Consolidated stencil, cover, and stroke draws into a **single `VkRenderPass` per layer** using intra-subpass `vkCmdPipelineBarrier` synchronization (`2.8 s` total 3-frame headless desktop capture on software Vulkan). |
| **5. Static Viewport Rulers (`0..512`)** | `ConstructCanvasRulerChrome` hardcoded `0..512` across `CanvasRect` instead of tracking live `CanvasZoom` and `CanvasPanX/Y`. | Computed ruler tick coordinates via `VectorCanvasRasterizer::ComputeTileLayoutMetrics` with adaptive step intervals (`16 / 32 / 64 / 128 / 256 / 512`) synchronized pixel-for-pixel with GPU tile borders under pan and zoom. |
| **6. Starter Presets, Band 1 `"Presets ▾"` & Browser Shelf** | Only 3 presets per mode existed (`StarterCatalogBridge.js` defines 6 Pattern + 6 Stamp presets), Band 1 lacked a `"Presets ▾"` selector, and `ContentBrowser` `Vectors` shelf did not surface starter presets. | Expanded `PatternStampStackSession` to **6 Pattern presets** and **6 Stamp presets**, added Band 1 `"Presets ▾"` dropdown (`PresetsSheetOpen`), and populated `ContentBrowser` `Vectors` shelf with `128×128` GPU-rendered thumbnails (`preset://pattern/<idx>` and `preset://stamp/<idx>`) supporting drag-drop and double-click loading. |
| **7. Hotkey Collisions & Bridge Actions** | `VectorViewportInteractor` mapped `R` to both Rounded-Square and Modal Rotate, `S` to both Star and Modal Scale, and `X` to Hexagon instead of Clear Canvas; `"Tile on Material"` / `"Use in Paint"` / `"Use Stamp"` had no bridge dispatch. | Aligned single-key shortcuts (`U` Rounded Square, `F` Star, `T` Text, `G` Gradient, `X` Clear Canvas, `Z` Zoom) with `InspectToolDescriptors()`, wired `ExecuteCanvasCommandBridge`, and registered `PatternWorkspace` + `StampWorkspace` in `WorkspaceCapabilityRegister`, `ProjectBrowser`, and `DesktopWorkspaceLaunch` (`--workspace=pattern|stamp` and `.VectorSpace` routing). |
| **8. `BannedWordsVocabulary.md` Sweep (`0` Violations)** | `VectorCommon.slang` used banned `Common` in its filename, and shader/C++ files contained banned stems (`Flags`, `Pass`, `Stage`, `State`, `Node`, `Palette`, `FontAssetPath`, `RasterizeInternal`, `Is*`/`Has*`/`Can*`). | Renamed `VectorCommon.slang` -> `VectorSharedLayouts.slang` and refactored every identifier across `.slang`, `Engine/Workspaces/Texturing/`, `Engine/GeometricRaster/Vector/`, `Engine/Codecs/VectorCodec.{h,cpp}`, and `Exhibits/` (**0 banned identifiers, 0 boolean prefix violations**). |

### Three-Profile Build & CTest Verification Results

- **`Editor` Profile (`build/Editor`)**: **30 / 30 CTests Passed (`100%`)** — including `PatternStampWorkspaceUIValidation`, `VectorGraphicsGPUValidation`, `ProjectBrowserValidation`, `VectorFieldValidation`, `FrontierEditorExecutableSmoke`, `FrontierUVExecutableSmoke`, `FrontierPatternExecutableSmoke`, and `FrontierStampExecutableSmoke`.
- **`Development` Profile (`build/Development`)**: **27 / 27 CTests Passed (`100%`)**.
- **`Shipping` Profile (`build/Shipping`)**: **26 / 26 CTests Passed (`100%`)**.


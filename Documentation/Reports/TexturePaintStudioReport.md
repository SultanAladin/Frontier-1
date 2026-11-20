# Frontier Texture Studio, Brush Studio & Material Studio — Complete Panels & Tool Context Menus Engineering Report

**Status:** Complete & Verified (`TexturePaint`, `Pattern`, `Stamp`, `Baking`, and `Material` C++ Workspaces)  
**Branch:** `arena/20fc22da-frontier`  
**Validation Suites:** `TexturePaintWorkspaceUIValidation`, `PatternStampWorkspaceUIValidation`, `BakingWorkspaceUIValidation`, `MaterialWorkspaceUIValidation`, `ProjectBrowserValidation` (All 0 errors)

---

## 1. Executive Summary

This report documents the complete C++ implementation and visual verification of **all docked workspace panels** (Outliners, 3D/2D Viewports, Project Zero Inspector Cards, Side-by-Side Multi-Open Channel Cards, and Outliner Action Footers) and **all per-tool context menus** (`Brush Studio`, `Stamp Studio`, `Eraser Studio`, `Fill Studio`, `Gradient Studio`, `Text Studio`, `Materials / 22-Channel Stack`, `Shapes Studio`, `Pattern Gradient Studio`, and `Curve & Pen Studio`) across all five authoring workspaces:

1. **Texture Paint Studio (`FrontierTexturePaint`)**:
   - **3-Column Dock**: Left `Layers` outliner (`2/2 Layers`, `4/22 Channels`, `Utah Teapot 4 parts · 512 px document` tree, 8-button footer action bar with `+` Add Layer 8-kind popover) | Center `Texture Paint` 2-band viewport (`3D SURFACE VIEWPORT` pill, 3D Utah Teapot / Shaderball / Chamfer Box / UV Sphere / Torus Knot PBR raycaster, 3D Gradient Point gizmos with prominent active point ring, 2D UV map split view, bottom tool/size readout) | Right `Layer Inspector` Project Zero card (`Opacity`, `Channels` 5-segment group meter, `Mask` tiles, `Gradient points` collapsible editor with `+ Add point` / `Remove point` and side-by-side `Radius` & `Strength` sliders, and 5-group 22-channel stack).
   - **All Per-Tool `Brush Studio` Context Menus**:
     - **`Brush Studio · PAINT`**: 12 brush presets (`Round Studio #2`, `Rigger Liner #0`, `Filbert Soft #8`, `Flat Shader #10`, `Bright Stiff #14`, `Fan Blender #6`, `Hog Bristle Flat #12`, `Sumi Ink Wash`, `Charcoal Stick`, `Chisel Marker`, `Airbrush Soft`, `Splatter Texture`) with S-curve stroke previews and 12 left-hand categories (`Library`, `Stroke path`, `Stabilization`, `Taper`, `Shape`, `Grain`, `Rendering`, `Wet mix`, `Colour dynamics`, `Dynamics`, `Stylus`, `Properties`, `Materials`, `About`).
     - **`Stamp Studio · STAMP`**: 16 procedural decal shapes in a 4×4 grid (`Round`, `Hard round`, `Square`, `Flat nib`, `Chalk`, `Graphite`, `Bristle`, `Splatter`, `Spray`, `Leaf`, `Ink blot`, `Cloud`, `Star`, `Sparkle`, `Hexagon`, `Knife`) + `Decal`, `Transform`, `Look`, `Material`.
     - **`Eraser Studio · ERASE`**: `Tip` (`Size`, `Strength`, `Flow`, `Hardness` sliders + `Soft | Hard | Grain` edge selector), `Feel`, `Channels`, `About erase`.
     - **`Fill Studio · FILL`**: `Source` (`Paint with: Colour | Grain`, wide `#E8C27A` swatch bar, `Opacity` slider, `Scope: Asset part | UV island | Whole mesh`), `Material`, `About fill`.
     - **`Gradient Studio · GRADIENT`**: 10 multi-stop ramp presets (`Ember Glow`, `Oceanic Mint`, `Iridescent Oil`, `Sunset Neon`, `Oxidized Bronze`, `Earth & Indigo`, `Arctic Aurora`, `Volcanic Magma`, `Gold Foil Patina`, `Cyber Berry`) + live `Stops` bar and swatch chips.
     - **`Text Studio · TEXT`**: 6 typeface cards (`DM Sans`, `Serif`, `Mono`, `Condensed`, `Display`, `Script`) with `Aa Frontier` specimen wells + `Type` (`Weight: 300 | 400 | 600 | 800`, `Italic`, `Align: Left | Centre | Right`, `Font size`).
     - **`Brush Studio · Materials / Channels`**: Shared 5-group 22-channel OpenPBR stack inside the Brush Studio modal.
2. **Pattern & Stamp Studios (`FrontierPattern`, `FrontierStamp`)**:
   - **Left `Layers` Outliner (`cards-pattern.png` & `outliner-footer-pattern.png` 1:1 parity)**: `VISIBLE` & `MODIFIERS` borderless KPI tiles, `+ Motif | + Text | + Gradient` quick chips, hierarchical `Document -> Layer -> Modifiers / Surface -> Starters` tree with `SEAMLESS` / `CUTOUT` badges and `#F0A43A` amber selection bar, 8-button action bar (`+ | Folder | ^ | v | Copy | Eye | Lock ... Trash`), and 4-column telemetry footer (`LAYERS | INSTANCES | COVERAGE | SEAM / MARGIN`).
   - **Right `Pattern Studio` / `Stamp Studio` Inspector (`cards-pattern.png`, `rows-side-by-side.png`, `fill-linear-scene.png`, `fill-point-field.png` 1:1 parity)**: Document hero + preset dropdown pill, 3 KPI metric tiles (`Instances`, `Coverage`, `Palette`), 7 vertical collapsible layer property rows (`Shape`, `Fill`, `Outline`, `Placement`, `Blend`, `Surface`, `Mask`) that expand **vertically in-place** so `Fill` (`Solid | Linear | Radial | Point`) and `Outline` can remain open simultaneously, `Modifiers` list (`Noise Scale`, `Grid / Hex Array`), `Document` fold bar, and `.svg | Project | Copy` footer.
   - **Single-Column `.atm-menu` Tool Popovers (`menu-shapes.png`, `menu-gradient.png`, `ArtworkToolMenu.js` 1:1 parity)**: `Shapes · CIRCLE` (4×5 17-shape grid + `FILL` + `OUTLINE`), `Gradient · GRADIENT` (`TARGET: Fill | Outline | Ground`, `SOURCE: Linear | Radial | Point field`, `STOPS`, `Reset placement`), and `Path · CURVE` (`CURVE: Smoothing`, `LOOK: Outline width`, colour chip, `Fill closed paths`).
3. **Baking Studio (`FrontierBaking`)**:
   - **Left `Queue` Outliner (`cards-bake.png` 1:1 parity)**: `QUEUED 5/18` & `UNBAKED 0/5` KPI tiles, `PBR Core | PBR + Volume | Normals | All | None` preset chips, `Utah Teapot` study root + `Normals`, `Surface`, `Masks & IDs`, `Volume & Distance` groups with 24×24 shaded 3D sphere map icons, action bar, and 4-column telemetry footer (`OUTPUT 4.2 MB | RAYS 6.7 M | ETA 141 ms | DEVICE GPU`).
   - **Right `Map Baking · HIGH -> LOW CAGE` Inspector (`cards-bake.png` 1:1 parity)**: `STANDARD STUDY / 01` hero + study dropdown, side-by-side `Low (Study mesh)` & `High (Study mesh)` cards, white `|> Bake 5 maps` pill + `Auto | GPU | CPU` segmented bar + progress card, `Open map` `#222327` raised card (`Tangent Normal`, `OpenGL +Y | DirectX -Y`, side-by-side `Cage front` & `Cage rear` sliders), and `Output & cage` fold bar.
4. **Material Studio (`FrontierMaterial`)**:
   - **Full 3-Column Dock (`Outliner | Material | Material Inspector`, `cards-material.png` & `outliner-footer-material.png` 1:1 parity)**:
     - **Left `Outliner` (`Material Scene & Strata`)**: `STRATA 1/1` & `DRIVEN 0/28` KPI tiles, `+ Detail stratum | Scuffs | Camo | Cells | Grain` quick chips, `Scene -> Surface body -> Core / Specular / Coat / Sheen / Thin film / Transmission` hierarchy with procedural 24×24 channel icons, action bar, and 4-column telemetry footer (`F0 2.8% | CLASS Hybrid | GLOSS Mirror | ALBEDO 5%`).
     - **Center `Material` Viewport**: Vulkan 1.3 Direct Studio OpenPBR raycaster + `ContentBrowser` dynamic material shelf.
     - **Right `Material Inspector` (`LOOK-DEV PBR`)**: Material hero + preset pill, 3 KPI tiles (`Response 0.028 F0`, `Albedo 5%`, `Signal 0/28`), and 6-group 28-channel vertical stack with **in-place multi-open `Colour` + `Roughness` cards** inside `CORE` (`Value | Field | Ramp | Image` segmented bars, `#090C11 sRGB`, `0.08` pill slider, `× Remove`) and right-aligned mini swatch chips on collapsed groups.

---

## 2. Texture Paint Studio — Panels & Per-Tool Context Menu Proofs

### 2.1 Main 3-Column Workspace (`12-TexturePaintWorkspaceMain.png`)
![Texture Paint Workspace Main](../../Exhibits/Images/Texturing/12-TexturePaintWorkspaceMain.png)

### 2.2 `Brush Studio · PAINT` — 12-Preset Brush Library (`13-TexturePaintBrushInspector.png`)
![Brush Studio Paint Library](../../Exhibits/Images/Texturing/13-TexturePaintBrushInspector.png)

### 2.3 `Stamp Studio · STAMP` — 16 Procedural Decal Shapes Grid (`16-TexturePaintStampStudio.png`)
![Stamp Studio Context Menu](../../Exhibits/Images/Texturing/16-TexturePaintStampStudio.png)

### 2.4 `Eraser Studio · ERASE` — Tip Sliders & `Soft | Hard | Grain` Edge (`17-TexturePaintEraserStudio.png`)
![Eraser Studio Context Menu](../../Exhibits/Images/Texturing/17-TexturePaintEraserStudio.png)

### 2.5 `Fill Studio · FILL` — Source `Colour | Grain`, Swatch & Scope (`18-TexturePaintFillStudio.png`)
![Fill Studio Context Menu](../../Exhibits/Images/Texturing/18-TexturePaintFillStudio.png)

### 2.6 `Gradient Studio · GRADIENT` — 10 Multi-Stop Ramp Presets & Stops Bar (`19-TexturePaintGradientStudio.png`)
![Gradient Studio Context Menu](../../Exhibits/Images/Texturing/19-TexturePaintGradientStudio.png)

### 2.7 `Text Studio · TEXT` & 3D + 2D UV Split Viewport (`15-TexturePaintSplitUvAndCurves.png`)
![Text Studio Context Menu & Split UV](../../Exhibits/Images/Texturing/15-TexturePaintSplitUvAndCurves.png)

### 2.8 `Brush Studio · Materials` — Shared 5-Group 22-Channel Stack (`20-TexturePaintBrushChannels.png`)
![Brush Studio Materials Channel Stack](../../Exhibits/Images/Texturing/20-TexturePaintBrushChannels.png)

### 2.9 3D Gradient Layer, Viewport Point Ring & Outliner `+` Add Layer Menu (`14-TexturePaintAddLayerAndMask.png`)
![Texture Paint 3D Gradient & Add Layer Menu](../../Exhibits/Images/Texturing/14-TexturePaintAddLayerAndMask.png)

---

## 3. Pattern & Stamp Studios — Side-by-Side Channels, Viewport Fills & Tool Context Menu Proofs

### 3.1 `Shapes · Circle` Tool Context Menu & Side-by-Side Open Channel Cards (`21-PatternShapesStudioMenu.png`)
![Pattern Shapes Studio Menu](../../Exhibits/Images/Texturing/21-PatternShapesStudioMenu.png)

### 3.2 `Gradient · Point Gradient` Tool Context Menu & Point Field Inspector (`22-PatternGradientStudioMenu.png`)
![Pattern Gradient Studio Menu](../../Exhibits/Images/Texturing/22-PatternGradientStudioMenu.png)

### 3.3 Viewport-Placed Linear Gradient Handles & Side-by-Side Channel Editors (`23-PatternLinearSceneFill.png`)
![Pattern Linear Scene Fill](../../Exhibits/Images/Texturing/23-PatternLinearSceneFill.png)

### 3.4 `Curve & Pen · Bezier Path` & `Text · TEXT` Tool Context Menus (`24-PatternCurvePenStudioMenu.png`, `25-PatternTextStudioMenu.png`)
![Pattern Curve & Pen Studio Menu](../../Exhibits/Images/Texturing/24-PatternCurvePenStudioMenu.png)
![Pattern Text Studio Menu](../../Exhibits/Images/Texturing/25-PatternTextStudioMenu.png)

### 3.5 Pattern Studio Main & Stamp Studio Main (`05-PatternWorkspaceMain.png`, `07-StampWorkspaceMain.png`)
![Pattern Studio Main](../../Exhibits/Images/Texturing/05-PatternWorkspaceMain.png)
![Stamp Studio Main](../../Exhibits/Images/Texturing/07-StampWorkspaceMain.png)

---

## 4. Baking Studio — `Queue` Outliner & `Map Baking (HIGH -> LOW CAGE)` Inspector Proof

### 4.1 3-Column Baking Studio (`09-BakingWorkspaceMain.png`)
![Baking Studio Main](../../Exhibits/Images/Texturing/09-BakingWorkspaceMain.png)

---

## 5. Material Studio — 3-Column `Outliner | Material | Material Inspector` Proofs

### 5.1 Full 3-Column Material Studio (`Outliner 28 Channels` + `OpenPBR 1.1 Inspector` with Side-by-Side `Colour` & `Roughness`) (`CppMaterialWorkspace_Viewport_Volcanic.png`)
![Material Studio 3-Column Volcanic](../../Exhibits/Images/Materials/CppMaterialWorkspace_Viewport_Volcanic.png)

### 5.2 3-Column Material Studio + Open `ContentBrowser` Dynamic Preset Shelf (`CppMaterialWorkspace_ViewportBrowser_Shield.png`)
![Material Studio 3-Column with ContentBrowser](../../Exhibits/Images/Materials/CppMaterialWorkspace_ViewportBrowser_Shield.png)

---

## 6. Build & Validation Summary

| Target | Exit Code | Verification Result |
| :--- | :---: | :--- |
| `TexturePaintWorkspaceUIValidation` | `0` | `[TexturePaintValidation] All Texture Paint workspace checks and 9 PNG exhibits succeeded.` |
| `PatternStampWorkspaceUIValidation` | `0` | `[PatternStampWorkspaceUIValidation] ALL CHECKS PASSED` (including Exhibits 05..08 and 21..24) |
| `BakingWorkspaceUIValidation` | `0` | `[BakingWorkspaceUIValidation] ALL CHECKS PASSED` |
| `MaterialWorkspaceUIValidation` | `0` | `[MaterialWorkspaceUIValidation] ALL CHECKS PASSED` (3-column `Outliner \| Material \| Material Inspector` + 13 Dynamic Material thumbnails) |
| `ProjectBrowserValidation --smoke-test` | `0` | `[PASS] ProjectBrowserValidation verified WorkspaceCapabilityRegister, ProjectGenerator, ContentBrowser close & VulkanWorldRenderSequence GPU compute.` |
| `FrontierTexturePaint --smoke-test` | `0` | Clean startup, 3-column dock presentation, and shutdown |
| `FrontierBaking --smoke-test` | `0` | Clean startup, 3-column dock presentation, and shutdown |
| `FrontierPattern --smoke-test` | `0` | Clean startup, 3-column dock presentation, and shutdown |
| `FrontierStamp --smoke-test` | `0` | Clean startup, 3-column dock presentation, and shutdown |
| `FrontierMaterial --smoke-test` | `0` | Clean startup, 3-column dock presentation, and shutdown |

---

## 7. PBR System Completeness Audit — What Is Complete vs. What Still Needs Work

### 7.1 What Is 100% Complete & Verified in C++ / Vulkan Slang

| Subsystem | Source Files | Status & Capabilities |
| :--- | :--- | :--- |
| **1. Core 10-Lobe OpenPBR 1.1 BSDF (CPU & GPU Slang)** | `Engine/MaterialSpace/SurfaceMaterial.h`<br>`Engine/MaterialSpace/Scattering/OpenPbrSurfaceScattering.h`<br>`Engine/Shaders/MaterialSpace/OpenPbrSurfaceScattering.slang` | **Complete (100%)** — Full reciprocal evaluation, VNDF/cosine mixture importance sampling, and White-Furnace energy conservation (`max(R+T) <= 1.0`) across all 10 physical lobes: **L1** Portsmouth 2025 Energy-Preserving Oren-Nayar diffuse, **L2** Anisotropic VNDF GGX + F0/F82 edge tint + Turquin multi-scatter, **L3** Beer-Lambert absorbing Clearcoat, **L4** Belcour & Barla Airy Thin-Film iridescence, **L5** Kemppinen 4D multiscale glinty microflakes, **L6** Rough GGX dielectric transmission + Abbe Cauchy dispersion, **L7** Christensen-Burley subsurface diffusion, **L8** Zeltner LTC cloth sheen, **L9** Chiang/Marschner dual-scattering hair/fiber BCSDF, **L10** Porosity water darkening + Planckian blackbody emission. |
| **2. Bindless 128-Byte GPU Material Register** | `Engine/MaterialSpace/CompactMaterialRegister.{h,cpp}`<br>`Engine/GeometricRaster/Visibility/VisibilityMaterialResolve.{h,cpp}` | **Complete (100%)** — Byte-exact 128-byte (`32 × uint32`) half-float/unorm packing shared between C++20 and Slang with wave-uniform lobe bitmask branching. |
| **3. Dynamic Material Compiler & 13 Procedural OpenPBR Recipes** | `Engine/MaterialSpace/Dynamic/*`<br>`Engine/Shaders/MaterialSpace/DynamicMaterialResolve.slang` | **Complete (100%)** — Vulkan 1.3 Direct Studio OpenPBR compute shader evaluating all 13 procedural & animated recipes (`Volcanic Pahoehoe Magma`, `Physical Ocean Water`, `Rain-Streaked Smoked Glass`, `Wet Asphalt Rain Puddles`, `Hoarfrost Sparkle Snow`, `Liquid Chrome Mercury`, `Battle-Damaged Shield`, etc.) on Shaderball, Draped Cloth, Rounded Box, and Ocean Plane. |
| **4. Pattern & Stamp 22-Channel GPU PBR Compositor** | `Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.{h,cpp}`<br>`Engine/Shaders/Vector/VectorPbrSurfaceCompute.slang` | **Complete (100%)** — Full 22-channel RGBA16F GPU layer stack (`StackedColor`, `StackedPbrPackA..D`, `StackedPbrExtra`) with finite-difference height-to-normal generation, OpenPBR lit preview, and all 22 individual diagnostic channel views. |
| **5. High-to-Low Cage & 3D SDF GPU Map Baker** | `Engine/GeometricRaster/Baking/GpuTextureBaker.{h,cpp}`<br>`Engine/GeometricRaster/Baking/GpuSdfBaker.{h,cpp}`<br>`Engine/Shaders/Baking/*.slang` | **Complete (100%)** — Unquantized RGBA32F Vulkan compute baking pipeline for all 17 surface maps (`Tangent Normal`, `World Normal`, `Bent Normal`, `Ambient Occlusion`, `Curvature`, `Thickness`, `Height`, `Cavity`, `Position`, `UV Island ID`, etc.) + 3D Volume SDF without 8-bit staircase artifacts. |
| **6. Container Codecs (`.MaterialSpace`, `.TextureSpace`, `.VectorSpace`, `.FontSpace`)** | `Engine/Codecs/MaterialCodec.{h,cpp}`<br>`Engine/Codecs/TextureCodec.{h,cpp}`<br>`Engine/Codecs/VectorCodec.{h,cpp}` | **Complete (100%)** — Deterministic round-trip serialization of OpenPBR parameters, texture layers, vector stacks, and font metrics. |

---

### 7.2 What We Still Need To Do (Remaining PBR Work)

While the core OpenPBR 1.1 math, GPU bakers, vector compositor, and all five workspace UIs are complete, **four concrete engineering items remain** to make the end-to-end authoring pipeline 100% unified:

1. **Upgrade Texture Paint 3D Viewport from 6-Channel CPU Shading to Full 22-Channel Vulkan OpenPBR Compute (`Engine/Workspaces/Texturing/TexturePaintSession.cpp`)**:
   - **Current state**: `TexturePaintSession::RenderViewportRgba8` evaluates a fast CPU 3-light GGX + hemisphere IBL shader over the cached mesh ray hits using the primary 6 channels (`Albedo`, `Roughness`, `Metalness`, `Height`, `Normal`, `Emissive`) and supports diagnostic views `0..5`.
   - **Remaining work**: Dispatch a Vulkan compute pass (reusing `OpenPbrSurfaceScattering.slang` / `VectorPbrSurfaceCompute.slang`) for the 3D painted mesh and wire all 22 paint channels (`Clearcoat`, `Coat Roughness`, `Sheen`, `Sheen Roughness`, `Anisotropy`, `Iridescence`, `Transmission`, `Subsurface`, `Specular Tint`, `Opacity`, etc.) into both the 3D lit surface and all 22 diagnostic channel views.
2. **GPU Rasterization of 2D/3D Brush Strokes & Projection (`TexturePaintSession.cpp`)**:
   - **Current state**: Brush stroke dabs, 3D Euclidean gradients, stamps, and procedural fills are composited on the CPU in `EvaluateLayerStackAtSurfacePoint`.
   - **Remaining work**: Move stroke dab splatting and layer-stack blending (`Normal`, `Multiply`, `Screen`, `Overlay`, `HeightBlend`) into a Vulkan compute ping-pong atlas at full `1024px`–`4096px` resolution so high-resolution painting with dozens of strokes runs at zero CPU cost.
3. **Live Cross-Workspace PBR Asset Bridge (`Baking` / `Pattern` / `Stamp` -> `Texture Paint` -> `Material Studio`)**:
   - **Current state**: The header bridge buttons (`Tile on Material`, `Use in Paint`, `Bake 5 maps`) and disk codecs exist, but workspaces currently operate on independent sessions unless exported/imported.
   - **Remaining work**: Wire live in-memory texture handoff so:
     - Baking a mesh pair in `Baking Studio` automatically publishes the baked `Curvature`, `Ambient Occlusion`, `Thickness`, and `Tangent Normal` maps into `Texture Paint` and `Material Studio` for smart masks (edge wear, cavity dirt, subsurface thickness).
     - Clicking `Use in Paint` in `Pattern` / `Stamp Studio` immediately binds the 22-channel vector output as a live Fill/Stamp source in `Texture Paint`.
4. **Arbitrary Per-Channel `Field` / `Ramp` / `Image` Evaluation & Multi-Stratum Blending in `Material Studio` (`MaterialInspectorPane.cpp` -> `DynamicMaterialResolve.slang`)**:
   - **Current state**: The Material Inspector UI exposes `Value | Field | Ramp | Image` tabs on every OpenPBR channel and `+ Detail stratum | Scuffs | Camo | Cells | Grain` in the Outliner, while the GPU shader (`DynamicMaterialResolve.slang`) evaluates fixed procedural graphs per preset (`0..12`).
   - **Remaining work**: Generalize `GpuDynamicParameterBlock` so any of the 28 OpenPBR channels can independently switch between constant `Value`, procedural `Field` (Perlin/Worley/Voronoi/Gabor), multi-stop `Ramp`, or sampled `Image` texture, and blend multiple material strata (`Surface body` + `Scuffs` / `Camo` / `Cells` / `Grain` detail strata) on the GPU.


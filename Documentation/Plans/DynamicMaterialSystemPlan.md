# Frontier Dynamic & Procedural Material Plan (Layer-Stack First — No Node Editor Required)

> **Status**: Refined Architecture & Planning Document (zero C++ code changes made — awaiting approval).  
> **Vocabulary Compliance**: Strictly audited against `Documentation/BannedWordsVocabulary.md` (including the 2026-11-15 `strata` / `stratum` and `morph` bans, plus `And`, `Core`, `Kind`, `Evaluator`, `Pass`, `Stage`, `Emission`/`Emit`, `Substrate`, etc.).  
> **Modularity Guarantee**: Every dynamic material, transition, scan, and dissolve recipe is **100% asset-agnostic and modular** — attachable to *any* static or animated 3D asset (props, architecture, characters, vehicles, terrain, or studio specimens).  
> **Live HTML Material Previews**: All 13 proposed procedural & animated OpenPBR shaders are live in `Experimental/TextureStudio/` (`:8098`, category **"Animated & VFX"**).  
> **Visual Proofs**:
> - `Exhibits/Images/Particles/TextureStudio/ProposedMaterials_Hero6_Upgraded.png`
> - `Exhibits/Images/Particles/TextureStudio/ProposedMaterials_13_Showcase.png`

---

## 1. Truthful Audit: Where Frontier's Material Engine & Material Workspace Stand Today

| Component | Current Status in Frontier | Exact Location |
| :--- | :--- | :--- |
| **10-Lobe Static OpenPBR 1.1 Physics (`L1..L10`)** | **COMPLETE** (C++20 + Slang `.slang`) — Portsmouth Oren-Nayar diffuse, anisotropic GGX + F82 conductor, tinted clearcoat, Belcour-Barla thin-film iridescence, Kemppinen 4D glinty microflakes, Abbe glass transmission, Burley SSS, Zeltner LTC sheen, Chiang fiber, porosity & Planckian blackbody | `Engine/MaterialSpace/SurfaceMaterial.{h,cpp}`, `Engine/MaterialSpace/Scattering/*.h`, `Engine/Shaders/MaterialSpace/*.slang` |
| **Compact GPU Packing & Energy Compensation** | **COMPLETE** — Compact register, surface-layer packing, bounce summary, directional albedo & sheen integrals | `Engine/MaterialSpace/CompactMaterialRegister.{h,cpp}`, `MaterialSurfaceLayer/`, `EnergyCompensation/` |
| **Static Specimen Rasterizer & Offline Channel Baker** | **COMPLETE** (CPU static thumbnails & offline textures only) | `Engine/MaterialSpace/Baking/MaterialSpecimenRasterizer.{h,cpp}`, `ProceduralChannelBaker.{h,cpp}` |
| **Material Workspace HTML Reference UI** | **COMPLETE** — Project Zero Editor layout, layer stack inspector, channel slots, PMREM shaderball turntable, 13 live animated shaders | `Experimental/TextureStudio/Workspaces/MaterialWorkspace.js`, `Experimental/ProjectZeroEditor/MaterialPanel.css` |
| **Dynamic / Procedural / Animated Material Architecture** | **NOT STARTED IN C++** (HTML prototype only) — no runtime time/event parameters, no non-destructive `MaterialLayerStack`, no live procedural fields, no generic material-to-material transition, no holographic scan, no animated water surface | **Designed in this document** |
| **C++ Material Workspace (`Engine/Workspaces/Material/`)** | **NOT STARTED** — `Engine/Workspaces/` currently contains `UV/`, `Texturing/` (Pattern/Stamp/Baking), `Particle/`, and `Revision/` | **Scheduled directly after the Dynamic Material Foundation & Preview Renderer** |

---

## 2. Design Philosophy: Unreal/Blender Power **Without** a Node Editor

Artists want the capabilities of Unreal Material Instances and Blender procedural shaders (live animated sliders, procedural noise/Voronoi/waves, physical layering, real-time asset transitions, zero-recompile tweaking) **without** having to untangle visual node spaghetti.

Frontier achieves this with a **Vertical `MaterialLayerStack` + Parametric Field + Live Driver** architecture (familiar from Substance Painter / Photoshop / Cinema 4D, backed by compiled **Slang `.slang` → SPIR-V** shaders):

1. **Vertical `MaterialLayerStack` (`SurfaceLayer` / `FinishLayer` / `CoatingLayer`)**:
   - Reads cleanly **Bottom → Top** (matching `Engine/MaterialSpace/MaterialSurfaceLayer/` and `PatternStampLayerStackPane`).
   - Every `MaterialLayerEntry` is one of four focused, modular layer types:
     1. **`FinishLayer` (Physical OpenPBR Foundation)** — Any complete 10-lobe OpenPBR 1.1 material (`Liquid Obsidian`, `Brushed Aerospace Titanium`, `Volcanic Basalt`, `Submerged Limestone`, `Deep Ocean Water`, `Hoarfrost Snow`, or any user-authored material asset).
     2. **`ProceduralFieldLayer` (Analytical Surface Generator)** — Evaluates analytical math in Local, World, Triplanar, or UV coordinates (`DomainWarpedFbm`, `DualScaleVoronoi`, `HexLatticeGrid`, `GerstnerWaveSum`, `RainBeadStreak`, `AnisotropicBrush`, `CubicVoxelLattice`, `OrthographicContourGrid`) targeting any combination of OpenPBR channels (Albedo, Roughness, Metalness, Relief Height, Clearcoat, Thin-Film Thickness, Incandescent Radiance, Opacity Cutout).
     3. **`MaskModifierLayer` (Spatial & Animated Mask)** — Modulates *where* and *when* any layer appears (`SweepWaveMask`, `FresnelGrazingMask`, `GeodesicImpactRippleMask`, `RecessedCreviceMask`, `ElevationBandMask`, `PuddleFloodMask`, `VoxelClockCutoutMask`) with optional **Leading-Edge Plasma / Thermal Seam** radiance.
     4. **`SurfaceTransitionLayer` (Modular Material A ↔ Material B Transition)** — Blends *any* `FinishLayer A` into *any* `FinishLayer B` on *any* 3D asset across any animated `MaskModifierLayer` (e.g., Brushed Titanium ↔ Liquid Obsidian, Clean Armor ↔ Weathered Rust, Dry Asphalt ↔ Flooded Mirror Puddles, Stone ↔ Ice).
2. **Zero Shader Recompilation on Slider Drag (`DynamicParameterBlock`)**:
   - Each structural `MaterialLayerStack` compiles once into a cached **Slang → SPIR-V** variant keyed only by its layer-type signature.
   - Every slider, color swatch, transition progress (`0..1`), simulation clock (`ClockSeconds`), wind velocity, wetness, crack heat, and 3D world impact coordinate (`ImpactOriginXYZ`, `ImpactAgeSec`) updates at 120 Hz inside a tight **256-byte GPU Uniform Block (`DynamicParameterBlock`)** with zero stalls.
3. **100% Modular & Asset-Agnostic**:
   - **Never tie a dynamic material or dissolve solver to one specific asset class** (no `VehicleDissolver`, no `VehicleHmiTransition`).
   - Every shader reads normalized local-space bounds (`AssetLocalBoundsMin/Max`), world position, world normal, and optional `AssetSurfaceAnchor` tracks — so dragging `LatticeVoxelDissolve`, `SurfaceMaterialTransition`, or `HolographicTelemetryScan` onto a statue, building, prop, character, shaderball, OR `ControlVehicle` works automatically with zero per-asset shader edits.
4. **Optional Future Visual Wiring (Never Forced)**:
   - Internally, `DynamicMaterialCompiler` lowers `MaterialLayerStack` to a flat, immutable `MaterialInstructionSequence` before emitting `.slang` source. Artists work 100% in clean Project Zero Editor layer rows, sliders, swatches, and drivers — while preserving a clean internal boundary if an optional graph editor is ever wanted later.

---

## 3. Complete C++ & Slang Blueprint (100% Banned-Word Clean)

### 3.1 Proposed Module Layout

```text
Engine/MaterialSpace/
├── Dynamic/
│   ├── DynamicMaterialDefinitions.h       // Enums: ProceduralFieldPattern, MaskModifierMode, BlendChannelBitmask, ParameterDriverMode
│   ├── DynamicMaterialRecipe.h/.cpp       // Authorable MaterialLayerStack, MaterialLayerEntry, NamedParameterSlot (asset-agnostic)
│   ├── DynamicParameterBlock.h/.cpp       // 256-byte GPU-aligned live uniform block + ResolveDynamicParameters(...)
│   ├── DynamicMaterialCompiler.h/.cpp     // Lowers MaterialLayerStack -> flat MaterialInstructionSequence + Slang source
│   ├── DynamicMaterialRepository.h/.cpp   // Built-in generic dynamic presets (all approved HTML proposals from :8098)
│   └── DynamicSurfaceResolver.h/.cpp      // CPU reference surface resolver (bit-exact with Slang for validations & baking)
├── Baking/
│   └── DynamicChannelSnapshotter.h/.cpp   // Optional static texture bake at any timestamp T for LODs & external export

Engine/Shaders/MaterialSpace/
├── ProceduralFieldGenerators.slang        // Triplanar, DomainWarpedFbm, DualScaleVoronoi, HexLatticeGrid, GerstnerWaveSum, RainBeads
├── DynamicMaskSeam.slang                  // SweepWaveMask, FresnelGrazingMask, GeodesicImpactRipple, VoxelCutout, LidarScanBeam, PuddleRipples
├── DynamicMaterialResolve.slang           // Resolves DynamicMaterialRecipe -> full 10-lobe OpenPBR SurfaceMaterial + IncandescentRadiance + Opacity
├── StudioEnvironmentRadiance.slang        // Calibrated studio prefiltered IBL / octahedral radiance lookup (for GI-free PBR preview)
└── TransmissiveSurfaceRasterization.slang // Forward refractive & luminous overlay (Ocean Water, Hex Barrier, Holographic Scan, Rain Glass)

Engine/Workspaces/Material/
├── MaterialWorkspaceSession.h/.cpp        // Active recipe, layer stack selection, undo/redo, live clock playback, specimen switching
├── MaterialPreviewRenderer.h/.cpp         // Lightweight Direct Studio OpenPBR viewport renderer (reuses existing raster/lighting stack; NO GI)
├── MaterialViewportHost.h/.cpp            // Interactive orbit camera, specimen selector, click-to-impact ripple testing, render mode switch
├── MaterialLayerStackPane.h/.cpp          // Left pane: Material Repository + Finish/Field/Mask/Transition Layer Stack reorder & visibility
├── MaterialInspectorPane.h/.cpp           // Right pane: Project Zero Editor sliders, Parameter Drivers, OpenPBR 10-lobe readouts
└── MaterialWorkspaceHost.h/.cpp           // Docked workspace host + Content Browser drag-drop & live hot-reload into SceneWorld
```

### 3.2 Live Parameter Drivers (`ParameterDriverMode`)

Any scalar or color parameter on any layer can stay static or link to a live runtime driver without scripting:

| Driver Mode | Input Signal | Example Modular Usage on Any Asset |
| :--- | :--- | :--- |
| `ConstantScalar` | Artist slider | Roughness `0.08`, basalt fissure depth, clearcoat thickness |
| `PlaybackClock` | Monotonic simulation clock `t * Speed` | Flowing ocean swells, running glass rain streaks, convective magma pulse |
| `PingPongOscillation` | Smooth sinusoidal / triangle wave | Turntable demo preview of dissolve, transition, or scan |
| `ExternalTimeline` | Sequencer / gameplay / HMI channel (`0..1`) | Drive `TransitionProgress: 0 -> 1`, `DissolveProgress: 0 -> 1`, or `ScanElevation: -1 -> +1` on *any* actor |
| `WorldImpactPulse` | Up to 4 live 3D hit coordinates + timestamps `(xyz, t0)` | Click or hit any shield asset -> expanding geodesic shockwave ripple from exact impact point |
| `WeatherClimate` | Scene Rain Intensity, Wind Vector, Snow Accumulation | Globally darkens porous asphalt into rippled puddles & adds rain streaks to all exterior glass |
| `ParticleLatticeLink` | Shared `16³` voxel dissolve clock + radiance feedback | Synchronizes `LatticeVoxelDissolve` surface cutout with detached 3D fragments in `ParticleSpace` on *any* asset |

### 3.3 Modular Generic Material Repository (The 13 Proposals Live in `:8098`)

All 13 presets are strictly **generic and reusable on any asset** — `ControlVehicle` is merely one selectable preview specimen, never baked into the material names:

1. **`LatticeVoxelDissolve`** — Any OpenPBR `FinishLayer` + `CubicVoxelLattice` cutout (`ExternalTimeline` / `ParticleLatticeLink`) + white-hot → cyan leading-edge seam.
2. **`VolcanicPahoehoeMagma`** — Rough ropy basalt `FinishLayer` + `DualScaleVoronoi` tectonic fissure mask + recessed `1900 K` blackbody incandescence.
3. **`HexagonalEnergyBarrier`** — Translucent sapphire dielectric + `FresnelGrazingMask` + spherical `HexLatticeGrid` wireframe + `GeodesicImpactRippleMask` (`WorldImpactPulse`).
4. **`SurfaceMaterialTransition`** — Modular `SurfaceTransitionLayer` blending *any* `FinishLayer A` ↔ `FinishLayer B` (default demo: Brushed Titanium ↔ Liquid Obsidian) across an ionized hex-jittered sweep seam.
5. **`HolographicTelemetryScan`** — Any dark `FinishLayer` + sweeping `LidarScanBeamMask` + trailing `OrthographicContourGrid` wake + grazing Fresnel contour.
6. **`PhysicalOceanWater`** — Dielectric water (`IOR 1.333`) + 4-swell `GerstnerWaveSum` + dual capillary FBM normal layers + Schlick Fresnel + backlit crest SSS + micro-foam.
7. **`LiquidObsidianClearcoat`** — Deep obsidian finish + Kemppinen `GlintMicroflakeScattering` + mirror `ClearcoatLacquer` (`roughness 0.04`).
8. **`RainStreakedSmokedGlass`** — Smoked glass + `RainBeadStreak` normal/roughness modulation (`0.28` mist → `0.02` wet tracks) driven by `WeatherClimate`.
9. **`WetAsphaltRainPuddles`** — Crushed aggregate tarmac + `PuddleFloodMask` (`WeatherClimate`) transitioning dry (`0.88`) → damp (`0.36`) → mirror puddles (`0.02`) + concentric rain ripples.
10. **`HoarfrostSparkleSnow`** — Wind-sculpted snow + cool subsurface scattering + view-dependent `GlintMicroflakeScattering` ice glints.
11. **`SubmergedLimestoneCaustics`** — Wet riverbed limestone + dual-layer chromatically dispersed refractive caustic projection.
12. **`LiquidChromeMercury`** — Mirror conductor (`metalness 0.98`) + intersecting harmonic capillary wave displacement.
13. **`AnodizedThinFilmIridescence`** — Brushed gunmetal + Belcour-Barla wave-optics `ThinFilmInterference` modulated by gentle organic film-thickness drift.

---

## 4. Material Workspace Preview Renderer: Reuse Existing Code vs. New Code?

You asked a critical architectural question:
> *"For the Material Workspace preview we need an ordinary PBR renderer (with our full material system, without Global Illumination) — does it require writing a new renderer, more rendering code, or just reusing existing code?"*

### Short Answer: **DO NOT write a new renderer — reuse ~85–90% of Frontier's existing rendering code, and add ~10–15% targeted shared glue.**

### Exact Codebase Breakdown

| Rendering Piece | Status in Frontier Today | Reuse or New? |
| :--- | :--- | :--- |
| **Full 10-Lobe OpenPBR 1.1 Shading (`L1..L10`)** | Already implemented in `Engine/Shaders/MaterialSpace/OpenPbrSurfaceScattering.slang`, `GlintMicroflakeScattering.slang`, `ThinFilmInterference.slang`, `LinearlyTransformedSheen.slang`, `FiberScattering.slang`, `SubsurfaceAndTransmission.slang` | **100% REUSED** |
| **Geometry Rasterization & UV Differentials** | Already implemented in `Engine/GeometricRaster/Visibility/` (`VisibilityClusterRasterization`, `VisibilityMaterialResolve`, `HardwareVisibilityRasterization.slang`, `AnalyticBarycentricResolve.slang`) | **100% REUSED** |
| **Studio Area Softbox Lights (Key / Fill / Rim)** | Already implemented in `Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.{h,cpp}` (exact Heitz 2016 clipped-quad LTC area lights + Lagarde specular AO) | **100% REUSED** |
| **Vulkan Device, Compute Dispatch & ImGui Viewport** | Already implemented in `Engine/GeometricRaster/VulkanInterchange`, `ComputeDispatchEncoder`, `ImGuiOverlayRasterization`, `ViewportProjectionPane` | **100% REUSED** |
| **ACES Filmic Tone Mapping & Color Space** | Already implemented in `Engine/ColourSpace/` & `Engine/Shaders/Colour/` | **100% REUSED** |
| **GI-Free Direct Studio OpenPBR Preview Sequence (`MaterialPreviewRenderer`)** | Existing `TieredWorldRenderSequence` / `VulkanWorldRenderSequence` always builds & runs SDF + Octahedral Irradiance Lattice GI | **NEW (~350 lines C++ + small `.slang` entry)**: Executes **Visibility → DynamicMaterialResolve → Direct LTC Studio Softboxes + Studio IBL → ACES** while **skipping ALL GI** (zero SDF rebuild, zero Irradiance Lattice bake, instant 120 Hz real-time slider feedback!) |
| **Calibrated Studio IBL Lookup (`StudioEnvironmentRadiance.slang`)** | Without GI, metals, clearcoat, mercury, thin-film, and ocean water still need smooth studio reflections | **NEW (~120 lines `.slang`)**: Prefiltered octahedral/cubemap split-sum studio environment radiance sampled by GGX roughness — also reusable as fallback sky lighting in SceneWorld! |
| **Forward Transmissive & Luminous Overlay (`TransmissiveSurfaceRasterization.slang`)** | Current opaque visibility resolver handles 2-layer glass peel, but not animated refractive Ocean Water depth absorption or depth-occluded translucent Energy Shields / Holographic Scans | **NEW (~250 lines `.slang` + thin encoder)**: Shared by **BOTH** Material Workspace preview AND SceneWorld! |

### Clean Multi-Renderer Support in the Material Workspace (Zero Duplicated Material Shaders)

Because every renderer consumes the **exact same `DynamicMaterialResolve.slang` → 10-lobe `SurfaceMaterial`**, the Material Workspace viewport exposes a clean **Render Mode Selector** in its toolbar:

1. **Direct Studio OpenPBR *(Built First — Default)***: Full 10-lobe OpenPBR + Dynamic Layers + LTC Studio Softboxes + Prefiltered Studio IBL + Forward Transmissive Overlay — **zero GI overhead, 120 Hz live animation & instant slider response**.
2. **Real-Time GI Preview *(Plug-in Toggle — Reuses `VulkanWorldRenderSequence`)***: One toggle switches the preview turntable to Frontier's existing Tier 1 GTX / Tier 2 RTX GI sequence whenever you want to inspect real multi-bounce incandescent lighting (e.g., glowing magma or dissolve edges illuminating the studio floor).
3. **Deterministic Path Tracer *(Plug-in Toggle — Reuses Existing `PathTracer`)***: Progressive ground-truth reference verification with zero changes to the material recipe.

---

## 5. Phased Execution Roadmap ("What's Next")

Once you approve this plan and the 13 live HTML material previews on `:8098`:

### **Phase M1 — Dynamic Material Foundation (`Engine/MaterialSpace/Dynamic/`)**
- Implement `DynamicMaterialDefinitions.h`, `DynamicMaterialRecipe.{h,cpp}`, `DynamicParameterBlock.{h,cpp}`, `DynamicMaterialRepository.{h,cpp}`, and CPU reference `DynamicSurfaceResolver.{h,cpp}`.
- Add backward-compatible serialization for `MaterialLayerStack` recipes alongside static OpenPBR materials.
- Validation exhibit: **`DynamicMaterialRecipeValidation`** (determinism, zero NaNs, energy conservation, zero banned words, CPU specimen snapshot proofs).

### **Phase M2 — Slang GPU Shaders & Vulkan Validation (`Engine/Shaders/MaterialSpace/`)**
- Author `ProceduralFieldGenerators.slang`, `DynamicMaskSeam.slang`, `DynamicMaterialResolve.slang`, and `StudioEnvironmentRadiance.slang`, compiling cleanly into SPIR-V 1.3 via CMake `FrontierShaders`.
- Verify on headless Vulkan 1.3 SwiftShader via **`DynamicMaterialVulkanValidation`** (rendering GPU specimens of all approved dynamic materials and inspecting them visually).
- Generalize particle dissolve coupling into an asset-agnostic **`SurfaceDissolveSolver`** (wrapping/refactoring `ControlVehicle` into a generic showcase asset plugged into `LatticeVoxelDissolve` + `SurfaceMaterialTransition`).

### **Phase M3 — Direct Studio OpenPBR Preview & Forward Transmissive Rasterization (`Engine/GeometricRaster/`)**
- Build `MaterialPreviewRenderer` (reusing `VisibilityClusterRasterization`, `VisibilityMaterialResolve`, `LinearCosineLightIntegration`, and `OpenPbrSurfaceScattering.slang` with **zero GI** + `StudioEnvironmentRadiance.slang`).
- Build shared `TransmissiveSurfaceRasterization.slang` so **Physical Ocean Water**, **Hexagonal Energy Barrier**, **Holographic Telemetry Scan**, and **Rain-Streaked Smoked Glass** render with true refraction, Beer-Lambert depth absorption, and depth-occluded transparency in **both** the Material Workspace and SceneWorld.

### **Phase M4 — Native C++ Material Workspace (`Engine/Workspaces/Material/`)**
- Build the complete Project Zero Editor-styled C++ Material Workspace:
  - **Left Pane (`MaterialLayerStackPane`)**: Modular Material Repository + Bottom-to-Top `FinishLayer` / `ProceduralFieldLayer` / `MaskModifierLayer` / `SurfaceTransitionLayer` stack with reorder, solo/mute, and thumbnails.
  - **Center Viewport (`MaterialViewportHost`)**: Live Vulkan **Direct Studio OpenPBR** viewport (120 Hz, no GI) with specimen switcher (**Shaderball · Draped Cloth · Rounded Box · Any Imported Asset / ControlVehicle**), live clock play/pause/scrub bar, click-to-impact ripple trigger, and Render Mode selector (`Direct Studio OpenPBR` / `Real-Time GI` / `Path Tracer`).
  - **Right Pane (`MaterialInspectorPane`)**: Live `DynamicParameterBlock` sliders, `ParameterDriverMode` links, OpenPBR 10-lobe summary, and 1-click **Snapshot Static Textures** / **Assign to Content Browser**.
- Validation exhibit: **`MaterialWorkspaceUIValidation`** with headless visual captures verified via `read_file`.

### **Phase M5 — Modular SceneWorld & Multi-Asset Demo Integration**
- Attach `LatticeVoxelDissolve`, `SurfaceMaterialTransition`, `HolographicTelemetryScan`, `HexagonalEnergyBarrier`, and `PhysicalOceanWater` to multiple diverse assets in SceneWorld (props, architecture, water plane, and `ControlVehicle`) sharing one depth buffer, one `DynamicParameterBlock`, and optional Tier 1/2 GI radiance bounce.

### **Phase M6 — Optional Visual Graph Overlay (Strictly Deferred / Opt-In)**
- Only if ever requested in the future: layer an optional visual wiring view onto the exact same `MaterialInstructionSequence` IR while keeping **`MaterialLayerStack`** as Frontier's primary authoring workflow.

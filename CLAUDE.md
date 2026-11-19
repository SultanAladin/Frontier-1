# Claude & Agent Instructions for Frontier

Read this file and the mandatory documents in **§1** at the start of every session before writing
code, shaders, build scripts, or documentation.

---

## 1. Mandatory Session Startup Reading

| Order | Document | Scope |
| ----- | -------- | ----- |
| ① | `Documentation/AgenticInstructions/SKILL-CompactOutputProtocol.md` | Skimmer-first output, mandatory decision tables before choices |
| ② | `Documentation/AgenticInstructions/SKILL-Emoji.md` | Sole authority on permitted emojis (`🌟`/`✨` forbidden) |
| ③ | `Documentation/AgenticInstructions/SKILL-Naming.md` | Mechanism-first PascalCase construction & 7-gate test |
| ④ | `Documentation/AgenticInstructions/SKILL-Formatting.md` | 140-col file banners, 120-col sections, Allman braces, unit tags |
| ⑤ | `Documentation/BannedWordsVocabulary.md` | Canonical blacklisted terminology across code, files, folders, comments |
| ⑥ | `Documentation/CodingStandards.md` | ISO C++20 baseline, explicit function prototypes, `concept` templates |
| ⑦ | `Documentation/FolderStructure.md` | Authoritative repository & `Engine/` module layout |
| ⑧ | `Documentation/ImportantNotes.md` · `Tracker.md` · `Backlog.md` | Hard invariants, active milestones, and deferred work |

---

## 2. Non-Negotiable Architectural Mandates

### 🔴 2.1 All Debug Visuals & Viewport Overlays Render on the GPU via `.slang`
- **Every** viewport overlay — reference grids (`NavigationOverlay`), transform manipulators (`TransformGizmo`), wireframe debug lines/boxes/spheres/rays (`DiagnosticPrimitives`, `SpatialAnnotation`, `VectorAnnotation`), and world/screen text callouts (`TextAnnotation`) — **MUST be drawn directly on the GPU** via dedicated **Slang (`.slang`)** `…Rasterization` units.
- **NEVER** draw grids, gizmos, or debug overlays on the CPU.
- **NEVER** draw viewport grids, 3D gizmos, or debug geometry through ImGui (`ImDrawList`). ImGui is strictly restricted to docked UI panels, inspectors, tool ribbons, and chrome controls.
- **NEVER** scatter inline raw Vulkan draw state across feature/editor code; route all GPU execution through `Engine/GeometricRaster/` (`RenderDeviceInterface`, `IndirectDrawEncoder`) and `.slang` shaders.

### 🔴 2.2 Slang (`.slang`) Shader Standard
- All raster, compute, and image-operation shaders in `Engine/Shaders/` are written in **`.slang`** and compiled via `slangc` (`Automation/PrepareShaderToolchain.py`). Do not add `.glsl` or `.hlsl` files to `Engine/Shaders/`.

### 🔴 2.3 Cross-Platform Portability (Any Device: Windows · Linux · Android)
- Frontier is strictly cross-platform and must work cleanly on **Windows** (Win32 / x64 / ARM64), **Linux** (X11 & Wayland), **Android** (NDK / `ANativeWindow` / ARM64), and **Headless validation** (`PlatformWindowStub`).
- Confine OS-specific headers to `PlatformWindow{Win32,X11,Wayland,Android,Stub}.cpp` and platform device implementation `.cpp` files.
- Always provide portable scalar + ARM64 NEON + x86_64 SSE4.2 paths — never lock math or physics to x86-only intrinsics.

### 🔴 2.4 Three-Profile Build Architecture (`Editor` · `Development` · `Shipping`)
- **`Editor` (`-DFRONTIER_PROFILE=Editor` → `FRONTIER_PROFILE_EDITOR=1`)**: Full authoring editor (`Engine/Editor/` compiled & linked), multi-document workspaces, GPU overlays, and diagnostics. Runs in-place without packaging.
- **`Development` (`-DFRONTIER_PROFILE=Development` → `FRONTIER_PROFILE_DEVELOPMENT=1`)**: Standalone project runtime with developer flags, assertions, profiler, and GPU debug annotations active; `Engine/Editor/` excluded; runs directly from build output **without binary packaging or CI workflows**.
- **`Shipping` (`-DFRONTIER_PROFILE=Shipping` → `FRONTIER_PROFILE_SHIPPING=1`)**: Production release binary (`Engine/Editor/` excluded at CMake target level, assertions/diagnostics/annotations stripped, release optimizations enabled). Requires **standalone binary packaging** (`Automation/`) and gates release GitHub Actions (run GitHub Actions **only** with explicit user approval).

### 🔴 2.5 ImGui 8-Patch Stack, `Experimental/` HTML Reference & Visual Proofs in `Exhibits/`
- Third-party packages live in `ExternalPackages/` (git-ignored) and are fetched + SHA-256 verified via `python3 Automation/PrepareDependencies.py`.
- `ExternalPackages/imgui` (`3bae66c`) **requires all 8 stacked patches (`PatchA`–`PatchH`)** applied in order via `Automation/ApplyImGuiPatches.py` and compiled with `ImDrawIdx=unsigned int`.
- **`Experimental/` Prototypes Are HTML/JS Only (No C++)**: All reference studios and editor UIs under `Experimental/` (`ProjectZeroEditor`, `FrontierEditor`, `FrontierWorldMirror`, `ProjectBrowser`, `FluidEditor`, `FractureEditor`, `ParticleEditor`, `PhotometricEditor`, `ForceFieldEditor`, `VehicleLab`, `UVEditor`, `TextureStudio`) are strictly HTML/JS/CSS prototypes. **`Experimental/ProjectZeroEditor/`**, **`Experimental/FrontierWorldMirror/`**, and **`Experimental/ProjectBrowser/`** are the primary UI design references for shared C++20 components (`Engine/Components/`, `Engine/Workspaces/`, `Engine/Editor/`).
- **Mandatory HTML-to-C++ UI Conversion Procedure (Zero UI Invention / Zero Agent Drift)**:
  1. **Render & Screenshot HTML Ground Truth First**: Before writing or modifying any C++ UI or workspace code, render/capture visual screenshots of the target HTML mirror (`Experimental/*`) and inspect existing `Exhibits/Images/*` `.png` proofs side-by-side so exact panel proportions, entries, icons, headers, and controls are verified visually first.
  2. **Reuse Existing `Engine/Components/` & `Engine/Editor/` Kit**: Always compose UI out of existing `PixelSpace`, `WorkspaceDockContainer`, `ControlCentrePanel`, `ContentBrowser`, `OutlinerPanel`, `ViewportPanel`, `InspectorPanel`, `CapsuleButton`, `ExclusiveSelector`, `DropdownSelector`, `ScalarSlider`, `TextInputField`, `ConfirmationDialogue`, `ThemeRepository`, and `TypographyRepository` units — **NEVER** invent ad-hoc raw `ImDrawList` / unstyled `ImGui::*` substitute layouts or replacement renderers (`TieredWorldRenderSequence` + `ShowcaseGalleryScene` is the canonical World renderer).
  3. **Verify 1:1 Visual Parity via C++ Offscreen `.png` Capture**: After wiring C++, capture deterministic `.png` screenshots from the C++ executable (`--frames=4 --capture-png=...`) and visually verify 1:1 panel, entry, and renderer parity against the HTML capture before finalizing.
- **Mandatory Visual Proofs for Every `Exhibits/` Target (`Exhibits/Images/<Domain>/`)**:
  - Every validation executable in `Exhibits/` **MUST** render deterministic `.png` visual proof images into `Exhibits/Images/<Domain>/` via a CPU software rasterizer, SwiftShader offscreen Vulkan, or rendered diagnostic/comparison charts showing what the user would see when running the feature.
  - **Zero Excuses Rule**: Never claim a visual proof cannot be shown because the sandbox runs Linux instead of Windows or lacks a physical GPU — always synthesize and present the CPU/offscreen `.png` visual proof!

### 🔴 2.6 Never Write Plans, Reports, or Architecture Summaries in Chat — Use Markdown Studio
- **NEVER** dump architectural plans, decision tables, implementation reports, or multi-section explanations into chat messages.
- Write all plans, architectural decisions, and turn reports into **`Documentation/Plans/*.md`** (and/or **`Documentation/Plans/SessionNotes.md`**), rebuild **`Documentation/Plans/MarkdownStudioViewer.html`** via `python3 Automation/BuildMarkdownViewer.py`, keep `Automation/ServeMarkdownStudio.py` live on `0.0.0.0:8080`, and open the primary deliverable `.md` or `.png` file in the viewer.
- Keep chat replies strictly to **1–3 short lines** pointing to the updated Markdown Studio document path and verification status.

### 🔴 2.7 Zero-`And` Conjunction Ban (Single-Responsibility Filenames & Identifiers)
- **NEVER** use the conjunction **`And`** inside any filename, class, struct, enum, function, variable, shader, or documentation plan name (e.g. **`GpuTextureAndSdfBaker.h`**, **`BuildAndUploadStudy`**, **`CameraEyeAndMode`**, **`VolumeAndDistance`** are **STRICTLY FORBIDDEN**).
- Conjunction names signal conflated responsibilities. Either split into focused single-responsibility units (e.g. `TextureBaker.{h,cpp}` + `SdfBaker.{h,cpp}`) or choose one precise domain noun covering the unified mechanism (`SurfaceUpload`, `CameraOpticsHeader`, `VolumetricDistance`).
- Applies to all new and refactored `.h`, `.cpp`, `.slang`, `.js`, `.jsx`, and `.md` files across the entire repository; also enforced in `Documentation/AgenticInstructions/SKILL-Naming.md` and `Documentation/BannedWordsVocabulary.md`.

---

## 3. Initial C++ Bringup Order

1. **Dependencies & Build Profiles** (`Automation/`, `ExternalPackages/`, `Engine/Configuration/BuildConfiguration.h`, `CMakeLists.txt`).
2. **Input & Cross-Platform Windowing** (`Engine/Input/{InputExchange,InputExtension}.{h,cpp}`, `Engine/Windowing/PlatformWindow*`).
3. **Editor & Workspace Groundwork** (`Engine/Workspaces/`, `Engine/Editor/` target gated off `FRONTIER_EDITOR_ENABLED`, patched ImGui docking host, GPU `.slang` viewport overlay hooks).

---

## 4. Stage 1–5 Photometric & GI Pipelines (Active Algorithms & Retired Legacy Paths)

All five rendering upgrades in **Global Illumination** and **Atmosphere / Celestials** are active across both CPU (`TieredWorldRenderSequence`) and Vulkan GPU compute (`VulkanWorldRenderSequence` + `WorldViewportRadiometricCompute.slang` + modular `.slang` passes). Keep these active mappings and retired legacy exclusions intact.

### 4.1 Active Pipelines by Stage

| Stage | Domain | Active CPU Implementation | Active GPU `.slang` / Vulkan Dispatch | Core Algorithm & Parameters |
| :---: | :--- | :--- | :--- | :--- |
| **1** | **Global Illumination** (`Engine 2 & 3`) | `DynamicProbeCacheSequence` (`DdgiRaytracing`), `ReservoirPathReplaySequence` (`SdfGlobalIllumination`), `IrradianceLatticeSequence` (compatibility facade) | `DdgiLatticeComputeEntry`, `EvaluateReservoirPathReplayCandidate`, `ReservoirSpatialReuseEntry` (`WorldViewportReservoirReuseCompute.comp.spv`), `DenoiseCompositeEntry` (`1,2,4,8,16`), `DynamicProbeCache.slang`, `ReservoirPathReplay.slang`, `ReservoirTemporalFilter.slang` | 8x8 octahedral `1/pi` probe cache + 16x16 Chebyshev radial moments + backface ratio gating (`>0.25`); 4-candidate Owen-Sobol `(0,2)` RIS + BVH4 1-bounce + `DynamicProbeCache` 2nd bounce + spatial/temporal reuse (`Buffer` slots `28`/`29`) + **5-pass edge-aware SVGF** (`1,2,4,8,16`). |
| **2** | **Solar Photosphere & Corona** | `CelestialEphemerisSolver`, `AtmosphericSkySequence::EvaluateSolarCoronaRadiance`, `EvaluateNeckelLabsLimbDarkening`, `EvaluateSolarErf`, `EvaluateSolarGlareSpread` | `CelestialDiscStarCompute.slang` (`EvaluateSolarCoronaGpu`, `EvaluateNeckelLabsLimbDarkeningGpu`, `EvaluateSolarErfGpu`, `EvaluateSolarGlareSpreadGpu`) | **Neckel & Labs (1994)** 5th-order photospheric limb darkening `(650/550/450 nm)` + smooth `erfc` PSF integration + `< 3 px` sub-pixel `SolarErf` coverage fallback + K+F radial corona + Radiant/Muted solar glare bounded within `30 deg`. |
| **3** | **Physical 3-LUT Atmosphere** | `AtmosphericSkySequence::BuildSkyViewLut`, `ComputeSolarTransmittance`, `SampleTransmittanceLutDirection`, `SampleMultiScatterLut`, `SampleSkyViewDirection`, `EvaluateOrbitalSpaceRadiance` | `SkyViewTransmittanceCompute.slang`: Pass 1 `TransmittanceLutComputeEntry` (`256x64`), Pass 2 `MultiScatterLutComputeEntry` (`32x32`), Pass 3 `ComputeEntry` (`192x108`), `EvaluateSkyViewLutSample` (slot `11`), `EvaluateOrbitalSpaceGpu` | Bruneton/Hillaire 3-pass spherical Rayleigh + Mie + Ozone pipeline (`256x64` Transmittance + `32x32` infinite-order Multi-Scatter + `192x108` geometric-limb SkyView with vertical Catmull-Rom) + orbital space planetary shell when `OrbitalViewEnabled` or `ObserverAltitudeMeters > AtmosphereThicknessMeters`. |
| **4** | **Lunar Discs & Exospheres** | `LunarStarfieldSequence::EvaluateCelestialField`, `BuildLunarSurfaceAtlasRgba8` (`6 x 2048x1024`), `EvaluateLunarSunTransmittanceMoon`, `ResolveRadialChordSegmentMoon`, `QueryLunarExosphereParams` | `CelestialDiscStarCompute.slang` (`EvaluateSolarMoonBodiesGpu`, `ResolveRadialChordSegmentMoonGpu`, `SampleLunarAtlasGpu`) | Up to 6 independent lunar bodies from 2K texture atlas (`luna`, `ember`, `glacier`, `sulfur`, `shroud`, `shard` presets), Lommel-Seeliger/Hapke regolith + opposition surge, 3D radial-chord spherical exosphere Rayleigh/Mie + detached haze, eclipse penumbra/umbra attenuation, 16-tap 4x4 stratified limb AA. |
| **5** | **Starfield, Nebulae & Zodiacal Light** | `J2000BrightStarCatalog.h` (`1640` Yale BSC5 stars `Vmag <= 5.05`), `LunarStarfieldSequence` (`BuildStarfieldPreset`, `EvaluateZodiacalLight`, `6x48x48` cubemap bin grid) | `StellarTileCullCompute.slang` (`ClearStellarTilesEntry`, `ProjectCullStellarTilesEntry` — 80B UBO + slots `30`/`31`, `36u` SSBO pool), `CelestialDiscStarCompute.slang` (`EvaluateCelestialBodiesTiledGpu`, `EvaluateTiledCatalogStarsGpu`, `EvaluateZodiacalLightGpu`, `EvaluateGalacticBulgeRadianceGpu`, `EvaluateDeepSkyNebulaeGpu`) | `1640` J2000 catalog stars (CPU `6x48x48` spatial grid, GPU `16x16` screen-tile culling `<=64` stars/tile) + faint cubemap stars (`5.05..8.5 mag`), `Erf`/Moffat 2D PSF, Ballesteros blackbody LUT, Kasten-Young `X^1.5` atmospheric scintillation, Galactic Bulge (`* 0.0135`) + Coalsack/Great Rift dust + Orion/Carina/Lagoon/Magellanic/Pleiades nebulae (`* 1.45`), and Leinert Zodiacal Light (`AtmoFlags` bit `3` + `8-bit` quantized scale, `SmoothLat`/`CosLat` polar smoothing). |

### 4.2 Retired / Replaced Legacy Algorithms (Do Not Reintroduce)

| Retired Legacy Path | Replacement | Current Status |
| :--- | :--- | :--- |
| `IrradianceLatticeSequence::EvaluateSdfRadiosityConeGi` (16-cone SDF GI marcher) | `ReservoirPathReplaySequence` + 5-pass SVGF | **Removed** from `IrradianceLatticeSequence.{h,cpp}`. |
| `IrradianceLatticeSequence::EvaluateGtxSignedDistanceConeGi` (6-cone GTX SDF GI) | `DynamicProbeCacheSequence` (`SampleIrradianceOverPi`) | **Removed** from `IrradianceLatticeSequence.{h,cpp}`. |
| `OctahedralIrradianceLattice.slang` & `SignedDistanceConeIllumination.slang` (+ CMake rules) | `DynamicProbeCache.slang`, `ReservoirPathReplay.slang`, `ReservoirTemporalFilter.slang` | **Deleted** from `Engine/Shaders/GlobalIllumination/` & `CMakeLists.txt`. |
| Duplicate probe raytrace inside `TieredWorldRenderSequence::PrepareWorldScene` / `PrepareRtxIrradianceLatticeOnly` | Single `ProbeCache.UpdateProbeCache(...)` -> `RtxLattice.AdoptProbeCacheCells(ProbeCache)` | Replaced so CPU probe raytracing executes once instead of twice. |
| Single-buffer brute-force sky dome / non-3-LUT heuristic sky & 2D flat moon glow | Stage 3 3-LUT pipeline (`Transmittance` -> `MultiScatter` -> `SkyView`) & Stage 4 3D radial-chord lunar exospheres | Superseded across CPU and GPU (`SkyViewTransmittanceCompute.slang`, `CelestialDiscStarCompute.slang`). |

### 4.3 Critical Compatibility & Numerical Invariants

- **Compatibility Symbols Preserved**: `WorldRenderEngine::SdfGlobalIllumination` (`= 3u`), `WorldRenderEngine::DdgiRaytracing` (`= 2u`), `TieredWorldRenderSequence` enum order, `IrradianceLatticeSequence` (`PropagateRtxIrradianceLattice`, `SampleChebyshevIrradianceOverPi`, `AdoptProbeCacheCells`), and `SignedDistanceRadiosityVolume` (used **strictly** for Debug Channel `7` / `GdfRad` inspection, never for GI) remain intact so `Exhibits/`, UI settings (`ViewportPanel`), and GPU uniform layouts do not break. Both `DdgiRaytracing` (`2u`) and `SdfGlobalIllumination` (`3u`) enable `LatticeReady` (`SolarIrradianceTier.w = 1.0`) so `DynamicProbeCache` powers Engine `2` primary GI, Engine `3` 2nd-bounce ReSTIR replay, and secondary reflection bounces.
- **Zodiacal Polar Seam & Baseline**: `EvaluateZodiacalLight` (CPU & `.slang`) MUST apply `SmoothLat = sqrt(LatDeg^2 + 16) - 4` and `CosLat` attenuation across high ecliptic latitudes to prevent `Lat=0` creases and ecliptic-pole wedges; note `ZodiacalLightEnabled = true` introduces a `>= 60 S10` whole-sky floor (`0.002+`), so dark-sky unit checks must disable `ZodiacalLightEnabled` when isolating Milky Way / Magellanic radiance.
- **Sun Uniform Contract**: `WorldUniforms.SunDirConeTan.xyz` MUST always carry the normalized world sun direction (`std::tan(SunRadius)` in `.w`), synced cleanly on both CPU and `VulkanWorldRenderSequence`.
- **Stage 5 GPU Descriptor Budget**: `VulkanWorldRenderSequence` binds slots `0..31` (`36u` pooled SSBO count), including slot `11` (`AtmosphericSkyViewLut`), slots `28`/`29` (`ReservoirCurrent`/`History`), and slots `30`/`31` (`ProjectedCatalogStars`/`StellarTileBins`). `EvaluateCelestialBodiesGpu` (untiled) remains solely for standalone cubemap/off-frustum secondary reflection rays; primary viewport pixels dispatch `EvaluateCelestialBodiesTiledGpu`.

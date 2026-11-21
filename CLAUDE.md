# Claude & Agent Instructions for Frontier

Read this file and all mandatory governance documents in **§1** at the start of every session before
writing code, `.slang` shaders, build scripts, or documentation.

---

## 1. Mandatory Session Startup Reading

| Order | Document | Scope |
| :---: | :--- | :--- |
| ① | `Documentation/AgenticInstructions/SKILL-CompactOutputProtocol.md` | Skimmer-first output, mandatory decision tables, Markdown Studio over chat walls |
| ② | `Documentation/AgenticInstructions/SKILL-Emoji.md` | Sole authority on permitted emojis (plain `⭐` only; sparkle/fancy stars forbidden; status dots strictly `🔴`/`🟢`) |
| ③ | `Documentation/AgenticInstructions/SKILL-Naming.md` | Mechanism-first PascalCase construction & 7-gate rejection check |
| ④ | `Documentation/AgenticInstructions/SKILL-Formatting.md` | 140-col file banners, 120-col section rulers, Allman braces, aligned unit tags |
| ⑤ | `Documentation/BannedWordsVocabulary.md` | Canonical blacklisted terminology across code, shaders, files, folders, and comments |
| ⑥ | `Documentation/CodingStandards.md` | ISO C++20 baseline, explicit function prototypes, `concept`-constrained templates |
| ⑦ | `Documentation/FolderStructure.md` | Authoritative repository & `Engine/` module layout |
| ⑧ | `Documentation/ImportantNotes.md` · `Tracker.md` · `Backlog.md` | Hard invariants, active milestones, and queued ground-up redos |
| ⑨ | `Documentation/README.md` · `Documentation/Plans/README.md` | Documentation index and Markdown Studio plan/report ledger |

### 1.1 Active Workspace & Engine Reference Index (9 Host Workspaces · 9 Desktop Launchers)

| Scope | Documentation |
| :--- | :--- |
| **Workspaces Master Index & Naming Audit** | [`Engine/Workspaces/README.md`](Engine/Workspaces/README.md) |
| **World Editor & 9 Desktop Launchers** | [`Engine/Lifecycle/README.md`](Engine/Lifecycle/README.md) |
| **Editor Explorer (`ProjectBrowser` · `ContentBrowser`)** | [`Engine/Editor/README.md`](Engine/Editor/README.md) |
| **`UVWorkspace` (`FrontierUV`)** | [`Engine/Workspaces/UV/README.md`](Engine/Workspaces/UV/README.md) |
| **`Pattern` · `Stamp` · `Baking` (`Texturing/`)** | [`Engine/Workspaces/Texturing/README.md`](Engine/Workspaces/Texturing/README.md) |
| **`FractureWorkspace` (`FrontierFracture`)** | [`Engine/Workspaces/Fracture/README.md`](Engine/Workspaces/Fracture/README.md) |
| **`SketcherWorkspace` (`FrontierSketcher` + B-Rep/NURBS)** | [`Engine/Workspaces/ParametricSketcher/README.md`](Engine/Workspaces/ParametricSketcher/README.md) · [`Engine/GeometrySpace/Parametric/README.md`](Engine/GeometrySpace/Parametric/README.md) |
| **`MaterialWorkspace` (`FrontierMaterial`)** | [`Engine/Workspaces/Material/README.md`](Engine/Workspaces/Material/README.md) |
| **`ComponentEditor`** | [`Engine/Workspaces/ComponentEditor/README.md`](Engine/Workspaces/ComponentEditor/README.md) |
| **Non-Linear Revision DAG (`Revision/`)** | [`Engine/Workspaces/Revision/README.md`](Engine/Workspaces/Revision/README.md) |
| **Direct World Renderer & Unified Atmosphere** | [`Engine/GeometricRaster/GlobalIllumination/README.md`](Engine/GeometricRaster/GlobalIllumination/README.md) · [`Engine/GeometricRaster/Atmosphere/README.md`](Engine/GeometricRaster/Atmosphere/README.md) |
| **HTML Reference Studios & MatCap Stock** | [`Experimental/README.md`](Experimental/README.md) · [`EngineContent/MatCaps/README.md`](EngineContent/MatCaps/README.md) |

### 1.2 Removed Simplifications Queued for Ground-Up Redo

⚠️ Prior C++ implementations of **GI / SDF**, **Fluid**, **ForceField / VectorField**, and **Particle**
were stripped from `Engine/` (`63db120`, `daed305`) because they introduced simplified approximations,
reinvented non-matching UIs, misrepresented rendering output, and broke shared engine components.
**We are redoing these four domains cleanly from the ground up — never resurrect simplified stubs.**

| Queued Redo Domain | Preserved Reference & Status |
| :--- | :--- |
| **SDF Shadows · SDF GI · Emissive GI · Path Tracing** *(Priority Redo — [`WorldRenderingRedoPlan.md`](Documentation/Plans/WorldRenderingRedoPlan.md))* | Byte-exact engine sources, `VoxelSdfClipmap`, `DeterministicIntegrators`, `ControlVehicleSurfaceLoader`, validation suites, and 16+ rendered proof sheets preserved in [`Experimental/RemovedSdfGi/README.md`](Experimental/RemovedSdfGi/README.md), alongside [`Experimental/SdfLightingDemoCpp/`](Experimental/SdfLightingDemoCpp/) and [`Experimental/DeterministicPathTracer/README.md`](Experimental/DeterministicPathTracer/README.md). Locked execution order: **(0)** purge all residual SDF / SDF-baking code from `Engine/`; **(1st)** Standalone Direct PBR (`MatCap` + `OpenPbrDirect`, zero GI, zero SDF); **(2nd)** Real-Time GI (`StokesContour` + `SurfelRadiosity`) using **Shadow Candidate 1** (`HybridConeShadow`) on `GtxLowEnd` and **exact `BVH4` / `RayQuery` shadows (`0 MB` SDF)** on `HighEnd`; **(3rd)** Unified **Deterministic Path Tracer** for both Games & Final Scene Rendering (differing strictly by settings). |
| **Fluid · ForceField / VectorField · Particle** | Removed from `Engine/` and `Experimental/` (`daed305`). Paused until explicitly scheduled by the user; when redone, must pass every gate in **§2.1** (exact HTML UI & exact solver/shader parity — zero simplified billboard substitutes). |

---

## 2. Non-Negotiable Architectural & Fidelity Mandates

### 🔴 2.0 Mandatory Continuous `git commit` + `git push` (Cloud Sandbox Reset Protection)
- **Cloud sandboxes can reset at any moment without warning, permanently wiping uncommitted edits and unpushed local commits.**
- **ALWAYS** stage, commit (`git commit`), and push (`git push origin <current-branch>`) **immediately after completing each logical step/milestone and ALWAYS before ending every turn** (including documentation-only and planning turns).
- Never leave a dirty working tree or unpushed local commits at turn end, and never create, rename, or push to any branch other than the active session branch.

### 🔴 2.1 Automatic HTML-to-C++ Parity, Pre-Code Screenshot & Zero-Simplification Gate
Run this sequence **automatically on every HTML-to-C++ port or UI/renderer task — never wait for the user to ask**:

1. **Automatic Step 0 — Screenshot the Live HTML First (Before Writing Any C++ / `.slang`)**:
   - Serve the HTML reference locally, capture real browser `.png` screenshots across every relevant tab,
     inspector section, drawer, and viewport mode, and view those `.png` captures via `read_file`
     alongside the HTML/CSS/JS/WGSL/GLSL source **before** authoring or editing C++ or `.slang`.
2. **Exact 1:1 UI Reproduction via `Engine/Components/` (Zero Reinvented UI)**:
   - Match the HTML studio's exact panel layout, proportions, headers, icons, tabs, and controls using
     `PixelSpace`, `WorkspaceDockContainer`, `ControlCentrePanel`, `ContentBrowserPanel`, `OutlinerPanel`,
     `ViewportPanel`, `InspectorPanel`, `CapsuleButton`, `ExclusiveSelector`, `DropdownSelector`,
     `ScalarSlider`, `TextInputField`, `ConfirmationDialogue`, `ThemeRepository`, and `TypographyRepository`.
   - **NEVER** invent substitute layouts, custom unstyled `ImGui::*` chrome, or UI that diverges from the HTML.
3. **Exact 1:1 Simulation & Rendering Fidelity (Zero Simplifications · Zero Rendering Lies)**:
   - Port the **exact** mathematical, physical, and GPU shader pipeline of the HTML/reference source.
   - **NEVER** downgrade an algorithm into a simplified approximation (e.g. substituting basic sprite
     billboards for HTML GPU compute / strand / ribbon / surface solvers, stubbing shader stages, or faking GI/shadows).
   - **NEVER** lie about, exaggerate, or falsify what is actually rendering — verify pixel output honestly
     and state any remaining gap explicitly.
4. **Zero Collateral Engine Component Breakage**:
   - Never break, fork, or regress shared `Engine/Components/`, `Engine/GeometricRaster/`,
     `TieredWorldRenderSequence`, `VulkanWorldRenderSequence`, or unrelated workspaces.
5. **Mandatory Side-by-Side C++ Offscreen `.png` Proof**:
   - Capture deterministic `.png` screenshots from the C++ target (`--frames=4 --capture-png=...` or
     `Exhibits/*Validation`) and visually compare HTML `.png` vs. C++ `.png` via `read_file` before completion.

### 🔴 2.2 All Viewport Overlays & Debug Visuals Render 100% on GPU via `.slang`
- Reference grids, `TransformGizmo`, wireframes, bounding volumes, and callouts **MUST** execute on the
  GPU via dedicated `.slang` `…Rasterization` units.
- **NEVER** rasterize viewport grids, 3D gizmos, or debug geometry on the CPU or through `ImDrawList`.
  ImGui is restricted strictly to docked UI panels, inspectors, ribbons, and chrome controls.
- Route all Vulkan command and state encoding through `Engine/GeometricRaster/` — never scatter raw
  Vulkan boilerplate across workspace or editor files.

### 🔴 2.3 Slang (`.slang`) Shader Standard
- Every raster, compute, and image-operation shader under `Engine/Shaders/` is authored in **`.slang`**
  and compiled via `slangc` (`Automation/PrepareShaderToolchain.py`). Never add `.glsl` or `.hlsl` files
  to `Engine/Shaders/`.

### 🔴 2.4 Cross-Platform Portability (`Windows` · `Linux` · `Android` · `Headless`)
- Every `Engine/` module compiles and runs cleanly across **Windows** (`Win32` / x64 / ARM64), **Linux**
  (`X11` & `Wayland`), **Android** (NDK / `ANativeWindow` / ARM64), and **Headless** (`PlatformWindowStub`).
- Isolate OS headers strictly inside `PlatformWindow{Win32,X11,Wayland,Android,Stub,Desktop}.cpp` and
  hardware-endpoint `.cpp` translation units.
- Always pair x86 SIMD intrinsics with ARM64 NEON and portable scalar paths.

### 🔴 2.5 Three-Profile Build Architecture (`Editor` · `Development` · `Shipping`)
- **`Editor` (`-DFRONTIER_PROFILE=Editor` → `FRONTIER_PROFILE_EDITOR=1`)**: Compiles & links `Engine/Editor/`,
  authoring workspaces, GPU overlays, and diagnostics. Runs in-place without packaging.
- **`Development` (`-DFRONTIER_PROFILE=Development` → `FRONTIER_PROFILE_DEVELOPMENT=1`)**: Excludes
  `Engine/Editor/`; keeps developer flags, assertions, and GPU debug annotations; runs in-place without packaging.
- **`Shipping` (`-DFRONTIER_PROFILE=Shipping` → `FRONTIER_PROFILE_SHIPPING=1`)**: Excludes `Engine/Editor/`
  at CMake target level, strips assertions/diagnostics, enables release optimizations, and gates standalone
  packaging & GitHub Actions (run GitHub Actions **only** with explicit user approval).

### 🔴 2.6 Dependencies, ImGui 8-Patch Stack, `Experimental/` Policy & `Exhibits/Images/` Proofs
- Third-party packages live in git-ignored `ExternalPackages/`, fetched and SHA-256 verified via
  `python3 Automation/PrepareDependencies.py`.
- `ExternalPackages/imgui` (`3bae66c`) **requires all 8 stacked patches (`PatchA`–`PatchH`)** applied in
  order via `Automation/ApplyImGuiPatches.py` and compiled with `ImDrawIdx=unsigned int`.
- **`Experimental/` Holds Reference Prototypes & Preserved Records**: Browser HTML/JS/WebGPU studios
  (`ProjectZeroEditor`, `FrontierWorldMirror`, `ProjectBrowser`, `FrontierEditor`, `FractureEditor`,
  `PhotometricEditor`, `VehicleLab`, `UVEditor`, `TextureStudio`, `ComponentViewer`, `ClothEditor`,
  `GeometryCompiler`, `LevelDesigner`, `DeterministicPathTracer`, `DeterministicRaytracingLab`,
  `GtxReflectionBenchmark`, `ShadowAlgorithmLab`) define UI/shader ground truth; `Experimental/RemovedSdfGi/`
  and `Experimental/SdfLightingDemoCpp/` preserve the SDF/GI redo baseline.
- **Zero-Excuses Visual Proofs (`Exhibits/Images/<Domain>/`)**: Every `Exhibits/*Validation` binary **MUST**
  emit deterministic `.png` visual proofs via CPU software rasterization or SwiftShader offscreen Vulkan.
  Never claim a visual proof cannot be produced due to Linux or absence of a physical GPU.

### 🔴 2.7 Markdown Studio Output Protocol (Never Dump Plans or Reports in Chat)
- **NEVER** write architectural plans, decision tables, implementation reports, or multi-section summaries in chat.
- Write plans, decisions, and turn reports into `Documentation/Plans/*.md` (and/or `SessionNotes.md`),
  rebuild `Documentation/Plans/MarkdownStudioViewer.html` via `python3 Automation/BuildMarkdownViewer.py`,
  keep `Automation/ServeMarkdownStudio.py` live on `0.0.0.0:8080`, and present the primary deliverable file.
- Keep chat replies strictly to **1–3 short lines** citing the updated document path and verification status.

### 🔴 2.8 Zero-`And` Conjunction Ban & Banned Vocabulary Enforcement
- **NEVER** use the conjunction **`And`** in any filename, class, struct, enum, function, variable, shader,
  or plan document name — split into single-responsibility units or name one concrete mechanism.
- Audit every candidate identifier against `Documentation/BannedWordsVocabulary.md` and `SKILL-Naming.md`
  before writing code or documentation.

### 🔴 2.9 Reuse Existing Modules First — Search Before Creating
- Before creating any new file, class, `.slang` shader, codec, solver, or UI panel, search `Engine/`,
  `EngineContent/`, `Exhibits/`, `Automation/`, `Documentation/FolderStructure.md`, and the `§1.1` READMEs.
- Reuse and extend existing units in place; add a new file only when no existing module owns that mechanism.

---

## 3. Active World Rendering & Atmosphere Baseline

| Subsystem | Active CPU / C++20 Units | Active Vulkan / `.slang` Units | Active Contract |
| :--- | :--- | :--- | :--- |
| **Direct World Renderer** | `TieredWorldRenderSequence`, `ShowcaseGalleryScene`, `SolarShadowSequence`, `LinearCosineLightIntegration`, `SpecularReflectionSequence` | `VulkanWorldRenderSequence` (`22` bindings, `2912 B` UBO), `WorldViewportRadiometricCompute.slang`, `SolarFrustumShadow.slang`, `PlaneAwareShadowFilter.slang`, `SpecularGlassRaytrace.slang` | Dual non-GI `WorldRenderEngine`: `MatCap` (`0u`) & `OpenPbrDirect` (`1u`) with Debug channels `0..11`, LTC area quads, Shadow Lab (`WideTreeRayQuery` / `ContactPcss`), and BVH4 specular & Abbe glass. *(GI/SDF redo staged in `Experimental/RemovedSdfGi/`.)* |
| **3-LUT Sky & Solar Optics** | `AtmosphericSkySequence`, `CelestialEphemerisSolver` | `SkyViewTransmittanceCompute.slang` (`256x64` Transmittance, `32x32` MultiScatter, `192x108` SkyView), `CelestialDiscStarCompute.slang` | Bruneton/Hillaire Rayleigh + Mie + Ozone sky, orbital shell, Neckel & Labs 5th-order solar limb darkening, `erfc` sub-pixel solar disc, K+F corona; `WorldUniforms.SunDirConeTan.xyz` carries normalized sun direction (`tan(SunRadius)` in `.w`). |
| **Single-Source `PlanetaryClouds` & Fog** | `VolumetricCloudSequence` (`PlanetaryCloudGenerated.hpp`), `CloudNoiseVolume.h`, `AtmosphericWindSolver`, `VolumetricFogSequence`, `LensFlareSequence` | `CloudNoise.slang`, `CloudDensity.slang`, `CloudTransport.slang`, `CloudAerialPerspective.slang`, `CloudCompute.slang` (`-target cpp` + GPU), `VolumetricCloudCompute.slang`, `VolumetricFogCompute.slang`, `LensFlareCompute.slang` | Single-source `.slang` planetary cloud deck across 12 formations + `BoundedCloudVolume` + 4×4 cubic B-spline upsample + solar cloud shadows, coupled with wind-advected height/local fog and baked optical lens flare. |
| **Moons, Starfield & Zodiacal Light** | `LunarStarfieldSequence`, `J2000BrightStarRepository.h` (`1,640` Yale BSC5 stars) | `StellarTileCullCompute.slang` (`16x16` screen tiles), `CelestialDiscStarCompute.slang` | Up to 6 atlas moons (`6 x 2048x1024`, Lommel-Seeliger/Hapke regolith, 3D radial-chord exospheres, eclipse umbra/penumbra), `1,640` tiled BSC5 + cubemap stars, Milky Way/nebulae, and Leinert zodiacal light (`SmoothLat = sqrt(LatDeg^2 + 16) - 4` + `CosLat` polar smoothing). |

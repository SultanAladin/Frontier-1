# Frontier Dynamic & Procedural Material System Plan (Layer-Stack First — No Node Editor Required)

> **Status**: Architecture & Planning Document (zero C++ changes committed until approved).  
> **Live HTML Material Previews**: All 13 proposed procedural & animated OpenPBR shaders are live-previewable right now in `Experimental/TextureStudio/` (`http://<preview>:8098/Experimental/TextureStudio/`, category **"Animated & VFX"**).  
> **Visual Proofs**:
> - `Exhibits/Images/Particles/TextureStudio/ProposedMaterials_Hero6_Upgraded.png`
> - `Exhibits/Images/Particles/TextureStudio/ProposedMaterials_13_Showcase.png`

---

## 1. Truthful Audit: Where Frontier's Material System & Material Workspace Stand Today

Your intuition was **100% accurate** — we have **not** started the C++ **Material Workspace** yet because the underlying **Dynamic & Procedural Material System** has not been built on top of the static OpenPBR engine.

| Layer | Current Status in Frontier | Exact Location |
| :--- | :--- | :--- |
| **10-Lobe Static OpenPBR 1.1 Physics (`L1..L10`)** | **COMPLETE** (C++20 + Slang `.slang`) — Oren-Nayar diffuse, anisotropic GGX + F82 metal, tinted clearcoat, Belcour-Barla thin-film iridescence, Kemppinen 4D glinty microflakes, Abbe glass transmission, Burley SSS, Zeltner LTC sheen, Chiang hair/fiber, porosity + Planckian blackbody | `Engine/MaterialSpace/SurfaceMaterial.{h,cpp}`, `Engine/MaterialSpace/Scattering/*.h`, `Engine/Shaders/MaterialSpace/*.slang` |
| **Compact GPU Material Packing & GI Bounce Summary** | **COMPLETE** — Compact register, surface-layer packing, directional albedo & sheen integrals | `Engine/MaterialSpace/CompactMaterialRegister.{h,cpp}`, `MaterialSurfaceLayer/`, `EnergyCompensation/` |
| **Offline Static Specimen Rasterizer & Channel Baker** | **COMPLETE** (static textures only) | `Engine/MaterialSpace/Baking/MaterialSpecimenRasterizer.{h,cpp}`, `ProceduralChannelBaker.{h,cpp}` |
| **Material Workspace HTML Reference UI** | **COMPLETE** — Project Zero Editor styling, strata inspector, channel slots, PMREM shaderball stage, 13 live animated shaders | `Experimental/TextureStudio/Workspaces/MaterialWorkspace.js`, `Experimental/ProjectZeroEditor/MaterialPanel.css` |
| **Dynamic / Procedural / Animated Material System** | **NOT STARTED IN C++** (HTML prototype only) — no runtime time/event parameters, no non-destructive layer stack, no live procedural fields, no material-to-material morph, no holographic scan, no animated water surface | **Designed in this document** |
| **C++ Material Workspace UI (`Engine/Workspaces/Material/`)** | **NOT STARTED** — `Engine/Workspaces/` currently contains `UV/`, `Texturing/` (Pattern/Stamp/Baking), `Particle/`, and `Revision/` | **Scheduled directly after the Dynamic Material Core** |

---

## 2. Core Design Philosophy: Unreal/Blender Power **Without** a Node Editor

Artists love what Unreal Material Instances and Blender procedural shaders **do** (live animated sliders, procedural noise/Voronoi/waves, physical layering, real-time transitions, zero-cost parameter tweaking), while many artists and engineers strongly dislike untangling node spaghetti.

Frontier solves this with a **Strata Layer-Stack + Parametric Field + Live Driver** architecture (closer to **Substance Painter + Cinema 4D + Unreal Material Instances**, backed by a clean compiled **Slang `.slang` shader recipe**):

1. **Everything is a Vertical Layer Stack (`MaterialStratumStack`)**, NOT a spiderweb of wires:
   - **Bottom → Top reading order**, identical to Photoshop / Substance / Texture Studio's existing **Strata** inspector.
   - Every layer is one of four crystal-clear types:
     1. **Physical Substrate Layer** — An OpenPBR 1.1 material (`Liquid Obsidian`, `Brushed Aerospace Titanium`, `Volcanic Basalt`, `Submerged Limestone`, `Deep Ocean Water`, `Hoarfrost Snow`).
     2. **Procedural Field Generator** — Evaluates analytical math in Object, World, Triplanar, or UV space (`DomainWarpedFbm`, `DualScaleVoronoi`, `HexLatticeGrid`, `GerstnerWaveSum`, `RainBeadStreak`, `AnisotropicBrush`, `CubicVoxelLattice`, `OrthographicContourGrid`) directly targeting **any subset of OpenPBR channels** (Albedo, Roughness, Metalness, Relief Height, Clearcoat, Thin-Film Thickness, Emission, Opacity/Cutout).
     3. **Spatial & Animated Mask Modifier** — Controls *where* and *when* a layer appears (`SweepWaveMask`, `FresnelGrazingMask`, `GeodesicImpactRippleMask`, `RecessedCreviceMask`, `ElevationBandMask`, `PuddleFloodMask`, `VoxelClockCutoutMask`) with optional **Leading-Edge Plasma / Thermal Seam** emission!
     4. **Material Transition / Blend Stratum** — Blends **Material A → Material B** cleanly across any animated Mask Modifier (e.g., Vehicle HMI **Brushed Titanium → Liquid Obsidian** with an ionized hex seam, or **Dry Asphalt → Mirror Rain Puddles** with concentric ripples).
2. **Zero Shader Recompilation on Slider Drag (`DynamicParameterRegister`)**:
   - Just like **Unreal Material Instances (`UMaterialInstanceDynamic`)**, every stack compiles once into a cached **Slang → SPIR-V variant** keyed strictly by its structural signature (layer count + generator enum IDs).
   - Every numeric slider, color swatch, sweep progress (`0..1`), elapsed clock (`ClockSeconds`), wind speed, wetness, crack temperature, and 3D world impact coordinate (`ImpactOriginXYZ`, `ImpactAgeSec`) lives inside a tight **256-byte GPU Uniform / Push-Constant Block (`DynamicMaterialUniformBlock`)** updated at 120 Hz with **zero pipeline stalls**.
3. **Future-Proof Internal Separation (Optional Node View Later, Zero Forced Nodes Now)**:
   - Under the hood, `MaterialStratumCompiler` lowers the linear layer stack into a tiny, flat, immutable **Typed Expression Sequence (`MaterialEvalInstruction[]`)** before emitting `.slang` code.
   - You author 100% through clean **Project Zero Editor** strata cards, sliders, swatches, and live animation timeline curves — and if an optional node-graph viewport is ever desired years later, it compiles to the exact same `MaterialEvalInstruction[]` without rewriting a single shader or file format.

---

## 3. Complete C++ Architecture Blueprint (`Engine/MaterialSpace/Dynamic/`)

> All proposed C++ identifiers strictly obey `Documentation/BannedWordsVocabulary.md` (zero banned words such as `Graph`, `Node`, `Core`, `Base`, `Map`, `Pass`, `Stage`, `Frame`, `Data`, `Info`, `Object`, `Manager`, `Helper`, `And`, etc.).

### 3.1 New C++ Module Layout

```text
Engine/MaterialSpace/
├── Dynamic/
│   ├── DynamicMaterialTypes.h             // Enums: ProceduralFieldKind, MaskModifierKind, BlendChannelMask, ParameterDriverKind
│   ├── DynamicMaterialRecipe.h/.cpp       // Authorable non-destructive strata stack + named live parameter slots
│   ├── DynamicParameterBlock.h/.cpp       // 256-byte GPU-aligned live uniform block + curve/clock/event driver evaluator
│   ├── DynamicMaterialCompiler.h/.cpp     // Lowers MaterialStratumStack -> flat MaterialEvalSequence + Slang shader source
│   ├── DynamicMaterialLibrary.h/.cpp      // Curated built-in dynamic presets (all approved proposals from :8098!)
│   └── DynamicSurfaceEvaluator.h/.cpp     // CPU reference evaluator (1:1 bit-exact math with Slang for tests & baking)
├── Baking/
│   └── DynamicChannelSnapshotter.h/.cpp   // Optional bake-to-static-texture export at any timestamp T for LODs/exporters
Engine/Shaders/MaterialSpace/
├── ProceduralFields.slang                 // Fast GPU library: Triplanar, DomainWarpedFbm, DualVoronoi, HexGrid, Gerstner, RainBeads, ThinFilm
├── DynamicMasksAndSeams.slang             // SweepWave, FresnelGrazing, GeodesicImpactRipple, VoxelCutout, LidarScanBeam, PuddleRipples
└── DynamicMaterialResolve.slang           // Evaluates active DynamicMaterialRecipe into full 10-lobe OpenPBR SurfaceMaterial + Emissive + AlphaCutout
Engine/Workspaces/Material/                // Phase M4: Native C++ Material Workspace (matching Project Zero Editor + Texture Studio UI)
├── MaterialWorkspaceSession.h/.cpp        // Active recipe, strata selection, undo/redo, live clock playback, preset switching
├── MaterialViewportHost.h/.cpp            // Vulkan PMREM 3-point studio viewport (Shaderball, Draped Cloth, Rounded Box, ControlVehicle)
├── MaterialStrataOutlinerPane.h/.cpp      // Left pane: Material Library + Strata Layer Stack reorder/visibility/mask cards
├── MaterialInspectorPane.h/.cpp           // Right pane: Project Zero Editor sliders, live Parameter Drivers, OpenPBR lobe readouts
└── MaterialWorkspaceHost.h/.cpp           // Docked workspace host + Content Browser drag-drop & live hot-reload into SceneWorld
```

### 3.2 Live Parameter & Animation Drivers (`ParameterDriverKind`)

Every scalar or vector parameter on any stratum can either stay static OR bind to a live runtime driver (zero scripting required):

| Driver Kind | What Drives It | Example Use Case |
| :--- | :--- | :--- |
| `ConstantScalar` | Static artist slider | Base roughness `0.08`, basalt crack depth |
| `PlaybackClock` | Monotonic simulation clock `t * Speed` | Flowing ocean waves, drifting rain streaks, convective magma pulse |
| `PingPongWave` | Smooth sinusoidal / triangle oscillation | Showroom demo loop of Derez or Material Transition |
| `ExternalTimeline` | Sequencer / Vehicle HMI state controller (`0..1`) | Car HMI button triggers `TransitionProgress: 0 -> 1` or `ScanHeight: -1 -> +1` |
| `WorldImpactEvent` | Up to 4 live 3D hit coordinates + timestamps `(xyz, t0)` | Projectile hits Energy Shield -> spawns real geodesic shockwave ripple at exact impact point |
| `WeatherEnvironment` | Global scene Rain Intensity, Wind Vector, Snow Depth | Automatically transforms dry asphalt into rippled puddles & coats car glass with streaks |
| `ParticleCoupling` | Shared `16³` lattice clock + emissive bounce buffer | Keeps `ControlVehicle` Derez shader cutout **100% locked** to detached 3D cubes in `ParticleSpace` |

### 3.3 The 13 Dynamic Material Presets (Currently Live in HTML `:8098` for Your Review)

Every one of these maps cleanly to a small 2–4 stratum `DynamicMaterialRecipe` (zero hard-coded monolithic uber-shaders once compiled!):

1. **Derez Lattice Dissolve (`ControlVehicle`)** — `Liquid Obsidian` substrate + `CubicVoxelLattice` cutout mask (`ExternalTimeline` / `ParticleCoupling`) + white-hot → cyan leading-edge seam.
2. **Molten Magma Crust (Pahoehoe Basalt)** — `Volcanic Basalt` substrate (`DomainWarpedFbm` ropy relief + rough ash `0.88`) + `DualScaleVoronoi` tectonic fault mask + recessed `BlackbodyEmission` (`1900 K`) convective core.
3. **Energy Shield Hex Barrier (1:1 HTML Parity)** — Translucent dark sapphire dielectric + `FresnelGrazingMask` (`pow(1 - NdotV, 2.55)`) + spherical `HexLatticeGrid` wireframe + `GeodesicImpactRippleMask` (`WorldImpactEvent` / auto-demo impacts).
4. **Material Transition (`Vehicle HMI`)** — **Blend Stratum**: `Anisotropic Brushed Aerospace Titanium` (`Stratum A`) → `Liquid Obsidian Clearcoat` (`Stratum B`) across an organic hex-jittered `SweepWaveMask` with blazing ionized cyan-white seam.
5. **Holographic Scan (`Vehicle HMI`)** — `Carbon-Obsidian` substrate + `LidarScanBeam` (`ExternalTimeline`) + trailing `OrthographicContourGrid` wake + grazing Fresnel holographic outline.
6. **Physical Ocean Water (Animated)** — `DielectricTransmission` water substrate (`IOR 1.333`) + 4-swell `GerstnerWaveSum` + dual capillary FBM normal layers + Schlick Fresnel sky reflection + backlit crest SSS + steep-crest micro-foam stratum.
7. **Liquid Obsidian Clearcoat (`ControlVehicle`)** — Deep piano-obsidian diffuse + `GlintMicroflakeScattering` pearlescent depth + `ClearcoatLacquer` (`roughness 0.04`).
8. **Rain-Streaked Smoked Glass** — Smoked glass substrate + `RainBeadStreak` generator modulating normal bump & roughness (`0.28` mist → `0.02` wet tracks) driven by `WeatherEnvironment`.
9. **Wet Asphalt & Rain Puddles** — `CrushedAggregateAsphalt` substrate (`roughness 0.88`) + `PuddleFloodMask` (`WeatherEnvironment`) transitioning through damp darkened pores (`0.36`) to mirror puddles (`0.02`) + concentric `RainRippleRing` normals.
10. **Hoarfrost & Sparkle Snow** — Wind-sculpted `SastrugiDrift` snow + cool blue `SubsurfaceDiffusion` + view-dependent `GlintMicroflakeScattering` diamond ice crystal glints.
11. **Submerged Limestone Caustics** — Wet riverbed limestone (`Voronoi` pebbles + FBM grit) + dual-layer chromatically dispersed `RefractiveCausticField` projection.
12. **Liquid Chrome Mercury** — Mirror conductor (`metalness 0.98`, `roughness 0.05`) + intersecting harmonic `CapillaryWaveField` surface displacement.
13. **Anodized Thin-Film Iridescence** — Brushed gunmetal titanium + `ThinFilmWaveParameters` (Belcour-Barla wave-optics interference modulated by subtle organic film-thickness drift).

---

## 4. What's Next — Phased Execution Roadmap

Once you review the 13 live HTML material previews on `:8098` (and tell us which ones to approve/tweak), here is the exact execution order:

### **Phase M1 — Dynamic Material Core (`Engine/MaterialSpace/Dynamic/`)**
- Implement `DynamicMaterialTypes.h`, `DynamicMaterialRecipe.{h,cpp}`, `DynamicParameterBlock.{h,cpp}`, and CPU reference `DynamicSurfaceEvaluator.{h,cpp}`.
- Add `DynamicMaterialRecipe` serialization into Frontier's existing `.MaterialSpace` TOML/binary codec (100% backward-compatible with existing static OpenPBR materials).
- Validation exhibit: `DynamicMaterialCoreValidation` (determinism, zero NaNs, energy conservation, zero banned words, CPU specimen renders of approved dynamic presets).

### **Phase M2 — Slang GPU Shaders & Vulkan Pipeline (`Engine/Shaders/MaterialSpace/`)**
- Author `ProceduralFields.slang`, `DynamicMasksAndSeams.slang`, and `DynamicMaterialResolve.slang` feeding straight into Frontier's existing `OpenPbrSurfaceScattering.slang` + `BounceLayerSummary.slang`.
- Compile to SPIR-V 1.3 via CMake `FrontierShaders` and verify on headless Vulkan 1.3 SwiftShader (`DynamicMaterialVulkanValidation`).
- Wire `VehicleDissolveSolver` / `ParticleSpace` to share the exact `DynamicParameterBlock` clock & leading-edge emission with `ControlVehicle`'s `Derez` and `Material Transition` shaders + two-way GI emissive bounce!

### **Phase M3 — Water Transmission, Refraction & Transparent Shield Pass (`Engine/GeometricRaster/`)**
- Extend `VisibilityMaterialResolve` / `TieredWorldRenderSequence` with a dedicated **Forward Transmissive & Additive Energy Pass** (reading the opaque scene color + depth buffer) so **Physical Ocean Water** refracts real underwater geometry with Beer-Lambert depth absorption AND **Energy Shield Hex Barrier** / **Holographic Scan** render true translucent depth-occluded overlays with scene-intersection edge highlights!

### **Phase M4 — Native C++ Material Workspace (`Engine/Workspaces/Material/`)**
- Build the complete Project Zero Editor-styled C++ Material Workspace matching `Experimental/TextureStudio/Workspaces/MaterialWorkspace.js` & `Experimental/ProjectZeroEditor/MaterialPanel.css`:
  - **Left Pane**: Material Library + Non-Destructive Strata Layer Stack (add substrate / procedural field / mask / material transition blend, drag-reorder, solo/mute, preset thumbnails).
  - **Center Stage**: Live Vulkan PMREM 3-point viewport with 1-click specimen switcher (**Shaderball · Draped Cloth · Rounded Box · ControlVehicle**), live animation play/pause/scrub bar, and interactive impact-click testing (click anywhere on the Energy Shield to trigger a live shockwave ripple!).
  - **Right Inspector**: Strata parameters, OpenPBR 10-lobe summary, live Parameter Driver bindings (`Clock`, `Timeline`, `Weather`, `Impact`), and 1-click **Bake Static Textures** / **Assign to Content Browser**.
- Validation exhibit: `MaterialWorkspaceUIValidation` with headless visual snapshot proof sheets inspected via `read_file`.

### **Phase M5 — SceneWorld & Vehicle HMI Integration**
- Expose `ControlVehicle` HMI triggers in SceneWorld / Particle Workspace: **Derez**, **Reverse-Rez**, **Dissolve to Nothing**, **Dissolve to Butterfly**, **Material Morph (Titanium ↔ Obsidian)**, and **Holographic Telemetry Scan** — all sharing one unified depth buffer, one unified `DynamicParameterBlock`, and full two-way GI bounce illumination.

### **Phase M6 — Optional Visual Graph View (Deferred / Strictly Opt-In)**
- Only if ever desired later: add an optional read/write visual wiring overlay on top of the exact same `MaterialEvalSequence` IR, keeping the **Strata Layer-Stack** as Frontier's primary first-class authoring workflow.

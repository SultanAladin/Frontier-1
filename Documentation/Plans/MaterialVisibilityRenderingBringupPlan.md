# Unified Material Space, Hybrid Visibility Rasterization & Rendering Bringup Plan

**Status:** Approved Architecture & Execution Blueprint (`2026-11-10`)  
**Scope:** Audit of `Frontier` (`arena/429d7dc7-frontier`) + `Frontier-OLD` (`arena/255c3b21-frontier`) and concrete C++20 + `.slang` roadmap for:
1. Calibrated Deterministic Shadows & Octahedral Irradiance Lattice (`:8097` & Vulkan `1.3` fixes),
2. Universal 10-Lobe Physical `MaterialSpace` (`OpenPBR 1.1` + Constant-Time `Glinty` Microflakes),
3. `64-bit` (`8 B/px`) Hybrid Hardware + Software Micro-Face Visibility Rasterization (`GTX` + `RTX`),
4. Shadow Architecture Decision (**No Virtual Shadow Maps**),
5. Workspace Readiness Audit (`GeometrySpace`, `UV`, `GeometricRaster`, `Codecs`),
6. Canonical **New Folders Only** Layout & Dependency-Ordered Starting Sequence.

Related documents: [RenderingRaytracingArchitecturePlan.md](RenderingRaytracingArchitecturePlan.md) · [../FolderStructure.md](../FolderStructure.md) · [../BannedWordsVocabulary.md](../BannedWordsVocabulary.md) · [../AgenticInstructions/SKILL-Naming.md](../AgenticInstructions/SKILL-Naming.md)

---

## 1. Workspace Sync Clarification & Readiness Audit (`Geometry + UV + Rendering`)

### 1.1 Repository Sync Distinction
- **Merged into Active Branch (`arena/429d7dc7-frontier`):** `SultanAladin/Frontier` (`arena/4c9015d5-frontier` @ `b53ca68`, `138` files, `17,962` lines of ISO C++20 + `.slang`) — providing the clean governance-compliant `GeometrySpace`, 6-pillar `UV` Workspace, `GeometricRaster` (`VulkanInterchange`, `ComputeDispatchEncoder`, `TimelineTransferRegister`), `ContentBrowser`, `Revision`, `Threading`, and `.Space` `Codecs`.
- **Audited as Algorithm Source (Not Raw-Merged):** `unassignedinbox/Frontier-OLD` (`arena/255c3b21-frontier`, `6,039` files). Because `Frontier-OLD` has an unrelated Git root and legacy pre-governance identifiers, we **port its verified math and `.slang` formulations selectively** into the canonical `Frontier` folder layout rather than raw-merging legacy trees.

### 1.2 Live Readiness Audit of Current `/home/user/Frontier`

| Engine Domain | Readiness | Present & Verified in `/home/user/Frontier` Today | Remaining Delta (Built During Rendering Bringup) |
| :--- | :---: | :--- | :--- |
| **`Engine/GeometrySpace/` + `Interchange/`** | **92%** | Half-edge `PolygonNetwork` (`Vertices`, `DirectedEdges`, `Faces`, `EnableSeamCut`, `EnableSharpCrease`, `FaceShadingAssignment`), `FacePartitionRegister`, `AnalyticPrimitives`, `883`-line OBJ + glTF 2.0 `GeometryInterchange`, `ControlledIntegrator.GeometrySpace`. | Continuous tangent basis construction + `128`-face `ClusterPyramid` & `4-Wide` `WideBoundingTree` (`BVH4`) builders (**Step 2**). |
| **`Engine/Workspaces/UV/` + `UVCodec`** | **100%** | Complete 6-pillar C++20 + `.slang` UV suite (`SeamCutGenerator`, LSCM `ConformalUnwrapSolver`, ABF++/Projection solvers, GPU `ProgressiveStretchRelaxer`, `ConcaveSkylinePacker`, UDIM `TexelDensityEqualizer`, `UVWorkspaceRasterizer`, `FrontierUV` desktop host). | **None.** Already supplies clean UDIM `(u, v)` coordinates and well-conditioned UV Jacobians `(∂u/∂x, ∂v/∂y)` required by multiscale glint microflakes. |
| **`Engine/GeometricRaster/` + `Windowing/`** | **85%** | `VulkanInterchange` (`649` lines: `volk`, Vulkan `1.2+` Timeline Semaphores with zero fences, hardware GPU + headless SwiftShader fallback, buffer/image allocation, SPIR-V loading), `ComputeDispatchEncoder`, `TimelineTransferRegister`, `ViewportPickQuery`, `ImGuiOverlayRasterization`, `ViewportProjection`, `DesktopWorkspaceLaunch`, `.slang` compiler toolchain. | Enable optional Vulkan `shaderInt64Atomics` + `VK_KHR_acceleration_structure` / `VK_KHR_ray_query` device features on `VulkanInterchange`, then add `MaterialSpace`, `Visibility`, and `Illumination` units (**Steps 1–5**). |
| **`Engine/Codecs/` + `Threading/` + `ContentBrowser/`** | **95%** | `SpaceContainerCodec` (`TOML` + `64 B` aligned binary sections), `MaterialCodec` skeleton, `TextureCodec`, `ImageCodec`, `TaskScheduler`, `AssetStreamThread`, `RevisionTracker` (`215 / 750 MiB`), `ThumbnailSheetRepository`. | Extend `MaterialCodec` binary section to serialize `CompactMaterialEntry` (`128 B`) + `BounceSlabEntry` (`32 B`) (**Step 1**). |

**Readiness Verdict:** ✔️ **100% ready to start C++20 + `.slang` rendering bringup immediately — zero missing prerequisites.**

---

## 2. Five Calibrated Fixes for Deterministic Raytraced Shadows & Irradiance (`RTX` / `:8097`)

Raytraced solar shadow rays are already active, and five targeted physical fixes eliminate the `6.6%` sRGB floor washout (`0.904` lit vs. `0.838` umbra):

| # | Defect Identified in `:8097` Inspection | Production Vulkan `1.3` / `.slang` Fix |
| :--- | :--- | :--- |
| **1** | Indirect irradiance (`ddgiGain = 1.65`) lacked `1/π` Lambertian normalization while direct sun multiplied by `INV_PI`, making ambient fill `1.24×` brighter than noon sun. | Multiply diffuse irradiance by `INV_PI (0.318309886)` (`DiffuseBRDF = Albedo / π`) and enforce physical daylight EV ratio (`Sun ≈ 6×` ambient sky/room bounce). |
| **2** | Analytic `LinearCosineLightIntegration` (`LTC`) overhead strips evaluated in closed form with `0` occlusion rays, shining straight through the solid car chassis onto the floor beneath. | Gate `LTC` diffuse and specular terms by a **1-ray deterministic `WideBoundingTree` luminaire visibility test + Bent-Normal cone occlusion**. |
| **3** | `1.63 m` sensor grid spacing (`z = 0.42 m` lowest layer) interpolated bright exterior probes underneath the `0.25 m` vehicle floor pan. | Store **`16×16` octahedral `(r, r²)` Chebyshev radial-depth moments** per irradiance sensor so occluded probes across solid walls/chassis receive zero interpolation weight. |
| **4** | Short `0.55 m` contact AO (`min = 0.25`) missed the `1.9 m`-wide chassis underbody and left grazing floor Fresnel reflections (`~50%`) unoccluded inside shadows. | Use **two-scale deterministic `WideBoundingTree` AO (`0.35 m` tyre contact + `2.2 m` chassis ground shadow, `min = 0.02`)** plus Lagarde–de Rousiers **Specular Occlusion (`specAO`)**. |
| **5** | Default `Sun Area Radius = 2.10` (`~11°` softbox cone) smeared shadows across `0.35 m`. | Calibrate default solar angular radius to **`0.53°–1.8°`** (`SolarDisc` representative-point highlight + plane-aware `PenumbraFilter`). |

---

## 3. Complete 10-Lobe Physical `MaterialSpace` Architecture (`C++20` + `.slang`)

Auditing `Frontier-OLD` (`SurfaceMaterial.h`, `ScatteringSequence.h`, `AutomotivePaint.h`, `MaterialAndFlakesReport.md`, and `MaterialGlintResearch.md`) confirms ~80% of the authoring foundation exists, while unifying `OpenPBR Surface 1.1` (ASWF 2025) + Kemppinen et al. (SIGGRAPH Asia 2025) completes **every physical material class**:

| Lobe # | Physical Material Family | Governing Optical Formulation (2024–2026 State of the Art) | Example Synthesized Materials |
| :---: | :--- | :--- | :--- |
| **L1** | **Rough & Porous Diffuse Solids** | **Energy-Preserving Oren-Nayar (`EON` — Portsmouth, Kutz & Hill 2025)** + `CLTC` importance sampling | Concrete, dry asphalt, clay, chalk, brick, matte rubber, lunar regolith |
| **L2** | **Metals & Brushed Alloys** | **Anisotropic Spherical-Cap VNDF GGX** (Dupuy–Benyoub 2023) + **`F0 / F82` Edge-Tint Fresnel** + **Turquin (2019) Multi-Scatter Energy Compensation** (`4 KB` `DirectionalAlbedoSheet`) | Gold, copper, brushed steel, machined aluminium, brake rotors, chrome |
| **L3** | **Clearcoat, Lacquer & Tinted Varnish** | Dielectric coat (`IOR`, roughness, anisotropy, independent orange-peel normal), **underlying roughness widening `(r_u⁴ + 2 r_c⁴)^¼`**, **internal TIR darkening**, and **Beer–Lambert coat absorption** | Automotive clearcoat, piano lacquer, varnished wood, resin-coated carbon fiber |
| **L4** | **Thin-Film Wave-Optics Iridescence** | **Belcour & Barla (SIGGRAPH 2017)** nanometer Airy interference (`0–2000 nm`, `film_ior`) — full Gaussian CIE spectral fit on primary hits, fast 3-channel RGB Airy on secondary bounces | Heat-blued titanium exhausts, oil slicks, soap bubbles, anodized trim, lens coatings |
| **L5** | **Metallic Flakes, Carpaints & Mineral Sparkles** | **Constant-Time 4D Multiscale Glinty NDFs** (Kemppinen et al., SIGGRAPH Asia 2025): fixed `2 LOD × 4×4` Gaussian evaluations independent of flake count (`10³–10⁹`), exact far-field GGX convergence + valid sampling PDF, driven by `12`-stop ramp/burial/peel parameters | Metallic & pearlescent car paint, snow crystals, micaceous granite, sparkly asphalt |
| **L6** | **Refractive Dielectrics, Frosted Glass & Glazing** | **Walter Rough Microfacet GGX BTDF** + volumetric Beer–Lambert `(σ_a, σ_s)` + **Abbe Number `V_d` Cauchy dispersion**, supporting both **`EnableThinWalledSlab`** (windshields/panes) and solid volumes | Windshields, architectural glass, frosted glass, water, crystal, prisms, headlamp optics |
| **L7** | **Subsurface Scattering & Translucent Organics** | **Tier 1 GTX:** Christensen–Burley (2015) Normalized Diffusion + separable screen-space SSS + thin diffuse transmission.<br>**Tier 2 RTX:** Deterministic Owen-Sobol Quasi-Monte-Carlo volumetric walk / Dwivedi-guided SSS | Human skin, wax, marble, jade, milk, silicone, backlit foliage & paper |
| **L8** | **Cloth, Velvet, Microfiber & Dust** | **Zeltner, Burley & Chiang (SIGGRAPH 2022) `LTC Sheen`** (`4 KB` `SheenLinearCosineSheet`) with exact `(1 - E_sheen)` substrate energy scaling | Velvet, Alcantara interiors, denim, satin, felt, dusty surfaces |
| **L9** | **Hair, Fur & Technical Fibers** | **Chiang et al. (2016) / Marschner Dual-Scattering BCSDF** (`R`, `TT`, `TRT` lobes, cuticle tilt `α`, eumelanin/pheomelanin absorption) | Hair, fur, stitched thread, woven aramid & exposed fiber bundles |
| **L10** | **Porosity Wetness & Blackbody Radiosity** | Dynamic relative `IOR / 1.333` porous darkening under water film + **Planckian Kelvin (`1000 K–12000 K`) & LM-63 `.ies` photometric luminous flux** | Rain-wet concrete/cloth/timber, glowing brake discs, LED clusters, filament lamps |

### Two-Tier Bindless GPU Compilation (`Zero G-Buffer Bloat`)
1. **`CompactMaterialEntry` (`128 B`, indexed by `uint16 MaterialSlot`):** Carries `uint32 ActiveLobeMask` so scalar wave-uniform branching (`WaveActiveBitOr(ActiveLobeMask)`) executes **only** active lobes per tile/wavefront.
2. **`BounceSlabEntry` (`32 B`, indexed by `uint16 MaterialSlot`):** Pre-integrated `(MeanAlbedo, EffectiveRoughness, MetallicF0, LuminousFlux)` evaluated in **5 ALU ops** on secondary `IrradianceLattice` and `2nd/3rd` reflection bounces.
3. **Dual C++20 / `.slang` Compilation (`FRONTIER_CPU_VERIFICATION`):** Identical scattering source compiles in CTest to enforce White-Furnace energy conservation (`∫ f cos θ dω ≤ 1`), Helmholtz reciprocity, and PDF normalization.

---

## 4. Hybrid `64-bit` Visibility Rasterization (`GTX` + `RTX`) & No-`VSM` Shadow Verdict

### 4.1 Primary Camera Front-End: `64-bit` (`8 B/px`) Hybrid Visibility Rasterization
- **Used in BOTH Tier 1 GTX and Tier 2 RTX:** Primary camera rays share a single origin (100% coherent). Two-phase `DepthPyramid` (`Hi-Z`) cluster culling + `VisibilityClusterRasterization` resolves primary hits in **`~0.4–0.7 ms`** (`3×–6×` faster than primary `BVH` rays) while storing only **`8 bytes/pixel`** (`uint64`: `ReversedZDepth32 | VisibilityToken32`) instead of a `>100 B/px` G-Buffer.
- **Analytic Barycentrics (`4×` Bandwidth Reduction vs. `Frontier-OLD`):** `AnalyticBarycentricResolve.slang` projects the 3 fetched face corners onto the camera ray to compute exact `(λ₀, λ₁, λ₂)` and screen-space UV derivatives `(∂u/∂x, ∂v/∂y)` without storing a separate `OpaqueBary` texture.
- **Hardware vs. Compute Micro-Face Split:** Clusters with projected face area `>= 4 px` dispatch via `vkCmdDrawIndexedIndirectCount` (hardware rasterizer); sub-pixel clusters `< 4 px` dispatch via `MicroFaceSoftwareRasterization.slang` (`atomicMax64`, avoiding `2×2` quad helper-lane waste) into the **exact same `uint64` target**.

### 4.2 Shadow Architecture Verdict: **Skip Virtual Shadow Maps (`VSM`)**
- **Why `VSM` Is Rejected for Frontier:** `16k×16k` paged `VSM` consumes **`48–96 MB` VRAM**, suffers **`5–12 ms` full-cache invalidation spikes** whenever solar ephemeris (`Sun Azimuth`) rotates or fast vehicles (`ControlVehicle`) / fracture debris cross tile boundaries, and cannot render colored transmissive glass shadows.
- **Adopted Dual-Tier Shadow Solution:**
  - **Tier 2 RTX:** `0 MB` extra memory — **100% Raytraced Deterministic Solar Frustum** (`1–4` `WideBoundingTree` / `VK_KHR_ray_query` rays + `SolarDisc` + `PlaneAwareShadowFilter`).
  - **Tier 1 GTX:** Fixed **`16 MB` VRAM**, zero page thrashing — **2-Cascade Tight Reversed-Z Contact Clip Projection (`0–8 m` @ `3.9 mm/texel` + `8–35 m`)** blended seamlessly into **Analytical `SignedDistanceVolume` Cone Penumbra (`35–500 m`)**, while also feeding volumetric fog shafts.

---

## 5. Canonical New Folder Structure (**New Folders Only**)

Every folder and file name below is verified against [../BannedWordsVocabulary.md](../BannedWordsVocabulary.md) and [../AgenticInstructions/SKILL-Naming.md](../AgenticInstructions/SKILL-Naming.md) (`0` banned or discouraged terms):

```text
Frontier/
├── Engine/
│   ├── MaterialSpace/                                 // NEW: Universal 10-lobe OpenPBR 1.1 + Glinty physical materials
│   │   ├── SurfaceMaterial.{h,cpp}                    // Root: authored material definition & validation
│   │   ├── CompactMaterialRegister.{h,cpp}            // Root: 128 B bindless GPU register + ActiveLobeMask
│   │   ├── MaterialSurfaceLayer/                      // Ordered 8-layer stack layout & 32 B bounce summary
│   │   │   ├── MaterialSurfaceLayer.{h,cpp}           // Aligned physical surface layer parameters & blending
│   │   │   └── BounceLayerSummary.{h,cpp}             // Pre-integrated 32 B secondary-bounce summary compiler
│   │   ├── Scattering/                                // 10 physical optical lobes (shared C++20 & .slang)
│   │   │   ├── OpenPbrSurfaceScattering.h             // Master 10-lobe evaluation & mixture PDF
│   │   │   ├── EnergyPreservingOrenNayar.h            // Portsmouth 2025 EON rough diffuse + CLTC sampling
│   │   │   ├── MetallicFresnelTint.h                  // Anisotropic VNDF GGX, F0/F82 metal tint & exact dielectric Fresnel
│   │   │   ├── ClearcoatAbsorption.h                  // Coat roughening, TIR darkening & Beer-Lambert absorption
│   │   │   ├── ThinFilmInterference.h                 // Belcour & Barla spectral/RGB Airy nanometer interference
│   │   │   ├── GlintMicroflakeScattering.h            // Kemppinen 2025 constant-time 4D multiscale glinty microflakes
│   │   │   ├── DielectricTransmission.h               // Thin-walled glazing & solid rough GGX BTDF + Abbe dispersion
│   │   │   ├── SubsurfaceDiffusion.h                  // Burley normalized diffusion + deterministic QMC volumetric walk
│   │   │   ├── LinearlyTransformedSheen.h             // Zeltner 2022 LTC multiple-scattering cloth/velvet sheen
│   │   │   ├── FiberScattering.h                      // Chiang/Marschner R + TT + TRT hair and fiber BCSDF
│   │   │   └── PorosityAndBlackbody.h                 // Relative-IOR wetness darkening & Planckian Kelvin luminous flux
│   │   ├── EnergyCompensation/                        // 4 KB & 8 KB pre-integrated hemispheres
│   │   │   ├── DirectionalAlbedoIntegral.{h,cpp}      // 32x32 RG16F (4 KB) Turquin multi-scatter energy preservation
│   │   │   └── SheenCosineIntegral.{h,cpp}            // 32x32 RGBA16F (8 KB) Zeltner LTC sheen fit coefficients
│   │   └── Baking/                                    // Procedural outputs & shader-ball previews
│   │       ├── ProceduralChannelBaker.{h,cpp}         // Deterministic procedural normal/roughness/albedo pyramid baker
│   │       └── MaterialSpecimenRasterizer.{h,cpp}     // Shader-ball & draped-cloth preview thumbnail rasterizer
│   │
│   ├── SpatialAcceleration/                           // NEW: Unified cluster LOD pyramid & 4-wide ray acceleration
│   │   ├── ClusterPyramid.{h,cpp}                     // 128-face PolygonNetwork LOD pyramid with locked seam/crease borders
│   │   └── WideBoundingTree.{h,cpp}                   // Compressed 128 B 4-Wide BVH4 + optional VK_KHR_acceleration_structure
│   │
│   ├── DistanceFieldSpace/                            // NEW: Volumetric signed distance fields for Tier 1 GTX GI & penumbra
│   │   └── SignedDistanceVolume.{h,cpp}               // Sparse brick + clip-grid 3D SDF builder & gradient encoder
│   │
│   └── GeometricRaster/
│       ├── Visibility/                                // NEW: Shared GTX/RTX 64-bit (8 B/px) primary visibility front-end
│       │   ├── DepthPyramidSequence.{h,cpp}           // Conservative min-depth Hi-Z mip chain sequence
│       │   ├── ClusterCullEncoder.{h,cpp}             // Two-phase Hi-Z cull splitting >=4 px HW draws vs <4 px SW compute
│       │   ├── VisibilityClusterRasterization.{h,cpp} // uint64 (Depth32 | VisibilityToken32) + 2-layer glass depth peel
│       │   └── VisibilityMaterialResolve.{h,cpp}      // Analytic ray-differential barycentrics & LobeMask tile resolve
│       │
│       ├── DirectLighting/                            // NEW: First-hit solar shadows, analytic area lights & chassis AO
│       │   ├── SolarShadowSequence.{h,cpp}            // RTX deterministic solar frustum + GTX 2-cascade contact clip + SDF
│       │   └── LinearCosineLightIntegration.{h,cpp}   // Visibility-gated analytic polygonal lights + 2.2 m chassis AO
│       │
│       └── GlobalIllumination/                        // NEW: Multi-bounce GI, octahedral irradiance lattice & RT reflections
│           ├── IrradianceLatticeSequence.{h,cpp}      // RTX 1/pi octahedral lattice (Chebyshev depth) + GTX SDF cone GI
│           ├── SpecularReflectionSequence.{h,cpp}     // Coherent WideBoundingTree multi-bounce specular & glass refraction
│           └── TieredWorldRenderSequence.{h,cpp}      // Complete ordered GTX/RTX world sequence bound to viewports
│
└── Shaders/                                           // NEW: Modular Slang (.slang) GPU shader domains
    ├── MaterialSpace/
    │   ├── OpenPbrSurfaceScattering.slang             // Master 10-lobe BSDF (compiles in both SPIR-V and C++20 CTest)
    │   ├── GlintMicroflakeScattering.slang            // Constant-time 4D multiscale glint NDF + ramp tinting
    │   ├── ThinFilmInterference.slang                 // Airy wave-optics iridescence
    │   ├── LinearlyTransformedSheen.slang             // LTC cloth/microfiber sheen
    │   ├── SubsurfaceAndTransmission.slang            // Thin/solid rough refraction, dispersion & SSS
    │   ├── FiberScattering.slang                      // Hair/fiber longitudinal & azimuthal BCSDF
    │   └── BounceLayerSummary.slang                   // Fast 5-ALU secondary-bounce surface shading
    ├── Visibility/
    │   ├── ClusterVisibilityCull.slang                // Two-phase frustum, normal-cone & DepthPyramid occlusion cull
    │   ├── DepthPyramidDownsample.slang               // Conservative reversed-Z min reduction
    │   ├── HardwareVisibilityRasterization.slang      // >=4 px indirect cluster rasterization + 2-layer glass peel
    │   ├── MicroFaceSoftwareRasterization.slang       // <4 px compute atomicMax64 sub-pixel face rasterization
    │   └── AnalyticBarycentricResolve.slang           // Ray-plane barycentrics, UV Jacobians & wave-uniform material shade
    ├── DirectLighting/
    │   ├── WideTreeRayQuery.slang                     // Shared 4-Wide BVH4 & VK_KHR_ray_query intersection routines
    │   ├── SolarFrustumShadow.slang                   // Deterministic Vogel solar disc trace + chassis 2-scale AO
    │   ├── ContactClipShadow.slang                    // GTX 2-cascade tight contact shadow + SDF cone penumbra blend
    │   └── PlaneAwareShadowFilter.slang               // Coplanar dilation + 3-step continuous-sigma edge-preserving filter
    └── GlobalIllumination/
        ├── OctahedralIrradianceLattice.slang          // 64-ray Fibonacci update, 16x16 Chebyshev depth & 1/pi lookup
        ├── SignedDistanceConeIllumination.slang       // GTX 6-cone directional SDF diffuse irradiance
        └── SpecularAndGlassRaytrace.slang             // Coherent cone-footprint multi-bounce specular & dielectric glass
```

---

## 6. Dependency-Ordered Starting Sequence ("What We Should Start With")

Start from the **bottom of the dependency graph upward** so every step compiles cleanly, passes CTest + `.png` visual validation in `Exhibits/`, and never has to be rewritten by a later step:

| Order | Milestone | Why It Comes Here | Deliverable & `Exhibits/` Visual Proof |
| :---: | :--- | :--- | :--- |
| **Step 1 (START HERE)** | **`Engine/MaterialSpace/` + `Shaders/MaterialSpace/`** | Every downstream pass (`VisibilityMaterialResolve`, `IrradianceLattice`, `SpecularReflection`, `ContentBrowser` thumbnails) reads `CompactMaterialEntry` (`128 B`) and `BounceSlabEntry` (`32 B`). Dual C++20/`.slang` compilation lets us prove 100% physical energy conservation *before* touching scene passes. | All 10 physical lobes + `4 KB`/`8 KB` LUT sheets + `.MaterialSpace` binary codec + `Exhibits/MaterialScatteringValidation.cpp` (**White-Furnace, Reciprocity & 10-Material Shader-Ball `.png` Suite**). |
| **Step 2** | **`Engine/SpatialAcceleration/` + `DistanceFieldSpace/`** | Both Visibility Culling and Raytracing require prebuilt geometry structures from `PolygonNetwork`: `128`-face `ClusterPyramid` (for raster), `4-Wide` `WideBoundingTree` (`BVH4` for GTX/RTX rays), and `SignedDistanceVolume` (for GTX GI/penumbra), plus continuous Duff/Mikk tangents. | Multithreaded builders + `.GeometrySpace` section serialization + `Exhibits/SpatialAccelerationValidation.cpp` (**LOD Hausdorff bounds + `BVH4` heatmaps**). |
| **Step 3** | **`Engine/GeometricRaster/Visibility/` + `Shaders/Visibility/`** | Establishes the shared **`64-bit` (`8 B/px`) Primary Camera Front-End** for both GTX and RTX: Two-Phase `DepthPyramid` (`Hi-Z`) cull, Hardware (`>= 4 px`) + Compute `atomicMax64` (`< 4 px`) visibility raster, 2-layer glass peel, and analytic ray-differential resolve calling Step 1's `MaterialSpace`. | Zero-overdraw primary visibility + analytic UV Jacobians feeding Kemppinen `Glinty` + `Exhibits/VisibilityRasterValidation.cpp`. |
| **Step 4** | **`SolarShadowSequence` + `LinearCosineLightIntegration` (No `VSM`)** | Plugs directly into resolved primary hits to deliver **crisp, calibrated direct lighting & contact shadows**: RTX deterministic `SolarDisc` + Vogel frustum + `PlaneAwareShadowFilter`, GTX 2-cascade `3.9 mm` contact clip + `SDF` penumbra, visibility-gated `LTC` lights, and `2.2 m` chassis AO + `specAO`. | Eliminates all shadow washout across both tiers + `Exhibits/SolarShadowValidation.cpp`. |
| **Step 5** | **`IrradianceLatticeSequence` + `SpecularReflectionSequence` + `TieredWorldRenderSequence`** | Completes full indirect global illumination (`1/π`-normalized `OctahedralIrradianceLattice` with `16×16` Chebyshev depth on RTX; `SignedDistanceVolume` cones on GTX) + multi-bounce `WideBoundingTree` reflections & glass refraction, wired live into `FrontierEditor`'s `ViewportProjectionPane`. | Full interactive GTX + Deterministic RTX world viewport in `FrontierEditor` + `Exhibits/TieredWorldRenderValidation.cpp`. |

# Frontier Deformable XPBD, 3D Volumetric FEM & Vertex Motion Atlas Architecture Plan

> **Status**: Active Implementation (`D01`–`D08`)
> **Branch**: `arena/6e2e878f-frontier`
> **Reference Prototype**: `Exhibits/XpbdStrandStudio/index.html` (verified 9-preset WebGL2 prototype: `1,080`-tet Stable Neo-Hookean 3D FEM Tire, `48`-sector Pneumatic XPBD Tire, Botanical Ferns, Orb Spiderweb, Cables/Chains, Hanging Vines, Marsh Reeds, Underwater Kelp, Woven Cloth, shared `12,544`-tri sculpted sport tire GPU cage embedding, and `64`-slice `RGBA32F` Vertex Motion Atlas baker/player).

---

## 1. Architectural Stratification

1. **Unified Deformable Physics (`Engine/SimulationSpace/Deformable/`)**
   - **XPBD** (*Extended Position-Based Dynamics*) is the unified compliance-based time-integration and constraint-projection solver.
   - **3D Volumetric FEM** (*Macklin & Müller 2021 Stable Neo-Hookean Tetrahedra*) is a constitutive 3D elastic constraint family executed inside the same XPBD substep loop alongside 1D strands/cables/chains, 2D orthotropic cloth, botanical posture springs, and pneumatic shells.
   - Couples cleanly to `SimulationSpace/PhysicsSpace/` (`BroadphaseSolver`, `VehicleDynamicsSolver` axles) and `SimulationSpace/ForceFields/` (`ForceFieldRegister`, `ForceFieldSampler`).
2. **Solver-Agnostic High-Poly Deformation & VAT (`Engine/GeometrySpace/Deformation/`)**
   - Strictly decoupled from physics solvers so the **exact same high-poly `PolygonNetwork`** (e.g., the `7,552`-vertex / `12,544`-triangle sculpted sport tire) can be deformed by:
     - **Live 3D FEM Tetrahedral Barycentric Cage** + **Nanson's exact 3D cofactor normal matrix** ($\operatorname{cof}(\mathbf{F}) = J \mathbf{F}^{-T}$),
     - **Live Pneumatic XPBD Bilinear Quad Cage** + **signed normal extrusion & local tangent basis**, or
     - **Baked 0-CPU Vertex Motion Atlas (VAT)** sampled via `texelFetch` with sub-slice interpolation and baked strain in `.a`.
3. **Native GPU Compute & Vertex Shaders (`Engine/Shaders/Deformable/`)**
   - All shaders authored in `.slang` targeting Vulkan 1.3 SPIR-V with exact 16-byte struct parity verified via C++20 `static_assert`.
   - Zero-atomic parallel constraint projection via greedy graph coloring (`ConflictColorPartitioner`).
4. **Two-Tier TOML + Binary Persistence (`Engine/Codecs/Simulation/`)**
   - **`.SoftBody`** *(temporary suffix; subject to future rename)*: TOML preset header + parameters + optional 64-byte aligned binary cage/tetrahedral rest buffers. Stores **all 9 studio presets** (3D FEM Tire, Pneumatic Tire, Botanical Ferns, Orb Spiderweb, Cables/Chains, Hanging Vines, Marsh Reeds, Underwater Kelp, Woven Cloth) under `EngineContent/Simulation/Deformables/*.SoftBody`.
   - **`.VertexAtlas`**: TOML bounds/packing header + 64-byte aligned binary `RGBA32F` / `RGBA16F` VAT texels + per-slice rigid axle poses + external `.png` + `.json` Unreal/Unity interchange export.

---

## 2. Canonical Folder & File Structure

```text
Frontier/
├── Documentation/Plans/
│   └── DeformableFemVatArchitecturePlan.md         // This authoritative architecture & phase ledger
├── Engine/
│   ├── SimulationSpace/
│   │   └── Deformable/                             // Unified XPBD + 3D Stable Neo-Hookean FEM Pillar
│   │       ├── README.md
│   │       ├── DeformableTypes.h                   // SOA MassPoints, Lamé (E, nu), strand/shell/tire/GPU UBO structs
│   │       ├── TetrahedralSolidBuilder.{h,cpp}     // Conformal 5-tet-per-hex decomposition, Dm^-1 & rest volume V0
│   │       ├── ConflictColorPartitioner.{h,cpp}    // Zero-atomic greedy batch coloring for tets, distance & bend edges
│   │       ├── XpbdConstraintSolver.{h,cpp}        // Unified substep loop: distance/tear, bending, posture, collisions
│   │       ├── NeoHookeanTetSolver.{h,cpp}         // Stable Neo-Hookean C_D + C_H, Nanson cof(F) & nodal strain scatter
│   │       ├── PneumaticTireSolver.{h,cpp}         // Co-rotating transport, Boyle cavity bulge, outward centrifugal growth
│   │       ├── DeformablePhysicsCoupling.{h,cpp}   // ForceFieldSampler wind/vortex + Jolt BroadphaseSolver & axle sync
│   │       └── DeformablePresetRegistry.{h,cpp}    // Exact C++20 builders for all 9 presets (tires + strands/plants/cloth)
│   ├── GeometrySpace/
│   │   └── Deformation/                            // Solver-Agnostic High-Poly Cage Embedder & VAT System
│   │       ├── README.md
│   │       ├── CageWeightTypes.h                   // 84-byte GPU vertex layout (rest normal, color, mat, 4 indices, 4 weights, extra)
│   │       ├── TireSurfaceBuilder.{h,cpp}          // Shared 7,552-vert / 12,544-tri sculpted automotive sport tire generator
│   │       ├── TetrahedralCageEmbedder.{h,cpp}     // Exact 3D barycentric embedding via Dm^-1 (maxRestErr < 1e-4 m)
│   │       ├── SurfaceQuadCageEmbedder.{h,cpp}     // Bilinear quad embedding + local tangent basis + height offset
│   │       ├── VertexMotionAtlasTypes.h            // 256-col tiled RGBA32F/RGBA16F VAT layout, bounds & per-slice axle poses
│   │       ├── VertexMotionAtlasBaker.{h,cpp}      // Deterministic 64-slice curb-roll/impact VAT baker (FEM or XPBD)
│   │       ├── VertexMotionAtlasPlayer.{h,cpp}     // 0-CPU playback cursor, sub-slice linear mix & axle pose interpolation
│   │       └── VertexMotionAtlasExporter.{h,cpp}   // Pure C++20 quantized RGBA8 PNG + JSON bounds exporter
│   ├── Shaders/
│   │   └── Deformable/                             // Native .slang Compute & Vertex-Stage Shaders (-> SPIR-V)
│   │       ├── README.md
│   │       ├── DeformableLayouts.slang
│   │       ├── XpbdPredictCompute.slang
│   │       ├── XpbdDistanceBendCompute.slang
│   │       ├── NeoHookeanTetCompute.slang
│   │       ├── DeformableCollisionCompute.slang
│   │       ├── DeformableCageSkinning.slang
│   │       ├── VertexMotionAtlasSampling.slang
│   │       └── VertexMotionAtlasBakeCompute.slang
│   ├── Codecs/Simulation/
│   │   ├── SoftBodyCodec.{h,cpp}                   // .SoftBody TOML + 64B-aligned binary codec
│   │   └── VertexAtlasCodec.{h,cpp}                // .VertexAtlas TOML + 64B-aligned binary codec
│   └── Serialization/Container/
│       └── SuffixRegistry.{h,cpp}                  // Tier-B .SoftBody (63u) & .VertexAtlas (64u)
├── EngineContent/Simulation/Deformables/           // All 9 presets saved as canonical TOML .SoftBody files
│   ├── SportTireFem.SoftBody
│   ├── SportTirePneumatic.SoftBody
│   ├── BotanicalFerns.SoftBody
│   ├── OrbSpiderweb.SoftBody
│   ├── CablesChains.SoftBody
│   ├── HangingVines.SoftBody
│   ├── MarshReeds.SoftBody
│   ├── UnderwaterKelp.SoftBody
│   └── WovenCloth.SoftBody
└── Exhibits/
    ├── DeformableSolverValidation.cpp              // CTest suite: D01-D03 + all 9 presets + spin/bulge/tear/coupling
    └── VertexMotionAtlasValidation.cpp             // CTest suite: D04-D07 embedder, VAT bake/play/export & codecs
```

---

## 3. Exact Mathematical & Implementation Parity Locks

1. **Stable Neo-Hookean 3D Volumetric FEM** (*Macklin & Müller 2021*):
   - $\mu = \frac{E}{2(1+\nu)}$, $\lambda = \frac{E\nu}{(1+\nu)(1-2\nu)}$, $\gamma = 1 + \frac{\mu}{\lambda}$.
   - Deviatoric shear: $C_D = \sqrt{\operatorname{tr}(\mathbf{F}^T\mathbf{F})}$, compliance $\alpha_D = \frac{1}{\mu V_0}$.
   - Hydrostatic volume preservation: $C_H = \det(\mathbf{F}) - \gamma$, compliance $\alpha_H = \frac{1}{\lambda V_0}$.
   - Nanson normal transform: store cofactor matrix $\operatorname{cof}(\mathbf{F})$ directly per tetrahedron; scatter $\frac{1}{4}(|C_D - \sqrt{3}| + |J - 1|\cdot 2.5)$ onto incident mass points.
2. **Co-Rotating Centrifugal Pneumatic Tires** (both 3D FEM & XPBD):
   - Exact 2D rigid transport $\mathbf{R}(\omega h)$ of positions & velocities per substep eliminates inward symplectic chord shrinkage at any RPM (`-20..+24 rad/s`).
   - Outward centrifugal growth $\propto \omega^2 / k_{\text{stiff}}$ + centrifugal hoop stiffening (`k + αω²`) + Boyle cavity pressure ($V_0 / V$) + rim keep-out.
3. **All 9 Presets in C++20 + `.SoftBody` TOML**:
   - Tires: `SportTireFem` (`504` pts / `1,080` tets), `SportTirePneumatic` (`432` pts / `48×9`).
   - Particle/Strand/Plant/Cloth Presets: `BotanicalFerns` (posture-elastic fronds + pinnae), `OrbSpiderweb` (tearable spokes + capture spiral + dewdrop beads), `CablesChains` (hanging cables + interlocking chain links), `HangingVines` (weeping tendrils + leaves), `MarshReeds` (posture-elastic stalks + cattails + grass blades), `UnderwaterKelp` (buoyant stipes + gas bladders + blades), `WovenCloth` (warp/weft/shear/bending tearable grid).
4. **Shared High-Poly Tire + GPU Cage + `RGBA32F`/`RGBA16F` VAT**:
   - `7,552` verts / `12,544` tris (`128` sectors × `35` profile stations + `320` beveled chevron tread blocks + `64` sidewall pads).
   - 2D tiled VAT (`width = 256`, `rowsPerSlice = ceil(V / 256) = 30`, `64` slices → `256 × 1920`).

---

## 4. Phased Implementation Ledger

| Phase | Scope | Status |
| :--- | :--- | :--- |
| **`D01`** | `DeformableTypes.h`, `TetrahedralSolidBuilder.{h,cpp}`, `ConflictColorPartitioner.{h,cpp}` + unit compile/test | ✅ Complete (`a832c98`) |
| **`D02`** | `NeoHookeanTetSolver.{h,cpp}`, `XpbdConstraintSolver.{h,cpp}` + Poisson/tear verification | ✅ Complete (`a832c98`) |
| **`D03`** | `PneumaticTireSolver.{h,cpp}`, `DeformablePhysicsCoupling.{h,cpp}`, `DeformablePresetRegistry.{h,cpp}` (all 9 presets) | ✅ Complete (`a832c98`) |
| **`D04`** | `CageWeightTypes.h`, `TireSurfaceBuilder.{h,cpp}`, `TetrahedralCageEmbedder.{h,cpp}`, `SurfaceQuadCageEmbedder.{h,cpp}` | ✅ Complete (`a832c98`) |
| **`D05`** | `VertexMotionAtlasTypes.h`, `VertexMotionAtlasBaker.{h,cpp}`, `VertexMotionAtlasPlayer.{h,cpp}`, `VertexMotionAtlasExporter.{h,cpp}` | ✅ Complete (`a832c98`) |
| **`D06`** | All 8 `.slang` shaders under `Engine/Shaders/Deformable/` + ABI parity check | ✅ Complete (`a832c98`) |
| **`D07`** | `SuffixRegistry` (`.SoftBody`, `.VertexAtlas`), `SoftBodyCodec.{h,cpp}`, `VertexAtlasCodec.{h,cpp}`, all 9 `EngineContent/Simulation/Deformables/*.SoftBody` files | ✅ Complete (`a832c98`) |
| **`D08`** | `Exhibits/DeformableSolverValidation.cpp`, `Exhibits/VertexMotionAtlasValidation.cpp`, `CMakeLists.txt`, full CTest & banned-word audit | ✅ Complete (`a832c98`) |
| **`D09`** | `Engine/GeometrySpace/Deformation/PolygonNetworkDeformer.{h,cpp}` — bidirectional Frontier `PolygonNetwork` <-> `HighPolySurfaceRest` / live cage & VAT deformation bridge | ✅ Complete |
| **`D10`** | `Engine/SimulationSpace/Deformable/DeformableGpuDispatch.{h,cpp}` + `VertexMotionAtlasBakeCompute.slang` + `Engine/Shaders/Deformable/README.md` — 16B-aligned GPU SSBO packer, lock-free color-batch dispatch plan & GPU VAT bake compute shader | ✅ Complete |
| **`D11`** | `Engine/SimulationSpace/Deformable/VehicleTireDeformBridge.{h,cpp}` — 4-wheel Jolt vehicle wheel-corner load/spin/thermal/curb bridge with distance-based Live FEM / Live XPBD / 0-CPU VAT LOD switching | ✅ Complete |
| **`D12`** | `Engine/SimulationSpace/Deformable/DeformableThreadExchange.{h,cpp}` + `Exhibits/DeformableIntegrationValidation.cpp` + `CMakeLists.txt` — wait-free `ThreadBoundaryExchange` physics/render publication & end-to-end integration validation | ✅ Complete (`efc25ff`) |
| **`D13`** | `Engine/GeometrySpace/Deformation/DeformableSurfaceBuilder.{h,cpp}` — unified C++20 procedural 3D render surface generator for ALL 9 presets (swept parallel-transport tubes, interlocking chain links, arched foliage blades, ellipsoidal beads, tear-aware woven cloth, forged alloy rims & props) | ✅ Complete |
| **`D14`** | `Engine/SimulationSpace/Deformable/DeformableVulkanExecutor.{h,cpp}` — native Vulkan 1.3 triple-buffered SSBO/image & compute dispatch executor (`#if __has_include(<volk.h>)`) with deterministic headless staging mirror | ✅ Complete |
| **`D15`** | `Engine/SimulationSpace/Deformable/DeformableWorldRegistry.{h,cpp}` — multi-instance world deformable registry with world transforms, distance-based LOD/sleep culling, shared `ForceFieldSampler` & Jolt `RigidColliderProxy` sweep | ✅ Complete |
| **`D16`** | `Exhibits/DeformableWorldValidation.cpp` + `Exhibits/DeformableVisualRenderer.cpp` + `Exhibits/DeformableRenders/ContactSheet_All9SoftBodyPresets.png` — C++20 world/Vulkan validation (`33/33` PASS; `156/156` deformable suite PASS) + direct C++20 PBR visual renders of all 9 `.SoftBody` presets | ✅ Complete (`52aab40`) |
| **`D17`** | `Exhibits/VertexAtlasBakeTool.cpp` + shipped `EngineContent/Simulation/Deformables/{SportTireFem,SportTirePneumatic}.VertexAtlas` binary VAT assets (`7,552` verts × `16` slices `RGBA32F`, `3,841 KiB` each, FNV-1a verified) | ✅ Complete |
| **`D18`** | `Engine/SimulationSpace/Deformable/VehicleDeformableCoupling.{h,cpp}` — native 4-wheel `VehicleReading` ↔ `VehicleTireDeformBridge` coupling & concentric world-space tire/rim/chassis surface assembly | ✅ Complete |
| **`D19`** | `Engine/SimulationSpace/Deformable/DeformableExtension.{h,cpp}` — official Frontier `Lifecycle::SuspendableParticipant` runtime extension uniting `DeformableWorldRegistry`, `DeformableVulkanExecutor`, `DeformableThreadExchange` & `VehicleDeformableCoupling` | ✅ Complete |
| **`D20`** | `Exhibits/DeformableRuntimeExtensionValidation.cpp` (`23/23` PASS; `179/179` deformable suite PASS) + `Exhibits/DeformableRenders/Vehicle4WheelTireDeformation.png` — `LifecycleSequence` runtime test, shipped `.VertexAtlas` validation & direct C++20 4-wheel deformed vehicle tire PBR render | ✅ Complete (`9dedcfc`) |
| **`D21`** | `Engine/SimulationSpace/Deformable/VehicleReadingConverter.{h,cpp}` — direct `PhysicsSpace::VehicleReading` (Z-up) -> `VehicleChassisCouplingInput` converter + two-way deformable -> Jolt `RigidImpulseAccumulatorEntry` impulse feedback | ✅ Complete |
| **`D22`** | `Engine/SimulationSpace/SimulationCoordinatorExtension.{h,cpp}` — unified Frontier `Lifecycle::SuspendableParticipant` orchestrating `ForceFields` -> `Fluid` -> `ParticleSpace` -> `Deformables` + Jolt `RigidColliderProxy` coupling in deterministic upstream order | ✅ Complete |
| **`D23`** | `Exhibits/UnifiedSimulationCouplingValidation.cpp` (`18/18` PASS; `507/507` total simulation suite PASS) + `Exhibits/DeformableRenders/UnifiedMultiDomainScene.png` — full 5-domain coupled world validation & direct C++20 composite PBR + volumetric render proof | ✅ Complete (`07ad6b1`) |
| **`D24`** | `CMakeLists.txt` — `frontier_compile_deformable_slang_shader(...)` SPIR-V compilation rules wiring all 9 executable `Engine/Shaders/Deformable/*.slang` compute/vertex/fragment shaders into `FRONTIER_SPV_OUTPUTS` alongside `ForceFields`, `ParticleSpace`, and `Fluid` | ✅ Complete |
| **`D25`** | `Engine/SimulationSpace/SimulationSceneDocument.{h,cpp}` + `EngineContent/Simulation/Scenes/TracksideCoupledShowcase.VolumetricSpace` — pure-TOML unified 5-domain scene serializer/deserializer (`ForceFields`, `Fluid`, `ParticleSpace`, `.SoftBody`, 4-wheel Vehicle/`.VertexAtlas`, Jolt `RigidColliderProxy`) & one-call `MountIntoCoordinator(...)` | ✅ Complete |
| **`D26`** | `Exhibits/SimulationSceneValidation.cpp` (`17/17` PASS; `524/524` total simulation suite PASS) — pure-TOML byte-identical round-trip, mounted 5-domain `LifecycleSequence` execution & static CMake ↔ `.slang` entry-point parity verification | ✅ Complete (`d267b51`) |
| **`D27`** | `Engine/ComponentClass/{ComponentInstanceCategories.h,ComponentInstanceStorage.{h,cpp}}`, `Engine/Codecs/ComponentSpaceCodec.cpp`, `Engine/Workspaces/ComponentEditor/ComponentTreePanel.cpp` — 4 new simulation POD categories (`SoftBodyInstance`, `ForceFieldInstance`, `FluidRegionInstance`, `ParticleCloudInstance`) with contiguous storage, duplication, `.ComponentSpace` codec serialization & `-Werror=switch` UI parity | ✅ Complete |
| **`D28`** | `Engine/SimulationSpace/ComponentClassSimulationAssembler.{h,cpp}` — prefab-to-simulation assembler composing nested transforms, `ComponentWorldPlacementRegister` world placements & non-destructive `InstanceOverrideDelta` transform/visibility overrides into `SimulationSceneDocument` & live `SimulationCoordinatorExtension` | ✅ Complete |
| **`D29`** | `Exhibits/ComponentClassSimulationValidation.cpp` (`13/13` PASS; `537/537` total simulation suite PASS) — verifies all 4 simulation POD categories, multi-placement world instancing with transform/visibility overrides, TOML round-trip & live 5-domain coupled execution inside `LifecycleSequence` | ✅ Complete (`8f493fe`) |
| **`D30`** | `Engine/ComponentClass/ComponentClassValidator.cpp` — structural & disk-asset validation rules across all 4 simulation POD categories (`SoftBodyInstance`, `ForceFieldInstance`, `FluidRegionInstance`, `ParticleCloudInstance`) verified by `ComponentClassSimulationValidation` (`15/15` PASS; **`539/539`** total simulation suite PASS) | ✅ Complete |
| **`D31`** | `Engine/Workspaces/ComponentEditor/{ComponentTreePanel.cpp,ComponentDetailsPanel.cpp}` — `+ Add` dropdown `SIMULATION` section (`Soft Body`, `Force Field`, `Fluid Region`, `Particle Cloud`), category accent tints, and interactive Details Panel inspector slider sections for all 4 simulation component types | ✅ Complete (`ff887fc`) |
| **`D32`** | `Engine/SimulationSpace/ComponentSimulationPreviewDriver.{h,cpp}` — live Play/Pause/Resume/Stop/Reset 5-domain simulation preview driver bridging any `ComponentClassDefinition` to `SimulationCoordinatorExtension` inside `Lifecycle::LifecycleSequence` with on-demand `ProceduralRenderSurface` deformable mesh extraction | ✅ Complete |
| **`D33`** | `Engine/Workspaces/ComponentEditor/ComponentWorkspaceSession.{h,cpp}` + `Exhibits/ComponentSimulationPreviewValidation.cpp` (`14/14` PASS; `553/553` total simulation suite PASS) — wires live 5-domain `SimulationPreview` into Component Editor `ToggleSimulation`/`StepLiveSimulation`/`ResetMotionKinematics` and verifies Play/Pause/Resume/Surface Extraction/Stop/Restart | ✅ Complete (`e22972d`) |
| **`D34`** | `Engine/SimulationSpace/WorldSimulationSession.{h,cpp}` — unified FrontierWorld & Play-in-Editor 5-domain runtime session launching directly from `EditorLifecycle` multi-class `ComponentDefinitions` + `WorldPlacements` OR from a `.VolumetricSpace` scene file on disk | ✅ Complete |
| **`D35`** | `Engine/Lifecycle/EditorLifecycle.{h,cpp}` + `Exhibits/WorldSimulationSessionValidation.cpp` (`12/12` PASS; **`565/565`** total simulation suite PASS) — wires `WorldSimulationSession` Play/Pause/Resume/Stop + per-tick camera-tracked stepping & teardown into `EditorLifecycle` and completes the full end-to-end native 5-domain simulation engine stack (`D01`–`D35`) | ✅ Complete |

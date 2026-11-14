# Frontier Physics Engine, Deformables, Fracture & Real-Cloth Architecture Plan

> **TL;DR:** Frontier unifies rigid, fracture, softbody, cloth, particle, and fluid simulation on **Z-up meters** (`Axis3D`, $g = -9.80665\,\text{m/s}^2$) by pairing **JoltPhysics v5.6.0 (`CROSS_PLATFORM_DETERMINISTIC ON`)** on `PhysicsThread` with **deterministic GPU `.slang` elasticity solvers** (`DeformableSpace` `3D Hex8/Tet4` elasticity + `XPBD` + **`~50`-point `ProxyCageDeformation`**, and `ClothSpace` real-fabric **2D Orthotropic Membrane + `AVBD` + `OGC`** tailoring). GPU determinism is guaranteed without floating-point race conditions via cook-time `ConstraintBatchColoring`, sorted per-point cell gather, micrometre integer contact accumulators, and `[precise]` IEEE-754 arithmetic. **Fracture**, **Softbody**, and **Cloth** authoring suites share the exact Frontier / `UVWorkspace` split-viewport layout (`Outliner | 2D Pattern/Slice + 3D Simulation | Inspector | ContentBrowser`).

---

## 1. Domain Responsibility Matrix: Jolt vs. Custom GPU `.slang` Solvers

*(Legend — Determinism / Coupling Precision: 🔴 = bit-exact cross-platform, 🚩 = deterministic per-device/seed, ✔️ = visual replay)*

| Simulation Domain | Solver Host & Module | Determinism Tier | Network Replication | Coupling & Render Mechanism |
| :--- | :--- | :---: | :--- | :--- |
| **Rigid Bodies, Joints, Vehicles & Character Capsules** | **`PhysicsSpace`** — **JoltPhysics v5.6.0** on `PhysicsThread` (CPU, SSE4.2 / ARM64 NEON, `USE_FMADD OFF`) | 🔴 **Bit-Exact** | ✔️ Optional (`WorldSnapshotRecorder` rollback or `DifferentialWorldEncoder`) | Writes unit `Quaternion` + `Translation` directly into GPU instance buffers (`0` Euler round-trips). |
| **Tier A: Physical Debris Particles (`<= 2,048` active)** | **`PhysicsSpace`** — Pooled Jolt `ConvexHullShape` / `SphereShape` bodies with aggressive island sleep | 🔴 **Bit-Exact** | ✔️ Optional (delta poses or deterministic impact seed) | Full two-way rigid contact, restitution, and rolling friction inside Jolt. |
| **Tier B: High-Density Visual Particles (`100k–2M`)** | **`ParticleSimulation`** — GPU `.slang` compute (`ParticleIntegration.slang`) seeded by `DeterministicRandom` | ✔️ **Seeded Visual Replay** | ✔️ Event directive only (`32 B`: origin, velocity, seed) | **Strictly one-way:** reads Jolt velocities + `DistanceFieldSpace` SDFs + wind; **never** writes forces into Jolt so rigid determinism never drifts. |
| **Volumetric Fluid (Gas, Smoke, Fire & SPH/FLIP Liquid)** | **`FluidSimulation`** — **Separate from Jolt**; GPU `.slang` MAC Eulerian grid + SPH/FLIP liquid (`.VolumetricSpace`) | 🚩 **Authoring Deterministic** | ✔️ Source directives + optional quantized buoyancy | **`FluidBoundaryExchange`:** Jolt colliders voxelize into solid velocity boundaries; optional fluid buoyancy/drag sums over sorted sample points in integer micronewtons (`1e-6 N`). |
| **Structural Fracture (`FractureSpace` + Jolt Split)** | **`FractureSpace`** (6 solvers from `FractureEditor`) + **Jolt** (`MutableCompoundShape` → `ConvexHullShape`) | 🔴 **Bit-Exact** | ✔️ `128 B` `FractureImpactDirective` (point, impulse, PRNG seed) + chunk poses | `BondConnectivityRegister` severs strained bonds in ascending index order, splits Jolt compound islands, and rebuilds `32³/64³/128³` `R16F` SDF bricks. |
| **Vehicle Crush Cage (`~50` Pts) & Volumetric Tyres (`32×5`)** | **`DeformableSpace`** — `Hex8SolidElasticity`, `CurvedMembraneElasticity` & `XPBDConstraintSolver` | 🔴 **Bit-Exact** (CPU & `.slang` parity) | ✔️ Compact proxy lattice (`< 8 KiB` per vehicle in rollback ring) | **`ProxyCageDeformation.slang`** skins `1k–50k` triangle `PolygonNetwork` surfaces on GPU from `~48–64` proxy points (`0` CPU surface loops). |
| **Volumetric Softbodies (`Tet4` / `Hex8` & Inflatables)** | **`DeformableSpace`** — Corotated & Neo-Hookean `Tet4`/`Hex8` elasticity + pneumatic cavity `XPBD` | 🚩 **Deterministic** | ✔️ Optional via baked `ProxyCageCoupling` | Interactive `SoftbodyWorkspaceHost` + Jolt contact proxy spheres + `.slang` Jacobian ($J = \det F$) & Von Mises heatmaps. |
| **Real-Fabric Cloth & Garment Sewing (`Authoring → Game`)** | **`ClothSpace`** — **2D Orthotropic $(u,v)$ Membrane + `AVBD` (`3×3` Block Newton + Seam Duals) + `OGC` Contact** + optional **Cubic-Barrier CCD Fit Bake** | 🚩 **Authoring Deterministic** | ✔️ Game export bakes to `.SimulationSpace` tether/proxy rig | `2D Pattern (mm) ↔ 3D Mannequin` viewports + **1:1 physical `.svg` / `.dxf` cutting export** for real fabric sewing + baked game LODs. |

---

## 2. Canonical Repository Folder Structure (Physics, Deformables, Cloth & Workspaces)

Every folder, file, class, and `.slang` unit below passes the 6-gate naming audit against `Documentation/BannedWordsVocabulary.md` and `Documentation/FolderStructure.md` (strictly zero `JPH/*` includes outside `PhysicsSpace/Internal/`):

```text
Frontier/
├── Automation/
│   └── JoltPhysics.cmake                                  // Deterministic Jolt v5.6.0 (CROSS_PLATFORM_DETERMINISTIC ON, SSE4.2 / NEON, USE_FMADD OFF)
│
├── Engine/
│   ├── SimulationSpace/                                   // Deterministic rigid, fracture, deformable, cloth, particle & fluid solvers
│   │   ├── PhysicsSpace/                                  // JoltPhysics v5.6.0 deterministic world, queries & snapshot bridge
│   │   │   ├── Internal/
│   │   │   │   └── JoltWorldBridge.{h,cpp}                // Private JPH/* encapsulation, RegionAllocator TempAllocator & ExecutionChannels bridge
│   │   │   ├── PhysicsExtension.{h,cpp}                   // Fixed-step lifecycle coordinator (Initialize -> Tick -> Finalize)
│   │   │   ├── RigidBodyWorld.{h,cpp}                     // Z-up rigid bodies, sleeping islands & WorldSnapshotRecorder (64-tick rollback ring)
│   │   │   ├── CollisionLayerRegister.{h,cpp}             // 16-bit broadphase/object layer masks (Static, Dynamic, Vehicle, Debris, Avatar)
│   │   │   ├── ConstraintJoints.{h,cpp}                   // Fixed, Hinge, Slider, SwingTwist, SixDOF & VehicleConstraint wrappers
│   │   │   └── SpatialPhysicsQueries.{h,cpp}              // Deterministic raycast, sphere/capsule/convex sweep & overlap queries
│   │   │
│   │   ├── DeformableSpace/                               // Deterministic CPU + GPU .slang 3D FEM, Curved Membrane & XPBD solvers
│   │   │   ├── ConstraintBatchColoring.{h,cpp}            // Cook-time conflict coloring into race-free parallel batches (0 float atomics)
│   │   │   ├── Hex8SolidElasticity.{h,cpp}                // 3D Hexahedral Neo-Hookean elasticity (incl. 32x5 = 160-pt volumetric tyre)
│   │   │   ├── CurvedMembraneElasticity.{h,cpp}           // Orthotropic curved thin-membrane elasticity (tyre sidewall/crown & curved panels)
│   │   │   ├── Tet4SolidElasticity.{h,cpp}                // 3D Tetrahedral Corotated & Neo-Hookean elasticity + volume preservation
│   │   │   ├── XPBDConstraintSolver.{h,cpp}               // Distance, shear, dihedral bend, volume, pneumatic cavity & plastic yield struts
│   │   │   ├── ProxyCageCoupling.{h,cpp}                  // Ray-sampled ~50-pt hull builder + Mean-Value/Barycentric skinning weights
│   │   │   └── VehicleTyreDynamics.{h,cpp}                // Port of VehicleHMI 32x5 Solid/Membrane/XPBD tyres, slip & thermal coupling
│   │   │
│   │   ├── ClothSpace/                                    // Authoring-grade real-fabric pattern drafting, sewing & garment simulation
│   │   │   ├── TextileMaterialRepository.{h,cpp}          // Physical GSM [g/m^2], thickness [mm], warp/weft crimp wall, bias shear & bend
│   │   │   ├── PatternDraftNetwork.{h,cpp}                // Metric 2D Bezier contours, darts, pleats, seam allowances, notches & grainlines
│   │   │   ├── PatternDelaunayTriangulator.{h,cpp}        // Constrained isotropic Delaunay remesher (3-20 mm spacing) + point color batches
│   │   │   ├── SeamStitchRegister.{h,cpp}                 // M:N arc-length segment/free sewing pairs, gather ratios & elastic tape tension
│   │   │   ├── OrthotropicClothSolver.{h,cpp}             // 2D Orthotropic (u,v) SVD membrane + isometric bend + AVBD 3x3 block Newton
│   │   │   ├── OffsetContactSolver.{h,cpp}                // OGC Minkowski normal step bounds + barycentric PT/EE contact & Coulomb friction
│   │   │   ├── CubicBarrierFitVerifier.{h,cpp}            // On-demand high-precision Cubic-Barrier + strain-limit CCD tailor fit verification
│   │   │   └── ParametricAvatarRig.{h,cpp}                // Anthropometric sizing morphs, poseable joints, SDF + 3D arrangement cylinders
│   │   │
│   │   ├── FractureSpace/                                 // Pre-baked & runtime fracture solvers coupled to Jolt & DistanceFieldSpace
│   │   │   ├── FractureMaterialRepository.{h,cpp}         // Density, tensile/shear fracture toughness Gc, Bouchaud H=0.78 roughness
│   │   │   ├── FracturePartitionSolvers.{h,cpp}           // Boussinesq BSP, 3D Navier-Cauchy FEM, Griffith, Mott Wavefront, Voronoi, Dendritic
│   │   │   ├── BondConnectivityRegister.{h,cpp}           // Impulse-driven bond severing, deterministic island split -> Jolt ConvexHull bodies
│   │   │   └── SignedDistanceBrickBuilder.{h,cpp}         // R16F 32^3/64^3/128^3 per-piece SDF brick builder for GI, shadows & particles
│   │   │
│   │   ├── ParticleSimulation/                            // Tier B GPU secondary visual particles (one-way coupled to Jolt & SDFs)
│   │   │   ├── ParticleSourceRegister.{h,cpp}             // Point, surface-scrape, fracture-seam & volume burst issuance
│   │   │   ├── ParticleField.{h,cpp}                      // Wind, curl turbulence, rotational & SDF boundary deflection fields
│   │   │   ├── ParticleIntegrator.{h,cpp}                 // Deterministic seeded CPU reference & GPU dispatch parameters
│   │   │   └── ComputeParticleBuffer.{h,cpp}              // GPU indirect-draw & storage buffer ring
│   │   │
│   │   └── FluidSimulation/                               // Volumetric Eulerian gas/fire + SPH/FLIP liquid + Jolt boundary exchange
│   │       ├── GasDynamics.{h,cpp}                        // Compressible/incompressible smoke, fire & buoyancy grid solver
│   │       ├── LiquidSolver.{h,cpp}                       // SPH / FLIP incompressible liquid solver
│   │       ├── AdvectionSolver.{h,cpp}                    // Semi-Lagrangian & MacCormack transport
│   │       ├── VorticityConfinement.{h,cpp}               // Sub-grid rotational curl preservation
│   │       └── FluidBoundaryExchange.{h,cpp}              // Jolt collider voxelization <-> quantized micronewton buoyancy/drag impulses
│   │
│   ├── Threading/Threads/Physics/
│   │   └── PhysicsThread.{h,cpp}                          // Dedicated 120 Hz fixed-step loop, Priority Tier 1 assist & GPU timeline sync
│   │
│   ├── Codecs/                                            // Two-Tier Hybrid TOML + 64B-aligned binary .Space containers
│   │   ├── SimulationCodec.{h,cpp}                        // .SimulationSpace (.RigidDynamics, .Fracture, .Deformable, .Garment, .Revision)
│   │   ├── VolumetricCodec.{h,cpp}                        // .VolumetricSpace (.SignedDistance, .DensityGrid, .Fluid, .Revision)
│   │   └── ParticleCodec.{h,cpp}                          // .ParticleSpace (.ParticleSource, .Field, .Integration, .Fibre, .Revision)
│   │
│   ├── Interchange/
│   │   └── GarmentPatternInterchange.{h,cpp}              // 1:1 physical .svg, ASTM/AAMA .dxf, tiled A4/A0 PDF & glTF/OBJ baked garment export
│   │
│   ├── GeometricRaster/Rasterization/
│   │   └── PhysicsOverlayRasterization.{h,cpp}            // 100% GPU .slang collider wireframes, fracture bonds, SDF slice, cage & strain heat
│   │
│   ├── Shaders/
│   │   ├── Compute/
│   │   │   ├── XPBDBatchProjection.slang                  // Color-batched deterministic XPBD constraints ([precise], 0 float atomics)
│   │   │   ├── DeformableElasticity.slang                 // Sorted point-gather Hex8/Tet4/Membrane nodal forces & symplectic step
│   │   │   ├── ProxyCageDeformation.slang                 // ~50-pt proxy lattice -> arbitrary high-poly PolygonNetwork position/normal skinning
│   │   │   ├── OrthotropicClothAVBD.slang                 // Color-batched 3x3 AVBD + 2D (u,v) SVD crimp strain wall + M:N seam duals
│   │   │   ├── ClothOffsetContact.slang                   // OGC Minkowski normal bounds + barycentric PT/EE + micron int32 accumulators
│   │   │   ├── ParticleIntegration.slang                  // Seeded GPU secondary particle integration & SDF collision
│   │   │   └── FluidAdvection.slang                       // Volumetric gas/liquid advection, pressure projection & vorticity
│   │   └── Raster/
│   │       └── PhysicsOverlayRasterization.slang          // GPU-only diagnostic overlays (never CPU or ImGui ImDrawList)
│   │
│   └── Workspaces/                                        // Shared Frontier / UVWorkspace-style interactive authoring suites
│       ├── Fracture/
│       │   ├── FractureWorkspaceSession.{h,cpp}           // Editable destructible document, 6-solver dispatch, RevisionTracker & Jolt preview
│       │   ├── FractureViewportHost.{h,cpp}               // Split 2D SDF Iso-Section + 3D Impact/Bond ViewportProjectionPane host
│       │   ├── FractureOutlinerPanel.{h,cpp}              // Shard islands, strain clusters & impact markers
│       │   ├── FractureInspectorPanel.{h,cpp}             // 5-tab Inspector (Solver, Material, Bonds, SDF Bricks, Diagnostics)
│       │   └── FractureWorkspaceHost.{h,cpp}              // Docked & standalone Fracture Workspace coordinator
│       ├── Cloth/
│       │   ├── ClothWorkspaceSession.{h,cpp}              // Editable 2D pattern network, seams, avatar, fabric swatches & RevisionTracker
│       │   ├── ClothViewportHost.{h,cpp}                  // Split 2D Pattern Drafting (mm) + 3D Avatar Draping ViewportProjectionPane host
│       │   ├── ClothOutlinerPanel.{h,cpp}                 // Pattern pieces, sewing seams, layer order (0..N), trims & avatar poses
│       │   ├── ClothInspectorPanel.{h,cpp}                // 5-tab Inspector (Pattern, Sewing, Fabric Physics, Avatar, Fit & 1:1 Export)
│       │   └── ClothWorkspaceHost.{h,cpp}                 // Docked & standalone Cloth Workspace coordinator
│       └── Softbody/
│           ├── SoftbodyWorkspaceSession.{h,cpp}           // Editable Tet4/Hex8/ProxyCage document, cavity pressure & RevisionTracker
│           ├── SoftbodyViewportHost.{h,cpp}               // Split 2D Lattice Slice + 3D Deformation ViewportProjectionPane host
│           ├── SoftbodyOutlinerPanel.{h,cpp}              // Volumetric cages, ~50-pt proxy points, rigid pin sockets & colliders
│           ├── SoftbodyInspectorPanel.{h,cpp}             // 5-tab Inspector (Cage Builder, Hyperelasticity, XPBD/Pressure, Proxy Bake, Stress)
│           └── SoftbodyWorkspaceHost.{h,cpp}              // Docked & standalone Softbody Workspace coordinator
│
└── Exhibits/                                              // Deterministic headless CTest suites + mandatory CPU .png visual proofs
    ├── PhysicsValidation.cpp                              // Jolt Z-up stack, joint constraints, cross-run bit-parity & 64-tick rollback + PNGs
    ├── DeformableCageValidation.cpp                       // VehicleHMI ~50-pt crush cage + 32x5 Hex8/XPBD tyres + GPU ProxyCageDeformation + PNGs
    ├── FractureWorkspaceValidation.cpp                    // All 6 fracture solvers, Jolt compound split, R16F SDF slice & UI + PNGs
    ├── ClothWorkspaceValidation.cpp                       // 2D pattern drafting, M:N sewing, drape on avatar, strain map & 1:1 SVG export + PNGs
    └── SoftbodyWorkspaceValidation.cpp                    // Tet4/Hex8 volume conservation, pneumatic inflation, plastic dent & UI + PNGs
```

---

## 3. Making GPU `3D FEM + XPBD + AVBD` Deterministic & Fast (`VehicleHMI` Heritage)

Standard GPU physics loses determinism due to unordered floating-point `atomicAdd`, hardware FMA contraction differences (`a*b+c`), and race conditions across shared constraint vertices.

*(Legend — Determinism / Speed / Portability: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest; Complexity: ✔️ = lowest/best)*

| Rank | GPU Determinism Strategy | Cross-Run & Cross-Vendor Determinism | GPU Throughput (`10k–100k` DoFs) | Android + Desktop Vulkan Portability | Complexity |
| :---: | :--- | :---: | :---: | :---: | :---: |
| 🏆 🥇 | **Option A — Cook-Time `ConstraintBatchColoring` + Sorted Point Gather + Fixed-Point `int32` Micron Contact Accumulators + `[precise]`** | 🔴 | 🔴 | 🔴 | 🚩 |
| 🥈 | **Option B — `float64` CAS Spin-Loop Atomic Accumulation** | 🚩 *(still order-dependent)* | ✔️ *(severe warp stalls)* | ✔️ *(missing `shaderFloat64` on mobile)* | 🚩 |
| 🥉 | **Option C — Uncolored Jacobi Float `atomicAdd`** | ✔️ *(non-deterministic)* | 🔴 | 🚩 | ✔️ |

💡 **Selected Mechanism (Option A):**
1. **Zero-Atomic Gauss–Seidel via `ConstraintBatchColoring`:** At asset cook / pattern triangulation time, constraints (`XPBD`) or point blocks (`AVBD`) are partitioned into conflict-free color batches (`6–12` colors). `.slang` dispatches `Color 0 .. K-1` sequentially with UAV barriers — **zero write races, zero float atomics**.
2. **Sorted Incident-Cell Gather (`<= 256`-Point FEM Rigs):** For compact `3D FEM` lattices (`160`-point tyres, `~50`-point vehicle cages), one GPU thread per point sums nodal forces across its sorted `IncidentCells[0..M-1]` list in fixed ascending index order.
3. **Associative Micrometre Integer Contact Accumulators (`1 unit = 1e-6 m`):** Dynamic spatial-hash self-collision impulses accumulate via `InterlockedAdd` into signed `int32` micrometre slots — integer addition is strictly associative and commutative on every GPU.
4. **FMA Contraction Suppression:** `Automation/JoltPhysics.cmake` sets `USE_FMADD OFF` + `CROSS_PLATFORM_DETERMINISTIC ON` on CPU; `.slang` compute shaders mark critical integration math `[precise]`.

### 3.1 `SultanAladin/Frontier` (`VehicleHMI`) Heritage & Proposed Demo (`DeformableCageValidation`)

Directly ports and unifies the proven `VehicleHmiPrototype` architecture (`HullStructure.js`, `TyreContinuumElasticity.js`, `TyreCarcassProjection.js`, `VehicleVisual.wgsl`):

```text
  [1] JoltPhysics Rigid Chassis (PhysicsThread, Z-up, 120 Hz deterministic)
               │                                                       ▲
               │ wheel transforms, barrier contacts, ground planes     │ net tyre Fz/Fx/Fy + hull restitution
               ▼                                                       │
  [2] Compact Deformable Proxy Lattices (CPU Reference & .slang Compute — Identical Bit-Parity)
       ├── 4 × Volumetric Tyres (32 circumferential × 5 meridional = 160 pts, 128 Hex8 cells):
       │     • Solid Neo-Hookean Hex8 FEM (220 kPa inflation, Poisson sidewall bulge, J clamp)
       │     • Curved Orthotropic Membrane FEM + XPBD Belt / Sidewall Ply / Shear / Radial Struts
       └── 1 × Ray-Sampled Vehicle Crush Cage (~48–64 outer-shell sphere points, r = 0.035 m):
             • ConstructHullPoints ray-grid sampling (0.30 × 0.26 m + 4 flank heights, 0.14 m merge)
             • Elastic-plastic XPBD + Tet4 internal struts (yield threshold stores permanent crash dents!)
               │
               │ < 8 KiB compact buffer upload per vehicle (fits inside 64-tick rollback ring!)
               ▼
  [3] GPU Embedded Surface Skinning (ProxyCageDeformation.slang)
       • Pre-bakes 4-nearest cage point indices + Mean-Value / Barycentric weights per visual vertex
       • Deforms 1k–25k+ triangle ControlVehicleBody & ControlVehicleTyre PolygonNetwork in < 0.05 ms
```

---

## 4. Networked / Replicated Physics Architecture (Optional Per Instance)

Integrates directly with `Engine/NetworkingSpace/` (`DifferentialWorldEncoder`, `RemoteDirectiveRouter`) and `ThreadingArchitecturePlan.md` (**Priority Tier 0** runs replication before Tier 1 simulation):

*(Legend — Bandwidth Efficiency / Rollback Accuracy: 🔴 = best, 🚩 = moderate, ✔️ = lowest)*

| Rank | Replication Policy (Selectable Per Instance in `.SimulationSpace`) | Bandwidth Cost | Prediction / Rollback Accuracy | Target Domain |
| :---: | :--- | :---: | :---: | :--- |
| 🏆 🥇 | **Policy A — Input Stream + `WorldSnapshotRecorder` 64-Tick Rollback** | 🔴 (`32–64 B/tick` inputs) | 🔴 (`0.0 m` divergence) | Vehicles (`Jolt` chassis + `~50`-pt crush cage + `160`-pt tyres), player-driven bodies. |
| 🥈 | **Policy B — Server-Authoritative `DifferentialWorldEncoder` Quantized Deltas** | 🚩 (`Smallest-Three` 48-bit quat + `0.1 mm` delta pos) | 🚩 (Hermite interpolated) | Large rigid-body stacks, non-predicted world props, sleeping islands (`0 B` when asleep). |
| 🥉 | **Policy C — Deterministic Event-Seed Directive (`128 B`)** | 🔴 (one-shot packet) | 🔴 (identical PRNG split) | `FractureSpace` impact splits, Tier B particle bursts, wind gusts. |
| 🏳️ | **Policy D — Local Only (Replication Disabled, Default for Authoring)** | 🔴 (`0 B`) | — | Single-player props, authoring workspaces (`Fracture`, `Cloth`, `Softbody`), secondary cloth. |

---

## 5. Cloth Editor: Root-Cause Post-Mortem & Real-Fabric Tailoring Solver

### 5.1 Why `arena/c2de3250-frontier-1` Looked Disastrous (6 Root Causes Eliminated)

| # | Defect in Scrapped `arena/c2de3250-frontier-1` Prototype | Consequence Seen on Screen | Permanent Fix in New `ClothSpace` |
| :---: | :--- | :--- | :--- |
| 1 | **Fake cylindrical wrap spawn** (`sample(y, theta)` around body + pinned waist vertices) instead of flat 2D pattern panels + real sewing | Looked like a shrink-wrapped tube, zero real tailoring seams, darts, or natural hang | True **2D flat pattern drafting (`mm`) + 3D cylindrical Arrangement Points + `M:N` seam zipping** (exact Marvelous Designer / CLO3D workflow). |
| 2 | **1D edge-length springs** on a regular grid | Diagonal locking, artificial diagonal creases, and rubbery gravity sag | **2D Orthotropic Triangle Membrane (`F = Ds * Dm^-1` in Warp/Weft $(u,v)$ mm)** + **Isometric Dihedral Hinge Bending**. |
| 3 | **Fake `"avbd"` mode** (`alpha *= 0.35` on 1D CPU JS springs) | Still stretched across heavy panels and sagged at seams | Real **Point-Colored `3×3` Block-Newton `AVBD` (`SIGGRAPH 2025`) + Augmented-Lagrangian seam/pin dual updates** + **Biphasic Crimp-to-Cubic Strain Wall**. |
| 4 | **Broken `solveSelfPT` self-collision** (projected infinite plane `(p - a)·n` without checking triangle barycentric `u,v` bounds!) | Vertices in nearby 2 cm cells launched along unrelated triangle normals — jagged spikes & crumpled noise | Exact **barycentric Point-Triangle + closest-segment Edge-Edge proximity** bounded by **`OGC` (`SIGGRAPH 2025`) Minkowski normal offset radii**. |
| 5 | **Frictionless stale contact planes** | Cloth slid endlessly or buzzed through curved joints | Exact **signed-distance + capsule avatar proximity with tangential Coulomb static/kinetic friction** ($\mu_s, \mu_d$). |
| 6 | **Ad-hoc standalone page** instead of Frontier / `UVWorkspace` split authoring layout | No 2D pattern drafting tools, no grainlines, no seam allowances, unusable for cutting real cloth | Full **Frontier / `UVWorkspace` dual-viewport UI** (`Outliner | 2D Pattern (mm) + 3D Avatar | 5-Tab Inspector | ContentBrowser`) + **1:1 `.svg`/`.dxf` export**. |

### 5.2 Cloth Algorithm Selection (`Authoring First → Real Physical Sewing → Game Bake`)

*(Legend — Drape Realism / Real-Pattern Dimensional Accuracy / Interactive 60 FPS Speed: 🔴 = best, 🚩 = moderate, ✔️ = poor)*

| Rank | Candidate Cloth Solver Architecture | Drape & Wrinkle Realism | 1:1 Sewing Dimensional Accuracy (`1–3%` woven lock) | 60 FPS Interactive Pulling (`20k` tris GPU) | Verdict |
| :---: | :--- | :---: | :---: | :---: | :--- |
| 🏆 🥇 | **Option A — Dual-Tier Orthotropic Tailoring Stack:**<br>• **Live Authoring (`60 FPS`):** Color-Batched **`AVBD` (`3×3` Block Newton)** + **2D Orthotropic $(u,v)$ Analytic-SVD Membrane (Biphasic Crimp + Cubic Strain Wall)** + **Isometric Hinge Bending** + **Augmented-Lagrangian `M:N` Seams** + **`OGC` Barycentric Contact**<br>• **Pre-Cut Fit Verification (`On-Demand Bake`):** **Cubic-Barrier + Strain-Limit `CCD` Equilibrium (`SIGGRAPH Asia 2024`)** | 🔴 ⭐⭐⭐⭐⭐ | 🔴 (`< 0.5%` edge-length drift — safe to cut real fabric) | 🔴 (`2–5 ms/step` on `.slang` / WebGPU) | **Recommended** — matches/exceeds Marvelous Designer & CLO3D for both live draping and real-fabric pattern cutting. |
| 🥈 | **Option B — Substepped Orthotropic-XPBD Only (`8–16` substeps, 2D triangle FEM + dihedral bend, Vellum/Chaos style)** | 🚩 ⭐⭐⭐⭐ | 🚩 (`2–6%` creep on stiff denim/tight waistbands without duals) | 🔴 (`1.5–4 ms/step`) | Keep as **Fast Game Runtime Tier** when exporting authored garments to real-time gameplay. |
| 🥉 | **Option C — Global Implicit Newton-PCG / Full C-IPC Every Frame** | 🔴 ⭐⭐⭐⭐⭐ | 🔴 | ✔️ (`150–2,000 ms/step` — stalls brush dragging) | Too slow for interactive authoring; used **only** inside Option A's on-demand `Verify Fit` bake. |
| 🏴 | **Option D — 1D Edge-Spring XPBD / Mass-Spring** | ✔️ ⭐ | ✔️ (grid-angle dependent; unusable for real sewing) | 🔴 | **Rejected Permanently.** |

### 5.3 Real-Fabric Tailoring Workflow (Digital Pattern → Physical Sewing Machine → Game Engine)

```text
  ┌─────────────────────────────────────────┐       ┌─────────────────────────────────────────┐
  │   LEFT VIEWPORT: 2D PATTERN (mm / cm)   │       │  RIGHT VIEWPORT: 3D AVATAR DRAPING      │
  │                                         │       │                                         │
  │   ┌───────────────┐   ┌─────────────┐   │       │          (Parametric Mannequin)         │
  │   │  Front Bodice │   │   Sleeve    │   │  M:N  │             ╭───┬───╮                   │
  │   │    │ Warp     │◄─►│   │ Warp    │   │ Seams │            ╱  Zipper ╲  Layer 0..N      │
  │   │    ▼ 180 GSM  │   │   ▼         │   │──────►│           │  Draped   │ + Live Pull Pin │
  │   │  ╲ Dart       │   │  Notches ┴  │   │  <─►  │           │  Garment  │ + Steam Brush   │
  │   └───────────────┘   └─────────────┘   │ Sync  │           ╰───────────╯                 │
  │   • 10–15 mm dashed Seam Allowance      │       │   • 3D Arrangement Points (wrap torso)  │
  │   • Constrained Isotropic Delaunay      │       │   • GPU Fit Overlays: Warp/Weft %,      │
  │     (3–20 mm particle spacing, no grid!)│       │     Contact Pressure [kPa], Ease [mm]   │
  └────────────────────┬────────────────────┘       └────────────────────┬────────────────────┘
                       │                                                 │
                       ▼                                                 ▼
      [Physical Tailoring Export (1:1 Scale)]             [Game Runtime Export (.SimulationSpace)]
      • True-scale .SVG & ASTM/AAMA .DXF                  • Baked low/mid-poly PolygonNetwork LODs
      • Tiled A4/Letter PDF (50x50 mm test square         • Tangent-space wrinkle Normal/AO maps
        + corner registration marks) or A0 Plotter        • ProxyCageCoupling OR Game-Tier XPBD rig
```

- **Measured Physical Textile Parameters (`TextileMaterialRepository`):** Area weight **GSM** $[\text{g/m}^2]$, **Thickness** $[\text{mm}]$, **Warp / Weft Crimp Slack** $[\%]$ (`1–4%` Cotton Poplin / Raw Denim, `25–45%` Cotton Jersey Knit), **Cubic Lock Modulus** $[\text{MPa}]$, **Bias Shear Rigidity** $[\text{N/m}]$, **Warp / Weft Bending Rigidity** $[\text{mN}\cdot\text{m}]$, **Internal Damping**, and **Coulomb Friction** ($\mu_s, \mu_d$).

---

## 6. Unified Frontier Workspace UI Architecture (`Fracture` · `Cloth` · `Softbody`)

All three authoring workspaces reuse the exact `Engine/Components/Panes/` + `UVWorkspaceHost` chrome (`WorkspaceDockContainer`, sharp trapezoidal tabs, `OutlinerPanel`, dual `ViewportProjectionPane`, 5-tab `InspectorPanel`, `ContentBrowserPanel`, and `.slang` GPU-only viewport overlays):

| Pane Slot | `FractureWorkspaceHost` | `ClothWorkspaceHost` | `SoftbodyWorkspaceHost` |
| :--- | :--- | :--- | :--- |
| **Left: Outliner** | Breakable Assemblies · Shard Islands · Strain Clusters · Impact Sources | Pattern Pieces · Sewing Seams (`M:N`) · Layer Priority (`0..N`) · Avatar & Arrangement Points | Deformable Bodies · `~50`-Pt Proxy Cages · `Tet4`/`Hex8` Lattices · Jolt Pin Sockets |
| **Center-Left: 2D Viewport** | **2D `R16F` SDF Brick Iso-Section** & Stress Wavefront Cross-Section | **2D Metric Pattern Drafting (`mm`/`cm`)** — Bézier pen, darts, pleats, seam allowance, notches, grainlines, sewing | **2D Volumetric Slice & Deformation Cross-Section** — cell quality, cavity pressure & cage weight Falloff |
| **Center-Right: 3D Viewport** | **3D Destructible + Jolt Impact Preview** — PBR, X-Ray, Bond Connectivity, Shard Explode slider | **3D Avatar Arrangement & Live GPU Draping** — PBR weave, Strain %, Contact Pressure kPa, Pull/Pin/Steam tools | **3D Interactive Softbody & Vehicle Crush Lab** — PBR, Proxy Cage Spheres, Jacobian $J$, Von Mises Stress |
| **Right: 5-Tab Inspector** | `Solver (6)` · `Material` · `Bonds` · `SDF Bricks` · `Diagnostics` | `Pattern` · `Sewing` · `Fabric Physics` · `Avatar` · `Fit & 1:1 Export` | `Cage Builder` · `Hyperelasticity` · `XPBD & Pressure` · `Proxy Skin Bake` · `Diagnostics` |
| **Bottom: Drawer** | `ContentBrowserPanel` (Fracture presets & `.SimulationSpace` assets) | `ContentBrowserPanel` (Digitized Fabrics: Cotton, Denim, Silk, Wool, Jersey, Leather + Block Patterns) | `ContentBrowserPanel` (Rubber `220 kPa` Tyre, Steel Crush Panel, Silicone Gel, Foam Cushion) |

---

## 7. Phased Implementation Roadmap

| Phase | Milestone | Concrete Deliverables | Mandatory Verification & Visual Proof (`Exhibits/Images/`) |
| :---: | :--- | :--- | :--- |
| **P1** | **JoltPhysics v5.6.0 Foundation & `PhysicsThread`** | Wire `frontier_add_jolt()` into `CMakeLists.txt`; implement `JoltWorldBridge`, `PhysicsExtension`, `RigidBodyWorld` (Z-up, deterministic, `WorldSnapshotRecorder`), `CollisionLayerRegister`, `ConstraintJoints`, `SpatialPhysicsQueries`, `PhysicsThread`, and `SimulationCodec`. | `PhysicsValidation` (`Editor`/`Development`/`Shipping` CTest) + deterministic rigid stack & `64`-tick `0.0 m` rollback `.png` proofs. |
| **P2** | **Deterministic `DeformableSpace` & `VehicleHMI` `~50`-Pt Demo** | Implement `ConstraintBatchColoring`, `Hex8SolidElasticity`, `CurvedMembraneElasticity`, `Tet4SolidElasticity`, `XPBDConstraintSolver`, `ProxyCageCoupling`, `VehicleTyreDynamics`, and `.slang` `ProxyCageDeformation` + `XPBDBatchProjection`. | `DeformableCageValidation`: `ControlVehicleBody` (`~50`-pt plastic crush cage deforming high-tri `PolygonNetwork`) + `32×5` `220 kPa` tyres, proving CPU/GPU bit-parity + `.png` proofs. |
| **P3** | **`FractureSpace` & `FractureWorkspaceHost`** | Port all 6 `Experimental/FractureEditor/` solvers + `BondConnectivityRegister` + Jolt `MutableCompoundShape` island splitting + `SignedDistanceBrickBuilder` (`R16F`) + full `FractureWorkspaceHost` UI. | `FractureWorkspaceValidation` + 5-lens fracture/SDF/UI `.png` visual proofs. |
| **P4** | **Authoring `ClothSpace` & `ClothWorkspaceHost`** | Implement `PatternDraftNetwork`, `PatternDelaunayTriangulator` (isotropic Delaunay, zero regular-grid bias!), `SeamStitchRegister`, `TextileMaterialRepository`, `OrthotropicClothSolver` (`AVBD` + 2D $(u,v)$ SVD crimp wall + `.slang`), `OffsetContactSolver` (`OGC`), `CubicBarrierFitVerifier`, `ParametricAvatarRig`, `GarmentPatternInterchange` (`1:1` `.svg`/`.dxf`), and `ClothWorkspaceHost`. | `ClothWorkspaceValidation`: flat bodice + sleeve + skirt patterns sewn & draped onto avatar (`0` clipping, crisp natural folds, `< 0.5%` woven stretch error) + `1:1` `.svg` pattern sheet + `.png` proofs. |
| **P5** | **`SoftbodyWorkspaceHost`, Fluid Exchange & Network Replication** | Build `SoftbodyWorkspaceHost`, `FluidBoundaryExchange` + Tier B `ParticleSimulation` coupling, and Priority Tier 0 `DifferentialWorldEncoder` + rollback integration. | `SoftbodyWorkspaceValidation` + dual-endpoint networked rollback `0.0 m` divergence `.png` proofs. |

---

## 8. Immediate Decisions Before Starting Phase 1 Coding

### Decision 1 — Execution Order After `P1` (`JoltPhysics` Bringup)

*(Legend — Prerequisite Reuse / Immediate Risk Reduction / Visual Proof Value: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest)*

| Rank | Sequence Option | Prerequisite Reuse | GPU Determinism Proof Early | Unblocks Target Workspaces | Summary |
| :---: | :--- | :---: | :---: | :---: | :--- |
| 🏆 🥇 | **Option A — `P1 (Jolt)` → `P2 (~50-Pt Vehicle/Tyre 3D FEM + XPBD Demo)` → `P3 (Fracture)` → `P4 (Cloth)` → `P5 (Softbody + Net)`** | 🔴 | 🔴 | 🔴 | Proves Jolt + deterministic `.slang` `ConstraintBatchColoring` + `~50`-pt `ProxyCageDeformation` right away, giving both Fracture and Cloth/Softbody a verified foundation. |
| 🥈 | **Option B — `P1 (Jolt)` → `P3 (Fracture Workspace)` → `P2 (3D FEM + XPBD Demo)` → `P4 (Cloth)` → `P5 (Softbody + Net)`** | 🚩 | ✔️ | 🚩 | Jumps straight from Jolt rigid bodies into Fracture Workspace, deferring GPU `3D FEM + XPBD` and Cloth until after Fracture. |
| 🥉 | **Option C — `P1 (Jolt)` → `P4 (Cloth Editor)` → `P2 (3D FEM + XPBD Demo)` → `P3 (Fracture)` → `P5 (Softbody + Net)`** | 🚩 | 🔴 | 🚩 | Prioritizes the real-fabric Cloth Editor immediately after Jolt setup before Fracture. |

💡 **Recommendation:** **Option A (`P1 → P2 → P3 → P4 → P5`)** — or select **Option B** (Fracture immediately after Jolt) or **Option C** (Cloth Editor immediately after Jolt).

### Decision 2 — Cloth & Softbody Editor UI Bringup Target

*(Legend — Native Engine Integration / Rapid UX Iteration / Zero Porting Duplication: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest)*

| Rank | UI Bringup Path | Native `.SimulationSpace` & `.slang` Integration | Rapid Pattern/Sewing UX Iteration | Zero Porting Duplication | Summary |
| :---: | :--- | :---: | :---: | :---: | :--- |
| 🏆 🥇 | **Option 1 — Native C++20 + `.slang` Directly in `Engine/Workspaces/{Fracture,Cloth,Softbody}/`** | 🔴 | 🚩 | 🔴 | Reuses `UVWorkspaceHost`'s existing native C++20 `OutlinerPanel` + split `2D | 3D` `ViewportProjectionPane` + `InspectorPanel` + `ContentBrowserPanel` directly (`0` JS-to-C++ rewrite). |
| 🥈 | **Option 2 — Interactive HTML + WebGPU Reference Studio in `Experimental/ClothEditor/` First (matching `Experimental/UVEditor/`), Then Port 1:1 to Native C++20** | ✔️ | 🔴 | ✔️ | Lets you test 2D pattern drafting, sewing, and live browser WebGPU draping right inside Arena Live Preview before locking C++20 headers. |

💡 **Recommendation:** **Option 1 (Direct Native C++20 + `.slang` reusing `UVWorkspace` panes)** for `Fracture` and `Softbody`, with optional **Option 2** if you want a live browser-clickable `Experimental/ClothEditor/` UX prototype first.

---

## 9. Experimental Cloth & Tailoring Atelier Verification (`Experimental/ClothEditor/`)

🧩 The interactive browser prototype in `Experimental/ClothEditor/` is live on `0.0.0.0:8084` (`Serve.py`) using the shared Project-Zero 3-column dock workspace (`Outliner` | `Split 2D Pattern (mm) + 3D Atelier Viewport` | `Inspector`) and now implements **all six candidate cloth solver architectures (A–F) side by side** — nothing was removed, so each can be compared directly for **Game Runtime** versus **Garment Authoring** — over WebGPU (`WGSL`) and deterministic CPU `Float32Array` execution:

- 🥇 **Option A — Dual-Tier Authoring Solver (`avbd`)**: Color-batched `3×3` SPD Point-Block Newton (`AVBD`) over 2D $(u,v)$ orthotropic Warp/Weft/Bias-Shear triangles ($F_{3\times 2} = D_s D_m^{-1}$), biphasic crimp slack + cubic strain-limiting lock wall, `15.7°` woven trellis shear-jamming barrier, Rayleigh-damped Augmented-Lagrangian `M:N` seam stitches, `OGC` displacement safety radius, and `runCubicBarrierFitVerification()` pre-cut equilibrium verification (`< 0.5%` woven yarn stretch tolerance).
- 🥈 **Option B — Substepped Game Runtime Solver (`xpbd`)**: Color-batched Gauss-Seidel Orthotropic-XPBD over conflict-free face, affine-invariant quadratic hinge, and `M:N` seam batches.
- **Parametric Tailor Form & Solid 3D Eikonal SDF**: `FrontierMannequin.bin` (`10,764` points, `21,524` faces, `28` anthropometric morphs, live `cm` bust/underbust/waist/hip tape rings, `52 × 96 × 40` signed Eikonal distance volume + exact Ericson barycentric triangle proximity polish).
- **Physical Cutting-Plotter Interchange**: `1:1` physical millimetre `.svg` (`50 mm` scale-check box, cut/sew contours, Warp grainline arrows), industrial `ASTM/AAMA .dxf` (`$INSUNITS = 4`, `1_CUT`, `8_SEW`, `7_GRAINLINE`), and draped 3D `.obj`.

### Verified Four-Garment × Dual-Solver Telemetry (`ClothValidation.mjs`)

| Garment Template | Textile (`GSM` / Grain) | Solver Tier | Bit-Exact Determinism | Mean Yarn Stretch | Mean Bias Shear | Mean `M:N` Seam Gap | Max Body Penetration |
| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Couture Bodice & Flared Dress** | `135 GSM` Cotton Poplin (`0°`) | 🏆 **Option A (`AVBD`)** | 🟢 `0.000 m` diff | `1.0391` (`0.244%` baked excess) | `8.8°` | `2.09 mm` (`0.50 mm` baked) | `0.009 mm` (`0.000 mm` baked) |
| **Couture Bodice & Flared Dress** | `135 GSM` Cotton Poplin (`0°`) | **Option B (`XPBD`)** | 🟢 `0.000 m` diff | `1.0557` | `1.6°` | `2.39 mm` | `0.000 mm` |
| **Gathered Midi Skirt (`1.36:1`)** | `225 GSM` Duchesse Satin (`0°` / `90°` band) | 🏆 **Option A (`AVBD`)** | 🟢 `0.000 m` diff | `1.0362` | `8.0°` | `3.04 mm` | `0.000 mm` |
| **Gathered Midi Skirt (`1.36:1`)** | `225 GSM` Duchesse Satin (`0°` / `90°` band) | **Option B (`XPBD`)** | 🟢 `0.000 m` diff | `1.0505` | `1.7°` | `3.31 mm` | `0.000 mm` |
| **Tailored Set-In Sleeve Tee** | `190 GSM` Cotton Jersey Knit (`0°`) | 🏆 **Option A (`AVBD`)** | 🟢 `0.000 m` diff | `1.0819` | `4.1°` | `1.82 mm` | `0.000 mm` |
| **Tailored Set-In Sleeve Tee** | `190 GSM` Cotton Jersey Knit (`0°`) | **Option B (`XPBD`)** | 🟢 `0.000 m` diff | `1.0486` | `2.1°` | `2.29 mm` | `0.000 mm` |
| **`45°` Bias Silk Slip Dress** | `85 GSM` Mulberry Silk (`45°` Bias) | 🏆 **Option A (`AVBD`)** | 🟢 `0.000 m` diff | `1.1132` | `16.5°` | `2.19 mm` | `0.000 mm` |
| **`45°` Bias Silk Slip Dress** | `85 GSM` Mulberry Silk (`45°` Bias) | **Option B (`XPBD`)** | 🟢 `0.000 m` diff | `1.0310` | `2.5°` | `2.02 mm` | `0.000 mm` |

### Visual Exhibits (`Exhibits/Images/ClothEditor/`)

- `Exhibits/Images/ClothEditor/01-FourGarmentDrapeGallery.png` — Front 3D draped comparison across Couture Poplin Dress, `1.36:1` Gathered Duchesse Satin Midi Skirt, Set-In Sleeve Cotton Jersey Tee, and `45°` Bias Mulberry Silk Slip Dress.
- `Exhibits/Images/ClothEditor/02-PatternAndHeatmapDiagnostics.png` — Synchronized `1:1` millimetre 2D sewing pattern sheet (`computeSeamAllowancePolygonMm`), Warp/Weft yarn-strain lens, `45°` bias-shear trellis lens, and conflict-free GPU color-batch partitioning.

---

## 10. Six Candidate Research Solvers (A–F) — Game Runtime vs. Garment Authoring

🏆 Every architecture below is selectable at runtime (`solver-mode-select`, `SOLVER_CATALOG`, and the Inspector **Algorithm** segmented control) and is implemented in `Experimental/ClothEditor/Engine/OrthotropicClothSolver.js`. All six share the same mesh, textile, seam, contact, and telemetry pipeline, so results are directly comparable.

| Opt. | `solverMode` | Source | Local solve | Best fit | Deterministic |
| :---: | :--- | :--- | :--- | :--- | :---: |
| **A** | `avbd` | Giles, Chen & Yuksel 2025 (AVBD) + Chen et al. 2024 / SIGGRAPH 2025 OGC | Color-batched `3×3` SPD point-block Newton, biphasic crimp + cubic wall, AL `M:N` seams, OGC step bounds | **Game + Authoring** baseline | 🟢 |
| **B** | `xpbd` | Macklin, Müller & Chentanez 2016 | Color-batched compliance projection (`\tilde\alpha = 1/(k h^2)`) over warp/weft/shear faces, hinges, seams | Lightweight **Game Runtime** | 🟢 |
| **C** | `scr` | Hsu, Wang, Wu & Yuksel 2025 (*Stable Cosserat Rods*) + Hsu 2026 (viscous SCR) | Isotopic diagonal stretch Hessian `k^{ss}_e/l_e^2 I` position pass + closed-form Banach quaternion orientation solve `q_e ← normalize(v b e3 + λ b)` with stored contraction state `γ_e` | Knitwear, piping, yarn-network cloth | 🟢 |
| **D** | `gn_knot` | Guo, Wang, Liu, Li, Wang, Yang & Wang 2025 (*Fast Physics-Based Modeling of Knots and Ties*) | Unified contact-coupled Gauss–Newton `H = JᵀJ` with `H_con = G_con G_conᵀ`, persistent contact lifespan `1.2^{min(K_i,12)}`, `0.9^k` intersection filter | **Authoring**: layered folds, knots, ties | 🟢 |
| **E** | `jgs2` | Lan, Lu, Yuan, Xu, Su, Wang, Jiang & Yang 2025 (*JGS2*, ACM TOG 44(4)) | 2-pass Schur-subspace block Gauss–Seidel `(H_ii − Σ_j H_ij H_jj^{-1} H_ji) δx_i = −g_i + Σ_j H_ij H_jj^{-1} g_j` (Eq. 12) | Stiff 60 FPS GPU runtime + stiff authoring | 🟢 |
| **F** | `kes_aa` | Kawabata *KES-FB* (`TRJ`); Peng & Cao 2005; Cerda & Mahadevan 2003; Walker & Ni 2011 | Non-orthogonal `(C11, C22, C12)` co-rotational fiber shell, `2HB`/`2HG` yarn-crossover hysteresis, compressive buckling cap, monotonicity-guarded Anderson(`m=2`) | Precision **CAD Authoring** + drape fidelity | 🟢 |

### 10.1 Self-Collision: Point-Triangle **and** Edge-Edge with Cross-Seam Exclusion

- **Point-Triangle (VF)**: uniform spatial hash (`selfCellSize = 24 mm`), Ericson barycentric closest-point, deterministic `Int32` micrometre accumulators (no float race, no order dependence), layer-aware correction share (`0.80 / 0.20` for stacked panel layers).
- **Edge-Edge (EE)**: a second narrow phase (`_projectEdgeEdgeContacts`) that catches folded layers and seam-adjacent crossings no vertex ever enters. Candidates come from the contact-touched patch (bounded 22 k narrow-phase budget), closest-point-between-segments, barycentric distribution to all four endpoints, and the same inelastic normal + Coulomb tangential friction response.
- **2-hop topological + cross-seam exclusion** (`selfExclOffsets` / `selfExclTargets`, sorted CSR, binary search): a vertex never collides with its own 1–2 ring, **but fabric on the opposite side of a stitched seam still collides** — the previous failure mode where seams folded through themselves.
- **Multi-substep**: VF+EE run every substep for `gn_knot` and every other substep for A/B/C/E/F, plus a final inelastic frame-level pass.

### 10.2 No Artificial Settling, Freezing or Deadbands

The atelier never fakes rest. Removed outright:

- the `isSettled` early return in `stepFrame` (integration now always proceeds),
- the `freezeAlpha` position-blend that clamped sub-2.8 mm motion,
- the 2.0 mm/s velocity deadband and the global `vel.fill(0)` standstill,
- the `settleT`/`subVisc` velocity ramp in both the CPU and WebGPU paths.

Rest now emerges from **measured physics only**:

1. **Kawabata hysteresis loss factor** — `2HB`/`2HG` set the Rayleigh/Kelvin–Voigt coefficient `c = 2ζ√(k m)` per yarn, clipped so an impulse can never reverse the relative velocity (strictly dissipative).
2. **Amplitude-dependent hysteresis** (`_applyLowAmplitudeHysteresis`) — real woven cloth has a much higher loss factor at small amplitudes; the rate rises smoothly below 50 mm/s. Continuous exponential decay, never a clamp.
3. **Contact friction** — stabilized Coulomb stiction (`μ_s`/`μ_k` per textile) on body and cloth–cloth contacts.
4. **Strain-rate viscosity** — Hsu 2026 Kelvin–Voigt on the rod network, `2HB`-scaled hinge damping for the Kawabata shell.

`isSettled` survives as a **read-only telemetry flag** derived from measured RMS/max speed; it never gates integration, and the validation suite proves a 35 mm drag still moves cloth after three seconds of relaxation.

### 10.3 Zhang et al. 2025 `PIE` — Physics-Optimal Mesh Resolution

`estimateClothResolutionPIE(textile)` solves the calibrated monotonic polynomial (Eq. 4)

$$c_1 r^6 + c_2 r^4 = \frac{B}{E},\qquad c_1 = 32.08\ \text{m}^{-3},\ c_2 = -1.876\times10^{-4}\ \text{m}^{-1}$$

by bisection on `r ∈ [2.5, 50] mm`, per axis (anisotropic warp/weft radii from `warpModulus`/`weftModulus` with decrimp correction), and reports the Cerda–Mahadevan wrinkle wavelength `λ ≈ r*/0.27` and the Vandeparre wrinklon transition `L_w ~ λ^{3/2}(δ/h²)^{1/4}`. Outputs:

- `authoringSpacingMm` — interactive Delaunay spacing (clamped `9.5–18.5 mm`) used by the **PIE Auto-Res** button and by `compileGarmentNetwork({ textile })`,
- `gameSpacingMm` — 45–60 FPS runtime spacing (`13.5–24 mm`),
- `seamSourceMm` — seam sampling density.

### 10.4 Guo et al. 2025 `FPB` — Knot & Tie Pipe Templates

`FPB_KNOT_TEMPLATES` (four-in-hand, half-Windsor, granny) with per-span variable radii plus `sampleKnotTemplate()` produce the Bézier pipe geometry that Option D's unified contact Gauss–Newton solvers drape cloth over.

### 10.5 Verified Six-Solver × Four-Garment Telemetry (`ClothValidation.mjs`)

Measured over 180 calm-air frames per cell (peak → relaxed RMS speed; stretch, shear, seam gap and penetration at frame 180):

| Garment | Solver | Relaxation (RMS mm/s) | Mean Yarn Stretch | Mean Bias Shear | Seam Gap | Penetration |
| :--- | :---: | :--- | :---: | :---: | :---: | :---: |
| Couture Dress | A `avbd` | `321.9 → 39.1` | `1.038` | `7.9°` | `0.13 mm` | `0.00 mm` |
| Couture Dress | B `xpbd` | `336.3 → 8.4` | `1.046` | `1.7°` | `0.20 mm` | `0.00 mm` |
| Couture Dress | C `scr` | `316.5 → 3.1` | `1.090` | `6.1°` | `0.41 mm` | `0.00 mm` |
| Couture Dress | D `gn_knot` | `489.4 → 36.8` | `1.038` | `4.4°` | `0.28 mm` | `0.00 mm` |
| Couture Dress | E `jgs2` | `327.0 → 39.8` | `1.107` | `9.2°` | `0.45 mm` | `0.00 mm` |
| Couture Dress | F `kes_aa` | `722.5 → 24.7` | `1.065` | `11.6°` | `0.35 mm` | `0.00 mm` |
| Gathered Skirt | A–F | `282 → 58.2`, `262 → 6.4`, `442 → 2.8`, `374 → 12.1`, `434 → 27.5`, `737 → 28.2` | `1.015–1.087` | `1.4–9.6°` | `< 0.56 mm` | `0.00 mm` |
| Tailored Tee | A–F | `348 → 4.8`, `355 → 6.7`, `324 → 3.8`, `566 → 11.0`, `338 → 57.9`, `656 → 8.2` | `1.038–1.157` | `2.0–11.0°` | `< 0.43 mm` | `0.00 mm` |
| Bias Silk Tunic | A–F | `576 → 313`, `393 → 21.6`, `551 → 3.2`, `340 → 25.5`, `611 → 46.1`, `694 → 70.5` | `1.014–1.083` | `3.1–21.0°` | `< 0.12 mm` | `0.00 mm` |

> *Bias silk keeps creeping/swinging at a low level: `2HG = 0.09` (almost no hysteresis) and friction-only support. It is bounded and physically dissipated — never frozen.*

### 10.6 Recommended Split (to be confirmed by interactive evaluation)

- **Game Runtime** → **B (`xpbd`)** for budget-limited targets, **E (`jgs2`)** when stiff tailoring must still run at 60 FPS, **A (`avbd`)** as the quality/determinism reference.
- **Garment Authoring** → **D (`gn_knot`)** for layered folds, knots and ties, **F (`kes_aa`)** for measured Kawabata drape fidelity and crisp hand-feel, **C (`scr`)** for knits, pipings and yarn-scale behaviour.
- **`PIE Auto-Res`** should be run once per textile change; it sets authoring spacing, and reports the wrinkle wavelength/wrinklon transition used by the drape lens.

### 10.7 Post-Fix Solver Corrections (E/F puff, E shimmer, D switch stall)

Three defects identified during interactive evaluation were traced to root cause and fixed in `OrthotropicClothSolver.js` / `ClothStudioApp.js`:

1. **E (`jgs2`) and F (`kes_aa`) puffed into an inflated "jacket".** Neither stiffness nor the cubic strain wall was responsible (varying `wallModulus` ×0.05/×0.25 and disabling the cubic wall moved the SDF gap < 2 mm). Both fed the *current* positions into a block/Newton solve whose inertial term is `m/h²`; at `h = 1/240 s` that term dominates gravity, so the solve cancelled gravity + aerodynamic drag every substep and the garment kept its spawn shell. Fix: advect every unpinned point to the inertial target (the symplectic-Euler tier AVBD and SCR already used) before assembling the block. Dress gap: E `30.6 → 24.9 mm`, F `46.2 → 30.5 mm`. F additionally gained a `_clothPatchProjection(h, 2)` jamming pass (yarn `1.097 → 1.065`, residual `74 → 21 mm/s`) and compressive-only Poisson suppression `c12eff = (εu>0 && εv>0) ? C12 : 0` (crimp interchange, gap `41.2 → 31.9 mm`).
2. **E never settled (73–110 mm/s residual).** Not an energy leak — ablating the patch projection made it worse (304 mm/s), and extra block passes/substeps made it worse still. A Kelvin–Voigt dashpot on the inertial block (`c = ζ·m/h`, ζ ≤ 8) changed the residual by < 3 mm/s. The Schur solve was overshooting where its quadratic model is invalid. Fix: a **2.5 mm trust region per block pass** plus **2 patch-projection sweeps**. Residual `73 → 40 mm/s` (dress) and `64 → 27.5 mm/s` (skirt) while the gap improved `29.0 → 24.9 mm`; caps of 5/10/20 mm leave 92/211/349 mm/s.
3. **D (`gn_knot`) appeared to freeze on switch.** The previous mode's contact manifold, `pointContactAge`, `_gnStepScale`, Anderson state, seam duals and XPBD `λ` arrays were retained, and a prior Verify-Fit bake had left `running = false`. Fix: `resetSolverMemory()` (also re-seeds Cosserat `edgeQuats`/`edgeGamma` from the current segment directions and resets `quietSettleTime`), plus `syncStateToGpu()`, `_lastSteppedMode` re-upload on return from a CPU-only mode, and auto-resume. D now moves `50.0 mm` in its first 5 frames after a switch (gap `18 → 17 mm`, no NaN).

Full comparison, rankings and the A–F recommendation are in `Documentation/Plans/ClothSolverComparisonReport.md`.

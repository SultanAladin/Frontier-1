# Frontier Physics, Deformables, Fracture & Real-Cloth Architecture Plan

> **TL;DR:** Build Frontier's unified physics architecture across **five ordered layers** on **Z-up meters** (`Axis3D`, $g = -9.80665\,\text{m/s}^2$):
> **① JoltPhysics v5.6.0 (`CROSS_PLATFORM_DETERMINISTIC ON`)** as the deterministic rigid-body, contact, vehicle, fracture-chunk, and `StateRecorder` rollback world on `PhysicsThread`;
> **② Deterministic GPU/Hybrid `3D FEM + XPBD` (`DeformableSpace`)** combining constraint batch coloring + fixed-order gather + fixed-point integer accumulators with **~50-point embedded proxy cages (`ProxyCageDeform.slang`)** so `1k–100k+`-triangle `PolygonNetwork` surfaces and `160`-point tires deform at zero CPU vertex cost;
> **③ Networked Replication (`DifferentialWorldEncoder` + `StateRecorder` Rollback)**;
> **④ Strict Separation & Two-Way Coupling of `FractureSpace`, `ParticleSimulation`, and `FluidSimulation`**; and
> **⑤ Real-Fabric `ClothSpace` (`AVBD` + `OGC` + Cubic Strain-Limiting Wall + Offline `Cubic-Barrier CCD` Bake, 1:1 `SVG`/`DXF` Pattern Export) + `Softbody` & `Fracture` Workspaces** sharing the exact Frontier / `UVWorkspace` split-viewport UI (`Outliner | 2D + 3D Viewports | Inspector | ContentBrowser`).

---

## 1. Domain Responsibility Matrix: Jolt vs. GPU `.slang` Solvers

| Simulation Domain | Primary Solver Host | Determinism Tier | Replicated Over Network? | Visual Render & Coupling Path |
| :--- | :--- | :--- | :--- | :--- |
| **Rigid Bodies, Joints, Vehicles & Contacts** | **JoltPhysics v5.6.0** (`PhysicsThread`, CPU) | 🔴 **Bit-Exact Cross-Platform** (`USE_SSE4_1/2`, `USE_FMADD OFF`, ARM64 NEON) | ✔️ Yes — `JPH::StateRecorderImpl` rollback or server delta | Writes `Quaternion` + `Translation` directly to GPU instance buffers (no Euler round-trip). |
| **Fracture Chunks & Bond Strain** | **Hybrid:** `FractureSpace` (Stress Lattice / Voronoi / SDF) + **Jolt** (`MutableCompoundShape` → `ConvexHullShape`) | 🔴 **Bit-Exact** (deterministic PRNG seed + Jolt contact impulses) | ✔️ Yes — compact `FractureEvent` (`128 B`: seed, impact point, energy) + chunk poses | `PolygonNetwork` shards + `32³/64³/128³` `R16F` brick SDFs feed shadows/GI immediately. |
| **Tires & Vehicle Body Deformation (~50 Proxy Points)** | **`DeformableSpace`** (`3D Hex8 Neo-Hookean FEM` + `Curved Membrane FEM` + `XPBD` `32×5` tires & `~50`-pt chassis cage) | 🔴 **Bit-Exact** (fixed-order gather / batch-colored XPBD; runs identically CPU/GPU) | ✔️ Yes — compact proxy lattice (`< 8 KiB`/vehicle) snaps/rolls back cleanly | `ProxyCageDeform.slang` deforms `1k–100k+` triangle `PolygonNetwork` on GPU via pre-baked MVC/barycentric weights. |
| **Real Cloth & Sewing Garments** | **`ClothSpace`** (`AVBD` + `OGC` + Cubic Strain-Limiting Wall GPU `.slang`; optional `Cubic-Barrier CCD` Final Bake) | 🚩 **Authoring Deterministic** (batch-colored `3×3` point blocks + local normal offsets) | ✔️ Optional — replicated via attachment/wind seeds or proxy anchors | Dual `2D Pattern ↔ 3D Mannequin` viewports + `1:1` `.svg`/`.dxf` cutting pattern export. |
| **General Softbodies (Inflatables, Cushions, Gel)** | **`DeformableSpace`** (`Tet4` / `Hex8` Corotated & Neo-Hookean FEM + XPBD volume/pressure) | 🚩 **Deterministic** (colored Tet4/Hex8 constraint sweeps + fixed timestep) | ✔️ Optional — proxy cage points or authoritative snapshots | Shares `SoftbodyWorkspace` + Jolt contact proxy points; GPU strain/Jacobian heat overlays. |
| **Gameplay Debris Particles** | **JoltPhysics** small `ConvexHull` / `Sphere` shapes (capped register) | 🔴 **Bit-Exact** | ✔️ Yes — replicated like rigid bodies | Driven directly by Jolt bodies + collision friction. |
| **Cosmetic / Secondary GPU Particles** | **`ParticleSimulation`** (`.slang` Compute: sparks, dust, smoke wisps, lightning) | ✔️ **Seeded Local Visual Replay** *(never feeds forces back into Jolt)* | ✔️ Event-triggered only (`SpawnToken`, seed, velocity) | Direct GPU indirect draw (`ComputeParticleBuffer`); zero CPU readback. |
| **Volumetric Fluid (Smoke, Fire, Liquid)** | **`FluidSimulation`** (`.slang` MAC Eulerian Gas Grid + FLIP/SPH Liquid, **separate from Jolt**) | ✔️ **Authoring Deterministic** | ✔️ Triggered sources + clamped buoyancy/drag impulses | `FluidBoundaryExchange` exchanges Jolt collider velocity slabs ↔ clamped buoyancy/drag impulses. |

💡 **Core Architectural Rule:** **Never loop over thousands of visual triangles in CPU collision or physics.** High-density visuals stay in `PolygonNetwork` VRAM buffers deformed by **compact physical proxy lattices (`30–160` points)** (`TyreElasticity` / `DeformableSpace` / `ProxyCageDeform`).

---

## 2. How GPU `3D FEM + XPBD` Stays Deterministic (No Race Conditions)

Standard GPU compute fails determinism for three reasons: unordered `atomicAdd` floats, FMA instruction contraction differences across vendors (`NVIDIA` / `AMD` / `SwiftShader` / `ARM64` mobile GPUs), and unordered workgroup reduction.

| Candidate GPU Determinism Strategy | Cross-Vendor Bit-Exactness | Performance (`10k–100k` constraints) | Implementation Complexity | Verdict |
| :--- | :---: | :---: | :---: | :--- |
| **🏆 🥇 Option A — Cook-Time Constraint Batch Coloring + Gather Integration + Fixed-Point Contact Deltas** | 🔴 | 🔴 *(0 atomics in Gauss-Seidel/XPBD; 100% coalesced)* | 🚩 *(one-time edge/cell greedy coloring at asset cook)* | **Selected** |
| **🥈 Option B — Double-Precision `float64` GPU Atomic Spin-Accumulation** | 🔴 | ✔️ *(slow on consumer GPUs & unsupported on Android Vulkan)* | 🚩 | Rejected (fails Android portability). |
| **🥉 Option C — Pure Jacobi Float Accumulation (`atomicAdd` / uncolored)** | ✔️ *(breaks across warp sizes/drivers)* | 🔴 | ✔️ | Rejected (non-deterministic + slower convergence). |

### 2.1 Four Mechanical Rules Enforced in `.slang` + C++20

1. **Zero Race-Condition Gauss-Seidel via `ConstraintColorBatch`:**
   - At cook / pattern-triangulation time on `WorkspaceThread`, `ConstraintColoring` partitions every XPBD constraint (distance, shear, dihedral bending, Tet4 volume, Hex8 elasticity) into independent **Color Batches** (typically `6–12` colors for triangle cloth, `8` colors for Hex8 grids). No two constraints in the same batch touch a common point.
   - Each `.slang` substep dispatches Color `0 → Color K-1` sequentially with a compute `RWStructuredBuffer` UAV barrier between batches — **zero atomics**, **100% deterministic point update order**.
2. **Deterministic Vertex-Centric Gather for FEM Nodal Forces (`<= 256`-Point Lattices):**
   - For compact high-precision rigs (`160`-point `TyreElasticity`, `~50`-point vehicle chassis cage), each thread owns one lattice point `n` and sums forces over its sorted incident cell array `IncidentCells[0..Count-1]` in fixed ascending order (`CellIndex`).
3. **Signed Fixed-Point `int64` / Split `int32` Micron Accumulators for Self-Collision Impulses:**
   - Dynamic triangle-point / edge-edge self-collision contacts cannot be pre-colored cleanly every tick. Spatial-hash collision pairs accumulate impulses into quantized **micrometre integer slots (`1 unit = 1e-6 m`)** via deterministic integer `InterlockedAdd`, then convert back to `float` in a deterministic sweep — **integer addition is associative and commutative on every GPU**.
4. **Strict IEEE-754 FMA Suppression (`USE_FMADD OFF` on CPU + `[precise]` in `.slang`):**
   - Matches `Automation/JoltPhysics.cmake` (`CROSS_PLATFORM_DETERMINISTIC ON`, `USE_FMADD OFF`) so CPU fallback, headless Exhibits, desktop Vulkan, and Android Vulkan stay reproducible.

---

## 3. Direct Elasticity Heritage from `Frontier/VehicleHmiPrototype` (`~50`-Point Vehicle Cage & Tires)

Formalizes the established mechanics from `TyreContinuumElasticity.js`, `TyreCarcassProjection.js`, `HullStructure.js`, and `VehicleVisual.wgsl` into native C++20 + `.slang`:

```text
  [1] JoltPhysics Rigid Chassis (1 body) + VehicleConstraint / RaycastSuspension
               │                                                    ▲
               │ wheel poses, ground normals, chassis impulses      │ net tire Fz/Fx/Fy + contact restitution
               ▼                                                    │
  [2] Compact Proxy Lattices (Deterministic CPU / .slang Compute)   │
       ├── 4 × Tire Carcass (`32×5 = 160` pts, `128 Hex8` cells):   │
       │     • Solid Neo-Hookean FEM (E_eff(P), nu=0.44, J clamp)   │
       │     • OR Orthotropic Curved Membrane FEM                   │
       │     • OR XPBD Belt + Sidewall + Shear + Pneumatic Struts   │
       └── 1 × Vehicle Crush Cage (`~48–64` boundary points sampled cleanly from chassis bounds):
             • Tet4 / Strut XPBD plastic crush (yield threshold + permanent deformation limit)
               │
               │ compact buffer upload (<= 8 KiB per vehicle!)
               ▼
  [3] Embedded Visual Surface Skinning (`ProxyCageDeform.slang`)
       • Every vertex of the 1k–50k triangle `PolygonNetwork` pairs at cook time to its 4 nearest
         proxy points (precomputed Mean-Value / Barycentric weights + local orthonormal basis)
       • GPU vertex/compute unit deforms positions + recomputes smooth tangents/normals in O(1)/vertex
```

- **Why this scales effortlessly:** Running `3D FEM` on `50–160` proxy points costs `< 0.12 ms` per vehicle even with rollback enabled, while `ProxyCageDeform.slang` drives arbitrary high-resolution `ControlledIntegrator.GeometrySpace` (`25,068` triangles) with zero CPU surface loops.

---

## 4. JoltPhysics Integration & Networked Physics Architecture

### 4.1 Hard Jolt Isolation Boundary (`JoltWorldBridge`)
- Mirror `RigidBodySimulation.h`: **no `JPH/*` header may ever be included outside `Engine/SimulationSpace/PhysicsSpace/Internal/`**.
- `Automation/JoltPhysics.cmake` already builds `Jolt` with `CROSS_PLATFORM_DETERMINISTIC ON`, `USE_FMADD OFF`, `OBJECT_LAYER_BITS=16`, and x86_64 SSE4.2 / ARM64 NEON parity. Wire `frontier_add_jolt()` inside `CMakeLists.txt` (`Editor`, `Development`, and `Shipping`).
- **Coordinate System Invariant:** Frontier is **Z-up, meters** (`+X` Forward, `+Y` Left/Right per right-handed basis, `-Z` Gravity `9.80665 m/s²`). `JoltWorldBridge` writes solved `(Quaternion, Translation)` directly into transform slots without Euler conversion.

### 4.2 Two-Mode Networked Replication (`NetworkingSpace/` + `PhysicsExtension`)

| Mode | Best Fit | Wire Bandwidth | Rewind / Determinism Mechanism |
| :--- | :--- | :--- | :--- |
| **Mode A — Deterministic Snapshot Rollback & Prediction** | Vehicles, competitive arena physics, tight player-pushed props | **Ultra-low** (`InputCommands` + tick checksums) | `64`-tick ring buffer storing `JPH::StateRecorderImpl` binary snapshots + `~8 KiB` vehicle/softbody proxy buffers. On `NetworkTick` misprediction, rewinds both Jolt & proxy rigs and re-steps cleanly on `PhysicsThread`. |
| **Mode B — Server-Authoritative Delta & Seeded Fracture** | Large `1,000+` fracture chunks, multi-garment crowds, dense scenes | **Adaptive** (`DifferentialWorldEncoder` Priority Tier 0) | Server simulates authoritative Jolt/fracture world; sends quantized `(Pos, Smallest-3 Quat, Vel)` deltas + compact `FractureEventToken` (`Seed, ImpactPos, Normal, Energy` -> **`128 B`**, never raw shard geometry). |

---

## 5. Fracture Physics & Workspace Plan (First Target Built on P0)

Unblocks `Fracture Workspace` cleanly by porting `Experimental/FractureEditor/` onto `PhysicsSpace` + `PolygonNetwork` + `DistanceFieldSpace`:

1. **Six Fracture Solvers (`FractureSpace/Solvers/`):**
   - **Fast Runtime Boussinesq Flat-BSP** (`runtime_boussinesq`) — instant sub-ms convex slab cuts at true impact point.
   - **Engineering Navier–Cauchy 3D Stress Lattice + Bouchaud `H=0.78` Interlocking Fractography** (`engineering_fem`) — exact volume conservation $\sum V_i = V_0$ + watertight shards.
   - **Griffith Energy-Budgeted Clipping**, **LEFM Mott Wavefront**, **Clustered 3D Voronoi + `StrainBondRegister`**, and **Dendritic Spall**.
2. **Jolt Compound ↔ Dynamic Chunk Lifecycle:**
   - Intact breakables share one `JPH::MutableCompoundShape`. On impact energy $E_k = \frac{1}{2}\mu v_n^2 > \text{MaterialThreshold}$, `StrainBondRegister` removes broken bonds, runs deterministic BFS island flood, extracts disconnected shard subsets into independent dynamic `JPH::ConvexHullShape` rigid bodies, triggers `ParticleSimulation` dust/sparks, and cooks `32³ / 64³ / 128³` `R16F` shard bricks into `DistanceFieldSpace`.
3. **Fracture Workspace UI (Matches Frontier / `UVWorkspace` Layout):**
   - `Outliner` (breakable bodies, bond clusters, shard count, active solver) · **`2D Cross-Section / SDF Iso-Section` + `3D Fracture Viewport`** (`PBR`, `X-Ray`, `Stress Telemetry`, `Bond Plexus`, `SDF Cross-Section` `.slang` lenses) · `Inspector` (material presets, toughness, seed, impact impulse injector, respawn/reassemble) · `ContentBrowser` (material toughness & pattern presets).

---

## 6. Real-Fabric Cloth Editor (Surpassing Legacy Tailoring Tools) & Softbody Editor

### 6.1 Research Review (Two Minute Papers & 2020–2026 Cloth Breakthroughs)

Why traditional cloth tools (including XPBD, Baraff–Witkin linear springs, and standard StVK without strain limits) suffer from **"rubbery over-stretching"**, **membrane locking** (stiff fake wrinkles), or **spurious tangential distortion** during self-contact — and what recent breakthroughs fix each:

| Research / Solver | Key Breakthrough | Real-Time Speed (`GPU`) | Non-Rubbery Stretch & Zero-Clip Fidelity | Verdict for Frontier |
| :--- | :--- | :---: | :---: | :--- |
| **Ando / ZOZO `Cubic Barrier + Elasticity-Inclusive Dynamic Stiffness` (`SIGGRAPH Asia 2024`, Two Minute Papers Oct 2025)** | Replaces log-barrier C-IPC with a **$C^2$ cubic barrier + strict `1–5%` principal stretch limit + CCD**, yielding strictly strain-limited cloth (`no rubber stretch`) and zero clipping across millions of contacts. | ✔️ *(offline global Newton + PCG: seconds–minutes)* | 🔴 **Gold Standard** | **Selected as `Final Tailor Bake` Mode** *(runs on user demand before physical pattern cutting)* |
| **`Offset Geometric Contact (OGC)` (Chen, Hsu, Liu, Macklin, Yang, Yuksel — `SIGGRAPH 2025`, Two Minute Papers)** | Fixes IPC's **false tangential stretch** during contact by enforcing strictly normal Minkowski offset bounds ("armor shell") with per-point local step bounds — eliminating global CCD bottlenecks (`1.2 ms/step` for `15.6k`-pt dress on RTX 4090). | 🔴 **`1–6 ms`** | 🔴 **Zero penetration + zero false contact stretch** | **Selected for Real-Time Contact** |
| **`Augmented Vertex Block Descent (AVBD)` (`SIGGRAPH 2025`, Two Minute Papers) over `VBD` (`SIGGRAPH 2024`) & `XPBD`** | Runs batch-colored `3×3` local Newton Gauss–Seidel blocks directly on implicit Euler variational energy (0 global linear matrices) + **Augmented Lagrangian dual updates** on pins, `M:N` seams, and hard limits — beating XPBD under high mass/stiffness ratios. | 🔴 **`1–5 ms`** | 🔴 **No rubber sag across heavy/light seam pairings** | **Selected as Real-Time Solver Core** |
| **`Cubic Eigen-Strain Wall` (`StiffGIPC` 2024/2025) + `Homogenized Yarn-Level Cloth` (`HYLC`, `SIGGRAPH 2020`)** | Splits 2D deformation gradient $F$ via **analytic SVD ($\sigma_1, \sigma_2$ along Warp/Weft)**: keeps compression soft (`~20–50 kPa`) for crisp folds, models weave crimp (`1–5%`), then applies a **$C^2$ cubic `1–10+ MPa` wall** once threads pull taut + captures knit curl & yarn anisotropy. | 🔴 *(closed-form 2D analytic SVD)* | 🔴 **Eliminates cardboard locking & rubber stretch simultaneously** | **Selected as Constitutive Fabric Energy** |
| **`Stable Cosserat Rods` (`SIGGRAPH 2025`, Two Minute Papers) / Pure `XPBD`** | `Stable Cosserat Rods` solves explicit individual yarn strands (`65k` pts in `32 ms`), whereas raw `XPBD` struggles on stiff low-stretch textiles (`1000+` iterations or rubbery sag). | 🚩 | 🚩 *(XPBD stretches; full yarn simulation is overkill for full garment pattern drafting)* | **Use Cosserat Rods only for Hero Knit/Stitch Closeups**; retire raw XPBD from garment stretch. |

### 6.2 Frontier's Next-Gen Dual-Tier Cloth Solver Stack

| Solver Tier | Algorithm Composition | Target Latency | Physical Guarantee |
| :--- | :--- | :---: | :--- |
| **🏆 Tier 1 — Interactive Real-Time Draping (Default)** | **`AVBD` (Point-Colored `3×3` Newton) + `Analytic-SVD Cubic Eigen-Strain Wall` (Warp/Weft/Shear + Plate Bending) + `OGC` Normal Contact Bounds** | **`2–6 ms / tick`** (`60–120 FPS` deterministic `.slang` compute) | Zero false contact stretching, soft realistic folds, hard `1–5%` woven crimp wall, and zero atomic write races (`VertexColorBatch`). |
| **🥇 Tier 2 — `Final Tailor Bake` Verification Mode** | **Ando `Cubic-Barrier` + Dynamic Strain-Limit `CCD` Newton/PCG Sweep** (triggered prior to `1:1` cutting export) | **`Seconds` (async `PhysicsThread` / GPU)** | Mathematical non-penetration + strict physical elongation bounds across tight collars, pleats, waistbands & layered garments. |

### 6.3 Real-World Physical Fabric Constants & Tailoring Features
So digital garments match physical textiles before cutting real cloth:
- **Real Textile Material Specification (`TextileSpecification`):**
  - Surface Density **GSM** $[\text{g/m}^2]$, **Thickness / OGC Offset Radius** $[\text{mm}]$, **Warp & Weft Crimp Threshold** $\varepsilon_{\text{crimp}}$ $[\%]$, **Hard Lock Strain Limit** $\varepsilon_{\text{limit}}$ (`1–5%` wovens, `15–45%` knits), **Cubic Wall Modulus** $[\text{MPa}]$, **Buckling Compression** $[\text{kPa}]$, **Bias Shear** $[\text{N/m}]$, **Warp/Weft Bending Rigidity** $[\text{mN}\cdot\text{m}]$, **Knit Curl Coupling (`HYLC` polynomial coefficients)**, **Static/Kinetic Friction** ($\mu_s, \mu_d$), and **Air Drag**.
- **2D Pattern Drafting ↔ 3D Mannequin Draping (`ClothWorkspaceHost` — identical UX pattern to `2D UV | 3D Surface`):**
  - **Left Viewport (`2D Pattern`):** Exact metric grid (`mm`/`cm`), cubic Bézier pattern contours, symmetry-linked halves, **seam allowance offset** (`10–15 mm` dashed cutting line), **grainline arrows** (aligning $u$ = warp directly to analytic SVD material coordinates!), **darts, pleats, notches, button/zipper tacks**, `M:N` edge-length-aware **Free Sewing & Segment Sewing** (gathers/easing calculated automatically from edge ratio!), and **Isotropic Constrained Delaunay Remeshing** (`3 mm – 20 mm` spacing).
  - **Right Viewport (`3D Garment`):** Parametric **3D Mannequin** (height, chest, waist, hip, shoulder, poseable joint rig wrapped in Jolt/GPU analytical capsules + SDF), **3D Arrangement Cylinders**, **AVBD Augmented-Lagrangian Seam Zipping**, multi-layer **Layering Priority (`0..N`)**, `OGC` self-collision (`OGCContactBounds.slang`), **Pin / Tack / Steam-Press / Live Fit Heatmaps** (**Strain %**, **Stretch Wall Proximity**, **Contact Pressure kPa**, **Fold Tension**).
  - **Real-World Pattern Export (`GarmentInterchange`):** Exports **1:1 physical-scale `.svg` & `.dxf` cutting sheets** (plus tiled A4/Letter/A0 PDF projection) preserving seam allowances, grainlines, notch ticks, fold lines, and stitch labels so you can print, cut, and sew the exact garment from real fabric!

### 6.4 Softbody Physics Editor (`SoftbodyWorkspaceHost`)
Pairs directly with `DeformableSpace` using the same dual-pane UX (`Outliner | 2D Lattice/Cross-Section + 3D Deformation Viewport | Inspector | ContentBrowser`):
- Generates **Tet4** (surface-conforming) or **Hex8** (regular carcass/block) volumetric cell cages from any `.GeometrySpace` `PolygonNetwork` — including **auto-sampling `~32–64` proxy cage points** (`BakeProxyCage`) for high-triangle vehicles/characters.
- Live Inspector authoring for **Young's Modulus ($E$)**, **Poisson's Ratio ($\nu \in [0, 0.495]$)**, **Internal Cavity Pneumatic Pressure ($\text{kPa}$)**, **Plastic Yield Limit**, **Fiber Anisotropy Vector Brush**, **Rigid-Socket Pin Attachments** (connecting to Jolt bodies/bones), and GPU `.slang` **Jacobian ($J = \det F$) / Von Mises Stress / Proxy-Cage Overlay** inspection.

---

## 7. Canonical Folder Structure (100% `BannedWordsVocabulary.md` & `FolderStructure.md` Compliant)

```text
Engine/
├── SimulationSpace/                               // Deterministic rigid, deformable, cloth, fracture, particle & fluid solvers
│   ├── PhysicsSpace/                              // JoltPhysics v5.6.0 deterministic world & collision bridge
│   │   ├── PhysicsExtension.{h,cpp}               // Fixed-step lifecycle coordinator, Priority Tier 1 dispatch & substeps
│   │   ├── RigidBodyWorld.{h,cpp}                 // Opaque JoltWorldBridge (Z-up, CROSS_PLATFORM_DETERMINISTIC, StateRecorder)
│   │   ├── CollisionLayerRegister.{h,cpp}         // Broad-phase / domain layer filters (Static, Dynamic, Vehicle, Debris, Mannequin)
│   │   ├── ConstraintJoints.{h,cpp}               // Fixed, Hinge, Slider, Cone, SixDOF & VehicleConstraint wrappers
│   │   └── PhysicsQueries.{h,cpp}                 // Raycast, sphere/capsule/convex swept casts & overlap queries
│   │
│   ├── DeformableSpace/                           // Deterministic CPU + .slang 3D FEM, Curved Membrane FEM & XPBD proxy rigs
│   │   ├── ConstraintColoring.{h,cpp}             // Cook-time greedy batch coloring (eliminates GPU write races)
│   │   ├── Hex8ElasticitySolver.{h,cpp}           // 3D Hexahedral Neo-Hookean FEM (incl. 32×5 tire carcass elasticity)
│   │   ├── Tet4ElasticitySolver.{h,cpp}           // 3D Tetrahedral Corotated & Neo-Hookean FEM + volume conservation
│   │   ├── XPBDConstraintSolver.{h,cpp}           // Distance, shear, volume, tether & pneumatic pressure constraints
│   │   ├── ProxyCageCoupling.{h,cpp}              // ~50-pt cage/lattice builder + MVC/Barycentric skin weights for PolygonNetwork
│   │   └── VehicleTyreDynamics.{h,cpp}            // Direct port: TyreElasticity, TyreCarcassXPBD, Slip & Thermal solvers
│   │
│   ├── ClothSpace/                                // Real-fabric AVBD + OGC + Cubic Eigen-Strain + Cubic-Barrier CCD pattern engine
│   │   ├── TextileMaterialLibrary.{h,cpp}         // Physical GSM, crimp limit, cubic tensile wall, plate bend, HYLC curl & friction
│   │   ├── PatternDraftNetwork.{h,cpp}            // Metric 2D pattern contours, seam allowance, darts, pleats, notches & grainlines
│   │   ├── PatternTriangulator.{h,cpp}            // Metric isotropic Delaunay remesher (3–20 mm target spacing) + point color batches
│   │   ├── AVBDClothSolver.{h,cpp}                // Batch-colored 3×3 Newton AVBD + analytic SVD strain wall + M:N seam duals
│   │   ├── OGCContactSolver.{h,cpp}               // SIGGRAPH 2025 Offset Geometric Contact bounds + BVH/spatial broadphase
│   │   ├── CubicBarrierVerifier.{h,cpp}           // Optional offline Final Tailor Bake (Ando Cubic-Barrier + CCD precision check)
│   │   └── MannequinRig.{h,cpp}                   // Parametric anthropometric body sizing, pose joints & Jolt/SDF colliders
│   │
│   ├── FractureSpace/                             // Runtime & pre-baked structural fracture bridge into Jolt & SDFs
│   │   ├── FractureMaterialLibrary.{h,cpp}        // Density, tensile/shear toughness, Bouchaud H=0.78 roughness & restitution
│   │   ├── StressLatticeSolver.{h,cpp}            // Boussinesq Flat-BSP, 3D Navier–Cauchy FEM, Griffith, Mott, Voronoi & Dendritic
│   │   ├── StrainBondRegister.{h,cpp}             // Connectivity bond register, BFS island extraction & Jolt MutableCompound split
│   │   └── FragmentSDFUpdater.{h,cpp}             // R16F 32³/64³/128³ brick SDF generation into DistanceFieldSpace
│   │
│   ├── ParticleSimulation/                        // Tier A cosmetic GPU particles + Tier B Jolt debris spawners
│   │   ├── ParticleSpawner.{h,cpp}
│   │   ├── ParticleField.{h,cpp}
│   │   ├── ParticleIntegrator.{h,cpp}
│   │   └── ComputeParticleBuffer.{h,cpp}
│   │
│   └── FluidSimulation/                           // Eulerian smoke/fire + SPH/FLIP liquid + two-way Jolt coupling
│       ├── GasDynamics.{h,cpp}
│       ├── LiquidSolver.{h,cpp}
│       ├── AdvectionSolver.{h,cpp}
│       ├── VorticityConfinement.{h,cpp}
│       └── FluidBoundaryExchange.{h,cpp}          // Jolt body voxelization -> clamped buoyancy & drag impulses
│
├── Threading/Threads/Physics/
│   └── PhysicsThread.{h,cpp}                      // Dedicated fixed-timestep loop + 64-tick rollback ring & GPU timeline sync
│
├── Codecs/                                        // Two-Tier Hybrid .SimulationSpace / .VolumetricSpace / .ParticleSpace
│   ├── SimulationCodec.{h,cpp}                    // Encodes .RigidDynamics, .Fracture, .Deformable, .Garment, .Fluid sections
│   └── ParticleCodec.{h,cpp}
│
├── Interchange/
│   └── GarmentInterchange.{h,cpp}                 // 1:1 physical-scale SVG & DXF sewing pattern export + OBJ/glTF drape export
│
├── Shaders/Compute/                               // Deterministic Vulkan 1.2 SPIR-V .slang simulation & overlay units
│   ├── XPBDBatchProjection.slang                  // Color-batched deterministic XPBD constraints (0 float atomics)
│   ├── AVBDClothDispatch.slang                    // Color-batched 3×3 Newton AVBD + analytic SVD cubic eigen-strain wall + dual seams
│   ├── OGCContactBounds.slang                     // Offset Geometric Contact normal bounds + fixed-point micron accumulators
│   ├── Tet4Hex8Elasticity.slang                   // GPU 3D FEM internal forces & symplectic integration
│   ├── ProxyCageDeform.slang                      // ~50-pt proxy lattice -> full PolygonNetwork position/normal deformation
│   ├── PhysicsOverlayRasterization.slang          // GPU .slang Fracture bonds, SDF iso-section, Cloth strain & Softbody Jacobian heat
│   ├── ParticleIntegration.slang
│   └── FluidAdvection.slang
│
└── Workspaces/                                    // Shared Frontier / UVWorkspace-style interactive desktop & editor suites
    ├── Fracture/                                  // Breakables Outliner · 2D SDF Section + 3D Viewport · Solver/Material Inspector
    │   ├── FractureWorkspaceSession.{h,cpp}
    │   ├── FractureViewportHost.{h,cpp}
    │   ├── FractureOutlinerPanel.{h,cpp}
    │   ├── FractureInspectorPanel.{h,cpp}
    │   └── FractureWorkspaceHost.{h,cpp}
    ├── Cloth/                                     // Pattern Outliner · 2D Pattern + 3D Mannequin Viewports · Fabric/Fit Inspector
    │   ├── ClothWorkspaceSession.{h,cpp}
    │   ├── ClothViewportHost.{h,cpp}
    │   ├── ClothOutlinerPanel.{h,cpp}
    │   ├── ClothInspectorPanel.{h,cpp}
    │   └── ClothWorkspaceHost.{h,cpp}
    ├── Softbody/                                  // Lattice Outliner · 2D Cross-Section + 3D Deform Viewport · FEM Inspector
    │   ├── SoftbodyWorkspaceSession.{h,cpp}
    │   ├── SoftbodyViewportHost.{h,cpp}
    │   ├── SoftbodyOutlinerPanel.{h,cpp}
    │   ├── SoftbodyInspectorPanel.{h,cpp}
    │   └── SoftbodyWorkspaceHost.{h,cpp}
    ├── Fluid/                                     // Phase 2 port of Experimental/FluidEditor
    └── Particle/                                  // Phase 2 port of Experimental/ParticleEditor

Exhibits/
├── PhysicsValidation.cpp                          // Jolt Z-up deterministic stack, cross-run bit-parity & StateRecorder rollback + PNG
├── DeformableCageValidation.cpp                   // ~50-pt vehicle cage deform + 32×5 Hex8/XPBD tire + GPU ProxyCageDeform parity + PNG
├── FractureWorkspaceValidation.cpp                // All 6 fracture solvers, Jolt chunks, SDF sections & Frontier UI + PNG
├── ClothWorkspaceValidation.cpp                   // AVBD + OGC + Cubic strain wall drape, M:N sewing, Final Bake & SVG 1:1 pattern + PNG
└── SoftbodyWorkspaceValidation.cpp                // Tet4/Hex8 volume preservation, pneumatic cavity & proxy cage baking + PNG
```

---

## 8. Phased Implementation Roadmap

| Milestone | Scope & Key Deliverables | Verification Target |
| :---: | :--- | :--- |
| **P0 — Jolt Foundation** | Wire `frontier_add_jolt()` in `CMakeLists.txt`; build `PhysicsExtension`, `RigidBodyWorld` (Z-up, deterministic), `PhysicsQueries`, `PhysicsThread`, and `SimulationCodec`. | `PhysicsValidation` passes across `Editor`/`Development`/`Shipping` (bit-exact multi-run + `StateRecorder` rollback PNG). |
| **P1 — `DeformableSpace` & `~50`-Pt Proxy Demo** | Implement `ConstraintColoring`, `Hex8ElasticitySolver`, `Tet4ElasticitySolver`, `XPBDConstraintSolver`, `ProxyCageCoupling`, `VehicleTyreDynamics`, and `ProxyCageDeform.slang`. | `DeformableCageValidation`: verifies deterministic CPU/GPU parity, `~50`-point cage deforming `1k+`-tri `PolygonNetwork`, and `160`-point tires + PNGs. |
| **P2 — `FractureSpace` & `Fracture Workspace`** | Port all 6 `Experimental/FractureEditor/` solvers + `StrainBondRegister` + `JPH::MutableCompoundShape` split + `FragmentSDFUpdater` + full Frontier `FractureWorkspaceHost` UI. | `FractureWorkspaceValidation`: impacts, Voronoi/FEM cuts, SDF section overlay, Jolt debris settling + full UI PNG exhibits. |
| **P3 — `ClothSpace` & Next-Gen `Cloth Workspace`** | Implement `TextileMaterialLibrary`, `PatternDraftNetwork`, `PatternTriangulator`, `AVBDClothSolver` (`.slang` + analytic SVD cubic wall), `OGCContactSolver`, `CubicBarrierVerifier`, `MannequinRig`, `GarmentInterchange` (`1:1` `.svg`/`.dxf`), and `2D Pattern \| 3D Garment` UI. | `ClothWorkspaceValidation`: T-shirt / trouser pattern sewing & drape on mannequin, strain comfort heatmap, and verified `1:1` cutting SVG + PNGs. |
| **P4 — `Softbody Workspace`, Fluid Coupling & Replication** | Build `SoftbodyWorkspaceHost`, wire `FluidSimulation` + `FluidBoundaryExchange`, and integrate `PhysicsExtension` with `NetworkingSpace/` Priority Tier 0 rollback & delta snapshots. | `SoftbodyWorkspaceValidation` + dual-world rollback divergence = `0.0 m`. |

---

## 9. Immediate Decision Checkpoint Before Coding P0

| Choice | Options | Recommendation |
| :--- | :--- | :--- |
| **First Execution Sequence** | **A)** Implement **P0 (`JoltPhysics` + `PhysicsThread`) → P2 (`Fracture Workspace`)** first, then **P1 (`~50`-pt FEM/XPBD Demo) → P3 (`Cloth Workspace`)**, **OR** **B)** **P0 → P1 (`~50`-pt Vehicle/Softbody FEM Demo) → P2 (`Fracture`) → P3 (`Cloth`)** | 🏆 **Option B (`P0 → P1 → P2 → P3`)** — P1 gives you the live **~50-point GPU `3D FEM + XPBD` determinism & tire/vehicle deformation proof** immediately on top of Jolt before Fracture & Cloth build on it. |

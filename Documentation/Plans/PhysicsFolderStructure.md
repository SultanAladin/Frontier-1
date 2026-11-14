# Frontier Physics — Folder Structure and File Responsibilities

> **TL;DR:** The tree below is the **canonical layout for this repository** (`Frontier-1`) as it grows from an empty scaffold into the physics-bearing engine. Phase tags `[P0]…[P6]` state when each file lands. **Only `PhysicsSpace/Internal/` may include `JPH/*`**; every other path in this document talks to Frontier POD types.
> Naming follows `SKILL-Naming.md` + `BannedWordsVocabulary.md`: no `Manager`, `Handler`, `System`, `Record`, `mesh`, `frame`, `node`, `table`, `stage`, `pipeline`, `context`, `continuum`, `data`, `info`, `object`.

Companion: [PhysicsEnginePlan.md](PhysicsEnginePlan.md) · [ClothAuthoringPlan.md](ClothAuthoringPlan.md).

---

## 1. What must exist before the first line of physics is written

This repository currently contains a single `README.md`. P0 is therefore **three bootstraps in one**, in this order:

| # | Bootstrap | Contents |
| :---: | :--- | :--- |
| ① | **Governance** | `AGENTS.md`, `CLAUDE.md`, `Documentation/{FolderStructure,CodingStandards,BannedWordsVocabulary,ImportantNotes,Tracker,Backlog}.md`, `Documentation/AgenticInstructions/SKILL-*.md` — imported from `SultanAladin/Frontier` so both repositories share one ruleset |
| ② | **Build + dependencies** | `CMakeLists.txt` (`FRONTIER_PROFILE` Editor/Development/Shipping, `cxx_std_20`), `Automation/{Dependencies.json,PrepareDependencies.py,JoltPhysics.cmake,PrepareShaderToolchain.py}`, `ExternalPackages/` (git-ignored), Jolt **v5.6.0 / e77f175595** |
| ③ | **Engine floor** | `Engine/MicroUtils/`, `Engine/Constants/`, `Engine/Mathematics/`, `Engine/Threading/`, `Engine/Configuration/BuildConfiguration.h` — the minimum that `PhysicsSpace` compiles against |

Nothing below `SimulationSpace/` is allowed to land earlier than ①–③; otherwise the physics folder accretes its own ad-hoc allocator, clock and assertion macros.

---

## 2. Canonical tree (physics slice, phase-tagged)

```text
Frontier-1/
├── CMakeLists.txt                                        [P0]  C++20, three build profiles, Jolt + Slang wiring
├── AGENTS.md · CLAUDE.md                                 [P0]  agent entry point + non-negotiables
├── README.md                                                   architecture index
│
├── Automation/
│   ├── Dependencies.json                                 [P0]  Jolt v5.6.0 (e77f175595) SHA-256 pin + package set
│   ├── PrepareDependencies.py                            [P0]  checksum fetch → ExternalPackages/
│   ├── JoltPhysics.cmake                                 [P0]  CROSS_PLATFORM_DETERMINISTIC, USE_FMADD OFF, SSE4.2/NEON
│   ├── PrepareShaderToolchain.py                         [P0]  slangc → SPIR-V
│   └── Patches/                                                ImGui stack (only when the editor shell lands)
│
├── ExternalPackages/JoltPhysics/                         [P0]  git-ignored, populated by Automation
│
├── Documentation/
│   ├── Plans/PhysicsEnginePlan.md                              master plan (this plan set)
│   ├── Plans/PhysicsFolderStructure.md                         this document
│   ├── Plans/ClothAuthoringPlan.md                             pattern → sew → drape → bake
│   ├── Plans/SessionNotes.md                             [P0]  live notepad
│   ├── FolderStructure.md · CodingStandards.md                 governance imported in ①
│   ├── BannedWordsVocabulary.md · ImportantNotes.md            governance imported in ①
│   └── Tracker.md · Backlog.md                                 phase ledger + deferred items
│
├── Engine/
│   ├── MicroUtils/
│   │   ├── RegionAllocator.{h,cpp}                             monotonic simulation memory regions
│   │   ├── IndexedSlotRecycler.h                               body/constraint slot recycling
│   │   ├── TokenIssuer.h · TokenAuthentication.h                stale-detecting BodyToken / ProxyToken
│   │   ├── PackedBits.h                                        layer masks, contact bitfields
│   │   └── Assertion.h                                         invariant enforcement in Development/Editor
│   │
│   ├── Configuration/
│   │   ├── BuildConfiguration.h                                Editor / Development / Shipping policy
│   │   ├── EngineConfiguration.{h,cpp}                         ExecutionChannels budget allocation
│   │   └── ProjectConfiguration.{h,cpp}                        tick rate, replication policy, physics toggles
│   │
│   ├── Constants/
│   │   ├── PhysicsConstants.h                                  gravity, densities, friction/restitution clamps
│   │   ├── TimeConstants.h                             [P0]  FixedDeltaTime = 1/60, substep constants
│   │   └── MathConstants.h
│   │
│   ├── Mathematics/
│   │   ├── LinearAlgebra.h                                     Vec2/Vec3/Vec4/Mat3/Mat4, alignas(16)
│   │   ├── QuaternionRotation.{h,cpp}                          unit quats, smallest-three packing
│   │   ├── ScalarFunctions.h                                   branchless clamp/saturate/remap
│   │   ├── TrigonometricFunctions.h                            minimax sin/cos/atan2 (portable, no libm)
│   │   ├── DeterministicRandom.h                               PCG32 keyed by (tick, domain, index)
│   │   ├── SparseLinearSolver.{h,cpp}                          CG / sparse Cholesky (bake paths only)
│   │   └── FixedPointAccumulator.h                      [P1]  int64 micrometre accumulators for scattered impulses
│   │
│   ├── MetricSpace/
│   │   ├── SpatialExtent.h                                     AABB / OBB extents
│   │   ├── MetricTolerance.h                                   contact, weld and sewing tolerances
│   │   └── IntersectionSolver.{h,cpp}                          ray/face, plane cut, volume overlap
│   │
│   ├── SpatialAcceleration/
│   │   ├── DynamicAABBTree.{h,cpp}                             deterministic broadphase for our own domains
│   │   ├── SpatialGridHash.{h,cpp}                             self-contact and particle neighbour search
│   │   └── BoundingVolumeTree.{h,cpp}                          static geometry queries (SDF bakes)
│   │
│   ├── DistanceFieldSpace/
│   │   ├── SignedDistanceField.{h,cpp}                         narrow-band voxel SDF (cloth vs body)
│   │   ├── DistanceTransform.{h,cpp}                           fast sweeping / Maurer
│   │   ├── VolumetricDistance.{h,cpp}                          trilinear sample + gradient
│   │   └── DistanceQuery.{h,cpp}                               closest-point / ray march
│   │
│   ├── Threading/
│   │   ├── Sync/{AtomicQueue,CompletionCounter,SpinLock,ThreadProgressionTracker}
│   │   ├── Tasks/{TaskScheduler,TaskChannel,ExecutionChannels}
│   │   └── Threads/Physics/PhysicsThread.{h,cpp}        [P0]  owns the fixed tick; no I/O, no allocation
│   │
│   ├── SimulationSpace/
│   │   ├── PhysicsSpace/                                [P0]  rigid mechanics (Jolt-backed)
│   │   │   ├── PhysicsExtension.{h,cpp}                        domain lifecycle coordinator, tick orchestration
│   │   │   ├── PhysicsTickOrder.h                              the ordered domain list (§4 of the master plan)
│   │   │   ├── BodyRegister.{h,cpp}                            monotonic body slots, deferred destruction
│   │   │   ├── RigidBodyDynamics.{h,cpp}                       fixed-step stepping, sleeping, island policy
│   │   │   ├── BroadphaseSolver.{h,cpp}                        layer-pair filtering, query intake
│   │   │   ├── CollisionDetection.{h,cpp}                      shape casts, manifold intake, contact filtering
│   │   │   ├── ConstraintSolver.{h,cpp}                        joint authoring → Jolt constraint descriptors
│   │   │   ├── ContactImpulseRegister.{h,cpp}                  quantised impulse read-out for other domains
│   │   │   ├── ShapeCastQuery.{h,cpp}                          suspension casts, particle casts, character sweeps
│   │   │   ├── VehicleDynamics.{h,cpp}                         chassis, suspension, drivetrain, aero
│   │   │   ├── TyreCarcassLattice.{h,cpp}                      32×5 = 160-node Hex8 tyre deformation
│   │   │   ├── TyreSlipSolver.{h,cpp}                          Pacejka MF6.1 + relaxation length
│   │   │   ├── TyreThermalSolver.{h,cpp}                       surface/bulk temperature, pressure, wear
│   │   │   ├── CharacterSweepSolver.{h,cpp}                    capsule sweep, slopes, step-up
│   │   │   └── Internal/                                 🔴 the ONLY folder that may include JPH/*
│   │   │       ├── JoltWorldBridge.{h,cpp}                     JPH::PhysicsSystem lifetime, gravity, step counts
│   │   │       ├── JoltBodyBridge.{h,cpp}                      BodyToken ↔ JPH::BodyID, activation, motion type
│   │   │       ├── JoltShapeEncoder.{h,cpp}                    ShapeDescriptor → hull/compound/mutable compound
│   │   │       ├── JoltLayerBridge.{h,cpp}                     object/broadphase layer pairs + filters
│   │   │       ├── JoltContactBridge.{h,cpp}                   manifold → ContactImpulse POD
│   │   │       └── JoltStateBridge.{h,cpp}                     StateRecorder snapshot / restore for rollback
│   │   │
│   │   ├── Determinism/                                 [P1]
│   │   │   ├── DeterminismTier.h                               constexpr tier declaration per domain
│   │   │   ├── FixedStepClock.{h,cpp}                          integer substeps, clamped catch-up
│   │   │   ├── SimulationHashRegister.{h,cpp}                  FNV-1a over 1 µm-quantised positions
│   │   │   ├── ReplayLedger.{h,cpp}                            90-tick input + state ring, resim entry point
│   │   │   └── QuantisedPoseCodec.{h,cpp}                      22-bit position / smallest-three rotation packing
│   │   │
│   │   ├── DeformableSpace/                             [P1]
│   │   │   ├── DeformableDynamics.{h,cpp}                      proxy lattice stepping at 240 Hz
│   │   │   ├── ProxyLattice.{h,cpp}                            48–200 point cage + cell register
│   │   │   ├── LatticeEmbeddingRegister.{h,cpp}                4-point weights + orthonormal basis per vertex
│   │   │   ├── Hex8ElasticitySolver.{h,cpp}                    corotated / Neo-Hookean, J-clamped
│   │   │   ├── Tet4ElasticitySolver.{h,cpp}                    soft-body volume path
│   │   │   ├── XpbdConstraintRegister.{h,cpp}                  distance/bend/volume constraints + compliance
│   │   │   ├── ConstraintColorPartition.{h,cpp}                greedy colouring at cook time → no atomics
│   │   │   ├── PressureConstraintSolver.{h,cpp}                inflatables, tyre air column
│   │   │   └── ProxyContactCoupler.{h,cpp}                     kinematic proxy ↔ Jolt body impulse exchange
│   │   │
│   │   ├── ClothSpace/                                  [P3]
│   │   │   ├── ClothDynamics.{h,cpp}                           authoring drape orchestration, 600 Hz substeps
│   │   │   ├── PatternPieceRegister.{h,cpp}                    pieces, holes, internal lines, grainline
│   │   │   ├── PatternBoundaryLoop.{h,cpp}                     ordered boundary chains + arc-length parameter
│   │   │   ├── PatternGridSampler.{h,cpp}                      piece → uniform quad grid (5–10 mm edges)
│   │   │   ├── SeamChainLinker.{h,cpp}                         stitch pairing, spacing resample, seam constraints
│   │   │   ├── FabricSpecification.{h,cpp}                     gsm, elongation@50 N, bending length, drape coeff.
│   │   │   ├── FabricRepository.{h,cpp}                        preset library (*.FabricSpace)
│   │   │   ├── FabricAnisotropyRegister.{h,cpp}                per-edge compliance from warp/weft/bias angle
│   │   │   ├── XpbdSubstepSolver.{h,cpp}                       stretch/shear/bend/LRA projection
│   │   │   ├── StrainLimitProjector.{h,cpp}                    hard strain clamp (kills the rubber look)
│   │   │   ├── DrapeCollisionSolver.{h,cpp}                    body SDF contact + friction + thickness
│   │   │   ├── SelfContactSolver.{h,cpp}                       PT/EE contacts, hash buckets, fixed-point resolve
│   │   │   ├── ClothBakeRegister.{h,cpp}                       16-bit quantised pose cache @ 30 Hz
│   │   │   ├── PatternGradingRegister.{h,cpp}                  size-run grade increments per landmark
│   │   │   ├── FabricConsumptionNester.{h,cpp}                 piece nesting on fabric width → consumption
│   │   │   └── ClothSurfaceProjection.{h,cpp}                  render surface, stitch lines, seam shadows
│   │   │
│   │   ├── SoftbodySpace/                               [P4]
│   │   │   ├── SoftbodyDynamics.{h,cpp}                        tet lattice + region stiffness
│   │   │   ├── VolumeTetrahedralizer.{h,cpp}                   constrained Delaunay with preserved boundary
│   │   │   ├── RegionStiffnessRegister.{h,cpp}                 painted material regions
│   │   │   ├── InflatablePressureSolver.{h,cpp}                pressure/volume constraint
│   │   │   └── SoftbodyAttachmentCoupler.{h,cpp}               rigid attachment through proxy points
│   │   │
│   │   ├── FractureSpace/                               [P2]
│   │   │   ├── FractureDynamics.{h,cpp}                        event intake, body swap, chunk stepping hand-off
│   │   │   ├── VoronoiCellPartitioner.{h,cpp}                  seeded 3D Voronoi, deterministic cell order
│   │   │   ├── BondStrainLattice.{h,cpp}                       adjacency bonds, area, accumulated strain
│   │   │   ├── CrackPropagationQueue.{h,cpp}                   max-heap ordered by (strain, bondIndex)
│   │   │   ├── ConvexHullExtractor.{h,cpp}                     clipping + hull repair + watertight guarantee
│   │   │   ├── InteriorSdfBaker.{h,cpp}                        32³/64³/128³ R16F interior bricks
│   │   │   ├── ChunkBudgetRegister.{h,cpp}                     live-chunk cap, settled merge, despawn radius
│   │   │   └── FracturePatternRepository.{h,cpp}               baked hero patterns (*.FractureSpace)
│   │   │
│   │   ├── ParticleSimulation/                          [P5]
│   │   │   ├── ParticleSpawner.{h,cpp}                         SpawnToken intake, deterministic index order
│   │   │   ├── ParticleField.{h,cpp}                           curl noise, vortex, attractor force fields
│   │   │   ├── ParticleIntegrator.{h,cpp}                      SoA integrator, fixed worker partitions
│   │   │   ├── ParticleContactQuery.{h,cpp}                    batched casts into Jolt, max 4 contacts/tick
│   │   │   └── ComputeParticleBuffer.{h,cpp}                   GPU buffer hand-off, indirect draw arguments
│   │   │
│   │   ├── FluidSimulation/                             [P5]
│   │   │   ├── GasDynamics.{h,cpp}                             MAC grid, buoyancy, combustion, sparse bricks
│   │   │   ├── LiquidSolver.{h,cpp}                            FLIP/APIC primary, PBF shallow
│   │   │   ├── AdvectionSolver.{h,cpp}                         semi-Lagrangian + MacCormack
│   │   │   ├── VorticityConfinement.{h,cpp}                    curl re-injection
│   │   │   ├── FluidBoundarySolver.{h,cpp}                     solid boundary conditions
│   │   │   └── FluidBoundaryExchange.{h,cpp}                   30 Hz clamped, quantised two-way impulses
│   │   │
│   │   └── Motion/                                             (existing procedural motion)
│   │
│   ├── NetworkingSpace/                                 [P6]
│   │   ├── NetworkingExtension.{h,cpp}                         lifecycle coordinator, tick alignment
│   │   ├── ReplicationPolicy.h                                 ServerOnly / PredictiveLocal / DeterministicProxy / EventOnly / VisualOnly
│   │   ├── CommandIntakeRegister.{h,cpp}                       tick-stamped input commands, de-duplication
│   │   ├── SnapshotEncoder.{h,cpp} · SnapshotDecoder.{h,cpp}   quantised delta snapshots
│   │   ├── PredictionReconciler.{h,cpp}                        local prediction + correction blend
│   │   ├── InterestPriorityRegister.{h,cpp}                    distance/relevance priority tiers, budgets
│   │   └── ProxyStateChallenge.{h,cpp}                         hash comparison → full-state fallback
│   │
│   ├── Codecs/
│   │   ├── SimulationCodec.{h,cpp}                             .SimulationSpace
│   │   ├── FractureCodec.{h,cpp}                               .FractureSpace
│   │   ├── ClothCodec.{h,cpp}                                  .ClothSpace (pattern + bake cache)
│   │   └── SoftbodyCodec.{h,cpp}                               .SoftbodySpace
│   │
│   ├── Interchange/
│   │   ├── PatternInterchange.{h,cpp}                          DXF-AAMA/ASTM import, 1:1 SVG/DXF export
│   │   ├── AnimationInterchange.{h,cpp}                        Alembic bake export
│   │   └── GeometryInterchange.{h,cpp}                         OBJ / glTF import-export
│   │
│   ├── Shaders/Simulation/
│   │   ├── ProxyCageDeform.slang                        [P1]  4-point weighted skin of 1k–100k triangle surfaces
│   │   ├── ClothConstraintBatch.slang                   [P3]  colour-batched XPBD projection, ping-pong positions
│   │   ├── ClothSelfContact.slang                       [P3]  PT/EE broadphase hash + fixed-point resolve
│   │   ├── ClothSurfaceShading.slang                    [P3]  double-sided sheen, stitch lines, seam AO
│   │   ├── SoftbodyVolumeProjection.slang               [P4]  tet volume + pressure constraints
│   │   ├── InteriorSdfBake.slang                        [P2]  32³/64³/128³ R16F brick bake
│   │   ├── FractureStressOverlay.slang                  [P2]  bond-strain heat overlay (GPU only, never ImGui)
│   │   ├── ParticleIntegration.slang                    [P5]  curl-noise advection, counter-based RNG
│   │   ├── FluidAdvection.slang · FluidPressureProjection.slang [P5]
│   │   └── FluidParticleToGrid.slang                    [P5]  gather-based P2G (zero float atomics)
│   │
│   ├── Workspaces/
│   │   ├── Physics/                                     [P2]  PhysicsWorkspaceSession, PhysicsOutlinerPanel,
│   │   │                                                       PhysicsInspectorPanel, PhysicsViewportHost
│   │   ├── Fracture/                                    [P2]  FractureWorkspaceSession, FractureOutlinerPanel,
│   │   │                                                       FractureInspectorPanel (Pattern/Material/Bonds/
│   │   │                                                       Impact/Debris/Bake), ImpactBrushInteractor,
│   │   │                                                       StressOverlayRasterization
│   │   ├── Cloth/                                       [P3]  ClothWorkspaceSession, ClothOutlinerPanel,
│   │   │                                                       ClothInspectorPanel (Pattern/Seam/Fabric/Fit/
│   │   │                                                       Collision/Simulate/Bake), ClothViewportHost,
│   │   │                                                       PatternSheetInteractor, SeamPaintInteractor,
│   │   │                                                       FabricPresetBrowser, ClothPaneAppearance
│   │   └── Softbody/                                    [P4]  SoftbodyWorkspaceSession, SoftbodyOutlinerPanel,
│   │                                                           SoftbodyInspectorPanel (Volume/Regions/Pressure/
│   │                                                           Attach/Collision/Bake), CrossSectionInteractor
│   │
│   └── Annotation/                                             DiagnosticPrimitives, SpatialAnnotation
│                                                               (collision wireframes, chunk bounds, seam previews)
│
├── EngineContent/
│   ├── Fabrics/*.FabricSpace                            [P3]  cotton poplin, silk, denim, wool, leather presets
│   ├── FracturePatterns/*.FractureSpace                  [P2]  baked hero patterns
│   └── Physics/PhysicsWorld.*.SimulationSpace            [P0]  default world templates each workspace clones
│
├── Experimental/                                               HTML/JS reference studios only — zero C++
│   ├── ClothStudio/                                     [D1]  pattern sheet + 3D drape + substep/hash readout
│   ├── SoftbodyStudio/                                  [P4]
│   ├── FractureStudio/                                         port of the existing FractureEditor reference
│   └── Shared/Mannequin/                                       preserved 10,764 vert / 21,524 tri avatar, 5 presets
│
├── Exhibits/
│   ├── JoltBridgeValidation.cpp                         [P0]
│   ├── PhysicsDeterminismValidation.cpp                 [P1]
│   ├── DeformableProxyValidation.cpp                    [P1]
│   ├── FractureValidation.cpp                           [P2]
│   ├── ClothDrapeValidation.cpp · ClothSewValidation.cpp [P3]
│   ├── SoftbodyValidation.cpp                           [P4]
│   ├── ParticleValidation.cpp · FluidCouplingValidation.cpp [P5]
│   ├── ReplicationValidation.cpp                        [P6]
│   └── Images/{JoltBridge,Determinism,Deformable,Fracture,Cloth,Softbody,Particle,Fluid,Replication}/*.png
│
└── Projects/SampleProject/{Content,Source,Saved}/
```

---

## 3. Integration rules enforced by this layout

| Rule | Mechanism | Failure it prevents |
| :--- | :--- | :--- |
| **Jolt isolation** | `JPH/*` only inside `PhysicsSpace/Internal/`; include-lint in CI scans translation units | third-party types leaking into workspaces, untestable code, no solver swap path |
| **No allocation on the tick** | `RegionAllocator` per tick phase, `IndexedSlotRecycler` for bodies/constraints; `PhysicsThread` asserts allocation count = 0 | frame-time spikes, pointer-order nondeterminism |
| **Tier separation** | `DeterminismTier.h` declares each domain; tier-C buffers are separate types, not writable through tier-A APIs | cosmetic simulation silently corrupting authoritative state |
| **One clock** | `FixedStepClock` is the only producer of `Δt`; no domain accepts a wall-clock delta | determinism collapse |
| **One hash** | `SimulationHashRegister` reads every authoritative buffer in a fixed order | silent divergence |
| **GPU overlays** | all debug/heat/seam/bounds visuals are `*.slang` `…Rasterization` units | ImGui draw-list overlays (banned) |
| **Every exhibit emits `.png`** | CPU rasterizer fallback in `Exhibits/` | "it compiles" accepted as proof |

---

## 4. Naming notes for this slice (banned-word avoidance)

| Concept | Not this | This |
| :--- | :--- | :--- |
| Dense visual surface | `ClothMesh`, `SoftbodyMesh` | `PolygonNetwork`, `ClothSurfaceProjection` |
| Tick | `SimFrame`, `FrameState` | `PhysicsTick`, `TickCounter`, `FixedStepClock` |
| Spatial dictionary | `ContactTable`, `GridMap` | `ContactImpulseRegister`, `SpatialGridHash` |
| Coordination | `PhysicsManager`, `PhysicsSystem` | `PhysicsExtension` (approved lifecycle coordinator only) |
| Container of patterns | `PatternStore`, `PatternCatalog` | `FracturePatternRepository`, `FabricRepository` |
| Tree links | `ParentPiece`, `ChildSeam` | `EnclosingPieceIndex`, `FirstNestedSeamIndex` |
| Test scaffold | `TestHarness`, `Trial` | `…Validation` |

🔴 `PhysicsSystem` may appear **only** as the Jolt C++ type (`JPH::PhysicsSystem`) inside `Internal/`, never as a Frontier identifier.

---

## 5. CMake targets proposed in P0

| Target | Sources | Notes |
| :--- | :--- | :--- |
| `FrontierEngine` | `Engine/**` excluding `Internal/` | C++20, no exceptions in tick paths, `/fp:precise` or `-ffp-contract=off` |
| `FrontierPhysicsJolt` | `Engine/SimulationSpace/PhysicsSpace/Internal/**` | the only target linking `Jolt`; `PRIVATE` include of `ExternalPackages/JoltPhysics` |
| `FrontierSimulation` | `SimulationSpace/**` except `Internal/` | depends on `FrontierPhysicsJolt`, `FrontierEngine` |
| `FrontierNetworking` | `NetworkingSpace/**` | depends on `FrontierSimulation` |
| `FrontierValidation<Domain>` | `Exhibits/<Domain>Validation.cpp` | headless, `PlatformWindowStub`, writes `.png` proofs |
| `FrontierEditor` | `Engine/Workspaces/**`, `Engine/Editor/**` | only in `FRONTIER_PROFILE_EDITOR` |

`frontier_add_jolt()` is invoked **once**, from `CMakeLists.txt`, guarded by `if(TARGET Jolt) return()` exactly as in the inherited `Automation/JoltPhysics.cmake`.

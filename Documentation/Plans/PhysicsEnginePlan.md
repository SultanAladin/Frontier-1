# Frontier Physics Engine — Master Plan

> **TL;DR:** Jolt is the **rigid-body, contact, joint, vehicle-chassis and rollback** solver — it is **not** a particle, fluid, fracture or authoring-cloth solver. Frontier therefore ships **five solvers under one fixed 60 Hz tick** with an explicit **determinism tier per domain**:
> **① Jolt v5.6.0** (`CROSS_PLATFORM_DETERMINISTIC ON`) → rigid bodies, joints, vehicles, character, fracture chunks, rollback (`StateRecorder`).
> **② `FractureSpace`** → seeded Voronoi / bond-strain lattice / SDF interior bricks → chunk bodies handed to Jolt **(this is the workspace you asked to build first)**.
> **③ `DeformableSpace` + `ClothSpace`** → custom **3D FEM (Hex8/Tet4) + XPBD small-steps**, solved on a **compact proxy lattice (48–200 points)** on CPU for authority, with GPU skinning of the dense `PolygonNetwork` visual.
> **④ `ParticleSimulation` + `FluidSimulation`** → our own deterministic CPU particles (contact queried through Jolt) and GPU volumetric gas/liquid (visual tier), coupled to Jolt by clamped impulses.
> **⑤ `NetworkingSpace`** → server-authoritative ticks, rollback ring, and `ReplicationPolicy` per domain (state / deterministic-proxy / event-only / visual-only).
>
> 🔴 **The one rule that makes GPU + determinism + networking coexist:** *authority is always small and on CPU; beauty is always large and on GPU.* A 50–200 point proxy is bit-exact, rollback-able and 600 B on the wire; the 1k–100k triangle visual is a pure function of that proxy.

**Repository:** `SultanAladin/Frontier-1` (branch `arena/32db5452-frontier-1`) — currently a fresh scaffold (`README.md` only). **P0 therefore also bootstraps the build, dependency and governance baseline**, not just physics.
**Companion documents:** [PhysicsFolderStructure.md](PhysicsFolderStructure.md) (canonical file tree) · [ClothAuthoringPlan.md](ClothAuthoringPlan.md) (pattern → sew → drape → bake).
**Inherited references:** `SultanAladin/Frontier` `Documentation/Plans/PhysicsArchitecturePlan.md`, `ClothAuthoringRestartResearch.md`, `Automation/JoltPhysics.cmake`, `Engine/Constants/PhysicsConstants.h`, `Docs/VehicleHmiEnginePlan.md`, `Experimental/SlateReference/A/Exhibits/VehicleProvingGrounds/Source/{VehicleSolver,TyreSlipSolver,TyreThermalSolver,HullStructure}.js`.

---

## 1. Scope, Non-Scope, and the Three Questions You Actually Asked

| Question you asked | Answer in this plan |
| :--- | :--- |
| *"Where does Jolt handle physics, and does it cover particles?"* | Jolt covers **rigid bodies, contacts, joints, soft bodies, vehicles, characters and rollback**. It has **no** particle solver, **no** fluid solver, **no** fracture solver, **no** sewing/pattern cloth, and **no** GPU compute. §2 states exactly what each domain gets from Jolt. |
| *"We need deterministic results — how?"* | Determinism is **not one global switch**; it is a **contract per domain** with three tiers (§3). Cross-platform bit-exactness is only required where you actually need it (rigid bodies, fractured chunks, network proxies). GPU cloth is **device-reproducible**, not cross-vendor bit-exact — and that is sufficient for authoring and bake. |
| *"Cloth + soft body on GPU, deterministic, networked — is that possible?"* | Yes, by **dual representation** (§3.4): solve the small proxy deterministically (CPU, bit-exact, rollback-able), skin the dense visual on GPU (tier C, cosmetic), and replicate the proxy + inputs — never the dense surface. |

**In scope:** rigid dynamics, vehicles, character, fracture, deformable solids (FEM/XPBD), garment cloth authoring, soft-body authoring, particles, volumetric fluid, replication, deterministic replay, editor workspaces, bake/export.
**Out of scope for P0–P3:** differentiable physics, ML material estimation, film-grade offline FEM, destructible terrain streaming, hair/fur grooming (a later `StrandSpace`).

---

## 2. Domain Responsibility Matrix — What Jolt Does and Does Not Do

| Domain | Solver host | What Jolt contributes | What we must write | Determinism tier | Replicated? |
| :--- | :--- | :--- | :--- | :---: | :--- |
| **Rigid bodies, joints, stacks** | Jolt (`PhysicsThread`) | Broadphase BVH, GJK/SAT narrowphase, manifolds, sleeping, CCD, constraints | Layering policy, fixed step counts, body register, snapshot/rollback | 🔴 **A** | ✔️ state |
| **Vehicle chassis** | Jolt body + our forces | One dynamic body, suspension ray/shape casts, contacts | MF6.1 tyre forces, drivetrain, aero, thermal, carcass lattice | 🔴 **A** | ✔️ state |
| **Character / player capsule** | Jolt `CharacterVirtual` | Sweep, slopes, step-up, moving platforms | Input commands, animation hooks | 🔴 **A** | ✔️ predictive |
| **Fracture chunks** | Jolt `ConvexHullShape` bodies | Contacts between hundreds of chunks, sleeping, compound/mutable compound | Voronoi partition, bond lattice, crack queue, SDF interior, budgets | 🔴 **A** | ✔️ event + state |
| **Game cloth (flags, tarps, 1–3k verts)** | Jolt `SoftBodySharedSettings` **or** `ClothSpace` runtime tier | Jolt: edge/dihedral/volume/LRA constraints, skinning, contacts, deterministic | Decision per asset (§6.4); authoring cloth stays `ClothSpace` | 🔴 **A** | ✔️ proxy |
| **Authoring garments (5k–60k verts)** | `ClothSpace` XPBD small-steps | — | Pattern → grid → seams → XPBD + self-collision + bake | 🚩 **B** | ⏳ bake only |
| **Soft bodies (gel, foam, inflatables)** | `DeformableSpace` FEM+Tet4/Hex8 | Contact via proxy points | Tetrahedralisation, corotated FEM, pressure, region stiffness | 🚩 **B** (A if proxied) | ✔️ optional |
| **Tyres & crash cages (~50–160 pts)** | `DeformableSpace` proxy | Wheel/ground contacts via casts | Hex8 Neo-Hookean / orthotropic membrane / XPBD belts | 🔴 **A** | ✔️ proxy |
| **Gameplay particles (debris that matters)** | Jolt bodies (capped) + our integrator | Shape casts, contact impulses | Particle register, budget, spawn policy | 🔴 **A** | ✔️ capped |
| **Cosmetic particles (sparks, dust)** | `ParticleSimulation` GPU | — | Curl-noise fields, counter-based RNG, indirect draw | ✔️ **C** | ❌ seed only |
| **Volumetric gas (smoke, fire)** | `FluidSimulation` GPU | — | MAC grid, MacCormack advection, vorticity, combustion | ✔️ **C** | ❌ event only |
| **Liquid affecting gameplay** | `FluidSimulation` GPU/CPU | Impulse exchange to bodies | FLIP/APIC P2G by gather, buoyancy, drag | 🚩 **B** | ✔️ coarse |

💡 **Consequence for the architecture:** Jolt is a **dependency of one folder** (`SimulationSpace/PhysicsSpace/Internal/`) and a **service to the others**. No other domain includes a `JPH/*` header — the bridge exposes only Frontier types. This is what lets each domain choose its own determinism tier without entangling the whole engine.

---

## 3. Determinism Architecture

### 3.1 Three tiers, declared in code

| Tier | Guarantee | Domains | Mechanism | Verification gate |
| :---: | :--- | :--- | :--- | :--- |
| **A** | **Bit-exact across Windows / Linux / Android, x86_64 + ARM64** | rigid, joints, vehicles, character, fracture chunks, gameplay particles, network proxies | Jolt `CROSS_PLATFORM_DETERMINISTIC ON`, `USE_FMADD OFF`, baseline SSE4.2 / NEON (never `-march=native`), fixed job-thread count, `-ffp-contract=off` / `/fp:precise`, `-ffast-math` forbidden, deterministic sorts with index tie-break, counter-based PCG32 | identical 128-bit position hash across 3 platforms × 100k ticks, in CI |
| **B** | **Reproducible on the same device + driver + binary** | authoring cloth, soft bodies, gameplay liquid | zero float atomics, colour-batched constraints, fixed dispatch order, fixed substep count, own polynomial transcendentals, fixed-point impulse accumulators | position hash every 60 ticks over a 3,600-tick replay; divergence `= 0` |
| **C** | **Seeded visual replay only** | smoke, fire, sparks, dust, secondary debris | counter-based RNG keyed by `(tick, index)`; seed stored in the document | perceptual image tolerance only |

🔴 **Hard invariant:** a tier-C domain may read tier-A/B state but **may never write it**. Enforced by buffer ownership: tier-C writes only its own `ComputeParticleBuffer`; any impulse it proposes must pass through `ContactImpulseRegister` with a clamp and a quantisation step.

### 3.2 The only floating-point operations that are portable

`+ − × ÷ √` and comparisons are correctly rounded under round-to-nearest-even **provided contraction into FMA is disabled and the rounding mode is unchanged**. `sin`, `cos`, `exp`, `pow`, `atan2`, `rsqrt` are **vendor-specific** and must never appear on a tier-A/B authoritative path.

- Use `Engine/Mathematics/TrigonometricFunctions.h` minimax polynomials (same coefficients in C++ and `.slang`) whenever a transcendental is needed.
- `-ffast-math`, `-Ofast`, `-funsafe-math-optimizations`, `-march=native` are **banned** repo-wide.
- Denormal handling must be identical: keep `-fno-fast-math` and never rely on flush-to-zero.

### 3.3 Where determinism actually breaks in practice

| Breakage | Symptom | Fix written into this plan |
| :--- | :--- | :--- |
| Variable Δt (accumulator passed straight through) | different results per frame rate | `FixedStepClock` → always integer substeps, clamp to 4 ticks/present |
| Adaptive substep count | "converges" differently per machine | substep count is a **document property**, never runtime-adaptive |
| Float `atomicAdd` in GPU scatter | non-reproducible jitter | colour-batched dispatch (no atomics) + `FixedPointAccumulator` (int64 µm) for self-contact |
| Thread-count-dependent reduction | different contact order per CPU core count | fixed worker partition + fixed combine order in `ExecutionChannels`; Jolt `JobSystem` thread count pinned |
| `std::unordered_map` / pointer-keyed containers in the hot path | iteration order varies | only index-ordered arrays, radix-sorted integer keys |
| `std::sort` without tie-break | unstable ordering of equal weights | every comparator ends with index comparison |
| Time- or address-derived RNG | impossibility of replay | `DeterministicRandom` keyed by `(tick, domain, index)` |
| Body creation order changes | Jolt island/contact order changes | `BodyRegister` allocates monotonically; destruction is deferred to tick end |

### 3.4 Dual representation — the answer to "GPU + deterministic + networked"

```text
   AUTHORITY  (small, CPU, tier A/B, rollback-able, replicated)
   ┌───────────────────────────────────────────────────────────────┐
   │ ProxyLattice  48–200 points · 0.4–2.4 KiB                       │
   │  • cloth: seam/attachment points, collision proxy, fit markers  │
   │  • soft body: Hex8/Tet4 lattice                                 │
   │  • tyre: 32×5 = 160 nodes over 128 Hex8 cells                   │
   └───────────────────────────────┬───────────────────────────────┘
                                   │ 6 KiB/frame upload (double-buffered)
                                   ▼
   PRESENTATION  (large, GPU, tier C, never authoritative)
   ┌───────────────────────────────────────────────────────────────┐
   │ ProxyCageDeform.slang                                          │
   │  • per-vertex 4-point mean-value / barycentric weights + basis │
   │    baked at cook time → 1k–100k triangle `PolygonNetwork`      │
   │  • recomputes position, tangent, normal per vertex in O(1)     │
   └───────────────────────────────────────────────────────────────┘
```

- **Authoring** runs the **dense** solver (tier B) so that the artist sees the true drape; **bake** writes positions; **runtime** uses the dense bake *or* the proxy tier.
- **Networking** replicates the **proxy** (≈ 600 B at 10 Hz, delta + hash) plus **inputs** (wind seed, pin/attachment changes, impact events). Dense surfaces are reconstructed identically on every client via the baked weights.
- **Rollback** rewinds only proxies (`ReplayLedger` ring of 90 ticks ≈ 216 KiB/garment) — never vertex buffers.

### 3.5 Determinism verification tooling

| Tool | Responsibility | Gate |
| :--- | :--- | :--- |
| `FixedStepClock` | integer substeps, clamped catch-up, tick counter as the global time authority | tick count identical for identical input streams |
| `SimulationHashRegister` | FNV-1a over positions quantised to 1 µm (`int32`), per domain, every N ticks | hash equality across platforms / replay |
| `ReplayLedger` | records input commands + tier-A `StateRecorder` blobs | bit-identical re-simulation from tick 0 |
| `DeterminismTier.h` | `constexpr` declaration per domain, `static_assert` that no tier-C buffer is writable by a tier-A path at the type level | compile-time |

---

## 4. Tick Model, Threading and Ordering

**Fixed 60 Hz authoritative tick** (`Δt = 1/60 s`). All sub-domains substep inside it at fixed integer counts.

| Domain | Inner rate | Inner steps / tick | Budget / tick |
| :--- | :--- | :--- | ---: |
| Rigid bodies (Jolt) | 60 Hz, velocity steps 8 / position steps 2 | 1 | 3.0 ms |
| Vehicle forces | 480 Hz | 8 | 1.5 ms |
| Deformable proxies | 240 Hz | 4 | 1.0 ms |
| Cloth authoring (dense) | 600 Hz | 10 | 10.0 ms (authoring only) |
| Fracture events | event-driven, drained once | 1 | 1.5 ms |
| Particle (deterministic) | 60 Hz | 1 | 0.8 ms |
| Fluid | 30 Hz (every 2nd tick) | 1 per 2 ticks | 3.0 ms (GPU async) |
| Fluid ⇄ body coupling | 30 Hz | 1 | 0.3 ms |
| Publish + hash | 60 Hz | 1 | 0.3 ms |

**Order inside one tick (never reordered — the order *is* part of the determinism contract):**

1. `CommandIntakeRegister` — inputs, scripted forces, network commands (tick-stamped, de-duplicated).
2. `RigidBodyDynamics` — Jolt step (contacts, joints, sleeping).
3. `ContactImpulseRegister` — contact impulses read once, quantised.
4. `DeformableDynamics` / `ClothDynamics` — proxy or dense substeps, with body impulses applied as clamped external forces.
5. `FluidBoundaryExchange` — every 2nd tick, clamped two-way impulse exchange.
6. `FractureDynamics` — deterministic event queue drained; bodies swapped.
7. `ParticleIntegrator` — deterministic register, GPU tier dispatched.
8. `Publish` — double-buffered transforms + proxy buffers to render; hash register update; snapshot if due.

**Threading:** `PhysicsThread` owns the tick; `ExecutionChannels` gives it a fixed slice; deformables may fan out to N fixed worker partitions; Jolt runs its own job system with a **pinned thread count equal to the configured budget** (never `hardware_concurrency()` at runtime). Overrun policy: drop tier-C work first, then fluid, then particles — never rigid, never proxies.

---

## 5. Jolt Integration Boundary

### 5.1 Isolation rule

```
Engine/SimulationSpace/PhysicsSpace/Internal/   ← the ONLY place JPH/* may be included
```

Public headers expose Frontier PODs only: `BodyToken`, `ShapeDescriptor`, `ContactImpulse`, `PoseState`, `MotionDescriptor`. This preserves the ability to swap or dual-run the solver (a Jolt world and a reference world side by side in validation) and keeps compile times sane.

### 5.2 Build definition (inherited verbatim from `Automation/JoltPhysics.cmake`)

| Setting | Value | Why |
| :--- | :--- | :--- |
| Version | **v5.6.0 (`e77f175595`)** | pinned, SHA-256 verified via `Dependencies.json` |
| `CROSS_PLATFORM_DETERMINISTIC` | **ON** | the only reason cross-platform replay is possible |
| `USE_FMADD` | **OFF** | prevents contraction differences |
| `USE_SSE4_1/2`, `USE_AVX*`, `USE_LZCNT/TZCNT/F16C` | 4.2 ON (x86_64 only), AVX* OFF, bit tricks OFF | baseline ISA only; NEON path on ARM64 |
| `DOUBLE_PRECISION` | OFF | Jolt uses `float`; world is in metres |
| `OBJECT_LAYER_BITS` | 16 | room for gameplay layers |
| Debug renderer / profiler | OFF in all profiles | no hidden state, no instrumentation drift |

### 5.3 World configuration

- Gravity `-9.80665 m/s²` on Z-up (`Frontier::Constants::StandardGravity`).
- One `JPH::PhysicsSystem` per **simulation world** (editor preview gets its own); a world owns its `BodyRegister`, layers, and `StateRecorder`.
- Body creation order is monotonic (`BodyRegister`), destruction deferred to end of tick.
- Layers: `StaticWorld`, `DynamicProp`, `VehicleBody`, `Debris`, `Character`, `Sensor`, `FluidProxy`, `DeformableProxy` — each with a Jolt object-layer/broadphase-layer pair and a filter declaring which pairs collide.
- `StateRecorder` snapshot at every tick into a ring (90 ticks) for rollback and for validation diffing.

---

## 6. Domain Plans

### 6.1 Rigid dynamics and vehicles — P0

Chassis is one Jolt dynamic body. Suspension uses explicit ray/shape casts, not `VehicleConstraint`'s wheeled controller (its tyre model would fight the MF6.1 + carcass stack — decision D6 in `Docs/VehicleHmiEnginePlan.md`, kept).

Force path per 480 Hz substep, wheels in fixed order **FL → FR → RL → RR**:
1. `ShapeCastQuery` — strut axis → ground/obstacle.
2. `TyreCarcassLattice` — 160-node Hex8 Neo-Hookean (or XPBD belts) → radial deflection, contact patch.
3. `TyreSlipSolver` — MF6.1 longitudinal/lateral, combined-slip cosine weighting, relaxation length, load clamp 24 kN.
4. `TyreThermalSolver` — surface/bulk temperature, pressure rise, wear.
5. Accumulate `Vec3` force + torque, apply **once** per tick as an impulse to Jolt (substep forces are summed, never applied mid-substep) → determinism and no solver coupling noise.

### 6.2 Fracture — P2 (the workspace you asked to build first)

| Stage | Mechanism | File |
| :--- | :--- | :--- |
| Pre-fracture (cook) | seeded 3D Voronoi inside the source shell, deterministic cell order by index | `VoronoiCellPartitioner` |
| Convex decomposition | plane clipping of source faces by neighbouring bisectors, hull repair, watertight guarantee | `ConvexHullExtractor` |
| Damage model | bond-strain lattice over the Voronoi adjacency; each bond carries area, stiffness, accumulated strain | `BondStrainLattice` |
| Crack propagation | deterministic max-heap ordered by `(strain, bondIndex)`; no RNG | `CrackPropagationQueue` |
| Body swap | intact body = one Jolt `MutableCompoundShape` (or single hull); on break, remove and add N `ConvexHullShape` bodies **in cell-index order** | `PhysicsSpace/Internal/JoltShapeEncoder` |
| Interior look | `InteriorSdfBaker` writes 32³/64³/128³ R16F bricks for shadows, AO, dust emission | `InteriorSdfBaker` |
| Budget | max live chunks, merge settled chunks into a static compound, despawn beyond radius | `ChunkBudgetRegister` |
| Pattern reuse | hero fractures baked as `FracturePatternRepository` entries (`*.FractureSpace`) | `FracturePatternRepository` |

**Networking:** replicate the **event** only — `{bodyId:u16, tick:u32, patternId:u16, seed:u32, impactPoint:3×f32, energy:f32, flags:u8}` ≈ 32 B. Chunks then fall deterministically on every client; 10 Hz pose corrections for the chunk set.

**Editor (`Workspaces/Fracture/`):** the existing `Experimental/FractureEditor/` HTML studio is the visual reference (Voronoi, convex partition, SDF, debris, engineering bake). Tabs: **Pattern · Material · Bonds · Impact · Debris · Bake**. Tools: impact brush, impact line (bullet path), seed/re-roll, symmetry mirror, stress heat overlay, live chunk-count and mass-conservation readout.

### 6.3 Deformables — P1 (3D FEM + XPBD, tyres, crash cages)

Inherited from the proven VehicleHMI result: **~50 points deforming ~1k triangles**.

| Layer | Mechanism |
| :--- | :--- |
| Lattice | `ProxyLattice` — Hex8/Tet4 cells over a coarse cage; 48–200 points typical |
| Material | corotated / Neo-Hookean (J-clamped), or orthotropic membrane for tyres; `ν ≈ 0.44`, `E` per region |
| Integrator | velocity-free XPBD substeps (240 Hz), plus FEM nodal gather in **fixed cell-index order** |
| Constraint colouring | `ConstraintColorPartition` — greedy graph colouring at cook time (6–12 colours) → zero atomics on GPU |
| Embedding | `LatticeEmbeddingRegister` — for each dense vertex: 4 nearest lattice points + mean-value weights + local orthonormal basis, baked once |
| Presentation | `ProxyCageDeform.slang` — position/tangent/normal per vertex, O(1) |
| Coupling | proxy boundary points written to Jolt as kinematic contact shapes; body impulses read back and applied as clamped forces |
| Plasticity | yield threshold + permanent deformation limit for crash cages |

This is the **same machinery** reused by soft bodies (§6.5) and by the runtime cloth tier (§6.4) — one solver, three front-ends.

### 6.4 Cloth — P3, two tiers, one authoring pipeline

| Tier | Solver | Vertex budget | Use | Determinism | Replication |
| :--- | :--- | :---: | :--- | :---: | :--- |
| **Authoring** (dense) | `ClothSpace` XPBD small-steps (10 substeps @ 600 Hz) | 5k–60k | the Cloth Workspace: pattern, sew, drape, fit, bake | B | bake only |
| **Runtime proxy** | `DeformableSpace` proxy lattice + seam anchors | 48–200 pts | game garments, pre-baked drape | A | proxy (600 B) |
| **Runtime cheap** | Jolt soft body | 300–3k verts | flags, tarps, banners where determinism + contacts come free | A | state |

🔴 **The garment is authored in 2D and sewn in 3D — it is never a pre-made 3D surface draped over a body.** Full detail in [ClothAuthoringPlan.md](ClothAuthoringPlan.md).

### 6.5 Soft-body editor — P4

Mirrors the cloth workspace with a section/slice view instead of a pattern sheet.

- Pipeline: `PolygonNetwork` → `VolumeTetrahedralizer` (constrained Delaunay, boundary preserved) → tet lattice → corotated FEM + XPBD constraints → dense embedding → GPU skin.
- Inspector tabs: **Volume · Regions · Pressure · Attach · Collision · Bake**.
- Region stiffness painting (gel core / foam skin / rigid insert), self-contact with thickness, inflation pressure for inflatables, rigid attachment to Jolt bodies through `SoftbodyAttachmentCoupler`.
- Validation: volume error < 1 %, energy monotonic decay without applied force, penetration = 0 against the body SDF, no inverted tets after 10k ticks.

### 6.6 Particles — P5 (this is where Jolt stops)

Jolt has no particle solver, so:

| Register | Authority | Solver | Collision | Replication |
| :--- | :--- | :--- | :--- | :--- |
| `DebrisParticleRegister` | A | Jolt bodies, capped at 2,000 live | native Jolt contacts | capped state |
| `PhysicalParticleRegister` | A | our SoA integrator, 1 thread-fixed slice per chunk | batched `ShapeCastQuery` against Jolt, max 4 contacts/particle/tick | spawn token + seed |
| `VisualParticleRegister` | C | GPU compute, indirect draw | none | seed only |

Rules: force fields (`ParticleField`: curl noise, vortex, attractor) are sampled from the **counter-based RNG keyed by particle index**, never from time; spawns are emitted as `SpawnToken{count, seed, origin, inheritedVelocity}` so a client can rebuild the same visual burst; visual particles can never apply force to tier A/B state.

### 6.7 Fluid — P5 (fluid is a sibling domain, not "Jolt physics")

| Medium | Solver | Grid/particles | Tier | Coupling |
| :--- | :--- | :--- | :---: | :--- |
| Gas (smoke, fire, plume) | MAC Eulerian, MacCormack advection, vorticity confinement, combustion | 64³–128³ bricks, sparse | C | one-way (bodies push gas) + visual buoyancy |
| Liquid (water, fuel) | FLIP/APIC (primary), PBF (shallow) | 200k–1M particles | B | two-way at 30 Hz, clamped + quantised |

`FluidBoundaryExchange` contract: bodies → fluid (velocity slabs, per tick); fluid → bodies (**clamped** buoyancy/drag impulses at 30 Hz, quantised to 1e-3 N·s). Never per-substep, never unclamped — that is the classic fluid–rigid explosion.

---

## 7. Networked Physics (P6, optional per project)

```text
Server (authoritative, 60 Hz)                    Client (predictive, 60 Hz)
  CommandIntakeRegister ◄── input packets ──────  local input (tick-stamped)
  PhysicsExtension tick                            PredictionReconciler
  SnapshotEncoder ────────── 20 Hz snapshot ────►  apply + interpolate remote
  ProxyStateChallenge ◄────  hash mismatch ──────  SimulationHashRegister
```

| Policy (`ReplicationPolicy`) | Domains | Wire cost |
| :--- | :--- | :--- |
| `ServerOnly` | far rigid debris | 0 |
| `PredictiveLocal` | player chassis/character | input 12 B/tick |
| `DeterministicProxy` | deformables, cloth, fracture chunks | 600 B/garment @ 10 Hz + 32 B events |
| `EventOnly` | fracture, explosions, particle bursts | 32 B |
| `VisualOnly` | smoke, sparks | 0 (seed) |

- **Rollback:** `ReplayLedger` ring (90 ticks) of tier-A `StateRecorder` blobs + tier-B proxy copies; resim budget 8 ms for 3 ticks.
- **Reconciliation:** divergence check on the proxy hash; if `> 1 mm` RMS, server sends the full 600 B proxy state instead of the delta.
- **Bandwidth target:** ≤ 32 KB/s per client, ≤ 1.5 KB/tick steady state.
- Quantisation: position 22 bit/axis (1 mm over 4 km), rotation smallest-three (10+10+10+2), linear velocity 14 bit/axis, angular 12 bit/axis ≈ 18 B per rigid body.

---

## 8. Workspaces and UI (same language as the Frontier / UV HTML studios)

All four workspaces reuse the UV Workspace split-viewport shell and theme tokens:

| Workspace | Left pane | Right pane | Inspector tabs |
| :--- | :--- | :--- | :--- |
| **Physics** (`Workspaces/Physics/`) | Outliner (worlds, bodies, layers) | 3D simulation viewport + debug overlays | Bodies · Shapes · Joints · Layers · Ticks · Diagnostics |
| **Fracture** (`Workspaces/Fracture/`) | Outliner (patterns, chunks, bonds) | 3D viewport + stress/chunk overlay | Pattern · Material · Bonds · Impact · Debris · Bake |
| **Cloth** (`Workspaces/Cloth/`) | **2D pattern sheet** (pieces, seams, grainline, notches) | **3D drape on mannequin** | Pattern · Seam · Fabric · Fit · Collision · Simulate · Bake |
| **Softbody** (`Workspaces/Softbody/`) | Outliner (regions, tet volumes) | 3D viewport + cross-section slice | Volume · Regions · Pressure · Attach · Collision · Bake |

- Overlays (contacts, stress heat, chunk bounds, seam preview, penetration markers) are **GPU `.slang` rasterization**, never ImGui draw lists.
- Each domain ships an **HTML/JS reference studio** under `Experimental/` first (fast iteration, live in the browser), then a native C++ panel set that mirrors it pixel-for-pixel: `PhysicsOutlinerPanel`, `FractureInspectorPanel`, `ClothViewportHost`, `PatternSheetInteractor`, `SeamPaintInteractor`, `FabricPresetBrowser`, `SoftbodyInspectorPanel`.

---

## 9. Data Formats

| Document | Extension | Contents |
| :--- | :--- | :--- |
| Simulation world | `.SimulationSpace` | bodies, shapes, layers, world settings, tick config |
| Fracture pattern | `.FractureSpace` | seed, cells, hulls, bond lattice, SDF brick refs, material |
| Cloth garment | `.ClothSpace` | pattern pieces, seams, fabric assignments, avatar binding, bake cache, export manifest |
| Soft body | `.SoftbodySpace` | volume lattice, regions, pressure, attachments |
| Fabric preset | `.FabricSpace` | measurable fabric data (gsm, elongation@50N, bending length, drape coefficient, thickness) + derived solver compliance |
| Bake cache | inside `.ClothSpace` / Alembic export | 16-bit quantised positions @ 30 Hz |

Codecs live in `Engine/Codecs/` (`SimulationCodec`, `FractureCodec`, `ClothCodec`, `SoftbodyCodec`); DXF-AAMA/ASTM and 1:1 SVG pattern export live in `Engine/Interchange/PatternInterchange`.

---

## 10. Validation Gates (visual proof is mandatory)

| Exhibit | Proves | Acceptance |
| :--- | :--- | :--- |
| `JoltBridgeValidation` | layer/shape/body round-trip, contact manifold intake | 0 leaked `JPH` types in public headers; 100 % round-trip |
| `PhysicsDeterminismValidation` | tier A across builds, replay, rollback | 100k-tick hash identical × 3 platforms; rollback resim = 0 divergence |
| `DeformableProxyValidation` | 50-point cage → 1k-tri deformation | silhouette error < 1 mm; < 0.12 ms/vehicle |
| `ClothDrapeValidation` | drape quality, no rubber | stretch ≤ 105 % at 95th percentile; penetration = 0; avg dihedral < 5°; settle < 2 s |
| `ClothSewValidation` | seams close, multi-layer self-collision | seam gap < 0.5 mm; PT/EE contacts resolved; zero layer interpenetration |
| `SoftbodyValidation` | volume/energy/attachment | volume error < 1 %; no inverted tets |
| `FractureValidation` | mass conservation, determinism, budget | Σ chunk volume = source ± 0.5 %; identical shard set per seed |
| `FluidCouplingValidation` | no explosion, clamped exchange | impulse clamp proven; kinetic energy bounded |
| `ReplicationValidation` | snapshot/rollback/prediction | divergence healed ≤ 3 ticks; ≤ 32 KB/s |

Every exhibit writes `.png` proofs into `Exhibits/Images/<Domain>/` (CPU rasterizer fallback — no excuses about headless Linux).

---

## 11. Phases, Deliverables, Exit Criteria

| Phase | Deliverable | Exit criteria |
| :--- | :--- | :--- |
| **P0 · Baseline + Jolt** | CMake profile build, `Dependencies.json` (Jolt v5.6.0 pin), `JoltPhysics.cmake`, `Engine/` skeleton (MicroUtils, Constants, Threading), `PhysicsSpace/{PhysicsExtension,BodyRegister,BroadphaseSolver,CollisionDetection}`, `JoltWorldBridge`, `PhysicsThread` | `Exhibits/JoltBridgeValidation` builds on Windows/Linux/Android targets; 1,000 boxes settle deterministically; `.png` proof |
| **P1 · Determinism + deformables** | `FixedStepClock`, `SimulationHashRegister`, `ReplayLedger`, `DeformableSpace` proxy + 160-pt tyre + 50-pt crash cage, `ProxyCageDeform.slang` | hash stable over 100k ticks; 50 pts deform 1k tris at < 0.12 ms; `.png` proof vs reference silhouette |
| **P2 · Fracture Workspace** | `FractureSpace` (Voronoi, bonds, crack queue, hulls, SDF interior, budget), `Workspaces/Fracture/`, `.FractureSpace` codec, chunk replication | `Exhibits/FractureValidation`; interactive workspace; mass conservation ±0.5 %; same seed → same shards |
| **P3 · Cloth Workspace** | `ClothSpace` XPBD, pattern/seam authoring, mannequin collision, fabric library, bake + DXF/SVG export, `Workspaces/Cloth/` | `ClothDrapeValidation` + `ClothSewValidation`; 20k-tri garment at ≥ 45 fps authoring; zero penetration |
| **P4 · Softbody Workspace** | `SoftbodySpace`, tetrahedralisation, region painting, pressure | `SoftbodyValidation` |
| **P5 · Particles + fluid** | registers, GPU compute, gas/liquid, `FluidBoundaryExchange` | `FluidCouplingValidation`, `ParticleValidation` |
| **P6 · Networking** | `NetworkingSpace`, snapshots, rollback, `ReplicationPolicy` | `ReplicationValidation` at ≤ 32 KB/s |

**Recommended execution order:** **P0 → P1 → P2** (P2 is *your* stated goal and depends only on P0/P1) **→ P3 → P4 → P5 → P6**. P3 can be pulled ahead of P2 at any time because it shares only the SDF/collision layer, but P2 is cheaper to finish and gives you a shipping workspace first.

---

## 12. Risks and Mitigations

| Risk | Impact | Mitigation written into this plan |
| :--- | :--- | :--- |
| Cross-platform bit-exactness drifts silently | replay desync, "impossible" bugs | CI hash gate from P0; `DeterminismTier` declared per domain; no transcendentals on tier A/B |
| GPU cloth looks rubbery (the previous attempt) | wasted weeks, no authoring value | XPBD **small steps** + strain limiting + shear/bend constraints + panel-grid topology + SDF collision — see `ClothAuthoringPlan.md` §3 |
| Dense cloth in rollback/network explodes bandwidth | unusable multiplayer | dual representation: proxy authoritative, dense cosmetic |
| Fracture chunk count explodes | frame time collapse | `ChunkBudgetRegister`, settled-chunk merging, despawn radius, bake for hero shots |
| Fluid ⇄ rigid feedback loop | simulation explosion | 30 Hz clamped quantised exchange, one-way by default |
| Jolt header leaks everywhere | untestable, slow builds, no swap path | `Internal/` isolation enforced by include-lint in CI |
| Second engine divergence from `Frontier` | duplicated work | P0 imports governance + folder conventions from `SultanAladin/Frontier`; algorithms ported, never blind-copied |

---

## 13. Decision Checkpoint (pick one before P0)

| # | Decision | Options | Recommendation |
| :--- | :--- | :--- | :--- |
| D1 | **What gets built next** | **A)** `Experimental/ClothStudio/` — browser XPBD cloth demo with live drape, substep/hash readout (visible in minutes, proves the algorithm) · **B)** Native P0 bootstrap (CMake + Jolt + `JoltBridgeValidation` + `.png` proofs) · **C)** Both, A first as the visual contract then B | 🏆 **C** — the browser studio de-risks the hardest visual problem (drape quality) before any C++ is written, and doubles as the UI specification |
| D2 | **Authoring solver** | XPBD small-steps / implicit Newton-FEM / AVBD | 🏆 XPBD small-steps (+ optional AVBD bake) |
| D3 | **Tyre stack** | Jolt `VehicleConstraint` wheels / custom MF6.1 + carcass | 🏆 custom (matches proven VehicleHMI result) |
| D4 | **GPU authority** | GPU-authoritative deformables / CPU proxy authority | 🏆 CPU proxy (only option that is rollback-able and replicable today) |
| D5 | **Fluid coupling** | per-substep two-way / 30 Hz clamped exchange | 🏆 30 Hz clamped |
| D6 | **First shipping workspace** | Fracture / Cloth | 🏆 Fracture (stated goal, P0/P1 only), Cloth immediately after |

# VehicleHMI Demo → Frontier Game Engine — Mirror Port Plan

🧩 Full conversion plan: the WebGPU VehicleHMI demo (ReSTIR, Jolt, tyre continuum, cabin HMI) becomes a native
C++ / Vulkan / Slang engine project, opened through the Project Browser. First deliverable is the Log Stream panel.

Status: ✔️ approved by owner 2026-10-22 — recommendations D1–D5 and D7 stand (D7 revisitable on request).
Phase 1 (Log Stream C++ port) landed the same day: portable engine + panel + Project Zero, validated
headlessly; the windowed SDK build is the remaining half (§16).
D6 is deferred to the owner's full physics reference; D8 approved the same day, with coloured user
categories + a user logging API folded into §11. 🔴 Phase 7 is gated on that reference (§9).

## TL;DR

- The demo is already an editor (Outliner · Viewport · Inspector · status bar) — the port keeps that exact shape.
- 138 JS modules (1.72 MB excl. three.js), 80 WGSL shaders (436 KB), 13 CSS files map to C++ divisions, Slang
  render units ordered by a dependency resolver, and a reusable native UI kit seeded from Texture Workspace.
- Physics is the lowest-risk port: the demo already runs Jolt (WASM bindings mirror the native C++ API).
- 🔴 Three request words are blacklisted by `SKILL-Naming.md` (`Graph`, `Frame`, `Entity`). Compliant
  replacements are constructed in §2 and need owner approval before any C++ is named.
- This turn: demo `index.html` copied byte-identical, Log Stream designed in HTML from the 3 named images,
  project scaffold created. No C++ until the HTML and this plan are approved.

## 1. Sources inspected

| Source                                                        | Revision    | Read in full                                        | Inventory only (Phase 0 reads)                     |
|---------------------------------------------------------------|-------------|-----------------------------------------------------|----------------------------------------------------|
| SultanAladin/Frontier branch `Frontier` `CLAUDE.md` (moved from `arena/db2f9953-frontier`) | `676eceb…c177821…9ec758d` | ✔️ + all 4 SKILL docs                               | —                                                  |
| `SourceImages/General UI/` (same branch)                      | `676eceb…`  | ✔️ the 3 named images                               | other 7 images: names only                         |
| Texture Workspace (same branch)                               | `676eceb…`  | static DOM of 3 HTML prototypes                     | prototype JS internals; C++ `TexturePaintWorkspacePanel` |
| `Engine/Diagnostics/WorkspaceBuildConfiguration.h`            | `676eceb…`  | ✔️ (macro precedent for §5)                         | —                                                  |
| SultanAladin/Frontier `arena/e9bd7db5-frontier` VehicleHmiPrototype | `6afb6f0…` | ✔️ `index.html`, `PreviewStartup.js`, `InspectorCategories.js`, `HudLayout.js`, full CSS selector list | remaining 134 JS + 80 WGSL: name + byte size only  |
| `Exhibits/LogStreamPanel/LogStreamPanel.html` (same branch)   | `6afb6f0…`  | identified: screenshot exhibit of a prior native composition | not used as design — the 3 images govern            |
| This checkout `Frontier/Projects/Project-Networking/`         | `b26d66d`   | ✔️ `.frontier` format, `Upstream.json`, `Reference/README.md` | —                                       |

📝 Nothing outside these sources was searched. Unread internals are Phase 0 work (§14), never guessed here.

## 2. 🔴 Naming-rule conflicts in the request vocabulary

`CLAUDE.md` requires rule conflicts to be reported before affected work. Replacements below are constructed with
the `SKILL-Naming.md` §0 procedure (mechanism sentence → verb + substrate noun → 6-gate test), not recalled words.

| Request term        | Rule hit                              | Mechanism (one sentence)                                                        | Compliant name(s)                                                        | Status          |
|---------------------|---------------------------------------|---------------------------------------------------------------------------------|--------------------------------------------------------------------------|-----------------|
| render graph        | `Graph` banned (§1)                   | Units declare target reads/writes; resolve the one valid order + barriers.     | `SubmissionOrder`, `RasterizationEntry`, `TargetAdjacency`               | 🚩 D1 approval   |
| frame graph         | `Frame`, `Graph` banned               | Targets live first-write → last-read; disjoint lifetimes share one allocation.  | `TransientTargetAtlas`, `TargetLifetimeSpan`                             | 🚩 D1 approval   |
| entity              | `Entity` banned (2026-07-25)          | Approved replacement already exists.                                            | `RecordEntry`, `RecordStore`, `RecordToken`, `RecordClassification`     | ✔️ settled       |
| outliner            | `Hierarchy` banned                    | Owning tree of scene records.                                                   | structure `SceneDirectory`; panel `SceneDirectoryPanel`; UI tab text stays "Outliner" (1:1) | 🚩 D2 approval |
| render pass         | `Pass` banned (2026-07-26)            | Choose by mechanism per skill table.                                            | `…Rasterization` / `…Submission` / `…Sequence`                           | ✔️ settled       |
| editor shell        | `shell` banned                        | Approved replacement exists.                                                    | `WorkspaceHost`                                                          | ✔️ settled       |
| geometry hierarchy  | `Hierarchy` banned (demo file name)    | Clusters nested in levels; each level bounds the next level's screen error.    | `ClusterNesting` (+ runtime)                                             | 🚩 owner rename  |
| lighting system     | `system` banned as noun (demo file)    | Coordinates clustered lights inside rendering.                                  | fold into `RenderDivision`; GPU side stays `ClusteredLights`             | 🚩 owner rename  |
| (coordinators)      | `Manager/Service/Core` banned         | Approved coordinator suffix exists.                                             | `…Division` (`RenderDivision`, `PhysicsDivision`, `SceneDivision`)       | ✔️ settled       |

💡 The demo is already close to compliant (`…Sequence`, `…Solver`, `…Depot`, `…Projection`, `TyreSlipSolver`).
Only the rows above need rulings; everything else ports with its existing name to stay 1:1.

## 3. Target shape

```text
Project Browser ──► ProjectVehicleHmiDemo.frontier (TOML) ──► Frontier host
                     (Content, OpeningScene, CodeImage)      GLFW window · Vulkan device
                                                                 │ loads project DLL
                                                                 │ (interchange 3, fingerprint checked)
                                                                 ▼
                                                        WorkspaceHost (editor)
                              ┌────────────────┬──────────────────────┬────────────────┐
                              │ SceneDirectory │ Viewport             │ Inspector      │
                              │ tab: "Outliner"│ SubmissionOrder over │ category tabs  │
                              │ RecordEntry ×N │ TransientTargetAtlas │ (1:1 pages)    │
                              ├────────────────┴──────────────────────┴────────────────┤
                              │ Log Stream panel (first dock) · status bar             │
                              └────────────────────────────────────────────────────────┘
   Divisions under the host: RenderDivision · PhysicsDivision (native Jolt) · SceneDivision
   UI: reusable native kit — first set lifted from Texture Workspace (§10.4), Log Stream is its first new consumer.
```

- Host owns platform window + Vulkan device (GLFW, no-OpenGL-context window, per `CLAUDE.md`).
- Project DLL owns the Proving Grounds scene, vehicle, cabin HMI — same split as Project-Networking.
- Slang is the shader language; TOML + toml++ is the only engine-authored serialization (per `CLAUDE.md`).

## 4. Repository layout

Created this turn, mirroring Project-Networking exactly:

| Path (this checkout)                                                                 | Contents                                                        | After merge into SultanAladin tree |
|--------------------------------------------------------------------------------------|------------------------------------------------------------------|------------------------------------|
| `Frontier/Projects/Project-VehicleHmiDemo/ProjectVehicleHmiDemo.frontier`            | project descriptor (TOML)                                        | unchanged                          |
| `Frontier/Projects/Project-VehicleHmiDemo/Upstream.json`                             | source branches + inspected revisions                            | unchanged                          |
| `Frontier/Projects/Project-VehicleHmiDemo/Reference/index.html`                      | demo HTML, byte-identical (blob `437d811…`)                      | `Experimental/` per folder rules   |
| `Frontier/Projects/Project-VehicleHmiDemo/Reference/LogStreamPanel.html`             | Log Stream design prototype (this turn)                          | `Experimental/`                    |
| `Docs/VehicleHmiEnginePlan.md`                                                       | this plan                                                        | `Documentation/Plans/Ongoing/`     |
| `VisualProof/VehicleHmi/` (future)                                                   | verification programs + captures                                 | `Exhibits/`                        |
| `…/Project-VehicleHmiDemo/Source/` (future)                                          | ported C++ (project layer)                                       | project layer; reusable parts → `Engine/`, editor panels → `UI/Editor/` |

## 5. Build configurations and `#ifdef` policy

Follows the existing precedent exactly (`FRONTIER_DEVELOPMENT_CONSOLE` + `static constexpr bool` policy struct in
`WorkspaceBuildConfiguration.h`). One header decides; no scattered ad-hoc macros.

| Capability                                                   | `FRONTIER_EDITOR` | `FRONTIER_DEVELOPMENT` | `FRONTIER_SHIPPING` |
|--------------------------------------------------------------|-------------------|------------------------|---------------------|
| SceneDirectory auto-registration in `RecordEntry` ctor/dtor  | ✔️ on              | ✔️ on                   | compiled out (empty inline, zero cost) |
| `RecordStore` token lookup                                   | ✔️ on              | ✔️ on                   | ✔️ on (runtime needs it) — 🚩 D3 |
| Log Stream panel + dev console                               | ✔️ on              | ✔️ on                   | off — file sink only |
| Verbose log statements                                       | ✔️ compiled        | ✔️ compiled             | stripped at the macro |
| Gizmos, cluster/reservoir/HiZ debug overlays                 | ✔️ on              | ✔️ on                   | compiled out        |
| HUD readout + telemetry                                      | ✔️ on              | ✔️ on                   | off                 |
| Slang hot reload, content watching                           | ✔️ on              | off                    | off                 |
| Vulkan validation layers (default)                           | ✔️ on              | on request             | off                 |

🚧 Sketch only — final formatting pass happens when the header is written for real:

```cpp
/*============================================================================================================================================
                                                        BUILDCONFIGURATION.H
============================================================================================================================================*/
// 🧩 Compile-time build-flavour policy: exactly one of Editor / Development / Shipping is active per binary.

#pragma once

#if !defined(FRONTIER_EDITOR) && !defined(FRONTIER_DEVELOPMENT) && !defined(FRONTIER_SHIPPING)
    #define FRONTIER_DEVELOPMENT 1
#endif

namespace Frontier
{
struct BuildConfiguration
{
    static constexpr bool EditorEnabled      = false;   // [-] - Set by the configuration block below
    static constexpr bool DevelopmentEnabled = false;   // [-] - Set by the configuration block below
    static constexpr bool ShippingEnabled    = false;   // [-] - Set by the configuration block below

    // 📝 Editor-only machinery (SceneDirectory registration, gizmos, consoles) keys off this one predicate.
    static constexpr bool EditorMachineryEnabled = EditorEnabled || DevelopmentEnabled;
};
}
```

## 6. Record registration — constructor registers, destructor unregisters

Every scene object derives from one base. Construction issues a token, inserts into the store, and — in
Editor/Development only — appears in the SceneDirectory (Outliner). Destruction reverses it. Nothing registers
by hand anywhere else; forgetting to register becomes impossible.

```cpp
class RecordEntry
{
public:
    explicit RecordEntry(RecordClassification Classification);
    virtual ~RecordEntry();

    RecordEntry(const RecordEntry&)            = delete;
    RecordEntry& operator=(const RecordEntry&) = delete;

    RecordToken        Token()          const { return IssuedToken; }        // [-] - Issued at construction
    RecordClassification Classification() const { return EntryClassification; } // [-] - Outliner icon + page

private:
    RecordToken          IssuedToken;          // [-] - From TokenIssuer; authenticated by TokenAuthentication
    RecordClassification EntryClassification;  // [-] - Fixed for the entry's whole lifetime
};

RecordEntry::RecordEntry(RecordClassification Classification)
    : IssuedToken(TokenIssuer::Issue())
    , EntryClassification(Classification)
{
    RecordStore::Insert(*this);
#if FRONTIER_EDITOR || FRONTIER_DEVELOPMENT
    SceneDirectory::Register(*this);   // 📝 Outliner visibility only; Shipping binaries contain no directory at all
#endif
}

RecordEntry::~RecordEntry()
{
#if FRONTIER_EDITOR || FRONTIER_DEVELOPMENT
    SceneDirectory::Unregister(*this);
#endif
    RecordStore::Remove(*this);
}
```

- ⚠️ Ordering: `SceneDivision` constructs the directory before any record can exist; static-initialisation
  order across the DLL boundary is a Phase 1 check, not an assumption.
- ⚠️ Threads: records are constructed on the main thread in editor builds. Jolt job threads never construct a
  `RecordEntry`; physics results land on existing entries.
- Nesting (Outliner indentation) is a directory-side link (`DepthTier` + up-link token), set after construction
  by the creating code — the base class never guesses a parent.

## 7. The graph family

"All those graphs" = eight distinct dependency/nesting structures in the demo. Each gets one owner and one job.

| # | Structure (request term)              | Compliant name                                  | Mechanism                                                                 | Demo source                                   |
|---|---------------------------------------|-------------------------------------------------|---------------------------------------------------------------------------|-----------------------------------------------|
| 1 | render graph                          | `SubmissionOrder`                               | Declared target reads/writes → adjacency → topological order → barriers   | `IlluminationHost.js` (88 KB) + per-module pipeline setup |
| 2 | frame graph                           | `TransientTargetAtlas`                          | Lifetime spans → aliasing of non-overlapping targets onto shared memory   | implicit in host (WebGPU hides it) — new on Vulkan |
| 3 | geometry cluster hierarchy            | `ClusterNesting` 🚩                              | Cluster DAG; GPU chooses cut level by screen-space error                  | `GeometryHierarchy.js` (68 KB) + runtime (39 KB) |
| 4 | scene hierarchy / outliner tree       | `SceneDirectory`                                | Owning tree of `RecordEntry` tokens; selection ↔ viewport picking         | `EditorShell.js`, `VehiclePicking.js`         |
| 5 | shader variant structure              | `ShaderInterchange` (existing name)             | Slang modules + permutation keys → pipeline variants                      | `ShaderInterchange.js`                        |
| 6 | inspector category tree               | `InspectorCategories` (existing name)           | Tablist of panes per record classification; roving tabindex, arrows/Home/End | `InspectorCategories.js` (read in full)    |
| 7 | cabin screen flow                     | `ScreenSequence` / `ClusterSequence` (existing) | HMI screens + instrument cluster sequencing                               | `Source/Cabin/*` (73 KB)                      |
| 8 | particle workspace graph              | `ParticleWorkspace` (existing name)             | Emitter graph edited in its own workspace                                | `ParticleWorkspace.js`; demo embeds via `iframe` → becomes native panel |

### 7.1 `SubmissionOrder` + `TransientTargetAtlas` — how a frame is built

```text
Declare ──► Adjacency ──► Order ──► Lifetimes ──► Atlas ──► Barriers ──► Record
units list   per-target    topological,  first-write →   disjoint spans  derived per    units record into
reads/writes writer→reader stable by      last-read span  share memory    adjacency      graphics + compute
formats      edges         declare index  per target      (VkMemory)      (sync2)        queues, timeline joins
```

- Modules keep owning their pipelines (as in the demo: `VirtualizedGeometry.js` loads 21 WGSL, `IlluminationHost.js`
  15) but only *declare* units; the order is resolved centrally. 💡 Same ownership, one source of ordering truth.
- Two target classes: **transient** (atlas-aliased, lives inside one frame) and **imported** (swapchain, history).
  ReSTIR prev/current reservoirs are imported ping-pong pairs — aliasing them would corrupt temporal reuse. 🔴
- Vulkan wins over WebGPU without touching quality: dynamic rendering (no render-pass objects), `synchronization2`
  fine-grained barriers, descriptor indexing for material resolve, subgroup operations in reservoir candidate
  reduction, timeline semaphores overlapping compute (reservoirs, plume) with graphics.
- ⚠️ FP16 and reduced-precision formats are allowed only where a parity capture (§8.2) proves no visible change.

## 8. Renderer port — 80 WGSL → Slang on Vulkan

### 8.1 Shader inventory by group (complete, sums to 80)

| Group                  | Files | Shaders                                                                                                    | Unit suffix      |
|------------------------|-------|------------------------------------------------------------------------------------------------------------|------------------|
| Atmosphere / celestial | 8     | AtmosphereMedium, AtmosphereMultiple, AtmosphereScattering, AtmosphereTransmittance, CelestialSky, NightStars, NativeTwilight, SkyProjection | `…Submission` (LUT bakes), `…Rasterization` (sky) |
| Clouds                 | 7     | CloudField, CloudPresent, CloudRecords, CloudShadowProject, CloudShadowRecords, CloudShadowSample, CloudTrace | mixed            |
| Distance-field GI      | 5     | DistanceRecords, DistanceTransfer, DistanceTraversal, DistanceUnion, FragmentDistance                      | `…Submission`    |
| Surface-card GI        | 7     | CardCapture, CardDirect, CardFinal, CardIrradiance, CardProjection, CardRadiosity, CardSampling           | `…Submission`    |
| ReSTIR + denoise       | 9     | ReservoirRecords, ReservoirSpatial, ReservoirTemporal, DirectRadiance, ClusteredLights, DiscrepancySequence, VarianceTemporal, VarianceUpsample, VarianceWavelet | `…Submission` |
| Visibility / geometry  | 10    | VisibilityRaster, VirtualizedGeometryCull, GeometryClusterCull, ClusterDebugFallback, HiZDownsample, HiZPyramid, MaterialResolve, ForwardRaster, SurfaceRaster, SurfaceReflectance | `…Rasterization` + cull `…Submission` |
| Display / post         | 4     | DisplayAntialias, DisplayExposure, DisplayPresent, DisplayRecombination                                    | `…Rasterization` |
| Plume / fire / sparks  | 10    | PlumeAdvection, PlumeMarch, PlumePressure, PlumeRecords, PlumeVorticity, EffectRaster, EffectRecords, FireIllumination, SparkRaster, LightningRaster | mixed     |
| Precipitation / deposit| 6     | PrecipitationCompute, PrecipitationRaster, PrecipitationRecords, PrecipitationSurface, SnowfallRaster, SurfaceDepositRaster | mixed |
| Cabin / vehicle        | 3     | CabinOverlay, CabinVolume, VehicleVisual                                                                   | `…Rasterization` |
| Flare / fog            | 4     | LensFlare, FlareImage, FlareVisibility, FogMedium                                                          | mixed            |
| Traversal / misc       | 7     | RayTraversal (46 KB), ReflectionTraversal, ShadowRaster, InspectionMarch, FrameDescriptor, SceneRecords, PackingCodec | `…Submission` |

### 8.2 ReSTIR chain (the rendering centrepiece)

```text
DiscrepancySequence (low-discrepancy candidates)
      → DirectRadiance + ClusteredLights (initial candidates)
      → ReservoirTemporal (M-clamped temporal reuse, prev-frame import)
      → ReservoirSpatial (neighbour reuse, Jacobian-corrected)
      → MaterialResolve + SurfaceReflectance (shade the winning sample)
      → VarianceTemporal → VarianceWavelet → VarianceUpsample (SVGF-style denoise)
Indirect source feeding the candidates: Distance* field GI + Card* radiosity.
```

- Port order: chain first on a test scene, distance-field/card GI second, atmosphere/clouds third (§14).
- Parity oracle: the demo ships `Tools/RestirPathTracer.test.mjs` plus browser GPU proofs — fixed-seed captures
  from the demo become golden images; the Slang port must match within a stated per-pixel tolerance.
- Vulkan-only optimisation shortlist (quality-neutral by construction): subgroup ballot for candidate
  compaction, wave-cooperative reservoir streaming, async-compute overlap of VarianceWavelet with next frame's
  cluster cull, bindless material textures. Anything touching the estimator itself needs a golden-image proof.

### 8.3 Renderer decisions

| #   | Decision                          | Option A (rating)                                            | Option B (rating)                                            | Recommendation |
|-----|-----------------------------------|--------------------------------------------------------------|--------------------------------------------------------------|----------------|
| D4  | Ray traversal                     | Port software `RayTraversal` 1:1 — exact parity ✔️            | `VK_KHR_ray_query` — faster, traversal order differs 🚩       | A first; B later behind golden-image proof |
| D5  | Cluster culling                   | Compute cull + HiZ, as demo ✔️                                | Mesh/task shaders — new code path, parity risk 🚩             | A; revisit after Phase 4 measurements |

## 9. Physics — native Jolt, custom vehicle + tyre forces

🔴 **Owner gate (set 2026-10-22): do not port tyre or vehicle physics from the demo JavaScript.** When Phase 7
is reached, stop and notify the owner first — the owner will supply the full reference to copy the physics from.
Everything in this section is therefore an inventory of what the demo contains, not porting instructions; the
supplied reference decides D6 and supersedes any mapping below where they differ.

**Verified correction, 2026-10-27:** the paragraph below is historical and its missing-source conclusion is
superseded. The actual VehicleHMI branch at `6afb6f00b5f25c45f7cda92c7da65b64845768f3` contains
`TyreContinuumElasticity.js` and `TyreCarcassProjection.js` (160-particle XPBD), now retained with their tests in
`Experimental/VehicleHmiReference`. The original five tests pass; this is not a completed native continuum port.
The existing 84-particle C++ lattice is independent. See `Exhibits/VehiclePhysics/Continuation.md`.

⭐ **Gate superseded by owner direction, 2026-10-27:** the owner explicitly sequenced Phase 7 and directed the
three tyre models be ported from the HTML demo with Jolt integration and driving proofs. Audit result the port
was built on: the in-tree demo (`Experimental/SlateReference/{A,B}`) does **not** contain
`TyreContinuumElasticity.js`, `TyreCarcassProjection.js`, or any XPBD code (the inventory below describes a fuller
demo than the copy in this repo); its "3D FEM" is the GPU PB-FEM *deformation* in `VehicleVisual.wgsl`, and its
own text states there is no XPBD carcass in it. Ported 1:1 to `Frontier/Engine/Physics/`: `VehicleSpecification.h`,
`TyreSlipSolver` (MF6.1, bit-exact vs the JS), `VehicleSolver` (480 Hz, carcass spring in series with GRIT struts),
`TyreThermalSolver` (core; smoke emission excluded), `TyreDeformation.h` (the WGSL deformation + encoder). The
XPBD lattice (`TyreCarcassLattice`) is a **new** implementation calibrated to the same tyre, labelled as such
everywhere — if the owner's full physics reference (the missing continuum module) is supplied, it supersedes the
lattice. Jolt C++ 5.2.0 integrates loose bodies + a chassis proxy exactly as `JoltWorld.js` does with WASM (D6
option B honoured: the custom solvers own tyre forces). Proof: `Exhibits/VehiclePhysics/` (29/29 checks). Not yet
ported from the demo: `RigidBodyWorld.js` (incl. wheels riding debris), fracture, tyre marks, the sculpted course.

💡 The demo's `node_modules/jolt-physics` WASM bindings mirror Jolt's native C++ API almost name-for-name, so
`JoltWorld.js` ports mechanically. The custom solvers stay custom — Jolt integrates bodies; Frontier computes
powertrain and tyre forces and applies them at the contact patches.

| Demo module (bytes)                     | C++ destination                                   | Jolt provides                          | Stays custom (Frontier)                              |
|-----------------------------------------|---------------------------------------------------|----------------------------------------|------------------------------------------------------|
| `JoltWorld.js` (11,258)                 | `JoltWorld.cpp`                                   | PhysicsSystem, layers, job system      | world settings, step cadence                         |
| `RigidBodyWorld.js` (48,237)            | `RigidBodyWorld.cpp`                              | bodies, constraints, contacts          | body catalogue for course + fracture debris          |
| `VehicleSolver.js` (67,153)             | `VehicleSolver.cpp`                               | chassis rigid body; wheel query (D6)   | powertrain, steering geometry, aero, stability     |
| `VehicleSpecification.js` (13,283)      | `VehicleSpecification` (TOML-backed)              | —                                      | full vehicle data model                              |
| `TyreContinuumElasticity.js` (34,183)   | `TyreContinuumElasticity.cpp`                     | — (force source)                       | carcass node continuum → patch force/moment          |
| `TyreSlipSolver.js` (6,913)             | `TyreSlipSolver.cpp`                              | —                                      | slip ratio κ, slip angle α → combined-slip forces    |
| `TyreThermalSolver.js` (21,864)         | `TyreThermalSolver.cpp`                           | —                                      | temperature field → μ coupling, wear inputs          |
| `TyreCarcassProjection.js` (12,938)     | `TyreCarcassProjection.cpp`                       | —                                      | visual carcass deformation                           |
| `TyreMarkDepot.js` (13,223)             | `TyreMarkDepot.cpp` + `SurfaceDeposit*` shaders   | —                                      | mark deposition onto course surface                  |
| `FractureStructure/Sequence.js` (63 KB) | `FractureStructure.cpp`, `FractureSequence.cpp`   | debris bodies on fracture events       | fracture topology + trigger logic                    |
| `DriverInputIntegrator.js`, cameras     | same names                                        | —                                      | input conditioning, chase/orbit solvers              |

- Physics identifiers follow `SKILL-formatting.md` §4: real symbols (`κ`, `α`, `σ`, `μ`, `τ`), unit-tagged comments.
- Fixed-step with substeps (exact Hz is read from `JoltWorld.js` in Phase 6 — not assumed here).
- Validation ports: `JoltWorld.test.mjs`, `VolumetricTyrePhysics.test.mjs`, `VehicleComponents.test.mjs`,
  `VehicleStability.mjs`, `VehicleEffects.test.mjs` become C++ validation programs with the same assertions.

| #  | Decision              | Option A (rating)                                    | Option B (rating)                                     | Recommendation |
|----|-----------------------|------------------------------------------------------|-------------------------------------------------------|----------------|
| D6 | Wheel ↔ ground        | Jolt `VehicleConstraint` wheeled solver 🚩 (convenient, but its tyre model would fight the continuum solver) | Custom per-wheel ray/shape cast; continuum+slip solvers own forces; Jolt integrates ✔️ | B — the tyre stack is the demo's signature; Jolt must not override it |

## 10. UI — 1:1 port contract

🔴 Rule for the whole port: **no element is redesigned, merged, or dropped silently.** Phase 0 generates the
complete inventory mechanically from the demo sources; every row ends its life as *Ported → (C++ file)* or
*Approved removal → (owner decision + reason)*. Known removals in advance: `three.module.js` (third-party,
replaced by engine math), web workers (`CloudPreviewWorker`, `NativeFlareWorker` → job tasks), the particle
workspace `iframe` (→ native panel), `CsvDownload.js` (→ native file export).

### 10.1 Static shell — complete inventory of the copied `index.html`

| Demo element (verbatim)                                                                 | C++ counterpart (in `WorkspaceHost`)        |
|------------------------------------------------------------------------------------------|---------------------------------------------|
| `#shell` root; title "Frontier · Proving Grounds — distance-field GI, smoke + fire, fracture" | `WorkspaceHost` root; window title identical |
| `aside#outliner` + `.column-tab` "Outliner" + hide button `title="Hide the editor columns (P)"` + `#outliner-body` | `SceneDirectoryPanel`, same tab text, same `P` shortcut |
| `#viewport-column` + tab "Viewport" + `.scene-pill` "Proving Grounds"                    | viewport column, scene pill identical       |
| `#viewport-bar` (scene heading + controls, filled by `EditorControls.js`)                | viewport bar — per-control rows in Phase 0  |
| `canvas#viewport` + `canvas#capture.capture`                                             | Vulkan swapchain surface + capture target   |
| `#hud` + close `(H)` + `pre#hud-text` "Starting…"                                        | HUD readout overlay, same shortcut          |
| `#inset-frame` (hidden) + close `(J)` + `#inset-legend` GDF-slice legend text (verbatim) | distance-field slice inset, legend verbatim |
| `#overlay-dock` (hidden), `#overlay-notice`, `#event-notice`                             | overlay dock + both notice surfaces         |
| `#bring-up` — `h2` "Frontier · Proving Grounds", caption "Starting", `.bring-up-bar`     | bring-up overlay with progress bar          |
| `#failure` (hidden) + `PreviewStartup.js` failure text pattern                           | startup-failure panel, same message pattern |
| `aside#inspector` + tab "Inspector" + hide `(P)` + `#inspector-top` + `#inspector-content` | inspector column identical                  |
| `footer#status` (`.status-item` label+value pairs, `.status-fill`)                       | status bar identical                        |
| 13 stylesheets + DM Sans 300/400/500 (OFL)                                               | `Appearance.toml` tokens + `EngineContent/FontArchives/DM Sans/` (licence beside fonts, per `CLAUDE.md`) |

### 10.2 Reusable controls from the demo stylesheet vocabulary

Extracted from all 200+ selectors of `FrontierEditor.css` + the 12 panel stylesheets. Each becomes one kit control.

| Kit control              | Demo classes / source                                                        | First consumer        |
|--------------------------|------------------------------------------------------------------------------|-----------------------|
| ActionButton             | `.action-button` (+`.primary`, `.danger`), `.action-row`                     | inspector pages       |
| IconButton               | `.icon-button` (+`.active`, `.particle-launch`)                              | Log Stream header ✔️   |
| LinkButton, ResetButton  | `.link-button`, `.reset-button`                                              | inspector pages       |
| TreeRow                  | `.tree-row` (+`.selected`, `.nested`, `.is-off`), `.object-button`, `.row-name`, `.row-meta`, `.visibility`, `.row-state.live/.warm` | SceneDirectoryPanel |
| Card                     | `.card`, `.card-heading` (+`.card-index`, `.glyph`), `.card-subtitle`, `.cards` | Moon/cloud pages    |
| CountTile                | `.count-tiles`, `.count-tile` (`.count-icon`, `.count-value`, `.count-caption`) | inspector headers    |
| Metric / StatReadout     | `.metric`, `.metric-row`, `.stats` (`.two`, `.four`), `.readout`             | Log Stream summary ✔️  |
| Segmented                | `.segmented` (buttons + `.active`)                                           | viewport bar          |
| Toggle, PropertySwitch   | `.toggle.on`, `.property-switch` (icon/name/state, `.is-off`), `.switch-row` | inspector pages       |
| Pills                    | `.live-pill`, `.small-pill` (`.live`,`.warm`), `.enabled-pill`, `.scene-pill` | Log Stream ✔️          |
| MaterialChip             | `.material-grid`, `.material-chip` (+`.active`, swatch `i`)                  | material pages        |
| ColourField              | `.colour-input`, `.colour-pair`, `.colour-code`                              | material/sky pages    |
| NumberField, TransformFields.number-field`, `.transform-fields`                                    | inspector pages       |
| SliderControl            | `SliderControl.css`, `SliderControl.js`, `ObservatorySlider.js`, `.range-labels` | everywhere        |
| SelectField              | `.select-field`, `.telemetry-select`                                         | telemetry pages       |
| SearchField              | `.search` + input + placeholder                                              | Log Stream ✔️          |
| CategoryTabs             | `InspectorCategories.js` — tablist, roving tabindex, ←/→/Home/End (behaviour read in full) | every inspector page |
| DrillRow / DrillHud      | `.drill-row` (title/best/note), `#drill-hud` (phases: armed/finished)        | training drills       |
| OverlayDock              | `#overlay-dock` buttons, `.overlay-close`                                    | viewport overlays     |
| StatusBar                | `#status .status-item` (`span` label + `strong` value), `.footer-dot`        | workspace footer      |
| Brand / headings         | `.brand` (symbol, dot, version), `.eyebrow`, `.section-label`, `.group-label`, `.scene-title` | workspace chrome |
| BringUpBar               | `.bring-up-bar`, `.bring-up-caption`, `.bar-fill`                            | startup               |
| ColumnChrome             | `.column`, `.column-tabs`, `.column-tab`, `.tab-fill`, resize via `EditorShell.js` | all three columns |
| Scrollbar styling        | webkit scrollbar rules on `.inspector-content`, `.outliner-body`             | all scroll surfaces   |

### 10.3 Inspector page families (each ports with all of its controls — lists generated in Phase 0)

| Family            | Demo sources                                                                 | Notes                                     |
|-------------------|------------------------------------------------------------------------------|-------------------------------------------|
| Atmosphere        | `AtmosphereInspector.css`, `AtmosphereInspectorDashboard.js`, `AtmosphereColumnInstrument.js` | column instrument readouts        |
| Clouds            | `CloudInspector.css` (19 KB), `CloudInspectorDashboard.js`, `CloudLayerCard.js`, `NativeCloudPanel.js` | layer cards + preview worker → job |
| Sun               | `SunInspector.css`, `SunInspectorDashboard.js`                                |                                           |
| Moon              | `MoonCards.css`, `MoonInspectorInfographics.js`, `NativeMoonPreview.js`       | card infographics                         |
| Stars             | `StarsInspectorInfographics.js`, `StarCatalogue.js`, `CelestialInspectorUI.js` | catalogue = `BrightStars.bin` asset      |
| Fog               | `FogDashboard.css`, `FogInspectorDashboard.js`, `NativeLocalFogPanel.js`      | local + global fog                        |
| Wind              | `WindInstruments.css`, `NativeWindPanel.js`                                   | instrument dials                          |
| Lens optics       | `LensOptics.css`, `NativeFlarePanel.js`, `NativeLensFlareKernel.js`           | flare kernel → compute unit               |
| Lightning         | `NativeLightningPanel.js`, `LightningStorm.js`, `ThunderAudio.js`             | ⚠️ audio path — engine audio scope is Phase 8 |
| Vehicle Systems   | `VehicleSystems.css`, `VehicleSystemsInspector.js` + upstream `Docs/VEHICLE_SYSTEMS_INSPECTOR.md` | the HMI centrepiece         |
| Cabin HMI         | `Cabin/AppSpecification.js`, `Cabin/ClusterSequence.js`, `Cabin/ScreenSequence.js` (47 KB), `Cabin/IconDepot.js`, `SlateCabinLayout/Fittings.js` | ⚠️ "Slate" relationship = §16 risk |
| Environment root  | `NativeEnvironment.css` (20 KB), `NativeEnvironmentControls/Panels.js`, `EnvironmentInspector.js`, persistence (TOML) | shared control factory `El`/`Button` lives here |
| Diagnostics       | `DiagnosticsInspector.js`, `HardwareMetrics.js`, `SessionTelemetry.js`        | closest relative of the Log Stream        |
| Geometry debug    | `HierarchyDebugOverlay.js` (25 KB), `ClusterDebugOverlay.js`                  | editor-only (`#ifdef`, §5)                |
| Particle workspace| `ParticleWorkspace.js` (13 KB) + `.particle-workspace-frame > iframe`         | iframe → native docked workspace          |
| Materials         | `MaterialSpecification.js` (16 KB), material chips/grid                       | feeds Texture Workspace archives          |

📝 `InspectorPages.js` alone is 77 KB — its per-page control lists (labels, ranges, defaults) are extracted by a
parser in Phase 0 and checked in as the binding contract table. This plan deliberately does not hand-list
hundreds of sliders from unread code.

### 10.4 First reusable set — from Texture Workspace

Static DOM of `TexturePaintUI.html`, `TexturePaintLayerStack.html`, `TexturePaintInspector.html` (db2f9953):

| Texture Workspace piece (verbatim ids/classes)                                            | Kit control            | Reused by                          |
|-------------------------------------------------------------------------------------------|------------------------|------------------------------------|
| `pane-head` / `pane-body` / `pane-foot` (×4 panes)                                        | Panel                  | Log Stream ✔️, every dock           |
| `swipe-view` + `swipe-track` + `swipe-pane`, `FaceSwipe`, `Slide`                         | SwipeView              | inspector faces                    |
| `face-tabs`, `pc-seg`                                                                     | FaceTabs (Segmented)   | inspector + viewport               |
| `Rail` + `rail-head` + `HeadCount` + `AddHost` + `#Filter` + `stack-body` + `stack-sled` + `StackFoot` + `Readout` | StackRail (count, add, filter, sled rows, foot readout) | SceneDirectory, layer stacks |
| `search-wrap` + `filterInput`                                                             | SearchField            | Log Stream ✔️                       |
| `Ctx`, `summon-veil` + `summon-menu` + `menu-track`                                       | ContextMenu            | Log Stream dropdowns ✔️             |
| `advanceHead` / `returnHead` + `stepper` + `step`                                         | Stepper                | wizards, drill flow                |
| `inspectIcon` + `inspectName` + `inspectSub` (`inspectIdentity`)                          | IdentityHeader         | inspector tops                     |
| `summonCarousel` + `inspectTrack`                                                         | Carousel               | material/sky pickers               |
| `summonUndo` / `summonRedo`, `hdr-btn`                                                    | HeaderButtons          | workspace chrome                   |
| `host-bar` (`hb-mark`, `hb-name`, `hb-path`, `hb-spacer`) in `workspace-host`              | HostBar                | WorkspaceHost top                  |
| `layerTally`, `Well`, `Legend` + `Key`, `viewport-weave/vignette/note/foot`               | Tally, Well, Legend    | texture + viewport surfaces        |

💡 Order of construction: Panel + IconButton + SearchField + ContextMenu + Metric ship first because the Log
Stream needs exactly those; StackRail follows for the SceneDirectory; SwipeView/Carousel arrive with the
inspector pages. One kit, built in consumption order — nothing speculative.

## 11. Log Stream — designed this turn, first panel of the engine

### 11.1 Anatomy → design source → C++ control

| Panel part                    | Motif source (image)                                   | C++ control (kit)      | Behaviour (as prototyped)                                             |
|-------------------------------|--------------------------------------------------------|------------------------|-----------------------------------------------------------------------|
| Glass panel, hairline edge    | all three — dark glass cards, 16–22 px radius          | Panel                  | docks bottom of WorkspaceHost; pop-out button in header               |
| Title + Live pill + pulse dot | img 1 "Category - Summary" chip; demo `.live-pill`     | LivePill               | Live ↔ Paused; session number; wall clock with dimmed ms              |
| Header icon buttons           | img 1 square buttons, active = filled mint             | IconButton             | pause / clear / export / pop-out / overflow menu                      |
| Summary metrics               | img 2 Online 12 / Offline 4 cards; img 3 dimmed decimals + delta badges | Metric + DeltaBadge | Entries · Warnings · Errors, per-minute delta badges                  |
| Entries/sec sparkline         | img 2 efficiency chart (dashed threshold, endpoint dot)| Sparkline              | 72 s window, peak readout                                             |
| Severity filter chips         | img 2 "24 Bus · 100 Taxi" count pills                  | FilterChip             | multi-toggle; All resets; live counts                                 |
| Channel dropdown              | img 2 "Bus 6023 ⌃" pill + glass menu                   | PillDropdown (checklist) | engine channels + user categories, per-category counts, All/None/Invert |
| Category colours              | owner addition: blue / purple / pink over the same dark token set | CategoryColour tokens | user categories carry a palette colour — tag tint + dots in menu; severity colours unchanged |
| User categories + creator     | owner addition: user-generated logs (e.g. a tree shedding leaves) | `LogCategory` registration (§11.2) | dropdown User categories section + name/colour creator; new categories stream immediately |
| Verbosity dropdown            | same pill pattern                                      | PillDropdown (radio)   | Fatal → Verbose ceiling                                               |
| Overflow menu                 | img 1/3 circular icon menus                            | PillDropdown (actions) | copy visible, save session, word wrap, timestamp mode, collapse repeats |
| Search field                  | img 1 + img 3 search circles                           | SearchField            | live filter, match highlight, shown-count                             |
| Follow pill                   | img 1 enabled state chip; demo `.enabled-pill`         | FollowPill             | autoscroll lock; breaks on manual scroll-up                           |
| Log rows                      | img 2 status glyphs (✓ green / ⚠ red); img 3 dimmed fraction numerals | LogRow    | severity glyph, time (+dimmed ms), channel tag, message, source file:line, ×N repeat badge; click expands detail (copy / only-this-channel / filter-to-text) |
| "+N new" pill                 | img 3 floating delta badges                            | NewEntriesPill         | appears when scrolled up; click → jump to latest + refollow           |
| Footer scrubber               | img 1 timeline strip (ticks, playhead, time readout)   | SessionScrubber        | per-second ticks coloured by worst severity; shown/total, ring size, dropped, session time |

Colour/typography tokens are CSS custom properties at the top of `LogStreamPanel.html` (`--mint:#2fe6a7`,
`--coral:#ff6d6d`, `--amber:#ffb454`, user-category palette `--blue:#63a8ff` / `--purple:#b18cff` /
`--pink:#ff7ad9`, hairlines at 7% white, DM Sans 300 for numerals). They become
`Appearance.toml` entries verbatim in Phase 1 — the HTML file is the token source of truth until then.

### 11.2 Logging substrate under the panel (C++ design)

| Piece              | Design                                                                                                   |
|--------------------|----------------------------------------------------------------------------------------------------------|
| `LogEntry`         | fixed-size slot: timestamp, channel (uint8), severity, verbosity, repeat count, message (≤256 B inline)  |
| `LogRingStore`     | 65,536 slots, allocated once; producers never allocate; oldest overwritten (panel shows this as "Ring 64k · Dropped N") |
| `LogChannel`       | Render · Illumination · Physics · Vehicle · Tyre · Environment · Geometry · Interface · Content — the demo's own domains |
| `LogCategory` (user) | runtime-registered by any code: name + palette colour (`LogColour`); persisted in project TOML; unknown categories render neutral |
| User emit API      | `RegisterLogCategory(...)` once, then `WriteLog(Category, Severity, …)` from any class — user records stream beside engine records under the same filters |
| Severity × verbosity | Info carries Display/Log/Verbose; filter semantics identical to the HTML (chips = severity set, dropdown = verbosity ceiling) |
| Sinks              | panel (Editor/Development), session file (all configs), debugger output (Development on Windows)        |
| Compile gates      | Verbose statements expand to nothing under `FRONTIER_SHIPPING`; panel + console absent under Shipping (§5) |
| Threading          | any thread may write (atomic slot claim); only the UI thread reads for display; repeat-collapsing is a read-side view |

🚧 User-side sketch — the owner's tree example, as gameplay code will write it:

```cpp
// Declared once by user code; the colour comes from the appearance palette.
static const LogCategory FoliageCategory = RegisterLogCategory("Foliage", LogColour::Pink);

// Any class can then write into the same stream the engine uses:
WriteLog(FoliageCategory, LogSeverity::Info, "Leaf cluster detached · %d leaves", LeafCount);
```

## 12. Project Browser flow

```text
 enumerate Frontier/Projects/*/*.frontier
      → parse TOML (ProjectName, ContentLocation, OpeningScene, CodeImage, CodeInterchangeNumber, InterfaceFingerprint)
      → project cards (design source: upstream SourceImages/Project Browser/)
      → Open: verify interchange number + fingerprint ──✗──► failure panel (demo #failure pattern)
      → load CodeImage DLL → WorkspaceHost opens → OpeningScene loads
      → Log Stream is docked and already streaming the bring-up sequence
```

- Mirrors Project-Networking's descriptor field-for-field (this turn's file included, §4).
- `OpeningScene = Content/Scenes/ProvingGrounds.gltf` names the exported scene; the demo builds its level
  procedurally (`LevelStructure.js`, `CourseStructure.js`), so the export step is Phase 3 work. ⚠️
- `InterfaceFingerprint` currently reuses the Project-Networking value (same host interface); host tooling
  re-issues it when the project is first built. Noted in `Reference/README.md`.

## 13. Phased roadmap

| Phase | Deliverable                                                                 | Exit evidence (retained under `VisualProof/VehicleHmi/`)            |
|-------|-----------------------------------------------------------------------------|---------------------------------------------------------------------|
| 0 ✔️  | Inventories + copied HTML + Log Stream HTML; mechanical extraction of every JS control/shader binding into contract tables — done 2026-10-22: `Frontier/Projects/Project-VehicleHmiDemo/Contract/` (473 files = upstream `git ls-tree` count, zero unlisted; 80 shaders family-assigned and JS-reachable; 138 UI labels as the 1:1 worklist) | contract tables checked in; zero unlisted demo files               |
| 1 🚧  | Host opens project from Project Browser; `BuildConfiguration`; Log Stream native (kit: Panel, IconButton, LivePill, Metric, FilterChip, PillDropdown, SearchField, LogRow) over `LogRingStore` — portable engine, panel view-model, host flow and Project Zero landed + validated (`Exhibits/LogStream/`, 69 checks × Development/Editor/Shipping); native drawing is the approved prototype's exact 1:1 CSS geometry (DM Sans via the upstream `TypefaceRegistry`; dropdowns = reusable `DropdownControlSurface`), rendered headlessly in the real workspace; OS window awaits the SDK build | Windows capture beside the approved HTML, 1:1 checklist signed off |
| 2 🚧  | Rest of first kit from Texture Workspace (StackRail, SwipeView, IdentityHeader…); `RecordEntry` + `SceneDirectory` + inspector with real category tabs — **portable core + kit + native Outliner landed 2026-10-22**: `Engine/Scene` (token/store/entry/directory, visibility), `SceneDirectoryPanel` view-model (rows, fold, text+classification filters, spawn requests, inspector summary), `Engine/Interface/StackRailControls` (StackRail head/foot, TreeRow, IdentityHeader) and the native `SceneDirectoryPanelPresentation`, wired into `FrontierMain` stacked over the Log Stream and CPU-captured in the real workspace; SwipeView/Carousel still deferred to inspector pages; OS-window proof remains the SDK gate | Outliner fills from constructors ✔️; checks pass ✔️ (`Exhibits/SceneDirectory`: 45 Dev/Editor, 17 Shipping, Shipping binary has zero `SceneDirectory` symbols); native captures ✔️ (`Capture/workspace-outliner*.png`) |
| 3 🚧  | Viewport: device, swapchain, `SubmissionOrder` + `TransientTargetAtlas`, first units (surface, display); Proving Grounds scene export — **complete portable CPU reference frame now runs end-to-end** (`CompleteFrameUnits`: 38 units / 77 barriers / 54% transient atlas saving; `ReferenceRenderer`: cut/cull, G-buffer raster, RIS/reuse/shadows, upstream one-stratum GGX/Smith/Schlick metallic-roughness + atmospheric environment MIS, optional indirect, SVGF, atmosphere/cloud/fog, display encode; 18/18; `Exhibits/CompleteRenderer/`) — portable half complete: touch/usage vocabulary, `BarrierDerivation` (+retained dump), `ViewportUnits` + 2 Slang bodies, `Math`, `ProvingGrounds` spinning through the resolved order on CPU (`Exhibits/RenderInfrastructure` 63/63, incl. inspection views). Device/swapchain are host-owned upstream (`VulkanPresentation`); recording into its command buffer is the SDK-build remainder | spinning test geometry through the resolved order ✔️ (CPU) · barrier dump ✔️ retained · on-screen spin: SDK build |
| 4 🚧  | Virtualized geometry: `ClusterNesting`, cull, HiZ, visibility resolve — **portable core landed 2026-10-22**: `Engine/Rendering/ClusterNesting` (median-split nesting, enclosing bounds, sphere-tree monotone error, screen-space-error cut) + `VisibilityCull` (frustum, max-depth HiZ pyramid, conservative occlusion), proven on procedural corpora (`Exhibits/GeometryPipeline` 57/57: exactly-once cuts, monotone error/refinement, zero false-negative culls); Slang/GPU bodies + simplification DAG + demo corpus parity at the SDK phase | portable invariants ✔️ · demo `GeometryHierarchy` corpus images matched (SDK phase) |
| 5 🚧  | ReSTIR chain + SVGF; distance-field + card GI — **chain portable core landed 2026-10-22**: `DeterministicRandom` (PCG32), `DiscrepancySequence`, `ReservoirSampling` (WRS/RIS + M-clamped temporal + Jacobian-factored spatial combines), `VarianceFiltering` (SVGF moments + à-trous + joint upsample), `IlluminationUnits` (§8.2 chain declared: resolves in order, ping-pongs off-atlas, 32 barriers), proven against analytic oracles (`Exhibits/IlluminationChain` 55/55: perfect-target RIS exactly zero-variance; reuse unbiased within 5σ; 256-receiver scene RMSE 0.385 vs naive 3.86; SVGF RMSE 12× down, edge 97% retained). **Distance-field + card GI portable core also landed**: `Rendering/DistanceField` (analytic SDF union, sphere tracing, baked records, AO, hemisphere indirect) + `Rendering/SurfaceCards` (texel radiosity, regularised form factors, DF visibility), declared as the indirect prelude in `IlluminationUnits` and proven in `Exhibits/IndirectIllumination` (60/60); Slang/GPU bodies + golden images remain | portable oracles ✔️ · golden-image parity vs demo captures (SDK phase) |
| 6 🚧  | Environment: atmosphere, celestial, clouds, fog, flare, precipitation, lightning (visual) — **portable cores landed 2026-10-22**: `AtmosphereModel` (Rayleigh/Mie/ozone, transmittance LUT, single + approximated multiple scattering), `CelestialModel` (solar position, twilight, lunar phase, deterministic stars), `FogMedium` (analytic height-fog integral), `CloudField` (integer-hash density + Beer-Lambert march with sun shadow march), `WeatherEffects` (wrap-exact precipitation, midpoint-displacement lightning), `LensFlareModel`, `EnvironmentUnits` declaration — proven in `Exhibits/EnvironmentPipeline` (73/73; vertical transmittance matches analytic optical depths to 4 decimals, Benoni solar-noon identity, uniform-slab march exactly Beer-Lambert). Slang bodies + per-family demo captures remain SDK-phase | portable oracles ✔️ · per-family captures vs demo (SDK phase) |
| 7     | 🔴 GATED (§9): reference received 2026-10-22 — previous project (`Frontier-` @9440f4d0) `TyreSlipSolver.js` = Pacejka MF6.1 + `VehicleSolver.js`; port still sequenced after infrastructure by owner direction → then Jolt world + vehicle + full tyre stack + fracture | ported `*.test.mjs` assertions green in C++ validation              |
| 8 ⏸️  | **Deferred per owner (2026-10-22):** cabin HMI screens, plume/fire/sparks, particle workspace. Do not start until explicitly reactivated. | owner reactivates phase |
| 9     | Shipping configuration audit: every `#ifdef` row of §5 verified; 1:1 contract closed (every row Ported or Approved-removal) | shipping binary contains no editor machinery (symbol/size report) |

⚠️ Windows binaries are the deliverable per `CLAUDE.md`; GitHub Actions runs happen only on explicit owner
approval. This checkout's `networking-windows.yml` trigger set must be inspected before any push.

## 14. Decisions required from the owner

| #  | Decision                                   | Options (rating = risk)                                                                 | Recommendation |
|----|--------------------------------------------|-----------------------------------------------------------------------------------------|----------------|
| D1 | `SubmissionOrder` + `TransientTargetAtlas` names (§2, §7) | approve ✔️ / rename via §0 procedure 🚩        | approve        |
| D2 | "Outliner" tab text kept, code name `SceneDirectory`     | keep 1:1 label ✔️ / rename label too 🚩          | keep label     |
| D3 | `RecordStore` compiled into Shipping                     | keep (runtime lookup) ✔️ / strip entirely 🚩     | keep           |
| D4 | Ray traversal (§8.3)                                     | software 1:1 first ✔️ / ray-query now 🚩         | software first |
| D5 | Cluster culling (§8.3)                                   | compute 1:1 ✔️ / mesh-task now 🚩                | compute first  |
| D6 | Wheel-ground model (§9)                                  | custom casts + continuum forces ✔️ / Jolt vehicle constraint 🚩 | custom |
| D7 | UI implementation substrate                              | native retained kit (Texture Workspace lineage, ThorVG) ✔️ / ImGui (mentioned by this checkout's networking reference; SultanAladin `CLAUDE.md` sanctions it only for an isolated exhibit) 🔴 conflict between repo directions | native kit for editor panels; owner ruling needed |
| D8 | Log Stream HTML as designed this turn                    | approve → Phase 1 converts it first ✔️ / revise design 🚩 | approve        |
| D9 | Flake model for M6 glints (§11/MaterialAndFlakesReport)    | replace cell-search flakes with Glinty constant-time NDF ✔️ (owner-directed 2026-10-22, deferred until TextureWorkspace stabilises) / keep current flakes 🚩 | Glinty, deferred |

Owner verdict, 2026-10-22: plan approved. D1–D5 and D7 stand as recommended. D6 is deferred to the owner's
full physics reference at the Phase 7 gate (§9). D8 approved the same day after the owner viewed the
corrected preview (the first attempt had landed on the copied demo shell). Two owner additions are
folded into §11: coloured user categories and the user
logging API. The expandable-row detail already present was confirmed sufficient — no dropdown-card variant.

## 15. Risks and unverified items — stated, not hidden

| Risk / unknown                                                                 | Impact                                   | Resolution path                                             |
|--------------------------------------------------------------------------------|------------------------------------------|-------------------------------------------------------------|
| 134 JS + 80 WGSL inventoried by name/size, not line-read                       | per-control and per-binding detail could shift estimates | Phase 0 mechanical extraction before any porting  |
| three.js (1.27 MB) touchpoints unknown                                         | math/scene helpers may hide inside it    | Phase 0 import scan; replace with engine math row-by-row    |
| `SlateCabinLayout/Fittings.js` vs this repo's Slate upstream                   | cabin HMI may depend on the Slate SDK    | owner clarification + Phase 0 read                          |
| WebGPU → Vulkan floating-point differences in ReSTIR/SVGF                      | golden images may differ beyond tolerance | tolerance set from demo's own variance data; FP32 path kept |
| No browser binary in this sandbox                                              | HTML verified by parse + JS syntax + HTTP only; pixels unverified here | owner reviews the live preview (this turn) |
| Windows/MSVC + interface SDK build not yet exercised                           | portable C++ is green under g++ `-Wall -Wextra -Werror`; the windowed units need the upstream `Engine/Interface` + vendored ImGui/GLFW + Vulkan SDK, absent here | first SDK build compiles `LogStreamPanelPresentation.cpp` + the `FrontierMain` windowed path; until then they are inspection-verified only, never claimed built |
| `ProvingGrounds.gltf` does not exist yet (procedural level)                    | project opens to an empty scene until Phase 3 | exporter is Phase 3; descriptor already names the target |
| Existing native Log Stream on the e9bd7db5 branch (`Exhibits/LogStreamPanel/`)  | possible overlap with this design        | owner decides: supersede or merge before Phase 1            |

## 16. Delivered this turn

| Item                                                                 | Verification                                            | Status |
|----------------------------------------------------------------------|---------------------------------------------------------|--------|
| Complete portable render pipeline: `CompleteFrameUnits` + `ReferenceRenderer` | one 38-unit declaration resolves, 77 barriers derive, 54% atlas saving; stages execute through display encoding; 18/18 deterministic + closed-form BRDF checks; upstream GGX/Smith/Schlick one-stratum PBR is carried into RIS target and direct shading; capture at `Exhibits/CompleteRenderer/Capture/reference-frame.png` | 🟢 CPU reference · 🚧 Vulkan/OS window |
| Phase 8 visual add-ons (cabin HMI, plume/fire/sparks, particle workspace) | explicitly deferred by owner 2026-10-22; untouched | ⏸️ |

| `Reference/index.html` — demo HTML copied, nothing else               | `git hash-object` == upstream blob `437d811…`           | 🟢      |
| `Reference/LogStreamPanel.html` — Log Stream design (filter chips, 3 dropdowns, search, live stream) | HTML tag balance ✔️, 57 ids unique ✔️, all JS id refs resolve ✔️, `node --check` ✔️, HTTP 200 from preview server ✔️ | 🟢 (pixels: owner preview) |
| `ProjectVehicleHmiDemo.frontier` + `Upstream.json` + `Reference/README.md` | field-for-field mirror of Project-Networking precedent | 🟢      |
| This plan (`Docs/VehicleHmiEnginePlan.md`)                            | every factual claim traced to §1 sources; unread areas marked Phase 0 | 🟢      |
| Log Stream additions: coloured user categories + creator (owner-requested) | defect reproduced headlessly (ReferenceError from stale refs), repaired deterministically, runtime smoke + syntax + id checks green | 🟢 |
| Commits / pushes / Actions                                            | none made — per `CLAUDE.md`, remote operations need owner approval | ✔️      |
| Phase 1 portable engine (`Frontier/Engine/Diagnostics`, `Frontier/Engine/Interface`) | g++ `-std=c++17 -Wall -Wextra -Werror` clean; `Exhibits/LogStream` 69/69 in Development, Editor and Shipping profiles; contradictory profile probe refused by `static_assert` | 🟢 |
| Log Stream panel C++ (view-model + SDK-guarded native drawing)        | view-model at HTML parity (filters, ceilings, 6 s collapse, pause-view, follow, caps); native drawing written only against signatures re-read from upstream `676eceb` headers and inspection-checked symbol by symbol | 🟢 view-model · 🚧 native (SDK build pending) |
| Project Zero (reference project + `ProjectZero.bat` + v3 code image)  | headless session green (59 entries, declared Foliage/Gameplay/Network registered); interchange negotiation tested against recreated contract in `Exhibits/LogStream/Support/` | 🟢 |
| Host flow (`Frontier/Applications/Editor`)                            | browser listing + `--project`/`--headless`/`--help`/invalid all exercised; windowed browser→project-window path guarded behind `FRONTIER_WITH_INTERFACE_SDK` | 🟢 headless · 🚧 windowed |

| Material & flakes inspection (`Docs/MaterialAndFlakesReport.md`)      | previous project's `MaterialEvaluation.slang` read in full: 🏆 excellent (OpenPBR, CPU-proved); flakes: adopt Glinty (constant-time) as the M6 lobe over the 144-candidate cell search | 🟢 |
| `CLAUDE.md` (this repo, new)                                          | reference index: upstream rules + previous project @9440f4d0 (physics = Pacejka MF6.1 JS) + no-Python preference | 🟢 |
| Log Stream UI parity checklist (`Exhibits/LogStream/PARITY.md`)    | every HTML element ↔ C++ ↔ upstream kit; contract drift 676eceb→c177821 = empty diff; drawing gaps named (overflow menu, deltas, match count) | 🟢 |
| Infrastructure core: `SubmissionOrder`, `TransientTargetAtlas`, `SnapshotExchange`, `WorkerPool` | `Exhibits/RenderInfrastructure` 29/29 incl. 200-set property test and 200k-publish torn-snapshot proof; resolutions stream to the Render log channel | 🟢 |
| TextureWorkspace complaint report (`Docs/TextureWorkspaceComplaintReport.md` + `Docs/TextureWorkspaceEvidence/`) | owner screenshots + read-only audit @ `c177821`: crash = `VK_ERROR_OUT_OF_POOL_MEMORY` (pool 128 vs 131 materials — hypothesis, log to confirm); Pattern Studio confirmed non-SVG (ThorVG icons-only, ImDrawList canvas); document-only per owner, fixes gated | 📋 |
| Phase 0 contract tables (`Frontier/Projects/Project-VehicleHmiDemo/Contract/`) | extractor (Node) re-runnable; 473 files zero-unlisted vs `git ls-tree`; 80/80 shaders reachable; 81 entrypoints; 138 UI labels | 🟢 |
| Visual evidence page (`Docs/ProgressEvidence.html` + `Exhibits/RenderInfrastructure/Capture/`) | CPU capture of the spinning cube (48 frames, GIF + stills) from engine geometry/matrices; proofs embedded verbatim; served live for the owner | 🟢 |
| Phase 3 portable viewport core (`Engine/Foundation/Math.h`, `Engine/Rendering/{RenderTarget,BarrierDerivation,ViewportUnits}`, `Projects/Project-ProvingGrounds`, 2 Slang unit bodies) | `Exhibits/RenderInfrastructure` 63/63; units refactored to ordered `TargetTouch`es; viewport frame derives exactly 5 layout transitions (`BarrierDump-Viewport1920x1080.log` retained); Vulkan recording blocked on SDK build (host `VulkanPresentation` contract read @ `c177821`) | 🟢 portable · 🚧 on-screen |
| Owner corrections (2026-10-22): proofs under `Exhibits/`; root `CLAUDE.md` = upstream verbatim @ `c177821`; scenes in `Projects/` (`Project-ProvingGrounds`); closer cube capture | applied; `Docs/References.md` holds local notes; browser suite decodes all 4 projects | 🟢 |
| Debug / inspection views (`Engine/Rendering/InspectionUnits`, `Contract/DebugViews.md`): 7 demo views as Editor-only units; MatCap/CAD = `ViewportShadingMode` over upstream preview shader sets | 7 new checks in the 63/63; Shipping excludes them via `InspectionViewsEnabled()` | 🟢 portable · 🚧 shaders at SDK phase |
| Native Log Stream window (owner gate): compiles against genuine upstream headers, then **rendered in the real TextureWorkspace composition** (`ControlCentrePanel` + host-owned `PanelDockspace`; patched ImGui @ `3bae66c` + Patches A–G, thorvg, stb) — trapezoidal notch tab, Viewport | Log Stream 50/50, both dropdowns; **2026-10-22: `FrontierMain` rehosted onto this composition** (owner direction: no `Back · … · levels · catalogue` header) and compile-proved; upstream code moved to SultanAladin/Frontier branch `Frontier` @ `9ec758d` (contracts verified unchanged) | `Exhibits/LogStream/Capture/*` PNG masters + `CompileProof.md`; bugs fixed en route (combo/search widths, C++20 `u8string`, `ContentDirectory` collision → `ProjectContent`); **2026-10-22 restyle: the panel's drawing rewritten as the approved HTML's exact 1:1 visual copy** (delta badges + session clock + search marks/count + floating new-pill + dashed sparkline guide now drawn; menus + dock frame remain upstream's; `PARITY.md`); OS window (GLFW/Vulkan) still = Windows SDK build; owner approved the proof 2026-10-22 and directed the port to continue | 🟢 rendered headless · 🚧 OS window |
| Merge with upstream (owner direction, 2026-10-22): overlay committed (`57fad80`), then SultanAladin/Frontier branch `Frontier` @ `fd2a957` merged in (`6c2fc2c`) — that tip already contains merged PR #6 (preview stage/rig/LED fixes) | unrelated histories; 3 overlaps resolved (upstream README, union `.gitignore`, union EngineContent README); root `CLAUDE.md` verified verbatim; Log Stream 69/69 + Render Infrastructure 63/63 re-proved on the merged tree | 🟢 |
| Phase 2 portable core: record registration + Scene Directory (`Frontier/Engine/Scene/{RecordToken,RecordClassification,RecordStore,RecordEntry,SceneDirectory}`, `Engine/Interface/SceneDirectoryPanel`) | `Exhibits/SceneDirectory`: 37/37 Development, 37/37 Editor, 17/17 Shipping; Shipping binary has zero `SceneDirectory` symbols (`nm`); cycle refusals read back through the log stream; exhibit CMake `FRONTIER_ROOT` corrected to `../../Frontier` (post-merge the repo-root `Engine/` is upstream's) | 🟢 portable · 🚧 native Outliner drawing at SDK phase |
| Phase 2 kit + native Outliner: `StackRailControls` (StackRail head/foot, TreeRow, IdentityHeader), `SceneDirectoryPanelPresentation`, view-model extensions (visibility, fold, text filter, spawn), `SceneRecord`; wired into `FrontierMain` (stacked over Log Stream, columns 50/50 kept) | `Exhibits/SceneDirectory` 45/45 Dev + Editor, 17/17 Shipping; presentation TUs compile clean `-Werror` vs upstream headers; CPU workspace captures incl. spawn menu (`Capture/workspace-outliner*.png`, `workspace-studio-outliner.png`); demo shell parity recorded in `Exhibits/LogStream/PARITY.md` (Outliner addendum) | 🟢 portable+native-draw · ⏳ OS window at SDK gate |
| Phase 4 portable geometry: `Engine/Rendering/ClusterNesting` + `VisibilityCull` (frustum + HiZ) | `Exhibits/GeometryPipeline` 57/57 on procedural icosphere/heightfield (no demo corpus read); deterministic builds, exactly-once monotone cuts, conservative culling; sphere-tree error is the documented stand-in for DAG simplification error | 🟢 portable · 🚧 Slang/GPU + corpus parity at SDK phase |
| Porting findings ledger + checkout recovery (`Docs/PortingFindings.md`) | Owner-directed consolidation of every parity limit, product delta, bug found (ours/upstream) and process lesson from the HTML→C++ port, filed for later attention. Same turn: sandbox reset had restored a stale pre-merge overlay over a fresh clone at `b26d66d` — recovered loss-free from the pushed branch (blob-compare audit: overlay strictly older); one evidence-page update from the previous turn was found absent from its commit and regenerated | 🟢 filed |
| Phase 6 portable environment: `Rendering/{AtmosphereModel, CelestialModel, FogMedium, CloudField, WeatherEffects, LensFlareModel, EnvironmentUnits}` | `Exhibits/EnvironmentPipeline` 73/73: analytic optical-depth match (0.93588 vs 0.93591), LUT 0.28% mean error, phase normalisation by quadrature, noon/sunset/night sky behaviour from the march, solstice declinations ±23.44°, solar-noon identity 74.80°=74.80° (Benoni), fog vs Simpson 1e-4, uniform cloud march = Beer-Lambert 1e-9 (early-out transmittance truncation caught + fixed), precipitation wrap identity, lightning endpoint/seed identities, environment declaration resolves with LUTs off-atlas (21 barriers); `Capture/sky-strip.png` [noon|sunset|sunset+clouds] | 🟢 portable · 🚧 Slang/captures at SDK phase |
| Phase 5 portable indirect GI: `Rendering/{DistanceField, SurfaceCards}` + indirect prelude in `IlluminationUnits` (DistanceRecords/Traversal/Transfer, CardCapture/Direct/Radiosity/Final → `IndirectRadiance` → DirectRadiance) | `Exhibits/IndirectIllumination` 60/60: exact SDF/trace oracles (3.02263 closed-form match), irradiance π·L exact / finite-sphere 0.4%, baked-grid error 13.5→3.2 mm, form-factor reciprocity exact, texel radiosity vs independent patch direct solve ≤0.66%, power audit 83%→90.5% under refinement; caught+fixed a real energy-creation defect (raw centre-sampled kernel: 124% power accounted) via symmetric distance regularisation; `Capture/distance-room.png` one-bounce room render. Chain suite re-verified 55/55 over the merged declaration (45 barriers, 52% atlas saving) | 🟢 portable · 🚧 Slang/golden images at SDK phase |
| Phase 5 portable illumination chain: `Foundation/DeterministicRandom`, `Rendering/{DiscrepancySequence, ReservoirSampling, VarianceFiltering, IlluminationUnits}` | `Exhibits/IlluminationChain` 55/55 at fixed seeds: perfect-target RIS exactly zero-variance; rough-target RIS/temporal/spatial unbiased within 5σ (RIS variance 10× under naive); 256-receiver analytic scene end-to-end RMSE 0.385 vs 3.86 naive; SVGF RMSE 0.277→0.023 with 97% guided-edge retention (`Capture/svgf-strip.png`); chain declaration resolves §8.2 order, atlas saves 47% with ping-pongs excluded, 32 barriers. Two test-design bugs found+fixed on the way (proposal≡target degeneracy masking M-scaling; step oracle ignoring the radial bump) and one real declaration bug (ping-pong resting usage) caught by `BarrierDerivation`'s contract | 🟢 portable · 🚧 DF/card GI next · ⏳ Slang/golden images at SDK phase |

Phase 1 landing notes (facts the port surfaced):

- Profile macros: upstream uses `FRONTIER_DEVELOPMENT_PROFILE` / `FRONTIER_SHIPPING_PROFILE`;
  `BuildConfiguration.h` adds `FRONTIER_EDITOR_PROFILE`, normalises all three to one-hot values
  (default Development) and refuses contradictory combinations at compile time.
- The committed `InterfaceFingerprint = 0xb6725f24d0869173` literal exceeds TOML's signed 64-bit
  integers, so a conforming parser rejects every current `.frontier` file. The decoder quotes that
  one bare-hex value in memory before parsing (files on disk untouched; both spellings accepted).
- Project Browser sits at `Frontier/Projects/` (repository layout); windowed flow is two sequential
  `RunWindowsApplication` calls — the browser window closes, then the project window opens with the
  project name on the notch (`SurfaceLayer::Above`) and the Log Stream as its one docked panel.

Phase 0 is complete; Phase 3's portable half is complete. Remaining, in order
(owner-resequenced 2026-10-22: infrastructure first): Phase 1+3 SDK build (Windows capture
beside the approved HTML + spinning cube through the resolved order on screen) → Phase 2 kit
→ Phase 4 virtualized geometry on this core. The TextureWorkspace fix phase (complaint report above) runs
when the owner calls it; Glinty (D9) follows it. Phase 7 physics reference is in hand (Pacejka
MF6.1, §9); its port starts only when the sequence reaches it.

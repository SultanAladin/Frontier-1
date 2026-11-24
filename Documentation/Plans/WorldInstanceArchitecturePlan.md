# World Instance Architecture, `.Instance` Document Format & Dual Execution Loop Plan

> **Purpose:** Implementation blueprint for Frontier's zero-bloat world instance architecture, `.Instance` multi-format document codec, and unified Production / Editor tight execution loops.  
> **Applies To:** `Engine/World/`, `Engine/Codecs/InstanceCodec.{h,cpp}`, `Engine/Editor/`, `Engine/Lifecycle/`  
> **Companion Plans:** `Documentation/Plans/DocumentFormatsInstancePlan.md` · `Documentation/Plans/GameEngineBringupPlan.md` · `Documentation/Plans/ThreadingArchitecturePlan.md` · `Docs/FrontierEngineArchitectureMap.html`

---

## 1. Design Principles & Strict Vocabulary Guarantees

Every placed world participant — geometry surfaces, suns, moons, atmosphere media, cloud decks, luminaires, cameras, particle clouds, force field volumes, trigger zones, and acoustic sources — follows four strict rules:

1. **Identical Registration From Editor UI or C++ Code:** Constructing or destroying any concrete instance (`LunarInstance`, `SolarInstance`, `SurfaceInstance`, `ParticleInstance`, `ForceFieldInstance`, `TriggerInstance`, etc.) executes the **exact same Production registration** first, then (under `#if FRONTIER_PROFILE_EDITOR`) registers/unregisters Outliner, Inspector, and Viewport selection entries automatically.
2. **Zero `UObject` Bloat:** No virtual inheritance, no reflection garbage-collector headers, no per-instance heap allocations, and zero per-tick cost for static placements. Core identity is an **8-byte `InstanceToken`** backed by a **48-byte trivially-copyable `InstanceRecordEntry`**.
3. **Tight Category Loops (`Execute*Loop`):** Simulation work runs in explicit, linear Structure-of-Arrays (SoA) loops (`ExecuteLunarLoop`, `ExecuteSolarLoop`, `ExecuteParticleLoop`, `ExecuteForceFieldLoop`, `ExecuteTriggerLoop`, `ExecuteAcousticLoop`). Every loop accepts a parameter struct containing `bool ExecutionEnabled` and exits in **1 branch (~0.0001 ms)** when inactive or when `RequireRebuild == false`.
4. **Composable `.Instance` Document Format:** Built on Frontier's TomlWithBinarySection container (`SpaceContainerEncoder` / `SpaceContainerDecoder`), holding Editor directives (stripped on Shipping cook) alongside multi-format `[[SpaceSlots]]` referencing `.GeometrySpace`, `.MaterialSpace`, `.ParticleSpace`, `.SimulationSpace`, `.ForceField`, `.TriggerZone`, `.Photometry`, and `.Acoustic` documents.

---

## 2. Lean Memory Layout (`InstanceToken` & `InstanceRecordEntry`)

```cpp
// 8-byte generation-checked handle (zero GC overhead, immune to stale references)
struct InstanceToken
{
    uint32_t SlotIndex       = 0u; // [idx] - Dense slot index inside WorldInstanceRegister
    uint16_t Generation      = 0u; // [ver] - Incremented on reclaim to invalidate stale tokens
    uint16_t CategoryOrdinal = 0u; // [idx] - InstanceCategory enumerator ordinal

    [[nodiscard]] constexpr bool QueryValid() const noexcept { return Generation != 0u; }
};

// 48-byte non-virtual core record (fits inside 1 cache line, zero vtable, trivially copyable)
struct alignas(16) InstanceRecordEntry
{
    InstanceToken Token            {};          // [8 B]  - Self identity + category ordinal
    uint32_t      CategorySlot     = ~0u;       // [idx]  - Dense index inside category SoA register
    uint32_t      TransformSlot    = ~0u;       // [idx]  - Dense index inside WorldTransformStorage
    uint32_t      EnclosingIndex   = ~0u;       // [idx]  - Non-kinship enclosing slot index (~0u = root)
    uint32_t      FirstNestedIndex = ~0u;       // [idx]  - First nested slot index (~0u = none)
    uint32_t      NextLateralIndex = ~0u;       // [idx]  - Next lateral entry at same nesting depth (~0u = end)
    uint32_t      DirectiveMask    = 0u;        // [mask] - InstanceDirectiveMask bitmask (EnableVisibility, ...)
    uint64_t      DocumentHash64   = 0ull;      // [hash] - FNV-1a 64-bit hash of source .Instance document
    uint64_t      SpatialCellId    = 0ull;      // [idx]  - Broad spatial cell stamp for culling
};
```

### Canonical Category Register Matrix

| Concrete Instance Class | Category Enumerator | Contiguous SoA Register | Tight Execution Loop | Multi-Format Space Slots |
| :--- | :--- | :--- | :--- | :--- |
| **`SurfaceInstance`** | `SurfaceGeometry` | `SurfaceInstanceRegister` | `ExecuteTransformLoop` | `.GeometrySpace`, `.MaterialSpace`, `.UVSpace`, `.SimulationSpace` |
| **`LunarInstance`** | `LunarBody` | `LunarInstanceRegister` | `ExecuteLunarLoop` | `.ImageSpace` (Lunar surface sheet), ephemeris parameters |
| **`SolarInstance`** | `SolarBody` | `SolarInstanceRegister` | `ExecuteSolarLoop` | `.Photometry`, `.ColourSpace` |
| **`AtmosphereInstance`** | `AtmosphereMedium` | `AtmosphereInstanceRegister` | `ExecuteAtmosphereLoop` | `.VolumetricSpace`, `.ColourSpace` |
| **`CloudInstance`** | `CloudDeck` | `CloudInstanceRegister` | `ExecuteCloudLoop` | `.VolumetricSpace` |
| **`LuminaireInstance`** | `Luminaire` | `LuminaireInstanceRegister` | `ExecuteLuminaireLoop` | `.Photometry`, `.IesProfile`, `.ColourSpace` |
| **`ParticleInstance`** | `ParticleCloud` | `ParticleInstanceRegister` | `ExecuteParticleLoop` | `.ParticleSpace`, `.MaterialSpace`, `.TextureSpace` |
| **`ForceFieldInstance`** | `ForceFieldVolume` | `ForceFieldInstanceRegister` | `ExecuteForceFieldLoop` | `.ForceField`, `.SimulationSpace` |
| **`TriggerInstance`** | `TriggerZone` | `TriggerInstanceRegister` | `ExecuteTriggerLoop` | `.TriggerZone`, analytic or `.GeometrySpace` bounds |
| **`AcousticInstance`** | `AcousticSource` | `AcousticInstanceRegister` | `ExecuteAcousticLoop` | `.Acoustic`, `.AudioSpace` |
| **`CameraInstance`** | `CameraOptics` | `CameraInstanceRegister` | `ExecuteCameraLoop` | `.CameraOptics`, `.Render` |

---

## 3. Dual-Mode Constructor & Destructor (`#if FRONTIER_PROFILE_EDITOR` vs Production)

Every category instance class owns one `InstanceToken` in `WorldInstanceRegister` and executes two strictly layered paths:

```cpp
class LunarInstance
{
public:
    explicit LunarInstance(WorldInstanceRegister& Register, const LunarInstanceSpecification& Specification);
    ~LunarInstance() noexcept;

    LunarInstance(const LunarInstance&)            = delete;
    LunarInstance& operator=(const LunarInstance&) = delete;
    LunarInstance(LunarInstance&& Other) noexcept;
    LunarInstance& operator=(LunarInstance&& Other) noexcept;

    [[nodiscard]] InstanceToken QueryToken() const noexcept { return IssuedToken; }

private:
    void RegisterProduction(WorldInstanceRegister& Register, const LunarInstanceSpecification& Specification);
    void UnregisterProduction() noexcept;

#if FRONTIER_PROFILE_EDITOR
    void RegisterEditorSurfaces(const LunarInstanceSpecification& Specification);
    void UnregisterEditorSurfaces() noexcept;
#endif

    WorldInstanceRegister* OwnerRegister = nullptr; // [-] - Non-owning world instance register
    InstanceToken          IssuedToken   {};        // [-] - Generation-checked slot token
};
```

- **Production / Shipping (`FRONTIER_PROFILE_EDITOR == 0`):**
  - Constructor executes `RegisterProduction()` ONLY → O(1) slot acquisition, dense SoA append, sets `RequireRebuild = true`. Zero Editor/Outliner/ImGui headers compiled or linked.
  - Destructor executes `UnregisterProduction()` ONLY → O(1) swap-remove in the category SoA register, generation increment, slot return to free index list, sets `RequireRebuild = true`.
- **Editor (`FRONTIER_PROFILE_EDITOR == 1`):**
  - Constructor executes `RegisterProduction()` **first**, then `RegisterEditorSurfaces()` → invokes the bound `EditorSurfaceHooks` to register the `OutlinerRecordEntry` (title, canonical category tag, icon, enclosing folder, duplicate/removal policy) and Inspector binding.
  - Destructor executes `UnregisterEditorSurfaces()` **first**, then `UnregisterProduction()`.
  - Because `Engine/World/` talks to the Editor solely through a lightweight `#if FRONTIER_PROFILE_EDITOR` hook struct, `Engine/World/` has **zero link dependency** on ImGui or Editor UI panes!

---

## 4. Tight Main-Loop Execution (`Execute*Loop`)

Every category loop takes an explicit parameter struct with `ExecutionEnabled` and `RequireRebuild` gating:

```cpp
struct LunarLoopParameters
{
    bool                                       ExecutionEnabled = false; // [-] - Fast 1-branch skip when false
    double                                     Δt               = 0.0;   // [s] - Simulation step interval
    double                                     WorldTimeSeconds = 0.0;   // [s] - Elapsed world clock
    GeometricRaster::WorldSceneSpecification*  TargetScene      = nullptr; // [-] - Output scene specification
};

uint32_t ExecuteLunarLoop(WorldInstanceRegister& Register, const LunarLoopParameters& Parameters) noexcept;
```

### Production / Shipping Tight Loop vs Editor Loop

```text
============================================================================================
  PRODUCTION / SHIPPING TIGHT LOOP (FRONTIER_PROFILE_SHIPPING == 1)
============================================================================================
  1. Hardware Input Intake (GLFW / Gamepad / Raw Input)
  2. ExecuteWorldSimulationSequence(Register, Parameters):
       ├── ExecuteCameraLoop       (skips in 1 branch if !ExecutionEnabled)
       ├── ExecuteSolarLoop        (skips if !ExecutionEnabled || !RequireRebuild)
       ├── ExecuteLunarLoop        (skips if !ExecutionEnabled || !RequireRebuild)
       ├── ExecuteAtmosphereLoop   (skips if !ExecutionEnabled || !RequireRebuild)
       ├── ExecuteCloudLoop        (skips if !ExecutionEnabled || !RequireRebuild)
       ├── ExecuteLuminaireLoop    (skips if !ExecutionEnabled || !RequireRebuild)
       ├── ExecuteForceFieldLoop   (parallelizable over contiguous SoA register)
       ├── ExecuteTriggerLoop      (contiguous spatial overlap evaluation)
       ├── ExecuteParticleLoop     (async GPU compute enqueue — zero CPU per-particle loop)
       ├── ExecuteAcousticLoop     (spatial audio emitter synchronization)
       └── ExecuteTransformLoop    (collects TransformRequireRebuild spans for GPU transform)
  3. Phase 2 GPU World Render Sequence (2.5–3.5 ms median on GTX 1650 SUPER):
       ├── GPU Transform Compute (only RebuildIndexList spans)
       ├── Async Precompute (SkyLUT, Shadow Cascades, 3D GDF/Radiosity — RequireRebuild only)
       ├── HW Rasterized Reversed-Z VisBuffer (WorldPrimaryVisRaster.slang)
       ├── Lean Direct Compute + ContactPCSS (ComputeEntry)
       ├── Half-Res (¼ px) 3D GDF AO + 5-Cone GI (HalfResIndirectEntry)
       └── 2×2 Joint Bilateral Upsample + Fog + ACES Tone (DenoiseCompositeEntry)
  4. Direct Fullscreen Swapchain Presentation (Zero Editor UI, Zero Outliner, Zero Gizmo)

============================================================================================
  EDITOR LOOP (#if FRONTIER_PROFILE_EDITOR == 1 — WRAPS IDENTICAL CORE SEQUENCE)
============================================================================================
  1. Input Intake + Non-Blocking 3-Slot Gizmo Hover/Drag Pick
  2. Editor Pre-Synchronization:
       ├── Apply Outliner/Inspector edits -> set RequireRebuild on touched category registers
       └── Assign ExecutionEnabled masks (Edit Mode: RequireRebuild-only; Play Mode: full tick)
  3. ExecuteWorldSimulationSequence(Register, Parameters)   <-- IDENTICAL PRODUCTION LOOP
  4. Phase 2 GPU World Render Sequence                      <-- IDENTICAL GPU PIPELINE
  5. Async Editor Overlays (3-Slot Gizmo · Retained/GPU Inspectors · 50 Studio SVGs · ImGui)
```

---

## 5. The `.Instance` Document Format (`InstanceCodec.{h,cpp}`)

Uses Frontier's existing **TomlWithBinarySection** container (`SpaceContainerEncoder` / `SpaceContainerDecoder`) with canonical suffix **`.Instance`** (case-insensitive load for `.instance`, `.Instance`, and `.InstanceSpace`):

```toml
[Document]
CanonicalCategory = "SurfaceGeometry"
DocumentTitle     = "RallyWheelUnit"
StableIdentifier  = "rally_wheel_01"

[EditorDirectives] # Stripped automatically when StripEditorDirectives = true (Shipping cook)
CategoryTag       = "geom:procedural"
OutlinerIconPath  = "EngineContent/Icons/Editor/editor-mesh.svg"
DefaultFolder     = "Scene/Vehicles"
AuthorNotes       = "Multi-format wheel combining geometry, material, particles, force field & acoustics."

[Transform]
PositionMeters    = [0.0, 0.0, 0.34]
RotationDegrees   = [0.0, 0.0, 0.0]
ScaleScalar       = [1.0, 1.0, 1.0]

[ExecutionPolicy]
SimulationMode    = "RequireRebuildOnly" # Static | RequireRebuildOnly | FixedStep | Continuous
CastShadows       = true
IncludeInGdf      = true

[[SpaceSlots]]
SlotName          = "PrimarySurface"
FormatSuffix      = ".GeometrySpace"
DocumentPath      = "Content/Vehicles/RallyWheel.GeometrySpace"

[[SpaceSlots]]
SlotName          = "PrimaryMaterial"
FormatSuffix      = ".MaterialSpace"
DocumentPath      = "Content/Materials/TreadRubber.MaterialSpace"

[[SpaceSlots]]
SlotName          = "GravelSpray"
FormatSuffix      = ".ParticleSpace"
DocumentPath      = "Content/FX/GravelDust.ParticleSpace"
LocalOffsetMeters = [0.0, -0.32, 0.0]

[[SpaceSlots]]
SlotName          = "SlipstreamField"
FormatSuffix      = ".ForceField"
DocumentPath      = "Content/Physics/WheelSlipstream.ForceField"

[[SpaceSlots]]
SlotName          = "TyreAcoustic"
FormatSuffix      = ".Acoustic"
DocumentPath      = "Content/Audio/TyreGravelRoll.Acoustic"
```

Tier 2 appends 64-byte aligned binary sections:
- `.InstanceDefaults` — packed binary numeric overrides for zero-parse Shipping loads.
- `.Revision` — Editor parametric undo stream (`<= 215 MiB` per document; stripped on Shipping cook).

---

## 6. Phased Delivery Schedule

| Phase | Scope | Key Modules | Verification Gate |
| :--- | :--- | :--- | :--- |
| **Phase A (Completed)** | Core Foundation + `.Instance` Codec | `Engine/World/InstanceToken.h`, `Engine/World/WorldInstanceRegister.{h,cpp}`, `Engine/World/CategoryInstances.{h,cpp}`, `Engine/Codecs/InstanceCodec.{h,cpp}`, `Exhibits/WorldInstanceValidation.cpp` | `sizeof(InstanceToken) == 8`, `sizeof(InstanceRecordEntry) == 48`, 10,000-slot construct/reclaim in `0.62 ms`, stale token rejection, dual Editor/Production constructor parity, multi-format `.Instance` round-trip + Shipping strip (`WorldInstanceValidation` PASS). |
| **Phase B (Completed)** | Live Editor Bridge & Tick Wiring | `Engine/Editor/WorldInstanceEditorBridge.{h,cpp}`, `Engine/Lifecycle/EditorLifecycle.{h,cpp}`, `Exhibits/WorldInstanceEditorBridgeValidation.cpp` | Live `OutlinerPanel` RAII bind/visibility/teardown, `WorldSceneSpecification` adoption + Edit/Play SoA synchronization, `FrontierExecutableSmoke` PASS, zero warnings in Editor & Shipping. |
| **Phase C (Completed)** | Content Browser Drop, Bidirectional Sync & Shipping Cook | `Engine/Components/ContentBrowser/ThumbnailSheetRepository.cpp`, `Engine/Codecs/InstanceCodec.{h,cpp}` (`CookFileForShipping`), `Engine/Editor/WorldInstanceEditorBridge.{h,cpp}`, `Engine/Lifecycle/EditorLifecycle.cpp` | `.Instance` Viewport/Outliner drop, Gizmo/Outliner → `WorldRegister` bidirectional sync, Duplicate/Delete overrides, on-disk Shipping cook (`1472 B` Editor → `1216 B` Shipping), PASS. |
| **Phase D (Completed)** | Standalone Shipping Host & O(rebuilt) GPU Transform Sync | `Engine/World/WorldSceneSyncBridge.{h,cpp}`, `Engine/Lifecycle/RuntimeWorldHost.{h,cpp}`, `Exhibits/RuntimeWorldHostValidation.cpp` | Profile-independent `RuntimeWorldHost` (`SuspendableParticipant`) running inside `RuntimeLifecycle` at 60 Hz with zero Editor/ImGui symbols, `.WorldSpace` + cooked `.Instance` directory boot, and O(rebuilt) incremental `ClusterInstances` flush (`50 / 1,000` in `0.000725 ms`, `RuntimeWorldHostValidation` PASS). |

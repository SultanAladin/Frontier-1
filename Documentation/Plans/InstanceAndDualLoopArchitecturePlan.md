# Frontier Instance System, `.Instance` Format & Dual Editor/Production Loop Plan

> **Status:** Architectural Blueprint & Implementation Plan  
> **Applies To:** `Engine/World/`, `Engine/Editor/`, `Engine/Lifecycle/`, `Engine/Codecs/`  
> **Companion Plans:** `Documentation/Plans/DocumentFormatsInstancePlan.md` · `Documentation/Plans/GameEngineBringupPlan.md` · `Documentation/Plans/ThreadingArchitecturePlan.md` · `Docs/FrontierEngineArchitectureMap.html`

---

## 1. Executive Summary & Design Goals

Frontier needs a unified way for **every entity placed in a world** — meshes, procedural geometry, suns, moons, lights, cameras, volumetric clouds, particle emitters, force fields, triggers, audio sources, physics bodies, and gameplay agents — to:

1. **Register identically whether spawned from the Editor UI (`+` menu / drag-drop) OR C++ gameplay code** — automatically populating the World, Outliner, Inspector, Pick/Gizmo registry, and Undo log in Editor builds without fragile manual glue code.
2. **Stay ultra-lightweight and data-oriented (ZERO Unreal `UObject` bloat)** — no virtual inheritance hierarchy, no reflection garbage collector headers, no scattered per-object heap allocations, and zero per-tick cost for passive entities.
3. **Run on a single tight Production / Gaming / Shipping Main Loop** — domain systems execute via tight, branch-skippable loop functions (e.g., `ExecuteLunarLoop(Params)`, `ExecuteParticleLoop(Params)`, `ExecuteForceFieldLoop(Params)`, `ExecuteTriggerLoop(Params)`) over cache-contiguous Structure-of-Arrays (SoA) pools.
4. **Persist as composable `.Instance` assets** — lightweight Blueprint-like containers (built on Frontier's Two-Tier Hybrid TOML + 64-byte aligned binary `.Space` format) combining editor metadata with multi-format sub-asset slots (`.GeometrySpace`, `.MaterialSpace`, `.ParticleSpace`, `.SimulationSpace`, `.Photometry`, `.Acoustic`, etc.).

---

## 2. Lean `Instance` Architecture (No `UObject` Bloat)

### 2.1 Core Identity & Memory Footprint (`~32–48 Bytes`, Zero `vtable`)

Instead of a heavy polymorphic base class, every placed entity is identified by a **64-bit generation-checked `InstanceToken`** and backed by a fixed-size, non-virtual `InstanceHeader` inside a contiguous pool:

```cpp
// Compact 8-byte handle (safe against dangling references with zero GC overhead)
struct InstanceToken
{
    uint32_t SlotIndex  = 0u;   // Dense index into InstanceWorld header table
    uint16_t Generation = 0u;   // Incremented on destroy to invalidate stale tokens
    uint16_t DomainKind = 0u;   // InstanceDomainKind enum (Object, Lunar, Solar, Light, Particle, ForceField, Trigger, ...)

    [[nodiscard]] constexpr bool Valid() const noexcept { return Generation != 0u; }
};

// 48-byte non-virtual core record (fits cleanly in < 1 cache line, zero vtable)
struct alignas(16) InstanceHeader
{
    InstanceToken Token {};              //  8 B - Self identity + domain kind
    uint32_t      DomainSlot    = 0u;    //  4 B - Index into domain-specific SoA pool (e.g. LunarPool[DomainSlot])
    uint32_t      TransformSlot = 0u;    //  4 B - Index into contiguous WorldTransformSoA
    uint32_t      EnclosingSlot = ~0u;   //  4 B - Non-kinship hierarchy parent slot
    uint32_t      FirstChild    = ~0u;   //  4 B - Hierarchy traversal link
    uint32_t      NextSibling   = ~0u;   //  4 B - Hierarchy traversal link
    uint32_t      Flags         = 0u;    //  4 B - Visible | Active | SimulationTick | DirtyTransform | Static | Locked
    uint64_t      AssetHash     = 0ull;  //  8 B - Source .Instance asset hash (0 for runtime-spawned)
    uint64_t      Reserved      = 0ull;  //  8 B - Alignment / future spatial cell id
};
```

### 2.2 Domain-Specialized Instance Classes (Zero Virtual Dispatch)

Each concrete world category is a lightweight, strongly-typed C++ RAII/handle class owning its domain slot in `InstanceWorld`:

| Instance Class | Contiguous SoA Pool | Tight Loop Function | Multi-Format Asset Slots |
| :--- | :--- | :--- | :--- |
| **`ObjectInstance`** | `ObjectInstancePool` | `ExecuteObjectTransformLoop(Params)` | `.GeometrySpace`, `.MaterialSpace`, `.UVSpace`, `.SimulationSpace` |
| **`LunarInstance`** | `LunarInstancePool` | `ExecuteLunarLoop(Params)` | `.ImageSpace` (Lunar atlas), ephemeris parameters |
| **`SolarInstance`** | `SolarInstancePool` | `ExecuteSolarLoop(Params)` | Solar ephemeris, physical spectrum / `.ColourSpace` |
| **`AtmosphereInstance`** | `AtmosphereInstancePool` | `ExecuteAtmosphereLoop(Params)` | `.VolumetricSpace`, `.ColourSpace` |
| **`CloudInstance`** | `CloudInstancePool` | `ExecuteCloudLoop(Params)` | `.VolumetricSpace` (cloud deck / local cloud) |
| **`LightInstance`** | `LightInstancePool` | `ExecuteLightLoop(Params)` | `.Photometry` (IES), `.ColourSpace` |
| **`ParticleInstance`** | `ParticleInstancePool` | `ExecuteParticleLoop(Params)` | `.ParticleSpace`, `.MaterialSpace`, `.TextureSpace` |
| **`ForceFieldInstance`** | `ForceFieldInstancePool` | `ExecuteForceFieldLoop(Params)` | `.SimulationSpace`, `.VectorSpace` (field curve) |
| **`TriggerInstance`** | `TriggerInstancePool` | `ExecuteTriggerLoop(Params)` | Analytic volume / `.GeometrySpace` bounds, event binding |
| **`AudioInstance`** | `AudioInstancePool` | `ExecuteAudioLoop(Params)` | `.Acoustic` |
| **`CameraInstance`** | `CameraInstancePool` | `ExecuteCameraLoop(Params)` | `.Render` (lens/sensor/post-process stack) |
| **`FolderInstance`** | *(Editor-only hierarchy)* | *(Stripped in Shipping)* | Editor grouping metadata |

---

## 3. Dual-Mode Constructor & Destructor (`#if FRONTIER_PROFILE_EDITOR` vs Production)

Every domain instance class implements **one Production path** that is ALWAYS compiled, plus an **Editor `#if FRONTIER_PROFILE_EDITOR` layer** compiled strictly out of Gaming/Shipping binaries:

```cpp
class LunarInstance
{
public:
    // Production / Shipping & Editor unified construction
    explicit LunarInstance(InstanceWorld& World, const LunarCreateSpec& Spec);
    ~LunarInstance();

    // Non-copyable, movable (transfers slot ownership cleanly without re-registering)
    LunarInstance(const LunarInstance&)            = delete;
    LunarInstance& operator=(const LunarInstance&) = delete;
    LunarInstance(LunarInstance&& Other) noexcept;
    LunarInstance& operator=(LunarInstance&& Other) noexcept;

    [[nodiscard]] InstanceToken QueryToken() const noexcept { return Token; }

private:
    void RegisterProduction(InstanceWorld& World, const LunarCreateSpec& Spec);
    void UnregisterProduction() noexcept;

#if FRONTIER_PROFILE_EDITOR
    void RegisterEditorSurfaces(const LunarCreateSpec& Spec);
    void UnregisterEditorSurfaces() noexcept;
#endif

    InstanceWorld* OwnerWorld = nullptr;
    InstanceToken  Token      {};
};
```

### Exact Construction & Destruction Sequence

1. **Production / Shipping Build (`FRONTIER_PROFILE_EDITOR == 0`):**
   - **Constructor:** calls `RegisterProduction()` ONLY → allocates `InstanceHeader` + dense `LunarPool` slot + marks `LunarPool.Dirty = true`. Zero strings, zero ImGui/Outliner headers, zero Inspector overhead.
   - **Destructor:** calls `UnregisterProduction()` ONLY → `O(1)` swap-remove from `LunarPool` (updating moved slot's `InstanceHeader::DomainSlot`), increments `Generation`, releases `InstanceHeader` slot.
2. **Editor Build (`FRONTIER_PROFILE_EDITOR == 1`):**
   - **Constructor:**
     1. Calls `RegisterProduction()` (runs the exact same game-world registration!).
     2. Calls `RegisterEditorSurfaces()`:
        - Registers an `OutlinerRecordEntry` with canonical `DomainTag` (e.g. `"env:moon"`), Studio SVG icon, folder placement, and duplicate/removal policies.
        - Binds the `InstanceToken` into `InspectorRegistry` so selecting it in the Outliner or Viewport immediately opens the matching 1:1 Inspector card.
        - Registers transform/pick proxy in the Editor Viewport gizmo & selection map.
        - Emits a parametric `.Revision` creation op (when constructed by user action).
   - **Destructor:**
     1. Calls `UnregisterEditorSurfaces()` → cleanly detaches from Outliner, clears Inspector selection if active, removes gizmo pick binding.
     2. Calls `UnregisterProduction()` → frees dense simulation/render slot.

> **Key Win:** Spawning a Moon, Point Light, Procedural Tyre, Particle Emitter, Force Field, or Trigger from **C++ code** OR from the **Editor `+` button** calls the **exact same constructor**, making desync between the World, Outliner, and Inspector structurally impossible.

---

## 4. Tight Main-Loop Architecture (`Execute*Loop(Params)`)

Instead of calling virtual `Entity->Tick(dt)` across thousands of heap-scattered objects, Frontier executes **explicit, domain-ordered tight loops** directly inside the world simulation frame:

```cpp
struct LunarLoopParams
{
    bool                    ShouldUpdate      = false; // Fast-skip gate: 1 branch exit if false!
    double                  DeltaSeconds      = 0.0;
    double                  WorldTimeSeconds  = 0.0;
    bool                    EphemerisDirty    = false;
    GeometricRaster::WorldSceneSpecification* TargetScene = nullptr;
};

// Tight data-oriented loop — zero virtual calls, zero heap allocations
void ExecuteLunarLoop(LunarInstancePool& Pool, const LunarLoopParams& Params) noexcept
{
    if (!Params.ShouldUpdate || Pool.ActiveCount == 0u)
    {
        return; // ~0.0001 ms skip when static/paused/empty
    }

    const uint32_t Count = Pool.ActiveCount;
    for (uint32_t i = 0u; i < Count; ++i)
    {
        if ((Pool.Flags[i] & LunarFlag_Enabled) == 0u)
        {
            continue;
        }
        // Linear SoA math: orbital angle, phase illumination, world direction -> TargetScene->Atmosphere.Moons[i]
    }
    Pool.Dirty = false;
}
```

### 4.1 Production / Gaming / Shipping Tight Loop vs Editor Loop

```text
============================================================================================
  PRODUCTION / GAMING / SHIPPING FRAME LOOP (FRONTIER_PROFILE_SHIPPING)
============================================================================================
  1. OS Input Poll (GLFW / Gamepad / Raw Input)
  2. Tight Domain Loops (each skips in 1 branch when !Params.ShouldUpdate):
       ├── ExecuteInputAndCameraLoop(CamParams)
       ├── ExecuteSolarLoop(SolarParams)         & ExecuteLunarLoop(LunarParams)
       ├── ExecuteAtmosphereAndCloudLoop(AtmoParams)
       ├── ExecuteForceFieldLoop(FieldParams)    [parallelizable via FrontierThreading]
       ├── ExecutePhysicsStepLoop(PhysParams)    [JoltPhysics fixed-step]
       ├── ExecuteTriggerLoop(TriggerParams)     [overlap/volume events]
       ├── ExecuteParticleLoop(ParticleParams)   [GPU compute emit/advect]
       ├── ExecuteAudioLoop(AudioParams)
       └── ExecuteObjectTransformLoop(XfmParams) [collects dirty transform spans ONLY]
  3. GPU World Render (Phase 2 Pipeline — 2.5–4.3 ms on GTX 1650 SUPER!):
       ├── GPU Dirty-Instance Transform (WorldInstanceTransformCompute.slang)
       ├── Async Precompute (SkyLUT, Shadow Cascades, 3D GDF/Radiosity — dirty only)
       ├── Pass 1: HW Rasterized Reversed-Z VisBuffer (WorldPrimaryVisRaster.slang)
       ├── Pass 2: Lean Direct Compute + ContactPCSS (ComputeEntry)
       ├── Pass 3: Half-Res (¼ px) 3D GDF AO & 5-Cone GI (HalfResIndirectEntry)
       └── Pass 4: 2×2 Joint Bilateral Upsample + Fog + ACES Tone (DenoiseCompositeEntry)
  4. Direct Fullscreen Swapchain Present (ZERO ImGui, ZERO Outliner, ZERO Gizmo!)

============================================================================================
  EDITOR FRAME LOOP (#if FRONTIER_PROFILE_EDITOR — wraps the EXACT same core loop!)
============================================================================================
  1. OS Input + ImGui Event Routing + Gizmo Hover/Drag Pick (non-blocking 3-slot ring)
  2. Editor-Only Pre-Sync:
       ├── Apply Outliner/Inspector parameter edits -> dirty flags on Domain Pools
       └── Execute Play-In-Editor (PIE) vs Edit-Mode gate (sets Params.ShouldUpdate flags!)
  3. [IDENTICAL TIGHT DOMAIN LOOPS AS PRODUCTION ABOVE]
       (In Edit mode: Solar/Lunar/Clouds only set ShouldUpdate=true when animated or edited;
        Physics/Triggers/Particles run when PIE is active or preview-stepping!)
  4. [IDENTICAL PHASE 2 GPU WORLD RENDER AS PRODUCTION ABOVE]
  5. Editor-Only Overlays (100% Async — zero mid-frame stalls):
       ├── 3-Slot Async Gizmo Overlay Raster (0.24–0.52 ms)
       ├── Retained-Cache Inspector Cards + InspectorGpuBaker + 50 Studio SVG Icons
       └── Deferred-Retirement ImGui Swapchain Present
```

---

## 5. The `.Instance` File Format (Frontier's Lean Blueprint Asset)

Aligned directly with `Documentation/Plans/DocumentFormatsInstancePlan.md` (Two-Tier Hybrid Container: UTF-8 TOML manifest at top + 64-byte aligned binary payload & `.Revision` history at bottom):

### 5.1 Canonical Extension & Composition
- **Extension:** `.Instance` *(case-insensitive load: `.instance` / `.Instance`, canonical PascalCase write)*.
- **Role:** Composable world archetype/prefab ("Blueprint" without bytecode VM bloat). One `.Instance` file can combine **multiple native `.Space` formats** into a single placeable world object!

### 5.2 Tier 1 Manifest Example (Human-Readable TOML)

```toml
[Document]
Format        = "Frontier.Instance"
SchemaVersion = 1
CanonicalKind = "ObjectInstance"        # ObjectInstance | LunarInstance | ParticleInstance | ForceFieldInstance | TriggerInstance ...
InstanceUuid  = "019a7c2e-8b4d-7000-a210-4f9e2b3c1104"

[EditorMetadata]                         # Stripped automatically by Shipping Cook!
DisplayTitle  = "Turbocharged Rally Wheel"
DomainTag     = "geom:procedural"
OutlinerIcon  = "EngineContent/Icons/Editor/editor-mesh.svg"
DefaultFolder = "Scene/Vehicles"
AuthorNotes   = "Composable wheel assembly combining Geometry, Material, Particle dust & ForceField draft."

[Transform]
PositionM     = [0.0, 0.0, 0.34]
RotationDeg   = [0.0, 0.0, 0.0]
Scale         = [1.0, 1.0, 1.0]

[Simulation]
TickPolicy    = "DirtyOnly"             # Never | DirtyOnly | FixedStep | Always
CastShadows   = true
IncludeInGdf  = true

# Multi-Format Composable Slots (one .Instance can reference any combination of .Space files!)
[[Slots]]
Role          = "Geometry"
Format        = ".GeometrySpace"
AssetPath     = "Content/Vehicles/RallyWheel.GeometrySpace"

[[Slots]]
Role          = "Material"
Format        = ".MaterialSpace"
AssetPath     = "Content/Materials/TreadRubber.MaterialSpace"

[[Slots]]
Role          = "Emitter"
Format        = ".ParticleSpace"
AssetPath     = "Content/FX/GravelDust.ParticleSpace"
LocalOffsetM  = [0.0, -0.32, 0.0]

[[Slots]]
Role          = "ForceField"
Format        = ".SimulationSpace"
AssetPath     = "Content/Physics/WheelSlipstream.SimulationSpace"

[[Slots]]
Role          = "Audio"
Format        = ".Acoustic"
AssetPath     = "Content/Audio/TyreGravelRoll.Acoustic"

[BinaryDirectory]
# 64-byte aligned binary sections follow after delimiter:
#   - .Defaults   (packed binary property overrides for instant zero-parse Shipping load)
#   - .Revision   (Editor-only parametric undo history <= 215 MiB; stripped in Shipping)
```

### 5.3 Multi-Format Loading & Shipping Cook
- **In Editor:** `InstanceDecoder` reads the TOML manifest + EditorMetadata + `.Revision` log, resolves each `[[Slots]]` reference through its domain codec (`GeometryDecoder`, `ParticleDecoder`, `MaterialDecoder`, etc.), and invokes the matching `#if FRONTIER_PROFILE_EDITOR` constructor — instantly spawning the complete multi-format object into World + Outliner + Inspector.
- **In Production / Shipping Cook:** Strips `[EditorMetadata]` and `.Revision`, resolves `AssetPath` strings to 64-bit content hashes, and packs all level instances into contiguous binary arrays matching `InstanceHeader` + domain SoA pools directly (sub-millisecond level streaming!).

---

## 6. Phased Implementation Roadmap

| Phase | Scope | Key Deliverables | Verification Gate |
| :--- | :--- | :--- | :--- |
| **Phase A — Core Foundation** | `Engine/World/` + `Engine/Codecs/` | `InstanceToken`, `InstanceHeader`, `WorldTransformSoA`, `InstanceWorld`, `InstanceCodec.{h,cpp}` (`.Instance` Tier-1 TOML + Tier-2 binary decoder/encoder). | Unit test: spawn/destroy 10,000 instances in < 1 ms, generation invalidation, round-trip `.Instance` encode/decode. |
| **Phase B — Environment & Lighting Instances** | Migrate `WorldEnvironmentRegistry` → Domain Instances | `AtmosphereInstance`, `SolarInstance`, `LunarInstance`, `CloudInstance`, `StarfieldInstance`, `LightInstance` with dual `#if FRONTIER_PROFILE_EDITOR` constructors/destructors and `ExecuteLunarLoop` / `ExecuteSolarLoop` / `ExecuteLightLoop`. | Add/Delete/Undo Moon, Sun, Atmosphere, Point/Spot/Area Light from BOTH Editor `+` button and C++ test — identical Outliner + Inspector + GPU state; `TickMs < 0.08 ms`. |
| **Phase C — Geometry, FX & Gameplay Instances** | Full World Entity Coverage | `ObjectInstance`, `CameraInstance`, `ParticleInstance`, `ForceFieldInstance`, `TriggerInstance`, `AudioInstance` + tight `ExecuteParticleLoop`, `ExecuteForceFieldLoop`, `ExecuteTriggerLoop`. | Multi-format `.Instance` test (`.GeometrySpace` + `.ParticleSpace` + `.SimulationSpace` trigger/forcefield) spawns cleanly in Editor and headless Shipping test. |
| **Phase D — Dedicated Shipping Host & PIE** | `Engine/Lifecycle/RuntimeLifecycle.{h,cpp}` | Clean `FRONTIER_PROFILE=Shipping` executable using ONLY the tight Production loop (zero ImGui/Outliner/Inspector link dependency) + Editor Play-In-Editor (PIE) toggle driving `Params.ShouldUpdate`. | Build both `FRONTIER_PROFILE=Editor` and `FRONTIER_PROFILE=Shipping`; confirm Shipping binary runs full Phase 2 GPU pipeline with zero Editor symbols and > 250 FPS on GTX 1650 SUPER. |

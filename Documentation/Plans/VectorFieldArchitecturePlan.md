# Active Plan: Vector Field Simulation, 19 Force Laws & `VectorFieldVisualizer` C++20 Architecture

> **Status:** 🚧 **Planning & HTML/CSS/JS Prototype Phase** — **ZERO C++ files authored yet.**
> **HTML Reference Prototype:** `Experimental/VectorFieldVisualizer/` (founded on `Experimental/ProjectZeroEditor/`, retaining the Inspector Wind Anemometer telemetry while placing engineering 3D vector/streamline/heatmap visualization exclusively in the Viewport).
> **C++ Target Profile:** ISO C++20 (`cxx_std_20`) + **100% GPU `.slang`** viewport overlays under `Engine/SimulationSpace/ForceFields/`, `Engine/Shaders/{Compute,VectorField}/`, `Engine/Workspaces/Simulation/`, and `Engine/Editor/Workspaces/Simulation/`.

---

## 1. Governance & 6-Gate Naming Audit (Mandatory 6-Gate Comparison)

*Legend — Compliance / Safety: 🔴 = highest/best · Friction / Migration Cost: ✔️ = lowest/best.*

Per `Documentation/BannedWordsVocabulary.md` and `Documentation/AgenticInstructions/SKILL-Naming.md`, several brainstorm terms require mechanism-first Frontier C++20 identifiers before C++ authoring begins:

| Brainstorm Concept | Governance Audit (`BannedWordsVocabulary` + `SKILL-Naming`) | Approved Frontier C++20 Identifier | Risk | Quality | Rank |
| :--- | :--- | :--- | :---: | :---: | :---: |
| `VectorField` · `TurbulenceField` · `VectorFieldEditor` · `StreamlineVisualizer` · `VectorFieldVisualizer` | 🟢 **Clean** — mechanism-grounded physics & tooling nouns (`Streamline` avoids banned `flow`). | Keep exact identifiers | ✔️ | ⭐⭐⭐⭐⭐ | 🥇 🏆 |
| `EForceDuration` · `ForceAttenuation` · `ForceDomain` | 🟢 **Clean** — temporal envelope, radial falloff curve, and finite spatial domain. | Keep exact identifiers | ✔️ | ⭐⭐⭐⭐⭐ | 🥇 🏆 |
| `FlowField` · `FlowStream` · `Navigation Flow Field` | 🔴 **Barred (`flow`)** in `BannedWordsVocabulary.md` §1.1. | **`ChannelField`** · **`DirectedStream`** · **`SteeringDirectionField`** | ✔️ | ⭐⭐⭐⭐⭐ | 🥇 |
| `Atmospheric Gust Emitter` | 🔴 **Barred (`emitter`)** in `BannedWordsVocabulary.md` §1.1. | **`AtmosphericGustSource`** *(paired with `WindGeneratorSource`)* | ✔️ | ⭐⭐⭐⭐⭐ | 🥇 |
| `Vortex` | 🔴 **Barred (`Vortex`)** in `BannedWordsVocabulary.md` §1.2 (mythology/pseudo-space trope). | **`RotationalColumn`** / **`BurgersSwirl`** *(alias label `"Vortex"` allowed in UI strings only)* | ✔️ | ⭐⭐⭐⭐ | 🥇 |
| `SurfaceAdhesion` | 🚩 **Caution (`Surface`)** — earlier UV ruling replaced vague `Surface` with concrete contact nouns. | **`BoundaryAdhesion`** *(nearest `SignedDistanceField` boundary cling)* | ✔️ | ⭐⭐⭐⭐⭐ | 🥇 |
| `ForceModel` (enum name) | 🚩 **Flagged (`Model`)** in `SKILL-Naming.md` §1. | **`EForceLaw`** *(19 enumerators governing force physics)* | ✔️ | ⭐⭐⭐⭐⭐ | 🥇 |
| `ForceEvaluator` · `FieldProbe` · `TelemetryCard` | 🔴 **Barred (`evaluator`, `probe`, `card`)** in `BannedWordsVocabulary.md` §1.1. | **`ForceFieldAccumulator`** · **`PointSampleCursor`** · **`AnemometerTelemetryPanel`** | ✔️ | ⭐⭐⭐⭐⭐ | 🥇 |

⭐ **Plan Convention:** All C++20 symbols and filenames use the compliant Frontier identifiers above (`ChannelField`, `DirectedStream`, `RotationalColumn`, `BoundaryAdhesion`, `AtmosphericGustSource`, `EForceLaw`, `ForceFieldAccumulator`), while the HTML UI prototype preserves intuitive domain labels (`Linear Wind`, `Vortex Column`, `Flow Stream`, `Surface / Boundary Adhesion`) for clarity.

---

## 2. Three Runtime Field Engines & Viewport Separation

```text
+---------------------------------------------------------------------------------------------------+
| FRONTIER VECTOR FIELD ARCHITECTURE (Bounded Spatial Domains — Zero Unbounded Spillover)           |
+---------------------------------------------------------------------------------------------------+
|  [1] VectorField                      [2] ChannelField ("FlowField")     [3] TurbulenceField      |
|   • LinearWind + Shear/Veer            • River/Ocean DirectedStream       • ∇ × ψ CurlNoise       |
|   • RadialBurst (1/r, 1/r², R=v·t)     • ThermalChimney Updraft           • AtmosphericGustSource |
|   • Attractor & Repulsor               • VenturiJet / Slipstream          • VorticityConfinement  |
|   • RotationalColumn (Burgers-Rott)    • SteeringDirectionField           • BrownianNoise Drift   |
|   • Drag & Buoyancy                    • WakeSlipstream (Tow + Roll)                              |
+---------------------------------------------------------------------------------------------------+
                 |                                      |                                   |
                 +--------------------------------------+-----------------------------------+
                                                        v
                         ForceFieldAccumulator + FieldLatticeCascade (4 Cascades · SIMD)
                                                        |
         +----------------------------------------------+-------------------------------------------+
         v                                                                                          v
+--------------------------------------------------------+  +---------------------------------------+
| CENTER 3D VIEWPORT (100% GPU .slang Overlays)          |  | RIGHT INSPECTOR & LEFT OUTLINER       |
|  • VectorArrowRasterization.slang  (3D Instanced)      |  |  • Outliner: Forces Only (No Entities)|
|  • StreamlineRasterization.slang   (RK4 3D Ribbons)    |  |  • Retained Wind Anemometer (60s Trace|
|  • SliceHeatmapRasterization.slang (XZ/XY/YZ + Iso)    |  |    + Beaufort Scale + Gust/Lull Rail) |
|  • InfluenceEnvelopeRasterization  (Bounds + Wavefront)|  |  • Live Attenuation & Duration Plots  |
|  • MoteAdvectionCompute.slang      (Lagrangian Motes)  |  |  • Per-Law Engineering Infographics   |
+--------------------------------------------------------+  +---------------------------------------+
```

### Key Architectural Invariants

1. **Strict Finite Spatial Influence (Zero Unbounded Leaks):** Every field source carries an explicit finite envelope (`Sphere`, `Column`, `OrientedBox`, `SplineTube`, or `LatticeRegion`). Outside the envelope boundary, contribution is mathematically exact zero (`0.0f`).
2. **Contribution Channel Separation:**
   - **Advection Velocity Channels (`[m/s]`):** `LinearWind`, `DirectedStream`, `RotationalColumn`, `Thermal`, `VenturiJet`, `WakeSlipstream`, `CurlNoise`, `AtmosphericGustSource` accumulate into a unified 3D velocity lattice (`FieldLatticeCascade`).
   - **Direct Acceleration Channels (`[m/s²]`):** `RadialBurst`, `Attractor`, `Repulsor`, `AxisAttractor`, `SplineForce`, `BoundaryAdhesion`, `Containment`, `Buoyancy`, `Lift`, `Torque`, `MagnusForce`, `LorentzField`, `MagneticDipole` integrate directly onto rigid bodies, particles, and fluid voxels (`Δv = a · Δt`).
   - **Damping Channel (`[1/s]`):** `Drag` removes relative kinetic energy (`v *= exp(-k_d · Δt)`) without injecting spurious directional bias.
3. **100% GPU `.slang` Overlays (`ImportantNotes.md` §1):** All 3D arrows, RK4 streamlines, influence cages, wavefront rings, and XZ/XY/YZ cross-section heatmaps execute exclusively via GPU `.slang` compute & raster units — **never CPU line loops and never ImGui `ImDrawList`**.
4. **Viewport vs. Inspector Boundary:** 3D/2D field visualization renders exclusively inside the **Viewport**. The **Inspector** hosts parameter controls, the retained 60-second Wind Anemometer + Beaufort readout, live attenuation/waveform plots, and per-law engineering infographics.

---

## 3. Temporal Modes (`EForceDuration`), `ForceAttenuation` & `ForceDomain`

| Enumeration | Values & Mechanism | Governing Math |
| :--- | :--- | :--- |
| **`EForceDuration`** | `Continuous` · `Modulated` · `Impulse` | `Continuous`: gate `g(t) = 1`.<br>`Modulated`: smoothstep attack/release envelope or periodic window `g(t) = Smooth(t_local / t_in) · (1 - Smooth((t_local - t_out) / t_fade))`.<br>`Impulse`: instantaneous delta-v `Δv = (F · Δt) / m` + propagating shell `R(t) = R₀ + v_wave · t` with shell Gaussian width `σ_w`. |
| **`ForceAttenuation`** | `Linear` · `InverseSquare` · `SmoothStep` · `None` | Let `u = clamp(d / R_max, 0, 1)`:<br>• `Linear`: `1 - u`<br>• `InverseSquare`: `[1 / (1 + 8u²)] · [1 - SmoothStep((u - 0.85) / 0.15)]` *(zero at boundary `u = 1`)*<br>• `SmoothStep`: `(1 - u²)²` *(C¹-smooth zero at boundary)*<br>• `None`: `u < 1 ? 1 : 0` *(hard bounded zone)* |
| **`ForceDomain`** | `LinearWind` · `RadialBurst` · `Attractor` · `Repulsor` · `RotationalColumn` · `CurlNoise` · `DirectedStream` | Routes source scheduling into `VectorField`, `ChannelField`, or `TurbulenceField` SIMD evaluation lanes and GPU lattice cascade strata. |

---

## 4. Unified 19-Law `EForceLaw` (`ForceModel`) Reference

| # | `EForceLaw` (UI Name) | Runtime Field | Unit | Governing Vector Equation | Spatial Bound |
| :---: | :--- | :--- | :---: | :--- | :--- |
| 1 | `LinearWind` | `VectorField` | `m/s` | `v = v₀(1 + s_z·h)·d(θ + v_z·h) + Gust(x, t)` | `Column` / `Box` |
| 2 | `DirectedStream` (`FlowStream`) | `ChannelField` | `m/s` | `v = S · g(t) · A(u) · d̂_stream` | `OrientedBox` |
| 3 | `Buoyancy` | `VectorField` | `m/s²` | `a_y = g · (ρ_ambient(y) - ρ_body) / ρ_body · A(u)` | `Column` / `Sphere` |
| 4 | `RadialBurst` | `VectorField` | `m/s²` | `a = S · (R₀/R(t))^0.65 · exp(-½((r - R(t))/σ_w)²) · r̂` | `Sphere` (`Impulse`) |
| 5 | `Attractor` | `VectorField` | `m/s²` | `a = -S · A(u) · r̂ + ω_swirl · (ŷ × r̂) · A(u)` | `Sphere` |
| 6 | `Repulsor` | `VectorField` | `m/s²` | `a = +S · A(u) · r̂` | `Sphere` |
| 7 | `AxisAttractor` | `VectorField` | `m/s²` | Let `q = closest(p, A, B)`: `a = -S · A(u_perp) · normalize(p - q) + S_ax · axiŝ` | `Capsule` / `Column` |
| 8 | `SplineForce` | `ChannelField` | `m/s²` | `a = S_tan · T̂(s*) - S_rad · A(u_tube) · (p - C(s*))` along Hermite curve | `SplineTube` |
| 9 | `BoundaryAdhesion` (`SurfaceAdhesion`) | `ChannelField` | `m/s²` | Given SDF distance `φ(p)` & `n̂ = ∇φ`: `a = -S · sign(φ - d_target) · exp(-φ²/σ²) · n̂` | `Sphere` / `Box` |
| 10 | `Containment` | `VectorField` | `m/s²` | Zero for `r < R_in`; restoring `-S · ((r - R_in)/(R_out - R_in)) · r̂ - c_d · (v·r̂) r̂` | `Sphere` / `Box` |
| 11 | `RotationalColumn` (`Vortex`) | `VectorField` | `m/s` | `v_θ = S · (2ρ)/(1 + ρ²)`, `v_r = -I · B(y) · (2ρ)/(1 + ρ²)`, `v_y = U · exp(-0.42ρ²)` | `Column` |
| 12 | `Torque` | `VectorField` | `rad/s²` | `α = (S / I_eff) · A(u) · axiŝ` *(tangential couple `r × F_θ` on particles/bodies)* | `Sphere` / `Column` |
| 13 | `MagnusForce` | `ChannelField` | `m/s²` | `a_M = C_M · A(u) · (ω_spin × v_rel)` *(spin-induced curved deflection)* | `Sphere` / `Box` |
| 14 | `Drag` | `VectorField` | `1/s` | `a_d = -(k₁ + k₂ · |v_rel|) · A(u) · v_rel` *(velocity clamping exponential decay)* | `Box` / `Sphere` |
| 15 | `Lift` | `ChannelField` | `m/s²` | `a_L = ½ ρ C_L |v_rel|² · A(u) · n̂_lift` *(upwash / airfoil lift vector)* | `Column` / `Box` |
| 16 | `CurlNoise` | `TurbulenceField` | `m/s` | `v = S · A(u) · (∇ × ψ(p / λ_eddy - v_drift·t))` *(exact `∇·v = 0` divergence-free)* | `Box` / `Sphere` |
| 17 | `BrownianNoise` | `TurbulenceField` | `m/s²` | `a = S · A(u) · PCGNormal3(CellID, TickSeed) / √Δt` *(deterministic micro-drift)* | `Sphere` / `Box` |
| 18 | `LorentzField` | `VectorField` | `m/s²` | `a = (q / m) · A(u) · (E⃗ + v_rel × B⃗)` *(electromagnetic cross-field deflection)* | `Box` / `Sphere` |
| 19 | `MagneticDipole` | `VectorField` | `m/s²` | `B(r) = S · [3(m̂·r̂)r̂ - m̂] / max(r, r_soft)³ · SmoothBoundary(u)` | `Sphere` |

---

## 5. Folder Layout & Module Boundaries (Synced with `Documentation/FolderStructure.md`)

```text
Engine/
├── SimulationSpace/ForceFields/
│   ├── ForceFieldDescriptors.h            // EForceDuration, ForceAttenuation, ForceDomain, 19 EForceLaw & POD structs
│   ├── VectorField.{h,cpp}                // Continuous analytical & impulse laws (LinearWind..MagneticDipole)
│   ├── ChannelField.{h,cpp}               // Directed currents, thermals, Venturi jets, steering fields & wakes
│   ├── TurbulenceField.{h,cpp}            // ∇×ψ CurlNoise, AtmosphericGustSource, vorticity confinement & Brownian
│   ├── ForceFieldAccumulator.{h,cpp}      // Concept-constrained SIMD multi-source accumulation & finite-bound culling
│   └── FieldLatticeCascade.{h,cpp}        // 4-cascade 3D velocity/acceleration lattice (finest 2 m → 100 km)
├── Shaders/
│   ├── Compute/
│   │   ├── FieldLatticeCompute.slang      // Parallel 3D cascade bake (Velocity.xyz + CurlMag)
│   │   └── MoteAdvectionCompute.slang     // GPU RK2 Lagrangian particle mote advection buffer
│   └── VectorField/                       // 100% GPU viewport visualization (Zero CPU lines, zero ImGui)
│       ├── VectorArrowRasterization.slang // Instanced 3D needles colored by Turbo/Diverging/Domain LUT
│       ├── StreamlineRasterization.slang  // RK4 3D advection ribbons with directional phase pulse
│       ├── SliceHeatmapRasterization.slang// XZ/XY/YZ cross-section heatmap + isoline contour shader
│       └── InfluenceEnvelopeRasterization.slang // Finite Sphere/Column/Box/SplineTube & Impulse wavefronts
├── Workspaces/Simulation/
│   ├── ForceFieldDocument.{h,cpp}         // Active force list, point-sample cursor, impulse trigger clock & cascade config
│   └── ForceFieldOperations.{h,cpp}       // Parametric Add/Edit/Remove/Trigger/Solo -> OperationRecorder (215/750 MiB)
├── Editor/Workspaces/Simulation/          // FRONTIER_PROFILE_EDITOR only (Excluded from Development/Shipping)
│   ├── VectorFieldEditor.{h,cpp}          // WorkspaceDockContainer host & Force Placement gizmos
│   ├── VectorFieldVisualizer.{h,cpp}      // Coordinates VectorArrow, SliceHeatmap, Envelope & Mote GPU units
│   ├── StreamlineVisualizer.{h,cpp}       // RK4 streamline seed lattice & animated pulse coordinator
│   ├── ForceOutlinerPanel.{h,cpp}         // Forces-only Outliner, active/muted tracks, filters & impulse triggers
│   └── AnemometerTelemetryPanel.{h,cpp}   // Retained 60s rolling Anemometer, Beaufort scale, attenuation & law plots
└── Exhibits/
    └── VectorFieldValidation.cpp          // All 19 laws, finite-bound zero leaks, |∇·v| < ε & PNG visual snapshots
```

---

## 6. C++20 Concept & Interface Sketch (Blueprint Only — Zero `.h`/`.cpp` Files Written)

```cpp
/*============================================================================================================================================
                                                    FORCEFIELDDESCRIPTORS.H
============================================================================================================================================*/
// 🧩 Compile-time enumerations, spatial envelope bounds, and SIMD force sample descriptors for SimulationSpace::ForceFields.

#pragma once

#include <concepts>
#include <cstdint>
#include <span>

namespace Frontier::Simulation::ForceFields
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class EForceDuration : uint8_t
{
    Continuous = 0, // [-] - Steady uninterrupted output (gate = 1.0)
    Modulated  = 1, // [-] - Periodic or attack/sustain/release windowed modulation
    Impulse    = 2  // [-] - Instantaneous Δv = F·Δt/m and propagating wavefront R(t) = v_wave · t
};

enum class ForceAttenuation : uint8_t
{
    Linear        = 0, // [-] - 1 - clamp(r / R, 0, 1)
    InverseSquare = 1, // [-] - Bounded 1/(1 + 8u²) with smooth C¹ taper to zero at R
    SmoothStep    = 2, // [-] - C¹ polynomial (1 - u²)² reaching exact zero at R
    None          = 3  // [-] - Uniform inside finite bound, exact zero outside R
};

enum class ForceDomain : uint8_t
{
    LinearWind       = 0,
    RadialBurst      = 1,
    Attractor        = 2,
    Repulsor         = 3,
    RotationalColumn = 4,
    CurlNoise        = 5,
    DirectedStream   = 6
};

enum class EForceLaw : uint8_t
{
    LinearWind       = 0,  DirectedStream = 1,  Buoyancy       = 2,  RadialBurst    = 3,
    Attractor        = 4,  Repulsor       = 5,  AxisAttractor  = 6,  SplineForce    = 7,
    BoundaryAdhesion = 8,  Containment    = 9,  RotationalColumn = 10, Torque       = 11,
    MagnusForce      = 12, Drag           = 13, Lift           = 14, CurlNoise      = 15,
    BrownianNoise    = 16, LorentzField   = 17, MagneticDipole = 18
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONCEPTS
//------------------------------------------------------------------------------------------------------------------------

template <typename SampleFieldType>
concept SpatialForceSampleSource = requires(const SampleFieldType& Source,
                                            float                  PositionX,
                                            float                  PositionY,
                                            float                  PositionZ,
                                            double                 TimeSeconds,
                                            float&                 OutX,
                                            float&                 OutY,
                                            float&                 OutZ)
{
    { Source.EvaluateSample(PositionX, PositionY, PositionZ, TimeSeconds, OutX, OutY, OutZ) } noexcept -> std::same_as<bool>;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) ForceEnvelopeParams
{
    float            CentreMetres[3]  = { 0.0f, 0.0f, 0.0f }; // [m] - World centre of finite force volume
    float            OuterRadius      = 240.0f;               // [m] - Strict outer zero boundary
    float            HeightMetres     = 180.0f;               // [m] - Column / box vertical span
    float            WaveSpeed        = 110.0f;               // [m/s] - Impulse wavefront R(t) = v_wave · t
    double           TriggeredSeconds = -1.0;                 // [s] - Monotonic impulse launch timestamp
    EForceDuration   DurationMode     = EForceDuration::Continuous;
    ForceAttenuation AttenuationCurve = ForceAttenuation::SmoothStep;
    EForceLaw        ForceLaw         = EForceLaw::LinearWind;
    ForceDomain      DomainCategory   = ForceDomain::LinearWind;
};

} // namespace Frontier::Simulation::ForceFields
```

---

## 7. Threading, Revision & Verification Roadmap

| Milestone | Target Deliverable | Threading & Revision Coupling | Mandatory Verification (`Exhibits/`) |
| :---: | :--- | :--- | :--- |
| **M0** | **`Experimental/VectorFieldVisualizer/`** | GPU WebGL2 GLSL3 prototype (`VectorFieldShaders`, `VectorFieldGPUViewport` 3D/Split/2D, 4,096 FBO particles) + Project Zero 3-dock UI + Wind Anemometer | `node Experimental/VectorFieldVisualizer/VectorFieldValidation.mjs` |
| **M1** | `Engine/SimulationSpace/ForceFields/` (`VectorField`, `ChannelField`, `TurbulenceField`, `ForceFieldAccumulator`) | `ExecutionCluster` parallel tile evaluation + SIMD SoA batching | `VectorFieldValidation.cpp` verifying all 19 laws, finite-bound zero leakage, `Δv` impulse balance & `|∇·v| < ε` |
| **M2** | `FieldLatticeCascade` + `FieldLatticeCompute.slang` + `.SimulationSpace` `SimulationCodec` | `WorkspaceThread` async edits + `OperationRecorder` (`215 / 750 MiB`) | `WorkspaceRevisionValidation` + cascade resolution convergence check |
| **M3** | `VectorFieldVisualizer` + `StreamlineVisualizer` + 4 `.slang` GPU rasterization shaders | Lock-free triple-buffered lattice upload via `VulkanInterchange` | CPU + SwiftShader `.png` snapshots rendered to `Exhibits/Images/VectorField/` |
| **M4** | `VectorFieldEditor` + `ForceOutlinerPanel` + `AnemometerTelemetryPanel` (`FRONTIER_PROFILE_EDITOR`) | Docked in `WorkspaceDockContainer` using `Engine/Components/Panes/` | Full 3-profile CMake build (`Editor`, `Development`, `Shipping`) verifying zero `Editor/` linkage in `Shipping` |

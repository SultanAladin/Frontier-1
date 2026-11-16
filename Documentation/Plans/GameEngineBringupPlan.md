# Game Engine & Hybrid SDF + ReSTIR Vulkan 1.3 Bringup Plan

> **TL;DR:** **Yes — we are ready to start the Game Engine now**, targeting **Vulkan 1.3**, the **Right-Hand Coordinate Rule** (`+X` Forward · `+Y` Left · `+Z` Up, $\hat{\mathbf{X}}\times\hat{\mathbf{Y}}=+\hat{\mathbf{Z}}$, Reverse-Z `[1 -> 0]`, codified in `Engine/Constants/EngineConstants.h`), and a clean C++20 + `.slang` translation of `unassignedinbox/Frontier-OLD`'s **`VehicleHMI` Hybrid Global SDF + Raytracing ReSTIR + Atmosphere/Sky/Volumetric Clouds renderer**. We start with the thin **`MicroUtils` + `Mathematics` + `MetricSpace`** spine (**Step 3A**), bring up **`WorldSpace` + `GameThread` + `JoltPhysics`** (**Step 3B**), and port the **3-Tier Vulkan 1.3 Hybrid SDF + ReSTIR Renderer** (**Step 3C**: RTX dedicated `VK_KHR_ray_tracing_pipeline`, inline `VK_KHR_ray_query`, and GTX Hi-Z + Global SDF + 4-Wide `CWBVH`), fixing all 3 vehicle driving ghosting bugs and enforcing a strict **`1–5%` pixel-delta quality gate (zero checkerboard subsampling)**. See [VehicleHmiRendererReflectionReport.md](VehicleHmiRendererReflectionReport.md) for the full GTX SDF vs. BVH reflection report and root-cause audit.

---

## 1. 🔍 Locked Core Engine & Renderer Mandates

| Invariant | Canonical Specification | Code / Governance Anchor |
| :--- | :--- | :--- |
| **Graphics & Compute API** | **Vulkan 1.3** (`VulkanTargetMajorVersion = 1`, `VulkanTargetMinorVersion = 3`, Reverse-Z `[1.0 -> 0.0]`) | `Engine/Constants/EngineConstants.h` · `Engine/GeometricRaster/VulkanInterchange` |
| **Coordinate Orientation** | **Right-Hand Rule** (`RightHandedCoordinateRule = true`, `+X` Forward, `+Y` Left, `+Z` Up; $\hat{\mathbf{X}} \times \hat{\mathbf{Y}} = +\hat{\mathbf{Z}}$ — matches `VehicleHMI` ground `+Z`) | `Engine/Constants/EngineConstants.h` · `Engine/MetricSpace/CoordinateSpace.h` |
| **Renderer Architecture** | **`VehicleHMI` Hybrid 4-Cascade Toroidal Global SDF (`SceneClip0..3`) + ReSTIR GI + Reflections + Sky/Atmosphere/Volumetric Clouds/Celestial** | Clean C++20 + `.slang` translation (zero banned words: no `Occupant`, `Family`, `Card`, `Pass`, `Frame`, `State`) |
| **RTX Path (`VK_KHR_ray_tracing_pipeline`)** | Dedicated hardware RT pipelines + SER + GPU `VkAccelerationStructureKHR` TLAS update (`5×–10×` faster than `Frontier-OLD` software BVH) + inline `VK_KHR_ray_query` for short shadow/visibility rays | `Engine/GeometricRaster/` · `Engine/Shaders/` |
| **GTX Path (Non-RT Vulkan 1.3)** | **Hi-Z (`0–1.5 m`) + Global SDF (`SceneClip0..3`) + Surface Radiosity** for reflections (`~1.35 ms`, `6×–10×` faster than compute BVH) + Subgroup 4-Wide `CWBVH` (`16`-entry shared-mem stack, zero VGPR spill) | [VehicleHmiRendererReflectionReport.md](VehicleHmiRendererReflectionReport.md) §1–§2 |
| **Driving Anti-Ghosting** | ① Virtual hit-distance reflection reprojection + `3×3` YCoCg box clamp; ② `PreviousVehicleProfile` wheel-spin/steering motion vectors; ③ Velocity-gated `100%` reservoir visibility validation | [VehicleHmiRendererReflectionReport.md](VehicleHmiRendererReflectionReport.md) §3 |
| **Non-Degrading Speedup Gate** | **`<= 1–5%` pixel delta (`MaximumOptimizationPixelDelta = 0.05f`)** verified via before/after `.png` `Exhibits/` diff — **checkerboard / interleaved subsampling strictly banned** | `Engine/Constants/EngineConstants.h` · `Documentation/ImportantNotes.md` |

---

## 2. 🧩 Prerequisite Audit & Dependency Sequence

```text
+--------------------------------------------------------------------------------------------------+
| ALREADY GREEN (Phases 0–2): Input · Windowing · RuntimeLifecycle · ExecutionChannels · .Space UI |
|   + EngineConstants.h (RightHandedCoordinateRule, Vulkan 1.3, Reverse-Z, 5% Pixel-Delta Gate)    |
+-------------------------------------------------+------------------------------------------------+
                                                  |
                                                  v
+==================================================================================================+
| STEP 3A — RIGHT-HANDED SPATIAL & TOKEN PREREQUISITE SPINE (Build First)                          |
|   • Engine/MicroUtils/    : TokenIssuer · TokenAuthentication · IndexedSlotRecycler · RegionAlloc|
|   • Engine/Mathematics/   : Right-handed LinearAlgebra (Vec2/3/4, Mat3/4) · Quaternions · Random |
|   • Engine/MetricSpace/   : Right-handed CoordinateSpace (+X Fwd, +Y Left, +Z Up) · Reverse-Z    |
|   • Engine/SpatialAcceleration/ : 4-Wide Compressed CWBVH (64 B nodes, zero-spill traversal)     |
+=================================================+================================================+
                                                  |
                        +-------------------------+-------------------------+
                        v                                                   v
+===============================================+   +==============================================+
| STEP 3B — WORLDSPACE, GAMETHREAD & VEHICLE    |   | STEP 3C — VULKAN 1.3 SDF + RESTIR RENDERER   |
|   • Contiguous 32 B Instances (zero virtuals) |   |   • ShaderCompiler (slangc) + Vulkan 1.3     |
|   • AnalyticPrimitives + .GeometrySpace/.Sdf  |   |   • Tier 1 RTX: VK_KHR_ray_tracing_pipeline  |
|   • .WorldSpace Codec + Outliner/Inspector    |   |   • Tier 2 Inline: VK_KHR_ray_query          |
|   • GameThread + PhysicsThread (Jolt v5.6.0)  |   |   • Tier 3 GTX: Hi-Z + Toroidal Global SDF   |
|   • ControlVehicle + PreviousVehicleProfile   |   |     + Subgroup 4-Wide CWBVH                  |
|   • Viewport Edit / Simulate / Play-Pause     |   |   • Sky + Volumetric Clouds + Anti-Ghosting  |
+=======================+=======================+   +======================+=======================+
                        |                                                  |
                        +-------------------------+------------------------+
                                                  v
+==================================================================================================+
| STEP 3D — PLAYABLE VEHICLEHMI PROVING GROUND (60–120+ FPS · Zero Ghosting · <= 1–5% Pixel Gate)  |
|   • ControlVehicle + ProvingGround + HMI Cabin Area Lights + Clouds/Sky in SampleProject         |
|   • Automated Before/After PNG RMSE & 95th-Percentile Pixel-Delta Verification in Exhibits/      |
+==================================================================================================+
```

---

## 3. 🏆 Next-System Decision Table

*(Legend — Unblocks Gameplay & Renderer / Architectural Cleanliness / Immediate Visual Proof: 🔴 = highest/best, 🚩 = moderate, ✔️ = lowest; Risk: ✔️ = lowest/best)*

| Rank | Next-System Track | Unblocks Engine & Renderer | Architectural Cleanliness | Immediate Visual Proof | Risk | Summary |
| :---: | :--- | :---: | :---: | :---: | :---: | :--- |
| 🏆 🥇 | **Option A — Start Game Engine Spine (`Step 3A` Right-Handed Math/Tokens/CWBVH → `Step 3B` World/GameThread → `Step 3C` Vulkan 1.3 SDF + ReSTIR)** | 🔴 | 🔴 | 🔴 | ✔️ | Builds the right-handed `MicroUtils` + `Mathematics` + `MetricSpace` + 4-wide `CWBVH` foundation first, then stands up `WorldSpace` + `GameThread` and translates the `VehicleHMI` Vulkan 1.3 SDF + ReSTIR renderer with RTX/RayQuery/GTX tiers and anti-ghosting fixes. |
| 🥈 | **Option B — Right-Handed Math + Vulkan 1.3 SDF + ReSTIR Renderer First (`Step 3A` → `Step 3C` → `Step 3B`)** | 🚩 | 🔴 | 🔴 | 🚩 | Brings up `Step 3A` and immediately ports the Vulkan 1.3 `VehicleHMI` renderer (`SceneClip0..3` + ReSTIR + Sky/Clouds) on static proving-ground scenes before `GameThread` and `JoltPhysics` vehicle driving. |
| 🥉 | **Option C — Standalone Browser WebGL/WebGPU Reflection Micro-Demo First** | ✔️ | ✔️ | 🚩 | ✔️ | Skipped in favor of the direct shader & hardware report ([VehicleHmiRendererReflectionReport.md](VehicleHmiRendererReflectionReport.md)) so zero turns are wasted on non-Vulkan browser code; WebGPU also cannot test native Vulkan 1.3 subgroups, `VK_KHR_ray_query`, or RTX `VK_KHR_ray_tracing_pipeline`. |

💡 **Recommendation:** **Option A (`Step 3A` → `Step 3B` → `Step 3C` → `Step 3D`)** *(or **Option B** if you want to see the Vulkan 1.3 `VehicleHMI` SDF + ReSTIR + Clouds viewport render even before `GameThread`/`JoltPhysics`!)*. Both start with the exact same **Step 3A** (`MicroUtils` + Right-Handed `Mathematics` + Reverse-Z `MetricSpace` + 4-Wide `CWBVH`).

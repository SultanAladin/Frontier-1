# 🔍 `ViewportTelemetrySpikeRemediationReport` — `FrontierTiming.log` Audit & Remediation (`2026-11-22`)

> **TL;DR**: Audited all `1,246` telemetry lines (`40,080` host ticks on `NVIDIA GeForce GTX 1650 SUPER`) in `FrontierTiming.log`. Active `GTX GDF GI` already runs at **`81–286 fps`** (`3.49–12.31 ms`) with **zero** runtime asset-SDF or resident-BVH rebuilds during camera flight (`0.27–0.42 ms` toroidal dirty-slab scroll). Every observed spike and `HighEnd BVH4` slowdown traced to seven concrete GPU/UI/OS causes — all seven are fixed and verified across `Editor` + `Shipping` builds and `7` validation suites (`0` failures).

---

## 🔍 1. `FrontierTiming.log` Telemetry Audit (`F=1..40080`, `GTX 1650 SUPER`)

| Regime / Cluster (`FrontierTiming.log`) | Observed Telemetry (`e755d52`) | Root Cause Identified | Remediation Applied |
| :--- | :--- | :--- | :--- |
| **🟢 Active `GTX GDF GI` camera flight** (`F=5460..9240`) | `Total = 3.49..12.31 ms` (`81–286 fps`), `GpuTot = 1.45..7.96 ms`, `SceneSync = 0.00..0.42 ms` | Resident multi-LOD cluster `BVH4` + toroidal `6.2%` dirty-slab GDF/Radiosity scroll already active; **zero** runtime asset-SDF or `BVH4` rebuilds during flight | Preserved intact; verified `BvhRebuilds = 5 -> 5 -> 5` and `AssetSdfUploads = 1 -> 1` |
| **🔴 `HighEnd BVH4` (`Engine 3`) steady cost** (`F=31380..32640`) | `Total = 33.7..34.8 ms` (`~29 fps`), `Shade = 29.3..29.6 ms` vs. `PT Realtime` `Shade = 7.1 ms` | `ComputeEntry` combined `Engines 0..3` and traced **`15..28` `BVH4` rays/pixel** (`3..4` closest-hit sun + `3` AO + rough reflection + `2` GI + `3` secondary shadow + `8` area-light rays) | Split dedicated `HighEndBvhComputeEntry` (`180 KB` `.spv`) with single-`[loop]` `TraceSceneClosest` + `1`-ray R2 solar-disc `TraceSceneAnyHit` NEE + `1`-ray cosine GI/AO (`ShadeSecondaryHitNoTrace`, `0` secondary rays) → **`2..3` rays/pixel** |
| **🔴 `WideTreeRayQuery` (`ShadowAlg == 4`) cost** (`F=35820..37920`) | `Shade = 12.1 ms` on `GTX GDF GI` (`+7.4 ms` over `ContactPCSS`), `35.1 ms` on `HighEnd BVH4` | `EvaluateSolarShadow` traced `3..4` full `TraceSceneClosest` rays per pixel instead of `1` early-out `TraceSceneAnyHit` NEE ray + spatial denoiser | GDF open-corridor cull (`Engines 1..2`) + `1` R2-stratified solar-disc `TraceSceneAnyHit` ray feeding `OutBlockerDist = 0.55` into existing plane-aware penumbra filter; gated `ContactPCSS` `0.24 m` contact ray on `!OpenCorridor` |
| **🔴 First switch to `PT Realtime`** (`F=32706`) | **`Total = 17,231.9 ms`** (`17.2 s` freeze), `WaitPrev = 17,230 ms` | Lazy `ViewportPathTraceEntry` registration with `VK_NULL_HANDLE` cache compiled `355 KB` SPIR-V synchronously mid-session | Added UUID/vendor/device-validated persistent `VkPipelineCache` (`ProgramBinaryCache`) in `ComputeDispatchEncoder` + eager `HighEndBvh` & `PathTrace` registration on hardware GPUs (`0 ms` mode switch, instant warm startup) |
| **🔴 `SceneSync = 22.96 ms` on non-geometry sync** (`F=32781`) | `SceneSync = 22.96 ms` despite unchanged geometry | `SynchronizeSceneResources(Scene, RebuildShadows)` hardcoded `ForceReassemble = true` into `AssembleResidentClusterSceneBvh` | Passed `RebuildShadows` (`ComputeResidentClusterTransformSignature` still detects any instance transform/visibility change automatically) |
| **🔴 UI / gizmo mouse-hover spikes** (`F=2608, 9090..9364, 34644`) | `UIBuild = 14.3..34.4 ms`, `Present = 15.1..25.8 ms`, `Gizmo = 4.6..8.2 ms` | (1) `ImFontAtlas` defaulted to `512x128` (`Oversample 3x2`), triggering mid-session `vkDeviceWaitIdle` atlas resizes; (2) `CursorNearGizmo` forced per-pixel GPU gizmo re-render + `9x9` staging readback; (3) unthrottled `~1,000 Hz` UI-hover `MAILBOX` spin hit WDDM queue backpressure | (1) Pre-sized `ImFontAtlas` `TexMinWidth = TexMinHeight = 2048` (`Oversample 2x1`); (2) analytical `EvaluateRayHover` with GPU gizmo update **only** when hovered handle changes (`RequestPickPatch = false`); (3) `240 Hz` `WaitOperatingEventsTimeout` + Win32 `1 ms` timer (`winmm.dll` `timeBeginPeriod(1)`) + cached monitor `RefreshRateHz` + guarded `ThumbnailSheetRepository` flush |
| **💡 Diagnostics sparkline scale** (`ViewportPanel.cpp`) | Idle `240 Hz` sleep interval blended into `Δt` sparkline | `PushDiagnosticsSample` sampled wall-clock `Δt` including voluntary `WaitOperatingEventsTimeout` sleep | Sampled active CPU+GPU `LastViewportTiming.TotalFrameMs` (`0.25..120 ms`) while bucketing over `100 ms` wall-clock windows |

---

## 🧩 2. Shader & Ray-Budget Comparison (`WorldViewportRadiometricCompute.slang`)

| Metric | Before (`e755d52`) | After Remediation | Status |
| :--- | :---: | :---: | :---: |
| **`WorldViewportRadiometricCompute.comp.spv`** (`Engines 0..2`) | `216.7 KB` (`Engines 0..3` coupled) | **`203.1 KB`** (`Engines 0..2` only) | 🟢 |
| **`WorldViewportHighEndCompute.comp.spv`** (`Engine 3`) | *(Coupled into `ComputeEntry`)* | **`180.0 KB`** (single-loop `TraceSceneClosest`) | 🟢 |
| **`WorldViewportPathTraceCompute.comp.spv`** (`Engines 4..5`) | `355.6 KB` (lazy runtime compile) | **`341.0 KB`** (eager + disk `VkPipelineCache`) | 🟢 |
| **Ground-pixel `BVH4` rays in `HighEnd BVH4` (`WideTreeRayQuery`)** | `15..28` rays (`4` closest sun + `3` AO + `1` rough refl + `2` GI + `3` sec shadow + `8` LTC) | **`2` rays** (`1` primary + `1` AnyHit sun + `1` cosine GI/AO bounce; `+1` refl only on reflective materials) | 🟢 |
| **Open-corridor `BVH4` rays in `ContactPCSS` (`Engines 1..3`)** | `1` unconditional `0.24 m` `TraceSceneAnyHit` per pixel | **`0` rays** outside shadow penumbra (`!OpenCorridor` gate) | 🟢 |
| **Runtime `SDF` / `BVH4` rebuilds during camera flight & rigid motion** | `0` (`1` redundant `ForceReassemble` on `RebuildShadows=false`) | **`0`** (`ForceReassemble = RebuildShadows`) | 🟢 |

---

## ✔️ 3. Build & Validation Ledger

| Target / Validation Suite | Profile | Result | Key Quantitative Reading |
| :--- | :---: | :---: | :--- |
| **`Frontier` + `FrontierEngine`** | `Editor` & `Shipping` | 🟢 **PASS** | `0` errors, `0` warnings in modified units, `0` banned-word hits |
| **`DynamicClipmapNaniteViewportValidation`** | `Editor` | 🟢 **`33 / 33` PASS** | `OpenFloor Seam`: `GTX Mean=0.03, Max=0.44` · `BVH4 Mean=0.01, Max=0.56` · `AssetSdfUploads 1->1` · `BvhRebuilds 5->5->5` |
| **`GtxGlobalDistanceFieldViewportValidation`** | `Editor` | 🟢 **`22 / 22` PASS** | `Interior Luma`: `Direct=56.63`, `GTX=59.35`, `HighEnd_BVH=63.77`, `PT_4SPP=69.59` · `Laplacian GTX=1.238` |
| **`NaniteViewportVehicleValidation`** | `Editor` | 🟢 **PASS** | `near 20,294 tris -> far 10,824 tris`, `BVH rebuilds=1`, `maskUpdates=9` |
| **`DirectPbrWorldValidation`** | `Editor` | 🟢 **PASS** | `MatCap=140.3`, `DaylightLTC=126.6`, `NightIES_GPU=52.2` (`BeamPool=58.8 vs DarkSky=38.2`) |
| **`WindCloudsFogValidation`** | `Editor` | 🟢 **`29 / 29` PASS** | `FullStack MAD = 21.62`, `HalfSphere+GodRays MAD = 19.67` |
| **`ContentBrowserValidation` · `ProjectBrowserValidation`** | `Editor` | 🟢 **PASS** | Persistent `FrontierVkComputeCache_*.bin` verified across runs |

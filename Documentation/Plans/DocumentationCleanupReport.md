# 🧩 Documentation Cleanup & Redo Baseline Alignment Report

> **TL;DR**: Synchronized branch `arena/60be9d3e-frontier` to the post-removal working tree (`SultanAladin/Frontier@arena/8391e994-frontier`, `940a125`), stripped obsolete GI/Fluid/ForceField/Particle bloat and broken links across `CLAUDE.md` and `Documentation/`, indexed `Experimental/RemovedSdfGi/` as the priority SDF/GI redo baseline, and promoted the **Automatic Pre-Code HTML Screenshot & Zero-Simplification Gate** to **`CLAUDE.md` §2.1**.

---

## 🏷️ 1. Documentation Cleanup Ledger

| File | Cleanup & Alignment Applied | Status |
| :--- | :--- | :---: |
| [`../../CLAUDE.md`](../../CLAUDE.md) | Removed broken `FluidWorkspace`, `ParticleWorkspace`, `ForceFieldWorkspace`, `12 Launchers`, and `Exhibits/DeterministicPathTracer` links; deleted obsolete Day-1 §3 bringup checklist and 50-line §4 Stage-1 GI (`DynamicProbeCache` / `ReservoirPathReplay` / `DdgiRaytracing` / `SdfGlobalIllumination`) dump; added **§1.2 Queued Redo Domains** (`Experimental/RemovedSdfGi/` + `Fluid` / `ForceField` / `Particle`), **🔴 §2.1 Automatic HTML-to-C++ Parity, Pre-Code Screenshot & Zero-Simplification Gate**, and factual **§3 Active Direct World Rendering & Atmosphere Baseline**. | 🟢 |
| [`../../README.md`](../../README.md) | Corrected `12` → `9` registered workspaces/launchers, linked `Experimental/RemovedSdfGi/README.md`, fixed `ColorSpace/` → `ColourSpace/`, and aligned the `Engine/` table to all 22 actual subdirectories. | 🟢 |
| [`../README.md`](../README.md) | Removed broken `Fluid`, `Particle`, `ForceField`, and `Exhibits/DeterministicPathTracer` links; added `Active World Renderer & Atmosphere` and `SDF / GI Redo Baseline` (`Experimental/RemovedSdfGi/README.md`). | 🟢 |
| [`../ImportantNotes.md`](../ImportantNotes.md) | Updated §1 to match the active dual-engine (`MatCap` & `OpenPbrDirect`) baseline and reinforced §6 with the unprompted Step 0 HTML screenshot, zero-simplification, and honest-rendering mandate. | 🟢 |
| [`../Tracker.md`](../Tracker.md) · [`../Backlog.md`](../Backlog.md) | Fixed Phase 5 (`9 Authoring Workspaces`), removed false `BKL-008` claim that `Fluid`/`Particle`/`ForceField` are landed, cleaned stale `BKL-011` references, and added **`BKL-012` (Priority SDF/GI Redo)** + **`BKL-013` (Un-Simplified Fluid/ForceField/Particle Redo)**. | 🟢 |
| [`../FolderStructure.md`](../FolderStructure.md) | Repaired spliced `GeometricRaster/` ↔ `Shaders/` tree corruption, removed duplicate `SpatialAcceleration/`, `Shaders/`, and `AudioSpace/` blocks, fixed zero-`And` filenames (`PorosityScattering.h`, `BlackbodyEmission.h`, `SubsurfaceTransmission.slang`), updated `Experimental/` entries (`RemovedSdfGi/`, `SdfLightingDemoCpp/`, `DeterministicPathTracer/`, `FrontierWorldMirror/`), and deleted orphaned empty `Engine/Editor/Workspaces/`. | 🟢 |
| [`../../Engine/README.md`](../../Engine/README.md) | Rewrote stale/truncated module summaries across all 22 `Engine/` directories and updated workspace/CTest counts (`9` workspaces, `58` CTests). | 🟢 |
| [`../../Experimental/README.md`](../../Experimental/README.md) | Removed deleted `FluidEditor/`, `ParticleEditor/`, `ForceFieldEditor/`, `ForceFieldVisualization/` rows and failing `node` commands; indexed all active reference studios and lighting/SDF redo baselines; repaired broken relative links in `DeterministicPathTracer`, `DeterministicRaytracingLab`, `FrontierEditor`, `UI_COMPONENT_GUIDELINES`, and `VehicleLab`. | 🟢 |
| [`README.md`](README.md) & Superseded Plans | Partitioned `ParticleSpace*`, `VectorField*`, `WorldRenderEngineUnificationPlan`, and `RenderingRaytracingArchitecturePlan` into **§3 Superseded Plans — Queued for Clean 1:1 Ground-Up Redo** with explicit top-of-file warning banners; updated `Automation/BuildMarkdownViewer.py` preferred filenames and rebuilt `MarkdownStudioViewer.html`. | 🟢 |

---

## 🧩 2. Removed Domains & Ground-Up Redo Queue

| Removed Domain | Removal Commit | Why Previous C++ Code Was Stripped | Preserved Baseline & Redo Gate |
| :--- | :---: | :--- | :--- |
| **SDF Shadows · SDF GI · Emissive GI · Deterministic Path Tracer** *(Priority 1 — `BKL-012`)* | `63db120` → preserved in `940a125` | World renderer simplified to clean direct baseline (`MatCap` & `OpenPbrDirect`) so SDF/GI and emissive lighting can be re-integrated cleanly without legacy drift or banned vocabulary. | **[`Experimental/RemovedSdfGi/`](../../Experimental/RemovedSdfGi/README.md)** (`Engine/DistanceFieldSpace/`, `VoxelSdfClipmap`, `DeterministicIntegrators`, `ControlVehicleSurfaceLoader`, 5 `Exhibits/*Validation` suites, `DeterministicPathTracer` results, 16+ proof `.png` sheets, `Restore.py`) + [`Experimental/SdfLightingDemoCpp/`](../../Experimental/SdfLightingDemoCpp/). |
| **`Particle`** *(`BKL-013`)* | `daed305` | Substituted simplified billboard sprites instead of the exact HTML GPU compute / strand / ribbon / surface pipeline, invented non-matching UI, misrepresented rendering, and regressed shared engine components. | Deferred; requires unprompted Step 0 HTML `.png` screenshots, 1:1 `Engine/Components/` UI parity, and 1:1 solver/shader fidelity (`CLAUDE.md` §2.1). |
| **`Fluid`** *(`BKL-013`)* | `daed305` | Simplified approximation that fell short of full gaseous/combustion/flipbook prototype fidelity. | Deferred; ground-up 1:1 redo under `CLAUDE.md` §2.1 when scheduled. |
| **`ForceField` / `VectorField`** *(`BKL-013`)* | `daed305` | Simplified couplings retired alongside `Particle` and `Fluid` for clean ground-up re-architecture. | Deferred; ground-up 1:1 redo under `CLAUDE.md` §2.1 and `BannedWordsVocabulary.md` §3.1 (`ForceModel`, zero `Law`). |

---

## 🔴 3. Enforced Automatic HTML-to-C++ Fidelity Gate (`CLAUDE.md` §2.1)

1. **① Automatic Step 0 HTML `.png` Capture**: Serve the HTML reference locally, capture browser `.png` screenshots across every tab, drawer, inspector section, and viewport mode, and inspect them via `read_file` **before** writing a single line of C++ or `.slang` — never wait to be asked.
2. **② Zero Reinvented UI**: Compose strictly from `Engine/Components/` to match the HTML layout, proportions, icons, and controls 1:1.
3. **③ Zero Simplified Solvers & Zero Rendering Lies**: Port the exact math, physics, and GPU shaders of the HTML specification — never substitute simplified sprites/fallbacks and never misrepresent what pixels are actually rendering.
4. **④ Zero Collateral Component Breakage**: Keep shared `Engine/Components/` and `Engine/GeometricRaster/` contracts intact.
5. **⑤ Side-by-Side Offscreen `.png` Proof**: Verify C++ `--capture-png` / `Exhibits/*Validation` renders directly against the Step 0 HTML `.png` screenshots via `read_file`.

---

## ✔️ 4. Verification Scoreboard

| Check | Result |
| :--- | :--- |
| Working tree parity with `SultanAladin/Frontier@arena/8391e994-frontier` (`Engine/`, `Experimental/RemovedSdfGi/`, `Exhibits/`) | 🟢 Verified (`100%` match + doc cleanup) |
| Broken relative Markdown links across all active `.md` documents | 🟢 **`0` broken links** |
| Stale active-workspace claims for `Fluid`, `Particle`, `ForceField`, or deleted Stage-1 GI symbols | 🟢 **`0` stale claims** |
| `SKILL-Emoji.md` & Zero-`And` filename compliance across modified/added docs | 🟢 **`0` violations** |
| `Documentation/Plans/MarkdownStudioViewer.html` rebuilt & live on `0.0.0.0:8080` | 🟢 Verified |

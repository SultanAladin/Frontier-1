# Geometry Compiler Studio — Solver, Sparse SDF & Nanite Evaluation Findings Report

- **Branch**: `arena/6eeea758-frontier`
- **Date**: 2026-11-14
- **Workspace**: `Experimental/GeometryCompiler/` (+ `Experimental/TextureStudio/` baking/icon upgrades)
- **Validation Status**:
  - `node Experimental/GeometryCompiler/GeometryCompilerValidation.mjs` — **14/14 suites PASS**
  - `node Experimental/TextureStudio/TextureStudioValidation.mjs` — **28/28 suites PASS**

---

## 1. Executive Summary & Architectural Decision Status

The **4-Way WebGPU Geometry Compiler Studio** (`Experimental/GeometryCompiler/`) was built to empirically benchmark four mesh-reduction and volumetric geometry pipelines side by side before committing production C++20 code:

| Pipeline | Studio Role | Production Status | Verdict |
|---|---|---|---|
| **Original (LOD 0)** | Ground-truth reference | Source input | Reference baseline |
| **Hybrid ★** (Probabilistic QEM + Regional Adaptive Shrinkwrap + Retopo-Seeded Coarse LOD 3) | **Default Production Solver** | **Recommended default** | **Best overall fidelity & safety** — `0` thin-wall breaches on every specimen, `4%–20%` slivers, `26.9°–34.8°` min triangle angle |
| **Nanite-Style Cluster QEM** | Comparison-Only Benchmark | **On hold / Not committed to C++** | **Unresolved trade-off** — suffers thin-wall collapse (`11` breaches on `GlassVessel LOD 3`), high sliver rate (`28%–29%`), and high runtime/DAG complexity; keep experimental until product need is proven |
| **Retopo** (Curvature-Guided Remesh) | Comparison-Only Benchmark | **Internal coarse seeder only** | Too costly standalone for real-time LOD chains, but highly effective when used internally at `seedRatio = 0.28` to seed Hybrid `LOD 3` |
| **Sparse `8×8×8` SDF (`16³–128³`) + 3D Raymarcher** | Volumetric Companion | **Validated & Production-Ready** | `61%–84%` brick pruning at `128³`, exact 3D EDT conservative `Mip1`, interactive WebGPU + Canvas2D 3D visualizer (`Iso+AO`, `3D Cutaway`, `Step Heat`, `Sparse Bricks`) |

### Why Nanite-Style Virtualized Geometry Is Still Uncommitted
1. **Thin-Shell Fragility**: Pure QEM decimation collapses across opposing faces of thin dielectric shells (`GlassVessel` `45 mm` double wall → **11 wall breaches** at `LOD 3` vs **0** for Hybrid).
2. **Mesh Quality Degradation at Coarse LODs**: On sharp hard-surface and organic curves (`ChamferBox`, `TorusKnot`), Nanite-Style QEM reaches `28%–29%` slivers with `25.6°` average minimum angles, degrading shading normals and ray-intersection stability.
3. **Complexity vs. Payoff**: A full cluster-DAG streaming/virtualized micro-polygon pipeline adds substantial GPU culling, cluster-seam locking, and memory-residency complexity. Before adopting Nanite-style streaming in Frontier C++, we need concrete scene-scale evidence that a simpler **Hybrid LOD chain + Sparse SDF / Path Tracing** pipeline cannot already meet target budgets with dramatically lower maintenance overhead.

---

## 2. Quantitative 4-Way Solver Findings (`LOD 3`)

Benchmarks across all 6 built-in specimens comparing **Locked Hybrid Production (`Retopo-Seeded LOD 3`)** against **Old Direct Hybrid** and **Nanite-Style QEM**:

| Specimen (LOD 0 Tris) | Locked Hybrid `LOD 3` (Production Default) | Old Direct Hybrid `LOD 3` | Nanite-Style QEM `LOD 3` (Comparison-Only) | Key Finding |
|---|---:|---:|---:|---|
| **GlassVessel** (`1,656t`) | **203t \| 12.20 / 68.7 mm \| 17% slv \| 31.1° \| 0 breaches** | 236t \| 13.49 / 121.6 mm \| 20% slv \| 0b | 198t \| **11 wall breaches** | Hybrid eliminates all thin-wall breaches and cuts max error by `43%` |
| **UtahTeapot** (`1,640t`) | **249t \| 9.33 / 63.3 mm \| 20% slv \| 26.9° \| 0 breaches** | 262t \| 8.35 / 74.0 mm \| 34% slv \| 0b | 197t \| 9.97 / 65.3 mm \| 20% slv | Cuts slivers from `34%` → `20%` and lowers max error by `14%` |
| **ChamferBox** (`1,200t`) | **144t \| 13.82 / 52.8 mm \| 8% slv \| 33.0° \| 0 breaches** | 144t \| 9.88 / 59.2 mm \| 29% slv \| 27.8° | 144t \| 28% slv \| 25.6° | Slivers crushed `29%` → `8%`; min angle raised `+5.2°` to `+7.4°` |
| **ShaderBall** (`2,688t`) | **323t \| 13.40 / 71.4 mm \| 8% slv \| 34.8° \| 0 breaches** | 14.43 / 82.7 mm \| 12% slv | 14.80 / 102.6 mm \| 14% slv | Strictly dominates both baselines across mean error, max error, and slivers |
| **TorusKnot** (`2,016t`) | **286t \| 14.56 / 76.0 mm \| 6% slv \| 32.1° \| 0 breaches** | 354t \| 11.16 / 76.7 mm \| 14% slv | 242t \| 18.08 / 98.0 mm \| 29% slv | Cuts Nanite slivers `29%` → `6%` and max error `98.0` → `76.0 mm` |
| **UvSphere** (`1,584t`) | **180t \| 8.08 / 46.7 mm \| 4% slv \| 34.7° \| 0 breaches** | 9.96 / 55.0 mm | 10.14 / 59.7 mm | Lowest mean/max error (`-19%` to `-22%`) and `4%` slivers |

### Locked Hybrid Production Invariants (`CompilerSolvers.js`)
1. **Decoupled Probabilistic QEM (`runHybridProbQemStep`)**:
   - Use probabilistic quadric `Q` (noise-regularized) for **optimal vertex placement**, while ranking edge collapses strictly with pure zero-variance geometric quadric **`Qgeom` (`σ = 0`)**. Coupling both to probabilistic `Q` distorted collapse ordering on flat faces.
2. **Regional Curvature Auto-Blend (`computeLocalRegionSharpness`)**:
   - Measure local sharpness via **normal-projected mean curvature** (NOT 3D Laplacian magnitude, which falsely spikes on curved organic surfaces).
3. **Error-Guarded LOD-Adaptive Shrinkwrap (`applyAdaptiveHybridShrinkwrap`)**:
   - Never apply Shrinkwrap in-place before generating downstream LODs (corrupts collapse history).
   - Guard projection strictly against thin-wall cross-snapping: `proj.dist < 0.035`, `nAlign > 0.45`, `useGeomNormals = true`. Strict 100% snap stays opt-in only.
4. **Retopo-Seeded Coarse `LOD 3` (`buildRetopoSeededCoarseLod`)**:
   - Locked constants: `seedRatio = 0.28`, `useCurvatureRelax = true`, `coarseCreaseDeg >= 50`, `anchorLod0 = true`, `lockUvSeams = false`, `minAngleGuardRad = 0.20`, `maxCollapseError = meshDiag * 0.016 * (0.85 | 1.05 | 1.35)`. Relaxing these caused tube stalls, spikes, or chord sag.

---

## 3. Sparse `8×8×8` Narrow-Band SDF & 3D Raymarching Visualizer Findings

### Sparse Brick Architecture (`16³–128³`)
- Narrow-band `8×8×8` brick allocation (`Int16Array(512)` quantized to `[-1, 1] * narrowBandMeters`) with a 3D `Int32Array` indirection grid (`-1` = pruned empty brick) and a single `conservativeMip1` (`dim/2`) level.
- Supports sparse QEF Dual Contouring, `.FVOL` binary serialization (`frontier.fvol.v1`), and live `Mip0` eviction under VRAM caps (default `2048 KB`).

| Specimen (`128³` SDF, `4,096` Total Bricks) | Allocated Bricks | Pruned Empty Space | Dense vs Sparse `Mip0` Size | Bake Time (JS) |
|---|---:|---:|---:|---:|
| **GlassVessel** | `672 / 4,096` | **84% pruned** | `8.00 MB` → **`1.66 MB`** | `722 ms` |
| **ShaderBall** | `1,456 / 4,096` | **64% pruned** | `8.00 MB` → **`2.44 MB`** | `977 ms` |
| **ChamferBox** | `1,592 / 4,096` | **61% pruned** | `8.00 MB` → **`2.58 MB`** | `490 ms` |

### Critical Defect Caught & Resolved via Visual Inspection
Rendering `GlassVessel 64³` (`45 mm` double-walled glass shell, `voxelStep = 25.6 mm`) exposed **two coupled sphere-tracing bugs** that passed pure scalar checks:
1. **Non-Conservative Empty-Brick `Mip1` Fill (`CompilerSolvers.js`)**:
   - Previously, pruned empty bricks copied `coarseBrickCenter` into all `4×4×4 = 64` sub-cells of `conservativeMip1`. Near empty-brick corners, distances could overestimate true surface distance by up to `√3/2 * brickSpan` (`+64 mm` on `GlassVessel 64³`), causing rays to step straight across the `45 mm` glass wall.
   - **Fix**: Implemented an exact **$O(M^3)$ 3-axis Felzenszwalb-Huttenlocher squared-Euclidean Distance Transform (EDT)** directly on `mip1Dim³` (`< 2 ms` at `64³`), seeded at surface-touching narrow-band cells, subtracting `1.15 * mip1Step` as a strict lower-bound margin. This guarantees `sampleMip1(p) <= d_true(p)` across all 3D space (including hollow glass cavities) and produces smooth concentric isolines on the 3D Cutaway plane.
2. **Unguarded Over-Relaxation & Brick Wireframe Edges (`WebGpuViewport.js`)**:
   - Replaced unguarded `omega = 1.18` stepping with strictly conservative relaxation (`0.96` in `Mip1` empty space, `0.78` in `Mip0` narrow band, handoff at `0.85 * narrowBandMeters`).
   - Switched `Sparse Bricks` DDA overlay edge detection from `min(dx, dy, dz)` (which lit entire brick faces) to second-smallest distance `e2 = min(max(dx, dy), max(dy, dz), max(dx, dz))` so only the 12 true 3D brick wireframe edges illuminate.
   - Added a permanent zero-tunneling front-wall coverage regression assertion (`GlassVessel 64³`, `0` dark punctured pixels) to Suite 14.

### 3D SDF Visualizer Capabilities (`SDF 3D` Viewport Tab)
- Dual backend: **WebGPU (`SDF_RAYMARCH_WGSL`)** fullscreen GPU shader + **Canvas2D CPU fallback** with exact mode parity.
- **4 Interactive Modes**: `Iso + AO` (tetrahedral normals + Mip1 AO + Fresnel + `±40 mm` iso-offset), `3D Cutaway` (X/Y/Z axis, flip, auto-sweep, solid cross-section cap, translucent exterior isolines), `Step Heat` (per-ray iteration cost), and `Sparse Bricks` (3D DDA wireframe lattice).
- **Single-Threaded CPU Fallback Timings (`128³`, `220×165`; WebGPU runs native fullscreen)**:
  - `GlassVessel`: Iso+AO `84.0 ms` · 3D Cutaway `24.2 ms` · Step Heat `27.8 ms` · Sparse Bricks `45.0 ms`
  - `ShaderBall`: Iso+AO `60.5 ms` · 3D Cutaway `61.2 ms` · Step Heat `53.6 ms` · Sparse Bricks `71.9 ms`
  - `ChamferBox`: Iso+AO `68.1 ms` · 3D Cutaway `26.2 ms` · Step Heat `59.4 ms` · Sparse Bricks `108.4 ms`

---

## 4. Supporting TextureStudio Upgrades Included in This Commit
- Added `Experimental/TextureStudio/GeometryIconBaker.js` for transparent `128×128` 3D specimen thumbnail baking (`alpha = 0` exterior).
- Upgraded `BakingWorkspace.js`, `BakingEngine.js`, `AssetBrowser.js`, and `Serve.py` to the unified 2-tier Baking header and validated with `TextureStudioValidation.mjs` (**28/28 PASS**).

---

## 5. Open Next Decisions
- **Nanite Feature Decision**: Keep Nanite-Style QEM strictly comparison-only in `Experimental/GeometryCompiler/`. Do not port cluster-DAG streaming into C++ until real-time Path Tracer integration benchmarks clarify total scene triangle + BVH/SDF budgets.
- **Next Milestone**: Merge and audit the real-time Path Tracer branch (`eosclient0001-rgb/Frontier` @ `arena/7afbeba9-frontier`) for strict determinism and GTX-class `30–60 FPS` optimization without quality loss.

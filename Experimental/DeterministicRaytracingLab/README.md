# Frontier — 100% Deterministic WebGPU Full Raytracing Lab (`:8097`)

This laboratory implements **Tier 2 (`Full Deterministic Raytracing`)** from [`Documentation/Plans/RenderingAndRaytracingArchitecturePlan.md`](../../Documentation/Plans/RenderingAndRaytracingArchitecturePlan.md), keeping [`Experimental/GtxReflectionBenchmark/`](../GtxReflectionBenchmark/) (`:8096`) frozen as-is for A/B reference.

## Architectural Guarantees
- **Zero Blind Monte Carlo RNG**: `0` calls to `rand()`, PCG, or stochastic hemisphere cosine sampling anywhere in the shader pipeline.
- **Zero Russian Roulette**: Multi-bounce specular & glass termination is 100% deterministic (`bounce <= maxSpecBounces` and `max(throughput.rgb) >= 0.025`).
- **4-Wide SIMD `BVH4` Storage Buffers (`"FRT1"`)**:
  - Collapses `5,930` binary SAH BVH nodes down to **`1,444` 4-wide `BVH4` nodes (`4.11×` reduction)** across `9,240` unique triangles (`25,920` instanced triangles in Right-Handed `+X Forward, +Y Left, +Z Up`).
  - Asset payload `Assets/DeterministicRtAssets.bin` is **`759 KB`** (`11.2×` smaller than the `8.5 MB` 3D SDF clipmap bundle), averaging **`10.00` `BVH4` steps/pixel** on primary camera rays.

## 5-Stage WGSL `@compute` Pipeline
1. **`DDGI_PROBE_UPDATE_WGSL` (`@workgroup_size(64, 1, 1)`)**:
   - `20 × 16 × 6 = 1,920` world-space octahedral irradiance + Chebyshev `[mean, mean²]` visibility probes.
   - Each workgroup traces `64` deterministic **Spherical-Fibonacci lattice** rays in parallel, evaluates direct sun + analytic `LTC` portals + previous-bounce `DDGI` irradiance (`1B / 2B / 3B / ∞`), shares results in `var<workgroup>`, and integrates `6 × 6` octahedral texels with zero noise and zero light leaks.
2. **`PRIMARY_AND_SHADOW_WGSL` (`@workgroup_size(8, 8, 1)`)**:
   - Traces full-resolution primary camera rays directly through `TLAS4 + BLAS4` (no raster G-buffer pass), followed by a deterministic **Vogel-Spiral Solar Frustum** (`1–12` rays) and **4-Ray `BVH4` Hemisphere Contact AO** (`0–0.55 m`, zero screen-space horizon rings).
3. **`BLOCKER_DILATE_WGSL` + `SHADOW_AO_FILTER_WGSL` (`steps = 1, 2, 4`)**:
   - Coplanar blocker dilation + continuous-sigma plane-aware Gaussian integration producing smooth contact-hardening penumbrae from razor-sharp tyre contact (`0 m`) to wide aerial shadows.
4. **`SPECULAR_AND_GLASS_WGSL` (`@workgroup_size(8, 8, 1)`)**:
   - **Cabin Glass (`mat 7`)**: Deterministic Fresnel reflection + Snell's-law (`IOR = 1.46`) cockpit refraction revealing the 3D bucket seats & interior trim.
   - **Clearcoat / Mirrors / Satin Floor**: `1–4` deterministic **Cone-Footprint** bounces (`coneRadius += hitT * alpha`) smoothly pre-integrating into `DDGI` irradiance at wide cone footprints.
5. **`COMPOSITE_WGSL` + `PRESENT_WGSL`**:
   - Combines Direct Sun + **Analytic `LTC` Overhead Showroom Ribbon Portals** + Trilinear Chebyshev `DDGI` + Contact AO + Specular/Glass, optional **3D `DDGI` Probe Spheres**, and **`BVH4` Traversal Heatmap**.

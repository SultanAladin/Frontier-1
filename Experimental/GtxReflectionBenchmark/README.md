# GTX Global SDF vs. Compute BVH — GI, Soft Penumbra, Color Bleed & Multi-Bounce Reflections (`ControlVehicle`)

Interactive WebGL2 GPU benchmark comparing **Global Distance Field (`sampler3D` Clipmap)** against **Two-Level SAH Compute BVH (`stack[48]`)** across **Global Illumination, Contact-Hardening Soft Shadows, Area-Light Penumbra, Multi-Surface Color Bleeding, and 1–3 Bounce Specular Reflections** on the authentic Right-Handed **`ControlVehicle`** (`+X` Forward, `+Y` Left, `+Z` Up) with **zero ghosting, zero smearing, and zero jittering noise**.

## Quick Start

```bash
python3 Experimental/GtxReflectionBenchmark/Serve.py 8096
```

## Architecture & Asset Bundle (`Assets/VehicleBenchmarkAssets.bin`, `"FVB1"`)

- **Expanded Spacious Proving-Ground Scene (`25,920` Instanced Triangles, `5,930` Binned-SAH BLAS Nodes)**:
  - `ControlVehicleBody` (`2,960` triangles), `4x ControlVehicleRim` (`1,568` triangles each), `4x ControlVehicleTyre` (`3,968` triangles each), and an expanded **`44 m × 28 m` `ProvingGroundArena` hall on a `96 m × 96 m` showroom concrete pad** (`44 m` North Silver Mirror Wall, `28 m` East Gold Mirror Wall, `6.4 m` High-Bay Overhead Showroom Portals, Central + West/East Vivid Crimson-Red `mat 18` & Cobalt-Cyan `mat 19` Color-Bleed Bays, Matte White Clay GI Plinths `mat 20`, and Chrome/Cyan Slalom Pylons).
  - Prominent **Live Display FPS (`#hud-fps-pill` & `#fps-hero-readout`) + Uncapped Pass-Equivalent FPS (`SDF` vs `BVH`)** HUD telemetry, unlocked `44 m` wide-angle orbit zoom, and `37 m × 23 m` `WASD` drivable proving-ground floor.
- **Zero-Ghosting / Zero-Smearing / Zero-Jitter Lighting Pipeline**:
  - **Zero Ghosting**: 100% single-frame evaluation (`0%` temporal history buffer) across all GI, shadow, penumbra, and reflection passes.
  - **Zero Smearing**: Demodulated irradiance filtering (`GI_BILATERAL_FRAG` filters `(giIrr.rgb, sunVis)` only with `normal^32 × exp(-|planeDist| * 45)` edge-stopping + PCSS blocker-distance contact-hardening shadow kernel, re-multiplying full-res albedo/procedural grid + joint-bilateral upsampling in `COMPOSITE_FRAG`).
  - **Zero Jittering Noise**: Time-invariant spatial `IGN` Golden-Spiral sampling (`6` self-plane-free sphere-marched cones in `GI_SDF_FRAG`; `6` cosine-hemisphere rays + `4` Golden-Disk solar rays in `GI_BVH_FRAG`) integrated by a 3-pass (`step = 1, 2, 4`) plane-aware cross-bilateral filter.
- **Interactive Sliders, Quality Scaling & A/B Improvement Toggles**:
  - `Sun Penumbra` slider (`0.00` hard pinhole → `3.50` wide overcast area sun), `GI + Color Bleed` slider (`0.00×` → `3.00×`), and live `Sun Azimuth / Elev` slider (`-80°` → `+110°` with `< 0.5 ms` GPU static cascade re-shading).
  - **Quality Scaling**: **`GI Bounces` (`1B / 2B / 3B`)** (3D Surface-Cache Voxel-to-Voxel Radiosity Propagation on Global SDF vs. Multi-Bounce Cosine Path Tracing on Compute BVH) and **`Samples / Px (SPP)` (`Low 4/2` | `Std 6/4` | `High 12/8` | `Ultra 24/12`)**, plus **`Full-Res 1.0×` vs `Half-Res 0.5×`** trace toggle.
  - **Live A/B Visual Fix Toggles (`A/B: Fixes ON` master switch + individual chips)**: **`Dir SDF GI`** (directional Lambertian SDF emission — fixes pink isotropic underglow), **`Contact AO`** (6-direction full-res horizon AO), **`Smooth PCSS`** (`3×3` blocker dilation + continuous Gaussian penumbra — fixes sharp->soft halo cliff), and **`R2 De-Blotch`** (2D `R2` stratification + Pass-1 variance clamp — fixes 6-ray cloudy GI blotches).
  - `Global SDF` | `Compute BVH` | `Split SDF | BVH` | `Clay GI + Penumbra` | `Pixel Delta` modes, plus `GI + Bleed`, `Sun Shadows`, `Soft Penumbra`, `Hi-Z Assist`, `BVH NEE`, `Off / 1B / 2B / 3B` reflections, `Auto-Glide / Static Pose / WASD`, `Chase Cam`, and 6 clearcoat paint swatches.
- **Measured `1280x720` Full-Pipeline Verification (`02-ControlVehicleGiPenumbraColorBleedSplit.png`)**:
  - **Global SDF Total 3D TMU Taps**: `66,088,440` (`71.7` taps/px) vs. **Compute BVH Stack Steps**: `1,133,046,238` (`1,229.4` steps/px) = **`17.14x` fewer traversal operations** in Global SDF.
  - **Mean Composite Pixel Delta**: **`4.91%`** (`GI = 2.82%`, `Penumbra = 0.71%`, `Reflections = 1.98%` — strictly within `Constants::MaximumOptimizationPixelDelta = 0.05f`, zero checkerboarding).

# Deterministic Path Tracer (WebGPU prototype)

Physically based path tracer for product, film and game look-development that uses **no random numbers**.
Scene: the VehicleHMI **Controlled Integrator** (25,068 triangles) in a three-softbox studio with seven
material spheres. Runs on WebGPU compute only, so it needs no RT cores: GTX 900-series and newer, and RTX.

## Run

```bash
python3 Serve.py            # then open http://localhost:8087/ in Chrome or Edge 113+
```

WebGPU needs a secure context: `localhost` qualifies, a plain-http LAN address does not.

| URL parameter          | Effect                                                        |
|------------------------|---------------------------------------------------------------|
| `camera=Hero`          | `Hero`, `Profile`, `Front`, `Spheres`, `Rear`, `Fleet`        |
| `paint=Racing`         | `Authored`, `Racing`, `Midnight`, `Pearl`, `Graphite`         |
| `scale=0.5`            | Render scale relative to the viewport (default 0.75)          |
| `trace=2`              | Trace resolution: 1 ray per 2×2 (`2`) or 4×4 (`4`) pixels     |
| `target=4096`          | Samples per pixel before the loop idles (default 1024)        |
| `width=…&height=…`     | Fixed accumulation size (validation)                          |
| `validation=1`         | No render loop; exposes `window.DeterministicTracing`         |
| `present=readback`     | 2D-canvas display via GPU readback, if a WebGPU canvas fails  |
| `pattern=pixel`        | Per-pixel decorrelated point sets (fine dither, no banding)   |
| `pattern=bluenoise`    | Per-pixel digital shift from a blue-noise mask (even dither)  |
| `visibility=raster`    | Camera hits from a triangle-ID raster instead of the tree     |
| `reproject=1`          | Keep the image through camera moves (reprojected history)     |
| `vehicles=50`          | 1, 2, 4, 8, 16, 32 or 50 ControlVehicles in a parking grid    |
| `denoise=1`            | Edge-aware à-trous denoiser on the display (sums untouched)   |
| `span=8`               | Exact samples until reprojected history is gone (default 8)   |
| `clamp=1.5`            | Clamp history to mean ± γσ of current 3×3 (default 0 = off)   |
| `sigma=4`              | Denoiser luminance tolerance at 1 spp (÷ n after that)        |
| `timing=0`             | Do not request GPU timestamp queries (no "GPU per frame" row) |
| `lamps=vehicles`       | IES lamps: `vehicles` (2 head + 2 tail per car), `carpark` (+ 28 downlights) |
| `lampsampling=split8`  | Lamp rule: `tree` (1 lamp per hit), `split4`, `split8` (default), `all` (reference) |
| `lampscale=2`          | Lamp intensity multiplier (default 1)                          |
| `night=1`              | Softboxes off, sky at 2 %: only the lamps light the scene      |

The top-right counter shows traced frames per second, smoothed ms per frame and samples per frame
("Samples per dispatch" selector). It reads "idle" once the target is reached. The loop is paced by
`requestAnimationFrame`, so FPS cannot exceed the display refresh rate. It no longer waits for the GPU after
every frame: up to 2 frames are in flight, and the frame time is the interval between GPU completions.

**GPU per frame** (inspector status) is measured with WebGPU `timestamp-query` on every pass, e.g.
`41.2 ms = guides 0.4 · trace 38.1 · denoise 2.7`. Stages: reproject (camera-move history), guides (denoiser
feature buffers), trace (all integration dispatches of the frame), denoise, display. Values are moving averages
(weight 0.15) read back through a 3-buffer ring without stalling the loop. If the adapter lacks
`timestamp-query` (or with `timing=0`) the row says so. Browsers may quantize timestamps, so very short
stages can read as 0 and are hidden below 0.05 ms.

## Real-time options (inspector → Real-time)

All are deterministic: no RNG, same camera path → same pixels. Defaults keep the original output bit-identical.

| Option              | What it does                                                                                   |
|---------------------|------------------------------------------------------------------------------------------------|
| Pattern             | Shared: one point set for all pixels. Per-pixel: each pair's digit permutation is XOR-keyed by a fixed hash of the pixel coordinate. Blue-noise: each pair's digits are XOR-shifted by two 64² void-and-cluster masks read at a fixed per-pair tile offset. Subpixel pair stays shared; every pixel still gets a full (0,m,2)-net. |
| Visibility          | Rasterized: a depth-tested `r32uint` pass, shifted to the sample's subpixel point, names the camera triangle; the ray is re-intersected exactly, misses fall back to the full trace. Pinhole only. |
| Camera motion       | Reproject history: one compute pass traces the new and previous centre rays, keeps the previous displayed colour where the surface matches (category, position, material, normal). Mirror-like surfaces (sharpest lobe ≤ 0.05) follow the reflected object's virtual image (hit distance + mirror-ray length) and must reflect the same object. |
| History             | Sample-equivalents of history kept (8–64): half on mirrors, 0 on glossy 0.05–0.4 lobes, full at ≥ 0.4. Weight × (1 − n/span)², span 8: gone after 8 exact samples, so it never drags and the converged image is history-free. |
| Denoiser            | `DisplayReconstruction.wgsl`: 5 à-trous steps (1–16 px) stopped at normal, plane-distance, material and variance-scaled luminance edges (σ/n). Mirrors use only the 2 finest steps. Strength 1 to 4 spp, fades to 0 at 64 spp. Pairs best with the blue-noise pattern (decorrelated error filters out; shared-pattern error is the same in neighbouring pixels). |
| Trace resolution    | ½ or ¼: each invocation traces one 2×2 / 4×4 tile; the shared subpixel point spans the tile and the sample lands in the pixel it falls in. The subpixel pair is a (0,2)-sequence, so every 4 (16) consecutive frames put exactly one sample in each pixel of every tile — no pixel is skipped or doubled, and all tiles stay coherent. Until a pixel has its own sample, the gather pass fills it with a tent-weighted mean of sampled neighbours within ±scale, weighted by the full-resolution guides (material, normal, plane distance); valid reprojected history outweighs the fill. The counter and target count samples **per pixel** (a ½ frame = 0.25 spp). Full resolution is untouched (`a0ca494d`). Rasterized visibility is off at reduced resolution. |
| Vehicles in scene   | 1–50 instances of one vehicle (5 rows × 10 bays, 9.7 m × 3.6 m pitch, ≥ 1.1 m clearance, cycling paints). One vehicle tree is shared; changing the count rebuilds only the instance tree (≈ 1 ms instead of 5.4 s for 50). |

Measured results and the comparison image: [`Exhibits/DeterministicPathTracer/`](../../Exhibits/DeterministicPathTracer/README.md#real-time-options).

## What "deterministic" means here

| Classic Monte Carlo path tracer             | This tracer                                                              |
|---------------------------------------------|--------------------------------------------------------------------------|
| Per-pixel random numbers → grain            | One fixed Sobol' (0,2) point set shared by every pixel → no grain        |
| Coin flip: reflect **or** refract           | Fresnel splitting: reflect **and** refract, each with its exact weight   |
| Coin flip: pick coat / specular / diffuse   | Lobe splitting: every lobe is traced every sample                        |
| Russian roulette kills paths at random      | Fixed bounce / chain limits and a fixed throughput cutoff                |
| Random light point                          | Light quadrature point per sample, MIS with the BSDF rule (power β = 2)  |
| Error ∝ N^-1/2, looks like noise            | Error ≈ N^-0.7…-1 measured, structured, shrinks every power of two       |
| Different image every run                   | Bit-identical accumulation for the same sample count (verified)          |

- Pairs are decorrelated by a **fixed** nested-uniform (Laine–Karras) digit permutation keyed only by the pair
  index, never by the pixel (default Shared pattern). Every 2^m prefix of every pair is a (0,m,2)-net.
- Samples are added to the float accumulation strictly in index order, so the result does not depend on
  dispatch batching, frame timing or GPU speed. The inspector's **Verify bit-exact repeat** checks this live.

## Materials

| Category          | Model                                                                         | Shown on                     |
|-------------------|-------------------------------------------------------------------------------|------------------------------|
| Layered           | Lambert under GGX dielectric specular, optional GGX clear coat                | Body paint, ceramic, chalk   |
| Conductor         | GGX (height-correlated Smith, VNDF quadrature), exact complex-IOR Fresnel     | Gold, copper, chrome, rims   |
| Solid dielectric  | Exact Fresnel split, Snell refraction, Beer–Lambert absorption                | Clear and emerald glass      |
| Thin dielectric   | Two-interface sheet Fresnel, straight tinted transmission (shadow rays too)   | Vehicle glazing              |
| Emissive          | Exitance on any layered surface                                               | Head- and tail-lamps         |

Measured metal indices: Au, Cu, Al, Cr, Ag (RGB at ~650/550/450 nm). Vehicle albedos follow
`SlateReference/.../VehicleProvingGrounds/Source/MaterialSpecification.js`; body family 9 (cabin canopy) gets
a clear-coated slot, rim family 9 a satin aluminium slot.

## Files

| Path                                  | Responsibility                                                     |
|---------------------------------------|--------------------------------------------------------------------|
| `Source/QuadratureSequence.wgsl`      | Sobol' (0,2) pairs, nested digit permutation, pixel keying         |
| `Source/RadianceIntegration.wgsl`     | Bounds-tree casting, materials, transport, history reprojection    |
| `Source/VisibilityRasterization.wgsl` | Triangle-ID raster for camera hits                                 |
| `Source/DisplayReconstruction.wgsl`   | History blend + edge-aware à-trous denoiser (display only)         |
| `Source/DisplayExposure.wgsl`         | Exposure, ACES fitted tone curve, sRGB                             |
| `Source/TracingDivision.js`           | WebGPU device, buffers, instance records, dispatch, readback, digest |
| `Source/BoundingVolumeConstruction.js`| Binned SAH (16 bins) collapsed to 4-wide nodes, 8-bit child bounds  |
| `Source/StageTiming.js`               | Per-pass GPU timestamps, non-blocking readback ring, averages       |
| `Source/VehicleAssembly.js`           | FCT1 decode, creased body normals, 4 mirrored wheel instances      |
| `Source/StudioSceneAssembly.js`       | Material table, per-car finishes, spheres, aimed softboxes, presets |
| `Source/PhotometricProfile.js`        | LM-63 (.ies) parser (Type C, all symmetries, TILT), 128 × 32 resampled table |
| `Source/PracticalLighting.js`         | Lamp placement per bay, light tree build, packed light records     |
| `Assets/Photometry/*.ies`             | Low-beam headlamp, tail lamp, car-park downlight (authored, LM-63-2002) |
| `Tools/PhotometryConstruction.mjs`    | Writes those three .ies files from closed-form distributions       |
| `Source/WorkspaceInterface.js`        | Inspector, orbit input, accumulation loop, verification, PNG       |
| `Assets/BlueNoiseRanks.bin`           | Two 64² void-and-cluster rank masks (16 KiB), committed            |
| `Tools/BlueNoiseConstruction.mjs`     | Rebuilds the masks: fixed integer seed, lowest-index tie-break     |
| `Assets/ControlVehicle*.facet`        | Blob-identical copies of the SlateReference archives               |

Validation, measurements and captures: [`Exhibits/DeterministicPathTracer/`](../../Exhibits/DeterministicPathTracer/README.md).

## Scene tree and instancing

| Piece             | Layout                                                                                          |
|-------------------|-------------------------------------------------------------------------------------------------|
| Vehicle tree      | Binary binned SAH, then collapsed: each node opens its largest-area inner child until it holds 4 children. 3,895 nodes, depth 11 (binary: 15,991 entries, depth 20). |
| Node (64 bytes)   | Origin (3 × f32), per-axis power-of-two scale, 8-bit min/max per child and axis, 16-bit leaf counts, 4 links. Child boxes are rounded outward and verified in float32 on the host, so no hit is ever culled. Leaves hold ≤ 15 triangles (larger SAH leaves are median-split). |
| Instance tree     | Same build over one box per bay, 1 instance per leaf. Instance record: world→object and object→world rows, tree root, slot swaps (BodyCoat → the bay's own `FleetFinish`, RimAlloy → one of four `FleetRim` finishes). |
| Closest hit       | One loop, one stack for both levels. Children are sorted by entry distance (fixed 5-exchange network on vector lanes, ties keep child order) and pushed farthest first; an instance leaf transforms the ray (not renormalized, so `t` stays a world distance) and pushes a return marker. |
| Shadow rays       | Same loop without sorting: every crossing counts, glazing attenuates, the product is order-fixed. |
| Visibility raster | Instanced draw (`instance_index` → record); IDs are `instance << 20 | triangle`. |

The one-vehicle output is **bit-identical** to the binary tree (digest `a0ca494d`, 320×180, 16 spp): same
triangles, same intersection arithmetic, identity transform. Fleets use object-space rays instead of
host-transformed triangles, so their digests changed once, and repeat exactly (batch 1 = batch 4).

`Division.MeasureRayStatistics(n)` runs the integrator with a pipeline-override constant that counts closest
rays, shadow rays, child-box tests and triangle tests per pixel-sample (the normal pipeline compiles the
counters away).

## Per-car finishes

Every parked car (bays 1–49) gets its own body material and one of four wheel finishes. A fixed rule, no RNG:
hues step by the golden ratio, and the finish cycles with stride 5 through six families: gloss solid (clear coat),
metallic (coat + raised F0), matte wrap (no coat, roughness 0.62), satin metal and candy chrome (tinted conductors
through the Gulbrandsen 2014 reflectivity / edge-tint → η, κ mapping), and pearl. Every 7th car is near-black,
every 7th (offset) near-white. The hero car keeps the inspector paint. Swapping is per instance (two slot
replacements in the instance record), so it costs no geometry.

## Practical lamps and the light tree

Lamps are IES point emitters: 2 low-beam headlamps and 2 tail lamps per car (5 cm in front of the lens strips,
moved with the bay transform), plus 28 batwing downlights 5 m above the four aisles (`lamps=carpark`). At 50 cars
that is 228 lamps.

| Piece            | What it does                                                                                  |
|------------------|-----------------------------------------------------------------------------------------------|
| Photometry       | `.ies` files are parsed (LM-63, Type C) and resampled to 128 θ × 32 φ, normalized to their peak; the lamp record carries peak candela × colour × 1/40 (scene units). The shader reads the table bilinearly. |
| Light tree       | Binary tree over lamp positions (Conty Estevez & Kulla 2018). Each node stores its box, total power, the cone of emission axes (θo) and the emission extent (θe). Split: longest axis, cut at the most even power balance. Built on the host in ≈ 2 ms for 228 lamps (depth 9). |
| Importance       | power × cos θ′ / d² × cos θi′, with θ′ and θi′ widened by the node's angular size. It is zero only when every lamp in the node is provably dark toward the point or below its surface, so no contributing lamp is ever skipped. A leaf (one lamp) uses its exact beam: I(ω) cos θi / d². |
| Selection        | Descend from a node, picking each child with probability ∝ importance, steered by one fixed quadrature coordinate (pair 66 + depth), rescaled at each level. Contribution ÷ product of the probabilities, so the result is **unbiased** (it converges to the every-lamp image). |
| Adaptive split   | `split4` / `split8` (default): before drawing, the root is split into up to 4 / 8 subtrees (Conty Estevez & Kulla's adaptive splitting). Each round splits the node with the largest power × radius² / distance⁴ while its radius² > 0.05 × distance², so nearby, bright, spread-out clusters get their own draw and far clusters share one. One lamp is drawn under each subtree. The subtrees partition the lamps, so it stays unbiased. |
| Every-lamp rule  | `lampsampling=all`: one shadow ray per lamp per hit. The reference for the light tree; its cost grows with the lamp count. |

Lamps are points, so lobe rays can never hit them and no MIS weight is needed; the lit lens strips are the
existing emissive triangles (the visible part, reached by lobe rays). On mirror-like surfaces a point lamp gives
a single-pixel highlight, not a reflection of the lens. Softboxes and lamps share one light loop and one
shadow-ray call site. With lamps off, output is bit-identical to before (`a0ca494d`).

**Why a light tree and not clustered lighting:** clustered (froxel) lighting bins lights by screen-space tiles
and depth slices, which only covers points the camera sees directly. Path-traced reflections and bounces shade
points anywhere in the world. A world-space light grid would cover them, but it gives every light in a cell the same
weight and has to cap lights per cell. The light tree weights each lamp by its actual contribution (distance,
orientation, beam shape) at the shading point, costs log₂(N) node visits, and stays unbiased.

## Limits

- Deterministic ≠ instant: every pixel uses the **same** quadrature point for a given sample index, so N samples
  look like N hard shadows and N copies of each softbox in glossy reflections (stepped penumbrae, banded
  reflections, blotchy caustics) instead of grain. Large softboxes need several hundred spp to merge. Caustics are reached only by
  BSDF rays and converge slowest.
- Softboxes are hidden from camera rays only. The bright rectangle on the floor at the lower right of Hero
  is the rim softbox's mirror image in the coated epoxy (isolation: gone on matte Concrete, still there
  with the spheres removed). It is physically correct; set Rim to 0 or use Concrete to remove it.
- Fixed limits are a deterministic bias: default 2 indirect bounces, 10-interaction specular chains, cutoff 0.002.
- Rough glass, textures, subsurface scattering, volumes and a sun/HDRI light are not implemented.
- Bit-exactness is per device and driver. WGSL leaves transcendental precision implementation-defined, so a
  GTX and an RTX need not produce identical bits; each repeats itself exactly.
- Tested only on SwiftShader (CPU WebGPU) in headless Chromium. No physical GTX/RTX run or timing is claimed.
  SwiftShader inlines every function and keeps the whole tree in CPU cache, so it rewards small shader code and
  does not reward compressed nodes or instancing; there the 4-wide instanced build is ≈ 1.2× slower per sample
  than the binary baked tree. Use the GPU-per-frame row to compare on real hardware.
- Default display is a WebGPU canvas swap chain. It is **untested**: headless SwiftShader cannot allocate one
  (device lost on first present), so the interface capture uses `present=readback`.
- Rasterized visibility is not bit-identical to traced visibility: subpixel vertex snapping and coplanar ties
  resolve ≈ 0.2 % of pixels differently (relative RMS 1e-4). It also depends on the GPU's rasterizer.
- Reprojection carries colour by surface position, so moving reflections lag on glossy-but-not-mirror surfaces.
- Lamp lenses are not light-sampled; their exitance defaults to 20 % of the authored 48.7. The IES lamps are
  separate point emitters, so the light from a lamp is its beam plus its glowing lens.
- Downlights have no visible fixture (no ceiling geometry); only their pools and highlights show.
- One lamp per hit means many-lamp scenes start noisier than the studio rig; the denoiser and more frames take
  it from there.

## Integration path (later)

Slang port of `RadianceIntegration.wgsl` into the Vulkan renderer: the quadrature functions, the explicit
transport stack and the material categories carry over unchanged; the bounds tree can be replaced by
`VK_KHR_ray_query` on RTX while keeping the same compute path on GTX.

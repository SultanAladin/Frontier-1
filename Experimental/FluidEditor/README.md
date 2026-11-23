# 🌪️ Fluid Studio (`Experimental/FluidEditor`)

> **TL;DR**: Project-Zero HTML reference studio for the ground-up **Fluid · ForceField** redo (`CLAUDE.md` §1.2). A real-time
> WebGL2 3D Eulerian gas solver (tiled half-float volumes, MacCormack advection, buoyancy, combustion, vorticity confinement,
> Jacobi pressure projection, sphere colliders) with **10 analytic force generators**, GPU flow tracers, velocity-vector and
> scalar heat-map slices, and inspector gizmos — all framed in the exact Project-Zero chrome (trapezoidal dock tabs, top Notch,
> Outliner entity cards, hero / metric / status-tile Inspector, bottom Asset Browser drawer).


> **Combined build (2026-11-16).** Imported from `c7egoist/Frontier-1@2b62585` (commit `26152b6` here is the unchanged copy), then
> merged with the strongest algorithms of the other Fluid Editor. Evidence: `Exhibits/FluidEditor/`.
>
> | Area | Change | From |
> | :--- | :--- | :--- |
> | Pressure | Warm start: last step's pressure is the first guess (retain 0.96 on iteration 1). Inner divergence after 10 steps 67% → 44% | other editor |
> | Combustion | Burning gas expands (`Expansion`, *Gas expansion* slider, default 0.6): the burn rate in `state.w` becomes a divergence source | other editor |
> | Advection | MacCormack limiter brackets the same RK2 departure point as the forward trace | fix |
> | Light sum | Energy-conserving step `S·(1 − e^{−σΔ})/σ` instead of `S·Δ` | other editor |
> | Phase | Two-lobe Henyey-Greenstein (forward g, back −0.25) | other editor |
> | Multiple scattering | Three octaves (energy, optical depth and anisotropy halve each octave), normalised to the old lit brightness | Wrenninge, in place of the other editor's blend |
> | Self-shadow | Geometric shadow steps (×1.35), default 8 | other editor |
> | Fire light | A screened-diffusion fire-light field solved in `pressure.y` during the Jacobi loop lights all nearby smoke and the floor (*Fire lights smoke* slider) | other editor |
> | Sky light | Ambient blocked by the smoke just above (two upward samples) | new |
> | Floor | Smoke shadows the floor | other editor |
> | Edges | Powder darkening on thin wisps | other editor |
> | Noise | Fixed interleaved-gradient jitter (no shimmer without temporal accumulation) | other editor |
> | Infinite Grid | The pressure loop no longer rebinds texture unit 0 over the tile lookup | fix |
| Advection (Follow-up 47) | MacCormack falls back to the plain semi-Lagrangian value wherever its correction overshoots the stencil, and fades out above a Courant number of 1–2 (Selle et al. 2008). Clamping the overshoot instead fed energy back every step: scenes started clean and churned into noise after 6–10 s | fix |
| Forces (Follow-up 47) | Steady pushes (wind, attractors, vortices, curl noise…) stop accelerating gas that already moves with them: each force is scaled by `clamp(1 − v·f̂ / (|f|·T), 0, 1)` with `T` = Domain *Force response* (default 1 s, 0 = off). Bursts, drag, containment and shocks are exempt | new |
| Scene presets (Follow-up 47) | The other editor's 18 presets (now `Presets/pyro-*.toml`; `Engine/PyroScenePresets.js` is kept as the unloaded record of the original blocks), translated by stated ratios to emitters, forces and colliders. Listed as *Pyro scenes* in the browser | other editor |
| Flipbook (Follow-up 47) | Toolbar *Flipbook* → bake 4/16/32/64 frames of 64–512 px at 12–60 fps, optional warm-up, empty-grid start and transparent background, into one PNG sheet plus timing/UV JSON | other editor |

---

## 🏷️ 1. Layout & Surfaces

| Surface | Mechanics |
| :--- | :--- |
| **Outliner** (left dock) | `Visible` / `Hidden` counters, search + filter pills, collapsible collections `Domain · Emitters · Force Generators · Colliders · Visualizers`, per-node enable dot + eye toggle, inline `+` constructors. |
| **Viewport** (centre dock) | `Scene · PREVIEW` header, `Edit / Sim / Play` segmented mode, `Add Force` primary pill, camera preset select, five overlay toggles, transport (`Play · Step · Reset` + sim clock), colour-ramp legends, bottom `Asset Browser` trapezoid drawer. |
| **Inspector** (right dock) | Breadcrumb, renameable title, tinted hero banner with type glyph, 2×2 metric cards, status tiles (`Enabled`, `Gizmo`, `MacCormack`, `Pulsed`…), `PROPERTIES` cards with pill sliders + numeric fields, XYZ vector rows, choice chips, `Duplicate / Delete`. |
| **Notch** (`Project-Zero · Fluid Studio`) | Workspace switcher placeholder, `.FluidScene` JSON export / import / reload, shortcut legend. |
| **Status bar** | Realtime fps, solver ms + GPU pass count, grid dims + cell count, active forces/emitters, honest `HTML preview · no engine connection` text. |

---

## 🧩 2. Solver Pipeline (`Engine/FluidSolver.js` + `Engine/FluidShaders.js`)

Domain resolution follows the Unreal convention: world **Size** stays fixed, **Voxel resolution** sets the cell count on the longest axis (`Draft 32 … Extreme 160` chips), and the per-axis grid + cell size are derived. Open faces (`OpenTop` default) carry an outflow sponge band so plumes exit instead of flattening on the lid; the `Voxel` volume filter shows the raw cells.

Fields are 3D grids stored as 2D tile atlases (`RGBA16F`, hardware bilinear + manual slice lerp = trilinear). Per step:

| # | Pass | Kernel |
| :---: | :--- | :--- |
| 1 | Emission | `InjectFragment` — sphere / disc / box masks, noise jitter, pulse envelopes → velocity + `(density, temperature, fuel)` |
| 2 | Advection | `AdvectFragment` (RK2 semi-Lagrangian) or `AdvectFragment ×2 + MacCormackFragment` (error-compensated, monotone-clamped) for velocity and state |
| 3 | Thermodynamics | `ThermodynamicsFragment` — fuel ignition above threshold, heat + soot release, Newtonian + radiative cooling |
| 4 | Forces | `ForcesFragment` — thermal buoyancy, smoke weight, **force generator sum**, collider masking |
| 5 | Vorticity | `CurlFragment` (kept for visualisation) + `VorticityFragment` confinement `ε h (N × ω)` |
| 6 | Projection | `DivergenceFragment` → `PressureFragment` × N Jacobi → `GradientFragment` with closed / open-top / open boundaries and solid Neumann cells |
| 7 | Tracers | `TracerUpdateFragment` — RK2 particle integration, lifetime respawn, previous-frame copy for trails |

### Particle / mograph system (`Engine/ParticleCatalog.js` · `ParticleShaders.js` · `ParticleSimulation.js`)

A self-contained GPU particle module that shares the force-generator gizmos with the gas solver — drag any force through a system and it reacts. Unlimited simultaneous systems (no 4-system cap) × 16 384 particles each.

| Layer | Contents |
| :--- | :--- |
| **Catalog** (portable, no WebGL) | 16 shapes `Point · Sphere · Box · Disc · Ring · Line · Grid · Torus · Box Shell · Mocap Figure · Dome · Object Surface · Spiderweb · Cloth Grid · Flora Bed · Firework Shell`, modes `Emit · Cloner`, colour rules `Life · Speed · Displacement · Height · Seed · Swatch · Cycle`, blend/style, 18 sprite cutouts, 54 templates |
| **Cinematic look** | World-space sprite sizing with log-spread size randomness (dust ↔ bokeh), sprite footprints `Glow · Disc · Bokeh · Star · Square`, fake depth of field (focus distance + aperture → circle of confusion grows/fades), additive glow halo pass, turbulence field (scale + evolution), Form-style cloner **Dissolve**, sphere **Fill**. Templates *Stardust Ribbon, Nebula Burst, Dissolving Sphere, Bioluminescent Bloom*; presets *Stardust* and *Dissolving Form* |
| **Trails** | Per-particle GPU history ring (`side × side·L` texture, up to 64 samples at a chosen interval), rendered as Catmull-Rom splines extruded into screen-space ribbons (width in metres or pixels, taper, fade, 1–4 subdivisions, particle or along-trail palette colour, glow halo pass, trailed fraction). Templates *Light Strands, Silk Ribbons, Neon Filaments*; preset *Light Strands* |
| **World Grid** | The domain size is one *tile*; `Tiles` (up to 5 per axis) runs several adjacent tiles as one seamless field, so gas crosses seams freely. `Follow` (Emitter / Particles / Camera) slides the window across an infinite tile lattice by whole tiles whenever the target enters a neighbour tile — fields are moved by an exact cell offset (`ShiftFragment`), so smoke already laid down keeps simulating in world space instead of being clipped at the old box. Emitter paths *Straight* and *Slalom* give unbounded drives; preset *Drift Run · World Grid* |
| **Infinite Grid** | `Domain.Grid = Infinite`: no box. An unbounded lattice of cubic tiles (`TileSize`, `TileResolution`³ cells) lives in a slot atlas (`TilePool`); a 64×16×64 tile→slot lookup texture around a sliding focus gives tile-aware addressing (`SparseVolumeChunk`, swapped into every kernel by `sparse()`), so advection / projection / overlays / particles run unchanged across tile seams. Emitter tiles are pinned; an occupancy pass (`OccupancyFragment`) is read back every 3 frames to allocate neighbours across faces gas presses on and free tiles that emptied. Ground is y = 0. Presets *Infinite Grid · Drift / Plume* |
| **Shaders** | `ParticleShapeChunk` (lattice + random sampling per shape), update kernel (spawn, force field × influence, gas coupling, gravity, drag, curl wander, cloner spring, floor bounce, timing envelope), soft-sprite + streak render |
| **Simulation** | One ping-pong record per scene node, lazily created and pruned; borrows solver force uniforms + velocity volume |

**Cloners** hold a formation (Fibonacci sphere, sunflower disc, cubic lattice, ring, line) with a spring and colour by displacement — the mograph effector workflow: your force gizmos are the effectors. Presets *Mograph Field* and *Celebration* hide the volume render entirely. Adding a shape/colour rule/template = one catalog entry + one shader branch; the C++ port mirrors the same numeric codes.

#### 3D object carriers & object-surface spawn (`Engine/ObjectMeshCatalog.js`)

A particle's visual is not necessarily a sprite, and a spawn is not necessarily a primitive: **Layer 211 *Sprite & Object*** picks the visual carrier (`Sprite` or `Object3D`) and **Layer 201 *Spawn*** can use `ObjectSurface` as its origin. Both read the same mesh catalogue and the same imported custom mesh.

| Piece | What it does |
| :--- | :--- |
| **19 object meshes** | `Cube · Sphere · Coin · CrystalShard · HexPrism · StarPoly · UtahTeapot · SportsCar · ObsidianHand · SculptedBust · TorusKnot · PrecisionGear · CrystalCluster · Archway · FacetedSpire · WingedButterfly · WingedBird · FishBody · CustomObject` — 10 061 procedural triangles, built and cached on first use |
| **Spawn sampling** | Area-weighted CDF over the mesh with stratified quasirandom picks: `EvenSurface`, `SurfaceVolume` (a third of the anchors pulled toward the interior) and `RidgeAccent` (crease edges found by dihedral angle, 72% of anchors on ridges). Anchors are baked on the CPU, uploaded once as `RGBA32F` (two texels per anchor) and fetched per particle with `texelFetch` |
| **3D carrier pass** | `gl_VertexID / verticesPerParticle` = particle, remainder = mesh corner; corners come from an `RGBA32F` texel mesh, so the whole pass is `emptyVao` + `drawArrays(TRIANGLES)` — no vertex buffers, attributes or instanced divisors. Per-particle spin + tilt matrix, Blinn-Phong with roughness→spec-power, metallic boost and grazing Fresnel (`ObjectSpin · ObjectTilt · ObjectRoughness · ObjectMetallic · ObjectGlow`) |
| **Backing surfaces** | `BackingSurface` draws the spawn mesh behind the cloud once per system: `ObsidianGlossy` (dual studio softboxes + deep Fresnel on black), `BrushedChrome` (anisotropic brushed bands) or `GhostWireframe` (emissive silhouette drawn with `gl.LINES` over the same program) |
| **Custom mesh import** | `.obj`, binary/ascii `.stl` and `.json` (`{vertices, indices}`) parse into a runtime registry, normalised to a unit bounding radius. Import from the Inspector button on **either** layer or by dropping the file **onto the viewport**; the id is stored once on the Spawn layer and both the origin and the carrier follow it (`CustomObjectId`). Unknown ids fall back to the Utah Teapot |
| **New origins & cutouts** | Spawn shapes `Spiderweb` (spokes + sagging rings), `ClothGrid` (drape folds), `FloraBed` (rooted, wind-bent blades), `FireworkBurst` (lobed shells) plus the object surface; sprite cutouts 9–17 `SnowCrystal · RainDrop · LeafBlade · BlossomPetal · CrystalShard2D · ShockRing · RadarArc · AuroraCurtain · CoinDisc` |
| **Content** | 54 particle templates and 46 scene presets — 14 new templates (*Spiderweb Orb, Cloth Flag, Flora Meadow, Object Ridge Pour, Obsidian Hand Halftone, Coin Fountain, Crystal Shatter, Starling Murmuration, Bioluminescent Fish School, Ferrofluid Spikes, Aurora Curtain, Radar Sweep, Autumn Leaves, Sakura Petals*) and 6 multi-cloud presets (*Obsidian Gallery, Aviary at Dusk, Coral Reef Drift, Mint Vault, Sakura Courtyard, Signal Array*) |

Evidence: `Exhibits/FluidEditor/Results/ObjectCarriers_SpawnObjects_CustomMesh.png` (all 19 meshes, the nine new cutouts, the four backing surfaces and the anchor/texel numbers) and `ObjectCarriers_NewTemplatesAndPresets.png` (rendered thumbnails of the 14 templates and the composition of the 6 presets). `FluidValidation.mjs` §13 covers the catalogue, anchor sampling determinism, texel packing, all three import formats, and the GPU object + backing passes.

### Gas layer stack (`Engine/LayerStack.js` · plan `Documentation/Plans/Ongoing/FluidLayerStack.md`, phases 1–2)

- **Single source:** `Engine/LayerSpecifications.toml` (8 gas categories, codes 101–108: Domain, Source, Buoyancy, Combustion, Vorticity,
  Dissipation, Smoke shading, Fire shading). After editing it run `node Experimental/FluidEditor/GenerateLayerSpecifications.mjs`, which
  validates the rules and writes `Engine/LayerSpecifications.js` with its hash. `FluidValidation.mjs` fails if the generated file is stale.
- **Evaluation:** layers fold top to bottom (Inject → Advance → Shade → Overlay) into `scene.Domain` and `Visualizers.Volume`, so the solver and
  renderer are unchanged. A disabled or missing layer contributes its neutral amount (for example, no Smoke shading = smoke not drawn).
  Weight 0–1, Blend (Replace / Add / Multiply / Maximum) and Solo per layer. Without the Domain layer the gas stops and particles keep running.
- **Presets:** all 36 convert on load and evaluate to the identical fields (checked). Existing Domain and Volume Render sliders write into
  the owning layer. Each emitter has a linked Source layer.
- **Presets are documents:** `Presets/*.toml` (36 `frontier-fluid-preset` files: scene summary, grid, render settings, debris, the gas
  stack, emitters, force fields, colliders, particle systems). After editing run `node Experimental/FluidEditor/GeneratePresetLibrary.mjs`
  (writes `Engine/PresetLibrary.js`). *Save as preset* in the top menu downloads the current scene as a document. The conversion from the
  old hardcoded objects is recorded in `Exhibits/FluidEditor/Results/PresetConversion.json` (36/36 identical scenes, identical FluidMetrics).
- **Particle systems are layer stacks:** every particle system has its own stack (Spawn, Home spring, Gravity, Drag, Noise, Field
  influence, Gas coupling, Bounce, Colour rule, Size over life, Sprite, Trail; categories 201–212 in `Engine/LayerSpecifications.toml`).
  The Inspector shows the stack (+ Add layer, enable, weight, blend, solo, reorder, delete) and the selected layer's settings. Templates
  are `ParticleTemplates/*.toml` (`frontier-particle-template`, 26 converted plus *Empty Cloud* = Spawn + Sprite), bundled by the same
  `GeneratePresetLibrary.mjs` into `Engine/ParticleTemplateLibrary.js`. Each particle entry of a preset carries its stack
  (`[[Particle.Layer]]`). Conversion record: `Exhibits/FluidEditor/Results/ParticleTemplateConversion.json`.
- **Files:** scene JSON Version 3 adds `GasStack`; Version 2 files convert. Stack TOML (`frontier-effect-stack`) round-trips byte for byte;
  unknown categories and parameters are kept verbatim and inert.

### Emitter motion paths

Emitters can orbit their position on a `Circle`, `Line` or `Figure 8` path; exit velocity is expressed in the moving frame so smoke trails behind (see *Tyre Drift Smoke*).

### Timing keys (forces + emitters)

Every force generator and emitter carries `StartTime · Duration · Fade · Repeat`. The solver scales strength (or emission) by a trapezoid envelope on its own clock, so a curl-noise generator can be live only during an explosion's roll-up and silent afterwards; a Scene Loop on the domain replays the sequence. Keyed nodes show a ⏱ range in the Outliner, a key track in the Inspector and a dimmed gizmo while inactive.

### Force generators (`Engine/ForceGeneratorCatalog.js` ↔ `ForceFieldChunk`) — the merged ForceField editor

| Category | Types |
| :--- | :--- |
| **Directional** | Directional Wind (gusting) · Wind Shear (heading twists with height) · Flow Stream (steer to target velocity) |
| **Attraction** | Point Attractor · Point Repulsor · Axis Attractor · Orbit (tangential + centripetal) · Containment (soft spring bottle) · Magnetic Dipole (flux-line field) |
| **Rotational** | Vortex Column · Vortex Ring · Rigid Spin |
| **Turbulence** | Curl Noise Turbulence (divergence-free gradient-noise curl) · Brownian Jitter (re-rolled per-cell kicks) |
| **Burst** | Radial Burst · Directional Burst · Shockwave Shell (expanding thin front) · Implosion — all with period, width and start delay |
| **Damping / Thermal** | Drag Baffle · Thermal Updraft |

Falloff envelopes: `Smooth · Linear · Inverse² · Infinite · Gaussian · Plateau · Interpolated (cosine)`. Up to **16** simultaneous generators, **6** emitters, **4** sphere colliders. Each generator exposes strength, reach, falloff, position, axis and up to three type-specific modulation parameters; negative strength inverts the field.

---

## 🔍 3. Flow Visualisation (`Engine/VolumeRenderer.js`)

- **Volume Render** — single-pass ray-march with jittered steps, directional shadow march, Henyey-Greenstein forward scattering, editable **fire incandescence ramp** (ember → flame → core colours, ignition temperature, span, flame opacity, emission), soot lit by its own flame, editable smoke albedo / extinction / ambient / sun / exposure with `Wood · Gas · Fuel · Magic` fire and `Soot · Grey · Steam · Dust` smoke look chips, ACES tone map; analytic ground grid + sphere colliders share the same ray.
- **Velocity Vectors** — GPU-generated arrow lines on an axis-aligned slice (stride, scale, Turbo / Engineering / Plasma / Diverging ramps).
- **Debris** — GPU particles launched from emitters with a Debris amount, in three kinds: **Rubble** (heavy, bounces, ember→ash), **Ember** (weightless glowing sparks that ride the heat), **Paper** (light fluttering sheets carried by the gas). Gravity, gas-coupled drag, floor bounce, streak trails.
- **Force Vectors** — GPU-evaluated Σ-force arrows on a slice plane or as a sparse 3D lattice, coloured by |F|.
- **Scalar Slice** — heat-map of `Speed · Pressure · Vorticity · Temperature · Density · Divergence · Force |F|` (force magnitude is evaluated live from the generator uniforms).
- **Flow Tracers** — 8 192 GPU particles with speed-coloured points + trails.
- **Force Gizmos** — per-type wireframes (wind arrows, attractor cages, vortex helix, ring tubes…); click to select, drag to move (`Shift` locks height). Gizmos never alter solver state.

---

## 🧪 4. Run & Validate

```bash
python3 Experimental/FluidEditor/Serve.py 8098        # http://0.0.0.0:8098/
node Experimental/FluidEditor/FluidValidation.mjs     # catalog · GLSL (glslangValidator if present) · mock-WebGL2 pass/uniform audit · JSON · DOM · UI smoke · layer stack
```

Requires WebGL2 with `EXT_color_buffer_float`. Fonts: DM Sans (OFL, bundled in `Fonts/`). No CDN or hot-linked assets.

Shortcuts — `Space` play/pause · `.` step · `R` reset · `F` frame · `Delete` remove · LMB orbit · RMB/MMB pan · wheel dolly.

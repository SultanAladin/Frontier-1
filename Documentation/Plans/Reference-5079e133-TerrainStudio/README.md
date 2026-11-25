# Frontier — AAA Procedural 3D Rock & Cliff Studio

**Frontier** is a structural-geology-first procedural 3D rock and cliff generator for AAA game production. Every overhang, stratified sedimentary bench, curve-swept sandstone arch, hexagonal basalt column, exfoliation sheeting visor, razor-fluted karst spire, and balanced caprock pedestal is synthesized in **true 3D geometry** — never faked with sedimentation stripe textures or displaced spheres.

---

## Core Architecture

1. **Analytical 3D Curve-Swept Arches (`src/geology/arch-curves.js`)**
   - Natural arches (`desert-arch`, `double-arch`, `sea-arch`) are sculpted directly along **true 3D cubic Bézier curves** with parallel-transported Frenet frames, slender crown taper, polygonal sandstone fin cross-sections, and stepped bedding pedestals — **zero hole-punching boolean cuts**.
2. **Real 3D Sedimentary Stratigraphy & Differential Weathering (`src/geology/sedimentary-formations.js`)**
   - `sedimentary-cliff` builds 17 continuous dipping stratigraphic horizons alternating between massive overhanging caprock cornices, rhythmically interbedded flagstone wafer packets, deeply undercut fissile shale recesses, lenticular paleochannel pinch-outs, oblique fault throws, and angular fallen caprock slabs.
3. **18 Distinct Procedural Formation Archetypes (`src/geology/presets.js`, `src/geology/formations.js`)**
   - **Arch**: *Utah Sandstone Arch*, *Double Sandstone Arch*, *Coastal Sea Arch*
   - **Sedimentary**: *Sedimentary Cliff*
   - **Desert**: *Utah Canyon Wall*, *Utah Slot Canyon*, *Desert Spires*, *Hoodoo Columns*, *Sandstone Butte*, *Balanced Pedestal Rock*
   - **Cliff**: *Jointed Cliff Face*, *Granite Exfoliation Dome*, *Columnar Basalt Cliff*
   - **Outcrop**: *Limestone Karst Tower*, *Alpine Crag Spire*, *Weathered Granite Tor*, *Dipping Slate Outcrop*
   - **Boulder**: *Fractured Hero Boulder*
4. **Sparse Narrow-Band SDF + QEF Dual Contouring (`src/core/sparse-sdf.js`, `src/core/dual-contour.js`, `src/core/qef.js`)**
   - Evaluates a narrow-band brick-sparse signed distance field and solves per-cell 3×3 Quadric Error Functions with Tikhonov regularization, preserving razor-sharp bedding ledges and fracture dihedrals while keeping rock faces organic and clean.
5. **AAA Game Engine Mesh Export (`src/io/glb-exporter.js`, `src/io/obj-exporter.js`)**
   - Exports binary **glTF 2.0 (`.glb`)** and **Wavefront `.obj`** with crease-split normals, triplanar-ready UVs, adaptive QEM LOD decimation, and baked RGBA vertex masks (`R = Volumetric AO`, `G = Concave Cavity`, `B = Convex Edge`, `A = Upward Slope`).

---

## Quick Start

```bash
npm install
npm run dev      # Launch interactive WebGL2 studio on 0.0.0.0:5173
npm test         # Run unit & integration suite across all 18 archetypes
npm run build    # Production Vite bundle
```

### Headless CLI Asset Baker

```bash
npm run generate -- --archetype desert-arch --seed 4029 --quality high --out exports/arch.glb
npm run generate -- --archetype sedimentary-cliff --seed 4029 --lods --out exports/cliff.glb
```

---

## Update (continued from arena/74dbc624): rain on rocks, flat terrain colours, fixed water/snow/rock physics

- **Rock & Cliff Studio — particle rain erosion on the SDF** (`src/geology/rain-erosion.js`):
  Terrain-Studio-style droplets fall from the sky, run over the true 3D surface
  (projected gravity + SDF reprojection), and carve/deposit with a sub-voxel
  brush directly in the SDF grid before Dual Contouring. Anti-"melted chocolate"
  design: no smoothing pass anywhere, per-step carve capped by actual downhill
  drop, sharp convex edges protected via SDF curvature, thin arch spans
  protected from perforation, per-node channel-depth cap, hardness-aware via
  the geological block hardness. Controls: *Rainfall Erosion Amount / Raindrop
  Count / Sharp Edge Protection* (defaults 50% / 8k / 70%). Erode at
  High/Ultra, then bake down with the QEM target. CLI: `--rain 0.6
  --rain-droplets 14000 --rain-edge 0.75`.
- **Terrain Studio — flat colours**: all procedural colour "textures" removed
  from the terrain & rock render shaders (hexagonal column tiles, honeycomb
  fabric, noise grain, sand ripples, glitter). Albedo is now only the flat
  lithology palettes blended by hardness/elevation/slope/sediment/snow/water.
  Noise *generators* (terrain shape, weather) are untouched.
- **Water flow fixed**: proper Mei pipe model with per-second friction and an
  anti-overshoot levelling cap (no more sloshing/uphill pumping); snowmelt is
  single-counted through the cryosphere pass; droplets ride full total head
  and merge into lakes instead of skating across them; flat hydrostatic
  water-surface normals at shorelines; Froude-limited flow velocity.
- **Snow improved**: slope/wind-aware snowfall — retention sheds from steep faces, windward faces are scoured and lee faces drift-loaded (ported from egoistm1inbox arena/fd0eaa50),
  cohesion-aware avalanche repose, **mass-conserving** wind drift (tilted
  potential), stable pairwise glacier creep, rain-on-snow melt.
- **Visibility toggles**: Water / Snow / Rain can be hidden independently from
  the viewport toolbar (and mirrored in the inspector) without pausing sims.
- **Rocks are real rigid bodies now** (`src/erosion/rock-calving.js`):
  world-space position + quaternion orientation integrated at 120 Hz substeps
  under g = 9.81 with ellipsoid rolling physics raised to the **exact faceted
  mesh support** at contact (sharp polyhedron corners no longer seat through
  slopes), a one-time footprint-ring seating correction for wide rocks on
  uneven ground, Coulomb friction, restitution, rolling, water buoyancy/drag
  and impulse rock-rock collision. The vertex shader no longer re-snaps rocks
  to the heightfield — settled boulders stay put until erosion genuinely
  removes their support, then fall. Pausing/stopping the live sim now freezes
  boulder physics too, instead of letting rocks keep settling on a frozen
  terrain snapshot.
- **Rocks embed into snow & sediment**: the ground is layered (snow/ice over
  loose sediment over bedrock) and the soft layers are crushable — a boulder
  sinks until bearing pressure × contact-patch area carries its true mass
  (fresh snow ~8 kPa hardening as it densifies; sediment 55–140 kPa), so big
  rocks sink deeper, slabs ride high, and mega-boulders punch through snow
  onto sediment. Landings in snow are dead (plastic crush), rolling rocks
  plough and bog down (deep soft ground still leaves ~35% of a loose rock's
  diameter protruding on its compacted plug), and embedded rocks stamp the snowpack
  (`ROCK_IMPRINT_FRAG`): compacted footprint + the displaced volume resurfacing
  as a rim hugging the rock, so boulders protrude naturally and rolling ones
  leave furrows. Melt wakes embedded rocks to settle onto the sediment.
- **Sediment feels the rocks back**: boulders are rasterized into an obstacle
  field (`ROCK_OBSTACLE_FRAG`: rock-top height + footprint coverage) that every
  transport pass reads. Pipe water flux is throttled around protruding rocks
  (streams divert, pond behind boulder dams, overtop when deep), thermal
  talus avalanches use `max(terrain, rockTop)` as ground (sediment banks
  against boulders, sheds off their tops, spills over once banked high
  enough), droplets ride over/around rock tops with shielded pickup under
  footprints, the integrator armours the bed beneath rocks while deposition
  fills only the free cell fraction so sediment wedges up and buries them,
  and snow creep/drift sheds off and banks against boulders too.

- **Chemical weathering stains**: iron "bleeding" (rust) and copper patina,
  simulated not painted. Rock Formation editor: dissolved Fe/Cu from
  mineral-veined seep zones (joints/concavities) is routed down the finished
  mesh by steepest-descent flow accumulation (`src/geology/chemical-staining.js`),
  depositing ochre streaks along real drainage lines and pooling malachite/
  azurite in hollows — baked into a per-vertex stain attribute (sliders:
  Chemical Weathering / Iron / Copper; CLI `--chem/--iron/--copper`).
  Terrain Studio: boulders get per-instance mineralogy (seed hash) with
  seep-column rust streaks running down steep faces plus an oxidation rind.
  The terrain itself now has a **Chemical weathering & dissolution**
  simulation layer (`CHEM_WEATHER_FRAG`): a persistent GPU state tracks iron
  rust, salt efflorescence, signed desert-varnish/copper-patina coating and
  hydrolysis alteration from water depth, discharge, snowmelt, evaporation,
  slope, burial, hardness/fractures and seeded Fe/Cu/carbonate veins. It is
  geomorphic as well as visual — altered rock loses hardness, soluble bedrock
  dissolves, hydrolysis creates clay fines and salt crystallisation bursts
  exposed rock into sediment. Per-process rates and baking live on the layer
  inspector; the toolbar "Weathering" slider controls stain visibility on
  terrain + boulders. Disabled on the airless lunar theme. Flat mineral
  colours only — no noise grain, in keeping with the flat-colour pass.
- **Simulation seasons**: live processes no longer all run at once by default.
  Rain, snow/glaciers, wind, rockfall/boulders and chemical weathering are
  separate seasons that can be run **one at a time** or armed together in
  Simultaneous mode from the Simulation Seasons panel. Selecting a simulation
  layer arms its season; pressing **Simulate** runs it, and each simulation
  layer also has its own inspector trigger. Any stack/base-layer evaluation
  auto-pauses every season until Simulate is pressed again, so snow cannot
  interfere while shaping the base landform.
- **Mineral-bearing sedimentary layers**: Stratigraphic Beds now carry iron,
  copper, carbonate and salt/evaporite composition plus a target horizon.
  Stack evaluation bakes those into a mineral source field, chemical water
  leaches and transports dissolved Fe/Cu along flow paths, and boulders in
  those paths stain from the transported state. The **Mineral Strata &
  Dissolved Stain** shading mode visualises the source beds and stain trails.
- **Dedicated Wind Erosion layer**: wind no longer hides inside the
  snow/glacier layer. Its own simulation layer controls prevailing direction,
  speed, overall rate, rock abrasion, deflation, lee deposition, turbulent
  direction wander, gustiness, abrasive sand supply, saltation-curtain /
  undercut height, wrap-around streamlining and vertical fluting. The
  prevailing direction itself can also change through geologic time
  (`src/erosion/wind-regime.js`), but not as a sinusoidal windscreen-wiper
  sweep. **Fixed** keeps one bearing. **Variable wind rose** dwells in
  prevailing-weighted weather spells in a deterministic seeded sequence,
  then veers or backs smoothly through every intermediate bearing; larger
  turns take longer, so it never jumps from north to south between steps.
  The chosen prevailing sector receives the largest quota, adjacent sectors
  receive the next-largest quotas, and side/rear/opposite sectors still
  occur. Left and right sector pairs are quota-balanced. **Weather fronts**
  dwell once in each configured sector per cycle in a non-linear seeded
  order, with the same finite frontal transitions, and **Full wind rose**
  gives all eight sectors equal quotas. Terrain uniforms are recomputed
  every substep, so long bakes and stack replays really do spend time in
  those spells and along the intervening transition bearings; moving gust
  wander and obstacle wrap then act around the current effective direction.
  Utah defaults to five front sectors across ±70° over 720 steps. Local
  flow genuinely wraps around terrain — a multi-scale obstacle gradient
  blocks the into-hill wind component, so flow splits around massifs,
  follows contours, speeds along flanks and funnels through saddles and
  valley axes (Wrap slider at 0% restores straight over-the-top flow).
  Streamline compression accelerates it over stoss slopes and convex
  crests; the immediate lee is a separated, depositional wake, while flow
  farther downwind reattaches and regains exposure. Saltation now follows
  aeolian threshold/fetch behaviour: entrainment requires excess shear
  above a slope-dependent threshold, sand load grows over fetch and lags
  capacity changes across an 8–30 m saturation length, and erosion versus
  deposition follows the resulting upwind-to-local flux change. Abrasion
  acts along those wrapped streamlines (tangential flank/valley-wall
  sandblasting, reattached-wake abrasion and flow-aligned fluting), not
  only on head-on faces, so the carved pattern tracks the debug flow field,
  while soft/fractured
  strata abrade faster than hard caprock — replacing ruler-straight mountain
  grooves with curved corridors, fins and spire-like remnants. Wind settings
  can be changed between seasons without rebuilding the already-eroded
  terrain. The layer's **Wind Flow Debug** card previews exactly where the
  wind will go before you run it: a toggleable vector field over the terrain
  (same flow model as the simulation — prevailing vector, downwind-moving
  turbulent gusts, wrap-around terrain flow, stoss/crest speedup, lee
  separation and downwind reattachment) where arrow direction is the
  local flow and arrow length/colour is local speed (long/warm = exposed,
  erosive; short/cool = sheltered deposition; faint rings = blocked by
  water/snow). A live readout translates the azimuth slider into compass
  terms ("blows toward NE — a wind FROM SW", North = −Z, East = +X) and
  one-click **From N/NE/E/…** buttons set the wind to flow from a chosen
  direction. The **Utah Wind + Rain Canyon** preset is a rocky Utah stack with
  only Rain and Wind simulation layers (no snow, rockfall/boulders or
  chemical layers).
- **Spline Landform + sculpt/noise authoring**: a new parameterized smooth
  Catmull-Rom spline layer (separate from the legacy Spline Canyon/Road
  layer, so existing stacks are unchanged) uses the path as an influence
  field. **Mountain Ridge** adds noisy uplift, **Canyon** subtracts a
  noise-broken gorge, **River Channel** grades from start elevation to end
  elevation and seeds water/sediment along the authored flow direction,
  **Road Cut / Grade** cuts or fills toward a drivable longitudinal grade,
  and **SDF Tunnel** turns the spline into a continuous 3D tube in the cave
  volume that can pass through a ridge and open portals on both sides.
  Controls include width, amount, falloff, sharpness, noise, elevations,
  grade strength, sediment, banks, tunnel radius/depth/roughness, seed and
  fully editable control points. Spline points are true **3D**: select a
  point and drag it on the terrain or pull the viewport gizmo's **X / Y / Z**
  axis arrows (Y = absolute elevation), or edit X/Z/Y numerically in the
  inspector's point list (with insert/delete per point; 2–8 points). An
  authored point elevation re-grades the river/road/canyon locally, sets the
  ridge crest height, and steers the SDF tunnel bore up or down through the
  ridge; leaving Y empty keeps the automatic Start→End grade, so all
  existing presets are unchanged. The SDF Sculpt layer can now combine hand-painted
  basic shapes with seeded noise using **Add/Subtract**, **Multiply/Scale**,
  **Mix to Target Height** or **Max/Min Union-Clip**, at any point in the
  bottom-to-top stack. Two isolated test terrains exercise this without
  modifying the canonical landscapes: **Spline Canyon + River Test** and
  **Spline Road + Tunnel Test**.
- **Game export path**: the simulation/SDF stays in the editor. Terrain
  export bakes the heightfield and active SDF cave/tunnel mesh into one
  ordinary position/normal/UV/colour polygon mesh for `.glb`/`.obj`, omitting
  heightfield triangles in open portals so tunnels are real holes rather
  than shader cutouts. In a game engine, use those baked static meshes (with
  LODs/decimation and a simplified collision mesh) rather than raymarching a
  full terrain SDF at runtime. For Unreal-style workflows, broad open terrain
  can remain Landscape/heightfield while caves, arches, road cuts and spline
  tunnels ship as baked static-mesh inserts; engine-generated mesh distance
  fields can still accelerate AO/GI without paying for runtime voxel terrain
  on GTX-class GPUs.

- **Rock & Cliff Studio — Mesh → SDF erosion bake**: the generated placement
  mesh can now be converted back into a true narrow-band 3D SDF
  (`src/core/mesh-to-sdf.js`) without changing its world scale or grounded
  `Y=0` origin. Sign is recovered by scanline parity and near-surface
  distance by a triangle BVH, with a node-memory guard and full conversion
  stats (source triangles, effective grid, voxel size in the HUD/readout).
  Erosion resolution is independent of formation quality — Match source,
  192, 256, 320 or 384 cells along the longest span — so a fast/interactive
  formation can receive a hero-resolution erosion bake and then use the
  existing QEM targets for terrain-placement LODs. Terrain Studio's process
  laws are adapted honestly to overhangs/undersides in
  `src/geology/sdf-erosion-suite.js`: capacity-limited rain runoff, gusting
  wrap-around wind saltation with lee deposition, repose-gated thermal
  talus, settled-meltwater freeze–thaw, and wet-residence chemical
  dissolution (the existing Fe/Cu stain pass remains the colour layer). All
  share geological hardness, convex-edge protection, thin-span protection
  and per-node carve caps before Dual Contouring. Rock wind uses the same
  smooth weather-spell regimes as Terrain: each saltation grain keeps its
  current bearing for its short life while later grains sample later dwells
  and frontal transitions over the bake. The HUD and CLI report both the
  realized direction path and the actual particle share in all eight sectors, making a
  left/right bias visible instead of hiding it in an average. Rock & Cliff
  Studio exposes prevailing azimuth, regime, wind-rose spread/sector range
  and weather cycles over the bake alongside its rain/wind/thermal/cryo/chemical
  amounts. Tick **▶ Watch erosion live** to receive staged, anchored SDF
  preview meshes in the viewport while the worker continues; preview frame
  count and hold time are user sliders, previews are triangle-capped and
  never replace the final grounded export bake. An Analytic SDF source
  remains available for the fastest native path. CLI example:
  `--sdf-source mesh --erosion-res 320 --wind 0.45 --wind-shift sectors
  --wind-shift-range 80 --wind-shift-cycles 2 --thermal 0.25`.
- **Slot-canyon flash-flood erosion**: Sculpted Narrow Slot Canyon no longer
  relies only on sky-spawned rain and outside wind. Its builder now returns a
  world-space centreline and a real segmented bedrock floor with pool
  undulations and small knickpoint drops. After generic rain, the SDF suite
  sends sediment-laden flood packets through the corridor itself: frequent,
  competent and rare-deep stages wet different wall heights; bedload and
  suspended tools abrade upstream-facing walls, ribs and the bed; pinch
  reaches accelerate while swells/eddies deposit; helical secondary flow and
  turbulence produce wall/floor impacts; and a sediment-supply control moves
  between abrasive **tools** and protective **cover**. A lower-rate wind
  polish is channelled along the slot afterwards, reflecting wind's secondary
  role between floods. Rock Studio sliders set flash-flood amount, packet
  count and sediment supply; the HUD reports flood/wind grains, wall and bed
  impacts, and abraded/deposited volume. CLI example:
  `--archetype slot-canyon --rain 0.55 --slot-flood 0.85
  --slot-flood-particles 6000 --slot-sediment 0.62 --wind 0.35`. The slot's
  boundary-matched wall blocks also use an exact-min union now: sequential
  smooth-min over their overlaps was the source of the old waffle/louver flank.
- **Formation cleanup pass**: Stratified Sedimentary Cliff is now one broad
  continuous escarpment with a rim-clipped skyline and narrower toe bench;
  Sculpted Narrow Slot Canyon is a taller, thinner pair of fins around a
  visibly open S-curved corridor; Bryce Badlands Hoodoo Ridge uses irregular
  fused fins, offset foreground sentinels, phase-shifted Claron ribs and a
  grounded apron. Utah Canyon Wall & Alcove and Desert Caprock Mesa Butte
  remain available, but their broad artificial alcove/hole cuts are gone —
  Utah is now a solid wall with bedding ledges plus narrow joint chimneys, while
  the Butte relies on its recessed soft bed under the caprock.
- **Terrain resolution + calmer studio UI**: the header **Terrain grid**
  selector offers **256² Performance**, **512² Balanced** (the unchanged
  default), **768² High**, and **1024² Ultra**. Resolution is applied at
  studio boot — the selector saves the choice and reloads with
  `?terrainRes=<size>` — so terrain height/water/sediment, snow, chemistry,
  mineral, rock-obstacle, sculpt, stack and render-mesh buffers are all
  allocated at exactly the same size instead of being resized underneath a
  live GPU pipeline. A one-time validated session snapshot restores the
  current recipe, layer stack, spline points, settings, cave strokes and
  camera after the reload (live erosion progress and GPU height-sculpt
  stamps reset with the reallocated textures). At the default 500 m span
  those grids are roughly
  1.95 m, 0.98 m, 0.65 m and 0.49 m per cell; 768² has 2.25× and 1024² has
  4× the 512² cell/VRAM workload. The editor chrome now uses near-black
  green glass panels, pill navigation and compact horizontal inspector
  controls; terrain recipes and global shading start collapsed, and the
  header **Focus** button hides both side docks to give the 3D viewport the
  full workspace.

Verification: `npm test` — 57/57 pass (includes `tests/rain-erosion.test.mjs`,
`tests/mesh-sdf-erosion.test.mjs` for mesh→SDF sign/distance/resolution,
directional wind, the full 3D erosion suite and grounded placement export,
`tests/wind-regime-preview-formations.test.mjs` for smooth shifting prevailing
wind, multi-face Rock wind abrasion, watchable SDF preview frames and the
five named formation fixes,
a static GLSL declaration-before-use scope check over every shader (the mock
WebGL in Node cannot compile GLSL, so this guards against browser-only
"undeclared identifier" black screens),
rigid-body free-fall/no-elevator tests, exact-mesh seating / pause-freeze,
simulation-season / mineral-strata, dedicated wind-erosion + wind-flow
debug vector field, spline
landform / sculpt-noise / SDF-through-tunnel, 3D spline-point editing
(gizmo + per-point elevations), and terrain-resolution / decluttered-UI
regression tests).
`npm run build` clean.

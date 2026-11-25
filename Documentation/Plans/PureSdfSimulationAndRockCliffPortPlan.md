# Pure-SDF Simulation Nodes, Thermal Fracture/Cracking, Reactive Geochemistry & Rock/Cliff Generator Port Plan

## 1. Objective & Cloned Reference Snapshot

- **Reference Snapshot Cloned in Repo**: `Documentation/Plans/Reference-5079e133-TerrainStudio/`
  - Exact source snapshot of [`eosclient0001-rgb/Frontier@5079e133744eef7bb3a8bd22cab148d1fc4e3325`](https://github.com/eosclient0001-rgb/Frontier/commit/5079e133744eef7bb3a8bd22cab148d1fc4e3325) (`Experimental/TerrainStudio/`, 74 files).
- **Target Destination**: `Experimental/TerrainWorldEditor/` (our zero-build, multi-region **Open-World SDF Terrain Studio** where large worlds are partitioned into small, manageable, individually-simulatable sub-world `TerrainRegion` tiles).
- **Core Architectural Mandate**:
  1. Port the **composable Simulation Stack Nodes, Simulation Seasons, Authored Mineral-Bearing Stratigraphic Columns, Wind-Flow Vector Field Debug, 6-DOF SDF Boulder Calving/Embedding, and 18+1 Archetype Procedural 3D Rock & Cliff Generator** from `5079e133` into `Experimental/TerrainWorldEditor/`.
  2. Keep the entire pipeline **100% Pure SDF** (Thin-Slab 2.5D SDF per sub-world + Sparse Narrow-Band 3D SDF Bricks for cliffs, overhangs, caves, arches, tunnels, and boulders) — **eliminating `SDF -> Mesh -> SDF` (`convertMeshToSdf`) completely**.
  3. Solve the two physics fidelity gaps that `5079e133` and `ErosionPhysicsEngine.js` only approximated:
     - **True Pure-SDF Brittle Fracturing & Thermal-Expansion Cracking** (macro jigsaw block cleaving + diurnal insolation stress tensor + 90° T-junction hierarchical thermal crazing + onion-skin exfoliation spalling).
     - **Realistic Reactive-Transport Geochemical Erosion** (mineral saturation thermodynamics $\Omega = C / C_{\text{eq}}$, carbonate karst flutes + travertine reprecipitation, 3D spheroidal corestone hydrolysis, Correns salt-crystallization tafoni overhang hollowing + case-hardening, and SDF-tangent mineral seepage).

---

## 2. Answering the Core Question: Can True Brittle Fracturing & Thermal Cracking Be Done Purely in SDF (Without `SDF -> Mesh -> SDF`)?

**Yes — 100% purely in SDF, faster and sharper than `SDF -> Mesh -> SDF`.**

### Why `5079e133` Had `convertMeshToSdf` (`src/core/mesh-to-sdf.js`) — And Why We Don't Need It
In `5079e133`, `generator.js` evaluated an analytical + half-space polyhedron SDF (`evaluateSparseSdf`), polygonized it into a triangle mesh via Dual Contouring + island cleanup, and then — when the user requested 3D rock erosion — ran `convertMeshToSdf` (scanline parity + triangle BVH) to rebuild a second 3D SDF grid just so `sdf-erosion-suite.js` could erode it before running Dual Contouring a *second* time. That `SDF -> Mesh -> SDF -> Mesh` detour costs $O(N^3 \log T)$ BVH queries, rounds sharp fracture edges twice, and burns hundreds of megabytes at `320³–384³`.

In our pure-SDF architecture, **polygonization (QEF Dual Contouring / Dual Surface Nets) happens strictly ONCE at the very end for WebGL2 display and `.GLB`/`.OBJ`/`.STL` export**. All fracturing, block tilting, thermal cracking, chemical hollowing, and rain/wind/slot-flood erosion operate directly on the **Composite SDF**:

$$\phi(\mathbf{p}) = \phi_{\text{analytic + strata + fracture}}(\mathbf{p}) + \Delta\phi_{\text{brick}}(\mathbf{p})$$

### Four Pure-SDF Fracture & Cracking Mechanisms (Macro Chasms $\to$ Micro Thermal Crazing)

#### Mechanism A — Kinematic Domain-Pullback Cleaving ("Jigsaw Half-Space Pullback")
Used for **split monoliths, frost-wedged summit chasms, stepped en-echelon fault blocks, and tilted caprock slabs** (upgrading `cracks-demo.js` & `polyhedron.js` to arbitrary terrain cliffs and hero rocks):

1. Define a geological fracture surface as a warped plane in 3D world space:
   $$S(\mathbf{p}) = \mathbf{n} \cdot (\mathbf{p} - \mathbf{p}_0) + A_{\text{plumose}}\,\psi_{3\text{D}}(\mathbf{p}) = 0$$
   Because **both** sides evaluate the exact same continuous 3D correlated noise $\psi_{3\text{D}}(\mathbf{p})$ (plumose hackle + en-echelon steps), opposing walls are guaranteed to match **1-to-1 like an interlocking jigsaw puzzle**.
2. Suppose thermal heave, frost wedging, or basal undercutting rotates and translates the hanging-wall block around a basal hinge $\mathbf{h}$ by rigid transform $T(\mathbf{x}) = \mathbf{h} + \mathbf{R}(\mathbf{x} - \mathbf{h}) + \mathbf{t}$ (so the fissure widens naturally upward while remaining hinged at depth).
3. Instead of meshing and splitting triangles, evaluate the parent SDF $\phi_0$ via **exact inverse rigid pullback** $T^{-1}\mathbf{p} = \mathbf{h} + \mathbf{R}^\top(\mathbf{p} - \mathbf{h} - \mathbf{t})$:
   $$\phi_{\text{foot}}(\mathbf{p}) = \max_{\text{chamfer}(r)}\!\Bigl(\phi_0(\mathbf{p}),\; +S(\mathbf{p}) + \tfrac{1}{2}g_0\Bigr)$$
   $$\phi_{\text{hang}}(\mathbf{p}) = \max_{\text{chamfer}(r)}\!\Bigl(\phi_0(T^{-1}\mathbf{p}),\; -S(T^{-1}\mathbf{p}) + \tfrac{1}{2}g_0\Bigr)$$
   $$\phi_{\text{cleaved}}(\mathbf{p}) = \min\bigl(\phi_{\text{foot}}(\mathbf{p}),\; \phi_{\text{hang}}(\mathbf{p})\bigr)$$
   where $\max_{\text{chamfer}(r)}(a, b)$ is the exact $45^\circ$ brittle-stone edge bevel from `polyhedron.js`:
   $$\max_{\text{chamfer}(r)}(a, b) = \begin{cases} \frac{a + b + r}{\sqrt{2}} & \text{if } a > -2r \text{ and } b > -2r \text{ and } \frac{a+b+r}{\sqrt{2}} > \max(a,b), \\ \max(a, b) & \text{otherwise.} \end{cases}$$
4. **Recursive Multi-Block Scaling (`GeologicalBspSdf`)**: For $K$ hierarchical splits (conjugate joint blocks, Y-forking summit horns), store a compact flat **3D BSP tree of half-spaces + per-leaf rigid transforms $(T_k^{-1}, \text{AABB}_k)$**. Brick culling (`buildBrickContext`) discards non-overlapping leaves per $8^3$ SDF brick, so inner-loop evaluation touches only 1–3 leaves per voxel with **zero mesh conversion**.

#### Mechanism B — Thermal Skin-Depth Attenuated 3D Joint-Sheet Subtraction (V-Tipped Cracks)
Naive boolean subtraction of infinite Voronoi sheets slices rocks all the way through into floating islands. Real thermal & frost cracks open at the **exposed rock skin** and pinch closed at the **thermal skin depth** $\delta_T = \sqrt{2\kappa / \omega}$ ($\approx 0.12\text{–}0.45\text{ m}$ for diurnal cycles; $\approx 2.5\text{–}4.5\text{ m}$ for annual seasonal cycles):

1. Let $\phi_0(\mathbf{p}) \le 0$ be signed depth inside the uncracked rock ($-\phi_0(\mathbf{p})$ = distance inward from the exposed cliff/boulder face!).
2. Let $w(\mathbf{p}) \ge 0$ be metric distance to the nearest 3D joint/crack sheet (e.g. $w = \frac{1}{2}(F_2 - F_1)$ on a stress-warped 3D Voronoi or conjugate strike/dip lattice).
3. Attenuate crack half-aperture $a(\mathbf{p})$ exponentially with inward SDF depth:
   $$a(\mathbf{p}) = a_{\text{surf}}(\mathbf{p}) \cdot \exp\!\Bigl(\frac{\phi_0(\mathbf{p})}{\delta_T}\Bigr)$$
4. Carve the crack purely in SDF:
   $$\phi_{\text{cracked}}(\mathbf{p}) = \max_{\text{chamfer}(r_{\text{lip}})}\!\Bigl(\phi_0(\mathbf{p}),\; a(\mathbf{p}) - w(\mathbf{p})\Bigr)$$
   - At the rock face ($\phi_0 = 0$), the crack opens to full width $2 a_{\text{surf}}$ with crisp beveled lips $r_{\text{lip}}$.
   - Inside the rock ($\phi_0 < 0$), $a(\mathbf{p}) \to 0$, so the fissure **naturally terminates in a sharp V-tip** at exact depth $z_{\text{tip}} = \delta_T \ln(a_{\text{surf}} / w_{\min})$ while keeping the rock core 100% solid and connected.

#### Mechanism C — Diurnal Insolation Thermal-Stress Tensor, 90° T-Junction Polygonal Crazing & Onion-Skin Exfoliation
Why current thermal cracking looks approximate: `ErosionPhysicsEngine.js` currently accumulates a scalar `crack` number per 2D cell and subtracts random scalar `Depth` (`H[I] -= Depth`), which makes isolated dimples rather than **connected polygonal crack networks, directional sun-split boulders, or curved exfoliation plates**.

We upgrade thermal cracking to a **3-mode physical SDF fracture law**:

1. **Directional Insolation & Thermal Shock Forcing**:
   - Compute incident solar flux over the diurnal arc against the true 3D SDF surface normal $\mathbf{N} = \nabla\phi / \|\nabla\phi\|$, plus rapid evening/rainstorm cooling rate $\dot{T}$ (thermal shock!).
   - Biaxial surface tensile/compressive stress in the tangent plane:
     $$\sigma_{\parallel}(\mathbf{p}) = \frac{E_{\text{bed}} \, \alpha_{\text{bed}} \, \Delta T_{\text{diurnal}}(\mathbf{N}, \text{shadow})}{1 - \nu} + \sigma_{\text{quartz-mismatch}}$$
     (granite/arkose with high quartz-feldspar differential expansion $\alpha_Q \ne \alpha_F$ fatigues $2.4\times$ faster than homogeneous basalt; dark low-albedo basalt heats $1.5\times$ more on sun-baked SW/NW faces).
   - Subcritical crack growth (Paris / Charles stress-corrosion law enhanced by pore moisture $M$, matching Eppes & Keanini 2017):
     $$\frac{da_{\text{surf}}}{dt} = k_{\text{th}} \, (1 + \beta_M M) \, \max\!\Bigl(0,\; \frac{\sigma_{\parallel} \sqrt{\pi a} - K_{\text{th}}}{K_{Ic}}\Bigr)^m$$
2. **Hierarchical 90° T-Junction Thermal Crazing (2-Tier Conditional Voronoi SDF)**:
   - Standard Voronoi $F_2 - F_1$ makes $120^\circ$ hexagonal columns (great for cooling basalt, wrong for thermal insolation & mud/playa crazing where secondary cracks meet older cracks at **$90^\circ$ T-junctions** because stress normal to the primary crack is already zero!).
   - **Pure-SDF T-Junction Formula**:
     - Evaluate primary 3D Voronoi cell $(\text{id}_1, F_1, F_2, \mathbf{g}_1 = \nabla w_1)$ with primary sheet distance $w_1 = \frac{1}{2}(F_2 - F_1)$.
     - Inside cell $\text{id}_1$, construct a local tangent axis $\mathbf{u}_2 \perp \mathbf{g}_1$ seeded deterministically from $\text{hash}(\text{id}_1)$ and evaluate secondary slice distance $w_2(\mathbf{p}) = |\mathbf{u}_2 \cdot (\mathbf{p} - \mathbf{c}_1) + \text{warp}|$.
     - Because $\mathbf{u}_2 \perp \mathbf{g}_1$ and $\text{id}_1$ switches across primary walls, **every secondary crack automatically terminates dead-on against primary cracks at $90^\circ$ T-junctions**!
     - Primary cracks open at lower damage ($D_{\text{crack}} > 0.25$); secondary T-cracks nucleate only at higher fatigue ($D_{\text{crack}} > 0.55$), producing authentic multi-order desert rock crazing.
3. **Curvature-Driven Onion-Skin Exfoliation Sheeting (Granite Domes & Boulders)**:
   - On convex rock surfaces ($\kappa = \nabla^2 \phi_0 > \kappa_{\text{crit}}$), surface-parallel compressive heating induces **radial tensile delamination** parallel to the surface itself at sheet spacing $L_{\text{sheet}} \in [0.18, 0.90]\text{ m}$:
     $$w_{\text{sheet}}(\mathbf{p}) = \Bigl|\operatorname{fract}\!\Bigl(\frac{-\phi_0(\mathbf{p})}{L_{\text{sheet}}}\Bigr) - 0.5\Bigr| L_{\text{sheet}}$$
   - Gated by boundary joint openings, this carves **undercut concentric exfoliation visors and curved granite spall scars** directly in the 3D SDF!

#### Mechanism D — Mesh-Free 3D SDF Overhang Failure & Rigid-Body Boulder Calving
When undercutting (wind saltation, river scour, or thermal/frost crack propagation) severs or over-stresses an overhanging block inside a 3D SDF brick:
1. Run an **$O(N)$ 6-connected `phi < 0` integer flood-fill** seeded from the grounded base slab of the brick (plus a neck cross-section tensile check $\rho g V_{\text{overhang}} > \sigma_t A_{\text{neck}}$) — **operating directly on the `Float32Array` SDF brick, zero meshing**.
2. Any ungrounded `phi < 0` voxel component $C$ is carved out of the terrain brick ($\phi_{\text{brick}} \leftarrow \max(\phi_{\text{brick}}, -\phi_C)$), exposing a fresh unweathered fracture scar (`alteration = 0`, `stain = 0`).
3. Its volume, center of mass, principal inertia tensor, and `24³` CoM-aligned local SDF are integrated directly from $C$ and handed to `RockCalvingEngine` (ported from `5079e133`'s `src/erosion/rock-calving.js`), which tumbles the boulder downslope at 120 Hz using **SDF-vs-SDF narrowphase contact manifolds** and crushable snow/sediment bearing physics!

---

## 3. Upgrading Chemical Erosion from Approximate to Reactive-Transport Geochemistry

### Diagnosis of Current Limitations
| Aspect | Current `ErosionPhysicsEngine.js` | `5079e133` (`CHEM_WEATHER_FRAG` + `chemical-staining.js`) | Planned Pure-SDF Geochemical Solver |
|---|---|---|---|
| **Lithology & Mineralogy** | Single scalar `carbonateShare` + noise hardness | **Strong**: 16 authored dipping beds (`STRATA_LITHOLOGIES`: Fe, Cu, $\text{CaCO}_3$, Salt, Clay, Porosity) | Port 16-bed 3D non-repeating **Dipping + Faulted Stratigraphic Column** shared by 2.5D & 3D SDF bricks |
| **Thermodynamic Saturation ($\Omega$)** | None (infinite linear dissolution, never saturates or precipitates) | None (`chemGrow` exponential saturation of visual coating only) | Track dissolved ion loads $(C_{\text{Ca}}, C_{\text{Fe}}, C_{\text{Cu}}, C_{\text{Salt}}, C_{\text{Si}})$ vs equilibrium $C_{\text{eq}}(T, p_{\text{CO}_2})$ so $\Omega < 1$ dissolves & $\Omega > 1$ **precipitates travertine / calcrete / evaporite** |
| **3D Cliff / Overhang Chemistry** | 2.5D heightfield only | Post-process on **welded triangle mesh** (`chemical-staining.js`), non-geomorphic | **True 3D SDF Geomorphic + Staining Pass**: capillary salt hollowing (tafoni), silica case-hardening, rillenkarren flutes, and tangent-projected $\mathbf{g}_\parallel$ Fe/Cu seepage directly on SDF |
| **Spheroidal Weathering** | None | Scalar `alteration` softening | 3D joint-corner accelerated hydrolysis front $\to$ emergent **rounded granite corestones & tors** |

### Five Coupled Pure-SDF Geochemical Processes (`GeochemicalSdfEngine.js`)

1. **Authored 3D Dipping & Faulted Stratigraphic Column (`StratigraphicColumn3D.js`, ported & expanded from `src/erosion/stratigraphic-column.js`)**:
   - Up to 16 non-repeating beds (`shale`, `mudstone`, `siltstone`, `sandstone`, `arkose`, `limestone`, `dolomite`, `marl`, `conglomerate`, `evaporite`, `basalt`, `gneiss`) evaluated at true 3D stratigraphic coordinate:
     $$\zeta(\mathbf{p}) = p_y + (p_x \sin\theta_{\text{strike}} + p_z \cos\theta_{\text{strike}})\tan\theta_{\text{dip}} + \Delta y_{\text{fault}}(\mathbf{p}) + \psi_{\text{fold}}(\mathbf{p})$$
   - Supplies exact `(hardness, porosity, fractureDensity, clay, iron, copper, carbonate, salt, linearRgb)` at any $(x, y, z)$ on both Thin-Slab heightfields and 3D Volumetric SDF bricks.

2. **Carbonate Karst Dissolution & Travertine Rimstone Precipitation ($\text{CaCO}_3\text{–H}_2\text{O–CO}_2$ System)**:
   - Transition-State Theory kinetic law driven by soil-respired $p_{\text{CO}_2}$ (higher under vegetated/soil-mantled benches) and cold-water $\text{CO}_2$ solubility (inverse temperature dependence of calcite!):
     $$R_{\text{calcite}} = k_{\text{karst}} \, f_{\text{carbonate}}(\zeta) \, q_{\text{runoff}}^{0.6} \, \bigl(1 - \Omega_{\text{Ca}}\bigr)|1 - \Omega_{\text{Ca}}|^{n-1}, \qquad \Omega_{\text{Ca}} = \frac{C_{\text{Ca}}}{C_{\text{eq}}(T, p_{\text{CO}_2}, \text{grad})}$$
   - **Undersaturated ($\Omega_{\text{Ca}} < 1$)**: Carves sharp vertical **rillenkarren dissolution flutes** on bare steep limestone, flat-floored **kamenitza solution pans** in depressions, and widens tectonic joints into **clint-and-grike limestone pavements & phreatic cave conduits**.
   - **Supersaturated ($\Omega_{\text{Ca}} > 1$)**: Rapid turbulent $\text{CO}_2$ degassing over steep waterfall knickpoints and warm spring outlets drops $C_{\text{eq}}$, precipitating **positive SDF travertine dams & rimstone terraces** ($\Delta\phi < 0$).

3. **Silicate Hydrolysis & Spheroidal Corestone Emergence**:
   - Chemical alteration front advances into silicate beds (`arkose`, `gneiss`, `basalt`, `sandstone`) as a function of Arrhenius temperature $\exp(-E_a / RT)$, pore water residence, and local 3D joint density:
     $$\frac{d A_{\text{sap}}}{dt} = k_{\text{hyd}} \, e^{-E_a / R(T - T_0)} \, f_{\text{silicate}}(\zeta) \, \theta_{\text{pore}} \, (1 + 2.5 \, J_{3\text{D}}(\mathbf{p})) \, (1 - A_{\text{sap}})$$
   - Because 3D orthogonal joint intersections have $3\times$ surface-area-to-volume exposure ($J_{3\text{D}}$ peaks at cube corners and edges!), block corners convert to soft clay saprolite (`hardness` drops from $0.88 \to 0.14$) while block interiors remain hard granite (`0.88`). Subsequent rainwash strips the saprolite, naturally revealing **spheroidally rounded granite tors & corestones**!

4. **Correns Salt Crystallization Hollowing (3D Tafoni / Alveolar Honeycombs) & Outer Case-Hardening**:
   - Porous sandstone/marl beds wick saline pore water (`porosity * salt`) to evaporation fronts. Where **evaporation exceeds liquid flushing** — specifically **shaded, wind-ventilated cliff alcoves and overhang undersides** ($\mathbf{N}_y < 0.15$) — sub-florescence crystallization pressure $\Delta P_{\text{salt}} = \frac{RT}{V_m}\ln(C_{\text{salt}}/C_{\text{sat}})$ exceeds rock tensile strength, flaking grain-by-grain upward into **cavernous 3D SDF tafoni & honeycomb alveoles**.
   - Simultaneously, dissolved silica/iron drawn to the sun-baked exterior face precipitates a **case-hardened crust** (`crustHardness += 0.25` in the outer $5\text{ cm}$), explaining why natural desert arches & tafoni keep thin, resilient outer shells over deeply hollowed interiors!

5. **3D SDF Tangent-Projected Mineral Seepage ($\text{Fe}$ Rust Bleeding, $\text{Cu}$ Malachite/Azurite, $\text{Mn}$ Desert Varnish)**:
   - Replaces mesh-dependent `computeChemicalStains` (`src/geology/chemical-staining.js`) with **SDF-native surface film routing**:
     - Seed dissolved $\text{Fe}^{2+}$ and $\text{Cu}^{2+}$ at intersections of mineral-bearing horizons ($\zeta$) with open fractures ($w(\mathbf{p}) < a(\mathbf{p})$) and concave seep niches ($\nabla^2 \phi > 0$).
     - Route thin water films down both 2.5D terrain and 3D overhang/cliff bricks along $\mathbf{v}_{\text{seep}} = \mathbf{g} - (\mathbf{g}\cdot\mathbf{N})\mathbf{N}$ (detaching as vertical drip lines when $\mathbf{N}_y < -0.35$!).
     - Precipitate ochre/hematite $\text{Fe}_2\text{O}_3$ streaks upon oxygenation, green/blue basic copper carbonate ($\text{Cu}_2\text{CO}_3(\text{OH})_2$) where Cu-bearing runoff crosses carbonate beds, and slow $\text{Mn/Fe}$ **desert varnish** on stable, un-spalled arid rock faces.

---

## 4. Porting All `5079e133` Simulation Nodes & Seasons to Open-World Sub-Worlds

In `5079e133` (`src/erosion/terrain-stack-presets.js` & `terrain-stack-pipeline.js`), simulations are **first-class composable nodes in the Layer Stack** paired with **Simulation Seasons**. We will bring this exact workflow into `Experimental/TerrainWorldEditor` — upgraded to work per **manageable `TerrainRegion` sub-world** (Solo 1-by-1 OR Shared-Weather Group with automatic `StitchAdjacentRegionBorders`!).

### Stack Node Catalog to Add to `TerrainLayerStack.js` & Inspector

| New Stack Node `type` | Source in `5079e133` | Pure-SDF Open-World Behavior in `TerrainWorldEditor` |
|---|---|---|
| `stratigraphic-beds` | `stratigraphic-column.js`, `MINERAL_ACCUM_FRAG` | Authored 16-bed non-repeating geological section (12 lithologies, dip/strike, fault throw, Fe/Cu/$\text{CaCO}_3$/Salt composition). Writes 3D bed lookup used by both Thin-Slab heightfield & 3D volumetric cliff/cave bricks. |
| `cliff-generator` & `rock-stacks` | `LAYER_OPERATION_SPECS['cliff-generator']`, `['rock-stacks']` | Procedural escarpment, caprock cornice, and hoodoo/spire landform nodes with 3D overhang brick promotion where undercuts form. |
| `sim-hydraulic` | `gpu-erosion-engine.js`, `rain-erosion.js`, `sdf-erosion-suite.js` | Froude-limited Mei pipe runoff + stream-power incision + **Slot-Canyon Helical Flash-Flood** option; runs on 2.5D sub-world AND projects 3D droplet runoff over any active 3D SDF cliff/arch bricks in the region. |
| `sim-cryo-frost` | `frost-weathering.js` (`SNOW_EFFECT_TYPES`) | All 6 modular cryosphere processes (`segregation-ice` $-8\dots-3^\circ\text{C}$ lens growth, `volumetric-freeze-thaw`, `nivation-snowpatch`, `frost-creep-solifluction`, `avalanche-scour`, `glacier-plucking`) + wind-scoured/lee-drifted snowpack. |
| `sim-thermal-fracture` | `cracks-demo.js` + **New Pure-SDF Fracture Engine** | Diurnal insolation $\Delta T$ stress tensor + Mechanism A/B/C pure-SDF joint opening, 90° T-junction thermal crazing, exfoliation sheeting, and repose-gated talus aprons. |
| `sim-wind` | `wind-regime.js`, `wind-field-debug.js` | Shifting 8-sector weather-spell wind regimes, terrain deflection, stoss speedup / lee separation, Bagnold saltation, basal canyon undercutting + **toggleable 3D Wind Flow Debug arrow overlay & compass readout**. |
| `sim-chemical` | `CHEM_WEATHER_FRAG` + **New Reactive Geochemistry** | Carbonate karst dissolution + travertine precipitation, 3D spheroidal corestone hydrolysis, Correns salt-tafoni alcove hollowing + case-hardening, and Fe/Cu/Mn seepage. |
| `rock-placement` / `rock-calving` | `rock-calving.js` (`ROCK_SDF_RES = 24`) | 6-DOF 3D SDF rigid-body boulders (`2700 kg/m³`) calved from undercut cliffs or scattered on talus aprons; crushable snow/sediment bearing sinkage + **two-way `rockObstacle` feedback** (streams divert/pond around boulders; talus & snow bank behind boulders). |
| `rock-formation` | `formations.js`, `sedimentary-formations.js`, `arch-curves.js`, `cracks-demo.js` | Stamps or spline-sweeps any of the **19 procedural 3D SDF Rock & Cliff archetypes** directly into the sub-world's 3D SDF brick system! |

### Simulation Seasons Workflow (`SimulationSeasonsController`)
- Port `5079e133`'s **Simulation Seasons** bar into the `Simulation` tab & viewport HUD:
  - Arm **Rain**, **Snow & Frost**, **Wind**, **Thermal Fracture & Rockfall**, and **Chemical Weathering** **one season at a time** (e.g., shape desert canyon with Rain $\to$ switch to Wind season with Wind Flow Debug arrows $\to$ finish with Thermal Fracture & Chemical Tafoni/Varnish) **OR** run all armed seasons **Simultaneously** driven by the seasonal calendar!
  - Editing any upstream base landform / spline / stratigraphic bed automatically pauses active live seasons and marks downstream simulation nodes stale via `ComputeRegionBaseSignature` until re-simulated.

---

## 5. Adding the 18+1 Archetype Procedural Rock & Cliff Generator to `TerrainWorldEditor`

### Source Modules to Port (Zero-Build ES Modules, Zero Vite/NPM Runtime Dependency)
From `Documentation/Plans/Reference-5079e133-TerrainStudio/src/` into `Experimental/TerrainWorldEditor/Engine/RockCliff/`:

1. **`PolyhedronAndFractureSdf.js`** *(from `geology/polyhedron.js` + `geology/cracks-demo.js` + new `GeologicalBspSdf`)*:
   - Exact chamfer-beveled half-space polyhedra (`evalCompiledBlock`), oriented eolian ellipsoids, recursive geological BSP fracture cleaving, kinematic hinge rotation/heave pullback, and 90° T-junction + exfoliation crack SDF operators.
2. **`ArchCurvesSdf.js`** *(from `geology/arch-curves.js` + `geology/custom-arch-spline.js` + `geology/formation-splines.js`)*:
   - True 3D cubic Bézier / Catmull-Rom parallel-transported Frenet-frame sandstone arches (`desert-arch`, `double-arch`, `sea-arch`) with crown taper, polygonal fin cross-sections, and stepped bedding pedestals — directly steerable by our existing **3D XYZ Up/Down Viewport Gizmo**!
3. **`RockCliffArchetypes.js`** *(from `geology/presets.js`, `geology/formations.js`, `geology/sedimentary-formations.js`, `geology/frontier-formations.js`, `geology/tectonics.js`, `geology/surface-detail.js`)*:
   - All **19 3D SDF Archetypes**:
     - **Arches (3)**: *Utah Sandstone Arch*, *Double Sandstone Arch*, *Coastal Sea Arch*
     - **Sedimentary & Canyon (7)**: *Stratified Sedimentary Cliff (17 horizons)*, *Utah Canyon Wall*, *Sculpted Narrow Slot Canyon*, *Desert Spires*, *Bryce Hoodoo Columns*, *Sandstone Caprock Butte*, *Balanced Pedestal Rock*
     - **Cliffs (3)**: *Jointed Cliff Face*, *Granite Exfoliation Dome*, *Columnar Basalt Cliff*
     - **Outcrops & Monoliths (5)**: *Limestone Karst Tower*, *Alpine Crag Spire*, *Weathered Granite Tor*, *Dipping Slate Outcrop*, *Cracked Jigsaw Monolith (`cracked-monolith`)*
     - **Boulders (1 + Atlas)**: *Fractured Hero Boulder* + `24³` SDF scatter variants.
4. **`PureSdfRockErosion.js`** *(upgraded from `geology/rain-erosion.js` & `geology/sdf-erosion-suite.js` — **NO `mesh-to-sdf.js`!**)*:
   - Evaluates `SparseSdf` (`8³` narrow-band bricks) directly from `RockCliffArchetypes.js` and runs **3D SDF Rain Droplets, Weather-Spell Wind Saltation, Thermal Talus, Settled-Meltwater Frost Wedging, Slot-Canyon Helical Flash Floods, and 3D Reactive Geochemistry** directly on the narrow-band SDF bricks (with convex-edge curvature protection, thin-arch span protection, and narrow-band Eikonal redistancing).
5. **`QefDualContourMesher.js` & `GlbExporter.js`** *(from `core/qef.js`, `core/dual-contour.js`, `core/mesh-post.js`, `io/glb-exporter.js`)*:
   - Replaces smooth centroid averaging on sharp rock formations with **Tikhonov-regularized $3\times 3$ SVD/QR Quadric Error Function (QEF) Dual Contouring**, preserving razor-sharp fracture dihedrals, columnar basalt hexagons, and sedimentary ledges!
   - Adds **Binary glTF 2.0 (`.GLB`) Export** (with QEM LOD decimation + baked RGBA `AO / Cavity / ConvexEdge / Slope` + `Fe/Cu/Mn` vertex attributes) alongside our existing `.OBJ` and `.STL` exporters!

### Seamless Open-World Integration (Two Workflows in One Studio)
1. **In-World Sub-World Placement (`+ Add Rock / Cliff Formation` in `TerrainWorldEditor`)**:
   - Drop any of the 19 archetypes onto any `TerrainRegion` sub-world, drag/rotate/scale or curve-rig it with the **3D XYZ Gizmo**, blend its pedestal smoothly (`smin`) into the Thin-Slab terrain, and let sub-world **Rain, Wind, Frost, Thermal Fracture, and Chemical Weathering** erode the cliff and shed talus/boulders directly onto the surrounding terrain!
2. **Focused "Rock & Cliff Lab" Workspace Mode (`2D Map | Split | 3D World | Rock & Cliff Lab` in Top Bar)**:
   - Inspect and author a single hero cliff/arch/monolith at up to `320³–384³` narrow-band SDF resolution with live staged SDF preview frames (`▶ Watch Erosion Live`), then click **"Stamp to Active Terrain Region"**, **"Send to Boulder Scatter Atlas"**, or **"Export .GLB + LODs"**.
   - Runs heavy `256³–384³` bakes inside a zero-build ES Module `Worker` (`RockCliffWorker.js`) so the 60 FPS WebGL2 viewport never stalls.

---

## 6. Phased Implementation Plan & Verification Gate (For Execution Turn)

### Phase 1 — Stratigraphic Mineral Column & Reactive-Transport Geochemistry (`Engine/StratigraphicColumn3D.js`, `Engine/GeochemicalSdfEngine.js`)
- Port 12 lithologies & 16-bed non-repeating 3D dipping/faulted column.
- Implement $\Omega_{\text{Ca}}$ karst dissolution + travertine rimstone precipitation, 3D spheroidal corestone hydrolysis, Correns salt-tafoni overhang hollowing + outer silica case-hardening, and 3D SDF tangent-projected Fe/Cu/Mn seepage.

### Phase 2 — Pure-SDF Brittle Fracture, Thermal-Stress Crazing & 6-DOF SDF Boulder Calving (`Engine/PureSdfFractureEngine.js`, `Engine/RockCalvingEngine.js`)
- Implement `GeologicalBspSdf` kinematic domain-pullback cleaving (jigsaw matching walls + basal hinge tilt), skin-depth attenuated V-tip cracks ($a(\mathbf{p}) = a_{\text{surf}}e^{\phi_0/\delta_T}$), 90° T-junction 2-tier conditional Voronoi thermal crazing, convex onion-skin exfoliation sheeting, and mesh-free 6-connected SDF overhang calving into `24³` 6-DOF SDF boulders with two-way `rockObstacle` flow/talus deflection.

### Phase 3 — Composable Stack Simulation Nodes, Frost Effects, Wind Debug & Simulation Seasons (`Engine/TerrainLayerStack.js`, `Engine/WindFieldDebug.js`, `TerrainWorldEditor.js`)
- Promote `stratigraphic-beds`, `sim-hydraulic` (+ slot-flood), `sim-cryo-frost` (6 snow/frost effects), `sim-thermal-fracture`, `sim-wind` (+ 3D Wind Flow Debug vector arrows & compass readout), `sim-chemical`, and `rock-placement` to composable per-region stack nodes with Solo 1-by-1 + Shared-Weather Group + Simulation Seasons controls.

### Phase 4 — 19-Archetype Pure-SDF Rock & Cliff Generator + QEF Dual Contouring + `.GLB` Export (`Engine/RockCliff/*`)
- Port all 19 archetypes + `PureSdfRockErosion.js` (zero `Mesh -> SDF`!), `QefDualContourMesher.js`, `GlbExporter.js`, Web Worker background baker, in-world `rock-formation` layer + `Rock & Cliff Lab` workspace view.

### Phase 5 — Automated Headless Validation (`TerrainWorldValidation.mjs` Suites 20–27)
- **Suite 20**: Pure-SDF Jigsaw Kinematic Cleaving (`0` mesh conversions, exact 1-to-1 opposing wall correlation, chamfered fracture rims, finite $|\nabla\phi| \approx 1$).
- **Suite 21**: Diurnal Thermal-Stress Tensor + 90° T-Junction Crazing + Skin-Depth V-Tip Termination (cracks open on sun-heated skin and terminate cleanly at $\delta_T$ with zero artificial floating islands).
- **Suite 22**: Reactive-Transport Geochemistry (undersaturated karst carves flutes; supersaturated waterfall lip precipitates positive travertine; shaded overhang hollows salt tafoni while case-hardening exterior; spheroidal joint corners round into corestones).
- **Suite 23**: 6-Process Cryosphere (`frost-weathering.js` sub-resolution damage accumulation $\to$ threshold spalling + solifluction export) & Shifting 8-Sector Wind Regimes + Wind Flow Debug field.
- **Suite 24**: Mesh-Free Overhang Calving $\to$ 6-DOF `24³` SDF Boulder Rigid-Body Seating, Snow/Sediment Bearing Sinkage, and Two-Way Stream/Talus Obstacle Deflection.
- **Suite 25**: All 19 Rock & Cliff SDF Archetypes + Pure-SDF 3D Rain/Wind/Slot-Flood Erosion + QEF Dual Contouring sharp dihedral preservation + valid binary `.GLB` export.
- **Suite 26**: Multi-Sub-World Composable Simulation Nodes + Simulation Seasons + Cross-Border Seam Stitching (`< 2 cm` border step).

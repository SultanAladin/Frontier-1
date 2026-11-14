# Frontier ParticleSpace Modular C++20 Port Plan

> **Scope:** Strip out unaligned prototype C++ particle scaffolding (leaving `Engine/SimulationSpace/ForceFields/` intact), adopt the latest HTML Particle Studio reference (`Experimental/ParticleEditor Final/` from `streamlinkinbox/Frontier@arena/4bfeedb2-frontier`, commit `5f14356`), and specify the complete, Frontier-aligned modular architecture across `Engine/ParticleSpace/`, `ParticleExtension`, `Engine/Codecs/ParticleCodec`, `Engine/Interchange/ParticleInterchange`, `Engine/Workspaces/Particle/`, `Engine/Shaders/Particles/`, and reusable `Engine/Components/` UI units — culminating in **exact 1:1 fidelity across all 102 HTML presets** with zero simplified inventions.

---

## 1. Completed Baseline & Cleanup Status

| Action | Path / Reference | Status |
| :--- | :--- | :--- |
| Stripped unaligned C++ particle plan | Removed `Engine/SimulationSpace/Particles/CxxParticleFoundationPlan.md` (empty directory removed) | Complete |
| Preserved C++ ForceFields intact | `Engine/SimulationSpace/ForceFields/*` (`19 ForceModel` laws, `FieldLatticeCascade`, `FieldLatticeVulkanExecutor`, `FieldFormulaCompiler`) untouched | Verified |
| Imported HTML Particle Studio only | Merged `Experimental/ParticleEditor Final/` from `streamlinkinbox/Frontier` (`arena/4bfeedb2-frontier` @ `5f14356`) including `FireworkPatterns.js`, `ObjectSurfaceSampler.js`, `ParticleInfographics.js`, `presets.js` (102 presets), `shaders.js`, `engine.js`, `SoftwarePreviewEngine.js` | Complete |
| HTML reference verification | `node "Experimental/ParticleEditor Final/Engine/CheckForceFields.mjs"` (`PASS 65`) + ES module load check (`102` presets, `93` active, `9` quarantined, `10` firework shapes, `17` object surfaces, `17` force types, `15` colour drivers, `6` ramp presets) | Verified |

---

## 2. Architectural Alignment & Naming Governance

Every C++20 file, namespace, type, enum, `.slang` shader, and `.ParticleSpace` format block strictly obeys Frontier's four architectural pillars:

1. **First-Class Space & Lifecycle Coordinator (`ParticleSpace` + `ParticleExtension`)**:
   - Parallels `Engine/GeometrySpace/`, `Engine/MaterialSpace/`, `Engine/DistanceFieldSpace/`, and `Engine/SimulationSpace/ForceFields/`.
   - `ParticleExtension` acts as the sole designated domain lifecycle coordinator (matching `InputExtension`, `WorkspaceExtension`, `RevisionExtension` under `Documentation/BannedWordsVocabulary.md` §3.1), managing deterministic tick scheduling, GPU buffer lifetime, cross-workspace publication (`WorkspaceExchange`), and `SimulationSpace::ForceFields` synchronization.
2. **Two-Tier Hybrid Persistence (`ParticleCodec`) + External `Interchange` (`ParticleInterchange`)**:
   - Native persistence uses `SpaceExtensionCategory::ParticleSpace` (`.ParticleSpace` — already reserved at slot `10` in `Engine/Codecs/SpaceContainerCodec.h`) with Tier 1 PascalCase TOML manifest + Tier 2 64-byte-aligned binary sections (`.StrandRest`, `.SurfaceAnchor`, `.RampLut`, `.Revision`) and inline/linked `.VectorFieldSpace` references via `SerializeInlineForceSourcesToManifest`.
   - External import/export lives in `Engine/Interchange/ParticleInterchange.{h,cpp}` (paralleling `Engine/Interchange/GeometryInterchange.{h,cpp}`), supporting HTML Particle Studio JSON/TOML exchange, external `.obj`/`.stl` surface-anchor intake via `GeometryInterchange`, and `.ParticleSpace` host-handoff packets (matching `Engine/handoff.js` for Fluid/World coupling).
3. **100% GPU Visuals via `.slang` + `Right-Hand +Z Up` Coordinate Lock**:
   - All particle geometry, ribbons, oriented solids, wings, arcs, fibres, lenses, backing solids, vector arrows, reference floor grids, interactive ball colliders, and debug curve overlays render strictly on Vulkan 1.3 via `.slang` shaders under `Engine/Shaders/Particles/`.
   - Deterministic Y-Up (HTML reference) ↔ Right-Hand `+Z Up` (Frontier Engine) axis transform `(x, y, z)_HTML → (x, -z, y)_Engine` is applied systematically at import/preset compilation time so every trajectory, gravity vector, mast height, puddle plane, and camera orbit matches visually 1:1.
4. **Zero-`And` & Zero-Banned-Word Compliance (`SKILL-Naming.md`)**:
   - No `And` conjunctions in filenames or identifiers; no banned nouns (`Emitter`/`Emit`, `System`, `State`, `Node`, `Mesh`, `Palette`, `Module`, `Pipeline`, `Pass`, `Stage`, `Frame`, `Pool`, `Catalog`, `Kind`, `Flag`, `Value`, `Manager`, `Controller`, `Helper`, `Base`, `Common`, `Shared`).

---

## 3. Canonical Modular Folder Structure

```text
Engine/
├── ParticleSpace/                                      // First-class modular particle domain (Frontier::ParticleSpace)
│   ├── ParticleExtension.h / .cpp                      // Lifecycle coordinator (Initialize, Tick, Suspend, Resume, Finalize, Exchange)
│   ├── ParticleDocument.h / .cpp                       // Authoritative in-memory ensemble of rigs, force links, colliders & timeline settings
│   ├── ParticleRig.h / .cpp                            // Single authored particle rig composing ReleaseOrigin + Laws + Colour + Carrier
│   ├── ParticleBuffers.h                               // 16B-aligned GPU structs (ParticleSlot 80B, StrandVertex 16B, GlobalUniform 240B, RigUniform 256B)
│   │
│   ├── Origins/                                        // Modular release-origin geometry types (replaces monolithic emitShape switches)
│   │   ├── ReleaseOriginTypes.h                        // ReleaseOriginCategory + std::variant typed origin descriptors
│   │   ├── AnalyticOrigins.h / .cpp                    // Point, SphereVolume, Disc, BoxVolume, SurfaceLattice, DualElectrode
│   │   ├── StructuralOrigins.h / .cpp                  // SpiderwebWeaveOrigin, ClothGridOrigin, BotanicalRootOrigin, CanopySkeletonOrigin
│   │   ├── SurfaceAnchorSampler.h / .cpp               // 17 procedural surfaces (0..16) + custom PolygonNetwork OBJ/STL/Facet anchor sampling
│   │   └── FireworkPatternSampler.h / .cpp             // All 10 FireworkShapes (Bouquet, SkyText, Heart, Star, Smiley, Saturn, Crown, Butterfly, Spiral, Finale)
│   │
│   ├── VisualCarriers/                                 // Modular render-geometry & shading carrier types
│   │   ├── VisualCarrierTypes.h                        // VisualCarrierCategory + SpriteCutoutCategory (all 20 shapes 0..19) + OpticalBlendEquation
│   │   ├── SpriteBillboardCarrier.h                    // Camera/axis/velocity-aligned quads + bokeh head styles + streak thinness
│   │   ├── RibbonStrandCarrier.h                       // 20-vertex strips: silk, cables, chains, solid woven cloth, grass/wheat blades, pinnate ferns, kelp, bark
│   │   ├── OrientedSolidCarrier.h                      // 3D cubes, coins, 3D glass shards, 8-fragment shatter derez, MoGraph height-extruded solids
│   │   ├── WingedCreatureCarrier.h                     // Articulated flapping 3D wings (butterflies, birds, starlings, bioluminescent fish)
│   │   ├── DielectricBeamCarrier.h                     // Coherent laser/plasma beams, Tesla/static arcs, plasma cages, light fibres, strike sparks
│   │   ├── RefractionLensCarrier.h                     // Relativistic gravitational lensing (black hole) + rising thermal heat shimmer distortion
│   │   └── BackingSurfaceCarrier.h                     // Obsidian/chrome/wireframe backing geometry (hand+orb, teapot, car, bust, custom PolygonNetwork)
│   │
│   ├── Colour/                                         // Modular colour, gradient, fill & ramp types (coupled with ColourSpace/ColourCodec)
│   │   ├── ParticleColourTypes.h                       // ColourSourceCategory (SolidPigment, LinearFill, MultiStopGradient, QuantizedRamp, BlackbodyThermal, SpeciesMapping, SpatialNoiseField)
│   │   ├── ColourDriverTypes.h                         // All 15 ColourDriverChannel modes (ModeDefault..LifeDuration), ColourWrapBehaviour, ColourBlendCurve
│   │   ├── ColourRampSampler.h / .cpp                  // Arbitrary-stop linear RGBA ramp sampling, 6 built-in ramp presets & 64B GPU LUT packing
│   │   └── ParticleColourEncoder.h / .cpp              // Packs ColourRamp LUT + driver uniforms identically for GPU compute/raster & CPU fallback
│   │
│   ├── Laws/                                           // Modular simulation behaviour & physics law types
│   │   ├── SimulationLawTypes.h                        // Composable law stack descriptor (each law self-contained, zero god-switch)
│   │   ├── BallisticMotionLaw.h / .cpp                 // Gravity, buoyancy, aerodynamic drag, flutter, floor bounce, timed origin/sky bursts
│   │   ├── ForceFieldCouplingLaw.h / .cpp              // Bridges directly to Engine/SimulationSpace/ForceFields/ (19 ForceModels + LatticeCascade + Formulas)
│   │   ├── CurlManifoldLaw.h / .cpp                    // Planar 3D curl waves, spherical curl plasma, bokeh stream ribbons, object surface pour
│   │   ├── ChainConstraintLaw.h / .cpp                 // 20-vertex XPBD/Verlet strands: cables/chains, vines/ropes, flags/sails, beads/chimes/drapes, tearing
│   │   ├── BotanicalSwayLaw.h / .cpp                   // Travelling wind stadium waves: grass/wheat, reeds, pinnate ferns, canopy trees, kelp, anemones, dandelion/blossom detachment
│   │   ├── SpiderwebLaw.h / .cpp                       // Real-time spinning Orb-Weave radial/spiral silk + Messy Attic Cobwebs + dew highlights
│   │   ├── FerrofluidMeniscusLaw.h / .cpp              // Rosensweig petri-dish spike meniscus + 3D levitated crown tracking orbiting/draggable magnets
│   │   ├── FractureFissureLaw.h / .cpp                 // Glacial ice cracking propagation + seismic Voronoi basalt ground fracture + abyss tilt
│   │   ├── ShallowWaterSplashLaw.h / .cpp              // Puddle ripple rings, crown splashes, figure-8 vehicle tyre mud/water splashes, sticky clods
│   │   ├── EffectorLatticeLaw.h / .cpp                 // MoGraph grids (7 shapes × 6 drivers × twin placeable 3D effectors: Sphere/Box/Torus/Disc/Cone)
│   │   ├── FlockBoidsLaw.h / .cpp                      // Spatial-hash neighbour grid: cohesion, alignment, separation, predator-ball evasion & murmuration
│   │   ├── MolecularChemistryLaw.h / .cpp              // Lennard-Jones atoms (kT, epsilon, sigma, Langevin damping) + A+B<->C chemical reaction kinetics
│   │   ├── TransitionDerezLaw.h / .cpp                 // Derez cube, shatter, coin stack, melt, reverse-derez, voxel blast, butterfly & glitch dissolves
│   │   ├── ShieldTechLaw.h / .cpp                      // 6 barrier forms × 7 stackable motif bitmasks, radar sweeps, acoustic sonar, EMP domes, holo glitch
│   │   ├── PyroSpectacleLaw.h / .cpp                   // Multi-stage bouquet fireworks, New Year's sky-text/shape shells, IR decoy V-salvo flares
│   │   └── InteractiveColliderLaw.h / .cpp             // Interactive 3D thrown ball collider, floor/puddle contact, & signed-distance volume collision
│   │
│   ├── Execution/                                      // Deterministic GPU & CPU verification executors
│   │   ├── ParticleVulkanExecutor.h / .cpp             // Vulkan 1.3 compute + raster encoder routed through Engine/GeometricRaster/RenderDeviceInterface
│   │   ├── ParticleSoftwareExecutor.h / .cpp           // Deterministic CPU parity executor (for Exhibits visual proofs & headless regression tests)
│   │   └── ParticleReadback.h / .cpp                   // Asynchronous alive count, active strand count, GPU timing & bounds telemetry readback
│   │
│   └── Presets/                                        // Built LAST (Phase 6) — 100% exact parity with all 102 HTML presets
│       ├── ParticlePresetRegistry.h / .cpp             // Registry + category metadata (15 categories: 93 active + 9 quarantined)
│       ├── PresetDefinitionsBallisticPyro.cpp          // Ballistics & Sparks (10), Pyro & Water (10), Storms & Weather (8), Atmosphere & Sky (5)
│       ├── PresetDefinitionsFloraCloth.cpp             // Cloth & Rigging (7), Foliage & Flora (7), Wind-Blown (10)
│       ├── PresetDefinitionsCurlTechDerez.cpp          // Silk & Curl (10), Shields & Tech (6), Transitions (9), Space & Distortion (2)
│       └── PresetDefinitionsFieldGridHold.cpp          // Motion & Grids (5), Ferrofluid & Fields (2), Fracture & Cracks (2), Quarantine (9)
│
├── Codecs/
│   └── ParticleCodec.h / .cpp                          // Two-Tier Hybrid .ParticleSpace encoder/decoder (TOML manifest + 64B binary sections)
│
├── Interchange/
│   └── ParticleInterchange.h / .cpp                    // External JSON/TOML HTML ParticleStudio import/export, OBJ/STL anchor bridge, Fluid handoff
│
├── Components/                                         // NEW REUSABLE UI UNITS (shared across Particle, Fluid, Fracture, ForceField, Animation, Vehicle, Material)
│   ├── Playback/
│   │   ├── PlaybackTransportBar.h / .cpp               // Play, Pause, SingleStep, Restart, Loop, TimeScale (0.25x..2x), BurstTrigger, Timecode
│   │   └── TimelineRulerScrubber.h / .cpp              // Scrubbable simulation/animation ruler, playhead needle, live track indicators
│   ├── Colour/
│   │   ├── ColourRampEditor.h / .cpp                   // Solid / Fill / Multi-Stop Gradient / Quantized Ramp editor + Driver/Wrap/Interp + Swatch presets
│   │   └── ScalarCurveEditor.h / .cpp                  // Value-over-life / attenuation / falloff curve editor
│   └── Diagnostics/
│       ├── DiagnosticInfographicCanvas.h / .cpp        // Tactile animated overview infographic tile (17 particle/field archetypes + extensible)
│       └── LogStreamDrawer.h / .cpp                    // Collapsible live event/telemetry stream drawer with severity filters
│
├── Workspaces/
│   └── Particle/                                       // Mirrors Engine/Workspaces/UV/ & Engine/Workspaces/Texturing/
│       ├── ParticleWorkspaceHost.h / .cpp              // Dual-deployment host (Standalone FrontierParticle or docked Editor workspace + ContentBrowser)
│       ├── ParticleWorkspaceSession.h / .cpp           // Binds WorkspaceDocument (.ParticleSpace), RevisionTracker undo/redo, ParticleExtension & playback
│       ├── ParticleAuthoringOperations.h / .cpp        // Parametric reversible operations recorded into RevisionTracker
│       ├── ParticleOutlinerPanel.h / .cpp              // Outliner with visible/hidden census tiles, 15 category filter chips, search & Add picker
│       ├── ParticleViewportHost.h / .cpp               // 3D viewport bar (projection, field overlay, Throw Ball, Field arrows, minimap) + left tool rail
│       ├── ParticleViewportInteractor.h / .cpp         // Orbit/pan/zoom, 3D handle dragging (effectors, forces, magnets, anemometer), Throw Ball raycast
│       ├── ParticleInspectorPanel.h / .cpp             // Context-aware tabbed inspector reusing ColourRampEditor, ScalarCurveEditor & ForceInspectorView
│       └── ParticlePaneAppearance.h                    // Styling tokens aligned with ThemeRepository & StudioIconSet (particle-* & force-* icons)
│
└── Shaders/
    └── Particles/                                      // Pure .slang (compiled via slangc / Automation/PrepareShaderToolchain.py)
        ├── ParticleBuffers.slang                       // 16B-aligned shared structs, PRNG, basis, curl/simplex noise, colour ramp evaluation
        ├── ParticleForceCoupling.slang                 // Imports ForceFieldEvaluationShared.slang (19 ForceModels + lattice cascade)
        ├── ParticleReleaseCompute.slang                // Compute release pass (all origins, webs, cloth grids, flora, fireworks, custom anchors)
        ├── ParticleAdvectCompute.slang                 // Compute simulation pass (ballistics, curl, XPBD strands, flora, ferro, cracks, splashes, grids, shields)
        ├── ParticleSpatialGridCompute.slang            // Spatial cell clear/insert + boids flocking + Lennard-Jones molecular + chemical reactions
        ├── ParticleReductionCompute.slang              // Alive count, bounding extent & telemetry reduction
        ├── ParticleBillboardRasterization.slang        // All 20 procedural sprite cutouts (shapes 0..19)
        ├── ParticleRibbonRasterization.slang           // 20-vertex ribbon strips (silk, chains, woven cloth, blades, pinnate ferns, kelp, bark, trails)
        ├── ParticleSolidRasterization.slang            // 3D cubes, coins, glass shards, 8-fragment shatter derez, MoGraph height cubes
        ├── ParticleWingedRasterization.slang           // Articulated flapping 3D butterflies, birds, starlings, fish
        ├── ParticleLineArcRasterization.slang          // Spiderweb bonds, molecular bonds, lightning arcs, laser beams, fibre tubes, strike sparks
        ├── ParticleLensRasterization.slang             // Screen-space gravitational lensing + heat shimmer refraction
        ├── ParticleSurfaceBackingRasterization.slang   // Obsidian/chrome/wireframe backing objects + vehicle hull + collider ball
        └── ParticleOverlayRasterization.slang          // Reference floor grid, vector arrows, 3D effector/force handles, curve debug vertices
```

CMake target structure:
- `FrontierParticleSpace` links `FrontierSimulationSpace` (`ForceFields`), `FrontierGeometrySpace`, `FrontierDistanceFieldSpace`, `FrontierColourCodec`, `FrontierGeometricRaster`.
- `FrontierParticleWorkspace` links `FrontierParticleSpace`, `FrontierWorkspaces`, `FrontierComponents` (including the new reusable `Playback`, `Colour`, and `Diagnostics` widgets).
- Registered in `WorkspaceCapabilityRegister` as `HostWorkspaceTarget::ParticleWorkspace` handling `.ParticleSpace` documents.

---

## 4. Modular Type Architecture (Zero Monolithic Switches)

### 4.1 Modular Visual Carriers & Sprite Cutouts

Every rig picks a typed `VisualCarrierSpec` (`std::variant`) instead of cramming unrelated rendering branches into one flat struct:

| Visual Carrier Type | GPU Shader Unit | Responsibilities |
| :--- | :--- | :--- |
| `SpriteBillboardCarrier` | `ParticleBillboardRasterization.slang` | All **20 procedural cutouts** (`0: Streak`, `1: ShadedSphere`, `2: BotanicalLeaf/Paper`, `3: SoftGlow`, `4: CubeProjection`, `5: MetallicCoin`, `6: IridescentBubble`, `7: DendriticSnowflake`, `8: CrystallineHailstone`, `9: BokehDisc`, `10: ShockwaveRing`, `11: PulseDome`, `12: HologramGlitch`, `13: WingedSilhouette`, `14: ViscousDroplet`, `15: RadarSonarSweep`, `16: EnergyBarrierShield`, `17: AuroraCurtain`, `18: GlassShardCrystal`, `19: VolumetricWaterMist`), bokeh head styles (`0..3`), `StreakThinness`, `OpticalBlendEquation` (`Additive` / `PremultipliedAlpha`) |
| `RibbonStrandCarrier` | `ParticleRibbonRasterization.slang` | 20-vertex continuous ribbon strips: velocity wake, surface stream pour, catenary cables/chains, leafy vines, solid woven cloth flags/sails, tapered grass/wheat/reeds blades, pinnate fern fronds, bark cylinders, giant kelp blades, sea anemone tentacles, decoy flare smoke plumes, viscous trails |
| `OrientedSolidCarrier` | `ParticleSolidRasterization.slang` | True 3D geometry instances: tumbling cubes, 3D faceted glass shards, coin stacks, 8-fragment floor shatter derez, height-extruded MoGraph cubes/boxes/hexagons/triangles/tiles, Voronoi fracture slabs |
| `WingedCreatureCarrier` | `ParticleWingedRasterization.slang` | 3D hinged dual-wing geometry for butterflies, birds, starling murmurations, bioluminescent fish schools, and butterfly-dissolve transitions |
| `DielectricBeamCarrier` | `ParticleLineArcRasterization.slang` | Bond segments (spiderweb radial/spiral silk, molecular covalent/LJ bonds), continuous laser/plasma beams, Tesla/static sphere arcs, plasma cage & storm-grid arcs, 3D fibre tubes, strike sparks |
| `RefractionLensCarrier` | `ParticleLensRasterization.slang` | Relativistic black-hole accretion gravitational lens distortion & rising thermal heat-shimmer refraction |
| `BackingSurfaceCarrier` | `ParticleSurfaceBackingRasterization.slang` | Backing surface styles (`ObsidianDark`, `BrushedChrome`, `GhostWireframe`, `ParticlesOnlyHidden`) for all 17 `ObjectSurface` presets + custom imported `PolygonNetwork` + interactive chrome ball collider + figure-8 rally vehicle hull |

### 4.2 Modular Colour, Gradient, Fill & Ramp Types (`Engine/ParticleSpace/Colour/`)

Directly mirrors and generalizes the HTML studio's `COLOR_BY_MODES`, `COLOR_WRAP_MODES`, `COLOR_INTERP_MODES`, `COLOR_PALETTES`, and arbitrary-stop `colorStops`, while integrating cleanly with `Engine/Codecs/ColourCodec.h` (`.ColourSpace` scene-linear ACEScg/sRGB swatches):

- **`ColourSourceSpec`** (`std::variant`):
  1. `SolidPigmentSource` — single scene-linear RGBA tint + opacity multiplier.
  2. `LinearFillSource` — two-stop or three-stop `ColA -> ColB -> ColC` fill (preserves exact compatibility with legacy HTML presets).
  3. `MultiStopGradientSource` — arbitrary ordered `std::vector<ColourStop>` (`Position01`, `LinearRgba`), supporting all 6 built-in presets (`Spectrum`, `Blackbody`, `Aurora`, `Plasma`, `Ocean`, `SunsetNeon`) and custom `.ColourSpace` imports.
  4. `QuantizedRampSource` — stepped/banded ramp with configurable band count and edge softness.
  5. `BlackbodyThermalSource` — physically motivated Kelvin temperature ramp (for fire, welding, embers, flares, fireworks, plasma).
  6. `SpeciesMappingSource` — discrete per-species / per-role colour slots (e.g. chemistry `A`/`B`/`C`, spiderweb silk/dew, tree trunk/leaves, firework sub-shells).
  7. `SpatialNoiseFillSource` — 3D value-noise driven colour variation (`rampVNoise3` parity).
- **15 Exact `ColourDriverChannel` Drivers** (1:1 with HTML `COLOR_BY_MODES` codes `0..14`):
  - `0 ModeDefault` · `1 NormalizedLifetime` · `2 ParticleAgeSeconds` · `3 VelocitySpeed` · `4 VerticalVelocity` · `5 OriginAzimuthAngle` · `6 VelocityHeadingAngle` · `7 DistanceFromOrigin` · `8 WorldElevation` · `9 SpatialNoise3D` · `10 PerParticleRandomSeed` · `11 SpinOrientationAngle` · `12 DistanceToCamera` · `13 ParticleSizeMeters` · `14 AssignedLifespanSeconds`.
- **Remapping Controls**: `RangeMin`, `RangeMax`, `RepeatCount`, `PhaseOffset`, `ColourWrapBehaviour` (`Clamp`, `Repeat`, `Mirror`), `ColourBlendCurve` (`Linear`, `Smooth`, `Stepped`), packed into a 64-byte-aligned LUT + uniform block shared identically between GPU `.slang` and CPU verification.

### 4.3 Modular Force Field Coupling (`Engine/SimulationSpace/ForceFields/` Reuse)

Zero duplication of ForceField math. `ForceFieldCouplingLaw` binds directly to our existing C++ `Engine/SimulationSpace/ForceFields/` units:

- **All 17 HTML `ForceFieldCatalogue` Types Mapped 1:1 to C++ `ForceModel` Laws**:
  - *Flow (`m/s`, 10 types)*: `WindGenerator` (`LinearWind`), `Tornado` (`RotationalColumn`), `Jet` (`FlowStream::SlipstreamJet`), `GustFront` (`AtmosphericGust`), `Microburst` (`RadialBurst` + downward jet profile), `Thermal` (`FlowStream::ThermalUpdraft` / `Buoyancy`), `Puff` (`RadialBurst` impulse), `Blast` (`RadialBurst` shock), `Wake` (`FlowStream::VesselWake`), `Current` (`FlowStream::Current`).
  - *Accelerate (`m/s²`, 6 types)*: `Attractor`, `Repulsor`, `Orbit` (`Torque` / `AxisAttractor`), `Lift` (`Buoyancy`), `Magnetic` (`MagneticDipole` / `LorentzField`), `Gravity` (`Attractor` columnar well).
  - *Damp (`1/s`, 1 type)*: `Drag`.
  - Plus C++ native `Containment`, `CurlNoise`, `VorticityConfinement`, `BrownianNoise`, `SplineForce`, `BoundaryAdhesion`, and compiled **`VectorFieldLang` symbolic formulas** (`FieldFormulaCompiler`)!
- **Shared Spatial Lattice & Channels**:
  - Samples the existing `FieldLatticeCascade` + packed GPU force entries via `ForceFieldEvaluationShared.slang`, honoring all 3 `ReachEnvelopeCategory` shapes (`Sphere`, `Column`, `OrientedBox`), 4 `ForceAttenuationMode` curves (`None`, `Linear`, `SmoothStep`, `InverseSquare`), 3 `ForceDurationMode` envelopes (`Continuous`, `Modulated`/`Window`, `Impulse`/`Trigger`), and all **7 receiver channels** (`Particles`, `Debris`, `Foliage`, `Cloth`, `Fluid`, `Smoke`, `Rigid`).

### 4.4 Modular Release Origins & Effector Geometry Types

- **Analytic Origins**: `Point`, `SphereVolume`, `Disc`, `BoxVolume`, `SurfaceLattice`, `DualElectrode`.
- **Procedural & Imported Surface Anchors (`SurfaceAnchorSampler`)**: All **17 `ObjectSurface` IDs (`0..16`)** (`XZ Floor Matrix`, `XY Wall Matrix`, `3D Obsidian Sphere`, `3D Obsidian Hand + Chrome Ball`, `3D Torus Ring`, `3D Cubic Volume`, `Utah Teapot`, `Sports Car Silhouette`, `Sculpted Head / Bust`, `Torus Knot (3,2)`, `Precision Gear / Cog`, `Quartz Crystal Cluster`, `Architectural Archway`, `Bevelled Monolith Cube`, `Faceted Spire / Cone`, `Stellated Star Polyhedron`, `Custom Imported PolygonNetwork` via `GeometryInterchange` `.obj`/`.stl`/`.gltf`/`.GeometrySpace`) with all 3 sampling modes (`EvenSurface`, `SurfaceInteriorVolume`, `RidgeCreaseAccent`) and Euler rotation.
- **Firework Pattern Sampler (`FireworkPatternSampler`)**: All **10 `FireworkShape` IDs (`0..9`)** (`Bouquet`, `SkyText` with custom UTF-8 string, `Heart`, `Star`, `Smiley`, `Saturn`, `Crown`, `Butterfly`, `Spiral`, `GrandFinale`).
- **MoGraph Effectors (`EffectorLatticeLaw`)**: All **7 `GridShape` modes** (`HalftoneCircles`, `UniformBoxes`, `HeightCubes`, `FlatTiles`, `Triangles`, `Hexagons`, `AutoMixed`), **6 `EffectorDriver` modes** (`FluidSimplexWave`, `OrbitingDualEffectors`, `CurlIslands`, `ExpandingPulseRing`, `BallForceFieldsOnly`, `Placeable3DEffectors`), and twin interactive 3D effectors with **5 `EffectorVolumeShape` types** (`Sphere`, `Box`, `Torus`, `Disc`, `Cone`) + blend toggle.

---

## 5. `.ParticleSpace` Codec & `ParticleInterchange` Formatting

### 5.1 Native `.ParticleSpace` Two-Tier Hybrid Format (`Engine/Codecs/ParticleCodec.{h,cpp}`)

Uses `SpaceContainerEncoder` (`SpaceExtensionCategory::ParticleSpace`, schema v1, `CoordinateSystem = "RightHandZUp"`):

- **Tier 1 (Human-Readable PascalCase TOML Manifest)**:
  - `[Document]` — `Title`, `SchemaVersion`, `CoordinateSystem`, `Seed`, `TimeScale`, `PlaybackLoopSeconds`, `ActivePresetId`.
  - `[[ParticleRig]]` — Modular sub-tables per rig: `[ParticleRig.Origin]`, `[ParticleRig.Carrier]`, `[ParticleRig.Colour]`, `[[ParticleRig.Colour.Stop]]`, `[ParticleRig.Laws.*]` (only active law tables are emitted — clean, minimal, diff-friendly).
  - `[ForceField]` / `[[ForceSource]]` / `LinkedVectorFieldPaths` — serialized via existing `Codecs::SerializeInlineForceSourcesToManifest` (`Engine/Codecs/VectorFieldCodec.h`).
  - `[InteractiveCollider]` — ball radius, bounce restitution, magnet position/orbit mode.
  - `[EditorViewport]` *(gated by `#if FRONTIER_EDITOR_ENABLED`)* — camera posture, overlay channel, minimap visibility, active tool.
- **Tier 2 (64-Byte-Aligned Binary Sections with FNV-1a 64-bit Checksums)**:
  - `.RampLut` — pre-baked linear RGBA16F/F32 gradient lookup samples for zero-jitter GPU upload.
  - `.SurfaceAnchor` — cached sampled anchor positions/normals/curvature weights for custom imported `PolygonNetwork` surfaces.
  - `.StrandRest` — initial rest-length/topology tables for spiderwebs, cloth grids, and canopy trees.
  - `.Revision` — optional embedded `RevisionTracker` non-linear undo/redo history via `RevisionCodec`.

### 5.2 External Interchange (`Engine/Interchange/ParticleInterchange.{h,cpp}`)

- **HTML Particle Studio Import/Export**: Lossless conversion between `Experimental/ParticleEditor Final/` TOML/JSON snapshots (including `PE.baseParams` 129-key schema and Y-Up ↔ `+Z Up` transform) and native `ParticleDocument` — enabling automated cross-verification of all 102 presets directly from `presets.js` exports.
- **Host Handoff Packet (`ParticleHandoffPacket`)**: Mirrors `Engine/handoff.js` (`PresetId`, `RigName`, typed parameter overrides) and publishes live updates across `WorkspaceExchange` (`SpaceExtensionCategory::ParticleSpace`) so Fluid, Fracture, and World workspaces can host and tune embedded particle rigs live.
- **External 3D Surface Intake**: Delegates `.obj` / `.stl` / `.gltf` drops to `Interchange::GeometryInterchange` → `PolygonNetwork` → `SurfaceAnchorSampler`.

---

## 6. Reusable UI Components (`Engine/Components/`)

Every reusable widget identified in the HTML Particle Studio is built once under `Engine/Components/` (composed strictly from `PixelSpace`, `CapsuleButton`, `ExclusiveSelector`, `DropdownSelector`, `ScalarSlider`, `TextInputField`, `ThemeRepository`, `TypographyRepository`, and `VectorIconRepository` backed by `Experimental/StudioIconSet/`) so Particle, Fluid, Fracture, ForceField, Animation, Vehicle, and Texture workspaces all share the exact same controls:

| Reusable Component | Location | Controls & Capabilities | Shared By Workspaces |
| :--- | :--- | :--- | :--- |
| **`PlaybackTransportBar`** | `Engine/Components/Playback/` | `Restart` (`R`), `Play`/`Pause` (`Space`), `SingleStep` (`S`), `Loop` toggle, `TimeScale` (`0.25×`, `0.5×`, `1.0×`, `2.0×`), tabular `Timecode` (`00:00.000`), optional `Burst` (`B`) trigger, `Live / Cached` status indicator; icons from `StudioIconSet` (`particle-play`, `particle-pause`, `particle-restart`, `particle-loop`, `particle-step`, `particle-burst`) | Particle, Fluid, Fracture, ForceField, Vehicle, Animation |
| **`TimelineRulerScrubber`** | `Engine/Components/Playback/` | Configurable time ruler (`0s..30s`), step counter, scrubbable playhead needle, active track range bar | Particle, Fluid, Fracture, Animation, Audio |
| **`ColourRampEditor`** | `Engine/Components/Colour/` | Mode switch (`Solid` / `Fill` / `Multi-Stop Gradient` / `Quantized Ramp` / `Blackbody`), draggable gradient stop strip, `DriverChannel` dropdown (15 modes), `RangeMin`/`RangeMax`, `Repeat`, `Phase`, `Wrap` (`Clamp`/`Repeat`/`Mirror`), `Interp` (`Linear`/`Smooth`/`Stepped`), built-in swatch presets + `.ColourSpace` load/save | Particle, Material, Pattern, Stamp, Fluid, ForceField |
| **`ScalarCurveEditor`** | `Engine/Components/Colour/` | Interactive 2D curve canvas for `SizeOverLife`, `OpacityOverLife`, `AttenuationFalloff`, `ModulationEnvelope` (`particle-size-over-life`, `particle-curve`, `force-falloff`) | Particle, ForceField, Fluid, Animation, Vehicle |
| **`DiagnosticInfographicCanvas`** | `Engine/Components/Diagnostics/` | Reusable live overview telemetry tile rendering domain-specific animated diagrams (17 particle/force archetypes ported from `ParticleInfographics.js`, extensible for fluid/vehicle/fracture telemetry) | Particle, ForceField, Fluid, Vehicle, Fracture |
| **`LogStreamDrawer`** | `Engine/Components/Diagnostics/` | Collapsible bottom diagnostic stream drawer with timestamp, severity sign, category filter & clear/copy actions | All Frontier Workspaces |

Existing Frontier UI components reused directly with zero reinvention:
- `WorkspaceDockContainer`, `WorkspaceTabStrip`, `OutlinerPanel`, `ViewportPanel`, `ViewportProjectionPane`, `InspectorPanel`, `ControlCentrePanel`, `ContentBrowserPanel`, `CameraComponent`, `TransformGizmo`, `ForceInspectorView`, `ForcePresetBrowserPanel`, `VectorFieldOverlaySequence`.

---

## 7. Phased Implementation Roadmap (Presets Strictly Last, Zero Deviations)

| Phase | Deliverable | Key Units & Verification Gate |
| :--- | :--- | :--- |
| **Phase 1 — Core Modular Types & `ParticleSpace` Foundation** | `ParticleBuffers.h`, `ParticleRig`, `ParticleDocument`, `ParticleExtension`, `Origins/*` (`AnalyticOrigins`, `StructuralOrigins`, `SurfaceAnchorSampler`, `FireworkPatternSampler`), `VisualCarriers/*`, `Colour/*`, `Laws/SimulationLawTypes.h` | Compiles cleanly across `Editor`/`Development`/`Shipping`; zero banned words / zero `And` identifiers; unit validation (`ParticleFoundationValidation`) verifies 16B GPU struct alignment, all 15 colour drivers, 6 ramp presets, 17 surface anchor samplers, and 10 firework pattern samplers. |
| **Phase 2 — `.ParticleSpace` Codec & `ParticleInterchange`** | `Engine/Codecs/ParticleCodec.{h,cpp}`, `Engine/Interchange/ParticleInterchange.{h,cpp}`, `SpaceContainerCodec` wiring, inline `VectorFieldCodec` force-source round-trip | `ParticleCodecInterchangeValidation` proves bit-exact `.ParticleSpace` Two-Tier Hybrid round-trip (TOML + 64B binary sections + FNV-1a checksums + `.Revision`) and lossless import of HTML `Experimental/ParticleEditor Final/` JSON/TOML exports across all 129 parameter keys. |
| **Phase 3 — Modular Simulation Laws & ForceField Coupling** | All 16 modular law units under `Engine/ParticleSpace/Laws/` + `ParticleSoftwareExecutor` + `ParticleForceCoupling.slang` importing `ForceFieldEvaluationShared.slang` | `ParticleLawSolverValidation` verifies every law independently (ballistics, all 17+2 ForceModels & 7 channels, curl manifolds, XPBD 20-vertex strands + tearing, botanical sway, spiderweb weave, ferrofluid Rosensweig spikes, ice/seismic fracture, shallow-water mud splashes, MoGraph effectors, boids + predator scatter, Lennard-Jones + chemistry, derez transitions, shields, pyro/flares, ball/SDF collider). |
| **Phase 4 — Pure `.slang` Vulkan 1.3 Compute & Raster Execution** | All 14 `.slang` shaders under `Engine/Shaders/Particles/` + `ParticleVulkanExecutor` (via `RenderDeviceInterface`) + `ParticleReadback` | Compiled via `slangc`; Hardware Vulkan + SwiftShader offscreen validation (`ParticleVulkanValidation`) renders every visual carrier (all 20 sprite cutouts, ribbons, oriented solids, shatter, wings, arcs/beams/fibres, gravitational lens/shimmer, backing objects, floor/arrow/handle overlays) to `.png` proofs under `Exhibits/Images/Particles/`. |
| **Phase 5 — Reusable `Engine/Components/` UI & `ParticleWorkspace`** | `PlaybackTransportBar`, `TimelineRulerScrubber`, `ColourRampEditor`, `ScalarCurveEditor`, `DiagnosticInfographicCanvas`, `LogStreamDrawer` + `Engine/Workspaces/Particle/*` + `WorkspaceCapabilityRegister` (`ParticleWorkspace`) + `StudioIconSet` bindings | `ParticleWorkspaceUIValidation` captures offscreen C++ `.png` screenshots of the docked 3-pane workspace (Outliner + Viewport/ToolRail/Transport/Ruler + Tabbed Inspector + Infographic) and verifies 1:1 visual parity against screenshots of `Experimental/ParticleEditor Final/index.html`. |
| **Phase 6 (Final Step) — Complete 102-Preset Registry & 1:1 HTML Parity Proof** | `Engine/ParticleSpace/Presets/*` implementing **all 102 presets** verbatim from `Experimental/ParticleEditor Final/Engine/presets.js` with exact parameters, colours, capacities, laws, carriers, and ForceField pairings — **no simplified inventions, no omitted presets** | `ParticlePresetParityValidation` imports every preset from `presets.js` via `ParticleInterchange`, asserts 100% parameter equality against `ParticlePresetRegistry`, steps both CPU & Vulkan executors, and writes deterministic contact-sheet `.png` visual proofs for all **15 categories (93 active + 9 quarantined = 102/102)** into `Exhibits/Images/Particles/` compared side-by-side against HTML reference renders. |

---

## 8. Complete 102-Preset Parity Ledger (Mandatory Phase 6 Deliverable)

Every single preset present in `Experimental/ParticleEditor Final/Engine/presets.js` **must** be reproduced verbatim in `Engine/ParticleSpace/Presets/` using the modular `ReleaseOrigin`, `VisualCarrier`, `ColourSource`, and `SimulationLaw` types — with zero dropped entries and zero invented simplifications:

| # | Category (HTML `VFX_CATEGORIES`) | Count | Exact Preset IDs Required (102 Total: 93 Active + 9 Quarantined) | Primary Carrier & Law Composition |
| :-: | :--- | :-: | :--- | :--- |
| 1 | **Ballistics & Sparks** | 10 | `coin-fountain`, `magnetic-particles`, `sparks`, `glass`, `shockwave-ring`, `plasma-laser-beam`, `plasma-tracers`, `projectile-trails`, `welding-grinder-sparks`, `strikeSparks` | `SpriteBillboard` (`Streak`/`Glow`/`Coin`/`ShockwaveRing`), `OrientedSolid` (`GlassShardCrystal`), `RibbonStrand` (trails), `DielectricBeam` (laser/strike sparks) + `BallisticMotionLaw`, `ForceFieldCouplingLaw` (`MagneticDipole`) |
| 2 | **Pyro & Water** | 10 | `bubbles`, `foam`, `fire`, `smoke`, `explosion`, `embers`, `watermist`, `lapilli`, `steam`, `viscous-dripping` | `SpriteBillboard` (`SoftGlow`/`IridescentBubble`/`VolumetricWaterMist`/`ShadedSphere`/`ViscousDroplet`), `RibbonStrand` (viscous trails) + `BallisticMotionLaw` (buoyancy/drag/bounce), `BlackbodyThermalSource` |
| 3 | **Storms & Weather** | 8 | `rain`, `hail`, `tornado`, `sandstorm`, `snow`, `sand`, `ash`, `debris` | `SpriteBillboard` (`Streak`/`DendriticSnowflake`/`CrystallineHailstone`/`ShadedSphere`/`BotanicalLeaf`) + `BallisticMotionLaw`, `ForceFieldCouplingLaw` (`RotationalColumn`, `AtmosphericGust`, `LinearWind`) |
| 4 | **Wind-Blown** | 10 | `leaves`, `paper`, `petals`, `seeds`, `feathers`, `confetti`, `boids-flock`, `boids-butterflies`, `boids-fish-school`, `starling-murmuration` | `SpriteBillboard` (`BotanicalLeaf`/paper), `WingedCreatureCarrier` (birds/butterflies/fish/starlings) + `BallisticMotionLaw` (flutter), `FlockBoidsLaw`, `ForceFieldCouplingLaw`, `InteractiveColliderLaw` (predator ball) |
| 5 | **Atmosphere & Sky** | 5 | `flares`, `pollen`, `fireworks`, `fireworks-spectacle`, `aurora-borealis` | `SpriteBillboard` (`AuroraCurtain`/`SoftGlow`/`Streak`), `RibbonStrand` (decoy flare smoke), `FireworkPatternSampler` (all 10 shapes + sky text) + `PyroSpectacleLaw` |
| 6 | **Silk & Curl** | 10 | `curl-fur-field`, `curl-sphere-plasma`, `bokeh-stream-ribbons`, `bokeh-object-pulse`, `tesla-sphere-lightning`, `static-sphere-shock`, `spiderweb-orb`, `spiderweb-cobweb`, `plasma-cage-arcs`, `lightning-web-mesh` | `RibbonStrand` (curl wave/bokeh stream/surface pour), `SpriteBillboard` (`BokehDisc`/`Streak`), `DielectricBeamCarrier` (arcs/webs/cages) + `CurlManifoldLaw`, `SpiderwebLaw`, `SurfaceAnchorSampler`, `InteractiveColliderLaw` |
| 7 | **Shields & Tech** | 6 | `emp-pulse-dome`, `radar-sweep`, `sonar-ping`, `hologram-data-glitch`, `energy-shield-hex`, `energy-shield-prism` | `SpriteBillboard` (`PulseDome`/`RadarSonarSweep`/`HologramGlitch`/`EnergyBarrierShield`) + `ShieldTechLaw` (6 forms × 7 motifs + live ball impact ripple) |
| 8 | **Transitions** | 9 | `derez-cube`, `cube-coins`, `melt-away`, `reverse-derez`, `voxel-explosion`, `coin-stack`, `dissolve-nothing`, `dissolve-butterflies`, `glitch-dissolve` | `OrientedSolid` (`Cube`/`ShatterFragment`/`Coin`), `WingedCreatureCarrier`, `SpriteBillboard` (`HologramGlitch`) + `TransitionDerezLaw`, `SurfaceLatticeOrigin` |
| 9 | **Space & Distortion** | 2 | `black-hole`, `heat-shimmer` | `RefractionLensCarrier` (`ParticleLensRasterization.slang`) + accretion spiral kinematics & thermal updraft distortion |
| 10 | **Cloth & Rigging** | 7 | `cables-chains`, `vines-ropes`, `flag-banner`, `ship-sail`, `beaded-door-curtain`, `bamboo-wind-chimes`, `velvet-theater-drapes` | `RibbonStrandCarrier` (solid woven cloth, chain links, leafy vines, bead/chime/drape strands) + `ChainConstraintLaw` (20-vertex XPBD + ball push-out + tear threshold), `ForceFieldCouplingLaw` |
| 11 | **Foliage & Flora** | 7 | `grass-wheat-field`, `tall-reeds`, `interactive-fern`, `interactive-tree`, `kelp-forest`, `sea-anemone`, `dandelion-puff` | `RibbonStrandCarrier` (tapered blades, pinnate ferns, bark cylinders, kelp, anemone tentacles), `SpriteBillboard` (wheat ears, cattails, canopy leaves, pappus seeds) + `BotanicalSwayLaw`, `InteractiveColliderLaw` |
| 12 | **Motion & Grids** | 5 | `effector-grid-cubes`, `effector-sphere-dots`, `effector-hand-halftone`, `effector-torus-hex`, `effector-object-placer` | `OrientedSolidCarrier` + `SpriteBillboard` (7 MoGraph shapes) + `BackingSurfaceCarrier` (17 surfaces + custom OBJ/STL) + `EffectorLatticeLaw` (6 drivers + twin placeable 3D effectors) |
| 13 | **Ferrofluid & Fields** | 2 | `ferrofluid-spikes`, `ferrofluid-orb` | `OrientedSolidCarrier` / meniscus spike geometry + `FerrofluidMeniscusLaw` (orbiting/draggable magnet + `ForceFieldCouplingLaw`) |
| 14 | **Fracture & Cracks** | 2 | `cracked-ice-sheet`, `ground-crack-quake` | `OrientedSolidCarrier` (Voronoi slabs + fissure debris) + `FractureFissureLaw` (propagation front + abyss tilt) |
| 15 | **Quarantine (Preserved for Completeness)** | 9 | `splash`, `impact-dust-ring`, `splatter-burst`, `car-puddle-splash`, `puddle-ripple-splash`, `mud-splatter-heavy`, `blossom-branch-detach`, `jelly-slime-blob`, `slime-tendril-cluster` | `ShallowWaterSplashLaw`, `BotanicalSwayLaw` (blossom detachment), `ChainConstraintLaw` (soft-body jelly/slime tendrils), `BackingSurfaceCarrier` (figure-8 rally car) — flagged `Quarantined` in registry metadata exactly matching HTML `QUARANTINE_IDS` |

*(Plus interactive scene additions supported in the HTML Studio Outliner/Add menu: **Molecular Lennard-Jones Atoms** (`kind = 3`), **Chemical Reaction Kinetics** (`kind = 4`, `A + B <-> C`), **17 Bounded Force Field Sources**, **Symbolic `VectorFieldLang` Formulas**, and **Interactive 3D Ball / Magnet / Effector / Anemometer Handles**.)*

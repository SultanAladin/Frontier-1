# Frontier ParticleSpace Modular C++20 Port Plan

> **Scope:** Strip out unaligned prototype C++ particle scaffolding (preserving our existing Vector Field Forces domain in `Engine/SimulationSpace/ForceFields/` untouched), adopt the latest HTML Particle Studio reference (`Experimental/ParticleEditor Final/` from `streamlinkinbox/Frontier@arena/4bfeedb2-frontier`, commit `5f14356`), and specify the complete Frontier-aligned modular architecture across `Engine/ParticleSpace/`, `ParticleExtension`, `Engine/Codecs/ParticleCodec`, `Engine/Interchange/ParticleInterchange`, `Engine/Workspaces/Particle/`, `Engine/Shaders/Particles/`, and reusable `Engine/Components/` UI units (including Content Browser drag-drop Colour Ramp presets) — verified via side-by-side HTML-vs-C++ screenshot parity for both UI and every rendered particle preset (all 102 presets reproduced verbatim in the final phase).

---

## 1. Completed Baseline & Milestone Safety Checkpoint

| Action | Path / Reference | Status |
| :--- | :--- | :--- |
| Stripped unaligned C++ particle plan | Removed `Engine/SimulationSpace/Particles/CxxParticleFoundationPlan.md` | Complete |
| Preserved C++ Vector Field Forces intact | `Engine/SimulationSpace/ForceFields/*` (`ForceFieldDescriptors.h`, `ForceFieldAccumulator`, `FieldLatticeCascade`, `FieldLatticeVulkanExecutor`, `FieldFormulaCompiler`, `VectorField`, `FlowField`, `TurbulenceField`, `ForceFieldEvaluationShared.slang`, `VectorFieldCodec`) untouched and directly reused | Verified |
| Imported HTML Particle Studio only | Merged `Experimental/ParticleEditor Final/` from `streamlinkinbox/Frontier` (`arena/4bfeedb2-frontier` @ `5f14356`) including `FireworkPatterns.js`, `ObjectSurfaceSampler.js`, `ParticleInfographics.js`, `presets.js` (102 presets), `shaders.js`, `engine.js`, `SoftwarePreviewEngine.js` | Complete |
| HTML reference verification | `node "Experimental/ParticleEditor Final/Engine/CheckForceFields.mjs"` (`PASS 65`) + ES module load check (`102` presets, `93` active, `9` quarantined, `10` firework shapes, `17` surface presets, `17` force types, `15` colour drivers, `6` ramp presets) | Verified |
| Milestone rollback commit | Committed as **`Pre Particle Editor Begin`** prior to writing any C++ ParticleSpace files | Locked |

---

## 2. Architectural Alignment & Naming Governance

Every C++20 file, namespace, type, enum, `.slang` shader, and `.ParticleSpace` format block obeys Frontier's architectural standards and explicit user naming rules:

1. **First-Class Space & Lifecycle Coordinator (`ParticleSpace` + `ParticleExtension`)**:
   - Lives directly at `Engine/ParticleSpace/` (`Frontier::ParticleSpace`), paralleling `Engine/GeometrySpace/`, `Engine/MaterialSpace/`, `Engine/DistanceFieldSpace/`, and `Engine/SimulationSpace/ForceFields/`.
   - `ParticleExtension` is the designated lifecycle coordinator (`Initialize`, `Tick`, `Suspend`, `Resume`, `Finalize`), coordinating deterministic stepping, GPU buffer ownership, `WorkspaceExchange` publication, and coupling to `Engine/SimulationSpace/ForceFields/`.
2. **Two-Tier Hybrid Persistence (`ParticleCodec`) + External `Interchange` (`ParticleInterchange`)**:
   - Native `.ParticleSpace` containers use `SpaceExtensionCategory::ParticleSpace` (`Engine/Codecs/SpaceContainerCodec.h`, slot `10`): Tier 1 PascalCase TOML manifest + Tier 2 64-byte-aligned binary sections (`.RampLut`, `.SurfaceAnchor`, `.StrandRest`, `.Revision`), embedding/linking Vector Field Forces via `Codecs::SerializeInlineForceSourcesToManifest`.
   - External interchange lives in `Engine/Interchange/ParticleInterchange.{h,cpp}` (paralleling `Engine/Interchange/GeometryInterchange.{h,cpp}`): imports/exports HTML Particle Studio TOML/JSON snapshots, host-handoff packets, and external `.obj`/`.stl`/`.gltf` surface anchor meshes via `GeometryInterchange`.
3. **Zero Duplication of Vector Field Forces (`Engine/SimulationSpace/ForceFields/`)**:
   - `ParticleSpace` directly consumes `Frontier::SimulationSpace::ForceFields` (`ForceFieldAccumulator`, `FieldLatticeCascade`, `FieldLatticeVulkanExecutor`, `FieldFormulaCompiler`, `ForceFieldEvaluationShared.slang`, `VectorFieldCodec`) and reuses `ForceInspectorView`, `ForcePresetBrowserPanel`, `VectorFieldOverlaySequence`, and `VectorFieldViewportCoordinator`.
4. **Strict Naming Compliance (`Documentation/BannedWordsVocabulary.md` + User Rulings)**:
   - Banned terms explicitly avoided everywhere: `Law`/`Laws`, `And` conjunctions, `Emitter`/`Emit`, `System`, `State`, `Node`, `Mesh`, `Palette`, `Module`, `Pipeline`, `Pass`, `Stage`, `Frame`, `Pool`, `Catalog`, `Kind`, `Flag`, `Value`, `Manager`, `Controller`, `Helper`, `Base`, `Common`, `Shared`, `ParticleReadback`, `PlaybackTransportBar`, `TimelineRulerScrubber`.
5. **Mandatory HTML-vs-C++ Visual Screenshot Parity Gate (`CLAUDE.md §2.5`)**:
   - **UI Parity Gate**: Capture ground-truth Chromium `.png` screenshots of `Experimental/ParticleEditor Final/index.html` first; then capture offscreen C++ `.png` screenshots of `ParticleWorkspaceHost` and visually compare them side-by-side until layout, controls, icons, census tiles, transport, ruler, inspector, and infographics match 1:1.
   - **Particle Render Parity Gate**: Capture ground-truth Chromium `.png` renders of HTML particle presets AND capture C++ produced particle `.png` renders at matching camera posture, deterministic seed, and elapsed time; inspect side-by-side to prove exact particle fidelity (zero simplified inventions).

---

## 3. Canonical Modular Folder Structure

```text
Engine/
├── ParticleSpace/                                      // First-class modular particle domain (Frontier::ParticleSpace)
│   ├── ParticleExtension.h / .cpp                      // Lifecycle coordinator (Initialize, Tick, Suspend, Resume, Finalize, Exchange)
│   ├── ParticleDocument.h / .cpp                       // Authoritative in-memory ensemble of rigs, Vector Field Force links, colliders & clock
│   ├── ParticleRig.h / .cpp                            // Single authored particle rig composing ReleaseOrigin + Dynamics + Colour + VisualCarrier
│   ├── ParticleBuffers.h                               // 16B-aligned GPU layouts (ParticleSlot 80B, StrandVertex 16B, GlobalUniform 240B, RigUniform 256B)
│   │
│   ├── Origins/                                        // Modular release-origin geometry types
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
│   ├── Colour/                                         // Modular colour, gradient, fill & ramp types (coupled with ColourSpace/ColourCodec + ContentBrowser)
│   │   ├── ParticleColourTypes.h                       // ColourSourceCategory (SolidPigment, LinearFill, MultiStopGradient, QuantizedRamp, BlackbodyThermal, SpeciesMapping, SpatialNoiseField)
│   │   ├── ColourDriverTypes.h                         // All 15 ColourDriverChannel modes (ModeDefault..LifeDuration), ColourWrapBehaviour, ColourBlendCurve
│   │   ├── ColourRampSampler.h / .cpp                  // Arbitrary-stop linear RGBA ramp sampling, 6 built-in ramp presets & 64B GPU LUT packing
│   │   └── ParticleColourEncoder.h / .cpp              // Packs ColourRamp LUT + driver uniforms identically for GPU compute/raster & CPU execution
│   │
│   ├── Dynamics/                                       // Modular simulation behaviour & physics solvers
│   │   ├── SimulationDynamicsTypes.h                   // Composable typed dynamics configuration (zero monolithic switch)
│   │   ├── BallisticMotion.h / .cpp                    // Gravity, buoyancy, aerodynamic drag, flutter, floor bounce, timed origin/sky bursts
│   │   ├── ForceFieldCoupling.h / .cpp                 // Directly binds Engine/SimulationSpace/ForceFields/ (19 ForceModels + LatticeCascade + Formulas)
│   │   ├── CurlManifold.h / .cpp                       // Planar 3D curl waves, spherical curl plasma, bokeh stream ribbons, object surface pour
│   │   ├── StrandConstraint.h / .cpp                   // 20-vertex XPBD/Verlet strands: cables/chains, vines/ropes, flags/sails, beads/chimes/drapes, tearing
│   │   ├── BotanicalSway.h / .cpp                      // Travelling wind stadium waves: grass/wheat, reeds, pinnate ferns, canopy trees, kelp, anemones, dandelion/blossom detachment
│   │   ├── SpiderwebWeave.h / .cpp                     // Real-time spinning Orb-Weave radial/spiral silk + Messy Attic Cobwebs + dew highlights
│   │   ├── FerrofluidMeniscus.h / .cpp                 // Rosensweig petri-dish spike meniscus + 3D levitated crown tracking orbiting/draggable magnets
│   │   ├── FractureFissure.h / .cpp                    // Glacial ice cracking propagation + seismic Voronoi basalt ground fracture + abyss tilt
│   │   ├── ShallowWaterSplash.h / .cpp                 // Puddle ripple rings, crown splashes, figure-8 vehicle tyre mud/water splashes, sticky clods
│   │   ├── EffectorLattice.h / .cpp                    // MoGraph grids (7 shapes × 6 drivers × twin placeable 3D effectors: Sphere/Box/Torus/Disc/Cone)
│   │   ├── FlockBoids.h / .cpp                         // Spatial-hash neighbour grid: cohesion, alignment, separation, predator-ball evasion & murmuration
│   │   ├── MolecularKinetics.h / .cpp                  // Lennard-Jones atoms (kT, epsilon, sigma, Langevin damping) + A+B<->C chemical reaction kinetics
│   │   ├── TransitionDerez.h / .cpp                    // Derez cube, shatter, coin stack, melt, reverse-derez, voxel blast, butterfly & glitch dissolves
│   │   ├── ShieldBarrier.h / .cpp                      // 6 barrier forms × 7 stackable motif bitmasks, radar sweeps, acoustic sonar, EMP domes, holo glitch
│   │   ├── PyroSpectacle.h / .cpp                      // Multi-stage bouquet fireworks, New Year's sky-text/shape shells, IR decoy V-salvo flares
│   │   └── InteractiveCollider.h / .cpp                // Interactive 3D thrown ball collider, floor/puddle contact, & SignedDistanceVolume collision
│   │
│   ├── Execution/                                      // Deterministic GPU & CPU executors + telemetry
│   │   ├── ParticleVulkanExecutor.h / .cpp             // Vulkan 1.3 compute + raster encoder routed through Engine/GeometricRaster/RenderDeviceInterface
│   │   ├── ParticleSoftwareExecutor.h / .cpp           // Deterministic CPU parity executor + RGBA rasterizer for Exhibits visual verification
│   │   └── ParticleMetricsCollector.h / .cpp           // Asynchronous alive count, active strand count, GPU timing & bounds snapshot collection
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
│   ├── Controls/
│   │   ├── SimulationPlaybackControls.h / .cpp         // Play, Pause, SingleStep, Restart, Loop, TimeScale (0.25x..2x), BurstTrigger, Timecode
│   │   ├── SimulationTimeRuler.h / .cpp                // Scrubbable simulation/animation time ruler, playhead needle, active track strip
│   │   ├── ColourRampEditor.h / .cpp                   // Solid / Fill / Multi-Stop Gradient / Quantized Ramp editor + drop-target slot for ContentBrowser ramps
│   │   └── ScalarCurveEditor.h / .cpp                  // Value-over-life / attenuation / falloff spline editor
│   ├── ContentBrowser/                                 // EXTENDED: built-in Colour Ramp presets + drag-and-drop ColourRampSlotRegistration
│   │   ├── ColourCodex.h / .cpp                        // Adds ColourRampPresetEntry repository (Spectrum, Blackbody, Aurora, Plasma, Ocean, SunsetNeon + custom)
│   │   ├── ThumbnailSheetRepository.h / .cpp           // Renders crisp gradient-strip thumbnails for ColourRamp presets under ContentCategory::Colours
│   │   └── ContentBrowserPanel.h / .cpp                // Adds RegisterColourRampSlot(...) for drag-drop assignment into any ColourRampEditor slot
│   └── Diagnostics/
│       ├── DiagnosticInfographicCanvas.h / .cpp        // Tactile animated overview infographic tile (17 particle/field archetypes + extensible)
│       └── LogStreamDrawer.h / .cpp                    // Collapsible live diagnostic event stream drawer with severity filters
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
        ├── ParticleBuffers.slang                       // 16B-aligned structs, PRNG, basis, curl/simplex noise, colour ramp sampling
        ├── ParticleForceCoupling.slang                 // Imports Engine/Shaders/Compute/ForceFieldEvaluationShared.slang (Vector Field Forces)
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

---

## 4. Modular Type Architecture (Zero Monolithic Switches)

### 4.1 Modular Visual Carriers & All 20 Sprite Cutouts

Every `ParticleRig` owns a typed `VisualCarrierSpec` (`std::variant`):

| Visual Carrier Type | GPU Shader Unit | Responsibilities |
| :--- | :--- | :--- |
| `SpriteBillboardCarrier` | `ParticleBillboardRasterization.slang` | All **20 procedural cutouts** (`0: Streak`, `1: ShadedSphere`, `2: BotanicalLeaf/Paper`, `3: SoftGlow`, `4: CubeProjection`, `5: MetallicCoin`, `6: IridescentBubble`, `7: DendriticSnowflake`, `8: CrystallineHailstone`, `9: BokehDisc`, `10: ShockwaveRing`, `11: PulseDome`, `12: HologramGlitch`, `13: WingedSilhouette`, `14: ViscousDroplet`, `15: RadarSonarSweep`, `16: EnergyBarrierShield`, `17: AuroraCurtain`, `18: GlassShardCrystal`, `19: VolumetricWaterMist`), bokeh head styles (`0..3`), `StreakThinness`, `OpticalBlendEquation` (`Additive` / `PremultipliedAlpha`) |
| `RibbonStrandCarrier` | `ParticleRibbonRasterization.slang` | 20-vertex continuous ribbon strips: velocity wake, surface stream pour, catenary cables/chains, leafy vines, solid woven cloth flags/sails, tapered grass/wheat/reeds blades, pinnate fern fronds, bark cylinders, giant kelp blades, sea anemone tentacles, decoy flare smoke plumes, viscous trails |
| `OrientedSolidCarrier` | `ParticleSolidRasterization.slang` | True 3D geometry instances: tumbling cubes, 3D faceted glass shards, coin stacks, 8-fragment floor shatter derez, height-extruded MoGraph cubes/boxes/hexagons/triangles/tiles, Voronoi fracture slabs |
| `WingedCreatureCarrier` | `ParticleWingedRasterization.slang` | 3D hinged dual-wing geometry for butterflies, birds, starling murmurations, bioluminescent fish schools, and butterfly-dissolve transitions |
| `DielectricBeamCarrier` | `ParticleLineArcRasterization.slang` | Bond segments (spiderweb radial/spiral silk, molecular bonds), continuous laser/plasma beams, Tesla/static sphere arcs, plasma cage & storm-grid arcs, 3D fibre tubes, strike sparks |
| `RefractionLensCarrier` | `ParticleLensRasterization.slang` | Relativistic black-hole accretion gravitational lens distortion & rising thermal heat-shimmer refraction |
| `BackingSurfaceCarrier` | `ParticleSurfaceBackingRasterization.slang` | Backing surface styles (`ObsidianDark`, `BrushedChrome`, `GhostWireframe`, `ParticlesOnlyHidden`) for all 17 surface presets + custom imported `PolygonNetwork` + interactive chrome ball collider + figure-8 rally vehicle hull |

### 4.2 Modular Colour, Gradient, Fill & Ramp Types + Content Browser Drag-Drop

Directly mirrors and generalizes HTML `COLOR_BY_MODES`, `COLOR_WRAP_MODES`, `COLOR_INTERP_MODES`, `COLOR_PALETTES`, and arbitrary-stop `colorStops`, coupled with `Engine/Codecs/ColourCodec.h` (`.ColourSpace`) and `Engine/Components/ContentBrowser/`:

- **`ColourSourceSpec`** (`std::variant`):
  1. `SolidPigmentSource` — single scene-linear RGBA tint + opacity multiplier.
  2. `LinearFillSource` — two-stop or three-stop `ColA -> ColB -> ColC` fill (1:1 compatibility with legacy HTML presets).
  3. `MultiStopGradientSource` — arbitrary ordered `std::vector<ColourStop>` (`Position01`, `LinearRgba`), supporting all 6 built-in presets (`Spectrum`, `Blackbody`, `Aurora`, `Plasma`, `Ocean`, `SunsetNeon`) plus user-authored `.ColourSpace` ramps.
  4. `QuantizedRampSource` — stepped/banded ramp with configurable stop positions.
  5. `BlackbodyThermalSource` — Kelvin temperature ramp (fire, welding, embers, flares, fireworks, plasma).
  6. `SpeciesMappingSource` — discrete per-species / per-role colour slots (chemistry `A`/`B`/`C`, spiderweb silk/dew, tree trunk/leaves, firework sub-shells).
  7. `SpatialNoiseFillSource` — 3D value-noise driven pigment modulation (`rampVNoise3` parity).
- **15 Exact `ColourDriverChannel` Drivers** (1:1 with HTML `COLOR_BY_MODES` codes `0..14`):
  - `0 ModeDefault` · `1 NormalizedLifetime` · `2 ParticleAgeSeconds` · `3 VelocitySpeed` · `4 VerticalVelocity` · `5 OriginAzimuthAngle` · `6 VelocityHeadingAngle` · `7 DistanceFromOrigin` · `8 WorldElevation` · `9 SpatialNoise3D` · `10 PerParticleRandomSeed` · `11 SpinOrientationAngle` · `12 DistanceToCamera` · `13 ParticleSizeMeters` · `14 AssignedLifespanSeconds`.
- **Remapping Controls**: `RangeMin`, `RangeMax`, `RepeatCount`, `PhaseOffset`, `ColourWrapBehaviour` (`Clamp`, `Repeat`, `Mirror`), `ColourBlendCurve` (`Linear`, `Smooth`, `Stepped`).
- **Content Browser Colour Ramp Presets & Drag-Drop**:
  - `ColourCodex` and `ThumbnailSheetRepository` expose built-in and project-authored **Colour Ramp Presets** (with smooth gradient-strip previews under `ContentCategory::Colours`).
  - `ContentBrowserPanel::RegisterColourRampSlot(ColourRampSlotRegistration)` allows dragging any Colour Ramp preset directly from the Content Browser into the **Colour Ramp Slot** of `ColourRampEditor` (usable across Particle, Material, Pattern, Stamp, Fluid, and ForceField workspaces).

### 4.3 Direct Reuse of Vector Field Forces (`Engine/SimulationSpace/ForceFields/`)

Zero re-implementation of vector-field physics. `ForceFieldCoupling` binds straight into our existing `Frontier::SimulationSpace::ForceFields` domain:

- Reuses `ForceFieldDescriptors.h` (`19 ForceModel` definitions, `ReachEnvelopeCategory`, `ForceAttenuationMode`, `ForceDurationMode`, `ForceMotionSpecification`), `ForceFieldAccumulator`, `FieldLatticeCascade`, `FieldLatticeVulkanExecutor`, `FieldFormulaCompiler` (`VectorFieldLang`), `VectorField`, `FlowField`, `TurbulenceField`, `Engine/Shaders/Compute/ForceFieldEvaluationShared.slang`, and `Engine/Codecs/VectorFieldCodec.{h,cpp}` (`.VectorFieldSpace`).
- Maps all 17 HTML `ForceFieldCatalogue` types cleanly to existing C++ `ForceModel` configurations (`WindGenerator`→`LinearWind`, `Tornado`→`RotationalColumn`, `Jet`→`FlowStream::SlipstreamJet`, `GustFront`→`AtmosphericGust`, `Microburst`→`RadialBurst`, `Thermal`→`FlowStream::ThermalUpdraft`, `Puff`/`Blast`→`RadialBurst`, `Wake`→`FlowStream::VesselWake`, `Current`→`FlowStream::Current`, `Attractor`, `Repulsor`, `Orbit`→`Torque`/`AxisAttractor`, `Lift`→`Buoyancy`, `Magnetic`→`MagneticDipole`/`LorentzField`, `Gravity`→`Attractor`, `Drag`) across all 7 receiver channels (`Particles`, `Debris`, `Foliage`, `Cloth`, `Fluid`, `Smoke`, `Rigid`).
- Editor UI reuses `ForceInspectorView`, `ForcePresetBrowserPanel`, `VectorFieldOverlaySequence`, and `VectorFieldViewportCoordinator` directly inside `ParticleWorkspaceHost`.

### 4.4 Modular Release Origins & Effector Geometry Types

- **Analytic Origins**: `Point`, `SphereVolume`, `Disc`, `BoxVolume`, `SurfaceLattice`, `DualElectrode`.
- **Procedural & Imported Surface Anchors (`SurfaceAnchorSampler`)**: All **17 surface presets (`0..16`)** (`XZ Floor Matrix`, `XY Wall Matrix`, `3D Obsidian Sphere`, `3D Obsidian Hand + Chrome Ball`, `3D Torus Ring`, `3D Cubic Volume`, `Utah Teapot`, `Sports Car Silhouette`, `Sculpted Head / Bust`, `Torus Knot (3,2)`, `Precision Gear / Cog`, `Quartz Crystal Cluster`, `Architectural Archway`, `Bevelled Monolith Cube`, `Faceted Spire / Cone`, `Stellated Star Polyhedron`, `Custom Imported PolygonNetwork` via `GeometryInterchange` `.obj`/`.stl`/`.gltf`/`.GeometrySpace`) with all 3 sampling modes (`EvenSurface`, `SurfaceInteriorVolume`, `RidgeCreaseAccent`) and Euler rotation.
- **Firework Pattern Sampler (`FireworkPatternSampler`)**: All **10 `FireworkShape` IDs (`0..9`)** (`Bouquet`, `SkyText` with custom UTF-8 string, `Heart`, `Star`, `Smiley`, `Saturn`, `Crown`, `Butterfly`, `Spiral`, `GrandFinale`).
- **MoGraph Effectors (`EffectorLattice`)**: All **7 `GridShape` modes** (`HalftoneCircles`, `UniformBoxes`, `HeightCubes`, `FlatTiles`, `Triangles`, `Hexagons`, `AutoMixed`), **6 `EffectorDriver` modes** (`FluidSimplexWave`, `OrbitingDualEffectors`, `CurlIslands`, `ExpandingPulseRing`, `BallForceFieldsOnly`, `Placeable3DEffectors`), and twin interactive 3D effectors with **5 `EffectorVolumeShape` types** (`Sphere`, `Box`, `Torus`, `Disc`, `Cone`).

---

## 5. `.ParticleSpace` Codec & `ParticleInterchange` Formatting

### 5.1 Native `.ParticleSpace` Two-Tier Hybrid Format (`Engine/Codecs/ParticleCodec.{h,cpp}`)

Uses `SpaceContainerEncoder` (`SpaceExtensionCategory::ParticleSpace`, schema v1, `CoordinateSystem = "RightHandZUp"`):

- **Tier 1 (PascalCase TOML Manifest)**:
  - `[Document]` — `Title`, `SchemaVersion`, `CoordinateSystem`, `Seed`, `TimeScale`, `PlaybackLoopSeconds`, `ActivePresetId`.
  - `[[ParticleRig]]` — Modular sub-tables: `[ParticleRig.Origin]`, `[ParticleRig.Carrier]`, `[ParticleRig.Colour]`, `[[ParticleRig.Colour.Stop]]`, `[ParticleRig.Dynamics.*]` (only active dynamics sub-tables written).
  - `[ForceField]` / `[[ForceSource]]` / `LinkedVectorFieldPaths` — serialized via `Codecs::SerializeInlineForceSourcesToManifest` (`VectorFieldCodec.h`).
  - `[InteractiveCollider]` — ball radius, bounce restitution, magnet position/orbit mode.
  - `[EditorViewport]` *(gated by `#if FRONTIER_EDITOR_ENABLED`)* — camera posture, overlay channel, minimap toggle, active tool.
- **Tier 2 (64-Byte-Aligned Binary Sections with FNV-1a 64-bit Checksums)**:
  - `.RampLut` — pre-baked linear RGBA gradient samples for zero-jitter GPU upload.
  - `.SurfaceAnchor` — cached sampled anchor positions/normals/curvature weights for custom `PolygonNetwork` surfaces.
  - `.StrandRest` — initial rest-length tables for spiderwebs, cloth grids, and canopy trees.
  - `.Revision` — optional embedded `RevisionTracker` undo/redo history via `RevisionCodec`.

### 5.2 External Interchange (`Engine/Interchange/ParticleInterchange.{h,cpp}`)

- **HTML Particle Studio Import/Export**: Lossless conversion between `Experimental/ParticleEditor Final/` TOML/JSON snapshots (all 129 `PE.baseParams` keys + Y-Up ↔ Right-Hand `+Z Up` transform) and native `ParticleDocument`.
- **Host Handoff Packet (`ParticleHandoffPacket`)**: Mirrors `Engine/handoff.js` (`PresetId`, `RigName`, typed parameter overrides) and publishes live updates across `WorkspaceExchange` (`SpaceExtensionCategory::ParticleSpace`) for Fluid, Fracture, and World workspaces.
- **External 3D Surface Intake**: Delegates `.obj` / `.stl` / `.gltf` drops to `Interchange::GeometryInterchange` → `PolygonNetwork` → `SurfaceAnchorSampler`.

---

## 6. Reusable UI Components (`Engine/Components/`)

Every reusable widget identified in the HTML Particle Studio is authored once under `Engine/Components/` (built strictly from `PixelSpace`, `CapsuleButton`, `ExclusiveSelector`, `DropdownSelector`, `ScalarSlider`, `TextInputField`, `ThemeRepository`, `TypographyRepository`, and `VectorIconRepository` backed by `Experimental/StudioIconSet/`):

| Reusable Component | Location | Controls & Capabilities | Shared By Workspaces |
| :--- | :--- | :--- | :--- |
| **`SimulationPlaybackControls`** | `Engine/Components/Controls/` | `Restart` (`R`), `Play`/`Pause` (`Space`), `SingleStep` (`S`), `Loop` toggle, `TimeScale` (`0.25×`, `0.5×`, `1.0×`, `2.0×`), tabular `Timecode` (`00:00.000`), optional `Burst` (`B`) trigger, `Live / Cached` indicator; icons from `StudioIconSet` (`particle-play`, `particle-pause`, `particle-restart`, `particle-loop`, `particle-step`, `particle-burst`) | Particle, Fluid, Fracture, ForceField, Vehicle, Animation |
| **`SimulationTimeRuler`** | `Engine/Components/Controls/` | Configurable time ruler (`0s..30s`), step counter, scrubbable playhead needle, active track strip | Particle, Fluid, Fracture, Animation, Audio |
| **`ColourRampEditor`** | `Engine/Components/Controls/` | Mode switch (`Solid` / `Fill` / `Multi-Stop Gradient` / `Quantized Ramp` / `Blackbody`), draggable gradient stop strip, **Content Browser drop-target slot** (`RegisterColourRampSlot`), `DriverChannel` dropdown (15 modes), `RangeMin`/`RangeMax`, `Repeat`, `Phase`, `Wrap` (`Clamp`/`Repeat`/`Mirror`), `Interp` (`Linear`/`Smooth`/`Stepped`), `.ColourSpace` load/save | Particle, Material, Pattern, Stamp, Fluid, ForceField |
| **`ScalarCurveEditor`** | `Engine/Components/Controls/` | Interactive 2D curve canvas for `SizeOverLife`, `OpacityOverLife`, `AttenuationFalloff`, `ModulationEnvelope` | Particle, ForceField, Fluid, Animation, Vehicle |
| **Content Browser Colour Ramp Presets** | `Engine/Components/ContentBrowser/` | `ColourRampPresetEntry` in `ColourCodex` + rendered gradient-strip tiles in `ThumbnailSheetRepository` + `RegisterColourRampSlot(...)` drag-drop in `ContentBrowserPanel` | All Frontier Workspaces |
| **`DiagnosticInfographicCanvas`** | `Engine/Components/Diagnostics/` | Reusable live overview telemetry tile rendering domain-specific animated diagrams (17 particle/force archetypes ported from `ParticleInfographics.js`, extensible for fluid/vehicle/fracture) | Particle, ForceField, Fluid, Vehicle, Fracture |
| **`LogStreamDrawer`** | `Engine/Components/Diagnostics/` | Collapsible bottom diagnostic stream drawer with timestamp, severity sign, category filter & clear/copy actions | All Frontier Workspaces |

---

## 7. Phased Implementation Roadmap & HTML-vs-C++ Visual Verification Gates

| Phase | Deliverable | Key Units & Mandatory Verification Gate |
| :--- | :--- | :--- |
| **Phase 0 — Milestone Lock** | Commit refined plan + BannedWords update as **`Pre Particle Editor Begin`** | Clean rollback checkpoint committed & pushed before any C++ particle source is written. |
| **Phase 1 — Core Modular Types & `ParticleSpace` Foundation** | `ParticleBuffers.h`, `ParticleRig`, `ParticleDocument`, `ParticleExtension`, `Origins/*` (`AnalyticOrigins`, `StructuralOrigins`, `SurfaceAnchorSampler`, `FireworkPatternSampler`), `VisualCarriers/*`, `Colour/*`, `Dynamics/SimulationDynamicsTypes.h` | Compiles cleanly across `Editor`/`Development`/`Shipping`; zero banned words / zero `And` identifiers; `ParticleFoundationValidation` verifies 16B GPU struct alignment, all 15 colour drivers, 6 ramp presets, 17 surface anchor samplers, and 10 firework pattern samplers, emitting visual contact sheets into `Exhibits/Images/Particles/`. |
| **Phase 2 — `.ParticleSpace` Codec & `ParticleInterchange`** | `Engine/Codecs/ParticleCodec.{h,cpp}`, `Engine/Interchange/ParticleInterchange.{h,cpp}`, `SpaceContainerCodec` wiring, inline `VectorFieldCodec` force-source round-trip | `ParticleCodecInterchangeValidation` proves bit-exact `.ParticleSpace` Two-Tier Hybrid round-trip (TOML + 64B binary sections + FNV-1a checksums + `.Revision`) and lossless import of all 102 presets exported directly from `Experimental/ParticleEditor Final/Engine/presets.js`. |
| **Phase 3 — Modular Dynamics & Vector Field Force Coupling** | All 16 modular dynamics units under `Engine/ParticleSpace/Dynamics/` + `ParticleSoftwareExecutor` + `ParticleForceCoupling.slang` importing `ForceFieldEvaluationShared.slang` | `ParticleDynamicsValidation` verifies every physics solver independently (ballistics, all 19 `ForceModel`s & 7 channels, curl manifolds, XPBD 20-vertex strands + tearing, botanical sway, spiderweb weave, ferrofluid Rosensweig spikes, ice/seismic fracture, shallow-water mud splashes, MoGraph effectors, boids + predator scatter, Lennard-Jones + chemistry, derez transitions, shields, pyro/flares, ball/SDF collider). |
| **Phase 4 — Pure `.slang` Vulkan 1.3 Compute & Raster Execution** | All 14 `.slang` shaders under `Engine/Shaders/Particles/` + `ParticleVulkanExecutor` (via `RenderDeviceInterface`) + `ParticleMetricsCollector` | Compiled via `slangc`; Hardware Vulkan + SwiftShader offscreen validation (`ParticleVulkanValidation`) renders every visual carrier to `.png` proofs under `Exhibits/Images/Particles/` and compares directly against headless Chromium HTML Particle Studio renders. |
| **Phase 5 — Reusable `Engine/Components/` UI, Content Browser Ramps & `ParticleWorkspace`** | `SimulationPlaybackControls`, `SimulationTimeRuler`, `ColourRampEditor`, `ScalarCurveEditor`, Content Browser `ColourRampPresetEntry` + drag-drop `RegisterColourRampSlot`, `DiagnosticInfographicCanvas`, `LogStreamDrawer` + `Engine/Workspaces/Particle/*` + `WorkspaceCapabilityRegister` | **HTML-vs-C++ UI Screenshot Parity Gate (`CLAUDE.md §2.5`)**: Capture Chromium screenshot of `Experimental/ParticleEditor Final/index.html` first; then capture offscreen C++ `.png` of `ParticleWorkspaceUIValidation` and inspect side-by-side to verify 1:1 layout, controls, icons, and Content Browser Colour Ramp drag-drop. |
| **Phase 6 (Final Step) — Complete 102-Preset Registry & Side-by-Side HTML-vs-C++ Particle Parity Proof** | `Engine/ParticleSpace/Presets/*` implementing **all 102 presets** verbatim from `Experimental/ParticleEditor Final/Engine/presets.js` — **zero simplified inventions, zero omitted presets** | **HTML-vs-C++ Particle Screenshot Parity Gate**: Import every preset via `ParticleInterchange`, assert 100% parameter parity against `ParticlePresetRegistry`, capture matching `.png` renders from BOTH HTML Particle Studio (headless Chromium) AND C++ (`ParticlePresetParityValidation`) at identical camera, seed, and elapsed time across all **15 categories (93 active + 9 quarantined = 102/102)**, and visually compare them side-by-side. |

---

## 8. Complete 102-Preset Parity Ledger (Mandatory Phase 6 Deliverable)

Every single preset in `Experimental/ParticleEditor Final/Engine/presets.js` must be reproduced verbatim in `Engine/ParticleSpace/Presets/` using the modular `ReleaseOrigin`, `VisualCarrier`, `ColourSource`, and `Dynamics` types — zero dropped presets, zero simplified inventions:

| # | Category (HTML `VFX_CATEGORIES`) | Count | Exact Preset IDs Required (102 Total: 93 Active + 9 Quarantined) | Primary Carrier & Dynamics Composition |
| :-: | :--- | :-: | :--- | :--- |
| 1 | **Ballistics & Sparks** | 10 | `coin-fountain`, `magnetic-particles`, `sparks`, `glass`, `shockwave-ring`, `plasma-laser-beam`, `plasma-tracers`, `projectile-trails`, `welding-grinder-sparks`, `strikeSparks` | `SpriteBillboard` (`Streak`/`Glow`/`Coin`/`ShockwaveRing`), `OrientedSolid` (`GlassShardCrystal`), `RibbonStrand` (trails), `DielectricBeam` (laser/strike sparks) + `BallisticMotion`, `ForceFieldCoupling` (`MagneticDipole`) |
| 2 | **Pyro & Water** | 10 | `bubbles`, `foam`, `fire`, `smoke`, `explosion`, `embers`, `watermist`, `lapilli`, `steam`, `viscous-dripping` | `SpriteBillboard` (`SoftGlow`/`IridescentBubble`/`VolumetricWaterMist`/`ShadedSphere`/`ViscousDroplet`), `RibbonStrand` (viscous trails) + `BallisticMotion` (buoyancy/drag/bounce), `BlackbodyThermalSource` |
| 3 | **Storms & Weather** | 8 | `rain`, `hail`, `tornado`, `sandstorm`, `snow`, `sand`, `ash`, `debris` | `SpriteBillboard` (`Streak`/`DendriticSnowflake`/`CrystallineHailstone`/`ShadedSphere`/`BotanicalLeaf`) + `BallisticMotion`, `ForceFieldCoupling` (`RotationalColumn`, `AtmosphericGust`, `LinearWind`) |
| 4 | **Wind-Blown** | 10 | `leaves`, `paper`, `petals`, `seeds`, `feathers`, `confetti`, `boids-flock`, `boids-butterflies`, `boids-fish-school`, `starling-murmuration` | `SpriteBillboard` (`BotanicalLeaf`/paper), `WingedCreatureCarrier` (birds/butterflies/fish/starlings) + `BallisticMotion` (flutter), `FlockBoids`, `ForceFieldCoupling`, `InteractiveCollider` (predator ball) |
| 5 | **Atmosphere & Sky** | 5 | `flares`, `pollen`, `fireworks`, `fireworks-spectacle`, `aurora-borealis` | `SpriteBillboard` (`AuroraCurtain`/`SoftGlow`/`Streak`), `RibbonStrand` (decoy flare smoke), `FireworkPatternSampler` (all 10 shapes + sky text) + `PyroSpectacle` |
| 6 | **Silk & Curl** | 10 | `curl-fur-field`, `curl-sphere-plasma`, `bokeh-stream-ribbons`, `bokeh-object-pulse`, `tesla-sphere-lightning`, `static-sphere-shock`, `spiderweb-orb`, `spiderweb-cobweb`, `plasma-cage-arcs`, `lightning-web-mesh` | `RibbonStrand` (curl wave/bokeh stream/surface pour), `SpriteBillboard` (`BokehDisc`/`Streak`), `DielectricBeamCarrier` (arcs/webs/cages) + `CurlManifold`, `SpiderwebWeave`, `SurfaceAnchorSampler`, `InteractiveCollider` |
| 7 | **Shields & Tech** | 6 | `emp-pulse-dome`, `radar-sweep`, `sonar-ping`, `hologram-data-glitch`, `energy-shield-hex`, `energy-shield-prism` | `SpriteBillboard` (`PulseDome`/`RadarSonarSweep`/`HologramGlitch`/`EnergyBarrierShield`) + `ShieldBarrier` (6 forms × 7 motifs + live ball impact ripple) |
| 8 | **Transitions** | 9 | `derez-cube`, `cube-coins`, `melt-away`, `reverse-derez`, `voxel-explosion`, `coin-stack`, `dissolve-nothing`, `dissolve-butterflies`, `glitch-dissolve` | `OrientedSolid` (`Cube`/`ShatterFragment`/`Coin`), `WingedCreatureCarrier`, `SpriteBillboard` (`HologramGlitch`) + `TransitionDerez`, `SurfaceLatticeOrigin` |
| 9 | **Space & Distortion** | 2 | `black-hole`, `heat-shimmer` | `RefractionLensCarrier` (`ParticleLensRasterization.slang`) + accretion spiral kinematics & thermal updraft distortion |
| 10 | **Cloth & Rigging** | 7 | `cables-chains`, `vines-ropes`, `flag-banner`, `ship-sail`, `beaded-door-curtain`, `bamboo-wind-chimes`, `velvet-theater-drapes` | `RibbonStrandCarrier` (solid woven cloth, chain links, leafy vines, bead/chime/drape strands) + `StrandConstraint` (20-vertex XPBD + ball push-out + tear threshold), `ForceFieldCoupling` |
| 11 | **Foliage & Flora** | 7 | `grass-wheat-field`, `tall-reeds`, `interactive-fern`, `interactive-tree`, `kelp-forest`, `sea-anemone`, `dandelion-puff` | `RibbonStrandCarrier` (tapered blades, pinnate ferns, bark cylinders, kelp, anemone tentacles), `SpriteBillboard` (wheat ears, cattails, canopy leaves, pappus seeds) + `BotanicalSway`, `InteractiveCollider` |
| 12 | **Motion & Grids** | 5 | `effector-grid-cubes`, `effector-sphere-dots`, `effector-hand-halftone`, `effector-torus-hex`, `effector-object-placer` | `OrientedSolidCarrier` + `SpriteBillboard` (7 MoGraph shapes) + `BackingSurfaceCarrier` (17 surfaces + custom OBJ/STL) + `EffectorLattice` (6 drivers + twin placeable 3D effectors) |
| 13 | **Ferrofluid & Fields** | 2 | `ferrofluid-spikes`, `ferrofluid-orb` | `OrientedSolidCarrier` / meniscus spike geometry + `FerrofluidMeniscus` (orbiting/draggable magnet + `ForceFieldCoupling`) |
| 14 | **Fracture & Cracks** | 2 | `cracked-ice-sheet`, `ground-crack-quake` | `OrientedSolidCarrier` (Voronoi slabs + fissure debris) + `FractureFissure` (propagation front + abyss tilt) |
| 15 | **Quarantine (Preserved for Completeness)** | 9 | `splash`, `impact-dust-ring`, `splatter-burst`, `car-puddle-splash`, `puddle-ripple-splash`, `mud-splatter-heavy`, `blossom-branch-detach`, `jelly-slime-blob`, `slime-tendril-cluster` | `ShallowWaterSplash`, `BotanicalSway` (blossom detachment), `StrandConstraint` (soft-body jelly/slime tendrils), `BackingSurfaceCarrier` (figure-8 rally car) — flagged `Quarantined` matching HTML `QUARANTINE_IDS` |

*(Plus interactive scene additions supported in the HTML Studio Outliner/Add menu: **Molecular Lennard-Jones Atoms** (`kind = 3`), **Chemical Reaction Kinetics** (`kind = 4`, `A + B <-> C`), **17 Bounded Vector Field Force Sources**, **Symbolic `VectorFieldLang` Formulas**, and **Interactive 3D Ball / Magnet / Effector / Anemometer Handles**.)*

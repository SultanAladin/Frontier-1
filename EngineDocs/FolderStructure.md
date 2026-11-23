# Frontier — Folder Structure Plan

Historical reference only. Current ownership is in `Documentation/FolderStructure.md`;
current progress and findings belong in `Documentation/Tracker.md`. Do not maintain a competing map or tracker here.

Six-pillar engine under `Internal/`. Coordinator suffix = `…Extension`. Editors are separate
executables. A WorkspaceDocument = one tab = one editable authoring session (Simulation is
document-less). Naming follows CLAUDE.md + SKILL-Naming.md (banned: Application, Division,
System-coordinator, Core, mesh, shell, binding, Stage, Trial, harness, Entity/Node/Parent/Child…).

```
Frontier/                                          ← C:\Users\OS\Documents\Projects\Frontier
│
├── Build.bat                                       // Root build entry → Automation/BuildPlan.ps1
├── CLAUDE.md   README.md   .gitignore   .claudeignore
│
├── Automation/                                     // Build orchestration (run via PowerShell only)
│   ├── BuildPlan.ps1                               // Compile/link driver invoked by every Build.bat
│   ├── ShaderPlan.ps1                              // glslc → .spv for every Internal/**/Shaders source. Stale-gated; owns
│   │                                               //   the per-shader --target-env and the two-variant route defines
│   └── ValidationRun.ps1                           // NEW: launch every …Validation target, collect pass/fail
│
├── ExternalPackages/                               // Vendored deps (git-ignored, .claudeignore'd)
│   └── (Vulkan, volk, VMA, glm, imgui, slang, SPIRV-Reflect, stb, MikkTSpace,
│        thorvg, tomlpp, tracy, xxHash, imgui-node-editor)
│
├── EngineContent/                                  // Shared runtime assets (fonts, default themes, icons)
│   └── ReferenceMaterials/Matcaps/                 // Matcap discs (512² RGBA PNG) the GPU solid pass shades extruded bodies with
│                                                   // { Matcap-Chrome.png, Matcap-PolishedSteel.png, Matcap-ClayGrey.png, Matcap-NeutralCool.png }
│
├── Build/                                          // Intermediate build output (git-ignored)
│   ├── obj/                                        // Per-translation-unit .obj
│   └── lib/                                        // Static libs per pillar
│
├── Internal/                                       // ★ THE ENGINE — pillars
│   │
│   ├── EngineContext/                              // ══ pillar 1 — foundation + shared runtime services ══
│   │   │
│   │   ├── EngineHost/                             // engine lifecycle (was Root/; "Core" banned)
│   │   │   ├── EngineHost.{h,cpp}                  // 🟢 initialize → loop → finalize (was RootExtension)
│   │   │   ├── ExtensionRegistry.{h,cpp}           // 🟢 registry of active extensions
│   │   │   ├── TimingExtension.{h,cpp}             // 🟢 delta accumulation
│   │   │   └── MemorySegment.{h,cpp}               // 🧩 contiguous block provisioning
│   │   │
│   │   ├── Configuration/
│   │   │   ├── EngineConfiguration.{h,cpp}
│   │   │   └── RenderConfiguration.{h,cpp}
│   │   │
│   │   ├── Math/
│   │   │   ├── LinearAlgebra.h                     // (live)
│   │   │   ├── VectorAlgebra.{h,cpp}              // ← retired EngineInfrastructure/Math
│   │   │   ├── IntersectionSolver.{h,cpp}          // ← retired
│   │   │   ├── MatrixOperations.{h,cpp}
│   │   │   └── QuaternionRotation.{h,cpp}
│   │   │
│   │   ├── MetricSpace/                            // (live) spatial coordinate primitives
│   │   │   └── CoordinateSpace.h  CoordinateProjection.h  DistanceMetric.h  SpatialExtent.h  README.md
│   │   │
│   │   ├── SpatialAcceleration/                    // (live) octree / BVH partitioning
│   │   │   ├── ToroidalAddressing.h                // 🟢 HEADER-ONLY scroll arithmetic — positive modulo, floor-to-cell, exposed-strip
│   │   │   │                                       //    solve. Shared by the WORLD-space GI field and the LIGHT-space sun window; a
│   │   │   │                                       //    sibling .cpp would link-fail the two Validation targets that gate this math
│   │   │   ├── ToroidalClipmapField.{h,cpp}        // camera-tracked 3D WORLD-space voxel clipmap (GI probes only — not the sun's)
│   │   │   └── SpatialPartition.{h,cpp}  README.md
│   │   │
│   │   ├── MicroUtils/                             // cross-pillar helpers — CANONICAL shared identity/store utils
│   │   │   ├── IdentifierAllocation.{h,cpp}  ColorConversion.{h,cpp}  StringFormatting.{h,cpp}
│   │   │   ├── HashComputation.{h,cpp}  RangeArithmetic.{h,cpp}
│   │   │   ├── VacancyTable.{h,cpp}                // ★ canonical (Scene + Revision include this)
│   │   │   ├── RecordToken.h                        // ★ canonical generational identity value (shared by every store)
│   │   │   ├── TokenIssuer.{h,cpp}                 // ★ canonical (templated on the store; mint/revoke over parallel arrays)
│   │   │   └── TokenAuthentication.{h,cpp}         // ★ canonical (templated on the store; stale-token guard + ResolveEntry)
│   │   │
│   │   ├── Motion/                                 // shared animation math (was Interface/Motion)
│   │   │   ├── MotionExtension.{h,cpp}  MotionEvaluator.{h,cpp}
│   │   │   ├── Interpolation/  ScalarInterpolation, VectorInterpolation, ColorInterpolation
│   │   │   ├── Easing/         EasingProfile, BezierEasing, EaseTable   // EaseTable replaces "Tween"
│   │   │   ├── Dynamics/       SpringDynamics, InertiaDynamics, DampingDynamics
│   │   │   └── Sequencing/     TransitionDescriptor, SequenceAnimator, TimelineAnimator
│   │   │
│   │   ├── Input/                                  // ALL input — mechanic + routing (nothing in Interface)
│   │   │   ├── InputExtension.{h,cpp}              // 🟢 raw events → input maps → routed actions
│   │   │   ├── InputSnapshot.h                     // (live) per-frame input state
│   │   │   ├── InputMap.{h,cpp}  InputMapRegistry.{h,cpp}  InputPreset.{h,cpp}  InputContext.{h,cpp}
│   │   │   ├── KeyCombination.{h,cpp}  GestureSpecification.{h,cpp}
│   │   │   ├── Routing/  OperationRouter, ActionInvoker, ModeActivator, ToolEngager
│   │   │   └── Build.bat
│   │   │
│   │   ├── Audio/                                  // shared audio service — 📝 PLANNED, see PLAN-Audio.md (no source ported yet)
│   │   │   ├── PLAN-Audio.md                       // 🧩 authoritative engine audio design (build blueprint)
│   │   │   ├── AudioExtension.{h,cpp}              // 🧩 device open, mix advance, per-cycle drive
│   │   │   ├── AudioConfiguration.{h,cpp}          // 🧩 sample rate, channel layout, buffer size
│   │   │   ├── AudioDevice.{h,cpp}                 // 🧩 backend output stream ownership
│   │   │   ├── SoundSource.{h,cpp}                 // 🧩 one playable source (gain, pitch, loop)
│   │   │   ├── SpatialEmitter.{h,cpp}              // 🧩 3D-positioned source (attenuation, panning)
│   │   │   ├── AudioListener.{h,cpp}               // 🧩 listener pose (from active camera)
│   │   │   ├── MixOrder.{h,cpp}                    // 🧩 bus/submix ordering (not "graph"/"chain")
│   │   │   ├── AudioSampleStore.{h,cpp}            // 🧩 loaded PCM/streamed clip store
│   │   │   └── Build.bat
│   │   │
│   │   ├── Scene/                                  // shared world tree (every editor uses it)
│   │   │   ├── SceneExtension.{h,cpp}              // 🟢 scene assembly + update
│   │   │   ├── SceneDirectory.{h,cpp}             // owning tree container
│   │   │   ├── RecordEntry.{h,cpp}               // one tree item (never Node/Entity)
│   │   │   ├── RecordArchive.{h,cpp}             // dense store — was RecordStore (VacancyTable/TokenIssuer/RecordToken ← MicroUtils)
│   │   │   └── AdjacencyTable.{h,cpp}            // connectivity store (folds EnclosureAtlas + LateralTraversal)
│   │   │
│   │   └── Navigation/                             // camera + ViewCube (shared view control)
│   │       ├── Camera/
│   │       │   ├── CameraConfiguration.h
│   │       │   ├── CameraNavigation/  CameraNavigation
│   │       │   ├── CameraConstraint/  DistanceBoundary, PitchBoundary
│   │       │   ├── CameraProjection/  ProjectionEvaluation, FrustumBoundary, ViewFrameEvaluation
│   │       │   └── CameraTransition/  CameraEaseTable, CameraPoseInterpolation   // calls EngineContext/Motion
│   │       ├── ViewportOrientationManipulator/     // (live ViewCube)
│   │       │   ├── ViewportOrientationManipulator.{h,cpp}  OrientationManipulatorContext.h
│   │       │   ├── Configuration/ ManipulatorConfiguration.h, AlignmentPreset.h, SphericalCoordinateTable.h
│   │       │   ├── Intersection/  RayIntersectionSolver.h, IntersectionResult.h, DepthEvaluation.h
│   │       │   ├── Projection/    LabelCoordinateProjection.h, LabelAlignment.h, TransformAlignment.h
│   │       │   ├── Topology/      ManipulatorPartition.h, SurfacePatchDefinition.h
│   │       │   └── AuxiliaryTriggers/ CameraResetTrigger.h, ProjectionModeToggle.h
│   │       └── Build.bat
│   │
│   ├── Platform/                                   // ══ pillar 1b — OS backend (feeds EngineContext) ══
│   │   ├── Windowing/
│   │   │   ├── PlatformWindow.h                    // (live) abstract contract
│   │   │   ├── PlatformWindowWin32.cpp             // (live) native Win32
│   │   │   ├── PlatformWindowX11.cpp  PlatformWindowWayland.cpp  PlatformWindowStub.cpp   // (live)
│   │   │   └── WindowContext.{h,cpp}              // ← retired
│   │   ├── Concurrency/   WorkerPool.{h,cpp}       // ← retired (job pool)
│   │   ├── Storage/       OpenFileRequest.{h,cpp}, SessionLogPaths.{h,cpp}   // ← retired
│   │   ├── InputProvider/  InputEncoder.{h,cpp}     // raw OS capture (in PlatformWindow) → normalized edges (was "InputBackend"; Backend banned)
│   │   └── Build.bat  README.md
│   │
│   ├── Simulation/                                 // ══ pillar 2 — the DYNAMIC runtime only ══
│   │   ├── Physics/                               // ← retired Authoring/Physics (real rigid-body kernel)
│   │   │   ├── PhysicsExtension.{h,cpp}          // 🟢 loop + broad-phase
│   │   │   ├── Collision/  ColliderShape, NarrowPhaseContact, AnalyticContact, ConvexDistance, ContactManifold
│   │   │   ├── RealTime/   RigidState, ImpulseResponse, OrientationAlgebra
│   │   │   └── PhysicsValidation.cpp             // ← was PhysicsTrial.cpp ("Trial" banned)
│   │   ├── Cloth/                                 // NEW: soft-body / fabric
│   │   │   └── ClothExtension, FabricEnvelope, SpringConstraint, IntegrationSolver, WindEffector
│   │   └── Gameplay/                              // NEW: game-logic execution (document-less)
│   │       └── GameplayExtension, ActorRecord ("Entity" banned), BehaviorDescriptor,
│   │          BehaviorEvaluator, TriggerVolume
│   │
│   ├── Graphics/                                  // ══ pillar 3 — visual output ══
│   │   ├── Render/                                // 🟢 (live) RenderExtension coordinates
│   │   │   ├── RenderExtension.{h,cpp}           // 🟢 (live)
│   │   │   ├── RenderingConfiguration.{h,cpp}    // ← retired
│   │   │   ├── Device/       VulkanHost, VulkanImguiBridge, WindowSubstrate (live)
│   │   │   │                 + DeviceAssembly, CommandStructure, SwapchainDescriptor (← retired)
│   │   │   ├── Resources/    BufferAllocation, LayerTextureAllocation   // ← retired
│   │   │   ├── Submission/   SubmissionOrder, SubmissionDependencyTable  // ← retired (Stage→Submission)
│   │   │   ├── Timing/       FrameTimingLedger, GpuMemoryLedger, CelestialProfileLedger  // ← retired
│   │   │   ├── Viewport/     ViewportTarget   // ← retired
│   │   │   ├── Surface/      PolygonSurface, SilhouetteOutline, SkyBackdrop, CelestialSurface,
│   │   │   │                 HorizonOcclusion, MaterialProperties.h, MatcapProperties.h,
│   │   │   │                 BrightStarCatalogue.h + Shaders/ (SurfaceForward, ChannelGBuffer,
│   │   │   │                 BrdfIntegration, Horizon…, Sky…, PaintUvRaster)   // ← retired
│   │   │   ├── Radiance/     RadianceTarget, RadianceResolveInscription + Shaders/   // 🟢 (live, P5.9b)
│   │   │   │                 linear HDR scene target (R16G16B16A16_SFLOAT) + the ONE tone map
│   │   │   │                 (Khronos PBR Neutral) into the _SRGB swapchain. Scene (sky+shade)
│   │   │   │                 renders here; display-referred overlays draw after the resolve.
│   │   │   ├── Illumination/ DeferredShadePass, DeferredShadeCompositePass, IlluminationSettings,
│   │   │   │                 IlluminationTierConfiguration, SubmissionSchedule + Shaders/
│   │   │   │                 (Reservoir*, ScreenProbe*, Reflection*, DeferredShade*)   // ← retired ReSTIR GI
│   │   │   ├── Shadow/       (← retired + Shaders/)
│   │   │   ├── Outline/      ComponentOverlaySurface   // ← retired
│   │   │   ├── Preview/      PreviewStudioTarget + Shaders/   // ← retired
│   │   │   ├── Diagnostics/  DiagnosticArchive   // (live)
│   │   │   ├── Extension/    RenderExtension   // (live — extensible pass registry)
│   │   │   └── Build.bat  README.md
│   │   ├── Atmosphere/                            // (live) Hillaire 2020 sky
│   │   │   ├── SkyAtmosphere.{h,cpp}  AtmosphereProfile.h
│   │   │   └── Shaders/ (Transmittance.comp, MultiScatter.comp, SkyView.frag, SkyDome.{vert,frag},
│   │   │                 FullscreenTriangle.vert, AtmosphereCommon.glsl, TransmittanceLookup.glsl)
│   │   ├── Acceleration/                          // 🟢 (live) GPU LBVH build for the surfel GI prepass — 4 dispatches, no CPU step
│   │   │   ├── RadixSortSubmission.{h,cpp}        // 8-bit-digit LSD radix sort over 32-bit keys + a payload. STABLE by construction:
│   │   │   │                                      //    one lane per bin sweeps the tile in ascending index (RadixKeyScatter lines
│   │   │   │                                      //    93-107), so equal keys keep primitive order. 🔴 the Karras delta() tie-break
│   │   │   │                                      //    DEPENDS on that — reorder the loop and the tree build hangs (TDR), not renders
│   │   │   │                                      //    wrong. Ping-pong parity sets, capacity 2^21 keys, refuses beyond it
│   │   │   ├── VolumeBoundsSubmission.{h,cpp}     // dispatch 1: scene AABB over triangle CENTROIDS, feeding the Morton normalisation.
│   │   │   │                                      //    🔴 no float atomics on this device, so the 6 accumulators are ORDERED-INT
│   │   │   │                                      //    (floatBitsToInt, XOR 0x7FFFFFFF when negative). Seeds are ±inf and differ, so
│   │   │   │                                      //    the reseed is TWO vkCmdFillBuffer ranges — and it is recorded WITH the
│   │   │   │                                      //    dispatch, because skipping it silently returns the union of every rebuild
│   │   │   ├── GeometryTreeBuild.{h,cpp}          // the BOTTOM level (BLAS): a SAH tree over ONE mesh's triangles in LOCAL space, on the
│   │   │   │                                      //    CPU, ported 1:1 from three-mesh-bvh. 🔴 the CPU may only ever see local-space
│   │   │   │                                      //    per-mesh geometry — a tree built here is shared by every instance forever, so
│   │   │   │                                      //    handing it world space converts a once-per-asset cost into a per-frame one.
│   │   │   │                                      //    ParentTable (child->parent, for a future skinned refit) only under DynamicCondition
│   │   │   ├── GeometryArenaSubmission.{h,cpp}    // uploads those trees: every mesh's node blob CONCATENATED into one device buffer +
│   │   │   │                                      //    a per-mesh slice table, so the trace binds a fixed set of buffers however many
│   │   │   │                                      //    meshes the world holds (descriptorIndexing is available but NOT enabled here).
│   │   │   │                                      //    🔴 append-then-upload is two phases: sizing needs every mesh, so appending must
│   │   │   │                                      //    not allocate. NodeOffset is in WORDS while NodeCount is in NODES
│   │   │   ├── GeometryStreamConcatenation.{h,cpp} // merges every mesh's vertex/index stream (heads + floor) into ONE device buffer at load,
│   │   │   │                                      //    so the surfel spawn/trace bind a single vertex + index SSBO regardless of mesh count.
│   │   │   │                                      //    Emits a per-mesh base-offset table; the floor rides the same claim as the heads
│   │   │   └── Shaders/ (RadixBinTally.comp, RadixBlockScan.comp, RadixBlockBaseAdd.comp, RadixKeyScatter.comp,
│   │   │                 VolumeBoundsReduce.comp)
│   │   ├── Surfel/                                 // 🟢 (live) surfel GI prepass — Phase-1 pool/grid/spawn/debug + Phase-2 trace+MSME integrate.
│   │   │   │                                      //    NO GI on screen yet (Phase 3 gathers at SurfaceShade.frag:547, still untouched)
│   │   │   ├── SurfelPool.{h,cpp}                  // surfel SSBO (stride-32: vec4 posb; vec3 normal; int age) + alive/pool atomics + free-list
│   │   │   │                                      //    + the four F7 zero-fill buffers (touched/guiding/surfelDepth/moments)
│   │   │   ├── SurfelPrefixSum.{h,cpp}             // segmented inclusive scan over grid cells (net-new — no scan existed in the repo).
│   │   │   │                                      //    Serial single-thread cross-segment variant (F4). 🔴 collect dispatch groups by 64
│   │   │   ├── SurfelGridSlotting.{h,cpp}          // clear->count->scan->slot; owns Offsets + List SSBOs (split, per F6) and an internal
│   │   │   │                                      //    SurfelPrefixSum. 🔴 grid coord signed ivec3 so the cascade `>>` is arithmetic (F1)
│   │   │   ├── SurfelLifecycleSubmission.{h,cpp}   // Prepare (F21 seed ages to RECYCLED, one-time) / Spawn (reconstructs world pos+normal
│   │   │   │                                      //    from the VISIBILITY id, mirroring SurfaceShade — no G-buffer here) / Age (+1, TTL recycle)
│   │   │   ├── SurfelDebugInscription.{h,cpp}      // splats each live surfel as a screen-space disc into the radiance scope; colour by mode
│   │   │   │                                      //    (Age/Cascade/Identity/Occupancy). Off by default; F6 cycles it. Visual half of the DoD
│   │   │   ├── SurfelIntegrateSubmission.{h,cpp}   // 🟢 (Phase 2) per-surfel INTEGRATE: SLG-guided trace (TraceTwoLevel inline) + hit shade
│   │   │   │                                      //    + MSME fold + radial-depth write. Two sets (BVH + surfel), direct ceil(Cap/64), swap last
│   │   │   ├── SurfelRadialDepth.{h,cpp}           // host occlusion constants (SurfelOcclusionParams 1.2/0.2/0.25/0.15, tile base index) + a CPU
│   │   │   │                                      //    mirror of compute_surfel_depth_weight for the I4 gate oracle. NO dispatch of its own
│   │   │   ├── SurfelCensusTrace.{h,cpp}           // per-frame spawn/kill/alive census -> CSV, on its own key (L is taken by the probe dump).
│   │   │   │                                      //    Diagnostic only (spawned / diedTtl / diedPolice / keepAlive)
│   │   │   └── Shaders/ (SurfelGrid.glsl [F1 helper + hashing], SurfelRecord.glsl, SurfelVisibilityReconstruct.glsl,
│   │   │                 SurfelMoments.glsl, SurfelGuiding.glsl, SurfelRadialDepth.glsl, SurfelGather.glsl [Phase 2 modules],
│   │   │                 SurfelPrepare.comp, SurfelGridClear/Count/Slot.comp, SurfelPrefixScanSegment/CollectSegment/
│   │   │                 SegmentReduce/Merge.comp, SurfelSpawnRequest.comp, SurfelAllocate.comp, SurfelAge.comp,
│   │   │                 SurfelIntegrate.comp, SurfelDebugSplat.{vert,frag})
│   │   ├── Grid/                                  // 🟢 (live) analytic ground grid — display-referred overlay, drawn after the
│   │   │   │                                      //    radiance resolve; samples scene depth for occlusion (no depth attachment)
│   │   │   ├── GroundGridPass.{h,cpp}  Shaders/ (AnalyticGroundPlane.{vert,frag})
│   │   ├── Shadow/                                // 🟢 (live) virtual sun shadows — replaces the retired SDF bake below
│   │   │   ├── SunShadowClipmap.{h,cpp}           // 2D LIGHT-space tile window, one level per LOD. Scrolls by whole tiles (≤2
│   │   │   │                                      //    exposed strips); a sun that ROTATES invalidates the whole window instead
│   │   │   ├── ShadowPageAtlas.{h,cpp}            // 4096² R32_UINT page pool (256 pages of 256²) + free/cached/used bookkeeping.
│   │   │   │                                      //    24:1 oversubscribed, so eviction is the steady state — coarsest LOD first
│   │   │   ├── ShadowTileStore.{h,cpp}            // the VIRTUAL table above the atlas: one 32-bit word per tile (6144 = 24 KiB).
│   │   │   │                                      //    Bits 0..7 marking-only (atomicOr), 8..31 the allocator's page index. Demand
│   │   │   │                                      //    resets per image; `Update` (staleness) deliberately survives the boundary.
│   │   │   │                                      //    Upload buffer + a 3-slot readback RING (≥ FramesInFlight) — sharing one
│   │   │   │                                      //    staging buffer raced the upload memcpy against the in-flight download copy
│   │   │   ├── ShadowTileMarkingSubmission.{h,cpp} // 🟢 the GPU side: 3 pipelines (S1/S2/S3), the ONE shared 5-binding set they all
│   │   │   │                                      //    agree on, and the binding-3 per-level origin UBO. 🔴 origins refresh AFTER
│   │   │   │                                      //    the clipmap scroll, before any dispatch; S3 dispatches once per level with
│   │   │   │                                      //    DISPATCH_BASE + gl_GlobalInvocationID.z. S2 is optional (needs frag atomics)
│   │   │   └── Shaders/
│   │   │       ├── ShadowTileStore.glsl           // #include body (NOT a compile target — ShaderPlan globs only vert/frag/comp).
│   │   │       │                                  //    Mirrors the bit layout; the .cpp validator asserts it as text, incl. the
│   │   │       │                                  //    SIGN of the toroidal-origin add, which no compiler can check
│   │   │       ├── MarkVisibleShadowPages.comp    \ S1 receiver marking, 8x8 per screen pixel. Reads the ID BUFFER (not depth) for
│   │   │       │                                  //    coverage, unprojects, picks a LOD by distance, raises Used|Direct
│   │   │       ├── ShadowTileTagInscription.{vert,frag}  \ S2 caster staleness, raster into a 32x32 tilemap. Raises Update ONLY.
│   │   │       │                                  //    Sphere proxy (the only bounds that exist), z-pancaked, 1-texel conservative
│   │   │       │                                  //    expansion emulated in the vert stage
│   │   │       └── ShadowTileLevelPropagate.comp  \ S3 fine->coarse demand, local 32x32 = 1024 (Pascal's exact invocation floor).
│   │   │                                          //    Raises Used|Coarse, never Direct. 🔴 ONE DISPATCH PER LEVEL, ascending, with a
│   │   │                                          //    barrier between — no in-shader ordering is possible across workgroups
│   │   ├── DistanceField/                         // ← retired (global SDF sun-shadow bake)
│   │   │   └── GlobalDistanceField, SignedDistanceVolume, DistanceVolumeBaker, BrickResidencyPool,
│   │   │      AsyncTransferRing, BakeOutputTexture, DistanceSlicePreview
│   │   └── Terrain/                               // ← retired (SDF terrain field)
│   │       └── TerrainFieldProgram, TerrainFieldVolume, TerrainFieldPreview
│   │
│   ├── Authoring/                                 // ══ pillar 4 — creation toolkit ══
│   │   ├── Modeling/                              // ← retired (half-edge editable polygons)
│   │   │   ├── ModelingExtension.{h,cpp}         // 🟢 modelling session loop
│   │   │   ├── Geometry/
│   │   │   │   ├── PolygonComplex, PolygonDescriptor, PolygonCluster, VertexField, FaceTriangulation
│   │   │   │   ├── Topology/ PolygonTopologyEdit    Extrude/ PolygonExtrude    Inset/ PolygonInset
│   │   │   │   ├── Bevel/ PolygonBevel    LoopCut/ PolygonLoopCut    Deform/ PolygonDeform
│   │   │   │   ├── Modifier/ ModifierStack, SubdivideCatmullClark, SubdivideSimple
│   │   │   │   ├── Primitives/ PrimitiveShape    Display/ DisplayPolygonAssembly, TriangleOrigin
│   │   │   │   ├── Fixtures/ CalibrationSpecimen
│   │   │   │   └── UV/  ComponentUVSurface        // per-mesh UV DATA (edited by UVEditing)
│   │   │   ├── Adjacency/ AdjacencyIndex    Selection/ ComponentSelection, SelectionMode
│   │   │   └── Picking/ RayPickIntersection
│   │   │
│   │   ├── UVEditing/                             // first-class UV editor (peer to Modeling)
│   │   │   ├── UVEditingExtension.{h,cpp}        // 🟢 UV session lifecycle
│   │   │   ├── SeamSpecification.{h,cpp}         // 🧩 seam / island descriptor
│   │   │   ├── LscmSolver.{h,cpp}                // 🧩 LSCM parameterization
│   │   │   ├── SlimSolver.{h,cpp}                // 🧩 SLIM relaxation
│   │   │   ├── HarmonicSolver.{h,cpp}            // 🧩 harmonic parameterization
│   │   │   ├── IslandPacking.{h,cpp}            // 🧩 atlas packing
│   │   │   └── UVPinConstraint.{h,cpp}          // 🧩 pinned-vertex constraint during unwrap
│   │   │
│   │   ├── ParametricSketch/                      // was Cad/ — exact B-rep NURBS kernel (PLAN-CadWorkspace.md)
│   │   │   ├── ParametricSketchExtension.{h,cpp}     // 🟢 (was CadExtension)
│   │   │   ├── ParametricSketchConfiguration.{h,cpp} // 🧩 tolerances, continuity, tessellation error
│   │   │   ├── Geometry/     ParametricCurve, NurbsCurve, NurbsSurface, BsplineBasis, KnotSequence,
│   │   │   │                 ConicSection, AnalyticSurface, CurveInterpolation, SurfaceInterpolation
│   │   │   ├── Boundary/     SolidBody, SolidEnvelope, SolidFace, FaceLoop, CoEdge, SolidEdge,
│   │   │   │                 SolidVertex, BoundaryConstructor, BoundaryValidate
│   │   │   ├── Sketch/       SketchPlane, SketchPrimitive, SketchConstraint, ConstraintSolver,
│   │   │   │                 ConstraintDecomposition, SketchProfile
│   │   │   ├── Operations/   Extrude, Revolve, Sweep, Loft, Fillet, Chamfer, BlendSurface,
│   │   │   │                 HollowOperation, OffsetOperation, SetbackCorner
│   │   │   ├── Boolean/      BooleanOperation, SurfaceIntersection, IntersectionLoop, FacePartition,
│   │   │   │                 RegionClassification, BoundaryStitch, ToleranceModel
│   │   │   ├── Tessellation/ SurfaceTessellation, TrimTessellation, EdgeDiscretization, DisplayPolygonAssembly
│   │   │   └── Draughting/   DraughtShapeStore, DraughtConstraintSolver, DraughtBoolean,
│   │   │                     DraughtFillet, DraughtLoft, DraughtTransform   // ← retired
│   │   │
│   │   ├── TexturePainting/                       // was Painting/ — 3D texture painting
│   │   │   ├── TexturePaintingExtension.{h,cpp}      // 🟢 (was PaintingExtension)
│   │   │   ├── TexturePaintingConfiguration.{h,cpp}  // 🧩 (was PaintingConfiguration)
│   │   │   ├── GpuPaintDispatch, PaintStoreAddress.h   // ← retired
│   │   │   ├── BrushSpecification, BrushProfile, PaintStroke, TexturePaintSurface,
│   │   │   │   ProjectionPaint, PaintChannel
│   │   │   └── PaintTools/ FillTool, SmearTool, CloneTool, MaskTool
│   │   │
│   │   ├── TextureBaking/                         // was Baking/ — normal/AO/curvature bake
│   │   │   ├── TextureBakingExtension.{h,cpp}    // 🟢 NEW coordinator
│   │   │   ├── GpuBakeDispatch, NormalMapBaker, SurfaceSampleField, TriangleRayVolume
│   │   │   └── Shaders/ Surface{Normal,Position,Occlusion,Curvature,Bevel,Dust,AmbientOcclusion}Encode.comp,
│   │   │              DistanceVolume{Seed,Flood,Narrow,Resolve,BandExact,BakeExact}.comp
│   │   │
│   │   ├── Interchange/                           // ← retired (import readers)
│   │   │   └── ModelInterchange, InterchangeReaders.h, InterchangeWavefrontReader (obj),
│   │   │      InterchangeGltfReader (glTF), InterchangeFilmboxReader (fbx)
│   │   │
│   │   └── Gizmos/                                // manipulation handles (was Interaction/Gizmos)
│   │       ├── GizmoExtension.{h,cpp}            // 🟢 gizmo lifecycle + hit-routing
│   │       ├── GizmoActiveAxis.{h,cpp}           // 🧩 active axis/plane, drag delta, snap ("state" banned)
│   │       ├── Gizmo3D/  TranslateGizmo3D, RotateGizmo3D, ScaleGizmo3D, TransformGizmoFrame
│   │       └── Gizmo2D/  TranslateGizmo2D, RotateGizmo2D, ScaleGizmo2D, PlanarGizmoFrame
│   │
│   │   └── ParametricAuthoring/                   // ported analytic-sketch CPU model → AuthoringParametric.lib
│   │       ├── ParametricSketchShapeStore.{h,cpp} // 🧩 analytic shapes + factory/flatten + the GPU bridges
│   │       │                                      //   (SceneView / stroke+shape bodies / solid+stroke images)
│   │       ├── ParametricSketchConstraintSolver.{h,cpp}  // 🧩 relaxation constraint solve
│   │       ├── Workplane.{h,cpp}                  // 🧩 standalone construction-plane model: parametric frame solve (Z-up), per-panel store,
│   │       │                                      //   Append/Detach + undo/redo history, sheet+grid render assembly (SketchModelViewport draws it)
│   │       ├── Operations/ Boolean, Fillet, Loft, Transform  // (Boolean/Transform → Clipper2, Loft → earcut)
│   │       └── Build.bat                          // → AuthoringParametric.lib (folds in Clipper2's 3 vendored .cpp)
│   │
│   ├── Workspaces/                                // ══ pillar 5 — editable workspace documents ══
│   │   │                                          //   A WorkspaceDocument = one tab = one editable authoring
│   │   │                                          //   session (Modeling/UV/Sketch/Paint/Bake). Each owns a
│   │   │                                          //   Revision track. Simulation is NOT a document.
│   │   ├── WorkspaceDocumentExtension.{h,cpp}     // 🟢 open / activate / close documents; owns the tab set
│   │   ├── WorkspaceDocument.{h,cpp}              // 🧩 one document: data + its Revision track + dirty mark
│   │   ├── WorkspaceRegistry.{h,cpp}              // 🧩 all open documents (dense store)
│   │   ├── WorkspaceToken.h                       // 🧩 stale-detecting identity (MicroUtils TokenIssuer + TokenAuthentication)
│   │   ├── WorkspaceClassification.h              // 🧩 which authoring category (Modeling/UV/Sketch/Paint/Bake)
│   │   ├── AuthoringWorkspace.{h,cpp}             // 🧩 links a document to its authoring content (PolygonComplex, sketch, paint target…)
│   │   │
│   │   ├── Revision/                              // ← graveyard RevisionDivision — PER-DOCUMENT history
│   │   │   │                                      //   Undo/redo/non-linear versioning + forking, one track
│   │   │   │                                      //   per WorkspaceDocument. P0 — headers + vertical slice
│   │   │   │                                      //   (ContentArena + MutationIntake + CursorResolution).
│   │   │   │                                      //   Replaces the legacy linear EditLog + LogCursor.
│   │   │   ├── RevisionExtension.{h,cpp}          // 🟢 (was RevisionDivision)
│   │   │   ├── Build.bat  README.md
│   │   │   ├── Registry/       RevisionEntry.h, RevisionRegistry.{h,cpp}, ContentArena.{h,cpp},
│   │   │   │                   TrackFootprint.h    // VacancyTable/TokenIssuer ← MicroUtils
│   │   │   ├── Connectivity/   SequenceAdjacency.{h,cpp}, ForkTranslation.{h,cpp}
│   │   │   ├── Evaluation/     CursorResolution.{h,cpp}, InvertibleContent.{h,cpp}, SequenceLinearizer.{h,cpp}
│   │   │   ├── Lifecycle/      MutationIntake.{h,cpp}, GarbageReclamation.{h,cpp}
│   │   │   └── SignalRouting/  SignalBroadcaster.{h,cpp}, RevisionDeltaDecoder.{h,cpp}  // "state" banned
│   │   │
│   │   └── Codex/                                 // ← retired (UOF unified binary project format)
│   │       ├── CodexDocument.{h,cpp}              // 🧩 serialize a WorkspaceDocument + its Revision track → UOF
│   │       └── CodexArchive.{h,cpp}              // 🧩 read/write the on-disk archive
│   │
│   └── Interface/                                 // ══ pillar 6 — purely the UI ══
│       ├── InterfaceExtension.{h,cpp}             // 🟢 UI lifecycle
│       ├── README.md   Build.bat
│       ├── Theme/          ThemeConfiguration, ThemeResolver, ColorPaletteDescriptor
│       ├── Instrumentation/                       // (live) on-screen metrics/telemetry HUD
│       │   ├── InstrumentationExtension.{h,cpp}  InstrumentationValidation.{h,cpp}
│       │   ├── Classification/ InstrumentRecordClassification.h, PresentationSpecification.h
│       │   ├── Registry/       InstrumentRecordEntry.h, InstrumentRecordStore, InstrumentVacancyTable, InstrumentToken.h
│       │   ├── SignalAccumulation/ MetricSignalAccumulator, SignalEvaluationRange.h, SignalIngestionProfile.h
│       │   ├── SpatialAlignment/   DormantInstrumentLinearizer, EnclosureAlignmentProfile
│       │   └── InstrumentProjection/ AllocationBarPass, BudgetPercentPass, FrameGraphPass,
│       │                             InstrumentEnclosurePass, LiveReadoutPass, RenderReportPass, InstrumentBodyContext.h
│       ├── Components/
│       │   ├── PropertyPanelBase
│       │   ├── Cards/    ActionToolbar, SectionHeader, ContentSection, ContentCarousel
│       │   ├── Controls/ ValueSlider, VectorEntry, ScalarEntry, BooleanEntry, ColorEntry,
│       │   │             SelectionEntry, PathEntry, Dropdown, ControlLayout, MenuPill
│       │   └── Menus/    GlyphInscription (unit-square vector glyphs, ImDrawList — backend-agnostic),
│       │       │         TopologyActionMenu (selection-driven context menu card, one flat list),
│       │       │         ModellingToolMenu  (two-pane band rail + tool grid, same catalogue split by family)
│       │       └── PolygonMutation/   // polygon-specific payload: what the menu offers and when
│       │           ├── SelectionPredicate      // gate: stratum + count + connectivity -> AvailabilityCondition
│       │           ├── OperationCatalogue      // the 94-operation table across 11 bands
│       │           ├── PolygonGlyphIdentity    // glyph names only (no ImGui — keeps the catalogue pure)
│       │           ├── PolygonGlyphTable       // the stroke paths that draw them
│       │           └── StratumBadgeTable       // the 7 two-tone header badges, one per TopologyStratum
│       ├── Workspaces/                             // (live) per-workspace UI ported from Documentation/Prototypes — UI-ONLY, no GPU
│       │   └── TexturePaint/  LayerStackPanel   // the paint-layer stack rail (ordinal 0 = TOP): identity tag · category glyph ·
│       │                      // name/meta · opacity scrub · eye · blend caret, + add catalogue (4 categories), reorder drag,
│       │                      // inline rename, delete, 12-layer cap + tally footer. Ported from PaintingSurface/Interface/
│       │                      // LayerInspector.js + Layers/LayerStack.js. Token-addressed, NEVER by ordinal. Palette-free:
│       │                      // draws entirely from the passed ThemeConfiguration. NOT the pillar-5 Workspaces/ document tree.
│       ├── Icons/        SvgRasterizer (thorvg), SvgIconRegistry (Vulkan textures),   // keyed SVG glyph store, content-hash deduped
│       │                 IconPackGlobal (g-), IconPackCad (cad-), IconPackScene (scene-), IconPackPaint (paint-)
│       ├── Dialogues/      ValidationDialogue, ConfirmDialogue   // ← retired
│       ├── Settings/       RootPanel, SettingsPanel, GeneralPanel, AppearancePanel, AppearancePersistence,
│       │                   LibraryPanel, NotificationPanel, PerformancePanel   // ← retired
│       ├── RevisionPanel/  RevisionPanel, RevisionPanelConfiguration   // consumes Workspaces/Revision delta bus
│       ├── WorkspaceHost/                         // (live) app chrome + docking (replaces "Shell")
│       │   ├── WorkspaceApplicationRunner, WorkspaceDockHost, WorkspacePanelDock, WorkspaceTabStrip
│       │   ├── PanelRegistry, DeploymentBracket, ImguiPlatformBridge
│       │   ├── Viewport/  ViewportPanel, ViewportCamera, ViewportGrid
│       │   ├── SketchOutliner/  SketchOutlinerPanel (the ONE shared outliner panel, ns Frontier::SketchOutlinerUi),
│       │   │                    OutlinerContentProfile.h, SketchContentProfile, SceneContentProfile
│       │   │                    // content-agnostic frame + per-app profile (icons/labels/sample/filters/fallback art).
│       │   │                    // Drives the CAD outliner (WorkspacePanelDock, SketchOutliner.exe, the SDI directory rail)
│       │   │                    // AND the scene outliner (SceneDirectory.exe) — "more outliners" = another profile, not a panel.
│       │   └── Outliner/  OutlinerPanel, OutlinerConfiguration, Model/OutlinerModel, Model/OutlinerRow  // ← thin, DEAD (registered, never invoked)
│       └── WorkspaceLayouts/                      // one UI layout per editor mode (was Workspaces/)
│           ├── Modeling/          ModelingWorkspace, ModelingPropertyPanel
│           ├── UV/                UVWorkspace, UVPropertyPanel
│           ├── ParametricSketch/  ParametricSketchWorkspace, SketchPropertyPanel
│           ├── TexturePaint/      TexturePaintWorkspace, PaintPropertyPanel
│           ├── TextureBake/       TextureBakeWorkspace, BakePropertyPanel
│           └── Simulation/        SimulationWorkspace, SimulationPropertyPanel
│
├── Executables/                                   // was "Applications/" ("Application" banned) — one folder per build target
│   │                                              //   Every tool is its OWN Editor executable that boots its workspace
│   ├── ModelingEditor/         { ModelingEditorEntry.cpp,          Config/, Build.bat, README.md }
│   ├── UVEditor/               { UVEditorEntry.cpp,                Config/, Build.bat }
│   ├── ParametricSketchEditor/ { ParametricSketchEditorEntry.cpp,  Config/, Build.bat }
│   ├── TexturePaintEditor/     { TexturePaintEditorEntry.cpp,      Config/, Build.bat }
│   ├── TextureBakeEditor/      { TextureBakeEditorEntry.cpp,       Config/, Build.bat }
│   ├── SimulationEditor/       { SimulationEditorEntry.cpp,        Config/, Build.bat }
│   └── Validation/                                // validation targets (not editors)
│       ├── ClipmapFieldValidation/         { ClipmapFieldValidation.cpp,             Build.bat }
│       ├── ConstructionCatalogueValidation/{ ConstructionCatalogueValidationHost.cpp, ConstructionCatalogue.{h,cpp}, ConstructionCataloguePanel.{h,cpp}, ConstructionConsoleBridge.{h,cpp}, ConstructionGlyphs.{h,cpp}, Build.bat }  // non-host units ALSO compiled in-place by SketchModelViewport (the embedded right-click console)
│       ├── ControlsGallery/                { ControlsGalleryEntry.cpp,               …Panel.{h,cpp}, Build.bat }
│       ├── GeometryArenaValidation/        { GeometryArenaValidationEntry.cpp,       Build.bat }  // headless device: uploads the BLAS
│       │                                                                                          //   blobs and reads them back byte for
│       │                                                                                          //   byte; also judges Model*InverseModel
│       │                                                                                          //   and drives a real .wsdoc through the
│       │                                                                                          //   load path into the arena (assertion 7)
│       ├── GeometryTreeValidation/         { GeometryTreeValidationEntry.cpp,        Build.bat }  // CPU-only: the SAH builder itself
│       ├── InstrumentationValidation/      { InstrumentationValidationEntry.cpp,     Build.bat }
│       ├── ModellingToolValidation/        { ModellingToolValidationHost.cpp,        …Panel.{h,cpp}, Build.bat }
│       ├── OrientationCube/                { OrientationCubeEntry.cpp,               Build.bat }
│       ├── PaintToolValidation/            { PaintToolValidationHost.cpp,            PaintToolPanel + Paint* card/catalogue/column units, Build.bat }
│       ├── PolygonActionValidation/        { PolygonActionValidationHost.cpp,        …Panel.{h,cpp}, Build.bat }
│       ├── RadixSortValidation/            { RadixSortValidationEntry.cpp,           Build.bat }
│       ├── RenderExtensionValidation/      { RenderExtensionValidationEntry.cpp,     Build.bat }
│       ├── RigidBodyDropValidation/        { RigidBodyDropValidationEntry.cpp, RigidBodySceneAuthor.{h,cpp}, RigidBodySimulation.{h,cpp}, RigidBodyCullRefit.{h,cpp}, RigidBodyFrameDriver.{h,cpp}, RigidBodyControlWindow.{h,cpp}, Build.bat }  // Jolt physics over RenderExtension: authors its own drop scene as .wsdoc, drives 65 dynamic bodies, rewrites the instance transforms each frame. Links ExternalPackages\jolt\lib\jolt.lib + /DNDEBUG (the archive was built with features 0, so a consumer without NDEBUG fails to link JPH::AssertFailed). ⚠️ Substitutes its scene by AUTHORING OVER the engine's own document filenames (SuzanneRadial.wsdoc + CheckerFloor.wsdoc) — RenderExtension.cpp is in Graphics.lib so FRONTIER_SCENE_ASSET_DIR cannot be redefined here. Runs from a PRIVATE working dir (Binaries\Validation\RigidBodyDropScene, Shaders junctioned in) via RunRigidBodyDropValidation.bat so it cannot clobber the shared Assets the other apps load. Zero engine edits: it wraps the public WindowSubstrate::RecordSequence rather than adding a host seam
│       ├── SceneDirectory/                 { SceneDirectoryHost.cpp, Build.bat }  // host-only; drives the SHARED SketchOutliner panel with the SCENE profile (no private panel copy)
│       ├── SceneDirectoryInspectorValidation/ { SceneDirectoryInspectorValidationHost.cpp, SceneDirectoryInspector.{h,cpp}, SceneDirectoryInspectorPanel.{h,cpp}, InspectorContentProfile.{h,cpp}, InspectorGlyphs.{h,cpp}, Build.bat }  // directory rail = SHARED SketchOutliner panel via InspectorContentProfile (opaque ClassificationId == RecordClassification); non-host units ALSO compiled in-place by SketchModelViewport
│       ├── SketchModelViewport/            { SketchModelViewportHost.cpp,            …Panel.{h,cpp}, SketchModelOffscreenSurface.{h,cpp}, SketchModelViewportChrome.{h,cpp}, SketchModelSummonedSurfaces.{h,cpp}, SketchModelWorkplaneOverlay.{h,cpp}, Build.bat }  // embeds two surfaces over the canvas: SceneDirectoryInspector card on Tab + ConstructionCatalogue console on right-click — both sibling folders' non-host .cpp compiled IN-PLACE (their Host.cpp skipped); links AuthoringParametric.lib + draws authored Workplane records as an ImGui DrawList overlay projected by the viewport camera
│       ├── SketchOutliner/                 { SketchOutlinerValidationHost.cpp, Build.bat }  // host-only; drives the SHARED SketchOutliner panel (SKETCH profile) from EngineContext.lib
│       ├── TexturePaintValidation/         { TexturePaintValidationHost.cpp, TexturePaintSummonedCard.{h,cpp}, Build.bat }  // the texture-paint surface: one flat paint field with the PaintToolValidation instrument card summoned over it on RIGHT-CLICK (that sibling's non-host .cpp compiled IN-PLACE, its Host.cpp skipped — the card is deliberately outside EngineContext.lib because its strip store needs a rectangular raster); links thorvg.lib + /DTVG_STATIC for the embedded PaintIconStore
│       ├── TexturePaintWorkspaceValidation/ { TexturePaintWorkspaceValidationHost.cpp, TexturePaintWorkspacePanel.{h,cpp}, Build.bat }  // the COMBINED workspace: left layer rail + centre paint field with the summoned instrument card + right property column, composed from the sibling panels 1:1 — LayerStackValidation + ChannelPropertyValidation (their non-host .cpp compiled IN-PLACE), TexturePaintValidation's summon, and the SHARED EngineContext LayerProperties mask card; frame order is load-bearing (field + card at WINDOW scope, then the two scroll children so nothing clips the card's self-pushed clip); UI shell only — no stroke/atlas passes yet
│       ├── TriangleCellOverlapValidation/  { TriangleCellOverlapValidationEntry.cpp, Build.bat }
│       ├── VolumeBoundsValidation/         { VolumeBoundsValidationEntry.cpp,        Build.bat }
│       ├── WorkspaceDock/                  { WorkspaceDockEntry.cpp,                 Build.bat }
│       └── WorkspaceDocumentWriter/        { WorkspaceDocumentSerializer.cpp,        Build.bat }
│
├── Binaries/                                       // outputs mirror Executables/ 1:1 (git-ignored)
│   ├── ModelingEditor/         { ModelingEditor.exe,         Config/, EngineContent/ }
│   ├── UVEditor/               { UVEditor.exe,               Config/, EngineContent/ }
│   ├── ParametricSketchEditor/ { ParametricSketchEditor.exe, Config/, EngineContent/ }
│   ├── TexturePaintEditor/     { TexturePaintEditor.exe,     Config/, EngineContent/ }
│   ├── TextureBakeEditor/      { TextureBakeEditor.exe,      Config/, EngineContent/ }
│   ├── SimulationEditor/       { SimulationEditor.exe,       Config/, EngineContent/ }
│   └── Validation/             { ControlsGallery.exe, OrientationCube.exe, SceneDirectory.exe,
│                                 WorkspaceDock.exe, InstrumentationValidation.exe, RenderExtensionValidation.exe,
│                                 PolygonActionValidation.exe, ModellingToolValidation.exe, PaintToolValidation.exe,
│                                 SketchOutliner.exe, SketchModelViewport.exe, ClipmapFieldValidation.exe,
│                                 RadixSortValidation.exe, TriangleCellOverlapValidation.exe, VolumeBoundsValidation.exe,
│                                 GeometryTreeValidation.exe, GeometryArenaValidation.exe,
│                                 WorkspaceDocumentWriter.exe, ConstructionCatalogueValidation.exe,
│                                 SceneDirectoryInspectorValidation.exe,
│                                 TexturePaintValidation.exe, RigidBodyDropValidation.exe,
│                                 TexturePaintWorkspaceValidation.exe,
│                                 Assets/,                     // the SHARED .wsdoc the render apps load — do NOT author over these
│                                 RigidBodyDropScene/          // RigidBodyDropValidation's PRIVATE cwd: its own Assets/ + a Shaders junction
│                                                              // (launch it through RunRigidBodyDropValidation.bat, which cd's here first)
│                                 RunRigidBodyDropValidation.bat }
│
├── Documentation/                                 // per "Where to Save Documents"
│   ├── Explainers/  Mockups/  Research/  Skills/  Assets/
│   └── Prototypes/                                // single-file .html prototypes, plus:
│       └── RockFormation/                         // node-based 3D rock/erosion editor (WebGPU, voxel)
│           │                                      //   RockFormationConstructionTree.html  (chrome + boot)
│           │                                      //   RockFormationEntry.js               (device, loop)
│           │                                      //   RockFormationNotes.md  (working notes:
│           │                                      //     confirmed species, deferred work, hazards)
│           ├── Construction/                      // the tree: species, state, WGSL transcription
│           │                                      //   GeologyReference.js  PortCategories.js
│           │                                      //   ConstructionSpecifications.js  TreeState.js
│           │                                      //   ConstructionTranscriber.js  SeedTree.js
│           ├── Simulation/                        // the erosion sim: voxel grid + compute WGSL
│           │                                      //   VoxelField.js    (ping-pong storage, bindings)
│           │                                      //   FieldSeed.js     (analytic SDF -> density)
│           │                                      //   ErosionCompute.js(thermal/hydraulic/aeolian/
│           │                                      //                     spheroidal/saltfreeze, gather-only)
│           │                                      //   GridMarch.js     (fixed-step density march)
│           │                                      //   ErosionTimeline.js (play/pause/scrub; GPU snapshot
│           │                                      //                     ring — erosion has no inverse,
│           │                                      //                     so back-scrub restores + re-steps)
│           ├── Resolve/                           // WGSL library + device host + card-preview march
│           │                                      //   DistanceExpression.js  DeviceHost.js
│           │                                      //   HullExpression.js (Blender rock-generator hull:
│           │                                      //                     plane intersection w/ per-plane
│           │                                      //                     crease + 4-stage displacement)
│           │                                      //   SphereTrace.js  (analytic; CARD PREVIEWS only —
│           │                                      //                    the viewport marches the grid)
│           └── Surface/                           // authoring interface (DOM cards, SVG links)
│                                                  //   TreeSurface.js  Catalogue.js  PanelDials.js
│
├── EngineDocs/                                     // plans + running docs
│   ├── FolderStructure.md   (+ PARSE:FOLDER-INDEX block)   Backlog.md   ImportantNotes.md   Phase.md
│   ├── PLAN-CadWorkspace.md
│   └── AgenticInstuctions/  SKILL-Naming.md, SKILL-formatting.md, SKILL-CompactOutputProtocol.md
│
└── Archive/                                        // single graveyard (dead code / retired trees)
    └── (RetiredProject + Attic-EngineReset fold here after adoption)
```

---

## Pillars at a glance

| # | Pillar | Owns |
|---|---|---|
| 1 | **EngineContext/** | EngineHost · Configuration · Math · MetricSpace · SpatialAcceleration · MicroUtils · Motion · Input · Audio · Scene · Navigation |
| 1b | **Platform/** | Windowing · Concurrency · Storage · InputProvider |
| 2 | **Simulation/** | Physics · Cloth · Gameplay |
| 3 | **Graphics/** | Render · Visibility · Atmosphere · Grid · Shadow · Acceleration · Surfel · DistanceField · Terrain |
| 4 | **Authoring/** | Modeling · UVEditing · ParametricSketch · TexturePainting · TextureBaking · Interchange · Gizmos |
| 5 | **Workspaces/** | WorkspaceDocument · Revision · Codex |
| 6 | **Interface/** | Theme · Instrumentation · Components · Dialogues · Settings · RevisionPanel · WorkspaceHost · WorkspaceLayouts |

## Editor targets (6) + Validation (7)

`ModelingEditor · UVEditor · ParametricSketchEditor · TexturePaintEditor · TextureBakeEditor · SimulationEditor` — each `…Entry.cpp`, mirrored 1:1 in Binaries. SimulationEditor is the only document-less one.

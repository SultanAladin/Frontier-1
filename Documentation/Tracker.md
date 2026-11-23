# Project Tracker

Single progress and violation tracker. Detailed evidence belongs in Exhibits, not competing progress reports.
The [renderer and GI evidence map](../Exhibits/RecoveryAudit/RendererStatus.md) separates software tracing,
hardware RT, non-ray paths, reflections, PBR and the statistical limits of the recent charts.

## Environment records, editor descriptions and Sun/sky/cloud/fog scene — 2026-10-28

- Implemented plan steps 1–4 for the environment slice: editor-profile-only `RecordEditorDescription`,
  built by each record's own constructor and registered through `RecordEntry`; `SunRecord`,
  `AtmosphereRecord`, `CloudRecord`, `FogRecord` and `WindRecord` bind inspector fields to live Engine
  settings with pinned HTML (`6afb6f0`) ranges. The Outliner model now projects HTML group sections,
  Enabled/Disabled tiles and a per-selection inspector model; an Inspector panel using the pinned CSS
  metrics is registered in `FrontierMain` beside Outliner and Log Stream, owning those records.
- `EnvironmentSceneValidation` passes 13/13 release and ASan/UBSan: groups, counts, inspector
  construction, live field edit, search, and rendered-frame changes for cloud/fog/Sun. Retained frame
  (Sun, sky, clouds, ground cloud shadow, height fog; no analytic lights) is a host CPU render from
  Engine models, **not** the editor viewport. SceneDirectory passes Development 45 / Editor 45 /
  Shipping 17; LogStream 2/2; the `FrontierMain` SDK path syntax-compiles. No ImGui window, Vulkan
  viewport, physical GPU or Windows run in this sandbox; vehicle/Jolt, gizmos and ReSTIR controls
  remain deferred. [Evidence and boundaries](../Exhibits/EnvironmentScene/README.md).

## Subpixel seed retracing combined with reservoir reuse — 2026-10-28

- The default-off seed-bank mode now combines with temporal and/or spatial reservoir reuse: gathered
  unselected seeds enter generation as one-sample reservoir candidates (duplicates of the temporal seed
  are skipped), resolve keeps its reservoir estimator, and finalize only shifts the bank with the fresh
  seed. Recorder dispatch is primary, gather, generate, resolve, finalize in this mode.
- Host release and ASan/UBSan CTests pass 5/5 and all five Slang entries compile to SPIR-V in this sandbox.
  The SwiftShader GPU check (`ValidateReplaySampleCombined`: rejected pixels bit-match reservoir-only
  output, accepted pixels shift the fresh bank) is wired into the sample-history run but was **not run**
  here — no Vulkan CPU driver is available in this sandbox. No unbiasedness, equal-time or editor claim.
  [Contract and limits](../Exhibits/LightCompute/ReplaySampleCombined.md).
- ReSTIR GI, hardware RT, non-ray-traced GI, general animated reconstruction, unified PBR and editor
  integration remain incomplete. The editor plan and vehicle dynamics/Jolt remain deferred.

## Subpixel diffuse path-seed retracing — 2026-10-28

- Added a default-off, one-candidate mode which retains up to three unselected fresh seeds and retraces them at
  the current diffuse surface. It does not accumulate old radiance and cannot be combined with reservoir reuse
  or colour reconstruction. A half-pixel SwiftShader fixture accepts 970 banks, rejects all 22 changed-token
  disocclusions, and reproduces 10 changed shadow-path answers at the current point. Scene changes reset history.
- Independent one-bounce rectangle checks, seven corrupt-bank and seven invalid-request controls, five CTests,
  release/host-sanitized rows and strict-camera regressions pass. On four-bounce half-pixel motion, `813,056`
  CPU paths and 16 GPU seed batches give red RMSE `0.1530` for four fresh paths versus `0.1517` for four current
  paths including retraced seeds. The latter also uses prior frames; equal path count is **not equal time or total
  work**. Explicit-facet sampling and source-off/relight resets pass in a separate one-bounce check.
  [Contract, uncertainty and readbacks](../Exhibits/LightCompute/ReplaySampleHistory.md).
- ReSTIR GI, hardware RT, non-ray-traced GI, general animated reconstruction, unified PBR and editor integration
  remain incomplete. The editor plan and vehicle dynamics/Jolt remain deferred.

## Dynamic camera, blocker and multibounce replay — 2026-10-28

- A simultaneous camera/blocker move now has a four-bounce, four-candidate SwiftShader CPU-Vulkan validation
  with temporal and spatial reservoir replay active. Of 88 changed surface-token pixels, all reject the old
  temporal donor; the frame still replays 904 temporal histories and 3,968 spatial links elsewhere.
- Against 835,584 brute-force CPU paths over 816 plane points, 16 batches give red means `0.116587` CPU,
  `0.116829` fresh and `0.116991` reused. The first changed and reopened colour frames reset and match raw.
  Reuse incurs extra evaluations and prior frames; no equal-time speedup, general unbiasedness or arbitrary
  dynamic colour reconstruction is claimed. [Methods, uncertainty and results](../Exhibits/LightCompute/ReplayDynamicCamera.md).

## Four-bounce diffuse camera reference — 2026-10-28

- The strict camera-only mode now also has a four-bounce SwiftShader CPU-Vulkan check against 1,187,840
  independent brute-force CPU paths at 290 exact points. Across 16 seed batches, raw/reprojected red RMSE
  against the per-point CPU means is `0.2986/0.1012`; the latter uses eight prior frames and is not equal-time.
- A paired one-bounce control exposes 35 pixels with additional indirect contributions; a simultaneously moving
  blocker and camera and a reopening both reset colour to the corresponding raw output. These are bounded
  fixture checks, not general dynamic reconstruction or a proof of unbiasedness.
  [Reference, uncertainty and results](../Exhibits/LightCompute/ReplayCameraReprojection.md#four-bounce-reference-and-repeated-camera-transitions).

## Strict diffuse camera reprojection — 2026-10-28

- Added a default-off colour-history path for certified camera-only transitions in static, untextured diffuse
  replay. It retains only exactly matching world points, normals and material guides; rough/textured modes,
  dynamic geometry and lighting changes remain reset-only. The Engine domain now also snapshots BVH bounds and
  corners, and the recorder rejects incompatible flags before recording.
- In a `32 × 32` occluded-area fixture, 290 points retained colour and 734 were fresh; red RMSE against an
  independent numerical area integral changed `0.03290 → 0.01055` after eight earlier frames. A half-pixel
  shift accepted zero, exposing the deliberately narrow coverage. Revision, exact-guide, policy and invalid
  input controls pass, with release and host-sanitized SwiftShader CPU-Vulkan readbacks.
  [Contract, caveats and diagnostic images](../Exhibits/LightCompute/ReplayCameraReprojection.md).
- This is not full ReSTIR GI, arbitrary animated/disocclusion-safe reconstruction, hardware RT, a finished
  non-ray-traced GI path, physical-GPU validation, unified PBR or an editor viewport. Editor implementation and
  vehicle dynamics/Jolt remain deferred.

## Bounded replay initial candidates — 2026-10-28

- Engine replay supports one to four fresh path candidates, with the previous single-candidate behaviour as the
  default. Candidate changes cap prior reservoir history at one and reset accumulated colour in the exhibit.
- SwiftShader CPU Vulkan and host ASan/UBSan pass an independent half-blocked area integral, 800,000 CPU
  four-bounce room reference paths, host reservoir checks, moving-blocker and rough-textured camera controls.
  Four candidates halve per-output SD in the tested fixtures but require four fresh paths: no equal-time claim.
  Twenty default-mode images match the pre-change shader bit-for-bit; 14 separate mixture-on captures match prior
  pixels. Five release CTests and reconstruction regressions pass.
  [Contract and results](../Exhibits/LightCompute/ReplayCandidates.md).
- GI is not complete: dynamic colour reconstruction, broad material/visibility cases, non-ray-traced GI,
  hardware RT, physical-GPU validation and editor linkage remain open. Vehicle dynamics and Jolt remain deferred.
  The [scene inspection/editor controls plan](Plans/Ongoing/EditorSceneInspection.md) is saved but not implemented.

## Bounded replay colour reconstruction — 2026-10-28

- Added opt-in Vulkan gather/finalize stages for the existing combined diffuse and rough-opaque replay output.
  In a static 32×32 half-blocked emitter fixture, 20-frame bounded reconstruction reduced RGB RMSE from
  `0.3193` to `0.07184` (diffuse) and `0.3740` to `0.08401` (rough opaque). Host recurrence, eleven
  malformed-guide rejections, out-of-bounds/background, reservoir composition and first-change resets pass.
- At this checkpoint camera, blocker, material and lighting changes reset colour, even while replay reservoirs
  may keep cap-one histories. The later strict diffuse camera exception is described above. This is **not**
  general animated/disocclusion-safe reconstruction;
  no physical GPU, hardware RT, editor viewport, dedicated full reflection denoiser or unified PBR is claimed.
  [Algorithm, readbacks, tests and limitations](../Exhibits/LightCompute/ReplayReconstruction.md).

## Caller-owned ReSTIR replay recording — 2026-10-28

- Moved normal primary/generate/resolve Vulkan command ordering into `Frontier/Engine/Rendering`, with a shared
  128-byte push ABI, descriptor-range/dispatch/uint-offset guards and internal compute dependencies. The exhibit
  now consumes it; diagnostic-only probe/tail scheduling and all resource creation/submission remain exhibit-owned.
- Recorder mocks, five Engine/exhibit CTests, ASan/UBSan-host checks and SwiftShader CPU-Vulkan moving-scene
  readbacks pass. Fourteen release and sanitized PNGs match the pre-change scheduler byte-for-byte; camera, rigid
  motion and blocker regressions pass. This is scheduling parity in bounded fixtures, not a physical-GPU or
  performance result. [API, verification and limitations](../Exhibits/LightCompute/PathReplayEngine.md).
- ReSTIR GI, dedicated reflection reconstruction, unified PBR and the editor viewport are **not complete**;
  no hardware-RT path or non-ray-traced GI pipeline is established. Vehicle dynamics and Jolt remain deferred.

## Matched gas-pressure prototype — 2026-10-28

- Added a separate Engine Slang face-flux pressure primitive with matching divergence, gradient and Laplacian.
  SwiftShader tests through wgpu/Naga pass analytic long-/near-grid modes, an independent 56-cell least-squares
  solve, disconnected regions, buffer guards and release/host-sanitizer checks. The long-wave case still needs
  1,024 Jacobi sweeps to reach `1.25%` remaining divergence; no performance claim is justified.
- This closed/static-solid prototype is not compatible with the demo's cell-centred transport and is not integrated
  into it. The original plume still retains `98.1%` divergence in its near-grid regression. Open/moving boundaries,
  compatible advection, scheduling, scene/editor integration and physical-GPU validation remain open.
  [Contract and evidence](../Exhibits/FluidReference/GasProjection.md).

## Standalone GPU curved-shell tyre — 2026-10-28

- Ported the demo's 160-particle, 128-patch curved-shell/membrane deformation to Engine Slang. Direct
  SwiftShader Vulkan checks compare 42,240 snapshots against unchanged JavaScript within `0.000102 m`;
  the resident particle field also feeds 24,576 surface probes. Release and ASan/UBSan-host readbacks match.
- Independent host counterexamples show that one source patch's cross-force derivatives differ by `65.9%`
  and a `0.3 rad` rigid rotation changes the source hinge curvature by `−0.0130 m`. Source parity is not a
  conservative/objective-shell or real-tyre validation. No physical GPU, graphics/editor consumer or standalone
  spring-contact solver is claimed. [Implementation and counterexamples](../Exhibits/VehiclePhysics/TyreMembraneDevice.md).

## Standalone GPU Hex8 tyre — 2026-10-28

- Ported the demo's 128-cell solid and checked cell/result footprints to Engine Slang/C++; the existing Vulkan
  recorder supplies exact counts. Reset/advance and the resident surface projection execute in bounded
  SwiftShader tests; 42,240 particle snapshots match JavaScript within `0.000102 m` and 4,096 surface probes
  match CPU projection within `5.97e-8 m`. Release and ASan/UBSan-host readback hashes match.
- A separate 64-gradient GPU constitutive check matches numerical derivatives of the Neo-Hookean energy for
  positive, unclamped Jacobians; identity and the tested rigid rotation have zero stress. This does not
  validate clamped/inverted cells, full-wheel mechanics or measured tyres. No physical GPU, graphics mesh,
  application/editor integration or spring-contact dynamics is claimed; the later membrane entry above adds
  the separate GPU shell model. Vehicle/Jolt remain deferred.
  [Contract and numerical evidence](../Exhibits/VehiclePhysics/TyreSolidDevice.md).

## GPU tyre invocation bound — 2026-10-28

- Fixed a dispatch-boundary defect: workgroups have 32 lanes, so previously a count smaller than the bound
  buffer could update unintended tyres. The Engine recorder now pushes the exact count and both projection
  entries exclude padded lanes. One requested tyre with 33 bound leaves the other 32 byte-identical in reset
  and advance; release and ASan/UBSan-host checks pass on SwiftShader. The earlier trajectory digest is unchanged.
  [Updated pipeline contract and evidence](../Exhibits/VehiclePhysics/TyreProjectionDevice.md).

## Plume footprints and reference transport — 2026-10-28

- Engine owns validated 256-byte lattice and 48-byte source footprints; four unchanged demo JavaScript grade
  records round-trip exactly. Restored the original scene/distance shader dependencies. A 15-dispatch demo
  transport step and independent source, buoyancy, vorticity, moving-SDF and light checks pass on SwiftShader
  CPU Vulkan; release and ASan/UBSan-host readback digests match.
- The chained pressure fixture is zero-divergence only. The nonzero high-frequency pressure defect remains;
  simulation shaders and scheduling remain reference/exhibit-owned, not connected to the production editor.
  No physical GPU, long-term fluid validation or liquid solver is claimed.
  [Footprints, numerical checks and limitations](../Exhibits/FluidReference/PlumeTransport.md).

## Standalone GPU tyre projection — 2026-10-28

- Ported the demo's 160-particle XPBD-labelled projection to Engine Slang, with a checked float32 ABI and a
  caller-owned Vulkan command recorder. Direct SPIR-V execution on SwiftShader CPU Vulkan matches 12 JS/CPU
  cases over 33 independent tyres × 8 steps within `9.95e-5 m` local and `9.69e-5 m` world; valid/invalid
  commands, reset, buffer and dispatch guards pass. Release and ASan/UBSan-host readbacks match exactly.
- Reusable spring/continuum surface functions now read the GPU particle layout. In one direct Vulkan command
  buffer, the XPBD advance feeds 24,576 surface probes without CPU re-upload; maximum position errors versus
  CPU are `9.70e-5 m` spring and `5.97e-8 m` continuum. A vertex-stage entry compiles but was not rendered.
- This is a sequential-per-tyre, prescribed-target model and its force is not an integrated reaction. No
  physical GPU, performance, measured-tyre, editor or mesh-renderer integration is claimed. A later entry above
  adds separate GPU Hex8 and curved-shell fixtures; spring-contact and long-duration stability remain open.
  Vehicle/Jolt work remains explicitly deferred.
  [ABI, commands and evidence](../Exhibits/VehiclePhysics/TyreProjectionDevice.md).

## Rigid GI rare-path audit — 2026-10-28

- The three largest uniform-spatial samples in the 2,048-seed set replay to large path radiance before
  reservoir scaling; their final scale factors are below one. Eight post-selected uniform/mixture cases
  match their recorded output, and an independent host reservoir reconstruction agrees exactly in this
  fixture. Release, ASan/UBSan-host and default-off regression checks pass.
- A checked mirrored breakdown places the dominant contribution in all three high paths at bounce `2`,
  hitting the same bright emissive triangle. It shares shader helpers and does not independently verify
  path values, PDF/MIS or PRNG behavior. [Mechanism and logs](../Exhibits/LightCompute/ReplayRigidTailAudit.md).

## Rigid GI tail follow-up — 2026-10-28

- A further 2,048 disjoint frame seeds compare uniform/mixture proposals with spatial reuse off/on against
  2,000,000 separately seeded CPU paths. Release and ASan/UBSan-host runs pass and their sample tables
  are identical; the low rigid-red mean does not recur. Shader/driver code remains uninstrumented.
- The old versus new uniform means differ by `3.33` nominal combined SE; the red mixture/uniform SD ratio
  reverses from `1.468` to `0.587` as rare large samples dominate the new set. These finite-sample intervals
  and measured variance are tail-sensitive, not proof of bias or of a general speedup. The original
  discrepancy remains unexplained. [Evidence and failed wrapper audit](../Exhibits/LightCompute/ReplayRigidVarianceRepeat.md).

## Disjoint moving-GI convergence repeat — 2026-10-28

- Four disjoint seed groups (256 batches) compare fresh uniform/mixture and cap-one temporal/spatial reuse
  against six independent 240,000-path CPU references. Release and ASan/UBSan host runs pass the existing
  tolerances and produce byte-identical sample tables; restricted/full primary probes match bit-for-bit
  in a three-seed audit. Shader/driver code is not sanitizer-instrumented.
- The earlier rigid-green mixture/uniform SD ratio `1.515` varied from `0.716–1.480` across groups and is
  `1.125` overall; fresh rigid red is `3.84` nominal combined SE below the CPU reference under uniform
  sampling despite passing the six-SE tolerance. The follow-up above does not reproduce the low mean,
  but its cause remains unresolved; neither a general improvement nor convergence is claimed.
  [Data, chart and limits](../Exhibits/LightCompute/ReplayFacetMixtureMotionRepeat.md).

## Moving emissive-mixture GI reference — 2026-10-28

- Independent primary-ray and four-hit CPU-path checks now exercise the optional facet mixture through camera,
  rigid-texture/material, translated emissive-triangle, source-power, blocker and reopening changes. Both
  selected primary pixels pass all 30 RGB comparisons over 96 SwiftShader CPU Vulkan batches against six
  separately seeded 240,000-path CPU references. Cap-one transitions retain temporal and spatial reuse.
  Full release and ASan/UBSan host runs pass; shaders and the external driver are not sanitizer-instrumented.
- The mixture/uniform fresh batch-SD ratio is `0.671–0.799` across floor channels, but `1.515` in object green.
  These two-probe measurements do not establish universal variance improvement or general unbiasedness.
  Physical-GPU/editor integration and general deforming-motion coverage remain open. A 14-image SwiftShader
  gallery now captures the translated sources; release/sanitized RGB hashes match, and the default capture
  helper remains bit-identical for two submissions. A 20-image prior-shader regression also remains bit-identical
  across previous uniform/power and cosine/GGX modes. These are not production or hardware renders.
  [Fixture, uncertainties and results](../Exhibits/LightCompute/ReplayFacetMixtureMotion.md) ·
  [moving-light readbacks](../Exhibits/LightCompute/ReplayFacetMixtureMotionCaptures.md).

## Exhibit ownership audit and lighting extraction — 2026-10-27

- Reviewed all 27 top-level exhibit directories against both Engine trees and production UI ownership. Existing
  CPU atmosphere/cloud/fog, celestial/optics and demo XPBD/Hex8 code is already shared; shader/editor integration,
  the demo continuum shell and full gas remain incomplete. Vehicle dynamics and Jolt work are explicitly held.
  [Ownership evidence and scope](../Exhibits/RecoveryAudit/EngineOwnershipAudit.md).
- The previously exhibit-local area/luminance radiant-facet distribution and duplicate host validation now live in
  `Frontier/Engine/Rendering` and are consumed by LightCompute. On 4,096 facets the exact former builder and Engine
  output agree byte-for-byte; 18 guard cases pass in release and ASan/UBSan. Four actual SwiftShader fog-area cases
  and their analytic/empty/invalid controls pass. This is a global CDF, not clustered emissive selection or editor
  integration. [Reproduction and limits](../Exhibits/LightCompute/RadiantFacetDistribution.md).
- Shared cloud/fog float32 payload compilers and guards now live in Engine rather than the exhibit host. Previous
  builders match byte-for-byte across 64 cloud cases and the fog fixture; 37 guards pass release and ASan/UBSan.
  A small SwiftShader sky-only scene matches homogeneous cloud/fog closed forms (maximum `6.83e-7`); it does not
  cover shadows, cloud noise, physical GPUs or editor integration. The full-size fog run was stopped, not passed.
  [Medium payload evidence](../Exhibits/LightCompute/MediumRadiancePayload.md).
- Made the existing demo XPBD/Hex8 models and spring/continuum visual vertex projection independently consumable
  without vehicle/Jolt dependencies. Release and ASan/UBSan JS-reference checks pass; 22,528 sampled native
  projections are byte-identical to the prior Engine header and agree with the actual demo WGSL on SwiftShader
  within `0.097 mm` position / `5.73e-5` normal-component maxima. Spring contact dynamics remain vehicle-coupled,
  and hard-drop convergence and measured tyre calibration remain open.
  [Standalone tyre evidence](../Exhibits/VehiclePhysics/StandaloneTyreProjection.md).

## Dynamic path reuse continuation — 2026-10-27

- An opt-in 50/50 uniform/power emitter proposal uses matching selection probabilities for explicit sampling and
  BSDF-hit MIS. SwiftShader area, colour-coded one/three/five-source, empty-source and four-hit textured-room
  tests cover proposal, blocker and relighting transitions. In a bright-blocked fixture its per-sample SD is
  `1.708×` uniform versus `20.586×` for pure power; with both lights visible it is `0.597×` uniform. The ideal
  fixed-receiver second-moment bound is twice uniform, **not** a variance or adaptive-reservoir guarantee.
  Previous modes remain default and their readbacks match the prior shader. A separate 128-batch seed repeat
  investigates an unusually low 24-batch uniform result; the repeat approaches the independent area integral,
  but independence and monotonic convergence are unproven. Physical-GPU/editor integration, equal-time
  performance, general motion and general unbiasedness remain open. Four settled and three first-transition
  SwiftShader readbacks now show the mixture through camera, object, source and blocker changes; release and
  sanitized-host decoded RGB hashes agree. A two-frame default-capture check matches the previous submission
  arguments bit-for-bit; the prior full-gallery recapture was stopped, not passed.
  [Mixture evidence](../Exhibits/LightCompute/ReplayFacetMixture.md),
  [readbacks](../Exhibits/LightCompute/ReplayFacetMixtureCaptures.md).
- Optional area/luminance-weighted emissive-facet replay now uses the shared Engine CDF and matching MIS
  probabilities; proposal, source-power and blocker changes retrace capped history. Independent area and
  four-hit room references pass on SwiftShader, while default-off cosine/GGX readbacks remain bit-identical.
  Visibility-blind weighting reduced unblocked sample SD to `0.181×` uniform but increased it to `18.26×`
  when the bright emitter was blocked. A separate 256-batch rare-event repeat is recorded; neither it nor
  the fixed-receiver room establishes general unbiasedness, hardware performance or clustered emitters.
  [Weighted replay evidence](../Exhibits/LightCompute/ReplayFacetPower.md).
- An optional Engine-shader cosine/GGX mixture improves the measured rough-metal environment SD to `0.138×`
  cosine and the moving textured floor's blue batch SD to `0.309×`; a nearly matte environment and one object
  channel get worse. Independent quadrature, bounded four-hit CPU references and changed-proposal/moving-blocker
  reuse pass on SwiftShader. Default-off readbacks match the prior shader bit-for-bit. This is not complete
  ReSTIR GI, general unbiasedness, a universal speedup or hardware/editor integration.
  [Proposal, controls and limits](../Exhibits/LightCompute/ReplayImportance.md).
- The bounded replay footprints, BVH/material/receiver guards, emissive-index compiler and exact-input history
  policy now have a single authoritative home in `Frontier/Engine/Rendering`. The exhibit links the reusable Engine
  library; the exhibit-local implementation was removed. Independent C++20 domain and C++17 legacy-archive tests
  pass in release and ASan/UBSan. This is not an editor-integrated GI pipeline or a scalable revision system.
  [Engine ownership and verification](../Exhibits/LightCompute/PathReplayEngine.md).
- Guarded single-stratum rough GGX and albedo-texture replay can now be opted into together. SwiftShader release
  and sanitized hosts pass independent hemisphere quadrature, two textured primary-pixel references and four-hit
  atlas/blocker/reopening references with capped temporal/spatial reuse. Two rough-textured moving-primary probes
  also pass independent four-hit references after camera, rigid and material changes and blocker insertion. An
  initial fresh-blue difference of 3.65 combined SE did not recur in a four-times-larger disjoint-seed follow-up
  (0.68 SE); its GPU SE rose, so convergence and seed correlations still need study.
  General motion/materials, hardware GPU and editor integration remain open. Four settled and three first-transition
  renders are captured; previews blur noise and are not ghosting proof.
  [Rough-textured evidence](../Exhibits/LightCompute/RoughTextureReplay.md),
  [moving reference](../Exhibits/LightCompute/RoughTextureMotion.md),
  [render gallery](../Exhibits/LightCompute/RoughTextureReplayCaptures.md).
- Independent double-precision primary rays and four-hit CPU paths now check actual textured pixels during a
  combined camera shift, rigid cuboid rotation, atlas change and subsequent blocker insertion. Release and
  sanitized-host runs match within stated statistical bounds on SwiftShader; both probes retain one temporal
  candidate in every transition. Four settled and three first-transition textured-motion readbacks were captured
  and inspected. This strengthens fixture evidence, not production integration or a general unbiasedness claim.
  [Textured motion reference](../Exhibits/LightCompute/ReplayTextureMotion.md),
  [readbacks](../Exhibits/LightCompute/ReplayTextureMotionCaptures.md).
- An opt-in opaque Lambertian texture subset now evaluates the existing albedo pyramid at secondary hits. Atlas
  contents trigger seed retracing and a one-candidate history cap; unrelated pipelines retain their hard reset.
  Release and sanitized-host checks cover 33 invalid inputs, primary and secondary filtering, and two-hit relighting.
  Four-hit textured rooms pass independent CPU references and paired textured/untextured controls while retaining
  capped history across atlas and blocker changes. Four settled and three first-transition SwiftShader readbacks
  were generated and inspected. After correcting an ignored reserved optics component, all 14 refreshed raw and
  preview PNGs were byte-identical. [Texture replay audit](../Exhibits/LightCompute/ReplayTexture.md),
  [captures](../Exhibits/LightCompute/ReplayTextureCaptures.md).
- A finite-domain audit exposes missing-support bias when previously dark paths become visible. A positive
  selection floor recovers the exact toy mean when random-number-domain paths are replayed.
- An isolated SwiftShader Vulkan experiment now retraces temporal and spatial reservoir seeds in current opaque
  triangle scenes. Moving-occluder, source, receiver and bounce-budget fixtures match independent area integrals
  or bounded plane results through four hits; changed visibility is recomputed. An opt-in rough GGX/diffuse mode
  also passes hemisphere quadrature and a four-hit moving-blocker CPU comparison within statistical bounds. This
  is not the demo's geometric reconnection and does not integrate general materials or a production/editor frame.
  Nearby-position and normal guides retain history across a tested camera shift and authored rigid-object motion;
  this is not general deforming-mesh reprojection. [Motion audit](../Exhibits/LightCompute/ReplayMotion.md),
  [four camera/object/blocker captures](../Exhibits/LightCompute/ReplayMotionCaptures.md).
  Optional explicit emissive-facet sampling passes occlusion, source-change, rough and four-hit reference checks;
  a reproduced near-source light leak is fixed in a bounded open-segment test, subject to floating-point limits.
  [Endpoint audit](../Exhibits/LightCompute/ReplayEndpoint.md). Optional balanced facet/BSDF sampling passes
  four-hit references but helps a large-light variance fixture and worsens a small-light one; no timing claim.
  [Sampling comparison](../Exhibits/LightCompute/ReplayBalance.md),
  [six Vulkan comparison captures](../Exhibits/LightCompute/ReplayBalanceCaptures.md).
  [Facet sampling](../Exhibits/LightCompute/FacetReplay.md), [four new Vulkan captures](../Exhibits/LightCompute/FacetReplayCaptures.md).
  Four earlier noisy SwiftShader readbacks also show bounce and blocker changes. [Audit and scope](../Exhibits/LightCompute/PathReplay.md),
  [captures](../Exhibits/LightCompute/ReplayCaptures.md).

## Analytic spot and IES cells — 2026-10-27

- Non-overflow SwiftShader cell lists match exhaustive summation for a spot, a synthetic IES point and an
  IES spot, including moved sources, source disabling and opaque shadows. Capacity-one and outside-grid
  fallback controls also pass; native RGB discrepancy is at most `1.23e-5`. Release and sanitized hosts pass.
  [Fixture and scope](../Exhibits/LightCompute/ClusterSpotIes.md).
- This is analytic-light clustering only. Emissive triangles use a separate global distribution or bounded
  replay-facet sampling; clustered emissive selection, physical-GPU and editor acceptance remain open.

## Gas and plume source audit — 2026-10-27

- Retained the exact full VehicleHMI smoke/fire JS and five WGSL programs, including its distinct moving-grid
  helper. All 28 retained full-demo blob hashes match; eight Node reference and stencil checks pass.
- The unchanged reference pressure WGSL also runs on SwiftShader CPU Vulkan and matches independent CPU stencils,
  including ground/open/solid probes and shifted-window clearing. In one bounded 3D fixture a near-grid velocity
  mode retains `98.1%` of its centred divergence despite a small six-point pressure residual after 256 sweeps;
  this is not a complete animated-plume measurement. Smoke/fire remains unported to the native renderer; advection,
  full solver, physical GPU and liquid-fluid scope remain open.
  [Gas reference](../Exhibits/FluidReference/README.md),
  [actual pressure-shader results](../Exhibits/FluidReference/PlumePressure.md).

## Vulkan solar optics and GI audit — 2026-10-27

- Isolated SwiftShader readbacks now verify the existing analytic starburst: six blades make six spikes, five
  make ten, aperture rotation moves them, and opaque source cover removes them. Starburst, warm streak and
  blue anamorphic layers add together. Three inspected images and release/sanitized results are in the
  [starburst audit](../Exhibits/LightCompute/SolarStarburst.md). This is not the demo's exact burst, editor
  integration, physical-GPU validation or measured-lens calibration.
- Opt-in finite-pixel Sun and mixed warm/anamorphic flares execute in the LightCompute Vulkan validation
  host, with limb-weighted opaque triangle and geometric-horizon visibility. SwiftShader readbacks pass flux,
  Cartesian integration and partial-occlusion checks. The editor remains unconnected; no physical GPU,
  Windows, real-time, Moon masking, per-ray clouds/fog or measured-lens calibration is claimed.
- An on-axis sensor-FWHM conversion accepts supplied measurements; the checked sensor example is synthetic.
  NASA's mean solar diameter checks angular size only. A 640×360 synthetic scene capture shows actual Vulkan
  solar readback, not a physical GPU or editor image. [Detailed scope and results](../Exhibits/LightCompute/SolarOpticsAndReuse.md).
- Direct-light reservoirs now retain dark proposal domains, normalize by selected support and store pre-spatial
  history. Changed receiver/view/material domains conservatively reject temporal history and match cold in
  tested fixtures; this is not general dynamic reuse.
- A separate Vulkan diffuse-path experiment supports identical-domain pooling and multiple opaque Lambertian
  hits, with revision resets. A second, one-hit experiment carries a translated rectangle endpoint and shifts
  reservoirs between moving/different receiver positions with an area Jacobian. Independent area integrals
  and an omitted-Jacobian negative control validate that strictly unobstructed, common-support fixture only.
  Neither experiment is integrated into the editor or scene frame.
- Full ReSTIR GI, general dynamic endpoint carry, occlusion-aware spatial reconnection and general multibounce
  reuse remain unfinished. The retained-demo temporal equation omits the audited Jacobian; this is not WGSL
  execution or a completed GI fix.

## Reference daylight and lunar bounce continuation — 2026-10-27

- Opt-in CPU reference-renderer surface Sun now follows atmospheric extinction and planet/triangle visibility;
  the atmosphere LUT rebakes when parameters change. Day/night HDR backgrounds include the actual demo Moon
  texture and a local-direction Hipparcos catalogue. Daylight naturally reduces star contrast.
- Opt-in lunar surface illumination and a static one-bounce, same-receiver temporal SDF reservoir are wired to
  the reference renderer. This is **not** full ReSTIR GI: no GPU, dynamic reprojection, GI spatial shifts or
  multibounce. Indirect shadows see analytic SDF primitives rather than general triangle meshes.
- The new release and ASan/UBSan suite passes 37/37; 6,228 star directions differ from Astropy/ERFA by at most
  `0.007170°` on the selected 2026 cases. Vertical atmospheric optical depth, lunar flux and a lunar-lit wall
  also have scoped analytic checks. Existing VehicleReference CTests pass 7/7 and the recorded default-off
  binary64 probe remains `04da4f54d91705d0`. Details and limitations: [Daylight evidence](../Exhibits/NightSky/DaylightReference.md).
- No physical sky/lunar photometry, measured observations, editor/Vulkan or lunar texture licensing approval is
  claimed. This entry supersedes the older reference-renderer Moon integration status immediately below.

## VehicleHMI, Project Drive and finite-pixel Sun continuation — 2026-10-27

- Demo 160-particle XPBD and Hex8 solid deformation modes are ported with controlled JavaScript comparisons;
  spring carries chassis loads. The 720-step extended impact diverges after step 645. Synchronized one-step checks
  match on that path; same-source perturbations show strong impact sensitivity, but do not prove the sole cause.
  The hard-drop timestep sequence is not converged. Shell is unported.
- Project Drive at `3f14808` is imported separately: six units preprocess identically and source/import traces match.
  The visual-only demo-tyre bridge passes a mock host; no editor/Jolt integration or measured-vehicle validation.
- The CPU reference renderer now uses finite-pixel solar filtering and an opt-in mixed-flare layer, with full
  mesh/planet occlusion checks. Seven-camera flux error ≤ `0.000439`; normal and ASan/UBSan results pass.
- NightSky Moon position now includes the Meeus periodic series and topocentric parallax. Against JPL DE421,
  max Moon direction error `0.003118°` over 960 selected 2026 cases (old exhibit `2.912255°`); this is a model
  comparison, not measured sky calibration. At that checkpoint the texture was NightSky-only; the later CPU
  reference-renderer integration is recorded above.
- An opt-in static one-bounce SDF-emitter temporal reservoir passes analytic tests. The existing CPU direct-light
  spatial reuse was corrected for tested static discrete-light domains (the old sparse mean was `0.502607` vs
  exhaustive `0.352533`; corrected `0.355025 ± 0.002030`). The retained demo's separate spatial path shader gives an
  exact 1.25 mean for a fixture with expected 1.0; no general GI fix is claimed.
  [GI evidence and scope](../Exhibits/LightCompute/StaticIndirect.md).
- [Celestial evidence](../Exhibits/NightSky/README.md); [vehicle evidence](../Exhibits/VehiclePhysics/Continuation.md).
  No physical lens/sky/tyre calibration, complete GI/ReSTIR, GPU execution or Windows/editor claim. Partial solar
  clipping at the planet limb, general subpixel occlusion, editor/GPU Moon integration and Moon-map licence remain open.
- This entry supersedes the stale XPBD/solar status in the earlier 2026-10-27 entry below.

## Sun disc, lens-flare optics, measured Moon — 2026-10-26

- `LensFlareModel` radiance layer: limb-darkened sun disc (flux 1.00001 vs analytic), aperture
  ghosts (centroids ≤0.01 px), 6-blade starburst, anamorphic streak, halo, glow, veiling glare.
  Demonstration audit: it contains no flare code and no lunar texture — neither is claimed as a port.
- `CelestialModel` measured lunar albedo (`EngineContent/CelestialMaps`), Lommel–Seeliger disc,
  pole orientation + Meeus optical libration cross-checked vs pole geometry (0.29°).
- Atmosphere: demonstration-constant parity pinned by check; Mie extinction split corrected
  (4.40e-3/km total); multiple scattering sun-gated and calibrated (was ~190%, sun-independent).
- Evidence: [NightSky](../Exhibits/NightSky/README.md) 33/33 with rendered images;
  EnvironmentPipeline regression gate 73/73. CPU reference only — no GPU/Slang, Windows,
  cloud, moonlight-in-scene or parallax claim.

## Prior-screen exitance, rigid motion and unified moving vehicle — 2026-10-26

- Roughness-cone exitance pyramid, verified natively (max mip RGB error 6.7e-7) and on SwiftShader; pinned 3e8259c control resets cold on authored motion; guarded lighting refresh matches cold build.
- Rigid prism reuses 318 pixels under motion; unified six-pose fixture: 14k–15k opaque reuse per moving pose, zero caps/invalid, 23,040 source-off black pixels.
- DistanceFieldSpace is a certified clearance bound (0 violations); it does not accelerate traversal. Traversal-acceleration claim removed.
- Sharp-strip estimator bias 5.7% (noise 1.2%) recorded as a limitation. [Report](../Exhibits/LightCompute/PriorExitance.md).
- Still open: physical RTX/GTX, Windows/editor, production scene, full denoiser, moving general authored geometry without conservative reset.

## Derived finite-thickness vehicle glazing — 2026-10-26

- Three exact-edge-connected components from 74 retained window triangles become 284 closed triangles at
  nominal 6 mm thickness. Native saved IOR1.52 / RGB absorption [1,4,12] feed the existing volume atlas.
- Fresh restored-build strict and ASan/UBSan:52 assertions,16 malformed cases rejected, six-pose closure;
  native saved-material39 assertions and four CTests passed. Current 13 SPIR-V entries rebuilt.
- Fresh SwiftShader320x200 /16 samples:887 glass pixels,1851 absorption-changed channels, cap/unsupported0,
  exact repeat, moved/cold equality and64000 source-off black pixels. Pinned250616f control fails59/16.
- Ray-aligned shared-edge intersection correction; finite2048-step guided-TIR budget; diagnostic TIR saturation.
  Material-buffer/descriptor ABI unchanged. Actual captures and limitations: [VehicleGlazing](../Exhibits/LightCompute/VehicleGlazing.md).
- Full retained transport chain completed0:camera/mirror/rough/window/diffuse, pinned controls, four CTests,
  retained640x400 vehicle901probes(max4.5973e-6), reconstructionRMSE.0103498→.00585726 and six poses passed.
  No physical RTX/GTX, Windows/editor importer,
  glass denoiser, general CAD offset certification or unified moving sun/sky/cloud/fog completion claim.

## Saved smooth-volume material spectra — 2026-10-26

- Optional native TOML IOR/RGB absorption survives disk round trips and material compilation without changing
  the shader material layout. Actual Vulkan slab uploads use the compiled spectrum; no shader arithmetic changed.
- Strict and sanitizer native authoring checks, pinned previous-decoder rejection and actual saved-material
  Vulkan slab/repeat/source-removal/rejection evidence: [SavedDielectric](../Exhibits/LightCompute/SavedDielectric.md).
- Vehicle edge audit: 74 glass triangles, 68 unmatched exact-position edges; thin-sheet assignment retained.
  No vehicle closed-volume geometry, glass studio preview, editor controls, general renderer importer,
  physical GPU or Windows-editor completion claim.

## Pole shadows and authored-reflection history — 2026-10-25

- Reported previous-project half-shadow not reproduced: 72 directional/point/spot, receiver-view, caster-route and light-selection combinations; 3,538,944 analytic comparisons, zero mismatches, including 111,408 far-half shadow checks. Previous project/screenshot still needed; no speculative shadow fix.
- Angular-aware authored rough-reflection reconstruction; very smooth/mirror/glass routes preserve fresh directional detail. Independent finite-reference RMSE 0.246571 → 0.0927787 (62.4% lower); 88 native filter probes, max RGB error 1.19058e-6.
- Separately reproduced geometry-history invalidation defect: unchanged emitter colour/token after movement retained 1,521 pixels (RMSE 0.156373); packed-facet revision tracking rejects history and matches fresh scene exactly. Camera accepts 1,339 / rejects 17 rough pixels; 427 smooth pixels remain exact; source-off is black.
- SwiftShader/Linux, 13 compute entries, strict Release C++ and four native CTests passed. [Actual captures, repro and scope](../Exhibits/LightCompute/ShadowHistory.md). No physical GPU/Windows/full-editor, full specular denoiser or old shadow-bug fix claim.


## Sampled rough-lobe reflections — 2026-10-25

- Plastic now integrates the shared BRDF with cosine/GGX rays; accepted screen proposals verify the actual reflected-ray/facet intersection and clear segment, otherwise use BVH. No separate sharp-reflection budget.
- SwiftShader: 14,941 accepted screen rays / 204,392 fallbacks; same-seed BVH-only HDR matches exactly. Off-screen source contributes; source-off is black; moved source matches fresh submission; exact repeat.
- Actual pinned sharp/new rough captures and two-position source animation: [RoughReflections](../Exhibits/LightCompute/RoughReflections.md). Four uniform-light checks, 384 native/34 Vulkan energy cases and four native CTests passed.
- Finite 16 samples/proposal retain visible noise. No screen-colour mip history, temporal specular reconstruction, full sampling/PDF acceptance, speedup or physical RTX/GTX/Windows/viewport claim. Cellular and all archive assets retained.










## Lower cloud budgets and material reuse audit — 2026-10-25

- User requested faster iteration: working fog scenes now use cloud32/fog-shadow32, self-shadow6 and view32.
  Actual 160×100 comparison against cloud64/shadow64 at two winds retained in `CloudBudgetComparison.png`.
  RMSE 0.00207406 / 0.00187121; suitable for iteration by visual inspection, not final cloud appearance.
- Comparison and low-only execution passed 702 native fog and 354 sky-cloud probes; changed-default fog
  regression passed 654 camera + 270 surface probes, history/toggle/bounds checks; native CTests 4/4.
  Release only this turn. Half the outer sampling budget, not a measured frame-time or hardware speed-up.
- `RunCloudPreview.sh` renders low-only by default; `COMPARE=1` opts into reference renders. Older high-budget
  fixtures remain explicit verification tools. See `Exhibits/LightCompute/README.md` for reproduction/scope.
- Audited user material source at upstream be1d0a8e45d39738f37ccf711522a745e326e149: 399/399 material archive
  files, 6/6 material ContentInterchange files and 47/50 Render files identical. Three vector-gradient files
  differ (176-byte upstream versus 112-byte local instruction layout); no partial import performed.
- Reuse existing material library; glints/flakes/glitter are the requested rewrite exception. Existing research
  and isolated software comparison found and reviewed; not a production winner or physical-GPU speed claim.
  Details: `Exhibits/TextureStudio/MaterialReuse.md`, `Results/MaterialReuse.json`. No glint rewrite implemented yet.

## Emissive-volume single scattering — 2026-10-25

- Shared Slang fog now gathers emissive triangle radiance with area/luminance CDF sampling, solid-angle
  conversion, HG phase, triangle visibility and analytic source-to-volume extinction. Both analytic lighting
  and emissive surface lighting remain. Optional `Fog[15]` budget 0–64; default 0 preserves prior presets.
- Original ControlVehicle/ground plus a disclosed 2×2 m calibration panel: four actual 160×100 Vulkan frames,
  468 native camera probes, maximum RGB discrepancy 1.59192e-5. Combined capture restores original analytic
  sources. `Exhibits/LightCompute/Images/FogFacetCombined.png` and `FogFacetComparison.png` show actual readbacks.
- ASan/UBSan checks add 448 native camera probes; independent 131,072-sample native rectangle integration
  matches analytic solid angle within 0.0446%. Cold repeat, surface isolation, zero density, empty CDF,
  occlusion and invalid budgets/CDF checked. External-driver leaks excluded; no global leak-free claim.
- Reproduction: `RunFogFacets.sh`; measured parameters and scope: exhibit README and `Results/FogFacets.json`.
  Still single scattering and finite/noisy sampling, not temporal fog, multiple scattering, final sky/cloud
  appearance or physical-GPU/Windows/real-time acceptance. Sun/flare/stars preserved; Moon planned.

## Fog shadow quadrature refinement — 2026-10-25

- Controlled 16/32/64/128 cloud-shadow budgets at fixed 32 view midpoints, two wind positions and original
  ControlVehicle; eight actual 160×100 Vulkan captures, 936 matching-budget native probes.
- 64 samples reduced full-image linear RGB RMSE versus finite 128-step quadrature by 96.57% / 96.39%.
  Changed the exhibit preset from 16 to 64; this costs 4× shadow samples, with no speed or convergence claim.
- `Exhibits/LightCompute/Images/FogSamplingComparison.png`: columns16/64/128, rows wind0/120.
  Full measurements, independent comparisons and serial reproduction: exhibit README and `Results/FogSampling.json`.
- Fresh ASan/UBSan: promoted preset 654 camera + 270 surface probes, additional budget study 1,040 probes;
  zero-density, occlusion, history, toggles and bounds passed. Four native CTests passed; external-driver leaks
  excluded. Subset sensitivity now uses shadow64→128, not a self-comparison.
- Corrected historical day-medium prose to the actual source: 0.0015/m, 450 m, not 0.0007/m, 650 m.
  Old 320×200 captures and numeric logs remain unchanged and clearly archived at 16 shadow steps.
- Regular view marching, temporal fog, multiple scattering, final cloud appearance, Moon and physical-device /
  Windows / production viewport acceptance are not completed by this refinement.

## Cloud-shadowed fog and light shafts — 2026-10-25

- Clarified provenance: inherited four-octave cloud density was ported and its rendering integration improved;
  it was not replaced by a new cloud-shape model. Earlier cloud/ground shadows did not establish air light shafts.
- Added new native/GPU height-fog single scattering, exact camera/light optical depth, HG phase, cloud visibility
  at each volume sample and triangle occlusion. Original point/IES/spot profiles illuminate the fog. Analytic
  arrivals and sampled emitter-to-surface segments receive fog extinction; both lighting paths remain.
- Actual 320×200 captures: `Exhibits/LightCompute/Images/FogSkyComparison.png` (cloud shadow off/on in fog),
  `FogLocalComparison.png` (local-light fog off/on), `FogComparison.png` (original ControlVehicle), plus wind120.
  The sky fixture uses an explicit thinner 800–1100 m broken-cloud slab to expose soft shafts, not a hidden effect.
- 654 numeric camera probes, 390 capture camera probes and 270 native surface-arrival probes. Daylight numeric
  RGB maximum discrepancy 1.14637e-5; local-light 1.04968e-5; capture maximum 5.48751e-5; surface maximum 5.21472e-5.
  Uniform Beer/ambient, near-horizontal stability, bitwise zero density, geometry occlusion, bounded steps,
  dry-history isolation and enable/disable cold equality passed. Focused checks passed ASan/UBSan.
- Sampling is not converged: on 12 numeric rays, view32→128 changed red radiance by up to 0.00311955 and
  shadow16→64 by up to 0.0293056. Remaining: alias/banding, temporal fog, multiple scattering, emissive-volume
  scattering, full inter-bounce/coupled-media transport and final cloud shapes/appearance.
- Thirteen entries, eight SSBOs, 128-byte pushes; no new output slabs or Int64 shader feature. SwiftShader CPU
  type 4 only, not physical-GPU/Windows/real-time acceptance. Existing native Sun/flare/stars remain; Moon planned.
- Reproduction, exact checks and scope: `Exhibits/LightCompute/README.md`, `RunFog.sh`, `Results/Fog.json`.

## Spatial cloud-shadow checkpoint — 2026-10-25

- Matching directional sunlight now integrates cloud extinction at each primary receiver and secondary hit,
  not at one observer position. Exact, reservoir-target and spatial-reuse evaluations use current-position T;
  proposal mass stays unattenuated with support retained. Both analytic and emissive lighting remain.
- Five cases × 433 native surface probes (2,165): maximum RGB discrepancy 8.87267e-6. Secondary transport:
  117 native probes, maximum 7.40833e-8. Each single-Sun case exercises 16,000 temporal and 63,438 spatial
  reuses; maximum exact/reused RGB discrepancy 9.53674e-7. Non-Solar analytic lighting is bitwise unchanged.
- Wind revision matches cold reconstruction. Reproduced an additional cloud-disable stale-history defect,
  then tracked out-of-prefix cloud selection: disable/re-enable now match cold direct/indirect output bitwise.
- Fixed omitted ambient RGB initializers from the previous cloud upload. The strengthened uniform check
  requires nonzero ambient. All 2,230 cube and 396 camera probes and corrected volume captures passed ASan/UBSan;
  maximum composite cube RGB error 8.32318e-6 and camera RGB error 8.63495e-6.
- Four actual 640×400 wind times (0/60/120/180 s), original ControlVehicle and original ground, are retained in
  `Exhibits/LightCompute/Images/CloudShadowVehicle.png`, `CloudShadowGround.png` and `CloudShadowComparison.png`.
  Cameras and vehicle are stationary; wind moves. The overhead direct-only view isolates spatial shadows.
- Final spatial/toggle ASan/UBSan and fresh-sampling checks passed. Legacy Vulkan, indirect reconstruction and
  native CTest 4/4 passed; their exact execution chronology is retained in `Results/CloudShadowRegression.txt`.
  Concurrent full-size software runs hit allocation errors; serial volume checks and resumed final view passed.
- Still twelve entries/eight SSBOs/128-byte pushes; no added slabs or Int64 shader feature. SwiftShader CPU
  type 4, not physical GPU, Windows or real-time acceptance. No new Moon/Sun/flare/star presentation integration.
  Remaining: flat-slab horizon artifacts, approximate ambient, indirect cube seams and finite-Sun penumbrae.
- Detailed evidence and reproduction: `Exhibits/LightCompute/README.md`, `RunClouds.sh`, `Results/CloudShadows.json`.

## Vulkan cloud environment checkpoint — 2026-10-25

- Added GPU cloud density, 64-step per-camera-ray integration, six-step Sun self-shadowing and a cloud radiance
  cube for indirect rays. Native 64-bit noise hash is emulated with paired 32-bit arithmetic; cloud/primary/
  transport SPIR-V requires only Shader capability, not Int64. CPU atmosphere precomputation remains.
- Added optional native/GPU uniform ambient cloud radiance (default zero preserves the earlier model). Sun at
  ground/cloud altitude uses atmospheric transmittance; one observer-to-Sun cloud factor shades local receivers,
  not a spatial cloud-shadow map. Cloud parameters and loop extents are checked before dispatch.
- 2,230 native cube probes: maximum T error 1.186e-5, RGB error 1.06147e-5. 396 direct camera-ray probes: maximum
  RGB error 9.02517e-6. Uniform Beer-Lambert, ambient closed form, clear-path bitwise equality, wind and bounded
  march checks pass. Full cloud cases passed ASan/UBSan; native CTest 4/4, recovered/native sanitizer checks,
  clustered ControlVehicle, legacy Vulkan and indirect-reconstruction regressions also pass.
- Actual clear/cloud/wind/3° low-Sun readbacks: `Exhibits/LightCompute/Images/CloudWeather.png`, `CloudSkyWeather.png`
  and `CloudComparison.png`. Fixture Sun angles/irradiance and camera are explicit, not observer/time ephemeris.
- Twelve compute entries, eight SSBOs, push ABI 128 B; cloud cache adds 221,200 B. Device remains SwiftShader CPU
  type 4; no physical-GPU/real-time/Windows claim. Existing native Sun/flare/stars and both light paths remain.
- Remaining cloud appearance: flat-slab/regular-step horizon structure, approximate ambient, indirect cube seams
  and spatial shadowing. Moon, GPU atmosphere and night/catalogue work remain in `Documentation/EnvironmentTodo.md`.

## Vulkan indirect reconstruction checkpoint — 2026-10-25

- Recovered saved branch from GitHub checkpoint `0a4560c2` after sandbox reset; restored pinned ignored tools.
- Added reprojection, current-neighborhood RGB clamp, capped temporal mean and surface-compatible 5×5 spatial
  filtering of indirect only. Current visible emission and sky misses stay separate; direct histories are unchanged.
- Isolated emission at 160×100: independent 16-seed reference, eight updates; RGB RMSE 0.0103498 → 0.0058573,
  **43.41% lower**. This is not a full-resolution moving-quality or equal-cost speed result. Independent native
  spatial kernel: 210 probes, maximum RGB discrepancy 4.8961e-8.
- Six full-resolution moving vehicle/camera poses exercise reuse and the pose-3 light reset. Emission revision and
  camera cut match cold reconstruction bitwise; all sources off is black. Visible emission is preserved; extracted
  direct output differs by at most 3.8147e-6 from final float arithmetic.
- Actual readbacks: `Exhibits/LightCompute/Images/TransportFiltered.png` and
  `TransportReconstructionComparison.png`. Legacy exact/cluster/reservoir/static-history, native primary and
  emissive-estimator checks passed; focused ASan/UBSan numeric/spatial/reset checks passed.
- Eleven compute entries, eight SSBOs, push constants 128 B; 21 float4 history/output slabs (86.016 MB at 640×400).
  SwiftShader CPU type 4 only. Clamping/smoothing are biased; moving emitter/shadow changes are not exhaustively
  revalidated. Physical RTX/GTX/Windows, glossy transport, GPU sky/clouds and Moon rendering remain open.
- Evidence/reproduction: `Exhibits/LightCompute/README.md` and `RunTransportReconstruction.sh`. Historical missing
  private scratch digests are explicitly labelled after recovery, not presented as current-byte verification.

## Vulkan emissive-area sampling checkpoint — 2026-10-24

- GitHub preservation verified before work: local/remote `3dfbc2d3` matched and the working tree was clean.
- Explicit area/luminance sampling over 1,130 emissive triangles replaces hemisphere-hit emission only. Analytic
  point/IES/spot/directional secondary shading and sky misses remain on the hemisphere route; visible emission
  is unchanged. Both lighting paths remain; no emissive clustering or authored proxy-energy accounting claimed.
- 225 native probes, 16 area draws: mean RGB error 7.2128e-9, max 1.36771e-6. Against independent 8,192-draw
  native reference probes, emissive RMSE 0.206886 → 0.00906289 (**95.62% lower**). This is not equal-cost speedup,
  a full-image quality statistic, or universal unbiasedness evidence.
- Six moving poses preserve current indirect bitwise versus fresh evaluation. Geometry-CDF rebuilds do not
  invalidate direct-light history; the changed analytic light at pose 3 still does. Probability ticks preserve
  dim emitter support; invalid CDF entries are rejected; empty-emitter and disabled-environment cases pass.
- Actual readbacks: `Exhibits/LightCompute/Images/EmissiveComparison.png` and `EmissiveMotion.png`. Coarse displaced
  bright footprints are substantially removed; residual grain remains. Indirect filtering is not implemented.
- Legacy exact/cluster/reservoir/history, native primary/cosine transport and new ASan/UBSan checks passed.
  Device remains SwiftShader CPU type 4. RTX/GTX/Windows acceptance, glossy transport and GPU environment are open;
  native Sun/stars/flare remain unchanged and Moon remains planned.
- Detailed source/device/sampling scope and reproduction: `Exhibits/LightCompute/README.md`, `RunEmissive.sh`.

## Vulkan one-bounce diffuse transport checkpoint — 2026-10-24

- `TraceSurfaceTransport` traces full-BVH secondary rays from Vulkan-generated camera surfaces. Secondary hits
  evaluate material emission and shadowed analytic point/IES/spot/directional lighting; misses sample a shared
  native-atmosphere cube. The primary response is diffuse; direct GGX lighting remains intact.
- 3,603,424 secondary rays; 901 independent double-reference receiver probes give mean RGB error 2.08237e-8,
  maximum 4.6271e-6. Analytic-only, emission-only and environment-only routes all contribute; isolated sum agrees
  within 8.44442e-6. All sources off is exactly black. Proxy-versus-emissive physical accounting remains open.
- Six moving frames retain current indirect/emission bitwise versus fresh evaluation, including the light-change
  reset. Actual readbacks: `Exhibits/LightCompute/Images/TransportMotion.png` and `TransportComparison.png`.
- Complete static/moving direct/primary regression and focused ASan/UBSan transport execution passed. Device is
  SwiftShader CPU type 4. Eight compute entries, eight SSBOs, 128-byte push constants; no GPU/Windows speed claim.
- Atmosphere is CPU-precomputed into six 48×48 float RGB cube faces, not CPU per-pixel lighting and not a GPU
  atmosphere/cloud port. Native Sun, flare and stars remain unchanged; Moon remains planned.
- Fixed 16-direction diffuse quadrature visibly creates coarse illumination footprints. Indirect denoising,
  emissive importance sampling, glossy/multi-bounce transport, environment appearance and physical RTX/GTX/Windows
  integration remain open. Numeric agreement with the finite quadrature is not a convergence/quality proof.
- Detailed reproduction, checks and source/device limits: `Exhibits/LightCompute/README.md` and transport reports.

## Moving Vulkan-primary history checkpoint — 2026-10-24

- Six authentic vehicle/camera poses now generate surfaces and rigid-motion metadata on Vulkan with CPU raster
  disabled and all receiver channels poisoned. Persistent temporal/spatial reservoirs and reconstruction execute
  over those generated receivers. Actual animation: `Exhibits/LightCompute/Images/PrimaryMotion.png`.
- Moving RMSE sum 0.651907 → 0.286237, **56.09% lower**; fixture quality, not equal-cost speed or GPU performance.
  Native motion projection agrees within 0.00012207 px. Uploaded per-pose rotation coefficients fix a measured
  shader trigonometry discrepancy; no CPU per-pixel visibility or motion buffer is supplied.
- Native ray/shading checks at all six poses; zero hit/miss disagreement. Eleven triangle identities differ at
  edges or within float-depth tolerance. Worst all-pixel RGB outlier is 0.960438; this remains a recorded
  robustness limitation. Matched-facet and all-pixel errors are separately reported, not conflated.
- Camera-cut versus cold restart, light-off invalidation and current facet-emission refresh pass bitwise checks.
  Full retained history, clustered lighting and primary clipping/fallback regressions pass.
- Device is still SwiftShader CPU type 4. This is direct plus visible material emission against black, not GPU
  indirect/emissive-source transport, sun/sky/cloud/Moon integration, physical RTX or Windows acceptance.
- Detailed evidence and reproduction: `Exhibits/LightCompute/README.md`, `Results/PrimaryMotion.txt` and JSON.

## Vulkan camera-ray visibility checkpoint — 2026-10-24

- Executed `TracePrimarySurfaces` on the authentic ControlVehicle, with native raster scene setup/render skipped
  and all 36 receiver floats poisoned before dispatch. Geometry, camera, material and light export remains CPU work.
- Independent double-precision rays: 225,214 hits / 30,786 misses, zero hit/miss or first-triangle disagreement;
  maximum relative depth error 5.00174e-6. Native barycentrics, BRDF, imported IES profiles and full-BVH shadows
  give mean RGB error 2.03235e-6, maximum 6.93947e-5. All-light and clustered output is identical.
- Near/far, nearest overlapping surfaces, all-miss and 256 forced-stack fallback checks passed. Exact shared-edge
  ties in the synthetic fixture are classified individually; nearest depths and shaded values remain unchanged.
- `Exhibits/LightCompute/Images/Primary.png` is the actual Vulkan readback, not an illustration. Device remains
  SwiftShader CPU type 4; no physical GPU, RTX query, Windows or production viewport proof is claimed.
- Seven shader entries / eight SSBOs / 128-byte push constants; private exhibit archive FLC3. Complete existing
  static/moving history regression and native sanitizer/CTest suite rerun successfully after the shared ABI change.
- This checkpoint is direct plus material emission against a black environment. Both analytic point/IES/spot and
  emissive lighting remain. GPU indirect transport, environment integration and moving GPU-primary history remain
  next integration gates. Preserve existing sun/lens flare/stars; Moon remains in `Documentation/EnvironmentTodo.md`.
- Reproduction, scope and measured results: `Exhibits/LightCompute/README.md`, `Results/Primary.txt` and manifest.

## EngineUpdate status

| ID  | Finding or task                        | Status      | Next action                            |
|-----|----------------------------------------|-------------|----------------------------------------|
| 001 | Checkout differs from source baseline  | Partial     | UI imported; rest deliberately absent. |
| 002 | Root agent rules used obsolete workflow| Implemented | Keep entry instructions consistent.    |
| 003 | Prototypes live in Documentation       | Open        | Inventory paths and launcher links.    |
| 004 | Inherited Internal tree remains        | Open        | Audit ownership before further ports.  |
| 005 | Old maps/backlog conflict              | Partial     | Historical pointers added; reconcile.  |
| 006 | Windows binary and GPU checks         | Partial     | Cross-built; Windows runtime pending.  |
| 007 | Four skills retained at old paths      | Intentional | Preserve until approved relocation.    |
| 008 | UI foundations and SVGs ported         | CPU checked | Windows integration still unverified.  |
| 009 | Six ImGui patches                     | Checked     | Applied, verified, reverted/reapplied. |
| 010 | Shared activation lifecycle           | CPU checked | Integrate into further scene classes.  |
| 011 | Scene renderer                        | Excluded    | Blank new Render directory by request.|
| 012 | Imported naming/formatting violations | Open        | Audit imported APIs before renaming.   |
| 013 | Source/artwork permissions            | Open        | Owner review before redistribution.   |
| 014 | Control Centre and quick panel        | CPU checked | Windows interaction validation next.  |
| 015 | Open fonts and catalogue              | Checked     | Four families, 30 faces, OFL retained. |
| 016 | Appearance persistence                | Checked     | Reload and invalid-save checks passed.|

## Verification

`Exhibits/ControlCentre/README.md` contains current commands, results, evidence, and limitations.
All six local tests passed in Debug and with address/undefined-behaviour sanitizers.
All 151 SVG symbols remain ready. Native Control Centre images are CPU raster output, not Windows GPU captures.
Windows x64 cross-compilation succeeded using Zig 0.13.0 / Clang 18.1.6 obtained through PyPI.
The unsigned Windows executable is packaged with fonts, SVGs, licences, and integrity metadata under ignored Binaries.
Native MSVC and Windows runtime/GPU execution are still unavailable. No Linux product binary is delivered.
No GitHub Actions are authorised.

## Known rule findings

The port preserves existing APIs for this bounded batch; it is not a clean naming/formatting certification.
Examples requiring repair: `VectorGlyphRecord`, short local names, comment vocabulary,
namespace brace layout, header banners, and non-approved decorative glyphs in inherited source comments.
The earlier official Windows example is now GLFW-hosted; its original display-adapter provenance remains explicit.
The four mandatory skill documents have not been weakened or edited to waive these findings.

## Scope history

The earlier documentation-only validation script was removed when that phase was clarified.
Subsequent user instructions authorise source porting, local validation code, proof images, and commit/push.
No full baseline replacement or inherited C++ relocation has occurred.

The selected theme/control configuration APIs now use `Configure...` rather than the banned `Assign...` verbs.
The new appearance files use Allman braces, flat namespace indentation, and descriptive file headers.

## Drawer and content correction

- Replaced the floating settings window with the reference top-edge trapezoidal drawer and inherited spring motion.
- Actual upward mouse swipe closes the sheet; added flick, axis lock, resize, focus-loss, and scaled-geometry tests.
- Renamed ContentDirectory and moved shared content into EngineContent/Icons and EngineContent/FontArchives.
- Native discovery reads metadata from TOML; Unicode file loading and corrupt-face rejection are exercised.
- GLFW 3.4 is pinned under ExternalPackages and used by the Windows host. toml++ remains the C++ format dependency.
- CLAUDE.md records Vulkan/Slang primacy, GLFW window ownership, content conventions, and TOML runtime formats.
- Windows runtime/GPU checks remain pending; no Actions are authorised.

#### Native Float32 vector surface map recovery (2026-10-19)

- Pattern scope remains approximately 68%; Stamp and full Pattern parity remain deferred/open.
- Added `VectorSurfaceCompute` with a typed `rgba32f` Vulkan storage image; the runtime derives format from SPIR-V,
  requires storage/sampling/linear-filter/transfer capabilities, and refuses unsupported devices rather than downgrading.
  Existing vector coverage and selection remain RGBA8.
- Compiles ordered layer/channel contributions from editable source, including visibility and layer masks. Separate
  RGBA32F maps cover colour, height, normal, roughness, metalness, opacity and occlusion. Contribution alpha is a
  separate mask; disabling a channel produces a neutral map and zero contribution. Layer-specific golden-thread
  metalness remains isolated from fabric metalness.
- Shared PFM encoding preserves IEEE Float32 bits, emits little-endian bottom-first `PF`/`Pf`, and does not clamp,
  gamma-transform or renormalize samples. The ZIP contains 14 actual GPU-readback fixture files, 32x32 each; it is
  not a document export. `Exhibits/TextureStudio/NativeSurfaceEvidence.html` presents the actual capture and GIF library.
- Seven real Vulkan channel dispatch/readback assertions pass strict and ASan/UBSan builds on SwiftShader. A cache-path
  regression now verifies 512x512 retired-slot submission and reuse of equivalent programs; it caught and fixed a reset
  that discarded the selected surface program before dispatch. The regression verifies authored gold/fabric separation,
  default neutral normals, finite Float32 samples, masks,
  PFM byte layout and strict/sanitized byte identity. Shader compiles with Slang WASM 2026.19 and reflects 24-byte
  surface push constants plus `Rgba32f` image storage.
- Native Texture Workspace now exposes the Pattern editor with persistent TOML, Vulkan coverage/selection, and a distinct
  dockspace. `PatternWorkspaceValidation` passes 606 checks on SwiftShader; its capture/result artifact is linked in the
  native evidence HTML. This runner did not execute a Windows desktop session.
- Float32-to-PBR bridge progress: 95%. `VectorSurfaceMaterialBridge` compiles five channels from a Pattern document,
  records their Float32 dispatches and image-to-host readbacks in one caller-owned submission, and exposes maps only after the
  caller's completion serial retires. No queue wait is inserted. Equivalent documents preserve the map cache; changed channel
  programs reconfigure and regenerate it. Five pyramids upload as RGBA32F authoring textures to the existing PBR shader.
- The native Texture Workspace host now routes its 512x512 Pattern document through that bridge on the shared Vulkan
  submission. Retired maps replace the selected material's live-preview channels; the material view shows the result without
  persisting source changes. The Windows x64 host cross-links, and packaging verifies all 13 surface/vector SPIR-V entries;
  native Windows GPU execution/capture remains a release gate.
- `MaterialRasterizationValidation` passes 224 checks on SwiftShader using 512x512 Float32 authoring maps and 128x128 PBR
  evidence captures. It exercises a single-submit five-map transaction, rejects premature retrieval, verifies
  unchanged/changed document caching, uploads actual GPU output, and compares the PBR centre capture with an independent
  scattering reference. The integrated bridge tests separate fabric and gold-thread layers: changing fabric Roughness to
  0.61 leaves gold Roughness at 0.18 and gold Metalness at 1.0 in the Float32 maps; reducing fabric SurfaceRelief from
  0.8 to 0.35 changes its derived tangent normals and PBR capture while keeping the gold-thread normal response unchanged.
  The three-frame C++ Vulkan GIF and provenance were regenerated from this 512x512-map run and remain embedded in
  `Exhibits/TextureStudio/NativeSurfaceEvidence.html`.
- `MaterialAtlasValidation` confirms changed Float32 authoring maps invalidate and repaint the same PBR preview selection,
  and that stale preview readiness is hidden until the replacement submission retires.
- Native Pattern channel enable toggles and strength controls are active for selected shapes; multi-selection applies each
  edit to every selected shape, and the document history preserves channel toggle Undo/Redo. Strength accepts 0–1 for
  ordinary channels and 0–4 for Normal; stitch/weave Height and Normal strengths cannot be reduced to zero. `PatternWorkspaceValidation`
  passes 626 checks, including selected-shape roughness-strength and surface-relief edits that leave the unselected layer unchanged
  and reach compiled GPU contributions. `VectorSurfaceValidation` confirms a GPU roughness disable removes only the fabric contribution
  while preserving the independent golden-thread roughness response.
- Native layers now persist a `surface_structure` classification (`freeform`, `stitch`, or `weave`). Selecting stitch/weave
  in the Pattern inspector enables and locks both Height and Normal channel toggles; document validation rejects missing,
  disabled, or zero-strength required contributions. TOML round-trip and invalid-weave rejection are covered in the native
  workspace validation, which passes on SwiftShader. This is explicit layer metadata and enforcement; automatic stitch/weave
  geometry generation and Windows interactive capture remain outside this phase.
- Native TOML now maps the web textile keys directly: `stitch_role` values `holes`, `thread`, and `highlight` resolve to
  NeedleEntry, StitchThread, and UpperThreadPass; `weave_role = "yarn"` resolves to WeaveYarn. The Pattern inspector can
  author these classifications, and validation enforces their parent stitch/weave structure and required channels. TOML
  compatibility exercises all four mappings; full Pattern JSON geometry import is still open.
- `VectorSurfaceValidation` runs the weave-classified fabric through all seven actual RGBA32F Vulkan channel dispatches,
  rejects disabled Height before compilation, verifies a per-layer Roughness strength of 0.23, and scales fabric relief by
  0.5 in GPU readback while preserving separate gold-thread height and normal response. `PatternWorkspaceValidation` confirms the inspector edit reaches the compiled
  contribution program. Both SwiftShader CTests pass; native Windows interactive preview capture remains open.
- Native evidence gallery now includes a three-frame C++ Vulkan PBR GIF: baseline, actual GPU-authored Float32 fabric/gold
  maps, and revised fabric relief/roughness with the gold response isolated. The linked JSON records source, executable and frame hashes; the fixture is SwiftShader, not a
  Windows desktop session.
- Still open: execute and capture the interactive Pattern 1:1-to-material-preview workflow on the native Windows host,
  including a per-shape channel edit; convert web Pattern JSON geometry and construction into native vector instructions;
  calibrate SurfaceRelief against measured height units and profile performance.
  Stamp remains deferred. The persistent/scene-level Apply action stays disabled until integrated native GPU submission and
  capture pass.
  GIF display is 8-bit evidence only; numerical Float32 checks are authoritative. No pixel stream is used.
- Reproduce: install pinned UI dependencies via `python Automation/PrepareUI.py`; compile the surface shader with
  `node Automation/CompileVectorSurface.mjs <Slang-WASM-JS> <shader-directory>`; run
  `python Exhibits/TextureStudio/Code/VerifyVectorSurface.py --driver <Vulkan-driver> --shader <VectorSurfaceCompute.spv> --destination <scratch>`.

#### Session rebuild on `arena/db2f9953-frontier` (2026-10-20)

- Restored pinned UI deps + fonts in a fresh sandbox, configured with `-DFRONTIER_HOST_VERIFICATION=ON`,
  built all host targets (GCC 12.2, Debug) and ran the full CPU suite: **21/21 CTests passed** (~70 s).
- Repaired four stale validation harnesses (no production-code changes): font inventory 4/30 → 5/35
  (`ControlCentreValidation`), per-domain icon rasterization (`InterfaceValidation`, Texture/Fluid symbols are
  denied to the Editor domain by design), canvas hover before preview F/arrow keys plus a non-vacuous F check
  (`MaterialPreviewConfigurationValidation`), and the full `###`-suffixed dockspace window name plus a null guard
  (`DockingValidation`, previously a null-`ImGuiWindow` SEGV). Docking also passes under ASan/UBSan.
- New `Exhibits/index.html` hub embeds the recorded HTML/native captures, per-workspace port status vs the Alloy
  reference (`Experimental/TextureStudio`), and this rebuild log; served from `Exhibits/` for preview.
- Still blocked in sandbox: SwiftShader/Vulkan readbacks (no `slangc`/driver), Windows link/run, Alloy `npm`
  verification. Carried UI defects (Project Browser 1:1, zoom FPS, layer cards, toggles, dropdown reuse,
  slider/ramp/gradient editors, rotation math, drag latency) remain queued after this rebuild.


## Windows preview 39483329514 (2026-10-20, branch arena/db2f9953-frontier)
- Commits: ba9b105 (settle-gated Pattern Float32 bridge, preview clip rects, dark-slate default stroke) + 42c9e54 (CI trigger for this branch).
- CI run 39483329514: Windows + Publish jobs success; Release unsigned preview at frontier-windows-39483329514.
- Linux host verification on ba9b105: full ctest 21/21 green; VectorSelectionValidation rotation/resize 1431/1431 pass (CPU).
- Confirmed open shader gap: PreviewPbrSurface samples albedo only; height/normal have no bindings (next round).

## Windows preview round 2 (branch arena/db2f9953-frontier, commits ba9b105..2fd6ea4)
- ba9b105: settle-gated Pattern Float32 bridge (drag perf), preview clip rects, dark-slate default stroke.
- 6b13015: parked material drafts across selection, full-bleed cover-fit preview + title scrim, exposure card, auto-size layer cards.
- 2fd6ea4: draped cloth preview shape, six-seamless-preset Pattern gallery, redesigned close dialogs.
- Linux host verification: full ctest 22/22 green (incl. 638 pattern-preset checks, cloth fold assertions).

## Windows preview round 3 (branch arena/db2f9953-frontier, commit e8755ee)
- Gesture-gated Pattern bridge (no regen while a gesture/pen stroke is in flight).
- Single-axis orbit rewrite (yaw about world-up, sway-tested old-fail/new-pass).
- Styled tooltip cards everywhere in Material; Open Library dialog restyled.
- TOML-backed preset gallery (six shipped .vector.toml files, built-in fallback).
- Apply-to-material gating with persistent snapshot per material.
- Linux host verification: full ctest 22/22 green (669 preset checks).

## Windows preview round 4, batch 1 (branch arena/db2f9953-frontier, unreleased)
- Pattern gallery preset thumbnails: CPU-drawn ImDrawList vector previews replaying each preset's
  instructions with VectorCoverage.slang corner/rotation conventions; click-to-open + hover tooltip.
- Project Browser Details rail: full projectspace metadata (identity/template/format/version, content
  roots, level/catalogue/pin counts, startup level, workspace theme/accent/font/scale) + Open button.
- Editor activation: opening a project launches EditorWorkspace (proven WindowsUI pattern ported into
  WindowsFrontier) with a top bar (back, project name/stats, Control centre / Theme / Settings buttons
  driving the ControlCentre drawer), a Viewport summary tab, and return-to-browser teardown.
- Materials: procedural detail textures (Automation/GenerateSurfaceDetailTextures.py) wired into five
  presets — tour/practice golf-ball dimple normal+ORM (TextureMask 6, 4x repeat), three solar-module
  cell/busbar albedo+ORM+normal sets (TextureMask 7, 2x repeat, shared ORM/normal).
- Correction to the round-2 note: the material preview already renders through MaterialSurface with full
  texture bindings (normal/height included) whenever a material residence exists; PreviewPbrSurface is
  only the albedo-only fallback. Golf/solar relief was a data gap, not a shader gap — no shader port needed.
- Height remains pattern-authored (AuthoringMaps[2] via Apply-to-material); .material files have no height slot.
- Linux host verification: full ctest 22/22 green (Docking close-all updated for the Viewport tab;
  MaterialLibraryValidation now compiles all five texture-wired presets incl. PNG decode).
- Still open: rotation-axis mapping vs user expectation, asset-browser HTML 1:1 (HTML file not in repo),
  remaining surface-detail ports (orange-peel, scratches, weaves need shader/UBO targets), zoom-FPS check.

## Windows preview round 4, batch 2: orbit fix (branch arena/db2f9953-frontier, unreleased)
- User report: yaw spins fine, but vertical drags roll "as if on Z" and the pivot feels off-centre.
- Root cause: orbit composed yaw-after-pitch (Ry*Rx), so pitch increments rotated about the yawed local
  X axis (a view-axis roll at 90 yaw) and yaw leaned the object up-axis sideways by sin(yaw)*sin(pitch).
- Fix: LookAt-style orbit (Blender/three.js convention) — yaw about screen-vertical world-up through the
  pivot, pitch about the fixed screen-right axis. Pure-yaw and pure-pitch poses are bit-identical to
  before, so the approved feels are untouched; only combined yaw+pitch changes.
- Correction to the round-3 sway test: zero-ellipse turntable yaw is incompatible with a screen-vertical
  object up-axis (proven: fixed front height + vertical up forces no yaw). Replaced with three
  old-fail/new-pass assertions: up-axis never leans, pitched tips never roll, pitched yaw traces the
  standard screen ellipse; plus a pivot-exactness guard across all five shapes.
- Linux host verification: full ctest 22/22 green; old matrix fails the new lean/roll/ellipse assertions.

## Windows preview round 4, batch 3: Alloy preset parity (branch arena/db2f9953-frontier, unreleased)
- User rejection: the six native-invented presets (and their gallery) did not match the Alloy Studio
  HTML. "Alloy Studio" is the web TextureStudio itself (schema alloy.pattern.v1); source of truth is
  Experimental/TextureStudio/src (PatternEditor.jsx gallery, patternDocument.js catalog of 103 families).
- Converter upgrades (Automation/ConvertWebPattern.mjs): per-copy instructions stay lean when they fit
  the 128-instruction budget, else tile-wrapped copies concatenate into one tile-space contour range
  (identical union under the same fill rule); minimal adjacent same-paint merging rescues the 65-layer
  Graduated lattice; half-drop/single-axis tiling accepted as documented straight-XY equivalents (baked
  tile pixel-identical, neighbour arrangement differs); miter/bevel joins and butt/square caps substitute
  round; anisotropic strokes use the larger axis; non-sized flowers import. All 178 family x palette
  variants convert (was 143 OK / 35 fail); deviations attach to each conversion for the sidecar.
- GPU/host budget raise: cubic segments 4,000 -> 32,768 (128 KB -> 1 MB storage buffer; shader needs no
  change since segments are an indexed buffer), vector source 600 KB -> 4 MB (codec, gallery loader,
  workspace save/load, import validation). Instruction cap stays 128 (shader stack array). Catalog peak
  is 18,954 segments (weaves); 25,600 is the editing-API maximum, so the 32,768 boundary is pinned by a
  struct-level coverage-validation test instead of the join fixture.
- Batch export (Automation/ExportPatternLibrary.mjs): 103 families / 178 palette TOMLs (37 MB) +
  Library.toml sidecar (families, 8 collection groups, 4 palettes, per-palette files, 24 deviation
  notes) under EngineContent/PatternArchives/TextureStudio/starters/. Serialized floats round to 7
  decimals (float32-parse within 1 ulp, invisible). The six invented TOMLs are deleted; the C++ fallback
  builders remain only for a missing preset directory.
- Gallery ported 1:1 from PatternEditor.jsx: kicker, two collection shortcuts, collection dropdown,
  search (name/group/description/study), conditional print-palette dropdown, heritage note, result
  count, two-column cards (76px cover-fit previews, wrapped names, description tooltips, selected
  ring), 24/page paging, trailing notes. Decoding is lazy per page (96-entry FIFO cache). Two
  documented deviations: DM Sans has no arrow glyphs, so paging/shortcut arrows render as ASCII
  < >; "Generate a layout" generator section is deferred (self-contained follow-up, not presets).
- Card tessellation: paint-order layer walk resolving Over/boolean composition; cubics flatten,
  RDP-simplify (closed-loop farthest-point split) and ear-clip to triangles once per preset; strokes
  replay as single polylines; fills submit batched with off-thumb culling. Fixed two real bugs found
  by headless harnesses: RDP collapsed closed loops (degenerate chord), and 16-bit ImDrawIdx overflowed
  past 64K verts per window draw list — 32-bit indices are now a PUBLIC compile definition on
  FrontierImGui (tracked; the vendored imconfig.h is untouched because CI recovers imgui fresh).
- Validation: PatternLibraryPresetValidation is Library.toml-driven (pins 103/178, decodes and
  coverage-validates every file, 1954 checks); new PatternLibraryGalleryValidation pins collection
  counts (30/16/10/10/10/10/10/7) and search counts, tessellates all 178 variants into finite geometry
  (830K triangles); headless card-render harness draws all 103 cards with draw-list invariants intact.
- Linux host verification: full ctest 23/23 green; converter node --test 7/7; ASan/UBSan clean on
  decode, tessellation and card rendering.
- Still open: rotation-axis follow-up already shipped in batch 2; asset-browser HTML 1:1 (HTML file
  still not in repo); "Generate a layout" section; remaining surface-detail ports; zoom-FPS check.

## Slate layer-stack port (2026-10-21, Slate arena/4006465a-slate cb8dcf0 onto db2f9953@34537e1)

- HTML first: `Experimental/TextureStudio` gains the Alloy material workspace (MaterialPanel PBR layer
  stack with per-channel sources, MaterialSpecification authoring document, FieldSpecification coded
  generators, FieldProjection Float32 atlas, editable camouflage palettes); catalogue 127 -> 131.
  Verified by production build and 7/7 Node model checks; Playwright specs ported but not run
  (browser download blocked in the sandbox).
- C++ port: coded fields in MaterialProceduralBaking (double-precision JS parity, 8 new ops on
  Albedo/Normal, palette text params), `Engine/ContentInterchange/MaterialAuthoring` document model with
  strict normalize, `[Authoring]` TOML round trip in SurfaceMaterial, pool text edits
  (ConfigureProceduralText), panel palette colour editors, deterministic catalogue export of the four
  camouflage presets with coded Albedo channels. Evidence: `Exhibits/TextureStudio/Results/LayerStackPort/`.
- Linux host verification: full ctest 26/26 green (new MaterialAuthoringValidation pins JS goldens,
  normalize rejection, link/designer projection, TOML stability, catalogue camouflage bakes).
- Boundaries still open: coded fields bake no ORM channel; woodland fabric weave relief is not yet
  composited under the camouflage field; Float32 field atlas stays HTML-only (native path is the CPU
  baker); panel renders strata rather than the full HTML authoring sheet; Windows runtime unverified.

## Inherited tree reclamation (2026-10-22, branch arena/1b8b280c-frontier)

- Reclaimed the inherited engine code trees from this branch (`Internal/`, `Executables/`,
  `Applications/`, `memory/`, `PaintingSurface.bat`; 736 files). Complete trees remain on `master`
  and in history; nothing in the current build referenced them (verified by configuration census
  and a full host build + test run).
- Retained the inherited design documents under `Documentation/Plans/References/InheritedEngine/`
  (170 files: `Prototypes/`, `Research/`, root-level plan/reference notes, and the notes that lived
  inside the reclaimed trees). `Documentation/FolderStructure.md` records the review outcome.

## Orbit staging and LED radiance correction (2026-10-22, branch arena/158c63c8-frontier)

- Specimen-group pitch (0.18 rad, baked into ball+pedestal) removed: pedestal lathe is level
  (rim height spread .34 -> ring-exact), Pivot/Radius re-measured. Preview key light, sky up and
  studio softboxes are world-locked through the orbit (identity at the default pose).
- LED radiance sampled through the first stratum's UV transform instead of raw coordinates.
- MaterialPreviewConfigurationValidation pins ring level + rigid light/up carry; shaders compile
  via the pinned Slang toolchain; host suite 26/26.

## Environment archives and layered inspector (2026-10-22, branch arena/7aa23edd-frontier)

- Baked five neutral equirect studio environments (`EnvironmentBake`) into
  `EngineContent/EnvironmentArchives/`; the shader samples them as a world-frame texture array
  (set 0 binding 3), replacing the tinted analytic presets and the analytic softbox integral.
  Key light + strength are derived per archive at scan time; equirect PNG drops import by rescan.
- Viewport: environment dropdown is now a card with CPU sphere previews, Exposure and Light
  intensity; the standalone exposure card is removed.
- Inspector: recipe controls categorized per stratum under LAYER headers (groups preserved as
  cards), uncovered stratum scalars editable per layer (incl. Thickness), profile-less generic
  editors on all layers. Host suite 26/26 (645,546 preview-config checks incl. card gestures);
  Windows GPU confirmation pending the user's hardware test.
- Still deferred: sand op binding, thin-film, glass depth, camouflage/ballistic underlays.

## Environment-switch freeze correction (2026-10-22, branch arena/71dc0cca-frontier)

- Windows test log: `PreviewConfiguration[2]` (now the environment layer) was still pinned to
  zero by `ValidateMaterialScattering`, so every non-default environment threw inside
  `ConfigurePreview`/`ConfigureSelection` and the viewport froze on its last good frame.
  Contract now bounds the layer slot; pinned by CPU checks (645,550).
- Light intensity missed the smoothed-camera discrete copy (applied only on settle snap); fixed.
- Environment sampling at mip 2 (firefly stipple), strength target 0.3 -> 0.2, key weight
  0.4 -> 0.32 (tonemap flooding), catalogue cap 24 -> 8 (pyramid layer contract). Suite 26/26.

## Fuzz/sheen BRDF and inspector card layout (2026-10-22, branch arena/b36a0fa5-frontier)

- Fabrics (velvet, wool, denim, cotton, silk, satin, …) and the leather/paper/foliage recipes that
  already carried `sheen`/`sheenRoughness`/`sheenColor` sliders were missing the grazing-angle rim
  lobe the HTML reference renders; native output looked flat. Added a Charlie-distribution +
  Ashikhmin-visibility sheen term (glTF `KHR_materials_sheen`-style), identical math in CPU
  `ScatteringSequence.h` and GPU `ScatteringEvaluation.slang`, additive on the opaque stratum only.
- ABI: `ScatteringStratum` gained `Sheen`/`SheenFinish` float4s (64 -> 96 bytes); cascaded through
  `PreviewScattering` (672 -> 928) and `MaterialScattering` (1504 -> 1760), every offset re-asserted
  in `SurfaceMaterial.h` and the GPU-reflection parity guard in `Automation/CompileSurfaceShaders.mjs`
  (this guard caught a stale hardcoded size before it could ship, the exact failure mode PR #7's
  dormant-contract freeze taught us to watch for).
- `SheenColor`/`SheenIntensity`/`SheenRoughness` are now live TOML keys, wired end to end: the colour
  swatch and the recipe sliders both write through `MaterialLibrary::ConfigureStratumColour` /
  `ConfigureStratumCoefficient`. The "Sheen colour" swatch is data-driven — any recipe profile that
  exposes a `sheen` control gets it, not a hardcoded per-material list.
  37 shipped `.material` files seeded with their HTML-authored default sheen values (fabrics, leather,
  paper, foliage) so the lobe is visible without hand-tuning. `MaterialLibraryValidation` gained a
  coefficient/colour/compile round-trip check for the new fields.
- Inspector cards: added an 8*Scale inset so cards no longer sit flush against the panel edge and
  scrollbar; fixed the per-card row count (profiled Finish cards no longer pad in 3 phantom rows,
  unprofiled Finish cards now count their real 6 generic sliders) so a card's background no longer
  under/overshoots its own controls; every card (not just the first per layer) now carries a small
  "LAYER N" tag so grouping stays legible while scrolling past several cards in the same layer.
  Host suite 26/26.
- Deferred, unchanged: sand op binding, thin-film, glass transmission depth, camouflage/ballistic
  coded-field underlays.

## Save-dialog modal chrome (2026-10-22, branch arena/8c806870-frontier)

- The "Unsaved material", "Open material library" and "Material destination" (create/duplicate)
  dialogs in TextureWorkspace, plus Pattern's "Unsaved pattern" close prompt, inherited the docked
  workspace's zeroed `WindowRounding`/`WindowPadding` and rendered as flush, square-cornered boxes
  with text crowding the edges.
- Added a local `MaterialModalScope`/`PatternModalScope` RAII helper per panel (mirrors the existing
  `MaterialTooltipScope` and the Editor's `ConfirmationDialogue` convention already used elsewhere)
  that scopes 20x18 padding, 10px rounding and a faint 1px border to just these popups, restored
  before the surrounding workspace's zeroed style resumes. No layout/behaviour change otherwise.
  Host suite 26/26.

## Fork merge: c7egoist/Frontier arena/ec6ca01d-frontier (2026-10-23, branch arena/c8d99312-frontier)

- Merged `c7egoist/Frontier@arena/ec6ca01d-frontier` as-is, same handling as the earlier
  `c7egoist`/other-session fork merge (PR #8): its unique commits beyond the shared `5a8c1b8`
  ancestor are a standalone-engine GGX material response port and an atmospheric-environment MIS
  integration, plus the `Exhibits/CompleteRenderer`, `EnvironmentPipeline`, `IlluminationChain` and
  `IndirectIllumination` reference exhibits and the `Frontier/Engine/Rendering/*` portable renderer
  tree they build on. Each exhibit carries its own standalone `CMakeLists.txt` and is not wired into
  the root build (same as the prior fork's `Frontier/`, `VisualProof/`, `Docs/` overlay) — merge was
  clean with no conflicts, since this branch's commits since the shared ancestor don't touch the
  files this session's sheen/inspector/dialog work changed.
- Verified: full root build + `ctest` 26/26, confirming the merge is inert for the shipped build.

## Pattern layer stack visual parity + selection-bounds regression coverage (2026-10-23, branch arena/649fea90-frontier)

- HTML Pattern Studio's layer rows (`PatternEditor.jsx`) now reuse the Texture workspace's
  `tp-layer-row` chrome verbatim: a drag handle, a per-layer preview thumbnail (new
  `PatternLayerThumbnail.jsx`, rendering the layer's own vector contour sized to just that shape's
  own bounds via `patternSizedPath`, falling back to the shape-kind glyph for kinds with no
  contour), the name/kind/opacity meta line, then eye/lock icon buttons — same classNames, same
  look, as requested, instead of the retired bespoke `se-layer-row` styling. Retired the now-dead
  `se-layer-row`/`se-layer-name`/`se-layer-toggle` CSS across `shapeEditor.css`, `studioTheme.css`
  and `artworkAuthoring.css`; added narrow overrides so the legacy `.pe-layers button > span` 12px
  swatch sizing doesn't clobber the ported thumbnail/kind-icon/name slots.
- Investigated the reported native (C++) Pattern Studio selection-hitbox issue ("highlight spans the
  whole canvas instead of hugging the selected shape"). Traced `QueryVectorSelectionExtent` /
  `QueryVectorAuthoringSelectionBounds` (`VectorAuthoringSelectionEditing.cpp`) and the pen tool's
  `EncodePath` (`CubicPathEditing.cpp`): both already compute tight, shape-local bounds for a single
  selected primitive or hand-drawn/library cubic-contour path in every scenario exercised, including
  a tile-normalized library-path simulation. Found and fixed a real gap instead: `VectorSelectionValidation.cpp`
  (1431+ assertions covering this exact selection-bounds/resize/reorder surface) existed but was
  never wired into CMake/ctest, so it never ran in CI. Added it as a real ctest target and added two
  new regression checks pinning a hand-drawn motif and a tile-normalized imported path to their own
  tight bounds, not the full 512 canvas. Host suite 27/27 (up from 26). If the on-canvas symptom
  still reproduces after this, it is most likely the multi-select case (selecting an entire
  array-placed group, whose true combined bounds legitimately span the tile) rather than a bounds
  bug — flagged for the user to confirm against the next preview.
- Ported the same layer-row preview into the native Pattern panel. `ConstructLayers()`'s row no
  longer draws a static placeholder swatch: it now renders that layer's own shape, cropped to its
  own tight extent (new `EncodePatternLayerThumbnailSvg`/`RasterizePatternLayerThumbnail` in
  `PatternThumbnailRaster.h/.cpp`, sharing the existing per-instruction SVG emitter with the gallery
  encoder so the two never drift). A second, smaller GPU atlas (`PatternWorkspacePanel::LayerAtlasTile`
  = 64px, 12×12 grid) carries every row's thumbnail for the open document and rebakes only when the
  document's edit revision moves; `WindowsTextureStudio.cpp` wires it up exactly like the existing
  library gallery atlas (`PatternLayerAtlas`/`PatternLayerAtlasDescriptor`, stage/upload/confirm/
  finalize, `Pattern->QueryLayerThumbnailAtlas`). Before the atlas is baked (or if no host wires it,
  e.g. in a headless/test build) the row falls back to a CPU vector replay of just that one layer via
  the existing `TessellatePresetDocument` tessellator, and only falls back further to a bare outline
  if that layer has no renderable geometry at all. `PatternThumbnailAtlasValidation` gained two new
  headless test blocks (single-layer thumbnail crop + the open-document layer atlas revision/bake
  contract) — 58 checks, up from 45, no new exhibit/CMake target needed. Host suite stays 27/27.
  The Vulkan wiring in
  `WindowsTextureStudio.cpp` is Windows-only and cannot compile-check in this Linux sandbox (same
  constraint as the rest of that file); it is a line-for-line structural mirror of the already-shipped
  `PatternAtlas` plumbing, reviewed by hand, and will be confirmed by the next Windows CI build.
- Pattern library motifs: the composable procedural modifier stack the user asked for (their own
  worked example — Diamond weave as one ring array/inset, mirrored top/bottom, then aligned to the
  tile edges) now exists in both HTML and C++, proven against the "Diamond weave"/"Diamond Weave"
  starter in each:
  - HTML: new `Experimental/TextureStudio/src/patternProcedural.js` exports three pure, reusable
    operations over a plain layer array — `arrayInset(layerFactory, kind, base, {count, sizeStep,
    stepX, stepY, colors, roles})` (repeats/shrinks one base layer, optionally role-tagging each
    copy), `mirrorMotif(motif, {axis, through})` (reflects x/y about a line, negating rotation) and
    `alignToEdges(motif, anchors)` (translates a motif cluster to tile-relative anchors, optionally
    recolouring by the `role` tag an anchor's `recolor` map names). `patternDocument.js`'s "Diamond
    weave" starter branch now calls these three operations instead of three nested hand-written
    loops; verified bit-for-bit identical output against the old flat implementation (same 16 diamond
    layers, same coordinates/colours, via a direct comparison script) before and after the rewrite.
    `validatePattern()`'s field-by-field layer rebuild already drops unknown properties, so the
    generation-only `role`/`proceduralRole` tag never reaches the saved/exported document.
  - C++: three analogous free functions added to `Engine/ContentInterchange/PatternLibraryPresets.h`
    — `ConstructInsetMotif` (runs the existing, already-shipped `GenerateVectorAuthoringArray` engine
    against a throwaway scratch document and harvests the instructions as a plain list, so Array/Inset
    reuses one tested placement engine rather than a new shrink loop), `MirrorVectorAuthoringMotif`
    (reflects a plain instruction list about an axis) and `AlignVectorAuthoringMotifToEdges`
    (translates copies to anchors, recolouring instructions that match a sentinel accent colour). A
    new 7th library preset, "Diamond Weave" (`ConstructPatternLibraryPreset(6)`,
    `QueryPatternLibraryPresetCount()` now 7), is built from exactly these three calls: one 4-ring
    inset array (242/189/136/83, matching the HTML sizes), mirrored across the tile's vertical
    midline, then aligned to the left/right edge quarters with two different accent colours.
    `PatternLibraryPresetValidation` gained a dedicated structural check (ring count, inset sizes,
    per-edge accent count) plus the count-change assertion bump to 7 — now 2065 checks, all existing
    index-0..5 preset checks untouched. Full build + `ctest` → 27/27 PASSED.
  - Still open: this pattern (one base shape + array/inset + mirror + align-to-edge) has only been
    applied to the Diamond weave/Diamond Weave starter so far, as the proof of the approach — the rest
    of the ~7,874-line, 9-module HTML pattern library and the remaining C++ presets have not yet been
    converted. Scoped but not started; large enough to warrant explicit user sign-off before an
    unattended full-library pass (risk of silently regressing 100+ shipped pattern designs without
    per-pattern visual verification).


## Vulkan illumination history — 2026-10-24

- Shared Slang temporal/spatial reservoirs, rigid reprojection rejection, temporal moments and edge-aware
  reconstruction now execute in native Vulkan validation on SwiftShader CPU. Six moving ControlVehicle poses,
  exact-light references, rejection diagnostics and light/residual invalidation checks are retained together in
  `Exhibits/LightCompute/README.md` and `Results/History.json`.
- Local quality checks: 69.08% lower static RMSE after sixteen updates; 56.04% lower mean per-image moving RMSE.
  These are fixture measurements, not equal-cost timings or general unbiasedness claims.
- Still open: physical GTX/RTX and hardware ray queries, Windows execution, production viewport, deforming motion,
  wider history/bias regressions and GPU primary/indirect transport. Filtering remains visibly imperfect.

Emissive-volume regression checkpoint: legacy fog ASan/UBSan checks, Vulkan exact/cluster/overflow,
static/moving history, light-off/residual refresh and all four native CTests passed. The first legacy history
attempt used the primary-motion archive (no uploaded raster receivers), correctly failed its coverage assertion,
and was rerun successfully using native `--vulkan-input` and `--motion-input` archives. Chronology is retained
in `Exhibits/LightCompute/Results/FogFacetRegression.txt`; this was a fixture-selection error, not a renderer fix.

## Closed-glass vehicle motion — 2026-10-26

- Six native posed ControlVehicle views with284closed glass/4mirror triangles each pass SwiftShader CPU Vulkan:
  exact fresh directional glass/mirrors, conservative geometry reset,17718stationary opaque history reuse,
  emission revision and23040black source-off pixels. Actual GIF/report: `Exhibits/LightCompute/GlazingMotion.md`.
- Original pose4 failure preserved (2capped/0unsupported paths); faint-path compensated survival addresses the
  capped absorbing continuations without raising2048/128limits or relaxing zero-cap tests.240production-block
  GPU arithmetic cases pass; all5previous static PNGs remain byte-identical.
- Complete retained optical chain after the shader change passed(exit0), including vehicle transport and
  old-material moving-history regressions. No physical GPU, Windows/editor,
  continuous moving authored indirect reuse, glass denoiser or completed unified renderer claim.

## Thin-pane transmittance, volumetric shafts and multi-stratum mixtures — 2026-10-26

- `SegmentShadowTransmittance` and `CrossedPanes < 8` close the remaining thin-sheet (`Category == 2`) transmittance
  gap across compact surface area-emitter quadrature (`SampleEmissiveFacet`), emissive volumetric fog (`SampleFogFacet`),
  analytic volumetric fog (`IntegrateFogScattering`) and 8-pane BSDF continuation, while keeping surface baking out of
  shadow/fog loops. Report and actual 4-panel Vulkan captures: `Exhibits/LightCompute/PaneTransmittance.md`.
- Verified on SwiftShader CPU Vulkan (`type=4`) + ASan/UBSan native C++: 8/8 native checks (`7.37e-7` balance-heuristic
  vs cosine identity), 2,016 surface emitter probes (`1.54e-8` max), 922 analytic fog probes (`7.09e-7` max), 541 emissive
  fog probes (`1.45e-6` max), 4 reused multi-stratum preset mixtures (`0.00969 -> 0.00745 -> 0.00730`), pinned `f5d99d4`
  negative control (`0.9937` floor leak vs `0.6563` accepted), 4/4 CTests, and 5/5 static `VehicleGlazing` PNGs byte-identical.
- Still open: roughness-mip prior-screen exitance/denoising, unified moving `ControlVehicle` + baked distance-field +
  sun/sky/cloud/fog scene, physical RTX/GTX + Windows/editor integration, legacy half-pole defect and native Jolt dynamics.


## VehicleHMI reference correction and low-cost raster continuation — 2026-10-27

- Continued `ae0bb7d` on `arena/2711375f-frontier`; corrected the incomplete-source audit, not a new physics rewrite.
- Full reference at `6afb6f00b5f25c45f7cda92c7da65b64845768f3` contains lunar textures, separate warm/anamorphic
  streaks, 160-particle XPBD and continuum tyres. Selected originals + hashes: `Experimental/VehicleHmiReference`.
- Restored actual Luna 2K JPEG; fixed missing ignored texture and sRGB decoding before filtering. Added independent
  warm streak and Cornette–Shanks aerosol phase. Sky 41/41; environment 73/73 release and ASan+UBSan.
- Existing production physics 29/29; full-demo original tests 5/5; recovered XPBD audit 12 cases verifies prescribed
  spring-force output, not integrated continuum reactions. Existing 84-particle lattice is not the full-demo port.
- Exhibit raster retains Lambert/Blinn + planar shadows. Improved hemispherical contrast, sky-coloured shadow fill,
  exposure-before-tone-map; skip matte highlights and reuse view distance. Same-camera paired medians 60.994–66.354 ms
  before, 44.072–47.157 ms after, unchanged 25,804,800-byte image buffers. Not a Vulkan renderer performance claim.
- Evidence and reproduction: `Exhibits/VehiclePhysics/Continuation.md`; actual before/after, Moon and flare captures.
- Open: full native continuum/XPBD parity, measured tyre/lens/sky validation, GPU/editor integration, Windows,
  unresolved solar-disc PSF, Moon parallax/scene illumination, cloud appearance and lunar-map licence confirmation.
- This entry supersedes earlier statements that the owner's demo had no flare, Moon texture or XPBD sources.

## Seed-bank correlation and Windows test binary — 2026-10-28

- Statistical mirror of combined ReSTIR seed-bank reuse found the original fresh-draw banking biased −25.6%
  (z −46) when combined with temporal reuse; temporal-only and bank-only are clean. Shipped fix: bank the
  generator stream's next unused draw and enter only the newest banked seed while temporal reuse is active
  (stationary bias +0.43%, z +0.84; dynamic +1.23%, z +0.68). Host CTests 6/6 release + ASan/UBSan; five
  Slang entry points compile to SPIR-V; the GPU combined check remains unrun (no Vulkan driver in sandbox)
  with its expectation updated. Evidence: `Exhibits/LightCompute/ReplaySeedCorrelation.md`.
- GitHub Actions is disabled on this repository (workflows API returns none; the agent token cannot enable
  it), so no `WindowsWorkspaces` run could be dispatched. A Windows x64 test zip was cross-compiled locally
  with the pinned Zig toolchain instead (root Frontier host + TextureWorkspace, unsigned, not
  Defender-scanned) and delivered to the user outside Git. Enabling Actions in repo settings remains open.

## Visual proofs for environment scene — 2026-10-28

- GitHub Actions re-checked: still no workflows registered (`.github/workflows` absent from `main`,
  agent token cannot read/enable Actions). PR #4 opened from this branch; merging it registers the
  Windows workflow on the default branch. Actions still needs enabling in repo Settings.
- Environment exhibit now emits small visual proofs in seconds: 2x2 comparison strip and a 16-frame
  wind-drift GIF (256x160, ~7 s total). Release + ASan validation pass with identical frame hashes.
- ReSTIR continuation: cross-pixel (spatial) reservoir correlation now also measured in the mirror harness
  (six-pixel strip, drifting integrand). Spatial-only and temporal+spatial+bank both unbiased at every pixel
  (worst |z| 1.82). LightCompute 6/6 release + ASan/UBSan.
- ReSTIR continuation: multi-candidate generation (Sampling.y = 2, 4) verified in the mirror for
  bank-only and temporal+bank modes (|z| <= 0.91); an off-by-one bank-skip negative control fails in
  the temporal modes (-9.9% / -4.7%), confirming the harness detects the correlation it guards against.
  LightCompute 6/6 release + ASan/UBSan. GitHub Actions: user enabled "Allow all actions" in repo
  Settings; PR #4 merge into main is the remaining step before WindowsWorkspaces can be dispatched.

## Viewer redesign track — 2026-11-16

- Imported commit `1fef98c8` of `SultanAladin/Frontier` (`arena/19265b6e-frontier`) unchanged: Component Viewer,
  Asset Viewer, Geometry Compiler and their shared folders under `Experimental/`. Checks 20/20; three jsdom smokes 0 errors.
- Standing owner rules, repository registry and the pending engine-vocabulary rename table:
  `Documentation/Rules/ViewerRedesignBrief.md`.
- Prototype 01 Tier Descent (standalone infographic card, real cluster tiers) awaiting approval;
  evidence in `Exhibits/ViewerRedesign/`.
- Cabin HMI track (2026-11-17): three 3D centre-display prototypes on the ControlVehicle in `Experimental/CabinHmi/`
  (A Contour off-road, B Afterglow ambient, C Nocturne night lake). Owner liked Afterglow; Contour recoloured purple/green;
  Lucent rejected and replaced by Nocturne. Clusters moved off the HMIs onto separate dashboard pages (`Clusters/`: Ridge / Halo / Tide), each with
  Cruise/Eco/Sport/Race modes and a fuel gauge, linked to their HMI over `CabinLink`. Each HMI keeps its own Lights menu. Nocturne Comfort =
  seat slide + back angle (ring dot) + A/C; Nocturne Drive replaced by the Power Current hybrid flow view. Names unapproved; evidence in `Exhibits/CabinHmi/`; launcher via `ServeCabinHmi.py`.
  Visor (`CabinHmi/Visor/`): separate windscreen HUD game demo, a cockpit drive on a night pass with porcupine, signal countdown, teammate ping,
  fuel / overheat / O₂ warnings and a station suggestion with route chevrons. Name unapproved.
  Visor follow-up: synthesised sound (SoundDesk), real bicycle-model steering with lane keeper, lane guides that follow the lane the car is in,
  middle-mouse ping wheel (PingWheel, 8 calls) with teammate acks, same-direction + oncoming traffic (MakeTraffic) with ACC, emergency braking, overtake advice, collisions.
  Lobby (`CabinHmi/Lobby/`): procedural garage configurator around the ControlVehicle. 9 seeded sets (Canopy Grove, Moss Stage, Lightbox, Race Hangar,
  Monolith, Shadow Canopy, Pit Box, Photo Studio, Workshop) with per-set options, grouped car options (paint, wheels/tyres, light colours,
  underglow, bolt-on components, drivetrain), white hotspot dots with close-up camera flights, see-through drivetrain cutaway with figures,
  orbit camera with presets, key-light dial + light plot. Names unapproved.
  Acoustics (`CabinHmi/Acoustics/`): synthesized vehicle sound library, no recordings. LaFerrari V12 + HY-KERS and Nissan GT-R NISMO
  twin-turbo V6 (whistle, spool, blow-off, flutter) from per-cylinder pulse trains with firing-sequence chatter (valve/injector ticks, jitter,
  gear clunks) through pipe waveguides, driven by a profile-aware DCT/limiter/boost/traction/ABS car model; 7 road surfaces, tyre squeal,
  disc brakes (squeal, groan, ABS) and quarter-car suspension (`ChassisVoices`), 5 light and 11 cabin cues, 7 impact cues (`ImpactFoley`:
  explosion, shockwave, EMP, sonic blast, window shatter, glass break, metal crash), weapons (`Ballistics`: Devotion-style energy LMG with
  spin-up and Turbocharger, LMG, SMG, sniper, turret, tank cannon, autocannon, hold-to-fire on the audio clock), bullet crack/whizz/ricochet
  and 8 impact materials. Follow-up 13: 10 swappable turbos/superchargers (`ForcedInduction`), electric cars (`ElectricDrive`: Porsche Taycan
  Turbo S, original Next-Gen racer in the style of Jackson Storm) and electric aircraft (`ElectricAviation`: trainer, racer, eVTOL).
  Follow-up 14: Energy LMG re-voiced as a real gun (boom, crack, geared drive, rolling tail, brass), spent shell casings (`Casings`: 8 case
  types on concrete/wood/steel/dirt, bounce physics, rolling, brass rain).
  Follow-up 15: Dodge Challenger SRT Demon (`Demon`: supercharged cross-plane V8 with uneven per-bank firing, stock 2.7 L blower,
  8-speed, TransBrake) and a turbo-lag shaft model (rpm/load-dependent spool, whistle follows the shaft, lag slider + stopwatch).
  Follow-up 16: Porsche 918 Spyder (`Spyder918`, flat-plane V8 + rear and front e-motors, `Motor.Front`) and Koenigsegg Agera R
  (`AgeraR`, twin-turbo flat-plane V8), launches and fly-bys.
  Follow-up 17: road noise switch (the loud layer was road/tyre roar, not wind; `RoadLevel`/`WindLevel`, Road 40 % default, M key,
  eased above 150 km/h), `FactoryFoley` (conveyor, metal works, creaks, sparks, electronics; 21 cues, hall reverb).
  Follow-up 18: three Mustangs (`MustangGt` Coyote cross-plane, `Gt350` Voodoo flat-plane, `MustangGt3` LMGT3) and the Ligier JS P320
  LMP3 (`LigierJsP320`, Nissan VK56), opt-in rotating mass (`Drive.Rotating`), 7 reel scenes.
  Follow-up 19: 8 more guns (`Fifty` .50 cal, `Ak47`, `M4`, `Minigun`, `Mg42`, `Shotgun` pump, `Pistol`, `AntiMateriel`) and `WarFoley`
  (12 cues: sand explosions, mortar, grenade, RPG, barrage, helicopter, siren, tank, shell shock, battlefield; Doppler `Flight`), 20 reel scenes.
  Follow-up 20: weapons, explosions, shells, turbos, superchargers, Taycan and racer redone (`NarrowTone`, `PolySaw`, `Detonation.js`
  with charge-scaled blasts and generated place reverbs, per-gun layer recipes, rewritten `Casings`, `Compressor`/`BlowOff`/`Surge`), 64 scenes re-rendered.
  Follow-up 21: ricochets (tumbling fragment with drag and flight-path Doppler), bullets into metal (dense `PlateModes` ring) and glass (crack, chink, shards landing).
  Follow-up 23: `Filament` particle showroom (separate demo): Light speed, Digital twin, Airflow, Particular, Clone field, Charging; capsule
  particles (`FilamentLooks`), `SurfaceScatter`, `BloomChain`. 10 captures, 0 console errors.
  Follow-up 24: Filament looks from reference stills: Orbits (glass sphere), Flora (particle flowers, bokeh), Display reveal, Fibre wave;
  depth of field, thread segments, PCG hash, backdrop dome (`FilamentFibres`). 15 captures, 0 console errors.
  Follow-up 25: Display reveal replaced by Vehicle transition (`Transition`, `SweepJag`): an electric edge sweeps the real car (dust → paint,
  graphite → lime, paint → particles), car shader cut and recoloured at the edge; straight evenly spaced lanes. Flora and Fibre wave made
  cleaner (even vein, rib and fibre spacing). 16 captures, 0 console errors.
  Follow-up 26: Orbits rings evenly spaced in one shared plane (golden-angle heads, radius-graded glow, fewer sparkles).
  Follow-up 27: Filament particle editor (`FilamentEditor`) in the Frontier Editor layout, reusing `FrontierEditor.css`, `EditorControls`, `EditorIcons`:
  the ten looks as stackable layers, layer / camera / car / backdrop / bloom / time / recipe inspector pages, JSON recipes. Renderer split into
  `FilamentRig` (showroom captures byte-identical). 7 editor captures + 16 showroom captures, 0 console errors.
  Follow-up 28: the editor became a standalone web app, `Experimental/ParticleEditor/`, with its own `index.html`, server
  (`ServeParticleEditor.py`, "/" opens the editor), UI copies, fonts, particle engine (moved from CabinHmi/Filament), car facets
  (`VehicleFacets`) and three.js. It loads nothing outside its folder; the CabinHmi showroom imports the engine from it. Launcher card
  removed. Checks in `Exhibits/ParticleEditor/`: 7 captures (identical to before), 0 console errors, 0 out-of-folder requests.
  Follow-up 29: Project Zero editor UI copied from `unassignedinbox/Frontier` (`arena/ad7fa01c-frontier`, UI-only build) into
  `Experimental/ProjectZeroEditor/`; served with `Serve.py`.
  Follow-up 30: Showcase primitive inspector (`PrimitiveInspector.jsx`, `PrimitiveIcons.jsx`, `PrimitiveInspector.css`): square vivid quick
  controls, then Transform, then an Overview / Physics / Geometry / Material / Rendering / Fracture carousel styled after the dashboard
  references. Local rebuild made to work (`package.json`, `Experimental/FrontierEditor/` deps, `Build.mjs` asset fallback).
  `CheckInspectorOrder` updated to the new order: 71 checks pass. `CheckQuickTools` fails only on the blocked Fontshare requests. Names unapproved.
  Follow-up 31: removed the generic "Live parameter map" bar charts (`PropertyGraph`) everywhere. Primitive inspector reworked:
  Overview first, with quick controls at its bottom as dark tiles with green / grey round badges. Transform moved to the top of Geometry.
  Purpose and Mass cards are dark. Checks updated: `CheckInspectorOrder` 71 pass, `CheckEnvironmentGraphs` passes.
  Follow-up 32: Cameras now hold Motion Blur (new) and Lens Flare (moved from Sun). Post Process has 7 effect children (Heat Shimmer, Glitch,
  Chromatic Aberration, Film Grain, Lens Distortion, Scanlines, Sharpen), each with its own icon (`PostEffects.jsx`, `PostEffectIcons.jsx`,
  `PostEffects.css`). Each effect inspector has a split-view preview that changes as the values change. Old saves are rearranged once on load.
  The Wind inspector's Composite flow card was removed; the WindEditor opens from Wind controls. Checks updated; InspectorOrder,
  EnvironmentGraphs, Wind, EntityContext, WorkspaceCards, LightDesign and SharedCards pass. CheckConstruction (Cube roughness) and
  CheckReference (hash) fail exactly as at HEAD. Names unapproved.
  Follow-up 36: socket inspectors rebuilt as live study (Component Viewer `stepSocket` driven by a looping test drive, 6 s trace,
  drag-to-pose, dock click / pull-test events) → Socket type (drawn glyphs) → per-kind range card (range strip, sliders, derived
  figures) → Mount (side-view marker; position feeds spring dive / squat; outliner summary names the spot). New
  `CheckVehicleSockets.mjs` (8 checks) passes; InspectorOrder (109) and EntityContext pass; Construction still fails at its pre-existing
  step. Names unapproved.
  Follow-up 37: per-camera **Render settings** (ported from unassignedinbox/Frontier `arena/28ada475-frontier`: engine, master quality,
  shadows, GI, reflections, exposure / resolution / edge AA) as a Quality / Lighting / Output section with its own icon on every camera,
  stored on that camera only. Primitives gain **UV preview** and **Fracture preview** cards that open the full workspaces, and two new
  workspaces: **SDF Bake** (Geometry Compiler `SparseSdfBaker`, slice view, probe, `.fvol` export) and **LOD Clusters** (`ClusterHierarchy`
  DAG, distance-driven cut, OBJ export). New `CheckRenderSettings.mjs` (6) and `CheckVolumeWorkspaces.mjs` (4) pass; InspectorOrder,
  SharedCards, InspectorLayout, EntityContext, InstrumentStyle, FogCards, LightDesign, VehicleSockets, WorkspaceCards pass. Browser draft;
  no renderer connected. Names unapproved.
  Follow-up 38: socket inspectors made static: the animated test drive, 6 s trace and vehicle side view are gone. Cards are now the
  joint at its pose (drag to set it) → Socket type → range card → **Attached to** (the asset's name as a link that selects it, plus an
  Attach to picker, offset and axis). `CheckVehicleSockets.mjs` rewritten (8 checks) and passes; InspectorOrder (109), EntityContext,
  SharedCards and InspectorLayout pass. Names unapproved.
  Follow-up 41: render settings became camera components: Render Engine, MatCap, Shadow Maps, SDF Tracing and Ray Tracing rows under
  every camera (`RenderComponents.jsx`, `RenderStudies.jsx`, four new icons; `RenderSettingsCards.jsx` removed). Each has its own card
  design, plus an in-use strip for the current engine. MatCap gains six editor-only fields. `ComponentRevision` 2 migrates saved scenes.
  RenderSettings (9), InspectorOrder (157), EntityContext (78), ObjectComponents, SharedCards, InspectorLayout, VolumeWorkspaces, FogCamera
  and InstrumentStyle (passes on rerun) pass; Construction:190 and QuickTools:296 also fail on the base commit. Names unapproved.
  Follow-up 42: each camera has one Render Engine component with Engine / PBR / SDF / Ray traced / MatCap tabs. Every renderer has its own
  shadow algorithm and settings (`Pbr*`, `Sdf*`, `Ray*`, `Matcap*` keys; old shared keys are upgraded on read; MatCap gains a studio
  shadow). The engine card is decluttered; Quality and Output are kept. `ComponentRevision` 3 drops the four retired rows, and their icons
  are deleted. Rigid Body is redesigned (body tiles, slope/bounce contact studies, damping half-lives). Mass has a new icon and can be set
  directly in g / kg / t / lb / oz. RenderSettings (9), EntityContext (70), InspectorOrder (141), ObjectComponents, SharedCards,
  InspectorLayout, VolumeWorkspaces, FogCamera, Browser, WorkspaceCards and Wind pass. InstrumentStyle:118 is flaky (it also fails on
  the base commit and passes on rerun). Construction:190 and QuickTools:296 are pre-existing failures. Names unapproved.
  Follow-up 45: the Follow-up 43 per-card Sun/Atmosphere studies are reverted. Sun now (`SunNow.jsx`) is a lit set (block, post, ball)
  whose light, sky and soft shadows follow the sun; drag it or the day track to scrub Local Hours. Six figures sit on hairlines. Sky colour
  (`SkyDome.jsx`) is a draggable all-sky fisheye from a single-scatter estimate of the Atmosphere settings, with four colour readings and a
  sky-at-every-sun-height strip. EnvironmentGraphs, InspectorOrder, LightDesign and SharedCards pass. InspectorLayout:115 (Wind canvas)
  fails the same way on the base commit. Names unapproved.
  Follow-up 46: `Experimental/FluidEditor/` added: c7egoist Fluid Studio (imported unchanged as `26152b6`) combined with the other Fluid
  Editor's strongest algorithms: warm-start pressure, combustion gas expansion, energy-conserving light sum, two-lobe phase, octave
  multiple scattering, geometric shadow steps, a fire-light field diffused in the pressure loop, sky occlusion, floor shadows, stable
  jitter; plus a MacCormack limiter fix and an Infinite Grid texture-unit fix. FluidValidation 349 pass; inner divergence after 10 steps
  67% → 44%. Evidence in `Exhibits/FluidEditor/`. SwiftShader only. Names unapproved.
  Follow-up 47: fluid scenes no longer start clean and then churn into noise.
  - Cause: steady forces kept accelerating gas without limit, and MacCormack velocity advection added energy every step by clamping its
    overshoots.
  - Fix: a Domain *Force response* limit, and MacCormack now falls back to semi-Lagrangian where it overshoots.
  - At 10 s, mean speed is down from 3.7–17.7 to 1.0–3.3 (`Exhibits/FluidEditor/StabilityResults.txt`, 28 runs).
  - The other editor's 18 presets were translated in `Engine/PyroScenePresets.js`, shown as "Pyro scenes" (36 presets total). Since the
    layer stack phase 2 they are `Presets/*.toml` documents; `PyroScenePresets.js` is kept unloaded as a 📝 reference of the original values.
  - Flipbook drawer: sprite-sheet PNG plus timing/UV JSON, with an optional transparent background.
  - FluidValidation: 496 checks pass. SwiftShader only. Names unapproved.
  Follow-up 40: Vehicle Movement charts are editable (suspension peak → damping/frequency, 18 torque points plus idle/redline,
  CdA/ClA handles, clamped to Blueprint ranges). Primitive carousel is Overview/Geometry/Material/UV/Fracture/SDF/LOD with UV, SDF and
  LOD preview cards on their own pages. Showcase objects carry Mass, Rigid Body and Collision component rows (`ObjectComponents.jsx`;
  values on the owner, `ComponentRevision` migration, collision-shape card with an SDF option); the Physics page is gone. New
  `CheckObjectComponents.mjs` (7) passes; InspectorOrder (137), EntityContext (68), VolumeWorkspaces, InstrumentStyle, SharedCards,
  VehicleSockets and RenderSettings pass. Materials:62, QuickTools:296 and Construction:190 also fail on the base commit. Names unapproved.
  Follow-up 39: Atmosphere sky presets removed (tiles, preset names, `AtmospherePresets`); a plain **Sky colour** card keeps the
  noon / sunset preview from the current settings. **Bumper Trigger** removed from the Vehicle folder (saved scenes drop it on load).
  Vehicle Movement and Chassis Collision no longer draw the car: Vehicle Movement is figures, fields and readouts (plus its data
  plots). Checks updated: EnvironmentGraphs, InspectorOrder (107), EntityContext (53 rows), Construction counts (48 / Vehicle 8);
  VehicleSockets, SharedCards, InspectorLayout, InstrumentStyle and WorkspaceCards pass; Construction still fails at its
  pre-existing step (:190). Names unapproved.
  Follow-up 35: in-place UV and Fracture edit workspaces for showcase primitives (`Workspaces.jsx`, `WorkspaceGeometry.js`; the viewport
  isolates the object, the inspector switches workspaces, Esc/Leave/Done exit), replacing the pop-out fracture window; Vehicle outliner
  folder (`VehicleComponents.jsx`: Vehicle Movement, Body, three sockets, two collisions, two lights, built from ComponentViewer
  Blueprint/Sockets, one-time `VehicleRevision` migration, Vehicle construct category); IES curve editor with library and LM-63 import /
  export (`IesProfile.js`); light Preview card merges readings and source sliders; rounded diagnostics icons, camera Sensor formats
  (`SensorFormats.jsx`), Mass card. Checks updated: InspectorOrder (109), SharedCards (14), LightDesign (22), EntityContext (54 rows),
  Construction (49 entities; still fails at :188 as before), Lighting (in-place workspace; still needs the missing FractureEditor file).
  InstrumentStyle, InspectorLayout, WorkspaceCards, FogCards, EnvironmentGraphs, Wind, Browser pass. Reference:21, QuickTools and Assets
  (stale "Content origin" control) fail as before. Names unapproved.
  Follow-up 34: Sun hero replaced by a "Sun now" card (`SunNow.jsx`: horizon panorama with the day's path, six lighting figures);
  Atmosphere atlas replaced by Sky preset tiles (noon / sunset gradients) plus the Rayleigh / Mie pill sliders; Height Fog (Fog layer,
  Density by altitude, Light in the fog) and Atmospheric Fog (Aerial perspective, Spectral transmission) get their own cards
  (`FogCards.jsx`), while Local Fog keeps the shared ones; Glitch study redrawn as signal trace plus test-frame break-up; primitive Overview
  now runs Quick controls → Notes, with Purpose inside Notes; primitive Geometry rebuilt with the editor's property cards and pill
  sliders (Shape wireframe, Resolution, Shading, Normals). Checks updated: InspectorOrder, InspectorLayout, EnvironmentGraphs,
  InstrumentStyle, LightDesign, FogCards (now passing), Reference, Lighting. Construction:188, Reference:21 and QuickTools (fonts) fail as
  before. Names unapproved.
  Follow-up 33: primitive inspector: Rendering tab, collision shape, origin and UV removed; quick tiles show state by icon only; Physics
  (Simulation drop study, Mass log ruler) and Geometry (Shape, Shading cross-section, Normals) rebuilt as camera-style cards. Post-process
  canvas preview replaced by one draggable infographic per effect. Environment Light added to Lighting (`EnvironmentLight.jsx`, one-time
  migration); Rim Point / Fill Point / LED Emitter icons (`LightGlyphs.js`); light inspector re-themed to the main colours and rounding.
  Wind binding removed from clouds, local cloud and fogs. Checks updated: InspectorOrder, EntityContext, Wind, InspectorLayout,
  WorkspaceCards, EnvironmentGraphs, InstrumentStyle, LightDesign and SharedCards pass. Construction:188, Reference:21, QuickTools
  (fonts) and FogCards (Colour label) fail exactly as at f8746f3. Names unapproved.
  Follow-up 22: Agera RS (`AgeraRs`, cross-plane 90° crank), rubber tyre squeal per surface (`Singer`, `Concrete` surface, `Tyres-Drift` scene), ricochet warble, subsonic whizz flown past the ear.
  Live AudioWorklet bench with car switch + offline MP3 reel (101 files, 26 MB) with spectrogram plates (`Exhibits/CabinHmi/Audio/`). Names unapproved.

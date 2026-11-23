# Slate cockpit/smoke and browser Jolt integration

## Reference and provenance

Requested source: https://github.com/unassignedinbox/Slate/commit/3c987f33c71c2477bd73ca29c69d5bd0ee484d1a,
`Experimental/IlluminationHost`.

- `CabinLayout.js` → `Source/SlateCabinLayout.js`: reference placements, dimensions,
  camera eyes and dashboard/seat geometry; imported by the host's cabin adapter.
- `CabinProjection.js` → `Source/SlateCabinFittings.js`: grip/spokes, hexagonal
  cluster shell and tablet shell geometry, remapped to host material families.
- `Source/PlumeSpace.js`, `Source/PlumeSourceProjection.js` and
  `Shader/Plume{Records,Advection,Pressure,Vorticity,March}.wgsl`: copied from the
  reference. The five shaders remain byte-identical. Host-side telemetry/preset
  adaptation lives in `SmokeSequence.js`; composition is integrated into the
  existing display pipeline before TAA.
- The host's tablet applications, real-car material updates, glass and mirrors are
  retained. The hexagonal screen is mounted on and rotates with the wheel.
- New WebGPU scan and CPU-ray-tested material dragging implement the requested HMI
  functionality natively. This is not an embedded Three.js configurator or its exact
  scan/ripple transition. Paint drops target body-coat facets; rim drops target hub
  facets. Click controls remain available. Material drops only target the Garage
  preview, then update the shared driven-car materials—not arbitrary scene objects.

## Jolt is the browser backend—not native C++

Pinned dependency: `jolt-physics@1.1.0`, upstream https://github.com/jrouwe/JoltPhysics.js.
Its npm package includes Jolt's MIT licence. Install with `npm ci` in this directory;
`node_modules` is intentionally not committed. The local `wasm-compat` JavaScript
entry point embeds its WASM payload, so there is no CDN, C++ compiler, native server,
SharedArrayBuffer or cross-origin-isolation requirement. Serve the whole prototype
folder, including installed `node_modules`.

`JoltWorld.js` owns a single-threaded Jolt world, fixed 120 Hz physics (maximum twelve
steps per frame), convex hull bodies, mass/inertia, friction/restitution, sleeping,
CCD, static ground/kerbs/ramp/collider boxes, obstacle rebuilds, resets and cleanup.
It synchronizes real Jolt poses back to the existing lighting/fracture/cone systems.
The contact metric reports Jolt added/persisted manifold callbacks during the frame,
not invented zeroes or a unique-point count.

**Scope:** custom vehicle drivetrain, suspension, MF tyres, thermals and terrain
contact solving remain. A dynamic Jolt chassis proxy exchanges collision-induced
linear/angular velocity with that solver for loose-body collisions. It does not
apply Jolt gravity/ground contacts or copy Jolt chassis position back, avoiding
second chassis integration. `RigidBodyWorld` is inherited for body metadata,
convex planes, tyre-support queries and support-load bookkeeping; its JavaScript
contact integrator is not used for the Jolt runtime. This is a hybrid vehicle
integration, not a claim that the entire vehicle is Jolt's VehicleConstraint.

## HUD sizing correction

Cabin/HMI geometry and seated eyes stay at their authored physical scale. The old
cockpit scale/offset UI/query is removed. Cabin · HMI instead controls the diagnostic
HUD's CSS scale and x/y offset, anchored top-left. The touch plane remains aligned
with the fixed reference tablet.

## Verification

Run the commands in README.md after installing both prototype and Tools packages.
Tests cover reference wheel/tablet poses, cluster attachment, touch picking, fixed
physical cabin under HUD edits, ray/material drops, four inspection modes, tyre
emission direction/release, plume retirement, actual WASM body settling/sleep/reset,
fast wall collision and chassis collision feedback, plus the earlier VFX tests.

Dawn null-backend runs cover idle startup, active effects and active plume
source/transport/march/composition with the new cockpit camera. They validate WGSL,
GPU layouts and submitted commands, **not pixels or physical GPU performance**.
A real WebGPU-browser review of appearance, driving feel and pointer interaction
is still required; no browser screenshot/rendered visual review is claimed.

Recorded results for this integration: **20/20 tests pass**, all **23 stability
scenarios pass**, and Dawn startup **12 frames**, effects **36 frames** and active
plume/cockpit **48 frames** each complete with **0 errors / 0 warnings**.

## Follow-up: rim classification and usable HMI focus

The metal rim archive contains 1,296 incorrectly rubber-labelled facets and 272
hub-labelled facets. `ProjectWheelFacets` now remaps the rim archive's rubber labels
to Hub, without changing the separate 3,968 rubber tyre facets. Both mirrored sides
retain their complete metal rims after shredding. Default Hub shading is neutral alloy.

Tablet-only scaling uses a dedicated housing material and a shared GPU placement
function for deferred/forward/shadow and screen-overlay paths. The same centre and
scale drive CPU touch picking. Cluster, wheel and cockpit geometry do not scale.
HMI focus uses the scaled screen's plane, aspect and up axis to frame it with padding;
M toggles focus/cockpit, and scrolling adjusts focus distance or cockpit FOV. Pointer
cancellation and touch-action handling protect touchscreen interaction.

The existing baked distance field/surface-card geometry remains authored-size (as
with the animated wheel); the tablet sizing control changes raster/screen/touch
placement, not the underlying GI bake. Large scales may overlap nearby cabin fittings.

New regression tests cover both actual rim archives, tablet-scale touch alignment,
record encoding, portrait/landscape framing and the bounded spark footprint.
**24/24 tests pass**; the 48-frame active integration also exercises scaled HMI and
cockpit cameras. This remains command/shader validation, not rendered visual review.

## Follow-up: display colour, material dragging and blue rims

Restored the authored blue metallic/emissive rim finish as the default, keeping the
corrected complete-rim geometry. Garage now offers a blue-glow rim chip as well as
alloy/dark rims. Paint, gloss/matte and rim finishes can all be dragged; the chip
follows the finger/pointer over the GPU preview, the turntable pauses during the
drag, and material-matched facet picking still gates application to the driven car.

The washed-out display came from treating the canvas UI as HDR emissive glass:
sky reflection could remain even at brightness zero, and the final scene exposure
and filmic curve changed authored colours. The antiglare screen now reconstructs
radiance from desired linear display colour using the inverse of the final ACES
fit and the exposure buffer. Window glass/mirror reflections are unchanged. The
panel and inspector brightness controls multiply once (0 means black); canvas
brightness is no longer baked into the cabin texture. Standalone canvas use keeps
its own brightness overlay. Screens no longer use the scene's emission multiplier.

27 tests pass, including brightness-zero, exposure/colour round trips and dragging
blue emissive rims/matte finishes. Effects (36 frames) and scaled-HMI/plume (48
frames) Dawn checks pass with no errors/warnings. Real-browser visual review is
still required.

## Correction: preserve the original two-material rim

Rechecked Project Drive on `SultanAladin/Frontier-`, branch
`arena/4f07c82a-frontier`: `EngineContent/GeometryArchives/ControlWheelStructure.inl`
explicitly records **MatTyre: 1296, MatHub: 272**. `DriveSceneAuthor.cpp` gives the
first slot its authored dark finish (linear 0.03741, roughness 0.76087) and calls
MatHub “Material.024 — rim mag accent”, a blue tinted metal with 0.12 emissive lift.

The earlier interpretation of those 1,296 labels as an authoring mistake was wrong;
they are intentional dark faces, not additional blue faces. The runtime now maps
ONLY those metal-archive MatTyre faces to a dedicated `RimBase` material carrying
that same dark finish. The 272 marked blue faces stay Hub, triangle-for-triangle.
Actual tyre rubber remains Tyre. Thus both rim slots survive shredding without
turning the entire rim blue. Garage finish changes only affect the accent slot;
drops are accepted on either rim slot. Both mirrored wheels are regression-tested
against each original triangle's material assignment.

Validation: 31 tests pass; populated effects/shredded-wheel Dawn validation completes
36 frames with no errors/warnings. No new rendered browser review is claimed.

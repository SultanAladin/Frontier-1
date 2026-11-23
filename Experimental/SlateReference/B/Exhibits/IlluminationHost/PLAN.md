# Vehicle physics and effects — implementation status

All six scoped changes are implemented in the JavaScript/WebGPU prototype.

1. **Visual tyre deformation removed.** Deleted the 96-node visual ring solver and its WGSL deformation code. A 144-byte rigid visual record now carries cabin articulation and wheel damage flags. Removed deformation/exaggeration controls and obsolete patch/wave readouts. Shredded rubber is hidden in world, sun shadows, mirrors and Garage; steering/spin/suspension and pressure/contact/wear/puncture physics remain active.
2. **Sparks and derez illuminate surroundings.** Eight pooled light slots aggregate at most four spatial spark clusters plus derez/rez emitter proxies. Added range falloff, smooth cooling/age decay, per-cluster and whole-pool energy limits, and distance-field occlusion. These feed screen direct and card direct lighting, so the card GI path receives them too. Mirrors use a cheaper unshadowed approximation; Garage studio lighting remains independent.
3. **World spark collisions.** Swept segments hit the analytic course (including ramp/kerbs), static world collider boxes and current convex debris hulls at up to 120 Hz. Thin static boxes cannot be skipped by endpoint tunnelling. Broadphase candidates refresh each frame. Bounces lose normal/tangential speed and heat; cold/settled sparks retire. Disabled sparks clear immediately.
4. **Spark colour.** Initial temperatures vary from 1900–3000 K. Linear blackbody radiance, size-dependent monotonic cooling and a low-temperature fade give pale-hot → orange → red/dim progression without an arbitrary age-based colour switch. Reduced HDR energy addresses overbright clipping; actual browser exposure review remains necessary.
5. **Rim marks instead of shredded-tyre skids.** Independent RGBA channels store rubber and rim deposits/lateral coordinates. Shredded contacts stamp two narrow flange scratches using the new abraded-aggregate/metal-transfer material; rubber marks retain tread grooves. Contact-kind changes break the trail. Debris-supported, no-contact and disabled marks are suppressed. Existing actual rim-contact events still drive sparks.
6. **Optional countersteer assist.** Vehicle → Telemetry exposes on/off and strength (default 0.45). Slip/yaw feedback is speed-aware, rate/angle limited, fades in between 3 and 12 m/s, yields to manual lock and switches off in reverse/airborne. Scripted commands bypass the driver assist. Keyboard steering still self-centres when released.

## Validation

From `Tools/`:

- `npm test`: material/control/effects/Jolt/Slate integration tests, including thin-wall/ramp/debris sweeps, spark cooling/retirement, rim-only deposits and transitions, rigid visual records/shared shader paths, bounded/expired light pools, assist symmetry/limits and grounded left/right slide recovery.
- `npm run test:stability`: existing rest/flat/blowout/shred/slalom/donut/reverse scenarios, updated to remove the deleted visual ring solver.
- `npm run validate`: Dawn null-backend shader/pipeline/frame validation.
- `npm run validate:effects`: 36-frame integration hook with populated spark/derez lights, shredded wheels, real RGBA mark uploads and all Garage modes.

## Deliberate approximations / remaining review

- Proxy lights approximate the bulk of sparks/derez, not every glowing cube. Card/temporal lighting uses its existing refresh cadence, so indirect illumination may lag a brief flash.
- Particle collisions use the same analytic course and collision boxes as the physics, not a triangle BVH of arbitrary render-only geometry. Dynamic debris is tested at its current pose; there is no relative-motion continuous collision against a rapidly moving hull, and sparks do not transfer impulses to it.
- Rigid tyre visuals deliberately no longer squash at low pressure, though physical corner heights still change.
- Dawn's null backend validates commands and WGSL but does not render pixels. Exposure/colour, nearby lighting, scrape appearance and steering feel need a real WebGPU-browser review and hardware performance measurement.

## Physics engine

Loose rigid bodies, debris, cones and chassis-vs-loose collision impulses now use
**Jolt Physics 1.1.0 JavaScript/WebAssembly**, loaded locally from the pinned npm
package. The custom `VehicleSolver`, `TyreSlipSolver` and `TyreThermalSolver` remain
responsible for drivetrain, suspension, tyre forces and car-vs-terrain contacts;
`RigidBodyWorld` supplies metadata/support helpers, not the active loose-body solver.
See [SLATE_PORT.md](SLATE_PORT.md) for the precise browser integration, provenance and
new cockpit/HUD/HMI/smoke validation.


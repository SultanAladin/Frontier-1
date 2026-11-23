# Plume transport: Engine footprints and GPU reference checks

## Ownership and scope

`Frontier/Engine/Rendering/PlumeLatticeFootprint.{h,cpp}` now defines the demo-compatible 256-byte lattice
uniform and 48-byte source footprints, with explicit field-offset assertions and host-side validation. It is
a C++17 Engine library, `FrontierPlumeLatticeFootprint`; it does **not** allocate textures, run the plume,
construct an obstruction field, or submit GPU commands. The exhibit validates its bytes and uses them to
feed the retained demo WGSL. The simulation shaders themselves remain in
`Experimental/VehicleHmiReference`, not in the production renderer.

Three previously missing reference dependencies—`SceneRecords.wgsl`, `DistanceRecords.wgsl`, and
`DistanceTraversal.wgsl`—were restored byte-for-byte from `SultanAladin/Frontier` commit
`6afb6f00b5f25c45f7cda92c7da65b64845768f3`; their blob identities are in `SourceManifest.json`. The
exhibit expands the real includes, adds a compile-time `MaximumTraversalSteps` constant required by the
included traversal (the plume checks do not invoke the ray-traversal functions), and executes the actual
retained advection, vorticity, pressure and sunlight-transmittance WGSL through wgpu/Vulkan. Its SDF test
uses a hand-filled distance texture and one hand-authored moving-occupant footprint; this is not the scene's
full distance-field construction or dynamic geometry integration.

## Footprint contract

`ValidatePlumeLatticeFootprint` checks finite fields bounded to ±1,000,000, coarse dimensions 1–256,
exactly doubled fine dimensions, finite XY window origins within ±2²⁸, fixed zero Z origin, source count
within the supplied capacity and the demo's 24-source maximum, paired cell sizes, timestep in
`[1e-6, 1/30] s`, normalized Sun direction, non-reversed active-source bounds, valid retention/albedo and
frame-duration fields. `ValidatePlumeSourceFootprints` checks up to 24 finite source footprints, radii in
`[1e-4, 1000] m`, and nonnegative source rates. These are explicit device-input bounds, **not** calibrated
physical ranges. The caller must separately validate actual buffer sizes, device limits, source allocation,
texture formats, synchronization and numerical stability for its chosen workload.

Four **unchanged JavaScript** `PlumeSpace.EncodeRecord` quality-grade records round-trip byte-for-byte
through the Engine footprint validator; independent field checks confirm extents, spacing, ground height
and step offsets. Their combined SHA-256 is
`cd0155ded41f3552e8aee506574f9a892a5283f88a5a8618940513314382fd73`. Seventeen invalid native cases and
valid/empty cases pass in release and ASan/UBSan builds. Seven Engine-validated lattice inputs and one
Engine-validated source were subsequently uploaded to the shader fixtures.

## GPU execution and independent expectations

`RunPlumeTransport.sh` and `SANITIZE=1 RunPlumeTransport.sh` both exited zero on **SwiftShader CPU Vulkan**
through wgpu `0.32.0`, not on a physical GPU. The sanitizer build instruments the native footprint validator;
Python, wgpu, shaders and the driver are not instrumented. The fixture uses `rgba16float` 8³ velocity and
16³ thermo volumes, and passes texture/buffer data directly between dispatches.

| Fixture | Observed |
|---|---|
| Uniform buoyancy | 512 coarse cells produce the hand-calculated half-float `0.533203125 m/s` vertical velocity, max difference `0` |
| Dynamic solid | Sampled negative SDF and dynamic occupant moving 0.2 m over 0.1 s yield `(2,0,0,1)` inside and `(0,0,0,0)` outside; interior smoke is cleared |
| Gaussian source and combustion | Hand-calculated source centre/neighbour, momentum relaxation and combustion/retention probes match after half-float quantization |
| One transport step | 15 dispatches in one command buffer, in separate compute passes: velocity advection, curl, confinement, divergence, eight Jacobi sweeps, projection, thermo advection and light transmission. A 15 m/s field over `1/30 s` moves an interior impulse one fine cell, and an edge impulse exits rather than wrapping across the window. Coverage equals `[6,6,7]–[6,6,7]` |
| Vorticity | An analytic quadratic velocity produces the centred discrete curl and confinement direction; a flagged solid cell is unchanged |
| Sun/zenith attenuation | Uniform-density Beer-like reference over the demo's discrete sample lengths: max absolute transmission error `1.13998836e-4` |

The complete readbacks sampled by these fixtures have identical release/sanitized SHA-256
`254545aa7c2111cb6c1aa7239edbe5ebf61ae90e5e6c307403ccbf212f7b1c9c`.
[Release log](Results/PlumeTransport-release.txt) · [sanitized log](Results/PlumeTransport-sanitized.txt).
The unchanged JavaScript source/scheduling and idealized stencil tests also pass (`Results/PlumeReference.txt`).

## Important limitations

The chained transport case has **zero divergence and zero pressure**, so it does not show that the pressure
projection works for general flow. The independent nonzero-pressure regression remains unchanged: a
near-grid mode retains `0.980990` of the original centred-divergence norm after 256 Jacobi sweeps, despite
a small pressure-equation residual. The underlying centred-divergence/gradient versus nearest-neighbour
Laplacian mismatch is explained in [PlumePressure.md](PlumePressure.md); the retained transport path still
uses it. A later [separate matched face-flux projection](GasProjection.md) passes independent closed-domain
checks, but its storage/boundary semantics differ and it is **not integrated into this plume**.

This is a tiny, single-step analytic validation, not a finished fluid solver. The full demo scheduler, moving
window updates, extended time stability, multi-source interaction, nonzero-pressure chain, moving-scene SDF
construction, turbulence, volume march, fire-light/tyre telemetry, editor, rendering and physical-GPU paths
remain unintegrated or unverified. No liquid solver, real-world smoke calibration, accuracy for turbulent flow,
or GPU performance claim follows from these tests. Vehicle dynamics and Jolt remain untouched.

```sh
Exhibits/FluidReference/RunPlumeTransport.sh
SANITIZE=1 Exhibits/FluidReference/RunPlumeTransport.sh
bash Exhibits/FluidReference/Run.sh
bash Exhibits/FluidReference/RunPlumePressure.sh
```

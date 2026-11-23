# Gas and smoke reference audit

The requested gas/fluid simulation is **not yet fully ported or integrated**. Engine now owns checked lattice
and source footprints, while this exhibit executes the retained demo's advection, vorticity, pressure and light
WGSL on SwiftShader CPU Vulkan in small analytic fixtures. A separate [face-flux pressure prototype](GasProjection.md)
passes bounded closed-domain tests but is not integrated with the demo's cell-centred flow. A one-step reference
chain is tested, but there is no native production plume scheduler, renderer/editor integration, long-term
simulation or verified liquid solver.

## Source provenance and scope

The retained files are unchanged Git blobs from `SultanAladin/Frontier` at
`6afb6f00b5f25c45f7cda92c7da65b64845768f3`, under `Experimental/VehicleHmiPrototype/`. Their paths and blob
IDs are in [SourceManifest.json](../../Experimental/VehicleHmiReference/SourceManifest.json).
The five plume shaders, source projection and smoke adapter also match the local Slate B copy. The full demo's
`PlumeSpace.js` differs: it calls `SpatialGrid.js` for window recentering instead of using the older inline code.
Both exact full-demo files are now retained; the import closure for the JavaScript source tests is present.

| Component       | Present in the demo                        | Port status                |
| --------------- | ------------------------------------------ | -------------------------- |
| Smoke/fire      | Tyre sources, onset, telemetry and lights  | Reference-side tests only  |
| Gas lattice     | Fine thermo, coarse velocity; moving XY    | Checked Engine footprints; no production allocation/scheduler |
| Flow            | Advection, buoyancy, curl and pressure     | Retained WGSL executes in bounded SwiftShader tests; pressure mismatch remains |
| Appearance      | Volume march, depth, light and self-shadow | Sun/zenith transmission tested; march not wired to editor |
| Liquid fluid    | Not established by this plume reference    | Scope/reference needed     |

The upstream source comments mention an older Frontier CPU smoke solver. Those comments are provenance claims,
not evidence that the named files exist in this checkout or that this port is complete. Existing static fog and
cloud shading are not a time-evolving fluid simulation.

## Executed reference checks

`PlumeReferenceValidation.test.mjs` executes the **unchanged JavaScript** with Node and a fake upload queue.
It checks negative-coordinate grid wrapping and recentering; onset/release hysteresis; emitted smoke and rearward
momentum; burning seats and a fire light; transient source expiry and capacity; shredded-tyre suppression; and
residency/record scheduling. The fake queue records writes but does not run a GPU solver. All six checks pass.
These are implementation-contract checks, not measured vehicle or smoke data.

The later [transport investigation](PlumeTransport.md) executes an ordered one-step GPU reference chain,
including advection, vorticity, eight pressure sweeps, thermo and light. It also tests analytic source,
buoyancy and a sampled moving-solid SDF. Its zero-divergence chain does not resolve the pressure defect or
establish production integration, long-duration fluid stability, physical-GPU execution or real-world accuracy.

## Pressure-stencil caution

The retained shader computes centred velocity divergence and a centred pressure gradient, while its Jacobi
pressure solve uses the nearest-neighbour six-point 3D Laplacian. Those discrete operators do not form the same
`div(grad)` stencil. An **idealized periodic, one-dimensional mode audit** isolates this arithmetic from boundary
conditions and finite-iteration effects: even an exact solution of that nearest-neighbour Laplacian leaves a
centred-divergence ratio of `sin²(kh/2)` after projection.

| Periodic mode, 32 cells       | Remaining / initial centred-divergence norm |
| ----------------------------- | ------------------------------------------: |
| Long wavelength, mode 1       | `0.00960736`                                |
| Near-grid wavelength, mode 15 | `0.99039264`                                |

The centred derivative also reports zero for an alternating collocated velocity in that idealized periodic
interior. That particular audit is a double-precision model, not a bounded demo measurement. The separate
[actual pressure-shader experiment](PlumePressure.md) now confirms a similar high-frequency limitation in one
bounded 3D fixture. These results do not quantify complete plume error, but show why the port needs a deliberate
choice of compatible discretization and measured divergence/boundary tests rather than assuming that more Jacobi
sweeps alone guarantee a divergence-free field.

## Next verification steps

1. Select and implement the exact retained source-to-grid mapping and time/lattice scheduling in the native
   renderer; preserve moving-window and source-lifetime behavior.
2. Implement and test advection, pressure, velocity projection, buoyancy, vorticity and solid/open boundaries;
   compare against independent analytic fields and residual/convergence benchmarks. Run the other real shaders
   and the complete scheduled plume, not just pressure.
3. Add volume rendering, fire lights and tyre telemetry; validate moving blockers, grid relocation, fade and
   editor controls. Investigate unsupported legacy settings rather than silently claiming parity.
4. Test real Vulkan hardware and Windows, profile equal-quality costs, and compare against external measured
   flow/smoke data before making physical-fidelity claims.

```bash
bash Exhibits/FluidReference/Run.sh
```

[Recorded source hashes and Node checks](Results/PlumeReference.txt). Linux x86-64; Node version is in the log.

# Recovered full VehicleHMI reference

Selected **unchanged** source files from `SultanAladin/Frontier` at
`6afb6f00b5f25c45f7cda92c7da65b64845768f3`, branch `arena/e9bd7db5-frontier`, inspected 2026-10-27.
Original prefix: `Experimental/VehicleHmiPrototype/`. Per-file Git blob hashes are in `SourceManifest.json`.
`package.json` is local test plumbing, not an upstream file. This is not a complete runnable browser demo.

This reference differs materially from both local `SlateReference` copies:

| Recovered source | What it establishes |
|---|---|
| `Shader/CelestialSky.wgsl` | Soft-edged limb-darkened Sun, sampled lunar atlas, not just the old hard Sun step |
| `Shader/LensFlare.wgsl` | Independently mixable warm streak, blue anamorphic, burst, ghosts and halo |
| `Shader/Reservoir{Records,Temporal,Spatial}.wgsl` | Current demo's GI sample storage and temporal/spatial reuse |
| `Source/Plume*.js`, `Source/SmokeSequence.js`, `Shader/Plume*.wgsl` | Exact demo smoke/fire source; not a native port |
| `Source/CelestialAtlas.js` | Actual 2048 × 1024 six-body sRGB atlas; Luna JPEG retained in `EngineContent/CelestialMaps` |
| `Source/TyreContinuumElasticity.js` | 160-particle solid and curved-surface continuum deformation |
| `Source/TyreCarcassProjection.js` | 160-particle compliant-distance XPBD deformation, prescribed spring reaction force |
| `Source/VehicleSolver.js` | Full demo selects `solid` by default and supports four tyre modes |
| `Tools/VolumetricTyrePhysics.test.mjs` | Five original tests, runnable with the retained dependency closure |

Reproduce original reference tests:

```sh
node --test Experimental/VehicleHmiReference/Tools/VolumetricTyrePhysics.test.mjs
node Exhibits/VehiclePhysics/Code/ReferenceTyreAudit.mjs
```

The old project's `Frontier-` revision `338293863d7df4e585dec0c4acae45fe9c524ed5` has byte-identical
`VehicleSolver.js` and `TyreSlipSolver.js` to the local SlateReference copy. That verifies the older source selection,
not parity with this fuller demo. Its Moon generator is procedural and is **not** used for the restored lunar texture.

## Physics interpretation

The recovered XPBD source applies `Δλ = −C / (wA + wB + compliance / dt²)` once per constraint/substep.
It then blends toward an authored contact/bulge target by 0.75 and returns
`Fz = deflection × (25000 + 0.88 × pressure)`. There is no accumulated constraint multiplier in this source.
The continuum variants also return prescribed force laws rather than integrated continuum contact reactions.
Their test success is therefore deformation/behaviour evidence, **not** experimental tyre validation.

The existing C++ `TyreCarcassLattice` instead has 84 particles, accumulated multipliers and a gas-volume constraint.
It is a separately authored approximation, not this source's port. Its earlier validation remains useful but cannot
establish parity with this 160-particle implementation. No force solver is silently replaced in this continuation.

See `Exhibits/VehiclePhysics/Continuation.md` for numerical checks, changes, remaining work and limits.
See `Exhibits/FluidReference/README.md` for gas-source checks, a pressure-stencil caution and port scope.
The retained spatial GI shader rejects zero-weight donor reservoirs; an exact counterexample and the limited CPU
reference temporal implementation are documented in `Exhibits/LightCompute/StaticIndirect.md`.

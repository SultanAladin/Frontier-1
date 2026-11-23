# Matched face-flux gas-pressure projection

## Scope and warning

`Frontier/Engine/Rendering/Shaders/GasPressureProjection.slang` is a **separate experimental Engine compute
primitive**, not a replacement currently used by the retained plume. It addresses the demonstrated
stencil mismatch in a closed, static-solid setting by storing each velocity component on a cell's positive
face and matching the divergence, pressure Laplacian and pressure gradient. Its companion
`GasProjectionFootprint.h` checks a 32-byte parameter block. Nothing here allocates resources or schedules an
Engine/editor frame.

This representation is **not drop-in compatible** with the demo's cell-centred `rgba16float` velocity
texture or its advection, vorticity, toroidal moving window, open side/top boundaries and moving obstacles.
Those must be redesigned or coupled with a verified staggered-grid treatment before the new projection can
be used for that plume. The [original pressure regression](PlumePressure.md) still reproduces the defect;
the [old 15-dispatch transport](PlumeTransport.md) still uses the unchanged original WGSL. No physical GPU,
production scheduler, complete gas/fluid solver, real-time performance or measured-fluid validation is claimed.

## Discrete contract

A float4 storage entry holds positive X/Y/Z **face velocities** and a static-solid marker in `.w`. A face
between fluid cells is open; faces touching a solid or the domain exterior are closed with zero normal flux.
The pressure is cell-centred. Solid values are ignored by the operators and copied unchanged in the final
velocity output; a nonzero copied solid velocity is not a moving-wall boundary condition.

With grid spacing `h`, the operators at a fluid cell are

```
D(u)[i] = sum_axis((u_axis[i] - u_axis[i-e_axis]) / h)
G(p)_axis[i] = (p[i+e_axis] - p[i]) / h
```

Only fluid–fluid faces contribute. For each blocked neighbor, the Laplacian uses the centre pressure, the
closed-wall Neumann rule. Thus `D(G(p))` is exactly the same six-neighbour Laplacian used in the solver,
including at these boundaries. The projection subtracts `G(p)` after solving `D(G(p)) = D(u)`. A weighted
Jacobi step (`0 < weight < 1`, tests use `2/3`) suppresses the undamped checkerboard mode of ordinary Jacobi.
This is an iterative approximation, not an exact solve at a fixed small iteration count. Each disconnected
fluid region has an arbitrary constant-pressure gauge.

The Vulkan caller must supply distinct input/output velocity and pressure ping-pong buffers, correctly sized
storage and uniform buffers, finite initial values, device-compatible dispatch dimensions, barriers, lifetimes
and synchronization. It must also supply a velocity representation and wall model compatible with these
semantics. Shader entry points reject invalid uniform dimensions/weight and short referenced buffers; they do
not establish the correctness of the caller's scene, aliasing or fluid state. The host guard checks dimensions
`1–256`, spacing `1e-4–1e4`, `0 < weight < 1` and zero reserved fields; these bounds are safety contracts,
not calibrated physical conditions.

## Executed tests

Slang emitted SPIR-V, which `wgpu 0.32.0` imported through **Naga** and ran with **SwiftShader CPU Vulkan**.
This is not a direct raw-SPIR-V submission or a physical GPU run. Release and ASan/UBSan builds passed; only
the native footprint validator is sanitizer-instrumented, not Python, Naga, shaders or the driver.

| Fixture | Evidence |
|---|---|
| Analytic long-wave face flux | 16 cells, mode 1, 1,024 sweeps: remaining divergence norm `0.01250065`, analytical weighted-Jacobi prediction `0.01250311` |
| Analytic near-grid face flux | 32 cells, mode 31, 128 sweeps: remaining divergence norm `3.66e-8`; finite-float error dominates the ideal prediction `2.70e-33` |
| Independent matrix solve | 4³ grid, 56 fluid cells and 8 solid cells; NumPy assembled the fluid adjacency matrix and solved it with least squares. After 1,024 sweeps, GPU divergence ratio `8.64e-8`; max fluid-face velocity difference `1.68e-7` |
| Nullspaces and solid state | Two disconnected fluid regions have two pressure gauges and reduce divergence by `3.51e-8` relative; an all-solid field is preserved |
| Inputs and bounds | 14 host-invalid footprints rejected; rounded dispatch tail, short velocity/pressure buffers and invalid shader relaxation leave protected output unchanged |
| Existing plume regressions | Reference pressure and the original transport fixture still pass their **own** expected checks; the old near-grid pressure divergence remains `0.980990` of its initial norm |

The analytic tests use different representation, boundary conditions and iteration counts from the original
plume's 98%-remaining example; the values are **not** an equal-cost head-to-head speedup or evidence that the
existing plume is fixed. The matrix solve independently checks the bounded discrete problem, not real smoke.

Release and sanitized GPU readbacks have SHA-256
`062e600b267caa2150d5a91f2bf7e91e2f5ed0335f9f71dfab0ef53aaa67c721`.
[Release](Results/GasProjection-release.txt) · [sanitized](Results/GasProjection-sanitized.txt) ·
[original pressure regression](Results/GasProjectionOriginalPressureRegression.txt) ·
[transport release](Results/GasProjectionTransportRegression-release.txt) ·
[transport sanitized](Results/GasProjectionTransportRegression-sanitized.txt).

```bash
bash Exhibits/FluidReference/RunGasProjection.sh
SANITIZE=1 bash Exhibits/FluidReference/RunGasProjection.sh
```

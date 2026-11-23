# Executing the demo gas-pressure shader

The original VehicleHMI plume pressure code now runs in an isolated WebGPU/Vulkan validation. This is a
**reference audit, not a native gas port or a complete smoke/fluid simulation**. The retained
`PlumePressure.wgsl` is unchanged; the test expands its existing `PlumeRecords.wgsl` include and executes
`ResolveDivergence`, `RelaxPressure` and `ProjectVelocity`. `wgpu`/Naga translates WGSL for **SwiftShader CPU
Vulkan**; no physical GPU or editor integration was tested.

## Fixture and checks

- The synthetic coarse lattice has `32 × 8 × 8` cells, one-metre spacing, `rgba16float` velocity and `f32`
  divergence/pressure. A separate NumPy implementation computes the discrete stencils on the quantized input;
  it checks every readback, but it models the same equations rather than independent observed fluid motion.
- A single relaxation sweep of unit pressure checks six hand-computed ground, open-side/top, corner and
  solid-neighbour probes. A 64-cell solid block checks that its velocities are preserved. A shifted window
  from prior origin `(-8, 3, 0)` to `(-7, 3, 0)` checks toroidal indexing and clears the 64 newly exposed
  pressure texels in **both** ping-pong buffers while preserving the overlap.
- Separate unblocked sinusoidal `x`-velocity fields use modes 1 and 15 across 32 cells. After 256 Jacobi
  sweeps starting from zero, the original shader projects velocity and computes its divergence again. Norms
  below use the same interior `24 × 4 × 4` cells before/after. The pressure residual is the norm of the
  six-neighbour Laplacian minus initial divergence, divided by the initial-divergence norm in that region.
- An unoccluded `50 m/s` uniform field tests the shader's `40 m/s` speed ceiling. Input and output half-float
  quantization is part of this check; pressure readbacks remain single precision.

| Case                          | Sweeps | CPU/GPU max pressure error | CPU/GPU max velocity error |
| ----------------------------- | -----: | -------------------------: | -------------------------: |
| Boundary probes               | 1      | `5.96e-8`                  | `0`                        |
| Solid block                   | 64     | `1.79e-7`                  | `0`                        |
| Shifted window                | 48     | `2.09e-7`                  | `0`                        |
| Sinusoid, mode 1              | 256    | `1.79e-6`                  | `1.53e-5`                  |
| Sinusoid, mode 15             | 256    | `4.18e-7`                  | `2.45e-4`                  |
| Uniform-speed clamp           | 1      | `0`                        | `0`                        |

All per-cell divergence readbacks matched the CPU stencil within `1e-6`, including the second divergence after
projection. The readback max velocity difference is at half-float precision. This confirms implementation
agreement for these fixtures, not that the model is physically accurate.

| Sinusoidal fixture | Remaining / initial centred-divergence norm | Relative pressure-equation residual |
| ------------------ | ------------------------------------------: | ----------------------------------: |
| Mode 1             | `0.019641`                                  | `0.000477`                          |
| Mode 15            | `0.980990`                                  | `0.0000565`                         |

In this bounded fixture, the near-grid mode retains roughly **98%** of its measured centred divergence even
though the nearest-neighbour pressure residual is small after 256 sweeps. This is consistent with the mismatch
between the centred divergence/gradient pair and the six-neighbour pressure Laplacian. It is not a measurement
of a full animated plume, all boundary configurations, or atmospheric fluid behavior. The separate
[idealized periodic audit](README.md#pressure-stencil-caution) explains the underlying stencil concern. A later
[matched face-flux experiment](GasProjection.md) passes closed/static-solid tests, but is not compatible with
or integrated into this unchanged plume's cell-centred advection and open/moving boundaries.

## Reproduce

```bash
python3 Exhibits/LightCompute/Code/Prepare.py
python3 -m venv _ClaudeScratch/venv
_ClaudeScratch/venv/bin/pip install wgpu==0.32.0 numpy==2.3.4
bash Exhibits/FluidReference/RunPlumePressure.sh
bash Exhibits/FluidReference/Run.sh
```

The captured run uses Linux x86-64, Python 3.11.2, wgpu 0.32.0, NumPy 2.3.4 and the pinned SwiftShader Vulkan
library. [Results and hashes](Results/PlumePressure.txt) ·
[Source-manifest and Node regressions](Results/PlumePressure-regression.txt).
The driver and `wgpu` native library are not sanitizer-instrumented. Advection, vorticity, buoyancy, thermo,
source coupling, rendering, moving-solid interaction, hardware GPU and real-world fluid validation remain open.

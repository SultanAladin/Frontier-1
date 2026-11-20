# Lighting Candidate Bench

Measurement harness that decides which GI, shadow, and path-trace architectures Frontier ships on GTX and RTX.

Everything here runs on **real triangle geometry**: a binary SAH bounds over triangles, tapered limb triangles for the deforming figure, and quad triangles for the studio and vehicle.
There is no 3D voxel SDF, no capsule distance proxy, and no denoiser anywhere in the pipeline.

## Why it exists

The architecture decision had been driven by RMSE numbers quoted from a validation harness that was never committed, so they could not be reproduced.
This harness replaces them with numbers you can rerun in about four seconds.

## Build and run

```bash
g++ -std=c++20 -O2 -pthread -o /tmp/bench tools/LightingCandidateBench/LightingCandidateBench.cpp
/tmp/bench tools/LightingCandidateBench/results
```

No dependencies beyond a C++20 compiler. The PNG writer is self-contained (stored deflate blocks plus a local Adler-32 and CRC-32).

## What it measures

| Family | Candidate | Reference |
| :--- | :--- | :--- |
| Solar shadows | `ContactPcss` (1024² cascade + 0.24 m contact ray), `VogelDiscQuery` (1/2/4/8/16 rays), `SurfelBlockerField` (cluster sweep 0.28/0.14/0.09/0.055 m) | 128-ray Vogel solar disc |
| Diffuse GI | `EmissiveFormFactorSequence` (SH9 field), `SurfelRadianceCascade` (8×8 octahedral + Chebyshev) | 48-spp Owen-scrambled Sobol path trace, emissive transport only |
| Deforming figure | Pose A (stride) versus Pose B (sprint reach), real triangles regenerated per pose | shadow footprint shift |

Reported per candidate: RMSE versus reference, banded error (umbra / penumbra / lit), relative RMSE, mean luminance, spatial correlation, CPU milliseconds, and a 64-bit repeat-evaluation hash for determinism.

## Outputs

`results/` holds three comparison sheets (`01`–`03`), three full-resolution renders (`04`–`06`), `lighting_candidate_metrics.csv`, and `lighting_candidate_summary.json`.

## Results summary

See `Documentation/Plans/WorldGiShadowPathTracerPlan.md` §2 for the measured table and the architectural consequences.

Headline findings:

- `SurfelBlockerField` is **rejected**. Its error plateaus at ~0.293 as cluster resolution is refined 5×, and its lit-band error of 0.2045 means it casts phantom occlusion on fully sunlit surfaces.
- `VogelDiscQuery` at 8 rays reaches 0.0153 RMSE with a 0.0098 lit-band error, so the solar disc query is reference-grade at a sane ray budget.
- `ContactPcss` has correct lit-band behaviour (0.0099) and all of its error in edge placement, so it stays as the cheap GTX tier.
- `EmissiveFormFactorSequence` wins GI on structure (correlation 0.772) but under-reports magnitude, which is a fixable cosine-windowing defect.
- `SurfelRadianceCascade` is the O(1) lookup candidate but currently loses energy; its measured 0.1118 is a floor to beat, not a result to defend.
- Pose A → Pose B real-triangle regeneration plus bounds rebuild costs 1.08 ms, confirming no voxel rebake is needed.

## Caveat

The scene is deliberately small (694 triangles) so the harness runs anywhere. Timings will grow with `ControlVehicle`-scale densities; re-measure those in the corresponding implementation phase before quoting them.

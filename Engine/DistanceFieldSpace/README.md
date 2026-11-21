# Engine/DistanceFieldSpace — Order 2 `GtxLowEnd`-Only Local Distance Volumes & Articulated Capsule Rigs

This directory provides compact, self-contained signed/unsigned distance primitives used **exclusively** by the Order 2 `GtxLowEnd` (`RealTimeGiGtxLowEnd`) tier (`Documentation/Plans/WorldRenderingRedoPlan.md` §4.1).

**Hardware Tier Invariant:** Order 1 (`MatCap`, `OpenPbrDirect`), Order 2 `HighEnd` (`RealTimeGiHighEnd`), and Order 3 (`DeterministicPathTracer`) allocate **`0` bytes** of `LocalSignedDistanceVolume`.

## Units

- `LocalSignedDistanceVolume.{h,cpp}` — Per-asset compact `48^3` binary16 (`221,184 B` << `3.7 MB` budget) local distance volume baked via BVH4 closest-triangle queries, with $C^0$-continuous Euclidean outer-box clamping $\sqrt{d_{\partial\Omega}^2 + d_{\text{box}}^2}$ eliminating AABB boundary seams.
- `ArticulatedCapsuleRig.h` — Sub-`0.01 ms` live deforming tapered bone-capsule rig (`TaperedLimbCapsule32`, `ArticulatedCapsuleRig`) and fixed-topology humanoid generator (`BuildArticulatedHumanoidPose`) shared between `GtxLowEnd` `SmoothMin` capsule cones and `HighEnd` skinned `BVH4` AABB refits.

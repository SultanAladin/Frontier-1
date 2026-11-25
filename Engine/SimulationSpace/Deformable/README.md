# 🧩 `Engine/SimulationSpace/Deformable/`

> **TL;DR**: Unified C++20 XPBD + 3D Volumetric Stable Neo-Hookean FEM deformable physics subsystem (1D tearable strands/cables/chains, 2D cloth/pneumatic shells, botanical posture springs, and 3D tetrahedral soft-body tires).

---

## Submodules

| File | Responsibility |
| :--- | :--- |
| `DeformableTypes.h` | SOA `DeformableBody`, GPU-aligned `DeformableStepUniforms`, Lamé elastic parameters, pneumatic/FEM tire & strand/foliage descriptors |
| `TetrahedralSolidBuilder.{h,cpp}` | Conformal 5-tet-per-hex decomposition, precomputed inverse material matrices ($\mathbf{D}_m^{-1}$) and positive rest volumes ($V_0$) |
| `ConflictColorPartitioner.{h,cpp}` | Greedy vertex-disjoint batch colorer for zero-atomic parallel GPU execution of 1D/2D/3D constraints |
| `XpbdConstraintSolver.{h,cpp}` | Unified substep integration, distance/tear, bending, botanical posture, sphere/ground/rim collision projection |
| `NeoHookeanTetSolver.{h,cpp}` | Macklin & Müller (2021) Stable Neo-Hookean 3D FEM ($C_D$ shear + $C_H$ volume), Nanson $\operatorname{cof}(\mathbf{F})$ & nodal strain scatter |
| `PneumaticTireSolver.{h,cpp}` | Exact co-rotating transport, Boyle air-cavity bulge, outward centrifugal growth ($\propto \omega^2$) & axle suspension coupling |
| `DeformablePhysicsCoupling.{h,cpp}` | `ForceFieldRegister` wind/vortex sampling & Jolt `BroadphaseSolver` rigid proxy collision coupling |
| `DeformablePresetRegistry.{h,cpp}` | Deterministic C++20 builders for all 9 studio presets (`SportTireFem`, `SportTirePneumatic`, `BotanicalFerns`, `OrbSpiderweb`, `CablesChains`, `HangingVines`, `MarshReeds`, `UnderwaterKelp`, `WovenCloth`) |

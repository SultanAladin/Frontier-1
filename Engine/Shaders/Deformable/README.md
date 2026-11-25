# Frontier Deformable XPBD, 3D FEM, GPU Cage & VAT Shaders

Native `.slang` compute, vertex, and fragment shaders targeting Vulkan 1.3 SPIR-V with exact 16-byte struct parity verified via C++20 `static_assert`.

## Shader Manifest

| Shader | Stage | Entry Point | Description |
| :--- | :--- | :--- | :--- |
| `DeformableLayouts.slang` | Shared | — | ABI structs (`DeformableStepUniforms` 128 B, `DistanceConstraintGpu` 24 B, `BendConstraintGpu` 24 B, `TetElementGpu` 64 B, `RigidColliderProxyGpu` 80 B, `CageBoundVertexGpu` 84 B, `VertexMotionAtlasUniforms` 64 B) |
| `XpbdPredictIntegrate.slang` | Compute `[64,1,1]` | `CsXpbdPredictIntegrate` | Co-rotating tire transport, centrifugal expansion, wind/drag & symplectic XPBD prediction |
| `XpbdDistanceBatch.slang` | Compute `[64,1,1]` | `CsXpbdDistanceBatch` | Zero-atomic conflict-colored XPBD distance projection + tearing |
| `XpbdBendBatch.slang` | Compute `[64,1,1]` | `CsXpbdBendBatch` | Zero-atomic conflict-colored isometric bending projection |
| `NeoHookeanTetBatch.slang` | Compute `[64,1,1]` | `CsNeoHookeanTetBatch` | Zero-atomic conflict-colored 3D Volumetric Stable Neo-Hookean FEM (`C_D`, `C_H`) + Nanson `cof(F)` writeout |
| `DeformableContactUpdate.slang` | Compute `[64,1,1]` | `CsDeformableContactUpdate` | Ground plane, curb collider, Jolt `RigidColliderProxy`, rim keep-out & XPBD velocity update |
| `CageSkinningVertex.slang` | Vertex | `VsCageSkinning` | Live GPU Cage Skinning (Mode 1 = 3D Tet Barycentrics + Nanson `cof(F)`; Mode 2 = Quad-Cage Bilinear) |
| `VertexMotionAtlasSampling.slang` | Vertex | `VsVertexMotionAtlas` | Mode 0 Baked `RGBA32F` VAT vertex shader (`0.00 ms` CPU physics) |
| `VertexMotionAtlasBakeCompute.slang` | Compute `[64,1,1]` | `CsVertexMotionAtlasBake` | Zero-readback GPU compute baker writing deformed vertices & normals directly into `RWTexture2D<float4>` VAT slices |
| `TireCarcassMaterial.slang` | Fragment | `FsTireCarcassMaterial` | Zone-aware PBR rubber shading + 3D volumetric strain heatmap overlay |

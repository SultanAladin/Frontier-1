/*====================================================================================================================================
                                                        ENCLOSEDVOLUME.H
====================================================================================================================================*/
// 🧩 Exact enclosed volume and parametric face area by Green's theorem over SP-curves (no tessellation)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <functional>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ShellVolumeMeasure
{
    double Volume        = 0.0;                                                              // [len³] - Σ face flux (signed)
    double FluxMagnitude = 0.0;                                                              // [len³] - Σ |face flux| (cancellation scale)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 V = ⅓ ∮∮ P·n dA. Per face, ∬_D f d𝑢 d𝑣 = ∮_∂D G d𝑣 with G(𝑢, 𝑣) = ∫ f(s, 𝑣) ds, integrated along each fin's SP-curve
//    in traversal order. Loop orientation in (𝑢, 𝑣) already carries the face sense, so the fin sum IS the outward flux.
//    Works for any trimmed face with SP-curves — replaces the tessellation volume that Sketcher used as truth.
[[nodiscard]] double IntegrateShellVolume(const ModelPartition& Partition, SolidShellToken SolidShell);
[[nodiscard]] ShellVolumeMeasure MeasureShellVolume(const ModelPartition& Partition, SolidShellToken SolidShell);
[[nodiscard]] double IntegrateBodyVolume(const ModelPartition& Partition, BodyToken Body);

// Signed parametric area Σ ∮ 𝑢 d𝑣 over a face's loops: positive when the loops run counter-clockwise in (𝑢, 𝑣).
[[nodiscard]] double IntegrateParametricArea(const ModelPartition& Partition, FaceToken Face);

// 💡 Outward flux ∮∮ F·n dA of a vector field over a closed shell, by the same Green reduction as the volume (F = P / 3
//    gives the volume). By the divergence theorem it is ∭ div F dV over the shell's interior, so F = (𝑥² / 2, 0, 0)
//    gives the first moment ∭ 𝑥 dV and F = (𝑥³ / 3, 0, 0) the second ∭ 𝑥² dV. Same noise floor as the volume.
[[nodiscard]] double IntegrateShellFlux(const ModelPartition& Partition, SolidShellToken SolidShell, const std::function<Vector3(Vector3)>& Field);

// 💡 ∬ w(P) dA over a trimmed face: density w · |∂P/∂𝑢 × ∂P/∂𝑣| through Green's theorem over its SP-curves, signed by
//    the loops' sense in (𝑢, 𝑣) so a reversed face still integrates positively. w ≡ 1 gives the true surface area.
[[nodiscard]] double IntegrateFaceSurface(const ModelPartition& Partition, FaceToken Face, const std::function<double(Vector3)>& Weight);

// Arc length ∫ |C′(τ)| dτ of an edge over its interval (adaptive Gauss–Legendre, split at the carrier's breakpoints).
[[nodiscard]] double MeasureEdgeLength(const ModelPartition& Partition, EdgeToken Edge);

} // namespace Frontier::GeometrySpace::Parametric

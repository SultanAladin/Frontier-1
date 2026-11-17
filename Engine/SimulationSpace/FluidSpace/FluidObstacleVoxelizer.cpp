/*============================================================================================================================================
                                                         FLUIDOBSTACLEVOXELIZER.CPP
============================================================================================================================================*/
// 🧩 Voxelizes GI SignedDistanceVolume + WideBoundingTree + 5 analytical colliders into +Z-Up fluid solid masks

#include "Engine/SimulationSpace/FluidSpace/FluidObstacleVoxelizer.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::FluidSpace
{

float FluidObstacleVoxelizer::EvaluateAnalyticalObstacleMask(
    const Mathematics::Axis3D&   WorldPoint,
    const FluidObstacleSettings& Obstacle,
    float                        ElapsedSeconds) noexcept
{
    if (Obstacle.Type == FluidObstacleType::None)
    {
        return 0.0f;
    }

    Mathematics::Axis3D Center = Obstacle.CenterMeters;
    if (Obstacle.AutoMove)
    {
        Center.X += std::sin(ElapsedSeconds * Obstacle.MoveSpeed) * 0.42f;
        Center.Y += std::cos(ElapsedSeconds * Obstacle.MoveSpeed * 0.7f) * 0.22f;
    }

    const Mathematics::Axis3D D = WorldPoint - Center;
    const float               R = std::max(0.02f, Obstacle.RadiusMeters);

    switch (Obstacle.Type)
    {
        case FluidObstacleType::Sphere:
            return (std::sqrt(D.X * D.X + D.Y * D.Y + D.Z * D.Z) < R) ? 1.0f : 0.0f;
        case FluidObstacleType::VerticalCylinder:
            return (std::hypot(D.X, D.Y) < R * 0.75f && std::abs(D.Z) < R * 1.8f) ? 1.0f : 0.0f;
        case FluidObstacleType::HorizontalCylinder:
            return (std::hypot(D.Y, D.Z) < R * 0.72f && std::abs(D.X) < R * 1.9f) ? 1.0f : 0.0f;
        case FluidObstacleType::DeflectorSlab:
            return (std::abs(D.X) < R * 1.55f && std::abs(D.Y) < R * 1.55f && std::abs(D.Z) < R * 0.45f) ? 1.0f : 0.0f;
        case FluidObstacleType::TyreRing:
        {
            const float RingR = std::hypot(D.X, D.Y) - R * 0.76f;
            return (std::hypot(RingR, D.Z) < R * 0.24f) ? 1.0f : 0.0f;
        }
        case FluidObstacleType::None:
        default:
            return 0.0f;
    }
}

void FluidObstacleVoxelizer::BakeSceneGiCollisionMask(
    const DistanceFieldSpace::SignedDistanceVolume* OptionalSdf,
    const SpatialAcceleration::WideBoundingTree*    OptionalTree,
    const FluidObstacleSettings&                    Obstacle,
    float                                           ElapsedSeconds,
    FluidGridVolume&                                InOutHeroGrid) noexcept
{
    const uint32_t R = InOutHeroGrid.Resolution;
    if (R == 0u) return;

    const float FR = static_cast<float>(R);
    const Mathematics::Axis3D Span = InOutHeroGrid.BoxMaxWorld - InOutHeroGrid.BoxMinWorld;
    const float VoxelRadius = 0.55f * std::max({ Span.X, Span.Y, Span.Z }) / FR;

    const bool HasSdf  = (Obstacle.VoxelizeSceneGiSdf && OptionalSdf != nullptr && !OptionalSdf->BrickIndirection.empty());
    const bool HasTree = (!HasSdf && Obstacle.VoxelizeSceneGiSdf && OptionalTree != nullptr && !OptionalTree->WideEntries.empty());

    for (uint32_t IZ = 0u; IZ < R; ++IZ)
    {
        const float WZ = InOutHeroGrid.BoxMinWorld.Z + ((static_cast<float>(IZ) + 0.5f) / FR) * Span.Z;
        for (uint32_t IY = 0u; IY < R; ++IY)
        {
            const float WY = InOutHeroGrid.BoxMinWorld.Y + ((static_cast<float>(IY) + 0.5f) / FR) * Span.Y;
            for (uint32_t IX = 0u; IX < R; ++IX)
            {
                const float WX = InOutHeroGrid.BoxMinWorld.X + ((static_cast<float>(IX) + 0.5f) / FR) * Span.X;
                const Mathematics::Axis3D P { WX, WY, WZ };

                float Solid = EvaluateAnalyticalObstacleMask(P, Obstacle, ElapsedSeconds);
                if (Solid < 0.5f && HasSdf)
                {
                    // Sample SignedDistanceVolume (inside or within voxel half-width counts as solid wall)
                    const float Dist = OptionalSdf->SampleSignedDistance(P);
                    if (Dist <= VoxelRadius * 0.45f)
                    {
                        Solid = 1.0f;
                    }
                }
                else if (Solid < 0.5f && HasTree)
                {
                    // Fallback 6-ray parity / short probe test when SDF wasn't baked
                    SpatialAcceleration::RaySegmentQuery Q {};
                    Q.Origin          = P;
                    Q.UnitDirection   = { 0.0f, 0.0f, 1.0f };
                    Q.MinimumDistance = 0.0f;
                    Q.MaximumDistance = VoxelRadius;
                    if (OptionalTree->QueryRayOccluded(Q))
                    {
                        Solid = 1.0f;
                    }
                }

                const uint32_t Idx = FluidGridVolume::FlatIndex(
                    static_cast<int32_t>(IX), static_cast<int32_t>(IY), static_cast<int32_t>(IZ), R);
                InOutHeroGrid.SceneSolidMask[Idx] = Solid;
            }
        }
    }
}

} // namespace Frontier::SimulationSpace::FluidSpace

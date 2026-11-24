/*============================================================================================================================================
                                                     VERTEXMOTIONATLASBAKER.CPP
============================================================================================================================================*/
// 🧩 Phase D05 Offscreen deterministic VAT baker running 3D FEM or Pneumatic XPBD into 256-col tiled RGBA32F textures

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasBaker.h"
#include "Engine/GeometrySpace/Deformation/SurfaceQuadCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TetrahedralCageEmbedder.h"
#include "Engine/SimulationSpace/Deformable/XpbdConstraintSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace::Deformation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  VERTEX MOTION ATLAS BAKER
//------------------------------------------------------------------------------------------------------------------------

VertexMotionAtlas VertexMotionAtlasBaker::BakeFromDeformable(
    SimulationSpace::Deformable::DeformableBody& body,
    const SimulationSpace::Deformable::DeformableSimulationConfiguration& configuration,
    const CageEmbeddedSurface& embeddedSurface,
    const VertexMotionAtlasBakeSettings& settings)
{
    VertexMotionAtlas atlas {};
    const uint32_t V = embeddedSurface.VertexCount();
    const uint32_t F = std::max(1u, settings.SliceCount);
    if (V == 0u)
    {
        return atlas;
    }

    const uint32_t W = VertexMotionAtlas::kDefaultAtlasWidth;
    const uint32_t R = (V + W - 1u) / W;
    const uint32_t H = R * F;

    atlas.VertexCount     = V;
    atlas.TriangleCount   = embeddedSurface.TriangleCount();
    atlas.SliceCount      = F;
    atlas.AtlasWidth      = W;
    atlas.RowsPerSlice    = R;
    atlas.AtlasHeight     = H;
    atlas.SlicesPerSecond = 1.0f / std::max(1.0e-4f, settings.StepDeltaSeconds * static_cast<float>(std::max(1u, settings.SimStepsPerSlice)));
    atlas.SolverLabel     = (embeddedSurface.Mode == CageEmbedMode::Tetrahedral3D) ? "3D Neo-Hookean FEM" : "Pneumatic XPBD";
    atlas.PositionStrainRgba.assign(static_cast<size_t>(W) * H * 4u, 0.0f);
    atlas.NormalRgba.assign(static_cast<size_t>(W) * H * 4u, 0.0f);
    atlas.SlicePoses.reserve(F);

    // Save live body kinematics so offscreen baking never perturbs the active scene
    const std::vector<float> savedPos  = body.Positions;
    const std::vector<float> savedPrev = body.PrevPositions;
    const std::vector<float> savedVel  = body.Velocities;
    const std::vector<float> savedStrain = body.PointStrain;
    const std::vector<float> savedCof    = body.TetCofactorNormals;
    const auto savedCollider           = body.Collider;

    const bool hasFem  = !body.FemTires.empty();
    const bool hasXpbd = !body.PneumaticTires.empty();
    const float savedAxleY    = hasFem ? body.FemTires[0].AxleY : (hasXpbd ? body.PneumaticTires[0].AxleY : 0.0f);
    const float savedAxleVy   = hasFem ? body.FemTires[0].AxleVelocityY : (hasXpbd ? body.PneumaticTires[0].AxleVelocityY : 0.0f);
    const float savedSpinRad  = hasFem ? body.FemTires[0].SpinAngleRad : (hasXpbd ? body.PneumaticTires[0].SpinAngleRad : 0.0f);

    SimulationSpace::Deformable::DeformableSimulationConfiguration bakeCfg = configuration;
    if (std::fabs(bakeCfg.TireSpinRadPerSec) < 2.5f)
    {
        bakeCfg.TireSpinRadPerSec = settings.MinimumSpinRadSec;
    }
    if (settings.EnableCurbSweep)
    {
        body.Collider.Active = true;
        bakeCfg.ColliderEnabled = true;
    }

    float minX = 1.0e9f, minY = 1.0e9f, minZ = 1.0e9f;
    float maxX = -1.0e9f, maxY = -1.0e9f, maxZ = -1.0e9f;
    DeformedSurfaceSample sample {};

    for (uint32_t f = 0u; f < F; ++f)
    {
        if (settings.EnableCurbSweep)
        {
            const float u = static_cast<float>(f) / static_cast<float>(F);
            body.Collider.CenterMeters[0] = settings.CurbSweepHalfSpanX - u * (2.0f * settings.CurbSweepHalfSpanX);
            body.Collider.CenterMeters[1] = body.GroundY + settings.CurbHeightAboveFloor;
            body.Collider.CenterMeters[2] = 0.0f;
        }

        const uint32_t steps = std::max(1u, settings.SimStepsPerSlice);
        for (uint32_t st = 0u; st < steps; ++st)
        {
            const float tSec = static_cast<float>(f * steps + st) * settings.StepDeltaSeconds;
            SimulationSpace::Deformable::XpbdConstraintSolver::Step(body, settings.StepDeltaSeconds, bakeCfg, tSec, 0.0f);
        }

        RigidSlicePose pose {};
        if (hasFem)
        {
            pose.AxleX        = body.FemTires[0].AxleX;
            pose.AxleY        = body.FemTires[0].AxleY;
            pose.SpinAngleRad = body.FemTires[0].SpinAngleRad;
        }
        else if (hasXpbd)
        {
            pose.AxleX        = body.PneumaticTires[0].AxleX;
            pose.AxleY        = body.PneumaticTires[0].AxleY;
            pose.SpinAngleRad = body.PneumaticTires[0].SpinAngleRad;
        }
        pose.ColliderX = body.Collider.CenterMeters[0];
        pose.ColliderY = body.Collider.CenterMeters[1];
        pose.ColliderZ = body.Collider.CenterMeters[2];
        atlas.SlicePoses.push_back(pose);

        if (embeddedSurface.Mode == CageEmbedMode::Tetrahedral3D)
        {
            TetrahedralCageEmbedder::EvaluateLiveSurface(body, embeddedSurface, sample);
        }
        else
        {
            SurfaceQuadCageEmbedder::EvaluateLiveSurface(body, embeddedSurface, sample);
        }

        const float* rPos = embeddedSurface.RestSurface.RestPositions.data();
        for (uint32_t vi = 0u; vi < V; ++vi)
        {
            const size_t v3 = static_cast<size_t>(vi) * 3u;
            const float wx = sample.Positions[v3 + 0u];
            const float wy = sample.Positions[v3 + 1u];
            const float wz = sample.Positions[v3 + 2u];
            const float dx = wx - rPos[v3 + 0u];
            const float dy = wy - rPos[v3 + 1u];
            const float dz = wz - rPos[v3 + 2u];

            minX = std::min(minX, dx); maxX = std::max(maxX, dx);
            minY = std::min(minY, dy); maxY = std::max(maxY, dy);
            minZ = std::min(minZ, dz); maxZ = std::max(maxZ, dz);

            const size_t px = atlas.TexelOffset(vi, f);
            atlas.PositionStrainRgba[px + 0u] = wx;
            atlas.PositionStrainRgba[px + 1u] = wy;
            atlas.PositionStrainRgba[px + 2u] = wz;
            atlas.PositionStrainRgba[px + 3u] = sample.Strain[vi];

            atlas.NormalRgba[px + 0u] = sample.Normals[v3 + 0u];
            atlas.NormalRgba[px + 1u] = sample.Normals[v3 + 1u];
            atlas.NormalRgba[px + 2u] = sample.Normals[v3 + 2u];
            atlas.NormalRgba[px + 3u] = 0.0f;
        }
    }

    // Restore original live body kinematics cleanly
    body.Positions          = savedPos;
    body.PrevPositions      = savedPrev;
    body.Velocities         = savedVel;
    body.PointStrain        = savedStrain;
    body.TetCofactorNormals = savedCof;
    body.Collider           = savedCollider;
    if (hasFem)
    {
        body.FemTires[0].AxleY         = savedAxleY;
        body.FemTires[0].AxleVelocityY = savedAxleVy;
        body.FemTires[0].SpinAngleRad  = savedSpinRad;
    }
    else if (hasXpbd)
    {
        body.PneumaticTires[0].AxleY         = savedAxleY;
        body.PneumaticTires[0].AxleVelocityY = savedAxleVy;
        body.PneumaticTires[0].SpinAngleRad  = savedSpinRad;
    }

    atlas.DisplacementMin = { minX, minY, minZ };
    atlas.DisplacementMax = { maxX, maxY, maxZ };
    return atlas;
}

} // namespace Frontier::GeometrySpace::Deformation

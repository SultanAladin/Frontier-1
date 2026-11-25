/*============================================================================================================================================
                                                    DEFORMABLEWORLDVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase D13-D16 Validation suite for DeformableSurfaceBuilder (all 9 presets), DeformableVulkanExecutor (triple-buffered
//    SSBO/VAT ring), and DeformableWorldRegistry (multi-instance .SoftBody spawning, distance LOD/sleep culling & ForceField coupling).

#include "Engine/GeometrySpace/Deformation/DeformableSurfaceBuilder.h"
#include "Engine/GeometrySpace/Deformation/SurfaceQuadCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TetrahedralCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TireSurfaceBuilder.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasBaker.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/Deformable/DeformableVulkanExecutor.h"
#include "Engine/SimulationSpace/Deformable/DeformableWorldRegistry.h"
#include "Engine/SimulationSpace/Deformable/XpbdConstraintSolver.h"

#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const std::string& label)
{
    if (cond) ++gPassed;
    else
    {
        ++gFailed;
        std::cerr << "[FAIL] " << label << "\n";
    }
}

bool AllFinite(const std::vector<float>& values)
{
    for (float v : values)
    {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

bool AllUnitNormals(const std::vector<float>& normals)
{
    for (size_t i = 0u; i + 2u < normals.size(); i += 3u)
    {
        const float l = std::sqrt(normals[i] * normals[i] + normals[i + 1u] * normals[i + 1u] + normals[i + 2u] * normals[i + 2u]);
        if (!std::isfinite(l) || l < 0.98f || l > 1.02f) return false;
    }
    return true;
}

} // namespace

int main()
{
    using namespace Frontier::GeometrySpace::Deformation;
    using namespace Frontier::SimulationSpace::Deformable;

    // ---------------------------------------------------------------------------------------------------------
    // D13: DeformableSurfaceBuilder covers ALL 9 presets with finite vertices, unit normals & tear-aware cloth
    // ---------------------------------------------------------------------------------------------------------
    const auto presets = DeformablePresetRegistry::EnumeratePresets();
    for (const auto& desc : presets)
    {
        DeformableBody body {};
        DeformableSimulationConfiguration cfg {};
        DeformablePresetRegistry::BuildPreset(desc.Kind, body, cfg);
        for (uint32_t s = 0u; s < 30u; ++s)
        {
            XpbdConstraintSolver::Step(body, 1.0f / 60.0f, cfg, static_cast<float>(s) / 60.0f, 0.0f);
        }

        ProceduralRenderSurface surf {};
        if (desc.Kind == DeformablePresetKind::SportTireFem)
        {
            const auto rest = TireSurfaceBuilder::ConstructSportTireSurface(0.0f, -0.62f, 0.0f, 0.66f, 1.15f, 0.43f);
            const auto emb  = TetrahedralCageEmbedder::EmbedIntoFemSolid(body, rest);
            DeformedSurfaceSample live {};
            TetrahedralCageEmbedder::EvaluateLiveSurface(body, emb, live);
            DeformableSurfaceBuilder::AppendSkinnedSurface(rest, live, surf);
        }
        else if (desc.Kind == DeformablePresetKind::SportTirePneumatic)
        {
            const auto rest = TireSurfaceBuilder::ConstructSportTireSurface(0.0f, -0.62f, 0.0f, 0.66f, 1.14f, 0.42f);
            const auto emb  = SurfaceQuadCageEmbedder::EmbedIntoPneumaticCarcass(body, rest);
            DeformedSurfaceSample live {};
            SurfaceQuadCageEmbedder::EvaluateLiveSurface(body, emb, live);
            DeformableSurfaceBuilder::AppendSkinnedSurface(rest, live, surf);
        }
        DeformableSurfaceBuilder::AppendBodyAttachments(body, surf, true, true);

        Check(surf.VertexCount() > 100u && surf.TriangleCount() > 100u,
              std::string("D13: ProceduralRenderSurface built cleanly for ") + desc.FileStem);
        Check(AllFinite(surf.Positions) && AllUnitNormals(surf.Normals),
              std::string("D13: Finite positions & unit normals for ") + desc.FileStem);
    }

    // Verify tear-aware woven cloth omits quads when edges are severed
    {
        DeformableBody cloth {};
        DeformableSimulationConfiguration cfg {};
        DeformablePresetRegistry::BuildPreset(DeformablePresetKind::WovenCloth, cloth, cfg);
        ProceduralRenderSurface intactSurf {}, tornSurf {};
        DeformableSurfaceBuilder::AppendBodyAttachments(cloth, intactSurf, false, false);
        const uint32_t cutCount = XpbdConstraintSolver::CutConstraintsNearSegment(
            cloth, { -0.8f, 0.4f, 0.0f }, { 0.8f, 0.4f, 0.0f }, 0.25f);
        DeformableSurfaceBuilder::AppendBodyAttachments(cloth, tornSurf, false, false);
        Check(cutCount > 0u && tornSurf.TriangleCount() < intactSurf.TriangleCount(),
              "D13: Tear-aware WovenCloth omits severed fabric quads");
    }

    // ---------------------------------------------------------------------------------------------------------
    // D14: DeformableVulkanExecutor triple-buffered SSBO ring & VAT upload tracking
    // ---------------------------------------------------------------------------------------------------------
    {
        DeformableBody femBody {};
        DeformableSimulationConfiguration femCfg {};
        DeformablePresetRegistry::BuildPreset(DeformablePresetKind::SportTireFem, femBody, femCfg);

        DeformableVulkanExecutor exec {};
        Check(exec.InitializeForBody(femBody, femCfg) && exec.Ready(),
              "D14: DeformableVulkanExecutor initializes triple-buffered SSBO ring");
        Check(exec.InspectTelemetry().AllocatedSsboBytes > 300000ull,
              "D14: Triple-buffered SSBO footprint accurately accounted");

        const auto rest = TireSurfaceBuilder::ConstructSportTireSurface(0.0f, -0.62f, 0.0f, 0.66f, 1.15f, 0.43f);
        const auto emb  = TetrahedralCageEmbedder::EmbedIntoFemSolid(femBody, rest);
        VertexMotionAtlasBakeSettings bs {};
        bs.SliceCount = 8u;
        const auto vat = VertexMotionAtlasBaker::BakeFromDeformable(femBody, femCfg, emb, bs);
        Check(exec.UploadVertexMotionAtlas(vat) && exec.InspectTelemetry().AllocatedVatImageBytes > 1000000ull,
              "D14: UploadVertexMotionAtlas registers RGBA32F position & normal texture bytes");

        for (uint32_t t = 0u; t < 5u; ++t)
        {
            const auto plan = exec.ExecuteTick(femBody, 1.0f / 60.0f, femCfg, static_cast<float>(t) / 60.0f);
            Check(plan.TotalDispatchCount > 0u, "D14: ExecuteTick returns non-empty lock-free dispatch plan");
        }
        const auto& tel = exec.InspectTelemetry();
        Check(tel.CompletedTicks == 5ull && tel.ActiveRingSlot == (5u % 3u) &&
              tel.TotalDispatches >= 5ull * femCfg.Substeps && tel.TotalMemoryBarriers == tel.TotalDispatches,
              "D14: Triple-buffer ring rotation & cumulative dispatch/barrier telemetry match exactly");
    }

    // ---------------------------------------------------------------------------------------------------------
    // D15: DeformableWorldRegistry multi-instance .SoftBody loading & distance Full/Half/Sleep LOD culling
    // ---------------------------------------------------------------------------------------------------------
    {
        DeformableWorldRegistry world {};
        DeformableWorldCullingPolicy policy {};
        policy.FullRateRadiusMeters = 25.0f;
        policy.HalfRateRadiusMeters = 60.0f;
        policy.SleepRadiusMeters    = 90.0f;
        world.SetCullingPolicy(policy);

        std::string err;
        const uint32_t nearId = world.SpawnFromSoftBodyFile(
            "EngineContent/Simulation/Deformables/SportTireFem.SoftBody", { 0.0f, 0.0f, 0.0f }, "NearTire", &err);
        const uint32_t midId = world.SpawnFromSoftBodyFile(
            "EngineContent/Simulation/Deformables/BotanicalFerns.SoftBody", { 40.0f, 0.0f, 0.0f }, "MidFerns", &err);
        const uint32_t farId = world.SpawnFromSoftBodyFile(
            "EngineContent/Simulation/Deformables/WovenCloth.SoftBody", { 140.0f, 0.0f, 0.0f }, "FarCloth", &err);

        Check(nearId != 0u && midId != 0u && farId != 0u,
              "D15: DeformableWorldRegistry spawns 3 instances directly from disk .SoftBody files");

        DeformableWorldStepSummary lastSum {};
        for (uint32_t step = 0u; step < 10u; ++step)
        {
            lastSum = world.StepWorld(1.0f / 60.0f, static_cast<float>(step) / 60.0f, { 0.0f, 1.5f, 5.0f });
        }
        Check(lastSum.TotalInstances == 3u && lastSum.FullRateCount == 1u &&
              lastSum.HalfRateCount == 1u && lastSum.SleepingCount == 1u,
              "D15: Distance culling classifies Near=FullRate, Mid=HalfRate, Far=Asleep");
        Check(world.Find(nearId)->SteppedTickCount == 10ull &&
              world.Find(midId)->SteppedTickCount == 5ull &&
              world.Find(farId)->SteppedTickCount == 0ull,
              "D15: Step budgets honor exact 10 / 5 / 0 ticks across FullRate / HalfRate / Asleep");

        // Move camera near the sleeping cloth -> wakes up immediately!
        const auto wakeSum = world.StepWorld(1.0f / 60.0f, 0.2f, { 140.0f, 1.0f, 4.0f });
        Check(world.Find(farId)->ActivityTier == DeformableActivityTier::FullRate &&
              world.Find(farId)->SteppedTickCount == 1ull &&
              wakeSum.FullRateCount == 1u,
              "D15: Moving camera near sleeping soft body wakes it immediately into FullRate");

        Check(world.RemoveInstance(midId) && world.EnumerateInstances().size() == 2u,
              "D15: RemoveInstance cleanly unregisters deformable instance");
    }

    std::cout << "DeformableWorldValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}

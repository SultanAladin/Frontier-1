/*============================================================================================================================================
                                                  DEFORMABLEINTEGRATIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase D09-D12 End-to-end Frontier engine integration validation:
//    - D09: PolygonNetworkDeformer (Frontier PolygonNetwork <-> HighPolySurfaceRest <-> Live FEM/Nanson deformation)
//    - D10: DeformableGpuDispatch (16B-aligned SSBO packer & lock-free conflict-colored compute dispatch plan)
//    - D11: VehicleTireDeformBridge (4-wheel Jolt load/spin/thermal/curb bridge + 3-tier Live FEM / Live XPBD / 0-CPU VAT LOD)
//    - D12: DeformableThreadExchange (real multi-threaded wait-free ThreadBoundaryExchange publication & lock-free directive intake)

#include "Engine/GeometrySpace/Deformation/PolygonNetworkDeformer.h"
#include "Engine/GeometrySpace/Deformation/TetrahedralCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TireSurfaceBuilder.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasBaker.h"
#include "Engine/SimulationSpace/Deformable/DeformableGpuDispatch.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/Deformable/DeformableThreadExchange.h"
#include "Engine/SimulationSpace/Deformable/VehicleTireDeformBridge.h"
#include "Engine/SimulationSpace/Deformable/XpbdConstraintSolver.h"

#include <atomic>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace
{

int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const std::string& label)
{
    if (cond)
    {
        ++gPassed;
    }
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

} // namespace

int main()
{
    using namespace Frontier::GeometrySpace;
    using namespace Frontier::GeometrySpace::Deformation;
    using namespace Frontier::SimulationSpace::Deformable;

    // ---------------------------------------------------------------------------------------------------------
    // D09: PolygonNetwork <-> HighPolySurfaceRest <-> Live 3D FEM Cage Skinning Bridge
    // ---------------------------------------------------------------------------------------------------------
    const HighPolySurfaceRest restTire = TireSurfaceBuilder::ConstructSportTireSurface(0.0f, -0.62f, 0.0f, 0.66f, 1.15f, 0.43f);
    PolygonNetwork tireNetwork = PolygonNetworkDeformer::BuildPolygonNetworkFromSurface(restTire, "SportTireFemNetwork");

    Check(tireNetwork.Vertices.size() == 7552u && tireNetwork.Faces.size() == 12544u,
          "D09: BuildPolygonNetworkFromSurface creates 7,552 vertices & 12,544 faces");
    Check(tireNetwork.VerifyNetworkIntegrity(),
          "D09: Constructed tire PolygonNetwork passes VerifyNetworkIntegrity()");

    const HighPolySurfaceRest extracted = PolygonNetworkDeformer::ExtractSurfaceRest(tireNetwork);
    Check(extracted.VertexCount() == 7552u && extracted.TriangleCount() == 12544u,
          "D09: ExtractSurfaceRest recovers exact 7,552 vertices & 12,544 triangles");

    DeformableBody femBody {};
    DeformableSimulationConfiguration femCfg {};
    DeformablePresetRegistry::BuildPreset(DeformablePresetKind::SportTireFem, femBody, femCfg);
    const CageEmbeddedSurface femEmbed = TetrahedralCageEmbedder::EmbedIntoFemSolid(femBody, extracted);
    Check(femEmbed.MaxRestError < 1.0e-4f,
          "D09: PolygonNetwork-extracted surface embeds into 1,080-tet FEM cage with < 0.0001 m rest error");

    femBody.Collider.Active = true;
    femBody.Collider.CenterMeters = { 0.0f, femBody.GroundY + 0.16f, 0.0f };
    for (uint32_t s = 0u; s < 25u; ++s)
    {
        XpbdConstraintSolver::Step(femBody, 1.0f / 60.0f, femCfg, static_cast<float>(s) / 60.0f, 0.0f);
    }

    DeformedSurfaceSample femSample {};
    TetrahedralCageEmbedder::EvaluateLiveSurface(femBody, femEmbed, femSample);
    const uint64_t revBefore = tireNetwork.NetworkRevision;
    Check(PolygonNetworkDeformer::ApplyDeformedSample(tireNetwork, femSample, true),
          "D09: ApplyDeformedSample updates PolygonNetwork vertices, normals & face normals");
    Check(tireNetwork.NetworkRevision == revBefore + 1ull,
          "D09: ApplyDeformedSample increments PolygonNetwork::NetworkRevision");

    bool netNormalsUnit = true;
    for (const Vertex& v : tireNetwork.Vertices)
    {
        const float l = std::sqrt(v.Normal.X * v.Normal.X + v.Normal.Y * v.Normal.Y + v.Normal.Z * v.Normal.Z);
        if (!std::isfinite(v.Axes.X) || !std::isfinite(v.Axes.Y) || !std::isfinite(v.Axes.Z) || l < 0.98f || l > 1.02f)
        {
            netNormalsUnit = false;
            break;
        }
    }
    Check(netNormalsUnit, "D09: All 7,552 deformed PolygonNetwork vertices are finite with unit normals");

    // ---------------------------------------------------------------------------------------------------------
    // D10: GPU SSBO Buffer Packer & Lock-Free Conflict-Colored Compute Dispatch Plan
    // ---------------------------------------------------------------------------------------------------------
    {
        const DeformableGpuBufferPack femPack = DeformableGpuDispatch::PackBodyForGpu(femBody, femCfg);
        Check(femPack.PositionsInvMass.size() == 504u &&
              femPack.TetElements.size() == 1080u &&
              femPack.TetCofactorCols.size() == 3240u &&
              femPack.TotalUploadBytes > 100000ull,
              "D10: PackBodyForGpu packs 504 points, 1,080 tets & 3,240 cofactor float4s cleanly");

        DeformableBody roundTripBody = femBody;
        roundTripBody.Positions.assign(roundTripBody.Positions.size(), 0.0f);
        Check(DeformableGpuDispatch::UnpackPositionsFromGpu(femPack, roundTripBody) &&
              roundTripBody.Positions == femBody.Positions,
              "D10: UnpackPositionsFromGpu reproduces exact CPU Positions");

        const DeformableSubstepDispatchPlan plan = DeformableGpuDispatch::BuildTickDispatchPlan(
            femBody, femCfg, true, 7552u);
        const uint32_t expectedPerSubstep = 2u + femBody.DistanceBatches.ColorCount
                                          + femBody.BendBatches.ColorCount
                                          + femBody.TetBatches.ColorCount;
        Check(plan.TotalDispatchCount == femCfg.Substeps * expectedPerSubstep + 1u,
              "D10: BuildTickDispatchPlan emits exact lock-free color-batched dispatch count + VAT bake pass");
        Check(!plan.Steps.empty() &&
              plan.Steps.back().Pass == DeformableComputePassOrdinal::VatSliceBake &&
              plan.Steps.back().ThreadGroupsX == 118u,
              "D10: Trailing VatSliceBake pass schedules exact 118 threadgroups (ceil(7552 / 64))");
    }

    // ---------------------------------------------------------------------------------------------------------
    // D11: 4-Wheel Jolt Vehicle Tire Deform Bridge (Live FEM, Live XPBD & 0-CPU Baked VAT LOD)
    // ---------------------------------------------------------------------------------------------------------
    {
        VertexMotionAtlasBakeSettings vatSettings {};
        vatSettings.SliceCount       = 16u; // Fast 16-slice atlas for unit test
        vatSettings.SimStepsPerSlice = 2u;
        const VertexMotionAtlas sharedAtlas = VertexMotionAtlasBaker::BakeFromDeformable(femBody, femCfg, femEmbed, vatSettings);
        Check(sharedAtlas.Ready(), "D11: Pre-baked shared vehicle VAT is ready");

        VehicleTireDeformBridge vehicleBridge {};
        VehicleTireLodPolicy policy {};
        policy.LiveFemMaxDistanceMeters  = 18.0f;
        policy.LiveXpbdMaxDistanceMeters = 45.0f;
        vehicleBridge.Initialize(policy, &sharedAtlas);
        Check(vehicleBridge.Initialized(), "D11: VehicleTireDeformBridge initializes all 4 wheels cleanly");
        Check(vehicleBridge.SelectTierForDistance(10.0f) == WheelSolverTier::LiveFem3D &&
              vehicleBridge.SelectTierForDistance(30.0f) == WheelSolverTier::LivePneumaticXpbd &&
              vehicleBridge.SelectTierForDistance(80.0f) == WheelSolverTier::BakedVatZeroCpu,
              "D11: Distance-based 3-tier LOD selects LiveFem3D / LivePneumaticXpbd / BakedVatZeroCpu");

        std::array<VehicleWheelDeformInput, 4u> inputs {};
        inputs[0] = { true, 5200.0f, 18.0f, 0.0f, 0.04f, -0.03f, 0.12f, 345.0f, 32.0f, 0.16f, 0.0f }; // FL loaded + curb + hot
        inputs[1] = { true, 2600.0f, 18.0f, 0.0f, 0.01f,  0.01f, 0.11f, 320.0f, 32.0f, 0.00f, 0.0f }; // FR lighter
        inputs[2] = { true, 4800.0f, 18.0f, 0.0f, 0.03f, -0.02f, 0.00f, 335.0f, 30.0f, 0.00f, 0.0f }; // RL
        inputs[3] = { true, 2800.0f, 18.0f, 0.0f, 0.01f,  0.00f, 0.00f, 310.0f, 30.0f, 0.00f, 0.0f }; // RR

        // Tier 0: Close camera -> Live 3D FEM on all 4 wheels
        for (uint32_t step = 0u; step < 30u; ++step)
        {
            vehicleBridge.StepAllWheels(inputs, 1.0f / 60.0f, static_cast<float>(step) / 60.0f, 10.0f);
        }
        const auto& flFem = vehicleBridge.InspectTelemetry(WheelCornerOrdinal::FrontLeft);
        Check(flFem.ActiveTier == WheelSolverTier::LiveFem3D &&
              flFem.EffectivePressurePsi > 36.0f &&
              flFem.PeakStrain > 0.01f,
              "D11: Close-range FL wheel runs Live 3D FEM with Ideal Gas thermal pressure rise & curb strain");

        DeformedSurfaceSample wheelSurf {};
        vehicleBridge.EvaluateWheelSurface(WheelCornerOrdinal::FrontLeft, wheelSurf);
        Check(wheelSurf.Positions.size() == 7552u * 3u && AllFinite(wheelSurf.Positions),
              "D11: Live 3D FEM wheel evaluates 7,552 finite high-poly vertices");

        // Tier 1: Mid camera -> Live Pneumatic XPBD
        for (uint32_t step = 0u; step < 20u; ++step)
        {
            vehicleBridge.StepAllWheels(inputs, 1.0f / 60.0f, static_cast<float>(step) / 60.0f, 30.0f);
        }
        vehicleBridge.EvaluateWheelSurface(WheelCornerOrdinal::FrontRight, wheelSurf);
        Check(vehicleBridge.InspectTelemetry(WheelCornerOrdinal::FrontRight).ActiveTier == WheelSolverTier::LivePneumaticXpbd &&
              wheelSurf.Positions.size() == 7552u * 3u && AllFinite(wheelSurf.Positions),
              "D11: Mid-range FR wheel runs Live Pneumatic XPBD & evaluates 7,552 finite vertices");

        // Tier 2: Far camera -> 0.00 ms CPU Baked VAT
        vehicleBridge.StepAllWheels(inputs, 1.0f / 60.0f, 1.0f, 80.0f);
        vehicleBridge.EvaluateWheelSurface(WheelCornerOrdinal::RearLeft, wheelSurf);
        Check(vehicleBridge.InspectTelemetry(WheelCornerOrdinal::RearLeft).ActiveTier == WheelSolverTier::BakedVatZeroCpu &&
              wheelSurf.Positions.size() == 7552u * 3u && AllFinite(wheelSurf.Positions),
              "D11: Far-range RL wheel runs 0-CPU Baked VAT & evaluates 7,552 finite vertices");
    }

    // ---------------------------------------------------------------------------------------------------------
    // D12: Multi-Threaded Wait-Free DeformableThreadExchange Stress Test
    // ---------------------------------------------------------------------------------------------------------
    {
        DeformableThreadExchange exchange(DeformablePresetKind::SportTireFem);
        std::atomic<bool> physicsDone { false };
        std::atomic<uint64_t> readerReads { 0ull };
        std::atomic<bool> readerSawNan { false };
        std::atomic<bool> readerMonotonic { true };

        // Submit initial directives from Game/UI thread before & during stepping
        {
            DeformableControlDirective dropDir {};
            dropDir.Category       = DeformableDirectiveCategory::TriggerTireDrop;
            dropDir.DropLiftMeters = 0.75f;
            Check(exchange.EnqueueDirective(dropDir), "D12: Lock-free TriggerTireDrop directive enqueued");
        }

        std::thread readerThread([&]()
        {
            uint64_t lastGen = 0ull;
            while (!physicsDone.load(std::memory_order_acquire) || readerReads.load(std::memory_order_relaxed) < 20ull)
            {
                const auto lease = exchange.AcquireReadLease();
                if (lease.Valid())
                {
                    const uint64_t gen = lease.InspectHeader().GenerationSequence;
                    if (gen < lastGen) readerMonotonic.store(false, std::memory_order_relaxed);
                    lastGen = gen;

                    if (!AllFinite(lease->Positions))
                    {
                        readerSawNan.store(true, std::memory_order_relaxed);
                    }
                    readerReads.fetch_add(1ull, std::memory_order_relaxed);
                }
                std::this_thread::yield();
            }
        });

        // Owner Physics thread steps 40 ticks, then receives live preset switch + segment cut directives
        for (uint32_t step = 0u; step < 40u; ++step)
        {
            exchange.StepAndPublishOnOwnerThread(1.0f / 60.0f);
            if (step == 15u)
            {
                DeformableControlDirective sw {};
                sw.Category   = DeformableDirectiveCategory::SwitchPreset;
                sw.PresetKind = DeformablePresetKind::OrbSpiderweb;
                (void)exchange.EnqueueDirective(sw);

                DeformableControlDirective cut {};
                cut.Category  = DeformableDirectiveCategory::CutAlongSegment;
                cut.SegmentA  = { -1.5f, 0.1f, 0.0f };
                cut.SegmentB  = {  1.5f, 0.1f, 0.0f };
                cut.CutRadius = 0.25f;
                (void)exchange.EnqueueDirective(cut);
            }
        }
        physicsDone.store(true, std::memory_order_release);
        readerThread.join();

        const auto finalLease = exchange.AcquireReadLease();
        Check(finalLease.Valid() &&
              finalLease->ActivePreset == DeformablePresetKind::OrbSpiderweb &&
              finalLease->SeveredCount > 0u,
              "D12: Live cross-thread SwitchPreset(OrbSpiderweb) + CutAlongSegment reflected in wait-free reading");
        Check(exchange.QueryPublishedGeneration() >= 40ull &&
              exchange.QueryDrainedDirectiveCount() == 3ull,
              "D12: All 3 cross-thread directives drained and >= 40 wait-free generations published");
        Check(!readerSawNan.load() && readerMonotonic.load() && readerReads.load() >= 20ull,
              "D12: Concurrent wait-free reader observed 0 NaNs and strictly monotonic generations");
    }

    std::cout << "DeformableIntegrationValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}

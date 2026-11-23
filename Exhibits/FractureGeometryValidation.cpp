/*============================================================================================================================================
                                                      FRACTUREGEOMETRYVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end verification of GeometrySpace::Fracture (all 6 solvers, exact volume conservation, PolygonNetwork round-trip) & JoltPhysics runner

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Fracture/ConvexPartitioning.h"
#include "Engine/GeometrySpace/Fracture/EngineeringBakedFracture.h"
#include "Engine/GeometrySpace/Fracture/FastRuntimeFracture.h"
#include "Engine/GeometrySpace/Fracture/FractureMaterialRepository.h"
#include "Engine/SimulationSpace/PhysicsSpace/FractureSimulationRunner.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace
{

void RequireCondition(bool Condition, const char* Expr, int Line) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[FractureGeometryValidation] FAILED at line %d: %s\n", Line, Expr);
        std::exit(1);
    }
}

#define FRONTIER_REQUIRE(Expr) RequireCondition(static_cast<bool>(Expr), #Expr, __LINE__)

using namespace Frontier;
using namespace Frontier::GeometrySpace;
using namespace Frontier::GeometrySpace::Fracture;
using namespace Frontier::SimulationSpace::PhysicsSpace;

} // namespace

int main()
{
    std::printf("[FractureGeometryValidation] Starting fracture geometry & JoltPhysics validation...\n");

    // 1. Verify material & constitutive spec repository
    const uint32_t MaterialCount = FractureMaterialRepository::QueryMaterialCount();
    FRONTIER_REQUIRE(MaterialCount == 6u);
    for (uint32_t I = 0u; I < MaterialCount; ++I)
    {
        const auto& Mat      = FractureMaterialRepository::LookupMaterialByIndex(I);
        const auto& LookedUp = FractureMaterialRepository::LookupMaterial(Mat.Identifier);
        const auto& EngSpec  = FractureMaterialRepository::LookupEngineeringSpec(Mat.Identifier);
        FRONTIER_REQUIRE(std::strcmp(LookedUp.Identifier, Mat.Identifier) == 0);
        FRONTIER_REQUIRE(LookedUp.DensityKgPerM3 > 100.0f);
        FRONTIER_REQUIRE(LookedUp.YoungModulusGPa > 1.0f);
        FRONTIER_REQUIRE(LookedUp.FractureToughnessGc > 1.0f);
        FRONTIER_REQUIRE(EngSpec.CompressiveStrengthMPa > 0.0f);
        FRONTIER_REQUIRE(FractureMaterialRepository::ComputeRayleighWaveSpeedMps(LookedUp) > 500.0);
    }

    // 2. Verify all 8 solid shapes & PolygonNetwork round-trip
    const FractureSolidCategory Solids[] = {
        FractureSolidCategory::Cube,
        FractureSolidCategory::Monolith,
        FractureSolidCategory::CurtainPane,
        FractureSolidCategory::TimberBeam,
        FractureSolidCategory::Cylinder,
        FractureSolidCategory::Cone,
        FractureSolidCategory::Boulder,
        FractureSolidCategory::HollowCavity
    };
    for (const auto SolidCat : Solids)
    {
        const auto Poly    = ConvexPartitioning::ConstructSolidPolyhedron(SolidCat);
        const auto Metrics = ConvexPartitioning::ComputePolyhedronMetrics(Poly);
        FRONTIER_REQUIRE(Metrics.VolumeM3 > 0.05);
        FRONTIER_REQUIRE(Metrics.RadiusM > 0.1);

        const auto Network   = ConvexPartitioning::ConvertPolyhedronToPolygonNetwork(Poly);
        const auto RoundTrip = ConvexPartitioning::ConvertPolygonNetworkToPolyhedron(Network);
        const auto RtMetrics = ConvexPartitioning::ComputePolyhedronMetrics(RoundTrip);
        FRONTIER_REQUIRE(std::abs(RtMetrics.VolumeM3 - Metrics.VolumeM3) / Metrics.VolumeM3 < 1.0e-5);
    }

    // 3. Verify Smith 3x3 symmetric eigensolver accuracy
    PrincipalStressEigen Eigen {};
    EngineeringBakedFracture::SolveSymmetricEigen3x3(120.0, 45.0, -30.0, 18.0, -9.0, 24.0, Eigen);
    FRONTIER_REQUIRE(Eigen.Sigma1 >= Eigen.Sigma2 && Eigen.Sigma2 >= Eigen.Sigma3);
    const double TraceDiff = std::abs((Eigen.Sigma1 + Eigen.Sigma2 + Eigen.Sigma3) - (120.0 + 45.0 - 30.0));
    FRONTIER_REQUIRE(TraceDiff < 1.0e-8);
    FRONTIER_REQUIRE(Eigen.VonMisesMPa > 100.0);

    // 4. Verify all 6 solvers on Concrete Monolith (RHR +Z Up, front impact on -Y face)
    const auto Monolith = ConvexPartitioning::ConstructSolidPolyhedron(FractureSolidCategory::Monolith);
    const FractureSolverMethod Solvers[] = {
        FractureSolverMethod::FastRuntimeBoussinesq,
        FractureSolverMethod::EngineeringBakedSolid,
        FractureSolverMethod::GriffithStressBudget,
        FractureSolverMethod::LefmWavefront,
        FractureSolverMethod::ClusteredVoronoi,
        FractureSolverMethod::DendriticSpallHull
    };

    FracturePartitionResult BakedConcreteResult {};
    FracturePartitionResult FastConcreteResult {};

    for (const auto Solver : Solvers)
    {
        FractureSpecification Spec {};
        Spec.Solver       = Solver;
        Spec.MaterialId   = "concrete";
        Spec.EnergyJoules = 3800.0;
        Spec.Ceiling      = 24u;
        Spec.MinimumSizeM = 0.04;
        Spec.Seed         = 42u;
        Spec.ImpactPoint  = { 0.05f, -0.48f, 0.20f };
        Spec.ImpactNormal = { 0.0f, -1.0f, 0.0f };

        const auto Result = ConvexPartitioning::ExecuteFracturePartition(Monolith, Spec);
        FRONTIER_REQUIRE(Result.Fragments.size() >= 2u);
        FRONTIER_REQUIRE(Result.Fragments.size() <= Spec.Ceiling);
        FRONTIER_REQUIRE(Result.Receipt.WatertightManifold);
        if (Solver == FractureSolverMethod::EngineeringBakedSolid)
        {
            FRONTIER_REQUIRE(Result.Receipt.VolumeDriftRatio < 1.0e-3);
            FRONTIER_REQUIRE(Result.HasEngineering);
            FRONTIER_REQUIRE(Result.EngineeringMetrics.GridDimension == 12u);
            FRONTIER_REQUIRE(Result.EngineeringMetrics.PeakSigma1MPa > 0.0);
            BakedConcreteResult = Result;
        }
        else
        {
            FRONTIER_REQUIRE(Result.Receipt.VolumeDriftRatio < 1.0e-4);
            if (Solver == FractureSolverMethod::FastRuntimeBoussinesq)
            {
                FastConcreteResult = Result;
            }
        }

        std::printf(
            "  Solver %-2d -> %2u shards, %3u bonds (%2u intact), volDrift=%.2e, crackArea=%.3f m^2, time=%.2f ms\n",
            static_cast<int>(Solver),
            Result.Receipt.FragmentCount,
            Result.Receipt.BondCount,
            Result.Receipt.IntactBondCount,
            Result.Receipt.VolumeDriftRatio,
            Result.Receipt.TotalCrackAreaM2,
            Result.Receipt.ComputeDurationMs);
    }

    // 5. Verify Tempered Glass spontaneous dicing (high stored energy breaks all bonds & fractures across entire pane)
    {
        const auto GlassPane = ConvexPartitioning::ConstructSolidPolyhedron(FractureSolidCategory::CurtainPane);
        FractureSpecification GlassSpec {};
        GlassSpec.Solver       = FractureSolverMethod::EngineeringBakedSolid;
        GlassSpec.MaterialId   = "tempered";
        GlassSpec.EnergyJoules = 450.0;
        GlassSpec.Ceiling      = 32u;
        GlassSpec.MinimumSizeM = 0.03;
        GlassSpec.Seed         = 77u;
        GlassSpec.ImpactPoint  = { 0.0f, -0.032f, 0.05f };
        GlassSpec.ImpactNormal = { 0.0f, -1.0f, 0.0f };

        const auto GlassResult = ConvexPartitioning::ExecuteFracturePartition(GlassPane, GlassSpec);
        FRONTIER_REQUIRE(GlassResult.Fragments.size() >= 6u);
        FRONTIER_REQUIRE(GlassResult.Receipt.IntactBondCount == 0u);
        FRONTIER_REQUIRE(GlassResult.Receipt.WatertightManifold);
    }

    // 6. Verify JoltPhysics v5.6.0 FractureSimulationRunner (ConvexHull + BreakableBond + Z=0 ground plane)
    {
        FractureSimulationRunner Runner {};
        FractureSimulationParameters SimParams {};
        SimParams.SolidWorldOffsetM  = { 0.0f, 0.0f, 1.0f };
        SimParams.GroundPlaneZ       = 0.0f;
        SimParams.EnableBonds        = true;
        SimParams.ApplyImpactImpulse = true;

        FRONTIER_REQUIRE(Runner.LoadPartitionResult(FastConcreteResult, SimParams));
        FRONTIER_REQUIRE(Runner.QueryLoaded());

        const auto InitTelemetry = Runner.InspectTelemetry();
        FRONTIER_REQUIRE(InitTelemetry.FragmentCount == FastConcreteResult.Fragments.size());

        for (uint32_t Frame = 0u; Frame < 45u; ++Frame)
        {
            Runner.Step(1.0 / 60.0);
        }

        const auto MidTelemetry = Runner.InspectTelemetry();
        FRONTIER_REQUIRE(MidTelemetry.CompletedSteps > 0ull);
        FRONTIER_REQUIRE(MidTelemetry.SimulatedSeconds > 0.70);

        bool AnyMoved = false;
        for (size_t I = 0u; I < Runner.InspectBodyStates().size(); ++I)
        {
            const auto& Body = Runner.InspectBodyStates()[I];
            FRONTIER_REQUIRE(Body.Token.Valid());
            FRONTIER_REQUIRE(Body.PositionMeters.Z > -0.25f); // Rests on or above Z=0 ground
            const auto InitPos = FastConcreteResult.Fragments[I].Centroid + SimParams.SolidWorldOffsetM;
            if (Mathematics::EvaluateDistance(Body.PositionMeters, InitPos) > 0.02f)
            {
                AnyMoved = true;
            }
        }
        FRONTIER_REQUIRE(AnyMoved);

        Runner.TriggerImpactImpulse(SimParams.SolidWorldOffsetM, { 0.0f, 140.0f, 25.0f }, 0.75f);
        Runner.Step(1.0 / 60.0);
        Runner.Clear();
        FRONTIER_REQUIRE(!Runner.QueryLoaded());
    }

    std::printf("[FractureGeometryValidation] ALL CHECKS PASSED.\n");
    return 0;
}

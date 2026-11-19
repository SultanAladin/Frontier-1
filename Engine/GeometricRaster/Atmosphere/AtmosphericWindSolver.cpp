/*============================================================================================================================================
                                                         ATMOSPHERICWINDSOLVER.CPP
============================================================================================================================================*/
// 🧩 Implementation of AtmosphericWindSolver coupling Global/Cloud wind, Ekman veer/gusts, ForceFieldAccumulator & FieldLatticeCascade

#include "Engine/GeometricRaster/Atmosphere/AtmosphericWindSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float SmoothStepWind(float Edge0, float Edge1, float X) noexcept
{
    const float T = std::clamp((X - Edge0) / std::max(1.0e-5f, Edge1 - Edge0), 0.0f, 1.0f);
    return T * T * (3.0f - 2.0f * T);
}

[[nodiscard]] SimulationSpace::ForceFields::SpatialVector3 ToSpatialVec(const Mathematics::Axis3D& V) noexcept
{
    return { V.X, V.Y, V.Z };
}

} // namespace

float AtmosphericWindSolver::EvaluateVolumeShapeMask(
    const Mathematics::Axis3D&   SampleWorld,
    const Mathematics::Axis3D&   CenterWorld,
    const Mathematics::Scalar3D& HalfExtentMeters,
    AtmosphericVolumeShape       Shape,
    float                        YawDegrees,
    uint32_t                     PrismSides,
    float                        EdgeSoftness) noexcept
{
    const float HX = std::max(0.1f, HalfExtentMeters.X);
    const float HY = std::max(0.1f, HalfExtentMeters.Y);
    const float HZ = std::max(0.1f, HalfExtentMeters.Z);

    const float Dx = SampleWorld.X - CenterWorld.X;
    const float Dy = SampleWorld.Y - CenterWorld.Y;
    const float Dz = SampleWorld.Z - CenterWorld.Z;

    const float YawRad = YawDegrees * Constants::DegToRadFloat;
    const float CosY   = std::cos(YawRad);
    const float SinY   = std::sin(YawRad);
    const float LX     =  Dx * CosY + Dy * SinY;
    const float LY     = -Dx * SinY + Dy * CosY;
    const float LZ     = Dz;

    const float NX = LX / HX;
    const float NY = LY / HY;
    const float NZ = LZ / HZ;

    float NormDist = 0.0f;
    switch (Shape)
    {
        case AtmosphericVolumeShape::Box:
            NormDist = std::max({ std::abs(NX), std::abs(NY), std::abs(NZ) });
            break;
        case AtmosphericVolumeShape::Sphere:
        {
            const float MinR = std::min({ HX, HY, HZ });
            NormDist = std::sqrt(LX * LX + LY * LY + LZ * LZ) / MinR;
            break;
        }
        case AtmosphericVolumeShape::Ellipsoid:
            NormDist = std::sqrt(NX * NX + NY * NY + NZ * NZ);
            break;
        case AtmosphericVolumeShape::Cylinder:
            NormDist = std::max(std::sqrt(NX * NX + NY * NY), std::abs(NZ));
            break;
        case AtmosphericVolumeShape::Cone:
        {
            const float T = std::clamp(0.5f * (1.0f - NZ), 0.0f, 1.0f);
            const float Rad = std::sqrt(NX * NX + NY * NY) / std::max(0.08f, T);
            NormDist = std::max(Rad, std::abs(NZ));
            break;
        }
        case AtmosphericVolumeShape::Diamond:
            NormDist = std::abs(NX) + std::abs(NY) + std::abs(NZ);
            break;
        case AtmosphericVolumeShape::Prism:
        {
            const float Sides = static_cast<float>(std::clamp(PrismSides, 3u, 16u));
            const float Theta = std::atan2(NY, NX);
            const float Sec   = (2.0f * Constants::PiFloat) / Sides;
            const float ModA  = (Theta - Sec * std::floor(Theta / Sec)) - 0.5f * Sec;
            const float Rad   = std::sqrt(NX * NX + NY * NY) * std::cos(ModA) / std::max(0.25f, std::cos(0.5f * Sec));
            NormDist = std::max(Rad, std::abs(NZ));
            break;
        }
        case AtmosphericVolumeShape::HalfSphere:
        {
            const float BaseDepth = (LZ < 0.0f) ? (-LZ / std::max(0.05f, 0.14f * HZ)) : 0.0f;
            const float DomeRad   = (LZ >= 0.0f)
                ? std::sqrt(NX * NX + NY * NY + NZ * NZ)
                : std::sqrt(NX * NX + NY * NY);
            NormDist = std::max(DomeRad, BaseDepth);
            break;
        }
    }

    const float Soft = std::clamp(EdgeSoftness, 0.04f, 0.92f);
    float Mask = 1.0f - SmoothStepWind(std::max(0.0f, 1.0f - Soft), 1.0f, NormDist);
    if (Shape == AtmosphericVolumeShape::HalfSphere)
    {
        const float RadialDecay = std::pow(std::clamp(1.0f - NormDist * 0.82f, 0.0f, 1.0f), 0.72f);
        Mask *= (0.35f + 0.65f * RadialDecay);
    }
    return Mask;
}

ResolvedWindSample AtmosphericWindSolver::ResolveWindSample(
    const AtmosphericWindParameters&                         GlobalWind,
    float                                                    DeckWindSpeedMps,
    float                                                    DeckWindBearingDeg,
    WindCouplingSource                                        Source,
    const Mathematics::Axis3D&                               WorldPoint,
    float                                                    AltitudeMeters,
    float                                                    TimeSeconds,
    const SimulationSpace::ForceFields::ForceFieldAccumulator* OptionalForceFields,
    const SimulationSpace::ForceFields::FieldLatticeCascade*   OptionalLattice) noexcept
{
    ResolvedWindSample Out {};
    const float AltKm = std::max(0.0f, AltitudeMeters) * 0.001f;

    float BaseSpeed   = GlobalWind.Enabled ? GlobalWind.SpeedMetersPerSecond : 0.0f;
    float BaseBearing = GlobalWind.BearingDegrees;
    if (Source == WindCouplingSource::CloudWind)
    {
        BaseSpeed   = DeckWindSpeedMps;
        BaseBearing = DeckWindBearingDeg;
    }

    if (Source != WindCouplingSource::ForceFieldCascade && BaseSpeed > 1.0e-4f)
    {
        // Apply Ekman directional veer with altitude & temporal gust modulation
        const float EffectiveBearing = BaseBearing + GlobalWind.VeerDegreesPerKm * AltKm;
        const float BearRad          = EffectiveBearing * Constants::DegToRadFloat;

        const float GustChaos   = 1.0f - 0.65f * std::clamp(GlobalWind.Steadiness, 0.0f, 1.0f);
        const float SpatialPhase = WorldPoint.X * 0.014f - WorldPoint.Y * 0.011f;
        const float GustWave    = 0.5f + 0.5f * std::sin(TimeSeconds * (2.0f * Constants::PiFloat) * std::max(0.05f, GlobalWind.GustFrequencyHz) + SpatialPhase);
        const float GustGain    = 1.0f + GlobalWind.GustStrength * GustChaos * (GustWave - 0.35f);
        const float ShearBoost  = 1.0f + GlobalWind.ShearPerKilometer * AltKm * (GlobalWind.AirShearAdvection ? 0.45f : 0.15f);
        const float SpeedNow    = BaseSpeed * std::max(0.0f, GustGain) * ShearBoost;

        Out.VelocityMetersPerSec.X += std::cos(BearRad) * SpeedNow;
        Out.VelocityMetersPerSec.Y += -std::sin(BearRad) * SpeedNow;
        Out.TurbulenceScalar       = std::clamp(GlobalWind.Turbulence + GlobalWind.GustStrength * 0.35f, 0.0f, 2.0f);
    }

    // Hook ForceFieldAccumulator WindField subcomponent (fast single-pass advection + horizontal curl vorticity)
    if (OptionalForceFields != nullptr && OptionalForceFields->QueryActiveSourceCount() > 0u)
    {
        const bool ProjectToClouds = (Source == WindCouplingSource::CloudWind ||
                                      Source == WindCouplingSource::LocalCloudWind ||
                                      Source == WindCouplingSource::GlobalWind ||
                                      Source == WindCouplingSource::ForceFieldCascade);
        const auto WF = OptionalForceFields->QueryWindFieldSample(
            ToSpatialVec(WorldPoint),
            static_cast<double>(TimeSeconds),
            ProjectToClouds);

        const float VX = WF.AdvectionVelocity.X + WF.LinearAcceleration.X * 0.5f;
        const float VY = WF.AdvectionVelocity.Y + WF.LinearAcceleration.Y * 0.5f;
        const float VZ = WF.AdvectionVelocity.Z + WF.LinearAcceleration.Z * 0.5f;
        Out.VelocityMetersPerSec.X += VX;
        Out.VelocityMetersPerSec.Y += VY;
        Out.VelocityMetersPerSec.Z += VZ;

        // Fast horizontal vorticity estimate (2-tap central-adjacent delta + direct rotational torque)
        constexpr float DeltaH = 1.5f;
        const auto WFx = OptionalForceFields->QueryWindFieldSample(
            { WorldPoint.X + DeltaH, WorldPoint.Y, WorldPoint.Z, 0.0f },
            static_cast<double>(TimeSeconds),
            ProjectToClouds);
        const auto WFy = OptionalForceFields->QueryWindFieldSample(
            { WorldPoint.X, WorldPoint.Y + DeltaH, WorldPoint.Z, 0.0f },
            static_cast<double>(TimeSeconds),
            ProjectToClouds);
        const float dVy_dx = ((WFx.AdvectionVelocity.Y + WFx.LinearAcceleration.Y * 0.5f) - VY) / DeltaH;
        const float dVx_dy = ((WFy.AdvectionVelocity.X + WFy.LinearAcceleration.X * 0.5f) - VX) / DeltaH;
        const float TorqueMag = SimulationSpace::ForceFields::VectorLength(WF.AngularTorque);
        const float LocalCurl = std::abs(dVy_dx - dVx_dy) + TorqueMag * 0.25f;

        Out.CurlScalar       += LocalCurl;
        Out.TurbulenceScalar  = std::max(Out.TurbulenceScalar, std::clamp(LocalCurl * 0.35f, 0.0f, 2.0f));
    }

    // Hook 4-tier FieldLatticeCascade (Near/Zone/District/Weather) when provided
    if (OptionalLattice != nullptr)
    {
        const uint32_t Tier = (Source == WindCouplingSource::CloudWind || Source == WindCouplingSource::GlobalWind)
                                  ? 3u
                                  : ((Source == WindCouplingSource::AerialFogWind) ? 2u : 1u);
        const auto Voxel = OptionalLattice->SampleTierTrilinear(Tier, ToSpatialVec(WorldPoint));
        Out.VelocityMetersPerSec.X += Voxel.VelocityX;
        Out.VelocityMetersPerSec.Y += Voxel.VelocityY;
        Out.VelocityMetersPerSec.Z += Voxel.VelocityZ;
        Out.CurlScalar             += Voxel.CurlScalar;
        Out.TurbulenceScalar       = std::max(Out.TurbulenceScalar, std::clamp(Voxel.CurlScalar * 0.35f, 0.0f, 2.0f));
    }

    return Out;
}

Mathematics::Axis3D AtmosphericWindSolver::AdvectWorldPointBackward(
    const AtmosphericWindParameters&                           GlobalWind,
    float                                                      DeckWindSpeedMps,
    float                                                      DeckWindBearingDeg,
    WindCouplingSource                                          Source,
    const Mathematics::Axis3D&                                 WorldPoint,
    float                                                      AltitudeMeters,
    float                                                      TotalSeconds,
    const SimulationSpace::ForceFields::ForceFieldAccumulator* OptionalForceFields,
    const SimulationSpace::ForceFields::FieldLatticeCascade*   OptionalLattice,
    float*                                                     OutCurlScalar) noexcept
{
    if (std::abs(TotalSeconds) <= 1.0e-4f)
    {
        if (OutCurlScalar != nullptr)
        {
            const auto W0 = ResolveWindSample(
                GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, Source,
                WorldPoint, AltitudeMeters, 0.0f, OptionalForceFields, OptionalLattice);
            *OutCurlScalar = W0.CurlScalar;
        }
        return WorldPoint;
    }

    // 4-step RK2 Semi-Lagrangian backward streamline advection (strictly monotone forward flow, never reverses)
    constexpr uint32_t Steps = 4u;
    const float Dt = TotalSeconds / static_cast<float>(Steps);
    Mathematics::Axis3D Pos = WorldPoint;
    float CurlAcc = 0.0f;

    for (uint32_t Step = 0u; Step < Steps; ++Step)
    {
        const float TNow = TotalSeconds - static_cast<float>(Step) * Dt;
        const auto K1 = ResolveWindSample(
            GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, Source,
            Pos, AltitudeMeters, TNow, OptionalForceFields, OptionalLattice);
        CurlAcc = std::max(CurlAcc, K1.CurlScalar);

        const Mathematics::Axis3D MidPos {
            Pos.X - 0.5f * Dt * K1.VelocityMetersPerSec.X,
            Pos.Y - 0.5f * Dt * K1.VelocityMetersPerSec.Y,
            Pos.Z - 0.5f * Dt * K1.VelocityMetersPerSec.Z,
        };
        const auto K2 = ResolveWindSample(
            GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, Source,
            MidPos, AltitudeMeters, TNow - 0.5f * Dt, OptionalForceFields, OptionalLattice);
        CurlAcc = std::max(CurlAcc, K2.CurlScalar);

        Pos.X -= Dt * K2.VelocityMetersPerSec.X;
        Pos.Y -= Dt * K2.VelocityMetersPerSec.Y;
        Pos.Z -= Dt * K2.VelocityMetersPerSec.Z;
    }

    if (OutCurlScalar != nullptr)
    {
        *OutCurlScalar = CurlAcc;
    }
    return Pos;
}

} // namespace Frontier::GeometricRaster

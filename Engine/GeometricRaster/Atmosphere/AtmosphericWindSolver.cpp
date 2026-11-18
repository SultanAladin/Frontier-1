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
    WindBindingSource                                        Source,
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
    if (Source == WindBindingSource::CloudWind)
    {
        BaseSpeed   = DeckWindSpeedMps;
        BaseBearing = DeckWindBearingDeg;
    }

    if (Source != WindBindingSource::ForceFieldCascade && BaseSpeed > 1.0e-4f)
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

    // Hook ForceFieldAccumulator (all 19 analytical ForceModels + WindFieldSubcomponent projection)
    if (OptionalForceFields != nullptr && OptionalForceFields->QueryActiveSourceCount() > 0u)
    {
        const bool ProjectToCloud = (Source == WindBindingSource::CloudWind ||
                                     Source == WindBindingSource::GlobalWind ||
                                     Source == WindBindingSource::ForceFieldCascade);
        const auto Total = OptionalForceFields->QueryWindFieldSample(
            ToSpatialVec(WorldPoint),
            static_cast<double>(TimeSeconds),
            ProjectToCloud);

        const float VelX = Total.AdvectionVelocity.X + Total.LinearAcceleration.X * 0.5f;
        const float VelY = Total.AdvectionVelocity.Y + Total.LinearAcceleration.Y * 0.5f;
        const float VelZ = Total.AdvectionVelocity.Z + Total.LinearAcceleration.Z * 0.5f;

        // Lightweight 2-tap horizontal vorticity curl + explicit AngularTorque
        constexpr float Eps = 1.5f;
        const auto DxP = OptionalForceFields->QueryWindFieldSample(
            { WorldPoint.X + Eps, WorldPoint.Y, WorldPoint.Z, 0.0f }, static_cast<double>(TimeSeconds), ProjectToCloud);
        const auto DyP = OptionalForceFields->QueryWindFieldSample(
            { WorldPoint.X, WorldPoint.Y + Eps, WorldPoint.Z, 0.0f }, static_cast<double>(TimeSeconds), ProjectToCloud);
        const float dVy_dx = ((DxP.AdvectionVelocity.Y + DxP.LinearAcceleration.Y * 0.5f) - VelY) / Eps;
        const float dVx_dy = ((DyP.AdvectionVelocity.X + DyP.LinearAcceleration.X * 0.5f) - VelX) / Eps;
        const float TorqueMag = std::sqrt(Total.AngularTorque.X * Total.AngularTorque.X +
                                          Total.AngularTorque.Y * Total.AngularTorque.Y +
                                          Total.AngularTorque.Z * Total.AngularTorque.Z);
        const float CurlEst = std::abs(dVy_dx - dVx_dy) + TorqueMag;

        Out.VelocityMetersPerSec.X += VelX;
        Out.VelocityMetersPerSec.Y += VelY;
        Out.VelocityMetersPerSec.Z += VelZ;
        Out.CurlScalar             += CurlEst;
        Out.TurbulenceScalar       = std::max(Out.TurbulenceScalar, std::clamp(CurlEst * 0.35f, 0.0f, 2.0f));
    }

    // Hook 4-tier FieldLatticeCascade (Near/Zone/District/Weather) when provided
    if (OptionalLattice != nullptr)
    {
        const uint32_t Tier = (Source == WindBindingSource::CloudWind || Source == WindBindingSource::GlobalWind)
                                  ? 3u
                                  : ((Source == WindBindingSource::AerialFogWind) ? 2u : 1u);
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
    const AtmosphericWindParameters&                         GlobalWind,
    float                                                    DeckWindSpeedMps,
    float                                                    DeckWindBearingDeg,
    WindBindingSource                                        Source,
    const Mathematics::Axis3D&                               WorldPoint,
    float                                                    AltitudeMeters,
    float                                                    TimeSeconds,
    float*                                                   OutSwirlScalar,
    float*                                                   OutSpeedScalar,
    const SimulationSpace::ForceFields::ForceFieldAccumulator* OptionalForceFields,
    const SimulationSpace::ForceFields::FieldLatticeCascade*   OptionalLattice) noexcept
{
    if (std::abs(TimeSeconds) < 1.0e-4f)
    {
        const ResolvedWindSample S = ResolveWindSample(
            GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, Source,
            WorldPoint, AltitudeMeters, TimeSeconds, OptionalForceFields, OptionalLattice);
        if (OutSwirlScalar != nullptr) *OutSwirlScalar = S.CurlScalar;
        if (OutSpeedScalar != nullptr)
        {
            *OutSpeedScalar = std::sqrt(
                S.VelocityMetersPerSec.X * S.VelocityMetersPerSec.X +
                S.VelocityMetersPerSec.Y * S.VelocityMetersPerSec.Y +
                S.VelocityMetersPerSec.Z * S.VelocityMetersPerSec.Z);
        }
        return WorldPoint;
    }

    constexpr uint32_t Steps = 4u;
    const float Dt           = TimeSeconds / static_cast<float>(Steps);
    Mathematics::Axis3D Pos  = WorldPoint;
    float AccSwirl           = 0.0f;
    float AccSpeed           = 0.0f;

    for (uint32_t I = 0u; I < Steps; ++I)
    {
        const float TNow = TimeSeconds - static_cast<float>(I) * Dt;
        const ResolvedWindSample W1 = ResolveWindSample(
            GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, Source,
            Pos, AltitudeMeters, TNow, OptionalForceFields, OptionalLattice);
        const Mathematics::Axis3D Mid {
            Pos.X - W1.VelocityMetersPerSec.X * (0.5f * Dt),
            Pos.Y - W1.VelocityMetersPerSec.Y * (0.5f * Dt),
            Pos.Z - W1.VelocityMetersPerSec.Z * (0.5f * Dt)
        };
        const ResolvedWindSample W2 = ResolveWindSample(
            GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, Source,
            Mid, AltitudeMeters, TNow - 0.5f * Dt, OptionalForceFields, OptionalLattice);

        Pos.X -= W2.VelocityMetersPerSec.X * Dt;
        Pos.Y -= W2.VelocityMetersPerSec.Y * Dt;
        Pos.Z -= W2.VelocityMetersPerSec.Z * Dt;
        AccSwirl += W2.CurlScalar;
        AccSpeed += std::sqrt(
            W2.VelocityMetersPerSec.X * W2.VelocityMetersPerSec.X +
            W2.VelocityMetersPerSec.Y * W2.VelocityMetersPerSec.Y +
            W2.VelocityMetersPerSec.Z * W2.VelocityMetersPerSec.Z);
    }

    if (OutSwirlScalar != nullptr) *OutSwirlScalar = AccSwirl / static_cast<float>(Steps);
    if (OutSpeedScalar != nullptr) *OutSpeedScalar = AccSpeed / static_cast<float>(Steps);
    return Pos;
}

} // namespace Frontier::GeometricRaster

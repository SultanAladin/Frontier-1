/*============================================================================================================================================
                                                        VOLUMETRICFOGSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Analytical Height Fog, Spectral Aerial Perspective, and Wind-Advected Multi-Shape LocalFogVolume raymarcher

#include "Engine/GeometricRaster/Atmosphere/VolumetricFogSequence.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

[[nodiscard]] float FracFog(float X) noexcept
{
    return X - std::floor(X);
}

[[nodiscard]] float HashFogInt3(int32_t IX, int32_t IY, int32_t IZ) noexcept
{
    uint32_t N = static_cast<uint32_t>(IX * 374761393 + IY * 668265263 + IZ * 2147483647);
    N = (N ^ (N >> 13u)) * 1274126177u;
    N = N ^ (N >> 16u);
    return static_cast<float>(N) * (1.0f / 4294967296.0f);
}

[[nodiscard]] float SmoothFogNoise3(float X, float Y, float Z) noexcept
{
    const int32_t IX = static_cast<int32_t>(std::floor(X));
    const int32_t IY = static_cast<int32_t>(std::floor(Y));
    const int32_t IZ = static_cast<int32_t>(std::floor(Z));
    const float FX   = X - static_cast<float>(IX);
    const float FY   = Y - static_cast<float>(IY);
    const float FZ   = Z - static_cast<float>(IZ);
    const float UX   = FX * FX * (3.0f - 2.0f * FX);
    const float UY   = FY * FY * (3.0f - 2.0f * FY);
    const float UZ   = FZ * FZ * (3.0f - 2.0f * FZ);

    const auto Mix = [](float A, float B, float T) noexcept -> float { return A + (B - A) * T; };
    const float N000 = HashFogInt3(IX, IY, IZ),         N100 = HashFogInt3(IX + 1, IY, IZ);
    const float N010 = HashFogInt3(IX, IY + 1, IZ),     N110 = HashFogInt3(IX + 1, IY + 1, IZ);
    const float N001 = HashFogInt3(IX, IY, IZ + 1),     N101 = HashFogInt3(IX + 1, IY, IZ + 1);
    const float N011 = HashFogInt3(IX, IY + 1, IZ + 1), N111 = HashFogInt3(IX + 1, IY + 1, IZ + 1);

    return Mix(
        Mix(Mix(N000, N100, UX), Mix(N010, N110, UX), UY),
        Mix(Mix(N001, N101, UX), Mix(N011, N111, UX), UY),
        UZ);
}

[[nodiscard]] float FogFbm3(float X, float Y, float Z) noexcept
{
    float Sum  = 0.0f;
    float Amp  = 0.54f;
    float Freq = 1.0f;
    for (uint32_t Oct = 0u; Oct < 4u; ++Oct)
    {
        Sum  += Amp * SmoothFogNoise3(X * Freq, Y * Freq, Z * Freq);
        Freq *= 2.03f;
        Amp  *= 0.46f;
    }
    return Sum;
}

[[nodiscard]] float EvaluateFogPhaseHG(float CosTheta, float G) noexcept
{
    const float ClampedG = std::clamp(G, -0.89f, 0.89f);
    const float G2       = ClampedG * ClampedG;
    const float Den      = std::max(1.0e-3f, 1.0f + G2 - 2.0f * ClampedG * CosTheta);
    return (1.0f - G2) / (4.0f * Constants::PiFloat * std::pow(Den, 1.5f));
}

[[nodiscard]] float EvaluateSingleLocalFogDensity(
    const LocalFogVolume&                                    Vol,
    const Mathematics::Axis3D&                               P,
    const AtmosphericWindParameters&                         GlobalWind,
    float                                                    DeckWindSpeedMps,
    float                                                    DeckWindBearingDeg,
    float                                                    SimulationSeconds,
    const ResolvedWindSample&                                CenterWind,
    const SimulationSpace::ForceFields::ForceFieldAccumulator* OptionalForceFields,
    const SimulationSpace::ForceFields::FieldLatticeCascade*   OptionalLattice,
    float&                                                   OutCurlHighlight) noexcept
{
    OutCurlHighlight = 0.0f;
    const float ShapeMask = AtmosphericWindSolver::EvaluateVolumeShapeMask(
        P, Vol.VolumeCenterMeters, Vol.HalfExtentMeters, Vol.Shape,
        Vol.YawDegrees, Vol.PrismSides, Vol.EdgeSoftness);
    if (ShapeMask <= 1.0e-4f)
    {
        return 0.0f;
    }

    ResolvedWindSample Wind = CenterWind;
    const bool SpatialForces = (OptionalForceFields != nullptr && OptionalForceFields->QueryActiveSourceCount() > 0u) ||
                               (OptionalLattice != nullptr);
    if (Vol.FollowWind && SpatialForces)
    {
        Wind = AtmosphericWindSolver::ResolveWindSample(
            GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, Vol.WindSource,
            P, P.Z, SimulationSeconds, OptionalForceFields, OptionalLattice);
    }

    const float Speed = std::sqrt(
        Wind.VelocityMetersPerSec.X * Wind.VelocityMetersPerSec.X +
        Wind.VelocityMetersPerSec.Y * Wind.VelocityMetersPerSec.Y +
        Wind.VelocityMetersPerSec.Z * Wind.VelocityMetersPerSec.Z);

    // Spatial streamline advection + helical vortex twist directly inside the fog domain
    const float AdvScale = 0.72f;
    float AdvX = P.X - Wind.VelocityMetersPerSec.X * SimulationSeconds * AdvScale;
    float AdvY = P.Y - Wind.VelocityMetersPerSec.Y * SimulationSeconds * AdvScale;
    float AdvZ = P.Z - Wind.VelocityMetersPerSec.Z * SimulationSeconds * 0.40f;
    float SpiralMod = 1.0f;
    if (Wind.CurlScalar > 1.0e-3f)
    {
        const float Dx = P.X - Vol.VolumeCenterMeters.X;
        const float Dy = P.Y - Vol.VolumeCenterMeters.Y;
        const float RadXY = std::sqrt(Dx * Dx + Dy * Dy);
        const float Theta = std::atan2(Dy, Dx);
        const float Twist = std::clamp(Wind.CurlScalar * 0.18f, 0.0f, 3.6f) *
                            (0.45f * SimulationSeconds - 0.08f * RadXY);
        const float Cs = std::cos(Twist), Sn = std::sin(Twist);
        AdvX = Vol.VolumeCenterMeters.X + (Dx * Cs - Dy * Sn) - Wind.VelocityMetersPerSec.X * SimulationSeconds * 0.42f;
        AdvY = Vol.VolumeCenterMeters.Y + (Dx * Sn + Dy * Cs) - Wind.VelocityMetersPerSec.Y * SimulationSeconds * 0.42f;
        AdvZ += std::sin(Theta * 2.0f + Twist) * 1.15f;
        SpiralMod = 0.62f + 0.72f * (0.5f + 0.5f * std::cos(Theta * 2.0f - RadXY * 0.22f + SimulationSeconds * 1.1f));
    }

    const float LScale = 1.0f / std::max(1.5f, Vol.FeatureScaleMeters);
    const float N      = FogFbm3(AdvX * LScale, AdvY * LScale, AdvZ * LScale);
    const float Thresh = 1.0f - std::clamp(Vol.Coverage, 0.0f, 1.0f);
    float BaseDen = std::max(0.0f, (N - Thresh) / std::max(0.15f, Vol.Coverage));

    // ForceField & wind-stream internal sculpting: carves wake/eye channels in fast jets & condenses curl filaments
    if (Vol.FollowWind && (SpatialForces || Speed > 0.25f))
    {
        const float StreamWave  = 0.5f + 0.5f * std::sin((AdvX * 0.30f - AdvY * 0.24f) + Speed * 0.35f);
        const float CurlBoost   = 1.0f + std::clamp(Wind.CurlScalar * 0.28f, 0.0f, 1.15f) * SpiralMod;
        const float JetClear    = 1.0f - std::clamp((Speed - 6.0f) * 0.036f, 0.0f, 0.64f) * (1.0f - StreamWave * 0.45f);
        BaseDen *= std::clamp(CurlBoost * JetClear, 0.22f, 2.15f);
        OutCurlHighlight = std::clamp(Wind.CurlScalar * 0.14f, 0.0f, 0.40f);
    }

    return BaseDen * ShapeMask * Vol.Density * 0.075f;
}

} // namespace

float VolumetricFogSequence::EvaluateHeightFogOpticalDepth(
    float                         OriginZ,
    float                         DirZ,
    float                         SegmentMeters,
    const HeightFogConfiguration& Config) noexcept
{
    if (!Config.Enabled || Config.Density <= 1.0e-6f || SegmentMeters <= 1.0e-4f)
    {
        return 0.0f;
    }
    const float Falloff  = std::max(10.0f, Config.FalloffHeightMeters);
    const float Sigma0   = std::clamp(Config.Density, 0.0f, 0.25f);
    const float StartRel = std::max(-4.0f, (OriginZ - Config.BaseAltitudeMeters) / Falloff);
    const float StartRho = Sigma0 * std::exp(-std::max(0.0f, StartRel));
    const float K        = (DirZ * SegmentMeters) / Falloff;
    if (std::abs(K) < 1.0e-3f)
    {
        // Singularity-free Taylor series for near-horizontal rays
        return StartRho * SegmentMeters * (1.0f - 0.5f * K + (K * K) / 6.0f);
    }
    const float SafeK = std::clamp(K, -24.0f, 24.0f);
    return std::max(0.0f, StartRho * SegmentMeters * ((1.0f - std::exp(-SafeK)) / SafeK));
}

float VolumetricFogSequence::ComputeTwoPercentVisibilityRangeMeters(
    float                         AltitudeMeters,
    const HeightFogConfiguration& HeightFog) noexcept
{
    const float Falloff = std::max(10.0f, HeightFog.FalloffHeightMeters);
    const float Sigma   = std::max(0.0f, HeightFog.Density) *
                          std::exp(-std::max(0.0f, AltitudeMeters - HeightFog.BaseAltitudeMeters) / Falloff);
    if (Sigma <= 1.0e-7f)
    {
        return 1.0e6f;
    }
    return -std::log(0.02f) / Sigma;
}

float VolumetricFogSequence::EvaluateFogMediumDensityAtPoint(
    const Mathematics::Axis3D&                               WorldPoint,
    const AtmosphericFogConfiguration&                       FogConfig,
    const AtmosphericWindParameters&                         GlobalWind,
    float                                                    DeckWindSpeedMps,
    float                                                    DeckWindBearingDeg,
    float                                                    SimulationSeconds,
    const SimulationSpace::ForceFields::ForceFieldAccumulator* OptionalForceFields,
    const SimulationSpace::ForceFields::FieldLatticeCascade*   OptionalLattice) noexcept
{
    float Sigma = 0.0f;
    if (FogConfig.HeightFog.Enabled && FogConfig.HeightFog.Density > 1.0e-6f)
    {
        const float Falloff = std::max(10.0f, FogConfig.HeightFog.FalloffHeightMeters);
        const float RelZ    = std::max(0.0f, (WorldPoint.Z - FogConfig.HeightFog.BaseAltitudeMeters) / Falloff);
        Sigma += std::clamp(FogConfig.HeightFog.Density, 0.0f, 0.25f) * std::exp(-RelZ);
    }

    for (const auto& Vol : FogConfig.LocalFogs)
    {
        if (!Vol.Enabled || Vol.Density <= 1.0e-4f || Vol.Coverage <= 1.0e-4f)
        {
            continue;
        }
        const float MaxExtent = std::max({ Vol.HalfExtentMeters.X, Vol.HalfExtentMeters.Y, Vol.HalfExtentMeters.Z }) * 1.45f;
        const float MinZ = (Vol.Shape == AtmosphericVolumeShape::HalfSphere)
            ? (Vol.VolumeCenterMeters.Z - 0.14f * Vol.HalfExtentMeters.Z)
            : (Vol.VolumeCenterMeters.Z - Vol.HalfExtentMeters.Z);
        if (WorldPoint.X < Vol.VolumeCenterMeters.X - MaxExtent || WorldPoint.X > Vol.VolumeCenterMeters.X + MaxExtent ||
            WorldPoint.Y < Vol.VolumeCenterMeters.Y - MaxExtent || WorldPoint.Y > Vol.VolumeCenterMeters.Y + MaxExtent ||
            WorldPoint.Z < MinZ || WorldPoint.Z > Vol.VolumeCenterMeters.Z + Vol.HalfExtentMeters.Z)
        {
            continue;
        }
        const ResolvedWindSample CenterWind = Vol.FollowWind
            ? AtmosphericWindSolver::ResolveWindSample(
                  GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, Vol.WindSource,
                  Vol.VolumeCenterMeters, Vol.VolumeCenterMeters.Z, SimulationSeconds,
                  OptionalForceFields, OptionalLattice)
            : ResolvedWindSample {};
        float DummyCurl = 0.0f;
        Sigma += EvaluateSingleLocalFogDensity(
            Vol, WorldPoint, GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg,
            SimulationSeconds, CenterWind, OptionalForceFields, OptionalLattice, DummyCurl);
    }

    return Sigma;
}

FogEvaluationResult VolumetricFogSequence::EvaluateVolumetricFog(
    const Mathematics::Axis3D&                               RayOriginWorld,
    const Mathematics::Axis3D&                               RayDirection,
    float                                                    MaxDistanceMeters,
    const Mathematics::Axis3D&                               SunDirection,
    const Mathematics::LinearRGB&                            SolarIrradiance,
    const Mathematics::LinearRGB&                            SkyRadiance,
    const AtmosphericFogConfiguration&                       FogConfig,
    const AtmosphericWindParameters&                         GlobalWind,
    float                                                    DeckWindSpeedMps,
    float                                                    DeckWindBearingDeg,
    float                                                    SimulationSeconds,
    const SimulationSpace::ForceFields::ForceFieldAccumulator* OptionalForceFields,
    const SimulationSpace::ForceFields::FieldLatticeCascade*   OptionalLattice,
    float                                                    PixelJitter01) noexcept
{
    FogEvaluationResult Out {};
    const float SafeDist = std::max(0.0f, MaxDistanceMeters);
    if (SafeDist <= 1.0e-3f)
    {
        return Out;
    }

    const float CosTheta = Mathematics::EvaluateDot(RayDirection, SunDirection);
    const float Ign      = (PixelJitter01 >= 0.0f)
                               ? std::clamp(PixelJitter01, 0.0f, 1.0f)
                               : FracFog(52.9829189f * FracFog(RayDirection.X * 193.7f + RayDirection.Y * 371.4f + RayDirection.Z * 547.8f));
    const float Jitter   = 0.5f + 0.75f * (Ign - 0.5f);

    // 1. Exponential Height Fog (with optional wind-driven boundary undulation)
    if (FogConfig.HeightFog.Enabled && FogConfig.HeightFog.Density > 1.0e-6f)
    {
        float TauH = EvaluateHeightFogOpticalDepth(
            RayOriginWorld.Z, RayDirection.Z, SafeDist, FogConfig.HeightFog);
        if (FogConfig.HeightFog.FollowWind)
        {
            const auto W = AtmosphericWindSolver::ResolveWindSample(
                GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, FogConfig.HeightFog.WindSource,
                RayOriginWorld + RayDirection * std::min(SafeDist * 0.35f, 80.0f),
                RayOriginWorld.Z, SimulationSeconds, OptionalForceFields, OptionalLattice);
            const float Ripple = 0.88f + 0.24f * SmoothFogNoise3(
                (RayOriginWorld.X - W.VelocityMetersPerSec.X * SimulationSeconds) * 0.03f,
                (RayOriginWorld.Y - W.VelocityMetersPerSec.Y * SimulationSeconds) * 0.03f,
                SimulationSeconds * 0.15f);
            TauH *= Ripple;
        }
        const float TransH = std::exp(-TauH);
        const float ScatH  = 1.0f - TransH;
        const float MieHg  = EvaluateFogPhaseHG(CosTheta, 0.62f) * FogConfig.HeightFog.SunScatterIntensity * 0.45f;
        const Mathematics::LinearRGB FogLit {
            FogConfig.HeightFog.HeightColour.R * (SkyRadiance.R * 0.62f + SolarIrradiance.R * (0.045f + MieHg)),
            FogConfig.HeightFog.HeightColour.G * (SkyRadiance.G * 0.62f + SolarIrradiance.G * (0.045f + MieHg)),
            FogConfig.HeightFog.HeightColour.B * (SkyRadiance.B * 0.62f + SolarIrradiance.B * (0.045f + MieHg)),
        };
        Out.InScatterHdr.R += Out.Transmittance * FogLit.R * ScatH;
        Out.InScatterHdr.G += Out.Transmittance * FogLit.G * ScatH;
        Out.InScatterHdr.B += Out.Transmittance * FogLit.B * ScatH;
        Out.Transmittance  *= TransH;
    }

    // 2. Spectral Aerial Perspective (distance-gated beyond StartDistanceMeters)
    if (FogConfig.AerialFog.Enabled && FogConfig.AerialFog.DensityMultiplier > 1.0e-5f)
    {
        const float Travel = std::max(0.0f, SafeDist - std::max(0.0f, FogConfig.AerialFog.StartDistanceMeters));
        if (Travel > 1.0e-3f)
        {
            const float AltFactor = std::exp(-std::max(0.0f, RayOriginWorld.Z) / std::max(100.0f, FogConfig.AerialFog.ScaleHeightMeters));
            const float SigmaA    = FogConfig.AerialFog.DensityMultiplier * 0.00022f * AltFactor;
            const float TransA    = std::exp(-SigmaA * Travel);
            const float ScatA     = 1.0f - TransA;
            const float MieBlend  = std::clamp(FogConfig.AerialFog.MieBlend, 0.0f, 1.0f);
            const float MiePhase  = EvaluateFogPhaseHG(CosTheta, 0.72f);
            const Mathematics::LinearRGB AerialCol {
                SkyRadiance.R * (1.0f - 0.30f * MieBlend) + SolarIrradiance.R * MieBlend * (0.04f + MiePhase * 0.42f),
                SkyRadiance.G * (1.0f - 0.30f * MieBlend) + SolarIrradiance.G * MieBlend * (0.04f + MiePhase * 0.42f),
                SkyRadiance.B * (1.0f - 0.30f * MieBlend) + SolarIrradiance.B * MieBlend * (0.04f + MiePhase * 0.42f),
            };
            Out.InScatterHdr.R += Out.Transmittance * AerialCol.R * ScatA;
            Out.InScatterHdr.G += Out.Transmittance * AerialCol.G * ScatA;
            Out.InScatterHdr.B += Out.Transmittance * AerialCol.B * ScatA;
            Out.Transmittance  *= TransA;
        }
    }

    // 3. Wind-driven Multi-Shape LocalFogVolumes (32 jittered steps clipped against MaxDistanceMeters)
    for (const auto& Vol : FogConfig.LocalFogs)
    {
        if (!Vol.Enabled || Vol.Density <= 1.0e-4f || Vol.Coverage <= 1.0e-4f)
        {
            continue;
        }

        const float MaxExtent = std::max({ Vol.HalfExtentMeters.X, Vol.HalfExtentMeters.Y, Vol.HalfExtentMeters.Z }) * 1.45f;
        const float MinZOff   = (Vol.Shape == AtmosphericVolumeShape::HalfSphere)
            ? (0.14f * Vol.HalfExtentMeters.Z)
            : Vol.HalfExtentMeters.Z;
        const Mathematics::Axis3D BoxMin = Vol.VolumeCenterMeters - Mathematics::Axis3D{ MaxExtent, MaxExtent, MinZOff };
        const Mathematics::Axis3D BoxMax = Vol.VolumeCenterMeters + Mathematics::Axis3D{ MaxExtent, MaxExtent, Vol.HalfExtentMeters.Z };

        const auto SafeInv = [](float V) noexcept -> float { return (std::abs(V) > 1.0e-7f) ? (1.0f / V) : 1.0e7f; };
        const float InvX = SafeInv(RayDirection.X), InvY = SafeInv(RayDirection.Y), InvZ = SafeInv(RayDirection.Z);
        const float TX0 = (BoxMin.X - RayOriginWorld.X) * InvX, TX1 = (BoxMax.X - RayOriginWorld.X) * InvX;
        const float TY0 = (BoxMin.Y - RayOriginWorld.Y) * InvY, TY1 = (BoxMax.Y - RayOriginWorld.Y) * InvY;
        const float TZ0 = (BoxMin.Z - RayOriginWorld.Z) * InvZ, TZ1 = (BoxMax.Z - RayOriginWorld.Z) * InvZ;
        const float TEnter = std::max({ 0.05f, std::min(TX0, TX1), std::min(TY0, TY1), std::min(TZ0, TZ1) });
        const float TExit  = std::min({ SafeDist, std::max(TX0, TX1), std::max(TY0, TY1), std::max(TZ0, TZ1) });
        if (TExit <= TEnter)
        {
            continue;
        }

        const ResolvedWindSample Wind = Vol.FollowWind
            ? AtmosphericWindSolver::ResolveWindSample(
                  GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg, Vol.WindSource,
                  Vol.VolumeCenterMeters, Vol.VolumeCenterMeters.Z, SimulationSeconds,
                  OptionalForceFields, OptionalLattice)
            : ResolvedWindSample {};

        constexpr uint32_t Steps = 32u;
        const float Dt     = (TExit - TEnter) / static_cast<float>(Steps);
        const float Phase  = 0.72f * EvaluateFogPhaseHG(CosTheta, Vol.AnisotropyG) +
                             0.28f * EvaluateFogPhaseHG(CosTheta, -0.18f);

        for (uint32_t S = 0u; S < Steps; ++S)
        {
            const float T = TEnter + (static_cast<float>(S) + Jitter) * Dt;
            const Mathematics::Axis3D P = RayOriginWorld + RayDirection * T;
            float CurlHighlight = 0.0f;
            const float Den = EvaluateSingleLocalFogDensity(
                Vol, P, GlobalWind, DeckWindSpeedMps, DeckWindBearingDeg,
                SimulationSeconds, Wind, OptionalForceFields, OptionalLattice, CurlHighlight);
            if (Den > 1.0e-5f)
            {
                const float SegT    = std::exp(-Den * Dt);
                const float Scatter = 1.0f - SegT;
                const float HeightNorm = std::clamp((P.Z - BoxMin.Z) /
                                                    std::max(0.2f, BoxMax.Z - BoxMin.Z), 0.0f, 1.0f);
                const float Amb = 0.42f + 0.58f * HeightNorm + CurlHighlight * 0.25f;
                Out.InScatterHdr.R += Out.Transmittance * Vol.FogTint.R * (SolarIrradiance.R * Phase * 1.45f + SkyRadiance.R * Amb * 0.75f) * Scatter;
                Out.InScatterHdr.G += Out.Transmittance * Vol.FogTint.G * (SolarIrradiance.G * Phase * 1.45f + SkyRadiance.G * Amb * 0.75f) * Scatter;
                Out.InScatterHdr.B += Out.Transmittance * Vol.FogTint.B * (SolarIrradiance.B * Phase * 1.45f + SkyRadiance.B * Amb * 0.75f) * Scatter;
                Out.Transmittance  *= SegT;
                if (Out.Transmittance < 0.01f)
                {
                    break;
                }
            }
        }
    }

    return Out;
}

} // namespace Frontier::GeometricRaster

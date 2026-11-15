/*============================================================================================================================================
                                                               PARTICLERIG.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleRig GPU uniform packing and Studio Default rig factory

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleRig.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourRampSampler.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

ParticleRigUniform ParticleRig::PackRigUniform(
    uint32_t SlotOffset,
    uint32_t AnchorOffset,
    uint32_t AnchorCount) const noexcept
{
    ParticleRigUniform U {};
    U.SlotOffset         = SlotOffset;
    U.SlotCapacity       = SlotCapacity;
    U.ReleaseOriginCode  = static_cast<uint32_t>(QueryReleaseOriginCategory(Origin));
    U.VisualCarrierCode  = static_cast<uint32_t>(QueryVisualCarrierCategory(Carrier));
    U.ScalarChannelCode  = static_cast<uint32_t>(TintDriver.Channel);
    U.OpticalBlendCode   = (ColourProfile.BlendEquation == ColourSpace::OpticalBlendEquation::PremultipliedAlpha) ? 1u : 0u;
    U.ReleaseRatePerSec  = ReleaseRatePerSec;

    if (const auto* Sprite = std::get_if<SpriteBillboardCarrier>(&Carrier))
    {
        U.SpriteCutoutCode  = static_cast<uint32_t>(Sprite->Cutout);
        U.BokehApertureCode = static_cast<uint32_t>(Sprite->BokehAperture);
        U.StreakThinness    = Sprite->StreakThinness;
    }
    else if (const auto* Ribbon = std::get_if<RibbonStrandCarrier>(&Carrier))
    {
        U.StrandSegments = std::clamp(Ribbon->VerticesPerStrand, 2u, 32u);
    }

    // Extract spatial origin centre / extents across variant origin types
    if (const auto* Pt = std::get_if<AnalyticPointOrigin>(&Origin))
    {
        for (int K = 0; K < 3; ++K) U.OriginCenter[K] = Pt->CenterMeters[K];
    }
    else if (const auto* Sph = std::get_if<AnalyticSphereOrigin>(&Origin))
    {
        for (int K = 0; K < 3; ++K) U.OriginCenter[K] = Sph->CenterMeters[K];
        U.OriginRadius = Sph->RadiusMeters;
    }
    else if (const auto* Disc = std::get_if<AnalyticDiscOrigin>(&Origin))
    {
        for (int K = 0; K < 3; ++K) U.OriginCenter[K] = Disc->CenterMeters[K];
        U.OriginRadius = Disc->RadiusMeters;
    }
    else if (const auto* Box = std::get_if<AnalyticBoxOrigin>(&Origin))
    {
        for (int K = 0; K < 3; ++K)
        {
            U.OriginCenter[K]      = Box->CenterMeters[K];
            U.OriginHalfExtents[K] = Box->HalfExtentsMeters[K];
        }
    }
    else if (const auto* Surf = std::get_if<SurfaceAnchorOrigin>(&Origin))
    {
        for (int K = 0; K < 3; ++K)
        {
            U.OriginCenter[K]      = Surf->CenterMeters[K];
            U.OriginHalfExtents[K] = Surf->ScaleMeters[K];
        }
        U.OriginRadius = Surf->ScaleMeters[0];
    }
    else if (const auto* Fw = std::get_if<FireworkOrigin>(&Origin))
    {
        for (int K = 0; K < 3; ++K) U.OriginCenter[K] = Fw->ShellOrigin[K];
        U.OriginRadius = Fw->PatternRadius;
    }
    else if (const auto* Lat = std::get_if<SurfaceLatticeOrigin>(&Origin))
    {
        for (int K = 0; K < 3; ++K)
        {
            U.OriginCenter[K]      = Lat->CenterMeters[K];
            U.OriginHalfExtents[K] = Lat->HalfExtentsMeters[K];
        }
    }

    for (int K = 0; K < 3; ++K) U.LaunchDirection[K] = Kinematics.LaunchDirection[K];
    U.SpreadRadians   = Kinematics.SpreadRadians;
    U.SpeedMinMps     = Kinematics.SpeedMinMps;
    U.SpeedMaxMps     = Kinematics.SpeedMaxMps;
    U.LifeMinSeconds  = Kinematics.LifeMinSeconds;
    U.LifeMaxSeconds  = Kinematics.LifeMaxSeconds;
    U.SizeBirthMeters = Kinematics.SizeBirthMeters;
    U.SizeDeathMeters = Kinematics.SizeDeathMeters;
    U.SizeMultiplier  = Kinematics.SizeMultiplier;

    U.GravityScale    = Dynamics.Ballistic.GravityScale;
    U.AerodynamicDrag = Dynamics.Ballistic.AerodynamicDrag;
    U.WindCoupling    = Dynamics.ForceCouple.WindCouplingRate;
    U.BuoyancyMps2    = Dynamics.Ballistic.BuoyancyMps2;
    U.FloorBounce     = Dynamics.Ballistic.FloorRestitution;
    U.FlutterAmplitude= Dynamics.Ballistic.FlutterAmplitude;

    if (Dynamics.Curl.has_value())
    {
        U.CurlFrequency     = Dynamics.Curl->FrequencyPerM;
        U.CurlAmplitude     = Dynamics.Curl->DriveSpeedMps;
        U.CurlEvolutionRate = Dynamics.Curl->EvolutionRateHz;
        U.ManifoldRadius    = Dynamics.Curl->ShellRadiusM;
    }

    U.ColourRangeMin  = ColourProfile.Range.RangeMin;
    U.ColourRangeMax  = ColourProfile.Range.RangeMax;
    U.ColourRepeat    = ColourProfile.Range.RepeatCount;
    U.ColourPhase     = ColourProfile.Range.PhaseOffset;
    U.ColourWrapCode  = static_cast<uint32_t>(ColourProfile.Range.WrapBehaviour);
    U.ColourCurveCode = static_cast<uint32_t>(ColourProfile.Range.BlendCurve);

    auto SampleFast = [&](float T, float Dst[4]) {
        ColourSpace::SpatialColourSampleInput Q {};
        Q.NormalizedCoordinate = T;
        const ColourSpace::LinearRgba Col = ColourSpace::SampleColourSource(ColourProfile, Q);
        Dst[0] = Col.Red;
        Dst[1] = Col.Green;
        Dst[2] = Col.Blue;
        Dst[3] = Col.Alpha;
    };
    SampleFast(0.0f, U.PrimaryTintLinear);
    SampleFast(0.5f, U.MidTintLinear);
    SampleFast(1.0f, U.EndTintLinear);

    U.DynamicsBitmask     = Dynamics.ComputeActiveBitmask();
    U.ReceiverChannelMask = Dynamics.ForceCouple.ReceiverChannelMask;
    U.AnchorBufferOffset  = AnchorOffset;
    U.AnchorBufferCount   = AnchorCount;
    return U;
}

ParticleRig ConstructDefaultStudioRig()
{
    ParticleRig Rig {};
    Rig.Identifier        = "particle.rig.object_surface_pour";
    Rig.DisplayName       = "Object Surface Pour Ribbons";
    Rig.CategoryLabel     = "Silk & Curl";
    Rig.SlotCapacity      = 3400u;
    Rig.ReleaseRatePerSec = 620.0f;

    SurfaceAnchorOrigin Surf {};
    Surf.Preset          = SurfacePresetCategory::ObsidianSphere;
    Surf.CenterMeters[0] = 0.0f;
    Surf.CenterMeters[1] = 2.25f;
    Surf.CenterMeters[2] = 0.0f;
    Surf.ScaleMeters[0]  = 1.65f;
    Surf.ScaleMeters[1]  = 1.65f;
    Surf.ScaleMeters[2]  = 1.65f;
    Rig.Origin = Surf;

    Rig.Kinematics.LifeMinSeconds  = 3.4f;
    Rig.Kinematics.LifeMaxSeconds  = 5.2f;
    Rig.Kinematics.SpeedMinMps     = 0.8f;
    Rig.Kinematics.SpeedMaxMps     = 2.1f;
    Rig.Kinematics.SizeBirthMeters = 0.048f;
    Rig.Kinematics.SizeDeathMeters = 0.032f;

    SpriteBillboardCarrier Carrier {};
    Carrier.Cutout         = SpriteCutoutCategory::BokehDisc;
    Carrier.BokehAperture  = BokehApertureCategory::MixedDepth;
    Carrier.StreakThinness = 0.35f;
    Rig.Carrier            = Carrier;

    ColourSpace::MultiStopGradientSource Grad {};
    Grad.Stops = {
        { 0.00f, ColourSpace::ConvertHexSrgbToLinear("#4D8898", 1.0f) },
        { 0.48f, ColourSpace::ConvertHexSrgbToLinear("#9CDCE6", 1.0f) },
        { 1.00f, ColourSpace::ConvertHexSrgbToLinear("#F5FBFF", 1.0f) },
    };
    Rig.ColourProfile.Source        = std::move(Grad);
    Rig.ColourProfile.BlendEquation = ColourSpace::OpticalBlendEquation::Additive;
    Rig.TintDriver.Channel          = ParticleScalarChannel::NormalizedLifetime;

    Rig.Dynamics.Ballistic.GravityScale      = 0.25f;
    Rig.Dynamics.Ballistic.AerodynamicDrag   = 0.35f;
    Rig.Dynamics.ForceCouple.WindCouplingRate= 0.25f;

    CurlManifoldSpec Curl {};
    Curl.ManifoldMode    = 3u;
    Curl.FrequencyPerM   = 0.85f;
    Curl.DriveSpeedMps   = 2.2f;
    Curl.EvolutionRateHz = 0.24f;
    Curl.ShellRadiusM    = 1.65f;
    Rig.Dynamics.Curl    = Curl;

    return Rig;
}

} // namespace Frontier::ParticleSpace

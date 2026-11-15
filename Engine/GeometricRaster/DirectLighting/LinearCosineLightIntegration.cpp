/*============================================================================================================================================
                                                       LINEARCOSINELIGHTINTEGRATION.CPP
============================================================================================================================================*/
// 🧩 Visibility-Gated Clipped-Polygon Linearly Transformed Cosine (LTC) Quad Area Lights + 2-Scale (0.25 m / 2.2 m) Chassis & Specular AO

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometrySpace/Analysis/CurvatureAnalysis.h"
#include "Engine/MaterialSpace/EnergyCompensation/DirectionalAlbedoIntegral.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometricRaster
{

namespace
{

// 📝 Exact edge contour integrand between two unit vectors on S^2 for Lambertian clamped-cosine irradiance (Baum / Heitz 2016)
[[nodiscard]] float IntegrateSphericalPolygonEdgeZ(Mathematics::Axis3D V1, Mathematics::Axis3D V2) noexcept
{
    const float X = std::clamp(Mathematics::EvaluateDot(V1, V2), -0.99999f, 0.99999f);
    const float AbsX = std::fabs(X);
    // Cubic rational fit to theta / sin(theta) smooth all the way to X -> 1.0 without 0/0 cancellation!
    const float A    = 5.42031f + (3.12829f + 0.0902326f * AbsX) * AbsX;
    const float B    = 3.45068f + (4.18814f + AbsX) * AbsX;
    float ThetaOverSin = A / B;
    if (X < 0.0f)
    {
        const float SinTheta = std::sqrt(std::max(1.0e-8f, 1.0f - X * X));
        ThetaOverSin = Constants::PiFloat / SinTheta - ThetaOverSin;
    }
    const float CrossZ = V1.X * V2.Y - V1.Y * V2.X;
    return CrossZ * ThetaOverSin;
}

[[nodiscard]] float EncodeLinearToSrgb(float LinearVal) noexcept
{
    const float C = std::clamp(LinearVal, 0.0f, 1.0f);
    return (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
}

[[nodiscard]] Mathematics::LinearRGB ApplyAcesFilmic(Mathematics::LinearRGB Hdr) noexcept
{
    const auto Curve = [](float X) noexcept -> float
    {
        const float V = std::max(0.0f, X * 0.85f);
        return std::clamp((V * (2.51f * V + 0.03f)) / (V * (2.43f * V + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return { Curve(Hdr.R), Curve(Hdr.G), Curve(Hdr.B) };
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           LINEAR COSINE LIGHT INTEGRATION
//------------------------------------------------------------------------------------------------------------------------

float LinearCosineLightIntegration::IntegrateClippedQuadCosineFormFactor(
    const std::array<Mathematics::Axis3D, 4u>& LocalQuadVertices) noexcept
{
    // Sutherland-Hodgman clip of 4-corner polygon against upper-hemisphere horizon plane Z >= 0 (produces up to 5 vertices)
    std::array<Mathematics::Axis3D, 8u> Clipped {};
    uint32_t Count = 0u;

    for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
    {
        const Mathematics::Axis3D Cur  = LocalQuadVertices[Idx];
        const Mathematics::Axis3D Next = LocalQuadVertices[(Idx + 1u) & 3u];
        const bool CurAbove            = (Cur.Z >= 0.0f);
        const bool NextAbove           = (Next.Z >= 0.0f);

        if (CurAbove)
        {
            Clipped[Count++] = Cur;
        }
        if (CurAbove != NextAbove)
        {
            const float T = Cur.Z / (Cur.Z - Next.Z);
            Mathematics::Axis3D Hit = Cur + (Next - Cur) * T;
            Hit.Z = 0.0f;
            Clipped[Count++] = Hit;
        }
    }

    if (Count < 3u)
    {
        return 0.0f;
    }

    for (uint32_t Idx = 0u; Idx < Count; ++Idx)
    {
        Clipped[Idx] = Mathematics::NormalizeAxis(Clipped[Idx], { 0.0f, 0.0f, 1.0f });
    }

    float ContourSum = 0.0f;
    for (uint32_t Idx = 0u; Idx < Count; ++Idx)
    {
        ContourSum += IntegrateSphericalPolygonEdgeZ(Clipped[Idx], Clipped[(Idx + 1u) % Count]);
    }

    // Unsigned irradiance form factor in [0..1] (invariant to CW/CCW quad corner ordering when TwoSided or front-facing)
    return std::clamp(std::fabs(ContourSum) / Constants::TauFloat, 0.0f, 1.0f);
}

Mathematics::LinearRGB LinearCosineLightIntegration::EvaluateVisibilityGatedQuadLight(
    const PolygonalAreaQuadLight&                AreaLight,
    const AnalyticResolvedSurfaceSample&         HitSample,
    const MaterialSpace::SurfaceMaterial&        Mat,
    uint32_t                                     ActiveLobeMask,
    const SpatialAcceleration::WideBoundingTree& SceneTree) noexcept
{
    const Mathematics::Axis3D QuadNormal = Mathematics::NormalizeAxis(
        Mathematics::EvaluateCross(AreaLight.RightTangent, AreaLight.UpBitangent), { 0.0f, 0.0f, -1.0f });

    // Reject single-sided quad when shaded point lies behind emitter plane
    const Mathematics::Axis3D EmitterToSurface = HitSample.WorldPosition - AreaLight.CenterWorld;
    if (!AreaLight.TwoSidedEmission && Mathematics::EvaluateDot(EmitterToSurface, QuadNormal) <= 1.0e-4f)
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    // 4 world corners of the polygonal area quad
    const Mathematics::Axis3D HalfU = AreaLight.RightTangent * AreaLight.HalfWidthMeters;
    const Mathematics::Axis3D HalfV = AreaLight.UpBitangent * AreaLight.HalfHeightMeters;
    const std::array<Mathematics::Axis3D, 4u> WorldCorners {{
        AreaLight.CenterWorld - HalfU - HalfV,
        AreaLight.CenterWorld + HalfU - HalfV,
        AreaLight.CenterWorld + HalfU + HalfV,
        AreaLight.CenterWorld - HalfU + HalfV,
    }};

    // Visibility-gate the analytic LTC integral using 5 deterministic stratified cells across the quad (center + 4 inset sub-quadrants)
    // so polygonal area lights NEVER bleed through walls, plinths, or occluders!
    const std::array<Mathematics::Axis2D, 5u> StratifiedCellUv {{
        {  0.0f,  0.0f },
        { -0.6f, -0.6f },
        {  0.6f, -0.6f },
        {  0.6f,  0.6f },
        { -0.6f,  0.6f },
    }};

    const Mathematics::Axis3D RayOrigin = HitSample.WorldPosition + HitSample.TangentFrame.NormalZ * 2.0e-3f;
    uint32_t UnoccludedCellCount = 0u;
    for (const auto& Uv : StratifiedCellUv)
    {
        const Mathematics::Axis3D TargetPt = AreaLight.CenterWorld + HalfU * Uv.X + HalfV * Uv.Y;
        const Mathematics::Axis3D ToLight  = TargetPt - RayOrigin;
        const float Dist = Mathematics::EvaluateLength(ToLight);
        if (Dist <= 1.0e-4f) continue;
        const Mathematics::Axis3D Dir = ToLight * (1.0f / Dist);
        if (Mathematics::EvaluateDot(HitSample.TangentFrame.NormalZ, Dir) <= 0.0f) continue;

        SpatialAcceleration::RaySegmentQuery VisRay {};
        VisRay.Origin          = RayOrigin;
        VisRay.UnitDirection   = Dir;
        VisRay.MinimumDistance = 1.0e-3f;
        VisRay.MaximumDistance = std::max(2.0e-3f, Dist - 4.0e-3f);
        if (!SceneTree.QueryRayOccluded(VisRay))
        {
            ++UnoccludedCellCount;
        }
    }

    if (UnoccludedCellCount == 0u)
    {
        return { 0.0f, 0.0f, 0.0f };
    }
    const float VisibilityGate = static_cast<float>(UnoccludedCellCount) / static_cast<float>(StratifiedCellUv.size());

    // Construct view-aligned orthonormal LTC shading frame (T1 in view-normal plane, T2 = N x T1, N = NormalZ)
    const Mathematics::Axis3D Nrm = HitSample.TangentFrame.NormalZ;
    const float NdotV = std::clamp(Mathematics::EvaluateDot(Nrm, HitSample.WorldViewDirection), 1.0e-4f, 1.0f);
    Mathematics::Axis3D T1 = HitSample.WorldViewDirection - Nrm * NdotV;
    if (Mathematics::EvaluateLengthSquared(T1) <= 1.0e-6f)
    {
        T1 = HitSample.TangentFrame.TangentX;
    }
    else
    {
        T1 = Mathematics::NormalizeAxis(T1);
    }
    const Mathematics::Axis3D T2 = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Nrm, T1));

    // 1. Exact Clipped-Polygon Diffuse LTC Form Factor
    std::array<Mathematics::Axis3D, 4u> DiffuseLocal {};
    for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
    {
        const Mathematics::Axis3D Rel = WorldCorners[Idx] - HitSample.WorldPosition;
        DiffuseLocal[Idx] = {
            Mathematics::EvaluateDot(Rel, T1),
            Mathematics::EvaluateDot(Rel, T2),
            Mathematics::EvaluateDot(Rel, Nrm),
        };
    }
    const float DiffuseFormFactor = IntegrateClippedQuadCosineFormFactor(DiffuseLocal);

    // 2. GGX-Fitted Inverse LTC Transform M^{-1} mapping rough GGX lobe into canonical cosine distribution!
    const float Alpha = std::clamp(Mat.Specular.Roughness * Mat.Specular.Roughness, 0.004f, 1.0f);
    const float SinV  = std::sqrt(std::max(0.0f, 1.0f - NdotV * NdotV));
    // Rational fit to Heitz 2016 M^{-1} shear & anisotropic width parameters (exact mirror peak at R + roughness widening!)
    const float InvScaleX = 1.0f / std::max(0.04f, Alpha * (0.85f + 0.15f * NdotV));
    const float InvScaleY = 1.0f / std::max(0.04f, Alpha);
    const float ShearXZ   = -SinV / std::max(0.08f, NdotV + Alpha);

    std::array<Mathematics::Axis3D, 4u> SpecularLocal {};
    for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
    {
        const Mathematics::Axis3D D = DiffuseLocal[Idx];
        // Reflect/shear around view mirror direction in (T1, T2, N) frame & scale by 1/Alpha!
        const float MirrorX = -D.X * NdotV + D.Z * SinV;
        const float MirrorZ =  D.X * SinV  + D.Z * NdotV;
        SpecularLocal[Idx] = {
            (MirrorX + ShearXZ * MirrorZ * Alpha) * InvScaleX,
            D.Y * InvScaleY,
            MirrorZ,
        };
    }
    const float SpecFormFactor = IntegrateClippedQuadCosineFormFactor(SpecularLocal);

    // Combine with 4 KB DirectionalAlbedoSheet & F0/F82 Fresnel!
    const auto& AlbedoSheet = MaterialSpace::InspectGlobalDirectionalAlbedoSheet();
    const auto  AlbO        = MaterialSpace::SampleDirectionalAlbedo(AlbedoSheet, NdotV, Mat.Specular.Roughness);
    const float DielF0      = ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
                              ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
                              Mat.Specular.SpecularWeight;
    const Mathematics::LinearRGB SpecF0 {
        DielF0 * (1.0f - Mat.Specular.Metalness) + Mat.Diffuse.BaseColor.R * Mat.Specular.Metalness,
        DielF0 * (1.0f - Mat.Specular.Metalness) + Mat.Diffuse.BaseColor.G * Mat.Specular.Metalness,
        DielF0 * (1.0f - Mat.Specular.Metalness) + Mat.Diffuse.BaseColor.B * Mat.Specular.Metalness,
    };
    const Mathematics::LinearRGB SpecAlbedo {
        SpecF0.R * AlbO.DirectionalAlbedoE + (1.0f - SpecF0.R) * (1.0f - AlbO.DirectionalAlbedoE) * 0.25f,
        SpecF0.G * AlbO.DirectionalAlbedoE + (1.0f - SpecF0.G) * (1.0f - AlbO.DirectionalAlbedoE) * 0.25f,
        SpecF0.B * AlbO.DirectionalAlbedoE + (1.0f - SpecF0.B) * (1.0f - AlbO.DirectionalAlbedoE) * 0.25f,
    };

    const float DiffScale = ((ActiveLobeMask & MaterialSpace::LobeDiffuseEON) != 0u)
        ? ((1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight) * Mat.Diffuse.BaseWeight)
        : 0.0f;

    return {
        VisibilityGate * AreaLight.EmittedRadiance.R * (DiffScale * Mat.Diffuse.BaseColor.R * DiffuseFormFactor + SpecAlbedo.R * SpecFormFactor),
        VisibilityGate * AreaLight.EmittedRadiance.G * (DiffScale * Mat.Diffuse.BaseColor.G * DiffuseFormFactor + SpecAlbedo.G * SpecFormFactor),
        VisibilityGate * AreaLight.EmittedRadiance.B * (DiffScale * Mat.Diffuse.BaseColor.B * DiffuseFormFactor + SpecAlbedo.B * SpecFormFactor),
    };
}

float LinearCosineLightIntegration::ComputeLagardeSpecularOcclusion(
    float NdotV,
    float DiffuseAmbientOcclusion,
    float PerceptualRoughness) noexcept
{
    const float SafeNV = std::clamp(NdotV, 0.0f, 1.0f);
    const float SafeAo = std::clamp(DiffuseAmbientOcclusion, 0.0f, 1.0f);
    const float Alpha  = std::clamp(PerceptualRoughness * PerceptualRoughness, 0.001f, 1.0f);
    const float ExpPow = std::exp2(-16.0f * Alpha - 1.0f);
    return std::clamp(std::pow(SafeNV + SafeAo, ExpPow) - 1.0f + SafeAo, 0.0f, 1.0f);
}

DirectLightingFrameOutput LinearCosineLightIntegration::EvaluateDirectLightingFrame(
    DirectShadowTier                                Tier,
    const PerspectiveCameraFrame&                   Camera,
    std::span<const RigidClusterInstance>           Instances,
    const VisibilityBufferTargets&                  VisibilityTargets,
    const MaterialSpace::CompactMaterialRegister&   MaterialRegister,
    const SpatialAcceleration::WideBoundingTree&    SceneTree,
    const SolarDirectionalEmitter&                  Sun,
    std::span<const PolygonalAreaQuadLight>         AreaLights,
    const DistanceFieldSpace::SignedDistanceVolume* OptionalSdfVolume,
    const OrthographicShadowCascade*                OptionalFineClip,
    const OrthographicShadowCascade*                OptionalCoarseClip,
    const AmbientOcclusionSettings&                 AoSettings)
{
    DirectLightingFrameOutput Out {};
    Out.WidthPx  = VisibilityTargets.WidthPx;
    Out.HeightPx = VisibilityTargets.HeightPx;
    const std::size_t TotalPixels = static_cast<std::size_t>(Out.WidthPx) * Out.HeightPx;

    Out.DirectHdrRadiance.assign(TotalPixels, { 0.0f, 0.0f, 0.0f });
    Out.ToneMappedSrgb8.assign(TotalPixels * 4u, 255u);
    Out.CombinedDiffuseAo.assign(TotalPixels, 1.0f);
    Out.LagardeSpecularAo.assign(TotalPixels, 1.0f);

    const OrthonormalCameraBasis Basis = ComputeCameraBasis(Camera);
    std::vector<AnalyticResolvedSurfaceSample> PrimarySamples(TotalPixels);
    for (uint32_t PY = 0u; PY < Out.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Out.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Out.WidthPx + PX;
            PrimarySamples[Off] = VisibilityMaterialResolve::ReconstructAnalyticPixelHit(
                PX, PY, VisibilityTargets.OpaquePrimary64[Off], Basis, Basis, Instances);
        }
    }

    // 1. Evaluate Tiered Solar Shadows + Plane-Aware Penumbra Filter!
    Out.SolarShadows = SolarShadowSequence::EvaluateSolarShadowField(
        Tier, Sun, Out.WidthPx, Out.HeightPx, PrimarySamples, SceneTree,
        OptionalSdfVolume, OptionalFineClip, OptionalCoarseClip);

    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const uint32_t MatCount = MaterialRegister.QuerySlotCount();

    // 2. Shade Direct Sun + Visibility-Gated LTC Area Lights + 2-Scale (0.25 m Contact / 2.20 m Chassis) AO + Lagarde specAO!
    for (uint32_t PY = 0u; PY < Out.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Out.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Out.WidthPx + PX;
            const auto& Hit       = PrimarySamples[Off];
            if (!Hit.ValidHit || Hit.MaterialSlot >= MatCount)
            {
                Out.DirectHdrRadiance[Off] = { 0.05f, 0.08f, 0.14f };
                const auto Tone = ApplyAcesFilmic(Out.DirectHdrRadiance[Off]);
                Out.ToneMappedSrgb8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
                Out.ToneMappedSrgb8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
                Out.ToneMappedSrgb8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
                continue;
            }

            const auto& Mat          = MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
            const uint32_t LobeMask  = MaterialRegister.InspectCompactEntry(Hit.MaterialSlot).ActiveLobeMask;
            const auto LocalWo       = Hit.TangentFrame.WorldToLocal(Hit.WorldViewDirection);

            // Evaluate 2-Scale Physical Ambient Occlusion (0.25 m micro-contact + 2.20 m chassis underbody)
            const uint32_t AoRays     = std::clamp(AoSettings.RaysPerPixel, 2u, 32u);
            const uint32_t Seed       = PX * 1973u + PY * 9277u + 89173u;
            const float    RotOffset  = static_cast<float>(Seed & 0xFFFFu) * (Constants::TauFloat / 65536.0f);
            float ContactUnoccluded   = 0.0f;
            float ChassisUnoccluded   = 0.0f;
            const Mathematics::Axis3D AoOrigin = Hit.WorldPosition + Hit.TangentFrame.NormalZ * 2.0e-3f;

            for (uint32_t RIdx = 0u; RIdx < AoRays; ++RIdx)
            {
                const float U1    = (static_cast<float>(RIdx) + 0.5f) / static_cast<float>(AoRays);
                const float Phi   = static_cast<float>(RIdx) * 2.39996323f + RotOffset;
                const float SinT  = std::sqrt(U1);
                const float CosT  = std::sqrt(std::max(0.0f, 1.0f - U1));
                const Mathematics::Axis3D LocalRay { SinT * std::cos(Phi), SinT * std::sin(Phi), CosT };
                const Mathematics::Axis3D WorldRay = Hit.TangentFrame.LocalToWorld(LocalRay);

                SpatialAcceleration::RaySegmentQuery AoQuery {};
                AoQuery.Origin          = AoOrigin;
                AoQuery.UnitDirection   = WorldRay;
                AoQuery.MinimumDistance = 1.5e-3f;
                AoQuery.MaximumDistance = AoSettings.ChassisRadiusMeters;

                const auto AoHit = SceneTree.TraceClosestRay(AoQuery, false);
                if (!AoHit.HitConfirmed)
                {
                    ContactUnoccluded += 1.0f;
                    ChassisUnoccluded += 1.0f;
                }
                else
                {
                    if (AoHit.HitDistanceMeters > AoSettings.ContactRadiusMeters)
                    {
                        ContactUnoccluded += 1.0f;
                    }
                    // Smooth falloff across 2.20 m chassis horizon
                    ChassisUnoccluded += std::clamp(AoHit.HitDistanceMeters / AoSettings.ChassisRadiusMeters, 0.0f, 1.0f);
                }
            }

            const float ContactAo = ContactUnoccluded / static_cast<float>(AoRays);
            const float ChassisAo = ChassisUnoccluded / static_cast<float>(AoRays);
            const float DiffAo    = std::clamp(ContactAo * (0.25f + 0.75f * ChassisAo), 0.0f, 1.0f);
            const float SpecAo    = ComputeLagardeSpecularOcclusion(LocalWo.Z, DiffAo, Mat.Specular.Roughness);
            Out.CombinedDiffuseAo[Off] = DiffAo;
            Out.LagardeSpecularAo[Off] = SpecAo;

            Mathematics::LinearRGB Hdr = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);

            // Direct Solar Contribution gated by Plane-Aware Filtered Solar Visibility
            const float SunVis = Out.SolarShadows.FilteredVisibility[Off];
            const float NdotL  = Mathematics::EvaluateDot(Hit.TangentFrame.NormalZ, SunDir);
            if (NdotL > 0.0f && SunVis > 1.0e-4f)
            {
                const auto LocalSun = Hit.TangentFrame.WorldToLocal(SunDir);
                const auto SunBsdf  = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                    Mat, LobeMask, LocalWo, LocalSun, Hit.Footprint);
                Hdr.R += SunBsdf.R * Sun.SolarIrradiance.R * NdotL * SunVis;
                Hdr.G += SunBsdf.G * Sun.SolarIrradiance.G * NdotL * SunVis;
                Hdr.B += SunBsdf.B * Sun.SolarIrradiance.B * NdotL * SunVis;
            }

            // Visibility-Gated Analytic Polygonal LTC Area Lights
            for (const auto& AreaLight : AreaLights)
            {
                const auto AreaRad = EvaluateVisibilityGatedQuadLight(AreaLight, Hit, Mat, LobeMask, SceneTree);
                Hdr.R += AreaRad.R;
                Hdr.G += AreaRad.G;
                Hdr.B += AreaRad.B;
            }

            // Physically Occluded Sky Dome Fill (Diffuse gated by DiffAo, Specular gated by Lagarde SpecAo!)
            const Mathematics::LinearRGB SkyHemisphere { 0.14f, 0.19f, 0.28f };
            const float DiffWeight = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight);
            const auto MetalEnvF   = MaterialSpace::Scattering::EvaluateConductorEdgeTintFresnel(
                LocalWo.Z, Mat.Diffuse.BaseColor, Mat.Specular.EdgeTintF82);
            const float DielEnvF   = MaterialSpace::Scattering::EvaluateExactDielectricFresnel(LocalWo.Z, Mat.Specular.DielectricIOR);
            const Mathematics::LinearRGB SpecTint {
                DielEnvF * (1.0f - Mat.Specular.Metalness) + MetalEnvF.R * Mat.Specular.Metalness,
                DielEnvF * (1.0f - Mat.Specular.Metalness) + MetalEnvF.G * Mat.Specular.Metalness,
                DielEnvF * (1.0f - Mat.Specular.Metalness) + MetalEnvF.B * Mat.Specular.Metalness,
            };

            Hdr.R += SkyHemisphere.R * (DiffAo * DiffWeight * Mat.Diffuse.BaseColor.R + SpecAo * SpecTint.R * 1.35f);
            Hdr.G += SkyHemisphere.G * (DiffAo * DiffWeight * Mat.Diffuse.BaseColor.G + SpecAo * SpecTint.G * 1.35f);
            Hdr.B += SkyHemisphere.B * (DiffAo * DiffWeight * Mat.Diffuse.BaseColor.B + SpecAo * SpecTint.B * 1.35f);

            Out.DirectHdrRadiance[Off] = Hdr;
            const auto Tone = ApplyAcesFilmic(Hdr);
            Out.ToneMappedSrgb8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
            Out.ToneMappedSrgb8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
            Out.ToneMappedSrgb8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
        }
    }

    return Out;
}

} // namespace Frontier::GeometricRaster

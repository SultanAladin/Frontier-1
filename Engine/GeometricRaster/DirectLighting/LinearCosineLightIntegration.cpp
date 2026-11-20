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
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <thread>

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

namespace
{

struct DirectEmissiveTri
{
    Mathematics::Axis3D    V0 {};
    Mathematics::Axis3D    V1 {};
    Mathematics::Axis3D    V2 {};
    Mathematics::Axis3D    Centroid {};
    Mathematics::Axis3D    Normal {};
    float                  Area = 0.0f;
    Mathematics::LinearRGB Radiance {};
};

[[nodiscard]] std::vector<DirectEmissiveTri> ExtractDirectEmissiveTriangles(
    const SpatialAcceleration::WideBoundingTree&  SceneTree,
    const MaterialSpace::CompactMaterialRegister& MaterialRegister)
{
    std::vector<DirectEmissiveTri> Out;
    const uint32_t MatCount = MaterialRegister.QuerySlotCount();
    for (const auto& Prim : SceneTree.Primitives)
    {
        if (Prim.MaterialOrdinal >= MatCount) continue;
        const auto& Mat = MaterialRegister.InspectAuthoredMaterial(Prim.MaterialOrdinal);
        const auto Emit = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
        if ((0.2126f * Emit.R + 0.7152f * Emit.G + 0.0722f * Emit.B) <= 1.0e-5f) continue;

        const Mathematics::Axis3D P0 = Prim.Corner0;
        const Mathematics::Axis3D P1 = Prim.Corner0 + Prim.Edge01;
        const Mathematics::Axis3D P2 = Prim.Corner0 + Prim.Edge02;
        const Mathematics::Axis3D CrossN = Mathematics::EvaluateCross(Prim.Edge01, Prim.Edge02);
        const float DoubleArea = Mathematics::EvaluateLength(CrossN);
        if (DoubleArea <= 1.0e-7f) continue;

        Out.push_back(DirectEmissiveTri{
            P0, P1, P2, (P0 + P1 + P2) * (1.0f / 3.0f), CrossN * (1.0f / DoubleArea), DoubleArea * 0.5f, Emit
        });
    }
    return Out;
}

[[nodiscard]] float IntegrateClippedTriCosineFormFactor(
    const std::array<Mathematics::Axis3D, 3u>& LocalTri) noexcept
{
    std::array<Mathematics::Axis3D, 6u> Clipped {};
    uint32_t Count = 0u;
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const auto& Cur  = LocalTri[I];
        const auto& Next = LocalTri[(I + 1u) % 3u];
        const bool CurUp  = (Cur.Z >= 0.0f);
        const bool NextUp = (Next.Z >= 0.0f);
        if (CurUp) Clipped[Count++] = Cur;
        if (CurUp != NextUp)
        {
            const float T = Cur.Z / (Cur.Z - Next.Z);
            Mathematics::Axis3D Hit = Cur + (Next - Cur) * T;
            Hit.Z = 0.0f;
            Clipped[Count++] = Hit;
        }
    }
    if (Count < 3u) return 0.0f;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        Clipped[I] = Mathematics::NormalizeAxis(Clipped[I], { 0.0f, 0.0f, 1.0f });
    }
    float Sum = 0.0f;
    for (uint32_t I = 0u; I < Count; ++I)
    {
        Sum += IntegrateSphericalPolygonEdgeZ(Clipped[I], Clipped[(I + 1u) % Count]);
    }
    return std::clamp(std::fabs(Sum) / Constants::TauFloat, 0.0f, 1.0f);
}

[[nodiscard]] Mathematics::LinearRGB EvaluateDirectEmissiveTriangleLtc(
    const DirectEmissiveTri&                     Tri,
    const AnalyticResolvedSurfaceSample&         HitSample,
    const MaterialSpace::SurfaceMaterial&        Mat,
    uint32_t                                     ActiveLobeMask,
    const SpatialAcceleration::WideBoundingTree& SceneTree) noexcept
{
    const Mathematics::Axis3D Nrm = HitSample.TangentFrame.NormalZ;
    // Reject pixels behind emitter plane or entirely below emitter horizon
    if (Mathematics::EvaluateDot(HitSample.WorldPosition - Tri.Centroid, Tri.Normal) <= 1.0e-4f) return {};
    const float D0 = Mathematics::EvaluateDot(Tri.V0 - HitSample.WorldPosition, Nrm);
    const float D1 = Mathematics::EvaluateDot(Tri.V1 - HitSample.WorldPosition, Nrm);
    const float D2 = Mathematics::EvaluateDot(Tri.V2 - HitSample.WorldPosition, Nrm);
    if (std::max({ D0, D1, D2 }) <= 1.0e-4f) return {};

    // Evaluate analytic LTC form factors first; skip BVH ray queries whenever form factor is zero!
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

    const std::array<Mathematics::Axis3D, 3u> WorldV {{ Tri.V0, Tri.V1, Tri.V2 }};
    std::array<Mathematics::Axis3D, 3u> DiffLocal {};
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const auto Rel = WorldV[I] - HitSample.WorldPosition;
        DiffLocal[I] = {
            Mathematics::EvaluateDot(Rel, T1),
            Mathematics::EvaluateDot(Rel, T2),
            Mathematics::EvaluateDot(Rel, Nrm),
        };
    }
    const float DiffFF = IntegrateClippedTriCosineFormFactor(DiffLocal);

    const float Alpha     = std::clamp(Mat.Specular.Roughness * Mat.Specular.Roughness, 0.004f, 1.0f);
    const float SinV      = std::sqrt(std::max(0.0f, 1.0f - NdotV * NdotV));
    const float InvScaleX = 1.0f / std::max(0.04f, Alpha * (0.85f + 0.15f * NdotV));
    const float InvScaleY = 1.0f / std::max(0.04f, Alpha);
    const float ShearXZ   = -SinV / std::max(0.08f, NdotV + Alpha);

    std::array<Mathematics::Axis3D, 3u> SpecLocal {};
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const auto& D = DiffLocal[I];
        const float MX = -D.X * NdotV + D.Z * SinV;
        const float MZ =  D.X * SinV  + D.Z * NdotV;
        SpecLocal[I] = { (MX + ShearXZ * MZ * Alpha) * InvScaleX, D.Y * InvScaleY, MZ };
    }
    const float SpecFF = IntegrateClippedTriCosineFormFactor(SpecLocal);
    if ((DiffFF + SpecFF) <= 1.0e-6f) return {};

    // Adaptive deterministic stratified barycentric visibility (zero jitter, zero denoiser):
    // 9 taps for high-solid-angle emitters, 4 taps for medium, 1 centroid tap for small/distant triangles
    const float Dist2 = std::max(1.0e-4f, Mathematics::EvaluateLengthSquared(Tri.Centroid - HitSample.WorldPosition));
    const float SolidAngleEst = Tri.Area / Dist2;
    float Visibility = 0.0f;
    if (SolidAngleEst > 0.0002f)
    {
        static constexpr std::array<std::array<float, 3u>, 9u> Bary9 {{
            {{ 0.70f, 0.15f, 0.15f }},
            {{ 0.15f, 0.70f, 0.15f }},
            {{ 0.15f, 0.15f, 0.70f }},
            {{ 0.45f, 0.45f, 0.10f }},
            {{ 0.45f, 0.10f, 0.45f }},
            {{ 0.10f, 0.45f, 0.45f }},
            {{ 0.3333f, 0.3333f, 0.3334f }},
            {{ 0.55f, 0.25f, 0.20f }},
            {{ 0.20f, 0.55f, 0.25f }},
        }};
        uint32_t Unoccluded = 0u;
        for (const auto& B : Bary9)
        {
            const Mathematics::Axis3D Tgt = Tri.V0 * B[0] + Tri.V1 * B[1] + Tri.V2 * B[2];
            const Mathematics::Axis3D ToL = Tgt - HitSample.WorldPosition;
            const float Dist = Mathematics::EvaluateLength(ToL);
            const Mathematics::Axis3D Dir = ToL * (1.0f / std::max(1.0e-5f, Dist));
            if (Mathematics::EvaluateDot(Nrm, Dir) <= 1.0e-4f) continue;

            SpatialAcceleration::RaySegmentQuery Ray {};
            Ray.Origin          = HitSample.WorldPosition + Nrm * 2.5e-3f;
            Ray.UnitDirection   = Dir;
            Ray.MinimumDistance = 1.0e-3f;
            Ray.MaximumDistance = std::max(2.0e-3f, Dist - 6.0e-3f);
            if (!SceneTree.QueryRayOccluded(Ray)) ++Unoccluded;
        }
        Visibility = static_cast<float>(Unoccluded) * (1.0f / 9.0f);
    }
    else
    {
        static constexpr std::array<std::array<float, 3u>, 4u> Bary4 {{
            {{ 0.3333f, 0.3333f, 0.3334f }},
            {{ 0.70f, 0.15f, 0.15f }},
            {{ 0.15f, 0.70f, 0.15f }},
            {{ 0.15f, 0.15f, 0.70f }},
        }};
        uint32_t Unoccluded = 0u;
        for (const auto& B : Bary4)
        {
            const Mathematics::Axis3D Tgt = Tri.V0 * B[0] + Tri.V1 * B[1] + Tri.V2 * B[2];
            const Mathematics::Axis3D ToL = Tgt - HitSample.WorldPosition;
            const float Dist = Mathematics::EvaluateLength(ToL);
            const Mathematics::Axis3D Dir = ToL * (1.0f / std::max(1.0e-5f, Dist));
            if (Mathematics::EvaluateDot(Nrm, Dir) <= 1.0e-4f) continue;

            SpatialAcceleration::RaySegmentQuery Ray {};
            Ray.Origin          = HitSample.WorldPosition + Nrm * 2.5e-3f;
            Ray.UnitDirection   = Dir;
            Ray.MinimumDistance = 1.0e-3f;
            Ray.MaximumDistance = std::max(2.0e-3f, Dist - 6.0e-3f);
            if (!SceneTree.QueryRayOccluded(Ray)) ++Unoccluded;
        }
        Visibility = static_cast<float>(Unoccluded) * 0.25f;
    }
    if (Visibility <= 1.0e-5f) return {};

    const auto& Sheet = MaterialSpace::InspectGlobalDirectionalAlbedoSheet();
    const auto  AlbO  = MaterialSpace::SampleDirectionalAlbedo(Sheet, NdotV, Mat.Specular.Roughness);
    const float DielF0 = ((Mat.Specular.DielectricIOR - 1.0f) / (Mat.Specular.DielectricIOR + 1.0f)) *
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
        Visibility * Tri.Radiance.R * (DiffScale * Mat.Diffuse.BaseColor.R * DiffFF + SpecAlbedo.R * SpecFF),
        Visibility * Tri.Radiance.G * (DiffScale * Mat.Diffuse.BaseColor.G * DiffFF + SpecAlbedo.G * SpecFF),
        Visibility * Tri.Radiance.B * (DiffScale * Mat.Diffuse.BaseColor.B * DiffFF + SpecAlbedo.B * SpecFF),
    };
}

} // namespace

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

DirectLightingOutput LinearCosineLightIntegration::EvaluateDirectLightingFrame(
    DirectShadowTier                              Tier,
    const PerspectiveCameraBasis&                 Camera,
    std::span<const RigidClusterInstance>         Instances,
    const VisibilityBufferTargets&                VisibilityTargets,
    const MaterialSpace::CompactMaterialRegister& MaterialRegister,
    const SpatialAcceleration::WideBoundingTree&  SceneTree,
    const SolarDirectionalSource&                 Sun,
    std::span<const PolygonalAreaQuadLight>       AreaLights,
    const OrthographicShadowCascade*              OptionalFineClip,
    const OrthographicShadowCascade*              OptionalCoarseClip,
    const AmbientOcclusionSettings&               AoSettings)
{
    const SolarShadowAlgorithm MappedAlg = (Tier == DirectShadowTier::RtxDeterministicSolarRay)
        ? SolarShadowAlgorithm::WideTreeRayQuery
        : SolarShadowAlgorithm::ContactPcss;
    return EvaluateDirectLightingFrameWithAlgorithm(
        MappedAlg,
        Camera,
        Instances,
        VisibilityTargets,
        MaterialRegister,
        SceneTree,
        Sun,
        AreaLights,
        OptionalFineClip,
        OptionalCoarseClip,
        AoSettings);
}

DirectLightingOutput LinearCosineLightIntegration::EvaluateDirectLightingFrameWithAlgorithm(
    SolarShadowAlgorithm                           Algorithm,
    const PerspectiveCameraBasis&                  Camera,
    std::span<const RigidClusterInstance>          Instances,
    const VisibilityBufferTargets&                 VisibilityTargets,
    const MaterialSpace::CompactMaterialRegister&  MaterialRegister,
    const SpatialAcceleration::WideBoundingTree&   SceneTree,
    const SolarDirectionalSource&                  Sun,
    std::span<const PolygonalAreaQuadLight>        AreaLights,
    const OrthographicShadowCascade*               OptionalFineClip,
    const OrthographicShadowCascade*               OptionalCoarseClip,
    const AmbientOcclusionSettings&                AoSettings,
    std::span<const DirectPointLightSpec>          PointLights,
    Mathematics::LinearRGB                         SkyFillRadiance)
{
    DirectLightingOutput Out {};
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

    // 1. Evaluate Shadow Lab Solar Shadows + Plane-Aware Penumbra Filter
    Out.SolarShadows = SolarShadowSequence::EvaluateSolarShadowFieldWithAlgorithm(
        Algorithm, Sun, Out.WidthPx, Out.HeightPx, PrimarySamples, SceneTree,
        OptionalFineClip, OptionalCoarseClip);

    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const uint32_t MatCount = MaterialRegister.QuerySlotCount();

    // 1b. Evaluate 2-Scale Physical Ambient Occlusion (0.25 m Contact + 2.20 m Chassis) with IGN + 2-Pass Plane-Aware Denoiser
    const uint32_t AoRays = std::clamp(std::max(12u, AoSettings.RaysPerPixel), 12u, 32u);
    for (uint32_t PY = 0u; PY < Out.HeightPx; ++PY)
    {
        for (uint32_t PX = 0u; PX < Out.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Out.WidthPx + PX;
            const auto& Hit       = PrimarySamples[Off];
            if (!Hit.ValidHit || Hit.MaterialSlot >= MatCount)
            {
                Out.CombinedDiffuseAo[Off] = 1.0f;
                continue;
            }
            const float IgnDot    = 0.06711056f * static_cast<float>(PX) + 0.00583715f * static_cast<float>(PY);
            const float IgnFrac   = 52.9829189f * (IgnDot - std::floor(IgnDot));
            const float RotOffset = (IgnFrac - std::floor(IgnFrac)) * Constants::TauFloat;
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
                    ChassisUnoccluded += std::clamp(AoHit.HitDistanceMeters / AoSettings.ChassisRadiusMeters, 0.0f, 1.0f);
                }
            }
            const float ContactAo = ContactUnoccluded / static_cast<float>(AoRays);
            const float ChassisAo = ChassisUnoccluded / static_cast<float>(AoRays);
            Out.CombinedDiffuseAo[Off] = std::clamp(ContactAo * (0.25f + 0.75f * ChassisAo), 0.0f, 1.0f);
        }
    }

    // Plane-aware 2-step à-trous AO smoothing (keeps 0px contact creases sharp while eliminating wall/floor grain!)
    {
        std::vector<float> AoPing = Out.CombinedDiffuseAo;
        std::vector<float> AoPong(TotalPixels, 1.0f);
        constexpr std::array<int, 2u> AoSteps { 1, 2 };
        constexpr float K1D[2] { 1.0f, 0.60f };
        for (int Step : AoSteps)
        {
            for (uint32_t PY = 0u; PY < Out.HeightPx; ++PY)
            {
                for (uint32_t PX = 0u; PX < Out.WidthPx; ++PX)
                {
                    const std::size_t Off = static_cast<std::size_t>(PY) * Out.WidthPx + PX;
                    const auto& CHit      = PrimarySamples[Off];
                    if (!CHit.ValidHit || CHit.MaterialSlot >= MatCount)
                    {
                        AoPong[Off] = 1.0f;
                        continue;
                    }
                    float SumAo = 0.0f, SumW = 0.0f;
                    for (int KY = -1; KY <= 1; ++KY)
                    {
                        const int NY = std::clamp(static_cast<int>(PY) + KY * Step, 0, static_cast<int>(Out.HeightPx) - 1);
                        for (int KX = -1; KX <= 1; ++KX)
                        {
                            const int NX = std::clamp(static_cast<int>(PX) + KX * Step, 0, static_cast<int>(Out.WidthPx) - 1);
                            const std::size_t NOff = static_cast<std::size_t>(NY) * Out.WidthPx + static_cast<uint32_t>(NX);
                            const auto& NHit       = PrimarySamples[NOff];
                            if (!NHit.ValidHit) continue;
                            const float KSpatial = K1D[std::abs(KX)] * K1D[std::abs(KY)];
                            const float PlaneD   = std::fabs(Mathematics::EvaluateDot(
                                CHit.TangentFrame.NormalZ, NHit.WorldPosition - CHit.WorldPosition));
                            const float WPlane   = std::exp(-PlaneD * 80.0f);
                            const float NrmD     = std::max(0.0f, Mathematics::EvaluateDot(
                                CHit.TangentFrame.NormalZ, NHit.TangentFrame.NormalZ));
                            const float WNrm     = std::pow(NrmD, 32.0f);
                            const float W        = KSpatial * WPlane * WNrm;
                            SumAo += AoPing[NOff] * W;
                            SumW  += W;
                        }
                    }
                    AoPong[Off] = (SumW > 1.0e-6f) ? (SumAo / SumW) : AoPing[Off];
                }
            }
            AoPing.swap(AoPong);
        }
        Out.CombinedDiffuseAo = std::move(AoPing);
    }

    const auto Emissives = ExtractDirectEmissiveTriangles(SceneTree, MaterialRegister);

    // 2. Shade Direct Sun + Visibility-Gated LTC Area Lights + Exact Analytic Emissive-Triangle LTC + Filtered 2-Scale AO + Lagarde specAO!
    const auto ShadeRows = [&](uint32_t YBegin, uint32_t YEnd)
    {
    for (uint32_t PY = YBegin; PY < YEnd; ++PY)
    {
        for (uint32_t PX = 0u; PX < Out.WidthPx; ++PX)
        {
            const std::size_t Off = static_cast<std::size_t>(PY) * Out.WidthPx + PX;
            const auto& Hit       = PrimarySamples[Off];
            if (!Hit.ValidHit || Hit.MaterialSlot >= MatCount)
            {
                Out.DirectHdrRadiance[Off] = {
                    SkyFillRadiance.R * 0.36f,
                    SkyFillRadiance.G * 0.42f,
                    SkyFillRadiance.B * 0.50f,
                };
                const auto Tone = ApplyAcesFilmic(Out.DirectHdrRadiance[Off]);
                Out.ToneMappedSrgb8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
                Out.ToneMappedSrgb8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
                Out.ToneMappedSrgb8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
                continue;
            }

            const auto& Mat          = MaterialRegister.InspectAuthoredMaterial(Hit.MaterialSlot);
            const uint32_t LobeMask  = MaterialRegister.InspectCompactEntry(Hit.MaterialSlot).ActiveLobeMask;
            const auto LocalWo       = Hit.TangentFrame.WorldToLocal(Hit.WorldViewDirection);
            const float DiffAo       = Out.CombinedDiffuseAo[Off];
            const float SpecAo       = ComputeLagardeSpecularOcclusion(LocalWo.Z, DiffAo, Mat.Specular.Roughness);
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

            // Exact Analytic Emissive-Triangle LTC + 9-Tap Deterministic Stratified Visibility (Zero Cone GI, Zero Jitter, Zero Denoiser)
            for (const auto& EmisTri : Emissives)
            {
                const auto EmisRad = EvaluateDirectEmissiveTriangleLtc(EmisTri, Hit, Mat, LobeMask, SceneTree);
                Hdr.R += EmisRad.R;
                Hdr.G += EmisRad.G;
                Hdr.B += EmisRad.B;
            }

            // Direct Point Lights (gated by BVH4 ray occlusion)
            for (const auto& Pt : PointLights)
            {
                const Mathematics::Axis3D Delta = Pt.PositionWorld - Hit.WorldPosition;
                const float Dist2 = Mathematics::EvaluateLengthSquared(Delta);
                const float Dist  = std::sqrt(std::max(1.0e-5f, Dist2));
                if (Dist >= Pt.RangeMeters) continue;
                const Mathematics::Axis3D LDir = Delta * (1.0f / Dist);
                const float NdotL = Mathematics::EvaluateDot(Hit.TangentFrame.NormalZ, LDir);
                if (NdotL <= 1.0e-4f) continue;

                SpatialAcceleration::RaySegmentQuery PtRay {};
                PtRay.Origin          = Hit.WorldPosition + Hit.TangentFrame.NormalZ * 2.0e-3f;
                PtRay.UnitDirection   = LDir;
                PtRay.MinimumDistance = 1.0e-3f;
                PtRay.MaximumDistance = std::max(0.02f, Dist - Pt.RadiusMeters);
                const float PtVis     = SceneTree.QueryRayOccluded(PtRay) ? 0.0f : 1.0f;

                if (PtVis > 1.0e-4f)
                {
                    const float RangeRatio = std::clamp(Dist / std::max(0.5f, Pt.RangeMeters), 0.0f, 1.0f);
                    const float SmoothWin  = std::max(0.0f, 1.0f - RangeRatio * RangeRatio);
                    const float Atten = (Pt.Intensity * SmoothWin * SmoothWin) /
                                        std::max(0.18f, Dist2 + Pt.RadiusMeters * Pt.RadiusMeters);
                    const auto LocalWi = Hit.TangentFrame.WorldToLocal(LDir);
                    const auto Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                        Mat, LobeMask, LocalWo, LocalWi, Hit.Footprint);
                    Hdr.R += Bsdf.R * Pt.Color.R * (NdotL * PtVis * Atten);
                    Hdr.G += Bsdf.G * Pt.Color.G * (NdotL * PtVis * Atten);
                    Hdr.B += Bsdf.B * Pt.Color.B * (NdotL * PtVis * Atten);
                }
            }

            // Clean hemispherical diffuse sky fill ONLY (zero fake sky-specular glare over BVH4 metal reflections)
            const Mathematics::LinearRGB SkyHemisphere = SkyFillRadiance * 0.22f;
            const float DiffWeight = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight);
            Hdr.R += SkyHemisphere.R * DiffAo * DiffWeight * Mat.Diffuse.BaseColor.R;
            Hdr.G += SkyHemisphere.G * DiffAo * DiffWeight * Mat.Diffuse.BaseColor.G;
            Hdr.B += SkyHemisphere.B * DiffAo * DiffWeight * Mat.Diffuse.BaseColor.B;

            Out.DirectHdrRadiance[Off] = Hdr;
            const auto Tone = ApplyAcesFilmic(Hdr);
            Out.ToneMappedSrgb8[Off * 4u + 0u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.R) * 255.0f));
            Out.ToneMappedSrgb8[Off * 4u + 1u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.G) * 255.0f));
            Out.ToneMappedSrgb8[Off * 4u + 2u] = static_cast<uint8_t>(std::round(EncodeLinearToSrgb(Tone.B) * 255.0f));
        }
    }
    };
    const uint32_t ThreadCount = (!Emissives.empty())
        ? std::min(Out.HeightPx, std::max(1u, std::thread::hardware_concurrency()))
        : 1u;
    if (ThreadCount <= 1u || Out.HeightPx <= 4u)
    {
        ShadeRows(0u, Out.HeightPx);
    }
    else
    {
        std::vector<std::thread> Workers;
        Workers.reserve(ThreadCount);
        const uint32_t Chunk = (Out.HeightPx + ThreadCount - 1u) / ThreadCount;
        for (uint32_t T = 0u; T < ThreadCount; ++T)
        {
            const uint32_t Y0 = T * Chunk;
            const uint32_t Y1 = std::min(Out.HeightPx, Y0 + Chunk);
            if (Y0 < Y1) Workers.emplace_back([&, Y0, Y1]() { ShadeRows(Y0, Y1); });
        }
        for (auto& W : Workers) W.join();
    }

    return Out;
}

} // namespace Frontier::GeometricRaster

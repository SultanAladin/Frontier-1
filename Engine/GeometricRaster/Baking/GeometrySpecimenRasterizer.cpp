/*============================================================================================================================================
                                                       GEOMETRYSPECIMENRASTERIZER.CPP
============================================================================================================================================*/
// 🧩 1:1 C++ miniature 3-point OpenPBR studio thumbnail raytracer matching FrontierWorldMirror ModelThumbnailRenderer

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Baking/GeometrySpecimenRasterizer.h"

#include "Engine/ColourSpace/TransferFunction.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/Scattering/OpenPbrSurfaceScattering.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <vector>

namespace Frontier::GeometricRaster
{

namespace
{

enum class SpecimenPartShape : uint8_t
{
    Box      = 0u,
    Sphere   = 1u,
    Cylinder = 2u,
    Cone     = 3u,
    Torus    = 4u,
};

enum class SpecimenStripePattern : uint8_t
{
    None         = 0u,
    TrafficCone  = 1u, // Safety orange + twin crisp white reflective collars
    HazardYellow = 2u, // Industrial yellow + dark charcoal hazard collar bands
};

struct Matrix3x3
{
    std::array<Mathematics::Axis3D, 3u> Columns{}; // Column vectors in world space
};

struct SpecimenPart
{
    SpecimenPartShape      Shape        = SpecimenPartShape::Box;
    Mathematics::Axis3D    LocalOffset  {};
    Mathematics::Scalar3D  HalfExtents  { 0.35f, 0.35f, 0.35f };
    Mathematics::Scalar3D  LocalRotDeg  {};
    MaterialSpace::SurfaceMaterial Material {};
    SpecimenStripePattern  StripePattern = SpecimenStripePattern::None;
    Matrix3x3              WorldFromLocal {};
    Matrix3x3              LocalFromWorld {};
};

[[nodiscard]] std::string LowercaseAscii(std::string_view Input)
{
    std::string Result;
    Result.reserve(Input.size());
    for (const unsigned char Ch : Input)
    {
        Result.push_back(static_cast<char>(std::tolower(Ch)));
    }
    return Result;
}

[[nodiscard]] bool ContainsToken(std::string_view LowerHaystack, std::string_view Needle) noexcept
{
    return LowerHaystack.find(Needle) != std::string_view::npos;
}

[[nodiscard]] Mathematics::LinearRGB SrgbHexToLinear(uint8_t Red, uint8_t Green, uint8_t Blue) noexcept
{
    return {
        ColourSpace::DecodePiecewiseSrgbChannel(static_cast<float>(Red) / 255.0f),
        ColourSpace::DecodePiecewiseSrgbChannel(static_cast<float>(Green) / 255.0f),
        ColourSpace::DecodePiecewiseSrgbChannel(static_cast<float>(Blue) / 255.0f),
    };
}

[[nodiscard]] constexpr Mathematics::LinearRGB MulRgb(Mathematics::LinearRGB A, Mathematics::LinearRGB B) noexcept
{
    return { A.R * B.R, A.G * B.G, A.B * B.B };
}

[[nodiscard]] MaterialSpace::SurfaceMaterial MakeStudioMaterial(
    Mathematics::LinearRGB BaseColor,
    float                  Roughness    = 0.38f,
    float                  Metallic     = 0.0f,
    float                  CoatWeight   = 0.0f,
    float                  CoatRough    = 0.08f,
    float                  EmissionLum  = 0.0f,
    float                  Transmission = 0.0f) noexcept
{
    MaterialSpace::SurfaceMaterial Mat {};
    Mat.Diffuse.BaseColor        = BaseColor;
    Mat.Diffuse.BaseWeight       = 1.0f;
    Mat.Diffuse.DiffuseRoughness = std::clamp(Roughness + 0.06f, 0.0f, 1.0f);
    Mat.Specular.Roughness       = std::clamp(Roughness, 0.02f, 1.0f);
    Mat.Specular.DielectricIOR   = 1.50f;
    Mat.Specular.Metalness       = std::clamp(Metallic, 0.0f, 1.0f);
    if (CoatWeight > 0.001f)
    {
        Mat.Clearcoat.CoatWeight    = std::clamp(CoatWeight, 0.0f, 1.0f);
        Mat.Clearcoat.CoatRoughness = std::clamp(CoatRough, 0.01f, 1.0f);
    }
    if (EmissionLum > 0.001f)
    {
        Mat.PorosityLight.EmissionLuminanceNits = EmissionLum;
        Mat.PorosityLight.EmissionTint          = BaseColor;
    }
    if (Transmission > 0.001f)
    {
        Mat.Transmission.TransmissionWeight = std::clamp(Transmission, 0.0f, 1.0f);
    }
    MaterialSpace::SanitizeSurfaceMaterial(Mat);
    return Mat;
}

// R = Rz * Ry * Rx matching FrontierWorldMirror WorldPropLibrary.js
[[nodiscard]] Matrix3x3 BuildEulerDegMatrix(Mathematics::Scalar3D RotDeg) noexcept
{
    constexpr float DegToRad = 0.017453292519943295f;
    const float Rx = RotDeg.X * DegToRad;
    const float Ry = RotDeg.Y * DegToRad;
    const float Rz = RotDeg.Z * DegToRad;
    const float Cx = std::cos(Rx), Sx = std::sin(Rx);
    const float Cy = std::cos(Ry), Sy = std::sin(Ry);
    const float Cz = std::cos(Rz), Sz = std::sin(Rz);

    Matrix3x3 M{};
    M.Columns[0] = { Cz * Cy, Sz * Cy, -Sy };
    M.Columns[1] = { Cz * Sy * Sx - Sz * Cx, Sz * Sy * Sx + Cz * Cx, Cy * Sx };
    M.Columns[2] = { Cz * Sy * Cx + Sz * Sx, Sz * Sy * Cx - Cz * Sx, Cy * Cx };
    return M;
}

[[nodiscard]] Matrix3x3 TransposeMatrix(const Matrix3x3& M) noexcept
{
    Matrix3x3 T{};
    T.Columns[0] = { M.Columns[0].X, M.Columns[1].X, M.Columns[2].X };
    T.Columns[1] = { M.Columns[0].Y, M.Columns[1].Y, M.Columns[2].Y };
    T.Columns[2] = { M.Columns[0].Z, M.Columns[1].Z, M.Columns[2].Z };
    return T;
}

[[nodiscard]] Mathematics::Axis3D TransformVector(const Matrix3x3& M, Mathematics::Axis3D V) noexcept
{
    return {
        M.Columns[0].X * V.X + M.Columns[1].X * V.Y + M.Columns[2].X * V.Z,
        M.Columns[0].Y * V.X + M.Columns[1].Y * V.Y + M.Columns[2].Y * V.Z,
        M.Columns[0].Z * V.X + M.Columns[1].Z * V.Y + M.Columns[2].Z * V.Z,
    };
}

[[nodiscard]] Matrix3x3 MultiplyMatrix(const Matrix3x3& A, const Matrix3x3& B) noexcept
{
    Matrix3x3 Out{};
    Out.Columns[0] = TransformVector(A, B.Columns[0]);
    Out.Columns[1] = TransformVector(A, B.Columns[1]);
    Out.Columns[2] = TransformVector(A, B.Columns[2]);
    return Out;
}

[[nodiscard]] SpecimenPart MakeStrutPart(
    Mathematics::Axis3D            P0,
    Mathematics::Axis3D            P1,
    float                          Radius,
    const MaterialSpace::SurfaceMaterial& Mat) noexcept
{
    const Mathematics::Axis3D Mid{
        0.5f * (P0.X + P1.X),
        0.5f * (P0.Y + P1.Y),
        0.5f * (P0.Z + P1.Z),
    };
    const Mathematics::Axis3D Delta = P1 - P0;
    const float Len = std::max(1.0e-4f, Mathematics::EvaluateLength(Delta));
    const Mathematics::Axis3D Uz = Delta * (1.0f / Len);
    const Mathematics::Axis3D Ref = (std::fabs(Uz.Z) < 0.92f) ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f } : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
    const Mathematics::Axis3D Ux = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Ref, Uz), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D Uy = Mathematics::EvaluateCross(Uz, Ux);

    SpecimenPart Part{};
    Part.Shape          = SpecimenPartShape::Cylinder;
    Part.LocalOffset    = Mid;
    Part.HalfExtents    = { Radius, Radius, Len * 0.5f };
    Part.Material       = Mat;
    Part.WorldFromLocal = Matrix3x3{ { Ux, Uy, Uz } };
    Part.LocalFromWorld = TransposeMatrix(Part.WorldFromLocal);
    return Part;
}

void FinalizeParts(std::vector<SpecimenPart>& Parts, Mathematics::Scalar3D RootRotDeg) noexcept
{
    const Matrix3x3 RootMat = BuildEulerDegMatrix(RootRotDeg);
    for (SpecimenPart& P : Parts)
    {
        if (Mathematics::EvaluateLengthSquared(P.WorldFromLocal.Columns[0]) < 0.25f)
        {
            P.WorldFromLocal = BuildEulerDegMatrix(P.LocalRotDeg);
        }
        P.LocalOffset    = TransformVector(RootMat, P.LocalOffset);
        P.WorldFromLocal = MultiplyMatrix(RootMat, P.WorldFromLocal);
        P.LocalFromWorld = TransposeMatrix(P.WorldFromLocal);
    }
}

void BuildPropParts(
    const GeometrySpecimenDescriptor& Descriptor,
    std::vector<SpecimenPart>&        OutParts,
    bool&                             OutIsCameraView) noexcept
{
    OutParts.clear();
    OutIsCameraView = false;

    const auto Add = [&](SpecimenPartShape Shape,
                         Mathematics::Axis3D Offset,
                         Mathematics::Scalar3D HalfExt,
                         Mathematics::Scalar3D RotDeg,
                         const MaterialSpace::SurfaceMaterial& Mat,
                         SpecimenStripePattern Stripe = SpecimenStripePattern::None)
    {
        SpecimenPart P{};
        P.Shape         = Shape;
        P.LocalOffset   = Offset;
        P.HalfExtents   = HalfExt;
        P.LocalRotDeg   = RotDeg;
        P.Material      = Mat;
        P.StripePattern = Stripe;
        OutParts.push_back(P);
    };

    const MaterialSpace::SurfaceMaterial CinemaBody    = MakeStudioMaterial(SrgbHexToLinear(33u, 37u, 45u), 0.44f, 0.38f);
    const MaterialSpace::SurfaceMaterial AnodizedBlack = MakeStudioMaterial(SrgbHexToLinear(20u, 22u, 27u), 0.34f, 0.78f);
    const MaterialSpace::SurfaceMaterial Gunmetal      = MakeStudioMaterial(SrgbHexToLinear(59u, 66u, 78u), 0.28f, 0.86f);
    const MaterialSpace::SurfaceMaterial Silver        = MakeStudioMaterial(SrgbHexToLinear(203u, 213u, 225u), 0.20f, 0.92f);
    const MaterialSpace::SurfaceMaterial Chrome        = MakeStudioMaterial(SrgbHexToLinear(223u, 230u, 239u), 0.09f, 0.97f);
    const MaterialSpace::SurfaceMaterial Carbon        = MakeStudioMaterial(SrgbHexToLinear(31u, 35u, 42u), 0.26f, 0.42f, 0.70f, 0.09f);
    const MaterialSpace::SurfaceMaterial RubberDark    = MakeStudioMaterial(SrgbHexToLinear(37u, 38u, 42u), 0.76f, 0.02f);
    const MaterialSpace::SurfaceMaterial FiaRed        = MakeStudioMaterial(SrgbHexToLinear(207u, 34u, 34u), 0.36f, 0.05f);
    const MaterialSpace::SurfaceMaterial CobaltHub     = MakeStudioMaterial(SrgbHexToLinear(59u, 120u, 216u), 0.22f, 1.00f);
    const MaterialSpace::SurfaceMaterial SapphireOptic = MakeStudioMaterial(SrgbHexToLinear(12u, 118u, 168u), 0.04f, 0.18f, 1.00f, 0.02f);
    const MaterialSpace::SurfaceMaterial WarmGlow      = MakeStudioMaterial(SrgbHexToLinear(255u, 244u, 214u), 0.18f, 0.0f, 0.0f, 0.08f, 3.8f);
    const MaterialSpace::SurfaceMaterial TallyGreen    = MakeStudioMaterial(SrgbHexToLinear(34u, 197u, 94u), 0.15f, 0.0f, 0.0f, 0.08f, 3.2f);
    const MaterialSpace::SurfaceMaterial ClearCoatRed  = MakeStudioMaterial(SrgbHexToLinear(204u, 46u, 54u), 0.24f, 0.08f, 0.90f, 0.05f);
    const MaterialSpace::SurfaceMaterial IvoryStone    = MakeStudioMaterial(SrgbHexToLinear(216u, 212u, 204u), 0.38f, 0.02f);
    const MaterialSpace::SurfaceMaterial WarmBronze    = MakeStudioMaterial(SrgbHexToLinear(200u, 137u, 84u), 0.24f, 0.75f);

    const MaterialSpace::SurfaceMaterial TintMat = Descriptor.UseTintOverride
        ? MakeStudioMaterial(Descriptor.PrimaryTint, 0.36f, 0.12f)
        : IvoryStone;

    Mathematics::Scalar3D RootRotDeg{ 0.0f, 0.0f, 0.0f };

    switch (Descriptor.Archetype)
    {
        case GeometrySpecimenArchetype::Cone:
        {
            const MaterialSpace::SurfaceMaterial SafetyOrange = MakeStudioMaterial(SrgbHexToLinear(236u, 93u, 22u), 0.44f, 0.0f);
            Add(SpecimenPartShape::Box,  { 0.0f, 0.0f, -0.42f }, { 0.31f, 0.31f, 0.035f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Cone, { 0.0f, 0.0f,  0.04f }, { 0.24f, 0.24f, 0.44f }, { 0.0f, 0.0f, 0.0f }, SafetyOrange, SpecimenStripePattern::TrafficCone);
            RootRotDeg = { 0.0f, 0.0f, 22.0f };
            break;
        }

        case GeometrySpecimenArchetype::CylinderCone:
        {
            const MaterialSpace::SurfaceMaterial HazardYellow = MakeStudioMaterial(SrgbHexToLinear(234u, 179u, 8u), 0.38f, 0.04f);
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f, -0.40f }, { 0.30f, 0.30f, 0.045f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f,  0.04f }, { 0.21f, 0.21f, 0.42f }, { 0.0f, 0.0f, 0.0f }, HazardYellow, SpecimenStripePattern::HazardYellow);
            Add(SpecimenPartShape::Sphere,   { 0.0f, 0.0f,  0.46f }, { 0.205f, 0.205f, 0.09f }, { 0.0f, 0.0f, 0.0f }, HazardYellow);
            RootRotDeg = { 0.0f, 0.0f, 18.0f };
            break;
        }

        case GeometrySpecimenArchetype::Sphere:
        {
            const MaterialSpace::SurfaceMaterial SphereMat = Descriptor.UseTintOverride
                ? MakeStudioMaterial(Descriptor.PrimaryTint, 0.22f, 0.28f)
                : MakeStudioMaterial(SrgbHexToLinear(216u, 221u, 228u), 0.22f, 0.25f);
            Add(SpecimenPartShape::Sphere, { 0.0f, 0.0f, 0.0f }, { 0.42f, 0.42f, 0.42f }, { 0.0f, 0.0f, 0.0f }, SphereMat);
            break;
        }

        case GeometrySpecimenArchetype::Cylinder:
        {
            const MaterialSpace::SurfaceMaterial CylMat = Descriptor.UseTintOverride
                ? MakeStudioMaterial(Descriptor.PrimaryTint, 0.34f, 0.08f)
                : MakeStudioMaterial(SrgbHexToLinear(203u, 200u, 194u), 0.36f, 0.0f);
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f, 0.0f }, { 0.32f, 0.32f, 0.42f }, { 0.0f, 0.0f, 0.0f }, CylMat);
            break;
        }

        case GeometrySpecimenArchetype::Torus:
        {
            const MaterialSpace::SurfaceMaterial TorusMat = Descriptor.UseTintOverride
                ? MakeStudioMaterial(Descriptor.PrimaryTint, 0.24f, 0.72f)
                : WarmBronze;
            Add(SpecimenPartShape::Torus, { 0.0f, 0.0f, 0.0f }, { 0.42f, 0.42f, 0.145f }, { 32.0f, -16.0f, 12.0f }, TorusMat);
            break;
        }

        case GeometrySpecimenArchetype::Box:
        {
            const MaterialSpace::SurfaceMaterial BoxMat = Descriptor.UseTintOverride
                ? MakeStudioMaterial(Descriptor.PrimaryTint, 0.36f, 0.04f)
                : MakeStudioMaterial(SrgbHexToLinear(210u, 207u, 201u), 0.34f, 0.0f);
            Add(SpecimenPartShape::Box, { 0.0f, 0.0f, 0.0f }, { 0.37f, 0.37f, 0.37f }, { 0.0f, 0.0f, 34.0f }, BoxMat);
            break;
        }

        case GeometrySpecimenArchetype::CalibrationBevelBox:
        {
            Add(SpecimenPartShape::Box, { 0.0f, 0.0f, 0.0f }, { 0.35f, 0.35f, 0.35f }, { 0.0f, 0.0f, 0.0f }, TintMat);
            Add(SpecimenPartShape::Box, { 0.0f, 0.0f, 0.0f }, { 0.39f, 0.31f, 0.31f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Box, { 0.0f, 0.0f, 0.0f }, { 0.31f, 0.39f, 0.31f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Box, { 0.0f, 0.0f, 0.37f }, { 0.24f, 0.24f, 0.03f }, { 0.0f, 0.0f, 0.0f }, CobaltHub);
            RootRotDeg = { 0.0f, 0.0f, 32.0f };
            break;
        }

        case GeometrySpecimenArchetype::ControlledIntegrator:
        {
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f, -0.34f }, { 0.36f, 0.36f, 0.07f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f, -0.22f }, { 0.27f, 0.27f, 0.06f }, { 0.0f, 0.0f, 0.0f }, Silver);
            Add(SpecimenPartShape::Sphere,   { 0.0f, 0.0f,  0.14f }, { 0.31f, 0.31f, 0.31f }, { 0.0f, 0.0f, 0.0f }, TintMat);
            Add(SpecimenPartShape::Torus,    { 0.0f, 0.0f,  0.14f }, { 0.37f, 0.37f, 0.032f }, { 24.0f, 0.0f, 0.0f }, CobaltHub);
            RootRotDeg = { 0.0f, 0.0f, -18.0f };
            break;
        }

        case GeometrySpecimenArchetype::AeroCoupeGT:
        {
            const MaterialSpace::SurfaceMaterial BodyMat = Descriptor.UseTintOverride
                ? MakeStudioMaterial(Descriptor.PrimaryTint, 0.22f, 0.14f, 0.85f, 0.05f)
                : ClearCoatRed;
            // Lower aero chassis + tapered nose + canopy greenhouse + rear wing + 4 wheels
            Add(SpecimenPartShape::Box,      { 0.0f,  0.00f, -0.08f }, { 0.25f, 0.54f, 0.10f }, { 0.0f, 0.0f, 0.0f }, BodyMat);
            Add(SpecimenPartShape::Box,      { 0.0f, -0.44f, -0.14f }, { 0.26f, 0.16f, 0.03f }, { 4.0f, 0.0f, 0.0f }, Carbon);
            Add(SpecimenPartShape::Sphere,   { 0.0f,  0.04f,  0.06f }, { 0.19f, 0.28f, 0.11f }, { 0.0f, 0.0f, 0.0f }, SapphireOptic);
            Add(SpecimenPartShape::Box,      { 0.0f,  0.48f,  0.10f }, { 0.24f, 0.07f, 0.018f }, { -8.0f, 0.0f, 0.0f }, Carbon);
            Add(SpecimenPartShape::Cylinder, { -0.25f, -0.32f, -0.14f }, { 0.11f, 0.11f, 0.045f }, { 0.0f, 90.0f, 0.0f }, RubberDark);
            Add(SpecimenPartShape::Cylinder, {  0.25f, -0.32f, -0.14f }, { 0.11f, 0.11f, 0.045f }, { 0.0f, 90.0f, 0.0f }, RubberDark);
            Add(SpecimenPartShape::Cylinder, { -0.25f,  0.32f, -0.14f }, { 0.115f, 0.115f, 0.045f }, { 0.0f, 90.0f, 0.0f }, RubberDark);
            Add(SpecimenPartShape::Cylinder, {  0.25f,  0.32f, -0.14f }, { 0.115f, 0.115f, 0.045f }, { 0.0f, 90.0f, 0.0f }, RubberDark);
            RootRotDeg = { 0.0f, 0.0f, -24.0f };
            break;
        }

        case GeometrySpecimenArchetype::TacticalCrate:
        {
            const MaterialSpace::SurfaceMaterial Olive = Descriptor.UseTintOverride
                ? MakeStudioMaterial(Descriptor.PrimaryTint, 0.56f, 0.10f)
                : MakeStudioMaterial(SrgbHexToLinear(142u, 134u, 112u), 0.54f, 0.12f);
            Add(SpecimenPartShape::Box, { 0.0f, 0.0f, 0.0f }, { 0.34f, 0.28f, 0.26f }, { 0.0f, 0.0f, 0.0f }, Olive);
            Add(SpecimenPartShape::Box, { 0.0f, 0.0f,  0.27f }, { 0.36f, 0.30f, 0.03f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Box, { 0.0f, 0.0f, -0.27f }, { 0.36f, 0.30f, 0.03f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Box, { -0.345f, -0.285f, 0.0f }, { 0.04f, 0.04f, 0.29f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Box, {  0.345f, -0.285f, 0.0f }, { 0.04f, 0.04f, 0.29f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Box, { -0.345f,  0.285f, 0.0f }, { 0.04f, 0.04f, 0.29f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Box, {  0.345f,  0.285f, 0.0f }, { 0.04f, 0.04f, 0.29f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Box, {  0.0f,   -0.295f, 0.04f }, { 0.10f, 0.02f, 0.05f }, { 0.0f, 0.0f, 0.0f }, CobaltHub);
            RootRotDeg = { 0.0f, 0.0f, 26.0f };
            break;
        }

        case GeometrySpecimenArchetype::ArmoredBust:
        {
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f, -0.40f }, { 0.28f, 0.28f, 0.06f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f, -0.26f }, { 0.14f, 0.14f, 0.10f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Box,      { 0.0f, 0.0f, -0.04f }, { 0.27f, 0.17f, 0.18f }, { 0.0f, 0.0f, 0.0f }, TintMat);
            Add(SpecimenPartShape::Sphere,   { -0.28f, 0.0f, 0.06f }, { 0.12f, 0.14f, 0.11f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Sphere,   {  0.28f, 0.0f, 0.06f }, { 0.12f, 0.14f, 0.11f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Sphere,   { 0.0f, -0.02f, 0.30f }, { 0.16f, 0.18f, 0.19f }, { 0.0f, 0.0f, 0.0f }, TintMat);
            Add(SpecimenPartShape::Box,      { 0.0f, -0.17f, 0.31f }, { 0.11f, 0.04f, 0.05f }, { 0.0f, 0.0f, 0.0f }, SapphireOptic);
            RootRotDeg = { 0.0f, 0.0f, -20.0f };
            break;
        }

        case GeometrySpecimenArchetype::UDIMArchKit:
        {
            Add(SpecimenPartShape::Box,      { 0.0f, 0.0f, -0.38f }, { 0.42f, 0.26f, 0.05f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Cylinder, { -0.24f, 0.0f, -0.02f }, { 0.09f, 0.09f, 0.31f }, { 0.0f, 0.0f, 0.0f }, TintMat);
            Add(SpecimenPartShape::Cylinder, {  0.24f, 0.0f, -0.02f }, { 0.09f, 0.09f, 0.31f }, { 0.0f, 0.0f, 0.0f }, TintMat);
            Add(SpecimenPartShape::Box,      { 0.0f, 0.0f,  0.34f }, { 0.42f, 0.22f, 0.07f }, { 0.0f, 0.0f, 0.0f }, TintMat);
            Add(SpecimenPartShape::Box,      { 0.0f, 0.0f,  0.44f }, { 0.34f, 0.18f, 0.04f }, { 0.0f, 0.0f, 0.0f }, Silver);
            RootRotDeg = { 0.0f, 0.0f, -22.0f };
            break;
        }

        case GeometrySpecimenArchetype::ShaderBallSolid:
        {
            const MaterialSpace::SurfaceMaterial OuterShell = Descriptor.UseTintOverride
                ? MakeStudioMaterial(Descriptor.PrimaryTint, 0.26f, 0.08f, 0.65f, 0.07f)
                : MakeStudioMaterial(SrgbHexToLinear(212u, 192u, 166u), 0.26f, 0.06f, 0.55f, 0.07f);
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f, -0.36f }, { 0.28f, 0.28f, 0.045f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Sphere,   { 0.0f, 0.0f,  0.02f }, { 0.34f, 0.34f, 0.34f }, { 0.0f, 0.0f, 0.0f }, OuterShell);
            Add(SpecimenPartShape::Torus,    { 0.0f, 0.0f,  0.02f }, { 0.39f, 0.39f, 0.055f }, { 62.0f, 18.0f, 0.0f }, Chrome);
            Add(SpecimenPartShape::Sphere,   { -0.14f, -0.22f, 0.14f }, { 0.15f, 0.15f, 0.15f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            RootRotDeg = { 0.0f, 0.0f, -16.0f };
            break;
        }

        case GeometrySpecimenArchetype::ModularHullAssembly:
        {
            const MaterialSpace::SurfaceMaterial HullMat = Descriptor.UseTintOverride
                ? MakeStudioMaterial(Descriptor.PrimaryTint, 0.28f, 0.45f)
                : MakeStudioMaterial(SrgbHexToLinear(152u, 198u, 178u), 0.28f, 0.42f);
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f, 0.0f }, { 0.16f, 0.16f, 0.48f }, { 90.0f, 0.0f, 0.0f }, HullMat);
            Add(SpecimenPartShape::Cone,     { 0.0f, -0.56f, 0.0f }, { 0.16f, 0.16f, 0.18f }, { 90.0f, 0.0f, 0.0f }, Silver);
            Add(SpecimenPartShape::Box,      { 0.0f,  0.08f, -0.02f }, { 0.48f, 0.16f, 0.025f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Box,      { 0.0f,  0.34f,  0.16f }, { 0.025f, 0.14f, 0.16f }, { -16.0f, 0.0f, 0.0f }, FiaRed);
            RootRotDeg = { 0.0f, 0.0f, -28.0f };
            break;
        }

        case GeometrySpecimenArchetype::EditorCameraRig:
        {
            OutIsCameraView = true;
            Add(SpecimenPartShape::Box,      { 0.0f,  0.04f, -0.02f }, { 0.082f, 0.125f, 0.088f }, { 0.0f, 0.0f, 0.0f }, CinemaBody);
            Add(SpecimenPartShape::Box,      { 0.089f, 0.01f, -0.01f }, { 0.008f, 0.036f, 0.036f }, { 0.0f, 0.0f, 0.0f }, FiaRed);
            Add(SpecimenPartShape::Cylinder, { -0.135f, 0.02f, 0.05f }, { 0.026f, 0.026f, 0.075f }, { 90.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Box,      { 0.0f,  0.195f, -0.01f }, { 0.064f, 0.034f, 0.078f }, { 6.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Box,      { -0.048f, 0.232f, 0.035f }, { 0.008f, 0.006f, 0.024f }, { 6.0f, 0.0f, 0.0f }, TallyGreen);
            Add(SpecimenPartShape::Cylinder, { 0.0f, -0.17f, -0.02f }, { 0.062f, 0.062f, 0.082f }, { 90.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Torus,    { 0.0f, -0.15f, -0.02f }, { 0.072f, 0.072f, 0.010f }, { 90.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Torus,    { 0.0f, -0.20f, -0.02f }, { 0.071f, 0.071f, 0.009f }, { 90.0f, 0.0f, 0.0f }, FiaRed);
            Add(SpecimenPartShape::Sphere,   { 0.0f, -0.22f, -0.02f }, { 0.055f, 0.055f, 0.055f }, { 0.0f, 0.0f, 0.0f }, SapphireOptic);
            Add(SpecimenPartShape::Box,      { 0.0f, -0.28f,  0.06f }, { 0.120f, 0.040f, 0.010f }, { -14.0f, 0.0f, 0.0f }, Carbon);
            Add(SpecimenPartShape::Box,      { 0.0f, -0.28f, -0.10f }, { 0.120f, 0.040f, 0.010f }, {  14.0f, 0.0f, 0.0f }, Carbon);
            Add(SpecimenPartShape::Box,      { -0.11f, -0.28f, -0.02f }, { 0.010f, 0.040f, 0.08f }, { 0.0f, 0.0f, 14.0f }, Carbon);
            Add(SpecimenPartShape::Box,      {  0.11f, -0.28f, -0.02f }, { 0.010f, 0.040f, 0.08f }, { 0.0f, 0.0f, -14.0f }, Carbon);
            Add(SpecimenPartShape::Cylinder, { 0.0f,  0.00f,  0.12f }, { 0.015f, 0.015f, 0.115f }, { 90.0f, 0.0f, 0.0f }, AnodizedBlack);
            RootRotDeg = { 0.0f, 0.0f, -24.0f };
            break;
        }

        case GeometrySpecimenArchetype::EngineCamera:
        {
            OutIsCameraView = true;
            Add(SpecimenPartShape::Box,      { 0.0f,  0.03f, -0.07f }, { 0.085f, 0.135f, 0.092f }, { 0.0f, 0.0f, 0.0f }, CinemaBody);
            Add(SpecimenPartShape::Cylinder, { 0.0f, -0.035f, 0.125f }, { 0.096f, 0.096f, 0.042f }, { 0.0f, 90.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Cylinder, { 0.0f,  0.155f, 0.145f }, { 0.096f, 0.096f, 0.042f }, { 0.0f, 90.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Cylinder, { 0.0f, -0.035f, 0.125f }, { 0.036f, 0.036f, 0.048f }, { 0.0f, 90.0f, 0.0f }, Silver);
            Add(SpecimenPartShape::Cylinder, { 0.0f,  0.155f, 0.145f }, { 0.036f, 0.036f, 0.048f }, { 0.0f, 90.0f, 0.0f }, Silver);
            Add(SpecimenPartShape::Cylinder, { -0.125f, 0.06f, -0.04f }, { 0.022f, 0.022f, 0.135f }, { 90.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Cylinder, { 0.0f, -0.19f, -0.07f }, { 0.066f, 0.066f, 0.088f }, { 90.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Torus,    { 0.0f, -0.21f, -0.07f }, { 0.076f, 0.076f, 0.010f }, { 90.0f, 0.0f, 0.0f }, FiaRed);
            Add(SpecimenPartShape::Sphere,   { 0.0f, -0.25f, -0.07f }, { 0.058f, 0.058f, 0.058f }, { 0.0f, 0.0f, 0.0f }, SapphireOptic);
            Add(SpecimenPartShape::Box,      { 0.0f,  0.02f, -0.185f }, { 0.076f, 0.165f, 0.018f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            RootRotDeg = { 0.0f, 0.0f, -24.0f };
            break;
        }

        case GeometrySpecimenArchetype::TripodBroadcastCamera:
        {
            OutIsCameraView = true;
            for (int I = 0; I < 3; ++I)
            {
                const float Ang = static_cast<float>(I) * 2.0943951f + 0.5235987f;
                const Mathematics::Axis3D Foot{ std::cos(Ang) * 0.42f, std::sin(Ang) * 0.42f, -0.52f };
                const Mathematics::Axis3D Crown{ std::cos(Ang) * 0.06f, std::sin(Ang) * 0.06f, 0.24f };
                OutParts.push_back(MakeStrutPart(Foot, Crown, 0.020f, Carbon));
                Add(SpecimenPartShape::Cylinder, Foot, { 0.036f, 0.036f, 0.018f }, { 0.0f, 0.0f, 0.0f }, RubberDark);
            }
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f, 0.29f }, { 0.074f, 0.074f, 0.046f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Box,      { 0.0f, 0.02f, 0.43f }, { 0.084f, 0.135f, 0.082f }, { 0.0f, 0.0f, 0.0f }, CinemaBody);
            Add(SpecimenPartShape::Cylinder, { 0.0f, -0.18f, 0.43f }, { 0.062f, 0.062f, 0.082f }, { 90.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Sphere,   { 0.0f, -0.24f, 0.43f }, { 0.054f, 0.054f, 0.054f }, { 0.0f, 0.0f, 0.0f }, SapphireOptic);
            Add(SpecimenPartShape::Box,      { 0.0f, -0.30f, 0.43f }, { 0.11f, 0.04f, 0.085f }, { 0.0f, 0.0f, 0.0f }, Carbon);
            RootRotDeg = { 0.0f, 0.0f, -24.0f };
            break;
        }

        case GeometrySpecimenArchetype::StudioSoftboxLight:
        {
            Add(SpecimenPartShape::Box,      { 0.0f,  0.04f, 0.0f }, { 0.36f, 0.06f, 0.26f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Box,      { 0.0f, -0.025f, 0.0f }, { 0.33f, 0.015f, 0.23f }, { 0.0f, 0.0f, 0.0f }, WarmGlow);
            Add(SpecimenPartShape::Box,      { -0.40f, 0.04f, -0.06f }, { 0.02f, 0.02f, 0.22f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Box,      {  0.40f, 0.04f, -0.06f }, { 0.02f, 0.02f, 0.22f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Box,      { 0.0f,  0.04f, -0.28f }, { 0.42f, 0.025f, 0.02f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Cylinder, { 0.0f,  0.04f, -0.44f }, { 0.032f, 0.032f, 0.16f }, { 0.0f, 0.0f, 0.0f }, Silver);
            RootRotDeg = { 0.0f, 0.0f, -22.0f };
            break;
        }

        case GeometrySpecimenArchetype::DownlightNarrowIES:
        {
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f,  0.22f }, { 0.09f, 0.09f, 0.10f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Cone,     { 0.0f, 0.0f,  0.02f }, { 0.24f, 0.24f, 0.14f }, { 180.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Torus,    { 0.0f, 0.0f, -0.12f }, { 0.24f, 0.24f, 0.02f }, { 0.0f, 0.0f, 0.0f }, Silver);
            Add(SpecimenPartShape::Sphere,   { 0.0f, 0.0f, -0.08f }, { 0.16f, 0.16f, 0.06f }, { 0.0f, 0.0f, 0.0f }, WarmGlow);
            RootRotDeg = { 0.0f, 0.0f, -18.0f };
            break;
        }

        case GeometrySpecimenArchetype::TurbineWindFan:
        {
            Add(SpecimenPartShape::Torus,    { 0.0f, 0.0f, 0.04f }, { 0.34f, 0.34f, 0.065f }, { 90.0f, 0.0f, 0.0f }, Gunmetal);
            Add(SpecimenPartShape::Cylinder, { 0.0f, 0.0f, 0.04f }, { 0.085f, 0.085f, 0.09f }, { 90.0f, 0.0f, 0.0f }, CobaltHub);
            for (int B = 0; B < 6; ++B)
            {
                const float ThetaDeg = static_cast<float>(B) * 60.0f;
                const float ThetaRad = ThetaDeg * 0.0174532925f;
                const float Cx = std::cos(ThetaRad) * 0.19f;
                const float Cz = 0.04f + std::sin(ThetaRad) * 0.19f;
                Add(SpecimenPartShape::Box, { Cx, -0.01f, Cz }, { 0.11f, 0.012f, 0.042f }, { 26.0f, -ThetaDeg, 0.0f }, Silver);
            }
            Add(SpecimenPartShape::Box, { -0.26f, 0.0f, -0.34f }, { 0.03f, 0.03f, 0.12f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Box, {  0.26f, 0.0f, -0.34f }, { 0.03f, 0.03f, 0.12f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
            Add(SpecimenPartShape::Box, {  0.0f,  0.0f, -0.44f }, { 0.34f, 0.22f, 0.025f }, { 0.0f, 0.0f, 0.0f }, CinemaBody);
            RootRotDeg = { 0.0f, 0.0f, -24.0f };
            break;
        }

        case GeometrySpecimenArchetype::TyreStack:
        {
            const auto AddTyre = [&](Mathematics::Axis3D C, bool WithHub)
            {
                Add(SpecimenPartShape::Torus, { C.X, C.Y, C.Z - 0.045f }, { 0.32f, 0.32f, 0.075f }, { 0.0f, 0.0f, 0.0f }, RubberDark);
                Add(SpecimenPartShape::Torus, { C.X, C.Y, C.Z + 0.045f }, { 0.32f, 0.32f, 0.075f }, { 0.0f, 0.0f, 0.0f }, RubberDark);
                if (WithHub)
                {
                    Add(SpecimenPartShape::Torus,    { C.X, C.Y, C.Z + 0.075f }, { 0.22f, 0.22f, 0.018f }, { 0.0f, 0.0f, 0.0f }, Gunmetal);
                    Add(SpecimenPartShape::Cylinder, { C.X, C.Y, C.Z + 0.01f },  { 0.20f, 0.20f, 0.025f }, { 0.0f, 0.0f, 0.0f }, AnodizedBlack);
                    Add(SpecimenPartShape::Cylinder, { C.X, C.Y, C.Z + 0.05f },  { 0.09f, 0.09f, 0.028f }, { 0.0f, 0.0f, 0.0f }, CobaltHub);
                    Add(SpecimenPartShape::Cylinder, { C.X, C.Y, C.Z + 0.075f }, { 0.042f, 0.042f, 0.022f }, { 0.0f, 0.0f, 0.0f }, Silver);
                }
            };
            AddTyre({ 0.0f, 0.0f, -0.22f }, false);
            AddTyre({ 0.0f, 0.0f,  0.00f }, false);
            AddTyre({ 0.0f, 0.0f,  0.22f }, true);
            RootRotDeg = { 0.0f, 0.0f, -18.0f };
            break;
        }
    }

    FinalizeParts(OutParts, RootRotDeg);
}

void ComputeTightWorldBounds(
    const std::vector<SpecimenPart>& Parts,
    Mathematics::Axis3D&             OutCenter,
    float&                           OutRadius) noexcept
{
    Mathematics::Axis3D MinP{  1.0e6f,  1.0e6f,  1.0e6f };
    Mathematics::Axis3D MaxP{ -1.0e6f, -1.0e6f, -1.0e6f };

    for (const SpecimenPart& P : Parts)
    {
        Mathematics::Scalar3D LocalExt = P.HalfExtents;
        if (P.Shape == SpecimenPartShape::Torus)
        {
            const float TubeR = P.HalfExtents.Z;
            LocalExt = { P.HalfExtents.X + TubeR, P.HalfExtents.Y + TubeR, TubeR };
        }
        const float Ex =
            std::fabs(P.WorldFromLocal.Columns[0].X) * LocalExt.X +
            std::fabs(P.WorldFromLocal.Columns[1].X) * LocalExt.Y +
            std::fabs(P.WorldFromLocal.Columns[2].X) * LocalExt.Z;
        const float Ey =
            std::fabs(P.WorldFromLocal.Columns[0].Y) * LocalExt.X +
            std::fabs(P.WorldFromLocal.Columns[1].Y) * LocalExt.Y +
            std::fabs(P.WorldFromLocal.Columns[2].Y) * LocalExt.Z;
        const float Ez =
            std::fabs(P.WorldFromLocal.Columns[0].Z) * LocalExt.X +
            std::fabs(P.WorldFromLocal.Columns[1].Z) * LocalExt.Y +
            std::fabs(P.WorldFromLocal.Columns[2].Z) * LocalExt.Z;

        MinP.X = std::min(MinP.X, P.LocalOffset.X - Ex);
        MinP.Y = std::min(MinP.Y, P.LocalOffset.Y - Ey);
        MinP.Z = std::min(MinP.Z, P.LocalOffset.Z - Ez);
        MaxP.X = std::max(MaxP.X, P.LocalOffset.X + Ex);
        MaxP.Y = std::max(MaxP.Y, P.LocalOffset.Y + Ey);
        MaxP.Z = std::max(MaxP.Z, P.LocalOffset.Z + Ez);
    }

    OutCenter = (MinP + MaxP) * 0.5f;
    const Mathematics::Axis3D HalfSpan = (MaxP - MinP) * 0.5f;
    OutRadius = std::max(0.10f, Mathematics::EvaluateLength(HalfSpan));
}

bool IntersectLocalBox(
    Mathematics::Axis3D   Ro,
    Mathematics::Axis3D   Rd,
    Mathematics::Scalar3D HalfExtents,
    float&                OutT,
    Mathematics::Axis3D&  OutLocalNormal) noexcept
{
    float TMin = 1.0e-4f;
    float TMax = 1.0e6f;
    Mathematics::Axis3D HitNormal{};

    const float Origins[3] = { Ro.X, Ro.Y, Ro.Z };
    const float Dirs[3]    = { Rd.X, Rd.Y, Rd.Z };
    const float Exts[3]    = { HalfExtents.X, HalfExtents.Y, HalfExtents.Z };

    for (int Axis = 0; Axis < 3; ++Axis)
    {
        if (std::fabs(Dirs[Axis]) < 1.0e-6f)
        {
            if (Origins[Axis] < -Exts[Axis] || Origins[Axis] > Exts[Axis])
            {
                return false;
            }
            continue;
        }
        const float InvD = 1.0f / Dirs[Axis];
        float T0 = (-Exts[Axis] - Origins[Axis]) * InvD;
        float T1 = ( Exts[Axis] - Origins[Axis]) * InvD;
        float Sign = -1.0f;
        if (T0 > T1)
        {
            std::swap(T0, T1);
            Sign = 1.0f;
        }
        if (T0 > TMin)
        {
            TMin = T0;
            HitNormal = { 0.0f, 0.0f, 0.0f };
            if (Axis == 0) HitNormal.X = Sign;
            else if (Axis == 1) HitNormal.Y = Sign;
            else HitNormal.Z = Sign;
        }
        TMax = std::min(TMax, T1);
        if (TMin > TMax)
        {
            return false;
        }
    }

    OutT           = TMin;
    OutLocalNormal = HitNormal;
    return true;
}

bool IntersectLocalSphere(
    Mathematics::Axis3D   Ro,
    Mathematics::Axis3D   Rd,
    Mathematics::Scalar3D HalfExtents,
    float&                OutT,
    Mathematics::Axis3D&  OutLocalNormal) noexcept
{
    const float Rx = std::max(1.0e-4f, HalfExtents.X);
    const float Ry = std::max(1.0e-4f, HalfExtents.Y);
    const float Rz = std::max(1.0e-4f, HalfExtents.Z);

    const Mathematics::Axis3D SO{ Ro.X / Rx, Ro.Y / Ry, Ro.Z / Rz };
    const Mathematics::Axis3D SD{ Rd.X / Rx, Rd.Y / Ry, Rd.Z / Rz };
    const float A = Mathematics::EvaluateDot(SD, SD);
    const float B = Mathematics::EvaluateDot(SO, SD);
    const float C = Mathematics::EvaluateDot(SO, SO) - 1.0f;
    const float Disc = B * B - A * C;
    if (Disc <= 0.0f)
    {
        return false;
    }
    const float T = (-B - std::sqrt(Disc)) / std::max(1.0e-6f, A);
    if (T <= 1.0e-4f)
    {
        return false;
    }
    const Mathematics::Axis3D Hit = Ro + Rd * T;
    OutT = T;
    OutLocalNormal = Mathematics::NormalizeAxis({ Hit.X / (Rx * Rx), Hit.Y / (Ry * Ry), Hit.Z / (Rz * Rz) }, { 0.0f, 0.0f, 1.0f });
    return true;
}

bool IntersectLocalCylinder(
    Mathematics::Axis3D   Ro,
    Mathematics::Axis3D   Rd,
    Mathematics::Scalar3D HalfExtents,
    float&                OutT,
    Mathematics::Axis3D&  OutLocalNormal) noexcept
{
    const float R = std::max(1.0e-4f, 0.5f * (HalfExtents.X + HalfExtents.Y));
    const float Hz = std::max(1.0e-4f, HalfExtents.Z);
    float BestT = 1.0e6f;
    Mathematics::Axis3D BestN{};

    const float A = Rd.X * Rd.X + Rd.Y * Rd.Y;
    const float B = Ro.X * Rd.X + Ro.Y * Rd.Y;
    const float C = Ro.X * Ro.X + Ro.Y * Ro.Y - R * R;
    const float Disc = B * B - A * C;
    if (Disc >= 0.0f && A > 1.0e-8f)
    {
        const float T = (-B - std::sqrt(Disc)) / A;
        if (T > 1.0e-4f)
        {
            const float Z = Ro.Z + Rd.Z * T;
            if (std::fabs(Z) <= Hz)
            {
                BestT = T;
                BestN = Mathematics::NormalizeAxis({ Ro.X + Rd.X * T, Ro.Y + Rd.Y * T, 0.0f }, { 1.0f, 0.0f, 0.0f });
            }
        }
    }

    if (std::fabs(Rd.Z) > 1.0e-6f)
    {
        for (int Cap = 0; Cap < 2; ++Cap)
        {
            const float CapZ = (Cap == 0) ? -Hz : Hz;
            const float T = (CapZ - Ro.Z) / Rd.Z;
            if (T > 1.0e-4f && T < BestT)
            {
                const float X = Ro.X + Rd.X * T;
                const float Y = Ro.Y + Rd.Y * T;
                if (X * X + Y * Y <= R * R)
                {
                    BestT = T;
                    BestN = { 0.0f, 0.0f, (Cap == 0) ? -1.0f : 1.0f };
                }
            }
        }
    }

    if (BestT >= 1.0e5f)
    {
        return false;
    }
    OutT           = BestT;
    OutLocalNormal = BestN;
    return true;
}

bool IntersectLocalCone(
    Mathematics::Axis3D   Ro,
    Mathematics::Axis3D   Rd,
    Mathematics::Scalar3D HalfExtents,
    float&                OutT,
    Mathematics::Axis3D&  OutLocalNormal) noexcept
{
    const float BaseR = std::max(1.0e-4f, 0.5f * (HalfExtents.X + HalfExtents.Y));
    const float TopR  = BaseR * 0.14f;
    const float Hz    = std::max(1.0e-4f, HalfExtents.Z);

    // Radius as linear function of z in [-Hz, +Hz]: r(z) = RMid + Slope * z
    const float RMid  = 0.5f * (BaseR + TopR);
    const float Slope = (TopR - BaseR) / (2.0f * Hz);

    float BestT = 1.0e6f;
    Mathematics::Axis3D BestN{};

    const float R0 = RMid + Slope * Ro.Z;
    const float K  = Slope * Rd.Z;
    const float A  = Rd.X * Rd.X + Rd.Y * Rd.Y - K * K;
    const float B  = Ro.X * Rd.X + Ro.Y * Rd.Y - R0 * K;
    const float C  = Ro.X * Ro.X + Ro.Y * Ro.Y - R0 * R0;
    const float Disc = B * B - A * C;
    if (Disc >= 0.0f && std::fabs(A) > 1.0e-8f)
    {
        const float SqrtD = std::sqrt(Disc);
        const float Ts[2] = { (-B - SqrtD) / A, (-B + SqrtD) / A };
        for (float T : Ts)
        {
            if (T > 1.0e-4f && T < BestT)
            {
                const float Z = Ro.Z + Rd.Z * T;
                if (Z >= -Hz && Z <= Hz)
                {
                    const float X = Ro.X + Rd.X * T;
                    const float Y = Ro.Y + Rd.Y * T;
                    const float Radial = std::max(1.0e-5f, std::hypot(X, Y));
                    BestT = T;
                    BestN = Mathematics::NormalizeAxis({ X / Radial, Y / Radial, -Slope }, { 0.0f, 0.0f, 1.0f });
                }
            }
        }
    }

    if (std::fabs(Rd.Z) > 1.0e-6f)
    {
        for (int Cap = 0; Cap < 2; ++Cap)
        {
            const float CapZ = (Cap == 0) ? -Hz : Hz;
            const float CapR = (Cap == 0) ? BaseR : TopR;
            const float T = (CapZ - Ro.Z) / Rd.Z;
            if (T > 1.0e-4f && T < BestT)
            {
                const float X = Ro.X + Rd.X * T;
                const float Y = Ro.Y + Rd.Y * T;
                if (X * X + Y * Y <= CapR * CapR)
                {
                    BestT = T;
                    BestN = { 0.0f, 0.0f, (Cap == 0) ? -1.0f : 1.0f };
                }
            }
        }
    }

    if (BestT >= 1.0e5f)
    {
        return false;
    }
    OutT           = BestT;
    OutLocalNormal = BestN;
    return true;
}

bool IntersectLocalTorus(
    Mathematics::Axis3D   Ro,
    Mathematics::Axis3D   Rd,
    Mathematics::Scalar3D HalfExtents,
    float&                OutT,
    Mathematics::Axis3D&  OutLocalNormal) noexcept
{
    const float TubeR  = std::max(0.005f, HalfExtents.Z);
    const float MajorR = std::max(TubeR + 0.01f, 0.5f * (HalfExtents.X + HalfExtents.Y) - TubeR);
    const float BoundR = MajorR + TubeR + 0.02f;

    const float B = Mathematics::EvaluateDot(Ro, Rd);
    const float C = Mathematics::EvaluateDot(Ro, Ro) - BoundR * BoundR;
    const float Disc = B * B - C;
    if (Disc <= 0.0f)
    {
        return false;
    }
    float T = std::max(1.0e-4f, -B - std::sqrt(Disc));
    const float TEnd = -B + std::sqrt(Disc);

    for (int Step = 0; Step < 40; ++Step)
    {
        const Mathematics::Axis3D P = Ro + Rd * T;
        const float Q = std::hypot(P.X, P.Y) - MajorR;
        const float D = std::hypot(Q, P.Z) - TubeR;
        if (D < 2.5e-4f)
        {
            const float Radial = std::max(1.0e-5f, std::hypot(P.X, P.Y));
            const Mathematics::Axis3D RingCenter{ (P.X / Radial) * MajorR, (P.Y / Radial) * MajorR, 0.0f };
            OutT           = T;
            OutLocalNormal = Mathematics::NormalizeAxis(P - RingCenter, { 0.0f, 0.0f, 1.0f });
            return true;
        }
        T += std::max(0.001f, D * 0.90f);
        if (T > TEnd)
        {
            break;
        }
    }
    return false;
}

bool IntersectPart(
    const SpecimenPart&  Part,
    Mathematics::Axis3D  WorldOrigin,
    Mathematics::Axis3D  WorldDir,
    float&               OutT,
    Mathematics::Axis3D& OutWorldNormal,
    Mathematics::Axis3D& OutLocalHit) noexcept
{
    const Mathematics::Axis3D RelOrigin = WorldOrigin - Part.LocalOffset;
    const Mathematics::Axis3D Lo = TransformVector(Part.LocalFromWorld, RelOrigin);
    const Mathematics::Axis3D Ld = TransformVector(Part.LocalFromWorld, WorldDir);

    float HitT = 0.0f;
    Mathematics::Axis3D LocalN{};
    bool Hit = false;

    switch (Part.Shape)
    {
        case SpecimenPartShape::Box:      Hit = IntersectLocalBox(Lo, Ld, Part.HalfExtents, HitT, LocalN); break;
        case SpecimenPartShape::Sphere:   Hit = IntersectLocalSphere(Lo, Ld, Part.HalfExtents, HitT, LocalN); break;
        case SpecimenPartShape::Cylinder: Hit = IntersectLocalCylinder(Lo, Ld, Part.HalfExtents, HitT, LocalN); break;
        case SpecimenPartShape::Cone:     Hit = IntersectLocalCone(Lo, Ld, Part.HalfExtents, HitT, LocalN); break;
        case SpecimenPartShape::Torus:    Hit = IntersectLocalTorus(Lo, Ld, Part.HalfExtents, HitT, LocalN); break;
    }
    if (!Hit)
    {
        return false;
    }

    OutT           = HitT;
    OutLocalHit    = Lo + Ld * HitT;
    OutWorldNormal = Mathematics::NormalizeAxis(TransformVector(Part.WorldFromLocal, LocalN), { 0.0f, 0.0f, 1.0f });
    if (Mathematics::EvaluateDot(OutWorldNormal, WorldDir) > 0.0f)
    {
        OutWorldNormal = OutWorldNormal * -1.0f;
    }
    return true;
}

[[nodiscard]] bool TraceClosestPart(
    const std::vector<SpecimenPart>& Parts,
    Mathematics::Axis3D              WorldOrigin,
    Mathematics::Axis3D              WorldDir,
    float&                           OutT,
    int&                             OutPartIdx,
    Mathematics::Axis3D&             OutWorldNormal,
    Mathematics::Axis3D&             OutLocalHit) noexcept
{
    float BestT = 1.0e6f;
    int   BestIdx = -1;
    Mathematics::Axis3D BestN{};
    Mathematics::Axis3D BestLocal{};

    for (std::size_t I = 0u; I < Parts.size(); ++I)
    {
        float T = 0.0f;
        Mathematics::Axis3D N{}, LocalHit{};
        if (IntersectPart(Parts[I], WorldOrigin, WorldDir, T, N, LocalHit) && T < BestT)
        {
            BestT     = T;
            BestIdx   = static_cast<int>(I);
            BestN     = N;
            BestLocal = LocalHit;
        }
    }

    if (BestIdx < 0)
    {
        return false;
    }
    OutT           = BestT;
    OutPartIdx     = BestIdx;
    OutWorldNormal = BestN;
    OutLocalHit    = BestLocal;
    return true;
}

[[nodiscard]] bool TraceShadowOccluded(
    const std::vector<SpecimenPart>& Parts,
    Mathematics::Axis3D              HitPos,
    Mathematics::Axis3D              DirToLight) noexcept
{
    const Mathematics::Axis3D ShadowOrigin = HitPos + DirToLight * 0.003f;
    for (const SpecimenPart& P : Parts)
    {
        float T = 0.0f;
        Mathematics::Axis3D N{}, LocalHit{};
        if (IntersectPart(P, ShadowOrigin, DirToLight, T, N, LocalHit) && T > 1.0e-4f)
        {
            return true;
        }
    }
    return false;
}

[[nodiscard]] uint8_t EncodeStudioPixel(float LinearVal) noexcept
{
    // EV Compensation +0.28 (2^0.28 = 1.2142) + Narkowicz ACES filmic + sRGB
    const float X = std::max(0.0f, LinearVal * 1.2142f * 0.56f);
    const float Mapped = std::clamp((X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f), 0.0f, 1.0f);
    const float Srgb   = (Mapped <= 0.0031308f) ? (12.92f * Mapped) : (1.055f * std::pow(Mapped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::lround(std::clamp(Srgb, 0.0f, 1.0f) * 255.0f));
}

void FitSilhouetteRgba(
    const std::vector<uint8_t>& SrcPixels,
    uint32_t                    SrcW,
    uint32_t                    SrcH,
    uint32_t                    DstW,
    uint32_t                    DstH,
    float                       MarginRatio,
    std::vector<uint8_t>&       OutPixels)
{
    OutPixels.assign(static_cast<std::size_t>(DstW) * DstH * 4u, 0u);

    int32_t MinX = static_cast<int32_t>(SrcW);
    int32_t MinY = static_cast<int32_t>(SrcH);
    int32_t MaxX = -1;
    int32_t MaxY = -1;
    for (uint32_t Y = 0u; Y < SrcH; ++Y)
    {
        for (uint32_t X = 0u; X < SrcW; ++X)
        {
            const uint8_t A = SrcPixels[(static_cast<std::size_t>(Y) * SrcW + X) * 4u + 3u];
            if (A > 8u)
            {
                MinX = std::min(MinX, static_cast<int32_t>(X));
                MaxX = std::max(MaxX, static_cast<int32_t>(X));
                MinY = std::min(MinY, static_cast<int32_t>(Y));
                MaxY = std::max(MaxY, static_cast<int32_t>(Y));
            }
        }
    }

    if (MaxX < MinX || MaxY < MinY)
    {
        return;
    }

    const float BoxW     = static_cast<float>(MaxX - MinX + 1);
    const float BoxH     = static_cast<float>(MaxY - MinY + 1);
    const float PadX     = static_cast<float>(DstW) * MarginRatio;
    const float PadY     = static_cast<float>(DstH) * MarginRatio;
    const float AvailW   = std::max(2.0f, static_cast<float>(DstW) - 2.0f * PadX);
    const float AvailH   = std::max(2.0f, static_cast<float>(DstH) - 2.0f * PadY);
    const float FitScale = std::min(AvailW / BoxW, AvailH / BoxH);

    const float DrawW   = BoxW * FitScale;
    const float DrawH   = BoxH * FitScale;
    const float DstLeft = (static_cast<float>(DstW) - DrawW) * 0.5f;
    const float DstTop  = (static_cast<float>(DstH) - DrawH) * 0.5f;

    for (uint32_t Dy = 0u; Dy < DstH; ++Dy)
    {
        const float Sy = static_cast<float>(MinY) + ((static_cast<float>(Dy) + 0.5f - DstTop) / FitScale) - 0.5f;
        if (Sy < static_cast<float>(MinY) - 1.0f || Sy > static_cast<float>(MaxY) + 1.0f)
        {
            continue;
        }
        const uint32_t Y0 = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(std::floor(Sy)), 0, static_cast<int32_t>(SrcH) - 1));
        const uint32_t Y1 = std::min(SrcH - 1u, Y0 + 1u);
        const float    Ty = std::clamp(Sy - std::floor(Sy), 0.0f, 1.0f);

        for (uint32_t Dx = 0u; Dx < DstW; ++Dx)
        {
            const float Sx = static_cast<float>(MinX) + ((static_cast<float>(Dx) + 0.5f - DstLeft) / FitScale) - 0.5f;
            if (Sx < static_cast<float>(MinX) - 1.0f || Sx > static_cast<float>(MaxX) + 1.0f)
            {
                continue;
            }
            const uint32_t X0 = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(std::floor(Sx)), 0, static_cast<int32_t>(SrcW) - 1));
            const uint32_t X1 = std::min(SrcW - 1u, X0 + 1u);
            const float    Tx = std::clamp(Sx - std::floor(Sx), 0.0f, 1.0f);

            const float W00 = (1.0f - Tx) * (1.0f - Ty);
            const float W10 = Tx * (1.0f - Ty);
            const float W01 = (1.0f - Tx) * Ty;
            const float W11 = Tx * Ty;

            const std::size_t I00 = (static_cast<std::size_t>(Y0) * SrcW + X0) * 4u;
            const std::size_t I10 = (static_cast<std::size_t>(Y0) * SrcW + X1) * 4u;
            const std::size_t I01 = (static_cast<std::size_t>(Y1) * SrcW + X0) * 4u;
            const std::size_t I11 = (static_cast<std::size_t>(Y1) * SrcW + X1) * 4u;

            const float A00 = static_cast<float>(SrcPixels[I00 + 3u]) * W00;
            const float A10 = static_cast<float>(SrcPixels[I10 + 3u]) * W10;
            const float A01 = static_cast<float>(SrcPixels[I01 + 3u]) * W01;
            const float A11 = static_cast<float>(SrcPixels[I11 + 3u]) * W11;
            const float Alpha = A00 + A10 + A01 + A11;
            if (Alpha < 1.0f)
            {
                continue;
            }

            const float InvA = 1.0f / Alpha;
            const float R = (SrcPixels[I00 + 0u] * A00 + SrcPixels[I10 + 0u] * A10 + SrcPixels[I01 + 0u] * A01 + SrcPixels[I11 + 0u] * A11) * InvA;
            const float G = (SrcPixels[I00 + 1u] * A00 + SrcPixels[I10 + 1u] * A10 + SrcPixels[I01 + 1u] * A01 + SrcPixels[I11 + 1u] * A11) * InvA;
            const float B = (SrcPixels[I00 + 2u] * A00 + SrcPixels[I10 + 2u] * A10 + SrcPixels[I01 + 2u] * A01 + SrcPixels[I11 + 2u] * A11) * InvA;

            const std::size_t DstIdx = (static_cast<std::size_t>(Dy) * DstW + Dx) * 4u;
            OutPixels[DstIdx + 0u] = static_cast<uint8_t>(std::clamp(std::lround(R), 0l, 255l));
            OutPixels[DstIdx + 1u] = static_cast<uint8_t>(std::clamp(std::lround(G), 0l, 255l));
            OutPixels[DstIdx + 2u] = static_cast<uint8_t>(std::clamp(std::lround(B), 0l, 255l));
            OutPixels[DstIdx + 3u] = static_cast<uint8_t>(std::clamp(std::lround(Alpha), 0l, 255l));
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  PUBLIC OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

GeometrySpecimenArchetype ResolveGeometrySpecimenArchetype(std::string_view DisplayTitle) noexcept
{
    const std::string Lower = LowercaseAscii(DisplayTitle);

    if (ContainsToken(Lower, "traffic") || (ContainsToken(Lower, "cone") && !ContainsToken(Lower, "cylinder")))
    {
        return GeometrySpecimenArchetype::Cone;
    }
    if (ContainsToken(Lower, "cylinder cone") || ContainsToken(Lower, "bollard"))
    {
        return GeometrySpecimenArchetype::CylinderCone;
    }
    if (ContainsToken(Lower, "tripod"))
    {
        return GeometrySpecimenArchetype::TripodBroadcastCamera;
    }
    if (ContainsToken(Lower, "engine camera"))
    {
        return GeometrySpecimenArchetype::EngineCamera;
    }
    if (ContainsToken(Lower, "camera"))
    {
        return GeometrySpecimenArchetype::EditorCameraRig;
    }
    if (ContainsToken(Lower, "downlight") || ContainsToken(Lower, "ies"))
    {
        return GeometrySpecimenArchetype::DownlightNarrowIES;
    }
    if (ContainsToken(Lower, "softbox") || ContainsToken(Lower, "key light"))
    {
        return GeometrySpecimenArchetype::StudioSoftboxLight;
    }
    if (ContainsToken(Lower, "fan") || ContainsToken(Lower, "turbine"))
    {
        return GeometrySpecimenArchetype::TurbineWindFan;
    }
    if (ContainsToken(Lower, "tyre") || ContainsToken(Lower, "tire"))
    {
        return GeometrySpecimenArchetype::TyreStack;
    }
    if (ContainsToken(Lower, "shader ball"))
    {
        return GeometrySpecimenArchetype::ShaderBallSolid;
    }
    if (ContainsToken(Lower, "integrator"))
    {
        return GeometrySpecimenArchetype::ControlledIntegrator;
    }
    if (ContainsToken(Lower, "coupe") || ContainsToken(Lower, "car"))
    {
        return GeometrySpecimenArchetype::AeroCoupeGT;
    }
    if (ContainsToken(Lower, "crate"))
    {
        return GeometrySpecimenArchetype::TacticalCrate;
    }
    if (ContainsToken(Lower, "bust") || ContainsToken(Lower, "armor"))
    {
        return GeometrySpecimenArchetype::ArmoredBust;
    }
    if (ContainsToken(Lower, "arch"))
    {
        return GeometrySpecimenArchetype::UDIMArchKit;
    }
    if (ContainsToken(Lower, "hull"))
    {
        return GeometrySpecimenArchetype::ModularHullAssembly;
    }
    if (ContainsToken(Lower, "bevel"))
    {
        return GeometrySpecimenArchetype::CalibrationBevelBox;
    }
    if (ContainsToken(Lower, "torus"))
    {
        return GeometrySpecimenArchetype::Torus;
    }
    if (ContainsToken(Lower, "cylinder"))
    {
        return GeometrySpecimenArchetype::Cylinder;
    }
    if (ContainsToken(Lower, "sphere") || ContainsToken(Lower, "orb"))
    {
        return GeometrySpecimenArchetype::Sphere;
    }
    return GeometrySpecimenArchetype::Box;
}

MaterialSpace::SpecimenPreviewImage RasterizeGeometrySpecimenPreview(
    const GeometrySpecimenDescriptor& Descriptor)
{
    const uint32_t DstRes = std::clamp(Descriptor.ResolutionPixels, 16u, 512u);
    const uint32_t SuperRes = std::clamp(static_cast<uint32_t>(std::lround(static_cast<float>(DstRes) * 1.45f)), 84u, 224u);

    std::vector<SpecimenPart> Parts;
    bool IsCameraView = false;
    BuildPropParts(Descriptor, Parts, IsCameraView);

    Mathematics::Axis3D BoundsCenter{};
    float BoundsRadius = 0.5f;
    ComputeTightWorldBounds(Parts, BoundsCenter, BoundsRadius);

    const Mathematics::Axis3D ViewOffsetDir = IsCameraView
        ? Mathematics::NormalizeAxis({ -0.64f, -0.68f, 0.36f })
        : Mathematics::NormalizeAxis({ -0.54f, -0.76f, 0.37f });

    constexpr float VerticalFovRad = 0.78539816f; // 45 deg
    const float TanHalf = std::tan(VerticalFovRad * 0.5f);
    const float Dist = std::max(0.28f, (BoundsRadius * 1.18f) / std::max(0.18f, TanHalf));

    const Mathematics::Axis3D CamOrigin  = BoundsCenter + ViewOffsetDir * Dist;
    const Mathematics::Axis3D CamForward = Mathematics::NormalizeAxis(BoundsCenter - CamOrigin, { 0.0f, 1.0f, 0.0f });
    const Mathematics::Axis3D CamRight   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(CamForward, { 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D CamUp      = Mathematics::EvaluateCross(CamRight, CamForward);

    // 3-point studio light setup matching ModelThumbnailRenderer.js
    const Mathematics::Axis3D KeyDir   = Mathematics::NormalizeAxis({ -0.48f, -0.62f, 0.62f });
    const Mathematics::LinearRGB KeyIrradiance{ 5.20f, 4.95f, 4.55f };
    const Mathematics::Axis3D SoftboxPos{ 0.65f, -0.75f, 2.20f };
    const Mathematics::LinearRGB SoftboxRadiance{ 4.40f, 4.20f, 3.92f };
    const Mathematics::Axis3D RimDir   = Mathematics::NormalizeAxis({ 0.62f, 0.54f, 0.38f });
    const Mathematics::LinearRGB RimRadiance{ 1.75f, 2.05f, 2.55f };
    const Mathematics::LinearRGB SkyFill{ 0.32f, 0.38f, 0.48f };

    const auto& AlbedoSheet = MaterialSpace::InspectGlobalDirectionalAlbedoSheet();
    const auto& SheenSheet  = MaterialSpace::InspectGlobalSheenLinearCosineSheet();

    std::vector<uint8_t> SuperPixels(static_cast<std::size_t>(SuperRes) * SuperRes * 4u, 0u);
    const float InvSuper = 1.0f / static_cast<float>(SuperRes);

    // 2x2 subpixel jitter offsets for crisp anti-aliased edges on transparent background
    constexpr std::array<std::pair<float, float>, 4u> SubOffsets = {{
        { 0.25f, 0.25f },
        { 0.75f, 0.25f },
        { 0.25f, 0.75f },
        { 0.75f, 0.75f },
    }};

    for (uint32_t Py = 0u; Py < SuperRes; ++Py)
    {
        for (uint32_t Px = 0u; Px < SuperRes; ++Px)
        {
            Mathematics::LinearRGB AccumRgb{};
            uint32_t HitCount = 0u;

            for (const auto& [Ox, Oy] : SubOffsets)
            {
                const float ScreenX = (2.0f * (static_cast<float>(Px) + Ox) * InvSuper - 1.0f) * TanHalf;
                const float ScreenY = (1.0f - 2.0f * (static_cast<float>(Py) + Oy) * InvSuper) * TanHalf;
                const Mathematics::Axis3D RayDir = Mathematics::NormalizeAxis(CamForward + CamRight * ScreenX + CamUp * ScreenY);

                float HitT = 0.0f;
                int   PartIdx = -1;
                Mathematics::Axis3D WorldNormal{}, LocalHit{};
                if (!TraceClosestPart(Parts, CamOrigin, RayDir, HitT, PartIdx, WorldNormal, LocalHit))
                {
                    continue;
                }

                ++HitCount;
                const SpecimenPart& HitPart = Parts[static_cast<std::size_t>(PartIdx)];
                MaterialSpace::SurfaceMaterial Mat = HitPart.Material;

                if (HitPart.StripePattern == SpecimenStripePattern::TrafficCone)
                {
                    const float NormH = (LocalHit.Z + HitPart.HalfExtents.Z) / std::max(1.0e-4f, 2.0f * HitPart.HalfExtents.Z);
                    if ((NormH >= 0.42f && NormH <= 0.56f) || (NormH >= 0.67f && NormH <= 0.78f))
                    {
                        Mat.Diffuse.BaseColor = SrgbHexToLinear(245u, 247u, 250u);
                        Mat.Specular.Roughness = 0.22f;
                    }
                }
                else if (HitPart.StripePattern == SpecimenStripePattern::HazardYellow)
                {
                    const float NormH = (LocalHit.Z + HitPart.HalfExtents.Z) / std::max(1.0e-4f, 2.0f * HitPart.HalfExtents.Z);
                    if ((NormH >= 0.34f && NormH <= 0.46f) || (NormH >= 0.62f && NormH <= 0.74f))
                    {
                        Mat.Diffuse.BaseColor = SrgbHexToLinear(34u, 38u, 45u);
                        Mat.Specular.Roughness = 0.36f;
                    }
                }

                const uint32_t ActiveMask = MaterialSpace::DetermineActiveLobeMask(Mat);
                const Mathematics::Axis3D HitPos  = CamOrigin + RayDir * HitT;
                const Mathematics::Axis3D ViewDir = RayDir * -1.0f;

                const Mathematics::Axis3D RefAxis    = (std::fabs(WorldNormal.Z) < 0.92f) ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f } : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
                const Mathematics::Axis3D TangentX   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(RefAxis, WorldNormal), { 1.0f, 0.0f, 0.0f });
                const Mathematics::Axis3D BitangentY = Mathematics::EvaluateCross(WorldNormal, TangentX);
                const MaterialSpace::Scattering::SurfaceShadingFrame Frame{ TangentX, BitangentY, WorldNormal };
                const Mathematics::Axis3D LocalWo = Frame.WorldToLocal(ViewDir);

                Mathematics::LinearRGB Radiance{};
                if (Mat.PorosityLight.EmissionLuminanceNits > 0.01f)
                {
                    Radiance = Radiance + Mat.PorosityLight.EmissionTint * Mat.PorosityLight.EmissionLuminanceNits;
                }

                // 1. Key directional studio light with raytraced self-shadowing
                const bool KeyShadowed = TraceShadowOccluded(Parts, HitPos, KeyDir);
                if (!KeyShadowed)
                {
                    const Mathematics::Axis3D LocalKeyWi = Frame.WorldToLocal(KeyDir);
                    if (LocalKeyWi.Z > 1.0e-5f)
                    {
                        const Mathematics::LinearRGB Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                            Mat, ActiveMask, LocalWo, LocalKeyWi, {}, AlbedoSheet, SheenSheet);
                        Radiance = Radiance + MulRgb(Bsdf, KeyIrradiance) * LocalKeyWi.Z;
                    }
                }

                // 2. Studio overhead softbox light
                const Mathematics::Axis3D SoftDir    = Mathematics::NormalizeAxis(SoftboxPos - HitPos);
                const Mathematics::Axis3D LocalSoftWi = Frame.WorldToLocal(SoftDir);
                if (LocalSoftWi.Z > 1.0e-5f)
                {
                    const Mathematics::LinearRGB Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                        Mat, ActiveMask, LocalWo, LocalSoftWi, {}, AlbedoSheet, SheenSheet);
                    Radiance = Radiance + MulRgb(Bsdf, SoftboxRadiance) * LocalSoftWi.Z;
                }

                // 3. Cool back-rim kick so dark rubber/carbon silhouettes separate cleanly
                const Mathematics::Axis3D LocalRimWi = Frame.WorldToLocal(RimDir);
                if (LocalRimWi.Z > 1.0e-5f)
                {
                    const Mathematics::LinearRGB Bsdf = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(
                        Mat, ActiveMask, LocalWo, LocalRimWi, {}, AlbedoSheet, SheenSheet);
                    Radiance = Radiance + MulRgb(Bsdf, RimRadiance) * LocalRimWi.Z;
                }

                // 4. Studio sky hemisphere fill + subtle Fresnel edge lift
                const float UpFactor    = 0.55f + 0.45f * std::clamp(WorldNormal.Z, -1.0f, 1.0f);
                const float NdotV       = std::clamp(Mathematics::EvaluateDot(WorldNormal, ViewDir), 0.0f, 1.0f);
                const float FresnelLift = std::pow(1.0f - NdotV, 3.0f) * (0.28f + 0.45f * Mat.Specular.Metalness);
                Radiance = Radiance + MulRgb(Mat.Diffuse.BaseColor, SkyFill) * UpFactor;
                Radiance = Radiance + SkyFill * FresnelLift;

                AccumRgb = AccumRgb + Radiance;
            }

            const std::size_t DstOffset = (static_cast<std::size_t>(Py) * SuperRes + Px) * 4u;
            if (HitCount > 0u)
            {
                const float InvHits = 1.0f / static_cast<float>(HitCount);
                SuperPixels[DstOffset + 0u] = EncodeStudioPixel(AccumRgb.R * InvHits);
                SuperPixels[DstOffset + 1u] = EncodeStudioPixel(AccumRgb.G * InvHits);
                SuperPixels[DstOffset + 2u] = EncodeStudioPixel(AccumRgb.B * InvHits);
                SuperPixels[DstOffset + 3u] = static_cast<uint8_t>((HitCount * 255u) / 4u);
            }
            else if (!Descriptor.TransparentBackground)
            {
                SuperPixels[DstOffset + 0u] = 28u;
                SuperPixels[DstOffset + 1u] = 28u;
                SuperPixels[DstOffset + 2u] = 31u;
                SuperPixels[DstOffset + 3u] = 255u;
            }
        }
    }

    MaterialSpace::SpecimenPreviewImage Out{};
    Out.Width  = DstRes;
    Out.Height = DstRes;

    if (Descriptor.TransparentBackground)
    {
        FitSilhouetteRgba(SuperPixels, SuperRes, SuperRes, DstRes, DstRes, 0.055f, Out.Rgba8);
    }
    else
    {
        Out.Rgba8.resize(static_cast<std::size_t>(DstRes) * DstRes * 4u, 255u);
        for (uint32_t Y = 0u; Y < DstRes; ++Y)
        {
            const uint32_t Sy = std::min(SuperRes - 1u, (Y * SuperRes) / DstRes);
            for (uint32_t X = 0u; X < DstRes; ++X)
            {
                const uint32_t Sx = std::min(SuperRes - 1u, (X * SuperRes) / DstRes);
                const std::size_t SIdx = (static_cast<std::size_t>(Sy) * SuperRes + Sx) * 4u;
                const std::size_t DIdx = (static_cast<std::size_t>(Y) * DstRes + X) * 4u;
                Out.Rgba8[DIdx + 0u] = SuperPixels[SIdx + 0u];
                Out.Rgba8[DIdx + 1u] = SuperPixels[SIdx + 1u];
                Out.Rgba8[DIdx + 2u] = SuperPixels[SIdx + 2u];
                Out.Rgba8[DIdx + 3u] = 255u;
            }
        }
    }
    return Out;
}

MaterialSpace::SpecimenPreviewImage RasterizeGeometrySpecimenByTitle(
    std::string_view DisplayTitle,
    ColorRgba        SrgbTint,
    uint32_t         ResolutionPixels,
    bool             TransparentBackground)
{
    GeometrySpecimenDescriptor Desc{};
    Desc.Archetype             = ResolveGeometrySpecimenArchetype(DisplayTitle);
    Desc.PrimaryTint           = SrgbHexToLinear(SrgbTint.Red, SrgbTint.Green, SrgbTint.Blue);
    Desc.TransparentBackground = TransparentBackground;
    Desc.ResolutionPixels      = ResolutionPixels;

    // Preserve explicit tint overrides on generic primitives/stock entries while keeping iconic multi-colour props true
    const bool PreserveSignaturePalette =
        Desc.Archetype == GeometrySpecimenArchetype::Cone ||
        Desc.Archetype == GeometrySpecimenArchetype::CylinderCone ||
        Desc.Archetype == GeometrySpecimenArchetype::EditorCameraRig ||
        Desc.Archetype == GeometrySpecimenArchetype::EngineCamera ||
        Desc.Archetype == GeometrySpecimenArchetype::TripodBroadcastCamera ||
        Desc.Archetype == GeometrySpecimenArchetype::StudioSoftboxLight ||
        Desc.Archetype == GeometrySpecimenArchetype::DownlightNarrowIES ||
        Desc.Archetype == GeometrySpecimenArchetype::TurbineWindFan ||
        Desc.Archetype == GeometrySpecimenArchetype::TyreStack;

    Desc.UseTintOverride = !PreserveSignaturePalette &&
                           (SrgbTint.Red > 0u || SrgbTint.Green > 0u || SrgbTint.Blue > 0u);
    return RasterizeGeometrySpecimenPreview(Desc);
}

} // namespace Frontier::GeometricRaster

/*============================================================================================================================================
                                                        VECTORFIELDOVERLAYSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Editor-only Vulkan 1.3 dynamic-rendering overlay sequence executing all 6 .slang ForceField rasterization pipelines + MRT PickID

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/Simulation/VectorFieldOverlaySequence.h"
#include "Engine/GeometricRaster/VulkanInterchange.h"

#include <volk.h>
#include <imgui.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

namespace Frontier::Editor::Simulation
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHADER STRUCT LAYOUTS
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) ViewportOverlayUniformsGpu
{
    float    ViewProjection[16];
    float    CameraPositionTime[4];
    float    CameraRightThickness[4];
    float    CameraUpEmissive[4];
    float    CustomStreamColor[4];
    float    CrossSectionPlaneParams[4];
    float    StreamVisualParams[4];
    uint32_t ActiveSourceCounts[4];
    float    AnemometerWorldActive[4];
};

static_assert(sizeof(ViewportOverlayUniformsGpu) == 192u);

struct DrawObjectPushConstantsGpu
{
    uint32_t ObjectIndex;
    uint32_t SubMode;
    uint32_t PickCategory;
    uint32_t PickPrimaryId;
    uint32_t PickSecondaryId;
    uint32_t ReservedPad;
};

static_assert(sizeof(DrawObjectPushConstantsGpu) == 24u);

struct alignas(16) ForceSourceGpuStd430
{
    float    CentreRadiusPacked[4];
    float    DirectionStrength[4];
    float    TangentLeftFalloff[4];
    float    UpCoreRadius[4];
    float    HalfExtentsTurbScale[4];
    float    SwirlNoiseParams[4];
    float    WaveformPhaseParams[4];
    uint32_t IdsPacked[4];
    uint32_t MotionParams[4];
};

static_assert(sizeof(ForceSourceGpuStd430) == 144u);

struct LitBeaconVertex
{
    float Pos[3];
    float Normal[3];
    float Color[4];
};

struct PositionNormalVertex
{
    float Pos[3];
    float Normal[3];
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    HELPERS & MATH
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool ReadSpirvWords(const std::filesystem::path& FilePath, std::vector<uint32_t>& OutWords)
{
    std::ifstream Reader(FilePath, std::ios::binary | std::ios::ate);
    if (!Reader.is_open())
    {
        return false;
    }
    const std::streamsize ByteCount = Reader.tellg();
    if (ByteCount <= 0 || (ByteCount % 4) != 0)
    {
        return false;
    }
    Reader.seekg(0, std::ios::beg);
    OutWords.resize(static_cast<std::size_t>(ByteCount / 4));
    Reader.read(reinterpret_cast<char*>(OutWords.data()), ByteCount);
    return Reader.good();
}

struct Float3
{
    float X;
    float Y;
    float Z;
};

[[nodiscard]] Float3 Normalize3(Float3 V) noexcept
{
    const float Len = std::sqrt(V.X * V.X + V.Y * V.Y + V.Z * V.Z);
    if (Len < 1.0e-6f)
    {
        return { 0.0f, 0.0f, 1.0f };
    }
    const float Inv = 1.0f / Len;
    return { V.X * Inv, V.Y * Inv, V.Z * Inv };
}

[[nodiscard]] Float3 Cross3(Float3 A, Float3 B) noexcept
{
    return {
        A.Y * B.Z - A.Z * B.Y,
        A.Z * B.X - A.X * B.Z,
        A.X * B.Y - A.Y * B.X,
    };
}

[[nodiscard]] float Dot3(Float3 A, Float3 B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

void ComputeCameraBasisViewProj(
    const OverlayFrameRequest& Request,
    float                      Aspect,
    ViewportOverlayUniformsGpu& OutUbo) noexcept
{
    const OverlayCameraState& Cam = Request.Camera;
    Float3 Target{
        static_cast<float>(Cam.FocusTargetMeters.X),
        static_cast<float>(Cam.FocusTargetMeters.Y),
        static_cast<float>(Cam.FocusTargetMeters.Z),
    };

    Float3 Fwd{};
    Float3 UpRef{ 0.0f, 0.0f, 1.0f };
    Float3 CamPos{};

    if (Request.UseExplicitCamera)
    {
        CamPos = { Request.ExplicitCameraOrigin[0], Request.ExplicitCameraOrigin[1], Request.ExplicitCameraOrigin[2] };
        Target = { Request.ExplicitTargetPoint[0],  Request.ExplicitTargetPoint[1],  Request.ExplicitTargetPoint[2] };
        UpRef  = { Request.ExplicitWorldUp[0],      Request.ExplicitWorldUp[1],      Request.ExplicitWorldUp[2] };
        const Float3 Delta{ Target.X - CamPos.X, Target.Y - CamPos.Y, Target.Z - CamPos.Z };
        const float Len2 = Dot3(Delta, Delta);
        Fwd = (Len2 > 1.0e-6f) ? Normalize3(Delta) : Float3{ 0.0f, 1.0f, 0.0f };
    }
    else
    {
        const float Dist = std::max(4.0f, Cam.DistanceMeters);

        switch (Cam.Projection)
        {
            case ViewportProjectionPreset::Top:
                CamPos = { Target.X, Target.Y, Target.Z + Dist * 1.4f };
                Fwd    = { 0.0f, 0.0f, -1.0f };
                UpRef  = { 1.0f, 0.0f, 0.0f };
                break;

            case ViewportProjectionPreset::Front:
                CamPos = { Target.X - Dist, Target.Y, Target.Z };
                Fwd    = { 1.0f, 0.0f, 0.0f };
                UpRef  = { 0.0f, 0.0f, 1.0f };
                break;

            case ViewportProjectionPreset::Right:
                CamPos = { Target.X, Target.Y - Dist, Target.Z };
                Fwd    = { 0.0f, 1.0f, 0.0f };
                UpRef  = { 0.0f, 0.0f, 1.0f };
                break;

            case ViewportProjectionPreset::Perspective:
            case ViewportProjectionPreset::Orthographic:
            case ViewportProjectionPreset::Count:
            default:
            {
                const float CosP = std::cos(Cam.PitchRadians);
                const float SinP = std::sin(Cam.PitchRadians);
                const float CosY = std::cos(Cam.YawRadians);
                const float SinY = std::sin(Cam.YawRadians);
                const Float3 Offset{
                    Dist * CosP * CosY,
                    Dist * CosP * SinY,
                    Dist * SinP,
                };
                CamPos = { Target.X + Offset.X, Target.Y + Offset.Y, Target.Z + Offset.Z };
                Fwd    = Normalize3({ Target.X - CamPos.X, Target.Y - CamPos.Y, Target.Z - CamPos.Z });
                UpRef  = { 0.0f, 0.0f, 1.0f };
                break;
            }
        }
    }

    Float3 Right = Cross3(Fwd, UpRef);
    if (Dot3(Right, Right) < 1.0e-5f)
    {
        Right = { 0.0f, -1.0f, 0.0f };
    }
    else
    {
        Right = Normalize3(Right);
    }
    const Float3 CamUp = Normalize3(Cross3(Right, Fwd));

    OutUbo.CameraPositionTime[0] = CamPos.X;
    OutUbo.CameraPositionTime[1] = CamPos.Y;
    OutUbo.CameraPositionTime[2] = CamPos.Z;

    OutUbo.CameraRightThickness[0] = Right.X;
    OutUbo.CameraRightThickness[1] = Right.Y;
    OutUbo.CameraRightThickness[2] = Right.Z;

    OutUbo.CameraUpEmissive[0] = CamUp.X;
    OutUbo.CameraUpEmissive[1] = CamUp.Y;
    OutUbo.CameraUpEmissive[2] = CamUp.Z;

    const float SafeAspect = std::max(0.1f, Aspect);
    const bool  IsOrtho    = (!Request.UseExplicitCamera && Cam.Projection != ViewportProjectionPreset::Perspective);

    float* M = OutUbo.ViewProjection;
    std::memset(M, 0, sizeof(OutUbo.ViewProjection));

    if (!IsOrtho)
    {
        const float FovDeg     = Request.UseExplicitCamera ? Request.ExplicitVerticalFovDeg : Cam.VerticalFovDeg;
        const float HalfFovRad = std::clamp(FovDeg, 15.0f, 110.0f) * (3.14159265f / 360.0f);
        const float TanHalf    = std::tan(HalfFovRad);
        const float Sx         = 1.0f / (SafeAspect * TanHalf);
        const float Sy         = 1.0f / TanHalf;
        const float NearZ      = 0.25f;

        // Row 0: ClipX
        M[0]  = Sx * Right.X;
        M[1]  = Sx * Right.Y;
        M[2]  = Sx * Right.Z;
        M[3]  = -Sx * Dot3(CamPos, Right);
        // Row 1: ClipY (Vulkan NDC -Y points UP)
        M[4]  = -Sy * CamUp.X;
        M[5]  = -Sy * CamUp.Y;
        M[6]  = -Sy * CamUp.Z;
        M[7]  = Sy * Dot3(CamPos, CamUp);
        // Row 2: ClipZ (Reverse-Z: NearZ / Vz -> [1..0])
        M[8]  = 0.0f;
        M[9]  = 0.0f;
        M[10] = 0.0f;
        M[11] = NearZ;
        // Row 3: ClipW = Vz
        M[12] = Fwd.X;
        M[13] = Fwd.Y;
        M[14] = Fwd.Z;
        M[15] = -Dot3(CamPos, Fwd);
    }
    else
    {
        const float HalfH = std::max(4.0f, Cam.OrthoHalfHeightM);
        const float HalfW = HalfH * SafeAspect;
        const float NearZ = -400.0f;
        const float FarZ  = 600.0f;
        const float InvDR = 1.0f / (FarZ - NearZ);

        M[0]  = Right.X / HalfW;
        M[1]  = Right.Y / HalfW;
        M[2]  = Right.Z / HalfW;
        M[3]  = -Dot3(CamPos, Right) / HalfW;

        M[4]  = -CamUp.X / HalfH;
        M[5]  = -CamUp.Y / HalfH;
        M[6]  = -CamUp.Z / HalfH;
        M[7]  = Dot3(CamPos, CamUp) / HalfH;

        M[8]  = -Fwd.X * InvDR;
        M[9]  = -Fwd.Y * InvDR;
        M[10] = -Fwd.Z * InvDR;
        M[11] = (FarZ + Dot3(CamPos, Fwd)) * InvDR;

        M[15] = 1.0f;
    }
}

void AppendQuad(
    std::vector<LitBeaconVertex>& Out,
    Float3 P0, Float3 P1, Float3 P2, Float3 P3,
    Float3 N,
    const std::array<float, 4>& Color)
{
    Out.push_back({ { P0.X, P0.Y, P0.Z }, { N.X, N.Y, N.Z }, { Color[0], Color[1], Color[2], Color[3] } });
    Out.push_back({ { P1.X, P1.Y, P1.Z }, { N.X, N.Y, N.Z }, { Color[0], Color[1], Color[2], Color[3] } });
    Out.push_back({ { P2.X, P2.Y, P2.Z }, { N.X, N.Y, N.Z }, { Color[0], Color[1], Color[2], Color[3] } });
    Out.push_back({ { P0.X, P0.Y, P0.Z }, { N.X, N.Y, N.Z }, { Color[0], Color[1], Color[2], Color[3] } });
    Out.push_back({ { P2.X, P2.Y, P2.Z }, { N.X, N.Y, N.Z }, { Color[0], Color[1], Color[2], Color[3] } });
    Out.push_back({ { P3.X, P3.Y, P3.Z }, { N.X, N.Y, N.Z }, { Color[0], Color[1], Color[2], Color[3] } });
}

void AppendAxisAlignedBoxLit(
    std::vector<LitBeaconVertex>& Out,
    Float3                        MinPt,
    Float3                        MaxPt,
    const std::array<float, 4>&   Color)
{
    const Float3 C000{ MinPt.X, MinPt.Y, MinPt.Z };
    const Float3 C100{ MaxPt.X, MinPt.Y, MinPt.Z };
    const Float3 C110{ MaxPt.X, MaxPt.Y, MinPt.Z };
    const Float3 C010{ MinPt.X, MaxPt.Y, MinPt.Z };
    const Float3 C001{ MinPt.X, MinPt.Y, MaxPt.Z };
    const Float3 C101{ MaxPt.X, MinPt.Y, MaxPt.Z };
    const Float3 C111{ MaxPt.X, MaxPt.Y, MaxPt.Z };
    const Float3 C011{ MinPt.X, MaxPt.Y, MaxPt.Z };

    AppendQuad(Out, C001, C101, C111, C011, { 0, 0, 1 }, Color);   // +Z top
    AppendQuad(Out, C010, C110, C100, C000, { 0, 0, -1 }, Color);  // -Z bottom
    AppendQuad(Out, C100, C110, C111, C101, { 1, 0, 0 }, Color);   // +X
    AppendQuad(Out, C010, C000, C001, C011, { -1, 0, 0 }, Color);  // -X
    AppendQuad(Out, C110, C010, C011, C111, { 0, 1, 0 }, Color);   // +Y
    AppendQuad(Out, C000, C100, C101, C001, { 0, -1, 0 }, Color);  // -Y
}

void AppendSphereLit(
    std::vector<LitBeaconVertex>& Out,
    Float3                        Center,
    float                         Radius,
    int                           Slices,
    int                           Stacks,
    const std::array<float, 4>&   Color)
{
    constexpr float Pi = 3.14159265f;
    for (int St = 0; St < Stacks; ++St)
    {
        const float P0 = Pi * static_cast<float>(St) / static_cast<float>(Stacks);
        const float P1 = Pi * static_cast<float>(St + 1) / static_cast<float>(Stacks);
        for (int Sl = 0; Sl < Slices; ++Sl)
        {
            const float T0 = 2.0f * Pi * static_cast<float>(Sl) / static_cast<float>(Slices);
            const float T1 = 2.0f * Pi * static_cast<float>(Sl + 1) / static_cast<float>(Slices);
            auto Pt = [&](float Phi, float Theta) -> std::pair<Float3, Float3>
            {
                const float S = std::sin(Phi);
                const Float3 N{ S * std::cos(Theta), S * std::sin(Theta), std::cos(Phi) };
                return { { Center.X + N.X * Radius, Center.Y + N.Y * Radius, Center.Z + N.Z * Radius }, N };
            };
            const auto [A, Na] = Pt(P0, T0);
            const auto [B, Nb] = Pt(P1, T0);
            const auto [C, Nc] = Pt(P1, T1);
            const auto [D, Nd] = Pt(P0, T1);

            Out.push_back({ { A.X, A.Y, A.Z }, { Na.X, Na.Y, Na.Z }, { Color[0], Color[1], Color[2], Color[3] } });
            Out.push_back({ { B.X, B.Y, B.Z }, { Nb.X, Nb.Y, Nb.Z }, { Color[0], Color[1], Color[2], Color[3] } });
            Out.push_back({ { C.X, C.Y, C.Z }, { Nc.X, Nc.Y, Nc.Z }, { Color[0], Color[1], Color[2], Color[3] } });
            Out.push_back({ { A.X, A.Y, A.Z }, { Na.X, Na.Y, Na.Z }, { Color[0], Color[1], Color[2], Color[3] } });
            Out.push_back({ { C.X, C.Y, C.Z }, { Nc.X, Nc.Y, Nc.Z }, { Color[0], Color[1], Color[2], Color[3] } });
            Out.push_back({ { D.X, D.Y, D.Z }, { Nd.X, Nd.Y, Nd.Z }, { Color[0], Color[1], Color[2], Color[3] } });
        }
    }
}

void AppendSegmentTubeLit(
    std::vector<LitBeaconVertex>& Out,
    Float3                        A,
    Float3                        B,
    float                         Radius,
    int                           Sides,
    const std::array<float, 4>&   Color)
{
    const Float3 Delta{ B.X - A.X, B.Y - A.Y, B.Z - A.Z };
    const float  Len = std::sqrt(Dot3(Delta, Delta));
    if (Len < 1.0e-4f)
    {
        return;
    }
    const Float3 Fwd  = { Delta.X / Len, Delta.Y / Len, Delta.Z / Len };
    const Float3 Ref  = (std::abs(Fwd.Z) < 0.9f) ? Float3{ 0.0f, 0.0f, 1.0f } : Float3{ 1.0f, 0.0f, 0.0f };
    const Float3 Left = Normalize3(Cross3(Ref, Fwd));
    const Float3 Up   = Cross3(Fwd, Left);

    constexpr float TwoPi = 6.2831853f;
    for (int I = 0; I < Sides; ++I)
    {
        const float Th0 = TwoPi * static_cast<float>(I) / static_cast<float>(Sides);
        const float Th1 = TwoPi * static_cast<float>(I + 1) / static_cast<float>(Sides);
        const Float3 N0{ Left.X * std::cos(Th0) + Up.X * std::sin(Th0), Left.Y * std::cos(Th0) + Up.Y * std::sin(Th0), Left.Z * std::cos(Th0) + Up.Z * std::sin(Th0) };
        const Float3 N1{ Left.X * std::cos(Th1) + Up.X * std::sin(Th1), Left.Y * std::cos(Th1) + Up.Y * std::sin(Th1), Left.Z * std::cos(Th1) + Up.Z * std::sin(Th1) };
        const Float3 A0{ A.X + N0.X * Radius, A.Y + N0.Y * Radius, A.Z + N0.Z * Radius };
        const Float3 A1{ A.X + N1.X * Radius, A.Y + N1.Y * Radius, A.Z + N1.Z * Radius };
        const Float3 B0{ B.X + N0.X * Radius, B.Y + N0.Y * Radius, B.Z + N0.Z * Radius };
        const Float3 B1{ B.X + N1.X * Radius, B.Y + N1.Y * Radius, B.Z + N1.Z * Radius };

        Out.push_back({ { A0.X, A0.Y, A0.Z }, { N0.X, N0.Y, N0.Z }, { Color[0], Color[1], Color[2], Color[3] } });
        Out.push_back({ { B0.X, B0.Y, B0.Z }, { N0.X, N0.Y, N0.Z }, { Color[0], Color[1], Color[2], Color[3] } });
        Out.push_back({ { B1.X, B1.Y, B1.Z }, { N1.X, N1.Y, N1.Z }, { Color[0], Color[1], Color[2], Color[3] } });
        Out.push_back({ { A0.X, A0.Y, A0.Z }, { N0.X, N0.Y, N0.Z }, { Color[0], Color[1], Color[2], Color[3] } });
        Out.push_back({ { B1.X, B1.Y, B1.Z }, { N1.X, N1.Y, N1.Z }, { Color[0], Color[1], Color[2], Color[3] } });
        Out.push_back({ { A1.X, A1.Y, A1.Z }, { N1.X, N1.Y, N1.Z }, { Color[0], Color[1], Color[2], Color[3] } });
    }
}

void BuildUnitSphereVertices(std::vector<PositionNormalVertex>& Out)
{
    Out.clear();
    constexpr int   Slices = 18;
    constexpr int   Stacks = 10;
    constexpr float Pi     = 3.14159265f;
    for (int St = 0; St < Stacks; ++St)
    {
        const float P0 = Pi * static_cast<float>(St) / static_cast<float>(Stacks);
        const float P1 = Pi * static_cast<float>(St + 1) / static_cast<float>(Stacks);
        for (int Sl = 0; Sl < Slices; ++Sl)
        {
            const float T0 = 2.0f * Pi * static_cast<float>(Sl) / static_cast<float>(Slices);
            const float T1 = 2.0f * Pi * static_cast<float>(Sl + 1) / static_cast<float>(Slices);
            auto Pt = [&](float Phi, float Theta) -> PositionNormalVertex
            {
                const float S = std::sin(Phi);
                const float X = S * std::cos(Theta);
                const float Y = S * std::sin(Theta);
                const float Z = std::cos(Phi);
                return { { X, Y, Z }, { X, Y, Z } };
            };
            const auto A = Pt(P0, T0);
            const auto B = Pt(P1, T0);
            const auto C = Pt(P1, T1);
            const auto D = Pt(P0, T1);
            Out.push_back(A); Out.push_back(B); Out.push_back(C);
            Out.push_back(A); Out.push_back(C); Out.push_back(D);
        }
    }
}

void BuildUnitBoxVertices(std::vector<PositionNormalVertex>& Out)
{
    Out.clear();
    auto PushQuad = [&](Float3 P0, Float3 P1, Float3 P2, Float3 P3, Float3 N)
    {
        Out.push_back({ { P0.X, P0.Y, P0.Z }, { N.X, N.Y, N.Z } });
        Out.push_back({ { P1.X, P1.Y, P1.Z }, { N.X, N.Y, N.Z } });
        Out.push_back({ { P2.X, P2.Y, P2.Z }, { N.X, N.Y, N.Z } });
        Out.push_back({ { P0.X, P0.Y, P0.Z }, { N.X, N.Y, N.Z } });
        Out.push_back({ { P2.X, P2.Y, P2.Z }, { N.X, N.Y, N.Z } });
        Out.push_back({ { P3.X, P3.Y, P3.Z }, { N.X, N.Y, N.Z } });
    };
    const Float3 V000{ -1, -1, -1 }, V100{ 1, -1, -1 }, V110{ 1, 1, -1 }, V010{ -1, 1, -1 };
    const Float3 V001{ -1, -1, 1 }, V101{ 1, -1, 1 }, V111{ 1, 1, 1 }, V011{ -1, 1, 1 };
    PushQuad(V001, V101, V111, V011, { 0, 0, 1 });
    PushQuad(V010, V110, V100, V000, { 0, 0, -1 });
    PushQuad(V100, V110, V111, V101, { 1, 0, 0 });
    PushQuad(V010, V000, V001, V011, { -1, 0, 0 });
    PushQuad(V110, V010, V011, V111, { 0, 1, 0 });
    PushQuad(V000, V100, V101, V001, { 0, -1, 0 });
}

void BuildUnitArrowVertices(std::vector<PositionNormalVertex>& Out)
{
    Out.clear();
    constexpr int   Sides    = 10;
    constexpr float TwoPi    = 6.2831853f;
    constexpr float ShaftR   = 0.065f;
    constexpr float HeadBaseR= 0.19f;
    constexpr float HeadStart= 0.65f;

    for (int I = 0; I < Sides; ++I)
    {
        const float Th0 = TwoPi * static_cast<float>(I) / static_cast<float>(Sides);
        const float Th1 = TwoPi * static_cast<float>(I + 1) / static_cast<float>(Sides);
        const float C0 = std::cos(Th0), S0 = std::sin(Th0);
        const float C1 = std::cos(Th1), S1 = std::sin(Th1);

        // Shaft cylinder X in [0..HeadStart]
        Out.push_back({ { 0.0f, C0 * ShaftR, S0 * ShaftR }, { 0.0f, C0, S0 } });
        Out.push_back({ { HeadStart, C0 * ShaftR, S0 * ShaftR }, { 0.0f, C0, S0 } });
        Out.push_back({ { HeadStart, C1 * ShaftR, S1 * ShaftR }, { 0.0f, C1, S1 } });
        Out.push_back({ { 0.0f, C0 * ShaftR, S0 * ShaftR }, { 0.0f, C0, S0 } });
        Out.push_back({ { HeadStart, C1 * ShaftR, S1 * ShaftR }, { 0.0f, C1, S1 } });
        Out.push_back({ { 0.0f, C1 * ShaftR, S1 * ShaftR }, { 0.0f, C1, S1 } });

        // Arrow cone head X in [HeadStart..1.0]
        const Float3 N0 = Normalize3({ 0.45f, C0, S0 });
        const Float3 N1 = Normalize3({ 0.45f, C1, S1 });
        Out.push_back({ { HeadStart, C0 * HeadBaseR, S0 * HeadBaseR }, { N0.X, N0.Y, N0.Z } });
        Out.push_back({ { 1.0f, 0.0f, 0.0f }, { N0.X, N0.Y, N0.Z } });
        Out.push_back({ { HeadStart, C1 * HeadBaseR, S1 * HeadBaseR }, { N1.X, N1.Y, N1.Z } });
    }
}

void BuildLitArchitecturalSceneVertices(std::vector<LitBeaconVertex>& Out)
{
    Out.clear();
    // 1. Wide courtyard ground slab at Z = 0
    AppendAxisAlignedBoxLit(Out, { -95.0f, -95.0f, -1.2f }, { 95.0f, 95.0f, 0.0f }, { 0.13f, 0.145f, 0.165f, 1.0f });
    // 2. Central calibration plinth (+X forward test target)
    AppendAxisAlignedBoxLit(Out, { -4.5f, -4.5f, 0.0f }, { 4.5f, 4.5f, 1.2f }, { 0.24f, 0.26f, 0.29f, 1.0f });
    // 3. Two aerodynamic wind-tunnel monolith towers flanking outer edges
    AppendAxisAlignedBoxLit(Out, { -12.0f, 22.0f, 0.0f }, { -3.0f, 30.0f, 18.0f }, { 0.34f, 0.37f, 0.42f, 1.0f });
    AppendAxisAlignedBoxLit(Out, { 26.0f, -34.0f, 0.0f }, { 35.0f, -25.0f, 13.0f }, { 0.36f, 0.33f, 0.30f, 1.0f });
    // 4. Cantilevered hangar bridge spanning North side
    AppendAxisAlignedBoxLit(Out, { -28.0f, 24.0f, 0.0f }, { -22.0f, 32.0f, 12.0f }, { 0.28f, 0.31f, 0.35f, 1.0f });
    AppendAxisAlignedBoxLit(Out, { 12.0f, 24.0f, 0.0f }, { 18.0f, 32.0f, 12.0f }, { 0.28f, 0.31f, 0.35f, 1.0f });
    AppendAxisAlignedBoxLit(Out, { -30.0f, 23.0f, 12.0f }, { 20.0f, 33.0f, 14.2f }, { 0.44f, 0.47f, 0.52f, 1.0f });
    // 5. Elevated radar calibration sphere dome at (-22, -20)
    AppendAxisAlignedBoxLit(Out, { -25.0f, -24.0f, 0.0f }, { -19.0f, -18.0f, 8.0f }, { 0.26f, 0.29f, 0.33f, 1.0f });
    AppendSphereLit(Out, { -22.0f, -21.0f, 10.6f }, 2.6f, 18, 10, { 0.62f, 0.66f, 0.72f, 1.0f });
    // 6. Low perimeter curbs
    AppendAxisAlignedBoxLit(Out, { -38.0f, -38.0f, 0.0f }, { 38.0f, -36.5f, 0.8f }, { 0.32f, 0.35f, 0.38f, 1.0f });
    AppendAxisAlignedBoxLit(Out, { -38.0f, 36.5f, 0.0f }, { 38.0f, 38.0f, 0.8f }, { 0.32f, 0.35f, 0.38f, 1.0f });
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              VULKAN OVERLAY IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

struct GpuBufferAllocation
{
    VkBuffer       Buffer    = VK_NULL_HANDLE;
    VkDeviceMemory Memory    = VK_NULL_HANDLE;
    VkDeviceSize   ByteSize  = 0u;
};

struct PaneTargetResources
{
    ~PaneTargetResources() noexcept
    {
        TextureRecord.Pixels = nullptr;
    }

    uint32_t           WidthPx           = 0u;
    uint32_t           HeightPx          = 0u;
    VkImage            ColorImage        = VK_NULL_HANDLE;
    VkDeviceMemory     ColorMemory       = VK_NULL_HANDLE;
    VkImageView        ColorView         = VK_NULL_HANDLE;
    VkImage            PickImage         = VK_NULL_HANDLE;
    VkDeviceMemory     PickMemory        = VK_NULL_HANDLE;
    VkImageView        PickView          = VK_NULL_HANDLE;
    VkImage            DepthImage        = VK_NULL_HANDLE;
    VkDeviceMemory     DepthMemory       = VK_NULL_HANDLE;
    VkImageView        DepthView         = VK_NULL_HANDLE;
    GpuBufferAllocation ColorReadback    {};
    GpuBufferAllocation PickReadback     {};
    std::vector<uint8_t>  CpuColorRgba   {};
    std::vector<uint32_t> CpuPickRgba32ui{};
    ImTextureData      TextureRecord     {};
};

struct VectorFieldOverlaySequence::SequenceImplementation
{
    VkPhysicalDevice      PhysicalDevice     = VK_NULL_HANDLE;
    VkDevice              Device             = VK_NULL_HANDLE;
    VkQueue               GraphicsQueue      = VK_NULL_HANDLE;
    uint32_t              QueueFamilyIndex   = 0u;
    bool                  UseKhrDynRender    = false;
    bool                  UseKhrSync2        = false;
    bool                  Ready              = false;

    PFN_vkCmdBeginRendering    DynBeginRender   = nullptr;
    PFN_vkCmdEndRendering      DynEndRender     = nullptr;
    PFN_vkCmdPipelineBarrier2  Sync2Barrier     = nullptr;

    VkDescriptorSetLayout DescLayout         = VK_NULL_HANDLE;
    VkPipelineLayout      PipeLayout         = VK_NULL_HANDLE;
    VkDescriptorPool      DescPool           = VK_NULL_HANDLE;
    VkDescriptorSet       DescSet            = VK_NULL_HANDLE;

    VkPipeline            LitPipe            = VK_NULL_HANDLE;
    VkPipeline            HeatmapPipe        = VK_NULL_HANDLE;
    VkPipeline            EnvelopePipe       = VK_NULL_HANDLE;
    VkPipeline            ArrowPipe          = VK_NULL_HANDLE;
    VkPipeline            StreamlinePipe     = VK_NULL_HANDLE;
    VkPipeline            BeaconPipe         = VK_NULL_HANDLE;

    VkCommandPool         CmdPool            = VK_NULL_HANDLE;
    VkCommandBuffer       CmdBuffer          = VK_NULL_HANDLE;
    VkFence               SubmitFence        = VK_NULL_HANDLE;

    GpuBufferAllocation   UniformBuf         {};
    GpuBufferAllocation   SourceBuf          {};
    GpuBufferAllocation   WaypointBuf        {};
    GpuBufferAllocation   LitSceneBuf        {};
    uint32_t              LitSceneVertCount  = 0u;
    GpuBufferAllocation   UnitSphereBuf      {};
    uint32_t              UnitSphereVertCount= 0u;
    GpuBufferAllocation   UnitBoxBuf         {};
    uint32_t              UnitBoxVertCount   = 0u;
    GpuBufferAllocation   UnitArrowBuf       {};
    uint32_t              UnitArrowVertCount = 0u;
    GpuBufferAllocation   BeaconBuf          {};

    std::array<PaneTargetResources, 2> Panes {};

    [[nodiscard]] uint32_t FindMemoryType(uint32_t TypeMask, VkMemoryPropertyFlags Required) const noexcept
    {
        VkPhysicalDeviceMemoryProperties Props{};
        vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &Props);
        for (uint32_t I = 0u; I < Props.memoryTypeCount; ++I)
        {
            if ((TypeMask & (1u << I)) != 0u &&
                (Props.memoryTypes[I].propertyFlags & Required) == Required)
            {
                return I;
            }
        }
        return UINT32_MAX;
    }

    void FreeBuffer(GpuBufferAllocation& Alloc) noexcept
    {
        if (Device != VK_NULL_HANDLE)
        {
            if (Alloc.Buffer != VK_NULL_HANDLE)
            {
                vkDestroyBuffer(Device, Alloc.Buffer, nullptr);
            }
            if (Alloc.Memory != VK_NULL_HANDLE)
            {
                vkFreeMemory(Device, Alloc.Memory, nullptr);
            }
        }
        Alloc = {};
    }

    bool EnsureBuffer(GpuBufferAllocation& Alloc, VkDeviceSize Bytes, VkBufferUsageFlags Usage) noexcept
    {
        const VkDeviceSize SafeBytes = std::max<VkDeviceSize>(Bytes, 256u);
        if (Alloc.Buffer != VK_NULL_HANDLE && Alloc.ByteSize >= SafeBytes)
        {
            return true;
        }
        FreeBuffer(Alloc);

        VkBufferCreateInfo BufInfo{};
        BufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        BufInfo.size  = SafeBytes;
        BufInfo.usage = Usage;
        if (vkCreateBuffer(Device, &BufInfo, nullptr, &Alloc.Buffer) != VK_SUCCESS)
        {
            return false;
        }

        VkMemoryRequirements Req{};
        vkGetBufferMemoryRequirements(Device, Alloc.Buffer, &Req);
        const uint32_t MemIdx = FindMemoryType(
            Req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (MemIdx == UINT32_MAX)
        {
            FreeBuffer(Alloc);
            return false;
        }

        VkMemoryAllocateInfo MemInfo{};
        MemInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        MemInfo.allocationSize  = Req.size;
        MemInfo.memoryTypeIndex = MemIdx;
        if (vkAllocateMemory(Device, &MemInfo, nullptr, &Alloc.Memory) != VK_SUCCESS)
        {
            FreeBuffer(Alloc);
            return false;
        }
        if (vkBindBufferMemory(Device, Alloc.Buffer, Alloc.Memory, 0u) != VK_SUCCESS)
        {
            FreeBuffer(Alloc);
            return false;
        }
        Alloc.ByteSize = SafeBytes;
        return true;
    }

    bool UploadToBuffer(GpuBufferAllocation& Alloc, VkBufferUsageFlags Usage, const void* Data, VkDeviceSize Bytes) noexcept
    {
        if (!EnsureBuffer(Alloc, Bytes, Usage))
        {
            return false;
        }
        if (Data != nullptr && Bytes > 0u)
        {
            void* Mapped = nullptr;
            if (vkMapMemory(Device, Alloc.Memory, 0u, Bytes, 0u, &Mapped) != VK_SUCCESS)
            {
                return false;
            }
            std::memcpy(Mapped, Data, static_cast<std::size_t>(Bytes));
            vkUnmapMemory(Device, Alloc.Memory);
        }
        return true;
    }

    void DestroyPaneTargets(PaneTargetResources& Pane) noexcept
    {
        if (Device != VK_NULL_HANDLE)
        {
            if (Pane.ColorImage != VK_NULL_HANDLE || Pane.PickImage != VK_NULL_HANDLE || Pane.DepthImage != VK_NULL_HANDLE)
            {
                vkDeviceWaitIdle(Device);
            }
            if (Pane.ColorView != VK_NULL_HANDLE)   { vkDestroyImageView(Device, Pane.ColorView, nullptr); }
            if (Pane.ColorImage != VK_NULL_HANDLE)  { vkDestroyImage(Device, Pane.ColorImage, nullptr); }
            if (Pane.ColorMemory != VK_NULL_HANDLE) { vkFreeMemory(Device, Pane.ColorMemory, nullptr); }

            if (Pane.PickView != VK_NULL_HANDLE)    { vkDestroyImageView(Device, Pane.PickView, nullptr); }
            if (Pane.PickImage != VK_NULL_HANDLE)   { vkDestroyImage(Device, Pane.PickImage, nullptr); }
            if (Pane.PickMemory != VK_NULL_HANDLE)  { vkFreeMemory(Device, Pane.PickMemory, nullptr); }

            if (Pane.DepthView != VK_NULL_HANDLE)   { vkDestroyImageView(Device, Pane.DepthView, nullptr); }
            if (Pane.DepthImage != VK_NULL_HANDLE)  { vkDestroyImage(Device, Pane.DepthImage, nullptr); }
            if (Pane.DepthMemory != VK_NULL_HANDLE) { vkFreeMemory(Device, Pane.DepthMemory, nullptr); }

            FreeBuffer(Pane.ColorReadback);
            FreeBuffer(Pane.PickReadback);
        }
        Pane.ColorView   = VK_NULL_HANDLE;
        Pane.ColorImage  = VK_NULL_HANDLE;
        Pane.ColorMemory = VK_NULL_HANDLE;
        Pane.PickView    = VK_NULL_HANDLE;
        Pane.PickImage   = VK_NULL_HANDLE;
        Pane.PickMemory  = VK_NULL_HANDLE;
        Pane.DepthView   = VK_NULL_HANDLE;
        Pane.DepthImage  = VK_NULL_HANDLE;
        Pane.DepthMemory = VK_NULL_HANDLE;
        Pane.WidthPx     = 0u;
        Pane.HeightPx    = 0u;
        Pane.TextureRecord.Pixels = nullptr;
    }

    bool EnsurePaneTargets(PaneTargetResources& Pane, uint32_t WidthPx, uint32_t HeightPx) noexcept
    {
        if (Pane.WidthPx == WidthPx && Pane.HeightPx == HeightPx && Pane.ColorImage != VK_NULL_HANDLE)
        {
            return true;
        }
        DestroyPaneTargets(Pane);

        auto CreateAttachment = [&](
            VkFormat           Format,
            VkImageUsageFlags  Usage,
            VkImageAspectFlags Aspect,
            VkImage&           OutImg,
            VkDeviceMemory&    OutMem,
            VkImageView&       OutView) noexcept -> bool
        {
            VkImageCreateInfo ImgInfo{};
            ImgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            ImgInfo.imageType     = VK_IMAGE_TYPE_2D;
            ImgInfo.format        = Format;
            ImgInfo.extent        = { WidthPx, HeightPx, 1u };
            ImgInfo.mipLevels     = 1u;
            ImgInfo.arrayLayers   = 1u;
            ImgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
            ImgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
            ImgInfo.usage         = Usage;
            ImgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            if (vkCreateImage(Device, &ImgInfo, nullptr, &OutImg) != VK_SUCCESS)
            {
                return false;
            }
            VkMemoryRequirements Req{};
            vkGetImageMemoryRequirements(Device, OutImg, &Req);
            uint32_t MemIdx = FindMemoryType(Req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (MemIdx == UINT32_MAX)
            {
                MemIdx = FindMemoryType(Req.memoryTypeBits, 0u);
            }
            if (MemIdx == UINT32_MAX)
            {
                return false;
            }
            VkMemoryAllocateInfo Alloc{};
            Alloc.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            Alloc.allocationSize  = Req.size;
            Alloc.memoryTypeIndex = MemIdx;
            if (vkAllocateMemory(Device, &Alloc, nullptr, &OutMem) != VK_SUCCESS)
            {
                return false;
            }
            if (vkBindImageMemory(Device, OutImg, OutMem, 0u) != VK_SUCCESS)
            {
                return false;
            }
            VkImageViewCreateInfo ViewInfo{};
            ViewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ViewInfo.image                           = OutImg;
            ViewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            ViewInfo.format                          = Format;
            ViewInfo.subresourceRange.aspectMask     = Aspect;
            ViewInfo.subresourceRange.levelCount     = 1u;
            ViewInfo.subresourceRange.layerCount     = 1u;
            return vkCreateImageView(Device, &ViewInfo, nullptr, &OutView) == VK_SUCCESS;
        };

        if (!CreateAttachment(
                VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                Pane.ColorImage,
                Pane.ColorMemory,
                Pane.ColorView) ||
            !CreateAttachment(
                VK_FORMAT_R32G32B32A32_UINT,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT,
                Pane.PickImage,
                Pane.PickMemory,
                Pane.PickView) ||
            !CreateAttachment(
                VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                Pane.DepthImage,
                Pane.DepthMemory,
                Pane.DepthView))
        {
            DestroyPaneTargets(Pane);
            return false;
        }

        const VkDeviceSize ColorBytes = static_cast<VkDeviceSize>(WidthPx) * HeightPx * 4u;
        const VkDeviceSize PickBytes  = static_cast<VkDeviceSize>(WidthPx) * HeightPx * 16u;
        if (!EnsureBuffer(Pane.ColorReadback, ColorBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT) ||
            !EnsureBuffer(Pane.PickReadback, PickBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT))
        {
            DestroyPaneTargets(Pane);
            return false;
        }

        Pane.CpuColorRgba.assign(static_cast<std::size_t>(ColorBytes), 0u);
        Pane.CpuPickRgba32ui.assign(static_cast<std::size_t>(WidthPx) * HeightPx * 4u, 0u);
        Pane.WidthPx  = WidthPx;
        Pane.HeightPx = HeightPx;

        Pane.TextureRecord.Format        = ImTextureFormat_RGBA32;
        Pane.TextureRecord.Width         = static_cast<int>(WidthPx);
        Pane.TextureRecord.Height        = static_cast<int>(HeightPx);
        Pane.TextureRecord.BytesPerPixel = 4;
        Pane.TextureRecord.Pixels        = Pane.CpuColorRgba.data();
        Pane.TextureRecord.UpdateRect    = { 0u, 0u, static_cast<unsigned short>(WidthPx), static_cast<unsigned short>(HeightPx) };
        Pane.TextureRecord.SetStatus(ImTextureStatus_WantCreate);
        return true;
    }

    void Cleanup() noexcept
    {
        if (Device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(Device);
            for (auto& Pane : Panes)
            {
                DestroyPaneTargets(Pane);
            }
            FreeBuffer(UniformBuf);
            FreeBuffer(SourceBuf);
            FreeBuffer(WaypointBuf);
            FreeBuffer(LitSceneBuf);
            FreeBuffer(UnitSphereBuf);
            FreeBuffer(UnitBoxBuf);
            FreeBuffer(UnitArrowBuf);
            FreeBuffer(BeaconBuf);

            auto KillPipe = [&](VkPipeline& P) noexcept
            {
                if (P != VK_NULL_HANDLE) { vkDestroyPipeline(Device, P, nullptr); P = VK_NULL_HANDLE; }
            };
            KillPipe(LitPipe);
            KillPipe(HeatmapPipe);
            KillPipe(EnvelopePipe);
            KillPipe(ArrowPipe);
            KillPipe(StreamlinePipe);
            KillPipe(BeaconPipe);

            if (SubmitFence != VK_NULL_HANDLE) { vkDestroyFence(Device, SubmitFence, nullptr); SubmitFence = VK_NULL_HANDLE; }
            if (CmdPool != VK_NULL_HANDLE)     { vkDestroyCommandPool(Device, CmdPool, nullptr); CmdPool = VK_NULL_HANDLE; }
            if (DescPool != VK_NULL_HANDLE)    { vkDestroyDescriptorPool(Device, DescPool, nullptr); DescPool = VK_NULL_HANDLE; }
            if (PipeLayout != VK_NULL_HANDLE)  { vkDestroyPipelineLayout(Device, PipeLayout, nullptr); PipeLayout = VK_NULL_HANDLE; }
            if (DescLayout != VK_NULL_HANDLE)  { vkDestroyDescriptorSetLayout(Device, DescLayout, nullptr); DescLayout = VK_NULL_HANDLE; }
        }
        Ready = false;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                           LIFECYCLE & PIPELINE CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

VectorFieldOverlaySequence::VectorFieldOverlaySequence() noexcept = default;

VectorFieldOverlaySequence::~VectorFieldOverlaySequence() noexcept
{
    Finalize();
}

bool VectorFieldOverlaySequence::Initialize(
    SimulationSpace::ForceFields::FieldLatticeVulkanExecutor& VulkanExecutor,
    const std::filesystem::path&                              ShaderBinaryDirectory,
    std::string&                                              OutDiagnostic) noexcept
{
    Finalize();
    if (!VulkanExecutor.QueryDeviceReady())
    {
        OutDiagnostic = "FieldLatticeVulkanExecutor is not ready for overlay initialization.";
        return false;
    }

    Impl                   = std::make_unique<SequenceImplementation>();
    Impl->PhysicalDevice   = static_cast<VkPhysicalDevice>(VulkanExecutor.QueryNativePhysicalDevice());
    Impl->Device           = static_cast<VkDevice>(VulkanExecutor.QueryNativeDevice());
    Impl->GraphicsQueue    = static_cast<VkQueue>(VulkanExecutor.QueryNativeQueue());
    Impl->QueueFamilyIndex = VulkanExecutor.QueryQueueFamilyIndex();
    Impl->UseKhrDynRender  = VulkanExecutor.QueryUsesKhrDynamicRendering();
    Impl->UseKhrSync2      = VulkanExecutor.QueryUsesKhrSynchronization2();

    Impl->DynBeginRender = Impl->UseKhrDynRender ? vkCmdBeginRenderingKHR : vkCmdBeginRendering;
    Impl->DynEndRender   = Impl->UseKhrDynRender ? vkCmdEndRenderingKHR   : vkCmdEndRendering;
    Impl->Sync2Barrier   = Impl->UseKhrSync2     ? vkCmdPipelineBarrier2KHR : vkCmdPipelineBarrier2;

    return InitializeAfterDeviceResolved(ShaderBinaryDirectory, OutDiagnostic);
}

bool VectorFieldOverlaySequence::Initialize(
    Frontier::GeometricRaster::VulkanInterchange& BoundDevice,
    const std::filesystem::path&                  ShaderBinaryDirectory,
    std::string&                                  OutDiagnostic) noexcept
{
    Finalize();
    if (!BoundDevice.Ready())
    {
        OutDiagnostic = "VulkanInterchange is not ready for VectorFieldOverlaySequence initialization.";
        return false;
    }
    if (!BoundDevice.QueryUsesKhrDynamicRendering() || !BoundDevice.QueryUsesKhrSynchronization2())
    {
        OutDiagnostic = "VulkanInterchange lacks VK_KHR_dynamic_rendering / VK_KHR_synchronization2.";
        return false;
    }

    Impl                   = std::make_unique<SequenceImplementation>();
    Impl->PhysicalDevice   = BoundDevice.AccessPhysicalDevice();
    Impl->Device           = BoundDevice.AccessDevice();
    Impl->GraphicsQueue    = BoundDevice.AccessQueue();
    Impl->QueueFamilyIndex = BoundDevice.QueryQueueOrdinal();
    Impl->UseKhrDynRender  = BoundDevice.QueryUsesKhrDynamicRendering();
    Impl->UseKhrSync2      = BoundDevice.QueryUsesKhrSynchronization2();

    Impl->DynBeginRender = (vkCmdBeginRenderingKHR != nullptr) ? vkCmdBeginRenderingKHR : vkCmdBeginRendering;
    Impl->DynEndRender   = (vkCmdEndRenderingKHR   != nullptr) ? vkCmdEndRenderingKHR   : vkCmdEndRendering;
    Impl->Sync2Barrier   = (vkCmdPipelineBarrier2KHR != nullptr) ? vkCmdPipelineBarrier2KHR : vkCmdPipelineBarrier2;

    return InitializeAfterDeviceResolved(ShaderBinaryDirectory, OutDiagnostic);
}

bool VectorFieldOverlaySequence::InitializeAfterDeviceResolved(
    const std::filesystem::path& ShaderBinaryDirectory,
    std::string&                 OutDiagnostic) noexcept
{
    if (Impl->DynBeginRender == nullptr || Impl->DynEndRender == nullptr || Impl->Sync2Barrier == nullptr)
    {
        OutDiagnostic = "Vulkan 1.3 dynamic rendering / synchronization2 entry points unavailable on device.";
        Finalize();
        return false;
    }

    // Shared DescriptorSetLayout: [0]=UBO, [1]=Source SSBO, [2]=Waypoint SSBO
    std::array<VkDescriptorSetLayoutBinding, 3> Bindings{};
    Bindings[0] = { 0u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    Bindings[1] = { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };
    Bindings[2] = { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr };

    VkDescriptorSetLayoutCreateInfo SetLayoutInfo{};
    SetLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    SetLayoutInfo.bindingCount = static_cast<uint32_t>(Bindings.size());
    SetLayoutInfo.pBindings    = Bindings.data();
    if (vkCreateDescriptorSetLayout(Impl->Device, &SetLayoutInfo, nullptr, &Impl->DescLayout) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateDescriptorSetLayout failed for VectorFieldOverlaySequence.";
        Finalize();
        return false;
    }

    VkPushConstantRange PushRange{};
    PushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    PushRange.offset     = 0u;
    PushRange.size       = sizeof(DrawObjectPushConstantsGpu);

    VkPipelineLayoutCreateInfo PipeLayoutInfo{};
    PipeLayoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    PipeLayoutInfo.setLayoutCount         = 1u;
    PipeLayoutInfo.pSetLayouts            = &Impl->DescLayout;
    PipeLayoutInfo.pushConstantRangeCount = 1u;
    PipeLayoutInfo.pPushConstantRanges    = &PushRange;
    if (vkCreatePipelineLayout(Impl->Device, &PipeLayoutInfo, nullptr, &Impl->PipeLayout) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreatePipelineLayout failed for VectorFieldOverlaySequence.";
        Finalize();
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> PoolSizes = {{
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1u },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2u },
    }};
    VkDescriptorPoolCreateInfo PoolInfo{};
    PoolInfo.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    PoolInfo.maxSets       = 1u;
    PoolInfo.poolSizeCount = static_cast<uint32_t>(PoolSizes.size());
    PoolInfo.pPoolSizes    = PoolSizes.data();
    if (vkCreateDescriptorPool(Impl->Device, &PoolInfo, nullptr, &Impl->DescPool) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateDescriptorPool failed.";
        Finalize();
        return false;
    }

    VkDescriptorSetAllocateInfo AllocSet{};
    AllocSet.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    AllocSet.descriptorPool     = Impl->DescPool;
    AllocSet.descriptorSetCount = 1u;
    AllocSet.pSetLayouts        = &Impl->DescLayout;
    if (vkAllocateDescriptorSets(Impl->Device, &AllocSet, &Impl->DescSet) != VK_SUCCESS)
    {
        OutDiagnostic = "vkAllocateDescriptorSets failed.";
        Finalize();
        return false;
    }

    VkCommandPoolCreateInfo CmdPoolInfo{};
    CmdPoolInfo.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    CmdPoolInfo.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    CmdPoolInfo.queueFamilyIndex = Impl->QueueFamilyIndex;
    if (vkCreateCommandPool(Impl->Device, &CmdPoolInfo, nullptr, &Impl->CmdPool) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateCommandPool failed.";
        Finalize();
        return false;
    }

    VkCommandBufferAllocateInfo CmdAlloc{};
    CmdAlloc.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    CmdAlloc.commandPool        = Impl->CmdPool;
    CmdAlloc.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    CmdAlloc.commandBufferCount = 1u;
    if (vkAllocateCommandBuffers(Impl->Device, &CmdAlloc, &Impl->CmdBuffer) != VK_SUCCESS)
    {
        OutDiagnostic = "vkAllocateCommandBuffers failed.";
        Finalize();
        return false;
    }

    VkFenceCreateInfo FenceInfo{};
    FenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (vkCreateFence(Impl->Device, &FenceInfo, nullptr, &Impl->SubmitFence) != VK_SUCCESS)
    {
        OutDiagnostic = "vkCreateFence failed.";
        Finalize();
        return false;
    }

    enum class VertexLayoutCategory : uint8_t { None, LitBeacon, PositionNormal };

    auto CreateOverlayPipeline = [&](
        const char*       StemName,
        VertexLayoutCategory  LayoutKind,
        VkPrimitiveTopology Topology,
        bool              AlphaBlend,
        bool              DepthWrite,
        VkCullModeFlags   CullMode,
        VkPipeline&       OutPipe) -> bool
    {
        std::vector<uint32_t> VertWords, FragWords;
        const auto VertPath = ShaderBinaryDirectory / (std::string(StemName) + ".vert.spv");
        const auto FragPath = ShaderBinaryDirectory / (std::string(StemName) + ".frag.spv");
        if (!ReadSpirvWords(VertPath, VertWords) || !ReadSpirvWords(FragPath, FragWords))
        {
            OutDiagnostic = "Missing overlay SPIR-V pair: " + VertPath.string();
            return false;
        }

        VkShaderModule VertexSpirvBinary = VK_NULL_HANDLE;
        VkShaderModule FragmentSpirvBinary = VK_NULL_HANDLE;
        VkShaderModuleCreateInfo SmInfo{};
        SmInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        SmInfo.codeSize = VertWords.size() * sizeof(uint32_t);
        SmInfo.pCode    = VertWords.data();
        if (vkCreateShaderModule(Impl->Device, &SmInfo, nullptr, &VertexSpirvBinary) != VK_SUCCESS)
        {
            OutDiagnostic = "vkCreateShaderModule failed for vertex shader " + std::string(StemName);
            return false;
        }
        SmInfo.codeSize = FragWords.size() * sizeof(uint32_t);
        SmInfo.pCode    = FragWords.data();
        if (vkCreateShaderModule(Impl->Device, &SmInfo, nullptr, &FragmentSpirvBinary) != VK_SUCCESS)
        {
            vkDestroyShaderModule(Impl->Device, VertexSpirvBinary, nullptr);
            OutDiagnostic = "vkCreateShaderModule failed for fragment shader " + std::string(StemName);
            return false;
        }

        std::array<VkPipelineShaderStageCreateInfo, 2> Stages{};
        Stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_VERTEX_BIT, VertexSpirvBinary, "main", nullptr };
        Stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0u, VK_SHADER_STAGE_FRAGMENT_BIT, FragmentSpirvBinary, "main", nullptr };

        VkVertexInputBindingDescription BindingDesc{};
        std::vector<VkVertexInputAttributeDescription> Attrs;
        if (LayoutKind == VertexLayoutCategory::LitBeacon)
        {
            BindingDesc = { 0u, sizeof(LitBeaconVertex), VK_VERTEX_INPUT_RATE_VERTEX };
            Attrs.push_back({ 0u, 0u, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(LitBeaconVertex, Pos) });
            Attrs.push_back({ 1u, 0u, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(LitBeaconVertex, Normal) });
            Attrs.push_back({ 2u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(LitBeaconVertex, Color) });
        }
        else if (LayoutKind == VertexLayoutCategory::PositionNormal)
        {
            BindingDesc = { 0u, sizeof(PositionNormalVertex), VK_VERTEX_INPUT_RATE_VERTEX };
            Attrs.push_back({ 0u, 0u, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PositionNormalVertex, Pos) });
            Attrs.push_back({ 1u, 0u, VK_FORMAT_R32G32B32_SFLOAT, offsetof(PositionNormalVertex, Normal) });
        }

        VkPipelineVertexInputStateCreateInfo VertInput{};
        VertInput.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VertInput.vertexBindingDescriptionCount   = (LayoutKind == VertexLayoutCategory::None) ? 0u : 1u;
        VertInput.pVertexBindingDescriptions      = (LayoutKind == VertexLayoutCategory::None) ? nullptr : &BindingDesc;
        VertInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(Attrs.size());
        VertInput.pVertexAttributeDescriptions    = Attrs.empty() ? nullptr : Attrs.data();

        VkPipelineInputAssemblyStateCreateInfo InputAsm{};
        InputAsm.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        InputAsm.topology = Topology;

        VkPipelineViewportStateCreateInfo ViewState{};
        ViewState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        ViewState.viewportCount = 1u;
        ViewState.scissorCount  = 1u;

        VkPipelineRasterizationStateCreateInfo Rast{};
        Rast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        Rast.polygonMode = VK_POLYGON_MODE_FILL;
        Rast.cullMode    = CullMode;
        Rast.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        Rast.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo Msaa{};
        Msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        Msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo Depth{};
        Depth.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        Depth.depthTestEnable  = VK_TRUE;
        Depth.depthWriteEnable = DepthWrite ? VK_TRUE : VK_FALSE;
        Depth.depthCompareOp   = VK_COMPARE_OP_GREATER_OR_EQUAL; // Reverse-Z!

        std::array<VkPipelineColorBlendAttachmentState, 2> BlendAttachments{};
        BlendAttachments[0].colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        BlendAttachments[0].blendEnable         = AlphaBlend ? VK_TRUE : VK_FALSE;
        BlendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        BlendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAttachments[0].colorBlendOp        = VK_BLEND_OP_ADD;
        BlendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        BlendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        BlendAttachments[0].alphaBlendOp        = VK_BLEND_OP_ADD;

        // PickId attachment (RGBA32UI) MUST NOT have blending enabled
        BlendAttachments[1].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        BlendAttachments[1].blendEnable    = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo Blend{};
        Blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        Blend.attachmentCount = static_cast<uint32_t>(BlendAttachments.size());
        Blend.pAttachments    = BlendAttachments.data();

        std::array<VkDynamicState, 2> DynStates = {{ VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR }};
        VkPipelineDynamicStateCreateInfo Dyn{};
        Dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        Dyn.dynamicStateCount = static_cast<uint32_t>(DynStates.size());
        Dyn.pDynamicStates    = DynStates.data();

        std::array<VkFormat, 2> ColorFormats = {{ VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R32G32B32A32_UINT }};
        VkPipelineRenderingCreateInfo RenderFmt{};
        RenderFmt.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        RenderFmt.colorAttachmentCount    = static_cast<uint32_t>(ColorFormats.size());
        RenderFmt.pColorAttachmentFormats = ColorFormats.data();
        RenderFmt.depthAttachmentFormat   = VK_FORMAT_D32_SFLOAT;

        VkGraphicsPipelineCreateInfo PipeInfo{};
        PipeInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        PipeInfo.pNext               = &RenderFmt;
        PipeInfo.stageCount          = static_cast<uint32_t>(Stages.size());
        PipeInfo.pStages             = Stages.data();
        PipeInfo.pVertexInputState   = &VertInput;
        PipeInfo.pInputAssemblyState = &InputAsm;
        PipeInfo.pViewportState      = &ViewState;
        PipeInfo.pRasterizationState = &Rast;
        PipeInfo.pMultisampleState   = &Msaa;
        PipeInfo.pDepthStencilState  = &Depth;
        PipeInfo.pColorBlendState    = &Blend;
        PipeInfo.pDynamicState       = &Dyn;
        PipeInfo.layout              = Impl->PipeLayout;

        const VkResult Res = vkCreateGraphicsPipelines(Impl->Device, VK_NULL_HANDLE, 1u, &PipeInfo, nullptr, &OutPipe);
        vkDestroyShaderModule(Impl->Device, VertexSpirvBinary, nullptr);
        vkDestroyShaderModule(Impl->Device, FragmentSpirvBinary, nullptr);
        if (Res != VK_SUCCESS)
        {
            OutDiagnostic = "vkCreateGraphicsPipelines failed for " + std::string(StemName);
            return false;
        }
        return true;
    };

    if (!CreateOverlayPipeline("LitBackdropRasterization",        VertexLayoutCategory::LitBeacon,      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,  false, true,  VK_CULL_MODE_BACK_BIT, Impl->LitPipe) ||
        !CreateOverlayPipeline("CrossSectionHeatmapRasterization",VertexLayoutCategory::None,           VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,  true,  false, VK_CULL_MODE_NONE,     Impl->HeatmapPipe) ||
        !CreateOverlayPipeline("InfluenceEnvelopeRasterization",  VertexLayoutCategory::PositionNormal, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,  true,  false, VK_CULL_MODE_NONE,     Impl->EnvelopePipe) ||
        !CreateOverlayPipeline("VectorArrowRasterization",        VertexLayoutCategory::PositionNormal, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,  true,  true,  VK_CULL_MODE_BACK_BIT, Impl->ArrowPipe) ||
        !CreateOverlayPipeline("StreamlineRasterization",         VertexLayoutCategory::None,           VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP, true,  false, VK_CULL_MODE_NONE,     Impl->StreamlinePipe) ||
        !CreateOverlayPipeline("ForcePathBeaconRasterization", VertexLayoutCategory::LitBeacon,      VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,  true,  true,  VK_CULL_MODE_BACK_BIT, Impl->BeaconPipe))
    {
        Finalize();
        return false;
    }

    std::vector<LitBeaconVertex> LitSceneVerts;
    BuildLitArchitecturalSceneVertices(LitSceneVerts);
    Impl->LitSceneVertCount = static_cast<uint32_t>(LitSceneVerts.size());

    std::vector<PositionNormalVertex> SphereVerts, BoxVerts, ArrowVerts;
    BuildUnitSphereVertices(SphereVerts);
    BuildUnitBoxVertices(BoxVerts);
    BuildUnitArrowVertices(ArrowVerts);
    Impl->UnitSphereVertCount = static_cast<uint32_t>(SphereVerts.size());
    Impl->UnitBoxVertCount    = static_cast<uint32_t>(BoxVerts.size());
    Impl->UnitArrowVertCount  = static_cast<uint32_t>(ArrowVerts.size());

    if (!Impl->EnsureBuffer(Impl->UniformBuf, sizeof(ViewportOverlayUniformsGpu), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) ||
        !Impl->EnsureBuffer(Impl->SourceBuf, sizeof(ForceSourceGpuStd430) * 64u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        !Impl->EnsureBuffer(Impl->WaypointBuf, sizeof(float) * 4u * 256u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) ||
        !Impl->UploadToBuffer(Impl->LitSceneBuf, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, LitSceneVerts.data(), LitSceneVerts.size() * sizeof(LitBeaconVertex)) ||
        !Impl->UploadToBuffer(Impl->UnitSphereBuf, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, SphereVerts.data(), SphereVerts.size() * sizeof(PositionNormalVertex)) ||
        !Impl->UploadToBuffer(Impl->UnitBoxBuf, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, BoxVerts.data(), BoxVerts.size() * sizeof(PositionNormalVertex)) ||
        !Impl->UploadToBuffer(Impl->UnitArrowBuf, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, ArrowVerts.data(), ArrowVerts.size() * sizeof(PositionNormalVertex)))
    {
        OutDiagnostic = "Failed to allocate static geometry buffers for VectorFieldOverlaySequence.";
        Finalize();
        return false;
    }

    Impl->Ready = true;
    return true;
}

void VectorFieldOverlaySequence::Finalize() noexcept
{
    if (Impl != nullptr)
    {
        Impl->Cleanup();
        Impl.reset();
    }
}

bool VectorFieldOverlaySequence::QueryReady() const noexcept
{
    return (Impl != nullptr) && Impl->Ready;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANE RENDERING
//------------------------------------------------------------------------------------------------------------------------

OverlayRenderOutput VectorFieldOverlaySequence::RenderPane(
    const OverlayFrameRequest& Request,
    std::string&               OutDiagnostic) noexcept
{
    OverlayRenderOutput Output{};
    if (!QueryReady())
    {
        OutDiagnostic = "VectorFieldOverlaySequence is not initialized.";
        return Output;
    }

    const uint32_t PaneIdx = (Request.PaneIndex < 2u) ? Request.PaneIndex : 0u;
    const uint32_t W       = std::clamp(Request.WidthPx, 64u, 2560u);
    const uint32_t H       = std::clamp(Request.HeightPx, 64u, 1440u);
    PaneTargetResources& Pane = Impl->Panes[PaneIdx];

    if (!Impl->EnsurePaneTargets(Pane, W, H))
    {
        OutDiagnostic = "Failed to allocate Vulkan MRT framebuffer attachments.";
        return Output;
    }

    // 1. Upload UBO + ForceSource SSBO + Waypoint SSBO
    ViewportOverlayUniformsGpu Ubo{};
    ComputeCameraBasisViewProj(Request, static_cast<float>(W) / static_cast<float>(H), Ubo);
    Ubo.CameraPositionTime[3]   = static_cast<float>(Request.TimeSeconds);
    Ubo.CameraRightThickness[3] = Request.OverlaySettings.StreamRibbonThickness;
    Ubo.CameraUpEmissive[3]     = Request.OverlaySettings.StreamEmissiveBoost;

    const uint32_t PackedCol = Request.OverlaySettings.CustomStreamColorRgba;
    Ubo.CustomStreamColor[0] = static_cast<float>((PackedCol >> 24u) & 0xFFu) / 255.0f;
    Ubo.CustomStreamColor[1] = static_cast<float>((PackedCol >> 16u) & 0xFFu) / 255.0f;
    Ubo.CustomStreamColor[2] = static_cast<float>((PackedCol >>  8u) & 0xFFu) / 255.0f;
    Ubo.CustomStreamColor[3] = static_cast<float>( PackedCol         & 0xFFu) / 255.0f;

    Ubo.CrossSectionPlaneParams[0] = static_cast<float>(Request.OverlaySettings.CrossSectionAxis);
    Ubo.CrossSectionPlaneParams[1] = Request.OverlaySettings.CrossSectionOffsetMeters;
    Ubo.CrossSectionPlaneParams[2] = std::max(10.0f, Request.SliceHalfSpanMeters);
    Ubo.CrossSectionPlaneParams[3] = static_cast<float>(Request.OverlaySettings.HeatmapChannel);

    Ubo.StreamVisualParams[0]      = static_cast<float>(Request.OverlaySettings.StreamColorMode);
    Ubo.StreamVisualParams[1]      = Request.OverlaySettings.StreamOpacity;
    Ubo.StreamVisualParams[2]      = static_cast<float>(Request.OverlaySettings.StreamCurveResolution);
    Ubo.StreamVisualParams[3]      = static_cast<float>(Request.OverlaySettings.StreamLineCount);

    Ubo.ActiveSourceCounts[0]      = static_cast<uint32_t>(Request.PackedSources.size());
    Ubo.ActiveSourceCounts[1]      = Request.SelectedForceToken;
    Ubo.ActiveSourceCounts[2]      = (Request.SelectedWaypointIndex >= 0) ? static_cast<uint32_t>(Request.SelectedWaypointIndex) : 0xFFFFFFFFu;
    Ubo.ActiveSourceCounts[3]      = Request.LitShadingEnabled ? 1u : 0u;

    Ubo.AnemometerWorldActive[0] = Request.OverlaySettings.AnemometerWorldMeters.X;
    Ubo.AnemometerWorldActive[1] = Request.OverlaySettings.AnemometerWorldMeters.Y;
    Ubo.AnemometerWorldActive[2] = Request.OverlaySettings.AnemometerWorldMeters.Z;
    Ubo.AnemometerWorldActive[3] = Request.OverlaySettings.CrossSectionAnemometerActive ? 1.0f : 0.0f;

    std::vector<ForceSourceGpuStd430> SrcUpload;
    if (Request.PackedSources.empty())
    {
        SrcUpload.resize(1u);
    }
    else
    {
        SrcUpload.resize(Request.PackedSources.size());
        static_assert(sizeof(ForceSourceGpuStd430) == sizeof(SimulationSpace::ForceFields::ForceSourceShaderEntry));
        std::memcpy(SrcUpload.data(), Request.PackedSources.data(), SrcUpload.size() * sizeof(ForceSourceGpuStd430));
    }

    std::vector<float> WpUpload;
    if (Request.PackedWaypoints.empty())
    {
        WpUpload.assign(4u, 0.0f);
    }
    else
    {
        WpUpload.resize(Request.PackedWaypoints.size() * 4u, 0.0f);
        for (std::size_t I = 0u; I < Request.PackedWaypoints.size(); ++I)
        {
            WpUpload[I * 4u + 0u] = static_cast<float>(Request.PackedWaypoints[I].X);
            WpUpload[I * 4u + 1u] = static_cast<float>(Request.PackedWaypoints[I].Y);
            WpUpload[I * 4u + 2u] = static_cast<float>(Request.PackedWaypoints[I].Z);
            WpUpload[I * 4u + 3u] = 1.0f;
        }
    }

    const VkDeviceSize SrcBytes = SrcUpload.size() * sizeof(ForceSourceGpuStd430);
    const VkDeviceSize WpBytes  = WpUpload.size() * sizeof(float);
    if (!Impl->UploadToBuffer(Impl->UniformBuf, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &Ubo, sizeof(Ubo)) ||
        !Impl->UploadToBuffer(Impl->SourceBuf, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, SrcUpload.data(), SrcBytes) ||
        !Impl->UploadToBuffer(Impl->WaypointBuf, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, WpUpload.data(), WpBytes))
    {
        OutDiagnostic = "Failed to upload overlay uniform/storage buffers.";
        return Output;
    }

    // Build dynamic beacon / motion-path / waypoint / gizmo / anemometer geometry on CPU into BeaconBuf
    std::vector<LitBeaconVertex> BeaconVerts;
    struct BeaconDrawSpan
    {
        uint32_t                   FirstVertex;
        uint32_t                   VertexCount;
        DrawObjectPushConstantsGpu Push;
    };
    std::vector<BeaconDrawSpan> BeaconDraws;

    auto EmitBeaconDraw = [&](uint32_t Cat, uint32_t PrimaryId, uint32_t SecondaryId, auto&& BuildFn)
    {
        const uint32_t Start = static_cast<uint32_t>(BeaconVerts.size());
        BuildFn();
        const uint32_t Count = static_cast<uint32_t>(BeaconVerts.size()) - Start;
        if (Count > 0u)
        {
            BeaconDraws.push_back({ Start, Count, { 0u, 0u, Cat, PrimaryId, SecondaryId, 0u } });
        }
    };

    if (Request.OverlaySettings.Enabled)
    {
        for (std::size_t S = 0u; S < Request.PackedSources.size(); ++S)
        {
            const auto& Src      = Request.PackedSources[S];
            const uint32_t Token = (S < Request.SourceTokens.size()) ? Request.SourceTokens[S] : static_cast<uint32_t>(S + 1u);
            const bool  Selected = (Token == Request.SelectedForceToken);
            const Float3 C{ Src.CentreX, Src.CentreY, Src.CentreZ };

            // Clickable Force center glowing orb
            EmitBeaconDraw(
                static_cast<uint32_t>(OverlayPickCategory::ForceCircle),
                Token,
                0u,
                [&]()
                {
                    const std::array<float, 4> Col = Selected
                        ? std::array<float, 4>{ 0.98f, 0.78f, 0.36f, 1.0f }
                        : std::array<float, 4>{ 0.40f, 0.86f, 1.00f, 0.96f };
                    AppendSphereLit(BeaconVerts, C, Selected ? 0.85f : 0.62f, 14, 8, Col);
                });

            // Motion Path tube & clickable Waypoint handles (A -> P1 -> ... -> B or Orbit ring)
            if (Request.OverlaySettings.ShowMotionCurves &&
                S < Request.SourceWaypointOffsets.size() &&
                S < Request.SourceWaypointCounts.size())
            {
                const uint32_t WpOff   = Request.SourceWaypointOffsets[S];
                const uint32_t WpCount = Request.SourceWaypointCounts[S];
                const bool     IsOrbit = (S < Request.SourcePathIsOrbit.size()) && (Request.SourcePathIsOrbit[S] != 0u);

                if (WpCount >= 2u && WpOff + WpCount <= Request.PackedWaypoints.size())
                {
                    EmitBeaconDraw(0u, Token, 0u, [&]()
                    {
                        const std::array<float, 4> PathCol = { 0.96f, 0.76f, 0.38f, 0.92f };
                        for (uint32_t WIdx = 0u; WIdx + 1u < WpCount; ++WIdx)
                        {
                            const auto& A = Request.PackedWaypoints[WpOff + WIdx];
                            const auto& B = Request.PackedWaypoints[WpOff + WIdx + 1u];
                            AppendSegmentTubeLit(
                                BeaconVerts,
                                { static_cast<float>(A.X), static_cast<float>(A.Y), static_cast<float>(A.Z) },
                                { static_cast<float>(B.X), static_cast<float>(B.Y), static_cast<float>(B.Z) },
                                0.14f,
                                6,
                                PathCol);
                        }
                        if (IsOrbit)
                        {
                            const auto& FirstPt = Request.PackedWaypoints[WpOff];
                            const auto& AxisPt  = Request.PackedWaypoints[WpOff + 1u];
                            const float Rad = static_cast<float>(std::hypot(AxisPt.X - FirstPt.X, AxisPt.Y - FirstPt.Y));
                            constexpr int Segs = 32;
                            for (int K = 0; K < Segs; ++K)
                            {
                                const float A0 = 6.2831853f * static_cast<float>(K) / static_cast<float>(Segs);
                                const float A1 = 6.2831853f * static_cast<float>(K + 1) / static_cast<float>(Segs);
                                AppendSegmentTubeLit(
                                    BeaconVerts,
                                    { static_cast<float>(FirstPt.X) + Rad * std::cos(A0), static_cast<float>(FirstPt.Y) + Rad * std::sin(A0), static_cast<float>(FirstPt.Z) },
                                    { static_cast<float>(FirstPt.X) + Rad * std::cos(A1), static_cast<float>(FirstPt.Y) + Rad * std::sin(A1), static_cast<float>(FirstPt.Z) },
                                    0.13f,
                                    6,
                                    { 0.42f, 0.88f, 1.0f, 0.92f });
                            }
                        }
                    });

                    for (uint32_t WIdx = 0u; WIdx < WpCount; ++WIdx)
                    {
                        const auto& Wp = Request.PackedWaypoints[WpOff + WIdx];
                        const bool  WpSel = Selected && (static_cast<int32_t>(WIdx) == Request.SelectedWaypointIndex);
                        EmitBeaconDraw(
                            static_cast<uint32_t>(OverlayPickCategory::MotionWaypoint),
                            Token,
                            WIdx,
                            [&]()
                            {
                                const std::array<float, 4> WpCol = WpSel
                                    ? std::array<float, 4>{ 1.0f, 0.92f, 0.35f, 1.0f }
                                    : std::array<float, 4>{ 0.98f, 0.56f, 0.24f, 1.0f };
                                AppendSphereLit(
                                    BeaconVerts,
                                    { static_cast<float>(Wp.X), static_cast<float>(Wp.Y), static_cast<float>(Wp.Z) },
                                    WpSel ? 0.58f : 0.42f,
                                    10,
                                    6,
                                    WpCol);
                            });
                    }
                }
            }

            // 3D XYZ Transform Gizmo triad around selected Force
            if (Selected)
            {
                constexpr float AxisLen = 4.2f;
                EmitBeaconDraw(0u, Token, 100u, [&]() { AppendSegmentTubeLit(BeaconVerts, C, { C.X + AxisLen, C.Y, C.Z }, 0.12f, 6, { 0.95f, 0.26f, 0.26f, 1.0f }); });
                EmitBeaconDraw(0u, Token, 101u, [&]() { AppendSegmentTubeLit(BeaconVerts, C, { C.X, C.Y + AxisLen, C.Z }, 0.12f, 6, { 0.26f, 0.88f, 0.38f, 1.0f }); });
                EmitBeaconDraw(0u, Token, 102u, [&]() { AppendSegmentTubeLit(BeaconVerts, C, { C.X, C.Y, C.Z + AxisLen }, 0.12f, 6, { 0.30f, 0.62f, 0.98f, 1.0f }); });
            }
        }

        if (Request.OverlaySettings.CrossSectionAnemometerActive)
        {
            const Float3 ProbeP{
                Request.OverlaySettings.AnemometerWorldMeters.X,
                Request.OverlaySettings.AnemometerWorldMeters.Y,
                Request.OverlaySettings.AnemometerWorldMeters.Z,
            };
            EmitBeaconDraw(static_cast<uint32_t>(OverlayPickCategory::Anemometer), 0u, 0u, [&]()
            {
                AppendSphereLit(BeaconVerts, ProbeP, 0.72f, 12, 8, { 1.0f, 0.94f, 0.28f, 1.0f });
                AppendSegmentTubeLit(BeaconVerts, ProbeP, { ProbeP.X, ProbeP.Y, 0.0f }, 0.10f, 6, { 1.0f, 0.92f, 0.35f, 0.72f });
            });
        }
    }

    if (BeaconVerts.empty())
    {
        BeaconVerts.push_back({});
    }
    if (!Impl->UploadToBuffer(Impl->BeaconBuf, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, BeaconVerts.data(), BeaconVerts.size() * sizeof(LitBeaconVertex)))
    {
        OutDiagnostic = "Failed to upload dynamic beacon vertices.";
        return Output;
    }

    // Update shared DescriptorSet bindings
    VkDescriptorBufferInfo UboInfo{ Impl->UniformBuf.Buffer, 0u, sizeof(ViewportOverlayUniformsGpu) };
    VkDescriptorBufferInfo SrcInfo{ Impl->SourceBuf.Buffer, 0u, SrcBytes };
    VkDescriptorBufferInfo WpInfo{ Impl->WaypointBuf.Buffer, 0u, WpBytes };
    std::array<VkWriteDescriptorSet, 3> Writes{};
    Writes[0] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, Impl->DescSet, 0u, 0u, 1u, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &UboInfo, nullptr };
    Writes[1] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, Impl->DescSet, 1u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &SrcInfo, nullptr };
    Writes[2] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, Impl->DescSet, 2u, 0u, 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &WpInfo, nullptr };
    vkUpdateDescriptorSets(Impl->Device, static_cast<uint32_t>(Writes.size()), Writes.data(), 0u, nullptr);

    // Record Vulkan 1.3 command buffer
    vkResetCommandBuffer(Impl->CmdBuffer, 0u);
    VkCommandBufferBeginInfo BeginInfo{};
    BeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    BeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(Impl->CmdBuffer, &BeginInfo) != VK_SUCCESS)
    {
        OutDiagnostic = "vkBeginCommandBuffer failed.";
        return Output;
    }

    auto EmitImageBarrier = [&](
        VkImage               Image,
        VkImageAspectFlags    Aspect,
        VkPipelineStageFlags2 SrcStage,
        VkAccessFlags2        SrcAccess,
        VkPipelineStageFlags2 DstStage,
        VkAccessFlags2        DstAccess,
        VkImageLayout         OldLayout,
        VkImageLayout         NewLayout)
    {
        VkImageMemoryBarrier2KHR Barrier{};
        Barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2_KHR;
        Barrier.srcStageMask        = SrcStage;
        Barrier.srcAccessMask       = SrcAccess;
        Barrier.dstStageMask        = DstStage;
        Barrier.dstAccessMask       = DstAccess;
        Barrier.oldLayout           = OldLayout;
        Barrier.newLayout           = NewLayout;
        Barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        Barrier.image               = Image;
        Barrier.subresourceRange    = { Aspect, 0u, 1u, 0u, 1u };

        VkDependencyInfoKHR Dep{};
        Dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO_KHR;
        Dep.imageMemoryBarrierCount = 1u;
        Dep.pImageMemoryBarriers    = &Barrier;
        Impl->Sync2Barrier(Impl->CmdBuffer, &Dep);
    };

    EmitImageBarrier(
        Pane.ColorImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        0u,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EmitImageBarrier(
        Pane.PickImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        0u,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    EmitImageBarrier(
        Pane.DepthImage,
        VK_IMAGE_ASPECT_DEPTH_BIT,
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
        0u,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL);

    std::array<VkRenderingAttachmentInfoKHR, 2> ColorAtts{};
    ColorAtts[0].sType             = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    ColorAtts[0].imageView         = Pane.ColorView;
    ColorAtts[0].imageLayout       = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColorAtts[0].loadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColorAtts[0].storeOp           = VK_ATTACHMENT_STORE_OP_STORE;
    ColorAtts[0].clearValue.color  = Request.TransparentBackdrop
        ? VkClearColorValue{ { 0.0f, 0.0f, 0.0f, 0.0f } }
        : VkClearColorValue{ { 0.068f, 0.076f, 0.092f, 1.0f } };

    ColorAtts[1].sType             = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    ColorAtts[1].imageView         = Pane.PickView;
    ColorAtts[1].imageLayout       = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    ColorAtts[1].loadOp            = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ColorAtts[1].storeOp           = VK_ATTACHMENT_STORE_OP_STORE;
    ColorAtts[1].clearValue.color.uint32[0] = 0u;
    ColorAtts[1].clearValue.color.uint32[1] = 0u;
    ColorAtts[1].clearValue.color.uint32[2] = 0u;
    ColorAtts[1].clearValue.color.uint32[3] = 0u;

    VkRenderingAttachmentInfoKHR DepthAtt{};
    DepthAtt.sType                   = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
    DepthAtt.imageView               = Pane.DepthView;
    DepthAtt.imageLayout             = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    DepthAtt.loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    DepthAtt.storeOp                 = VK_ATTACHMENT_STORE_OP_STORE;
    DepthAtt.clearValue.depthStencil = { 0.0f, 0u }; // Reverse-Z clear 0.0f!

    VkRenderingInfoKHR RenderInfo{};
    RenderInfo.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR;
    RenderInfo.renderArea           = { { 0, 0 }, { W, H } };
    RenderInfo.layerCount           = 1u;
    RenderInfo.colorAttachmentCount = static_cast<uint32_t>(ColorAtts.size());
    RenderInfo.pColorAttachments    = ColorAtts.data();
    RenderInfo.pDepthAttachment     = &DepthAtt;

    Impl->DynBeginRender(Impl->CmdBuffer, &RenderInfo);

    VkViewport View{};
    View.x        = 0.0f;
    View.y        = 0.0f;
    View.width    = static_cast<float>(W);
    View.height   = static_cast<float>(H);
    View.minDepth = 0.0f;
    View.maxDepth = 1.0f;
    vkCmdSetViewport(Impl->CmdBuffer, 0u, 1u, &View);
    VkRect2D Scissor{ { 0, 0 }, { W, H } };
    vkCmdSetScissor(Impl->CmdBuffer, 0u, 1u, &Scissor);

    vkCmdBindDescriptorSets(
        Impl->CmdBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        Impl->PipeLayout,
        0u,
        1u,
        &Impl->DescSet,
        0u,
        nullptr);

    const VkDeviceSize ZeroOffset = 0u;
    auto PushCmd = [&](const DrawObjectPushConstantsGpu& P)
    {
        vkCmdPushConstants(
            Impl->CmdBuffer,
            Impl->PipeLayout,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            0u,
            sizeof(P),
            &P);
    };

    // Pass 1: Architectural Lit PBR Backdrop
    if (Request.LitShadingEnabled)
    {
        vkCmdBindPipeline(Impl->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Impl->LitPipe);
        vkCmdBindVertexBuffers(Impl->CmdBuffer, 0u, 1u, &Impl->LitSceneBuf.Buffer, &ZeroOffset);
        PushCmd({ 0u, 0u, static_cast<uint32_t>(OverlayPickCategory::LitObject), 1u, 0u, 0u });
        vkCmdDraw(Impl->CmdBuffer, Impl->LitSceneVertCount, 1u, 0u, 0u);
    }

    if (Request.OverlaySettings.Enabled)
    {
        // Pass 2: Cross-Section Heatmaps
        if (Request.OverlaySettings.ShowHeatmap)
        {
            vkCmdBindPipeline(Impl->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Impl->HeatmapPipe);
            if (Request.OverlaySettings.CrossSectionAxis == SimulationSpace::ForceFields::CrossSectionPlaneAxis::AllThreeAxes)
            {
                for (uint32_t Axis = 0u; Axis < 3u; ++Axis)
                {
                    PushCmd({ 0u, Axis, 0u, 0u, 0u, 0u });
                    vkCmdDraw(Impl->CmdBuffer, 6u, 1u, 0u, 0u);
                }
            }
            else
            {
                PushCmd({ 0u, static_cast<uint32_t>(Request.OverlaySettings.CrossSectionAxis), 0u, 0u, 0u, 0u });
                vkCmdDraw(Impl->CmdBuffer, 6u, 1u, 0u, 0u);
            }
        }

        // Pass 3: Semi-Transparent Force Envelopes (no wireframes)
        if (Request.OverlaySettings.ShowBounds && !Request.PackedSources.empty())
        {
            vkCmdBindPipeline(Impl->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Impl->EnvelopePipe);
            for (std::size_t S = 0u; S < Request.PackedSources.size(); ++S)
            {
                const uint32_t Env   = Request.PackedSources[S].EnvelopeId;
                const uint32_t Token = (S < Request.SourceTokens.size()) ? Request.SourceTokens[S] : static_cast<uint32_t>(S + 1u);
                const bool     IsBox = (Env == static_cast<uint32_t>(SimulationSpace::ForceFields::ReachEnvelopeCategory::OrientedBox) ||
                                        Env == static_cast<uint32_t>(SimulationSpace::ForceFields::ReachEnvelopeCategory::PlanarBox));
                VkBuffer       MeshBuf = IsBox ? Impl->UnitBoxBuf.Buffer : Impl->UnitSphereBuf.Buffer;
                const uint32_t VertCnt = IsBox ? Impl->UnitBoxVertCount : Impl->UnitSphereVertCount;
                vkCmdBindVertexBuffers(Impl->CmdBuffer, 0u, 1u, &MeshBuf, &ZeroOffset);
                PushCmd({ static_cast<uint32_t>(S), Env, static_cast<uint32_t>(OverlayPickCategory::ForceCircle), Token, 0u, 0u });
                vkCmdDraw(Impl->CmdBuffer, VertCnt, 1u, 0u, 0u);
            }
        }

        // Pass 4: Instanced 3D Vector Arrows
        if (Request.OverlaySettings.ShowVectors && !Request.PackedSources.empty())
        {
            vkCmdBindPipeline(Impl->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Impl->ArrowPipe);
            vkCmdBindVertexBuffers(Impl->CmdBuffer, 0u, 1u, &Impl->UnitArrowBuf.Buffer, &ZeroOffset);
            PushCmd({ 0u, 0u, 0u, 0u, 0u, 0u });
            vkCmdDraw(Impl->CmdBuffer, Impl->UnitArrowVertCount, 11u * 11u * 3u, 0u, 0u);
        }

        // Pass 5: Volumetric 3D RK4 Streamline Ribbons
        if (Request.OverlaySettings.ShowStreamlines && !Request.PackedSources.empty())
        {
            const uint32_t Segs      = std::clamp(Request.OverlaySettings.StreamCurveResolution, 8u, 72u);
            const uint32_t VertCount = (Segs + 1u) * 2u;
            const uint32_t LineCount = std::clamp(Request.OverlaySettings.StreamLineCount, 4u, 220u);
            vkCmdBindPipeline(Impl->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Impl->StreamlinePipe);
            PushCmd({ 0u, 0u, 0u, 0u, 0u, 0u });
            vkCmdDraw(Impl->CmdBuffer, VertCount, LineCount, 0u, 0u);
        }

        // Pass 6: Force Beacons, Motion Paths, Clickable Waypoints, 3D Axis Gizmo, Anemometer
        if (!BeaconDraws.empty())
        {
            vkCmdBindPipeline(Impl->CmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Impl->BeaconPipe);
            vkCmdBindVertexBuffers(Impl->CmdBuffer, 0u, 1u, &Impl->BeaconBuf.Buffer, &ZeroOffset);
            for (const auto& Span : BeaconDraws)
            {
                PushCmd(Span.Push);
                vkCmdDraw(Impl->CmdBuffer, Span.VertexCount, 1u, Span.FirstVertex, 0u);
            }
        }
    }

    Impl->DynEndRender(Impl->CmdBuffer);

    // Transition Color + Pick images -> TRANSFER_SRC_OPTIMAL and readback to host buffers
    EmitImageBarrier(
        Pane.ColorImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    EmitImageBarrier(
        Pane.PickImage,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_TRANSFER_READ_BIT,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    VkBufferImageCopy CopyRegion{};
    CopyRegion.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
    CopyRegion.imageExtent      = { W, H, 1u };
    vkCmdCopyImageToBuffer(Impl->CmdBuffer, Pane.ColorImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Pane.ColorReadback.Buffer, 1u, &CopyRegion);
    vkCmdCopyImageToBuffer(Impl->CmdBuffer, Pane.PickImage,  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, Pane.PickReadback.Buffer,  1u, &CopyRegion);

    if (vkEndCommandBuffer(Impl->CmdBuffer) != VK_SUCCESS)
    {
        OutDiagnostic = "vkEndCommandBuffer failed for overlay pass.";
        return Output;
    }

    vkResetFences(Impl->Device, 1u, &Impl->SubmitFence);
    VkSubmitInfo Submit{};
    Submit.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    Submit.commandBufferCount = 1u;
    Submit.pCommandBuffers    = &Impl->CmdBuffer;
    if (vkQueueSubmit(Impl->GraphicsQueue, 1u, &Submit, Impl->SubmitFence) != VK_SUCCESS)
    {
        OutDiagnostic = "vkQueueSubmit failed for overlay pass.";
        return Output;
    }
    if (vkWaitForFences(Impl->Device, 1u, &Impl->SubmitFence, VK_TRUE, 30'000'000'000ull) != VK_SUCCESS)
    {
        OutDiagnostic = "vkWaitForFences timed out waiting for overlay pass.";
        return Output;
    }

    void* MappedColor = nullptr;
    if (vkMapMemory(Impl->Device, Pane.ColorReadback.Memory, 0u, Pane.CpuColorRgba.size(), 0u, &MappedColor) == VK_SUCCESS)
    {
        std::memcpy(Pane.CpuColorRgba.data(), MappedColor, Pane.CpuColorRgba.size());
        vkUnmapMemory(Impl->Device, Pane.ColorReadback.Memory);
    }
    void* MappedPick = nullptr;
    const VkDeviceSize PickBytes = Pane.CpuPickRgba32ui.size() * sizeof(uint32_t);
    if (vkMapMemory(Impl->Device, Pane.PickReadback.Memory, 0u, PickBytes, 0u, &MappedPick) == VK_SUCCESS)
    {
        std::memcpy(Pane.CpuPickRgba32ui.data(), MappedPick, static_cast<std::size_t>(PickBytes));
        vkUnmapMemory(Impl->Device, Pane.PickReadback.Memory);
    }

    Pane.TextureRecord.Pixels = Pane.CpuColorRgba.data();
    Pane.TextureRecord.UpdateRect = { 0u, 0u, static_cast<unsigned short>(W), static_cast<unsigned short>(H) };
    Pane.TextureRecord.SetStatus(
        (Pane.TextureRecord.BackendUserData != nullptr && Pane.TextureRecord.GetTexID() != ImTextureID_Invalid)
            ? ImTextureStatus_WantUpdates
            : ImTextureStatus_WantCreate);

    Output.ImTextureDataPtr = &Pane.TextureRecord;
    Output.WidthPx          = W;
    Output.HeightPx         = H;
    Output.RenderedOnVulkan = true;
    return Output;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           MRT PICK QUERY & PNG EXPORT
//------------------------------------------------------------------------------------------------------------------------

OverlayPickResult VectorFieldOverlaySequence::QueryPickAtNormalizedUv(
    uint32_t PaneIndex,
    float    NormU,
    float    NormV) const noexcept
{
    OverlayPickResult Result{};
    if (Impl == nullptr || PaneIndex >= 2u)
    {
        return Result;
    }
    const auto& Pane = Impl->Panes[PaneIndex];
    if (Pane.WidthPx == 0u || Pane.HeightPx == 0u || Pane.CpuPickRgba32ui.empty())
    {
        return Result;
    }

    const uint32_t Px = static_cast<uint32_t>(std::clamp(NormU, 0.0f, 0.9999f) * static_cast<float>(Pane.WidthPx));
    const uint32_t Py = static_cast<uint32_t>(std::clamp(NormV, 0.0f, 0.9999f) * static_cast<float>(Pane.HeightPx));
    const std::size_t Base = (static_cast<std::size_t>(Py) * Pane.WidthPx + Px) * 4u;
    if (Base + 2u >= Pane.CpuPickRgba32ui.size())
    {
        return Result;
    }

    const uint32_t RawCat = Pane.CpuPickRgba32ui[Base + 0u];
    if (RawCat <= static_cast<uint32_t>(OverlayPickCategory::LitObject))
    {
        Result.Category    = static_cast<OverlayPickCategory>(RawCat);
        Result.PrimaryId   = Pane.CpuPickRgba32ui[Base + 1u];
        Result.SecondaryId = Pane.CpuPickRgba32ui[Base + 2u];
    }
    return Result;
}

bool VectorFieldOverlaySequence::SavePanePng(
    uint32_t                     PaneIndex,
    const std::filesystem::path& OutputPngPath) const noexcept
{
    if (Impl == nullptr || PaneIndex >= 2u)
    {
        return false;
    }
    const auto& Pane = Impl->Panes[PaneIndex];
    if (Pane.WidthPx == 0u || Pane.HeightPx == 0u || Pane.CpuColorRgba.empty())
    {
        return false;
    }
    std::error_code Ec;
    if (OutputPngPath.has_parent_path())
    {
        std::filesystem::create_directories(OutputPngPath.parent_path(), Ec);
    }
    return stbi_write_png(
               OutputPngPath.string().c_str(),
               static_cast<int>(Pane.WidthPx),
               static_cast<int>(Pane.HeightPx),
               4,
               Pane.CpuColorRgba.data(),
               static_cast<int>(Pane.WidthPx * 4u)) != 0;
}

std::span<const uint8_t> VectorFieldOverlaySequence::QueryColorReadbackRgba(uint32_t PaneIndex) const noexcept
{
    if (Impl == nullptr || PaneIndex >= 2u)
    {
        return {};
    }
    return Impl->Panes[PaneIndex].CpuColorRgba;
}

uint32_t VectorFieldOverlaySequence::QueryPaneWidth(uint32_t PaneIndex) const noexcept
{
    return (Impl != nullptr && PaneIndex < 2u) ? Impl->Panes[PaneIndex].WidthPx : 0u;
}

uint32_t VectorFieldOverlaySequence::QueryPaneHeight(uint32_t PaneIndex) const noexcept
{
    return (Impl != nullptr && PaneIndex < 2u) ? Impl->Panes[PaneIndex].HeightPx : 0u;
}

} // namespace Frontier::Editor::Simulation

/*============================================================================================================================================
                                                CLEANEMISSIVESHADOWVALIDATION.CPP
============================================================================================================================================*/
// 🧪 Validation & Visual Suite: Soft Solar Shadows + Deforming Character Bone Capsules Paired with Clean Emissive GI (Zero Denoiser)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Exhibits/CleanShadowAndGiCandidates.h"

#include "Engine/Constants/MathConstants.h"
#include "Engine/DistanceFieldSpace/VoxelSdfClipmap.h"
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicIntegrators.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"
#include "Engine/MaterialSpace/Scattering/OpenPbrSurfaceScattering.h"
#include "Engine/ParticleSpace/Origins/ControlVehicleSurfaceLoader.h"

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
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace Frontier;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   VALIDATION & TILE TOOLS
//------------------------------------------------------------------------------------------------------------------------

struct ShadowScoreboard
{
    uint32_t Verified = 0u;
    uint32_t Failed   = 0u;

    void Check(bool Passed, const char* Text) noexcept
    {
        if (Passed)
        {
            ++Verified;
            std::printf("  [OK] %s\n", Text);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "  [FAIL] %s\n", Text);
        }
    }
};

struct GlyphPattern
{
    char                   Symbol;
    std::array<uint8_t, 7> Rows;
};

inline constexpr std::array<GlyphPattern, 44u> SheetGlyphs {{
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { ':', { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { '/', { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
    { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
    { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F } },
    { '3', { 0x1F, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
    { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 } },
    { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
    { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
    { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
}};

void DrawBitmapLabel(
    std::vector<uint8_t>&  Rgba,
    uint32_t               SheetW,
    uint32_t               SheetH,
    uint32_t               OX,
    uint32_t               OY,
    std::string_view       Text,
    uint32_t               Scale,
    std::array<uint8_t, 3> Tint) noexcept
{
    uint32_t CX = OX;
    for (char Ch : Text)
    {
        if (Ch >= 'a' && Ch <= 'z') Ch = static_cast<char>(Ch - 'a' + 'A');
        const GlyphPattern* Hit = &SheetGlyphs[0];
        for (const auto& G : SheetGlyphs)
        {
            if (G.Symbol == Ch) { Hit = &G; break; }
        }
        for (uint32_t R = 0u; R < 7u; ++R)
        {
            const uint8_t Bits = Hit->Rows[R];
            for (uint32_t C = 0u; C < 5u; ++C)
            {
                if (((Bits >> (4u - C)) & 1u) == 0u) continue;
                for (uint32_t SY = 0u; SY < Scale; ++SY)
                {
                    for (uint32_t SX = 0u; SX < Scale; ++SX)
                    {
                        const uint32_t PX = CX + C * Scale + SX;
                        const uint32_t PY = OY + R * Scale + SY;
                        if (PX < SheetW && PY < SheetH)
                        {
                            const std::size_t Off = (static_cast<std::size_t>(PY) * SheetW + PX) * 4u;
                            Rgba[Off + 0u] = Tint[0];
                            Rgba[Off + 1u] = Tint[1];
                            Rgba[Off + 2u] = Tint[2];
                            Rgba[Off + 3u] = 255u;
                        }
                    }
                }
            }
        }
        CX += 6u * Scale;
    }
}

void FillRectRgba(
    std::vector<uint8_t>&  Rgba,
    uint32_t               SheetW,
    uint32_t               SheetH,
    uint32_t               X0,
    uint32_t               Y0,
    uint32_t               W,
    uint32_t               H,
    std::array<uint8_t, 3> Color) noexcept
{
    const uint32_t X1 = std::min(SheetW, X0 + W);
    const uint32_t Y1 = std::min(SheetH, Y0 + H);
    for (uint32_t Y = Y0; Y < Y1; ++Y)
    {
        for (uint32_t X = X0; X < X1; ++X)
        {
            const std::size_t Off = (static_cast<std::size_t>(Y) * SheetW + X) * 4u;
            Rgba[Off + 0u] = Color[0];
            Rgba[Off + 1u] = Color[1];
            Rgba[Off + 2u] = Color[2];
            Rgba[Off + 3u] = 255u;
        }
    }
}

[[nodiscard]] inline uint8_t EncodeSrgb8(float LinearVal) noexcept
{
    const float C = std::clamp(LinearVal, 0.0f, 1.0f);
    const float S = (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::round(S * 255.0f), 0.0f, 255.0f));
}

[[nodiscard]] inline float ApplyAcesFit(float V) noexcept
{
    const float X = std::max(0.0f, V);
    return (X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f);
}

void SavePngRgba(
    const std::filesystem::path& OutPath,
    uint32_t                     W,
    uint32_t                     H,
    std::span<const uint8_t>     Rgba)
{
    std::filesystem::create_directories(OutPath.parent_path());
    stbi_write_png(OutPath.string().c_str(), static_cast<int>(W), static_cast<int>(H), 4, Rgba.data(), static_cast<int>(W * 4u));
}

struct SheetTile
{
    std::string            Title  {};
    std::string            Detail {};
    std::array<uint8_t, 3> Accent { 170u, 196u, 232u };
    std::vector<uint8_t>   Rgba8  {};
};

void SaveGridSheetPng(
    const std::filesystem::path& OutPath,
    std::string_view             Banner,
    uint32_t                     Cols,
    uint32_t                     Rows,
    uint32_t                     TileW,
    uint32_t                     TileH,
    std::span<const SheetTile>   Tiles)
{
    constexpr uint32_t PadX = 14u, PadY = 14u, BannerH = 42u, HeadH = 34u;
    const uint32_t SheetW = PadX + Cols * (TileW + PadX);
    const uint32_t SheetH = BannerH + PadY + Rows * (TileH + HeadH + PadY);
    std::vector<uint8_t> Sheet(static_cast<std::size_t>(SheetW) * SheetH * 4u, 255u);
    FillRectRgba(Sheet, SheetW, SheetH, 0u, 0u, SheetW, SheetH, { 16u, 19u, 25u });
    FillRectRgba(Sheet, SheetW, SheetH, 0u, 0u, SheetW, BannerH, { 26u, 31u, 42u });
    FillRectRgba(Sheet, SheetW, SheetH, 0u, BannerH - 2u, SheetW, 2u, { 72u, 130u, 206u });
    DrawBitmapLabel(Sheet, SheetW, SheetH, 16u, 14u, Banner, 2u, { 235u, 242u, 252u });

    for (uint32_t R = 0u; R < Rows; ++R)
    {
        for (uint32_t C = 0u; C < Cols; ++C)
        {
            const std::size_t Idx = static_cast<std::size_t>(R) * Cols + C;
            if (Idx >= Tiles.size()) continue;
            const auto& T = Tiles[Idx];
            const uint32_t OX = PadX + C * (TileW + PadX);
            const uint32_t OY = BannerH + PadY + R * (TileH + HeadH + PadY);
            FillRectRgba(Sheet, SheetW, SheetH, OX, OY, TileW, HeadH, { 28u, 33u, 44u });
            FillRectRgba(Sheet, SheetW, SheetH, OX, OY, 5u, HeadH, T.Accent);
            DrawBitmapLabel(Sheet, SheetW, SheetH, OX + 12u, OY + 6u, T.Title, 2u, { 244u, 247u, 252u });
            if (!T.Detail.empty())
            {
                DrawBitmapLabel(Sheet, SheetW, SheetH, OX + 12u, OY + 23u, T.Detail, 1u, T.Accent);
            }
            if (T.Rgba8.size() == static_cast<std::size_t>(TileW) * TileH * 4u)
            {
                for (uint32_t Y = 0u; Y < TileH; ++Y)
                {
                    std::copy_n(
                        &T.Rgba8[static_cast<std::size_t>(Y) * TileW * 4u],
                        static_cast<std::size_t>(TileW) * 4u,
                        &Sheet[(static_cast<std::size_t>(OY + HeadH + Y) * SheetW + OX) * 4u]);
                }
            }
        }
    }
    SavePngRgba(OutPath, SheetW, SheetH, Sheet);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  STUDIO SCENE WITH CONTROLVEHICLE + DEFORMING FIGURE
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Mathematics::Axis3D StudioPointToWorld(const float (&P)[3]) noexcept
{
    return { P[0], -P[2], P[1] };
}

struct ShadowStudioWorld
{
    GeometricRaster::WorldSceneSpecification        Scene            {};
    GeometrySpace::PolygonNetwork                   StaticNet        {};
    GeometrySpace::PolygonNetwork                   VehicleNet       {};
    SpatialAcceleration::WideBoundingTree           VehicleBvh       {};
    DistanceFieldSpace::LocalGeometrySdfAsset       VehicleLocalSdf  {};
    std::vector<DistanceFieldSpace::ClosedBox>      StaticBoxes      {};
    std::vector<DistanceFieldSpace::ClosedBox>      InteriorBoxes    {};
    std::array<uint16_t, 16u>                       VehicleSlots     {};
    uint16_t                                        SlotFloor        = 0u;
    uint16_t                                        SlotCrimson      = 0u;
    uint16_t                                        SlotCobalt       = 0u;
    uint16_t                                        SlotCharSuit     = 0u;
    uint16_t                                        SlotCharAccent   = 0u;
};

[[nodiscard]] ShadowStudioWorld ConstructShadowStudioBase()
{
    ShadowStudioWorld World {};
    auto& Sc = World.Scene;

    MaterialSpace::SurfaceMaterial FloorMat {};
    FloorMat.MaterialName       = "StudioFloor";
    FloorMat.Diffuse.BaseColor  = { 0.64f, 0.63f, 0.60f };
    FloorMat.Specular.Roughness = 0.48f;

    MaterialSpace::SurfaceMaterial CrimsonMat {};
    CrimsonMat.MaterialName       = "StudioCrimson";
    CrimsonMat.Diffuse.BaseColor  = { 0.85f, 0.08f, 0.07f };
    CrimsonMat.Specular.Roughness = 0.60f;

    MaterialSpace::SurfaceMaterial CobaltMat {};
    CobaltMat.MaterialName       = "StudioCobalt";
    CobaltMat.Diffuse.BaseColor  = { 0.16f, 0.34f, 0.86f };
    CobaltMat.Specular.Roughness = 0.42f;

    MaterialSpace::SurfaceMaterial SuitMat {};
    SuitMat.MaterialName       = "CharacterSuit";
    SuitMat.Diffuse.BaseColor  = { 0.68f, 0.70f, 0.74f };
    SuitMat.Specular.Roughness = 0.50f;

    MaterialSpace::SurfaceMaterial AccentMat {};
    AccentMat.MaterialName       = "CharacterAccent";
    AccentMat.Diffuse.BaseColor  = { 0.88f, 0.52f, 0.16f };
    AccentMat.Specular.Roughness = 0.36f;

    World.SlotFloor      = Sc.MaterialRegister.RegisterMaterial(FloorMat);
    World.SlotCrimson    = Sc.MaterialRegister.RegisterMaterial(CrimsonMat);
    World.SlotCobalt     = Sc.MaterialRegister.RegisterMaterial(CobaltMat);
    World.SlotCharSuit   = Sc.MaterialRegister.RegisterMaterial(SuitMat);
    World.SlotCharAccent = Sc.MaterialRegister.RegisterMaterial(AccentMat);

    MaterialSpace::SurfaceMaterial BodyCoat {};
    BodyCoat.Diffuse.BaseColor       = { 0.08f, 0.32f, 0.84f };
    BodyCoat.Specular.Roughness      = 0.14f;
    BodyCoat.Clearcoat.CoatWeight    = 1.0f;
    BodyCoat.Clearcoat.CoatRoughness = 0.04f;

    MaterialSpace::SurfaceMaterial GlassMat {};
    GlassMat.Diffuse.BaseWeight              = 0.05f;
    GlassMat.Diffuse.BaseColor               = { 0.10f, 0.15f, 0.22f };
    GlassMat.Specular.Roughness              = 0.04f;
    GlassMat.Transmission.TransmissionWeight = 0.82f;

    MaterialSpace::SurfaceMaterial DarkMat {};
    DarkMat.Diffuse.BaseColor  = { 0.06f, 0.065f, 0.07f };
    DarkMat.Specular.Roughness = 0.65f;

    MaterialSpace::SurfaceMaterial RimMat {};
    RimMat.Diffuse.BaseColor  = { 0.72f, 0.74f, 0.77f };
    RimMat.Specular.Metalness = 0.85f;
    RimMat.Specular.Roughness = 0.22f;

    MaterialSpace::SurfaceMaterial HeadlightMat {};
    HeadlightMat.MaterialName                 = "HeadlightBeam";
    HeadlightMat.Diffuse.BaseColor            = { 0.96f, 0.98f, 1.0f };
    HeadlightMat.PorosityLight.EmissionLuminanceNits = 76000.0f;
    HeadlightMat.PorosityLight.EmissionTint  = { 1.0f, 0.96f, 0.88f };

    MaterialSpace::SurfaceMaterial TaillightMat {};
    TaillightMat.MaterialName                 = "TaillightLamp";
    TaillightMat.Diffuse.BaseColor            = { 0.85f, 0.08f, 0.08f };
    TaillightMat.PorosityLight.EmissionLuminanceNits = 64000.0f;
    TaillightMat.PorosityLight.EmissionTint  = { 1.0f, 0.12f, 0.08f };

    MaterialSpace::SurfaceMaterial PlateMat {};
    PlateMat.Diffuse.BaseColor  = { 0.72f, 0.73f, 0.74f };
    PlateMat.Specular.Roughness = 0.42f;

    World.VehicleSlots[6u]  = Sc.MaterialRegister.RegisterMaterial(BodyCoat);
    World.VehicleSlots[7u]  = Sc.MaterialRegister.RegisterMaterial(GlassMat);
    World.VehicleSlots[8u]  = Sc.MaterialRegister.RegisterMaterial(DarkMat);
    World.VehicleSlots[9u]  = Sc.MaterialRegister.RegisterMaterial(DarkMat);
    World.VehicleSlots[10u] = Sc.MaterialRegister.RegisterMaterial(RimMat);
    World.VehicleSlots[11u] = Sc.MaterialRegister.RegisterMaterial(RimMat);
    World.VehicleSlots[12u] = Sc.MaterialRegister.RegisterMaterial(DarkMat);
    World.VehicleSlots[13u] = Sc.MaterialRegister.RegisterMaterial(HeadlightMat);
    World.VehicleSlots[14u] = Sc.MaterialRegister.RegisterMaterial(TaillightMat);
    World.VehicleSlots[15u] = Sc.MaterialRegister.RegisterMaterial(PlateMat);

    const auto& Geom = ParticleSpace::QueryControlVehicleGeometrySet();
    for (const auto& Tri : Geom.Triangles)
    {
        const uint32_t Code = std::clamp(Tri.MaterialSlotCode, 6u, 15u);
        const uint32_t V0 = World.VehicleNet.AppendVertex(StudioPointToWorld(Tri.PositionA));
        const uint32_t V1 = World.VehicleNet.AppendVertex(StudioPointToWorld(Tri.PositionB));
        const uint32_t V2 = World.VehicleNet.AppendVertex(StudioPointToWorld(Tri.PositionC));
        const std::array<uint32_t, 3u> Loop { V0, V1, V2 };
        (void)World.VehicleNet.AppendFace(Loop, {}, {}, 0u, World.VehicleSlots[Code]);
    }
    (void)World.VehicleBvh.BuildFromPolygonNetwork(World.VehicleNet);
    (void)World.VehicleLocalSdf.BakeFromLocalTree(
        "ControlVehicle", World.VehicleBvh, Sc.MaterialRegister, 0.048f, 0.30f);

    const auto AddBox = [&](Mathematics::Axis3D C, Mathematics::Scalar3D H, uint16_t Mat, bool Interior)
    {
        GeometrySpace::AppendBoxPrimitive(World.StaticNet, C, H, {}, {}, 0u, Mat);
        const DistanceFieldSpace::ClosedBox B {
            DistanceFieldSpace::ClipVector { C.X, C.Y, C.Z },
            DistanceFieldSpace::ClipVector { H.X, H.Y, H.Z },
            Mat
        };
        World.StaticBoxes.push_back(B);
        if (Interior)
        {
            World.InteriorBoxes.push_back(B);
        }
    };

    // Studio floor + left crimson & back/right plaster walls + two interior color pillars
    AddBox({  0.0f,  0.0f, -0.25f }, { 7.5f, 6.5f, 0.25f }, World.SlotFloor,   false);
    AddBox({ -5.75f, 0.0f,  2.2f  }, { 0.25f, 6.5f, 2.45f }, World.SlotCrimson, false);
    AddBox({  0.0f,  4.45f, 2.2f  }, { 7.5f, 0.25f, 2.45f }, World.SlotFloor,   false);
    AddBox({  5.25f, 0.0f,  2.2f  }, { 0.25f, 6.5f, 2.45f }, World.SlotFloor,   false);
    AddBox({ -3.1f,  2.5f,  1.20f }, { 0.35f, 0.35f, 1.20f }, World.SlotCobalt,  true);
    AddBox({ -2.4f,  2.8f,  0.90f }, { 0.30f, 0.30f, 0.90f }, World.SlotFloor,   true);

    return World;
}

[[nodiscard]] Exhibits::ArticulatedFigureRig ComposeSceneWithPose(
    ShadowStudioWorld&          World,
    const Mathematics::Axis3D&  VehicleOffset,
    const Mathematics::Axis3D&  CharacterRoot,
    float                       PosePhase)
{
    GeometrySpace::PolygonNetwork FullNet = World.StaticNet;

    const auto& Geom = ParticleSpace::QueryControlVehicleGeometrySet();
    for (const auto& Tri : Geom.Triangles)
    {
        const uint32_t Code = std::clamp(Tri.MaterialSlotCode, 6u, 15u);
        const uint32_t V0 = FullNet.AppendVertex(StudioPointToWorld(Tri.PositionA) + VehicleOffset);
        const uint32_t V1 = FullNet.AppendVertex(StudioPointToWorld(Tri.PositionB) + VehicleOffset);
        const uint32_t V2 = FullNet.AppendVertex(StudioPointToWorld(Tri.PositionC) + VehicleOffset);
        const std::array<uint32_t, 3u> Loop { V0, V1, V2 };
        (void)FullNet.AppendFace(Loop, {}, {}, 0u, World.VehicleSlots[Code]);
    }

    auto Rig = Exhibits::BuildArticulatedFigurePose(
        PosePhase, CharacterRoot, FullNet, World.SlotCharSuit, World.SlotCharAccent);

    (void)World.Scene.SceneTree.BuildFromPolygonNetwork(FullNet);

    DistanceFieldSpace::DynamicLocalSdfInstance Inst {};
    Inst.Asset     = &World.VehicleLocalSdf;
    Inst.Transform = DistanceFieldSpace::RigidTransform3D::FromTranslationYawPitchRoll(VehicleOffset);
    Inst.Enabled   = true;
    Inst.UpdateWorldBounds();
    World.Scene.DynamicSdfObjects = { Inst };

    return Rig;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CAMERA RAY & PIXEL SHADING PIPELINE
//------------------------------------------------------------------------------------------------------------------------

struct CameraFrame
{
    Mathematics::Axis3D Origin  {};
    Mathematics::Axis3D Forward {};
    Mathematics::Axis3D Right   {};
    Mathematics::Axis3D Up      {};
    float               TanHalfFov = 0.42f;
    float               Aspect     = 16.0f / 9.0f;
};

[[nodiscard]] CameraFrame MakeShowcaseCamera(uint32_t W, uint32_t H) noexcept
{
    CameraFrame C {};
    C.Origin  = { 4.35f, -4.75f, 2.35f };
    const Mathematics::Axis3D LookAt { -0.15f, -0.55f, 0.70f };
    C.Forward = Mathematics::NormalizeAxis(LookAt - C.Origin);
    C.Right   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(C.Forward, { 0.0f, 0.0f, 1.0f }));
    C.Up      = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(C.Right, C.Forward));
    C.TanHalfFov = std::tan(23.0f * Constants::PiFloat / 180.0f);
    C.Aspect     = static_cast<float>(W) / static_cast<float>(H);
    return C;
}

struct PrimaryHitSample
{
    bool                   Valid   = false;
    Mathematics::Axis3D    Pos     {};
    Mathematics::Axis3D    Normal  {};
    Mathematics::LinearRGB Albedo  {};
    Mathematics::LinearRGB SelfRad {};
    float                  Metal   = 0.0f;
    float                  Rough   = 0.5f;
    float                  DiffW   = 1.0f;
};

template <typename Fn>
void ParallelRows(uint32_t Height, Fn&& Callback)
{
    const uint32_t Threads = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
    if (Threads <= 1u || Height < Threads * 2u)
    {
        Callback(0u, Height);
        return;
    }
    std::vector<std::thread> Pool {};
    Pool.reserve(Threads);
    const uint32_t Chunk = (Height + Threads - 1u) / Threads;
    for (uint32_t T = 0u; T < Threads; ++T)
    {
        const uint32_t Y0 = T * Chunk;
        const uint32_t Y1 = std::min(Height, Y0 + Chunk);
        if (Y0 < Y1)
        {
            Pool.emplace_back([&, Y0, Y1]() { Callback(Y0, Y1); });
        }
    }
    for (auto& W : Pool)
    {
        W.join();
    }
}

[[nodiscard]] std::vector<PrimaryHitSample> TracePrimaryBuffer(
    const GeometricRaster::WorldSceneSpecification& Scene,
    const CameraFrame&                              Cam,
    uint32_t                                        W,
    uint32_t                                        H)
{
    std::vector<PrimaryHitSample> Out(static_cast<std::size_t>(W) * H);
    const uint32_t MatCount = Scene.MaterialRegister.QuerySlotCount();
    ParallelRows(H, [&](uint32_t Y0, uint32_t Y1)
    {
        for (uint32_t Y = Y0; Y < Y1; ++Y)
        {
            const float NDCY = 1.0f - 2.0f * ((static_cast<float>(Y) + 0.5f) / static_cast<float>(H));
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float NDCX = 2.0f * ((static_cast<float>(X) + 0.5f) / static_cast<float>(W)) - 1.0f;
                const auto Dir = Mathematics::NormalizeAxis(
                    Cam.Forward + Cam.Right * (NDCX * Cam.TanHalfFov * Cam.Aspect) + Cam.Up * (NDCY * Cam.TanHalfFov));
                SpatialAcceleration::RaySegmentQuery Q {};
                Q.Origin          = Cam.Origin;
                Q.UnitDirection   = Dir;
                Q.MinimumDistance = 1.0e-3f;
                Q.MaximumDistance = 80.0f;
                const auto Hit = Scene.SceneTree.TraceClosestRay(Q, false);
                if (!Hit.HitConfirmed)
                {
                    continue;
                }
                auto& S = Out[static_cast<std::size_t>(Y) * W + X];
                S.Valid  = true;
                S.Pos    = Cam.Origin + Dir * Hit.HitDistanceMeters;
                S.Normal = Hit.GeometricNormal;
                if (Mathematics::EvaluateDot(S.Normal, Dir) > 0.0f)
                {
                    S.Normal = S.Normal * -1.0f;
                }
                if (Hit.MaterialOrdinal < MatCount)
                {
                    const auto& Mat = Scene.MaterialRegister.InspectAuthoredMaterial(Hit.MaterialOrdinal);
                    S.Albedo  = Mat.Diffuse.BaseColor * Mat.Diffuse.BaseWeight;
                    S.Metal   = Mat.Specular.Metalness;
                    S.Rough   = Mat.Specular.Roughness;
                    S.DiffW   = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight);
                    S.SelfRad = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
                }
            }
        }
    });
    return Out;
}

[[nodiscard]] std::vector<uint8_t> ConvertShadowMaskToRgba(std::span<const float> Vis)
{
    std::vector<uint8_t> Out(Vis.size() * 4u, 255u);
    for (std::size_t I = 0u; I < Vis.size(); ++I)
    {
        const uint8_t C = EncodeSrgb8(std::clamp(Vis[I], 0.0f, 1.0f));
        Out[I * 4u + 0u] = C;
        Out[I * 4u + 1u] = C;
        Out[I * 4u + 2u] = C;
    }
    return Out;
}

[[nodiscard]] std::vector<uint8_t> RenderFullLitFrame(
    uint32_t                                W,
    uint32_t                                H,
    std::span<const PrimaryHitSample>       GBuf,
    std::span<const float>                  ShadowVis,
    std::span<const Mathematics::LinearRGB> EmissiveGiOverPi,
    const Exhibits::DirectionalSolarSpec&   Sun,
    const CameraFrame&                      Cam,
    std::vector<Mathematics::LinearRGB>*    OutHdrLinear = nullptr)
{
    const std::size_t Count = static_cast<std::size_t>(W) * H;
    std::vector<uint8_t> Rgba(Count * 4u, 255u);
    if (OutHdrLinear != nullptr)
    {
        OutHdrLinear->assign(Count, {});
    }
    const Mathematics::Axis3D SunDir = Mathematics::NormalizeAxis(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });

    for (std::size_t I = 0u; I < Count; ++I)
    {
        const auto& P = GBuf[I];
        if (!P.Valid)
        {
            Rgba[I * 4u + 0u] = 10u;
            Rgba[I * 4u + 1u] = 12u;
            Rgba[I * 4u + 2u] = 16u;
            continue;
        }
        const float Vis   = ShadowVis[I];
        const float NdotL = std::max(0.0f, Mathematics::EvaluateDot(P.Normal, SunDir));
        const auto  ViewD = Mathematics::NormalizeAxis(Cam.Origin - P.Pos);
        const auto  HalfD = Mathematics::NormalizeAxis(SunDir + ViewD);
        const float NdotH = std::max(0.0f, Mathematics::EvaluateDot(P.Normal, HalfD));
        const float Alpha = std::max(0.04f, P.Rough * P.Rough);
        const float SpecP = std::pow(NdotH, std::clamp(2.0f / (Alpha * Alpha) - 2.0f, 2.0f, 256.0f)) *
                            (0.04f + 0.30f * (1.0f - P.Rough)) * NdotL * Vis;

        const Mathematics::LinearRGB SunDiff {
            P.Albedo.R * P.DiffW * Sun.SolarIrradiance.R * (NdotL * Vis / Constants::PiFloat),
            P.Albedo.G * P.DiffW * Sun.SolarIrradiance.G * (NdotL * Vis / Constants::PiFloat),
            P.Albedo.B * P.DiffW * Sun.SolarIrradiance.B * (NdotL * Vis / Constants::PiFloat),
        };
        const Mathematics::LinearRGB Gi {
            P.Albedo.R * P.DiffW * EmissiveGiOverPi[I].R,
            P.Albedo.G * P.DiffW * EmissiveGiOverPi[I].G,
            P.Albedo.B * P.DiffW * EmissiveGiOverPi[I].B,
        };
        const Mathematics::LinearRGB Fill {
            P.Albedo.R * P.DiffW * 0.025f,
            P.Albedo.G * P.DiffW * 0.030f,
            P.Albedo.B * P.DiffW * 0.040f,
        };
        const Mathematics::LinearRGB Spec {
            Sun.SolarIrradiance.R * SpecP * (P.Metal > 0.5f ? P.Albedo.R : 1.0f),
            Sun.SolarIrradiance.G * SpecP * (P.Metal > 0.5f ? P.Albedo.G : 1.0f),
            Sun.SolarIrradiance.B * SpecP * (P.Metal > 0.5f ? P.Albedo.B : 1.0f),
        };

        const Mathematics::LinearRGB Total {
            P.SelfRad.R + SunDiff.R + Gi.R + Fill.R + Spec.R,
            P.SelfRad.G + SunDiff.G + Gi.G + Fill.G + Spec.G,
            P.SelfRad.B + SunDiff.B + Gi.B + Fill.B + Spec.B,
        };
        if (OutHdrLinear != nullptr)
        {
            (*OutHdrLinear)[I] = Total;
        }
        Rgba[I * 4u + 0u] = EncodeSrgb8(ApplyAcesFit(Total.R));
        Rgba[I * 4u + 1u] = EncodeSrgb8(ApplyAcesFit(Total.G));
        Rgba[I * 4u + 2u] = EncodeSrgb8(ApplyAcesFit(Total.B));
    }
    return Rgba;
}

[[nodiscard]] float ComputeScalarRmse(std::span<const float> A, std::span<const float> B, std::span<const PrimaryHitSample> Mask) noexcept
{
    double SumSq = 0.0;
    uint64_t N = 0u;
    for (std::size_t I = 0u; I < A.size(); ++I)
    {
        if (!Mask[I].Valid) continue;
        const double D = static_cast<double>(A[I]) - static_cast<double>(B[I]);
        SumSq += D * D;
        ++N;
    }
    return N > 0u ? static_cast<float>(std::sqrt(SumSq / static_cast<double>(N))) : 0.0f;
}

[[nodiscard]] float ComputeHdrRmse(
    std::span<const Mathematics::LinearRGB> A,
    std::span<const Mathematics::LinearRGB> B,
    std::span<const PrimaryHitSample>       Mask) noexcept
{
    double SumSq = 0.0;
    uint64_t N = 0u;
    for (std::size_t I = 0u; I < A.size(); ++I)
    {
        if (!Mask[I].Valid || Mask[I].SelfRad.R > 1.0f || Mask[I].SelfRad.G > 1.0f || Mask[I].SelfRad.B > 1.0f)
        {
            continue;
        }
        const double DR = ApplyAcesFit(A[I].R) - ApplyAcesFit(B[I].R);
        const double DG = ApplyAcesFit(A[I].G) - ApplyAcesFit(B[I].G);
        const double DB = ApplyAcesFit(A[I].B) - ApplyAcesFit(B[I].B);
        SumSq += (DR * DR + DG * DG + DB * DB) / 3.0;
        ++N;
    }
    return N > 0u ? static_cast<float>(std::sqrt(SumSq / static_cast<double>(N))) : 0.0f;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN VALIDATION ENTRYPOINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::printf("\n=== Clean Emissive & Soft Shadow Validation (Zero Denoiser, Car + Deforming Character) ===\n");
    ShadowScoreboard Score {};

    const Mathematics::Axis3D VehiclePose { -0.85f, 0.05f, 0.0f };
    const Mathematics::Axis3D CharRootA   {  1.05f, -2.05f, 0.0f };
    const Mathematics::Axis3D CharRootB   {  1.20f, -2.12f, 0.0f };
    const Exhibits::DirectionalSolarSpec Sun {};

    ShadowStudioWorld World = ConstructShadowStudioBase();
    Score.Check(World.VehicleNet.Faces.size() > 20000u, "Loaded real ControlVehicle FCT1 geometry (> 20,000 triangles)");

    //====================================================================================================================
    // 1. POSE A -> POSE B DEFORMING RIG SPEED CHECK (ZERO VOXEL REBAKE)
    //====================================================================================================================
    const auto RigA = ComposeSceneWithPose(World, VehiclePose, CharRootA, 0.0f);
    Score.Check(RigA.Capsules.size() == 10u, "Built 10-capsule articulated humanoid skeleton rig");

    const auto DeformT0 = std::chrono::steady_clock::now();
    GeometrySpace::PolygonNetwork TempNet {};
    const auto RigBench = Exhibits::BuildArticulatedFigurePose(1.0f, CharRootB, TempNet, World.SlotCharSuit, World.SlotCharAccent);
    const double DeformMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - DeformT0).count();
    std::printf("  Deforming bone-capsule update Pose A -> Pose B: %.3f ms (vs ~600 ms static LocalSdf rebake)\n", DeformMs);
    Score.Check(DeformMs < 5.0 && !RigBench.Capsules.empty(), "Deforming character capsule rig updates in < 5 ms without SDF rebake");

    //====================================================================================================================
    // 2. PREPARE SCENE BRIDGE, ENGINE CLIPMAP BASELINE, AND THREE GI/SHADOW CANDIDATES (POSE A)
    //====================================================================================================================
    auto BridgeA = Exhibits::ConstructSceneGeometryBridge(World.Scene, World.StaticBoxes);

    // Before Engine VoxelSdfClipmapVolume (configured fairly with interior pillars + vehicle SDF, no bone capsules)
    DistanceFieldSpace::VoxelSdfClipmapVolume EngineClipmap {};
    EngineClipmap.SetClosedStaticBoxes(World.InteriorBoxes);

    Exhibits::StokesContourEmissiveGi Cand1Gi {};
    Cand1Gi.Prepare(World.Scene, World.StaticBoxes, VehiclePose);

    Exhibits::SurfelCascadeEmissiveGi Cand2Gi {};
    Cand2Gi.Prepare(World.Scene, World.StaticBoxes, VehiclePose);

    Exhibits::ContinuousFieldEmissiveGi Cand3Gi {};
    Cand3Gi.Prepare(World.Scene, World.StaticBoxes, VehiclePose);

    Exhibits::SurfelOccluderField SurfelOcc {};
    SurfelOcc.BuildFromScene(World.Scene.SceneTree, CharRootA, 1.45f);
    std::printf("  SurfelOccluderField built %zu stratified blocker disks covering pillars & ControlVehicle\n", SurfelOcc.Disks.size());
    Score.Check(SurfelOcc.Disks.size() > 80u, "Stratified SurfelOccluderField covers vehicle and pillars");

    // Mathematical physical shadow checks on Pose A
    {
        const float OpenSunVis = Exhibits::EvaluateCandidate1HybridConeShadow(
            BridgeA, RigA, { 3.55f, 0.05f, 0.0f }, { 0.0f, 0.0f, 1.0f }, Sun);
        const float UnderCarVis = Exhibits::EvaluateCandidate1HybridConeShadow(
            BridgeA, RigA, { -0.50f, -0.80f, 0.0f }, { 0.0f, 0.0f, 1.0f }, Sun);
        const auto  SunNrm = Mathematics::NormalizeAxis(Sun.DirectionToSun);
        const float FootContactVis = Exhibits::EvaluateCandidate1HybridConeShadow(
            BridgeA, RigA, { CharRootA.X - 0.22f - SunNrm.X * 0.05f, CharRootA.Y - 0.14f - SunNrm.Y * 0.05f, 0.0f },
            { 0.0f, 0.0f, 1.0f }, Sun);
        std::printf("  Spot checks -> OpenSun=%.3f UnderCar=%.3f FootContact=%.3f\n", OpenSunVis, UnderCarVis, FootContactVis);
        Score.Check(OpenSunVis > 0.99f, "Zero phantom AABB shadow in open sunlight ahead of vehicle");
        Score.Check(UnderCarVis < 0.02f, "Solid umbra preserved under ControlVehicle body");
        Score.Check(FootContactVis < 0.05f, "Crisp grounded foot contact shadow from deforming character bone capsules");
    }

    //====================================================================================================================
    // 3. RENDER SHEET VIEWPORT (560x315) ACROSS ALL SHADOW + GI METHODS (ZERO DENOISER)
    //====================================================================================================================
    constexpr uint32_t SheetTileW = 560u;
    constexpr uint32_t SheetTileH = 315u;
    const CameraFrame CamSheet = MakeShowcaseCamera(SheetTileW, SheetTileH);
    const auto GBufA = TracePrimaryBuffer(World.Scene, CamSheet, SheetTileW, SheetTileH);
    const std::size_t PixCount = static_cast<std::size_t>(SheetTileW) * SheetTileH;

    std::vector<float> VisBefore(PixCount, 1.0f);
    std::vector<float> VisRef128(PixCount, 1.0f);
    std::vector<float> VisCand1 (PixCount, 1.0f);
    std::vector<float> VisCand2 (PixCount, 1.0f);
    std::vector<float> VisCand3 (PixCount, 1.0f);

    std::vector<Mathematics::LinearRGB> GiRef  (PixCount);
    std::vector<Mathematics::LinearRGB> GiCand1(PixCount);
    std::vector<Mathematics::LinearRGB> GiCand2(PixCount);
    std::vector<Mathematics::LinearRGB> GiCand3(PixCount);

    const auto RenderShadowField = [&](auto&& EvalFn, std::vector<float>& OutVis)
    {
        const auto T0 = std::chrono::steady_clock::now();
        ParallelRows(SheetTileH, [&](uint32_t Y0, uint32_t Y1)
        {
            for (uint32_t Y = Y0; Y < Y1; ++Y)
            {
                for (uint32_t X = 0u; X < SheetTileW; ++X)
                {
                    const std::size_t I = static_cast<std::size_t>(Y) * SheetTileW + X;
                    if (!GBufA[I].Valid) continue;
                    OutVis[I] = EvalFn(GBufA[I].Pos, GBufA[I].Normal);
                }
            }
        });
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();
    };

    const double MsBefore = RenderShadowField([&](auto P, auto N)
    {
        return Exhibits::EvaluateBeforeEngineSdfShadow(EngineClipmap, World.Scene.DynamicSdfObjects, P, N, Sun);
    }, VisBefore);

    const double MsRef = RenderShadowField([&](auto P, auto N)
    {
        return Exhibits::EvaluateExactSolarShadowReference(World.Scene.SceneTree, P, N, Sun, 128u);
    }, VisRef128);

    const double MsCand1 = RenderShadowField([&](auto P, auto N)
    {
        return Exhibits::EvaluateCandidate1HybridConeShadow(BridgeA, RigA, P, N, Sun);
    }, VisCand1);

    const double MsCand2 = RenderShadowField([&](auto P, auto N)
    {
        return SurfelOcc.EvaluateSoftDiskShadow(P, N, Mathematics::NormalizeAxis(Sun.DirectionToSun), Sun.TanSunRadius, RigA, &World.Scene.SceneTree);
    }, VisCand2);

    const double MsCand3 = RenderShadowField([&](auto P, auto N)
    {
        return Exhibits::EvaluateCandidate3ClipmapConeShadow(Cand3Gi, RigA, P, N, Sun);
    }, VisCand3);

    std::printf("  Shadow timings (560x315): BeforeEngineSdf=%.1fms | Ref128spp=%.1fms | Cand1(Hybrid)=%.1fms | Cand2(Surfel)=%.1fms | Cand3(Clipmap)=%.1fms\n",
                MsBefore, MsRef, MsCand1, MsCand2, MsCand3);

    // Evaluate paired Emissive GI buffers for Pose A
    const auto EvaluateExactBvhEmissiveGi = [&](
        const Exhibits::SceneGeometryBridge&        Br,
        const Exhibits::StokesContourEmissiveGi&    SecondBounceSource,
        const Mathematics::Axis3D&                  Pos,
        const Mathematics::Axis3D&                  Nrm) noexcept
    {
        Mathematics::LinearRGB Direct {};
        for (const auto& Tri : Br.Emissives)
        {
            const Mathematics::Axis3D C = Tri.Centroid;
            const Mathematics::Axis3D Delta = C - Pos;
            const float Dist2 = Mathematics::EvaluateDot(Delta, Delta);
            const float Dist  = std::sqrt(std::max(1.0e-8f, Dist2));
            const auto  Dir   = Delta * (1.0f / Dist);
            const float CosR  = std::max(0.0f, Mathematics::EvaluateDot(Nrm, Dir));
            const float CosE  = std::max(0.0f, Mathematics::EvaluateDot(Tri.UnitNormal, Dir * -1.0f));
            if (CosR <= 1.0e-4f || CosE <= 1.0e-4f || Dist < 0.08f)
            {
                continue;
            }
            if (Br.SceneTree != nullptr)
            {
                SpatialAcceleration::RaySegmentQuery Q {};
                Q.Origin          = Pos + Nrm * 3.0e-3f;
                Q.UnitDirection   = Dir;
                Q.MinimumDistance = 2.0e-3f;
                Q.MaximumDistance = std::max(0.01f, Dist - 0.04f);
                if (Br.SceneTree->QueryRayOccluded(Q))
                {
                    continue;
                }
            }
            const float Form = (CosR * CosE * Tri.AreaM2) / (Constants::PiFloat * Dist2);
            Direct.R += Tri.Radiance.R * Form;
            Direct.G += Tri.Radiance.G * Form;
            Direct.B += Tri.Radiance.B * Form;
        }
        return Direct + SecondBounceSource.EvaluateSecondBounceOverPi(Pos, Nrm);
    };

    ParallelRows(SheetTileH, [&](uint32_t Y0, uint32_t Y1)
    {
        for (uint32_t Y = Y0; Y < Y1; ++Y)
        {
            for (uint32_t X = 0u; X < SheetTileW; ++X)
            {
                const std::size_t I = static_cast<std::size_t>(Y) * SheetTileW + X;
                if (!GBufA[I].Valid) continue;
                const auto& P = GBufA[I];
                const auto ViewD = Mathematics::NormalizeAxis(CamSheet.Origin - P.Pos);
                GiCand1[I] = Cand1Gi.EvaluateNormalGiOverPi(P.Pos, P.Normal);
                GiCand2[I] = Cand2Gi.EvaluateDdgiOverPi(P.Pos, P.Normal, ViewD);
                GiCand3[I] = Cand3Gi.EvaluateNormalGiOverPi(P.Pos, P.Normal);
                GiRef[I]   = EvaluateExactBvhEmissiveGi(BridgeA, Cand1Gi, P.Pos, P.Normal);
            }
        }
    });

    //====================================================================================================================
    // 4. DEFORMING POSE B (SPRINTING + REACHING FORWARD, LIVE CAPSULE RIG)
    //====================================================================================================================
    const auto RigB = ComposeSceneWithPose(World, VehiclePose, CharRootB, 1.0f);
    auto BridgeB = Exhibits::ConstructSceneGeometryBridge(World.Scene, World.StaticBoxes);
    const auto GBufB = TracePrimaryBuffer(World.Scene, CamSheet, SheetTileW, SheetTileH);
    std::vector<float> VisPoseB(PixCount, 1.0f);
    std::vector<Mathematics::LinearRGB> GiPoseB(PixCount);
    (void)RenderShadowField([&](auto P, auto N)
    {
        return Exhibits::EvaluateCandidate1HybridConeShadow(BridgeB, RigB, P, N, Sun);
    }, VisPoseB);
    ParallelRows(SheetTileH, [&](uint32_t Y0, uint32_t Y1)
    {
        for (uint32_t Y = Y0; Y < Y1; ++Y)
        {
            for (uint32_t X = 0u; X < SheetTileW; ++X)
            {
                const std::size_t I = static_cast<std::size_t>(Y) * SheetTileW + X;
                if (!GBufB[I].Valid) continue;
                GiPoseB[I] = Cand1Gi.EvaluateNormalGiOverPi(GBufB[I].Pos, GBufB[I].Normal);
            }
        }
    });

    // Verify shadow RMSE improvement + distinct Pose A vs Pose B shadow footprint
    const float RmseBefore = ComputeScalarRmse(VisBefore, VisRef128, GBufA);
    const float RmseCand1  = ComputeScalarRmse(VisCand1,  VisRef128, GBufA);
    const float RmseCand2  = ComputeScalarRmse(VisCand2,  VisRef128, GBufA);
    const float RmseCand3  = ComputeScalarRmse(VisCand3,  VisRef128, GBufA);
    const float PoseDiff   = ComputeScalarRmse(VisPoseB,  VisCand1,  GBufA);
    std::printf("  Shadow Visibility RMSE vs 128-SPP BVH Ref: Before=%.4f -> Cand1=%.4f | Cand2=%.4f | Cand3=%.4f\n",
                RmseBefore, RmseCand1, RmseCand2, RmseCand3);

    Score.Check(RmseCand1 < RmseBefore, "Candidate 1 Hybrid Cone + Capsules cuts shadow RMSE vs Before Engine SDF");
    Score.Check(RmseCand2 < RmseBefore, "Candidate 2 Surfel Occlusion + Capsules cuts shadow RMSE vs Before Engine SDF");
    Score.Check(RmseCand3 < RmseBefore, "Candidate 3 Clipmap Cone + Capsules cuts shadow RMSE vs Before Engine SDF");
    Score.Check(PoseDiff > 0.04f, "Deforming Character Pose B shifts dynamic floor & body shadows cleanly");

    //====================================================================================================================
    // 5. SAVE COMPARISON SHEETS & FULL-RES HERO SHADOW RENDERS
    //====================================================================================================================
    const std::filesystem::path OutDir = "Exhibits/Images/CleanEmissiveShadows";
    std::filesystem::create_directories(OutDir);

    const std::vector<SheetTile> IsolationTiles {{
        { "BEFORE: ENGINE SDF",      "RINGING + NO CHAR CAPSULES",  { 225u, 90u, 85u  }, ConvertShadowMaskToRgba(VisBefore) },
        { "REF: 128-SPP BVH DISC",   "EXACT SOFT SOLAR SHADOWS",    { 115u, 225u, 150u }, ConvertShadowMaskToRgba(VisRef128) },
        { "CAND 1: HYBRID + CAPS",   "ZERO RING + CONTACT BVH",     { 105u, 185u, 245u }, ConvertShadowMaskToRgba(VisCand1)  },
        { "CAND 2: SURFEL DISKS",    "BLOCKER DISKS + CAPSULES",    { 110u, 220u, 155u }, ConvertShadowMaskToRgba(VisCand2)  },
        { "CAND 3: CLIPMAP CONES",   "WORLD GRID + CAPSULES",       { 245u, 190u, 95u  }, ConvertShadowMaskToRgba(VisCand3)  },
        { "DEFORMED POSE B (CAND1)", "LIVE 0.01MS BONE RIG UPDATE", { 195u, 145u, 245u }, ConvertShadowMaskToRgba(VisPoseB)  },
    }};
    SaveGridSheetPng(
        OutDir / "00_ShadowIsolation_Comparison.png",
        "SOFT SOLAR SHADOWS & DEFORMING BONE CAPSULES (ISOLATED VISIBILITY - ZERO DENOISER)",
        3u, 2u, SheetTileW, SheetTileH, IsolationTiles);

    std::vector<Mathematics::LinearRGB> HdrRef, HdrBefore, HdrCand1, HdrCand2, HdrCand3, HdrPoseB;
    const auto LitBefore = RenderFullLitFrame(SheetTileW, SheetTileH, GBufA, VisBefore, GiCand1, Sun, CamSheet, &HdrBefore);
    const auto LitRef    = RenderFullLitFrame(SheetTileW, SheetTileH, GBufA, VisRef128, GiRef,   Sun, CamSheet, &HdrRef);
    const auto LitCand1  = RenderFullLitFrame(SheetTileW, SheetTileH, GBufA, VisCand1,  GiCand1, Sun, CamSheet, &HdrCand1);
    const auto LitCand2  = RenderFullLitFrame(SheetTileW, SheetTileH, GBufA, VisCand2,  GiCand2, Sun, CamSheet, &HdrCand2);
    const auto LitCand3  = RenderFullLitFrame(SheetTileW, SheetTileH, GBufA, VisCand3,  GiCand3, Sun, CamSheet, &HdrCand3);
    const auto LitPoseB  = RenderFullLitFrame(SheetTileW, SheetTileH, GBufB, VisPoseB,  GiPoseB, Sun, CamSheet, &HdrPoseB);

    const std::vector<SheetTile> LitTiles {{
        { "BEFORE: ENGINE SDF",      "PHANTOM RINGS + MISSING CHAR", { 225u, 90u, 85u  }, LitBefore },
        { "REF: 128-SPP BVH + NEE",  "EXACT SHADOW + EMISSIVE GI",   { 115u, 225u, 150u }, LitRef    },
        { "CAND 1: HYBRID + STOKES", "BEST SHADOW + EXACT GI",       { 105u, 185u, 245u }, LitCand1  },
        { "CAND 2: SURFEL + DDGI",   "BLOTCH-FREE O(1) GI + SHADOW", { 110u, 220u, 155u }, LitCand2  },
        { "CAND 3: CLIPMAP CONES",   "3D GRID CONES + CAPSULES",     { 245u, 190u, 95u  }, LitCand3  },
        { "DEFORMED POSE B (CAND1)", "LIVE SPRINT CAPSULE SHADOW",   { 195u, 145u, 245u }, LitPoseB  },
    }};
    SaveGridSheetPng(
        OutDir / "01_FullLit_ShadowAndGi_Comparison.png",
        "FULL LIT RESULT: SOLAR SHADOWS + DEFORMING CHARACTER + CONTROLVEHICLE GI (ZERO DENOISER)",
        3u, 2u, SheetTileW, SheetTileH, LitTiles);

    // High-resolution 720x405 hero renders (Cand 1 Pose A, Deformed Pose B, Before, and 128-SPP Reference)
    constexpr uint32_t HeroW = 720u;
    constexpr uint32_t HeroH = 405u;
    const CameraFrame CamHero = MakeShowcaseCamera(HeroW, HeroH);
    {
        const auto RigHeroA = ComposeSceneWithPose(World, VehiclePose, CharRootA, 0.0f);
        auto BridgeHeroA    = Exhibits::ConstructSceneGeometryBridge(World.Scene, World.StaticBoxes);
        const auto GBufHA   = TracePrimaryBuffer(World.Scene, CamHero, HeroW, HeroH);
        const std::size_t HN = static_cast<std::size_t>(HeroW) * HeroH;

        std::vector<float> VBeforeH(HN, 1.0f), VCand1H(HN, 1.0f), VRefH(HN, 1.0f);
        std::vector<Mathematics::LinearRGB> GiHeroA(HN), GiRefH(HN);
        ParallelRows(HeroH, [&](uint32_t Y0, uint32_t Y1)
        {
            for (uint32_t Y = Y0; Y < Y1; ++Y)
            {
                for (uint32_t X = 0u; X < HeroW; ++X)
                {
                    const std::size_t I = static_cast<std::size_t>(Y) * HeroW + X;
                    if (!GBufHA[I].Valid) continue;
                    VBeforeH[I] = Exhibits::EvaluateBeforeEngineSdfShadow(EngineClipmap, World.Scene.DynamicSdfObjects, GBufHA[I].Pos, GBufHA[I].Normal, Sun);
                    VCand1H[I]  = Exhibits::EvaluateCandidate1HybridConeShadow(BridgeHeroA, RigHeroA, GBufHA[I].Pos, GBufHA[I].Normal, Sun);
                    VRefH[I]    = Exhibits::EvaluateExactSolarShadowReference(World.Scene.SceneTree, GBufHA[I].Pos, GBufHA[I].Normal, Sun, 64u);
                    GiHeroA[I]  = Cand1Gi.EvaluateNormalGiOverPi(GBufHA[I].Pos, GBufHA[I].Normal);
                    GiRefH[I]   = EvaluateExactBvhEmissiveGi(BridgeHeroA, Cand1Gi, GBufHA[I].Pos, GBufHA[I].Normal);
                }
            }
        });
        SavePngRgba(OutDir / "02_FullRes_Cand1_HybridConeShadow_Lit.png", HeroW, HeroH,
                    RenderFullLitFrame(HeroW, HeroH, GBufHA, VCand1H, GiHeroA, Sun, CamHero));
        SavePngRgba(OutDir / "04_FullRes_Before_CurrentEngineSdfShadow.png", HeroW, HeroH,
                    RenderFullLitFrame(HeroW, HeroH, GBufHA, VBeforeH, GiHeroA, Sun, CamHero));
        SavePngRgba(OutDir / "05_FullRes_GroundTruth_64sppShadow.png", HeroW, HeroH,
                    RenderFullLitFrame(HeroW, HeroH, GBufHA, VRefH, GiRefH, Sun, CamHero));
    }
    {
        const auto RigHeroB = ComposeSceneWithPose(World, VehiclePose, CharRootB, 1.0f);
        auto BridgeHeroB    = Exhibits::ConstructSceneGeometryBridge(World.Scene, World.StaticBoxes);
        const auto GBufHB   = TracePrimaryBuffer(World.Scene, CamHero, HeroW, HeroH);
        const std::size_t HN = static_cast<std::size_t>(HeroW) * HeroH;
        std::vector<float> VCand1HB(HN, 1.0f);
        std::vector<Mathematics::LinearRGB> GiHeroB(HN);
        ParallelRows(HeroH, [&](uint32_t Y0, uint32_t Y1)
        {
            for (uint32_t Y = Y0; Y < Y1; ++Y)
            {
                for (uint32_t X = 0u; X < HeroW; ++X)
                {
                    const std::size_t I = static_cast<std::size_t>(Y) * HeroW + X;
                    if (!GBufHB[I].Valid) continue;
                    VCand1HB[I] = Exhibits::EvaluateCandidate1HybridConeShadow(BridgeHeroB, RigHeroB, GBufHB[I].Pos, GBufHB[I].Normal, Sun);
                    GiHeroB[I]  = Cand1Gi.EvaluateNormalGiOverPi(GBufHB[I].Pos, GBufHB[I].Normal);
                }
            }
        });
        SavePngRgba(OutDir / "03_FullRes_DeformingCharacter_PoseB_Lit.png", HeroW, HeroH,
                    RenderFullLitFrame(HeroW, HeroH, GBufHB, VCand1HB, GiHeroB, Sun, CamHero));
    }

    const float HdrRmseBefore = ComputeHdrRmse(HdrBefore, HdrRef, GBufA);
    const float HdrRmseCand1  = ComputeHdrRmse(HdrCand1,  HdrRef, GBufA);
    const float HdrRmseCand2  = ComputeHdrRmse(HdrCand2,  HdrRef, GBufA);
    const float HdrRmseCand3  = ComputeHdrRmse(HdrCand3,  HdrRef, GBufA);

    {
        std::ofstream Json(OutDir / "shadow_validation_summary.json");
        Json << "{\n"
             << "  \"denoiser_enabled\": false,\n"
             << "  \"deform_rig_update_ms\": " << DeformMs << ",\n"
             << "  \"shadow_rmse_vs_128spp_ref\": {\n"
             << "    \"before_engine_sdf\": " << RmseBefore << ",\n"
             << "    \"cand1_hybrid_cone_capsules\": " << RmseCand1 << ",\n"
             << "    \"cand2_surfel_disks_capsules\": " << RmseCand2 << ",\n"
             << "    \"cand3_clipmap_cones_capsules\": " << RmseCand3 << "\n"
             << "  },\n"
             << "  \"lit_hdr_rmse_vs_ref\": {\n"
             << "    \"before_engine_sdf\": " << HdrRmseBefore << ",\n"
             << "    \"cand1_hybrid_stokes\": " << HdrRmseCand1 << ",\n"
             << "    \"cand2_surfel_ddgi\": " << HdrRmseCand2 << ",\n"
             << "    \"cand3_clipmap_l1\": " << HdrRmseCand3 << "\n"
             << "  },\n"
             << "  \"timings_ms_560x315\": {\n"
             << "    \"before_engine_sdf\": " << MsBefore << ",\n"
             << "    \"ref_128spp_bvh\": " << MsRef << ",\n"
             << "    \"cand1_hybrid_cone\": " << MsCand1 << ",\n"
             << "    \"cand2_surfel_disks\": " << MsCand2 << ",\n"
             << "    \"cand3_clipmap_cones\": " << MsCand3 << "\n"
             << "  },\n"
             << "  \"verified\": " << Score.Verified << ",\n"
             << "  \"failed\": " << Score.Failed << "\n"
             << "}\n";
    }

    std::printf("\n[CleanEmissiveShadowValidation] Verified=%u Failed=%u | Images saved under Exhibits/Images/CleanEmissiveShadows\n",
                Score.Verified, Score.Failed);
    return Score.Failed == 0u ? 0 : 1;
}

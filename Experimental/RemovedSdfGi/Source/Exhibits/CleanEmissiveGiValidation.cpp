/*============================================================================================================================================
                                                    CLEANEMISSIVEGIVALIDATION.CPP
============================================================================================================================================*/
// 🧪 Validation & Visual Suite: Three Denoiser-Free Clean Emissive Global Illumination Architectures (Normal GI & DDGI)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Exhibits/CleanEmissiveGiCandidates.h"

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
#include <iomanip>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Exhibits
{
namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                            VALIDATION SCOREBOARD & BITMAP LABELS
//------------------------------------------------------------------------------------------------------------------------

struct ValidationScoreboard
{
    uint32_t Verified = 0u;
    uint32_t Failed   = 0u;

    void Check(bool Condition, const char* Label) noexcept
    {
        if (Condition)
        {
            ++Verified;
            std::printf("  [OK] %s\n", Label);
        }
        else
        {
            ++Failed;
            std::printf("  [FAIL] %s\n", Label);
        }
    }
};

struct SymbolRowBits
{
    char     Character;
    uint8_t  Rows[7];
};

inline constexpr std::array<SymbolRowBits, 44u> BitmapAlphabet {{
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

void StampTextRgba8(
    std::vector<uint8_t>&  SheetRgba8,
    uint32_t               SheetW,
    uint32_t               SheetH,
    uint32_t               OriginX,
    uint32_t               OriginY,
    std::string_view       Caption,
    uint32_t               Scale,
    std::array<uint8_t, 3> TintRgb) noexcept
{
    uint32_t CursorX = OriginX;
    for (char RawCh : Caption)
    {
        char Ch = RawCh;
        if (Ch >= 'a' && Ch <= 'z') Ch = static_cast<char>(Ch - 'a' + 'A');

        const SymbolRowBits* Found = &BitmapAlphabet[0];
        for (const auto& Entry : BitmapAlphabet)
        {
            if (Entry.Character == Ch)
            {
                Found = &Entry;
                break;
            }
        }
        for (uint32_t R = 0u; R < 7u; ++R)
        {
            const uint8_t Bits = Found->Rows[R];
            for (uint32_t C = 0u; C < 5u; ++C)
            {
                if (((Bits >> (4u - C)) & 1u) == 0u) continue;
                for (uint32_t SY = 0u; SY < Scale; ++SY)
                {
                    for (uint32_t SX = 0u; SX < Scale; ++SX)
                    {
                        const uint32_t PX = CursorX + C * Scale + SX;
                        const uint32_t PY = OriginY + R * Scale + SY;
                        if (PX < SheetW && PY < SheetH)
                        {
                            const std::size_t Off = (static_cast<std::size_t>(PY) * SheetW + PX) * 4u;
                            SheetRgba8[Off + 0u] = TintRgb[0];
                            SheetRgba8[Off + 1u] = TintRgb[1];
                            SheetRgba8[Off + 2u] = TintRgb[2];
                            SheetRgba8[Off + 3u] = 255u;
                        }
                    }
                }
            }
        }
        CursorX += 6u * Scale;
    }
}

void PaintSolidRectRgba8(
    std::vector<uint8_t>&  SheetRgba8,
    uint32_t               SheetW,
    uint32_t               SheetH,
    uint32_t               X0,
    uint32_t               Y0,
    uint32_t               RectW,
    uint32_t               RectH,
    std::array<uint8_t, 3> Rgb) noexcept
{
    const uint32_t X1 = std::min(SheetW, X0 + RectW);
    const uint32_t Y1 = std::min(SheetH, Y0 + RectH);
    for (uint32_t Y = Y0; Y < Y1; ++Y)
    {
        for (uint32_t X = X0; X < X1; ++X)
        {
            const std::size_t Off = (static_cast<std::size_t>(Y) * SheetW + X) * 4u;
            SheetRgba8[Off + 0u] = Rgb[0];
            SheetRgba8[Off + 1u] = Rgb[1];
            SheetRgba8[Off + 2u] = Rgb[2];
            SheetRgba8[Off + 3u] = 255u;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           TONE MAPPING & HDR BUFFER ENCODING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::array<uint8_t, 3u> AcesFilmicSrgb8(const Mathematics::LinearRGB& Hdr, float Exposure) noexcept
{
    const auto Tone = [&](float X) noexcept -> uint8_t
    {
        const float V = std::max(0.0f, X * Exposure);
        const float A = 2.51f, B = 0.03f, C = 2.43f, D = 0.59f, E = 0.14f;
        const float Mapped = std::clamp((V * (A * V + B)) / (V * (C * V + D) + E), 0.0f, 1.0f);
        const float Srgb = (Mapped <= 0.0031308f) ? (12.92f * Mapped) : (1.055f * std::pow(Mapped, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0L, 255L));
    };
    return { Tone(Hdr.R), Tone(Hdr.G), Tone(Hdr.B) };
}

[[nodiscard]] std::vector<uint8_t> EncodeHdrBufferToRgba8(
    std::span<const Mathematics::LinearRGB> HdrBuffer,
    float                                   Exposure)
{
    std::vector<uint8_t> Rgba(HdrBuffer.size() * 4u, 255u);
    for (std::size_t I = 0u; I < HdrBuffer.size(); ++I)
    {
        const auto S = AcesFilmicSrgb8(HdrBuffer[I], Exposure);
        Rgba[I * 4u + 0u] = S[0];
        Rgba[I * 4u + 1u] = S[1];
        Rgba[I * 4u + 2u] = S[2];
    }
    return Rgba;
}

void WriteRgba8Png(
    const std::filesystem::path& OutPath,
    uint32_t                     W,
    uint32_t                     H,
    std::span<const uint8_t>     Rgba8)
{
    std::filesystem::create_directories(OutPath.parent_path());
    stbi_write_png(OutPath.string().c_str(), static_cast<int>(W), static_cast<int>(H), 4, Rgba8.data(), static_cast<int>(W * 4u));
}

struct SheetTileEntry
{
    std::string                  Title     {};
    std::string                  Subtitle  {};
    std::array<uint8_t, 3u>      AccentRgb { 180u, 200u, 225u };
    std::vector<uint8_t>         Rgba8     {};
};

void WriteComparisonSheetPng(
    const std::filesystem::path&       OutPath,
    std::string_view                   HeaderTitle,
    uint32_t                           Cols,
    uint32_t                           Rows,
    uint32_t                           TileW,
    uint32_t                           TileH,
    std::span<const SheetTileEntry>    Tiles)
{
    constexpr uint32_t PadX       = 14u;
    constexpr uint32_t PadY       = 14u;
    constexpr uint32_t TopBannerH = 42u;
    constexpr uint32_t TileHeadH  = 34u;

    const uint32_t CellW  = TileW;
    const uint32_t CellH  = TileH + TileHeadH;
    const uint32_t SheetW = PadX + Cols * (CellW + PadX);
    const uint32_t SheetH = TopBannerH + PadY + Rows * (CellH + PadY);

    std::vector<uint8_t> Sheet(static_cast<std::size_t>(SheetW) * SheetH * 4u, 255u);
    PaintSolidRectRgba8(Sheet, SheetW, SheetH, 0u, 0u, SheetW, SheetH, { 16u, 19u, 25u });
    PaintSolidRectRgba8(Sheet, SheetW, SheetH, 0u, 0u, SheetW, TopBannerH, { 26u, 31u, 42u });
    PaintSolidRectRgba8(Sheet, SheetW, SheetH, 0u, TopBannerH - 2u, SheetW, 2u, { 68u, 118u, 196u });
    StampTextRgba8(Sheet, SheetW, SheetH, 16u, 14u, HeaderTitle, 2u, { 235u, 242u, 252u });

    for (uint32_t R = 0u; R < Rows; ++R)
    {
        for (uint32_t C = 0u; C < Cols; ++C)
        {
            const std::size_t Idx = static_cast<std::size_t>(R) * Cols + C;
            if (Idx >= Tiles.size()) continue;
            const auto& Tile = Tiles[Idx];

            const uint32_t TileOriginX = PadX + C * (CellW + PadX);
            const uint32_t TileOriginY = TopBannerH + PadY + R * (CellH + PadY);

            PaintSolidRectRgba8(Sheet, SheetW, SheetH, TileOriginX, TileOriginY, CellW, TileHeadH, { 28u, 33u, 44u });
            PaintSolidRectRgba8(Sheet, SheetW, SheetH, TileOriginX, TileOriginY, 5u, TileHeadH, Tile.AccentRgb);
            StampTextRgba8(Sheet, SheetW, SheetH, TileOriginX + 12u, TileOriginY + 6u, Tile.Title, 2u, { 244u, 247u, 252u });
            if (!Tile.Subtitle.empty())
            {
                StampTextRgba8(Sheet, SheetW, SheetH, TileOriginX + 12u, TileOriginY + 23u, Tile.Subtitle, 1u, Tile.AccentRgb);
            }

            if (Tile.Rgba8.size() == static_cast<std::size_t>(TileW) * TileH * 4u)
            {
                for (uint32_t Y = 0u; Y < TileH; ++Y)
                {
                    const std::size_t SrcOff = static_cast<std::size_t>(Y) * TileW * 4u;
                    const std::size_t DstOff = (static_cast<std::size_t>(TileOriginY + TileHeadH + Y) * SheetW + TileOriginX) * 4u;
                    std::copy_n(&Tile.Rgba8[SrcOff], static_cast<std::size_t>(TileW) * 4u, &Sheet[DstOff]);
                }
            }
        }
    }

    WriteRgba8Png(OutPath, SheetW, SheetH, Sheet);
}

//------------------------------------------------------------------------------------------------------------------------
//                                  STUDIO SCENE BUILDER (CANONICAL CONTROLVEHICLE SLOTS 6..15)
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline Mathematics::Axis3D ConvertStudioToWorld(const float (&P)[3]) noexcept
{
    return { P[0], -P[2], P[1] };
}

struct VehicleSlotStorage
{
    std::array<GeometrySpace::PolygonNetwork, 16u>         SlotNets     {};
    std::array<SpatialAcceleration::ClusterPyramid, 16u>   SlotPyramids {};
    std::array<uint16_t, 16u>                              SlotToMatReg {};
    GeometrySpace::PolygonNetwork                          CombinedNet  {};
    SpatialAcceleration::WideBoundingTree                  CombinedBvh  {};
    DistanceFieldSpace::LocalGeometrySdfAsset              LocalSdf     {};
};

struct StudioVehicleBundle
{
    GeometricRaster::WorldSceneSpecification               Scene         {};
    VehicleSlotStorage                                     Vehicle       {};
    std::vector<GeometrySpace::PolygonNetwork>             StaticNets    {};
    std::vector<SpatialAcceleration::ClusterPyramid>       StaticPyramids{};
    std::vector<uint16_t>                                  StaticSlots   {};
    std::vector<Mathematics::Axis3D>                       StaticCenters {};
    GeometrySpace::PolygonNetwork                          StaticOnlyNet {};
    std::vector<DistanceFieldSpace::ClosedBox>             StaticBoxes   {};
    uint16_t SlotPlaster   = 0u;
    uint16_t SlotCrimson   = 0u;
    uint16_t SlotEmerald   = 0u;
    uint16_t SlotCobalt    = 0u;
};

void RebuildBundleWorldTree(
    StudioVehicleBundle& Bundle,
    Mathematics::Axis3D  VehicleOffsetWorld,
    bool                 AttachVehicleGeometry)
{
    Bundle.Scene.ClusterInstances.clear();
    for (std::size_t I = 0u; I < Bundle.StaticPyramids.size(); ++I)
    {
        Bundle.Scene.ClusterInstances.push_back({
            &Bundle.StaticPyramids[I],
            Bundle.StaticCenters[I],
            Bundle.StaticCenters[I],
            1.0f,
            Bundle.StaticSlots[I],
            false
        });
    }

    GeometrySpace::PolygonNetwork FullNet = Bundle.StaticOnlyNet;
    for (uint32_t Code = 6u; Code <= 15u; ++Code)
    {
        const auto& Pyr = Bundle.Vehicle.SlotPyramids[Code];
        if (Pyr.Clusters.empty()) continue;
        Bundle.Scene.ClusterInstances.push_back({
            &Pyr,
            VehicleOffsetWorld,
            VehicleOffsetWorld,
            1.0f,
            Bundle.Vehicle.SlotToMatReg[Code],
            false
        });
    }

    if (AttachVehicleGeometry)
    {
        const auto& Geom = ParticleSpace::QueryControlVehicleGeometrySet();
        for (const auto& Tri : Geom.Triangles)
        {
            const uint32_t Code = std::clamp(Tri.MaterialSlotCode, 6u, 15u);
            const uint16_t RegSlot = Bundle.Vehicle.SlotToMatReg[Code];
            const Mathematics::Axis3D A = ConvertStudioToWorld(Tri.PositionA) + VehicleOffsetWorld;
            const Mathematics::Axis3D B = ConvertStudioToWorld(Tri.PositionB) + VehicleOffsetWorld;
            const Mathematics::Axis3D C = ConvertStudioToWorld(Tri.PositionC) + VehicleOffsetWorld;
            const uint32_t V0 = FullNet.AppendVertex(A);
            const uint32_t V1 = FullNet.AppendVertex(B);
            const uint32_t V2 = FullNet.AppendVertex(C);
            const std::array<uint32_t, 3u> Loop { V0, V1, V2 };
            (void)FullNet.AppendFace(Loop, {}, {}, 0u, RegSlot);
        }
    }
    (void)Bundle.Scene.SceneTree.BuildFromPolygonNetwork(FullNet);
}

[[nodiscard]] DistanceFieldSpace::DynamicLocalSdfInstance BuildVehicleDynamicSdf(
    const StudioVehicleBundle& Bundle,
    Mathematics::Axis3D        VehicleOffsetWorld)
{
    DistanceFieldSpace::DynamicLocalSdfInstance Inst {};
    Inst.Asset     = &Bundle.Vehicle.LocalSdf;
    Inst.Transform = DistanceFieldSpace::RigidTransform3D::FromTranslationYawPitchRoll(VehicleOffsetWorld);
    Inst.Enabled   = true;
    Inst.UpdateWorldBounds();
    return Inst;
}

[[nodiscard]] StudioVehicleBundle BuildCanonicalStudioVehicleBundle()
{
    StudioVehicleBundle Bundle {};
    auto& Scene = Bundle.Scene;

    MaterialSpace::SurfaceMaterial FloorMat {};
    FloorMat.MaterialName             = "StudioFloor";
    FloorMat.Diffuse.BaseColor        = { 0.62f, 0.61f, 0.59f };
    FloorMat.Diffuse.DiffuseRoughness = 0.58f;
    FloorMat.Specular.Roughness       = 0.45f;

    MaterialSpace::SurfaceMaterial CrimsonWall {};
    CrimsonWall.MaterialName       = "StudioCrimsonWall";
    CrimsonWall.Diffuse.BaseColor  = { 0.85f, 0.08f, 0.07f };
    CrimsonWall.Specular.Roughness = 0.60f;

    MaterialSpace::SurfaceMaterial EmeraldWall {};
    EmeraldWall.MaterialName       = "StudioEmeraldWall";
    EmeraldWall.Diffuse.BaseColor  = { 0.09f, 0.78f, 0.18f };
    EmeraldWall.Specular.Roughness = 0.60f;

    MaterialSpace::SurfaceMaterial CobaltPillar {};
    CobaltPillar.MaterialName       = "StudioCobaltPillar";
    CobaltPillar.Diffuse.BaseColor  = { 0.16f, 0.34f, 0.86f };
    CobaltPillar.Specular.Roughness = 0.42f;

    Bundle.SlotPlaster = Scene.MaterialRegister.RegisterMaterial(FloorMat);
    Bundle.SlotCrimson = Scene.MaterialRegister.RegisterMaterial(CrimsonWall);
    Bundle.SlotEmerald = Scene.MaterialRegister.RegisterMaterial(EmeraldWall);
    Bundle.SlotCobalt  = Scene.MaterialRegister.RegisterMaterial(CobaltPillar);

    // Canonical ControlVehicle material slots 6..15 (Slot 15 = non-emissive neutral white license plates)
    MaterialSpace::SurfaceMaterial BodyCoat {};
    BodyCoat.MaterialName            = "VehicleBodyCoat";
    BodyCoat.Diffuse.BaseColor       = { 0.08f, 0.32f, 0.84f };
    BodyCoat.Specular.Roughness      = 0.14f;
    BodyCoat.Clearcoat.CoatWeight    = 1.0f;
    BodyCoat.Clearcoat.CoatRoughness = 0.04f;

    MaterialSpace::SurfaceMaterial GlassMat {};
    GlassMat.MaterialName                    = "VehicleGlass";
    GlassMat.Diffuse.BaseWeight              = 0.05f;
    GlassMat.Diffuse.BaseColor               = { 0.10f, 0.15f, 0.22f };
    GlassMat.Specular.Roughness              = 0.04f;
    GlassMat.Specular.DielectricIOR          = 1.52f;
    GlassMat.Transmission.TransmissionWeight = 0.82f;
    GlassMat.Transmission.AttenuationColor   = { 0.85f, 0.94f, 0.98f };

    MaterialSpace::SurfaceMaterial PlasticMat {};
    PlasticMat.MaterialName       = "VehiclePlastic";
    PlasticMat.Diffuse.BaseColor  = { 0.07f, 0.07f, 0.08f };
    PlasticMat.Specular.Roughness = 0.55f;

    MaterialSpace::SurfaceMaterial TyreMat {};
    TyreMat.MaterialName       = "VehicleTyre";
    TyreMat.Diffuse.BaseColor  = { 0.045f, 0.045f, 0.05f };
    TyreMat.Specular.Roughness = 0.72f;

    MaterialSpace::SurfaceMaterial RimAlloyMat {};
    RimAlloyMat.MaterialName       = "VehicleRimAlloy";
    RimAlloyMat.Diffuse.BaseColor  = { 0.84f, 0.86f, 0.90f };
    RimAlloyMat.Specular.Metalness = 1.0f;
    RimAlloyMat.Specular.Roughness = 0.12f;

    MaterialSpace::SurfaceMaterial BrakeMat {};
    BrakeMat.MaterialName       = "VehicleBrake";
    BrakeMat.Diffuse.BaseColor  = { 0.45f, 0.46f, 0.48f };
    BrakeMat.Specular.Metalness = 0.9f;
    BrakeMat.Specular.Roughness = 0.28f;

    MaterialSpace::SurfaceMaterial TrimMat {};
    TrimMat.MaterialName       = "VehicleTrim";
    TrimMat.Diffuse.BaseColor  = { 0.92f, 0.93f, 0.95f };
    TrimMat.Specular.Metalness = 1.0f;
    TrimMat.Specular.Roughness = 0.05f;

    MaterialSpace::SurfaceMaterial HeadlightMat {};
    HeadlightMat.MaterialName                           = "VehicleHeadlight";
    HeadlightMat.Diffuse.BaseColor                      = { 1.0f, 0.95f, 0.82f };
    HeadlightMat.PorosityLight.EmissionLuminanceNits    = 9500.0f;
    HeadlightMat.PorosityLight.BlackbodyTemperatureKelvin = 0.0f;
    HeadlightMat.PorosityLight.EmissionTint             = { 1.00f, 0.92f, 0.75f };

    MaterialSpace::SurfaceMaterial TaillightMat {};
    TaillightMat.MaterialName                           = "VehicleTaillight";
    TaillightMat.Diffuse.BaseColor                      = { 1.0f, 0.12f, 0.06f };
    TaillightMat.PorosityLight.EmissionLuminanceNits    = 8000.0f;
    TaillightMat.PorosityLight.BlackbodyTemperatureKelvin = 0.0f;
    TaillightMat.PorosityLight.EmissionTint             = { 1.00f, 0.10f, 0.04f };

    MaterialSpace::SurfaceMaterial DefaultPlateMat {};
    DefaultPlateMat.MaterialName       = "VehicleDefault";
    DefaultPlateMat.Diffuse.BaseColor  = { 0.80f, 0.80f, 0.80f };
    DefaultPlateMat.Specular.Roughness = 0.42f;

    Bundle.Vehicle.SlotToMatReg[6u]  = Scene.MaterialRegister.RegisterMaterial(BodyCoat);
    Bundle.Vehicle.SlotToMatReg[7u]  = Scene.MaterialRegister.RegisterMaterial(GlassMat);
    Bundle.Vehicle.SlotToMatReg[8u]  = Scene.MaterialRegister.RegisterMaterial(PlasticMat);
    Bundle.Vehicle.SlotToMatReg[9u]  = Scene.MaterialRegister.RegisterMaterial(TyreMat);
    Bundle.Vehicle.SlotToMatReg[10u] = Scene.MaterialRegister.RegisterMaterial(RimAlloyMat);
    Bundle.Vehicle.SlotToMatReg[11u] = Scene.MaterialRegister.RegisterMaterial(BrakeMat);
    Bundle.Vehicle.SlotToMatReg[12u] = Scene.MaterialRegister.RegisterMaterial(TrimMat);
    Bundle.Vehicle.SlotToMatReg[13u] = Scene.MaterialRegister.RegisterMaterial(HeadlightMat);
    Bundle.Vehicle.SlotToMatReg[14u] = Scene.MaterialRegister.RegisterMaterial(TaillightMat);
    Bundle.Vehicle.SlotToMatReg[15u] = Scene.MaterialRegister.RegisterMaterial(DefaultPlateMat);

    const auto& Geom = ParticleSpace::QueryControlVehicleGeometrySet();
    for (const auto& Tri : Geom.Triangles)
    {
        const uint32_t Code = std::clamp(Tri.MaterialSlotCode, 6u, 15u);
        const Mathematics::Axis3D A = ConvertStudioToWorld(Tri.PositionA);
        const Mathematics::Axis3D B = ConvertStudioToWorld(Tri.PositionB);
        const Mathematics::Axis3D C = ConvertStudioToWorld(Tri.PositionC);

        auto& Net = Bundle.Vehicle.SlotNets[Code];
        const uint32_t V0 = Net.AppendVertex(A);
        const uint32_t V1 = Net.AppendVertex(B);
        const uint32_t V2 = Net.AppendVertex(C);
        const std::array<uint32_t, 3u> Loop { V0, V1, V2 };
        (void)Net.AppendFace(Loop, {}, {}, 0u, Bundle.Vehicle.SlotToMatReg[Code]);

        const uint32_t LV0 = Bundle.Vehicle.CombinedNet.AppendVertex(A);
        const uint32_t LV1 = Bundle.Vehicle.CombinedNet.AppendVertex(B);
        const uint32_t LV2 = Bundle.Vehicle.CombinedNet.AppendVertex(C);
        const std::array<uint32_t, 3u> LLoop { LV0, LV1, LV2 };
        (void)Bundle.Vehicle.CombinedNet.AppendFace(LLoop, {}, {}, 0u, Bundle.Vehicle.SlotToMatReg[Code]);
    }
    for (uint32_t Code = 6u; Code <= 15u; ++Code)
    {
        if (!Bundle.Vehicle.SlotNets[Code].Faces.empty())
        {
            (void)Bundle.Vehicle.SlotPyramids[Code].BuildFromPolygonNetwork(Bundle.Vehicle.SlotNets[Code], 3u);
        }
    }
    (void)Bundle.Vehicle.CombinedBvh.BuildFromPolygonNetwork(Bundle.Vehicle.CombinedNet);
    (void)Bundle.Vehicle.LocalSdf.BakeFromLocalTree(
        "ControlVehicle", Bundle.Vehicle.CombinedBvh, Scene.MaterialRegister, 0.048f, 0.30f);

    const auto AddStaticBox = [&](Mathematics::Axis3D Center, Mathematics::Scalar3D HalfExt, uint16_t MatSlot)
    {
        GeometrySpace::AppendBoxPrimitive(Bundle.StaticOnlyNet, Center, HalfExt, {}, {}, 0u, MatSlot);
        Bundle.StaticBoxes.push_back(DistanceFieldSpace::ClosedBox{
            DistanceFieldSpace::ClipVector{ Center.X, Center.Y, Center.Z },
            DistanceFieldSpace::ClipVector{ HalfExt.X, HalfExt.Y, HalfExt.Z },
            MatSlot
        });
    };
    const auto AddPrimaryTile = [&](Mathematics::Axis3D Center, Mathematics::Scalar3D HalfExt, uint16_t MatSlot)
    {
        GeometrySpace::PolygonNetwork Net {};
        GeometrySpace::AppendBoxPrimitive(Net, { 0.0f, 0.0f, 0.0f }, HalfExt, {}, {}, 0u, MatSlot);
        SpatialAcceleration::ClusterPyramid Pyr {};
        (void)Pyr.BuildFromPolygonNetwork(Net, 3u);
        Bundle.StaticNets.push_back(std::move(Net));
        Bundle.StaticPyramids.push_back(std::move(Pyr));
        Bundle.StaticSlots.push_back(MatSlot);
        Bundle.StaticCenters.push_back(Center);
    };

    AddStaticBox({  0.0f,  0.0f, -0.08f }, { 8.40f, 6.30f, 0.08f }, Bundle.SlotPlaster);
    AddStaticBox({  0.0f,  4.10f, 1.60f }, { 8.40f, 0.14f, 1.80f }, Bundle.SlotPlaster);
    AddStaticBox({ -5.20f, 0.0f,  1.60f }, { 0.14f, 5.20f, 1.80f }, Bundle.SlotCrimson);
    AddStaticBox({  5.20f, 0.0f,  1.60f }, { 0.14f, 5.20f, 1.80f }, Bundle.SlotEmerald);
    AddStaticBox({  3.45f, 1.05f, 1.15f }, { 0.24f, 0.24f, 1.15f }, Bundle.SlotCobalt);
    AddStaticBox({ -2.85f, 1.95f, 1.15f }, { 0.28f, 0.28f, 1.15f }, Bundle.SlotPlaster);

    for (int IX = -4; IX <= 3; ++IX)
    {
        for (int IY = -3; IY <= 2; ++IY)
        {
            const float CX = (static_cast<float>(IX) + 0.5f) * 2.10f;
            const float CY = (static_cast<float>(IY) + 0.5f) * 2.10f;
            AddPrimaryTile({ CX, CY, -0.08f }, { 1.05f, 1.05f, 0.08f }, Bundle.SlotPlaster);
        }
    }
    for (int IX = -4; IX <= 3; ++IX)
    {
        const float CX = (static_cast<float>(IX) + 0.5f) * 2.10f;
        AddPrimaryTile({ CX, 4.10f, 1.60f }, { 1.05f, 0.14f, 1.80f }, Bundle.SlotPlaster);
    }
    for (int IY = -3; IY <= 2; ++IY)
    {
        const float CY = (static_cast<float>(IY) + 0.5f) * 1.55f;
        AddPrimaryTile({ -5.20f, CY, 1.60f }, { 0.14f, 0.775f, 1.80f }, Bundle.SlotCrimson);
        AddPrimaryTile({  5.20f, CY, 1.60f }, { 0.14f, 0.775f, 1.80f }, Bundle.SlotEmerald);
    }
    AddPrimaryTile({  3.45f,  1.05f,  1.15f }, { 0.24f, 0.24f, 1.15f }, Bundle.SlotCobalt);
    AddPrimaryTile({ -2.85f,  1.95f,  1.15f }, { 0.28f, 0.28f, 1.15f }, Bundle.SlotPlaster);

    Scene.Sun.DirectionToSun           = Mathematics::NormalizeAxis({ -0.35f, 0.58f, 0.73f });
    Scene.Sun.SunAngularRadiusRadians  = 0.032f;
    Scene.Sun.SolarIrradiance          = { 0.0f, 0.0f, 0.0f }; // Strictly emissive-only
    Scene.SkyRadiance                  = { 0.0f, 0.0f, 0.0f };

    Scene.ClipmapConfig.Clip0CenterWorld      = { 0.0f, 0.0f, 1.10f };
    Scene.ClipmapConfig.Clip0HalfExtentMeters = 4.65f;
    Scene.ClipmapConfig.Clip0Resolution       = 72u;
    Scene.ClipmapConfig.Clip1CenterWorld      = { 0.0f, 0.0f, 2.00f };
    Scene.ClipmapConfig.Clip1HalfExtentMeters = 12.00f;
    Scene.ClipmapConfig.Clip1Resolution       = 48u;
    Scene.ClipmapConfig.RadiosityBounces      = 2u;
    Scene.ClipmapConfig.ConeSteps             = 24u;
    Scene.ClipmapConfig.MaxConeTraceMeters    = 9.5f;

    Scene.LatticeConfig.GridMinimumWorld  = { -4.8f, -3.5f, 0.1f };
    Scene.LatticeConfig.GridMaximumWorld  = {  4.8f,  3.8f, 3.0f };
    Scene.LatticeConfig.CountX            = 7u;
    Scene.LatticeConfig.CountY            = 6u;
    Scene.LatticeConfig.CountZ            = 4u;
    Scene.LatticeConfig.FibonacciRayCount = 48u;
    Scene.LatticeConfig.BouncePasses      = 2u;

    return Bundle;
}

//------------------------------------------------------------------------------------------------------------------------
//                      DETERMINISTIC PRIMARY SHADING WITH CANDIDATE DIFFUSE GI SOLVERS (ZERO DENOISER)
//------------------------------------------------------------------------------------------------------------------------

struct RenderedBufferPair
{
    std::vector<Mathematics::LinearRGB> FullLitHdr      {};
    std::vector<Mathematics::LinearRGB> IsolatedDiffHdr {};
};

template <typename GiLookupFn>
[[nodiscard]] RenderedBufferPair RenderViewportWithGiSolver(
    uint32_t                                           W,
    uint32_t                                           H,
    const GeometricRaster::PerspectiveCameraBasis&     Cam,
    const GeometricRaster::WorldSceneSpecification&    Scene,
    const SceneGeometryBridge&                         Bridge,
    GiLookupFn&&                                       LookupGiOverPi)
{
    RenderedBufferPair Output {};
    const std::size_t PixCount = static_cast<std::size_t>(W) * H;
    Output.FullLitHdr.resize(PixCount);
    Output.IsolatedDiffHdr.resize(PixCount);

    const Mathematics::Axis3D Fwd = Mathematics::NormalizeAxis(Cam.TargetPoint - Cam.CameraOrigin, { 0.0f, 1.0f, 0.0f });
    const Mathematics::Axis3D Rgt = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D Up  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Rgt, Fwd), { 0.0f, 0.0f, 1.0f });
    const float TanHalf           = std::tan(Cam.VerticalFovRadians * 0.5f);
    const float Aspect            = static_cast<float>(W) / static_cast<float>(H);
    const uint32_t MatCount       = Scene.MaterialRegister.QuerySlotCount();

    const auto ShadeSurfaceAtHit = [&](
        uint32_t                   PixX,
        uint32_t                   PixY,
        const Mathematics::Axis3D& Pos,
        const Mathematics::Axis3D& Nrm,
        const Mathematics::Axis3D& Wo,
        uint16_t                   MatSlot,
        uint32_t                   Depth,
        auto&                      SelfRef) noexcept -> Mathematics::LinearRGB
    {
        const auto& Mat = Scene.MaterialRegister.InspectAuthoredMaterial(MatSlot);
        const auto Em   = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
        if (ComputeLinearLuminance(Em) > 1.0e-4f)
        {
            return Em;
        }

        const float Metal  = std::clamp(Mat.Specular.Metalness, 0.0f, 1.0f);
        const float Trans  = std::clamp(Mat.Transmission.TransmissionWeight, 0.0f, 1.0f);
        const float DiffW  = (1.0f - Metal) * (1.0f - Trans);

        const auto GiOverPi = LookupGiOverPi(PixX, PixY, Depth, Pos, Nrm, Wo);
        Mathematics::LinearRGB Sum {
            GiOverPi.R * Mat.Diffuse.BaseColor.R * DiffW,
            GiOverPi.G * Mat.Diffuse.BaseColor.G * DiffW,
            GiOverPi.B * Mat.Diffuse.BaseColor.B * DiffW,
        };

        // Analytic clean specular highlight from clustered vehicle lights on glossy paint/floor/rims (noise-free)
        const float Rough = std::clamp(Mat.Specular.Roughness, 0.04f, 0.90f);
        if (Rough < 0.65f)
        {
            for (const auto& Clust : Bridge.Clusters)
            {
                const Mathematics::Axis3D ToE = Clust.CenterWorld - Pos;
                const float D2 = std::max(0.08f, Mathematics::EvaluateLengthSquared(ToE));
                const float D  = std::sqrt(D2);
                const Mathematics::Axis3D L = ToE * (1.0f / D);
                const float NdotL = Mathematics::EvaluateDot(Nrm, L);
                const float CosE  = Mathematics::EvaluateDot(Clust.MeanNormal, L * -1.0f);
                if (NdotL <= 0.01f || CosE <= 0.01f) continue;

                const float Vis = Bridge.EvaluateSoftConeVisibility(Pos, Nrm, Clust.CenterWorld, Clust.BoundRadiusM * 0.45f, 0.36f);
                if (Vis <= 1.0e-4f) continue;

                const auto HV = Mathematics::NormalizeAxis(L + Wo, Nrm);
                const float NdotH = std::max(0.0f, Mathematics::EvaluateDot(Nrm, HV));
                const float Alpha = std::max(0.06f, Rough * Rough);
                const float Alpha2 = Alpha * Alpha;
                const float Denom = NdotH * NdotH * (Alpha2 - 1.0f) + 1.0f;
                const float Dggx = Alpha2 / std::max(1.0e-5f, Constants::PiFloat * Denom * Denom);
                const float Fres0 = 0.04f * (1.0f - Metal) + 0.60f * Metal;
                const float Fres  = Fres0 + (1.0f - Fres0) * std::pow(1.0f - ClampUnit(Mathematics::EvaluateDot(Wo, HV)), 5.0f);
                const float SolidAng = std::min(0.45f, (Clust.TotalAreaM2 * CosE) / D2);
                const float SpecTerm = std::min(3.5f, Dggx * 0.25f * Fres) * SolidAng * Vis * NdotL;
                Sum.R += Clust.MeanRadiance.R * SpecTerm * (1.0f - Metal + Metal * Mat.Diffuse.BaseColor.R);
                Sum.G += Clust.MeanRadiance.G * SpecTerm * (1.0f - Metal + Metal * Mat.Diffuse.BaseColor.G);
                Sum.B += Clust.MeanRadiance.B * SpecTerm * (1.0f - Metal + Metal * Mat.Diffuse.BaseColor.B);
            }
        }

        // Deterministic mirror reflection / glass refraction for crisp ControlVehicle clearcoat, chrome & windows
        if (Depth == 0u && (Trans > 0.35f || Mat.Clearcoat.CoatWeight > 0.35f || (Metal > 0.75f && Rough < 0.20f)))
        {
            const Mathematics::Axis3D ReflDir = Mathematics::NormalizeAxis(
                Wo * -1.0f + Nrm * (2.0f * Mathematics::EvaluateDot(Wo, Nrm)), Nrm);
            const float CosV = ClampUnit(Mathematics::EvaluateDot(Wo, Nrm));
            const float Fres = MaterialSpace::Scattering::EvaluateExactDielectricFresnel(
                CosV, std::max(1.45f, Mat.Specular.DielectricIOR));

            SpatialAcceleration::RaySegmentQuery RefRay {};
            RefRay.Origin          = Pos + Nrm * 3.0e-3f;
            RefRay.UnitDirection   = ReflDir;
            RefRay.MinimumDistance = 1.0e-3f;
            RefRay.MaximumDistance = 40.0f;
            const auto RHit = Scene.SceneTree.TraceClosestRay(RefRay, false);
            Mathematics::LinearRGB ReflCol {};
            if (RHit.HitConfirmed && RHit.MaterialOrdinal < MatCount)
            {
                const auto RN = (Mathematics::EvaluateDot(RHit.GeometricNormal, ReflDir) < 0.0f)
                    ? RHit.GeometricNormal : (RHit.GeometricNormal * -1.0f);
                ReflCol = SelfRef(
                    PixX, PixY,
                    Pos + Nrm * 3.0e-3f + ReflDir * RHit.HitDistanceMeters,
                    RN,
                    ReflDir * -1.0f,
                    RHit.MaterialOrdinal,
                    1u,
                    SelfRef);
            }

            if (Trans > 0.35f)
            {
                SpatialAcceleration::RaySegmentQuery ThruRay {};
                ThruRay.Origin          = Pos - Nrm * 5.0e-3f;
                ThruRay.UnitDirection   = Wo * -1.0f;
                ThruRay.MinimumDistance = 2.0e-3f;
                ThruRay.MaximumDistance = 40.0f;
                const auto THit = Scene.SceneTree.TraceClosestRay(ThruRay, false);
                Mathematics::LinearRGB ThruCol {};
                if (THit.HitConfirmed && THit.MaterialOrdinal < MatCount)
                {
                    const auto TN = (Mathematics::EvaluateDot(THit.GeometricNormal, Wo * -1.0f) < 0.0f)
                        ? THit.GeometricNormal : (THit.GeometricNormal * -1.0f);
                    ThruCol = SelfRef(
                        PixX, PixY,
                        Pos - Nrm * 5.0e-3f + (Wo * -1.0f) * THit.HitDistanceMeters,
                        TN,
                        Wo,
                        THit.MaterialOrdinal,
                        1u,
                        SelfRef);
                }
                const float GlassMix = ClampUnit(Fres * 1.4f);
                Sum = ReflCol * GlassMix + ThruCol * (1.0f - GlassMix) * 0.72f;
            }
            else if (Metal > 0.75f)
            {
                const float MRefl = 0.65f * (1.0f - Rough * 2.0f);
                Sum.R = Sum.R * (1.0f - MRefl) + ReflCol.R * Mat.Diffuse.BaseColor.R * MRefl;
                Sum.G = Sum.G * (1.0f - MRefl) + ReflCol.G * Mat.Diffuse.BaseColor.G * MRefl;
                Sum.B = Sum.B * (1.0f - MRefl) + ReflCol.B * Mat.Diffuse.BaseColor.B * MRefl;
            }
            else
            {
                const float CoatF = Fres * Mat.Clearcoat.CoatWeight * 0.85f;
                Sum = Sum * (1.0f - CoatF * 0.3f) + ReflCol * CoatF;
            }
        }
        return Sum;
    };

    RunParallelScanlines(H, [&](uint32_t YBegin, uint32_t YEnd)
    {
        for (uint32_t Y = YBegin; Y < YEnd; ++Y)
        {
            const float NDCY = 1.0f - 2.0f * (static_cast<float>(Y) + 0.5f) / static_cast<float>(H);
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float NDCX = 2.0f * (static_cast<float>(X) + 0.5f) / static_cast<float>(W) - 1.0f;
                const Mathematics::Axis3D RayD = Mathematics::NormalizeAxis(
                    Fwd + Rgt * (NDCX * Aspect * TanHalf) + Up * (NDCY * TanHalf));

                SpatialAcceleration::RaySegmentQuery Ray {};
                Ray.Origin          = Cam.CameraOrigin;
                Ray.UnitDirection   = RayD;
                Ray.MinimumDistance = 1.0e-3f;
                Ray.MaximumDistance = 80.0f;
                const auto Hit = Scene.SceneTree.TraceClosestRay(Ray, false);
                if (!Hit.HitConfirmed) continue;

                const uint16_t MatSlot = (Hit.MaterialOrdinal < MatCount) ? Hit.MaterialOrdinal : 0u;
                const auto& Mat        = Scene.MaterialRegister.InspectAuthoredMaterial(MatSlot);
                const Mathematics::Axis3D HitP = Cam.CameraOrigin + RayD * Hit.HitDistanceMeters;
                const Mathematics::Axis3D HitN = (Mathematics::EvaluateDot(Hit.GeometricNormal, RayD) < 0.0f)
                    ? Hit.GeometricNormal : (Hit.GeometricNormal * -1.0f);
                const Mathematics::Axis3D Wo = RayD * -1.0f;

                const std::size_t PIdx = static_cast<std::size_t>(Y) * W + X;

                const auto Em = MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Mat.PorosityLight);
                const float DiffW = (1.0f - Mat.Specular.Metalness) * (1.0f - Mat.Transmission.TransmissionWeight);
                const auto GiOverPi = LookupGiOverPi(X, Y, 0u, HitP, HitN, Wo);
                Output.IsolatedDiffHdr[PIdx] = (ComputeLinearLuminance(Em) > 1.0e-4f)
                    ? Em
                    : Mathematics::LinearRGB{
                        GiOverPi.R * Mat.Diffuse.BaseColor.R * DiffW,
                        GiOverPi.G * Mat.Diffuse.BaseColor.G * DiffW,
                        GiOverPi.B * Mat.Diffuse.BaseColor.B * DiffW,
                    };

                Output.FullLitHdr[PIdx] = ShadeSurfaceAtHit(X, Y, HitP, HitN, Wo, MatSlot, 0u, ShadeSurfaceAtHit);
            }
        }
    });

    return Output;
}

[[nodiscard]] double ComputeLinearRgbRmse(
    std::span<const Mathematics::LinearRGB> Candidate,
    std::span<const Mathematics::LinearRGB> Reference) noexcept
{
    const std::size_t N = std::min(Candidate.size(), Reference.size());
    if (N == 0u) return 0.0;
    double SqSum = 0.0;
    for (std::size_t I = 0u; I < N; ++I)
    {
        const double DR = std::clamp(static_cast<double>(Candidate[I].R), 0.0, 20.0) -
                          std::clamp(static_cast<double>(Reference[I].R), 0.0, 20.0);
        const double DG = std::clamp(static_cast<double>(Candidate[I].G), 0.0, 20.0) -
                          std::clamp(static_cast<double>(Reference[I].G), 0.0, 20.0);
        const double DB = std::clamp(static_cast<double>(Candidate[I].B), 0.0, 20.0) -
                          std::clamp(static_cast<double>(Reference[I].B), 0.0, 20.0);
        SqSum += (DR * DR + DG * DG + DB * DB) / 3.0;
    }
    return std::sqrt(SqSum / static_cast<double>(N));
}

[[nodiscard]] double ComputeMeanLuminance(std::span<const Mathematics::LinearRGB> Buffer) noexcept
{
    if (Buffer.empty()) return 0.0;
    double Sum = 0.0;
    for (const auto& P : Buffer)
    {
        Sum += static_cast<double>(ComputeLinearLuminance(P));
    }
    return Sum / static_cast<double>(Buffer.size());
}

} // namespace
} // namespace Frontier::Exhibits

int main()
{
    using namespace Frontier;
    using namespace Frontier::Exhibits;

    ValidationScoreboard Score {};
    const std::filesystem::path OutDir = "Exhibits/Images/CleanEmissiveGi";
    std::filesystem::create_directories(OutDir);

    std::printf("\n=== Clean Emissive GI Validation (Zero Denoiser, Same ControlVehicle, DDGI & Normal GI) ===\n");

    //====================================================================================================================
    // 1. BUILD CANONICAL STUDIO & CONTROLVEHICLE (SLOTS 6..15)
    //====================================================================================================================
    const auto& VehicleGeom = ParticleSpace::QueryControlVehicleGeometrySet();
    Score.Check(VehicleGeom.LoadedFromDisk && VehicleGeom.Triangles.size() > 20000u,
                "Loaded real ControlVehicle FCT1 geometry (> 20,000 triangles)");

    const Mathematics::Axis3D VehiclePose { -0.85f, 0.05f, 0.0f };
    StudioVehicleBundle Bundle = BuildCanonicalStudioVehicleBundle();

    RebuildBundleWorldTree(Bundle, VehiclePose, false);
    Bundle.Scene.DynamicSdfObjects = { BuildVehicleDynamicSdf(Bundle, VehiclePose) };
    GeometricRaster::TieredWorldRenderSequence EngineClipmapSeq {};
    EngineClipmapSeq.PrepareWorldScene(Bundle.Scene);
    RebuildBundleWorldTree(Bundle, VehiclePose, true);

    GeometricRaster::TieredWorldRenderSequence EngineDdgiSeq {};
    EngineDdgiSeq.PrepareWorldScene(Bundle.Scene);

    const SceneGeometryBridge Bridge = ConstructSceneGeometryBridge(Bundle.Scene, Bundle.StaticBoxes);
    Score.Check(Bridge.Emissives.size() == 42u && Bridge.Clusters.size() == 6u && Bridge.TotalFlux > 10.0,
                "Canonical slot census extracts 42 emissive faces (slots 13/14 only; slot 15 plates stay non-emissive)");

    //====================================================================================================================
    // 2. INSTANTIATE ALL 3 CLEAN EMISSIVE GI ARCHITECTURES + NUMERICAL CONSERVATION CHECKS
    //====================================================================================================================
    const auto T0 = std::chrono::steady_clock::now();
    StokesContourEmissiveGi Cand1 {};
    Cand1.Prepare(Bundle.Scene, Bundle.StaticBoxes, VehiclePose);
    const double Cand1PrepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T0).count();

    const auto T1 = std::chrono::steady_clock::now();
    SurfelCascadeEmissiveGi Cand2 {};
    Cand2.Prepare(Bundle.Scene, Bundle.StaticBoxes, VehiclePose);
    const double Cand2PrepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T1).count();

    const auto T2 = std::chrono::steady_clock::now();
    ContinuousFieldEmissiveGi Cand3 {};
    Cand3.Prepare(Bundle.Scene, Bundle.StaticBoxes, VehiclePose);
    const double Cand3PrepMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - T2).count();

    std::printf("  Preparation timings: Cand1(Stokes)=%.1fms | Cand2(Surfel)=%.1fms | Cand3(L1Field+Veach)=%.1fms\n",
                Cand1PrepMs, Cand2PrepMs, Cand3PrepMs);

    // Check A: Exact Stokes Hemispherical Quad White-Furnace Conservation (E/pi = 1.0 over enclosed hemisphere)
    {
        const Mathematics::Axis3D O { 0.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D N { 0.0f, 0.0f, 1.0f };
        const float L = 1.0e4f;
        const float F0 = StokesContourEmissiveGi::EvaluateClippedTriangleFormFactor(O, N, {-L,-L,1e-4f}, { L,-L,1e-4f}, { L, L,1e-4f});
        const float F1 = StokesContourEmissiveGi::EvaluateClippedTriangleFormFactor(O, N, {-L,-L,1e-4f}, { L, L,1e-4f}, {-L, L,1e-4f});
        Score.Check(std::fabs((F0 + F1) - 1.0f) < 1.0e-3f,
                    "Candidate 1 Stokes contour form factor satisfies exact hemispherical white-furnace conservation (F=1.000)");
    }

    // Check B: Physically consistent floor irradiance ahead of headlights across all 3 solutions
    {
        const Mathematics::Axis3D RecP { 3.55f, 0.05f, 0.00f };
        const Mathematics::Axis3D RecN { 0.0f, 0.0f, 1.0f };
        const auto S1 = Cand1.EvaluateNormalGiOverPi(RecP, RecN);
        const auto S2 = Cand2.EvaluateNormalGiOverPi(RecP, RecN);
        const auto S3 = Cand3.EvaluateNormalGiOverPi(RecP, RecN);
        std::printf("  Floor Irradiance@3.55m: S1=%.3f S2=%.3f S3=%.3f\n",
                    ComputeLinearLuminance(S1), ComputeLinearLuminance(S2), ComputeLinearLuminance(S3));
        Score.Check(ComputeLinearLuminance(S1) > 0.12f &&
                    ComputeLinearLuminance(S2) > 0.12f &&
                    ComputeLinearLuminance(S3) > 0.12f,
                    "All 3 candidates produce physically strong floor irradiance ahead of ControlVehicle headlights");
    }

    // Check C: Zero-variance deterministic reproducibility
    {
        const Mathematics::Axis3D P { 2.85f, -0.45f, 0.00f };
        const Mathematics::Axis3D N { 0.0f, 0.0f, 1.0f };
        const Mathematics::Axis3D V { 0.2f, -0.8f, 0.56f };
        const auto A1 = Cand1.EvaluateNormalGiOverPi(P, N);
        const auto B1 = Cand1.EvaluateNormalGiOverPi(P, N);
        const auto A2 = Cand2.EvaluateDdgiOverPi(P, N, V);
        const auto B2 = Cand2.EvaluateDdgiOverPi(P, N, V);
        const auto A3 = Cand3.EvaluateDdgiOverPi(P, N, V);
        const auto B3 = Cand3.EvaluateDdgiOverPi(P, N, V);
        Score.Check(A1.R == B1.R && A2.G == B2.G && A3.B == B3.B,
                    "Zero-variance deterministic GI evaluation across both Normal GI and DDGI");
    }

    //====================================================================================================================
    // 3. RENDER 560x315 SHOWCASE RENDERS (BASELINES, 128-SPP EMISSIVE-NEE REF, CANDIDATES 1..3 NORMAL GI & DDGI)
    //====================================================================================================================
    constexpr uint32_t W = 560u;
    constexpr uint32_t H = 315u;
    constexpr float DisplayExposure = 1.65f;

    GeometricRaster::PerspectiveCameraBasis Cam {};
    Cam.CameraOrigin       = { 3.35f, -4.35f, 1.85f };
    Cam.TargetPoint        = { -0.35f, 0.15f, 0.70f };
    Cam.WorldUpAxis        = { 0.0f, 0.0f, 1.0f };
    Cam.VerticalFovRadians = 44.0f * Constants::DegToRadFloat;
    Cam.ViewportWidthPx    = W;
    Cam.ViewportHeightPx   = H;

    // 3a. Reference: 128-SPP Unbiased Emissive-NEE + MIS Path Tracer (NO denoiser!)
    std::vector<Mathematics::LinearRGB> RefFullHdr(static_cast<std::size_t>(W) * H);
    const auto RefStart = std::chrono::steady_clock::now();
    {
        constexpr uint32_t RefSpp = 128u;
        const Mathematics::Axis3D Fwd = Mathematics::NormalizeAxis(Cam.TargetPoint - Cam.CameraOrigin, { 0.0f, 1.0f, 0.0f });
        const Mathematics::Axis3D Rgt = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
        const Mathematics::Axis3D Up  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Rgt, Fwd), { 0.0f, 0.0f, 1.0f });
        const float TanHalf           = std::tan(Cam.VerticalFovRadians * 0.5f);
        const float Aspect            = static_cast<float>(W) / static_cast<float>(H);

        RunParallelScanlines(H, [&](uint32_t YB, uint32_t YE)
        {
            for (uint32_t Y = YB; Y < YE; ++Y)
            {
                for (uint32_t X = 0u; X < W; ++X)
                {
                    const uint32_t Seed = GeometricRaster::AvalancheKey32(X + Y * W + 193u);
                    Mathematics::LinearRGB Sum {};
                    for (uint32_t S = 0u; S < RefSpp; ++S)
                    {
                        const auto J = GeometricRaster::Sobol02Sample(S, Seed, 0u);
                        const float NDCX = 2.0f * (static_cast<float>(X) + J.X) / static_cast<float>(W) - 1.0f;
                        const float NDCY = 1.0f - 2.0f * (static_cast<float>(Y) + J.Y) / static_cast<float>(H);
                        const Mathematics::Axis3D RayD = Mathematics::NormalizeAxis(
                            Fwd + Rgt * (NDCX * Aspect * TanHalf) + Up * (NDCY * TanHalf));
                        Sum = Sum + TraceEmissiveNeeReferenceSample(
                            Cam.CameraOrigin, RayD, Bundle.Scene, Bridge, S, Seed, 4u);
                    }
                    RefFullHdr[static_cast<std::size_t>(Y) * W + X] = Sum * (1.0f / static_cast<float>(RefSpp));
                }
            }
        });
    }
    const double RefMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - RefStart).count();
    const auto RefRgba8 = EncodeHdrBufferToRgba8(RefFullHdr, DisplayExposure);
    WriteRgba8Png(OutDir / "07_GroundTruth_EmissiveNee_128spp.png", W, H, RefRgba8);

    // 3b. Frontier Current Baselines (NO denoiser, pure Emissive-Only)
    GeometricRaster::WorldRenderConfiguration EngineCfg {};
    EngineCfg.ShadowAlgorithm  = GeometricRaster::SolarShadowAlgorithm::HybridSdf;
    EngineCfg.SdfGiBilateral   = false; // Strictly NO denoiser
    EngineCfg.PathTraceDenoise = false;

    EngineCfg.Engine = GeometricRaster::WorldRenderEngine::DdgiRaytracing;
    const auto EngineDdgiOut = EngineDdgiSeq.RenderWorldFrameWithEngine(EngineCfg, Cam, Cam, Bundle.Scene);

    EngineCfg.Engine = GeometricRaster::WorldRenderEngine::SdfGlobalIllumination;
    const auto EngineRestirOut = EngineClipmapSeq.RenderWorldFrameWithEngine(EngineCfg, Cam, Cam, Bundle.Scene);

    // Wrap Existing DDGI, Existing Clipmap Cone GI, and Existing ReSTIR through identical primary shading
    const auto EngineDdgiView = RenderViewportWithGiSolver(W, H, Cam, Bundle.Scene, Bridge,
        [&](uint32_t PX, uint32_t PY, uint32_t Depth, const Mathematics::Axis3D& Pos, const Mathematics::Axis3D& Nrm, const Mathematics::Axis3D& ViewD) noexcept
        {
            if (Depth == 0u) return EngineDdgiOut.IndirectDiffuseHdr[static_cast<std::size_t>(PY) * W + PX];
            return EngineDdgiSeq.InspectIrradianceLattice().SampleChebyshevIrradianceOverPi(Pos, Nrm, ViewD);
        });

    const auto EngineClipmapView = RenderViewportWithGiSolver(W, H, Cam, Bundle.Scene, Bridge,
        [&](uint32_t, uint32_t, uint32_t, const Mathematics::Axis3D& Pos, const Mathematics::Axis3D& Nrm, const Mathematics::Axis3D&) noexcept
        {
            return EngineClipmapSeq.InspectClipmapVolume().EvaluateDeterministicConeGiOverPi(
                Pos, Nrm, Bundle.Scene.SkyRadiance, 24u, Bundle.Scene.DynamicSdfObjects);
        });

    const auto EngineRestirView = RenderViewportWithGiSolver(W, H, Cam, Bundle.Scene, Bridge,
        [&](uint32_t PX, uint32_t PY, uint32_t Depth, const Mathematics::Axis3D& Pos, const Mathematics::Axis3D& Nrm, const Mathematics::Axis3D&) noexcept
        {
            if (Depth == 0u) return EngineRestirOut.IndirectDiffuseHdr[static_cast<std::size_t>(PY) * W + PX];
            return EngineClipmapSeq.InspectClipmapVolume().EvaluateDeterministicConeGiOverPi(
                Pos, Nrm, Bundle.Scene.SkyRadiance, 24u, Bundle.Scene.DynamicSdfObjects);
        });

    const auto EngineDdgiFullRgba8    = EncodeHdrBufferToRgba8(EngineDdgiView.FullLitHdr, DisplayExposure);
    const auto EngineClipmapFullRgba8 = EncodeHdrBufferToRgba8(EngineClipmapView.FullLitHdr, DisplayExposure);
    const auto EngineDdgiGiRgba8      = EncodeHdrBufferToRgba8(EngineDdgiView.IsolatedDiffHdr, DisplayExposure);
    const auto EngineClipmapGiRgba8   = EncodeHdrBufferToRgba8(EngineClipmapView.IsolatedDiffHdr, DisplayExposure);
    const auto EngineRestirRgba8      = EncodeHdrBufferToRgba8(EngineRestirView.FullLitHdr, DisplayExposure);

    // 3c. Candidate 1: Normal GI & DDGI
    const auto C1NormalStart = std::chrono::steady_clock::now();
    const auto C1Normal = RenderViewportWithGiSolver(W, H, Cam, Bundle.Scene, Bridge,
        [&](uint32_t, uint32_t, uint32_t, const Mathematics::Axis3D& P, const Mathematics::Axis3D& N, const Mathematics::Axis3D&) noexcept
        {
            return Cand1.EvaluateNormalGiOverPi(P, N);
        });
    const double C1NormalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - C1NormalStart).count();

    const auto C1DdgiStart = std::chrono::steady_clock::now();
    const auto C1Ddgi = RenderViewportWithGiSolver(W, H, Cam, Bundle.Scene, Bridge,
        [&](uint32_t, uint32_t, uint32_t, const Mathematics::Axis3D& P, const Mathematics::Axis3D& N, const Mathematics::Axis3D& V) noexcept
        {
            return Cand1.EvaluateDdgiOverPi(P, N, V);
        });
    const double C1DdgiMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - C1DdgiStart).count();

    // 3d. Candidate 2: Normal GI & DDGI
    const auto C2NormalStart = std::chrono::steady_clock::now();
    const auto C2Normal = RenderViewportWithGiSolver(W, H, Cam, Bundle.Scene, Bridge,
        [&](uint32_t, uint32_t, uint32_t, const Mathematics::Axis3D& P, const Mathematics::Axis3D& N, const Mathematics::Axis3D&) noexcept
        {
            return Cand2.EvaluateNormalGiOverPi(P, N);
        });
    const double C2NormalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - C2NormalStart).count();

    const auto C2DdgiStart = std::chrono::steady_clock::now();
    const auto C2Ddgi = RenderViewportWithGiSolver(W, H, Cam, Bundle.Scene, Bridge,
        [&](uint32_t, uint32_t, uint32_t, const Mathematics::Axis3D& P, const Mathematics::Axis3D& N, const Mathematics::Axis3D& V) noexcept
        {
            return Cand2.EvaluateDdgiOverPi(P, N, V);
        });
    const double C2DdgiMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - C2DdgiStart).count();

    // 3e. Candidate 3: Normal GI & DDGI
    const auto C3NormalStart = std::chrono::steady_clock::now();
    const auto C3Normal = RenderViewportWithGiSolver(W, H, Cam, Bundle.Scene, Bridge,
        [&](uint32_t, uint32_t, uint32_t, const Mathematics::Axis3D& P, const Mathematics::Axis3D& N, const Mathematics::Axis3D&) noexcept
        {
            return Cand3.EvaluateNormalGiOverPi(P, N);
        });
    const double C3NormalMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - C3NormalStart).count();

    const auto C3DdgiStart = std::chrono::steady_clock::now();
    const auto C3Ddgi = RenderViewportWithGiSolver(W, H, Cam, Bundle.Scene, Bridge,
        [&](uint32_t, uint32_t, uint32_t, const Mathematics::Axis3D& P, const Mathematics::Axis3D& N, const Mathematics::Axis3D& V) noexcept
        {
            return Cand3.EvaluateDdgiOverPi(P, N, V);
        });
    const double C3DdgiMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - C3DdgiStart).count();

    // Save full individual PNGs
    const auto C1NormalRgba8 = EncodeHdrBufferToRgba8(C1Normal.FullLitHdr, DisplayExposure);
    const auto C1DdgiRgba8   = EncodeHdrBufferToRgba8(C1Ddgi.FullLitHdr,   DisplayExposure);
    const auto C2NormalRgba8 = EncodeHdrBufferToRgba8(C2Normal.FullLitHdr, DisplayExposure);
    const auto C2DdgiRgba8   = EncodeHdrBufferToRgba8(C2Ddgi.FullLitHdr,   DisplayExposure);
    const auto C3NormalRgba8 = EncodeHdrBufferToRgba8(C3Normal.FullLitHdr, DisplayExposure);
    const auto C3DdgiRgba8   = EncodeHdrBufferToRgba8(C3Ddgi.FullLitHdr,   DisplayExposure);

    WriteRgba8Png(OutDir / "01_Candidate1_StokesContour_NormalGi.png",  W, H, C1NormalRgba8);
    WriteRgba8Png(OutDir / "02_Candidate1_StokesContour_Ddgi.png",      W, H, C1DdgiRgba8);
    WriteRgba8Png(OutDir / "03_Candidate2_SurfelRadiosity_NormalGi.png",W, H, C2NormalRgba8);
    WriteRgba8Png(OutDir / "04_Candidate2_SurfelRadiosity_Ddgi.png",    W, H, C2DdgiRgba8);
    WriteRgba8Png(OutDir / "05_Candidate3_ContinuousField_NormalGi.png",W, H, C3NormalRgba8);
    WriteRgba8Png(OutDir / "06_Candidate3_ContinuousField_Ddgi.png",    W, H, C3DdgiRgba8);

    //====================================================================================================================
    // 4. RMSE & QUALITY BENCHMARKS VS 128-SPP EMISSIVE-NEE REFERENCE & CURRENT BASELINES
    //====================================================================================================================
    const double RmseEngineDdgi    = ComputeLinearRgbRmse(EngineDdgiView.FullLitHdr,    RefFullHdr);
    const double RmseEngineClipmap = ComputeLinearRgbRmse(EngineClipmapView.FullLitHdr, RefFullHdr);
    const double RmseC1Normal      = ComputeLinearRgbRmse(C1Normal.FullLitHdr, RefFullHdr);
    const double RmseC1Ddgi        = ComputeLinearRgbRmse(C1Ddgi.FullLitHdr,   RefFullHdr);
    const double RmseC2Normal      = ComputeLinearRgbRmse(C2Normal.FullLitHdr, RefFullHdr);
    const double RmseC2Ddgi        = ComputeLinearRgbRmse(C2Ddgi.FullLitHdr,   RefFullHdr);
    const double RmseC3Normal      = ComputeLinearRgbRmse(C3Normal.FullLitHdr, RefFullHdr);
    const double RmseC3Ddgi        = ComputeLinearRgbRmse(C3Ddgi.FullLitHdr,   RefFullHdr);

    std::printf("  Render timings (560x315, 0 denoiser):\n"
                "    Ref 128spp=%.0fms | C1(Norm/DDGI)=%.1f/%.1fms | C2=%.1f/%.1fms | C3=%.1f/%.1fms\n",
                RefMs, C1NormalMs, C1DdgiMs, C2NormalMs, C2DdgiMs, C3NormalMs, C3DdgiMs);
    std::printf("  HDR RMSE vs 128-SPP Ref:\n"
                "    EngineDDGI=%.4f EngineClipmap=%.4f\n"
                "    C1(Norm/DDGI)=%.4f / %.4f\n"
                "    C2(Norm/DDGI)=%.4f / %.4f\n"
                "    C3(Norm/DDGI)=%.4f / %.4f\n",
                RmseEngineDdgi, RmseEngineClipmap,
                RmseC1Normal, RmseC1Ddgi,
                RmseC2Normal, RmseC2Ddgi,
                RmseC3Normal, RmseC3Ddgi);

    Score.Check(RmseC1Normal < RmseEngineClipmap && RmseC1Ddgi < RmseEngineDdgi,
                "Candidate 1 (Stokes Contour + Soft SDF) cuts RMSE in BOTH Normal GI & DDGI vs current Frontier baselines");
    Score.Check(RmseC2Normal < RmseEngineClipmap && RmseC2Ddgi < RmseEngineDdgi,
                "Candidate 2 (Two-Tier Surfel Radiosity) cuts RMSE in BOTH Normal GI & DDGI vs current Frontier baselines");
    Score.Check(RmseC3Normal < RmseEngineClipmap && RmseC3Ddgi < RmseEngineDdgi,
                "Candidate 3 (Directional L1 Clipmap + Veach-MIS DDGI) cuts RMSE in BOTH Normal GI & DDGI vs current baselines");

    const double MeanLumRef = ComputeMeanLuminance(RefFullHdr);
    const double MeanLumC1N = ComputeMeanLuminance(C1Normal.IsolatedDiffHdr);
    const double MeanLumC2N = ComputeMeanLuminance(C2Normal.IsolatedDiffHdr);
    const double MeanLumC3N = ComputeMeanLuminance(C3Normal.IsolatedDiffHdr);
    Score.Check(MeanLumRef > 0.01 && MeanLumC1N > 0.005 && MeanLumC2N > 0.005 && MeanLumC3N > 0.005,
                "Non-trivial physical diffuse GI luminance preserved across all 3 candidates without collapse");

    //====================================================================================================================
    // 5. BUILD LABELED VISUAL SHOWCASE COMPARISON SHEETS
    //====================================================================================================================
    {
        const std::array<SheetTileEntry, 4u> Tiles {{
            { "BEFORE: CURRENT DDGI",      "48-RAY NO DENOISER (BLOTCHES)", { 235u, 110u, 95u },  EngineDdgiFullRgba8 },
            { "BEFORE: CURRENT CLIPMAP",   "16-CONE NO DENOISER (BLOCKS)",  { 235u, 110u, 95u },  EngineClipmapFullRgba8 },
            { "BEFORE: CURRENT RESTIR",    "NO DENOISER (FIREFLY NOISE)",   { 235u, 110u, 95u },  EngineRestirRgba8 },
            { "GROUND TRUTH REFERENCE",    "128-SPP EMISSIVE-NEE + MIS",    { 110u, 225u, 155u }, RefRgba8 },
        }};
        WriteComparisonSheetPng(
            OutDir / "00_Before_CurrentBaselines.png",
            "EXISTING BASELINES (NO DENOISER) VS 128-SPP EMISSIVE-NEE REFERENCE",
            2u, 2u, W, H, Tiles);
    }

    {
        const std::array<SheetTileEntry, 8u> Tiles {{
            { "BEFORE: CURRENT CLIPMAP",  "NORMAL GI (BLOCKY VOXELS)",   { 235u, 110u, 95u },  EngineClipmapFullRgba8 },
            { "CAND 1: STOKES CONTOUR",   "NORMAL GI (EXACT + SDF)",     { 115u, 200u, 255u }, C1NormalRgba8 },
            { "CAND 2: SURFEL FIELD",     "NORMAL GI (BUNNELL DISKS)",   { 130u, 235u, 165u }, C2NormalRgba8 },
            { "CAND 3: L1 CLIPMAP",       "NORMAL GI (64 CONES + HERM)", { 245u, 195u, 105u }, C3NormalRgba8 },
            { "BEFORE: CURRENT DDGI",     "DDGI (BLOTCHES + CUTOFF)",    { 235u, 110u, 95u },  EngineDdgiFullRgba8 },
            { "CAND 1: STOKES SPLIT",     "DDGI (STOKES + 2B LATTICE)",  { 115u, 200u, 255u }, C1DdgiRgba8 },
            { "CAND 2: SURFEL LATTICE",   "DDGI (DUAL SURFEL-BAKED)",    { 130u, 235u, 165u }, C2DdgiRgba8 },
            { "CAND 3: VEACH-MIS DDGI",   "DDGI (192-RAY HERMITE)",      { 245u, 195u, 105u }, C3DdgiRgba8 },
        }};
        WriteComparisonSheetPng(
            OutDir / "08_Comparison_ThreeCandidates_SameVehicle.png",
            "THREE CLEAN EMISSIVE GI CANDIDATES (ZERO DENOISER) - SAME CONTROLVEHICLE",
            4u, 2u, W, H, Tiles);
    }

    {
        const std::array<SheetTileEntry, 8u> Tiles {{
            { "BEFORE: CURRENT CLIPMAP",  "ISOLATED GI (BLOCKY STREAKS)",{ 235u, 110u, 95u },  EngineClipmapGiRgba8 },
            { "CAND 1: STOKES GI ONLY",   "NORMAL GI DIFFUSE FIELD",     { 115u, 200u, 255u }, EncodeHdrBufferToRgba8(C1Normal.IsolatedDiffHdr, DisplayExposure) },
            { "CAND 2: SURFEL GI ONLY",   "NORMAL GI DIFFUSE FIELD",     { 130u, 235u, 165u }, EncodeHdrBufferToRgba8(C2Normal.IsolatedDiffHdr, DisplayExposure) },
            { "CAND 3: L1 FIELD GI ONLY", "NORMAL GI DIFFUSE FIELD",     { 245u, 195u, 105u }, EncodeHdrBufferToRgba8(C3Normal.IsolatedDiffHdr, DisplayExposure) },
            { "BEFORE: CURRENT DDGI",     "ISOLATED GI (BLOTCHES)",      { 235u, 110u, 95u },  EngineDdgiGiRgba8 },
            { "CAND 1: DDGI GI ONLY",     "DDGI DIFFUSE FIELD",          { 115u, 200u, 255u }, EncodeHdrBufferToRgba8(C1Ddgi.IsolatedDiffHdr, DisplayExposure) },
            { "CAND 2: DDGI GI ONLY",     "DDGI DIFFUSE FIELD",          { 130u, 235u, 165u }, EncodeHdrBufferToRgba8(C2Ddgi.IsolatedDiffHdr, DisplayExposure) },
            { "CAND 3: DDGI GI ONLY",     "DDGI DIFFUSE FIELD",          { 245u, 195u, 105u }, EncodeHdrBufferToRgba8(C3Ddgi.IsolatedDiffHdr, DisplayExposure) },
        }};
        WriteComparisonSheetPng(
            OutDir / "09_IsolatedGi_ComparisonSheet.png",
            "PURE ISOLATED DIFFUSE GI ONLY (ZERO SPECULAR, ZERO DENOISER)",
            4u, 2u, W, H, Tiles);
    }

    {
        std::ofstream Out(OutDir / "validation_summary.json");
        Out << std::fixed << std::setprecision(4)
            << "{\n"
            << "  \"denoiser_enabled\": false,\n"
            << "  \"resolution\": \"560x315\",\n"
            << "  \"vehicle_triangles\": " << VehicleGeom.Triangles.size() << ",\n"
            << "  \"emissive_triangles\": " << Bridge.Emissives.size() << ",\n"
            << "  \"rmse_vs_128spp_ref\": {\n"
            << "    \"engine_ddgi\": " << RmseEngineDdgi << ",\n"
            << "    \"engine_clipmap\": " << RmseEngineClipmap << ",\n"
            << "    \"cand1_stokes_normal_gi\": " << RmseC1Normal << ",\n"
            << "    \"cand1_stokes_ddgi\": " << RmseC1Ddgi << ",\n"
            << "    \"cand2_surfel_normal_gi\": " << RmseC2Normal << ",\n"
            << "    \"cand2_surfel_ddgi\": " << RmseC2Ddgi << ",\n"
            << "    \"cand3_l1field_normal_gi\": " << RmseC3Normal << ",\n"
            << "    \"cand3_l1field_ddgi\": " << RmseC3Ddgi << "\n"
            << "  },\n"
            << "  \"verified\": " << Score.Verified << ",\n"
            << "  \"failed\": " << Score.Failed << "\n"
            << "}\n";
    }

    std::printf("\n[CleanEmissiveGiValidation] Verified=%u Failed=%u | Images saved under %s\n",
                Score.Verified, Score.Failed, OutDir.string().c_str());
    return (Score.Failed == 0u) ? 0 : 1;
}

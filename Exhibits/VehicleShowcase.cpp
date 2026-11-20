/*============================================================================================================================================
                                                          VEHICLESHOWCASE.CPP
============================================================================================================================================*/
// 🧩 Large-format ten-view turntable of the Controlled Integrator vehicle decoded directly from FCT1 triangle streams

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/MathConstants.h"
#include "Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/EmissiveGiCandidateSequence.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/MaterialSpace/CompactMaterialRegister.h"
#include "Engine/MaterialSpace/Scattering/BlackbodyEmission.h"
#include "Engine/MaterialSpace/SurfaceMaterial.h"
#include "Engine/Mathematics/CoordinateSpace.h"
#include "Engine/Mathematics/DeterministicSampling.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wcast-align"
#include <stb_image_write.h>
#include <stb_easy_font.h>
#pragma GCC diagnostic pop

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

using namespace Frontier;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr float PiFloat                  = 3.14159265358979323846f;
constexpr uint32_t DefaultImageWidth      = 1920u; // [px] - Requested large deliverable width
constexpr uint32_t DefaultImageHeight      = 1080u; // [px] - Requested large deliverable height
constexpr uint32_t DefaultPrimarySamples   = 4u;   // [rays] - R2 stratified primary rays per pixel
constexpr uint32_t DefaultSunSamples      = 6u;   // [rays] - Solar-disc visibility rays per shaded hit
constexpr uint32_t ContactSheetColumns   = 5u;   // [cnt] - Contact sheet grid columns
constexpr uint32_t ContactSheetRows      = 2u;   // [cnt] - Contact sheet grid rows
constexpr uint32_t ContactSheetThumbWidth  = 960u; // [px] - Rendered thumbnail width (half of 1920)
constexpr uint32_t ContactSheetThumbHeight = 540u; // [px] - Rendered thumbnail height (half of 1080)
constexpr uint32_t ContactSheetGap       = 14u;  // [px] - Inter-tile spacing
constexpr uint32_t ContactSheetHeader    = 34u;  // [px] - Caption strip height

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCORECARD
//------------------------------------------------------------------------------------------------------------------------

struct Scorecard
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Check(bool Condition, const std::string& Caption)
    {
        if (Condition)
        {
            ++Passed;
            std::cout << "[VehicleShowcase][PASS] " << Caption << '\n';
        }
        else
        {
            ++Failed;
            std::cerr << "[VehicleShowcase][FAIL] " << Caption << '\n';
        }
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  AXIS ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Mathematics::Axis3D Normalize(const Mathematics::Axis3D& Vector, const Mathematics::Axis3D& Fallback = { 0.0f, 0.0f, 1.0f }) noexcept
{
    const float LengthSquared = Vector.X * Vector.X + Vector.Y * Vector.Y + Vector.Z * Vector.Z;
    if (LengthSquared < 1.0e-12f)
    {
        return Fallback;
    }
    const float InverseLength = 1.0f / std::sqrt(LengthSquared);
    return { Vector.X * InverseLength, Vector.Y * InverseLength, Vector.Z * InverseLength };
}

[[nodiscard]] float Dot(const Mathematics::Axis3D& Left, const Mathematics::Axis3D& Right) noexcept
{
    return Left.X * Right.X + Left.Y * Right.Y + Left.Z * Right.Z;
}

[[nodiscard]] Mathematics::Axis3D Cross(const Mathematics::Axis3D& Left, const Mathematics::Axis3D& Right) noexcept
{
    return {
        Left.Y * Right.Z - Left.Z * Right.Y,
        Left.Z * Right.X - Left.X * Right.Z,
        Left.X * Right.Y - Left.Y * Right.X,
    };
}

[[nodiscard]] Mathematics::Axis3D Mul(const Mathematics::Axis3D& Operand, float Scale) noexcept
{
    return { Operand.X * Scale, Operand.Y * Scale, Operand.Z * Scale };
}

[[nodiscard]] Mathematics::Axis3D Add(const Mathematics::Axis3D& Left, const Mathematics::Axis3D& Right) noexcept
{
    return { Left.X + Right.X, Left.Y + Right.Y, Left.Z + Right.Z };
}

[[nodiscard]] Mathematics::Axis3D Sub(const Mathematics::Axis3D& Left, const Mathematics::Axis3D& Right) noexcept
{
    return { Left.X - Right.X, Left.Y - Right.Y, Left.Z - Right.Z };
}

[[nodiscard]] Mathematics::Axis3D LerpAxis(const Mathematics::Axis3D& Left, const Mathematics::Axis3D& Right, float Blend) noexcept
{
    return {
        Left.X + (Right.X - Left.X) * Blend,
        Left.Y + (Right.Y - Left.Y) * Blend,
        Left.Z + (Right.Z - Left.Z) * Blend,
    };
}

[[nodiscard]] Mathematics::LinearRGB LerpColor(const Mathematics::LinearRGB& Left, const Mathematics::LinearRGB& Right, float Blend) noexcept
{
    return {
        Left.R + (Right.R - Left.R) * Blend,
        Left.G + (Right.G - Left.G) * Blend,
        Left.B + (Right.B - Left.B) * Blend,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    COLOR PIPELINE
//------------------------------------------------------------------------------------------------------------------------

// 📝 ACES-style filmic curve keeps saturated body paint from clipping while preserving shadow detail
[[nodiscard]] float ApplyFilmicCurve(float Radiance) noexcept
{
    constexpr float CurveA = 2.51f;
    constexpr float CurveB = 0.03f;
    constexpr float CurveC = 2.43f;
    constexpr float CurveD = 0.59f;
    constexpr float CurveE = 0.14f;
    const float Safe = std::max(0.0f, Radiance);
    return std::clamp((Safe * (CurveA * Safe + CurveB)) / (Safe * (CurveC * Safe + CurveD) + CurveE), 0.0f, 1.0f);
}

[[nodiscard]] uint8_t EncodeSrgbByte(float LinearValue) noexcept
{
    const float Clamped = std::clamp(LinearValue, 0.0f, 1.0f);
    const float Encoded = Clamped <= 0.0031308f ? 12.92f * Clamped : 1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(std::clamp(std::lround(Encoded * 255.0f), 0l, 255l));
}

// 📝 FNV-1a 64-bit digest over the finished pixel array proves run-to-run determinism
[[nodiscard]] uint64_t DigestPixelBytes(std::span<const uint8_t> Bytes) noexcept
{
    uint64_t Digest = 1469598103934665603ull;
    for (uint8_t Byte : Bytes)
    {
        Digest ^= static_cast<uint64_t>(Byte);
        Digest *= 1099511628211ull;
    }
    return Digest;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PNG OUTPUT
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] bool WriteRgba8Png(const std::filesystem::path& Path, uint32_t Width, uint32_t Height, std::span<const uint8_t> Rgba)
{
    if (Width == 0u || Height == 0u || Rgba.size() != static_cast<std::size_t>(Width) * Height * 4u)
    {
        return false;
    }
    if (Path.has_parent_path())
    {
        std::error_code DirectoryError {};
        std::filesystem::create_directories(Path.parent_path(), DirectoryError);
        if (DirectoryError)
        {
            return false;
        }
    }
    const std::string NarrowPath = Path.string();
    return stbi_write_png(NarrowPath.c_str(), static_cast<int>(Width), static_cast<int>(Height), 4, Rgba.data(), static_cast<int>(Width * 4u)) != 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 TEXT RASTERIZATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 stb_easy_font emits axis-aligned quads; each quad is filled as a scaled rectangle with clipping
void RasterizeText(
    std::vector<uint8_t>& Rgba,
    uint32_t              Width,
    uint32_t              Height,
    int32_t               OriginX,
    int32_t               OriginY,
    float                 Scale,
    const std::string&    Text,
    uint8_t               Red,
    uint8_t               Green,
    uint8_t               Blue)
{
    constexpr int VertexStrideBytes = 16; // [B] - float x, float y, float z, uint8 rgba

    std::vector<char> FontQuadBuffer(Text.size() * 512u + 512u, '\0');
    std::vector<char> TextCopy(Text.begin(), Text.end());
    TextCopy.push_back('\0');
    const int QuadCount = stb_easy_font_print(0.0f, 0.0f, TextCopy.data(), nullptr, FontQuadBuffer.data(), static_cast<int>(FontQuadBuffer.size()));
    if (QuadCount <= 0)
    {
        return;
    }

    const auto PlotRect = [&](int32_t X0, int32_t Y0, int32_t X1, int32_t Y1)
    {
        const int32_t Left   = std::max<int32_t>(0, X0);
        const int32_t Top    = std::max<int32_t>(0, Y0);
        const int32_t Right  = std::min<int32_t>(static_cast<int32_t>(Width), X1);
        const int32_t Bottom = std::min<int32_t>(static_cast<int32_t>(Height), Y1);
        for (int32_t Row = Top; Row < Bottom; ++Row)
        {
            for (int32_t Column = Left; Column < Right; ++Column)
            {
                const std::size_t Offset = (static_cast<std::size_t>(Row) * Width + static_cast<std::size_t>(Column)) * 4u;
                Rgba[Offset + 0u] = Red;
                Rgba[Offset + 1u] = Green;
                Rgba[Offset + 2u] = Blue;
                Rgba[Offset + 3u] = 255u;
            }
        }
    };

    for (int Quad = 0; Quad < QuadCount; ++Quad)
    {
        float MinX = 1.0e30f, MinY = 1.0e30f, MaxX = -1.0e30f, MaxY = -1.0e30f;
        for (int Corner = 0; Corner < 4; ++Corner)
        {
            const float* Vertex = reinterpret_cast<const float*>(FontQuadBuffer.data() + (static_cast<std::size_t>(Quad) * 4u + static_cast<std::size_t>(Corner)) * VertexStrideBytes);
            MinX = std::min(MinX, Vertex[0]);
            MinY = std::min(MinY, Vertex[1]);
            MaxX = std::max(MaxX, Vertex[0]);
            MaxY = std::max(MaxY, Vertex[1]);
        }
        const int32_t X0 = OriginX + static_cast<int32_t>(std::lround(MinX * Scale));
        const int32_t Y0 = OriginY + static_cast<int32_t>(std::lround(MinY * Scale));
        const int32_t X1 = OriginX + static_cast<int32_t>(std::lround(MaxX * Scale));
        const int32_t Y1 = OriginY + static_cast<int32_t>(std::lround(MaxY * Scale));
        PlotRect(X0, Y0, std::max(X1, X0 + 1), std::max(Y1, Y0 + 1));
    }
}

// 📝 Dark translucent caption bar plus readable caption text stamped over the top of a finished render
void OverlayCaptionBar(std::vector<uint8_t>& Rgba, uint32_t Width, uint32_t Height, const std::string& Caption, float TextScale)
{
    const uint32_t BarHeight = static_cast<uint32_t>(std::lround(22.0f * TextScale));
    for (uint32_t Row = 0u; Row < BarHeight && Row < Height; ++Row)
    {
        const float Fade = 0.72f * (1.0f - static_cast<float>(Row) / static_cast<float>(BarHeight));
        for (uint32_t Column = 0u; Column < Width; ++Column)
        {
            const std::size_t Offset = (static_cast<std::size_t>(Row) * Width + Column) * 4u;
            Rgba[Offset + 0u] = static_cast<uint8_t>(Rgba[Offset + 0u] * (1.0f - Fade) + 12.0f * Fade);
            Rgba[Offset + 1u] = static_cast<uint8_t>(Rgba[Offset + 1u] * (1.0f - Fade) + 16.0f * Fade);
            Rgba[Offset + 2u] = static_cast<uint8_t>(Rgba[Offset + 2u] * (1.0f - Fade) + 26.0f * Fade);
        }
    }
    RasterizeText(Rgba, Width, Height, static_cast<int32_t>(6.0f * TextScale), static_cast<int32_t>(5.0f * TextScale), TextScale, Caption, 236u, 240u, 248u);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CAMERA BASKET
//------------------------------------------------------------------------------------------------------------------------

struct CameraBasis
{
    Mathematics::Axis3D Origin       {};              // [m] - Eye position
    Mathematics::Axis3D Forward      {};              // [-] - Unit view direction
    Mathematics::Axis3D Right        {};              // [-] - Unit horizontal screen axis
    Mathematics::Axis3D Up           {};              // [-] - Unit vertical screen axis
    float               TanHalfFovY  = 0.5f;          // [-] - Vertical half-FOV tangent
    float               TanHalfFovX  = 0.5f;          // [-] - Horizontal half-FOV tangent
    float               NearClip     = 0.03f;         // [m] - Near plane distance
    float               FarClip      = 250.0f;        // [m] - Far plane distance
};

[[nodiscard]] CameraBasis ConstructCamera(
    const Mathematics::Axis3D& Eye,
    const Mathematics::Axis3D& Target,
    float                      VerticalFovRadians,
    uint32_t                   Width,
    uint32_t                   Height) noexcept
{
    CameraBasis Basis {};
    Basis.Origin  = Eye;
    Basis.Forward = Normalize(Sub(Target, Eye), { 1.0f, 0.0f, 0.0f });

    Mathematics::Axis3D ReferenceUp { 0.0f, 0.0f, 1.0f };
    Mathematics::Axis3D RightVector = Cross(Basis.Forward, ReferenceUp);
    if (Dot(RightVector, RightVector) < 1.0e-6f)
    {
        ReferenceUp = { 0.0f, 1.0f, 0.0f };
        RightVector = Cross(Basis.Forward, ReferenceUp);
    }
    Basis.Right = Normalize(RightVector, { 1.0f, 0.0f, 0.0f });
    Basis.Up    = Normalize(Cross(Basis.Right, Basis.Forward), { 0.0f, 0.0f, 1.0f });

    const float AspectRatio = static_cast<float>(Width) / static_cast<float>(std::max(1u, Height));
    Basis.TanHalfFovY = std::tan(VerticalFovRadians * 0.5f);
    Basis.TanHalfFovX = Basis.TanHalfFovY * AspectRatio;
    return Basis;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SKY RADIANCE
//------------------------------------------------------------------------------------------------------------------------

struct SkyGradient
{
    Mathematics::LinearRGB ZenithRadiance  { 0.115f, 0.155f, 0.235f }; // [W/m2/sr] - Overhead sky radiance
    Mathematics::LinearRGB HorizonRadiance { 0.330f, 0.375f, 0.445f }; // [W/m2/sr] - Horizon radiance
    Mathematics::LinearRGB FloorRadiance   { 0.135f, 0.140f, 0.150f }; // [W/m2/sr] - Below-horizon studio floor radiance
};

[[nodiscard]] Mathematics::LinearRGB EvaluateSkyRadiance(const SkyGradient& Sky, const Mathematics::Axis3D& UnitDirection) noexcept
{
    const float Vertical = std::clamp(UnitDirection.Z, -1.0f, 1.0f);
    if (Vertical >= 0.0f)
    {
        const float Blend = std::pow(Vertical, 0.55f);
        return LerpColor(Sky.HorizonRadiance, Sky.ZenithRadiance, Blend);
    }
    const float Blend = std::clamp(-Vertical * 2.2f, 0.0f, 1.0f);
    return LerpColor(Sky.HorizonRadiance, Sky.FloorRadiance, Blend);
}

// 📝 Angular average of the upper hemisphere used for the cheap ambient diffuse and rough env reflection terms
[[nodiscard]] Mathematics::LinearRGB EvaluateSkyAverage(const SkyGradient& Sky) noexcept
{
    return LerpColor(Sky.HorizonRadiance, Sky.ZenithRadiance, 0.55f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHADING COEFFICIENTS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] float EvaluateGgxDistribution(float NormalDotHalf, float Alpha) noexcept
{
    const float AlphaSquared = Alpha * Alpha;
    const float Denominator  = NormalDotHalf * NormalDotHalf * (AlphaSquared - 1.0f) + 1.0f;
    return AlphaSquared / std::max(1.0e-6f, PiFloat * Denominator * Denominator);
}

[[nodiscard]] float EvaluateSmithMasking(float NormalDotDirection, float K) noexcept
{
    return NormalDotDirection / std::max(1.0e-6f, NormalDotDirection * (1.0f - K) + K);
}

[[nodiscard]] float EvaluateSchlickFresnel(float CosineTheta, float IncidentReflectance) noexcept
{
    const float Gap = std::clamp(1.0f - CosineTheta, 0.0f, 1.0f);
    const float Gap5 = Gap * Gap * Gap * Gap * Gap;
    return IncidentReflectance + (1.0f - IncidentReflectance) * Gap5;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  VEHICLE STREAM INTAKE
//------------------------------------------------------------------------------------------------------------------------

struct VehicleMaterialSlot
{
    uint32_t             SlotIdentifier = 0u;                 // [idx] - FCT1 material slot identifier
    const char*          Caption        { "VehicleSlot" };    // [-]   - Human-readable slot caption
    Mathematics::Axis3D  Albedo         { 0.8f, 0.8f, 0.8f }; // [-]   - Linear Rec.709 base reflectance
    float                Roughness      = 0.5f;               // [-]   - Isotropic GGX alpha root
    float                Metallic       = 0.0f;               // [-]   - Conductor fraction
    float                CoatWeight     = 0.0f;               // [-]   - Clearcoat presence
    float                EmissionNits   = 0.0f;               // [cd/m2] - Explicit luminous exitance
    Mathematics::LinearRGB EmissionTint { 1.0f, 1.0f, 1.0f }; // [-]   - Spectral filter over emission
};

// 📝 Mirrors IndexedTriangleStreamInterchange::VehicleMaterialSlots with showcase-grade coat and emission tuning
constexpr std::array<VehicleMaterialSlot, 10u> VehicleMaterialSlots {{
    {  6u, "BodyCoat",       { 0.680f, 0.800f, 0.000f }, 0.20f, 0.00f, 1.00f, 0.0f, { 1.0f, 1.0f, 1.0f } },
    {  7u, "VehicleGlass",   { 0.026f, 0.028f, 0.030f }, 0.05f, 0.00f, 0.00f, 0.0f, { 1.0f, 1.0f, 1.0f } },
    {  8u, "VehiclePlastic", { 0.012f, 0.014f, 0.018f }, 0.50f, 0.00f, 0.00f, 0.0f, { 1.0f, 1.0f, 1.0f } },
    {  9u, "Tyre",           { 0.037f, 0.037f, 0.037f }, 0.76f, 0.00f, 0.00f, 0.0f, { 1.0f, 1.0f, 1.0f } },
    { 10u, "WheelCenter",            { 0.520f, 0.535f, 0.560f }, 0.24f, 0.92f, 0.00f, 0.0f, { 1.0f, 1.0f, 1.0f } },
    { 11u, "Brake",          { 0.290f, 0.070f, 0.022f }, 0.33f, 0.55f, 0.00f, 0.0f, { 1.0f, 1.0f, 1.0f } },
    { 12u, "VehicleTrim",    { 0.048f, 0.050f, 0.055f }, 0.46f, 0.00f, 0.00f, 0.0f, { 1.0f, 1.0f, 1.0f } },
    { 13u, "Headlight",      { 0.070f, 0.076f, 0.088f }, 0.14f, 0.00f, 0.55f, 1.6f, { 0.90f, 0.95f, 1.00f } },
    { 14u, "Taillight",      { 0.240f, 0.018f, 0.020f }, 0.12f, 0.00f, 0.35f, 4.0f, { 1.0f, 0.06f, 0.03f } },
    { 15u, "VehicleDefault", { 0.115f, 0.120f, 0.130f }, 0.48f, 0.10f, 0.15f, 0.0f, { 1.0f, 1.0f, 1.0f } },
}};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FCT1 STREAM DECODING
//------------------------------------------------------------------------------------------------------------------------

struct Fct1DecodeTarget
{
    std::vector<SpatialAcceleration::RayTrianglePrimitive>  Primitives    {}; // [-] - Accumulated triangle stream
    std::vector<SpatialAcceleration::TriangleSmoothNormals> SmoothNormals {}; // [-] - Aligned smooth corner normals
};

[[nodiscard]] uint32_t ReadLittleEndian32(const std::byte* Bytes) noexcept
{
    uint32_t Value = 0u;
    std::memcpy(&Value, Bytes, sizeof(Value));
    return Value;
}

[[nodiscard]] float ReadLittleEndianFloat(const std::byte* Bytes) noexcept
{
    float Value = 0.0f;
    std::memcpy(&Value, Bytes, sizeof(Value));
    return Value;
}

// 📝 Decodes one FCT1 stream (48 B header, positions, 3x u32 indices, per-triangle material slot) into world triangles
[[nodiscard]] bool DecodeFct1Stream(
    const std::filesystem::path&                    SourcePath,
    const Mathematics::Axis3D&                      Translation,
    bool                                            MirrorAcrossVerticalAxis,
    const MaterialSpace::CompactMaterialRegister&   Materials,
    const std::unordered_map<uint32_t, uint16_t>&   SlotMaterialOrdinal,
    Fct1DecodeTarget&                               Target)
{
    std::ifstream Stream(SourcePath, std::ios::binary);
    if (!Stream)
    {
        return false;
    }
    std::vector<char> RawBytes((std::istreambuf_iterator<char>(Stream)), std::istreambuf_iterator<char>());
    if (RawBytes.size() < 48u)
    {
        return false;
    }
    std::vector<std::byte> Bytes(RawBytes.size());
    std::memcpy(Bytes.data(), RawBytes.data(), RawBytes.size());

    if (static_cast<char>(Bytes[0]) != 'F' || static_cast<char>(Bytes[1]) != 'C' || static_cast<char>(Bytes[2]) != 'T' || static_cast<char>(Bytes[3]) != '1')
    {
        return false;
    }

    const uint32_t VertexCount   = ReadLittleEndian32(Bytes.data() + 4u);
    const uint32_t TriangleCount = ReadLittleEndian32(Bytes.data() + 8u);
    const uint32_t StreamMask    = ReadLittleEndian32(Bytes.data() + 12u);
    const std::size_t PositionBytes = static_cast<std::size_t>(VertexCount) * 12u;
    const std::size_t IndexBytes    = static_cast<std::size_t>(TriangleCount) * 12u;
    const std::size_t SlotBytes     = static_cast<std::size_t>(TriangleCount) * 4u;
    const std::size_t ExtraBytes    = ((StreamMask & 1u) != 0u) ? static_cast<std::size_t>(TriangleCount) * 36u : 0u;
    if (VertexCount == 0u || TriangleCount == 0u || Bytes.size() < 48u + PositionBytes + IndexBytes + SlotBytes + ExtraBytes)
    {
        return false;
    }

    const std::byte* PositionStart = Bytes.data() + 48u;
    const std::byte* IndexStart    = PositionStart + PositionBytes;
    const std::byte* SlotStart     = IndexStart + IndexBytes;

    std::vector<Mathematics::Axis3D> Positions(VertexCount);
    for (uint32_t Vertex = 0u; Vertex < VertexCount; ++Vertex)
    {
        Mathematics::Axis3D Point {
            ReadLittleEndianFloat(PositionStart + static_cast<std::size_t>(Vertex) * 12u + 0u),
            ReadLittleEndianFloat(PositionStart + static_cast<std::size_t>(Vertex) * 12u + 4u),
            ReadLittleEndianFloat(PositionStart + static_cast<std::size_t>(Vertex) * 12u + 8u),
        };
        if (MirrorAcrossVerticalAxis)
        {
            Point.X = -Point.X;
            Point.Y = -Point.Y;
        }
        Positions[Vertex] = Add(Point, Translation);
    }

    // 📝 Area-weighted vertex normals give the low-poly hull and the round wheels a smooth, photographic silhouette
    std::vector<Mathematics::Axis3D> AccumulatedNormals(VertexCount, Mathematics::Axis3D { 0.0f, 0.0f, 0.0f });
    for (uint32_t Triangle = 0u; Triangle < TriangleCount; ++Triangle)
    {
        const uint32_t CornerA = ReadLittleEndian32(IndexStart + static_cast<std::size_t>(Triangle) * 12u + 0u);
        const uint32_t CornerB = ReadLittleEndian32(IndexStart + static_cast<std::size_t>(Triangle) * 12u + 4u);
        const uint32_t CornerC = ReadLittleEndian32(IndexStart + static_cast<std::size_t>(Triangle) * 12u + 8u);
        if (CornerA >= VertexCount || CornerB >= VertexCount || CornerC >= VertexCount)
        {
            continue;
        }
        const Mathematics::Axis3D FaceNormal = Cross(Sub(Positions[CornerB], Positions[CornerA]), Sub(Positions[CornerC], Positions[CornerA]));
        AccumulatedNormals[CornerA] = Add(AccumulatedNormals[CornerA], FaceNormal);
        AccumulatedNormals[CornerB] = Add(AccumulatedNormals[CornerB], FaceNormal);
        AccumulatedNormals[CornerC] = Add(AccumulatedNormals[CornerC], FaceNormal);
    }

    for (uint32_t Triangle = 0u; Triangle < TriangleCount; ++Triangle)
    {
        const uint32_t CornerA = ReadLittleEndian32(IndexStart + static_cast<std::size_t>(Triangle) * 12u + 0u);
        const uint32_t CornerB = ReadLittleEndian32(IndexStart + static_cast<std::size_t>(Triangle) * 12u + 4u);
        const uint32_t CornerC = ReadLittleEndian32(IndexStart + static_cast<std::size_t>(Triangle) * 12u + 8u);
        if (CornerA >= VertexCount || CornerB >= VertexCount || CornerC >= VertexCount || CornerA == CornerB || CornerB == CornerC || CornerA == CornerC)
        {
            continue;
        }

        const uint32_t SlotIdentifier = ReadLittleEndian32(SlotStart + static_cast<std::size_t>(Triangle) * 4u);
        const auto SlotEntry = SlotMaterialOrdinal.find(SlotIdentifier);
        const uint16_t MaterialOrdinal = (SlotEntry != SlotMaterialOrdinal.end()) ? SlotEntry->second : 0u;

        const Mathematics::Axis3D& PointA = Positions[CornerA];
        const Mathematics::Axis3D& PointB = Positions[CornerB];
        const Mathematics::Axis3D& PointC = Positions[CornerC];

        SpatialAcceleration::RayTrianglePrimitive Primitive {};
        Primitive.Corner0          = PointA;
        Primitive.Edge01           = Sub(PointB, PointA);
        Primitive.Edge02           = Sub(PointC, PointA);
        Primitive.MaterialOrdinal  = MaterialOrdinal;
        Primitive.OpaqueMask       = 1u;
        Primitive.SourceFaceIndex  = Triangle;
        Primitive.PrimitiveOrdinal = static_cast<uint32_t>(Target.Primitives.size());

        SpatialAcceleration::TriangleSmoothNormals CornerNormals {};
        CornerNormals.Normal0 = Normalize(AccumulatedNormals[CornerA], Normalize(Cross(Primitive.Edge01, Primitive.Edge02), { 0.0f, 0.0f, 1.0f }));
        CornerNormals.Normal1 = Normalize(AccumulatedNormals[CornerB], CornerNormals.Normal0);
        CornerNormals.Normal2 = Normalize(AccumulatedNormals[CornerC], CornerNormals.Normal0);

        Target.Primitives.push_back(Primitive);
        Target.SmoothNormals.push_back(CornerNormals);
    }

    (void)Materials;
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHOWCASE TURNTABLE VIEW
//------------------------------------------------------------------------------------------------------------------------

struct TurntableView
{
    float        YawDegrees      = 0.0f;  // [deg] - Orbit azimuth around +Z (90 deg looks at the vehicle nose)
    float        PitchDegrees    = 12.0f; // [deg] - Orbit elevation above the studio floor
    float        DistanceScale   = 1.0f;  // [-]   - Multiplier over the framing distance
    float        FocusForward    = 0.0f;  // [m]   - Additional focal offset along +X (vehicle nose)
    const char*  Caption         { "View" }; // [-] - Contact sheet caption
};

constexpr std::array<TurntableView, 10u> TurntableViews {{
    {  90.0f, 12.0f, 1.00f, 0.0f, "01 FRONT" },
    { 135.0f, 18.0f, 1.03f, 0.0f, "02 FRONT-LEFT 3/4" },
    { 180.0f,  9.0f, 1.00f, 0.0f, "03 LEFT PROFILE" },
    { 225.0f, 17.0f, 1.03f, 0.0f, "04 REAR-LEFT 3/4" },
    { 270.0f, 12.0f, 1.00f, 0.0f, "05 REAR" },
    { 315.0f, 17.0f, 1.03f, 0.0f, "06 REAR-RIGHT 3/4" },
    {   0.0f,  9.0f, 1.00f, 0.0f, "07 RIGHT PROFILE" },
    {  45.0f, 18.0f, 1.03f, 0.0f, "08 FRONT-RIGHT 3/4" },
    { 120.0f, 52.0f, 0.95f, 0.0f, "09 HIGH TOP" },
    { 118.0f, 15.0f, 0.72f, 0.95f, "10 FRONT DETAIL" },
}};

//------------------------------------------------------------------------------------------------------------------------
//                                                  RENDER SETTINGS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Selects which deterministic global-illumination candidate feeds the indirect term
enum class GiMethodSelection : uint8_t
{
    DirectOnly      = 0u, // [-] - Sun, sky ambient, AO and environment specular only (single bounce)
    StokesSh9       = 1u, // [-] - SH9 cosine-convolved multi-bounce radiance gathering
    SurfelCascade   = 2u, // [-] - Visibility-gated three-scale surfel cascade (DDGI)
    PathTrace       = 3u, // [-] - Cosine path-traced multi-bounce reference
};

struct RenderSettings
{
    uint32_t Width             = DefaultImageWidth;    // [px]   - Output width
    uint32_t Height            = DefaultImageHeight;   // [px]   - Output height
    uint32_t PrimarySamples    = DefaultPrimarySamples; // [rays] - R2 stratified camera rays per pixel
    uint32_t SunSamples        = DefaultSunSamples;    // [rays] - Solar disc samples per shaded hit
    float    Exposure          = 1.02f;                // [-]    - Pre-curve exposure multiplier
    GiMethodSelection GiMethod = GiMethodSelection::SurfelCascade; // [-] - Indirect GI candidate
    uint32_t GiDownsampleFactor = 4u;                  // [cnt]  - Linear GI resolution divisor (1 = full res)
    uint32_t GiSamplesPerPixel  = 24u;                 // [rays] - GI sample budget per GI pixel
    uint32_t GiBounceLimit      = 3u;                  // [cnt]  - Diffuse bounce depth
    float    GiWeight           = 1.0f;                // [-]    - Indirect radiance blend weight
    bool     StudioEnclosure    = false;               // [-]    - Add Cornell-style coloured bounce panels
    bool     GiComparisonRun    = false;               // [-]    - Also emit the four-method GI comparison sheet
};

[[nodiscard]] const char* QueryGiMethodCaption(GiMethodSelection Method) noexcept
{
    switch (Method)
    {
        case GiMethodSelection::DirectOnly:    return "DIRECT ONLY";
        case GiMethodSelection::StokesSh9:     return "SH9 MULTI-BOUNCE";
        case GiMethodSelection::SurfelCascade: return "SURFEL CASCADE DDGI";
        case GiMethodSelection::PathTrace:     return "PATH-TRACED";
    }
    return "DIRECT ONLY";
}

// 📝 Maps the exhibit selector onto the library candidate list
[[nodiscard]] GeometricRaster::EmissiveGiMethod QueryLibraryGiMethod(GiMethodSelection Method) noexcept
{
    switch (Method)
    {
        case GiMethodSelection::StokesSh9:     return GeometricRaster::EmissiveGiMethod::StokesSh9MultiBounce;
        case GiMethodSelection::SurfelCascade: return GeometricRaster::EmissiveGiMethod::SurfelCascadeDdgi;
        case GiMethodSelection::PathTrace:     return GeometricRaster::EmissiveGiMethod::DeterministicPathTrace;
        case GiMethodSelection::DirectOnly:    break;
    }
    return GeometricRaster::EmissiveGiMethod::SurfelCascadeDdgi;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SURFACE SHADING
//------------------------------------------------------------------------------------------------------------------------

struct SurfaceShadingParameters
{
    const SpatialAcceleration::WideBoundingTree*  Tree      = nullptr; // [-] - Scene acceleration tree
    const MaterialSpace::CompactMaterialRegister* Materials = nullptr; // [-] - Material register
    const GeometricRaster::SolarDirectionalSource* Sun      = nullptr; // [-] - Solar directional source
    const SkyGradient*                            Sky       = nullptr; // [-] - Sky radiance gradient
    Mathematics::LinearRGB                        IndirectGiRadiance { 0.0f, 0.0f, 0.0f }; // [cd/m2] - Bounced diffuse GI from the selected candidate
    bool                                          GiActive  = false;   // [-] - True when a GI candidate supplied the indirect term
    float                                         BouncedBlend = 1.0f; // [-] - Multiplier on the bounced diffuse return
};

[[nodiscard]] Mathematics::LinearRGB ShadeSurface(
    const SurfaceShadingParameters&     Parameters,
    const Mathematics::Axis3D& HitPosition,
    Mathematics::Axis3D        SurfaceNormal,
    const Mathematics::Axis3D& ViewDirection,
    uint16_t                   MaterialOrdinal,
    uint32_t                   PixelSeed,
    uint32_t                   SampleOrdinal)
{
    const MaterialSpace::CompactMaterialRegister& Materials = *Parameters.Materials;
    const SpatialAcceleration::WideBoundingTree&  Tree      = *Parameters.Tree;
    const GeometricRaster::SolarDirectionalSource& Sun      = *Parameters.Sun;
    const SkyGradient&                            Sky       = *Parameters.Sky;

    const MaterialSpace::SurfaceMaterial& Authored = Materials.InspectAuthoredMaterial(MaterialOrdinal);
    const Mathematics::LinearRGB BaseAlbedo = Authored.Diffuse.BaseColor;
    const float Metalness      = std::clamp(Authored.Specular.Metalness, 0.0f, 1.0f);
    const float Roughness      = std::clamp(Authored.Specular.Roughness, 0.03f, 1.0f);
    const float SpecularWeight = std::clamp(Authored.Specular.SpecularWeight, 0.0f, 1.0f);
    const float CoatWeight     = std::clamp(Authored.Clearcoat.CoatWeight, 0.0f, 1.0f);

    const float NormalDotView = Dot(SurfaceNormal, ViewDirection);
    if (NormalDotView < 0.0f)
    {
        SurfaceNormal = Mul(SurfaceNormal, -1.0f);
    }
    const float ClampedNormalDotView = std::max(Dot(SurfaceNormal, ViewDirection), 1.0e-4f);

    const Mathematics::LinearRGB DielectricReflectance { 0.04f * SpecularWeight, 0.04f * SpecularWeight, 0.04f * SpecularWeight };
    const Mathematics::LinearRGB IncidentReflectance = LerpColor(DielectricReflectance, BaseAlbedo, Metalness);
    const Mathematics::LinearRGB DiffuseAlbedo {
        BaseAlbedo.R * (1.0f - Metalness) * Authored.Diffuse.BaseWeight,
        BaseAlbedo.G * (1.0f - Metalness) * Authored.Diffuse.BaseWeight,
        BaseAlbedo.B * (1.0f - Metalness) * Authored.Diffuse.BaseWeight,
    };

    const Mathematics::Axis3D SunDirection = Normalize(Sun.DirectionToSun, { 0.0f, 0.0f, 1.0f });
    const float NormalDotSun = std::max(0.0f, Dot(SurfaceNormal, SunDirection));

    // ---- Direct solar visibility over the solar disc (deterministic penumbra) ----
    float SolarVisibility = 0.0f;
    if (NormalDotSun > 1.0e-4f)
    {
        const Mathematics::Axis3D TangentReference = (std::fabs(SunDirection.Z) < 0.95f) ? Mathematics::Axis3D { 0.0f, 0.0f, 1.0f } : Mathematics::Axis3D { 1.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D TangentX = Normalize(Cross(SunDirection, TangentReference));
        const Mathematics::Axis3D TangentY = Cross(SunDirection, TangentX);
        const float DiscRadius = std::max(Sun.SunAngularRadiusRadians, 1.0e-4f);
        const uint32_t DiscSamples = std::max(Parameters.Sun->RtxSamplesPerPixel > 0u ? std::min(Parameters.Sun->RtxSamplesPerPixel, 4u) : 1u, 1u);
        const uint32_t DiscCount = std::max(1u, std::min<uint32_t>(DiscSamples, 4u));

        for (uint32_t DiscSample = 0u; DiscSample < DiscCount; ++DiscSample)
        {
            const Mathematics::DeterministicSample2D Disc = Mathematics::EvaluateDeterministicR2Sample(PixelSeed, SampleOrdinal * 7u + DiscSample, 3u);
            const float Radius = DiscRadius * std::sqrt(std::clamp(Disc.X, 0.0f, 1.0f));
            const float Angle  = 2.0f * PiFloat * Disc.Y;
            const Mathematics::Axis3D JitteredDirection = Normalize(
                Add(SunDirection, Add(Mul(TangentX, Radius * std::cos(Angle)), Mul(TangentY, Radius * std::sin(Angle)))),
                SunDirection);

            SpatialAcceleration::RaySegmentQuery ShadowRay {};
            ShadowRay.Origin          = Add(HitPosition, Mul(SurfaceNormal, 0.0025f));
            ShadowRay.UnitDirection   = JitteredDirection;
            ShadowRay.MinimumDistance = 1.0e-3f;
            ShadowRay.MaximumDistance = 120.0f;
            if (!Tree.QueryRayOccluded(ShadowRay))
            {
                SolarVisibility += 1.0f / static_cast<float>(DiscCount);
            }
        }
    }

    // ---- Direct diffuse and microfacet specular ----
    Mathematics::LinearRGB DirectRadiance { 0.0f, 0.0f, 0.0f };
    if (SolarVisibility > 0.0f && NormalDotSun > 0.0f)
    {
        const float InvPi = 0.31830988618f;
        const Mathematics::Axis3D HalfVector = Normalize(Add(SunDirection, ViewDirection), SunDirection);
        const float NormalDotHalf = std::max(0.0f, Dot(SurfaceNormal, HalfVector));
        const float HalfDotView   = std::max(0.0f, Dot(HalfVector, ViewDirection));
        const float Alpha         = Roughness * Roughness;
        const float Distribution  = EvaluateGgxDistribution(NormalDotHalf, Alpha);
        const float K             = (Roughness + 1.0f) * (Roughness + 1.0f) / 8.0f;
        const float Geometric     = EvaluateSmithMasking(std::max(NormalDotSun, 1.0e-4f), K) * EvaluateSmithMasking(ClampedNormalDotView, K);
        const float SpecularScale = SolarVisibility * Distribution * Geometric / std::max(4.0f * NormalDotSun * ClampedNormalDotView, 1.0e-4f);
        const float FresnelValue  = EvaluateSchlickFresnel(HalfDotView, 0.04f);
        // 📝 Conductor lobe keeps the metal tint; dielectric lobe uses the scalar Fresnel weight
        const Mathematics::LinearRGB SpecularFresnel = LerpColor(
            { FresnelValue, FresnelValue, FresnelValue },
            { IncidentReflectance.R * FresnelValue, IncidentReflectance.G * FresnelValue, IncidentReflectance.B * FresnelValue },
            Metalness);

        const Mathematics::LinearRGB DiffuseTerm {
            DiffuseAlbedo.R * InvPi * NormalDotSun,
            DiffuseAlbedo.G * InvPi * NormalDotSun,
            DiffuseAlbedo.B * InvPi * NormalDotSun,
        };

        DirectRadiance = {
            Sun.SolarIrradiance.R * (DiffuseTerm.R + SpecularFresnel.R * SpecularScale) * (1.0f - CoatWeight * 0.35f),
            Sun.SolarIrradiance.G * (DiffuseTerm.G + SpecularFresnel.G * SpecularScale) * (1.0f - CoatWeight * 0.35f),
            Sun.SolarIrradiance.B * (DiffuseTerm.B + SpecularFresnel.B * SpecularScale) * (1.0f - CoatWeight * 0.35f),
        };
    }

    // ---- Ambient sky diffuse with a short ambient-occlusion ray ----
    float OcclusionTrace = 0.0f;
    {
        constexpr uint32_t OcclusionRayCount = 2u;
        const Mathematics::Axis3D TangentReference = (std::fabs(SurfaceNormal.Z) < 0.95f) ? Mathematics::Axis3D { 0.0f, 0.0f, 1.0f } : Mathematics::Axis3D { 1.0f, 0.0f, 0.0f };
        const Mathematics::Axis3D TangentX = Normalize(Cross(SurfaceNormal, TangentReference));
        const Mathematics::Axis3D TangentY = Cross(SurfaceNormal, TangentX);
        for (uint32_t OcclusionOrdinal = 0u; OcclusionOrdinal < OcclusionRayCount; ++OcclusionOrdinal)
        {
            const Mathematics::DeterministicSample2D AmbientSample = Mathematics::EvaluateDeterministicR2Sample(PixelSeed, SampleOrdinal * 11u + 5u + OcclusionOrdinal, 5u);
            const float Radius = std::sqrt(std::clamp(AmbientSample.X, 0.0f, 1.0f));
            const float Angle  = 2.0f * PiFloat * AmbientSample.Y;
            const Mathematics::Axis3D AmbientDirection = Normalize(
                Add(Mul(SurfaceNormal, std::sqrt(std::max(0.0f, 1.0f - AmbientSample.X))),
                    Add(Mul(TangentX, Radius * std::cos(Angle)), Mul(TangentY, Radius * std::sin(Angle)))),
                SurfaceNormal);

            SpatialAcceleration::RaySegmentQuery OcclusionRay {};
            OcclusionRay.Origin          = Add(HitPosition, Mul(SurfaceNormal, 0.0025f));
            OcclusionRay.UnitDirection   = AmbientDirection;
            OcclusionRay.MinimumDistance = 1.0e-3f;
            OcclusionRay.MaximumDistance = 0.42f;
            OcclusionTrace += Tree.QueryRayOccluded(OcclusionRay) ? 0.42f : 1.0f;
        }
        OcclusionTrace /= static_cast<float>(OcclusionRayCount);
    }

    const float HemisphereBlend = std::clamp(0.5f + 0.5f * SurfaceNormal.Z, 0.0f, 1.0f);
    const Mathematics::LinearRGB AmbientRadiance = EvaluateSkyAverage(Sky);

    // 📝 With a GI candidate active the multi-bounce answer replaces the hemispherical sky fill
    Mathematics::LinearRGB AmbientDiffuse { 0.0f, 0.0f, 0.0f };
    if (Parameters.GiActive)
    {
        // 📝 The candidate output already carries the receiver albedo, so it enters the sum unscaled
        const float Blend = std::clamp(Parameters.BouncedBlend, 0.0f, 1.0f);
        AmbientDiffuse = {
            Parameters.IndirectGiRadiance.R * Blend,
            Parameters.IndirectGiRadiance.G * Blend,
            Parameters.IndirectGiRadiance.B * Blend,
        };
    }
    else
    {
        const float AmbientScale = (0.30f + 0.55f * HemisphereBlend) * OcclusionTrace;
        AmbientDiffuse = {
            DiffuseAlbedo.R * AmbientRadiance.R * AmbientScale,
            DiffuseAlbedo.G * AmbientRadiance.G * AmbientScale,
            DiffuseAlbedo.B * AmbientRadiance.B * AmbientScale,
        };
    }

    // ---- Environment specular reflection (sky gradient mirror lobe) ----
    const Mathematics::Axis3D ReflectionDirection = Normalize(
        Sub(Mul(SurfaceNormal, 2.0f * ClampedNormalDotView), ViewDirection),
        SurfaceNormal);
    Mathematics::LinearRGB EnvironmentRadiance = EvaluateSkyRadiance(Sky, ReflectionDirection);
    EnvironmentRadiance = LerpColor(EnvironmentRadiance, AmbientRadiance, std::clamp(Roughness * 1.35f, 0.0f, 1.0f));

    const float FresnelView = EvaluateSchlickFresnel(ClampedNormalDotView, 0.04f);
    const Mathematics::LinearRGB FresnelTerm = LerpColor(
        { FresnelView, FresnelView, FresnelView },
        IncidentReflectance,
        Metalness);
    const float EnvironmentWeight = (0.30f + 0.70f * (1.0f - Roughness)) * (0.5f + 0.5f * Metalness);

    Mathematics::LinearRGB EnvironmentSpecular {
        EnvironmentRadiance.R * FresnelTerm.R * EnvironmentWeight,
        EnvironmentRadiance.G * FresnelTerm.G * EnvironmentWeight,
        EnvironmentRadiance.B * FresnelTerm.B * EnvironmentWeight,
    };

    // ---- Clearcoat lobe: sharp additional Fresnel reflection ----
    if (CoatWeight > 0.01f)
    {
        const float CoatFresnel = EvaluateSchlickFresnel(ClampedNormalDotView, 0.04f);
        const Mathematics::LinearRGB CoatReflection = EvaluateSkyRadiance(Sky, ReflectionDirection);
        EnvironmentSpecular.R += CoatReflection.R * CoatFresnel * CoatWeight * 0.42f;
        EnvironmentSpecular.G += CoatReflection.G * CoatFresnel * CoatWeight * 0.42f;
        EnvironmentSpecular.B += CoatReflection.B * CoatFresnel * CoatWeight * 0.42f;

        if (SolarVisibility > 0.0f && NormalDotSun > 0.0f && CoatWeight > 0.5f)
        {
            const float CoatShininess = 220.0f;
            const float CoatPower = std::pow(std::max(0.0f, Dot(SurfaceNormal, Normalize(Add(SunDirection, ViewDirection), SunDirection))), CoatShininess);
            const float CoatHighlight = CoatPower * (CoatShininess + 8.0f) / (8.0f * PiFloat) * NormalDotSun * SolarVisibility * CoatWeight;
            EnvironmentSpecular.R += Sun.SolarIrradiance.R * CoatHighlight * 0.06f;
            EnvironmentSpecular.G += Sun.SolarIrradiance.G * CoatHighlight * 0.06f;
            EnvironmentSpecular.B += Sun.SolarIrradiance.B * CoatHighlight * 0.06f;
        }
    }

    // ---- Lobe 10B luminous exitance, shared with the GI candidates so bounce agrees with direct ----
    const Mathematics::LinearRGB EmissionRadiance =
        MaterialSpace::Scattering::EvaluatePlanckianBlackbodyRadiance(Authored.PorosityLight);

    return {
        DirectRadiance.R + AmbientDiffuse.R + EnvironmentSpecular.R + EmissionRadiance.R,
        DirectRadiance.G + AmbientDiffuse.G + EnvironmentSpecular.G + EmissionRadiance.G,
        DirectRadiance.B + AmbientDiffuse.B + EnvironmentSpecular.B + EmissionRadiance.B,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                        GLOBAL ILLUMINATION CANDIDATE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

struct GiFrameBuffer
{
    uint32_t                            WidthPx          = 0u; // [px]    - Reduced GI raster width
    uint32_t                            HeightPx         = 0u; // [px]    - Reduced GI raster height
    std::vector<Mathematics::LinearRGB> IndirectRadiance {};   // [cd/m2] - Diffuse indirect radiance per GI pixel
    GeometricRaster::EmissiveGiMetrics  Metrics          {};   // [-]     - Ray, surfel, memory and timing telemetry
    bool                                Populated        = false; // [-]   - True when a candidate produced values
    float                               MeanLuminance    = 0.0f;  // [cd/m2] - Mean indirect luminance over covered pixels
    float                               PeakLuminance    = 0.0f;  // [cd/m2] - Peak indirect luminance
    uint32_t                            CoveredPixelCount = 0u;   // [count] - Pixels receiving non-zero indirect light
};

// 📝 Reconstructs the resolved surface samples the GI library consumes straight from the BVH4 primary hits
[[nodiscard]] GiFrameBuffer EvaluateGiFrameBuffer(
    const SpatialAcceleration::WideBoundingTree&    Tree,
    const MaterialSpace::CompactMaterialRegister&   Materials,
    const GeometricRaster::SolarDirectionalSource&  Sun,
    const SkyGradient&                              Sky,
    const CameraBasis&                              Camera,
    const RenderSettings&                           Settings)
{
    GiFrameBuffer Buffer {};
    Buffer.WidthPx  = std::max(2u, Settings.Width / std::max(1u, Settings.GiDownsampleFactor));
    Buffer.HeightPx = std::max(2u, Settings.Height / std::max(1u, Settings.GiDownsampleFactor));
    Buffer.IndirectRadiance.assign(static_cast<std::size_t>(Buffer.WidthPx) * Buffer.HeightPx, { 0.0f, 0.0f, 0.0f });

    std::vector<GeometricRaster::AnalyticResolvedSurfaceSample> PrimarySamples(
        static_cast<std::size_t>(Buffer.WidthPx) * Buffer.HeightPx);

    for (uint32_t Row = 0u; Row < Buffer.HeightPx; ++Row)
    {
        for (uint32_t Column = 0u; Column < Buffer.WidthPx; ++Column)
        {
            const float NdcX = ((static_cast<float>(Column) + 0.5f) / static_cast<float>(Buffer.WidthPx)) * 2.0f - 1.0f;
            const float NdcY = 1.0f - ((static_cast<float>(Row) + 0.5f) / static_cast<float>(Buffer.HeightPx)) * 2.0f;
            const Mathematics::Axis3D RayDirection = Normalize(
                Add(Add(Camera.Forward, Mul(Camera.Right, NdcX * Camera.TanHalfFovX)), Mul(Camera.Up, NdcY * Camera.TanHalfFovY)),
                Camera.Forward);

            SpatialAcceleration::RaySegmentQuery PrimaryRay {};
            PrimaryRay.Origin          = Camera.Origin;
            PrimaryRay.UnitDirection   = RayDirection;
            PrimaryRay.MinimumDistance = Camera.NearClip;
            PrimaryRay.MaximumDistance = Camera.FarClip;
            const SpatialAcceleration::RayIntersectionResult Hit = Tree.TraceClosestRay(PrimaryRay, false);
            if (!Hit.HitConfirmed)
            {
                continue;
            }

            Mathematics::Axis3D ShadingNormal = Hit.GeometricNormal;
            if (Tree.PrimitiveNormals.size() == Tree.Primitives.size() && Hit.PrimitiveIndex < Tree.PrimitiveNormals.size())
            {
                const SpatialAcceleration::TriangleSmoothNormals& CornerNormals = Tree.PrimitiveNormals[Hit.PrimitiveIndex];
                const float Weight0 = std::max(0.0f, 1.0f - Hit.BarycentricU - Hit.BarycentricV);
                const float Weight1 = std::max(0.0f, Hit.BarycentricU);
                const float Weight2 = std::max(0.0f, Hit.BarycentricV);
                ShadingNormal = Normalize(
                    Add(Add(Mul(CornerNormals.Normal0, Weight0), Mul(CornerNormals.Normal1, Weight1)), Mul(CornerNormals.Normal2, Weight2)),
                    Hit.GeometricNormal);
            }

            GeometricRaster::AnalyticResolvedSurfaceSample& Sample =
                PrimarySamples[static_cast<std::size_t>(Row) * Buffer.WidthPx + Column];
            Sample.ValidHit           = true;
            Sample.MaterialSlot       = Hit.MaterialOrdinal;
            Sample.TriangleOrdinal    = Hit.PrimitiveIndex;
            Sample.WorldPosition      = Add(Camera.Origin, Mul(RayDirection, Hit.HitDistanceMeters));
            Sample.WorldViewDirection = Mul(RayDirection, -1.0f);
            Sample.BarycentricWeights = { 1.0f - Hit.BarycentricU - Hit.BarycentricV, Hit.BarycentricU, Hit.BarycentricV };

            const Mathematics::Axis3D TangentReference = (std::fabs(ShadingNormal.Z) < 0.95f)
                ? Mathematics::Axis3D { 0.0f, 0.0f, 1.0f }
                : Mathematics::Axis3D { 1.0f, 0.0f, 0.0f };
            Sample.TangentFrame.TangentX   = Normalize(Cross(ShadingNormal, TangentReference), { 1.0f, 0.0f, 0.0f });
            Sample.TangentFrame.BitangentY = Cross(ShadingNormal, Sample.TangentFrame.TangentX);
            Sample.TangentFrame.NormalZ    = ShadingNormal;
        }
    }

    GeometricRaster::EmissiveGiSceneView SceneView { Tree, Materials, Sun, {}, {}, EvaluateSkyAverage(Sky) };

    GeometricRaster::EmissiveGiSettings GiSettings {};
    GiSettings.SamplesPerPixel       = std::clamp(Settings.GiSamplesPerPixel, 4u, 128u);
    GiSettings.BounceLimit           = std::clamp(Settings.GiBounceLimit, 1u, 6u);
    GiSettings.MaximumTraceDistance  = 150.0f;

    GeometricRaster::EmissiveGiCandidateSequence Sequence {};
    GeometricRaster::EmissiveGiOutput Output = Sequence.Evaluate(
        QueryLibraryGiMethod(Settings.GiMethod),
        PrimarySamples,
        Buffer.WidthPx,
        Buffer.HeightPx,
        SceneView,
        GiSettings);

    Buffer.IndirectRadiance = std::move(Output.IndirectRadiance);
    Buffer.Metrics          = Output.Metrics;
    Buffer.Populated        = true;

    double LuminanceSum = 0.0;
    double LuminancePeak = 0.0;
    uint64_t CoveredPixels = 0ull;
    for (const Mathematics::LinearRGB& Radiance : Buffer.IndirectRadiance)
    {
        const double Luminance = 0.2126 * Radiance.R + 0.7152 * Radiance.G + 0.0722 * Radiance.B;
        if (Luminance <= 0.0)
        {
            continue;
        }
        LuminanceSum += Luminance;
        LuminancePeak = std::max(LuminancePeak, Luminance);
        ++CoveredPixels;
    }
    Buffer.MeanLuminance = (CoveredPixels > 0ull) ? static_cast<float>(LuminanceSum / static_cast<double>(CoveredPixels)) : 0.0f;
    Buffer.PeakLuminance = static_cast<float>(LuminancePeak);
    Buffer.CoveredPixelCount = static_cast<uint32_t>(CoveredPixels);
    return Buffer;
}

// 📝 Bilinear reconstruction of the reduced GI raster onto the full-resolution primary hits
[[nodiscard]] Mathematics::LinearRGB SampleGiBuffer(
    const GiFrameBuffer& Buffer,
    uint32_t             FullWidth,
    uint32_t             FullHeight,
    uint32_t             PixelX,
    uint32_t             PixelY) noexcept
{
    if (!Buffer.Populated || Buffer.IndirectRadiance.empty())
    {
        return { 0.0f, 0.0f, 0.0f };
    }

    const float SampleX = ((static_cast<float>(PixelX) + 0.5f) / static_cast<float>(FullWidth)) * static_cast<float>(Buffer.WidthPx) - 0.5f;
    const float SampleY = ((static_cast<float>(PixelY) + 0.5f) / static_cast<float>(FullHeight)) * static_cast<float>(Buffer.HeightPx) - 0.5f;
    const int32_t X0 = static_cast<int32_t>(std::floor(SampleX));
    const int32_t Y0 = static_cast<int32_t>(std::floor(SampleY));
    const float FracX = SampleX - static_cast<float>(X0);
    const float FracY = SampleY - static_cast<float>(Y0);

    const auto Fetch = [&](int32_t X, int32_t Y) noexcept -> Mathematics::LinearRGB
    {
        const int32_t SafeX = std::clamp(X, 0, static_cast<int32_t>(Buffer.WidthPx) - 1);
        const int32_t SafeY = std::clamp(Y, 0, static_cast<int32_t>(Buffer.HeightPx) - 1);
        return Buffer.IndirectRadiance[static_cast<std::size_t>(SafeY) * Buffer.WidthPx + static_cast<std::size_t>(SafeX)];
    };

    const Mathematics::LinearRGB TopRow    = LerpColor(Fetch(X0, Y0), Fetch(X0 + 1, Y0), FracX);
    const Mathematics::LinearRGB BottomRow = LerpColor(Fetch(X0, Y0 + 1), Fetch(X0 + 1, Y0 + 1), FracX);
    return LerpColor(TopRow, BottomRow, FracY);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   VIEW RENDERER
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<uint8_t> RenderTurntableView(
    const SpatialAcceleration::WideBoundingTree&  Tree,
    const MaterialSpace::CompactMaterialRegister& Materials,
    const GeometricRaster::SolarDirectionalSource& Sun,
    const SkyGradient&                            Sky,
    const CameraBasis&                            Camera,
    const RenderSettings&                         Settings,
    const GiFrameBuffer*                          GiBuffer = nullptr)
{
    const uint32_t Width  = Settings.Width;
    const uint32_t Height = Settings.Height;
    const uint32_t RayCount = std::max(1u, Settings.PrimarySamples);

    std::vector<uint8_t> Rgba(static_cast<std::size_t>(Width) * Height * 4u, 0u);

    SurfaceShadingParameters Parameters {};
    Parameters.Tree          = &Tree;
    Parameters.Materials     = &Materials;
    Parameters.Sun           = &Sun;
    Parameters.Sky           = &Sky;
    Parameters.GiActive      = (GiBuffer != nullptr && GiBuffer->Populated && Settings.GiMethod != GiMethodSelection::DirectOnly);
    Parameters.BouncedBlend  = std::clamp(Settings.GiWeight, 0.0f, 1.0f);

    for (uint32_t Row = 0u; Row < Height; ++Row)
    {
        for (uint32_t Column = 0u; Column < Width; ++Column)
        {
            const uint32_t PixelSeed = Mathematics::ConstructPixelSampleSeed(Column, Row);

            Mathematics::LinearRGB Accumulated { 0.0f, 0.0f, 0.0f };
            for (uint32_t Ray = 0u; Ray < RayCount; ++Ray)
            {
                const Mathematics::DeterministicSample2D Jitter = Mathematics::EvaluateDeterministicR2Sample(PixelSeed, Ray, 0u);

                const float NdcX = ((static_cast<float>(Column) + Jitter.X) / static_cast<float>(Width)) * 2.0f - 1.0f;
                const float NdcY = 1.0f - ((static_cast<float>(Row) + Jitter.Y) / static_cast<float>(Height)) * 2.0f;
                const Mathematics::Axis3D RayDirection = Normalize(
                    Add(Add(Camera.Forward, Mul(Camera.Right, NdcX * Camera.TanHalfFovX)), Mul(Camera.Up, NdcY * Camera.TanHalfFovY)),
                    Camera.Forward);

                SpatialAcceleration::RaySegmentQuery PrimaryRay {};
                PrimaryRay.Origin          = Camera.Origin;
                PrimaryRay.UnitDirection   = RayDirection;
                PrimaryRay.MinimumDistance = Camera.NearClip;
                PrimaryRay.MaximumDistance = Camera.FarClip;
                const SpatialAcceleration::RayIntersectionResult Hit = Tree.TraceClosestRay(PrimaryRay, false);

                Mathematics::LinearRGB Radiance { 0.0f, 0.0f, 0.0f };
                if (Hit.HitConfirmed)
                {
                    const Mathematics::Axis3D HitPosition = Add(Camera.Origin, Mul(RayDirection, Hit.HitDistanceMeters));
                    const Mathematics::Axis3D ViewDirection = Mul(RayDirection, -1.0f);

                    Mathematics::Axis3D InterpolatedNormal = Hit.GeometricNormal;
                    if (Tree.PrimitiveNormals.size() == Tree.Primitives.size() && Hit.PrimitiveIndex < Tree.PrimitiveNormals.size())
                    {
                        const SpatialAcceleration::TriangleSmoothNormals& CornerNormals = Tree.PrimitiveNormals[Hit.PrimitiveIndex];
                        const float Weight0 = std::max(0.0f, 1.0f - Hit.BarycentricU - Hit.BarycentricV);
                        const float Weight1 = std::max(0.0f, Hit.BarycentricU);
                        const float Weight2 = std::max(0.0f, Hit.BarycentricV);
                        InterpolatedNormal = Normalize(
                            Add(Add(Mul(CornerNormals.Normal0, Weight0), Mul(CornerNormals.Normal1, Weight1)), Mul(CornerNormals.Normal2, Weight2)),
                            Hit.GeometricNormal);
                    }

                    if (Parameters.GiActive)
                    {
                        Parameters.IndirectGiRadiance = SampleGiBuffer(*GiBuffer, Width, Height, Column, Row);
                    }

                    Radiance = ShadeSurface(Parameters, HitPosition, InterpolatedNormal, ViewDirection, Hit.MaterialOrdinal, PixelSeed, Ray);
                }
                else
                {
                    Radiance = EvaluateSkyRadiance(Sky, RayDirection);
                }

                Accumulated.R += Radiance.R / static_cast<float>(RayCount);
                Accumulated.G += Radiance.G / static_cast<float>(RayCount);
                Accumulated.B += Radiance.B / static_cast<float>(RayCount);
            }

            const std::size_t Offset = (static_cast<std::size_t>(Row) * Width + Column) * 4u;
            Rgba[Offset + 0u] = EncodeSrgbByte(ApplyFilmicCurve(Accumulated.R * Settings.Exposure));
            Rgba[Offset + 1u] = EncodeSrgbByte(ApplyFilmicCurve(Accumulated.G * Settings.Exposure));
            Rgba[Offset + 2u] = EncodeSrgbByte(ApplyFilmicCurve(Accumulated.B * Settings.Exposure));
            Rgba[Offset + 3u] = 255u;
        }
    }

    return Rgba;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CONTACT SHEET
//------------------------------------------------------------------------------------------------------------------------

struct RenderedView
{
    std::string           Caption {};             // [-] - Overlay caption used during rendering
    std::vector<uint8_t>  Rgba    {};             // [-] - Finished RGBA8 pixels
    uint64_t              Digest  = 0ull;         // [-] - FNV-1a pixel digest
    std::string           Path    {};             // [-] - Absolute deliverable path
};

[[nodiscard]] std::vector<uint8_t> StackContactSheet(
    const std::vector<RenderedView>& Views,
    uint32_t                         SourceWidth,
    uint32_t                         SourceHeight,
    uint32_t&                        OutWidth,
    uint32_t&                        OutHeight)
{
    // 📝 Grid adapts to the view count: single row up to five tiles, otherwise five per row
    const uint32_t ViewCount   = static_cast<uint32_t>(Views.size());
    const uint32_t ThumbWidth  = ContactSheetThumbWidth;
    const uint32_t ThumbHeight = ContactSheetThumbHeight;
    const uint32_t Columns     = std::clamp(ViewCount, 1u, ContactSheetColumns);
    const uint32_t Rows        = (ViewCount + Columns - 1u) / Columns;

    OutWidth  = Columns * ThumbWidth + (Columns + 1u) * ContactSheetGap;
    OutHeight = Rows * (ThumbHeight + ContactSheetHeader) + (Rows + 1u) * ContactSheetGap;

    std::vector<uint8_t> Sheet(static_cast<std::size_t>(OutWidth) * OutHeight * 4u, 0u);
    for (std::size_t Index = 0u; Index < Sheet.size(); Index += 4u)
    {
        Sheet[Index + 0u] = 22u;
        Sheet[Index + 1u] = 24u;
        Sheet[Index + 2u] = 30u;
        Sheet[Index + 3u] = 255u;
    }

    const auto BlitTile = [&](uint32_t ColumnIndex, uint32_t RowIndex, const RenderedView& View)
    {
        const uint32_t DestinationX = ContactSheetGap + ColumnIndex * (ThumbWidth + ContactSheetGap);
        const uint32_t DestinationY = ContactSheetGap + RowIndex * (ThumbHeight + ContactSheetHeader + ContactSheetGap);

        // Caption strip
        for (uint32_t Row = 0u; Row < ContactSheetHeader; ++Row)
        {
            for (uint32_t Column = 0u; Column < ThumbWidth; ++Column)
            {
                const std::size_t Offset = (static_cast<std::size_t>(DestinationY + Row) * OutWidth + (DestinationX + Column)) * 4u;
                Sheet[Offset + 0u] = 34u;
                Sheet[Offset + 1u] = 38u;
                Sheet[Offset + 2u] = 50u;
                Sheet[Offset + 3u] = 255u;
            }
        }

        // 📝 Two-by-two box averaging keeps the shrunken tiles free of sparkle aliasing
        const uint32_t ImageTop = DestinationY + ContactSheetHeader;
        const uint32_t FootprintX = std::max(1u, SourceWidth / ThumbWidth);
        const uint32_t FootprintY = std::max(1u, SourceHeight / ThumbHeight);
        for (uint32_t ThumbRow = 0u; ThumbRow < ThumbHeight; ++ThumbRow)
        {
            const uint32_t SourceRowOrigin = (ThumbRow * SourceHeight) / ThumbHeight;
            for (uint32_t ThumbColumn = 0u; ThumbColumn < ThumbWidth; ++ThumbColumn)
            {
                const uint32_t SourceColumnOrigin = (ThumbColumn * SourceWidth) / ThumbWidth;
                uint32_t RedTotal = 0u, GreenTotal = 0u, BlueTotal = 0u, SampleCount = 0u;
                for (uint32_t SampleRow = 0u; SampleRow < FootprintY; ++SampleRow)
                {
                    const uint32_t SourceRow = std::min(SourceHeight - 1u, SourceRowOrigin + SampleRow);
                    for (uint32_t SampleColumn = 0u; SampleColumn < FootprintX; ++SampleColumn)
                    {
                        const uint32_t SourceColumn = std::min(SourceWidth - 1u, SourceColumnOrigin + SampleColumn);
                        const std::size_t SourceOffset = (static_cast<std::size_t>(SourceRow) * SourceWidth + SourceColumn) * 4u;
                        RedTotal   += View.Rgba[SourceOffset + 0u];
                        GreenTotal += View.Rgba[SourceOffset + 1u];
                        BlueTotal  += View.Rgba[SourceOffset + 2u];
                        ++SampleCount;
                    }
                }
                const std::size_t Offset = (static_cast<std::size_t>(ImageTop + ThumbRow) * OutWidth + (DestinationX + ThumbColumn)) * 4u;
                Sheet[Offset + 0u] = static_cast<uint8_t>(RedTotal / SampleCount);
                Sheet[Offset + 1u] = static_cast<uint8_t>(GreenTotal / SampleCount);
                Sheet[Offset + 2u] = static_cast<uint8_t>(BlueTotal / SampleCount);
                Sheet[Offset + 3u] = 255u;
            }
        }

        RasterizeText(Sheet, OutWidth, OutHeight, static_cast<int32_t>(DestinationX + 8u), static_cast<int32_t>(DestinationY + 11u), 1.6f, View.Caption, 232u, 236u, 246u);
    };

    for (std::size_t Index = 0u; Index < Views.size() && Index < static_cast<std::size_t>(Columns) * Rows; ++Index)
    {
        BlitTile(static_cast<uint32_t>(Index) % Columns, static_cast<uint32_t>(Index) / Columns, Views[Index]);
    }

    return Sheet;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    Scorecard Score {};
    bool QuickMode = false;
    RenderSettings Requested {};
    for (int Argument = 1; Argument < ArgumentCount; ++Argument)
    {
        const char* Current = ArgumentValues[Argument];
        const bool HasNext = (Argument + 1) < ArgumentCount;
        if (std::strcmp(Current, "--quick") == 0)
        {
            QuickMode = true;
        }
        else if (HasNext && std::strcmp(Current, "--width") == 0)
        {
            Requested.Width = static_cast<uint32_t>(std::max(64, std::atoi(ArgumentValues[++Argument])));
        }
        else if (HasNext && std::strcmp(Current, "--height") == 0)
        {
            Requested.Height = static_cast<uint32_t>(std::max(64, std::atoi(ArgumentValues[++Argument])));
        }
        else if (HasNext && std::strcmp(Current, "--rays") == 0)
        {
            Requested.PrimarySamples = static_cast<uint32_t>(std::clamp(std::atoi(ArgumentValues[++Argument]), 1, 16));
        }
        else if (HasNext && std::strcmp(Current, "--sun") == 0)
        {
            Requested.SunSamples = static_cast<uint32_t>(std::clamp(std::atoi(ArgumentValues[++Argument]), 1, 8));
        }
        else if (HasNext && std::strcmp(Current, "--exposure") == 0)
        {
            Requested.Exposure = static_cast<float>(std::clamp(std::atof(ArgumentValues[++Argument]), 0.2, 4.0));
        }
        else if (HasNext && std::strcmp(Current, "--gi") == 0)
        {
            const std::string Mode(ArgumentValues[++Argument]);
            if (Mode == "off" || Mode == "direct")      { Requested.GiMethod = GiMethodSelection::DirectOnly; }
            else if (Mode == "sh9" || Mode == "sh")     { Requested.GiMethod = GiMethodSelection::StokesSh9; }
            else if (Mode == "surfel" || Mode == "ddgi"){ Requested.GiMethod = GiMethodSelection::SurfelCascade; }
            else if (Mode == "path" || Mode == "pt")    { Requested.GiMethod = GiMethodSelection::PathTrace; }
            else { std::cerr << "[VehicleShowcase] unknown --gi mode '" << Mode << "'\n"; }
        }
        else if (HasNext && std::strcmp(Current, "--gi-scale") == 0)
        {
            Requested.GiDownsampleFactor = static_cast<uint32_t>(std::clamp(std::atoi(ArgumentValues[++Argument]), 1, 16));
        }
        else if (HasNext && std::strcmp(Current, "--gi-samples") == 0)
        {
            Requested.GiSamplesPerPixel = static_cast<uint32_t>(std::clamp(std::atoi(ArgumentValues[++Argument]), 4, 128));
        }
        else if (HasNext && std::strcmp(Current, "--gi-bounces") == 0)
        {
            Requested.GiBounceLimit = static_cast<uint32_t>(std::clamp(std::atoi(ArgumentValues[++Argument]), 1, 6));
        }
        else if (std::strcmp(Current, "--studio") == 0)
        {
            Requested.StudioEnclosure = true;
        }
        else if (std::strcmp(Current, "--gi-compare") == 0)
        {
            Requested.GiComparisonRun = true;
        }
        else if (HasNext && std::strcmp(Current, "--gi-weight") == 0)
        {
            Requested.GiWeight = static_cast<float>(std::clamp(std::atof(ArgumentValues[++Argument]), 0.0, 1.0));
        }
    }

    std::cout << "[VehicleShowcase] Decoding Controlled Integrator FCT1 streams...\n";

    MaterialSpace::CompactMaterialRegister Materials {};
    std::unordered_map<uint32_t, uint16_t> SlotMaterialOrdinal {};
    for (const VehicleMaterialSlot& Slot : VehicleMaterialSlots)
    {
        MaterialSpace::SurfaceMaterial Authored {};
        Authored.MaterialName                       = Slot.Caption;
        Authored.Diffuse.BaseColor                  = { Slot.Albedo.X, Slot.Albedo.Y, Slot.Albedo.Z };
        Authored.Diffuse.BaseWeight                 = 1.0f;
        Authored.Diffuse.DiffuseRoughness           = std::clamp(Slot.Roughness, 0.05f, 1.0f);
        Authored.Specular.Metalness                 = Slot.Metallic;
        Authored.Specular.SpecularWeight            = 1.0f;
        Authored.Specular.Roughness                 = std::max(0.03f, Slot.Roughness);
        Authored.Clearcoat.CoatWeight               = Slot.CoatWeight;
        Authored.Clearcoat.CoatRoughness            = 0.045f;
        Authored.Clearcoat.CoatIOR                  = 1.52f;
        Authored.PorosityLight.EmissionLuminanceNits = Slot.EmissionNits;
        Authored.PorosityLight.EmissionTint          = Slot.EmissionTint;
        SlotMaterialOrdinal[Slot.SlotIdentifier] = Materials.RegisterMaterial(Authored);
    }

    const auto ResolveVertexStream = [](const char* StreamName) -> std::filesystem::path
    {
        const std::array<std::filesystem::path, 3u> Candidates {
            std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "EngineContent/EnvironmentArchives/ControlVehicle" / (std::string(StreamName) + ".face"),
            std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Experimental/Shared/Models" / (std::string(StreamName) + ".face"),
            std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Experimental/Shared/Models" / (std::string(StreamName) + ".facet"),
        };
        for (const std::filesystem::path& Candidate : Candidates)
        {
            if (std::filesystem::exists(Candidate))
            {
                return Candidate;
            }
        }
        return Candidates[0];
    };

    // 📝 Authoring contract from ControlVehicleClassBuilder: body lifted by GroundOffset, wheels at four axle pivots
    constexpr float GroundOffsetZ = 0.41715f; // [m] - Ride height between authored local origin and studio floor
    struct AxlePivot
    {
        const char* Caption = "";      // [-] - Wheel caption
        float       Forward = 0.0f;    // [m] - X position of the axle pivot
        float       Lateral = 0.0f;    // [m] - Y position of the axle pivot
        float       Vertical = 0.0f;   // [m] - Z position above GroundOffsetZ
        bool        Mirror  = false;   // [-] - Mirror the authored right-side wheel across the vertical axis
    };
    constexpr std::array<AxlePivot, 4u> AxlePivots {{
        { "FL",  1.7274f,  1.0475f, 0.0914f, false },
        { "FR",  1.7274f, -1.0475f, 0.0914f, true  },
        { "RL", -1.6686f,  1.0475f, 0.0914f, false },
        { "RR", -1.6686f, -1.0475f, 0.0914f, true  },
    }};

    Fct1DecodeTarget Vehicle {};
    Vehicle.Primitives.reserve(24000u);
    Vehicle.SmoothNormals.reserve(24000u);

    const bool BodyDecoded = DecodeFct1Stream(
        ResolveVertexStream("ControlVehicleBody"),
        { 0.0f, 0.0f, GroundOffsetZ },
        false,
        Materials,
        SlotMaterialOrdinal,
        Vehicle);
    Score.Check(BodyDecoded, "Decoded ControlVehicleBody FCT1 stream");

    for (const AxlePivot& Axle : AxlePivots)
    {
        const Mathematics::Axis3D WheelOrigin { Axle.Forward, Axle.Lateral, GroundOffsetZ + Axle.Vertical };
        const bool TyreDecoded = DecodeFct1Stream(ResolveVertexStream("ControlVehicleTyre"), WheelOrigin, Axle.Mirror, Materials, SlotMaterialOrdinal, Vehicle);
        const bool RimDecoded  = DecodeFct1Stream(ResolveVertexStream("ControlVehicleRim"), WheelOrigin, Axle.Mirror, Materials, SlotMaterialOrdinal, Vehicle);
        Score.Check(TyreDecoded && RimDecoded, std::string("Decoded tyre and rim for wheel ") + Axle.Caption);
    }

    Score.Check(Vehicle.Primitives.size() > 1000u, "Decoded vehicle exceeds 1k triangles");

    // ---- Scene bounds ----
    Mathematics::Axis3D BoundsMin { 1.0e30f, 1.0e30f, 1.0e30f };
    Mathematics::Axis3D BoundsMax { -1.0e30f, -1.0e30f, -1.0e30f };
    for (const SpatialAcceleration::RayTrianglePrimitive& Primitive : Vehicle.Primitives)
    {
        const Mathematics::Axis3D Corner0 = Primitive.Corner0;
        const Mathematics::Axis3D Corner1 = Add(Primitive.Corner0, Primitive.Edge01);
        const Mathematics::Axis3D Corner2 = Add(Primitive.Corner0, Primitive.Edge02);
        for (const Mathematics::Axis3D& Point : { Corner0, Corner1, Corner2 })
        {
            BoundsMin.X = std::min(BoundsMin.X, Point.X);
            BoundsMin.Y = std::min(BoundsMin.Y, Point.Y);
            BoundsMin.Z = std::min(BoundsMin.Z, Point.Z);
            BoundsMax.X = std::max(BoundsMax.X, Point.X);
            BoundsMax.Y = std::max(BoundsMax.Y, Point.Y);
            BoundsMax.Z = std::max(BoundsMax.Z, Point.Z);
        }
    }

    const Mathematics::Axis3D VehicleCenter {
        (BoundsMin.X + BoundsMax.X) * 0.5f,
        (BoundsMin.Y + BoundsMax.Y) * 0.5f,
        (BoundsMin.Z + BoundsMax.Z) * 0.5f,
    };
    const float VehicleHalfLength = std::max(BoundsMax.X - BoundsMin.X, BoundsMax.Y - BoundsMin.Y) * 0.5f;
    std::cout << "[VehicleShowcase] Bounds min(" << BoundsMin.X << ", " << BoundsMin.Y << ", " << BoundsMin.Z << ") max("
              << BoundsMax.X << ", " << BoundsMax.Y << ", " << BoundsMax.Z << ") halfLength=" << VehicleHalfLength << '\n';
    Score.Check(VehicleHalfLength > 0.5f && VehicleHalfLength < 10.0f, "Vehicle bounds are sane");

    // ---- Studio floor ----
    MaterialSpace::SurfaceMaterial StudioFloor {};
    StudioFloor.MaterialName             = "StudioFloor";
    StudioFloor.Diffuse.BaseColor        = { 0.185f, 0.190f, 0.200f };
    StudioFloor.Diffuse.DiffuseRoughness = 0.92f;
    StudioFloor.Specular.Roughness       = 0.72f;
    StudioFloor.Specular.SpecularWeight  = 0.35f;
    const uint16_t FloorMaterialOrdinal = Materials.RegisterMaterial(StudioFloor);

    const float FloorZ = BoundsMin.Z - 0.004f;
    const float FloorHalfSpan = std::max(14.0f, VehicleHalfLength * 4.5f);
    const auto AppendQuad = [&](const Mathematics::Axis3D& PointA, const Mathematics::Axis3D& PointB,
                                const Mathematics::Axis3D& PointC, const Mathematics::Axis3D& PointD,
                                uint16_t MaterialOrdinal, const Mathematics::Axis3D& QuadNormal)
    {
        const std::array<Mathematics::Axis3D, 3u> Loops[2] = {{ PointA, PointB, PointC }, { PointA, PointC, PointD }};
        for (const std::array<Mathematics::Axis3D, 3u>& Loop : Loops)
        {
            SpatialAcceleration::RayTrianglePrimitive Primitive {};
            Primitive.Corner0          = Loop[0];
            Primitive.Edge01           = Sub(Loop[1], Loop[0]);
            Primitive.Edge02           = Sub(Loop[2], Loop[0]);
            Primitive.MaterialOrdinal  = MaterialOrdinal;
            Primitive.OpaqueMask       = 1u;
            Primitive.SourceFaceIndex  = 0xFFFFFFFEu;
            Primitive.PrimitiveOrdinal = static_cast<uint32_t>(Vehicle.Primitives.size());

            SpatialAcceleration::TriangleSmoothNormals CornerNormals {};
            CornerNormals.Normal0 = QuadNormal;
            CornerNormals.Normal1 = QuadNormal;
            CornerNormals.Normal2 = QuadNormal;

            Vehicle.Primitives.push_back(Primitive);
            Vehicle.SmoothNormals.push_back(CornerNormals);
        }
    };

    const Mathematics::Axis3D FloorCorners[4] = {
        { VehicleCenter.X - FloorHalfSpan, VehicleCenter.Y - FloorHalfSpan, FloorZ },
        { VehicleCenter.X + FloorHalfSpan, VehicleCenter.Y - FloorHalfSpan, FloorZ },
        { VehicleCenter.X + FloorHalfSpan, VehicleCenter.Y + FloorHalfSpan, FloorZ },
        { VehicleCenter.X - FloorHalfSpan, VehicleCenter.Y + FloorHalfSpan, FloorZ },
    };
    AppendQuad(FloorCorners[0], FloorCorners[1], FloorCorners[2], FloorCorners[3], FloorMaterialOrdinal, { 0.0f, 0.0f, 1.0f });

    // ---- Optional bounce panels: Cornell-style coloured walls that make colour bleeding readable ----
    if (Requested.StudioEnclosure)
    {
        const auto RegisterBouncePanel = [&](const char* PanelName, Mathematics::LinearRGB AlbedoColors) -> uint16_t
        {
            MaterialSpace::SurfaceMaterial Panel {};
            Panel.MaterialName             = PanelName;
            Panel.Diffuse.BaseColor        = AlbedoColors;
            Panel.Diffuse.DiffuseRoughness = 0.95f;
            Panel.Specular.Roughness       = 0.88f;
            Panel.Specular.SpecularWeight  = 0.12f;
            return Materials.RegisterMaterial(Panel);
        };

        const uint16_t WarmPanelOrdinal = RegisterBouncePanel("BouncePanelWarm", { 0.62f, 0.155f, 0.115f });
        const uint16_t CoolPanelOrdinal = RegisterBouncePanel("BouncePanelCool", { 0.115f, 0.44f, 0.235f });

        // 📝 Panels sit outside the orbit radius so the camera is always between them and the vehicle
        const float PanelHalfSpan = std::max(13.5f, VehicleHalfLength * 2.4f);
        const float PanelHeight   = 5.2f;

        // Warm panel on the camera-left (+Y) side, facing back toward the vehicle
        const Mathematics::Axis3D WarmCorners[4] = {
            { VehicleCenter.X - PanelHalfSpan, VehicleCenter.Y + PanelHalfSpan, FloorZ },
            { VehicleCenter.X + PanelHalfSpan, VehicleCenter.Y + PanelHalfSpan, FloorZ },
            { VehicleCenter.X + PanelHalfSpan, VehicleCenter.Y + PanelHalfSpan, FloorZ + PanelHeight },
            { VehicleCenter.X - PanelHalfSpan, VehicleCenter.Y + PanelHalfSpan, FloorZ + PanelHeight },
        };
        AppendQuad(WarmCorners[0], WarmCorners[1], WarmCorners[2], WarmCorners[3], WarmPanelOrdinal, { 0.0f, -1.0f, 0.0f });

        // Cool panel on the camera-right (-Y) side, facing back toward the vehicle
        const Mathematics::Axis3D CoolCorners[4] = {
            { VehicleCenter.X + PanelHalfSpan, VehicleCenter.Y - PanelHalfSpan, FloorZ },
            { VehicleCenter.X - PanelHalfSpan, VehicleCenter.Y - PanelHalfSpan, FloorZ },
            { VehicleCenter.X - PanelHalfSpan, VehicleCenter.Y - PanelHalfSpan, FloorZ + PanelHeight },
            { VehicleCenter.X + PanelHalfSpan, VehicleCenter.Y - PanelHalfSpan, FloorZ + PanelHeight },
        };
        AppendQuad(CoolCorners[0], CoolCorners[1], CoolCorners[2], CoolCorners[3], CoolPanelOrdinal, { 0.0f, 1.0f, 0.0f });
    }

    std::cout << "[VehicleShowcase] Scene triangles=" << Vehicle.Primitives.size() << '\n';

    SpatialAcceleration::WideBoundingTree Tree {};
    const bool TreeBuilt = Tree.BuildFromTrianglePrimitivesWithNormals(Vehicle.Primitives, Vehicle.SmoothNormals);
    Score.Check(TreeBuilt && !Tree.WideEntries.empty(), "Built BVH4 WideBoundingTree with smooth normals");
    if (!TreeBuilt)
    {
        return 1;
    }
    std::cout << "[VehicleShowcase] BVH entries=" << Tree.WideEntries.size() << " depth=" << Tree.MaxTreeDepth << '\n';

    // ---- Lighting ----
    GeometricRaster::SolarDirectionalSource Sun {};
    Sun.DirectionToSun          = Normalize(Mathematics::Axis3D { -0.42f, -0.68f, 0.60f });
    Sun.SolarIrradiance         = { 5.60f, 5.32f, 4.82f };
    Sun.SunAngularRadiusRadians = 0.017f;
    Sun.RtxSamplesPerPixel      = DefaultSunSamples;

    SkyGradient Sky {};

    RenderSettings Settings {};
    if (QuickMode)
    {
        Settings.Width          = 800u;
        Settings.Height         = 450u;
        Settings.PrimarySamples = 1u;
        Settings.SunSamples     = 1u;
    }
    if (Requested.Width != DefaultImageWidth || Requested.Height != DefaultImageHeight)
    {
        Settings.Width  = Requested.Width;
        Settings.Height = Requested.Height;
    }
    if (Requested.PrimarySamples != DefaultPrimarySamples)
    {
        Settings.PrimarySamples = Requested.PrimarySamples;
    }
    if (Requested.SunSamples != DefaultSunSamples)
    {
        Settings.SunSamples = Requested.SunSamples;
    }
    if (Requested.Exposure != RenderSettings {}.Exposure)
    {
        Settings.Exposure = Requested.Exposure;
    }
    Settings.GiMethod           = Requested.GiMethod;
    Settings.GiDownsampleFactor = Requested.GiDownsampleFactor;
    Settings.GiSamplesPerPixel  = Requested.GiSamplesPerPixel;
    Settings.GiBounceLimit      = Requested.GiBounceLimit;
    Settings.GiWeight           = Requested.GiWeight;
    Settings.StudioEnclosure    = Requested.StudioEnclosure;
    Settings.GiComparisonRun    = Requested.GiComparisonRun;
    if (QuickMode)
    {
        Settings.GiDownsampleFactor = 6u;
        Settings.GiSamplesPerPixel  = 8u;
    }
    Sun.RtxSamplesPerPixel = Settings.SunSamples;

    const float VerticalFovRadians = 38.0f * Constants::DegToRadFloat;
    const float HalfFovY = std::tan(VerticalFovRadians * 0.5f);
    const float HalfFovX = HalfFovY * static_cast<float>(Settings.Width) / static_cast<float>(Settings.Height);

    // 📝 Orbit fit: horizontal term uses the plan-view circumradius (yaw-invariant), vertical term adds roof height scaled by pitch
    const float PlanCircumradius = 0.5f * std::sqrt((BoundsMax.X - BoundsMin.X) * (BoundsMax.X - BoundsMin.X) + (BoundsMax.Y - BoundsMin.Y) * (BoundsMax.Y - BoundsMin.Y));
    const float RoofHalfHeight = 0.5f * (BoundsMax.Z - BoundsMin.Z);
    const float SinHalfFovX = HalfFovX / std::sqrt(1.0f + HalfFovX * HalfFovX);
    const float SinHalfFovY = HalfFovY / std::sqrt(1.0f + HalfFovY * HalfFovY);
    const auto EvaluateFramingDistance = [&](float PitchRadians) noexcept
    {
        // 📝 Sphere-fit horizontally (near-corner magnification included) and extent-fit vertically for the roof line
        const float HorizontalDistance = PlanCircumradius / std::max(0.05f, SinHalfFovX);
        const float VerticalExtent = RoofHalfHeight * std::abs(std::cos(PitchRadians)) + PlanCircumradius * std::sin(PitchRadians);
        const float VerticalDistance = VerticalExtent / std::max(0.05f, SinHalfFovY);
        return std::max(HorizontalDistance, VerticalDistance) * 1.06f + 0.20f;
    };
    const float FramingDistance = EvaluateFramingDistance(14.0f * Constants::DegToRadFloat);
    std::cout << "[VehicleShowcase] resolution=" << Settings.Width << 'x' << Settings.Height
              << " primarySamples=" << Settings.PrimarySamples << " sunSamples=" << Settings.SunSamples
              << " framingDistance=" << FramingDistance << " m\n";

    const std::filesystem::path OutputDirectory = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Exhibits/Images/Vehicle";
    std::error_code DirectoryError {};
    std::filesystem::create_directories(OutputDirectory, DirectoryError);

    std::vector<RenderedView> RenderedViews {};
    RenderedViews.reserve(TurntableViews.size());
    std::vector<GeometricRaster::EmissiveGiMetrics> ViewGiMetrics {};
    ViewGiMetrics.reserve(TurntableViews.size());

    for (std::size_t ViewIndex = 0u; ViewIndex < TurntableViews.size(); ++ViewIndex)
    {
        const TurntableView& ViewDefinition = TurntableViews[ViewIndex];
        const float YawRadians   = ViewDefinition.YawDegrees * Constants::DegToRadFloat;
        const float PitchRadians = ViewDefinition.PitchDegrees * Constants::DegToRadFloat;
        const float Distance     = EvaluateFramingDistance(PitchRadians) * ViewDefinition.DistanceScale;

        const Mathematics::Axis3D OrbitOffset = Mathematics::EvaluateOrbitOffsetFromAngles(Distance, YawRadians, PitchRadians, Mathematics::WorldCoordinateBasis::RightHandedZUp);
        const Mathematics::Axis3D FocusPoint { VehicleCenter.X + ViewDefinition.FocusForward, VehicleCenter.Y, VehicleCenter.Z + 0.06f };
        const Mathematics::Axis3D EyePosition = Add(FocusPoint, OrbitOffset);

        const CameraBasis Camera = ConstructCamera(EyePosition, FocusPoint, VerticalFovRadians, Settings.Width, Settings.Height);

        std::cout << "[VehicleShowcase] Rendering " << (ViewIndex + 1u) << '/' << TurntableViews.size() << ' ' << ViewDefinition.Caption
                  << " eye(" << EyePosition.X << ", " << EyePosition.Y << ", " << EyePosition.Z << ") distance=" << Distance << " m ..." << std::flush;

        // 📝 Evaluate the selected GI candidate for this camera before shading the full-resolution frame
        GiFrameBuffer GiBuffer {};
        if (Settings.GiMethod != GiMethodSelection::DirectOnly)
        {
            GiBuffer = EvaluateGiFrameBuffer(Tree, Materials, Sun, Sky, Camera, Settings);
            ViewGiMetrics.push_back(GiBuffer.Metrics);
            std::cout << "\n[VehicleShowcase] GI " << QueryGiMethodCaption(Settings.GiMethod)
                      << " " << GiBuffer.WidthPx << 'x' << GiBuffer.HeightPx
                      << " rays=" << GiBuffer.Metrics.RayQueryCount
                      << " surfels=" << GiBuffer.Metrics.SurfelCount
                      << " meanL=" << GiBuffer.MeanLuminance
                      << " peakL=" << GiBuffer.PeakLuminance
                      << " " << GiBuffer.Metrics.ElapsedMilliseconds << " ms"
                      << " ..." << std::flush;
        }

        std::vector<uint8_t> Rgba = RenderTurntableView(Tree, Materials, Sun, Sky, Camera, Settings, &GiBuffer);

        char CaptionBuffer[256];
        std::snprintf(CaptionBuffer, sizeof(CaptionBuffer), "CONTROLLED INTEGRATOR  %s   %ux%u   %u SPP   GI %s   yaw %d  pitch %d  dist %.2f m",
                      ViewDefinition.Caption,
                      Settings.Width,
                      Settings.Height,
                      Settings.PrimarySamples * Settings.SunSamples,
                      QueryGiMethodCaption(Settings.GiMethod),
                      static_cast<int>(ViewDefinition.YawDegrees),
                      static_cast<int>(ViewDefinition.PitchDegrees),
                      static_cast<double>(Distance));
        OverlayCaptionBar(Rgba, Settings.Width, Settings.Height, CaptionBuffer, 1.35f);

        const uint64_t Digest = DigestPixelBytes(Rgba);
        char FileName[256];
        std::snprintf(FileName, sizeof(FileName), "Vehicle_Turntable_%02zu_%s.png", ViewIndex, ViewDefinition.Caption);
        std::string SanitizedFileName(FileName);
        std::replace(SanitizedFileName.begin(), SanitizedFileName.end(), ' ', '_');
        std::replace(SanitizedFileName.begin(), SanitizedFileName.end(), '/', '-');
        const std::filesystem::path OutputPath = OutputDirectory / SanitizedFileName;

        const bool Written = WriteRgba8Png(OutputPath, Settings.Width, Settings.Height, Rgba);
        Score.Check(Written, std::string("Wrote ") + OutputPath.filename().string());
        std::cout << " digest=" << std::hex << Digest << std::dec << '\n';

        RenderedViews.push_back({ ViewDefinition.Caption, std::move(Rgba), Digest, OutputPath.string() });
    }

    // ---- Contact sheet with the full ten-view set ----
    uint32_t SheetWidth = 0u;
    uint32_t SheetHeight = 0u;
    std::vector<uint8_t> Sheet = StackContactSheet(RenderedViews, Settings.Width, Settings.Height, SheetWidth, SheetHeight);
    const std::filesystem::path ContactSheetPath = OutputDirectory / "Vehicle_ContactSheet_10x.png";
    const bool SheetWritten = WriteRgba8Png(ContactSheetPath, SheetWidth, SheetHeight, Sheet);
    Score.Check(SheetWritten, "Wrote ten-view contact sheet");
    std::cout << "[VehicleShowcase] Contact sheet -> " << ContactSheetPath.string() << ' ' << SheetWidth << 'x' << SheetHeight << '\n';

    // ---- GI method comparison: same camera, same scene, four indirect policies ----
    if (Settings.GiComparisonRun)
    {
        const TurntableView& ComparisonView = TurntableViews[1u];
        const float YawRadians   = ComparisonView.YawDegrees * Constants::DegToRadFloat;
        const float PitchRadians = ComparisonView.PitchDegrees * Constants::DegToRadFloat;
        const float Distance     = EvaluateFramingDistance(PitchRadians) * ComparisonView.DistanceScale;
        const Mathematics::Axis3D OrbitOffset = Mathematics::EvaluateOrbitOffsetFromAngles(
            Distance, YawRadians, PitchRadians, Mathematics::WorldCoordinateBasis::RightHandedZUp);
        const Mathematics::Axis3D FocusPoint { VehicleCenter.X, VehicleCenter.Y, VehicleCenter.Z + 0.06f };
        const CameraBasis ComparisonCamera = ConstructCamera(Add(FocusPoint, OrbitOffset), FocusPoint, VerticalFovRadians, Settings.Width, Settings.Height);

        const std::array<GiMethodSelection, 4u> ComparisonMethods {{
            GiMethodSelection::DirectOnly,
            GiMethodSelection::StokesSh9,
            GiMethodSelection::SurfelCascade,
            GiMethodSelection::PathTrace,
        }};

        std::vector<RenderedView> ComparisonViews {};
        std::vector<GeometricRaster::EmissiveGiMetrics> ComparisonMetrics {};
        std::vector<float> ComparisonMeanLuminance {};
        std::vector<float> ComparisonPeakLuminance {};
        ComparisonViews.reserve(ComparisonMethods.size());

        for (const GiMethodSelection Method : ComparisonMethods)
        {
            RenderSettings ComparisonSettings = Settings;
            ComparisonSettings.GiMethod = Method;

            GiFrameBuffer ComparisonGiBuffer {};
            if (Method != GiMethodSelection::DirectOnly)
            {
                ComparisonGiBuffer = EvaluateGiFrameBuffer(Tree, Materials, Sun, Sky, ComparisonCamera, ComparisonSettings);
            }

            std::vector<uint8_t> Rgba = RenderTurntableView(Tree, Materials, Sun, Sky, ComparisonCamera, ComparisonSettings,
                                                            (Method == GiMethodSelection::DirectOnly) ? nullptr : &ComparisonGiBuffer);

            char Caption[224];
            std::snprintf(Caption, sizeof(Caption), "GI %s   meanL %.4f  rays %llu",
                          QueryGiMethodCaption(Method),
                          static_cast<double>(ComparisonGiBuffer.MeanLuminance),
                          static_cast<unsigned long long>(ComparisonGiBuffer.Metrics.RayQueryCount));
            OverlayCaptionBar(Rgba, Settings.Width, Settings.Height, Caption, 1.35f);

            ComparisonMetrics.push_back(ComparisonGiBuffer.Metrics);
            ComparisonMeanLuminance.push_back(ComparisonGiBuffer.MeanLuminance);
            ComparisonPeakLuminance.push_back(ComparisonGiBuffer.PeakLuminance);
            RenderedView ComparisonEntry {};
            ComparisonEntry.Caption = QueryGiMethodCaption(Method);
            ComparisonEntry.Rgba    = std::move(Rgba);
            ComparisonEntry.Digest  = DigestPixelBytes(ComparisonEntry.Rgba);
            ComparisonViews.push_back(std::move(ComparisonEntry));

            std::cout << "[VehicleShowcase] GI comparison " << QueryGiMethodCaption(Method)
                      << " meanL=" << ComparisonGiBuffer.MeanLuminance
                      << " peakL=" << ComparisonGiBuffer.PeakLuminance
                      << " rays=" << ComparisonGiBuffer.Metrics.RayQueryCount
                      << " surfels=" << ComparisonGiBuffer.Metrics.SurfelCount
                      << " " << ComparisonGiBuffer.Metrics.ElapsedMilliseconds << " ms\n";
        }

        uint32_t ComparisonWidth = 0u;
        uint32_t ComparisonHeight = 0u;
        std::vector<uint8_t> ComparisonSheet = StackContactSheet(ComparisonViews, Settings.Width, Settings.Height, ComparisonWidth, ComparisonHeight);
        const std::filesystem::path ComparisonPath = OutputDirectory / "Vehicle_GiMethodComparison.png";
        Score.Check(WriteRgba8Png(ComparisonPath, ComparisonWidth, ComparisonHeight, ComparisonSheet),
                    "Wrote GI method comparison sheet");

        std::ofstream GiCsv(OutputDirectory / "Vehicle_GiMethodComparison.csv");
        GiCsv << "method,gi_width_px,gi_height_px,mean_luminance,peak_luminance,ray_queries,visibility_rays,surfels,elapsed_ms,approximate_memory_bytes,pixel_digest_fnv1a64\n";
        const char* MethodNames[4] = { "DirectOnly", "StokesSh9MultiBounce", "SurfelCascadeDdgi", "DeterministicPathTrace" };
        for (std::size_t Index = 0u; Index < ComparisonViews.size(); ++Index)
        {
            const GeometricRaster::EmissiveGiMetrics& Metrics = ComparisonMetrics[Index];
            char DigestText[32];
            std::snprintf(DigestText, sizeof(DigestText), "%016llx", static_cast<unsigned long long>(ComparisonViews[Index].Digest));
            GiCsv << MethodNames[Index] << ','
                  << (Settings.Width / std::max(1u, Settings.GiDownsampleFactor)) << ','
                  << (Settings.Height / std::max(1u, Settings.GiDownsampleFactor)) << ','
                  << ComparisonMeanLuminance[Index] << ','
                  << ComparisonPeakLuminance[Index] << ','
                  << Metrics.RayQueryCount << ','
                  << Metrics.VisibilityRayCount << ','
                  << Metrics.SurfelCount << ','
                  << Metrics.ElapsedMilliseconds << ','
                  << Metrics.ApproximateMemoryBytes << ','
                  << DigestText << '\n';
        }
        GiCsv.close();
    }

    // ---- Determinism and asset manifest ----
    const std::filesystem::path ManifestPath = OutputDirectory / "Vehicle_Turntable_Manifest.csv";
    std::ofstream Manifest(ManifestPath);
    Manifest << "view,caption,path,width,height,primary_rays,sun_rays,triangles,bvh_entries,pixel_digest_fnv1a64\n";
    for (std::size_t ViewIndex = 0u; ViewIndex < RenderedViews.size(); ++ViewIndex)
    {
        char DigestText[32];
        std::snprintf(DigestText, sizeof(DigestText), "%016llx", static_cast<unsigned long long>(RenderedViews[ViewIndex].Digest));
        const std::filesystem::path RelativeOutput = std::filesystem::path(RenderedViews[ViewIndex].Path).lexically_relative(std::filesystem::path(FRONTIER_REPO_ROOT_DIR));
        Manifest << ViewIndex << ",\"" << RenderedViews[ViewIndex].Caption << "\",\"" << RelativeOutput.generic_string() << "\","
                 << Settings.Width << ',' << Settings.Height << ',' << Settings.PrimarySamples << ',' << Settings.SunSamples << ','
                 << Vehicle.Primitives.size() << ',' << Tree.WideEntries.size() << ',' << DigestText << '\n';
    }
    Manifest.close();
    Score.Check(std::filesystem::exists(ManifestPath), "Wrote determinism manifest CSV");

    std::cout << "[VehicleShowcase][SUMMARY] passed=" << Score.Passed << " failed=" << Score.Failed
              << " out=" << OutputDirectory.string()
              << " resolution=" << Settings.Width << 'x' << Settings.Height
              << " views=" << RenderedViews.size()
              << " triangles=" << Vehicle.Primitives.size()
              << " bvhEntries=" << Tree.WideEntries.size() << '\n';
    return Score.Failed == 0u ? 0 : 1;
}

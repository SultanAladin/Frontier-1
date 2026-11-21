/*============================================================================================================================================
                                                          VULKANVECTORRENDERMETHOD.H
============================================================================================================================================*/
// 🧩 Symbolic ThorVG 1.0.0 frontend (SVG DOM + TTF/OTF typography + cubic path flattening) implementing tvg::RenderMethod for Vulkan .slang GPU rasterization.

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class VectorGradientPreset : uint8_t
{
    Solid  = 0u,
    Linear = 1u,
    Radial = 2u,
};

enum class VectorStrokeCapStyle : uint8_t
{
    Butt   = 0u,
    Round  = 1u,
    Square = 2u,
};

enum class VectorStrokeJoinStyle : uint8_t
{
    Miter = 0u,
    Round = 1u,
    Bevel = 2u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct VectorVertex2D
{
    float X = 0.0f;
    float Y = 0.0f;
};

struct VectorColorStop
{
    float     Offset = 0.0f;
    ColorRgba Tint   { 255u, 255u, 255u, 255u };
};

struct VectorCubicKnot
{
    float AnchorX    = 0.0f;
    float AnchorY    = 0.0f;
    float InHandleX  = 0.0f;
    float InHandleY  = 0.0f;
    float OutHandleX = 0.0f;
    float OutHandleY = 0.0f;
    bool  Smooth     = true;
};

struct VectorCubicSubpath
{
    bool                        ClosedLoop = true;
    std::vector<VectorCubicKnot> Knots     {};
};

struct ExtractedVectorShape
{
    uint32_t                        ShapeOrdinal      = 0u;
    bool                            EvenOddFillRule   = false;
    bool                            FillEnabled       = true;
    VectorGradientPreset            GradientPreset    = VectorGradientPreset::Solid;
    ColorRgba                       FillTint          { 239u, 244u, 207u, 255u };
    float                           GradientStart[2]  { 0.0f, 0.0f };
    float                           GradientEnd[2]    { 1.0f, 1.0f };
    float                           GradientRadius0   = 0.0f;
    float                           GradientRadius1   = 1.0f;
    uint32_t                        StopCount         = 0u;
    std::array<VectorColorStop, 8u> Stops             {};
    float                           Opacity           = 1.0f;
    uint8_t                         BlendOrdinal      = 0u;

    bool                            StrokeEnabled     = false;
    float                           StrokeWidthPx     = 0.0f;
    ColorRgba                       StrokeTint        { 22u, 24u, 28u, 255u };

    float                           BoundsMinX        = 0.0f;
    float                           BoundsMinY        = 0.0f;
    float                           BoundsMaxX        = 0.0f;
    float                           BoundsMaxY        = 0.0f;

    std::vector<VectorVertex2D>     FillFanTriangles  {};
    std::vector<VectorVertex2D>     StrokeTriangles   {};
};

struct ExtractedSvgLayer
{
    std::string                      Title             {};
    bool                             EvenOddFillRule   = false;
    bool                             FillEnabled       = true;
    VectorGradientPreset             GradientPreset    = VectorGradientPreset::Solid;
    ColorRgba                        FillTint          { 239u, 244u, 207u, 255u };
    ColorRgba                        FillTintB         { 90u, 118u, 146u, 255u };
    float                            GradientAngleDeg  = 45.0f;
    std::vector<VectorColorStop>     Stops             {};
    bool                             StrokeEnabled     = false;
    float                            StrokeWidthPx     = 0.0f;
    ColorRgba                        StrokeTint        { 22u, 24u, 28u, 255u };
    float                            Opacity           = 1.0f;
    uint8_t                          BlendOrdinal      = 0u;
    float                            CenterX           = 256.0f;
    float                            CenterY           = 256.0f;
    float                            WidthPx           = 128.0f;
    float                            HeightPx          = 128.0f;
    std::vector<VectorCubicSubpath>  Subpaths          {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THORVG SYMBOLIC FRONTEND
//------------------------------------------------------------------------------------------------------------------------

class ThorVGVectorFrontend
{
public:
    ThorVGVectorFrontend() noexcept;
    ~ThorVGVectorFrontend() noexcept;

    ThorVGVectorFrontend(const ThorVGVectorFrontend&)            = delete;
    ThorVGVectorFrontend& operator=(const ThorVGVectorFrontend&) = delete;

    [[nodiscard]] static bool RegisterTypefaceFile(const std::filesystem::path& FontPath);
    [[nodiscard]] static bool RegisterTypefaceMemory(
        std::string_view              FontKey,
        std::span<const uint8_t>      FontBytes,
        std::string_view              FormatTag = "ttf");

    [[nodiscard]] bool CompileSvgToGpuShapes(
        std::string_view                  SvgSourceOrPath,
        bool                              SourceIsFilePath,
        float                             TargetBoxExtentPx,
        float                             CenterX,
        float                             CenterY,
        std::vector<ExtractedVectorShape>& OutShapes,
        std::string*                      OutDiagnostic = nullptr) const;

    [[nodiscard]] bool ExtractSvgToEditableLayers(
        std::string_view                SvgSourceOrPath,
        bool                            SourceIsFilePath,
        float                           TargetBoxExtentPx,
        float                           DropCenterX,
        float                           DropCenterY,
        std::vector<ExtractedSvgLayer>& OutLayers,
        std::string*                    OutDiagnostic = nullptr) const;

    [[nodiscard]] bool CompileTextToGpuShapes(
        const std::filesystem::path&      FontFilePath,
        std::string_view                  FontRegisteredKey,
        std::string_view                  Utf8Text,
        float                             FontSizePx,
        float                             CenterX,
        float                             CenterY,
        ColorRgba                         FillTint,
        float                             StrokeWidthPx,
        ColorRgba                         StrokeTint,
        std::vector<ExtractedVectorShape>& OutShapes,
        float*                            OutWidthPx    = nullptr,
        float*                            OutHeightPx   = nullptr,
        std::string*                      OutDiagnostic = nullptr) const;

    static void CompileCubicSubpathsToGpuShape(
        std::span<const VectorCubicSubpath> Subpaths,
        bool                                EvenOddFillRule,
        bool                                FillEnabled,
        ColorRgba                           FillTint,
        bool                                StrokeEnabled,
        float                               StrokeWidthPx,
        ColorRgba                           StrokeTint,
        VectorStrokeCapStyle                CapStyle,
        VectorStrokeJoinStyle               JoinStyle,
        ExtractedVectorShape&               OutShape) noexcept;

    static void TessellatePolygonsToGpuShape(
        std::span<const std::vector<VectorVertex2D>> Polygons,
        std::span<const uint8_t>                     ClosedLoops,
        bool                                         EvenOddFillRule,
        bool                                         FillEnabled,
        ColorRgba                                    FillTint,
        bool                                         StrokeEnabled,
        float                                        StrokeWidthPx,
        ColorRgba                                    StrokeTint,
        VectorStrokeCapStyle                         CapStyle,
        VectorStrokeJoinStyle                        JoinStyle,
        ExtractedVectorShape&                        OutShape) noexcept;
};

} // namespace Frontier::GeometricRaster

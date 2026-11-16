/*============================================================================================================================================
                                                               FRACTUREICONS.H
============================================================================================================================================*/
// 🧩 Project Zero SVG vector icon helpers for Fracture Editor (primitives, toolbar glyphs, method cards & material swatches)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace Frontier::Workspaces::Fracture::FractureIcons
{

enum class PrimitiveShapeId : uint8_t
{
    Box       = 0u,
    Sphere    = 1u,
    Cylinder  = 2u,
    Cone      = 3u,
    Capsule   = 4u,
    Torus     = 5u,
    Pipe      = 6u,
    Pane      = 7u,
    HexColumn = 8u,
    Count     = 9u,
};

inline void DrawSvgStroke24(
    PixelSpace&      Surface,
    Rectangle2D      Box,
    std::string_view SvgPath,
    ColorRgba        Ink,
    float            StrokeWidth = 1.75f) noexcept
{
    const float Size = std::min(Box.Width, Box.Height);
    VectorIconPlacement Place {};
    Place.X           = Box.X + (Box.Width - Size) * 0.5f;
    Place.Y           = Box.Y + (Box.Height - Size) * 0.5f;
    Place.Size        = Size;
    Place.Ink         = Ink;
    Place.StrokeWidth = StrokeWidth;
    VectorIconRepository::StrokeSvgPath(Surface, SvgPath, Place, 24.0f);
}

inline void DrawSvgFill24(
    PixelSpace&      Surface,
    Rectangle2D      Box,
    std::string_view SvgPath,
    ColorRgba        Ink) noexcept
{
    const float Size = std::min(Box.Width, Box.Height);
    VectorIconPlacement Place {};
    Place.X           = Box.X + (Box.Width - Size) * 0.5f;
    Place.Y           = Box.Y + (Box.Height - Size) * 0.5f;
    Place.Size        = Size;
    Place.Ink         = Ink;
    Place.StrokeWidth = 1.0f;
    VectorIconRepository::FillSvgPath(Surface, SvgPath, Place, 24.0f);
}

inline void DrawSvg256(
    PixelSpace&      Surface,
    Rectangle2D      Box,
    std::string_view FillPath,
    std::string_view StrokePath,
    ColorRgba        FillTint,
    ColorRgba        StrokeTint,
    float            StrokeWidth256 = 8.0f) noexcept
{
    const float Size = std::min(Box.Width, Box.Height);
    VectorIconPlacement Place {};
    Place.X           = Box.X + (Box.Width - Size) * 0.5f;
    Place.Y           = Box.Y + (Box.Height - Size) * 0.5f;
    Place.Size        = Size;
    Place.StrokeWidth = StrokeWidth256;
    if (!FillPath.empty() && FillTint.Alpha > 0u)
    {
        Place.Ink = FillTint;
        VectorIconRepository::FillSvgPath(Surface, FillPath, Place, 256.0f);
    }
    if (!StrokePath.empty())
    {
        Place.Ink = StrokeTint;
        VectorIconRepository::StrokeSvgPath(Surface, StrokePath, Place, 256.0f);
    }
}

inline void DrawPrimitiveIcon(
    PixelSpace&      Surface,
    Rectangle2D      Box,
    PrimitiveShapeId Shape,
    float            Alpha = 1.0f) noexcept
{
    const uint8_t A    = static_cast<uint8_t>(std::clamp(Alpha * 255.0f, 0.0f, 255.0f));
    const ColorRgba Fill { 23u, 75u, 136u, static_cast<uint8_t>(std::clamp(Alpha * 210.0f, 0.0f, 255.0f)) };
    const ColorRgba Line { 56u, 156u, 255u, A };

    switch (Shape)
    {
    case PrimitiveShapeId::Box:
        DrawSvg256(Surface, Box,
            "M128 40 L204 84 L204 172 L128 216 L52 172 L52 84 Z",
            "M128 40 L204 84 L204 172 L128 216 L52 172 L52 84 Z M52 84 L128 128 L204 84 M128 128 L128 216",
            Fill, Line, 9.0f);
        break;
    case PrimitiveShapeId::Sphere:
        DrawSvg256(Surface, Box,
            "M128 46 A82 82 0 1 0 128 210 A82 82 0 1 0 128 46 Z",
            "M128 46 A82 82 0 1 0 128 210 A82 82 0 1 0 128 46 Z M128 46 A34 82 0 1 0 128 210 A34 82 0 1 0 128 46 Z M46 128 L210 128 M50 104 Q128 138 206 104",
            Fill, Line, 8.5f);
        break;
    case PrimitiveShapeId::Cylinder:
        DrawSvg256(Surface, Box,
            "M54 68 L202 68 L202 188 A74 30 0 0 1 54 188 Z",
            "M54 68 L54 188 A74 30 0 0 0 202 188 L202 68 M128 38 A74 30 0 1 0 128 98 A74 30 0 1 0 128 38 Z",
            Fill, Line, 8.5f);
        break;
    case PrimitiveShapeId::Cone:
        DrawSvg256(Surface, Box,
            "M128 32 L54 186 A74 30 0 0 0 202 186 Z",
            "M128 32 L54 186 A74 30 0 0 0 202 186 Z M54 186 A74 30 0 0 1 202 186",
            Fill, Line, 8.5f);
        break;
    case PrimitiveShapeId::Capsule:
        DrawSvg256(Surface, Box,
            "M76 92 A52 52 0 0 1 180 92 L180 164 A52 52 0 0 1 76 164 Z",
            "M76 92 A52 52 0 0 1 180 92 L180 164 A52 52 0 0 1 76 164 Z M76 92 A52 24 0 0 0 180 92 M76 164 A52 24 0 0 0 180 164",
            Fill, Line, 8.5f);
        break;
    case PrimitiveShapeId::Torus:
        DrawSvg256(Surface, Box,
            "M128 56 C182 56 226 88 226 128 C226 168 182 200 128 200 C74 200 30 168 30 128 C30 88 74 56 128 56 Z",
            "M128 56 C182 56 226 88 226 128 C226 168 182 200 128 200 C74 200 30 168 30 128 C30 88 74 56 128 56 Z M128 102 A40 26 0 1 0 128 154 A40 26 0 1 0 128 102 Z M44 108 Q128 154 212 108",
            Fill, Line, 8.5f);
        break;
    case PrimitiveShapeId::Pipe:
        DrawSvg256(Surface, Box,
            "M54 68 L202 68 L202 188 A74 30 0 0 1 54 188 Z",
            "M54 68 L54 188 A74 30 0 0 0 202 188 L202 68 M128 38 A74 30 0 1 0 128 98 A74 30 0 1 0 128 38 Z M128 53 A38 15 0 1 0 128 83 A38 15 0 1 0 128 53 Z",
            Fill, Line, 8.5f);
        break;
    case PrimitiveShapeId::Pane:
        DrawSvg256(Surface, Box,
            "M60 66 L196 40 L196 190 L60 216 Z",
            "M60 66 L196 40 L196 190 L60 216 Z M60 66 L72 72 L208 46 L196 40 M208 46 L208 196 L196 190 M86 120 L132 90 M102 160 L168 116",
            Fill, Line, 8.5f);
        break;
    case PrimitiveShapeId::HexColumn:
        DrawSvg256(Surface, Box,
            "M68 64 L98 46 L158 46 L188 64 L188 194 L158 212 L98 212 L68 194 Z",
            "M68 64 L98 46 L158 46 L188 64 L188 194 L158 212 L98 212 L68 194 Z M68 64 L98 82 L158 82 L188 64 M98 82 L98 212 M158 82 L158 212",
            Fill, Line, 8.5f);
        break;
    default:
        break;
    }
}

inline void DrawFolderSceneIcon(PixelSpace& Surface, Rectangle2D Box) noexcept
{
    DrawSvg256(Surface, Box,
        "M32 76 L98 76 L116 96 L224 96 L224 198 L32 198 Z",
        "M32 76 L98 76 L116 96 L224 96 L224 198 L32 198 Z M32 114 L224 114",
        ColorRgba { 25u, 72u, 128u, 220u },
        ColorRgba { 68u, 162u, 255u, 255u },
        9.0f);
}

inline void DrawCollectionIcon(PixelSpace& Surface, Rectangle2D Box) noexcept
{
    DrawSvg256(Surface, Box,
        "M52 54 L106 54 L106 108 L52 108 Z M148 54 L204 54 L204 108 L148 108 Z M52 148 L106 148 L106 204 L52 204 Z M148 148 L204 148 L204 204 L148 204 Z",
        "M52 54 L106 54 L106 108 L52 108 Z M148 54 L204 54 L204 108 L148 108 Z M52 148 L106 148 L106 204 L52 204 Z M148 148 L204 148 L204 204 L148 204 Z",
        ColorRgba { 110u, 82u, 170u, 215u },
        ColorRgba { 188u, 152u, 255u, 255u },
        9.0f);
}

inline void DrawMaterialSphereIcon(PixelSpace& Surface, Rectangle2D Box, ColorRgba BaseRgb, bool CutCube = false) noexcept
{
    if (CutCube)
    {
        const Rectangle2D Cube { Box.X + 1.0f, Box.Y + 1.0f, Box.Width - 2.0f, Box.Height - 2.0f };
        Surface.FillRectangle(Cube, BaseRgb, std::min(12.0f, Cube.Width * 0.28f));
        Surface.StrokeRectangle(Cube, ColorRgba { 255u, 255u, 255u, 36u }, std::min(12.0f, Cube.Width * 0.28f), 1.0f);
        const ColorRgba Hi {
            static_cast<uint8_t>(std::min(255, BaseRgb.Red + 42)),
            static_cast<uint8_t>(std::min(255, BaseRgb.Green + 42)),
            static_cast<uint8_t>(std::min(255, BaseRgb.Blue + 42)),
            110u
        };
        Surface.FillRectangle({ Cube.X + 2.0f, Cube.Y + 2.0f, Cube.Width * 0.48f, Cube.Height * 0.42f }, Hi, 6.0f);
        return;
    }

    const Point2D C = Box.Center();
    const float R = std::min(Box.Width, Box.Height) * 0.48f;
    Surface.FillCircle(C, R, BaseRgb);
    const ColorRgba Shadow {
        static_cast<uint8_t>(BaseRgb.Red / 2),
        static_cast<uint8_t>(BaseRgb.Green / 2),
        static_cast<uint8_t>(BaseRgb.Blue / 2),
        120u
    };
    Surface.FillCircle({ C.X + R * 0.14f, C.Y + R * 0.16f }, R * 0.72f, Shadow);
    Surface.FillCircle(C, R * 0.88f, BaseRgb);
    Surface.FillCircle({ C.X - R * 0.28f, C.Y - R * 0.30f }, R * 0.34f, ColorRgba { 255u, 255u, 255u, 90u });
    Surface.StrokeCircle(C, R, ColorRgba { 255u, 255u, 255u, 38u }, 1.0f);
}

inline void DrawSimulateGearPlayIcon(PixelSpace& Surface, Rectangle2D Box) noexcept
{
    const Point2D C = Box.Center();
    const float R = std::min(Box.Width, Box.Height) * 0.42f;
    Surface.FillCircle(C, R, ColorRgba { 74u, 88u, 103u, 255u });
    Surface.StrokeCircle(C, R, ColorRgba { 170u, 183u, 196u, 255u }, 1.2f);
    Surface.FillCircle(C, R * 0.60f, ColorRgba { 29u, 40u, 52u, 255u });
    Surface.StrokeCircle(C, R * 0.60f, ColorRgba { 212u, 222u, 229u, 255u }, 1.0f);
    DrawSvgFill24(Surface, { C.X - R * 0.45f, C.Y - R * 0.48f, R * 0.96f, R * 0.96f },
        "M8 5.5 L19 12 L8 18.5 Z", ColorRgba { 246u, 197u, 94u, 255u });
}

inline void DrawBakeIcon(PixelSpace& Surface, Rectangle2D Box) noexcept
{
    DrawSvgStroke24(Surface, Box,
        "M9 6 L20 6 L20 20 L9 20 Z M13 9 L16 9 L16 12 L13 12 Z M16 12 L19 12 L19 15 L16 15 Z",
        ColorRgba { 158u, 186u, 198u, 255u }, 1.4f);
    DrawSvgStroke24(Surface, Box,
        "M2 13 L11 13 M8 10 L11 13 L8 16",
        ColorRgba { 237u, 193u, 129u, 255u }, 1.8f);
}

inline void DrawLightningIcon(PixelSpace& Surface, Rectangle2D Box, ColorRgba Tint = ColorRgba { 172u, 206u, 255u, 255u }) noexcept
{
    DrawSvgStroke24(Surface, Box,
        "M14 2.5 L6.5 13 L12.5 13 L9.5 21.5 L18 11 L12 11 Z",
        Tint, 1.85f);
}

inline void DrawSlidersGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M4 7 L14 7 M18 7 L20 7 M4 17 L8 17 M12 17 L20 17", Ink, 1.7f);
    const float S = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;
    Surface.StrokeCircle({ Ox + 16.0f * S, Oy + 7.0f * S }, 2.1f * S, Ink, 1.5f);
    Surface.StrokeCircle({ Ox + 10.0f * S, Oy + 17.0f * S }, 2.1f * S, Ink, 1.5f);
}

inline void DrawCheckGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink, float StrokeW = 2.4f) noexcept
{
    DrawSvgStroke24(Surface, Box, "M6 12.5 L10 16.5 L18 7.5", Ink, StrokeW);
}

inline void DrawTriangleGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M12 5 L20 19 L4 19 Z", Ink, 1.8f);
}

inline void DrawSearchGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    const float S = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;
    Surface.StrokeCircle({ Ox + 10.5f * S, Oy + 10.5f * S }, 5.6f * S, Ink, 1.55f);
    Surface.StrokeLine({ Ox + 14.8f * S, Oy + 14.8f * S }, { Ox + 19.5f * S, Oy + 19.5f * S }, Ink, 1.65f);
}

inline void DrawFilterGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M4 7 L20 7 M7 12 L17 12 M10 17 L14 17", Ink, 1.7f);
}

inline void DrawChevronDownGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M7 10 L12 15 L17 10", Ink, 1.7f);
}

inline void DrawChevronRightGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M10 7 L15 12 L10 17", Ink, 1.7f);
}

inline void DrawFrameGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box,
        "M4 9 L4 5 L8 5 M16 5 L20 5 L20 9 M20 15 L20 19 L16 19 M8 19 L4 19 L4 15 M9 9 L15 9 L15 15 L9 15 Z",
        Ink, 1.65f);
}

inline void DrawPulseGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M3 12 L7 12 L9.5 6 L13.5 18 L16 12 L21 12", Ink, 1.75f);
}

inline void DrawWireCubeGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box,
        "M12 3 L20 7.5 L20 16.5 L12 21 L4 16.5 L4 7.5 Z M4 7.5 L12 12 L20 7.5 M12 12 L12 21",
        Ink, 1.65f);
}

inline void DrawSeedsDotsGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    const float S = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;
    DrawSvgStroke24(Surface, Box, "M7.5 8.5 L13.5 6 M16 7.5 L17.5 13.5 M16.5 15.5 L10.5 17", Ink.ScaledAlpha(0.6f), 1.2f);
    Surface.FillCircle({ Ox + 6.0f * S, Oy + 8.0f * S }, 1.8f * S, Ink);
    Surface.FillCircle({ Ox + 15.0f * S, Oy + 6.0f * S }, 1.8f * S, Ink);
    Surface.FillCircle({ Ox + 18.0f * S, Oy + 15.0f * S }, 1.8f * S, Ink);
    Surface.FillCircle({ Ox + 9.0f * S, Oy + 17.0f * S }, 1.8f * S, Ink);
}

inline void DrawGridGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M4 9 L20 9 M4 15 L20 15 M9 4 L9 20 M15 4 L15 20", Ink, 1.7f);
}

inline void DrawArrowNeGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M8 16 L16 8 M9 8 L16 8 L16 15", Ink, 1.7f);
}

inline void DrawDiceGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M5 5 L19 5 L19 19 L5 19 Z", Ink, 1.6f);
    const float S = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;
    Surface.FillCircle({ Ox + 9.0f * S, Oy + 9.0f * S }, 1.2f * S, Ink);
    Surface.FillCircle({ Ox + 15.0f * S, Oy + 15.0f * S }, 1.2f * S, Ink);
    Surface.FillCircle({ Ox + 15.0f * S, Oy + 9.0f * S }, 1.2f * S, Ink);
    Surface.FillCircle({ Ox + 9.0f * S, Oy + 15.0f * S }, 1.2f * S, Ink);
}

inline void DrawEyeGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink, bool Visible) noexcept
{
    if (!Visible)
    {
        DrawSvgStroke24(Surface, Box, "M4 4 L20 20 M4 12 Q12 6 20 12 Q12 18 4 12", Ink, 1.5f);
        return;
    }
    DrawSvgStroke24(Surface, Box, "M3 12 Q12 5.5 21 12 Q12 18.5 3 12 Z", Ink, 1.5f);
    Surface.FillCircle(Box.Center(), std::min(Box.Width, Box.Height) * 0.13f, Ink);
}

inline void DrawSphereWireGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    const Point2D C = Box.Center();
    const float R = std::min(Box.Width, Box.Height) * 0.36f;
    Surface.StrokeCircle(C, R, Ink, 1.55f);
    DrawSvgStroke24(Surface, Box, "M4.5 12 Q12 15 19.5 12 M12 4.5 Q9.5 12 12 19.5", Ink.ScaledAlpha(0.7f), 1.25f);
}

inline void DrawAlgorithmGlyph(PixelSpace& Surface, Rectangle2D Box, uint32_t AlgIndex, ColorRgba Ink) noexcept
{
    switch (AlgIndex % 6u)
    {
    case 0u: // Voronoi
        DrawSvgStroke24(Surface, Box, "M4 6 L9 4 L12 8 L10 13 L4 14 Z M12 8 L18 5 L20 11 L15 14 L10 13 M4 14 L10 13 L15 14 L16 20 L6 20 Z M15 14 L20 11 L20 19 L16 20", Ink, 1.45f);
        break;
    case 1u: // Uniform
        DrawSvgStroke24(Surface, Box, "M3 3 L21 3 L21 21 L3 21 Z M9 3 L8 9 L9 15 L8 21 M15 3 L16 9 L15 15 L16 21 M3 9 L21 9 M3 15 L21 15", Ink, 1.45f);
        break;
    case 2u: // Radial
        Surface.StrokeCircle(Box.Center(), std::min(Box.Width, Box.Height) * 0.13f, Ink, 1.4f);
        Surface.StrokeCircle(Box.Center(), std::min(Box.Width, Box.Height) * 0.30f, Ink, 1.3f);
        DrawSvgStroke24(Surface, Box, "M12 2 L12 9 M12 15 L12 22 M2 12 L9 12 M15 12 L22 12 M5 5 L10 10 M14 14 L19 19 M19 5 L14 10 M10 14 L5 19", Ink, 1.35f);
        break;
    case 3u: // Slices
        DrawSvgStroke24(Surface, Box, "M3 3 L21 3 L21 21 L3 21 Z M3 16 L19 3 M7 21 L21 8 M3 9 L21 18", Ink, 1.45f);
        break;
    case 4u: // Brick
        DrawSvgStroke24(Surface, Box, "M3 4 L21 4 L21 20 L3 20 Z M3 9.3 L21 9.3 M3 14.6 L21 14.6 M9 4 L9 9.3 M16 4 L16 9.3 M6 9.3 L6 14.6 M13 9.3 L13 14.6 M19 9.3 L19 14.6 M9 14.6 L9 20 M16 14.6 L16 20", Ink, 1.45f);
        break;
    case 5u: // Split
        DrawSvgStroke24(Surface, Box, "M3 3 L21 3 L21 21 L3 21 Z M12 3 L12 21 M12 11 L21 11 M16.5 11 L16.5 21 M16.5 16 L21 16", Ink, 1.45f);
        break;
    }
}

} // namespace Frontier::Workspaces::Fracture::FractureIcons

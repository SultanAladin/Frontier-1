/*============================================================================================================================================
                                                               FRACTUREICONS.H
============================================================================================================================================*/
// 🧩 Project Zero SVG vector icon helpers for Fracture Editor (primitives, toolbar glyphs, method cards, dice & material swatches)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/PixelSpace.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <algorithm>
#include <array>
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

// Keeps screen-space stroke thickness consistent (>= 1.1px) regardless of target icon box size
inline void DrawSvgStroke24(
    PixelSpace&      Surface,
    Rectangle2D      Box,
    std::string_view SvgPath,
    ColorRgba        Ink,
    float            ScreenStrokePx = 1.45f) noexcept
{
    const float Size = std::max(8.0f, std::min(Box.Width, Box.Height));
    VectorIconPlacement Place {};
    Place.X           = Box.X + (Box.Width - Size) * 0.5f;
    Place.Y           = Box.Y + (Box.Height - Size) * 0.5f;
    Place.Size        = Size;
    Place.Ink         = Ink;
    Place.StrokeWidth = ScreenStrokePx * (24.0f / Size);
    VectorIconRepository::StrokeSvgPath(Surface, SvgPath, Place, 24.0f);
}

inline void DrawSvgFill24(
    PixelSpace&      Surface,
    Rectangle2D      Box,
    std::string_view SvgPath,
    ColorRgba        Ink) noexcept
{
    const float Size = std::max(8.0f, std::min(Box.Width, Box.Height));
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
    float            ScreenStrokePx = 1.25f) noexcept
{
    const float Size = std::max(8.0f, std::min(Box.Width, Box.Height));
    VectorIconPlacement Place {};
    Place.X           = Box.X + (Box.Width - Size) * 0.5f;
    Place.Y           = Box.Y + (Box.Height - Size) * 0.5f;
    Place.Size        = Size;
    Place.StrokeWidth = ScreenStrokePx * (256.0f / Size);
    if (!FillPath.empty() && FillTint.Alpha > 0u)
    {
        Place.Ink = FillTint;
        VectorIconRepository::FillSvgPath(Surface, FillPath, Place, 256.0f);
    }
    if (!StrokePath.empty() && StrokeTint.Alpha > 0u)
    {
        Place.Ink = StrokeTint;
        VectorIconRepository::StrokeSvgPath(Surface, StrokePath, Place, 256.0f);
    }
}

inline void FillEllipse(
    PixelSpace& Surface,
    Point2D     Center,
    float       Rx,
    float       Ry,
    ColorRgba   Color) noexcept
{
    std::array<Point2D, 24> Pts {};
    for (size_t I = 0u; I < Pts.size(); ++I)
    {
        const float A = (6.2831853f * static_cast<float>(I)) / static_cast<float>(Pts.size());
        Pts[I] = { Center.X + std::cos(A) * Rx, Center.Y + std::sin(A) * Ry };
    }
    Surface.FillPolygon(Pts, Color);
}

inline void StrokeEllipse(
    PixelSpace& Surface,
    Point2D     Center,
    float       Rx,
    float       Ry,
    ColorRgba   Color,
    float       Thickness = 1.2f) noexcept
{
    constexpr size_t N = 24u;
    Point2D Prev { Center.X + Rx, Center.Y };
    for (size_t I = 1u; I <= N; ++I)
    {
        const float A = (6.2831853f * static_cast<float>(I % N)) / static_cast<float>(N);
        const Point2D Next { Center.X + std::cos(A) * Rx, Center.Y + std::sin(A) * Ry };
        Surface.StrokeLine(Prev, Next, Color, Thickness);
        Prev = Next;
    }
}

inline void DrawPrimitiveIcon(
    PixelSpace&      Surface,
    Rectangle2D      Box,
    PrimitiveShapeId Shape,
    float            Alpha = 1.0f) noexcept
{
    const uint8_t   A    = static_cast<uint8_t>(std::clamp(Alpha * 255.0f, 0.0f, 255.0f));
    const ColorRgba Fill { 23u, 75u, 136u, static_cast<uint8_t>(std::clamp(Alpha * 215.0f, 0.0f, 255.0f)) };
    const ColorRgba Line { 56u, 156u, 255u, A };
    const float     Size = std::min(Box.Width, Box.Height);
    const Point2D   C    = Box.Center();

    switch (Shape)
    {
    case PrimitiveShapeId::Box:
        DrawSvg256(Surface, Box,
            "M128 40 L204 84 L204 172 L128 216 L52 172 L52 84 Z",
            "M128 40 L204 84 L204 172 L128 216 L52 172 L52 84 Z M52 84 L128 128 L204 84 M128 128 L128 216",
            Fill, Line, 1.25f);
        break;
    case PrimitiveShapeId::Sphere:
    {
        const float R = Size * 0.36f;
        Surface.FillCircle(C, R, Fill);
        Surface.StrokeCircle(C, R, Line, 1.25f);
        StrokeEllipse(Surface, C, R * 0.44f, R, Line.ScaledAlpha(0.75f), 1.0f);
        Surface.StrokeLine({ C.X - R, C.Y }, { C.X + R, C.Y }, Line.ScaledAlpha(0.75f), 1.0f);
        break;
    }
    case PrimitiveShapeId::Cylinder:
    {
        const float Rx = Size * 0.31f;
        const float Ry = Size * 0.12f;
        const float H  = Size * 0.48f;
        Surface.FillRectangle({ C.X - Rx, C.Y - H * 0.5f, Rx * 2.0f, H }, Fill, 0.0f);
        FillEllipse(Surface, { C.X, C.Y + H * 0.5f }, Rx, Ry, Fill);
        FillEllipse(Surface, { C.X, C.Y - H * 0.5f }, Rx, Ry, ColorRgba { 35u, 102u, 180u, A });
        StrokeEllipse(Surface, { C.X, C.Y - H * 0.5f }, Rx, Ry, Line, 1.2f);
        Surface.StrokeLine({ C.X - Rx, C.Y - H * 0.5f }, { C.X - Rx, C.Y + H * 0.5f }, Line, 1.2f);
        Surface.StrokeLine({ C.X + Rx, C.Y - H * 0.5f }, { C.X + Rx, C.Y + H * 0.5f }, Line, 1.2f);
        StrokeEllipse(Surface, { C.X, C.Y + H * 0.5f }, Rx, Ry, Line, 1.15f);
        break;
    }
    case PrimitiveShapeId::Cone:
    {
        const float Rx = Size * 0.32f;
        const float Ry = Size * 0.12f;
        const float TopY = C.Y - Size * 0.34f;
        const float BotY = C.Y + Size * 0.24f;
        const std::array<Point2D, 3> Tri {{ { C.X, TopY }, { C.X + Rx, BotY }, { C.X - Rx, BotY } }};
        Surface.FillPolygon(Tri, Fill);
        FillEllipse(Surface, { C.X, BotY }, Rx, Ry, Fill);
        StrokeEllipse(Surface, { C.X, BotY }, Rx, Ry, Line, 1.15f);
        Surface.StrokeLine({ C.X, TopY }, { C.X - Rx, BotY }, Line, 1.2f);
        Surface.StrokeLine({ C.X, TopY }, { C.X + Rx, BotY }, Line, 1.2f);
        Surface.FillCircle({ C.X, TopY }, 1.6f, ColorRgba { 217u, 231u, 242u, A });
        break;
    }
    case PrimitiveShapeId::Capsule:
    {
        const float R  = Size * 0.23f;
        const float H  = Size * 0.66f;
        const Rectangle2D Body { C.X - R, C.Y - H * 0.5f, R * 2.0f, H };
        Surface.FillRectangle(Body, Fill, R);
        Surface.StrokeRectangle(Body, Line, R, 1.25f);
        Surface.StrokeLine({ Body.X + 1.0f, C.Y - H * 0.14f }, { Body.Right() - 1.0f, C.Y - H * 0.14f }, Line.ScaledAlpha(0.65f), 1.0f);
        Surface.StrokeLine({ Body.X + 1.0f, C.Y + H * 0.14f }, { Body.Right() - 1.0f, C.Y + H * 0.14f }, Line.ScaledAlpha(0.65f), 1.0f);
        break;
    }
    case PrimitiveShapeId::Torus:
    {
        const float Rx = Size * 0.40f;
        const float Ry = Size * 0.27f;
        FillEllipse(Surface, C, Rx, Ry, Fill);
        StrokeEllipse(Surface, C, Rx, Ry, Line, 1.25f);
        FillEllipse(Surface, C, Rx * 0.42f, Ry * 0.36f, ColorRgba { 18u, 18u, 18u, 255u });
        StrokeEllipse(Surface, C, Rx * 0.42f, Ry * 0.36f, Line, 1.15f);
        break;
    }
    case PrimitiveShapeId::Pipe:
    {
        const float Rx = Size * 0.31f;
        const float Ry = Size * 0.12f;
        const float H  = Size * 0.48f;
        Surface.FillRectangle({ C.X - Rx, C.Y - H * 0.5f, Rx * 2.0f, H }, Fill, 0.0f);
        FillEllipse(Surface, { C.X, C.Y + H * 0.5f }, Rx, Ry, Fill);
        FillEllipse(Surface, { C.X, C.Y - H * 0.5f }, Rx, Ry, ColorRgba { 35u, 102u, 180u, A });
        FillEllipse(Surface, { C.X, C.Y - H * 0.5f }, Rx * 0.50f, Ry * 0.50f, ColorRgba { 16u, 28u, 44u, A });
        StrokeEllipse(Surface, { C.X, C.Y - H * 0.5f }, Rx, Ry, Line, 1.2f);
        StrokeEllipse(Surface, { C.X, C.Y - H * 0.5f }, Rx * 0.50f, Ry * 0.50f, Line, 1.05f);
        Surface.StrokeLine({ C.X - Rx, C.Y - H * 0.5f }, { C.X - Rx, C.Y + H * 0.5f }, Line, 1.2f);
        Surface.StrokeLine({ C.X + Rx, C.Y - H * 0.5f }, { C.X + Rx, C.Y + H * 0.5f }, Line, 1.2f);
        StrokeEllipse(Surface, { C.X, C.Y + H * 0.5f }, Rx, Ry, Line, 1.15f);
        break;
    }
    case PrimitiveShapeId::Pane:
        DrawSvg256(Surface, Box,
            "M64 68 L194 42 L194 188 L64 214 Z",
            "M64 68 L194 42 L194 188 L64 214 Z M92 122 L134 94 M106 158 L164 120",
            Fill, Line, 1.2f);
        break;
    case PrimitiveShapeId::HexColumn:
        DrawSvg256(Surface, Box,
            "M68 64 L98 46 L158 46 L188 64 L188 194 L158 212 L98 212 L68 194 Z",
            "M68 64 L98 46 L158 46 L188 64 L188 194 L158 212 L98 212 L68 194 Z M68 64 L98 82 L158 82 L188 64 M98 82 L98 212 M158 82 L158 212",
            Fill, Line, 1.2f);
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
        ColorRgba { 35u, 82u, 138u, 230u },
        ColorRgba { 92u, 174u, 255u, 255u },
        1.25f);
}

// editor-collection.svg: 3 purple isometric cubes (Fragments group icon)
inline void DrawCollectionIcon(PixelSpace& Surface, Rectangle2D Box) noexcept
{
    const ColorRgba CubeFill   { 89u, 51u, 146u, 255u };
    const ColorRgba CubeStroke { 184u, 121u, 255u, 255u };
    DrawSvgStroke24(Surface, Box, "M9 12 L11 9.5 M15 9.5 L17 12 M10.5 16 L13.5 16", CubeStroke.ScaledAlpha(0.55f), 1.1f);
    DrawSvg256(Surface, Box,
        "M128 51 L153 65 L153 94 L128 108 L103 94 L103 65 Z",
        "M128 51 L153 65 L153 94 L128 108 L103 94 L103 65 Z M103 65 L128 79 L153 65 M128 79 L128 108",
        CubeFill, CubeStroke, 1.1f);
    DrawSvg256(Surface, Box,
        "M77 138 L102 152 L102 181 L77 195 L52 181 L52 152 Z",
        "M77 138 L102 152 L102 181 L77 195 L52 181 L52 152 Z M52 152 L77 166 L102 152 M77 166 L77 195",
        CubeFill, CubeStroke, 1.1f);
    DrawSvg256(Surface, Box,
        "M179 138 L204 152 L204 181 L179 195 L154 181 L154 152 Z",
        "M179 138 L204 152 L204 181 L179 195 L154 181 L154 152 Z M154 152 L179 166 L204 152 M179 166 L179 195",
        CubeFill, CubeStroke, 1.1f);
}

inline void DrawMaterialSphereIcon(PixelSpace& Surface, Rectangle2D Box, ColorRgba BaseRgb, bool CutCube = false) noexcept
{
    if (CutCube)
    {
        const float R = std::min(10.0f, Box.Width * 0.25f);
        const Rectangle2D Cube { Box.X + 1.0f, Box.Y + 1.0f, Box.Width - 2.0f, Box.Height - 2.0f };
        Surface.FillRectangle(Cube, BaseRgb, R);
        Surface.StrokeRectangle(Cube, ColorRgba { 255u, 255u, 255u, 44u }, R, 1.0f);
        const ColorRgba Hi {
            static_cast<uint8_t>(std::min(255, BaseRgb.Red + 36)),
            static_cast<uint8_t>(std::min(255, BaseRgb.Green + 36)),
            static_cast<uint8_t>(std::min(255, BaseRgb.Blue + 36)),
            120u
        };
        Surface.FillRectangle({ Cube.X + 2.5f, Cube.Y + 2.5f, Cube.Width * 0.48f, Cube.Height * 0.42f }, Hi, 5.0f);
        return;
    }

    const Point2D C = Box.Center();
    const float R = std::min(Box.Width, Box.Height) * 0.46f;
    Surface.FillCircle(C, R, ColorRgba {
        static_cast<uint8_t>(BaseRgb.Red * 0.65f),
        static_cast<uint8_t>(BaseRgb.Green * 0.65f),
        static_cast<uint8_t>(BaseRgb.Blue * 0.65f),
        255u
    });
    Surface.FillCircle({ C.X - R * 0.08f, C.Y - R * 0.08f }, R * 0.86f, BaseRgb);
    Surface.FillCircle({ C.X - R * 0.28f, C.Y - R * 0.30f }, R * 0.30f, ColorRgba { 255u, 255u, 255u, 96u });
    Surface.StrokeCircle(C, R, ColorRgba { 255u, 255u, 255u, 42u }, 1.0f);
}

// editor-simulate.svg: 12-tooth gear (#4a5867 / #aab7c4) + dark inner well (#1d2834) + golden play triangle (#f6c55e)
inline void DrawSimulateGearPlayIcon(PixelSpace& Surface, Rectangle2D Box) noexcept
{
    const Point2D C = Box.Center();
    const float R = std::min(Box.Width, Box.Height) * 0.44f;
    std::array<Point2D, 24> Gear {};
    for (int I = 0; I < 24; ++I)
    {
        const float Ang = (static_cast<float>(I) / 24.0f) * 6.2831853f;
        const float Rad = (I % 2 == 0) ? R : (R * 0.78f);
        Gear[static_cast<size_t>(I)] = { C.X + Rad * std::cos(Ang), C.Y + Rad * std::sin(Ang) };
    }
    Surface.FillPolygon(Gear, ColorRgba { 74u, 88u, 103u, 255u });
    Surface.StrokePolyline(Gear, ColorRgba { 170u, 183u, 196u, 255u }, 1.1f, true);
    Surface.FillCircle(C, R * 0.48f, ColorRgba { 29u, 40u, 52u, 255u });
    Surface.StrokeCircle(C, R * 0.48f, ColorRgba { 212u, 222u, 229u, 255u }, 1.0f);
    const std::array<Point2D, 3> PlayTri {{
        { C.X - R * 0.16f, C.Y - R * 0.26f },
        { C.X + R * 0.28f, C.Y },
        { C.X - R * 0.16f, C.Y + R * 0.26f },
    }};
    Surface.FillPolygon(PlayTri, ColorRgba { 246u, 197u, 94u, 255u });
}

// editor-bake.svg: rounded #303b46 container + 2x3 cell grid + golden arrow (#edc181) + sparkle (#f4d69e) + green check (#95c3ad)
inline void DrawBakeIcon(PixelSpace& Surface, Rectangle2D Box) noexcept
{
    const float S  = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;

    const Rectangle2D Slab { Ox + 9.0f * S, Oy + 5.5f * S, 13.0f * S, 15.0f * S };
    Surface.FillRectangle(Slab, ColorRgba { 48u, 59u, 70u, 255u }, 3.0f * S);
    Surface.StrokeRectangle(Slab, ColorRgba { 154u, 175u, 189u, 255u }, 3.0f * S, 1.2f);

    const ColorRgba LightCell { 158u, 186u, 198u, 255u };
    const ColorRgba DarkCell  { 83u, 109u, 123u, 255u };
    const float Cw = 3.0f * S;
    for (int R = 0; R < 3; ++R)
    {
        for (int C = 0; C < 2; ++C)
        {
            Surface.FillRectangle(
                { Ox + (12.8f + static_cast<float>(C) * 3.1f) * S, Oy + (8.6f + static_cast<float>(R) * 3.1f) * S, Cw, Cw },
                ((R + C) % 2 == 0) ? LightCell : DarkCell, 0.5f);
        }
    }

    // Golden incoming arrow
    const ColorRgba Gold { 237u, 193u, 129u, 255u };
    Surface.StrokeLine({ Ox + 2.0f * S, Oy + 13.0f * S }, { Ox + 10.8f * S, Oy + 13.0f * S }, Gold, 1.7f);
    Surface.StrokeLine({ Ox + 8.0f * S, Oy + 10.0f * S }, { Ox + 10.8f * S, Oy + 13.0f * S }, Gold, 1.7f);
    Surface.StrokeLine({ Ox + 8.0f * S, Oy + 16.0f * S }, { Ox + 10.8f * S, Oy + 13.0f * S }, Gold, 1.7f);

    // Top-left sparkle star
    const std::array<Point2D, 4> Star {{
        { Ox + 5.0f * S, Oy + 1.8f * S },
        { Ox + 7.8f * S, Oy + 4.8f * S },
        { Ox + 5.0f * S, Oy + 7.8f * S },
        { Ox + 2.2f * S, Oy + 4.8f * S },
    }};
    Surface.FillPolygon(Star, ColorRgba { 244u, 214u, 158u, 255u });

    // Bottom-right check accent
    const ColorRgba Mint { 149u, 195u, 173u, 255u };
    Surface.StrokeLine({ Ox + 16.5f * S, Oy + 20.5f * S }, { Ox + 18.5f * S, Oy + 22.5f * S }, Mint, 1.5f);
    Surface.StrokeLine({ Ox + 18.5f * S, Oy + 22.5f * S }, { Ox + 22.5f * S, Oy + 18.5f * S }, Mint, 1.5f);
}

// Solid crisp lightning bolt (Realtime card & Impact toolbar button)
inline void DrawLightningIcon(PixelSpace& Surface, Rectangle2D Box, ColorRgba Tint = ColorRgba { 246u, 197u, 94u, 255u }) noexcept
{
    const float S  = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;
    const std::array<Point2D, 4> TopTri {{
        { Ox + 14.5f * S, Oy + 2.5f * S },
        { Ox + 5.5f * S,  Oy + 13.5f * S },
        { Ox + 12.0f * S, Oy + 13.5f * S },
        { Ox + 13.5f * S, Oy + 10.5f * S },
    }};
    const std::array<Point2D, 3> BotTri {{
        { Ox + 11.5f * S, Oy + 10.5f * S },
        { Ox + 18.5f * S, Oy + 10.5f * S },
        { Ox + 9.5f * S,  Oy + 21.5f * S },
    }};
    Surface.FillPolygon(TopTri, Tint);
    Surface.FillPolygon(BotTri, Tint);
}

inline void DrawEditPencilGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M5 19 L6 15 L16 5 L19 8 L9 18 Z M14 7 L17 10", Ink, 1.45f);
}

inline void DrawSimFlaskGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M10 4 L14 4 M10.5 4 L10.5 10 L5 19 L19 19 L13.5 10 L13.5 4 M7.5 15 L16.5 15", Ink, 1.45f);
}

inline void DrawPlayTriangleIcon(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    const float S  = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;
    const std::array<Point2D, 3> Tri {{
        { Ox + 8.0f * S,  Oy + 5.5f * S },
        { Ox + 19.0f * S, Oy + 12.0f * S },
        { Ox + 8.0f * S,  Oy + 18.5f * S },
    }};
    Surface.FillPolygon(Tri, Ink);
}

inline void DrawSlidersGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M4 7 L14 7 M18 7 L20 7 M4 17 L8 17 M12 17 L20 17", Ink, 1.5f);
    const float S = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;
    Surface.StrokeCircle({ Ox + 16.0f * S, Oy + 7.0f * S }, 2.1f * S, Ink, 1.4f);
    Surface.StrokeCircle({ Ox + 10.0f * S, Oy + 17.0f * S }, 2.1f * S, Ink, 1.4f);
}

inline void DrawCheckGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink, float StrokeW = 2.0f) noexcept
{
    DrawSvgStroke24(Surface, Box, "M6 12.5 L10 16.5 L18 7.5", Ink, StrokeW);
}

inline void DrawTriangleGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M12 5 L20 19 L4 19 Z", Ink, 1.5f);
}

inline void DrawSearchGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    const float S = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;
    Surface.StrokeCircle({ Ox + 10.5f * S, Oy + 10.5f * S }, 5.6f * S, Ink, 1.45f);
    Surface.StrokeLine({ Ox + 14.8f * S, Oy + 14.8f * S }, { Ox + 19.5f * S, Oy + 19.5f * S }, Ink, 1.55f);
}

inline void DrawFilterGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M4 7 L20 7 M7 12 L17 12 M10 17 L14 17", Ink, 1.45f);
}

inline void DrawChevronDownGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M7 10 L12 15 L17 10", Ink, 1.45f);
}

inline void DrawChevronRightGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M10 7 L15 12 L10 17", Ink, 1.45f);
}

inline void DrawFrameGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box,
        "M4 9 L4 5 L8 5 M16 5 L20 5 L20 9 M20 15 L20 19 L16 19 M8 19 L4 19 L4 15 M9 9 L15 9 L15 15 L9 15 Z",
        Ink, 1.4f);
}

inline void DrawPulseGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M3 12 L7 12 L9.5 6 L13.5 18 L16 12 L21 12", Ink, 1.5f);
}

inline void DrawWireCubeGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box,
        "M12 3 L20 7.5 L20 16.5 L12 21 L4 16.5 L4 7.5 Z M4 7.5 L12 12 L20 7.5 M12 12 L12 21",
        Ink, 1.4f);
}

inline void DrawSeedsDotsGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    const float S = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;
    DrawSvgStroke24(Surface, Box, "M7.5 8.5 L13.5 6 M16 7.5 L17.5 13.5 M16.5 15.5 L10.5 17", Ink.ScaledAlpha(0.6f), 1.1f);
    Surface.FillCircle({ Ox + 6.0f * S, Oy + 8.0f * S }, 1.8f * S, Ink);
    Surface.FillCircle({ Ox + 15.0f * S, Oy + 6.0f * S }, 1.8f * S, Ink);
    Surface.FillCircle({ Ox + 18.0f * S, Oy + 15.0f * S }, 1.8f * S, Ink);
    Surface.FillCircle({ Ox + 9.0f * S, Oy + 17.0f * S }, 1.8f * S, Ink);
}

inline void DrawGridGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M4 9 L20 9 M4 15 L20 15 M9 4 L9 20 M15 4 L15 20", Ink, 1.45f);
}

inline void DrawArrowNeGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    DrawSvgStroke24(Surface, Box, "M8 16 L16 8 M9 8 L16 8 L16 15", Ink, 1.35f);
}

// Isometric 3D die icon matching Icons/dice.svg (for Reroll seed pill)
inline void DrawDiceGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba /*Ink*/) noexcept
{
    const float S  = std::min(Box.Width, Box.Height) / 24.0f;
    const float Ox = Box.X + (Box.Width - 24.0f * S) * 0.5f;
    const float Oy = Box.Y + (Box.Height - 24.0f * S) * 0.5f;
    const std::array<Point2D, 4> Top {{
        { Ox + 12.0f * S, Oy + 3.0f * S },
        { Ox + 20.5f * S, Oy + 7.5f * S },
        { Ox + 12.0f * S, Oy + 12.0f * S },
        { Ox + 3.5f * S,  Oy + 7.5f * S },
    }};
    const std::array<Point2D, 4> Left {{
        { Ox + 3.5f * S,  Oy + 7.5f * S },
        { Ox + 12.0f * S, Oy + 12.0f * S },
        { Ox + 12.0f * S, Oy + 21.0f * S },
        { Ox + 3.5f * S,  Oy + 16.5f * S },
    }};
    const std::array<Point2D, 4> Right {{
        { Ox + 12.0f * S, Oy + 12.0f * S },
        { Ox + 20.5f * S, Oy + 7.5f * S },
        { Ox + 20.5f * S, Oy + 16.5f * S },
        { Ox + 12.0f * S, Oy + 21.0f * S },
    }};
    Surface.FillPolygon(Top,   ColorRgba { 236u, 238u, 236u, 255u });
    Surface.FillPolygon(Left,  ColorRgba { 202u, 206u, 209u, 255u });
    Surface.FillPolygon(Right, ColorRgba { 148u, 156u, 162u, 255u });
    const ColorRgba Pip { 28u, 33u, 37u, 255u };
    Surface.FillCircle({ Ox + 12.0f * S, Oy + 7.5f * S },  1.1f * S, Pip);
    Surface.FillCircle({ Ox + 6.5f * S,  Oy + 12.5f * S }, 1.0f * S, Pip);
    Surface.FillCircle({ Ox + 9.2f * S,  Oy + 16.5f * S }, 1.0f * S, Pip);
    Surface.FillCircle({ Ox + 15.0f * S, Oy + 14.5f * S }, 1.0f * S, Pip);
    Surface.FillCircle({ Ox + 18.0f * S, Oy + 12.5f * S }, 1.0f * S, Pip);
}

inline void DrawEyeGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink, bool Visible) noexcept
{
    if (!Visible)
    {
        DrawSvgStroke24(Surface, Box, "M4 4 L20 20 M4 12 Q12 6 20 12 Q12 18 4 12", Ink, 1.35f);
        return;
    }
    DrawSvgStroke24(Surface, Box, "M3 12 Q12 5.5 21 12 Q12 18.5 3 12 Z", Ink, 1.35f);
    Surface.FillCircle(Box.Center(), std::min(Box.Width, Box.Height) * 0.13f, Ink);
}

inline void DrawSphereWireGlyph(PixelSpace& Surface, Rectangle2D Box, ColorRgba Ink) noexcept
{
    const Point2D C = Box.Center();
    const float R = std::min(Box.Width, Box.Height) * 0.36f;
    Surface.StrokeCircle(C, R, Ink, 1.4f);
    DrawSvgStroke24(Surface, Box, "M4.5 12 Q12 15 19.5 12 M12 4.5 Q9.5 12 12 19.5", Ink.ScaledAlpha(0.75f), 1.15f);
}

inline void DrawAlgorithmGlyph(PixelSpace& Surface, Rectangle2D Box, uint32_t AlgIndex, ColorRgba Ink) noexcept
{
    switch (AlgIndex % 6u)
    {
    case 0u: // Voronoi
        DrawSvgStroke24(Surface, Box, "M4 6 L9 4 L12 8 L10 13 L4 14 Z M12 8 L18 5 L20 11 L15 14 L10 13 M4 14 L10 13 L15 14 L16 20 L6 20 Z M15 14 L20 11 L20 19 L16 20", Ink, 1.35f);
        break;
    case 1u: // Uniform
        DrawSvgStroke24(Surface, Box, "M3 3 L21 3 L21 21 L3 21 Z M9 3 L8 9 L9 15 L8 21 M15 3 L16 9 L15 15 L16 21 M3 9 L21 9 M3 15 L21 15", Ink, 1.35f);
        break;
    case 2u: // Radial
        Surface.StrokeCircle(Box.Center(), std::min(Box.Width, Box.Height) * 0.13f, Ink, 1.3f);
        Surface.StrokeCircle(Box.Center(), std::min(Box.Width, Box.Height) * 0.30f, Ink, 1.2f);
        DrawSvgStroke24(Surface, Box, "M12 2 L12 9 M12 15 L12 22 M2 12 L9 12 M15 12 L22 12 M5 5 L10 10 M14 14 L19 19 M19 5 L14 10 M10 14 L5 19", Ink, 1.25f);
        break;
    case 3u: // Slices
        DrawSvgStroke24(Surface, Box, "M3 3 L21 3 L21 21 L3 21 Z M3 16 L19 3 M7 21 L21 8 M3 9 L21 18", Ink, 1.35f);
        break;
    case 4u: // Brick
        DrawSvgStroke24(Surface, Box, "M3 4 L21 4 L21 20 L3 20 Z M3 9.3 L21 9.3 M3 14.6 L21 14.6 M9 4 L9 9.3 M16 4 L16 9.3 M6 9.3 L6 14.6 M13 9.3 L13 14.6 M19 9.3 L19 14.6 M9 14.6 L9 20 M16 14.6 L16 20", Ink, 1.35f);
        break;
    case 5u: // Split
        DrawSvgStroke24(Surface, Box, "M3 3 L21 3 L21 21 L3 21 Z M12 3 L12 21 M12 11 L21 11 M16.5 11 L16.5 21 M16.5 16 L21 16", Ink, 1.35f);
        break;
    }
}

} // namespace Frontier::Workspaces::Fracture::FractureIcons

/*============================================================================================================================================
                                                       WORLDENVIRONMENTINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of Editor-only Environment Inspectors (Atmosphere, Sun, Clouds, Moon, Stars) matched to ProjectZeroEditor reference

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/WorldEnvironmentInspector.h"

#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/GeometricRaster/Atmosphere/CelestialEphemerisSolver.h"
#include "Engine/GeometricRaster/Atmosphere/VolumetricCloudSequence.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

struct QuickTileSpec
{
    const char*               Label  = "";
    EnvironmentSymbolCategory Symbol = EnvironmentSymbolCategory::None;
    bool                      Active = false;
};

namespace
{

constexpr ColorRgba SectionBg      { 26u,  26u,  26u,  255u }; // [-] - #1A1A1A rounded section shell
constexpr ColorRgba SectionBorder  { 42u,  42u,  42u,  255u }; // [-] - #2A2A2A subtle section outline
constexpr ColorRgba TileFillIdle   { 32u,  32u,  32u,  255u }; // [-] - #202020 quick-control tile
constexpr ColorRgba TileFillHover  { 42u,  42u,  42u,  255u }; // [-] - #2A2A2A hovered tile
constexpr ColorRgba TileBorder     { 48u,  48u,  48u,  255u }; // [-] - #303030 tile outline
constexpr ColorRgba GreenWellFill  { 26u,  54u,  36u,  255u }; // [-] - Emerald icon well for active toggles
constexpr ColorRgba GreenWellGlow  { 74u,  222u, 128u, 255u }; // [-] - #4ADE80 active green accent
constexpr ColorRgba MutedWellFill  { 40u,  40u,  40u,  255u }; // [-] - Inactive icon well
constexpr ColorRgba SubtleInk      { 138u, 138u, 138u, 255u }; // [-] - #8A8A8A secondary label
constexpr ColorRgba DimInk         { 96u,  96u,  96u,  255u }; // [-] - #606060 faint caption
constexpr ColorRgba ChamberBg      { 9u,   10u,  13u,  255u }; // [-] - Deep dark preview chamber

[[nodiscard]] const char* ResolveCompassLabel(float AzimuthDeg) noexcept
{
    float Wrapped = std::fmod(AzimuthDeg, 360.0f);
    if (Wrapped < 0.0f) Wrapped += 360.0f;
    if (Wrapped < 22.5f || Wrapped >= 337.5f) return "north";
    if (Wrapped < 67.5f)  return "northeast";
    if (Wrapped < 112.5f) return "east";
    if (Wrapped < 157.5f) return "southeast";
    if (Wrapped < 202.5f) return "south";
    if (Wrapped < 247.5f) return "southwest";
    if (Wrapped < 292.5f) return "west";
    return "northwest";
}

[[nodiscard]] ColorRgba LinearRgbToRgba8(const Mathematics::LinearRGB& Linear) noexcept
{
    auto Encode = [](float V) noexcept -> uint8_t
    {
        const float Clamped = std::clamp(V, 0.0f, 1.0f);
        const float Srgb    = std::pow(Clamped, 1.0f / 2.2f);
        return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
    };
    return ColorRgba{ Encode(Linear.R), Encode(Linear.G), Encode(Linear.B), 255u };
}

[[nodiscard]] uint32_t HashU32(uint32_t Seed) noexcept
{
    Seed ^= Seed >> 16u;
    Seed *= 0x7FEB352Du;
    Seed ^= Seed >> 15u;
    Seed *= 0x846CA68Bu;
    Seed ^= Seed >> 16u;
    return Seed;
}

[[nodiscard]] const char* QueryCategoryHeadline(WorldEnvironmentCategory Category) noexcept
{
    switch (Category)
    {
        case WorldEnvironmentCategory::Atmosphere: return "Atmosphere";
        case WorldEnvironmentCategory::Sun:        return "Sun";
        case WorldEnvironmentCategory::Clouds:     return "Clouds";
        case WorldEnvironmentCategory::Moon:       return "Moon";
        case WorldEnvironmentCategory::Stars:      return "Stars";
        default:                                   return "Environment";
    }
}

[[nodiscard]] const char* QueryCategorySubtitle(WorldEnvironmentCategory Category) noexcept
{
    switch (Category)
    {
        case WorldEnvironmentCategory::Atmosphere: return "Procedural sky";
        case WorldEnvironmentCategory::Sun:        return "Directional light";
        case WorldEnvironmentCategory::Clouds:     return "Global cloud layer";
        case WorldEnvironmentCategory::Moon:       return "Lunar bodies  ·  six slots";
        case WorldEnvironmentCategory::Stars:      return "Starfield  ·  scintillation  ·  celestial pole";
        default:                                   return "Environment";
    }
}

void DrawQuickGlyph(
    PixelSpace&               Surface,
    EnvironmentSymbolCategory Symbol,
    Point2D                   Center,
    float                     Size,
    bool                      Active) noexcept
{
    const ColorRgba Ink = Active ? GreenWellGlow : ColorRgba{ 146u, 146u, 146u, 255u };
    const float     R   = Size * 0.5f;
    switch (Symbol)
    {
        case EnvironmentSymbolCategory::Atmosphere:
            Surface.StrokeCircle(Center, R * 0.72f, Ink, 1.7f);
            Surface.FillCircle(Center, R * 0.24f, Ink);
            break;
        case EnvironmentSymbolCategory::Sun:
            Surface.FillCircle(Center, R * 0.36f, Ink);
            for (int32_t Ray = 0; Ray < 8; ++Ray)
            {
                const float A = static_cast<float>(Ray) * 0.78539816f;
                Surface.StrokeLine(
                    { Center.X + std::cos(A) * R * 0.54f, Center.Y + std::sin(A) * R * 0.54f },
                    { Center.X + std::cos(A) * R * 0.84f, Center.Y + std::sin(A) * R * 0.84f },
                    Ink,
                    1.6f);
            }
            break;
        case EnvironmentSymbolCategory::Clouds:
            Surface.FillCircle({ Center.X - R * 0.26f, Center.Y + R * 0.14f }, R * 0.32f, Ink);
            Surface.FillCircle({ Center.X + R * 0.24f, Center.Y + R * 0.14f }, R * 0.28f, Ink);
            Surface.FillCircle({ Center.X - R * 0.02f, Center.Y - R * 0.12f }, R * 0.38f, Ink);
            Surface.FillRectangle({ Center.X - R * 0.32f, Center.Y + R * 0.08f, R * 0.64f, R * 0.32f }, Ink, R * 0.12f);
            break;
        case EnvironmentSymbolCategory::Moon:
            Surface.FillCircle(Center, R * 0.64f, Ink);
            Surface.FillCircle({ Center.X + R * 0.24f, Center.Y - R * 0.14f }, R * 0.52f, Active ? GreenWellFill : MutedWellFill);
            break;
        case EnvironmentSymbolCategory::Stars:
        {
            const std::array<Point2D, 8> Spark = {{
                { Center.X,           Center.Y - R * 0.74f },
                { Center.X + R * 0.20f, Center.Y - R * 0.20f },
                { Center.X + R * 0.74f, Center.Y           },
                { Center.X + R * 0.20f, Center.Y + R * 0.20f },
                { Center.X,           Center.Y + R * 0.74f },
                { Center.X - R * 0.20f, Center.Y + R * 0.20f },
                { Center.X - R * 0.74f, Center.Y           },
                { Center.X - R * 0.20f, Center.Y - R * 0.20f },
            }};
            Surface.FillPolygon(Spark, Ink);
            break;
        }
        default:
            // Eye visibility glyph
            Surface.StrokeCircle(Center, R * 0.62f, Ink, 1.7f);
            Surface.FillCircle(Center, R * 0.26f, Ink);
            break;
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 CONFIGURATION & QUERIES
//------------------------------------------------------------------------------------------------------------------------

void WorldEnvironmentInspector::ConfigureActivePage(WorldEnvironmentCategory Category, uint32_t PageIndex) noexcept
{
    const std::size_t Idx = static_cast<std::size_t>(Category);
    if (Idx < ActivePageByCategory.size())
    {
        ActivePageByCategory[Idx] = PageIndex;
        ScrollOffsetY             = 0.0f;
    }
}

uint32_t WorldEnvironmentInspector::QueryActivePage(WorldEnvironmentCategory Category) const noexcept
{
    const std::size_t Idx = static_cast<std::size_t>(Category);
    return (Idx < ActivePageByCategory.size()) ? ActivePageByCategory[Idx] : 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHARED BUILDING BLOCKS
//------------------------------------------------------------------------------------------------------------------------

float WorldEnvironmentInspector::RenderPageCarousel(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    std::span<const char* const> PageTitles,
    uint32_t&                    InOutActivePage,
    const PaneInputReading&      Input) noexcept
{
    const uint32_t PageCount = static_cast<uint32_t>(PageTitles.size());
    if (PageCount == 0u)
    {
        return TopY;
    }
    InOutActivePage = std::min(InOutActivePage, PageCount - 1u);

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   RowH = 28.0f;

    // Left "<" arrow button
    const Rectangle2D PrevBtn{ LeftX, TopY, 24.0f, RowH };
    const bool        PrevHov = PrevBtn.Contains(Cursor);
    Surface.FillCircle(PrevBtn.Center(), 11.0f, PrevHov ? ColorRgba{ 44u, 44u, 44u, 255u } : ColorRgba{ 28u, 28u, 28u, 255u });
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { PrevBtn.X + 8.5f, PrevBtn.Y + 6.5f },
        11.0f,
        PrevHov ? WorkspaceSurfaceColors::PrimaryInk : SubtleInk,
        "<");
    if (PrevHov && Input.PrimaryPointer.Pressed)
    {
        InOutActivePage = (InOutActivePage == 0u) ? (PageCount - 1u) : (InOutActivePage - 1u);
        ScrollOffsetY   = 0.0f;
    }

    // Right page counter "1 / N" & ">" arrow button
    char CounterBuf[24]{};
    std::snprintf(CounterBuf, sizeof(CounterBuf), "%u / %u", InOutActivePage + 1u, PageCount);
    const float CounterW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, CounterBuf);
    const float RightEdge = LeftX + InnerWidth;
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { RightEdge - CounterW, TopY + 7.5f },
        10.5f,
        SubtleInk,
        CounterBuf);

    const Rectangle2D NextBtn{ RightEdge - CounterW - 30.0f, TopY, 24.0f, RowH };
    const bool        NextHov = NextBtn.Contains(Cursor);
    Surface.FillCircle(NextBtn.Center(), 11.0f, NextHov ? ColorRgba{ 44u, 44u, 44u, 255u } : ColorRgba{ 28u, 28u, 28u, 255u });
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { NextBtn.X + 9.0f, NextBtn.Y + 6.5f },
        11.0f,
        NextHov ? WorkspaceSurfaceColors::PrimaryInk : SubtleInk,
        ">");
    if (NextHov && Input.PrimaryPointer.Pressed)
    {
        InOutActivePage = (InOutActivePage + 1u) % PageCount;
        ScrollOffsetY   = 0.0f;
    }

    // Numbered tab pills in center strip (clipped cleanly between PrevBtn & NextBtn)
    const Rectangle2D PillClip{ PrevBtn.Right() + 6.0f, TopY, std::max(40.0f, NextBtn.X - PrevBtn.Right() - 12.0f), RowH };
    Surface.PushClip(PillClip);

    // Start from ActivePage or previous page so active pill is always visible
    const uint32_t StartIdx = (InOutActivePage > 0u && InOutActivePage + 1u == PageCount) ? (InOutActivePage - 1u) : InOutActivePage;
    float PillX = PillClip.X;
    for (uint32_t Idx = StartIdx; Idx < PageCount && PillX < PillClip.Right(); ++Idx)
    {
        const bool  Active = (Idx == InOutActivePage);
        const float LabelW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 11.0f, PageTitles[Idx]);
        const float PillW  = 28.0f + LabelW + 12.0f;
        const Rectangle2D PillRect{ PillX, TopY, PillW, RowH };
        const bool Hovered = PillClip.Contains(Cursor) && PillRect.Contains(Cursor);

        if (Active)
        {
            Surface.FillRectangle(PillRect, ColorRgba{ 44u, 44u, 44u, 255u }, 14.0f);
            Surface.StrokeRectangle(PillRect, ColorRgba{ 64u, 64u, 64u, 255u }, 14.0f, 1.0f);
        }
        else if (Hovered)
        {
            Surface.FillRectangle(PillRect, ColorRgba{ 32u, 32u, 32u, 255u }, 14.0f);
        }

        // Circular number badge
        const Point2D BadgeCenter{ PillRect.X + 14.0f, PillRect.Center().Y };
        Surface.FillCircle(BadgeCenter, 9.0f, Active ? ColorRgba{ 244u, 244u, 244u, 255u } : ColorRgba{ 38u, 38u, 38u, 255u });
        char NumBuf[16]{};
        std::snprintf(NumBuf, sizeof(NumBuf), "%u", Idx + 1u);
        const float NumW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.5f, NumBuf);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { BadgeCenter.X - NumW * 0.5f, BadgeCenter.Y - 5.5f },
            9.5f,
            Active ? ColorRgba{ 16u, 16u, 16u, 255u } : SubtleInk,
            NumBuf);

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            Active ? FontWeightClass::Medium : FontWeightClass::Regular,
            { PillRect.X + 28.0f, PillRect.Y + 7.5f },
            11.0f,
            Active ? WorkspaceSurfaceColors::PrimaryInk : SubtleInk,
            PageTitles[Idx]);

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            InOutActivePage = Idx;
            ScrollOffsetY   = 0.0f;
        }
        PillX += PillW + 6.0f;
    }

    Surface.PopClip();
    return TopY + RowH + 16.0f;
}

float WorldEnvironmentInspector::RenderQuickControlGrid(
    PixelSpace&                   Surface,
    float                         LeftX,
    float                         TopY,
    float                         InnerWidth,
    std::span<const QuickTileSpec> Tiles,
    const PaneInputReading&       Input,
    int32_t&                      OutClickedIndex) noexcept
{
    OutClickedIndex = -1;
    if (Tiles.empty())
    {
        return TopY;
    }

    uint32_t ActiveCount = 0u;
    for (const QuickTileSpec& T : Tiles)
    {
        if (T.Active) ++ActiveCount;
    }

    const uint32_t Rows   = (static_cast<uint32_t>(Tiles.size()) + 1u) / 2u;
    const float    Pad    = 16.0f;
    const float    Gap    = 10.0f;
    const float    TileH  = 86.0f;
    const float    BoxH   = Pad + 24.0f + static_cast<float>(Rows) * TileH + static_cast<float>(Rows > 0u ? Rows - 1u : 0u) * Gap + Pad;

    const Rectangle2D SectionRect{ LeftX, TopY, InnerWidth, BoxH };
    Surface.FillRectangle(SectionRect, SectionBg, 18.0f);
    Surface.StrokeRectangle(SectionRect, SectionBorder, 18.0f, 1.0f);

    // Header: "Quick controls" + "N / M on"
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + Pad, TopY + Pad },
        12.5f,
        ColorRgba{ 214u, 214u, 214u, 255u },
        "Quick controls");

    char ActiveNum[16]{};
    char TotalSuffix[24]{};
    std::snprintf(ActiveNum, sizeof(ActiveNum), "%u", ActiveCount);
    std::snprintf(TotalSuffix, sizeof(TotalSuffix), " / %u on", static_cast<uint32_t>(Tiles.size()));
    const float NumW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 11.0f, ActiveNum);
    const float SufW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, TotalSuffix);
    const float BadgeX = SectionRect.Right() - Pad - (NumW + SufW);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Medium,
        { BadgeX, TopY + Pad + 1.0f },
        11.0f,
        ActiveCount > 0u ? GreenWellGlow : SubtleInk,
        ActiveNum);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { BadgeX + NumW, TopY + Pad + 1.0f },
        11.0f,
        SubtleInk,
        TotalSuffix);

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   TileW = std::floor((InnerWidth - Pad * 2.0f - Gap) * 0.5f);
    const float   GridY = TopY + Pad + 28.0f;

    for (std::size_t Idx = 0u; Idx < Tiles.size(); ++Idx)
    {
        const uint32_t Col = static_cast<uint32_t>(Idx) % 2u;
        const uint32_t Row = static_cast<uint32_t>(Idx) / 2u;
        const Rectangle2D Tile{
            LeftX + Pad + static_cast<float>(Col) * (TileW + Gap),
            GridY + static_cast<float>(Row) * (TileH + Gap),
            TileW,
            TileH,
        };
        const bool Hovered = Tile.Contains(Cursor);

        Surface.FillRectangle(Tile, Hovered ? TileFillHover : TileFillIdle, 14.0f);
        Surface.StrokeRectangle(
            Tile,
            Hovered ? ColorRgba{ 72u, 72u, 72u, 255u } : TileBorder,
            14.0f,
            1.0f);

        // Centered icon well (34x34 rounded square)
        const Rectangle2D IconWell{ Tile.Center().X - 17.0f, Tile.Y + 14.0f, 34.0f, 34.0f };
        Surface.FillRectangle(IconWell, Tiles[Idx].Active ? GreenWellFill : MutedWellFill, 10.0f);
        DrawQuickGlyph(Surface, Tiles[Idx].Symbol, IconWell.Center(), 18.0f, Tiles[Idx].Active);

        // Centered label
        const float MaxLW = Tile.Width - 12.0f;
        const float LW    = std::min(MaxLW, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, Tiles[Idx].Label));
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { Tile.Center().X - LW * 0.5f, Tile.Bottom() - 22.0f },
            MaxLW,
            10.5f,
            Tiles[Idx].Active ? WorkspaceSurfaceColors::PrimaryInk : SubtleInk,
            Tiles[Idx].Label);

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            OutClickedIndex = static_cast<int32_t>(Idx);
        }
    }

    return SectionRect.Bottom() + 14.0f;
}

float WorldEnvironmentInspector::RenderLabeledScalarSlider(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    uint32_t                ControlId,
    const char*             Label,
    const char*             UnitText,
    const char*             FormatSpec,
    float                   MinVal,
    float                   MaxVal,
    float&                  InOutValue,
    const PaneInputReading& Input,
    bool&                   OutChanged) noexcept
{
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, TopY },
        11.0f,
        ColorRgba{ 196u, 196u, 196u, 255u },
        Label);

    char ValueBuf[32]{};
    std::snprintf(ValueBuf, sizeof(ValueBuf), FormatSpec, InOutValue);

    const float CapsuleW   = (UnitText != nullptr && UnitText[0] != '\0') ? 90.0f : 68.0f;
    const float UnitCellW  = (UnitText != nullptr && UnitText[0] != '\0') ? 34.0f : 0.0f;
    const float RowY       = TopY + 18.0f;
    ScalarSlider::ConstructReadoutCapsule(
        Surface,
        LeftX,
        RowY,
        ValueBuf,
        (UnitText != nullptr) ? UnitText : "",
        1.0f,
        CapsuleW,
        UnitCellW);

    const float       Gap       = 10.0f;
    const Rectangle2D TrackRect { LeftX + CapsuleW + Gap, RowY + 4.0f, std::max(60.0f, InnerWidth - CapsuleW - Gap), 20.0f };
    const bool        Dragging  = (ActiveDragControlId == ControlId);
    float             Updated   = InOutValue;
    const ControlContact Contact = ScalarSlider::Construct(
        Surface,
        TrackRect,
        MinVal,
        MaxVal,
        InOutValue,
        Dragging,
        Input.PrimaryPointer,
        Updated,
        1.0f,
        20.0f,
        18.0f);

    if (Contact.Pressed)
    {
        ActiveDragControlId = ControlId;
    }
    if (Dragging && !Input.PrimaryPointer.Down)
    {
        ActiveDragControlId = 0u;
    }
    if ((Contact.Dragging || Contact.Pressed) && std::fabs(Updated - InOutValue) > 1.0e-5f)
    {
        InOutValue = Updated;
        OutChanged = true;
    }

    return RowY + 28.0f + 12.0f;
}

float WorldEnvironmentInspector::RenderPresetChipStrip(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    const char*                  SectionTitle,
    std::span<const char* const> Labels,
    uint32_t                     ActiveIndex,
    const PaneInputReading&      Input,
    int32_t&                     OutChosenIndex) noexcept
{
    OutChosenIndex = -1;
    const float    Pad   = 16.0f;
    const float    Gap   = 8.0f;
    const uint32_t Cols  = 2u;
    const uint32_t Rows  = (static_cast<uint32_t>(Labels.size()) + Cols - 1u) / Cols;
    const float    ChipH = 30.0f;
    const float    BoxH  = Pad + 24.0f + static_cast<float>(Rows) * ChipH + static_cast<float>(Rows > 0u ? Rows - 1u : 0u) * Gap + Pad;

    const Rectangle2D SectionRect{ LeftX, TopY, InnerWidth, BoxH };
    Surface.FillRectangle(SectionRect, SectionBg, 18.0f);
    Surface.StrokeRectangle(SectionRect, SectionBorder, 18.0f, 1.0f);

    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + Pad, TopY + Pad },
        12.5f,
        ColorRgba{ 214u, 214u, 214u, 255u },
        SectionTitle);

    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   ChipW = std::floor((InnerWidth - Pad * 2.0f - Gap) * 0.5f);
    const float   GridY = TopY + Pad + 26.0f;

    for (std::size_t Idx = 0u; Idx < Labels.size(); ++Idx)
    {
        const uint32_t Col = static_cast<uint32_t>(Idx) % Cols;
        const uint32_t Row = static_cast<uint32_t>(Idx) / Cols;
        const Rectangle2D Chip{
            LeftX + Pad + static_cast<float>(Col) * (ChipW + Gap),
            GridY + static_cast<float>(Row) * (ChipH + Gap),
            ChipW,
            ChipH,
        };
        const bool Active  = (static_cast<uint32_t>(Idx) == ActiveIndex);
        const bool Hovered = Chip.Contains(Cursor);

        if (Active)
        {
            Surface.FillRectangle(Chip, ColorRgba{ 242u, 242u, 242u, 255u }, 9.0f);
        }
        else
        {
            Surface.FillRectangle(Chip, Hovered ? TileFillHover : TileFillIdle, 9.0f);
            Surface.StrokeRectangle(Chip, TileBorder, 9.0f, 1.0f);
        }

        const float MaxW = Chip.Width - 14.0f;
        const float LW   = std::min(MaxW, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, Active ? FontWeightClass::Medium : FontWeightClass::Regular, 10.5f, Labels[Idx]));
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            Active ? FontWeightClass::Medium : FontWeightClass::Regular,
            { Chip.Center().X - LW * 0.5f, Chip.Y + 8.5f },
            MaxW,
            10.5f,
            Active ? ColorRgba{ 16u, 16u, 16u, 255u } : ColorRgba{ 196u, 196u, 196u, 255u },
            Labels[Idx]);

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            OutChosenIndex = static_cast<int32_t>(Idx);
        }
    }

    return SectionRect.Bottom() + 14.0f;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              CATEGORY-SPECIFIC INSPECTORS
//------------------------------------------------------------------------------------------------------------------------

float WorldEnvironmentInspector::RenderAtmospherePages(
    PixelSpace&                               Surface,
    float                                     LeftX,
    float                                     TopY,
    float                                     InnerWidth,
    OutlinerRecordEntry&                      Entry,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading&                   Input,
    bool&                                     OutMutated)
{
    static constexpr std::array<const char*, 5u> Pages = {{
        "Overview", "Scattering", "Density", "Sky colour", "View",
    }};
    uint32_t& ActivePage = ActivePageByCategory[static_cast<std::size_t>(WorldEnvironmentCategory::Atmosphere)];
    float CursorY = RenderPageCarousel(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input);

    auto& Medium = Scene.Atmosphere.Medium;
    const float Pad = 16.0f;

    if (ActivePage == 0u)
    {
        // Atmosphere overview hero section (matches 10-InspectorAtmosphere.png)
        const Rectangle2D HeroPanel{ LeftX, CursorY, InnerWidth, 252.0f };
        Surface.FillRectangle(HeroPanel, SectionBg, 18.0f);
        Surface.StrokeRectangle(HeroPanel, SectionBorder, 18.0f, 1.0f);

        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX + Pad, CursorY + Pad },
            12.5f,
            ColorRgba{ 214u, 214u, 214u, 255u },
            "Atmosphere overview");

        const Rectangle2D Chamber{ LeftX + Pad, CursorY + 46.0f, InnerWidth - Pad * 2.0f, 148.0f };
        Surface.FillRectangle(Chamber, ChamberBg, 14.0f);
        Surface.PushClip(Chamber);

        // Planetary limb arc + Rayleigh blue glow band
        const Point2D PlanetCenter{ Chamber.Center().X, Chamber.Bottom() + 265.0f };
        const float   PlanetR = 320.0f;
        const float   AtmoScale = std::clamp(Medium.AtmosphereThicknessMeters / 60000.0f, 0.35f, 2.2f);
        const float   GlowR   = PlanetR + 18.0f * AtmoScale;
        Surface.StrokeCircle(PlanetCenter, GlowR, ColorRgba{ 68u, 148u, 242u, 48u }, 16.0f);
        Surface.StrokeCircle(PlanetCenter, PlanetR + 8.0f * AtmoScale, ColorRgba{ 118u, 192u, 255u, 128u }, 4.0f);
        Surface.StrokeCircle(PlanetCenter, PlanetR, ColorRgba{ 170u, 188u, 214u, 140u }, 1.2f);

        // Zenith reference dot + live thickness readout ("60 km")
        const Point2D ZenithDot{ Chamber.Center().X, PlanetCenter.Y - PlanetR - 8.0f * AtmoScale };
        Surface.FillCircle(ZenithDot, 3.0f, ColorRgba{ 244u, 244u, 244u, 255u });
        char ThickBuf[24]{};
        std::snprintf(ThickBuf, sizeof(ThickBuf), "%.0f km", Medium.AtmosphereThicknessMeters * 0.001f);
        const float ThickW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, ThickBuf);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { ZenithDot.X - ThickW * 0.5f, ZenithDot.Y - 18.0f },
            10.5f,
            ColorRgba{ 184u, 184u, 184u, 255u },
            ThickBuf);
        Surface.PopClip();

        // Sky brightness slider footer inside overview panel
        const float SliderY = Chamber.Bottom() + 14.0f;
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX + Pad, SliderY + 6.0f },
            10.5f,
            SubtleInk,
            "Sky brightness");
        char BrightBuf[24]{};
        std::snprintf(BrightBuf, sizeof(BrightBuf), "%.2fx", Medium.SkyBrightnessMultiplier);
        const float BrightW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, BrightBuf);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { HeroPanel.Right() - Pad - BrightW, SliderY + 6.0f },
            10.5f,
            WorkspaceSurfaceColors::PrimaryInk,
            BrightBuf);

        const Rectangle2D TrackBounds{ LeftX + Pad + 90.0f, SliderY + 4.0f, std::max(72.0f, InnerWidth - Pad * 2.0f - 138.0f), 18.0f };
        float UpdatedBright = Medium.SkyBrightnessMultiplier;
        const bool Dragging = (ActiveDragControlId == 1001u);
        const ControlContact C = ScalarSlider::Construct(
            Surface, TrackBounds, 0.0f, 3.0f, Medium.SkyBrightnessMultiplier, Dragging, Input.PrimaryPointer, UpdatedBright, 1.0f, 5.0f, 12.0f);
        if (C.Pressed) ActiveDragControlId = 1001u;
        if (Dragging && !Input.PrimaryPointer.Down) ActiveDragControlId = 0u;
        if ((C.Dragging || C.Pressed) && std::fabs(UpdatedBright - Medium.SkyBrightnessMultiplier) > 1.0e-4f)
        {
            Medium.SkyBrightnessMultiplier = UpdatedBright;
            OutMutated = true;
        }

        CursorY = HeroPanel.Bottom() + 14.0f;

        // Quick controls grid
        const std::array<QuickTileSpec, 2u> Tiles = {{
            { "Visible",      EnvironmentSymbolCategory::None,       Entry.VisibilityEnabled },
            { "Orbital view", EnvironmentSymbolCategory::Atmosphere, Medium.OrbitalViewEnabled },
        }};
        int32_t Clicked = -1;
        CursorY = RenderQuickControlGrid(Surface, LeftX, CursorY, InnerWidth, Tiles, Input, Clicked);
        if (Clicked == 0)
        {
            Entry.VisibilityEnabled = !Entry.VisibilityEnabled;
            OutMutated = true;
        }
        else if (Clicked == 1)
        {
            Medium.OrbitalViewEnabled = !Medium.OrbitalViewEnabled;
            OutMutated = true;
        }
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 354.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Optical scattering");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1101u, "Rayleigh strength", "x", "%.2f", 0.0f, 4.0f, Medium.RayleighStrength, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1102u, "Mie haze strength", "x", "%.2f", 0.0f, 6.0f, Medium.MieStrength, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1103u, "Mie anisotropy g",  "",  "%.2f", 0.0f, 0.99f, Medium.MieAnisotropy, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1104u, "Ozone absorption",  "x", "%.2f", 0.0f, 4.0f, Medium.OzoneStrength, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1105u, "Limb glow",         "x", "%.2f", 0.0f, 3.0f, Medium.LimbGlowIntensity, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 238.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Scale height & density");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1201u, "Rayleigh scale height", "m", "%.0f", 1000.0f, 20000.0f, Medium.RayleighScaleHeightMeters, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1202u, "Mie scale height",      "m", "%.0f", 200.0f,  6000.0f,  Medium.MieScaleHeightMeters, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1203u, "Atmosphere thickness",  "m", "%.0f", 10000.0f, 150000.0f, Medium.AtmosphereThicknessMeters, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }
    else if (ActivePage == 3u)
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 238.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Sky tint");
        Surface.FillRectangle({ Section.Right() - Pad - 42.0f, Y - 2.0f, 42.0f, 18.0f }, LinearRgbToRgba8(Medium.SkyTint), 6.0f);
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1301u, "Sky tint R", "", "%.2f", 0.0f, 2.0f, Medium.SkyTint.R, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1302u, "Sky tint G", "", "%.2f", 0.0f, 2.0f, Medium.SkyTint.G, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1303u, "Sky tint B", "", "%.2f", 0.0f, 2.0f, Medium.SkyTint.B, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }
    else
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 180.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Observer & planet");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1401u, "Observer altitude", "m", "%.0f", 0.0f, 250000.0f, Medium.ObserverAltitudeMeters, Input, OutMutated);
        float RadiusKm = Medium.PlanetRadiusMeters * 0.001f;
        bool  RadChanged = false;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 1402u, "Planet radius", "km", "%.0f", 1000.0f, 12000.0f, RadiusKm, Input, RadChanged);
        if (RadChanged)
        {
            Medium.PlanetRadiusMeters = RadiusKm * 1000.0f;
            OutMutated = true;
        }
        CursorY = Section.Bottom() + 14.0f;
    }

    return CursorY;
}

float WorldEnvironmentInspector::RenderSunPages(
    PixelSpace&                               Surface,
    float                                     LeftX,
    float                                     TopY,
    float                                     InnerWidth,
    OutlinerRecordEntry&                      Entry,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading&                   Input,
    bool&                                     OutMutated)
{
    static constexpr std::array<const char*, 4u> Pages = {{
        "Overview", "Temperature", "Intensity", "Direction",
    }};
    uint32_t& ActivePage = ActivePageByCategory[static_cast<std::size_t>(WorldEnvironmentCategory::Sun)];
    float CursorY = RenderPageCarousel(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input);

    auto& Solar = Scene.Atmosphere.Solar;
    const auto Solved = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Solar);
    const float Pad = 16.0f;

    if (ActivePage == 0u)
    {
        // "Sun now" hero panel (matches 11-InspectorSun.png)
        const Rectangle2D HeroPanel{ LeftX, CursorY, InnerWidth, 246.0f };
        Surface.FillRectangle(HeroPanel, SectionBg, 18.0f);
        Surface.StrokeRectangle(HeroPanel, SectionBorder, 18.0f, 1.0f);

        DrawQuickGlyph(Surface, EnvironmentSymbolCategory::Sun, { LeftX + Pad + 7.0f, CursorY + Pad + 7.0f }, 13.0f, false);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX + Pad + 22.0f, CursorY + Pad },
            12.5f,
            ColorRgba{ 214u, 214u, 214u, 255u },
            "Sun now");

        const Rectangle2D Chamber{ LeftX + Pad, CursorY + 46.0f, InnerWidth - Pad * 2.0f, 132.0f };
        Surface.FillRectangle(Chamber, ChamberBg, 14.0f);

        // Large HH:MM clock readout
        const float WrappedH = std::fmod(std::max(0.0f, Solar.LocalHours), 24.0f);
        const int32_t Hours  = static_cast<int32_t>(std::floor(WrappedH));
        const int32_t Mins   = static_cast<int32_t>(std::floor((WrappedH - static_cast<float>(Hours)) * 60.0f));
        char ClockBuf[16]{};
        std::snprintf(ClockBuf, sizeof(ClockBuf), "%02d:%02d", Hours, Mins);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Light,
            { Chamber.X + 16.0f, Chamber.Y + 16.0f },
            26.0f,
            WorkspaceSurfaceColors::PrimaryInk,
            ClockBuf);

        char EphemBuf[64]{};
        std::snprintf(
            EphemBuf,
            sizeof(EphemBuf),
            "%.0f deg %s, facing %s",
            std::fabs(Solved.ElevationDegrees),
            Solved.ElevationDegrees >= 0.0f ? "up" : "below",
            ResolveCompassLabel(Solved.AzimuthDegrees));
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { Chamber.X + 16.0f, Chamber.Y + 50.0f },
            10.5f,
            SubtleInk,
            EphemBuf);

        // Subtle solar elevation arc visual along lower half of chamber
        const float ArcY = Chamber.Bottom() - 22.0f;
        Surface.StrokeLine({ Chamber.X + 16.0f, ArcY }, { Chamber.Right() - 16.0f, ArcY }, ColorRgba{ 44u, 46u, 52u, 255u }, 1.0f);
        const float NormDay = std::clamp(WrappedH / 24.0f, 0.0f, 1.0f);
        const float SunDotX = Chamber.X + 16.0f + NormDay * (Chamber.Width - 32.0f);
        const float SunDotY = ArcY - std::clamp(Solved.ElevationDegrees / 90.0f, -0.25f, 1.0f) * 34.0f;
        Surface.FillCircle({ SunDotX, SunDotY }, 8.0f, ColorRgba{ 255u, 178u, 72u, 55u });
        Surface.FillCircle({ SunDotX, SunDotY }, 4.0f, ColorRgba{ 255u, 212u, 118u, 255u });

        // Interactive daylight time-of-day gradient bar (00:00..24:00)
        const Rectangle2D TimeBar{ LeftX + Pad, Chamber.Bottom() + 14.0f, InnerWidth - Pad * 2.0f, 20.0f };
        const float BarY = TimeBar.Center().Y - 3.0f;
        const float SegW = TimeBar.Width / 4.0f;
        Surface.FillRectangle({ TimeBar.X,             BarY, SegW, 6.0f }, ColorRgba{ 32u,  38u,  58u,  255u }, 3.0f);
        Surface.FillRectangle({ TimeBar.X + SegW,      BarY, SegW, 6.0f }, ColorRgba{ 238u, 186u, 114u, 255u }, 0.0f);
        Surface.FillRectangle({ TimeBar.X + SegW * 2.f, BarY, SegW, 6.0f }, ColorRgba{ 244u, 224u, 174u, 255u }, 0.0f);
        Surface.FillRectangle({ TimeBar.X + SegW * 3.f, BarY, SegW, 6.0f }, ColorRgba{ 46u,  44u,  68u,  255u }, 3.0f);

        const float KnobX = TimeBar.X + NormDay * TimeBar.Width;
        Surface.FillCircle({ KnobX, TimeBar.Center().Y }, 7.0f, ColorRgba{ 246u, 246u, 246u, 255u });
        Surface.StrokeCircle({ KnobX, TimeBar.Center().Y }, 7.0f, ColorRgba{ 22u, 22u, 22u, 255u }, 1.5f);

        const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
        const bool DragTime = (ActiveDragControlId == 2001u);
        if (TimeBar.Contains(Cursor) && Input.PrimaryPointer.Pressed)
        {
            ActiveDragControlId = 2001u;
        }
        if ((ActiveDragControlId == 2001u) && Input.PrimaryPointer.Down)
        {
            const float NewHours = std::clamp((Cursor.X - TimeBar.X) / std::max(1.0f, TimeBar.Width), 0.0f, 1.0f) * 24.0f;
            if (std::fabs(NewHours - Solar.LocalHours) > 1.0e-3f)
            {
                Solar.LocalHours = NewHours;
                OutMutated       = true;
            }
        }
        else if (DragTime && !Input.PrimaryPointer.Down)
        {
            ActiveDragControlId = 0u;
        }

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TimeBar.X, TimeBar.Bottom() + 4.0f }, 9.5f, DimInk, "00:00");
        const float NoonW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, "12:00");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TimeBar.Center().X - NoonW * 0.5f, TimeBar.Bottom() + 4.0f }, 9.5f, DimInk, "12:00");
        const float EndW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, "24:00");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TimeBar.Right() - EndW, TimeBar.Bottom() + 4.0f }, 9.5f, DimInk, "24:00");

        CursorY = HeroPanel.Bottom() + 14.0f;

        const bool RadiantOn = (Solar.AppearanceMode == GeometricRaster::SolarAppearanceMode::Radiant);
        const std::array<QuickTileSpec, 4u> Tiles = {{
            { "Enabled",          EnvironmentSymbolCategory::Sun,  Entry.VisibilityEnabled },
            { "Day cycle",        EnvironmentSymbolCategory::Sun,  Solar.CycleAnimationEnabled },
            { "Manual direction", EnvironmentSymbolCategory::None, Solar.ManualDirectionOverride },
            { "Radiant glare",    EnvironmentSymbolCategory::Sun,  RadiantOn },
        }};
        int32_t Clicked = -1;
        CursorY = RenderQuickControlGrid(Surface, LeftX, CursorY, InnerWidth, Tiles, Input, Clicked);
        if (Clicked == 0)
        {
            Entry.VisibilityEnabled = !Entry.VisibilityEnabled;
            OutMutated = true;
        }
        else if (Clicked == 1)
        {
            Solar.CycleAnimationEnabled = !Solar.CycleAnimationEnabled;
            OutMutated = true;
        }
        else if (Clicked == 2)
        {
            Solar.ManualDirectionOverride = !Solar.ManualDirectionOverride;
            OutMutated = true;
        }
        else if (Clicked == 3)
        {
            Solar.AppearanceMode = RadiantOn
                                       ? GeometricRaster::SolarAppearanceMode::Muted
                                       : GeometricRaster::SolarAppearanceMode::Radiant;
            OutMutated = true;
        }
    }
    else if (ActivePage == 1u)
    {
        static constexpr std::array<const char*, 2u> TintModes = {{ "RGB Tint", "Kelvin K" }};
        int32_t ChosenMode = -1;
        CursorY = RenderPresetChipStrip(
            Surface, LeftX, CursorY, InnerWidth, "Tint source", TintModes,
            Solar.TintSource == GeometricRaster::SolarTintSource::KelvinTemperature ? 1u : 0u, Input, ChosenMode);
        if (ChosenMode >= 0)
        {
            Solar.TintSource = (ChosenMode == 1) ? GeometricRaster::SolarTintSource::KelvinTemperature : GeometricRaster::SolarTintSource::RgbTint;
            OutMutated = true;
        }

        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 296.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Solar spectrum");
        Surface.FillRectangle({ Section.Right() - Pad - 42.0f, Y - 2.0f, 42.0f, 18.0f }, LinearRgbToRgba8(Solved.EffectiveSolarTint), 6.0f);
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2101u, "Color temperature", "K", "%.0f", 1500.0f, 18000.0f, Solar.ColorTemperatureKelvin, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2102u, "Manual tint R",     "",  "%.2f", 0.0f, 1.5f, Solar.ManualRgbTint.R, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2103u, "Manual tint G",     "",  "%.2f", 0.0f, 1.5f, Solar.ManualRgbTint.G, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2104u, "Manual tint B",     "",  "%.2f", 0.0f, 1.5f, Solar.ManualRgbTint.B, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 296.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Intensity & optics");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2201u, "Solar intensity",  "x",   "%.1f", 0.0f, 60.0f, Solar.SolarIntensityMultiplier, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2202u, "Direct gain",      "x",   "%.2f", 0.0f, 5.0f,  Solar.DirectGainMultiplier, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2203u, "Angular diameter", "deg", "%.2f", 0.1f, 5.0f,  Solar.AngularDiameterDegrees, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2204u, "Glare strength",   "x",   "%.2f", 0.0f, 2.0f,  Solar.SunGlareStrength, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }
    else
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 296.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Solar position & site");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2301u, "Manual azimuth",   "deg", "%.1f", 0.0f,    360.0f, Solar.ManualAzimuthDegrees, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2302u, "Manual elevation", "deg", "%.1f", -90.0f,  90.0f,  Solar.ManualElevationDegrees, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2303u, "Site latitude",    "deg", "%.1f", -90.0f,  90.0f,  Solar.SiteLatitudeDegrees, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 2304u, "Site longitude",   "deg", "%.1f", -180.0f, 180.0f, Solar.SiteLongitudeDegrees, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }

    return CursorY;
}

float WorldEnvironmentInspector::RenderCloudsPages(
    PixelSpace&                               Surface,
    float                                     LeftX,
    float                                     TopY,
    float                                     InnerWidth,
    OutlinerRecordEntry&                      Entry,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading&                   Input,
    bool&                                     OutMutated)
{
    static constexpr std::array<const char*, 5u> Pages = {{
        "Overview", "Coverage", "Layer", "Wind", "Quality",
    }};
    uint32_t& ActivePage = ActivePageByCategory[static_cast<std::size_t>(WorldEnvironmentCategory::Clouds)];
    float CursorY = RenderPageCarousel(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input);

    auto& Clouds = Scene.Atmosphere.Clouds;
    const float Pad = 16.0f;

    if (ActivePage == 0u)
    {
        // 2x2 Metric KPI tiles (exact match to 12-InspectorClouds.png)
        const float Gap   = 10.0f;
        const float KpiW  = std::floor((InnerWidth - Gap) * 0.5f);
        const float KpiH  = 72.0f;
        auto DrawKpi = [&](uint32_t Col, uint32_t Row, const char* ValStr, const char* UnitStr, const char* Caption)
        {
            const Rectangle2D Box{
                LeftX + static_cast<float>(Col) * (KpiW + Gap),
                CursorY + static_cast<float>(Row) * (KpiH + Gap),
                KpiW,
                KpiH,
            };
            Surface.FillRectangle(Box, SectionBg, 14.0f);
            Surface.StrokeRectangle(Box, SectionBorder, 14.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { Box.X + 14.0f, Box.Y + 14.0f }, 20.0f, WorkspaceSurfaceColors::PrimaryInk, ValStr);
            const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 20.0f, ValStr);
            if (UnitStr[0] != '\0')
            {
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 18.0f + VW, Box.Y + 21.0f }, 10.5f, SubtleInk, UnitStr);
            }
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 14.0f, Box.Bottom() - 21.0f }, 9.0f, DimInk, Caption);
        };

        char CovBuf[16]{};
        char ExtBuf[16]{};
        char BaseBuf[16]{};
        char DepthBuf[16]{};
        std::snprintf(CovBuf,   sizeof(CovBuf),   "%.0f", Clouds.CloudFraction * 100.0f);
        std::snprintf(ExtBuf,   sizeof(ExtBuf),   "%.1f", Clouds.ExtinctionMultiplier);
        std::snprintf(BaseBuf,  sizeof(BaseBuf),  "%.0f", Clouds.CloudFloorMeters);
        std::snprintf(DepthBuf, sizeof(DepthBuf), "%.0f", Clouds.LayerThicknessMeters);

        DrawKpi(0u, 0u, CovBuf,   "%", "COVERAGE");
        DrawKpi(1u, 0u, ExtBuf,   "x", "EXTINCTION");
        DrawKpi(0u, 1u, BaseBuf,  "m", "BASE");
        DrawKpi(1u, 1u, DepthBuf, "m", "DEPTH");
        CursorY += KpiH * 2.0f + Gap + 14.0f;

        // Quick controls
        const std::array<QuickTileSpec, 3u> Tiles = {{
            { "Enabled",       EnvironmentSymbolCategory::Clouds, Entry.VisibilityEnabled },
            { "Cloud shadows", EnvironmentSymbolCategory::Clouds, Clouds.CloudShadow.ShadowEnabled },
            { "Follow wind",   EnvironmentSymbolCategory::None,   Clouds.FollowWindEnabled },
        }};
        int32_t Clicked = -1;
        CursorY = RenderQuickControlGrid(Surface, LeftX, CursorY, InnerWidth, Tiles, Input, Clicked);
        if (Clicked == 0)
        {
            Entry.VisibilityEnabled = !Entry.VisibilityEnabled;
            OutMutated = true;
        }
        else if (Clicked == 1)
        {
            Clouds.CloudShadow.ShadowEnabled = !Clouds.CloudShadow.ShadowEnabled;
            OutMutated = true;
        }
        else if (Clicked == 2)
        {
            Clouds.FollowWindEnabled = !Clouds.FollowWindEnabled;
            OutMutated = true;
        }

        // Cloud formation preset strip
        static constexpr std::array<const char*, 6u> CloudNames = {{
            "Cumulus", "Stratus", "Stratocumulus", "Cumulonimbus", "Cirrus", "Altostratus",
        }};
        static constexpr std::array<GeometricRaster::CloudCategoryType, 6u> CloudTypes = {{
            GeometricRaster::CloudCategoryType::Cumulus,
            GeometricRaster::CloudCategoryType::Stratus,
            GeometricRaster::CloudCategoryType::Stratocumulus,
            GeometricRaster::CloudCategoryType::Cumulonimbus,
            GeometricRaster::CloudCategoryType::Cirrus,
            GeometricRaster::CloudCategoryType::Altostratus,
        }};
        uint32_t ActivePreset = 0u;
        for (uint32_t Idx = 0u; Idx < CloudTypes.size(); ++Idx)
        {
            if (Clouds.Category == CloudTypes[Idx]) { ActivePreset = Idx; break; }
        }
        int32_t ChosenPreset = -1;
        CursorY = RenderPresetChipStrip(Surface, LeftX, CursorY, InnerWidth, "Formation preset", CloudNames, ActivePreset, Input, ChosenPreset);
        if (ChosenPreset >= 0 && ChosenPreset < static_cast<int32_t>(CloudTypes.size()))
        {
            GeometricRaster::VolumetricCloudSequence::ApplyCloudCategoryPreset(Clouds, CloudTypes[static_cast<std::size_t>(ChosenPreset)]);
            OutMutated = true;
        }
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 296.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Coverage & density");
        Y += 28.0f;
        float CovPct = Clouds.CloudFraction * 100.0f;
        bool CovChanged = false;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3101u, "Cloud coverage",   "%", "%.0f", 0.0f, 100.0f, CovPct, Input, CovChanged);
        if (CovChanged) { Clouds.CloudFraction = CovPct * 0.01f; OutMutated = true; }
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3102u, "Extinction",       "x", "%.2f", 0.0f, 4.0f, Clouds.ExtinctionMultiplier, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3103u, "Feature scale",    "x", "%.2f", 0.2f, 3.0f, Clouds.FeatureScaleMultiplier, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3104u, "Forward scatter",  "g", "%.2f", -0.9f, 0.9f, Clouds.ForwardScatterAnisotropy, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }
    else if (ActivePage == 2u)
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 296.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Altitude layer");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3201u, "Cloud floor",     "m", "%.0f", 100.0f,  15000.0f, Clouds.CloudFloorMeters, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3202u, "Layer thickness", "m", "%.0f", 100.0f,  6500.0f,  Clouds.LayerThicknessMeters, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3203u, "Deck ceiling",    "m", "%.0f", 4000.0f, 22000.0f, Clouds.DeckCeilingMeters, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3204u, "Anvil spread",    "",  "%.2f", 0.0f,    1.0f,     Clouds.AnvilTopSpread, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }
    else if (ActivePage == 3u)
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 296.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Wind & advection");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3301u, "Wind speed",      "m/s", "%.1f", 0.0f, 40.0f,  Clouds.WindSpeedMetersPerSecond, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3302u, "Wind bearing",    "deg", "%.0f", 0.0f, 360.0f, Clouds.WindBearingDegrees, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3303u, "Vertical shear",  "/km", "%.2f", 0.0f, 2.0f,   Clouds.WindShearPerKilometer, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3304u, "Wind turbulence", "",    "%.2f", 0.0f, 1.0f,   Clouds.WindTurbulence, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }
    else
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 238.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "March & shadow quality");
        Y += 28.0f;
        float StepsF = static_cast<float>(Clouds.CloudMarchSteps);
        bool StepsChanged = false;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3401u, "March steps",    "",  "%.0f", 16.0f, 128.0f, StepsF, Input, StepsChanged);
        if (StepsChanged) { Clouds.CloudMarchSteps = static_cast<uint32_t>(std::lround(StepsF)); OutMutated = true; }
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3402u, "Shadow density", "x", "%.2f", 0.0f,  3.0f, Clouds.CloudShadow.ShadowDensityMultiplier, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 3403u, "Shadow fraction", "", "%.2f", 0.0f, 1.0f, Clouds.CloudShadow.ShadowFraction, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }

    return CursorY;
}

float WorldEnvironmentInspector::RenderMoonPages(
    PixelSpace&                               Surface,
    float                                     LeftX,
    float                                     TopY,
    float                                     InnerWidth,
    OutlinerRecordEntry&                      Entry,
    uint32_t                                  MoonSlotIndex,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading&                   Input,
    bool&                                     OutMutated)
{
    static constexpr std::array<const char*, 3u> Pages = {{
        "Overview", "Appearance", "Position",
    }};
    uint32_t& ActivePage = ActivePageByCategory[static_cast<std::size_t>(WorldEnvironmentCategory::Moon)];
    float CursorY = RenderPageCarousel(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input);

    const uint32_t SafeSlot = std::min<uint32_t>(MoonSlotIndex, WorldEnvironmentRegistry::MaximumMoonCount - 1u);
    auto& Moon = Scene.Atmosphere.Moons[SafeSlot];
    const float Pad = 16.0f;

    if (ActivePage == 0u)
    {
        // Moon slot identity panel (matches 13-InspectorMoon.png)
        const Rectangle2D SlotPanel{ LeftX, CursorY, InnerWidth, 92.0f };
        Surface.FillRectangle(SlotPanel, SectionBg, 18.0f);
        Surface.StrokeRectangle(SlotPanel, SectionBorder, 18.0f, 1.0f);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, CursorY + Pad + 4.0f }, 12.0f, ColorRgba{ 214u, 214u, 214u, 255u }, "Moon slot");
        char SlotTag[24]{};
        std::snprintf(SlotTag, sizeof(SlotTag), "Moon %u", SafeSlot + 1u);
        const Rectangle2D SlotPill{ SlotPanel.Right() - Pad - 94.0f, CursorY + Pad - 1.0f, 94.0f, 26.0f };
        Surface.FillRectangle(SlotPill, ColorRgba{ 18u, 18u, 18u, 255u }, 13.0f);
        Surface.StrokeRectangle(SlotPill, ColorRgba{ 54u, 54u, 54u, 255u }, 13.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SlotPill.X + 14.0f, SlotPill.Y + 6.5f }, 11.0f, WorkspaceSurfaceColors::PrimaryInk, SlotTag);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, CursorY + 54.0f }, 10.5f, SubtleInk, "Six moons can be active; controls edit this slot.");
        CursorY = SlotPanel.Bottom() + 14.0f;

        // Lunar phase hero panel (matches 13-InspectorMoon.png)
        const Rectangle2D PhasePanel{ LeftX, CursorY, InnerWidth, 326.0f };
        Surface.FillRectangle(PhasePanel, SectionBg, 18.0f);
        Surface.StrokeRectangle(PhasePanel, SectionBorder, 18.0f, 1.0f);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, CursorY + Pad }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Lunar phase");

        const float SynodicDays = Moon.SynodicPhaseFraction * 29.53f;
        const float IllumPct    = (1.0f - std::cos(Moon.SynodicPhaseFraction * 6.2831853f)) * 50.0f;
        char DaysBuf[24]{};
        std::snprintf(DaysBuf, sizeof(DaysBuf), "%.1f", SynodicDays);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { LeftX + Pad, CursorY + 44.0f }, 26.0f, WorkspaceSurfaceColors::PrimaryInk, DaysBuf);
        const float DaysW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 26.0f, DaysBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad + DaysW + 6.0f, CursorY + 57.0f }, 11.0f, SubtleInk, "days");

        char IllumBuf[48]{};
        std::snprintf(IllumBuf, sizeof(IllumBuf), "%.0f%% illuminated  ·  29.53-day synodic cycle", IllumPct);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, CursorY + 80.0f }, 10.5f, SubtleInk, IllumBuf);

        // Live phase-lit lunar disc preview
        const Point2D MoonCenter{ PhasePanel.Center().X, CursorY + 178.0f };
        const float   MoonR = 62.0f;
        Surface.FillCircle(MoonCenter, MoonR, ColorRgba{ 182u, 186u, 194u, 255u });
        // Regolith mare craters
        Surface.FillCircle({ MoonCenter.X - 18.0f, MoonCenter.Y - 14.0f }, 16.0f, ColorRgba{ 138u, 142u, 152u, 175u });
        Surface.FillCircle({ MoonCenter.X + 14.0f, MoonCenter.Y - 22.0f }, 12.0f, ColorRgba{ 126u, 130u, 140u, 165u });
        Surface.FillCircle({ MoonCenter.X + 10.0f, MoonCenter.Y + 18.0f }, 15.0f, ColorRgba{ 132u, 136u, 146u, 170u });
        Surface.FillCircle({ MoonCenter.X - 24.0f, MoonCenter.Y + 14.0f }, 10.0f, ColorRgba{ 146u, 150u, 160u, 150u });
        // Terminator shadow driven by SynodicPhaseFraction (0=new, 0.5=full, 1=new)
        const float DarkFrac = 1.0f - (IllumPct * 0.01f);
        if (DarkFrac > 0.02f)
        {
            const float ShiftDir = (Moon.SynodicPhaseFraction < 0.5f) ? -1.0f : 1.0f;
            const Point2D ShadowCenter{
                MoonCenter.X + ShiftDir * (1.0f - DarkFrac) * MoonR * 1.45f,
                MoonCenter.Y,
            };
            Surface.PushClip({ MoonCenter.X - MoonR, MoonCenter.Y - MoonR, MoonR * 2.0f, MoonR * 2.0f });
            Surface.FillCircle(ShadowCenter, MoonR * 1.02f, ColorRgba{ 16u, 18u, 22u, static_cast<uint8_t>(std::clamp(DarkFrac * 245.0f, 0.0f, 235.0f)) });
            Surface.PopClip();
        }
        Surface.StrokeCircle(MoonCenter, MoonR, ColorRgba{ 212u, 216u, 224u, 96u }, 1.2f);

        (void)RenderLabeledScalarSlider(
            Surface, LeftX + Pad, CursorY + 258.0f, InnerWidth - Pad * 2.0f, 4001u,
            "Phase", "", "%.2f", 0.0f, 1.0f, Moon.SynodicPhaseFraction, Input, OutMutated);

        CursorY = PhasePanel.Bottom() + 14.0f;

        const std::array<QuickTileSpec, 3u> Tiles = {{
            { "Visible",      EnvironmentSymbolCategory::Moon,       Entry.VisibilityEnabled },
            { "Opposite Sun", EnvironmentSymbolCategory::Sun,        Moon.FollowSkyOppositeSun },
            { "Exosphere",    EnvironmentSymbolCategory::Atmosphere, Moon.HasAtmosphere },
        }};
        int32_t Clicked = -1;
        CursorY = RenderQuickControlGrid(Surface, LeftX, CursorY, InnerWidth, Tiles, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Moon.FollowSkyOppositeSun = !Moon.FollowSkyOppositeSun; OutMutated = true; }
        else if (Clicked == 2) { Moon.HasAtmosphere = !Moon.HasAtmosphere; OutMutated = true; }
    }
    else if (ActivePage == 1u)
    {
        static constexpr std::array<const char*, 6u> MoonNames = {{
            "Luna", "Ember", "Glacier", "Sulfur", "Shroud", "Shard",
        }};
        int32_t ChosenPreset = -1;
        CursorY = RenderPresetChipStrip(
            Surface, LeftX, CursorY, InnerWidth, "Surface preset", MoonNames,
            static_cast<uint32_t>(Moon.SurfacePreset), Input, ChosenPreset);
        if (ChosenPreset >= 0 && ChosenPreset < 6)
        {
            Moon.SurfacePreset = static_cast<GeometricRaster::LunarSurfacePreset>(ChosenPreset);
            OutMutated = true;
        }

        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 296.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Optics & exosphere");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 4101u, "Brightness",          "x",   "%.2f", 0.0f, 6.0f,  Moon.BrightnessMultiplier, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 4102u, "Halo glow",           "x",   "%.2f", 0.0f, 3.0f,  Moon.HaloGlowMultiplier, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 4103u, "Angular diameter",    "deg", "%.2f", 0.1f, 15.0f, Moon.AngularDiameterDegrees, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 4104u, "Exosphere intensity", "x",   "%.2f", 0.0f, 3.0f,  Moon.AtmosphereIntensity, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }
    else
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 296.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Sky orbit & orientation");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 4201u, "Azimuth",       "deg", "%.1f", 0.0f,    360.0f, Moon.AzimuthDegrees, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 4202u, "Elevation",     "deg", "%.1f", -90.0f,  90.0f,  Moon.ElevationDegrees, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 4203u, "Surface roll",  "deg", "%.1f", 0.0f,    360.0f, Moon.SurfaceRollDegrees, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 4204u, "Surface pitch", "deg", "%.1f", -180.0f, 180.0f, Moon.SurfacePitchDegrees, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }

    return CursorY;
}

float WorldEnvironmentInspector::RenderStarsPages(
    PixelSpace&                               Surface,
    float                                     LeftX,
    float                                     TopY,
    float                                     InnerWidth,
    OutlinerRecordEntry&                      Entry,
    GeometricRaster::WorldSceneSpecification& Scene,
    const PaneInputReading&                   Input,
    bool&                                     OutMutated)
{
    static constexpr std::array<const char*, 3u> Pages = {{
        "Overview", "Motion", "Renderer",
    }};
    uint32_t& ActivePage = ActivePageByCategory[static_cast<std::size_t>(WorldEnvironmentCategory::Stars)];
    float CursorY = RenderPageCarousel(Surface, LeftX, TopY, InnerWidth, Pages, ActivePage, Input);

    auto& Stars = Scene.Atmosphere.Stars;
    const float Pad = 16.0f;

    if (ActivePage == 0u)
    {
        // Star field hero panel (exact match to 14-InspectorStars.png)
        const Rectangle2D HeroPanel{ LeftX, CursorY, InnerWidth, 294.0f };
        Surface.FillRectangle(HeroPanel, SectionBg, 18.0f);
        Surface.StrokeRectangle(HeroPanel, SectionBorder, 18.0f, 1.0f);

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, CursorY + Pad }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Star field");

        char MagBuf[24]{};
        std::snprintf(MagBuf, sizeof(MagBuf), "%.1f", Stars.LimitingMagnitude);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light, { LeftX + Pad, CursorY + 46.0f }, 26.0f, WorkspaceSurfaceColors::PrimaryInk, MagBuf);
        const float MagW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 26.0f, MagBuf);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad + MagW + 6.0f, CursorY + 59.0f }, 11.0f, SubtleInk, "mag");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, CursorY + 84.0f }, 10.5f, SubtleInk, "Higher values reveal fainter stars");

        // Live procedural starfield preview box scaled by LimitingMagnitude
        const Rectangle2D Chamber{ LeftX + Pad, CursorY + 108.0f, InnerWidth - Pad * 2.0f, 100.0f };
        Surface.FillRectangle(Chamber, ColorRgba{ 16u, 17u, 22u, 255u }, 12.0f);
        Surface.PushClip(Chamber);
        const uint32_t StarCount = static_cast<uint32_t>(std::clamp(Stars.LimitingMagnitude * 22.0f, 8.0f, 176.0f));
        for (uint32_t I = 0u; I < StarCount; ++I)
        {
            const uint32_t Hx = HashU32(I * 1973u + 17u);
            const uint32_t Hy = HashU32(I * 9277u + 43u);
            const uint32_t Hb = HashU32(I * 26699u + 7u);
            const float SX = Chamber.X + 6.0f + static_cast<float>(Hx % 1000u) * 0.001f * (Chamber.Width - 12.0f);
            const float SY = Chamber.Y + 6.0f + static_cast<float>(Hy % 1000u) * 0.001f * (Chamber.Height - 12.0f);
            const float Bright = 0.25f + 0.75f * (static_cast<float>(Hb % 100u) * 0.01f);
            const float Rad    = (Hb % 11u == 0u) ? 1.55f : 0.95f;
            const uint8_t Alpha = static_cast<uint8_t>(std::clamp(Bright * 245.0f, 40.0f, 255.0f));
            const ColorRgba StarTint = (Hb % 5u == 0u)
                                           ? ColorRgba{ 196u, 214u, 255u, Alpha }
                                           : ((Hb % 7u == 0u) ? ColorRgba{ 255u, 232u, 202u, Alpha }
                                                              : ColorRgba{ 238u, 240u, 248u, Alpha });
            Surface.FillCircle({ SX, SY }, Rad, StarTint);
        }
        Surface.PopClip();

        (void)RenderLabeledScalarSlider(
            Surface, LeftX + Pad, CursorY + 222.0f, InnerWidth - Pad * 2.0f, 5001u,
            "Limiting magnitude", "mag", "%.1f", 0.0f, 8.0f, Stars.LimitingMagnitude, Input, OutMutated);

        CursorY = HeroPanel.Bottom() + 14.0f;

        const std::array<QuickTileSpec, 5u> Tiles = {{
            { "Star field",    EnvironmentSymbolCategory::Stars, Entry.VisibilityEnabled },
            { "Twinkle",       EnvironmentSymbolCategory::Stars, Stars.ScintillationEnabled },
            { "Bright stars",  EnvironmentSymbolCategory::Stars, Stars.EnableBrightStarCatalog },
            { "Milky Way",     EnvironmentSymbolCategory::Stars, Stars.EnableGalacticBand },
            { "Zodiacal glow", EnvironmentSymbolCategory::Sun,   Stars.ZodiacalLightEnabled },
        }};
        int32_t Clicked = -1;
        CursorY = RenderQuickControlGrid(Surface, LeftX, CursorY, InnerWidth, Tiles, Input, Clicked);
        if (Clicked == 0)      { Entry.VisibilityEnabled = !Entry.VisibilityEnabled; OutMutated = true; }
        else if (Clicked == 1) { Stars.ScintillationEnabled = !Stars.ScintillationEnabled; OutMutated = true; }
        else if (Clicked == 2) { Stars.EnableBrightStarCatalog = !Stars.EnableBrightStarCatalog; OutMutated = true; }
        else if (Clicked == 3) { Stars.EnableGalacticBand = !Stars.EnableGalacticBand; OutMutated = true; }
        else if (Clicked == 4) { Stars.ZodiacalLightEnabled = !Stars.ZodiacalLightEnabled; OutMutated = true; }
    }
    else if (ActivePage == 1u)
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 238.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Scintillation & celestial pole");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 5101u, "Scintillation depth", "%",   "%.0f", 0.0f, 100.0f, Stars.ScintillationDepthPercent, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 5102u, "Scintillation rate",  "Hz",  "%.2f", 0.2f, 3.0f,   Stars.ScintillationRateHertz, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 5103u, "Celestial rotation",  "deg", "%.1f", 0.0f, 360.0f, Stars.CelestialRotationDegrees, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }
    else
    {
        const Rectangle2D Section{ LeftX, CursorY, InnerWidth, 238.0f };
        Surface.FillRectangle(Section, SectionBg, 18.0f);
        Surface.StrokeRectangle(Section, SectionBorder, 18.0f, 1.0f);
        float Y = CursorY + Pad;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX + Pad, Y }, 12.5f, ColorRgba{ 214u, 214u, 214u, 255u }, "Starfield optics");
        Y += 28.0f;
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 5201u, "Brightness",     "x", "%.2f", 0.0f, 4.0f, Stars.BrightnessMultiplier, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 5202u, "Point size",     "x", "%.2f", 0.4f, 3.0f, Stars.PointSizeScale, Input, OutMutated);
        Y = RenderLabeledScalarSlider(Surface, LeftX + Pad, Y, InnerWidth - Pad * 2.0f, 5203u, "Zodiacal scale", "x", "%.2f", 0.0f, 4.0f, Stars.ZodiacalRadianceScale, Input, OutMutated);
        CursorY = Section.Bottom() + 14.0f;
    }

    return CursorY;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOP-LEVEL ENTRY
//------------------------------------------------------------------------------------------------------------------------

bool WorldEnvironmentInspector::RenderIfEnvironment(
    PixelSpace&                               Surface,
    Rectangle2D                               Bounds,
    OutlinerPanel*                            Outliner,
    OutlinerRecordEntry*                      PrimaryEntry,
    WorldEnvironmentRegistry&                 Registry,
    GeometricRaster::WorldSceneSpecification& Scene,
    ViewportDisplaySettings*                  DisplaySettings,
    const PaneInputReading&                   Input,
    double                                    /*Δt*/,
    bool&                                     OutMutated)
{
    OutMutated = false;
    if (PrimaryEntry == nullptr || Outliner == nullptr)
    {
        return false;
    }

    WorldEnvironmentCategory Category{};
    if (!WorldEnvironmentRegistry::ResolveCategoryFromRegistryKey(PrimaryEntry->DomainTag, Category))
    {
        return false;
    }

    // Resolve Moon slot index when inspecting a Moon entry
    uint32_t MoonSlotIndex = 0u;
    for (const RegisteredEnvironmentEntry& Reg : Registry.QueryRegisteredEntries())
    {
        if (Reg.Token == PrimaryEntry->Token)
        {
            MoonSlotIndex = Reg.MoonSlotIndex;
            break;
        }
    }

    if (LastInspectedToken != PrimaryEntry->Token)
    {
        LastInspectedToken  = PrimaryEntry->Token;
        ScrollOffsetY       = 0.0f;
        ActiveDragControlId = 0u;
    }

    const float       FooterH = 28.0f;
    const Rectangle2D ScrollViewport{ Bounds.X, Bounds.Y, Bounds.Width, std::max(40.0f, Bounds.Height - FooterH) };
    const Point2D     Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    if (ScrollViewport.Contains(Cursor) && std::fabs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY -= Input.WheelDeltaY * 36.0f;
    }
    const float MaxScroll = std::max(0.0f, LastContentHeight - ScrollViewport.Height);
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, MaxScroll);

    Surface.PushClip(ScrollViewport);

    const float PadX       = 22.0f;
    const float LeftX      = ScrollViewport.X + PadX;
    const float InnerWidth = std::max(160.0f, ScrollViewport.Width - PadX * 2.0f);
    float       CursorY    = ScrollViewport.Y + 18.0f - ScrollOffsetY;

    // Breadcrumb: "Inspector / Environment"
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CursorY },
        10.0f,
        DimInk,
        "Inspector / Environment");
    CursorY += 28.0f;

    // Large header title & subtitle (matches 10..14.png)
    const char* Headline = PrimaryEntry->Title.empty() ? QueryCategoryHeadline(Category) : PrimaryEntry->Title.c_str();
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Light,
        { LeftX, CursorY },
        26.0f,
        WorkspaceSurfaceColors::PrimaryInk,
        Headline);
    CursorY += 34.0f;

    const char* Subtitle = QueryCategorySubtitle(Category);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CursorY },
        11.0f,
        SubtleInk,
        Subtitle);
    CursorY += 24.0f;

    const float ContentStartY = ScrollViewport.Y - ScrollOffsetY;

    switch (Category)
    {
        case WorldEnvironmentCategory::Atmosphere:
            CursorY = RenderAtmospherePages(Surface, LeftX, CursorY, InnerWidth, *PrimaryEntry, Scene, Input, OutMutated);
            break;
        case WorldEnvironmentCategory::Sun:
            CursorY = RenderSunPages(Surface, LeftX, CursorY, InnerWidth, *PrimaryEntry, Scene, Input, OutMutated);
            break;
        case WorldEnvironmentCategory::Clouds:
            CursorY = RenderCloudsPages(Surface, LeftX, CursorY, InnerWidth, *PrimaryEntry, Scene, Input, OutMutated);
            break;
        case WorldEnvironmentCategory::Moon:
            CursorY = RenderMoonPages(Surface, LeftX, CursorY, InnerWidth, *PrimaryEntry, MoonSlotIndex, Scene, Input, OutMutated);
            break;
        case WorldEnvironmentCategory::Stars:
            CursorY = RenderStarsPages(Surface, LeftX, CursorY, InnerWidth, *PrimaryEntry, Scene, Input, OutMutated);
            break;
    }

    LastContentHeight = std::max(ScrollViewport.Height, (CursorY - ContentStartY) + 16.0f);
    Surface.PopClip();

    // Footer bar (matches 10..14.png without HTML-only preview string)
    const Rectangle2D Footer{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.StrokeLine({ Footer.X, Footer.Y }, { Footer.Right(), Footer.Y }, ColorRgba{ 36u, 36u, 36u, 255u }, 1.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { Footer.X + 18.0f, Footer.Y + 8.0f },
        Footer.Width - 110.0f,
        10.0f,
        DimInk,
        Subtitle);
    const char* FooterRight = "Native Engine";
    const float RightW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, FooterRight);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { Footer.Right() - 18.0f - RightW, Footer.Y + 8.0f },
        10.0f,
        DimInk,
        FooterRight);

    if (OutMutated)
    {
        (void)Registry.SyncToWorld(*Outliner, Scene, DisplaySettings);
        if (DisplaySettings != nullptr)
        {
            DisplaySettings->SolarTimeOfDayHours = Scene.Atmosphere.Solar.LocalHours;
            DisplaySettings->CloudFraction       = Scene.Atmosphere.Clouds.CloudFraction;
            DisplaySettings->CloudWindSpeedMps   = Scene.Atmosphere.Clouds.WindSpeedMetersPerSecond;
            DisplaySettings->DayCyclePlaying     = Scene.Atmosphere.Solar.CycleAnimationEnabled;
        }
    }

    return true;
}

} // namespace Frontier

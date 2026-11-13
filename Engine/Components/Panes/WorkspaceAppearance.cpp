/*============================================================================================================================================
                                                        WORKSPACEAPPEARANCE.CPP
============================================================================================================================================*/
// 🧩 Shared surface color tokens, VisualToken, VectorGraphic, Workspace icons, indicators, signs, and symbols

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Input/InputExtension.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier::WorkspaceAppearance
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] ColorRgba ScaleAlpha(ColorRgba BaseColor, float Opacity) noexcept
{
    const float Clamped = std::clamp(Opacity, 0.0f, 1.0f);
    return {
        BaseColor.Red,
        BaseColor.Green,
        BaseColor.Blue,
        static_cast<uint8_t>(std::round(static_cast<float>(BaseColor.Alpha) * Clamped)),
    };
}

void EncodeUtf8Codepoint(uint32_t Codepoint, std::string& Destination)
{
    if (Codepoint == 0u || Codepoint == '\r' || Codepoint == '\n' || Codepoint < 32u)
    {
        return;
    }
    if (Codepoint < 0x80u)
    {
        Destination.push_back(static_cast<char>(Codepoint));
    }
    else if (Codepoint < 0x800u)
    {
        Destination.push_back(static_cast<char>(0xC0u | (Codepoint >> 6u)));
        Destination.push_back(static_cast<char>(0x80u | (Codepoint & 0x3Fu)));
    }
    else if (Codepoint < 0x10000u)
    {
        Destination.push_back(static_cast<char>(0xE0u | (Codepoint >> 12u)));
        Destination.push_back(static_cast<char>(0x80u | ((Codepoint >> 6u) & 0x3Fu)));
        Destination.push_back(static_cast<char>(0x80u | (Codepoint & 0x3Fu)));
    }
    else if (Codepoint <= 0x10FFFFu)
    {
        Destination.push_back(static_cast<char>(0xF0u | (Codepoint >> 18u)));
        Destination.push_back(static_cast<char>(0x80u | ((Codepoint >> 12u) & 0x3Fu)));
        Destination.push_back(static_cast<char>(0x80u | ((Codepoint >> 6u) & 0x3Fu)));
        Destination.push_back(static_cast<char>(0x80u | (Codepoint & 0x3Fu)));
    }
}

void ConstructHierarchySubIcon(
    PixelSpace& Surface,
    Point2D     Origin,
    float       ScaleFactor,
    ColorRgba   StrokeInk,
    float       StrokeWidth) noexcept
{
    const auto SX = [&](float X) noexcept -> float { return Origin.X + X * ScaleFactor; };
    const auto SY = [&](float Y) noexcept -> float { return Origin.Y + Y * ScaleFactor; };

    const float TopRx = std::max(0.6f, 1.5f * ScaleFactor);
    Surface.StrokeRectangle(
        { SX(11.0f), SY(3.0f), 10.0f * ScaleFactor, 7.0f * ScaleFactor },
        StrokeInk,
        TopRx,
        StrokeWidth);
    Surface.StrokeLine({ SX(16.0f), SY(10.0f) }, { SX(16.0f), SY(17.0f) }, StrokeInk, StrokeWidth);

    const std::array<Point2D, 4> Fork = {{
        { SX(6.0f),  SY(22.0f) },
        { SX(6.0f),  SY(17.0f) },
        { SX(26.0f), SY(17.0f) },
        { SX(26.0f), SY(22.0f) },
    }};
    Surface.StrokePolyline(Fork, StrokeInk, StrokeWidth, false);

    Surface.StrokeRectangle(
        { SX(2.0f), SY(22.0f), 9.0f * ScaleFactor, 7.0f * ScaleFactor },
        StrokeInk,
        TopRx,
        StrokeWidth);
    Surface.StrokeRectangle(
        { SX(21.0f), SY(22.0f), 9.0f * ScaleFactor, 7.0f * ScaleFactor },
        StrokeInk,
        TopRx,
        StrokeWidth);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             INPUT & TYPOGRAPHY CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

PaneInputReading CaptureImGuiPaneInput(Rectangle2D ScreenBounds, float ScaleFactor) noexcept
{
    PaneInputReading Reading{};
    if (ImGui::GetCurrentContext() == nullptr)
    {
        return Reading;
    }

    const auto& Snapshot  = Input::InputExtension::InspectActiveEditorSnapshot();
    const float SafeScale = std::max(0.25f, ScaleFactor);

    if (Snapshot.PointerSeeded && Snapshot.PositionX > -1.0e8f && Snapshot.PositionY > -1.0e8f)
    {
        Reading.PrimaryPointer.X = (Snapshot.PositionX - ScreenBounds.X) / SafeScale;
        Reading.PrimaryPointer.Y = (Snapshot.PositionY - ScreenBounds.Y) / SafeScale;
        Reading.PointerDeltaX    = Snapshot.TickDeltaX / SafeScale;
        Reading.PointerDeltaY    = Snapshot.TickDeltaY / SafeScale;
    }

    const Point2D ScreenCursor{ Snapshot.PositionX, Snapshot.PositionY };
    const bool    InsideBounds  = Snapshot.PointerSeeded && ScreenBounds.Contains(ScreenCursor);
    const bool    HoveredWindow = InsideBounds && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    Reading.PrimaryPointer.Enabled  = HoveredWindow || InsideBounds;
    Reading.PrimaryPointer.Down     = Snapshot.QueryButtonDown(0u);
    Reading.PrimaryPointer.Pressed  = Snapshot.QueryButtonPressed(0u);
    Reading.PrimaryPointer.Released = Snapshot.QueryButtonReleased(0u);

    Reading.SecondaryDown     = Snapshot.QueryButtonDown(1u);
    Reading.SecondaryPressed  = Snapshot.QueryButtonPressed(1u);
    Reading.SecondaryReleased = Snapshot.QueryButtonReleased(1u);
    Reading.MiddleDown        = Snapshot.QueryButtonDown(2u);
    Reading.MiddlePressed     = Snapshot.QueryButtonPressed(2u);
    Reading.MiddleReleased    = Snapshot.QueryButtonReleased(2u);
    Reading.DoubleClicked     = Snapshot.QueryButtonDoubleClicked(0u);
    Reading.WheelDeltaY       = (HoveredWindow || InsideBounds) ? Snapshot.ScrollDeltaY : 0.0f;

    Reading.ShiftHeld         = Snapshot.ShiftHeld;
    Reading.ControlHeld       = Snapshot.ControlHeld;
    Reading.AltHeld           = Snapshot.AltHeld;
    Reading.EnterPressed      = Snapshot.QueryKeyPressed(Input::InputLogicalKey::Enter, false) ||
                                Snapshot.QueryKeyPressed(Input::InputLogicalKey::KeypadEnter, false);
    Reading.EscapePressed     = Snapshot.QueryKeyPressed(Input::InputLogicalKey::Escape, false);
    Reading.DeletePressed     = Snapshot.QueryKeyPressed(Input::InputLogicalKey::Delete, false);
    Reading.BackspacePressed  = Snapshot.QueryKeyPressed(Input::InputLogicalKey::Backspace, true);
    Reading.TabPressed        = Snapshot.QueryKeyPressed(Input::InputLogicalKey::Tab, false);
    Reading.ArrowLeftPressed  = Snapshot.QueryKeyPressed(Input::InputLogicalKey::LeftArrow, true);
    Reading.ArrowRightPressed = Snapshot.QueryKeyPressed(Input::InputLogicalKey::RightArrow, true);
    Reading.HomePressed       = Snapshot.QueryKeyPressed(Input::InputLogicalKey::Home, false);
    Reading.EndPressed        = Snapshot.QueryKeyPressed(Input::InputLogicalKey::End, false);

    Reading.ShortcutAll       = Snapshot.ControlHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::A, false);
    Reading.ShortcutFind      = Snapshot.ControlHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::F, false);
    Reading.ShortcutGroup     = Snapshot.ControlHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::G, false);
    Reading.ShortcutDuplicate = Snapshot.ControlHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::D, false);
    Reading.ShortcutLinked    = Snapshot.ControlHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::L, false);
    Reading.ShortcutInvert    = Snapshot.ControlHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::I, false);
    Reading.ShortcutUndo      = Snapshot.ControlHeld && !Snapshot.ShiftHeld && !Snapshot.AltHeld &&
                                Snapshot.QueryKeyPressed(Input::InputLogicalKey::Z, false);
    Reading.ShortcutRedo      = Snapshot.ControlHeld && !Snapshot.AltHeld &&
                                ((Snapshot.ShiftHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::Z, false)) ||
                                 (!Snapshot.ShiftHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::Y, false)));
    Reading.ToggleDiagnostics = Snapshot.QueryKeyPressed(Input::InputLogicalKey::F3, false);

    for (const uint32_t Codepoint : Snapshot.QueryTypedCodepoints())
    {
        EncodeUtf8Codepoint(Codepoint, Reading.TypedUtf8);
    }

    return Reading;
}

void ForwardInputToTextBuffer(TextEditBuffer& Buffer, const PaneInputReading& Input, double Δt) noexcept
{
    Buffer.BlinkPhase = std::fmod(
        Buffer.BlinkPhase + static_cast<float>(std::max(0.0, Δt)),
        ControlMetrics::CaretBlinkSeconds);
    if (!Buffer.Active)
    {
        return;
    }

    if (Input.ShortcutAll)
    {
        Buffer.SelectAll();
    }
    if (Input.BackspacePressed)
    {
        Buffer.Backspace();
    }
    if (Input.DeletePressed)
    {
        Buffer.DeleteForward();
    }
    if (Input.ArrowLeftPressed)
    {
        Buffer.MoveCaret(-1, Input.ShiftHeld);
    }
    if (Input.ArrowRightPressed)
    {
        Buffer.MoveCaret(1, Input.ShiftHeld);
    }
    if (Input.HomePressed)
    {
        Buffer.MoveHome(Input.ShiftHeld);
    }
    if (Input.EndPressed)
    {
        Buffer.MoveEnd(Input.ShiftHeld);
    }
    if (!Input.TypedUtf8.empty() && !Input.ControlHeld)
    {
        Buffer.Insert(Input.TypedUtf8.c_str());
    }
}

ImFont* ResolveWeightedFont(FontWeightClass Weight, bool Monospace) noexcept
{
    const TypographyRepository* Repo = TypographyRepository::QueryCurrent();
    if (Repo == nullptr)
    {
        return nullptr;
    }

    auto TryFamily = [&](std::string_view Family) -> ImFont*
    {
        const int32_t GroupIdx = Repo->ResolveGroupIndex(Family);
        if (GroupIdx >= 0)
        {
            return Repo->QueryFont(static_cast<uint32_t>(GroupIdx), Weight);
        }
        return nullptr;
    };

    if (Monospace)
    {
        if (ImFont* Mono = TryFamily("JetBrains Mono"))
        {
            return Mono;
        }
    }

    if (ImFont* Primary = TryFamily("DM Sans"))
    {
        return Primary;
    }
    if (ImFont* Secondary = TryFamily("Inter"))
    {
        return Secondary;
    }
    if (Repo->QueryGroupCount() > 0u)
    {
        return Repo->QueryFont(0u, FontWeightClass::Regular);
    }
    return nullptr;
}

void ConstructWeightedText(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    Point2D          Position,
    float            PixelSize,
    ColorRgba        Ink,
    std::string_view Text,
    bool             Monospace) noexcept
{
    if (Text.empty() || Ink.Alpha == 0u || PixelSize <= 0.0f)
    {
        return;
    }

    ImFont* Resolved = ResolveWeightedFont(Weight, Monospace);
    if (Resolved != nullptr)
    {
        Surface.PushTypeface(Resolved);
    }
    Surface.Text(Position, PixelSize, Ink, Text.data(), Text.data() + Text.size());
    if (Resolved != nullptr)
    {
        Surface.PopTypeface();
    }
}

float MeasureWeightedTextWidth(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    float            PixelSize,
    std::string_view Text,
    bool             Monospace) noexcept
{
    if (Text.empty() || PixelSize <= 0.0f)
    {
        return 0.0f;
    }

    ImFont* Resolved = ResolveWeightedFont(Weight, Monospace);
    if (Resolved != nullptr)
    {
        Surface.PushTypeface(Resolved);
    }
    const float Width = Surface.MeasureTextWidth(PixelSize, Text.data(), Text.data() + Text.size());
    if (Resolved != nullptr)
    {
        Surface.PopTypeface();
    }
    return Width;
}

void ConstructTruncatedText(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    Point2D          Position,
    float            MaxWidth,
    float            PixelSize,
    ColorRgba        Ink,
    std::string_view Text,
    bool             Monospace) noexcept
{
    if (Text.empty() || MaxWidth <= 2.0f || Ink.Alpha == 0u)
    {
        return;
    }

    if (MeasureWeightedTextWidth(Surface, Weight, PixelSize, Text, Monospace) <= MaxWidth)
    {
        ConstructWeightedText(Surface, Weight, Position, PixelSize, Ink, Text, Monospace);
        return;
    }

    std::string Candidate{};
    Candidate.reserve(Text.size() + 3u);
    for (std::size_t Count = Text.size(); Count > 0u; --Count)
    {
        Candidate.assign(Text.data(), Count);
        Candidate.append("...");
        if (MeasureWeightedTextWidth(Surface, Weight, PixelSize, Candidate, Monospace) <= MaxWidth)
        {
            ConstructWeightedText(Surface, Weight, Position, PixelSize, Ink, Candidate, Monospace);
            return;
        }
    }
}

void ConstructRightAlignedText(
    PixelSpace&      Surface,
    FontWeightClass  Weight,
    Point2D          RightTopAnchor,
    float            PixelSize,
    ColorRgba        Ink,
    std::string_view Text,
    bool             Monospace) noexcept
{
    const float Width = MeasureWeightedTextWidth(Surface, Weight, PixelSize, Text, Monospace);
    ConstructWeightedText(Surface, Weight, { RightTopAnchor.X - Width, RightTopAnchor.Y }, PixelSize, Ink, Text, Monospace);
}

//------------------------------------------------------------------------------------------------------------------------
//                                         VISUALTOKEN & VECTORGRAPHIC PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

ColorRgba QueryDockedPaneBackground() noexcept
{
    if (ImGui::GetCurrentContext() != nullptr)
    {
        const ImVec4 WinBg = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        return ColorRgba{
            static_cast<uint8_t>(std::clamp(std::lround(WinBg.x * 255.0f), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(WinBg.y * 255.0f), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(WinBg.z * 255.0f), 0L, 255L)),
            static_cast<uint8_t>(std::clamp(std::lround(WinBg.w * 255.0f), 0L, 255L)),
        };
    }
    return WorkspaceSurfaceColors::DockBackground;
}

ColorRgba QueryCategoryAccent(ConstructSlotCategory Category, ColorRgba FolderTint) noexcept
{
    switch (Category)
    {
        case ConstructSlotCategory::Folder:
            return FolderTint;
        case ConstructSlotCategory::CategoryOne:
            return { 56, 156, 255, 255 };  // #389CFF (editor-cube)
        case ConstructSlotCategory::CategoryTwo:
            return { 192, 174, 212, 255 }; // #C0AED4 (collection-stacked-layers)
        case ConstructSlotCategory::CategoryThree:
            return { 255, 205, 56, 255 };  // #FFCD38 (editor-point-light)
        case ConstructSlotCategory::CategoryFour:
            return { 160, 164, 182, 255 }; // #A0A4B6 (camera)
    }
    return WorkspaceSurfaceColors::PrimaryInk;
}

VisualToken QueryCategoryVisualToken(ConstructSlotCategory Category, float Extent, ColorRgba FolderTint) noexcept
{
    VisualToken Token{};
    Token.Shape     = VisualTokenShape::Circle;
    Token.FillColor = QueryCategoryAccent(Category, FolderTint);
    Token.Extent    = Extent;
    return Token;
}

const char* QueryCategoryDefaultLabel(ConstructSlotCategory Category) noexcept
{
    switch (Category)
    {
        case ConstructSlotCategory::Folder:
            return "Collection";
        case ConstructSlotCategory::CategoryOne:
            return "Type 1";
        case ConstructSlotCategory::CategoryTwo:
            return "Type 2";
        case ConstructSlotCategory::CategoryThree:
            return "Type 3";
        case ConstructSlotCategory::CategoryFour:
            return "Type 4";
    }
    return "Entry";
}

const char* QueryProjectionPresetLabel(ViewportProjectionPreset Preset) noexcept
{
    switch (Preset)
    {
        case ViewportProjectionPreset::Perspective:
            return "Perspective";
        case ViewportProjectionPreset::Orthographic:
            return "Orthographic";
        case ViewportProjectionPreset::Top:
            return "Top";
        case ViewportProjectionPreset::Front:
            return "Front";
        case ViewportProjectionPreset::Right:
            return "Right";
        case ViewportProjectionPreset::Count:
            break;
    }
    return "Perspective";
}

void ConstructVisualToken(
    PixelSpace&        Surface,
    Point2D            Center,
    const VisualToken& Token,
    float              Opacity) noexcept
{
    const float     Half       = Token.Extent * 0.5f;
    const ColorRgba FillRgba   = ScaleAlpha(Token.FillColor, Opacity);
    const ColorRgba StrokeRgba = ScaleAlpha(Token.StrokeColor, Opacity);

    if (Token.Shape == VisualTokenShape::Circle)
    {
        if (FillRgba.Alpha > 0u)
        {
            Surface.FillCircle(Center, Half, FillRgba);
        }
        if (StrokeRgba.Alpha > 0u && Token.StrokeWidth > 0.0f)
        {
            Surface.StrokeCircle(Center, Half + 2.2f, StrokeRgba, Token.StrokeWidth);
        }
        return;
    }

    const Rectangle2D Bounds{ Center.X - Half, Center.Y - Half, Token.Extent, Token.Extent };
    const float       Corner = (Token.Shape == VisualTokenShape::RoundedSquare) ? Token.CornerRadius : 0.0f;
    if (FillRgba.Alpha > 0u)
    {
        Surface.FillRectangle(Bounds, FillRgba, Corner);
    }
    if (StrokeRgba.Alpha > 0u && Token.StrokeWidth > 0.0f)
    {
        Surface.StrokeRectangle(Bounds, StrokeRgba, Corner, Token.StrokeWidth);
    }
}

void ConstructVectorGraphic(
    PixelSpace&          Surface,
    const VectorGraphic& Graphic,
    Point2D              TopLeft,
    float                Extent,
    ColorRgba            Ink) noexcept
{
    if (Graphic.SvgPath.empty() || Extent <= 0.0f || Ink.Alpha == 0u)
    {
        return;
    }

    const VectorIconPlacement Placement{ TopLeft.X, TopLeft.Y, Extent, Ink, Graphic.StrokeWidth };
    if (Graphic.Filled)
    {
        VectorIconRepository::FillSvgPath(Surface, Graphic.SvgPath, Placement, Graphic.ViewBox);
    }
    else
    {
        VectorIconRepository::StrokeSvgPath(Surface, Graphic.SvgPath, Placement, Graphic.ViewBox);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           AUTHENTIC PROJECT-ZERO UI ICONS
//------------------------------------------------------------------------------------------------------------------------

void ConstructShowcaseFolderIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    ColorRgba   AccentTint,
    float       Opacity) noexcept
{
    if (Extent <= 2.0f || Opacity <= 0.0f)
    {
        return;
    }

    const auto ProjectX = [&](float SvgX) noexcept -> float { return TopLeft.X + (SvgX / 256.0f) * Extent; };
    const auto ProjectY = [&](float SvgY) noexcept -> float { return TopLeft.Y + (SvgY / 256.0f) * Extent; };

    // 1. Back rounded folder body (x=29 y=55 w=195 h=146 rx=20 in 256x256 space, zero shadow)
    const Rectangle2D BackRect{
        ProjectX(29.0f),
        ProjectY(55.0f),
        ProjectX(224.0f) - ProjectX(29.0f),
        ProjectY(201.0f) - ProjectY(55.0f),
    };
    const float BackCorner   = (20.0f / 256.0f) * Extent;
    const float PocketStroke = std::max(0.9f, Extent * 0.03125f);
    Surface.FillRectangle(BackRect, ScaleAlpha({ 48, 48, 48, 255 }, Opacity), BackCorner);
    Surface.StrokeRectangle(BackRect, ScaleAlpha({ 85, 85, 85, 255 }, Opacity), BackCorner, PocketStroke);

    // 2. Two tilted #DEDEDB cream document sheets peeking from top pocket
    {
        const std::array<Point2D, 4> LeftSheet = {{
            { ProjectX(46.0f),  ProjectY(63.0f)  },
            { ProjectX(143.0f), ProjectY(55.0f)  },
            { ProjectX(149.0f), ProjectY(122.0f) },
            { ProjectX(52.0f),  ProjectY(130.0f) },
        }};
        Surface.FillPolygon(LeftSheet, ScaleAlpha({ 222, 222, 219, 245 }, Opacity));

        const std::array<Point2D, 4> RightSheet = {{
            { ProjectX(132.0f), ProjectY(66.0f)  },
            { ProjectX(206.0f), ProjectY(78.0f)  },
            { ProjectX(198.0f), ProjectY(134.0f) },
            { ProjectX(124.0f), ProjectY(124.0f) },
        }};
        Surface.FillPolygon(RightSheet, ScaleAlpha({ 222, 222, 219, 255 }, Opacity));

        if (Extent >= 20.0f)
        {
            ConstructHierarchySubIcon(
                Surface,
                { ProjectX(60.0f), ProjectY(69.0f) },
                (0.78f / 256.0f) * Extent,
                ScaleAlpha({ 120, 122, 120, 115 }, Opacity),
                0.85f);
        }
    }

    // 3. Stepped front folder pocket (M29 98 q0-23 23-23 h55 q12 0 23 8 l16 10 q8 5 18 5 h41 q24 0 24 24 v72...)
    {
        const std::array<Point2D, 10> FrontPocket = {{
            { ProjectX(29.0f),  ProjectY(92.0f)  },
            { ProjectX(42.0f),  ProjectY(75.0f)  },
            { ProjectX(112.0f), ProjectY(75.0f)  },
            { ProjectX(146.0f), ProjectY(98.0f)  },
            { ProjectX(212.0f), ProjectY(98.0f)  },
            { ProjectX(224.0f), ProjectY(110.0f) },
            { ProjectX(224.0f), ProjectY(204.0f) },
            { ProjectX(212.0f), ProjectY(214.0f) },
            { ProjectX(41.0f),  ProjectY(214.0f) },
            { ProjectX(29.0f),  ProjectY(204.0f) },
        }};
        Surface.FillPolygon(FrontPocket, ScaleAlpha({ 60, 60, 60, 255 }, Opacity));
        Surface.StrokePolyline(FrontPocket, ScaleAlpha({ 85, 85, 85, 255 }, Opacity), PocketStroke, true);
    }

    // 4. Bottom-left rounded scene-tree icon tile (x=44 y=159 w=43 h=43 rx=11 + 3-node hierarchy icon)
    const Rectangle2D SubTile{
        ProjectX(44.0f),
        ProjectY(159.0f),
        ProjectX(87.0f) - ProjectX(44.0f),
        ProjectY(202.0f) - ProjectY(159.0f),
    };
    const float SubCorner = (11.0f / 256.0f) * Extent;
    Surface.FillRectangle(SubTile, ScaleAlpha({ 57, 57, 57, 255 }, Opacity), SubCorner);
    Surface.FillRectangle(SubTile, ScaleAlpha(AccentTint, Opacity * 0.14f), SubCorner);
    Surface.StrokeRectangle(SubTile, ScaleAlpha({ 85, 85, 85, 255 }, Opacity), SubCorner, 0.9f);

    ConstructHierarchySubIcon(
        Surface,
        { ProjectX(52.0f), ProjectY(167.0f) },
        (0.84375f / 256.0f) * Extent,
        ScaleAlpha(AccentTint, Opacity),
        std::max(1.0f, Extent * 0.042f));
}

void ConstructGenericFolderIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    ColorRgba   AccentTint,
    float       Opacity) noexcept
{
    if (Extent <= 2.0f || Opacity <= 0.0f)
    {
        return;
    }

    const auto ProjectX = [&](float SvgX) noexcept -> float { return TopLeft.X + (SvgX / 256.0f) * Extent; };
    const auto ProjectY = [&](float SvgY) noexcept -> float { return TopLeft.Y + (SvgY / 256.0f) * Extent; };

    const std::array<Point2D, 8> BackTab = {{
        { ProjectX(26.0f),  ProjectY(56.0f)  },
        { ProjectX(40.0f),  ProjectY(46.0f)  },
        { ProjectX(103.0f), ProjectY(46.0f)  },
        { ProjectX(127.0f), ProjectY(66.0f)  },
        { ProjectX(218.0f), ProjectY(66.0f)  },
        { ProjectX(230.0f), ProjectY(78.0f)  },
        { ProjectX(230.0f), ProjectY(198.0f) },
        { ProjectX(26.0f),  ProjectY(198.0f) },
    }};
    Surface.FillPolygon(BackTab, ScaleAlpha({ 48, 48, 48, 255 }, Opacity));
    Surface.StrokePolyline(BackTab, ScaleAlpha({ 85, 85, 85, 255 }, Opacity), 1.0f, true);

    const Rectangle2D FrontBody{
        ProjectX(26.0f),
        ProjectY(88.0f),
        ProjectX(230.0f) - ProjectX(26.0f),
        ProjectY(210.0f) - ProjectY(88.0f),
    };
    const float Corner = (18.0f / 256.0f) * Extent;
    Surface.FillRectangle(FrontBody, ScaleAlpha({ 60, 60, 60, 255 }, Opacity), Corner);
    Surface.StrokeRectangle(FrontBody, ScaleAlpha({ 85, 85, 85, 255 }, Opacity), Corner, 1.0f);

    const float RuleStroke = std::max(1.2f, (8.0f / 256.0f) * Extent);
    Surface.StrokeLine(
        { ProjectX(82.0f), ProjectY(129.0f) },
        { ProjectX(174.0f), ProjectY(129.0f) },
        ScaleAlpha(AccentTint, Opacity),
        RuleStroke);
    Surface.StrokeLine(
        { ProjectX(82.0f), ProjectY(151.0f) },
        { ProjectX(142.0f), ProjectY(151.0f) },
        ScaleAlpha(AccentTint, Opacity),
        RuleStroke);
}

void ConstructCameraIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    float       Opacity) noexcept
{
    if (Extent <= 2.0f || Opacity <= 0.0f)
    {
        return;
    }

    const auto ProjectX = [&](float SvgX) noexcept -> float { return TopLeft.X + (SvgX / 256.0f) * Extent; };
    const auto ProjectY = [&](float SvgY) noexcept -> float { return TopLeft.Y + (SvgY / 256.0f) * Extent; };

    // Workspace 256x256 camera icon (zero blur/shadow)
    const Rectangle2D Body{
        ProjectX(28.0f),
        ProjectY(55.0f),
        ProjectX(228.0f) - ProjectX(28.0f),
        ProjectY(209.0f) - ProjectY(55.0f),
    };
    const float BodyRx = (23.0f / 256.0f) * Extent;
    Surface.FillRectangle(Body, ScaleAlpha({ 54, 55, 63, 255 }, Opacity), BodyRx);
    Surface.StrokeRectangle(Body, ScaleAlpha({ 134, 136, 150, 220 }, Opacity), BodyRx, std::max(1.0f, Extent * 0.045f));

    const Point2D LensCenter{ ProjectX(128.0f), ProjectY(134.0f) };
    Surface.FillCircle(LensCenter, (58.0f / 256.0f) * Extent, ScaleAlpha({ 42, 44, 52, 255 }, Opacity));
    Surface.StrokeCircle(LensCenter, (58.0f / 256.0f) * Extent, ScaleAlpha({ 146, 146, 158, 165 }, Opacity), 1.0f);
    Surface.FillCircle(LensCenter, (42.0f / 256.0f) * Extent, ScaleAlpha({ 16, 17, 24, 255 }, Opacity));
    Surface.StrokeCircle(LensCenter, (42.0f / 256.0f) * Extent, ScaleAlpha({ 119, 119, 131, 150 }, Opacity), 0.9f);
    Surface.FillCircle(
        { ProjectX(118.0f), ProjectY(118.0f) },
        std::max(1.1f, (11.0f / 256.0f) * Extent),
        ScaleAlpha({ 226, 230, 242, 165 }, Opacity));

    const Rectangle2D FlashWindow{
        ProjectX(184.0f),
        ProjectY(71.0f),
        std::max(2.0f, (22.0f / 256.0f) * Extent),
        std::max(1.6f, (16.0f / 256.0f) * Extent),
    };
    Surface.FillRectangle(FlashWindow, ScaleAlpha({ 220, 222, 235, 225 }, Opacity), 1.0f);
}

void ConstructSlotCategoryIcon(
    PixelSpace&           Surface,
    ConstructSlotCategory Category,
    Point2D               TopLeft,
    float                 Extent,
    ColorRgba             FolderTint,
    float                 Opacity) noexcept
{
    if (Extent <= 2.0f || Opacity <= 0.0f)
    {
        return;
    }

    const auto ProjectX = [&](float SvgX) noexcept -> float { return TopLeft.X + (SvgX / 256.0f) * Extent; };
    const auto ProjectY = [&](float SvgY) noexcept -> float { return TopLeft.Y + (SvgY / 256.0f) * Extent; };

    switch (Category)
    {
        case ConstructSlotCategory::Folder:
            ConstructShowcaseFolderIcon(Surface, TopLeft, Extent, FolderTint, Opacity);
            break;

        case ConstructSlotCategory::CategoryOne:
        {
            // Workspace editor-cube: #174B88 filled hexagon + #389CFF bold isometric edges
            const std::array<Point2D, 6> Hex = {{
                { ProjectX(128.0f), ProjectY(40.0f)  },
                { ProjectX(204.0f), ProjectY(84.0f)  },
                { ProjectX(204.0f), ProjectY(172.0f) },
                { ProjectX(128.0f), ProjectY(216.0f) },
                { ProjectX(52.0f),  ProjectY(172.0f) },
                { ProjectX(52.0f),  ProjectY(84.0f)  },
            }};
            const ColorRgba EdgeInk = ScaleAlpha({ 56, 156, 255, 255 }, Opacity);
            const float     EdgeW   = std::max(1.3f, (7.0f / 256.0f) * Extent);
            Surface.FillPolygon(Hex, ScaleAlpha({ 23, 75, 136, 255 }, Opacity));
            Surface.StrokePolyline(Hex, EdgeInk, EdgeW, true);

            const std::array<Point2D, 3> UpperY = {{
                { ProjectX(52.0f),  ProjectY(84.0f)  },
                { ProjectX(128.0f), ProjectY(128.0f) },
                { ProjectX(204.0f), ProjectY(84.0f)  },
            }};
            Surface.StrokePolyline(UpperY, EdgeInk, EdgeW, false);
            Surface.StrokeLine(
                { ProjectX(128.0f), ProjectY(128.0f) },
                { ProjectX(128.0f), ProjectY(216.0f) },
                EdgeInk,
                EdgeW);
            break;
        }

        case ConstructSlotCategory::CategoryTwo:
        {
            // Workspace collection-stacked-layers (scale 5.3333 in 256x256 space)
            const auto Slab = [&](float CenterY, ColorRgba Fill, ColorRgba Edge)
            {
                const float S = Extent / 48.0f;
                const std::array<Point2D, 4> Diamond = {{
                    { TopLeft.X + 5.0f * S,  TopLeft.Y + CenterY * S         },
                    { TopLeft.X + 24.0f * S, TopLeft.Y + (CenterY - 10.0f) * S },
                    { TopLeft.X + 43.0f * S, TopLeft.Y + CenterY * S         },
                    { TopLeft.X + 24.0f * S, TopLeft.Y + (CenterY + 11.0f) * S },
                }};
                Surface.FillPolygon(Diamond, ScaleAlpha(Fill, Opacity));
                Surface.StrokePolyline(Diamond, ScaleAlpha(Edge, Opacity), std::max(1.1f, 2.2f * S), true);
            };
            Slab(30.0f, { 118, 102, 143, 255 }, { 160, 142, 184, 255 });
            Slab(23.0f, { 160, 139, 185, 255 }, { 192, 174, 212, 255 });
            Slab(16.0f, { 197, 179, 218, 255 }, { 223, 210, 236, 255 });
            break;
        }

        case ConstructSlotCategory::CategoryThree:
        {
            // Workspace editor-point-light: #F0AD22 filled core + #FFCD38 stroke & 8 rays
            const Point2D   Center{ ProjectX(128.0f), ProjectY(128.0f) };
            const float     CoreR  = (32.0f / 256.0f) * Extent;
            const float     Stroke = std::max(1.3f, (7.0f / 256.0f) * Extent);
            const ColorRgba RayInk = ScaleAlpha({ 255, 205, 56, 255 }, Opacity);
            Surface.FillCircle(Center, CoreR, ScaleAlpha({ 240, 173, 34, 255 }, Opacity));
            Surface.StrokeCircle(Center, CoreR, RayInk, Stroke);

            const float InnerRay = (63.0f / 256.0f) * Extent;
            const float OuterRay = (81.0f / 256.0f) * Extent;
            for (int Ray = 0; Ray < 8; ++Ray)
            {
                const float Angle = static_cast<float>(Ray) * 0.78539816339f;
                const float CosA  = std::cos(Angle);
                const float SinA  = std::sin(Angle);
                Surface.StrokeLine(
                    { Center.X + CosA * InnerRay, Center.Y + SinA * InnerRay },
                    { Center.X + CosA * OuterRay, Center.Y + SinA * OuterRay },
                    RayInk,
                    Stroke);
            }
            break;
        }

        case ConstructSlotCategory::CategoryFour:
            ConstructCameraIcon(Surface, TopLeft, Extent, Opacity);
            break;
    }
}

void ConstructFilterSlidersIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    ColorRgba   Ink,
    ColorRgba   KnobFill) noexcept
{
    // Workspace Inspectors.jsx Glyph Name="filter" (24x24 viewBox)
    const auto PX = [&](float X) noexcept -> float { return TopLeft.X + (X / 24.0f) * Extent; };
    const auto PY = [&](float Y) noexcept -> float { return TopLeft.Y + (Y / 24.0f) * Extent; };
    const float Stroke = std::max(1.2f, Extent * (1.5f / 24.0f));
    const float KnobR  = std::max(1.5f, Extent * (2.1f / 24.0f));

    Surface.StrokeLine({ PX(4.0f), PY(6.0f) }, { PX(20.0f), PY(6.0f) }, Ink, Stroke);
    Surface.StrokeLine({ PX(4.0f), PY(12.0f) }, { PX(20.0f), PY(12.0f) }, Ink, Stroke);
    Surface.StrokeLine({ PX(4.0f), PY(18.0f) }, { PX(20.0f), PY(18.0f) }, Ink, Stroke);

    const std::array<Point2D, 3> Knobs = {{
        { PX(8.0f),  PY(6.0f)  },
        { PX(16.0f), PY(12.0f) },
        { PX(10.0f), PY(18.0f) },
    }};
    for (const Point2D Knob : Knobs)
    {
        Surface.FillCircle(Knob, KnobR, KnobFill);
        Surface.StrokeCircle(Knob, KnobR, Ink, Stroke);
    }
}

void ConstructEditIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept
{
    // ActionIcon.jsx "edit": m15 2 7 7-12 12-8 1 1-8Z with nib cutout
    const auto PX = [&](float X) noexcept -> float { return TopLeft.X + (X / 24.0f) * Extent; };
    const auto PY = [&](float Y) noexcept -> float { return TopLeft.Y + (Y / 24.0f) * Extent; };

    const std::array<Point2D, 4> Barrel = {{
        { PX(15.0f), PY(2.0f)  },
        { PX(22.0f), PY(9.0f)  },
        { PX(10.0f), PY(21.0f) },
        { PX(3.0f),  PY(14.0f) },
    }};
    Surface.FillPolygon(Barrel, Ink);

    const std::array<Point2D, 3> Tip = {{
        { PX(3.0f), PY(15.5f) },
        { PX(8.5f), PY(21.0f) },
        { PX(2.0f), PY(22.0f) },
    }};
    Surface.FillPolygon(Tip, Ink);
}

void ConstructSimulateIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    ColorRgba   Ink,
    ColorRgba   InteriorCutout) noexcept
{
    // ActionIcon.jsx "simulate": solid flask silhouette + upper interior cutout
    const auto PX = [&](float X) noexcept -> float { return TopLeft.X + (X / 24.0f) * Extent; };
    const auto PY = [&](float Y) noexcept -> float { return TopLeft.Y + (Y / 24.0f) * Extent; };

    Surface.FillRectangle({ PX(6.0f), PY(1.0f), PX(18.0f) - PX(6.0f), PY(4.0f) - PY(1.0f) }, Ink, 0.8f);
    Surface.FillRectangle({ PX(9.0f), PY(4.0f), PX(15.0f) - PX(9.0f), PY(8.5f) - PY(4.0f) }, Ink, 0.0f);

    const std::array<Point2D, 6> Cone = {{
        { PX(9.0f),  PY(8.0f)  },
        { PX(15.0f), PY(8.0f)  },
        { PX(22.0f), PY(19.5f) },
        { PX(20.0f), PY(23.0f) },
        { PX(4.0f),  PY(23.0f) },
        { PX(2.0f),  PY(19.5f) },
    }};
    Surface.FillPolygon(Cone, Ink);

    const std::array<Point2D, 4> UpperCavity = {{
        { PX(11.0f), PY(8.5f)  },
        { PX(13.0f), PY(8.5f)  },
        { PX(17.0f), PY(15.5f) },
        { PX(7.0f),  PY(15.5f) },
    }};
    Surface.FillPolygon(UpperCavity, InteriorCutout);
}

void ConstructPlayIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept
{
    // ActionIcon.jsx "play": M6 2 ... 21 12 ... 6 22Z
    const auto PX = [&](float X) noexcept -> float { return TopLeft.X + (X / 24.0f) * Extent; };
    const auto PY = [&](float Y) noexcept -> float { return TopLeft.Y + (Y / 24.0f) * Extent; };

    const std::array<Point2D, 3> Wedge = {{
        { PX(6.0f),  PY(2.5f)  },
        { PX(21.2f), PY(12.0f) },
        { PX(6.0f),  PY(21.5f) },
    }};
    Surface.FillPolygon(Wedge, Ink);
}

void ConstructPauseIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept
{
    // ActionIcon.jsx "pause": two solid rounded vertical bars
    const auto PX = [&](float X) noexcept -> float { return TopLeft.X + (X / 24.0f) * Extent; };
    const auto PY = [&](float Y) noexcept -> float { return TopLeft.Y + (Y / 24.0f) * Extent; };
    const float Rx = std::max(0.8f, (1.5f / 24.0f) * Extent);

    Surface.FillRectangle({ PX(5.0f), PY(4.0f), PX(10.0f) - PX(5.0f), PY(20.0f) - PY(4.0f) }, Ink, Rx);
    Surface.FillRectangle({ PX(14.0f), PY(4.0f), PX(19.0f) - PX(14.0f), PY(20.0f) - PY(4.0f) }, Ink, Rx);
}

void ConstructConstructIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept
{
    // ActionIcon.jsx "construct": m8 2 8 4v7l-8 4-8-4V6Z (opacity .65) + M17 11h3v4h4v3h-4v4h-3v-4h-4v-3h4Z
    const auto PX = [&](float X) noexcept -> float { return TopLeft.X + (X / 24.0f) * Extent; };
    const auto PY = [&](float Y) noexcept -> float { return TopLeft.Y + (Y / 24.0f) * Extent; };

    const std::array<Point2D, 6> CubeHex = {{
        { PX(8.0f),  PY(2.0f)  },
        { PX(16.0f), PY(6.0f)  },
        { PX(16.0f), PY(13.0f) },
        { PX(8.0f),  PY(17.0f) },
        { PX(0.5f),  PY(13.0f) },
        { PX(0.5f),  PY(6.0f)  },
    }};
    Surface.FillPolygon(CubeHex, ScaleAlpha(Ink, 0.65f));

    Surface.FillRectangle({ PX(17.0f), PY(11.0f), PX(20.0f) - PX(17.0f), PY(22.0f) - PY(11.0f) }, Ink, 0.4f);
    Surface.FillRectangle({ PX(13.0f), PY(15.0f), PX(24.0f) - PX(13.0f), PY(18.0f) - PY(15.0f) }, Ink, 0.4f);
}

void ConstructFocusIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept
{
    // ActionIcon.jsx "focus": 4 solid corner brackets + solid r=3 center circle
    const auto PX = [&](float X) noexcept -> float { return TopLeft.X + (X / 24.0f) * Extent; };
    const auto PY = [&](float Y) noexcept -> float { return TopLeft.Y + (Y / 24.0f) * Extent; };

    // Top-left L
    Surface.FillRectangle({ PX(2.0f), PY(2.0f), PX(9.0f) - PX(2.0f), PY(5.0f) - PY(2.0f) }, Ink);
    Surface.FillRectangle({ PX(2.0f), PY(2.0f), PX(5.0f) - PX(2.0f), PY(9.0f) - PY(2.0f) }, Ink);
    // Top-right L
    Surface.FillRectangle({ PX(15.0f), PY(2.0f), PX(22.0f) - PX(15.0f), PY(5.0f) - PY(2.0f) }, Ink);
    Surface.FillRectangle({ PX(19.0f), PY(2.0f), PX(22.0f) - PX(19.0f), PY(9.0f) - PY(2.0f) }, Ink);
    // Bottom-left L
    Surface.FillRectangle({ PX(2.0f), PY(15.0f), PX(5.0f) - PX(2.0f), PY(22.0f) - PY(15.0f) }, Ink);
    Surface.FillRectangle({ PX(2.0f), PY(19.0f), PX(9.0f) - PX(2.0f), PY(22.0f) - PY(19.0f) }, Ink);
    // Bottom-right L
    Surface.FillRectangle({ PX(19.0f), PY(15.0f), PX(22.0f) - PX(19.0f), PY(22.0f) - PY(15.0f) }, Ink);
    Surface.FillRectangle({ PX(15.0f), PY(19.0f), PX(22.0f) - PX(15.0f), PY(22.0f) - PY(19.0f) }, Ink);

    Surface.FillCircle({ PX(12.0f), PY(12.0f) }, (3.0f / 24.0f) * Extent, Ink);
}

void ConstructSplitViewportIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    ColorRgba   Ink,
    ColorRgba   InteriorCutout) noexcept
{
    // ActionIcon.jsx "split": M2 4h20v16H2Zm3 3v10h6V7Zm9 0v10h5V7Z
    const auto PX = [&](float X) noexcept -> float { return TopLeft.X + (X / 24.0f) * Extent; };
    const auto PY = [&](float Y) noexcept -> float { return TopLeft.Y + (Y / 24.0f) * Extent; };

    Surface.FillRectangle({ PX(2.0f), PY(4.0f), PX(22.0f) - PX(2.0f), PY(20.0f) - PY(4.0f) }, Ink, 1.0f);
    Surface.FillRectangle({ PX(5.0f), PY(7.0f), PX(11.0f) - PX(5.0f), PY(17.0f) - PY(7.0f) }, InteriorCutout);
    Surface.FillRectangle({ PX(14.0f), PY(7.0f), PX(19.0f) - PX(14.0f), PY(17.0f) - PY(7.0f) }, InteriorCutout);
}

void ConstructDiagnosticsIcon(PixelSpace& Surface, Point2D TopLeft, float Extent, ColorRgba Ink) noexcept
{
    // ActionIcon.jsx "diagnostics": 3 solid rounded bars (x=2 y=13 w=4 h=9, x=10 y=7 w=4 h=15, x=18 y=2 w=4 h=20)
    const auto PX = [&](float X) noexcept -> float { return TopLeft.X + (X / 24.0f) * Extent; };
    const auto PY = [&](float Y) noexcept -> float { return TopLeft.Y + (Y / 24.0f) * Extent; };
    const float Rx = std::max(0.6f, (1.0f / 24.0f) * Extent);

    Surface.FillRectangle({ PX(2.0f), PY(13.0f), PX(6.0f) - PX(2.0f), PY(22.0f) - PY(13.0f) }, Ink, Rx);
    Surface.FillRectangle({ PX(10.0f), PY(7.0f), PX(14.0f) - PX(10.0f), PY(22.0f) - PY(7.0f) }, Ink, Rx);
    Surface.FillRectangle({ PX(18.0f), PY(2.0f), PX(22.0f) - PX(18.0f), PY(22.0f) - PY(2.0f) }, Ink, Rx);
}

void ConstructViewportSettingsIcon(
    PixelSpace& Surface,
    Point2D     TopLeft,
    float       Extent,
    ColorRgba   Ink,
    ColorRgba   InteriorCutout) noexcept
{
    // ActionIcon.jsx "settings": solid 8-tooth gear + circular center cutout
    const VectorGraphic OuterGear{
        "m9 1 6 0 1 4 3-1 3 5-3 3 3 3-3 5-3-1-1 4H9l-1-4-3 1-3-5 3-3-3-3 3-5 3 1Z",
        24.0f,
        true,
        1.0f,
    };
    ConstructVectorGraphic(Surface, OuterGear, TopLeft, Extent, Ink);
    Surface.FillCircle(
        { TopLeft.X + Extent * 0.5f, TopLeft.Y + Extent * 0.5f },
        (4.6f / 24.0f) * Extent,
        InteriorCutout);
}

void ConstructCapabilityIcon(
    PixelSpace&      Surface,
    Point2D          TopLeft,
    float            Extent,
    CapabilityIconId Capability,
    ColorRgba        Ink,
    ColorRgba        InteriorCutout) noexcept
{
    const auto PX = [&](float X) noexcept -> float { return TopLeft.X + (X / 24.0f) * Extent; };
    const auto PY = [&](float Y) noexcept -> float { return TopLeft.Y + (Y / 24.0f) * Extent; };

    switch (Capability)
    {
        case CapabilityIconId::Visibility:
        {
            // ActionIcon.jsx "visible"
            const VectorGraphic EyeOuter{
                "M1 12C3.7 6.8 7.4 4 12 4C16.6 4 20.3 6.8 23 12C20.3 17.2 16.6 20 12 20C7.4 20 3.7 17.2 1 12Z",
                24.0f,
                true,
                1.0f,
            };
            ConstructVectorGraphic(Surface, EyeOuter, TopLeft, Extent, Ink);
            const Point2D Center{ PX(12.0f), PY(12.0f) };
            Surface.FillCircle(Center, (4.8f / 24.0f) * Extent, InteriorCutout);
            Surface.FillCircle(Center, (2.8f / 24.0f) * Extent, Ink);
            break;
        }

        case CapabilityIconId::ShadowCaster:
        {
            // ActionIcon.jsx "shadow": M3 5h10v10H3z + m13 8 9 9-8 5-8-7h7Z (opacity .4)
            const std::array<Point2D, 5> CastPoly = {{
                { PX(13.0f), PY(8.0f)  },
                { PX(22.0f), PY(17.0f) },
                { PX(14.0f), PY(22.0f) },
                { PX(6.0f),  PY(15.0f) },
                { PX(13.0f), PY(15.0f) },
            }};
            Surface.FillPolygon(CastPoly, ScaleAlpha(Ink, 0.42f));
            Surface.FillRectangle({ PX(3.0f), PY(5.0f), PX(13.0f) - PX(3.0f), PY(15.0f) - PY(5.0f) }, Ink, 1.0f);
            break;
        }

        case CapabilityIconId::DynamicMotion:
        {
            // ActionIcon.jsx "motion": M10 4v16l12-8Z + 3 speed bars
            const std::array<Point2D, 3> Wedge = {{
                { PX(10.0f), PY(4.0f)  },
                { PX(22.0f), PY(12.0f) },
                { PX(10.0f), PY(20.0f) },
            }};
            Surface.FillPolygon(Wedge, Ink);
            Surface.FillRectangle({ PX(1.0f), PY(6.0f), PX(7.0f) - PX(1.0f), PY(9.0f) - PY(6.0f) }, Ink, 0.6f);
            Surface.FillRectangle({ PX(0.0f), PY(11.0f), PX(7.0f) - PX(0.0f), PY(13.0f) - PY(11.0f) }, Ink, 0.6f);
            Surface.FillRectangle({ PX(1.0f), PY(15.0f), PX(7.0f) - PX(1.0f), PY(18.0f) - PY(15.0f) }, Ink, 0.6f);
            break;
        }

        case CapabilityIconId::TransformLock:
        {
            // ActionIcon.jsx "lock": rect x=5 y=10 w=14 h=12 rx=3 + thick shackle
            const float ShackleStroke = std::max(1.6f, (2.6f / 24.0f) * Extent);
            Surface.StrokeCircle({ PX(12.0f), PY(8.0f) }, (3.6f / 24.0f) * Extent, Ink, ShackleStroke);
            Surface.FillRectangle(
                { PX(5.0f), PY(10.0f), PX(19.0f) - PX(5.0f), PY(22.0f) - PY(10.0f) },
                Ink,
                (3.0f / 24.0f) * Extent);
            break;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      SEMANTIC INDICATORS, SIGNS, AND SYMBOLS
//------------------------------------------------------------------------------------------------------------------------

void ConstructVisibilityIndicator(
    PixelSpace& Surface,
    Point2D     Center,
    bool        Visible,
    float       Opacity) noexcept
{
    if (Visible)
    {
        VisualToken Disc{};
        Disc.Shape     = VisualTokenShape::Circle;
        Disc.FillColor = WorkspaceSurfaceColors::StatusGreen;
        Disc.Extent    = 15.6f;
        ConstructVisualToken(Surface, Center, Disc, Opacity);
        ConstructCheckSymbol(Surface, Center, 8.0f, ScaleAlpha(WorkspaceSurfaceColors::StatusGreenInk, Opacity), 1.8f);
    }
    else
    {
        VisualToken Disc{};
        Disc.Shape       = VisualTokenShape::Circle;
        Disc.FillColor   = { 42, 42, 42, 255 };
        Disc.StrokeColor = { 76, 76, 76, 255 };
        Disc.Extent      = 14.0f;
        Disc.StrokeWidth = 1.0f;
        ConstructVisualToken(Surface, Center, Disc, Opacity);
    }
}

void ConstructVerticalScrollIndicator(
    PixelSpace& Surface,
    Rectangle2D ViewportBounds,
    float       ContentHeight,
    float       ScrollOffset,
    Point2D     Cursor) noexcept
{
    if (ContentHeight <= ViewportBounds.Height + 1.0f || ViewportBounds.Height <= 24.0f)
    {
        return;
    }

    const float       TrackWidth  = 5.0f;
    const float       TrackMargin = 3.0f;
    const Rectangle2D TrackBounds{
        ViewportBounds.Right() - TrackWidth - TrackMargin,
        ViewportBounds.Y + 4.0f,
        TrackWidth,
        ViewportBounds.Height - 8.0f,
    };

    const float VisibleRatio = std::clamp(ViewportBounds.Height / ContentHeight, 0.08f, 1.0f);
    const float ThumbHeight  = std::max(24.0f, TrackBounds.Height * VisibleRatio);
    const float MaxScroll    = std::max(1.0f, ContentHeight - ViewportBounds.Height);
    const float Normalized   = std::clamp(ScrollOffset / MaxScroll, 0.0f, 1.0f);
    const float ThumbY       = TrackBounds.Y + (TrackBounds.Height - ThumbHeight) * Normalized;

    const Rectangle2D ThumbBounds{ TrackBounds.X, ThumbY, TrackWidth, ThumbHeight };
    const bool        Hovered = ThumbBounds.Contains(Cursor) || TrackBounds.Contains(Cursor);

    Surface.FillRectangle(TrackBounds, { 255, 255, 255, 10 }, 2.5f);
    Surface.FillRectangle(ThumbBounds, Hovered ? ColorRgba{ 138, 138, 138, 195 } : ColorRgba{ 98, 98, 98, 145 }, 2.5f);
}

void ConstructCautionTriangleSign(
    PixelSpace& Surface,
    Point2D     Center,
    float       Extent,
    ColorRgba   Ink) noexcept
{
    const float Half = Extent * 0.5f;
    const std::array<Point2D, 3> Triangle = {{
        { Center.X,               Center.Y - Half * 0.88f },
        { Center.X + Half * 0.95f, Center.Y + Half * 0.78f },
        { Center.X - Half * 0.95f, Center.Y + Half * 0.78f },
    }};
    Surface.StrokePolyline(Triangle, Ink, 1.35f, true);
}

void ConstructOrientationAxesSign(
    PixelSpace&              Surface,
    Rectangle2D              PaneBounds,
    ViewportProjectionPreset Projection) noexcept
{
    const Point2D   TriadOrigin{ PaneBounds.Right() - 36.0f, PaneBounds.Bottom() - 28.0f };
    constexpr float AxisLength = 19.0f;

    const ColorRgba RedAxis{ 226, 86, 86, 235 };
    const ColorRgba GreenAxis{ 94, 201, 118, 235 };
    const ColorRgba BlueAxis{ 88, 152, 238, 235 };

    Point2D AxisX{ TriadOrigin.X + AxisLength * 0.88f, TriadOrigin.Y + AxisLength * 0.34f };
    Point2D AxisY{ TriadOrigin.X - AxisLength * 0.76f, TriadOrigin.Y + AxisLength * 0.42f };
    Point2D AxisZ{ TriadOrigin.X, TriadOrigin.Y - AxisLength };

    if (Projection == ViewportProjectionPreset::Top)
    {
        AxisX = { TriadOrigin.X + AxisLength, TriadOrigin.Y };
        AxisY = { TriadOrigin.X, TriadOrigin.Y - AxisLength };
        AxisZ = { TriadOrigin.X - AxisLength * 0.45f, TriadOrigin.Y + AxisLength * 0.45f };
    }
    else if (Projection == ViewportProjectionPreset::Front)
    {
        AxisX = { TriadOrigin.X + AxisLength, TriadOrigin.Y };
        AxisZ = { TriadOrigin.X, TriadOrigin.Y - AxisLength };
        AxisY = { TriadOrigin.X - AxisLength * 0.45f, TriadOrigin.Y + AxisLength * 0.45f };
    }
    else if (Projection == ViewportProjectionPreset::Right)
    {
        AxisY = { TriadOrigin.X + AxisLength, TriadOrigin.Y };
        AxisZ = { TriadOrigin.X, TriadOrigin.Y - AxisLength };
        AxisX = { TriadOrigin.X - AxisLength * 0.45f, TriadOrigin.Y + AxisLength * 0.45f };
    }

    Surface.StrokeLine(TriadOrigin, AxisX, RedAxis, 1.7f);
    Surface.StrokeLine(TriadOrigin, AxisY, GreenAxis, 1.7f);
    Surface.StrokeLine(TriadOrigin, AxisZ, BlueAxis, 1.7f);

    Surface.FillCircle(AxisX, 3.0f, RedAxis);
    Surface.FillCircle(AxisY, 3.0f, GreenAxis);
    Surface.FillCircle(AxisZ, 3.0f, BlueAxis);
    Surface.FillCircle(TriadOrigin, 2.4f, { 210, 210, 210, 255 });

    ConstructWeightedText(Surface, FontWeightClass::Medium, { AxisX.X + 4.0f, AxisX.Y - 6.0f }, 9.0f, RedAxis, "X");
    ConstructWeightedText(Surface, FontWeightClass::Medium, { AxisY.X - 10.0f, AxisY.Y - 5.0f }, 9.0f, GreenAxis, "Y");
    ConstructWeightedText(Surface, FontWeightClass::Medium, { AxisZ.X - 3.0f, AxisZ.Y - 14.0f }, 9.0f, BlueAxis, "Z");
}

void ConstructCheckSymbol(
    PixelSpace& Surface,
    Point2D     Center,
    float       Extent,
    ColorRgba   Ink,
    float       Thickness) noexcept
{
    const float R = Extent * 0.5f;
    const std::array<Point2D, 3> Check = {{
        { Center.X - R * 0.85f, Center.Y + R * 0.05f },
        { Center.X - R * 0.18f, Center.Y + R * 0.72f },
        { Center.X + R * 0.95f, Center.Y - R * 0.65f },
    }};
    Surface.StrokePolyline(Check, Ink, Thickness, false);
}

void ConstructChevronSymbol(
    PixelSpace& Surface,
    Point2D     Center,
    float       Extent,
    bool        Expanded,
    ColorRgba   Ink) noexcept
{
    const float R = Extent * 0.36f;
    if (Expanded)
    {
        const std::array<Point2D, 3> Down = {{
            { Center.X - R, Center.Y - R * 0.45f },
            { Center.X,     Center.Y + R * 0.55f },
            { Center.X + R, Center.Y - R * 0.45f },
        }};
        Surface.StrokePolyline(Down, Ink, 1.5f, false);
    }
    else
    {
        const std::array<Point2D, 3> Right = {{
            { Center.X - R * 0.45f, Center.Y - R },
            { Center.X + R * 0.55f, Center.Y     },
            { Center.X - R * 0.45f, Center.Y + R },
        }};
        Surface.StrokePolyline(Right, Ink, 1.5f, false);
    }
}

void ConstructResetSymbol(
    PixelSpace& Surface,
    Point2D     Center,
    float       Extent,
    ColorRgba   Ink,
    float       Thickness) noexcept
{
    const float R          = Extent * 0.42f;
    constexpr float PiVal  = 3.14159265f;
    constexpr float StartA = -0.78f * PiVal;
    constexpr float EndA   =  0.88f * PiVal;
    std::array<Point2D, 15> Arc{};
    for (std::size_t Step = 0u; Step < Arc.size(); ++Step)
    {
        const float T     = static_cast<float>(Step) / static_cast<float>(Arc.size() - 1u);
        const float Angle = StartA + T * (EndA - StartA);
        Arc[Step]         = { Center.X + std::cos(Angle) * R, Center.Y + std::sin(Angle) * R };
    }
    Surface.StrokePolyline(Arc, Ink, Thickness, false);

    // Counterclockwise arrowhead at StartA (upper-left)
    const Point2D Tip = Arc.front();
    const std::array<Point2D, 3> Head = {{
        { Tip.X + R * 0.08f, Tip.Y - R * 0.62f },
        Tip,
        { Tip.X + R * 0.68f, Tip.Y + R * 0.08f },
    }};
    Surface.StrokePolyline(Head, Ink, Thickness, false);
}

} // namespace Frontier::WorkspaceAppearance

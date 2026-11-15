/*============================================================================================================================================
                                                           WORKSPACEAPPEARANCE.CPP
============================================================================================================================================*/
// 🧩 Shared surface color tokens, VisualToken, VectorGraphic, Workspace icons, indicators, signs, and symbols

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"
#include "Engine/Input/InputExtension.h"

#include "imgui.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

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

struct PointerOwnershipRegister
{
    const void* ActiveOwnerPane         = nullptr;
    bool        ExternalDragSuppression = false;
    uint32_t    OwnerStaleEnforceTicks  = 0u;
    Rectangle2D PrimaryOverlayShield    {};
    Rectangle2D SecondaryOverlayShield  {};
};

[[nodiscard]] PointerOwnershipRegister& AccessPointerOwnershipRegister() noexcept
{
    static PointerOwnershipRegister CoordinatorRegister{};
    return CoordinatorRegister;
}

[[nodiscard]] bool QueryOverlayShieldContains(Point2D PhysicalCursor) noexcept
{
    const auto& Reg = AccessPointerOwnershipRegister();
    if (Reg.PrimaryOverlayShield.Width > 0.5f &&
        Reg.PrimaryOverlayShield.Height > 0.5f &&
        Reg.PrimaryOverlayShield.Contains(PhysicalCursor))
    {
        return true;
    }
    if (Reg.SecondaryOverlayShield.Width > 0.5f &&
        Reg.SecondaryOverlayShield.Height > 0.5f &&
        Reg.SecondaryOverlayShield.Contains(PhysicalCursor))
    {
        return true;
    }
    return false;
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
    Reading.ShortcutUnwrap    = Snapshot.ControlHeld && !Snapshot.ShiftHeld && !Snapshot.AltHeld &&
                                Snapshot.QueryKeyPressed(Input::InputLogicalKey::U, false);
    Reading.ShortcutUndo      = Snapshot.ControlHeld && !Snapshot.ShiftHeld && !Snapshot.AltHeld &&
                                Snapshot.QueryKeyPressed(Input::InputLogicalKey::Z, false);
    Reading.ShortcutRedo      = Snapshot.ControlHeld && !Snapshot.AltHeld &&
                                ((Snapshot.ShiftHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::Z, false)) ||
                                 (!Snapshot.ShiftHeld && Snapshot.QueryKeyPressed(Input::InputLogicalKey::Y, false)));
    Reading.ToggleDiagnostics = Snapshot.QueryKeyPressed(Input::InputLogicalKey::F3, false);
    Reading.KeyboardFocused   = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    for (const uint32_t Codepoint : Snapshot.QueryTypedCodepoints())
    {
        EncodeUtf8Codepoint(Codepoint, Reading.TypedUtf8);
    }

    const auto& Reg = AccessPointerOwnershipRegister();
    if (Reg.ActiveOwnerPane == nullptr &&
        !Reg.ExternalDragSuppression &&
        Snapshot.PointerSeeded &&
        QueryOverlayShieldContains(ScreenCursor))
    {
        SuppressPanePointerInput(Reading);
    }

    return Reading;
}

void SuppressPanePointerInput(PaneInputReading& InOutInput) noexcept
{
    InOutInput.PrimaryPointer.X        = -1.0e6f;
    InOutInput.PrimaryPointer.Y        = -1.0e6f;
    InOutInput.PrimaryPointer.Enabled  = false;
    InOutInput.PrimaryPointer.Pressed  = false;
    InOutInput.PrimaryPointer.Down     = false;
    InOutInput.PrimaryPointer.Released = false;
    InOutInput.SecondaryPressed        = false;
    InOutInput.SecondaryDown           = false;
    InOutInput.SecondaryReleased       = false;
    InOutInput.MiddlePressed           = false;
    InOutInput.MiddleDown              = false;
    InOutInput.MiddleReleased          = false;
    InOutInput.DoubleClicked           = false;
    InOutInput.WheelDeltaY             = 0.0f;
}

const void* QueryActivePointerOwner() noexcept
{
    return AccessPointerOwnershipRegister().ActiveOwnerPane;
}

bool QueryPointerBlockedForPane(const void* PaneIdentity) noexcept
{
    const auto& Reg = AccessPointerOwnershipRegister();
    return Reg.ActiveOwnerPane != nullptr && Reg.ActiveOwnerPane != PaneIdentity;
}

void AssignExternalDragSuppression(bool ExternalDragActive) noexcept
{
    AccessPointerOwnershipRegister().ExternalDragSuppression = ExternalDragActive;
}

void AssignOverlayPointerShield(Rectangle2D PrimaryPhysicalBounds, Rectangle2D SecondaryPhysicalBounds) noexcept
{
    auto& Reg                  = AccessPointerOwnershipRegister();
    Reg.PrimaryOverlayShield   = PrimaryPhysicalBounds;
    Reg.SecondaryOverlayShield = SecondaryPhysicalBounds;
}

void ClearOverlayPointerShield() noexcept
{
    auto& Reg                  = AccessPointerOwnershipRegister();
    Reg.PrimaryOverlayShield   = {};
    Reg.SecondaryOverlayShield = {};
}

void EnforcePanePointerOwnership(const void* PaneIdentity, PaneInputReading& InOutInput) noexcept
{
    auto& Reg = AccessPointerOwnershipRegister();
    if (Reg.ActiveOwnerPane == nullptr)
    {
        return;
    }
    if (Reg.ActiveOwnerPane == PaneIdentity)
    {
        Reg.OwnerStaleEnforceTicks        = 0u;
        InOutInput.PrimaryPointer.Enabled = true;
        return;
    }

    const auto& RawSnapshot = Input::InputExtension::InspectActiveEditorSnapshot();
    const bool  RawButtonsDown =
        RawSnapshot.QueryButtonDown(0u) ||
        RawSnapshot.QueryButtonDown(1u) ||
        RawSnapshot.QueryButtonDown(2u);
    if (!RawButtonsDown)
    {
        ++Reg.OwnerStaleEnforceTicks;
        if (Reg.OwnerStaleEnforceTicks > 16u)
        {
            Reg.ActiveOwnerPane        = nullptr;
            Reg.OwnerStaleEnforceTicks = 0u;
            return;
        }
    }
    else
    {
        Reg.OwnerStaleEnforceTicks = 0u;
    }

    SuppressPanePointerInput(InOutInput);
}

void UpdatePanePointerOwnership(
    const void*             PaneIdentity,
    Rectangle2D             ScreenBounds,
    float                   ScaleFactor,
    const PaneInputReading& Input,
    bool                    ActiveDragCaptured) noexcept
{
    if (PaneIdentity == nullptr)
    {
        return;
    }

    auto&       Reg         = AccessPointerOwnershipRegister();
    const float SafeScale   = std::max(0.25f, ScaleFactor);
    const Point2D PhysicalCursor{
        ScreenBounds.X + Input.PrimaryPointer.X * SafeScale,
        ScreenBounds.Y + Input.PrimaryPointer.Y * SafeScale,
    };
    const bool AnyButtonDown = Input.PrimaryPointer.Down || Input.MiddleDown || Input.SecondaryDown;

    if (Reg.ActiveOwnerPane == PaneIdentity)
    {
        Reg.OwnerStaleEnforceTicks = 0u;
        if (!ActiveDragCaptured && !AnyButtonDown)
        {
            Reg.ActiveOwnerPane = nullptr;
        }
        return;
    }

    if (Reg.ActiveOwnerPane == nullptr && !Reg.ExternalDragSuppression)
    {
        if (ActiveDragCaptured || (ScreenBounds.Contains(PhysicalCursor) && AnyButtonDown))
        {
            Reg.ActiveOwnerPane        = PaneIdentity;
            Reg.OwnerStaleEnforceTicks = 0u;
        }
    }
}

void ReleasePanePointerOwnership(const void* PaneIdentity) noexcept
{
    auto& Reg = AccessPointerOwnershipRegister();
    if (Reg.ActiveOwnerPane == PaneIdentity)
    {
        Reg.ActiveOwnerPane        = nullptr;
        Reg.OwnerStaleEnforceTicks = 0u;
    }
}

void ResetAllPanePointerOwnership() noexcept
{
    auto& Reg = AccessPointerOwnershipRegister();
    Reg.ActiveOwnerPane         = nullptr;
    Reg.ExternalDragSuppression = false;
    Reg.OwnerStaleEnforceTicks  = 0u;
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
        case ConstructSlotCategory::GeometryVolume:
            return { 56, 156, 255, 255 };  // #389CFF (editor-cube)
        case ConstructSlotCategory::LayerGroup:
            return { 192, 174, 212, 255 }; // #C0AED4 (collection-stacked-layers)
        case ConstructSlotCategory::PointLuminaire:
            return { 255, 205, 56, 255 };  // #FFCD38 (editor-point-light)
        case ConstructSlotCategory::OpticsCamera:
            return { 160, 164, 182, 255 }; // #A0A4B6 (camera)
        case ConstructSlotCategory::ForceField:
            return { 104, 220, 255, 255 }; // #68DCFF (aerodynamic cyan streamline)
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
        case ConstructSlotCategory::GeometryVolume:
            return "Type 1";
        case ConstructSlotCategory::LayerGroup:
            return "Type 2";
        case ConstructSlotCategory::PointLuminaire:
            return "Type 3";
        case ConstructSlotCategory::OpticsCamera:
            return "Type 4";
        case ConstructSlotCategory::ForceField:
            return "Force Field";
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

        case ConstructSlotCategory::GeometryVolume:
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

        case ConstructSlotCategory::LayerGroup:
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

        case ConstructSlotCategory::PointLuminaire:
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

        case ConstructSlotCategory::OpticsCamera:
            ConstructCameraIcon(Surface, TopLeft, Extent, Opacity);
            break;

        case ConstructSlotCategory::ForceField:
        {
            const ColorRgba StreamInk = ScaleAlpha({ 104, 220, 255, 255 }, Opacity);
            const ColorRgba CrestInk  = ScaleAlpha({ 255, 180, 84, 255 }, Opacity);
            const float     StrokeW   = std::max(1.4f, (13.0f / 256.0f) * Extent);

            const std::array<Point2D, 4> TopArc = {{
                { ProjectX(34.0f),  ProjectY(82.0f)  },
                { ProjectX(96.0f),  ProjectY(54.0f)  },
                { ProjectX(162.0f), ProjectY(92.0f)  },
                { ProjectX(218.0f), ProjectY(68.0f)  },
            }};
            const std::array<Point2D, 4> MidStream = {{
                { ProjectX(24.0f),  ProjectY(128.0f) },
                { ProjectX(104.0f), ProjectY(128.0f) },
                { ProjectX(158.0f), ProjectY(128.0f) },
                { ProjectX(226.0f), ProjectY(128.0f) },
            }};
            const std::array<Point2D, 4> BottomArc = {{
                { ProjectX(34.0f),  ProjectY(176.0f) },
                { ProjectX(96.0f),  ProjectY(202.0f) },
                { ProjectX(162.0f), ProjectY(164.0f) },
                { ProjectX(210.0f), ProjectY(186.0f) },
            }};
            Surface.StrokePolyline(TopArc, StreamInk, StrokeW, false);
            Surface.StrokePolyline(MidStream, CrestInk, StrokeW, false);
            Surface.StrokePolyline(BottomArc, StreamInk, StrokeW, false);

            const std::array<Point2D, 3> ArrowTip = {{
                { ProjectX(198.0f), ProjectY(104.0f) },
                { ProjectX(234.0f), ProjectY(128.0f) },
                { ProjectX(198.0f), ProjectY(152.0f) },
            }};
            Surface.FillPolygon(ArrowTip, CrestInk);
            break;
        }
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

void ConstructComponentModeIcon(PixelSpace& Surface, Point2D Origin, float Extent, uint32_t ModeIdx, ColorRgba Ink) noexcept
{
    const float L = Origin.X + Extent * 0.18f;
    const float R = Origin.X + Extent * 0.82f;
    const float T = Origin.Y + Extent * 0.18f;
    const float B = Origin.Y + Extent * 0.82f;
    if (ModeIdx == 0u) // Vertex
    {
        Surface.FillCircle({ L, T }, 1.9f, Ink);
        Surface.FillCircle({ R, T }, 1.9f, Ink);
        Surface.FillCircle({ L, B }, 1.9f, Ink);
        Surface.FillCircle({ R, B }, 1.9f, Ink);
    }
    else if (ModeIdx == 1u) // Edge
    {
        Surface.StrokeLine({ L, B }, { R, T }, Ink, 1.6f);
        Surface.FillCircle({ L, B }, 1.8f, Ink);
        Surface.FillCircle({ R, T }, 1.8f, Ink);
    }
    else if (ModeIdx == 2u) // Face
    {
        ColorRgba Soft = Ink;
        Soft.Alpha     = 72u;
        Surface.FillRectangle({ L, T, R - L, B - T }, Soft, 1.5f);
        Surface.StrokeRectangle({ L, T, R - L, B - T }, Ink, 1.5f, 1.3f);
    }
    else // Island
    {
        Surface.StrokeRectangle({ Origin.X + Extent * 0.12f, Origin.Y + Extent * 0.14f, Extent * 0.42f, Extent * 0.42f }, Ink, 1.5f, 1.25f);
        Surface.StrokeRectangle({ Origin.X + Extent * 0.46f, Origin.Y + Extent * 0.44f, Extent * 0.42f, Extent * 0.42f }, Ink, 1.5f, 1.25f);
    }
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

ColorRgba QueryForceModelTint(uint32_t LawOrdinal) noexcept
{
    static constexpr std::array<ColorRgba, 19> LawTints = {{
        { 137, 224, 196, 255 }, // 0 LinearWind
        { 112, 207, 151, 255 }, // 1 Buoyancy
        { 224, 131, 104, 255 }, // 2 RadialBurst
        { 142, 182, 224, 255 }, // 3 Attractor
        { 217, 158, 108, 255 }, // 4 Repulsor
        { 123, 198, 219, 255 }, // 5 AxisAttractor
        { 129, 199, 132, 255 }, // 6 Containment
        { 185, 169, 105, 255 }, // 7 RotationalColumn
        { 200, 176, 116, 255 }, // 8 Torque
        { 161, 152, 204, 255 }, // 9 Drag
        { 217, 181, 108, 255 }, // 10 CurlNoise
        { 116, 198, 184, 255 }, // 11 AtmosphericGust
        { 112, 207, 190, 255 }, // 12 VorticityConfinement
        { 209, 194, 122, 255 }, // 13 BrownianNoise
        { 116, 200, 224, 255 }, // 14 LorentzField
        { 198, 139, 217, 255 }, // 15 MagneticDipole
        { 100, 181, 246, 255 }, // 16 Stream
        { 157, 180, 226, 255 }, // 17 SplineForce
        { 142, 199, 158, 255 }, // 18 BoundaryAdhesion
    }};
    return (LawOrdinal < LawTints.size()) ? LawTints[LawOrdinal] : LawTints[0];
}

void ConstructForceModelIcon(
    PixelSpace& Surface,
    uint32_t    LawOrdinal,
    Point2D     TopLeft,
    float       Extent,
    ColorRgba   AccentInk,
    float       Opacity) noexcept
{
    const ColorRgba Ink    = ScaleAlpha(AccentInk, Opacity);
    const float     Scale  = Extent / 16.0f;
    const float     Stroke = std::max(1.0f, 1.4f * Scale);

    auto P = [&](float X, float Y) noexcept -> Point2D
    {
        return { TopLeft.X + X * Scale, TopLeft.Y + Y * Scale };
    };
    auto StrokeSvg16 = [&](std::string_view Svg) noexcept
    {
        VectorIconRepository::StrokeSvgPath(
            Surface,
            Svg,
            VectorIconPlacement{ TopLeft.X, TopLeft.Y, Extent, Ink, 1.4f },
            16.0f);
    };
    auto StrokeDashedRing = [&](float Cx, float Cy, float Radius, int DashCount) noexcept
    {
        constexpr float TwoPi = 6.2831853f;
        const float     Step  = TwoPi / static_cast<float>(DashCount);
        for (int Seg = 0; Seg < DashCount; ++Seg)
        {
            const float A0 = static_cast<float>(Seg) * Step;
            const float A1 = A0 + Step * 0.54f;
            const std::array<Point2D, 3> Arc = {{
                P(Cx + Radius * std::cos(A0), Cy + Radius * std::sin(A0)),
                P(Cx + Radius * std::cos(0.5f * (A0 + A1)), Cy + Radius * std::sin(0.5f * (A0 + A1))),
                P(Cx + Radius * std::cos(A1), Cy + Radius * std::sin(A1)),
            }};
            Surface.StrokePolyline(Arc, Ink, Stroke, false);
        }
    };

    switch (LawOrdinal)
    {
        case 0u:
            StrokeSvg16("M2 6h8.5a2 2 0 1 0-1.9-2.6M2 10h11a1.8 1.8 0 1 1-1.6 2.5M2 13h6.5a1.5 1.5 0 1 1-1.3 2.2");
            break;
        case 1u:
            StrokeSvg16("M8 14V3M4.8 6.2L8 3l3.2 3.2");
            Surface.StrokeCircle(P(8.0f, 11.5f), 1.7f * Scale, Ink, Stroke);
            break;
        case 2u:
            Surface.StrokeCircle(P(8.0f, 8.0f), 2.2f * Scale, Ink, Stroke);
            StrokeDashedRing(8.0f, 8.0f, 5.5f, 8);
            StrokeSvg16("M8 1v2M8 13v2M1 8h2M13 8h2");
            break;
        case 3u:
            Surface.FillCircle(P(8.0f, 8.0f), 1.8f * Scale, Ink);
            StrokeSvg16("M2.5 2.5L5.8 5.8M13.5 2.5l-3.3 3.3M2.5 13.5l3.3-3.3M13.5 13.5l-3.3-3.3");
            break;
        case 4u:
            Surface.StrokeCircle(P(8.0f, 8.0f), 1.8f * Scale, Ink, Stroke);
            StrokeSvg16("M5.5 5.5L2.2 2.2M10.5 5.5l3.3-3.3M5.5 10.5l-3.3 3.3M10.5 10.5l3.3 3.3M2.2 4.5V2.2h2.3M13.8 4.5V2.2h-2.3");
            break;
        case 5u:
            StrokeSvg16("M3 13L13 3M4.5 5.5l2.5 2.5M11.5 10.5L9 8");
            break;
        case 6u:
            Surface.StrokeCircle(P(8.0f, 8.0f), 6.0f * Scale, Ink, Stroke);
            StrokeDashedRing(8.0f, 8.0f, 3.0f, 6);
            StrokeSvg16("M8 2v2M8 12v2M2 8h2M12 8h2");
            break;
        case 7u:
            StrokeSvg16("M8 8a1.2 1.2 0 1 1-1.2-1.2a2.4 2.4 0 0 1 2.4 2.4A3.8 3.8 0 0 1 4 8a5.2 5.2 0 0 1 10.2-1");
            break;
        case 8u:
            StrokeSvg16("M12.8 6.2A5.2 5.2 0 1 0 11.5 12M10.2 6.5h3.2V3.3");
            Surface.StrokeCircle(P(8.0f, 8.0f), 1.4f * Scale, Ink, Stroke);
            break;
        case 9u:
            StrokeSvg16("M5.5 3v10M10.5 3v10M2 8h3.5M14 8h-3.5");
            break;
        case 10u:
            StrokeSvg16("M2.5 7.5a2.8 2.8 0 1 1 5.2 1.4a2.8 2.8 0 1 0 5.6 1M11.8 5.2a2 2 0 0 0-3-.2");
            break;
        case 11u:
            StrokeSvg16("M2 5.5h8a1.9 1.9 0 1 0-1.7-2.6M2 10h9.5a2.3 2.3 0 1 1-1.9 3.2M10.5 3.5a2 2 0 0 1 3 1.5");
            break;
        case 12u:
            Surface.StrokeCircle(P(6.5f, 9.5f), 3.0f * Scale, Ink, Stroke);
            StrokeSvg16("M11 4.5l2.5-2M11 2.5h2.5V5M3 5c1.8-1.6 4.5-1.6 6.2 0");
            break;
        case 13u:
            StrokeSvg16("M2.5 11.5L5 7l2.2 3.2L10 4.5l2 4l2-2.5");
            Surface.FillCircle(P(2.5f, 11.5f), 1.0f * Scale, Ink);
            Surface.FillCircle(P(14.0f, 6.0f), 1.0f * Scale, Ink);
            break;
        case 14u:
            StrokeSvg16("M2.5 9.5c1.5-4.5 3.5-4.5 4.5 0s3 4.5 4.5 0s1.8-4.5 2.5 0M2 4h12M11.8 2.2L14 4l-2.2 1.8");
            break;
        case 15u:
            StrokeSvg16("M6.5 4.5C3 4.5 2 8 2 8s1 3.5 4.5 3.5M9.5 4.5C13 4.5 14 8 14 8s-1 3.5-4.5 3.5");
            Surface.StrokeRectangle({ TopLeft.X + 6.6f * Scale, TopLeft.Y + 4.5f * Scale, 2.8f * Scale, 7.0f * Scale }, Ink, 1.0f * Scale, Stroke);
            break;
        case 16u:
            StrokeSvg16("M2 5c3-2 6 2 9 0l2-1M2 9c3-2 6 2 9 0l2-1M2 13c3-2 6 2 9 0l2-1M11.5 3.2L14 4l-.8 2.4");
            break;
        case 17u:
            StrokeSvg16("M2 12.5C5 12.5 5 3.5 8 3.5s3 9 6 9M12.2 10.5L14 12.5l-2 1.8");
            break;
        case 18u:
            StrokeSvg16("M2 12.5h12M4 5c2.5 4.5 5.5 4.5 8 0M8 3.5v6.5M6 8l2 2l2-2");
            break;
        default:
            Surface.StrokeCircle(P(8.0f, 8.0f), 4.0f * Scale, Ink, Stroke);
            break;
    }
}

void ConstructSectionHeading(
    PixelSpace&      Surface,
    float            LeftX,
    float            TopY,
    float            Width,
    std::string_view Title,
    std::string_view RightTag,
    ControlIcon      Icon,
    ColorRgba        IconTint) noexcept
{
    const ControlColorSet& Colors = ControlAppearance::Colors();
    const bool             HasIcon = (Icon != ControlIcon::Count);
    if (HasIcon)
    {
        VectorIconRepository::ConstructControl(
            Surface, Icon, VectorIconPlacement{ LeftX, TopY + 1.0f, 14.0f, IconTint, 1.8f });
    }
    else
    {
        Surface.FillRectangle({ LeftX, TopY + 2.0f, 3.0f, 14.0f }, Colors.Accent.ToRgba8(), 1.5f);
    }

    ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + (HasIcon ? 21.0f : 10.0f), TopY },
        12.0f,
        Colors.Text.ToRgba8(),
        Title);

    if (!RightTag.empty())
    {
        const float       TagW    = MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.5f, RightTag) + 12.0f;
        const Rectangle2D TagRect { LeftX + Width - TagW, TopY - 1.0f, TagW, 18.0f };
        const ColorQuad   WellCol = ColorQuad::Blend(Colors.Field, Colors.Panel, 0.35f);
        const ColorQuad   EdgeCol = ColorQuad::Blend(Colors.StrokeStrong, Colors.Text, 0.14f);
        Surface.FillRectangle(TagRect, WellCol.ToRgba8(), 9.0f);
        Surface.StrokeRectangle(TagRect, EdgeCol.ToRgba8(), 9.0f, 1.0f);
        ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { TagRect.X + 6.0f, TagRect.Y + 3.5f },
            8.5f,
            Colors.TextDim.ToRgba8(),
            RightTag);
    }
}

bool ConstructCapsuleToggleRow(
    PixelSpace&            Surface,
    Rectangle2D            RowRect,
    std::string_view       Label,
    bool&                  InOutEnabled,
    Point2D                Cursor,
    bool                   Pressed,
    const OptionalTooltip& Tooltip,
    std::string_view       DetailFirstLine,
    std::string_view       DetailSecondLine) noexcept
{
    const ControlColorSet& Colors = ControlAppearance::Colors();
    ConstructWeightedText(
        Surface, FontWeightClass::Regular, { RowRect.X, RowRect.Y }, 11.5f, Colors.Text.ToRgba8(), Label);
    if (!DetailFirstLine.empty())
    {
        ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { RowRect.X, RowRect.Y + 16.0f }, RowRect.Width - 54.0f,
            9.5f, Colors.TextDim.ToRgba8(), DetailFirstLine);
        if (!DetailSecondLine.empty())
        {
            ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { RowRect.X, RowRect.Y + 27.0f }, RowRect.Width - 54.0f,
                9.5f, Colors.TextDim.ToRgba8(), DetailSecondLine);
        }
    }

    const Rectangle2D TrackCapsule{ RowRect.Right() - 44.0f, RowRect.Y + 3.0f, 44.0f, 25.0f };
    const bool        Hovered = RowRect.Contains(Cursor);
    if (Hovered)
    {
        std::string     TitleStorage{};
        OptionalTooltip Effective = Tooltip;
        if (!Effective.Enabled)
        {
            TitleStorage.assign(Label);
            Effective = OptionalTooltip::FromTitled(TitleStorage.c_str(), "Toggle this workspace setting.");
        }
        ControlAppearance::RecordHoveredTooltip(RowRect, Effective);
    }

    Surface.FillRectangle(
        TrackCapsule, InOutEnabled ? ColorRgba{ 255, 255, 255, 255 } : ColorRgba{ 35, 35, 35, 255 }, 13.0f);
    Surface.StrokeRectangle(
        TrackCapsule, InOutEnabled ? ColorRgba{ 255, 255, 255, 255 } : ColorRgba{ 255, 255, 255, 26 }, 13.0f, 1.0f);
    const float KnobX = InOutEnabled ? (TrackCapsule.Right() - 13.0f) : (TrackCapsule.X + 13.0f);
    Surface.FillCircle(
        { KnobX, TrackCapsule.Y + 12.5f }, 9.5f,
        InOutEnabled ? ColorRgba{ 17, 17, 17, 255 } : ColorRgba{ 188, 188, 188, 255 });

    if (Hovered && Pressed)
    {
        InOutEnabled = !InOutEnabled;
        return true;
    }
    return false;
}

} // namespace Frontier::WorkspaceAppearance

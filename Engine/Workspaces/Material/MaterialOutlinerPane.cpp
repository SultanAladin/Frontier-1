/*============================================================================================================================================
                                                         MATERIALOUTLINERPANE.CPP
============================================================================================================================================*/
// 🧩 Implementation of MaterialOutlinerPane: 28 OpenPBR 1.1 channels in 6 collapsible groups + bottom "+ Enable | Preset | Reset · Solo | Mute".

#include "Engine/Workspaces/Material/MaterialOutlinerPane.h"

#include "Engine/Components/Controls/ControlAppearance.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

using namespace TexturingAppearance;

namespace
{

constexpr std::array<const char*, MaterialOutlinerPane::GroupCount> GroupLabels {{
    "BASE SURFACE",
    "MICROSURFACE",
    "COAT & FILM",
    "SHEEN & FUZZ",
    "TRANSMISSION & SSS",
    "EMISSION & OPACITY",
}};

constexpr std::array<MaterialChannelEntrySpec, MaterialOutlinerPane::ChannelCount> ChannelCatalog {{
    // 0. Base Surface (7)
    {  0u, MaterialChannelGroup::BaseSurface,     "Colour",         "Base albedo tint",       true,  { 185u, 122u,  69u, 255u }, 1.00f, "%"   },
    {  1u, MaterialChannelGroup::BaseSurface,     "Diffuse Rough",  "Oren-Nayar roughness",   false, { 160u, 140u, 120u, 255u }, 0.25f, "%"   },
    {  2u, MaterialChannelGroup::BaseSurface,     "Metalness",      "Conductor blend",        false, { 145u, 155u, 170u, 255u }, 0.10f, "%"   },
    {  3u, MaterialChannelGroup::BaseSurface,     "Specular",       "Dielectric F0 weight",   false, { 210u, 215u, 225u, 255u }, 1.00f, "%"   },
    {  4u, MaterialChannelGroup::BaseSurface,     "Specular Tint",  "Fresnel edge tint",      true,  { 255u, 255u, 255u, 255u }, 1.00f, "%"   },
    {  5u, MaterialChannelGroup::BaseSurface,     "Anisotropy",     "Brushed highlight",      false, { 175u, 165u, 210u, 255u }, 0.00f, "%"   },
    {  6u, MaterialChannelGroup::BaseSurface,     "Aniso Angle",    "Tangent rotation",       false, { 175u, 165u, 210u, 255u }, 0.00f, "deg" },
    // 1. Microsurface (6)
    {  7u, MaterialChannelGroup::Microsurface,    "Roughness",      "Microfacet scatter",     false, { 148u, 163u, 184u, 255u }, 0.42f, "%"   },
    {  8u, MaterialChannelGroup::Microsurface,    "Normal",         "Tangent-space normal",   false, { 128u, 134u, 255u, 255u }, 1.00f, "%"   },
    {  9u, MaterialChannelGroup::Microsurface,    "Height",         "Displacement relief",    false, { 180u, 184u, 192u, 255u }, 0.18f, "%"   },
    { 10u, MaterialChannelGroup::Microsurface,    "Bump",           "Fine micro-relief",      false, { 165u, 170u, 180u, 255u }, 0.00f, "%"   },
    { 11u, MaterialChannelGroup::Microsurface,    "Curvature",      "Convex/concave wear",    false, { 196u, 172u, 132u, 255u }, 0.35f, "%"   },
    { 12u, MaterialChannelGroup::Microsurface,    "Ambient Occl",   "Cavity shadowing",       false, { 140u, 145u, 155u, 255u }, 1.00f, "%"   },
    // 2. Coat & Film (6)
    { 13u, MaterialChannelGroup::CoatAndFilm,     "Coat",           "Clearcoat weight",       false, { 207u, 227u, 255u, 255u }, 0.32f, "%"   },
    { 14u, MaterialChannelGroup::CoatAndFilm,     "Coat Rough",     "Lacquer gloss",          false, { 190u, 210u, 240u, 255u }, 0.12f, "%"   },
    { 15u, MaterialChannelGroup::CoatAndFilm,     "Coat IOR",       "Refractive index",       false, { 190u, 210u, 240u, 255u }, 0.60f, "ior" },
    { 16u, MaterialChannelGroup::CoatAndFilm,     "Coat Tint",      "Absorption colour",      true,  { 255u, 255u, 255u, 255u }, 1.00f, "%"   },
    { 17u, MaterialChannelGroup::CoatAndFilm,     "Thin Film",      "Iridescence weight",     false, { 168u, 220u, 240u, 255u }, 0.00f, "%"   },
    { 18u, MaterialChannelGroup::CoatAndFilm,     "Film Thickness", "Interference nm",        false, { 168u, 220u, 240u, 255u }, 0.38f, "nm"  },
    // 3. Sheen & Fuzz (3)
    { 19u, MaterialChannelGroup::SheenAndFuzz,    "Sheen",          "Grazing cloth fuzz",     false, { 232u, 214u, 198u, 255u }, 0.14f, "%"   },
    { 20u, MaterialChannelGroup::SheenAndFuzz,    "Sheen Rough",    "Microfiber spread",      false, { 218u, 200u, 184u, 255u }, 0.35f, "%"   },
    { 21u, MaterialChannelGroup::SheenAndFuzz,    "Sheen Tint",     "Velvet edge colour",     true,  { 245u, 232u, 216u, 255u }, 1.00f, "%"   },
    // 4. Transmission & SSS (5)
    { 22u, MaterialChannelGroup::TransmissionSss, "Transmission",   "Refractive glass",       false, { 158u, 230u, 216u, 255u }, 0.00f, "%"   },
    { 23u, MaterialChannelGroup::TransmissionSss, "IOR",            "Dielectric index",       false, { 158u, 230u, 216u, 255u }, 0.50f, "ior" },
    { 24u, MaterialChannelGroup::TransmissionSss, "Subsurface",     "Volumetric scatter",     false, { 226u, 142u, 118u, 255u }, 0.08f, "%"   },
    { 25u, MaterialChannelGroup::TransmissionSss, "SSS Radius",     "Mean free path",         false, { 226u, 142u, 118u, 255u }, 0.22f, "%"   },
    { 26u, MaterialChannelGroup::TransmissionSss, "SSS Colour",     "Sub-dermal tint",        true,  { 217u, 107u,  67u, 255u }, 1.00f, "%"   },
    // 5. Emission & Opacity (1)
    { 27u, MaterialChannelGroup::EmissionOpacity, "Emission",       "Self-luminous radiance", true,  { 255u, 122u,  59u, 255u }, 0.00f, "ev"  },
}};

} // namespace

MaterialOutlinerPane::MaterialOutlinerPane() noexcept
{
    for (std::size_t I = 0u; I < ChannelCount; ++I)
    {
        ChannelScalars[I]   = ChannelCatalog[I].DefaultValue;
        ChannelTints[I]     = ChannelCatalog[I].DefaultTint;
        ChannelEnabled[I]   = false;
        ChannelExpanded[I]  = false;
        ChannelSourceTab[I] = 0u;
    }
    // Default 6 active OpenPBR channels matching cards-material.png ("Glazed Terracotta · 6/28 CHANNELS ACTIVE")
    ChannelEnabled[0]  = true; // Colour (#b97a45)
    ChannelEnabled[2]  = true; // Metalness (10%)
    ChannelEnabled[7]  = true; // Roughness (42%)
    ChannelEnabled[8]  = true; // Normal (100%)
    ChannelEnabled[9]  = true; // Height (18%)
    ChannelEnabled[13] = true; // Coat (32%)

    // Open Colour + Roughness side-by-side by default (matching cards-material.png)
    ChannelExpanded[0] = true;
    ChannelExpanded[7] = true;
}

MaterialOutlinerPane::~MaterialOutlinerPane() noexcept
{
    WorkspaceAppearance::ReleasePanePointerOwnership(this);
}

const std::array<MaterialChannelEntrySpec, MaterialOutlinerPane::ChannelCount>& MaterialOutlinerPane::QueryChannelCatalog() noexcept
{
    return ChannelCatalog;
}

const std::array<const char*, MaterialOutlinerPane::GroupCount>& MaterialOutlinerPane::QueryGroupNames() noexcept
{
    return GroupLabels;
}

std::size_t MaterialOutlinerPane::CountEnabledChannels() const noexcept
{
    std::size_t Total = 0u;
    for (bool On : ChannelEnabled)
    {
        if (On) ++Total;
    }
    return Total;
}

void MaterialOutlinerPane::PresentInImGuiPane(double DeltaSeconds, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##MaterialOutlinerCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds { WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace       Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    WorkspaceAppearance::EnforcePanePointerOwnership(this, Input);

    Render(Surface, LogicalBounds, Input, DeltaSeconds);
    WorkspaceAppearance::UpdatePanePointerOwnership(this, ScreenBounds, SafeScale, Input, false);
}

void MaterialOutlinerPane::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  DeltaSeconds)
{
    (void)DeltaSeconds;
    if (Bounds.Width < 160.0f || Bounds.Height < 180.0f)
    {
        return;
    }

    const TexturingThemeColors Theme   = QueryTexturingThemeColors();
    const Point2D              Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool                 Pressed = Input.PrimaryPointer.Pressed;

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PaneBackground, 0.0f);
    Surface.StrokeLine({ Bounds.Right() - 1.0f, Bounds.Y }, { Bounds.Right() - 1.0f, Bounds.Bottom() }, Theme.BorderSubtle, 1.0f);

    const float PadX   = 12.0f;
    const float LeftX  = Bounds.X + PadX;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float       CurY   = Bounds.Y + 12.0f;

    // 1. Outliner Header ("Outliner  28 CHANNELS · 6 GROUPS")
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::MaterialSphere, VectorIconPlacement{ LeftX, CurY + 3.0f, 13.5f, ColorRgba{ 232u, 194u, 122u, 255u }, 1.7f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { LeftX + 19.0f, CurY + 1.0f }, 10.8f, Theme.TextPrimary, "Outliner");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Bold, { LeftX + 70.0f, CurY + 3.0f }, 7.8f, ColorRgba{ 232u, 194u, 122u, 255u }, "28 CHANNELS · 6 GROUPS");
    CurY += 26.0f;

    // 2. Search & Filter Row ("Filter channels..." + "All | On")
    const float FilterSegW = 70.0f;
    const Rectangle2D SearchR{ LeftX, CurY, InnerW - FilterSegW - 6.0f, 26.0f };
    Surface.FillRectangle(SearchR, Theme.WellSurface, 7.0f);
    Surface.StrokeRectangle(SearchR, Theme.BorderSubtle, 7.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchR.X + 8.0f, SearchR.Y + 7.0f, 12.0f, Theme.TextFaint, 1.6f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { SearchR.X + 25.0f, SearchR.Y + 5.5f }, 8.8f, Theme.TextFaint, "Filter channels...");

    static constexpr const char* FilterLabels[2] { "All", "On" };
    for (uint32_t Fi = 0u; Fi < 2u; ++Fi)
    {
        const Rectangle2D PillR{ SearchR.Right() + 6.0f + static_cast<float>(Fi) * 35.0f, CurY, 33.0f, 26.0f };
        const bool Act = (Fi == 1u) ? FilterActiveOnly : !FilterActiveOnly;
        Surface.FillRectangle(PillR, Act ? ColorRgba{ 238u, 238u, 236u, 255u } : Theme.SectionSurface, 6.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, Act ? FontWeightClass::SemiBold : FontWeightClass::Medium, { PillR.X + 9.0f, PillR.Y + 6.0f }, 8.2f,
            Act ? ColorRgba{ 16u, 17u, 20u, 255u } : Theme.TextMuted, FilterLabels[Fi]);
        if (Pressed && PillR.Contains(Cursor))
        {
            FilterActiveOnly = (Fi == 1u);
        }
    }
    CurY += 32.0f;
    Surface.StrokeLine({ Bounds.X, CurY }, { Bounds.Right(), CurY }, Theme.BorderSubtle, 1.0f);
    CurY += 4.0f;

    // Reserve space at bottom for the Action Footer Bar (38px) + Status Bar (26px) matching outliner-footer-material.png
    constexpr float ActionBarH = 38.0f;
    constexpr float StatusBarH = 26.0f;
    const Rectangle2D ScrollRect{
        Bounds.X,
        CurY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - ActionBarH - StatusBarH - CurY)
    };

    if (ScrollRect.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        ScrollOffsetY = std::clamp(ScrollOffsetY - Input.WheelDeltaY * 32.0f, 0.0f, MaxScrollY);
    }

    Surface.PushClip(ScrollRect);
    float Sy = ScrollRect.Y + 6.0f - ScrollOffsetY;
    const float ScrollStartY = Sy;

    for (std::size_t G = 0u; G < GroupCount; ++G)
    {
        const MaterialChannelGroup GroupEnum = static_cast<MaterialChannelGroup>(G);
        std::size_t GroupTotal = 0u;
        std::size_t GroupOn    = 0u;
        for (std::size_t C = 0u; C < ChannelCount; ++C)
        {
            if (ChannelCatalog[C].Group == GroupEnum)
            {
                ++GroupTotal;
                if (ChannelEnabled[C]) ++GroupOn;
            }
        }
        if (FilterActiveOnly && GroupOn == 0u)
        {
            continue;
        }

        const Rectangle2D GHeadR{ LeftX, Sy, InnerW, 26.0f };
        const bool GOpen = GroupOpen[G];
        Surface.FillRectangle(GHeadR, GHeadR.Contains(Cursor) ? Theme.HoverSurface : Theme.SectionSurface, 6.0f);
        VectorIconRepository::ConstructControl(
            Surface, GOpen ? ControlIcon::ChevronDown : ControlIcon::ChevronForward,
            VectorIconPlacement{ GHeadR.X + 7.0f, GHeadR.Y + 8.0f, 10.0f, Theme.TextMuted, 1.7f });
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Bold, { GHeadR.X + 22.0f, GHeadR.Y + 6.5f }, 8.0f, Theme.TextMuted, GroupLabels[G]);

        char GCntBuf[24];
        std::snprintf(GCntBuf, sizeof(GCntBuf), "%zu/%zu", GroupOn, GroupTotal);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { GHeadR.Right() - 34.0f, GHeadR.Y + 6.5f }, 8.0f, Theme.TextFaint, GCntBuf);

        if (Pressed && ScrollRect.Contains(Cursor) && GHeadR.Contains(Cursor))
        {
            GroupOpen[G] = !GroupOpen[G];
        }
        Sy += 29.0f;

        if (!GOpen)
        {
            continue;
        }

        for (std::size_t C = 0u; C < ChannelCount; ++C)
        {
            const auto& Spec = ChannelCatalog[C];
            if (Spec.Group != GroupEnum) continue;
            if (FilterActiveOnly && !ChannelEnabled[C]) continue;

            const Rectangle2D RowR{ LeftX, Sy, InnerW, 30.0f };
            const bool SelRow = (SelectedChannelIndex == C);
            const bool HovRow = ScrollRect.Contains(Cursor) && RowR.Contains(Cursor);

            Surface.FillRectangle(RowR, SelRow ? Theme.HoverSurface : (HovRow ? Theme.SectionSurface : Theme.WellSurface), 7.0f);
            if (SelRow)
            {
                Surface.FillRectangle({ RowR.X, RowR.Y + 5.0f, 3.0f, RowR.Height - 10.0f }, ColorRgba{ 255u, 255u, 255u, 255u }, 1.5f);
            }

            // Swatch or scalar mini-bar
            if (Spec.ColorChannel)
            {
                Surface.FillRectangle({ RowR.X + 8.0f, RowR.Y + 8.0f, 14.0f, 14.0f }, ChannelTints[C], 4.0f);
                Surface.StrokeRectangle({ RowR.X + 8.0f, RowR.Y + 8.0f, 14.0f, 14.0f }, Theme.BorderSubtle, 4.0f, 1.0f);
            }
            else
            {
                Surface.FillCircle({ RowR.X + 15.0f, RowR.Y + 15.0f }, 4.5f, ChannelEnabled[C] ? Spec.DefaultTint : Theme.TextFaint);
            }

            WorkspaceAppearance::ConstructTruncatedText(
                Surface,
                ChannelEnabled[C] ? FontWeightClass::SemiBold : FontWeightClass::Regular,
                { RowR.X + 27.0f, RowR.Y + 7.5f },
                InnerW - 96.0f,
                9.2f,
                ChannelEnabled[C] ? Theme.TextPrimary : Theme.TextMuted,
                Spec.Label);

            char ValBuf[24];
            if (!ChannelEnabled[C])
            {
                std::snprintf(ValBuf, sizeof(ValBuf), "Off");
            }
            else if (Spec.ColorChannel)
            {
                std::snprintf(ValBuf, sizeof(ValBuf), "#%02x%02x%02x", ChannelTints[C].Red, ChannelTints[C].Green, ChannelTints[C].Blue);
            }
            else
            {
                std::snprintf(ValBuf, sizeof(ValBuf), "%d%%", static_cast<int>(std::lround(ChannelScalars[C] * 100.0f)));
            }
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { RowR.Right() - 68.0f, RowR.Y + 8.5f }, 8.0f,
                ChannelEnabled[C] ? Theme.TextMuted : Theme.TextFaint, ValBuf);

            // Toggle pill on right of row
            const Rectangle2D ToggleR{ RowR.Right() - 28.0f, RowR.Y + 8.0f, 22.0f, 14.0f };
            Surface.FillRectangle(ToggleR, ChannelEnabled[C] ? ColorRgba{ 238u, 238u, 236u, 255u } : Theme.SectionSurface, 7.0f);
            Surface.FillCircle(
                { ChannelEnabled[C] ? (ToggleR.Right() - 7.0f) : (ToggleR.X + 7.0f), ToggleR.Y + 7.0f },
                4.8f,
                ChannelEnabled[C] ? ColorRgba{ 16u, 17u, 20u, 255u } : Theme.TextFaint);

            if (Pressed && HovRow)
            {
                if (ToggleR.Contains(Cursor))
                {
                    ChannelEnabled[C] = !ChannelEnabled[C];
                    if (ChannelEnabled[C]) ChannelExpanded[C] = true;
                }
                else
                {
                    SelectedChannelIndex = static_cast<uint32_t>(C);
                    ChannelExpanded[C]   = !ChannelExpanded[C];
                }
            }
            Sy += 32.0f;
        }
        Sy += 4.0f;
    }

    MaxScrollY = std::max(0.0f, (Sy - ScrollStartY) - ScrollRect.Height + 12.0f);
    Surface.PopClip();

    // 3. Bottom Action Footer Bar ("+ Enable | Preset | Reset   Solo | Mute", matching outliner-footer-material.png)
    const Rectangle2D ActionBarR{ Bounds.X, Bounds.Bottom() - StatusBarH - ActionBarH, Bounds.Width, ActionBarH };
    Surface.FillRectangle(ActionBarR, Theme.PaneBackground, 0.0f);
    Surface.StrokeLine({ ActionBarR.X, ActionBarR.Y }, { ActionBarR.Right(), ActionBarR.Y }, Theme.BorderSubtle, 1.0f);

    auto DrawActionPill = [&](Rectangle2D R, const char* Text, bool Active) -> bool
    {
        const bool Hov = R.Contains(Cursor);
        Surface.FillRectangle(R, Active ? ColorRgba{ 238u, 238u, 236u, 255u } : (Hov ? Theme.HoverSurface : Theme.SectionSurface), 6.0f);
        Surface.StrokeRectangle(R, Theme.BorderSubtle, 6.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, Active ? FontWeightClass::SemiBold : FontWeightClass::Medium, { R.X + 8.0f, R.Y + 5.5f }, 8.2f,
            Active ? ColorRgba{ 16u, 17u, 20u, 255u } : Theme.TextPrimary, Text);
        return Hov && Pressed;
    };

    const float BtnY = ActionBarR.Y + 7.0f;
    if (DrawActionPill({ LeftX, BtnY, 58.0f, 24.0f }, "+ Enable", false))
    {
        ChannelEnabled[SelectedChannelIndex % ChannelCount]  = true;
        ChannelExpanded[SelectedChannelIndex % ChannelCount] = true;
    }
    if (DrawActionPill({ LeftX + 62.0f, BtnY, 48.0f, 24.0f }, "Preset", false) && Session != nullptr && !Session->PresetRepository.empty())
    {
        const std::size_t NextIdx = (Session->ActiveTabIndex + 1u) % Session->PresetRepository.size();
        Session->SelectPresetIdentity(Session->PresetRepository[NextIdx].PresetIdentity);
    }
    if (DrawActionPill({ LeftX + 114.0f, BtnY, 44.0f, 24.0f }, "Reset", false))
    {
        for (std::size_t I = 0u; I < ChannelCount; ++I)
        {
            ChannelScalars[I] = ChannelCatalog[I].DefaultValue;
            ChannelTints[I]   = ChannelCatalog[I].DefaultTint;
        }
    }

    if (DrawActionPill({ Bounds.Right() - PadX - 86.0f, BtnY, 40.0f, 24.0f }, "Solo", SoloActive))
    {
        SoloActive = !SoloActive;
    }
    if (DrawActionPill({ Bounds.Right() - PadX - 42.0f, BtnY, 42.0f, 24.0f }, "Mute", MuteActive))
    {
        MuteActive = !MuteActive;
    }

    // 4. Bottom Status Footer
    const Rectangle2D FooterR{ Bounds.X, Bounds.Bottom() - StatusBarH, Bounds.Width, StatusBarH };
    char StatusLeft[48];
    std::snprintf(StatusLeft, sizeof(StatusLeft), "%zu/%zu active", CountEnabledChannels(), ChannelCount);
    ConstructPaneStatusFooter(Surface, FooterR, StatusLeft, "OpenPBR 1.1", Theme, false);

    Surface.PopClip();
}

} // namespace Frontier::Workspaces

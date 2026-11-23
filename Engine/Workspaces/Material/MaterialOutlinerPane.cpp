/*============================================================================================================================================
                                                         MATERIALOUTLINERPANE.CPP
============================================================================================================================================*/
// 🧩 Implementation of MaterialOutlinerPane: 1:1 parity with Experimental/TextureStudio docked Material Outliner (cards-material.png
//    & outliner-footer-material.png): STRATA / DRIVEN KPI tiles, quick stratum pills, Scene -> Surface body -> Core/Specular/Coat/Sheen/Thin film
//    tree with 2x2 quadrant group icons & 28x28 procedural channel icons, bottom action bar, and 4-column telemetry footer.

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
    "Core",
    "Specular",
    "Coat",
    "Sheen",
    "Thin film",
    "Transmission",
}};

constexpr std::array<const char*, MaterialOutlinerPane::GroupCount> GroupUpperLabels {{
    "CORE",
    "SPECULAR",
    "COAT",
    "SHEEN",
    "THIN FILM",
    "TRANSMISSION",
}};

constexpr std::array<MaterialChannelEntrySpec, MaterialOutlinerPane::ChannelCount> ChannelCatalog {{
    // 0. Core (8) — matches cards-material.png & outliner-footer-material.png 1:1
    {  0u, MaterialChannelGroup::Core,                    "Colour",          "Base albedo",           "color",         true,  false, {   9u,  12u,  17u, 255u }, 1.00f, ""   },
    {  1u, MaterialChannelGroup::Core,                    "Roughness",       "Microfacet gloss",      "roughness",     false, false, { 118u, 122u, 132u, 255u }, 0.08f, ""   },
    {  2u, MaterialChannelGroup::Core,                    "Metallic",        "Conductor mask",        "metallic",      false, false, { 118u, 122u, 132u, 255u }, 0.34f, ""   },
    {  3u, MaterialChannelGroup::Core,                    "Normal",          "Surface tangent",       "normal",        false, true,  { 118u, 130u, 245u, 255u }, 1.00f, ""   },
    {  4u, MaterialChannelGroup::Core,                    "Micro normal",    "Fine pore / grain",     "microNormal",   false, true,  { 118u, 130u, 245u, 255u }, 1.00f, ""   },
    {  5u, MaterialChannelGroup::Core,                    "Height",          "Displacement relief",   "height",        false, false, { 118u, 122u, 132u, 255u }, 0.50f, ""   },
    {  6u, MaterialChannelGroup::Core,                    "Occlusion",       "Cavity AO",             "occlusion",     false, false, {  98u, 102u, 110u, 255u }, 1.00f, ""   },
    {  7u, MaterialChannelGroup::Core,                    "Opacity",         "Cutout / alpha",        "opacity",       false, false, { 118u, 122u, 132u, 255u }, 1.00f, ""   },
    // 1. Specular (5)
    {  8u, MaterialChannelGroup::Specular,                "Specular",        "Reflectance weight",    "specular",      false, false, { 118u, 122u, 132u, 255u }, 1.00f, ""   },
    {  9u, MaterialChannelGroup::Specular,                "Specular Tint",   "Fresnel edge tint",     "specularTint",  true,  false, { 255u, 255u, 255u, 255u }, 1.00f, ""   },
    { 10u, MaterialChannelGroup::Specular,                "IOR",             "Refractive index",      "ior",           false, false, {  98u, 102u, 110u, 255u }, 1.50f, ""   },
    { 11u, MaterialChannelGroup::Specular,                "Anisotropy",      "Brushed highlight",     "anisotropy",    false, false, {  98u, 102u, 110u, 255u }, 0.00f, ""   },
    { 12u, MaterialChannelGroup::Specular,                "Aniso Rotation",  "Tangent angle",         "anisoRotation", false, false, {  98u, 102u, 110u, 255u }, 0.00f, ""   },
    // 2. Coat (4)
    { 13u, MaterialChannelGroup::Coat,                    "Coat",            "Clearcoat weight",      "coat",          false, false, { 118u, 122u, 132u, 255u }, 0.32f, ""   },
    { 14u, MaterialChannelGroup::Coat,                    "Coat Roughness",  "Lacquer gloss",         "coatRoughness", false, false, { 118u, 122u, 132u, 255u }, 0.12f, ""   },
    { 15u, MaterialChannelGroup::Coat,                    "Coat IOR",        "Coat refractive index", "coatIor",       false, false, {  98u, 102u, 110u, 255u }, 1.50f, ""   },
    { 16u, MaterialChannelGroup::Coat,                    "Coat Normal",     "Clearcoat tangent",     "coatNormal",    false, true,  { 118u, 130u, 245u, 255u }, 1.00f, ""   },
    // 3. Sheen (3)
    { 17u, MaterialChannelGroup::Sheen,                   "Sheen",           "Grazing cloth fuzz",    "sheen",         false, false, { 118u, 122u, 132u, 255u }, 0.14f, ""   },
    { 18u, MaterialChannelGroup::Sheen,                   "Sheen Roughness", "Microfiber spread",     "sheenRoughness",false, false, { 118u, 122u, 132u, 255u }, 0.35f, ""   },
    { 19u, MaterialChannelGroup::Sheen,                   "Sheen Colour",    "Velvet edge tint",      "sheenColor",    true,  false, { 245u, 232u, 216u, 255u }, 1.00f, ""   },
    // 4. Thin film (3)
    { 20u, MaterialChannelGroup::ThinFilm,                "Thin Film",       "Iridescence weight",    "thinFilm",      false, false, { 118u, 122u, 132u, 255u }, 0.00f, ""   },
    { 21u, MaterialChannelGroup::ThinFilm,                "Film Thickness",  "Interference nm",       "filmThickness", false, false, { 118u, 122u, 132u, 255u }, 0.38f, "nm" },
    { 22u, MaterialChannelGroup::ThinFilm,                "Film IOR",        "Film refractive index", "filmIor",       false, false, {  98u, 102u, 110u, 255u }, 1.40f, ""   },
    // 5. Transmission & Emission (5)
    { 23u, MaterialChannelGroup::TransmissionAndEmissive, "Transmission",    "Refractive glass",      "transmission",  false, false, { 118u, 122u, 132u, 255u }, 0.00f, ""   },
    { 24u, MaterialChannelGroup::TransmissionAndEmissive, "Subsurface",      "Volumetric scatter",    "subsurface",    false, false, { 118u, 122u, 132u, 255u }, 0.08f, ""   },
    { 25u, MaterialChannelGroup::TransmissionAndEmissive, "SSS Radius",      "Mean free path",        "sssRadius",     false, false, { 118u, 122u, 132u, 255u }, 0.22f, ""   },
    { 26u, MaterialChannelGroup::TransmissionAndEmissive, "SSS Colour",      "Sub-dermal tint",       "sssColor",      true,  false, { 217u, 107u,  67u, 255u }, 1.00f, ""   },
    { 27u, MaterialChannelGroup::TransmissionAndEmissive, "Emission",        "Self-luminous radiance","emission",      true,  false, { 255u, 122u,  59u, 255u }, 0.00f, "ev" },
}};

void DrawGroupQuadrantGlyph(PixelSpace& Surface, Rectangle2D SlotR) noexcept
{
    const float Cx = SlotR.X + SlotR.Width * 0.5f;
    const float Cy = SlotR.Y + SlotR.Height * 0.5f;
    const ColorRgba SqCol { 94u, 98u, 108u, 255u };
    // 2x2 grid: TL square, TR square, BL square, BR purple circle (matches cards-material.png)
    Surface.FillRectangle({ Cx - 7.5f, Cy - 7.5f, 6.5f, 6.5f }, SqCol, 1.8f);
    Surface.FillRectangle({ Cx + 1.0f, Cy - 7.5f, 6.5f, 6.5f }, SqCol, 1.8f);
    Surface.FillRectangle({ Cx - 7.5f, Cy + 1.0f, 6.5f, 6.5f }, SqCol, 1.8f);
    Surface.FillCircle({ Cx + 4.2f, Cy + 4.2f }, 3.2f, ColorRgba{ 118u, 130u, 245u, 255u });
}

} // namespace

MaterialOutlinerPane::MaterialOutlinerPane() noexcept
{
    for (std::size_t I = 0u; I < ChannelCount; ++I)
    {
        ChannelScalars[I]   = ChannelCatalog[I].DefaultValue;
        ChannelTints[I]     = ChannelCatalog[I].DefaultTint;
        ChannelEnabled[I]   = true;
        ChannelExpanded[I]  = false;
        ChannelSourceTab[I] = 0u;
    }
    // Open Colour + Roughness simultaneously in the vertical stack (matching outliner-footer-material.png & CheckCardDock.mjs)
    ChannelExpanded[0] = true;
    ChannelExpanded[1] = true;
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

const std::array<const char*, MaterialOutlinerPane::GroupCount>& MaterialOutlinerPane::QueryGroupUpperNames() noexcept
{
    return GroupUpperLabels;
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

std::size_t MaterialOutlinerPane::CountDrivenChannels() const noexcept
{
    std::size_t Driven = 0u;
    for (std::size_t I = 0u; I < ChannelCount; ++I)
    {
        if (ChannelEnabled[I] && ChannelSourceTab[I] > 0u)
        {
            ++Driven;
        }
    }
    return Driven;
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
    Surface.FillRectangle(Bounds, Theme.LightSurface ? Theme.PaneBackground : ColorRgba{ 24u, 25u, 28u, 255u }, 0.0f);

    const float PadX   = 12.0f;
    const float LeftX  = Bounds.X + PadX;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float       CurY   = Bounds.Y + 12.0f;

    // 1. Outliner Header ("Outliner  Material Scene & Strata" + circular '+' button)
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, CurY - 2.0f }, 15.0f, Theme.TextPrimary, "Outliner");
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX + 64.0f, CurY + 3.0f }, 9.2f, Theme.TextFaint, "Material Scene & Strata");

    const Rectangle2D TopAddBtnR{ Bounds.Right() - PadX - 24.0f, CurY - 1.0f, 24.0f, 24.0f };
    Surface.FillRectangle(TopAddBtnR, TopAddBtnR.Contains(Cursor) ? ColorRgba{ 44u, 45u, 49u, 255u } : ColorRgba{ 34u, 35u, 39u, 255u }, 12.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::PlusAdd, VectorIconPlacement{ TopAddBtnR.X + 6.5f, TopAddBtnR.Y + 6.5f, 11.0f, Theme.TextPrimary, 1.7f });
    CurY += 30.0f;

    // 2. Two KPI Stat Tiles: STRATA (1/1) & DRIVEN (0/28)
    const float StatGap = 8.0f;
    const float StatW   = (InnerW - StatGap) * 0.5f;
    const std::size_t DrivenCnt = CountDrivenChannels();
    char DrivenBuf[24];
    std::snprintf(DrivenBuf, sizeof(DrivenBuf), "%zu/%zu", DrivenCnt, ChannelCount);

    ConstructOutlinerStatTile(
        Surface, { LeftX, CurY, StatW, 42.0f }, ControlIcon::CircleCheck, "STRATA", "1/1", true, false, Theme, Cursor, Pressed);
        if (ConstructOutlinerStatTile(
                Surface, { LeftX + StatW + StatGap, CurY, StatW, 42.0f }, ControlIcon::NoiseWave, "DRIVEN", DrivenBuf, DrivenCnt > 0u, FilterActiveOnly, Theme, Cursor, Pressed))
    {
        FilterActiveOnly = !FilterActiveOnly;
    }
    CurY += 50.0f;

    // 3. Search & Filter Row ("Search strata, channels" + "Filter v")
    const float FilterW = 68.0f;
    const Rectangle2D SearchR{ LeftX, CurY, InnerW - FilterW - 6.0f, 28.0f };
    Surface.FillRectangle(SearchR, ColorRgba{ 19u, 20u, 23u, 255u }, 14.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::SearchGlass, VectorIconPlacement{ SearchR.X + 10.0f, SearchR.Y + 8.0f, 12.0f, Theme.TextFaint, 1.6f });
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { SearchR.X + 27.0f, SearchR.Y + 6.5f }, 9.2f, Theme.TextFaint, "Search strata, channels");

    const Rectangle2D FilterR{ SearchR.Right() + 6.0f, CurY, FilterW, 28.0f };
    Surface.FillRectangle(FilterR, FilterR.Contains(Cursor) ? ColorRgba{ 34u, 35u, 39u, 255u } : ColorRgba{ 19u, 20u, 23u, 255u }, 14.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { FilterR.X + 12.0f, FilterR.Y + 6.5f }, 8.8f, Theme.TextMuted, "Filter");
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::ChevronDown, VectorIconPlacement{ FilterR.Right() - 16.0f, FilterR.Y + 9.5f, 9.0f, Theme.TextFaint, 1.6f });
    CurY += 35.0f;

    // 4. Quick Stratum Chips Row ("Detail stratum", "Scuffs", "Camo", "Cells", "Grain")
    struct QuickChipSpec { const char* Label; bool HasGrad; ColorRgba C0; ColorRgba C1; };
    static constexpr std::array<QuickChipSpec, 5u> QuickChips {{
        { "+ Detail stratum", false, {}, {} },
        { "Scuffs",           true,  {  55u,  58u,  64u, 255u }, { 215u, 218u, 224u, 255u } },
        { "Camo",             true,  {  52u,  88u,  62u, 255u }, { 196u, 158u,  92u, 255u } },
        { "Cells",            true,  {  40u,  44u,  52u, 255u }, { 225u, 228u, 235u, 255u } },
        { "Grain",            true,  {  58u,  60u,  66u, 255u }, { 210u, 212u, 218u, 255u } },
    }};
    float ChipX = LeftX;
    float ChipY = CurY;
    for (const auto& Qc : QuickChips)
    {
        const float TextW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 8.2f, Qc.Label);
        const float ChipW = TextW + (Qc.HasGrad ? 32.0f : 18.0f);
        if (ChipX + ChipW > LeftX + InnerW && ChipX > LeftX)
        {
            ChipX = LeftX;
            ChipY += 24.0f;
        }
        const Rectangle2D ChipR{ ChipX, ChipY, ChipW, 20.0f };
        Surface.FillRectangle(ChipR, ChipR.Contains(Cursor) ? ColorRgba{ 40u, 42u, 48u, 255u } : ColorRgba{ 30u, 31u, 36u, 255u }, 10.0f);
        float LabelX = ChipR.X + 9.0f;
        if (Qc.HasGrad)
        {
            Surface.FillRectangle({ ChipR.X + 6.0f, ChipR.Y + 5.0f, 7.0f, 10.0f }, Qc.C0, 2.5f);
            Surface.FillRectangle({ ChipR.X + 12.0f, ChipR.Y + 5.0f, 7.0f, 10.0f }, Qc.C1, 2.5f);
            LabelX = ChipR.X + 23.0f;
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { LabelX, ChipR.Y + 3.5f }, 8.2f, Theme.TextMuted, Qc.Label);
        ChipX += ChipW + 5.0f;
    }
    CurY = ChipY + 26.0f;

    // Reserve space at bottom for the Action Bar (30px) + 4-Column Telemetry Strip (32px) matching outliner-footer-material.png
    constexpr float ActionBarH = 30.0f;
    constexpr float StatusBarH = 32.0f;
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
    float Sy = ScrollRect.Y + 2.0f - ScrollOffsetY;
    const float ScrollStartY = Sy;

    const char* ActiveMatTitle = (Session != nullptr && !Session->DocumentTabs.empty())
        ? Session->InspectActiveRecipe().DisplayTitle.c_str()
        : "Derez Lattice Dissolve (ControlVehicle)";

    // Root Scene Row (Depth 0): "Derez Lattice Dissolve (ControlVehicle)" / "Animated & VFX · 1/1 strata"
    {
        const Rectangle2D SceneRowR{ LeftX, Sy, InnerW, 34.0f };
        const auto SceneAct = ConstructOutlinerTreeRow(
            Surface, SceneRowR, 0u, true, SceneNodeOpen, false, false,
            ControlIcon::MaterialSphere, Theme.TextMuted,
            ActiveMatTitle, "Animated & VFX · 1/1 strata",
            {}, "", -1, -1, -1, Theme, Cursor, Pressed, false);
        if (SceneAct.CaretToggled || SceneAct.RowSelected)
        {
            SceneNodeOpen = !SceneNodeOpen;
        }
        Sy += 35.0f;
    }

    if (SceneNodeOpen)
    {
        // Stratum Row (Depth 1): "Surface body" / "Surface · 28 ch" + ACTIVE badge + white check disc
        {
            const Rectangle2D StratRowR{ LeftX, Sy, InnerW, 34.0f };
            const auto StratAct = ConstructOutlinerTreeRow(
                Surface, StratRowR, 1u, true, StratumNodeOpen, false, false,
                ControlIcon::LayersSlabs, Theme.TextMuted,
                "Surface body", "Surface · 28 ch",
                {}, "ACTIVE", -1, 1, -1, Theme, Cursor, Pressed, false);
            if (StratAct.CaretToggled || StratAct.RowSelected)
            {
                StratumNodeOpen = !StratumNodeOpen;
            }
            Sy += 35.0f;
        }

        if (StratumNodeOpen)
        {
            for (std::size_t G = 0u; G < GroupCount; ++G)
            {
                const MaterialChannelGroup GroupEnum = static_cast<MaterialChannelGroup>(G);
                std::size_t GroupTotal = 0u;
                for (std::size_t C = 0u; C < ChannelCount; ++C)
                {
                    if (ChannelCatalog[C].Group == GroupEnum) ++GroupTotal;
                }

                char CountTag[16];
                std::snprintf(CountTag, sizeof(CountTag), "%zu ch", GroupTotal);

                const Rectangle2D GHeadR{ LeftX, Sy, InnerW, 30.0f };
                Rectangle2D GIconSlot {};
                const auto GAct = ConstructOutlinerTreeRow(
                    Surface, GHeadR, 2u, true, GroupOpen[G], false, false,
                    ControlIcon::Count, Theme.TextPrimary,
                    GroupLabels[G], "",
                    {}, CountTag, -1, -1, -1, Theme, Cursor, Pressed, false, &GIconSlot);
                DrawGroupQuadrantGlyph(Surface, GIconSlot);
                if (GAct.CaretToggled || GAct.RowSelected)
                {
                    GroupOpen[G] = !GroupOpen[G];
                }
                Sy += 31.0f;

                if (!GroupOpen[G])
                {
                    continue;
                }

                for (std::size_t C = 0u; C < ChannelCount; ++C)
                {
                    const auto& Spec = ChannelCatalog[C];
                    if (Spec.Group != GroupEnum) continue;

                    char MetaBuf[40];
                    if (Spec.ColorChannel)
                    {
                        char HexB[12];
                        FormatHexLower(ChannelTints[C], HexB, sizeof(HexB));
                        for (char* P = HexB; *P; ++P)
                        {
                            if (*P >= 'a' && *P <= 'f') *P = static_cast<char>(*P - 'a' + 'A');
                        }
                        std::snprintf(MetaBuf, sizeof(MetaBuf), "Value · %s", HexB);
                    }
                    else if (Spec.NormalChannel)
                    {
                        std::snprintf(MetaBuf, sizeof(MetaBuf), "Value · Flat +Z");
                    }
                    else
                    {
                        std::snprintf(MetaBuf, sizeof(MetaBuf), "Value · %.2f", ChannelScalars[C]);
                    }

                    const Rectangle2D ChRowR{ LeftX, Sy, InnerW, 34.0f };
                    Rectangle2D ChIconSlot {};
                    const bool IsSel = (SelectedChannelIndex == C);
                    const auto ChAct = ConstructOutlinerTreeRow(
                        Surface, ChRowR, 3u, false, false, IsSel, false,
                        ControlIcon::Count, Theme.TextPrimary,
                        Spec.Label, MetaBuf,
                        {}, "", -1, -1, -1, Theme, Cursor, Pressed, false, &ChIconSlot);
                    ConstructProceduralChannelIcon(
                        Surface, ChIconSlot, Spec.Label, ChannelTints[C], Spec.ColorChannel, ChannelScalars[C], true);

                    if (ChAct.RowSelected)
                    {
                        SelectedChannelIndex = static_cast<uint32_t>(C);
                        ChannelExpanded[C]   = !ChannelExpanded[C];
                    }
                    Sy += 35.0f;
                }
            }
        }
    }

    const float ContentH = (Sy - ScrollStartY) + 8.0f;
    MaxScrollY = std::max(0.0f, ContentH - ScrollRect.Height);
    Surface.PopClip();

    // 5. Outliner Action Bar above Footer (+ | ^ | v | Eye ... Trash, matching outliner-footer-material.png)
    const float ActY = Bounds.Bottom() - ActionBarH - StatusBarH;
    static constexpr std::array<ControlIcon, 4u> LeftIcons {{
        ControlIcon::PlusAdd,
        ControlIcon::ChevronUp,
        ControlIcon::ChevronDown,
        ControlIcon::EyeVisible,
    }};
    for (std::size_t I = 0u; I < LeftIcons.size(); ++I)
    {
        const Rectangle2D BtnR{ LeftX + static_cast<float>(I) * 26.0f, ActY + 3.0f, 24.0f, 24.0f };
        ConstructHeaderIconButton(Surface, BtnR, LeftIcons[I], false, true, Theme, Cursor, Pressed);
    }
    const Rectangle2D TrashR{ Bounds.Right() - PadX - 24.0f, ActY + 3.0f, 24.0f, 24.0f };
    ConstructHeaderIconButton(Surface, TrashR, ControlIcon::TrashDelete, false, false, Theme, Cursor, Pressed);

    // 6. Bottom 4-Column Telemetry Strip (F0 2.8% | CLASS Hybrid | GLOSS Mirror | ALBEDO 5%)
    const Rectangle2D FootR{ Bounds.X + 8.0f, Bounds.Bottom() - StatusBarH - 4.0f, Bounds.Width - 16.0f, StatusBarH };
    Surface.FillRectangle(FootR, ColorRgba{ 19u, 20u, 23u, 255u }, 10.0f);
    struct FootCol { const char* Label; const char* Value; bool Amber; };
    static constexpr std::array<FootCol, 4u> FootCols {{
        { "F0",     "2.8%",   false },
        { "CLASS",  "Hybrid", true  },
        { "GLOSS",  "Mirror", false },
        { "ALBEDO", "5%",     true  },
    }};
    const float ColW = (FootR.Width - 16.0f) / 4.0f;
    for (std::size_t I = 0u; I < 4u; ++I)
    {
        const float Cx = FootR.X + 8.0f + static_cast<float>(I) * ColW;
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { Cx, FootR.Y + 4.0f }, 7.2f, Theme.TextFaint, FootCols[I].Label);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { Cx, FootR.Y + 15.0f }, 9.2f,
            FootCols[I].Amber ? ColorRgba{ 240u, 164u, 58u, 255u } : Theme.TextPrimary, FootCols[I].Value);
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces

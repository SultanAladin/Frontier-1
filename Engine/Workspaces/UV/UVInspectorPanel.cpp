/*============================================================================================================================================
                                                          UVINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVInspectorPanel matching Experimental/UVEditor across Pack, Unwrap, UDIM, Shading & Check tabs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVInspectorPanel.h"

#include "Engine/Components/Controls/ControlTooltip.h"
#include "Engine/Components/Controls/DropdownSelector.h"
#include "Engine/Components/Controls/TextInputField.h"
#include "Engine/Workspaces/UV/UVPaneAppearance.h"
#include "Engine/Workspaces/UV/Solvers/SeamCutGenerator.h"
#include "Engine/Workspaces/UV/Operations/UVAuthoringOperations.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>

namespace Frontier::Workspaces
{

namespace
{

ImTextureData* QueryMatCapAtlasTexture() noexcept
{
    static ImTextureData Texture {};
    static bool Attempted = false;
    if (Attempted) return Texture.Pixels != nullptr ? &Texture : nullptr;
    Attempted = true;

    constexpr std::size_t AtlasBytes = 1024u * 1024u * 4u;
    constexpr int AtlasWidth = 1024;
    constexpr int AtlasHeight = 1024;
    constexpr const char* AssetRelativePath = "EngineContent/MatCaps/MatCapStudioAtlas.rgba";
    std::array<std::filesystem::path, 2u> Starts {{
        std::filesystem::current_path(),
        std::filesystem::absolute(std::filesystem::path(__FILE__)).parent_path()
    }};
    std::filesystem::path AssetPath {};
    for (std::filesystem::path Start : Starts)
    {
        for (uint32_t Depth = 0u; !Start.empty() && Depth < 12u; ++Depth)
        {
            const auto Candidate = Start / AssetRelativePath;
            if (std::filesystem::exists(Candidate))
            {
                AssetPath = Candidate;
                break;
            }
            const auto Parent = Start.parent_path();
            if (Parent == Start) break;
            Start = Parent;
        }
        if (!AssetPath.empty()) break;
    }
    if (AssetPath.empty()) return nullptr;

    std::ifstream Input(AssetPath, std::ios::binary | std::ios::ate);
    if (!Input || static_cast<std::size_t>(Input.tellg()) != AtlasBytes) return nullptr;
    Input.seekg(0, std::ios::beg);
    Texture.Create(ImTextureFormat_RGBA32, AtlasWidth, AtlasHeight);
    if (Texture.Pixels == nullptr) return nullptr;
    Input.read(reinterpret_cast<char*>(Texture.Pixels), static_cast<std::streamsize>(AtlasBytes));
    if (!Input) { Texture.DestroyPixels(); return nullptr; }
    Texture.UseColors = true;
    Texture.SetStatus(ImTextureStatus_WantCreate);
    return &Texture;
}

void ConstructMetricTrio(
    PixelSpace&                        Surface,
    Rectangle2D                        Box,
    const char*                        Val0, const char* Cap0, const char* Sub0,
    const char*                        Val1, const char* Cap1, const char* Sub1,
    const char*                        Val2, const char* Cap2, const char* Sub2,
    const UVAppearance::UVThemeColors& Theme,
    bool                               HighlightFirst = false)
{
    constexpr float Gap = 8.0f;
    const float CardW = (Box.Width - Gap * 2.0f) / 3.0f;
    const std::array<std::array<const char*, 3u>, 3u> Cols {{
        {{ Val0, Cap0, Sub0 }}, {{ Val1, Cap1, Sub1 }}, {{ Val2, Cap2, Sub2 }}
    }};
    for (uint32_t Idx = 0u; Idx < Cols.size(); ++Idx)
    {
        const float X = Box.X + static_cast<float>(Idx) * (CardW + Gap);
        const Rectangle2D Card{ X, Box.Y, CardW, Box.Height };
        const bool Highlight = HighlightFirst && Idx == 0u;
        Surface.FillRectangle(Card, Highlight ? ColorRgba{ 239, 244, 207, 255 } : ColorRgba{ 26, 26, 26, 255 }, 16.0f);
        Surface.StrokeRectangle(Card, Highlight ? ColorRgba{ 239, 244, 207, 255 } : ColorRgba{ 255, 255, 255, 15 }, 16.0f, 1.0f);
        const ColorRgba SubInk = Highlight ? ColorRgba{ 23, 25, 14, 166 } : Theme.TextMuted;
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 7.0f, Box.Y + 9.0f }, CardW - 14.0f, 8.5f, SubInk, Cols[Idx][1]);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 7.0f, Box.Y + 24.0f }, CardW - 14.0f, 18.0f,
            Highlight ? ColorRgba{ 23, 25, 14, 255 } : Theme.TextPrimary, Cols[Idx][0]);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 7.0f, Box.Y + 47.0f }, CardW - 14.0f, 8.0f, SubInk, Cols[Idx][2]);
    }
}

void ConstructPackingMetricCards(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const std::array<std::array<std::string_view, 3u>, 3u>& Cards,
    const UVAppearance::UVThemeColors& Theme)
{
    // Match the HTML .kpi-trio: three standalone cards with no enclosing card/background.
    constexpr float Gap = 8.0f;
    const float CardW = (Bounds.Width - Gap * 2.0f) / 3.0f;
    for (uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const float X = Bounds.X + static_cast<float>(Index) * (CardW + Gap);
        const Rectangle2D Card{ X, Bounds.Y, CardW, Bounds.Height };
        Surface.FillRectangle(Card, ColorRgba{ 26, 26, 26, 255 }, 16.0f);
        Surface.StrokeRectangle(Card, ColorRgba{ 255, 255, 255, 15 }, 16.0f, 1.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 7.0f, Bounds.Y + 9.0f }, CardW - 14.0f, 8.5f,
            Theme.TextMuted, Cards[Index][0]);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 7.0f, Bounds.Y + 24.0f }, CardW - 14.0f, 18.0f,
            Index == 0u ? Theme.AccentFill : Theme.TextPrimary, Cards[Index][1]);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { X + 7.0f, Bounds.Y + 48.0f }, CardW - 14.0f, 8.0f,
            Theme.TextMuted, Cards[Index][2]);
    }
}

[[nodiscard]] float EvaluateUDIMFillPercent(const GeometrySpace::PolygonNetwork& Net, uint32_t TargetUDIM) noexcept
{
    double AreaSum = 0.0;
    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Net.Faces.size()); ++FaceIdx)
    {
        const auto& Face = Net.Faces[FaceIdx];
        if (Face.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex)
        {
            continue;
        }
        const auto UV = Net.Edges[Face.BoundaryLoopEdgeIndex].ParameterUV;
        const int  TU = std::clamp(static_cast<int>(std::floor(UV.U)), 0, 9);
        const int  TV = std::max(0, static_cast<int>(std::floor(UV.V)));
        const uint32_t Code = static_cast<uint32_t>(1001 + TU + 10 * TV);
        if (Code == TargetUDIM)
        {
            AreaSum += std::max(0.0f, Net.EvaluateFaceSignedAreaUV(FaceIdx));
        }
    }
    return static_cast<float>(std::clamp(AreaSum * 100.0, 0.0, 100.0));
}

} // namespace

void UVInspectorPanel::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session       = TargetSession;
    ScrollOffsetY = 0.0f;
    CachedNetworkRevision = UINT64_MAX;
    CachedUVRevision = UINT64_MAX;
    CachedChartCount = 0u;
    CachedTabContentHeights.fill(0.0f);
    LayerNameEditor = {};
    EditingLayerOrdinal = 0u;
    ActiveLayerDropdownOpen = false;
    TargetUdimDropdownOpen = false;
    PackingScopeDropdownOpen = false;
    ShadingModeDropdownOpen = false;
    LayerColorPickerOpen = false;
    LiveUnwrapEnabled = false;
}

float UVInspectorPanel::RenderPackingTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    Rectangle2D             PopupClip,
    const PaneInputReading& Input,
    double                  Δt)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;
    const bool          Down    = Input.PrimaryPointer.Down;
    if (Input.EscapePressed)
    {
        ActiveLayerDropdownOpen = false;
        TargetUdimDropdownOpen = false;
        PackingScopeDropdownOpen = false;
        LayerColorPickerOpen = false;
    }

    float CurY = StartY;
    Rectangle2D ActiveLayerDropdownBounds{};
    Rectangle2D TargetUdimDropdownBounds{};
    Rectangle2D PackingScopeDropdownBounds{};

    const float FillPct     = EvaluateUDIMFillPercent(Session->Asset.Network, Session->ActiveUDIMNumber);
    const float RemPct      = Session->Asset.Layers.DeriveRemainderSharePercent();
    const auto  TileCfg     = Session->QueryUDIMTileSetting(Session->ActiveUDIMNumber);
    const auto  Islands     = SeamCutGenerator::PartitionConnectedUVIslands(Session->Asset.Network);
    const auto  SelectedFaces = Session->SelectionMask.CollectSelectedFaces();
    std::vector<uint8_t> FaceSelected(Session->Asset.Network.Faces.size(), 0u);
    for (const uint32_t FaceIndex : SelectedFaces)
    {
        if (FaceIndex < FaceSelected.size())
        {
            FaceSelected[FaceIndex] = 1u;
        }
    }

    uint32_t TotalIslandsInTile = 0u;
    uint32_t SelectedIslandsInTile = 0u;
    uint32_t SelectedFacesInTile = 0u;
    for (const UVIslandPartition& Island : Islands)
    {
        if (Island.AssignedUDIMNumber != Session->ActiveUDIMNumber)
        {
            continue;
        }
        ++TotalIslandsInTile;
        bool IslandSelected = false;
        for (const uint32_t FaceIndex : Island.FaceIndices)
        {
            if (FaceIndex < FaceSelected.size() && FaceSelected[FaceIndex] != 0u)
            {
                ++SelectedFacesInTile;
                IslandSelected = true;
            }
        }
        SelectedIslandsInTile += IslandSelected ? 1u : 0u;
    }

    char SelectedCount[32]{}, SelectedSub[48]{}, FillStr[24]{}, FillSub[48]{}, RemStr[24]{}, RemSub[32]{};
    std::snprintf(SelectedCount, sizeof(SelectedCount), "%u / %u", SelectedIslandsInTile, TotalIslandsInTile);
    if (SelectedFacesInTile > 0u)
    {
        std::snprintf(SelectedSub, sizeof(SelectedSub), "%u faces selected", SelectedFacesInTile);
    }
    else
    {
        std::snprintf(SelectedSub, sizeof(SelectedSub), "Objects in UDIM %u", Session->ActiveUDIMNumber);
    }
    std::snprintf(FillStr, sizeof(FillStr), "%.1f%%", FillPct);
    std::snprintf(FillSub, sizeof(FillSub), "%u charts · %u px", TotalIslandsInTile, TileCfg.ResolutionPx);
    std::snprintf(RemStr, sizeof(RemStr), "%.0f%%", RemPct);
    std::snprintf(RemSub, sizeof(RemSub), "%zu layers active", Session->Asset.Layers.InspectLayers().size());

    ConstructPackingMetricCards(
        Surface,
        { LeftX, CurY, InnerW, 66.0f },
        {{{ "Selected Islands", SelectedCount, SelectedSub },
          { "Tile Fill", FillStr, FillSub },
          { "Remainder", RemStr, RemSub }}},
        Theme);
    CurY += 82.0f;

    // Section 1: Importance Space Allocation, matching the HTML inspector's controls.
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 392.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 22.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 22.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;
        ConstructSectionHeading(Surface, SX, SY, SW, "Importance Space Allocation", Theme, {}, ControlIcon::LayersSlabs);
        SY += 45.0f;

        const auto Layers = Session->Asset.Layers.InspectLayers();
        const auto* CurrentLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
        const std::string ActiveLabel = CurrentLayer != nullptr
            ? CurrentLayer->LayerTitle + " (" + std::to_string(static_cast<uint32_t>(std::round(CurrentLayer->TargetSharePercent))) + "%)"
            : std::string("Unassigned Remainder");
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY }, 11.5f, Theme.TextMuted, "Active Layer");
        ActiveLayerDropdownBounds = { SX, SY + 17.0f, SW, 31.0f };
        const OptionalTooltip ActiveLayerTip = OptionalTooltip::FromTitled(
            "Active Layer", "Choose which priority allocation layer is active and select its faces in the workspace.");
        const ControlContact ActiveLayerButton = DropdownSelector::ConstructButton(
            Surface, ActiveLayerDropdownBounds, ActiveLabel.c_str(), ActiveLayerDropdownOpen, Input.PrimaryPointer, 1.0f,
            ControlIcon::LayersSlabs, ActiveLayerTip);
        if (ActiveLayerButton.Clicked)
        {
            ActiveLayerDropdownOpen = !ActiveLayerDropdownOpen;
            TargetUdimDropdownOpen = false;
        }
        SY += 55.0f;

        CurrentLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
        if (CurrentLayer != nullptr)
        {
            float NormShare = CurrentLayer->TargetSharePercent * 0.01f;
            char ShareVal[24]{};
            std::snprintf(ShareVal, sizeof(ShareVal), "%.0f%%", CurrentLayer->TargetSharePercent);
            const OptionalTooltip ShareTip = OptionalTooltip::FromTitled(
                "Space Share", "Set the active layer's target share of the UV packing area. Other explicit layer shares limit the available range.");
            if (ConstructSliderControl(
                    Surface, { SX, SY, SW, 46.0f }, "Space Share", ShareVal, NormShare, Theme, Cursor, Down, Pressed,
                    ColorRgba{ 0u, 0u, 0u, 0u }, ShareTip))
            {
                Session->AdjustSelectedLayerShare(NormShare * 100.0f);
            }
        }
        else
        {
            float FixedNorm = RemPct * 0.01f;
            ConstructSliderControl(
                Surface, { SX, SY, SW, 46.0f }, "Remainder Share (Auto)", RemStr, FixedNorm,
                Theme, Cursor, false, false,
                ColorRgba{ 0u, 0u, 0u, 0u },
                OptionalTooltip::FromText("Unassigned faces automatically receive the remaining allocation budget."));
        }
        SY += 54.0f;

        const Rectangle2D NameBounds{ SX, SY + 17.0f, SW, 28.0f };
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY }, 11.5f, Theme.TextMuted, "Layer Name");
        CurrentLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
        if (EditingLayerOrdinal != Session->SelectedLayerOrdinal)
        {
            ActiveLayerDropdownOpen = false;
            TargetUdimDropdownOpen = false;
            PackingScopeDropdownOpen = false;
            LayerColorPickerOpen = false;
            LayerNameEditor = {};
            EditingLayerOrdinal = Session->SelectedLayerOrdinal;
            if (CurrentLayer != nullptr)
            {
                LayerNameEditor.Begin(CurrentLayer->LayerTitle.c_str());
                LayerNameEditor.Active = false;
            }
        }
        const OptionalTooltip NameTip = OptionalTooltip::FromTitled(
            "Layer Name", "Rename the active priority layer. The updated name is shared with the UV outliner.");
        const ControlContact NameContact = TextInputField::Construct(
            Surface, NameBounds, LayerNameEditor, Input.PrimaryPointer, 11.5f, 1.0f, NameTip);
        if (NameContact.Clicked && CurrentLayer != nullptr)
        {
            LayerNameEditor.Begin(CurrentLayer->LayerTitle.c_str());
            EditingLayerOrdinal = Session->SelectedLayerOrdinal;
        }
        if (LayerNameEditor.Active)
        {
            WorkspaceAppearance::ForwardInputToTextBuffer(LayerNameEditor, Input, Δt);
            if (Input.EscapePressed)
            {
                LayerNameEditor.Cancel();
                if (CurrentLayer != nullptr && EditingLayerOrdinal == Session->SelectedLayerOrdinal)
                {
                    Session->RenameSelectedLayer(LayerNameEditor.Text);
                }
            }
            else if (Input.EnterPressed || (Input.PrimaryPointer.Pressed && !NameBounds.Contains(Cursor)))
            {
                LayerNameEditor.Finalize();
            }
            if (LayerNameEditor.Active && CurrentLayer != nullptr && LayerNameEditor.Length > 0u &&
                CurrentLayer->LayerTitle != LayerNameEditor.Text)
            {
                Session->RenameSelectedLayer(LayerNameEditor.Text);
            }
            if (LayerNameEditor.Finalized && CurrentLayer != nullptr)
            {
                Session->RenameSelectedLayer(LayerNameEditor.Text);
                LayerNameEditor.Finalized = false;
            }
        }
        SY += 62.0f;

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY + 9.0f }, 11.5f, Theme.TextMuted, "Layer Colour");
        constexpr std::array<Mathematics::Scalar3D, 6u> Swatches {{
            { 0.937f, 0.957f, 0.812f }, { 0.620f, 0.812f, 1.000f }, { 0.608f, 0.890f, 0.769f },
            { 0.949f, 0.757f, 0.490f }, { 0.898f, 0.596f, 0.608f }, { 0.769f, 0.655f, 0.906f },
        }};
        constexpr std::array<const char*, 6u> SwatchNames {{ "Studio Ivory", "Sky", "Mint", "Amber", "Coral", "Lavender" }};
        constexpr std::array<const char*, 6u> SwatchHex {{ "#EFF4CF", "#9ECFFF", "#9BE3C4", "#F2C17D", "#E5989B", "#C4A7E7" }};
        uint32_t ActiveSwatch = static_cast<uint32_t>(Swatches.size());
        if (CurrentLayer != nullptr)
        {
            for (uint32_t Slot = 0u; Slot < Swatches.size(); ++Slot)
            {
                const auto& Tint = CurrentLayer->LinearTintRGB;
                if (std::abs(Tint.X - Swatches[Slot].X) < 0.015f &&
                    std::abs(Tint.Y - Swatches[Slot].Y) < 0.015f &&
                    std::abs(Tint.Z - Swatches[Slot].Z) < 0.015f)
                {
                    ActiveSwatch = Slot;
                    break;
                }
            }
        }
        const float ColorPillW = std::min(210.0f, SW);
        const Rectangle2D ColorPill{ SX + SW - ColorPillW, SY, ColorPillW, 30.0f };
        Surface.FillRectangle(ColorPill, ColorRgba{ 20, 20, 20, 255 }, 15.0f);
        Surface.StrokeRectangle(ColorPill, ColorRgba{ 48, 48, 48, 255 }, 15.0f, 1.0f);
        const ColorRgba CurrentTint = CurrentLayer != nullptr
            ? LinearTintToRgba(CurrentLayer->LinearTintRGB)
            : ColorRgba{ 128, 128, 128, 255 };
        Surface.FillCircle({ ColorPill.X + 16.0f, ColorPill.Y + 15.0f }, 8.0f, CurrentTint);
        Surface.StrokeCircle({ ColorPill.X + 16.0f, ColorPill.Y + 15.0f }, 8.0f, ColorRgba{ 255, 255, 255, 70 }, 1.0f);
        const std::string_view ColorName = CurrentLayer == nullptr
            ? std::string_view("No Layer")
            : (ActiveSwatch < Swatches.size() ? std::string_view(SwatchNames[ActiveSwatch]) : std::string_view("Custom"));
        const std::string_view HexCode = ActiveSwatch < Swatches.size() ? std::string_view(SwatchHex[ActiveSwatch]) : std::string_view("CUSTOM");
        const float HexWidth = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.8f, HexCode);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { ColorPill.X + 31.0f, ColorPill.Y + 9.0f },
            ColorPill.Width - HexWidth - 49.0f, 10.5f, Theme.TextPrimary, ColorName);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { ColorPill.Right() - HexWidth - 9.0f, ColorPill.Y + 10.0f },
            8.8f, Theme.TextMuted, HexCode);
        const bool ColorPillHovered = ColorPill.Contains(Cursor);
        if (ColorPillHovered)
        {
            RecordControlTooltip(ColorPill, "Layer Colour", OptionalTooltip::FromTitled(
                "Layer Colour", "Choose a layer tint from the shared studio palette."), "Open the layer colour palette.");
        }
        if (ColorPillHovered && Pressed)
        {
            LayerColorPickerOpen = !LayerColorPickerOpen;
        }
        const Rectangle2D ColorPaletteBounds{ ColorPill.Right() - 152.0f, SY + 32.0f, 152.0f, 30.0f };
        if (LayerColorPickerOpen)
        {
            Surface.FillRectangle(ColorPaletteBounds, ColorRgba{ 20, 20, 20, 255 }, 12.0f);
            Surface.StrokeRectangle(ColorPaletteBounds, ColorRgba{ 48, 48, 48, 255 }, 12.0f, 1.0f);
            for (uint32_t Slot = 0u; Slot < Swatches.size(); ++Slot)
            {
                const Rectangle2D SwatchHit{ ColorPaletteBounds.X + 7.0f + static_cast<float>(Slot) * 23.0f,
                    ColorPaletteBounds.Y + 7.0f, 18.0f, 16.0f };
                const ColorRgba SwatchColor = LinearTintToRgba(Swatches[Slot]);
                Surface.FillRectangle(SwatchHit, SwatchColor, 8.0f);
                if (Slot == ActiveSwatch)
                {
                    Surface.StrokeRectangle(SwatchHit, Theme.PrimaryFill, 8.0f, 1.5f);
                }
                if (SwatchHit.Contains(Cursor))
                {
                    ControlAppearance::RecordHoveredTooltip(
                        SwatchHit, OptionalTooltip::FromTitled(SwatchNames[Slot], SwatchHex[Slot]));
                    if (Pressed && CurrentLayer != nullptr)
                    {
                        Session->AssignSelectedLayerTint(Swatches[Slot]);
                        LayerColorPickerOpen = false;
                    }
                }
            }
            if (Pressed && !ColorPaletteBounds.Contains(Cursor) && !ColorPill.Contains(Cursor))
            {
                LayerColorPickerOpen = false;
            }
        }
        SY += 68.0f;

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY }, 11.5f, Theme.TextMuted, "Target UDIM");
        CurrentLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
        const uint32_t TargetUDIM = CurrentLayer != nullptr ? CurrentLayer->TargetUDIMNumber : 0u;
        const std::string TargetLabel = TargetUDIM == 0u ? "Auto UDIM" : "UDIM " + std::to_string(TargetUDIM);
        TargetUdimDropdownBounds = { SX, SY + 17.0f, SW, 31.0f };
        const OptionalTooltip TargetTip = OptionalTooltip::FromTitled(
            "Target UDIM", "Pin the active layer's UV islands to a tile. Choosing an explicit UDIM translates its islands into that tile.");
        if (CurrentLayer != nullptr)
        {
            const ControlContact TargetButton = DropdownSelector::ConstructButton(
                Surface, TargetUdimDropdownBounds, TargetLabel.c_str(), TargetUdimDropdownOpen, Input.PrimaryPointer, 1.0f,
                ControlIcon::LayoutGrid, TargetTip);
            if (TargetButton.Clicked)
            {
                TargetUdimDropdownOpen = !TargetUdimDropdownOpen;
                ActiveLayerDropdownOpen = false;
                PackingScopeDropdownOpen = false;
            }
        }
        else
        {
            Surface.FillRectangle(TargetUdimDropdownBounds, Theme.WellBg, 14.0f);
            Surface.StrokeRectangle(TargetUdimDropdownBounds, Theme.BorderSubtle, 14.0f, 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { TargetUdimDropdownBounds.X + 12.0f, TargetUdimDropdownBounds.Y + 8.0f },
                TargetUdimDropdownBounds.Width - 24.0f, 9.5f, Theme.TextMuted, "Select a priority layer first");
            if (TargetUdimDropdownBounds.Contains(Cursor))
            {
                ControlAppearance::RecordHoveredTooltip(
                    TargetUdimDropdownBounds, OptionalTooltip::FromText("Choose a priority layer to set or move a Target UDIM."));
            }
            TargetUdimDropdownOpen = false;
        }
        SY += 52.0f;

        const float HalfW = (SW - 8.0f) * 0.5f;
        if (ConstructCapsuleButton(
                Surface, { SX, SY, HalfW, 33.0f }, "Bind Selected Faces", Theme, Cursor, Pressed,
                false, false, 9.6f, -1.0f,
                OptionalTooltip::FromText("Assign selected faces to the active priority allocation layer.")))
        {
            if (Session->SelectedLayerOrdinal != 0u)
            {
                Session->BindSelectionToPriorityLayer(Session->SelectedLayerOrdinal);
            }
        }
        if (ConstructCapsuleButton(
                Surface, { SX + HalfW + 8.0f, SY, HalfW, 33.0f }, "Unassign Selected", Theme, Cursor, Pressed,
                false, false, 9.6f, -1.0f,
                OptionalTooltip::FromText("Move selected faces back to the automatically budgeted remainder.")))
        {
            Session->BindSelectionToPriorityLayer(0u);
        }

        CurY += SecRect.Height + 10.0f;
    }

    // Section 2: Concave Packing Engine (.pz-section)
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 338.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 22.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 22.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Concave Packing Engine", Theme, "EXACT SHAPE", ControlIcon::LayoutGrid);
        SY += 24.0f;

        auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();

        // Match StudioControls.SegmentedRow: two options inside one recessed capsule track.
        const Rectangle2D BudgetTrack{ SX, SY, SW, 34.0f };
        Surface.FillRectangle(BudgetTrack, ColorRgba{ 11, 11, 11, 255 }, 17.0f);
        Surface.StrokeRectangle(BudgetTrack, ColorRgba{ 255, 255, 255, 22 }, 17.0f, 1.0f);
        const float BudgetGap = 4.0f;
        const float BudgetInnerW = (SW - 12.0f) * 0.5f;
        const bool StrictOn = Prefs.BudgetMode == Configuration::UVBudgetEnforcementMode::StrictRatio;
        const std::array<std::string_view, 2u> BudgetLabels {{ "Strict Ratio", "Fill Pockets" }};
        for (uint32_t Index = 0u; Index < BudgetLabels.size(); ++Index)
        {
            const bool Active = (Index == 0u) == StrictOn;
            const Rectangle2D Option{
                SX + 4.0f + static_cast<float>(Index) * (BudgetInnerW + BudgetGap), SY + 4.0f,
                BudgetInnerW, 26.0f };
            const bool Hovered = Option.Contains(Cursor);
            if (Active || Hovered)
            {
                Surface.FillRectangle(Option, Active ? ColorRgba{ 41, 41, 41, 255 } : ColorRgba{ 33, 33, 33, 255 }, 13.0f);
            }
            const float TextW = WorkspaceAppearance::MeasureWeightedTextWidth(
                Surface, FontWeightClass::Regular, 10.5f, BudgetLabels[Index]);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular,
                { Option.X + (Option.Width - TextW) * 0.5f, Option.Y + 7.0f },
                Option.Width - 12.0f, 10.5f, Active || Hovered ? Theme.TextPrimary : Theme.TextMuted, BudgetLabels[Index]);
            if (Hovered)
            {
                const auto Tip = Index == 0u
                    ? OptionalTooltip::FromText("Keep each layer within its explicit packing-space share.")
                    : OptionalTooltip::FromText("Allow suitable islands to expand into unused pockets after packing.");
                RecordControlTooltip(Option, BudgetLabels[Index], Tip, "Choose the packing budget enforcement mode.");
            }
            if (Hovered && Pressed)
            {
                if (Index == 0u)
                {
                    Prefs.BudgetMode = Configuration::UVBudgetEnforcementMode::StrictRatio;
                    Session->FillPocketsEnabled = false;
                }
                else
                {
                    Prefs.BudgetMode = Configuration::UVBudgetEnforcementMode::FillRemainderPockets;
                    Session->FillPocketsEnabled = true;
                }
            }
        }
        SY += 42.0f;

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY }, 11.5f, Theme.TextMuted, "UDIM Packing Scope");
        constexpr std::array<const char*, 4u> ScopeOptions {{
            "Pack into Active UDIM Tile", "Pack Per Assigned UDIM Tile", "Auto-Spill Overflow to New UDIMs", "Pack Selected Islands Only" }};
        const uint32_t ScopeIndex = static_cast<uint32_t>(Prefs.PackingScope);
        PackingScopeDropdownBounds = { SX, SY + 17.0f, SW, 31.0f };
        const OptionalTooltip ScopeTip = OptionalTooltip::FromTitled(
            "UDIM Packing Scope", "Choose which UDIM tiles are affected by the next pack operation.");
        const ControlContact ScopeButton = DropdownSelector::ConstructButton(
            Surface, PackingScopeDropdownBounds, ScopeOptions[std::min<uint32_t>(ScopeIndex, 3u)],
            PackingScopeDropdownOpen, Input.PrimaryPointer, 1.0f, ControlIcon::LayoutGrid, ScopeTip);
        if (ScopeButton.Clicked)
        {
            PackingScopeDropdownOpen = !PackingScopeDropdownOpen;
            ActiveLayerDropdownOpen = false;
            TargetUdimDropdownOpen = false;
        }
        SY += 55.0f;

        // Island Margin Padding (0..64 px)
        float MarginNorm = static_cast<float>(Prefs.IslandMarginPixels) / 64.0f;
        char  MarginTxt[24]{};
        std::snprintf(MarginTxt, sizeof(MarginTxt), "%u px", Prefs.IslandMarginPixels);
        if (ConstructSliderControl(
                Surface, { SX, SY, SW, 46.0f }, "Island Margin Padding", MarginTxt, MarginNorm, Theme, Cursor, Down, Pressed,
                ColorRgba{ 0u, 0u, 0u, 0u },
                OptionalTooltip::FromTitled("Island Margin Padding", "Set the edge gutter between packed islands in pixels.")))
        {
            Prefs.IslandMarginPixels = static_cast<uint32_t>(std::round(MarginNorm * 64.0f));
        }
        SY += 54.0f;

        // Match StudioControls.SegmentedRow: full-width segmented rotation choices.
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, SY }, 11.5f, Theme.TextMuted, "Rotation Steps");
        constexpr std::array<std::pair<const char*, uint32_t>, 4u> RotSteps {{
            { "0° Locked", 0u }, { "90° Ortho", 90u }, { "45° Oct", 45u }, { "15° Fine", 15u },
        }};
        const Rectangle2D RotationTrack{ SX, SY + 17.0f, SW, 34.0f };
        Surface.FillRectangle(RotationTrack, ColorRgba{ 11, 11, 11, 255 }, 17.0f);
        Surface.StrokeRectangle(RotationTrack, ColorRgba{ 255, 255, 255, 22 }, 17.0f, 1.0f);
        constexpr float RotationGap = 4.0f;
        const float RotationSegmentW = (SW - 12.0f) / 4.0f;
        for (uint32_t Idx = 0u; Idx < RotSteps.size(); ++Idx)
        {
            const Rectangle2D Segment{
                SX + 4.0f + static_cast<float>(Idx) * (RotationSegmentW + RotationGap), SY + 21.0f,
                RotationSegmentW, 26.0f };
            const bool Active = Prefs.RotationStepDegrees == RotSteps[Idx].second;
            const bool Hovered = Segment.Contains(Cursor);
            if (Active || Hovered)
            {
                Surface.FillRectangle(Segment, Active ? ColorRgba{ 41, 41, 41, 255 } : ColorRgba{ 33, 33, 33, 255 }, 13.0f);
            }
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { Segment.X + 5.0f, Segment.Y + 8.0f },
                Segment.Width - 10.0f, 9.8f, Active || Hovered ? Theme.TextPrimary : Theme.TextMuted, RotSteps[Idx].first);
            if (Hovered)
            {
                RecordControlTooltip(
                    Segment, RotSteps[Idx].first,
                    OptionalTooltip::FromText("Set the rotation increments considered by the island packer."),
                    "Choose a pack rotation step.");
            }
            if (Hovered && Pressed)
            {
                Prefs.RotationStepDegrees = RotSteps[Idx].second;
            }
        }
        SY += 59.0f;

        // Equalize Intra-Group Scale switch
        (void)ConstructToggleRow(
            Surface, { SX, SY, SW, 46.0f }, "Equalize Intra-Group Scale", Prefs.EqualizeIntraLayerScale, Theme, Cursor, Pressed,
            OptionalTooltip::FromText("Normalize texel density inside each priority layer before scaling."),
            "Normalize texel density inside each priority layer", "before scaling");
        SY += 53.0f;

        // Auto-spill scope routes through the multi-UDIM pack directive; other scopes use the single-tile pack path.
        const bool AutoSpillScope = Prefs.PackingScope == Configuration::UVPackingScopeMode::AutoSpillOverflowUDIMs;
        const char* PackButtonLabel = AutoSpillScope
            ? "Pack with Auto UDIM Spill (Shift+P)"
            : "Pack with Importance Budgets (Shift+P)";
        if (ConstructCapsuleButton(
                Surface,
                { SX, SY, SW, 34.0f },
                PackButtonLabel,
                Theme,
                Cursor,
                Pressed,
                false,
                true,
                10.5f,
                17.0f,
                OptionalTooltip::FromTitled(
                    AutoSpillScope ? "Pack with Auto UDIM Spill" : "Pack with Importance Budgets",
                    AutoSpillScope
                        ? "Pack priority-layer islands across successive UDIM tiles when the active tile fills."
                        : "Arrange UV islands using the active allocation shares.")))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::PackUVsConcaveSkyline, nullptr);
        }

        CurY += SecRect.Height + 12.0f;
    }

    // Repaint open dropdowns last so their sheets sit above later controls in the panel.
    auto DrawFittedOptionSheet = [&](
        Rectangle2D ButtonBounds,
        const char* const* Options,
        uint32_t OptionCount,
        uint32_t SelectedIndex,
        bool& IsOpen,
        auto&& OnChoice)
    {
        if (!IsOpen || OptionCount == 0u)
        {
            return;
        }
        Rectangle2D SheetBounds = DropdownSelector::QuerySheetBounds(ButtonBounds, OptionCount);
        Rectangle2D SheetAnchor = ButtonBounds;
        if (SheetBounds.Bottom() > PopupClip.Bottom())
        {
            SheetAnchor.Y = ButtonBounds.Y - SheetBounds.Height - 2.0f * ControlMetrics::DropdownPaddingY - ButtonBounds.Height;
            const Rectangle2D AboveSheet = DropdownSelector::QuerySheetBounds(SheetAnchor, OptionCount);
            if (AboveSheet.Y >= PopupClip.Y)
            {
                SheetBounds = AboveSheet;
            }
            else
            {
                SheetAnchor = ButtonBounds;
            }
        }
        uint32_t ChosenIndex = SelectedIndex;
        const ControlContact Contact = DropdownSelector::ConstructOptionSheet(
            Surface, SheetAnchor, Options, OptionCount, SelectedIndex, Input.PrimaryPointer, ChosenIndex);
        if (Contact.Clicked)
        {
            OnChoice(ChosenIndex);
            IsOpen = false;
        }
        else if (Input.PrimaryPointer.Released && !ButtonBounds.Contains(Cursor) && !SheetBounds.Contains(Cursor))
        {
            IsOpen = false;
        }
    };

    const auto LayersForPopup = Session->Asset.Layers.InspectLayers();
    std::vector<std::string> ActiveLabelStorage{ "Unassigned Remainder" };
    for (const auto& Layer : LayersForPopup)
    {
        ActiveLabelStorage.emplace_back(
            Layer.LayerTitle + " (" + std::to_string(static_cast<uint32_t>(std::round(Layer.TargetSharePercent))) + "%)");
    }
    std::vector<const char*> ActivePopupOptions{};
    for (const std::string& Label : ActiveLabelStorage)
    {
        ActivePopupOptions.push_back(Label.c_str());
    }
    uint32_t ActivePopupIndex = 0u;
    for (uint32_t Index = 0u; Index < LayersForPopup.size(); ++Index)
    {
        if (LayersForPopup[Index].LayerOrdinal == Session->SelectedLayerOrdinal)
        {
            ActivePopupIndex = Index + 1u;
            break;
        }
    }
    DrawFittedOptionSheet(
        ActiveLayerDropdownBounds, ActivePopupOptions.data(), static_cast<uint32_t>(ActivePopupOptions.size()),
        ActivePopupIndex, ActiveLayerDropdownOpen,
        [&](uint32_t Chosen)
        {
            if (Chosen < ActiveLabelStorage.size())
            {
                Session->SelectPriorityLayer(Chosen == 0u ? 0u : LayersForPopup[Chosen - 1u].LayerOrdinal);
            }
        });

    std::vector<uint32_t> TilePopupOptions{ 0u };
    for (const uint32_t Tile : Session->Asset.UDIMTiles)
    {
        if (std::find(TilePopupOptions.begin(), TilePopupOptions.end(), Tile) == TilePopupOptions.end())
        {
            TilePopupOptions.push_back(Tile);
        }
    }
    if (const auto* TargetLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
        TargetLayer != nullptr && TargetLayer->TargetUDIMNumber >= 1001u &&
        std::find(TilePopupOptions.begin(), TilePopupOptions.end(), TargetLayer->TargetUDIMNumber) == TilePopupOptions.end())
    {
        TilePopupOptions.push_back(TargetLayer->TargetUDIMNumber);
    }
    std::sort(TilePopupOptions.begin() + 1, TilePopupOptions.end());
    std::vector<std::string> TileLabelStorage{};
    std::vector<const char*> TilePopupLabels{};
    for (const uint32_t Tile : TilePopupOptions)
    {
        TileLabelStorage.emplace_back(Tile == 0u ? "Auto UDIM" : "UDIM " + std::to_string(Tile));
    }
    for (const std::string& Label : TileLabelStorage)
    {
        TilePopupLabels.push_back(Label.c_str());
    }
    const auto* PopupLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
    uint32_t TilePopupIndex = 0u;
    if (PopupLayer != nullptr && PopupLayer->TargetUDIMNumber != 0u)
    {
        const auto It = std::find(TilePopupOptions.begin(), TilePopupOptions.end(), PopupLayer->TargetUDIMNumber);
        if (It != TilePopupOptions.end())
        {
            TilePopupIndex = static_cast<uint32_t>(std::distance(TilePopupOptions.begin(), It));
        }
    }
    DrawFittedOptionSheet(
        TargetUdimDropdownBounds, TilePopupLabels.data(), static_cast<uint32_t>(TilePopupLabels.size()),
        TilePopupIndex, TargetUdimDropdownOpen,
        [&](uint32_t Chosen)
        {
            if (PopupLayer != nullptr && Chosen < TilePopupOptions.size())
            {
                (void)Session->MoveSelectedLayerToUDIM(TilePopupOptions[Chosen]);
            }
        });

    constexpr std::array<const char*, 4u> ScopePopupOptions {{
        "Pack into Active UDIM Tile", "Pack Per Assigned UDIM Tile", "Auto-Spill Overflow to New UDIMs", "Pack Selected Islands Only" }};
    const uint32_t ScopePopupIndex = static_cast<uint32_t>(Session->WorkspaceConfig.InspectUVPreferences().PackingScope);
    DrawFittedOptionSheet(
        PackingScopeDropdownBounds, ScopePopupOptions.data(), static_cast<uint32_t>(ScopePopupOptions.size()),
        std::min<uint32_t>(ScopePopupIndex, static_cast<uint32_t>(ScopePopupOptions.size() - 1u)),
        PackingScopeDropdownOpen,
        [&](uint32_t Chosen)
        {
            if (Chosen < ScopePopupOptions.size())
            {
                Session->WorkspaceConfig.AccessUVPreferences().PackingScope =
                    static_cast<Configuration::UVPackingScopeMode>(Chosen);
            }
        });

    return CurY - StartY;
}

float UVInspectorPanel::RenderUnwrapTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;
    const bool          Down    = Input.PrimaryPointer.Down;

    float CurY = StartY;

    // Transform · 2D UV summary card (same placement and data hierarchy as the HTML inspector).
    {
        const Rectangle2D Card{ LeftX, CurY, InnerW, 228.0f };
        Surface.FillRectangle(Card, Theme.SectionBg, 22.0f);
        Surface.StrokeRectangle(Card, Theme.BorderSubtle, 22.0f, 1.0f);
        const float SX = Card.X + 16.0f;
        const float SW = Card.Width - 32.0f;
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SX, Card.Y + 14.0f }, 12.5f, Theme.TextPrimary, "Transform · 2D UV");
        const char* ScopeTag = Session->SelectionMask.CollectSelectedFaces().empty() ? "UV SPACE · ALL" : "UV SPACE · SELECTED";
        const float TagW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.3f, ScopeTag);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { Card.Right() - 16.0f - TagW, Card.Y + 16.0f }, 8.3f, Theme.TextMuted, ScopeTag);

        const float LabelW = 68.0f;
        const float CellW = (SW - LabelW - 12.0f) / 3.0f;
        const float Ux = SX + LabelW;
        const float Vx = Ux + CellW + 6.0f;
        const float TileX = Vx + CellW + 6.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX, Card.Y + 43.0f }, 8.0f, Theme.TextFaint, "COMPONENT");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Ux, Card.Y + 43.0f }, 8.0f, ColorRgba{ 202, 153, 148, 255 }, "U");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Vx, Card.Y + 43.0f }, 8.0f, ColorRgba{ 151, 187, 157, 255 }, "V");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { TileX, Card.Y + 43.0f }, 8.0f, ColorRgba{ 142, 171, 216, 255 }, "UDIM");

        std::vector<uint32_t> TransformFaces = Session->SelectionMask.CollectSelectedFaces();
        if (TransformFaces.empty())
        {
            TransformFaces.resize(Session->Asset.Network.Faces.size());
            std::iota(TransformFaces.begin(), TransformFaces.end(), 0u);
        }
        float MinU = std::numeric_limits<float>::max(), MinV = std::numeric_limits<float>::max();
        float MaxU = std::numeric_limits<float>::lowest(), MaxV = std::numeric_limits<float>::lowest();
        for (const uint32_t FaceIndex : TransformFaces)
        {
            if (FaceIndex >= Session->Asset.Network.Faces.size()) continue;
            const auto& Face = Session->Asset.Network.Faces[FaceIndex];
            uint32_t Edge = Face.BoundaryLoopEdgeIndex;
            for (uint16_t Corner = 0u; Corner < Face.CornerCount && Edge < Session->Asset.Network.Edges.size(); ++Corner)
            {
                const auto& UV = Session->Asset.Network.Edges[Edge].ParameterUV;
                MinU = std::min(MinU, UV.U); MinV = std::min(MinV, UV.V);
                MaxU = std::max(MaxU, UV.U); MaxV = std::max(MaxV, UV.V);
                Edge = Session->Asset.Network.Edges[Edge].NextEdgeIndex;
            }
        }
        if (!std::isfinite(MinU) || !std::isfinite(MinV)) { MinU = MinV = 0.0f; MaxU = MaxV = 1.0f; }
        const float CenterU = (MinU + MaxU) * 0.5f;
        const float CenterV = (MinV + MaxV) * 0.5f;
        const float SpanU = MaxU - MinU, SpanV = MaxV - MinV;
        const std::array<const char*, 3u> RowLabels {{ "Position", "Extent", "Orient" }};
        char PositionU[24]{}, PositionV[24]{}, PositionTile[16]{};
        char ExtentU[24]{}, ExtentV[24]{}, Aspect[24]{};
        char OrientU[24]{}, OrientV[24]{}, OrientTile[16]{};
        std::snprintf(PositionU, sizeof(PositionU), "%.2f", CenterU - static_cast<float>((Session->ActiveUDIMNumber - 1001u) % 10u));
        std::snprintf(PositionV, sizeof(PositionV), "%.2f", CenterV - static_cast<float>((Session->ActiveUDIMNumber - 1001u) / 10u));
        std::snprintf(PositionTile, sizeof(PositionTile), "%u", Session->ActiveUDIMNumber);
        std::snprintf(ExtentU, sizeof(ExtentU), "%.2f", SpanU);
        std::snprintf(ExtentV, sizeof(ExtentV), "%.2f", SpanV);
        std::snprintf(Aspect, sizeof(Aspect), "%.2f", SpanV > 1.0e-5f ? SpanU / SpanV : 0.0f);
        std::snprintf(OrientU, sizeof(OrientU), "0.0°");
        std::snprintf(OrientV, sizeof(OrientV), "%.1f%%", std::max(0.0f, (Session->LatestMetrics.MeanL2Stretch - 1.0f) * 100.0f));
        std::snprintf(OrientTile, sizeof(OrientTile), "0");
        const std::array<std::array<const char*, 3u>, 3u> Values {{
            {{ PositionU, PositionV, PositionTile }}, {{ ExtentU, ExtentV, Aspect }}, {{ OrientU, OrientV, OrientTile }}
        }};
        for (uint32_t Row = 0u; Row < 3u; ++Row)
        {
            const float Y = Card.Y + 64.0f + static_cast<float>(Row) * 34.0f;
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX, Y + 6.0f }, 9.0f, Theme.TextMuted, RowLabels[Row]);
            const std::array<float, 3u> Xs {{ Ux, Vx, TileX }};
            for (uint32_t Col = 0u; Col < 3u; ++Col)
            {
                const Rectangle2D Cell{ Xs[Col], Y, CellW, 26.0f };
                Surface.FillRectangle(Cell, ColorRgba{ 19, 19, 19, 255 }, 7.0f);
                Surface.StrokeRectangle(Cell, ColorRgba{ 45, 45, 45, 255 }, 7.0f, 1.0f);
                WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular,
                    { Cell.X + 6.0f, Cell.Y + 7.0f }, Cell.Width - 12.0f, 9.0f,
                    Col == 2u ? ColorRgba{ 151, 180, 219, 255 } : Theme.TextPrimary, Values[Row][Col]);
            }
        }
        const float ButtonGap = 7.0f;
        const float ButtonW = (SW - ButtonGap * 2.0f) / 3.0f;
        const float ButtonY = Card.Y + 177.0f;
        const Rectangle2D CenterBtn{ SX, ButtonY, ButtonW, 28.0f };
        const Rectangle2D RotateBtn{ SX + ButtonW + ButtonGap, ButtonY, ButtonW, 28.0f };
        const Rectangle2D FrameBtn{ SX + (ButtonW + ButtonGap) * 2.0f, ButtonY, ButtonW, 28.0f };
        auto TransformUVs = [&](Mathematics::UVAxes Delta, float Angle, const char* Tag)
        {
            UVAuthoringOperations::TransformSelectedFaceUVs(
                Session->Asset.Network, TransformFaces, { CenterU, CenterV }, Delta, Angle, { 1.0f, 1.0f });
            Session->FinalizeInteractiveUVRevision(Tag);
            Session->FramebuffersDirty = true;
        };
        if (ConstructCapsuleButton(Surface, CenterBtn, "Center Tile", Theme, Cursor, Pressed, false, false, 9.0f,
            -1.0f, OptionalTooltip::FromText("Center UVs in the active UDIM tile.")))
        {
            const float TileU = static_cast<float>((Session->ActiveUDIMNumber - 1001u) % 10u);
            const float TileV = static_cast<float>((Session->ActiveUDIMNumber - 1001u) / 10u);
            TransformUVs({ TileU + 0.5f - CenterU, TileV + 0.5f - CenterV }, 0.0f, "CenterUVTile");
        }
        if (ConstructCapsuleButton(Surface, RotateBtn, "Rotate 90°", Theme, Cursor, Pressed, false, false, 9.0f,
            -1.0f, OptionalTooltip::FromText("Rotate the active UV selection by 90 degrees.")))
        {
            TransformUVs({}, 1.57079632679f, "RotateUV90");
        }
        if (ConstructCapsuleButton(Surface, FrameBtn, "Frame UV", Theme, Cursor, Pressed, false, false, 9.0f,
            -1.0f, OptionalTooltip::FromText("Frame the active UV selection in the viewport.")))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::FitActiveSelection, nullptr);
        }
        CurY += Card.Height + 10.0f;
    }

    // 1. Unwrap, projections, quad strips and similar stacking.
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 330.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 22.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 22.0f, 1.0f);
        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float SY = SecRect.Y + 14.0f;
        const std::string Status = Session->SelectionMask.CollectSelectedFaces().empty() ? "SURFACE" : "SELECTION";
        ConstructSectionHeading(Surface, SX, SY, SW, "Unwrap, Quads & Stacking", Theme, Status.c_str(), ControlIcon::LayoutGrid);
        SY += 24.0f;
        const float ActionGap = 7.0f;
        const float ActionW = (SW - ActionGap) * 0.5f;
        if (ConstructCapsuleButton(Surface, { SX, SY, ActionW, 31.0f }, "LSCM Unwrap (U)", Theme, Cursor, Pressed, false, true, 10.0f,
            -1.0f, OptionalTooltip::FromText("Unwrap selected faces with least-squares conformal mapping.")))
            (void)Session->ExecuteDirective(UVCommandDirective::UnwrapConformal, nullptr);
        if (ConstructCapsuleButton(Surface, { SX + ActionW + ActionGap, SY, ActionW, 31.0f }, "Smart UV Project", Theme, Cursor, Pressed, false, false, 10.0f,
            -1.0f, OptionalTooltip::FromText("Automatically segment and project the selected surface.")))
            (void)Session->ExecuteDirective(UVCommandDirective::ProjectSmartUV, nullptr);
        SY += 42.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX, SY }, 9.5f, Theme.TextMuted, "QUAD STRIP & SIMILAR STACKING");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX + SW - 35.0f, SY }, 8.5f, Theme.TextFaint, "1-click");
        SY += 16.0f;
        const float TripleGap = 4.0f;
        const float TripleW = (SW - TripleGap * 2.0f) / 3.0f;
        const std::array<std::pair<const char*, UVCommandDirective>, 3u> QuadActions {{
            { "Follow Quads", UVCommandDirective::FollowActiveQuads },
            { "Stack Similar", UVCommandDirective::StackSimilarIslands },
            { "Unstack", UVCommandDirective::UnstackOverlapping },
        }};
        for (uint32_t Index = 0u; Index < QuadActions.size(); ++Index)
        {
            if (ConstructCapsuleButton(Surface, { SX + static_cast<float>(Index) * (TripleW + TripleGap), SY, TripleW, 30.0f },
                QuadActions[Index].first, Theme, Cursor, Pressed, false, false, 8.7f, 10.0f,
                OptionalTooltip::FromText("Run the selected quad or stacking operation.")))
                (void)Session->ExecuteDirective(QuadActions[Index].second, nullptr);
        }
        SY += 42.0f;

        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX, SY }, 9.5f, Theme.TextMuted, "ANALYTIC PROJECTIONS");
        char AngleMeta[16]{};
        std::snprintf(AngleMeta, sizeof(AngleMeta), "%.0f°", Session->SharpAngleThreshold);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,
            { SX + SW - 34.0f, SY }, 8.5f, Theme.TextFaint, AngleMeta);
        SY += 16.0f;
        constexpr std::array<ControlIcon, 5u> ProjectionIcons {{
            ControlIcon::CubeShape, ControlIcon::LayersSlabs, ControlIcon::LayoutGrid, ControlIcon::CameraBody, ControlIcon::RefreshCycle,
        }};
        constexpr std::array<const char*, 5u> ProjectionTips {{
            "Cube projection", "Cylinder projection", "Sphere projection", "Project from camera view", "Reset face UVs to the active tile",
        }};
        constexpr std::array<UVCommandDirective, 5u> ProjectionOps {{
            UVCommandDirective::ProjectCubicBox, UVCommandDirective::ProjectCylindrical,
            UVCommandDirective::ProjectSpherical, UVCommandDirective::ProjectCameraView, UVCommandDirective::UnwrapConformal,
        }};
        const float ProjectionGap = 5.0f;
        const float ProjectionW = (SW - ProjectionGap * 4.0f) / 5.0f;
        for (uint32_t Index = 0u; Index < ProjectionIcons.size(); ++Index)
        {
            const Rectangle2D Button{ SX + static_cast<float>(Index) * (ProjectionW + ProjectionGap), SY, ProjectionW, 30.0f };
            const bool Hovered = Button.Contains(Cursor);
            Surface.FillRectangle(Button, Hovered ? Theme.HoverBg : ColorRgba{ 20, 20, 20, 255 }, 10.0f);
            Surface.StrokeRectangle(Button, Theme.BorderSubtle, 10.0f, 1.0f);
            VectorIconRepository::ConstructControl(Surface, ProjectionIcons[Index],
                VectorIconPlacement{ Button.X + (Button.Width - 15.0f) * 0.5f, Button.Y + 7.0f, 15.0f,
                    Hovered ? Theme.TextPrimary : Theme.TextMuted, 1.8f });
            if (Hovered)
            {
                RecordControlTooltip(Button, ProjectionTips[Index], OptionalTooltip::FromText(ProjectionTips[Index]), ProjectionTips[Index]);
                if (Pressed) (void)Session->ExecuteDirective(ProjectionOps[Index], nullptr);
            }
        }
        SY += 40.0f;

        float AngleNorm = std::clamp((Session->SharpAngleThreshold - 10.0f) / 79.0f, 0.0f, 1.0f);
        char AngleTxt[24]{};
        std::snprintf(AngleTxt, sizeof(AngleTxt), "%.0f°", Session->SharpAngleThreshold);
        if (ConstructSliderControl(Surface, { SX, SY, SW, 46.0f }, "Smart Angle Limit", AngleTxt, AngleNorm, Theme, Cursor, Down, Pressed))
            Session->SharpAngleThreshold = 10.0f + AngleNorm * 79.0f;
        SY += 52.0f;
        (void)ConstructToggleRow(Surface, { SX, SY, SW, 42.0f }, "Live Unwrap", LiveUnwrapEnabled,
            Theme, Cursor, Pressed, OptionalTooltip::FromText("Continuously re-solve LSCM while moving pins."),
            "Continuously re-solve LSCM while moving pins", "");
        CurY += SecRect.Height + 10.0f;
    }

    // Section 2: Polygon Network, Pins & Seams
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 134.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Pin, Stitch & Seam Operations", Theme);
        SY += 24.0f;

        const float ThirdW = (SW - 8.0f) / 3.0f;
        if (ConstructCapsuleButton(Surface, { SX, SY, ThirdW, 26.0f }, "Pin UVs", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::PinSelectedUVs, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + ThirdW + 4.0f, SY, ThirdW, 26.0f }, "Unpin UVs", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::UnpinSelectedUVs, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + (ThirdW + 4.0f) * 2.0f, SY, ThirdW, 26.0f }, "Rectify", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::RectifySelectedQuads, nullptr);
        }
        SY += 32.0f;

        if (ConstructCapsuleButton(Surface, { SX, SY, ThirdW, 26.0f }, "Mark Seams", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::MarkSharpSeams, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + ThirdW + 4.0f, SY, ThirdW, 26.0f }, "Clear Seams", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::ClearSelectedSeams, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + (ThirdW + 4.0f) * 2.0f, SY, ThirdW, 26.0f }, "Stitch", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::StitchSelectedSeams, nullptr);
        }

        CurY += SecRect.Height + 10.0f;
    }

    // Section 3: Relax & Texel Density
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 146.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "GPU Stretch Relax & Texel Density", Theme);
        SY += 24.0f;

        float IterNorm = static_cast<float>(Session->RelaxIterations) / 64.0f;
        char  IterTxt[24]{};
        std::snprintf(IterTxt, sizeof(IterTxt), "%u passes", Session->RelaxIterations);
        if (ConstructSliderControl(Surface, { SX, SY, SW, 46.0f }, "Relax Passes", IterTxt, IterNorm, Theme, Cursor, Down, Pressed))
        {
            Session->RelaxIterations = std::max(1u, static_cast<uint32_t>(std::round(IterNorm * 64.0f)));
        }
        SY += 50.0f;

        const float HalfW = (SW - 6.0f) * 0.5f;
        if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 28.0f }, "Relax Stretch GPU", Theme, Cursor, Pressed, false, true, 9.8f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::RelaxStretchGPU, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 28.0f }, "Equalize Density", Theme, Cursor, Pressed, false, false, 9.8f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::EqualizeTexelDensity, nullptr);
        }

        CurY += SecRect.Height + 12.0f;
    }

    return CurY - StartY;
}

float UVInspectorPanel::RenderUDIMTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme = QueryThemeColors();
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool Pressed = Input.PrimaryPointer.Pressed;
    float CurY = StartY;
    const auto IslandsByUDIM = SeamCutGenerator::PartitionConnectedUVIslands(Session->Asset.Network);

    char ActiveValue[24]{}, TileCount[24]{}, DensityValue[24]{}, ActiveSub[32]{}, TotalSub[32]{};
    std::snprintf(ActiveValue, sizeof(ActiveValue), "%u", Session->ActiveUDIMNumber);
    std::snprintf(TileCount, sizeof(TileCount), "%zu", Session->Asset.UDIMTiles.size());
    const auto ActiveCfg = Session->QueryUDIMTileSetting(Session->ActiveUDIMNumber);
    std::snprintf(DensityValue, sizeof(DensityValue), "%.1f",
        Session->WorkspaceConfig.InspectUVPreferences().TargetTexelDensity * 0.01f);
    double TotalMP = 0.0;
    for (const uint32_t Tile : Session->Asset.UDIMTiles)
    {
        const uint32_t Res = Session->QueryUDIMTileSetting(Tile).ResolutionPx;
        TotalMP += static_cast<double>(Res) * Res / 1.0e6;
    }
    const double ActiveMP = static_cast<double>(ActiveCfg.ResolutionPx) * ActiveCfg.ResolutionPx / 1.0e6;
    if (ActiveCfg.ResolutionPx >= 1024u)
        std::snprintf(ActiveSub, sizeof(ActiveSub), "%uK · %.1f MP", ActiveCfg.ResolutionPx / 1024u, ActiveMP);
    else
        std::snprintf(ActiveSub, sizeof(ActiveSub), "%u px · %.1f MP", ActiveCfg.ResolutionPx, ActiveMP);
    std::snprintf(TotalSub, sizeof(TotalSub), "%.1f MP total", TotalMP);

    ConstructMetricTrio(
        Surface, { LeftX, CurY, InnerW, 60.0f },
        ActiveValue, "Active UDIM", ActiveSub,
        TileCount, "Atlas Tiles", TotalSub,
        DensityValue, "Texel Density", "px/cm average",
        Theme, true);
    CurY += 70.0f;

    // UDIM Atlas table, following the HTML section card, compact action toolbar, and chart rows.
    {
        const std::size_t TileCountSize = Session->Asset.UDIMTiles.size();
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 106.0f + static_cast<float>(TileCountSize) * 40.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 22.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 22.0f, 1.0f);
        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float SY = SecRect.Y + 14.0f;
        char TileBadge[24]{};
        std::snprintf(TileBadge, sizeof(TileBadge), "%zu TILE%s", TileCountSize, TileCountSize == 1u ? "" : "S");
        ConstructSectionHeading(Surface, SX, SY, SW, "UDIM Tiles", Theme, TileBadge, ControlIcon::LayoutGrid);
        SY += 24.0f;

        const float ToolH = 28.0f;
        const float AddW = std::min(104.0f, SW - 124.0f);
        const Rectangle2D AddBounds{ SX, SY, AddW, ToolH };
        const bool AddHovered = AddBounds.Contains(Cursor);
        const ColorRgba AddFill = AddHovered ? Theme.PrimaryHover : Theme.PrimaryFill;
        Surface.FillRectangle(AddBounds, AddFill, 14.0f);
        Surface.StrokeRectangle(AddBounds, AddFill, 14.0f, 1.0f);
        VectorIconRepository::ConstructControl(
            Surface, ControlIcon::PlusAdd, VectorIconPlacement{ AddBounds.X + 9.0f, AddBounds.Y + 7.0f, 14.0f, Theme.PrimaryInk, 1.8f });
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium,
            { AddBounds.X + 28.0f, AddBounds.Y + 9.0f }, AddBounds.Width - 34.0f, 9.2f, Theme.PrimaryInk, "Add UDIM Tile");
        if (AddHovered)
            RecordControlTooltip(AddBounds, "Add UDIM Tile", OptionalTooltip::FromText("Allocate the next sequential UDIM tile."),
                "Allocate the next sequential UDIM tile.");
        if (AddHovered && Pressed) (void)Session->ConstructNextUDIMTile();
        // Reserve space for the icon added to the existing shared capsule button label.
        const float IconW = 28.0f;
        const float IconGap = 5.0f;
        const float IconWTotal = 4.0f * IconW + 3.0f * IconGap;
        const float IconStart = SX + SW - IconWTotal;
        const std::array<ControlIcon, 4u> TileTools {{
            ControlIcon::LayersSlabs, ControlIcon::GaugeRate, ControlIcon::LayoutSplit, ControlIcon::ExternalOpen,
        }};
        const std::array<const char*, 4u> ToolTips {{
            "Move selected charts to the active UDIM", "Match texel density across UDIM tiles",
            "Frame all allocated UDIM tiles", "Load a reference texture",
        }};
        for (uint32_t Index = 0u; Index < TileTools.size(); ++Index)
        {
            const Rectangle2D Button{ IconStart + static_cast<float>(Index) * (IconW + IconGap), SY, IconW, ToolH };
            const bool Hovered = Button.Contains(Cursor);
            Surface.FillRectangle(Button, Hovered ? Theme.HoverBg : ColorRgba{ 31, 31, 31, 255 }, 10.0f);
            Surface.StrokeRectangle(Button, Theme.BorderSubtle, 10.0f, 1.0f);
            VectorIconRepository::ConstructControl(
                Surface, TileTools[Index], VectorIconPlacement{
                    Button.X + 7.0f, Button.Y + 7.0f, 14.0f, Hovered ? Theme.TextPrimary : Theme.TextMuted, 1.8f });
            if (Hovered)
            {
                RecordControlTooltip(Button, ToolTips[Index], OptionalTooltip::FromText(ToolTips[Index]), ToolTips[Index]);
            }
            if (Hovered && Pressed)
            {
                if (Index == 0u) (void)Session->MoveSelectedLayerToUDIM(Session->ActiveUDIMNumber);
                else if (Index == 1u) (void)Session->ExecuteDirective(UVCommandDirective::EqualizeTexelDensity, nullptr);
                else if (Index == 2u) (void)Session->ExecuteDirective(UVCommandDirective::FitActiveSelection, nullptr);
                // The upload control shares its affordance with the HTML reference; texture import is handled by the viewport asset flow.
            }
        }
        SY += 36.0f;

        const Rectangle2D TableBounds{ SX, SY, SW, 18.0f + static_cast<float>(TileCountSize) * 40.0f };
        Surface.FillRectangle(TableBounds, ColorRgba{ 23, 23, 23, 255 }, 12.0f);
        Surface.StrokeRectangle(TableBounds, ColorRgba{ 46, 46, 46, 255 }, 12.0f, 1.0f);
        Surface.FillRectangle({ TableBounds.X + 1.0f, TableBounds.Y + 1.0f, TableBounds.Width - 2.0f, 17.0f },
            ColorRgba{ 28, 28, 28, 255 }, 11.0f);
        // Compact table header.
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX + 25.0f, SY + 3.0f }, 8.2f, Theme.TextMuted, "TILE");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX + SW - 94.0f, SY + 3.0f }, 8.2f, Theme.TextMuted, "CHARTS");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX + SW - 47.0f, SY + 3.0f }, 8.2f, Theme.TextMuted, "RES");
        SY += 18.0f;

        std::vector<uint32_t> SortedTiles = Session->Asset.UDIMTiles;
        std::sort(SortedTiles.begin(), SortedTiles.end());
        constexpr std::array<uint32_t, 5u> ResolutionCycle {{ 512u, 1024u, 2048u, 4096u, 8192u }};
        for (const uint32_t TileCode : SortedTiles)
        {
            const bool IsActive = Session->ActiveUDIMNumber == TileCode;
            const auto Cfg = Session->QueryUDIMTileSetting(TileCode);
            const float Occupancy = EvaluateUDIMFillPercent(Session->Asset.Network, TileCode);
            const uint32_t ChartCount = static_cast<uint32_t>(std::count_if(
                IslandsByUDIM.begin(), IslandsByUDIM.end(), [TileCode](const UVIslandPartition& Island)
                { return Island.AssignedUDIMNumber == TileCode; }));
            const Rectangle2D Row{ SX + 1.0f, SY, SW - 2.0f, 40.0f };
            const bool Hovered = Row.Contains(Cursor);
            if (IsActive) Surface.FillRectangle(Row, ColorRgba{ 31, 31, 31, 255 }, 0.0f);
            Surface.StrokeLine({ Row.X + 8.0f, Row.Bottom() }, { Row.Right() - 8.0f, Row.Bottom() }, ColorRgba{ 37, 37, 37, 255 }, 1.0f);
            Surface.FillCircle({ Row.X + 14.0f, Row.Y + 20.0f }, 5.0f, IsActive ? ColorRgba{ 239, 244, 207, 255 } : Theme.TextMuted);
            const uint32_t U = (TileCode - 1001u) % 10u;
            const uint32_t V = (TileCode - 1001u) / 10u;
            char TileName[24]{}, CoordText[48]{}, CountText[16]{}, ResText[12]{};
            std::snprintf(TileName, sizeof(TileName), "%u", TileCode);
            std::snprintf(CoordText, sizeof(CoordText), "u%u, v%u · %.0f%%", U, V, Occupancy);
            std::snprintf(CountText, sizeof(CountText), "%u", ChartCount);
            if (Cfg.ResolutionPx >= 1024u) std::snprintf(ResText, sizeof(ResText), "%uK", Cfg.ResolutionPx / 1024u);
            else std::snprintf(ResText, sizeof(ResText), "%u", Cfg.ResolutionPx);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium,
                { Row.X + 27.0f, Row.Y + 6.0f }, 10.5f, Theme.TextPrimary, TileName);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,
                { Row.X + 27.0f, Row.Y + 22.0f }, 8.2f, Theme.TextMuted, CoordText);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,
                { Row.X + SW - 91.0f, Row.Y + 13.0f }, 9.2f, Theme.TextPrimary, CountText);
            const Rectangle2D ResChip{ Row.X + SW - 53.0f, Row.Y + 7.0f, 32.0f, 26.0f };
            Surface.FillRectangle(ResChip, ColorRgba{ 32, 32, 32, 255 }, 8.0f);
            Surface.StrokeRectangle(ResChip, Theme.BorderSubtle, 8.0f, 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium,
                { ResChip.X + 3.0f, ResChip.Y + 8.0f }, ResChip.Width - 6.0f, 8.8f, Theme.TextPrimary, ResText);
            const Rectangle2D DeleteHit{ Row.Right() - 18.0f, Row.Y + 8.0f, 16.0f, 24.0f };
            VectorIconRepository::ConstructControl(Surface, ControlIcon::CloseCross,
                VectorIconPlacement{ DeleteHit.X + 2.0f, DeleteHit.Y + 5.0f, 12.0f, Theme.TextMuted, 1.8f });
            if (Hovered && Pressed)
            {
                Session->ActiveUDIMNumber = TileCode;
                Session->FramebuffersDirty = true;
            }
            if (ResChip.Contains(Cursor))
            {
                RecordControlTooltip(ResChip, "Resolution", OptionalTooltip::FromText("Click to cycle through 512, 1K, 2K, 4K, and 8K."),
                    "Set this UDIM's texture resolution.");
                if (Pressed)
                {
                    const auto It = std::find(ResolutionCycle.begin(), ResolutionCycle.end(), Cfg.ResolutionPx);
                    const uint32_t NextIndex = It == ResolutionCycle.end() ? 0u :
                        (static_cast<uint32_t>(std::distance(ResolutionCycle.begin(), It)) + 1u) % static_cast<uint32_t>(ResolutionCycle.size());
                    Session->AccessUDIMTileSetting(TileCode).ResolutionPx = ResolutionCycle[NextIndex];
                    Session->ActiveUDIMNumber = TileCode;
                    Session->FramebuffersDirty = true;
                }
            }
            if (DeleteHit.Contains(Cursor))
            {
                RecordControlTooltip(DeleteHit, "Remove Tile", OptionalTooltip::FromText("Remove this UDIM tile."), "Remove this UDIM tile.");
                if (Pressed && Session->Asset.UDIMTiles.size() > 1u)
                {
                    std::erase(Session->Asset.UDIMTiles, TileCode);
                    if (Session->ActiveUDIMNumber == TileCode) Session->ActiveUDIMNumber = Session->Asset.UDIMTiles.front();
                }
            }
            if (Occupancy > 0.0f)
                Surface.FillRectangle({ Row.X + 4.0f, Row.Bottom() - 3.0f, (Row.Width - 8.0f) * Occupancy * 0.01f, 2.0f },
                    IsActive ? ColorRgba{ 239, 244, 207, 255 } : ColorRgba{ 90, 90, 90, 255 }, 1.0f);
            SY += 40.0f;
        }
        CurY += SecRect.Height + 10.0f;
    }

    // Active UDIM resolution deck and the HTML Color / UV Code / Checker / Slate segmented backing selector.
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 150.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 22.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 22.0f, 1.0f);
        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float SY = SecRect.Y + 14.0f;
        char Title[48]{};
        std::snprintf(Title, sizeof(Title), "UDIM %u Settings", Session->ActiveUDIMNumber);
        ConstructSectionHeading(Surface, SX, SY, SW, Title, Theme, "", ControlIcon::SlidersQuality);
        SY += 24.0f;
        auto& TileCfg = Session->AccessUDIMTileSetting(Session->ActiveUDIMNumber);
        constexpr std::array<std::pair<const char*, const char*>, 5u> ResDeck {{
            { "512", "0.3 MP" }, { "1K", "1.0 MP" }, { "2K", "4.2 MP" }, { "4K", "16.8 MP" }, { "8K", "67.1 MP" },
        }};
        constexpr std::array<uint32_t, 5u> ResValues {{ 512u, 1024u, 2048u, 4096u, 8192u }};
        const float ResGap = 4.0f;
        const float ResW = (SW - ResGap * 4.0f) / 5.0f;
        for (uint32_t Index = 0u; Index < ResDeck.size(); ++Index)
        {
            const Rectangle2D Pill{ SX + static_cast<float>(Index) * (ResW + ResGap), SY, ResW, 38.0f };
            const bool Active = TileCfg.ResolutionPx == ResValues[Index];
            const bool Hovered = Pill.Contains(Cursor);
            Surface.FillRectangle(Pill, Active ? ColorRgba{ 239, 244, 207, 255 } :
                (Hovered ? ColorRgba{ 43, 43, 43, 255 } : ColorRgba{ 28, 28, 28, 255 }), 11.0f);
            Surface.StrokeRectangle(Pill, Active ? ColorRgba{ 239, 244, 207, 255 } : Theme.BorderSubtle, 11.0f, 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium,
                { Pill.X + 4.0f, Pill.Y + 7.0f }, Pill.Width - 8.0f, 9.5f,
                Active ? ColorRgba{ 23, 25, 14, 255 } : Theme.TextPrimary, ResDeck[Index].first);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular,
                { Pill.X + 4.0f, Pill.Y + 23.0f }, Pill.Width - 8.0f, 7.6f,
                Active ? ColorRgba{ 23, 25, 14, 166 } : Theme.TextMuted, ResDeck[Index].second);
            if (Hovered)
            {
                RecordControlTooltip(Pill, ResDeck[Index].first,
                    OptionalTooltip::FromText("Set the active UDIM texture resolution."), "Set the active UDIM resolution.");
                if (Pressed)
                {
                    TileCfg.ResolutionPx = ResValues[Index];
                    Session->FramebuffersDirty = true;
                }
            }
        }
        SY += 50.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,
            { SX, SY }, 11.5f, Theme.TextMuted, "Backing");
        constexpr std::array<std::pair<const char*, GeometricRaster::UVBackdropPreset>, 4u> Backings {{
            { "Color", GeometricRaster::UVBackdropPreset::ColorChecker },
            { "UV Code", GeometricRaster::UVBackdropPreset::UVCode },
            { "Checker", GeometricRaster::UVBackdropPreset::Checker },
            { "Slate", GeometricRaster::UVBackdropPreset::Slate },
        }};
        const Rectangle2D BackTrack{ SX, SY + 16.0f, SW, 30.0f };
        Surface.FillRectangle(BackTrack, ColorRgba{ 11, 11, 11, 255 }, 15.0f);
        Surface.StrokeRectangle(BackTrack, ColorRgba{ 255, 255, 255, 22 }, 15.0f, 1.0f);
        const float BackGap = 3.0f;
        const float BackW = (SW - 12.0f) / 4.0f;
        for (uint32_t Index = 0u; Index < Backings.size(); ++Index)
        {
            const Rectangle2D Option{ SX + 4.0f + static_cast<float>(Index) * (BackW + BackGap), SY + 19.0f, BackW, 24.0f };
            const bool Active = TileCfg.BackdropPreset == Backings[Index].second;
            const bool Hovered = Option.Contains(Cursor);
            if (Active || Hovered)
                Surface.FillRectangle(Option, Active ? ColorRgba{ 41, 41, 41, 255 } : ColorRgba{ 33, 33, 33, 255 }, 12.0f);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular,
                { Option.X + 3.0f, Option.Y + 7.0f }, Option.Width - 6.0f, 8.8f,
                Active || Hovered ? Theme.TextPrimary : Theme.TextMuted, Backings[Index].first);
            if (Hovered)
            {
                RecordControlTooltip(Option, Backings[Index].first,
                    OptionalTooltip::FromText("Choose the preview backing for the active UDIM tile."),
                    "Choose a UDIM backing.");
                if (Pressed)
                {
                    TileCfg.BackdropPreset = Backings[Index].second;
                    Session->FramebuffersDirty = true;
                }
            }
        }
        CurY += SecRect.Height + 12.0f;
    }
    return CurY - StartY;
}

float UVInspectorPanel::RenderShadingTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme = QueryThemeColors();
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool Pressed = Input.PrimaryPointer.Pressed;
    const bool Down = Input.PrimaryPointer.Down;
    if (Input.EscapePressed) ShadingModeDropdownOpen = false;
    const auto& Network = Session->Asset.Network;
    float CurY = StartY;

    // Material hero with the same hierarchy as Experimental/UVEditor.
    {
        const Rectangle2D Hero{ LeftX, CurY, InnerW, 154.0f };
        Surface.FillRectangle(Hero, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(Hero, Theme.BorderSubtle, 20.0f, 1.0f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,
            { Hero.X + 18.0f, Hero.Y + 16.0f }, 8.8f, Theme.TextMuted, "VIEWPORT SHADER");
        const auto DisplayMode = Session->RenderOptions3D.DisplayMode;
        const char* ShaderName = "Surface Material";
        if (DisplayMode == GeometricRaster::SurfaceDisplayMode::MatCapPreview) ShaderName = "MatCap (Per-Face)";
        else if (DisplayMode == GeometricRaster::SurfaceDisplayMode::MaterialAndMatCap) ShaderName = "Material + MatCap";
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Light,
            { Hero.X + 18.0f, Hero.Y + 36.0f }, Hero.Width - 36.0f, 25.0f, Theme.TextPrimary, ShaderName);
        char ChartTxt[48]{};
        std::snprintf(ChartTxt, sizeof(ChartTxt), "%u charts", CachedChartCount);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,
            { Hero.X + 18.0f, Hero.Y + 68.0f }, 9.8f, Theme.TextMuted, ChartTxt);
        Surface.FillRectangle({ Hero.X + 18.0f, Hero.Y + 111.0f, Hero.Width - 36.0f, 5.0f }, Theme.WellBg, 3.0f);
        Surface.FillRectangle({ Hero.X + 18.0f, Hero.Y + 111.0f, Hero.Width - 36.0f, 5.0f }, Theme.AccentFill, 3.0f);
        char FaceTxt[48]{}, AreaTxt[64]{};
        std::snprintf(FaceTxt, sizeof(FaceTxt), "Entire Surface (%s faces)", FormatCountWithCommas(Network.Faces.size()).c_str());
        std::snprintf(AreaTxt, sizeof(AreaTxt), "UDIM %u · %u px", Session->ActiveUDIMNumber,
            Session->QueryUDIMTileSetting(Session->ActiveUDIMNumber).ResolutionPx);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular,
            { Hero.X + 18.0f, Hero.Y + 124.0f }, Hero.Width * 0.57f, 8.8f, Theme.TextMuted, FaceTxt);
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular,
            { Hero.X + Hero.Width * 0.57f, Hero.Y + 124.0f }, Hero.Width * 0.39f, 8.8f, Theme.TextMuted, AreaTxt);
        CurY += Hero.Height + 10.0f;
    }

    // Four independent telemetry tiles as in the HTML bento grid.
    {
        const float Gap = 8.0f;
        const float TileW = (InnerW - Gap) * 0.5f;
        const float TileH = 76.0f;
        const float StretchPct = std::max(0.0f, (Session->LatestMetrics.MeanL2Stretch - 1.0f) * 100.0f);
        char IssueValue[32]{}, StretchValue[32]{}, DensityValue[32]{}, SurfaceValue[32]{};
        const uint32_t Issues = Session->LatestMetrics.OverlapFaceCount + Session->LatestMetrics.FlippedFaceCount;
        std::snprintf(IssueValue, sizeof(IssueValue), "%u", Issues);
        std::snprintf(StretchValue, sizeof(StretchValue), "%.1f", StretchPct);
        std::snprintf(DensityValue, sizeof(DensityValue), "%.1f", Session->WorkspaceConfig.InspectUVPreferences().TargetTexelDensity * 0.01f);
        std::snprintf(SurfaceValue, sizeof(SurfaceValue), "%s", FormatCountWithCommas(Network.Faces.size()).c_str());
        const std::array<std::array<const char*, 3u>, 4u> Tiles {{
            {{ Issues ? "Overlaps" : "UV Health", IssueValue, Issues ? "issues" : "ok" }},
            {{ "Stretch", StretchValue, "%" }},
            {{ "Density", DensityValue, "px" }},
            {{ "Surface", SurfaceValue, "f" }}
        }};
        for (uint32_t Index = 0u; Index < Tiles.size(); ++Index)
        {
            const float X = LeftX + static_cast<float>(Index % 2u) * (TileW + Gap);
            const float Y = CurY + static_cast<float>(Index / 2u) * (TileH + Gap);
            const Rectangle2D Card{ X, Y, TileW, TileH };
            Surface.FillRectangle(Card, Theme.SectionBg, 16.0f);
            Surface.StrokeRectangle(Card, Theme.BorderSubtle, 16.0f, 1.0f);
            const ColorRgba BadgeColor = (Index == 0u && Issues == 0u) ? Theme.ReadyGreen : Theme.AccentBorder;
            Surface.FillCircle({ X + 24.0f, Y + 24.0f }, 10.0f, BadgeColor);
            constexpr std::array<ControlIcon, 4u> BadgeIcons {{
                ControlIcon::CircleCheck, ControlIcon::GaugeRate, ControlIcon::LayoutGrid, ControlIcon::LayersSlabs
            }};
            const ControlIcon BadgeIcon = (Index == 0u && Issues > 0u) ? ControlIcon::TriangleAlert : BadgeIcons[Index];
            VectorIconRepository::ConstructControl(Surface, BadgeIcon,
                VectorIconPlacement{ X + 18.0f, Y + 18.0f, 12.0f, Theme.WellBg, 1.5f });
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular,
                { X + 12.0f, Y + 50.0f }, TileW - 24.0f, 8.8f, Theme.TextMuted, Tiles[Index][0]);
            const float ValueSize = (Index == 3u && Network.Faces.size() > 999u) ? 15.0f : 19.0f;
            const float ValueW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, ValueSize, Tiles[Index][1]);
            const float UnitW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.0f, Tiles[Index][2]);
            const float ValueX = X + TileW - 12.0f - ValueW - UnitW - 3.0f;
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Light,
                { ValueX, Y + 38.0f }, ValueSize, Theme.TextPrimary, Tiles[Index][1]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,
                { ValueX + ValueW + 3.0f, Y + 50.0f }, 8.0f, Theme.TextMuted, Tiles[Index][2]);
        }
        CurY += TileH * 2.0f + Gap + 10.0f;
    }

    // Native material controls remain actionable; the MatCap palette and finish sliders
    // use the existing assignment operations rather than visual-only placeholders.
    {
        const Rectangle2D Card{ LeftX, CurY, InnerW, 422.0f };
        Surface.FillRectangle(Card, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(Card, Theme.BorderSubtle, 20.0f, 1.0f);
        const float SX = Card.X + 16.0f;
        const float SW = Card.Width - 32.0f;
        float SY = Card.Y + 14.0f;
        ConstructSectionHeading(Surface, SX, SY, SW, "Surface Finish", Theme, "SURFACE", ControlIcon::SparklesAntiAliasing);
        SY += 30.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,
            { SX, SY + 7.0f }, 8.8f, Theme.TextMuted, "Shader");
        constexpr std::array<const char*, 3u> ShaderOptions {{
            "Surface Material", "MatCap (Per-Face)", "Material + MatCap"
        }};
        uint32_t ShaderIndex = 0u;
        if (Session->RenderOptions3D.DisplayMode == GeometricRaster::SurfaceDisplayMode::MatCapPreview) ShaderIndex = 1u;
        else if (Session->RenderOptions3D.DisplayMode == GeometricRaster::SurfaceDisplayMode::MaterialAndMatCap) ShaderIndex = 2u;
        Rectangle2D ShaderDropdownBounds{ SX + 58.0f, SY, SW - 58.0f, 31.0f };
        const ControlContact ShaderDropdown = DropdownSelector::ConstructButton(
            Surface, ShaderDropdownBounds, ShaderOptions[ShaderIndex], ShadingModeDropdownOpen,
            Input.PrimaryPointer, 1.0f, ControlIcon::DisplayMonitor,
            OptionalTooltip::FromText("Choose the viewport shader mode."));
        if (ShaderDropdown.Clicked)
        {
            ShadingModeDropdownOpen = !ShadingModeDropdownOpen;
            ActiveLayerDropdownOpen = false;
            TargetUdimDropdownOpen = false;
            PackingScopeDropdownOpen = false;
        }
        SY += 33.0f;
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular,
            { SX, SY + 3.0f }, 8.8f, Theme.TextMuted, "MatCap Library · 16");
        SY += 19.0f;
        const float ColStep = SW / 8.0f;
        ImTextureData* MatCapAtlas = QueryMatCapAtlasTexture();
        for (uint32_t Slot = 0u; Slot < 16u; ++Slot)
        {
            const uint32_t Col = Slot % 8u;
            const uint32_t Row = Slot / 8u;
            const Point2D Center{ SX + ColStep * (static_cast<float>(Col) + 0.5f), SY + 13.0f + static_cast<float>(Row) * 31.0f };
            ConstructMatCapOrbPreview(Surface, Center, 10.5f, Slot, SelectedMatCap == Slot, Theme, MatCapAtlas);
            const Rectangle2D Hit{ Center.X - 13.0f, Center.Y - 13.0f, 26.0f, 26.0f };
            if (Hit.Contains(Cursor) && Pressed)
            {
                SelectedMatCap = static_cast<uint8_t>(Slot);
                Session->ApplyMatCapToActiveSelection(SelectedMatCap);
            }
            if (Hit.Contains(Cursor)) ControlAppearance::RecordHoveredTooltip(Hit,
                OptionalTooltip::FromText("Select a procedural MatCap and apply it to the active selection."));
        }
        SY += 72.0f;
        char MetTxt[24]{}, RghTxt[24]{}, SpcTxt[24]{}, BldTxt[24]{};
        std::snprintf(MetTxt, sizeof(MetTxt), "%.2f", SurfaceMetallic);
        std::snprintf(RghTxt, sizeof(RghTxt), "%.2f", SurfaceRoughness);
        std::snprintf(SpcTxt, sizeof(SpcTxt), "%.2f", SurfaceSpecular);
        std::snprintf(BldTxt, sizeof(BldTxt), "%.0f%%", SurfaceMatBlend * 100.0f);
        bool Dirty = false;
        Dirty |= ConstructSliderControl(Surface, { SX, SY, SW, 46.0f }, "Metallic", MetTxt, SurfaceMetallic, Theme, Cursor, Down, Pressed);
        SY += 50.0f;
        Dirty |= ConstructSliderControl(Surface, { SX, SY, SW, 46.0f }, "Roughness", RghTxt, SurfaceRoughness, Theme, Cursor, Down, Pressed);
        SY += 50.0f;
        Dirty |= ConstructSliderControl(Surface, { SX, SY, SW, 46.0f }, "Specular", SpcTxt, SurfaceSpecular, Theme, Cursor, Down, Pressed);
        SY += 50.0f;
        Dirty |= ConstructSliderControl(Surface, { SX, SY, SW, 46.0f }, "MatCap Blend", BldTxt, SurfaceMatBlend, Theme, Cursor, Down, Pressed);
        if (Dirty) Session->ApplySurfaceRoughnessMetallic(SurfaceRoughness, SurfaceMetallic, SurfaceMatBlend, SurfaceSpecular);

        if (ShadingModeDropdownOpen)
        {
            Rectangle2D SheetBounds = DropdownSelector::QuerySheetBounds(ShaderDropdownBounds,
                static_cast<uint32_t>(ShaderOptions.size()));
            if (SheetBounds.Bottom() > Card.Bottom())
            {
                ShaderDropdownBounds.Y -= SheetBounds.Height + 2.0f * ControlMetrics::DropdownPaddingY + ShaderDropdownBounds.Height;
                SheetBounds = DropdownSelector::QuerySheetBounds(ShaderDropdownBounds,
                    static_cast<uint32_t>(ShaderOptions.size()));
            }
            uint32_t ChosenShader = ShaderIndex;
            const ControlContact Choice = DropdownSelector::ConstructOptionSheet(
                Surface, ShaderDropdownBounds, ShaderOptions.data(), static_cast<uint32_t>(ShaderOptions.size()),
                ShaderIndex, Input.PrimaryPointer, ChosenShader);
            if (Choice.Clicked && ChosenShader < ShaderOptions.size())
            {
                constexpr std::array<GeometricRaster::SurfaceDisplayMode, 3u> Modes {{
                    GeometricRaster::SurfaceDisplayMode::SurfaceMaterial,
                    GeometricRaster::SurfaceDisplayMode::MatCapPreview,
                    GeometricRaster::SurfaceDisplayMode::MaterialAndMatCap
                }};
                Session->RenderOptions3D.DisplayMode = Modes[ChosenShader];
                Session->FramebuffersDirty = true;
                ShadingModeDropdownOpen = false;
            }
            else if (Input.PrimaryPointer.Released && !ShaderDropdownBounds.Contains(Cursor) && !SheetBounds.Contains(Cursor))
            {
                ShadingModeDropdownOpen = false;
            }
        }
        CurY += Card.Height + 12.0f;
    }
    return CurY - StartY;
}

float UVInspectorPanel::RenderCheckTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme = QueryThemeColors();
    const Point2D Cursor{ Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool Pressed = Input.PrimaryPointer.Pressed;
    float CurY = StartY;

    const auto& Metrics = Session->LatestMetrics;
    char OverlapValue[24]{}, FlippedValue[24]{}, StretchValue[24]{};
    char OverlapSub[40]{}, FlippedSub[40]{}, StretchSub[32]{};
    std::snprintf(OverlapValue, sizeof(OverlapValue), "%u", Metrics.OverlapFaceCount);
    std::snprintf(FlippedValue, sizeof(FlippedValue), "%u", Metrics.FlippedFaceCount);
    std::snprintf(StretchValue, sizeof(StretchValue), "%.2f", Metrics.MeanL2Stretch);
    std::snprintf(OverlapSub, sizeof(OverlapSub), "%s", Metrics.OverlapFaceCount == 0u ? "No overlap" : "Review overlaps");
    std::snprintf(FlippedSub, sizeof(FlippedSub), "%s", Metrics.FlippedFaceCount == 0u ? "All positive" : "Inverted UV");
    float MaxStretch = 0.0f;
    for (const float Stretch : Metrics.PerFaceStretch) MaxStretch = std::max(MaxStretch, Stretch);
    std::snprintf(StretchSub, sizeof(StretchSub), "Max %.2f", MaxStretch);

    ConstructMetricTrio(
        Surface, { LeftX, CurY, InnerW, 60.0f },
        OverlapValue, "Overlaps", OverlapSub,
        FlippedValue, "Flipped Winding", FlippedSub,
        StretchValue, "Mean Stretch", StretchSub,
        Theme, Metrics.OverlapFaceCount == 0u);
    CurY += 70.0f;

    const Rectangle2D SecRect{ LeftX, CurY, InnerW, 148.0f };
    Surface.FillRectangle(SecRect, Theme.SectionBg, 22.0f);
    Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 22.0f, 1.0f);
    const float SX = SecRect.X + 16.0f;
    const float SW = SecRect.Width - 32.0f;
    float SY = SecRect.Y + 14.0f;
    const bool IsClean = Metrics.OverlapFaceCount == 0u && Metrics.FlippedFaceCount == 0u;
    ConstructSectionHeading(Surface, SX, SY, SW, "UV Integrity & Repair", Theme,
        IsClean ? "CLEAN" : "ATTENTION", IsClean ? ControlIcon::CircleCheck : ControlIcon::TriangleAlert);
    SY += 26.0f;

    const float HalfW = (SW - 6.0f) * 0.5f;
    if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 28.0f }, "Select Overlaps", Theme, Cursor, Pressed, false, false, 9.6f,
        -1.0f, OptionalTooltip::FromText("Select faces participating in UV island overlaps.")))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::SelectOverlappingFaces, nullptr);
    }
    if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 28.0f }, "Select Flipped", Theme, Cursor, Pressed, false, false, 9.6f,
        -1.0f, OptionalTooltip::FromText("Select faces whose UV winding is flipped.")))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::SelectFlippedFaces, nullptr);
    }
    SY += 34.0f;
    if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 30.0f }, "Flip UV Winding", Theme, Cursor, Pressed, false, false, 9.6f,
        -1.0f, OptionalTooltip::FromText("Reverse UV winding on selected faces, or all currently flipped faces.")))
    {
        std::vector<uint32_t> Faces = Session->SelectionMask.CollectSelectedFaces();
        if (Faces.empty())
        {
            for (uint32_t FaceIndex = 0u; FaceIndex < Session->Asset.Network.Faces.size(); ++FaceIndex)
            {
                if (Session->Asset.Network.EvaluateFaceSignedAreaUV(FaceIndex) <= 0.0f) Faces.push_back(FaceIndex);
            }
        }
        if (!Faces.empty())
        {
            UVAuthoringOperations::MirrorSelectedFaceUVs(Session->Asset.Network, Faces, true);
            Session->FinalizeInteractiveUVRevision("FlipUVWinding");
        }
    }
    if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 30.0f }, "Auto-Relax & Repack", Theme, Cursor, Pressed, false, true, 9.6f,
        -1.0f, OptionalTooltip::FromText("Relax stretch, then repack using the active UDIM packing scope.")))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::RelaxStretchGPU, nullptr);
        (void)Session->ExecuteDirective(UVCommandDirective::PackUVsConcaveSkyline, nullptr);
    }

    CurY += SecRect.Height + 12.0f;
    return CurY - StartY;
}

void UVInspectorPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    using namespace UVAppearance;
    const UVThemeColors Theme = QueryThemeColors();

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PanelBg);

    if (Session == nullptr)
    {
        Surface.PopClip();
        return;
    }

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;
    const auto&   Network = Session->Asset.Network;
    if (CachedNetworkRevision != Network.NetworkRevision || CachedUVRevision != Network.UVRevision)
    {
        CachedNetworkRevision = Network.NetworkRevision;
        CachedUVRevision = Network.UVRevision;
        CachedChartCount = static_cast<uint32_t>(SeamCutGenerator::PartitionConnectedUVIslands(Network).size());
    }
    const std::vector<uint32_t> SelectedFaces = Session->SelectionMask.CollectSelectedFaces();

    const float PadX   = 18.0f;
    const float LeftX  = Bounds.X + PadX;
    const float InnerW = std::max(150.0f, Bounds.Width - PadX * 2.0f);
    float       CurY   = Bounds.Y + 23.0f;

    // 1. Breadcrumb + "+ Export OBJ" + "Reset"
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY + 4.0f },
        InnerW - 140.0f,
        9.0f,
        Theme.TextMuted,
        std::array<const char*, 5u>{
            "Inspector / Importance Packing",
            "Inspector / Unwrap & Tools",
            "Inspector / Spatial UDIM Matrix",
            "Inspector / Surface & Multi-MatCap",
            "Inspector / Checks, Studio & Flat Debug" }[
                std::min<std::size_t>(static_cast<std::size_t>(Session->ActiveInspectorTab), 4u)]);

    const Rectangle2D ResetBtn  { LeftX + InnerW - 48.0f, CurY, 48.0f, 22.0f };
    const Rectangle2D ExportBtn { ResetBtn.X - 84.0f, CurY, 78.0f, 22.0f };
    if (ConstructCapsuleButton(
            Surface, ExportBtn, "+ Export OBJ", Theme, Cursor, Pressed, false, false, 8.8f, -1.0f,
            OptionalTooltip::FromText("Export the current polygon network and UV coordinates as an OBJ file.")))
    {
        std::string Diag {};
        (void)Session->ExportExternalGeometry(std::filesystem::temp_directory_path() / "FrontierExport.obj", Diag);
    }
    if (ConstructCapsuleButton(
            Surface, ResetBtn, "Reset", Theme, Cursor, Pressed, false, false, 8.8f, -1.0f,
            OptionalTooltip::FromText("Reload the current sample asset and reset its workspace state.")))
    {
        Session->LoadSamplePreset(Session->ActivePreset);
    }
    CurY += 54.0f;

    // 2. Large 23px Light Selection / Asset Heading + Subtitle (matching InspectorPanel.cpp & Experimental/UVEditor)
    const std::string Heading = SelectedFaces.empty()
        ? Session->Asset.Network.NetworkTitle
        : FormatCountWithCommas(SelectedFaces.size()) + " Faces Selected";
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Light,
        { LeftX, CurY },
        InnerW,
        23.0f,
        Theme.TextPrimary,
        Heading);
    CurY += 41.0f;

    char SubMeta[112]{};
    const auto TileCfg = Session->QueryUDIMTileSetting(Session->ActiveUDIMNumber);
    if (SelectedFaces.empty())
    {
        std::snprintf(
            SubMeta,
            sizeof(SubMeta),
            "%u charts · %s faces · Whole Surface",
            CachedChartCount,
            FormatCountWithCommas(Session->Asset.Network.Faces.size()).c_str());
    }
    else
    {
        uint32_t FirstSelectedUDIM = 0u;
        bool MultipleSelectedUDIMs = false;
        for (const uint32_t FaceIndex : SelectedFaces)
        {
            if (FaceIndex >= Network.Faces.size())
            {
                continue;
            }
            const uint32_t FaceUDIM = Network.DeriveFaceUDIMNumber(FaceIndex);
            if (FirstSelectedUDIM == 0u)
            {
                FirstSelectedUDIM = FaceUDIM;
            }
            else if (FirstSelectedUDIM != FaceUDIM)
            {
                MultipleSelectedUDIMs = true;
                break;
            }
        }
        std::snprintf(
            SubMeta,
            sizeof(SubMeta),
            "%s · UDIM %s",
            FormatCountWithCommas(SelectedFaces.size()).c_str(),
            MultipleSelectedUDIMs ? "Multiple" : std::to_string(FirstSelectedUDIM).c_str());
    }
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY },
        InnerW,
        9.8f,
        Theme.TextMuted,
        SubMeta);
    CurY += 39.0f;

    // 3. 5-Tab Segmented Bar (.inspector-tabs: Pack | Unwrap | UDIM | Shading | Check)
    {
        const Rectangle2D TabBar{ LeftX, CurY, InnerW, 34.0f };
        Surface.FillRectangle(TabBar, Theme.WellBg, 11.0f);
        Surface.StrokeRectangle(TabBar, Theme.BorderSubtle, 11.0f, 1.0f);

        constexpr std::array<const char*, 5u> TabNames {{ "Pack", "Unwrap", "UDIM", "Shading", "Check" }};
        constexpr std::array<const char*, 5u> TabTips {{
            "Allocation layers, island packing, and packing scope.",
            "Unwrap, projection, seam, pin, stitch, and relax tools.",
            "Manage UDIM tiles, resolution, and viewport backdrops.",
            "Configure MatCaps and surface material response.",
            "Inspect UV overlaps, flips, stretch, and repair options." }};
        const float SegW = (InnerW - 6.0f) / 5.0f;
        for (uint32_t Idx = 0u; Idx < 5u; ++Idx)
        {
            const Rectangle2D Seg{ TabBar.X + 3.0f + static_cast<float>(Idx) * SegW, TabBar.Y + 3.0f, SegW, 28.0f };
            const auto        TargetTab = static_cast<UVInspectorTab>(Idx);
            const bool        Active    = (Session->ActiveInspectorTab == TargetTab);
            const bool        Hovered   = Seg.Contains(Cursor);
            if (Hovered)
            {
                ControlAppearance::RecordHoveredTooltip(
                    Seg, OptionalTooltip::FromText(TabTips[Idx]));
            }

            if (Active)
            {
                Surface.FillRectangle(Seg, Theme.RaisedBg, 8.0f);
                Surface.StrokeRectangle(Seg, Theme.AccentBorder, 8.0f, 1.0f);
            }
            else if (Hovered)
            {
                Surface.FillRectangle(Seg, Theme.HoverBg, 8.0f);
            }

            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, TabNames[Idx]);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Seg.X + (Seg.Width - TW) * 0.5f, Seg.Y + 7.0f },
                9.8f,
                Active ? Theme.TextPrimary : Theme.TextMuted,
                TabNames[Idx]);

            if (Hovered && Pressed)
            {
                Session->ActiveInspectorTab = TargetTab;
                ScrollOffsetY               = 0.0f;
                ActiveLayerDropdownOpen = false;
                TargetUdimDropdownOpen = false;
                PackingScopeDropdownOpen = false;
                ShadingModeDropdownOpen = false;
            }
        }
        CurY += 54.0f;
    }

    // 4. Scrollable Active Tab Body above Sticky 40px Inspector Footer
    constexpr float   FooterH = 40.0f;
    const Rectangle2D ScrollRect{
        Bounds.X,
        CurY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - FooterH - CurY)
    };

    const std::size_t TabIndex = std::min<std::size_t>(
        static_cast<std::size_t>(Session->ActiveInspectorTab), CachedTabContentHeights.size() - 1u);
    const float PreviousMaxScroll = std::max(0.0f, CachedTabContentHeights[TabIndex] - ScrollRect.Height);
    if (ScrollRect.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY -= Input.WheelDeltaY * 28.0f;
    }
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, PreviousMaxScroll);

    Surface.PushClip(ScrollRect);
    const float BodyStartY = ScrollRect.Y - ScrollOffsetY;
    float       ContentH   = 0.0f;

    switch (Session->ActiveInspectorTab)
    {
        case UVInspectorTab::Packing: ContentH = RenderPackingTab(Surface, LeftX, BodyStartY, InnerW, ScrollRect, Input, Δt); break;
        case UVInspectorTab::Unwrap:  ContentH = RenderUnwrapTab(Surface, LeftX, BodyStartY, InnerW, Input);  break;
        case UVInspectorTab::UDIM:    ContentH = RenderUDIMTab(Surface, LeftX, BodyStartY, InnerW, Input);    break;
        case UVInspectorTab::Shading: ContentH = RenderShadingTab(Surface, LeftX, BodyStartY, InnerW, Input); break;
        case UVInspectorTab::Check:   ContentH = RenderCheckTab(Surface, LeftX, BodyStartY, InnerW, Input);   break;
    }

    CachedTabContentHeights[TabIndex] = ContentH;
    const float MaxScroll = std::max(0.0f, ContentH - ScrollRect.Height);
    ScrollOffsetY = std::clamp(ScrollOffsetY, 0.0f, MaxScroll);
    Surface.PopClip();

    // 5. Sticky 40px Inspector Footer (.pz-inspector-foot)
    const Rectangle2D FootRect{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.FillRectangle(FootRect, Theme.PanelBg);
    Surface.StrokeLine({ FootRect.X, FootRect.Y }, { FootRect.Right(), FootRect.Y }, Theme.BorderSubtle, 1.0f);

    Surface.FillCircle({ LeftX + 5.0f, FootRect.Y + 20.0f }, 3.0f, Theme.ReadyGreen);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + 14.0f, FootRect.Y + 13.0f },
        9.5f,
        Theme.TextMuted,
        "Studio + Multi-MatCap");

    char FootRight[48]{};
    std::snprintf(FootRight, sizeof(FootRight), "UDIM %u · %u px", Session->ActiveUDIMNumber, TileCfg.ResolutionPx);
    const float FRW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, FootRight);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + InnerW - FRW, FootRect.Y + 13.0f },
        9.5f,
        Theme.TextMuted,
        FootRight);

    Surface.PopClip();
    (void)ControlTooltip::PresentHoveredControlTooltip(
        Surface, { 0.0f, 0.0f, Bounds.Width, Bounds.Height }, Input.PrimaryPointer);
}

void UVInspectorPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##UVInspectorPaneCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace             Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(Surface, LogicalBounds, Input, Δt);
}

} // namespace Frontier::Workspaces

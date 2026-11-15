/*==========================================================================================================================================
                                                  MATERIALLAYERSTACKPANE.CPP
==========================================================================================================================================*/
// 🧩 Implementation of MaterialLayerStackPane (Modular Material Repository + Non-Destructive Finish/Field/Mask/Transition Layer Stack)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Material/MaterialLayerStackPane.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Workspaces
{

namespace
{

[[nodiscard]] ColorRgba LinearSwatchRgba(const std::array<float, 4u>& Linear) noexcept
{
    const auto ChannelByte = [](float V) noexcept -> uint8_t {
        const float G = std::pow(std::clamp(V, 0.0f, 1.0f), 1.0f / 2.2f);
        return static_cast<uint8_t>(std::lround(G * 255.0f));
    };
    return { ChannelByte(Linear[0]), ChannelByte(Linear[1]), ChannelByte(Linear[2]), 255u };
}

[[nodiscard]] ColorRgba CategoryAccentColor(MaterialSpace::Dynamic::MaterialLayerCategory Cat) noexcept
{
    switch (Cat)
    {
        case MaterialSpace::Dynamic::MaterialLayerCategory::FinishLayer:            return { 232u, 194u, 122u, 255u };
        case MaterialSpace::Dynamic::MaterialLayerCategory::ProceduralFieldLayer:   return {  74u, 196u, 196u, 255u };
        case MaterialSpace::Dynamic::MaterialLayerCategory::MaskModifierLayer:      return {  84u, 184u, 255u, 255u };
        case MaterialSpace::Dynamic::MaterialLayerCategory::SurfaceTransitionLayer: return {  52u, 199u,  89u, 255u };
    }
    return { 200u, 200u, 200u, 255u };
}

} // namespace

void MaterialLayerStackPane::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    MaterialWorkspaceSession& Session,
    const ControlPointer&     Pointer) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    LastPresetRowRects.clear();
    LastLayerRowRects.clear();

    Surface.FillRectangle(Bounds, C.PanelBg, 20.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 20.0f, 1.0f);
    Surface.PushClip(Bounds);

    float Y = Bounds.Y + 16.0f;
    const float X = Bounds.X + 14.0f;
    const float W = Bounds.Width - 28.0f;

    // Section 1: Non-Destructive Material Layer Stack (Top Half)
    Surface.Text({ X, Y }, 10.5f, C.TextMuted, "MATERIAL LAYER STACK");
    char CountBuf[32];
    std::snprintf(CountBuf, sizeof(CountBuf), "%zu LAYERS", Session.InspectActiveRecipe().LayerStack.Layers.size());
    Surface.TextRightAligned({ X, Y, W, 14.0f }, 9.5f, C.WarmAccent, CountBuf);
    Y += 22.0f;

    // Quick add / remove toolbar
    const float BtnW = (W - 12.0f) / 3.0f;
    const Rectangle2D AddFieldRect { X, Y, BtnW, 24.0f };
    const Rectangle2D AddTransRect { X + BtnW + 6.0f, Y, BtnW, 24.0f };
    const Rectangle2D RemoveRect   { X + (BtnW + 6.0f) * 2.0f, Y, BtnW, 24.0f };

    const auto RenderActionBtn = [&](Rectangle2D Rect, const char* Label, ColorRgba Accent) noexcept -> bool {
        const bool Hovered = ControlAppearance::QueryPointerOver(Rect, Pointer);
        Surface.FillRectangle(Rect, Hovered ? C.CardElevated : C.CardBg, 8.0f);
        Surface.StrokeRectangle(Rect, Hovered ? Accent : C.Hairline, 8.0f, 1.0f);
        Surface.TextCentered(Rect, 9.0f, Hovered ? C.TextPrimary : C.TextSoft, Label);
        return Hovered && Pointer.Pressed;
    };

    if (RenderActionBtn(AddFieldRect, "+ Field", C.TealTint))
    {
        Session.AppendProceduralLayer();
    }
    if (RenderActionBtn(AddTransRect, "+ Transition", C.LiveGreen))
    {
        Session.AppendTransitionLayer();
    }
    if (RenderActionBtn(RemoveRect, "Remove", C.DangerRed))
    {
        Session.RemoveSelectedLayer();
    }
    Y += 32.0f;

    // Layer stack rows (displayed Top -> Bottom like Photoshop / Substance)
    const auto& ActiveRecipe = Session.InspectActiveRecipe();
    const std::size_t SelectedLayer = Session.QuerySelectedLayerIndex();
    const std::size_t LayerCount = ActiveRecipe.LayerStack.Layers.size();
    LastLayerRowRects.resize(LayerCount);

    for (std::size_t Rev = 0u; Rev < LayerCount && Rev < 5u; ++Rev)
    {
        const std::size_t Idx = LayerCount - 1u - Rev;
        const auto& Entry = ActiveRecipe.LayerStack.Layers[Idx];
        const bool Selected = (Idx == SelectedLayer);
        const bool Visible  = ActiveRecipe.LayerStack.QueryLayerEffectiveVisibility(Idx);

        const Rectangle2D RowRect { X, Y, W, 42.0f };
        LastLayerRowRects[Idx] = RowRect;
        const bool Hovered = ControlAppearance::QueryPointerOver(RowRect, Pointer);

        Surface.FillRectangle(RowRect, Selected ? C.CardElevated : (Hovered ? ColorRgba { 27u, 27u, 29u, 255u } : C.CardBg), 10.0f);
        Surface.StrokeRectangle(RowRect, Selected ? C.WarmAccent : C.Hairline, 10.0f, 1.0f);

        // Left category color indicator bar
        const ColorRgba CatCol = CategoryAccentColor(Entry.Category);
        Surface.FillRectangle({ RowRect.X + 6.0f, RowRect.Y + 8.0f, 3.5f, RowRect.Height - 16.0f },
            Visible ? CatCol : C.TextFaint, 2.0f);

        Surface.Text({ RowRect.X + 16.0f, RowRect.Y + 7.0f }, 10.5f,
            Visible ? C.TextPrimary : C.TextMuted, Entry.LayerTitle.c_str());

        const auto CatLabel = MaterialSpace::Dynamic::QueryLayerCategoryLabel(Entry.Category);
        const auto DrvLabel = MaterialSpace::Dynamic::QueryDriverModeLabel(Entry.DriverMode);
        char SubBuf[96];
        std::snprintf(SubBuf, sizeof(SubBuf), "%.*s  |  %.*s",
            static_cast<int>(CatLabel.size()), CatLabel.data(),
            static_cast<int>(DrvLabel.size()), DrvLabel.data());
        Surface.Text({ RowRect.X + 16.0f, RowRect.Y + 23.0f }, 8.5f, C.TextMuted, SubBuf);

        // Visibility & Solo buttons on right edge
        const Rectangle2D VisBtn  { RowRect.Right() - 48.0f, RowRect.Y + 11.0f, 20.0f, 20.0f };
        const Rectangle2D SoloBtn { RowRect.Right() - 24.0f, RowRect.Y + 11.0f, 20.0f, 20.0f };
        Surface.FillRectangle(VisBtn, Visible ? ColorRgba { 52u, 199u, 89u, 40u } : C.WellBg, 6.0f);
        Surface.TextCentered(VisBtn, 8.5f, Visible ? C.LiveGreen : C.TextFaint, Visible ? "V" : "-");
        Surface.FillRectangle(SoloBtn, Entry.SoloEnabled ? C.WarmSoft : C.WellBg, 6.0f);
        Surface.TextCentered(SoloBtn, 8.5f, Entry.SoloEnabled ? C.WarmAccent : C.TextFaint, "S");

        if (ControlAppearance::QueryPointerOver(VisBtn, Pointer) && Pointer.Pressed)
        {
            Session.ToggleLayerVisibility(Idx);
        }
        else if (ControlAppearance::QueryPointerOver(SoloBtn, Pointer) && Pointer.Pressed)
        {
            Session.ToggleLayerSolo(Idx);
        }
        else if (Hovered && Pointer.Pressed)
        {
            Session.SelectLayer(Idx);
        }

        Y += 48.0f;
    }

    // Divider
    Y += 4.0f;
    Surface.FillRectangle({ X, Y, W, 1.0f }, C.Hairline, 0.0f);
    Y += 12.0f;

    // Section 2: Modular Material Repository (13 Asset-Agnostic Dynamic Presets)
    Surface.Text({ X, Y }, 10.5f, C.TextMuted, "MODULAR MATERIAL REPOSITORY");
    Surface.TextRightAligned({ X, Y, W, 14.0f }, 9.0f, C.TextSoft, "13 DYNAMIC");
    Y += 20.0f;

    // Category filter buttons
    const std::array<const char*, 4u> Categories {{ "All", "Transitions", "Nature", "Metals" }};
    const float FilterW = (W - 12.0f) / 4.0f;
    for (std::size_t I = 0u; I < Categories.size(); ++I)
    {
        const Rectangle2D FilterRect { X + static_cast<float>(I) * (FilterW + 4.0f), Y, FilterW, 21.0f };
        const bool Active  = (Session.CategoryFilter == Categories[I]);
        const bool Hovered = ControlAppearance::QueryPointerOver(FilterRect, Pointer);
        Surface.FillRectangle(FilterRect, Active ? C.WarmSoft : (Hovered ? C.CardElevated : C.WellBg), 10.0f);
        Surface.StrokeRectangle(FilterRect, Active ? C.WarmAccent : C.Hairline, 10.0f, 1.0f);
        Surface.TextCentered(FilterRect, 8.2f, Active ? C.WarmAccent : C.TextSoft, Categories[I]);
        if (Hovered && Pointer.Pressed)
        {
            Session.CategoryFilter = Categories[I];
        }
    }
    Y += 28.0f;

    // Preset entries with 3-dot linear color preview token
    for (std::size_t I = 0u; I < Session.PresetRepository.size(); ++I)
    {
        const auto& Preset = Session.PresetRepository[I];
        if (Session.CategoryFilter != "All" && Preset.CategoryLabel != Session.CategoryFilter)
        {
            continue;
        }
        if (Y + 30.0f > Bounds.Bottom() - 10.0f)
        {
            break;
        }

        const bool IsActive = (Preset.PresetIdentity == ActiveRecipe.PresetIdentity);
        const Rectangle2D RowRect { X, Y, W, 28.0f };
        LastPresetRowRects.push_back(RowRect);
        const bool Hovered = ControlAppearance::QueryPointerOver(RowRect, Pointer);

        Surface.FillRectangle(RowRect, IsActive ? C.CardElevated : (Hovered ? ColorRgba { 25u, 25u, 27u, 255u } : ColorRgba { 0u, 0u, 0u, 0u }), 8.0f);
        if (IsActive)
        {
            Surface.StrokeRectangle(RowRect, C.WarmAccent, 8.0f, 1.0f);
        }

        // Visual swatch circle showing primary & tertiary accent colors
        const Point2D SwatchCenter { RowRect.X + 14.0f, RowRect.Center().Y };
        Surface.FillCircle(SwatchCenter, 7.5f, LinearSwatchRgba(Preset.Parameters.PrimaryColorRgba));
        Surface.StrokeCircle(SwatchCenter, 7.5f, LinearSwatchRgba(Preset.Parameters.TertiaryColorRgba), 1.8f);

        Surface.Text({ RowRect.X + 28.0f, RowRect.Y + 7.0f }, 9.8f,
            IsActive ? C.TextPrimary : C.TextSoft, Preset.DisplayTitle.c_str());

        if (Hovered && Pointer.Pressed)
        {
            Session.SelectPresetIdentity(Preset.PresetIdentity);
        }

        Y += 31.0f;
    }

    Surface.PopClip();
}

Rectangle2D MaterialLayerStackPane::QueryPresetRowBounds(std::size_t Index) const noexcept
{
    return (Index < LastPresetRowRects.size()) ? LastPresetRowRects[Index] : Rectangle2D {};
}

Rectangle2D MaterialLayerStackPane::QueryLayerRowBounds(std::size_t Index) const noexcept
{
    return (Index < LastLayerRowRects.size()) ? LastLayerRowRects[Index] : Rectangle2D {};
}

} // namespace Frontier::Workspaces

/*============================================================================================================================================
                                                          FLUIDPRESETBROWSER.CPP
============================================================================================================================================*/
// 🧩 Implementation of FluidPresetBrowser: 10-preset visual card grid & instant preset switching

#include "Engine/Workspaces/Fluid/FluidPresetBrowser.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/SimulationSpace/FluidSpace/FluidPresets.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <cstdio>

namespace Frontier::Workspaces
{

void FluidPresetBrowser::RenderPane(
    PixelSpace&            Surface,
    Rectangle2D            PaneBounds,
    FluidWorkspaceSession& Session,
    const ControlPointer&  Pointer) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(PaneBounds, C.PanelBg, 0.0f);

    const auto& Presets = SimulationSpace::FluidSpace::FluidPresets::ListPresets();
    const float PadX = 20.0f;
    float Y = PaneBounds.Y + 18.0f;
    char TitleBuf[96] {};
    std::snprintf(TitleBuf, sizeof(TitleBuf), "FLUID PYRO PRESET GALLERY (%zu PRESETS)", Presets.size());
    Surface.Text({ PaneBounds.X + PadX, Y }, 15.0f, C.TextPrimary, TitleBuf);

    char DocBuf[128] {};
    std::snprintf(DocBuf, sizeof(DocBuf), "%s.fluid  |  %s", Session.DocumentTitle.c_str(), Session.StatusMessage.c_str());
    Surface.Text({ PaneBounds.X + PadX, Y + 21.0f }, 10.0f, C.TextMuted, DocBuf);

    // Quick scene TOML export / import pills inside the dockable Presets pane
    const Rectangle2D SaveBtn { PaneBounds.Right() - PadX - 102.0f, Y + 2.0f, 102.0f, 26.0f };
    if (CapsuleButton::Construct(
            Surface,
            SaveBtn,
            ButtonDescriptor{ "Save TOML", ButtonTone::Secondary, 10.2f, 12.0f },
            Pointer).Pressed)
    {
        if (Session.ExportSceneToml("FluidStudioScene.gasscene.toml"))
        {
            Session.DocumentDirty = false;
            Session.StatusMessage = "Saved FluidStudioScene.gasscene.toml";
        }
    }

    const Rectangle2D OpenBtn { SaveBtn.X - 108.0f, Y + 2.0f, 100.0f, 26.0f };
    if (CapsuleButton::Construct(
            Surface,
            OpenBtn,
            ButtonDescriptor{ "Open TOML", ButtonTone::Secondary, 10.2f, 12.0f },
            Pointer).Pressed)
    {
        (void)Session.ImportSceneToml("FluidStudioScene.gasscene.toml");
    }

    const uint32_t Cols = (PaneBounds.Width >= 560.0f) ? 2u : 1u;
    const float Gap = 14.0f;
    const float CardW = (Cols == 2u)
        ? std::max(140.0f, (PaneBounds.Width - PadX * 2.0f - Gap) * 0.5f)
        : std::max(140.0f, PaneBounds.Width - PadX * 2.0f);
    const float CardH = (Presets.size() > 10u) ? 66.0f : 74.0f;
    const float StartX = PaneBounds.X + PadX;
    const float StartY = Y + 48.0f;

    for (std::size_t I = 0u; I < Presets.size(); ++I)
    {
        const uint32_t Col = static_cast<uint32_t>(I % Cols);
        const uint32_t Row = static_cast<uint32_t>(I / Cols);
        const Rectangle2D Card {
            StartX + static_cast<float>(Col) * (CardW + Gap),
            StartY + static_cast<float>(Row) * (CardH + 10.0f),
            CardW,
            CardH,
        };
        if (Card.Bottom() > PaneBounds.Bottom() - 10.0f)
        {
            break;
        }
        const bool Active  = (Session.ActivePresetId == Presets[I].Key);
        const bool Hovered = Card.Contains({ Pointer.X, Pointer.Y });
        Surface.FillRectangle(Card, Active ? C.WarmAccent.ScaledAlpha(0.24f) : (Hovered ? C.CardElevated : C.WellBg), 10.0f);
        Surface.StrokeRectangle(Card, Active ? C.WarmAccent : C.HairlineStrong, 10.0f, 1.0f);

        Surface.Text({ Card.X + 12.0f, Card.Y + 10.0f }, 12.5f, C.TextPrimary, Presets[I].Title.data());
        Surface.Text({ Card.X + 12.0f, Card.Y + 30.0f }, 10.0f, C.TextMuted, Presets[I].Summary.data());

        const auto Doc = SimulationSpace::FluidSpace::FluidPresets::ConstructPreset(Presets[I].Key);
        const Rectangle2D Bar { Card.X + 12.0f, Card.Bottom() - 12.0f, Card.Width - 24.0f, 5.0f };
        constexpr uint32_t Slices = 16u;
        const float SW = Bar.Width / static_cast<float>(Slices);
        for (uint32_t S = 0u; S < Slices; ++S)
        {
            const float Temp = (static_cast<float>(S) / static_cast<float>(Slices - 1u)) * 4.2f;
            const auto ColLin = SimulationSpace::FluidSpace::FluidPresets::EvaluateBlackbodyPalette(
                Temp, Doc.Shading.TemperatureScale, Doc.Shading.ColorPalette);
            const auto R = static_cast<uint8_t>(std::clamp(ColLin.R / (1.0f + ColLin.R) * 255.0f, 0.0f, 255.0f));
            const auto G = static_cast<uint8_t>(std::clamp(ColLin.G / (1.0f + ColLin.G) * 255.0f, 0.0f, 255.0f));
            const auto B = static_cast<uint8_t>(std::clamp(ColLin.B / (1.0f + ColLin.B) * 255.0f, 0.0f, 255.0f));
            Surface.FillRectangle({ Bar.X + static_cast<float>(S) * SW, Bar.Y, SW + 0.5f, Bar.Height }, { R, G, B, 255u }, 0.0f);
        }

        if (Hovered && Pointer.Pressed)
        {
            Session.LoadPreset(Presets[I].Key);
            Session.PresetBrowserOpen = false;
        }
    }
}

void FluidPresetBrowser::RenderModal(
    PixelSpace&            Surface,
    Rectangle2D            WindowBounds,
    FluidWorkspaceSession& Session,
    const ControlPointer&  Pointer) noexcept
{
    if (!Session.PresetBrowserOpen) return;

    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(WindowBounds, { 6u, 8u, 12u, 190u }, 0.0f);

    const float MW = std::min(760.0f, WindowBounds.Width - 60.0f);
    const float MH = std::min(480.0f, WindowBounds.Height - 60.0f);
    const Rectangle2D Modal {
        WindowBounds.Center().X - MW * 0.5f,
        WindowBounds.Center().Y - MH * 0.5f,
        MW,
        MH,
    };
    Surface.FillRectangle(Modal, C.PanelBg, 18.0f);
    Surface.StrokeRectangle(Modal, C.HairlineStrong, 18.0f, 1.2f);

    Surface.Text({ Modal.X + 20.0f, Modal.Y + 18.0f }, 15.0f, C.TextPrimary, "FLUID PYRO PRESET GALLERY (10 PRESETS)");
    if (CapsuleButton::Construct(
            Surface,
            { Modal.Right() - 88.0f, Modal.Y + 14.0f, 68.0f, 26.0f },
            ButtonDescriptor{ "Close", ButtonTone::Secondary, 10.5f, 12.0f },
            Pointer).Pressed)
    {
        Session.PresetBrowserOpen = false;
        return;
    }

    const auto& Presets = SimulationSpace::FluidSpace::FluidPresets::ListPresets();
    constexpr uint32_t Cols = 2u;
    const float Pad = 14.0f;
    const float CardW = (Modal.Width - 40.0f - Pad) * 0.5f;
    const float CardH = 68.0f;
    const float StartX = Modal.X + 20.0f;
    const float StartY = Modal.Y + 56.0f;

    for (std::size_t I = 0u; I < Presets.size(); ++I)
    {
        const uint32_t Col = static_cast<uint32_t>(I % Cols);
        const uint32_t Row = static_cast<uint32_t>(I / Cols);
        const Rectangle2D Card {
            StartX + static_cast<float>(Col) * (CardW + Pad),
            StartY + static_cast<float>(Row) * (CardH + 10.0f),
            CardW,
            CardH,
        };
        const bool Active  = (Session.ActivePresetId == Presets[I].Key);
        const bool Hovered = Card.Contains({ Pointer.X, Pointer.Y });
        Surface.FillRectangle(Card, Active ? C.WarmAccent.ScaledAlpha(0.24f) : (Hovered ? C.CardElevated : C.WellBg), 10.0f);
        Surface.StrokeRectangle(Card, Active ? C.WarmAccent : C.HairlineStrong, 10.0f, 1.0f);

        Surface.Text({ Card.X + 12.0f, Card.Y + 10.0f }, 12.5f, C.TextPrimary, Presets[I].Title.data());
        Surface.Text({ Card.X + 12.0f, Card.Y + 29.0f }, 10.0f, C.TextMuted, Presets[I].Summary.data());

        // Blackbody preview footer bar
        const auto Doc = SimulationSpace::FluidSpace::FluidPresets::ConstructPreset(Presets[I].Key);
        const Rectangle2D Bar { Card.X + 12.0f, Card.Bottom() - 12.0f, Card.Width - 24.0f, 5.0f };
        constexpr uint32_t Slices = 16u;
        const float SW = Bar.Width / static_cast<float>(Slices);
        for (uint32_t S = 0u; S < Slices; ++S)
        {
            const float Temp = (static_cast<float>(S) / static_cast<float>(Slices - 1u)) * 4.2f;
            const auto ColLin = SimulationSpace::FluidSpace::FluidPresets::EvaluateBlackbodyPalette(
                Temp, Doc.Shading.TemperatureScale, Doc.Shading.ColorPalette);
            const auto R = static_cast<uint8_t>(std::clamp(ColLin.R / (1.0f + ColLin.R) * 255.0f, 0.0f, 255.0f));
            const auto G = static_cast<uint8_t>(std::clamp(ColLin.G / (1.0f + ColLin.G) * 255.0f, 0.0f, 255.0f));
            const auto B = static_cast<uint8_t>(std::clamp(ColLin.B / (1.0f + ColLin.B) * 255.0f, 0.0f, 255.0f));
            Surface.FillRectangle({ Bar.X + static_cast<float>(S) * SW, Bar.Y, SW + 0.5f, Bar.Height }, { R, G, B, 255u }, 0.0f);
        }

        if (Hovered && Pointer.Pressed)
        {
            Session.LoadPreset(Presets[I].Key);
            Session.PresetBrowserOpen = false;
        }
    }
}

} // namespace Frontier::Workspaces

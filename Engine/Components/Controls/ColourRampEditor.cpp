/*============================================================================================================================================
                                                          COLOURRAMPEDITOR.CPP
============================================================================================================================================*/
// 🎨 Implementation of reusable ColourRampEditor with smooth multi-stop strip, draggable stop handles, mode/wrap/curve/space toggles & preset bar

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ColourRampEditor.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/ColourPresetRepository.h"
#include "Engine/ColourSpace/ColourRampSampler.h"
#include "Engine/ColourSpace/TransferFunction.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::ColourRampEditor
{

namespace
{

[[nodiscard]] ColorRgba LinearToDisplayRgba(ColourSpace::LinearRgba LinearTint) noexcept
{
    const auto Srgb = ColourSpace::ConvertLinearToSrgb8(LinearTint, ColourSpace::TransferCurveCategory::PureGamma22);
    return { Srgb.Red, Srgb.Green, Srgb.Blue, Srgb.Alpha };
}

[[nodiscard]] bool ConstructMiniChip(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    const char*           Label,
    bool                  Selected,
    const ControlPointer& Pointer,
    const ControlColorSet& ThemeColors,
    float                 Opacity) noexcept
{
    const bool Hovered = ControlAppearance::QueryPointerOver(Bounds, Pointer);
    const ColorQuad FillQuad = Selected
        ? ThemeColors.Accent.Faded(0.24f * Opacity)
        : (Hovered ? ThemeColors.Raised.Faded(Opacity) : ThemeColors.Inset.Faded(Opacity));
    const ColorQuad StrokeQuad = Selected
        ? ThemeColors.Accent.Faded(Opacity)
        : ThemeColors.StrokeStrong.Faded(Opacity);
    const ColorQuad InkQuad = Selected
        ? ThemeColors.Text.Faded(Opacity)
        : ThemeColors.TextDim.Faded(Opacity);

    Surface.FillRectangle(Bounds, FillQuad.ToRgba8(), 5.0f);
    Surface.StrokeRectangle(Bounds, StrokeQuad.ToRgba8(), 5.0f, Selected ? 1.25f : 0.9f);
    Surface.TextCentered(Bounds, 9.2f, InkQuad.ToRgba8(), Label);
    return Hovered && Pointer.Pressed;
}

void ConstructCheckerboardUnderlay(PixelSpace& Surface, Rectangle2D Bounds) noexcept
{
    Surface.PushClip(Bounds);
    constexpr float Cell = 6.0f;
    const int Cols = static_cast<int>(std::ceil(Bounds.Width / Cell));
    const int Rows = static_cast<int>(std::ceil(Bounds.Height / Cell));
    for (int R = 0; R < Rows; ++R)
    {
        for (int C = 0; C < Cols; ++C)
        {
            const bool Light = ((R + C) & 1) == 0;
            const ColorRgba Tile = Light ? ColorRgba{ 62u, 64u, 70u, 255u } : ColorRgba{ 42u, 44u, 50u, 255u };
            Surface.FillRectangle({ Bounds.X + static_cast<float>(C) * Cell, Bounds.Y + static_cast<float>(R) * Cell, Cell, Cell }, Tile, 0.0f);
        }
    }
    Surface.PopClip();
}

void EnsureEditableMultiStop(ColourSpace::ColourSourceProfile& Profile)
{
    if (!std::holds_alternative<ColourSpace::MultiStopGradientSource>(Profile.Source)
        && !std::holds_alternative<ColourSpace::QuantizedRampSource>(Profile.Source))
    {
        ColourSpace::MultiStopGradientSource Grad {};
        Grad.Stops     = ColourSpace::ExtractOrderedStops(Profile.Source);
        Profile.Source = std::move(Grad);
    }
}

[[nodiscard]] std::vector<ColourSpace::ColourStop>* AccessMutableStops(ColourSpace::ColourSourceProfile& Profile) noexcept
{
    if (auto* Grad = std::get_if<ColourSpace::MultiStopGradientSource>(&Profile.Source))
    {
        return &Grad->Stops;
    }
    if (auto* Quant = std::get_if<ColourSpace::QuantizedRampSource>(&Profile.Source))
    {
        return &Quant->BandStops;
    }
    if (auto* Noise = std::get_if<ColourSpace::SpatialNoiseFillSource>(&Profile.Source))
    {
        return &Noise->Stops;
    }
    return nullptr;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              GRADIENT BAR & FULL EDITOR
//------------------------------------------------------------------------------------------------------------------------

void ConstructGradientBar(
    PixelSpace&                              Surface,
    Rectangle2D                              StripBounds,
    const ColourSpace::ColourSourceProfile&  Profile,
    float                                    CornerRadius,
    bool                                     HighlightDropHalo) noexcept
{
    if (StripBounds.Width <= 2.0f || StripBounds.Height <= 2.0f)
    {
        return;
    }

    ConstructCheckerboardUnderlay(Surface, StripBounds);

    ImDrawList* DrawList = Surface.QueryCanvasBuffer();
    if (DrawList != nullptr)
    {
        Surface.PushClip(StripBounds);
        const Rectangle2D Phys = Surface.ToPhysical(StripBounds);
        const std::vector<ColourSpace::ColourStop> Stops = ColourSpace::ExtractOrderedStops(Profile.Source);
        ColourSpace::ColourRangeTransform EffectiveRange = Profile.Range;
        if (std::holds_alternative<ColourSpace::QuantizedRampSource>(Profile.Source))
        {
            EffectiveRange.BlendCurve = ColourSpace::ColourBlendCurve::Stepped;
        }

        constexpr uint32_t Segments = 64u;
        const float SegW = Phys.Width / static_cast<float>(Segments);
        for (uint32_t I = 0u; I < Segments; ++I)
        {
            const float T0 = static_cast<float>(I) / static_cast<float>(Segments);
            const float T1 = static_cast<float>(I + 1u) / static_cast<float>(Segments);
            const ColorRgba C0 = LinearToDisplayRgba(ColourSpace::SampleColourStopsAt(Stops, T0, EffectiveRange));
            const ColorRgba C1 = LinearToDisplayRgba(ColourSpace::SampleColourStopsAt(Stops, T1, EffectiveRange));
            const ImU32 Im0    = IM_COL32(C0.Red, C0.Green, C0.Blue, C0.Alpha);
            const ImU32 Im1    = IM_COL32(C1.Red, C1.Green, C1.Blue, C1.Alpha);
            const float X0     = Phys.X + static_cast<float>(I) * SegW;
            const float X1     = (I + 1u == Segments) ? (Phys.X + Phys.Width) : (X0 + SegW + 0.5f);
            DrawList->AddRectFilledMultiColor(ImVec2(X0, Phys.Y), ImVec2(X1, Phys.Y + Phys.Height), Im0, Im1, Im1, Im0);
        }
        Surface.PopClip();
    }

    const ControlColorSet& Colors = ControlAppearance::Colors();
    if (HighlightDropHalo)
    {
        Surface.StrokeRectangle(StripBounds.Inset(-2.0f), Colors.Accent.ToRgba8(), CornerRadius + 2.0f, 2.2f);
    }
    else
    {
        Surface.StrokeRectangle(StripBounds, Colors.StrokeStrong.ToRgba8(), CornerRadius, 1.0f);
    }
}

bool Construct(
    PixelSpace&                       Surface,
    Rectangle2D                       Bounds,
    std::string_view                  HeadingTitle,
    ColourSpace::ColourSourceProfile& Profile,
    ColourRampEditorSession&          Session,
    const ControlPointer&             Pointer,
    bool                              HighlightDropTarget,
    float                             Opacity)
{
    Session.Modified = false;
    const ControlColorSet& Colors = ControlAppearance::Colors();

    // Container card
    Surface.FillRectangle(Bounds, Colors.SectionSurface.Faded(Opacity).ToRgba8(), 10.0f);
    Surface.StrokeRectangle(Bounds, Colors.StrokeStrong.Faded(Opacity).ToRgba8(), 10.0f, 1.0f);

    const float Pad = 10.0f;
    float CurY      = Bounds.Y + Pad;
    const float InnerW = std::max(80.0f, Bounds.Width - 2.0f * Pad);

    // 1. Heading + Category Mode Pills
    std::string HeadingCopy(HeadingTitle);
    Surface.Text({ Bounds.X + Pad, CurY + 2.0f }, 10.5f, Colors.Text.Faded(Opacity).ToRgba8(), HeadingCopy.c_str());

    const auto CurrentCat = ColourSpace::QueryColourSourceCategory(Profile.Source);
    constexpr std::array<std::pair<ColourSpace::ColourSourceCategory, const char*>, 5> ModePills = {{
        { ColourSpace::ColourSourceCategory::SolidPigment,      "Solid"    },
        { ColourSpace::ColourSourceCategory::LinearFill,        "Fill"     },
        { ColourSpace::ColourSourceCategory::MultiStopGradient, "Gradient" },
        { ColourSpace::ColourSourceCategory::QuantizedRamp,     "Quant"    },
        { ColourSpace::ColourSourceCategory::BlackbodyThermal,  "Thermal"  },
    }};

    float PillRight = Bounds.Right() - Pad;
    for (int I = static_cast<int>(ModePills.size()) - 1; I >= 0; --I)
    {
        const auto [Cat, Label] = ModePills[static_cast<std::size_t>(I)];
        const float W = std::max(38.0f, Surface.MeasureTextWidth(9.0f, Label) + 12.0f);
        PillRight -= W;
        if (ConstructMiniChip(Surface, { PillRight, CurY, W, 19.0f }, Label, CurrentCat == Cat, Pointer, Colors, Opacity))
        {
            const auto ExistingStops = ColourSpace::ExtractOrderedStops(Profile.Source);
            if (Cat == ColourSpace::ColourSourceCategory::SolidPigment)
            {
                Profile.Source = ColourSpace::SolidPigmentSource { ExistingStops.front().LinearTint };
            }
            else if (Cat == ColourSpace::ColourSourceCategory::LinearFill)
            {
                Profile.Source = ColourSpace::LinearFillSource {
                    ExistingStops.front().LinearTint,
                    ExistingStops.back().LinearTint,
                    ExistingStops[ExistingStops.size() / 2u].LinearTint,
                    ExistingStops.size() >= 3u,
                };
            }
            else if (Cat == ColourSpace::ColourSourceCategory::MultiStopGradient)
            {
                Profile.Source = ColourSpace::MultiStopGradientSource { {}, ExistingStops };
            }
            else if (Cat == ColourSpace::ColourSourceCategory::QuantizedRamp)
            {
                Profile.Source = ColourSpace::QuantizedRampSource { ExistingStops };
            }
            else if (Cat == ColourSpace::ColourSourceCategory::BlackbodyThermal)
            {
                Profile.Source = ColourSpace::BlackbodyThermalSource {};
            }
            Session.SelectedStopIndex = 0u;
            Session.Modified          = true;
        }
        PillRight -= 4.0f;
    }

    CurY += 25.0f;

    // 2. Main Interactive Gradient Strip (ContentBrowser drop target!)
    const Rectangle2D StripRect { Bounds.X + Pad, CurY, InnerW, 28.0f };
    Session.LastStripBounds = StripRect;
    ConstructGradientBar(Surface, StripRect, Profile, 6.0f, HighlightDropTarget);

    // Click inside gradient strip to insert a new stop (Shift+Click or click empty area in gradient mode)
    const std::vector<ColourSpace::ColourStop> OrderedStops = ColourSpace::ExtractOrderedStops(Profile.Source);
    if (ControlAppearance::QueryPointerOver(StripRect, Pointer) && Pointer.Pressed && !Session.DraggingStop)
    {
        const float ClickPos01 = std::clamp((Pointer.X - StripRect.X) / std::max(1.0f, StripRect.Width), 0.0f, 1.0f);
        bool NearExisting = false;
        for (std::size_t S = 0u; S < OrderedStops.size(); ++S)
        {
            if (std::fabs(OrderedStops[S].Position01 - ClickPos01) < 0.03f)
            {
                Session.SelectedStopIndex = S;
                Session.DraggingStop      = true;
                NearExisting              = true;
                break;
            }
        }
        if (!NearExisting)
        {
            EnsureEditableMultiStop(Profile);
            if (auto* Stops = AccessMutableStops(Profile); Stops != nullptr && Stops->size() < ColourSpace::MaxAuthoringRampStops)
            {
                const auto Sampled = ColourSpace::SampleColourStopsAt(OrderedStops, ClickPos01, Profile.Range);
                Stops->push_back({ ClickPos01, Sampled });
                std::stable_sort(Stops->begin(), Stops->end(), [](const ColourSpace::ColourStop& A, const ColourSpace::ColourStop& B) noexcept {
                    return A.Position01 < B.Position01;
                });
                for (std::size_t K = 0u; K < Stops->size(); ++K)
                {
                    if (std::fabs((*Stops)[K].Position01 - ClickPos01) < 1.0e-4f)
                    {
                        Session.SelectedStopIndex = K;
                        break;
                    }
                }
                Session.DraggingStop = true;
                Session.Modified     = true;
            }
        }
    }

    // 3. Draggable Stop Handles Row beneath gradient bar
    const float HandleY = StripRect.Bottom() + 10.0f;
    const std::vector<ColourSpace::ColourStop> DisplayStops = ColourSpace::ExtractOrderedStops(Profile.Source);
    if (Session.SelectedStopIndex >= DisplayStops.size())
    {
        Session.SelectedStopIndex = DisplayStops.empty() ? 0u : (DisplayStops.size() - 1u);
    }

    for (std::size_t I = 0u; I < DisplayStops.size(); ++I)
    {
        const float HandleX = StripRect.X + std::clamp(DisplayStops[I].Position01, 0.0f, 1.0f) * StripRect.Width;
        const bool  Active  = (I == Session.SelectedStopIndex);
        const ColorRgba Fill = LinearToDisplayRgba(DisplayStops[I].LinearTint);

        Surface.StrokeLine({ HandleX, StripRect.Bottom() }, { HandleX, HandleY - 5.0f }, Colors.StrokeStrong.ToRgba8(), 1.2f);
        Surface.FillCircle({ HandleX, HandleY }, Active ? 6.5f : 5.2f, { Fill.Red, Fill.Green, Fill.Blue, 255u });
        Surface.StrokeCircle(
            { HandleX, HandleY },
            Active ? 6.5f : 5.2f,
            Active ? Colors.Accent.ToRgba8() : Colors.Text.ToRgba8(),
            Active ? 2.0f : 1.1f);

        const Rectangle2D HitBox { HandleX - 8.0f, HandleY - 8.0f, 16.0f, 16.0f };
        if (ControlAppearance::QueryPointerOver(HitBox, Pointer) && Pointer.Pressed)
        {
            Session.SelectedStopIndex = I;
            Session.DraggingStop      = true;
        }
    }

    if (Session.DraggingStop)
    {
        if (Pointer.Down)
        {
            EnsureEditableMultiStop(Profile);
            if (auto* Stops = AccessMutableStops(Profile); Stops != nullptr && Session.SelectedStopIndex < Stops->size())
            {
                const float NewPos = std::clamp((Pointer.X - StripRect.X) / std::max(1.0f, StripRect.Width), 0.0f, 1.0f);
                if (std::fabs((*Stops)[Session.SelectedStopIndex].Position01 - NewPos) > 1.0e-4f)
                {
                    (*Stops)[Session.SelectedStopIndex].Position01 = NewPos;
                    Session.Modified = true;
                }
            }
        }
        else
        {
            Session.DraggingStop = false;
            if (auto* Stops = AccessMutableStops(Profile))
            {
                std::stable_sort(Stops->begin(), Stops->end(), [](const ColourSpace::ColourStop& A, const ColourSpace::ColourStop& B) noexcept {
                    return A.Position01 < B.Position01;
                });
            }
        }
    }

    CurY = HandleY + 14.0f;

    // 4. Wrap / Curve / Interpolation Space Controls + Delete Stop Button
    float ControlX = Bounds.X + Pad;
    const char* WrapLabel =
        (Profile.Range.WrapBehaviour == ColourSpace::ColourWrapBehaviour::Repeat) ? "Wrap: Repeat" :
        (Profile.Range.WrapBehaviour == ColourSpace::ColourWrapBehaviour::Mirror) ? "Wrap: Mirror" : "Wrap: Clamp";
    if (ConstructMiniChip(Surface, { ControlX, CurY, 76.0f, 20.0f }, WrapLabel, false, Pointer, Colors, Opacity))
    {
        Profile.Range.WrapBehaviour = static_cast<ColourSpace::ColourWrapBehaviour>((static_cast<uint8_t>(Profile.Range.WrapBehaviour) + 1u) % 3u);
        Session.Modified            = true;
    }
    ControlX += 80.0f;

    const char* CurveLabel =
        (Profile.Range.BlendCurve == ColourSpace::ColourBlendCurve::Smooth)  ? "Smooth" :
        (Profile.Range.BlendCurve == ColourSpace::ColourBlendCurve::Stepped) ? "Stepped" : "Linear";
    if (ConstructMiniChip(Surface, { ControlX, CurY, 54.0f, 20.0f }, CurveLabel, false, Pointer, Colors, Opacity))
    {
        Profile.Range.BlendCurve = static_cast<ColourSpace::ColourBlendCurve>((static_cast<uint8_t>(Profile.Range.BlendCurve) + 1u) % 3u);
        Session.Modified         = true;
    }
    ControlX += 58.0f;

    const char* SpaceLabel =
        (Profile.Range.InterpolationSpace == ColourSpace::ColourInterpolationSpace::Oklab)  ? "Oklab" :
        (Profile.Range.InterpolationSpace == ColourSpace::ColourInterpolationSpace::AcesCg) ? "ACEScg" : "LinRGB";
    if (ConstructMiniChip(Surface, { ControlX, CurY, 52.0f, 20.0f }, SpaceLabel, Profile.Range.InterpolationSpace == ColourSpace::ColourInterpolationSpace::Oklab, Pointer, Colors, Opacity))
    {
        Profile.Range.InterpolationSpace = static_cast<ColourSpace::ColourInterpolationSpace>((static_cast<uint8_t>(Profile.Range.InterpolationSpace) + 1u) % 3u);
        Session.Modified                 = true;
    }

    if (DisplayStops.size() > 2u)
    {
        if (ConstructMiniChip(Surface, { Bounds.Right() - Pad - 52.0f, CurY, 52.0f, 20.0f }, "- Stop", false, Pointer, Colors, Opacity))
        {
            EnsureEditableMultiStop(Profile);
            if (auto* Stops = AccessMutableStops(Profile); Stops != nullptr && Stops->size() > 2u && Session.SelectedStopIndex < Stops->size())
            {
                Stops->erase(Stops->begin() + static_cast<std::ptrdiff_t>(Session.SelectedStopIndex));
                Session.SelectedStopIndex = std::min(Session.SelectedStopIndex, Stops->size() - 1u);
                Session.Modified          = true;
            }
        }
    }

    CurY += 26.0f;

    // 5. Quick Built-In Studio Presets Strip (first 6 Studio presets: rainbow, thermal, aurora, plasma, ocean, sunset)
    const auto& BuiltIns = ColourSpace::QueryBuiltInColourRampPresets();
    const std::size_t PresetCount = std::min<std::size_t>(6u, BuiltIns.size());
    if (PresetCount > 0u && CurY + 26.0f <= Bounds.Bottom())
    {
        const float Gap     = 6.0f;
        const float SwatchW = (InnerW - Gap * static_cast<float>(PresetCount - 1u)) / static_cast<float>(PresetCount);
        for (std::size_t P = 0u; P < PresetCount; ++P)
        {
            const Rectangle2D PresetRect { Bounds.X + Pad + static_cast<float>(P) * (SwatchW + Gap), CurY, SwatchW, 20.0f };
            const auto PresetProfile = ColourSpace::ConstructProfileFromRampPreset(BuiltIns[P]);
            ConstructGradientBar(Surface, PresetRect, PresetProfile, 4.0f, false);
            if (ControlAppearance::QueryPointerOver(PresetRect, Pointer))
            {
                Surface.StrokeRectangle(PresetRect, Colors.Accent.ToRgba8(), 4.0f, 1.4f);
                if (Pointer.Pressed)
                {
                    Profile                   = PresetProfile;
                    Session.SelectedStopIndex = 0u;
                    Session.Modified          = true;
                }
            }
        }
    }

    return Session.Modified;
}

void RegisterDropSlot(
    ContentBrowserPanel&                                                           Browser,
    std::string_view                                                               SlotIdentifier,
    std::string_view                                                               DisplayTitle,
    const ColourRampEditorSession&                                                 Session,
    ColourSpace::ColourSourceProfile*                                              TargetProfile,
    std::function<void(const ColourSpace::ColourSourceProfile&, std::string_view)> NotifyAssigned)
{
    ColourRampSlotRegistration Reg {};
    Reg.SlotIdentifier     = std::string(SlotIdentifier);
    Reg.DisplayTitle       = std::string(DisplayTitle);
    Reg.ScreenBounds       = Session.LastStripBounds;
    Reg.TargetProfile      = TargetProfile;
    Reg.NotifyRampAssigned = std::move(NotifyAssigned);
    Browser.RegisterColourRampSlot(Reg);
}

} // namespace Frontier::ColourRampEditor

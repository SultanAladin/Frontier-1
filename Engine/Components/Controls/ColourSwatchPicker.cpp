/*============================================================================================================================================
                                                            COLOURSWATCHPICKER.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable ColourSwatchPicker (2D S/L field, Hue spectrum strip, Alpha bar, Kelvin quick bar & WCAG readout)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ColourSwatchPicker.h"

#include "Engine/ColourSpace/ChromaticitySpace.h"
#include "Engine/ColourSpace/SpectralDistribution.h"
#include "Engine/ColourSpace/TransferFunction.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::ColourSwatchPicker
{

namespace
{

[[nodiscard]] ImU32 SrgbToImU32(ColourSpace::SrgbRgba8 C) noexcept
{
    return IM_COL32(C.Red, C.Green, C.Blue, C.Alpha);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 COLOUR SWATCH PICKER
//------------------------------------------------------------------------------------------------------------------------

bool Construct(
    PixelSpace&                Surface,
    Rectangle2D                Bounds,
    std::string_view           HeadingTitle,
    ColourSpace::LinearRgba&   TargetLinearTint,
    ColourSwatchPickerSession& Session,
    const ControlPointer&      Pointer,
    bool                       ShowKelvinStrip,
    float                      Opacity)
{
    Session.Modified = false;
    const ControlColorSet& Colors = ControlAppearance::Colors();

    Surface.FillRectangle(Bounds, Colors.SectionSurface.Faded(Opacity).ToRgba8(), 10.0f);
    Surface.StrokeRectangle(Bounds, Colors.StrokeStrong.Faded(Opacity).ToRgba8(), 10.0f, 1.0f);

    const float Pad    = 10.0f;
    const float InnerW = std::max(100.0f, Bounds.Width - 2.0f * Pad);
    float CurY         = Bounds.Y + Pad;

    const ColourSpace::SrgbRgba8 DisplaySrgb = ColourSpace::ConvertLinearToSrgb8(TargetLinearTint, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
    ColourSpace::HslTriplet Hsl              = ColourSpace::ConvertSrgb8ToHsl(DisplaySrgb);
    if (Hsl.SaturationPercent > 1.0f && Hsl.LightnessPercent > 1.0f && Hsl.LightnessPercent < 99.0f)
    {
        Session.CachedHueDegrees = Hsl.HueDegrees;
    }
    else
    {
        Hsl.HueDegrees = Session.CachedHueDegrees;
    }

    // 1. Heading + Preview Swatch Card with WCAG Contrast HEX/Linear readout
    std::string HeadingCopy(HeadingTitle);
    Surface.Text({ Bounds.X + Pad, CurY }, 10.5f, Colors.Text.Faded(Opacity).ToRgba8(), HeadingCopy.c_str());
    CurY += 18.0f;

    const Rectangle2D PreviewRect { Bounds.X + Pad, CurY, InnerW, 34.0f };
    Session.LastPreviewBounds = PreviewRect;
    Surface.FillRectangle(PreviewRect, { DisplaySrgb.Red, DisplaySrgb.Green, DisplaySrgb.Blue, 255u }, 7.0f);
    const ColourSpace::ContrastInkSet Ink = ColourSpace::SelectContrastInk(DisplaySrgb);
    const std::string HexStr = ColourSpace::FormatHexUpper(DisplaySrgb, false);
    char LinBuf[64] {};
    std::snprintf(LinBuf, sizeof(LinBuf), "LIN %.2f  %.2f  %.2f  A %.2f", TargetLinearTint.Red, TargetLinearTint.Green, TargetLinearTint.Blue, TargetLinearTint.Alpha);
    Surface.Text({ PreviewRect.X + 10.0f, PreviewRect.Y + 5.0f }, 10.5f, { Ink.PrimaryInk.Red, Ink.PrimaryInk.Green, Ink.PrimaryInk.Blue, 255u }, HexStr.c_str());
    Surface.Text({ PreviewRect.X + 10.0f, PreviewRect.Y + 19.0f }, 8.6f, { Ink.MutedInk.Red, Ink.MutedInk.Green, Ink.MutedInk.Blue, Ink.MutedInk.Alpha }, LinBuf);
    Surface.StrokeRectangle(PreviewRect, { Ink.BorderInk.Red, Ink.BorderInk.Green, Ink.BorderInk.Blue, Ink.BorderInk.Alpha }, 7.0f, 1.0f);
    CurY += 42.0f;

    // 2. 2D Saturation (X) x Lightness (Y) Field
    const float FieldH = std::max(44.0f, Bounds.Bottom() - CurY - (ShowKelvinStrip ? 74.0f : 50.0f));
    const Rectangle2D FieldRect { Bounds.X + Pad, CurY, InnerW, FieldH };
    ImDrawList* DrawList = Surface.QueryCanvasBuffer();
    if (DrawList != nullptr)
    {
        Surface.PushClip(FieldRect);
        const Rectangle2D Phys = Surface.ToPhysical(FieldRect);
        constexpr int GridX = 16;
        constexpr int GridY = 12;
        const float CellW   = Phys.Width / static_cast<float>(GridX);
        const float CellH   = Phys.Height / static_cast<float>(GridY);
        for (int Gy = 0; Gy < GridY; ++Gy)
        {
            const float L0 = (1.0f - static_cast<float>(Gy) / static_cast<float>(GridY)) * 100.0f;
            const float L1 = (1.0f - static_cast<float>(Gy + 1) / static_cast<float>(GridY)) * 100.0f;
            for (int Gx = 0; Gx < GridX; ++Gx)
            {
                const float S0 = (static_cast<float>(Gx) / static_cast<float>(GridX)) * 100.0f;
                const float S1 = (static_cast<float>(Gx + 1) / static_cast<float>(GridX)) * 100.0f;
                const ImU32 C00 = SrgbToImU32(ColourSpace::ConvertHslToSrgb8({ Session.CachedHueDegrees, S0, L0 }));
                const ImU32 C10 = SrgbToImU32(ColourSpace::ConvertHslToSrgb8({ Session.CachedHueDegrees, S1, L0 }));
                const ImU32 C11 = SrgbToImU32(ColourSpace::ConvertHslToSrgb8({ Session.CachedHueDegrees, S1, L1 }));
                const ImU32 C01 = SrgbToImU32(ColourSpace::ConvertHslToSrgb8({ Session.CachedHueDegrees, S0, L1 }));
                const float X0 = Phys.X + static_cast<float>(Gx) * CellW;
                const float Y0 = Phys.Y + static_cast<float>(Gy) * CellH;
                DrawList->AddRectFilledMultiColor(ImVec2(X0, Y0), ImVec2(X0 + CellW + 0.5f, Y0 + CellH + 0.5f), C00, C10, C11, C01);
            }
        }
        Surface.PopClip();
    }
    Surface.StrokeRectangle(FieldRect, Colors.StrokeStrong.ToRgba8(), 6.0f, 1.0f);

    if (ControlAppearance::QueryPointerOver(FieldRect, Pointer) && Pointer.Pressed) Session.DraggingField = true;
    if (Session.DraggingField)
    {
        if (Pointer.Down)
        {
            const float NewSat = std::clamp((Pointer.X - FieldRect.X) / std::max(1.0f, FieldRect.Width), 0.0f, 1.0f) * 100.0f;
            const float NewLit = (1.0f - std::clamp((Pointer.Y - FieldRect.Y) / std::max(1.0f, FieldRect.Height), 0.0f, 1.0f)) * 100.0f;
            const auto Updated = ColourSpace::ConvertHslToSrgb8({ Session.CachedHueDegrees, NewSat, NewLit }, DisplaySrgb.Alpha);
            TargetLinearTint   = ColourSpace::ConvertSrgb8ToLinear(Updated, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
            Hsl.SaturationPercent = NewSat;
            Hsl.LightnessPercent  = NewLit;
            Session.Modified      = true;
        }
        else
        {
            Session.DraggingField = false;
        }
    }

    const Point2D Reticle {
        FieldRect.X + (Hsl.SaturationPercent * 0.01f) * FieldRect.Width,
        FieldRect.Y + (1.0f - Hsl.LightnessPercent * 0.01f) * FieldRect.Height,
    };
    Surface.StrokeCircle(Reticle, 5.5f, { 255u, 255u, 255u, 255u }, 1.8f);
    Surface.StrokeCircle(Reticle, 6.8f, { 12u, 14u, 18u, 220u }, 1.0f);
    CurY = FieldRect.Bottom() + 8.0f;

    // 3. Hue Spectrum Strip
    const Rectangle2D HueRect { Bounds.X + Pad, CurY, InnerW, 14.0f };
    if (DrawList != nullptr)
    {
        Surface.PushClip(HueRect);
        const Rectangle2D Phys = Surface.ToPhysical(HueRect);
        constexpr int Segs = 24;
        const float SegW   = Phys.Width / static_cast<float>(Segs);
        for (int I = 0; I < Segs; ++I)
        {
            const float H0 = (static_cast<float>(I) / static_cast<float>(Segs)) * 360.0f;
            const float H1 = (static_cast<float>(I + 1) / static_cast<float>(Segs)) * 360.0f;
            const ImU32 C0 = SrgbToImU32(ColourSpace::ConvertHslToSrgb8({ H0, 100.0f, 50.0f }));
            const ImU32 C1 = SrgbToImU32(ColourSpace::ConvertHslToSrgb8({ H1, 100.0f, 50.0f }));
            const float X0 = Phys.X + static_cast<float>(I) * SegW;
            DrawList->AddRectFilledMultiColor(ImVec2(X0, Phys.Y), ImVec2(X0 + SegW + 0.5f, Phys.Y + Phys.Height), C0, C1, C1, C0);
        }
        Surface.PopClip();
    }
    Surface.StrokeRectangle(HueRect, Colors.StrokeStrong.ToRgba8(), 5.0f, 1.0f);

    if (ControlAppearance::QueryPointerOver(HueRect, Pointer) && Pointer.Pressed) Session.DraggingHue = true;
    if (Session.DraggingHue)
    {
        if (Pointer.Down)
        {
            Session.CachedHueDegrees = std::clamp((Pointer.X - HueRect.X) / std::max(1.0f, HueRect.Width), 0.0f, 1.0f) * 359.5f;
            const auto Updated = ColourSpace::ConvertHslToSrgb8({ Session.CachedHueDegrees, Hsl.SaturationPercent, Hsl.LightnessPercent }, DisplaySrgb.Alpha);
            TargetLinearTint   = ColourSpace::ConvertSrgb8ToLinear(Updated, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
            Session.Modified   = true;
        }
        else
        {
            Session.DraggingHue = false;
        }
    }
    const float HueKnobX = HueRect.X + (Session.CachedHueDegrees / 360.0f) * HueRect.Width;
    Surface.FillCircle({ HueKnobX, HueRect.Center().Y }, 6.0f, { 255u, 255u, 255u, 255u });
    Surface.StrokeCircle({ HueKnobX, HueRect.Center().Y }, 6.0f, Colors.StrokeStrong.ToRgba8(), 1.2f);
    CurY = HueRect.Bottom() + 8.0f;

    // 4. Opacity Strip
    const Rectangle2D AlphaRect { Bounds.X + Pad, CurY, InnerW, 12.0f };
    if (DrawList != nullptr)
    {
        const Rectangle2D Phys = Surface.ToPhysical(AlphaRect);
        const ImU32 LeftCol    = IM_COL32(36u, 38u, 44u, 255u);
        const ImU32 RightCol   = IM_COL32(DisplaySrgb.Red, DisplaySrgb.Green, DisplaySrgb.Blue, 255u);
        DrawList->AddRectFilledMultiColor(ImVec2(Phys.X, Phys.Y), ImVec2(Phys.X + Phys.Width, Phys.Y + Phys.Height), LeftCol, RightCol, RightCol, LeftCol);
    }
    Surface.StrokeRectangle(AlphaRect, Colors.StrokeStrong.ToRgba8(), 5.0f, 1.0f);

    if (ControlAppearance::QueryPointerOver(AlphaRect, Pointer) && Pointer.Pressed) Session.DraggingAlpha = true;
    if (Session.DraggingAlpha)
    {
        if (Pointer.Down)
        {
            TargetLinearTint.Alpha = std::clamp((Pointer.X - AlphaRect.X) / std::max(1.0f, AlphaRect.Width), 0.0f, 1.0f);
            Session.Modified       = true;
        }
        else
        {
            Session.DraggingAlpha = false;
        }
    }
    const float AlphaKnobX = AlphaRect.X + std::clamp(TargetLinearTint.Alpha, 0.0f, 1.0f) * AlphaRect.Width;
    Surface.FillCircle({ AlphaKnobX, AlphaRect.Center().Y }, 5.5f, { 255u, 255u, 255u, 255u });
    Surface.StrokeCircle({ AlphaKnobX, AlphaRect.Center().Y }, 5.5f, Colors.StrokeStrong.ToRgba8(), 1.2f);
    CurY = AlphaRect.Bottom() + 8.0f;

    // 5. Optional Planckian Blackbody Kelvin Strip (1400 K .. 10000 K)
    if (ShowKelvinStrip && CurY + 14.0f <= Bounds.Bottom())
    {
        const Rectangle2D KelvinRect { Bounds.X + Pad, CurY, InnerW, 14.0f };
        if (DrawList != nullptr)
        {
            Surface.PushClip(KelvinRect);
            const Rectangle2D Phys = Surface.ToPhysical(KelvinRect);
            constexpr int Segs = 24;
            const float SegW   = Phys.Width / static_cast<float>(Segs);
            for (int I = 0; I < Segs; ++I)
            {
                const float K0 = 1400.0f + (static_cast<float>(I) / static_cast<float>(Segs)) * 8600.0f;
                const float K1 = 1400.0f + (static_cast<float>(I + 1) / static_cast<float>(Segs)) * 8600.0f;
                const ImU32 C0 = SrgbToImU32(ColourSpace::ConvertLinearToSrgb8(ColourSpace::ConvertKelvinToLinearSrgb(K0, 1.0f), ColourSpace::TransferCurveCategory::PureGamma22));
                const ImU32 C1 = SrgbToImU32(ColourSpace::ConvertLinearToSrgb8(ColourSpace::ConvertKelvinToLinearSrgb(K1, 1.0f), ColourSpace::TransferCurveCategory::PureGamma22));
                const float X0 = Phys.X + static_cast<float>(I) * SegW;
                DrawList->AddRectFilledMultiColor(ImVec2(X0, Phys.Y), ImVec2(X0 + SegW + 0.5f, Phys.Y + Phys.Height), C0, C1, C1, C0);
            }
            Surface.PopClip();
        }
        Surface.StrokeRectangle(KelvinRect, Colors.StrokeStrong.ToRgba8(), 4.0f, 1.0f);
        if (ControlAppearance::QueryPointerOver(KelvinRect, Pointer) && Pointer.Pressed)
        {
            const float T    = std::clamp((Pointer.X - KelvinRect.X) / std::max(1.0f, KelvinRect.Width), 0.0f, 1.0f);
            TargetLinearTint = ColourSpace::ConvertKelvinToLinearSrgb(1400.0f + T * 8600.0f, TargetLinearTint.Alpha, ColourSpace::TransferCurveCategory::PiecewiseSrgb);
            Session.Modified = true;
        }
    }

    return Session.Modified;
}

void RegisterDropSlot(
    ContentBrowserPanel&                         Browser,
    std::string_view                             SlotIdentifier,
    std::string_view                             DisplayTitle,
    const ColourSwatchPickerSession&             Session,
    ColourSpace::LinearRgba*                     TargetLinearTint,
    std::function<void(ColourSpace::LinearRgba)> NotifyAssigned)
{
    ColourSlotRegistration Reg {};
    Reg.SlotIdentifier = std::string(SlotIdentifier);
    Reg.DisplayTitle   = std::string(DisplayTitle);
    Reg.ScreenBounds   = Session.LastPreviewBounds;
    Reg.NotifyColourAssigned = [TargetLinearTint, NotifyAssigned = std::move(NotifyAssigned)](ColorRgba DroppedSrgb, const ColourSwatchEntry&) {
        const ColourSpace::LinearRgba Linear = ColourSpace::ConvertSrgb8ToLinear(
            { DroppedSrgb.Red, DroppedSrgb.Green, DroppedSrgb.Blue, DroppedSrgb.Alpha },
            ColourSpace::TransferCurveCategory::PiecewiseSrgb);
        if (TargetLinearTint != nullptr)
        {
            *TargetLinearTint = Linear;
        }
        if (NotifyAssigned)
        {
            NotifyAssigned(Linear);
        }
    };
    Browser.RegisterColourSlot(Reg);
}

} // namespace Frontier::ColourSwatchPicker

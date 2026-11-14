/*============================================================================================================================================
                                                           SCALARCURVEEDITOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable ScalarCurveEditor (0..1 Hermite-smoothed spline, draggable handles & quick preset curve chips)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/ScalarCurveEditor.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

void ScalarCurveProfile::NormalizeKnots() noexcept
{
    if (Knots.empty())
    {
        Knots = { { 0.0f, 1.0f }, { 1.0f, 0.0f } };
        return;
    }
    for (ScalarCurveKnot& K : Knots)
    {
        K.PositionT = std::clamp(K.PositionT, 0.0f, 1.0f);
        K.Amplitude = std::clamp(K.Amplitude, 0.0f, 1.0f);
    }
    std::sort(Knots.begin(), Knots.end(), [](const ScalarCurveKnot& A, const ScalarCurveKnot& B) {
        return A.PositionT < B.PositionT;
    });
}

float ScalarCurveProfile::EvaluateAt(float NormalizedT) const noexcept
{
    if (Knots.empty())
    {
        return 1.0f;
    }
    const float T = std::clamp(NormalizedT, 0.0f, 1.0f);
    if (T <= Knots.front().PositionT) return Knots.front().Amplitude;
    if (T >= Knots.back().PositionT)  return Knots.back().Amplitude;

    for (std::size_t I = 0u; I + 1u < Knots.size(); ++I)
    {
        const ScalarCurveKnot& A = Knots[I];
        const ScalarCurveKnot& B = Knots[I + 1u];
        if (T >= A.PositionT && T <= B.PositionT)
        {
            const float Span = std::max(1.0e-5f, B.PositionT - A.PositionT);
            const float U    = (T - A.PositionT) / Span;
            const float S    = U * U * (3.0f - 2.0f * U); // Smooth cubic Hermite ease
            return A.Amplitude + (B.Amplitude - A.Amplitude) * S;
        }
    }
    return Knots.back().Amplitude;
}

bool ScalarCurveEditor::Construct(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    const char*               Caption,
    ScalarCurveProfile&       Profile,
    ScalarCurveEditorSession& Session,
    const ControlPointer&     Pointer,
    float                     Opacity) noexcept
{
    if (Bounds.Width < 140.0f || Bounds.Height < 86.0f || Opacity <= 0.001f)
    {
        return false;
    }

    bool Modified = false;
    Profile.NormalizeKnots();

    const ColorRgba CardBg    { 26u,  26u,  26u,  static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba PlotBg    { 13u,  14u,  16u,  static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba GridLine  { 255u, 255u, 255u, static_cast<uint8_t>(16.0f * Opacity) };
    const ColorRgba StrokeCol { 232u, 194u, 122u, static_cast<uint8_t>(240.0f * Opacity) };
    const ColorRgba FillTint  { 232u, 194u, 122u, static_cast<uint8_t>(38.0f * Opacity) };
    const ColorRgba TextInk   { 232u, 232u, 232u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba MutedInk  { 132u, 132u, 132u, static_cast<uint8_t>(255.0f * Opacity) };

    Surface.FillRectangle(Bounds, CardBg, 14.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba { 255u, 255u, 255u, static_cast<uint8_t>(20.0f * Opacity) }, 14.0f, 1.0f);

    // Header + quick curve shape chips (Linear / Bell / Decay)
    if (Caption != nullptr)
    {
        Surface.Text({ Bounds.X + 12.0f, Bounds.Y + 9.0f }, 10.5f, TextInk, Caption);
    }

    const char* ChipTitles[3] = { "Linear", "Bell", "Decay" };
    float ChipX = Bounds.Right() - 156.0f;
    for (int C = 0; C < 3; ++C)
    {
        const Rectangle2D ChipRect { ChipX, Bounds.Y + 6.0f, 46.0f, 18.0f };
        const bool Hover = ControlAppearance::QueryPointerOver(ChipRect, Pointer);
        Surface.FillRectangle(ChipRect, Hover ? ColorRgba { 48u, 48u, 48u, 255u } : ColorRgba { 32u, 32u, 32u, 255u }, 9.0f);
        Surface.TextCentered(ChipRect, 8.8f, Hover ? TextInk : MutedInk, ChipTitles[C]);
        if (Hover && Pointer.Pressed)
        {
            if (C == 0)      Profile.Knots = { { 0.0f, 1.0f }, { 1.0f, 0.2f } };
            else if (C == 1) Profile.Knots = { { 0.0f, 0.1f }, { 0.45f, 1.0f }, { 1.0f, 0.05f } };
            else             Profile.Knots = { { 0.0f, 1.0f }, { 0.35f, 0.52f }, { 1.0f, 0.0f } };
            Session.SelectedKnotIndex = 0u;
            Modified = true;
        }
        ChipX += 50.0f;
    }

    // Plot area
    const Rectangle2D Plot { Bounds.X + 12.0f, Bounds.Y + 30.0f, Bounds.Width - 24.0f, Bounds.Height - 42.0f };
    Session.LastPlotBounds = Plot;
    Surface.FillRectangle(Plot, PlotBg, 8.0f);
    Surface.StrokeRectangle(Plot, GridLine, 8.0f, 1.0f);

    // Subtle 4x3 grid hairlines
    for (int G = 1; G <= 3; ++G)
    {
        const float GX = Plot.X + Plot.Width * (static_cast<float>(G) / 4.0f);
        Surface.StrokeLine({ GX, Plot.Y + 2.0f }, { GX, Plot.Bottom() - 2.0f }, GridLine, 0.8f);
    }
    for (int G = 1; G <= 2; ++G)
    {
        const float GY = Plot.Y + Plot.Height * (static_cast<float>(G) / 3.0f);
        Surface.StrokeLine({ Plot.X + 2.0f, GY }, { Plot.Right() - 2.0f, GY }, GridLine, 0.8f);
    }

    // Sampled curve polyline & subtle translucent ribbon columns underneath
    constexpr uint32_t SampleSteps = 44u;
    std::array<Point2D, SampleSteps + 1u> CurvePts {};
    for (uint32_t I = 0u; I <= SampleSteps; ++I)
    {
        const float T = static_cast<float>(I) / static_cast<float>(SampleSteps);
        const float Y = Profile.EvaluateAt(T);
        CurvePts[I]   = {
            Plot.X + 4.0f + (Plot.Width - 8.0f) * T,
            Plot.Bottom() - 4.0f - (Plot.Height - 8.0f) * Y,
        };
        if (I > 0u)
        {
            const std::array<Point2D, 4> Quad = {{
                CurvePts[I - 1u],
                CurvePts[I],
                { CurvePts[I].X, Plot.Bottom() - 3.0f },
                { CurvePts[I - 1u].X, Plot.Bottom() - 3.0f },
            }};
            Surface.FillPolygon(Quad, FillTint);
        }
    }
    Surface.StrokePolyline(CurvePts, StrokeCol, 1.85f, false);

    // Knot handles & drag interaction
    bool HitAnyKnot = false;
    for (std::size_t I = 0u; I < Profile.Knots.size(); ++I)
    {
        const Point2D Pt {
            Plot.X + 4.0f + (Plot.Width - 8.0f) * Profile.Knots[I].PositionT,
            Plot.Bottom() - 4.0f - (Plot.Height - 8.0f) * Profile.Knots[I].Amplitude,
        };
        const float Dx = Pointer.X - Pt.X;
        const float Dy = Pointer.Y - Pt.Y;
        const bool OverKnot = Pointer.Enabled && (Dx * Dx + Dy * Dy <= 81.0f);
        if (OverKnot && Pointer.Pressed)
        {
            Session.SelectedKnotIndex = I;
            Session.DraggingKnot      = true;
            HitAnyKnot                = true;
        }
        const bool Selected = (I == Session.SelectedKnotIndex);
        Surface.FillCircle(Pt, Selected ? 5.2f : 4.2f, Selected ? StrokeCol : ColorRgba { 236u, 236u, 236u, 255u });
        Surface.StrokeCircle(Pt, Selected ? 5.2f : 4.2f, ColorRgba { 16u, 16u, 16u, 255u }, 1.4f);
    }

    // Click empty plot area to insert a new knot (up to 8 knots)
    if (!HitAnyKnot && Pointer.Pressed && ControlAppearance::QueryPointerOver(Plot, Pointer) && Profile.Knots.size() < 8u)
    {
        const float NewT = std::clamp((Pointer.X - (Plot.X + 4.0f)) / std::max(1.0f, Plot.Width - 8.0f), 0.0f, 1.0f);
        const float NewA = std::clamp(((Plot.Bottom() - 4.0f) - Pointer.Y) / std::max(1.0f, Plot.Height - 8.0f), 0.0f, 1.0f);
        Profile.Knots.push_back({ NewT, NewA });
        Profile.NormalizeKnots();
        Modified = true;
    }

    if (!Pointer.Down)
    {
        Session.DraggingKnot = false;
    }
    if (Session.DraggingKnot && Session.SelectedKnotIndex < Profile.Knots.size())
    {
        const float NewT = std::clamp((Pointer.X - (Plot.X + 4.0f)) / std::max(1.0f, Plot.Width - 8.0f), 0.0f, 1.0f);
        const float NewA = std::clamp(((Plot.Bottom() - 4.0f) - Pointer.Y) / std::max(1.0f, Plot.Height - 8.0f), 0.0f, 1.0f);
        if (Session.SelectedKnotIndex > 0u && Session.SelectedKnotIndex + 1u < Profile.Knots.size())
        {
            Profile.Knots[Session.SelectedKnotIndex].PositionT = NewT;
        }
        Profile.Knots[Session.SelectedKnotIndex].Amplitude = NewA;
        Modified = true;
    }

    return Modified;
}

} // namespace Frontier

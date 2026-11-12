/*============================================================================================================================================
                                                              CONTROLTOOLTIP.CPP
============================================================================================================================================*/
// 🧩 Directional, light/dark, and split-header diagnostic tooltip implementation

#include "Engine/Components/Controls/ControlTooltip.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr float PiConstant           = 3.14159265358979323846f; // [rad] - Mathematical pi constant
constexpr float HorizontalPadding    = 18.0f;                   // [px]  - Interior left/right content inset
constexpr float VerticalPadding      = 15.0f;                   // [px]  - Interior top/bottom content inset
constexpr float SplitHeaderHeight    = 40.0f;                   // [px]  - Upper header band height in SplitHeader layout
constexpr float TitleFontSize        = 14.0f;                   // [px]  - Tooltip title pixel height
constexpr float SplitTitleFontSize   = 13.0f;                   // [px]  - SplitHeader title pixel height
constexpr float BodyFontSize         = 12.5f;                   // [px]  - Wrapped body text pixel height
constexpr float BodyLineHeight       = 17.5f;                   // [px]  - Vertical stride per wrapped body line
constexpr float MetricRowHeight      = 22.0f;                   // [px]  - Height of icon/swatch metric row
constexpr float SuggestionBarHeight  = 32.0f;                   // [px]  - Interactive suggestion button height
constexpr float SuggestionButtonGap  = 6.0f;                    // [px]  - Horizontal gap between suggestion buttons
constexpr float FilletedTailHalfSpan = 14.5f;                   // [px]  - Half-width of smooth C1 speech pointer base
constexpr float FilletedTailLength   = 10.5f;                   // [px]  - Protrusion length of smooth C1 speech pointer
constexpr float RoundedTailHalfSpan  = 9.5f;                    // [px]  - Half-width of sleek rounded pointer base
constexpr float RoundedTailLength    = 10.0f;                   // [px]  - Protrusion length of sleek rounded pointer

//------------------------------------------------------------------------------------------------------------------------
//                                                    SURFACE COLORS
//------------------------------------------------------------------------------------------------------------------------

struct TooltipResolvedColors
{
    ColorRgba BodyFill          {}; // [-] - Main tooltip container fill
    ColorRgba HeaderFill        {}; // [-] - Upper SplitHeader band fill
    ColorRgba DividerStroke     {}; // [-] - SplitHeader horizontal separator rule
    ColorRgba PerimeterStroke   {}; // [-] - Subtle 1px outer hairline border
    ColorRgba TitleInk          {}; // [-] - Primary heading ink
    ColorRgba TagInk            {}; // [-] - Secondary header tag ink
    ColorRgba BodyInk           {}; // [-] - Wrapped body text ink
    ColorRgba NeutralButtonFill {}; // [-] - Neutral suggestion button background
    ColorRgba NeutralButtonHov  {}; // [-] - Hovered neutral suggestion button background
    ColorRgba SubtleButtonFill  {}; // [-] - Subtle dismiss button background
    ColorRgba SubtleButtonHov   {}; // [-] - Hovered subtle dismiss button background
    ColorRgba AccentInk         {}; // [-] - Resolved semantic accent color
    bool      LightMode         = false;
};

[[nodiscard]] TooltipResolvedColors ResolveTooltipColors(
    TooltipSurfaceMode Mode,
    TooltipSeverity    Severity,
    float              Opacity) noexcept
{
    const ControlColorSet& ThemeColors = ControlAppearance::Colors();
    const bool             UseLight    = (Mode == TooltipSurfaceMode::LightSurface) ||
                              (Mode == TooltipSurfaceMode::ThemeAutomatic && ThemeColors.LightSurface);

    const ColorQuad AccentQuad = ControlTooltip::ResolveSeverityAccent(Severity);
    const ColorRgba AccentRgba = AccentQuad.Faded(Opacity).ToRgba8();

    TooltipResolvedColors Out{};
    Out.LightMode = UseLight;
    Out.AccentInk = AccentRgba;

    if (UseLight)
    {
        Out.BodyFill          = ColorRgba{ 255u, 255u, 255u, 255u }.ScaledAlpha(Opacity);
        Out.HeaderFill        = ColorRgba{ 244u, 245u, 248u, 255u }.ScaledAlpha(Opacity);
        Out.DividerStroke     = ColorRgba{ 16u, 18u, 24u, 22u }.ScaledAlpha(Opacity);
        Out.PerimeterStroke   = ColorRgba{ 16u, 18u, 24u, 28u }.ScaledAlpha(Opacity);
        Out.TitleInk          = ColorRgba{ 18u, 19u, 23u, 255u }.ScaledAlpha(Opacity);
        Out.TagInk            = ColorRgba{ 108u, 110u, 120u, 255u }.ScaledAlpha(Opacity);
        Out.BodyInk           = ColorRgba{ 104u, 106u, 116u, 255u }.ScaledAlpha(Opacity);
        Out.NeutralButtonFill = ColorRgba{ 236u, 238u, 243u, 255u }.ScaledAlpha(Opacity);
        Out.NeutralButtonHov  = ColorRgba{ 224u, 227u, 235u, 255u }.ScaledAlpha(Opacity);
        Out.SubtleButtonFill  = ColorRgba{ 245u, 246u, 249u, 255u }.ScaledAlpha(Opacity);
        Out.SubtleButtonHov   = ColorRgba{ 234u, 236u, 242u, 255u }.ScaledAlpha(Opacity);
    }
    else
    {
        Out.BodyFill          = ColorRgba{ 17u, 18u, 22u, 255u }.ScaledAlpha(Opacity);
        Out.HeaderFill        = ColorRgba{ 32u, 33u, 38u, 255u }.ScaledAlpha(Opacity);
        Out.DividerStroke     = ColorRgba{ 255u, 255u, 255u, 18u }.ScaledAlpha(Opacity);
        Out.PerimeterStroke   = ColorRgba{ 255u, 255u, 255u, 26u }.ScaledAlpha(Opacity);
        Out.TitleInk          = ColorRgba{ 245u, 246u, 249u, 255u }.ScaledAlpha(Opacity);
        Out.TagInk            = ColorRgba{ 182u, 184u, 194u, 255u }.ScaledAlpha(Opacity);
        Out.BodyInk           = ColorRgba{ 156u, 159u, 170u, 255u }.ScaledAlpha(Opacity);
        Out.NeutralButtonFill = ColorRgba{ 43u, 48u, 56u, 255u }.ScaledAlpha(Opacity);
        Out.NeutralButtonHov  = ColorRgba{ 56u, 62u, 73u, 255u }.ScaledAlpha(Opacity);
        Out.SubtleButtonFill  = ColorRgba{ 29u, 31u, 37u, 255u }.ScaledAlpha(Opacity);
        Out.SubtleButtonHov   = ColorRgba{ 41u, 44u, 52u, 255u }.ScaledAlpha(Opacity);
    }

    if (Severity != TooltipSeverity::Neutral)
    {
        // 📝 Subtle semantic wash on SplitHeader and border when conveying diagnostic status.
        Out.HeaderFill      = ColorRgba::Blend(Out.HeaderFill, AccentRgba, UseLight ? 0.10f : 0.14f);
        Out.PerimeterStroke = ColorRgba::Blend(Out.PerimeterStroke, AccentRgba, 0.42f);
    }

    return Out;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TEXT WRAPPING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::string> WrapParagraphLines(
    PixelSpace&      Surface,
    float            FontSize,
    float            MaxLineWidth,
    std::string_view SourceText)
{
    std::vector<std::string> Lines;
    if (SourceText.empty() || MaxLineWidth <= 8.0f)
    {
        return Lines;
    }

    std::size_t Cursor = 0u;
    while (Cursor < SourceText.size())
    {
        const std::size_t NewlinePos = SourceText.find('\n', Cursor);
        const std::string_view Paragraph =
            (NewlinePos == std::string_view::npos)
                ? SourceText.substr(Cursor)
                : SourceText.substr(Cursor, NewlinePos - Cursor);

        std::string CurrentLine{};
        std::size_t WordCursor = 0u;
        while (WordCursor < Paragraph.size())
        {
            while (WordCursor < Paragraph.size() && Paragraph[WordCursor] == ' ')
            {
                ++WordCursor;
            }
            if (WordCursor >= Paragraph.size())
            {
                break;
            }

            std::size_t WordEnd = WordCursor;
            while (WordEnd < Paragraph.size() && Paragraph[WordEnd] != ' ')
            {
                ++WordEnd;
            }
            const std::string_view Word = Paragraph.substr(WordCursor, WordEnd - WordCursor);

            std::string Candidate = CurrentLine;
            if (!Candidate.empty())
            {
                Candidate.push_back(' ');
            }
            Candidate.append(Word.data(), Word.size());

            if (CurrentLine.empty() || Surface.MeasureTextWidth(FontSize, Candidate.c_str()) <= MaxLineWidth)
            {
                CurrentLine = std::move(Candidate);
            }
            else
            {
                Lines.push_back(std::move(CurrentLine));
                CurrentLine.assign(Word.data(), Word.size());
            }

            WordCursor = WordEnd;
        }

        if (!CurrentLine.empty())
        {
            Lines.push_back(std::move(CurrentLine));
        }

        if (NewlinePos == std::string_view::npos)
        {
            break;
        }
        Cursor = NewlinePos + 1u;
    }

    return Lines;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              UNIFIED CONTOUR GENERATION
//------------------------------------------------------------------------------------------------------------------------

void AppendDistinctVertex(std::vector<Point2D>& Vertices, Point2D Candidate)
{
    if (!Vertices.empty())
    {
        const Point2D Prev = Vertices.back();
        if (std::fabs(Prev.X - Candidate.X) + std::fabs(Prev.Y - Candidate.Y) < 0.08f)
        {
            return;
        }
    }
    Vertices.push_back(Candidate);
}

[[nodiscard]] float EvaluateTailDisplacement(float NormalizedCoord, TooltipTailProfile Profile, float TailLength) noexcept
{
    const float AbsU = std::clamp(std::fabs(NormalizedCoord), 0.0f, 1.0f);
    if (Profile == TooltipTailProfile::RoundedPointer)
    {
        // 📝 Tapered pointer with rounded apex and tiny base fillet (matches e6ee3086... reference).
        if (AbsU >= 0.88f)
        {
            const float Shoulder = (1.0f - AbsU) / 0.12f;
            return TailLength * 0.08f * Shoulder * Shoulder;
        }
        if (AbsU <= 0.18f)
        {
            const float ApexT = AbsU / 0.18f;
            return TailLength * (1.0f - 0.10f * ApexT * ApexT);
        }
        const float LinearT = (AbsU - 0.18f) / 0.70f;
        return TailLength * (0.90f * (1.0f - LinearT) + 0.08f * LinearT);
    }

    // 📝 Smooth C1 raised-cosine speech pointer (matches b705d179... and 8226e835... references).
    const float CosineBell = 0.5f * (1.0f + std::cos(PiConstant * AbsU));
    return TailLength * std::pow(CosineBell, 1.38f);
}

[[nodiscard]] float ResolveTailEdgeCoordinate(
    float                EdgeStart,
    float                EdgeEnd,
    float                CornerRadius,
    float                TailHalfSpan,
    float                TargetCoord,
    TooltipTailAlignment Alignment) noexcept
{
    const float MinCenter = EdgeStart + CornerRadius + TailHalfSpan + 2.0f;
    const float MaxCenter = EdgeEnd - CornerRadius - TailHalfSpan - 2.0f;
    if (MinCenter >= MaxCenter)
    {
        return (EdgeStart + EdgeEnd) * 0.5f;
    }

    float Desired = TargetCoord;
    switch (Alignment)
    {
        case TooltipTailAlignment::Leading:
            Desired = EdgeStart + (EdgeEnd - EdgeStart) * 0.20f;
            break;
        case TooltipTailAlignment::Centered:
            Desired = (EdgeStart + EdgeEnd) * 0.50f;
            break;
        case TooltipTailAlignment::Trailing:
            Desired = EdgeStart + (EdgeEnd - EdgeStart) * 0.80f;
            break;
        case TooltipTailAlignment::TargetAligned:
        default:
            break;
    }
    return std::clamp(Desired, MinCenter, MaxCenter);
}

[[nodiscard]] std::vector<Point2D> BuildTooltipContour(
    Rectangle2D          Body,
    float                Radius,
    TooltipDirection     Direction,
    TooltipTailProfile   TailProfile,
    TooltipTailAlignment TailAlignment,
    Point2D              TargetPoint,
    Rectangle2D&         OutEnvelope)
{
    const float MaxRadius    = std::max(0.0f, std::min(Body.Width, Body.Height) * 0.46f);
    const float SafeRadius   = std::clamp(Radius, 4.0f, MaxRadius);
    const float TailHalfSpan = (TailProfile == TooltipTailProfile::RoundedPointer) ? RoundedTailHalfSpan : FilletedTailHalfSpan;
    const float TailLength   = (TailProfile == TooltipTailProfile::RoundedPointer) ? RoundedTailLength : FilletedTailLength;

    constexpr int CornerSteps = 8;
    constexpr int TailSamples = 18;

    std::vector<Point2D> Contour;
    Contour.reserve(static_cast<std::size_t>((CornerSteps + 1) * 4 + TailSamples + 4));

    OutEnvelope = Body;

    auto AppendCornerArc = [&](Point2D Center, float StartAngle, float EndAngle)
    {
        for (int Step = 0; Step <= CornerSteps; ++Step)
        {
            const float Fraction = static_cast<float>(Step) / static_cast<float>(CornerSteps);
            const float Angle    = StartAngle + (EndAngle - StartAngle) * Fraction;
            AppendDistinctVertex(
                Contour,
                { Center.X + SafeRadius * std::cos(Angle), Center.Y + SafeRadius * std::sin(Angle) });
        }
    };

    // ① Top-Left corner (-pi -> -pi/2)
    AppendCornerArc({ Body.X + SafeRadius, Body.Y + SafeRadius }, -PiConstant, -0.5f * PiConstant);

    // ② Top edge (Left -> Right): carries upward tail when Direction == TooltipDirection::Below
    if (Direction == TooltipDirection::Below && Body.Width >= 2.0f * (SafeRadius + TailHalfSpan))
    {
        const float CenterX = ResolveTailEdgeCoordinate(
            Body.X, Body.Right(), SafeRadius, TailHalfSpan, TargetPoint.X, TailAlignment);
        for (int Step = 0; Step <= TailSamples; ++Step)
        {
            const float NormU = -1.0f + 2.0f * static_cast<float>(Step) / static_cast<float>(TailSamples);
            const float Disp  = EvaluateTailDisplacement(NormU, TailProfile, TailLength);
            AppendDistinctVertex(Contour, { CenterX + NormU * TailHalfSpan, Body.Y - Disp });
        }
        OutEnvelope.Y      -= TailLength;
        OutEnvelope.Height += TailLength;
    }

    // ③ Top-Right corner (-pi/2 -> 0)
    AppendCornerArc({ Body.Right() - SafeRadius, Body.Y + SafeRadius }, -0.5f * PiConstant, 0.0f);

    // ④ Right edge (Top -> Bottom): carries rightward tail when Direction == TooltipDirection::Left
    if (Direction == TooltipDirection::Left && Body.Height >= 2.0f * (SafeRadius + TailHalfSpan))
    {
        const float CenterY = ResolveTailEdgeCoordinate(
            Body.Y, Body.Bottom(), SafeRadius, TailHalfSpan, TargetPoint.Y, TailAlignment);
        for (int Step = 0; Step <= TailSamples; ++Step)
        {
            const float NormU = -1.0f + 2.0f * static_cast<float>(Step) / static_cast<float>(TailSamples);
            const float Disp  = EvaluateTailDisplacement(NormU, TailProfile, TailLength);
            AppendDistinctVertex(Contour, { Body.Right() + Disp, CenterY + NormU * TailHalfSpan });
        }
        OutEnvelope.Width += TailLength;
    }

    // ⑤ Bottom-Right corner (0 -> +pi/2)
    AppendCornerArc({ Body.Right() - SafeRadius, Body.Bottom() - SafeRadius }, 0.0f, 0.5f * PiConstant);

    // ⑥ Bottom edge (Right -> Left): carries downward tail when Direction == TooltipDirection::Above
    if (Direction == TooltipDirection::Above && Body.Width >= 2.0f * (SafeRadius + TailHalfSpan))
    {
        const float CenterX = ResolveTailEdgeCoordinate(
            Body.X, Body.Right(), SafeRadius, TailHalfSpan, TargetPoint.X, TailAlignment);
        for (int Step = 0; Step <= TailSamples; ++Step)
        {
            const float NormU = 1.0f - 2.0f * static_cast<float>(Step) / static_cast<float>(TailSamples);
            const float Disp  = EvaluateTailDisplacement(NormU, TailProfile, TailLength);
            AppendDistinctVertex(Contour, { CenterX + NormU * TailHalfSpan, Body.Bottom() + Disp });
        }
        OutEnvelope.Height += TailLength;
    }

    // ⑦ Bottom-Left corner (+pi/2 -> +pi)
    AppendCornerArc({ Body.X + SafeRadius, Body.Bottom() - SafeRadius }, 0.5f * PiConstant, PiConstant);

    // ⑧ Left edge (Bottom -> Top): carries leftward tail when Direction == TooltipDirection::Right
    if (Direction == TooltipDirection::Right && Body.Height >= 2.0f * (SafeRadius + TailHalfSpan))
    {
        const float CenterY = ResolveTailEdgeCoordinate(
            Body.Y, Body.Bottom(), SafeRadius, TailHalfSpan, TargetPoint.Y, TailAlignment);
        for (int Step = 0; Step <= TailSamples; ++Step)
        {
            const float NormU = 1.0f - 2.0f * static_cast<float>(Step) / static_cast<float>(TailSamples);
            const float Disp  = EvaluateTailDisplacement(NormU, TailProfile, TailLength);
            AppendDistinctVertex(Contour, { Body.X - Disp, CenterY + NormU * TailHalfSpan });
        }
        OutEnvelope.X     -= TailLength;
        OutEnvelope.Width += TailLength;
    }

    if (Contour.size() >= 2u)
    {
        const Point2D First = Contour.front();
        const Point2D Last  = Contour.back();
        if (std::fabs(First.X - Last.X) + std::fabs(First.Y - Last.Y) < 0.08f)
        {
            Contour.pop_back();
        }
    }

    return Contour;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 PUBLIC TOOLTIP API
//------------------------------------------------------------------------------------------------------------------------

namespace ControlTooltip
{

ControlIcon ResolveSeverityIcon(TooltipSeverity Severity, ControlIcon ExplicitIcon) noexcept
{
    if (ExplicitIcon != ControlIcon::Count)
    {
        return ExplicitIcon;
    }
    switch (Severity)
    {
        case TooltipSeverity::Informational:
            return ControlIcon::CircleNotice;
        case TooltipSeverity::Caution:
            return ControlIcon::OctagonAlert;
        case TooltipSeverity::Warning:
            return ControlIcon::TriangleAlert;
        case TooltipSeverity::Error:
            return ControlIcon::OctagonAlert;
        case TooltipSeverity::Success:
            return ControlIcon::CircleCheck;
        case TooltipSeverity::Neutral:
        default:
            return ControlIcon::Count;
    }
}

ColorQuad ResolveSeverityAccent(TooltipSeverity Severity) noexcept
{
    const ControlColorSet& Colors = ControlAppearance::Colors();
    switch (Severity)
    {
        case TooltipSeverity::Informational:
            return Colors.Notice;
        case TooltipSeverity::Caution:
            return Colors.Caution;
        case TooltipSeverity::Warning:
            return Colors.Warning;
        case TooltipSeverity::Error:
            return Colors.Danger;
        case TooltipSeverity::Success:
            return Colors.Success;
        case TooltipSeverity::Neutral:
        default:
            return Colors.Accent;
    }
}

Point2D MeasureBodyExtent(PixelSpace& Surface, const TooltipDescriptor& Descriptor) noexcept
{
    const bool HasTitle       = (Descriptor.Title != nullptr && Descriptor.Title[0] != '\0');
    const bool HasBody        = (Descriptor.BodyText != nullptr && Descriptor.BodyText[0] != '\0');
    const bool HasMetrics     = !Descriptor.MetricTokens.empty();
    const bool HasSuggestions = !Descriptor.Suggestions.empty();
    const bool SplitHeader    = (Descriptor.Layout == TooltipLayoutVariant::SplitHeader);

    float TargetWidth = std::clamp(Descriptor.PreferredWidth, 168.0f, 440.0f);

    if (HasSuggestions && Descriptor.Suggestions.size() > 1u)
    {
        float MinButtonsWidth = HorizontalPadding * 2.0f;
        for (std::size_t Index = 0u; Index < Descriptor.Suggestions.size(); ++Index)
        {
            const TooltipSuggestion& Suggestion = Descriptor.Suggestions[Index];
            const float IconSpace =
                (Suggestion.LeadingIcon != ControlIcon::Count ? 18.0f : 0.0f) +
                (Suggestion.TrailingIcon != ControlIcon::Count ? 18.0f : 0.0f);
            MinButtonsWidth += Surface.MeasureTextWidth(12.0f, Suggestion.Label) + IconSpace + 24.0f;
            if (Index + 1u < Descriptor.Suggestions.size())
            {
                MinButtonsWidth += SuggestionButtonGap;
            }
        }
        TargetWidth = std::max(TargetWidth, MinButtonsWidth);
    }

    const ControlIcon ResolvedIcon = ResolveSeverityIcon(Descriptor.Severity, Descriptor.LeadingIcon);
    const float       IconLead     = (!SplitHeader && ResolvedIcon != ControlIcon::Count) ? 24.0f : 0.0f;
    const float       WrapWidth    = std::max(80.0f, TargetWidth - HorizontalPadding * 2.0f - IconLead);

    const std::vector<std::string> BodyLines =
        HasBody ? WrapParagraphLines(Surface, BodyFontSize, WrapWidth, Descriptor.BodyText) : std::vector<std::string>{};

    // 📝 Shrink single-line simple tooltips cleanly so compact labels do not leave wide empty whitespace.
    if (!SplitHeader && !HasSuggestions && !HasMetrics && BodyLines.size() <= 1u)
    {
        const float MeasuredTitleW = HasTitle ? Surface.MeasureTextWidth(TitleFontSize, Descriptor.Title) : 0.0f;
        const float MeasuredBodyW  = BodyLines.empty() ? 0.0f : Surface.MeasureTextWidth(BodyFontSize, BodyLines[0].c_str());
        const float TightWidth     = std::max(MeasuredTitleW, MeasuredBodyW) + HorizontalPadding * 2.0f + IconLead + 6.0f;
        TargetWidth                = std::clamp(TightWidth, 148.0f, TargetWidth);
    }

    float Height = 0.0f;
    if (SplitHeader)
    {
        Height += SplitHeaderHeight + 1.0f;
        float LowerContentHeight = 0.0f;
        if (!BodyLines.empty())
        {
            LowerContentHeight += static_cast<float>(BodyLines.size()) * BodyLineHeight;
        }
        if (HasMetrics)
        {
            if (LowerContentHeight > 0.0f)
            {
                LowerContentHeight += 10.0f;
            }
            LowerContentHeight += MetricRowHeight;
        }
        if (HasSuggestions)
        {
            if (LowerContentHeight > 0.0f)
            {
                LowerContentHeight += 12.0f;
            }
            LowerContentHeight += SuggestionBarHeight;
        }
        Height += VerticalPadding * 2.0f + std::max(LowerContentHeight, 18.0f) - 4.0f;
    }
    else
    {
        Height += VerticalPadding * 2.0f;
        if (HasTitle)
        {
            Height += 20.0f;
            if (!BodyLines.empty())
            {
                Height += 5.0f;
            }
        }
        if (!BodyLines.empty())
        {
            Height += static_cast<float>(BodyLines.size()) * BodyLineHeight;
        }
        if (HasMetrics)
        {
            Height += 10.0f + MetricRowHeight;
        }
        if (HasSuggestions)
        {
            Height += 12.0f + SuggestionBarHeight;
        }
        Height = std::max(Height, 48.0f);
    }

    return { TargetWidth, Height };
}

TooltipDirection ResolvePlacementDirection(
    Rectangle2D      AnchorBounds,
    Rectangle2D      ViewportBounds,
    Point2D          BodyExtent,
    TooltipDirection Requested,
    float            TailLength,
    float            AnchorGap) noexcept
{
    if (Requested != TooltipDirection::Automatic)
    {
        return Requested;
    }

    const float NeededVertical   = BodyExtent.Y + TailLength + AnchorGap + 6.0f;
    const float NeededHorizontal = BodyExtent.X + TailLength + AnchorGap + 6.0f;

    const float RoomAbove = AnchorBounds.Y - ViewportBounds.Y;
    const float RoomBelow = ViewportBounds.Bottom() - AnchorBounds.Bottom();
    const float RoomRight = ViewportBounds.Right() - AnchorBounds.Right();
    const float RoomLeft  = AnchorBounds.X - ViewportBounds.X;

    if (RoomAbove >= NeededVertical)
    {
        return TooltipDirection::Above;
    }
    if (RoomBelow >= NeededVertical)
    {
        return TooltipDirection::Below;
    }
    if (RoomRight >= NeededHorizontal)
    {
        return TooltipDirection::Right;
    }
    if (RoomLeft >= NeededHorizontal)
    {
        return TooltipDirection::Left;
    }
    return (RoomBelow > RoomAbove) ? TooltipDirection::Below : TooltipDirection::Above;
}

TooltipInteractionResult ConstructAtBodyBounds(
    PixelSpace&              Surface,
    Rectangle2D              BodyBounds,
    Point2D                  TargetPoint,
    TooltipDirection         Direction,
    const TooltipDescriptor& Descriptor,
    const ControlPointer&    Pointer,
    float                    Opacity) noexcept
{
    TooltipInteractionResult Result{};
    Result.BodyBounds        = BodyBounds;
    Result.ResolvedDirection = (Direction == TooltipDirection::Automatic) ? TooltipDirection::Above : Direction;

    const TooltipResolvedColors Colors = ResolveTooltipColors(Descriptor.SurfaceMode, Descriptor.Severity, Opacity);

    Rectangle2D                Envelope{};
    const std::vector<Point2D> Contour = BuildTooltipContour(
        BodyBounds,
        Descriptor.CornerRadius,
        Result.ResolvedDirection,
        Descriptor.TailProfile,
        Descriptor.TailAlignment,
        TargetPoint,
        Envelope);
    Result.EnvelopeBounds = Envelope;
    Result.BodyHovered    = ControlAppearance::QueryPointerOver(BodyBounds, Pointer);

    if (Contour.size() >= 3u)
    {
        Surface.FillPolygon(Contour, Colors.BodyFill);

        if (Descriptor.Layout == TooltipLayoutVariant::SplitHeader)
        {
            const float DividerY = BodyBounds.Y + SplitHeaderHeight;
            Surface.PushClip(
                {
                    Envelope.X - 2.0f,
                    Envelope.Y - 2.0f,
                    Envelope.Width + 4.0f,
                    std::max(0.0f, DividerY - (Envelope.Y - 2.0f)),
                },
                true);
            Surface.FillPolygon(Contour, Colors.HeaderFill);
            Surface.PopClip();

            Surface.StrokeLine(
                { BodyBounds.X + 1.0f, DividerY },
                { BodyBounds.Right() - 1.0f, DividerY },
                Colors.DividerStroke,
                1.0f);
        }

        Surface.StrokePolyline(Contour, Colors.PerimeterStroke, 1.0f, true);
    }

    const ControlIcon ResolvedIcon = ResolveSeverityIcon(Descriptor.Severity, Descriptor.LeadingIcon);
    const bool        HasTitle     = (Descriptor.Title != nullptr && Descriptor.Title[0] != '\0');
    const bool        HasTag       = (Descriptor.HeaderTag != nullptr && Descriptor.HeaderTag[0] != '\0');
    const bool        HasBody      = (Descriptor.BodyText != nullptr && Descriptor.BodyText[0] != '\0');

    float ContentLeft  = BodyBounds.X + HorizontalPadding;
    float ContentRight = BodyBounds.Right() - HorizontalPadding;
    float CursorY      = BodyBounds.Y + VerticalPadding;

    if (Descriptor.Layout == TooltipLayoutVariant::SplitHeader)
    {
        float HeaderX = BodyBounds.X + HorizontalPadding;
        const float HeaderMidY = BodyBounds.Y + SplitHeaderHeight * 0.5f;

        if (ResolvedIcon != ControlIcon::Count)
        {
            ColorRgba TokenFill = Colors.AccentInk.WithAlpha(static_cast<uint8_t>(Colors.LightMode ? 32u : 42u));
            Surface.FillCircle({ HeaderX + 10.0f, HeaderMidY }, 11.0f, TokenFill);
            VectorIconRepository::ConstructControl(
                Surface,
                ResolvedIcon,
                { HeaderX + 3.0f, HeaderMidY - 7.0f, 14.0f, Colors.AccentInk, 2.0f });
            HeaderX += 28.0f;
        }

        if (HasTitle)
        {
            const Point2D TitleSize = Surface.MeasureText(SplitTitleFontSize, Descriptor.Title);
            Surface.Text(
                { HeaderX, HeaderMidY - TitleSize.Y * 0.5f },
                SplitTitleFontSize,
                Colors.TitleInk,
                Descriptor.Title);
            HeaderX += TitleSize.X + 10.0f;
        }

        if (HasTag)
        {
            Surface.FillCircle({ HeaderX, HeaderMidY }, 2.2f, Colors.TitleInk);
            HeaderX += 10.0f;
            const Point2D TagSize = Surface.MeasureText(12.0f, Descriptor.HeaderTag);
            Surface.Text(
                { HeaderX, HeaderMidY - TagSize.Y * 0.5f },
                12.0f,
                Colors.TagInk,
                Descriptor.HeaderTag);
        }

        CursorY = BodyBounds.Y + SplitHeaderHeight + 1.0f + VerticalPadding - 2.0f;
    }
    else
    {
        if (ResolvedIcon != ControlIcon::Count)
        {
            const ColorRgba IconTint =
                (Descriptor.Severity == TooltipSeverity::Neutral) ? Colors.TitleInk : Colors.AccentInk;
            VectorIconRepository::ConstructControl(
                Surface,
                ResolvedIcon,
                { ContentLeft, CursorY + 1.0f, 16.0f, IconTint, 2.0f });
            ContentLeft += 24.0f;
        }

        if (HasTitle)
        {
            const Point2D TitleSize = Surface.MeasureText(TitleFontSize, Descriptor.Title);
            Surface.Text(
                { ContentLeft, CursorY + (18.0f - TitleSize.Y) * 0.5f },
                TitleFontSize,
                Colors.TitleInk,
                Descriptor.Title);
            CursorY += 20.0f;
            if (HasBody)
            {
                CursorY += 5.0f;
            }
        }
    }

    if (HasBody)
    {
        const float                    WrapWidth = std::max(60.0f, ContentRight - ContentLeft);
        const std::vector<std::string> Lines     = WrapParagraphLines(Surface, BodyFontSize, WrapWidth, Descriptor.BodyText);
        for (const std::string& Line : Lines)
        {
            const Point2D LineExtent = Surface.MeasureText(BodyFontSize, Line.c_str());
            Surface.Text(
                { ContentLeft, CursorY + (BodyLineHeight - LineExtent.Y) * 0.5f },
                BodyFontSize,
                Colors.BodyInk,
                Line.c_str());
            CursorY += BodyLineHeight;
        }
    }

    if (!Descriptor.MetricTokens.empty())
    {
        if (HasBody || (Descriptor.Layout != TooltipLayoutVariant::SplitHeader && HasTitle))
        {
            CursorY += 10.0f;
        }

        float TokenX = BodyBounds.X + HorizontalPadding;
        const float TokenMidY = CursorY + MetricRowHeight * 0.5f;
        for (const TooltipMetricToken& Metric : Descriptor.MetricTokens)
        {
            if (Metric.SwatchEnabled)
            {
                Surface.FillCircle({ TokenX + 9.0f, TokenMidY }, 8.5f, Metric.SwatchColor.ScaledAlpha(Opacity));
                TokenX += 24.0f;
            }
            else if (Metric.Icon != ControlIcon::Count)
            {
                const ColorRgba DiscFill = Colors.LightMode
                                               ? ColorRgba{ 230u, 232u, 238u, 255u }.ScaledAlpha(Opacity)
                                               : ColorRgba{ 242u, 243u, 247u, 255u }.ScaledAlpha(Opacity);
                const ColorRgba DiscIcon = ColorRgba{ 22u, 24u, 30u, 255u }.ScaledAlpha(Opacity);
                Surface.FillCircle({ TokenX + 9.0f, TokenMidY }, 9.0f, DiscFill);
                VectorIconRepository::ConstructControl(
                    Surface,
                    Metric.Icon,
                    { TokenX + 3.5f, TokenMidY - 5.5f, 11.0f, DiscIcon, 2.0f });
                TokenX += 24.0f;
            }

            if (Metric.Caption != nullptr && Metric.Caption[0] != '\0')
            {
                const Point2D CapSize = Surface.MeasureText(12.0f, Metric.Caption);
                const ColorRgba CapInk = Metric.SwatchEnabled
                                             ? Metric.SwatchColor.ScaledAlpha(Opacity)
                                             : Colors.TitleInk;
                Surface.Text({ TokenX, TokenMidY - CapSize.Y * 0.5f }, 12.0f, CapInk, Metric.Caption);
                TokenX += CapSize.X + 18.0f;
            }
        }
        CursorY += MetricRowHeight;
    }

    if (!Descriptor.Suggestions.empty())
    {
        if (HasBody || !Descriptor.MetricTokens.empty() ||
            (Descriptor.Layout != TooltipLayoutVariant::SplitHeader && HasTitle))
        {
            CursorY += 12.0f;
        }

        const float       BarLeft  = BodyBounds.X + HorizontalPadding;
        const float       BarWidth = std::max(40.0f, BodyBounds.Width - HorizontalPadding * 2.0f);
        const std::size_t Count    = Descriptor.Suggestions.size();
        const float       CellWidth =
            (BarWidth - SuggestionButtonGap * static_cast<float>(Count > 0u ? Count - 1u : 0u)) /
            static_cast<float>(Count);

        for (std::size_t Index = 0u; Index < Count; ++Index)
        {
            const TooltipSuggestion& Suggestion = Descriptor.Suggestions[Index];
            const Rectangle2D        ButtonRect{
                BarLeft + static_cast<float>(Index) * (CellWidth + SuggestionButtonGap),
                CursorY,
                CellWidth,
                SuggestionBarHeight,
            };

            const bool Hovered = ControlAppearance::QueryPointerOver(ButtonRect, Pointer);
            if (Hovered)
            {
                Result.HoveredSuggestionIndex = static_cast<int32_t>(Index);
                if (Pointer.Pressed)
                {
                    Result.PressedSuggestionIndex = static_cast<int32_t>(Index);
                }
                if (Pointer.Released)
                {
                    Result.ClickedSuggestionIndex = static_cast<int32_t>(Index);
                }
            }

            ColorRgba BtnFill   = Hovered ? Colors.NeutralButtonHov : Colors.NeutralButtonFill;
            ColorRgba BtnInk    = Colors.TitleInk;
            ColorRgba BtnBorder = ColorRgba{ 0u, 0u, 0u, 0u };

            if (Suggestion.Tone == TooltipSuggestionTone::SemanticPrimary)
            {
                if (Descriptor.Severity != TooltipSeverity::Neutral)
                {
                    BtnFill = Hovered
                                  ? ColorRgba::Blend(Colors.AccentInk, ColorRgba{ 255u, 255u, 255u, 255u }, 0.14f)
                                  : Colors.AccentInk;
                    BtnInk  = (Colors.AccentInk.Luminance() > 0.62f)
                                  ? ColorRgba{ 16u, 18u, 22u, 255u }.ScaledAlpha(Opacity)
                                  : ColorRgba{ 255u, 255u, 255u, 255u }.ScaledAlpha(Opacity);
                }
                else
                {
                    BtnFill = Colors.LightMode
                                  ? ColorRgba{ 22u, 24u, 30u, 255u }.ScaledAlpha(Opacity)
                                  : ColorRgba{ 242u, 244u, 248u, 255u }.ScaledAlpha(Opacity);
                    BtnInk  = Colors.LightMode
                                  ? ColorRgba{ 255u, 255u, 255u, 255u }.ScaledAlpha(Opacity)
                                  : ColorRgba{ 18u, 20u, 24u, 255u }.ScaledAlpha(Opacity);
                }
            }
            else if (Suggestion.Tone == TooltipSuggestionTone::SubtleDismiss)
            {
                BtnFill   = Hovered ? Colors.SubtleButtonHov : Colors.SubtleButtonFill;
                BtnInk    = Hovered ? Colors.TitleInk : Colors.BodyInk;
                BtnBorder = Colors.DividerStroke;
            }

            Surface.FillRectangle(ButtonRect, BtnFill, 8.0f);
            if (BtnBorder.Alpha > 0u)
            {
                Surface.StrokeRectangle(ButtonRect, BtnBorder, 8.0f, 1.0f);
            }

            const float LabelFontSize = (Count == 1u) ? 13.0f : 12.0f;
            const Point2D LabelExtent = Surface.MeasureText(LabelFontSize, Suggestion.Label);

            if (Count == 1u && Suggestion.TrailingIcon != ControlIcon::Count && Suggestion.LeadingIcon == ControlIcon::Count)
            {
                // 📝 Full-width action bar with centered caption and right-pinned launch icon (matches b705d179...).
                Surface.Text(
                    {
                        ButtonRect.X + (ButtonRect.Width - LabelExtent.X) * 0.5f,
                        ButtonRect.Y + (ButtonRect.Height - LabelExtent.Y) * 0.5f,
                    },
                    LabelFontSize,
                    BtnInk,
                    Suggestion.Label);
                VectorIconRepository::ConstructControl(
                    Surface,
                    Suggestion.TrailingIcon,
                    {
                        ButtonRect.Right() - 24.0f,
                        ButtonRect.Y + (ButtonRect.Height - 14.0f) * 0.5f,
                        14.0f,
                        BtnInk.WithAlpha(static_cast<uint8_t>(BtnInk.Alpha * 3u / 4u)),
                        1.8f,
                    });
            }
            else
            {
                const float LeadW   = (Suggestion.LeadingIcon != ControlIcon::Count) ? 17.0f : 0.0f;
                const float TrailW  = (Suggestion.TrailingIcon != ControlIcon::Count) ? 17.0f : 0.0f;
                const float GroupW  = LeadW + LabelExtent.X + TrailW;
                float       CursorX = ButtonRect.X + (ButtonRect.Width - GroupW) * 0.5f;

                if (Suggestion.LeadingIcon != ControlIcon::Count)
                {
                    VectorIconRepository::ConstructControl(
                        Surface,
                        Suggestion.LeadingIcon,
                        { CursorX, ButtonRect.Y + (ButtonRect.Height - 13.0f) * 0.5f, 13.0f, BtnInk, 1.9f });
                    CursorX += LeadW;
                }

                Surface.Text(
                    { CursorX, ButtonRect.Y + (ButtonRect.Height - LabelExtent.Y) * 0.5f },
                    LabelFontSize,
                    BtnInk,
                    Suggestion.Label);
                CursorX += LabelExtent.X + 4.0f;

                if (Suggestion.TrailingIcon != ControlIcon::Count)
                {
                    VectorIconRepository::ConstructControl(
                        Surface,
                        Suggestion.TrailingIcon,
                        { CursorX, ButtonRect.Y + (ButtonRect.Height - 13.0f) * 0.5f, 13.0f, BtnInk, 1.9f });
                }
            }
        }
    }

    return Result;
}

TooltipInteractionResult ConstructAnchored(
    PixelSpace&              Surface,
    Rectangle2D              AnchorBounds,
    Rectangle2D              ViewportBounds,
    const TooltipDescriptor& Descriptor,
    const ControlPointer&    Pointer,
    float                    Opacity) noexcept
{
    const Point2D BodyExtent = MeasureBodyExtent(Surface, Descriptor);
    const float   TailLen    = (Descriptor.TailProfile == TooltipTailProfile::RoundedPointer)
                                   ? RoundedTailLength
                                   : FilletedTailLength;

    const TooltipDirection Direction = ResolvePlacementDirection(
        AnchorBounds,
        ViewportBounds,
        BodyExtent,
        Descriptor.Direction,
        TailLen,
        Descriptor.AnchorGap);

    const Point2D AnchorCenter = AnchorBounds.Center();
    const float   Margin       = 8.0f;
    Rectangle2D   BodyBounds{ 0.0f, 0.0f, BodyExtent.X, BodyExtent.Y };

    switch (Direction)
    {
        case TooltipDirection::Above:
            BodyBounds.X = AnchorCenter.X - BodyExtent.X * 0.5f;
            BodyBounds.Y = AnchorBounds.Y - Descriptor.AnchorGap - TailLen - BodyExtent.Y;
            break;
        case TooltipDirection::Below:
            BodyBounds.X = AnchorCenter.X - BodyExtent.X * 0.5f;
            BodyBounds.Y = AnchorBounds.Bottom() + Descriptor.AnchorGap + TailLen;
            break;
        case TooltipDirection::Left:
            BodyBounds.X = AnchorBounds.X - Descriptor.AnchorGap - TailLen - BodyExtent.X;
            BodyBounds.Y = AnchorCenter.Y - BodyExtent.Y * 0.5f;
            break;
        case TooltipDirection::Right:
        case TooltipDirection::Automatic:
        default:
            BodyBounds.X = AnchorBounds.Right() + Descriptor.AnchorGap + TailLen;
            BodyBounds.Y = AnchorCenter.Y - BodyExtent.Y * 0.5f;
            break;
    }

    if (ViewportBounds.Width > BodyExtent.X + Margin * 2.0f)
    {
        BodyBounds.X = std::clamp(
            BodyBounds.X,
            ViewportBounds.X + Margin,
            ViewportBounds.Right() - BodyExtent.X - Margin);
    }
    if (ViewportBounds.Height > BodyExtent.Y + Margin * 2.0f)
    {
        BodyBounds.Y = std::clamp(
            BodyBounds.Y,
            ViewportBounds.Y + Margin,
            ViewportBounds.Bottom() - BodyExtent.Y - Margin);
    }

    return ConstructAtBodyBounds(Surface, BodyBounds, AnchorCenter, Direction, Descriptor, Pointer, Opacity);
}

bool PresentHoveredControlTooltip(
    PixelSpace&           Surface,
    Rectangle2D           ViewportBounds,
    const ControlPointer& Pointer,
    float                 Opacity) noexcept
{
    Rectangle2D     AnchorBounds{};
    OptionalTooltip Hovered{};
    if (!ControlAppearance::ConsumeHoveredTooltip(AnchorBounds, Hovered))
    {
        return false;
    }

    TooltipDescriptor Descriptor{};
    Descriptor.Title         = Hovered.Title;
    Descriptor.HeaderTag     = Hovered.HeaderTag;
    Descriptor.BodyText      = Hovered.Text;
    Descriptor.Direction     = Hovered.Direction;
    Descriptor.Severity      = Hovered.Severity;
    Descriptor.LeadingIcon   = Hovered.LeadingIcon;
    Descriptor.Layout        = (Hovered.HeaderTag != nullptr && Hovered.HeaderTag[0] != '\0')
                                   ? TooltipLayoutVariant::SplitHeader
                                   : TooltipLayoutVariant::Standard;
    Descriptor.PreferredWidth = 264.0f;

    (void)ConstructAnchored(Surface, AnchorBounds, ViewportBounds, Descriptor, Pointer, Opacity);
    return true;
}

} // namespace ControlTooltip

} // namespace Frontier

/*====================================================================================================================================
                                                       DRAWINGSHEET.CPP
====================================================================================================================================*/
// 🧩 Drawing sheets: paper, first / third angle view layout, standard scales, automatic dimensions, section line, title block

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Drawing/DrawingSheet.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double Pi             = std::numbers::pi;
    constexpr double ArrowLength    = 3.0;                                                   // [mm] - ISO 129: filled, 1 : 3
    constexpr double ArrowHalfWidth = 0.5;                                                   // [mm]
    constexpr double FirstTier      = 10.0;                                                  // [mm] - Outline → first dimension line
    constexpr double TierSpacing    = 8.0;                                                   // [mm] - Between dimension lines
    constexpr double ExtensionGap   = 1.0;                                                   // [mm] - Feature → extension line
    constexpr double ExtensionReach = 2.0;                                                   // [mm] - Extension past the dimension line
    constexpr double TitleWidth     = 180.0;                                                 // [mm] - ISO 7200
    constexpr double TitleHeight    = 36.0;                                                  // [mm]

    enum class ViewSlot : uint8_t { Front = 0, Top = 1, Right = 2, Fourth = 3 };

    // Glyph count of a UTF-8 string (continuation bytes skipped); monospace advance 0.6·h.
    [[nodiscard]] double TextWidth(const std::string& Content, double Height)
    {
        size_t Glyphs = 0;
        for (const unsigned char Byte : Content) if ((Byte & 0xC0) != 0x80) ++Glyphs;
        return 0.6 * Height * static_cast<double>(Glyphs);
    }

    // Everything one view carries, in view-local millimetres (sheet = Origin + local).
    struct ViewAnnotation
    {
        std::vector<SheetPolyline>    Lines      = {};
        std::vector<SheetText>        Texts      = {};
        std::vector<SheetSolid>       Solids     = {};
        std::vector<DrawingDimension> Dimensions = {};
        Vector2                       Low        = {};                                       // [mm] - Footprint
        Vector2                       High       = {};
    };

    void Grow(Vector2& Low, Vector2& High, Vector2 Point)
    {
        Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y) };
        High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y) };
    }

    void GrowText(Vector2& Low, Vector2& High, const SheetText& Text, Vector2& TextLow, Vector2& TextHigh)
    {
        const double Width = TextWidth(Text.Content, Text.Height);
        const double Shift = Text.Anchor == SheetTextAnchor::Start ? 0.0 : Text.Anchor == SheetTextAnchor::Middle ? -0.5 * Width : -Width;
        const Vector2 Along = { std::cos(Text.Angle), std::sin(Text.Angle) }, Up = { -Along.Y, Along.X };
        TextLow  = { 1.0e300, 1.0e300 };
        TextHigh = { -1.0e300, -1.0e300 };
        for (const double A : { Shift, Shift + Width })
            for (const double B : { 0.0, Text.Height })
                Grow(TextLow, TextHigh, { Text.Position.X + Along.X * A + Up.X * B, Text.Position.Y + Along.Y * A + Up.Y * B });
        Grow(Low, High, TextLow);
        Grow(Low, High, TextHigh);
    }

    [[nodiscard]] SheetSolid Arrowhead(Vector2 Tip, Vector2 Direction)                       // Direction: base → tip (unit)
    {
        const Vector2 Base = { Tip.X - Direction.X * ArrowLength, Tip.Y - Direction.Y * ArrowLength };
        const Vector2 Side = { -Direction.Y * ArrowHalfWidth, Direction.X * ArrowHalfWidth };
        return { SheetLayer::Dimension, { Tip, Vector2{ Base.X + Side.X, Base.Y + Side.Y }, Vector2{ Base.X - Side.X, Base.Y - Side.Y } } };
    }

    // A circle or arc read off one stroke: circumcircle of its first, middle and last points, accepted when every point
    // lies on it within 10⁻⁹·max(1, r).
    struct CircleFit
    {
        Vector2 Centre = {};
        double  Radius = 0.0;
        double  Cover  = 0.0;                                                                // [rad] - Σ |Δθ| over the strokes
        double  Middle = 0.0;                                                                // [rad] - Angle of the longest stroke's middle point
        double  Longest = 0.0;
    };

    [[nodiscard]] std::optional<CircleFit> FitCircle(const std::vector<Vector2>& Points)
    {
        if (Points.size() < 3) return {};
        const Vector2 A = Points.front(), B = Points[Points.size() / 3], C = Points[2 * Points.size() / 3];   // A closed stroke ends where it starts
        const double  Determinant = 2.0 * (A.X * (B.Y - C.Y) + B.X * (C.Y - A.Y) + C.X * (A.Y - B.Y));
        const double  Span = std::max({ std::hypot(B.X - A.X, B.Y - A.Y), std::hypot(C.X - B.X, C.Y - B.Y), 1.0e-300 });
        if (std::fabs(Determinant) <= 1.0e-12 * Span * Span) return {};                       // Collinear
        const double SquareA = A.X * A.X + A.Y * A.Y, SquareB = B.X * B.X + B.Y * B.Y, SquareC = C.X * C.X + C.Y * C.Y;
        CircleFit Fit;
        Fit.Centre = { (SquareA * (B.Y - C.Y) + SquareB * (C.Y - A.Y) + SquareC * (A.Y - B.Y)) / Determinant,
                       (SquareA * (C.X - B.X) + SquareB * (A.X - C.X) + SquareC * (B.X - A.X)) / Determinant };
        Fit.Radius = std::hypot(A.X - Fit.Centre.X, A.Y - Fit.Centre.Y);
        for (const Vector2& Point : Points)
            if (std::fabs(std::hypot(Point.X - Fit.Centre.X, Point.Y - Fit.Centre.Y) - Fit.Radius) > 1.0e-9 * std::max(1.0, Fit.Radius)) return {};
        for (size_t Index = 1; Index < Points.size(); ++Index)
        {
            double Turn = std::atan2(Points[Index].Y - Fit.Centre.Y, Points[Index].X - Fit.Centre.X) - std::atan2(Points[Index - 1].Y - Fit.Centre.Y, Points[Index - 1].X - Fit.Centre.X);
            while (Turn > Pi) Turn -= 2.0 * Pi;
            while (Turn < -Pi) Turn += 2.0 * Pi;
            Fit.Cover += std::fabs(Turn);
        }
        const Vector2 Half = Points[Points.size() / 2];
        Fit.Middle  = std::atan2(Half.Y - Fit.Centre.Y, Half.X - Fit.Centre.X);
        Fit.Longest = Fit.Cover;
        return Fit;
    }

    // Visible circles and arcs of a view, merged by centre and radius.
    [[nodiscard]] std::vector<CircleFit> CollectCircles(const DrawingView& View)
    {
        std::vector<CircleFit> Circles;
        const double Precision = 1.0e-9 * std::max(1.0, View.Reach);
        for (const DrawingStroke& Stroke : View.Strokes)
        {
            if (Stroke.Visibility != DrawingVisibility::Visible || Stroke.Kind == DrawingStrokeCategory::Hatch) continue;
            const std::optional<CircleFit> Fit = FitCircle(Stroke.Points);
            if (!Fit) continue;
            bool Merged = false;
            for (CircleFit& Known : Circles)
            {
                if (std::hypot(Known.Centre.X - Fit->Centre.X, Known.Centre.Y - Fit->Centre.Y) > Precision || std::fabs(Known.Radius - Fit->Radius) > Precision) continue;
                Known.Cover += Fit->Cover;
                if (Fit->Longest > Known.Longest) { Known.Longest = Fit->Longest; Known.Middle = Fit->Middle; }
                Merged = true;
                break;
            }
            if (!Merged) Circles.push_back(*Fit);
        }
        return Circles;
    }

    // Coaxial strokes (two rims of one hole) each add 2π; a full circle is any group with cover ≥ 2π.
    [[nodiscard]] bool FullCondition(const CircleFit& Circle) { return Circle.Cover >= 2.0 * Pi - 1.0e-6; }

    // Extreme outline point along an axis (0 = 𝑥, 1 = 𝑦); ties broken toward the dimension side on the other axis.
    [[nodiscard]] Vector2 ExtremePoint(const DrawingView& View, int Axis, bool Least, int TieSide)
    {
        const double Precision = 1.0e-12 * std::max(1.0, View.Reach);
        Vector2 Best = {};
        bool    Found = false;
        for (const DrawingStroke& Stroke : View.Strokes)
        {
            if (Stroke.Kind == DrawingStrokeCategory::Hatch) continue;
            for (const Vector2& Point : Stroke.Points)
            {
                const double Here = Axis == 0 ? Point.X : Point.Y, There = Axis == 0 ? Best.X : Best.Y;
                const double Other = Axis == 0 ? Point.Y : Point.X, OtherBest = Axis == 0 ? Best.Y : Best.X;
                const bool Better = !Found || (Least ? Here < There - Precision : Here > There + Precision) || (std::fabs(Here - There) <= Precision && (TieSide < 0 ? Other < OtherBest : Other > OtherBest));
                if (Better) { Best = Point; Found = true; }
            }
        }
        return Best;
    }

    struct LinearPlan
    {
        bool    VerticalCondition = false;                                                   // [-]   - Horizontal or vertical
        Vector2 First             = {};                                                      // [len] - Model view coordinates
        Vector2 Second            = {};
        double  Value             = 0.0;                                                     // [len]
    };

    // 📝 Sides: third angle — Front below / left, Top above / left, Right below / right; first angle mirrors them.
    void Sides(ViewSlot Slot, DrawingProjectionAngle Angle, int& HorizontalSide, int& VerticalSide)
    {
        const bool Third = Angle == DrawingProjectionAngle::Third;
        switch (Slot)
        {
        case ViewSlot::Front: HorizontalSide = Third ? -1 : 1; VerticalSide = Third ? -1 : 1; break;
        case ViewSlot::Top:   HorizontalSide = Third ? 1 : -1; VerticalSide = Third ? -1 : 1; break;
        default:              HorizontalSide = -1;             VerticalSide = Third ? 1 : -1; break;
        }
    }

    [[nodiscard]] ViewAnnotation AnnotateView(const PlacedDrawingView& Placed, ViewSlot Slot, double Millimetres, const DrawingSheetRequest& Request,
                                              const std::optional<std::pair<Vector2, Vector2>>& CuttingLine, Vector2 Look, double Unit)
    {
        const DrawingView& View = Placed.View;
        ViewAnnotation Note;
        const Vector2 BoxLow = { Millimetres * View.Low.X, Millimetres * View.Low.Y }, BoxHigh = { Millimetres * View.High.X, Millimetres * View.High.Y };
        Note.Low = BoxLow;
        Note.High = BoxHigh;
        const double Height = Request.TextHeight;
        const auto Local = [&](Vector2 Point) { return Vector2{ Millimetres * Point.X, Millimetres * Point.Y }; };
        const auto Line = [&](SheetLayer Layer, std::vector<Vector2> Points)
        {
            for (const Vector2& Point : Points) Grow(Note.Low, Note.High, Point);
            Note.Lines.push_back({ Layer, std::move(Points) });
        };
        const auto Solid = [&](const SheetSolid& Shape)
        {
            for (const Vector2& Corner : Shape.Corners) Grow(Note.Low, Note.High, Corner);
            Note.Solids.push_back(Shape);
        };
        const auto Text = [&](SheetText Shape, Vector2* TextLow = nullptr, Vector2* TextHigh = nullptr)
        {
            Vector2 Low, High;
            GrowText(Note.Low, Note.High, Shape, Low, High);
            if (TextLow) { *TextLow = Low; *TextHigh = High; }
            Note.Texts.push_back(std::move(Shape));
        };

        if (Request.DimensionsEnabled && Slot != ViewSlot::Fourth)
        {
            int HorizontalSide = -1, VerticalSide = -1;
            Sides(Slot, Request.Angle, HorizontalSide, VerticalSide);
            const std::vector<CircleFit> Circles = CollectCircles(View);
            std::vector<LinearPlan> Plans;
            const auto Plan = [&](bool Vertical, Vector2 First, Vector2 Second)
            {
                const double Value = Vertical ? Second.Y - First.Y : Second.X - First.X;
                if (Value <= 1.0e-9 * std::max(1.0, View.Reach)) return;
                for (const LinearPlan& Known : Plans)
                    if (Known.VerticalCondition == Vertical && std::fabs(Known.Value - Value) <= 1.0e-12 * std::max(1.0, View.Reach) &&
                        std::fabs((Vertical ? Known.First.Y : Known.First.X) - (Vertical ? First.Y : First.X)) <= 1.0e-12 * std::max(1.0, View.Reach)) return;
                Plans.push_back({ Vertical, First, Second, Value });
            };
            const Vector2 Left   = ExtremePoint(View, 0, true, HorizontalSide);
            const Vector2 Right  = ExtremePoint(View, 0, false, HorizontalSide);
            const Vector2 Bottom = ExtremePoint(View, 1, true, VerticalSide);
            const Vector2 Upper  = ExtremePoint(View, 1, false, VerticalSide);
            if (Slot == ViewSlot::Front) { Plan(false, Left, Right); Plan(true, Bottom, Upper); }
            // A full circle spanning the outline along an axis (a cylinder seen end-on) is sized by its ⌀ alone: neither
            // the overall size nor its position is repeated along that axis.
            const double Tolerance = 1.0e-9 * std::max(1.0, View.Reach);
            const auto Spans = [&](const CircleFit& Circle, bool Vertical)
            {
                const double Centre = Vertical ? Circle.Centre.Y : Circle.Centre.X;
                const double Low = Vertical ? Bottom.Y : Left.X, High = Vertical ? Upper.Y : Right.X;
                // Nothing reaches past the circle on either side (sampled circle points lie inside it, so equality is not
                // the test): the circle, being part of the outline, then is its extent.
                return FullCondition(Circle) && Low >= Centre - Circle.Radius - Tolerance && High <= Centre + Circle.Radius + Tolerance;
            };
            bool SpannedVertically = false;
            for (const CircleFit& Circle : Circles) SpannedVertically = SpannedVertically || Spans(Circle, true);
            if (Slot == ViewSlot::Top)
            {
                if (!SpannedVertically) Plan(true, Bottom, Upper);
                for (const CircleFit& Circle : Circles)
                {
                    if (!FullCondition(Circle)) continue;
                    if (!Spans(Circle, false)) Plan(false, Left, Circle.Centre);
                    if (!Spans(Circle, true)) Plan(true, Bottom, Circle.Centre);
                }
            }
            // Linear dimensions: shorter ones nearer the outline, one tier each.
            for (const bool Vertical : { false, true })
            {
                std::vector<LinearPlan> Chosen;
                for (const LinearPlan& Candidate : Plans) if (Candidate.VerticalCondition == Vertical) Chosen.push_back(Candidate);
                std::stable_sort(Chosen.begin(), Chosen.end(), [](const LinearPlan& First, const LinearPlan& Second) { return First.Value < Second.Value; });
                for (size_t Tier = 0; Tier < Chosen.size(); ++Tier)
                {
                    const LinearPlan& Selected = Chosen[Tier];
                    DrawingDimension Dimension;
                    Dimension.Kind   = Vertical ? DrawingDimensionCategory::Vertical : DrawingDimensionCategory::Horizontal;
                    Dimension.Value  = Selected.Value;
                    Dimension.Text   = FormatDimensionValue(Selected.Value * Unit);
                    Dimension.First  = Local(Selected.First);
                    Dimension.Second = Local(Selected.Second);
                    const double Offset = FirstTier + TierSpacing * static_cast<double>(Tier);
                    const Vector2 A = Dimension.First, B = Dimension.Second;
                    if (!Vertical)
                    {
                        const int    Side  = HorizontalSide;
                        const double Level = Side < 0 ? BoxLow.Y - Offset : BoxHigh.Y + Offset;
                        for (const Vector2& Foot : { A, B })
                            Line(SheetLayer::Dimension, { { Foot.X, Foot.Y + Side * ExtensionGap }, { Foot.X, Level + Side * ExtensionReach } });
                        Dimension.LineStart = { A.X, Level };
                        Dimension.LineEnd   = { B.X, Level };
                        const bool Inside = B.X - A.X >= 2.0 * ArrowLength + 1.0;
                        Line(SheetLayer::Dimension, Inside ? std::vector<Vector2>{ Dimension.LineStart, Dimension.LineEnd } : std::vector<Vector2>{ { A.X - 2.0 * ArrowLength, Level }, { B.X + 2.0 * ArrowLength, Level } });
                        Solid(Arrowhead(Dimension.LineStart, { Inside ? -1.0 : 1.0, 0.0 }));
                        Solid(Arrowhead(Dimension.LineEnd, { Inside ? 1.0 : -1.0, 0.0 }));
                        Text({ SheetLayer::Dimension, { 0.5 * (A.X + B.X), Level + 1.0 }, Height, 0.0, SheetTextAnchor::Middle, Dimension.Text }, &Dimension.TextLow, &Dimension.TextHigh);
                    }
                    else
                    {
                        const int    Side  = VerticalSide;
                        const double Level = Side < 0 ? BoxLow.X - Offset : BoxHigh.X + Offset;
                        for (const Vector2& Foot : { A, B })
                            Line(SheetLayer::Dimension, { { Foot.X + Side * ExtensionGap, Foot.Y }, { Level + Side * ExtensionReach, Foot.Y } });
                        Dimension.LineStart = { Level, A.Y };
                        Dimension.LineEnd   = { Level, B.Y };
                        const bool Inside = B.Y - A.Y >= 2.0 * ArrowLength + 1.0;
                        Line(SheetLayer::Dimension, Inside ? std::vector<Vector2>{ Dimension.LineStart, Dimension.LineEnd } : std::vector<Vector2>{ { Level, A.Y - 2.0 * ArrowLength }, { Level, B.Y + 2.0 * ArrowLength } });
                        Solid(Arrowhead(Dimension.LineStart, { 0.0, Inside ? -1.0 : 1.0 }));
                        Solid(Arrowhead(Dimension.LineEnd, { 0.0, Inside ? 1.0 : -1.0 }));
                        Text({ SheetLayer::Dimension, { Level - 1.0, 0.5 * (A.Y + B.Y) }, Height, 0.5 * Pi, SheetTextAnchor::Middle, Dimension.Text }, &Dimension.TextLow, &Dimension.TextHigh);
                    }
                    Note.Dimensions.push_back(std::move(Dimension));
                }
            }
            // ⌀ / R leaders and centre marks.
            for (const CircleFit& Circle : Circles)
            {
                const double Radius = Millimetres * Circle.Radius;
                if (Radius < 1.0) continue;
                const bool    Full   = FullCondition(Circle);
                const Vector2 Centre = Local(Circle.Centre);
                Vector2 Out;
                if (Full)
                {
                    // Into the corner free of both dimension stacks: opposite the centre's own extension lines, so the
                    // leader crosses neither a dimension nor its value.
                    Out = { -VerticalSide * std::sqrt(0.5), -HorizontalSide * std::sqrt(0.5) };
                    const double Mark = Radius + 2.0;
                    Line(SheetLayer::Centre, { { Centre.X - Mark, Centre.Y }, { Centre.X + Mark, Centre.Y } });
                    Line(SheetLayer::Centre, { { Centre.X, Centre.Y - Mark }, { Centre.X, Centre.Y + Mark } });
                }
                else Out = { std::cos(Circle.Middle), std::sin(Circle.Middle) };
                DrawingDimension Dimension;
                Dimension.Kind   = Full ? DrawingDimensionCategory::Diameter : DrawingDimensionCategory::Radius;
                Dimension.Value  = Full ? 2.0 * Circle.Radius : Circle.Radius;
                Dimension.Text   = (Full ? std::string("⌀") : std::string("R")) + FormatDimensionValue(Dimension.Value * Unit);
                Dimension.First  = Centre;
                Dimension.Second = { Centre.X + Out.X * Radius, Centre.Y + Out.Y * Radius };
                // Leader: from the tip on the curve out past the outline box, then a shoulder carrying the value.
                double Exit = 1.0e300;
                for (int Axis = 0; Axis < 2; ++Axis)
                {
                    const double Direction = Axis == 0 ? Out.X : Out.Y, Start = Axis == 0 ? Dimension.Second.X : Dimension.Second.Y;
                    const double Bound = Direction > 0.0 ? (Axis == 0 ? BoxHigh.X : BoxHigh.Y) : (Axis == 0 ? BoxLow.X : BoxLow.Y);
                    if (std::fabs(Direction) > 1.0e-12) Exit = std::min(Exit, std::max(0.0, (Bound - Start) / Direction));
                }
                // The value sits on the shoulder (1 mm up, Height tall): the knee must clear the box either below/above by
                // the whole text or sideways, whichever is nearer.
                const double Sideways = Out.X > 0.0 ? (BoxHigh.X - Dimension.Second.X) / Out.X : (Dimension.Second.X - BoxLow.X) / -Out.X;
                const double Upright  = Out.Y > 0.0 ? (BoxHigh.Y - Dimension.Second.Y) / Out.Y
                                                    : (Dimension.Second.Y - BoxLow.Y + Height + 2.0) / -Out.Y;
                const double  Reach = std::max({ Exit + 4.0, 6.0, std::min(Sideways, Upright) });
                const Vector2 Knee  = { Dimension.Second.X + Out.X * Reach, Dimension.Second.Y + Out.Y * Reach };
                const double  Width = TextWidth(Dimension.Text, Height);
                const double  Shoulder = Out.X >= 0.0 ? Width + 2.0 : -(Width + 2.0);
                Dimension.LineStart = Dimension.Second;
                Dimension.LineEnd   = Knee;
                Line(SheetLayer::Dimension, { Dimension.Second, Knee, { Knee.X + Shoulder, Knee.Y } });
                Solid(Arrowhead(Dimension.Second, { -Out.X, -Out.Y }));
                Text({ SheetLayer::Dimension, { Knee.X + (Out.X >= 0.0 ? 1.0 : -1.0), Knee.Y + 1.0 }, Height, 0.0, Out.X >= 0.0 ? SheetTextAnchor::Start : SheetTextAnchor::End, Dimension.Text },
                     &Dimension.TextLow, &Dimension.TextHigh);
                Note.Dimensions.push_back(std::move(Dimension));
            }
        }

        // Section line: chain across the view, arrows in the viewing direction, letters A.
        if (CuttingLine)
        {
            const Vector2 Start = CuttingLine->first, End = CuttingLine->second;
            const Vector2 Along = { (End.X - Start.X) / std::hypot(End.X - Start.X, End.Y - Start.Y), (End.Y - Start.Y) / std::hypot(End.X - Start.X, End.Y - Start.Y) };
            Line(SheetLayer::CuttingPlane, { Start, End });
            for (const auto& [Point, Sign] : { std::pair{ Start, -1.0 }, std::pair{ End, 1.0 } })
            {
                const Vector2 Tip = { Point.X + Look.X * 7.0, Point.Y + Look.Y * 7.0 };
                Line(SheetLayer::CuttingPlane, { Point, Tip });
                Solid({ SheetLayer::CuttingPlane, Arrowhead(Tip, Look).Corners });
                const Vector2 Letter = { Tip.X + Look.X * 1.5 + Sign * Along.X * 1.0, Tip.Y + Look.Y * 1.5 + Sign * Along.Y * 1.0 - (Look.Y < 0.0 ? Height : 0.0) };
                Text({ SheetLayer::CuttingPlane, Letter, Height, 0.0, SheetTextAnchor::Middle, "A" });
            }
        }
        if (Slot == ViewSlot::Fourth)
            Text({ SheetLayer::Title, { 0.5 * (BoxLow.X + BoxHigh.X), BoxLow.Y - 8.0 }, Height, 0.0, SheetTextAnchor::Middle, Placed.Label == "Isometric" ? "ISOMETRIC" : "SECTION A-A" });
        return Note;
    }

    [[nodiscard]] std::vector<Vector2> CirclePolyline(Vector2 Centre, double Radius)
    {
        std::vector<Vector2> Points;
        for (int Index = 0; Index <= 72; ++Index)
        {
            const double Angle = 2.0 * Pi * Index / 72.0;
            Points.push_back({ Centre.X + Radius * std::cos(Angle), Centre.Y + Radius * std::sin(Angle) });
        }
        return Points;
    }

    void DrawTitleBlock(DrawingSheet& Sheet, const DrawingSheetRequest& Request)
    {
        const double Left = Sheet.TitleLow.X, Bottom = Sheet.TitleLow.Y;
        const auto Rule = [&](double FromX, double FromY, double ToX, double ToY) { Sheet.Lines.push_back({ SheetLayer::Title, { { Left + FromX, Bottom + FromY }, { Left + ToX, Bottom + ToY } } }); };
        Sheet.Lines.push_back({ SheetLayer::Title, { Sheet.TitleLow, { Sheet.TitleHigh.X, Sheet.TitleLow.Y }, Sheet.TitleHigh, { Sheet.TitleLow.X, Sheet.TitleHigh.Y }, Sheet.TitleLow } });
        Rule(110.0, 0.0, 110.0, TitleHeight);
        Rule(0.0, 12.0, 110.0, 12.0);
        Rule(0.0, 24.0, 110.0, 24.0);
        Rule(80.0, 12.0, 80.0, 24.0);
        Rule(40.0, 0.0, 40.0, 12.0);
        Rule(75.0, 0.0, 75.0, 12.0);
        Rule(110.0, 18.0, TitleWidth, 18.0);
        Rule(145.0, 18.0, 145.0, TitleHeight);
        Rule(150.0, 0.0, 150.0, 18.0);
        const auto Cell = [&](double X, double Top, double Base, const char* Caption, const std::string& Value, double Size)
        {
            Sheet.Texts.push_back({ SheetLayer::Title, { Left + X + 1.5, Bottom + Top - 2.8 }, 1.8, 0.0, SheetTextAnchor::Start, Caption });
            if (!Value.empty()) Sheet.Texts.push_back({ SheetLayer::Title, { Left + X + 1.5, Bottom + Base + 2.5 }, Size, 0.0, SheetTextAnchor::Start, Value });
        };
        const DrawingTitleBlock& Title = Request.Title;
        Cell(0.0, 36.0, 24.0, "TITLE", Title.Title, 5.0);
        Cell(0.0, 24.0, 12.0, "DRAWING NO.", Title.DrawingNumber, 3.5);
        Cell(80.0, 24.0, 12.0, "REV", Title.Revision, 3.5);
        Cell(0.0, 12.0, 0.0, "DRAWN", Title.Author, 3.5);
        Cell(40.0, 12.0, 0.0, "DATE", Title.Date, 3.5);
        Cell(75.0, 12.0, 0.0, "MATERIAL", Title.Material, 3.5);
        Cell(110.0, 36.0, 18.0, Sheet.Angle == DrawingProjectionAngle::Third ? "THIRD ANGLE" : "FIRST ANGLE", "", 3.5);
        Cell(145.0, 36.0, 18.0, "SCALE", Sheet.ScaleLabel, 5.0);
        Cell(110.0, 18.0, 0.0, "COMPANY", Title.Company, 3.5);
        Cell(150.0, 18.0, 0.0, "PAPER", std::string(DrawingPaperLabel(Request.Paper)) + " mm", 3.5);

        // 📝 ISO 5456-2 symbol: a truncated cone, small end ⌀4 on the left and large end ⌀8, beside its end view (two
        //    concentric circles). Third angle: circles left of the cone; first angle: circles right of it.
        constexpr double Small = 2.0, Large = 4.0, Length = 8.0, Gap = 5.0;
        const double Start  = Left + 127.5 - 0.5 * (2.0 * Large + Gap + Length);
        const double Middle = Bottom + 25.5;
        const bool   Third  = Sheet.Angle == DrawingProjectionAngle::Third;
        const double CircleX = Third ? Start + Large : Start + Length + Gap + Large;
        const double ConeX   = Third ? Start + 2.0 * Large + Gap : Start;
        Sheet.Lines.push_back({ SheetLayer::Title, CirclePolyline({ CircleX, Middle }, Small) });
        Sheet.Lines.push_back({ SheetLayer::Title, CirclePolyline({ CircleX, Middle }, Large) });
        Sheet.Lines.push_back({ SheetLayer::Title, { { ConeX, Middle - Small }, { ConeX + Length, Middle - Large }, { ConeX + Length, Middle + Large }, { ConeX, Middle + Small }, { ConeX, Middle - Small } } });
        Sheet.Lines.push_back({ SheetLayer::Centre, { { Start - 1.5, Middle }, { Start + 2.0 * Large + Gap + Length + 1.5, Middle } } });
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

Vector2 DrawingPaperDimensions(DrawingPaperSize Paper) noexcept
{
    switch (Paper)
    {
    case DrawingPaperSize::A4: return { 297.0, 210.0 };
    case DrawingPaperSize::A3: return { 420.0, 297.0 };
    case DrawingPaperSize::A2: return { 594.0, 420.0 };
    case DrawingPaperSize::A1: return { 841.0, 594.0 };
    case DrawingPaperSize::A0: return { 1189.0, 841.0 };
    }
    return { 420.0, 297.0 };
}

const char* DrawingPaperLabel(DrawingPaperSize Paper) noexcept
{
    switch (Paper)
    {
    case DrawingPaperSize::A4: return "A4";
    case DrawingPaperSize::A3: return "A3";
    case DrawingPaperSize::A2: return "A2";
    case DrawingPaperSize::A1: return "A1";
    case DrawingPaperSize::A0: return "A0";
    }
    return "?";
}

const char* SheetLayerLabel(SheetLayer Layer) noexcept
{
    switch (Layer)
    {
    case SheetLayer::Visible:        return "VISIBLE";
    case SheetLayer::Hidden:         return "HIDDEN";
    case SheetLayer::Tangent:        return "TANGENT";
    case SheetLayer::SectionOutline: return "SECTION";
    case SheetLayer::Hatch:          return "HATCH";
    case SheetLayer::Dimension:      return "DIMENSION";
    case SheetLayer::Centre:         return "CENTRE";
    case SheetLayer::CuttingPlane:   return "CUTTING";
    case SheetLayer::Border:         return "BORDER";
    case SheetLayer::Title:          return "TITLE";
    }
    return "?";
}

std::string FormatDrawingScale(double Scale)
{
    char Text[64];
    if (Scale >= 1.0) std::snprintf(Text, sizeof(Text), "%g:1", Scale);
    else std::snprintf(Text, sizeof(Text), "1:%g", std::round(1.0 / Scale * 1.0e6) / 1.0e6);
    return Text;
}

std::string FormatDimensionValue(double Value)
{
    char Text[64];
    std::snprintf(Text, sizeof(Text), "%.3f", Value);
    std::string Result = Text;
    while (!Result.empty() && Result.back() == '0') Result.pop_back();
    if (!Result.empty() && Result.back() == '.') Result.pop_back();
    if (Result == "-0") Result = "0";
    return Result;
}

std::vector<double> EnumerateStandardScales()
{
    return { 100.0, 50.0, 20.0, 10.0, 5.0, 2.0, 1.0, 0.5, 0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001 };
}

DrawingSheet ComposeDrawingSheet(const ModelPartition& Partition, const std::vector<BodyToken>& Bodies, const DrawingSheetRequest& Request)
{
    const auto   Start = std::chrono::steady_clock::now();
    DrawingSheet Sheet;
    const auto   Finish = [&]() -> DrawingSheet
    {
        Sheet.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        return Sheet;
    };
    const Vector2 Paper = DrawingPaperDimensions(Request.Paper);
    Sheet.Width  = Paper.X;
    Sheet.Height = Paper.Y;
    Sheet.Angle  = Request.Angle;
    if (Bodies.empty()) { Sheet.Refusal = "Drawing: no bodies to project"; return Finish(); }
    if (!(Request.ModelUnitMillimetres > 0.0)) { Sheet.Refusal = "Drawing: the model unit must be a positive length"; return Finish(); }
    if (Request.Scale < 0.0 || !std::isfinite(Request.Scale)) { Sheet.Refusal = "Drawing: the scale must be positive (0 picks one)"; return Finish(); }

    // ---- ① frame, title block and drawing area (ISO 5457: 20 mm filing margin on the left, 10 mm elsewhere)
    Sheet.BorderLow  = { 20.0, 10.0 };
    Sheet.BorderHigh = { Paper.X - 10.0, Paper.Y - 10.0 };
    Sheet.TitleLow   = { Sheet.BorderHigh.X - TitleWidth, Sheet.BorderLow.Y };
    Sheet.TitleHigh  = { Sheet.BorderHigh.X, Sheet.BorderLow.Y + TitleHeight };
    Sheet.AreaLow    = { Sheet.BorderLow.X + 5.0, Sheet.TitleHigh.Y + 5.0 };
    Sheet.AreaHigh   = { Sheet.BorderHigh.X - 5.0, Sheet.BorderHigh.Y - 5.0 };

    // ---- ② projections: Front, Top, Right, then the section (or the isometric) for the free quadrant
    std::vector<PlacedDrawingView> Views;
    const auto Project = [&](const char* Label, OrthonormalBasis Frame, bool Hidden, std::optional<OrthonormalBasis> Section, double HatchSpacing) -> bool
    {
        DrawingViewRequest ViewRequest;
        ViewRequest.Frame               = Frame;
        ViewRequest.HiddenLinesEnabled  = Hidden && Request.HiddenLinesEnabled;
        ViewRequest.TangentEdgesEnabled = Request.TangentEdgesEnabled;
        ViewRequest.Section             = Section;
        ViewRequest.HatchSpacing        = HatchSpacing;
        PlacedDrawingView Placed;
        Placed.Label = Label;
        Placed.View  = ProjectDrawingView(Partition, Bodies, ViewRequest);
        if (!Placed.View.Refusal.empty()) { Sheet.Refusal = Placed.View.Refusal; return false; }
        Views.push_back(std::move(Placed));
        return true;
    };
    if (!Project("Front", StandardDrawingFrame(DrawingStandardView::Front), true, {}, 0.0)) return Finish();
    if (!Project("Top", StandardDrawingFrame(DrawingStandardView::Top), true, {}, 0.0)) return Finish();
    if (!Project("Right", StandardDrawingFrame(DrawingStandardView::Right), true, {}, 0.0)) return Finish();
    OrthonormalBasis SectionBasis = {};
    if (Request.Section)
    {
        const Vector3 Normal = Request.Section->ZAxis;
        bool Matched = false;
        for (DrawingStandardView Kind : { DrawingStandardView::Front, DrawingStandardView::Back, DrawingStandardView::Top, DrawingStandardView::Bottom, DrawingStandardView::Right, DrawingStandardView::Left })
            if (!Matched && Dot(StandardDrawingFrame(Kind).ZAxis, Normal) > 1.0 - 1.0e-12) { SectionBasis = StandardDrawingFrame(Kind); Matched = true; }
        if (!Matched)
        {
            const Vector3 Up = std::fabs(Normal.Z) < 0.9 ? Vector3{ 0.0, 0.0, 1.0 } : Vector3{ 0.0, 1.0, 0.0 };
            SectionBasis = ConstructFrame({}, Normal, Cross(Up, Normal));
        }
        if (!Project("Section A-A", SectionBasis, true, Request.Section, 0.0)) return Finish();
    }
    else if (Request.IsometricEnabled)
    {
        if (!Project("Isometric", StandardDrawingFrame(DrawingStandardView::Isometric), false, {}, 0.0)) return Finish();
    }

    // Section line: in the first principal view that sees the plane edge-on (model view coordinates, viewing direction).
    int                                       CuttingView = -1;
    std::pair<Vector2, Vector2>               CuttingModel = {};
    Vector2                                   Looking = {};
    if (Request.Section)
    {
        const OrthonormalBasis& Plane = *Request.Section;
        for (int Index = 0; Index < 3 && CuttingView < 0; ++Index)
        {
            const OrthonormalBasis& Frame = Views[static_cast<size_t>(Index)].View.Frame;
            if (std::fabs(Dot(Plane.ZAxis, Frame.ZAxis)) > 1.0e-9) continue;
            const Vector2 Normal = { Dot(Plane.ZAxis, Frame.XAxis), Dot(Plane.ZAxis, Frame.YAxis) };
            const double  Level  = Dot(Plane.Origin - Frame.Origin, Plane.ZAxis);
            const Vector2 Foot   = { Normal.X * Level, Normal.Y * Level };
            const Vector2 Along  = { -Normal.Y, Normal.X };
            const DrawingView& View = Views[static_cast<size_t>(Index)].View;
            double Least = 1.0e300, Most = -1.0e300;
            for (const double X : { View.Low.X, View.High.X })
                for (const double Y : { View.Low.Y, View.High.Y }) { const double T = (X - Foot.X) * Along.X + (Y - Foot.Y) * Along.Y; Least = std::min(Least, T); Most = std::max(Most, T); }
            CuttingModel = { { Foot.X + Along.X * Least, Foot.Y + Along.Y * Least }, { Foot.X + Along.X * Most, Foot.Y + Along.Y * Most } };
            Looking      = { -Normal.X, -Normal.Y };
            CuttingView  = Index;
        }
    }

    // ---- ③ layout at one scale: annotations in view-local millimetres, then placement by the projection angle
    std::vector<ViewAnnotation> Notes;
    const auto Layout = [&](double Scale) -> bool
    {
        const double Millimetres = Scale * Request.ModelUnitMillimetres;
        Notes.clear();
        for (size_t Index = 0; Index < Views.size(); ++Index)
        {
            std::optional<std::pair<Vector2, Vector2>> Cutting;
            if (static_cast<int>(Index) == CuttingView)
            {
                const Vector2 Along = { CuttingModel.second.X - CuttingModel.first.X, CuttingModel.second.Y - CuttingModel.first.Y };
                const double  Span  = std::hypot(Along.X, Along.Y);
                const Vector2 Unit  = { Along.X / Span, Along.Y / Span };
                Cutting = std::pair{ Vector2{ Millimetres * CuttingModel.first.X - 5.0 * Unit.X, Millimetres * CuttingModel.first.Y - 5.0 * Unit.Y },
                                     Vector2{ Millimetres * CuttingModel.second.X + 5.0 * Unit.X, Millimetres * CuttingModel.second.Y + 5.0 * Unit.Y } };
            }
            Notes.push_back(AnnotateView(Views[Index], static_cast<ViewSlot>(std::min<size_t>(Index, 3)), Millimetres, Request, Cutting, Looking, Request.ModelUnitMillimetres));
        }
        // Origins: Front at 0; Top shares Front's 𝑥 mapping, Right its 𝑦 mapping.
        std::vector<Vector2> Origins(Views.size(), Vector2{});
        const ViewAnnotation& Front = Notes[0];
        const ViewAnnotation& Top   = Notes[1];
        const ViewAnnotation& Right = Notes[2];
        const double Gap = Request.ViewGap;
        if (Request.Angle == DrawingProjectionAngle::Third)
        {
            Origins[1] = { 0.0, Front.High.Y + Gap - Top.Low.Y };
            Origins[2] = { Front.High.X + Gap - Right.Low.X, 0.0 };
            if (Views.size() > 3)
            {
                const ViewAnnotation& Fourth = Notes[3];
                const double X = std::max(Origins[2].X + Right.Low.X, Origins[1].X + Top.High.X + Gap) - Fourth.Low.X;
                const double Y = std::max(Origins[2].Y + Right.High.Y + Gap, Origins[1].Y + Top.Low.Y) - Fourth.Low.Y;
                Origins[3] = { X, Y };
            }
        }
        else
        {
            Origins[1] = { 0.0, Front.Low.Y - Gap - Top.High.Y };
            Origins[2] = { Front.Low.X - Gap - Right.High.X, 0.0 };
            if (Views.size() > 3)
            {
                const ViewAnnotation& Fourth = Notes[3];
                const double X = std::min(Origins[2].X + Right.High.X, Origins[1].X + Top.Low.X - Gap) - Fourth.High.X;
                const double Y = std::min(Origins[2].Y + Right.Low.Y - Gap, Origins[1].Y + Top.High.Y) - Fourth.High.Y;
                Origins[3] = { X, Y };
            }
        }
        Vector2 Low = { 1.0e300, 1.0e300 }, High = { -1.0e300, -1.0e300 };
        for (size_t Index = 0; Index < Views.size(); ++Index)
        {
            Grow(Low, High, { Origins[Index].X + Notes[Index].Low.X, Origins[Index].Y + Notes[Index].Low.Y });
            Grow(Low, High, { Origins[Index].X + Notes[Index].High.X, Origins[Index].Y + Notes[Index].High.Y });
        }
        const double Width = High.X - Low.X, Height = High.Y - Low.Y;
        if (Width > Sheet.AreaHigh.X - Sheet.AreaLow.X || Height > Sheet.AreaHigh.Y - Sheet.AreaLow.Y) return false;
        const Vector2 Shift = { 0.5 * (Sheet.AreaLow.X + Sheet.AreaHigh.X) - 0.5 * (Low.X + High.X), 0.5 * (Sheet.AreaLow.Y + Sheet.AreaHigh.Y) - 0.5 * (Low.Y + High.Y) };
        for (size_t Index = 0; Index < Views.size(); ++Index)
        {
            PlacedDrawingView& Placed = Views[Index];
            Placed.Origin        = { Origins[Index].X + Shift.X, Origins[Index].Y + Shift.Y };
            Placed.Low           = { Placed.Origin.X + Millimetres * Placed.View.Low.X, Placed.Origin.Y + Millimetres * Placed.View.Low.Y };
            Placed.High          = { Placed.Origin.X + Millimetres * Placed.View.High.X, Placed.Origin.Y + Millimetres * Placed.View.High.Y };
            Placed.FootprintLow  = { Placed.Origin.X + Notes[Index].Low.X, Placed.Origin.Y + Notes[Index].Low.Y };
            Placed.FootprintHigh = { Placed.Origin.X + Notes[Index].High.X, Placed.Origin.Y + Notes[Index].High.Y };
        }
        return true;
    };

    double Chosen = 0.0;
    if (Request.Scale > 0.0)
    {
        if (!Layout(Request.Scale))
        {
            Sheet.Refusal = std::string("Drawing: the views do not fit on ") + DrawingPaperLabel(Request.Paper) + " at " + FormatDrawingScale(Request.Scale);
            return Finish();
        }
        Chosen = Request.Scale;
    }
    else
    {
        for (const double Candidate : EnumerateStandardScales()) if (Layout(Candidate)) { Chosen = Candidate; break; }
        if (Chosen == 0.0) { Sheet.Refusal = std::string("Drawing: the views do not fit on ") + DrawingPaperLabel(Request.Paper) + " even at 1:1000"; return Finish(); }
    }
    Sheet.Scale      = Chosen;
    Sheet.ScaleLabel = FormatDrawingScale(Chosen);
    const double Millimetres = Chosen * Request.ModelUnitMillimetres;

    // Hatch spacing is set on paper: re-project the section at the chosen scale and lay out again (same boxes).
    if (Request.Section)
    {
        Views.pop_back();
        if (!Project("Section A-A", SectionBasis, true, Request.Section, Request.HatchSpacing / Millimetres)) return Finish();
        (void)Layout(Chosen);
    }

    // ---- ④ emit: border, title block, view strokes, annotations
    Sheet.Lines.push_back({ SheetLayer::Border, { Sheet.BorderLow, { Sheet.BorderHigh.X, Sheet.BorderLow.Y }, Sheet.BorderHigh, { Sheet.BorderLow.X, Sheet.BorderHigh.Y }, Sheet.BorderLow } });
    DrawTitleBlock(Sheet, Request);
    for (size_t Index = 0; Index < Views.size(); ++Index)
    {
        const PlacedDrawingView& Placed = Views[Index];
        for (const DrawingStroke& Stroke : Placed.View.Strokes)
        {
            SheetLayer Layer = SheetLayer::Visible;
            if (Stroke.Kind == DrawingStrokeCategory::Hatch) Layer = SheetLayer::Hatch;
            else if (Stroke.Visibility == DrawingVisibility::Hidden) Layer = SheetLayer::Hidden;
            else if (Stroke.Kind == DrawingStrokeCategory::TangentEdge) Layer = SheetLayer::Tangent;
            else if (Stroke.Kind == DrawingStrokeCategory::SectionOutline) Layer = SheetLayer::SectionOutline;
            SheetPolyline Line{ Layer, {} };
            for (const Vector2& Point : Stroke.Points) Line.Points.push_back({ Placed.Origin.X + Millimetres * Point.X, Placed.Origin.Y + Millimetres * Point.Y });
            Sheet.Lines.push_back(std::move(Line));
        }
        const auto Move = [&](Vector2 Point) { return Vector2{ Point.X + Placed.Origin.X, Point.Y + Placed.Origin.Y }; };
        for (SheetPolyline Line : Notes[Index].Lines) { for (Vector2& Point : Line.Points) Point = Move(Point); Sheet.Lines.push_back(std::move(Line)); }
        for (SheetText Text : Notes[Index].Texts) { Text.Position = Move(Text.Position); Sheet.Texts.push_back(std::move(Text)); }
        for (SheetSolid Solid : Notes[Index].Solids) { for (Vector2& Corner : Solid.Corners) Corner = Move(Corner); Sheet.Solids.push_back(Solid); }
        for (DrawingDimension Dimension : Notes[Index].Dimensions)
        {
            Dimension.View      = static_cast<int>(Index);
            Dimension.First     = Move(Dimension.First);
            Dimension.Second    = Move(Dimension.Second);
            Dimension.LineStart = Move(Dimension.LineStart);
            Dimension.LineEnd   = Move(Dimension.LineEnd);
            Dimension.TextLow   = Move(Dimension.TextLow);
            Dimension.TextHigh  = Move(Dimension.TextHigh);
            Sheet.Dimensions.push_back(std::move(Dimension));
        }
    }
    Sheet.Views = std::move(Views);
    return Finish();
}

} // namespace Frontier::GeometrySpace::Parametric

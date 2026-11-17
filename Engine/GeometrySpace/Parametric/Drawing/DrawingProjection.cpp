/*====================================================================================================================================
                                                     DRAWINGPROJECTION.CPP
====================================================================================================================================*/
// 🧩 Orthographic drawing views: projected edges, exact silhouettes, ray-cast hidden lines, section views with hatching

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Drawing/DrawingProjection.h"
#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"
#include "Engine/GeometrySpace/Parametric/Containment/FaceDomain.h"
#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Intersection/CurveSurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Measurement/MassProperties.h"
#include "Engine/GeometrySpace/Parametric/Measurement/PlaneSection.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <map>
#include <set>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // A face that can hide strokes: its exact chart domain and its view-space box.
    struct OccluderFace
    {
        FaceToken  Face      = {};                                                           // [-]   - Face
        BodyToken  Body      = {};                                                           // [-]   - Its body
        FaceDomain Domain    = {};                                                           // [-]   - Chart loops + carrier
        Vector2    Low       = {};                                                           // [len] - View box
        Vector2    High      = {};                                                           // [len] - View box
        double     DepthHigh = 0.0;                                                          // [len] - Nearest point toward the viewer
    };

    struct ViewContext
    {
        const ModelPartition*     Partition    = nullptr;                                    // [-]   - Partition projected (scratch for sections)
        OrthonormalFrame          Frame        = {};                                         // [-]   - View frame
        std::vector<OccluderFace> Faces        = {};                                         // [-]   - Every face of the shown bodies
        double                    Reach        = 1.0;                                        // [len] - Model radius
        double                    DepthCeiling = 0.0;                                        // [len] - Largest depth of any face box
        double                    Gap          = 1.0e-7;                                     // [len] - Ray start clearance (own face)
        double                    Margin       = 1.0e-9;                                     // [len] - View box slack
        long                      Probes       = 0;                                          // [-]   - Rays cast
    };

    // One stroke before visibility: a parameterised 3D curve with the samples that carry its shape.
    struct StrokeSource
    {
        DrawingStrokeKind                    Kind     = DrawingStrokeKind::Edge;             // [-] - What draws it
        BodyToken                            Body     = {};                                  // [-] - Body
        uint32_t                             Source   = 0;                                   // [-] - Edge or face slot
        std::function<Vector3(double)>       Evaluate = {};                                  // [-] - Point at a parameter
        std::vector<double>                  Shape    = {};                                  // [-] - Ascending parameters, first = low, last = high
        bool                                 Straight = false;                               // [-] - Only the run ends are emitted
        std::vector<Vector2>                 Outline  = {};                                  // [len] - Projected Shape samples (snapping)
        Vector2                              Low      = {};                                  // [len] - Outline box
        Vector2                              High     = {};                                  // [len] - Outline box
    };

    [[nodiscard]] double ViewDepth(const OrthonormalFrame& Frame, Vector3 Position) noexcept { return Dot(Position - Frame.Origin, Frame.ZAxis); }

    [[nodiscard]] bool OrthonormalCondition(const OrthonormalFrame& Frame) noexcept
    {
        constexpr double Tolerance = 1.0e-9;
        const bool Units = std::fabs(Length(Frame.XAxis) - 1.0) < Tolerance && std::fabs(Length(Frame.YAxis) - 1.0) < Tolerance && std::fabs(Length(Frame.ZAxis) - 1.0) < Tolerance;
        const bool Square = std::fabs(Dot(Frame.XAxis, Frame.YAxis)) < Tolerance && std::fabs(Dot(Frame.YAxis, Frame.ZAxis)) < Tolerance && std::fabs(Dot(Frame.ZAxis, Frame.XAxis)) < Tolerance;
        return Units && Square && Dot(Cross(Frame.XAxis, Frame.YAxis), Frame.ZAxis) > 0.0;
    }

    // 💡 Quantitative invisibility by one exact ray: hidden when the ray toward the viewer pierces the interior of a face
    //    (a touch, a boundary hit or the point's own face within Gap do not count).
    [[nodiscard]] bool HiddenCondition(ViewContext& View, Vector3 Position)
    {
        ++View.Probes;
        const Vector2 At   = ProjectDrawingPoint(View.Frame, Position);
        const double  Here = ViewDepth(View.Frame, Position);
        const double  Span = View.DepthCeiling - Here + 0.01 * View.Reach;                   // [len] - Ray length past every face
        if (Span <= View.Gap) return false;
        const CurveGeometry Ray  = StraightLine{ Position, View.Frame.ZAxis };
        const double        Back = 0.01 * View.Reach;                                        // [len] - Start behind P so a grazing pair merges into a touch
        for (const OccluderFace& Face : View.Faces)
        {
            if (At.X < Face.Low.X - View.Margin || At.X > Face.High.X + View.Margin || At.Y < Face.Low.Y - View.Margin || At.Y > Face.High.Y + View.Margin) continue;
            if (Face.DepthHigh < Here + View.Gap) continue;
            const CurveSurfaceOutcome Hits = IntersectCurveSurface(Ray, -Back, Span, Face.Domain.Surface);
            for (const CurveSurfaceCrossing& Hit : Hits.Crossings)
            {
                if (Hit.CurveParameter <= View.Gap || Hit.Category == CrossingCategory::Tangent) continue;
                if (Hit.CurveParameter < Back)                                               // Grazing: the ray hugs the surface up to the hit
                {
                    const Vector3 Middle = Position + View.Frame.ZAxis * (0.5 * Hit.CurveParameter);
                    const Vector3 Foot   = EvaluateSurface(Face.Domain.Surface, ProjectOntoSurface(Face.Domain.Surface, Middle, Hit.SurfaceParameter)).Position;
                    if (Distance(Middle, Foot) <= SessionPrecision) continue;
                }
                if (ClassifyFacePosition(Face.Domain, Hit.Position, View.Gap).Membership == PointMembership::Inside) return true;
            }
        }
        return false;
    }

    // 💡 Exact visibility change: the bisected parameter is only good to the classifier's boundary band (~Gap), so the
    //    change is snapped to where the stroke crosses a projected outline — another edge or silhouette passing within
    //    Window of it — by Newton on p(𝑠) − q(𝑡) = 0 (2 × 2, central differences); the nearest crossing inside the
    //    bracket wins. Parallel (coincident) projections are singular and skipped.
    [[nodiscard]] double SnapTransition(const ViewContext& View, const StrokeSource& Source, const std::vector<StrokeSource>& Sources, double Before, double After)
    {
        const double Low = Source.Shape.front(), High = Source.Shape.back();
        const double Change = 0.5 * (Before + After);
        const auto   At = [&](const StrokeSource& Curve, double Parameter) { return ProjectDrawingPoint(View.Frame, Curve.Evaluate(std::clamp(Parameter, Curve.Shape.front(), Curve.Shape.back()))); };
        const Vector2 Here   = At(Source, Change);
        const double  Window = 1.0e-4 * View.Reach;
        const double  Speed  = std::max(Distance(At(Source, std::min(High, Change + 1.0e-6 * (High - Low))), At(Source, std::max(Low, Change - 1.0e-6 * (High - Low)))) / (2.0e-6 * (High - Low)), 1.0e-300);
        const double  Reach  = 2.0 * Window / Speed + (After - Before);                       // [-] - Parameter bracket accepted
        double Best = Change, BestGap = 1.0e300;
        for (const StrokeSource& Other : Sources)
        {
            if (&Other == &Source || Other.Outline.size() < 2) continue;
            if (Here.X < Other.Low.X - Window || Here.X > Other.High.X + Window || Here.Y < Other.Low.Y - Window || Here.Y > Other.High.Y + Window) continue;
            // Nearest outline segment → starting 𝑡.
            double Nearest = 1.0e300, Start = Other.Shape.front();
            for (size_t Index = 1; Index < Other.Outline.size(); ++Index)
            {
                const Vector2 First = Other.Outline[Index - 1], Second = Other.Outline[Index];
                const double  DeltaX = Second.X - First.X, DeltaY = Second.Y - First.Y, Square = DeltaX * DeltaX + DeltaY * DeltaY;
                const double  Blend = Square > 0.0 ? std::clamp(((Here.X - First.X) * DeltaX + (Here.Y - First.Y) * DeltaY) / Square, 0.0, 1.0) : 0.0;
                const double  Gap = std::hypot(First.X + DeltaX * Blend - Here.X, First.Y + DeltaY * Blend - Here.Y);
                if (Gap < Nearest) { Nearest = Gap; Start = Other.Shape[Index - 1] + (Other.Shape[Index] - Other.Shape[Index - 1]) * Blend; }
            }
            if (Nearest > Window) continue;
            double Parameter = Change, Along = Start;
            const double SpanS = 1.0e-7 * (High - Low), SpanT = 1.0e-7 * (Other.Shape.back() - Other.Shape.front());
            bool Converged = false, Singular = false;
            for (int Step = 0; Step < 30; ++Step)
            {
                const Vector2 P = At(Source, Parameter), Q = At(Other, Along);
                const double  ResidualX = P.X - Q.X, ResidualY = P.Y - Q.Y;
                const Vector2 PS = At(Source, Parameter + SpanS), PN = At(Source, Parameter - SpanS);
                const Vector2 QS = At(Other, Along + SpanT), QN = At(Other, Along - SpanT);
                const double  A = (PS.X - PN.X) / (2.0 * SpanS), C = (PS.Y - PN.Y) / (2.0 * SpanS);   // ∂p/∂𝑠
                const double  B = -(QS.X - QN.X) / (2.0 * SpanT), D = -(QS.Y - QN.Y) / (2.0 * SpanT); // −∂q/∂𝑡
                const double  Determinant = A * D - B * C;
                if (std::fabs(Determinant) <= 1.0e-9 * std::hypot(A, C) * std::hypot(B, D) || !std::isfinite(Determinant)) { Singular = true; break; }   // Parallel: coincident outline
                if (std::hypot(ResidualX, ResidualY) <= 1.0e-15 * std::max(1.0, View.Reach)) { Converged = true; break; }
                Parameter -= (D * ResidualX - B * ResidualY) / Determinant;
                Along     -= (A * ResidualY - C * ResidualX) / Determinant;
                if (!std::isfinite(Parameter) || !std::isfinite(Along)) break;
                Converged = std::hypot(ResidualX, ResidualY) <= 1.0e-13 * std::max(1.0, View.Reach) && Step > 0;
            }
            const double SlackT = 1.0e-9 * (Other.Shape.back() - Other.Shape.front());
            if (!Converged || Singular || Along < Other.Shape.front() - SlackT || Along > Other.Shape.back() + SlackT) continue;
            if (Parameter < Before - Reach || Parameter > After + Reach) continue;
            if (std::fabs(Parameter - Change) < BestGap) { BestGap = std::fabs(Parameter - Change); Best = std::clamp(Parameter, Low, High); }
        }
        // Stroke ends: a change within the band of an end is that end (the stroke leaves a vertex on an outline).
        for (const double End : { Low, High })
            if (Distance(At(Source, End), Here) <= Window && std::fabs(End - Change) < BestGap && BestGap > 1.0e299) Best = End;
        return Best;
    }

    // Visibility runs of one stroke: classified at the shape samples plus evenly spaced probes, each change bisected
    // along the stroke parameter, then snapped to the exact outline crossing.
    void TraceStroke(ViewContext& View, const StrokeSource& Source, const std::vector<StrokeSource>& Sources, std::vector<DrawingStroke>& Strokes)
    {
        if (Source.Shape.size() < 2) return;
        const double Low = Source.Shape.front(), High = Source.Shape.back();
        if (!(High > Low)) return;
        double Projected = 0.0;
        Vector2 Previous = ProjectDrawingPoint(View.Frame, Source.Evaluate(Low));
        for (size_t Index = 1; Index < Source.Shape.size(); ++Index)
        {
            const Vector2 Next = ProjectDrawingPoint(View.Frame, Source.Evaluate(Source.Shape[Index]));
            Projected += std::hypot(Next.X - Previous.X, Next.Y - Previous.Y);
            Previous = Next;
        }
        if (Projected <= View.Margin) return;                                                // Seen end-on
        const int Probes = std::clamp(static_cast<int>(std::ceil(Projected / (0.02 * View.Reach))), 4, 200);
        std::vector<double> Parameters = Source.Shape;
        for (int Index = 1; Index < Probes; ++Index) Parameters.push_back(Low + (High - Low) * Index / Probes);
        std::sort(Parameters.begin(), Parameters.end());
        Parameters.erase(std::unique(Parameters.begin(), Parameters.end(), [&](double First, double Second) { return Second - First <= 1.0e-14 * (High - Low); }), Parameters.end());
        std::vector<bool> Hidden(Parameters.size());
        for (size_t Index = 0; Index < Parameters.size(); ++Index) Hidden[Index] = HiddenCondition(View, Source.Evaluate(Parameters[Index]));

        const auto Emit = [&](const std::vector<double>& Run, bool RunHidden)
        {
            DrawingStroke Stroke;
            Stroke.Kind       = Source.Kind;
            Stroke.Visibility = RunHidden ? DrawingVisibility::Hidden : DrawingVisibility::Visible;
            Stroke.Body       = Source.Body;
            Stroke.Source     = Source.Source;
            for (size_t Index = 0; Index < Run.size(); ++Index)
            {
                if (Source.Straight && Index != 0 && Index + 1 != Run.size()) continue;
                Stroke.Points.push_back(ProjectDrawingPoint(View.Frame, Source.Evaluate(Run[Index])));
            }
            double Span = 0.0;
            for (size_t Index = 1; Index < Stroke.Points.size(); ++Index) Span += std::hypot(Stroke.Points[Index].X - Stroke.Points[Index - 1].X, Stroke.Points[Index].Y - Stroke.Points[Index - 1].Y);
            if (Stroke.Points.size() >= 2 && Span > View.Margin) Strokes.push_back(std::move(Stroke));
        };
        std::vector<double> Run = { Parameters.front() };
        for (size_t Index = 1; Index < Parameters.size(); ++Index)
        {
            if (Hidden[Index] != Hidden[Index - 1])
            {
                double Before = Parameters[Index - 1], After = Parameters[Index];             // Hidden[Index − 1] holds at Before
                for (int Step = 0; Step < 60 && After - Before > 1.0e-13 * (High - Low); ++Step)
                {
                    const double Middle = 0.5 * (Before + After);
                    if (HiddenCondition(View, Source.Evaluate(Middle)) == Hidden[Index - 1]) Before = Middle;
                    else After = Middle;
                }
                const double Change = SnapTransition(View, Source, Sources, Before, After);
                Run.push_back(Change);
                Emit(Run, Hidden[Index - 1]);
                Run = { Change };
            }
            Run.push_back(Parameters[Index]);
        }
        Emit(Run, Hidden.back());
    }

    // Faces around an edge through its radial fin ring (two for a manifold edge, one face twice for a seam).
    [[nodiscard]] std::vector<FaceToken> FacesAround(const ModelPartition& Partition, EdgeToken Edge)
    {
        std::vector<FaceToken> Faces;
        const FinToken First = Partition.Edges.Resolve(Edge).RadialFin;
        if (!First.PresentCondition()) return Faces;
        FinToken Fin = First;
        for (int Guard = 0; Guard < 16; ++Guard)
        {
            const FinProfile& Profile = Partition.Fins.Resolve(Fin);
            Faces.push_back(Partition.Loops.Resolve(Profile.Loop).Face);
            Fin = Profile.RadialSuccessor;
            if (!Fin.PresentCondition() || Fin == First) break;
        }
        return Faces;
    }

    [[nodiscard]] Vector3 OutwardNormal(const ModelPartition& Partition, const FaceDomain& Domain, Vector3 Position)
    {
        const Vector2 Reference = { 0.5 * (Domain.Window.Low.X + Domain.Window.High.X), 0.5 * (Domain.Window.Low.Y + Domain.Window.High.Y) };
        const Vector3 Normal    = EvaluateSurfaceNormal(Domain.Surface, ProjectOntoSurface(Domain.Surface, Position, Reference));
        (void)Partition;
        return Domain.Sense == Orientation::Forward ? Normal : Normal * -1.0;
    }

    // Parameter samples of an edge: the end points for a line, otherwise pieces no longer than Step (8 … 1024).
    [[nodiscard]] std::vector<double> EdgeShape(const ModelPartition& Partition, EdgeToken Edge, double Step)
    {
        const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
        const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
        if (std::holds_alternative<StraightLine>(Curve)) return { Profile.IntervalLow, Profile.IntervalHigh };
        const int Pieces = std::clamp(static_cast<int>(std::ceil(MeasureEdgeLength(Partition, Edge) / Step)), 8, 1024);
        std::vector<double> Shape;
        for (int Index = 0; Index <= Pieces; ++Index) Shape.push_back(Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Index / Pieces);
        return Shape;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                SILHOUETTES
    //--------------------------------------------------------------------------------------------------------------------

    // 💡 Zero set of 𝑔(𝑢, 𝑣) = N·ZAxis on the face chart: marching squares on a Density² grid, each crossing bisected on
    //    its grid edge (sign with a 10⁻¹² dead band counted positive, so a carrier seen along its rulings — a hole from
    //    above — has no contour), chains linked through shared grid edges, then cut to the face by the chart winding
    //    number (the run ends bisected onto the boundary).
    void TraceSilhouettes(ViewContext& View, const OccluderFace& Face, int Density, std::vector<StrokeSource>& Sources)
    {
        const SurfaceGeometry& Surface = Face.Domain.Surface;
        if (std::holds_alternative<PlaneSurface>(Surface)) return;
        const Vector3 Toward = View.Frame.ZAxis;
        const auto Positive = [&](Vector2 Parameter)
        {
            const double Value = Dot(EvaluateSurfaceNormal(Surface, Parameter), Toward);
            return !(Value < -1.0e-12);                                                       // NaN (apex, pole) counts positive
        };
        const Vector2 Low = Face.Domain.Window.Low, High = Face.Domain.Window.High;
        if (!(High.X > Low.X) || !(High.Y > Low.Y)) return;
        const int Columns = Density, Rows = Density;
        const auto Node = [&](int Column, int Row) { return Vector2{ Low.X + (High.X - Low.X) * Column / Columns, Low.Y + (High.Y - Low.Y) * Row / Rows }; };
        std::vector<char> Signs(static_cast<size_t>((Columns + 1) * (Rows + 1)));
        for (int Row = 0; Row <= Rows; ++Row)
            for (int Column = 0; Column <= Columns; ++Column) Signs[static_cast<size_t>(Row * (Columns + 1) + Column)] = Positive(Node(Column, Row)) ? 1 : 0;
        const auto SignAt = [&](int Column, int Row) { return Signs[static_cast<size_t>(Row * (Columns + 1) + Column)]; };

        // Crossing points keyed by grid edge: 2·node index for the edge toward +𝑢, +1 for the edge toward +𝑣.
        std::map<long, Vector2> Crossings;
        const auto Crossing = [&](int Column, int Row, bool AlongV) -> long
        {
            const long Key = 2L * (Row * (Columns + 1) + Column) + (AlongV ? 1 : 0);
            if (Crossings.count(Key)) return Key;
            Vector2 Before = Node(Column, Row);
            Vector2 After  = AlongV ? Node(Column, Row + 1) : Node(Column + 1, Row);
            const bool First = SignAt(Column, Row) != 0;
            for (int Step = 0; Step < 64; ++Step)
            {
                const Vector2 Middle = { 0.5 * (Before.X + After.X), 0.5 * (Before.Y + After.Y) };
                if (Positive(Middle) == First) Before = Middle;
                else After = Middle;
            }
            Crossings[Key] = { 0.5 * (Before.X + After.X), 0.5 * (Before.Y + After.Y) };
            return Key;
        };
        std::map<long, std::vector<long>> Links;
        const auto Join = [&](long First, long Second) { Links[First].push_back(Second); Links[Second].push_back(First); };
        for (int Row = 0; Row < Rows; ++Row)
        {
            for (int Column = 0; Column < Columns; ++Column)
            {
                const char Corner[4] = { SignAt(Column, Row), SignAt(Column + 1, Row), SignAt(Column + 1, Row + 1), SignAt(Column, Row + 1) };
                std::vector<long> Cut;                                                       // Order: bottom, right, top, left
                long Sides[4] = { -1, -1, -1, -1 };
                if (Corner[0] != Corner[1]) Sides[0] = Crossing(Column, Row, false);
                if (Corner[1] != Corner[2]) Sides[1] = Crossing(Column + 1, Row, true);
                if (Corner[3] != Corner[2]) Sides[2] = Crossing(Column, Row + 1, false);
                if (Corner[0] != Corner[3]) Sides[3] = Crossing(Column, Row, true);
                for (long Side : Sides) if (Side >= 0) Cut.push_back(Side);
                if (Cut.size() == 2) Join(Cut[0], Cut[1]);
                else if (Cut.size() == 4)
                {
                    const Vector2 Centre = { Low.X + (High.X - Low.X) * (Column + 0.5) / Columns, Low.Y + (High.Y - Low.Y) * (Row + 0.5) / Rows };
                    if ((Positive(Centre) ? 1 : 0) == Corner[0]) { Join(Sides[0], Sides[1]); Join(Sides[2], Sides[3]); }   // Corners 1 and 3 cut off
                    else { Join(Sides[0], Sides[3]); Join(Sides[1], Sides[2]); }                                         // Corners 0 and 2 cut off
                }
            }
        }

        // Chains: open ones from their degree-one ends first, then the closed loops.
        std::set<long> Used;
        std::vector<std::vector<Vector2>> Chains;
        const auto Walk = [&](long Start)
        {
            std::vector<Vector2> Chain;
            long Here = Start, From = -1;
            while (Here >= 0 && !Used.count(Here))
            {
                Used.insert(Here);
                Chain.push_back(Crossings[Here]);
                long Next = -1;
                for (long Candidate : Links[Here]) if (Candidate != From && !Used.count(Candidate)) { Next = Candidate; break; }
                if (Next < 0)
                    for (long Candidate : Links[Here]) if (Candidate == Start && Chain.size() > 2) Chain.push_back(Crossings[Start]);
                From = Here;
                Here = Next;
            }
            if (Chain.size() >= 2) Chains.push_back(std::move(Chain));
        };
        for (const auto& [Key, Neighbours] : Links) if (Neighbours.size() == 1 && !Used.count(Key)) Walk(Key);
        for (const auto& [Key, Neighbours] : Links) if (!Used.count(Key)) Walk(Key);

        // Trim to the face and hand each inside run over as a stroke parameterised by its point index.
        const auto Inside = [&](Vector2 Parameter) { return ClassifyParameterPoint(Face.Domain, Parameter) != PointMembership::Outside; };
        for (const std::vector<Vector2>& Chain : Chains)
        {
            std::vector<Vector2> Run;
            const auto Close = [&]()
            {
                if (Run.size() >= 2)
                {
                    StrokeSource Source;
                    Source.Kind   = DrawingStrokeKind::Silhouette;
                    Source.Body   = Face.Body;
                    Source.Source = Face.Face.Slot;
                    const std::vector<Vector2> Points = Run;
                    // Between contour points the chord is pushed back onto N·ZAxis = 0 across itself (Newton,
                    // central differences), so every evaluated point lies on the exact silhouette.
                    Source.Evaluate = [Points, &Surface, Toward](double Parameter)
                    {
                        const double Clamped = std::clamp(Parameter, 0.0, static_cast<double>(Points.size() - 1));
                        const size_t Index   = std::min(static_cast<size_t>(Clamped), Points.size() - 2);
                        const double Blend   = Clamped - static_cast<double>(Index);
                        const Vector2 First = Points[Index], Second = Points[Index + 1];
                        Vector2 At = { First.X + (Second.X - First.X) * Blend, First.Y + (Second.Y - First.Y) * Blend };
                        if (Blend > 0.0 && Blend < 1.0)
                        {
                            const double  Chord  = std::hypot(Second.X - First.X, Second.Y - First.Y);
                            const Vector2 Across = { -(Second.Y - First.Y) / Chord, (Second.X - First.X) / Chord };
                            const auto    Value  = [&](double Offset) { return Dot(EvaluateSurfaceNormal(Surface, { At.X + Across.X * Offset, At.Y + Across.Y * Offset }), Toward); };
                            const double  Step   = 1.0e-7 * Chord;
                            double Offset = 0.0;
                            for (int Iteration = 0; Iteration < 8; ++Iteration)
                            {
                                const double Here = Value(Offset);
                                if (!std::isfinite(Here) || std::fabs(Here) <= 1.0e-16) break;
                                const double Slope = (Value(Offset + Step) - Value(Offset - Step)) / (2.0 * Step);
                                if (!std::isfinite(Slope) || Slope == 0.0) break;
                                const double Next = Offset - Here / Slope;
                                if (std::fabs(Next) > Chord) break;
                                Offset = Next;
                            }
                            At = { At.X + Across.X * Offset, At.Y + Across.Y * Offset };
                        }
                        return EvaluateSurface(Surface, At).Position;
                    };
                    for (size_t Index = 0; Index < Points.size(); ++Index) Source.Shape.push_back(static_cast<double>(Index));
                    Sources.push_back(std::move(Source));
                }
                Run.clear();
            };
            bool Previous = Inside(Chain.front());
            if (Previous) Run.push_back(Chain.front());
            for (size_t Index = 1; Index < Chain.size(); ++Index)
            {
                const bool Current = Inside(Chain[Index]);
                if (Current != Previous)
                {
                    Vector2 Before = Chain[Index - 1], After = Chain[Index];
                    for (int Step = 0; Step < 50; ++Step)
                    {
                        const Vector2 Middle = { 0.5 * (Before.X + After.X), 0.5 * (Before.Y + After.Y) };
                        if (Inside(Middle) == Previous) Before = Middle;
                        else After = Middle;
                    }
                    Run.push_back(Previous ? Before : After);
                    if (Previous) Close();
                }
                if (Current) Run.push_back(Chain[Index]);
                Previous = Current;
            }
            Close();
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                HATCHING
    //--------------------------------------------------------------------------------------------------------------------

    // Parallel lines n·p = (k + ½)·Spacing across one cap, clipped by the even-odd rule against its sampled boundary.
    void HatchCap(const ViewContext& View, FaceToken Cap, BodyToken Body, double Spacing, double Angle, double Step, std::vector<DrawingStroke>& Strokes)
    {
        const ModelPartition& Partition = *View.Partition;
        std::vector<std::pair<Vector2, Vector2>> Boundary;
        for (LoopToken Loop : Partition.Faces.Resolve(Cap).Loops)
        {
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const EdgeToken      Edge    = Partition.Fins.Resolve(Fin).Edge;
                const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
                const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
                std::vector<double> Shape = EdgeShape(Partition, Edge, 0.25 * Step);
                for (size_t Index = 1; Index < Shape.size(); ++Index)
                    Boundary.push_back({ ProjectDrawingPoint(View.Frame, EvaluateCurve(Curve, Shape[Index - 1]).Position), ProjectDrawingPoint(View.Frame, EvaluateCurve(Curve, Shape[Index]).Position) });
            });
        }
        if (Boundary.empty()) return;
        const Vector2 Along  = { std::cos(Angle), std::sin(Angle) };
        const Vector2 Across = { -std::sin(Angle), std::cos(Angle) };
        double Least = 1.0e300, Most = -1.0e300;
        for (const auto& [Start, End] : Boundary)
            for (const Vector2& Point : { Start, End }) { const double Offset = Across.X * Point.X + Across.Y * Point.Y; Least = std::min(Least, Offset); Most = std::max(Most, Offset); }
        for (long Line = static_cast<long>(std::ceil(Least / Spacing - 0.5)); (Line + 0.5) * Spacing <= Most; ++Line)
        {
            const double Offset = (Line + 0.5) * Spacing;
            std::vector<double> Hits;
            for (const auto& [Start, End] : Boundary)
            {
                const double Before = Across.X * Start.X + Across.Y * Start.Y - Offset;
                const double After  = Across.X * End.X + Across.Y * End.Y - Offset;
                if ((Before < 0.0) == (After < 0.0)) continue;                               // Half-open: a vertex on the line counts once
                const double  Blend = Before / (Before - After);
                const Vector2 Point = { Start.X + (End.X - Start.X) * Blend, Start.Y + (End.Y - Start.Y) * Blend };
                Hits.push_back(Along.X * Point.X + Along.Y * Point.Y);
            }
            std::sort(Hits.begin(), Hits.end());
            for (size_t Index = 0; Index + 1 < Hits.size(); Index += 2)
            {
                if (Hits[Index + 1] - Hits[Index] <= View.Margin) continue;
                DrawingStroke Stroke;
                Stroke.Kind   = DrawingStrokeKind::Hatch;
                Stroke.Body   = Body;
                Stroke.Source = Cap.Slot;
                Stroke.Points = { { Along.X * Hits[Index] + Across.X * Offset, Along.Y * Hits[Index] + Across.Y * Offset },
                                  { Along.X * Hits[Index + 1] + Across.X * Offset, Along.Y * Hits[Index + 1] + Across.Y * Offset } };
                Strokes.push_back(std::move(Stroke));
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

OrthonormalFrame StandardDrawingFrame(DrawingStandardView View) noexcept
{
    const double Third = 1.0 / std::sqrt(3.0), Sixth = 1.0 / std::sqrt(6.0), Half = 1.0 / std::sqrt(2.0);
    switch (View)
    {
    case DrawingStandardView::Front:     return { {}, { 1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, -1.0, 0.0 } };
    case DrawingStandardView::Back:      return { {}, { -1.0, 0.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 0.0, 1.0, 0.0 } };
    case DrawingStandardView::Top:       return { {}, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    case DrawingStandardView::Bottom:    return { {}, { 1.0, 0.0, 0.0 }, { 0.0, -1.0, 0.0 }, { 0.0, 0.0, -1.0 } };
    case DrawingStandardView::Right:     return { {}, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 0.0 } };
    case DrawingStandardView::Left:      return { {}, { 0.0, -1.0, 0.0 }, { 0.0, 0.0, 1.0 }, { -1.0, 0.0, 0.0 } };
    case DrawingStandardView::Isometric: return { {}, { Half, Half, 0.0 }, { -Sixth, Sixth, 2.0 * Sixth }, { Third, -Third, Third } };
    }
    return {};
}

const char* DrawingStandardViewLabel(DrawingStandardView View) noexcept
{
    switch (View)
    {
    case DrawingStandardView::Front:     return "Front";
    case DrawingStandardView::Back:      return "Back";
    case DrawingStandardView::Top:       return "Top";
    case DrawingStandardView::Bottom:    return "Bottom";
    case DrawingStandardView::Right:     return "Right";
    case DrawingStandardView::Left:      return "Left";
    case DrawingStandardView::Isometric: return "Isometric";
    }
    return "?";
}

const char* DrawingStrokeKindLabel(DrawingStrokeKind Kind) noexcept
{
    switch (Kind)
    {
    case DrawingStrokeKind::Edge:           return "edge";
    case DrawingStrokeKind::TangentEdge:    return "tangent edge";
    case DrawingStrokeKind::Silhouette:     return "silhouette";
    case DrawingStrokeKind::SectionOutline: return "section outline";
    case DrawingStrokeKind::Hatch:          return "hatch";
    }
    return "?";
}

Vector2 ProjectDrawingPoint(const OrthonormalFrame& Frame, Vector3 Position) noexcept
{
    const Vector3 Offset = Position - Frame.Origin;
    return { Dot(Offset, Frame.XAxis), Dot(Offset, Frame.YAxis) };
}

double MeasureStrokeLength(const DrawingStroke& Stroke) noexcept
{
    double Total = 0.0;
    for (size_t Index = 1; Index < Stroke.Points.size(); ++Index) Total += std::hypot(Stroke.Points[Index].X - Stroke.Points[Index - 1].X, Stroke.Points[Index].Y - Stroke.Points[Index - 1].Y);
    return Total;
}

DrawingView ProjectDrawingView(const ModelPartition& Partition, const std::vector<BodyToken>& Bodies, const DrawingViewRequest& Request)
{
    const auto  Start = std::chrono::steady_clock::now();
    DrawingView View;
    View.Frame = Request.Frame;
    const auto Finish = [&]() -> DrawingView
    {
        View.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
        return View;
    };
    if (Bodies.empty()) { View.Refusal = "Drawing: no bodies to project"; return Finish(); }
    if (!OrthonormalCondition(Request.Frame)) { View.Refusal = "Drawing: the view frame is not a right-handed orthonormal frame"; return Finish(); }
    if (Request.Section && !OrthonormalCondition(*Request.Section)) { View.Refusal = "Drawing: the section plane frame is not orthonormal"; return Finish(); }

    // ---- ① the bodies shown: as they are, or the pieces behind the section plane (on a scratch partition)
    std::optional<ModelPartition> Scratch;
    std::vector<BodyToken>        Shown = Bodies;
    std::set<uint32_t>            Caps;                                                      // Cap face slots
    std::vector<std::pair<FaceToken, BodyToken>> CapFaces;
    if (Request.Section)
    {
        Scratch = Partition;
        Shown.clear();
        const OrthonormalFrame& Plane   = *Request.Section;
        const OrthonormalFrame  Flipped = { Plane.Origin, Plane.XAxis, Plane.YAxis * -1.0, Plane.ZAxis * -1.0 };   // Keeps the −ZAxis side
        for (BodyToken Body : Bodies)
        {
            const HalfSpaceCut Cut = CutBodyAtPlane(*Scratch, Body, Flipped);
            if (Cut.Refusal.empty())
            {
                Shown.push_back(Cut.Body);
                for (FaceToken Face : Cut.Caps) { Caps.insert(Face.Slot); CapFaces.push_back({ Face, Cut.Body }); }
                continue;
            }
            // Not cut: the whole body stays when it lies behind the plane.
            const BodyMassProperties Mass = MeasureBodyMass(*Scratch, Body);
            if (Mass.Refusal.empty() && Dot(Mass.Centroid - Plane.Origin, Plane.ZAxis) < 0.0) Shown.push_back(Body);
        }
        if (CapFaces.empty()) { View.Refusal = "Drawing: the section plane misses every body"; return Finish(); }
    }
    const ModelPartition& Work = Scratch ? *Scratch : Partition;

    // ---- ② occluders: every face with its chart domain and view box
    ViewContext Context;
    Context.Partition = &Work;
    Context.Frame     = Request.Frame;
    Vector3 BoxLow = { 1.0e300, 1.0e300, 1.0e300 }, BoxHigh = { -1.0e300, -1.0e300, -1.0e300 };
    Context.DepthCeiling = -1.0e300;
    for (BodyToken Body : Shown)
    {
        for (FaceToken Face : Work.CollectInventory(Body).Faces)
        {
            if (!Work.Faces.Resolve(Face).Surface.PresentCondition()) continue;
            OccluderFace Occluder;
            Occluder.Face   = Face;
            Occluder.Body   = Body;
            Occluder.Domain = DescribeFaceDomain(Work, Face);
            Occluder.Low    = { 1.0e300, 1.0e300 };
            Occluder.High   = { -1.0e300, -1.0e300 };
            Occluder.DepthHigh = -1.0e300;
            const Vector3 Low = Occluder.Domain.BoundsLow, High = Occluder.Domain.BoundsHigh;
            for (int Corner = 0; Corner < 8; ++Corner)
            {
                const Vector3 Point = { (Corner & 1) ? High.X : Low.X, (Corner & 2) ? High.Y : Low.Y, (Corner & 4) ? High.Z : Low.Z };
                const Vector2 At    = ProjectDrawingPoint(Request.Frame, Point);
                Occluder.Low  = { std::min(Occluder.Low.X, At.X), std::min(Occluder.Low.Y, At.Y) };
                Occluder.High = { std::max(Occluder.High.X, At.X), std::max(Occluder.High.Y, At.Y) };
                Occluder.DepthHigh = std::max(Occluder.DepthHigh, ViewDepth(Request.Frame, Point));
            }
            BoxLow  = { std::min(BoxLow.X, Low.X), std::min(BoxLow.Y, Low.Y), std::min(BoxLow.Z, Low.Z) };
            BoxHigh = { std::max(BoxHigh.X, High.X), std::max(BoxHigh.Y, High.Y), std::max(BoxHigh.Z, High.Z) };
            Context.DepthCeiling = std::max(Context.DepthCeiling, Occluder.DepthHigh);
            Context.Faces.push_back(std::move(Occluder));
        }
    }
    if (Context.Faces.empty()) { View.Refusal = "Drawing: the bodies have no faces"; return Finish(); }
    Context.Reach  = std::max(0.5 * Distance(BoxLow, BoxHigh), 1.0e-6);
    Context.Gap    = 1.0e-7 * std::max(1.0, Context.Reach);
    Context.Margin = 1.0e-9 * std::max(1.0, Context.Reach);
    View.Reach     = Context.Reach;
    const double Step = Request.ChordStep > 0.0 ? Request.ChordStep : 0.005 * Context.Reach;

    // ---- ③ stroke sources: edges (seams skipped, tangent edges flagged, cap edges as section outlines), silhouettes
    std::vector<StrokeSource> Sources;
    std::map<uint32_t, const OccluderFace*> FaceBySlot;
    for (const OccluderFace& Face : Context.Faces) FaceBySlot[Face.Face.Slot] = &Face;
    for (BodyToken Body : Shown)
    {
        for (EdgeToken Edge : Work.CollectInventory(Body).Edges)
        {
            const EdgeProfile& Profile = Work.Edges.Resolve(Edge);
            if (!Profile.Curve.PresentCondition()) continue;
            const std::vector<FaceToken> Around = FacesAround(Work, Edge);
            if (Around.size() == 2 && Around[0] == Around[1]) continue;                      // Seam of a periodic face
            const CurveGeometry& Curve = Work.Curves.Resolve(Profile.Curve);
            StrokeSource Source;
            Source.Kind     = DrawingStrokeKind::Edge;
            Source.Body     = Body;
            Source.Source   = Edge.Slot;
            Source.Straight = std::holds_alternative<StraightLine>(Curve);
            Source.Evaluate = [&Curve](double Parameter) { return EvaluateCurve(Curve, Parameter).Position; };
            Source.Shape    = EdgeShape(Work, Edge, Step);
            bool CapEdge = false;
            for (FaceToken Face : Around) CapEdge = CapEdge || Caps.count(Face.Slot) > 0;
            if (CapEdge) Source.Kind = DrawingStrokeKind::SectionOutline;
            else if (Around.size() == 2 && FaceBySlot.count(Around[0].Slot) && FaceBySlot.count(Around[1].Slot))
            {
                const Vector3 Middle = EvaluateCurve(Curve, 0.5 * (Profile.IntervalLow + Profile.IntervalHigh)).Position;
                const Vector3 First  = OutwardNormal(Work, FaceBySlot[Around[0].Slot]->Domain, Middle);
                const Vector3 Second = OutwardNormal(Work, FaceBySlot[Around[1].Slot]->Domain, Middle);
                if (Length(Cross(First, Second)) < 1.0e-6 && Dot(First, Second) > 0.0) Source.Kind = DrawingStrokeKind::TangentEdge;
            }
            if (Source.Kind == DrawingStrokeKind::TangentEdge && !Request.TangentEdgesEnabled) continue;
            Sources.push_back(std::move(Source));
        }
    }
    for (const OccluderFace& Face : Context.Faces) TraceSilhouettes(Context, Face, std::max(4, Request.SilhouetteDensity), Sources);

    // ---- ④ visibility, then hatching (caps face the viewer with nothing in front: always visible)
    for (StrokeSource& Source : Sources)
    {
        Source.Low  = { 1.0e300, 1.0e300 };
        Source.High = { -1.0e300, -1.0e300 };
        for (const double Parameter : Source.Shape)
        {
            const Vector2 Point = ProjectDrawingPoint(Context.Frame, Source.Evaluate(Parameter));
            Source.Outline.push_back(Point);
            Source.Low  = { std::min(Source.Low.X, Point.X), std::min(Source.Low.Y, Point.Y) };
            Source.High = { std::max(Source.High.X, Point.X), std::max(Source.High.Y, Point.Y) };
        }
    }
    for (const StrokeSource& Source : Sources) TraceStroke(Context, Source, Sources, View.Strokes);
    if (!Request.HiddenLinesEnabled)
        View.Strokes.erase(std::remove_if(View.Strokes.begin(), View.Strokes.end(), [](const DrawingStroke& Stroke) { return Stroke.Visibility == DrawingVisibility::Hidden; }), View.Strokes.end());
    const double Spacing = Request.HatchSpacing > 0.0 ? Request.HatchSpacing : 0.025 * Context.Reach;
    for (const auto& [Face, Body] : CapFaces)
    {
        HatchCap(Context, Face, Body, Spacing, Request.HatchAngle, Step, View.Strokes);
        View.SectionArea += IntegrateFaceSurface(Work, Face, [](Vector3) { return 1.0; });
    }
    View.SectionCaps      = static_cast<int>(CapFaces.size());
    View.VisibilityProbes = Context.Probes;

    View.Low  = { 1.0e300, 1.0e300 };
    View.High = { -1.0e300, -1.0e300 };
    for (const DrawingStroke& Stroke : View.Strokes)
        for (const Vector2& Point : Stroke.Points) { View.Low = { std::min(View.Low.X, Point.X), std::min(View.Low.Y, Point.Y) }; View.High = { std::max(View.High.X, Point.X), std::max(View.High.Y, Point.Y) }; }
    if (View.Strokes.empty()) { View.Low = {}; View.High = {}; }
    return Finish();
}

} // namespace Frontier::GeometrySpace::Parametric

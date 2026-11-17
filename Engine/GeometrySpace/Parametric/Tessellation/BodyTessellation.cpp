/*====================================================================================================================================
                                                     BODYTESSELLATION.CPP
====================================================================================================================================*/
// 🧩 Watertight body tessellation: one shared sampling per edge, constrained Delaunay refinement per face chart

#include "Engine/GeometrySpace/Parametric/Tessellation/BodyTessellation.h"

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr double ClosureGap       = 1.0e-9;                                              // [-] - Chart gap needing a closure (FaceDomain)
    constexpr int    EdgeDepthLimit   = 30;                                                  // [-] - Bisection depth cap per edge
    constexpr int    ClosureMinimum   = 4;                                                   // [-] - Fewest points on a singular row
    constexpr int    ClosureMaximum   = 4096;                                                 // [-] - Most points on a singular row
    constexpr double RowDensity       = 4.0;                                                 // [-] - Singular-row points per mean boundary spacing
    constexpr double FoldCosine       = 0.25;                                                // [-] - Triangle / surface normal cosine floor

    [[nodiscard]] double ElapsedMilliseconds(Clock::time_point Start)
    {
        return std::chrono::duration<double, std::milli>(Clock::now() - Start).count();
    }

    // Distance from a point to the segment [A, B].
    [[nodiscard]] double SegmentDeviation(Vector3 Point, Vector3 A, Vector3 B)
    {
        const Vector3 Chord  = B - A;                                                        // [len] - Segment vector
        const double  Square = Dot(Chord, Chord);                                            // [len²] - |B − A|²
        const double  λ      = Square > 0.0 ? std::clamp(Dot(Point - A, Chord) / Square, 0.0, 1.0) : 0.0;
        return Distance(Point, A + Chord * λ);
    }

    // 📝 A surface use of an edge: the face surface and the fin's SP-curve (same τ as the edge).
    struct EdgeSurfaceUse
    {
        const SurfaceGeometry* Surface = nullptr;                                            // [-] - Face carrier
        const ParameterCurve*  Curve   = nullptr;                                            // [-] - SP-curve of the fin
    };

    // 💡 Adaptive bisection of the τ interval. A piece is accepted when its chord sag is within ε, its chord is within the
    //    global side limit and within √(8ε/κ) for the peak curvature κ of every adjacent surface at its midpoint (so face
    //    triangles using the piece as a side can meet ε without splitting it), and every fin's chart chord maps to within
    //    ε of the curve (so each face polygon is faithful in its chart).
    class EdgeSampler
    {
    public:
        EdgeSampler(const CurveGeometry& Curve, const std::vector<EdgeSurfaceUse>& Uses, double Sag, double EdgeLimit, int MinimumDepth)
            : Curve(Curve), Uses(Uses), Sag(Sag), EdgeLimit(EdgeLimit), MinimumDepth(MinimumDepth) {}

        void Divide(double Low, double High, Vector3 LowPosition, Vector3 HighPosition, int Depth, std::vector<double>& Parameters)
        {
            const double  Middle         = 0.5 * (Low + High);                               // [-]   - Piece midpoint τ
            const Vector3 MiddlePosition = EvaluateCurve(Curve, Middle).Position;            // [len] - P(τ_mid)
            const double  Chord          = Distance(LowPosition, HighPosition);              // [len] - Chord length
            double        Limit          = EdgeLimit;                                        // [len] - Allowed chord
            bool          Unfaithful     = false;                                            // [-]   - A chart chord strays from the curve
            for (const EdgeSurfaceUse& Use : Uses)
            {
                const Vector2 ChartMiddle = EvaluateParameterCurve(*Use.Curve, Middle).Position;
                const double  κ           = MeasurePeakCurvature(*Use.Surface, ChartMiddle);
                if (κ > 0.0) Limit = std::min(Limit, std::sqrt(8.0 * Sag / κ));
                // Chart fidelity: the surface image of the chart chord's midpoint must stay within ε of the curve midpoint,
                // otherwise the face polygon in the chart misrepresents the edge (SP-curves bend sharply near a pole).
                const Vector2 ChordMiddle = (EvaluateParameterCurve(*Use.Curve, Low).Position + EvaluateParameterCurve(*Use.Curve, High).Position) * 0.5;
                Unfaithful = Unfaithful || Distance(EvaluateSurface(*Use.Surface, ChordMiddle).Position, MiddlePosition) > Sag;
            }
            const bool Split = Depth < MinimumDepth ||
                               (Depth < EdgeDepthLimit && (SegmentDeviation(MiddlePosition, LowPosition, HighPosition) > Sag || Chord > Limit || Unfaithful));
            if (!Split)
            {
                Parameters.push_back(High);
                return;
            }
            Divide(Low, Middle, LowPosition, MiddlePosition, Depth + 1, Parameters);
            Divide(Middle, High, MiddlePosition, HighPosition, Depth + 1, Parameters);
        }

    private:
        const CurveGeometry&               Curve;
        const std::vector<EdgeSurfaceUse>& Uses;
        double                             Sag;
        double                             EdgeLimit;
        int                                MinimumDepth;
    };

    // 📝 A boundary point of a face chart: chart position, complex vertex, and whether it lies on a singular row.
    struct LoopPoint
    {
        Vector2 Chart  = {};                                                                 // [-] - (𝑢, 𝑣)
        int     Vertex = -1;                                                                 // [-] - Complex vertex
    };

    // Circumcentre of a counter-clockwise chart triangle.
    [[nodiscard]] Vector2 Circumcentre(Vector2 A, Vector2 B, Vector2 C)
    {
        const Vector2 AB = B - A, AC = C - A;                                                // [-] - Edge vectors
        const double  Denominator = 2.0 * Cross(AB, AC);                                     // [-] - 4 × signed area
        const double  ABSquare = Dot(AB, AB), ACSquare = Dot(AC, AC);
        return { A.X + (AC.Y * ABSquare - AB.Y * ACSquare) / Denominator, A.Y + (AB.X * ACSquare - AC.X * ABSquare) / Denominator };
    }

    [[nodiscard]] double Circumradius(Vector2 A, Vector2 B, Vector2 C)
    {
        const double Area = 0.5 * std::fabs(Cross(B - A, C - A));                            // [-] - Triangle area
        return Area > 0.0 ? Distance(A, B) * Distance(B, C) * Distance(C, A) / (4.0 * Area) : INFINITY;
    }

    void Accumulate(ChartTriangulationStatistics& Total, const ChartTriangulationStatistics& Part)
    {
        Total.InCircleTests   += Part.InCircleTests;
        Total.LocateSteps     += Part.LocateSteps;
        Total.LocateFallbacks += Part.LocateFallbacks;
        Total.CavityRepairs   += Part.CavityRepairs;
        Total.ConstraintFlips += Part.ConstraintFlips;
        Total.LegalizingFlips += Part.LegalizingFlips;
    }

    // 💡 Per-face state: the chart is metric-scaled by |∂P/∂𝑢|, |∂P/∂𝑣| at the window centre so that Delaunay and the
    //    quality bound act on near-isotropic coordinates instead of raw angles.
    // Undirected side key.
    [[nodiscard]] uint64_t UndirectedSide(int From, int To) noexcept
    {
        const uint32_t Low = static_cast<uint32_t>(std::min(From, To)), High = static_cast<uint32_t>(std::max(From, To));
        return (static_cast<uint64_t>(Low) << 32) | High;
    }

    class FaceTessellator
    {
    public:
        FaceTessellator(const ModelPartition& Partition, FaceToken Face, PolygonComplex& Complex, const std::unordered_map<uint32_t, int>& EdgeIndex,
                        std::unordered_set<uint64_t>& ClaimedSides, const std::unordered_set<uint64_t>& FixedSides, double Sag, double EdgeLimit, double Bound)
            : Partition(Partition), Face(Face), Complex(Complex), EdgeIndex(EdgeIndex), ClaimedSides(ClaimedSides), FixedSides(FixedSides), Sag(Sag), EdgeLimit(EdgeLimit),
              Bound(Bound), Surface(Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface)) {}

        // Returns an empty string on success, else the refusal reason.
        std::string Run(int& Budget, ChartTriangulationStatistics& Totals)
        {
            const Clock::time_point Start = Clock::now();
            Summary.Face = Face;
            std::string Refusal = CollectLoops();
            if (Refusal.empty()) Refusal = Triangulate(Budget);
            if (Triangulation) Accumulate(Totals, Triangulation->Statistics());
            Summary.Milliseconds = ElapsedMilliseconds(Start);
            Complex.Faces.push_back(Summary);
            return Refusal;
        }

    private:
        // ① Loops of chart points from the shared edge samples; singular rows (pole / apex) closed by virtual points.
        std::string CollectLoops()
        {
            struct FinRun
            {
                std::vector<LoopPoint> Points;                                               // [-] - Samples in fin direction
            };
            std::vector<std::vector<FinRun>> LoopRuns;
            const FaceProfile& Profile = Partition.Faces.Resolve(Face);
            for (LoopToken Loop : Profile.Loops)
            {
                if (Partition.Loops.Resolve(Loop).IsolatedVertex.PresentCondition()) continue;
                std::vector<FinRun> Runs;
                std::string         Missing;
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                {
                    const FinProfile& Use = Partition.Fins.Resolve(Fin);
                    const auto Found = EdgeIndex.find(Use.Edge.Slot);
                    if (Found == EdgeIndex.end() || !Use.SurfaceCurve)
                    {
                        Missing = "fin without edge sampling or SP-curve";
                        return;
                    }
                    const PolygonComplexEdge& Samples = Complex.Edges[Found->second];
                    FinRun Run;
                    const int Count = static_cast<int>(Samples.Parameters.size());
                    for (int Step = 0; Step < Count; ++Step)
                    {
                        const int Index = Use.Sense == Orientation::Forward ? Step : Count - 1 - Step;
                        Run.Points.push_back({ EvaluateParameterCurve(*Use.SurfaceCurve, Samples.Parameters[Index]).Position, Samples.Vertices[Index] });
                    }
                    Runs.push_back(std::move(Run));
                });
                if (!Missing.empty()) return Missing;
                if (!Runs.empty()) LoopRuns.push_back(std::move(Runs));
            }
            if (LoopRuns.empty()) return "face has no fin loops";

            // Chart window → triangulation chart (needs only the fin samples; closures stay inside the window).
            Vector2 Low  = LoopRuns.front().front().Points.front().Chart;
            Vector2 High = Low;
            for (const auto& Runs : LoopRuns)
            {
                for (const FinRun& Run : Runs)
                {
                    for (const LoopPoint& Point : Run.Points)
                    {
                        Low  = { std::min(Low.X, Point.Chart.X), std::min(Low.Y, Point.Chart.Y) };
                        High = { std::max(High.X, Point.Chart.X), std::max(High.Y, Point.Chart.Y) };
                    }
                }
            }
            DefineChart(Low, High);

            for (const auto& Runs : LoopRuns)
            {
                // 💡 Singular rows get RowDensity × the mean boundary density. A fan triangle (row point T, X, Y) keeps its 3D
                //    orientation iff 𝑢(T) lies between 𝑢(X) and 𝑢(Y); a dense row makes Delaunay pick such a T. Measured on
                //    the cone at ε = 1e-4: density 1 leaves 6 folded triangles, density 4 leaves none. Row slivers cost
                //    nothing: both of their row corners are the pole vertex, so they are dropped.
                double SpacingTotal = 0.0;                                                   // [-] - Σ spacing
                int    SpacingCount = 0;                                                     // [-] - Spacing samples
                for (const FinRun& Run : Runs)
                {
                    for (size_t Index = 1; Index < Run.Points.size(); ++Index)
                    {
                        SpacingTotal += Distance(Scale(Run.Points[Index].Chart), Scale(Run.Points[Index - 1].Chart));
                        SpacingCount += 1;
                    }
                }
                const double Spacing = SpacingCount > 0 && SpacingTotal > 0.0 ? SpacingTotal / SpacingCount : 1.0;

                std::vector<LoopPoint> Points;
                for (size_t RunIndex = 0; RunIndex < Runs.size(); ++RunIndex)
                {
                    const FinRun& Run  = Runs[RunIndex];
                    const FinRun& Next = Runs[(RunIndex + 1) % Runs.size()];
                    Points.insert(Points.end(), Run.Points.begin(), Run.Points.end() - 1);
                    const LoopPoint Last  = Run.Points.back();
                    const LoopPoint First = Next.Points.front();
                    if (Last.Vertex != First.Vertex) return "consecutive fins do not share a vertex";
                    if (Distance(Last.Chart, First.Chart) <= ClosureGap) continue;

                    // Virtual closure: every point maps to the same vertex, so it must be a singular row of the chart.
                    const Vector3 Pole  = Complex.Positions[Last.Vertex];
                    const Vector3 Probe = EvaluateSurface(Surface, (Last.Chart + First.Chart) * 0.5).Position;
                    const double  Slack = 1.0e-7 * (1.0 + Length(Pole));                     // [len] - Singular-row tolerance
                    if (Distance(Probe, Pole) > Slack) return "virtual closure is not a singular row";
                    if (std::find(SingularVertices.begin(), SingularVertices.end(), Last.Vertex) == SingularVertices.end()) SingularVertices.push_back(Last.Vertex);
                    const double RowLength = Distance(Scale(Last.Chart), Scale(First.Chart)); // [-] - Row length in the triangulation chart
                    const int    Count     = std::clamp(static_cast<int>(std::ceil(RowDensity * RowLength / Spacing)), ClosureMinimum, ClosureMaximum);
                    for (int Step = 0; Step < Count; ++Step)
                    {
                        const double λ = static_cast<double>(Step) / Count;                 // [-] - Fraction along the row
                        Points.push_back({ Last.Chart + (First.Chart - Last.Chart) * λ, Last.Vertex });
                    }
                }
                Loops.push_back(std::move(Points));
            }
            return {};
        }

        // 💡 Triangulation chart: (𝑢, 𝑣) scaled by |∂P/∂𝑢|, |∂P/∂𝑣| at the window centre — a linear map, so chart segments
        //    stay straight and the boundary polygon stays simple. (A sinusoidal map ρ(𝑣)·𝑢 was tried: it shears away from
        //    the centre meridian and halved the torus minimum angle.)
        void DefineChart(Vector2 Low, Vector2 High)
        {
            Centre = (Low + High) * 0.5;
            const SurfaceSample Metric = EvaluateSurface(Surface, Centre);
            double Peak = 0.0;                                                               // [len] - Largest |∂P/∂𝑢| over the window
            for (int Step = 0; Step <= 8; ++Step)
            {
                Peak = std::max(Peak, Length(EvaluateSurface(Surface, { Centre.X, Low.Y + (High.Y - Low.Y) * Step / 8.0 }).DerivativeU));
            }
            SingularFloor = 1.0e-9 * std::max(Peak, 1.0e-300);
            Summary.ScaleU = std::max(Length(Metric.DerivativeU), 1.0e-6 * std::max(Peak, 1.0e-12));
            Summary.ScaleV = std::max(Length(Metric.DerivativeV), 1.0e-12);
        }

        [[nodiscard]] Vector2 Scale(Vector2 Chart) const { return { (Chart.X - Centre.X) * Summary.ScaleU, (Chart.Y - Centre.Y) * Summary.ScaleV }; }
        [[nodiscard]] Vector2 Unscale(Vector2 Scaled) const { return { Centre.X + Scaled.X / Summary.ScaleU, Centre.Y + Scaled.Y / Summary.ScaleV }; }

        // 📝 An emitted triangle in the natural orientation (chart counter-clockwise = ∂P/∂𝑢 × ∂P/∂𝑣 side).
        struct Facet
        {
            std::array<int, 3>     Vertices = {};                                            // [-] - Complex vertices
            std::array<Vector2, 3> Charts   = {};                                            // [-] - Chart per corner
        };

        [[nodiscard]] bool SingularChart(Vector2 Chart) const
        {
            const SurfaceSample Sample = EvaluateSurface(Surface, Chart);
            return Length(Cross(Sample.DerivativeU, Sample.DerivativeV)) <= SingularFloor * Length(Sample.DerivativeV);
        }

        // ⚠️ Fold test. Degenerate: (near) zero area — e.g. two seam samples and an apex-row point, collinear in 3D.
        //    Folded: the normal turns away from the surface normal at a non-singular corner. Refining folded triangles was
        //    tried and made things worse: inserted points crowd the singular row and create new folds (cone and sphere
        //    ran to the triangle limit); folds are repaired by RepairStar instead.
        [[nodiscard]] bool FoldedFacet(const Facet& Candidate) const
        {
            const Vector3 P0 = Complex.Positions[Candidate.Vertices[0]], P1 = Complex.Positions[Candidate.Vertices[1]], P2 = Complex.Positions[Candidate.Vertices[2]];
            const Vector3 Normal    = Cross(P1 - P0, P2 - P0);
            const double  Square    = std::max({ Dot(P1 - P0, P1 - P0), Dot(P2 - P1, P2 - P1), Dot(P0 - P2, P0 - P2) });
            const double  Magnitude = Length(Normal);
            if (Magnitude <= 1.0e-10 * Square) return true;
            for (const Vector2& Chart : Candidate.Charts)
            {
                if (SingularChart(Chart)) continue;
                if (Dot(Normal, EvaluateSurfaceNormal(Surface, Chart)) <= FoldCosine * Magnitude) return true;
            }
            return false;
        }

        // 💡 Star repair at a singular vertex (pole / apex). Its triangles' opposite sides form the link — a cycle when the
        //    seam samples are shared, else a chain. When any star triangle is folded, P and the link are projected along
        //    the mean outward normal (the axis, for a pole or an apex, where the star is faithful) and the polygon is
        //    re-triangulated by the same exact CDT. A non-simple projection makes constraint recovery fail: no change.
        void RepairStar(int Pole, std::vector<Facet>& Facets)
        {
            bool AnyFolded = false;
            for (size_t Index = 0; Index < Facets.size(); ++Index)
            {
                const auto& Vertices = Facets[Index].Vertices;
                if (Vertices[0] != Pole && Vertices[1] != Pole && Vertices[2] != Pole) continue;
                AnyFolded = AnyFolded || FoldedFacet(Facets[Index]);
            }
            if (!AnyFolded) return;

            // ① Link from the CHART: the union of the star's chart triangles is a disk whose boundary (minus singular-row
            //    sides) runs P → X₀ → … → Xₖ → P in chart order even when the 3D fan folds; the vertex-level link of a
            //    folded star can branch (two triangles P, X, ·), so it is not used. Anisotropic slivers next to a dense
            //    singular row can pinch the union at a point or enclose a hole: the region then grows over every triangle
            //    at the offending points until its boundary is one chain, and enclosed vertices become Steiner points.
            const ChartTriangulation& Tiling = *Triangulation;
            const auto OnPole = [&](int Point) { return PointVertex[Point] == Pole; };
            std::vector<char> InRegion(Tiling.Triangles().size(), 0);
            for (size_t Triangle = 0; Triangle < Tiling.Triangles().size(); ++Triangle)
            {
                const ChartTriangle& Owner = Tiling.Triangles()[Triangle];
                if (Owner.Alive && Owner.Interior && (OnPole(Owner.Corners[0]) || OnPole(Owner.Corners[1]) || OnPole(Owner.Corners[2]))) InRegion[Triangle] = 1;
            }
            std::vector<int> ChainPoints;
            for (int Growth = 0;; ++Growth)
            {
                if (Growth == 6) return;
                std::unordered_map<int, int> Successor, Arrivals;                            // [-] - Boundary walk; in-degree
                std::vector<int>             Entries, Exits;                                 // [-] - Points after / before a pole point
                std::vector<int>             Offending;
                for (size_t Triangle = 0; Triangle < Tiling.Triangles().size(); ++Triangle)
                {
                    if (!InRegion[Triangle]) continue;
                    const ChartTriangle& Owner = Tiling.Triangles()[Triangle];
                    for (int Side = 0; Side < 3; ++Side)
                    {
                        const int Across = Owner.Neighbours[Side];
                        if (Across >= 0 && InRegion[Across]) continue;
                        const int From = Owner.Corners[(Side + 1) % 3], To = Owner.Corners[(Side + 2) % 3];
                        if (OnPole(From) && OnPole(To)) continue;                            // Singular-row side (zero length in 3D)
                        if (OnPole(From)) { Entries.push_back(To); continue; }
                        if (OnPole(To)) { Exits.push_back(From); continue; }
                        if (Successor.count(From)) Offending.push_back(From);                // Pinch
                        if (++Arrivals[To] > 1) Offending.push_back(To);
                        Successor[From] = To;
                    }
                }
                if (Entries.size() != 1 || Exits.size() != 1) return;
                if (Offending.empty())
                {
                    ChainPoints = { Entries.front() };
                    while (ChainPoints.back() != Exits.front() && ChainPoints.size() <= Successor.size())
                    {
                        const auto Found = Successor.find(ChainPoints.back());
                        if (Found == Successor.end()) return;
                        ChainPoints.push_back(Found->second);
                    }
                    if (ChainPoints.back() != Exits.front()) return;
                    if (ChainPoints.size() == Successor.size() + 1) break;
                    // Sides left over form hole cycles: absorb them.
                    std::unordered_set<int> Walked(ChainPoints.begin(), ChainPoints.end());
                    for (const auto& [From, To] : Successor) if (!Walked.count(From)) Offending.push_back(From);
                }
                std::unordered_set<int> Absorb(Offending.begin(), Offending.end());
                for (size_t Triangle = 0; Triangle < Tiling.Triangles().size(); ++Triangle)
                {
                    const ChartTriangle& Owner = Tiling.Triangles()[Triangle];
                    if (!Owner.Alive || !Owner.Interior || InRegion[Triangle]) continue;
                    if (Absorb.count(Owner.Corners[0]) || Absorb.count(Owner.Corners[1]) || Absorb.count(Owner.Corners[2])) InRegion[Triangle] = 1;
                }
            }
            std::vector<int>                 Chain;
            std::unordered_map<int, Vector2> ChartOf;
            for (int Point : ChainPoints)
            {
                Chain.push_back(PointVertex[Point]);
                ChartOf.emplace(PointVertex[Point], PointChart[Point]);
            }
            const bool Closed = Chain.size() > 2 && Chain.front() == Chain.back();          // Seam samples shared: the link closes
            if (Closed) Chain.pop_back();
            for (size_t Index = 0; Index < Chain.size(); ++Index)
            {
                if (std::count(Chain.begin(), Chain.end(), Chain[Index]) != 1) return;       // Link touches itself
            }
            // Region triangles (as vertex triples) and enclosed Steiner vertices.
            std::unordered_set<int>      OnChain(Chain.begin(), Chain.end());
            std::vector<int>             Steiner;
            std::set<std::array<int, 3>> Replaced;
            size_t                       Proper = 0;                                         // [-] - Region triangles that were emitted
            for (size_t Triangle = 0; Triangle < Tiling.Triangles().size(); ++Triangle)
            {
                if (!InRegion[Triangle]) continue;
                std::array<int, 3> Triple{};
                for (int Corner = 0; Corner < 3; ++Corner)
                {
                    const int Point = Tiling.Triangles()[Triangle].Corners[Corner];
                    Triple[Corner]  = PointVertex[Point];
                    if (Triple[Corner] < 0) return;                                          // Not yet emitted (cannot happen)
                    if (Triple[Corner] != Pole && !OnChain.count(Triple[Corner]) && !ChartOf.count(Triple[Corner]))
                    {
                        Steiner.push_back(Triple[Corner]);
                        ChartOf.emplace(Triple[Corner], PointChart[Point]);
                    }
                }
                std::sort(Triple.begin(), Triple.end());
                if (Triple[0] != Triple[1] && Triple[1] != Triple[2]) Proper += 1;           // Emitted (two chart triangles may share a triple)
                Replaced.insert(Triple);
            }
            double PoleV = 0.0;                                                              // [-] - 𝑣 of the singular row
            for (size_t Point = 0; Point < PointVertex.size(); ++Point) if (OnPole(static_cast<int>(Point))) { PoleV = PointChart[Point].Y; break; }

            // ② Projection frame from the mean outward (natural) normal at the link.
            Vector3 Mean{};
            for (int Vertex : Chain)
            {
                if (!SingularChart(ChartOf[Vertex])) Mean = Mean + EvaluateSurfaceNormal(Surface, ChartOf[Vertex]);
            }
            if (Length(Mean) < 1.0e-12) return;
            const Vector3 Axis   = Normalize(Mean);
            const Vector3 Hint   = std::fabs(Axis.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
            const Vector3 First  = Normalize(Hint - Axis * Dot(Hint, Axis));
            const Vector3 Second = Cross(Axis, First);
            const Vector3 Origin = Complex.Positions[Pole];
            const auto Project = [&](int Vertex)
            {
                const Vector3 Offset = Complex.Positions[Vertex] - Origin;
                return Vector2{ Dot(Offset, First), Dot(Offset, Second) };
            };

            // ③ CDT of the projected star polygon.
            std::vector<int> Polygon = Closed ? Chain : std::vector<int>{};
            if (!Closed)
            {
                Polygon.push_back(Pole);
                Polygon.insert(Polygon.end(), Chain.begin(), Chain.end());
            }
            Vector2 Low = Project(Pole), High = Low;
            for (int Vertex : Polygon)
            {
                const Vector2 Point = Project(Vertex);
                Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y) };
                High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y) };
            }
            ChartTriangulation Patch(Low, High);
            std::vector<int>   Owner = { -1, -1, -1 };                                       // [-] - Patch point → complex vertex
            std::vector<int>   Ring;
            for (int Vertex : Polygon)
            {
                const int Point = Patch.InsertPoint(Project(Vertex));
                if (Point != static_cast<int>(Owner.size())) return;                         // Coincident projection
                Owner.push_back(Vertex);
                Ring.push_back(Point);
            }
            if (Closed)
            {
                const int Point = Patch.InsertPoint(Project(Pole));
                if (Point != static_cast<int>(Owner.size())) return;
                Owner.push_back(Pole);
            }
            const int PolePoint = Closed ? static_cast<int>(Owner.size()) - 1 : Ring.front();
            for (int Vertex : Steiner)
            {
                const int Point = Patch.InsertPoint(Project(Vertex));
                if (Point != static_cast<int>(Owner.size())) return;
                Owner.push_back(Vertex);
            }
            for (size_t Index = 0; Index < Ring.size(); ++Index)
            {
                if (!Patch.ConstrainSegment(Ring[Index], Ring[(Index + 1) % Ring.size()])) return;
            }
            // P keeps every edge-sample side it owns (a seam inside the face ends at the pole): those are B-rep edges.
            for (const PolygonComplexEdge& Edge : Complex.Edges)
            {
                for (size_t Index = 1; Index < Edge.Vertices.size(); ++Index)
                {
                    const int From = Edge.Vertices[Index - 1], To = Edge.Vertices[Index];
                    const int Other = From == Pole ? To : (To == Pole ? From : -1);
                    if (Other < 0 || Other == Pole) continue;
                    const auto Found = std::find(Owner.begin(), Owner.end(), Other);
                    if (Found == Owner.end()) continue;
                    if (!Patch.ConstrainSegment(PolePoint, static_cast<int>(Found - Owner.begin()))) return;
                }
            }
            Patch.Legalize();
            Patch.ClassifyInterior();
            std::vector<Facet> Fresh;
            for (const ChartTriangle& Triangle : Patch.Triangles())
            {
                if (!Triangle.Alive || !Triangle.Interior) continue;
                Facet Candidate;
                for (int Corner = 0; Corner < 3; ++Corner) Candidate.Vertices[Corner] = Owner[Triangle.Corners[Corner]];
                for (int Corner = 0; Corner < 3; ++Corner)
                {
                    const int Vertex = Candidate.Vertices[Corner];
                    if (Vertex != Pole) Candidate.Charts[Corner] = ChartOf[Vertex];
                }
                for (int Corner = 0; Corner < 3; ++Corner)
                {
                    if (Candidate.Vertices[Corner] != Pole) continue;
                    const Vector2 Left = Candidate.Charts[(Corner + 1) % 3], Right = Candidate.Charts[(Corner + 2) % 3];
                    Candidate.Charts[Corner] = { 0.5 * (Left.X + Right.X), PoleV };
                }
                if (FoldedFacet(Candidate)) return;                                          // No improvement: keep the original
                Fresh.push_back(Candidate);
            }
            // Polygon of n vertices with k enclosed points: n − 2 + 2k triangles (a closed link encloses P as well).
            const size_t Boundary = Closed ? Chain.size() : Chain.size() + 1;
            const size_t Enclosed = Steiner.size() + (Closed ? 1 : 0);
            if (Fresh.size() != Boundary - 2 + 2 * Enclosed) return;

            std::vector<Facet> Kept;
            for (const Facet& Candidate : Facets)
            {
                std::array<int, 3> Triple = Candidate.Vertices;
                std::sort(Triple.begin(), Triple.end());
                if (!Replaced.count(Triple)) Kept.push_back(Candidate);
            }
            if (Facets.size() - Kept.size() != Proper) return;                               // Facets already altered: keep them
            Kept.insert(Kept.end(), Fresh.begin(), Fresh.end());
            Facets = std::move(Kept);
            Summary.StarRepairs += 1;
        }

        // 💡 Folded slivers off the singular vertices (chart-CCW but flipped in 3D next to a pole, where the chart metric
        //    degenerates) are repaired by 3D side flips: a folded facet (a, b, c) and its neighbour (b, a, d) across a side
        //    that is not a B-rep edge sample become (c, a, d) and (d, b, c) when both are unfolded and the diagonal is new.
        void FlipFolded(std::vector<Facet>& Facets)
        {
            const auto Key = [](int From, int To) { return (static_cast<uint64_t>(static_cast<uint32_t>(From)) << 32) | static_cast<uint32_t>(To); };
            std::unordered_map<uint64_t, size_t> Sides;                                      // [-] - Directed side → facet
            const auto Register = [&](size_t Index)
            {
                const auto& Vertices = Facets[Index].Vertices;
                for (int Corner = 0; Corner < 3; ++Corner) Sides[Key(Vertices[Corner], Vertices[(Corner + 1) % 3])] = Index;
            };
            for (size_t Index = 0; Index < Facets.size(); ++Index) Register(Index);
            for (int Sweep = 0; Sweep < 8; ++Sweep)
            {
                bool Changed = false;
                for (size_t Index = 0; Index < Facets.size(); ++Index)
                {
                    if (!FoldedFacet(Facets[Index])) continue;
                    for (int Corner = 0; Corner < 3; ++Corner)
                    {
                        const Facet Folded = Facets[Index];
                        const int   A = Folded.Vertices[Corner], B = Folded.Vertices[(Corner + 1) % 3], C = Folded.Vertices[(Corner + 2) % 3];
                        if (FixedSides.count(UndirectedSide(A, B))) continue;
                        const auto Across = Sides.find(Key(B, A));
                        if (Across == Sides.end() || Across->second == Index) continue;
                        const Facet Opposite = Facets[Across->second];
                        int         Slot     = 0;
                        while (Slot < 3 && Opposite.Vertices[Slot] != B) ++Slot;
                        if (Slot == 3 || Opposite.Vertices[(Slot + 1) % 3] != A) continue;
                        const int D = Opposite.Vertices[(Slot + 2) % 3];
                        if (D == C || Sides.count(Key(C, D)) || Sides.count(Key(D, C))) continue;
                        const Vector2 ChartA = Folded.Charts[Corner], ChartB = Folded.Charts[(Corner + 1) % 3], ChartC = Folded.Charts[(Corner + 2) % 3];
                        const Vector2 ChartD = Opposite.Charts[(Slot + 2) % 3];
                        const Facet   First{ { C, A, D }, { ChartC, ChartA, ChartD } };
                        const Facet   Second{ { D, B, C }, { ChartD, ChartB, ChartC } };
                        if (FoldedFacet(First) || FoldedFacet(Second)) continue;
                        const size_t Other = Across->second;
                        for (int Side = 0; Side < 3; ++Side)
                        {
                            Sides.erase(Key(Folded.Vertices[Side], Folded.Vertices[(Side + 1) % 3]));
                            Sides.erase(Key(Opposite.Vertices[Side], Opposite.Vertices[(Side + 1) % 3]));
                        }
                        Facets[Index] = First;
                        Facets[Other] = Second;
                        Register(Index);
                        Register(Other);
                        Summary.FoldFlips += 1;
                        Changed = true;
                        break;
                    }
                }
                if (!Changed) break;
            }
        }

        // 💡 Two faces can share a whole boundary with no interior points between them — a micro-lens where a sphere cap
        //    pokes less than ε through a plane — and their CDTs may pick the same diagonal: a side used by four triangles.
        //    A side already claimed by an earlier face is split here at the surface image of its chart midpoint, turning
        //    (a, b, c) + (b, a, d) into four triangles around a vertex this face owns. Afterwards the face claims its sides.
        void SplitClaimedSides(std::vector<Facet>& Facets)
        {
            const std::unordered_set<uint64_t>& Fixed      = FixedSides;                     // [-] - Edge-sample sides
            const size_t                        SplitLimit = std::max<size_t>(256, 8 * Facets.size());
            size_t                              Splits     = 0;
            for (bool Changed = true; Changed && Splits < SplitLimit;)
            {
                Changed = false;
                for (size_t Index = 0; Index < Facets.size() && !Changed; ++Index)
                {
                    for (int Corner = 0; Corner < 3 && !Changed; ++Corner)
                    {
                        const Facet Claimed = Facets[Index];
                        const int   A = Claimed.Vertices[Corner], B = Claimed.Vertices[(Corner + 1) % 3], C = Claimed.Vertices[(Corner + 2) % 3];
                        const uint64_t Side = UndirectedSide(A, B);
                        if (Fixed.count(Side) || !ClaimedSides.count(Side)) continue;
                        size_t Other = Facets.size();
                        int    Slot  = 0;
                        for (size_t Search = 0; Search < Facets.size() && Other == Facets.size(); ++Search)
                        {
                            for (Slot = 0; Slot < 3; ++Slot)
                            {
                                if (Facets[Search].Vertices[Slot] == B && Facets[Search].Vertices[(Slot + 1) % 3] == A) { Other = Search; break; }
                            }
                        }
                        if (Other == Facets.size()) continue;
                        const Facet   Opposite = Facets[Other];
                        const int     D        = Opposite.Vertices[(Slot + 2) % 3];
                        const Vector2 ChartA = Claimed.Charts[Corner], ChartB = Claimed.Charts[(Corner + 1) % 3], ChartC = Claimed.Charts[(Corner + 2) % 3];
                        const Vector2 ChartD = Opposite.Charts[(Slot + 2) % 3];
                        const Vector2 ChartM = (ChartA + ChartB) * 0.5;
                        const int     M      = static_cast<int>(Complex.Positions.size());
                        Complex.Positions.push_back(EvaluateSurface(Surface, ChartM).Position);
                        Facets[Index] = Facet{ { A, M, C }, { ChartA, ChartM, ChartC } };
                        Facets[Other] = Facet{ { M, B, C }, { ChartM, ChartB, ChartC } };
                        Facets.push_back(Facet{ { B, M, D }, { ChartB, ChartM, ChartD } });
                        Facets.push_back(Facet{ { M, A, D }, { ChartM, ChartA, ChartD } });
                        Summary.ClaimedSplits += 1;
                        Splits                += 1;
                        Changed = true;
                    }
                }
            }
            for (const Facet& Candidate : Facets)
            {
                for (int Corner = 0; Corner < 3; ++Corner)
                {
                    const uint64_t Side = UndirectedSide(Candidate.Vertices[Corner], Candidate.Vertices[(Corner + 1) % 3]);
                    if (!Fixed.count(Side)) ClaimedSides.insert(Side);
                }
            }
        }

        int AddPoint(Vector2 Scaled, Vector2 Chart, int Vertex, Vector3 Position)
        {
            PointChart.push_back(Chart);
            PointVertex.push_back(Vertex);
            PointPosition.push_back(Position);
            (void)Scaled;
            return static_cast<int>(PointChart.size()) - 1;
        }

        // ② Constrained Delaunay triangulation of the loops, then refinement until sag, side length and quality hold.
        std::string Triangulate(int& Budget)
        {
            Vector2 Low{ INFINITY, INFINITY }, High{ -INFINITY, -INFINITY };
            for (const auto& Loop : Loops)
            {
                for (const LoopPoint& Point : Loop)
                {
                    const Vector2 Scaled = Scale(Point.Chart);
                    Low  = { std::min(Low.X, Scaled.X), std::min(Low.Y, Scaled.Y) };
                    High = { std::max(High.X, Scaled.X), std::max(High.Y, Scaled.Y) };
                }
            }
            Triangulation.emplace(Low, High);
            ChartTriangulation& Tiling = *Triangulation;
            for (int Corner = 0; Corner < 3; ++Corner) AddPoint(Tiling.Points()[Corner], {}, -2, {});

            // Boundary points.
            std::vector<std::vector<int>> LoopIndices;
            for (const auto& Loop : Loops)
            {
                std::vector<int> Indices;
                for (const LoopPoint& Point : Loop)
                {
                    const Vector2 Scaled = Scale(Point.Chart);
                    const int     Index  = Tiling.InsertPoint(Scaled);
                    if (Index < 0) return "boundary point lies on a constrained segment";
                    if (Index == static_cast<int>(PointChart.size())) AddPoint(Scaled, Point.Chart, Point.Vertex, Complex.Positions[Point.Vertex]);
                    else if (PointVertex[Index] != Point.Vertex) return "coincident chart points carry different vertices";
                    Indices.push_back(Index);
                }
                LoopIndices.push_back(std::move(Indices));
                Summary.BoundaryPoints += static_cast<int>(Loop.size());
            }

            // Boundary segments.
            std::vector<double> SegmentLengths;                                              // [-] - Scaled boundary segment lengths
            for (const auto& Indices : LoopIndices)
            {
                for (size_t Index = 0; Index < Indices.size(); ++Index)
                {
                    const int First = Indices[Index], Second = Indices[(Index + 1) % Indices.size()];
                    if (First == Second) continue;
                    if (!Tiling.ConstrainSegment(First, Second)) return "boundary segment crosses another boundary segment";
                    SegmentLengths.push_back(Distance(Tiling.Points()[First], Tiling.Points()[Second]));
                }
            }
            // ⚠️ Quality-only refinement stops below a quarter of the median boundary segment. Gating on the SHORTEST segment
            //    let one tiny chart feature (a section circle bending sharply near a pole) license a Ruppert cascade of
            //    zero-area triangles.
            std::nth_element(SegmentLengths.begin(), SegmentLengths.begin() + SegmentLengths.size() / 2, SegmentLengths.end());
            const double Feature = SegmentLengths.empty() ? 0.0 : 0.25 * SegmentLengths[SegmentLengths.size() / 2]; // [-] - Quality floor
            Tiling.Legalize();
            Tiling.ClassifyInterior();

            // Refinement queue.
            std::deque<int> Pending;
            for (int Triangle = 0; Triangle < static_cast<int>(Tiling.Triangles().size()); ++Triangle)
            {
                if (Tiling.Triangles()[Triangle].Alive && Tiling.Triangles()[Triangle].Interior) Pending.push_back(Triangle);
            }
            const auto Encroaches = [&](const std::vector<int>& Cavity, Vector2 Candidate)
            {
                std::vector<int> Sorted = Cavity;
                std::sort(Sorted.begin(), Sorted.end());
                for (int Triangle : Cavity)
                {
                    const ChartTriangle& Owner = Tiling.Triangles()[Triangle];
                    for (int Side = 0; Side < 3; ++Side)
                    {
                        if (!Owner.Constrained[Side]) continue;
                        if (Owner.Neighbours[Side] >= 0 && std::binary_search(Sorted.begin(), Sorted.end(), Owner.Neighbours[Side])) continue;
                        const Vector2 A = Tiling.Points()[Owner.Corners[(Side + 1) % 3]], B = Tiling.Points()[Owner.Corners[(Side + 2) % 3]];
                        if (Dot(A - Candidate, B - Candidate) < 0.0) return true;
                    }
                }
                return false;
            };
            const auto Prepare = [&](Vector2 Candidate, int& Container, std::vector<int>& Cavity)
            {
                Container = Tiling.LocateTriangle(Candidate);
                if (Container < 0 || !Tiling.Triangles()[Container].Interior) return false;
                for (int Corner : Tiling.Triangles()[Container].Corners)
                {
                    if (Tiling.Points()[Corner].X == Candidate.X && Tiling.Points()[Corner].Y == Candidate.Y) return false;
                }
                Cavity = Tiling.CollectCavity(Candidate, Container);
                return !Cavity.empty();
            };

            while (!Pending.empty())
            {
                const int Triangle = Pending.front();
                Pending.pop_front();
                const ChartTriangle Owner = Tiling.Triangles()[Triangle];
                if (!Owner.Alive || !Owner.Interior) continue;

                // Geometric criteria in 3D: sag (distance to the SURFACE, as audited — the chart lerp is only the projection
                // seed; measuring against S(lerp) adds the boundary chord's parametric distortion, which no split can remove)
                // at the centroid and unconstrained side midpoints, and unconstrained side length.
                const std::array<int, 3>& Corners = Owner.Corners;
                double Deviation = 0.0;                                                      // [len] - Worst measured sag
                double Longest   = 0.0;                                                      // [len] - Longest unconstrained side
                // 📝 Curvature first: on a flat carrier (κ = 0) the sag floor is infinite and the deviation never counts, so
                //    its surface projections (a Newton solve per probe) are skipped.
                const double κ        = MeasurePeakCurvature(Surface, (PointChart[Corners[0]] + PointChart[Corners[1]] + PointChart[Corners[2]]) * (1.0 / 3.0));
                const double SagFloor = κ > 0.0 ? 0.25 * std::sqrt(8.0 * Sag / κ) : INFINITY;    // [len]
                const bool   Measured = SagFloor < INFINITY;
                if (Measured)
                {
                    const Vector2 Chart = (PointChart[Corners[0]] + PointChart[Corners[1]] + PointChart[Corners[2]]) * (1.0 / 3.0);
                    const Vector3 Flat  = (PointPosition[Corners[0]] + PointPosition[Corners[1]] + PointPosition[Corners[2]]) * (1.0 / 3.0);
                    Deviation = Distance(EvaluateSurface(Surface, ProjectOntoSurface(Surface, Flat, Chart)).Position, Flat);
                }
                for (int Side = 0; Side < 3; ++Side)
                {
                    if (Owner.Constrained[Side]) continue;
                    const int A = Corners[(Side + 1) % 3], B = Corners[(Side + 2) % 3];
                    const Vector2 Chart = (PointChart[A] + PointChart[B]) * 0.5;
                    const Vector3 Flat  = (PointPosition[A] + PointPosition[B]) * 0.5;
                    if (Measured) Deviation = std::max(Deviation, Distance(EvaluateSurface(Surface, ProjectOntoSurface(Surface, Flat, Chart)).Position, Flat));
                    Longest   = std::max(Longest, Distance(PointPosition[A], PointPosition[B]));
                }
                // ⚠️ Termination guard: sag counts only when the triangle is large enough for curvature to produce it,
                //    ¼·√(8ε/κ) (below it the true sag is ≤ ε/16), so no residual mismatch can spiral into a segment.
                double Size = 0.0;                                                           // [len] - Longest side, constrained included
                for (int Side = 0; Side < 3; ++Side) Size = std::max(Size, Distance(PointPosition[Corners[Side]], PointPosition[Corners[(Side + 1) % 3]]));
                const bool   Geometric = (Deviation > Sag && Size > SagFloor) || Longest > EdgeLimit;

                // Quality in the scaled chart (only above the boundary feature size, which bounds the point density).
                const Vector2 A = Tiling.Points()[Corners[0]], B = Tiling.Points()[Corners[1]], C = Tiling.Points()[Corners[2]];
                const double Radius   = Circumradius(A, B, C);
                const double Shortest = std::min({ Distance(A, B), Distance(B, C), Distance(C, A) });
                const bool   Poor     = Radius > Bound * Shortest && Radius > Feature;
                if (!Geometric && !Poor) continue;

                int              Container = -1;
                std::vector<int> Cavity;
                Vector2          Candidate = Circumcentre(A, B, C);
                bool             Accepted  = std::isfinite(Candidate.X) && std::isfinite(Candidate.Y) && Prepare(Candidate, Container, Cavity) &&
                                             !Encroaches(Cavity, Candidate);
                if (!Accepted)
                {
                    if (!Geometric)
                    {
                        Summary.QualitySkips += 1;
                        continue;
                    }
                    Candidate = (A + B + C) * (1.0 / 3.0);
                    if (!Prepare(Candidate, Container, Cavity)) continue;
                    Summary.CentroidFallbacks += 1;
                }
                const Vector2 Chart = Unscale(Candidate);
                Tiling.CommitCavity(Candidate, Cavity, Container);
                AddPoint(Candidate, Chart, -1, EvaluateSurface(Surface, Chart).Position);
                Summary.Insertions += 1;
                if (--Budget < 0) return "triangle limit reached";
                for (int Fresh : Tiling.RecentTriangles()) Pending.push_back(Fresh);
                // The bad triangle may survive if it was outside the cavity of a fallback point; revisit it.
                if (Tiling.Triangles()[Triangle].Alive && Tiling.Triangles()[Triangle].Corners == Corners) Pending.push_back(Triangle);
            }

            Summary.DelaunayViolations = Tiling.CountDelaunayViolations();
            if (Summary.DelaunayViolations > 0) Tiling.Legalize();
            PublishTriangles();
            return {};
        }

        // ③ Triangles in the outward sense; interior points become complex vertices; pole / apex slivers collapse; folded
        //    stars at singular vertices are repaired.
        void PublishTriangles()
        {
            const ChartTriangulation& Tiling  = *Triangulation;
            const bool                Flip  = Partition.Faces.Resolve(Face).Sense == Orientation::Reversed;
            const int                 Index = static_cast<int>(Complex.Faces.size());
            std::vector<Facet>        Facets;
            for (const ChartTriangle& Triangle : Tiling.Triangles())
            {
                if (!Triangle.Alive || !Triangle.Interior) continue;
                Facet Candidate;
                for (int Corner = 0; Corner < 3; ++Corner)
                {
                    const int Point = Triangle.Corners[Corner];
                    if (PointVertex[Point] < 0)
                    {
                        PointVertex[Point] = static_cast<int>(Complex.Positions.size());
                        Complex.Positions.push_back(PointPosition[Point]);
                    }
                    Candidate.Vertices[Corner] = PointVertex[Point];
                    Candidate.Charts[Corner]   = PointChart[Point];
                }
                const auto& Vertices = Candidate.Vertices;
                if (Vertices[0] == Vertices[1] || Vertices[1] == Vertices[2] || Vertices[2] == Vertices[0])
                {
                    Summary.SingularDropped += 1;
                    continue;
                }
                Facets.push_back(Candidate);
            }
            for (int Pole : SingularVertices) RepairStar(Pole, Facets);
            FlipFolded(Facets);
            SplitClaimedSides(Facets);

            Summary.FirstTriangle = static_cast<int>(Complex.Triangles.size());
            for (Facet& Candidate : Facets)
            {
                if (FoldedFacet(Candidate)) Summary.FoldedResidual += 1;
                if (Flip)
                {
                    std::swap(Candidate.Vertices[1], Candidate.Vertices[2]);
                    std::swap(Candidate.Charts[1], Candidate.Charts[2]);
                }
                Complex.Triangles.push_back(Candidate.Vertices);
                Complex.TriangleCharts.push_back(Candidate.Charts);
                Complex.TriangleFaces.push_back(Index);
            }
            Summary.TriangleCount = static_cast<int>(Complex.Triangles.size()) - Summary.FirstTriangle;
        }

        const ModelPartition&                   Partition;
        FaceToken                               Face;
        PolygonComplex&                         Complex;
        const std::unordered_map<uint32_t, int>& EdgeIndex;
        std::unordered_set<uint64_t>&           ClaimedSides;
        const std::unordered_set<uint64_t>&     FixedSides;                                  // [-] - Edge-sample sides of the body (undirected)
        double                                  Sag;
        double                                  EdgeLimit;
        double                                  Bound;
        const SurfaceGeometry&                  Surface;
        PolygonComplexFace                      Summary;
        std::vector<std::vector<LoopPoint>>     Loops;
        Vector2                                 Centre;
        double                                  SingularFloor = 0.0;
        std::vector<int>                        SingularVertices;
        std::optional<ChartTriangulation>       Triangulation;
        std::vector<Vector2>                    PointChart;
        std::vector<int>                        PointVertex;
        std::vector<Vector3>                    PointPosition;
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CURVATURE
//------------------------------------------------------------------------------------------------------------------------

double MeasurePeakCurvature(const SurfaceGeometry& Surface, Vector2 Parameter) noexcept
{
    if (const auto* Carrier = std::get_if<RollingBallSurface>(&Surface)) return MeasureRollingBallCurvature(*Carrier, Parameter.X);
    if (const auto* Spline = std::get_if<NurbsSurface>(&Surface)) return MeasureNurbsCurvature(*Spline, Parameter);
    if (const auto* Offset = std::get_if<ParallelSurface>(&Surface)) return MeasureParallelCurvature(*Offset, Parameter);
    if (std::holds_alternative<PlaneSurface>(Surface)) return 0.0;
    if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface)) return 1.0 / Cylinder->Radius;
    if (const auto* Sphere = std::get_if<SphereSurface>(&Surface)) return 1.0 / Sphere->Radius;
    if (const auto* Cone = std::get_if<ConeSurface>(&Surface))
    {
        // Circumferential curvature cos β / ρ with tan β = s; the generator is straight.
        const double ρ = std::fabs(Cone->OriginRadius + Parameter.Y * Cone->RadialSlope);      // [len] - Distance from the axis
        return ρ > 0.0 ? 1.0 / (ρ * std::sqrt(1.0 + Cone->RadialSlope * Cone->RadialSlope)) : INFINITY;
    }
    const auto& Torus = std::get<TorusSurface>(Surface);
    const double Ring = std::fabs(Torus.MajorRadius + Torus.MinorRadius * std::cos(Parameter.Y)); // [len] - Distance from the axis
    const double Hoop = Ring > 0.0 ? std::fabs(std::cos(Parameter.Y)) / Ring : INFINITY;     // [1/len] - Circumferential curvature
    return std::max(1.0 / Torus.MinorRadius, Hoop);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TESSELLATION
//------------------------------------------------------------------------------------------------------------------------

static TessellationOutcome TessellateBodyUntraced(const ModelPartition& Partition, BodyToken Body, const TessellationSettings& Settings)
{
    const Clock::time_point Start = Clock::now();
    TessellationOutcome     Outcome;
    PolygonComplex&         Complex   = Outcome.Complex;
    const BodyInventory     Inventory = Partition.CollectInventory(Body);

    // ① Bounding diagonal from vertices and coarse edge samples sets ε and the side limit.
    Vector3 Low{ INFINITY, INFINITY, INFINITY }, High{ -INFINITY, -INFINITY, -INFINITY };
    const auto Enclose = [&](Vector3 Position)
    {
        Low  = { std::min(Low.X, Position.X), std::min(Low.Y, Position.Y), std::min(Low.Z, Position.Z) };
        High = { std::max(High.X, Position.X), std::max(High.Y, Position.Y), std::max(High.Z, Position.Z) };
    };
    for (VertexToken Vertex : Inventory.Vertices) Enclose(Partition.Vertices.Resolve(Vertex).Position);
    for (EdgeToken Edge : Inventory.Edges)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        for (int Step = 0; Step <= 16; ++Step)
        {
            Enclose(EvaluateCurve(Partition.Curves.Resolve(Profile.Curve), Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Step / 16.0).Position);
        }
    }
    Outcome.Diagonal  = Inventory.Vertices.empty() && Inventory.Edges.empty() ? 1.0 : std::max(Length(High - Low), 1.0e-9);
    Outcome.Sag       = Settings.Sag > 0.0 ? Settings.Sag : Settings.SagRatio * Outcome.Diagonal;
    Outcome.EdgeLimit = Settings.EdgeLengthRatio * Outcome.Diagonal;

    // ② Complex vertices: B-rep vertices first.
    std::unordered_map<uint32_t, int> VertexIndex;
    for (VertexToken Vertex : Inventory.Vertices)
    {
        VertexIndex[Vertex.Slot] = static_cast<int>(Complex.Positions.size());
        Complex.Positions.push_back(Partition.Vertices.Resolve(Vertex).Position);
    }
    Complex.TopologicalVertices = static_cast<int>(Complex.Positions.size());

    // ③ One sampling per edge, shared by every fin of the edge.
    std::unordered_map<uint32_t, int> EdgeIndex;
    for (EdgeToken Edge : Inventory.Edges)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        std::vector<EdgeSurfaceUse> Uses;
        FinToken Fin = Profile.RadialFin;
        for (int Guard = 0; Fin.PresentCondition() && Guard < 64; ++Guard)
        {
            const FinProfile& Use = Partition.Fins.Resolve(Fin);
            if (Use.SurfaceCurve)
            {
                const FaceToken Face = Partition.Loops.Resolve(Use.Loop).Face;
                Uses.push_back({ &Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface), &*Use.SurfaceCurve });
            }
            Fin = Use.RadialSuccessor;
            if (Fin == Profile.RadialFin) break;
        }
        const CurveGeometry& Curve  = Partition.Curves.Resolve(Profile.Curve);
        const bool           Closed = Profile.Start == Profile.End;
        PolygonComplexEdge   Samples;
        Samples.Edge       = Edge;
        Samples.Parameters = { Profile.IntervalLow };
        EdgeSampler Sampler(Curve, Uses, Outcome.Sag, Outcome.EdgeLimit, Closed ? 2 : 0);
        Sampler.Divide(Profile.IntervalLow, Profile.IntervalHigh, EvaluateCurve(Curve, Profile.IntervalLow).Position,
                       EvaluateCurve(Curve, Profile.IntervalHigh).Position, 0, Samples.Parameters);
        for (size_t Index = 0; Index < Samples.Parameters.size(); ++Index)
        {
            if (Index == 0) Samples.Vertices.push_back(VertexIndex[Profile.Start.Slot]);
            else if (Index + 1 == Samples.Parameters.size()) Samples.Vertices.push_back(VertexIndex[Profile.End.Slot]);
            else
            {
                Samples.Vertices.push_back(static_cast<int>(Complex.Positions.size()));
                Complex.Positions.push_back(EvaluateCurve(Curve, Samples.Parameters[Index]).Position);
            }
        }
        EdgeIndex[Edge.Slot] = static_cast<int>(Complex.Edges.size());
        Complex.Edges.push_back(std::move(Samples));
    }

    // ④ Faces. Interior sides are claimed body-wide: a face that reproduces another face's interior side splits it.
    //    The edge-sample sides are fixed for every face: gathered once here, not once per face.
    std::unordered_set<uint64_t> FixedSides;
    for (const PolygonComplexEdge& Edge : Complex.Edges)
    {
        for (size_t Index = 1; Index < Edge.Vertices.size(); ++Index) FixedSides.insert(UndirectedSide(Edge.Vertices[Index - 1], Edge.Vertices[Index]));
    }
    std::unordered_set<uint64_t> ClaimedSides;
    int Budget = Settings.TriangleLimit / 2;                                                 // [-] - Insertions left (≈ 2 triangles each)
    for (FaceToken Face : Inventory.Faces)
    {
        FaceTessellator Tessellator(Partition, Face, Complex, EdgeIndex, ClaimedSides, FixedSides, Outcome.Sag, Outcome.EdgeLimit, Settings.RadiusEdgeBound);
        const std::string Refusal = Tessellator.Run(Budget, Outcome.Triangulation);
        if (!Refusal.empty())
        {
            Outcome.Refusal = "face " + std::to_string(Face.Slot) + ": " + Refusal;
            break;
        }
    }
    Outcome.Milliseconds = ElapsedMilliseconds(Start);
    return Outcome;
}

// 📝 Traced entry (Diagnostics/OperationTrace.h): the request on entry, the outcome before the duration line.

TessellationOutcome TessellateBody(const ModelPartition& Partition, BodyToken Body, const TessellationSettings& Settings)
{
    FRONTIER_TRACE_SCOPE("Tessellate", "TessellateBody · body " + std::to_string(Body.Slot));
    TessellationOutcome Outcome = TessellateBodyUntraced(Partition, Body, Settings);
    FRONTIER_TRACE("Tessellate", Outcome.Refusal.empty() ? std::to_string(Outcome.Complex.Triangles.size()) + " triangles · sag " + TraceNumber(Outcome.Sag)
                                                         : "refused: " + Outcome.Refusal);
    return Outcome;
}


} // namespace Frontier::GeometrySpace::Parametric

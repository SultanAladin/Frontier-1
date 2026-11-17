/*====================================================================================================================================
                                                        EDGEBLEND.CPP
====================================================================================================================================*/
// 🧩 Rolling-ball fillets and chamfers on edge sets: vertex resolution (caps, mitres, ball corners) and direct rebuild

#include "Engine/GeometrySpace/Parametric/Blend/EdgeBlend.h"

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"

#include "Engine/GeometrySpace/Parametric/Blend/FairPatch.h"

#include "Engine/GeometrySpace/Parametric/Construction/BodyAssembly.h"
#include "Engine/GeometrySpace/Parametric/Containment/FaceDomain.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"
#include "Engine/GeometrySpace/Parametric/Intersection/CurveSurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Intersection/IntersectionCurve.h"
#include "Engine/GeometrySpace/Parametric/Intersection/SurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceChange.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <tuple>

namespace Frontier::GeometrySpace::Parametric
{

namespace
{
    //--------------------------------------------------------------------------------------------------------------------
    //                                                STRUCTS
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 A blueprint edge traversed From → To (vertex indices); the use sense follows from the edge's Start.
    struct ChainLink
    {
        int Edge = -1;                                                                       // [-] - BlueprintEdge index
        int From = -1;                                                                       // [-] - BlueprintVertex index
        int To   = -1;                                                                       // [-] - BlueprintVertex index
    };

    struct CornerPatch
    {
        VertexToken            Vertex    = {};                                               // [-] - Source vertex
        SurfaceGeometry        Surface   = PlaneSurface{};                                   // [-] - Corner carrier
        Orientation            Sense     = Orientation::Forward;                             // [-] - Outward vs natural normal
        std::vector<ChainLink> Links     = {};                                               // [-] - Unordered boundary
        Vector2                Reference = {};                                               // [-] - Chart anchor reference
    };

    // Far corner of a support the blend is wider than (cliff overflow), as seen from one blend end.
    struct CliffCorner
    {
        int         Point      = -1;                                                         // [-] - Blend ∩ cliff edge (−1: no overflow)
        FaceToken   Cliff      = {};                                                         // [-] - Face across the far edge
        EdgeToken   FarEdge    = {};                                                         // [-] - Consumed support ∩ cliff face
        EdgeToken   Trimmed    = {};                                                         // [-] - End face ∩ cliff face (absent at a seam)
        int         TrimmedEnd = 0;                                                          // [-] - Its end at the far corner
        bool        NotchCondition = false;                                                  // [-] - Far edge concave: the cliff face grows
        std::string Refusal    = {};                                                         // [-] - Overflow that v1 cannot build
    };

    // One side of a blend that runs off its support: the support is consumed and the cliff face trims the blend.
    struct CliffOverflow
    {
        FaceToken Consumed = {};                                                             // [-] - Support narrower than the blend
        FaceToken Cliff    = {};                                                             // [-] - Trimming face
        EdgeToken FarEdge  = {};                                                             // [-] - Replaced by the cliff section
        int       Ends     = 0;                                                              // [-] - Bit per blend end that saw it
        bool      NotchCondition = false;                                                    // [-] - Concave far edge (notch)
    };

    // One side of a blend whose ball rolls over a tangent far edge onto the next face (smooth overflow).
    struct SmoothOverflow
    {
        FaceToken Consumed = {};                                                             // [-] - Original support, narrower than the blend
        FaceToken Next     = {};                                                             // [-] - Face the ball rolls onto
        EdgeToken FarEdge  = {};                                                             // [-] - Tangent edge, replaced by the contact rail
    };

    using EdgeEndKey    = std::pair<uint32_t, int>;                                          // (edge slot, 0 = Start end / 1 = End end)
    using ContactKey    = std::tuple<uint32_t, int, int>;                                    // (edge slot, side, end)
    using FaceVertexKey = std::pair<uint32_t, uint32_t>;                                     // (face slot, vertex slot)

    //--------------------------------------------------------------------------------------------------------------------
    //                                                REBUILD
    //--------------------------------------------------------------------------------------------------------------------

    class BlendRebuild
    {
    public:
        BlendRebuild(ModelPartition& Partition, BodyToken Body, const BlendSettings& Settings, BlendOutcome& Outcome)
            : Partition(Partition), Body(Body), Settings(Settings), Outcome(Outcome), Inventory(Partition.CollectInventory(Body))
        {
            for (VertexToken Vertex : Inventory.Vertices)
            {
                const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
                Scale = std::max({ Scale, std::fabs(Position.X), std::fabs(Position.Y), std::fabs(Position.Z) });
            }
            Scale     = std::max(Scale, Settings.Size);
            for (double Size : Settings.AnchorSizes) Scale = std::max(Scale, Size);
            Tolerance = 1.0e-9 * Scale;
            Agreement = 1.0e-8 * Scale;
        }

        [[nodiscard]] std::string Run(const std::vector<EdgeToken>& Edges)
        {
            // ① Sheets.
            for (EdgeToken Edge : Edges)
            {
                if (SheetIndex.count(Edge.Slot) != 0) continue;
                const bool Member = std::find(Inventory.Edges.begin(), Inventory.Edges.end(), Edge) != Inventory.Edges.end();
                if (!Member) return "edge " + LabelToken("E", Edge) + " is not in the body";
                EdgeBlendSheetOutcome Resolved = ResolveEdgeBlendSheet(Partition, Edge, Settings);
                if (!Resolved.Sheet) return Resolved.Refusal;
                SheetIndex[Edge.Slot] = static_cast<int>(Outcome.Sheets.size());
                Outcome.Statistics.MaxSheetResidual = std::max(Outcome.Statistics.MaxSheetResidual, Resolved.Sheet->MaxResidual);
                Outcome.Sheets.push_back(*Resolved.Sheet);
            }
            if (Outcome.Sheets.empty()) return "no edges selected";
            Outcome.Statistics.BlendedEdges = static_cast<int>(Outcome.Sheets.size());
            for (EdgeBlendSheet& Sheet : Outcome.Sheets)
            {
                if (std::string Refusal = ResolveSmoothOverflow(Sheet); !Refusal.empty()) return Refusal;
            }

            // ② Vertex resolution, ③ pieces, ④ faces.
            for (VertexToken Vertex : Inventory.Vertices)
            {
                const std::string Refusal = ResolveVertex(Vertex);
                if (!Refusal.empty()) return "vertex " + LabelToken("V", Vertex) + ": " + Refusal;
            }
            if (std::string Refusal = ResolvePieces(); !Refusal.empty()) return Refusal;
            for (const auto& [Key, Smooth] : Smooths) Replacements[Smooth.FarEdge.Slot] = ContactPieces[Key];   // the rail runs where the tangent edge ran
            if (std::string Refusal = ComposeFaces(); !Refusal.empty()) return Refusal;
            if (std::string Refusal = MergeCoincidences(); !Refusal.empty()) return Refusal;
            return {};
        }

        BodyBlueprint                 Blueprint;
        std::vector<BlendFaceHistory> Provenance;

    private:
        ModelPartition&      Partition;
        BodyToken            Body;
        const BlendSettings& Settings;

        // A ball-law sheet is a chamfer on a curved edge or a variable-size blend off the planar line.
        [[nodiscard]] std::string LawSubject() const { return Settings.Profile == BlendProfile::Chamfer ? "curved-edge chamfer" : "variable fillet"; }
        BlendOutcome&        Outcome;
        BodyInventory        Inventory;
        double               Scale     = 1.0;                                                // [len] - Model size
        double               Tolerance = 1.0e-9;                                             // [len] - Coincidence
        double               Agreement = 1.0e-8;                                             // [len] - Corner / mitre agreement

        std::map<uint32_t, int>                    SheetIndex;                               // edge slot → sheet
        std::map<uint32_t, int>                    OriginalVertices;                         // vertex slot → blueprint vertex
        std::map<EdgeEndKey, int>                  TrimVertices;                             // sharp edge end → blueprint vertex
        std::map<ContactKey, int>                  ContactVertices;                          // contact end → blueprint vertex
        std::map<EdgeEndKey, std::vector<ChainLink>> Terminations;                           // blend end, side 0 → side 1
        std::map<FaceVertexKey, int>               CapEdges;                                 // third face corner → cap edge
        std::map<FaceVertexKey, std::vector<ChainLink>> SetbackLinks;                        // face corner → setback conic halves
        std::map<uint32_t, int>                    Pieces;                                   // sharp edge → blueprint edge
        std::map<std::pair<uint32_t, int>, int>    ContactPieces;                            // (edge, side) → blueprint edge
        std::vector<CornerPatch>                   Corners;
        std::set<EdgeEndKey>                       Extensions;                               // sharp edge ends a concave blend may lengthen
        std::map<std::pair<uint32_t, int>, CliffOverflow> Cliffs;                            // (blended edge, side) → cliff overflow
        std::set<uint32_t>                         ConsumedEdges;                            // sharp edges inside a consumed support
        std::set<uint32_t>                         ConsumedFaces;                            // supports narrower than their blend
        std::map<uint32_t, int>                    Replacements;                             // far edge → cliff section (blueprint edge)
        std::set<std::pair<uint32_t, int>>         FlippedContacts;                          // cliff sections running against τ
        std::map<std::pair<uint32_t, int>, SmoothOverflow> Smooths;                          // (blended edge, side) → smooth overflow
        std::set<int>                              Collapsed;                                // zero-length blueprint edges to fuse

        //----------------------------------------------------------------------------------------------------------------
        //                                            BLUEPRINT RECORDS
        //----------------------------------------------------------------------------------------------------------------

        [[nodiscard]] Vector3 PositionOf(int Vertex) const { return Blueprint.Vertices[Vertex].Position; }

        int IssueVertex(Vector3 Position)
        {
            Blueprint.Vertices.push_back({ Position });
            return static_cast<int>(Blueprint.Vertices.size()) - 1;
        }

        int MapOriginalVertex(VertexToken Vertex)
        {
            const auto Found = OriginalVertices.find(Vertex.Slot);
            if (Found != OriginalVertices.end()) return Found->second;
            return OriginalVertices[Vertex.Slot] = IssueVertex(Partition.Vertices.Resolve(Vertex).Position);
        }

        int IssueEdge(const CurveGeometry& Curve, double Low, double High, int Start, int End)
        {
            Blueprint.Edges.push_back({ Curve, Low, High, Start, End });
            if (std::holds_alternative<EllipticalArc>(Curve)) ++Outcome.Statistics.EllipticalEdges;
            return static_cast<int>(Blueprint.Edges.size()) - 1;
        }

        // Straight edge between two blueprint vertices (−1 if they coincide).
        int IssueSegment(int From, int To)
        {
            const Vector3 Start = PositionOf(From), End = PositionOf(To);
            const double  Span  = Distance(Start, End);                                      // [len] - Segment length
            if (Span <= Tolerance) return -1;
            return IssueEdge(StraightLine{ Start, (End - Start) * (1.0 / Span) }, 0.0, Span, From, To);
        }

        // 📝 Shorter arc (≤ π of τ) of a conic between two of its points; −1 if an end is off the carrier.
        int IssueConicArc(const CurveGeometry& Curve, int From, int To)
        {
            const double Start = ProjectOntoCurve(Curve, PositionOf(From), 0.0);             // [-] - τ at From
            const double End   = ProjectOntoCurve(Curve, PositionOf(To), Start);             // [-] - τ at To, folded above Start
            if (Distance(EvaluateCurve(Curve, Start).Position, PositionOf(From)) > Agreement) return -1;
            if (Distance(EvaluateCurve(Curve, End).Position, PositionOf(To)) > Agreement) return -1;
            const double Sweep = End - Start;                                                // [rad] - In [0, 2π)
            if (Sweep <= 1.0e-12 || Sweep >= 2.0 * std::numbers::pi - 1.0e-12) return -1;
            if (Sweep <= std::numbers::pi) return IssueEdge(Curve, Start, End, From, To);
            return IssueEdge(Curve, End, Start + 2.0 * std::numbers::pi, To, From);
        }

        // 💡 Exact section of two carriers between two blueprint vertices lying on both (mitres, caps): two planes → segment;
        //    an analytic SSI branch through both → that line or conic; otherwise the marched branch through both becomes an
        //    exact intersection-curve carrier (Newton onto both surfaces per evaluation) trimmed to them. The SSI window
        //    and step are scaled to the chord, not to the model, so small blends on large parts still resolve. −1 if no
        //    branch passes through both points (e.g. the surfaces are tangent at an end and the march stops short).
        int IssueSection(const SurfaceGeometry& First, const SurfaceGeometry& Second, int From, int To)
        {
            const Vector3 Start = PositionOf(From), End = PositionOf(To);
            const double  Chord = Distance(Start, End);                                      // [len] - Endpoint separation
            if (Chord <= Tolerance) return -1;
            if (std::holds_alternative<PlaneSurface>(First) && std::holds_alternative<PlaneSurface>(Second)) return IssueSegment(From, To);
            const auto Window = [&](const SurfaceGeometry& Surface)
            {
                const Vector2            Near   = ProjectOntoSurface(Surface, Start, {});
                const Vector2            Far    = ProjectOntoSurface(Surface, End, Near);
                const SurfaceSample      Sample = EvaluateSurface(Surface, (Near + Far) * 0.5);
                const SurfacePeriodicity Period = ResolvePeriodicity(Surface);
                double MarginU = 1.5 * Chord / std::max(Length(Sample.DerivativeU), 1.0e-12);  // [-] - Chord in 𝑢 units
                double MarginV = 1.5 * Chord / std::max(Length(Sample.DerivativeV), 1.0e-12);  // [-] - Chord in 𝑣 units
                if (Period.PeriodU > 0.0) MarginU = std::min(MarginU, 0.25 * Period.PeriodU);
                if (Period.PeriodV > 0.0) MarginV = std::min(MarginV, 0.25 * Period.PeriodV);
                ParameterBox Box{ { std::min(Near.X, Far.X) - MarginU, std::min(Near.Y, Far.Y) - MarginV }, { std::max(Near.X, Far.X) + MarginU, std::max(Near.Y, Far.Y) + MarginV } };
                if (std::holds_alternative<SphereSurface>(Surface))
                {
                    Box.Low.Y  = std::max(Box.Low.Y, -0.5 * std::numbers::pi);
                    Box.High.Y = std::min(Box.High.Y, 0.5 * std::numbers::pi);
                }
                else if (const NurbsSurface* Spline = ResolveNurbsChart(Surface))
                {
                    const NurbsDomain DomU = MeasureNurbsSurfaceDomain(*Spline->Definition, true);
                    const NurbsDomain DomV = MeasureNurbsSurfaceDomain(*Spline->Definition, false);
                    if (Period.PeriodU <= 0.0) { Box.Low.X = std::max(Box.Low.X, DomU.Low); Box.High.X = std::min(Box.High.X, DomU.High); }
                    if (Period.PeriodV <= 0.0) { Box.Low.Y = std::max(Box.Low.Y, DomV.Low); Box.High.Y = std::min(Box.High.Y, DomV.High); }
                }
                else if (const auto* Roll = std::get_if<RollingBallSurface>(&Surface))
                {
                    if (Roll->Definition->Period <= 0.0)
                    {
                        if (std::isfinite(Roll->Definition->SpineLow))  Box.Low.X  = std::max(Box.Low.X, Roll->Definition->SpineLow);
                        if (std::isfinite(Roll->Definition->SpineHigh)) Box.High.X = std::min(Box.High.X, Roll->Definition->SpineHigh);
                    }
                }
                return Box;
            };
            const ParameterBox FirstBox  = Window(First);
            const ParameterBox SecondBox = Window(Second);
            for (bool LoopPass : { false, true })
            {
                SurfaceIntersectionSettings Settings;
                Settings.ChordTolerance       = 1.0e-3 * Chord;
                Settings.MaximumStep          = 0.125 * Chord;
                Settings.LoopDetectionEnabled = LoopPass;
                const SurfaceIntersectionOutcome Section = IntersectSurfaces(First, FirstBox, Second, SecondBox, Settings);
                if (Section.CoincidentCondition) return -1;
                for (const IntersectionBranch& Branch : Section.Branches)
                {
                    if (Branch.Category != BranchCategory::Traced)
                    {
                        if (DistanceToCurve(Branch.Curve, Start) > Agreement || DistanceToCurve(Branch.Curve, End) > Agreement) continue;
                        if (std::holds_alternative<StraightLine>(Branch.Curve)) return IssueSegment(From, To);
                        return IssueConicArc(Branch.Curve, From, To);
                    }
                    if (Branch.Chart.size() < 2 || Branch.StartTermination == BranchTermination::Limit || Branch.EndTermination == BranchTermination::Limit) continue;
                    std::vector<Vector3> Guide;
                    for (const IntersectionChartPoint& Point : Branch.Chart) Guide.push_back(Point.Position);
                    // 📝 Where the carriers are tangent (two fillets touching the shared face at M) the march stops just short of
                    //    the point with a singular termination. The endpoint is exact on both surfaces, so it closes the guide:
                    //    the normal-plane Newton at that knot starts on a solution, and acceptance below re-checks every sample.
                    if (!Branch.ClosedCondition)
                    {
                        for (const Vector3& Endpoint : { Start, End })
                        {
                            const double Reach = 0.25 * Chord;                               // [len] - How short a march may stop
                            if (Branch.StartTermination == BranchTermination::Singular && Distance(Guide.front(), Endpoint) > Tolerance && Distance(Guide.front(), Endpoint) < Reach) Guide.insert(Guide.begin(), Endpoint);
                            else if (Branch.EndTermination == BranchTermination::Singular && Distance(Guide.back(), Endpoint) > Tolerance && Distance(Guide.back(), Endpoint) < Reach) Guide.push_back(Endpoint);
                        }
                    }
                    const CurveGeometry Carrier = BuildIntersectionCurve(First, Second, Guide, Branch.ClosedCondition);
                    if (Branch.ClosedCondition) { if (const int Arc = IssueConicArc(Carrier, From, To); Arc >= 0) return Arc; continue; }
                    const auto [Low, High] = MeasureIntersectionCurveRange(*std::get<IntersectionCurve>(Carrier).Trace);
                    const double Near = ProjectOntoCurve(Carrier, Start, Low);               // [-] - τ at From
                    const double Far  = ProjectOntoCurve(Carrier, End, Low);                 // [-] - τ at To
                    if (Distance(EvaluateCurve(Carrier, Near).Position, Start) > Agreement || Distance(EvaluateCurve(Carrier, Far).Position, End) > Agreement) continue;
                    if (std::fabs(Far - Near) <= 1.0e-12) continue;
                    ++Outcome.Statistics.TracedSections;
                    return Near < Far ? IssueEdge(Carrier, Near, Far, From, To) : IssueEdge(Carrier, Far, Near, To, From);
                }
            }
            return -1;
        }

        // Largest distance of an issued edge from a surface over Samples interior points (mitre / cap acceptance).
        [[nodiscard]] double MeasureEdgeDeparture(int Edge, const SurfaceGeometry& Surface, int Samples) const
        {
            const BlueprintEdge& Block = Blueprint.Edges[Edge];
            double Worst = 0.0;                                                              // [len] - Largest departure
            for (int Sample = 1; Sample <= Samples; ++Sample)
            {
                const Vector3 Point = EvaluateCurve(Block.Curve, Block.Low + (Block.High - Block.Low) * Sample / (Samples + 1.0)).Position;
                Worst = std::max(Worst, Distance(ProjectPointOntoSurface(Surface, Point), Point));
            }
            return Worst;
        }

        [[nodiscard]] Orientation LinkSense(const ChainLink& Link) const
        {
            return Blueprint.Edges[Link.Edge].Start == Link.From ? Orientation::Forward : Orientation::Reversed;
        }

        [[nodiscard]] int UseStart(const BlueprintUse& Use) const
        {
            return Use.Sense == Orientation::Forward ? Blueprint.Edges[Use.Edge].Start : Blueprint.Edges[Use.Edge].End;
        }

        [[nodiscard]] int UseEnd(const BlueprintUse& Use) const
        {
            return Use.Sense == Orientation::Forward ? Blueprint.Edges[Use.Edge].End : Blueprint.Edges[Use.Edge].Start;
        }

        //----------------------------------------------------------------------------------------------------------------
        //                                            TOPOLOGY QUERIES
        //----------------------------------------------------------------------------------------------------------------

        [[nodiscard]] FaceToken FaceOfFin(FinToken Fin) const { return Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face; }

        [[nodiscard]] std::pair<FaceToken, FaceToken> FacesOfEdge(EdgeToken Edge) const
        {
            const FinToken Fin = Partition.Edges.Resolve(Edge).RadialFin;
            return { FaceOfFin(Fin), FaceOfFin(Partition.RadialMate(Fin)) };
        }

        [[nodiscard]] const SurfaceGeometry& SurfaceOf(FaceToken Face) const { return Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface); }

        [[nodiscard]] const EdgeBlendSheet* SheetOf(EdgeToken Edge) const
        {
            const auto Found = SheetIndex.find(Edge.Slot);
            return Found == SheetIndex.end() ? nullptr : &Outcome.Sheets[Found->second];
        }

        [[nodiscard]] static int SideOn(const EdgeBlendSheet& Sheet, FaceToken Face) { return Sheet.Faces[0] == Face ? 0 : (Sheet.Faces[1] == Face ? 1 : -1); }

        // Side of a support, including an original support consumed by smooth overflow (−1 otherwise).
        [[nodiscard]] int SupportSide(const EdgeBlendSheet& Sheet, FaceToken Face) const
        {
            const int Side = SideOn(Sheet, Face);
            if (Side >= 0) return Side;
            for (int Other = 0; Other < 2; ++Other)
            {
                const auto Found = Smooths.find({ Sheet.Edge.Slot, Other });
                if (Found != Smooths.end() && Found->second.Consumed == Face) return Other;
            }
            return -1;
        }

        [[nodiscard]] const SmoothOverflow* SmoothOf(const EdgeBlendSheet& Sheet, int Side) const
        {
            const auto Found = Smooths.find({ Sheet.Edge.Slot, Side });
            return Found != Smooths.end() ? &Found->second : nullptr;
        }

        // 📝 Smooth overflow (Parasolid): a support that is a strip of 4 edges whose far edge is tangent (G1) to the next
        //    face, with the ball's foot at or past that edge at both blend ends. The ball rolls over it: the sheet is
        //    re-solved with the next face as its support, the strip and its two end edges are consumed, and the tangent
        //    far edge is replaced by the new contact rail, so the blend meets the next face G1 exactly. Closed edges, one-
        //    ended (partial) and two-sided overflows are named and refused.
        [[nodiscard]] std::string ResolveSmoothOverflow(EdgeBlendSheet& Sheet)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Sheet.Edge);
            const std::string  Subject = "edge " + LabelToken("E", Sheet.Edge) + ": ";
            for (int Side = 0; Side < 2; ++Side)
            {
                const FaceToken    Support = Sheet.Faces[Side];
                const FaceProfile& Face    = Partition.Faces.Resolve(Support);
                if (Face.Loops.size() != 1) continue;
                std::vector<EdgeToken> Boundary;
                Partition.EnumerateLoopFins(Face.Loops.front(), [&](FinToken Fin) { Boundary.push_back(Partition.Fins.Resolve(Fin).Edge); });
                if (Boundary.size() != 4 || Profile.Start == Profile.End) continue;
                EdgeToken Far = {}, Ends[2] = {};
                for (EdgeToken Edge : Boundary)
                {
                    if (Edge == Sheet.Edge) continue;
                    const EdgeProfile& Other   = Partition.Edges.Resolve(Edge);
                    const bool         AtStart = Other.Start == Profile.Start || Other.End == Profile.Start;
                    const bool         AtEnd   = Other.Start == Profile.End || Other.End == Profile.End;
                    if (AtStart && !AtEnd) Ends[0] = Edge;
                    else if (AtEnd && !AtStart) Ends[1] = Edge;
                    else if (!AtStart && !AtEnd) Far = Edge;
                }
                if (!Far.PresentCondition() || !Ends[0].PresentCondition() || !Ends[1].PresentCondition() || !SmoothEdgeCondition(Partition, Far)) continue;
                if (Sheet.VariableCondition()) return Subject + "variable size beside a tangent far edge (smooth overflow, 🚧)";
                if (Sheet.LawCondition()) return Subject + LawSubject() + " beside a tangent far edge (smooth overflow, 🚧)";
                int Over = 0;                                                                // [-] - Blend ends past the far edge
                for (int End = 0; End < 2; ++End)
                {
                    const VertexToken  Vertex = End == 0 ? Profile.Start : Profile.End;
                    const EdgeProfile& Sharp  = Partition.Edges.Resolve(Ends[End]);
                    if (!std::holds_alternative<StraightLine>(Partition.Curves.Resolve(Sharp.Curve))) return Subject + "smooth overflow past a curved end edge (🚧)";
                    const Vector3 Near        = Partition.Vertices.Resolve(Vertex).Position;
                    const Vector3 FarPosition = Partition.Vertices.Resolve(Sharp.Start == Vertex ? Sharp.End : Sharp.Start).Position;
                    const double  Span        = Distance(Near, FarPosition);                // [len] - End edge length
                    const Vector3 Foot        = EvaluateCurve(Sheet.Contacts[Side], ProjectOntoCurve(Sheet.Spine, Near, 0.0)).Position;
                    if (Dot(Foot - Near, FarPosition - Near) / Span >= Span - Agreement) ++Over;
                }
                if (Over == 0) continue;
                if (Over == 1) return Subject + "blend wider than its face at one end only (partial smooth overflow, 🚧)";
                if (SmoothOf(Sheet, 1 - Side)) return Subject + "smooth overflow on both sides (🚧)";
                const auto [FaceA, FaceB] = FacesOfEdge(Far);
                const FaceToken Next = FaceA == Support ? FaceB : FaceA;
                if (Next == Sheet.Faces[1 - Side]) return Subject + "smooth overflow onto the other support (🚧)";
                EdgeBlendSheetOutcome Resolved = ResolveEdgeBlendSheet(Partition, Sheet.Edge, Settings, { Side, Next });
                if (!Resolved.Sheet) return "smooth overflow: " + Resolved.Refusal;
                Sheet = *Resolved.Sheet;
                Outcome.Statistics.MaxSheetResidual = std::max(Outcome.Statistics.MaxSheetResidual, Sheet.MaxResidual);
                Smooths[{ Sheet.Edge.Slot, Side }] = { Support, Next, Far };
                ConsumedFaces.insert(Support.Slot);
                ConsumedEdges.insert(Ends[0].Slot);
                ConsumedEdges.insert(Ends[1].Slot);
                ++Outcome.Statistics.SmoothOverflows;
            }
            return {};
        }

        [[nodiscard]] int EndAt(EdgeToken Edge, VertexToken Vertex) const { return Partition.Edges.Resolve(Edge).Start == Vertex ? 0 : 1; }

        // Point where a curve meets a surface nearest a vertex (lines searched within Reach of it, conics over a turn).
        [[nodiscard]] std::optional<Vector3> CrossCurveSurface(const CurveGeometry& Curve, Vector3 Near, const SurfaceGeometry& Surface) const
        {
            double Low = 0.0, High = 2.0 * std::numbers::pi;                                 // [-] - Search window
            if (!PeriodicCondition(Curve))
            {
                const double Centre = ProjectOntoCurve(Curve, Near, 0.0);                     // [-] - τ nearest the vertex
                Low  = Centre - 4.0 * Scale;
                High = Centre + 4.0 * Scale;
                // An open traced or contact carrier exists only over its knots (a ball-law spine past the edge ends is
                // extended by a margin, not indefinitely): keep the window inside its range.
                const CarrierRange Range = MeasureCarrierRange(Curve);
                if (std::isfinite(Range.Low))  Low  = std::max(Low, Range.Low);
                if (std::isfinite(Range.High)) High = std::min(High, Range.High);
            }
            const CurveSurfaceOutcome Crossing = IntersectCurveSurface(Curve, Low, High, Surface);
            std::optional<Vector3> Best;
            for (const CurveSurfaceCrossing& Point : Crossing.Crossings)
            {
                if (!Best || Distance(Point.Position, Near) < Distance(*Best, Near)) Best = Point.Position;
            }
            return Best;
        }

        // ⚠️ Point where a curve meets a FACE nearest a vertex: crossings on the carrier outside the face's loops are passed over.
        //    A contact line crossing a cylindrical third face (a fillet beside an earlier fillet) meets the carrier twice,
        //    symmetric about the vertex; nearest-only picked the root on the far side of the axis by rounding, and the sharp
        //    edge was then trimmed past its end (beside a 0.4 fillet, r 0.01 / 0.03 / 0.05 / 0.12 / 0.3 were refused as
        //    "consumed by the blend" while their neighbours built). Falls back to the nearest carrier crossing when none
        //    lies on the face.
        [[nodiscard]] std::optional<Vector3> CrossCurveFace(const CurveGeometry& Curve, Vector3 Near, FaceToken Face) const
        {
            double Low = 0.0, High = 2.0 * std::numbers::pi;                                 // [-] - Search window (as CrossCurveSurface)
            if (!PeriodicCondition(Curve))
            {
                const double Centre = ProjectOntoCurve(Curve, Near, 0.0);
                Low  = Centre - 4.0 * Scale;
                High = Centre + 4.0 * Scale;
                const CarrierRange Range = MeasureCarrierRange(Curve);
                if (std::isfinite(Range.Low))  Low  = std::max(Low, Range.Low);
                if (std::isfinite(Range.High)) High = std::min(High, Range.High);
            }
            const CurveSurfaceOutcome Crossing = IntersectCurveSurface(Curve, Low, High, SurfaceOf(Face));
            if (Crossing.Crossings.size() < 2) return Crossing.Crossings.empty() ? std::nullopt : std::optional<Vector3>(Crossing.Crossings.front().Position);
            const FaceDomain       Domain = DescribeFaceDomain(Partition, Face);
            std::optional<Vector3> Best, BestOnFace;
            for (const CurveSurfaceCrossing& Point : Crossing.Crossings)
            {
                if (!Best || Distance(Point.Position, Near) < Distance(*Best, Near)) Best = Point.Position;
                if (ClassifyFacePosition(Domain, Point.Position, Agreement).Membership == PointMembership::Outside) continue;
                if (!BestOnFace || Distance(Point.Position, Near) < Distance(*BestOnFace, Near)) BestOnFace = Point.Position;
            }
            return BestOnFace ? BestOnFace : Best;
        }

        // Closest approach of two straight contacts on one face (chamfer mitres and apexes).
        [[nodiscard]] std::optional<Vector3> MeetLines(const CurveGeometry& First, const CurveGeometry& Second)
        {
            const auto* LineA = std::get_if<StraightLine>(&First);
            const auto* LineB = std::get_if<StraightLine>(&Second);
            if (!LineA || !LineB) return std::nullopt;
            const Vector3 Offset      = LineA->Origin - LineB->Origin;                       // [len] - o₁ − o₂
            const double  Cosine      = Dot(LineA->Direction, LineB->Direction);             // [-]   - d₁·d₂
            const double  Denominator = 1.0 - Cosine * Cosine;                               // [-]   - sin²
            if (Denominator < 1.0e-20) return std::nullopt;
            const double  ParameterA = (Cosine * Dot(LineB->Direction, Offset) - Dot(LineA->Direction, Offset)) / Denominator;
            const double  ParameterB = Dot(LineB->Direction, Offset) + ParameterA * Cosine;
            const Vector3 PointA     = LineA->Origin + LineA->Direction * ParameterA;
            const Vector3 PointB     = LineB->Origin + LineB->Direction * ParameterB;
            Outcome.Statistics.MaxVertexResidual = std::max(Outcome.Statistics.MaxVertexResidual, Distance(PointA, PointB));
            if (Distance(PointA, PointB) > Agreement) return std::nullopt;
            return (PointA + PointB) * 0.5;
        }

        [[nodiscard]] double DistanceToCurve(const CurveGeometry& Curve, Vector3 Point) const
        {
            return Distance(Point, EvaluateCurve(Curve, ProjectOntoCurve(Curve, Point, 0.0)).Position);
        }

        bool Agree(double Gap)
        {
            Outcome.Statistics.MaxVertexResidual = std::max(Outcome.Statistics.MaxVertexResidual, Gap);
            return Gap <= Agreement;
        }

        //----------------------------------------------------------------------------------------------------------------
        //                                            OVERFLOW
        //----------------------------------------------------------------------------------------------------------------

        // Convex edge: the outward normals turn the same way as the sheet convention (N₀ × N₁ · T > 0, T along fin 0).
        [[nodiscard]] bool ConvexEdge(EdgeToken Edge) const
        {
            const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
            const CurveSample    Sample  = EvaluateCurve(Partition.Curves.Resolve(Profile.Curve), 0.5 * (Profile.IntervalLow + Profile.IntervalHigh));
            const FinToken       Fin     = Profile.RadialFin;
            const Vector3        NormalA = EvaluateOutwardNormal(Partition, FaceOfFin(Fin), Sample.Position);
            const Vector3        NormalB = EvaluateOutwardNormal(Partition, FaceOfFin(Partition.RadialMate(Fin)), Sample.Position);
            return Dot(Cross(NormalA, NormalB), Normalize(Sample.Derivative) * (Partition.Fins.Resolve(Fin).Sense == Orientation::Forward ? 1.0 : -1.0)) > 0.0;
        }

        // 📝 Cliff overflow (Parasolid's term): the foot of the blend on a support lies at or past the far end of the
        //    sharp edge it trims, so the blend is wider than that support. v1 builds it when the support is a strip of
        //    4 edges (blended edge, two end edges or one seam, far edge), the far corner has degree 3 with no blend, and
        //    the far edge is convex: the support is consumed, and the face across the far edge (the cliff) trims the
        //    blend. Point = where the blend meets the cliff along the end face's edge (Section == nullptr) or along the
        //    blend's cross-section at a seam. A concave far edge is a notch (built the same way, the cliff face grows).
        //    Concave-blend, smooth, partial and blend-on-blend overflows are named and refused;
        //    other shapes return Unmatched, so the trim step reports the consumed edge as before.
        [[nodiscard]] CliffCorner DetectCliff(const EdgeBlendSheet& Sheet, VertexToken Vertex, EdgeToken Sharp, FaceToken Support, Vector3 Foot, const CurveGeometry* Section)
        {
            CliffCorner        Corner;
            const EdgeProfile& Profile = Partition.Edges.Resolve(Sharp);
            if (!std::holds_alternative<StraightLine>(Partition.Curves.Resolve(Profile.Curve))) return Corner;
            const VertexToken Far         = Profile.Start == Vertex ? Profile.End : Profile.Start;
            const Vector3     Near        = Partition.Vertices.Resolve(Vertex).Position;
            const Vector3     FarPosition = Partition.Vertices.Resolve(Far).Position;
            const double      Span        = Distance(Near, FarPosition);                     // [len] - Sharp edge length
            if (Span <= Tolerance || Far == Vertex) return Corner;
            const double Reach = Dot(Foot - Near, FarPosition - Near) / Span;                // [len] - Foot along the edge
            if (Reach < Span - Agreement) return Corner;
            const auto Refuse = [&](const std::string& Reason) { Corner.Refusal = "blend wider than its face: " + Reason + " (🚧)"; return Corner; };
            const CliffCorner Unmatched;                                                     // not a v1 cliff: the trim refusal names it
            if (!Sheet.ConvexCondition) return Refuse("concave blend overflow");
            std::vector<std::pair<EdgeToken, int>> Around;
            for (EdgeToken Edge : Inventory.Edges)
            {
                const EdgeProfile& Other = Partition.Edges.Resolve(Edge);
                if (Other.Start == Far) Around.push_back({ Edge, 0 });
                if (Other.End == Far)   Around.push_back({ Edge, 1 });
            }
            if (Around.size() != 3) return Unmatched;
            for (const auto& [Edge, EndIndex] : Around)
            {
                if (SheetOf(Edge)) return Refuse("it runs onto another blended edge at the far corner");
                if (Edge == Sharp) continue;
                const auto [FaceA, FaceB] = FacesOfEdge(Edge);
                if (FaceA == Support || FaceB == Support)
                {
                    if (FaceA == FaceB) return Unmatched;
                    Corner.FarEdge = Edge;
                    Corner.Cliff   = FaceA == Support ? FaceB : FaceA;
                }
                else
                {
                    Corner.Trimmed    = Edge;
                    Corner.TrimmedEnd = EndIndex;
                }
            }
            if (!Corner.FarEdge.PresentCondition()) return Unmatched;
            if (Corner.Cliff == Sheet.Faces[0] || Corner.Cliff == Sheet.Faces[1]) return Refuse("wider than both supports");
            if (!Section && !Corner.Trimmed.PresentCondition()) return Unmatched;
            const FaceProfile& Consumed = Partition.Faces.Resolve(Support);
            if (Consumed.Loops.size() != 1) return Unmatched;
            int  Fins    = 0;                                                                // [-] - Support boundary size
            bool FarSeen = false;                                                            // [-] - Far edge on that boundary
            Partition.EnumerateLoopFins(Consumed.Loops.front(), [&](FinToken Fin) { ++Fins; FarSeen = FarSeen || Partition.Fins.Resolve(Fin).Edge == Corner.FarEdge; });
            if (Fins != 4 || !FarSeen) return Unmatched;
            // 💡 Notch overflow (Parasolid): the far edge is concave, so the face across it lies on the material side. The
            //    construction is the cliff's: that face trims the blend, but its edge to the end face now grows past the
            //    far corner instead of shrinking (the section point lies beyond it), and the face extends under the blend.
            if (SmoothEdgeCondition(Partition, Corner.FarEdge)) return Refuse("smooth overflow, the far edge is tangent");
            Corner.NotchCondition = !ConvexEdge(Corner.FarEdge);
            std::optional<Vector3> Point;
            if (Section) Point = CrossCurveSurface(*Section, FarPosition, SurfaceOf(Corner.Cliff));
            else
            {
                const EdgeProfile&   Trimmed = Partition.Edges.Resolve(Corner.Trimmed);
                const CurveGeometry& Curve   = Partition.Curves.Resolve(Trimmed.Curve);
                Point = CrossCurveSurface(Curve, FarPosition, Sheet.Surface);
                if (Point)
                {
                    const double τ = ProjectOntoCurve(Curve, *Point, Trimmed.IntervalLow);
                    const double Slack = Agreement / std::max(MeasureParameterScale(Curve), 1.0e-300);   // [-] - Agreement in τ
                    const bool Inside = τ >= Trimmed.IntervalLow - Slack && τ <= Trimmed.IntervalHigh + Slack;   // [-] - On the existing edge
                    const bool Beyond = Corner.TrimmedEnd == 0 ? τ <= Trimmed.IntervalLow + Slack : τ >= Trimmed.IntervalHigh - Slack;   // [-] - Past the far corner
                    if (!Agree(DistanceToCurve(Curve, *Point))) return Refuse("the blend misses the cliff face's edge");
                    if (!Corner.NotchCondition && !Inside) return Refuse("it also runs past the cliff face's edge");
                    if (Corner.NotchCondition && !Beyond) return Refuse("notch section falls short of the far corner");
                }
            }
            if (!Point) return Refuse("the blend misses the cliff face");
            Corner.Point = IssueVertex(*Point);
            return Corner;
        }

        [[nodiscard]] bool RecordCliff(EdgeToken Edge, int End, int Side, EdgeToken Sharp, FaceToken Support, const CliffCorner& Corner)
        {
            CliffOverflow& Overflow = Cliffs[{ Edge.Slot, Side }];
            if (Overflow.Ends != 0 && (Overflow.Consumed != Support || Overflow.Cliff != Corner.Cliff || Overflow.FarEdge != Corner.FarEdge)) return false;
            Overflow.Consumed = Support;
            Overflow.Cliff    = Corner.Cliff;
            Overflow.FarEdge  = Corner.FarEdge;
            Overflow.NotchCondition = Corner.NotchCondition;
            Overflow.Ends    |= 1 << End;
            ConsumedEdges.insert(Sharp.Slot);
            ConsumedFaces.insert(Support.Slot);
            return true;
        }

        [[nodiscard]] static std::optional<OrthonormalFrame> AxisOf(const SurfaceGeometry& Surface)
        {
            if (const auto* Torus = std::get_if<TorusSurface>(&Surface))       return Torus->Frame;
            if (const auto* Cone = std::get_if<ConeSurface>(&Surface))         return Cone->Frame;
            if (const auto* Cylinder = std::get_if<CylinderSurface>(&Surface)) return Cylinder->Frame;
            return std::nullopt;
        }

        // 📝 The blend side trimmed by the cliff: blend ∩ cliff face between the two cliff points (open edges), or the
        //    full circle through the single point when a rim blend of revolution meets a plane square to its axis.
        [[nodiscard]] std::string IssueCliffSection(const EdgeBlendSheet& Sheet, EdgeToken Edge, int Side, const CliffOverflow& Overflow, bool Closed)
        {
            const std::string Subject = "edge " + LabelToken("E", Edge) + ": ";
            if (Overflow.Ends != 3) return Subject + "blend wider than its face at one end only (partial overflow, 🚧)";
            const int              From  = ContactVertices[{ Edge.Slot, Side, 0 }];
            const int              To    = ContactVertices[{ Edge.Slot, Side, 1 }];
            const SurfaceGeometry& Cliff = SurfaceOf(Overflow.Cliff);
            int Section = -1;
            if (Closed)
            {
                const auto*                     Plane = std::get_if<PlaneSurface>(&Cliff);
                const std::optional<OrthonormalFrame> Axis = AxisOf(Sheet.Surface);
                if (!Plane || !Axis || Length(Cross(Plane->Frame.ZAxis, Axis->ZAxis)) > 1.0e-12) return Subject + "closed cliff section off a plane square to the blend axis (🚧)";
                const Vector3 Point  = PositionOf(From);
                const Vector3 Centre = Axis->Origin + Axis->ZAxis * Dot(Point - Axis->Origin, Axis->ZAxis);
                const double  Radius = Distance(Point, Centre);                             // [len] - Section circle radius
                if (Radius <= Tolerance) return Subject + "cliff section collapses onto the blend axis";
                Section = IssueEdge(CircularArc{ ConstructFrame(Centre, Axis->ZAxis, Point - Centre), Radius }, 0.0, 2.0 * std::numbers::pi, From, From);
            }
            else Section = IssueSection(Sheet.Surface, Cliff, From, To);
            if (Section < 0) return Subject + "no exact section of the " + SurfaceCategoryLabel(Sheet.Surface) + " blend and the cliff face";
            if (!Agree(MeasureEdgeDeparture(Section, Sheet.Surface, 9)) || !Agree(MeasureEdgeDeparture(Section, Cliff, 9))) return Subject + "cliff section leaves the blend or the cliff face";
            const BlueprintEdge& Block  = Blueprint.Edges[Section];
            const CurveSample    Middle = EvaluateCurve(Block.Curve, 0.5 * (Block.Low + Block.High));
            const double         τ      = ProjectOntoCurve(Sheet.Contacts[Side], Middle.Position, 0.0);
            if (Dot(Middle.Derivative, EvaluateCurve(Sheet.Contacts[Side], τ).Derivative) < 0.0) FlippedContacts.insert({ Edge.Slot, Side });
            ContactPieces[{ Edge.Slot, Side }] = Section;
            Replacements[Overflow.FarEdge.Slot] = Section;
            ++(Overflow.NotchCondition ? Outcome.Statistics.NotchOverflows : Outcome.Statistics.CliffOverflows);
            return {};
        }

        //----------------------------------------------------------------------------------------------------------------
        //                                            VERTEX RESOLUTION
        //----------------------------------------------------------------------------------------------------------------

        [[nodiscard]] std::string ResolveVertex(VertexToken Vertex)
        {
            std::vector<std::pair<EdgeToken, int>> Ends, Blended, Sharp;
            for (EdgeToken Edge : Inventory.Edges)
            {
                const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
                if (Profile.Start == Vertex) Ends.push_back({ Edge, 0 });
                if (Profile.End == Vertex)   Ends.push_back({ Edge, 1 });
            }
            for (const auto& End : Ends) (SheetOf(End.first) ? Blended : Sharp).push_back(End);
            if (Blended.empty()) return {};
            // ⚠️ A variable sheet ends in a plain cap or a mitre with one other blend at a degree-3 vertex (its contacts do not
            //    share the spine's τ, see VariableCondition, so the mitre takes the general carrier section).
            const bool MitrePair = Blended.size() == 2 && Blended[0].first != Blended[1].first && Ends.size() == 3;   // [-] - Two blends + one sharp edge
            for (const auto& End : Blended)
            {
                if (SheetOf(End.first)->VariableCondition() && !((Blended.size() == 1 && Ends.size() == 3) || MitrePair))
                    return "variable size where three blends meet or at a vertex of degree ≠ 3 (🚧)";
                if (!SheetOf(End.first)->LawCondition()) continue;
                // 📝 A ball-law sheet ends in a cap, runs through a seam (continuation) or meets its own curve's next edge at
                //    a seams-only vertex (the law is geometric, so both sheets share the section ball); elsewhere it refuses.
                const bool Continuation = Blended.size() == 2 && Blended[0].first == Blended[1].first;   // [-] - Closed rim through a seam
                bool SeamsOnly = Blended.size() == 2 && !Continuation && !Sharp.empty();             // [-] - Every sharp edge is a seam
                for (const auto& Other : Sharp) { const auto Faces = FacesOfEdge(Other.first); SeamsOnly = SeamsOnly && Faces.first == Faces.second; }
                if (!SeamsOnly && (!(Blended.size() == 1 || Continuation) || Ends.size() != 3)) return LawSubject() + " where blends meet or at a vertex of degree ≠ 3 (🚧)";
            }
            const bool Convex = SheetOf(Blended.front().first)->ConvexCondition;
            for (const auto& End : Blended)
            {
                if (SheetOf(End.first)->ConvexCondition == Convex) continue;
                if (Sharp.empty()) return ResolveSetback(Vertex, Ends);
                return "convex and concave blends meet beside a sharp edge (🚧)";
            }
            if (Blended.size() == 2 && Blended[0].first != Blended[1].first && !Sharp.empty())
            {
                bool SeamsOnly = true;                                                       // [-] - Every other edge is a seam
                for (const auto& End : Sharp) { const auto Faces = FacesOfEdge(End.first); SeamsOnly = SeamsOnly && Faces.first == Faces.second; }
                if (SeamsOnly) return ResolveSeamCrossing(Vertex, Blended[0], Blended[1], Sharp);
            }
            if (ResolvePinch(Vertex, Blended)) return {};
            if (Ends.size() != 3)
            {
                if (Sharp.empty()) return ResolveSetback(Vertex, Ends);
                return "degree " + std::to_string(Ends.size()) + " vertex with blended and sharp edges (🚧)";
            }
            if (Blended.size() == 2 && Blended[0].first == Blended[1].first) return ResolveContinuation(Vertex, Blended[0].first, Sharp[0].first);
            if (Blended.size() == 1) return ResolveCap(Vertex, Blended[0], Sharp[0].first, Sharp[1].first);
            if (Blended.size() == 2) return ResolveMitre(Vertex, Blended[0], Blended[1], Sharp[0].first);
            return ResolveBallCorner(Vertex, Blended);
        }

        // 📝 Pinch: where the two supports of a blended edge are tangent (a singular point of their intersection, e.g. equal
        //    crossing bores), the dihedral flattens, the setback r·tan(θ/2) vanishes and the ball touches both supports at
        //    the vertex itself. When that holds for every blended edge here, each blend simply ends on the vertex with a
        //    zero-width section: both rails close on it, no termination edge, sharp edges (seams) keep their ends.
        [[nodiscard]] bool ResolvePinch(VertexToken Vertex, const std::vector<std::pair<EdgeToken, int>>& Blended)
        {
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            double Worst = 0.0;                                                              // [len] - Largest rail miss
            for (const auto& End : Blended)
            {
                const EdgeBlendSheet& Sheet = *SheetOf(End.first);
                if (Sheet.VariableCondition() || Sheet.LawCondition()) return false;
                const double τ = ProjectOntoCurve(Sheet.Contacts[0], Position, 0.0);         // [-] - Spine τ of the pinch
                Worst = std::max({ Worst, Distance(EvaluateCurve(Sheet.Contacts[0], τ).Position, Position), Distance(EvaluateCurve(Sheet.Contacts[1], τ).Position, Position) });
                if (Worst > Agreement) return false;
            }
            Agree(Worst);
            const int Pinched = MapOriginalVertex(Vertex);
            for (const auto& End : Blended)
            {
                for (int Side = 0; Side < 2; ++Side) ContactVertices[{ End.first.Slot, Side, End.second }] = Pinched;
                Terminations[{ End.first.Slot, End.second }] = {};
            }
            ++Outcome.Statistics.Vertices[static_cast<int>(VertexBlendCategory::Pinch)];
            return true;
        }

        // Spine-parameter crossing of two rails nearest a point (Gauss–Newton on C₁(a) − C₂(b)); empty if they miss.
        [[nodiscard]] std::optional<std::pair<double, double>> MeetRails(const CurveGeometry& First, const CurveGeometry& Second, Vector3 Near) const
        {
            double ParameterA = ProjectOntoCurve(First, Near, 0.0), ParameterB = ProjectOntoCurve(Second, Near, 0.0);
            for (int Iteration = 0; Iteration < 60; ++Iteration)
            {
                const CurveSample A = EvaluateCurve(First, ParameterA), B = EvaluateCurve(Second, ParameterB);
                const Vector3 Gap = A.Position - B.Position;
                const double  SquareA     = Dot(A.Derivative, A.Derivative);                 // [len²] - Gauss–Newton normal matrix
                const double  Mixed       = -Dot(A.Derivative, B.Derivative);
                const double  SquareB     = Dot(B.Derivative, B.Derivative);
                const double  GradientA   = -Dot(A.Derivative, Gap), GradientB = Dot(B.Derivative, Gap);
                const double  Determinant = SquareA * SquareB - Mixed * Mixed;
                if (std::fabs(Determinant) <= 1.0e-14 * std::max(SquareA * SquareB, 1.0e-300)) return std::nullopt;
                const double StepA = (GradientA * SquareB - Mixed * GradientB) / Determinant, StepB = (SquareA * GradientB - Mixed * GradientA) / Determinant;
                ParameterA += StepA;
                ParameterB += StepB;
                if (std::fabs(StepA) + std::fabs(StepB) <= 1.0e-15 * (1.0 + std::fabs(ParameterA) + std::fabs(ParameterB))) break;
            }
            if (Distance(EvaluateCurve(First, ParameterA).Position, EvaluateCurve(Second, ParameterB).Position) > Agreement) return std::nullopt;
            return std::pair{ ParameterA, ParameterB };
        }

        // 📝 Varady–Rockwood setback vertex blend (research § I): every blend at the vertex is cut back past the crossings
        //    of its rails with the neighbouring rails, plus one radius, at an exact cross-section (a circle of the ball); on
        //    each face the two rail ends are joined by a conic tangent to both rails (middle pole at their crossing); the
        //    2n-sided hole is filled by 2n fair bicubic quads about a centre (rim halves + cubic spokes, FairPatch). Rims
        //    are exact (G0); G1 is solved for and its worst break reported. Every edge at the vertex blended, planar faces
        //    around the vertex — anything else is refused. A chamfer sheet is ball-based too (r = d·tan(θ/2), contacts =
        //    feet of the ball), so ring, cuts, conics and quads are shared; only its section is the straight chord of the
        //    two feet across the chamfer plane (split at its midpoint) instead of the ball's arc.
        [[nodiscard]] std::string ResolveSetback(VertexToken Vertex, const std::vector<std::pair<EdgeToken, int>>& Ends)
        {
            constexpr double SetbackRatio = 1.0;                                             // [-] - Margin past the crossings, in radii
            constexpr int    QuadSpans    = 6;                                               // [-] - Fair quad spans per direction
            const bool ChamferCondition = Settings.Profile == BlendProfile::Chamfer;         // [-] - Straight sections
            const size_t Count = Ends.size();
            for (size_t First = 0; First < Count; ++First)
            {
                for (size_t Second = First + 1; Second < Count; ++Second)
                {
                    if (Ends[First].first == Ends[Second].first) return "setback vertex blend through a closed blended edge (🚧)";
                }
            }

            // ① Ring: blend i lies between faces Before and After; After of blend i is Before of blend i + 1.
            struct RingBlend
            {
                std::pair<EdgeToken, int> End        = {};                                   // [-]   - Blended edge end at the vertex
                FaceToken                 Before     = {};                                   // [-]   - Support shared with the previous blend
                FaceToken                 After      = {};                                   // [-]   - Support shared with the next blend
                int                       SideBefore = 0;                                    // [-]   - Sheet side of Before
                double                    Section    = 0.0;                                  // [-]   - Spine τ at the vertex
                double                    Speed      = 1.0;                                  // [len] - |spine′| there
                double                    Away       = 1.0;                                  // [-]   - ±1: τ direction leaving the vertex
                double                    Reach      = 0.0;                                  // [len] - Farthest rail crossing along the spine
                double                    Cut        = 0.0;                                  // [-]   - Spine τ of the setback section
                int                       FootBefore = -1;                                   // [-]   - Blueprint vertex on Before
                int                       FootAfter  = -1;                                   // [-]   - Blueprint vertex on After
            };
            std::vector<RingBlend> Ring;
            std::vector<bool>      Used(Count, false);
            Ring.push_back({ Ends[0], SheetOf(Ends[0].first)->Faces[0], SheetOf(Ends[0].first)->Faces[1], 0 });
            Used[0] = true;
            while (Ring.size() < Count)
            {
                bool Found = false;
                for (size_t Index = 0; Index < Count && !Found; ++Index)
                {
                    if (Used[Index]) continue;
                    const EdgeBlendSheet& Sheet = *SheetOf(Ends[Index].first);
                    const int Side = SideOn(Sheet, Ring.back().After);
                    if (Side < 0) continue;
                    Ring.push_back({ Ends[Index], Sheet.Faces[Side], Sheet.Faces[1 - Side], Side });
                    Used[Index] = true;
                    Found       = true;
                }
                if (!Found) return "setback: the blended edges do not form a face ring";
            }
            if (Ring.back().After != Ring.front().Before) return "setback: the face ring does not close";
            std::set<uint32_t> RingFaces;
            for (const RingBlend& Blend : Ring) RingFaces.insert(Blend.After.Slot);
            if (RingFaces.size() != Count) return "setback: a face meets the vertex twice (🚧)";
            for (const RingBlend& Blend : Ring)
            {
                if (!std::holds_alternative<PlaneSurface>(SurfaceOf(Blend.After))) return "setback vertex blend on a curved face (🚧)";
            }

            // ② Cuts: past the farther rail crossing on either support, plus SetbackRatio radii, measured along the spine.
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            const auto Rail = [&](const RingBlend& Blend, bool OnBefore) -> const CurveGeometry&
            {
                return SheetOf(Blend.End.first)->Contacts[OnBefore ? Blend.SideBefore : 1 - Blend.SideBefore];
            };
            for (RingBlend& Blend : Ring)
            {
                const EdgeBlendSheet& Sheet = *SheetOf(Blend.End.first);
                Blend.Section = ProjectOntoCurve(Sheet.Spine, Position, 0.0);
                Blend.Speed   = std::max(Length(EvaluateCurve(Sheet.Spine, Blend.Section).Derivative), 1.0e-12);
                Blend.Away    = (Blend.End.second == 0 ? 1.0 : -1.0) * (Sheet.Alignment > 0.0 ? 1.0 : -1.0);
            }
            const auto Along = [&](const RingBlend& Blend, double τ)
            {
                double Step = τ - Blend.Section;                                             // [-] - τ offset from the vertex section
                if (PeriodicCondition(SheetOf(Blend.End.first)->Spine)) Step -= 2.0 * std::numbers::pi * std::round(Step / (2.0 * std::numbers::pi));
                return Step * Blend.Away * Blend.Speed;
            };
            for (size_t Index = 0; Index < Count; ++Index)
            {
                RingBlend& Blend = Ring[Index];
                RingBlend& Next  = Ring[(Index + 1) % Count];
                const auto Meet  = MeetRails(Rail(Blend, false), Rail(Next, true), Position);
                if (!Meet) continue;
                Blend.Reach = std::max(Blend.Reach, Along(Blend, Meet->first));
                Next.Reach  = std::max(Next.Reach, Along(Next, Meet->second));
            }
            for (RingBlend& Blend : Ring)
            {
                const EdgeBlendSheet& Sheet = *SheetOf(Blend.End.first);
                // 💡 Short edges: the margin past the crossings shrinks to fit 45 % of the spine span (floor ¼ radius), so
                //    setbacks at both ends of one edge never overlap (pocket rim 1.0 long, size 0.3: 2 × (0.3 + 0.3) > 1.0
                //    refused "contact rail runs backwards"). Edges with room keep the full radius: unchanged geometry.
                const EdgeProfile& Profile = Partition.Edges.Resolve(Blend.End.first);
                const Vector3      Far     = Partition.Vertices.Resolve(Blend.End.second == 0 ? Profile.End : Profile.Start).Position;
                const double       Span    = Along(Blend, ProjectOntoCurve(Sheet.Spine, Far, Blend.Section));   // [len] - Spine length to the far end
                double             Margin  = SetbackRatio * Sheet.Radius;                                       // [len] - Past the crossings
                if (Span > 0.0 && Blend.Reach + Margin > 0.45 * Span) Margin = std::max(0.25 * Sheet.Radius, 0.45 * Span - Blend.Reach);
                Blend.Cut        = Blend.Section + Blend.Away * (Blend.Reach + Margin) / Blend.Speed;
                Blend.FootBefore = IssueVertex(EvaluateCurve(Rail(Blend, true), Blend.Cut).Position);
                Blend.FootAfter  = IssueVertex(EvaluateCurve(Rail(Blend, false), Blend.Cut).Position);
                ContactVertices[{ Blend.End.first.Slot, Blend.SideBefore, Blend.End.second }]     = Blend.FootBefore;
                ContactVertices[{ Blend.End.first.Slot, 1 - Blend.SideBefore, Blend.End.second }] = Blend.FootAfter;
            }

            // ③ Rims in ring order: the section of blend i (Before → After), then the conic on its After face.
            struct SetbackRim
            {
                RationalBezier         Halves[2] = {};                                       // [-] - Exact halves, Start → Middle → End
                int                    Edges[2]  = { -1, -1 };                               // [-] - Blueprint edges of the halves
                int                    Start     = -1;                                       // [-] - Blueprint vertices
                int                    Middle    = -1;
                int                    End       = -1;
                const SurfaceGeometry* Support   = nullptr;                                  // [-] - Surface the rim lies on
                Orientation            Sense     = Orientation::Forward;                     // [-] - Its outward sense
                Vector2                Reference = {};                                       // [-] - Projection reference
            };
            std::vector<SetbackRim> Rims;
            for (size_t Index = 0; Index < Count; ++Index)
            {
                const RingBlend&      Blend = Ring[Index];
                const RingBlend&      Next  = Ring[(Index + 1) % Count];
                const EdgeBlendSheet& Sheet = *SheetOf(Blend.End.first);
                const Vector3 Ball   = EvaluateCurve(Sheet.Spine, Blend.Cut).Position;         // [len] - Ball centre of the section
                const Vector3 Before = PositionOf(Blend.FootBefore), After = PositionOf(Blend.FootAfter);
                if (!Agree(std::fabs(Distance(Before, Ball) - Sheet.Radius)) || !Agree(std::fabs(Distance(After, Ball) - Sheet.Radius))) return "setback: section feet are off the ball";
                const Vector3 Bisector = (Before - Ball) + (After - Ball);
                if (Length(Bisector) <= 1.0e-6 * Sheet.Radius) return "setback: half-turn section (🚧)";
                SetbackRim Section;
                Section.Start = Blend.FootBefore;
                Section.End   = Blend.FootAfter;
                if (ChamferCondition)
                {
                    const Vector3 Middle = (Before + After) * 0.5;                          // [len] - Chord midpoint
                    Section.Middle    = IssueVertex(Middle);
                    Section.Halves[0] = ConstructConicBezier(Before, (Before + Middle) * 0.5, Middle, 1.0);
                    Section.Halves[1] = ConstructConicBezier(Middle, (Middle + After) * 0.5, After, 1.0);
                    Section.Edges[0]  = IssueSegment(Section.Start, Section.Middle);
                    Section.Edges[1]  = IssueSegment(Section.Middle, Section.End);
                }
                else
                {
                    Section.Middle    = IssueVertex(Ball + Normalize(Bisector) * Sheet.Radius);
                    Section.Halves[0] = ConstructArcBezier(Ball, Before, PositionOf(Section.Middle));
                    Section.Halves[1] = ConstructArcBezier(Ball, PositionOf(Section.Middle), After);
                    const CircularArc Circle{ ConstructFrame(Ball, Cross(Before - Ball, After - Ball), Before - Ball), Sheet.Radius };
                    Section.Edges[0]  = IssueConicArc(Circle, Section.Start, Section.Middle);
                    Section.Edges[1]  = IssueConicArc(Circle, Section.Middle, Section.End);
                }
                if (Section.Edges[0] < 0 || Section.Edges[1] < 0) return "setback: degenerate section";
                Section.Support   = &Sheet.Surface;
                Section.Sense     = Sheet.Sense;
                Section.Reference = ResolveSheetChart(Sheet, Blend.Cut);
                const ChainLink Near{ Section.Edges[0], Section.Start, Section.Middle }, Far{ Section.Edges[1], Section.Middle, Section.End };
                Terminations[{ Blend.End.first.Slot, Blend.End.second }] = Blend.SideBefore == 0 ? std::vector<ChainLink>{ Near, Far }
                                                                                               : std::vector<ChainLink>{ { Far.Edge, Far.To, Far.From }, { Near.Edge, Near.To, Near.From } };
                Rims.push_back(Section);

                // Conic on the After face, tangent to both rails at their feet (turning towards the vertex).
                const Vector3 From = After, To = PositionOf(Next.FootBefore);
                const Vector3 TangentFrom = Normalize(EvaluateCurve(Rail(Blend, false), Blend.Cut).Derivative * (-Blend.Away));
                const Vector3 TangentTo   = Normalize(EvaluateCurve(Rail(Next, true), Next.Cut).Derivative * (-Next.Away));
                const Vector3 Offset      = To - From;
                const double  Cosine      = Dot(TangentFrom, TangentTo);
                const double  Denominator = 1.0 - Cosine * Cosine;
                RationalBezier Conic = ConstructConicBezier(From, (From + To) * 0.5, To, 1.0);
                if (Denominator > 1.0e-10)
                {
                    const double  ReachFrom = (Dot(Offset, TangentFrom) - Cosine * Dot(Offset, TangentTo)) / Denominator;
                    const double  ReachTo   = (Cosine * Dot(Offset, TangentFrom) - Dot(Offset, TangentTo)) / Denominator;
                    const Vector3 Control   = From + TangentFrom * ReachFrom;
                    if (ReachFrom > Tolerance && ReachTo > Tolerance && Distance(Control, To + TangentTo * ReachTo) <= Agreement)
                    {
                        const Vector3 LegFrom = Normalize(From - Control), LegTo = Normalize(To - Control);
                        const double  Opening = std::atan2(Length(Cross(LegFrom, LegTo)), Dot(LegFrom, LegTo));  // [rad] - Angle at the crossing
                        Conic = ConstructConicBezier(From, Control, To, std::sin(0.5 * Opening));
                    }
                }
                const auto [Head, Tail] = SplitRationalBezier(Conic, 0.5);
                SetbackRim Face;
                Face.Start     = Blend.FootAfter;
                Face.End       = Next.FootBefore;
                Face.Middle    = IssueVertex(Head.Poles.back());
                Face.Halves[0] = Head;
                Face.Halves[1] = Tail;
                const NurbsCurve Carrier = ConvertBezierToNurbs(Conic);
                Face.Edges[0]  = IssueEdge(Carrier, 0.0, 0.5, Face.Start, Face.Middle);
                Face.Edges[1]  = IssueEdge(Carrier, 0.5, 1.0, Face.Middle, Face.End);
                Face.Support   = &SurfaceOf(Blend.After);
                Face.Sense     = Partition.Faces.Resolve(Blend.After).Sense;
                Face.Reference = ProjectOntoSurface(*Face.Support, Position, {});
                SetbackLinks[{ Blend.After.Slot, Vertex.Slot }] = { { Face.Edges[0], Face.Start, Face.Middle }, { Face.Edges[1], Face.Middle, Face.End } };
                Rims.push_back(Face);
            }

            // ④ Centre and spokes: each spoke leaves its rim middle across the rim inside the rim's tangent plane and
            //    reaches the centre in the plane normal to the mean outward normal; the centre sits on that normal where
            //    the spoke tangents pass closest.
            const auto Outward = [&](const SetbackRim& Rim, Vector3 Point)
            {
                const Vector3 Normal = EvaluateSurfaceNormal(*Rim.Support, ProjectOntoSurface(*Rim.Support, Point, Rim.Reference));
                return Rim.Sense == Orientation::Forward ? Normal : Normal * -1.0;
            };
            const size_t         Sides = Rims.size();
            std::vector<Vector3> Middles(Sides), Normals(Sides), Inwards(Sides);
            Vector3 Mean{}, Axis{};
            for (size_t Index = 0; Index < Sides; ++Index)
            {
                Middles[Index] = PositionOf(Rims[Index].Middle);
                Normals[Index] = Outward(Rims[Index], Middles[Index]);
                Mean           = Mean + Middles[Index] * (1.0 / Sides);
                Axis           = Axis + Normals[Index];
            }
            if (Length(Axis) <= 1.0e-6) return "setback: the rims share no outward direction";
            Axis = Normalize(Axis);
            double Height = 0.0, Spread = 0.0, Votes = 0.0;
            for (size_t Index = 0; Index < Sides; ++Index)
            {
                const RationalBezier& Tail = Rims[Index].Halves[1];
                Inwards[Index] = Normalize(Cross(Normals[Index], Tail.Poles[1] - Tail.Poles[0]));
                if (Dot(Inwards[Index], Mean - Middles[Index]) < 0.0) Inwards[Index] = Inwards[Index] * -1.0;
                Spread = std::max(Spread, Distance(Middles[Index], Mean));
                const double Skew = Dot(Axis, Inwards[Index]);
                const Vector3 Gap = Mean - Middles[Index];
                if (1.0 - Skew * Skew <= 1.0e-6) continue;
                Height += (Skew * Dot(Inwards[Index], Gap) - Dot(Axis, Gap)) / (1.0 - Skew * Skew);
                Votes  += 1.0;
            }
            if (Votes > 0.0) Height = std::clamp(Height / Votes, -Spread, Spread);
            const Vector3 Centre       = Mean + Axis * Height;
            const int     CentreVertex = IssueVertex(Centre);
            std::vector<RationalBezier> Spokes(Sides);
            std::vector<int>            SpokeEdges(Sides);
            for (size_t Index = 0; Index < Sides; ++Index)
            {
                const double  Lever  = 0.38 * Distance(Middles[Index], Centre);              // [len] - Tangent pole distance
                const Vector3 Radial = (Middles[Index] - Centre) - Axis * Dot(Middles[Index] - Centre, Axis);
                if (Length(Radial) <= 1.0e-9 * Scale) return "setback: a rim middle lies on the centre normal";
                Spokes[Index]     = { { Middles[Index], Middles[Index] + Inwards[Index] * Lever, Centre + Normalize(Radial) * Lever, Centre }, { 1.0, 1.0, 1.0, 1.0 } };
                SpokeEdges[Index] = IssueEdge(ConvertBezierToNurbs(Spokes[Index]), 0.0, 1.0, Rims[Index].Middle, CentreVertex);
            }

            // ⑤ Orientation: the neighbours use every rim in one rotational sense, the quads the other (checked per section).
            std::optional<bool> RingForward;
            for (size_t Index = 0; Index < Count; ++Index)
            {
                const SetbackRim& Rim = Rims[2 * Index];
                const ChainLink   Link = Ring[Index].SideBefore == 0 ? ChainLink{ Rim.Edges[0], Rim.Start, Rim.Middle } : ChainLink{ Rim.Edges[0], Rim.Middle, Rim.Start };
                const bool        Forward = AgainstBlend(Ring[Index].End, Link).From == Rim.Start;
                if (RingForward && *RingForward != Forward) return "setback: the ring is not consistently oriented";
                RingForward = Forward;
            }

            // ⑥ Quads: P00 = middle of rim k, P10 = junction, P11 = middle of rim k + 1, P01 = centre.
            // Spoke normal field: the blend of rim-middle and centre normals, made perpendicular to the spoke tangent so a G1
            //    fill exists; both quads beside a spoke honour the same field, which makes them G1 to each other.
            const auto SpokeNormal = [&](size_t Index, double 𝑡)
            {
                const Vector3 Blend   = Normals[Index] * (1.0 - 𝑡) + Axis * 𝑡;
                const Vector3 Tangent = Normalize(EvaluateRationalBezier(Spokes[Index], std::min(𝑡 + 1.0e-6, 1.0)) - EvaluateRationalBezier(Spokes[Index], std::max(𝑡 - 1.0e-6, 0.0)));
                return Normalize(Blend - Tangent * Dot(Blend, Tangent));
            };
            for (size_t Index = 0; Index < Sides; ++Index)
            {
                const size_t      Next = (Index + 1) % Sides;
                const SetbackRim& Rim  = Rims[Index];
                const SetbackRim& Following = Rims[Next];
                FairSide Bottom{ Rim.Halves[1], [&, Index](double 𝑡) { return Outward(Rims[Index], EvaluateRationalBezier(Rims[Index].Halves[1], 𝑡)); } };
                FairSide Right{ Following.Halves[0], [&, Next](double 𝑡) { return Outward(Rims[Next], EvaluateRationalBezier(Rims[Next].Halves[0], 𝑡)); } };
                FairSide Top{ ReverseRationalBezier(Spokes[Next]), [&, Next](double 𝑡) { return SpokeNormal(Next, 1.0 - 𝑡); } };
                FairSide Left{ Spokes[Index], [&, Index](double 𝑡) { return SpokeNormal(Index, 𝑡); } };
                const FairQuadOutcome Quad = BuildFairQuad(Bottom, Right, Top, Left, QuadSpans);
                if (!Quad.Surface) return "setback: " + Quad.Refusal;
                Outcome.Statistics.MaxTangentBreak = std::max({ Outcome.Statistics.MaxTangentBreak, Quad.SideBreaks[0], Quad.SideBreaks[1] });
                Outcome.Statistics.MaxSeamBreak    = std::max({ Outcome.Statistics.MaxSeamBreak, Quad.SideBreaks[2], Quad.SideBreaks[3] });
                Outcome.Statistics.MaxRimGap       = std::max(Outcome.Statistics.MaxRimGap, Quad.RimGap);
                const NurbsSurfaceDerivatives Probe = EvaluateNurbsSurfaceDerivatives(*Quad.Surface->Definition, { 0.5, 0.0 });
                CornerPatch Patch;
                Patch.Vertex    = Vertex;
                Patch.Surface   = *Quad.Surface;
                Patch.Sense     = Dot(Cross(Probe.U, Probe.V), Outward(Rim, Probe.Position)) > 0.0 ? Orientation::Forward : Orientation::Reversed;
                Patch.Reference = { 0.5, 0.5 };
                Patch.Links     = { { Rim.Edges[1], Rim.Middle, Rim.End }, { Following.Edges[0], Following.Start, Following.Middle }, { SpokeEdges[Next], Following.Middle, CentreVertex }, { SpokeEdges[Index], CentreVertex, Rim.Middle } };
                if (!*RingForward)
                {
                    for (ChainLink& Link : Patch.Links) std::swap(Link.From, Link.To);
                }
                Corners.push_back(Patch);
            }
            Outcome.Statistics.SetbackPatches += static_cast<int>(Sides);
            ++Outcome.Statistics.Vertices[static_cast<int>(VertexBlendCategory::Setback)];
            return {};
        }

        // 📝 Section where the rail on Side crosses a seam of its support (secant on the signed offset across the seam,
        //    from the vertex's own section). A seam is a chart artefact, so the section may move off the old vertex; the
        //    vertex projection alone left the foot off the seam on any mouth without a mirror plane through the seam.
        [[nodiscard]] double ResolveSeamSection(const EdgeBlendSheet& Sheet, int Side, EdgeToken Seam, double Start) const
        {
            const CurveGeometry&   SeamCurve = Partition.Curves.Resolve(Partition.Edges.Resolve(Seam).Curve);
            const SurfaceGeometry& Support   = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Sheet.Faces[Side]).Surface);
            const auto Across = [&](double τ)
            {
                const Vector3     Rail = EvaluateCurve(Sheet.Contacts[Side], τ).Position;
                const CurveSample Foot = EvaluateCurve(SeamCurve, ProjectOntoCurve(SeamCurve, Rail, 0.0));
                const Vector3     Normal = EvaluateSurfaceNormal(Support, ProjectOntoSurface(Support, Foot.Position, { 0.0, 0.0 }));
                return Dot(Rail - Foot.Position, Normalize(Cross(Normal, Foot.Derivative)));
            };
            double Previous = Start, Current = Start + 1.0e-4 * std::max(1.0, std::fabs(Start));
            double PreviousValue = Across(Previous), CurrentValue = Across(Current);
            for (int Iteration = 0; Iteration < 60 && CurrentValue != PreviousValue; ++Iteration)
            {
                const double Next = Current - CurrentValue * (Current - Previous) / (CurrentValue - PreviousValue);
                Previous = Current; PreviousValue = CurrentValue;
                Current  = Next;    CurrentValue  = Across(Current);
                if (std::fabs(Current - Previous) <= 1.0e-15 * (1.0 + std::fabs(Current))) break;
            }
            return Current;
        }

        // 📝 Closed rim through its seam vertex: the cross-section at the vertex is the blend face's seam.
        [[nodiscard]] std::string ResolveContinuation(VertexToken Vertex, EdgeToken Edge, EdgeToken Seam)
        {
            const EdgeBlendSheet& Sheet = *SheetOf(Edge);
            const auto [SeamFace, SeamMate] = FacesOfEdge(Seam);
            if (SeamFace != SeamMate) return "closed blended edge meets a sharp edge (rim ends need a seam)";
            const int SeamSide = SideOn(Sheet, SeamFace);
            if (SeamSide < 0) return "seam is not on a support of the blended rim";
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            const double  τ        = ResolveSeamSection(Sheet, SeamSide, Seam, ProjectOntoCurve(Sheet.Spine, Position, 0.0));
            int           FootA    = IssueVertex(EvaluateCurve(Sheet.Contacts[0], τ).Position);
            int           FootB    = IssueVertex(EvaluateCurve(Sheet.Contacts[1], τ).Position);
            const CurveGeometry& SeamCurve = Partition.Curves.Resolve(Partition.Edges.Resolve(Seam).Curve);
            if (!Agree(DistanceToCurve(SeamCurve, PositionOf(SeamSide == 0 ? FootA : FootB)))) return "seam does not cross the contact rail";
            const Vector3 Ball = EvaluateCurve(Sheet.Spine, τ).Position;                     // [len] - Ball centre
            CurveGeometry Across = StraightLine{ PositionOf(FootA), Normalize(PositionOf(FootB) - PositionOf(FootA)) };
            if (Settings.Profile == BlendProfile::Fillet)
            {
                const Vector3 Out = PositionOf(FootA) - Ball;                                // [len] - To contact A
                Across = CircularArc{ ConstructFrame(Ball, Cross(Out, PositionOf(FootB) - Ball), Out), Sheet.Radius };
            }
            int& SeamFoot = SeamSide == 0 ? FootA : FootB;
            const CliffCorner Corner = DetectCliff(Sheet, Vertex, Seam, SeamFace, PositionOf(SeamFoot), &Across);
            if (!Corner.Refusal.empty()) return Corner.Refusal;
            const bool Overflow = Corner.Point >= 0;                                         // [-] - The seam's support is consumed
            if (Overflow && Sheet.LawCondition()) return LawSubject() + " with a cliff overflow at a seam (🚧)";
            if (Overflow)
            {
                if (!RecordCliff(Edge, 0, SeamSide, Seam, SeamFace, Corner) || !RecordCliff(Edge, 1, SeamSide, Seam, SeamFace, Corner)) return "cliff overflow: inconsistent cliff at the seam";
                SeamFoot = Corner.Point;
            }
            int Section = -1;
            if (Settings.Profile == BlendProfile::Fillet) Section = IssueConicArc(Across, FootA, FootB);
            else Section = IssueSegment(FootA, FootB);
            if (Section < 0) return "degenerate cross-section at the seam";
            for (int End = 0; End < 2; ++End)
            {
                ContactVertices[{ Edge.Slot, 0, End }] = FootA;
                ContactVertices[{ Edge.Slot, 1, End }] = FootB;
                Terminations[{ Edge.Slot, End }]       = { { Section, FootA, FootB } };
            }
            if (!Overflow) TrimVertices[{ Seam.Slot, EndAt(Seam, Vertex) }] = SeamFoot;
            ++Outcome.Statistics.Vertices[static_cast<int>(VertexBlendCategory::Continuation)];
            return {};
        }

        // 📝 A blended loop split into two edges at a vertex where only seams meet it (a radial bore mouth crossing the seams of
        //    both cylinders): the two sheets continue smoothly, so the joint is their shared cross-section at the vertex,
        //    and every seam is trimmed at the foot on its own support. The seams must pass through those feet; seams that
        //    cross the rails at different sections would need extra rail vertices and are refused.
        [[nodiscard]] std::string ResolveSeamCrossing(VertexToken Vertex, std::pair<EdgeToken, int> EndA, std::pair<EdgeToken, int> EndB, const std::vector<std::pair<EdgeToken, int>>& Seams)
        {
            const EdgeBlendSheet& SheetA = *SheetOf(EndA.first);
            const EdgeBlendSheet& SheetB = *SheetOf(EndB.first);
            const int SideB[2] = { SideOn(SheetB, SheetA.Faces[0]), SideOn(SheetB, SheetA.Faces[1]) };   // Side of B on A's faces
            if (SideB[0] < 0 || SideB[1] < 0) return "seam crossing: the blended edges do not share both supports";
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            const int     FirstSide = SideOn(SheetA, FacesOfEdge(Seams[0].first).first);
            if (FirstSide < 0) return "seam crossing: a seam is not on a support of the blend";
            const double  SpineA   = ResolveSeamSection(SheetA, FirstSide, Seams[0].first, ProjectOntoCurve(SheetA.Spine, Position, 0.0));
            const Vector3 Ball     = EvaluateCurve(SheetA.Spine, SpineA).Position;          // [len] - Ball centre
            if (!Agree(DistanceToCurve(SheetB.Spine, Ball))) return "seam crossing: spines do not share the section ball";
            const double SpineB = ProjectOntoCurve(SheetB.Spine, Ball, 0.0);                // [-] - Same section on B
            int Feet[2] = { -1, -1 };
            for (int Side = 0; Side < 2; ++Side)
            {
                const Vector3 FootA = EvaluateCurve(SheetA.Contacts[Side], SpineA).Position;
                const Vector3 FootB = EvaluateCurve(SheetB.Contacts[SideB[Side]], SpineB).Position;
                if (!Agree(Distance(FootA, FootB))) return "seam crossing: the blends' sections disagree (unequal sizes, setback 🚧)";
                Feet[Side] = IssueVertex((FootA + FootB) * 0.5);
            }
            for (const auto& Seam : Seams)
            {
                const int Side = SideOn(SheetA, FacesOfEdge(Seam.first).first);
                if (Side < 0) return "seam crossing: a seam is not on a support of the blend";
                if (!Agree(DistanceToCurve(Partition.Curves.Resolve(Partition.Edges.Resolve(Seam.first).Curve), PositionOf(Feet[Side]))))
                    return "seam crossing: a seam meets the rail away from the shared section (extra rail vertex, 🚧)";
                TrimVertices[{ Seam.first.Slot, Seam.second }] = Feet[Side];
            }
            int Section = -1;
            if (Settings.Profile == BlendProfile::Fillet)
            {
                const Vector3 Out = PositionOf(Feet[0]) - Ball;                              // [len] - To contact A
                Section = IssueConicArc(CircularArc{ ConstructFrame(Ball, Cross(Out, PositionOf(Feet[1]) - Ball), Out), SheetA.Radius }, Feet[0], Feet[1]);
            }
            else Section = IssueSegment(Feet[0], Feet[1]);
            if (Section < 0) return "seam crossing: degenerate cross-section";
            ContactVertices[{ EndA.first.Slot, 0, EndA.second }] = Feet[0];
            ContactVertices[{ EndA.first.Slot, 1, EndA.second }] = Feet[1];
            Terminations[{ EndA.first.Slot, EndA.second }]       = { { Section, Feet[0], Feet[1] } };
            ContactVertices[{ EndB.first.Slot, SideB[0], EndB.second }] = Feet[0];
            ContactVertices[{ EndB.first.Slot, SideB[1], EndB.second }] = Feet[1];
            Terminations[{ EndB.first.Slot, EndB.second }]       = { { Section, Feet[SideB[0] == 0 ? 0 : 1], Feet[SideB[0] == 0 ? 1 : 0] } };
            ++Outcome.Statistics.Vertices[static_cast<int>(VertexBlendCategory::Continuation)];
            return {};
        }

        // 📝 One blended edge: the blend ends on the third face; cap = carrier ∩ that plane, from contact A to contact B.
        [[nodiscard]] std::string ResolveCap(VertexToken Vertex, std::pair<EdgeToken, int> End, EdgeToken First, EdgeToken Second)
        {
            const EdgeBlendSheet& Sheet = *SheetOf(End.first);
            const auto FacesFirst  = FacesOfEdge(First);
            const auto FacesSecond = FacesOfEdge(Second);
            FaceToken Third = {};
            for (FaceToken Face : { FacesFirst.first, FacesFirst.second })
            {
                if (SupportSide(Sheet, Face) < 0) Third = Face;
            }
            if (!Third.PresentCondition() || (FacesSecond.first != Third && FacesSecond.second != Third)) return "cap: no common third face";
            const auto*   Plane    = std::get_if<PlaneSurface>(&SurfaceOf(Third));
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            int Feet[2] = { -1, -1 };
            for (int Side = 0; Side < 2; ++Side)
            {
                const std::optional<Vector3> Foot = CrossCurveFace(Sheet.Contacts[Side], Position, Third);
                if (!Foot) return "cap: contact curve misses the third face";
                Feet[Side] = IssueVertex(*Foot);
            }
            bool Overflow[2] = { false, false };                                             // [-] - Side runs off its support
            for (EdgeToken Sharp : { First, Second })
            {
                const auto      Faces   = FacesOfEdge(Sharp);
                const FaceToken Support = Faces.first == Third ? Faces.second : Faces.first;
                const int       Side    = SupportSide(Sheet, Support);
                if (Side < 0) return "cap: sharp edge not on a support";
                if (const SmoothOverflow* Smooth = SmoothOf(Sheet, Side); Smooth && Smooth->Consumed == Support)
                {
                    // The consumed strip's end edge goes; the next face's edge on the third face is trimmed to the foot.
                    const EdgeProfile& Consumed = Partition.Edges.Resolve(Sharp);
                    const VertexToken  Far      = Consumed.Start == Vertex ? Consumed.End : Consumed.Start;
                    EdgeToken Trimmed = {};
                    int       TrimmedEnd = 0;
                    for (EdgeToken Edge : Inventory.Edges)
                    {
                        const EdgeProfile& Other = Partition.Edges.Resolve(Edge);
                        if (Other.Start != Far && Other.End != Far) continue;
                        const auto Around = FacesOfEdge(Edge);
                        if ((Around.first == Third && Around.second == Smooth->Next) || (Around.second == Third && Around.first == Smooth->Next)) { Trimmed = Edge; TrimmedEnd = Other.Start == Far ? 0 : 1; }
                    }
                    if (!Trimmed.PresentCondition()) return "smooth overflow: the next face does not meet the third face at the far corner";
                    const EdgeProfile&   Range = Partition.Edges.Resolve(Trimmed);
                    const CurveGeometry& Curve = Partition.Curves.Resolve(Range.Curve);
                    const double τ     = ProjectOntoCurve(Curve, PositionOf(Feet[Side]), Range.IntervalLow);
                    const double Slack = Agreement / std::max(MeasureParameterScale(Curve), 1.0e-300);   // [-] - Agreement in τ
                    if (!Agree(DistanceToCurve(Curve, PositionOf(Feet[Side]))) || τ < Range.IntervalLow - Slack || τ > Range.IntervalHigh + Slack)
                        return "blend wider than its face: smooth overflow runs past the next face too (🚧)";
                    TrimVertices[{ Trimmed.Slot, TrimmedEnd }] = Feet[Side];
                    Overflow[Side] = true;
                    continue;
                }
                const CliffCorner Corner = DetectCliff(Sheet, Vertex, Sharp, Support, PositionOf(Feet[Side]), nullptr);
                if (!Corner.Refusal.empty()) return Corner.Refusal;
                if (Corner.Point < 0) continue;
                if (Sheet.VariableCondition()) return "variable size with a cliff or notch overflow (🚧)";
                if (Sheet.LawCondition()) return LawSubject() + " with a cliff or notch overflow (🚧)";
                if (!RecordCliff(End.first, End.second, Side, Sharp, Support, Corner)) return "cliff overflow: the two blend ends see different cliffs";
                Feet[Side]     = Corner.Point;
                Overflow[Side] = true;
                TrimVertices[{ Corner.Trimmed.Slot, Corner.TrimmedEnd }] = Corner.Point;
                if (Corner.NotchCondition) Extensions.insert({ Corner.Trimmed.Slot, Corner.TrimmedEnd });   // the ledge grows under the blend
            }
            // 📝 Cap = blend carrier ∩ third face between the feet. A cylinder on a plane is a closed-form conic; every
            //    other pair (torus or cone blends, curved third faces) goes through the exact section primitive.
            int Cap = -1;
            const auto* Cylinder = std::get_if<CylinderSurface>(&Sheet.Surface);
            if (Cylinder && Plane)
            {
                const std::optional<CurveGeometry> Conic = SectionCylinderByPlane(*Cylinder, Plane->Frame.Origin, Plane->Frame.ZAxis);
                if (Conic) Cap = IssueConicArc(*Conic, Feet[0], Feet[1]);
            }
            else Cap = IssueSection(Sheet.Surface, SurfaceOf(Third), Feet[0], Feet[1]);
            if (Cap < 0) return std::string("cap: no exact section of the ") + SurfaceCategoryLabel(Sheet.Surface) + " blend and the " + SurfaceCategoryLabel(SurfaceOf(Third)) + " face through both feet";
            if (!Agree(MeasureEdgeDeparture(Cap, Sheet.Surface, 5)) || !Agree(MeasureEdgeDeparture(Cap, SurfaceOf(Third), 5))) return "cap curve leaves the blend or the third face";
            for (EdgeToken Sharp : { First, Second })
            {
                const auto Faces = FacesOfEdge(Sharp);
                const FaceToken Support = Faces.first == Third ? Faces.second : Faces.first;
                const int Side = SupportSide(Sheet, Support);
                if (Side < 0) return "cap: sharp edge not on a support";
                if (Overflow[Side]) continue;                                                 // consumed with its support
                TrimVertices[{ Sharp.Slot, EndAt(Sharp, Vertex) }] = Feet[Side];
                // Added material (concave) may run past the edge's end; so may a convex blend ending at a reflex corner (the
                // inner corner of an L): the foot on the far support lies beyond the vertex and the third face grows a cap.
                if (!Sheet.ConvexCondition || !ConvexEdge(First) || !ConvexEdge(Second)) Extensions.insert({ Sharp.Slot, EndAt(Sharp, Vertex) });
            }
            ContactVertices[{ End.first.Slot, 0, End.second }] = Feet[0];
            ContactVertices[{ End.first.Slot, 1, End.second }] = Feet[1];
            Terminations[{ End.first.Slot, End.second }]       = { { Cap, Feet[0], Feet[1] } };
            CapEdges[{ Third.Slot, Vertex.Slot }]              = Cap;
            ++Outcome.Statistics.Vertices[static_cast<int>(VertexBlendCategory::Cap)];
            return {};
        }

        // 📝 Two blended edges and one sharp edge: P where both outer contacts meet the sharp edge, M where both blends
        //    touch the shared face; the mitre M–P lies on the plane through the corner ball centre O, M and P.
        [[nodiscard]] std::string ResolveMitre(VertexToken Vertex, std::pair<EdgeToken, int> EndA, std::pair<EdgeToken, int> EndB, EdgeToken Sharp)
        {
            const EdgeBlendSheet& SheetA = *SheetOf(EndA.first);
            const EdgeBlendSheet& SheetB = *SheetOf(EndB.first);
            FaceToken Shared = {};
            for (FaceToken Face : SheetA.Faces) if (SideOn(SheetB, Face) >= 0) Shared = Face;
            if (!Shared.PresentCondition()) return "mitre: blended edges share no face";
            const int SharedA = SideOn(SheetA, Shared), SharedB = SideOn(SheetB, Shared);
            const int OuterA  = 1 - SharedA, OuterB = 1 - SharedB;
            const FaceToken FaceA = SheetA.Faces[OuterA], FaceB = SheetB.Faces[OuterB];
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            {
                const Vector3 NormalA = EvaluateSurfaceNormal(SurfaceOf(FaceA), ProjectOntoSurface(SurfaceOf(FaceA), Position, {}));
                const Vector3 NormalB = EvaluateSurfaceNormal(SurfaceOf(FaceB), ProjectOntoSurface(SurfaceOf(FaceB), Position, {}));
                if (Length(Cross(NormalA, NormalB)) < 1.0e-9)
                {
                    if (SheetA.VariableCondition() || SheetB.VariableCondition()) return "variable size across a smooth joint (🚧)";
                    return ResolveSmoothJoint(Vertex, EndA, EndB, Sharp, SharedA, SharedB);
                }
            }
            const bool Variable = SheetA.VariableCondition() || SheetB.VariableCondition();   // [-] - A cone or tilted chamfer plane
            const std::optional<Vector3> PointA = CrossCurveSurface(SheetA.Contacts[OuterA], Position, SurfaceOf(FaceB));
            const std::optional<Vector3> PointB = CrossCurveSurface(SheetB.Contacts[OuterB], Position, SurfaceOf(FaceA));
            if (!PointA || !PointB) return "mitre: outer contacts miss the far faces";
            if (!Agree(Distance(*PointA, *PointB)))
            {
                if (Settings.Profile != BlendProfile::Fillet || Variable || SheetA.LawCondition() || SheetB.LawCondition() || !SheetA.ConvexCondition || !SheetB.ConvexCondition)
                    return "unequal mitre: outer contacts meet the sharp edge apart (setback, 🚧)";
                return ResolveUnequalMitre(Vertex, EndA, EndB, Sharp, SharedA, SharedB, *PointA, *PointB);
            }
            const int Outer = IssueVertex((*PointA + *PointB) * 0.5);
            int Inner = -1, Mitre = -1;
            if (Settings.Profile == BlendProfile::Fillet && Variable)
            {
                // 📝 Variable fillets (cones): the sizes match at this corner (the outer contacts met above), but the spines
                //    are skew, so there is no corner ball. Both cones touch the shared face along their contact lines, which
                //    cross at M; the mitre is the general cone ∩ cone section from M (tangent there) to the sharp edge.
                std::optional<Vector3> Meet = MeetLines(SheetA.Contacts[SharedA], SheetB.Contacts[SharedB]);
                if (!Meet) return "variable mitre: fillet contacts on the shared face do not meet";
                Inner = IssueVertex(*Meet);
                Mitre = IssueSection(SheetA.Surface, SheetB.Surface, Inner, Outer);
                if (Mitre < 0) return std::string("variable mitre: no exact section of the ") + SurfaceCategoryLabel(SheetA.Surface) + " and " + SurfaceCategoryLabel(SheetB.Surface)
                                    + " fillets from the shared face to the sharp edge (🚧)";
            }
            else if (Settings.Profile == BlendProfile::Fillet)
            {
                const std::optional<Vector3> Ball = CrossCurveSurface(SheetA.Spine, Position, SheetB.Offsets[OuterB]);
                if (!Ball || !Agree(DistanceToCurve(SheetB.Spine, *Ball))) return "mitre: spines do not meet at a corner ball";
                const Vector3 FootA = EvaluateCurve(SheetA.Contacts[SharedA], ProjectOntoCurve(SheetA.Spine, *Ball, 0.0)).Position;
                const Vector3 FootB = EvaluateCurve(SheetB.Contacts[SharedB], ProjectOntoCurve(SheetB.Spine, *Ball, 0.0)).Position;
                if (!Agree(Distance(FootA, FootB))) return "mitre: blends touch the shared face apart";
                Inner = IssueVertex((FootA + FootB) * 0.5);
                // 📝 Both fillets touch the shared face at M, so they are tangent there and A ∩ B is singular at that end.
                //    Mirror-image blends (equal radii, symmetric supports — patterned cuts) meet on the plane through the
                //    corner ball O, M and P, which cuts A transversally: try that section first (two cylinders give the
                //    closed-form ellipse), then the general A ∩ B; the acceptance test below decides which one is true.
                const Vector3 Normal = Cross(PositionOf(Inner) - *Ball, PositionOf(Outer) - *Ball);
                const auto*   CylinderA = std::get_if<CylinderSurface>(&SheetA.Surface);
                if (CylinderA && std::holds_alternative<CylinderSurface>(SheetB.Surface))
                {
                    if (const std::optional<CurveGeometry> Conic = SectionCylinderByPlane(*CylinderA, *Ball, Normal)) Mitre = IssueConicArc(*Conic, Inner, Outer);
                }
                else if (Length(Normal) > Tolerance * Tolerance)
                {
                    const PlaneSurface Bisector{ ConstructFrame(*Ball, Normal, PositionOf(Inner) - *Ball) };
                    Mitre = IssueSection(SheetA.Surface, Bisector, Inner, Outer);
                    if (Mitre >= 0 && MeasureEdgeDeparture(Mitre, SheetB.Surface, 5) > Agreement) Mitre = -1;
                }
                if (Mitre < 0 && !(CylinderA && std::holds_alternative<CylinderSurface>(SheetB.Surface))) Mitre = IssueSection(SheetA.Surface, SheetB.Surface, Inner, Outer);
                if (Mitre < 0) return std::string("mitre: no exact section of the ") + SurfaceCategoryLabel(SheetA.Surface) + " and " + SurfaceCategoryLabel(SheetB.Surface)
                                    + " fillets from the shared face to the sharp edge (asymmetric fillets are tangent at the shared face, 🚧)";
            }
            else
            {
                // 📝 M = contact A on the shared face ∩ chamfer B (lines meet in closed form; circles and other contacts through
                //    the curve–surface solver, confirmed on contact B). Chamfers cut the shared face at an angle, so A ∩ B is
                //    transversal at M and the exact section primitive applies to every carrier pair.
                std::optional<Vector3> Meet = MeetLines(SheetA.Contacts[SharedA], SheetB.Contacts[SharedB]);
                if (!Meet)
                {
                    Meet = CrossCurveSurface(SheetA.Contacts[SharedA], Position, SheetB.Surface);
                    if (Meet && !Agree(DistanceToCurve(SheetB.Contacts[SharedB], *Meet))) Meet.reset();
                }
                if (!Meet) return "mitre: chamfer contacts on the shared face do not meet";
                Inner = IssueVertex(*Meet);
                Mitre = IssueSection(SheetA.Surface, SheetB.Surface, Inner, Outer);
                if (Mitre < 0) return std::string("mitre: no exact section of the ") + SurfaceCategoryLabel(SheetA.Surface) + " and " + SurfaceCategoryLabel(SheetB.Surface) + " chamfers";
            }
            if (Mitre < 0) return "mitre curve is degenerate";
            if (!Agree(MeasureEdgeDeparture(Mitre, SheetA.Surface, 5)) || !Agree(MeasureEdgeDeparture(Mitre, SheetB.Surface, 5))) return "mitre curve leaves a blend";
            TrimVertices[{ Sharp.Slot, EndAt(Sharp, Vertex) }] = Outer;
            if (!SheetA.ConvexCondition) Extensions.insert({ Sharp.Slot, EndAt(Sharp, Vertex) });
            for (const auto& [End, SharedSide] : { std::pair{ EndA, SharedA }, std::pair{ EndB, SharedB } })
            {
                ContactVertices[{ End.first.Slot, SharedSide, End.second }]     = Inner;
                ContactVertices[{ End.first.Slot, 1 - SharedSide, End.second }] = Outer;
                const int From = SharedSide == 0 ? Inner : Outer;
                Terminations[{ End.first.Slot, End.second }] = { { Mitre, From, From == Inner ? Outer : Inner } };
            }
            ++Outcome.Statistics.Vertices[static_cast<int>(VertexBlendCategory::Mitre)];
            return {};
        }

        // 📝 Unequal mitre (a pocket rim knifed into a curved face: the walls meet the shared face at different angles at a
        //    pocket corner, so the outer contacts cross the sharp corner edge at different depths P_A ≠ P_B). Each blend is
        //    the solid side of its own wedge, so the result is the union of the two rounded wedges: the corner edge stays up
        //    to the deeper crossing P_D; the shallow wall S runs on past the corner line up to the deep blend D, bounded by
        //    the section S-wall ∩ D from P_D to Q (the shallow outer contact meeting D); the mitre D ∩ S runs from Q to M.
        //    Blend D ends M → Q → P_D, blend S ends M → Q. Convex fillets only (the concave case would add material).
        [[nodiscard]] std::string ResolveUnequalMitre(VertexToken Vertex, std::pair<EdgeToken, int> EndA, std::pair<EdgeToken, int> EndB, EdgeToken Sharp, int SharedA, int SharedB,
                                                      Vector3 PointA, Vector3 PointB)
        {
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            const bool    DeepA    = Distance(PointA, Position) > Distance(PointB, Position);
            const std::pair<EdgeToken, int> EndD = DeepA ? EndA : EndB, EndS = DeepA ? EndB : EndA;
            const int     SharedD  = DeepA ? SharedA : SharedB, SharedS = DeepA ? SharedB : SharedA;
            const EdgeBlendSheet& SheetD = *SheetOf(EndD.first);
            const EdgeBlendSheet& SheetS = *SheetOf(EndS.first);
            const int       OuterD = 1 - SharedD, OuterS = 1 - SharedS;
            const FaceToken FaceS  = SheetS.Faces[OuterS];
            // Corner ball: the spines (offset shared face ∩ offset walls) still meet, so M is exact as in the equal mitre.
            const std::optional<Vector3> Ball = CrossCurveSurface(SheetD.Spine, Position, SheetS.Offsets[OuterS]);
            if (!Ball || !Agree(DistanceToCurve(SheetS.Spine, *Ball))) return "unequal mitre: spines do not meet at a corner ball";
            const Vector3 FootD = EvaluateCurve(SheetD.Contacts[SharedD], ProjectOntoCurve(SheetD.Spine, *Ball, 0.0)).Position;
            const Vector3 FootS = EvaluateCurve(SheetS.Contacts[SharedS], ProjectOntoCurve(SheetS.Spine, *Ball, 0.0)).Position;
            if (!Agree(Distance(FootD, FootS))) return "unequal mitre: blends touch the shared face apart";
            const std::optional<Vector3> Meet = CrossCurveSurface(SheetS.Contacts[OuterS], DeepA ? PointB : PointA, SheetD.Surface);
            if (!Meet) return "unequal mitre: the shallow outer contact misses the deep blend";
            const int Inner = IssueVertex((FootD + FootS) * 0.5);
            const int Deep  = IssueVertex(DeepA ? PointA : PointB);
            const int Knee  = IssueVertex(*Meet);
            // Mitre D ∩ S from M (tangent there, both blends touch the shared face) to Q: the plane through O, M, Q first
            // (accepted only if it stays on S), then the general section.
            int Mitre = -1;
            const Vector3 Normal = Cross(PositionOf(Inner) - *Ball, PositionOf(Knee) - *Ball);
            if (Length(Normal) > Tolerance * Tolerance)
            {
                const PlaneSurface Through{ ConstructFrame(*Ball, Normal, PositionOf(Inner) - *Ball) };
                Mitre = IssueSection(SheetD.Surface, Through, Inner, Knee);
                if (Mitre >= 0 && MeasureEdgeDeparture(Mitre, SheetS.Surface, 5) > Agreement) Mitre = -1;
            }
            if (Mitre < 0) Mitre = IssueSection(SheetD.Surface, SheetS.Surface, Inner, Knee);
            if (Mitre < 0) Mitre = IssueSection(SheetS.Surface, SheetD.Surface, Knee, Inner);
            if (Mitre < 0) return std::string("unequal mitre: no exact section of the ") + SurfaceCategoryLabel(SheetD.Surface) + " and " + SurfaceCategoryLabel(SheetS.Surface)
                                + " fillets from the shared face to the shallow wall (🚧)";
            const int Ledge = IssueSection(SurfaceOf(FaceS), SheetD.Surface, Deep, Knee);
            if (Ledge < 0) return std::string("unequal mitre: no exact section of the ") + SurfaceCategoryLabel(SurfaceOf(FaceS)) + " wall and the " + SurfaceCategoryLabel(SheetD.Surface) + " fillet";
            if (!Agree(MeasureEdgeDeparture(Mitre, SheetD.Surface, 5)) || !Agree(MeasureEdgeDeparture(Mitre, SheetS.Surface, 5))) return "unequal mitre: mitre curve leaves a blend";
            if (!Agree(MeasureEdgeDeparture(Ledge, SurfaceOf(FaceS), 5)) || !Agree(MeasureEdgeDeparture(Ledge, SheetD.Surface, 5))) return "unequal mitre: wall section leaves the wall or the blend";
            TrimVertices[{ Sharp.Slot, EndAt(Sharp, Vertex) }] = Deep;
            CapEdges[{ FaceS.Slot, Vertex.Slot }]               = Ledge;
            ContactVertices[{ EndD.first.Slot, SharedD, EndD.second }] = Inner;
            ContactVertices[{ EndD.first.Slot, OuterD, EndD.second }]  = Deep;
            ContactVertices[{ EndS.first.Slot, SharedS, EndS.second }] = Inner;
            ContactVertices[{ EndS.first.Slot, OuterS, EndS.second }]  = Knee;
            if (SharedD == 0) Terminations[{ EndD.first.Slot, EndD.second }] = { { Mitre, Inner, Knee }, { Ledge, Knee, Deep } };
            else              Terminations[{ EndD.first.Slot, EndD.second }] = { { Ledge, Deep, Knee }, { Mitre, Knee, Inner } };
            Terminations[{ EndS.first.Slot, EndS.second }] = { { Mitre, SharedS == 0 ? Inner : Knee, SharedS == 0 ? Knee : Inner } };
            ++Outcome.Statistics.Vertices[static_cast<int>(VertexBlendCategory::Mitre)];
            return {};
        }

        // 📝 Tangent-continuous chain (line ↔ arc across a smooth sharp edge, e.g. a slot rim or a rounded-rectangle loop):
        //    the two blends share their cross-section at the vertex, so the joint is that section — circle (fillet) or chord
        //    (chamfer). Every point comes from a transversal operation: the ball is spine A ∩ the edge's normal plane at
        //    the vertex, the feet are the contacts at that τ. (The mitre construction would use contact ∩ far face and
        //    spine ∩ offset, which are tangent here and only √ε accurate — 1e-8 gaps that break the SP-curves.)
        [[nodiscard]] std::string ResolveSmoothJoint(VertexToken Vertex, std::pair<EdgeToken, int> EndA, std::pair<EdgeToken, int> EndB, EdgeToken Sharp, int SharedA, int SharedB)
        {
            const EdgeBlendSheet& SheetA = *SheetOf(EndA.first);
            const EdgeBlendSheet& SheetB = *SheetOf(EndB.first);
            const int OuterA = 1 - SharedA, OuterB = 1 - SharedB;
            const Vector3        Position = Partition.Vertices.Resolve(Vertex).Position;
            const EdgeProfile&   Profile  = Partition.Edges.Resolve(EndA.first);
            const CurveGeometry& Carrier  = Partition.Curves.Resolve(Profile.Curve);
            const Vector3 Tangent = Normalize(EvaluateCurve(Carrier, ProjectOntoCurve(Carrier, Position, Profile.IntervalLow)).Derivative);
            const Vector3 Pick    = std::fabs(Tangent.X) < 0.6 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
            const PlaneSurface Section{ ConstructFrame(Position, Tangent, Normalize(Cross(Tangent, Pick))) };
            std::optional<Vector3> Ball = CrossCurveSurface(SheetA.Spine, Position, Section);
            // 📝 The edge's normal plane holds the shared ball only when the smooth edge runs along the shared face's normal
            //    (a slot on a plane). On a curved shared face (a slot knifed into a cylinder along the tangent-plane normal)
            //    the joint ball sits a few mm off that plane, where the two spines are tangent, so the section ball misses
            //    spine B by ~ρκs². The exact joint is the section whose outer foot lies on the smooth edge: the outer contact
            //    crosses the smooth edge transversally inside the outer face, so cut it with the plane through the smooth
            //    edge's tangent and the face normal (exact for a straight edge on a plane, converging otherwise).
            if (Ball)
            {
                const EdgeProfile&     SharpProfile = Partition.Edges.Resolve(Sharp);
                const CurveGeometry&   SharpCurve   = Partition.Curves.Resolve(SharpProfile.Curve);
                const SurfaceGeometry& OuterFace    = SurfaceOf(SheetA.Faces[OuterA]);
                Vector3 Foot = EvaluateCurve(SheetA.Contacts[OuterA], ProjectOntoCurve(SheetA.Spine, *Ball, 0.0)).Position;
                for (int Step = 0; Step < 6; ++Step)
                {
                    const CurveSample      Edge   = EvaluateCurve(SharpCurve, ProjectOntoCurve(SharpCurve, Foot, SharpProfile.IntervalLow));
                    const Vector3          Normal = EvaluateSurfaceNormal(OuterFace, ProjectOntoSurface(OuterFace, Edge.Position, {}));
                    const Vector3          Across = Cross(Edge.Derivative, Normal);
                    if (Length(Across) <= Tolerance) break;
                    const PlaneSurface     Cut{ ConstructFrame(Edge.Position, Normalize(Across), Normalize(Edge.Derivative)) };
                    const std::optional<Vector3> Crossing = CrossCurveSurface(SheetA.Contacts[OuterA], Foot, Cut);
                    if (!Crossing) break;
                    const double Moved = Distance(*Crossing, Foot);
                    Foot = *Crossing;
                    Ball = EvaluateCurve(SheetA.Spine, ProjectOntoCurve(SheetA.Contacts[OuterA], Foot, 0.0)).Position;
                    if (Moved <= Tolerance) break;
                }
            }
            if (!Ball || !Agree(DistanceToCurve(SheetB.Spine, *Ball))) return "smooth joint: spines do not share the section ball";
            const double  SpineA = ProjectOntoCurve(SheetA.Spine, *Ball, 0.0);               // [-] - Section τ on A
            const double  SpineB = ProjectOntoCurve(SheetB.Spine, *Ball, 0.0);               // [-] - Section τ on B
            const Vector3 InnerA = EvaluateCurve(SheetA.Contacts[SharedA], SpineA).Position, InnerB = EvaluateCurve(SheetB.Contacts[SharedB], SpineB).Position;
            const Vector3 OuterPointA = EvaluateCurve(SheetA.Contacts[OuterA], SpineA).Position, OuterPointB = EvaluateCurve(SheetB.Contacts[OuterB], SpineB).Position;
            if (!Agree(Distance(InnerA, InnerB)) || !Agree(Distance(OuterPointA, OuterPointB))) return "smooth joint: the blends' sections disagree (unequal sizes, setback 🚧)";
            const int Inner = IssueVertex((InnerA + InnerB) * 0.5);
            const int Outer = IssueVertex((OuterPointA + OuterPointB) * 0.5);
            if (!Agree(DistanceToCurve(Partition.Curves.Resolve(Partition.Edges.Resolve(Sharp).Curve), PositionOf(Outer)))) return "smooth joint: section misses the smooth edge";
            int Joint = -1;
            if (Settings.Profile == BlendProfile::Fillet)
            {
                const Vector3 Out = PositionOf(Inner) - *Ball;                               // [len] - To the shared-face foot
                Joint = IssueConicArc(CircularArc{ ConstructFrame(*Ball, Cross(Out, PositionOf(Outer) - *Ball), Out), SheetA.Radius }, Inner, Outer);
            }
            else Joint = IssueSegment(Inner, Outer);
            if (Joint < 0) return "smooth joint: degenerate section";
            TrimVertices[{ Sharp.Slot, EndAt(Sharp, Vertex) }] = Outer;
            if (!SheetA.ConvexCondition) Extensions.insert({ Sharp.Slot, EndAt(Sharp, Vertex) });
            for (const auto& [End, SharedSide] : { std::pair{ EndA, SharedA }, std::pair{ EndB, SharedB } })
            {
                ContactVertices[{ End.first.Slot, SharedSide, End.second }]     = Inner;
                ContactVertices[{ End.first.Slot, 1 - SharedSide, End.second }] = Outer;
                const int From = SharedSide == 0 ? Inner : Outer;
                Terminations[{ End.first.Slot, End.second }] = { { Joint, From, From == Inner ? Outer : Inner } };
            }
            ++Outcome.Statistics.Vertices[static_cast<int>(VertexBlendCategory::Continuation)];
            return {};
        }

        // 📝 Three blended edges: fillet → sphere of the ball touching all three faces, bounded by the three end
        //    cross-sections (great circles); chamfer → three lines from the contact corners to the apex of the planes.
        [[nodiscard]] std::string ResolveBallCorner(VertexToken Vertex, const std::vector<std::pair<EdgeToken, int>>& Ends)
        {
            std::vector<FaceToken> Faces;
            for (const auto& End : Ends)
            {
                for (FaceToken Face : SheetOf(End.first)->Faces)
                {
                    if (std::find(Faces.begin(), Faces.end(), Face) == Faces.end()) Faces.push_back(Face);
                }
            }
            if (Faces.size() != 3) return "ball corner needs three distinct faces";
            const Vector3 Position = Partition.Vertices.Resolve(Vertex).Position;
            std::map<uint32_t, int> Feet;                                                    // face slot → blueprint vertex
            CornerPatch Patch;
            Patch.Vertex = Vertex;
            if (Settings.Profile == BlendProfile::Fillet)
            {
                // Corner ball centre: spine of the first edge ∩ the offset of its third face (from a sheet supporting it).
                const EdgeBlendSheet& First = *SheetOf(Ends[0].first);
                FaceToken Third = {};
                for (FaceToken Face : Faces) if (SideOn(First, Face) < 0) Third = Face;
                const SurfaceGeometry* ThirdOffset = nullptr;
                for (const auto& End : Ends)
                {
                    const EdgeBlendSheet& Sheet = *SheetOf(End.first);
                    if (SideOn(Sheet, Third) >= 0) ThirdOffset = &Sheet.Offsets[SideOn(Sheet, Third)];
                }
                if (!ThirdOffset) return "ball corner: no offset of the third face";
                const std::optional<Vector3> Ball = CrossCurveSurface(First.Spine, Position, *ThirdOffset);
                if (!Ball) return "ball corner: spine misses the third offset";
                for (const auto& End : Ends) if (!Agree(DistanceToCurve(SheetOf(End.first)->Spine, *Ball))) return "ball corner: spines do not meet";
                for (FaceToken Face : Faces) Feet[Face.Slot] = IssueVertex(ProjectPointOntoSurface(SurfaceOf(Face), *Ball));
                for (const auto& End : Ends)
                {
                    const EdgeBlendSheet& Sheet = *SheetOf(End.first);
                    const double τ = ProjectOntoCurve(Sheet.Spine, *Ball, 0.0);
                    for (int Side = 0; Side < 2; ++Side)
                    {
                        if (!Agree(Distance(EvaluateCurve(Sheet.Contacts[Side], τ).Position, PositionOf(Feet[Sheet.Faces[Side].Slot])))) return "ball corner: contacts disagree with the corner feet";
                    }
                    const int     From = Feet[Sheet.Faces[0].Slot], To = Feet[Sheet.Faces[1].Slot];
                    const Vector3 Out  = PositionOf(From) - *Ball;                           // [len] - To the first foot
                    const int     Arc  = IssueConicArc(CircularArc{ ConstructFrame(*Ball, Cross(Out, PositionOf(To) - *Ball), Out), Sheet.Radius }, From, To);
                    if (Arc < 0) return "ball corner: degenerate end section";
                    Terminations[{ End.first.Slot, End.second }] = { { Arc, From, To } };
                    Patch.Links.push_back(AgainstBlend(End, { Arc, From, To }));
                }
                const Vector3 Diagonal = Normalize(Position - *Ball);                        // [-] - Towards the old vertex
                const Vector3 Pick     = std::fabs(Diagonal.X) < 0.6 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
                Patch.Surface   = SphereSurface{ ConstructFrame(*Ball, Cross(Diagonal, Pick), -Diagonal), SheetOf(Ends[0].first)->Radius };
                Patch.Sense     = SheetOf(Ends[0].first)->ConvexCondition ? Orientation::Forward : Orientation::Reversed;
                Patch.Reference = { std::numbers::pi, 0.0 };
                Corners.push_back(Patch);
                ++Outcome.Statistics.CornerSpheres;
            }
            else
            {
                Vector3 Normals[3];
                double  Offsets[3];
                for (int Index = 0; Index < 3; ++Index)
                {
                    const auto* Plane = std::get_if<PlaneSurface>(&SheetOf(Ends[Index].first)->Surface);
                    if (!Plane) return "chamfer corner of non-planar chamfers (🚧)";
                    Normals[Index] = Plane->Frame.ZAxis;
                    Offsets[Index] = Dot(Plane->Frame.ZAxis, Plane->Frame.Origin);
                }
                const double Volume = Dot(Normals[0], Cross(Normals[1], Normals[2]));        // [-] - Triple product
                if (std::fabs(Volume) < 1.0e-12) return "chamfer planes meet in no single apex";
                const Vector3 Apex = (Cross(Normals[1], Normals[2]) * Offsets[0] + Cross(Normals[2], Normals[0]) * Offsets[1] + Cross(Normals[0], Normals[1]) * Offsets[2]) * (1.0 / Volume);
                const int ApexVertex = IssueVertex(Apex);
                for (FaceToken Face : Faces)
                {
                    std::vector<const CurveGeometry*> Contacts;
                    for (const auto& End : Ends)
                    {
                        const EdgeBlendSheet& Sheet = *SheetOf(End.first);
                        if (SideOn(Sheet, Face) >= 0) Contacts.push_back(&Sheet.Contacts[SideOn(Sheet, Face)]);
                    }
                    const std::optional<Vector3> Meet = Contacts.size() == 2 ? MeetLines(*Contacts[0], *Contacts[1]) : std::nullopt;
                    if (!Meet) return "chamfer corner: contacts do not meet on a face";
                    Feet[Face.Slot] = IssueVertex(*Meet);
                }
                std::map<uint32_t, int> Spokes;                                              // face slot → apex segment
                for (FaceToken Face : Faces)
                {
                    Spokes[Face.Slot] = IssueSegment(Feet[Face.Slot], ApexVertex);
                    if (Spokes[Face.Slot] < 0) return "chamfer corner: apex coincides with a contact corner";
                }
                for (const auto& End : Ends)
                {
                    const EdgeBlendSheet& Sheet = *SheetOf(End.first);
                    const int From = Feet[Sheet.Faces[0].Slot], To = Feet[Sheet.Faces[1].Slot];
                    Terminations[{ End.first.Slot, End.second }] = { { Spokes[Sheet.Faces[0].Slot], From, ApexVertex }, { Spokes[Sheet.Faces[1].Slot], ApexVertex, To } };
                }
            }
            for (const auto& End : Ends)
            {
                const EdgeBlendSheet& Sheet = *SheetOf(End.first);
                for (int Side = 0; Side < 2; ++Side) ContactVertices[{ End.first.Slot, Side, End.second }] = Feet[Sheet.Faces[Side].Slot];
            }
            ++Outcome.Statistics.Vertices[static_cast<int>(VertexBlendCategory::BallCorner)];
            return {};
        }

        // 📝 The blend face uses its termination at the X end (start of the side-0 fin) side 0 → side 1 and at the Y end
        //    side 1 → side 0; a corner face uses the same link the other way.
        [[nodiscard]] ChainLink AgainstBlend(std::pair<EdgeToken, int> End, ChainLink Link) const
        {
            const EdgeBlendSheet& Sheet = *SheetOf(End.first);
            const bool XEnd = Partition.FinStart(Sheet.Fins[0]) == (End.second == 0 ? Partition.Edges.Resolve(End.first).Start : Partition.Edges.Resolve(End.first).End);
            if (XEnd) std::swap(Link.From, Link.To);
            return Link;
        }

        //----------------------------------------------------------------------------------------------------------------
        //                                            PIECES
        //----------------------------------------------------------------------------------------------------------------

        [[nodiscard]] std::string ResolvePieces()
        {
            for (EdgeToken Edge : Inventory.Edges)
            {
                const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
                const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
                const bool           Closed  = Profile.Start == Profile.End;
                if (ConsumedEdges.count(Edge.Slot) != 0) continue;                           // inside a consumed support
                bool FarEdge = false;                                                        // [-] - Replaced by a cliff section
                for (const auto& [Key, Overflow] : Cliffs) FarEdge = FarEdge || Overflow.FarEdge == Edge;
                if (FarEdge) continue;
                if (const EdgeBlendSheet* Sheet = SheetOf(Edge))
                {
                    for (int Side = 0; Side < 2; ++Side)
                    {
                        if (const auto Cliff = Cliffs.find({ Edge.Slot, Side }); Cliff != Cliffs.end())
                        {
                            if (std::string Refusal = IssueCliffSection(*Sheet, Edge, Side, Cliff->second, Closed); !Refusal.empty()) return Refusal;
                            continue;
                        }
                        const auto StartFound = ContactVertices.find({ Edge.Slot, Side, 0 });
                        const auto EndFound   = ContactVertices.find({ Edge.Slot, Side, 1 });
                        if (StartFound == ContactVertices.end() || EndFound == ContactVertices.end()) return "edge " + LabelToken("E", Edge) + ": unresolved blend end";
                        const CurveGeometry& Contact = Sheet->Contacts[Side];
                        const int            From    = StartFound->second, To = EndFound->second;
                        if (Closed)
                        {
                            const double Low = ProjectOntoCurve(Contact, PositionOf(From), 0.0);
                            ContactPieces[{ Edge.Slot, Side }] = IssueEdge(Contact, Low, Low + 2.0 * std::numbers::pi, From, From);
                            continue;
                        }
                        // 📝 Contacts share the spine τ; along the edge τ must advance in the sheet's alignment, otherwise the
                        //    rail runs backwards (the blend is wider than the face or edge it rests on).
                        const bool   Along = Sheet->Alignment > 0.0;
                        const double Low   = ProjectOntoCurve(Contact, PositionOf(Along ? From : To), 0.0);
                        const double High  = ProjectOntoCurve(Contact, PositionOf(Along ? To : From), Low);
                        if (!(High - Low > Tolerance))
                        {
                            // 📝 Rails that shrink to a point (two rails meeting at the middle of a face, a loop closing over
                            //    its face) are collapsed, not refused: MergeCoincidences fuses the ends and drops the face.
                            if (Distance(PositionOf(From), PositionOf(To)) > Agreement) return "edge " + LabelToken("E", Edge) + ": contact rail runs backwards (blend wider than its face)";
                            ContactPieces[{ Edge.Slot, Side }] = IssueEdge(Contact, Low, Low, Along ? From : To, Along ? To : From);
                            Collapsed.insert(ContactPieces[{ Edge.Slot, Side }]);
                            continue;
                        }
                        ContactPieces[{ Edge.Slot, Side }] = IssueEdge(Contact, Low, High, Along ? From : To, Along ? To : From);
                    }
                    continue;
                }
                const auto StartFound = TrimVertices.find({ Edge.Slot, 0 });
                const auto EndFound   = TrimVertices.find({ Edge.Slot, 1 });
                const bool Trimmed    = StartFound != TrimVertices.end() || EndFound != TrimVertices.end();
                const int  From       = StartFound != TrimVertices.end() ? StartFound->second : MapOriginalVertex(Profile.Start);
                const int  To         = EndFound != TrimVertices.end() ? EndFound->second : MapOriginalVertex(Profile.End);
                if (!Trimmed) { Pieces[Edge.Slot] = IssueEdge(Curve, Profile.IntervalLow, Profile.IntervalHigh, From, To); continue; }
                if (Closed) return "edge " + LabelToken("E", Edge) + ": closed sharp edge at a blended vertex";
                // 📝 A convex blend only shortens a sharp edge. A concave blend adds material flush with the third face, so its
                //    end may lie past the old vertex: the edge is lengthened along its own carrier (the third face grows by the
                //    cap region); the self-clash gate after assembly rejects an extension that runs into the body.
                const double Span = Profile.IntervalHigh - Profile.IntervalLow;              // [-] - Original τ span
                // Fold periodic carriers at the middle of the gap the edge does not cover, so a short arc extended at its
                // start is not wrapped a full turn.
                const double Fold = PeriodicCondition(Curve) ? Profile.IntervalLow - 0.5 * (2.0 * std::numbers::pi - Span) : Profile.IntervalLow - 0.25 * Span;
                const double Low  = StartFound != TrimVertices.end() ? ProjectOntoCurve(Curve, PositionOf(From), Fold) : Profile.IntervalLow;
                const double High = EndFound != TrimVertices.end() ? ProjectOntoCurve(Curve, PositionOf(To), Low) : Profile.IntervalHigh;
                const bool   LowExtended  = Extensions.count({ Edge.Slot, 0 }) != 0;
                const bool   HighExtended = Extensions.count({ Edge.Slot, 1 }) != 0;
                if (!(High - Low > Tolerance) && Distance(PositionOf(From), PositionOf(To)) <= Agreement)
                {
                    // Both trims land on the same point (two blends meeting on this edge): a zero-length piece to collapse.
                    Pieces[Edge.Slot] = IssueEdge(Curve, Low, Low, From, To);
                    Collapsed.insert(Pieces[Edge.Slot]);
                    continue;
                }
                if (!(High - Low > Tolerance) || (High > Profile.IntervalHigh + Agreement && !HighExtended) || (Low < Profile.IntervalLow - Agreement && !LowExtended))
                {
                    const double Overshoot = std::max({ Profile.IntervalLow - Low, High - Profile.IntervalHigh, Low - High }) * MeasureParameterScale(Curve);
                    char Detail[96];
                    std::snprintf(Detail, sizeof Detail, " (%s, overshoot %.3g)", CurveCategoryLabel(Curve), Overshoot);
                    return "edge " + LabelToken("E", Edge) + ": consumed by the blend (blend wider than the edge)" + Detail;
                }
                if (Distance(EvaluateCurve(Curve, Low).Position, PositionOf(From)) > Agreement || Distance(EvaluateCurve(Curve, High).Position, PositionOf(To)) > Agreement)
                {
                    return "edge " + LabelToken("E", Edge) + ": blend end is off the sharp edge's carrier";
                }
                Pieces[Edge.Slot] = IssueEdge(Curve, Low, High, From, To);
            }
            return {};
        }

        //----------------------------------------------------------------------------------------------------------------
        //                                            FACES
        //----------------------------------------------------------------------------------------------------------------

        [[nodiscard]] ParameterCurve FitUse(const SurfaceGeometry& Surface, int Edge, Vector2 Reference) const
        {
            const BlueprintEdge& Block  = Blueprint.Edges[Edge];
            const Vector3        Middle = EvaluateCurve(Block.Curve, 0.5 * (Block.Low + Block.High)).Position;
            const Vector2        Anchor = ProjectOntoSurface(Surface, Middle, Reference);
            return FitSurfaceCurve(Surface, Block.Curve, Block.Low, Block.High, { Anchor - Vector2{ 1.0, 1.0 }, Anchor + Vector2{ 1.0, 1.0 } }, Anchor);
        }

        [[nodiscard]] std::string DiagnoseLoop(const std::vector<BlueprintUse>& Loop, const std::string& Subject) const
        {
            for (size_t Index = 0; Index < Loop.size(); ++Index)
            {
                if (UseEnd(Loop[Index]) != UseStart(Loop[(Index + 1) % Loop.size()])) return Subject + ": loop does not close at use " + std::to_string(Index);
            }
            return {};
        }

        //----------------------------------------------------------------------------------------------------------------
        //                                            MERGE
        //----------------------------------------------------------------------------------------------------------------

        // 💡 Blends that meet (two rails reaching the middle of a face, a loop closing over its face, two trims landing on
        //    one point of a sharp edge) leave zero-length edges. ① Each collapsed edge fuses its end vertices (union–find);
        //    ② edges that now share both ends and run along the same carrier within Agreement become one edge, used by
        //    both blend faces; ③ a loop whose uses cancel in pairs (the face swept to zero area between two merged rails)
        //    is dropped, and a face without loops with it; ④ merged uses are refitted against the surviving carrier.
        //    Nothing is loosened: only edges whose ends coincide within Agreement collapse, and only coincident carriers
        //    merge, so blends that do not meet keep their blueprint untouched.
        [[nodiscard]] std::string MergeCoincidences()
        {
            if (Collapsed.empty()) return {};
            std::vector<int> Root(Blueprint.Vertices.size());
            for (size_t Index = 0; Index < Root.size(); ++Index) Root[Index] = static_cast<int>(Index);
            const auto Find = [&](int Vertex)
            {
                while (Root[Vertex] != Vertex) Vertex = Root[Vertex] = Root[Root[Vertex]];
                return Vertex;
            };
            for (int Edge : Collapsed)
            {
                const int Start = Find(Blueprint.Edges[Edge].Start), End = Find(Blueprint.Edges[Edge].End);
                if (Start != End) Root[std::max(Start, End)] = std::min(Start, End);
            }
            for (BlueprintEdge& Block : Blueprint.Edges)
            {
                if (Block.Start >= 0) Block.Start = Find(Block.Start);
                if (Block.End >= 0)   Block.End   = Find(Block.End);
            }

            // ② Coincident carriers between the same fused ends: the lower index survives.
            std::vector<int>  Survivor(Blueprint.Edges.size());
            std::vector<bool> Turned(Blueprint.Edges.size(), false);
            for (size_t Index = 0; Index < Survivor.size(); ++Index) Survivor[Index] = static_cast<int>(Index);
            const auto Coincident = [&](const BlueprintEdge& First, const BlueprintEdge& Second)
            {
                for (int Sample = 1; Sample < 8; ++Sample)
                {
                    const double  τ     = First.Low + (First.High - First.Low) * Sample / 8.0;
                    const Vector3 Point = EvaluateCurve(First.Curve, τ).Position;
                    const double  Along = std::clamp(ProjectOntoCurve(Second.Curve, Point, 0.5 * (Second.Low + Second.High)), Second.Low, Second.High);
                    if (Distance(EvaluateCurve(Second.Curve, Along).Position, Point) > Agreement) return false;
                }
                return true;
            };
            for (size_t Later = 0; Later < Blueprint.Edges.size(); ++Later)
            {
                const BlueprintEdge& Second = Blueprint.Edges[Later];
                if (Collapsed.count(static_cast<int>(Later)) != 0 || Second.Start == Second.End) continue;
                for (size_t Earlier = 0; Earlier < Later; ++Earlier)
                {
                    const BlueprintEdge& First = Blueprint.Edges[Earlier];
                    if (Collapsed.count(static_cast<int>(Earlier)) != 0 || Survivor[Earlier] != static_cast<int>(Earlier)) continue;
                    const bool Same    = First.Start == Second.Start && First.End == Second.End;
                    const bool Opposed = First.Start == Second.End && First.End == Second.Start;
                    if ((!Same && !Opposed) || !Coincident(First, Second) || !Coincident(Second, First)) continue;
                    Survivor[Later] = static_cast<int>(Earlier);
                    Turned[Later]   = Opposed;
                    ++Outcome.Statistics.MergedEdges;
                    break;
                }
            }

            // ③ Rewrite the loops: collapsed uses vanish, merged uses move to the survivor, cancelling pairs drop out.
            std::vector<BlueprintFace>    Faces;
            std::vector<BlendFaceHistory> Histories;
            for (size_t FaceIndex = 0; FaceIndex < Blueprint.Faces.size(); ++FaceIndex)
            {
                BlueprintFace& Face = Blueprint.Faces[FaceIndex];
                std::vector<std::vector<BlueprintUse>> Loops;
                for (std::vector<BlueprintUse>& Loop : Face.Loops)
                {
                    std::vector<BlueprintUse> Uses;
                    std::vector<bool>         Moved;
                    for (BlueprintUse& Use : Loop)
                    {
                        if (Collapsed.count(Use.Edge) != 0) continue;
                        const int Edge = Use.Edge;
                        if (Survivor[Edge] != Edge)
                        {
                            const BlueprintEdge& Old       = Blueprint.Edges[Edge];
                            const Vector2        Reference = EvaluateParameterCurve(Use.SurfaceCurve, 0.5 * (Old.Low + Old.High)).Position;
                            Use.Edge         = Survivor[Edge];
                            Use.Sense        = Turned[Edge] ? InvertOrientation(Use.Sense) : Use.Sense;
                            Use.SurfaceCurve = FitUse(Face.Surface, Use.Edge, Reference);
                        }
                        Uses.push_back(Use);
                        Moved.push_back(Survivor[Edge] != Edge);
                    }
                    // A merged rail used both ways in one loop bounds nothing: the face between the rails has no width.
                    // (Seam edges of periodic faces are used both ways too, but neither use was moved, so they stay.)
                    for (bool Cancelled = true; Cancelled;)
                    {
                        Cancelled = false;
                        for (size_t First = 0; First < Uses.size() && !Cancelled; ++First)
                        {
                            for (size_t Second = First + 1; Second < Uses.size() && !Cancelled; ++Second)
                            {
                                if (Uses[First].Edge != Uses[Second].Edge || Uses[First].Sense == Uses[Second].Sense || (!Moved[First] && !Moved[Second])) continue;
                                Uses.erase(Uses.begin() + static_cast<std::ptrdiff_t>(Second));
                                Uses.erase(Uses.begin() + static_cast<std::ptrdiff_t>(First));
                                Moved.erase(Moved.begin() + static_cast<std::ptrdiff_t>(Second));
                                Moved.erase(Moved.begin() + static_cast<std::ptrdiff_t>(First));
                                Cancelled = true;
                            }
                        }
                    }
                    if (Uses.empty()) continue;
                    // The loop may have been opened where a cancelled pair sat: rotate so it closes, else refuse.
                    if (std::string Refusal = DiagnoseLoop(Uses, "merged loop"); !Refusal.empty())
                    {
                        bool Closed = false;
                        for (size_t Turn = 1; Turn < Uses.size() && !Closed; ++Turn)
                        {
                            std::rotate(Uses.begin(), Uses.begin() + 1, Uses.end());
                            Closed = DiagnoseLoop(Uses, {}).empty();
                        }
                        if (!Closed) return "blends meet but the merged boundary does not close (" + Refusal + ")";
                    }
                    Loops.push_back(std::move(Uses));
                }
                if (Loops.empty()) { ++Outcome.Statistics.MergedFaces; continue; }
                Face.Loops = std::move(Loops);
                Faces.push_back(std::move(Face));
                Histories.push_back(Provenance[FaceIndex]);
            }
            Blueprint.Faces = std::move(Faces);
            Provenance      = std::move(Histories);
            Outcome.Statistics.MergedVertices = 0;
            for (size_t Index = 0; Index < Root.size(); ++Index) Outcome.Statistics.MergedVertices += Find(static_cast<int>(Index)) != static_cast<int>(Index) ? 1 : 0;
            return {};
        }

        [[nodiscard]] std::string ComposeFaces()
        {
            // ① Original faces: contacts replace blended fins, sharp fins become their trimmed pieces, caps fill corners.
            for (FaceToken Face : Inventory.Faces)
            {
                if (ConsumedFaces.count(Face.Slot) != 0) continue;                           // narrower than its blend (cliff overflow)
                const FaceProfile&     Profile = Partition.Faces.Resolve(Face);
                const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Profile.Surface);
                BlueprintFace Blue{ Surface, Profile.Sense, {} };
                for (LoopToken Loop : Profile.Loops)
                {
                    std::vector<FinToken> Fins;
                    Partition.EnumerateLoopFins(Loop, [&](FinToken Fin) { Fins.push_back(Fin); });
                    std::vector<BlueprintUse> Uses;
                    for (FinToken Fin : Fins)
                    {
                        const FinProfile& FinRecord = Partition.Fins.Resolve(Fin);
                        if (!FinRecord.SurfaceCurve) return "fin without an SP-curve on " + LabelToken("F", Face);
                        if (const EdgeBlendSheet* Sheet = SheetOf(FinRecord.Edge))
                        {
                            const int Side  = Sheet->Fins[0] == Fin ? 0 : 1;
                            const int Piece = ContactPieces[{ FinRecord.Edge.Slot, Side }];
                            const BlueprintEdge& Block = Blueprint.Edges[Piece];
                            const Orientation Sense = Sheet->Alignment > 0.0 ? FinRecord.Sense : InvertOrientation(FinRecord.Sense);
                            const CurveGeometry& Original = Partition.Curves.Resolve(Partition.Edges.Resolve(FinRecord.Edge).Curve);
                            const EdgeProfile&   Range    = Partition.Edges.Resolve(FinRecord.Edge);
                            const Vector3 Middle = EvaluateCurve(Block.Curve, 0.5 * (Block.Low + Block.High)).Position;
                            const double  Along  = std::clamp(ProjectOntoCurve(Original, Middle, Range.IntervalLow), Range.IntervalLow, Range.IntervalHigh);
                            Uses.push_back({ Piece, Sense, FitUse(Surface, Piece, EvaluateParameterCurve(*FinRecord.SurfaceCurve, Along).Position) });
                        }
                        else if (ConsumedEdges.count(FinRecord.Edge.Slot) != 0) {}               // gone with a consumed support
                        else if (const auto Replaced = Replacements.find(FinRecord.Edge.Slot); Replaced != Replacements.end())
                        {
                            // The cliff section runs where the far edge ran; its sense follows the fin's direction.
                            const BlueprintEdge& Block    = Blueprint.Edges[Replaced->second];
                            const CurveSample    Middle   = EvaluateCurve(Block.Curve, 0.5 * (Block.Low + Block.High));
                            const EdgeProfile&   Range    = Partition.Edges.Resolve(FinRecord.Edge);
                            const CurveGeometry& Original = Partition.Curves.Resolve(Range.Curve);
                            const double         Along    = std::clamp(ProjectOntoCurve(Original, Middle.Position, Range.IntervalLow), Range.IntervalLow, Range.IntervalHigh);
                            const Vector3        Heading  = EvaluateCurve(Original, Along).Derivative * (FinRecord.Sense == Orientation::Forward ? 1.0 : -1.0);
                            const Orientation    Sense    = Dot(Middle.Derivative, Heading) > 0.0 ? Orientation::Forward : Orientation::Reversed;
                            Uses.push_back({ Replaced->second, Sense, FitUse(Surface, Replaced->second, EvaluateParameterCurve(*FinRecord.SurfaceCurve, Along).Position) });
                        }
                        else Uses.push_back({ Pieces[FinRecord.Edge.Slot], FinRecord.Sense, *FinRecord.SurfaceCurve });
                        const auto Cap = CapEdges.find({ Face.Slot, Partition.FinEnd(Fin).Slot });
                        // ⚠️ A loop that starts with a consumed fin has no use yet to hang the cap or setback on (radius equal to
                        //    the neighbouring face's width): Uses.back() read past an empty vector and crashed the workspace.
                        if ((Cap != CapEdges.end() || SetbackLinks.count({ Face.Slot, Partition.FinEnd(Fin).Slot }) != 0) && Uses.empty())
                            return "blend consumes the face next to the corner on " + LabelToken("F", Face) + " (size reaches the face width)";
                        if (Cap != CapEdges.end())
                        {
                            const int From = UseEnd(Uses.back());
                            const ChainLink Link{ Cap->second, From, Blueprint.Edges[Cap->second].Start == From ? Blueprint.Edges[Cap->second].End : Blueprint.Edges[Cap->second].Start };
                            const Vector2 Reference = EvaluateParameterCurve(*FinRecord.SurfaceCurve, Partition.FinParameterEnd(Fin)).Position;
                            Uses.push_back({ Link.Edge, LinkSense(Link), FitUse(Surface, Link.Edge, Reference) });
                        }
                        const auto Setback = SetbackLinks.find({ Face.Slot, Partition.FinEnd(Fin).Slot });
                        if (Setback != SetbackLinks.end())
                        {
                            std::vector<ChainLink> Chain = Setback->second;
                            if (UseEnd(Uses.back()) == Chain.back().To)
                            {
                                std::reverse(Chain.begin(), Chain.end());
                                for (ChainLink& Link : Chain) std::swap(Link.From, Link.To);
                            }
                            if (UseEnd(Uses.back()) != Chain.front().From) return "setback conic does not meet the rail on " + LabelToken("F", Face);
                            const Vector2 Reference = EvaluateParameterCurve(*FinRecord.SurfaceCurve, Partition.FinParameterEnd(Fin)).Position;
                            for (const ChainLink& Link : Chain) Uses.push_back({ Link.Edge, LinkSense(Link), FitUse(Surface, Link.Edge, Reference) });
                        }
                    }
                    if (std::string Refusal = DiagnoseLoop(Uses, LabelToken("F", Face)); !Refusal.empty()) return Refusal;
                    Blue.Loops.push_back(std::move(Uses));
                }
                Blueprint.Faces.push_back(std::move(Blue));
                Provenance.push_back({ {}, BlendFaceOrigin::Modified, Face, {}, {} });
            }

            // ② Blend faces: contact A reversed, termination at X, contact B, termination at Y reversed.
            for (const EdgeBlendSheet& Sheet : Outcome.Sheets)
            {
                const EdgeProfile& Profile = Partition.Edges.Resolve(Sheet.Edge);
                const int XEnd = Partition.FinStart(Sheet.Fins[0]) == Profile.Start ? 0 : 1;
                const int YEnd = 1 - XEnd;
                const int PieceA = ContactPieces[{ Sheet.Edge.Slot, 0 }], PieceB = ContactPieces[{ Sheet.Edge.Slot, 1 }];
                const Orientation NaturalA = Sheet.Alignment > 0.0 ? Partition.Fins.Resolve(Sheet.Fins[0]).Sense : InvertOrientation(Partition.Fins.Resolve(Sheet.Fins[0]).Sense);
                const Orientation NaturalB = Sheet.Alignment > 0.0 ? Partition.Fins.Resolve(Sheet.Fins[1]).Sense : InvertOrientation(Partition.Fins.Resolve(Sheet.Fins[1]).Sense);
                const Orientation SenseA   = FlippedContacts.count({ Sheet.Edge.Slot, 0 }) != 0 ? InvertOrientation(NaturalA) : NaturalA;
                const Orientation SenseB   = FlippedContacts.count({ Sheet.Edge.Slot, 1 }) != 0 ? InvertOrientation(NaturalB) : NaturalB;
                const bool        CliffA   = Cliffs.count({ Sheet.Edge.Slot, 0 }) != 0;
                const bool        CliffB   = Cliffs.count({ Sheet.Edge.Slot, 1 }) != 0;
                const BlueprintEdge& BlockA = Blueprint.Edges[PieceA];
                const BlueprintEdge& BlockB = Blueprint.Edges[PieceB];
                // 📝 Spine τ of the ends from a contact piece (parameterised by τ); a cliff section is not, so use the other
                //    side, or project the end vertices when both sides run off their supports (open edges only).
                double SpineX = NaturalA == Orientation::Forward ? BlockA.Low : BlockA.High;   // [-] - τ of the X end
                double SpineY = NaturalA == Orientation::Forward ? BlockA.High : BlockA.Low;   // [-] - τ of the Y end
                if (CliffA && !CliffB)
                {
                    SpineX = NaturalB == Orientation::Forward ? BlockB.High : BlockB.Low;
                    SpineY = NaturalB == Orientation::Forward ? BlockB.Low : BlockB.High;
                }
                else if (CliffA && CliffB)
                {
                    if (Profile.Start == Profile.End) return "blend of " + LabelToken("E", Sheet.Edge) + ": closed rim wider than both supports (🚧)";
                    SpineX = ProjectOntoCurve(Sheet.Spine, PositionOf(ContactVertices[{ Sheet.Edge.Slot, 0, XEnd }]), 0.0);
                    SpineY = ProjectOntoCurve(Sheet.Spine, PositionOf(ContactVertices[{ Sheet.Edge.Slot, 0, YEnd }]), 0.0);
                }
                const double MidA = CliffA ? 0.5 * (SpineX + SpineY) : 0.5 * (BlockA.Low + BlockA.High);   // [-] - Chart reference τ
                const double MidB = CliffB ? 0.5 * (SpineX + SpineY) : 0.5 * (BlockB.Low + BlockB.High);
                const auto Unwrap = [&](Vector3 Point, double Expected)
                {
                    double τ = ProjectOntoCurve(Sheet.Spine, Point, 0.0);
                    if (PeriodicCondition(Sheet.Spine)) τ += 2.0 * std::numbers::pi * std::round((Expected - τ) / (2.0 * std::numbers::pi));
                    return τ;
                };
                const auto Reference = [&](int Edge, double Expected)
                {
                    const BlueprintEdge& Block = Blueprint.Edges[Edge];
                    return ResolveSheetChart(Sheet, Unwrap(EvaluateCurve(Block.Curve, 0.5 * (Block.Low + Block.High)).Position, Expected));
                };
                BlueprintFace Blue{ Sheet.Surface, Sheet.Sense, {} };
                std::vector<BlueprintUse> Uses;
                Uses.push_back({ PieceA, InvertOrientation(SenseA), FitUse(Sheet.Surface, PieceA, ResolveSheetChart(Sheet, MidA)) });
                for (const ChainLink& Link : Terminations[{ Sheet.Edge.Slot, XEnd }])
                {
                    Uses.push_back({ Link.Edge, LinkSense(Link), FitUse(Sheet.Surface, Link.Edge, Reference(Link.Edge, SpineX)) });
                }
                Uses.push_back({ PieceB, InvertOrientation(SenseB), FitUse(Sheet.Surface, PieceB, ResolveSheetChart(Sheet, MidB)) });
                const std::vector<ChainLink>& Back = Terminations[{ Sheet.Edge.Slot, YEnd }];
                for (auto Link = Back.rbegin(); Link != Back.rend(); ++Link)
                {
                    const ChainLink Reversed{ Link->Edge, Link->To, Link->From };
                    Uses.push_back({ Reversed.Edge, LinkSense(Reversed), FitUse(Sheet.Surface, Reversed.Edge, Reference(Reversed.Edge, SpineY)) });
                }
                if (std::string Refusal = DiagnoseLoop(Uses, "blend of " + LabelToken("E", Sheet.Edge)); !Refusal.empty()) return Refusal;
                Blue.Loops.push_back(std::move(Uses));
                Blueprint.Faces.push_back(std::move(Blue));
                Provenance.push_back({ {}, BlendFaceOrigin::Edge, {}, Sheet.Edge, {} });
                if (std::holds_alternative<PlaneSurface>(Sheet.Surface))         ++Outcome.Statistics.CarrierPlanes;
                else if (std::holds_alternative<CylinderSurface>(Sheet.Surface)) ++Outcome.Statistics.CarrierCylinders;
                else if (std::holds_alternative<ConeSurface>(Sheet.Surface))     ++Outcome.Statistics.CarrierCones;
                else if (std::holds_alternative<TorusSurface>(Sheet.Surface))    ++Outcome.Statistics.CarrierTori;
                else if (std::holds_alternative<RollingBallSurface>(Sheet.Surface)) ++Outcome.Statistics.CarrierProcedural;
            }

            // ③ Corner faces: the end sections chained into one loop.
            for (CornerPatch& Patch : Corners)
            {
                std::vector<ChainLink> Order = { Patch.Links.front() };
                std::vector<bool>      Taken(Patch.Links.size(), false);
                Taken[0] = true;
                while (Order.size() < Patch.Links.size())
                {
                    bool Found = false;
                    for (size_t Index = 0; Index < Patch.Links.size() && !Found; ++Index)
                    {
                        if (!Taken[Index] && Patch.Links[Index].From == Order.back().To) { Order.push_back(Patch.Links[Index]); Taken[Index] = true; Found = true; }
                    }
                    if (!Found) return "corner at " + LabelToken("V", Patch.Vertex) + ": end sections do not chain";
                }
                BlueprintFace Blue{ Patch.Surface, Patch.Sense, {} };
                std::vector<BlueprintUse> Uses;
                for (const ChainLink& Link : Order) Uses.push_back({ Link.Edge, LinkSense(Link), FitUse(Patch.Surface, Link.Edge, Patch.Reference) });
                if (std::string Refusal = DiagnoseLoop(Uses, "corner at " + LabelToken("V", Patch.Vertex)); !Refusal.empty()) return Refusal;
                Blue.Loops.push_back(std::move(Uses));
                Blueprint.Faces.push_back(std::move(Blue));
                Provenance.push_back({ {}, BlendFaceOrigin::Vertex, {}, {}, Patch.Vertex });
            }
            return {};
        }
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

static BlendOutcome BlendEdgesUntraced(ModelPartition& Partition, BodyToken Body, const std::vector<EdgeToken>& Edges, const BlendSettings& Settings)
{
    const auto   Started = std::chrono::steady_clock::now();
    BlendOutcome Outcome;
    const auto   Finish  = [&](std::string Refusal)
    {
        Outcome.Refusal = Refusal.empty() ? std::string{} : "Blend: " + Refusal;
        Outcome.Statistics.Milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Started).count();
        return Outcome;
    };
    BlendRebuild Rebuild(Partition, Body, Settings, Outcome);
    const std::vector<EdgeToken> Selection = Settings.TangentChainEnabled ? GrowTangentChain(Partition, Body, Edges) : Edges;
    if (std::string Refusal = Rebuild.Run(Selection); !Refusal.empty()) return Finish(Refusal);

    // ⑤ Assembly and the atomic soundness gate (topology + geometry audit, genus unchanged).
    const AssemblyOutcome Assembly = AssembleBody(Partition, Rebuild.Blueprint, "Blend");
    if (!Assembly.Refusal.empty()) return Finish(Assembly.Refusal);
    Outcome.Audit = AuditBody(Partition, Assembly.Body);
    if (!Outcome.Audit.SoundCondition())
    {
        const AuditFinding& First = Outcome.Audit.Findings.front();
        return Finish("result fails audit " + std::string(First.Code) + " on " + First.Subject + " (" + First.Detail + ")");
    }
    // 💡 Genus is Euler–Poincaré on topology (set before the T-scope return): the G-checks of the input body were wasted.
    const int Genus = AuditBody(Partition, Body, AuditScope::Topology).Genus;
    if (Outcome.Audit.Genus != Genus) return Finish("genus changed from " + std::to_string(Genus) + " to " + std::to_string(Outcome.Audit.Genus));
    // ⑥ Blends that reach each other (footprints overlapping on a shared face, tubes cutting through a neighbour) pass
    //    the local audit but leave the body passing through itself: refuse instead of returning it.
    if (std::string Clash = DetectSelfClash(Partition, Assembly.Body); !Clash.empty()) return Finish("neighbouring blends overlap: " + Clash);
    for (size_t Index = 0; Index < Assembly.Faces.size(); ++Index)
    {
        BlendFaceHistory History = Rebuild.Provenance[Index];
        History.Result = Assembly.Faces[Index];
        Outcome.FaceHistories.push_back(History);
    }
    Outcome.Body = Assembly.Body;
    return Finish({});
}

// 📝 Traced entry (Diagnostics/OperationTrace.h): the request on entry, the outcome before the duration line.

BlendOutcome BlendEdges(ModelPartition& Partition, BodyToken Body, const std::vector<EdgeToken>& Edges, const BlendSettings& Settings)
{
    FRONTIER_TRACE_SCOPE("Blend", std::string(Settings.Profile == BlendProfile::Fillet ? "Fillet" : "Chamfer") + " · " + std::to_string(Edges.size()) +
                                  " edge(s) · size " + TraceNumber(Settings.Size) + (Settings.EndSize ? " → " + TraceNumber(*Settings.EndSize) : std::string()));
    BlendOutcome Outcome = BlendEdgesUntraced(Partition, Body, Edges, Settings);
    FRONTIER_TRACE("Blend", Outcome.Refusal.empty() ? std::string("built · ") + std::to_string(Outcome.Audit.Findings.size()) + " audit finding(s)"
                                                    : "refused: " + Outcome.Refusal);
    return Outcome;
}


const char* VertexBlendCategoryLabel(VertexBlendCategory Category) noexcept
{
    switch (Category)
    {
        case VertexBlendCategory::Cap:          return "Cap";
        case VertexBlendCategory::Mitre:        return "Mitre";
        case VertexBlendCategory::BallCorner:   return "BallCorner";
        case VertexBlendCategory::Continuation: return "Continuation";
        case VertexBlendCategory::Pinch:        return "Pinch";
        case VertexBlendCategory::Setback:      return "Setback";
    }
    return "?";
}

const char* BlendFaceOriginLabel(BlendFaceOrigin Origin) noexcept
{
    switch (Origin)
    {
        case BlendFaceOrigin::Modified: return "Modified";
        case BlendFaceOrigin::Edge:     return "Edge";
        case BlendFaceOrigin::Vertex:   return "Vertex";
    }
    return "?";
}

} // namespace Frontier::GeometrySpace::Parametric

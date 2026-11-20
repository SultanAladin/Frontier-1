/*====================================================================================================================================
                                                         MODELAUDIT.CPP
====================================================================================================================================*/
// 🧩 Tolerance-aware topological and geometric audit of a body — the gate every operation result must pass

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int    CurveSampleCount     = 9;                                               // [-] - Samples per edge / fin
    constexpr int    GapSampleCount       = 7;                                               // [-] - Samples across a (𝑢, 𝑣) gap
    constexpr double ParameterGapBoundary = 1.0e-9;                                          // [-] - (𝑢, 𝑣) gap treated as closed

    class AuditLedger
    {
    public:
        explicit AuditLedger(AuditReport& Target) : Report(Target) {}

        size_t Open(const char* Code, const char* Description)
        {
            Report.Tallies.push_back({ Code, Description, 0, 0 });
            return Report.Tallies.size() - 1;
        }

        void Examine(size_t Check) { Report.Tallies[Check].Examined += 1; }

        void Fail(size_t Check, std::string Subject, std::string Detail, double Measured = 0.0, double Allowed = 0.0)
        {
            Report.Tallies[Check].Failed += 1;
            Report.Findings.push_back({ Report.Tallies[Check].Code, std::move(Subject), std::move(Detail), Measured, Allowed });
        }

        [[nodiscard]] int FailedCount(size_t Check) const { return Report.Tallies[Check].Failed; }

    private:
        AuditReport& Report;
    };

    template <typename Token>
    void AppendUnique(std::vector<Token>& Sequence, Token Candidate)
    {
        if (std::find(Sequence.begin(), Sequence.end(), Candidate) == Sequence.end()) Sequence.push_back(Candidate);
    }

    [[nodiscard]] double SampleParameter(double Low, double High, int SampleIndex, int SampleCount)
    {
        return Low + (High - Low) * static_cast<double>(SampleIndex) / static_cast<double>(SampleCount - 1);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

AuditReport AuditBody(const ModelPartition& Partition, BodyToken Body, AuditScope Scope)
{
    AuditReport Report;
    AuditLedger Ledger(Report);
    BodyInventory Inventory;

    // ---- T1 token links: every reference authenticates and every back-link agrees.
    const size_t TokenCheck = Ledger.Open("T1", "Token links authenticate and back-links agree");
    Ledger.Examine(TokenCheck);
    if (!Partition.Bodies.Authenticate(Body))
    {
        Ledger.Fail(TokenCheck, LabelToken("B", Body), "stale body token");
        return Report;
    }
    for (RegionToken Region : Partition.Bodies.Resolve(Body).Regions)
    {
        Ledger.Examine(TokenCheck);
        if (!Partition.Regions.Authenticate(Region)) { Ledger.Fail(TokenCheck, LabelToken("R", Region), "stale region token"); continue; }
        if (!(Partition.Regions.Resolve(Region).Body == Body)) Ledger.Fail(TokenCheck, LabelToken("R", Region), "region names another body");
        Inventory.Regions.push_back(Region);
        for (SolidShellToken Shell : Partition.Regions.Resolve(Region).Shells)
        {
            Ledger.Examine(TokenCheck);
            if (!Partition.Shells.Authenticate(Shell)) { Ledger.Fail(TokenCheck, LabelToken("S", Shell), "stale shell token"); continue; }
            if (!(Partition.Shells.Resolve(Shell).Region == Region)) Ledger.Fail(TokenCheck, LabelToken("S", Shell), "shell names another region");
            Inventory.Shells.push_back(Shell);
            for (FaceToken Face : Partition.Shells.Resolve(Shell).Faces)
            {
                Ledger.Examine(TokenCheck);
                if (!Partition.Faces.Authenticate(Face)) { Ledger.Fail(TokenCheck, LabelToken("F", Face), "stale face token"); continue; }
                if (!(Partition.Faces.Resolve(Face).Shell == Shell)) Ledger.Fail(TokenCheck, LabelToken("F", Face), "face names another shell");
                Inventory.Faces.push_back(Face);
                for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
                {
                    Ledger.Examine(TokenCheck);
                    if (!Partition.Loops.Authenticate(Loop)) { Ledger.Fail(TokenCheck, LabelToken("L", Loop), "stale loop token"); continue; }
                    if (!(Partition.Loops.Resolve(Loop).Face == Face)) Ledger.Fail(TokenCheck, LabelToken("L", Loop), "loop names another face");
                    Inventory.Loops.push_back(Loop);
                    const LoopProfile& Cycle = Partition.Loops.Resolve(Loop);
                    if (Cycle.IsolatedVertex.PresentCondition())
                    {
                        if (Partition.Vertices.Authenticate(Cycle.IsolatedVertex)) AppendUnique(Inventory.Vertices, Cycle.IsolatedVertex);
                        else Ledger.Fail(TokenCheck, LabelToken("L", Loop), "stale isolated vertex");
                    }
                    if (!Cycle.FirstFin.PresentCondition()) continue;
                    FinToken     Cursor     = Cycle.FirstFin;                                // [-] - Walking fin
                    const size_t GuardLimit = Partition.Fins.OccupiedCount() + 1;            // [-] - Upper bound on a cycle
                    for (size_t GuardCount = 0; GuardCount < GuardLimit; ++GuardCount)
                    {
                        Ledger.Examine(TokenCheck);
                        if (!Partition.Fins.Authenticate(Cursor)) { Ledger.Fail(TokenCheck, LabelToken("L", Loop), "stale fin in cycle"); break; }
                        const FinProfile& Use = Partition.Fins.Resolve(Cursor);
                        if (!(Use.Loop == Loop)) Ledger.Fail(TokenCheck, LabelToken("Fn", Cursor), "fin names another loop");
                        if (!Partition.Edges.Authenticate(Use.Edge)) { Ledger.Fail(TokenCheck, LabelToken("Fn", Cursor), "stale edge"); break; }
                        const EdgeProfile& Edge = Partition.Edges.Resolve(Use.Edge);
                        if (!Partition.Vertices.Authenticate(Edge.Start) || !Partition.Vertices.Authenticate(Edge.End))
                        {
                            Ledger.Fail(TokenCheck, LabelToken("E", Use.Edge), "stale end vertex");
                            break;
                        }
                        Inventory.Fins.push_back(Cursor);
                        AppendUnique(Inventory.Edges, Use.Edge);
                        AppendUnique(Inventory.Vertices, Edge.Start);
                        AppendUnique(Inventory.Vertices, Edge.End);
                        Cursor = Use.Successor;
                        if (!Cursor.PresentCondition() || Cursor == Cycle.FirstFin) break;
                    }
                }
            }
        }
    }
    if (Ledger.FailedCount(TokenCheck) > 0) return Report;

    // ---- T2 loop cycles: successor/predecessor inverse, vertex continuity, fin-less loops carry a vertex.
    const size_t CycleCheck = Ledger.Open("T2", "Loop cycles close and consecutive fins share vertices");
    for (LoopToken Loop : Inventory.Loops)
    {
        Ledger.Examine(CycleCheck);
        const LoopProfile& Cycle = Partition.Loops.Resolve(Loop);
        if (Cycle.FirstFin.PresentCondition() == Cycle.IsolatedVertex.PresentCondition())
        {
            Ledger.Fail(CycleCheck, LabelToken("L", Loop), "loop must have either fins or one isolated vertex");
            continue;
        }
        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
        {
            const FinProfile& Use = Partition.Fins.Resolve(Fin);
            if (!Partition.Fins.Authenticate(Use.Successor) || !(Partition.Fins.Resolve(Use.Successor).Predecessor == Fin))
            {
                Ledger.Fail(CycleCheck, LabelToken("Fn", Fin), "successor's predecessor is not this fin");
                return;
            }
            if (!(Partition.FinEnd(Fin) == Partition.FinStart(Use.Successor)))
            {
                Ledger.Fail(CycleCheck, LabelToken("Fn", Fin), "fin end vertex differs from successor start vertex");
            }
        });
    }

    // ---- T3 edge uses: a solid edge has exactly two fins, of opposite sense, in one shell; a sheet edge may instead be
    //      laminar (a one-fin ring on the sheet boundary).
    const bool   SheetCondition = Partition.Bodies.Resolve(Body).Category == BodyCategory::Sheet;
    const size_t UseCheck       = Ledger.Open("T3", SheetCondition ? "Each edge has two opposite fins in one shell, or one (laminar)" : "Each edge has two opposite fins in one shell");
    std::vector<EdgeToken> LaminarEdges;
    std::map<uint32_t, int> InventoryUse;                                                    // [-] - Fin count per edge slot
    for (FinToken Fin : Inventory.Fins) InventoryUse[Partition.Fins.Resolve(Fin).Edge.Slot] += 1;
    for (EdgeToken Edge : Inventory.Edges)
    {
        Ledger.Examine(UseCheck);
        const FinToken First  = Partition.Edges.Resolve(Edge).RadialFin;                     // [-] - Ring entry
        if (!Partition.Fins.Authenticate(First)) { Ledger.Fail(UseCheck, LabelToken("E", Edge), "stale radial fin"); continue; }
        const FinToken Second = Partition.Fins.Resolve(First).RadialSuccessor;               // [-] - Ring mate
        if (SheetCondition && Second == First && InventoryUse[Edge.Slot] == 1)
        {
            if (!(Partition.Fins.Resolve(First).Edge == Edge)) Ledger.Fail(UseCheck, LabelToken("E", Edge), "ring fin names another edge");
            LaminarEdges.push_back(Edge);
            continue;
        }
        if (!Partition.Fins.Authenticate(Second) || !(Partition.Fins.Resolve(Second).RadialSuccessor == First) || First == Second)
        {
            Ledger.Fail(UseCheck, LabelToken("E", Edge), "radial ring is not a two-fin ring");
            continue;
        }
        const FinProfile& FirstUse  = Partition.Fins.Resolve(First);
        const FinProfile& SecondUse = Partition.Fins.Resolve(Second);
        if (!(FirstUse.Edge == Edge) || !(SecondUse.Edge == Edge)) Ledger.Fail(UseCheck, LabelToken("E", Edge), "ring fin names another edge");
        if (FirstUse.Sense == SecondUse.Sense) Ledger.Fail(UseCheck, LabelToken("E", Edge), "both fins run the same way");
        if (InventoryUse[Edge.Slot] != 2) Ledger.Fail(UseCheck, LabelToken("E", Edge), "edge is not used by exactly two reachable fins");
        const SolidShellToken FirstShell  = Partition.Faces.Resolve(Partition.Loops.Resolve(FirstUse.Loop).Face).Shell;
        const SolidShellToken SecondShell = Partition.Faces.Resolve(Partition.Loops.Resolve(SecondUse.Loop).Face).Shell;
        if (!(FirstShell == SecondShell)) Ledger.Fail(UseCheck, LabelToken("E", Edge), "fins lie in different shells");
    }

    // ---- T4 Euler–Poincaré: V − E + 2F − L = 2(S − G) − B must give an integral, non-negative genus (B = boundary
    //      cycles of laminar edges, 0 for a solid).
    const size_t EulerCheck = Ledger.Open("T4", "Euler-Poincare gives an integral genus >= 0");
    Ledger.Examine(EulerCheck);
    std::map<uint32_t, uint32_t> BoundaryRoot;                                               // [-] - Union-find over laminar vertices
    const std::function<uint32_t(uint32_t)> FindRoot = [&](uint32_t Slot) { return BoundaryRoot[Slot] == Slot ? Slot : BoundaryRoot[Slot] = FindRoot(BoundaryRoot[Slot]); };
    for (EdgeToken Edge : LaminarEdges)
    {
        for (VertexToken Vertex : { Partition.Edges.Resolve(Edge).Start, Partition.Edges.Resolve(Edge).End }) BoundaryRoot.try_emplace(Vertex.Slot, Vertex.Slot);
        BoundaryRoot[FindRoot(Partition.Edges.Resolve(Edge).Start.Slot)] = FindRoot(Partition.Edges.Resolve(Edge).End.Slot);
    }
    long BoundaryCycles = 0;                                                                 // [-] - B
    for (const auto& [Slot, Root] : BoundaryRoot) BoundaryCycles += FindRoot(Slot) == Slot;
    const long Characteristic = static_cast<long>(Inventory.Vertices.size()) - static_cast<long>(Inventory.Edges.size()) +
                                2L * static_cast<long>(Inventory.Faces.size()) - static_cast<long>(Inventory.Loops.size()) + BoundaryCycles;
    const long ShellCount = static_cast<long>(Inventory.Shells.size());                      // [-] - S
    if (Characteristic % 2 != 0 || ShellCount - Characteristic / 2 < 0)
    {
        Ledger.Fail(EulerCheck, LabelToken("B", Body), "V - E + 2F - L = " + std::to_string(Characteristic) + " has no valid genus");
    }
    else
    {
        Report.Genus = static_cast<int>(ShellCount - Characteristic / 2);
    }

    // ---- T5 precision order (Jackson 1995): a vertex tolerance covers the tolerances of its edges.
    const size_t OrderCheck = Ledger.Open("T5", "Vertex precision >= precision of incident edges");
    for (EdgeToken Edge : Inventory.Edges)
    {
        Ledger.Examine(OrderCheck);
        const EdgeProfile& Profile  = Partition.Edges.Resolve(Edge);
        const double       EdgeTube = ResolvePrecision(Profile.Precision);                   // [len] - Edge tube radius
        for (VertexToken Vertex : { Profile.Start, Profile.End })
        {
            const double Sphere = ResolvePrecision(Partition.Vertices.Resolve(Vertex).Precision);  // [len] - Vertex sphere radius
            if (Sphere < EdgeTube) Ledger.Fail(OrderCheck, LabelToken("V", Vertex), "vertex precision below edge " + LabelToken("E", Edge), Sphere, EdgeTube);
        }
    }

    if (Scope == AuditScope::Topology) return Report;

    // ---- T6 geometry attached (Complete scope only: mid-construction topology legitimately lacks geometry).
    const size_t AttachCheck = Ledger.Open("T6", "Surfaces, curves and SP-curves attached");
    for (FaceToken Face : Inventory.Faces)
    {
        Ledger.Examine(AttachCheck);
        if (!Partition.Surfaces.Authenticate(Partition.Faces.Resolve(Face).Surface)) Ledger.Fail(AttachCheck, LabelToken("F", Face), "no surface");
    }
    for (EdgeToken Edge : Inventory.Edges)
    {
        Ledger.Examine(AttachCheck);
        if (!Partition.Curves.Authenticate(Partition.Edges.Resolve(Edge).Curve)) Ledger.Fail(AttachCheck, LabelToken("E", Edge), "no curve");
    }
    for (FinToken Fin : Inventory.Fins)
    {
        Ledger.Examine(AttachCheck);
        if (!Partition.Fins.Resolve(Fin).SurfaceCurve.has_value()) Ledger.Fail(AttachCheck, LabelToken("Fn", Fin), "no SP-curve");
    }
    if (Report.Findings.size() > 0) return Report;

    // ---- G1 vertex on curve: curve ends lie within the vertex sphere plus the edge tube.
    const size_t VertexCheck = Ledger.Open("G1", "Edge curve ends lie on their vertices");
    for (EdgeToken Edge : Inventory.Edges)
    {
        const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
        const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
        const double         Tube    = ResolvePrecision(Profile.Precision);                  // [len] - Edge tube radius
        const std::pair<VertexToken, double> Ends[] = { { Profile.Start, Profile.IntervalLow }, { Profile.End, Profile.IntervalHigh } };
        for (const auto& [Vertex, τ] : Ends)
        {
            Ledger.Examine(VertexCheck);
            const VertexProfile& Point   = Partition.Vertices.Resolve(Vertex);
            const double         Gap     = Distance(EvaluateCurve(Curve, τ).Position, Point.Position);  // [len] - Curve end to vertex
            const double         Allowed = ResolvePrecision(Point.Precision) + Tube;         // [len] - Sphere + tube
            if (Gap > Allowed) Ledger.Fail(VertexCheck, LabelToken("E", Edge), "curve end misses " + LabelToken("V", Vertex), Gap, Allowed);
        }
    }

    // ---- G2 curve on surface and G3 SP-curve agreement, per fin.
    const size_t OnSurfaceCheck = Ledger.Open("G2", "Edge curves lie on the surfaces of their faces");
    const size_t AgreementCheck = Ledger.Open("G3", "SP-curves map onto their edge curves at equal parameter");
    for (FinToken Fin : Inventory.Fins)
    {
        const FinProfile&      Use     = Partition.Fins.Resolve(Fin);
        const EdgeProfile&     Edge    = Partition.Edges.Resolve(Use.Edge);
        const CurveGeometry&   Curve   = Partition.Curves.Resolve(Edge.Curve);
        const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Partition.Loops.Resolve(Use.Loop).Face).Surface);
        const double           Tube    = ResolvePrecision(Edge.Precision);                   // [len] - Edge tube radius
        double WorstOnSurface = 0.0;                                                         // [len] - Max curve-to-surface distance
        double WorstAgreement = 0.0;                                                         // [len] - Max SP-curve-to-curve distance
        for (int SampleIndex = 0; SampleIndex < CurveSampleCount; ++SampleIndex)
        {
            const double  τ          = SampleParameter(Edge.IntervalLow, Edge.IntervalHigh, SampleIndex, CurveSampleCount);
            const Vector3 OnCurve    = EvaluateCurve(Curve, τ).Position;                     // [len] - Curve point
            const Vector2 Chart      = EvaluateParameterCurve(*Use.SurfaceCurve, τ).Position; // [-]  - SP-curve point
            Vector2       Projection = ProjectOntoSurfaceNear(Surface, OnCurve, Chart);      // [-]  - Closest chart point (seeded)
            double        SurfaceGap = Distance(EvaluateSurface(Surface, Projection).Position, OnCurve);
            if (SurfaceGap > Tube)
            {
                Projection = ProjectOntoSurface(Surface, OnCurve, Chart);
                SurfaceGap = Distance(EvaluateSurface(Surface, Projection).Position, OnCurve);
            }
            WorstOnSurface = std::max(WorstOnSurface, SurfaceGap);
            WorstAgreement = std::max(WorstAgreement, Distance(EvaluateSurface(Surface, Chart).Position, OnCurve));
        }
        Ledger.Examine(OnSurfaceCheck);
        Ledger.Examine(AgreementCheck);
        if (WorstOnSurface > Tube) Ledger.Fail(OnSurfaceCheck, LabelToken("Fn", Fin), "edge " + LabelToken("E", Use.Edge) + " leaves the face surface", WorstOnSurface, Tube);
        if (WorstAgreement > Tube) Ledger.Fail(AgreementCheck, LabelToken("Fn", Fin), "SP-curve disagrees with edge " + LabelToken("E", Use.Edge), WorstAgreement, Tube);
    }

    // ---- G4 SP-loop closure: consecutive SP-curves meet in (𝑢, 𝑣), or across a singular row that maps to the vertex.
    const size_t ClosureCheck = Ledger.Open("G4", "SP-curve loops close in (u, v) or across a singular row");
    for (LoopToken Loop : Inventory.Loops)
    {
        const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Partition.Loops.Resolve(Loop).Face).Surface);
        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
        {
            Ledger.Examine(ClosureCheck);
            const FinToken      Successor = Partition.Fins.Resolve(Fin).Successor;           // [-] - Next fin
            const Vector2       Arrival   = EvaluateParameterCurve(*Partition.Fins.Resolve(Fin).SurfaceCurve, Partition.FinParameterEnd(Fin)).Position;
            const Vector2       Departure = EvaluateParameterCurve(*Partition.Fins.Resolve(Successor).SurfaceCurve, Partition.FinParameterStart(Successor)).Position;
            const VertexProfile& Corner   = Partition.Vertices.Resolve(Partition.FinEnd(Fin));
            const double        Allowed   = ResolvePrecision(Corner.Precision) + ResolvePrecision(Partition.Edges.Resolve(Partition.Fins.Resolve(Fin).Edge).Precision);
            if (Distance(Arrival, Departure) <= ParameterGapBoundary) return;
            double WorstGap = 0.0;                                                           // [len] - Max gap-row distance to the vertex
            for (int SampleIndex = 0; SampleIndex < GapSampleCount; ++SampleIndex)
            {
                const Vector2 Across = Arrival + (Departure - Arrival) * (static_cast<double>(SampleIndex) / (GapSampleCount - 1));
                WorstGap = std::max(WorstGap, Distance(EvaluateSurface(Surface, Across).Position, Corner.Position));
            }
            if (WorstGap > Allowed) Ledger.Fail(ClosureCheck, LabelToken("Fn", Fin), "SP-curve gap to the next fin is not a singular row", WorstGap, Allowed);
        });
    }

    // ---- G5 face orientation: the loops' parametric winding agrees with the face sense.
    const size_t OrientationCheck = Ledger.Open("G5", "Loop winding in (u, v) agrees with face sense");
    for (FaceToken Face : Inventory.Faces)
    {
        Ledger.Examine(OrientationCheck);
        const double Area     = IntegrateParametricArea(Partition, Face);                    // [-] - Σ ∮ 𝑢 d𝑣
        const double Expected = Partition.Faces.Resolve(Face).Sense == Orientation::Forward ? 1.0 : -1.0;
        if (!(Area * Expected > 0.0)) Ledger.Fail(OrientationCheck, LabelToken("F", Face), "parametric area has the wrong sign for the face sense", Area, 0.0);
    }

    // ---- G6 enclosed volume: every shell encloses non-zero volume; each solid region encloses positive volume.
    const size_t VolumeCheck = Ledger.Open("G6", "Shells enclose volume; solid regions are positive");
    for (RegionToken Region : Inventory.Regions)
    {
        const RegionProfile& Profile = Partition.Regions.Resolve(Region);
        if (Profile.Material != RegionMaterial::Solid) continue;
        double RegionVolume = 0.0;                                                           // [len³] - Σ shell volumes
        for (SolidShellToken SolidShell : Profile.Shells)
        {
            Ledger.Examine(VolumeCheck);
            // 📝 Relative to Σ|face flux|: a flat shell cancels to round-off at any size or distance from the origin.
            const SolidVolumeMeasure Measure = MeasureShellVolume(Partition, SolidShell);
            const double ShellVolume = Measure.Volume;                                        // [len³] - Signed shell volume
            Report.Volume += ShellVolume;                                                    // Same shells, same order as IntegrateBodyVolume
            if (!(std::fabs(ShellVolume) > 1.0e-10 * Measure.FluxMagnitude)) Ledger.Fail(VolumeCheck, LabelToken("S", SolidShell), "shell encloses no volume", ShellVolume, 0.0);
            RegionVolume += ShellVolume;
        }
        Ledger.Examine(VolumeCheck);
        if (!(RegionVolume > 0.0)) Ledger.Fail(VolumeCheck, LabelToken("R", Region), "solid region volume is not positive", RegionVolume, 0.0);
    }
    Report.VolumeCondition = true;
    return Report;
}

} // namespace Frontier::GeometrySpace::Parametric

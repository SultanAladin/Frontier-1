/*====================================================================================================================================
                                                           SECTIONLOFT.CPP
====================================================================================================================================*/
// 🧩 Loft through sections: knot merging, chord-averaged 𝑣ₖ, homogeneous interpolation, sheet assembly with iso-line charts

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Surfacing/SectionLoft.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/LinearSystem.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/SurfaceAlgebra.h"
#include "Engine/GeometrySpace/Parametric/Construction/BodyArrangement.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIVATE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double ClosureGap      = 1.0e-9;                                               // [len] - First = last pole: a closed section
    constexpr double ContinuityMatch = 1.0e-9;                                               // [len] - Shared section vs reference boundary (relative)

    // Affine τ′ = (τ − Low) / (High − Low): the same curve on [0, 1] (knots move, poles and weights stay).
    [[nodiscard]] NurbsCurveDefinition MapOntoUnit(const NurbsCurveDefinition& Definition)
    {
        NurbsCurveDefinition Result = Definition;
        const NurbsDomain Domain = MeasureNurbsCurveDomain(Definition);
        const double Width = Domain.High - Domain.Low;
        for (double& Knot : Result.Knots) Knot = (Knot - Domain.Low) / Width;
        for (int Index = 0; Index <= Result.Degree; ++Index)
        {
            Result.Knots[Index] = 0.0;
            Result.Knots[Result.Knots.size() - 1 - Index] = 1.0;
        }
        return Result;
    }

    [[nodiscard]] bool ClosedSection(const NurbsCurveDefinition& Definition)
    {
        return Length(Definition.Poles.front() - Definition.Poles.back()) <= ClosureGap;
    }

    [[nodiscard]] ParameterLine IsoLine(Vector2 Origin, Vector2 Direction) { return ParameterLine{ Origin, Direction }; }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

LoftSurfaceOutcome LoftSectionSurface(const std::vector<NurbsCurve>& Sections, const LoftSettings& Settings)
{
    LoftSurfaceOutcome Outcome;
    auto Refuse = [&](const std::string& Reason) { Outcome.Refusal = "Loft: " + Reason; return Outcome; };
    if (Sections.size() < 2) return Refuse("needs at least 2 sections");

    // ① Each section on [0, 1]; all open or all closed; open ones aligned with the first; lower degrees raised exactly.
    std::vector<NurbsCurveDefinition> Mapped;
    for (size_t Index = 0; Index < Sections.size(); ++Index)
    {
        if (!Sections[Index].Definition) return Refuse("section " + std::to_string(Index) + " is empty");
        // A closed carrier flagged periodic but with clamped ends (e.g. a full circle) is used as a closed clamped section.
        const NurbsCurveDefinition& Definition = *Sections[Index].Definition;
        const size_t Last = Definition.Knots.size() - 1;
        const bool   Clamped = Definition.Knots[0] == Definition.Knots[Definition.Degree] && Definition.Knots[Last] == Definition.Knots[Last - Definition.Degree];
        if (Definition.Period > 0.0 && !Clamped) return Refuse("section " + std::to_string(Index) + " is periodic with unclamped knots (🚧)");
        Mapped.push_back(MapOntoUnit(Definition));
        Mapped.back().Period = 0.0;
    }
    Outcome.ClosedCondition = ClosedSection(Mapped.front());
    for (size_t Index = 1; Index < Mapped.size(); ++Index)
    {
        if (ClosedSection(Mapped[Index]) != Outcome.ClosedCondition) return Refuse("open and closed sections mixed (section " + std::to_string(Index) + ")");
    }
    if (Settings.AlignCondition && !Outcome.ClosedCondition)
    {
        for (size_t Index = 1; Index < Mapped.size(); ++Index)
        {
            const Vector3 PrevStart = Mapped[Index - 1].Poles.front();
            const Vector3 PrevEnd   = Mapped[Index - 1].Poles.back();
            const Vector3 CurStart  = Mapped[Index].Poles.front();
            const Vector3 CurEnd    = Mapped[Index].Poles.back();
            const Vector3 PrevChord = PrevEnd - PrevStart;
            const Vector3 CurChord  = CurEnd - CurStart;
            const double  EndpointGap = std::fmax(5.0e-5, 1.0e-4 * std::fmin(Length(PrevChord), Length(CurChord)));
            const double  SameStart = Length(CurStart - PrevStart);
            const double  SameEnd   = Length(CurEnd - PrevEnd);
            const double  SwapStart = Length(CurStart - PrevEnd);
            const double  SwapEnd   = Length(CurEnd - PrevStart);
            const double  SameMin   = std::fmin(SameStart, SameEnd);
            const double  SwapMin   = std::fmin(SwapStart, SwapEnd);
            bool ReverseCondition = false;
            if (SameMin <= EndpointGap && SwapMin > EndpointGap)
            {
                ReverseCondition = false;                                                    // Shared endpoint already on the same rail
            }
            else if (SwapMin <= EndpointGap && SameMin > EndpointGap)
            {
                ReverseCondition = true;                                                     // Shared endpoint pairs cleanly once reversed
            }
            else if (SameMin <= EndpointGap && SwapMin <= EndpointGap)
            {
                ReverseCondition = std::fmax(SwapStart, SwapEnd) < std::fmax(SameStart, SameEnd);
            }
            else
            {
                const Vector3 Reference = Mapped.front().Poles.back() - Mapped.front().Poles.front();
                ReverseCondition = Dot(CurChord, Length(PrevChord) > 1.0e-12 ? PrevChord : Reference) < 0.0;
            }
            if (!ReverseCondition) continue;
            Mapped[Index] = MapOntoUnit(*ReverseNurbsCurve(Mapped[Index]).Definition);
            ++Outcome.Reversed;
        }
    }

    // ①′ Continuity references: turned so the end section is their 𝑣 = 0 or 𝑣 = 1 row running with 𝑢 (transposed /
    //     reversed as needed); σ records the side. A reference that does not carry the section as a boundary refuses.
    struct ContinuityEnd
    {
        int                    Order     = 0;
        NurbsSurfaceDefinition Reference = {};
        double                 Boundary  = 0.0;                                              // [-] - 𝑣 of the shared row on the reference
    };
    std::array<ContinuityEnd, 2> Ends;
    for (int Side = 0; Side < 2; ++Side)
    {
        const LoftContinuity& Continuity = Side == 0 ? Settings.Start : Settings.End;
        if (Continuity.Order <= 0) continue;
        const char* Name = Side == 0 ? "start" : "end";
        if (Continuity.Order > 2) return Refuse(std::string(Name) + " continuity G" + std::to_string(Continuity.Order) + " (only G0 / G1 / G2)");
        if (!Continuity.Reference.Definition) return Refuse(std::string(Name) + " continuity has no reference panel");
        if (Outcome.ClosedCondition) return Refuse(std::string(Name) + " continuity on closed sections (🚧)");
        for (double Weight : Continuity.Reference.Definition->Weights)
        {
            if (Weight != 1.0) return Refuse(std::string(Name) + " continuity with a rational reference panel (🚧)");
        }
        const NurbsCurveDefinition& Section = Side == 0 ? Mapped.front() : Mapped.back();
        double Scale = 0.0;
        for (const Vector3& Pole : Section.Poles) Scale = std::max(Scale, Length(Pole));
        const double Match = ContinuityMatch * std::max(1.0, Scale);
        bool Found = false;
        for (int Variant = 0; Variant < 8 && !Found; ++Variant)
        {
            // Variant bits: 1 transpose, 2 reverse 𝑢, 4 boundary 𝑣 = 1.
            NurbsSurfaceDefinition Candidate = *Continuity.Reference.Definition;
            if (Variant & 1) Candidate = TransposeNurbsSurface(Candidate);
            if (Variant & 2) Candidate = ReverseNurbsSurface(Candidate, true);
            const double Boundary = (Variant & 4) ? 1.0 : 0.0;
            double Worst = 0.0;
            for (int Sample = 0; Sample <= 16 && Worst <= Match; ++Sample)
            {
                const double U = Sample / 16.0;
                Worst = std::max(Worst, Length(EvaluateNurbsSurfaceDerivatives(Candidate, { U, Boundary }).Position - EvaluateNurbsCurveDerivatives(Section, U).Position));
            }
            if (Worst > Match) continue;
            Ends[Side] = { Continuity.Order, Candidate, Boundary };
            Found = true;
        }
        if (!Found) return Refuse(std::string(Name) + " section is not a boundary of the continuity reference panel");
    }

    // ①″ One degree along 𝑢 for the sections and the references (exact elevation).
    int Degree = 1;
    for (const NurbsCurveDefinition& Definition : Mapped) Degree = std::max(Degree, Definition.Degree);
    for (const ContinuityEnd& End : Ends) if (End.Order > 0) Degree = std::max(Degree, End.Reference.DegreeU);
    for (NurbsCurveDefinition& Definition : Mapped)
    {
        if (Definition.Degree == Degree) continue;
        std::optional<NurbsCurveDefinition> Elevated = ElevateNurbsCurveDegree(Definition, Degree);
        if (!Elevated) return Refuse("degree elevation of a section to " + std::to_string(Degree) + " failed");
        Definition = std::move(*Elevated);
        ++Outcome.Elevated;
    }
    for (ContinuityEnd& End : Ends)
    {
        if (End.Order <= 0 || End.Reference.DegreeU == Degree) continue;
        std::optional<NurbsSurfaceDefinition> Elevated = ElevateNurbsSurfaceDegree(End.Reference, Degree, End.Reference.DegreeV);
        if (!Elevated) return Refuse("degree elevation of a continuity reference failed");
        End.Reference = std::move(*Elevated);
    }

    // ② Merge the knot vectors: every section (and reference) gets the union (largest multiplicity of each value).
    std::vector<std::vector<double>> KnotVectors;
    for (const NurbsCurveDefinition& Definition : Mapped) KnotVectors.push_back(Definition.Knots);
    for (const ContinuityEnd& End : Ends) if (End.Order > 0) KnotVectors.push_back(End.Reference.KnotsU);
    const std::vector<double> Union = UniteKnotVectors(KnotVectors, Degree);
    for (NurbsCurveDefinition& Definition : Mapped) Definition = RefineNurbsCurveKnots(Definition, MissingInteriorKnots(Definition.Knots, Union, Degree));
    for (ContinuityEnd& End : Ends) if (End.Order > 0) End.Reference = RefineNurbsSurfaceKnots(End.Reference, MissingInteriorKnots(End.Reference.KnotsU, Union, Degree), {});
    const int CountU = static_cast<int>(Mapped.front().Poles.size());
    for (const NurbsCurveDefinition& Definition : Mapped)
    {
        if (static_cast<int>(Definition.Poles.size()) != CountU) return Refuse("knot merging failed (a knot exceeds multiplicity p)");
    }
    for (const ContinuityEnd& End : Ends)
    {
        if (End.Order > 0 && End.Reference.CountU != CountU) return Refuse("knot merging with a continuity reference failed");
    }

    // ③ 𝑣ₖ: chord length along each pole column, normalised and averaged over the columns that move.
    const int CountV = static_cast<int>(Mapped.size());
    std::vector<double> Parameters(CountV, 0.0);
    int Columns = 0;
    double Across = 0.0;
    for (int Column = 0; Column < CountU; ++Column)
    {
        std::vector<double> Cumulative(CountV, 0.0);
        for (int Row = 1; Row < CountV; ++Row) Cumulative[Row] = Cumulative[Row - 1] + Length(Mapped[Row].Poles[Column] - Mapped[Row - 1].Poles[Column]);
        Across += Cumulative.back() / CountU;
        if (!(Cumulative.back() > ClosureGap)) continue;
        for (int Row = 1; Row < CountV; ++Row) Parameters[Row] += Cumulative[Row] / Cumulative.back();
        ++Columns;
    }
    if (Settings.Parameters.empty())
    {
        if (Columns == 0) return Refuse("all sections coincide");
        for (double& Parameter : Parameters) Parameter /= Columns;
        Parameters.back() = 1.0;
    }
    else
    {
        // ③″ Caller-fixed 𝑣ₖ: lofts that must share boundary columns bit-for-bit (evolution sides) take one list.
        if (static_cast<int>(Settings.Parameters.size()) != CountV) return Refuse(std::to_string(Settings.Parameters.size()) + " fixed parameters for " + std::to_string(CountV) + " sections");
        if (Settings.Parameters.front() != 0.0 || Settings.Parameters.back() != 1.0) return Refuse("fixed parameters must run from 0 to 1");
        Parameters = Settings.Parameters;
    }
    for (int Row = 1; Row < CountV; ++Row)
    {
        if (!(Parameters[Row] > Parameters[Row - 1] + 1.0e-12)) return Refuse("sections " + std::to_string(Row - 1) + " and " + std::to_string(Row) + " coincide");
    }

    // ③′ End conditions per pole column: ∂𝑣 = σ·α·R𝑣, ∂𝑣𝑣 = α²·R𝑣𝑣 at the shared row (α = across-chord ratio).
    CurveGroupSkinning Skinning;
    Skinning.Degree     = Settings.DegreeAcross;
    Skinning.Parameters = Parameters;
    for (int Side = 0; Side < 2; ++Side)
    {
        const ContinuityEnd& End = Ends[Side];
        if (End.Order <= 0) continue;
        double ReferenceAcross = 0.0;
        for (int Column = 0; Column < CountU; ++Column)
        {
            const NurbsCurveDefinition Pillar = ExtractPoleColumn(End.Reference, Column);
            for (size_t Pole = 1; Pole < Pillar.Poles.size(); ++Pole) ReferenceAcross += Length(Pillar.Poles[Pole] - Pillar.Poles[Pole - 1]) / CountU;
        }
        if (!(ReferenceAcross > ClosureGap)) return Refuse("the continuity reference panel has no extent across");
        const double Scale = Across / ReferenceAcross;
        // Start meets the reference's 𝑣 = 1 row, or end meets its 𝑣 = 0 row: the loft continues the reference (σ = +1).
        const double Sign = (Side == 0) == (End.Boundary == 1.0) ? 1.0 : -1.0;
        CurveGroupEndCondition& Condition = Side == 0 ? Skinning.Start : Skinning.End;
        for (int Column = 0; Column < CountU; ++Column)
        {
            const NurbsCurveDerivatives Pillar = EvaluateNurbsCurveDerivatives(ExtractPoleColumn(End.Reference, Column), End.Boundary);
            Condition.First.push_back(Pillar.First * (Sign * Scale));
            if (End.Order >= 2) Condition.Second.push_back(Pillar.Second * (Scale * Scale));
        }
        (Side == 0 ? Outcome.StartScale : Outcome.EndScale) = Scale;
        // The shared section takes the reference's boundary row so both panels hold bit-identical poles there.
        NurbsCurveDefinition& Section = Side == 0 ? Mapped.front() : Mapped.back();
        const int Row = End.Boundary == 1.0 ? End.Reference.CountV - 1 : 0;
        const NurbsCurveDefinition Shared = ExtractPoleRow(End.Reference, Row);
        for (int Column = 0; Column < CountU; ++Column)
        {
            if (Length(Shared.Poles[Column] - Section.Poles[Column]) > 1.0e-6) return Refuse("the shared section and the reference boundary differ after knot merging");
        }
        Section.Poles = Shared.Poles;
    }

    // ④ Interpolate every pole column through the sections in homogeneous form (w·P, w), with the end conditions.
    std::string Reason;
    const std::optional<NurbsSurfaceDefinition> Skin = SkinCurveFamily(Mapped, Skinning, Reason);
    if (!Skin) return Refuse(Reason);
    const std::optional<NurbsSurface> Surface = FinishNurbsSurface(*Skin);
    if (!Surface) return Refuse("surface construction refused the skinned net");
    Outcome.Surface           = *Surface;
    Outcome.SectionParameters = Parameters;
    for (const NurbsCurveDefinition& Definition : Mapped) Outcome.Sections.push_back(NurbsCurve{ std::make_shared<NurbsCurveDefinition>(Definition) });
    return Outcome;
}

LoftSheetOutcome LoftSectionSheet(ModelPartition& Partition, const std::vector<NurbsCurve>& Sections, const LoftSettings& Settings)
{
    LoftSheetOutcome Outcome;
    Outcome.Loft = LoftSectionSurface(Sections, Settings);
    if (!Outcome.Loft.Refusal.empty()) { Outcome.Sheet.Refusal = Outcome.Loft.Refusal; return Outcome; }
    NurbsSurface Surface = Outcome.Loft.Surface;

    const auto PointCollapsedCondition = [](const NurbsCurve& Curve)
    {
        if (!Curve.Definition || Curve.Definition->Poles.empty()) return true;
        const Vector3 Origin = EvaluateNurbsCurve(*Curve.Definition, 0.0).Position;
        for (int Sample = 1; Sample <= 16; ++Sample)
        {
            if (Length(EvaluateNurbsCurve(*Curve.Definition, Sample / 16.0).Position - Origin) > 5.0e-5) return false;
        }
        return true;
    };
    if (!Outcome.Loft.ClosedCondition)
    {
        const bool BottomCollapsed = PointCollapsedCondition(ExtractNurbsIsoCurve(Surface, false, 0.0));
        const bool TopCollapsed    = PointCollapsedCondition(ExtractNurbsIsoCurve(Surface, false, 1.0));
        const bool LeftCollapsed   = PointCollapsedCondition(ExtractNurbsIsoCurve(Surface, true, 0.0));
        const bool RightCollapsed  = PointCollapsedCondition(ExtractNurbsIsoCurve(Surface, true, 1.0));
        if ((LeftCollapsed || RightCollapsed) && !BottomCollapsed && !TopCollapsed)
        {
            if (const std::optional<NurbsSurface> Turned = FinishNurbsSurface(ReverseNurbsSurface(TransposeNurbsSurface(*Surface.Definition), true)))
                Surface = *Turned;
        }
    }

    // 📝 Chart (𝑢, 𝑣) ∈ [0, 1]²; boundary edges are exact iso-curves sharing the chart parameter, so each SP-curve is a line:
    //    bottom 𝑣 = 0 and top 𝑣 = 1 run in 𝑢, the rails 𝑢 = 0 / 𝑢 = 1 run in 𝑣. Loop CCW in the chart (Forward face).
    //    Any collapsed 𝑢-rail was transposed onto a singular row 𝑣 ∈ {0, 1} (d𝑣 = 0) and is omitted from the fin loop.
    BodyBlueprint Blueprint;
    Blueprint.Category = BodyCategory::Sheet;
    auto Corner = [&](double U, double V) { return EvaluateSurface(SurfaceGeometry{ Surface }, { U, V }).Position; };
    auto VertexAt = [&](Vector3 Position)
    {
        for (size_t Index = 0; Index < Blueprint.Vertices.size(); ++Index)
            if (Length(Blueprint.Vertices[Index].Position - Position) <= 5.0e-5) return static_cast<int>(Index);
        Blueprint.Vertices.push_back({ Position });
        return static_cast<int>(Blueprint.Vertices.size()) - 1;
    };
    auto AddEdge = [&](bool FixedU, double Value, int Start, int End)
    {
        BlueprintEdge Edge;
        Edge.Curve = ExtractNurbsIsoCurve(Surface, FixedU, Value);
        Edge.Low = 0.0; Edge.High = 1.0; Edge.Start = Start; Edge.End = End;
        Blueprint.Edges.push_back(std::move(Edge));
        return static_cast<int>(Blueprint.Edges.size()) - 1;
    };
    auto Use = [](int Edge, Orientation Sense, ParameterLine Chart) { BlueprintUse Usage; Usage.Edge = Edge; Usage.Sense = Sense; Usage.SurfaceCurve = Chart; return Usage; };
    BlueprintFace Face;
    Face.Surface = Surface;
    Face.Sense   = Orientation::Forward;
    if (Outcome.Loft.ClosedCondition)
    {
        Blueprint.Vertices = { { Corner(0.0, 0.0) }, { Corner(0.0, 1.0) } };
        const int Bottom = AddEdge(false, 0.0, 0, 0), Top = AddEdge(false, 1.0, 1, 1), Seam = AddEdge(true, 0.0, 0, 1);
        Face.Loops.push_back({ Use(Bottom, Orientation::Forward,  IsoLine({ 0.0, 0.0 }, { 1.0, 0.0 })),
                               Use(Seam,   Orientation::Forward,  IsoLine({ 1.0, 0.0 }, { 0.0, 1.0 })),
                               Use(Top,    Orientation::Reversed, IsoLine({ 0.0, 1.0 }, { 1.0, 0.0 })),
                               Use(Seam,   Orientation::Reversed, IsoLine({ 0.0, 0.0 }, { 0.0, 1.0 })) });
    }
    else
    {
        const bool BottomCollapsed = PointCollapsedCondition(ExtractNurbsIsoCurve(Surface, false, 0.0));
        const bool TopCollapsed    = PointCollapsedCondition(ExtractNurbsIsoCurve(Surface, false, 1.0));
        const bool LeftCollapsed   = PointCollapsedCondition(ExtractNurbsIsoCurve(Surface, true, 0.0));
        const bool RightCollapsed  = PointCollapsedCondition(ExtractNurbsIsoCurve(Surface, true, 1.0));
        if (LeftCollapsed || RightCollapsed || (BottomCollapsed && TopCollapsed && PointCollapsedCondition(ExtractNurbsIsoCurve(Surface, true, 0.5))))
        {
            Outcome.Sheet.Refusal = "Loft: surface collapses to a curve or point";
            return Outcome;
        }
        const int V00 = VertexAt(Corner(0.0, 0.0)), V10 = VertexAt(Corner(1.0, 0.0));
        const int V11 = VertexAt(Corner(1.0, 1.0)), V01 = VertexAt(Corner(0.0, 1.0));
        std::vector<BlueprintUse> Loop;
        if (!BottomCollapsed) Loop.push_back(Use(AddEdge(false, 0.0, V00, V10), Orientation::Forward,  IsoLine({ 0.0, 0.0 }, { 1.0, 0.0 })));
        Loop.push_back(Use(AddEdge(true, 1.0, V10, V11), Orientation::Forward, IsoLine({ 1.0, 0.0 }, { 0.0, 1.0 })));
        if (!TopCollapsed) Loop.push_back(Use(AddEdge(false, 1.0, V01, V11), Orientation::Reversed, IsoLine({ 0.0, 1.0 }, { 1.0, 0.0 })));
        Loop.push_back(Use(AddEdge(true, 0.0, V00, V01), Orientation::Reversed, IsoLine({ 0.0, 0.0 }, { 0.0, 1.0 })));
        Face.Loops.push_back(std::move(Loop));
    }
    Blueprint.Faces.push_back(std::move(Face));
    const ArrangementOutcome Assembly = AssembleBody(Partition, Blueprint, "Loft");
    if (!Assembly.Refusal.empty()) { Outcome.Sheet.Refusal = "Loft: " + Assembly.Refusal; return Outcome; }
    Outcome.Sheet.Body  = Assembly.Body;
    Outcome.Sheet.Faces = Assembly.Faces;
    Outcome.Sheet.Roles = { SweepFace{ SweepFacePlacement::Side, "Surface", 0 } };
    return Outcome;
}

LoftSurfaceOutcome LoftTriangularLoop(const std::vector<NurbsCurve>& Curves, double CornerTolerance)
{
    LoftSurfaceOutcome Outcome;
    auto Refuse = [&](const std::string& Reason) { Outcome.Refusal = "Loft: " + Reason; return Outcome; };
    if (Curves.size() != 3) return Refuse("triangular loop needs 3 curves");
    std::array<NurbsCurveDefinition, 3> Mapped;
    for (size_t Index = 0; Index < 3; ++Index)
    {
        if (!Curves[Index].Definition) return Refuse("curve is empty");
        Mapped[Index] = MapCurveOntoUnit(*Curves[Index].Definition);
        if (Length(Mapped[Index].Poles.front() - Mapped[Index].Poles.back()) <= CornerTolerance) return Refuse("triangular loop curves must be open");
        for (double Weight : Mapped[Index].Weights)
            if (std::abs(Weight - 1.0) > 1.0e-9) return Refuse("rational triangular loop needs polynomial refit");
    }

    // Orient C₀: A → B, C₁: B → C, C₂: A → C around the 3 corners (A, B, C).
    const Vector3 CornerA = Mapped[0].Poles.front();
    const Vector3 CornerB = Mapped[0].Poles.back();
    int RightIndex = -1, LeftIndex = -1;
    bool FlipRight = false, FlipLeft = false;
    for (int Candidate = 1; Candidate < 3; ++Candidate)
    {
        const Vector3 Start = Mapped[Candidate].Poles.front(), End = Mapped[Candidate].Poles.back();
        if (RightIndex < 0 && (Length(Start - CornerB) <= CornerTolerance || Length(End - CornerB) <= CornerTolerance))
        {
            RightIndex = Candidate;
            FlipRight  = Length(End - CornerB) < Length(Start - CornerB);
        }
        else if (LeftIndex < 0 && (Length(Start - CornerA) <= CornerTolerance || Length(End - CornerA) <= CornerTolerance))
        {
            LeftIndex = Candidate;
            FlipLeft  = Length(End - CornerA) < Length(Start - CornerA);
        }
    }
    if (RightIndex < 0 || LeftIndex < 0) return Refuse("the 3 curves do not close around corner A and corner B");
    const NurbsCurveDefinition Bottom = Mapped[0];
    const NurbsCurveDefinition Right  = FlipRight ? *ReverseNurbsCurve(Mapped[RightIndex]).Definition : Mapped[RightIndex];
    const NurbsCurveDefinition Left   = FlipLeft  ? *ReverseNurbsCurve(Mapped[LeftIndex]).Definition  : Mapped[LeftIndex];
    const Vector3 CornerC = (Right.Poles.back() + Left.Poles.back()) * 0.5;
    if (Length(Right.Poles.back() - Left.Poles.back()) > CornerTolerance) return Refuse("the 3 curves do not meet at the apex corner");
    if (Length(CornerC - CornerA) <= CornerTolerance || Length(CornerC - CornerB) <= CornerTolerance) return Refuse("triangular loop corners coincide");

    // S₁(𝑢, 𝑣) = (1 − 𝑣)·Bottom(𝑢) + 𝑣·C (degree (deg(Bottom), 1)).
    NurbsSurfaceDefinition SkinFirst;
    SkinFirst.DegreeU = Bottom.Degree;
    SkinFirst.DegreeV = 1;
    SkinFirst.CountU  = static_cast<int>(Bottom.Poles.size());
    SkinFirst.CountV  = 2;
    SkinFirst.KnotsU  = Bottom.Knots;
    SkinFirst.KnotsV  = { 0.0, 0.0, 1.0, 1.0 };
    SkinFirst.Poles.resize(static_cast<size_t>(SkinFirst.CountU) * 2);
    SkinFirst.Weights.assign(SkinFirst.Poles.size(), 1.0);
    for (int Column = 0; Column < SkinFirst.CountU; ++Column)
    {
        SkinFirst.Poles[static_cast<size_t>(Column) * 2 + 0] = Bottom.Poles[Column];
        SkinFirst.Poles[static_cast<size_t>(Column) * 2 + 1] = CornerC;
    }
    SkinFirst.Poles[0] = CornerA;
    SkinFirst.Poles[static_cast<size_t>(SkinFirst.CountU - 1) * 2 + 0] = CornerB;

    // S₂(𝑢, 𝑣) = (1 − 𝑢)·Left(𝑣) + 𝑢·Right(𝑣), built by ruling Left → Right and transposing onto (𝑢, 𝑣).
    LoftSettings Unaligned;
    Unaligned.AlignCondition = false;
    NurbsCurveDefinition LeftExact = Left, RightExact = Right;
    LeftExact.Poles.front()  = CornerA;
    LeftExact.Poles.back()   = CornerC;
    RightExact.Poles.front() = CornerB;
    RightExact.Poles.back()  = CornerC;
    const LoftSurfaceOutcome RuledAcross = LoftSectionSurface(
        { NurbsCurve{ std::make_shared<NurbsCurveDefinition>(LeftExact) }, NurbsCurve{ std::make_shared<NurbsCurveDefinition>(RightExact) } }, Unaligned);
    if (!RuledAcross.Refusal.empty()) return Refuse(RuledAcross.Refusal);
    const NurbsSurfaceDefinition SkinSecond = TransposeNurbsSurface(*RuledAcross.Surface.Definition);

    // T(𝑢, 𝑣) = (1 − 𝑢)(1 − 𝑣)·A + 𝑢(1 − 𝑣)·B + 𝑣·C.
    NurbsSurfaceDefinition Tensor;
    Tensor.DegreeU = 1;
    Tensor.DegreeV = 1;
    Tensor.CountU  = 2;
    Tensor.CountV  = 2;
    Tensor.KnotsU  = { 0.0, 0.0, 1.0, 1.0 };
    Tensor.KnotsV  = { 0.0, 0.0, 1.0, 1.0 };
    Tensor.Poles   = { CornerA, CornerC, CornerB, CornerC };
    Tensor.Weights = { 1.0, 1.0, 1.0, 1.0 };

    std::vector<NurbsSurfaceDefinition> Parts = { SkinFirst, SkinSecond, Tensor };
    int DegreeU = 1, DegreeV = 1;
    for (const NurbsSurfaceDefinition& Part : Parts) { DegreeU = std::max(DegreeU, Part.DegreeU); DegreeV = std::max(DegreeV, Part.DegreeV); }
    for (NurbsSurfaceDefinition& Part : Parts)
    {
        std::optional<NurbsSurfaceDefinition> Elevated = ElevateNurbsSurfaceDegree(Part, DegreeU, DegreeV);
        if (!Elevated) return Refuse("triangular Coons degree elevation failed");
        Part = std::move(*Elevated);
    }
    std::vector<std::vector<double>> KnotsU, KnotsV;
    for (const NurbsSurfaceDefinition& Part : Parts) { KnotsU.push_back(Part.KnotsU); KnotsV.push_back(Part.KnotsV); }
    const std::vector<double> UnionU = UniteKnotVectors(KnotsU, DegreeU), UnionV = UniteKnotVectors(KnotsV, DegreeV);
    for (NurbsSurfaceDefinition& Part : Parts)
        Part = RefineNurbsSurfaceKnots(Part, MissingInteriorKnots(Part.KnotsU, UnionU, DegreeU), MissingInteriorKnots(Part.KnotsV, UnionV, DegreeV));
    for (const NurbsSurfaceDefinition& Part : Parts)
        if (Part.CountU != Parts[0].CountU || Part.CountV != Parts[0].CountV) return Refuse("triangular Coons knot union failed");

    NurbsSurfaceDefinition Sum = Parts[0];
    for (size_t Index = 0; Index < Sum.Poles.size(); ++Index) Sum.Poles[Index] = Parts[0].Poles[Index] + Parts[1].Poles[Index] - Parts[2].Poles[Index];
    std::fill(Sum.Weights.begin(), Sum.Weights.end(), 1.0);
    const std::optional<NurbsSurface> Surface = FinishNurbsSurface(Sum);
    if (!Surface) return Refuse("triangular Coons surface construction failed");
    Outcome.Surface = *Surface;
    Outcome.Sections = Curves;
    Outcome.SectionParameters = { 0.0, 0.5, 1.0 };
    return Outcome;
}

}

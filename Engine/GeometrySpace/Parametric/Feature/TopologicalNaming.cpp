/*====================================================================================================================================
                                                     TOPOLOGICALNAMING.CPP
====================================================================================================================================*/
// 🧩 Persistent names from operation histories (research § L 🥇) and reference resolution: exact → source → geometric

#include "Engine/GeometrySpace/Parametric/Feature/TopologicalNaming.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <regex>
#include <set>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] double MeasureEdgeLength(const ModelPartition& Partition, EdgeToken Edge)
    {
        static constexpr double Nodes[4]   = { 0.1834346424956498, 0.5255324099163290, 0.7966664774136267, 0.9602898564975363 };
        static constexpr double Weights[4] = { 0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763 };
        const EdgeProfile&   Record = Partition.Edges.Resolve(Edge);
        const CurveGeometry& Curve  = Partition.Curves.Resolve(Record.Curve);
        double Arc = 0.0;
        constexpr int Pieces = 8;
        for (int Piece = 0; Piece < Pieces; ++Piece)
        {
            const double Low  = Record.IntervalLow + (Record.IntervalHigh - Record.IntervalLow) * Piece / Pieces;
            const double High = Record.IntervalLow + (Record.IntervalHigh - Record.IntervalLow) * (Piece + 1) / Pieces;
            for (int GaussIndex = 0; GaussIndex < 8; ++GaussIndex)
            {
                const double Abscissa = GaussIndex < 4 ? -Nodes[3 - GaussIndex] : Nodes[GaussIndex - 4];
                const double Weight   = (GaussIndex < 4 ? Weights[3 - GaussIndex] : Weights[GaussIndex - 4]) * 0.5 * (High - Low);
                Arc += Weight * Length(EvaluateCurve(Curve, Low + (Abscissa + 1.0) * 0.5 * (High - Low)).Derivative);
            }
        }
        return Arc;
    }

    [[nodiscard]] CurveSample EdgeMiddle(const ModelPartition& Partition, EdgeToken Edge)
    {
        const EdgeProfile& Record = Partition.Edges.Resolve(Edge);
        return EvaluateCurve(Partition.Curves.Resolve(Record.Curve), 0.5 * (Record.IntervalLow + Record.IntervalHigh));
    }

    [[nodiscard]] double MeasureBodyScale(const ModelPartition& Partition, const BodyInventory& Inventory)
    {
        Vector3 Low{ 1e300, 1e300, 1e300 }, High{ -1e300, -1e300, -1e300 };
        for (VertexToken Vertex : Inventory.Vertices)
        {
            const Vector3 Point = Partition.Vertices.Resolve(Vertex).Position;
            Low  = { std::min(Low.X, Point.X), std::min(Low.Y, Point.Y), std::min(Low.Z, Point.Z) };
            High = { std::max(High.X, Point.X), std::max(High.Y, Point.Y), std::max(High.Z, Point.Z) };
        }
        return Inventory.Vertices.empty() ? 1.0 : std::max(1.0e-12, Distance(Low, High));
    }

    // 📝 Stable ordinal key: position quantised at 10⁻⁶·L, compared x, then y, then z.
    struct OrdinalKey
    {
        long long X = 0, Y = 0, Z = 0;                                                       // [-] - Quantised coordinates
        bool operator<(const OrdinalKey& Other) const noexcept { return X != Other.X ? X < Other.X : Y != Other.Y ? Y < Other.Y : Z < Other.Z; }
    };

    [[nodiscard]] OrdinalKey QuantiseKey(Vector3 Position, double Scale)
    {
        const double Step = 1.0e-6 * Scale;
        return { std::llround(Position.X / Step), std::llround(Position.Y / Step), std::llround(Position.Z / Step) };
    }

    // Gives every member of a group of equal names a suffix (Separator + ordinal) ordered by its key; singletons keep
    // the bare name. ⚠️ All members are suffixed, so a bare reference never silently picks one of them.
    void Disambiguate(std::map<uint32_t, std::string>& Names, const std::map<uint32_t, Vector3>& Positions, double Scale, const char* Separator, int FirstOrdinal)
    {
        std::map<std::string, std::vector<uint32_t>> Groups;
        for (const auto& [Slot, Name] : Names) Groups[Name].push_back(Slot);
        for (auto& [Name, Slots] : Groups)
        {
            if (Slots.size() < 2) continue;
            std::sort(Slots.begin(), Slots.end(), [&](uint32_t First, uint32_t Second)
            {
                const OrdinalKey FirstKey = QuantiseKey(Positions.at(First), Scale), SecondKey = QuantiseKey(Positions.at(Second), Scale);
                if (FirstKey < SecondKey) return true;
                if (SecondKey < FirstKey) return false;
                return First < Second;
            });
            for (size_t Index = 0; Index < Slots.size(); ++Index) Names[Slots[Index]] = Name + Separator + std::to_string(FirstOrdinal + static_cast<int>(Index));
        }
    }

    void DisambiguateFaces(const ModelPartition& Partition, BodyToken Body, BodyNaming& Naming)
    {
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        std::map<uint32_t, Vector3> Positions;
        for (FaceToken Face : Inventory.Faces) Positions[Face.Slot] = MeasureFaceBoundaryCentre(Partition, Face);
        Disambiguate(Naming.Faces, Positions, MeasureBodyScale(Partition, Inventory), "/", 1);
    }

    [[nodiscard]] std::string Lookup(const std::map<uint32_t, std::string>& Names, uint32_t Slot)
    {
        const auto Found = Names.find(Slot);
        return Found == Names.end() ? std::string("Unnamed") : Found->second;
    }

    [[nodiscard]] const std::map<uint32_t, std::string>& NamesOf(const BodyNaming& Naming, ReferenceCategory Kind)
    {
        return Kind == ReferenceCategory::Face ? Naming.Faces : Kind == ReferenceCategory::Edge ? Naming.Edges : Naming.Vertices;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    NAMING
//------------------------------------------------------------------------------------------------------------------------

void CompleteNaming(const ModelPartition& Partition, BodyToken Body, BodyNaming& Naming)
{
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    const double        Scale     = MeasureBodyScale(Partition, Inventory);
    Naming.Edges.clear();
    Naming.Vertices.clear();

    // Edges: sorted names of the faces around the radial ring.
    std::map<uint32_t, Vector3> EdgePositions;
    for (EdgeToken Edge : Inventory.Edges)
    {
        std::vector<std::string> Around;
        const FinToken Entry = Partition.Edges.Resolve(Edge).RadialFin;
        FinToken Fin = Entry;
        do
        {
            Around.push_back(Lookup(Naming.Faces, Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face.Slot));
            Fin = Partition.Fins.Resolve(Fin).RadialSuccessor;
        } while (Fin.PresentCondition() && !(Fin == Entry) && Around.size() < 16);
        if (Around.size() == 1) Around.push_back("∅");
        std::sort(Around.begin(), Around.end());
        std::string Name = "Edge(";
        for (size_t Index = 0; Index < Around.size(); ++Index) Name += (Index ? "|" : "") + Around[Index];
        Naming.Edges[Edge.Slot] = Name + ")";
        EdgePositions[Edge.Slot] = EdgeMiddle(Partition, Edge).Position;
    }
    Disambiguate(Naming.Edges, EdgePositions, Scale, "#", 0);

    // Vertices: sorted set of incident face names.
    std::map<uint32_t, std::set<std::string>> Incident;
    for (FinToken Fin : Inventory.Fins)
    {
        const VertexToken Start = Partition.FinStart(Fin);
        Incident[Start.Slot].insert(Lookup(Naming.Faces, Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face.Slot));
    }
    std::map<uint32_t, Vector3> VertexPositions;
    for (VertexToken Vertex : Inventory.Vertices)
    {
        std::string Name = "Vertex(";
        bool First = true;
        for (const std::string& Face : Incident[Vertex.Slot]) { Name += (First ? "" : "|") + Face; First = false; }
        Naming.Vertices[Vertex.Slot] = Name + ")";
        VertexPositions[Vertex.Slot] = Partition.Vertices.Resolve(Vertex).Position;
    }
    Disambiguate(Naming.Vertices, VertexPositions, Scale, "#", 0);
}

BodyNaming NameSweptBody(const ModelPartition& Partition, const SweepOutcome& Sweep, const std::string& Prefix)
{
    BodyNaming Naming;
    for (size_t Index = 0; Index < Sweep.Faces.size(); ++Index) Naming.Faces[Sweep.Faces[Index].Slot] = Prefix + "." + SweepRoleName(Sweep.Roles[Index]);
    DisambiguateFaces(Partition, Sweep.Body, Naming);
    CompleteNaming(Partition, Sweep.Body, Naming);
    return Naming;
}

BodyNaming NamePanelBody(const ModelPartition& Partition, BodyToken Body, const std::vector<FaceToken>& Faces, const std::vector<std::string>& FaceNames)
{
    BodyNaming Naming;
    for (size_t Index = 0; Index < Faces.size() && Index < FaceNames.size(); ++Index) Naming.Faces[Faces[Index].Slot] = FaceNames[Index];
    DisambiguateFaces(Partition, Body, Naming);
    CompleteNaming(Partition, Body, Naming);
    return Naming;
}

BodyNaming NameBooleanBody(const ModelPartition& Partition, BodyToken Body, const BooleanOutcome& Boolean, const BodyNaming& First, const BodyNaming& Second)
{
    BodyNaming Naming;
    for (const FaceHistory& History : Boolean.FaceHistories)
    {
        Naming.Faces[History.Result.Slot] = Lookup(History.Argument == 0 ? First.Faces : Second.Faces, History.Source.Slot);
    }
    DisambiguateFaces(Partition, Body, Naming);
    CompleteNaming(Partition, Body, Naming);
    return Naming;
}

BodyNaming NameBlendedBody(const ModelPartition& Partition, BodyToken Body, const BlendOutcome& Blend, const BodyNaming& Source, const std::string& Prefix)
{
    BodyNaming Naming;
    for (const BlendFaceHistory& History : Blend.FaceHistories)
    {
        std::string Name;
        switch (History.Origin)
        {
        case BlendFaceOrigin::Modified: Name = Lookup(Source.Faces, History.Face.Slot); break;
        case BlendFaceOrigin::Edge:     Name = Prefix + ".Strip(" + Lookup(Source.Edges, History.Edge.Slot) + ")"; break;
        case BlendFaceOrigin::Vertex:   Name = Prefix + ".Corner(" + Lookup(Source.Vertices, History.Vertex.Slot) + ")"; break;
        }
        Naming.Faces[History.Result.Slot] = Name;
    }
    DisambiguateFaces(Partition, Body, Naming);
    CompleteNaming(Partition, Body, Naming);
    return Naming;
}

BodyNaming NameChangedBody(const ModelPartition& Partition, BodyToken Body, const FaceOperationOutcome& Change, const BodyNaming& Source, const std::string& Prefix)
{
    BodyNaming Naming;
    for (const FaceChangeHistory& History : Change.FaceHistories)
    {
        if (History.Origin != FaceChangeOrigin::Generated) { Naming.Faces[History.Result.Slot] = Lookup(Source.Faces, History.Face.Slot); continue; }
        const std::string Cause = History.Edge.PresentCondition() ? Lookup(Source.Edges, History.Edge.Slot) : History.Face.PresentCondition() ? Lookup(Source.Faces, History.Face.Slot) : std::string();
        Naming.Faces[History.Result.Slot] = Prefix + ".Generated(" + Cause + ")";
    }
    DisambiguateFaces(Partition, Body, Naming);
    CompleteNaming(Partition, Body, Naming);
    return Naming;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

std::string StripDisambiguators(const std::string& Name)
{
    static const std::regex Disambiguator(R"((/\d+|#\d+)(?=$|[|)]))");
    return std::regex_replace(Name, Disambiguator, "");
}

ReferenceSignature CaptureSignature(const ModelPartition& Partition, ReferenceCategory Kind, uint32_t Slot)
{
    ReferenceSignature Signature;
    Signature.CapturedCondition = true;
    if (Kind == ReferenceCategory::Face)
    {
        const FaceToken    Face   = Partition.Faces.TokenAt(Slot);
        const FaceProfile& Record = Partition.Faces.Resolve(Face);
        Signature.Position = MeasureFaceBoundaryCentre(Partition, Face);
        Signature.Category = SurfaceCategoryLabel(Partition.Surfaces.Resolve(Record.Surface));
        for (LoopToken Loop : Record.Loops) Partition.EnumerateLoopFins(Loop, [&](FinToken Fin) { Signature.Size += MeasureEdgeLength(Partition, Partition.Fins.Resolve(Fin).Edge); });
        return Signature;
    }
    if (Kind == ReferenceCategory::Edge)
    {
        const EdgeToken   Edge   = Partition.Edges.TokenAt(Slot);
        const CurveSample Middle = EdgeMiddle(Partition, Edge);
        Signature.Position  = Middle.Position;
        Signature.Direction = Normalize(Middle.Derivative);
        Signature.Category  = CurveCategoryLabel(Partition.Curves.Resolve(Partition.Edges.Resolve(Edge).Curve));
        Signature.Size      = MeasureEdgeLength(Partition, Edge);
        return Signature;
    }
    Signature.Position = Partition.Vertices.Resolve(Partition.Vertices.TokenAt(Slot)).Position;
    Signature.Category = "Vertex";
    return Signature;
}

ResolvedReference ResolveReference(const ModelPartition& Partition, BodyToken Body, const BodyNaming& Naming, const ConnectivityReference& Reference)
{
    ResolvedReference Resolved;
    const std::map<uint32_t, std::string>& Names = NamesOf(Naming, Reference.Kind);

    // ① Exact.
    for (const auto& [Slot, Name] : Names)
    {
        if (Name != Reference.Name) continue;
        Resolved.Method = ResolutionMethod::Exact; Resolved.Slot = Slot; Resolved.Matched = Name;
        return Resolved;
    }

    // ② Same sources: names equal once split / ordinal disambiguators are removed.
    const std::string Stripped = StripDisambiguators(Reference.Name);
    std::vector<uint32_t> SameSource;
    for (const auto& [Slot, Name] : Names) if (StripDisambiguators(Name) == Stripped) SameSource.push_back(Slot);
    if (SameSource.size() == 1)
    {
        Resolved.Method = ResolutionMethod::Source; Resolved.Slot = SameSource[0]; Resolved.Matched = Names.at(SameSource[0]);
        return Resolved;
    }

    // ③ Geometric rebind with a ratio test.
    if (!Reference.Signature.CapturedCondition)
    {
        Resolved.Refusal = SameSource.empty() ? "reference " + Reference.Name + " not found (no signature to rebind)" : "reference " + Reference.Name + " is ambiguous (no signature to rebind)";
        for (uint32_t Slot : SameSource) Resolved.Candidates.push_back(Names.at(Slot));
        return Resolved;
    }
    std::vector<uint32_t> Pool = SameSource;
    if (Pool.empty()) for (const auto& [Slot, Name] : Names) Pool.push_back(Slot);
    struct Candidate { uint32_t Slot; double Separation; };
    std::vector<Candidate> Ranked;
    const ReferenceSignature& Wanted = Reference.Signature;
    for (uint32_t Slot : Pool)
    {
        const ReferenceSignature Present = CaptureSignature(Partition, Reference.Kind, Slot);
        if (Present.Category != Wanted.Category) continue;
        if (Reference.Kind != ReferenceCategory::Vertex && (Present.Size < 0.5 * Wanted.Size || Present.Size > 2.0 * Wanted.Size)) continue;
        double Separation = Distance(Present.Position, Wanted.Position);
        if (Reference.Kind == ReferenceCategory::Edge) Separation += Wanted.Size * (1.0 - std::fabs(Dot(Present.Direction, Wanted.Direction)));
        Ranked.push_back({ Slot, Separation });
    }
    std::sort(Ranked.begin(), Ranked.end(), [](const Candidate& First, const Candidate& Second) { return First.Separation < Second.Separation; });
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    const double Reach = Reference.Kind == ReferenceCategory::Vertex ? 0.25 * MeasureBodyScale(Partition, Inventory) : Wanted.Size;   // [len] - Max rebind distance
    const double Floor = 1.0e-9 * MeasureBodyScale(Partition, Inventory);
    if (!Ranked.empty() && Ranked[0].Separation <= Reach && (Ranked.size() == 1 || Ranked[1].Separation >= 2.0 * Ranked[0].Separation + Floor))
    {
        Resolved.Method = ResolutionMethod::Geometric; Resolved.Slot = Ranked[0].Slot; Resolved.Matched = Names.at(Ranked[0].Slot);
        return Resolved;
    }
    for (size_t Index = 0; Index < Ranked.size() && Index < 4; ++Index) Resolved.Candidates.push_back(Names.at(Ranked[Index].Slot));
    if (Ranked.empty()) Resolved.Refusal = "reference " + Reference.Name + " not found: nothing with its sources or its shape remains";
    else if (Ranked[0].Separation > Reach) Resolved.Refusal = "reference " + Reference.Name + " not found: nearest look-alike is too far to rebind";
    else Resolved.Refusal = "reference " + Reference.Name + " is ambiguous: " + std::to_string(Ranked.size()) + " equally plausible candidates";
    return Resolved;
}

const char* ResolutionMethodLabel(ResolutionMethod Method) noexcept
{
    switch (Method)
    {
    case ResolutionMethod::Exact:     return "Exact";
    case ResolutionMethod::Source:    return "Source";
    case ResolutionMethod::Geometric: return "Geometric";
    case ResolutionMethod::Refused:   return "Refused";
    }
    return "?";
}

} // namespace Frontier::GeometrySpace::Parametric

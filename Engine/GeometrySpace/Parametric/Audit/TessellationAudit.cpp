/*====================================================================================================================================
                                                     TESSELLATIONAUDIT.CPP
====================================================================================================================================*/
// 🧩 Independent audit of a body tessellation: closure, Euler characteristic, deviation, orientation, volume, conformity

#include "Engine/GeometrySpace/Parametric/Audit/TessellationAudit.h"

#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] inline uint64_t SideKey(int From, int To) noexcept
    {
        return (static_cast<uint64_t>(static_cast<uint32_t>(From)) << 32) | static_cast<uint32_t>(To);
    }

    // 📝 Barycentric samples per triangle: centroid, side midpoints, and the three interior points (4, 1, 1)/6.
    constexpr double SampleWeights[7][3] = {
        { 1.0 / 3.0, 1.0 / 3.0, 1.0 / 3.0 },
        { 0.5, 0.5, 0.0 }, { 0.0, 0.5, 0.5 }, { 0.5, 0.0, 0.5 },
        { 4.0 / 6.0, 1.0 / 6.0, 1.0 / 6.0 }, { 1.0 / 6.0, 4.0 / 6.0, 1.0 / 6.0 }, { 1.0 / 6.0, 1.0 / 6.0, 4.0 / 6.0 },
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    AUDIT
//------------------------------------------------------------------------------------------------------------------------

TessellationAuditReport AuditTessellation(const ModelPartition& Partition, BodyToken Body, const TessellationOutcome& Outcome)
{
    TessellationAuditReport Report;
    const PolygonComplex&   Complex = Outcome.Complex;
    Report.Triangles = static_cast<int>(Complex.Triangles.size());

    // ① Directed side counts.
    std::unordered_map<uint64_t, int> Directed;
    Directed.reserve(Complex.Triangles.size() * 4);
    std::vector<char> Referenced(Complex.Positions.size(), 0);
    for (const auto& Triangle : Complex.Triangles)
    {
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            Directed[SideKey(Triangle[Corner], Triangle[(Corner + 1) % 3])] += 1;
            Referenced[Triangle[Corner]] = 1;
        }
    }
    int64_t Undirected = 0;                                                                  // [-] - Distinct undirected sides
    for (const auto& [Key, Count] : Directed)
    {
        const int From = static_cast<int>(Key >> 32), To = static_cast<int>(Key & 0xFFFFFFFFu);
        const auto Reverse = Directed.find(SideKey(To, From));
        const int  Back    = Reverse == Directed.end() ? 0 : Reverse->second;
        if (From < To || Back == 0) Undirected += 1;
        if (Count != 1 || Back != 1) Report.UnpairedSides += (From < To || Back == 0) ? 1 : 0;
    }
    // 📝 A sheet's rim edge has one fin, so each of its sample pairs is used by exactly one triangle — the only sides a
    //    sheet may leave unpaired.
    for (const PolygonComplexEdge& Edge : Complex.Edges)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge.Edge);
        if (Partition.Fins.Resolve(Profile.RadialFin).RadialSuccessor == Profile.RadialFin) Report.LaminarSides += static_cast<int64_t>(Edge.Vertices.size()) - 1;
    }
    for (char Flag : Referenced) Report.Vertices += Flag;
    Report.UnusedVertices = static_cast<int>(Complex.Positions.size()) - Report.Vertices;
    Report.EulerComplex   = Report.Vertices - static_cast<int>(Undirected) + Report.Triangles;
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    const int Faces = static_cast<int>(Inventory.Faces.size());
    Report.EulerBrep = static_cast<int>(Inventory.Vertices.size()) - static_cast<int>(Inventory.Edges.size()) + Faces - (static_cast<int>(Inventory.Loops.size()) - Faces);

    // ② Deviation, orientation, area, volume, quality.
    double DeviationTotal = 0.0;                                                             // [len] - Σ deviation
    double AngleTotal     = 0.0;                                                             // [deg] - Σ minimum angles
    int64_t Below20 = 0, Free = 0, FreeBelow20 = 0;
    Report.MinimumAngle = 180.0;
    std::unordered_set<uint64_t> BoundarySides;
    for (const PolygonComplexEdge& Edge : Complex.Edges)
    {
        for (size_t Index = 1; Index < Edge.Vertices.size(); ++Index)
        {
            BoundarySides.insert(SideKey(Edge.Vertices[Index - 1], Edge.Vertices[Index]));
            BoundarySides.insert(SideKey(Edge.Vertices[Index], Edge.Vertices[Index - 1]));
        }
    }
    const double AreaFloor = 1.0e-24 * Outcome.Diagonal * Outcome.Diagonal;                  // [len²] - Degenerate threshold
    for (size_t Index = 0; Index < Complex.Triangles.size(); ++Index)
    {
        const auto&              Triangle = Complex.Triangles[Index];
        const auto&              Charts   = Complex.TriangleCharts[Index];
        const PolygonComplexFace& Face    = Complex.Faces[Complex.TriangleFaces[Index]];
        const FaceProfile&       Profile  = Partition.Faces.Resolve(Face.Face);
        const SurfaceGeometry&   Surface  = Partition.Surfaces.Resolve(Profile.Surface);
        const Vector3 P0 = Complex.Positions[Triangle[0]], P1 = Complex.Positions[Triangle[1]], P2 = Complex.Positions[Triangle[2]];
        const Vector3 Normal = Cross(P1 - P0, P2 - P0);                                      // [len²] - 2 × area × unit normal
        const double  Area   = 0.5 * Length(Normal);
        Report.ComplexArea   += Area;
        Report.ComplexVolume += Dot(P0, Cross(P1, P2)) / 6.0;

        for (const auto& Weight : SampleWeights)
        {
            const Vector3 Sample    = P0 * Weight[0] + P1 * Weight[1] + P2 * Weight[2];
            const Vector2 Reference = Charts[0] * Weight[0] + Charts[1] * Weight[1] + Charts[2] * Weight[2];
            const double  Deviation = Distance(EvaluateSurface(Surface, ProjectOntoSurface(Surface, Sample, Reference)).Position, Sample);
            Report.PeakDeviation = std::max(Report.PeakDeviation, Deviation);
            DeviationTotal      += Deviation;
            Report.DeviationSamples += 1;
        }

        if (Area <= AreaFloor)
        {
            Report.Degenerate += 1;
            continue;
        }
        const Vector3 Centroid = (P0 + P1 + P2) * (1.0 / 3.0);
        const Vector2 Chart    = ProjectOntoSurface(Surface, Centroid, (Charts[0] + Charts[1] + Charts[2]) * (1.0 / 3.0));
        Vector3 Outward = EvaluateSurfaceNormal(Surface, Chart);
        if (Profile.Sense == Orientation::Reversed) Outward = -Outward;
        if (Dot(Normal, Outward) <= 0.0) Report.Inverted += 1;

        double Smallest = 180.0;                                                             // [deg] - Minimum angle
        const Vector3 Corners[3] = { P0, P1, P2 };
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            const Vector3 First  = Corners[(Corner + 1) % 3] - Corners[Corner];
            const Vector3 Second = Corners[(Corner + 2) % 3] - Corners[Corner];
            const double  Angle  = std::atan2(Length(Cross(First, Second)), Dot(First, Second)) * 180.0 / std::numbers::pi;
            Smallest = std::min(Smallest, Angle);
        }
        Report.MinimumAngle = std::min(Report.MinimumAngle, Smallest);
        AngleTotal += Smallest;
        Below20    += Smallest < 20.0;
        bool Touches = false;
        for (int Corner = 0; Corner < 3; ++Corner) Touches |= BoundarySides.count(SideKey(Triangle[Corner], Triangle[(Corner + 1) % 3])) > 0;
        if (!Touches)
        {
            Free        += 1;
            FreeBelow20 += Smallest < 20.0;
        }
    }
    const int64_t Measured = Report.Triangles - Report.Degenerate;
    Report.MeanDeviation    = Report.DeviationSamples > 0 ? DeviationTotal / Report.DeviationSamples : 0.0;
    Report.MeanMinimumAngle = Measured > 0 ? AngleTotal / Measured : 0.0;
    Report.ShareBelow20     = Measured > 0 ? static_cast<double>(Below20) / Measured : 0.0;
    Report.ShareBelow20Free = Free > 0 ? static_cast<double>(FreeBelow20) / Free : 0.0;
    Report.ExactVolume      = IntegrateBodyVolume(Partition, Body);

    // ③ Every consecutive edge-sample pair is a complex side, once per fin direction.
    for (const PolygonComplexEdge& Edge : Complex.Edges)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge.Edge);
        int Forward = 0, Backward = 0;                                                       // [-] - Fins along / against τ
        FinToken Fin = Profile.RadialFin;
        for (int Guard = 0; Fin.PresentCondition() && Guard < 64; ++Guard)
        {
            const FinProfile& Use = Partition.Fins.Resolve(Fin);
            (Use.Sense == Orientation::Forward ? Forward : Backward) += 1;
            Fin = Use.RadialSuccessor;
            if (Fin == Profile.RadialFin) break;
        }
        for (size_t Index = 1; Index < Edge.Vertices.size(); ++Index)
        {
            const int From = Edge.Vertices[Index - 1], To = Edge.Vertices[Index];
            if (From == To) continue;
            Report.EdgeSides += 1;
            const auto Along   = Directed.find(SideKey(From, To));
            const auto Against = Directed.find(SideKey(To, From));
            const int  AlongCount   = Along == Directed.end() ? 0 : Along->second;
            const int  AgainstCount = Against == Directed.end() ? 0 : Against->second;
            // A Forward fin's face lies left of the edge, so its triangle traverses the side against τ… or along it,
            // depending on the face sense — only the pairing (one use per fin, opposite directions) is invariant.
            // A laminar (sheet boundary) edge has one fin and one triangle side.
            if (AlongCount + AgainstCount != Forward + Backward || (Forward + Backward == 2 && AlongCount != AgainstCount)) Report.MissingEdgeSides += 1;
        }
    }
    return Report;
}

uint64_t DigestPolygonComplex(const PolygonComplex& Complex)
{
    uint64_t Digest = 1469598103934665603ULL;                                                // [-] - FNV-1a offset basis
    const auto Mix = [&](const void* Bytes, size_t Count)
    {
        const auto* Cursor = static_cast<const unsigned char*>(Bytes);
        for (size_t Index = 0; Index < Count; ++Index) Digest = (Digest ^ Cursor[Index]) * 1099511628211ULL;
    };
    for (const Vector3& Position : Complex.Positions) Mix(&Position, sizeof(Position));
    for (const auto& Triangle : Complex.Triangles) Mix(Triangle.data(), sizeof(int) * 3);
    return Digest;
}

} // namespace Frontier::GeometrySpace::Parametric

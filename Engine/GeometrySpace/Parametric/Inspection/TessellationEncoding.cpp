/*====================================================================================================================================
                                                   TESSELLATIONENCODING.CPP
====================================================================================================================================*/
// 🧩 PolygonNetworkStructure → JSON: geometry for the shaded view, per-face refinement records and the N1–N7 gate verdicts

#include "Engine/GeometrySpace/Parametric/Inspection/TessellationEncoding.h"

#include "Engine/GeometrySpace/Parametric/Audit/ModelAudit.h"
#include "Engine/GeometrySpace/Parametric/Inspection/JsonWriter.h"

#include <cmath>
#include <cstdio>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    void WriteGate(JsonWriter& Writer, const char* Name, const std::string& Measured, const std::string& Allowance, bool Passed)
    {
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.String("Measured", Measured);
        Writer.String("Allowance", Allowance);
        Writer.Boolean("Passed", Passed);
        Writer.CloseObject();
    }

    [[nodiscard]] std::string Format(const char* Pattern, double First, double Second = 0.0, double Third = 0.0)
    {
        char Buffer[128];
        std::snprintf(Buffer, sizeof(Buffer), Pattern, First, Second, Third);
        return Buffer;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENCODING
//------------------------------------------------------------------------------------------------------------------------

std::string EncodeTessellation(const ModelPartition& Partition, const std::string& SpecimenName, double SagRatio, const TessellationOutcome& Outcome,
                               const TessellationAuditReport& Report, double AuditMilliseconds)
{
    const PolygonNetworkStructure& Complex = Outcome.Complex;
    JsonWriter Writer;
    Writer.OpenObject();
    Writer.String("Kind", "Tessellation");
    Writer.String("Specimen", SpecimenName);
    Writer.Number("SagRatio", SagRatio);
    Writer.Number("Sag", Outcome.Sag);
    Writer.Number("EdgeLimit", Outcome.EdgeLimit);
    Writer.Number("Diagonal", Outcome.Diagonal);
    if (!Outcome.Refusal.empty()) Writer.String("Refusal", Outcome.Refusal);

    // ① Geometry.
    Writer.OpenArray("Positions");
    for (const Vector3& Position : Complex.Positions)
    {
        Writer.Number(nullptr, Position.X);
        Writer.Number(nullptr, Position.Y);
        Writer.Number(nullptr, Position.Z);
    }
    Writer.CloseArray();
    Writer.OpenArray("Triangles");
    for (const auto& Triangle : Complex.Triangles)
    {
        for (int Corner : Triangle) Writer.Integer(nullptr, Corner);
    }
    Writer.CloseArray();
    Writer.OpenArray("TriangleFaces");
    for (int Face : Complex.TriangleFaces) Writer.Integer(nullptr, Face);
    Writer.CloseArray();
    Writer.OpenArray("Edges");
    for (const PolygonNetEdge& Edge : Complex.Edges)
    {
        Writer.OpenArray();
        for (int Vertex : Edge.Vertices) Writer.Integer(nullptr, Vertex);
        Writer.CloseArray();
    }
    Writer.CloseArray();

    // ② Per-face refinement records.
    Writer.OpenArray("Faces");
    for (const PolygonNetFace& Face : Complex.Faces)
    {
        const FaceProfile& Patch = Partition.Faces.Resolve(Face.Face);
        Writer.OpenObject();
        Writer.String("Token", LabelToken("F", Face.Face));
        Writer.String("Surface", SurfaceCategoryLabel(Partition.Surfaces.Resolve(Patch.Surface)));
        Writer.Integer("Triangles", Face.TriangleCount);
        Writer.Integer("BoundaryPoints", Face.BoundaryPoints);
        Writer.Integer("Insertions", Face.Insertions);
        Writer.Integer("SingularDropped", Face.SingularDropped);
        Writer.Integer("StarRepairs", Face.StarRepairs);
        Writer.Integer("FoldFlips", Face.FoldFlips);
        Writer.Integer("ClaimedSplits", Face.ClaimedSplits);
        Writer.Integer("FoldedResidual", Face.FoldedResidual);
        Writer.Number("Milliseconds", Face.Milliseconds);
        Writer.CloseObject();
    }
    Writer.CloseArray();

    // ③ Gates, as in TessellationValidation.
    int64_t Folded = 0, Delaunay = 0;
    for (const PolygonNetFace& Face : Complex.Faces)
    {
        Folded   += Face.FoldedResidual;
        Delaunay += Face.DelaunayViolations;
    }
    // 💡 Open sheets: the laminar rim sides are the allowed unpaired sides and an enclosed volume does not exist (N4 n/a).
    const bool   Open        = Report.LaminarSides > 0;
    const double VolumeRatio = !Open && Outcome.Sag > 0.0 && Report.ComplexArea > 0.0 ? std::fabs(Report.ComplexVolume - Report.ExactVolume) / (Outcome.Sag * Report.ComplexArea) : 0.0;
    const bool   Refused     = !Outcome.Refusal.empty();
    Writer.OpenArray("Gates");
    WriteGate(Writer, Open ? "N1 closure: unpaired sides (laminar rim) · χ complex / B-rep · unused vertices" : "N1 closure: unpaired sides · χ complex / B-rep · unused vertices",
              Format("%.0f · %.0f / %.0f", static_cast<double>(Report.UnpairedSides), Report.EulerComplex, Report.EulerBrep) + Format(" · %.0f", Report.UnusedVertices),
              Open ? Format("%.0f · equal · 0", static_cast<double>(Report.LaminarSides)) : std::string("0 · equal · 0"),
              !Refused && Report.UnpairedSides == Report.LaminarSides && Report.EulerComplex == Report.EulerBrep && Report.UnusedVertices == 0);
    WriteGate(Writer, "N2 peak deviation / ε (7 samples per triangle)", Format("%.3f", Outcome.Sag > 0.0 ? Report.PeakDeviation / Outcome.Sag : 0.0), "≤ 1",
              !Refused && Report.PeakDeviation <= Outcome.Sag);
    WriteGate(Writer, "N3 inverted · zero-area · folded", Format("%.0f · %.0f · %.0f", static_cast<double>(Report.Inverted), static_cast<double>(Report.Degenerate),
              static_cast<double>(Folded)), "0 · 0 · 0", !Refused && Report.Inverted == 0 && Report.Degenerate == 0 && Folded == 0);
    WriteGate(Writer, "N4 |V_complex − V_B-rep| / (ε·A)", Open ? std::string("— (open sheet)") : Format("%.3f", VolumeRatio), Open ? "n/a" : "≤ 1", !Refused && VolumeRatio <= 1.0);
    WriteGate(Writer, "N5 Delaunay residual (chart, before final legalisation)", Format("%.0f", static_cast<double>(Delaunay)), "0",
              !Refused && Delaunay == 0);
    WriteGate(Writer, "N6 missing / edge-sample sides", Format("%.0f / %.0f", static_cast<double>(Report.MissingEdgeSides), static_cast<double>(Report.EdgeSides)), "0",
              !Refused && Report.MissingEdgeSides == 0);
    WriteGate(Writer, "N7 min angle · mean min angle · share < 20°", Format("%.1f° · %.1f° · %.1f%%", Report.MinimumAngle, Report.MeanMinimumAngle, 100.0 * Report.ShareBelow20),
              "reported", true);
    Writer.CloseArray();
    Writer.Number("ComplexVolume", Report.ComplexVolume);
    Writer.Number("ExactVolume", Report.ExactVolume);
    Writer.Number("ComplexArea", Report.ComplexArea);
    Writer.Number("Milliseconds", Outcome.Milliseconds);
    Writer.Number("AuditMilliseconds", AuditMilliseconds);
    Writer.CloseObject();
    return Writer.Result();
}

} // namespace Frontier::GeometrySpace::Parametric

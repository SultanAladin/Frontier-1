/*====================================================================================================================================
                                                      BLENDENCODING.CPP
====================================================================================================================================*/
// 🧩 Blend result → JSON: shown body geometry, face provenance, spines / contacts and the closed-form volume check

#include "Engine/GeometrySpace/Parametric/Inspection/BlendEncoding.h"

#include "Engine/GeometrySpace/Parametric/Inspection/InspectionEncoding.h"
#include "Engine/GeometrySpace/Parametric/Inspection/JsonWriter.h"

#include <cmath>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int TraceSamples = 48;                                                         // [-] - Samples per selected edge

    // 📝 Polyline of an edge's curve, or of a curve traced alongside it (each edge sample projected onto the trace).
    void WriteTrace(JsonWriter& Writer, const char* Key, const ModelPartition& Partition, EdgeToken Edge, const CurveGeometry* Trace)
    {
        const EdgeProfile&   Profile = Partition.Edges.Resolve(Edge);
        const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
        const int            Count   = std::holds_alternative<StraightLine>(Curve) && Trace == nullptr ? 2 : TraceSamples;
        Writer.OpenArray(Key);
        for (int Sample = 0; Sample < Count; ++Sample)
        {
            const Vector3 Position = EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Sample / (Count - 1)).Position;
            Writer.Point(nullptr, Trace == nullptr ? Position : EvaluateCurve(*Trace, ProjectOntoCurve(*Trace, Position, 0.0)).Position);
        }
        Writer.CloseArray();
    }

    [[nodiscard]] double SurfaceDistance(const ModelPartition& Partition, FaceToken Face, Vector3 Position)
    {
        const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Face).Surface);
        return Distance(Position, EvaluateSurface(Surface, ProjectOntoSurface(Surface, Position, {})).Position);
    }

    [[nodiscard]] FaceToken FinFace(const ModelPartition& Partition, FinToken Fin) { return Partition.Loops.Resolve(Partition.Fins.Resolve(Fin).Loop).Face; }

    // ⚠️ Contact rails are the result's edges between a sheet's blend face and its two supports — trimmed at caps,
    //    mitres and corners — never the sheet's untrimmed contact curves (those overshoot every corner by the setback).
    [[nodiscard]] std::vector<EdgeToken> CollectContactEdges(const ModelPartition& Partition, const BlendOutcome& Outcome, const EdgeBlendSheet& Sheet)
    {
        std::vector<EdgeToken> Contacts;
        for (const BlendFaceHistory& Blend : Outcome.FaceHistories)
        {
            if (Blend.Origin != BlendFaceOrigin::Edge || !(Blend.Edge == Sheet.Edge)) continue;
            for (LoopToken Loop : Partition.Faces.Resolve(Blend.Result).Loops)
            {
                Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                {
                    const FaceToken Across = FinFace(Partition, Partition.RadialMate(Fin));
                    for (const BlendFaceHistory& Other : Outcome.FaceHistories)
                    {
                        if (!(Other.Result == Across) || Other.Origin != BlendFaceOrigin::Modified) continue;
                        if (Other.Face == Sheet.Faces[0] || Other.Face == Sheet.Faces[1]) Contacts.push_back(Partition.Fins.Resolve(Fin).Edge);
                    }
                });
            }
        }
        return Contacts;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENCODING
//------------------------------------------------------------------------------------------------------------------------

std::string EncodeBlend(const ModelPartition& Partition, const BlendSpecimen& Specimen, const BlendOutcome& Outcome, BodyToken Shown,
                        const AuditReport& Audit, const BlendInspection& Inspection)
{
    const BodyInventory Inventory   = Partition.CollectInventory(Shown);
    const bool          Blended     = Outcome.Refusal.empty() && !Inspection.SourceShown;
    const bool          Catalogued  = std::fabs(Inspection.Applied.Size - Specimen.Settings.Size) <= 1.0e-12 * Specimen.Settings.Size;
    JsonWriter Writer;
    Writer.OpenObject();
    Writer.String("Kind", "Blend");
    Writer.String("Specimen", Specimen.Name);
    Writer.String("Family", Specimen.Family);
    Writer.String("Description", Specimen.Description);
    Writer.OpenObject("Parameters");
    Writer.Number("Size", Inspection.Applied.Size);
    Writer.Number("Source", Inspection.SourceShown ? 1.0 : 0.0);
    Writer.CloseObject();

    // ---- summary: counts of the shown body, volume change against the closed form
    Writer.OpenObject("Summary");
    const std::pair<const char*, long> Counts[] = {
        { "Vertices", static_cast<long>(Inventory.Vertices.size()) }, { "Edges", static_cast<long>(Inventory.Edges.size()) },
        { "Faces", static_cast<long>(Inventory.Faces.size()) },       { "Loops", static_cast<long>(Inventory.Loops.size()) },
        { "Shells", static_cast<long>(Inventory.Shells.size()) },     { "Genus", Audit.Genus },
    };
    Writer.OpenArray("Counts");
    for (const auto& [Name, Measured] : Counts)
    {
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.Integer("Measured", Measured);
        if (Blended && Catalogued && Specimen.ExpectedFaces >= 0 && std::string(Name) == "Faces") Writer.Integer("Expected", Specimen.ExpectedFaces);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.Integer("Fins", static_cast<long>(Inventory.Fins.size()));
    Writer.Integer("Regions", static_cast<long>(Inventory.Regions.size()));
    Writer.Number("SourceVolume", Inspection.SourceVolume);
    Writer.Number("Volume", Inspection.ShownVolume);
    if (Blended)
    {
        const double Delta = Inspection.ShownVolume - Inspection.SourceVolume;               // [len³] - ΔV measured
        Writer.Number("Delta", Delta);
        if (Catalogued && !std::isnan(Specimen.ExpectedDelta))
        {
            Writer.Number("ExpectedDelta", Specimen.ExpectedDelta);
            Writer.Number("DeltaError", std::fabs(Delta - Specimen.ExpectedDelta));
            Writer.Number("DeltaAllowance", 1.0e-9 * std::max(1.0, Inspection.SourceVolume));
        }
    }
    Writer.Number("ConstructionMilliseconds", Inspection.ConstructionMilliseconds);
    Writer.Number("AuditMilliseconds", Inspection.AuditMilliseconds);
    Writer.CloseObject();

    // ---- blend: settings, statistics, selection, sheets and face provenance
    Writer.OpenObject("Blend");
    Writer.String("Profile", BlendProfileLabel(Inspection.Applied.Profile));
    Writer.Number("Size", Inspection.Applied.Size);
    Writer.Boolean("Shown", Blended);
    Writer.Boolean("RefusalExpected", Specimen.RefusalExpected);
    if (!Outcome.Refusal.empty()) Writer.String("Refusal", Outcome.Refusal);
    const BlendStatistics& Statistics = Outcome.Statistics;
    Writer.OpenObject("Statistics");
    Writer.Integer("BlendedEdges", Statistics.BlendedEdges);
    Writer.Integer("Caps", Statistics.Vertices[static_cast<int>(VertexBlendCategory::Cap)]);
    Writer.Integer("Mitres", Statistics.Vertices[static_cast<int>(VertexBlendCategory::Mitre)]);
    Writer.Integer("BallCorners", Statistics.Vertices[static_cast<int>(VertexBlendCategory::BallCorner)]);
    Writer.Integer("Continuations", Statistics.Vertices[static_cast<int>(VertexBlendCategory::Continuation)]);
    Writer.Integer("Pinches", Statistics.Vertices[static_cast<int>(VertexBlendCategory::Pinch)]);
    Writer.Integer("Setbacks", Statistics.Vertices[static_cast<int>(VertexBlendCategory::Setback)]);
    Writer.Integer("CliffOverflows", Statistics.CliffOverflows);
    Writer.Integer("NotchOverflows", Statistics.NotchOverflows);
    Writer.Integer("SmoothOverflows", Statistics.SmoothOverflows);
    Writer.Integer("SetbackPatches", Statistics.SetbackPatches);
    Writer.Number("MaxTangentBreak", Statistics.MaxTangentBreak);
    Writer.Number("MaxSeamBreak", Statistics.MaxSeamBreak);
    Writer.Number("MaxRimGap", Statistics.MaxRimGap);
    Writer.Integer("Planes", Statistics.CarrierPlanes);
    Writer.Integer("Cylinders", Statistics.CarrierCylinders);
    Writer.Integer("Cones", Statistics.CarrierCones);
    Writer.Integer("Tori", Statistics.CarrierTori);
    Writer.Integer("Procedural", Statistics.CarrierProcedural);
    Writer.Integer("CornerSpheres", Statistics.CornerSpheres);
    Writer.Integer("EllipticalEdges", Statistics.EllipticalEdges);
    Writer.Number("MaxSheetResidual", Statistics.MaxSheetResidual);
    Writer.Number("MaxVertexResidual", Statistics.MaxVertexResidual);
    Writer.Number("Milliseconds", Statistics.Milliseconds);
    Writer.CloseObject();
    Writer.OpenArray("Selection");
    for (EdgeToken Edge : Specimen.Edges)
    {
        Writer.OpenObject();
        Writer.String("Token", LabelToken("E", Edge));
        WriteTrace(Writer, "Polyline", Partition, Edge, nullptr);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.OpenArray("Sheets");
    for (const EdgeBlendSheet& Sheet : Outcome.Sheets)
    {
        Writer.OpenObject();
        Writer.String("Edge", LabelToken("E", Sheet.Edge));
        Writer.Boolean("Convex", Sheet.ConvexCondition);
        Writer.Number("Radius", Sheet.Radius);
        Writer.Number("Dihedral", Sheet.Dihedral);
        Writer.String("Spine", CurveCategoryLabel(Sheet.Spine));
        Writer.String("Carrier", SurfaceCategoryLabel(Sheet.Surface));
        Writer.Number("Residual", Sheet.MaxResidual);
        const std::vector<EdgeToken> Contacts = Blended ? CollectContactEdges(Partition, Outcome, Sheet) : std::vector<EdgeToken>{};
        Writer.OpenArray("SpinePolylines");                                                  // Spine traced alongside each rail on side 0
        for (EdgeToken Contact : Contacts)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Contact);
            const Vector3 Middle = EvaluateCurve(Partition.Curves.Resolve(Profile.Curve), 0.5 * (Profile.IntervalLow + Profile.IntervalHigh)).Position;
            if (SurfaceDistance(Partition, Sheet.Faces[0], Middle) > 1.0e-7 * std::max(1.0, Sheet.Radius)) continue;
            WriteTrace(Writer, nullptr, Partition, Contact, &Sheet.Spine);
        }
        Writer.CloseArray();
        Writer.OpenArray("Contacts");
        for (EdgeToken Contact : Contacts)
        {
            Writer.OpenObject();
            Writer.String("Token", LabelToken("E", Contact));
            WriteTrace(Writer, "Polyline", Partition, Contact, nullptr);
            Writer.CloseObject();
        }
        Writer.CloseArray();
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.OpenArray("FaceOrigins");
    if (Blended)
    {
        for (const BlendFaceHistory& History : Outcome.FaceHistories)
        {
            Writer.OpenObject();
            Writer.String("Face", LabelToken("F", History.Result));
            Writer.String("Origin", BlendFaceOriginLabel(History.Origin));
            Writer.String("Source", History.Origin == BlendFaceOrigin::Modified ? LabelToken("F", History.Face)
                                    : History.Origin == BlendFaceOrigin::Edge   ? LabelToken("E", History.Edge)
                                                                                : LabelToken("V", History.Vertex));
            Writer.String("Surface", SurfaceCategoryLabel(Partition.Surfaces.Resolve(Partition.Faces.Resolve(History.Result).Surface)));
            Writer.CloseObject();
        }
    }
    Writer.CloseArray();
    Writer.CloseObject();

    EncodeAuditSection(Writer, Audit);
    EncodeBodyGeometry(Writer, Partition, Shown);
    Writer.CloseObject();
    return Writer.Result();
}

} // namespace Frontier::GeometrySpace::Parametric

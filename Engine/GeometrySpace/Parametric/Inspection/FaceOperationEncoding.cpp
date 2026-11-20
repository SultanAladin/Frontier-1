/*====================================================================================================================================
                                                      FACEOPERATIONENCODING.CPP
====================================================================================================================================*/
// 🧩 Face-operation result → JSON: shown body geometry, applied steps, face provenance and the closed-form volume check

#include "Engine/GeometrySpace/Parametric/Inspection/FaceOperationEncoding.h"

#include "Engine/GeometrySpace/Parametric/Inspection/InspectionEncoding.h"
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
    constexpr int TraceSamples = 32;                                                         // [-] - Samples per curved boundary edge

    // 📝 Boundary polylines of a source face — drawn as the "before" ghost of every selected face.
    void WriteFaceOutline(JsonWriter& Writer, const ModelPartition& Partition, FaceToken Face)
    {
        Writer.OpenArray("Outline");
        for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
        {
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const EdgeProfile&   Profile = Partition.Edges.Resolve(Partition.Fins.Resolve(Fin).Edge);
                const CurveGeometry& Curve   = Partition.Curves.Resolve(Profile.Curve);
                const int            Count   = std::holds_alternative<StraightLine>(Curve) ? 2 : TraceSamples;
                Writer.OpenArray(nullptr);
                for (int Sample = 0; Sample < Count; ++Sample)
                {
                    Writer.Point(nullptr, EvaluateCurve(Curve, Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * Sample / (Count - 1)).Position);
                }
                Writer.CloseArray();
            });
        }
        Writer.CloseArray();
    }

    [[nodiscard]] std::string DescribeStep(const FaceOperationSettings& Step)
    {
        char Text[160];
        switch (Step.Kind)
        {
        case FaceOperationCategory::Rotate:
        case FaceOperationCategory::Draft:   std::snprintf(Text, sizeof(Text), "%s %.4g°", FaceOperationLabel(Step.Kind), Step.Angle * 180.0 / 3.14159265358979323846); break;
        case FaceOperationCategory::Scale:   std::snprintf(Text, sizeof(Text), "%s ×%.4g", FaceOperationLabel(Step.Kind), Step.Factor); break;
        case FaceOperationCategory::Tweak:   std::snprintf(Text, sizeof(Text), "%s (%.4g, %.4g, %.4g)", FaceOperationLabel(Step.Kind), Step.Vector.X, Step.Vector.Y, Step.Vector.Z); break;
        case FaceOperationCategory::Delete:
        case FaceOperationCategory::Heal:    std::snprintf(Text, sizeof(Text), "%s", FaceOperationLabel(Step.Kind)); break;
        default:                         std::snprintf(Text, sizeof(Text), "%s %.4g", FaceOperationLabel(Step.Kind), Step.Distance); break;
        }
        std::string Result = Text;
        if (!Step.Faces.empty())
        {
            Result += " ·";
            for (FaceToken Face : Step.Faces) Result += " " + LabelToken("F", Face);
        }
        return Result;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENCODING
//------------------------------------------------------------------------------------------------------------------------

std::string EncodeFaceOperation(const ModelPartition& Partition, const FaceOperationSpecimen& Specimen, const FaceOperationOutcome& Outcome,
                                BodyToken Shown, const AuditReport& Audit, const FaceOperationInspection& Inspection)
{
    const BodyInventory Inventory  = Partition.CollectInventory(Shown);
    const bool          Changed    = Outcome.Refusal.empty() && !Inspection.SourceShown;
    const bool          Catalogued = std::fabs(Inspection.Amount - 1.0) <= 1.0e-12;
    JsonWriter Writer;
    Writer.OpenObject();
    Writer.String("Kind", "FaceOperation");
    Writer.String("Specimen", Specimen.Name);
    Writer.String("Family", Specimen.Family);
    Writer.String("Description", Specimen.Description);
    Writer.OpenObject("Parameters");
    Writer.Number("Amount", Inspection.Amount);
    Writer.Number("Source", Inspection.SourceShown ? 1.0 : 0.0);
    Writer.CloseObject();

    // ---- summary: counts of the shown body, volume against the closed form
    Writer.OpenObject("Summary");
    const std::pair<const char*, long> Counts[] = {
        { "Vertices", static_cast<long>(Inventory.Vertices.size()) }, { "Edges", static_cast<long>(Inventory.Edges.size()) },
        { "Faces", static_cast<long>(Inventory.Faces.size()) },       { "Loops", static_cast<long>(Inventory.Loops.size()) },
        { "Shells", static_cast<long>(Inventory.Shells.size()) },     { "Genus", Audit.Genus },
    };
    const int Expected[] = { Specimen.ExpectedCounts[2], Specimen.ExpectedCounts[1], Specimen.ExpectedCounts[0], -1, -1, Specimen.ExpectedGenus };
    Writer.OpenArray("Counts");
    for (size_t Index = 0; Index < std::size(Counts); ++Index)
    {
        Writer.OpenObject();
        Writer.String("Name", Counts[Index].first);
        Writer.Integer("Measured", Counts[Index].second);
        if (Changed && Catalogued && Expected[Index] >= 0) Writer.Integer("Expected", Expected[Index]);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.Integer("Fins", static_cast<long>(Inventory.Fins.size()));
    Writer.Integer("Regions", static_cast<long>(Inventory.Regions.size()));
    Writer.String("Category", Changed && Outcome.Category == BodyCategory::Sheet ? "Sheet" : "Solid");
    Writer.Number("SourceVolume", Inspection.SourceVolume);
    if (!std::isnan(Inspection.ShownVolume)) Writer.Number("Volume", Inspection.ShownVolume);
    if (Changed && Catalogued && !std::isnan(Specimen.ExpectedVolume) && !std::isnan(Inspection.ShownVolume))
    {
        Writer.Number("ExpectedVolume", Specimen.ExpectedVolume);
        Writer.Number("VolumeError", std::fabs(Inspection.ShownVolume - Specimen.ExpectedVolume));
        Writer.Number("VolumeAllowance", 1.0e-9 * std::max(1.0, Specimen.ExpectedVolume));
    }
    Writer.Number("ConstructionMilliseconds", Inspection.ConstructionMilliseconds);
    Writer.Number("AuditMilliseconds", Inspection.AuditMilliseconds);
    Writer.CloseObject();

    // ---- face operation: steps, statistics, selection outlines and face provenance
    Writer.OpenObject("FaceOperation");
    Writer.String("Corpus", Specimen.Corpus);
    Writer.String("LegacyStatus", Specimen.LegacyStatus);
    Writer.String("Method", Outcome.Method);
    Writer.Boolean("Shown", Changed);
    Writer.Boolean("RefusalExpected", Specimen.RefusalExpected);
    Writer.Integer("CompletedSteps", Inspection.CompletedSteps);
    if (!Outcome.Refusal.empty()) Writer.String("Refusal", Outcome.Refusal);
    Writer.OpenArray("Steps");
    for (const FaceOperationSettings& Step : Inspection.Applied) Writer.String(nullptr, DescribeStep(Step));
    Writer.CloseArray();
    const FaceChangeStatistics& Statistics = Outcome.Statistics;
    Writer.OpenObject("Statistics");
    Writer.Integer("ReplacedFaces", Statistics.ReplacedFaces);
    Writer.Integer("MovedVertices", Statistics.MovedVertices);
    Writer.Integer("EllipticalEdges", Statistics.EllipticalEdges);
    Writer.OpenArray("EdgeCategories");
    for (int Category = 0; Category < 4; ++Category)
    {
        Writer.OpenObject();
        Writer.String("Name", EdgeChangeCategoryLabel(static_cast<EdgeChangeCategory>(Category)));
        Writer.Integer("Count", Statistics.Edges[Category]);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.Number("MaxVertexResidual", Statistics.MaxVertexResidual);
    Writer.Number("MaxEdgeResidual", Statistics.MaxEdgeResidual);
    Writer.Number("Milliseconds", Inspection.OperationMilliseconds);
    Writer.CloseObject();
    Writer.OpenArray("Selection");
    if (!Inspection.Applied.empty())
    {
        for (FaceToken Face : Inspection.Applied.front().Faces)
        {
            Writer.OpenObject();
            Writer.String("Token", LabelToken("F", Face));
            WriteFaceOutline(Writer, Partition, Face);
            Writer.CloseObject();
        }
    }
    Writer.CloseArray();
    Writer.OpenArray("FaceOrigins");
    if (Changed)
    {
        for (const FaceChangeHistory& History : Outcome.FaceHistories)
        {
            Writer.OpenObject();
            Writer.String("Face", LabelToken("F", History.Result));
            Writer.String("Origin", FaceChangeOriginLabel(History.Origin));
            Writer.String("Source", History.Origin == FaceChangeOrigin::Generated ? (History.Edge.PresentCondition() ? LabelToken("E", History.Edge) : "—")
                                                                                  : LabelToken("F", History.Face));
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

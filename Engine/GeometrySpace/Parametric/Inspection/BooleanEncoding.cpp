/*====================================================================================================================================
                                                      BOOLEANENCODING.CPP
====================================================================================================================================*/
// 🧩 General Fuse + Boolean inspection as JSON: result body, history, ghost arguments, all four operations, identities

#include "Engine/GeometrySpace/Parametric/Inspection/BooleanEncoding.h"
#include "Engine/GeometrySpace/Parametric/Inspection/InspectionEncoding.h"
#include "Engine/GeometrySpace/Parametric/Inspection/JsonWriter.h"

#include <algorithm>
#include <cmath>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int ArcSampleCount = 64;                                                       // [-] - Samples per curved edge

    // Ghost wireframe of one argument body (its edges sampled from their exact curves).
    void EncodeArgument(JsonWriter& Writer, const ModelPartition& Partition, BodyToken Body, const char* Name, double ClosedFormVolume)
    {
        const BodyInventory Inventory = Partition.CollectInventory(Body);
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.Number("Volume", ClosedFormVolume);
        Writer.Integer("Faces", static_cast<long>(Inventory.Faces.size()));
        Writer.OpenArray("Edges");
        for (EdgeToken Edge : Inventory.Edges)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            if (!Profile.Curve.PresentCondition()) continue;
            const CurveGeometry& Curve = Partition.Curves.Resolve(Profile.Curve);
            const int SampleCount = std::holds_alternative<StraightLine>(Curve) ? 2 : ArcSampleCount;
            Writer.OpenArray();
            for (int SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
            {
                const double τ = Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * SampleIndex / (SampleCount - 1);
                Writer.Point(nullptr, EvaluateCurve(Curve, τ).Position);
            }
            Writer.CloseArray();
        }
        Writer.CloseArray();
        Writer.CloseObject();
    }

    [[nodiscard]] const char* ArgumentLabel(int Argument) noexcept
    {
        return Argument == 0 ? "A" : Argument == 1 ? "B" : "Section";
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::string EncodeBoolean(const ModelPartition&                         Partition,
                          const BooleanScene&                           Scene,
                          BooleanOperation                              Shown,
                          const GeneralFuseOutcome&                     Fuse,
                          const std::array<BooleanInspectionResult, 4>& Results,
                          const AuditReport&                            Audit,
                          const BooleanInspectionTiming&                Timing)
{
    const int                      ShownIndex = static_cast<int>(Shown);
    const BooleanInspectionResult& Result     = Results[ShownIndex];
    const BooleanExpectation&      Expected   = Scene.Expected[ShownIndex];
    JsonWriter Writer;
    Writer.OpenObject();
    Writer.String("Kind", "Boolean");
    Writer.String("Specimen", Scene.Name);
    Writer.String("Description", Scene.Description);
    Writer.String("Operation", BooleanOperationLabel(Shown));
    Writer.OpenObject("Parameters");
    Writer.Number("Operation", ShownIndex);
    Writer.CloseObject();
    Writer.String("Refusal", Fuse.Refusal.empty() ? Result.Outcome.Refusal : Fuse.Refusal);

    // ---- summary of the shown operation
    Writer.OpenObject("Summary");
    Writer.Number("Volume", Result.Volume);
    Writer.Number("ExpectedVolume", Expected.Volume);
    Writer.Number("RelativeVolumeError", std::isnan(Expected.Volume) ? std::nan("") : std::fabs(Result.Volume - Expected.Volume) / std::max(1.0, std::fabs(Expected.Volume)));
    Writer.Integer("Faces", Result.Outcome.Faces);
    Writer.Integer("Shells", Result.Outcome.Shells);
    Writer.Integer("ExpectedShells", Expected.Shells);
    Writer.Integer("Solids", Result.Outcome.Solids);
    Writer.Integer("ExpectedSolids", Expected.Solids);
    Writer.Number("ConstructionMilliseconds", Timing.ConstructionMilliseconds);
    Writer.Number("FuseMilliseconds", Timing.FuseMilliseconds);
    Writer.Number("AssemblyMilliseconds", Result.Milliseconds);
    Writer.Number("AuditMilliseconds", Timing.AuditMilliseconds);
    Writer.CloseObject();

    // ---- General Fuse work counters (PaveFiller interferences, pave blocks, fragments per state)
    const GeneralFuseStatistics& Counters = Fuse.Statistics;
    Writer.OpenObject("Fuse");
    Writer.Integer("FacePairs", Counters.FacePairs);
    Writer.Integer("FacePairsTested", Counters.FacePairsTested);
    Writer.Integer("CoincidentPairs", Counters.CoincidentPairs);
    Writer.Integer("SectionBranches", Counters.SectionBranches);
    Writer.Integer("EdgeEdgeTests", Counters.EdgeEdgeTests);
    Writer.Integer("EdgeFaceTests", Counters.EdgeFaceTests);
    Writer.Integer("InterferenceVertices", Counters.InterferenceVertices);
    Writer.Integer("Paves", Counters.Paves);
    Writer.Integer("SplitEdges", Counters.SplitEdges);
    Writer.Integer("CommonBlocks", Counters.CommonBlocks);
    Writer.Integer("SectionEdges", Counters.SectionEdges);
    Writer.Integer("ImprintedEdges", Counters.ImprintedEdges);
    Writer.Integer("FragmentsA", Counters.Fragments[0]);
    Writer.Integer("FragmentsB", Counters.Fragments[1]);
    Writer.Integer("Inside", Counters.States[0]);
    Writer.Integer("Outside", Counters.States[1]);
    Writer.Integer("OnSame", Counters.States[2]);
    Writer.Integer("OnOpposite", Counters.States[3]);
    Writer.Integer("DroppedCycles", Counters.DroppedCycles);
    Writer.Number("IntersectMilliseconds", Counters.IntersectMilliseconds);
    Writer.Number("SplitMilliseconds", Counters.SplitMilliseconds);
    Writer.Number("ClassifyMilliseconds", Counters.ClassifyMilliseconds);
    Writer.CloseObject();

    // ---- all four operations + the volume identities they must satisfy
    Writer.OpenArray("Operations");
    for (int Index = 0; Index < 4; ++Index)
    {
        const BooleanInspectionResult& Entry = Results[Index];
        Writer.OpenObject();
        Writer.String("Operation", BooleanOperationLabel(static_cast<BooleanOperation>(Index)));
        Writer.String("Refusal", Entry.Outcome.Refusal);
        Writer.Number("Volume", Entry.Volume);
        Writer.Number("ExpectedVolume", Scene.Expected[Index].Volume);
        Writer.Integer("Faces", Entry.Outcome.Faces);
        Writer.Integer("Shells", Entry.Outcome.Shells);
        Writer.Integer("Solids", Entry.Outcome.Solids);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    const double Union = Results[0].Volume, Common = Results[1].Volume;                       // [len³] - V(A∪B), V(A∩B)
    const double Cut   = Results[2].Volume, Reverse = Results[3].Volume;                      // [len³] - V(A−B), V(B−A)
    Writer.OpenObject("Identities");
    Writer.Number("UnionPlusIntersection", Union + Common - Scene.FirstVolume - Scene.SecondVolume);
    Writer.Number("DifferencePlusIntersection", Cut + Common - Scene.FirstVolume);
    Writer.Number("ReversedPlusIntersection", Reverse + Common - Scene.SecondVolume);
    Writer.Number("Scale", std::max(1.0, Scene.FirstVolume + Scene.SecondVolume));
    Writer.CloseObject();

    // ---- ghost arguments
    Writer.OpenArray("Arguments");
    EncodeArgument(Writer, Partition, Scene.First, "A", Scene.FirstVolume);
    EncodeArgument(Writer, Partition, Scene.Second, "B", Scene.SecondVolume);
    Writer.CloseArray();

    // ---- result body (only when the shown operation produced one)
    if (Result.Outcome.Refusal.empty() && Result.Outcome.Body.PresentCondition())
    {
        EncodeAuditSection(Writer, Audit);
        EncodeBodyGeometry(Writer, Partition, Result.Outcome.Body);
        Writer.OpenArray("FaceHistories");
        for (const FaceHistory& History : Result.Outcome.FaceHistories)
        {
            Writer.OpenObject();
            Writer.String("Face", LabelToken("F", History.Result));
            Writer.String("Argument", ArgumentLabel(History.Argument));
            Writer.String("Source", LabelToken("F", History.Source));
            Writer.String("State", FragmentStateLabel(History.State));
            Writer.Boolean("Flipped", History.Flipped);
            Writer.CloseObject();
        }
        Writer.CloseArray();
        Writer.OpenArray("EdgeHistories");
        for (const EdgeHistory& History : Result.Outcome.EdgeHistories)
        {
            Writer.OpenObject();
            Writer.String("Edge", LabelToken("E", History.Result));
            Writer.String("Origin", ArgumentLabel(History.Argument));
            Writer.CloseObject();
        }
        Writer.CloseArray();
    }
    Writer.CloseObject();
    return Writer.Result();
}

} // namespace Frontier::GeometrySpace::Parametric

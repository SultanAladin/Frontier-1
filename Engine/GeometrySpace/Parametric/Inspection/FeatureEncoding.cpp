/*====================================================================================================================================
                                                      FEATUREENCODING.CPP
====================================================================================================================================*/
// 🧩 Sketch and feature-tree results → JSON: solved sketches, diagnosis, step reports, persistent names, body geometry

#include "Engine/GeometrySpace/Parametric/Inspection/FeatureEncoding.h"

#include "Engine/GeometrySpace/Parametric/Inspection/InspectionEncoding.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchSpline.h"

#include <cmath>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    [[nodiscard]] Vector3 PlacePoint(const OrthonormalFrame& Plane, Vector2 Position)
    {
        return Plane.Origin + Plane.XAxis * Position.X + Plane.YAxis * Position.Y;
    }

    // Curve polyline in the plane (arcs counter-clockwise from Start to End, splines per knot span).
    void WriteSketchCurve(JsonWriter& Writer, const SketchModel& Sketch, const SketchCurve& Curve, const OrthonormalFrame& Plane)
    {
        Writer.OpenArray("Polyline");
        for (const Vector2& Sample : SampleSketchCurve(Sketch, Curve)) Writer.Point(nullptr, PlacePoint(Plane, Sample));
        Writer.CloseArray();
    }

    void WriteNames(JsonWriter& Writer, const char* Key, const char* Prefix, const std::map<uint32_t, std::string>& Names, auto TokenAt)
    {
        Writer.OpenArray(Key);
        for (const auto& [Slot, Name] : Names)
        {
            Writer.OpenObject();
            Writer.String("Token", LabelToken(Prefix, TokenAt(Slot)));
            Writer.String("Name", Name);
            Writer.CloseObject();
        }
        Writer.CloseArray();
    }

    void WriteDiagnosis(JsonWriter& Writer, const SketchModel& Sketch, const SketchSolveOutcome& Solve)
    {
        Writer.OpenObject("Diagnosis");
        Writer.Boolean("Converged", Solve.Converged);
        Writer.Integer("Iterations", Solve.Iterations);
        Writer.Integer("Unknowns", Solve.Unknowns);
        Writer.Integer("Equations", Solve.Equations);
        Writer.Integer("Rank", Solve.Rank);
        Writer.Integer("StructuralRank", Solve.StructuralRank);
        Writer.Integer("DegreesOfFreedom", Solve.DegreesOfFreedom);
        Writer.Boolean("Singular", Solve.SingularCondition);
        Writer.Number("Conditioning", Solve.Conditioning);
        Writer.Number("GenericConditioning", Solve.GenericConditioning);
        Writer.Number("MaxResidual", Solve.MaxResidual);
        Writer.Number("Milliseconds", Solve.Milliseconds);
        Writer.OpenArray("Constraints");
        for (size_t Index = 0; Index < Sketch.Constraints.size() && Index < Solve.Constraints.size(); ++Index)
        {
            const SketchConstraint&       Constraint = Sketch.Constraints[Index];
            const SketchConstraintReport& Report     = Solve.Constraints[Index];
            Writer.OpenObject();
            Writer.String("Label", Constraint.Label.empty() ? std::string(SketchConstraintLabel(Constraint.Kind)) + std::to_string(Index) : Constraint.Label);
            Writer.String("Kind", SketchConstraintLabel(Constraint.Kind));
            if (EditableDimensionCondition(Constraint)) Writer.Number("Value", Constraint.Value);
            if (!Constraint.Parameter.empty()) Writer.String("Parameter", Constraint.Parameter);
            Writer.String("Status", SketchConstraintStatusLabel(Report.Status));
            Writer.Number("Residual", Report.Residual);
            Writer.CloseObject();
        }
        Writer.CloseArray();
        Writer.OpenArray("ConflictGroups");
        for (const std::vector<int>& Group : Solve.ConflictGroups)
        {
            Writer.OpenArray(nullptr);
            for (int Index : Group) Writer.String(nullptr, Sketch.Constraints[Index].Label.empty() ? SketchConstraintLabel(Sketch.Constraints[Index].Kind) : Sketch.Constraints[Index].Label);
            Writer.CloseArray();
        }
        Writer.CloseArray();
        Writer.CloseObject();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool EditableDimensionCondition(const SketchConstraint& Constraint)
{
    switch (Constraint.Kind)
    {
    case SketchConstraintKind::Distance:
    case SketchConstraintKind::HorizontalDistance:
    case SketchConstraintKind::VerticalDistance:
    case SketchConstraintKind::Length:
    case SketchConstraintKind::Radius:
    case SketchConstraintKind::Angle: return true;
    default:                          return false;
    }
}

std::string EncodeFeature(const ModelPartition& Partition, const FeatureSpecimen& Specimen, const FeatureTree& Tree,
                          const RegenerationOutcome& Outcome, const AuditReport& Audit, const FeatureInspection& Inspection)
{
    const bool Complete = Inspection.ShownSteps == Inspection.TotalSteps;
    JsonWriter Writer;
    Writer.OpenObject();
    Writer.String("Kind", "Feature");
    Writer.String("Specimen", Tree.Name);
    Writer.String("Description", Tree.Description);
    Writer.OpenObject("Parameters");
    for (const FeatureParameter& Parameter : Tree.Parameters) Writer.Number(Parameter.Name.c_str(), Parameter.Value);
    Writer.Integer("Upto", Inspection.ShownSteps);
    Writer.CloseObject();

    const BodyInventory Inventory = Outcome.Body.PresentCondition() ? Partition.CollectInventory(Outcome.Body) : BodyInventory{};
    Writer.OpenObject("Summary");
    Writer.OpenArray("Counts");
    const std::pair<const char*, long> Counts[] = { { "Vertices", static_cast<long>(Inventory.Vertices.size()) }, { "Edges", static_cast<long>(Inventory.Edges.size()) },
                                                    { "Faces", static_cast<long>(Inventory.Faces.size()) },       { "Genus", Audit.Genus } };
    for (const auto& [Name, Count] : Counts)
    {
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.Integer("Measured", Count);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.Integer("Fins", static_cast<long>(Inventory.Fins.size()));
    Writer.Integer("Regions", static_cast<long>(Inventory.Regions.size()));
    if (Outcome.Body.PresentCondition()) Writer.Number("Volume", Inspection.Volume);
    if (Complete && Outcome.Body.PresentCondition())
    {
        const double Expected = Specimen.ExpectedVolume(Tree);
        Writer.Number("ExpectedVolume", Expected);
        Writer.Number("VolumeError", std::fabs(Inspection.Volume - Expected));
        Writer.Number("VolumeAllowance", 1.0e-9 * std::max(1.0, std::fabs(Expected)));
    }
    Writer.Number("RegenerationMilliseconds", Outcome.Milliseconds);
    Writer.Number("AuditMilliseconds", Inspection.AuditMilliseconds);
    Writer.CloseObject();

    Writer.OpenObject("Feature");
    Writer.String("Oracle", Specimen.Oracle);
    Writer.Integer("ShownSteps", Inspection.ShownSteps);
    Writer.Integer("TotalSteps", Inspection.TotalSteps);
    Writer.OpenArray("ExpectedRefusals");
    if (Complete) for (const std::string& Label : Specimen.ExpectedRefusals(Tree)) Writer.String(nullptr, Label);
    Writer.CloseArray();
    Writer.OpenArray("Ranges");
    for (const FeatureParameter& Parameter : Tree.Parameters)
    {
        Writer.OpenObject();
        Writer.String("Name", Parameter.Name);
        Writer.Number("Low", Parameter.Low);
        Writer.Number("High", Parameter.High);
        Writer.String("Description", Parameter.Description);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.OpenArray("Steps");
    for (size_t Index = 0; Index < Outcome.Steps.size(); ++Index)
    {
        const FeatureStepReport& Report = Outcome.Steps[Index];
        const FeatureStep&       Step   = Tree.Steps[Index];
        Writer.OpenObject();
        Writer.String("Label", Report.Label);
        Writer.String("Kind", FeatureStepKindLabel(Report.Kind));
        Writer.String("Status", Report.Status == FeatureStepStatus::Built ? "Built" : "Refused");
        if (!Report.Refusal.empty()) Writer.String("Refusal", Report.Refusal);
        Writer.Number("Milliseconds", Report.Milliseconds);
        if (Report.Kind == FeatureStepKind::Extrude || Report.Kind == FeatureStepKind::Revolve)
        {
            Writer.String("Detail", Step.Profile + " · " + FeatureCombinationLabel(Step.Combination) +
                                    (Report.Kind == FeatureStepKind::Extrude ? " · extent " + std::to_string(EvaluateFeatureValue(Tree, Step.Extent)).substr(0, 6) : std::string()));
        }
        if (Report.Kind == FeatureStepKind::Sketch)
        {
            Writer.String("Detail", "DOF " + std::to_string(Report.Solve.DegreesOfFreedom) + " · rank " + std::to_string(Report.Solve.Rank) + "/" +
                                    std::to_string(Report.Solve.Unknowns) + (Report.Solve.SingularCondition ? " · singular" : ""));
        }
        if (Report.Kind == FeatureStepKind::Blend) Writer.String("Detail", std::string(Step.Shape == BlendProfile::Fillet ? "fillet " : "chamfer ") + std::to_string(EvaluateFeatureValue(Tree, Step.Size)).substr(0, 6));
        if (Report.Kind == FeatureStepKind::FaceOperation) Writer.String("Detail", std::string(FaceOperationLabel(Step.Operation)) + " " + std::to_string(EvaluateFeatureValue(Tree, Step.Amount)).substr(0, 6));
        if (Report.Kind == FeatureStepKind::SpaceCurve)
        {
            const auto Found = Outcome.Curves.find(Step.Label);
            std::string Detail = std::to_string(Step.Points.size()) + " points";
            if (Found != Outcome.Curves.end())
            {
                Detail += " → " + std::to_string(Found->second.Curve.Definition->Poles.size()) + " poles · worst " + std::to_string(Found->second.WorstDeviation * 1000.0).substr(0, 5) + " mm";
            }
            if (!Step.PoleEdits.empty()) Detail += " · " + std::to_string(Step.PoleEdits.size()) + " pole edits";
            Writer.String("Detail", Detail);
        }
        if (Report.Kind == FeatureStepKind::Loft || Report.Kind == FeatureStepKind::Network)
        {
            std::string Detail;
            for (size_t Section = 0; Section < Step.Sections.size(); ++Section) Detail += (Section ? " · " : "") + Step.Sections[Section];
            if (Step.Continuity > 0) Detail += " · G" + std::to_string(Step.Continuity);
            Writer.String("Detail", Detail);
        }
        if (Report.Kind == FeatureStepKind::Mirror)
        {
            const Vector3 Normal = Step.Plane.ZAxis;
            Writer.String("Detail", "normal (" + std::to_string(Normal.X).substr(0, 5) + ", " + std::to_string(Normal.Y).substr(0, 5) + ", " + std::to_string(Normal.Z).substr(0, 5) + ")");
        }
        Writer.OpenArray("References");
        for (size_t Reference = 0; Reference < Step.References.size(); ++Reference)
        {
            Writer.OpenObject();
            Writer.String("Name", Step.References[Reference].Name);
            if (Reference < Report.Resolutions.size())
            {
                const ResolvedReference& Resolved = Report.Resolutions[Reference];
                Writer.String("Method", ResolutionMethodLabel(Resolved.Method));
                if (!Resolved.Matched.empty()) Writer.String("Matched", Resolved.Matched);
                if (Resolved.Method != ResolutionMethod::Refused)
                {
                    Writer.String("Token", Step.References[Reference].Kind == ReferenceKind::Face ? LabelToken("F", Partition.Faces.TokenAt(Resolved.Slot))
                                                                                                  : LabelToken("E", Partition.Edges.TokenAt(Resolved.Slot)));
                }
            }
            Writer.CloseObject();
        }
        Writer.CloseArray();
        Writer.CloseObject();
    }
    Writer.CloseArray();

    // ---- sketches on their planes (construction curves flagged)
    Writer.OpenArray("Sketches");
    for (size_t Index = 0; Index < Outcome.Steps.size(); ++Index)
    {
        const FeatureStep& Step = Tree.Steps[Index];
        if (Step.Kind != FeatureStepKind::Sketch) continue;
        const auto Found = Outcome.Sketches.find(Step.Label);
        if (Found == Outcome.Sketches.end()) continue;
        OrthonormalFrame Plane = Step.Plane;
        Plane.Origin = Step.Plane.Origin + Step.Plane.ZAxis * EvaluateFeatureValue(Tree, Step.Elevation);
        Writer.OpenObject();
        Writer.String("Label", Step.Label);
        Writer.Boolean("Refused", Outcome.Steps[Index].Status == FeatureStepStatus::Refused);
        Writer.OpenArray("Curves");
        for (const SketchCurve& Curve : Found->second.Curves)
        {
            Writer.OpenObject();
            Writer.String("Name", SketchCurveName(Curve));
            Writer.Boolean("Construction", Curve.Construction);
            WriteSketchCurve(Writer, Found->second, Curve, Plane);
            Writer.CloseObject();
        }
        Writer.CloseArray();
        Writer.CloseObject();
    }
    Writer.CloseArray();

    Writer.OpenObject("Names");
    WriteNames(Writer, "Faces", "F", Outcome.Naming.Faces, [&](uint32_t Slot) { return Partition.Faces.TokenAt(Slot); });
    WriteNames(Writer, "Edges", "E", Outcome.Naming.Edges, [&](uint32_t Slot) { return Partition.Edges.TokenAt(Slot); });
    WriteNames(Writer, "Vertices", "V", Outcome.Naming.Vertices, [&](uint32_t Slot) { return Partition.Vertices.TokenAt(Slot); });
    Writer.CloseObject();
    Writer.CloseObject();

    EncodeAuditSection(Writer, Audit);
    if (Outcome.Body.PresentCondition()) EncodeBodyGeometry(Writer, Partition, Outcome.Body);
    else
    {
        Writer.OpenObject("Tree"); Writer.String("Label", "no body yet"); Writer.OpenArray("Members"); Writer.CloseArray(); Writer.CloseObject();
        Writer.OpenArray("Vertices"); Writer.CloseArray();
        Writer.OpenArray("Edges"); Writer.CloseArray();
        Writer.OpenArray("Faces"); Writer.CloseArray();
    }
    Writer.CloseObject();
    return Writer.Result();
}

std::string EncodeSketch(const SketchSpecimen& Specimen, const SketchModel& Solved, const SketchSolveOutcome& Solve, const SketchProfile& Profile, bool EditedCondition)
{
    JsonWriter Writer;
    Writer.OpenObject();
    Writer.String("Kind", "Sketch");
    Writer.String("Specimen", Specimen.Name);
    Writer.String("Description", Specimen.Description);
    Writer.OpenObject("Parameters");
    for (const SketchConstraint& Constraint : Solved.Constraints) if (EditableDimensionCondition(Constraint) && !Constraint.Label.empty()) Writer.Number(Constraint.Label.c_str(), Constraint.Value);
    Writer.CloseObject();

    Writer.OpenObject("Summary");
    Writer.Integer("ExpectedDegreesOfFreedom", Specimen.ExpectedDegreesOfFreedom);
    Writer.Integer("ExpectedRedundant", Specimen.ExpectedRedundant);
    Writer.Integer("ExpectedConflictGroups", Specimen.ExpectedConflictGroups);
    Writer.Boolean("ExpectedSingular", Specimen.ExpectedSingular);
    Writer.Boolean("ExpectedProfile", Specimen.ExpectedProfile);
    if (std::isfinite(Specimen.ExpectedArea) && !EditedCondition) Writer.Number("ExpectedArea", Specimen.ExpectedArea);
    Writer.Boolean("Edited", EditedCondition);
    Writer.Number("Area", Profile.Area);
    Writer.Integer("Loops", static_cast<long>(Profile.Loops.size()));
    if (!Profile.Refusal.empty()) Writer.String("ProfileRefusal", Profile.Refusal);
    Writer.CloseObject();

    WriteDiagnosis(Writer, Solved, Solve);
    Writer.OpenArray("Redundant");
    for (int Index : Solve.Redundant) Writer.String(nullptr, Solved.Constraints[Index].Label.empty() ? SketchConstraintLabel(Solved.Constraints[Index].Kind) : Solved.Constraints[Index].Label);
    Writer.CloseArray();

    // ---- geometry in the viewer's body format: points as vertices, curves as edges, no faces
    const OrthonormalFrame Plane{};
    Writer.OpenObject("Audit"); Writer.Boolean("Sound", true); Writer.Integer("Genus", 0); Writer.OpenArray("Tallies"); Writer.CloseArray(); Writer.OpenArray("Findings"); Writer.CloseArray(); Writer.CloseObject();
    Writer.OpenObject("Tree");
    Writer.String("Label", Specimen.Name + " · " + std::to_string(Solved.Points.size()) + " points · " + std::to_string(Solved.Curves.size()) + " curves");
    Writer.OpenArray("Members");
    for (size_t Index = 0; Index < Solved.Curves.size(); ++Index)
    {
        Writer.OpenObject();
        Writer.String("Token", SketchCurveName(Solved.Curves[Index]));
        Writer.String("Label", SketchCurveName(Solved.Curves[Index]) + (Solved.Curves[Index].Construction ? " (construction)" : "") + (Index < Solve.CurveFixed.size() && Solve.CurveFixed[Index] ? " · fixed" : " · free"));
        Writer.OpenArray("Members"); Writer.CloseArray();
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.CloseObject();
    Writer.OpenArray("Vertices");
    for (size_t Index = 0; Index < Solved.Points.size(); ++Index)
    {
        Writer.OpenObject();
        Writer.String("Token", "P" + std::to_string(Index));
        Writer.Point("Position", PlacePoint(Plane, Solved.Points[Index].Position));
        Writer.Boolean("Fixed", Index < Solve.PointFixed.size() && Solve.PointFixed[Index]);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.OpenArray("Edges");
    for (size_t Index = 0; Index < Solved.Curves.size(); ++Index)
    {
        const SketchCurve& Curve = Solved.Curves[Index];
        Writer.OpenObject();
        Writer.String("Token", SketchCurveName(Curve));
        Writer.String("Curve", Curve.Kind == SketchCurveKind::Line ? "Line" : Curve.Kind == SketchCurveKind::Arc ? "Arc" : Curve.Kind == SketchCurveKind::Spline ? SketchSplineFormLabel(Curve.Form) : "Circle");
        Writer.Boolean("Construction", Curve.Construction);
        Writer.Boolean("Fixed", Index < Solve.CurveFixed.size() && Solve.CurveFixed[Index]);
        WriteSketchCurve(Writer, Solved, Curve, Plane);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.OpenArray("Faces"); Writer.CloseArray();
    Writer.CloseObject();
    return Writer.Result();
}

void EncodeSketcherCatalogue(JsonWriter& Writer)
{
    for (const SketchSpecimen& Specimen : BuildSketchCatalogue())
    {
        Writer.OpenObject();
        Writer.String("Name", Specimen.Name);
        Writer.String("Kind", "Sketch");
        Writer.String("Description", Specimen.Description);
        Writer.OpenObject("Parameters");
        for (const SketchConstraint& Constraint : Specimen.Sketch.Constraints) if (EditableDimensionCondition(Constraint) && !Constraint.Label.empty()) Writer.Number(Constraint.Label.c_str(), Constraint.Value);
        Writer.CloseObject();
        Writer.CloseObject();
    }
    for (const std::string& Name : EnumerateFeatureSpecimens())
    {
        const FeatureSpecimen Specimen = BuildFeatureSpecimen(Name);
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.String("Kind", "Feature");
        Writer.String("Description", Specimen.Tree.Description);
        Writer.OpenObject("Parameters");
        for (const FeatureParameter& Parameter : Specimen.Tree.Parameters) Writer.Number(Parameter.Name.c_str(), Parameter.Value);
        Writer.Integer("Upto", static_cast<long>(Specimen.Tree.Steps.size()));
        Writer.CloseObject();
        Writer.OpenArray("Ranges");
        for (const FeatureParameter& Parameter : Specimen.Tree.Parameters)
        {
            Writer.OpenObject();
            Writer.String("Name", Parameter.Name);
            Writer.Number("Low", Parameter.Low);
            Writer.Number("High", Parameter.High);
            Writer.String("Description", Parameter.Description);
            Writer.CloseObject();
        }
        Writer.CloseArray();
        Writer.OpenArray("Steps");
        for (const FeatureStep& Step : Specimen.Tree.Steps) Writer.String(nullptr, Step.Label);
        Writer.CloseArray();
        Writer.CloseObject();
    }
}

} // namespace Frontier::GeometrySpace::Parametric

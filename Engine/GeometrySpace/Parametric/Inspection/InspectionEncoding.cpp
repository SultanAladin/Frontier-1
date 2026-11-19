/*====================================================================================================================================
                                                    INSPECTIONENCODING.CPP
====================================================================================================================================*/
// 🧩 One-line JSON description of a body for the HTML inspection viewport (topology tree, exact samples, audit)

#include "Engine/GeometrySpace/Parametric/Inspection/InspectionEncoding.h"

#include "Engine/GeometrySpace/Parametric/Blend/BlendRepository.h"
#include "Engine/GeometrySpace/Parametric/Boolean/BooleanRepository.h"
#include "Engine/GeometrySpace/Parametric/Inspection/IntersectionRepository.h"
#include "Engine/GeometrySpace/Parametric/Inspection/JsonWriter.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceOperationRepository.h"
#include "Engine/GeometrySpace/Parametric/Inspection/FeatureEncoding.h"
#include "Engine/GeometrySpace/Parametric/Connectivity/ModelPartition.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int ArcSampleCount   = 64;                                                     // [-] - Samples per circular edge
    constexpr int HatchLineCount   = 9;                                                      // [-] - Iso-lines per direction
    constexpr int HatchSampleCount = 97;                                                     // [-] - Samples per iso-line

    // Parameter-space polygon of every loop of a face (SP-curves sampled; gaps close implicitly).
    [[nodiscard]] std::vector<std::vector<Vector2>> SampleFaceBoundary(const ModelPartition& Partition, FaceToken Face)
    {
        std::vector<std::vector<Vector2>> Polygons;
        for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
        {
            std::vector<Vector2> Polygon;
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const FinProfile& Use = Partition.Fins.Resolve(Fin);
                if (!Use.SurfaceCurve.has_value()) return;
                const int    SampleCount = std::holds_alternative<ParameterLine>(*Use.SurfaceCurve) ? 2 : ArcSampleCount;
                const double Start = Partition.FinParameterStart(Fin);                       // [-] - τ at fin start
                const double End   = Partition.FinParameterEnd(Fin);                         // [-] - τ at fin end
                for (int SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
                {
                    const double τ = Start + (End - Start) * SampleIndex / (SampleCount - 1);
                    Polygon.push_back(EvaluateParameterCurve(*Use.SurfaceCurve, τ).Position);
                }
            });
            if (Polygon.size() >= 3) Polygons.push_back(std::move(Polygon));
        }
        return Polygons;
    }

    // Even–odd containment against all loop polygons.
    [[nodiscard]] bool InsideCondition(const std::vector<std::vector<Vector2>>& Polygons, Vector2 Probe)
    {
        bool Inside = false;                                                                 // [-] - Parity of crossings
        for (const auto& Polygon : Polygons)
        {
            for (size_t Index = 0, Previous = Polygon.size() - 1; Index < Polygon.size(); Previous = Index++)
            {
                const Vector2 First  = Polygon[Index];                                       // [-] - Segment end
                const Vector2 Second = Polygon[Previous];                                    // [-] - Segment start
                if ((First.Y > Probe.Y) != (Second.Y > Probe.Y) &&
                    Probe.X < (Second.X - First.X) * (Probe.Y - First.Y) / (Second.Y - First.Y) + First.X)
                {
                    Inside = !Inside;
                }
            }
        }
        return Inside;
    }

    void EncodeTree(JsonWriter& Writer, const ModelPartition& Partition, BodyToken Body)
    {
        const auto PrecisionSuffix = [](double Precision) -> std::string
        {
            if (!TolerantCondition(Precision)) return "";
            char Buffer[32];
            std::snprintf(Buffer, sizeof(Buffer), " \xC2\xB7 tol %.1e", Precision);
            return Buffer;
        };
        Writer.OpenObject("Tree");
        const BodyProfile& Profile = Partition.Bodies.Resolve(Body);
        constexpr const char* Categories[] = { "Acorn", "Wire", "Sheet", "Solid", "General" };
        Writer.String("Label", LabelToken("B", Body) + " Body \xC2\xB7 " + Categories[static_cast<int>(Profile.Category)]);
        Writer.String("Token", LabelToken("B", Body));
        Writer.OpenArray("Members");
        for (RegionToken Region : Profile.Regions)
        {
            const RegionProfile& Space = Partition.Regions.Resolve(Region);
            Writer.OpenObject();
            Writer.String("Label", LabelToken("R", Region) + " Region \xC2\xB7 " + (Space.Material == RegionMaterial::Solid ? "Solid" : "Void"));
            Writer.String("Token", LabelToken("R", Region));
            Writer.OpenArray("Members");
            for (SolidShellToken Shell : Space.Shells)
            {
                Writer.OpenObject();
                Writer.String("Label", LabelToken("S", Shell) + " SolidShell \xC2\xB7 " + std::to_string(Partition.Shells.Resolve(Shell).Faces.size()) + " faces");
                Writer.String("Token", LabelToken("S", Shell));
                Writer.OpenArray("Members");
                for (FaceToken Face : Partition.Shells.Resolve(Shell).Faces)
                {
                    const FaceProfile& Patch = Partition.Faces.Resolve(Face);
                    const char* Surface = Patch.Surface.PresentCondition() ? SurfaceCategoryLabel(Partition.Surfaces.Resolve(Patch.Surface)) : "none";
                    Writer.OpenObject();
                    Writer.String("Label", LabelToken("F", Face) + " Face \xC2\xB7 " + Surface + (Patch.Sense == Orientation::Forward ? " \xC2\xB7 +" : " \xC2\xB7 \xE2\x88\x92"));
                    Writer.String("Token", LabelToken("F", Face));
                    Writer.OpenArray("Members");
                    for (LoopToken Loop : Patch.Loops)
                    {
                        const LoopProfile& Cycle = Partition.Loops.Resolve(Loop);
                        Writer.OpenObject();
                        Writer.String("Label", LabelToken("L", Loop) + " Loop" + (Cycle.IsolatedVertex.PresentCondition() ? " \xC2\xB7 isolated " + LabelToken("V", Cycle.IsolatedVertex) : ""));
                        Writer.String("Token", LabelToken("L", Loop));
                        Writer.OpenArray("Members");
                        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
                        {
                            const FinProfile&  Use  = Partition.Fins.Resolve(Fin);
                            const EdgeProfile& Edge = Partition.Edges.Resolve(Use.Edge);
                            const char* Curve = Edge.Curve.PresentCondition() ? CurveCategoryLabel(Partition.Curves.Resolve(Edge.Curve)) : "none";
                            Writer.OpenObject();
                            Writer.String("Label", LabelToken("Fn", Fin) + " Fin " + (Use.Sense == Orientation::Forward ? "+" : "\xE2\x88\x92") + LabelToken("E", Use.Edge) +
                                                   " \xC2\xB7 " + Curve + " \xC2\xB7 " + LabelToken("V", Partition.FinStart(Fin)) + "\xE2\x86\x92" + LabelToken("V", Partition.FinEnd(Fin)) +
                                                   PrecisionSuffix(Edge.Precision));
                            Writer.String("Token", LabelToken("E", Use.Edge));
                            Writer.CloseObject();
                        });
                        Writer.CloseArray();
                        Writer.CloseObject();
                    }
                    Writer.CloseArray();
                    Writer.CloseObject();
                }
                Writer.CloseArray();
                Writer.CloseObject();
            }
            Writer.CloseArray();
            Writer.CloseObject();
        }
        Writer.CloseArray();
        Writer.CloseObject();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void EncodeAuditSection(JsonWriter& Writer, const AuditReport& Audit)
{
    Writer.OpenObject("Audit");
    Writer.Boolean("Sound", Audit.SoundCondition());
    Writer.OpenArray("Tallies");
    for (const AuditTally& Tally : Audit.Tallies)
    {
        Writer.OpenObject();
        Writer.String("Code", Tally.Code);
        Writer.String("Description", Tally.Description);
        Writer.Integer("Examined", Tally.Examined);
        Writer.Integer("Failed", Tally.Failed);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.OpenArray("Findings");
    for (const AuditFinding& Finding : Audit.Findings)
    {
        Writer.OpenObject();
        Writer.String("Code", Finding.Code);
        Writer.String("Subject", Finding.Subject);
        Writer.String("Detail", Finding.Detail);
        Writer.Number("Measured", Finding.Measured);
        Writer.Number("Allowed", Finding.Allowed);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.CloseObject();

}

void EncodeBodyGeometry(JsonWriter& Writer, const ModelPartition& Partition, BodyToken Body)
{
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    EncodeTree(Writer, Partition, Body);

    // ---- vertices
    Writer.OpenArray("Vertices");
    for (VertexToken Vertex : Inventory.Vertices)
    {
        const VertexProfile& Point = Partition.Vertices.Resolve(Vertex);
        Writer.OpenObject();
        Writer.String("Token", LabelToken("V", Vertex));
        Writer.Point("Position", Point.Position);
        Writer.Number("Precision", Point.Precision);
        Writer.CloseObject();
    }
    Writer.CloseArray();

    // ---- edges sampled from their exact curves
    Writer.OpenArray("Edges");
    for (EdgeToken Edge : Inventory.Edges)
    {
        const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
        if (!Profile.Curve.PresentCondition()) continue;
        const CurveGeometry& Curve = Partition.Curves.Resolve(Profile.Curve);
        const int SampleCount = std::holds_alternative<StraightLine>(Curve) ? 2 : ArcSampleCount;
        Writer.OpenObject();
        Writer.String("Token", LabelToken("E", Edge));
        Writer.String("Curve", CurveCategoryLabel(Curve));
        Writer.Number("Precision", Profile.Precision);
        Writer.OpenArray("Polyline");
        for (int SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
        {
            const double τ = Profile.IntervalLow + (Profile.IntervalHigh - Profile.IntervalLow) * SampleIndex / (SampleCount - 1);
            Writer.Point(nullptr, EvaluateCurve(Curve, τ).Position);
        }
        Writer.CloseArray();
        Writer.CloseObject();
    }
    Writer.CloseArray();

    // ---- faces: trimmed iso-parameter hatching and one outward normal
    Writer.OpenArray("Faces");
    for (FaceToken Face : Inventory.Faces)
    {
        const FaceProfile& Patch = Partition.Faces.Resolve(Face);
        if (!Patch.Surface.PresentCondition()) continue;
        const SurfaceGeometry& Surface  = Partition.Surfaces.Resolve(Patch.Surface);
        const auto             Polygons = SampleFaceBoundary(Partition, Face);
        Vector2 Low  = {  std::numeric_limits<double>::max(),  std::numeric_limits<double>::max() };   // [-] - Chart box minimum
        Vector2 High = { -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max() };   // [-] - Chart box maximum
        for (const auto& Polygon : Polygons)
        {
            for (Vector2 Sample : Polygon)
            {
                Low  = { std::min(Low.X, Sample.X),  std::min(Low.Y, Sample.Y) };
                High = { std::max(High.X, Sample.X), std::max(High.Y, Sample.Y) };
            }
        }
        Writer.OpenObject();
        Writer.String("Token", LabelToken("F", Face));
        Writer.String("Surface", SurfaceCategoryLabel(Surface));
        Writer.String("Sense", Patch.Sense == Orientation::Forward ? "Forward" : "Reversed");
        Writer.OpenArray("Hatch");
        Vector2 Anchor        = { (Low.X + High.X) * 0.5, (Low.Y + High.Y) * 0.5 };          // [-] - Normal-arrow chart point
        bool    AnchorInside  = !Polygons.empty() && InsideCondition(Polygons, Anchor);       // [-] - Anchor lies on the face
        for (int Direction = 0; Direction < 2 && !Polygons.empty(); ++Direction)
        {
            for (int LineIndex = 0; LineIndex < HatchLineCount; ++LineIndex)
            {
                const double Fraction = (LineIndex + 0.5) / HatchLineCount;                  // [0-1] - Iso-line position
                std::vector<Vector3> Run;
                const auto FlushRun = [&]()
                {
                    if (Run.size() >= 2)
                    {
                        Writer.OpenArray();
                        for (Vector3 Position : Run) Writer.Point(nullptr, Position);
                        Writer.CloseArray();
                    }
                    Run.clear();
                };
                for (int SampleIndex = 0; SampleIndex < HatchSampleCount; ++SampleIndex)
                {
                    const double  Along = static_cast<double>(SampleIndex) / (HatchSampleCount - 1);   // [0-1] - Along the iso-line
                    const Vector2 Chart = Direction == 0
                        ? Vector2{ Low.X + (High.X - Low.X) * Fraction, Low.Y + (High.Y - Low.Y) * Along }
                        : Vector2{ Low.X + (High.X - Low.X) * Along,    Low.Y + (High.Y - Low.Y) * Fraction };
                    if (InsideCondition(Polygons, Chart))
                    {
                        Run.push_back(EvaluateSurface(Surface, Chart).Position);
                        if (!AnchorInside) { Anchor = Chart; AnchorInside = true; }
                    }
                    else
                    {
                        FlushRun();
                    }
                }
                FlushRun();
            }
        }
        Writer.CloseArray();
        if (AnchorInside)
        {
            const double  Sign   = Patch.Sense == Orientation::Forward ? 1.0 : -1.0;         // [-] - Outward normal sign
            Writer.OpenObject("Normal");
            Writer.Point("Origin", EvaluateSurface(Surface, Anchor).Position);
            Writer.Point("Direction", EvaluateSurfaceNormal(Surface, Anchor) * Sign);
            Writer.CloseObject();
        }
        Writer.CloseObject();
    }
    Writer.CloseArray();
}

std::string EncodeInspection(const ModelPartition&   Partition,
                             const std::string&      SpecimenName,
                             const SpecimenOutcome&  Specimen,
                             const AuditReport&      Audit,
                             double                  Volume,
                             const InspectionTiming& Timing)
{
    const BodyInventory Inventory = Partition.CollectInventory(Specimen.Body);
    JsonWriter Writer;
    Writer.OpenObject();
    Writer.String("Kind", "Model");
    Writer.String("Specimen", SpecimenName);
    Writer.String("Description", Specimen.Description);
    Writer.OpenObject("Parameters");
    for (const auto& [Key, Magnitude] : Specimen.Parameters) Writer.Number(Key.c_str(), Magnitude);
    Writer.CloseObject();

    // ---- summary against independent expectations
    const SpecimenExpectation& Expected = Specimen.Expectation;
    Writer.OpenObject("Summary");
    const std::pair<const char*, std::pair<long, long>> Counts[] = {
        { "Vertices", { static_cast<long>(Inventory.Vertices.size()), Expected.Vertices } },
        { "Edges",    { static_cast<long>(Inventory.Edges.size()),    Expected.Edges } },
        { "Faces",    { static_cast<long>(Inventory.Faces.size()),    Expected.Faces } },
        { "Loops",    { static_cast<long>(Inventory.Loops.size()),    Expected.Loops } },
        { "Shells",   { static_cast<long>(Inventory.Shells.size()),   Expected.Shells } },
        { "Genus",    { Audit.Genus,                                   Expected.Genus } },
    };
    Writer.OpenArray("Counts");
    for (const auto& [Name, Pair] : Counts)
    {
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.Integer("Measured", Pair.first);
        Writer.Integer("Expected", Pair.second);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.Integer("Fins", static_cast<long>(Inventory.Fins.size()));
    Writer.Integer("Regions", static_cast<long>(Inventory.Regions.size()));
    Writer.Number("Volume", Volume);
    Writer.Number("ExpectedVolume", Expected.Volume);
    Writer.Number("RelativeVolumeError", std::fabs(Volume - Expected.Volume) / std::max(1.0e-300, std::fabs(Expected.Volume)));
    Writer.Number("ConstructionMilliseconds", Timing.ConstructionMilliseconds);
    Writer.Number("AuditMilliseconds", Timing.AuditMilliseconds);
    Writer.Number("VolumeMilliseconds", Timing.VolumeMilliseconds);
    Writer.CloseObject();

    EncodeAuditSection(Writer, Audit);
    EncodeBodyGeometry(Writer, Partition, Specimen.Body);
    Writer.CloseObject();
    return Writer.Result();
}

std::string EncodeRefusal(const std::string& SpecimenName, const std::string& Refusal)
{
    JsonWriter Writer;
    Writer.OpenObject();
    Writer.String("Specimen", SpecimenName);
    Writer.String("Refusal", Refusal);
    Writer.CloseObject();
    return Writer.Result();
}

std::string EncodeCatalogue()
{
    JsonWriter Writer;
    Writer.OpenObject();
    Writer.OpenArray("Specimens");
    for (const std::string& Name : EnumerateSpecimens())
    {
        ModelPartition  Scratch;
        SpecimenOutcome Defaults = ConstructSpecimen(Scratch, Name, {});
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.String("Kind", "Model");
        Writer.String("Description", Defaults.Description);
        Writer.OpenObject("Parameters");
        for (const auto& [Key, Magnitude] : Defaults.Parameters) Writer.Number(Key.c_str(), Magnitude);
        Writer.CloseObject();
        Writer.CloseObject();
    }
    for (const std::string& Name : EnumerateIntersectionScenes())
    {
        const IntersectionScene Defaults = ConstructIntersectionScene(Name, {});
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.String("Kind", "Intersection");
        Writer.String("Description", Defaults.Description);
        Writer.OpenObject("Parameters");
        for (const auto& [Key, Magnitude] : Defaults.Parameters) Writer.Number(Key.c_str(), Magnitude);
        Writer.CloseObject();
        Writer.CloseObject();
    }
    for (const std::string& Name : EnumerateBlendSpecimens())
    {
        ModelPartition      Scratch;
        const BlendSpecimen Defaults = ConstructBlendSpecimen(Scratch, Name);
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.String("Kind", "Blend");
        Writer.String("Family", Defaults.Family);
        Writer.String("Description", Defaults.Description);
        Writer.OpenObject("Parameters");
        Writer.Number("Size", Defaults.Settings.Size);
        Writer.Number("Source", 0.0);
        Writer.CloseObject();
        Writer.CloseObject();
    }
    for (const std::string& Name : EnumerateFaceOperationSpecimens())
    {
        ModelPartition              Scratch;
        const FaceOperationSpecimen Defaults = ConstructFaceOperationSpecimen(Scratch, Name);
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.String("Kind", "FaceOperation");
        Writer.String("Family", Defaults.Family);
        Writer.String("Description", Defaults.Description);
        Writer.OpenObject("Parameters");
        Writer.Number("Amount", 1.0);
        Writer.Number("Source", 0.0);
        Writer.CloseObject();
        Writer.CloseObject();
    }
    for (const std::string& Name : EnumerateBooleanScenes())
    {
        ModelPartition     Scratch;
        const BooleanScene Defaults = ConstructBooleanScene(Scratch, Name);
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.String("Kind", "Boolean");
        Writer.String("Description", Defaults.Description);
        Writer.OpenObject("Parameters");
        Writer.Number("Operation", 0.0);
        Writer.CloseObject();
        Writer.CloseObject();
    }
    EncodeSketcherCatalogue(Writer);
    Writer.CloseArray();
    Writer.CloseObject();
    return Writer.Result();
}

} // namespace Frontier::GeometrySpace::Parametric

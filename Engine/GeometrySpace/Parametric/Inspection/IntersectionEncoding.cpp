/*====================================================================================================================================
                                                   INTERSECTIONENCODING.CPP
====================================================================================================================================*/
// 🧩 Hatch patches, branch polylines, special points, metrics, audit and a scene tree as JSON

#include "Engine/GeometrySpace/Parametric/Inspection/IntersectionEncoding.h"
#include "Engine/GeometrySpace/Parametric/Inspection/JsonWriter.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr int    HatchLineCount   = 13;                                                  // [-]   - Iso-lines per direction
    constexpr int    HatchSampleCount = 97;                                                  // [-]   - Samples per iso-line
    constexpr double LongSpan         = 2.0 * std::numbers::pi + 1.0e-9;                     // [-]   - Spans beyond a full turn shrink

    // 📝 Display box: spans up to a full turn stay whole (hatch density); longer ones (planes, tall cylinders) shrink to the curve's range plus a margin.
    [[nodiscard]] ParameterBox DisplayBox(const ParameterBox& Box, const std::vector<Vector2>& Charts)
    {
        ParameterBox Shown = Box;
        double Range[2][2] = { { 1.0e300, -1.0e300 }, { 1.0e300, -1.0e300 } };               // [-] - Chart extents per direction
        for (Vector2 Chart : Charts)
        {
            Range[0][0] = std::min(Range[0][0], Chart.X); Range[0][1] = std::max(Range[0][1], Chart.X);
            Range[1][0] = std::min(Range[1][0], Chart.Y); Range[1][1] = std::max(Range[1][1], Chart.Y);
        }
        double* Lows[2]  = { &Shown.Low.X, &Shown.Low.Y };
        double* Highs[2] = { &Shown.High.X, &Shown.High.Y };
        for (int Direction = 0; Direction < 2; ++Direction)
        {
            const double Span = *Highs[Direction] - *Lows[Direction];
            if (Span <= LongSpan) continue;                                                   // Short spans: the world window clips
            double Low = Range[Direction][0], High = Range[Direction][1];
            if (Charts.empty()) { const double Middle = 0.5 * (*Lows[Direction] + *Highs[Direction]); Low = High = Middle; }
            const double Margin = std::max(0.6, 0.35 * (High - Low));                        // [-] - Context around the curve
            *Lows[Direction]  = std::max(*Lows[Direction], Low - Margin);
            *Highs[Direction] = std::min(*Highs[Direction], High + Margin);
        }
        return Shown;
    }

    using HatchLines = std::vector<std::vector<Vector3>>;

    [[nodiscard]] HatchLines SampleHatch(const SurfaceGeometry& Surface, const ParameterBox& Box)
    {
        HatchLines Lines;
        for (int Direction = 0; Direction < 2; ++Direction)
        {
            for (int LineIndex = 0; LineIndex < HatchLineCount; ++LineIndex)
            {
                const double Fraction = static_cast<double>(LineIndex) / (HatchLineCount - 1);    // [0-1] - Iso-line position
                std::vector<Vector3>& Line = Lines.emplace_back();
                for (int SampleIndex = 0; SampleIndex < HatchSampleCount; ++SampleIndex)
                {
                    const double  Along = static_cast<double>(SampleIndex) / (HatchSampleCount - 1);
                    const Vector2 Chart = Direction == 0
                        ? Vector2{ Box.Low.X + (Box.High.X - Box.Low.X) * Fraction, Box.Low.Y + (Box.High.Y - Box.Low.Y) * Along }
                        : Vector2{ Box.Low.X + (Box.High.X - Box.Low.X) * Along,    Box.Low.Y + (Box.High.Y - Box.Low.Y) * Fraction };
                    Line.push_back(EvaluateSurface(Surface, Chart).Position);
                }
            }
        }
        return Lines;
    }

    struct WorldBounds
    {
        Vector3 Low  = { 1.0e300, 1.0e300, 1.0e300 };                                        // [len] - Minimum corner
        Vector3 High = { -1.0e300, -1.0e300, -1.0e300 };                                     // [len] - Maximum corner

        void    Include(Vector3 Position) { Low = { std::min(Low.X, Position.X), std::min(Low.Y, Position.Y), std::min(Low.Z, Position.Z) };
                                            High = { std::max(High.X, Position.X), std::max(High.Y, Position.Y), std::max(High.Z, Position.Z) }; }
        [[nodiscard]] bool    EmptyCondition() const { return Low.X > High.X; }
        [[nodiscard]] Vector3 Centre() const { return (Low + High) * 0.5; }
        [[nodiscard]] double  HalfDiagonal() const { return EmptyCondition() ? 0.0 : 0.5 * Distance(Low, High); }
    };

    // 📝 Lines are cut where they leave the display window (a sphere), so a huge patch never swamps the curve.
    void EncodeHatch(JsonWriter& Writer, const char* Token, const SurfaceGeometry& Surface, const ParameterBox& Box, const HatchLines& Lines, Vector3 WindowCentre, double WindowRadius)
    {
        Writer.OpenObject();
        Writer.String("Token", Token);
        Writer.String("Surface", SurfaceCategoryLabel(Surface));
        Writer.String("Sense", "Forward");
        Writer.OpenArray("Hatch");
        for (const std::vector<Vector3>& Line : Lines)
        {
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
            for (Vector3 Position : Line)
            {
                if (Distance(Position, WindowCentre) <= WindowRadius) Run.push_back(Position); else FlushRun();
            }
            FlushRun();
        }
        Writer.CloseArray();
        Vector2 Anchor = (Box.Low + Box.High) * 0.5;                                          // [-] - Normal-arrow chart point
        const Vector2 Seed = ProjectOntoSurface(Surface, WindowCentre, Anchor);
        if (Distance(EvaluateSurface(Surface, Seed).Position, WindowCentre) <= WindowRadius) Anchor = Seed;
        Writer.OpenObject("Normal");
        Writer.Point("Origin", EvaluateSurface(Surface, Anchor).Position);
        Writer.Point("Direction", EvaluateSurfaceNormal(Surface, Anchor));
        Writer.CloseObject();
        Writer.CloseObject();
    }

    [[nodiscard]] const char* BranchCategoryLabel(BranchCategory Category)
    {
        switch (Category)
        {
            case BranchCategory::Line:   return "Line";
            case BranchCategory::Circle: return "Circle";
            default:                     return "Traced";
        }
    }

    [[nodiscard]] double BranchLength(const IntersectionBranch& Branch)
    {
        double Length = 0.0;                                                                 // [len] - Polyline length
        for (size_t Index = 1; Index < Branch.Chart.size(); ++Index) Length += Distance(Branch.Chart[Index].Position, Branch.Chart[Index - 1].Position);
        if (Branch.ClosedCondition && Branch.Chart.size() > 1) Length += Distance(Branch.Chart.back().Position, Branch.Chart.front().Position);
        return Length;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::string EncodeIntersection(const std::string&                SceneName,
                               const IntersectionScene&          Scene,
                               const SurfaceIntersectionOutcome& Outcome,
                               const IntersectionAuditReport&    Audit,
                               double                            SolveMilliseconds,
                               double                            AuditMilliseconds)
{
    JsonWriter Writer;
    Writer.OpenObject();
    Writer.String("Kind", "Intersection");
    Writer.String("Specimen", SceneName);
    Writer.String("Description", Scene.Description);
    Writer.OpenObject("Parameters");
    for (const auto& [Key, Magnitude] : Scene.Parameters) Writer.Number(Key.c_str(), Magnitude);
    Writer.CloseObject();

    // ---- summary: counts and metrics against independent expectations
    const IntersectionExpectation& Expected = Scene.Expectation;
    Writer.OpenObject("Summary");
    const std::pair<const char*, std::pair<long, long>> Counts[] = {
        { "Branches",       { static_cast<long>(Outcome.Branches.size()),      Expected.Branches } },
        { "Closed",         { Audit.ClosedBranches,                            Expected.ClosedBranches } },
        { "Special points", { static_cast<long>(Outcome.SpecialPoints.size()), Expected.SpecialPoints } },
    };
    Writer.OpenArray("Counts");
    for (const auto& [Name, Pair] : Counts)
    {
        Writer.OpenObject();
        Writer.String("Name", Name);
        Writer.Integer("Measured", Pair.first);
        if (Pair.second >= 0) Writer.Integer("Expected", Pair.second); else Writer.Number("Expected", std::numeric_limits<double>::quiet_NaN());
        Writer.CloseObject();
    }
    Writer.CloseArray();
    struct MetricRow { const char* Name; double Value; double Expected; bool Passed; };
    const MetricRow Metrics[] = {
        { "Length",                Audit.MeasuredLength,  Expected.Length,                    !std::isfinite(Audit.LengthError) || Audit.LengthError <= 1.0e-3 },
        { "Length relative error", Audit.LengthError,     1.0e-3,                             !std::isfinite(Audit.LengthError) || Audit.LengthError <= 1.0e-3 },
        { "Max residual",          Audit.MaximumResidual, 1.0e-9,                             Audit.MaximumResidual <= 1.0e-9 },
        { "Reference crossings",   static_cast<double>(Audit.ReferencePoints), std::numeric_limits<double>::quiet_NaN(), true },
        { "Reference misses",      static_cast<double>(Audit.ReferenceMisses), 0.0,           Audit.ReferenceMisses == 0 },
        { "Worst reference gap",   Audit.ReferenceReach,  1.5 * Scene.Settings.ChordTolerance + 1.0e-7, Audit.ReferenceMisses == 0 },
        { "Solve ms",              SolveMilliseconds,     std::numeric_limits<double>::quiet_NaN(), true },
        { "Audit ms",              AuditMilliseconds,     std::numeric_limits<double>::quiet_NaN(), true },
    };
    Writer.OpenArray("Metrics");
    for (const MetricRow& Row : Metrics)
    {
        Writer.OpenObject();
        Writer.String("Name", Row.Name);
        Writer.Number("Value", Row.Value);
        Writer.Number("Expected", Row.Expected);
        Writer.Boolean("Passed", Row.Passed);
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.String("Method", Outcome.Method);
    Writer.CloseObject();

    // ---- audit
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

    // ---- tree: scene → surfaces, branches, special points, solver statistics
    const IntersectionStatistics& Work = Outcome.Statistics;
    Writer.OpenObject("Tree");
    Writer.String("Label", SceneName + " \xC2\xB7 " + Outcome.Method);
    Writer.String("Token", "Scene");
    Writer.OpenArray("Members");
    for (const auto& [Token, Surface] : { std::pair{ "S1", &Scene.First }, std::pair{ "S2", &Scene.Second } })
    {
        Writer.OpenObject();
        Writer.String("Label", std::string(Token) + " Surface \xC2\xB7 " + SurfaceCategoryLabel(*Surface));
        Writer.String("Token", Token);
        Writer.CloseObject();
    }
    for (size_t Index = 0; Index < Outcome.Branches.size(); ++Index)
    {
        const IntersectionBranch& Branch = Outcome.Branches[Index];
        char Buffer[192];
        std::snprintf(Buffer, sizeof(Buffer), "C%zu Branch \xC2\xB7 %s \xC2\xB7 %zu pts \xC2\xB7 len %.6g \xC2\xB7 %s / %s%s", Index, BranchCategoryLabel(Branch.Category), Branch.Chart.size(),
                      BranchLength(Branch), BranchTerminationLabel(Branch.StartTermination), BranchTerminationLabel(Branch.EndTermination), Branch.TangentialCondition ? " \xC2\xB7 tangential" : "");
        Writer.OpenObject();
        Writer.String("Label", Buffer);
        Writer.String("Token", "C" + std::to_string(Index));
        Writer.CloseObject();
    }
    for (size_t Index = 0; Index < Outcome.SpecialPoints.size(); ++Index)
    {
        const IntersectionSpecialPoint& Point = Outcome.SpecialPoints[Index];
        char Buffer[160];
        std::snprintf(Buffer, sizeof(Buffer), "P%zu %s \xC2\xB7 (%.4f, %.4f, %.4f)", Index, Point.Category == SpecialPointCategory::Singular ? "Singular" : "Touch",
                      Point.Position.X, Point.Position.Y, Point.Position.Z);
        Writer.OpenObject();
        Writer.String("Label", Buffer);
        Writer.String("Token", "P" + std::to_string(Index));
        Writer.CloseObject();
    }
    {
        char Buffer[256];
        std::snprintf(Buffer, sizeof(Buffer), "Statistics \xC2\xB7 lattice %d curves / %d hits \xC2\xB7 loop %d pairs / %d seeds \xC2\xB7 traced %d skipped %d \xC2\xB7 %d corrections \xC2\xB7 %d rejections",
                      Work.LatticeCurves, Work.LatticeCrossings, Work.LoopCellPairs, Work.LoopSeeds, Work.SeedsTraced, Work.SeedsSkipped, Work.Corrections, Work.StepRejections);
        Writer.OpenObject();
        Writer.String("Label", Buffer);
        Writer.String("Token", "Statistics");
        Writer.CloseObject();
    }
    Writer.CloseArray();
    Writer.CloseObject();

    // ---- vertices = special points
    Writer.OpenArray("Vertices");
    for (size_t Index = 0; Index < Outcome.SpecialPoints.size(); ++Index)
    {
        Writer.OpenObject();
        Writer.String("Token", "P" + std::to_string(Index));
        Writer.Point("Position", Outcome.SpecialPoints[Index].Position);
        Writer.CloseObject();
    }
    Writer.CloseArray();

    // ---- edges = branch polylines
    std::vector<Vector2> FirstCharts, SecondCharts;
    Writer.OpenArray("Edges");
    for (size_t Index = 0; Index < Outcome.Branches.size(); ++Index)
    {
        const IntersectionBranch& Branch = Outcome.Branches[Index];
        Writer.OpenObject();
        Writer.String("Token", "C" + std::to_string(Index));
        Writer.String("Curve", BranchCategoryLabel(Branch.Category));
        Writer.Number("Precision", Scene.Settings.ChordTolerance);
        Writer.OpenArray("Polyline");
        for (const IntersectionChartPoint& Point : Branch.Chart)
        {
            Writer.Point(nullptr, Point.Position);
            FirstCharts.push_back(Point.First);
            SecondCharts.push_back(Point.Second);
        }
        if (Branch.ClosedCondition && !Branch.Chart.empty()) Writer.Point(nullptr, Branch.Chart.front().Position);
        Writer.CloseArray();
        Writer.CloseObject();
    }
    Writer.CloseArray();
    for (const IntersectionSpecialPoint& Point : Outcome.SpecialPoints)
    {
        FirstCharts.push_back(ProjectOntoSurface(Scene.First, Point.Position, (Scene.FirstBox.Low + Scene.FirstBox.High) * 0.5));
        SecondCharts.push_back(ProjectOntoSurface(Scene.Second, Point.Position, (Scene.SecondBox.Low + Scene.SecondBox.High) * 0.5));
    }

    // ---- faces = the two surface patches
    // 💡 Display window: centred on the curve, radius max(1.6 × curve extent, 1.2 × the smaller patch) — the smaller
    //    surface stays whole for context while an enormous partner (> 3× larger) is cut down to the neighbourhood that matters.
    const ParameterBox FirstShown  = DisplayBox(Scene.FirstBox, FirstCharts);
    const ParameterBox SecondShown = DisplayBox(Scene.SecondBox, SecondCharts);
    const HatchLines   FirstLines  = SampleHatch(Scene.First, FirstShown);
    const HatchLines   SecondLines = SampleHatch(Scene.Second, SecondShown);
    WorldBounds Curve, FirstPatch, SecondPatch;
    for (const IntersectionBranch& Branch : Outcome.Branches) for (const IntersectionChartPoint& Point : Branch.Chart) Curve.Include(Point.Position);
    for (const IntersectionSpecialPoint& Point : Outcome.SpecialPoints) Curve.Include(Point.Position);
    for (const auto& Line : FirstLines) for (Vector3 Position : Line) FirstPatch.Include(Position);
    for (const auto& Line : SecondLines) for (Vector3 Position : Line) SecondPatch.Include(Position);
    const WorldBounds& Smaller = FirstPatch.HalfDiagonal() <= SecondPatch.HalfDiagonal() ? FirstPatch : SecondPatch;
    const Vector3 WindowCentre = Curve.EmptyCondition() ? Smaller.Centre() : Curve.Centre();
    const WorldBounds& Larger  = &Smaller == &FirstPatch ? SecondPatch : FirstPatch;
    double WindowRadius = std::max(1.6 * Curve.HalfDiagonal(), 1.2 * Smaller.HalfDiagonal() + Distance(WindowCentre, Smaller.Centre()));
    if (Larger.HalfDiagonal() <= 3.0 * Smaller.HalfDiagonal()) WindowRadius = std::max(WindowRadius, 1.2 * Larger.HalfDiagonal() + Distance(WindowCentre, Larger.Centre()));
    Writer.OpenArray("Faces");
    EncodeHatch(Writer, "S1", Scene.First, FirstShown, FirstLines, WindowCentre, WindowRadius);
    EncodeHatch(Writer, "S2", Scene.Second, SecondShown, SecondLines, WindowCentre, WindowRadius);
    Writer.CloseArray();
    Writer.CloseObject();
    return Writer.Result();
}

} // namespace Frontier::GeometrySpace::Parametric

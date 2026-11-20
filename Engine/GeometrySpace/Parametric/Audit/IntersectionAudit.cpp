/*====================================================================================================================================
                                                    INTERSECTIONAUDIT.CPP
====================================================================================================================================*/
// 🧩 Residual, box, completeness, termination, count and length checks for intersection outcomes

#include "Engine/GeometrySpace/Parametric/Audit/IntersectionAudit.h"
#include "Engine/GeometrySpace/Parametric/Intersection/CurveSurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Intersection/ImplicitSurface.h"
#include "Engine/GeometrySpace/Parametric/Intersection/IsoParameterCurve.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <tuple>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    constexpr double ResidualAllowance = 1.0e-9;                                             // [len] - I1 allowance
    constexpr double LengthAllowance   = 1.0e-3;                                             // [-]   - I6 relative allowance

    [[nodiscard]] std::string BranchLabel(size_t Index) { return "C" + std::to_string(Index); }

    [[nodiscard]] double SegmentDistance(Vector3 Start, Vector3 End, Vector3 Position)
    {
        const Vector3 Span = End - Start;
        const double  λ    = std::clamp(Dot(Position - Start, Span) / std::max(Dot(Span, Span), 1.0e-300), 0.0, 1.0);
        return Distance(Start + Span * λ, Position);
    }

    [[nodiscard]] double NearestBranchDistance(const SurfaceIntersectionOutcome& Outcome, Vector3 Position)
    {
        double Nearest = std::numeric_limits<double>::infinity();                         // [len] - Running minimum
        for (const IntersectionBranch& Branch : Outcome.Branches)
        {
            const auto& Chart = Branch.Chart;
            for (size_t Index = 0; Index + 1 < Chart.size(); ++Index) Nearest = std::min(Nearest, SegmentDistance(Chart[Index].Position, Chart[Index + 1].Position, Position));
            if (Branch.ClosedCondition && Chart.size() > 1) Nearest = std::min(Nearest, SegmentDistance(Chart.back().Position, Chart.front().Position, Position));
            if (Chart.size() == 1) Nearest = std::min(Nearest, Distance(Chart.front().Position, Position));
        }
        for (const IntersectionSpecialPoint& Point : Outcome.SpecialPoints) Nearest = std::min(Nearest, Distance(Point.Position, Position));
        return Nearest;
    }

    // 📝 Dense reference: crossings of both surfaces' iso-lattices with the other surface, inside both boxes.
    [[nodiscard]] std::vector<Vector3> CollectReference(const IntersectionScene& Scene, int Divisions)
    {
        std::vector<Vector3> Points;
        const auto InsideBoth = [&](Vector3 Position)
        {
            return InsideParameterBox(Scene.First, Scene.FirstBox, ProjectOntoSurface(Scene.First, Position, (Scene.FirstBox.Low + Scene.FirstBox.High) * 0.5), 1.0e-9)
                && InsideParameterBox(Scene.Second, Scene.SecondBox, ProjectOntoSurface(Scene.Second, Position, (Scene.SecondBox.Low + Scene.SecondBox.High) * 0.5), 1.0e-9);
        };
        for (const auto& [Surface, Box, Target] : { std::tuple{ &Scene.First, &Scene.FirstBox, &Scene.Second }, std::tuple{ &Scene.Second, &Scene.SecondBox, &Scene.First } })
        {
            for (int Line = 0; Line <= Divisions; ++Line)
            {
                for (const IsoDirection Direction : { IsoDirection::FixedU, IsoDirection::FixedV })
                {
                    // ⚠️ Offset by a quarter cell so the reference lines never coincide with the solver's 8 × 8 lattice.
                    const double Fraction = (Line + 0.25) / (Divisions + 1);
                    const double Value = Direction == IsoDirection::FixedU ? Box->Low.X + (Box->High.X - Box->Low.X) * Fraction : Box->Low.Y + (Box->High.Y - Box->Low.Y) * Fraction;
                    const IsoParameterCurve Iso = ExtractIsoParameterCurve(*Surface, Direction, Value);
                    if (Iso.DegenerateCondition || Iso.ApproximateCondition) continue;   // Reference lines must be exact
                    const double RunLow  = Direction == IsoDirection::FixedU ? Box->Low.Y : Box->Low.X;
                    const double RunHigh = Direction == IsoDirection::FixedU ? Box->High.Y : Box->High.X;
                    const CurveSurfaceOutcome Hits = IntersectCurveSurface(Iso.Curve, Iso.Offset + Iso.Scale * RunLow, Iso.Offset + Iso.Scale * RunHigh, *Target);
                    for (const CurveSurfaceCrossing& Hit : Hits.Crossings) if (InsideBoth(Hit.Position)) Points.push_back(Hit.Position);
                }
            }
        }
        return Points;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

IntersectionAuditReport AuditIntersection(const IntersectionScene& Scene, const SurfaceIntersectionOutcome& Outcome, int ReferenceDivisions)
{
    IntersectionAuditReport Report;
    const auto Fail = [&](const char* Code, std::string Subject, std::string Detail, double Measured, double Allowed)
    {
        Report.Findings.push_back({ Code, std::move(Subject), std::move(Detail), Measured, Allowed });
    };

    // ---- I1 / I2: every chart point on both surfaces and inside both boxes.
    AuditTally Residual{ "I1", "Chart points lie on both surfaces (≤ 1e-9)" };
    AuditTally Boxes{ "I2", "Chart points lie inside both parameter boxes" };
    for (size_t BranchIndex = 0; BranchIndex < Outcome.Branches.size(); ++BranchIndex)
    {
        const IntersectionBranch& Branch = Outcome.Branches[BranchIndex];
        double BranchResidual = 0.0;                                                         // [len] - Worst on this branch
        bool   Outside        = false;                                                       // [-]   - Any point outside
        for (const IntersectionChartPoint& Point : Branch.Chart)
        {
            const double Off = std::max(std::fabs(MeasureSurfaceDistance(Scene.First, Point.Position).Distance), std::fabs(MeasureSurfaceDistance(Scene.Second, Point.Position).Distance));
            BranchResidual = std::max(BranchResidual, Off);
            Outside = Outside || !InsideParameterBox(Scene.First, Scene.FirstBox, Point.First, 1.0e-7) || !InsideParameterBox(Scene.Second, Scene.SecondBox, Point.Second, 1.0e-7);
        }
        for (size_t Index = 1; Index < Branch.Chart.size(); ++Index) Report.MeasuredLength += Distance(Branch.Chart[Index].Position, Branch.Chart[Index - 1].Position);
        if (Branch.ClosedCondition && Branch.Chart.size() > 1) Report.MeasuredLength += Distance(Branch.Chart.back().Position, Branch.Chart.front().Position);
        Report.ClosedBranches += Branch.ClosedCondition ? 1 : 0;
        Report.MaximumResidual = std::max(Report.MaximumResidual, BranchResidual);
        Residual.Examined += 1; Boxes.Examined += 1;
        if (BranchResidual > ResidualAllowance) { Residual.Failed += 1; Fail("I1", BranchLabel(BranchIndex), "chart point off a surface", BranchResidual, ResidualAllowance); }
        if (Outside) { Boxes.Failed += 1; Fail("I2", BranchLabel(BranchIndex), "chart point outside a parameter box", 0.0, 0.0); }
    }

    // ---- I3: completeness against the dense reference lattice.
    AuditTally Completeness{ "I3", "Dense reference-lattice crossings lie on a branch or special point" };
    const double Reach = 1.5 * Scene.Settings.ChordTolerance + 1.0e-7;                       // [len] - Allowed reference distance
    for (const Vector3& Reference : CollectReference(Scene, ReferenceDivisions))
    {
        const double Gap = NearestBranchDistance(Outcome, Reference);
        Completeness.Examined += 1;
        Report.ReferenceReach = std::max(Report.ReferenceReach, Gap);
        if (Gap <= Reach) continue;
        Completeness.Failed += 1;
        if (Completeness.Failed <= 5)
        {
            char Buffer[96];
            std::snprintf(Buffer, sizeof(Buffer), "missed crossing at (%.4f, %.4f, %.4f)", Reference.X, Reference.Y, Reference.Z);
            Fail("I3", "reference", Buffer, Gap, Reach);
        }
    }
    Report.ReferencePoints = Completeness.Examined;
    Report.ReferenceMisses = Completeness.Failed;

    // ---- I4: terminations.
    AuditTally Terminations{ "I4", "Branch ends: no budget exhaustion; singular ends on special points" };
    for (size_t BranchIndex = 0; BranchIndex < Outcome.Branches.size(); ++BranchIndex)
    {
        const IntersectionBranch& Branch = Outcome.Branches[BranchIndex];
        Terminations.Examined += 1;
        bool Sound = Branch.StartTermination != BranchTermination::Limit && Branch.EndTermination != BranchTermination::Limit;
        const auto OnSpecial = [&](Vector3 Position)
        {
            return std::any_of(Outcome.SpecialPoints.begin(), Outcome.SpecialPoints.end(), [&](const IntersectionSpecialPoint& Point) { return Distance(Point.Position, Position) <= 1.0e-6; });
        };
        if (Branch.StartTermination == BranchTermination::Singular && !OnSpecial(Branch.Chart.front().Position)) Sound = false;
        if (Branch.EndTermination == BranchTermination::Singular && !OnSpecial(Branch.Chart.back().Position)) Sound = false;
        if (!Sound)
        {
            Terminations.Failed += 1;
            Fail("I4", BranchLabel(BranchIndex), std::string("termination ") + BranchTerminationLabel(Branch.StartTermination) + " / " + BranchTerminationLabel(Branch.EndTermination) + " is inconsistent", 0.0, 0.0);
        }
    }

    // ---- I5: counts against independent expectations.
    AuditTally Counts{ "I5", "Branch, closed-branch and special-point counts match expectations" };
    const IntersectionExpectation& Expected = Scene.Expectation;
    const std::tuple<const char*, int, int> CountRows[] = {
        { "branches", static_cast<int>(Outcome.Branches.size()), Expected.Branches },
        { "closed branches", Report.ClosedBranches, Expected.ClosedBranches },
        { "special points", static_cast<int>(Outcome.SpecialPoints.size()), Expected.SpecialPoints },
    };
    for (const auto& [Name, Measured, Wanted] : CountRows)
    {
        if (Wanted < 0) continue;
        Counts.Examined += 1;
        if (Measured != Wanted)
        {
            Counts.Failed += 1;
            Fail("I5", Name, std::to_string(Measured) + " found, " + std::to_string(Wanted) + " expected", Measured, Wanted);
        }
    }

    // ---- I6: total length.
    AuditTally Length{ "I6", "Total length matches the exact reference (relative ≤ 1e-3)" };
    Report.LengthError = std::numeric_limits<double>::quiet_NaN();
    if (std::isfinite(Expected.Length))
    {
        Length.Examined = 1;
        Report.LengthError = Expected.Length > 0.0 ? std::fabs(Report.MeasuredLength - Expected.Length) / Expected.Length : Report.MeasuredLength;
        if (Report.LengthError > LengthAllowance)
        {
            Length.Failed = 1;
            Fail("I6", "length", "total length differs from the reference", Report.LengthError, LengthAllowance);
        }
    }
    Report.Tallies = { Residual, Boxes, Completeness, Terminations, Counts, Length };
    return Report;
}

} // namespace Frontier::GeometrySpace::Parametric

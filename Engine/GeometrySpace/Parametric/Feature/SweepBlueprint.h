/*====================================================================================================================================
                                                         SWEEPBLUEPRINT.H
====================================================================================================================================*/
// 🧩 Shared blueprint writer for profile sweeps (extrude, revolve, evolution): faces take their sense from an outward probe

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Construction/BodyArrangement.h"
#include "Engine/GeometrySpace/Parametric/Feature/ProfileSweep.h"
#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/ProjectedCurve.h"

#include <algorithm>
#include <functional>
#include <numbers>
#include <optional>
#include <vector>

namespace Frontier::GeometrySpace::Parametric::Sweeping
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    BLUEPRINT WRITER
//------------------------------------------------------------------------------------------------------------------------

    constexpr double FullTurn = 2.0 * std::numbers::pi;                                      // [rad] - 2π

    [[nodiscard]] inline Orientation Opposite(Orientation Sense) noexcept { return Sense == Orientation::Forward ? Orientation::Reversed : Orientation::Forward; }

    struct LoopUse
    {
        int                    Edge   = -1;                                                  // [-] - Blueprint edge
        Orientation            Sense  = Orientation::Forward;                                // [-] - Along / against τ
        std::optional<Vector2> Anchor = std::nullopt;                                        // [-] - Chart point of the use midpoint (periodic charts)
    };

    struct LoopPlan
    {
        std::vector<LoopUse> Uses  = {};                                                     // [-] - Loop order (either direction)
        bool                 Outer = true;                                                   // [-] - Outer boundary vs hole
    };

    // 📝 Blueprint writer: faces get their sense from an outward probe and their loops are turned to agree with it
    //    (Σ∮𝑢 d𝑣 > 0 for outer loops of a Forward face, audit G5), so callers never reason about loop direction.
    class BlueprintWriter
    {
    public:
        int AddVertex(Vector3 Position)
        {
            Blueprint.Vertices.push_back({ Position });
            return static_cast<int>(Blueprint.Vertices.size()) - 1;
        }

        int AddEdge(CurveGeometry Curve, double Low, double High, int Start, int End)
        {
            BlueprintEdge Edge;
            Edge.Curve = std::move(Curve); Edge.Low = Low; Edge.High = High; Edge.Start = Start; Edge.End = End;
            Blueprint.Edges.push_back(std::move(Edge));
            return static_cast<int>(Blueprint.Edges.size()) - 1;
        }

        [[nodiscard]] Vector3 EdgeMidpoint(int Edge) const
        {
            const BlueprintEdge& Record = Blueprint.Edges[Edge];
            return EvaluateCurve(Record.Curve, 0.5 * (Record.Low + Record.High)).Position;
        }

        void AddFace(const SurfaceGeometry& Surface, Vector3 Probe, Vector3 Outward, std::vector<LoopPlan> Loops, SweepFace Role, Vector2 Seed = { std::numbers::pi, 0.0 })
        {
            BlueprintFace Face;
            Face.Surface = Surface;
            const Vector2       Parameter = ProjectOntoSurface(Surface, Probe, Seed);                   // Seed: chart point near the probe
            const SurfaceSample Sample    = EvaluateSurface(Surface, Parameter);
            Face.Sense = Dot(Cross(Sample.DerivativeU, Sample.DerivativeV), Outward) > 0.0 ? Orientation::Forward : Orientation::Reversed;
            for (LoopPlan& Plan : Loops)
            {
                std::vector<BlueprintUse> Uses;
                for (const LoopUse& Use : Plan.Uses)
                {
                    const BlueprintEdge& Edge = Blueprint.Edges[Use.Edge];
                    const Vector2 Anchor = Use.Anchor.value_or(Vector2{ 0.0, 0.0 });
                    const ParameterWindow Window = { Anchor - Vector2{ 1.0, 1.0 }, Anchor + Vector2{ 1.0, 1.0 } };
                    BlueprintUse Record;
                    Record.Edge         = Use.Edge;
                    Record.Sense        = Use.Sense;
                    Record.SurfaceCurve = FitSurfaceCurve(Surface, Edge.Curve, Edge.Low, Edge.High, Window, Use.Anchor);
                    Uses.push_back(std::move(Record));
                }
                const double Expected = (Face.Sense == Orientation::Forward ? 1.0 : -1.0) * (Plan.Outer ? 1.0 : -1.0);
                if (MeasureChartArea(Uses) * Expected < 0.0)
                {
                    std::reverse(Uses.begin(), Uses.end());
                    for (BlueprintUse& Record : Uses) Record.Sense = Opposite(Record.Sense);
                }
                Face.Loops.push_back(std::move(Uses));
            }
            Blueprint.Faces.push_back(std::move(Face));
            Roles.push_back(std::move(Role));
        }

        [[nodiscard]] double MeasureChartArea(const std::vector<BlueprintUse>& Uses) const
        {
            std::vector<Vector2> Polygon;
            constexpr int Samples = 24;
            for (const BlueprintUse& Use : Uses)
            {
                const BlueprintEdge& Edge = Blueprint.Edges[Use.Edge];
                for (int Index = 0; Index < Samples; ++Index)
                {
                    const double Fraction = static_cast<double>(Index) / Samples;
                    const double Tau = Use.Sense == Orientation::Forward ? Edge.Low + Fraction * (Edge.High - Edge.Low) : Edge.High - Fraction * (Edge.High - Edge.Low);
                    Polygon.push_back(EvaluateParameterCurve(Use.SurfaceCurve, Tau).Position);
                }
            }
            double Area = 0.0;
            for (size_t Index = 0; Index < Polygon.size(); ++Index)
            {
                const Vector2 First = Polygon[Index], Second = Polygon[(Index + 1) % Polygon.size()];
                Area += 0.5 * (First.X * Second.Y - Second.X * First.Y);
            }
            return Area;
        }

        BodyBlueprint          Blueprint = {};
        std::vector<SweepFace> Roles     = {};
    };

    [[nodiscard]] inline SweepOutcome Assemble(ModelPartition& Partition, BlueprintWriter& Writer, const char* Label)
    {
        SweepOutcome Outcome;
        const ArrangementOutcome Assembly = AssembleBody(Partition, Writer.Blueprint, Label);
        if (!Assembly.Refusal.empty()) { Outcome.Refusal = Assembly.Refusal; return Outcome; }
        Outcome.Body  = Assembly.Body;
        Outcome.Faces = Assembly.Faces;
        Outcome.Roles = Writer.Roles;
        return Outcome;
    }

    // Right of travel (outward for an outer CCW / hole CW profile) at a segment fraction, in sketch coordinates.
    [[nodiscard]] inline Vector2 OutwardAt(const ProfileSegment& Segment, double Fraction)
    {
        const Vector2 Tangent = EvaluateProfileTangent(Segment, Fraction);
        return Normalize(Vector2{ Tangent.Y, -Tangent.X });
    }

    // Spline lifted onto the sketch plane at one elevation: an exact polynomial NURBS curve (unit weights).
    [[nodiscard]] inline std::optional<NurbsCurve> LiftSpline(const SketchSplineCurve& Spline, const std::function<Vector3(Vector2)>& Place)
    {
        std::vector<Vector3> Poles;
        for (const Vector2& Pole : Spline.Poles) Poles.push_back(Place(Pole));
        return ConstructNurbsCurve(Spline.Degree, Spline.Knots, std::move(Poles), std::vector<double>(Spline.Poles.size(), 1.0));
    }

} // namespace Frontier::GeometrySpace::Parametric::Sweeping

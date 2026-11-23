//============================================================================================================================================
//                                                     CONSTRAINTAUTHORING.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/ConstraintAuthoring/Api/ConstraintAuthoring.h"

#include "SlateShape/Sketch/SketchPolyline/Api/SketchPolyline.h"
#include "SlateShape/Sketch/SketchSelection/Api/SketchSelection.h"

#include <cmath>
#include <vector>

namespace Slate
{

namespace
{

// 🔴 ONE ROW PER RELATIONSHIP, STATING WHAT IT NEEDS. The host decided this in an `if`-chain that tested
//    the subject and then reached for whichever selection field the branch assumed — so what a constraint
//    demanded was a property of where it sat in the chain rather than of the relationship itself.
//
// 📝 `Fixed` takes one curve: it pins that curve where it stands and needs nothing to relate it to.
const ConstraintDeclaration ConstraintTable[] =
{
    { ConstraintSubject::Coincident,    ConstraintDemand::TwoPoints, "●", "Coincident"    },
    { ConstraintSubject::Horizontal,    ConstraintDemand::OneCurve,  "H", "Horizontal"    },
    { ConstraintSubject::Vertical,      ConstraintDemand::OneCurve,  "V", "Vertical"      },
    { ConstraintSubject::Parallel,      ConstraintDemand::TwoCurves, "∥", "Parallel"      },
    { ConstraintSubject::Perpendicular, ConstraintDemand::TwoCurves, "⊥", "Perpendicular" },
    { ConstraintSubject::Tangent,       ConstraintDemand::TwoCurves, "T", "Tangent"       },
    { ConstraintSubject::Equal,         ConstraintDemand::TwoCurves, "=", "Equal"         },
    { ConstraintSubject::Fixed,         ConstraintDemand::OneCurve,  "F", "Fixed"         },
};

const ConstraintDeclaration* ResolveRow(ConstraintSubject Subject)
{
    for (const ConstraintDeclaration& Row : ConstraintTable)
        if (Row.Subject == Subject)
            return &Row;

    return nullptr;
}

ReferenceSpecification ReferenceForCurve(SketchCurveName Curve)
{
    ReferenceSpecification Reference = {};
    Reference.Subject     = ReferenceSubject::SketchCurve;
    Reference.SketchCurve = Curve;
    return Reference;
}

ReferenceSpecification ReferenceForPoint(SketchPointName Point)
{
    ReferenceSpecification Reference = {};
    Reference.Subject     = ReferenceSubject::SketchPoint;
    Reference.SketchPoint = Point;
    return Reference;
}

}   // namespace

Deliver<ConstraintDeclaration> DeclaredConstraint(ConstraintSubject Subject)
{
    const ConstraintDeclaration* Row = ResolveRow(Subject);
    if (Row == nullptr)
        return Deliver<ConstraintDeclaration>::Refuse({ RefusalReason::ContentUnsupported,
                                                        "this unit does not author that relationship" });

    return Deliver<ConstraintDeclaration>::Result(*Row);
}

bool ConstraintSupported(ConstraintSubject Subject)
{
    return ResolveRow(Subject) != nullptr;
}

const char* ConstraintGlyph(ConstraintSubject Subject)
{
    const ConstraintDeclaration* Row = ResolveRow(Subject);

    // 📝 A glyph is decoration. Refusing one would leave a constraint the artist can see the effect of
    //    but not identify, which is worse than a question mark.
    return Row != nullptr ? Row->Glyph : "?";
}

const char* ConstraintDispositionNaming(ConstraintDisposition Disposition)
{
    // 🔴 EVERY VERDICT THE SOLVER CAN RETURN, NAMED. The host's version handled four of the six and fell
    //    through to "unknown" for the other two, so a sketch that was simply not solved yet and one
    //    carrying a duplicate constraint both read as an unexplained failure. The compiler found this the
    //    moment the switch was moved somewhere warnings are errors.
    switch (Disposition)
    {
        case ConstraintDisposition::Produced:              return "valid";
        case ConstraintDisposition::NotRequested:          return "not solved yet";
        case ConstraintDisposition::InvalidSketch:         return "invalid sketch references";
        case ConstraintDisposition::UnsupportedConstraint: return "unsupported constraint";
        case ConstraintDisposition::ConflictingConstraint: return "conflicting constraint";
        case ConstraintDisposition::RepeatedConstraint:    return "repeated constraint";
    }

    return "unknown";
}

bool ResolveConstraintAnchor(const SketchStructure& Sketch,
                             const ReferenceSpecification& Reference,
                             SpatialPoint& Anchor)
{
    if (Reference.Subject == ReferenceSubject::SketchCurve && Reference.SketchCurve.Assigned() &&
        Reference.SketchCurve.IssuedIndex <= Sketch.Curves().size())
    {
        std::vector<SpatialPoint> Polyline;
        AppendCurvePolyline(Sketch.Curves()[Reference.SketchCurve.IssuedIndex - 1u].Geometry, Polyline, 24u);

        if (Polyline.empty())
            return false;

        // 🔴 THE MIDPOINT BY LENGTH, NOT THE MIDDLE SAMPLE. A line samples to exactly TWO points however
        //    many steps are asked for, so `Polyline[size / 2]` is index 1 — its END. Every line's badge
        //    hung on its endpoint, which is precisely where lines meet, so badges on connected lines
        //    stacked on top of each other. Interpolating halfway along the polyline is right for a
        //    two-point line and for a fifty-point arc alike.
        double Total = 0.0;
        for (std::size_t Index = 1u; Index < Polyline.size(); ++Index)
            Total += std::sqrt(LengthSquared(Difference(Polyline[Index - 1u], Polyline[Index])));

        if (Total <= 1.0e-9)
        {
            Anchor = Polyline.front();
            return true;
        }

        double Walked = 0.0;
        for (std::size_t Index = 1u; Index < Polyline.size(); ++Index)
        {
            const double Step = std::sqrt(LengthSquared(Difference(Polyline[Index - 1u], Polyline[Index])));
            if (Walked + Step >= Total * 0.5)
            {
                const double Fraction = Step > 1.0e-12 ? (Total * 0.5 - Walked) / Step : 0.0;
                Anchor = Added(Polyline[Index - 1u],
                               Scaled(Difference(Polyline[Index - 1u], Polyline[Index]), Fraction));
                return true;
            }
            Walked += Step;
        }

        Anchor = Polyline.back();
        return true;
    }

    if (Reference.Subject == ReferenceSubject::SketchPoint && Reference.SketchPoint.Assigned())
    {
        // 🔴 A point's name packs the curve it belongs to in its high bits, so the curve to search is
        //    recovered by shifting rather than by looking through every curve in the sketch.
        const std::uint32_t CurveIndex = Reference.SketchPoint.IssuedIndex >> 8u;

        std::vector<SketchPointPlacement> Points;
        if (CurveIndex != 0u && ResolveSketchPoints(Sketch, { CurveIndex }, Points))
        {
            for (const SketchPointPlacement& Point : Points)
            {
                if (Point.Name.IssuedIndex != Reference.SketchPoint.IssuedIndex)
                    continue;

                Anchor = Point.Position;
                return true;
            }
        }
    }

    // ⚠️ Not the origin. A badge for geometry that has been removed must vanish with it rather than
    //    collect at the centre of the world.
    return false;
}

bool ConstraintDemandMet(ConstraintDemand Demand,
                         bool PrimaryCurve,
                         bool SecondaryCurve,
                         bool PrimaryPoint,
                         bool SecondaryPoint)
{
    switch (Demand)
    {
        case ConstraintDemand::OneCurve:    return PrimaryCurve;
        case ConstraintDemand::TwoCurves:   return PrimaryCurve && SecondaryCurve;
        case ConstraintDemand::TwoPoints:   return PrimaryPoint && SecondaryPoint;
        case ConstraintDemand::DemandCount: break;
    }

    return false;
}

Deliver<ConstraintSpecification> DeclareConstraintFrom(ConstraintSubject Subject,
                                                       SketchCurveName PrimaryCurve,
                                                       SketchCurveName SecondaryCurve,
                                                       SketchPointName PrimaryPoint,
                                                       SketchPointName SecondaryPoint)
{
    const ConstraintDeclaration* Row = ResolveRow(Subject);
    if (Row == nullptr)
        return Deliver<ConstraintSpecification>::Refuse({ RefusalReason::ContentUnsupported,
                                                          "this unit does not author that relationship" });

    if (!ConstraintDemandMet(Row->Demand, PrimaryCurve.Assigned(), SecondaryCurve.Assigned(),
                             PrimaryPoint.Assigned(), SecondaryPoint.Assigned()))
        return Deliver<ConstraintSpecification>::Refuse({ RefusalReason::ContentUnsupported,
                                                          "the selection does not satisfy the relationship" });

    ConstraintSpecification Constraint = {};
    Constraint.Subject = Subject;

    switch (Row->Demand)
    {
        case ConstraintDemand::OneCurve:
            Constraint.Primary = ReferenceForCurve(PrimaryCurve);
            break;

        case ConstraintDemand::TwoCurves:
            Constraint.Primary   = ReferenceForCurve(PrimaryCurve);
            Constraint.Secondary = ReferenceForCurve(SecondaryCurve);
            break;

        case ConstraintDemand::TwoPoints:
            Constraint.Primary   = ReferenceForPoint(PrimaryPoint);
            Constraint.Secondary = ReferenceForPoint(SecondaryPoint);
            break;

        case ConstraintDemand::DemandCount:
            break;
    }

    // ⚠️ A specification whose references were never filled is still `Declared()` for some subjects, so
    //    this is checked rather than assumed.
    if (!Constraint.Declared())
        return Deliver<ConstraintSpecification>::Refuse({ RefusalReason::ContentUnsupported,
                                                          "the relationship did not describe anything" });

    return Deliver<ConstraintSpecification>::Result(Constraint);
}

}   // namespace Slate

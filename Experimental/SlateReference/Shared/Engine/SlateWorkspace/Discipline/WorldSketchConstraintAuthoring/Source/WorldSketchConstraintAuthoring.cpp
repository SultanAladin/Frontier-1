//============================================================================================================================================
//                                        WORLDSKETCHCONSTRAINTAUTHORING.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/WorldSketchConstraintAuthoring/Api/WorldSketchConstraintAuthoring.h"

namespace Slate
{

namespace
{

const WorldConstraintDeclaration WorldConstraintTable[] =
{
    { WorldConstraintSubject::Coincident,    WorldConstraintDemand::TwoPoints,  "●", "Coincident"    },
    { WorldConstraintSubject::Horizontal,    WorldConstraintDemand::OneCurve,   "H", "Horizontal"    },
    { WorldConstraintSubject::Vertical,      WorldConstraintDemand::OneCurve,   "V", "Vertical"      },
    { WorldConstraintSubject::Parallel,      WorldConstraintDemand::TwoCurves,  "∥", "Parallel"      },
    { WorldConstraintSubject::Perpendicular, WorldConstraintDemand::TwoCurves,  "⊥", "Perpendicular" },
    { WorldConstraintSubject::Tangent,       WorldConstraintDemand::TwoCurves,  "T", "Tangent"       },
    { WorldConstraintSubject::Equal,         WorldConstraintDemand::TwoCurves,  "=", "Equal"         },
    { WorldConstraintSubject::Fixed,         WorldConstraintDemand::OneCurve,   "F", "Fixed"         },
};

const WorldConstraintDeclaration* ResolveRow(WorldConstraintSubject Subject)
{
    for (const WorldConstraintDeclaration& Row : WorldConstraintTable)
        if (Row.Subject == Subject)
            return &Row;
    return nullptr;
}

bool CurvePick(const WorldPick& Pick)
{
    return Pick.Subject == WorldPickSubject::Curve && Pick.Curve.Assigned();
}

bool PointPick(const WorldPick& Pick)
{
    return Pick.Subject == WorldPickSubject::Point && Pick.Point.Assigned();
}

} // namespace

Deliver<WorldConstraintDeclaration> DeclaredWorldConstraint(WorldConstraintSubject Subject)
{
    const WorldConstraintDeclaration* Row = ResolveRow(Subject);
    if (Row == nullptr)
        return Deliver<WorldConstraintDeclaration>::Refuse({ RefusalReason::ContentUnsupported,
                                                             "this unit does not author that world relationship" });
    return Deliver<WorldConstraintDeclaration>::Result(*Row);
}

bool WorldConstraintSupported(WorldConstraintSubject Subject)
{
    return ResolveRow(Subject) != nullptr;
}

Deliver<WorldConstraintSpecification> DeclareWorldConstraintFrom(WorldConstraintSubject Subject,
                                                                 const WorldPick& Primary,
                                                                 const WorldPick& Secondary)
{
    const Deliver<WorldConstraintDeclaration> Declaration = DeclaredWorldConstraint(Subject);
    if (!Declaration)
        return Deliver<WorldConstraintSpecification>::Refuse(Declaration.Error);

    const WorldConstraintDeclaration& Row = Declaration.Resolve();
    const bool HasPrimaryCurve = CurvePick(Primary);
    const bool HasSecondaryCurve = CurvePick(Secondary);
    const bool HasPrimaryPoint = PointPick(Primary);
    const bool HasSecondaryPoint = PointPick(Secondary);

    const bool DemandMet =
        (Row.Demand == WorldConstraintDemand::OneCurve && HasPrimaryCurve)
     || (Row.Demand == WorldConstraintDemand::TwoCurves && HasPrimaryCurve && HasSecondaryCurve)
     || (Row.Demand == WorldConstraintDemand::TwoPoints && HasPrimaryPoint && HasSecondaryPoint);
    if (!DemandMet)
        return Deliver<WorldConstraintSpecification>::Refuse({ RefusalReason::ContentUnsupported,
                                                               "the semantic world selections do not satisfy the relationship" });

    WorldConstraintSpecification Result = {};
    Result.Subject = Subject;
    if (Row.Demand == WorldConstraintDemand::TwoPoints)
    {
        Result.Primary.Subject = WorldConstraintReferenceSubject::Point;
        Result.Primary.Point = Primary.Point.IssuedIndex;
        Result.Secondary.Subject = WorldConstraintReferenceSubject::Point;
        Result.Secondary.Point = Secondary.Point.IssuedIndex;
    }
    else
    {
        Result.Primary.Subject = WorldConstraintReferenceSubject::Curve;
        Result.Primary.Curve = Primary.Curve;
        if (Row.Demand == WorldConstraintDemand::TwoCurves)
        {
            Result.Secondary.Subject = WorldConstraintReferenceSubject::Curve;
            Result.Secondary.Curve = Secondary.Curve;
        }
    }

    if (!Result.Declared())
        return Deliver<WorldConstraintSpecification>::Refuse({ RefusalReason::ContentUnsupported,
                                                               "the world relationship did not describe anything" });
    return Deliver<WorldConstraintSpecification>::Result(Result);
}

} // namespace Slate

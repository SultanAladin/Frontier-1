//============================================================================================================================================
//                                       WORLDSKETCHDIMENSIONAUTHORING.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/WorldSketchDimensionAuthoring/Api/WorldSketchDimensionAuthoring.h"

namespace Slate
{

namespace
{

const WorldDimensionDeclaration WorldDimensionTable[] =
{
    { WorldDimensionSubject::Horizontal, WorldDimensionDemand::OneReference,  "Horizontal" },
    { WorldDimensionSubject::Vertical,   WorldDimensionDemand::OneReference,  "Vertical"   },
    // 🔴 ONE REFERENCE, NOT TWO, AND THE SPECIFICATION ALWAYS SAID SO. `WorldDimensionSpecification`
    //    accepts an aligned dimension naming a single CURVE -- a curve already names both of its ends,
    //    and demanding a second reference for it asks the artist to say the same thing twice. This table
    //    demanded two regardless, so authoring a length off one edge was refused here and then would have
    //    been accepted by the very structure it was being authored into. The two disagreed; the structure
    //    is authoritative, because it is what the solver and the renderer both read.
    // 📝 A dimension between two POINTS still needs both, and still gets both: `Declared()` enforces that
    //    directly, so nothing is loosened by counting one here.
    { WorldDimensionSubject::Aligned,    WorldDimensionDemand::OneReference,  "Aligned"    },
    { WorldDimensionSubject::Radius,     WorldDimensionDemand::OneReference,  "Radius"     },
    { WorldDimensionSubject::Diameter,   WorldDimensionDemand::OneReference,  "Diameter"   },
    { WorldDimensionSubject::Angle,      WorldDimensionDemand::TwoReferences, "Angle"      },
};

const WorldDimensionDeclaration* ResolveRow(WorldDimensionSubject Subject)
{
    for (const WorldDimensionDeclaration& Row : WorldDimensionTable)
        if (Row.Subject == Subject)
            return &Row;
    return nullptr;
}

} // namespace

Deliver<WorldDimensionDeclaration> DeclaredWorldDimension(WorldDimensionSubject Subject)
{
    const WorldDimensionDeclaration* Row = ResolveRow(Subject);
    if (Row == nullptr)
        return Deliver<WorldDimensionDeclaration>::Refuse({ RefusalReason::ContentUnsupported,
                                                            "this unit does not author that world dimension" });
    return Deliver<WorldDimensionDeclaration>::Result(*Row);
}

bool WorldDimensionSupported(WorldDimensionSubject Subject)
{
    return ResolveRow(Subject) != nullptr;
}

Deliver<WorldDimensionSpecification> DeclareWorldDimensionFrom(WorldDimensionSubject Subject,
                                                               const WorldDimensionReference& Primary,
                                                               const WorldDimensionReference& Secondary,
                                                               double Target)
{
    const Deliver<WorldDimensionDeclaration> Declaration = DeclaredWorldDimension(Subject);
    if (!Declaration)
        return Deliver<WorldDimensionSpecification>::Refuse(Declaration.Error);
    if (!Primary.Declared() || Target <= 0.0)
        return Deliver<WorldDimensionSpecification>::Refuse({ RefusalReason::ContentUnsupported,
                                                              "the world dimension has no positive target or primary reference" });
    if (Declaration.Resolve().Demand == WorldDimensionDemand::TwoReferences && !Secondary.Declared())
        return Deliver<WorldDimensionSpecification>::Refuse({ RefusalReason::ContentUnsupported,
                                                              "the world dimension needs two world references" });

    WorldDimensionSpecification Result = {};
    Result.Subject = Subject;
    Result.Primary = Primary;
    Result.Secondary = Secondary;
    Result.Target = Target;
    if (!Result.Declared())
        return Deliver<WorldDimensionSpecification>::Refuse({ RefusalReason::ContentUnsupported,
                                                              "the world dimension references are incompatible" });
    return Deliver<WorldDimensionSpecification>::Result(Result);
}

} // namespace Slate

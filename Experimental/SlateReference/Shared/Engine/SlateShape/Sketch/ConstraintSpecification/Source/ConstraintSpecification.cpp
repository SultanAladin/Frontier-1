//============================================================================================================================================
//                                                 CONSTRAINTSPECIFICATION.CPP
//============================================================================================================================================

#include "SlateShape/Sketch/ConstraintSpecification/Api/ConstraintSpecification.h"

namespace Slate
{

bool ConstraintSpecification::Declared() const
{
    switch (Subject)
    {
        case ConstraintSubject::Coincident:
        case ConstraintSubject::Parallel:
        case ConstraintSubject::Perpendicular:
        case ConstraintSubject::Tangent:
        case ConstraintSubject::Equal:
            return Primary.Declared() && Secondary.Declared();

        case ConstraintSubject::Horizontal:
        case ConstraintSubject::Vertical:
        case ConstraintSubject::Fixed:
            return Primary.Declared();

        case ConstraintSubject::SubjectCount:
            return false;
    }

    return false;
}

} // namespace Slate

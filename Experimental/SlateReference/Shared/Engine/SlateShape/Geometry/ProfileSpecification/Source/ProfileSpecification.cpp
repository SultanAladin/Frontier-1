//============================================================================================================================================
//                                                    PROFILESPECIFICATION.CPP
//============================================================================================================================================

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Geometry/ProfileSpecification/Api/ProfileSpecification.h"

namespace Slate
{

namespace
{
}

bool ProfileSpecification::Declared() const
{
    if (LengthSquared(Plane.Normal) == 0.0 || LengthSquared(Plane.AlongDirection) == 0.0)
        return false;
    if (Loops.empty())
        return false;

    bool OuterDeclared = false;
    for (const ProfileLoop& Loop : Loops)
    {
        if (Loop.Traversal.size() < 1u)
            return false;
        if (Loop.Orientation == ProfileLoopOrientation::Outer)
        {
            if (OuterDeclared)
                return false;
            OuterDeclared = true;
        }
        for (const ProfileCurveUse& Use : Loop.Traversal)
            if (!Use.TraversedCurve.Assigned())
                return false;
    }

    return OuterDeclared;
}

} // namespace Slate

//============================================================================================================================================
//                                                  DIMENSIONSPECIFICATION.CPP
//============================================================================================================================================

#include "SlateShape/Sketch/DimensionSpecification/Api/DimensionSpecification.h"

namespace Slate
{

bool DimensionSpecification::Declared() const
{
    switch (Subject)
    {
        case DimensionSubject::Horizontal:
        case DimensionSubject::Vertical:
        case DimensionSubject::Aligned:
            return Primary.Declared() && Target > 0.0
                && ((Primary.Subject == ReferenceSubject::SketchPoint && Secondary.Declared())
                 || Primary.Subject == ReferenceSubject::SketchCurve);

        case DimensionSubject::Radius:
        case DimensionSubject::Diameter:
            return Primary.Declared() && Target > 0.0;

        case DimensionSubject::Angle:
            return Primary.Declared() && Secondary.Declared() && Target > 0.0;

        case DimensionSubject::SubjectCount:
            return false;
    }

    return false;
}

} // namespace Slate

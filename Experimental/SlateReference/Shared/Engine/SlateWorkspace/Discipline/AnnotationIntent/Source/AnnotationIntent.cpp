//============================================================================================================================================
//                                                       ANNOTATIONINTENT.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/AnnotationIntent/Api/AnnotationIntent.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------

AnnotationIntent ResolveAnnotationIntent(ParametricToolSubject Subject)
{
    AnnotationIntent Intent = {};

    switch (Subject)
    {
        //--------------------------------------------------------------------------------------------------------------------
        // The three dimensions.
        //--------------------------------------------------------------------------------------------------------------------
        case ParametricToolSubject::LinearDimension:
            Intent.Standing = true;
            Intent.Dimension = WorldDimensionSubject::Aligned;
            Intent.Supported = true;
            break;

        case ParametricToolSubject::AngularDimension:
            Intent.Standing = true;
            Intent.Dimension = WorldDimensionSubject::Angle;
            Intent.Supported = true;
            break;

        case ParametricToolSubject::RadialDimension:
            Intent.Standing = true;
            Intent.Dimension = WorldDimensionSubject::Radius;
            Intent.Supported = true;
            break;

        //--------------------------------------------------------------------------------------------------------------------
        // The eight constraints the solver actually supports.
        //--------------------------------------------------------------------------------------------------------------------
        case ParametricToolSubject::HorizontalConstraint:
            Intent.Standing = Intent.Constraining = Intent.Supported = true;
            Intent.Constraint = WorldConstraintSubject::Horizontal;
            break;

        case ParametricToolSubject::VerticalConstraint:
            Intent.Standing = Intent.Constraining = Intent.Supported = true;
            Intent.Constraint = WorldConstraintSubject::Vertical;
            break;

        case ParametricToolSubject::CoincidentConstraint:
            Intent.Standing = Intent.Constraining = Intent.Supported = true;
            Intent.Constraint = WorldConstraintSubject::Coincident;
            break;

        case ParametricToolSubject::ParallelConstraint:
            Intent.Standing = Intent.Constraining = Intent.Supported = true;
            Intent.Constraint = WorldConstraintSubject::Parallel;
            break;

        case ParametricToolSubject::PerpendicularConstraint:
            Intent.Standing = Intent.Constraining = Intent.Supported = true;
            Intent.Constraint = WorldConstraintSubject::Perpendicular;
            break;

        case ParametricToolSubject::TangentConstraint:
            Intent.Standing = Intent.Constraining = Intent.Supported = true;
            Intent.Constraint = WorldConstraintSubject::Tangent;
            break;

        case ParametricToolSubject::EqualConstraint:
            Intent.Standing = Intent.Constraining = Intent.Supported = true;
            Intent.Constraint = WorldConstraintSubject::Equal;
            break;

        //--------------------------------------------------------------------------------------------------------------------
        // 🔴 THE THREE THAT ARE NOT BUILT SAY SO. Midpoint, Symmetry and Concentric are in the catalogue
        //    and are NOT among the eight relations the solver knows. They report `Standing` -- the arm
        //    owns the tile, so the pointer is not handed to some other tool -- and `Supported = false`,
        //    so nothing is applied. Silently mapping them onto the nearest working constraint would apply
        //    a relation the artist did not ask for, which is far worse than a tile that declines.
        //--------------------------------------------------------------------------------------------------------------------
        case ParametricToolSubject::MidpointConstraint:
        case ParametricToolSubject::SymmetryConstraint:
        case ParametricToolSubject::ConcentricConstraint:
            Intent.Standing = true;
            Intent.Constraining = true;
            Intent.Supported = false;
            break;

        default:
            break;
    }

    return Intent;
}

} // namespace Slate

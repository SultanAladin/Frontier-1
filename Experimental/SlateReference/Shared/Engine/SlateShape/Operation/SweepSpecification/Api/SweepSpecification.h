//============================================================================================================================================
//                                                      SWEEPSPECIFICATION.H
//============================================================================================================================================

#pragma once

#include "SlateShape/Geometry/ProfileSpecification/Api/ProfileSpecification.h"
#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"

namespace Slate
{

struct SweepSpecification
{
    ProfileName SourceProfile = {};
    CurveName SpineCurve = {};
    bool RigidNormal = true;

    bool Declared() const { return SourceProfile.Assigned() && SpineCurve.Assigned(); }
};

} // namespace Slate

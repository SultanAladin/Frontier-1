//============================================================================================================================================
//                                                     ORIENTATIONSTANDING.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/OrientationCube/Api/OrientationStanding.h"

#include <cmath>

namespace Slate
{

void OrientationYawPitch(ViewportOrientation Orientation, double& YawDegrees, double& PitchDegrees)
{
    switch (Orientation)
    {
        case ViewportOrientation::Top:       YawDegrees = 180.0; PitchDegrees = -89.9; return;
        case ViewportOrientation::Bottom:    YawDegrees = 180.0; PitchDegrees =  89.9; return;
        case ViewportOrientation::Front:     YawDegrees = 180.0; PitchDegrees =   0.0; return;
        case ViewportOrientation::Back:      YawDegrees =   0.0; PitchDegrees =   0.0; return;
        case ViewportOrientation::Right:     YawDegrees = -90.0; PitchDegrees =   0.0; return;
        case ViewportOrientation::Left:      YawDegrees =  90.0; PitchDegrees =   0.0; return;
        case ViewportOrientation::Isometric: YawDegrees =  52.0; PitchDegrees =  24.0; return;
    }
}

ViewportOrientation ResolveCameraOrientation(double YawDegrees, double PitchDegrees,
                                             double ToleranceDegrees)
{
    // 🔴 Answered by asking the forward table, so the two cannot drift apart. Writing a second table
    //    of angles here is how an inverse stops being an inverse.
    const ViewportOrientation Axial[6] =
    {
        ViewportOrientation::Top,   ViewportOrientation::Bottom,
        ViewportOrientation::Front, ViewportOrientation::Back,
        ViewportOrientation::Left,  ViewportOrientation::Right,
    };

    for (const ViewportOrientation Candidate : Axial)
    {
        double Yaw   = 0.0;
        double Pitch = 0.0;
        OrientationYawPitch(Candidate, Yaw, Pitch);

        if (std::fabs(PitchDegrees - Pitch) > ToleranceDegrees)
            continue;

        // 📝 Looking straight up or down, yaw does not change what is on screen, so it is not tested.
        if (Candidate == ViewportOrientation::Top || Candidate == ViewportOrientation::Bottom)
            return Candidate;

        // ⚠️ Yaw wraps, so -180 and 180 are the same direction and a plain subtraction would call
        //    them 360 apart.
        const double Difference = std::fmod(YawDegrees - Yaw + 540.0, 360.0) - 180.0;
        if (std::fabs(Difference) <= ToleranceDegrees)
            return Candidate;
    }

    return ViewportOrientation::Isometric;
}

}   // namespace Slate

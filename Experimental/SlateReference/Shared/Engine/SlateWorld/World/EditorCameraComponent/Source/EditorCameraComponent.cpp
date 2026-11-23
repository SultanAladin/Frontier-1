//============================================================================================================================================
//                                                    EDITORCAMERACOMPONENT.CPP
//============================================================================================================================================

#include "SlateWorld/World/EditorCameraComponent/Api/EditorCameraComponent.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

void EditorCameraComponent::AdjustFlySpeed(double Steps)
{
    if (Steps == 0.0)
        return;

    FlySpeed *= std::pow(1.25, Steps);
    FlySpeed = std::clamp(FlySpeed, 1.0, 5000.0);
}

} // namespace Slate

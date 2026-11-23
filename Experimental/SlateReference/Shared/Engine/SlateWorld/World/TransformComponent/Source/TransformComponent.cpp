//============================================================================================================================================
//                                                      TRANSFORMCOMPONENT.CPP
//============================================================================================================================================

#include "SlateWorld/World/TransformComponent/Api/TransformComponent.h"

#include <cmath>

namespace Slate
{

bool TransformComponent::Differs(const double Left[3], const double Right[3])
{
    constexpr double PoseTolerance = 1.0e-8;
    for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        if (std::abs(Left[Axis] - Right[Axis]) > PoseTolerance)
            return true;
    return false;
}

void TransformComponent::Reset(const double Position[3], const double Rotation[3])
{
    for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
    {
        PositionSlot[Axis] = Position[Axis];
        RotationSlot[Axis] = Rotation[Axis];
        LastPublishedPosition[Axis] = Position[Axis];
        LastPublishedRotation[Axis] = Rotation[Axis];
    }
    PublicationStanding = true;
}

bool TransformComponent::ConsumeAuthored(const double Position[3], const double Rotation[3])
{
    if (!PublicationStanding)
    {
        Reset(Position, Rotation);
        return true;
    }

    if (!Differs(Position, LastPublishedPosition) && !Differs(Rotation, LastPublishedRotation))
        return false;

    for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
    {
        PositionSlot[Axis] = Position[Axis];
        RotationSlot[Axis] = Rotation[Axis];
    }
    return true;
}

void TransformComponent::Publish(const double Position[3], const double Rotation[3],
                                 double PublishedPosition[3], double PublishedRotation[3])
{
    for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
    {
        PositionSlot[Axis] = Position[Axis];
        RotationSlot[Axis] = Rotation[Axis];
        PublishedPosition[Axis] = Position[Axis];
        PublishedRotation[Axis] = Rotation[Axis];
        LastPublishedPosition[Axis] = Position[Axis];
        LastPublishedRotation[Axis] = Rotation[Axis];
    }
    PublicationStanding = true;
}

} // namespace Slate

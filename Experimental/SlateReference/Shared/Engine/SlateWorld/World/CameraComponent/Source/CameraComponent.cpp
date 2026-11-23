//============================================================================================================================================
//                                                         CAMERACOMPONENT.CPP
//============================================================================================================================================

#include "SlateWorld/World/CameraComponent/Api/CameraComponent.h"

#include <algorithm>
#include <cstdint>
#include <cmath>

namespace Slate
{

namespace
{

constexpr double HalfTurn = 3.14159265358979323846;

}   // namespace

void CameraComponent::Ease(double& Lagged, double Target, double Seconds, const CameraSettings& Settings)
{
    if (!Settings.LagEnabled || Settings.LagSeconds <= 0.0 || Seconds <= 0.0)
    {
        Lagged = Target;
        return;
    }

    // 📐 The exponential approach: each tick closes a fraction of the remaining gap determined by the
    //    time constant. `1 - exp(-dt/τ)` is the exact integral of the first-order lag, so the rate is
    //    frame-rate independent.
    const double Fraction = 1.0 - std::exp(-Seconds / Settings.LagSeconds);
    Lagged += (Target - Lagged) * Fraction;
}

void CameraComponent::Advance(double Seconds, const CameraCondition& Input, const CameraSettings& Settings)
{
    if (Seconds <= 0.0)
        return;

    // ① The look gesture: the pointer's travel turns the target. Positive X is rightward, so a drag to
    //    the right yaws clockwise; positive Y is downward, so a drag down pitches the view down and
    //    `InvertPitch` flips that.
    if (Input.LookHeld)
    {
        YawDegrees += static_cast<double>(Input.LookDeltaX) * Settings.LookSensitivity;
        // 📐 Positive Y is downward on the display, so a drag down pitches the view down (pitch
        //    decreases) by default, and `InvertPitch` flips that to the flight-sim convention.
        PitchDegrees += static_cast<double>(Input.LookDeltaY) * Settings.LookSensitivity
                      * (Settings.InvertPitch ? 1.0 : -1.0);
        PitchDegrees = std::clamp(PitchDegrees, -89.0, 89.0);
    }

    // ② The movement keys drive the target position along the camera's own frame: forward is the view
    //    direction (pitch included), right is the yaw's cross, up is world up.
    const double Yaw   = YawDegrees * HalfTurn / 180.0;
    const double Pitch = PitchDegrees * HalfTurn / 180.0;
    const double CosPitch = std::cos(Pitch);

    const double Forward[3] =
    {
        CosPitch * std::sin(Yaw),
        std::sin(Pitch),
        CosPitch * std::cos(Yaw)
    };
    // 🔴 RIGHT IS NOT SCALED BY THE PITCH. It carried a `CosPitch` factor on both terms, which is not a
    //    unit vector: strafing ran at cos(pitch) of the forward speed, so A/D weakened as the artist
    //    looked down and all but stopped near vertical -- 17% of forward speed at 80°, 0.2% at 89.9°,
    //    which is the pitch the Top view sits at. The keys were not inverted there; they were dead,
    //    while W/S kept full speed because `Forward` IS a unit vector. Right is the yaw's perpendicular
    //    in the ground plane and does not depend on pitch at all.
    const double Right[3] =
    {
        std::cos(Yaw),
        0.0,
        -std::sin(Yaw)
    };

    double Velocity[3] = { 0.0, 0.0, 0.0 };

    if (Input.ForwardHeld)
    {
        Velocity[0] += Forward[0];
        Velocity[1] += Forward[1];
        Velocity[2] += Forward[2];
    }
    if (Input.BackwardHeld)
    {
        Velocity[0] -= Forward[0];
        Velocity[1] -= Forward[1];
        Velocity[2] -= Forward[2];
    }
    if (Input.RightHeld)
    {
        Velocity[0] += Right[0];
        Velocity[1] += Right[1];
        Velocity[2] += Right[2];
    }
    if (Input.LeftHeld)
    {
        Velocity[0] -= Right[0];
        Velocity[1] -= Right[1];
        Velocity[2] -= Right[2];
    }
    if (Input.UpHeld)
        Velocity[1] += 1.0;
    if (Input.DownHeld)
        Velocity[1] -= 1.0;

    const double Speed = std::sqrt(Velocity[0] * Velocity[0]
                                 + Velocity[1] * Velocity[1]
                                 + Velocity[2] * Velocity[2]);

    if (Speed > 0.0)
    {
        // 📐 Unreal's boost: Shift multiplies the fly speed (3x), so an artist can sprint across the
        //    world or creep up to a detail without opening the settings.
        const double Boost = Input.ShiftHeld ? 3.0 : 1.0;
        const double Scale = Settings.FlySpeed * Boost * Seconds / Speed;

        Position[0] += Velocity[0] * Scale;
        Position[1] += Velocity[1] * Scale;
        Position[2] += Velocity[2] * Scale;
    }

    // ③ Rotation is immediate. Easing yaw or pitch after the pointer stops makes a stationary right-click
    //    continue to turn the viewport as the old angular lag catches up — perceived as an unexplained
    //    upward pitch. Camera lag is positional only; holding Look with zero travel is therefore invariant.
    LaggedYawDegrees = YawDegrees;
    LaggedPitchDegrees = PitchDegrees;

    for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        Ease(LaggedPosition[Axis], Position[Axis], Seconds, Settings);
}

void CameraComponent::Snap()
{
    LaggedYawDegrees   = YawDegrees;
    LaggedPitchDegrees = PitchDegrees;
    LaggedPosition[0]  = Position[0];
    LaggedPosition[1]  = Position[1];
    LaggedPosition[2]  = Position[2];
}

bool CameraComponent::ConsumeTransform(double AuthoredPosition[3], double AuthoredRotation[3])
{
    if (!TransformSynchronizer.ConsumeAuthored(AuthoredPosition, AuthoredRotation))
        return false;

    const double* IncomingPosition = TransformSynchronizer.AuthoredPosition();
    const double* IncomingRotation = TransformSynchronizer.AuthoredRotation();
    for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
        Position[Axis] = IncomingPosition[Axis];
    YawDegrees = IncomingRotation[0];
    PitchDegrees = std::clamp(IncomingRotation[1], -89.0, 89.0);
    Snap();
    return true;
}

void CameraComponent::PublishTransform(double PublishedPosition[3], double PublishedRotation[3])
{
    const double PresentedRotation[3] = { LaggedYawDegrees, LaggedPitchDegrees, 0.0 };
    TransformSynchronizer.Publish(LaggedPosition, PresentedRotation,
                                  PublishedPosition, PublishedRotation);
}

} // namespace Slate

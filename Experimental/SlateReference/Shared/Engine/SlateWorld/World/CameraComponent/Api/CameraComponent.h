//============================================================================================================================================
//                                                          CAMERACOMPONENT.H
//============================================================================================================================================
// 🧩 The common camera component: pose, movement integration and positional lag shared by editor,
//    player and spectator camera specialisations. EditorCameraComponent supplies the editor's WASD +
//    QE movement and Unreal-style right-button look today; future camera types can build on the same
//    pose component without inheriting editor-only UI.
//
//    The component is a plain state machine, owned by the host and advanced once per
//    tick with the seam's `CameraCondition` and the artist's `CameraSettings`.
//    It knows no device and no vendor: the host maps keys to the condition, and
//    the harness proofs synthesise the same condition to render the same motion.
//
//    Convention: the second axis is up (the atmosphere's own), yaw is clockwise
//    from north, pitch is above the horizon — the same frame the sky dome's
//    azimuth/elevation use, so the component's yaw and pitch ARE the viewport crop.

#pragma once

#include "Foundation/CameraCondition.h"
#include "SlateWorld/World/TransformComponent/Api/TransformComponent.h"

namespace Slate
{

/// 🧩 The artist's camera settings, edited through the scene directory's camera
///    details and properties: the fly speed, the lag's presence and its time
///    constant, and the look gesture's pitch direction.
/// tag   guarantee, nonallocating, nonthrowing
struct CameraSettings
{
    double FlySpeed    = 50.0;    // [m/s] - the movement integrator's rate
    double LagSeconds  = 0.18;    // [s]   - the lag's time constant
    bool   LagEnabled  = true;    // [-]   - the camera eases toward its target when set
    bool   InvertPitch = false;   // [-]   - the look gesture turns pitch the other way
    double LookSensitivity = 0.12; // [deg/px] - the look gesture's turn rate
};

/// 🧩 The reusable base camera pose: the target a controller drives and the presented position that
///    follows it. Rotation is immediate; camera lag is positional only, so taking the right mouse
///    button without moving it can never tilt the view.
/// tag   owning, nonallocating, nonthrowing
class CameraComponent
{
public:

    CameraComponent()  = default;
    ~CameraComponent() = default;

    CameraComponent(const CameraComponent&)            = delete;
    CameraComponent& operator=(const CameraComponent&) = delete;

    /// 🧩 Advances the component by one tick: the look gesture turns the target yaw and pitch, the held
    ///    movement keys drive the target position along the camera's own frame, and the lagged
    ///    presentation eases toward the target.
    /// in    Seconds   [s]   how long this tick lasted
    /// in    Input     [-]   the seam's camera condition
    /// in    Settings  [-]   the artist's camera settings
    /// note  📐 Movement is Unreal-fly: W/S along the view direction (pitch included), A/D strafing
    ///        across it and E/Q along world up. Yaw and pitch are presented immediately; only position
    ///        is allowed to lag.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Advance(double Seconds, const CameraCondition& Input, const CameraSettings& Settings);

    /// 🧩 Sets the lagged presentation onto the target, cancelling any residual lag.
    /// note  🔴 Called at bring-up and on a device rebuild, so the re-created viewport does not ease in
    ///        from the previous session's pose.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Snap();

    /// Consumes a transform edited in an inspector. A value last published by this camera is ignored,
    /// preventing the UI mirror from fighting navigation; a genuine authored edit becomes the target pose.
    bool ConsumeTransform(double AuthoredPosition[3], double AuthoredRotation[3]);

    /// Publishes the camera's current presented pose back to its inspector-only transform fields.
    void PublishTransform(double PublishedPosition[3], double PublishedRotation[3]);

    double YawDegrees   = 100.0;   // [deg] - the target yaw; the viewport crop reads the lagged one
    double PitchDegrees = -15.0;   // [deg] - the target pitch, clamped to ±89°; looks slightly down
    double Position[3]  = { 0.0, 1.5, 0.0 };   // [m] - the target position; Y is up

    // 📝 Lens and clipping belong to the reusable camera rather than to a panel. Editor, player and
    //    spectator specialisations can expose different controls while sharing the same projection data.
    double FieldOfViewDegrees = 60.0;    // [deg] - vertical perspective field
    double NearClipMetres     = 0.1;     // [m] - nearest presented geometry
    double FarClipMetres      = 10000.0; // [m] - furthest presented geometry

    double LaggedYawDegrees   = 100.0;   // [deg] - what the viewport actually shows
    double LaggedPitchDegrees = -15.0;   // [deg]
    double LaggedPosition[3]  = { 0.0, 1.5, 0.0 };   // [m]

private:

    void Ease(double& Lagged, double Target, double Seconds, const CameraSettings& Settings);
    TransformComponent TransformSynchronizer;
};

} // namespace Slate

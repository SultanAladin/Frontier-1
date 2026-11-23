//============================================================================================================================================
//                                                               RIGIDWHEEL.H
//============================================================================================================================================
// 📦 The wheel as the solver sees it: a rigid tyre on a kinematic hub, its contact answered by TraceContactTyre.h.
//
// 📐 The tyre carries no state of its own beyond the hub pose and its accumulated spin angle. Load, patch and normal
//    come from the ray-trace contact every step and are kept here as the step's TyreReaction so telemetry, the
//    thermal gas path and the renderer read one record. Visual deformation is a separate, GPU-side concern
//    (Plans/Ongoing/TyreContactAndGPUDeformation.md §5) driven by that same record — the mesh never feeds back.

#pragma once

#include "VehicleMath.h"
#include <cmath>
#include <cstdint>
#include <functional>

namespace Frontier::Vehicle {

inline constexpr float TyreBeadWidthRatio = 0.74f; // [-] rim seat width relative to the tread shoulders

//------------------------------------------------------------------------------------------------------------------------
//                                                   TYRE SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------
struct TyreSpecification
{
    float Radius              = 0.34f;      // [m]  unloaded tread radius
    float RimRadius           = 0.20f;      // [m]  bead/rim radius
    float BeadWidthRatio      = TyreBeadWidthRatio; // [-] rigid bead seat, shared with the authored wheel geometry
    float Width               = 0.245f;     // [m]  section width
    float TotalMass           = 12.0f;      // [kg] tyre mass (rotating with the rim; enters the spin inertia)
    float InflationPressure   = 110000.0f;  // [Pa] cold gauge pressure
    float AtmosphericPressure = 101325.0f;  // [Pa] outside pressure
    Vec3  Gravity             = {0.0f, 0.0f, -9.81f};

    // The air inside: the shell between rim and tread over the section width. The gas-thermal path takes this as
    //    its reference volume; the rigid tyre does not change it.
    [[nodiscard]] double RestVolume() const noexcept
    {
        const double R = Radius, r = RimRadius, W = Width;
        return 3.14159265358979323846 * std::max(0.0, R * R - r * r) * W;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   HUB MOTION / REACTION
//------------------------------------------------------------------------------------------------------------------------
struct HubMotion
{
    Vec3  Position{};            // [m]     hub centre at the END of this step
    Quat  Orientation{};         // [-]     hub frame at the END of this step — steer and camber, never spin
    Vec3  LinearVelocity{};      // [m/s]   hub translation rate (chassis + strut travel)
    float SpinRate = 0.0f;       // [rad/s] wheel rotation about hub-local +Y (+ = rolling toward +X)
    Vec3  AngularVelocity{};     // [rad/s] bearing orientation rate, excluding axle spin
};

struct TyreReaction
{
    Vec3     RimForce{};         // [N]   tyre force on the hub
    Vec3     RimTorque{};        // [N·m] tyre moment on the hub
    Vec3     Force{};            // [N]   {Fx, Fy, Fz} summary
    Vec3     NormalReaction{};   // [N]   ground non-penetration before tread friction
    Vec3     SupportReaction{};  // [N]   upward-facing contact normal load
    float    Mz = 0.0f;          // [N·m] self-aligning torque about the contact normal
    uint32_t ContactCount = 0u;  // [-]   samples that found ground
    Vec3     PatchCentre{};      // [m]   world centre of the contact patch
    float    EnclosedVolume = 0.0f; // [m³] gas volume (rest volume for a rigid tyre)
    float    GaugePressure  = 0.0f; // [Pa] gauge pressure
    Vec3     InflationForce{};   // [N]   retained for telemetry layouts; zero for a rigid tyre
    // Ray-trace contact figures the renderer's deformation uses (TyreDeformation): see TraceContactTyre.h.
    Vec3     ContactNormal{0.0f, 0.0f, 1.0f}; // [-] averaged contact normal
    float    Deflection = 0.0f;  // [m]   radial compression along that normal
    float    PatchLateral = 0.0f;// [-]   where across the tread the hits sit, −1 (−Y shoulder) … +1 (+Y shoulder)
};

// Heightfield / scene sample: the surface point and normal for a world position. Returns false off the course.
using GroundQuery = std::function<bool(const Vec3& Point, Vec3& OutSurfacePoint, Vec3& OutNormal)>;

//------------------------------------------------------------------------------------------------------------------------
//                                                        RIGID WHEEL
//------------------------------------------------------------------------------------------------------------------------
class RigidWheel
{
public:
    void Build(const TyreSpecification& Specification, const Vec3& HubPosition, const Quat& HubRotation) noexcept
    {
        Parameters = Specification;
        SnapshotPosition = HubPosition;
        SnapshotRotation = HubRotation;
        SpinAngle = 0.0f;
        ContactReaction = TyreReaction{};
        ContactReaction.EnclosedVolume = static_cast<float>(Parameters.RestVolume());
        ContactReaction.GaugePressure  = Parameters.InflationPressure;
    }

    // Advance the hub to the end-of-step pose and roll the material by SpinRate·dt.
    void Pose(float dt, const HubMotion& Hub) noexcept
    {
        constexpr float Pi = 3.14159265358979323846f;
        SpinAngle += Hub.SpinRate * dt;
        if (SpinAngle > Pi || SpinAngle < -Pi) SpinAngle = std::fmod(SpinAngle + Pi, 2.0f * Pi) - Pi;
        SnapshotPosition = Hub.Position;
        SnapshotRotation = Hub.Orientation;
    }

    void AssignReaction(const TyreReaction& Reaction) noexcept
    {
        ContactReaction = Reaction;
        ContactReaction.EnclosedVolume = static_cast<float>(Parameters.RestVolume());
        ContactReaction.GaugePressure  = Parameters.InflationPressure;
    }

    // A teleport of the chassis (spawn correction, reset) carries the wheel with it.
    void CorrectAttachment(const Vec3& BeforePosition, const Quat& BeforeRotation,
                           const Vec3& AfterPosition, const Quat& AfterRotation) noexcept
    {
        const Quat Rotation = (AfterRotation * BeforeRotation.Conjugate()).Normalized();
        SnapshotPosition = AfterPosition + Rotation.Rotate(SnapshotPosition - BeforePosition);
        SnapshotRotation = (Rotation * SnapshotRotation).Normalized();
    }

    [[nodiscard]] const TyreSpecification& Params() const noexcept { return Parameters; }
    [[nodiscard]] const TyreReaction& Reaction() const noexcept { return ContactReaction; }
    [[nodiscard]] Vec3  HubPosition() const noexcept { return SnapshotPosition; }
    [[nodiscard]] Quat  HubRotation() const noexcept { return SnapshotRotation; }
    [[nodiscard]] float CarcassAngle() const noexcept { return SpinAngle; }
    // The material frame: hub frame with the wheel's own spin composed inside it (a tread pattern is authored here).
    [[nodiscard]] Quat  MaterialFrame(const Quat& HubRot) const noexcept
    { return (HubRot * Quat::AxisAngle(Vec3{0.0f, 1.0f, 0.0f}, SpinAngle)).Normalized(); }
    [[nodiscard]] Quat  MaterialFrame() const noexcept { return MaterialFrame(SnapshotRotation); }
    [[nodiscard]] double RestVolume() const noexcept { return Parameters.RestVolume(); }
    [[nodiscard]] double EnclosedVolume() const noexcept { return Parameters.RestVolume(); }

private:
    TyreSpecification Parameters{};
    Vec3  SnapshotPosition{};
    Quat  SnapshotRotation{};
    float SpinAngle = 0.0f;
    TyreReaction ContactReaction{};
};

} // namespace Frontier::Vehicle

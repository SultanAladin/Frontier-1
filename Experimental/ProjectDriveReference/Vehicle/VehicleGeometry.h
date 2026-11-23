//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/VehicleGeometry.h — the vehicle's physical dimensions, as authored DATA
//============================================================================================================================================
//
//    In GRIT the wheel positions and aero force-application points came from skeletal-mesh SOCKETS, which live on the
//    game thread; GRIT had to pre-resolve them to CoM-relative offsets and marshal them across to the physics thread.
//    Frontier is our own engine, so we have none of that: the geometry is a single plain-data block that BOTH threads
//    read directly. `VehicleGeometry` is that block. From it we derive the four `WheelMount`s, every aero device's
//    `ForceApplicationPoint_COM`, the CoM-above-floor reference for ride height, and the inertia tensor.
//
//    ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────
//    THESE NUMBERS ARE NO LONGER ESTIMATES. They are extracted from the real authored vehicle,
//    `CarModelling/ControlVehicle/ControlVehicle.blend` (Blender 5.2, the "PROTO-X" wedge coupe), by reading the
//    socket Empties directly out of the .blend (see ParseBlendSockets.py alongside it, and the ControlVehicleSockets
//    table below). Blender's axes for this model already match our body frame: +X forward (nose), +Y left, +Z up —
//    confirmed by Socket_AxleMount_FL sitting at (+X,+Y) = front-left. So NO reorientation of the vehicle is required;
//    the sockets drop straight into the physics frame.
//
//    Comparison of the real model vs. GRIT's ChassisConfiguration defaults (the "what GRIT used" reference):
//                                   GRIT default        ControlVehicle.blend (real)
//        Wheelbase                    3.00 m               3.396 m   (front axle +1.7274, rear −1.6686)
//        Track (front == rear)        1.60 m               2.095 m   (wheel centres at Y = ±1.0475)
//        Suspension strut span         —                   0.552 m   (mount Z 0.6436 − hub Z 0.0914)
//    The model is a wide, long muscle/GT wedge; the horizontal geometry below IS the model, and it is within ~13 %
//    of GRIT on wheelbase. Mass and the vertical scalars (CoM height, tyre radius, ride height) are NOT stored in the
//    .blend as reliable authored data — they need the applied-modifier mesh bounds, which only local Blender can give
//    (ExportControlVehicle.py emits them). Until then they stay at documented GT/muscle-class values, tunable freely.
//
//    Wheels are DELIBERATELY not read from the model: the RubberFL* / RimFL* mesh objects are excluded and the sim
//    builds procedural wheels at the four Socket_AxleMount_* positions instead.
//    ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────

#pragma once

#include "SuspensionSpecification.h"
#include "VehicleMath.h"   // Vec3

#include <algorithm>
#include <vector>

namespace Frontier::Vehicle {

struct WheelMount;                 // fwd (defined in VehicleSolver.h)
struct VehicleSolverConfiguration;    // fwd

//------------------------------------------------------------------------------------------------------------------------
// Raw socket coordinates lifted verbatim from ControlVehicle.blend (metres, model-origin frame = +X fwd, +Y left, +Z up).
// The model origin sits on the longitudinal/lateral centreline at roughly hub height; CoM is placed at CoMHeight above
// ground by the dynamics. These constants exist so every derived offset below is traceable to the authored asset — nothing
// here is invented. (Camera-mount "sockets" Chassis_Mount_Exterior / Cockpit_Mount_Internal are omitted: they mark the
// external/interior CAMERA rigs, not chassis hard-points.)
//------------------------------------------------------------------------------------------------------------------------
namespace ControlVehicleSockets {
    // Axle mounts (wheel centres). Front at +X, rear at −X; left +Y, right −Y.
    inline const Vec3 AxleMount_FL { +1.7274f, +1.0475f, +0.0914f };
    inline const Vec3 AxleMount_FR { +1.7274f, -1.0475f, +0.0914f };
    inline const Vec3 AxleMount_RL { -1.6686f, +1.0475f, +0.0914f };
    inline const Vec3 AxleMount_RR { -1.6686f, -1.0475f, +0.0914f };
    // Suspension upper mounts (strut tops).
    inline const Vec3 SuspensionMount_FL { +1.7274f, +1.0475f, +0.6436f };
    inline const Vec3 SuspensionMount_FR { +1.7274f, -1.0475f, +0.6436f };
    inline const Vec3 SuspensionMount_RL { -1.6686f, +1.0475f, +0.6436f };
    inline const Vec3 SuspensionMount_RR { -1.6686f, -1.0475f, +0.6436f };
    // Aero / body hard-points.
    inline const Vec3 RearWingPort       { -2.9822f,  0.0000f, +0.8367f };
    inline const Vec3 RearWingPortLeft   { -2.9072f, +1.0576f, +0.8367f };
    inline const Vec3 RearWingPortRight  { -2.9072f, -1.0576f, +0.8367f };
    inline const Vec3 SideSkirt_L        { -0.0979f, +1.0745f, +0.0228f };
    inline const Vec3 SideSkirt_R        { -0.0979f, -1.0745f, +0.0228f };
    inline const Vec3 IdPlate_Primary    { +2.9842f,  0.0000f, +0.1006f };  // front bumper plate (nose extent)
    inline const Vec3 IdPlate_Secondary  { -2.9906f,  0.0000f, +0.2628f };  // rear bumper plate (tail extent)
}

struct VehicleGeometry
{
    // ---- Mass & primary dimensions (horizontal dims = ControlVehicle.blend sockets) ----
    float Mass               = 1300.0f;  // [kg] incl. driver + fuel (not stored in model/GRIT chassis — tunable)
    float Wheelbase          = 3.396f;   // [m]  front→rear axle (blend: +1.7274 − (−1.6686))
    float TrackFront         = 2.095f;   // [m]  centre-to-centre front tyres (blend: 2·1.0475)
    float TrackRear          = 2.095f;   // [m]  centre-to-centre rear tyres  (blend: 2·1.0475, equal front/rear)
    float FrontWeightFraction= 0.4914f;  // [-]  static front mass fraction — puts CoM on the model X-origin (front 1.727 / rear 1.669)
    // 💡 CoM height is 30 % of the 1.769 m overall vehicle height (model Z span 1.4575 plus the 0.41715 the model
    //    origin sits above ground) — the standard sports-car figure, and it keeps the static rollover threshold at
    //    1.98 g so the car still slides before it rolls. It was 0.35 m, which in the corrected frame is BELOW the
    //    floor pan, because the model-to-ground offset below was missing and the two errors hid each other.
    float CoMHeight          = 0.53f;    // [m]  centre of mass above flat ground at rest
    // ---- Wheel dimensions, MEASURED from ControlVehicle.blend (RubberFL / RimFL mesh bounds) ----
    //   The vertical numbers are no longer placeholders. ExtractWheels.py walks the .blend block table and reports
    //   the tyre mesh as 1.0171 m across and 0.3958 m wide, and the rim mesh as 0.6892 m across. The frames close on
    //   themselves: the axle sockets sit at model Z +0.0914 and the body mesh bottoms out at −0.4172, and
    //   0.0914 − (−0.4172) = 0.5086 = the measured tyre radius, so the modelled car stands exactly on its tyres.
    float TyreRadius         = 0.50855f; // [m]  unloaded tread radius   (blend: RubberFL Z/X span 1.0171 / 2)
    float TyreRimRadius      = 0.34460f; // [m]  bead / rim radius       (blend: RimFL Z/X span 0.6892 / 2)
    float TyreWidth          = 0.39580f; // [m]  section width           (blend: RubberFL Y span)
    float StaticRideHeight   = 0.06f;    // [m]  underfloor-to-ground at rest (feeds ground-effect ride height)

    // ---- Unsprung mass: what hangs BELOW the spring and therefore does not ride on it ----
    //   Carcass 12 kg (TyreSpecification::TotalMass) plus hub, upright, brake and half the strut.
    float UnsprungMassPerWheel = 20.0f;  // [kg] excluded from the sprung mass each corner's spring is rated for

    // ---- Suspension, ported from GRIT (see SuspensionSpecification.h) ----
    SuspensionSpecification Front{};     // [-]  front strut pair
    SuspensionSpecification Rear{};      // [-]  rear strut pair
    AntiRollBar             FrontBar{ true, 80000.0f, 1500.0f, 0u, 1u };
    AntiRollBar             RearBar { true, 80000.0f, 1500.0f, 2u, 3u };
    RollCentreGeometry      RollCentres{};

    // ---- Radii of gyration for the inertia tensor (m) ----
    //   Iaa = Mass · k_a². Truer than a solid-box estimate: mass concentrated low and central, so yaw/pitch inertia
    //   sits well below a box of the same envelope. Scaled for the model's longer wheelbase.
    float RollRadiusGyration  = 0.60f;   // [m] about body-X (forward)
    float PitchRadiusGyration = 1.55f;   // [m] about body-Y (right) — grows with the longer wheelbase
    float YawRadiusGyration   = 1.60f;   // [m] about body-Z (up)

    // ---- Derived quantities ----
    [[nodiscard]] float FrontAxleX() const noexcept { return Wheelbase * (1.0f - FrontWeightFraction); } // CoM→front (+x)
    [[nodiscard]] float RearAxleX()  const noexcept { return Wheelbase * FrontWeightFraction; }          // CoM→rear  (−x)
    // 🔴 The model origin is NOT on the ground. The axle sockets sit at model Z +0.0914 and the car stands on tyres
    //    of radius 0.50855, so the ground plane is 0.41715 m BELOW the model origin. Every conversion between the
    //    authored frame and a ground-referenced one goes through here. Omitting it draws the body one whole offset
    //    into the floor, which is what "the car is being dragged along the ground" looks like.
    [[nodiscard]] float ModelGroundOffset() const noexcept
    { return TyreRadius - ControlVehicleSockets::AxleMount_FL.z; }

    // Centre of mass expressed in the authored model frame, so mesh vertices can be rebased onto it directly.
    [[nodiscard]] float CoMModelZ() const noexcept { return CoMHeight - ModelGroundOffset(); }

    [[nodiscard]] float WheelLocalZ() const noexcept { return TyreRadius - CoMHeight; }                  // hub z rel. CoM

    // Strut top for one corner, CoM-relative, straight off Socket_SuspensionMount_*. 0 = FL, 1 = FR, 2 = RL, 3 = RR.
    [[nodiscard]] Vec3 SuspensionMountLocal(uint32_t Wheel) const noexcept
    {
        namespace S = ControlVehicleSockets;
        const Vec3 Socket = (Wheel == 0u) ? S::SuspensionMount_FL : (Wheel == 1u) ? S::SuspensionMount_FR
                          : (Wheel == 2u) ? S::SuspensionMount_RL : S::SuspensionMount_RR;
        return { Socket.x, Socket.y, Socket.z - CoMModelZ() };
    }

    // Hub rest position for one corner, CoM-relative, straight off Socket_AxleMount_*.
    [[nodiscard]] Vec3 AxleMountLocal(uint32_t Wheel) const noexcept
    {
        namespace S = ControlVehicleSockets;
        const Vec3 Socket = (Wheel == 0u) ? S::AxleMount_FL : (Wheel == 1u) ? S::AxleMount_FR
                          : (Wheel == 2u) ? S::AxleMount_RL : S::AxleMount_RR;
        return { Socket.x, Socket.y, Socket.z - CoMModelZ() };
    }

    // Authored strut span: 0.6436 − 0.0914 = 0.5522 m. This is the STATIC length — the model is posed at rest — so
    //    SuspensionSpecification::ResolveFromSockets adds the static sag back to recover the free length.
    [[nodiscard]] float StrutStaticLength() const noexcept
    { return ControlVehicleSockets::SuspensionMount_FL.z - ControlVehicleSockets::AxleMount_FL.z; }
    [[nodiscard]] float ComHeightAboveFloor() const noexcept { return CoMHeight - StaticRideHeight; }    // for ride height
    [[nodiscard]] Vec3  Inertia() const noexcept
    {
        const float SprungMass = std::max(1.0f, Mass - 4.0f * UnsprungMassPerWheel);
        return { SprungMass * RollRadiusGyration  * RollRadiusGyration,
                 SprungMass * PitchRadiusGyration * PitchRadiusGyration,
                 SprungMass * YawRadiusGyration   * YawRadiusGyration };
    }
    [[nodiscard]] Vec3 InvInertia() const noexcept
    {
        const Vec3 I = Inertia();
        return { 1.0f / I.x, 1.0f / I.y, 1.0f / I.z };
    }
    // Static rollover threshold [g] — a sanity number, not used by the sim.
    // Wide track + low CoM ⇒ (0.5·2.095)/0.53 ≈ 1.98 g ⇒ the car slides long before it rolls.
    [[nodiscard]] float RolloverThresholdG() const noexcept { return (0.5f * TrackFront) / CoMHeight; }
};

// Build the four wheel mounts (FL, FR, RL, RR) from the geometry. Front wheels steer; rears drive (RWD).
[[nodiscard]] std::vector<WheelMount> MakeWheelMounts(const VehicleGeometry& g);

// Rate both axles for the sprung mass they carry and recover the free strut length from the sockets.
//    Mutates g; call before ApplyGeometry, which copies the resolved struts into the solver config.
void ResolveSuspension(VehicleGeometry& g);

// Stamp geometry onto a controller config: wheels, aero device force points, CoM-above-floor and mass.
void ApplyGeometry(VehicleSolverConfiguration& config, const VehicleGeometry& g);

} // namespace Frontier::Vehicle

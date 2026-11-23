//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/VehicleGeometry.cpp — derive wheel mounts + aero force points from the authored VehicleGeometry.
//
//   Horizontal positions are lifted straight from ControlVehicle.blend sockets (see ControlVehicleSockets). The model
//   origin lies on the vehicle centreline (X = Y = 0) at roughly hub height, so socket X/Y ARE the CoM-relative offsets
//   directly. Socket Z is model-origin-relative; the CoM rides a little above the origin, so aero heights are shifted
//   down by `kComLift` to land in the CoM frame. That lift is small next to the metre-scale moment arms, but we apply it
//   so the numbers stay honest rather than fudged.
//============================================================================================================================================

#include "VehicleGeometry.h"
#include "VehicleSolver.h"   // WheelMount, VehicleSolverConfiguration (full definitions)

namespace Frontier::Vehicle {

// Model origin → CoM vertical offset [m]: the reference point sits ~this far below the centre of mass.
static constexpr float kComLift = 0.10f;

std::vector<WheelMount> MakeWheelMounts(const VehicleGeometry& g)
{
    // 💡 Read straight from Socket_AxleMount_*, not rebuilt from Wheelbase/Track arithmetic. The two agreed to the
    //    millimetre, but only by coincidence: nothing kept them in step, so editing the track width silently moved
    //    the wheels away from the sockets the body mesh was modelled around. The sockets are the authored truth.
    return {
        WheelMount{ g.AxleMountLocal(0u), g.SuspensionMountLocal(0u), /*Steered*/true,  /*Driven*/false, /*Braked*/true },
        WheelMount{ g.AxleMountLocal(1u), g.SuspensionMountLocal(1u), /*Steered*/true,  /*Driven*/false, /*Braked*/true },
        WheelMount{ g.AxleMountLocal(2u), g.SuspensionMountLocal(2u), /*Steered*/false, /*Driven*/true,  /*Braked*/true },
        WheelMount{ g.AxleMountLocal(3u), g.SuspensionMountLocal(3u), /*Steered*/false, /*Driven*/true,  /*Braked*/true },
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SUSPENSION RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

// Rate each axle for the sprung mass it actually carries, then recover the free strut length from the sockets.
//    📐 The sprung mass is the total less everything hanging below the springs, split by the static weight
//    distribution and then halved because each axle has two corners.
void ResolveSuspension(VehicleGeometry& g)
{
    const float Sprung     = g.Mass - 4.0f * g.UnsprungMassPerWheel;         // [kg]
    const float FrontCorner = 0.5f * Sprung * g.FrontWeightFraction;         // [kg] per front corner
    const float RearCorner  = 0.5f * Sprung * (1.0f - g.FrontWeightFraction);// [kg] per rear corner

    g.Front.Calibrate(FrontCorner);
    g.Rear .Calibrate(RearCorner);

    // The authored strut span is the STATIC length; the free length is that plus the sag the spring is already
    //    carrying at rest. Resolve each axle against its own sag so the car settles onto the pose the artist posed.
    // 📝 Struts support the sprung body; rim and tread masses live at the corners.
    const float MountZ     = ControlVehicleSockets::SuspensionMount_FL.z;
    const float AxleZ      = ControlVehicleSockets::AxleMount_FL.z;
    const float Gravity    = 9.81f;
    const float FrontLoad  = 0.5f * Sprung * g.FrontWeightFraction        * Gravity;   // [N] per front corner
    const float RearLoad   = 0.5f * Sprung * (1.0f - g.FrontWeightFraction) * Gravity; // [N] per rear corner
    g.Front.ResolveFromSockets(MountZ, AxleZ, FrontLoad);
    g.Rear .ResolveFromSockets(MountZ, AxleZ, RearLoad);

    // Telescopic struts put the roll centre at the mount, which is unrealistically high; GRIT scales it down by a
    //    correction factor to stand in for control-arm geometry. Ground-referenced, hence the model offset.
    if (g.RollCentres.FrontArchitecture == SuspensionArchitecture::Telescopic)
        g.RollCentres.FrontHeight = (MountZ + g.ModelGroundOffset()) * g.RollCentres.TelescopicCorrection;
    if (g.RollCentres.RearArchitecture == SuspensionArchitecture::Telescopic)
        g.RollCentres.RearHeight  = (MountZ + g.ModelGroundOffset()) * g.RollCentres.TelescopicCorrection;
}

void ApplyGeometry(VehicleSolverConfiguration& c, const VehicleGeometry& Authored)
{
    // 📝 Resolve on a copy so every existing call site picks up calibrated struts without changing its signature:
    //    the rates depend on the mass split, and nothing should be able to build a solver with uncalibrated springs.
    VehicleGeometry g = Authored;
    ResolveSuspension(g);

    c.ChassisMass = g.Mass - 4.0f * g.UnsprungMassPerWheel;
    c.InvChassisInertia = g.InvInertia();
    const float RimMass = std::max(0.0f, g.UnsprungMassPerWheel - c.Tyre.TotalMass);
    c.WheelInertia = 0.5f * RimMass * g.TyreRimRadius * g.TyreRimRadius;
    c.Wheels      = MakeWheelMounts(g);
    // Keep the soft tyre in sync with the geometry. All three come from the ControlVehicle.blend wheel meshes, so the
    //    tyre the contact fan samples is the size of the wheel the renderer draws — one number, not two.
    // Struts and bars, already rated for the corner masses by ResolveSuspension.
    c.FrontStrut   = g.Front;
    c.RearStrut    = g.Rear;
    c.FrontBar     = g.FrontBar;
    c.RearBar      = g.RearBar;
    c.UnsprungMass = g.UnsprungMassPerWheel;

    if (g.TyreRadius    > 0.01f) c.Tyre.Radius    = g.TyreRadius;
    if (g.TyreRimRadius > 0.01f) c.Tyre.RimRadius = g.TyreRimRadius;
    if (g.TyreWidth     > 0.01f) c.Tyre.Width     = g.TyreWidth;

    // Ride-height reference: ground-effect terms use (CoM.z − groundZ) − this.
    c.Aero.ComHeightAboveFloor_m = g.ComHeightAboveFloor();

    namespace S = ControlVehicleSockets;
    const float a = g.FrontAxleX();   // +1.7274 (front axle X)

    // Aero device force-application points, CoM-relative (body frame: +x forward, +y left, +z up).
    // Rear wing: straight from Socket_RearWingAssemblyPort (behind the rear axle, high on the tail).
    c.Aero.RearWing.ForceApplicationPoint_COM      = { S::RearWingPort.x, 0.0f, S::RearWingPort.z - kComLift };
    // Front splitter: no dedicated socket — placed on the front lower lip, between the front axle and the nose ID plate,
    // at side-skirt (floor) height.
    c.Aero.FrontSplitter.ForceApplicationPoint_COM = { 0.5f * (a + S::IdPlate_Primary.x), 0.0f, S::SideSkirt_L.z - kComLift };
    // Floor diffuser: under the rear floor, ahead of the wing, at floor height.
    c.Aero.FloorDiffuser.ForceApplicationPoint_COM = { 0.5f * (-g.RearAxleX() + S::RearWingPort.x), 0.0f, S::SideSkirt_L.z - kComLift };
    // Body centre of pressure: on the centreline, mid-height between floor skirt and canopy.
    c.Aero.VehicleBody.ForceApplicationPoint_COM   = { 0.0f, 0.0f, 0.30f - kComLift };

    // Canards: no sockets in the model — procedural front pair, ahead of the front axle, at splitter/skirt height.
    for (std::size_t i = 0; i < c.Aero.Canards.size(); ++i)
    {
        const float sy = (i % 2 == 0) ? +0.85f : -0.85f;                // left / right pair, inboard of the track
        const float fx = (i < 2) ? (a + 0.55f) : (a + 0.25f);          // front pair slightly ahead of rear pair
        c.Aero.Canards[i].ForceApplicationPoint_COM = { fx, sy, 0.18f - kComLift };
    }
}

} // namespace Frontier::Vehicle

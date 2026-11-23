//============================================================================================================================
// 🧩 Frontier — VehicleSolver.h
//============================================================================================================================
// 📝 ControlVehicle rigid-body dynamics: socket struts, transient-slip MF6.1 tyres, GT-R powertrain, brakes, aero, contact.
//    Port of the HTML demonstration's VehicleSolver.js, equation-for-equation at its fixed 480 Hz substep. The chassis,
//    sockets, struts and simplified drivetrain follow the HTML demo and are not a verified Project Drive vehicle port.
//    The demo spring carries corner load in spring, projection and solid modes. Projection and Hex8 are the current
//    VehicleHMI demo's 160-particle deformation solvers; their prescribed forces do not feed back into the chassis.
//    The older 84-particle lattice is a separate approximation that does feed its reaction into the vehicle.
//    Deviations from the demo, stated: the driving surface is a caller-supplied height function (the demo's sculpted
//    course is exhibit content, not engine), wheels do not yet ride loose debris (the demo's RigidBodyWorld support),
//    the 700-point production hull is not assigned so the demo's sphere hull stands in, and scrape/spark bookkeeping
//    (purely visual in the demo) is omitted. Thermal stepping is integrated here exactly as the demo's host drives it
//    once per advance: drain wheel readings, advance TyreThermalSolver, assign pressures and grip, advance tyre wear,
//    and turn pinch cuts into punctures.
//============================================================================================================================

#pragma once

#include <vector>

#include "Engine/Physics/TyreSlipSolver.h"
#include "Engine/Physics/TyreThermalSolver.h"
#include "Engine/Physics/TyreCarcassLattice.h"
#include "Engine/Physics/TyreContinuum.h"
#include "Engine/Physics/VehicleSpecification.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       DRIVER COMMAND
//------------------------------------------------------------------------------------------------------------------------

struct VehicleCommand
{
    double Throttle   = 0.0;   // [-] 0..1
    double Brake      = 0.0;   // [-] 0..1 (also reverse once stopped, per the demo's pedal logic)
    double Steer      = 0.0;   // [-] -1..1 of the speed-eased lock
    bool   Handbrake  = false; // [-]
};

// 📝 Static axis-aligned obstacle (walls, blocks) the sphere hull collides with — the demo's obstacle boxes.
struct ObstacleBox
{
    double Minimum[3] = { 0.0, 0.0, 0.0 };   // [m] world
    double Maximum[3] = { 0.0, 0.0, 0.0 };   // [m] world
};

// 📝 Spring carries the load in the demo modes; its projection/solid models supply nodal deformation.
//    The older lattice mode is separate and replaces the spring force with its own reaction.
enum CarcassModel
{
    CarcassSpringModel     = 0,
    CarcassLatticeModel    = 1,
    CarcassProjectionModel = 2,
    CarcassSolidModel      = 3
};

// 📝 Caller-supplied driving surface. Height [m] at world (X, Y); Normal writes the unit surface normal. Either may be
//    null, giving the flat plane z = 0 with normal +Z.
typedef double (*GroundHeightFunction)(double X, double Y, void* Context);
typedef void (*GroundNormalFunction)(double X, double Y, void* Context, double OutNormal[3]);

//------------------------------------------------------------------------------------------------------------------------
//                                                       WHEEL STATE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Per-wheel dynamic state, the demo's wheel record (visual-only fields omitted).
struct VehicleWheelState
{
    double Mount[3]        = { 0.0, 0.0, 0.0 };  // [m]      strut top mount, CoM-local
    bool   Steered         = false;              // [-]
    bool   Driven          = false;              // [-]
    double Length          = 0.0;                // [m]      strut length
    double Compression     = 0.0;                // [m]      strut compression from free length
    double Deflection      = 0.0;                // [m]      closure past bump travel (carcass series travel)
    double Rate            = 0.0;                // [m/s]    compression rate
    double Spin            = 0.0;                // [rad/s]
    double SpinAngle       = 0.0;                // [rad]
    double SlipRatio       = 0.0;                // [-]      transient (relaxed) κ′
    double SlipAngle       = 0.0;                // [rad]    transient (relaxed) α′
    double Load            = 0.0;                // [N]      corner load through the tyre
    bool   Contact         = false;              // [-]
    double Steer           = 0.0;                // [rad]
    double Skid            = 0.0;                // [-]      0..1 combined-slip severity
    double Grip            = 1.0;                // [-]      thermal grip factor
    double Radius          = 0.50855;            // [m]      current outer radius (rim + margin once shredded)
    double Pressure        = 220000.0;           // [Pa]     gauge, from the thermal gas law
    double TyreDeflection  = 0.016;              // [m]      carcass radial deflection
    double TyreForce       = 0.0;                // [N]      carcass normal force
    double DeflectionRate  = 0.0;                // [m/s]
    bool   Pinched         = false;              // [-]      rim flange through the rubber
    bool   ToppedOut       = false;              // [-]      strut at full droop, corner on the tyre alone
    bool   Shredded        = false;              // [-]      rubber shed; running on the rim
    double ShredTravel     = 0.0;                // [m]      distance rolled flat
    double HubDistance     = 1.0e30;             // [m]      hub height over the road (radius − deflection in contact)
    double GroundNormal[3] = { 0.0, 0.0, 1.0 };  // [-]
    double LateralForce    = 0.0;                // [N]
    double RollingSpeed    = 0.0;                // [m/s]    |ω|·r
    double ContactPoint[3] = { 0.0, 0.0, 0.0 };  // [m]      world
    bool   HasContactPoint = false;              // [-]
    double LatticeStiffness = 223000.0;          // [N/m]    XPBD lattice tangent stiffness feeding the stack solve
};

// 📝 Frame events drained by ResolveEvents: shreds and pinch cuts since the last drain (spark scrapes omitted, see header).
struct VehicleEvents
{
    int    ShredWheels[4]  = { -1, -1, -1, -1 };  // [-] wheel indices, -1 = none
    int    ShredCount      = 0;                   // [-]
    int    PinchWheels[8]  = { -1, -1, -1, -1, -1, -1, -1, -1 };
    double PinchRates[8]   = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };  // [m/s] closing rate at the cut
    int    PinchCount      = 0;                   // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SOLVER
//------------------------------------------------------------------------------------------------------------------------

class VehicleSolver
{
public:
    explicit VehicleSolver(const std::vector<ObstacleBox>& Obstacles = std::vector<ObstacleBox>());

    // 📝 Places the car at rest: Position is the centre of mass [m] world, Yaw about +Z [rad]. Demo default spawn height
    //    is CoMHeight + 0.02 m over the ground.
    void Respawn(const double Position[3], double Yaw);

    // 📝 Advances at the fixed 480 Hz substep, then steps the thermal model once for the whole interval (demo host order).
    void Advance(const VehicleCommand& Command, double Seconds);

    // 📝 An impulse [N·s] at a world point — debris and cones push back through this.
    void ApplyImpulse(const double Point[3], const double Impulse[3]);

    void AssignObstacles(const std::vector<ObstacleBox>& Obstacles);
    void AssignGround(GroundHeightFunction Height, GroundNormalFunction Normal, void* Context);
    void AssignAssists(bool LineLockEnabled);
    void AssignCarcassModel(CarcassModel Model);
    void AssignTyrePressures(const double Pressures[4]);
    void AssignGrip(const double Factors[4]);
    void RestoreTyres();

    // 📝 Hub world position and full wheel orientation quaternion (body · steer about +Z · spin about +Y).
    void QueryWheelPose(int Index, double OutHub[3], double OutOrientation[4]) const;

    // 📝 The XPBD carcass lattice of one wheel (particle queries for rendering/inspection; most recently stepped
    //    when the lattice carcass model is active).
    const TyreCarcassLattice& QueryCarcassLattice(int Index) const { return m_Lattices[Index]; }
    const TyreCarcassProjection& QueryCarcassProjection(int Index) const { return m_Projections[Index]; }
    const TyreCarcassSolid& QueryCarcassSolid(int Index) const { return m_Solids[Index]; }
    CarcassModel QueryCarcassModel() const { return m_CarcassModel; }

    VehicleEvents ResolveEvents();

    // 📝 Chassis state, world frame (demo field layout).
    double Position[3]        = { 0.0, 0.0, 0.55 };   // [m]
    double Orientation[4]     = { 0.0, 0.0, 0.0, 1.0 };  // [x, y, z, w]
    double LinearVelocity[3]  = { 0.0, 0.0, 0.0 };    // [m/s]
    double AngularVelocity[3] = { 0.0, 0.0, 0.0 };    // [rad/s]
    VehicleWheelState Wheels[4];
    double SteerAngle  = 0.0;    // [rad]  rack angle before the Ackermann split
    int    GearIndex   = 3;      // [-]    TransmissionSheet index (3 = 1st)
    double EngineRPM   = 700.0;  // [rpm]
    double Boost       = 0.0;    // [bar]
    double Throttle    = 0.0;    // [-]    effective throttle after the pedal/reverse logic
    bool   Burnout     = false;  // [-]    line lock currently engaged
    bool   Frozen      = false;  // [-]    held where it is: no integration, no forces

    TyreThermalSolver Thermal;   // [-]    tyre heat, gas law, grip, wear and failure

private:
    void Step(const VehicleCommand& Command, double Seconds);
    double ResolveDriveTorque(double ThrottleIn, double ForwardSpeed, double Seconds);
    void ResolveHullContacts();
    void AdvanceTyreWear(double Seconds);
    void AdvanceTyreContinuum(double Seconds);
    void ShredTyre(int Index);
    double GroundHeight(double X, double Y) const;
    void GroundNormal(double X, double Y, double OutNormal[3]) const;
    void ResolveNearObstacles();

    TyreSlipSolver m_Tyres;
    SuspensionSpecification m_Struts[4];
    TyreCarcassLattice m_Lattices[4];
    TyreCarcassProjection m_Projections[4];
    TyreCarcassSolid m_Solids[4];
    CarcassModel m_CarcassModel = CarcassSpringModel; // demo defaults to solid; native default preserves existing callers
    double m_StaticLength = 0.6436 - 0.0914;       // [m] socket separation at rest
    double m_InertiaBody[3] = { 0.0, 0.0, 0.0 };   // [kg·m²]
    double m_Accumulator = 0.0;                    // [s]
    double m_ShiftTimer = 0.0;                     // [s]
    double m_ReverseTimer = 0.0;                   // [s]
    bool m_LineLock = true;                        // [-]
    double m_Pressures[4] = { 220000.0, 220000.0, 220000.0, 220000.0 };  // [Pa]
    bool m_HasPressures = false;                   // [-]
    std::vector<ObstacleBox> m_Obstacles;
    std::vector<int> m_NearObstacles;              // [-] indices into m_Obstacles
    GroundHeightFunction m_GroundHeight = nullptr;
    GroundNormalFunction m_GroundNormal = nullptr;
    void* m_GroundContext = nullptr;
    // 📝 Reading accumulators (the demo's per-wheel sampling fields) and pending events.
    double m_SlidingEnergy[4]  = { 0.0, 0.0, 0.0, 0.0 };   // [J]
    double m_ContactSeconds[4] = { 0.0, 0.0, 0.0, 0.0 };   // [s]
    double m_SampleSeconds[4]  = { 0.0, 0.0, 0.0, 0.0 };   // [s]
    double m_SlipTravel[4]     = { 0.0, 0.0, 0.0, 0.0 };   // [m]
    VehicleEvents m_Events;
};

} // namespace Frontier

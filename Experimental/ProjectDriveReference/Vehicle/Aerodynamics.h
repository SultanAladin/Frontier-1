//============================================================================================================================================
// 📦 Frontier/PhysicalDynamics/Vehicle/Aerodynamics.h — adaptive aerodynamics (faithful port of GRIT `source-only`)
//============================================================================================================================================
//
//    This is the FIRST subsystem re-ported from the up-to-date GRIT branch `source-only`
//    (SultanAladin/GRIT@e61b15c, 2026-09-29) — NOT the 8-month-stale `main`. It reproduces
//    `AVehicleSolver::ComputeAerodynamicForces()` (VehicleFramework/VehicleSolver.cpp) and the
//    component config in `Components/AerodynamicSpecifications.h`, line for line, in engine-agnostic
//    C++ that reuses the self-contained `Vec3` from VehicleMath.h (no Unreal, no Jolt).
//
//    The package models a GT3-class adaptive aero kit:
//        • Rear wing        — lifting-line (Prandtl) lift + induced drag, DRS-style adaptive angle, aero-brake deploy
//        • Front splitter   — pressure-coefficient downforce with ride-height (ground-effect) sensitivity
//        • Canards ×N       — lifting-line, adaptive with throttle reduction and damage
//        • Underbody/floor + diffuser — Bernoulli venturi + diffuser pressure recovery, ride-height map
//        • Side skirts L/R  — ground-effect sealing suction
//        • Vortex generators— diffuser/underbody enhancement + drag penalty
//        • Body             — 3D component drag (Cd·A), body lift, side-force
//
//    Sign / frame conventions match the rest of the vehicle layer and GRIT:
//        right-handed, +Z up, body-local +X forward, +Y left. Downforce is stored POSITIVE (a magnitude that
//        pushes the car DOWN); `LiftForceWorld = -up · TotalDownforce`. Drag opposes the velocity direction.
//
//    HOW THE CONTROLLER APPLIES THESE (see VehicleSolver::StepPacejka):
//        • Drag + side-force + aero moments  → applied to the CHASSIS rigid body.
//        • Downforce                         → added to the per-axle tyre vertical load Fz that feeds the Pacejka
//          slip model (Front/RearDownforce split across front/rear wheels), i.e. "more Fz ⇒ more grip".
//          This is exactly GRIT's documented rule ("Downforce is applied to wheel loads, NOT to chassis";
//          VehicleSolver.h ~line 600). It reuses the existing Fz→Pacejka path and never injects free vertical
//          momentum into the chassis.

#pragma once

#include "VehicleMath.h"   // Vec3, Dot

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier::Vehicle {

// ---------------------------------------------------------------------------------------------------------------------
// Component configuration structs. Fields + defaults mirror the `*_GT` structs in
// GRIT Components/AerodynamicSpecifications.h; `ForceApplicationPoint_COM` is the body-local force point
// (GRIT resolves it from mesh sockets — headless we set it directly). `Enabled` mirrors "socket exists".
// ---------------------------------------------------------------------------------------------------------------------

// --- Rear wing / spoiler --- (defaults = GRIT GT3 factory kit, InitializeAerodynamicsPackage())
struct AeroWing
{
    bool  Enabled           = true;
    float Area_m2           = 1.4f;    // [m²]
    float BaseCoeffLift     = -3.2f;   // [-] negative ⇒ downforce
    float BaseCoeffDrag     = 0.52f;   // [-]
    float AspectRatio       = 3.5f;    // [-] span²/area
    float MaxAngle_deg      = 22.0f;   // [deg]
    float MinAngle_deg      = 4.0f;    // [deg]
    float CurrentAngle_deg  = 14.0f;   // [deg]
    bool  IsAdaptive        = true;    // DRS-style adaptive angle
    float AdaptiveSpeedThreshold_ms = 35.0f; // [m/s]
    float BrakeDeployAngle_deg      = 10.0f; // [deg] extra angle under braking (air-brake)
    Vec3  ForceApplicationPoint_COM = {-1.45f, 0.0f, 0.65f}; // [m] behind + above CoM
};

// --- Front splitter / air dam --- (defaults = GRIT GT3 factory)
struct AeroSplitter
{
    bool  Enabled              = true;
    float Area_m2              = 0.75f;  // [m²]
    float CoeffPressure        = 0.9f;   // [-] pressure coeff (positive value scaled to downforce)
    float AirDamHeight_m       = 0.08f;  // [m]
    float AirDamDragCoeff      = 0.28f;  // [-]
    float RideHeightSensitivity = 3.2f;  // [-]
    Vec3  ForceApplicationPoint_COM = {1.55f, 0.0f, -0.30f}; // [m] ahead + below CoM
};

// --- Canard (front dive plane) --- (defaults = GRIT GT3 front canard)
struct AeroCanard
{
    bool  Enabled           = true;
    float Area_m2           = 0.32f;   // [m²]
    float BaseCoeffLift     = -2.1f;   // [-]
    float BaseCoeffDrag     = 0.38f;   // [-]
    float AspectRatio       = 2.2f;    // [-]
    float MaxAngle_deg      = 28.0f;   // [deg]
    float MinAngle_deg      = 2.0f;    // [deg]
    float CurrentAngle_deg  = 12.0f;   // [deg]
    bool  IsAdaptive        = true;
    float AdaptiveSpeedThreshold_ms = 28.0f; // [m/s]
    float BrakeDeployAngle_deg      = 15.0f; // [deg]
    float ThrottleReductionAngle_deg = 8.0f; // [deg] reduce at high throttle
    float DamageLevel       = 0.0f;    // [0..1]
    Vec3  ForceApplicationPoint_COM = {1.35f, 0.55f, 0.10f}; // [m] (per-canard; sign of Y set per instance)
};

// --- Underbody floor + diffuser --- (defaults = GRIT GT3 flat floor + diffuser)
struct AeroUnderbody
{
    bool  Enabled            = true;
    float FloorArea_m2       = 3.2f;    // [m²]
    float DiffuserArea_m2    = 1.6f;    // [m²]
    float DiffuserAngle_deg  = 16.0f;   // [deg]
    float BaseCoeffPressure  = -0.75f;  // [-] negative = suction (kept for parity; active model uses Cp_floor venturi)
    float DiffuserEfficiency = 0.82f;   // [0..1]
    float RideHeightOptimum_m  = 0.035f;// [m]
    float RideHeightCritical_m = 0.012f;// [m]
    float DiffuserDragCoeff  = 0.09f;   // [-]
    Vec3  ForceApplicationPoint_COM = {-0.20f, 0.0f, -0.35f}; // [m] slightly rearward of CoM, low
};

// --- Side skirt (single side) --- (defaults = GRIT GT3)
struct AeroSideSkirt
{
    bool  Enabled                  = true;
    float Length_m                 = 1.8f;   // [m]
    float Height_m                 = 0.14f;  // [m]
    float CoeffPressure            = -0.48f; // [-] negative = suction
    float UnderbodySealingEfficiency = 0.72f;// [0..1]
    float RideHeightSensitivity    = 3.5f;   // [-]
    float DragCoeff                = 0.18f;  // [-]
    float DamageLevel              = 0.0f;   // [0..1]
};

// --- Vortex generators --- (defaults = GRIT GT3)
struct AeroVortexGen
{
    bool  Enabled                 = true;
    float TotalArea_m2            = 0.12f;   // [m²]
    float VortexStrength          = 0.78f;   // [0..1]
    float DiffuserEnhancementFactor = 1.32f; // [1..2]
    float DragPenalty             = 0.06f;   // [-]
    bool  EnhancesDiffuser        = true;
    bool  EnhancesUnderbody       = true;
};

// --- Body aerodynamics --- (defaults = GRIT GT3 body)
struct AeroBody
{
    bool  Enabled          = true;
    float FrontalArea_m2   = 2.3f;   // [m²]
    float SideArea_m2      = 3.8f;   // [m²]
    float CoeffDrag        = 0.28f;  // [-]
    float CoeffLift        = 0.12f;  // [-] positive = lift up
    float CoeffSideForce   = 0.42f;  // [-]
    Vec3  ForceApplicationPoint_COM = {0.10f, 0.0f, 0.15f}; // [m] centre of pressure vs CoM
};

// ---------------------------------------------------------------------------------------------------------------------
// The full package (mirrors FAerodynamicPackage_GT/PT).
// ---------------------------------------------------------------------------------------------------------------------
struct AerodynamicPackage
{
    bool  Enabled            = true;          // master switch (skip all aero when false)
    float AirDensity_kgm3    = 1.225f;        // [kg/m³] sea-level ISA
    float AeroBrakeThreshold = 0.65f;         // [-] brake input above which the air-brake deploys

    AeroWing                    RearWing;
    AeroSplitter                FrontSplitter;
    std::vector<AeroCanard>     Canards;      // 0..N (GT3 default kit = 4)
    AeroUnderbody               FloorDiffuser;
    AeroSideSkirt               LeftSideSkirt;
    AeroSideSkirt               RightSideSkirt;
    AeroVortexGen               VortexGens;
    AeroBody                    VehicleBody;

    // Height of the CoM above the underfloor reference plane [m]; ride height = (CoM.z − groundZ) − this.
    // Only used to derive the dynamic ride height fed to the ground-effect terms.
    float ComHeightAboveFloor_m = 0.32f;

    // GT3 factory kit: 4 canards (FR, FL, RR, RL), matching GRIT InitializeAerodynamicsPackage().
    //   Front pair (0,1) = larger/more aggressive; rear pair (2,3) = smaller. Values are verbatim GRIT.
    static AerodynamicPackage DefaultGT3()
    {
        AerodynamicPackage p;
        p.Canards.resize(4);
        // Front canards FR/FL — Canards[0] template (Canards[1] = copy).
        for (int i : {0, 1})
        {
            p.Canards[i].Area_m2 = 0.32f;  p.Canards[i].BaseCoeffLift = -2.1f; p.Canards[i].BaseCoeffDrag = 0.38f;
            p.Canards[i].AspectRatio = 2.2f; p.Canards[i].MaxAngle_deg = 28.0f; p.Canards[i].MinAngle_deg = 2.0f;
            p.Canards[i].CurrentAngle_deg = 12.0f; p.Canards[i].AdaptiveSpeedThreshold_ms = 28.0f;
            p.Canards[i].BrakeDeployAngle_deg = 15.0f; p.Canards[i].ThrottleReductionAngle_deg = 8.0f;
        }
        // Rear canards RR/RL — Canards[2] template (Canards[3] = copy).
        for (int i : {2, 3})
        {
            p.Canards[i].Area_m2 = 0.24f;  p.Canards[i].BaseCoeffLift = -1.6f; p.Canards[i].BaseCoeffDrag = 0.32f;
            p.Canards[i].AspectRatio = 2.0f; p.Canards[i].MaxAngle_deg = 22.0f; p.Canards[i].MinAngle_deg = 2.0f;
            p.Canards[i].CurrentAngle_deg = 10.0f; p.Canards[i].AdaptiveSpeedThreshold_ms = 28.0f;
            p.Canards[i].BrakeDeployAngle_deg = 12.0f; p.Canards[i].ThrottleReductionAngle_deg = 6.0f;
        }
        p.Canards[0].ForceApplicationPoint_COM = { 1.35f,  0.55f, 0.10f};
        p.Canards[1].ForceApplicationPoint_COM = { 1.35f, -0.55f, 0.10f};
        p.Canards[2].ForceApplicationPoint_COM = { 1.05f,  0.60f, 0.10f};
        p.Canards[3].ForceApplicationPoint_COM = { 1.05f, -0.60f, 0.10f};
        return p;
    }

    // A bare shell with no aero devices — only body drag/lift/side-force (a road car / the box placeholder).
    static AerodynamicPackage DefaultBody()
    {
        AerodynamicPackage p;
        p.RearWing.Enabled      = false;
        p.FrontSplitter.Enabled = false;
        p.FloorDiffuser.Enabled = false;
        p.LeftSideSkirt.Enabled = false;
        p.RightSideSkirt.Enabled= false;
        p.VortexGens.Enabled    = false;
        p.Canards.clear();
        return p;
    }
};

// ---------------------------------------------------------------------------------------------------------------------
// Computed forces (mirrors FAerodynamicForces_PT). Downforce values are POSITIVE magnitudes (push down).
// ---------------------------------------------------------------------------------------------------------------------
struct AeroForces
{
    float TotalDrag_N      = 0.0f;   // magnitude of DragForceWorld
    float TotalDownforce_N = 0.0f;   // Σ component downforce − body lift
    float FrontDownforce_N = 0.0f;   // apportioned to the front axle
    float RearDownforce_N  = 0.0f;   // apportioned to the rear axle
    float SideForce_N      = 0.0f;   // lateral (body-Y) force

    Vec3  DragForceWorld{};          // [N] world-space drag (opposes velocity) + body resistance
    Vec3  LiftForceWorld{};          // [N] world-space vertical (= −up·TotalDownforce)
    Vec3  SideForceWorld{};          // [N] world-space lateral

    float PitchMoment_Nm = 0.0f;     // about body-Y
    float RollMoment_Nm  = 0.0f;     // about body-X
    float YawMoment_Nm   = 0.0f;     // about body-Z

    // Component breakdown (telemetry / validation)
    float WingDownforce_N = 0.0f, WingDrag_N = 0.0f;
    float TotalCanardDownforce_N = 0.0f, TotalCanardDrag_N = 0.0f;
    float SplitterDownforce_N = 0.0f, SplitterDrag_N = 0.0f;
    float UnderbodyDownforce_N = 0.0f, UnderbodyDrag_N = 0.0f;
    float LeftSideSkirtDownforce_N = 0.0f, LeftSideSkirtDrag_N = 0.0f;
    float RightSideSkirtDownforce_N = 0.0f, RightSideSkirtDrag_N = 0.0f;
    float VortexGenDownforceBonus_N = 0.0f, VortexGenDrag_N = 0.0f;
    float BodyDrag_N = 0.0f, BodyLift_N = 0.0f;
    float UnderbodyRideHeight_m = 0.0f;
    float UnderbodyRideHeightFactor = 1.0f;
    float AeroEfficiency = 0.0f;     // L/D (downforce / drag)
};

// ---------------------------------------------------------------------------------------------------------------------
// The faithful port of AVehicleSolver::ComputeAerodynamicForces().
//   velocityWorld  — chassis CoM velocity [m/s], world
//   rideHeight_m   — effective underfloor ride height [m]
//   bodyForward/Right/Up — chassis basis vectors, world (unit)
//   brake/handbrake/throttle — [0..1]; yawRate_rads — chassis yaw rate [rad/s]
// ---------------------------------------------------------------------------------------------------------------------
[[nodiscard]] AeroForces ComputeAerodynamicForces(
    const AerodynamicPackage& aero,
    const Vec3& velocityWorld,
    float rideHeight_m,
    const Vec3& bodyForward,
    const Vec3& bodyRight,
    const Vec3& bodyUp,
    float brakeInput,
    float handbrakeInput,
    float throttleInput,
    float yawRate_rads) noexcept;

} // namespace Frontier::Vehicle

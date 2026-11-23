//============================================================================================================================
// 🧩 Frontier — VehicleSpecification.h
//============================================================================================================================
// 📝 Port of the HTML demonstration's VehicleSpecification.js, transcribed value-for-value: the ControlVehicle sockets
//    (from VehicleGeometry.h / ControlVehicle.blend), the GRIT strut with its derived digressive spring, anti-roll bars,
//    the Pacejka MF6.1 sheet, the GT-R engine/turbo/transmission sheets and the chassis sheet. Coordinates are the
//    authored model frame: X forward, Y left, Z up, origin 0.41715 m above the ground at rest.
//============================================================================================================================

#pragma once

#include <cmath>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONTROLVEHICLE SOCKETS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Wheel slots in authored order.
enum WheelSlot
{
    WheelFrontLeft  = 0,
    WheelFrontRight = 1,
    WheelRearLeft   = 2,
    WheelRearRight  = 3
};

// 📝 VehicleGeometry.h ControlVehicleSockets, model-origin frame [m]: axle (spindle) and strut-top mounts per wheel.
struct VehicleSockets
{
    double AxleMount[4][3]       = { { +1.7274, +1.0475, +0.0914 }, { +1.7274, -1.0475, +0.0914 },
                                     { -1.6686, +1.0475, +0.0914 }, { -1.6686, -1.0475, +0.0914 } };  // [m]
    double SuspensionMount[4][3] = { { +1.7274, +1.0475, +0.6436 }, { +1.7274, -1.0475, +0.6436 },
                                     { -1.6686, +1.0475, +0.6436 }, { -1.6686, -1.0475, +0.6436 } };  // [m]
};

// 📝 VehicleGeometry.h defaults: mass 1300 kg, centre of mass 0.53 m above ground, measured tyre and rim radii.
struct VehicleGeometry
{
    double Mass                = 1300.0;    // [kg]  incl. driver and fuel
    double CoMHeight           = 0.53;      // [m]   centre of mass above flat ground at rest
    double TyreRadius          = 0.50855;   // [m]   RubberFL Z/X span 1.0171 / 2
    double TyreRimRadius       = 0.34460;   // [m]   RimFL span 0.6892 / 2
    double TyreWidth           = 0.39580;   // [m]   RubberFL Y span
    double AuthoredRimRadius   = 0.344818;  // [m]   ControlWheelStructure kRimRadius
    double UnsprungMassPerWheel = 20.0;     // [kg]  carcass, hub, upright, brake, half strut
    double FrontWeightFraction = 0.4914;    // [-]   static front mass fraction
    double RollRadiusGyration  = 0.60;      // [m]   about body X
    double PitchRadiusGyration = 1.55;      // [m]   about body Y
    double YawRadiusGyration   = 1.60;      // [m]   about body Z
};

// 📐 The model origin is 0.41715 m above the ground: axle socket +0.0914 less the 0.50855 tyre radius.
inline double ResolveAuthoredGroundOffset(const VehicleGeometry& G, const VehicleSockets& S)
{
    return G.TyreRadius - S.AxleMount[0][2];
}

// 📐 Centre of mass expressed in the authored model frame (0.53 − 0.41715 = 0.11285 m), the body rebase.
inline double ResolveCoMAuthoredZ(const VehicleGeometry& G, const VehicleSockets& S)
{
    return G.CoMHeight - ResolveAuthoredGroundOffset(G, S);
}

inline double ResolveSprungMass(const VehicleGeometry& G)
{
    return std::fmax(1.0, G.Mass - 4.0 * G.UnsprungMassPerWheel);
}

// 📐 Body inertias [kg·m²] from the sprung mass and the radii of gyration (demo ResolveInertia).
inline void ResolveInertia(const VehicleGeometry& G, double OutInertia[3])
{
    const double Sprung = ResolveSprungMass(G);
    OutInertia[0] = Sprung * G.RollRadiusGyration * G.RollRadiusGyration;
    OutInertia[1] = Sprung * G.PitchRadiusGyration * G.PitchRadiusGyration;
    OutInertia[2] = Sprung * G.YawRadiusGyration * G.YawRadiusGyration;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SUSPENSION SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 GRIT's strut, carried over whole by SuspensionSpecification.h: 2.0 Hz, ζ 0.6, digressive spring with a 1.5
//    hardening factor over 2 cm bump + 8 cm droop, rate and damping derived from the corner mass, never authored.
struct SuspensionSpecification
{
    double NaturalFrequency  = 2.0;        // [Hz]
    double DampingRatio      = 0.6;        // [-]
    double SpringRate        = 0.0;        // [N/m]    derived
    double DampingRate       = 0.0;        // [N·s/m]  derived
    double MaxTravel         = 0.35;       // [m]
    double BumpTravel        = 0.02;       // [m]
    double DroopTravel       = 0.08;       // [m]
    double Preload           = 0.0;        // [N]      derived
    double StaticPreload     = 0.05;       // [m]
    double StaticCompression = 0.0;        // [m]      solved
    double FreeLength        = 0.0;        // [m]      solved from sockets
    double HardeningFactor   = 1.5;        // [-]
    double Coefficients[3]   = { 0.0, 0.0, 0.0 };  // k₁ [N/m], k₂ [N/m²], k₃ [N/m³]

    // 📐 ω = 2πf, k = mω², c = 2ζmω; preload is GRIT's 2 cm carried over as a compression, not as its 5000 N.
    void Calibrate(double SprungMass)
    {
        const double Omega = 2.0 * 3.141592653589793 * NaturalFrequency;                          // [rad/s]
        SpringRate  = SprungMass * Omega * Omega;                                                   // [N/m]
        DampingRate = 2.0 * DampingRatio * SprungMass * Omega;                                      // [N·s/m]
        Preload     = SpringRate * 0.02;                                                            // [N]
        const double Range = BumpTravel + DroopTravel;                                              // [m]
        Coefficients[0] = SpringRate * HardeningFactor;                                             // [N/m]
        Coefficients[1] = -(SpringRate * (HardeningFactor - 1.0)) / Range;                          // [N/m²]
        Coefficients[2] = 0.0;                                                                      // [N/m³]
    }

    // 📐 F(x) = ∫₀ˣ max(k(u), 0.1·SpringRate) du — the floored digressive cubic that cannot invert past its turn.
    double SpringForce(double Compression) const
    {
        const double k1 = Coefficients[0], k2 = Coefficients[1], k3 = Coefficients[2];
        const auto Cubic = [&](double x) { return k1 * x + k2 * x * x + k3 * x * x * x; };
        const double Floor = SpringRate * 0.1;
        const double Turn = RateFloorCompression();
        if (Turn <= 0.0 || Compression <= Turn)
            return Cubic(Compression);
        return Cubic(Turn) + Floor * (Compression - Turn);
    }

    double RateFloorCompression() const
    {
        const double k1 = Coefficients[0], k2 = Coefficients[1], k3 = Coefficients[2];
        const double a = 3.0 * k3, b = 2.0 * k2, c = k1 - SpringRate * 0.1;
        if (std::fabs(a) < 1.0e-6)
        {
            if (std::fabs(b) < 1.0e-6)
                return 0.0;
            const double Root = -c / b;
            return Root > 0.0 ? Root : 0.0;
        }
        const double Discriminant = b * b - 4.0 * a * c;
        if (Discriminant < 0.0)
            return 0.0;
        const double Root = std::sqrt(Discriminant);
        const double R1 = (-b - Root) / (2.0 * a), R2 = (-b + Root) / (2.0 * a);
        const double Low = std::fmin(R1, R2), High = std::fmax(R1, R2);
        return Low > 0.0 ? Low : (High > 0.0 ? High : 0.0);
    }

    // 📐 Bisects Preload + F(x) = Load; the closed form g/ω² is only the linear, zero-preload case.
    double StaticCompressionFor(double SupportedLoad) const
    {
        if (SupportedLoad <= Preload)
            return 0.0;
        double Low = 0.0, High = MaxTravel;
        for (int Iteration = 0; Iteration < 48; ++Iteration)
        {
            const double Middle = 0.5 * (Low + High);
            if (Preload + SpringForce(Middle) < SupportedLoad)
                Low = Middle;
            else
                High = Middle;
        }
        return 0.5 * (Low + High);
    }

    // 📝 The model is posed at rest, so the socket separation is the STATIC length; the free length adds the sag back.
    void ResolveFromSockets(double MountHeight, double AxleHeight, double SupportedLoad)
    {
        StaticCompression = StaticCompressionFor(SupportedLoad);
        FreeLength = (MountHeight - AxleHeight) + StaticCompression;
    }
};

struct SuspensionPair
{
    SuspensionSpecification Front;
    SuspensionSpecification Rear;
};

// 📝 Mirrors VehicleGeometry.cpp ResolveSuspension(): rate each axle for the sprung mass it carries.
inline SuspensionPair ResolveSuspensionPair(const VehicleGeometry& G, const VehicleSockets& S)
{
    SuspensionPair Out;
    const double Sprung = ResolveSprungMass(G);
    const double FrontCorner = 0.5 * Sprung * G.FrontWeightFraction;                 // [kg]
    const double RearCorner = 0.5 * Sprung * (1.0 - G.FrontWeightFraction);           // [kg]
    Out.Front.Calibrate(FrontCorner);
    Out.Rear.Calibrate(RearCorner);
    const double MountZ = S.SuspensionMount[0][2];                                    // [m]
    const double AxleZ = S.AxleMount[0][2];                                           // [m]
    Out.Front.ResolveFromSockets(MountZ, AxleZ, FrontCorner * 9.81);
    Out.Rear.ResolveFromSockets(MountZ, AxleZ, RearCorner * 9.81);
    return Out;
}

// 📝 Anti-roll bars coupling each axle's corners (demo AntiRollBars).
struct AntiRollBar
{
    double Stiffness  = 80000.0;   // [N/m]
    double Damping    = 1500.0;    // [N·s/m]
    int    LeftWheel  = 0;         // [-]
    int    RightWheel = 1;         // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  TYRE AND POWERTRAIN SHEETS
//------------------------------------------------------------------------------------------------------------------------

// 📝 PacejkaMagicFormula.h PacejkaParameters defaults (GRIT FTireSpecSheet), MF6.1.
struct PacejkaParameters
{
    double Fz0 = 5.0, Lx = 1.2, Ly = 1.3, Ltr = 1.0;
    double MuLoadSensitivityLong = 0.08, MuLoadSensitivityLat = 0.12, MuLoadMinFactor = 0.60;
    double pBx1 = 18.0, pBx2 = 20.0, pBx3 = 0.50, pCx1 = 1.55, pDx1 = 1.6, pDx2 = -0.03, pDx3 = 0.015;
    double pEx1 = -0.10, pEx2 = -0.15, pEx3 = 0.005, pEx4 = 0.030, pHx1 = 0.0, pHx2 = 0.0, pVx1 = 0.0, pVx2 = 0.0;
    double pBy1 = 18.0, pBy2 = 10.0, pBy3 = 0.10, pCy1 = 1.45, pDy1 = 1.85, pDy2 = -0.06, pDy3 = 0.10;
    double pEy1 = -1.2, pEy2 = -0.50, pEy3 = 0.20, pHy1 = 0.0, pHy2 = 0.0, pVy1 = 0.0, pVy2 = 0.0, pVy3 = 0.0, pVy4 = 0.0;
    double pCamber1 = 0.12, pCamber2 = -0.05;
    double rBx1 = 12.0, rBx2 = 8.0, rBx3 = 0.0, rCx1 = 1.0, rEx1 = 0.0, rEx2 = 0.0, rHx1 = 0.0;
    double rBy1 = 5.0, rBy2 = 4.0, rBy3 = 0.0, rBy4 = 0.0, rCy1 = 0.75, rEy1 = 0.0, rEy2 = 0.0, rHy1 = 0.0, rHy2 = 0.0;
    double rVy1 = 0.0, rVy2 = 0.0, rVy3 = 0.0, rVy4 = 0.0, rVy5 = 1.9, rVy6 = 0.0;
    double RelaxationLengthLong = 0.052, RelaxationLengthLat = 0.048;
    double RelaxationLoadExponentLong = 0.4, RelaxationLoadExponentLat = 0.45;
};

// 📝 Drivetrain.cpp EngineParameters::DefaultGTR() — 18-point wide-open-throttle curve, peak 520 N·m.
struct EngineSheet
{
    double TorqueCurve[18][2] = { { 800, 234 }, { 1200, 300 }, { 1600, 352 }, { 2000, 392 }, { 2400, 428 }, { 2800, 460 },
                                  { 3200, 486 }, { 3600, 504 }, { 4000, 514 }, { 4400, 518 }, { 4800, 520 }, { 5200, 520 },
                                  { 5800, 520 }, { 6200, 506 }, { 6600, 486 }, { 7000, 462 }, { 7600, 448 }, { 8500, 442 } };
    double IdleRPM                = 700.0;     // [rpm]
    double RedlineRPM             = 7200.0;    // [rpm]
    double EngineBrakingCoeff     = 0.08;      // [N·m·s/rad]
    double EngineBrakingQuadratic = 0.00012;   // [N·m·s²/rad²]
    double EngineInertia          = 0.38;      // [kg·m²]
};

// 📝 TurbochargerParameters::DefaultGTR() boost-to-torque multiplier, used through a first-order spool lag.
struct TurbochargerSheet
{
    double TorqueMultiplierCurve[4][2] = { { 0.0, 1.0 }, { 0.5, 1.3 }, { 1.0, 1.7 }, { 1.2, 1.85 } };
    double WastegateMaxBoost  = 1.2;    // [Bar]
    double SpoolTimeConstant  = 0.55;   // [s]  first-order stand-in for the shaft integration
};

// 📝 TransmissionParameters defaults: [0]=R2, [1]=R1, [2]=N, [3..8]=1st..6th; GT-R final drive.
struct TransmissionSheet
{
    double GearRatios[9]         = { -3.8, -2.0, 0.0, 4.2, 3.1, 2.5, 1.9, 1.5, 1.2 };
    double FinalDriveRatio       = 3.55;     // [-]
    double DriveEfficiency       = 0.92;     // [-]
    int    NeutralIndex          = 2;        // [-]
    int    ReverseIndex          = 1;        // [-]
    int    FirstIndex            = 3;        // [-]
    double UpshiftRPM            = 6800.0;   // [rpm]
    double DownshiftRPM          = 2600.0;   // [rpm]
    double ShiftCooldownSeconds  = 0.35;     // [s]
    double ClutchCapacity        = 1200.0;   // [N·m]
    double LaunchRPM             = 2600.0;   // [rpm] AMT slip target while the wheels cannot yet carry the engine
};

// 📝 VehicleSolverConfiguration defaults: brake and handbrake torque, steering lock and rate, wheel inertia.
struct ChassisSheet
{
    double MaxSteerAngleRad           = 0.52;     // [rad]
    double SteerRatePerSecond         = 6.0;      // [1/s]
    double MaxBrakeTorquePerWheel     = 2600.0;   // [N·m]
    double HandbrakeTorque            = 4200.0;   // [N·m]
    double RollingResistance          = 0.015;    // [-]
    double WheelInertia               = 1.2;      // [kg·m²]
    double SpinDampingCoefficient     = 1400.0;   // [N per m/s]
    double LateralDampingCoefficient  = 1600.0;   // [N per m/s]
    double ContactDampingFadeSpeed    = 6.0;      // [m/s]
    double DragArea                   = 0.74;     // [m²]  Cd·A of the GT3 package
    double LiftArea                   = 1.10;     // [m²]  Cl·A downforce, split by FrontLiftFraction
    double FrontLiftFraction          = 0.45;     // [-]
    double AirDensity                 = 1.225;    // [kg/m³]
};

// 📐 Piecewise-linear table lookup with end clamping (the demo's curve samplers).
inline double SampleCurve(const double Points[][2], int Count, double X)
{
    if (X <= Points[0][0])
        return Points[0][1];
    for (int Index = 1; Index < Count; ++Index)
    {
        if (X <= Points[Index][0])
        {
            const double T = (X - Points[Index - 1][0]) / (Points[Index][0] - Points[Index - 1][0]);
            return Points[Index - 1][1] + (Points[Index][1] - Points[Index - 1][1]) * T;
        }
    }
    return Points[Count - 1][1];
}

} // namespace Frontier

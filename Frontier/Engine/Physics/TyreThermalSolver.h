//============================================================================================================================
// 🧩 Frontier — TyreThermalSolver.h
//============================================================================================================================
// 📝 Per-tyre heat, gas law, grip, wear and failure. Port of the HTML demonstration's TyreThermalSolver.js dynamics core
//    (ThermalIntegrator's 3-node backward-Euler conduction tread ↔ carcass ↔ gas, ideal-gas pressure from air mass and
//    gas temperature, temperature/pressure grip factor, Archard-style abrasion with pyrolysis heat loss, ignition and
//    the three failure modes). The demo's smoke-source classification (emission bookkeeping for its gas lattice) is
//    visual-side and is not ported; everything that feeds back into dynamics is. Sliding heat is the actual contact
//    dissipation |Fx·Vsx| + |Fy·Vsy| the vehicle solver integrates across its substeps; static support forces and
//    impacts never heat the tyre.
//============================================================================================================================

#pragma once

namespace Frontier
{

// 📝 ThermalSpecification from the demo (Frontier's ThermalIntegrator.h values).
struct TyreThermalSpecification
{
    double AmbientTemperature        = 293.15;      // [K]
    double RoadTemperature           = 293.15;      // [K]
    double SurfaceHeatCapacity       = 1800.0;      // [J/K]   fast tread layer
    double CarcassHeatCapacity       = 14400.0;     // [J/K]   bulk rubber
    double SurfaceCarcassConductance = 28.0;        // [W/K]
    double CarcassGasConductance     = 3.0;         // [W/K]
    double SurfaceAirConductance     = 12.0;        // [W/K]   still air
    double CarcassAirConductance     = 3.0;         // [W/K]
    double RoadConductance           = 18.0;        // [W/K]   contact cooling
    double SlidingHeatShare          = 0.75;        // [-]     sliding work entering rubber; the rest heats the road
    double AirSpecificGasConstant    = 287.05;      // [J/kg/K]
    double AirSpecificHeat           = 718.0;       // [J/kg/K]
    double OptimumTemperature        = 353.15;      // [K]     grip plateau end
    double GripFadeTemperature       = 473.15;      // [K]     grip reaches its floor
    double MinimumGripFactor         = 0.60;        // [-]
    double PressureGripSensitivity   = 0.30;        // [-]     per fractional deviation from cold pressure
    double CombustibleMass           = 2.0;         // [kg]
    double BurnRate                  = 0.002;       // [kg/s]
    double HeatOfCombustion          = 30000000.0;  // [J/kg]  authored heat release, not chemical validation
    double CombustionHeatShare       = 0.10;        // [-]     burn heat entering the tyre; the rest feeds the plume
    double RestVolume                = 0.034;       // [m³]    enclosed air of the authored tyre
    double ColdGaugePressure         = 220000.0;    // [Pa]
    double AtmosphericPressure       = 101325.0;    // [Pa]
};

// 📝 Wear and failure (the demo's AbrasionSpecification).
struct TyreAbrasionSpecification
{
    double TreadMass       = 1.1;        // [kg]    wearable tread rubber per tyre
    double WearCoefficient = 6.0e-8;     // [kg/J]  tread worn per joule of sliding work at the optimum temperature
    double WearHeatSpan    = 80.0;       // [K]     each span above the optimum adds the cold wear rate again
    double VapourShare     = 0.55;       // [-]     worn rubber that leaves as smoke rather than as crumbs
    double PyrolysisHeat   = 1.6e6;      // [J/kg]  carried away by the vapourised share
    double BlowoutRate     = 30.0;       // [1/s]   the air's escape when the tyre lets go
    double PunctureRate    = 0.035;      // [1/s]   a nail: half the pressure in about twenty seconds
    double CarcassFailure  = 493.15;     // [K]     220 °C bulk rubber: the cords' bond gives way
    double BurstPressure   = 650000.0;   // [Pa]    gauge; ~3× a road tyre's rating
};

// 📝 Ignition gate (ThermalIntegrator.h defaults): authored 623.15 K held 4 s; the labelled diagnostic setting lights
//    under any sustained drift; Off disables combustion.
struct TyreIgnitionMode
{
    bool   Enabled    = true;     // [-]
    double Ignition   = 623.15;   // [K]
    double Exposure   = 4.0;      // [s]
    double Extinction = 473.15;   // [K]
};

enum TyreBurstCause
{
    TyreBurstNone     = 0,
    TyreBurstWorn     = 1,   // tread worn through
    TyreBurstCooked   = 2,   // carcass overheated
    TyreBurstPressure = 3,   // over-pressure
    TyreBurstBlowout  = 4,   // blowout
    TyreBurstPuncture = 5,   // puncture
    TyreBurstPinch    = 6    // pinch cut
};

// 📝 Per-wheel readings the vehicle solver hands over once per advance.
struct TyreThermalReading
{
    double SlidingPower    = 0.0;   // [W]     mean sliding dissipation over the interval
    double ContactFraction = 0.0;   // [-]     share of the interval in contact
    double Airspeed        = 0.0;   // [m/s]
    bool   Shredded        = false; // [-]     a bare rim neither heats nor fails thermally
};

// 📝 Burst events, drained once each.
struct TyreBurstEvent
{
    int  Wheel   = -1;              // [-]
    int  Cause   = TyreBurstNone;   // [-] TyreBurstCause
    bool Blowout = false;           // [-]
};

struct TyreThermalWheel
{
    double Surface         = 293.15;   // [K]     tread surface node
    double Carcass         = 293.15;   // [K]     bulk rubber node
    double Gas             = 293.15;   // [K]     enclosed air node
    double AirMass         = 0.0;      // [kg]
    double GaugePressure   = 220000.0; // [Pa]
    double Grip            = 1.0;      // [-]
    double SlidingPower    = 0.0;      // [W]
    double CombustionPower = 0.0;      // [W]
    double RemainingRubber = 2.0;      // [kg]    combustible mass left
    double OverheatExposure = 0.0;     // [s]
    bool   Ignited         = false;    // [-]
    double TreadMass       = 1.1;      // [kg]
    double WearRate        = 0.0;      // [kg/s]
    double VapourRate      = 0.0;      // [kg/s]
    double LeakRate        = 0.0;      // [1/s]
    bool   Burst           = false;    // [-]
    int    Cause           = TyreBurstNone;  // [-]
};

class TyreThermalSolver
{
public:
    explicit TyreThermalSolver(int WheelCount = 4);

    // 📝 Fresh, cold tyres at the set cold pressures.
    void Reset();

    // 📝 The tyre bay: cold pressures [Pa gauge] per wheel; every intact tyre is pumped to them now at its present gas
    //    temperature and any slow leak is sealed. A burst tyre needs a fresh one (Reset).
    void Inflate(const double* Pressures);

    // 📝 A nail (a slow leak) or a blowout in one tyre; Cause names it (TyreBurstCause).
    bool Puncture(int Index, bool Blowout, int Cause);

    // 📐 Combustion gate, then the 3×3 backward-Euler conduction system (tread ↔ carcass ↔ gas, tread → air and road,
    //    carcass → air) solved by Gauss–Jordan elimination; then leaks, the gas law, failure checks and the grip factor.
    void Advance(const TyreThermalReading* Readings, double Seconds, const TyreIgnitionMode& Mode);

    // 📝 The frame's burst events (once each). Returns the count written (at most Capacity).
    int DrainEvents(TyreBurstEvent* OutEvents, int Capacity);

    bool QueryBurning() const;
    double QueryTreadFraction(int Index) const;

    TyreThermalWheel Wheels[8];         // [-] indexed by wheel (WheelCount in use)
    int WheelCount = 4;                 // [-]
    double ColdPressures[8];            // [Pa] gauge set-points
    bool WearEnabled = true;            // [-]
    double WearScale = 1.0;             // [-]
    bool FailureEnabled = true;         // [-]

private:
    TyreBurstEvent m_Events[16];
    int m_EventCount = 0;               // [-]
};

} // namespace Frontier

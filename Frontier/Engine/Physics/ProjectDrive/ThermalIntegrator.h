//============================================================================================================================================
//                                                            THERMALINTEGRATOR.H
//============================================================================================================================================
// 📦 Per-wheel tread, carcass and enclosed-gas heat transfer with sustained-overheat combustion and conditioned grip.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Frontier::Vehicle
{

struct ThermalSpecification
{
    bool   Enabled                   = false;        // [-]     - preserve the existing isothermal tyre when disabled
    bool   IgnitionEnabled           = false;        // [-]     - authored overheating may ignite rubber
    double AmbientTemperature        = 293.15;       // [K]     - air and initial tyre temperature
    double RoadTemperature           = 293.15;       // [K]     - road heat reservoir
    double SurfaceHeatCapacity       = 1800.0;       // [J/K]   - fast tread heat capacity
    double CarcassHeatCapacity       = 14400.0;      // [J/K]   - slower bulk rubber heat capacity
    double SurfaceCarcassConductance = 28.0;         // [W/K]   - tread to bulk rubber
    double CarcassGasConductance     = 3.0;          // [W/K]   - rubber to enclosed air
    double SurfaceAirConductance     = 12.0;         // [W/K]   - stationary air cooling
    double CarcassAirConductance     = 3.0;          // [W/K]   - stationary bulk cooling
    double RoadConductance           = 18.0;         // [W/K]   - contact cooling
    double SlidingHeatShare          = 0.75;         // [-]     - sliding work entering rubber, remainder entering road
    double AirSpecificGasConstant    = 287.05;       // [J/kg/K]- dry-air gas constant
    double AirSpecificHeat           = 718.0;        // [J/kg/K]- constant-volume heat capacity
    double OptimumTemperature        = 353.15;       // [K]     - plateau end; no cold-force retune
    double GripFadeTemperature       = 473.15;       // [K]     - hot-force multiplier reaches its authored floor
    double MinimumGripFactor         = 0.60;         // [-]     - hot tyre grip floor
    double PressureGripSensitivity   = 0.30;         // [-]     - penalty per fractional cold-reference deviation
    double IgnitionTemperature       = 623.15;       // [K]     - sustained threshold, compound calibration pending
    double IgnitionExposureSeconds   = 4.0;          // [s]     - threshold exposure before ignition
    double ExtinctionTemperature     = 473.15;       // [K]     - burning rubber must remain hot
    double CombustibleMass           = 2.0;          // [kg]    - rubber available to the bounded combustion reading
    double BurnRate                  = 0.002;        // [kg/s]  - burning mass rate
    double HeatOfCombustion          = 30000000.0;   // [J/kg] - authored heat release, not chemical validation
    double CombustionHeatShare       = 0.10;         // [-]     - burn heat entering tyre; rest enters the plume
};

struct ThermalContact
{
    double SlidingPower = 0.0;     // [W]   - final dissipated tangential work, never normal impact work
    double Airspeed     = 0.0;     // [m/s] - tyre travel relative to ambient air
    double Volume       = 0.0;     // [m³]  - gas volume this step
    bool   InContact    = false;   // [-]   - ground cooling admitted only while touching road
};

struct ThermalMetrics
{
    double SurfaceTemperature     = 293.15;     // [K]   - tread temperature
    double CarcassTemperature     = 293.15;     // [K]   - bulk rubber temperature
    double GasTemperature         = 293.15;     // [K]   - internal air temperature, never instant tread temperature
    double ReferenceTemperature   = 293.15;     // [K]   - cold inflation temperature
    double ReferenceVolume        = 0.0;        // [m³]  - initial enclosed volume
    double PreviousVolume         = 0.0;        // [m³]  - completed previous geometry
    double ReferenceGaugePressure = 0.0;        // [Pa]  - authored cold gauge pressure
    double AtmosphericPressure    = 101325.0;   // [Pa] - ambient absolute pressure
    double AirMass                = 0.0;        // [kg]  - constant until a separate leak mechanism is introduced
    double GaugePressure          = 0.0;        // [Pa]  - current thermal/volume pressure
    double GripFactor             = 1.0;        // [-]   - one wheel's condition multiplier
    double SlidingPower           = 0.0;        // [W]   - nonnegative final contact dissipation
    double SlidingHeatJoules      = 0.0;        // [J]   - accumulated heat admitted to rubber
    double AmbientHeatJoules      = 0.0;        // [J]   - signed energy transferred to ambient/road
    double CompressionWorkJoules  = 0.0;        // [J]   - adiabatic gas compression/expansion work
    double CombustionHeatJoules   = 0.0;        // [J]   - burn heat admitted to tyre
    double CombustionPower        = 0.0;        // [W]   - total instantaneous combustion heat release
    double RemainingRubber        = 0.0;        // [kg]  - remaining combustible material
    double OverheatExposure       = 0.0;        // [s]   - continuous hot exposure
    double StoredHeatJoules       = 0.0;        // [J]   - heat relative to cold initial conditions
    double EnergyResidualJoules   = 0.0;        // [J]   - thermal balance only, not whole-vehicle conservation
    bool   Ignited                = false;      // [-]   - distinct from a gas domain merely emitting smoke
    bool   Valid                  = false;      // [-]   - refused readings never replace a valid temperature
};

/// 📦 Refuses nonfinite or nonphysical thermal tuning before it can change a wheel reading.
/// out   bool [-] all temperatures/capacities positive, transfer rates nonnegative and shares bounded
inline bool ResolveThermalSpecification(const ThermalSpecification& Specification) noexcept
{
    const double Positive[] = {Specification.AmbientTemperature, Specification.RoadTemperature,
        Specification.SurfaceHeatCapacity, Specification.CarcassHeatCapacity, Specification.AirSpecificGasConstant,
        Specification.AirSpecificHeat, Specification.OptimumTemperature, Specification.GripFadeTemperature,
        Specification.IgnitionTemperature, Specification.ExtinctionTemperature,
        Specification.IgnitionExposureSeconds};
    for (double Reading : Positive) if (!std::isfinite(Reading) || Reading <= 0.0) return false;
    const double Nonnegative[] = {Specification.SurfaceCarcassConductance, Specification.CarcassGasConductance,
        Specification.SurfaceAirConductance, Specification.CarcassAirConductance, Specification.RoadConductance,
        Specification.PressureGripSensitivity, Specification.CombustibleMass, Specification.BurnRate,
        Specification.HeatOfCombustion};
    for (double Reading : Nonnegative) if (!std::isfinite(Reading) || Reading < 0.0) return false;
    const double Shares[] = {Specification.SlidingHeatShare, Specification.CombustionHeatShare, Specification.MinimumGripFactor};
    for (double Share : Shares) if (!std::isfinite(Share) || Share < 0.0 || Share > 1.0) return false;
    return Specification.GripFadeTemperature > Specification.OptimumTemperature &&
           Specification.IgnitionTemperature > Specification.ExtinctionTemperature;
}

/// 📦 Constructs independent cold inflation and thermal readings for one wheel.
/// in    Specification       [-]  thermal tuning; capacities must be positive
/// in    ReferenceVolume     [m³] cold tyre air volume (TyreSpecification::RestVolume)
/// in    GaugePressure       [Pa] cold gauge pressure
/// in    AtmosphericPressure [Pa] ambient absolute pressure
/// out   Reading             [-] independent temperatures, air mass and cold pressure
/// err   false for invalid capacities/reference conditions; the previous reading is unchanged
/// note  binary64 heat arithmetic; published vehicle telemetry may round to binary32
inline bool ConstructThermal(
    ThermalMetrics&             Reading,
    const ThermalSpecification& Specification,
    double                      ReferenceVolume,
    double                      GaugePressure,
    double                      AtmosphericPressure) noexcept
{
    if (!ResolveThermalSpecification(Specification) || !std::isfinite(ReferenceVolume) || ReferenceVolume <= 0.0 ||
        !std::isfinite(GaugePressure) || GaugePressure < 0.0 ||
        !std::isfinite(AtmosphericPressure) || AtmosphericPressure <= 0.0 ||
        !std::isfinite(Specification.AmbientTemperature) || Specification.AmbientTemperature <= 0.0 ||
        Specification.SurfaceHeatCapacity <= 0.0 || Specification.CarcassHeatCapacity <= 0.0 ||
        Specification.AirSpecificHeat <= 0.0 || Specification.AirSpecificGasConstant <= 0.0) return false;
    ThermalMetrics Candidate;
    Candidate.SurfaceTemperature = Candidate.CarcassTemperature = Candidate.GasTemperature = Specification.AmbientTemperature;
    Candidate.ReferenceTemperature = Specification.AmbientTemperature;
    Candidate.ReferenceVolume = Candidate.PreviousVolume = ReferenceVolume;
    Candidate.ReferenceGaugePressure = Candidate.GaugePressure = GaugePressure;
    Candidate.AtmosphericPressure = AtmosphericPressure;
    Candidate.AirMass = (GaugePressure + AtmosphericPressure) * ReferenceVolume /
                        (Specification.AirSpecificGasConstant * Specification.AmbientTemperature);
    Candidate.RemainingRubber = std::max(0.0, Specification.CombustibleMass);
    Candidate.Valid = std::isfinite(Candidate.AirMass) && Candidate.AirMass > 0.0;
    if (!Candidate.Valid) return false;
    Reading = Candidate;
    return true;
}

/// 📦 Advances three coupled heat capacities with an implicit conductive solve and volume-dependent gas work.
/// in    Contact       [-] final dissipated sliding work and completed volume
/// in    Seconds       [s] completed fixed physics interval
/// out   Reading       [-] independent wheel heat, pressure, combustion and balance measurements
/// err   false for nonfinite input, invalid volume or invalid temperature; prior reading remains unchanged
/// note  tuning is a first bounded approximation; ignition thresholds are not measured compound properties
inline bool IntegrateThermal(
    ThermalMetrics&             Reading,
    const ThermalSpecification& Specification,
    const ThermalContact&       Contact,
    double                      Seconds) noexcept
{
    if (!ResolveThermalSpecification(Specification) || !Reading.Valid || !std::isfinite(Seconds) || Seconds <= 0.0 ||
        !std::isfinite(Contact.SlidingPower) || !std::isfinite(Contact.Airspeed) ||
        !std::isfinite(Contact.Volume) || Contact.Volume <= 0.0) return false;
    ThermalMetrics Candidate = Reading;
    Candidate.SlidingPower = Contact.InContact ? std::max(0.0, Contact.SlidingPower) : 0.0;
    Candidate.CombustionPower = 0.0;
    const double GasCapacity = Candidate.AirMass * Specification.AirSpecificHeat;
    const double Capacity[3] = {Specification.SurfaceHeatCapacity, Specification.CarcassHeatCapacity, GasCapacity};
    const double Initial[3] = {Reading.SurfaceTemperature, Reading.CarcassTemperature, Reading.GasTemperature};
    double Temperatures[3] = {Initial[0], Initial[1], Initial[2]};
    if (Specification.Enabled)
    {
        // 📝 Adiabatic volume work precedes conduction; no heat is borrowed from brake discs or normal impacts.
        const double γ = 1.0 + Specification.AirSpecificGasConstant / Specification.AirSpecificHeat;
        Temperatures[2] *= std::pow(Candidate.PreviousVolume / Contact.Volume, γ - 1.0);
        Candidate.CompressionWorkJoules += GasCapacity * (Temperatures[2] - Initial[2]);
        if (Specification.IgnitionEnabled && Temperatures[0] >= Specification.IgnitionTemperature)
            Candidate.OverheatExposure += Seconds;
        else Candidate.OverheatExposure = 0.0;
        if (Specification.IgnitionEnabled && Candidate.RemainingRubber > 0.0 &&
            Candidate.OverheatExposure >= Specification.IgnitionExposureSeconds) Candidate.Ignited = true;
        if (!Specification.IgnitionEnabled || Candidate.RemainingRubber <= 0.0 ||
            Temperatures[0] < Specification.ExtinctionTemperature) Candidate.Ignited = false;
        if (Candidate.Ignited)
        {
            const double Burned = std::min(Candidate.RemainingRubber, std::max(0.0, Specification.BurnRate) * Seconds);
            Candidate.RemainingRubber -= Burned;
            Candidate.CombustionPower = Burned * std::max(0.0, Specification.HeatOfCombustion) / Seconds;
        }
        const double SlidingHeat = Candidate.SlidingPower * std::clamp(Specification.SlidingHeatShare, 0.0, 1.0) * Seconds;
        const double BurnHeat = Candidate.CombustionPower * std::clamp(Specification.CombustionHeatShare, 0.0, 1.0) * Seconds;
        Candidate.SlidingHeatJoules += SlidingHeat;
        Candidate.CombustionHeatJoules += BurnHeat;
        const double AirFactor = 1.0 + 0.35 * std::sqrt(std::max(0.0, Contact.Airspeed));
        const double AirSurface = std::max(0.0, Specification.SurfaceAirConductance) * AirFactor;
        const double AirCarcass = std::max(0.0, Specification.CarcassAirConductance) * AirFactor;
        const double Road = Contact.InContact ? std::max(0.0, Specification.RoadConductance) : 0.0;
        const double SurfaceCarcass = std::max(0.0, Specification.SurfaceCarcassConductance);
        const double CarcassGas = std::max(0.0, Specification.CarcassGasConductance);
        double Equations[3][4] = {
            {Capacity[0] / Seconds + AirSurface + Road + SurfaceCarcass, -SurfaceCarcass, 0.0,
                Capacity[0] * Temperatures[0] / Seconds + (SlidingHeat + BurnHeat) / Seconds +
                AirSurface * Specification.AmbientTemperature + Road * Specification.RoadTemperature},
            {-SurfaceCarcass, Capacity[1] / Seconds + AirCarcass + SurfaceCarcass + CarcassGas, -CarcassGas,
                Capacity[1] * Temperatures[1] / Seconds + AirCarcass * Specification.AmbientTemperature},
            {0.0, -CarcassGas, Capacity[2] / Seconds + CarcassGas, Capacity[2] * Temperatures[2] / Seconds}
        };
        for (uint32_t Pivot = 0u; Pivot < 3u; ++Pivot)
        {
            const double Diagonal = Equations[Pivot][Pivot];
            if (!std::isfinite(Diagonal) || Diagonal <= 0.0) return false;
            for (uint32_t Column = Pivot; Column < 4u; ++Column) Equations[Pivot][Column] /= Diagonal;
            for (uint32_t Row = 0u; Row < 3u; ++Row)
            {
                if (Row == Pivot) continue;
                const double Factor = Equations[Row][Pivot];
                for (uint32_t Column = Pivot; Column < 4u; ++Column)
                    Equations[Row][Column] -= Factor * Equations[Pivot][Column];
            }
        }
        for (uint32_t Row = 0u; Row < 3u; ++Row) Temperatures[Row] = Equations[Row][3];
        Candidate.AmbientHeatJoules += Seconds * (AirSurface * (Temperatures[0] - Specification.AmbientTemperature) +
            Road * (Temperatures[0] - Specification.RoadTemperature) + AirCarcass * (Temperatures[1] - Specification.AmbientTemperature));
    }
    for (double Temperature : Temperatures) if (!std::isfinite(Temperature) || Temperature <= 0.0) return false;
    Candidate.SurfaceTemperature = Temperatures[0];
    Candidate.CarcassTemperature = Temperatures[1];
    Candidate.GasTemperature = Temperatures[2];
    Candidate.PreviousVolume = Contact.Volume;
    Candidate.GaugePressure = std::max(0.0, Candidate.AirMass * Specification.AirSpecificGasConstant *
                                              Candidate.GasTemperature / Contact.Volume - Candidate.AtmosphericPressure);
    Candidate.StoredHeatJoules = 0.0;
    for (uint32_t Index = 0u; Index < 3u; ++Index)
        Candidate.StoredHeatJoules += Capacity[Index] * (Temperatures[Index] - Candidate.ReferenceTemperature);
    Candidate.EnergyResidualJoules = Candidate.StoredHeatJoules - Candidate.SlidingHeatJoules -
        Candidate.CompressionWorkJoules - Candidate.CombustionHeatJoules + Candidate.AmbientHeatJoules;
    Candidate.GripFactor = 1.0;
    if (Specification.Enabled)
    {
        const double Hot = std::clamp((Candidate.SurfaceTemperature - Specification.OptimumTemperature) /
            std::max(1.0, Specification.GripFadeTemperature - Specification.OptimumTemperature), 0.0, 1.0);
        const double PressureDeviation = std::abs(Candidate.GaugePressure - Candidate.ReferenceGaugePressure) /
                                         std::max(1000.0, Candidate.ReferenceGaugePressure);
        Candidate.GripFactor = (1.0 - Hot * (1.0 - std::clamp(Specification.MinimumGripFactor, 0.0, 1.0))) *
            std::max(0.25, 1.0 - std::max(0.0, Specification.PressureGripSensitivity) * PressureDeviation);
    }
    if (!std::isfinite(Candidate.GaugePressure) || !std::isfinite(Candidate.GripFactor)) return false;
    Reading = Candidate;
    return true;
}

} // namespace Frontier::Vehicle

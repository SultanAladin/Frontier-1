//============================================================================================================================
// 🧩 Frontier — TyreThermalSolver.cpp
//============================================================================================================================
// 📝 The demonstration's TyreThermalSolver Advance() transcribed line-for-line (emission/smoke classification excluded,
//    see the header). Gauss–Jordan on the 3-node backward-Euler conduction system, ideal-gas pressure, abrasion with
//    pyrolysis heat loss, ignition, three failure modes, and the temperature/pressure grip factor.
//============================================================================================================================

#include "Engine/Physics/TyreThermalSolver.h"

#include <cmath>

namespace Frontier
{

namespace
{
    const TyreThermalSpecification S;
    const TyreAbrasionSpecification Abrasion;

    // 📐 Air mass [kg] that sits at Gauge [Pa] in the rest volume at Temperature [K].
    double ResolveAirMass(double Gauge, double Temperature)
    {
        return (Gauge + S.AtmosphericPressure) * S.RestVolume / (S.AirSpecificGasConstant * Temperature);
    }
}

TyreThermalSolver::TyreThermalSolver(int Count)
    : WheelCount(Count)
{
    for (int Index = 0; Index < 8; ++Index)
        ColdPressures[Index] = S.ColdGaugePressure;
    Reset();
}

void TyreThermalSolver::Reset()
{
    for (int Index = 0; Index < WheelCount; ++Index)
    {
        TyreThermalWheel& Wheel = Wheels[Index];
        Wheel.Surface = S.AmbientTemperature;
        Wheel.Carcass = S.AmbientTemperature;
        Wheel.Gas = S.AmbientTemperature;
        Wheel.AirMass = ResolveAirMass(ColdPressures[Index], S.AmbientTemperature);
        Wheel.GaugePressure = ColdPressures[Index];
        Wheel.Grip = 1.0;
        Wheel.SlidingPower = 0.0;
        Wheel.CombustionPower = 0.0;
        Wheel.RemainingRubber = S.CombustibleMass;
        Wheel.OverheatExposure = 0.0;
        Wheel.Ignited = false;
        Wheel.TreadMass = Abrasion.TreadMass;
        Wheel.WearRate = 0.0;
        Wheel.VapourRate = 0.0;
        Wheel.LeakRate = 0.0;
        Wheel.Burst = false;
        Wheel.Cause = TyreBurstNone;
    }
    m_EventCount = 0;
}

void TyreThermalSolver::Inflate(const double* Pressures)
{
    for (int Index = 0; Index < WheelCount; ++Index)
    {
        ColdPressures[Index] = Pressures[Index];
        TyreThermalWheel& Wheel = Wheels[Index];
        if (Wheel.Burst)
            continue;
        Wheel.LeakRate = 0.0;
        Wheel.Cause = TyreBurstNone;
        Wheel.AirMass = ResolveAirMass(ColdPressures[Index], Wheel.Gas);
        Wheel.GaugePressure = ColdPressures[Index];
    }
}

bool TyreThermalSolver::Puncture(int Index, bool Blowout, int Cause)
{
    if (Index < 0 || Index >= WheelCount)
        return false;
    TyreThermalWheel& Wheel = Wheels[Index];
    if (Wheel.Burst)
        return false;
    const double Rate = Blowout ? Abrasion.BlowoutRate : Abrasion.PunctureRate;
    Wheel.LeakRate = std::fmax(Wheel.LeakRate, Rate);
    Wheel.Cause = Cause;
    if (Blowout)
    {
        Wheel.Burst = true;
        if (m_EventCount < 16)
        {
            m_Events[m_EventCount].Wheel = Index;
            m_Events[m_EventCount].Cause = Cause;
            m_Events[m_EventCount].Blowout = true;
            ++m_EventCount;
        }
    }
    return true;
}

void TyreThermalSolver::Advance(const TyreThermalReading* Readings, double Seconds, const TyreIgnitionMode& Mode)
{
    if (!(Seconds > 0.0))
        return;
    for (int Index = 0; Index < WheelCount; ++Index)
    {
        TyreThermalWheel& Wheel = Wheels[Index];
        const TyreThermalReading& Reading = Readings[Index];
        const bool InContact = Reading.ContactFraction > 0.5;
        Wheel.SlidingPower = InContact ? std::fmax(0.0, Reading.SlidingPower) : 0.0;
        Wheel.CombustionPower = 0.0;
        if (Mode.Enabled && Wheel.Surface >= Mode.Ignition)
            Wheel.OverheatExposure += Seconds;
        else
            Wheel.OverheatExposure = 0.0;
        if (Mode.Enabled && Wheel.RemainingRubber > 0.0 && Wheel.OverheatExposure >= Mode.Exposure)
            Wheel.Ignited = true;
        if (!Mode.Enabled || Wheel.RemainingRubber <= 0.0 || Wheel.Surface < Mode.Extinction)
            Wheel.Ignited = false;
        if (Wheel.Ignited)
        {
            const double Burned = std::fmin(Wheel.RemainingRubber, S.BurnRate * Seconds);
            Wheel.RemainingRubber -= Burned;
            Wheel.CombustionPower = Burned * S.HeatOfCombustion / Seconds;
        }
        // 📝 Abrasion: the tread wears with the sliding work, hotter rubber faster; the vapourised share leaves as
        //    smoke and takes its pyrolysis heat with it. A bare rim has no rubber left to heat or wear.
        if (Reading.Shredded)
            Wheel.SlidingPower = 0.0;
        const double HeatFactor = 1.0 + std::fmax(0.0, Wheel.Surface - S.OptimumTemperature) / Abrasion.WearHeatSpan;
        Wheel.WearRate = (WearEnabled && !Wheel.Burst && Wheel.TreadMass > 0.0)
                       ? Abrasion.WearCoefficient * WearScale * Wheel.SlidingPower * HeatFactor : 0.0;
        const double Worn = std::fmin(Wheel.TreadMass, Wheel.WearRate * Seconds);
        Wheel.TreadMass -= Worn;
        Wheel.VapourRate = Worn * Abrasion.VapourShare / Seconds;
        const double VapourHeat = Worn * Abrasion.VapourShare * Abrasion.PyrolysisHeat;
        const double SlidingHeat = std::fmax(0.0, Wheel.SlidingPower * S.SlidingHeatShare * Seconds - VapourHeat);
        const double BurnHeat = Wheel.CombustionPower * S.CombustionHeatShare * Seconds;
        const double AirFactor = 1.0 + 0.35 * std::sqrt(std::fmax(0.0, Reading.Airspeed));
        const double AirSurface = S.SurfaceAirConductance * AirFactor;
        const double AirCarcass = S.CarcassAirConductance * AirFactor;
        const double Road = InContact ? S.RoadConductance : 0.0;
        const double Capacity[3] = { S.SurfaceHeatCapacity, S.CarcassHeatCapacity, Wheel.AirMass * S.AirSpecificHeat };
        double Equations[3][4] = {
            { Capacity[0] / Seconds + AirSurface + Road + S.SurfaceCarcassConductance, -S.SurfaceCarcassConductance, 0.0,
              Capacity[0] * Wheel.Surface / Seconds + (SlidingHeat + BurnHeat) / Seconds + AirSurface * S.AmbientTemperature
              + Road * S.RoadTemperature },
            { -S.SurfaceCarcassConductance, Capacity[1] / Seconds + AirCarcass + S.SurfaceCarcassConductance + S.CarcassGasConductance,
              -S.CarcassGasConductance, Capacity[1] * Wheel.Carcass / Seconds + AirCarcass * S.AmbientTemperature },
            { 0.0, -S.CarcassGasConductance, Capacity[2] / Seconds + S.CarcassGasConductance, Capacity[2] * Wheel.Gas / Seconds }
        };
        for (int Pivot = 0; Pivot < 3; ++Pivot)
        {
            const double Diagonal = Equations[Pivot][Pivot];
            for (int Column = Pivot; Column < 4; ++Column)
                Equations[Pivot][Column] /= Diagonal;
            for (int Row = 0; Row < 3; ++Row)
            {
                if (Row == Pivot)
                    continue;
                const double Factor = Equations[Row][Pivot];
                for (int Column = Pivot; Column < 4; ++Column)
                    Equations[Row][Column] -= Factor * Equations[Pivot][Column];
            }
        }
        Wheel.Surface = Equations[0][3];
        Wheel.Carcass = Equations[1][3];
        Wheel.Gas = Equations[2][3];
        // 📝 A leak bleeds the air toward the mass that would sit at atmospheric pressure at this gas temperature.
        if (Wheel.LeakRate > 0.0)
        {
            const double Settled = ResolveAirMass(0.0, Wheel.Gas);
            Wheel.AirMass = Settled + (Wheel.AirMass - Settled) * std::exp(-Wheel.LeakRate * Seconds);
        }
        Wheel.GaugePressure = std::fmax(0.0, Wheel.AirMass * S.AirSpecificGasConstant * Wheel.Gas / S.RestVolume
                                            - S.AtmosphericPressure);
        // 📝 Failure: worn through, cooked, or over-pressured — each a blowout, reported once.
        if (FailureEnabled && !Wheel.Burst && !Reading.Shredded)
        {
            const int Cause = (WearEnabled && Wheel.TreadMass <= 0.0) ? TyreBurstWorn
                            : Wheel.Carcass >= Abrasion.CarcassFailure ? TyreBurstCooked
                            : Wheel.GaugePressure >= Abrasion.BurstPressure ? TyreBurstPressure : TyreBurstNone;
            if (Cause != TyreBurstNone)
                Puncture(Index, true, Cause);
        }
        const double Hot = std::fmin(1.0, std::fmax(0.0, (Wheel.Surface - S.OptimumTemperature)
                                                  / std::fmax(1.0, S.GripFadeTemperature - S.OptimumTemperature)));
        const double Reference = std::fmax(1000.0, ColdPressures[Index]);
        const double Deviation = std::fmin(1.0, std::fabs(Wheel.GaugePressure - Reference) / Reference);
        Wheel.Grip = (1.0 - Hot * (1.0 - S.MinimumGripFactor)) * std::fmax(0.25, 1.0 - S.PressureGripSensitivity * Deviation);
    }
}

int TyreThermalSolver::DrainEvents(TyreBurstEvent* OutEvents, int Capacity)
{
    const int Count = m_EventCount < Capacity ? m_EventCount : Capacity;
    for (int Index = 0; Index < Count; ++Index)
        OutEvents[Index] = m_Events[Index];
    m_EventCount = 0;
    return Count;
}

bool TyreThermalSolver::QueryBurning() const
{
    for (int Index = 0; Index < WheelCount; ++Index)
        if (Wheels[Index].Ignited)
            return true;
    return false;
}

double TyreThermalSolver::QueryTreadFraction(int Index) const
{
    return std::fmax(0.0, Wheels[Index].TreadMass / Abrasion.TreadMass);
}

} // namespace Frontier

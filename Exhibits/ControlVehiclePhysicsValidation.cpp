/*============================================================================================================================================
                                                  CONTROLVEHICLEPHYSICSVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end JoltPhysics v5.6.0 ControlVehicle verification: Pacejka MF6.1, Turbo/Supercharger, Fuel, 6 Suspensions, Slope Hold & PhysicsThread

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/Components/Panes/ViewportPanel.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsDebugLog.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsMaterial.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"
#include "Engine/Threading/Sync/ThreadProgressionTracker.h"
#include "Engine/Threading/Threads/Physics/PhysicsThread.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

using namespace Frontier;
using namespace Frontier::ComponentClass;
using namespace Frontier::SimulationSpace::PhysicsSpace;
using namespace Frontier::Threading;

namespace
{

bool CheckCondition(bool Condition, const char* Label)
{
    if (Condition)
    {
        std::printf("[PASS] %s\n", Label);
        return true;
    }
    std::fprintf(stderr, "[FAIL] %s\n", Label);
    return false;
}

RigidBodyToken SpawnFlatTrack(PhysicsExtension& Physics, float HalfLengthX = 1200.0f, float HalfWidthZ = 400.0f)
{
    RigidBodyDescriptor Ground {};
    Ground.Shape.ShapeCategory          = CollisionShapeCategory::Box;
    Ground.Shape.HalfExtentsMeters = { HalfLengthX, 0.5f, HalfWidthZ };
    Ground.MotionCategory          = RigidMotionCategory::Static;
    Ground.PositionMeters          = { 0.0f, -0.5f, 0.0f };
    Ground.CollisionChannel        = 1u;
    return Physics.IssueRigidBody(Ground);
}

RigidBodyToken SpawnTiltedRamp(PhysicsExtension& Physics, float PitchAngleRad)
{
    // Rotate flat ground box around +Z axis so +X runs uphill (+Y rises with +X)
    const float HalfA = 0.5f * PitchAngleRad;
    RigidBodyDescriptor Ramp {};
    Ramp.Shape.ShapeCategory          = CollisionShapeCategory::Box;
    Ramp.Shape.HalfExtentsMeters = { 60.0f, 0.5f, 30.0f };
    Ramp.MotionCategory          = RigidMotionCategory::Static;
    Ramp.PositionMeters          = { 0.0f, -0.5f * std::cos(PitchAngleRad), 0.0f };
    Ramp.Orientation             = { 0.0f, 0.0f, std::sin(HalfA), std::cos(HalfA) };
    Ramp.CollisionChannel        = 1u;
    return Physics.IssueRigidBody(Ramp);
}

void StepCouple(PhysicsExtension& Physics, VehiclePhysicsRig& Rig, double FixedDt)
{
    Rig.StepOnPhysicsThread(Physics, FixedDt);
    Physics.Tick(FixedDt);
}

bool ValidateControlVehicleFullSuite(const ControlVehicleBuildResult& Built)
{
    const ComponentClassDefinition& ControlVehicleDef = Built.ClassDefinition;
    constexpr double FixedDt = 1.0 / 120.0;
    constexpr float  SpawnY  = 0.41715f; // Exact ControlVehicle GroundOffset (0.50855 - 0.0914)

    //------------------------------------------------------------------------------------------------------------------
    // 1. STATIC EQUILIBRIUM & IDLE VACUUM STABILITY
    //------------------------------------------------------------------------------------------------------------------
    {
        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        if (!CheckCondition(Physics.Initialize(), "PhysicsExtension initialized for ControlVehicle equilibrium (120 Hz Y-up)"))
        {
            return false;
        }
        (void)SpawnFlatTrack(Physics);

        VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleDef);
        VehiclePhysicsRig Rig {};
        if (!CheckCondition(Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f }), "VehiclePhysicsRig initialized from authored ControlVehicle"))
        {
            return false;
        }

        for (uint32_t Step = 0u; Step < 240u; ++Step)
        {
            StepCouple(Physics, Rig, FixedDt);
        }

        const auto Lease = Rig.AcquireVehicleReading();
        const float TotalLoadN = Lease->Corners[0u].VerticalLoadNewtons + Lease->Corners[1u].VerticalLoadNewtons
                               + Lease->Corners[2u].VerticalLoadNewtons + Lease->Corners[3u].VerticalLoadNewtons;
        const float ExpectedWeightN = Lease->LiveTotalMassKg * 9.80665f;
        const bool AllContact = Lease->Corners[0u].InContact && Lease->Corners[1u].InContact
                             && Lease->Corners[2u].InContact && Lease->Corners[3u].InContact;

        std::printf("[INFO] Equilibrium: Y=%.4f m, Vz=%.4f m/s, SumFz=%.1f N (Weight=%.1f N), IdleMAP=%.1f kPa, Step=%.1f us\n",
            Lease->WorldPositionMeters.Y, Lease->VerticalSpeedMps, TotalLoadN, ExpectedWeightN,
            Lease->ManifoldPressureKPa, Lease->StepComputeMicroseconds);

        if (!CheckCondition(AllContact, "ControlVehicle maintains 4-wheel Jolt ground contact at rest")) return false;
        if (!CheckCondition(std::fabs(Lease->VerticalSpeedMps) < 0.08f && Lease->GroundSpeedMps < 0.08f,
                "ControlVehicle settles cleanly at static equilibrium without bounce or drift")) return false;
        if (!CheckCondition(std::fabs(TotalLoadN - ExpectedWeightN) < ExpectedWeightN * 0.04f,
                "Static 4-corner normal loads match total vehicle weight within 4%")) return false;
        if (!CheckCondition(Lease->ManifoldPressureKPa > 25.0f && Lease->ManifoldPressureKPa < 60.0f,
                "Idle intake manifold pressure reports realistic engine vacuum (~32 kPa)")) return false;

        //--------------------------------------------------------------------------------------------------------------
        // 2. WOT LAUNCH, TURBOCHARGER SPOOL, WASTEGATE, BOV, GEAR SHIFTS & BODY-FRAME AERO
        //--------------------------------------------------------------------------------------------------------------
        VehicleControlDirective DriveDir {};
        DriveDir.Category           = VehicleDirectiveCategory::DriverInput;
        DriveDir.ThrottleNormalized = 1.0f;
        (void)Rig.SubmitControlDirective(DriveDir);

        float PeakTurboRpm    = 0.0f;
        float PeakTurboBoost  = 0.0f;
        float PeakMapKPa      = 0.0f;
        float PeakLongG       = 0.0f;
        int32_t PeakGear      = 1;

        for (uint32_t Step = 0u; Step < 840u; ++Step) // 7.0 seconds WOT
        {
            StepCouple(Physics, Rig, FixedDt);
            const auto R = Rig.AcquireVehicleReading();
            PeakTurboRpm   = std::max(PeakTurboRpm, R->TurboShaftRpm);
            PeakTurboBoost = std::max(PeakTurboBoost, R->TurboBoostBar);
            PeakMapKPa     = std::max(PeakMapKPa, R->ManifoldPressureKPa);
            PeakLongG      = std::max(PeakLongG, R->LongitudinalG);
            PeakGear       = std::max(PeakGear, R->ActiveGear);
        }

        const auto AtTopSpeed = Rig.AcquireVehicleReading();
        const float SpeedBeforeLiftKph = AtTopSpeed->SpeedKph;
        const float DownforceWithWingN = AtTopSpeed->AeroRearDownforceN;
        const float TotalAeroDfN       = AtTopSpeed->AeroDownforceNewtons;
        const float BoostBeforeLiftBar = AtTopSpeed->TurboBoostBar;

        // Verify RearWing Dock detachment coupling drops rear aerodynamic downforce immediately
        VehicleControlDirective DetachWing {};
        DetachWing.Category         = VehicleDirectiveCategory::AssignRearWingAttached;
        DetachWing.RearWingAttached = false;
        (void)Rig.SubmitControlDirective(DetachWing);
        StepCouple(Physics, Rig, FixedDt);
        const float DownforceWithoutWingN = Rig.AcquireVehicleReading()->AeroRearDownforceN;

        // Re-attach wing & lift throttle to verify BOV surge relief
        DetachWing.RearWingAttached = true;
        (void)Rig.SubmitControlDirective(DetachWing);
        DriveDir.ThrottleNormalized = 0.0f;
        (void)Rig.SubmitControlDirective(DriveDir);

        float PeakBov = 0.0f;
        for (uint32_t Step = 0u; Step < 36u; ++Step) // 0.3 s lift
        {
            StepCouple(Physics, Rig, FixedDt);
            PeakBov = std::max(PeakBov, Rig.AcquireVehicleReading()->BovPosition);
        }
        const float BoostAfterLiftBar = Rig.AcquireVehicleReading()->TurboBoostBar;

        std::printf("[INFO] WOT 7s: Speed=%.1f km/h, PeakGear=%d, PeakLongG=%.2f g, Turbo=%.0f rpm (%.2f bar, MAP=%.1f kPa), AeroDF=%.0f N (RearWing %.0f->%.0f N), BOV=%.2f (%.2f->%.2f bar)\n",
            SpeedBeforeLiftKph, PeakGear, PeakLongG, PeakTurboRpm, PeakTurboBoost, PeakMapKPa,
            TotalAeroDfN, DownforceWithWingN, DownforceWithoutWingN, PeakBov, BoostBeforeLiftBar, BoostAfterLiftBar);

        if (!CheckCondition(SpeedBeforeLiftKph > 160.0f && PeakGear >= 3,
                "ControlVehicle accelerates >160 km/h with automatic DCT upshifts across gears")) return false;
        if (!CheckCondition(PeakLongG > 0.45f,
                "Derived body-axis LongitudinalG is populated accurately during launch")) return false;
        if (!CheckCondition(PeakTurboRpm > 80000.0f && PeakTurboBoost > 0.85f && PeakMapKPa > 185.0f,
                "Turbocharger spools >80k rpm with >0.85 bar boost and physically exact absolute MAP")) return false;
        if (!CheckCondition(PeakBov > 0.25f && BoostAfterLiftBar < BoostBeforeLiftBar * 0.55f,
                "Blow-Off Valve opens on throttle lift and rapidly bleeds plenum boost")) return false;
        if (!CheckCondition(TotalAeroDfN > 600.0f && DownforceWithoutWingN < DownforceWithWingN * 0.70f,
                "Body-frame aero builds downforce and RearWing detachment drops rear downforce")) return false;

        //--------------------------------------------------------------------------------------------------------------
        // 3. HARD ABS BRAKING TO FULL STOP & ROTOR THERMAL RISE
        //--------------------------------------------------------------------------------------------------------------
        const float InitialRotorTempK = Rig.AcquireVehicleReading()->Corners[0u].BrakeTempKelvin;
        DriveDir.ThrottleNormalized   = 0.0f;
        DriveDir.BrakeNormalized      = 1.0f;
        (void)Rig.SubmitControlDirective(DriveDir);

        float MinLongG        = 0.0f;
        bool  SawAbs          = false;
        for (uint32_t Step = 0u; Step < 600u; ++Step) // Up to 5 s full brake
        {
            StepCouple(Physics, Rig, FixedDt);
            const auto R = Rig.AcquireVehicleReading();
            MinLongG = std::min(MinLongG, R->LongitudinalG);
            if (R->AbsIntervening) SawAbs = true;
            if (R->GroundSpeedMps < 0.05f && Step > 60u) break;
        }
        const auto Stopped = Rig.AcquireVehicleReading();
        std::printf("[INFO] ABS Stop: FinalSpeed=%.3f m/s, PeakDecel=%.2f g, RotorTemp=%.1f->%.1f K, ABS=%d, Regime=%u\n",
            Stopped->GroundSpeedMps, MinLongG, InitialRotorTempK, Stopped->Corners[0u].BrakeTempKelvin,
            SawAbs ? 1 : 0, static_cast<unsigned>(Stopped->Corners[0u].FrictionRegime));

        if (!CheckCondition(Stopped->GroundSpeedMps < 0.15f && MinLongG < -0.75f,
                "Hydraulic braking decelerates ControlVehicle >0.75 g cleanly to standstill")) return false;
        if (!CheckCondition(Stopped->Corners[0u].BrakeTempKelvin > InitialRotorTempK + 25.0f,
                "Brake rotor thermal model heats disks realistically during high-speed stop")) return false;

        Rig.Finalize(Physics);
        Physics.Finalize();
    }

    //------------------------------------------------------------------------------------------------------------------
    // 4. FORCED INDUCTION MATRIX: ROOTS / TWINSCREW / CENTRIFUGAL SUPERCHARGER + TWINCHARGED
    //------------------------------------------------------------------------------------------------------------------
    {
        auto MeasureInductionBoost = [&](ForcedInductionCategory Ind, SuperchargerDriveCategory ScDrive, float& OutLowRpmBoost, float& OutHighRpmBoost, float& OutDragNm, float& OutBypassHigh) -> bool
        {
            PhysicsSimulationParameters Params {};
            Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
            Params.FixedStepHz = 120.0f;
            PhysicsExtension Physics(Params);
            if (!Physics.Initialize()) return false;
            (void)SpawnFlatTrack(Physics);

            VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleDef);
            Spec.ForcedInduction = Ind;
            Spec.Supercharger.DriveCategory = ScDrive;
            VehiclePhysicsRig Rig {};
            if (!Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f })) return false;

            VehicleControlDirective Dir {};
            Dir.Category           = VehicleDirectiveCategory::DriverInput;
            Dir.ThrottleNormalized = 1.0f;
            (void)Rig.SubmitControlDirective(Dir);

            OutLowRpmBoost  = 0.0f;
            OutHighRpmBoost = 0.0f;
            OutDragNm       = 0.0f;
            OutBypassHigh   = 0.0f;
            bool CapturedLow = false;

            for (uint32_t Step = 0u; Step < 480u; ++Step)
            {
                StepCouple(Physics, Rig, FixedDt);
                const auto R = Rig.AcquireVehicleReading();
                OutDragNm = std::max(OutDragNm, R->SuperchargerParasiticNm);
                if (!CapturedLow && R->EngineRpm >= 2200.0f)
                {
                    OutLowRpmBoost = R->CombinedBoostBar;
                    CapturedLow    = true;
                }
                if (R->EngineRpm >= 4600.0f)
                {
                    OutHighRpmBoost = std::max(OutHighRpmBoost, R->CombinedBoostBar);
                    OutBypassHigh   = std::max(OutBypassHigh, R->SuperchargerBypassPos);
                }
            }
            Rig.Finalize(Physics);
            Physics.Finalize();
            return true;
        };

        float RootsLow = 0.0f, RootsHigh = 0.0f, RootsDrag = 0.0f, RootsByp = 0.0f;
        float ScrewLow = 0.0f, ScrewHigh = 0.0f, ScrewDrag = 0.0f, ScrewByp = 0.0f;
        float CentLow  = 0.0f, CentHigh  = 0.0f, CentDrag  = 0.0f, CentByp  = 0.0f;
        float TwinLow  = 0.0f, TwinHigh  = 0.0f, TwinDrag  = 0.0f, TwinByp  = 0.0f;

        if (!MeasureInductionBoost(ForcedInductionCategory::Supercharged, SuperchargerDriveCategory::Roots,       RootsLow, RootsHigh, RootsDrag, RootsByp)) return false;
        if (!MeasureInductionBoost(ForcedInductionCategory::Supercharged, SuperchargerDriveCategory::TwinScrew,   ScrewLow, ScrewHigh, ScrewDrag, ScrewByp)) return false;
        if (!MeasureInductionBoost(ForcedInductionCategory::Supercharged, SuperchargerDriveCategory::Centrifugal, CentLow,  CentHigh,  CentDrag,  CentByp))  return false;
        if (!MeasureInductionBoost(ForcedInductionCategory::Twincharged,  SuperchargerDriveCategory::TwinScrew,   TwinLow,  TwinHigh,  TwinDrag,  TwinByp))  return false;

        std::printf("[INFO] Induction: Roots(%.2f/%.2f bar, drag=%.1f Nm) TwinScrew(%.2f/%.2f bar) Centrifugal(%.2f/%.2f bar) Twincharged(%.2f/%.2f bar, SC_Bypass=%.2f)\n",
            RootsLow, RootsHigh, RootsDrag, ScrewLow, ScrewHigh, CentLow, CentHigh, TwinLow, TwinHigh, TwinByp);

        if (!CheckCondition(RootsLow > CentLow + 0.15f && RootsDrag > 15.0f,
                "Supercharger Roots delivers instant low-RPM boost + real crank parasitic drag")) return false;
        if (!CheckCondition(ScrewHigh > 0.65f && CentHigh > CentLow * 2.5f,
                "Supercharger TwinScrew & Centrifugal produce distinct physical boost profiles")) return false;
        if (!CheckCondition(TwinLow > 0.15f && TwinHigh > 0.90f && TwinByp > 0.40f,
                "Twincharged mode hands off seamlessly from Supercharger to Turbocharger via bypass valve")) return false;
    }

    //------------------------------------------------------------------------------------------------------------------
    // 5. FUEL DELIVERY, LIVE JOLT MASS COUPLING, STARVATION STALL & REFUEL
    //------------------------------------------------------------------------------------------------------------------
    {
        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        if (!Physics.Initialize()) return false;
        (void)SpawnFlatTrack(Physics);

        VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleDef);
        Spec.FuelDelivery.TankCapacityLiters      = 10.0f;
        Spec.FuelDelivery.InitialFuelLiters       = 0.35f; // Low fuel to test full burn -> warning -> critical -> starvation
        Spec.FuelDelivery.CoupleFuelMassToChassis = true;

        VehiclePhysicsRig Rig {};
        if (!Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f })) return false;

        const float StartMassKg  = Rig.AcquireVehicleReading()->LiveTotalMassKg;
        const float StartFuelL   = Rig.AcquireVehicleReading()->FuelLiters;

        VehicleControlDirective WOT {};
        WOT.Category           = VehicleDirectiveCategory::DriverInput;
        WOT.ThrottleNormalized = 1.0f;
        (void)Rig.SubmitControlDirective(WOT);

        bool SawEnrichedAfr = false;
        for (uint32_t Step = 0u; Step < 1440u; ++Step) // Up to 12 s until fuel runs dry
        {
            StepCouple(Physics, Rig, FixedDt);
            const auto R = Rig.AcquireVehicleReading();
            if (R->ActiveAirFuelRatio < 13.8f) SawEnrichedAfr = true;
            if (R->FuelStarved) break;
        }

        const auto DryReading = Rig.AcquireVehicleReading();
        const bool StarvedCleanly = DryReading->FuelStarved && DryReading->FuelCriticalActive && DryReading->FuelWarningActive;
        const bool MassDropped    = DryReading->LiveTotalMassKg < StartMassKg - 0.15f;

        // Run 0.5 s starved to confirm combustion torque cuts to <= 0
        for (uint32_t Step = 0u; Step < 60u; ++Step) StepCouple(Physics, Rig, FixedDt);
        const float StarvedTorqueNm = Rig.AcquireVehicleReading()->EngineTorqueNm;

        // Refuel via cross-thread directive & verify recovery
        VehicleControlDirective Refuel {};
        Refuel.Category     = VehicleDirectiveCategory::RefuelTank;
        Refuel.RefuelLiters = 8.0f;
        (void)Rig.SubmitControlDirective(Refuel);
        for (uint32_t Step = 0u; Step < 60u; ++Step) StepCouple(Physics, Rig, FixedDt);
        const auto Refueled = Rig.AcquireVehicleReading();

        std::printf("[INFO] Fuel: %.2f->%.3f L (Mass %.2f->%.2f->%.2f kg), StarvedTq=%.1f Nm, RefueledTq=%.1f Nm\n",
            StartFuelL, DryReading->FuelLiters, StartMassKg, DryReading->LiveTotalMassKg, Refueled->LiveTotalMassKg,
            StarvedTorqueNm, Refueled->EngineTorqueNm);

        if (!CheckCondition(StarvedCleanly && SawEnrichedAfr && MassDropped,
                "Fuel burns with boost AFR enrichment, reduces live Jolt mass & triggers starvation")) return false;
        if (!CheckCondition(StarvedTorqueNm <= 5.0f && !Refueled->FuelStarved && Refueled->EngineTorqueNm > 200.0f,
                "Fuel starvation cuts combustion torque and RefuelTank directive restores full power")) return false;

        Rig.Finalize(Physics);
        Physics.Finalize();
    }

    //------------------------------------------------------------------------------------------------------------------
    // 6. ALL SUSPENSION ARCHITECTURES + ASYMMETRIC CURB BUMP RECOVERY
    //------------------------------------------------------------------------------------------------------------------
    {
        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        if (!Physics.Initialize()) return false;
        (void)SpawnFlatTrack(Physics);

        // Place an asymmetric left-side curb bump (0.055 m tall) at X = 8 m, Z = -1.05 m
        RigidBodyDescriptor Curb {};
        Curb.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        Curb.Shape.HalfExtentsMeters = { 0.60f, 0.0275f, 0.70f };
        Curb.MotionCategory          = RigidMotionCategory::Static;
        Curb.PositionMeters          = { 8.0f, 0.0275f, -1.05f };
        Curb.CollisionChannel        = 1u;
        (void)Physics.IssueRigidBody(Curb);

        auto TestArchitectureOverCurb = [&](SuspensionArchitectureCategory FrontArch, SuspensionArchitectureCategory RearArch, float& OutPeakFrontCamberDiff, float& OutPeakRearBumpToe, float& OutPeakSolidAxleTilt) -> bool
        {
            VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleDef);
            for (uint32_t I = 0u; I < MaxVehicleWheelCorners; ++I)
            {
                VehicleRigSpecification::ApplySuspensionArchitectureDefaults(
                    Spec.Corners[I].Suspension,
                    Spec.Corners[I].FrontAxle ? FrontArch : RearArch);
            }
            VehiclePhysicsRig Rig {};
            if (!Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f })) return false;

            VehicleControlDirective Drive {};
            Drive.Category           = VehicleDirectiveCategory::DriverInput;
            Drive.ThrottleNormalized = 0.65f;
            (void)Rig.SubmitControlDirective(Drive);

            OutPeakFrontCamberDiff = 0.0f;
            OutPeakRearBumpToe     = 0.0f;
            OutPeakSolidAxleTilt   = 0.0f;
            float MaxPitchDeg      = 0.0f;

            for (uint32_t Step = 0u; Step < 360u; ++Step)
            {
                StepCouple(Physics, Rig, FixedDt);
                const auto R = Rig.AcquireVehicleReading();
                const float FrontCamDiff = std::fabs(R->Corners[0u].DynamicCamberRad - R->Corners[1u].DynamicCamberRad);
                OutPeakFrontCamberDiff   = std::max(OutPeakFrontCamberDiff, FrontCamDiff);
                OutPeakRearBumpToe       = std::max(OutPeakRearBumpToe, std::fabs(R->Corners[2u].DynamicBumpToeRad));
                OutPeakSolidAxleTilt     = std::max(OutPeakSolidAxleTilt, std::fabs(R->Corners[2u].DynamicCamberRad - R->Corners[3u].DynamicCamberRad));
                MaxPitchDeg              = std::max(MaxPitchDeg, std::fabs(R->PitchRateRadPerSec) * 57.2958f);
            }

            const auto EndR = Rig.AcquireVehicleReading();
            const bool StableAfterCurb = std::isfinite(EndR->WorldPositionMeters.Y)
                                      && EndR->WorldPositionMeters.Y > 0.15f
                                      && EndR->WorldPositionMeters.Y < 1.20f
                                      && EndR->WorldPositionMeters.X > 14.0f;
            Rig.Finalize(Physics);
            return StableAfterCurb && (MaxPitchDeg < 240.0f);
        };

        float DwCam = 0.0f, MlToe = 0.0f, Ign1 = 0.0f;
        float MacCam = 0.0f, SolidToe = 0.0f, SolidTilt = 0.0f;
        float TeleCam = 0.0f, TrailToe = 0.0f, Ign2 = 0.0f;

        if (!CheckCondition(TestArchitectureOverCurb(SuspensionArchitectureCategory::DoubleWishbone, SuspensionArchitectureCategory::MultiLink, DwCam, MlToe, Ign1),
                "DoubleWishbone + MultiLink traverses asymmetric curb bump cleanly")) return false;
        if (!CheckCondition(TestArchitectureOverCurb(SuspensionArchitectureCategory::MacPhersonStrut, SuspensionArchitectureCategory::SolidAxle, MacCam, SolidToe, SolidTilt),
                "MacPhersonStrut + SolidAxle traverses asymmetric curb bump cleanly")) return false;
        if (!CheckCondition(TestArchitectureOverCurb(SuspensionArchitectureCategory::Telescopic, SuspensionArchitectureCategory::TrailingArm, TeleCam, TrailToe, Ign2),
                "Telescopic + TrailingArm traverses asymmetric curb bump cleanly")) return false;

        std::printf("[INFO] Suspensions over curb: DoubleWishbone dCam=%.4f rad, MultiLink bumpToe=%.4f rad, SolidAxle coupledTilt=%.4f rad\n",
            DwCam, MlToe, SolidTilt);

        if (!CheckCondition(DwCam > 0.002f && MlToe > 0.002f && SolidTilt > 0.005f,
                "All 6 suspension architectures exhibit distinct camber, bump-toe & solid-axle kinematics")) return false;

        Physics.Finalize();
    }

    //------------------------------------------------------------------------------------------------------------------
    // 7. HIGH-SPEED CORNERING (ACKERMANN + PACEJKA COMBINED SLIP + TIRE THERMAL) & 10-DEG SLOPE KARNOPP HOLD
    //------------------------------------------------------------------------------------------------------------------
    {
        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        if (!Physics.Initialize()) return false;
        (void)SpawnFlatTrack(Physics);

        VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleDef);
        VehiclePhysicsRig Rig {};
        if (!Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f })) return false;

        // Accelerate to ~24 m/s (~86 km/h) then execute sustained right turn
        VehicleControlDirective Drive {};
        Drive.Category           = VehicleDirectiveCategory::DriverInput;
        Drive.ThrottleNormalized = 0.85f;
        (void)Rig.SubmitControlDirective(Drive);
        for (uint32_t Step = 0u; Step < 240u; ++Step) StepCouple(Physics, Rig, FixedDt);

        const float TreadBeforeTurnK = Rig.AcquireVehicleReading()->Corners[0u].TreadTempKelvin;
        Drive.ThrottleNormalized = 0.35f;
        Drive.SteeringNormalized = 0.55f; // Right turn (+Z)
        (void)Rig.SubmitControlDirective(Drive);

        float PeakLatG     = 0.0f;
        float PeakYawRate  = 0.0f;
        bool  AckermannOk  = false;
        for (uint32_t Step = 0u; Step < 300u; ++Step) // 2.5 s cornering
        {
            StepCouple(Physics, Rig, FixedDt);
            const auto R = Rig.AcquireVehicleReading();
            PeakLatG    = std::max(PeakLatG, std::fabs(R->LateralG));
            PeakYawRate = std::max(PeakYawRate, R->YawRateRadPerSec);
            if (R->Corners[1u].SteerAngleRad > R->Corners[0u].SteerAngleRad + 0.003f)
            {
                AckermannOk = true; // Right turn -> inner FR wheel steers sharper than outer FL
            }
        }

        const auto EndTurn = Rig.AcquireVehicleReading();
        const float TreadAfterTurnK = EndTurn->Corners[0u].TreadTempKelvin;
        std::printf("[INFO] Cornering: Speed=%.1f km/h, PeakLatG=%.2f g, PeakYawRate=%.2f rad/s, Z=%.1f m, TreadTemp=%.1f->%.1f K\n",
            EndTurn->SpeedKph, PeakLatG, PeakYawRate, EndTurn->WorldPositionMeters.Z, TreadBeforeTurnK, TreadAfterTurnK);

        if (!CheckCondition(PeakLatG > 0.50f && PeakYawRate > 0.25f && EndTurn->WorldPositionMeters.Z > 4.0f,
                "Pacejka MF6.1 combined-slip cornering turns smoothly rightward with >0.50 g lateral grip")) return false;
        if (!CheckCondition(AckermannOk && TreadAfterTurnK > TreadBeforeTurnK,
                "Ackermann steering geometry + tire thermal slip heating verified during sustained corner")) return false;

        Rig.Finalize(Physics);
        Physics.Finalize();
    }

    // 10-degree ramp Karnopp static hold test
    {
        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        if (!Physics.Initialize()) return false;

        constexpr float SlopeDeg = 10.0f;
        const float SlopeRad     = SlopeDeg * static_cast<float>(Constants::DegToRad);
        (void)SpawnTiltedRamp(Physics, SlopeRad);

        VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleDef);
        const float HalfA = 0.5f * SlopeRad;
        const RigidOrientationQuaternion RampQuat { 0.0f, 0.0f, std::sin(HalfA), std::cos(HalfA) };
        const Mathematics::Axis3D SpawnOnRamp { -SpawnY * std::sin(SlopeRad), SpawnY * std::cos(SlopeRad), 0.0f };

        VehiclePhysicsRig Rig {};
        if (!Rig.Initialize(Physics, Spec, SpawnOnRamp, RampQuat)) return false;

        VehicleControlDirective HoldBrake {};
        HoldBrake.Category        = VehicleDirectiveCategory::DriverInput;
        HoldBrake.BrakeNormalized = 1.0f;
        (void)Rig.SubmitControlDirective(HoldBrake);

        // Settle 0.3 s, then record reference position and hold for 2.5 s
        for (uint32_t Step = 0u; Step < 36u; ++Step) StepCouple(Physics, Rig, FixedDt);
        const Mathematics::Axis3D AnchorPos = Rig.AcquireVehicleReading()->WorldPositionMeters;
        for (uint32_t Step = 0u; Step < 300u; ++Step) StepCouple(Physics, Rig, FixedDt);

        const auto AfterHold = Rig.AcquireVehicleReading();
        const float Dx = AfterHold->WorldPositionMeters.X - AnchorPos.X;
        const float Dy = AfterHold->WorldPositionMeters.Y - AnchorPos.Y;
        const float Dz = AfterHold->WorldPositionMeters.Z - AnchorPos.Z;
        const float CreepMeters = std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);

        std::printf("[INFO] 10-deg Slope Hold: Creep=%.4f m over 2.5 s, Speed=%.4f m/s, Regime=%u\n",
            CreepMeters, AfterHold->GroundSpeedMps, static_cast<unsigned>(AfterHold->Corners[0u].FrictionRegime));

        if (!CheckCondition(CreepMeters < 0.05f && AfterHold->Corners[0u].FrictionRegime == FrictionRegimeCategory::StaticFull,
                "Karnopp StaticFull stiction holds ControlVehicle rock-steady on a 10-degree slope (<5 cm creep)")) return false;

        Rig.Finalize(Physics);
        Physics.Finalize();
    }

    //------------------------------------------------------------------------------------------------------------------
    // 8. LIVE DEDICATED PhysicsThread + 3 CONCURRENT WAIT-FREE READERS (Game / Render / Audio)
    //------------------------------------------------------------------------------------------------------------------
    {
        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        if (!Physics.Initialize()) return false;
        (void)SpawnFlatTrack(Physics);

        VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleDef);
        VehiclePhysicsRig Rig {};
        if (!Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f })) return false;

        struct ThreadedHost
        {
            PhysicsExtension*  Physics = nullptr;
            VehiclePhysicsRig* Rig     = nullptr;
        };
        ThreadedHost Host { &Physics, &Rig };

        ThreadProgressionTracker Watchdog {};
        PhysicsThread SimThread {};
        PhysicsThreadConfiguration ThreadCfg {};
        ThreadCfg.FixedStepHz            = 120.0f;
        ThreadCfg.SpawnDedicatedOsThread = true;
        ThreadCfg.PacedToWallClock       = false;
        ThreadCfg.OpaqueHost             = &Host;
        ThreadCfg.StepCallback           = [](void* OpaqueHost, double StepDt) noexcept
        {
            auto* H = static_cast<ThreadedHost*>(OpaqueHost);
            H->Rig->StepOnPhysicsThread(*H->Physics, StepDt);
            H->Physics->Tick(StepDt);
        };

        VehicleControlDirective InitialThrottle {};
        InitialThrottle.Category           = VehicleDirectiveCategory::DriverInput;
        InitialThrottle.ThrottleNormalized = 0.90f;
        (void)Rig.SubmitControlDirective(InitialThrottle);

        if (!CheckCondition(SimThread.Initialize(ThreadCfg, &Watchdog), "Dedicated PhysicsThread initialized for live ControlVehicle"))
        {
            return false;
        }

        std::atomic<bool> StopReaders { false };
        std::atomic<uint64_t> Violations { 0ull };
        std::atomic<uint64_t> GameReads { 0ull };
        std::atomic<uint64_t> RenderReads { 0ull };
        std::atomic<uint64_t> AudioReads { 0ull };

        auto ReaderLoop = [&](std::atomic<uint64_t>& Counter, bool SendInputs)
        {
            uint64_t PrevStep = 0ull;
            uint32_t Iter     = 0u;
            while (!StopReaders.load(std::memory_order_acquire))
            {
                const auto Lease = Rig.AcquireVehicleReading();
                const auto& R    = *Lease;
                if (R.CompletedSteps < PrevStep)
                {
                    Violations.fetch_add(1ull, std::memory_order_relaxed);
                }
                PrevStep = R.CompletedSteps;
                if (!std::isfinite(R.WorldPositionMeters.X) || !std::isfinite(R.EngineRpm) || !std::isfinite(R.ManifoldPressureKPa))
                {
                    Violations.fetch_add(1ull, std::memory_order_relaxed);
                }
                if (SendInputs && (Iter % 16u) == 0u)
                {
                    VehicleControlDirective D {};
                    D.Category           = VehicleDirectiveCategory::DriverInput;
                    D.ThrottleNormalized = 0.90f;
                    D.SteeringNormalized = 0.15f * std::sin(static_cast<float>(Iter) * 0.01f);
                    (void)Rig.SubmitControlDirective(D);
                }
                Counter.fetch_add(1ull, std::memory_order_relaxed);
                ++Iter;
            }
        };

        std::thread GameThread(ReaderLoop, std::ref(GameReads), true);
        std::thread RenderThread(ReaderLoop, std::ref(RenderReads), false);
        std::thread AudioThread(ReaderLoop, std::ref(AudioReads), false);

        const auto WallStart = std::chrono::steady_clock::now();
        while (SimThread.QueryCompletedStepCount() < 360ull
            || GameReads.load(std::memory_order_relaxed) < 500ull
            || RenderReads.load(std::memory_order_relaxed) < 500ull
            || AudioReads.load(std::memory_order_relaxed) < 500ull)
        {
            if (std::chrono::steady_clock::now() - WallStart > std::chrono::seconds(10))
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        StopReaders.store(true, std::memory_order_release);
        GameThread.join();
        RenderThread.join();
        AudioThread.join();
        SimThread.Finalize();

        const auto FinalR = Rig.AcquireVehicleReading();
        std::printf("[INFO] Live PhysicsThread: Steps=%llu, Speed=%.1f km/h, AvgStep=%.1f us, GameReads=%llu, RenderReads=%llu, AudioReads=%llu, Violations=%llu\n",
            static_cast<unsigned long long>(SimThread.QueryCompletedStepCount()),
            FinalR->SpeedKph,
            FinalR->StepComputeMicroseconds,
            static_cast<unsigned long long>(GameReads.load()),
            static_cast<unsigned long long>(RenderReads.load()),
            static_cast<unsigned long long>(AudioReads.load()),
            static_cast<unsigned long long>(Violations.load()));

        if (!CheckCondition(SimThread.QueryCompletedStepCount() >= 360ull && FinalR->SpeedKph > 50.0f,
                "Dedicated PhysicsThread drove live Jolt ControlVehicle smoothly under real-time steering")) return false;
        if (!CheckCondition(Violations.load() == 0ull && GameReads.load() >= 500ull && RenderReads.load() >= 500ull && AudioReads.load() >= 500ull,
                "Zero torn readings or NaN across concurrent Game, Render & Audio wait-free readers")) return false;

        Rig.Finalize(Physics);
        Physics.Finalize();
    }

    //------------------------------------------------------------------------------------------------------------------
    // 9. COMPOUNDHULL / TRIANGLEMESH BAKING (.GeometrySpace) + RIGIDBODY COMPONENT + PHYSICSMATERIAL (ASPHALT VS ICE)
    //------------------------------------------------------------------------------------------------------------------
    {
        // 9a. Verify ControlVehicle carries authored ChassisRigidBody component
        const auto Summary = ControlVehicleDef.Storage.ComputeSummaryMetrics();
        if (!CheckCondition(Summary.RigidBodyCount == 1u && Summary.CollisionCount == 1u,
                "ControlVehicle carries authored ChassisRigidBody (RigidBodyCount=1) alongside FrontBumperTrigger")) return false;

        // 9b. Bake CompoundHull from ControlledIntegrator.GeometrySpace body network & static TriangleMesh road
        const BakedColliderGeometry BakedChassis =
            RigidBodyDynamics::BakeCompoundHullFromPolygonNetwork(Built.GeometryRepository[0u].Asset.Network, 4u);

        GeometrySpace::PolygonNetwork RoadMesh {};
        RoadMesh.NetworkTitle = "SyntheticTrackDeck";
        const uint32_t V0 = RoadMesh.AppendVertex({ -40.0f, 0.0f, -20.0f });
        const uint32_t V1 = RoadMesh.AppendVertex({ 260.0f, 0.0f, -20.0f });
        const uint32_t V2 = RoadMesh.AppendVertex({ 260.0f, 0.0f,  20.0f });
        const uint32_t V3 = RoadMesh.AppendVertex({ -40.0f, 0.0f,  20.0f });
        const std::array<Mathematics::UVAxes, 3u> RoadUvs {{ { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f } }};
        const GeometrySpace::FaceShadingAssignment RoadShade {};
        (void)RoadMesh.AppendFace(std::array<uint32_t, 3u>{ V0, V2, V1 }, RoadUvs, RoadShade, 1u, 1u);
        (void)RoadMesh.AppendFace(std::array<uint32_t, 3u>{ V0, V3, V2 }, RoadUvs, RoadShade, 1u, 1u);

        const BakedColliderGeometry BakedRoad = RigidBodyDynamics::BakeTriangleMeshFromPolygonNetwork(RoadMesh);

        std::printf("[INFO] Collider Baking: CompoundHull(hulls=%zu, verts=%zu), TriangleMesh(tris=%zu)\n",
            BakedChassis.CompoundHullVertexCounts.size(),
            BakedChassis.ConvexVertices.size(),
            BakedRoad.MeshTriangles.size() / 3u);

        if (!CheckCondition(!BakedChassis.CompoundHullVertexCounts.empty() && BakedChassis.ConvexVertices.size() >= 16u
                && BakedRoad.MeshTriangles.size() == 6u,
                "Baked CompoundHull from ControlledIntegrator.GeometrySpace and static TriangleMesh road deck")) return false;

        // 9c. Spawn CompoundHull vehicle over TriangleMesh road (Asphalt vs Ice PhysicsMaterial comparison)
        auto RunSurfaceLaunch = [&](uint16_t SurfaceMatId, float& OutFinalKph, float& OutReportedGrip) -> bool
        {
            PhysicsSimulationParameters Params {};
            Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
            Params.FixedStepHz = 120.0f;
            PhysicsExtension Physics(Params);
            if (!Physics.Initialize()) return false;

            RigidBodyDescriptor RoadDesc {};
            RoadDesc.Shape.ShapeCategory = CollisionShapeCategory::TriangleMesh;
            RoadDesc.Shape.MeshTriangles = BakedRoad.MeshTriangles;
            RoadDesc.MotionCategory      = RigidMotionCategory::Static;
            RoadDesc.PhysicsMaterialId   = SurfaceMatId;
            RoadDesc.CollisionChannel    = 0u;
            if (!Physics.IssueRigidBody(RoadDesc).Valid()) return false;

            VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleDef);
            Spec.WheelContactMode        = WheelContactQueryMode::MultiRayArc;
            VehiclePhysicsRig Rig {};
            if (!Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f }, {}, &BakedChassis)) return false;

            VehicleControlDirective WOT {};
            WOT.Category           = VehicleDirectiveCategory::DriverInput;
            WOT.ThrottleNormalized = 1.0f;
            (void)Rig.SubmitControlDirective(WOT);

            for (uint32_t Step = 0u; Step < 300u; ++Step) StepCouple(Physics, Rig, FixedDt);
            const auto R = Rig.AcquireVehicleReading();
            OutFinalKph     = R->SpeedKph;
            OutReportedGrip = R->Corners[0u].SurfaceGripScale;

            Rig.Finalize(Physics);
            Physics.Finalize();
            return true;
        };

        float AsphaltKph = 0.0f, AsphaltGrip = 0.0f;
        float IceKph     = 0.0f, IceGrip     = 0.0f;
        if (!RunSurfaceLaunch(static_cast<uint16_t>(SurfaceMaterialCategory::Asphalt), AsphaltKph, AsphaltGrip)) return false;
        if (!RunSurfaceLaunch(static_cast<uint16_t>(SurfaceMaterialCategory::Ice),     IceKph,     IceGrip))     return false;

        std::printf("[INFO] PhysicsMaterial Launch (CompoundHull on TriangleMesh): Asphalt=%.1f km/h (grip=%.2fx) vs Ice=%.1f km/h (grip=%.2fx)\n",
            AsphaltKph, AsphaltGrip, IceKph, IceGrip);

        if (!CheckCondition(AsphaltKph > IceKph * 1.45f && AsphaltGrip > 0.95f && IceGrip < 0.30f,
                "Live PhysicsMaterial scales Pacejka tire traction (Asphalt 1.00x vs Ice 0.22x) on CompoundHull + TriangleMesh")) return false;
    }

    //------------------------------------------------------------------------------------------------------------------
    // 10. HEAD-TO-HEAD WHEEL CONTACT BENCHMARK: SingleRay vs MultiRayArc (5-ray) vs CylinderShapeCast (3D Sweep)
    //------------------------------------------------------------------------------------------------------------------
    {
        struct ContactBenchmarkMetrics
        {
            double   AvgStepUs      = 0.0;
            float    PeakSuspVelMps = 0.0f;
            float    PeakLoadN      = 0.0f;
            float    ExitSpeedKph   = 0.0f;
            uint32_t CurbHits       = 0u;
            bool     Valid          = false;
        };

        auto RunCurbBenchmark = [&](WheelContactQueryMode Mode) -> ContactBenchmarkMetrics
        {
            ContactBenchmarkMetrics Out {};
            PhysicsSimulationParameters Params {};
            Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
            Params.FixedStepHz = 120.0f;
            PhysicsExtension Physics(Params);
            if (!Physics.Initialize()) return Out;
            (void)SpawnFlatTrack(Physics);

            // Sharp 5.5 cm track curb strip spanning X = [14.0, 22.0] m, Y = [0.0, 0.055] m
            RigidBodyDescriptor CurbDesc {};
            CurbDesc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
            CurbDesc.Shape.HalfExtentsMeters = { 4.0f, 0.0275f, 12.0f };
            CurbDesc.MotionCategory          = RigidMotionCategory::Static;
            CurbDesc.PositionMeters          = { 18.0f, 0.0275f, 0.0f };
            CurbDesc.PhysicsMaterialId       = static_cast<uint16_t>(SurfaceMaterialCategory::Curb);
            CurbDesc.CollisionChannel        = 0u;
            (void)Physics.IssueRigidBody(CurbDesc);

            VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleDef);
            Spec.WheelContactMode        = Mode;
            Spec.EnableDebugLogging      = (Mode == WheelContactQueryMode::MultiRayArc);
            VehiclePhysicsRig Rig {};
            if (!Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f })) return Out;

            VehicleControlDirective Drive {};
            Drive.Category           = VehicleDirectiveCategory::DriverInput;
            Drive.ThrottleNormalized = 0.75f;
            (void)Rig.SubmitControlDirective(Drive);

            double SumStepUs = 0.0;
            for (uint32_t Step = 0u; Step < 360u; ++Step)
            {
                const auto T0 = std::chrono::steady_clock::now();
                StepCouple(Physics, Rig, FixedDt);
                const auto T1 = std::chrono::steady_clock::now();
                SumStepUs += std::chrono::duration<double, std::micro>(T1 - T0).count();

                const auto R = Rig.AcquireVehicleReading();
                if (!std::isfinite(R->SpeedKph) || !std::isfinite(R->WorldPositionMeters.Y))
                {
                    Rig.Finalize(Physics);
                    Physics.Finalize();
                    return Out;
                }
                // Measure front-axle curb strike impulse window (X in [10.5, 21.0] m)
                if (R->WorldPositionMeters.X >= 10.5f && R->WorldPositionMeters.X <= 21.0f)
                {
                    const float FrontVel = std::max(
                        std::fabs(R->Corners[0u].SuspensionVelocityMps),
                        std::fabs(R->Corners[1u].SuspensionVelocityMps));
                    const float FrontLoad = std::max(
                        R->Corners[0u].VerticalLoadNewtons,
                        R->Corners[1u].VerticalLoadNewtons);
                    Out.PeakSuspVelMps = std::max(Out.PeakSuspVelMps, FrontVel);
                    Out.PeakLoadN      = std::max(Out.PeakLoadN, FrontLoad);
                    if (R->Corners[0u].SurfaceMaterialId == static_cast<uint16_t>(SurfaceMaterialCategory::Curb))
                    {
                        ++Out.CurbHits;
                    }
                }
            }

            const auto FinalR = Rig.AcquireVehicleReading();
            Out.AvgStepUs    = SumStepUs / 360.0;
            Out.ExitSpeedKph = FinalR->SpeedKph;
            Out.Valid        = (Out.CurbHits > 10u) && (Out.ExitSpeedKph > 40.0f);

            Rig.Finalize(Physics);
            Physics.Finalize();
            return Out;
        };

        const auto Single = RunCurbBenchmark(WheelContactQueryMode::SingleRay);
        const auto Arc    = RunCurbBenchmark(WheelContactQueryMode::MultiRayArc);
        const auto Cyl    = RunCurbBenchmark(WheelContactQueryMode::CylinderShapeCast);

        std::printf("[INFO] Wheel Contact Curb Benchmark (5.5 cm sharp step):\n");
        std::printf("       SingleRay         : %.1f us/step | PeakSuspVel=%.2f m/s | PeakLoad=%.0f N | Exit=%.1f km/h\n",
            Single.AvgStepUs, Single.PeakSuspVelMps, Single.PeakLoadN, Single.ExitSpeedKph);
        std::printf("       MultiRayArc (5)   : %.1f us/step | PeakSuspVel=%.2f m/s | PeakLoad=%.0f N | Exit=%.1f km/h\n",
            Arc.AvgStepUs, Arc.PeakSuspVelMps, Arc.PeakLoadN, Arc.ExitSpeedKph);
        std::printf("       CylinderShapeCast : %.1f us/step | PeakSuspVel=%.2f m/s | PeakLoad=%.0f N | Exit=%.1f km/h\n",
            Cyl.AvgStepUs, Cyl.PeakSuspVelMps, Cyl.PeakLoadN, Cyl.ExitSpeedKph);

        if (!CheckCondition(Single.Valid && Arc.Valid && Cyl.Valid,
                "All 3 wheel contact modes (SingleRay, MultiRayArc, CylinderShapeCast) traverse 5.5 cm curb stably")) return false;
        if (!CheckCondition(Arc.PeakSuspVelMps < Single.PeakSuspVelMps && Cyl.PeakSuspVelMps < Single.PeakSuspVelMps,
                "MultiRayArc & CylinderShapeCast smooth out single-ray vertical curb impulse spike")) return false;
    }

    //------------------------------------------------------------------------------------------------------------------
    // 11. WAIT-FREE TRIGGER OVERLAPS + BOX/SPHERE OVERLAPS + PHYSICSDEBUGLOG + F3 VIEWPORTPANEL TELEMETRY
    //------------------------------------------------------------------------------------------------------------------
    {
        auto& Journal = PhysicsDebugLog::AccessSharedJournal();
        Journal.Clear();

        PhysicsSimulationParameters Params {};
        Params.GravityMps2 = { 0.0f, -9.80665f, 0.0f };
        Params.FixedStepHz = 120.0f;
        PhysicsExtension Physics(Params);
        if (!Physics.Initialize()) return false;
        (void)SpawnFlatTrack(Physics);

        // Static checkpoint trigger gate across track at X = [10, 14] m (sensor: zero collision impulse!)
        RigidBodyDescriptor GateDesc {};
        GateDesc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        GateDesc.Shape.HalfExtentsMeters = { 2.0f, 2.5f, 8.0f };
        GateDesc.MotionCategory          = RigidMotionCategory::Static;
        GateDesc.PositionMeters          = { 12.0f, 1.5f, 0.0f };
        GateDesc.IsTriggerSensor         = true;
        GateDesc.CollisionChannel        = 4u;
        const RigidBodyToken GateToken   = Physics.IssueRigidBody(GateDesc);
        if (!GateToken.Valid()) return false;

        VehicleRigSpecification Spec = VehiclePhysicsRig::ConstructSpecificationFromControlVehicle(ControlVehicleDef);
        Spec.EnableDebugLogging      = true;
        VehiclePhysicsRig Rig {};
        if (!Rig.Initialize(Physics, Spec, { 0.0f, SpawnY, 0.0f })) return false;

        VehicleControlDirective WOT {};
        WOT.Category           = VehicleDirectiveCategory::DriverInput;
        WOT.ThrottleNormalized = 1.0f;
        (void)Rig.SubmitControlDirective(WOT);

        bool SawEntered = false, SawStaying = false, SawExited = false;
        bool BoxOverlapFound = false, SphereOverlapFound = false;

        for (uint32_t Step = 0u; Step < 480u; ++Step)
        {
            StepCouple(Physics, Rig, FixedDt);

            const auto TrigLease = Physics.AcquireTriggerOverlapReading();
            if (TrigLease.Valid())
            {
                const auto& List = *TrigLease;
                for (uint32_t K = 0u; K < List.OverlapCount; ++K)
                {
                    if (List.Overlaps[K].SensorBody == GateToken)
                    {
                        if (List.Overlaps[K].Phase == TriggerOverlapPhase::Entered) SawEntered = true;
                        if (List.Overlaps[K].Phase == TriggerOverlapPhase::Staying) SawStaying = true;
                        if (List.Overlaps[K].Phase == TriggerOverlapPhase::Exited)  SawExited  = true;
                    }
                }
            }

            const auto VehR = Rig.AcquireVehicleReading();
            if (!BoxOverlapFound && VehR->WorldPositionMeters.X >= 11.0f && VehR->WorldPositionMeters.X <= 13.0f)
            {
                BoxOverlapQuery BQ {};
                BQ.CenterMeters      = { 12.0f, 1.0f, 0.0f };
                BQ.HalfExtentsMeters = { 2.5f, 2.0f, 4.0f };
                std::vector<RigidBodyToken> BoxHits {};
                Physics.QueryBoxOverlaps(BQ, BoxHits);
                for (const auto& Tok : BoxHits)
                {
                    if (Tok == Rig.QueryChassisBodyToken()) BoxOverlapFound = true;
                }

                SphereOverlapQuery SQ {};
                SQ.CenterMeters = { 12.0f, 1.0f, 0.0f };
                SQ.RadiusMeters = 3.0f;
                std::vector<RigidBodyToken> SphHits {};
                Physics.QuerySphereOverlaps(SQ, SphHits);
                for (const auto& Tok : SphHits)
                {
                    if (Tok == Rig.QueryChassisBodyToken()) SphereOverlapFound = true;
                }
            }
        }

        const auto FinalVeh = Rig.AcquireVehicleReading();
        const bool DroveCleanlyThroughGate = FinalVeh->WorldPositionMeters.X > 25.0f && FinalVeh->SpeedKph > 80.0f;
        const uint64_t TotalLogCount       = Journal.QueryTotalEmittedCount();
        const auto RecentLogs              = Journal.CopyRecentEntries(8u);

        // Hook live telemetry + PhysicsDebugLog into Editor F3 ViewportPanel & render both Physics and Vehicle tabs
        PhysicsTelemetryReading Tel {};
        Tel.PhysicsActive         = true;
        Tel.ActiveBodies          = Physics.QueryLiveBodyCount();
        Tel.ActiveConstraints     = Physics.QueryLiveConstraintCount();
        Tel.ActiveTriggerOverlaps = Physics.AcquireTriggerOverlapReading()->OverlapCount;
        Tel.StepComputeMs         = FinalVeh->StepComputeMicroseconds * 0.001f;
        Tel.FixedStepHz           = 120.0f;
        Tel.CompletedSteps        = FinalVeh->CompletedSteps;
        Tel.DebugLogSequence      = TotalLogCount;
        for (const auto& E : RecentLogs)
        {
            Tel.RecentLogLines.push_back(PhysicsDebugLog::FormatEntryLine(E));
        }
        if (!Tel.RecentLogLines.empty()) Tel.LatestLogSummary = Tel.RecentLogLines.back();
        Tel.VehicleActive         = true;
        Tel.SpeedKmh              = FinalVeh->SpeedKph;
        Tel.EngineRpm             = FinalVeh->EngineRpm;
        Tel.BoostBar              = FinalVeh->CombinedBoostBar;
        Tel.ActiveGear            = FinalVeh->ActiveGear;
        Tel.DownforceNewtons      = FinalVeh->AeroDownforceNewtons;
        Tel.FuelLiters            = FinalVeh->FuelLiters;
        Tel.FuelCapacityLiters    = Spec.FuelDelivery.TankCapacityLiters;
        Tel.AvgTreadTempC         = FinalVeh->Corners[0u].TreadTempKelvin - 273.15f;
        Tel.AvgSurfaceGrip        = FinalVeh->Corners[0u].SurfaceGripScale;
        Tel.WheelsInContact       = 4u;
        Tel.WheelContactModeLabel = "MultiRayArc";
        Tel.ActiveSurfaceLabel    = Physics.InspectPhysicsMaterial(FinalVeh->Corners[0u].SurfaceMaterialId).Label;

        ImGuiContext* HeadlessCtx = ImGui::CreateContext();
        ImGuiIO& Io               = ImGui::GetIO();
        Io.DisplaySize            = ImVec2(1280.0f, 720.0f);
        unsigned char* AtlasPx    = nullptr;
        int AtlasW = 0, AtlasH = 0;
        Io.Fonts->GetTexDataAsRGBA32(&AtlasPx, &AtlasW, &AtlasH);
        ImGui::NewFrame();

        ImDrawList* DrawList = ImGui::GetBackgroundDrawList();
        PixelSpace Surface(DrawList, Point2D{ 0.0f, 0.0f }, 1.0f, ImGui::GetFont());

        ViewportPanel VP {};
        VP.ConfigureDiagnosticsVisible(true);
        VP.ConfigurePhysicsTelemetry(Tel);
        PaneInputReading EmptyInput {};

        const int VtxBefore = DrawList->VtxBuffer.Size;
        VP.ConfigureDiagnosticsCategory(DiagnosticsTabCategory::Physics);
        VP.Render(Surface, Rectangle2D{ 0.0f, 0.0f, 1280.0f, 720.0f }, EmptyInput, 1.0 / 60.0);
        const int PhysicsTabVtx = DrawList->VtxBuffer.Size - VtxBefore;

        const int VtxBeforeVeh = DrawList->VtxBuffer.Size;
        VP.ConfigureDiagnosticsCategory(DiagnosticsTabCategory::Vehicle);
        VP.Render(Surface, Rectangle2D{ 0.0f, 0.0f, 1280.0f, 720.0f }, EmptyInput, 1.0 / 60.0);
        const int VehicleTabVtx = DrawList->VtxBuffer.Size - VtxBeforeVeh;

        ImGui::EndFrame();
        ImGui::DestroyContext(HeadlessCtx);

        std::printf("[INFO] Trigger Gate & F3 Telemetry: Enter=%d Stay=%d Exit=%d | BoxOverlap=%d SphereOverlap=%d | Logs=%llu | F3Vtx(Phys=%d, Veh=%d)\n",
            SawEntered ? 1 : 0,
            SawStaying ? 1 : 0,
            SawExited ? 1 : 0,
            BoxOverlapFound ? 1 : 0,
            SphereOverlapFound ? 1 : 0,
            static_cast<unsigned long long>(TotalLogCount),
            PhysicsTabVtx,
            VehicleTabVtx);
        if (!Tel.LatestLogSummary.empty())
        {
            std::printf("[INFO] Latest PhysicsDebugLog: %s\n", Tel.LatestLogSummary.c_str());
        }

        if (!CheckCondition(DroveCleanlyThroughGate && SawEntered && SawStaying && SawExited,
                "Wait-free TriggerOverlapListReading reports Entered -> Staying -> Exited with zero collision impulse")) return false;
        if (!CheckCondition(BoxOverlapFound && SphereOverlapFound,
                "QueryBoxOverlaps and QuerySphereOverlaps detect ControlVehicle inside trigger gate")) return false;
        if (!CheckCondition(TotalLogCount >= 4ull && PhysicsTabVtx > 200 && VehicleTabVtx > 200
                && VP.QueryLastPhysicsTelemetry().VehicleActive && VP.QueryLastPhysicsTelemetry().SpeedKmh > 80.0f,
                "PhysicsDebugLog journal & F3 ViewportPanel Physics/Vehicle telemetry tabs render live")) return false;

        Rig.Finalize(Physics);
        Physics.Finalize();
    }

    return true;
}

} // namespace

int main()
{
    std::printf("=== ControlVehiclePhysicsValidation ===\n");

    const std::filesystem::path IntegratorPath =
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "EngineContent" / "Geometry" / "ControlledIntegrator.GeometrySpace";
    ControlVehicleBuildResult Built {};
    std::string Diagnostic {};
    if (!CheckCondition(ControlVehicleClassBuilder::ConstructFromControlledIntegrator(IntegratorPath, Built, Diagnostic),
            "ControlVehicleClassBuilder::ConstructFromControlledIntegrator loaded ControlledIntegrator.GeometrySpace"))
    {
        std::fprintf(stderr, "Diagnostic: %s\n", Diagnostic.c_str());
        return 1;
    }

    const bool PureGeometrySpace = Built.DecodedSourceFaces == 25068u
                                && Built.GeometryRepository.size() == 4u
                                && Built.GeometryRepository[0u].SourceFormat == "GeometrySpace"
                                && Built.GeometryRepository[1u].SourceFormat == "GeometrySpace"
                                && Built.GeometryRepository[2u].SourceFormat == "GeometrySpace";
    std::printf("[INFO] ControlledIntegrator.GeometrySpace: SourceFaces=%u, BodyFaces=%zu, TyreFaces=%zu, RimFaces=%zu\n",
        Built.DecodedSourceFaces,
        Built.GeometryRepository[0u].Asset.Network.Faces.size(),
        Built.GeometryRepository[1u].Asset.Network.Faces.size(),
        Built.GeometryRepository[2u].Asset.Network.Faces.size());
    if (!CheckCondition(PureGeometrySpace, "ControlVehicle uses pure .GeometrySpace container (25,068 faces, zero .facet)"))
    {
        return 1;
    }

    if (!ValidateControlVehicleFullSuite(Built))
    {
        return 1;
    }

    std::printf("ALL ControlVehiclePhysicsValidation CHECKS PASSED.\n");
    return 0;
}

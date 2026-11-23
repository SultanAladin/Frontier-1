/*====================================================================================================================================
                                                     COMBATSIMULATION.JS
====================================================================================================================================*/
// 🧩 Chassis roster integration — driving, turret slew, ballistics, armour attrition, pursuit logic and possession transfer

import { FieldSpecification, ChassisClassRegistry, ChassisClassOrder,
         ArmamentRegistry, ArmamentOrder, ArmamentDropOrder, PickupSpecification } from "./CombatSpecification.js";
import { TerrainRelief, TerrainGradient } from "./ChassisGeometry.js";
import { Vec3GroundDistance, ShortestAngularSweep, ClampScalar, InterpolateScalar } from "./LinearAlgebra.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const TurretPitchCeiling   =  0.42;    // [rad] - Maximum barrel elevation
const TurretPitchFloor     = -0.16;    // [rad] - Maximum barrel depression
const StrafeSpeedFraction  =  0.62;    // [-]   - Strafe ceiling as a fraction of the class forward speed; tracks
                                       //         crab poorly, so sidestepping stays deliberately slower than driving
const ChassisSeparation    =  5.2;     // [m]   - Minimum ground distance between two chassis centres
const ObstacleSeparation   =  6.9;     // [m]   - Minimum ground distance from an obstacle centre
const DestructionDwell     =  4.0;     // [s]   - Wreck dwell before a replacement chassis enters the field
const OpponentSightRange   = 260.0;    // [m]   - Range at which an opponent begins pursuit
const OpponentFireRange    = 190.0;    // [m]   - Range at which an opponent will fire
const OpponentAimTolerance =  0.055;   // [rad] - Angular error an opponent accepts before firing

//------------------------------------------------------------------------------------------------------------------------
//                                                   DETERMINISTIC SAMPLING
//------------------------------------------------------------------------------------------------------------------------

// 📝 A hash-driven sequence rather than Math.random so a field layout can be reproduced from its seed while debugging.
class SampleSequence
{
    constructor(SeedScalar)
    {
        this.SequenceState = SeedScalar >>> 0;
    }

    NextUnitScalar()
    {
        this.SequenceState = (this.SequenceState * 1664525 + 1013904223) >>> 0;
        return this.SequenceState / 4294967296.0;
    }

    NextRangeScalar(MinimumBoundary, MaximumBoundary)
    {
        return MinimumBoundary + this.NextUnitScalar() * (MaximumBoundary - MinimumBoundary);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      FIELD ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

export class CombatField
{
    constructor(FieldSeed = 20260804)
    {
        this.SampleSource     = new SampleSequence(FieldSeed);
        this.ElapsedSeconds   = 0.0;
        this.ChassisRoster    = [];
        this.ProjectileRoster = [];
        this.ObstacleRoster   = [];
        this.ArmamentCrates   = [];
        this.CommandNotices   = [];
        this.PossessedIndex   = 0;
        this.EliminationTally = 0;
        this.NextChassisToken = 1;

        this.SeedObstacleRoster();
        this.SeedChassisRoster();
        this.SeedArmamentCrates();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  SEEDING
    //--------------------------------------------------------------------------------------------------------------------

    SeedObstacleRoster()
    {
        const PlacementLimit = FieldSpecification.GroundHalfExtent * 0.86;
        for (let ObstacleIndex = 0; ObstacleIndex < FieldSpecification.ObstacleCount; ++ObstacleIndex)
        {
            const OriginX = this.SampleSource.NextRangeScalar(-PlacementLimit, PlacementLimit);
            const OriginZ = this.SampleSource.NextRangeScalar(-PlacementLimit, PlacementLimit);

            // Keep the muster area around the origin clear so the opening view is not blocked by cover.
            if (Math.sqrt(OriginX * OriginX + OriginZ * OriginZ) < 34.0) continue;

            this.ObstacleRoster.push({
                GroundOrigin:   [OriginX, TerrainRelief(OriginX, OriginZ), OriginZ],
                PyramidProfile: this.SampleSource.NextUnitScalar() > 0.42,
                YawAngle:       this.SampleSource.NextRangeScalar(0.0, Math.PI * 2.0),
                ScaleFactor:    this.SampleSource.NextRangeScalar(0.72, 1.34)
            });
        }
    }

    // The possessed chassis is roster slot 0; the remainder are opposition seeded across the three classes.
    SeedChassisRoster()
    {
        this.ChassisRoster.push(this.ConstructChassis("Medium", [0.0, 0.0], 0.0, false));

        const OppositionPlan = ["Light", "Light", "Medium", "Heavy", "Medium", "Light", "Heavy"];
        for (const ClassLabel of OppositionPlan)
        {
            const SpawnAngle    = this.SampleSource.NextRangeScalar(0.0, Math.PI * 2.0);
            const SpawnDistance = this.SampleSource.NextRangeScalar(120.0, 330.0);
            const SpawnOrigin   = [Math.cos(SpawnAngle) * SpawnDistance, Math.sin(SpawnAngle) * SpawnDistance];
            this.ChassisRoster.push(this.ConstructChassis(ClassLabel, SpawnOrigin, SpawnAngle + Math.PI, true));
        }
    }

    SeedArmamentCrates()
    {
        // Two crates per scarce armament, spread across the field so neither armament is cornered.
        for (const ArmamentLabel of ArmamentDropOrder)
        {
            for (let CrateIndex = 0; CrateIndex < 2; ++CrateIndex)
            {
                const CrateAngle    = this.SampleSource.NextRangeScalar(0.0, Math.PI * 2.0);
                const CrateDistance = this.SampleSource.NextRangeScalar(70.0, 300.0);
                const OriginX = Math.cos(CrateAngle) * CrateDistance;
                const OriginZ = Math.sin(CrateAngle) * CrateDistance;
                this.ArmamentCrates.push({
                    GroundOrigin:    [OriginX, TerrainRelief(OriginX, OriginZ) + 1.6, OriginZ],
                    ArmamentLabel:   ArmamentLabel,
                    AvailableState:  true,
                    ReplenishTimer:  0.0
                });
            }
        }
    }

    ConstructChassis(ClassLabel, GroundOrigin, HeadingAngle, OppositionState)
    {
        const ClassProfile = ChassisClassRegistry[ClassLabel];
        return {
            ChassisToken:      this.NextChassisToken++,
            ClassLabel:        ClassLabel,
            ClassProfile:      ClassProfile,
            GroundPosition:    [GroundOrigin[0], TerrainRelief(GroundOrigin[0], GroundOrigin[1]), GroundOrigin[1]],
            HeadingAngle:      HeadingAngle,          // [rad] - Hull yaw
            TurretAngle:       HeadingAngle,          // [rad] - Turret yaw in world space
            BarrelPitch:       0.0,                   // [rad] - Barrel elevation
            ForwardVelocity:   0.0,                   // [m/s] - Signed speed along the drive axis
            LateralVelocity:   0.0,                   // [m/s] - Signed strafe speed across the drive axis
            WorldVelocity:     [0.0, 0.0],            // [m/s] - Resolved ground velocity (X, Z); the lead predictor
                                                      //         reads this rather than re-deriving from the heading
            ChassisPitch:      0.0,                   // [rad] - Terrain-seated pitch
            ChassisRoll:       0.0,                   // [rad] - Terrain-seated roll
            ArmourRemaining:   ClassProfile.ArmourCapacity,
            OppositionState:   OppositionState,
            DestroyedState:    false,
            DestructionTimer:  0.0,
            ArmamentLabel:     "Cannon",
            ArmamentReserves:  { Cannon: Infinity, Rocket: 0, ArcMortar: 0 },
            ReloadRemaining:   0.0,
            PursuitDrift:      this.SampleSource.NextRangeScalar(-1.0, 1.0),
            PursuitRefresh:    0.0,
            MuzzleFlashTimer:  0.0
        };
    }

    PossessedChassis()
    {
        return this.ChassisRoster[this.PossessedIndex];
    }

    PublishNotice(NoticeText)
    {
        this.CommandNotices.push({ NoticeText: NoticeText, RemainingSeconds: 3.4 });
        if (this.CommandNotices.length > 4) this.CommandNotices.shift();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              POSSESSION TRANSFER
    //--------------------------------------------------------------------------------------------------------------------

    // 🔴 Possession is the headline mechanic: the vacated chassis must immediately become opposition, otherwise the
    //    field would accumulate inert hulls that never fight back.
    TransferPossession(TargetIndex)
    {
        if (TargetIndex === this.PossessedIndex) return false;
        const TargetChassis = this.ChassisRoster[TargetIndex];
        if (!TargetChassis || TargetChassis.DestroyedState) return false;

        const VacatedChassis = this.ChassisRoster[this.PossessedIndex];
        if (VacatedChassis && !VacatedChassis.DestroyedState)
        {
            VacatedChassis.OppositionState = true;
            VacatedChassis.ForwardVelocity = 0.0;
            VacatedChassis.LateralVelocity = 0.0;   // Strafe is a possessed-only motion; a vacated hull must not keep it
            VacatedChassis.WorldVelocity[0] = 0.0;
            VacatedChassis.WorldVelocity[1] = 0.0;
        }

        TargetChassis.OppositionState = false;
        this.PossessedIndex           = TargetIndex;
        this.PublishNotice(`POSSESSION TRANSFERRED — ${TargetChassis.ClassProfile.ClassLabel}`);
        return true;
    }

    // Cycle to the next surviving chassis in roster order; wraps, and skips the currently possessed slot.
    TransferPossessionForward(SweepDirection)
    {
        const RosterCount = this.ChassisRoster.length;
        for (let SweepOffset = 1; SweepOffset <= RosterCount; ++SweepOffset)
        {
            const CandidateIndex = (this.PossessedIndex + SweepDirection * SweepOffset + RosterCount * 2) % RosterCount;
            if (!this.ChassisRoster[CandidateIndex].DestroyedState)
                return this.TransferPossession(CandidateIndex);
        }
        return false;
    }

    // Possess whichever surviving chassis sits nearest the possessed sight axis — the "take that one" seizure.
    TransferPossessionAlongSight()
    {
        const SourceChassis = this.PossessedChassis();
        let ClosestAngular  = 0.34;                                 // [rad] - Acceptance cone half-angle
        let ClosestIndex    = -1;

        for (let RosterIndex = 0; RosterIndex < this.ChassisRoster.length; ++RosterIndex)
        {
            if (RosterIndex === this.PossessedIndex) continue;
            const CandidateChassis = this.ChassisRoster[RosterIndex];
            if (CandidateChassis.DestroyedState) continue;

            const BearingAngle = Math.atan2(
                CandidateChassis.GroundPosition[0] - SourceChassis.GroundPosition[0],
                CandidateChassis.GroundPosition[2] - SourceChassis.GroundPosition[2]);
            const AngularError = Math.abs(ShortestAngularSweep(SourceChassis.TurretAngle, BearingAngle));
            if (AngularError < ClosestAngular)
            {
                ClosestAngular = AngularError;
                ClosestIndex   = RosterIndex;
            }
        }

        if (ClosestIndex < 0)
        {
            this.PublishNotice("NO CHASSIS IN SIGHT CONE");
            return false;
        }
        return this.TransferPossession(ClosestIndex);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 ARMAMENT DISCHARGE
    //--------------------------------------------------------------------------------------------------------------------

    CycleArmament(SourceChassis)
    {
        const CurrentOrdinal = ArmamentOrder.indexOf(SourceChassis.ArmamentLabel);
        for (let SweepOffset = 1; SweepOffset <= ArmamentOrder.length; ++SweepOffset)
        {
            const CandidateLabel = ArmamentOrder[(CurrentOrdinal + SweepOffset) % ArmamentOrder.length];
            if (SourceChassis.ArmamentReserves[CandidateLabel] > 0)
            {
                SourceChassis.ArmamentLabel = CandidateLabel;
                this.PublishNotice(`ARMAMENT — ${ArmamentRegistry[CandidateLabel].ArmamentLabel}`);
                return;
            }
        }
    }

    // Muzzle origin and axis derived from turret yaw + barrel pitch, so shells leave the bore rather than the hull centre.
    ResolveMuzzleFootprint(SourceChassis)
    {
        const ClassProfile = SourceChassis.ClassProfile;
        const YawCosine    = Math.cos(SourceChassis.TurretAngle);
        const YawSine      = Math.sin(SourceChassis.TurretAngle);
        const PitchCosine  = Math.cos(SourceChassis.BarrelPitch);
        const PitchSine    = Math.sin(SourceChassis.BarrelPitch);

        const BoreAxis = [YawSine * PitchCosine, PitchSine, YawCosine * PitchCosine];

        // 🔴 This MUST match the trunnion the renderer places the barrel at (see AccumulateFieldInstances), or shells
        //    leave from a point ~1.0-1.5 m below the visible muzzle. On flat ground that only looked slightly wrong;
        //    over cratered relief the shell departs below the crest ahead of the tank and buries itself in the rim,
        //    which is why opponents fired steadily and almost never landed a hit.
        const TrunnionHeight = ClassProfile.HullExtent[1] * 2.0 + ClassProfile.TrackHeight * 0.5 +
                               ClassProfile.TurretHeight * 0.5;
        const BoreReach      = ClassProfile.BarrelLength + ClassProfile.TurretRadius * 0.5;

        return {
            MuzzlePosition: [
                SourceChassis.GroundPosition[0] + BoreAxis[0] * BoreReach,
                SourceChassis.GroundPosition[1] + TrunnionHeight + BoreAxis[1] * BoreReach,
                SourceChassis.GroundPosition[2] + BoreAxis[2] * BoreReach
            ],
            BoreAxis: BoreAxis
        };
    }

    DischargeArmament(SourceChassis)
    {
        if (SourceChassis.DestroyedState || SourceChassis.ReloadRemaining > 0.0) return false;

        const ArmamentProfile = ArmamentRegistry[SourceChassis.ArmamentLabel];
        if (SourceChassis.ArmamentReserves[SourceChassis.ArmamentLabel] <= 0)
        {
            // Depleted scarce armament falls back to the innate cannon rather than dry-firing.
            SourceChassis.ArmamentLabel = "Cannon";
            return false;
        }

        const MuzzleFootprint = this.ResolveMuzzleFootprint(SourceChassis);
        this.ProjectileRoster.push({
            WorldPosition:   MuzzleFootprint.MuzzlePosition.slice(),
            LinearVelocity: [
                MuzzleFootprint.BoreAxis[0] * ArmamentProfile.MuzzleSpeed,
                MuzzleFootprint.BoreAxis[1] * ArmamentProfile.MuzzleSpeed,
                MuzzleFootprint.BoreAxis[2] * ArmamentProfile.MuzzleSpeed
            ],
            ArmamentLabel:   SourceChassis.ArmamentLabel,
            ArmamentProfile: ArmamentProfile,
            OriginToken:     SourceChassis.ChassisToken,
            ElapsedLifetime: 0.0
        });

        SourceChassis.ReloadRemaining  = ArmamentProfile.ReloadInterval;
        SourceChassis.MuzzleFlashTimer = 0.075;
        if (SourceChassis.ArmamentReserves[SourceChassis.ArmamentLabel] !== Infinity)
            SourceChassis.ArmamentReserves[SourceChassis.ArmamentLabel] -= 1;
        return true;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              CHASSIS INTEGRATION
    //--------------------------------------------------------------------------------------------------------------------

    // Terrain-seated pitch/roll from the relief gradient, resolved in the hull frame so slopes read correctly at any heading.
    SeatChassisAgainstRelief(TargetChassis, IntegrationStep)
    {
        const ReliefGradient = TerrainGradient(TargetChassis.GroundPosition[0], TargetChassis.GroundPosition[2]);
        const HeadingCosine  = Math.cos(TargetChassis.HeadingAngle);
        const HeadingSine    = Math.sin(TargetChassis.HeadingAngle);

        const AlongSlope  = ReliefGradient[0] * HeadingSine + ReliefGradient[1] * HeadingCosine;
        const AcrossSlope = ReliefGradient[0] * HeadingCosine - ReliefGradient[1] * HeadingSine;

        // ⚠️ Sign follows Mat4Assemble's positive-is-nose-up elevation convention: climbing a slope gives a positive
        //    AlongSlope and must raise the nose. Negating here (as an X-rotation would need) buries the nose uphill.
        const SeatingRate = ClampScalar(IntegrationStep * 7.0, 0.0, 1.0);
        TargetChassis.ChassisPitch = InterpolateScalar(TargetChassis.ChassisPitch, Math.atan(AlongSlope),  SeatingRate);
        TargetChassis.ChassisRoll  = InterpolateScalar(TargetChassis.ChassisRoll,  Math.atan(AcrossSlope), SeatingRate);
        TargetChassis.GroundPosition[1] = TerrainRelief(TargetChassis.GroundPosition[0], TargetChassis.GroundPosition[2]);
    }

    // 📝 Rejection against obstacles and other chassis, applied after the move rather than as a predictive sweep —
    //    at these speeds and separations a positional push-out is stable and far cheaper than a swept test.
    EnforceChassisSeparation(TargetChassis)
    {
        const FieldLimit = FieldSpecification.GroundHalfExtent - 8.0;
        TargetChassis.GroundPosition[0] = ClampScalar(TargetChassis.GroundPosition[0], -FieldLimit, FieldLimit);
        TargetChassis.GroundPosition[2] = ClampScalar(TargetChassis.GroundPosition[2], -FieldLimit, FieldLimit);

        for (const ObstacleEntry of this.ObstacleRoster)
        {
            const SeparationLimit = ObstacleSeparation * ObstacleEntry.ScaleFactor;
            const GroundSeparation = Vec3GroundDistance(TargetChassis.GroundPosition, ObstacleEntry.GroundOrigin);
            if (GroundSeparation < SeparationLimit && GroundSeparation > 1e-4)
            {
                const PushScale = (SeparationLimit - GroundSeparation) / GroundSeparation;
                TargetChassis.GroundPosition[0] +=
                    (TargetChassis.GroundPosition[0] - ObstacleEntry.GroundOrigin[0]) * PushScale;
                TargetChassis.GroundPosition[2] +=
                    (TargetChassis.GroundPosition[2] - ObstacleEntry.GroundOrigin[2]) * PushScale;
                TargetChassis.ForwardVelocity *= 0.35;
            }
        }

        for (const OtherChassis of this.ChassisRoster)
        {
            if (OtherChassis === TargetChassis || OtherChassis.DestroyedState) continue;
            const GroundSeparation = Vec3GroundDistance(TargetChassis.GroundPosition, OtherChassis.GroundPosition);
            if (GroundSeparation < ChassisSeparation && GroundSeparation > 1e-4)
            {
                const PushScale = (ChassisSeparation - GroundSeparation) / GroundSeparation * 0.5;
                TargetChassis.GroundPosition[0] +=
                    (TargetChassis.GroundPosition[0] - OtherChassis.GroundPosition[0]) * PushScale;
                TargetChassis.GroundPosition[2] +=
                    (TargetChassis.GroundPosition[2] - OtherChassis.GroundPosition[2]) * PushScale;
                TargetChassis.ForwardVelocity *= 0.6;
            }
        }
    }

    // DriveIntent: { ThrottleAxis, SteerAxis, TurretSweep, PitchSweep, DischargeRequest }
    IntegrateChassis(TargetChassis, DriveIntent, IntegrationStep)
    {
        const ClassProfile = TargetChassis.ClassProfile;

        if (TargetChassis.ReloadRemaining  > 0.0) TargetChassis.ReloadRemaining  -= IntegrationStep;
        if (TargetChassis.MuzzleFlashTimer > 0.0) TargetChassis.MuzzleFlashTimer -= IntegrationStep;

        // 🔴 The DRIVE BASIS is the whole reason movement felt detached from the view. The first-person sight looks
        //    along TurretAngle, but movement was resolved against HeadingAngle — so the moment the turret slewed off
        //    the hull centreline, "forward" was no longer where the player was looking and there was no strafe axis at
        //    all. The possessed chassis therefore drives in its SIGHT frame (modern first-person scheme); opposition
        //    keeps driving in its hull frame, which is what the pursuit solver's steering output means.
        const SightRelativeDrive = !TargetChassis.OppositionState;
        const DriveBearing = SightRelativeDrive ? TargetChassis.TurretAngle : TargetChassis.HeadingAngle;

        // Forward is (sin, cos) — the convention the whole simulation shares. Screen-right is the camera's lateral
        // axis, which Mat4LookDirection resolves as cross(Up, Backward) = (−cos, 0, sin); strafe MUST use that or
        // "strafe right" drifts diagonally instead of square across the view.
        const ForwardX = Math.sin(DriveBearing),  ForwardZ = Math.cos(DriveBearing);
        const StrafeX  = -Math.cos(DriveBearing), StrafeZ  = Math.sin(DriveBearing);

        // Explicit tank steering still available on the arrow keys; authority scales with speed so a stationary
        // chassis cannot pirouette instantly.
        const SpeedFraction  = Math.abs(TargetChassis.ForwardVelocity) / ClassProfile.ForwardSpeed;
        const SteerAuthority = 0.35 + ClampScalar(SpeedFraction, 0.0, 1.0) * 0.65;
        TargetChassis.HeadingAngle += DriveIntent.SteerAxis * ClassProfile.ChassisTurnRate *
                                      SteerAuthority * IntegrationStep;

        // Throttle toward the class ceiling; released throttle coasts down through the same acceleration term.
        const SpeedCeiling = DriveIntent.ThrottleAxis >= 0.0 ? ClassProfile.ForwardSpeed : ClassProfile.ReverseSpeed;
        const TargetSpeed  = DriveIntent.ThrottleAxis * SpeedCeiling;
        const SpeedDelta   = TargetSpeed - TargetChassis.ForwardVelocity;
        const SpeedStep    = ClassProfile.AccelerationRate * IntegrationStep;
        TargetChassis.ForwardVelocity += ClampScalar(SpeedDelta, -SpeedStep, SpeedStep);
        if (Math.abs(DriveIntent.ThrottleAxis) < 0.01)
            TargetChassis.ForwardVelocity *= Math.pow(0.12, IntegrationStep);

        // Strafe is a tracked velocity of its own so it accelerates and coasts like the forward axis rather than
        // snapping the chassis sideways. Tracks are poor at crabbing, so it tops out below the forward ceiling.
        const StrafeAxis    = ClampScalar(DriveIntent.StrafeAxis || 0.0, -1.0, 1.0);
        const StrafeCeiling = ClassProfile.ForwardSpeed * StrafeSpeedFraction;
        const StrafeDelta   = StrafeAxis * StrafeCeiling - TargetChassis.LateralVelocity;
        TargetChassis.LateralVelocity += ClampScalar(StrafeDelta, -SpeedStep, SpeedStep);
        if (Math.abs(StrafeAxis) < 0.01)
            TargetChassis.LateralVelocity *= Math.pow(0.12, IntegrationStep);

        // 🔴 Published so predictors never re-derive it. The pursuit solver used to reconstruct target velocity from
        //    HeadingAngle x ForwardVelocity, which silently became wrong the moment movement went sight-relative and
        //    gained a strafe axis — opponents then led every shot into empty ground and stopped scoring hits.
        TargetChassis.WorldVelocity[0] = ForwardX * TargetChassis.ForwardVelocity +
                                         StrafeX  * TargetChassis.LateralVelocity;
        TargetChassis.WorldVelocity[1] = ForwardZ * TargetChassis.ForwardVelocity +
                                         StrafeZ  * TargetChassis.LateralVelocity;

        TargetChassis.GroundPosition[0] += TargetChassis.WorldVelocity[0] * IntegrationStep;
        TargetChassis.GroundPosition[2] += TargetChassis.WorldVelocity[1] * IntegrationStep;

        // 📝 With a sight-relative basis the hull would otherwise never face where it travels. Easing the heading
        //    toward the actual direction of travel keeps the tracks and the hull reading correctly, and it leaves the
        //    hull free to trail behind during a strafe the way a real chassis would.
        if (SightRelativeDrive)
        {
            const TravelRate = Math.hypot(TargetChassis.ForwardVelocity, TargetChassis.LateralVelocity);
            if (TravelRate > 0.4)
            {
                const TravelBearing = Math.atan2(
                    ForwardX * TargetChassis.ForwardVelocity + StrafeX * TargetChassis.LateralVelocity,
                    ForwardZ * TargetChassis.ForwardVelocity + StrafeZ * TargetChassis.LateralVelocity);
                const HeadingError = ShortestAngularSweep(TargetChassis.HeadingAngle, TravelBearing);
                TargetChassis.HeadingAngle += HeadingError *
                    ClampScalar(ClassProfile.ChassisTurnRate * IntegrationStep, 0.0, 1.0);
            }
        }

        // 🔴 Two DISTINCT turret inputs, and conflating them throttles mouse-look to uselessness:
        //    TurretSweep is an ABSOLUTE angular delta already sized by the pointer (or by the pursuit solver), so it
        //    applies verbatim. TurretSlewAxis is a -1..1 held-key axis, so it is what the class turn rate bounds.
        const TurretSlewLimit = ClassProfile.TurretTurnRate * IntegrationStep;
        TargetChassis.TurretAngle += DriveIntent.TurretSweep +
                                     ClampScalar(DriveIntent.TurretSlewAxis || 0.0, -1.0, 1.0) * TurretSlewLimit;

        const PitchSlewLimit = ClassProfile.TurretTurnRate * IntegrationStep;
        TargetChassis.BarrelPitch = ClampScalar(
            TargetChassis.BarrelPitch + DriveIntent.PitchSweep +
            ClampScalar(DriveIntent.PitchSlewAxis || 0.0, -1.0, 1.0) * PitchSlewLimit,
            TurretPitchFloor, TurretPitchCeiling);

        this.EnforceChassisSeparation(TargetChassis);
        this.SeatChassisAgainstRelief(TargetChassis, IntegrationStep);

        if (DriveIntent.DischargeRequest) this.DischargeArmament(TargetChassis);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 PURSUIT RESOLUTION
    //--------------------------------------------------------------------------------------------------------------------

    // 💡 Opposition intent is derived, not scripted: bearing to the possessed chassis drives steering and turret slew,
    //    with a drift term so several opponents do not converge into one identical line of approach.
    ResolvePursuitIntent(OpponentChassis, IntegrationStep)
    {
        const TargetChassis = this.PossessedChassis();
        const DriveIntent   = { ThrottleAxis: 0.0, SteerAxis: 0.0, TurretSweep: 0.0,
                                PitchSweep: 0.0, DischargeRequest: false };
        if (!TargetChassis || TargetChassis.DestroyedState) return DriveIntent;

        const GroundSeparation = Vec3GroundDistance(OpponentChassis.GroundPosition, TargetChassis.GroundPosition);
        const BearingAngle     = Math.atan2(
            TargetChassis.GroundPosition[0] - OpponentChassis.GroundPosition[0],
            TargetChassis.GroundPosition[2] - OpponentChassis.GroundPosition[2]);

        OpponentChassis.PursuitRefresh -= IntegrationStep;
        if (OpponentChassis.PursuitRefresh <= 0.0)
        {
            OpponentChassis.PursuitDrift   = this.SampleSource.NextRangeScalar(-0.7, 0.7);
            OpponentChassis.PursuitRefresh = this.SampleSource.NextRangeScalar(1.1, 2.8);
        }

        // Beyond sight range an opponent patrols on its drift term instead of homing from across the map.
        if (GroundSeparation > OpponentSightRange)
        {
            DriveIntent.ThrottleAxis = 0.42;
            DriveIntent.SteerAxis    = OpponentChassis.PursuitDrift * 0.5;
            DriveIntent.TurretSweep  = ShortestAngularSweep(OpponentChassis.TurretAngle,
                                                            OpponentChassis.HeadingAngle) * 0.5 * IntegrationStep;
            return DriveIntent;
        }

        const HeadingError = ShortestAngularSweep(OpponentChassis.HeadingAngle,
                                                  BearingAngle + OpponentChassis.PursuitDrift * 0.30);
        DriveIntent.SteerAxis = ClampScalar(HeadingError * 1.8, -1.0, 1.0);

        // Standoff band: close in when far, back off when uncomfortably close, hold at effective range.
        const StandoffDistance = 74.0 + OpponentChassis.ClassProfile.ThreatRating * 28.0;
        if (GroundSeparation > StandoffDistance * 1.2)      DriveIntent.ThrottleAxis =  0.95;
        else if (GroundSeparation < StandoffDistance * 0.55) DriveIntent.ThrottleAxis = -0.65;
        else                                                 DriveIntent.ThrottleAxis =  0.22;

        // Turret lead: aim where the target will be after the shell's flight, using the armament's muzzle speed.
        const ArmamentProfile = ArmamentRegistry[OpponentChassis.ArmamentLabel];
        const FlightDuration  = GroundSeparation / ArmamentProfile.MuzzleSpeed;
        const LeadPositionX   = TargetChassis.GroundPosition[0] + TargetChassis.WorldVelocity[0] * FlightDuration;
        const LeadPositionZ   = TargetChassis.GroundPosition[2] + TargetChassis.WorldVelocity[1] * FlightDuration;
        const LeadBearing     = Math.atan2(LeadPositionX - OpponentChassis.GroundPosition[0],
                                           LeadPositionZ - OpponentChassis.GroundPosition[2]);

        const TurretError = ShortestAngularSweep(OpponentChassis.TurretAngle, LeadBearing);
        DriveIntent.TurretSweep = ClampScalar(TurretError, -1.0, 1.0) *
                                  OpponentChassis.ClassProfile.TurretTurnRate * IntegrationStep;

        // Ballistic elevation for the arcing armaments; the flat cannon holds a level bore.
        const VerticalSeparation = TargetChassis.GroundPosition[1] - OpponentChassis.GroundPosition[1];
        let RequiredPitch = Math.atan2(VerticalSeparation, Math.max(GroundSeparation, 1.0));
        if (ArmamentProfile.GravityScale > 0.0)
        {
            const DropCompensation = FieldSpecification.GravityAcceleration * ArmamentProfile.GravityScale *
                                     FlightDuration * 0.5 / ArmamentProfile.MuzzleSpeed;
            RequiredPitch += DropCompensation;
        }
        RequiredPitch = ClampScalar(RequiredPitch, TurretPitchFloor, TurretPitchCeiling);
        DriveIntent.PitchSweep = (RequiredPitch - OpponentChassis.BarrelPitch) *
                                 ClampScalar(IntegrationStep * 4.0, 0.0, 1.0);

        DriveIntent.DischargeRequest = GroundSeparation < OpponentFireRange &&
                                       Math.abs(TurretError) < OpponentAimTolerance;
        return DriveIntent;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                               BALLISTIC RESOLUTION
    //--------------------------------------------------------------------------------------------------------------------

    ApplyArmourAttrition(TargetChassis, DamageAmount)
    {
        if (TargetChassis.DestroyedState) return;
        TargetChassis.ArmourRemaining -= DamageAmount;
        if (TargetChassis.ArmourRemaining > 0.0) return;

        TargetChassis.ArmourRemaining  = 0.0;
        TargetChassis.DestroyedState   = true;
        TargetChassis.DestructionTimer = DestructionDwell;
        TargetChassis.ForwardVelocity  = 0.0;
        TargetChassis.LateralVelocity  = 0.0;
        TargetChassis.WorldVelocity[0] = 0.0;
        TargetChassis.WorldVelocity[1] = 0.0;

        if (TargetChassis === this.PossessedChassis())
            this.PublishNotice("POSSESSED CHASSIS DESTROYED — SEIZING ANOTHER");
        else
        {
            this.EliminationTally += 1;
            this.PublishNotice(`${TargetChassis.ClassProfile.ClassLabel} DESTROYED`);
        }
    }

    // Splash damage falls off linearly to the radius edge; direct hits already took their full damage.
    ApplySplashAttrition(ImpactPosition, ArmamentProfile, ExemptChassis)
    {
        if (ArmamentProfile.SplashRadius <= 0.0) return;
        for (const TargetChassis of this.ChassisRoster)
        {
            if (TargetChassis === ExemptChassis || TargetChassis.DestroyedState) continue;
            const ImpactSeparation = Vec3GroundDistance(ImpactPosition, TargetChassis.GroundPosition);
            if (ImpactSeparation > ArmamentProfile.SplashRadius) continue;
            const FalloffWeight = 1.0 - (ImpactSeparation / ArmamentProfile.SplashRadius);
            this.ApplyArmourAttrition(TargetChassis, ArmamentProfile.ProjectileDamage * 0.55 * FalloffWeight);
        }
    }

    IntegrateProjectiles(IntegrationStep)
    {
        const SurvivingProjectiles = [];

        for (const ProjectileEntry of this.ProjectileRoster)
        {
            const ArmamentProfile = ProjectileEntry.ArmamentProfile;
            ProjectileEntry.ElapsedLifetime += IntegrationStep;

            ProjectileEntry.LinearVelocity[1] -= FieldSpecification.GravityAcceleration *
                                                 ArmamentProfile.GravityScale * IntegrationStep;
            ProjectileEntry.WorldPosition[0] += ProjectileEntry.LinearVelocity[0] * IntegrationStep;
            ProjectileEntry.WorldPosition[1] += ProjectileEntry.LinearVelocity[1] * IntegrationStep;
            ProjectileEntry.WorldPosition[2] += ProjectileEntry.LinearVelocity[2] * IntegrationStep;

            if (ProjectileEntry.ElapsedLifetime > ArmamentProfile.LifetimeCeiling) continue;

            // Ground impact.
            const GroundHeight = TerrainRelief(ProjectileEntry.WorldPosition[0], ProjectileEntry.WorldPosition[2]);
            if (ProjectileEntry.WorldPosition[1] <= GroundHeight)
            {
                this.ApplySplashAttrition(ProjectileEntry.WorldPosition, ArmamentProfile, null);
                continue;
            }

            // Chassis impact — a coarse capsule test against the hull box, generous enough to feel fair at speed.
            let ImpactResolved = false;
            for (const TargetChassis of this.ChassisRoster)
            {
                if (TargetChassis.DestroyedState) continue;
                if (TargetChassis.ChassisToken === ProjectileEntry.OriginToken) continue;

                const ClassProfile     = TargetChassis.ClassProfile;
                const ImpactSeparation = Vec3GroundDistance(ProjectileEntry.WorldPosition, TargetChassis.GroundPosition);
                const VerticalOffset   = ProjectileEntry.WorldPosition[1] - TargetChassis.GroundPosition[1];
                const ImpactRadius     = Math.max(ClassProfile.HullExtent[0], ClassProfile.HullExtent[2]) * 0.92 +
                                         ArmamentProfile.ProjectileRadius;
                const ImpactCeiling    = ClassProfile.HullExtent[1] * 2.0 + ClassProfile.TurretHeight + 0.6;

                if (ImpactSeparation < ImpactRadius && VerticalOffset > -1.6 && VerticalOffset < ImpactCeiling)
                {
                    this.ApplyArmourAttrition(TargetChassis, ArmamentProfile.ProjectileDamage);
                    this.ApplySplashAttrition(ProjectileEntry.WorldPosition, ArmamentProfile, TargetChassis);
                    ImpactResolved = true;
                    break;
                }
            }
            if (ImpactResolved) continue;

            // Obstacle impact — pyramids and blocks stop shells, which is what makes the arc mortar worth carrying.
            let ObstacleBlocked = false;
            for (const ObstacleEntry of this.ObstacleRoster)
            {
                const ObstacleReach  = ObstacleSeparation * ObstacleEntry.ScaleFactor * 0.78;
                const ObstacleHeight = (ObstacleEntry.PyramidProfile ? 9.2 : 6.2) * ObstacleEntry.ScaleFactor;
                if (Vec3GroundDistance(ProjectileEntry.WorldPosition, ObstacleEntry.GroundOrigin) < ObstacleReach &&
                    ProjectileEntry.WorldPosition[1] < ObstacleEntry.GroundOrigin[1] + ObstacleHeight)
                {
                    this.ApplySplashAttrition(ProjectileEntry.WorldPosition, ArmamentProfile, null);
                    ObstacleBlocked = true;
                    break;
                }
            }
            if (ObstacleBlocked) continue;

            SurvivingProjectiles.push(ProjectileEntry);
        }

        this.ProjectileRoster = SurvivingProjectiles;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              CRATE AND WRECK LIFECYCLE
    //--------------------------------------------------------------------------------------------------------------------

    IntegrateArmamentCrates(IntegrationStep)
    {
        for (const CrateEntry of this.ArmamentCrates)
        {
            if (!CrateEntry.AvailableState)
            {
                CrateEntry.ReplenishTimer -= IntegrationStep;
                if (CrateEntry.ReplenishTimer <= 0.0) CrateEntry.AvailableState = true;
                continue;
            }

            for (const TargetChassis of this.ChassisRoster)
            {
                if (TargetChassis.DestroyedState) continue;
                if (Vec3GroundDistance(TargetChassis.GroundPosition, CrateEntry.GroundOrigin) >
                    PickupSpecification.CollectionRadius) continue;

                const ArmamentProfile = ArmamentRegistry[CrateEntry.ArmamentLabel];
                TargetChassis.ArmamentReserves[CrateEntry.ArmamentLabel] += ArmamentProfile.MagazineCapacity;
                TargetChassis.ArmamentLabel  = CrateEntry.ArmamentLabel;
                CrateEntry.AvailableState    = false;
                CrateEntry.ReplenishTimer    = PickupSpecification.ReplenishInterval;

                if (TargetChassis === this.PossessedChassis())
                    this.PublishNotice(`ACQUIRED — ${ArmamentProfile.ArmamentLabel}`);
                break;
            }
        }
    }

    // A destroyed chassis dwells as a wreck, then re-enters the field as a fresh opponent at the perimeter.
    IntegrateWreckRecovery(IntegrationStep)
    {
        for (let RosterIndex = 0; RosterIndex < this.ChassisRoster.length; ++RosterIndex)
        {
            const TargetChassis = this.ChassisRoster[RosterIndex];
            if (!TargetChassis.DestroyedState) continue;

            TargetChassis.DestructionTimer -= IntegrationStep;
            if (TargetChassis.DestructionTimer > 0.0) continue;

            const ReplacementClass = ChassisClassOrder[
                Math.floor(this.SampleSource.NextUnitScalar() * ChassisClassOrder.length)];
            const SpawnAngle    = this.SampleSource.NextRangeScalar(0.0, Math.PI * 2.0);
            const SpawnDistance = this.SampleSource.NextRangeScalar(240.0, 360.0);
            const ReplacementChassis = this.ConstructChassis(ReplacementClass,
                [Math.cos(SpawnAngle) * SpawnDistance, Math.sin(SpawnAngle) * SpawnDistance],
                SpawnAngle + Math.PI, true);

            this.ChassisRoster[RosterIndex] = ReplacementChassis;

            // 🔴 If the possessed chassis was the one recycled, possession must migrate or the sight has no owner.
            if (RosterIndex === this.PossessedIndex)
            {
                let RelocatedIndex = -1;
                for (let CandidateIndex = 0; CandidateIndex < this.ChassisRoster.length; ++CandidateIndex)
                {
                    if (CandidateIndex === RosterIndex) continue;
                    if (!this.ChassisRoster[CandidateIndex].DestroyedState) { RelocatedIndex = CandidateIndex; break; }
                }
                if (RelocatedIndex >= 0)
                {
                    this.PossessedIndex = RelocatedIndex;
                    this.ChassisRoster[RelocatedIndex].OppositionState = false;
                }
                else
                {
                    ReplacementChassis.OppositionState = false;
                }
            }
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  FIELD INTEGRATION
    //--------------------------------------------------------------------------------------------------------------------

    IntegrateField(PossessedIntent, IntegrationStep)
    {
        this.ElapsedSeconds += IntegrationStep;

        for (let RosterIndex = 0; RosterIndex < this.ChassisRoster.length; ++RosterIndex)
        {
            const TargetChassis = this.ChassisRoster[RosterIndex];
            if (TargetChassis.DestroyedState) continue;

            const DriveIntent = RosterIndex === this.PossessedIndex
                ? PossessedIntent
                : this.ResolvePursuitIntent(TargetChassis, IntegrationStep);
            this.IntegrateChassis(TargetChassis, DriveIntent, IntegrationStep);
        }

        this.IntegrateProjectiles(IntegrationStep);
        this.IntegrateArmamentCrates(IntegrationStep);
        this.IntegrateWreckRecovery(IntegrationStep);

        for (const NoticeEntry of this.CommandNotices) NoticeEntry.RemainingSeconds -= IntegrationStep;
        this.CommandNotices = this.CommandNotices.filter(NoticeEntry => NoticeEntry.RemainingSeconds > 0.0);
    }
}

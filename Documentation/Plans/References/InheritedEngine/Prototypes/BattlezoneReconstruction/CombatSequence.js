/*====================================================================================================================================
                                                      COMBATSEQUENCE.JS
====================================================================================================================================*/
// 🧩 Frame sequence — fixed-step integration, first-person sight placement, instance accumulation and overlay dispatch

import { FieldSpecification, ChassisClassRegistry, ChassisClassOrder,
         ArmamentRegistry, PickupSpecification } from "./CombatSpecification.js";
import { AssembleGeometryAtlas, TerrainRelief } from "./ChassisGeometry.js";
import { CombatField } from "./CombatSimulation.js";
import { SurfaceRasterization } from "./SurfaceRasterization.js";
import { InputSubstrate } from "./InputSubstrate.js";
import { CombatOverlay } from "./CombatOverlay.js";
import { ClampScalar } from "./LinearAlgebra.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const IntegrationStep   = 1.0 / 120.0;   // [s] - Fixed simulation step
const AccumulatorCeiling = 0.25;         // [s] - Maximum frame time absorbed in one go, guarding against tab-switch spikes
const FogDensity        = 0.0022;        // [-] - Exponential-squared fog coefficient
const TerrainTone       = [0.29, 0.29, 0.31];   // Lunar regolith grey — faintly blue-cold under the key light
const ObstacleTone      = [0.38, 0.37, 0.40];   // Basalt monoliths, a shade lighter than the plain they sit on

//------------------------------------------------------------------------------------------------------------------------
//                                                    SEQUENCE ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

export class CombatSequence
{
    constructor(RenderSurface, OverlaySurface, NoticeSurface)
    {
        this.RenderSurface   = RenderSurface;
        this.OverlaySurface  = OverlaySurface;
        this.NoticeSurface   = NoticeSurface;
        this.StepAccumulator = 0.0;
        this.PreviousStamp   = 0.0;
        this.FrameRate       = 0.0;
        this.SuspendedState  = false;
    }

    async Initialize()
    {
        this.GeometryAtlas = AssembleGeometryAtlas(ChassisClassRegistry, ChassisClassOrder);
        this.CombatFieldInstance = new CombatField();
        this.Rasterization = await new SurfaceRasterization(this.RenderSurface).InitializeDevice(this.GeometryAtlas);
        this.InputSource   = new InputSubstrate(this.RenderSurface);
        this.Overlay       = new CombatOverlay(this.OverlaySurface);

        this.ConformSurfaceExtent();
        window.addEventListener("resize", () => this.ConformSurfaceExtent());
        this.CombatFieldInstance.PublishNotice("CLICK TO ENGAGE — POINTER LOCKED");
        return this;
    }

    // 📝 Both canvases track the device pixel ratio; the overlay would otherwise draw at CSS resolution and read soft.
    ConformSurfaceExtent()
    {
        const PixelRatio    = Math.min(window.devicePixelRatio || 1.0, 2.0);
        const SurfaceWidth  = Math.floor(window.innerWidth  * PixelRatio);
        const SurfaceHeight = Math.floor(window.innerHeight * PixelRatio);

        this.RenderSurface.width   = SurfaceWidth;
        this.RenderSurface.height  = SurfaceHeight;
        this.OverlaySurface.width  = SurfaceWidth;
        this.OverlaySurface.height = SurfaceHeight;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                DISCRETE REQUESTS
    //--------------------------------------------------------------------------------------------------------------------

    ResolveDiscreteRequests()
    {
        for (const KeyIdentity of this.InputSource.ExtractDiscreteRequests())
        {
            switch (KeyIdentity)
            {
                case "KeyF":
                    // Seize whichever chassis lies along the sight axis — the headline possession mechanic.
                    this.CombatFieldInstance.TransferPossessionAlongSight();
                    break;
                case "KeyE":
                    this.CombatFieldInstance.TransferPossessionForward(1);
                    break;
                case "KeyQ":
                    this.CombatFieldInstance.TransferPossessionForward(-1);
                    break;
                case "Tab":
                    this.CombatFieldInstance.CycleArmament(this.CombatFieldInstance.PossessedChassis());
                    break;
                case "Digit1":
                    this.SelectArmament("Cannon");
                    break;
                case "Digit2":
                    this.SelectArmament("Rocket");
                    break;
                case "Digit3":
                    this.SelectArmament("ArcMortar");
                    break;
                case "KeyP":
                    this.SuspendedState = !this.SuspendedState;
                    this.CombatFieldInstance.PublishNotice(this.SuspendedState ? "SUSPENDED" : "RESUMED");
                    break;
            }
        }
    }

    SelectArmament(ArmamentLabel)
    {
        const PossessedChassis = this.CombatFieldInstance.PossessedChassis();
        if (PossessedChassis.ArmamentReserves[ArmamentLabel] > 0)
        {
            PossessedChassis.ArmamentLabel = ArmamentLabel;
            this.CombatFieldInstance.PublishNotice(`ARMAMENT — ${ArmamentRegistry[ArmamentLabel].ArmamentLabel}`);
        }
        else
        {
            this.CombatFieldInstance.PublishNotice("ARMAMENT DEPLETED");
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                 SIGHT PLACEMENT
    //--------------------------------------------------------------------------------------------------------------------

    // First-person sight seated at the commander's cupola, aimed along the turret axis and barrel elevation.
    ResolveSightFootprint(PossessedChassis)
    {
        const ClassProfile  = PossessedChassis.ClassProfile;
        const TurretCosine  = Math.cos(PossessedChassis.TurretAngle);
        const TurretSine    = Math.sin(PossessedChassis.TurretAngle);
        const PitchCosine   = Math.cos(PossessedChassis.BarrelPitch);
        const PitchSine     = Math.sin(PossessedChassis.BarrelPitch);

        // Seat slightly behind the turret centre so the barrel enters frame, selling the first-person interior.
        const SightRecess = -ClassProfile.TurretRadius * 0.15;
        const SightPosition = [
            PossessedChassis.GroundPosition[0] + TurretSine * SightRecess,
            PossessedChassis.GroundPosition[1] + ClassProfile.SightElevation,
            PossessedChassis.GroundPosition[2] + TurretCosine * SightRecess
        ];
        const SightTarget = [
            SightPosition[0] + TurretSine * PitchCosine * 60.0,
            SightPosition[1] + PitchSine * 60.0,
            SightPosition[2] + TurretCosine * PitchCosine * 60.0
        ];
        return { SightPosition, SightTarget };
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                              INSTANCE ACCUMULATION
    //--------------------------------------------------------------------------------------------------------------------

    AccumulateFieldInstances(PossessedChassis)
    {
        const Rasterization = this.Rasterization;
        const CombatFieldInstance = this.CombatFieldInstance;
        Rasterization.ResetInstanceAccumulator();

        Rasterization.AccumulatePart("Terrain", [0, 0, 0], 0.0, 0.0, [1, 1, 1], TerrainTone, 0.0);

        // 📝 Obstacles are accumulated by profile so the two shapes coalesce into two instanced draws rather than 46.
        for (const PyramidProfileState of [true, false])
        {
            for (const ObstacleEntry of CombatFieldInstance.ObstacleRoster)
            {
                if (ObstacleEntry.PyramidProfile !== PyramidProfileState) continue;
                Rasterization.AccumulatePart(
                    PyramidProfileState ? "ObstaclePyramid" : "ObstacleBlock",
                    ObstacleEntry.GroundOrigin, ObstacleEntry.YawAngle, 0.0,
                    [ObstacleEntry.ScaleFactor, ObstacleEntry.ScaleFactor, ObstacleEntry.ScaleFactor],
                    ObstacleTone, 0.0);
            }
        }

        for (const ChassisEntry of CombatFieldInstance.ChassisRoster)
        {
            const ClassProfile = ChassisEntry.ClassProfile;
            const ChassisTone  = ChassisEntry.DestroyedState ? [0.16, 0.16, 0.17] : ClassProfile.ChassisTone;

            // The possessed chassis hides its own hull — the sight sits inside it, so drawing it would fill the screen.
            const PossessedState = ChassisEntry === PossessedChassis;

            // A wreck settles into the ground and rolls onto its side.
            const WreckSink  = ChassisEntry.DestroyedState ? -0.55 : 0.0;
            const WreckRoll  = ChassisEntry.DestroyedState ? 0.42  : ChassisEntry.ChassisRoll;

            if (!PossessedState)
            {
                Rasterization.AccumulatePart(`Hull${ChassisEntry.ClassLabel}`,
                    [ChassisEntry.GroundPosition[0],
                     ChassisEntry.GroundPosition[1] + ClassProfile.HullExtent[1] + ClassProfile.TrackHeight * 0.5 + WreckSink,
                     ChassisEntry.GroundPosition[2]],
                    ChassisEntry.HeadingAngle, ChassisEntry.ChassisPitch, [1, 1, 1], ChassisTone, 0.0);

                Rasterization.AccumulatePart(`Turret${ChassisEntry.ClassLabel}`,
                    [ChassisEntry.GroundPosition[0],
                     ChassisEntry.GroundPosition[1] + ClassProfile.HullExtent[1] * 2.0 +
                        ClassProfile.TrackHeight * 0.5 + ClassProfile.TurretHeight * 0.5 + WreckSink,
                     ChassisEntry.GroundPosition[2]],
                    ChassisEntry.TurretAngle, 0.0, [1, 1, 1], ChassisTone, 0.0);
            }

            // The barrel is always drawn — on the possessed chassis it is the only visible part of the tank.
            const BarrelTone = ChassisEntry.MuzzleFlashTimer > 0.0
                ? ArmamentRegistry[ChassisEntry.ArmamentLabel].ProjectileTone : ChassisTone;
            const BarrelLift = ChassisEntry.MuzzleFlashTimer > 0.0 ? 2.4 : 0.0;

            const TrunnionHeight = ClassProfile.HullExtent[1] * 2.0 + ClassProfile.TrackHeight * 0.5 +
                                   ClassProfile.TurretHeight * 0.5 + WreckSink;
            Rasterization.AccumulatePart(`Barrel${ChassisEntry.ClassLabel}`,
                [ChassisEntry.GroundPosition[0] + Math.sin(ChassisEntry.TurretAngle) * ClassProfile.TurretRadius * 0.6,
                 ChassisEntry.GroundPosition[1] + TrunnionHeight,
                 ChassisEntry.GroundPosition[2] + Math.cos(ChassisEntry.TurretAngle) * ClassProfile.TurretRadius * 0.6],
                ChassisEntry.TurretAngle, ChassisEntry.BarrelPitch, [1, 1, 1], BarrelTone, BarrelLift);
        }

        for (const ProjectileEntry of CombatFieldInstance.ProjectileRoster)
        {
            const ArmamentProfile = ProjectileEntry.ArmamentProfile;
            Rasterization.AccumulatePart("ProjectileBolt", ProjectileEntry.WorldPosition, 0.0, 0.0,
                [ArmamentProfile.ProjectileRadius, ArmamentProfile.ProjectileRadius, ArmamentProfile.ProjectileRadius],
                ArmamentProfile.ProjectileTone, 3.2);
        }

        for (const CrateEntry of CombatFieldInstance.ArmamentCrates)
        {
            if (!CrateEntry.AvailableState) continue;
            const HoverOffset = Math.sin(CombatFieldInstance.ElapsedSeconds * PickupSpecification.HoverRate) *
                                PickupSpecification.HoverAmplitude;
            Rasterization.AccumulatePart("ArmamentCrate",
                [CrateEntry.GroundOrigin[0], CrateEntry.GroundOrigin[1] + HoverOffset, CrateEntry.GroundOrigin[2]],
                CombatFieldInstance.ElapsedSeconds * 0.9, 0.0,
                [PickupSpecification.CrateHalfExtent, PickupSpecification.CrateHalfExtent,
                 PickupSpecification.CrateHalfExtent],
                ArmamentRegistry[CrateEntry.ArmamentLabel].ProjectileTone, 1.3);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    FRAME LOOP
    //--------------------------------------------------------------------------------------------------------------------

    // 🔴 Fixed-step integration with an accumulator: a variable step would make ballistics and pursuit lead
    //    frame-rate dependent, so shots that hit on a fast machine would miss on a slow one.
    AdvanceFrame(FrameStamp)
    {
        const FrameDelta = Math.min((FrameStamp - this.PreviousStamp) / 1000.0, AccumulatorCeiling);
        this.PreviousStamp = FrameStamp;
        if (FrameDelta > 0.0) this.FrameRate = this.FrameRate * 0.9 + (1.0 / FrameDelta) * 0.1;

        this.ResolveDiscreteRequests();
        const DriveIntent = this.InputSource.DecodeDriveIntent();

        if (!this.SuspendedState)
        {
            this.StepAccumulator += FrameDelta;
            let StepBudget = 0;
            while (this.StepAccumulator >= IntegrationStep && StepBudget < 8)
            {
                // Pointer sweep is a per-frame quantity, so it is applied on the first step only and zeroed after.
                const StepIntent = StepBudget === 0 ? DriveIntent
                    : { ...DriveIntent, TurretSweep: 0.0, PitchSweep: 0.0 };
                this.CombatFieldInstance.IntegrateField(StepIntent, IntegrationStep);
                this.StepAccumulator -= IntegrationStep;
                StepBudget += 1;
            }
        }

        const PossessedChassis = this.CombatFieldInstance.PossessedChassis();

        // A destroyed possessed chassis hands the sight to the nearest survivor rather than stranding the view.
        if (PossessedChassis.DestroyedState) this.CombatFieldInstance.TransferPossessionForward(1);

        const SightFootprint = this.ResolveSightFootprint(this.CombatFieldInstance.PossessedChassis());
        this.AccumulateFieldInstances(this.CombatFieldInstance.PossessedChassis());
        this.Rasterization.UploadViewUniform(SightFootprint.SightPosition, SightFootprint.SightTarget,
                                             this.CombatFieldInstance.ElapsedSeconds, FogDensity);
        this.Rasterization.SubmitFrame();
        this.Overlay.RenderOverlay(this.CombatFieldInstance, this.FrameRate);

        requestAnimationFrame(NextStamp => this.AdvanceFrame(NextStamp));
    }

    Activate()
    {
        this.PreviousStamp = performance.now();
        requestAnimationFrame(FrameStamp => this.AdvanceFrame(FrameStamp));
    }
}

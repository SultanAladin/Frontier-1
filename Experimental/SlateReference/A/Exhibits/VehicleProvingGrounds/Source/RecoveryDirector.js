//============================================================================================================================================
//                                                           RECOVERYDIRECTOR.JS
//============================================================================================================================================
// 🧩 Getting the car back: an overturned, spinning-out or lost car (or R, or a teleport) derezzes, is moved, and rezzes in.

// 📝 One sequence for every way the car needs putting back:
//
//      ① Derez — the car dissolves cell by cell; with the VFX editor's Momentum on (the default) it carries on under
//         the solver, coasting with no driver input, and every cube leaves with the car's velocity at its release —
//         otherwise it freezes where it is (keeping the velocity its cubes inherit);
//      ② Hidden — gone from the raster, the distance field and the cards for a beat;
//      ③ Move — the car is respawned at the destination: upright where it lay for a flip, the last safe spot on the
//         pad for a car that left it, the drill's start or the spawn for R, the chosen place for a teleport;
//      ④ Rez — still frozen, it assembles from cubes flying in, on fresh tyres;
//      ⑤ Control — the solver runs again.
//
//    Triggers: upside down (or on its side) and nearly still for 1.5 s; airborne for 2.5 s, or airborne and tumbling
//    hard for 0.9 s; outside the grounds (35 m past the pad) or below them. The old 3 s overturn respawn in the
//    vehicle solver is gone — this replaces it.

import { CourseConstants, SpawnPoint } from "./CourseStructure.js";
import { VehicleGeometry } from "./VehicleSpecification.js";
import { RotateVector, LengthVector } from "./TransformProjection.js";

export const RecoveryReasons = Object.freeze({ Overturned: "Overturned", SpinOut: "Out of control", OutOfBounds: "Out of bounds",
                                               Respawn: "Respawn", Teleport: "Teleport" });

export const RecoverySettingsDefaults = Object.freeze({
    Enabled:        true,    // [-] automatic triggers (R and teleports always derez)
    OverturnSeconds: 1.5,    // [s]
    AirborneSeconds: 2.5,    // [s]
    TumbleSeconds:  0.9,     // [s]
    TumbleRate:     5.0,     // [rad/s]
    HiddenSeconds:  0.35     // [s]
});

export class RecoveryDirector
{
    constructor(Host)
    {
        this.Host     = Host;
        this.Settings = { ...RecoverySettingsDefaults };
        this.Stage    = "control";   // control | derez | hidden | rez
        this.Timer    = 0.0;
        this.Clock    = 0.0;
        this.Overturned = 0.0;
        this.Airborne = 0.0;
        this.Tumbling = 0.0;
        this.Safe     = { Position: SpawnPoint.Position.slice(), Yaw: SpawnPoint.Yaw };
        this.SafeTimer = 0.0;
        this.Pending  = null;
        this.Metrics  = { Recoveries: 0, Last: "" };
    }

    get Busy()
    {
        return this.Stage !== "control";
    }

    // 📝 Before the vehicle steps: watch for the triggers; run the sequence's clock.
    Advance(Seconds)
    {
        const Vehicle = this.Host.Vehicle;
        this.Clock += Seconds;
        if (this.Stage === "control")
        {
            this.Watch(Vehicle, Seconds);
            return;
        }
        this.Timer -= Seconds;
        if (this.Timer > 0.0)
            return;
        if (this.Stage === "derez")
        {
            this.Stage = "hidden";
            this.Timer = this.Settings.HiddenSeconds;
            this.Host.Effects.HideVehicle();
            this.Host.Effects.StopPoses();
            Vehicle.Frozen = true;
            Vehicle.Coasting = false;
            this.Host.AssignVehicleHidden(true);
        }
        else if (this.Stage === "hidden")
        {
            const Destination = this.Pending.Destination;
            Vehicle.Respawn([Destination.Position[0], Destination.Position[1], VehicleGeometry.CoMHeight + 0.02], Destination.Yaw);
            Vehicle.Frozen = true;
            if (this.Host.RestoreTyres)
                this.Host.RestoreTyres();
            this.Host.AfterVehicleMoved();
            this.Host.AssignVehicleHidden(false);
            this.Stage = "rez";
            this.Timer = this.Host.Effects.RezVehicle(Vehicle);
        }
        else if (this.Stage === "rez")
        {
            this.Host.Effects.ClearVehicleMask();
            Vehicle.Frozen = false;
            Vehicle.Hidden = false;
            this.Stage = "control";
            const Done = this.Pending && this.Pending.Done;
            this.Pending = null;
            if (Done)
                Done();
        }
    }

    Watch(Vehicle, Seconds)
    {
        const Up = RotateVector(Vehicle.Orientation, [0.0, 0.0, 1.0]);
        const Speed = LengthVector(Vehicle.LinearVelocity);
        const Spin = LengthVector(Vehicle.AngularVelocity);
        const Contacts = Vehicle.Wheels.filter((Wheel) => Wheel.Contact).length;
        const P = Vehicle.Position;
        // 📝 The last place the car sat squarely on the pad, every half second.
        this.SafeTimer += Seconds;
        if (this.SafeTimer > 0.5 && Contacts === 4 && Up[2] > 0.9 && this.WithinPad(P, 0.0))
        {
            this.SafeTimer = 0.0;
            const Forward = RotateVector(Vehicle.Orientation, [1.0, 0.0, 0.0]);
            this.Safe = { Position: [P[0], P[1]], Yaw: Math.atan2(Forward[1], Forward[0]) };
        }
        if (!this.Settings.Enabled)
            return;
        this.Overturned = Up[2] < 0.3 && Speed < 2.5 ? this.Overturned + Seconds : 0.0;
        this.Airborne = Contacts === 0 ? this.Airborne + Seconds : 0.0;
        this.Tumbling = Contacts === 0 && Spin > this.Settings.TumbleRate ? this.Tumbling + Seconds : 0.0;
        if (this.Overturned > this.Settings.OverturnSeconds)
            this.Begin(RecoveryReasons.Overturned, this.ResolveUpright(Vehicle));
        else if (this.Airborne > this.Settings.AirborneSeconds || this.Tumbling > this.Settings.TumbleSeconds)
            this.Begin(RecoveryReasons.SpinOut, this.WithinPad(P, 0.0) ? this.ResolveUpright(Vehicle) : this.Safe);
        else if (!this.WithinPad(P, 35.0) || P[2] < -3.0)
            this.Begin(RecoveryReasons.OutOfBounds, this.Safe);
    }

    WithinPad(Position, Margin)
    {
        const C = CourseConstants;
        return Position[0] > C.PadMinimum[0] - Margin && Position[0] < C.PadMaximum[0] + Margin &&
               Position[1] > C.PadMinimum[1] - Margin && Position[1] < C.PadMaximum[1] + Margin;
    }

    // 📝 Upright where it lies, keeping its heading.
    ResolveUpright(Vehicle)
    {
        const Forward = RotateVector(Vehicle.Orientation, [1.0, 0.0, 0.0]);
        const Heading = Math.hypot(Forward[0], Forward[1]) > 0.2 ? Math.atan2(Forward[1], Forward[0]) : this.Safe.Yaw;
        return { Position: [Vehicle.Position[0], Vehicle.Position[1]], Yaw: Heading };
    }

    // 📝 Starts the sequence toward Destination { Position [x, y], Yaw [rad] }; Done runs once the car has control.
    Begin(Reason, Destination, Done = null)
    {
        if (this.Busy)
            return false;
        const Vehicle = this.Host.Vehicle;
        this.Pending = { Reason, Destination, Done };
        this.Stage = "derez";
        this.Timer = this.Host.Effects.DerezVehicle(Vehicle);
        const Momentum = !!this.Host.Effects.Settings.Momentum;
        Vehicle.Frozen = !Momentum;
        Vehicle.Coasting = Momentum;
        this.Overturned = this.Airborne = this.Tumbling = 0.0;
        this.Metrics.Recoveries++;
        this.Metrics.Last = Reason;
        if (this.Host.Shell)
            this.Host.Shell.Announce(Reason === RecoveryReasons.Teleport ? "Derezzing…" : `${Reason} — derezzing`);
        return true;
    }

    QueryReadout()
    {
        return [`stage: ${this.Stage}${this.Busy ? ` (${Math.max(0.0, this.Timer).toFixed(1)} s)` : ""} · recoveries ${this.Metrics.Recoveries}${this.Metrics.Last ? ` · last: ${this.Metrics.Last}` : ""}`,
                `safe spot: ${this.Safe.Position[0].toFixed(0)}, ${this.Safe.Position[1].toFixed(0)}`];
    }
}

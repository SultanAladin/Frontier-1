//============================================================================================================================================
//                                                           TRAININGSEQUENCE.JS
//============================================================================================================================================
// 🧩 The training arena's drills: timing gates, penalties, scores and best times — Apex Legends' Firing Range, for driving.

// 📝 Every station is live all the time: drive through a start gate (or into a zone) and its drill starts, the HUD
//    shows it running, and the result lands in the HUD, the Training page and — when it beats the best — the
//    browser's storage. Starting a drill from the Training page derezzes the car to its start and rezzes the cones
//    the last run knocked over back onto their marks.
//
//      launch   62 m sprint (60 ft and 0–100 splits), trap speed at the brake line; counts only stopped in the box
//      slalom   gate to gate, +1 s per displaced cone
//      moose    ISO 3888-2 lane change: entry speed and a clean pass (any cone fails it)
//      bowling  pins down, 3 s after the foul line (a strike is all 15)
//      spin     full turns inside the box without touching a cone or leaving it
//      drift    angle × speed × time while sliding, chained with a growing multiplier; a cone hit loses the chain
//      skidpad  the same score on the 14 m ring
//      jump     airtime and distance past the lip
//      smash    props broken and impact energy, alley entrance to exit
//      burnout  seconds of held burnout and the hottest tyre

import { TrainingDrills } from "./LevelStructure.js";
import { RotateVector, LengthVector } from "./TransformProjection.js";

const StorageKey = "FrontierRange.Best";

export class TrainingSequence
{
    constructor(Host)
    {
        this.Host    = Host;
        this.Drills  = TrainingDrills;
        this.Active  = null;    // { Drill, Phase, Clock, ... }
        this.Results = {};      // drill id → [latest first]
        this.Best    = this.RetrieveBest();
        this.Previous = null;   // car centre last frame
        this.Clock   = 0.0;
        this.ConstructHud();
    }

    RetrieveBest()
    {
        try { return JSON.parse(localStorage.getItem(StorageKey) || "{}") || {}; }
        catch { return {}; }
    }

    StoreBest()
    {
        try { localStorage.setItem(StorageKey, JSON.stringify(this.Best)); }
        catch { /* storage unavailable: best times last the session */ }
    }

    QueryActiveDrill()
    {
        return this.Active ? this.Active.Drill : null;
    }

    QueryDrill(Id)
    {
        return this.Drills.find((Drill) => Drill.Id === Id);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  STARTING
    //--------------------------------------------------------------------------------------------------------------------

    // 📝 From the Training page: derez to the start, rez the drill's cones, arm it.
    StartDrill(Id)
    {
        const Drill = this.QueryDrill(Id);
        if (!Drill)
            return;
        this.Host.Cones.ResetCones(Drill.Groups, true);
        this.Arm(Drill);
        this.Host.TeleportVehicle({ Name: Drill.Name, Position: Drill.Teleport.Position, Yaw: Drill.Teleport.Yaw }, false, () => this.Arm(Drill));
    }

    Arm(Drill)
    {
        this.Active = { Drill, Phase: "armed", Clock: 0.0, Score: 0.0, Chain: 0.0, Multiplier: 1.0, Since: 0.0, Splits: {}, Start: null };
        this.RefreshHud();
    }

    Cancel()
    {
        this.Active = null;
        this.RefreshHud();
    }

    // 📝 R during a drill: back to its start, armed again.
    OnRespawn()
    {
        if (this.Active)
            this.Arm(this.Active.Drill);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    FRAME
    //--------------------------------------------------------------------------------------------------------------------

    Advance(Seconds)
    {
        const Vehicle = this.Host.Vehicle;
        this.Clock += Seconds;
        const Position = Vehicle.Position.slice();
        const Previous = this.Previous || Position;
        this.Previous = Position;
        if (this.Host.Recovery && this.Host.Recovery.Busy)
        {
            this.RefreshHud();
            return;
        }
        const Forward = RotateVector(Vehicle.Orientation, [1.0, 0.0, 0.0]);
        const Speed = LengthVector(Vehicle.LinearVelocity);
        const State = { Vehicle, Position, Previous, Forward, Speed, Seconds };
        // 📝 Drive into a station and its drill starts on its own.
        if (!this.Active || this.Active.Phase === "finished")
            this.DetectStation(State);
        if (this.Active)
            this.AdvanceDrill(State);
        this.RefreshHud();
    }

    Crossed(Gate, State)
    {
        if (!Gate)
            return false;
        const Axis = Gate.Axis === "x" ? 0 : 1, Other = 1 - Axis;
        const Before = State.Previous[Axis] - Gate.At, After = State.Position[Axis] - Gate.At;
        const Across = State.Position[Other];
        return Before < 0.0 && After >= 0.0 && Across >= Gate.From && Across <= Gate.To;
    }

    Within(Zone, Position)
    {
        const Minimum = Zone.Minimum, Maximum = Zone.Maximum;
        return Position[0] >= Minimum[0] && Position[0] <= Maximum[0] && Position[1] >= Minimum[1] && Position[1] <= Maximum[1];
    }

    DetectStation(State)
    {
        for (const Drill of this.Drills)
        {
            let Enter = false;
            if (Drill.Start && Drill.Id !== "launch")
                Enter = this.Crossed(Drill.Start, State);
            else if (Drill.Id === "launch")
                Enter = this.Crossed(Drill.Start, State);
            else if (Drill.Box)
                Enter = this.Within(Drill.Box, State.Position) && !this.Within(Drill.Box, State.Previous);
            else if (Drill.Zone)
                Enter = this.Within(Drill.Zone, State.Position) && !this.Within(Drill.Zone, State.Previous);
            else if (Drill.Lip !== undefined)
                Enter = State.Previous[0] < Drill.Lip && State.Position[0] >= Drill.Lip && Math.abs(State.Position[1]) < 3.4;
            if (!Enter)
                continue;
            this.Arm(Drill);
            this.Begin(State);
            return;
        }
    }

    // 📝 The clock starts: the gate was crossed (or the zone entered, or the lip left).
    Begin(State)
    {
        const Run = this.Active;
        Run.Phase = "running";
        Run.Clock = 0.0;
        Run.Start = State.Position.slice();
        Run.EntrySpeed = State.Speed;
        Run.Hits = this.Host.Cones.CountDisplaced(Run.Drill.Groups);
        Run.Breaks = this.Host.Fracture.Metrics.Breaks;
        Run.Yaw = Math.atan2(State.Forward[1], State.Forward[0]);
        Run.Turned = 0.0;
        Run.Airborne = false;
    }

    AdvanceDrill(State)
    {
        const Run = this.Active;
        const Drill = Run.Drill;
        if (Run.Phase === "armed")
        {
            const Gate = Drill.Start;
            const Starts = (Gate && this.Crossed(Gate, State)) || (Drill.Box && this.Within(Drill.Box, State.Position)) ||
                           (Drill.Zone && this.Within(Drill.Zone, State.Position)) ||
                           (Drill.Lip !== undefined && State.Previous[0] < Drill.Lip && State.Position[0] >= Drill.Lip);
            if (Starts)
                this.Begin(State);
            return;
        }
        if (Run.Phase !== "running")
            return;
        Run.Clock += State.Seconds;
        const Hits = this.Host.Cones.CountDisplaced(Drill.Groups) - (Run.Hits || 0);
        switch (Drill.Id)
        {
            case "launch":  this.AdvanceLaunch(Run, Drill, State, Hits); break;
            case "slalom":
            case "moose":   this.AdvanceGated(Run, Drill, State, Hits); break;
            case "bowling": this.AdvanceBowling(Run, Drill, State); break;
            case "spin":    this.AdvanceSpin(Run, Drill, State, Hits); break;
            case "drift":
            case "skidpad": this.AdvanceDrift(Run, Drill, State, Hits); break;
            case "jump":    this.AdvanceJump(Run, Drill, State); break;
            case "smash":   this.AdvanceSmash(Run, Drill, State); break;
            case "burnout": this.AdvanceBurnout(Run, Drill, State); break;
        }
    }

    AdvanceLaunch(Run, Drill, State, Hits)
    {
        const Y = State.Position[1];
        if (Run.Splits.SixtyFoot === undefined && Y >= Drill.SixtyFoot)
            Run.Splits.SixtyFoot = Run.Clock;
        if (Run.Splits.Hundred === undefined && State.Speed >= 100.0 / 3.6)
            Run.Splits.Hundred = Run.Clock;
        if (Run.Splits.Trap === undefined && Y >= Drill.Brake)
        {
            Run.Splits.Trap = State.Speed * 3.6;
            Run.Splits.BrakeAt = Run.Clock;
        }
        if (Run.Splits.Trap !== undefined && State.Speed < 0.3)
        {
            const Stop = Y - Drill.Brake;
            const Lane = Drill.Lanes.find((X) => Math.abs(State.Position[0] - X) <= Drill.LaneHalf);
            const InBox = Lane !== undefined && Y >= Drill.StopBox[0] && Y <= Drill.StopBox[1];
            // 📝 The score is the sprint to the brake line; it counts only if the car then stops inside a stop box.
            this.Finish({ Primary: Run.Splits.BrakeAt, Unit: "s", Lower: true,
                          Lines: [`60 ft ${Format(Run.Splits.SixtyFoot)} s`, Run.Splits.Hundred !== undefined ? `0–100 ${Format(Run.Splits.Hundred)} s` : "",
                                  `trap ${Run.Splits.Trap.toFixed(0)} km/h`, `stopped in ${Stop.toFixed(1)} m${InBox ? " · in the box ✓" : " · missed the box"}`,
                                  Hits > 0 ? `${Hits} cones` : "clean"].filter(Boolean),
                          Valid: InBox });
        }
        else if (Run.Clock > 40.0 || Math.abs(State.Position[0] - 76.0) > 14.0)
            this.Finish({ Primary: Run.Clock, Unit: "s", Lines: ["left the strip"], Valid: false });
    }

    AdvanceGated(Run, Drill, State, Hits)
    {
        if (this.Crossed(Drill.Finish, State))
        {
            if (Drill.Id === "moose")
                this.Finish({ Primary: Run.EntrySpeed * 3.6, Unit: "km/h", Lower: false,
                              Lines: [`entry ${(Run.EntrySpeed * 3.6).toFixed(1)} km/h`, `${Run.Clock.toFixed(2)} s`, Hits > 0 ? `${Hits} cones — failed` : "clean pass ✓"],
                              Valid: Hits === 0 });
            else
                this.Finish({ Primary: Run.Clock + Hits, Unit: "s", Lower: true,
                              Lines: [`${Run.Clock.toFixed(2)} s`, Hits > 0 ? `+${Hits} s (${Hits} cones)` : "clean ✓"], Valid: true });
        }
        else if (Run.Clock > 60.0 || State.Position[1] < Drill.Start.At - 6.0)
            this.Finish({ Primary: Run.Clock, Unit: "s", Lines: ["abandoned"], Valid: false });
    }

    AdvanceBowling(Run, Drill, State)
    {
        Run.Since += State.Seconds;
        if (Run.Since > 3.0 && (State.Speed < 1.0 || Run.Since > 6.0))
        {
            const Pins = this.Host.Cones.CountDisplaced(Drill.Groups);
            this.Finish({ Primary: Pins, Unit: "pins", Lower: false,
                          Lines: [Pins === 15 ? "STRIKE!" : `${Pins} of 15`, `at ${(Run.EntrySpeed * 3.6).toFixed(0)} km/h`], Valid: true });
        }
    }

    AdvanceSpin(Run, Drill, State, Hits)
    {
        const Yaw = Math.atan2(State.Forward[1], State.Forward[0]);
        let Delta = Yaw - Run.Yaw;
        Delta = Math.atan2(Math.sin(Delta), Math.cos(Delta));
        Run.Yaw = Yaw;
        Run.Turned += Delta;
        Run.Score = Math.floor(Math.abs(Run.Turned) / (2.0 * Math.PI));
        if (Hits > 0 || !this.Within(Drill.Box, State.Position))
            this.Finish({ Primary: Run.Score, Unit: "spins", Lower: false,
                          Lines: [`${Run.Score} × 360°`, Hits > 0 ? "touched a cone" : "left the box"], Valid: Run.Score > 0 });
        else if (Run.Clock > 30.0)
            this.Finish({ Primary: Run.Score, Unit: "spins", Lower: false, Lines: [`${Run.Score} × 360° in 30 s`], Valid: true });
    }

    // 📐 Drift score: while the slip angle between heading and velocity exceeds 12° above 20 km/h, angle [°] ×
    //    speed [m/s] × Δt accrues into the chain, × a multiplier that grows a step every 2 s of unbroken drift (to ×5).
    //    A second without drifting banks the chain; a cone hit loses it.
    AdvanceDrift(Run, Drill, State, Hits)
    {
        const Velocity = State.Vehicle.LinearVelocity;
        const Planar = Math.hypot(Velocity[0], Velocity[1]);
        let Angle = 0.0;
        if (Planar > 20.0 / 3.6)
        {
            const Cosine = (Velocity[0] * State.Forward[0] + Velocity[1] * State.Forward[1]) / (Planar * Math.hypot(State.Forward[0], State.Forward[1]));
            Angle = Math.acos(Math.max(-1.0, Math.min(1.0, Cosine))) * 180.0 / Math.PI;
        }
        Run.Angle = Angle;
        if (Hits > (Run.LastHits || 0))
        {
            Run.Chain = 0.0;
            Run.Multiplier = 1.0;
            Run.Drifting = 0.0;
            Run.LastHits = Hits;
            this.Host.Shell.Announce("Cone! Chain lost");
        }
        if (Angle > 12.0 && Angle < 120.0)
        {
            Run.Drifting = (Run.Drifting || 0.0) + State.Seconds;
            Run.Since = 0.0;
            Run.Multiplier = Math.min(5.0, 1.0 + Math.floor(Run.Drifting / 2.0));
            Run.Chain += Angle * Planar * State.Seconds * Run.Multiplier;
        }
        else
        {
            Run.Since += State.Seconds;
            if (Run.Since > 1.0 && Run.Chain > 0.0)
            {
                Run.Score += Run.Chain;
                Run.Best = Math.max(Run.Best || 0.0, Run.Chain);
                Run.Chain = 0.0;
                Run.Multiplier = 1.0;
                Run.Drifting = 0.0;
            }
        }
        if (!this.Within(Drill.Zone, State.Position) || Run.Clock > 90.0)
        {
            const Total = Run.Score + Run.Chain;
            this.Finish({ Primary: Math.round(Total), Unit: "pts", Lower: false,
                          Lines: [`${Math.round(Total)} points`, `best chain ${Math.round(Math.max(Run.Best || 0.0, Run.Chain))}`], Valid: Total > 0.0 });
        }
    }

    AdvanceJump(Run, Drill, State)
    {
        const Contacts = State.Vehicle.Wheels.filter((Wheel) => Wheel.Contact).length;
        if (!Run.Airborne && Contacts === 0)
        {
            Run.Airborne = true;
            Run.TakeOff = Run.Clock;
        }
        if (Run.Airborne && Contacts >= 2 && Run.Clock - Run.TakeOff > 0.15)
        {
            const Air = Run.Clock - Run.TakeOff;
            const Distance = State.Position[0] - Drill.Lip;
            this.Finish({ Primary: Distance, Unit: "m", Lower: false, Lines: [`${Distance.toFixed(1)} m`, `${Air.toFixed(2)} s airtime`], Valid: Air > 0.25 });
        }
        else if (Run.Clock > 6.0)
            this.Finish({ Primary: 0.0, Unit: "m", Lines: ["no jump"], Valid: false });
    }

    AdvanceSmash(Run, Drill, State)
    {
        if (!this.Within(Drill.Zone, State.Position))
        {
            const Broken = this.Host.Fracture.Metrics.Breaks - Run.Breaks;
            this.Finish({ Primary: Broken, Unit: "props", Lower: false, Lines: [`${Broken} props smashed`, `${Run.Clock.toFixed(1)} s in the alley`],
                          Valid: Broken > 0 });
        }
    }

    AdvanceBurnout(Run, Drill, State)
    {
        if (State.Vehicle.Burnout)
            Run.Score += State.Seconds;
        const Hottest = Math.max(...this.Host.Thermal.Wheels.map((Wheel) => Wheel.Surface)) - 273.15;
        Run.Hottest = Math.max(Run.Hottest || 0.0, Hottest);
        if (!this.Within(Drill.Zone, State.Position))
            this.Finish({ Primary: Run.Score, Unit: "s", Lower: false, Lines: [`${Run.Score.toFixed(1)} s of burnout`, `tyres ${Run.Hottest.toFixed(0)} °C`],
                          Valid: Run.Score > 0.5 });
    }

    // 📝 Result: { Primary, Unit, Lower (smaller is better), Lines, Valid }.
    Finish(Result)
    {
        const Run = this.Active;
        if (!Run || Run.Phase !== "running")
            return;
        Run.Phase = "finished";
        Run.Result = Result;
        const Id = Run.Drill.Id;
        (this.Results[Id] = this.Results[Id] || []).unshift({ ...Result, At: Date.now() });
        this.Results[Id].length = Math.min(this.Results[Id].length, 6);
        const Best = this.Best[Id];
        const Better = Result.Valid && (Best === undefined || (Result.Lower ? Result.Primary < Best.Primary : Result.Primary > Best.Primary));
        if (Better)
        {
            this.Best[Id] = { Primary: Result.Primary, Unit: Result.Unit, Lower: !!Result.Lower };
            this.StoreBest();
        }
        Run.NewBest = Better;
        this.Host.Shell.Announce(`${Run.Drill.Name}: ${FormatPrimary(Result)}${Better ? " — new best!" : ""}`);
        if (this.OnResult)
            this.OnResult(Id, Result);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                     HUD
    //--------------------------------------------------------------------------------------------------------------------

    ConstructHud()
    {
        const Stage = document.getElementById("stage");
        if (!Stage)
            return;
        this.Hud = document.createElement("div");
        this.Hud.id = "drill-hud";
        this.Hud.className = "hidden";
        this.HudParts = ["drill-hud-name", "drill-hud-primary", "drill-hud-lines", "drill-hud-best"].map((ClassName) =>
        {
            const Part = document.createElement("div");
            Part.className = ClassName;
            this.Hud.appendChild(Part);
            return Part;
        });
        Stage.appendChild(this.Hud);
    }

    RefreshHud()
    {
        if (!this.Hud)
            return;
        const Run = this.Active;
        this.Hud.classList.toggle("hidden", !Run);
        if (!Run)
            return;
        const [Name, Primary, Lines, Best] = this.HudParts;
        Name.textContent = Run.Drill.Name.toUpperCase();
        let Text = "", Detail = [];
        if (Run.Phase === "armed")
        {
            Text = "READY";
            Detail = [Run.Drill.Note];
        }
        else if (Run.Phase === "running")
        {
            if (Run.Drill.Id === "drift" || Run.Drill.Id === "skidpad")
            {
                Text = `${Math.round(Run.Score + Run.Chain)}`;
                Detail = [Run.Chain > 0.0 ? `chain ${Math.round(Run.Chain)} ×${Run.Multiplier.toFixed(0)}` : "slide it", `angle ${Math.round(Run.Angle || 0)}°`];
            }
            else if (Run.Drill.Id === "spin")
            {
                Text = `${Run.Score} × 360°`;
                Detail = [`${(Math.abs(Run.Turned) * 180.0 / Math.PI % 360).toFixed(0)}° into the next`];
            }
            else if (Run.Drill.Id === "burnout")
            {
                Text = `${Run.Score.toFixed(1)} s`;
                Detail = [`tyres ${(Run.Hottest || 0).toFixed(0)} °C`];
            }
            else
            {
                Text = `${Run.Clock.toFixed(2)} s`;
                Detail = Object.entries(Run.Splits).filter(([Key]) => Key !== "BrakeAt")
                               .map(([Key, Value]) => `${{ SixtyFoot: "60 ft", Hundred: "0–100", Trap: "trap" }[Key] || Key} ${Key === "Trap" ? `${Value.toFixed(0)} km/h` : `${Value.toFixed(2)} s`}`);
            }
        }
        else if (Run.Result)
        {
            Text = FormatPrimary(Run.Result);
            Detail = Run.Result.Lines;
        }
        Primary.textContent = Text;
        Lines.textContent = Detail.join("  ·  ");
        const Record = this.Best[Run.Drill.Id];
        Best.textContent = Record ? `best ${FormatPrimary(Record)}${Run.NewBest && Run.Phase === "finished" ? " — NEW" : ""}` : "";
        if (this.Hud.dataset)
            this.Hud.dataset.phase = Run.Phase;
    }

    QueryReadout()
    {
        const Run = this.Active;
        return [Run ? `${Run.Drill.Name}: ${Run.Phase}${Run.Phase === "running" ? ` · ${Run.Clock.toFixed(1)} s` : ""}` : "no drill running — drive into a station",
                `results this session: ${Object.values(this.Results).reduce((Sum, List) => Sum + List.length, 0)} · best times stored: ${Object.keys(this.Best).length}`];
    }

    ClearBest()
    {
        this.Best = {};
        this.StoreBest();
    }
}

function Format(Value)
{
    return Value === undefined ? "–" : Value.toFixed(2);
}

export function FormatPrimary(Result)
{
    if (Result.Unit === "s")
        return `${Result.Primary.toFixed(2)} s`;
    if (Result.Unit === "m")
        return `${Result.Primary.toFixed(1)} m`;
    if (Result.Unit === "km/h")
        return `${Result.Primary.toFixed(1)} km/h`;
    return `${Math.round(Result.Primary)} ${Result.Unit}`;
}

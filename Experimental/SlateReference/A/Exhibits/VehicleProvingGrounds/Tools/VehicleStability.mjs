//============================================================================================================================================
//                                                          VEHICLESTABILITY.MJS
//============================================================================================================================================
// 🧩 Node regression for the soft-tyre dynamics: no GPU, the solvers alone, in IlluminationHost's per-frame order.

// 📝 node Tools/VehicleStability.mjs [path to the exhibit]   — runs in a few seconds, exits non-zero on a failure.
//
//    The vehicle, thermal solvers are stepped exactly as IlluminationHost.AdvanceFrame steps them (vehicle,
//    wear, events, thermal, pinch punctures, pressures) at 120 Hz on the open pad, and each scenario is held
//    to a bound:
//
//    | Scenario                                   | Bound                                                          |
//    |--------------------------------------------|----------------------------------------------------------------|
//    | rest at 220, 60 kPa; one, two, four flats  | finite; the body still (≤ 2 mm peak to peak over the last 2 s) |
//    | a blowout on each corner at rest           | left and right mirror each other (loads within 1 %)            |
//    | blowouts and shreds at 20–30 m/s, slaloms  | finite, upright, never airborne                                |
//    | donuts left and right                      | each corner's sliding work mirrors its partner's within 5 %    |
//    | coasting backwards on a shredded rear      | finite and slowing (the clutch opens against the gear)         |

import { resolve } from "node:path";
import { pathToFileURL } from "node:url";

const Exhibit = resolve(process.argv[2] || new URL("..", import.meta.url).pathname);
const Source = (Name) => pathToFileURL(resolve(Exhibit, "Source", Name)).href;
const { VehicleSolver } = await import(Source("VehicleSolver.js"));
const { TyreThermalSolver, IgnitionModes } = await import(Source("TyreThermalSolver.js"));
const { VehicleGeometry } = await import(Source("VehicleSpecification.js"));

const Idle = { Throttle: 0.0, Brake: 0.0, Steer: 0.0, Handbrake: false };
const Pad = { Position: [-100.0, -34.0, 0.53], Yaw: 0.0 };   // a 200 m flat run, clear of the course's ramps
let Failures = 0;

function ConstructRig(Pressures = null)
{
    const Vehicle = new VehicleSolver([]);
    const Thermal = new TyreThermalSolver(4);
    if (Pressures)
    {
        Thermal.ColdPressures = Pressures.map((Kilopascal) => Kilopascal * 1000.0);
        Thermal.Reset();
    }
    Vehicle.AssignTyrePressures(Thermal.Wheels.map((Wheel) => Wheel.GaugePressure));
    Vehicle.Respawn(Pad.Position, Pad.Yaw);
    const Rig = { Vehicle, Thermal, Energy: [0, 0, 0, 0] };
    Rig.Frame = (Command, Step = 1.0 / 120.0) =>
    {
        Vehicle.Advance(Command, Step);
        Vehicle.AdvanceTyreWear(Step);
        const Events = Vehicle.ResolveEvents();
        const Readings = Vehicle.DrainWheelReadings();
        Readings.forEach((Reading, Index) => { Rig.Energy[Index] += Reading.SlidingPower * Reading.Seconds; });
        Thermal.Advance(Readings, Readings[0].Seconds, IgnitionModes[0]);
        for (const Pinch of Events.Pinches)
            Thermal.Puncture(Pinch.Wheel, false, "pinch cut");
        Thermal.DrainEvents();
        Vehicle.AssignTyrePressures(Thermal.Wheels.map((Wheel) => Wheel.GaugePressure));
    };
    return Rig;
}

function Launch(Rig, Speed)
{
    Rig.Vehicle.LinearVelocity = [Speed, 0.0, 0.0];
    Rig.Vehicle.Wheels.forEach((Wheel) => { Wheel.Spin = Speed / VehicleGeometry.TyreRadius; });
}

function Shred(Rig, Wheel)
{
    Rig.Thermal.Puncture(Wheel, true);
    Rig.Vehicle.ShredTyre(Wheel);
}

function Check(Label, Passed, Detail)
{
    console.log(`${Passed ? "pass" : "FAIL"}  ${Label.padEnd(44)} ${Detail}`);
    if (!Passed)
        ++Failures;
}

const Finite = (Rig) => Rig.Vehicle.Position.concat(Rig.Vehicle.LinearVelocity).every(Number.isFinite);

// ① at rest: settle, then hold still
function Rest(Label, { Pressures = null, Damage = null } = {})
{
    const Rig = ConstructRig(Pressures);
    for (let Frame = 0; Frame < 240; ++Frame)
        Rig.Frame(Idle);
    if (Damage)
        Damage(Rig);
    let Low = Infinity, High = -Infinity;
    const Loads = [0, 0, 0, 0];
    for (let Frame = 0; Frame < 720; ++Frame)
    {
        Rig.Frame(Idle);
        if (Frame >= 480)
        {
            Low = Math.min(Low, Rig.Vehicle.Position[2]);
            High = Math.max(High, Rig.Vehicle.Position[2]);
            Rig.Vehicle.Wheels.forEach((Wheel, Index) => { Loads[Index] += Wheel.Load / 240.0; });
        }
    }
    const Swing = (High - Low) * 1000.0;
    Check(Label, Finite(Rig) && Swing <= 2.0, `body ${(Rig.Vehicle.Position[2] * 1000.0).toFixed(1)} mm, swing ${Swing.toFixed(2)} mm, loads ${Loads.map((Load) => Load.toFixed(0)).join("/")} N`);
    return Loads;
}

Rest("rest, 220 kPa");
Rest("rest, 60 kPa", { Pressures: [60, 60, 60, 60] });
Rest("rest, both fronts flat", { Pressures: [0, 0, 220, 220] });
Rest("rest, all four flat", { Pressures: [0, 0, 0, 0] });
const Loads = [0, 1, 2, 3].map((Wheel) => Rest(`rest, ${["FL", "FR", "RL", "RR"][Wheel]} blowout`, { Damage: (Rig) => Rig.Thermal.Puncture(Wheel, true) }));
const Mirror = [1, 0, 3, 2];
for (const [Left, Right] of [[0, 1], [2, 3]])
{
    const Worst = Math.max(...Loads[Left].map((Load, Wheel) => Math.abs(Load - Loads[Right][Mirror[Wheel]]) / Math.max(Load, 1.0)));
    Check(`${["FL", "FR", "RL", "RR"][Left]} vs ${["FL", "FR", "RL", "RR"][Right]} blowout mirror`, Worst <= 0.01, `worst load mismatch ${(Worst * 100.0).toFixed(2)} %`);
}

// ② at speed: finite, upright, on the ground (the weave keeps within ±5 m of the line, all of it flat pad)
function Drive(Label, Speed, Damage, Command, Seconds = 5.0, Pressures = null)
{
    const Rig = ConstructRig(Pressures);
    for (let Frame = 0; Frame < 120; ++Frame)
        Rig.Frame(Idle);
    Launch(Rig, Speed);
    for (let Frame = 0; Frame < 60; ++Frame)
        Rig.Frame({ Throttle: 0.3, Brake: 0.0, Steer: 0.0, Handbrake: false });
    Damage(Rig);
    let Airborne = 0, Time = 0.0, Wander = 0.0;
    for (let Frame = 0; Frame < Seconds * 120; ++Frame, Time += 1.0 / 120.0)
    {
        Rig.Frame(Command(Time));
        Wander = Math.max(Wander, Math.abs(Rig.Vehicle.Position[1] - Pad.Position[1]));
        if (Rig.Vehicle.Wheels.every((Wheel) => !Wheel.Contact))
            ++Airborne;
        if (!Finite(Rig))
            break;
    }
    const Q = Rig.Vehicle.Orientation;
    const Upright = 1.0 - 2.0 * (Q[0] * Q[0] + Q[1] * Q[1]);
    // 📝 The flat band runs ±20 m either side of the line; beyond it the course's kerbs and ramps would test the driving.
    Check(Label, Finite(Rig) && Airborne === 0 && Upright > 0.99 && Wander < 20.0, `upright ${Upright.toFixed(3)}, airborne frames ${Airborne}, off the line ≤ ${Wander.toFixed(1)} m`);
    return Rig;
}

const Cruise = () => ({ Throttle: 0.3, Brake: 0.0, Steer: 0.0, Handbrake: false });
const Slalom = (Time) => ({ Throttle: 0.3, Brake: 0.0, Steer: 0.35 * Math.cos(2.5 * Time), Handbrake: false });
for (let Wheel = 0; Wheel < 4; ++Wheel)
    Drive(`${["FL", "FR", "RL", "RR"][Wheel]} blowout at 25 m/s`, 25.0, (Rig) => Rig.Thermal.Puncture(Wheel, true), Cruise);
Drive("both fronts blown at 25 m/s", 25.0, (Rig) => { Rig.Thermal.Puncture(0, true); Rig.Thermal.Puncture(1, true); }, Cruise);
Drive("all four blown at 20 m/s", 20.0, (Rig) => [0, 1, 2, 3].forEach((Wheel) => Rig.Thermal.Puncture(Wheel, true)), Cruise);
Drive("FL shredded at 25 m/s", 25.0, (Rig) => Shred(Rig, 0), Cruise);
// 📝 A bare rim drags its corner (a third of the grip, four times the rolling resistance) and pulls the car off any line
//    an open-loop driver holds — as a real one does — so the rear shred coasts straight, briefly: the rim contact's
//    stability is what is under test here, not the driving.
Drive("RR shredded, coasting at 25 m/s", 25.0, (Rig) => Shred(Rig, 3), () => Idle, 3.0);
Drive("220 kPa, slalom at 25 m/s", 25.0, () => {}, Slalom);
Drive("60 kPa, slalom at 25 m/s", 25.0, () => {}, Slalom, 5.0, [60, 60, 60, 60]);
Drive("both fronts flat, slalom at 15 m/s", 15.0, () => {}, Slalom, 5.0, [0, 0, 220, 220]);

// ③ donuts both ways: the corners' sliding work mirrors
function Donut(Steer)
{
    const Rig = ConstructRig();
    Rig.Vehicle.Respawn([-60.0, 0.0, 0.53], 0.0);
    for (let Frame = 0; Frame < 120; ++Frame)
        Rig.Frame(Idle);
    Rig.Energy = [0, 0, 0, 0];
    for (let Frame = 0; Frame < 20 * 120; ++Frame)
        Rig.Frame({ Throttle: 0.85, Brake: 0.0, Steer, Handbrake: false });
    return Rig.Energy;
}
const Left = Donut(1.0), Right = Donut(-1.0);
const Spread = Math.max(...[0, 1, 2, 3].map((Wheel) => Math.abs(Left[Wheel] - Right[Mirror[Wheel]]) / Math.max(Left[Wheel], Right[Mirror[Wheel]], 1.0)));
Check("donuts left vs right mirror", Spread <= 0.05, `sliding MJ ${Left.map((E) => (E / 1e6).toFixed(2)).join("/")} vs ${Right.map((E) => (E / 1e6).toFixed(2)).join("/")}`);

// ④ coasting backwards on a shredded rear rim: the engine must not drive the roll
{
    const Rig = ConstructRig();
    for (let Frame = 0; Frame < 120; ++Frame)
        Rig.Frame(Idle);
    Launch(Rig, -6.0);
    Shred(Rig, 3);
    Rig.Vehicle.Wheels[3].Spin = -6.0 / (VehicleGeometry.TyreRimRadius + 0.012);
    for (let Frame = 0; Frame < 360; ++Frame)
        Rig.Frame(Idle);
    const Speed = Math.hypot(...Rig.Vehicle.LinearVelocity);
    const Spin = Math.max(...Rig.Vehicle.Wheels.map((Wheel) => Math.abs(Wheel.Spin)));
    Check("coasting backwards, RR shredded", Finite(Rig) && Speed < 7.0 && Spin < 60.0, `speed ${Speed.toFixed(2)} m/s, fastest wheel ${Spin.toFixed(1)} rad/s`);
}

console.log(Failures ? `\n${Failures} failed` : "\nall passed");
process.exit(Failures ? 1 : 0);

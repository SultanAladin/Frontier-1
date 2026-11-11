// The force field taxonomy, checked.
//
//     node Experimental/ParticleEditor/Engine/CheckForceFields.mjs
//
// The claim this file exists to defend is the one that decided the whole design: wind and gravity are
// different KINDS of quantity and cannot share a bucket. That claim is easy to write in a comment and
// easy to quietly break later, so it is asserted here with numbers.
import {
  Contribution, Reach, Falloff, Falls, ForceFieldKinds, KindById, BaseField,
  Awake, ActsOn, Reaching, SampleField, Resolve, Advance, Bakeable,
} from "./forcefields.js";

let Checks = 0;
function Check(Condition, Claim) {
  Checks++;
  if (!Condition) { console.error("  FAIL  " + Claim); process.exit(1); }
  console.log("  PASS  " + Claim);
}
function Banner(Title) { console.log("\n" + Title); }
const Near = (A, B, Slack = 1e-6) => Math.abs(A - B) <= Slack;

//---------------------------------------------------------------------------------------------------------
Banner("The umbrella describes what already exists, rather than replacing it");
{
  const Was = ForceFieldKinds.filter((One) => One.Was !== null);
  Check(Was.length === 8, "eight of the kinds are things the editor already had, not new inventions");
  Check(Was.filter((One) => One.Was.startsWith("wind component")).length === 4,
        "four are the wind components buildWind already sums into the lattice");
  Check(Was.filter((One) => One.Was.startsWith("force type")).length === 4,
        "and four are the force types fieldAccel already evaluates per particle");
  Check(Was.filter((One) => One.Was.startsWith("wind")).every((One) => One.Give === Contribution.Flow),
        "every wind component lands in Flow");
  Check(Was.filter((One) => One.Was.startsWith("force")).every((One) => One.Give === Contribution.Accelerate),
        "and every force type lands in Accelerate -- the split falls out of the existing code, it was "
        + "not imposed on it");
  Check(new Set(ForceFieldKinds.map((One) => One.Id)).size === ForceFieldKinds.length,
        "no kind is listed twice");
  Check(ForceFieldKinds.every((One) => Object.values(Contribution).includes(One.Give)),
        "and every kind contributes exactly one of the three");
}

//---------------------------------------------------------------------------------------------------------
Banner("🔴 Why gravity cannot simply be added to the wind");
{
  // Two receivers, identical but for how strongly the air drags them: a leaf and a hailstone.
  const Leaf = 6.0;      // [1/s] coupling
  const Stone = 0.05;
  const Step = 1 / 60;

  // Done correctly: gravity is an acceleration, so the coupling has no say in it.
  const Right = [{ ...BaseField("gravity"), Strength: 9.81 }];
  const Acting = Resolve(Right, [0, 2, 0], 0);
  const LeafRight = Advance([0, 0, 0], Acting, Leaf, Step);
  const StoneRight = Advance([0, 0, 0], Acting, Stone, Step);
  Check(Near(LeafRight[1], -9.81 * Step) && Near(StoneRight[1], -9.81 * Step),
        "as an acceleration, gravity gives the leaf and the hailstone the SAME downward change in one "
        + "step -- which is the entire content of Galileo's claim and is not negotiable");

  // Done the tempting way: gravity smuggled in as a downward "wind" of the same number.
  const Wrong = [{ ...BaseField("prevailing"), Direction: [0, -1, 0], Strength: 9.81,
                   Reaches: Reach.Everywhere }];
  const Mistaken = Resolve(Wrong, [0, 2, 0], 0);
  const LeafWrong = Advance([0, 0, 0], Mistaken, Leaf, Step);
  const StoneWrong = Advance([0, 0, 0], Mistaken, Stone, Step);
  Check(!Near(LeafWrong[1], StoneWrong[1], 1e-3),
        "❌ as a flow, the very same number moves them by different amounts, because a flow is relaxed "
        + "toward at the receiver's own coupling");
  Check(Math.abs(LeafWrong[1]) > Math.abs(StoneWrong[1]) * 50,
        "the leaf falls more than fifty times faster than the hailstone, which is backwards and would "
        + "read as a tuning problem rather than a category error");

  const Deaf = Advance([0, 0, 0], Mistaken, 0, Step);
  Check(Near(Deaf[1], 0),
        "❌ and a receiver with no wind coupling ignores it completely: smuggled-in gravity does not "
        + "reach anything the wind does not already move");
  const Heard = Advance([0, 0, 0], Acting, 0, Step);
  Check(Near(Heard[1], -9.81 * Step), "✔️ whereas real gravity reaches it, as it must");

  // And the other half: a flow really does have a terminal speed, which an acceleration does not.
  let Blown = [0, 0, 0], Pushed = [0, 0, 0];
  const Wind = Resolve([{ ...BaseField("prevailing"), Direction: [1, 0, 0], Strength: 8,
                          Reaches: Reach.Everywhere }], [0, 0, 0], 0);
  const Thrust = Resolve([{ ...BaseField("gravity"), Strength: 8 }], [0, 0, 0], 0);
  for (let Tick = 0; Tick < 600; Tick++) {
    Blown = Advance(Blown, Wind, 2.0, Step);
    Pushed = Advance(Pushed, Thrust, 2.0, Step);   // no flow field here, so coupling does nothing
  }
  Check(Near(Blown[0], 8, 0.01),
        "✔️ ten seconds in a wind leaves a receiver moving at the wind's speed and no faster -- a flow "
        + "has a terminal velocity built into its very definition");
  Check(Math.abs(Pushed[1]) > 70,
        "✔️ ten seconds of the same number as an acceleration is past 70 m/s and still climbing, because "
        + "nothing in an acceleration field bounds it. One bucket could not have produced both.");
}

//---------------------------------------------------------------------------------------------------------
Banner("Damping is the third, and is not either of the others");
{
  const Water = [{ ...BaseField("drag"), Strength: 4, Reaches: Reach.Everywhere }];
  const Acting = Resolve(Water, [0, 0, 0], 0);
  Check(Acting.Damp === 4 && Acting.Flow.every((One) => One === 0) && Acting.Accelerate.every((One) => One === 0),
        "a drag volume contributes a rate and no direction at all");
  const Slowed = Advance([10, 0, 0], Acting, 0, 0.25);
  Check(Near(Slowed[0], 10 * Math.exp(-1), 1e-9),
        "and scales velocity down exponentially, which cannot be written as a force without knowing the "
        + "receiver's mass");
  Check(Advance([0, 0, 0], Acting, 0, 0.25).every((One) => One === 0),
        "🔴 damping something already at rest does nothing -- unlike a force, it cannot start motion, "
        + "which is exactly why it is its own contribution and not a repulsor pointing backwards");
}

//---------------------------------------------------------------------------------------------------------
Banner("Reach, falloff and the time window");
{
  const Ball = { ...BaseField("attract"), Centre: [0, 0, 0], Radius: 10, Strength: 4, Fades: Falloff.None };
  Check(SampleField(Ball, [20, 0, 0], 0).every((One) => One === 0), "outside the radius a field gives nothing");
  Check(Near(SampleField(Ball, [5, 0, 0], 0)[0], -4),
        "inside it, an attractor pulls toward its centre at full strength when nothing fades");

  const Everywhere = { ...BaseField("gravity"), Reaches: Reach.Everywhere };
  Check(Near(SampleField(Everywhere, [9999, 9999, 9999], 0)[1], -9.81),
        "🔴 Everywhere is not a very large sphere: it reaches a point ten kilometres out, which is the "
        + "case a bounded lattice bake could never have represented");

  Check(Near(Falls(Falloff.None, 9, 10), 1), "None holds full strength to the edge");
  Check(Near(Falls(Falloff.Linear, 5, 10), 0.5), "Linear is half way at half way");
  Check(Near(Falls(Falloff.Smooth, 0, 10), 1) && Near(Falls(Falloff.Smooth, 10, 10), 0),
        "Smooth runs one to zero, and is the shape the wind kinds already used");
  Check(Falls(Falloff.InverseSquare, 10, 10) < Falls(Falloff.Linear, 10, 10) + 0.12,
        "and InverseSquare is near nothing at the edge without ever being negative");
  Check([Falloff.None, Falloff.Linear, Falloff.Smooth, Falloff.InverseSquare]
          .every((One) => Falls(One, 11, 10) >= 0),
        "no falloff goes negative past the edge, which would push instead of pull");

  const Timed = { ...BaseField("repel"), Begins: 2, Lasts: 1, Repeats: 5 };
  Check(!Awake(Timed, 1.9) && Awake(Timed, 2.5) && !Awake(Timed, 3.5),
        "a timed field is awake only inside its window");
  Check(Awake(Timed, 7.5) && !Awake(Timed, 9),
        "and again one period later, which is how the existing blast fields already behave");
  Check(!Awake({ ...Timed, Enabled: false }, 2.5), "a disabled field is never awake");
  Check(Awake({ ...BaseField("gravity"), Lasts: 0 }, 1e6),
        "and a field with no duration never stops, because gravity should not need a repeat period");
}

//---------------------------------------------------------------------------------------------------------
Banner("Who responds is configuration, not code");
{
  const Everything = BaseField("gravity");
  Check(ActsOn(Everything, []) && ActsOn(Everything, ["debris"]),
        "a field naming no channels acts on everything, so the simple case stays simple");

  const OnlyDebris = { ...BaseField("gravity"), Acts: ["debris"] };
  Check(ActsOn(OnlyDebris, ["debris"]) && !ActsOn(OnlyDebris, ["smoke"]),
        "🔴 and one naming a channel acts only on receivers in it -- 'this room is on the Moon, but only "
        + "for the rubble' is authored, never compiled in");
  Check(!ActsOn(OnlyDebris, []), "a receiver in no channel is not reached by a selective field");

  const Mixed = Resolve([OnlyDebris, { ...BaseField("lift"), Reaches: Reach.Everywhere, Strength: 2 }],
                        [0, 0, 0], 0, ["smoke"]);
  Check(Near(Mixed.Accelerate[1], 2),
        "resolving for smoke gets the lift and not the debris-only gravity, in one pass over one list");
}

//---------------------------------------------------------------------------------------------------------
Banner("What can be baked into the lattice, and what cannot");
{
  const Fields = [
    BaseField("prevailing"),
    BaseField("tornado"),
    BaseField("gravity"),
    BaseField("attract"),
    { ...BaseField("current"), Acts: ["water"] },
  ];
  const { Baked, Live } = Bakeable(Fields);
  Check(Baked.length === 2 && Baked.every((One) => KindById(One.Kind).Give === Contribution.Flow),
        "the unselective flow fields sum into one velocity lattice, however many there are");
  Check(Live.some((One) => One.Kind === "gravity") && Live.some((One) => One.Kind === "attract"),
        "the acceleration fields stay live, because a bounded texture cannot hold an unbounded pull");
  Check(Live.some((One) => One.Kind === "current"),
        "💡 and a flow field that names channels also stays live -- one shared texture cannot both "
        + "include and exclude the same receiver, which is the real reason selectivity costs something");
  Check(Baked.length + Live.length === Fields.length, "every field lands in exactly one of the two");
}

//---------------------------------------------------------------------------------------------------------
Banner("Order of application is fixed, because it is observable");
{
  const Both = Resolve([{ ...BaseField("prevailing"), Direction: [1, 0, 0], Strength: 10,
                          Reaches: Reach.Everywhere },
                        { ...BaseField("gravity"), Strength: 10 }], [0, 0, 0], 0);
  const After = Advance([0, 0, 0], Both, 1.0, 0.5);
  Check(Near(After[0], 5) && Near(After[1], -5),
        "flow relaxation first, then acceleration, then damping: with a half-second step this is 5 m/s "
        + "across and 5 m/s down, and any other order gives different numbers");
  const Still = Advance([0, 0, 0], Resolve([], [0, 0, 0], 0), 1.0, 0.5);
  Check(Still.every((One) => One === 0), "and no fields at all leaves a receiver exactly as it was");
}

//---------------------------------------------------------------------------------------------------------
Banner("🔴 The absence of wind is not a wind of zero");
{
  // Found by the check above, not by reading the code: relaxing toward a flow of [0,0,0] is a brake, so
  // an empty field list silently slowed everything to a stop. Resolve now reports whether any flow field
  // actually reaches the point, and Advance relaxes only then.
  const Nothing = Resolve([], [0, 0, 0], 0);
  Check(Nothing.Flowing === false, "with no fields at all, nothing is flowing");
  const Coasting = Advance([10, 0, 0], Nothing, 5.0, 0.5);
  Check(Near(Coasting[0], 10),
        "so a fast receiver with a high coupling coasts untouched -- empty space does not brake it");

  const Far = Resolve([{ ...BaseField("gust"), Centre: [500, 0, 0], Radius: 10 }], [0, 0, 0], 0);
  Check(Far.Flowing === false && Near(Advance([10, 0, 0], Far, 5.0, 0.5)[0], 10),
        "and neither does a gust a long way away, which before this was indistinguishable from still air");

  const Calm = Resolve([{ ...BaseField("prevailing"), Strength: 0, Reaches: Reach.Everywhere }], [0, 0, 0], 0);
  Check(Calm.Flowing === true,
        "✔️ but air that is genuinely still IS flowing, at zero");
  Check(Advance([10, 0, 0], Calm, 5.0, 0.5)[0] < 10,
        "and it does slow a receiver down, because that is what still air does -- the distinction is "
        + "whether somebody put air there, not what the arithmetic happened to come out as");

  const Asleep = Resolve([{ ...BaseField("gust"), Reaches: Reach.Everywhere, Begins: 10, Lasts: 1 }],
                         [0, 0, 0], 0);
  Check(Asleep.Flowing === false, "a flow field outside its time window is not air either");
}

console.log("\nPASS " + Checks);

//---------------------------------------------------------------------------------------------------------
// 🔴 THE MIGRATION CHANGED NOTHING.
//
// Wind components and force fields were two arrays with two shapes edited in two panels. They are one
// list now. The only way that is a refactor rather than a rewrite is if the payloads the GPU receives are
// the same ones it received before, so this asserts the new list against the original hand-written data
// still sitting in presets.js, field by field, rather than against a description of it.
//---------------------------------------------------------------------------------------------------------
import { PE } from "./pe.js";
import "./fibres.js";
import "./presets.js";
import {
  Bearing, Along, LatticeComponent, ForceEntry, Packable, DefaultForces, ForGpu,
} from "./forcefields.js";

Banner("The default scene opens exactly as it did");
{
  const Original = PE.defaultWind().components;
  const Moved = ForGpu(DefaultForces()).Lattice;

  Check(Moved.length === Original.length, "the same number of flow fields as there were wind components");
  for (let At = 0; At < Original.length; At++) {
    const Was = Original[At], Now = Moved[At];
    Check(Now.type === Was.type && Now.x === Was.x && Now.z === Was.z && Now.radius === Was.radius
          && Now.strength === Was.strength && Now.freq === Was.freq && Now.enabled === Was.enabled,
          `component ${At} (${Was.name}) packs identically: type, position, radius, strength, rate, enabled`);
    Check(Near(Now.bearing, Was.bearing, 1e-9),
          `and its bearing survives the trip through a direction vector to within 1e-9 of a degree`);
  }
  Check(DefaultForces().map((One) => One.Name).join("|") === Original.map((One) => One.name).join("|"),
        "and they keep their names and their order, so the panel reads the same way it always did");
}

Banner("Bearings round-trip");
{
  for (const Degrees of [0, 70, 90, 180, 269, 359.5]) {
    Check(Near(Bearing({ Direction: Along(Degrees) }), Degrees, 1e-9),
          `${Degrees}\u00b0 survives being stored as a direction and read back`);
  }
  Check(Near(Bearing({ Direction: Along(-90) }), 270, 1e-9),
        "and a negative bearing comes back on the compass rather than as a negative number");
}

Banner("Every kind can reach the GPU, or is known not to");
{
  for (const Kind of ForceFieldKinds) {
    const Field = BaseField(Kind.Id);
    const Reached = Packable(Field);
    if (Kind.Give === Contribution.Damp || Kind.Id === "current") {
      Check(!Reached, `${Kind.Name} has no GPU path yet, and says so rather than silently doing nothing`);
      continue;
    }
    Check(Reached, `${Kind.Name} packs into a payload the existing shaders already read`);
    const Packed = Kind.Give === Contribution.Flow ? LatticeComponent(Field) : ForceEntry(Field);
    Check(Packed !== null && Number.isFinite(Packed.strength),
          `and comes out with a finite strength rather than a NaN the uniform packer would swallow`);
  }
}

Banner("🔴 Gravity packs as an unbounded field, not an eight-metre sphere");
{
  const Gravity = ForceEntry(BaseField("gravity"));
  Check(Gravity.radius === 0,
        "a radius of 0 is what the shader reads as 'everywhere' -- packing the authored 8 m would have "
        + "made gravity a small ball and looked like a physics bug for a week");
  Check(Gravity.strength === -9.81,
        "and it packs as a downward lift, because Lift is the one existing force type that is a fixed "
        + "world axis; the sign is the whole mapping");
  Check(ForceEntry(BaseField("lift")).strength > 0, "while lift itself still points up");
  Check(ForceEntry({ ...BaseField("attract"), Radius: 12 }).radius === 12,
        "a field that really is a sphere keeps its radius");
  Check(ForceEntry(BaseField("gravity")).duration >= 1e9,
        "and a field with no duration packs as effectively forever, as the old timed fields did");
}

Banner("Only awake acceleration fields are packed, but flow keeps its slot");
{
  const Fields = [
    { ...BaseField("prevailing"), Enabled: false },
    { ...BaseField("gust"), Enabled: true },
    { ...BaseField("attract"), Enabled: false },
    { ...BaseField("repel"), Enabled: true, Begins: 50, Lasts: 1 },
    { ...BaseField("lift"), Enabled: true },
  ];
  const { Lattice, Forces } = ForGpu(Fields, 0);
  Check(Lattice.length === 2 && Lattice[0].enabled === false,
        "🔴 a disabled flow field keeps its slot and carries its own flag -- buildWind reads a fixed "
        + "count, so dropping it would shift every field behind it into the wrong one");
  Check(Forces.length === 1 && Forces[0].type === 2,
        "whereas a disabled or sleeping acceleration field is simply not packed, as before");
}

console.log("\nPASS " + Checks);

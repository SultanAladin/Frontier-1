// 📦 Force fields: the umbrella that wind turned out to be one of.
//
// This is the design reference. It is written in the browser first, as the gas inspector was, because the
//    native port is read from it afterwards and a port needs something to be a port OF.
//
//========================================================================================================
// 🔴 THE DISTINCTION THAT MATTERS IS NOT WHAT A FIELD IS CALLED. IT IS WHAT A FIELD RETURNS.
//========================================================================================================
//
// "Rename Wind to Force Field and make gravity another one" is the right instinct and the wrong single
//    bucket, because wind and gravity are not the same kind of quantity and cannot be applied the same
//    way. Three kinds of contribution exist, and every field is exactly one of them:
//
//    FLOW          returns a VELOCITY, m/s. The receiver is dragged toward it:  v += (F(p) - v) * k*dt
//                  Wind is this. A leaf in an 8 m/s wind ends up moving at about 8 m/s and no faster;
//                  it does not accelerate forever. `k` is the receiver's own coupling -- a leaf has a
//                  high one, a hailstone a low one -- which is why the same wind moves them differently.
//
//    ACCELERATE    returns an ACCELERATION, m/s². Added straight to velocity:   v += F(p) * dt
//                  Gravity, attraction, repulsion, lift and the magnetic drive are this. There is no
//                  terminal speed; drag is what eventually balances it, and drag is separate.
//
//    DAMP          returns a RATE, 1/s. Scales velocity down:                   v *= exp(-F(p) * dt)
//                  Water, mud, treacle, a slow-motion volume. Not a force at all in the sense of the
//                  other two -- it removes energy rather than adding a direction -- but it belongs in
//                  the same entity because an author places and shapes it identically.
//
// ⚠️ WHY YOU CANNOT JUST PUT GRAVITY IN THE WIND. This is the trap the rename invites, so it is written
//    down. If gravity were added to the wind field, then a receiver with coupling k = 0 would IGNORE
//    gravity entirely, and one with a high k would reach the "gravity velocity" within a single frame and
//    then stop accelerating. Both are wrong, and both would look like a tuning problem rather than a
//    category error. CheckForceFields.mjs asserts this numerically rather than trusting the comment.
//
// 💡 WHY THE SPLIT ALSO PAYS FOR ITSELF. Flow fields can be summed into one 3D velocity texture once per
//    frame and then sampled in O(1) no matter how many of them there are -- which is exactly what
//    engine.js's buildWind pass already does with its four wind kinds. Acceleration fields cannot be
//    baked that way, because the useful ones (gravity, a planet's pull) are unbounded and a bounded box
//    would clip them; they are evaluated analytically per receiver, O(receivers x fields). So the two
//    want different machinery, and a single list would have had to pick one and be wrong for the other.
//
// 📝 WHAT THIS REPLACES. Nothing is renamed. Engine/DisplayPresentation/WindField.h keeps its name and
//    its job: it is the atmospheric model (Ekman shear and veer, gust envelope, curl turbulence) and it
//    is well tested, so it becomes the solver BEHIND the flow kinds rather than being replaced by them.
//    The umbrella sits above it.
import { PE } from "./pe.js";

// ─── What a field contributes ───────────────────────────────────────────────────────────────────────────

export const Contribution = {
  Flow: "flow",             // [m/s]  relax toward it at the receiver's coupling
  Accelerate: "accelerate", // [m/s²] add to velocity
  Damp: "damp",             // [1/s]  scale velocity down
};

// ─── Where a field reaches ──────────────────────────────────────────────────────────────────────────────

// A field's extent is separate from its kind, so that "gravity, but only inside this room" costs nothing
//    to express. Everywhere is not a sphere of huge radius: it skips the distance maths entirely and,
//    more importantly, it is the case a bounded bake CANNOT represent.
export const Reach = {
  Everywhere: "everywhere",
  Sphere: "sphere",
  Box: "box",
  Cone: "cone",
};

export const Falloff = {
  None: "none",                    // full strength to the edge, then nothing
  Linear: "linear",                // 1 - q
  Smooth: "smooth",                // (1 - q²)², the one the wind kinds already use
  InverseSquare: "inverse-square", // 1/(1 + q²)·, for anything claiming to be gravity-like
};

// 📦 How much of a field reaches a point: 0 outside, 1 at the centre.
export function Falls(Shape, Distance, Radius) {
  if (Shape === Falloff.None) return 1;
  const Span = Math.max(Radius, 1e-3);
  const q = Math.min(Math.max(Distance / Span, 0), 1);
  if (Shape === Falloff.Linear) return 1 - q;
  if (Shape === Falloff.InverseSquare) return 1 / (1 + 8 * q * q);
  const Soft = 1 - q * q;
  return Soft * Soft;
}

// ─── The kinds ──────────────────────────────────────────────────────────────────────────────────────────

// Every kind the editor already had, sorted into the three contributions, plus the ones this makes
//    possible. The first eight are not new: four are wind components built by buildWind in shaders.js and
//    four are the force types in fieldAccel. Putting them in one list is the whole point -- it shows the
//    umbrella is a description of what exists, not a wish.
export const ForceFieldKinds = [
  // ── flow: already built into the wind lattice, one texture sample however many there are ──
  { Id: "prevailing", Name: "Prevailing wind", Give: Contribution.Flow, Reaches: Reach.Everywhere,
    Was: "wind component 0", Unit: "m/s", Lattice: 0 },
  { Id: "gust", Name: "Gust front", Give: Contribution.Flow, Reaches: Reach.Sphere,
    Was: "wind component 1", Unit: "m/s", Lattice: 1 },
  { Id: "tornado", Name: "Tornado", Give: Contribution.Flow, Reaches: Reach.Sphere,
    Was: "wind component 2", Unit: "m/s", Lattice: 2 },
  { Id: "outflow", Name: "Blast outflow", Give: Contribution.Flow, Reaches: Reach.Sphere,
    Was: "wind component 3", Unit: "m/s", Lattice: 3 },

  // ── accelerate: evaluated per receiver ──
  { Id: "attract", Name: "Attractor", Give: Contribution.Accelerate, Reaches: Reach.Sphere,
    Was: "force type 0", Unit: "m/s\u00b2", Force: 0 },
  { Id: "repel", Name: "Repulsor", Give: Contribution.Accelerate, Reaches: Reach.Sphere,
    Was: "force type 1", Unit: "m/s\u00b2", Force: 1 },
  { Id: "lift", Name: "Lift", Give: Contribution.Accelerate, Reaches: Reach.Sphere,
    Was: "force type 2", Unit: "m/s\u00b2", Force: 2 },
  { Id: "magnetic", Name: "Magnetic dipole", Give: Contribution.Accelerate, Reaches: Reach.Sphere,
    Was: "force type 3", Unit: "m/s\u00b2", Force: 3 },

  // ── the ones the umbrella is for ──
  // 📝 Gravity is the clearest proof that this had to be its own contribution. It is unbounded, so it has
  //    no radius to bake into a lattice, and it is an acceleration, so a receiver's wind coupling must
  //    have no say in it whatsoever. Today it is a per-system scalar that every preset sets separately,
  //    which is why there is no way to author "this room is on the Moon".
  // 📝 Gravity reaches the existing GPU path as a downward Lift, which is the one force type that is
  //    already a fixed world axis. That is a mapping, not a merge: it stays Accelerate here, it is
  //    authored as gravity, and when the shader grows a type of its own only this line changes.
  { Id: "gravity", Name: "Gravity", Give: Contribution.Accelerate, Reaches: Reach.Everywhere,
    Was: null, Unit: "m/s\u00b2", Force: 2, Inverts: true },
  { Id: "orbit", Name: "Orbit", Give: Contribution.Accelerate, Reaches: Reach.Sphere,
    Was: null, Unit: "m/s\u00b2", Force: 0 },
  // These two have no GPU path yet. Declaring them without one is deliberate: the taxonomy is the design
  //    reference, and a kind that cannot be packed is caught by CheckForceFields rather than by a user.
  { Id: "drag", Name: "Drag volume", Give: Contribution.Damp, Reaches: Reach.Box,
    Was: null, Unit: "1/s" },
  { Id: "current", Name: "Current", Give: Contribution.Flow, Reaches: Reach.Box,
    Was: null, Unit: "m/s" },
];

export function KindById(Id) {
  return ForceFieldKinds.find((One) => One.Id === Id) || null;
}

// ─── A field ────────────────────────────────────────────────────────────────────────────────────────────

export function BaseField(Id) {
  const Kind = KindById(Id);
  return {
    Kind: Id,
    Name: Kind ? Kind.Name : Id,
    Enabled: true,
    Centre: [0, 0, 0],
    Direction: [0, -1, 0],
    Strength: Id === "gravity" ? 9.81 : 5,
    Radius: 8,
    Reaches: Kind ? Kind.Reaches : Reach.Sphere,
    Fades: Falloff.Smooth,
    Swirl: 0,
    Rate: 0,        // [Hz] gust pulse rate; the flow lattice's `freq`
    Swallow: 0,     // [m] an attractor this close removes the receiver; the black hole's horizon
    // 🔴 WHO RESPONDS IS CONFIGURATION, NOT CODE. The same rule the gas colliders follow. A field names
    //    the channels it acts on and a receiver names the channels it belongs to; an empty list on the
    //    field means everything. Hardcoding "gravity affects debris" is how an engine ends up needing a
    //    recompile to make a balloon float.
    Acts: [],
    // The time window fieldAccel already understands: start, how long, how often (0 = once).
    Begins: 0,
    Lasts: 0,       // 0 = forever
    Repeats: 0,
  };
}

// 📦 Is this field acting at this moment?
export function Awake(Field, Now) {
  if (!Field.Enabled) return false;
  if (Field.Lasts <= 0) return Now >= Field.Begins;
  const Since = Now - Field.Begins;
  if (Since < 0) return false;
  if (Field.Repeats > 0) return Since % Field.Repeats < Field.Lasts;
  return Since < Field.Lasts;
}

// 📦 Does this field act on a receiver carrying these channels?
export function ActsOn(Field, Channels) {
  if (!Field.Acts || Field.Acts.length === 0) return true;
  if (!Channels || Channels.length === 0) return false;
  return Field.Acts.some((One) => Channels.includes(One));
}

// ─── Sampling ───────────────────────────────────────────────────────────────────────────────────────────

function Away(Field, At) {
  const dx = At[0] - Field.Centre[0], dy = At[1] - Field.Centre[1], dz = At[2] - Field.Centre[2];
  return { d: [dx, dy, dz], r: Math.sqrt(dx * dx + dy * dy + dz * dz) };
}

// 📦 What one field contributes at a point. Returns a vector in the units of the field's contribution,
//    which is why the caller must look at `Give` before using it and cannot simply add the three.
export function SampleField(Field, At, Now) {
  const Kind = KindById(Field.Kind);
  if (!Kind || !Awake(Field, Now)) return [0, 0, 0];

  let Share = 1;
  if (Field.Reaches !== Reach.Everywhere) {
    const { r } = Away(Field, At);
    if (r >= Field.Radius) return [0, 0, 0];
    Share = Falls(Field.Fades, r, Field.Radius);
  }

  const Scale = Field.Strength * Share;
  if (Field.Kind === "gravity") return [0, -Scale, 0];
  if (Field.Kind === "lift") return [0, Scale, 0];
  if (Field.Kind === "drag") return [Scale, Scale, Scale];

  if (Field.Kind === "attract" || Field.Kind === "repel" || Field.Kind === "orbit") {
    const { d, r } = Away(Field, At);
    const Span = Math.max(r, 1e-3);
    const Out = [d[0] / Span, d[1] / Span, d[2] / Span];
    if (Field.Kind === "repel") return [Out[0] * Scale, Out[1] * Scale, Out[2] * Scale];
    const In = [-Out[0] * Scale, -Out[1] * Scale, -Out[2] * Scale];
    if (Field.Kind === "attract" && Field.Swirl === 0) return In;
    // Swirl is tangential, around world up. An attractor with swirl is how a drain behaves and how the
    //    black hole preset already behaves; orbit is the same term with no inward part.
    const Tangent = [-Out[2], 0, Out[0]];
    const Length = Math.hypot(Tangent[0], Tangent[2]) || 1;
    const Turn = Field.Kind === "orbit" ? Scale : Field.Swirl * Share;
    const Round = [Tangent[0] / Length * Turn, 0, Tangent[2] / Length * Turn];
    if (Field.Kind === "orbit") return Round;
    return [In[0] + Round[0], In[1] + Round[1], In[2] + Round[2]];
  }

  // The flow kinds and the magnetic drive are directional about their own axis.
  const Axis = Field.Direction;
  const Length = Math.hypot(Axis[0], Axis[1], Axis[2]) || 1;
  return [Axis[0] / Length * Scale, Axis[1] / Length * Scale, Axis[2] / Length * Scale];
}

// 📦 Everything acting at a point, kept apart by contribution because they are in different units.
//    out  { Flow: [m/s], Accelerate: [m/s²], Damp: [1/s] }
export function Resolve(Fields, At, Now, Channels) {
  // 🔴 `Flowing` is not bookkeeping. A flow of [0,0,0] and NO FLOW AT ALL have to be told apart, because
  //    relaxing toward a zero flow is a brake: a receiver in a place no wind reaches would be slowed to a
  //    stop by the mere absence of wind, which is both wrong and invisible. Still air that drags is a
  //    Damp field somebody placed on purpose, not a side effect of an empty list.
  const Out = { Flow: [0, 0, 0], Flowing: false, Accelerate: [0, 0, 0], Damp: 0 };
  for (const Field of Fields || []) {
    const Kind = KindById(Field.Kind);
    if (!Kind || !ActsOn(Field, Channels)) continue;
    if (!Awake(Field, Now)) continue;
    if (Kind.Give === Contribution.Damp) { Out.Damp += SampleField(Field, At, Now)[0]; continue; }
    if (Kind.Give === Contribution.Flow) {
      // Reach is what decides this, not the sampled magnitude: a gust that genuinely blows at 0 m/s here
      //    still counts as air, whereas a gust a kilometre away does not.
      if (!Reaching(Field, At)) continue;
      const Given = SampleField(Field, At, Now);
      Out.Flow[0] += Given[0]; Out.Flow[1] += Given[1]; Out.Flow[2] += Given[2];
      Out.Flowing = true;
      continue;
    }
    const Given = SampleField(Field, At, Now);
    Out.Accelerate[0] += Given[0]; Out.Accelerate[1] += Given[1]; Out.Accelerate[2] += Given[2];
  }
  return Out;
}

// 📦 Is this point inside the field's extent at all, regardless of how strong it is there?
export function Reaching(Field, At) {
  if (Field.Reaches === Reach.Everywhere) return true;
  return Away(Field, At).r < Field.Radius;
}

// 📦 Advance one receiver by one step. This is the only function that knows how the three contributions
//    differ in APPLICATION, and it is deliberately tiny — it is the paragraph at the top of this file,
//    executable, and it is what the C++ port has to reproduce exactly.
//
//    in  Velocity   [m/s]
//    in  Acting     the output of Resolve
//    in  Coupling   [1/s] how fast this receiver is dragged to the flow. 0 ignores wind entirely.
//    in  Step       [s]
export function Advance(Velocity, Acting, Coupling, Step) {
  const Next = Velocity.slice();
  // Only relax when a flow field actually reaches here. See the note on `Flowing` in Resolve.
  if (Acting.Flowing) {
    const Pull = Math.min(Math.max(Coupling * Step, 0), 1);
    Next[0] += (Acting.Flow[0] - Velocity[0]) * Pull;
    Next[1] += (Acting.Flow[1] - Velocity[1]) * Pull;
    Next[2] += (Acting.Flow[2] - Velocity[2]) * Pull;
  }
  // Acceleration is added AFTER the relaxation and is not scaled by Coupling. This single line is the
  //    whole reason flow and acceleration are separate contributions.
  Next[0] += Acting.Accelerate[0] * Step;
  Next[1] += Acting.Accelerate[1] * Step;
  Next[2] += Acting.Accelerate[2] * Step;
  if (Acting.Damp > 0) {
    const Keep = Math.exp(-Acting.Damp * Step);
    Next[0] *= Keep; Next[1] *= Keep; Next[2] *= Keep;
  }
  return Next;
}

// 📦 Which fields can be summed into the flow lattice, and which must be evaluated per receiver.
//    This is the performance consequence of the taxonomy, made queryable so the engine does not have to
//    re-derive it and the two cannot disagree.
export function Bakeable(Fields) {
  const Baked = [], Live = [];
  for (const Field of Fields || []) {
    const Kind = KindById(Field.Kind);
    if (!Kind) continue;
    // A flow field can be summed into the lattice only if nothing about it is per-receiver: an Acts list
    //    makes it selective, and a selective field cannot share one texture with the fields it excludes.
    const Selective = Field.Acts && Field.Acts.length > 0;
    if (Kind.Give === Contribution.Flow && !Selective) Baked.push(Field);
    else Live.push(Field);
  }
  return { Baked, Live };
}

PE.Forces = {
  Contribution, Reach, Falloff, Falls, ForceFieldKinds, KindById, BaseField,
  Awake, ActsOn, Reaching, SampleField, Resolve, Advance, Bakeable,
};

// ─── Reaching the GPU ───────────────────────────────────────────────────────────────────────────────────
//
// 🔴 THE SHADERS DO NOT CHANGE. One authored list has to produce exactly the two payloads the GPU already
//    consumes — the flow lattice's component array and the acceleration array that fieldAccel walks — and
//    produce them bit for bit the same as the two hand-written structures it replaces. That constraint is
//    what makes this a migration that can be verified without a GPU in the room, and CheckForceFields
//    asserts the equality against the original packing rather than against a description of it.

// 📦 The compass bearing a directional field points along, in degrees. The lattice is authored in
//    bearings because that is how wind is talked about; the field stores a direction because that is what
//    generalises. Round-tripping through this is exact to about 1e-13 of a degree, not bit-exact.
export function Bearing(Field) {
  const [x, , z] = Field.Direction;
  const Degrees = (Math.atan2(x, z) * 180) / Math.PI;
  return Degrees < 0 ? Degrees + 360 : Degrees;
}

export function Along(Degrees) {
  const Turn = (Degrees * Math.PI) / 180;
  return [Math.sin(Turn), 0, Math.cos(Turn)];
}

// 📦 One flow field as the lattice builder's component: the eight floats buildWind reads per entry.
export function LatticeComponent(Field) {
  const Kind = KindById(Field.Kind);
  if (!Kind || Kind.Give !== Contribution.Flow || Kind.Lattice === undefined) return null;
  return {
    x: Field.Centre[0],
    z: Field.Centre[2],
    radius: Field.Radius,
    type: Kind.Lattice,
    strength: Field.Strength,
    bearing: Bearing(Field),
    freq: Field.Rate,
    enabled: Field.Enabled,
  };
}

// 📦 One acceleration field as fieldAccel's three vec4s.
//    ⚠️ A radius of 0 means "everywhere" to the shader, which is why Reach.Everywhere packs as 0 rather
//       than as a large number. Getting that backwards would make gravity a sphere eight metres wide.
export function ForceEntry(Field) {
  const Kind = KindById(Field.Kind);
  if (!Kind || Kind.Give !== Contribution.Accelerate || Kind.Force === undefined) return null;
  const Everywhere = Field.Reaches === Reach.Everywhere;
  return {
    type: Kind.Force,
    pos: Field.Centre.slice(),
    radius: Everywhere ? 0 : Field.Radius,
    strength: Kind.Inverts ? -Field.Strength : Field.Strength,
    swirl: Field.Swirl,
    swallow: Field.Swallow,
    t0: Field.Begins,
    duration: Field.Lasts > 0 ? Field.Lasts : 1e9,
    period: Field.Repeats,
  };
}

// 📦 Every kind that claims a contribution must be able to reach the GPU, or be known not to.
export function Packable(Field) {
  const Kind = KindById(Field.Kind);
  if (!Kind) return false;
  if (Kind.Give === Contribution.Flow) return Kind.Lattice !== undefined;
  if (Kind.Give === Contribution.Accelerate) return Kind.Force !== undefined;
  return false;   // 📝 Damp has no GPU path yet; the inspector says so rather than silently dropping it.
}

// ─── The authored list ──────────────────────────────────────────────────────────────────────────────────

// 📦 The scene's fields, as one list. This reproduces the four wind components defaultWind() shipped —
//    same order, same readings — because the migration must not change what the editor opens with.
export function DefaultForces() {
  const Make = (Id, Name, Enabled, Centre, Radius, Strength, BearingDegrees, Rate) => ({
    ...BaseField(Id),
    Name, Enabled, Centre, Radius, Strength, Rate,
    Direction: Along(BearingDegrees),
  });
  return [
    Make("prevailing", "Prevailing wind", true, [0, 0, 0], 6, 3, 70, 0.3),
    Make("gust", "Passing gust", true, [-4, 0, 0], 4, 5, 70, 0.3),
    Make("tornado", "Tornado", false, [-2, 0, -4], 1.6, 6, 0, 0),
    Make("outflow", "Radial suction", false, [0, 0, -2], 3, -2, 0, 0),
  ];
}

// 📦 Split an authored list into the two GPU payloads, in the order the GPU expects them.
//    Flow fields keep their slot whether enabled or not, because buildWind reads a fixed count and tests
//    the enabled flag itself; acceleration fields are packed only while awake, as fieldAccel's caller
//    always did.
export function ForGpu(Fields, Now) {
  const Lattice = [], Forces = [];
  for (const Field of Fields || []) {
    const Kind = KindById(Field.Kind);
    if (!Kind) continue;
    if (Kind.Give === Contribution.Flow) {
      const Component = LatticeComponent(Field);
      if (Component) Lattice.push(Component);
    } else if (Kind.Give === Contribution.Accelerate) {
      if (!Field.Enabled) continue;
      if (!Awake(Field, Now === undefined ? Field.Begins : Now)) continue;
      const Entry = ForceEntry(Field);
      if (Entry) Forces.push(Entry);
    }
  }
  return { Lattice, Forces };
}

Object.assign(PE.Forces, {
  Bearing, Along, LatticeComponent, ForceEntry, Packable, DefaultForces, ForGpu,
});

// SampleScenes.js — the scenes that cross into the engine.
//
// These are not demonstrations. They are the parity corpus: the native port loads these exact scenes, runs
// them, and is compared against what the browser does with them. So each one is chosen to exercise a part of
// the port that nothing else reaches, and the set is deliberately small enough to look at one by one.
//
// A sample is a complete scene — settings, object names and camera — not a preset. A preset is tuning; a scene
// is tuning plus a framing plus the names the outliner shows, which is what actually has to survive the
// crossing. Presets already round-trip through PRESETS; scenes are what round-trip through a file.
//
// 🔴 Each sample states what it is for. A sample that covers nothing another already covers does not earn a
//    place here, because every one of them is a thing the native port has to match forever.

import { PRESETS, DEFAULT_PARAMS } from "./presets.js";

// Camera framings reused across the samples, so a scene differs from its neighbour in what it simulates
// rather than in where it happens to be standing.
const Framings = {
  Eye: { theta: 0.72, phi: 1.32, distance: 3.05, center: [0, 0.18, 0] },
  Low: { theta: 2.3, phi: 1.52, distance: 2.45, center: [0, -0.05, 0] },
  High: { theta: -0.95, phi: 0.92, distance: 4.3, center: [0, 0.35, 0] },
  Close: { theta: 1.15, phi: 1.28, distance: 1.75, center: [0, 0.05, 0] },
};

// Each entry: the preset it starts from, what it is in the corpus for, and the few settings it changes.
const Definitions = [
  {
    identity: "hero_detonation",
    name: "Hero detonation",
    covers:
      "The expensive case. Highest lattice, most pressure sweeps, dynamic bounds expanding mid-blast — " +
      "this is the scene that proves the native budget governor assigns the Hero rung and holds it.",
    preset: "ue5_pyro_default",
    framing: "Eye",
    names: {
      domain: "Blast domain",
      emitter: "Charge",
      collider: "Ground plate",
      sun: "Key light",
    },
    params: { gridResolution: 128, pressureIterations: 26 },
  },
  {
    identity: "camp_fire_steady",
    name: "Camp fire, steady",
    covers:
      "The cheap steady case. A continuous emitter with no blast at all, so the native port's emitter path " +
      "is exercised without the detonation path masking a fault in it.",
    preset: "camp_fire",
    framing: "Close",
    names: {
      domain: "Fire domain",
      emitter: "Log fire",
      collider: "Stone ring",
      sun: "Moonlight",
    },
    params: {},
  },
  {
    identity: "cold_dust_fall",
    name: "Cold dust fall",
    covers:
      "No fuel and no heat anywhere. Everything visible arrives through scattering, so this is the scene " +
      "that catches a native raymarch whose emission term is carrying the picture.",
    preset: "settling_dust",
    framing: "Low",
    names: {
      domain: "Dust domain",
      emitter: "Ledge spill",
      collider: "Rubble",
      sun: "Overcast key",
    },
    params: {},
  },
  {
    identity: "tyre_burnout_ring",
    name: "Tyre burnout",
    covers:
      "The only sample selecting the tyre ring obstacle, and the one that ties a gas domain to a vehicle. " +
      "GasColliderShape::TyreRing and this scene have to agree or a burnout smokes through its own tyre.",
    preset: "tyre_burnout",
    framing: "Low",
    names: {
      domain: "Contact patch",
      emitter: "Slip",
      collider: "Rear tyre",
      sun: "Pit light",
    },
    params: {},
  },
  {
    identity: "deflector_obstacle",
    name: "Deflected plume",
    covers:
      "A plume driven into a slab. The one sample where obstruction changes the silhouette, so a native " +
      "occupancy path that marks nothing still produces a picture — and a visibly wrong one.",
    preset: "obstacle_deflection",
    framing: "Eye",
    names: {
      domain: "Vent domain",
      emitter: "Floor vent",
      collider: "Deflector slab",
      sun: "Key light",
    },
    params: {},
  },
  {
    identity: "fracture_dust_burst",
    name: "Fracture dust burst",
    covers:
      "What a fracture piece actually spawns: a cold one-shot burst, no emitter, gone in under two seconds. " +
      "The shortest-lived sample, and the one the flipbook bake in step 5 is aimed at.",
    preset: "brick_fracture_dust",
    framing: "Close",
    names: {
      domain: "Impact domain",
      emitter: "Break point",
      collider: "Masonry",
      sun: "Key light",
    },
    params: {},
  },
  {
    identity: "far_cheap_plume",
    name: "Distant plume",
    covers:
      "Deliberately the cheapest legal scene: lowest lattice, fewest sweeps. The native governor should " +
      "place this on a low rung unprompted, and the port should still look like something at that cost.",
    preset: "low_gpu_performance",
    framing: "High",
    names: {
      domain: "Far domain",
      emitter: "Distant fire",
      collider: "None",
      sun: "Key light",
    },
    params: { gridResolution: 32, pressureIterations: 14 },
  },
  {
    identity: "open_bounds_megaton",
    name: "Open bounds megaton",
    covers:
      "The largest domain in the corpus. Catches the optical normalisation: without it this scene renders " +
      "nearly transparent in the native port while the browser shows a solid fireball.",
    preset: "megaton_open_bounds",
    framing: "High",
    names: {
      domain: "Open domain",
      emitter: "Device",
      collider: "Terrain",
      sun: "Key light",
    },
    params: {},
  },
];

// A sample resolves to a full scene the same way the application does: defaults, then the preset's
// differences, then the sample's own. Building it this way rather than writing 85 settings out eight times is
// what stops a sample silently freezing an old default.
export function ConstructSampleScene(Identity) {
  const Definition = Definitions.find((Entry) => Entry.identity === Identity);
  if (!Definition) throw new Error(`Unknown sample scene: ${Identity}`);
  const Preset = PRESETS[Definition.preset];
  if (!Preset) throw new Error(`Sample ${Identity} starts from a preset that no longer exists`);
  return {
    format: "frontier-fluid-scene",
    version: 1,
    name: Definition.name,
    params: { ...DEFAULT_PARAMS, ...Preset.params, ...Definition.params },
    names: { ...Definition.names },
    camera: { ...Framings[Definition.framing], center: [...Framings[Definition.framing].center] },
  };
}

export function SampleIdentities() {
  return Definitions.map((Entry) => Entry.identity);
}

export function SampleSummary() {
  return Definitions.map((Entry) => ({
    identity: Entry.identity,
    name: Entry.name,
    covers: Entry.covers,
    preset: Entry.preset,
    presetName: PRESETS[Entry.preset]?.name ?? "",
  }));
}

export const SampleCount = Definitions.length;

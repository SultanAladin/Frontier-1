// SceneTomlCodec.js — the scene as the engine reads it.
//
// 🔴 JSON IS THE BROWSER'S FORMAT. TOML IS THE ENGINE'S, AND THE ENGINE'S IS THE ONE THAT MATTERS.
//    The application keeps saving .fluid.json because a browser already has JSON.parse and the web page has
//    no other consumer. The engine does not: Frontier already carries toml++ and already configures itself
//    from TOML, so a gas scene arriving as JSON would be the only JSON in the build and would need a parser
//    brought in to read it. This module writes the TOML the engine reads, from the same scene object the
//    JSON path writes, so the two can never describe different scenes.
//
// The emitted text is deliberately plain: no inline arrays except the camera centre, no nested tables beyond
// one level, no datetimes, no multi-line strings. Everything here is within what any conforming reader
// accepts, which keeps the native side a straight toml++ read rather than a dialect.
//
// Layout:
//
//    format  = "frontier-fluid-scene"
//    version = 1
//    name    = "Hero detonation"
//
//    [names]            one string per outliner object
//    [camera]           theta, phi, distance, and centre as a three-element array
//    [settings]         all 85, sorted, one per line
//
// ⚠️ Settings are emitted sorted by key, not in authoring order. A file whose line order depends on object
//    iteration produces a different diff every time it is saved from a different browser, and the corpus is
//    version controlled.

const Format = "frontier-fluid-scene";
const Version = 1;

function EscapeString(Text) {
  return String(Text)
    .replace(/\\/g, "\\\\")
    .replace(/"/g, '\\"')
    .replace(/\n/g, "\\n")
    .replace(/\r/g, "\\r")
    .replace(/\t/g, "\\t");
}

// TOML distinguishes integers from floats, and so does the engine: GasSettings declares a lattice as an
// integer and an exposure as a float. Emitting 1 where 1.0 is meant makes toml++ hand back an integer and
// the native read then fails its type check — which is the correct failure, and an avoidable one.
function SpellNumber(Reading, ForceFloat) {
  if (!Number.isFinite(Reading)) throw new Error(`Cannot write a non-finite number: ${Reading}`);
  if (!ForceFloat && Number.isInteger(Reading)) return String(Reading);
  return Number.isInteger(Reading) ? `${Reading}.0` : String(Reading);
}

// The settings the engine declares as counts. Mirrors the Counted set in Tools/Build/GenerateGasPresets.py;
// the node checks assert the two agree, because a disagreement here is a type error on the far side only.
export const CountedSettings = new Set([
  "gridResolution",
  "pressureIterations",
  "blastLobes",
  "raymarchSteps",
  "shadowSteps",
  "emberCount",
  "sliceAxis",
  "obstacleType",
  "renderChannel",
  "colorPalette",
  "voxelQuantization",
  "atlasMinimapField",
]);

function SpellReading(Key, Reading) {
  if (typeof Reading === "boolean") return Reading ? "true" : "false";
  if (typeof Reading === "string") return `"${EscapeString(Reading)}"`;
  if (typeof Reading === "number") return SpellNumber(Reading, !CountedSettings.has(Key));
  throw new Error(`Setting ${Key} has a type TOML cannot carry: ${typeof Reading}`);
}

export function WriteSceneToml(Scene) {
  if (!Scene || Scene.format !== Format || Scene.version !== Version)
    throw new Error("Not a supported Fluid scene (version 1).");

  const Lines = [];
  Lines.push("# Frontier gas scene. Written by Experimental/Fluid; read by Engine/VolumetricDynamics.");
  Lines.push("# Settings are sorted by name so the file diffs cleanly; order carries no meaning.");
  Lines.push("");
  Lines.push(`format = "${Format}"`);
  Lines.push(`version = ${Version}`);
  Lines.push(`name = "${EscapeString(Scene.name)}"`);
  Lines.push("");

  Lines.push("[names]");
  for (const Key of Object.keys(Scene.names ?? {}).sort())
    Lines.push(`${Key} = "${EscapeString(Scene.names[Key])}"`);
  Lines.push("");

  if (Scene.camera) {
    const Camera = Scene.camera;
    Lines.push("[camera]");
    Lines.push(`theta = ${SpellNumber(Camera.theta, true)}`);
    Lines.push(`phi = ${SpellNumber(Camera.phi, true)}`);
    Lines.push(`distance = ${SpellNumber(Camera.distance, true)}`);
    Lines.push(`centre = [${Camera.center.map((Value) => SpellNumber(Value, true)).join(", ")}]`);
    Lines.push("");
  }

  Lines.push("[settings]");
  for (const Key of Object.keys(Scene.params).sort())
    Lines.push(`${Key} = ${SpellReading(Key, Scene.params[Key])}`);
  Lines.push("");

  return Lines.join("\n");
}

// A reader for the subset written above, so the round-trip can be checked without a TOML library in the
// browser. It is not a general TOML parser and does not pretend to be: it rejects anything it was not
// written to emit, which is the correct behaviour for a check whose job is to catch the emitter drifting.
export function ReadSceneToml(Text) {
  const Scene = { format: "", version: 0, name: "", names: {}, params: {}, camera: null };
  let Section = "";

  for (const Raw of String(Text).split("\n")) {
    const Line = Raw.trim();
    if (!Line || Line.startsWith("#")) continue;

    const Heading = Line.match(/^\[([a-z]+)\]$/);
    if (Heading) {
      Section = Heading[1];
      if (Section === "camera") Scene.camera = { theta: 0, phi: 0, distance: 0, center: [] };
      continue;
    }

    const Pair = Line.match(/^([A-Za-z][A-Za-z0-9_]*)\s*=\s*(.+)$/);
    if (!Pair) throw new Error(`Unreadable line: ${Line}`);
    const Key = Pair[1];
    const Spelt = Pair[2].trim();

    let Reading;
    if (Spelt === "true") Reading = true;
    else if (Spelt === "false") Reading = false;
    else if (Spelt.startsWith('"'))
      Reading = Spelt
        .slice(1, -1)
        .replace(/\\n/g, "\n")
        .replace(/\\r/g, "\r")
        .replace(/\\t/g, "\t")
        .replace(/\\"/g, '"')
        .replace(/\\\\/g, "\\");
    else if (Spelt.startsWith("["))
      Reading = Spelt
        .slice(1, -1)
        .split(",")
        .map((Entry) => Number(Entry.trim()));
    else {
      Reading = Number(Spelt);
      if (!Number.isFinite(Reading)) throw new Error(`Unreadable number for ${Key}: ${Spelt}`);
    }

    if (Section === "") {
      if (Key === "format") Scene.format = Reading;
      else if (Key === "version") Scene.version = Reading;
      else if (Key === "name") Scene.name = Reading;
      else throw new Error(`Unexpected top-level key: ${Key}`);
    } else if (Section === "names") Scene.names[Key] = Reading;
    else if (Section === "camera") {
      if (Key === "centre") Scene.camera.center = Reading;
      else Scene.camera[Key] = Reading;
    } else if (Section === "settings") Scene.params[Key] = Reading;
    else throw new Error(`Unexpected section: ${Section}`);
  }

  if (Scene.format !== Format || Scene.version !== Version)
    throw new Error("Not a supported Fluid scene (version 1).");
  return Scene;
}

// The extension the engine looks for. Named for what the file describes rather than for the simulator that
// happens to write it, because a fracture emitter will write one of these too and it is not a fluid.
export const SceneExtension = ".gasscene.toml";

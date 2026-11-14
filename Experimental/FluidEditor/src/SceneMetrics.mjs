import { ConstructSampleScene, SampleIdentities, SampleCount } from "./SampleScenes.js";
import { WriteSceneToml, ReadSceneToml, CountedSettings } from "./SceneTomlCodec.js";
import { test as Verify } from "node:test";
import Assert from "node:assert/strict";
import { readFileSync as ReadText } from "node:fs";
import { DEFAULT_PARAMS, PRESETS, RESOLUTION_OPTIONS } from "./presets.js";
import {
  InitialParameters,
  ControlSpecification,
  PresetPresentation,
  ValidateParameter,
  ValidateScene,
  ConstructPresetParameters,
  ComputeColliderPosition,
} from "./SceneSpecification.js";
import { OrbitCamera } from "./camera.js";

const ConstructScene = () => ({
  format: "frontier-fluid-scene",
  version: 1,
  name: "Ember study",
  params: { ...InitialParameters },
});

Verify(
  "Every gas default and every retained preset fits the scene specification",
  () => {
    for (const Parameters of [
      DEFAULT_PARAMS,
      ...Object.values(PRESETS).map((Preset) => Preset.params),
    ]) {
      for (const [Key, Value] of Object.entries(Parameters))
        Assert.equal(ValidateParameter(Key, Value), Value, Key);
    }
    Assert.equal(Object.keys(PRESETS).length, 18);
    Assert.deepEqual(
      Object.keys(PresetPresentation).sort(),
      Object.keys(PRESETS).sort(),
    );
  },
);
Verify("Every simulation setting has an inspector or toolbar control", () => {
  Assert.deepEqual(
    Object.keys(InitialParameters)
      .filter((Key) => !ControlSpecification[Key])
      .sort(),
    ["autoTurntable", "interactionMode", "paused"],
  );
});
Verify("Resolution choices cover exactly their tiled voxel atlases", () => {
  for (const Resolution of RESOLUTION_OPTIONS)
    Assert.equal(Resolution.tilesX * Resolution.tilesY, Resolution.value);
});
Verify("Presets and scene decoding do not mutate shared defaults", () => {
  const Parameters = ConstructPresetParameters("oil_well_inferno");
  Parameters.emitterRate = 2.5;
  Assert.equal(InitialParameters.emitterRate, 1);
  Assert.throws(() => ConstructPresetParameters("__proto__"));
  const Scene = ConstructScene();
  const Decoded = ValidateScene(Scene);
  Decoded.Parameters.emitterRate = 2;
  Assert.equal(Scene.params.emitterRate, 1);
});
Verify(
  "Scene settings round-trip including camera, disabled source, and collider",
  () => {
    const Scene = ConstructScene();
    Scene.params.emitterEnabled = false;
    Scene.params.obstacleType = 4;
    Scene.camera = { theta: 0.7, phi: 1.2, distance: 4, center: [0, 0.22, 0] };
    Scene.names = { emitter: "A smoke source" };
    const Decoded = ValidateScene(JSON.parse(JSON.stringify(Scene)));
    Assert.deepEqual(Decoded.Parameters, Scene.params);
    Assert.deepEqual(Decoded.Camera, Scene.camera);
    Assert.equal(Decoded.Names.emitter, "A smoke source");
  },
);
Verify(
  "Unsupported versions, unknown keys, and non-finite settings are rejected atomically",
  () => {
    const Scene = ConstructScene();
    for (const Value of [
      null,
      [],
      {},
      { ...Scene, version: 2 },
      { ...Scene, params: [] },
      { ...Scene, params: { simMode: 1 } },
      { ...Scene, params: { emitterFuel: NaN } },
      { ...Scene, params: { raymarchSteps: Infinity } },
      { ...Scene, params: { paused: "false" } },
    ])
      Assert.throws(() => ValidateScene(Value));
  },
);
Verify(
  "Out-of-range numbers and invalid menu choices cannot allocate malformed GPU grids",
  () => {
    for (const [Key, Value] of [
      ["gridResolution", 72],
      ["gridResolution", 1024],
      ["gridResolution", -1],
      ["obstacleType", 6],
      ["colorPalette", 6],
      ["renderChannel", 9],
      ["renderScale", 100],
      ["timeScale", -1],
      ["interactionMode", "water"],
    ])
      Assert.throws(() => ValidateParameter(Key, Value));
  },
);
Verify(
  "Malformed camera and names are rejected before scene replacement",
  () => {
    const Scene = ConstructScene();
    for (const Camera of [
      {},
      { theta: 1, phi: 2, distance: -1, center: [0, 0, 0] },
      { theta: 1, phi: 2, distance: 4, center: [0, NaN, 0] },
    ])
      Assert.throws(() => ValidateScene({ ...Scene, camera: Camera }));
    Assert.throws(() => ValidateScene({ ...Scene, name: "" }));
    Assert.throws(() =>
      ValidateScene({ ...Scene, names: { emitter: "a".repeat(65) } }),
    );
  },
);
Verify(
  "Collider animation is deterministic, bounded, and preserves authored coordinates",
  () => {
    const Parameters = {
      ...InitialParameters,
      obstacleType: 1,
      colliderAutoMove: true,
    };
    const Original = { ...Parameters };
    Assert.deepEqual(ComputeColliderPosition(Parameters, 0), [
      Parameters.obstacleX,
      Parameters.obstacleY,
      Parameters.obstacleZ,
    ]);
    Assert.notDeepEqual(
      ComputeColliderPosition(Parameters, 1),
      ComputeColliderPosition(Parameters, 0),
    );
    for (let Time = 0; Time < 20; Time += 0.03)
      Assert.ok(
        ComputeColliderPosition(Parameters, Time).every(
          (Value) => Value >= 0 && Value <= 1,
        ),
      );
    Assert.deepEqual(Parameters, Original);
    Assert.deepEqual(
      ComputeColliderPosition({ ...Parameters, colliderAutoMove: false }, 4),
      [Parameters.obstacleX, Parameters.obstacleY, Parameters.obstacleZ],
    );
  },
);
Verify("Camera raycasting reaches the actual simulation volume", () => {
  const Camera = new OrbitCamera();
  Camera.update(1);
  const Hit = Camera.raycastVolumeUVW(0, 0, [-1, -0.6, -1], [1, 1.5, 1]);
  Assert.ok(Hit && Hit.every((Value) => Value >= 0 && Value <= 1));
  Camera.zoom(-10000);
  Assert.ok(Camera.targetDistance >= 0.85);
  Camera.zoom(10000);
  Assert.ok(Camera.targetDistance <= 9.5);
});
Verify(
  "Gas import excludes all liquid simulation entry points and uniforms",
  () => {
    for (const File of [
      "engine-webgl2.js",
      "engine-webgpu.js",
      "shaders-glsl.js",
      "shaders-wgsl.js",
      "presets.js",
    ]) {
      const Text = ReadText(new URL(File, import.meta.url), "utf8");
      Assert.doesNotMatch(
        Text,
        /\b(?:simMode|water\w*|liquid\w*|hydro(?!carbon)\w*|csHydro\w*|foamGeneration|surfaceTension)\b/i,
        File,
      );
    }
  },
);
Verify("WebGPU gas uniform arrays agree with retained shader layouts", () => {
  const Shader = ReadText(new URL("shaders-wgsl.js", import.meta.url), "utf8");
  const Engine = ReadText(new URL("engine-webgpu.js", import.meta.url), "utf8");
  Assert.equal(
    Shader.match(/struct SimUniforms \{([\s\S]*?)\};/)[1].match(
      /:\s*(?:f32|u32)/g,
    ).length * 4,
    176,
  );
  Assert.match(Engine, /new ArrayBuffer\(176\)/);
  Assert.match(Engine, /new ArrayBuffer\(208\)/);
});

Verify(
  "Projected bounds have twelve finite edges, including axis-aligned and near-plane views",
  async () => {
    const { ProjectBounds } = await import("./BoundsProjection.js");
    const Camera = new OrbitCamera();
    const Bounds = { boxMin: [-1, -1, -1], boxMax: [1, 1, 1] };
    for (const Angle of [0, Math.PI / 2, Math.PI]) {
      Camera.targetTheta = Angle;
      Camera.targetPhi = Math.PI / 2;
      Camera.update(1);
      const Lines = ProjectBounds(Camera, Bounds, 640, 480);
      Assert.equal(Lines.length, 12);
      Assert.ok(Lines.flat().every(Number.isFinite));
    }
    Camera.position = [0, 0, 0];
    Assert.ok(
      ProjectBounds(Camera, Bounds, 640, 480).flat().every(Number.isFinite),
    );
  },
);
Verify(
  "Flipbook allocation limits, atlas layout and row order are explicit",
  async () => {
    const { SpecifyFlipbook, UnpackFrame } = await import(
      "./FlipbookSequence.js"
    );
    const Layout = SpecifyFlipbook(64, 512, 24, 0.25);
    Assert.equal(Layout.Width, 4096);
    Assert.equal(Layout.Height, 4096);
    for (const Arguments of [
      [10000, 512, 24, 0],
      [4, 10000, 24, 0],
      [4, 64, 0, 0],
      [4, 64, 24, NaN],
    ])
      Assert.throws(() => SpecifyFlipbook(...Arguments));
    const Pixels = UnpackFrame(
      new Uint8Array([
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 0, 0, 0, 0,
      ]),
      2,
      true,
    );
    Assert.deepEqual(
      [...Pixels],
      [0, 0, 255, 255, 0, 0, 0, 0, 255, 0, 0, 255, 0, 255, 0, 255],
    );
    Assert.deepEqual(
      [...UnpackFrame(new Uint8Array([64, 32, 0, 128]), 1, true)],
      [128, 64, 0, 128],
    );
  },
);
Verify("Bounds are no longer evaluated inside the volume shader", () => {
  const Shader = ReadText(new URL("shaders-glsl.js", import.meta.url), "utf8");
  Assert.doesNotMatch(Shader, /computeBoxWireframe|uShowBoundingBox/);
  Assert.match(Shader, /max\(abs\(rd\), vec3\(1e-7\)\)/);
});
Verify("Detonations have room to expand without a coarser voxel", () => {
  // blastRadius is a fraction of the domain, not a world length: shaders-glsl.js splats it in
  //    normalised uvw. So a wider box on its own grows the fireball with it and the plume still
  //    reaches the wall. Room comes from a wider box AND a smaller fraction, and the voxel only
  //    stays the same size if the grid grows with the box.
  const Detonations = [
    "ue5_pyro_default",
    "shrapnel_airburst",
    "tactical_ordnance",
    "megaton_open_bounds",
    "brick_fracture_dust",
  ];
  for (const Key of Detonations) {
    const Parameters = ConstructPresetParameters(Key);
    const Density = Parameters.gridResolution / Parameters.boundsWidth;
    Assert.ok(
      Density >= 28,
      `${Key} resolves ${Density.toFixed(1)} voxels per world unit, under 28`,
    );
    Assert.ok(
      Parameters.blastRadius <= 0.15,
      `${Key} fills ${Parameters.blastRadius} of the domain, over 0.15`,
    );
    if (Parameters.dynamicBounds)
      Assert.ok(
        Parameters.dynamicBoundsMax <= 1.25,
        `${Key} surges to ${Parameters.dynamicBoundsMax}x, which coarsens the voxel mid-blast`,
      );
  }
});
Verify("Cold effects carry no fuel and no fire", () => {
  // A dust, sand or masonry preset that quietly lights itself is the easiest mistake to make here,
  //    because every one of them is tuned from a fire preset.
  for (const Key of [
    "dust_tornado",
    "ledge_sandfall",
    "settling_dust",
    "small_gust",
    "brick_fracture_dust",
  ]) {
    const Parameters = ConstructPresetParameters(Key);
    Assert.equal(Parameters.emitterFuel, 0, `${Key} emits fuel`);
    Assert.equal(Parameters.blastFuel, 0, `${Key} bursts fuel`);
    Assert.ok(Parameters.burnRate <= 0.2, `${Key} burns`);
    Assert.ok(
      Parameters.smokeAlbedo >= 0.5,
      `${Key} scatters like soot, not like dust`,
    );
  }
});
Verify("Sand and dust fall, fire rises", () => {
  for (const Key of ["ledge_sandfall", "settling_dust"]) {
    const Parameters = ConstructPresetParameters(Key);
    Assert.ok(
      Parameters.smokeWeight > Parameters.buoyancy,
      `${Key} would rise instead of settling`,
    );
  }
  for (const Key of ["lantern_flame", "camp_fire"]) {
    const Parameters = ConstructPresetParameters(Key);
    Assert.ok(
      Parameters.buoyancy > Parameters.smokeWeight,
      `${Key} would sink instead of rising`,
    );
    Assert.ok(Parameters.emitterFuel > 0, `${Key} has nothing to burn`);
  }
});

// ── The parity corpus ──────────────────────────────────────────────────────────────────────────────
// These eight scenes are what the native port loads and is compared against, so they are checked harder
// than a demonstration would be: every one must survive ValidateScene, every one must survive a TOML
// round-trip unchanged, and the set must still cover the cases it claims to cover.

Verify("Every sample scene is a scene the application would accept", () => {
  Assert.equal(SampleCount, 8, "the corpus changed size without the checks being revisited");
  for (const Identity of SampleIdentities()) {
    const Scene = ConstructSampleScene(Identity);
    const Validated = ValidateScene(Scene);
    Assert.ok(Validated.Name.length > 0, `${Identity} has no name`);
    Assert.equal(
      Object.keys(Scene.params).length,
      Object.keys(DEFAULT_PARAMS).length,
      `${Identity} does not carry every setting`,
    );
    for (const Key of ["domain", "emitter", "collider", "sun"])
      Assert.ok(Validated.Names[Key], `${Identity} leaves ${Key} unnamed in the outliner`);
    Assert.ok(Validated.Camera, `${Identity} has no camera, so it frames nothing`);
  }
});

Verify("Every sample survives the TOML the engine reads", () => {
  for (const Identity of SampleIdentities()) {
    const Scene = ConstructSampleScene(Identity);
    const Written = WriteSceneToml(Scene);
    const Recovered = ReadSceneToml(Written);

    Assert.equal(Recovered.name, Scene.name, `${Identity} lost its name`);
    for (const [Key, Reading] of Object.entries(Scene.params))
      Assert.equal(Recovered.params[Key], Reading, `${Identity} changed ${Key} crossing TOML`);
    for (const [Key, Reading] of Object.entries(Scene.names))
      Assert.equal(Recovered.names[Key], Reading, `${Identity} changed the name of ${Key}`);
    Assert.deepEqual(Recovered.camera.center, Scene.camera.center, `${Identity} moved its camera`);

    // Writing twice must produce the same bytes, or the corpus churns in version control.
    Assert.equal(Written, WriteSceneToml(Scene), `${Identity} does not write deterministically`);
    // And re-writing what was read must reproduce it, which is the actual round-trip claim.
    Assert.equal(Written, WriteSceneToml({ ...Recovered, format: Scene.format, version: 1 }),
      `${Identity} is not stable across a read and a second write`);
  }
});

Verify("Counted settings cross TOML as integers and the rest as floats", () => {
  // A float where the engine declares an integer makes toml++ hand back the wrong type and the native read
  // refuses the file. The failure is correct and entirely avoidable, so it is caught here instead.
  const Written = WriteSceneToml(ConstructSampleScene("hero_detonation"));
  for (const Line of Written.split("\n")) {
    const Pair = Line.match(/^([a-zA-Z][A-Za-z0-9_]*) = (-?[\d.]+)$/);
    if (!Pair) continue;
    const [, Key, Spelt] = Pair;
    if (!Object.hasOwn(DEFAULT_PARAMS, Key)) continue;
    if (CountedSettings.has(Key))
      Assert.ok(!Spelt.includes("."), `${Key} is a count and must not be written as a float`);
    else Assert.ok(Spelt.includes("."), `${Key} is continuous and must be written as a float`);
  }
});

Verify("The corpus covers what it claims to", () => {
  const Scenes = Object.fromEntries(
    SampleIdentities().map((Identity) => [Identity, ConstructSampleScene(Identity).params]),
  );
  // The tyre ring, which exists in exactly one sample and must agree with GasColliderShape::TyreRing.
  Assert.equal(Scenes.tyre_burnout_ring.obstacleType, 5, "the burnout sample lost its tyre ring");
  // A cold sample with no fire anywhere, or the raymarch's scattering path is never exercised.
  Assert.equal(Scenes.cold_dust_fall.emitterFuel, 0, "the cold sample lit itself");
  Assert.equal(Scenes.cold_dust_fall.blastFuel, 0, "the cold sample lit itself");
  // An obstruction that changes the silhouette.
  Assert.notEqual(Scenes.deflector_obstacle.obstacleType, 0, "the deflection sample has nothing to deflect");
  // The two ends of the cost range, which is what the budget governor is assigned against.
  Assert.ok(
    Scenes.hero_detonation.gridResolution >= 128,
    "the hero sample is no longer the expensive one",
  );
  Assert.ok(
    Scenes.far_cheap_plume.gridResolution <= 32,
    "the distant sample is no longer the cheap one",
  );
  Assert.ok(
    Scenes.open_bounds_megaton.boundsWidth > Scenes.camp_fire_steady.boundsWidth * 1.5,
    "the megaton sample no longer stresses the optical normalisation",
  );
});

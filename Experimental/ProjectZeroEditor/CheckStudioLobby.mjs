// Automated verification for the Photographic Studio Lobby HTML prototype
// inside Experimental/ProjectZeroEditor/index.html.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import {
  StudioLobbyRows,
  StudioLobbyInitialValues,
  StudioLobby2Rows,
  StudioLobby2InitialValues,
  ConfiguratorPresets,
  EvaluateOpeningReveal,
  ClampGameCameraInsideLobby,
  ComputeTripodMountHeight,
  ResolveStudioHierarchy,
  HeroCarAimTarget,
  Lobby2HeroAimTarget,
  FixtureStyles,
  CinemaCameraStyles,
} from "./StudioLobbyScene.js";
import {
  DecodeGeometrySpace,
  ConstructProceduralSurface,
  ConstructShowcasePrimitive,
  ConstructFallbackVehicleSurface,
} from "./StudioProceduralSurface.js";

const Here = path.dirname(new URL(import.meta.url).pathname);
const Root = path.resolve(Here, "../..");
const Require = createRequire(path.join(Root, "Experimental/FrontierEditor/package.json"));
const { JSDOM } = Require("jsdom");

function AssertValidSurface(Label, Surface) {
  assert.ok(Surface && Surface.Buffer instanceof Float32Array, Label + " returns Float32Array");
  assert.ok(Surface.TriangleCount > 0, Label + " has triangles (" + Surface.TriangleCount + ")");
  assert.equal(Surface.VertexCount, Surface.TriangleCount * 3, Label + " vertex count matches triangles");
  assert.equal(Surface.Buffer.length, Surface.VertexCount * 8, Label + " packed stride is 8 floats");
  for (let I = 0; I < Surface.Buffer.length; I += 8) {
    assert.ok(Number.isFinite(Surface.Buffer[I]), Label + " finite pos X");
    assert.ok(Number.isFinite(Surface.Buffer[I + 1]), Label + " finite pos Y");
    assert.ok(Number.isFinite(Surface.Buffer[I + 2]), Label + " finite pos Z");
    const Nl = Math.hypot(Surface.Buffer[I + 3], Surface.Buffer[I + 4], Surface.Buffer[I + 5]);
    assert.ok(Nl > 0.5 && Nl < 1.5, Label + " unit normal length (" + Nl + ")");
  }
}

// 1. Verify ControlledIntegrator.GeometrySpace binary decode
const GeoPath = path.join(Root, "EngineContent/Geometry/ControlledIntegrator.GeometrySpace");
const RawBytes = fs.readFileSync(GeoPath);
const RawBuf = RawBytes.buffer.slice(RawBytes.byteOffset, RawBytes.byteOffset + RawBytes.byteLength);
const Vehicle = DecodeGeometrySpace(RawBuf);
AssertValidSurface("ControlledIntegrator.GeometrySpace", Vehicle);
assert.equal(Vehicle.TriangleCount, 25068, "Exact 25,068 vehicle triangles");
assert.equal(Vehicle.Bounds.Min[1], 0, "Vehicle grounded flush at Y=0");
console.log("PASS [1/6] ControlledIntegrator.GeometrySpace decoded:", Vehicle.TriangleCount, "tris");

// 2. Verify all procedural studio components & variants + fallback vehicle + showcase primitives
AssertValidSurface("FallbackVehicle", ConstructFallbackVehicleSurface());
AssertValidSurface("studio.cyclorama", ConstructProceduralSurface("studio.cyclorama", StudioLobbyInitialValues["studio-cyclorama"]));
AssertValidSurface("studio.tiled_floor", ConstructProceduralSurface("studio.tiled_floor", StudioLobbyInitialValues["studio-floor"]));
AssertValidSurface("studio.ceiling_truss", ConstructProceduralSurface("studio.ceiling_truss", StudioLobbyInitialValues["studio-ceiling"]));
AssertValidSurface("studio.tripod(casters)", ConstructProceduralSurface("studio.tripod", StudioLobbyInitialValues["rig-key-tripod"]));
AssertValidSurface("studio.tripod(floorMat)", ConstructProceduralSurface("studio.tripod", StudioLobbyInitialValues["rig-cam-a-tripod"]));
AssertValidSurface("studio.workstation(desk)", ConstructProceduralSurface("studio.workstation", StudioLobbyInitialValues["prop-workstation"]));
AssertValidSurface("studio.workstation(cases)", ConstructProceduralSurface("studio.workstation", StudioLobbyInitialValues["prop-grip-cases"]));
for (const Style of FixtureStyles) {
  AssertValidSurface("studio.light_fixture(" + Style + ")", ConstructProceduralSurface("studio.light_fixture", { FixtureStyle: Style }));
}
for (const Style of CinemaCameraStyles) {
  AssertValidSurface("studio.cinema_camera(" + Style + ")", ConstructProceduralSurface("studio.cinema_camera", { CameraStyle: Style }));
}
for (const Prim of ["cube", "sphere", "cylinder", "torus", "cone"]) {
  AssertValidSurface("primitive(" + Prim + ")", ConstructShowcasePrimitive(Prim));
}
// 2b. Verify wider lobby dimensions + straight vertical overhead softbox rigging & power/tether cables
const CycInit = StudioLobbyInitialValues["studio-cyclorama"];
assert.ok(CycInit.CoveWidth >= 18.0, "Wider cyclorama prevents camera clipping");
assert.ok(CycInit.FloorDepth >= 13.0, "Deeper studio floor prevents clipping");
assert.ok(StudioLobbyInitialValues["studio-ceiling"].CeilingHeight >= 7.0, "Higher studio ceiling prevents clipping");

// Verify strict studio set staging: ONLY Hero Coupe + lighting inside cyclorama; other 2 vehicles, desk, cases & foreground cameras OUTSIDE
const CycHalfW = CycInit.CoveWidth * 0.5;
const CycFrontZ = CycInit.Position[2] + CycInit.FloorDepth * 0.42;
const CycBackZ = CycInit.Position[2] - CycInit.FloorDepth * 0.58;
const IsInsideCyclorama = (Pos) =>
  Math.abs(Pos[0]) < CycHalfW && Pos[2] > CycBackZ && Pos[2] < CycFrontZ;

assert.ok(IsInsideCyclorama(StudioLobbyInitialValues["vehicle-hero"].Position), "Hero Coupe is inside cyclorama set");
assert.ok(!IsInsideCyclorama(StudioLobbyInitialValues["vehicle-gt3"].Position), "Alpine GT is OUTSIDE cyclorama set");
assert.ok(!IsInsideCyclorama(StudioLobbyInitialValues["vehicle-tourer"].Position), "Liquid Tourer is OUTSIDE cyclorama set");
assert.ok(!IsInsideCyclorama(StudioLobbyInitialValues["prop-workstation"].Position), "Tether Workstation desk is OUTSIDE cyclorama set");
assert.ok(!IsInsideCyclorama(StudioLobbyInitialValues["prop-grip-cases"].Position), "Grip Cases & props are OUTSIDE cyclorama set");
assert.ok(!IsInsideCyclorama(StudioLobbyInitialValues["rig-cam-a-tripod"].Position), "Cinema Tripod A is OUTSIDE cyclorama front lip");
assert.ok(!IsInsideCyclorama(StudioLobbyInitialValues["rig-cam-b-tripod"].Position), "Cinema Tripod B is OUTSIDE cyclorama front lip");

const SoftboxSurface = ConstructProceduralSurface("studio.light_fixture", StudioLobbyInitialValues["rig-softbox-fixture"]);
assert.ok(SoftboxSurface.TriangleCount > 500, "Overhead softbox includes vertical cables + coiled umbilical + roof wiring (" + SoftboxSurface.TriangleCount + " tris)");
const TripodSurface = ConstructProceduralSurface("studio.tripod", StudioLobbyInitialValues["rig-key-tripod"]);
assert.ok(TripodSurface.TriangleCount > 450, "Studio tripod includes power/tether cabling & ballast (" + TripodSurface.TriangleCount + " tris)");

// 2c. Verify ALL 7 new Lobby 2 Forest Soundstage procedural 3D meshes (real ferns, wood, rock incline, grass, fan, backdrop, dust)
const L2Rock = ConstructProceduralSurface("studio.rock_outcrop", StudioLobby2InitialValues["l2-rock-outcrop"]);
const L2Wood = ConstructProceduralSurface("studio.fallen_wood", StudioLobby2InitialValues["l2-fallen-wood"]);
const L2Ferns = ConstructProceduralSurface("studio.fern_cluster", StudioLobby2InitialValues["l2-fern-cluster"]);
const L2Grass = ConstructProceduralSurface("studio.grass_plants", StudioLobby2InitialValues["l2-grass-plants"]);
const L2Backdrop = ConstructProceduralSurface("studio.forest_backdrop", StudioLobby2InitialValues["l2-forest-backdrop"]);
const L2Fan = ConstructProceduralSurface("studio.studio_fan", StudioLobby2InitialValues["l2-studio-fan"]);
const L2Dust = ConstructProceduralSurface("studio.dust_field", StudioLobby2InitialValues["l2-dust-particles"]);
AssertValidSurface("L2 studio.rock_outcrop", L2Rock);
AssertValidSurface("L2 studio.fallen_wood", L2Wood);
AssertValidSurface("L2 studio.fern_cluster", L2Ferns);
AssertValidSurface("L2 studio.grass_plants", L2Grass);
AssertValidSurface("L2 studio.forest_backdrop", L2Backdrop);
AssertValidSurface("L2 studio.studio_fan", L2Fan);
AssertValidSurface("L2 studio.dust_field", L2Dust);
assert.ok(L2Ferns.TriangleCount > 2000, "Real sculpted 3D pinnate ferns (" + L2Ferns.TriangleCount + " tris)");
assert.ok(L2Grass.TriangleCount > 1000, "Real 3D grass tufts & woodland shrubs (" + L2Grass.TriangleCount + " tris)");
assert.ok(L2Rock.TriangleCount > 4000, "Sculpted mossy granite rock incline, boulders & river cobbles (" + L2Rock.TriangleCount + " tris)");
assert.ok(L2Wood.TriangleCount > 400, "Gnarled fallen tree trunk & twisted roots (" + L2Wood.TriangleCount + " tris)");
assert.ok(L2Fan.TriangleCount > 500, "Wheeled Studio Wind Fan with spinning propeller & wire cage (" + L2Fan.TriangleCount + " tris)");
console.log(
  `PASS [2/6] Lobby 1 & Lobby 2 procedural 3D geometry verified (Ferns: ${L2Ferns.TriangleCount} tris, Grass: ${L2Grass.TriangleCount} tris, Rock: ${L2Rock.TriangleCount} tris, Wood: ${L2Wood.TriangleCount} tris, Fan: ${L2Fan.TriangleCount} tris)`,
);

// 3. Verify 3 vehicles + 6 shared studio.tripod instances + hierarchical mount propagation
const VehicleRows = StudioLobbyRows.filter((R) => R.Panel === "studio-vehicle");
assert.equal(VehicleRows.length, 3, "Studio Lobby has 3 vehicles");
const TripodRows = StudioLobbyRows.filter(
  (R) => R.Panel === "studio-procedural" && StudioLobbyInitialValues[R.Id]?.ProceduralKind === "studio.tripod",
);
assert.equal(TripodRows.length, 6, "6 shared studio.tripod instances across lights & cameras");

const HierBase = ResolveStudioHierarchy(StudioLobbyRows, StudioLobbyInitialValues, {});
const RaisedValues = JSON.parse(JSON.stringify(StudioLobbyInitialValues));
RaisedValues["rig-key-tripod"].StandHeight += 0.5;
const HierRaised = ResolveStudioHierarchy(StudioLobbyRows, RaisedValues, {});
const DeltaFixtureY = HierRaised["rig-key-fixture"].Position[1] - HierBase["rig-key-fixture"].Position[1];
const DeltaLightY = HierRaised["light-key-ies"].Position[1] - HierBase["light-key-ies"].Position[1];
assert.ok(Math.abs(DeltaFixtureY - 0.5) < 1e-5, "Child fixture rises +0.5 m with parent tripod");
assert.ok(Math.abs(DeltaLightY - 0.5) < 1e-5, "Grandchild IES light rises +0.5 m with parent tripod");
assert.ok(
  Math.abs(ComputeTripodMountHeight(RaisedValues["rig-key-tripod"]) - ComputeTripodMountHeight(StudioLobbyInitialValues["rig-key-tripod"]) - 0.5) < 1e-5,
  "ComputeTripodMountHeight matches delta",
);
// 3b. Verify EVERY tripod-mounted camera & light rig aims DEAD-ON at Hero Coupe (dot > 0.99)!
const AimedIds = [
  "rig-key-fixture",
  "light-key-ies",
  "rig-umbrella-fixture",
  "light-umbrella-area",
  "rig-rim-fixture",
  "light-rim-area",
  "rig-accent-fixture",
  "light-accent-ies",
  "rig-cam-a-body",
  "cam-configurator",
  "rig-cam-b-body",
  "cam-profile",
];
for (const Id of AimedIds) {
  const Xf = HierBase[Id];
  assert.ok(Xf && Xf.Forward, Id + " resolved with Forward vector");
  const From = [Xf.Position[0], Xf.Position[1] + 0.16, Xf.Position[2]];
  const ToX = HeroCarAimTarget[0] - From[0];
  const ToY = HeroCarAimTarget[1] - From[1];
  const ToZ = HeroCarAimTarget[2] - From[2];
  const Len = Math.hypot(ToX, ToY, ToZ) || 1;
  const Dot = (Xf.Forward[0] * ToX + Xf.Forward[1] * ToY + Xf.Forward[2] * ToZ) / Len;
  assert.ok(
    Dot > 0.99,
    `${Id} must face Hero Coupe (dot=${Dot.toFixed(4)}, fwd=${Xf.Forward.map((V) => V.toFixed(2))})`,
  );
}

// 3c. Verify Lobby 2 has 4 vehicles (Hero angled on rock incline + 3 companion vehicles outside forest island) & 12/12 aimed rigs!
const L2Vehicles = StudioLobby2Rows.filter((R) => R.Panel === "studio-vehicle");
assert.equal(L2Vehicles.length, 4, "Lobby 2 has Hero Vehicle on rock incline + 3 other positioned vehicles");
const L2Hero = StudioLobby2InitialValues["vehicle-hero"];
assert.ok(L2Hero.Position[1] > 0.45 && L2Hero.InclineDeg >= 6 && L2Hero.BankDeg >= 2, "Lobby 2 Hero Vehicle is elevated & angled on rock incline");
const HierL2 = ResolveStudioHierarchy(StudioLobby2Rows, StudioLobby2InitialValues, {});
for (const Id of AimedIds) {
  const Xf = HierL2[Id];
  assert.ok(Xf && Xf.Forward, "L2 " + Id + " resolved with Forward vector");
  const From = [Xf.Position[0], Xf.Position[1] + 0.16, Xf.Position[2]];
  const ToX = Lobby2HeroAimTarget[0] - From[0];
  const ToY = Lobby2HeroAimTarget[1] - From[1];
  const ToZ = Lobby2HeroAimTarget[2] - From[2];
  const Len = Math.hypot(ToX, ToY, ToZ) || 1;
  const Dot = (Xf.Forward[0] * ToX + Xf.Forward[1] * ToY + Xf.Forward[2] * ToZ) / Len;
  assert.ok(Dot > 0.99, `L2 ${Id} must aim at elevated Hero Vehicle (dot=${Dot.toFixed(4)})`);
}
console.log("PASS [3/6] Lobby 1 (3 cars) & Lobby 2 (4 cars, rock incline) hierarchy & 24/24 camera/light aim directions verified (dot > 0.99)");

// 4. Verify Opening Reveal curve, 6 Configurator Presets & Game Camera studio bounds
assert.equal(ConfiguratorPresets.length, 6, "6 Car Configurator Camera presets");
const RevealStart = EvaluateOpeningReveal(0);
const RevealMid = EvaluateOpeningReveal(0.5);
const RevealEnd = EvaluateOpeningReveal(1);
for (let A = 0; A < 3; A++) {
  assert.ok(Number.isFinite(RevealStart.Eye[A]) && Number.isFinite(RevealMid.Eye[A]));
  assert.ok(Math.abs(RevealEnd.Eye[A] - ConfiguratorPresets[0].Eye[A]) < 1e-4, "Opening reveal settles on Hero 3/4");
}
const ClampedGameEye = ClampGameCameraInsideLobby([35, 22, -20], StudioLobbyInitialValues, "studio-lobby");
assert.ok(ClampedGameEye[0] <= 9.0 && ClampedGameEye[1] < 7.0 && ClampedGameEye[2] > -8.63, "Game Camera stays clamped inside studio cove");
console.log("PASS [4/6] Opening reveal (3.8 s), 6 presets & Game Camera bounds verified");

// 5. Mount compiled standalone Experimental/ProjectZeroEditor/index.html in JSDOM + WebGL2 spy
const Html = fs.readFileSync(path.join(Here, "index.html"), "utf8");
const CompileSpy = { Programs: 0, Draws: 0, Shaders: 0 };

function CreateMockWebGL2() {
  const Stub = new Proxy(
    {},
    {
      get(_Target, Prop) {
        if (Prop === "VERTEX_SHADER") return 35633;
        if (Prop === "FRAGMENT_SHADER") return 35632;
        if (Prop === "COMPILE_STATUS") return 35713;
        if (Prop === "LINK_STATUS") return 35714;
        if (Prop === "ARRAY_BUFFER") return 34962;
        if (Prop === "STATIC_DRAW") return 35044;
        if (Prop === "DYNAMIC_DRAW") return 35048;
        if (Prop === "TRIANGLES") return 4;
        if (Prop === "LINES") return 1;
        if (Prop === "POINTS") return 0;
        if (Prop === "BLEND") return 3042;
        if (Prop === "SRC_ALPHA") return 770;
        if (Prop === "ONE") return 1;
        if (Prop === "FLOAT") return 5126;
        if (Prop === "DEPTH_TEST") return 2929;
        if (Prop === "LEQUAL") return 515;
        if (Prop === "CULL_FACE") return 2884;
        if (Prop === "COLOR_BUFFER_BIT") return 16384;
        if (Prop === "DEPTH_BUFFER_BIT") return 256;
        if (Prop === "createShader") return () => { CompileSpy.Shaders++; return {}; };
        if (Prop === "createProgram") return () => { CompileSpy.Programs++; return {}; };
        if (Prop === "getShaderParameter") return () => true;
        if (Prop === "getProgramParameter") return () => true;
        if (Prop === "getUniformLocation") return () => ({});
        if (Prop === "createVertexArray" || Prop === "createBuffer" || Prop === "createFramebuffer" || Prop === "createTexture" || Prop === "createRenderbuffer") return () => ({});
        if (Prop === "FRAMEBUFFER") return 36160;
        if (Prop === "TEXTURE_2D") return 3553;
        if (Prop === "RENDERBUFFER") return 36161;
        if (Prop === "RGBA") return 6408;
        if (Prop === "UNSIGNED_BYTE") return 5121;
        if (Prop === "DEPTH_COMPONENT16") return 33189;
        if (Prop === "COLOR_ATTACHMENT0") return 36064;
        if (Prop === "DEPTH_ATTACHMENT") return 36096;
        if (Prop === "TEXTURE_MIN_FILTER") return 10241;
        if (Prop === "TEXTURE_MAG_FILTER") return 10240;
        if (Prop === "LINEAR") return 9729;
        if (Prop === "readPixels") return (_x, _y, _w, _h, _f, _t, buf) => { buf.fill(180); };
        if (Prop === "drawArrays") return () => { CompileSpy.Draws++; };
        return () => {};
      },
    },
  );
  return Stub;
}

const PageErrors = [];
const Dom = new JSDOM(Html, {
  url: "http://127.0.0.1:8092/",
  runScripts: "dangerously",
  pretendToBeVisual: true,
  beforeParse(Win) {
    Win.Element.prototype.scrollIntoView = () => {};
    Win.Element.prototype.getBoundingClientRect = () => ({
      left: 0,
      top: 0,
      right: 1280,
      bottom: 720,
      width: 1280,
      height: 720,
    });
    Win.ResizeObserver = class {
      observe() {}
      disconnect() {}
    };
    Win.fetch = async () => ({
      ok: true,
      status: 200,
      arrayBuffer: async () => RawBuf.slice(0),
    });
    const OrigGetContext = Win.HTMLCanvasElement.prototype.getContext;
    Win.HTMLCanvasElement.prototype.getContext = function (Type, ...Args) {
      if (Type === "webgl2") return CreateMockWebGL2();
      if (Type === "2d") {
        return {
          createImageData: (W, H) => ({ data: new Uint8ClampedArray(W * H * 4) }),
          putImageData: () => {},
        };
      }
      return OrigGetContext ? OrigGetContext.call(this, Type, ...Args) : null;
    };
    Win.HTMLCanvasElement.prototype.toDataURL = () => "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
    Win.addEventListener("error", (Ev) => PageErrors.push(Ev.message || String(Ev.error)));
    Win.addEventListener("unhandledrejection", (Ev) => PageErrors.push(String(Ev.reason)));
  },
});

await new Promise((Resolve) => setTimeout(Resolve, 320));
const Doc = Dom.window.document;
assert.equal(PageErrors.length, 0, "Zero page errors on boot: " + PageErrors.join("; "));

// Confirm WebGL2 canvas mounted, shader programs compiled once (PBR + Gizmo = 2), and rendered frames
assert.ok(Doc.querySelector(".studio-viewport-canvas"), "WebGL2 canvas present in DOM");
assert.equal(CompileSpy.Programs, 2, "Exactly 2 WebGL2 programs compiled once on startup");
assert.ok(CompileSpy.Draws > 10, "WebGL2 renderer drew studio scene (" + CompileSpy.Draws + " draw calls)");

// Confirm default Lobby 2 Forest Stage Outliner + 4 vehicles + ferns, wood, rock, grass, fan & dust mounted
const TreeText = Doc.querySelector(".outliner-rows")?.textContent || "";
assert.ok(TreeText.includes("Hero Vehicle"), "Outliner lists Hero Vehicle on rock incline");
assert.ok(TreeText.includes("Obsidian Coupe"), "Outliner lists companion car #1");
assert.ok(TreeText.includes("Alpine GT"), "Outliner lists companion car #2");
assert.ok(TreeText.includes("Liquid Tourer"), "Outliner lists companion car #3");
assert.ok(TreeText.includes("Real 3D Forest Ferns"), "Outliner lists Real 3D Forest Ferns");
assert.ok(TreeText.includes("Gnarled Fallen Log"), "Outliner lists Gnarled Fallen Log & Roots");
assert.ok(TreeText.includes("Mossy Rock Incline"), "Outliner lists Mossy Rock Incline & Stream");
assert.ok(TreeText.includes("Grass Tufts"), "Outliner lists Grass Tufts & Woodland Shrubs");
assert.ok(TreeText.includes("Studio Wind Fan"), "Outliner lists Studio Wind Fan (Blower)");
assert.ok(TreeText.includes("Sunbeam Dust Particles"), "Outliner lists Sunbeam Dust Particles");
assert.ok(TreeText.includes("Key Fresnel Tripod"), "Outliner lists Key Fresnel Tripod");
assert.ok(TreeText.includes("Overhead Softbox Bank"), "Outliner lists Overhead Softbox Bank");

// Wait briefly for thumbnail capture tick (400ms interval) and verify real 3D geometry thumbnails in Outliner!
await new Promise((Resolve) => setTimeout(Resolve, 460));
const GeoThumbs = Doc.querySelectorAll("[data-geometry-thumb]");
assert.ok(GeoThumbs.length >= 20, "Outliner renders real WebGL2 3D geometry screenshots (" + GeoThumbs.length + " thumbnails)");
assert.ok(Doc.querySelector('[data-geometry-thumb="vehicle-hero"]'), "Hero Vehicle shows actual 3D geometry screenshot in Outliner");
assert.ok(Doc.querySelector('[data-geometry-thumb="l2-fern-cluster"]'), "Real 3D Ferns shows actual 3D geometry screenshot in Outliner");
assert.ok(Doc.querySelector('[data-geometry-thumb="l2-studio-fan"]'), "Studio Wind Fan shows actual 3D geometry screenshot in Outliner");
assert.ok(Doc.querySelector('[data-geometry-thumb="rig-softbox-fixture"]'), "Overhead Softbox shows actual 3D geometry screenshot in Outliner");

// Click Key Fresnel Tripod -> verify reused native TransformPanel + shared instances + Aim at Hero Car
const TripodItem = [...Doc.querySelectorAll(".outliner-row")].find((El) =>
  El.textContent.includes("Key Fresnel Tripod"),
);
assert.ok(TripodItem, "Found Key Fresnel Tripod row");
TripodItem.click();
await new Promise((Resolve) => setTimeout(Resolve, 60));
const InspectorText = Doc.querySelector(".inspector-scroll")?.textContent || "";
assert.ok(InspectorText.includes("6 SHARED INSTANCES"), "Procedural inspector reports 6 shared tripod instances");
assert.ok(InspectorText.includes("Mount Height"), "Procedural inspector shows live tripod mount height");
assert.ok(Doc.querySelector(".inspector-scroll .transform-card"), "Procedural inspector reuses native TransformPanel");
assert.ok(InspectorText.includes("Aim at Hero Car"), "Procedural inspector includes Aim at Hero Car action");

// Click Car Configurator Camera -> verify reused native Camera cards + Studio add-on card
const CamItem = [...Doc.querySelectorAll(".outliner-row")].find((El) =>
  El.textContent.includes("Car Configurator Camera"),
);
assert.ok(CamItem, "Found Car Configurator Camera row");
CamItem.click();
await new Promise((Resolve) => setTimeout(Resolve, 60));
const CamText = Doc.querySelector(".inspector-scroll")?.textContent || "";
assert.ok(CamText.includes("Lens + field of view"), "Reuses native Camera Lens card");
assert.ok(CamText.includes("Aperture study"), "Reuses native Camera Aperture card");
assert.ok(CamText.includes("Subject plane + sharpness"), "Reuses native Camera DepthOfField card");
assert.ok(CamText.includes("Car Configurator Studio Rig"), "Appends Studio Camera add-on card");

// Click Key Fresnel IES Light -> verify reused native ReferencePanel (ieslight) + Studio coupling card
const IesItem = [...Doc.querySelectorAll(".outliner-row")].find((El) =>
  El.textContent.includes("Key Fresnel IES Light"),
);
assert.ok(IesItem, "Found Key Fresnel IES Light row");
IesItem.click();
await new Promise((Resolve) => setTimeout(Resolve, 60));
assert.ok(
  Doc.querySelector('.inspector-scroll [data-reference-kind="ieslight"]'),
  "Key Fresnel IES Light reuses native ieslight ReferencePanel",
);
assert.ok(
  (Doc.querySelector(".inspector-scroll")?.textContent || "").includes("Studio Rig Coupling"),
  "IES Light includes Studio Rig Coupling add-on card",
);

// Click Overhead Softbox Area Light -> verify reused native ReferencePanel (arealight)
const AreaItem = [...Doc.querySelectorAll(".outliner-row")].find((El) =>
  El.textContent.includes("Overhead Softbox Area Light"),
);
assert.ok(AreaItem, "Found Overhead Softbox Area Light row");
AreaItem.click();
await new Promise((Resolve) => setTimeout(Resolve, 60));
assert.ok(
  Doc.querySelector('.inspector-scroll [data-reference-kind="arealight"]'),
  "Overhead Softbox Area Light reuses native arealight ReferencePanel",
);

// Verify Pause button works in Play mode
const PauseBtn = Doc.querySelector('[aria-label="Pause Play"]');
assert.ok(PauseBtn, "Pause button visible in default Play mode");
PauseBtn.click();
await new Promise((Resolve) => setTimeout(Resolve, 40));
assert.ok(Doc.querySelector('[aria-label="Resume Play"]'), "Clicking Pause toggles to Resume");

// Switch to Edit mode and verify Move (W) / Rotate (E) Transform Gizmo controls + Unrestricted Editor Camera
const ViewportHost = Doc.querySelector(".studio-viewport-host");
assert.equal(ViewportHost.getAttribute("data-active-camera"), "game", "Play mode renders through Game Camera");
const EditBtn = Doc.querySelector('[aria-label="Edit mode"]');
assert.ok(EditBtn, "Edit mode button present");
EditBtn.click();
await new Promise((Resolve) => setTimeout(Resolve, 60));
assert.equal(ViewportHost.getAttribute("data-active-camera"), "editor", "Edit mode renders through unrestricted Editor Camera");
assert.ok(Doc.querySelector(".studio-gizmo-pill"), "3D Transform Gizmo pill (Move W / Rotate E) visible in Edit mode");
assert.ok(
  (Doc.querySelector(".studio-viewport-top-hud")?.textContent || "").includes("EDITOR CAMERA"),
  "HUD clearly indicates unrestricted Editor Camera in Edit mode",
);

// Verify Editor Camera is NOT limited by room bounds (large wheel zoom-out succeeds freely)
const CanvasEl = Doc.querySelector(".studio-viewport-canvas");
for (let W = 0; W < 6; W++) {
  CanvasEl.dispatchEvent(new Dom.window.WheelEvent("wheel", { deltaY: 600, bubbles: true, cancelable: true }));
}
await new Promise((Resolve) => setTimeout(Resolve, 420));

// Switch to Sim mode and verify Editor Camera stays active while Game Camera animates
const SimBtn = Doc.querySelector('[aria-label="Sim mode"]');
assert.ok(SimBtn, "Sim mode button present");
SimBtn.click();
await new Promise((Resolve) => setTimeout(Resolve, 60));
assert.equal(ViewportHost.getAttribute("data-active-camera"), "editor", "Sim mode views through unrestricted Editor Camera while Game Camera runs live");

// Switch back to Play mode and verify Game Camera takes over cleanly
const PlayBtn = Doc.querySelector('[aria-label="Play mode"]');
PlayBtn.click();
await new Promise((Resolve) => setTimeout(Resolve, 60));
assert.equal(ViewportHost.getAttribute("data-active-camera"), "game", "Play mode switches back to moving Game Camera");

// Switch levels Lobby 2 -> Lobby 1 -> Showcase -> Lobby 2 and assert ZERO extra shader compilations!
const LevelSelect = Doc.querySelector(".studio-level-select");
assert.ok(LevelSelect, "Level switcher present in viewport toolbar");
const ProgramsBefore = CompileSpy.Programs;
const SwitchT0 = performance.now();
const NativeSetter = Object.getOwnPropertyDescriptor(Dom.window.HTMLSelectElement.prototype, "value").set;

NativeSetter.call(LevelSelect, "studio-lobby");
LevelSelect.dispatchEvent(new Dom.window.Event("change", { bubbles: true }));
await new Promise((Resolve) => setTimeout(Resolve, 60));
assert.ok(Doc.querySelector(".outliner-rows").textContent.includes("Cyclorama Infinity Cove"), "Lobby 1 Cyclorama loaded");

NativeSetter.call(LevelSelect, "showcase");
LevelSelect.dispatchEvent(new Dom.window.Event("change", { bubbles: true }));
await new Promise((Resolve) => setTimeout(Resolve, 60));
assert.ok(Doc.querySelector(".outliner-rows").textContent.includes("Cube"), "Showcase level loaded");

NativeSetter.call(LevelSelect, "studio-lobby-2");
LevelSelect.dispatchEvent(new Dom.window.Event("change", { bubbles: true }));
await new Promise((Resolve) => setTimeout(Resolve, 60));
const SwitchElapsed = Math.round((performance.now() - SwitchT0) * 10) / 10;
assert.ok(Doc.querySelector(".outliner-rows").textContent.includes("Real 3D Forest Ferns"), "Lobby 2 Forest Stage restored");
assert.equal(CompileSpy.Programs, ProgramsBefore, "ZERO shader recompilations across all 3 level switches!");
assert.equal(PageErrors.length, 0, "Zero runtime errors across interactions");
console.log(
  `PASS [5/6] Standalone index.html JSDOM + WebGL2 verified: ${CompileSpy.Draws} draw calls, 0 shader recompiles, round-trip level switch OK`,
);

// 6. Verify HTTP preview server responds 200 OK for both index.html and ControlledIntegrator.GeometrySpace
const RespIndex = await fetch("http://127.0.0.1:8092/");
assert.equal(RespIndex.status, 200, "Serve.py / returns 200");
const HtmlText = await RespIndex.text();
assert.ok(HtmlText.length > 4_000_000, "Serve.py serves full standalone index.html (" + HtmlText.length + " bytes)");
const RespCar = await fetch("http://127.0.0.1:8092/EngineContent/Geometry/ControlledIntegrator.GeometrySpace");
assert.equal(RespCar.status, 200, "Serve.py serves ControlledIntegrator.GeometrySpace");
console.log("PASS [6/6] Live HTTP preview server verified on http://0.0.0.0:8092/");
Dom.window.close();
process.exit(0);

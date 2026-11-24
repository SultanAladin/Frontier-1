// Automated verification for the Photographic Studio Lobby HTML prototype
// inside Experimental/ProjectZeroEditor/index.html.
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { createRequire } from "node:module";
import {
  StudioLobbyRows,
  StudioLobbyInitialValues,
  ConfiguratorPresets,
  EvaluateOpeningReveal,
  ComputeTripodMountHeight,
  ResolveStudioHierarchy,
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
console.log("PASS [2/6] All 7 procedural studio component families & 5 showcase primitives verified");

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
console.log("PASS [3/6] 3 vehicles, 6 shared tripods, and parent->child->grandchild mount propagation verified");

// 4. Verify Opening Reveal curve & 6 Configurator Presets
assert.equal(ConfiguratorPresets.length, 6, "6 Car Configurator Camera presets");
const RevealStart = EvaluateOpeningReveal(0);
const RevealMid = EvaluateOpeningReveal(0.5);
const RevealEnd = EvaluateOpeningReveal(1);
for (let A = 0; A < 3; A++) {
  assert.ok(Number.isFinite(RevealStart.Eye[A]) && Number.isFinite(RevealMid.Eye[A]));
  assert.ok(Math.abs(RevealEnd.Eye[A] - ConfiguratorPresets[0].Eye[A]) < 1e-4, "Opening reveal settles on Hero 3/4");
}
console.log("PASS [4/6] Opening reveal (3.8 s) and 6 Car Configurator presets verified");

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
        if (Prop === "createVertexArray" || Prop === "createBuffer") return () => ({});
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
      return OrigGetContext ? OrigGetContext.call(this, Type, ...Args) : null;
    };
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

// Confirm default Studio Lobby Outliner + Vehicle Inspector mounted
const TreeText = Doc.querySelector(".outliner-rows")?.textContent || "";
assert.ok(TreeText.includes("Hero Coupe"), "Outliner lists Hero Coupe");
assert.ok(TreeText.includes("Alpine GT"), "Outliner lists Alpine GT");
assert.ok(TreeText.includes("Liquid Tourer"), "Outliner lists Liquid Tourer");
assert.ok(TreeText.includes("Key Fresnel Tripod"), "Outliner lists Key Fresnel Tripod");
assert.ok(TreeText.includes("Overhead Softbox Bank"), "Outliner lists Overhead Softbox Bank");

// Click Key Fresnel Tripod -> verify StudioProceduralInspector renders shared instances count
const TripodItem = [...Doc.querySelectorAll(".outliner-row")].find((El) =>
  El.textContent.includes("Key Fresnel Tripod"),
);
assert.ok(TripodItem, "Found Key Fresnel Tripod row");
TripodItem.click();
await new Promise((Resolve) => setTimeout(Resolve, 60));
const InspectorText = Doc.querySelector(".inspector-scroll")?.textContent || "";
assert.ok(InspectorText.includes("6 SHARED INSTANCES"), "Procedural inspector reports 6 shared tripod instances");
assert.ok(InspectorText.includes("Mount Height"), "Procedural inspector shows live tripod mount height");

// Verify Pause button works in Play mode
const PauseBtn = Doc.querySelector('[aria-label="Pause Play"]');
assert.ok(PauseBtn, "Pause button visible in default Play mode");
PauseBtn.click();
await new Promise((Resolve) => setTimeout(Resolve, 40));
assert.ok(Doc.querySelector('[aria-label="Resume Play"]'), "Clicking Pause toggles to Resume");

// Switch level Studio Lobby -> Showcase -> Studio Lobby and assert ZERO extra shader compilations!
const LevelSelect = Doc.querySelector(".studio-level-select");
assert.ok(LevelSelect, "Level switcher present in viewport toolbar");
const ProgramsBefore = CompileSpy.Programs;
const SwitchT0 = performance.now();
const NativeSetter = Object.getOwnPropertyDescriptor(Dom.window.HTMLSelectElement.prototype, "value").set;
NativeSetter.call(LevelSelect, "showcase");
LevelSelect.dispatchEvent(new Dom.window.Event("change", { bubbles: true }));
await new Promise((Resolve) => setTimeout(Resolve, 60));
assert.ok(Doc.querySelector(".outliner-rows").textContent.includes("Cube"), "Showcase level loaded");

NativeSetter.call(LevelSelect, "studio-lobby");
LevelSelect.dispatchEvent(new Dom.window.Event("change", { bubbles: true }));
await new Promise((Resolve) => setTimeout(Resolve, 60));
const SwitchElapsed = Math.round((performance.now() - SwitchT0) * 10) / 10;
assert.ok(Doc.querySelector(".outliner-rows").textContent.includes("Hero Coupe"), "Studio Lobby restored");
assert.equal(CompileSpy.Programs, ProgramsBefore, "ZERO shader recompilations across level switches!");
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

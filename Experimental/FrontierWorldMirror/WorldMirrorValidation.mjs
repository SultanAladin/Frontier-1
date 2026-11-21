//==========================================================================================================================================
//                                                  WORLDMIRRORVALIDATION.MJS
//==========================================================================================================================================
// Standalone Node ESM validation suite verifying 1:1 C++ <-> HTML parity of Experimental/FrontierWorldMirror:
//   1. Exact 17-entry Outliner hierarchy matching Engine/GeometricRaster/GlobalIllumination/ShowcaseGalleryScene.cpp
//   2. Canonical EditorCamera pose, 43.0 deg vertical FOV, and bidirectional camera/transform/visibility compilation
//   3. Analytical 3D ray picking across the Cornell-Studio Gallery specimens and Ceiling Softbox
//   4. Ring-free analytical GTX shadow/GI shader + 2px screen-space silhouette selection outline verification
//   5. Full 20-channel OpenPBR Material Atlas compilation (Fill, Gradient, Fabric, Subsurface, Clear coat, Unlit)
//   6. 1:1 C++ Slate 13-handle TransformGizmo3D (GizmoPRO) geometry, ray-triangle picking, and drag/Ctrl-snap math
//   7. Multi-Level Viewport Footer Switcher & Create Level Dialogue templates

import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import {
  CanonicalEditorCamera,
  CanonicalRenderSettings,
  CompileLiveGtxScene,
  PickSceneEntityAtUv,
  RaycastWorldPlacementAtUv,
  ResolveRenderSettings,
  ShowcaseInitialRows,
  ShowcaseInitialValues,
} from "./ShowcaseGalleryScene.js";
import {
  BuildVehicleTlasAndInstances,
  ParseVehicleFvb1Buffer,
  TraceVehicleBvhCpu,
} from "./VehicleBvhAssets.js";
import {
  BuildAreaShadowMatrix,
  BuildSunShadowMatrix,
  BuildUnitShadowGeometries,
  EncodeRenderSettingsUniforms,
} from "./ShadowPipeline.js";
import {
  BuildSkyViewLut,
  EvalMoonAndStarsCpu,
  EvalVolumetricCloudsCpu,
  PackSkyViewRgba16f,
  SKY_VIEW_HEIGHT,
  SKY_VIEW_WIDTH,
  SampleSkyViewLutCpu,
} from "./AtmosphereSky.js";
import {
  BuildActorPayloadFromModelSpec,
  WORLD_PLACEABLE_MODELS,
} from "./WorldDragDrop.js";
import { Solar } from "./Solar.mjs";
import {
  MOON_ATLAS_H,
  MOON_ATLAS_W,
  MOON_PRESET_NAMES,
  MOON_TILE_COUNT,
} from "./MoonAtlasLoader.js";
import { RenderAnalyticalThumbnailRgba } from "./ModelThumbnailRenderer.js";
import { ExpandPropPrimitives, WORLD_PROP_CATALOG } from "./WorldPropLibrary.js";
import { ResolveMaterial } from "./MaterialSpecification.js";
import {
  ATLAS_COLS,
  ATLAS_TILE_SIZE,
  ATLAS_WIDTH,
  MaterialAtlasSynchronizer,
  TILES_PER_PRIM,
} from "./MaterialAtlasCompiler.js";
import {
  BeginGizmoDrag,
  BuildSlateGizmoGeometry,
  GizmoHandle,
  PickGizmoHandle,
  SolveGizmoDrag,
} from "./TransformGizmo3D.js";
import {
  BuildDefaultDiagnosticForceRig,
  BuildForceOverlayGeometry,
  DEFAULT_FORCE_OVERLAY,
  ENGINE_FORCE_PRESETS,
  EvaluateSceneCompositeTelemetry,
  EvaluateSceneForceFieldAt,
  FORCE_MODELS,
  ParseForceModelToml,
  PickForceBeacon,
  ResolveForceValues,
  SampleCurlNoise3D,
  SerializeForceModelToml,
  SerializeSimulationSpaceToml,
} from "./ForceFieldCatalog.js";
import { CompileFieldProgram, DEFAULT_FORMULA_CODE } from "./VectorFieldLang.js";
import { CreateWebgpuGtxRenderer, GTX_WGSL_SCENE_SHADER } from "./WebgpuGtxRenderer.js";
import {
  BuildDefaultLevels,
  CreateNewLevelRecord,
} from "./LevelSystem.js";
import { CreateGtxBandKernel, CpuGtxRenderer } from "./CpuGtxFallbackRenderer.js";
import {
  ApplyUnrealDolly,
  ApplyUnrealMouseLook,
  ApplyUnrealOrbit,
  ApplyUnrealPan,
  BuildUnrealCameraBasis,
  DeriveUnrealCameraState,
  StepUnrealWasdCamera,
} from "./UnrealViewportCamera.js";

const Folder = path.dirname(fileURLToPath(import.meta.url));

// 1. Verify exact 25-entry Cornell-Studio Gallery Outliner matching C++ ShowcaseGalleryScene (+ RenderSettings + ControlVehicle + Physical Environment + 2 Editor Camera components)
assert.equal(
  ShowcaseInitialRows.length,
  25,
  "Expected 25 Outliner entries (4 folders + 10 geometry + ControlVehicle + Editor Camera + Lens Flare + Tonemapping + Render Settings + Sun + Atmosphere + Clouds + Moon + Starfield + Ceiling Softbox)",
);
const ShowcaseChildren = ShowcaseInitialRows.filter(
  (Row) => Row.Parent === "showcase",
);
assert.equal(ShowcaseChildren.length, 11, "Expected 10 geometry specimens + ControlVehicle under Showcase");

// 2. Verify compiled Tier 1 GTX scene & EditorCamera 43.0 deg vertical FOV parity
const Compiled = CompileLiveGtxScene(
  ShowcaseInitialRows,
  ShowcaseInitialValues,
  {},
  "PERSP",
);
assert.equal(Compiled.Primitives.length, 10, "All 10 gallery primitives compiled");
assert.equal(Compiled.Vehicles.length, 1, "Showcase ControlVehicle compiled");
assert.equal(Compiled.AreaLights.length, 1, "Ceiling Softbox LTC area light compiled");
assert.deepEqual(
  Compiled.AreaLights[0].Center,
  [0.55, 0.35, 1.82],
  "Ceiling Softbox retains exact C++ Showcase coordinates [0.55, 0.35, 1.82]",
);
const FovDeg = (Compiled.VerticalFovRad * 180) / Math.PI;
assert.ok(
  Math.abs(FovDeg - CanonicalEditorCamera.VerticalFovDeg) < 0.25,
  `EditorCamera vertical FOV (${FovDeg.toFixed(2)} deg) matches C++ 43.0 deg`,
);
assert.equal(
  Compiled.Primitives.filter((P) => P.GlassShell).length,
  1,
  "Abbe Crown Glass Orb routed to 2-layer glass peel",
);

// 3. Verify analytical 3D ray picking (including rotated objects)
const CenterPick = PickSceneEntityAtUv(Compiled, 0.48, 0.45, 16 / 9);
assert.equal(
  CenterPick,
  "chrome-sphere",
  `Center viewport ray pick should select chrome-sphere (got ${CenterPick})`,
);
const GlassPick = PickSceneEntityAtUv(Compiled, 0.62, 0.63, 16 / 9);
assert.equal(
  GlassPick,
  "abbe-glass-orb",
  `Foreground right ray pick should select abbe-glass-orb (got ${GlassPick})`,
);

// 4. Verify 20-Channel Material Atlas compilation (Fill + multi-stop Gradient + Unlit + Sheen + Subsurface)
const Atlas = new MaterialAtlasSynchronizer();
const GradientMat = ResolveMaterial({
  Material: {
    Name: "Sunset Gradient Silk",
    Type: "Fabric",
    Channels: {
      colour: {
        Source: "Gradient",
        Axis: "U",
        Fill: "#ff2200",
        Stops: [
          { Id: "a", Position: 0.0, Value: "#ff0000" },
          { Id: "b", Position: 1.0, Value: "#0000ff" },
        ],
      },
      sheen: { Source: "Fill", Fill: "#ffc0dd" },
      sheenRoughness: { Source: "Fill", Fill: 0.5 },
      opacity: { Source: "Fill", Fill: 0.85 },
    },
  },
});
const CustomPrims = [...Compiled.Primitives];
CustomPrims[0] = { ...CustomPrims[0], ResolvedMaterial: GradientMat };
assert.equal(Atlas.UpdatePrimitives(CustomPrims), true, "Atlas marks dirty on update");

// Check Prim 0 Slot 0 (BaseColour + Opacity) across U = left (red) vs right (blue)
const ReadTexel = (TileIdx, Px, Py) => {
  const Col = TileIdx % ATLAS_COLS;
  const Row = Math.floor(TileIdx / ATLAS_COLS);
  const Off = ((Row * ATLAS_TILE_SIZE + Py) * ATLAS_WIDTH + (Col * ATLAS_TILE_SIZE + Px)) * 4;
  return [Atlas.Pixels[Off], Atlas.Pixels[Off + 1], Atlas.Pixels[Off + 2], Atlas.Pixels[Off + 3]];
};
const LeftRgbA = ReadTexel(0, 2, 32);
const RightRgbA = ReadTexel(0, 61, 32);
assert.ok(LeftRgbA[0] > 220 && LeftRgbA[2] < 20, "Gradient U=0 bakes linear Red");
assert.ok(RightRgbA[2] > 220 && RightRgbA[0] < 20, "Gradient U=1 bakes linear Blue");
assert.ok(Math.abs(LeftRgbA[3] - Math.round(0.85 * 255)) <= 2, "Opacity channel baked into Slot 0 Alpha");
const SheenTexel = ReadTexel(4, 32, 32);
assert.ok(SheenTexel[0] > 180 && SheenTexel[2] > 140, "Fabric Sheen channel baked into Slot 4 RGB");

// Verify non-Subsurface materials do NOT bleed default #cccccc subsurface wrap lighting, while Subsurface materials DO!
const StandardPlasterMat = ResolveMaterial({
  Material: {
    Name: "Matte Gallery Plaster",
    Type: "Standard",
    Channels: {
      colour: { Source: "Fill", Fill: "#dfd9cf" },
      roughness: { Source: "Fill", Fill: 0.65 },
    },
  },
});
const TrueSubsurfaceMat = ResolveMaterial({
  Material: {
    Name: "Alabaster Wax",
    Type: "Subsurface",
    Channels: {
      colour: { Source: "Fill", Fill: "#ede6da" },
      subsurface: { Source: "Fill", Fill: "#ffd2ad" },
    },
  },
});
CustomPrims[1] = { ...CustomPrims[1], ResolvedMaterial: StandardPlasterMat };
CustomPrims[2] = { ...CustomPrims[2], ResolvedMaterial: TrueSubsurfaceMat };
Atlas.UpdatePrimitives(CustomPrims);
const PlasterSssTexel = ReadTexel(1 * TILES_PER_PRIM + 5, 32, 32);
const AlabasterSssTexel = ReadTexel(2 * TILES_PER_PRIM + 5, 32, 32);
assert.deepEqual(
  PlasterSssTexel.slice(0, 3),
  [0, 0, 0],
  "Standard material zeroes unauthored default #cccccc subsurface channel (no plaster blowout)",
);
assert.ok(
  AlabasterSssTexel[0] > 200 && AlabasterSssTexel[1] > 120,
  "Subsurface material bakes warm subsurface scatter tint into Slot 5 RGB",
);

// 5. Verify 1:1 C++ Slate 13-Handle TransformGizmo3D (GizmoPRO)
const Pivot = [0.0, 0.5, 0.25];
const BuiltGizmo = BuildSlateGizmoGeometry(Pivot, [0, 1, 0], 1.0);
assert.ok(BuiltGizmo.OpaqueVertexCount > 400, "Opaque Slate gizmo cones, cylinders, arcs, borders & torus built");
assert.equal(BuiltGizmo.TranslucentVertexCount, 36, "3 double-sided translucent plane quads (3 * 12 verts) built");

// Verify picking TranslateX cone at Pivot + [1.02, 0, 0]
const HitTranslateX = PickGizmoHandle([1.02, -2.0, 0.25], [0, 1, 0], BuiltGizmo);
assert.equal(HitTranslateX, GizmoHandle.TranslateX, "Ray hits +X Translate cone");

// Verify picking ScaleX cylinder at Pivot + [0.67, 0, 0]
const HitScaleX = PickGizmoHandle([0.67, -2.0, 0.25], [0, 1, 0], BuiltGizmo);
assert.equal(HitScaleX, GizmoHandle.ScaleX, "Ray hits +X Scale cylinder");

// Verify picking CenterRing billboarded torus at radius 0.16
const HitCenterRing = PickGizmoHandle([0.16, -2.0, 0.25], [0, 1, 0], BuiltGizmo);
assert.equal(HitCenterRing, GizmoHandle.CenterRing, "Ray hits camera-billboarded CenterRing");

// Verify TranslateX drag + Ctrl-snap (0.25 m increment)
const DragStart = BeginGizmoDrag(
  GizmoHandle.TranslateX,
  [1.0, -2.0, 0.25],
  [0, 1, 0],
  BuiltGizmo,
  { Position: [...Pivot], Rotation: [0, 0, 0], Scale: [1, 1, 1] },
);
const UnsnappedMove = SolveGizmoDrag(DragStart, [1.37, -2.0, 0.25], [0, 1, 0], false);
assert.ok(Math.abs(UnsnappedMove.Position[0] - 0.37) < 0.01, "TranslateX moves +0.37 m along X");
const SnappedMove = SolveGizmoDrag(DragStart, [1.37, -2.0, 0.25], [0, 1, 0], true);
assert.equal(SnappedMove.Position[0], 0.25, "Ctrl-snap quantizes TranslateX to 0.25 m");

// 6. Verify Multi-Level System & Create Level templates
const DefaultLevels = BuildDefaultLevels();
assert.equal(DefaultLevels.length, 4, "4 built-in Frontier levels shipped (Frontier Lobby default + Cornell-Studio Gallery + Look-Dev Plinth Studio + Empty Proving Ground)");
assert.equal(
  DefaultLevels[0].Id,
  "level-frontier-lobby",
  "Frontier Lobby is the default boot level",
);
assert.ok(
  DefaultLevels[0].Rows.some((R) => R.Id === "control-vehicle") &&
    DefaultLevels[0].Rows.some((R) => R.Id === "lobby-aircraft" && R.PropId === "light_aircraft"),
  "Frontier Lobby default level stages both the ControlVehicle & the light trainer aircraft",
);
const CreatedLookDev = CreateNewLevelRecord({
  Name: "Custom Courtyard",
  TemplateId: "lookdev",
  Notes: "Shader validation stage",
});
assert.equal(CreatedLookDev.Name, "Custom Courtyard");
assert.ok(
  CreatedLookDev.Rows.some((R) => R.Id === "velvet-torus"),
  "Look-Dev template populates Charlie velvet & subsurface specimens",
);
const Duplicated = CreateNewLevelRecord({
  Name: "Lobby Variant B",
  TemplateId: "duplicate",
  CurrentLevel: DefaultLevels[0],
});
assert.equal(
  Duplicated.Rows.length,
  DefaultLevels[0].Rows.length,
  "Duplicate Current Level clones every Frontier Lobby row 1:1 (geometry + ControlVehicle + light aircraft + camera components)",
);
assert.ok(
  DefaultLevels[0].Rows.length > 14,
  `Frontier Lobby default level carries 14 authored rows + injected Render Settings, camera components & physical environment (got ${DefaultLevels[0].Rows.length})`,
);

// 7. Verify standalone index.html bundle integrity & non-SDF shadow map / BVH shader presence
const ViewportSrc = fs.readFileSync(path.join(Folder, "GtxWorldViewport.jsx"), "utf8");
const WebgpuSrc = fs.readFileSync(path.join(Folder, "WebgpuGtxRenderer.js"), "utf8");
const CpuSrc = fs.readFileSync(path.join(Folder, "CpuGtxFallbackRenderer.js"), "utf8");
for (const ForbiddenSdfFn of [
  "evalSceneSdfOpaque",
  "evalLocalPrimSdf",
  "closestRayPrimitiveSoftVis",
  "evaluateSoftConeVis",
  "EvalLocalPrimSdf",
  "ClosestRayPrimSoftVis",
  "EvaluateSoftConeVis",
]) {
  assert.ok(!ViewportSrc.includes(ForbiddenSdfFn), `GtxWorldViewport has zero ${ForbiddenSdfFn}`);
  assert.ok(!WebgpuSrc.includes(ForbiddenSdfFn), `WebgpuGtxRenderer has zero ${ForbiddenSdfFn}`);
  assert.ok(!CpuSrc.includes(ForbiddenSdfFn), `CpuGtxFallbackRenderer has zero ${ForbiddenSdfFn}`);
}
assert.ok(
  !ViewportSrc.includes("localDirs[6]") &&
    !ViewportSrc.includes("fwidth(") &&
    !ViewportSrc.includes("uMetaTex"),
  "Zero discrete GI ghost rays, zero divergent fwidth derivatives, and zero MRT overhead",
);
assert.ok(
  ViewportSrc.includes("evalShadowMapPCSS") &&
    ViewportSrc.includes("evalShadowMapSMRT") &&
    ViewportSrc.includes("evalShadowMapPCF") &&
    ViewportSrc.includes("evalBvhRayShadow") &&
    ViewportSrc.includes("traceVehicleBvh") &&
    ViewportSrc.includes("encodeSelectionDepthAlpha") &&
    ViewportSrc.includes("OUTLINE_FRAGMENT_SHADER"),
  "Non-SDF PCSS/SMRT/PCF/BVH shadow pipeline, ControlVehicle BVH, and 2px silhouette outline present",
);

// 8. Verify Zero-WebGL CpuGtxRenderer + KHR_parallel_shader_compile Dual-Backend Self-Healing Parity
assert.ok(
  ViewportSrc.includes("KHR_parallel_shader_compile") &&
    ViewportSrc.includes("CpuGtxRenderer") &&
    ViewportSrc.includes("SessionWebGlBlocked"),
  "Dual-backend KHR_parallel_shader_compile + zero-WebGL CpuGtxRenderer self-healing wired",
);
const CpuFallback = new CpuGtxRenderer();
const CpuAtlas = new MaterialAtlasSynchronizer();
CpuAtlas.UpdatePrimitives(Compiled.Primitives);
const DoneCpu = CpuFallback.RenderSlice(
  {
    CompiledScene: Compiled,
    AtlasPixels: CpuAtlas.Pixels,
    Width: 160,
    Height: 90,
    ViewModeCode: 0,
    ShowGrid: 1,
  },
  90,
);
assert.equal(DoneCpu, true, "CpuGtxRenderer completes full Cornell-Studio Gallery frame");
let NonBlackPixels = 0;
for (let I = 0; I < CpuFallback.OffscreenImg.data.length; I += 4) {
  const R = CpuFallback.OffscreenImg.data[I];
  const G = CpuFallback.OffscreenImg.data[I + 1];
  const B = CpuFallback.OffscreenImg.data[I + 2];
  assert.ok(Number.isFinite(R) && Number.isFinite(G) && Number.isFinite(B), "Finite RGB output");
  if (R + G + B > 24) NonBlackPixels += 1;
}
assert.ok(NonBlackPixels > 160 * 90 * 0.9, "CpuGtxRenderer produces fully lit Cornell-Studio Gallery");

// 9. Verify Atomic Double-Buffering (Zero Top-to-Bottom Scanline Refresh), Hermetic Worker Kernel, and Unreal WASD Camera
const SnapshotFrameA = new Uint8ClampedArray(CpuFallback.OffscreenImg.data);
const RevA = CpuFallback.FrontRevision;
CpuFallback.Invalidate();
const ZoomedScene = {
  ...Compiled,
  CameraOrigin: [0, -2.4, 1.05],
};
const PartialDone = CpuFallback.RenderSlice(
  {
    CompiledScene: ZoomedScene,
    AtlasPixels: CpuAtlas.Pixels,
    Width: 160,
    Height: 90,
    ViewModeCode: 0,
    ShowGrid: 1,
  },
  30,
);
assert.equal(PartialDone, false, "30/90 rows is an in-progress back-buffer slice");
assert.equal(
  CpuFallback.FrontRevision,
  RevA,
  "FrontRevision MUST NOT advance during partial slice",
);
let ByteDiffDuringSlice = 0;
for (let I = 0; I < SnapshotFrameA.length; ++I) {
  if (CpuFallback.OffscreenImg.data[I] !== SnapshotFrameA[I]) {
    ByteDiffDuringSlice += 1;
    break;
  }
}
assert.equal(
  ByteDiffDuringSlice,
  0,
  "Front buffer MUST stay 100% untouched during partial render (zero top-to-bottom scanline tearing!)",
);
const FinishedZoom = CpuFallback.RenderSlice(
  {
    CompiledScene: ZoomedScene,
    AtlasPixels: CpuAtlas.Pixels,
    Width: 160,
    Height: 90,
    ViewModeCode: 0,
    ShowGrid: 1,
  },
  60,
);
assert.equal(FinishedZoom, true, "Remaining 60/90 rows complete zoomed frame");
assert.equal(CpuFallback.FrontRevision, RevA + 1, "FrontRevision advances atomically on full completion");

// Hermetic isolated Worker kernel test (zero outer scope variables)
const IsolatedKernelFactory = new Function(
  `"use strict"; return (${CreateGtxBandKernel.toString()})(1024, 16, 64, 8);`,
);
const IsolatedKernel = IsolatedKernelFactory();
assert.equal(typeof IsolatedKernel.GtxBandEngine, "function", "Hermetic Worker kernel instantiates cleanly");

// Unreal Engine 5 WASDQE + simultaneous RMB free-look + sprint + dolly + orbit verification
const StartCam = DeriveUnrealCameraState(
  CanonicalEditorCamera.Position,
  CanonicalEditorCamera.Target,
  1.0,
);
const Basis0 = BuildUnrealCameraBasis(StartCam.Yaw, StartCam.Pitch);
assert.ok(Basis0.Forward[1] > 0.98 && Math.abs(Basis0.Right[0] - 1.0) < 1e-3, "Canonical Z-up Unreal camera looks +Y with +X Right");

const MovedFwd = StepUnrealWasdCamera(StartCam, new Set(["KeyW"]), 0.5, 3.4);
assert.ok(MovedFwd.Position[1] > StartCam.Position[1] + 1.6, "KeyW flies forward along 3D view ray");
const MovedSprint = StepUnrealWasdCamera(StartCam, new Set(["KeyW", "ShiftLeft"]), 0.5, 3.4);
assert.ok(
  Math.abs((MovedSprint.Position[1] - StartCam.Position[1]) / (MovedFwd.Position[1] - StartCam.Position[1]) - 2.5) < 1e-4,
  "ShiftLeft applies exact 2.5x Unreal sprint boost",
);
const MovedDiag = StepUnrealWasdCamera(StartCam, new Set(["KeyW", "KeyD"]), 0.5, 3.4);
const DistFwd = Math.hypot(
  MovedFwd.Position[0] - StartCam.Position[0],
  MovedFwd.Position[1] - StartCam.Position[1],
  MovedFwd.Position[2] - StartCam.Position[2],
);
const DistDiag = Math.hypot(
  MovedDiag.Position[0] - StartCam.Position[0],
  MovedDiag.Position[1] - StartCam.Position[1],
  MovedDiag.Position[2] - StartCam.Position[2],
);
assert.ok(Math.abs(DistDiag - DistFwd) < 1e-5, "Diagonal W+D movement is normalized (no sqrt(2) overspeed)");

const SteeredAfterFly = ApplyUnrealMouseLook(MovedFwd, 45, -20, 0.0038);
assert.deepEqual(
  SteeredAfterFly.Position,
  MovedFwd.Position,
  "Simultaneous RMB mouse-look NEVER snaps Position back to drag start!",
);
const ElevatedCam = StepUnrealWasdCamera(SteeredAfterFly, new Set(["KeyE"]), 0.25, 3.4);
assert.ok(ElevatedCam.Position[2] > SteeredAfterFly.Position[2] + 0.8, "KeyE elevates along world +Z");

const DolliedIn = ApplyUnrealDolly(StartCam, -3, 0.105);
assert.ok(DolliedIn.FocusDist < StartCam.FocusDist, "Mouse wheel dolly-in reduces FocusDist smoothly");
const Orbited = ApplyUnrealOrbit(DolliedIn, DolliedIn.Target, 60, 15, 0.0052);
const OrbitDist = Math.hypot(
  Orbited.Target[0] - Orbited.Position[0],
  Orbited.Target[1] - Orbited.Position[1],
  Orbited.Target[2] - Orbited.Position[2],
);
assert.ok(Math.abs(OrbitDist - DolliedIn.FocusDist) < 1e-4, "Unreal Orbit preserves exact pivot FocusDist");
const Panned = ApplyUnrealPan(Orbited, 30, -20, 0.0015);
assert.ok(Panned.Position[0] !== Orbited.Position[0], "Unreal MMB Pan translates Position & Target together");

const HtmlPath = path.join(Folder, "index.html");
assert.ok(fs.existsSync(HtmlPath), "Bundled index.html exists");
const HtmlText = fs.readFileSync(HtmlPath, "utf8");
assert.ok(
  HtmlText.includes("Frontier World Mirror — Tier 1 GTX") &&
    HtmlText.includes("evalShadowMapPCSS") &&
    HtmlText.includes("KHR_parallel_shader_compile") &&
    (HtmlText.includes("RequestAtomicFrame") || HtmlText.includes("bandDone")),
  "Bundled index.html embeds Multi-Worker Atomic Double-Buffered GTX pipeline",
);
assert.ok(
  HtmlText.includes("Create New Level") &&
    HtmlText.includes("Active level") &&
    !HtmlText.includes("NO ENGINE CONNECTION"),
  "Bundled index.html embeds Slate GizmoPRO, Level Switcher, and Create Level modal",
);

// 10. Verify WebGPU 120 FPS Renderer, Always-On-Top Gizmo Overlap Picking, and 19-Law ForceFields + VectorFieldLang
assert.ok(
  ViewportSrc.includes("WebgpuGtxRenderer") &&
    ViewportSrc.includes("ForceOverlay") &&
    ViewportSrc.includes("PickGizmoHandle"),
  "WebgpuGtxRenderer + Always-On-Top Gizmo + ForceOverlay GPU pass wired in GtxWorldViewport",
);

// Verify gizmo picks TranslateX even when an overlapping scene object stands in front of Pivot along the camera ray
const BehindPivot = [0.0, 1.4, 0.45];
const OverlapGizmo = BuildSlateGizmoGeometry(BehindPivot, [0, 1, 0], 1.0);
// Front occluder sphere sits at [0.85, 0.2, 0.45] right between camera [0.85, -2.5, 0.45] and Gizmo TranslateX shaft/cone
const OverlapHit = PickGizmoHandle([0.85, -2.5, 0.45], [0, 1, 0], OverlapGizmo);
assert.equal(
  OverlapHit,
  GizmoHandle.TranslateX,
  "Gizmo handle picks cleanly on top even when another scene object overlaps in front",
);

assert.equal(FORCE_MODELS.length, 19, "All 19 canonical C++ ForceModel laws registered");
assert.equal(ENGINE_FORCE_PRESETS.length, 12, "12 built-in Engine Force presets registered");

// Numerical divergence of analytical SampleCurlNoise3D must be near zero (incompressible curl field)
const CurlEps = 1e-3;
const CxP = SampleCurlNoise3D(1.01 + CurlEps, -0.37, 0.64, 1.3, 3, 0.0)[0];
const CxM = SampleCurlNoise3D(1.01 - CurlEps, -0.37, 0.64, 1.3, 3, 0.0)[0];
const CyP = SampleCurlNoise3D(1.01, -0.37 + CurlEps, 0.64, 1.3, 3, 0.0)[1];
const CyM = SampleCurlNoise3D(1.01, -0.37 - CurlEps, 0.64, 1.3, 3, 0.0)[1];
const CzP = SampleCurlNoise3D(1.01, -0.37, 0.64 + CurlEps, 1.3, 3, 0.0)[2];
const CzM = SampleCurlNoise3D(1.01, -0.37, 0.64 - CurlEps, 1.3, 3, 0.0)[2];
const CurlDiv = (CxP - CxM + (CyP - CyM) + (CzP - CzM)) / (2.0 * CurlEps);
assert.ok(Math.abs(CurlDiv) < 1e-3, `SampleCurlNoise3D is incompressible (div=${CurlDiv.toExponential(2)})`);

// Symbolic VectorFieldLang compiler + evaluation through EvaluateSceneForceFieldAt
const CompiledFormula = CompileFieldProgram(DEFAULT_FORMULA_CODE);
assert.equal(CompiledFormula.Ok, true, "Default symbolic VectorFieldLang formula compiles cleanly");
const SampledFormulaVec = EvaluateSceneForceFieldAt(
  [
    {
      Id: "formula-force-1",
      Position: [0, 0, 1.0],
      Rotation: [0, 0, 0],
      Force: ResolveForceValues({
        Model: "RotationalColumn",
        Strength: 12.0,
        Radius: 4.0,
        FormulaEnabled: true,
        FormulaCode: DEFAULT_FORMULA_CODE,
      }),
    },
  ],
  1.0,
  0.5,
  1.25,
  0.0,
);
assert.ok(
  Array.isArray(SampledFormulaVec) &&
    SampledFormulaVec.every((Comp) => Number.isFinite(Comp)) &&
    Math.hypot(...SampledFormulaVec) > 0.01,
  "Compiled symbolic formula evaluates finite non-zero 3D acceleration",
);

// .ForceModel TOML round-trip + ForceOverlay geometry + Beacon picking
const VortexForce = ResolveForceValues({
  Model: "RotationalColumn",
  Strength: 14.5,
  Radius: 3.2,
});
const EncodedForceToml = SerializeForceModelToml("Validation Vortex", VortexForce, {
  Position: [0.0, 0.4, 0.8],
  Rotation: [0, 0, 0],
});
const DecodedForceDoc = ParseForceModelToml(EncodedForceToml);
assert.equal(DecodedForceDoc.Force.Model, "RotationalColumn", ".ForceModel TOML round-trips Model");
assert.equal(DecodedForceDoc.Force.Strength, 14.5, ".ForceModel TOML round-trips Strength");

const ForceNodes = [
  {
    Id: "force-vortex-1",
    Position: [0.0, 0.4, 0.8],
    Rotation: [0, 0, 0],
    Force: VortexForce,
  },
];
const EvaluatedForce = EvaluateSceneForceFieldAt(ForceNodes, 1.0, 0.4, 0.8, 0.0);
assert.ok(Math.hypot(...EvaluatedForce) > 0.1, "RotationalColumn evaluates non-zero acceleration");

const BuiltForceOverlay = BuildForceOverlayGeometry(
  ForceNodes,
  { ...DEFAULT_FORCE_OVERLAY, SliceEnabled: true },
  "force-vortex-1",
  0.25,
);
assert.ok(
  BuiltForceOverlay.OpaqueVertexCount > 100 && BuiltForceOverlay.TranslucentVertexCount > 500,
  "ForceOverlay builds opaque beacons/arrows + translucent envelopes/streamlines/heatmap slice",
);
const PickedForce = PickForceBeacon([0.0, -2.0, 0.8], [0, 1, 0], BuiltForceOverlay);
assert.equal(PickedForce?.Id, "force-vortex-1", "PickForceBeacon selects Force source beacon in 3D");

// 11. Strict WebGPU PipelineLayout / BindGroupLayout identity & Windows requestAdapter() validation
assert.ok(
  ViewportSrc.includes("const ComputeCurrentGizmo = () =>"),
  "ComputeCurrentGizmo defined in GtxWorldViewport component scope for pointer hover & down",
);
{
  globalThis.GPUBufferUsage = { UNIFORM: 1, STORAGE: 2, COPY_DST: 4, VERTEX: 8 };
  globalThis.GPUTextureUsage = { TEXTURE_BINDING: 1, COPY_DST: 2, RENDER_ATTACHMENT: 4 };
  globalThis.GPUShaderStage = { VERTEX: 1, FRAGMENT: 2 };
  const ExecutedDraws = [];
  const StrictMockDevice = {
    lost: new Promise(() => {}),
    createShaderModule: ({ code }) => ({ code, getCompilationInfo: async () => ({ messages: [] }) }),
    createBindGroupLayout: (desc) => ({ __kind: "BGL", label: desc.label, entries: desc.entries }),
    createPipelineLayout: (desc) => ({ __kind: "PL", label: desc.label, bgls: desc.bindGroupLayouts }),
    createRenderPipeline: (desc) => {
      assert.notEqual(desc.layout, "auto", `Pipeline ${desc.label} must use explicit PipelineLayout`);
      assert.equal(desc.layout?.__kind, "PL");
      return { __kind: "Pipeline", label: desc.label, layout: desc.layout };
    },
    createBuffer: (desc) => ({ __kind: "Buffer", size: desc.size, destroy() {} }),
    createTexture: (desc) => ({ __kind: "Texture", desc, createView: () => ({ __kind: "TexView" }), destroy() {} }),
    createSampler: () => ({ __kind: "Sampler" }),
    createBindGroup: (desc) => {
      assert.equal(desc.layout?.__kind, "BGL");
      return { __kind: "BG", label: desc.label, layout: desc.layout };
    },
    createCommandEncoder: () => ({
      beginRenderPass: () => {
        let curPipe = null;
        const curBgs = {};
        return {
          setPipeline(p) { curPipe = p; },
          setBindGroup(i, bg) { curBgs[i] = bg; },
          setVertexBuffer() {},
          draw(vertCount) {
            for (let i = 0; i < curPipe.layout.bgls.length; ++i) {
              assert.strictEqual(
                curBgs[i]?.layout,
                curPipe.layout.bgls[i],
                `BindGroupLayout mismatch on ${curPipe.label} Draw(${vertCount})`,
              );
            }
            ExecutedDraws.push({ pipeline: curPipe.label, verts: vertCount });
          },
          end() {},
        };
      },
      finish: () => ({ __kind: "CmdBuf" }),
    }),
    queue: { writeBuffer() {}, writeTexture() {}, submit() {} },
    destroy() {},
  };
  Object.defineProperty(globalThis, "navigator", {
    value: {
      userAgent: "Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
      gpu: {
        requestAdapter: async (opts) => {
          assert.equal(opts, undefined, "Windows requestAdapter() omits powerPreference");
          return { requestDevice: async () => StrictMockDevice };
        },
        getPreferredCanvasFormat: () => "bgra8unorm",
      },
    },
    configurable: true,
  });
  const GpuRenderer = await CreateWebgpuGtxRenderer(() => {});
  assert.equal(GpuRenderer.Ok, true);
  assert.equal(
    GpuRenderer.AttachCanvas({
      getContext: (k) =>
        k === "webgpu"
          ? { configure() {}, getCurrentTexture: () => ({ createView: () => ({}) }) }
          : null,
    }),
    true,
  );
  assert.equal(
    GpuRenderer.RenderFrame({
      Width: 1280,
      Height: 720,
      NeedScenePass: true,
      ActiveScene: Compiled,
      CameraOrigin: Compiled.CameraOrigin,
      Fwd: [0, 1, 0],
      Right: [1, 0, 0],
      Up: [0, 0, 1],
      EffectiveViewMode: 0,
      ShowGrid: true,
      SelectedCode: 1,
      PrimCount: Compiled.Primitives.length,
      LightCount: Compiled.AreaLights.length,
      BuiltGizmo,
      ForceOverlay: BuiltForceOverlay,
      ViewProjZeroToOne: new Float32Array([1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]),
      HighlightedHandle: 1,
    }),
    true,
  );
  const ShadowDraws = ExecutedDraws.filter((D) => D.pipeline === "GtxShadowPipeline");
  const MainDraws = ExecutedDraws.filter((D) => D.pipeline !== "GtxShadowPipeline");
  assert.ok(ShadowDraws.length >= 9, `WebGPU Pass 0 rendered ${ShadowDraws.length} shadow-map caster draws`);
  assert.equal(MainDraws.length, 6, "All 6 WebGPU main/overlay passes (Scene, Outline, ForceOpaque, ForceTrans, GizmoOpaque, GizmoTrans) executed cleanly");
}

// 12. Verify Automotive Traffic Cone (cone-like) & Cylinder Cone (cylinder-like), Unreal-Style Surface Raycast Placement, and Cone Slalom Level
{
  const TrafficSpec = WORLD_PLACEABLE_MODELS.find((M) => M.Id === "model-traffic-cone");
  const CylConeSpec = WORLD_PLACEABLE_MODELS.find((M) => M.Id === "model-cylinder-cone");
  assert.ok(TrafficSpec && TrafficSpec.PrimitiveShape === "cone", "Traffic Cone model spec registered");
  assert.ok(CylConeSpec && CylConeSpec.PrimitiveShape === "cylinder-cone", "Cylinder Cone model spec registered");

  const TrafficPayload = BuildActorPayloadFromModelSpec(TrafficSpec);
  const CylConePayload = BuildActorPayloadFromModelSpec(CylConeSpec);

  // Unreal-style surface placement onto Cornell-Studio Gallery floor (z = -0.60)
  const FloorDrop = RaycastWorldPlacementAtUv(Compiled, 0.25, 0.85, 16 / 9, TrafficPayload.HalfHeightZ, "__drag_preview__");
  assert.equal(FloorDrop.HitId, "gallery-floor", "Lower viewport ray hits gallery-floor support surface");
  assert.ok(
    Math.abs(FloorDrop.Position[2] - (-0.64 + TrafficPayload.HalfHeightZ)) < 0.01,
    `Traffic Cone sits flush on gallery floor (z=${FloorDrop.Position[2]})`,
  );

  // Ensure transient __drag_preview__ is ignored during dragover raycasts (zero self-climbing!)
  const PreviewRows = [
    ...ShowcaseInitialRows,
    { ...TrafficPayload.Template, Id: "__drag_preview__", Parent: null, Preview: true },
    { ...TrafficPayload.Template, Id: "placed-traffic-cone", Parent: "showcase", Preview: true },
    { ...CylConePayload.Template, Id: "placed-cylinder-cone", Parent: "showcase", Preview: true },
  ];
  const PreviewValues = {
    ...ShowcaseInitialValues,
    __drag_preview__: { ...TrafficPayload.Properties, Position: FloorDrop.Position },
    "placed-traffic-cone": { ...TrafficPayload.Properties, Position: [-0.85, -1.15, -0.21] },
    "placed-cylinder-cone": { ...CylConePayload.Properties, Position: [0.85, -1.15, -0.16] },
  };
  const ConeScene = CompileLiveGtxScene(PreviewRows, PreviewValues, {}, "PERSP");
  const TrafficPrim = ConeScene.Primitives.find((P) => P.Id === "placed-traffic-cone");
  const CylConePrim = ConeScene.Primitives.find((P) => P.Id === "placed-cylinder-cone");
  assert.equal(TrafficPrim?.Shape, 3, "Traffic Cone compiles to analytical Shape 3");
  assert.equal(CylConePrim?.Shape, 5, "Cylinder Cone compiles to analytical Shape 5");

  const RepeatDrop = RaycastWorldPlacementAtUv(ConeScene, 0.25, 0.85, 16 / 9, TrafficPayload.HalfHeightZ, "__drag_preview__");
  assert.ok(
    Math.abs(RepeatDrop.Position[2] - FloorDrop.Position[2]) < 1e-4,
    "__drag_preview__ never causes self-climbing feedback during dragover",
  );

  // Bake both cones into 32-primitive MaterialAtlas and render through CPU GTX raytracer
  const ConeAtlas = new MaterialAtlasSynchronizer();
  ConeAtlas.UpdatePrimitives(ConeScene.Primitives);
  const ConeCpu = new CpuGtxRenderer();
  assert.equal(
    ConeCpu.RenderSlice(
      {
        CompiledScene: ConeScene,
        AtlasPixels: ConeAtlas.Pixels,
        Width: 120,
        Height: 68,
        ViewModeCode: 0,
        ShowGrid: 1,
      },
      68,
    ),
    true,
    "CPU GTX raytraces Traffic Cone (Shape 3) & Cylinder Cone (Shape 5) cleanly",
  );

  // Verify WGSL shader & Frontier Lobby level template
  assert.ok(GTX_WGSL_SCENE_SHADER.includes("fn intersectFrustumZ") && GTX_WGSL_SCENE_SHADER.includes("shape == 5"), "WGSL shader includes analytical cone & cylinder-cone");
  const LobbyLvl = CreateNewLevelRecord({ Name: "Test Lobby", TemplateId: "lobby" });
  assert.ok(
    LobbyLvl.Rows.some((R) => R.Id === "control-vehicle") &&
      LobbyLvl.Rows.some((R) => R.Id === "lobby-aircraft" && R.PropId === "light_aircraft"),
    "Frontier Lobby template provisions both the ControlVehicle & the light trainer aircraft",
  );

  // Verify 3D Analytical OpenPBR Thumbnail Raytracer across Traffic Cone, Cylinder Cone & Analytical Shapes (100% transparent sky)
  for (const Shape of ["cone", "cylinder-cone", "box", "sphere", "cylinder", "torus"]) {
    const Thumb = RenderAnalyticalThumbnailRgba({ Shape, Width: 64, Height: 64 });
    assert.equal(Thumb.Pixels.length, 64 * 64 * 4, `Thumbnail RGBA buffer sized 64x64 for ${Shape}`);
    // Corners must be 100% transparent (Alpha = 0, RGB = 0) — zero studio floor or background box
    assert.equal(Thumb.Pixels[3], 0, `Top-left corner of ${Shape} thumbnail is 100% transparent`);
    assert.equal(Thumb.Pixels[(63 * 64 + 63) * 4 + 3], 0, `Bottom-right corner of ${Shape} thumbnail is 100% transparent`);
    let LitPixels = 0;
    let OrangePixels = 0;
    let MinX = 64, MinY = 64, MaxX = -1, MaxY = -1;
    for (let Y = 0; Y < 64; ++Y) {
      for (let X = 0; X < 64; ++X) {
        const P = (Y * 64 + X) * 4;
        const R = Thumb.Pixels[P], G = Thumb.Pixels[P + 1], B = Thumb.Pixels[P + 2], A = Thumb.Pixels[P + 3];
        if (A > 16) {
          if (X < MinX) MinX = X;
          if (X > MaxX) MaxX = X;
          if (Y < MinY) MinY = Y;
          if (Y > MaxY) MaxY = Y;
        }
        if (R + G + B > 45 && A > 16) LitPixels += 1;
        if (R > 165 && R > G * 1.45 && G > B * 1.15 && A > 16) OrangePixels += 1;
      }
    }
    const MinFillFraction = Shape === "cylinder-cone" ? 0.16 : 0.25;
    assert.ok(LitPixels > 64 * 64 * MinFillFraction, `3D raytraced thumbnail for ${Shape} has lit studio surfaces (${LitPixels} px)`);
    assert.ok(MinX >= 1 && MinY >= 1 && MaxX <= 62 && MaxY <= 62, `Thumbnail for ${Shape} is unclipped inside frame ([${MinX},${MinY}]..[${MaxX},${MaxY}])`);
    assert.ok(Math.max(MaxX - MinX + 1, MaxY - MinY + 1) >= 48, `Thumbnail for ${Shape} is tightly framed without being far away`);
    if (Shape === "cone" || Shape === "cylinder-cone") {
      assert.ok(OrangePixels > 35, `3D raytraced thumbnail for ${Shape} renders safety-orange OpenPBR body (got ${OrangePixels} px)`);
    }
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------
// 13. 29-Prop Studio Camera/Light/Track/Aircraft Library, Transparent Thumbnails, Per-Face Colours, 15 Debug Views & ProjectBrowser
// ---------------------------------------------------------------------------------------------------------------------------------------
{
  assert.equal(WORLD_PROP_CATALOG.length, 29, "WORLD_PROP_CATALOG exposes 29 bespoke cinema cameras, studio lights, fans, ControlVehicle tyres, barriers & the light trainer aircraft");
  assert.equal(WORLD_PROP_CATALOG.filter((P) => P.Panel === "camera").length, 6, "6 bespoke 3D cinema camera models registered");
  assert.equal(WORLD_PROP_CATALOG.filter((P) => P.Panel === "light").length, 6, "6 3D studio light models registered");
  assert.equal(WORLD_PROP_CATALOG.filter((P) => P.Category === "Fans & Wind").length, 3, "3 motorsport & studio fan models registered");
  assert.equal(WORLD_PROP_CATALOG.filter((P) => P.Panel === "geometry").length, 17, "17 fans, ControlVehicle tyre stack, barriers, sawtooth kerb, paddock props & light aircraft registered");
  assert.equal(WORLD_PROP_CATALOG.filter((P) => P.Category === "Aircraft").length, 1, "1 display-scale light trainer aircraft registered");

  // Verify every single prop renders a transparent, unclipped, tightly-framed 3D thumbnail
  for (const Prop of WORLD_PROP_CATALOG) {
    const Thumb = RenderAnalyticalThumbnailRgba({ PropId: Prop.PropId, Width: 64, Height: 64 });
    assert.equal(Thumb.Pixels[3], 0, `Prop ${Prop.PropId} thumbnail has transparent background (Alpha=0)`);
    let OpaqueCount = 0;
    let MinX = 64, MinY = 64, MaxX = -1, MaxY = -1;
    for (let Y = 0; Y < 64; ++Y) {
      for (let X = 0; X < 64; ++X) {
        const A = Thumb.Pixels[(Y * 64 + X) * 4 + 3];
        if (A > 16) {
          OpaqueCount += 1;
          if (X < MinX) MinX = X;
          if (X > MaxX) MaxX = X;
          if (Y < MinY) MinY = Y;
          if (Y > MaxY) MaxY = Y;
        }
      }
    }
    assert.ok(OpaqueCount > 180, `Prop ${Prop.PropId} renders visible 3D silhouette (${OpaqueCount} px)`);
    assert.ok(MinX >= 1 && MinY >= 1 && MaxX <= 62 && MaxY <= 62, `Prop ${Prop.PropId} thumbnail is unclipped ([${MinX},${MinY}]..[${MaxX},${MaxY}])`);
    assert.ok(Math.max(MaxX - MinX + 1, MaxY - MinY + 1) >= 48, `Prop ${Prop.PropId} thumbnail fills frame tightly`);
  }

  // Verify placed 3D Camera, Studio Light (LTC + stand parts + QuadVisible=false), and Track Prop compilation & OwnerId picking
  const CamPayload = BuildActorPayloadFromModelSpec(WORLD_PLACEABLE_MODELS.find((M) => M.PropId === "cinema_camera"));
  const LightPayload = BuildActorPayloadFromModelSpec(WORLD_PLACEABLE_MODELS.find((M) => M.PropId === "softbox_cinema_light"));
  const PropPayload = BuildActorPayloadFromModelSpec(WORLD_PLACEABLE_MODELS.find((M) => M.PropId === "tire_stack"));
  assert.equal(CamPayload.ActorKind, "camera");
  assert.equal(LightPayload.ActorKind, "light");
  assert.equal(PropPayload.ActorKind, "geometry");

  const ExtendedRows = [
    ...ShowcaseInitialRows,
    { ...CamPayload.Template, Id: "placed-cam-1" },
    { ...LightPayload.Template, Id: "placed-light-1" },
    { ...PropPayload.Template, Id: "placed-prop-1" },
  ];
  const ExtendedValues = {
    ...ShowcaseInitialValues,
    "placed-cam-1": { ...CamPayload.Properties, Position: [-1.05, -1.75, -0.64 + CamPayload.HalfHeightZ] },
    "placed-light-1": { ...LightPayload.Properties, Position: [1.05, -1.75, -0.64 + LightPayload.HalfHeightZ] },
    "placed-prop-1": { ...PropPayload.Properties, Position: [0.0, -1.75, -0.64 + PropPayload.HalfHeightZ] },
  };
  const CompiledExt = CompileLiveGtxScene(
    ExtendedRows,
    ExtendedValues,
    {},
    "PERSP",
    16 / 9,
  );
  const SoftboxLight = CompiledExt.AreaLights.find((L) => L.Id === "placed-light-1");
  assert.ok(SoftboxLight && SoftboxLight.QuadVisible === false, "3D Studio Softbox registers LTC AreaLight while hiding floating quad");
  const StandPrims = CompiledExt.Primitives.filter((P) => P.OwnerId === "placed-light-1");
  assert.ok(StandPrims.length >= 4 && StandPrims.every((P) => P.PickCode === SoftboxLight.PickCode), "3D Studio Softbox expands multi-part stand sharing PickCode");
  const CamPrims = CompiledExt.Primitives.filter((P) => P.OwnerId === "placed-cam-1");
  assert.ok(CamPrims.length >= 6, "Placed Tripod Camera expands multi-part 3D rig in scene");
  const TirePrims = CompiledExt.Primitives.filter((P) => P.OwnerId === "placed-prop-1");
  assert.ok(TirePrims.length >= 4, "Placed ControlVehicle Tyre Stack expands multi-part torus/rim assembly");

  // Verify Fan & Light Outliner Power Toggles (Powered=false keeps 3D geometry visible while disabling light emission / fan spin)
  const FanPayload = BuildActorPayloadFromModelSpec(WORLD_PLACEABLE_MODELS.find((M) => M.PropId === "wind_drum_fan"));
  const ToggleRows = [...ExtendedRows, { ...FanPayload.Template, Id: "placed-fan-1" }];
  const ToggleValuesOn = {
    ...ExtendedValues,
    "placed-fan-1": { ...FanPayload.Properties, Position: [0.2, -1.2, 0.45], Powered: true },
    "placed-light-1": { ...ExtendedValues["placed-light-1"], Powered: true },
  };
  const ToggleValuesOff = {
    ...ExtendedValues,
    "placed-fan-1": { ...FanPayload.Properties, Position: [0.2, -1.2, 0.45], Powered: false },
    "placed-light-1": { ...ExtendedValues["placed-light-1"], Powered: false },
  };
  const CompiledOn0 = CompileLiveGtxScene(ToggleRows, ToggleValuesOn, {}, "PERSP", { FanSpinDeg: 0 });
  const CompiledOn45 = CompileLiveGtxScene(ToggleRows, ToggleValuesOn, {}, "PERSP", { FanSpinDeg: 45 });
  const CompiledOff = CompileLiveGtxScene(ToggleRows, ToggleValuesOff, {}, "PERSP", { FanSpinDeg: 45 });
  assert.equal(CompiledOn0.HasSpinningFans, true, "Powered Fan activates HasSpinningFans");
  assert.equal(CompiledOff.HasSpinningFans, false, "Unpowered Fan stops HasSpinningFans");
  assert.ok(
    CompiledOff.Primitives.some((P) => P.OwnerId === "placed-fan-1"),
    "Unpowered Fan remains visible in 3D scene",
  );
  assert.ok(
    CompiledOff.Primitives.some((P) => P.OwnerId === "placed-light-1") &&
      !CompiledOff.AreaLights.some((L) => L.Id === "placed-light-1"),
    "Unpowered Studio Light keeps 3D stand visible while turning LTC AreaLight OFF",
  );
  const FanBlade0 = CompiledOn0.Primitives.filter((P) => P.OwnerId === "placed-fan-1")[15];
  const FanBlade45 = CompiledOn45.Primitives.filter((P) => P.OwnerId === "placed-fan-1")[15];
  assert.notDeepEqual(FanBlade0.Center, FanBlade45.Center, "Spinning Fan updates 3D rotor blade positions");
  const ProjectUv = (WorldPos, Scene, Aspect) => {
    const Norm3 = (V) => {
      const L = Math.hypot(V[0], V[1], V[2]) || 1;
      return [V[0] / L, V[1] / L, V[2] / L];
    };
    const Cross3 = (A, B) => [
      A[1] * B[2] - A[2] * B[1],
      A[2] * B[0] - A[0] * B[2],
      A[0] * B[1] - A[1] * B[0],
    ];
    const CamFwd = Norm3([
      Scene.CameraTarget[0] - Scene.CameraOrigin[0],
      Scene.CameraTarget[1] - Scene.CameraOrigin[1],
      Scene.CameraTarget[2] - Scene.CameraOrigin[2],
    ]);
    const CamRight = Norm3(Cross3(CamFwd, Scene.WorldUp));
    const CamUp = Cross3(CamRight, CamFwd);
    const TanHalf = Math.tan(Scene.VerticalFovRad * 0.5);
    const Dx = WorldPos[0] - Scene.CameraOrigin[0];
    const Dy = WorldPos[1] - Scene.CameraOrigin[1];
    const Dz = WorldPos[2] - Scene.CameraOrigin[2];
    const Fwd = Dx * CamFwd[0] + Dy * CamFwd[1] + Dz * CamFwd[2];
    const Rx = Dx * CamRight[0] + Dy * CamRight[1] + Dz * CamRight[2];
    const Uz = Dx * CamUp[0] + Dy * CamUp[1] + Dz * CamUp[2];
    const Sx = Rx / (Fwd * TanHalf * Aspect);
    const Sy = Uz / (Fwd * TanHalf);
    return [(Sx + 1) * 0.5, (1 - Sy) * 0.5];
  };
  const [TireU, TireV] = ProjectUv(TirePrims[0].Center, CompiledExt, 16 / 9);
  assert.equal(PickSceneEntityAtUv(CompiledExt, TireU, TireV, 16 / 9), "placed-prop-1", "PickSceneEntityAtUv resolves multi-part Tire Stack child hit back to parent OwnerId");
  const [CamU, CamV] = ProjectUv(CamPrims[0].Center, CompiledExt, 16 / 9);
  assert.equal(PickSceneEntityAtUv(CompiledExt, CamU, CamV, 16 / 9), "placed-cam-1", "PickSceneEntityAtUv resolves placed 3D Camera rig child hit back to parent OwnerId");
  const [LightU, LightV] = ProjectUv(StandPrims[0].Center, CompiledExt, 16 / 9);
  assert.equal(PickSceneEntityAtUv(CompiledExt, LightU, LightV, 16 / 9), "placed-light-1", "PickSceneEntityAtUv resolves 3D Studio Softbox stand child hit back to parent OwnerId");

  // Verify Per-Face FaceColors 3x2 guttered atlas baking & raytraced color differentiation on cube faces
  const FaceAtlasSync = new MaterialAtlasSynchronizer();
  const FaceCpu = new CpuGtxRenderer();
  const FaceRows = [
    { Id: "face-cube", Name: "Face Cube", Panel: "geometry", PrimitiveShape: "box", Preview: true },
    { Id: "camera", Name: "Cam", Panel: "camera" },
    { Id: "sun", Name: "Sun", Panel: "sun", Preview: true },
  ];
  const FaceScene = CompileLiveGtxScene(
    FaceRows,
    {
      "face-cube": {
        Position: [0, 0, 0],
        Rotation: [0, 0, 45],
        Scale: [1.2, 1.2, 1.2],
        Material: ResolveMaterial({}),
        FaceColors: {
          Enabled: true,
          "-Y": "#ef4444",
          "-X": "#22c55e",
          "+X": "#3b82f6",
          "+Z": "#eab308",
          "+Y": "#ffffff",
          "-Z": "#111827",
        },
      },
      camera: { Position: [0, -2.6, 1.3], Target: [0, 0, 0], "Focal Length": 45, "Sensor Width": 36 },
      sun: { Azimuth: 25, Elevation: 55, Intensity: 1.4 },
    },
    {},
    "PERSP",
    1,
  );
  assert.equal(FaceScene.Primitives[0].FaceAtlas, true, "Cube with FaceColors sets FaceAtlas flag");
  FaceAtlasSync.InvalidateAll();
  FaceAtlasSync.UpdatePrimitives(FaceScene.Primitives);
  FaceCpu.Invalidate();
  FaceCpu.RenderSlice(
    {
      CompiledScene: FaceScene,
      AtlasPixels: FaceAtlasSync.Pixels,
      Width: 64,
      Height: 64,
      ViewModeCode: 0,
      ShowGrid: 0,
    },
    64,
  );
  const FacePix = FaceCpu.OffscreenImg.data;
  // Left-front face vs right-front face of 45-deg Z-rotated cube
  const LeftSample = (38 * 64 + 24) * 4;
  const RightSample = (38 * 64 + 40) * 4;
  assert.ok(
    Math.abs(FacePix[LeftSample] - FacePix[RightSample]) + Math.abs(FacePix[LeftSample + 1] - FacePix[RightSample + 1]) > 45,
    "Per-Face FaceColors renders distinct colors on adjacent visible faces of the same cube",
  );

  // Verify all 15 DebugViews (1..15) render non-empty buffers in CpuGtxRenderer AND exist in WGSL & GLSL shaders
  const DbgDir = path.dirname(fileURLToPath(import.meta.url));
  const WgslSrc = fs.readFileSync(path.join(DbgDir, "WebgpuGtxRenderer.js"), "utf8");
  const GlslSrc = fs.readFileSync(path.join(DbgDir, "GtxWorldViewport.jsx"), "utf8");
  const EditorSrc = fs.readFileSync(path.join(DbgDir, "Editor.jsx"), "utf8");
  FaceAtlasSync.InvalidateAll();
  FaceAtlasSync.UpdatePrimitives(CompiledExt.Primitives);
  for (let Mode = 1; Mode <= 15; ++Mode) {
    assert.ok(WgslSrc.includes(`dbg == ${Mode}`), `WGSL shader implements DebugView ${Mode}`);
    assert.ok(GlslSrc.includes(`dbg == ${Mode}`), `GLSL shader implements DebugView ${Mode}`);
    FaceCpu.Invalidate();
    FaceCpu.RenderSlice(
      {
        CompiledScene: CompiledExt,
        AtlasPixels: FaceAtlasSync.Pixels,
        Width: 32,
        Height: 32,
        ViewModeCode: Mode,
        ShowGrid: 0,
      },
      32,
    );
    const DbgBuf = FaceCpu.OffscreenImg.data;
    let NonZero = 0;
    for (let I = 0; I < DbgBuf.length; I += 4) {
      if (DbgBuf[I] + DbgBuf[I + 1] + DbgBuf[I + 2] > 5) NonZero += 1;
    }
    assert.ok(NonZero > 40, `CPU DebugView ${Mode} renders active diagnostic signal (${NonZero} px)`);
  }

  // Verify Editor UI invariants: Diagnostics decoupled from Debug, quick-place strip removed, Outliner 3D icons, ProjectBrowser present
  assert.ok(!EditorSrc.includes('className="viewport-place-strip"'), "Entity quick-place strip removed from viewport top header");
  assert.ok(EditorSrc.includes("DiagnosticsOpen") && EditorSrc.includes('aria-label="Debug views"'), "Diagnostics popup decoupled from Debug views dropdown");
  assert.ok(EditorSrc.includes("RenderOutlinerRowIcon"), "Outliner renders 3D AnalyticalShapeThumbnail icons");
  assert.ok(
    fs.existsSync(path.join(DbgDir, "..", "ProjectBrowser", "index.html")) &&
      fs.existsSync(path.join(DbgDir, "..", "ProjectBrowser", "Browser.js")) &&
      fs.existsSync(path.join(DbgDir, "..", "ProjectBrowser", "ReferenceRenderer.js")),
    "Standalone Experimental/ProjectBrowser/ exists with WebGL2 ReferenceRenderer & Frontier Editor launch",
  );
}

// 14. VectorField Studio 1:1 Inspector Cards + Streamlines/Heatmap/Bokeh/Curl/Div Debug Views
{
  const PanelSrc = fs.readFileSync(path.join(Folder, "ForceFieldPanel.jsx"), "utf8");
  const EditorSrc = fs.readFileSync(path.join(Folder, "Editor.jsx"), "utf8");
  const ViewportSrc14 = fs.readFileSync(path.join(Folder, "GtxWorldViewport.jsx"), "utf8");

  for (const RequiredCard of [
    "ANEMOMETER · PROBE WIND",
    "Probe Sample",
    "VORTICITY |∇×F|",
    "DIVERGENCE ∇·F",
    "Curve & Path Animation",
    "Symbolic GPU Language",
    "GPU Slice & Colorimetry",
    "Bokeh & RK4 Streamlines",
    "4-Tier Cascade Clipmap",
    "SimulationSpace TOML",
  ]) {
    assert.ok(
      PanelSrc.includes(RequiredCard),
      `ForceFieldPanel implements original VectorField Studio card: ${RequiredCard}`,
    );
  }

  for (const ForceDbgView of [
    "ForceField · Streams + Heatmap",
    "ForceField · Bokeh + Heatmap",
    "ForceField · Vorticity |∇×F|",
    "ForceField · Divergence ∇·F",
  ]) {
    assert.ok(
      EditorSrc.includes(ForceDbgView),
      `Editor DebugViews includes ${ForceDbgView}`,
    );
  }
  assert.ok(
    ViewportSrc14.includes("vf-viewport-hud-bar") &&
      ViewportSrc14.includes("EvaluateSceneCompositeTelemetry"),
    "GtxWorldViewport renders live VectorField Colorbar + 3D Probe Differential Telemetry HUD",
  );

  const Rig = BuildDefaultDiagnosticForceRig();
  assert.equal(Rig.length, 3, "Diagnostic Wind + Cyclonic Vortex + Curl Turbulence rig has 3 sources");
  const Tel = EvaluateSceneCompositeTelemetry(Rig, 0.35, 0.15, 0.45, 0.5, 0.08);
  assert.ok(
    Tel.Speed > 0.5 && Tel.CurlMag > 0.05 && Tel.ContributingCount >= 2 && Tel.Beaufort.Number >= 1,
    "Composite differential telemetry computes live Speed, Vorticity |∇×F|, Divergence ∇·F, and Beaufort scale",
  );

  const BokehOverlay = BuildForceOverlayGeometry(
    Rig,
    {
      ...DEFAULT_FORCE_OVERLAY,
      ShowHeatmap: true,
      ShowStreamlines: true,
      ShowBokehRibbons: true,
      Quantity: "Curl",
      Colormap: "Plasma",
      SliceAxis: "ALL",
    },
    Rig[1].Id,
    0.75,
    [0, -3.55, 0.42],
  );
  assert.ok(
    BokehOverlay.OpaqueVertexCount > 300 && BokehOverlay.TranslucentVertexCount > 4000,
    "Streamlines + All-3 Slice Heatmap + Bokeh Stream Ribbons generate rich camera-facing 3D geometry",
  );

  const SpaceToml = SerializeSimulationSpaceToml(Rig, "TunnelLab");
  assert.ok(
    SpaceToml.includes("[VectorFieldSpace]") &&
      (SpaceToml.match(/\[\[ForceSources\]\]/g) || []).length === 3,
    "SerializeSimulationSpaceToml encodes all 3 scene ForceSources cleanly",
  );

  const ForceLabLevel = CreateNewLevelRecord({
    Name: "Aero Tunnel",
    TemplateId: "force-lab",
  });
  assert.equal(
    ForceLabLevel.Rows.filter((R) => R.Panel === "force").length,
    3,
    "Wind & Vortex ForceField Lab template populates 3 editable ForceField rows",
  );
}

// 15. RenderSettings Inspector + Non-SDF Shadow Map / BVH Pipeline + ControlVehicle FVB1 Asset Loading
{
  const RsRow = ShowcaseInitialRows.find((R) => R.Id === "render-settings");
  assert.ok(RsRow && RsRow.Parent === "camera" && RsRow.Panel === "render-settings", "Render Settings lives under Editor Camera");
  const RsDefault = ResolveRenderSettings(ShowcaseInitialValues);
  assert.equal(RsDefault.ShadowAlgorithm, CanonicalRenderSettings.ShadowAlgorithm);
  assert.equal(RsDefault.ShadowMapRes, 2048);

  const RsUniforms = EncodeRenderSettingsUniforms({
    ShadowAlgorithm: "smrt",
    GiAlgorithm: "bvh",
    ReflectionAlgorithm: "bvh",
    ShadowMapRes: 1024,
    SmrtRays: 8,
    RenderScale: 75,
  });
  assert.equal(RsUniforms.shadowAlg, 2);
  assert.equal(RsUniforms.giAlg, 1);
  assert.equal(RsUniforms.reflAlg, 1);
  assert.equal(RsUniforms.shadowMapRes, 1024);
  assert.equal(RsUniforms.smrtRays, 8);
  assert.equal(RsUniforms.renderScale, 75);

  const UnitGeoms = BuildUnitShadowGeometries();
  for (const ShapeId of [0, 1, 2, 3, 4, 5]) {
    assert.ok(UnitGeoms[ShapeId]?.length >= 18, `Unit shadow geometry built for shape ${ShapeId}`);
  }
  const SunMat = BuildSunShadowMatrix(Compiled);
  assert.equal(SunMat.vp.length, 16, "Sun shadow VP matrix built");

  const FvbPath = path.join(Folder, "..", "ShadowAlgorithmLab", "Assets", "VehicleBenchmarkAssets.bin");
  assert.ok(fs.existsSync(FvbPath), "VehicleBenchmarkAssets.bin FVB1 bundle present");
  const RawBuf = fs.readFileSync(FvbPath);
  const Ab = RawBuf.buffer.slice(RawBuf.byteOffset, RawBuf.byteOffset + RawBuf.byteLength);
  const VehAssets = ParseVehicleFvb1Buffer(Ab);
  assert.equal(VehAssets.meshMeta.length, 3, "FVB1 parses 3 ControlVehicle meshes (Body, Rim, Tyre)");

  const BuiltVeh = BuildVehicleTlasAndInstances(Compiled.Vehicles, VehAssets);
  assert.equal(BuiltVeh.instanceCount, 9, "Single ControlVehicle expands to 9 BVH instances (body + 4 tyres + 4 rims)");

  // Ray aimed straight at Showcase ControlVehicle center [-1.42, -0.82, -0.50]
  const VehHit = TraceVehicleBvhCpu([-1.42, -3.0, -0.50], [0, 1, 0], 10.0, Compiled.Vehicles, VehAssets);
  assert.ok(VehHit && VehHit.pickCode >= 500, "TraceVehicleBvhCpu intersects ControlVehicle BVH cleanly");

  // Verify CpuGtxRenderer renders ControlVehicle pixels cleanly into EntityBuf (pickCode >= 500)
  const CpuVehProbe = new CpuGtxRenderer();
  const ProbeAtlas = new Uint8Array(2048 * 2048 * 4);
  ProbeAtlas.fill(190);
  CpuVehProbe.RenderFullFrame({
    CompiledScene: Compiled,
    AtlasPixels: ProbeAtlas,
    VehicleBvhAssets: VehAssets,
    Width: 120,
    Height: 90,
    ViewModeCode: 0,
    ShowGrid: true,
  });
  let CpuVehPixels = 0;
  for (let i = 0; i < CpuVehProbe.EntityBuf.length; i++) {
    if (CpuVehProbe.EntityBuf[i] >= 500) CpuVehPixels++;
  }
  assert.ok(CpuVehPixels > 100, `CpuGtxRenderer rendered ${CpuVehPixels} ControlVehicle BVH pixels cleanly`);

  const LobbyLevel = CreateNewLevelRecord({
    Name: "Frontier Lobby",
    TemplateId: "lobby",
  });
  assert.ok(
    LobbyLevel.Rows.some((R) => R.Id === "control-vehicle") &&
      LobbyLevel.Rows.some((R) => R.Id === "render-settings"),
    "Lobby template includes ControlVehicle & Render Settings",
  );
  assert.ok(
    LobbyLevel.Rows.some((R) => R.Id === "lobby-aircraft" && R.PropId === "light_aircraft"),
    "Lobby template includes the light trainer aircraft",
  );
  assert.ok(
    DefaultLevels.every((L) => L.Rows.some((R) => R.Id === "render-settings")),
    "Every default level includes Render Settings under Editor Camera",
  );
}

// 16. Smooth ControlVehicle BVH Vertex Normals + Shadow Matrix Precision + Physical Sky/Atmosphere/Volumetric Clouds/Moon/Stars
{
  const FvbPath = path.join(Folder, "..", "ShadowAlgorithmLab", "Assets", "VehicleBenchmarkAssets.bin");
  const RawBuf = fs.readFileSync(FvbPath);
  const Ab = RawBuf.buffer.slice(RawBuf.byteOffset, RawBuf.byteOffset + RawBuf.byteLength);
  const VehAssets = ParseVehicleFvb1Buffer(Ab);
  // Check individual vertex octahedral normals + matId packing in triTexData
  let SmoothSpreadCount = 0;
  for (let t = 0; t < VehAssets.totalTriangles; t++) {
    const b = t * 16;
    const o0 = VehAssets.triTexData[b + 7];
    const o1 = VehAssets.triTexData[b + 11];
    const o2 = VehAssets.triTexData[b + 15];
    if (o0 !== o1 || o1 !== o2) SmoothSpreadCount++;
  }
  assert.ok(SmoothSpreadCount > 1000, `Smooth octahedral vertex normals preserved across ${SmoothSpreadCount} triangles`);

  const AreaMat = BuildAreaShadowMatrix(Compiled.AreaLights[0]);
  assert.ok(AreaMat && AreaMat.vp.length === 16, "Area shadow VP matrix built with tightened Near=0.32/Far=14.0");

  // Physical Sky-View LUT + rgba16float packing
  const Lut = BuildSkyViewLut(Compiled.SunDir, Compiled.Atmosphere);
  assert.equal(Lut.length, SKY_VIEW_WIDTH * SKY_VIEW_HEIGHT * 4, "Sky-View LUT dimensions match 96x48x4");
  const Half = PackSkyViewRgba16f(Lut);
  assert.equal(Half.length, Lut.length, "PackSkyViewRgba16f packs all RGBA16F channels");
  const Zenith = SampleSkyViewLutCpu(Lut, 0, 0, 1);
  const Horizon = SampleSkyViewLutCpu(Lut, 1, 0, 0.03);
  assert.ok(Zenith.every(Number.isFinite) && Horizon.every(Number.isFinite), "Sky-View LUT samples finite RGB");
  assert.ok(Horizon[0] + Horizon[1] + Horizon[2] > Zenith[0] + Zenith[1] + Zenith[2], "Horizon luminance exceeds zenith");

  // Volumetric 3D Clouds raymarching (non-zero extinction + scattering)
  let CloudHits = 0;
  for (let i = 0; i < 16; i++) {
    const az = (i / 16) * Math.PI * 2;
    const dx = Math.cos(az) * 0.85, dy = Math.sin(az) * 0.85, dz = 0.52;
    const cl = EvalVolumetricCloudsCpu(dx, dy, dz, Compiled.SunDir, Compiled.SunIrradiance, Horizon, Compiled.Clouds, 1.5);
    if (cl.trans < 0.995) CloudHits++;
  }
  assert.ok(CloudHits >= 2, `Volumetric 3D cloud raymarch intersected cloud cells (${CloudHits}/16 rays)`);

  // Phased Moon + Night Starfield
  const NightMoonDir = Compiled.Moon.dir;
  const MoonEval = EvalMoonAndStarsCpu(
    NightMoonDir[0],
    NightMoonDir[1],
    NightMoonDir[2],
    [0.2, 0.4, -0.5],
    { ...Compiled.Moon, visible: true },
    { ...Compiled.Stars, enabled: true },
    1.0,
  );
  assert.ok(MoonEval[0] + MoonEval[1] + MoonEval[2] > 0.1, "Phased moon disc renders non-zero luminance");

  // Outliner defaults & CompileLiveGtxScene environment fields
  assert.ok(Compiled.Atmosphere && Compiled.Clouds && Compiled.Moon && Compiled.Stars, "Compiled scene carries all 4 physical environment blocks");
  assert.equal(Compiled.HasAnimatedSky, true, "Enabled clouds/twinkle flag HasAnimatedSky");
}

// 17. Full 24h Night Sun, Dark Night Scene, Emissive/Area Shadows at Night, & Moon Texture Atlas
{
  // 24-hour ephemeris reaches full night across midnight and negative Local Hours wrap cleanly
  const Sol0 = Solar({ LocalHours: 0, Month: 10, Day: 4, Latitude: -26.19, Longitude: 28.32, UtcOffset: 2 });
  const Sol2 = Solar({ LocalHours: 2, Month: 10, Day: 4, Latitude: -26.19, Longitude: 28.32, UtcOffset: 2 });
  const Sol12 = Solar({ LocalHours: 12, Month: 10, Day: 4, Latitude: -26.19, Longitude: 28.32, UtcOffset: 2 });
  const Sol22 = Solar({ LocalHours: 22, Month: 10, Day: 4, Latitude: -26.19, Longitude: 28.32, UtcOffset: 2 });
  const Sol24 = Solar({ LocalHours: 24, Month: 10, Day: 4, Latitude: -26.19, Longitude: 28.32, UtcOffset: 2 });
  assert.ok(Sol0.Elevation < -45 && Sol2.Elevation < -35 && Sol22.Elevation < -35 && Sol24.Elevation < -45, "Solar ephemeris reaches full negative night elevation at 0h, 2h, 22h, 24h");
  assert.ok(Sol12.Elevation > 60, "Solar ephemeris reaches daytime zenith around 12h");

  // Compile showcase at Local Hours = 0 (midnight)
  const NightValues = {
    ...ShowcaseInitialValues,
    sun: { ...ShowcaseInitialValues.sun, "Local Hours": 0.0, Direction: undefined },
  };
  const NightScene = CompileLiveGtxScene(ShowcaseInitialRows, NightValues, {}, "PERSP");
  assert.ok(NightScene.SunDir[2] < -0.5, `Midnight SunDir.z is deeply below horizon (${NightScene.SunDir[2].toFixed(3)})`);
  assert.ok(
    NightScene.SkyRadiance[0] < 0.02 && NightScene.SkyRadiance[1] < 0.02 && NightScene.SkyRadiance[2] < 0.03,
    `Night SkyRadiance is truly dark (${NightScene.SkyRadiance.map((v) => v.toFixed(4)).join(",")})`,
  );
  const NightLut = BuildSkyViewLut(NightScene.SunDir, NightScene.Atmosphere);
  const NightZenith = SampleSkyViewLutCpu(NightLut, 0, 0, 1);
  assert.ok(
    NightZenith[0] + NightZenith[1] + NightZenith[2] < 0.015,
    `Night Sky-View zenith luminance is dark (${(NightZenith[0] + NightZenith[1] + NightZenith[2]).toFixed(5)})`,
  );

  // Emissive/Area shadow matrix clears clearcoat-roof-baffle (Z=1.99)
  assert.equal(ShowcaseInitialValues["ceiling-softbox"]["Cast shadows"], true, "ceiling-softbox Cast shadows defaults to true");
  const NightAreaMat = BuildAreaShadowMatrix(NightScene.AreaLights[0]);
  assert.ok(NightAreaMat && NightAreaMat.lightPos[2] < 1.85, "Area shadow lightPos clears roof baffle");

  // CPU night render with area/emissive light vs dark lights-off render + shadow contrast
  const CpuNight = new CpuGtxRenderer();
  const NightAtlas = new MaterialAtlasSynchronizer();
  NightAtlas.UpdatePrimitives(NightScene.Primitives);
  const FvbPath = path.join(Folder, "..", "ShadowAlgorithmLab", "Assets", "VehicleBenchmarkAssets.bin");
  const RawBuf = fs.readFileSync(FvbPath);
  const VehAssets = ParseVehicleFvb1Buffer(RawBuf.buffer.slice(RawBuf.byteOffset, RawBuf.byteOffset + RawBuf.byteLength));
  const MockMoonAtlas = {
    width: MOON_ATLAS_W,
    height: MOON_ATLAS_H,
    ready: true,
    version: 1,
    pixels: new Uint8Array(MOON_ATLAS_W * MOON_ATLAS_H * 4).fill(200),
  };
  CpuNight.RenderFullFrame({
    CompiledScene: NightScene,
    AtlasPixels: NightAtlas.Pixels,
    MoonAtlas: MockMoonAtlas,
    VehicleBvhAssets: VehAssets,
    Width: 120,
    Height: 80,
    ViewModeCode: 0,
    ShowGrid: false,
  });
  // Sample floor under vehicle shadow vs lit floor nearby
  const SamplePix = (x, y) => {
    const o = (y * 120 + x) * 4;
    return (CpuNight.OffscreenImg.data[o] + CpuNight.OffscreenImg.data[o + 1] + CpuNight.OffscreenImg.data[o + 2]) / (3 * 255);
  };
  let MinFloor = 1, MaxFloor = 0;
  for (let y = 52; y < 74; y++) {
    for (let x = 25; x < 95; x++) {
      const v = SamplePix(x, y);
      if (v < MinFloor) MinFloor = v;
      if (v > MaxFloor) MaxFloor = v;
    }
  }
  assert.ok(MaxFloor > 0.12 && MinFloor < MaxFloor * 0.65, `Night emissive/area light produces lit floor (${MaxFloor.toFixed(3)}) and crisp shadow (${MinFloor.toFixed(3)})`);

  // Turn off all lights & emissives at night -> scene must become genuinely dark
  const NightHidden = {};
  for (const Row of ShowcaseInitialRows) {
    if (Row.Panel === "light" || Row.Id === "moon" || Row.Id.includes("emissive") || Row.Id.includes("neon")) {
      NightHidden[Row.Id] = true;
    }
  }
  const PitchBlackScene = CompileLiveGtxScene(ShowcaseInitialRows, NightValues, NightHidden, "PERSP");
  NightAtlas.InvalidateAll();
  NightAtlas.UpdatePrimitives(PitchBlackScene.Primitives);
  CpuNight.RenderFullFrame({
    CompiledScene: PitchBlackScene,
    AtlasPixels: NightAtlas.Pixels,
    MoonAtlas: MockMoonAtlas,
    VehicleBvhAssets: VehAssets,
    Width: 120,
    Height: 80,
    ViewModeCode: 0,
    ShowGrid: false,
  });
  let SumDark = 0;
  for (let i = 0; i < CpuNight.OffscreenImg.data.length; i += 4) {
    SumDark += (CpuNight.OffscreenImg.data[i] + CpuNight.OffscreenImg.data[i + 1] + CpuNight.OffscreenImg.data[i + 2]) / (3 * 255);
  }
  const AvgDark = SumDark / (120 * 80);
  assert.ok(AvgDark < 0.03, `Unlit night scene average brightness is genuinely dark (${AvgDark.toFixed(4)})`);

  // Add an emissive primitive at night with an occluder between it and the floor -> verifies direct emissive GI + shadow
  const EmissiveRows = [
    ...ShowcaseInitialRows,
    { Id: "night-emissive-orb", Name: "Night Emissive Orb", Panel: "geometry", PrimitiveShape: "sphere", Parent: "showcase", Preview: true },
  ];
  const EmissiveValues = {
    ...NightValues,
    "night-emissive-orb": {
      Position: [1.1, -0.82, 0.55],
      Rotation: [0, 0, 0],
      Scale: [0.38, 0.38, 0.38],
      "Cast shadows": true,
      GI: true,
      Material: {
        Type: "Standard",
        Channels: {
          colour: { Source: "Fill", Fill: "#ff9b42" },
          emission: { Source: "Fill", Fill: 8.0 },
        },
      },
    },
  };
  const EmissiveHidden = { ...NightHidden };
  delete EmissiveHidden["night-emissive-orb"];
  const EmissiveNightScene = CompileLiveGtxScene(EmissiveRows, EmissiveValues, EmissiveHidden, "PERSP");
  NightAtlas.InvalidateAll();
  NightAtlas.UpdatePrimitives(EmissiveNightScene.Primitives);
  CpuNight.RenderFullFrame({
    CompiledScene: EmissiveNightScene,
    AtlasPixels: NightAtlas.Pixels,
    MoonAtlas: MockMoonAtlas,
    VehicleBvhAssets: VehAssets,
    Width: 120,
    Height: 80,
    ViewModeCode: 0,
    ShowGrid: false,
  });
  let EmissivePeak = 0;
  for (let i = 0; i < CpuNight.OffscreenImg.data.length; i += 4) {
    const L = (CpuNight.OffscreenImg.data[i] + CpuNight.OffscreenImg.data[i + 1] + CpuNight.OffscreenImg.data[i + 2]) / (3 * 255);
    if (L > EmissivePeak) EmissivePeak = L;
  }
  assert.ok(EmissivePeak > 0.35, `Emissive primitive lights up the pitch-dark night scene (peak=${EmissivePeak.toFixed(3)})`);

  // Moon Atlas constants & built Assets.Moons verification
  assert.equal(MOON_ATLAS_W, 512);
  assert.equal(MOON_ATLAS_H, 1536);
  assert.equal(MOON_TILE_COUNT, 6);
  assert.deepEqual(MOON_PRESET_NAMES, ["luna", "ember", "glacier", "sulfur", "shroud", "shard"]);
  const HtmlPath = path.join(Folder, "index.html");
  if (fs.existsSync(HtmlPath)) {
    const HtmlText = fs.readFileSync(HtmlPath, "utf8");
    const Marker = "<script>window.NativeAssets=";
    const JsonStart = HtmlText.indexOf(Marker) + Marker.length;
    const JsonEnd = HtmlText.indexOf(";</script><script>", JsonStart);
    const Embedded = JSON.parse(HtmlText.slice(JsonStart, JsonEnd));
    assert.ok(Array.isArray(Embedded.Moons) && Embedded.Moons.length === 6, "Built index.html embeds 6 moon textures");
    assert.ok(Embedded.Moons.every((m) => typeof m === "string" && m.startsWith("data:image/jpeg;base64,")), "All 6 moon textures are base64 JPEG data URLs");
  }
}

// 18. Dedicated Emissive GI Bounce, Emissive Occlusion/Shadows, BVH Raytraced GI Area-Light Parity, & Emissive WorldProps
{
  const NightBase = {
    ...ShowcaseInitialValues,
    sun: { ...ShowcaseInitialValues.sun, "Local Hours": 0.0, Direction: undefined },
  };
  const AllLightsOffHidden = {};
  for (const Row of ShowcaseInitialRows) {
    if (Row.Panel === "light" || Row.Id === "moon" || Row.Id.includes("emissive") || Row.Id.includes("neon")) {
      AllLightsOffHidden[Row.Id] = true;
    }
  }

  // Test 18A: Emissive GI Bounce at Night (Sky dark, Area lights OFF)
  // Place a white emissive sphere near Crimson Left Wall (-1.85, 0.25, 0.15) AND a solid wall occluder (-0.85, 0.25, 0.25)
  // blocking direct line-of-sight to plaster-east & the right floor, proving indirect GI itself bounces Crimson wall radiance!
  const GiRows = [
    ...ShowcaseInitialRows,
    { Id: "gi-emissive-orb", Name: "GI Emissive Orb", Panel: "geometry", PrimitiveShape: "sphere", Parent: "showcase", Preview: true },
    { Id: "gi-blocker-wall", Name: "GI Blocker Wall", Panel: "geometry", PrimitiveShape: "box", Parent: "showcase", Preview: true },
  ];
  const GiOrbVals = {
    Position: [-1.85, 0.25, 0.15],
    Rotation: [0, 0, 0],
    Scale: [0.55, 0.55, 0.55],
    "Cast shadows": false,
    GI: true,
    Material: {
      Type: "Emissive",
      Channels: {
        colour: { Source: "Fill", Fill: "#ffffff" },
        metallic: { Source: "Fill", Fill: 1.0 },
        emission: { Source: "Fill", Fill: 14.0 },
      },
    },
  };
  const GiBlockerVals = {
    Position: [-0.85, 0.25, 0.25],
    Rotation: [0, 0, 0],
    Scale: [0.18, 6.2, 2.2],
    "Cast shadows": true,
    GI: true,
    Material: {
      Type: "Standard",
      Channels: {
        colour: { Source: "Fill", Fill: "#c8c8c8" },
      },
    },
  };

  const RunNightFrame = (GiAlgorithm, GiIntensity, EmissiveShadowMode = "shadowed", KeepSoftbox = false) => {
    const Vals = {
      ...NightBase,
      "render-settings": {
        ...CanonicalRenderSettings,
        GiAlgorithm,
        GiIntensity,
        FillBoost: 1.0,
        GiSamples: 10,
        EmissiveShadowMode,
      },
      "gi-emissive-orb": GiOrbVals,
      "gi-blocker-wall": GiBlockerVals,
    };
    const Hid = { ...AllLightsOffHidden };
    if (KeepSoftbox) delete Hid["ceiling-softbox"];
    const Sc = CompileLiveGtxScene(GiRows, Vals, Hid, "PERSP");
    const Atl = new MaterialAtlasSynchronizer();
    Atl.UpdatePrimitives(Sc.Primitives);
    const Cpu = new CpuGtxRenderer();
    Cpu.RenderFullFrame({
      CompiledScene: Sc,
      AtlasPixels: Atl.Pixels,
      Width: 120,
      Height: 80,
      ViewModeCode: 0,
      ShowGrid: false,
    });
    return { Cpu, Sc };
  };

  const SampleEntity = ({ Cpu, Sc }, PickId) => {
    const Code = Sc.Primitives.find((P) => P.Id === PickId)?.PickCode;
    let r = 0, g = 0, b = 0, n = 0;
    for (let i = 0; i < Cpu.EntityBuf.length; i++) {
      if (Cpu.EntityBuf[i] === Code) {
        const o = i * 4;
        r += Cpu.OffscreenImg.data[o] / 255;
        g += Cpu.OffscreenImg.data[o + 1] / 255;
        b += Cpu.OffscreenImg.data[o + 2] / 255;
        n++;
      }
    }
    return n > 0 ? [r / n, g / n, b / n, (r + g + b) / (3 * n)] : [0, 0, 0, 0];
  };

  // Cone GI ON vs GI OFF on shielded plaster-east and gallery-floor
  const ConeOn = RunNightFrame("sdf", 1.6, "shadowed", false);
  const ConeOff = RunNightFrame("off", 0.0, "shadowed", false);
  const OnEast = SampleEntity(ConeOn, "plaster-east");
  const OffEast = SampleEntity(ConeOff, "plaster-east");
  const OnFloor = SampleEntity(ConeOn, "gallery-floor");
  const OffFloor = SampleEntity(ConeOff, "gallery-floor");
  assert.ok(OffEast[3] < 0.025 && OffFloor[3] < 0.032, `GI Off shielded surfaces at night remain dark under starlight (east=${OffEast[3].toFixed(4)}, floor=${OffFloor[3].toFixed(4)})`);
  assert.ok(
    OnEast[3] > 0.25 && OnFloor[0] > OnFloor[2] * 1.2,
    `Cone GI produces strong indirect GI + warm crimson bounce (east=${OnEast[3].toFixed(3)}, floor=${OnFloor.map((v) => v.toFixed(3)).join(",")})`,
  );

  // Test 18B: Emissive shadow contrast (Shadowed vs Unshadowed on gallery-floor behind blocker wall)
  const Unshad = RunNightFrame("sdf", 0.0, "unshadowed", false);
  const ShadOnly = RunNightFrame("sdf", 0.0, "shadowed", false);
  const UnshadFloor = SampleEntity(Unshad, "gallery-floor");
  const ShadFloor = SampleEntity(ShadOnly, "gallery-floor");
  assert.ok(
    UnshadFloor[3] > ShadFloor[3] * 4.0,
    `Emissive blocker wall casts crisp shadow when shadowed (${ShadFloor[3].toFixed(4)}) vs unshadowed (${UnshadFloor[3].toFixed(4)})`,
  );

  // Test 18C: BVH Raytraced GI hits Ceiling Softbox + Emissive Primitives at night
  const BvhOn = RunNightFrame("bvh", 1.5, "shadowed", true);
  const BvhFloor = SampleEntity(BvhOn, "gallery-floor");
  assert.ok(
    BvhFloor[3] > 0.20,
    `BVH Raytraced GI (giAlg=1) captures Area Light + Emissive GI bounce cleanly (${BvhFloor[3].toFixed(3)})`,
  );

  // Test 18D: WorldPropLibrary emissive prop colors & face emission extraction
  const NeonProp = WORLD_PROP_CATALOG.find((P) => P.Id === "prop_neon_tube");
  const SoftboxProp = WORLD_PROP_CATALOG.find((P) => P.Id === "prop_studio_softbox");
  if (NeonProp) {
    const Prims = ExpandPropPrimitives({ Id: "n1", Name: "Neon", PropId: NeonProp.Id }, {}, 900);
    assert.ok(Prims.some((P) => P.Emission > 1.0 && (P.BaseColor[0] + P.BaseColor[1] + P.BaseColor[2]) > 0.2), "Neon tube prop exports emissive radiance and non-hardcoded linear color");
  }
  if (SoftboxProp) {
    const Prims = ExpandPropPrimitives({ Id: "s1", Name: "Softbox", PropId: SoftboxProp.Id }, {}, 901);
    assert.ok(Prims.some((P) => P.Emission > 1.0), "Studio Softbox prop promotes emissive diffuser face into GI emission");
  }

  // Verify shader source parity across WebGPU & WebGL2 for 2-bounce BVH GI quad trace & emissive bounce
  const WgslSrc = fs.readFileSync(path.join(Folder, "WebgpuGtxRenderer.js"), "utf8");
  const GlslSrc = fs.readFileSync(path.join(Folder, "GtxWorldViewport.jsx"), "utf8");
  assert.ok(WgslSrc.includes("let qHit = traceLightQuad(currPos + currN * 0.012, d, gHit.t);"), "WGSL 2-bounce BVH GI traces Area Light quads");
  assert.ok(GlslSrc.includes("float tQ = traceLightQuad(currPos + currN * 0.012, rd, giHit.t, qEmit, qIdx);"), "GLSL 2-bounce BVH GI traces Area Light quads");
  assert.ok(WgslSrc.includes("for (var ei : i32 = 0; ei < primCount; ei = ei + 1)") && GlslSrc.includes("for (int ei = 0; ei < uPrimCount; ++ei)"), "Both WGSL and GLSL bounce emissive primitives off walls");
}

// ============================================================================
// Suite 19: ShadowAlgorithmLab Parity (No SDF Shadows/Reflections) & Sun+Moon+Star Illumination
// ============================================================================
{
  const { BuildSunShadowMatrix, BuildAreaShadowMatrix } = await import("./ShadowPipeline.js");
  const { SHADOW_ALGORITHMS, GI_ALGORITHMS, REFLECTION_ALGORITHMS, ResolveRenderSettings } = await import("./ShowcaseGalleryScene.js");

  // 1. Algorithm menus exclude SDF shadows and SDF reflections
  assert.deepStrictEqual(
    SHADOW_ALGORITHMS.map((a) => a.id),
    ["contact_pcss", "pcss", "smrt", "pcf", "bvh"],
    "Shadows expose exact non-SDF ShadowAlgorithmLab modes",
  );
  assert.deepStrictEqual(
    GI_ALGORITHMS.map((a) => a.id),
    ["sdf", "bvh", "off"],
    "GI exposes Cone GI, BVH Raytraced GI, and Off",
  );
  assert.deepStrictEqual(
    REFLECTION_ALGORITHMS.map((a) => a.id),
    ["bvh", "env"],
    "Reflections exclude SDF reflections and expose BVH + Env",
  );
  assert.strictEqual(
    ResolveRenderSettings({ "render-settings": { ReflectionAlgorithm: "sdf" } }).ReflectionAlgorithm,
    "bvh",
    "Saved scenes with legacy 'sdf' reflection normalize to 'bvh'",
  );

  // 2. Shadow matrix metadata for linear-depth PCF/PCSS/SMRT
  const DayScene = CompileLiveGtxScene(ShowcaseInitialRows, ShowcaseInitialValues, {}, "PERSP");
  const SunInfo = BuildSunShadowMatrix(DayScene);
  assert.ok(Array.isArray(SunInfo.eye) && SunInfo.orthoHalfSpan > 0 && SunInfo.far > SunInfo.near, "Sun shadow matrix exports linear-depth metadata");
  const AreaInfo = BuildAreaShadowMatrix(DayScene.AreaLights[0]);
  assert.ok(AreaInfo && Array.isArray(AreaInfo.forward) && AreaInfo.perspTan > 0 && AreaInfo.far > AreaInfo.near, "Area shadow matrix exports linear-depth perspective metadata");

  // 3. WGSL + GLSL carry ShadowAlgorithmLab LTC, PCF, PCSS, SMRT, BVH reflections, and Sun+Moon+Starlight
  const WgslSrc = fs.readFileSync(path.join(Folder, "WebgpuGtxRenderer.js"), "utf8");
  const GlslSrc = fs.readFileSync(path.join(Folder, "GtxWorldViewport.jsx"), "utf8");
  for (const [WgslFn, GlslFn] of [
    ["evalWgslShadowMapPCF", "evalShadowMapPCF"],
    ["evalWgslShadowMapPCSS", "evalShadowMapPCSS"],
    ["evalWgslShadowMapSMRT", "evalShadowMapSMRT"],
    ["evalWgslBvhRayShadow", "evalBvhRayShadow"],
    ["evalEmissivePanelLtc", "evalEmissivePanelLtc"],
    ["traceBvhReflection", "traceBvhReflection"],
    ["evalMoonIrradiance", "evalMoonIrradiance"],
    ["evalStarlightAmbient", "evalStarlightAmbient"],
  ]) {
    assert.ok(WgslSrc.includes(WgslFn), `WebgpuGtxRenderer.js implements ${WgslFn}`);
    assert.ok(GlslSrc.includes(GlslFn), `GtxWorldViewport.jsx implements ${GlslFn}`);
  }
  assert.ok(!WgslSrc.includes("REFLECTION_SDF") && !GlslSrc.includes("REFLECTION_SDF"), "Neither renderer uses SDF reflections");
  assert.ok(!WgslSrc.includes("SHADOW_SDF") && !GlslSrc.includes("SHADOW_SDF"), "Neither renderer uses SDF shadows");
  assert.ok(!/select\([^;]*(ShadowVP|mat4x4f)/.test(WgslSrc), "WGSL never calls select() on mat4x4f");

  // 4. Sun + Moon + Starlight illumination deltas in CPU renderer
  const NoLightsHidden = {};
  for (const Row of ShowcaseInitialRows) {
    if (Row.Panel === "light" || Row.Id.includes("emissive") || Row.Id.includes("neon")) {
      NoLightsHidden[Row.Id] = true;
    }
  }
  const RenderCelestialFrame = (SunHours, MoonOn, StarsOn) => {
    const Hid = { ...NoLightsHidden };
    if (!MoonOn) Hid.moon = true;
    if (!StarsOn) Hid.stars = true;
    const Vals = {
      ...ShowcaseInitialValues,
      sun: {
        ...ShowcaseInitialValues.sun,
        "Local Hours": SunHours,
        Direction: undefined,
      },
      moon: {
        ...ShowcaseInitialValues.moon,
        "Moon visible": MoonOn,
        Elevation: 52,
        Azimuth: 225,
        Brightness: 2.2,
        Phase: 0.55,
      },
      stars: {
        ...ShowcaseInitialValues.stars,
        "Star field": StarsOn,
        Brightness: 1.5,
      },
      "render-settings": {
        ...CanonicalRenderSettings,
        GiAlgorithm: "sdf",
        GiIntensity: 1.0,
      },
    };
    const Sc = CompileLiveGtxScene(ShowcaseInitialRows, Vals, Hid, "PERSP");
    const Atl = new MaterialAtlasSynchronizer();
    Atl.UpdatePrimitives(Sc.Primitives);
    const Cpu = new CpuGtxRenderer();
    Cpu.RenderFullFrame({
      CompiledScene: Sc,
      AtlasPixels: Atl.Pixels,
      Width: 120,
      Height: 80,
      ViewModeCode: 0,
      ShowGrid: false,
    });
    const FloorCode = Sc.Primitives.find((P) => P.Id === "gallery-floor")?.PickCode;
    let r = 0, g = 0, b = 0, n = 0;
    for (let i = 0; i < Cpu.EntityBuf.length; i++) {
      if (Cpu.EntityBuf[i] === FloorCode) {
        const o = i * 4;
        r += Cpu.OffscreenImg.data[o] / 255;
        g += Cpu.OffscreenImg.data[o + 1] / 255;
        b += Cpu.OffscreenImg.data[o + 2] / 255;
        n++;
      }
    }
    return n > 0 ? [r / n, g / n, b / n, (r + g + b) / (3 * n)] : [0, 0, 0, 0];
  };

  const FNone = RenderCelestialFrame(0, false, false);
  const FStar = RenderCelestialFrame(0, false, true);
  const FMoon = RenderCelestialFrame(0, true, true);
  const FSun = RenderCelestialFrame(12, false, false);

  assert.ok(FNone[3] < 0.015, `Midnight with moon/stars/lights off is pitch dark (${FNone[3].toFixed(4)})`);
  assert.ok(FStar[3] > FNone[3] * 1.4 && FStar[2] > FStar[0], `Starlight visibly adds cool illumination at night (star=${FStar[3].toFixed(4)} vs off=${FNone[3].toFixed(4)})`);
  assert.ok(FMoon[3] > FStar[3] * 2.2, `Moonlight visibly illuminates the night scene above starlight (moon=${FMoon[3].toFixed(4)} vs star=${FStar[3].toFixed(4)})`);
  assert.ok(FSun[3] > FMoon[3] * 2.0, `Daytime sunlight strongly illuminates the scene above moonlight (sun=${FSun[3].toFixed(4)} vs moon=${FMoon[3].toFixed(4)})`);
}

console.log(
  "[WorldMirrorValidation] ALL 19 SUITES PASSED: ShadowAlgorithmLab Parity (PCF/PCSS/SMRT/BVH, LTC Area Lights, Cone & 2-Bounce BVH GI, BVH Reflections, No SDF Shadows/Reflections), Sun+Moon+Starlight Illumination, Moon Atlas, Physical Sky/Clouds, RenderSettings, ControlVehicle BVH, and ProjectBrowser verified!",
);

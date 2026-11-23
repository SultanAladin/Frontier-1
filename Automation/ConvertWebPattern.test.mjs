import test from "node:test";
import assert from "node:assert/strict";
import { convertPatternJson, serializeNativeToml } from "./ConvertWebPattern.mjs";

const document = (layers, extra = {}) => ({
  schema: "alloy.pattern.v1", name: "Import test", layers, ...extra,
});

test("converts normalized primitives and cubic paths into native vector instructions", () => {
  const converted = convertPatternJson(document([
    { kind: "star", name: "Gold thread", x: 128, y: 256, width: 80, height: 120,
      starPoints: 7, innerRadius: 0.38, rotation: 25, color: "#c18a2e", strokeWidth: 2,
      roughness: 0.34, metalness: 0.82, stitchRole: "thread", relief: 0.7, paint: true,
      SurfaceChannels: { Color: { Enabled: false, Strength: 0.35 }, Height: { Enabled: true, Strength: 0.8 },
        Normal: { Enabled: true, Strength: 1.2 }, Roughness: { Enabled: true, Strength: 0.45 },
        Metalness: { Enabled: true, Strength: 0.75 }, Occlusion: { Enabled: true, Strength: 0.4 } } },
    { kind: "path", name: "Curve", x: 300, y: 250, width: 100, height: 100,
      nodes: [{ x: 10, y: 50, out: { x: 30, y: 5 } }, { x: 90, y: 50, in: { x: 70, y: 95 } }],
      color: "#e0e0e0", strokeWidth: 4, opacity: 0.8,
      SurfaceChannels: { Opacity: { Enabled: true, Strength: 0.5 } } },
    { kind: "text", name: "Outlined text", path: "M10 10 L90 10 L50 90 Z", x: 250, y: 100,
      width: 120, height: 80, color: "#ffffff", paint: true },
  ]));
  assert.equal(converted.format, "frontier.vector_artwork");
  assert.equal(converted.layers.length, 4);
  assert.ok(converted.segments.length > 8);
  assert.equal(converted.layers[1].surface_structure, "stitch");
  assert.equal(converted.layers[1].textile_geometry, "stitch_thread");
  assert.equal(converted.layers[1].surface_channels.normal.strength, 1.2);
  assert.equal(converted.layers[1].surface_channels.colour.enabled, false);
  assert.equal(converted.layers[1].surface_channels.colour.strength, 0.35);
  assert.equal(converted.layers[1].surface_channels.roughness.strength, 0.34);
  assert.equal(converted.layers[1].surface_channels.metalness.strength, 0.82);
  assert.equal(converted.layers[1].surface_channels.occlusion.strength, 0.6);
  assert.ok(converted.instructions.some((item) => Math.abs(item.colour[0] - 193 / 255) < 1e-8));
  assert.ok(Math.abs(converted.instructions.find((item) => item.transform[1] === 4).transform[2] - 0.4) < 1e-8);
  assert.ok(converted.segments.length > 4);
  assert.ok(converted.instructions.length >= 2);
  assert.match(serializeNativeToml(converted), /format = "frontier\.vector_artwork"/);
});

test("preserves boolean fill rules and independent fill/stroke colours", () => {
  const converted = convertPatternJson(document([{
    kind: "path", name: "Outlined", path: "M10 10 L90 10 L90 90 Z", closed: true,
    x: 256, y: 256, width: 100, height: 100, fillRule: "evenodd",
    color: "#ffffff", stroke: "#000000", strokeWidth: 3, paint: true, fillEnabled: true,
  }]));
  assert.equal(converted.instructions.length, 4);
  assert.equal(converted.instructions[1].topology[3], 1);
  assert.equal(converted.layers[1].root, 3);
});

test("converts the remaining built-in vector silhouettes and arc closures", () => {
  const kinds = ["rect", "ellipse", "diamond", "triangle", "flower", "polygon", "arc"];
  const converted = convertPatternJson(document(kinds.map((kind) => ({
    kind, name: kind, x: 256, y: 256, width: 80, height: 90, color: "#4488cc",
    paint: true, fillEnabled: true, arcClosure: kind === "arc" ? "pie" : undefined,
  }))));
  assert.equal(converted.layers.length, kinds.length + 1);
  assert.ok(converted.segments.length >= 30);
});

test("wraps edge-crossing geometry into the periodic 512-pixel tile and retains the solid background", () => {
  const converted = convertPatternJson(document([{
    kind: "rect", name: "Seam-crossing tile", x: 4, y: 256, width: 40, height: 40,
    color: "#224466",
  }]));
  assert.equal(converted.layers[0].name, "Pattern background");
  assert.equal(converted.instructions[0].topology[0], 0);
  const Copies = converted.instructions.filter((instruction) => instruction.topology[0] === 6);
  assert.deepEqual(Copies.map((instruction) => instruction.bounds[0]), [4, 516]);
  assert.equal(Copies[0].topology[1], Copies[1].topology[1]);
  assert.equal(converted.layers[1].root, 3);
});

test("concatenates full-bleed copies into one tile-space instruction once the lean layout exceeds the instruction budget", () => {
  const converted = convertPatternJson(document([0, 1, 2].map((index) => ({
    kind: "rect", name: `Full-bleed band ${index}`, x: 256, y: 256, width: 800, height: 800,
    color: ["#224466", "#663322", "#446622"][index],
  }))));
  const contours = converted.instructions.filter((instruction) => instruction.topology[0] === 6);
  assert.equal(contours.length, 3);
  for (const contour of contours) {
    assert.deepEqual(contour.bounds, [256, 256, 512, 512]);
    assert.equal(contour.transform[0], 0);
  }
  assert.ok(converted.instructions.length + converted.layers.length - 1 <= 128);
  const tileX = converted.segments.flatMap((segment) =>
    [segment.start_control[0], segment.start_control[2], segment.control_end[0], segment.control_end[2]]
      .map((coordinate) => coordinate * 512 + 256));
  assert.ok(Math.min(...tileX) < 0);
  assert.ok(Math.max(...tileX) > 512);
});

test("maps supported rectangle and ellipse masks through the layer transform", () => {
  const converted = convertPatternJson(document([{
    kind: "rect", name: "Masked rectangle", x: 256, y: 256, width: 100, height: 50,
    rotation: 90, color: "#ffffff", mask: { category: "ellipse", x: 75, y: 50,
      width: 50, height: 50, opacity: 0.7, invert: true, mode: "luminance" },
  }]));
  const layer = converted.layers[1];
  assert.equal(layer.mask_enabled, true);
  assert.equal(layer.mask_inverted, true);
  assert.equal(layer.mask_luminance, true);
  assert.deepEqual(layer.mask.topology, [1, 0, 0, 0]);
  assert.ok(Math.abs(layer.mask.bounds[0] - 256) < 1e-5);
  assert.ok(Math.abs(layer.mask.bounds[1] - 281) < 1e-5);
  assert.equal(layer.mask.bounds[2], 50);
  assert.equal(layer.mask.bounds[3], 25);
  assert.equal(layer.mask.transform[2], 0.7);
  assert.match(serializeNativeToml(converted), /mask_enabled = true/);
});

test("fails closed rather than silently dropping unsupported Pattern features", () => {
  assert.throws(() => convertPatternJson(document([{ kind: "image", src: "data:image/png;base64,AA==" }])), /not yet supported/);
  assert.throws(() => convertPatternJson(document([{ kind: "star", mask: { category: "ellipse" } }])), /supported only on unstroked rectangle and ellipse/);
  assert.throws(() => convertPatternJson(document([{ kind: "rect", mask: { category: "image", image: "data:image/png;base64,AA==" } }])), /image or painted artwork mask/);
  assert.throws(() => convertPatternJson(document([{ kind: "rect", fillSource: { category: "linear", stops: [{ color: "#ffffff", offset: 0 }, { color: "#000000", offset: 1 }] } }])), /linked or gradient/);
  assert.throws(() => convertPatternJson(document([{ kind: "rect", group: "group-1" }])), /folders, linked instances/);
  assert.throws(() => convertPatternJson(document([{ kind: "rect", stitchRole: "thread",
    SurfaceChannels: { Height: { Enabled: false } } }])), /requires enabled, non-zero Height and Normal/);
  assert.throws(() => convertPatternJson(document([{ kind: "rect",
    SurfaceChannels: { Normal: { Source: "data:image/png;base64,AA==" } } }])), /image normal map/);
  assert.throws(() => convertPatternJson(document(Array.from({ length: 63 }, () => ({
    kind: "rect", mask: { category: "ellipse" },
  })))), /128-instruction limit after layer and mask composition/);
});

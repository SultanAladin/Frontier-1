import { test, expect } from "@playwright/test";
import {
  stampAngleDelta,
  stampCanvasPoint,
  stampTransformDraft,
} from "../src/stampTransform.js";

const layer = {
  id: "artwork",
  x: 256,
  y: 232,
  width: 365,
  height: 144,
  rotation: 0,
};
const start = { x: 356, y: 232 };

test("Stamp viewport coordinates use both canvas dimensions at any zoom", () => {
  expect(
    stampCanvasPoint(
      { clientX: 225, clientY: 400 },
      { left: 100, top: 150, width: 250, height: 500 },
    ),
  ).toEqual({ x: 256, y: 256 });
});

test("G translates from the snapshot, with exact bounds, 8px snapping and X/Y constraints", () => {
  const point = { x: 401, y: 258 };
  expect(stampTransformDraft(layer, "move", start, point)).toEqual({
    id: layer.id,
    x: 301,
    y: 258,
  });
  expect(
    stampTransformDraft(layer, "move", start, point, { snap: true }),
  ).toEqual({
    id: layer.id,
    x: 304,
    y: 256,
  });
  expect(
    stampTransformDraft(layer, "move", start, point, { axis: "x" }),
  ).toEqual({
    id: layer.id,
    x: 301,
    y: 232,
  });
  expect(
    stampTransformDraft(layer, "move", start, point, { axis: "y" }),
  ).toEqual({
    id: layer.id,
    x: 256,
    y: 258,
  });
  expect(
    stampTransformDraft(layer, "move", start, { x: 100000, y: -100000 }),
  ).toEqual({
    id: layer.id,
    x: 1024,
    y: -512,
  });
});

test("R rotates about the artwork pivot, supports accumulated turns and snaps by 15 degrees", () => {
  expect(
    stampTransformDraft(layer, "rotate", start, { x: 256, y: 332 }),
  ).toEqual({
    id: layer.id,
    rotation: 90,
  });
  expect(
    stampTransformDraft(layer, "rotate", start, start, {
      angleDelta: (225 * Math.PI) / 180,
    }).rotation,
  ).toBe(225);
  expect(
    stampTransformDraft(layer, "rotate", start, start, {
      snap: true,
      angleDelta: (37 * Math.PI) / 180,
    }).rotation,
  ).toBe(30);
  expect(
    stampTransformDraft({ ...layer, rotation: 350 }, "rotate", start, start, {
      angleDelta: (25 * Math.PI) / 180,
    }).rotation,
  ).toBe(15);
  expect(
    stampAngleDelta((179 * Math.PI) / 180, (-179 * Math.PI) / 180),
  ).toBeCloseTo((2 * Math.PI) / 180);
});

test("S preserves aspect ratio and clamps the shared factor rather than distorting either dimension", () => {
  expect(
    stampTransformDraft(layer, "scale", start, { x: 406, y: 232 }),
  ).toEqual({
    id: layer.id,
    width: 547.5,
    height: 216,
  });
  const max = stampTransformDraft(layer, "scale", start, { x: 100000, y: 232 });
  expect(max.width).toBe(1024);
  expect(max.width / max.height).toBeCloseTo(layer.width / layer.height, 6);
  const min = stampTransformDraft(layer, "scale", start, { x: 256, y: 232 });
  expect(min.height).toBe(1);
  expect(min.width / min.height).toBeCloseTo(layer.width / layer.height, 6);
  expect(
    stampTransformDraft(
      layer,
      "scale",
      start,
      { x: 392, y: 232 },
      { snap: true },
    ),
  ).toEqual({ id: layer.id, width: 511, height: 201.6 });
});

test("S X/Y changes only its constrained dimension", () => {
  expect(
    stampTransformDraft(
      layer,
      "scale",
      start,
      { x: 406, y: 232 },
      { axis: "x" },
    ),
  ).toEqual({ id: layer.id, width: 547.5, height: 144 });
  expect(
    stampTransformDraft(
      layer,
      "scale",
      start,
      { x: 406, y: 232 },
      { axis: "y" },
    ),
  ).toEqual({ id: layer.id, width: 365, height: 216 });
});

test("starting R or S over the pivot remains finite, smooth and bounded", () => {
  const pivot = { x: layer.x, y: layer.y };
  expect(
    stampTransformDraft(layer, "rotate", pivot, { x: 286, y: 232 }).rotation,
  ).toBe(30);
  const scale = stampTransformDraft(layer, "scale", pivot, { x: 306, y: 232 });
  expect(scale.width).toBeCloseTo(layer.width * Math.exp(0.5), 5);
  expect(scale.height).toBeCloseTo(layer.height * Math.exp(0.5), 5);
  expect(stampTransformDraft(layer, "scale", pivot, pivot)).toEqual({
    id: layer.id,
    width: layer.width,
    height: layer.height,
  });
});

test("corner resizing follows rotated local axes and never mutates the component snapshot", () => {
  const rotated = { ...layer, rotation: 90 },
    before = structuredClone(rotated);
  expect(
    stampTransformDraft(rotated, "resize", start, { x: 346, y: 252 }),
  ).toEqual({
    id: layer.id,
    width: 405,
    height: 164,
  });
  expect(rotated).toEqual(before);
});

// Pure editor operations; document validation remains the final boundary.
const bounded = (n, min, max) => Math.max(min, Math.min(max, n));
const snapped = (n, grid) => (grid ? Math.round(n / grid) * grid : n);
export function movePatternLayer(layer, delta, grid = 0) {
  return {
    ...layer,
    x: bounded(snapped(layer.x + delta[0], grid), -512, 1024),
    y: bounded(snapped(layer.y + delta[1], grid), -512, 1024),
  };
}
export function resizePatternLayer(
  layer,
  point,
  { grid = 0, aspect = false } = {},
) {
  const angle = (-layer.rotation * Math.PI) / 180,
    dx = point[0] - layer.x,
    dy = point[1] - layer.y;
  let width = Math.abs(dx * Math.cos(angle) - dy * Math.sin(angle)) * 2,
    height = Math.abs(dx * Math.sin(angle) + dy * Math.cos(angle)) * 2;
  width = Math.max(1, snapped(width, grid));
  height = Math.max(1, snapped(height, grid));
  if (aspect) {
    const scale = bounded(
      Math.max(width / layer.width, height / layer.height),
      Math.max(1 / layer.width, 1 / layer.height),
      Math.min(1024 / layer.width, 1024 / layer.height),
    );
    width = layer.width * scale;
    height = layer.height * scale;
  }
  return {
    ...layer,
    width: bounded(width, 1, 1024),
    height: bounded(height, 1, 1024),
  };
}
export function rotatePatternLayer(layer, start, point, snap = false) {
  const a = Math.atan2(start[1] - layer.y, start[0] - layer.x),
    b = Math.atan2(point[1] - layer.y, point[0] - layer.x);
  let delta = ((b - a) * 180) / Math.PI;
  delta = ((delta + 540) % 360) - 180;
  let rotation = snapped(layer.rotation + delta, snap ? 15 : 1);
  rotation = ((rotation + 540) % 360) - 180;
  return { ...layer, rotation };
}
export function reflectPatternLayer(layer, axis) {
  return {
    ...layer,
    name: layer.name + " reflected",
    rotation: -layer.rotation,
    ...(axis === "x"
      ? { x: 512 - layer.x, flipX: !layer.flipX }
      : { y: 512 - layer.y, flipY: !layer.flipY }),
  };
}
export function radialPatternLayers(layer, count, radius) {
  const n = bounded(Math.round(Number(count) || 6), 2, 12),
    r = bounded(Number(radius) || 0, 0, 256);
  return Array.from({ length: n }, (_, i) => {
    const a = (i * 2 * Math.PI) / n;
    return {
      ...layer,
      name: layer.name + " " + (i + 1),
      x: 256 + Math.sin(a) * r,
      y: 256 - Math.cos(a) * r,
      rotation: ((layer.rotation + (i * 360) / n + 540) % 360) - 180,
    };
  });
}

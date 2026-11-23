// Transform only the bounded artwork geometry. Text runs, SVG and image sources
// stay untouched, and every mouse update is derived from the gesture's snapshot.
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
const round = (value) => Number(value.toFixed(6));
const radians = (degrees) => (degrees * Math.PI) / 180;
const snapTo = (value, step) => Math.round(value / step) * step;

export function stampCanvasPoint(pointer, rect) {
  return {
    x: ((pointer.clientX - rect.left) * 512) / Math.max(1, rect.width),
    y: ((pointer.clientY - rect.top) * 512) / Math.max(1, rect.height),
  };
}

export function stampAngleDelta(from, to) {
  return Math.atan2(Math.sin(to - from), Math.cos(to - from));
}

export function stampTransformDraft(
  layer,
  mode,
  start,
  point,
  { snap = false, axis = null, angleDelta } = {},
) {
  const dx = point.x - start.x,
    dy = point.y - start.y;
  const result = { id: layer.id };
  if (mode === "move") {
    result.x = round(
      axis === "y"
        ? layer.x
        : clamp(snap ? snapTo(layer.x + dx, 8) : layer.x + dx, -512, 1024),
    );
    result.y = round(
      axis === "x"
        ? layer.y
        : clamp(snap ? snapTo(layer.y + dy, 8) : layer.y + dy, -512, 1024),
    );
  } else if (mode === "resize") {
    // The corner is in the component's local axes, even after an R rotation.
    const angle = radians(layer.rotation),
      localX = dx * Math.cos(angle) + dy * Math.sin(angle),
      localY = -dx * Math.sin(angle) + dy * Math.cos(angle);
    result.width = round(
      clamp(
        snap ? snapTo(layer.width + localX * 2, 8) : layer.width + localX * 2,
        1,
        1024,
      ),
    );
    result.height = round(
      clamp(
        snap ? snapTo(layer.height + localY * 2, 8) : layer.height + localY * 2,
        1,
        1024,
      ),
    );
  } else if (mode === "rotate") {
    const radius = Math.hypot(start.x - layer.x, start.y - layer.y);
    let delta =
      radius < 8
        ? dx
        : (angleDelta ??
            stampAngleDelta(
              Math.atan2(start.y - layer.y, start.x - layer.x),
              Math.atan2(point.y - layer.y, point.x - layer.x),
            )) *
          (180 / Math.PI);
    if (snap) delta = snapTo(delta, 15);
    let rotation = layer.rotation + delta;
    if (rotation < -360 || rotation > 360) rotation %= 360;
    result.rotation = round(rotation);
  } else if (mode === "scale") {
    const radius = Math.hypot(start.x - layer.x, start.y - layer.y);
    // Starting over the pivot is valid: horizontal motion gives a smooth,
    // positive scale instead of dividing by zero or jumping to a huge size.
    let factor =
      radius < 8
        ? Math.exp(clamp(dx / 100, -10, 10))
        : Math.hypot(point.x - layer.x, point.y - layer.y) / radius;
    if (snap) factor = snapTo(factor, 0.1);
    const min = Math.max(
        axis === "y" ? 0 : 1 / layer.width,
        axis === "x" ? 0 : 1 / layer.height,
      ),
      max = Math.min(
        axis === "y" ? Infinity : 1024 / layer.width,
        axis === "x" ? Infinity : 1024 / layer.height,
      );
    factor = clamp(factor, min, max);
    result.width = round(axis === "y" ? layer.width : layer.width * factor);
    result.height = round(axis === "x" ? layer.height : layer.height * factor);
  }
  return result;
}

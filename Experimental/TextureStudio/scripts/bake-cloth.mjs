// Deterministic, offline position-based cloth simulation. The app loads only the
// settled mesh, so inspecting a material never runs physics or resets the folds.
// Run: node scripts/bake-cloth.mjs
import fs from "node:fs";
const N = 73,
  width = 4.9,
  spacing = width / (N - 1),
  count = N * N;
const positions = new Float64Array(count * 3),
  previous = new Float64Array(count * 3),
  invMass = new Float64Array(count).fill(1);
const center = [0, 1.39, 0],
  radius = 1.169,
  floor = 0.018;
let randomState = 419;
const random = () => {
  randomState = (Math.imul(1664525, randomState) + 1013904223) >>> 0;
  return randomState / 4294967296;
};
for (let row = 0; row < N; row++)
  for (let col = 0; col < N; col++) {
    const i = row * N + col,
      x = (col / (N - 1) - 0.5) * width,
      z = (row / (N - 1) - 0.5) * width;
    positions.set(
      [x + 0.045, 2.76 + (random() - 0.5) * 0.016, z - 0.025],
      i * 3,
    );
    if (Math.hypot(x, z) < 0.105) {
      invMass[i] = 0;
      positions[i * 3 + 1] =
        center[1] +
        Math.sqrt(
          radius * radius - positions[i * 3] ** 2 - positions[i * 3 + 2] ** 2,
        );
    }
  }
previous.set(positions);
const constraints = [];
function link(a, b, rest, stiffness) {
  constraints.push([a, b, rest, stiffness]);
}
for (let row = 0; row < N; row++)
  for (let col = 0; col < N; col++) {
    const i = row * N + col;
    if (col < N - 1) link(i, i + 1, spacing, 0.98);
    if (row < N - 1) link(i, i + N, spacing, 0.98);
    if (col < N - 1 && row < N - 1) {
      link(i, i + N + 1, spacing * Math.SQRT2, 0.62);
      link(i + 1, i + N, spacing * Math.SQRT2, 0.62);
    }
    if (col < N - 2) link(i, i + 2, spacing * 2, 0.055);
    if (row < N - 2) link(i, i + N * 2, spacing * 2, 0.055);
  }
// Alternate the solve direction each iteration to avoid a diagonal solver bias.
for (let step = 0; step < 780; step++) {
  for (let i = 0; i < count; i++)
    if (invMass[i]) {
      const j = i * 3;
      for (let axis = 0; axis < 3; axis++) {
        const value = positions[j + axis],
          velocity = (value - previous[j + axis]) * 0.985;
        previous[j + axis] = value;
        positions[j + axis] =
          value + velocity + (axis === 1 ? -9.81 / (120 * 120) : 0);
      }
      if (step < 160)
        positions[j] +=
          0.00009 * Math.sin(step * 0.035 + positions[j + 2] * 4.3);
    }
  for (let iteration = 0; iteration < 11; iteration++) {
    const reverse = (iteration + step) % 2;
    for (let k = 0; k < constraints.length; k++) {
      const [a, b, rest, strength] =
        constraints[reverse ? constraints.length - 1 - k : k];
      const sum = invMass[a] + invMass[b];
      if (!sum) continue;
      const ai = a * 3,
        bi = b * 3,
        dx = positions[bi] - positions[ai],
        dy = positions[bi + 1] - positions[ai + 1],
        dz = positions[bi + 2] - positions[ai + 2];
      const length = Math.hypot(dx, dy, dz);
      const correction =
        (((length - rest) / Math.max(length, 1e-8)) * strength) / sum;
      positions[ai] += dx * correction * invMass[a];
      positions[ai + 1] += dy * correction * invMass[a];
      positions[ai + 2] += dz * correction * invMass[a];
      positions[bi] -= dx * correction * invMass[b];
      positions[bi + 1] -= dy * correction * invMass[b];
      positions[bi + 2] -= dz * correction * invMass[b];
    }
    for (let i = 0; i < count; i++)
      if (invMass[i]) {
        const j = i * 3,
          dx = positions[j] - center[0],
          dy = positions[j + 1] - center[1],
          dz = positions[j + 2] - center[2];
        const distance = Math.hypot(dx, dy, dz);
        if (distance < radius) {
          const scale = radius / Math.max(distance, 1e-8);
          positions[j] = center[0] + dx * scale;
          positions[j + 1] = center[1] + dy * scale;
          positions[j + 2] = center[2] + dz * scale;
        }
        const contactFloor =
          Math.hypot(positions[j], positions[j + 2]) < 1.265 ? 0.224 : floor;
        if (positions[j + 1] < contactFloor) positions[j + 1] = contactFloor;
      }
  }
}
const data = {
  version: 1,
  grid: N,
  width,
  sphere: { center, radius: 1.16 },
  floor,
  method:
    "Frozen PBD cloth: structural, shear and bending constraints; sphere/floor collision; center pins.",
  positions: Array.from(positions, (v) => Number(v.toFixed(5))),
};
fs.mkdirSync(new URL("../src/assets/", import.meta.url), { recursive: true });
fs.writeFileSync(
  new URL("../src/assets/draped-cloth.json", import.meta.url),
  JSON.stringify(data),
);
console.log(
  `Baked ${count} vertices / ${(N - 1) ** 2 * 2} triangles; ${constraints.length} constraints. Frozen drape saved.`,
);

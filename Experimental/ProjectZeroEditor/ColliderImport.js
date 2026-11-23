//============================================================================================================================================
//                                                             COLLIDERIMPORT.JS
//============================================================================================================================================
// 🧩 Custom collision: reads your own collider file with the Geometry Compiler readers (OBJ, STL, PLY, glTF / GLB, Frontier .facet),
// welds it, and keeps it in the object's local space. The record lives in Values["Collision Custom"]. Coordinates are rounded to 0.1 mm
// and capped at CustomTriangleLimit so the scene stays small in browser storage. "Collision Custom Mode" picks whether the physics
// uses the triangles as they are or their convex hull. No browser APIs except File.arrayBuffer() in importCollider.

import { readGeometry, IMPORT_FORMATS } from "../Shared/GeometryCompile/Engine/ImportFormats.js";
import { convexHull } from "./ConvexHull.js";

export const CustomKey = "Collision Custom";
export const CustomModeKey = "Collision Custom Mode";
export const CustomModes = ["Triangles as imported", "Convex hull of the import"];
export const CustomTriangleLimit = 4000;
export const ColliderAccept = IMPORT_FORMATS.filter((Each) => Each.reads).map((Each) => "." + Each.ext).join(",");

// Weld by position and drop triangles that collapse.
function Weld(Positions, Indices) {
  const Seen = new Map(), Out = [], Remap = new Uint32Array(Positions.length / 3);
  for (let V = 0; V < Remap.length; V++) {
    const Point = [0, 1, 2].map((K) => Math.round(Positions[V * 3 + K] * 1e4) / 1e4);
    const Key = Point.join(",");
    if (!Seen.has(Key)) {
      Seen.set(Key, Seen.size);
      Out.push(...Point);
    }
    Remap[V] = Seen.get(Key);
  }
  const Kept = [];
  for (let T = 0; T < Indices.length; T += 3) {
    const [A, B, C] = [Remap[Indices[T]], Remap[Indices[T + 1]], Remap[Indices[T + 2]]];
    if (A !== B && B !== C && C !== A) Kept.push(A, B, C);
  }
  return { Positions: Out, Indices: Kept };
}

// Edge-use count: closed when every edge is shared by exactly two triangles.
export function closedSurface(Indices) {
  const Uses = new Map();
  for (let T = 0; T < Indices.length; T += 3)
    for (let K = 0; K < 3; K++) {
      const A = Indices[T + K], B = Indices[T + ((K + 1) % 3)], Key = A < B ? A + ":" + B : B + ":" + A;
      Uses.set(Key, (Uses.get(Key) || 0) + 1);
    }
  for (const Count of Uses.values()) if (Count !== 2) return false;
  return Uses.size > 0;
}

export function enclosedVolume(Positions, Indices) {
  let Sum = 0;
  for (let T = 0; T < Indices.length; T += 3) {
    const [A, B, C] = [Indices[T] * 3, Indices[T + 1] * 3, Indices[T + 2] * 3];
    const P = Positions;
    Sum += P[A] * (P[B + 1] * P[C + 2] - P[B + 2] * P[C + 1]) - P[A + 1] * (P[B] * P[C + 2] - P[B + 2] * P[C]) + P[A + 2] * (P[B] * P[C + 1] - P[B + 1] * P[C]);
  }
  return Math.abs(Sum) / 6;
}

// Turns a file's bytes into the stored record. Throws with a readable message when the file can't be used.
export function colliderFromBytes(Name, Bytes) {
  const Raw = readGeometry(Name, Bytes);
  const { Positions, Indices } = Weld(Raw.positions, Raw.indices);
  const Triangles = Indices.length / 3;
  if (!Triangles) throw new Error(`${Name} has no triangles.`);
  if (Triangles > CustomTriangleLimit) throw new Error(`${Name} has ${Triangles.toLocaleString("en-US")} triangles; colliders are limited to ${CustomTriangleLimit.toLocaleString("en-US")}. Simplify it first, or use Convex hull.`);
  const Hull = convexHull(Positions);
  const Closed = closedSurface(Indices);
  return {
    Name,
    Format: Raw.format || Name.split(".").pop().toUpperCase(),
    Positions,
    Indices,
    Triangles,
    Vertices: Positions.length / 3,
    Closed,
    Volume: Closed ? enclosedVolume(Positions, Indices) : Hull ? Hull.Volume : 0,
    HullVolume: Hull ? Hull.Volume : 0,
    At: Date.now(),
  };
}

export async function importCollider(File_) {
  return colliderFromBytes(File_.name, await File_.arrayBuffer());
}

// The triangles physics would use for a stored record in the chosen mode, as typed arrays.
export function customTriangles(Collider, Mode = 0) {
  if (!Collider?.Positions?.length) return null;
  if (Mode === 1) {
    const Hull = convexHull(Collider.Positions);
    return Hull ? { Positions: Hull.Positions, Indices: Hull.Indices, Volume: Hull.Volume } : null;
  }
  return { Positions: Float32Array.from(Collider.Positions), Indices: Uint32Array.from(Collider.Indices), Volume: Collider.Volume };
}

//============================================================================================================================================
//                                                             PIECECOLLISION.JS
//============================================================================================================================================
// 🧩 Collision for fractured pieces. Each Voronoi piece gets the collider type chosen on the Fracture recipe (PieceCollision):
// convex hull (ConvexHull.js), box / sphere / capsule (the Geometry Compiler's fitCollision) or its exact triangles. Reports how much
// of each collider the piece fills, the summed collider volume and how many triangles or hull faces physics would carry.

import { fitCollision, collisionVolume } from "../Shared/GeometryCompile/Engine/MeshAnalysis.js";
import { convexHull } from "./ConvexHull.js";

const PiecePoints = (Cell) => {
  const Out = [];
  for (const Face of Cell.faces) for (const Point of Face.pts) Out.push(Point[0], Point[1], Point[2]);
  return new Float32Array(Out);
};
const PieceTriangles = (Cell) => Cell.faces.reduce((Sum, Face) => Sum + Math.max(0, Face.pts.length - 2), 0);

// Returns { Pieces: [{ Fit, Volume, Fill, Cost }], Volume, Fill, Cost, Unit }.
export function pieceColliders(Cells, Type = "convex") {
  const Pieces = Cells.map((Cell) => {
    if (Type === "triangles") {
      const Count = PieceTriangles(Cell);
      return { Fit: null, Volume: Cell.volume, Fill: 1, Cost: Count };
    }
    const Points = PiecePoints(Cell);
    if (Type === "convex") {
      const Hull = convexHull(Points);
      if (!Hull) return { Fit: null, Volume: Cell.volume, Fill: 1, Cost: 0 };
      return { Fit: { type: "convex", Positions: Hull.Positions, Indices: Hull.Indices }, Volume: Hull.Volume, Fill: Math.min(1, Cell.volume / Math.max(1e-12, Hull.Volume)), Cost: Hull.Faces };
    }
    const Fit = fitCollision(Points, Type, 0), Volume = collisionVolume(Fit);
    return { Fit, Volume, Fill: Math.min(1, Cell.volume / Math.max(1e-12, Volume)), Cost: 1 };
  });
  const Volume = Pieces.reduce((Sum, Each) => Sum + Each.Volume, 0);
  const Solid = Cells.reduce((Sum, Cell) => Sum + Cell.volume, 0);
  const Cost = Pieces.reduce((Sum, Each) => Sum + Each.Cost, 0);
  return { Pieces, Volume, Fill: Volume > 0 ? Solid / Volume : 0, Cost, Unit: Type === "triangles" ? "triangles" : Type === "convex" ? "hull faces" : "primitives" };
}

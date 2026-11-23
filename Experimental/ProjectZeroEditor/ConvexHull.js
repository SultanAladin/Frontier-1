//============================================================================================================================================
//                                                               CONVEXHULL.JS
//============================================================================================================================================
// 🧩 3D convex hull for collision proxies and fracture pieces, by quickhull: start from the widest tetrahedron and give every other
// point to a face it lies outside of. Then, while any face has outside points, take that face's farthest point, remove every face
// it can see, join the horizon (edges between seen and unseen faces) to it and hand the orphaned points to the new faces. Points
// within the tolerance of a face are dropped, so coplanar points (a fracture piece has many) never make sliver faces. Points are
// welded on a grid first. Pure math, no browser APIs. The result is triangulated and outward-facing.

const Subtract = (A, B) => [A[0] - B[0], A[1] - B[1], A[2] - B[2]];
const Cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const Dot = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const Length = (A) => Math.hypot(A[0], A[1], A[2]);

// Welds positions (flat xyz) on a grid of `Step` and returns unique points as [x, y, z] arrays.
function UniquePoints(Positions, Step) {
  const Seen = new Map(), Points = [];
  for (let Index = 0; Index < Positions.length; Index += 3) {
    const Point = [Positions[Index], Positions[Index + 1], Positions[Index + 2]];
    const Key = Point.map((Each) => Math.round(Each / Step)).join(",");
    if (!Seen.has(Key)) {
      Seen.set(Key, Points.length);
      Points.push(Point);
    }
  }
  return Points;
}

function Plane(Points, A, B, C) {
  const Normal = Cross(Subtract(Points[B], Points[A]), Subtract(Points[C], Points[A]));
  const Size = Length(Normal) || 1;
  const Unit = Normal.map((Each) => Each / Size);
  return { Vertices: [A, B, C], Normal: Unit, Offset: Dot(Unit, Points[A]) };
}

// Returns { Positions: Float32Array, Indices: Uint32Array, Volume, Area, Faces, Points } or null for degenerate input (flat or a line).
export function convexHull(Positions) {
  if (!Positions || Positions.length < 12) return null;
  let Extent = 0;
  for (let Index = 0; Index < Positions.length; Index++) Extent = Math.max(Extent, Math.abs(Positions[Index]));
  const Step = Math.max(1e-9, Extent * 1e-6), Epsilon = Math.max(1e-9, Extent * 2e-5);
  const Points = UniquePoints(Positions, Step);
  if (Points.length < 4) return null;

  // Widest tetrahedron: the two farthest-apart extreme points, then the point farthest from their line, then from their plane.
  const Extremes = [];
  for (let Axis = 0; Axis < 3; Axis++) {
    let Low = 0, High = 0;
    Points.forEach((Point, Index) => {
      if (Point[Axis] < Points[Low][Axis]) Low = Index;
      if (Point[Axis] > Points[High][Axis]) High = Index;
    });
    Extremes.push(Low, High);
  }
  let A = Extremes[0], B = Extremes[1], Widest = -1;
  for (const I of Extremes)
    for (const J of Extremes) {
      const Distance = Length(Subtract(Points[I], Points[J]));
      if (Distance > Widest) [Widest, A, B] = [Distance, I, J];
    }
  if (Widest < Epsilon) return null;
  const Direction = Subtract(Points[B], Points[A]);
  let C = -1, Farthest = Epsilon;
  Points.forEach((Point, Index) => {
    const Distance = Length(Cross(Direction, Subtract(Point, Points[A]))) / Widest;
    if (Distance > Farthest) [Farthest, C] = [Distance, Index];
  });
  if (C < 0) return null;
  const Triangle0 = Plane(Points, A, B, C);
  let D = -1;
  Farthest = Epsilon;
  Points.forEach((Point, Index) => {
    const Distance = Math.abs(Dot(Triangle0.Normal, Point) - Triangle0.Offset);
    if (Distance > Farthest) [Farthest, D] = [Distance, Index];
  });
  if (D < 0) return null;

  // Orient the four starting faces outward from the tetrahedron's centre.
  const Inside = [0, 1, 2].map((Axis) => (Points[A][Axis] + Points[B][Axis] + Points[C][Axis] + Points[D][Axis]) / 4);
  let Faces = [[A, B, C], [A, C, D], [A, D, B], [B, D, C]].map(([P, Q, R]) => {
    const Face = Plane(Points, P, Q, R);
    return Dot(Face.Normal, Inside) - Face.Offset > 0 ? Plane(Points, P, R, Q) : Face;
  });

  const Distance = (Face, Point) => Dot(Face.Normal, Point) - Face.Offset;
  // Hands each point to the face it is farthest outside of; points inside every face are dropped.
  const Assign = (Indices, Targets) => {
    for (const Index of Indices) {
      let Best = null, Far = Epsilon;
      for (const Face of Targets) {
        const Away = Distance(Face, Points[Index]);
        if (Away > Far) [Best, Far] = [Face, Away];
      }
      if (Best) Best.Outside.push(Index);
    }
  };
  for (const Face of Faces) Face.Outside = [];
  const Start = new Set([A, B, C, D]);
  Assign(Points.map((_, Index) => Index).filter((Index) => !Start.has(Index)), Faces);
  for (let Guard = 0; Guard < Points.length * 4; Guard++) {
    const Busy = Faces.find((Face) => Face.Outside.length);
    if (!Busy) break;
    let Apex = Busy.Outside[0], Far = -Infinity;
    for (const Index of Busy.Outside) {
      const Away = Distance(Busy, Points[Index]);
      if (Away > Far) [Far, Apex] = [Away, Index];
    }
    const Point = Points[Apex];
    const Seen = Faces.filter((Face) => Distance(Face, Point) > Epsilon);
    if (!Seen.includes(Busy)) Seen.push(Busy);
    // Horizon: directed edges of seen faces whose reverse edge is not on a seen face.
    const Edges = new Set();
    for (const Face of Seen) for (let K = 0; K < 3; K++) Edges.add(Face.Vertices[K] + ":" + Face.Vertices[(K + 1) % 3]);
    const Added = [];
    for (const Edge of Edges) {
      const [From, To] = Edge.split(":").map(Number);
      if (!Edges.has(To + ":" + From)) Added.push({ ...Plane(Points, From, To, Apex), Outside: [] });
    }
    const Orphans = Seen.flatMap((Face) => Face.Outside).filter((Index) => Index !== Apex);
    Faces = Faces.filter((Face) => !Seen.includes(Face)).concat(Added);
    Assign(Orphans, Added);
  }

  // Compact to the vertices the hull uses.
  const Remap = new Map(), Out = [], Indices = [];
  for (const Face of Faces)
    for (const Vertex of Face.Vertices) {
      if (!Remap.has(Vertex)) {
        Remap.set(Vertex, Remap.size);
        Out.push(...Points[Vertex]);
      }
      Indices.push(Remap.get(Vertex));
    }
  let Volume = 0, Area = 0;
  for (const Face of Faces) {
    const [P, Q, R] = Face.Vertices.map((Vertex) => Points[Vertex]);
    Volume += Dot(P, Cross(Q, R)) / 6;
    Area += Length(Cross(Subtract(Q, P), Subtract(R, P))) / 2;
  }
  return { Positions: new Float32Array(Out), Indices: new Uint32Array(Indices), Volume: Math.abs(Volume), Area, Faces: Faces.length, Points: Remap.size };
}

// Grows a hull outward by `Margin`: every face plane moves out by the margin. Each vertex moves along the sum of its distinct face
// normals, scaled so the move measured along each of those normals is the margin (a cube corner moves margin on every axis). Coplanar
// triangles count once, so the split of a square face into two triangles doesn't bias the direction.
export function inflateHull(Hull, Margin) {
  if (!Hull || !Margin) return Hull;
  const P = Hull.Positions, I = Hull.Indices, Count = P.length / 3;
  const Faces = Array.from({ length: Count }, () => []);
  for (let T = 0; T < I.length; T += 3) {
    const [A, B, C] = [I[T], I[T + 1], I[T + 2]].map((V) => [P[V * 3], P[V * 3 + 1], P[V * 3 + 2]]);
    const N = Cross(Subtract(B, A), Subtract(C, A)), Size = Length(N);
    if (Size < 1e-12) continue;
    const Unit = N.map((Each) => Each / Size);
    for (const V of [I[T], I[T + 1], I[T + 2]]) if (!Faces[V].some((Other) => Dot(Other, Unit) > 0.99999)) Faces[V].push(Unit);
  }
  const Out = new Float32Array(P.length);
  for (let V = 0; V < Count; V++) {
    const Sum = [0, 0, 0];
    for (const N of Faces[V]) for (let K = 0; K < 3; K++) Sum[K] += N[K];
    const Size = Length(Sum) || 1, Direction = Sum.map((Each) => Each / Size);
    const Least = Math.max(0.25, Math.min(...Faces[V].map((N) => Dot(N, Direction)), 1));
    for (let K = 0; K < 3; K++) Out[V * 3 + K] = P[V * 3 + K] + (Margin * Direction[K]) / Least;
  }
  let Volume = 0;
  for (let T = 0; T < I.length; T += 3) {
    const [A, B, C] = [I[T], I[T + 1], I[T + 2]].map((V) => [Out[V * 3], Out[V * 3 + 1], Out[V * 3 + 2]]);
    Volume += Dot(A, Cross(B, C)) / 6;
  }
  return { ...Hull, Positions: Out, Volume: Math.abs(Volume) };
}

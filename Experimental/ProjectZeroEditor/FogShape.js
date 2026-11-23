export const FogShapes = [
  "Box",
  "Sphere",
  "Ellipsoid",
  "Diamond",
  "Cylinder",
  "Cone",
  "Prism",
  "Custom",
];
const NumberIn = (Value, Min, Max, Default) =>
  Number.isFinite(Number(Value))
    ? Math.max(Min, Math.min(Max, Number(Value)))
    : Default;
export function ResolveFogShape(Values = {}) {
  const Saved = Values.FogShape || {},
    Half = Values["Half Size"] || [100, 100, 100];
  const Shape = {
    Type: FogShapes.includes(Saved.Type) ? Saved.Type : "Box",
    Width: NumberIn(
      Saved.Width,
      0.01,
      200000,
      NumberIn(Half[0], 0.005, 100000, 100) * 2,
    ),
    Depth: NumberIn(
      Saved.Depth,
      0.01,
      200000,
      NumberIn(Half[1], 0.005, 100000, 100) * 2,
    ),
    Height: NumberIn(
      Saved.Height,
      0.01,
      200000,
      NumberIn(Half[2], 0.005, 100000, 100) * 2,
    ),
    Radius: NumberIn(
      Saved.Radius,
      0.01,
      100000,
      NumberIn(Half[0], 0.01, 100000, 100),
    ),
    Sides: Math.round(NumberIn(Saved.Sides, 3, 16, 6)),
    Points: (Array.isArray(Saved.Points) && Saved.Points.length >= 3
      ? Saved.Points
      : [
          [-100, -70],
          [30, -100],
          [100, -10],
          [50, 100],
          [-80, 60],
        ]
    )
      .slice(0, 32)
      .map((Point) => [
        NumberIn(Point?.[0], -100000, 100000, 0),
        NumberIn(Point?.[1], -100000, 100000, 0),
      ]),
  };
  if (!ValidFogPolygon(Shape.Points))
    Shape.Points = [
      [-100, -70],
      [30, -100],
      [100, -10],
      [50, 100],
      [-80, 60],
    ];
  return Shape;
}
export function FogGeometry(Shape) {
  const { Type, Width, Depth, Height, Radius, Sides, Points } = Shape;
  let Vertices = [],
    Edges = [],
    Faces = [];
  const Extrude = (Polygon) => {
    const Count = Polygon.length;
    Vertices = [
      ...Polygon.map(([X, Y]) => [X, Y, -Height / 2]),
      ...Polygon.map(([X, Y]) => [X, Y, Height / 2]),
    ];
    for (let I = 0; I < Count; I++) {
      const J = (I + 1) % Count;
      Edges.push([I, J], [I + Count, J + Count], [I, I + Count]);
      Faces.push([I, J, J + Count, I + Count]);
    }
    Faces.push(
      Array.from({ length: Count }, (_, I) => I),
      Array.from({ length: Count }, (_, I) => I + Count),
    );
  };
  if (Type === "Box")
    Extrude([
      [-Width / 2, -Depth / 2],
      [Width / 2, -Depth / 2],
      [Width / 2, Depth / 2],
      [-Width / 2, Depth / 2],
    ]);
  else if (Type === "Custom") Extrude(Points);
  else if (Type === "Prism" || Type === "Cylinder")
    Extrude(
      Array.from({ length: Type === "Prism" ? Sides : 48 }, (_, I) => {
        const A = (I * 2 * Math.PI) / (Type === "Prism" ? Sides : 48);
        return [Radius * Math.cos(A), Radius * Math.sin(A)];
      }),
    );
  else if (Type === "Cone") {
    Vertices = Array.from({ length: 48 }, (_, I) => [
      Radius * Math.cos((I * Math.PI) / 24),
      Radius * Math.sin((I * Math.PI) / 24),
      -Height / 2,
    ]);
    Vertices.push([0, 0, Height / 2]);
    for (let I = 0; I < 48; I++) {
      Edges.push([I, (I + 1) % 48]);
      if (I % 6 === 0) Edges.push([I, 48]);
      Faces.push([I, (I + 1) % 48, 48]);
    }
  } else if (Type === "Diamond") {
    Vertices = [
      [Width / 2, 0, 0],
      [0, Depth / 2, 0],
      [-Width / 2, 0, 0],
      [0, -Depth / 2, 0],
      [0, 0, Height / 2],
      [0, 0, -Height / 2],
    ];
    for (let I = 0; I < 4; I++) {
      Edges.push([I, (I + 1) % 4], [I, 4], [I, 5]);
      Faces.push([I, (I + 1) % 4, 4], [I, (I + 1) % 4, 5]);
    }
  } else {
    const Radii =
      Type === "Sphere"
        ? [Radius, Radius, Radius]
        : [Width / 2, Depth / 2, Height / 2];
    for (let Latitude = 0; Latitude <= 12; Latitude++)
      for (let Longitude = 0; Longitude < 24; Longitude++) {
        const A = (Latitude * Math.PI) / 12,
          B = (Longitude * Math.PI) / 12;
        Vertices.push([
          Radii[0] * Math.sin(A) * Math.cos(B),
          Radii[1] * Math.sin(A) * Math.sin(B),
          Radii[2] * Math.cos(A),
        ]);
        const I = Latitude * 24 + Longitude,
          J = Latitude * 24 + ((Longitude + 1) % 24);
        Edges.push([I, J]);
        if (Latitude < 12) {
          if (Longitude % 3 === 0) Edges.push([I, I + 24]);
          Faces.push([I, J, J + 24, I + 24]);
        }
      }
  }
  const Min = [0, 1, 2].map((Axis) =>
      Math.min(...Vertices.map((Point) => Point[Axis])),
    ),
    Max = [0, 1, 2].map((Axis) =>
      Math.max(...Vertices.map((Point) => Point[Axis])),
    );
  return {
    Vertices,
    Edges,
    Faces,
    Min,
    Max,
    Size: Max.map((Value, Axis) => Value - Min[Axis]),
  };
}
export function ValidFogPolygon(Points) {
  let Area = 0;
  const Cross = (A, B, C) =>
    (B[0] - A[0]) * (C[1] - A[1]) - (B[1] - A[1]) * (C[0] - A[0]);
  const On = (A, B, C) =>
    Math.abs(Cross(A, B, C)) < 1e-7 &&
    C[0] >= Math.min(A[0], B[0]) &&
    C[0] <= Math.max(A[0], B[0]) &&
    C[1] >= Math.min(A[1], B[1]) &&
    C[1] <= Math.max(A[1], B[1]);
  for (let I = 0; I < Points.length; I++) {
    const A = Points[I],
      B = Points[(I + 1) % Points.length];
    Area += A[0] * B[1] - B[0] * A[1];
    if (Math.hypot(A[0] - B[0], A[1] - B[1]) < 0.001) return false;
    for (let J = I + 1; J < Points.length; J++) {
      if (J === I + 1 || (I === 0 && J === Points.length - 1)) continue;
      const C = Points[J],
        D = Points[(J + 1) % Points.length];
      if (
        (Cross(A, B, C) * Cross(A, B, D) < 0 &&
          Cross(C, D, A) * Cross(C, D, B) < 0) ||
        On(A, B, C) ||
        On(A, B, D) ||
        On(C, D, A) ||
        On(C, D, B)
      )
        return false;
    }
  }
  return Math.abs(Area) > 0.001;
}

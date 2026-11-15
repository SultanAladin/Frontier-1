// Bounds are editor geometry, not a branch inside the volume raymarch shader.
export function ProjectBounds(Camera, Bounds, Width, Height) {
  const Dot = (Left, Right) =>
    Left.reduce((Sum, Value, Index) => Sum + Value * Right[Index], 0);
  const Corners = Array.from({ length: 8 }, (_, Index) => {
    const Offset = [0, 1, 2].map(
      (Axis) =>
        ((Index >> Axis) & 1 ? Bounds.boxMax[Axis] : Bounds.boxMin[Axis]) -
        Camera.position[Axis],
    );
    return [
      Dot(Offset, Camera.right),
      Dot(Offset, Camera.up),
      Dot(Offset, Camera.forward),
    ];
  });
  const Near = Math.max(Camera.near || 0.05, 0.001);
  const Tangent = Math.tan(Camera.fovY / 2);
  const Project = (Point) => [
    (0.5 + (Point[0] / (Point[2] * Tangent * Camera.aspect)) * 0.5) * Width,
    (0.5 - (Point[1] / (Point[2] * Tangent)) * 0.5) * Height,
  ];
  const Lines = [];
  for (let Index = 0; Index < 8; Index++) {
    for (let Axis = 0; Axis < 3; Axis++) {
      if ((Index >> Axis) & 1) continue;
      let Start = [...Corners[Index]],
        End = [...Corners[Index | (1 << Axis)]];
      if (Start[2] < Near && End[2] < Near) continue;
      if (Start[2] < Near || End[2] < Near) {
        const Fraction = (Near - Start[2]) / (End[2] - Start[2]);
        const Clipped = Start.map(
          (Value, Coordinate) => Value + Fraction * (End[Coordinate] - Value),
        );
        if (Start[2] < Near) Start = Clipped;
        else End = Clipped;
      }
      const Segment = [...Project(Start), ...Project(End)];
      if (Segment.every(Number.isFinite)) Lines.push(Segment);
    }
  }
  return Lines;
}

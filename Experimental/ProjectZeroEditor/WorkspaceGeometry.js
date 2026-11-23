//============================================================================================================================================
//                                                           WORKSPACEGEOMETRY.JS
//============================================================================================================================================
// 📐 Welded triangle meshes for the five primitives (so the UV unwrap can flood across shared edges) and a small orthographic orbit view
// that paints flat-shaded or textured polygons back to front on a 2D canvas. Browser authoring only; this is not the native renderer.

const Clamp = (Amount, Minimum, Maximum) => Math.max(Minimum, Math.min(Maximum, Amount));

// Positions in metres (y up), indices counter-clockwise seen from outside. Segment counts are capped so the 2D painter stays interactive.
export function PrimitiveSurface(Shape, Read, Scale = [1, 1, 1]) {
  const P = (Key, Initial) => (Number.isFinite(+Read(Key, Initial)) ? +Read(Key, Initial) : Initial);
  const Positions = [], Indices = [];
  const Vertex = (X, Y, Z) => (Positions.push(X * Scale[0], Y * Scale[1], Z * Scale[2]), Positions.length / 3 - 1);
  const Quad = (A, B, C, D) => Indices.push(A, B, C, A, C, D);
  if (Shape === "cube") {
    const X = P("Geometry Size X", 1) / 2, Y = P("Geometry Size Y", 1) / 2, Z = P("Geometry Size Z", 1) / 2;
    const C = [[-X, -Y, -Z], [X, -Y, -Z], [X, Y, -Z], [-X, Y, -Z], [-X, -Y, Z], [X, -Y, Z], [X, Y, Z], [-X, Y, Z]].map((Corner) => Vertex(...Corner));
    Quad(C[4], C[5], C[6], C[7]); // front +z
    Quad(C[1], C[0], C[3], C[2]); // back
    Quad(C[5], C[1], C[2], C[6]); // right
    Quad(C[0], C[4], C[7], C[3]); // left
    Quad(C[7], C[6], C[2], C[3]); // top
    Quad(C[0], C[1], C[5], C[4]); // bottom
  } else if (Shape === "sphere") {
    const R = P("Geometry Radius", 0.5), Segments = Clamp(Math.round(P("Geometry Segments", 32)), 3, 48), Rings = Clamp(Math.round(P("Geometry Rings", 16)), 2, 24);
    const Top = Vertex(0, R, 0), Rows = [];
    for (let Ring = 1; Ring < Rings; Ring++) {
      const Polar = (Ring / Rings) * Math.PI, Row = [];
      for (let Step = 0; Step < Segments; Step++) {
        const Around = (Step / Segments) * Math.PI * 2;
        Row.push(Vertex(R * Math.sin(Polar) * Math.cos(Around), R * Math.cos(Polar), -R * Math.sin(Polar) * Math.sin(Around)));
      }
      Rows.push(Row);
    }
    const Bottom = Vertex(0, -R, 0);
    for (let Step = 0; Step < Segments; Step++) {
      const Next = (Step + 1) % Segments;
      Indices.push(Top, Rows[0][Step], Rows[0][Next]);
      for (let Ring = 0; Ring < Rows.length - 1; Ring++) Quad(Rows[Ring][Step], Rows[Ring + 1][Step], Rows[Ring + 1][Next], Rows[Ring][Next]);
      Indices.push(Bottom, Rows[Rows.length - 1][Next], Rows[Rows.length - 1][Step]);
    }
  } else if (Shape === "torus") {
    const Major = P("Geometry Major Radius", 0.5), Minor = P("Geometry Minor Radius", 0.15);
    const Segments = Clamp(Math.round(P("Geometry Segments", 48)), 3, 48), Sides = Clamp(Math.round(P("Geometry Sides", 16)), 3, 20);
    const Grid = [];
    for (let Step = 0; Step < Segments; Step++) {
      const Around = (Step / Segments) * Math.PI * 2, Row = [];
      for (let Side = 0; Side < Sides; Side++) {
        const Tube = (Side / Sides) * Math.PI * 2, Reach = Major + Minor * Math.cos(Tube);
        Row.push(Vertex(Reach * Math.cos(Around), Minor * Math.sin(Tube), -Reach * Math.sin(Around)));
      }
      Grid.push(Row);
    }
    for (let Step = 0; Step < Segments; Step++)
      for (let Side = 0; Side < Sides; Side++) {
        const S1 = (Step + 1) % Segments, T1 = (Side + 1) % Sides;
        Quad(Grid[Step][Side], Grid[S1][Side], Grid[S1][T1], Grid[Step][T1]);
      }
  } else {
    // Cylinder and cone share one builder; the cone's top ring collapses to an apex.
    const Cone = Shape === "cone";
    const R = P("Geometry Radius", 0.5), H = P("Geometry Height", 1) / 2, Segments = Clamp(Math.round(P("Geometry Segments", 32)), 3, 64);
    const Caps = Read("Geometry Caps", true) !== false;
    const Ring = (Y, Radius) => Array.from({ length: Segments }, (_, Step) => {
      const Around = (Step / Segments) * Math.PI * 2;
      return Vertex(Radius * Math.cos(Around), Y, -Radius * Math.sin(Around));
    });
    const Low = Ring(-H, R);
    if (Cone) {
      const Apex = Vertex(0, H, 0);
      for (let Step = 0; Step < Segments; Step++) Indices.push(Low[Step], Low[(Step + 1) % Segments], Apex);
    } else {
      const High = Ring(H, R);
      for (let Step = 0; Step < Segments; Step++) Quad(Low[Step], Low[(Step + 1) % Segments], High[(Step + 1) % Segments], High[Step]);
      if (Caps) {
        // Separate cap rings keep the rim a hard edge so caps become their own islands.
        const Rim = Ring(H, R), Middle = Vertex(0, H, 0);
        for (let Step = 0; Step < Segments; Step++) Indices.push(Middle, Rim[Step], Rim[(Step + 1) % Segments]);
      }
    }
    if (Caps) {
      const Rim = Ring(-H, R), Middle = Vertex(0, -H, 0);
      for (let Step = 0; Step < Segments; Step++) Indices.push(Middle, Rim[(Step + 1) % Segments], Rim[Step]);
    }
  }
  return { Positions: new Float32Array(Positions), Indices: new Uint32Array(Indices) };
}

// Orthographic orbit: yaw about +y, then pitch about the camera x axis. Returns screen x/y (pixels) and depth (larger = nearer).
export function OrbitView({ Yaw = 0.7, Pitch = 0.45, Radius = 1, Width, Height, Zoom = 1 }) {
  const Cy = Math.cos(Yaw), Sy = Math.sin(Yaw), Cp = Math.cos(Pitch), Sp = Math.sin(Pitch);
  const Scale = (Math.min(Width, Height) * 0.4 * Zoom) / Math.max(1e-6, Radius);
  const Rotate = ([X, Y, Z]) => {
    const Rx = Cy * X + Sy * Z, Rz = -Sy * X + Cy * Z;
    return [Rx, Cp * Y - Sp * Rz, Sp * Y + Cp * Rz];
  };
  const Project = (Point) => {
    const [X, Y, Z] = Rotate(Point);
    return [Width / 2 + X * Scale, Height / 2 - Y * Scale, Z];
  };
  // World-space direction toward the viewer (for back-face culling and shading).
  const Toward = (() => {
    const [X, Y, Z] = [0, Sp, Cp];
    return [Cy * X - Sy * Z, Y, Sy * X + Cy * Z];
  })();
  // Screen point → world ray (origin far in front, along -Toward) for picking.
  const Unproject = (ScreenX, ScreenY) => {
    const X = (ScreenX - Width / 2) / Scale, Y = -(ScreenY - Height / 2) / Scale, Z = 10 * Radius;
    const Ry = Cp * Y + Sp * Z, Rz = -Sp * Y + Cp * Z;
    return { Origin: [Cy * X - Sy * Rz, Ry, Sy * X + Cy * Rz], Direction: Toward.map((Each) => -Each) };
  };
  return { Project, Toward, Unproject, Scale };
}

const Light = (() => {
  const L = [0.45, 0.8, 0.55], Length = Math.hypot(...L);
  return L.map((Each) => Each / Length);
})();
export const Normal = (A, B, C) => {
  const U = [B[0] - A[0], B[1] - A[1], B[2] - A[2]], V = [C[0] - A[0], C[1] - A[1], C[2] - A[2]];
  const N = [U[1] * V[2] - U[2] * V[1], U[2] * V[0] - U[0] * V[2], U[0] * V[1] - U[1] * V[0]];
  const Length = Math.hypot(...N) || 1;
  return N.map((Each) => Each / Length);
};
export const Shade = (N) => 0.38 + 0.62 * Math.max(0, N[0] * Light[0] + N[1] * Light[1] + N[2] * Light[2]);

// Painter's algorithm over polygons [{ Points: [[x,y,z]…], Paint(Brush, Screen, Light) }]. Back faces are skipped unless TwoSided.
export function PaintPolygons(Brush, View, Polygons, { TwoSided = false } = {}) {
  const Drawn = [];
  for (const Polygon of Polygons) {
    const N = Polygon.Normal || Normal(Polygon.Points[0], Polygon.Points[1], Polygon.Points[2]);
    const Facing = N[0] * View.Toward[0] + N[1] * View.Toward[1] + N[2] * View.Toward[2];
    if (Facing <= 0 && !TwoSided) continue;
    const Screen = Polygon.Points.map(View.Project);
    Drawn.push({ Polygon, Screen, Depth: Screen.reduce((Sum, Point) => Sum + Point[2], 0) / Screen.length, Lit: Shade(Facing < 0 ? N.map((Each) => -Each) : N) });
  }
  Drawn.sort((A, B) => A.Depth - B.Depth);
  for (const Each of Drawn) Each.Polygon.Paint(Brush, Each.Screen, Each.Lit);
  return Drawn;
}

// Draw `Image` (a square texture) into a screen triangle using the triangle's UVs (affine mapping).
export function TexturedTriangle(Brush, Image, Screen, Uv) {
  const [[X0, Y0], [X1, Y1], [X2, Y2]] = Screen;
  const Size = Image.width;
  const U0 = Uv[0] * Size, V0 = (1 - Uv[1]) * Size, U1 = Uv[2] * Size, V1 = (1 - Uv[3]) * Size, U2 = Uv[4] * Size, V2 = (1 - Uv[5]) * Size;
  const Det = (U1 - U0) * (V2 - V0) - (U2 - U0) * (V1 - V0);
  if (Math.abs(Det) < 1e-9) return;
  const A = ((X1 - X0) * (V2 - V0) - (X2 - X0) * (V1 - V0)) / Det;
  const B = ((Y1 - Y0) * (V2 - V0) - (Y2 - Y0) * (V1 - V0)) / Det;
  const C = ((X2 - X0) * (U1 - U0) - (X1 - X0) * (U2 - U0)) / Det;
  const D = ((Y2 - Y0) * (U1 - U0) - (Y1 - Y0) * (U2 - U0)) / Det;
  const E = X0 - A * U0 - C * V0, F = Y0 - B * U0 - D * V0;
  Brush.save();
  // Grow the clip by half a pixel so neighbouring triangles do not show hairline gaps.
  const Cx = (X0 + X1 + X2) / 3, Cy = (Y0 + Y1 + Y2) / 3;
  const Grow = ([X, Y]) => {
    const Dx = X - Cx, Dy = Y - Cy, L = Math.hypot(Dx, Dy) || 1;
    return [X + (Dx / L) * 0.6, Y + (Dy / L) * 0.6];
  };
  Brush.beginPath();
  [Screen[0], Screen[1], Screen[2]].map(Grow).forEach(([X, Y], Index) => (Index ? Brush.lineTo(X, Y) : Brush.moveTo(X, Y)));
  Brush.closePath();
  Brush.clip();
  Brush.setTransform(Brush.getTransform().multiply(new DOMMatrix([A, B, C, D, E, F])));
  Brush.drawImage(Image, 0, 0);
  Brush.restore();
}

// A UV checker texture with numbered cells, the usual way to read stretching and island orientation.
const Checkers = new Map();
export function CheckerTexture(Cells = 8) {
  if (Checkers.has(Cells)) return Checkers.get(Cells);
  const Canvas = document.createElement("canvas");
  Canvas.width = Canvas.height = 256;
  const Brush = Canvas.getContext("2d"), Cell = 256 / Cells;
  for (let Row = 0; Row < Cells; Row++)
    for (let Column = 0; Column < Cells; Column++) {
      Brush.fillStyle = (Row + Column) % 2 ? "#3a3a3a" : "#d6d6d6";
      Brush.fillRect(Column * Cell, Row * Cell, Cell, Cell);
      Brush.fillStyle = (Row + Column) % 2 ? "#9a9a9a" : "#4a4a4a";
      Brush.font = `${Math.max(7, Cell * 0.32)}px sans-serif`;
      Brush.textAlign = "center";
      Brush.textBaseline = "middle";
      Brush.fillText(String.fromCharCode(65 + Column) + (Cells - Row), Column * Cell + Cell / 2, Row * Cell + Cell / 2);
    }
  // A warm corner marks UV (0, 0) so flipped islands are obvious.
  Brush.fillStyle = "#e0a25a";
  Brush.fillRect(0, 256 - Cell / 3, Cell / 3, Cell / 3);
  Checkers.set(Cells, Canvas);
  return Canvas;
}

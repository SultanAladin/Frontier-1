// Divergence left after each editor's pressure projection, on the same buoyant plume.
// A: cleared every step, 36 Jacobi iterations, open top (p = 0 outside), closed sides/floor.
// B: kept between steps (warm start), 22 Jacobi iterations, every face closed (clamped fetch).
// Both use the same collocated discretisation: wide-stencil divergence/gradient, compact 7-point Laplacian.
const N = 48, H = 1, Count = N * N * N;
const At = (X, Y, Z) => X + N * (Y + N * Z);
function Run(Label, Iterations, Warm, OpenTop, Frames) {
  const U = new Float32Array(Count), V = new Float32Array(Count), W = new Float32Array(Count);
  let P = new Float32Array(Count), Q = new Float32Array(Count);
  const D = new Float32Array(Count);
  const Fetch = (F, X, Y, Z) => F[At(Math.min(N - 1, Math.max(0, X)), Math.min(N - 1, Math.max(0, Y)), Math.min(N - 1, Math.max(0, Z)))];
  const Divergence = () => {
    let Sum = 0;
    for (let Z = 0; Z < N; Z++) for (let Y = 0; Y < N; Y++) for (let X = 0; X < N; X++) {
      const Face = (F, A, B, Axis, Self) => {
        const Cx = X + A[0], Cy = Y + A[1], Cz = Z + A[2];
        if (Cx < 0 || Cy < 0 || Cz < 0 || Cx >= N || Cz >= N) return 0;
        if (Cy >= N) return OpenTop ? Self : 0;
        return F[At(Cx, Cy, Cz)];
      };
      const I = At(X, Y, Z);
      const Value = 0.5 * (Face(U, [1, 0, 0]) - Face(U, [-1, 0, 0]) + Face(V, [0, 1, 0], 0, 0, V[I]) - Face(V, [0, -1, 0]) + Face(W, [0, 0, 1]) - Face(W, [0, 0, -1])) / H;
      D[I] = Value;
      if (X > 2 && Y > 2 && Z > 2 && X < N - 3 && Y < N - 3 && Z < N - 3) Sum += Value * Value;
    }
    return Math.sqrt(Sum / Count);
  };
  const Pressure = (F, X, Y, Z, Centre) => {
    if (Y >= N) return OpenTop ? 0 : Centre;
    if (X < 0 || Y < 0 || Z < 0 || X >= N || Z >= N) return Centre;
    return F[At(X, Y, Z)];
  };
  const Rows = [];
  for (let Frame = 0; Frame < Frames; Frame++) {
    // Buoyant column: heat near the floor pushes gas up, plus a slow swirl so the field changes every frame.
    for (let Z = 0; Z < N; Z++) for (let Y = 0; Y < N; Y++) for (let X = 0; X < N; X++) {
      const Dx = X - N / 2, Dz = Z - N / 2, R2 = Dx * Dx + Dz * Dz;
      const I = At(X, Y, Z);
      V[I] += 0.08 * Math.exp(-R2 / 18) * Math.exp(-Y / 14);
      U[I] += 0.01 * Math.sin(0.3 * Y + Frame * 0.2) * Math.exp(-R2 / 40);
    }
    const Before = Divergence();
    if (!Warm) P.fill(0);
    for (let K = 0; K < Iterations; K++) {
      for (let Z = 0; Z < N; Z++) for (let Y = 0; Y < N; Y++) for (let X = 0; X < N; X++) {
        const I = At(X, Y, Z), C = P[I];
        Q[I] = (Pressure(P, X + 1, Y, Z, C) + Pressure(P, X - 1, Y, Z, C) + Pressure(P, X, Y + 1, Z, C) + Pressure(P, X, Y - 1, Z, C) + Pressure(P, X, Y, Z + 1, C) + Pressure(P, X, Y, Z - 1, C) - D[I] * H * H) / 6;
      }
      [P, Q] = [Q, P];
    }
    for (let Z = 0; Z < N; Z++) for (let Y = 0; Y < N; Y++) for (let X = 0; X < N; X++) {
      const I = At(X, Y, Z), C = P[I];
      U[I] -= 0.5 * (Pressure(P, X + 1, Y, Z, C) - Pressure(P, X - 1, Y, Z, C)) / H;
      V[I] -= 0.5 * (Pressure(P, X, Y + 1, Z, C) - Pressure(P, X, Y - 1, Z, C)) / H;
      W[I] -= 0.5 * (Pressure(P, X, Y, Z + 1, C) - Pressure(P, X, Y, Z - 1, C)) / H;
      if (Y === 0 && V[I] < 0) V[I] = 0;
      if (!OpenTop && Y === N - 1 && V[I] > 0) V[I] = 0;
    }
    const After = Divergence();
    let Mean = 0; for (let I = 0; I < Count; I++) Mean += P[I]; Mean /= Count;
    Rows.push({ Frame, Before, After, Left: After / Before, Mean });
    for (let I = 0; I < Count; I++) { U[I] *= 0.9; V[I] *= 0.9; W[I] *= 0.9; }
  }
  return { Label, Rows };
}
const Frames = 30;
for (const Case of [Run("A  cold 36, open top", 36, false, true, Frames), Run("B  warm 22, closed", 22, true, false, Frames), Run("A' cold 36, closed", 36, false, false, Frames), Run("B' warm 22, open top", 22, true, true, Frames)]) {
  const Pick = [0, 4, 9, 19, 29].map((F) => Case.Rows[F]);
  console.log(Case.Label.padEnd(24), Pick.map((R) => `f${R.Frame + 1}:${(100 * R.Left).toFixed(1)}%`).join("  "), " mean p", Case.Rows[Frames - 1].Mean.toFixed(3));
}

// Divergence left after projection: the two original schemes and the combined one, same plume, 30 steps.
import fs from "node:fs";
const Source = fs.readFileSync(new URL("./ProjectionTest.mjs", import.meta.url), "utf8").split("const Frames = 30;")[0];
const Run = new Function(Source + "; return Run;")();
for (const [Label, Iterations, Warm] of [["A original  cold 36", 36, false], ["B original  warm 22", 22, true], ["Combined    warm 36", 36, true]]) {
  const Rows = Run(Label, Iterations, Warm, true, 30).Rows;
  console.log(Label.padEnd(22), [0, 9, 29].map((F) => `f${F + 1}:${(100 * Rows[F].Left).toFixed(1)}%`).join("  "));
}

// Parses every /* glsl */ template literal in the GPU paint modules with a
// real GLSL grammar (@shaderfrog/glsl-parser) so shader typos fail here
// instead of in a browser console. Template interpolations are replaced
// with inert stand-ins (numeric constants and the shared depth packer).
import { readFileSync } from "node:fs";
import { parse } from "@shaderfrog/glsl-parser";

const FILES = [
  "src/paintGpuKit.js",
  "src/screenPaintGpu.js",
  "src/maskPaintGpu.js",
  "src/digitalPaintGpu.js",
];

let checked = 0;
for (const file of FILES) {
  const source = readFileSync(new URL(`../${file}`, import.meta.url), "utf8");
  const packer =
    /const PACK_DEPTH_GLSL = \/\* glsl \*\/ `([\s\S]*?)`;/.exec(source)?.[1] ?? "";
  const blocks = [...source.matchAll(/\/\* glsl \*\/ `([\s\S]*?)`/g)].map(
    (m) => m[1],
  );
  for (const raw of blocks) {
    const glsl = raw
      .replaceAll("${PACK_DEPTH_GLSL}", packer)
      .replace(/\$\{[^}]+\}/g, "0");
    // The depth pass is raw GLSL3 (in/out, layout locations); the rest is
    // GLSL1 that three transpiles. Parse both flavours as ES 3.0 where the
    // source declares it, ES 1.0 otherwise.
    try {
      parse(glsl);
    } catch (error) {
      console.error(`GLSL parse failed in ${file}:\n${glsl}\n${error.message}`);
      process.exit(1);
    }
    checked++;
  }
}
console.log(`verify-glsl: ${checked} shader blocks parse OK`);

// ExportSamples.mjs — writes the parity corpus to Samples/ as the TOML the engine reads.
//
//     node Experimental/Fluid/ExportSamples.mjs            # write
//     node Experimental/Fluid/ExportSamples.mjs --check    # exit 1 if what is committed is stale
//
// The committed files are what the native round-trip check reads, so they are the contract between the two
// emitters. Keeping them in version control rather than generating them during the check is deliberate: a
// check that regenerates its own fixture cannot notice the fixture changing.
import { mkdirSync, readFileSync, writeFileSync, existsSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { ConstructSampleScene, SampleIdentities } from "./src/SampleScenes.js";
import { WriteSceneToml, SceneExtension } from "./src/SceneTomlCodec.js";

const Here = dirname(fileURLToPath(import.meta.url));
const Folder = join(Here, "Samples");
const Checking = process.argv.includes("--check");
mkdirSync(Folder, { recursive: true });

let Stale = 0;
for (const Identity of SampleIdentities()) {
  const Path = join(Folder, Identity + SceneExtension);
  const Composed = WriteSceneToml(ConstructSampleScene(Identity));
  if (Checking) {
    if (!existsSync(Path) || readFileSync(Path, "utf8") !== Composed) {
      console.log(`STALE: Samples/${Identity}${SceneExtension}`);
      Stale += 1;
    }
  } else writeFileSync(Path, Composed);
}

if (Checking && Stale) {
  console.log("       run: node Experimental/Fluid/ExportSamples.mjs");
  process.exit(1);
}
console.log(
  Checking
    ? `the ${SampleIdentities().length} committed samples are current`
    : `wrote ${SampleIdentities().length} samples to Experimental/Fluid/Samples`,
);

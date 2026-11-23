import authoringModuleSource from "./MaterialSpecification.js?raw";
import textureSourceCode from "./textureSource.js?raw";
import fieldSpecificationSource from "./FieldSpecification.js?raw";
import fieldProjectionSource from "./FieldProjection.js?raw";
import { ExportPbrAtlas } from "./FieldProjection.js";
import patternGeometrySource from "./patternGeometry.js?raw";
import geometricModuleSource from "./geometricConstructions.js?raw";
import heritageModuleSource from "./heritagePatterns.js?raw";
import weaveModuleSource from "./patternWeaves.js?raw";
import ornamentDrawingSource from "./ornamentDrawing.js?raw";
import ornamentalSource from "./ornamentalConstructions.js?raw";
import referenceModuleSource from "./referencePatterns.js?raw";
import rugDesignModuleSource from "./rugDesigns.js?raw";
import rugCompositionModuleSource from "./rugCompositions.js?raw";
import stitchModuleSource from "./patternStitches.js?raw";
import textileModuleSource from "./patternLibrary.js?raw";
import collectionModuleSource from "./patternCollections.js?raw";
import patternImportSource from "./patternImport.js?raw";
import patternDocumentSource from "./patternDocument.js?raw";
import leatherSourceModule from "./leatherSource.js?raw";
import patternRuntimeSource from "./patternRuntime.js?raw";
import recipeModuleSource from "./materialProfiles.js?raw";
import materialModuleSource from "./materials.js?raw";
import kernelModuleSource from "./surfaceKernels.js?raw";
import botanicalModuleSource from "./botanicalKernels.js?raw";
import architectureModuleSource from "./architecturalKernels.js?raw";
import sandModuleSource from "./sandKernels.js?raw";
const shaderSource =
  geometricModuleSource +
  "\n" +
  heritageModuleSource +
  "\n" +
  weaveModuleSource +
  "\n" +
  ornamentDrawingSource +
  "\n" +
  referenceModuleSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  ornamentalSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  stitchModuleSource +
  "\n" +
  rugDesignModuleSource +
  "\n" +
  rugCompositionModuleSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  textileModuleSource
    .replace(/^import[\s\S]*?;\s*/gm, "")
    .replace(/^export \{[^}]*\} from [^;]*;\s*/gm, "") +
  "\n" +
  collectionModuleSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  patternImportSource +
  "\n" +
  patternGeometrySource +
  "\n" +
  patternDocumentSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  leatherSourceModule +
  "\n" +
  patternRuntimeSource.replace(/^import[\s\S]*?;\s*/gm, "") +
  "\n" +
  recipeModuleSource +
  "\n" +
  kernelModuleSource +
  "\n" +
  botanicalModuleSource +
  "\n" +
  architectureModuleSource +
  "\n" +
  sandModuleSource +
  "\n" +
  "const { ResolveAuthoringParameters, PbrChannels, ChannelAddress, ReadMaterialParameter, ResolveAddress } = (() => {\n" +
  [textureSourceCode, fieldSpecificationSource, authoringModuleSource].map(Source => Source.replace(/^import[\s\S]*?;\s*/gm, "").replace(/\bexport /g, "")).join("\n") +
  "\nreturn { ResolveAuthoringParameters, PbrChannels, ChannelAddress, ReadMaterialParameter, ResolveAddress }; })();\n" +
  materialModuleSource.slice(
    materialModuleSource.indexOf("export function normalizeMaterial("),
    materialModuleSource.indexOf("export function createBallGeometry("),
  );

export function exportMaterialJSON(params) {
  return JSON.stringify(
    { schema: "alloy.material.v6", version: 6, material: params },
    null,
    2,
  );
}
export function exportMaterialShader(params) {
  const Atlas = ExportPbrAtlas(params);
  const Enumeration = fieldProjectionSource.slice(fieldProjectionSource.indexOf("export function EnumerateMaterialFields"), fieldProjectionSource.indexOf("function FieldStamp")).replace(/\bexport /g, "");
  const Projection = fieldProjectionSource.slice(fieldProjectionSource.indexOf("export function AttachPbrFields"), fieldProjectionSource.indexOf("export function ExportPbrAtlas")).replace(/\bexport /g, "").replace(/    const Stamp = FieldStamp[\s\S]*?    const Existing =/, "    Atlas.image.data = new Float32Array(PackedAtlas.Pixels); Atlas.needsUpdate = true;\n    const Existing =");
  const FieldRuntime = `const AttachPbrFields = (() => { const PackedAtlas = ${JSON.stringify(Atlas)};\n${Enumeration}\n${Projection}\nreturn AttachPbrFields; })();`;
  return `import * as THREE from 'three';\n\nexport const preset = ${JSON.stringify(params, null, 2)};\n\n${shaderSource}\n\n${FieldRuntime}\n\nexport default createMaterial(preset);\n`;
}

import { AttachPbrFields } from "./FieldProjection.js";
import { DeclareMaterialAuthoring, DeclareCodedField, ResolveAuthoringParameters } from "./MaterialSpecification.js";
import {
  attachSurfaceSources,
  patternGLSL,
  patternColorGLSL,
} from "./patternRuntime.js";
import * as THREE from "three";
import { botanicalCatalog } from "./botanicalCatalog.js";
import { botanicalGLSL, botanicalColor } from "./botanicalKernels.js";
import { boundMaterial, supportsMetalScratches } from "./materialProfiles.js";
import { expandedCatalog } from "./catalogExpansion.js";
import { architectureCatalog } from "./architectureCatalog.js";
import {
  architecturalGLSL,
  architecturalColor,
} from "./architecturalKernels.js";
import { extendedSurfaceGLSL, extendedSurfaceColor } from "./surfaceKernels.js";
import { sandGLSL, sandColor } from "./sandKernels.js";
import { granularCatalog } from "./granularCatalog.js";
import { normalizeSandLayers, sandLayerSeed } from "./materialProfiles.js";

export const materials = [
  {
    id: "racing-green",
    name: "Racing Green",
    category: "Paint",
    label: "MULTILAYER METALLIC",
    color: "#345f42",
    roughness: 0.24,
    metalness: 0.87,
    depth: 0.65,
    flakes: 0.64,
    size: 0.45,
    coat: 0.94,
    coatRoughness: 0.09,
    type: 0,
    colors: ["#c6db89", "#59bea3", "#ab8bc9", "#d17b83", "#82a3d5"],
    description: "A classic, with another dimension.",
  },
  {
    id: "midnight-amethyst",
    name: "Midnight Amethyst",
    category: "Paint",
    label: "COLOR-SHIFT METALLIC",
    color: "#51406f",
    roughness: 0.22,
    metalness: 0.85,
    depth: 0.72,
    flakes: 0.72,
    size: 0.42,
    coat: 1,
    coatRoughness: 0.07,
    type: 0,
    colors: ["#bc89e8", "#567edd", "#e980b6", "#66d2c0", "#dbc595"],
    description: "Deep violet. Unexpected brilliance.",
  },
  {
    id: "liquid-silver",
    name: "Liquid Silver",
    category: "Metal",
    label: "POLISHED ALUMINUM",
    color: "#adb3b0",
    roughness: 0.2,
    metalness: 1,
    depth: 0.4,
    flakes: 0.18,
    size: 0.3,
    coat: 0.4,
    coatRoughness: 0.14,
    type: 1,
    colors: ["#dadcdb", "#b3bac3", "#e7ded0"],
    description: "Precision, reflected in every curve.",
  },
  {
    id: "candy-crimson",
    name: "Candy Crimson",
    category: "Paint",
    label: "CANDY METALLIC",
    color: "#8b202a",
    roughness: 0.21,
    metalness: 0.81,
    depth: 0.82,
    flakes: 0.55,
    size: 0.35,
    coat: 1,
    coatRoughness: 0.06,
    type: 0,
    colors: ["#ee685e", "#eab870", "#c985c8", "#8da1e3", "#f2b6b0"],
    description: "Rich color. Remarkable depth.",
  },
  {
    id: "carbon-ceramic",
    name: "Carbon Ceramic",
    category: "Ceramic",
    label: "SINTERED CARBON CERAMIC",
    color: "#4e5050",
    roughness: 0.73,
    metalness: 0.35,
    depth: 0.25,
    flakes: 0.45,
    size: 0.35,
    coat: 0.1,
    coatRoughness: 0.5,
    type: 2,
    colors: ["#898b8b", "#555657", "#b2b1aa"],
    description: "Engineered for the extreme.",
  },
  {
    id: "brushed-titanium",
    name: "Brushed Titanium",
    category: "Metal",
    label: "DIRECTIONAL BRUSHED METAL",
    color: "#858681",
    roughness: 0.38,
    metalness: 1,
    depth: 0.25,
    flakes: 0.15,
    size: 0.5,
    coat: 0.2,
    coatRoughness: 0.25,
    type: 1,
    colors: ["#a7a7a1", "#dedbd0", "#929792"],
    description: "Industrial character. Refined finish.",
  },
  {
    id: "performance-rubber",
    name: "Performance Rubber",
    category: "Rubber",
    label: "MICROTEXTURED ELASTOMER",
    color: "#262927",
    roughness: 0.87,
    metalness: 0,
    depth: 0.25,
    flakes: 0.12,
    size: 0.45,
    coat: 0,
    coatRoughness: 0.5,
    type: 3,
    colors: ["#414641", "#2b302b"],
    description: "Grip you can almost feel.",
  },
  {
    id: "woven-fabric",
    name: "Woven Fabric",
    category: "Fabric",
    label: "TECHNICAL TWILL WEAVE",
    color: "#77766c",
    roughness: 0.93,
    metalness: 0,
    depth: 0.5,
    flakes: 0.35,
    size: 0.5,
    coat: 0,
    coatRoughness: 0.8,
    type: 4,
    colors: ["#9f9b87", "#666a61"],
    description: "A tactile touch of craftsmanship.",
  },
  {
    id: "crystal-glass",
    name: "Crystal Glass",
    category: "Glass",
    label: "OPTICAL AUTOMOTIVE GLASS",
    color: "#b1c8bc",
    roughness: 0.04,
    metalness: 0,
    depth: 0.75,
    flakes: 0,
    size: 0.3,
    coat: 1,
    coatRoughness: 0.03,
    type: 5,
    colors: ["#c9e5dd", "#a8d4ca"],
    description: "Clarity, without compromise.",
  },
  {
    id: "satin-pearl",
    name: "Satin Pearl",
    category: "Paint",
    label: "PEARLESCENT FINISH",
    color: "#dddace",
    roughness: 0.32,
    metalness: 0.45,
    depth: 0.55,
    flakes: 0.42,
    size: 0.23,
    coat: 0.9,
    coatRoughness: 0.15,
    type: 0,
    colors: ["#edddaa", "#cae0d4", "#ddbdda", "#b0cfeb", "#f4eee0"],
    description: "Quiet luxury. Subtle radiance.",
  },
  {
    id: "brake-disc",
    name: "Brake Disc",
    category: "Metal",
    label: "MACHINED CAST IRON",
    color: "#898c88",
    roughness: 0.42,
    metalness: 0.96,
    depth: 0.2,
    flakes: 0.12,
    size: 0.28,
    coat: 0.08,
    coatRoughness: 0.35,
    type: 6,
    colors: ["#b3b4ae", "#757876"],
    description: "Purposeful down to the last micron.",
  },
  {
    id: "carbon-fiber",
    name: "Carbon Fiber",
    category: "Fabric",
    label: "2 × 2 CARBON TWILL",
    detailScale: 14,
    weaveRelief: 0.7,
    anisotropy: 0.85,
    color: "#717379",
    roughness: 0.3,
    metalness: 0.8,
    depth: 0.65,
    flakes: 0.25,
    size: 0.7,
    coat: 0.9,
    coatRoughness: 0.12,
    type: 7,
    colors: ["#68716a", "#252d27"],
    description: "Lightweight. Uncompromising.",
  },
  {
    id: "piano-black",
    name: "Piano Black",
    category: "Plastic",
    label: "HIGH-GLOSS POLYMER",
    color: "#101115",
    roughness: 0.17,
    metalness: 0,
    depth: 0.3,
    flakes: 0,
    size: 0.4,
    coat: 0.8,
    coatRoughness: 0.07,
    type: 8,
    colors: ["#26272b"],
    ior: 1.58,
    description: "A polished finish. Perfectly understated.",
  },
  {
    id: "grained-abs",
    name: "Grained ABS",
    category: "Plastic",
    label: "INJECTION-MOLDED GRAIN",
    color: "#353638",
    roughness: 0.62,
    metalness: 0,
    depth: 0.35,
    flakes: 0,
    size: 0.4,
    coat: 0.08,
    coatRoughness: 0.4,
    type: 9,
    colors: ["#3e3f41"],
    detailScale: 85,
    grain: 0.85,
    ior: 1.54,
    description: "Tactile, durable, purposefully practical.",
  },
  {
    id: "worn-polymer",
    name: "Worn Polymer",
    category: "Plastic",
    label: "WEATHERED AUTOMOTIVE TRIM",
    color: "#44474c",
    roughness: 0.56,
    metalness: 0,
    depth: 0.4,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.5,
    type: 10,
    colors: ["#53575c"],
    grain: 0.35,
    wear: 0.7,
    detailScale: 110,
    ior: 1.52,
    description: "A little history in every surface.",
  },
  {
    id: "suede-microfiber",
    name: "Suede Microfiber",
    category: "Fabric",
    label: "BRUSHED MICROFIBER",
    color: "#77716b",
    roughness: 0.94,
    metalness: 0,
    depth: 0.3,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.8,
    type: 4,
    colors: ["#aaa297"],
    fabricMode: 1,
    sheen: 1,
    sheenColor: "#cdc6bb",
    sheenRoughness: 0.75,
    fuzz: 0.85,
    fuzzLength: 0.55,
    description: "Soft to the eye. Rich in the details.",
  },
  {
    id: "midnight-velvet",
    name: "Midnight Velvet",
    category: "Fabric",
    label: "DENSE CUT-PILE TEXTILE",
    color: "#213842",
    roughness: 0.86,
    metalness: 0,
    depth: 0.3,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.8,
    type: 4,
    colors: ["#547780"],
    fabricMode: 2,
    sheen: 1,
    sheenColor: "#89a7ba",
    sheenRoughness: 0.35,
    fuzz: 1,
    fuzzLength: 1.1,
    description: "Deep color with a soft, luminous edge.",
  },

  {
    id: "natural-cotton",
    name: "Natural Cotton",
    category: "Fabric",
    label: "SOFT PLAIN-WEAVE COTTON",
    color: "#c7bfae",
    roughness: 0.9,
    metalness: 0,
    depth: 0.35,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.6,
    type: 4,
    colors: ["#dfd6c3"],
    fabricMode: 3,
    detailScale: 64,
    weaveAngle: 0,
    weaveRelief: 0.72,
    fiberDetail: 18,
    sheen: 0.48,
    sheenColor: "#e5dfcf",
    sheenRoughness: 0.8,
    fuzz: 0.62,
    fuzzLength: 0.42,
    anisotropy: 0.15,
    description: "An honest weave. A naturally soft hand.",
  },
  {
    id: "indigo-denim",
    name: "Indigo Denim",
    category: "Fabric",
    label: "DYED COTTON TWILL",
    color: "#283d57",
    roughness: 0.85,
    metalness: 0,
    depth: 0.45,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.7,
    type: 4,
    colors: ["#697785", "#253c5a"],
    fabricMode: 0,
    detailScale: 150,
    weaveAngle: 0,
    weaveRelief: 0.9,
    sheen: 0.6,
    sheenColor: "#a7b7c7",
    sheenRoughness: 0.65,
    fuzz: 0.5,
    fuzzLength: 0.5,
    description: "Indigo yarn. Familiar character.",
  },
  {
    id: "champagne-silk",
    name: "Champagne Silk",
    category: "Fabric",
    label: "FIVE-HARNESS SILK SATIN",
    color: "#b99c78",
    roughness: 0.17,
    metalness: 0,
    depth: 0.2,
    flakes: 0,
    size: 0.35,
    coat: 0,
    coatRoughness: 0.25,
    type: 4,
    colors: ["#dac6a2"],
    fabricMode: 4,
    detailScale: 75,
    weaveAngle: 0,
    weaveRelief: 0.24,
    fiberDetail: 22,
    anisotropy: 0.65,
    ior: 1.57,
    specular: 1,
    sheen: 0.65,
    sheenColor: "#f5e8d2",
    sheenRoughness: 0.28,
    fuzz: 0.035,
    fuzzLength: 0.065,
    description: "Long floating yarns. Liquid light.",
  },
  {
    id: "midnight-satin",
    name: "Midnight Satin",
    category: "Fabric",
    label: "LUSTROUS SATIN WEAVE",
    color: "#30374e",
    roughness: 0.29,
    metalness: 0,
    depth: 0.2,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.2,
    type: 4,
    colors: ["#7b86a2"],
    fabricMode: 4,
    detailScale: 66,
    weaveAngle: 25,
    weaveRelief: 0.32,
    fiberDetail: 20,
    anisotropy: 0.8,
    ior: 1.53,
    sheen: 0.85,
    sheenColor: "#b6c2e5",
    sheenRoughness: 0.34,
    fuzz: 0.07,
    fuzzLength: 0.12,
    description: "Soft folds. A quiet, directional lustre.",
  },
  {
    id: "cognac-leather",
    name: "Cognac Leather",
    category: "Leather",
    label: "FULL-GRAIN AUTOMOTIVE HIDE",
    color: "#713b23",
    roughness: 0.48,
    metalness: 0,
    depth: 0.5,
    flakes: 0,
    size: 0.4,
    coat: 0.2,
    coatRoughness: 0.32,
    type: 11,
    colors: ["#9b643d"],
    detailScale: 43,
    grain: 0.75,
    wear: 0.12,
    sheen: 0.18,
    sheenColor: "#b68866",
    sheenRoughness: 0.7,
    ior: 1.48,
    description: "Irregular grain. Warm, enduring character.",
  },
  {
    id: "nappa-leather",
    name: "Nappa Leather",
    category: "Leather",
    label: "SOFT FINE-GRAIN NAPPA",
    color: "#252429",
    roughness: 0.54,
    metalness: 0,
    depth: 0.35,
    flakes: 0,
    size: 0.4,
    coat: 0.12,
    coatRoughness: 0.4,
    type: 11,
    colors: ["#55535c"],
    detailScale: 72,
    grain: 0.4,
    wear: 0.05,
    sheen: 0.25,
    sheenColor: "#777780",
    sheenRoughness: 0.8,
    ior: 1.48,
    description: "Supple, refined, beautifully understated.",
  },
  {
    category: "Fabric",
    type: 4,
    fabricMode: 0,
    metalness: 0,
    depth: 0.3,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.5,
    colors: ["#bcb09a"],
    detailScale: 38,
    weaveAngle: 0,
    weaveRelief: 0.6,
    fiberDetail: 16,
    sheen: 0.45,
    sheenColor: "#a79e8c",
    sheenRoughness: 0.7,
    fuzzLength: 0.5,
    anisotropy: 0.2,
    description: "Two yarn colors. A distinct construction.",
    id: "plain-linen",
    name: "Plain Linen",
    label: "PLAIN \u00b7 1 OVER / 1 UNDER",
    color: "#bcb09a",
    warpColor: "#c9bfa9",
    weftColor: "#a79e8c",
    textileClass: "linen",
    weavePattern: "plain",
    roughness: 0.94,
    fuzz: 0.22,
  },
  {
    category: "Fabric",
    type: 4,
    fabricMode: 0,
    metalness: 0,
    depth: 0.3,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.5,
    colors: ["#b7b2a2"],
    detailScale: 38,
    weaveAngle: 0,
    weaveRelief: 0.6,
    fiberDetail: 16,
    sheen: 0.45,
    sheenColor: "#969483",
    sheenRoughness: 0.7,
    fuzzLength: 0.5,
    anisotropy: 0.2,
    description: "Two yarn colors. A distinct construction.",
    id: "basket-cotton",
    name: "Basket Cotton",
    label: "BASKET \u00b7 2 \u00d7 2",
    color: "#b7b2a2",
    warpColor: "#d4ccb7",
    weftColor: "#969483",
    textileClass: "cotton",
    weavePattern: "basket",
    roughness: 0.86,
    fuzz: 0.4,
  },
  {
    category: "Fabric",
    type: 4,
    fabricMode: 0,
    metalness: 0,
    depth: 0.3,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.5,
    colors: ["#b0957b"],
    detailScale: 38,
    weaveAngle: 0,
    weaveRelief: 0.6,
    fiberDetail: 16,
    sheen: 0.45,
    sheenColor: "#6f5a4b",
    sheenRoughness: 0.7,
    fuzzLength: 0.5,
    anisotropy: 0.2,
    description: "Two yarn colors. A distinct construction.",
    id: "ribbed-cotton",
    name: "Ribbed Cotton",
    label: "RIB \u00b7 PAIRED WARP FLOATS",
    color: "#b0957b",
    warpColor: "#b0957b",
    weftColor: "#6f5a4b",
    textileClass: "cotton",
    weavePattern: "rib",
    roughness: 0.89,
    fuzz: 0.4,
  },
  {
    category: "Fabric",
    type: 4,
    fabricMode: 0,
    metalness: 0,
    depth: 0.3,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.5,
    colors: ["#7d98ae"],
    detailScale: 38,
    weaveAngle: 0,
    weaveRelief: 0.6,
    fiberDetail: 16,
    sheen: 0.45,
    sheenColor: "#e2ded1",
    sheenRoughness: 0.7,
    fuzzLength: 0.5,
    anisotropy: 0.2,
    description: "Two yarn colors. A distinct construction.",
    id: "oxford-weave",
    name: "Oxford Weave",
    label: "OXFORD \u00b7 2 \u00d7 1 BASKET",
    color: "#7d98ae",
    warpColor: "#49667f",
    weftColor: "#e2ded1",
    textileClass: "cotton",
    weavePattern: "oxford",
    roughness: 0.85,
    fuzz: 0.3,
  },
  {
    category: "Fabric",
    type: 4,
    fabricMode: 0,
    metalness: 0,
    depth: 0.3,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.5,
    colors: ["#7e817a"],
    detailScale: 38,
    weaveAngle: 0,
    weaveRelief: 0.6,
    fiberDetail: 16,
    sheen: 0.45,
    sheenColor: "#c6c4b5",
    sheenRoughness: 0.7,
    fuzzLength: 0.5,
    anisotropy: 0.2,
    description: "Two yarn colors. A distinct construction.",
    id: "herringbone-wool",
    name: "Herringbone Wool",
    label: "HERRINGBONE \u00b7 BROKEN TWILL",
    color: "#7e817a",
    warpColor: "#777e71",
    weftColor: "#c6c4b5",
    textileClass: "wool",
    weavePattern: "herringbone",
    roughness: 0.91,
    fuzz: 0.65,
  },
  {
    category: "Fabric",
    type: 4,
    fabricMode: 0,
    metalness: 0,
    depth: 0.3,
    flakes: 0,
    size: 0.4,
    coat: 0,
    coatRoughness: 0.5,
    colors: ["#aca996"],
    detailScale: 38,
    weaveAngle: 0,
    weaveRelief: 0.6,
    fiberDetail: 16,
    sheen: 0.45,
    sheenColor: "#ddd5bd",
    sheenRoughness: 0.7,
    fuzzLength: 0.5,
    anisotropy: 0.2,
    description: "Two yarn colors. A distinct construction.",
    id: "houndstooth-wool",
    name: "Houndstooth Wool",
    label: "HOUNDSTOOTH \u00b7 COLOR TWILL",
    color: "#aca996",
    warpColor: "#292a2c",
    weftColor: "#ddd5bd",
    textileClass: "wool",
    weavePattern: "houndstooth",
    roughness: 0.92,
    fuzz: 0.55,
  },
  {
    id: "aurora-flip",
    name: "Aurora Flip",
    category: "Paint",
    type: 0,
    label: "THIN-FILM IRIDESCENT PAINT",
    color: "#183441",
    roughness: 0.2,
    metalness: 0.85,
    depth: 0.4,
    flakes: 0.24,
    size: 0.3,
    coat: 0.95,
    coatRoughness: 0.07,
    colors: ["#acccd1", "#9985bc", "#c2ab77"],
    paintEffect: "iridescent",
    iridescence: 1,
    iridescenceIOR: 1.38,
    filmThickness: 390,
    filmVariation: 55,
    description: "Petrol blue. Violet at the edges.",
  },
  {
    id: "sunset-prism",
    name: "Sunset Prism",
    category: "Paint",
    type: 0,
    label: "THIN-FILM IRIDESCENT PAINT",
    color: "#542b36",
    roughness: 0.2,
    metalness: 0.85,
    depth: 0.4,
    flakes: 0.24,
    size: 0.3,
    coat: 0.95,
    coatRoughness: 0.07,
    colors: ["#acccd1", "#9985bc", "#c2ab77"],
    paintEffect: "iridescent",
    iridescence: 1,
    iridescenceIOR: 1.46,
    filmThickness: 590,
    filmVariation: 55,
    description: "Copper, rose and a shifting violet sheen.",
  },
  {
    id: "opal-pearl",
    name: "Opal Pearl",
    category: "Paint",
    type: 0,
    label: "THIN-FILM IRIDESCENT PAINT",
    color: "#c7c9bd",
    roughness: 0.2,
    metalness: 0.45,
    depth: 0.4,
    flakes: 0.24,
    size: 0.3,
    coat: 0.95,
    coatRoughness: 0.07,
    colors: ["#acccd1", "#9985bc", "#c2ab77"],
    paintEffect: "iridescent",
    iridescence: 1,
    iridescenceIOR: 1.28,
    filmThickness: 270,
    filmVariation: 55,
    description: "A pearl finish with a delicate interference glow.",
  },
  {
    id: "woodland-camouflage-fabric", name: "Woodland Camouflage Fabric", category: "Fabric", label: "PROCEDURAL CAMOUFLAGE · WOVEN CLOTH",
    type: 4, fabricMode: 0, weavePattern: "twill", color: "#566044", warpColor: "#787965", weftColor: "#62684e", colors: ["#b5a477", "#566044", "#514334", "#232921"],
    roughness: .86, metalness: 0, depth: .35, coat: 0, detailScale: 68, grain: .7, sheen: .2, sheenRoughness: .8, camouflage: true,
    description: "Editable woodland-style camouflage over woven cloth. Procedural colour, textile relief and soft fibre sheen.",
  },
  {
    id: "camouflage-ballistic-polymer", name: "Camouflage Ballistic Polymer", category: "Plastic", label: "MATTE COMPOSITE LOOK · VISUAL MATERIAL ONLY",
    type: 9, color: "#566044", colors: ["#b5a477", "#566044", "#514334", "#232921"], roughness: .68, metalness: 0, depth: .2,
    coat: .04, coatRoughness: .55, detailScale: 110, grain: .45, ior: 1.53, camouflage: true,
    description: "Camouflage on a matte, lightly textured ballistic-polymer-style surface. Appearance only: no ballistic rating or performance claim.",
  },
  ...expandedCatalog(),
  ...architectureCatalog(),
  ...botanicalCatalog(),
  ...granularCatalog(),
].map(normalizeMaterial).flatMap(Parameters => Parameters.camouflage ? [Parameters, { ...Parameters, id: `${Parameters.id}-smooth`, name: `${Parameters.name} · Smooth`, camouflageStyle: "camouflage-smooth" }] : [Parameters]).map(Parameters =>
{
  if (!Parameters.camouflage) return Parameters;
  const Declared = { ...Parameters, Authoring: DeclareMaterialAuthoring(Parameters) };
  const Result = DeclareCodedField(Declared, Parameters.camouflageStyle || "camouflage", Parameters.camouflageStyle ? "Smooth woodland camouflage" : "Woodland camouflage", "Camouflage");
  Result.Authoring.Sources.color = { Mode: "field", Direct: true, Generator: Result.Authoring.Generators.at(-1).Identity };
  return Result;
});

export function normalizeMaterial(input) {
  const p = input || {};
  const isFabric = p.type === 4;
  const colors = (
    Array.isArray(p.colors) && p.colors.length ? p.colors : ["#e0dccf"]
  ).slice(0, 12);
  const Normalized = boundMaterial({
    color: "#b5b0a2",
    type: 0,
    roughness: 0.3,
    metalness: 0,
    coat: 0,
    coatRoughness: 0.15,
    depth: 0.5,
    flakes: 0,
    tertiaryColor: "#282b30",
    ribDepth: 0.018,
    ribProfile: 0,
    sheetFinish: 0,
    sheetOxidation: 0,
    sheetSpangle: 0,
    grainSize: 0.65,
    grainSizeVariation: 0.5,
    grainRoughness: 0.6,
    grainRoughnessVariation: 0.3,
    grainSpecular: 0.8,
    grainSpecularVariation: 0.35,
    grainIOR: 1.544,
    grainRoundness: 0.45,
    grainTilt: 0.32,
    grainRelief: 0.85,
    sandCoverage: 1,
    sandScale: 1,
    sandBandFrequency: 1.5,
    sandBandWarp: 0.2,
    sandBandAngle: 0,
    groutWidth: 0.045,
    tileVariation: 0.1,
    tileStagger: 0,
    veinWidth: 0.035,
    poreDensity: 0.5,
    wallMode: 0,
    leafAspect: 0.4,
    bladeWidth: 0.13,
    bladeLean: 0.4,
    surfaceSeed: 17,
    metalScratches: false,
    scratchAngle: 25,
    scratchDensity: supportsMetalScratches(p) ? 6 : 0.6,
    scratchLength: 1.15,
    scratchWidth: supportsMetalScratches(p) ? 0.01 : 0.012,
    scratchDepth: 0.0018,
    scratchSpread: 0.8,
    scratchBend: supportsMetalScratches(p) ? 0.42 : 0.04,
    cellLobing: 0.65,
    stomataDensity: 0.22,
    veinRelief: 0.65,
    cellScale: 120,
    cellRelief: 0.5,
    colorGradient: 0.65,
    spotDensity: 0.4,
    skinMode: 0,
    plantRibs: 10,
    lensDome: 0.7,
    packageDepth: 0.6,
    secondaryColor: "#afa38d",
    emissionColor: "#ffffff",
    emissionStrength: 0,
    pixelFill: 0.65,
    moisture: 0,
    scattering: 0,
    translucency: 0,
    freckles: 0,
    paperRibs: 0,
    fiberContrast: 0.3,
    oxidation: 0,
    panelMode: 0,
    busbarWidth: 0.016,
    dimpleDepth: 0.012,
    denimFade: 0.12,
    slub: 0.3,
    opticalGrade: (p.translucency || 0) > 0,
    polymerIOR: 1.5,
    bakeMode: 0,
    bakeHeightRange: 0.02,
    recipeId: p.recipeId,
    tuning: p.tuning || {},
    weavePattern:
      p.id === "indigo-denim"
        ? "denim"
        : p.fabricMode === 3
          ? "plain"
          : p.fabricMode === 4
            ? "satin"
            : "twill",
    warpColor: p.color || "#b5b0a2",
    weftColor: p.id === "indigo-denim" ? "#aab6b9" : p.color || "#b5b0a2",
    paintEffect: "standard",
    iridescence: 0,
    iridescenceIOR: 1.35,
    filmThickness: 380,
    filmVariation: 45,
    ior: 1.5,
    coatIor: 1.5,
    specular: 1,
    specularColor: "#ffffff",
    flakeSize: Math.max(0.01, (p.size ?? 0.45) * 1000),
    flakeScale: 1,
    flakeRoughnessMin: 0.12,
    flakeRoughnessMax: 0.3,
    flakeMetalnessMin: 0.85,
    flakeMetalnessMax: 1,
    flakeTilt: 0.2,
    flakeLayers: 3,
    flakeLayerDepth: 0.55,
    colorMode: "palette",
    orangePeel: p.type === 0 ? 0.18 : 0,
    orangePeelScale: 24,
    detailScale: p.type === 7 ? 14 : isFabric ? 30 : 180,
    weaveAngle: 45,
    weaveRelief: 0.65,
    fiberDetail: 12,
    anisotropy: p.type === 7 ? 0.8 : p.type === 1 ? 0.65 : 0,
    sheen: isFabric ? 0.85 : 0,
    sheenColor: p.color || "#cccccc",
    sheenRoughness: 0.55,
    fuzz: isFabric ? 0.5 : 0,
    fuzzLength: 0.7,
    fabricMode: 0,
    grain: p.type === 9 ? 0.6 : p.type === 10 ? 0.35 : 0,
    wear: p.type === 10 ? 0.65 : 0,
    scratchScale: supportsMetalScratches(p) ? 6 : 32,
    wearSoftness: 0.65,
    wornRoughness: 0.24,
    clothMapping: false,
    ...p,
    colors,
    colorStops: colors.map((_, i) =>
      Number.isFinite(p.colorStops?.[i])
        ? p.colorStops[i]
        : i / Math.max(1, colors.length - 1),
    ),
    hideVariant:
      p.hideVariant ??
      (p.type === 30
        ? 3
        : p.id?.includes("nappa")
          ? 0
          : p.id?.includes("bull-grain")
            ? 2
            : 1),
    hideVariation: p.hideVariation ?? 0.65,
    materialVersion: 6,
  });
  return p.Authoring ? ResolveAuthoringParameters(Normalized, p, false) : Normalized;
}

export function createMaterial(input) {
  const p = ResolveAuthoringParameters(normalizeMaterial(input), input);
  const m = new THREE.MeshPhysicalMaterial({
    color:
      p.type === 5 || (p.type === 4 && ![1, 2].includes(p.fabricMode))
        ? "#ffffff"
        : p.color,
    iridescence: p.iridescence,
    iridescenceIOR: p.iridescenceIOR,
    iridescenceThicknessRange: [
      Math.max(0, p.filmThickness - p.filmVariation * 0.5),
      p.filmThickness + p.filmVariation * 0.5,
    ],
    metalness: p.metalness,
    roughness: p.roughness,
    clearcoat: p.coat,
    clearcoatRoughness: p.coatRoughness,
    ior: p.ior,
    specularIntensity: p.specular,
    specularColor: p.specularColor,
    sheen: p.sheen,
    sheenColor: p.sheenColor,
    sheenRoughness: p.sheenRoughness,
    anisotropy: p.anisotropy,
    anisotropyRotation: (p.weaveAngle * Math.PI) / 180,
    transmission: p.Authoring ? p.translucency : p.type === 5 ? 0.97 : p.translucency,
    emissive: p.type === 20 ? p.emissionColor : "#000000",
    emissiveIntensity: p.emissionStrength,
    toneMapped: !p.bakeMode,
    alphaToCoverage: !p.bakeMode && (p.type === 27 || p.type === 28),
    thickness: p.depth * 2,
    attenuationColor: new THREE.Color(p.color),
    attenuationDistance: 2.8,
    envMapIntensity: p.type === 4 && p.fabricMode === 4 ? 1.8 : 1.35,
    side: THREE.DoubleSide,
  });
  const surfaceSources = attachSurfaceSources(m, p);
  if (surfaceSources.doc) {
    m.clearcoat = Math.max(m.clearcoat, 0.01);
    m.sheen = Math.max(m.sheen, 0.01);
  }
  const stops = p.colors.map((color, i) => ({
    color,
    position: p.colorStops[i],
  }));
  if (p.colorMode === "ramp") stops.sort((a, b) => a.position - b.position);
  const count = stops.length;
  // Average unresolved flakes in linear color space, rather than collapsing to a single ramp stop.
  const averageColor = new THREE.Color(0, 0, 0);
  for (let sample = 0; sample < 48; sample++) {
    const t = (sample + 0.5) / 48;
    let color = new THREE.Color(stops[0].color);
    if (p.colorMode === "palette")
      color.set(stops[Math.min(count - 1, Math.floor(t * count))].color);
    else if (p.colorMode === "ramp") {
      for (let i = 1; i < count; i++) {
        const blend = THREE.MathUtils.clamp(
          (t - stops[i - 1].position) /
            Math.max(0.0001, stops[i].position - stops[i - 1].position),
          0,
          1,
        );
        color.lerp(new THREE.Color(stops[i].color), blend);
      }
    }
    averageColor.add(color.multiplyScalar(1 / 48));
  }
  while (stops.length < 12) stops.push(stops[stops.length - 1]);
  m.userData.params = p;
  m.onBeforeCompile = (shader) => {
    Object.assign(shader.uniforms, surfaceSources.uniforms);
    const beds =
      p.type === 38
        ? p.sandLayers
        : normalizeSandLayers([
            {
              color: p.color,
              secondaryColor: p.secondaryColor,
              size: p.grainSize,
              sizeVariation: p.grainSizeVariation,
              roughness: p.grainRoughness,
              roughnessVariation: p.grainRoughnessVariation,
              specular: p.grainSpecular,
              specularVariation: p.grainSpecularVariation,
              ior: p.grainIOR,
              roundness: p.grainRoundness,
              tilt: p.grainTilt,
              relief: p.grainRelief,
              coverage: p.sandCoverage,
            },
          ]);
    const sandCount = beds.length;
    const sandBeds = Array.from(
      { length: 4 },
      (_, i) => beds[i] || beds[beds.length - 1],
    );
    const values = {
      uSandCount: sandCount,
      uSandLayerSeeds: sandBeds.map((b) => sandLayerSeed(b.id)),
      uSandBandsMode: p.sandArrangement === "bands" ? 1 : 0,
      uSandScale: p.sandScale,
      uSandSeed: p.surfaceSeed,
      uSandBandFrequency: p.sandBandFrequency,
      uSandBandWarp: p.sandBandWarp,
      uSandBandAngle: (p.sandBandAngle * Math.PI) / 180,
      uSandColors: sandBeds.map((b) => new THREE.Color(b.color)),
      uSandPale: sandBeds.map((b) => new THREE.Color(b.secondaryColor)),
      uSandSize: sandBeds.map(
        (b) =>
          new THREE.Vector4(
            b.size,
            b.sizeVariation,
            b.enabled ? b.coverage : 0,
            b.enabled ? b.thickness : 0,
          ),
      ),
      uSandFinish: sandBeds.map(
        (b) =>
          new THREE.Vector4(
            b.roughness,
            b.roughnessVariation,
            b.specular,
            b.specularVariation,
          ),
      ),
      uSandShape: sandBeds.map(
        (b) => new THREE.Vector4(b.ior, b.roundness, b.tilt, b.relief),
      ),
      uRibProfile: p.ribProfile,
      uSheetFinish: p.sheetFinish,
      uSheetOxidation: p.sheetOxidation,
      uSheetSpangle: p.sheetSpangle,
      uCellLobing: p.cellLobing,
      uStomataDensity: p.stomataDensity,
      uScratchAngle:
        ((p.type === 29 ? p.weaveAngle : p.scratchAngle) * Math.PI) / 180,
      uVeinRelief: p.veinRelief,
      uCellScale: p.cellScale,
      uCellRelief: p.cellRelief,
      uColorGradient: p.colorGradient,
      uSpotDensity: p.spotDensity,
      uSkinMode: p.skinMode,
      uPlantRibs: p.plantRibs,
      uTertiary: new THREE.Color(p.tertiaryColor),
      uRibDepth: p.ribDepth,
      uGroutWidth: p.groutWidth,
      uTileVariation: p.tileVariation,
      uTileStagger: p.tileStagger,
      uVeinWidth: p.veinWidth,
      uPoreDensity: p.poreDensity,
      uWallMode: p.wallMode,
      uLeafAspect: p.leafAspect,
      uBladeWidth: p.bladeWidth,
      uBladeLean: p.bladeLean,
      uSurfaceSeed: p.surfaceSeed,
      uScratchDensity: p.scratchDensity,
      uScratchLength: p.scratchLength,
      uScratchWidth: p.scratchWidth,
      uScratchDepth: p.scratchDepth,
      uScratchSpread: p.scratchSpread,
      uScratchBend: p.scratchBend,
      uLensDome: p.lensDome,
      uPackageDepth: p.packageDepth,
      uBakeMode: p.bakeMode,
      uBakeHeightRange: p.bakeHeightRange,
      uSecondary: new THREE.Color(p.secondaryColor),
      uMoisture: p.moisture,
      uScattering: p.scattering,
      uFreckles: p.freckles,
      uPaperRibs: p.paperRibs,
      uFiberContrast: p.fiberContrast,
      uOxidation: p.oxidation,
      uPanelMode: p.panelMode,
      uBusbarWidth: p.busbarWidth,
      uDimpleDepth: p.dimpleDepth,
      uDenimFade: p.denimFade,
      uSlub: p.slub,
      uPixelFill: p.pixelFill,
      uEmissionScale: Math.max(1, p.emissionStrength),
      uAverageFlakeColor: averageColor,
      uType: p.type,
      uFlakes: p.flakes,
      uFlakeFrequency: Math.min(
        1e7,
        (52000 / Math.max(0.001, p.flakeSize)) * p.flakeScale,
      ),
      uFlakeRoughMin: p.flakeRoughnessMin,
      uFlakeRoughMax: p.flakeRoughnessMax,
      uFlakeMetalMin: p.flakeMetalnessMin,
      uFlakeMetalMax: p.flakeMetalnessMax,
      uFlakeTilt: p.flakeTilt,
      uFlakeWeight: p.FlakeWeight ?? 1,
      uFlakeLayers: p.flakeLayers,
      uFlakeLayerDepth: p.flakeLayerDepth,
      uCloth: p.clothMapping ? 1 : 0,
      uScale: p.detailScale,
      uDepth: p.depth,
      uCoatIor: p.coatIor,
      uPeel: p.orangePeel,
      uPeelScale: p.orangePeelScale,
      uWeaveAngle: (p.weaveAngle * Math.PI) / 180,
      uRelief: p.weaveRelief,
      uFiberDetail: p.fiberDetail,
      uFabricMode: p.fabricMode,
      uWeave: [
        "plain",
        "twill",
        "satin",
        "basket",
        "rib",
        "herringbone",
        "oxford",
        "houndstooth",
        "denim",
      ].indexOf(p.weavePattern),
      uWarpColor: new THREE.Color(p.warpColor),
      uWeftColor: new THREE.Color(p.weftColor),
      uFilmThickness: p.filmThickness,
      uFilmVariation: p.filmVariation,
      uFuzz: p.fuzz,
      uGrain: p.grain,
      uWear: p.wear,
      uWearSoftness: p.wearSoftness,
      uWornRoughness: p.wornRoughness,
      uScratchScale: p.scratchScale,
      uColorCount: count,
      uColorMode: p.colorMode === "ramp" ? 1 : p.colorMode === "single" ? 2 : 0,
      uColors: stops.map((s) => new THREE.Color(s.color)),
      uStops: stops.map((s) => s.position),
    };
    for (const [key, value] of Object.entries(values))
      shader.uniforms[key] = { value };
    shader.vertexShader =
      "varying vec3 vProcPosition;\nvarying vec3 vProcNormal;\nvarying vec2 vProcUv;\n" +
      shader.vertexShader;
    shader.vertexShader = shader.vertexShader.replace(
      "#include <begin_vertex>",
      "#include <begin_vertex>\nvProcPosition=position;vProcNormal=normal;vProcUv=uv;",
    );
    shader.fragmentShader =
      `
      ${surfaceSources.leather ? "#define ALLOY_HIDE_SOURCE" : ""}
      ${surfaceSources.doc ? "#define ALLOY_PATTERN" : ""}
      varying vec3 vProcPosition;
      varying vec3 vProcNormal;
      varying vec2 vProcUv;
      #define uType ${Math.max(0, Math.min(38, Math.floor(Number(p.type) || 0)))}
      #define uPotterySurface ${p.potterySurface ? 1 : 0}
      #define uMetalScratches ${p.metalScratches ? 1 : 0}
      uniform int uColorCount, uColorMode, uFabricMode, uFlakeLayers, uCloth, uWeave;
      uniform float uFlakeWeight,uFlakeLayerDepth,uWearSoftness,uWornRoughness;
      uniform float uFlakes,uFlakeFrequency,uFlakeRoughMin,uFlakeRoughMax,uFlakeMetalMin,uFlakeMetalMax,uFlakeTilt;
      uniform float uScale,uDepth,uCoatIor,uPeel,uPeelScale,uWeaveAngle,uRelief,uFiberDetail,uGrain,uWear,uScratchScale,uFuzz;
      uniform vec3 uColors[12];
      uniform vec3 uAverageFlakeColor;
      uniform vec3 uWarpColor,uWeftColor;
      uniform float uFilmThickness,uFilmVariation;
      uniform float uStops[12];
      float hash31(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
      float noise3(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(mix(hash31(i),hash31(i+vec3(1,0,0)),f.x),mix(hash31(i+vec3(0,1,0)),hash31(i+vec3(1,1,0)),f.x),f.y),mix(mix(hash31(i+vec3(0,0,1)),hash31(i+vec3(1,0,1)),f.x),mix(hash31(i+vec3(0,1,1)),hash31(i+vec3(1,1,1)),f.x),f.y),f.z);}
      vec3 pickColor(float t){
        if(uColorMode==2)return uColors[0];
        if(uColorMode==0){vec3 c=uColors[0];for(int i=0;i<12;i++){if(i<uColorCount && t>=float(i)/float(uColorCount))c=uColors[i];}return c;}
        vec3 c=uColors[0];for(int i=1;i<12;i++){if(i<uColorCount)c=mix(c,uColors[i],clamp((t-uStops[i-1])/max(.0001,uStops[i]-uStops[i-1]),0.,1.));}return c;
      }
      vec3 projectionWeights(vec3 n){vec3 w=pow(abs(normalize(n)),vec3(8.));return w/max(dot(w,vec3(1.)),.0001);}
      vec2 rotateUV(vec2 uv){float c=cos(uWeaveAngle),s=sin(uWeaveAngle);return mat2(c,-s,s,c)*uv;}
      // 3D Worley / cellular noise. F2-F1 yields filled, irregular cell interiors,
      // not one centered dot per grid square. Three independently transformed
      // volumes are composited front-to-back to create partially buried flakes.
      vec4 cellular3(vec3 p,float seed){
        vec3 cell=floor(p),local=fract(p),nearest=vec3(0.);
        float f1=1e8,f2=1e8;
        for(int z=-1;z<=1;z++)for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
          vec3 offset=vec3(float(x),float(y),float(z)),id=cell+offset;
          vec3 site=.08+.84*vec3(hash31(id+seed),hash31(id+seed+17.31),hash31(id+seed+43.97));
          vec3 delta=offset+site-local;
          float d=dot(delta,delta);
          if(d<f1){f2=f1;f1=d;nearest=id;}else if(d<f2)f2=d;
        }
        return vec4(max(0.,sqrt(f2)-sqrt(f1)),hash31(nearest+seed+61.7),hash31(nearest+seed+127.3),hash31(nearest+seed+223.1));
      }
      float smoothMinimum(float a,float b,float k){float h=clamp(.5+.5*(b-a)/max(k,.0001),0.,1.);return mix(b,a,h)-k*h*(1.-h);}
      float polishHeight(float height,float wearField){
        float ceiling=mix(1.35,.22,pow(uWear*wearField,.55));
        return mix(height,smoothMinimum(height,ceiling,mix(.035,.24,uWearSoftness)),smoothstep(0.,.08,uWear));
      }
      // 2-over / 2-under twill with rounded bundles, inter-yarn gaps and sub-fibers.
      vec4 twill(vec2 uv){
        uv=rotateUV(uv)*uScale;
        vec2 cell=floor(uv),f=fract(uv);
        float over=step(2.,mod(cell.x-cell.y,4.));
        if(uType==4){
          if(uWeave==0)over=mod(cell.x+cell.y,2.);
          if(uWeave==2)over=step(4.,mod(cell.x+2.*cell.y,5.));
          if(uWeave==3)over=mod(floor(cell.x*.5)+floor(cell.y*.5),2.);
          if(uWeave==4)over=mod(cell.x+floor(cell.y*.5),2.);
          if(uWeave==5){float reverse=mod(cell.x,16.);float chevron=reverse<8.?reverse:15.-reverse;over=step(2.,mod(chevron-cell.y,4.));}
          if(uWeave==6)over=mod(floor(cell.x*.5)+cell.y,2.);
          if(uWeave==8)over=step(3.,mod(cell.x-cell.y,4.));
        }
        float across=mix(f.x,f.y,over),coord=mix(uv.x,uv.y,over);
        float edge=min(across,1.-across);
        float bundle=sqrt(max(0.,1.-pow(across*2.-1.,2.)));
        float gap=smoothstep(.008,.045,edge);
        float along=mix(uv.y,uv.x,over);
        float floatLength=(uType==4 && uWeave==2)?5.:(uType==4 && uWeave==8)?3.:(uType==4 && uWeave==0)?1.:2.;
        float arch=.86+.14*cos((fract(along/floatLength)*2.-1.)*3.14159);
        float freq=coord*6.283*uFiberDetail;
        float fiber=(sin(freq)*.5+.5)*(1.-smoothstep(.5,3.1,fwidth(freq)));
        float height=bundle*gap*arch;
        float light=(.32+.68*bundle)*gap*(.86+.14*fiber);
        if(uType==4 && uFabricMode==3)light*=.88+.16*noise3(vec3(uv*.6,3.));
        if(uType==4 && uWeave==2)light=(.72+.28*bundle)*(.91+.09*fiber)*mix(.9,1.,gap);
        float yarnResolved=1.-smoothstep(.22,.95,max(fwidth(uv.x),fwidth(uv.y)));
        float meanLight=(uType==4 && uWeave==2)?.86:.69;
        return vec4(mix(meanLight,light,yarnResolved),mix(.63,height,yarnResolved),mix((uWeave==2 && uType==4)?.2:(uWeave==8 && uType==4)?.25:.5,over,yarnResolved),fiber*yarnResolved);
      }
      vec3 reliefNormal(float height,vec3 n,vec3 position){
        vec3 q0=dFdx(position),q1=dFdy(position);
        vec3 r1=cross(q1,n),r2=cross(n,q0);
        float det=dot(q0,r1);
        vec3 grad=sign(det)*(dFdx(height)*r1+dFdy(height)*r2);
        return normalize(max(abs(det),1e-10)*n-grad);
      }
      float flakeMask=0.,flakeRandom=.5,flakeResolved=1.,surfaceHeight=0.,peelHeight=0.,wearMask=0.;
      ${extendedSurfaceGLSL()}
      ${architecturalGLSL()}
      ${sandGLSL()}
${patternGLSL()}
${botanicalGLSL()}
      vec3 flakeTint=vec3(0.);
      vec2 yarnUV=vec2(0.);
      float yarnDirection=0.;
    ` + shader.fragmentShader;
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <color_fragment>",
      `
      #include <color_fragment>
      vec3 pp=uCloth==1?vec3(vProcUv*4.9,0.):vProcPosition;
      vec3 weights=projectionWeights(vProcNormal);
      float grain=noise3(pp*uScale);
      peelHeight=(noise3(pp*uPeelScale)+.35*noise3(pp*uPeelScale*2.1))*uPeel*.003;
      if(uType==0){
        vec3 base=diffuseColor.rgb*exp(-uDepth*.28);
        vec3 blended=base;
        float maskTotal=0.,randomTotal=0.,resolved=0.;
        vec3 domain=pp*uFlakeFrequency;
        float footprint=max(length(dFdx(domain)),length(dFdy(domain)));
        float detail=1.-smoothstep(.55,1.6,footprint);
        if(detail>.001){
          // A smooth low-frequency domain warp removes residual lattice alignment.
          vec3 warp=vec3(noise3(domain*.31),noise3(domain*.31+11.7),noise3(domain*.31+37.4))-.5;
          for(int layer=0;layer<4;layer++)if(layer<uFlakeLayers){
            float l=float(layer),depth=float(uFlakeLayers-1-layer)/max(float(uFlakeLayers-1),1.);
            vec3 q=domain*(.71+l*.37)+warp*.7+vec3(l*27.17,l*51.73,l*19.43);
            q=mat3(.80,.36,-.48,-.60,.48,-.64,0.,.80,.60)*q;
            vec4 cell=cellular3(q,l*97.3+4.1);
            float aa=max(fwidth(cell.x)*.65,.009);
            float inset=.018+cell.w*.07;
            float interior=smoothstep(inset-aa,inset+aa,cell.x);
            float occupied=step(1.-uFlakes*.55,cell.y);
            float opacity=interior*occupied;
            vec3 tint=pickColor(cell.z);
            // Buried particles are tinted by the binder; upper cells occlude lower ones.
            tint=mix(tint,base,depth*uFlakeLayerDepth*.72);
            blended=mix(blended,tint,opacity);
            randomTotal=mix(randomTotal,cell.w,opacity);
            maskTotal=opacity+maskTotal*(1.-opacity);
          }
        }
        float meanMask=1.-pow(1.-uFlakes*.31,float(uFlakeLayers));
        vec3 farTint=mix(uAverageFlakeColor,base,uFlakeLayerDepth*.26);
        diffuseColor.rgb=${p.FlakeOutputs?.colour === false ? "base" : "mix(base,mix(mix(base,farTint,meanMask),blended,detail),uFlakeWeight)"};
        flakeMask=mix(meanMask,maskTotal,detail)*uFlakeWeight;
        flakeRandom=mix(.5,randomTotal,detail);flakeResolved=detail;
      }else if(uType==1){diffuseColor.rgb*=.92+.1*noise3(pp*vec3(uScale,5.,uScale));surfaceHeight=noise3(pp*uScale)*uGrain*.003;}
      else if(uType==2){diffuseColor.rgb*=.6+.65*grain;surfaceHeight=grain*.003*uDepth;}
      else if(uType==3){diffuseColor.rgb*=.84+.22*grain;surfaceHeight=grain*.002*uDepth;}
      else if(uType==4 || uType==7){
        vec4 tx=twill(pp.yz),ty=twill(pp.xz),tz=twill(pp.xy);
        vec4 weave=tx*weights.x+ty*weights.y+tz*weights.z;
        if(uCloth==1)weave=twill(vProcUv*4.9);
        if(weights.x>weights.y && weights.x>weights.z){yarnUV=rotateUV(pp.yz)*uScale;yarnDirection=tx.z;}
        else if(weights.y>weights.z){yarnUV=rotateUV(pp.xz)*uScale;yarnDirection=ty.z;}
        else{yarnUV=rotateUV(pp.xy)*uScale;yarnDirection=tz.z;}
        if(uCloth==1){yarnUV=rotateUV(vProcUv*4.9)*uScale;yarnDirection=weave.z;}
        if(uType==7){diffuseColor.rgb*=.18+weave.x*.82;}
        else if(uFabricMode==0 || uFabricMode==3 || uFabricMode==4){
          vec3 yarnColor=mix(uWarpColor,uWeftColor,weave.z);
          if(uWeave==7){
            float warpBand=step(2.,mod(floor(yarnUV.x),4.));
            float weftBand=step(2.,mod(floor(yarnUV.y),4.));
            float colorDetail=1.-smoothstep(.35,1.5,max(fwidth(yarnUV.x),fwidth(yarnUV.y)));
            yarnColor=mix((uWarpColor+uWeftColor)*.5,mix(uWarpColor,uWeftColor,mix(warpBand,weftBand,yarnDirection)),colorDetail);
          }
          diffuseColor.rgb=yarnColor*(.48+weave.x*.58);
        }
        else {
          float nap=noise3(pp*vec3(uScale*18.,uScale*4.,uScale*18.));
          diffuseColor.rgb*=.68+.36*nap;
        }
        surfaceHeight=(uType==4 && (uFabricMode==1 || uFabricMode==2)?grain*.15:weave.y)*uRelief*.022/max(uScale,.01);
        if(uType==4){
          float fiberNap=noise3(pp*vec3(uScale*18.,uScale*4.,uScale*18.));
          surfaceHeight+=fiberNap*uFuzz*.0004;
          diffuseColor.rgb*=1.-uFuzz*.12+uFuzz*fiberNap*.24;
        }
      }else if(uType==6){
        float phase=length(pp.xy)*uScale*3.3;
        float lines=sin(phase)*.5*(1.-smoothstep(.5,3.1,fwidth(phase)))+.5;
        diffuseColor.rgb*=.84+lines*.14;surfaceHeight=lines*.0001*uDepth;
      }else if(uType>=8 && uType<=11){
        float micro=noise3(pp*uScale)+.3*noise3(pp*uScale*2.7);
        if(uType==11){
          vec3 hide=hideGrain(pp);
          micro=hide.x;
          leatherGrain=hide.x;leatherFold=hide.y;
          diffuseColor.rgb*=1.-hide.y*.035-hide.z*.02;

        }
        float patchNoise=noise3(pp*5.3)+.28*noise3(pp*11.7);
        float transition=mix(.035,.32,uWearSoftness);
        float wearField=smoothstep(.5-transition,.72+transition,patchNoise);
        float polished=polishHeight(micro,wearField);
        float removed=max(0.,micro-polished);
        wearMask=smoothstep(.005,.24,removed);
        surfaceHeight=polished*uGrain*(uType==11?.003:.004);
        diffuseColor.rgb*=1.-uGrain*.12+micro*uGrain*.15;
        float scratches=noise3(pp*vec3(uScratchScale*8.,uScratchScale*.12,uScratchScale*8.));
        float fine=1.-smoothstep(.5,2.,length(fwidth(pp*uScratchScale*8.)));
        float marks=smoothstep(.65,.87,scratches)*fine;
        float scratchWear=marks*uWear*.2*wearField;
        diffuseColor.rgb=mix(diffuseColor.rgb,diffuseColor.rgb*1.18+vec3(.012),wearMask*.6+scratchWear);
        surfaceHeight-=scratchWear*.001*(1.-wearMask*.85);
      }
      ${extendedSurfaceColor()}
      ${architecturalColor()}
      ${sandColor()}
      if(uType==22 && uPotterySurface==1)surfaceHeight=(noise3(pp*220.)-.5)*.00004*(1.-smoothstep(.5,1.5,length(fwidth(pp*220.))));
${botanicalColor()}
${patternColorGLSL()}
      #if uMetalScratches == 1
      vec3 metalCuts=scratchField(surfaceUV(pp,weights));
      scratchMask=metalCuts.x;surfaceHeight+=metalCuts.y;
      diffuseColor.rgb*=1.-scratchMask*.11;
      #endif
    `,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <roughnessmap_fragment>",
      `
      #include <roughnessmap_fragment>
      ${p.FlakeOutputs?.roughness === false ? "" : "if(uType==0)roughnessFactor=mix(roughnessFactor,mix(uFlakeRoughMin,uFlakeRoughMax,flakeRandom),flakeMask);"}
      if(uType>=8 && uType<=11)roughnessFactor=mix(clamp(roughnessFactor+uGrain*(grain-.5)*.16,.025,1.),uWornRoughness,wearMask);
      if(uType==12)roughnessFactor=mix(roughnessFactor,.24,uMoisture*.7);
      if(uType==16)roughnessFactor=mix(roughnessFactor,.94,oxideMask);
      if(uType==21 && uSheetFinish>0)roughnessFactor=mix(clamp(roughnessFactor+(sheetRoughness-.5)*uSheetSpangle*.24,.04,1.),.88,sheetOxide);
      #if uType == 37 || uType == 38
        roughnessFactor=sandRoughness;
      #endif
      if(uType==17)roughnessFactor=mix(roughnessFactor,.22,contactMask);
      if(uType==20)roughnessFactor=mix(mix(roughnessFactor,.08,ledLens),.22,ledContact);
      if(uType==22)roughnessFactor=mix(roughnessFactor,.93,groutMask);
      if(uType==30)roughnessFactor=clamp(roughnessFactor+bioJoint*.065+(leatherGrain-.5)*.035,.12,.95);
      if(uType==11)roughnessFactor=clamp(roughnessFactor+leatherFold*.06-(leatherGrain-.5)*.05,.18,.95);
      #ifdef ALLOY_PATTERN
        roughnessFactor=mix(roughnessFactor,patternParamsSample.r,patternChannelMaskSample.r);
      #endif
      if(uType==34 || uType==35)roughnessFactor=mix(roughnessFactor,min(1.,roughnessFactor+.24),bioPore);
      if(uType==29 || uMetalScratches==1)roughnessFactor=mix(roughnessFactor,min(1.,roughnessFactor+.3),scratchMask);
    `,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <metalnessmap_fragment>",
      `
      #include <metalnessmap_fragment>
      ${p.FlakeOutputs?.metallic === false ? "" : "if(uType==0)metalnessFactor=mix(metalnessFactor,mix(uFlakeMetalMin,uFlakeMetalMax,flakeRandom),flakeMask);"}
      if(uType==16)metalnessFactor*=1.-oxideMask;
      if(uType==21 && uSheetFinish>0)metalnessFactor*=1.-sheetOxide;
      if(uType==17)metalnessFactor=mix(.25,.96,contactMask);
      if(uType==20)metalnessFactor=ledContact*.92;
      #ifdef ALLOY_PATTERN
        metalnessFactor=mix(metalnessFactor,patternParamsSample.g,patternChannelMaskSample.g);
      #endif
    `,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <normal_fragment_maps>",
      `
      #include <normal_fragment_maps>
      normal=reliefNormal(surfaceHeight+peelHeight*.2,normal,-vViewPosition);
      #ifdef ALLOY_PATTERN
        if(patternNormalSample.a>.0001){
          vec3 PatternQx=dFdx(-vViewPosition),PatternQy=dFdy(-vViewPosition);
          vec2 PatternUx=dFdx(patternUV),PatternUy=dFdy(patternUV);
          float PatternDet=PatternUx.x*PatternUy.y-PatternUx.y*PatternUy.x;
          if(abs(PatternDet)>.00000001){
            vec3 PatternT=normalize((PatternQx*PatternUy.y-PatternQy*PatternUx.y)/PatternDet);
            PatternT=normalize(PatternT-normal*dot(normal,PatternT));
            vec3 PatternB=normalize(cross(normal,PatternT))*sign(PatternDet);
            vec3 PatternN=normalize(patternNormalSample.rgb*2.-1.);
            // Canvas sources use downward image V; imported normals use OpenGL +Y.
            PatternN.y=-PatternN.y;
            normal=normalize(mix(normal,mat3(PatternT,PatternB,normal)*PatternN,patternNormalSample.a));
          }
        }
      #endif
      #if uType == 37 || uType == 38
        vec3 sandT=normalize(cross(normal,abs(normal.y)<.95?vec3(0,1,0):vec3(1,0,0)));
        vec3 sandB=cross(normal,sandT);
        normal=normalize(normal+sandT*sandFacet.x+sandB*sandFacet.y);
      #endif
      if(uType==0){
        vec3 tangent=normalize(cross(normal,abs(normal.y)<.95?vec3(0,1,0):vec3(1,0,0)));
        vec3 bitangent=cross(normal,tangent);
        float angle=flakeRandom*6.283;
        ${p.FlakeOutputs?.normal === false ? "" : "normal=normalize(normal+(tangent*cos(angle)+bitangent*sin(angle))*uFlakeTilt*flakeMask*flakeResolved);"}
      }
    `,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <clearcoat_normal_fragment_maps>",
      `
      #include <clearcoat_normal_fragment_maps>
      #ifdef USE_CLEARCOAT
        #ifdef ALLOY_PATTERN
          clearcoatNormal=reliefNormal(surfaceHeight+peelHeight,clearcoatNormal,-vViewPosition);
        #else
          clearcoatNormal=reliefNormal(peelHeight+((uType==11 || uType==20 || uType==22 || uType==30 || uType>=31 || uMetalScratches==1)?surfaceHeight:0.),clearcoatNormal,-vViewPosition);
        #endif
      #endif
    `,
    );
    // Three.js hardcodes the coat F0 to .04. Replace it with the independent coat IOR Fresnel term.
    let physical = THREE.ShaderChunk.lights_physical_fragment.replace(
      "material.clearcoatF0 = vec3( 0.04 );",
      "material.clearcoatF0 = vec3( pow2( (uCoatIor - 1.0) / (uCoatIor + 1.0) ) );",
    );
    physical += `
      #ifdef USE_CLEARCOAT
        if(uType==11 || uType==30){
          material.clearcoatRoughness=clamp(material.clearcoatRoughness+leatherFold*.055+(leatherGrain-.5)*.035,.06,1.);
        }
        #ifdef ALLOY_PATTERN
          float glaze=patternFinishSample.r;
          material.clearcoat=mix(material.clearcoat,glaze*.75+material.clearcoat*(1.-clamp(patternFinishSample.r+patternFinishSample.g+patternFinishSample.b,0.,1.)),patternMaterialCoverage);
          material.clearcoatRoughness=mix(material.clearcoatRoughness,.16,glaze*patternCoverage);
        #endif
        if(uType==20)material.clearcoat*=ledLens;
        if(uType==22)material.clearcoat*=1.-groutMask;
      #endif
      #if defined(ALLOY_PATTERN) && defined(USE_SHEEN)
        float wool=patternFinishSample.g;
        material.sheenColor=mix(material.sheenColor,diffuseColor.rgb*.4,patternMaterialCoverage*wool);
        material.sheenRoughness=mix(material.sheenRoughness,.85,patternMaterialCoverage*wool);
      #endif
      #if uType == 37 || uType == 38
        float grainF0=pow2((sandIOR-1.)/(sandIOR+1.));
        material.specularColor=mix(vec3(grainF0*sandSpecular),diffuseColor.rgb,metalnessFactor);
        material.specularF90=mix(sandSpecular,1.,metalnessFactor);
      #endif
      #ifdef USE_IRIDESCENCE
        material.iridescenceThickness=uFilmThickness+(noise3(pp*3.7)-.5)*uFilmVariation;
      #endif
      #ifdef USE_ANISOTROPY
      if(uType==7 || uType==4){
        vec3 dpdx=dFdx(-vViewPosition),dpdy=dFdy(-vViewPosition);
        vec2 duvdx=dFdx(yarnUV),duvdy=dFdy(yarnUV);
        vec3 t=dpdx*duvdy.y-dpdy*duvdx.y;
        t=normalize(t-normal*dot(t,normal)+vec3(1e-7));
        vec3 b=normalize(cross(normal,t));
        material.anisotropyT=normalize(mix(t,b,yarnDirection));
        material.anisotropyB=normalize(cross(normal,material.anisotropyT));
      }
      #endif
    `;
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <lights_physical_fragment>",
      physical,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <emissivemap_fragment>",
      `#include <emissivemap_fragment>
      if(uType==20)totalEmissiveRadiance*=emitterMask;`,
    );
    shader.fragmentShader = shader.fragmentShader.replace(
      "#include <lights_fragment_end>",
      `#include <lights_fragment_end>
      #ifdef ALLOY_PATTERN
        reflectedLight.indirectDiffuse*=mix(1.,patternParamsSample.a,patternChannelMaskSample.a);
      #endif
      if(uType==13 || uType==14)reflectedLight.indirectDiffuse+=diffuseColor.rgb*ambientLightColor*uScattering*.22*pow(1.-abs(dot(normal,geometryViewDir)),2.)*vec3(1.,.55,.32);`,
    );
    // Same procedural field, unlit channel output. Bake colors are encoded sRGB;
    // scalar and normal channels are linear. A flat XY patch gives tangent normals.
    if (!p.bakeMode)
      shader.fragmentShader = shader.fragmentShader.replace(
        "#include <opaque_fragment>",
        "#include <opaque_fragment>\nif(uType==27 || uType==28){if(surfaceCoverage<.001)discard;gl_FragColor.a=surfaceCoverage;}",
      );
    if (p.bakeMode)
      shader.fragmentShader = shader.fragmentShader.replace(
        "#include <opaque_fragment>",
        `#include <opaque_fragment>
      if(uBakeMode==1)gl_FragColor=vec4(bakeSRGB(diffuseColor.rgb),surfaceCoverage);
      if(uBakeMode==2)gl_FragColor=vec4(vec3(clamp(roughnessFactor,0.,1.)),surfaceCoverage);
      if(uBakeMode==3)gl_FragColor=vec4(vec3(clamp(metalnessFactor,0.,1.)),surfaceCoverage);
      if(uBakeMode==4)gl_FragColor=vec4(normal*.5+.5,surfaceCoverage);
      if(uBakeMode==5)gl_FragColor=vec4(vec3(clamp(.5+surfaceHeight/uBakeHeightRange,0.,1.)),surfaceCoverage);
      if(uBakeMode==6)gl_FragColor=vec4(bakeSRGB(totalEmissiveRadiance/uEmissionScale),surfaceCoverage);
    `,
      );
    m.userData.shader = shader;
  };
  m.customProgramCacheKey = () =>
    `alloy-procedural-v7-${p.type}-${p.metalScratches}-${p.bakeMode > 0}-${!!p.pattern}-${!!p.potterySurface}-${JSON.stringify(p.FlakeOutputs || {})}`;
  AttachPbrFields(m, p);
  return m;
}

export function createBallGeometry() {
  const points = [];
  for (let i = 0; i <= 340; i++) {
    const a = (Math.PI * i) / 340;
    const y = 1.43 * Math.cos(a);
    let r = 1.43 * Math.sin(a);
    const groove = (c, w, d) => d * Math.exp(-Math.pow((y - c) / w, 6));
    r -= groove(0.69, 0.021, 0.032) + groove(0.6, 0.014, 0.023);
    r -=
      groove(-0.81, 0.018, 0.028) +
      groove(-0.89, 0.018, 0.028) +
      groove(-0.97, 0.018, 0.028);
    points.push(new THREE.Vector2(Math.max(0.001, r), y));
  }
  return new THREE.LatheGeometry(points.reverse(), 192);
}

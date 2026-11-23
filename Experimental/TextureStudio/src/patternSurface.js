import { materials, normalizeMaterial } from "./materials.js";

// One base resolver for the editor's live preview and the committed workspace.
export function resolvePatternBase(current, base) {
  if (base === "pottery")
    return normalizeMaterial({
      id: "glazed-pottery",
      name: "Glazed Porcelain",
      category: "Ceramic",
      label: "CONTINUOUS CERAMIC GLAZE",
      type: 22,
      recipeId: "pottery",
      potterySurface: true,
      color: "#eee9df",
      roughness: 0.18,
      coat: 0.85,
      coatRoughness: 0.12,
    });
  return base === "current"
    ? current
    : materials.find((m) => m.id === base) || current;
}
export function composePatternMaterial(target, pattern) {
  const name = target.patternBaseName || target.name;
  return {
    ...target,
    pattern,
    name: pattern.name + " · " + name,
    patternBaseName: name,
  };
}
export function patternPreviewShape(target) {
  if (target.pattern?.presentation === "rug" && target.category === "Fabric")
    return "Rug";
  return target.potterySurface
    ? "Teapot"
    : target.category === "Fabric"
      ? "Draped cloth"
      : target.category === "Leather"
        ? "Leather swatch"
        : "Panel";
}

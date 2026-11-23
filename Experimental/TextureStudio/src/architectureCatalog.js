// A small expansion of distinct surface constructions, not a preset-count target.
export function architectureCatalog() {
  const rows = [];
  const add = (name, category, type, recipeId, color, extra = {}) =>
    rows.push({
      id: name.toLowerCase().replace(/[^a-z0-9]+/g, "-"),
      name,
      category,
      type,
      recipeId,
      color,
      colors: [color],
      roughness: 0.5,
      metalness: 0,
      coat: 0,
      coatRoughness: 0.12,
      grain: 0.5,
      depth: 0.4,
      flakes: 0,
      detailScale: 12,
      label: category.toUpperCase() + " · PROCEDURAL",
      ...extra,
    });
  add("Corrugated Aluminium", "Technical", 21, "corrugated", "#d1d5d5", {
    metalness: 1,
    roughness: 0.28,
    ribDepth: 0.018,
    detailScale: 12,
    description:
      "A hollow pipe preview with ribbed normal relief over a conductor. Smooth silhouette.",
  });
  add("Porcelain Grid Tiles", "Ceramic", 22, "tiles", "#d9d8cc", {
    secondaryColor: "#706e65",
    roughness: 0.2,
    coat: 0.85,
    grain: 0.45,
    detailScale: 1.8,
    groutWidth: 0.045,
    tileVariation: 0.08,
    description:
      "Glazed ceramic faces, rounded edge relief and matte recessed grout.",
  });
  add("Zellige Ceramic Tiles", "Ceramic", 22, "tiles", "#316865", {
    secondaryColor: "#b5ab92",
    roughness: 0.28,
    coat: 0.85,
    grain: 0.8,
    detailScale: 2.5,
    groutWidth: 0.035,
    tileVariation: 0.3,
    tileStagger: 0.5,
    description:
      "Uneven glazed color, handmade surface undulation and staggered grout.",
  });
  add("Salt and Pepper Granite", "Stone", 23, "granite", "#898781", {
    secondaryColor: "#ddd7c6",
    tertiaryColor: "#24272b",
    roughness: 0.2,
    coat: 0.35,
    detailScale: 35,
    grain: 0.35,
    description:
      "Interlocking feldspar, pale quartz and dark mica; not a speckle image.",
  });
  add("Rose Granite", "Stone", 23, "granite", "#b07e6e", {
    secondaryColor: "#cfc4b3",
    tertiaryColor: "#343638",
    roughness: 0.28,
    coat: 0.25,
    detailScale: 24,
    grain: 0.5,
    description: "Warm feldspar and irregular crystalline mineral grains.",
  });
  add("Carrara Marble", "Stone", 24, "marble", "#dddcd3", {
    secondaryColor: "#626b70",
    roughness: 0.18,
    coat: 0.4,
    detailScale: 2.8,
    veinWidth: 0.035,
    grain: 0.3,
    description:
      "Warped, multiscale mineral veins through a pale polished stone body.",
  });
  add("Nero Marble", "Stone", 24, "marble", "#24282a", {
    secondaryColor: "#c8ba91",
    roughness: 0.16,
    coat: 0.45,
    detailScale: 2.2,
    veinWidth: 0.025,
    grain: 0.4,
    description: "Warm wandering veins in a dark stone matrix.",
  });
  add("Cast Concrete", "Wall", 25, "concrete", "#aaa79c", {
    secondaryColor: "#68665c",
    roughness: 0.88,
    detailScale: 40,
    grain: 0.65,
    poreDensity: 0.65,
    description: "Cement mottling, fine aggregate and recessed air voids.",
  });
  add("Board Form Concrete", "Wall", 25, "concrete", "#949589", {
    secondaryColor: "#595c54",
    roughness: 0.86,
    detailScale: 38,
    grain: 0.7,
    poreDensity: 0.4,
    wallMode: 1,
    description:
      "Subtle timber-form impressions and joints over a porous cement surface.",
  });
  add("Lime Stucco", "Wall", 26, "stucco", "#d5cbb4", {
    roughness: 0.94,
    detailScale: 48,
    grain: 0.85,
    description:
      "A coarse, layered plaster grain with raised peaks and dry diffuse reflections.",
  });
  add("Broadleaf Green", "Nature", 31, "leafSurface", "#355d28", {
    secondaryColor: "#829548",
    tertiaryColor: "#8c8a38",
    colorGradient: 0.18,
    roughness: 0.48,
    detailScale: 10,
    leafAspect: 0.4,
    grain: 0.65,
    description:
      "Full UV leaf surface: branching veins, epidermal cells and mottled pigment. For real leaf meshes, not an atlas.",
  });
  add("Autumn Leaf", "Nature", 31, "leafSurface", "#985527", {
    secondaryColor: "#c3a24a",
    tertiaryColor: "#c78b37",
    colorGradient: 0.75,
    roughness: 0.6,
    detailScale: 12,
    leafAspect: 0.32,
    grain: 0.5,
    description:
      "Whole-leaf autumn pigment, branching veins and fine epidermis. No cutout; use your mesh silhouette.",
  });
  add("Meadow Grass Blades", "Nature", 32, "grassSurface", "#335b27", {
    secondaryColor: "#849348",
    roughness: 0.78,
    detailScale: 13,
    bladeWidth: 0.13,
    bladeLean: 0.45,
    grain: 0.7,
    description:
      "One grass-blade surface with longitudinal ribs, tip gradient and fine epidermal detail. No atlas.",
  });
  add("Scratches", "Technical", 29, "scratches", "#b9bec0", {
    metalness: 1,
    roughness: 0.23,
    scratchScale: 6,
    scratchDensity: 6,
    scratchLength: 1.15,
    scratchWidth: 0.01,
    scratchDepth: 0.0018,
    scratchSpread: 0.8,
    scratchBend: 0.42,
    weaveAngle: 25,
    description:
      "Isolated scratch study: finite tapered cuts, irregular lengths, tiny raised lips and real groove normals. Also available as an optional layer on metals.",
  });
  return rows;
}

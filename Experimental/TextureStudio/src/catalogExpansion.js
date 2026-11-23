// Curated variants share procedural kernels, not bitmap maps. Alloy/grade names
// describe representative finishes; these are not certified measured BRDFs.
export function expandedCatalog() {
  const result = [];
  const add = (name, category, type, recipeId, color, params = {}) =>
    result.push({
      id: name
        .toLowerCase()
        .replace(/[^a-z0-9]+/g, "-")
        .replace(/-$/, ""),
      name,
      category,
      type,
      recipeId,
      color,
      label: category.toUpperCase() + " · PROCEDURAL",
      description: "Built from surface structure, not texture maps.",
      roughness: 0.5,
      metalness: 0,
      coat: 0,
      coatRoughness: 0.15,
      depth: 0.5,
      flakes: 0,
      grain: 0.35,
      detailScale: 35,
      colors: [color],
      ...params,
    });
  [
    ["Terracotta Clay", "#aa5840", 0.89, 0.8, 0.04],
    ["Wet Potter Clay", "#685647", 0.38, 0.45, 0.35],
    ["Kaolin Clay", "#d7cebd", 0.92, 0.35, 0.02],
    ["Sculpting Clay", "#777569", 0.76, 0.6, 0.1],
  ].forEach(([n, c, r, g, moisture]) =>
    add(n, "Clay", 12, "clay", c, {
      roughness: r,
      grain: g,
      moisture,
      detailScale: 24,
      description:
        "Fine aggregate, finger-scale variation and a bounded wet finish.",
    }),
  );
  [
    ["Beeswax", "#d9a34f", 0.38, 0.55],
    ["Paraffin Wax", "#e2ddcc", 0.23, 0.8],
    ["Soy Candle Wax", "#e4dbc4", 0.5, 0.4],
    ["Sealing Wax", "#732e34", 0.3, 0.25],
  ].forEach(([n, c, r, scattering]) =>
    add(n, "Wax", 13, "wax", c, {
      roughness: r,
      scattering,
      grain: 0.2,
      detailScale: 28,
      translucency: 0.16,
      description:
        "Soft transmission and marbling. Real-time wax approximation.",
    }),
  );
  [
    ["Skin Porcelain", "#dba78b", 0.55, 0.15],
    ["Skin Rose", "#c98e7e", 0.48, 0.3],
    ["Skin Golden", "#b98156", 0.52, 0.2],
    ["Skin Olive", "#a37754", 0.57, 0.18],
    ["Skin Umber", "#704830", 0.55, 0.16],
    ["Skin Deep", "#422d24", 0.5, 0.12],
  ].forEach(([n, c, r, freckles]) =>
    add(n, "Skin", 14, "skin", c, {
      roughness: r,
      freckles,
      scattering: 0.55,
      grain: 0.55,
      detailScale: 55,
      secondaryColor: "#8f322b",
      description:
        "Procedural pores and pigment. Wrap-scattering approximation, not measured skin.",
    }),
  );
  [
    ["PVC-U Rigid", "#d3d4cd", 0.35, 1.54, 0, 0.2],
    ["PVC-P Flexible", "#373d40", 0.55, 1.54, 0, 0.48],
    ["PP Polypropylene", "#c9c6b7", 0.43, 1.49, 0.07, 0.25],
    ["HDPE Polyethylene", "#dedccc", 0.5, 1.53, 0.1, 0.32],
    ["LDPE Polyethylene", "#d7d9c8", 0.4, 1.51, 0.2, 0.22],
    ["PET Polyester", "#b3c5c6", 0.23, 1.57, 0.45, 0.08],
    ["PBT Polyester", "#b5b4a1", 0.42, 1.57, 0, 0.23],
    ["PTFE Fluoropolymer", "#e6e5dc", 0.64, 1.35, 0, 0.2],
    ["POM Acetal", "#e0dfd1", 0.3, 1.48, 0.04, 0.1],
    ["PA6 Nylon", "#d1c3a7", 0.48, 1.53, 0.05, 0.3],
    ["PA66 Glass Filled", "#393d38", 0.66, 1.53, 0, 0.75],
    ["PC Polycarbonate", "#becbc7", 0.12, 1.59, 0.85, 0.02],
    ["PMMA Acrylic", "#c4d0d1", 0.07, 1.49, 0.93, 0.01],
    ["PEEK Polymer", "#9b8a6d", 0.45, 1.65, 0, 0.3],
  ].forEach(([n, c, r, polymerIOR, translucency, g]) =>
    add(n, "Plastic", 9, "engineering", c, {
      roughness: r,
      polymerIOR,
      translucency,
      grain: g,
      detailScale: n.includes("Filled") ? 75 : 130,
      description:
        "Representative molded polymer finish. Optical settings are grade-specific.",
    }),
  );
  [
    ["Cotton Rag Paper", "#dfd8c4", 0.94, 0.8, 0],
    ["Kraft Paper", "#9c7750", 0.89, 0.6, 0],
    ["Coated Art Paper", "#e7e3d7", 0.35, 0.12, 0],
    ["Newsprint Paper", "#bdb69d", 0.88, 0.5, 0],
    ["Corrugated Card", "#96714e", 0.9, 0.7, 1],
    ["Handmade Mulberry Paper", "#d6c6a0", 0.96, 1, 0],
  ].forEach(([n, c, r, g, paperRibs]) =>
    add(n, "Paper", 15, "paper", c, {
      roughness: r,
      grain: g,
      paperRibs,
      detailScale: 90,
      secondaryColor: "#817259",
      description: "Directional pulp fibers and paper tooth. No printed image.",
    }),
  );
  [
    ["Pure Aluminium", "#e8e9e7", 0.18, 0],
    ["24K Gold", "#ffd484", 0.16, 0],
    ["18K Yellow Gold", "#edc283", 0.2, 0],
    ["14K Yellow Gold", "#dcb786", 0.24, 0],
    ["18K Rose Gold", "#e7b09b", 0.21, 0],
    ["18K White Gold", "#dedbd2", 0.17, 0],
    ["Pure Copper", "#efb096", 0.22, 0],
    ["Maraging Steel", "#b3b9bc", 0.29, 0.12],
    ["Cast Steel", "#a0a4a2", 0.56, 0.8],
    ["Stainless Steel", "#ccd0cc", 0.24, 0.1],
    ["Pure Iron", "#b6bab8", 0.33, 0.2],
    ["Rusted Iron", "#a8aca9", 0.36, 0.8],
    ["Chromium", "#d9dce1", 0.08, 0],
    ["Bronze Alloy", "#c39a61", 0.33, 0.15],
    ["Brass Alloy", "#d6b66c", 0.25, 0.08],
    ["Pure Nickel", "#d4cfbb", 0.25, 0.05],
    ["Titanium Metal", "#b4b2aa", 0.34, 0.13],
    ["Pure Zinc", "#c6d0d4", 0.4, 0.25],
    ["Pure Silver", "#f2f0e6", 0.12, 0],
    ["Pure Tin", "#d5d6d0", 0.32, 0.1],
  ].forEach(([n, c, r, g]) =>
    add(
      n,
      "Metal",
      n === "Rusted Iron" ? 16 : 1,
      n === "Rusted Iron" ? "rust" : "bareMetal",
      c,
      {
        metalness: 1,
        roughness: r,
        grain: g,
        detailScale: 65,
        oxidation: 0.7,
        secondaryColor: "#94471f",
        label: n.includes("Gold")
          ? "PRECIOUS ALLOY · APPROXIMATE REFLECTANCE"
          : "CONDUCTOR · PROCEDURAL FINISH",
        description:
          n === "Rusted Iron"
            ? "Dielectric oxide grows over conductive iron, with raised porous crust."
            : "Uncoated conductor. Representative RGB reflectance, not spectral certification.",
      },
    ),
  );
  [
    ["Monocrystalline Solar", "#172742", 0],
    ["Polycrystalline Solar", "#253f83", 1],
    ["Black Solar Module", "#10191d", 2],
  ].forEach(([n, c, panelMode]) =>
    add(n, "Technical", 17, "solar", c, {
      panelMode,
      roughness: 0.2,
      metalness: 0.55,
      coat: 0.95,
      grain: 0.15,
      detailScale: 3,
      secondaryColor: "#bec6c4",
      description:
        "Silicon cells, separators, silver busbars and fine collection fingers beneath glass.",
    }),
  );
  add("Tour Golf Ball", "Technical", 18, "golf", "#e6e5df", {
    roughness: 0.25,
    coat: 0.65,
    detailScale: 7,
    dimpleDepth: 0.012,
    description:
      "Rounded recessed dimples in the surface normal. No displaced silhouette.",
  });
  add("Practice Golf Ball", "Technical", 18, "golf", "#c6d833", {
    roughness: 0.4,
    coat: 0.35,
    detailScale: 6,
    dimpleDepth: 0.016,
    description: "A durable ionomer-like finish with procedural dimples.",
  });
  add("Bull Grain Saddle Leather", "Leather", 11, "leather", "#744326", {
    roughness: 0.53,
    grain: 1,
    detailScale: 19,
    coat: 0.18,
    sheen: 0.15,
    description:
      "Large irregular pebble grain, fine pores and a restrained hide finish.",
  });
  add("Bull Grain Black Leather", "Leather", 11, "leather", "#242221", {
    roughness: 0.47,
    grain: 0.85,
    detailScale: 23,
    coat: 0.24,
    sheen: 0.18,
    description: "Deep pebbled upholstery leather with height-aware patina.",
  });
  [
    ["Raw Selvedge Denim", "#163353", "#bdb8a0", 0.08, 0.35],
    ["Washed Indigo Denim", "#526c81", "#d1c9b1", 0.6, 0.55],
    ["Black Selvedge Denim", "#242a2d", "#aaa496", 0.12, 0.25],
  ].forEach(([n, c, weftColor, denimFade, slub]) =>
    add(n, "Fabric", 4, "denim", c, {
      fabricMode: 0,
      textileClass: "denim",
      warpColor: c,
      weftColor,
      weavePattern: "denim",
      roughness: 0.84,
      fuzz: 0.35,
      sheen: 0.35,
      detailScale: 160,
      weaveAngle: 0,
      denimFade,
      slub,
      description:
        "Warp-faced 3/1 twill, undyed weft, slub yarns and raised-thread fading.",
    }),
  );
  add("Cotton Jersey Knit", "Fabric", 19, "jersey", "#8d9294", {
    roughness: 0.88,
    secondaryColor: "#e2ddd0",
    grain: 0.6,
    detailScale: 22,
    weaveAngle: 0,
    fuzz: 0.35,
    sheen: 0.5,
    description: "Interlocking V-shaped knit loops—not a woven twill.",
  });
  add("Melange Jersey Knit", "Fabric", 19, "jersey", "#525d69", {
    roughness: 0.91,
    secondaryColor: "#b8b7aa",
    grain: 0.75,
    detailScale: 26,
    weaveAngle: 0,
    fuzz: 0.4,
    sheen: 0.55,
    description: "Looped knitted yarns with mixed-fiber melange color.",
  });
  add("LED Pixel Matrix", "Technical", 20, "led", "#111518", {
    roughness: 0.38,
    detailScale: 7,
    emissionColor: "#72beff",
    emissionStrength: 3,
    pixelFill: 0.62,
    secondaryColor: "#15222c",
    description:
      "Procedural light-emitting packages. Emissive appearance; no scene-light emission or bloom.",
  });
  add("Warm COB LED", "Technical", 20, "led", "#b6a76b", {
    roughness: 0.46,
    detailScale: 11,
    emissionColor: "#ffd28b",
    emissionStrength: 2.5,
    pixelFill: 0.86,
    secondaryColor: "#726339",
    description:
      "Warm phosphor-like emitter array. Emissive appearance, not a light-source simulation.",
  });
  return result;
}

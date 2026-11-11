/*==========================================================================================================================================
                                                            MATERIALEXPORT.JS
==========================================================================================================================================*/
/* Zero-build JSON, Frontier .material TOML, and Three.js preset exporter for Material Studio. */

function hexToLinearRgb(hex) {
  const clean = /^#?([a-f\d]{6})$/i.exec(String(hex || "#808080"))?.[1] || "808080";
  return [0, 2, 4].map((offset) => {
    const srgb = parseInt(clean.slice(offset, offset + 2), 16) / 255;
    return srgb <= 0.04045 ? srgb / 12.92 : Math.pow((srgb + 0.055) / 1.055, 2.4);
  });
}

export function exportMaterialJSON(params) {
  return JSON.stringify(
    {
      schema: "alloy.material.v6",
      version: 6,
      material: params,
    },
    null,
    2,
  );
}

export function exportMaterialTOML(params) {
  const rgb = hexToLinearRgb(params.color || "#808080");
  const lines = [
    "Format = 1",
    `Name = ${JSON.stringify(String(params.name || "Untitled Material"))}`,
    "",
    "[[Strata]]",
    `Category = ${params.transmission > 0.1 ? '"Transmissive"' : '"Opaque"'}`,
    `Albedo = [${rgb.map((v) => v.toFixed(6)).join(", ")}]`,
    `Roughness = ${Number(params.roughness ?? 0.5).toFixed(4)}`,
    `Metallic = ${Number(params.metalness ?? 0).toFixed(4)}`,
    `Ior = ${Number(params.ior ?? 1.5).toFixed(4)}`,
    "Coverage = 1.0",
    `Anisotropy = ${Number(params.anisotropy ?? 0).toFixed(4)}`,
  ];

  if (Number(params.coat ?? 0) > 0.01) {
    lines.push(
      "",
      "[[Strata]]",
      'Category = "Coating"',
      "Albedo = [1.0, 1.0, 1.0]",
      `Roughness = ${Number(params.coatRoughness ?? 0.08).toFixed(4)}`,
      "Metallic = 0.0",
      `Ior = ${Number(params.coatIor ?? 1.5).toFixed(4)}`,
      `Coverage = ${Number(params.coat).toFixed(4)}`,
    );
  }

  if (params.type === 0 || Number(params.flakes ?? 0) > 0.01) {
    const palette = (params.colors || ["#ffffff"]).map((c, idx, arr) => {
      const col = hexToLinearRgb(c);
      const t = arr.length > 1 ? idx / (arr.length - 1) : 0;
      return `[${col[0].toFixed(6)}, ${col[1].toFixed(6)}, ${col[2].toFixed(6)}, ${t.toFixed(3)}]`;
    });
    lines.push(
      "",
      "[AutomotivePaint]",
      "Enabled = true",
      "Layers = 3",
      'ColourMode = "Palette"',
      `Distribution = [${Number(params.flakes ?? 0).toFixed(3)}, ${Math.round((params.flakeSize ?? 0.4) * 1000)}, 1, ${Number(params.depth ?? 0.5).toFixed(3)}]`,
      `Palette = [${palette.join(", ")}]`,
    );
  }

  return lines.join("\n") + "\n";
}

export function exportMaterialShader(params) {
  return `// Frontier Procedural Material Preset Module
// Exported from Frontier Experimental Remastered Texture Studio

export const preset = ${JSON.stringify(params, null, 2)};

export function applyToThreeStandardMaterial(targetMaterial) {
  targetMaterial.color.set(preset.color || "#808080");
  targetMaterial.roughness = preset.roughness ?? 0.5;
  targetMaterial.metalness = preset.metalness ?? 0.0;
  if ("clearcoat" in targetMaterial) {
    targetMaterial.clearcoat = preset.coat ?? 0.0;
    targetMaterial.clearcoatRoughness = preset.coatRoughness ?? 0.1;
  }
  targetMaterial.needsUpdate = true;
  return targetMaterial;
}
`;
}

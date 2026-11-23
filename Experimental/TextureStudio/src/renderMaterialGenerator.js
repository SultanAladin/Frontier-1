import { bakeMaterial } from "./bakeMaterial.js";
import {
  normalizeMaterialGenerator,
  generatorChannelValue,
} from "./materialGeneratorModel.js";
import {
  normalizeTextureSource,
  TEXTURE_SOURCE_LIMIT,
} from "./textureSource.js";
export async function renderMaterialGenerator(
  raw,
  { signal, onProgress } = {},
) {
  const value = normalizeMaterialGenerator(raw),
    channel = value.output === "scratch-mask" ? "height" : value.output;
  const baked = await bakeMaterial(value.material, {
    resolution: 256,
    widthMM: value.widthMM,
    channels: [channel],
    signal,
    onProgress,
  });
  if (signal?.aborted)
    throw new DOMException("Generator cancelled", "AbortError");
  const bitmap = await createImageBitmap(
    new Blob([baked.maps[channel]], { type: "image/png" }),
  );
  try {
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = 256;
    const ctx = canvas.getContext("2d");
    ctx.drawImage(bitmap, 0, 0);
    const image = ctx.getImageData(0, 0, 256, 256);
    for (let i = 0; i < image.data.length; i += 4) {
      for (let j = 0; j < 3; j++)
        image.data[i + j] = Math.round(
          generatorChannelValue(image.data[i + j], value) * 255,
        );
      image.data[i + 3] = 255;
    }
    ctx.putImageData(image, 0, 0);
    let dataUrl = canvas.toDataURL("image/png"),
      output = canvas;
    // Noisy 256px PNGs can exceed the portable byte budget; shrink deterministically.
    if (dataUrl.length > TEXTURE_SOURCE_LIMIT) {
      output = document.createElement("canvas");
      output.width = output.height = 224;
      output.getContext("2d").drawImage(canvas, 0, 0, 224, 224);
      dataUrl = output.toDataURL("image/png");
    }
    if (signal?.aborted)
      throw new DOMException("Generator cancelled", "AbortError");
    const result = normalizeTextureSource({
      name: `${value.material.name || "Material"} · ${value.output}`,
      width: output.width,
      height: output.height,
      dataUrl,
      origin: "material-generator",
    });
    if (!result)
      throw new Error(
        "This generator output exceeds the portable source limit.",
      );
    return { ...value, result };
  } finally {
    bitmap.close();
  }
}

import React, { useEffect, useState } from "react";
import { colourMaskMatch, colourMaskSwatch } from "./textureMasks.js";
const COLOURS = [
  "#000000",
  "#ffffff",
  "#b87333",
  "#dd5d5d",
  "#83af77",
  "#6489cc",
  "#b58bd0",
  "#dab278",
];
export default function ColourMaskPreview({ layer, sourceLayer = layer }) {
  const [preview, setPreview] = useState(null),
    mask = layer.mask;
  const sourceSetting = sourceLayer.channels
    .map((id) => sourceLayer.channelSettings[id])
    .find((s) => s.texture || s.generator?.result);
  const source = sourceSetting?.texture || sourceSetting?.generator?.result;
  useEffect(() => {
    let active = true;
    setPreview(null);
    if (!source) return;
    const image = new Image();
    image.onload = () => {
      if (!active) return;
      const canvas = document.createElement("canvas");
      canvas.width = source.width;
      canvas.height = source.height;
      const ctx = canvas.getContext("2d");
      ctx.drawImage(image, 0, 0, canvas.width, canvas.height);
      const pixels = ctx.getImageData(0, 0, canvas.width, canvas.height);
      for (let i = 0; i < pixels.data.length; i += 4) {
        const match =
            colourMaskMatch(
              [
                pixels.data[i] / 255,
                pixels.data[i + 1] / 255,
                pixels.data[i + 2] / 255,
              ],
              mask,
            ) *
            (pixels.data[i + 3] / 255),
          value = mask.enabled ? 1 + ((match - 1) * mask.strength) / 100 : 1;
        const grey = Math.round(value * 255);
        pixels.data[i] = pixels.data[i + 1] = pixels.data[i + 2] = grey;
        pixels.data[i + 3] = 255;
      }
      ctx.putImageData(pixels, 0, 0);
      setPreview(canvas.toDataURL("image/png"));
    };
    image.src = source.dataUrl;
    return () => {
      active = false;
      image.onload = null;
    };
  }, [
    source?.dataUrl,
    mask.color,
    mask.tolerance,
    mask.softness,
    mask.inverted,
    mask.strength,
    mask.enabled,
  ]);
  return (
    <div className="tp-colour-mask-preview" aria-label="Colour mask preview">
      {preview ? (
        <>
          <div>
            <img src={source.dataUrl} alt="Colour mask source texture" />
            <img src={preview} alt="Colour-selection mask preview" />
          </div>
          <small>
            RGB selection on {source?.name} · source pixels only, not teapot
            coverage
          </small>
        </>
      ) : (
        <>
          <div className="tp-colour-match-swatches">
            {COLOURS.map((color) => (
              <span
                key={color}
                title={`${color}: ${Math.round(colourMaskSwatch(color, mask) * 100)}% match`}
              >
                <i style={{ background: color }} />
                <b
                  style={{
                    background: `rgb(${Math.round(colourMaskSwatch(color, mask) * 255)},${Math.round(colourMaskSwatch(color, mask) * 255)},${Math.round(colourMaskSwatch(color, mask) * 255)})`,
                  }}
                />
              </span>
            ))}
          </div>
          <small>
            RGB colour matching examples · import a channel texture{" "}
            {layer.kind === "folder" ? "into a child layer " : ""}for a source
            preview
          </small>
        </>
      )}
    </div>
  );
}

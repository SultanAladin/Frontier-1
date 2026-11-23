import React, { useMemo } from "react";
import { rasterizePointGradient } from "./pointGradient.js";
export default function GradientSliceImage({ gradient, mask = null, alt }) {
  const src = useMemo(() => {
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = 32;
    const ctx = canvas.getContext("2d"),
      image = ctx.createImageData(32, 32);
    image.data.set(rasterizePointGradient(gradient, { edge: 32, mask }));
    ctx.putImageData(image, 0, 0);
    return canvas.toDataURL("image/png");
  }, [gradient, mask]);
  return (
    <img
      src={src}
      alt={alt}
      title="Object-space field slice; not a painted UV texture"
    />
  );
}

import React, { useEffect, useState } from "react";
import { RenderAnalyticalThumbnailDataUrl } from "./ModelThumbnailRenderer.js";

let SyncRenderBudget = 10;

export function AnalyticalShapeThumbnail({
  PropId = null,
  Shape = "cone",
  Scale = null,
  Material = null,
  FaceColors = null,
  Size = 48,
  ClassName = "",
  Style = undefined,
}) {
  const RenderPx = Math.min(96, Math.max(52, Math.round(Size * 1.6)));
  const [Src, SetSrc] = useState(() => {
    if (SyncRenderBudget > 0) {
      SyncRenderBudget -= 1;
      return RenderAnalyticalThumbnailDataUrl({
        PropId,
        Shape,
        Scale,
        Material,
        FaceColors,
        Size: RenderPx,
      });
    }
    return "";
  });

  useEffect(() => {
    let Cancelled = false;
    const Handle = setTimeout(() => {
      if (Cancelled) return;
      const Next = RenderAnalyticalThumbnailDataUrl({
        PropId,
        Shape,
        Scale,
        Material,
        FaceColors,
        Size: RenderPx,
      });
      if (!Cancelled && Next && Next !== Src) SetSrc(Next);
    }, 0);
    return () => {
      Cancelled = true;
      clearTimeout(Handle);
    };
  }, [PropId, Shape, RenderPx, Scale, Material, FaceColors]);

  if (!Src) {
    return (
      <span
        className={"analytical-thumb-fallback " + ClassName}
        data-thumb-shape={PropId || Shape}
        style={{
          display: "inline-flex",
          alignItems: "center",
          justifyContent: "center",
          width: Size,
          height: Size,
          background: "transparent",
          color: "rgba(220,224,232,0.55)",
          fontSize: Math.max(8, Math.round(Size * 0.28)),
          flexShrink: 0,
          ...Style,
        }}
      >
        ◇
      </span>
    );
  }

  return (
    <img
      src={Src}
      alt=""
      width={Size}
      height={Size}
      draggable={false}
      data-thumb-shape={PropId || Shape}
      className={"analytical-thumb-img " + ClassName}
      style={{
        width: Size,
        height: Size,
        objectFit: "contain",
        background: "transparent",
        display: "block",
        flexShrink: 0,
        pointerEvents: "none",
        ...Style,
      }}
    />
  );
}

export default AnalyticalShapeThumbnail;

//============================================================================================================================================
//                                                           WORKSPACESURFACE.JSX
//============================================================================================================================================
// 🧰 Pieces shared by every edit-mode workspace: the repainting canvas, orbit dragging, downloads and the island hue.

import React, { useEffect, useRef } from "react";

// Every Values key the primitive surface reads; a change to any of them means a new mesh.
export const GeometryKeys = ["Geometry Size X", "Geometry Size Y", "Geometry Size Z", "Geometry Radius", "Geometry Height", "Geometry Segments", "Geometry Rings", "Geometry Major Radius", "Geometry Minor Radius", "Geometry Sides", "Geometry Caps"];
export const Hue = (Index) => (Index * 137.508) % 360;
export const Clamp = (Amount, Minimum, Maximum) => Math.max(Minimum, Math.min(Maximum, Amount));

export function Download(Name, Text, Type = "text/plain") {
  const Link = document.createElement("a");
  Link.href = URL.createObjectURL(new Blob([Text], { type: Type }));
  Link.download = Name;
  Link.click();
  setTimeout(() => URL.revokeObjectURL(Link.href), 2000);
}

// A canvas that repaints on size change and hands the painter CSS-pixel dimensions.
export function Surface({ Paint, Label, Class = "", Handlers = {}, Deps = [] }) {
  const Ref = useRef(null);
  const PaintRef = useRef(Paint);
  PaintRef.current = Paint;
  const Draw = () => {
    const Canvas = Ref.current;
    if (!Canvas) return;
    const Width = Canvas.clientWidth || 300, Height = Canvas.clientHeight || 300, Ratio = Math.min(devicePixelRatio || 1, 2);
    if (Canvas.width !== Math.round(Width * Ratio) || Canvas.height !== Math.round(Height * Ratio)) {
      Canvas.width = Math.round(Width * Ratio);
      Canvas.height = Math.round(Height * Ratio);
    }
    const Brush = Canvas.getContext("2d");
    Brush.setTransform(Ratio, 0, 0, Ratio, 0, 0);
    Brush.clearRect(0, 0, Width, Height);
    PaintRef.current(Brush, Width, Height);
  };
  useEffect(() => {
    const Observer = new ResizeObserver(Draw);
    Observer.observe(Ref.current);
    return () => Observer.disconnect();
  }, []);
  useEffect(Draw);
  return <canvas ref={Ref} className={"ws-surface " + Class} role="img" aria-label={Label} {...Handlers} />;
}

// Orbit + click handling shared by every workspace view: drag turns the view, a click without movement is passed to `Pick`.
export function useOrbit(Workspace, SetWorkspace) {
  const Drag = useRef(null);
  return {
    onPointerDown: (Event) => {
      Drag.current = { X: Event.clientX, Y: Event.clientY, Yaw: Workspace.Yaw ?? 0.7, Pitch: Workspace.Pitch ?? 0.45, Moved: false };
      Event.currentTarget.setPointerCapture(Event.pointerId);
    },
    onPointerMove: (Event) => {
      const Start = Drag.current;
      if (!Start) return;
      const Dx = Event.clientX - Start.X, Dy = Event.clientY - Start.Y;
      if (Math.hypot(Dx, Dy) > 3) Start.Moved = true;
      if (Start.Moved) SetWorkspace((Previous) => ({ ...Previous, Yaw: Start.Yaw + Dx * 0.01, Pitch: Clamp(Start.Pitch + Dy * 0.01, -1.45, 1.45) }));
    },
    onWheel: (Event) => SetWorkspace((Previous) => ({ ...Previous, Zoom: Clamp((Previous.Zoom ?? 1) * (Event.deltaY > 0 ? 0.9 : 1.1), 0.4, 4) })),
    End: (Event, Pick) => {
      const Start = Drag.current;
      Drag.current = null;
      if (Start && !Start.Moved && Pick) Pick(Event);
    },
  };
}


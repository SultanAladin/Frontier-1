import React, { useEffect, useRef, useState } from "react";

// A non-modal editor window. Material ownership stays pinned while scene selection changes.
export default function ShaderPanel({ Children, Close, Dock }) {
  const [Position, Move] = useState({
      X: Math.max(12, (innerWidth - 1120) / 2),
      Y: Math.max(40, (innerHeight - 820) / 2),
    }),
    Drag = useRef(null),
    Window = useRef(null),
    Opener = useRef(document.activeElement);
  useEffect(() => {
    Window.current?.focus();
    const Resize = () =>
      Move((Previous) => ({
        X: Math.max(
          0,
          Math.min(
            Previous.X,
            Math.max(0, innerWidth - (Window.current?.offsetWidth || 200) - 12),
          ),
        ),
        Y: Math.max(
          0,
          Math.min(
            Previous.Y,
            Math.max(
              0,
              innerHeight - (Window.current?.offsetHeight || 80) - 12,
            ),
          ),
        ),
      }));
    window.addEventListener("resize", Resize);
    return () => {
      window.removeEventListener("resize", Resize);
      if (Opener.current?.isConnected && Opener.current.getClientRects().length)
        Opener.current.focus();
    };
  }, []);
  return (
    <section
      ref={Window}
      className="shader-window"
      role="dialog"
      aria-label="ShaderEditor window"
      aria-modal="false"
      tabIndex={-1}
      style={{ left: Position.X, top: Position.Y }}
      onKeyDown={(Event) => {
        if (Event.key === "Escape") {
          Event.stopPropagation();
          Close();
        }
      }}
    >
      <header
        className="shader-window-title"
        onPointerDown={(Event) => {
          if (Event.button !== 0 || Event.target.closest("button")) return;
          Drag.current = {
            X: Event.clientX,
            Y: Event.clientY,
            ...Position,
            StartX: Position.X,
            StartY: Position.Y,
          };
          Drag.current.X = Event.clientX;
          Drag.current.Y = Event.clientY;
          Event.currentTarget.setPointerCapture(Event.pointerId);
        }}
        onPointerMove={(Event) => {
          const Start = Drag.current;
          if (!Start) return;
          Move({
            X: Math.max(
              0,
              Math.min(
                Math.max(
                  0,
                  innerWidth - (Window.current?.offsetWidth || 200) - 12,
                ),
                Start.StartX + Event.clientX - Start.X,
              ),
            ),
            Y: Math.max(
              0,
              Math.min(
                Math.max(
                  0,
                  innerHeight - (Window.current?.offsetHeight || 80) - 12,
                ),
                Start.StartY + Event.clientY - Start.Y,
              ),
            ),
          });
        }}
        onPointerUp={() => (Drag.current = null)}
        onPointerCancel={() => (Drag.current = null)}
      >
        <span className="shader-document">
          ◈ ShaderEditor<span>material.surface</span>
        </span>
        <div className="shader-docking">
          <span>Dock to</span>
          {["Left", "Centre", "Right"].map((Side) => (
            <button
              key={Side}
              aria-label={"Dock ShaderEditor " + Side}
              onClick={() => Dock(Side)}
            >
              {Side}
            </button>
          ))}
          <button aria-label="Close ShaderEditor window" onClick={Close}>
            ×
          </button>
        </div>
      </header>
      <div className="shader-window-content">{Children}</div>
      <footer className="shader-window-footer">
        <span>HTML AUTHORING · C++ MIRROR DEFERRED</span>
        <span>Drag title to move · corner to resize</span>
      </footer>
    </section>
  );
}

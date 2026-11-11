import React, { useEffect, useRef, useState } from "react";
import "./AssetPanel.css";

// Shared top/bottom gesture model, adapted from the supplied UI.html reference.
export default function DrawerPanel({
  Edge = "top",
  Open,
  Toggle,
  Activate,
  Label,
  Name,
  ClassName = "",
  Escape,
  children,
}) {
  const [Extent, Resize] = useState(innerHeight - 36),
    [Rest, Snap] = useState(Open ? 1 : 0),
    [Travel, Slide] = useState(null),
    [Offset, Shift] = useState(0);
  const Gesture = useRef(null),
    Root = useRef(null),
    Handle = useRef(null),
    Opener = useRef(null),
    SuppressClick = useRef(false);
  const Direction = Edge === "top" ? 1 : -1;
  const Progress = Travel ?? Rest;
  useEffect(() => {
    const Resized = () => {
      Gesture.current = null;
      Slide(null);
      Resize(innerHeight - 36);
      Shift((Value) =>
        Math.max(
          -(innerWidth - 220) / 2,
          Math.min((innerWidth - 220) / 2, Value),
        ),
      );
    };
    window.addEventListener("resize", Resized);
    return () => window.removeEventListener("resize", Resized);
  }, []);
  useEffect(() => {
    if (!Gesture.current) {
      Snap(Open ? 1 : 0);
      Slide(null);
    }
    if (Open) {
      Opener.current = document.activeElement;
      Handle.current?.focus({ preventScroll: true });
    } else if (Opener.current?.isConnected)
      Opener.current.focus({ preventScroll: true });
  }, [Open]);
  const Settle = (Next) => {
    Snap(Next);
    Slide(null);
    Toggle(Next > 0);
  };
  const Begin = (Event, FromHandle = false) => {
    if (
      Event.button !== 0 ||
      Gesture.current ||
      (!FromHandle &&
        (!Open ||
          (Event.target.closest(
            "button,input,select,textarea,a,[contenteditable=true],[data-drawer-scroll]",
          ) &&
            !Event.target.closest(".drawer-page-grip"))))
    )
      return;
    Activate?.();
    Gesture.current = {
      Id: Event.pointerId,
      X: Event.clientX,
      Y: Event.clientY,
      Start: Rest,
      Offset,
      Axis: null,
      FromHandle,
      At: performance.now(),
      Samples: [[performance.now(), Event.clientY]],
      Current: Rest,
    };
    if (FromHandle) Root.current.setPointerCapture(Event.pointerId);
  };
  const Move = (Event) => {
    const Drag = Gesture.current;
    if (!Drag || Drag.Id !== Event.pointerId) return;
    const DX = Event.clientX - Drag.X,
      DY = Event.clientY - Drag.Y;
    if (!Drag.Axis && Math.hypot(DX, DY) > 7) {
      Drag.Axis = Math.abs(DY) > Math.abs(DX) ? "Y" : "X";
      if (Drag.Axis === "Y") Root.current.setPointerCapture(Event.pointerId);
    }
    if (Drag.Axis === "X" && Drag.FromHandle)
      Shift(
        Math.max(
          -(innerWidth - 220) / 2,
          Math.min((innerWidth - 220) / 2, Drag.Offset + DX),
        ),
      );
    if (Drag.Axis === "Y") {
      Event.preventDefault();
      Drag.Current = Math.max(
        0,
        Math.min(1, Drag.Start + (Direction * DY) / Extent),
      );
      Slide(Drag.Current);
      const Now = performance.now();
      Drag.Samples.push([Now, Event.clientY]);
      while (Drag.Samples.length > 2 && Now - Drag.Samples[0][0] > 120)
        Drag.Samples.shift();
    }
  };
  const End = (Event, Cancelled = false) => {
    const Drag = Gesture.current;
    if (!Drag || Drag.Id !== Event.pointerId) return;
    // Keep the pointer gesture alive through Toggle's render, so half-open isn't replaced by full-open.
    let Next = Drag.Start;
    if (!Cancelled && Drag.Axis === "Y") {
      const First = Drag.Samples[0],
        Last = Drag.Samples.at(-1),
        Idle = performance.now() - Last[0];
      const Velocity =
        Idle < 120
          ? ((Direction * (Last[1] - First[1])) /
              Math.max(16, Last[0] - First[0])) *
            1000
          : 0;
      Next = Drag.Current < 0.25 ? 0 : Drag.Current > 0.75 ? 1 : 0.5;
      if (Math.abs(Event.clientY - Drag.Y) > 12 && Math.abs(Velocity) > 300)
        Next =
          Math.abs(Velocity) > 1000
            ? Velocity > 0
              ? 1
              : 0
            : Math.max(
                0,
                Math.min(1, Drag.Start + (Velocity > 0 ? 0.5 : -0.5)),
              );
    } else if (!Cancelled && !Drag.Axis && Drag.FromHandle) Next = Open ? 0 : 1;
    SuppressClick.current = !!Drag.Axis;
    Snap(Next);
    Slide(null);
    Toggle(Next > 0);
    Gesture.current = null;
    // React applies external Open on the next render; retain an intentional half-open snap.
    Pending.current = Next > 0 !== Open ? Next : null;
  };
  const Pending = useRef(null);
  useEffect(() => {
    if (Pending.current !== null) {
      Snap(Pending.current);
      Pending.current = null;
    }
  }, [Open]);
  const Keyboard = (Event) => {
    if (Event.key === "Escape" && Open) {
      Event.stopPropagation();
      Escape ? Escape() : Settle(0);
      return;
    }
    if (Event.key === "Tab" && Open) {
      const Nodes = [
        ...Root.current.querySelectorAll(
          'button:not(:disabled),input:not(:disabled),select:not(:disabled),textarea:not(:disabled),a[href],[tabindex="0"]',
        ),
      ].filter(
        (Node) => Node.getClientRects().length && !Node.closest("[inert]"),
      );
      const First = Nodes[0],
        Last = Nodes.at(-1);
      if (Event.shiftKey && document.activeElement === First) {
        Event.preventDefault();
        Last?.focus();
      } else if (!Event.shiftKey && document.activeElement === Last) {
        Event.preventDefault();
        First?.focus();
      }
    }
  };
  return (
    <section
      ref={Root}
      className={`drawer-shell ${Edge} ${ClassName} ${Open || Progress > 0 ? "open" : ""} ${Travel !== null ? "dragging" : ""}`}
      data-drawer={Edge}
      data-stage={Progress === 0 ? "closed" : Progress === 1 ? "full" : "half"}
      style={{
        "--drawer-visible": `${Math.max(0, Extent * Progress)}px`,
        transform: `translateY(${(Edge === "top" ? -1 : 1) * Extent * (1 - Progress)}px)`,
      }}
      onPointerDown={(Event) => Begin(Event)}
      onPointerMove={Move}
      onPointerUp={End}
      onPointerCancel={(Event) => End(Event, true)}
      onLostPointerCapture={(Event) => {
        if (Gesture.current && Event.target === Root.current) End(Event, true);
      }}
      onKeyDown={Keyboard}
    >
      <div
        className="drawer-surface"
        role={Edge === "bottom" ? "dialog" : "region"}
        aria-label={Label + " drawer"}
        aria-modal={Edge === "bottom" && Open ? "true" : undefined}
        aria-hidden={!Open && Travel === null ? "true" : undefined}
        inert={!Open && Travel === null ? "" : undefined}
      >
        {children}
        <button
          className="drawer-page-grip"
          aria-label={"Close " + Label + " using page grip"}
          onClick={() => {
            if (!SuppressClick.current) Settle(0);
            SuppressClick.current = false;
          }}
        >
          <i />
          <span>Drag the page or handle to close</span>
        </button>
      </div>
      <button
        ref={Handle}
        className={
          "drawer-handle " +
          (Edge === "top" ? "notch-handle" : "asset-notch-handle")
        }
        aria-label={Name}
        aria-expanded={Open}
        title={Label + " · drag to open/close · drag sideways to reposition"}
        style={{ left: `calc(50% - 110px + ${Offset}px)` }}
        onPointerDown={(Event) => {
          Event.stopPropagation();
          Begin(Event, true);
        }}
        onClick={(Event) => {
          if (Event.detail === 0) Settle(Open ? 0 : 1);
          SuppressClick.current = false;
        }}
        onKeyDown={(Event) => {
          if (Event.key === "Enter" || Event.key === " ") {
            Event.preventDefault();
            Settle(Open ? 0 : 1);
          }
        }}
      >
        <svg viewBox="0 0 400 36" preserveAspectRatio="none">
          <path d="M0 0C15 0 20 6 25 15L35 28C40 34 45 36 52 36H348C355 36 360 34 365 28L375 15C380 6 385 0 400 0Z" />
        </svg>
        <span>{Label}</span>
      </button>
    </section>
  );
}

//==========================================================================================================================================
//                                              INSPECTORCAROUSEL.JSX
//==========================================================================================================================================
// Settings carousel for the Frontier World Mirror inspector: paginates a subject's settings
// into named menus (A | B | C) behind a segmented control with prev/next arrows, pointer swipe,
// arrow-key navigation, and scrollable per-page content. A single-page request renders its
// content directly so sparse subjects keep a plain layout.
//
// The page viewport is the single scroll container: it fills a fixed-height frame exactly
// (absolute inset), so long pages scroll instead of clipping at the panel edge. When the
// active page extends below the fold a soft "more below" fade appears at the bottom edge,
// and scroll-snap (proximity) lets cards settle against the top edge.
//==========================================================================================================================================

import React, { useEffect, useRef, useState } from "react";
import "./InspectorCarousel.css";

const PageLetter = (Index) => String.fromCharCode(65 + Math.min(25, Index));
const EDITOR_TAGS = new Set(["INPUT", "SELECT", "TEXTAREA"]);

export function InspectorCarousel({
  Pages,
  DefaultIndex = 0,
  Region = "Settings menus",
}) {
  const Safe = Array.isArray(Pages) ? Pages : [];
  const Count = Safe.length;
  const Clamped = Math.max(0, Math.min(DefaultIndex, Count - 1));
  const [Index, Select] = useState(Clamped);
  const [Dragging, SetDragging] = useState(false);
  const [DragOffset, SetDragOffset] = useState(0);
  const [MoreBelow, SetMoreBelow] = useState(false);
  const ViewportRef = useRef(null);
  const DragState = useRef(null);
  const Active = Math.max(0, Math.min(Index, Count - 1));

  const Step = (Delta) =>
    Select((Previous) => Math.max(0, Math.min(Count - 1, Previous + Delta)));

  // Recomputes whether the active page continues below the visible fold, so the
  // bottom-edge fade only shows when there is actually more content to scroll to.
  const UpdateMoreBelow = () => {
    const El = ViewportRef.current;
    if (!El) return;
    SetMoreBelow(El.scrollTop + El.clientHeight < El.scrollHeight - 4);
  };
  const OnScroll = () => UpdateMoreBelow();

  useEffect(() => {
    if (Count <= 1) return;
    const El = ViewportRef.current;
    if (!El) return;
    // A new menu starts at the top so a long page never strands the user mid-scroll.
    El.scrollTop = 0;
    UpdateMoreBelow();
    let Observed = null;
    if (typeof ResizeObserver === "function") {
      Observed = new ResizeObserver(() => UpdateMoreBelow());
      if (El.firstElementChild) Observed.observe(El.firstElementChild);
      Observed.observe(El);
    }
    window.addEventListener("resize", UpdateMoreBelow);
    return () => {
      Observed?.disconnect();
      window.removeEventListener("resize", UpdateMoreBelow);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [Active, Count]);

  if (Count <= 1) return <>{Count === 1 ? Safe[0]?.Node : null}</>;

  const OnPointerDown = (Event) => {
    if (Event.pointerType === "mouse" && Event.button !== 0) return;
    DragState.current = {
      Id: Event.pointerId,
      X: Event.clientX,
      Y: Event.clientY,
      Horizontal: false,
    };
  };
  const OnPointerMove = (Event) => {
    const State = DragState.current;
    if (!State || State.Id !== Event.pointerId) return;
    const DX = Event.clientX - State.X,
      DY = Event.clientY - State.Y;
    if (!State.Horizontal) {
      if (Math.abs(DX) < 8 || Math.abs(DX) <= Math.abs(DY)) return;
      State.Horizontal = true;
      SetDragging(true);
      ViewportRef.current?.setPointerCapture(Event.pointerId);
    }
    const Edge =
      (Active === 0 && DX > 0) || (Active === Count - 1 && DX < 0) ? 0.35 : 1;
    SetDragOffset(DX * Edge);
  };
  const EndDrag = (Committed = true) => {
    const State = DragState.current;
    if (!State) return;
    DragState.current = null;
    if (!State.Horizontal) return;
    const Width = ViewportRef.current?.clientWidth || 320;
    const DX = DragOffset;
    SetDragging(false);
    SetDragOffset(0);
    if (!Committed) return;
    if (DX < -Math.min(56, Width * 0.22)) Step(1);
    else if (DX > Math.min(56, Width * 0.22)) Step(-1);
  };
  const OnPointerUp = (Event) => EndDrag(true);
  const OnPointerCancel = (Event) => EndDrag(false);
  const OnKeyDown = (Event) => {
    if (EDITOR_TAGS.has(Event.target?.tagName)) return;
    if (Event.key === "ArrowRight") {
      Event.preventDefault();
      Step(1);
    } else if (Event.key === "ArrowLeft") {
      Event.preventDefault();
      Step(-1);
    } else if (Event.key === "Home") {
      Event.preventDefault();
      Select(0);
    } else if (Event.key === "End") {
      Event.preventDefault();
      Select(Count - 1);
    }
  };

  return (
    <div
      className="insp-carousel"
      data-page-count={Count}
      data-page-active={Active}
      onPointerDown={OnPointerDown}
      onPointerMove={OnPointerMove}
      onPointerUp={OnPointerUp}
      onPointerCancel={OnPointerCancel}
      onKeyDown={OnKeyDown}
    >
      <div className="insp-carousel-head" role="tablist" aria-label={Region}>
        <button
          type="button"
          className="insp-carousel-arrow"
          aria-label="Previous settings menu"
          disabled={Active === 0}
          onClick={() => Step(-1)}
        >
          ‹
        </button>
        <div className="insp-carousel-segments">
          {Safe.map((Page, Pos) => (
            <button
              key={Page.Id || Pos}
              type="button"
              role="tab"
              aria-selected={Pos === Active}
              aria-controls={`insp-page-${Page.Id || Pos}`}
              className={"insp-segment " + (Pos === Active ? "active" : "")}
              onClick={() => Select(Pos)}
            >
              <span className="seg-key">{PageLetter(Pos)}</span>
              <span className="seg-label">{Page.Label}</span>
            </button>
          ))}
        </div>
        <button
          type="button"
          className="insp-carousel-arrow"
          aria-label="Next settings menu"
          disabled={Active === Count - 1}
          onClick={() => Step(1)}
        >
          ›
        </button>
        <span className="insp-carousel-count" aria-hidden="true">
          {Active + 1} / {Count}
        </span>
      </div>
      <div className="insp-carousel-scroll">
        <div
          ref={ViewportRef}
          className={
            "insp-carousel-viewport" +
            (MoreBelow ? " has-more-below" : "")
          }
          onScroll={OnScroll}
        >
          <div
            className={
              "insp-carousel-track" + (Dragging ? " dragging" : "")
            }
            style={{
              transform: `translateX(calc(${-Active * 100}% + ${DragOffset}px))`,
            }}
          >
            {Safe.map((Page, Pos) => (
              <div
                key={Page.Id || Pos}
                id={`insp-page-${Page.Id || Pos}`}
                role="tabpanel"
                aria-label={Page.Label + " settings"}
                aria-hidden={Pos !== Active}
                className={
                  "insp-carousel-page " +
                  (Pos === Active ? "active" : Pos < Active ? "left" : "right")
                }
                inert={Pos !== Active ? "" : undefined}
              >
                {Page.Node}
              </div>
            ))}
          </div>
        </div>
        {MoreBelow && (
          <div
            className="insp-carousel-fade"
            aria-hidden="true"
            title="Scroll for more settings"
          />
        )}
      </div>
    </div>
  );
}

export default InspectorCarousel;

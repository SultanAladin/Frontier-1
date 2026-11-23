//============================================================================================================================================
//                                                               ORBITMENU.JSX
//============================================================================================================================================
// 🧩 The Orbit menu (Follow-up 59/60): one round button pinned to the bottom right of the inspector, ringed in the colours of its pages
// (the open page's arc lit). Pressing it opens the pages in a circle around a middle that tells where you are; picking one turns to it
// and closes the ring. ← and → on the button (or a page) move between pages, Esc or a press outside closes the ring. The pages it
// moves between always come first in the inspector; the menu never sits above them.
// Used by the Showcase objects' categories (PrimitiveInspector.jsx) and by the lettered inspector pages (InspectorTabs.jsx), each with
// its own Look: the look sets the page buttons' shape and the ring's backdrop, so no two inspectors look alike.

import React, { useEffect, useRef, useState } from "react";
import "./OrbitMenu.css";

// Pages: [{ Id, Label, Tone, Inside (the button's face), ClassName, Style, Extra (more button props) }].
export function OrbitMenu({ Owner, Look = "plain", Pages, Index, Pick, Middle = null, Face = null, Caption = null }) {
  const [Open, Show] = useState(false);
  const Host = useRef(null);
  const Count = Pages.length;
  const Current = Pages[Math.max(0, Math.min(Count - 1, Index))];
  // A press anywhere outside the menu closes the ring.
  useEffect(() => {
    if (!Open) return undefined;
    const Away = (Event) => {
      if (!Host.current?.contains(Event.target)) Show(false);
    };
    document.addEventListener("pointerdown", Away, true);
    return () => document.removeEventListener("pointerdown", Away, true);
  }, [Open]);
  const Go = (At) => {
    Pick(At);
    Show(false);
  };
  const Keys = (Event) => {
    const Step = Event.key === "ArrowRight" ? 1 : Event.key === "ArrowLeft" ? -1 : 0;
    if (Step) {
      Event.preventDefault();
      Pick((Index + Step + Count) % Count);
    }
    if (Event.key === "Escape") Show(false);
  };
  const Gap = Count > 6 ? 3 : 5;
  const Ring = Pages.map((Each, At) => `${At === Index ? Each.Tone : Each.Tone + "45"} ${(At / Count) * 360 + Gap}deg ${((At + 1) / Count) * 360 - Gap}deg, transparent 0 ${((At + 1) / Count) * 360}deg`).join(", ");
  const Reach = Count > 6 ? 84 : 82;
  return (
    <nav ref={Host} className={"orbit-menu orbit-" + Look + (Open ? " open" : "")} aria-label={Owner + " pages"} data-orbit={Owner} data-orbit-look={Look} style={{ "--orbit-tone": Current.Tone }}>
      {Open && (
        <div className="orbit-ring" role="tablist" aria-label={Owner + " pages"}>
          <div className="orbit-middle">{Middle}</div>
          {Pages.map((Each, At) => {
            const Turn = (At / Count) * Math.PI * 2 - Math.PI / 2;
            return (
              <button
                key={Each.Id}
                type="button"
                role="tab"
                aria-selected={At === Index}
                aria-label={Each.Label}
                title={Each.Label}
                className={"orbit-page" + (Each.ClassName ? " " + Each.ClassName : "") + (At === Index ? " active open" : "")}
                style={{ ...(Each.Style || {}), "--orbit-tone": Each.Tone, left: `calc(50% + ${(Math.cos(Turn) * Reach).toFixed(1)}px)`, top: `calc(50% + ${(Math.sin(Turn) * Reach).toFixed(1)}px)` }}
                onClick={() => Go(At)}
                onKeyDown={Keys}
                {...(Each.Extra || {})}
              >
                {Each.Inside}
              </button>
            );
          })}
        </div>
      )}
      <button type="button" className="orbit-button" aria-label={(Open ? "Close pages · " : "Pages · ") + Current.Label} aria-expanded={Open} onClick={() => Show(!Open)} onKeyDown={Keys} style={{ background: `conic-gradient(${Ring})` }}>
        <span>
          {Open ? (
            <svg viewBox="0 0 16 16" width="16" height="16" aria-hidden="true">
              <path d="M4 4l8 8M12 4l-8 8" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" />
            </svg>
          ) : (
            Face
          )}
        </span>
      </button>
      <span className="orbit-caption">{Caption ?? Current.Label}</span>
    </nav>
  );
}

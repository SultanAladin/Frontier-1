//============================================================================================================================================
//                                                               GIZMOPANEL.JSX
//============================================================================================================================================
// 🧩 Transform gizmo preferences (Project Zero specification §13, Follow-up 48): Universal / Translate / Rotate / Scale on W / E / R / Q,
// snapping with 0.25 m, 0.10 and 5° steps, and the pick path (GPU pick, CPU fallback). Editor-wide; kept in this browser.
// 📝 The HTML viewport draws 2D markers, so no gizmo is drawn and no GPU pick runs here.

import React, { useEffect, useState } from "react";

export const GizmoModes = [
  ["universal", "Universal", "W"],
  ["translate", "Translate", "E"],
  ["rotate", "Rotate", "R"],
  ["scale", "Scale", "Q"],
];
export const SnapDefaults = { Translate: 0.25, Scale: 0.1, Rotate: 5 };
const Storage = "Frontier.ProjectZeroGizmo.v1";
const Defaults = { Mode: "universal", Snap: false, Steps: { ...SnapDefaults } };

function Restore() {
  try {
    const Saved = JSON.parse(localStorage.getItem(Storage)) || {};
    return {
      Mode: GizmoModes.some(([Id]) => Id === Saved.Mode) ? Saved.Mode : Defaults.Mode,
      Snap: !!Saved.Snap,
      Steps: { ...SnapDefaults, ...(Saved.Steps || {}) },
    };
  } catch {
    return { ...Defaults };
  }
}

// Shared across every Transform card on the page.
let Current = Restore();
const Listeners = new Set();
function Publish(Next) {
  Current = Next;
  try {
    localStorage.setItem(Storage, JSON.stringify(Next));
  } catch {}
  Listeners.forEach((Listener) => Listener(Next));
}
export const GizmoPreferences = () => Current;

// W / E / R / Q switch the gizmo outside text fields.
if (typeof window !== "undefined")
  window.addEventListener("keydown", (Event) => {
    if (Event.ctrlKey || Event.metaKey || Event.altKey || Event.repeat) return;
    if (Event.target?.closest?.('input, textarea, select, [contenteditable]:not([contenteditable="false"])')) return;
    const Mode = GizmoModes.find(([, , Key]) => Event.code === "Key" + Key);
    if (Mode) Publish({ ...Current, Mode: Mode[0] });
  });

export default function GizmoPanel({ Disabled = false }) {
  const [Preferences, Store] = useState(Current);
  useEffect(() => {
    Listeners.add(Store);
    return () => Listeners.delete(Store);
  }, []);
  const Step = (Key, Label, Unit, Minimum, Maximum, Increment) => (
    <label className="gizmo-step">
      <span>{Label}</span>
      <input
        type="number"
        aria-label={Label + " snap step"}
        min={Minimum}
        max={Maximum}
        step={Increment}
        value={Preferences.Steps[Key]}
        disabled={Disabled || !Preferences.Snap}
        onChange={(Event) => {
          const Next = Math.min(Maximum, Math.max(Minimum, +Event.target.value || SnapDefaults[Key]));
          Publish({ ...Preferences, Steps: { ...Preferences.Steps, [Key]: Next } });
        }}
      />
      <small>{Unit}</small>
    </label>
  );
  return (
    <section className="gizmo-card" data-card="Gizmo" aria-label="Transform gizmo">
      <header>
        <h3>Gizmo</h3>
        <span>{GizmoModes.find(([Id]) => Id === Preferences.Mode)[1].toUpperCase()}</span>
      </header>
      <div className="gizmo-modes" role="radiogroup" aria-label="Gizmo mode">
        {GizmoModes.map(([Id, Label, Key]) => (
          <button key={Id} type="button" role="radio" disabled={Disabled} aria-checked={Preferences.Mode === Id} onClick={() => Publish({ ...Preferences, Mode: Id })}>
            {Label}
            <kbd>{Key}</kbd>
          </button>
        ))}
      </div>
      <label className="gizmo-snap">
        <span>Snap</span>
        <input type="checkbox" aria-label="Gizmo snap" disabled={Disabled} checked={Preferences.Snap} onChange={(Event) => Publish({ ...Preferences, Snap: Event.target.checked })} />
      </label>
      <div className="gizmo-steps">
        {Step("Translate", "Move", "m", 0.01, 10, 0.05)}
        {Step("Scale", "Scale", "×", 0.01, 1, 0.01)}
        {Step("Rotate", "Rotate", "°", 1, 90, 1)}
      </div>
      <p>Pick: GPU ID buffer, CPU ray fallback when the GPU pick is unavailable. HTML preview: preference only; no gizmo is drawn.</p>
    </section>
  );
}

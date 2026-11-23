//============================================================================================================================================
//                                                             FRACTUREPANEL.JSX
//============================================================================================================================================
// 📦 Selected-object fracture enablement, execution choice and expansion.

import React from "react";
import { FractureGlyph } from "../FractureEditor/FractureProjection.js";

const FractureArtwork = FractureGlyph();
import {
  Normalize,
  Describe,
  Signature,
} from "../FractureEditor/FractureSpecification.js";

export default function FracturePanel({ Subject, Values, Change, Expand }) {
  const Settings = Normalize(Values.Fracture),
    Owner = Describe(Subject, Values),
    Supported = ["cube", "sphere", "cylinder", "cone"].includes(
      Owner.Primitive,
    ),
    Ready = Settings.Baked?.Signature === Signature(Owner, Settings);
  const Assign = (Key, Value) =>
    Change("Fracture", { ...Settings, [Key]: Value });
  return (
    <section className="generic-card fracture-card" data-card="Fracture">
      <header>
        <div>
          <h3>Fracture</h3>
          <small>PER-OBJECT GEOMETRY</small>
        </div>
        <button
          className="fracture-expand"
          aria-label="Open fracture workspace"
          title="Edit this object's fracture in place: the viewport isolates it"
          disabled={!Expand}
          onClick={Expand}
        >
          <svg viewBox="0 0 24 24" width="15" height="15" aria-hidden="true" fill="none" stroke="currentColor" strokeWidth="2" strokeLinecap="round" strokeLinejoin="round">
            <path d="M9 5H6.5A1.5 1.5 0 0 0 5 6.5v11A1.5 1.5 0 0 0 6.5 19h11a1.5 1.5 0 0 0 1.5-1.5V15M13 5h6v6M19 5l-8 8" />
          </svg>
        </button>
      </header>
      <label className="switch-row">
        <span>Enable fracture</span>
        <button
          className={"toggle " + (Settings.Enabled ? "on" : "")}
          role="switch"
          aria-label="Enable fracture"
          aria-checked={Settings.Enabled}
          onClick={() => Assign("Enabled", !Settings.Enabled)}
        >
          <i />
        </button>
      </label>
      {Settings.Enabled && (
        <>
          <div
            className="fracture-diagram"
            dangerouslySetInnerHTML={{ __html: FractureArtwork }}
          />
          <h4>BAKE</h4>
          <div
            className="fracture-mode"
            role="group"
            aria-label="Fracture execution"
          >
            {["dynamic", "baked"].map((Mode) => (
              <button
                key={Mode}
                aria-pressed={Settings.Mode === Mode}
                onClick={() => Assign("Mode", Mode)}
              >
                {Mode === "dynamic" ? "Dynamic" : "Baked"}
              </button>
            ))}
          </div>
          <p>
            {Settings.Mode === "dynamic"
              ? "Generate fragments on demand."
              : "Reuse stored geometry for this object."}{" "}
            {Settings.Mode === "baked"
              ? Ready
                ? Settings.PieceSdf
                  ? "Browser geometry ready. SDF pending."
                  : "Browser bake ready."
                : "Open the workspace to bake or refresh geometry."
              : ""}
          </p>
          {Settings.Mode === "baked" && (
            <div className="fracture-sdf">
              <label className="switch-row">
                <span>Bake SDF per piece</span>
                <button
                  className={"toggle " + (Settings.PieceSdf ? "on" : "")}
                  role="switch"
                  aria-label="Bake SDF per piece"
                  aria-checked={Settings.PieceSdf}
                  onClick={() => Assign("PieceSdf", !Settings.PieceSdf)}
                >
                  <i />
                </button>
              </label>
              {Settings.PieceSdf && (
                <>
                  <label className="field">
                    <span>Resolution per piece</span>
                    <select
                      aria-label="SDF resolution per piece"
                      value={Settings.SdfResolution}
                      onChange={(Event) =>
                        Assign("SdfResolution", Number(Event.target.value))
                      }
                    >
                      {[32, 64, 128].map((Resolution) => (
                        <option key={Resolution} value={Resolution}>
                          {Resolution}³ · R16F
                        </option>
                      ))}
                    </select>
                  </label>
                  <p>SDF authoring setting · generation pending.</p>
                </>
              )}
            </div>
          )}
          <button className="fracture-open" disabled={!Expand} onClick={Expand}>
            Open fracture workspace
          </button>
          {!Supported && (
            <p>
              Fracture settings are available.{" "}
              {Owner.Primitive === "torus"
                ? "Concave preview needs decomposition."
                : "Source geometry preview is pending."}
            </p>
          )}
        </>
      )}
    </section>
  );
}

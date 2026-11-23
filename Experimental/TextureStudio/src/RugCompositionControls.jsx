import React from "react";
import { rugDesignCatalog, rugStudySources } from "./rugDesigns.js";
import {
  normalizeRugComposition,
  rebuildRugComposition,
} from "./rugCompositions.js";
export default function RugCompositionControls({ doc, commit, onError }) {
  if (!doc.ornament) return null;
  const config = normalizeRugComposition(doc.ornament),
    design = rugDesignCatalog.find((p) => p.id === config.id),
    source = rugStudySources[design.study];
  const hasCorners = [
    "medallion",
    "radial",
    "triple",
    "cross",
    "arch",
    "bordered",
    "vertical",
  ].includes(design.layout);
  const hasFocal =
    ["tree", "vases", "arch"].includes(design.layout) ||
    (design.center !== "none" &&
      [
        "medallion",
        "radial",
        "triple",
        "cross",
        "bordered",
        "vertical",
        "garden",
        "trellis",
      ].includes(design.layout));
  const update = (patch) => {
    try {
      commit(rebuildRugComposition(doc, patch));
    } catch (e) {
      onError(e.message);
    }
  };
  return (
    <div className="pe-section pe-rug-composition">
      <span className="pe-kicker">COMPOSITION DESIGNER</span>
      <h3>{design.group}</h3>
      <p className="pe-hint">
        {design.description}. An original reference-informed composition, not a
        replica or an authenticated traditional pattern.
      </p>
      <label>
        Detail level
        <select
          aria-label="Rug detail level"
          value={config.detail}
          onChange={(e) => update({ detail: +e.target.value })}
        >
          <option value="1">Open — fewer field ornaments</option>
          <option value="2">Detailed — layered field and borders</option>
          <option value="3">Intricate — fine secondary ornament</option>
        </select>
      </label>
      <label>
        Border width
        <div className="pe-fade-range">
          <input
            aria-label="Rug border width"
            type="range"
            min="28"
            max="76"
            step="2"
            value={config.borderWidth}
            onChange={(e) => update({ borderWidth: +e.target.value })}
          />
          <output>{config.borderWidth}</output>
        </div>
      </label>
      {[
        ["field", "Patterned field"],
        ["medallions", "Medallions / focal structure"],
        ["corners", "Corner ornaments"],
        ["borderOrnaments", "Ornamental border"],
      ]
        .filter(
          ([key]) =>
            (key !== "corners" && key !== "medallions") ||
            (key === "corners" && hasCorners) ||
            (key === "medallions" && hasFocal),
        )
        .map(([key, label]) => (
          <label className="pe-check" key={key}>
            <input
              type="checkbox" role="switch"
              aria-label={label}
              checked={config[key]}
              onChange={(e) => update({ [key]: e.target.checked })}
            />
            {label}
          </label>
        ))}
      <p className="pe-hint">
        Rebuilds named composition layers; matching palette/finish assignments
        and added stitches are retained. Manual geometry edits to generated
        layers are replaced. Controls apply where the selected layout has those
        elements. Wool is relief and sheen, not groomed fibers.
      </p>
      <div className="pe-study-note">
        {source.url ? (
          <a href={source.url} target="_blank" rel="noreferrer">
            Study reference ↗
          </a>
        ) : (
          <span>Based on the earlier supplied diamond-rug reference</span>
        )}
      </div>
    </div>
  );
}

import {
  applyWeave,
  weaveVariants,
  weaveFamilyName,
  weaveDraft,
  weavePeriod,
} from "./patternWeaves.js";
import React from "react";
import {
  normalizeStitches,
  stitchTypes,
  stitchDescriptions,
  applyStitches,
} from "./patternStitches.js";
import { replacePatternColor } from "./patternLibrary.js";

export default function PatternTextileTools({ doc, commit, onError }) {
  const stitch = normalizeStitches(doc.stitch || { enabled: false });
  const colors = [
    ...new Set([
      doc.background,
      ...doc.layers
        .filter(
          (l) => !["image", "svg"].includes(l.kind) && l.stitchRole !== "holes",
        )
        .flatMap((l) => [
          l.color,
          ...(l.paint && l.strokeWidth ? [l.stroke] : []),
        ]),
    ]),
  ];
  const update = (patch) => {
    try {
      commit(applyStitches(doc, patch));
    } catch (e) {
      onError(e.message);
    }
  };
  return (
    <>
      {doc.weave && (
        <details className="pe-section pe-textile-tools" open>
          <summary>
            Weave construction <span>One yarn color</span>
          </summary>
          <label>
            Draft within this family
            <select
              aria-label="Weave draft"
              value={doc.weave.draft}
              onChange={(e) => {
                try {
                  commit(applyWeave(doc, { draft: e.target.value }));
                } catch (error) {
                  onError(error.message);
                }
              }}
            >
              {weaveVariants[weaveFamilyName(doc.weave.draft)].map((d) => (
                <option key={d}>{d}</option>
              ))}
            </select>
          </label>
          <label className="pe-color">
            Yarn color · warp and weft
            <input
              aria-label="Weave yarn color"
              type="color"
              value={doc.weave.color}
              onChange={(e) =>
                commit(
                  replacePatternColor(doc, doc.weave.color, e.target.value),
                )
              }
            />
          </label>
          <svg
            viewBox="0 0 120 120"
            role="img"
            aria-label={
              doc.weave.draft + " drawdown; filled cells mean warp above weft"
            }
            style={{
              width: 120,
              height: 120,
              display: "block",
              margin: "12px auto",
              background: "#eee9dd",
            }}
          >
            {Array.from(
              { length: weavePeriod(doc.weave.draft) ** 2 },
              (_, i) => {
                const n = weavePeriod(doc.weave.draft),
                  x = i % n,
                  y = Math.floor(i / n);
                return (
                  <rect
                    key={i}
                    x={(x * 120) / n}
                    y={(y * 120) / n}
                    width={120 / n}
                    height={120 / n}
                    fill={
                      weaveDraft(doc.weave.draft, x, y) ? "#364c51" : "#eee9dd"
                    }
                    stroke="#87918b"
                    strokeWidth=".4"
                  />
                );
              },
            )}
          </svg>
          <p className="pe-hint">
            Drawdown, not yarn colors: filled = warp on top; empty = weft on
            top. Both use the same yarn. Use 3D material to inspect crossing
            relief. Draft settings are not extra library patterns.
          </p>
        </details>
      )}
      <details className="pe-section pe-textile-tools">
        <summary>
          Design palette <span>{colors.length} colors</span>
        </summary>
        <p className="pe-hint">
          Recolor matching vector layers together. Geometry, relief and finish
          assignments stay intact.
        </p>
        <div className="pe-palette-swatches">
          {colors.map((color, i) => (
            <label key={i} title={color}>
              <input
                type="color"
                aria-label={"Palette color " + (i + 1)}
                value={color}
                onChange={(e) =>
                  commit(replacePatternColor(doc, color, e.target.value))
                }
              />
              <span>{i + 1}</span>
            </label>
          ))}
        </div>
        <p className="pe-hint">
          Imported image and grouped-SVG internal colors are not replaced.
        </p>
      </details>
      <details className="pe-section pe-textile-tools" open={stitch.enabled}>
        <summary>
          Stitch overlay <span>{stitch.enabled ? "On" : "Add stitching"}</span>
        </summary>
        <label className="pe-check">
          <input
            type="checkbox" role="switch"
            aria-label="Enable stitch overlay"
            checked={stitch.enabled}
            onChange={(e) => update({ enabled: e.target.checked })}
          />{" "}
          Stitch this design
        </label>
        <label>
          Stitch construction
          <select
            aria-label="Stitch construction"
            value={stitch.type}
            onChange={(e) => update({ type: e.target.value, enabled: true })}
          >
            {stitchTypes.map((type) => (
              <option key={type}>{type}</option>
            ))}
          </select>
        </label>
        <p className="pe-hint">{stitchDescriptions[stitch.type]}</p>
        <label>
          Placement
          <select
            aria-label="Stitch placement"
            value={stitch.layout}
            onChange={(e) => update({ layout: e.target.value, enabled: true })}
          >
            <option value="rows">Repeating rows</option>
            <option value="columns">Repeating columns</option>
            <option value="diagonal">Diagonal field</option>
            <option value="border">Inset border</option>
          </select>
        </label>
        <label className="pe-color">
          Thread color
          <input
            type="color"
            aria-label="Stitch thread color"
            value={stitch.color}
            onChange={(e) => update({ color: e.target.value, enabled: true })}
          />
        </label>
        {[
          ["spacing", "Stitch spacing", 16, 64, 1],
          ["width", "Thread width", 1, 8, 0.25],
          ["relief", "Stitch relief (mm)", 0, 1, 0.05],
          ...(stitch.layout === "border"
            ? [["inset", "Border inset", 12, 96, 1]]
            : []),
        ].map(([key, label, min, max, step]) => (
          <label key={key}>
            {label}
            <div className="pe-fade-range">
              <input
                type="range"
                aria-label={label}
                value={stitch[key]}
                min={min}
                max={max}
                step={step}
                onChange={(e) =>
                  update({ [key]: +e.target.value, enabled: true })
                }
              />
              <output>{stitch[key]}</output>
            </div>
          </label>
        ))}
        <p className="pe-hint">
          One thread color, raised upper passes and recessed needle entries.
          Spacing snaps to whole repeats; spacing, width and inset use tile
          units. Edits rebuild only the generated stitch layers, replacing
          manual edits to their paths. This is surface relief, not holes cut
          into the mesh.
        </p>
        {stitch.enabled && (
          <button className="pe-wide" onClick={() => update({})}>
            Rebuild stitch layers
          </button>
        )}
      </details>
    </>
  );
}

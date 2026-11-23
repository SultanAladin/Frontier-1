import { DIGITAL_BRUSH_PRESETS } from "./digitalBrush.js";
import React, { useEffect, useState } from "react";
import {
  History,
  Stamp,
  Undo2,
  Redo2,
  Brush,
  Folder,
  Layers3,
  Image,
  SlidersHorizontal,
  Clock,
  ArrowUpRight,
} from "lucide-react";
import { paintTool, toolArtURL } from "./paintToolCatalogue.js";
import { PAINT_CHANNELS } from "./paintChannelModel.js";
import { TEXTURE_HISTORY_LIMIT } from "./textureHistory.js";
const icons = {
  stroke: Brush,
  decal: Stamp,
  folder: Folder,
  layer: Layers3,
  source: Image,
  tool: Brush,
  channel: SlidersHorizontal,
  mask: Layers3,
  project: History,
};
export default function TextureHistoryPanel({ history }) {
  const [chosen, setChosen] = useState(null),
    [filter, setFilter] = useState("all");
  useEffect(() => setChosen(null), [history.currentRevisionId]);
  const entries = history.entries,
    selected =
      entries.find((e) => e.id === chosen) || entries.find((e) => e.current),
    digitalStroke = selected?.lastDigitalStroke,
    sourceStroke = selected?.lastStroke,
    stroke = digitalStroke
      ? { ...digitalStroke, settings: { color: digitalStroke.brush.color } }
      : sourceStroke;
  const darkInk =
    stroke &&
    parseInt(stroke.settings.color.slice(1, 3), 16) * 0.299 +
      parseInt(stroke?.settings.color.slice(3, 5) || "00", 16) * 0.587 +
      parseInt(stroke?.settings.color.slice(5, 7) || "00", 16) * 0.114 <
      140;
  const list = entries
    .filter((e) => filter === "all" || e.kind === "stroke")
    .slice()
    .reverse();
  return (
    <>
      <div className="tp-history-scroll">
        <div className="tp-history-heading">
          <div>
            <small>DOCUMENT TIMELINE</small>
            <h1>History</h1>
          </div>
          <span>{entries.length} states</span>
        </div>
        <div
          className="tp-history-actions"
          role="toolbar"
          aria-label="History actions"
        >
          <button
            aria-label="Undo history step"
            disabled={!history.canUndo}
            onClick={history.undo}
          >
            <Undo2 size={13} /> Undo
          </button>
          <button
            aria-label="Redo history step"
            disabled={!history.canRedo}
            onClick={history.redo}
          >
            <Redo2 size={13} /> Redo
          </button>
        </div>
        <section
          className="tp-property-card tp-history-preview"
          aria-label="Last stroke preview"
        >
          <h2>
            <i />
            Last stroke{" "}
            <small>
              {digitalStroke
                ? digitalStroke.mask
                  ? "Mask coverage"
                  : "Surface channels"
                : "2D source"}
            </small>
          </h2>
          {stroke ? (
            <>
              <div
                className="tp-history-stroke-image"
                style={{
                  "--stroke-check-a":
                    darkInk && stroke.operation !== "erase"
                      ? "#bcb9ad"
                      : "#252525",
                  "--stroke-check-b":
                    darkInk && stroke.operation !== "erase"
                      ? "#d5d1c3"
                      : "#45453c",
                }}
              >
                <img
                  src={stroke.image.dataUrl}
                  alt={
                    stroke.operation === "erase"
                      ? "Last recorded erase footprint"
                      : "Last recorded paint stroke"
                  }
                />
              </div>
              <div className="tp-history-stroke-info">
                {!digitalStroke && (
                  <img src={toolArtURL(stroke.settings.toolId, true)} alt="" />
                )}
                <div>
                  <strong>
                    {digitalStroke
                      ? DIGITAL_BRUSH_PRESETS.find(
                          (p) => p.id === digitalStroke.brush.preset,
                        )?.name
                      : paintTool(stroke.settings.toolId).label}
                  </strong>
                  <span>
                    {stroke.layerName} ·{" "}
                    {digitalStroke
                      ? digitalStroke.mask
                        ? "Mask coverage"
                        : digitalStroke.channels
                            .map(
                              (id) =>
                                PAINT_CHANNELS.find((c) => c.id === id)?.label,
                            )
                            .join(", ")
                      : PAINT_CHANNELS.find((c) => c.id === stroke.channelId)
                          ?.label}
                  </span>
                </div>
              </div>
              <p>
                {stroke.operation === "erase"
                  ? "Actual eraser footprint"
                  : "Actual recorded stroke"}{" "}
                at this history state.{" "}
                {digitalStroke
                  ? "Rendered footprint of the committed UV operation, not a tool-setting illustration."
                  : "Source pixels only, not painted teapot coverage."}
              </p>
            </>
          ) : (
            <div className="tp-history-stroke-empty">
              <Brush size={21} />
              <strong>No recorded stroke</strong>
              <span>
                Use digital Paint / Erase / Fill on a layer or mask, or Paint
                texture in a channel's Texture source.
              </span>
            </div>
          )}
        </section>
        <div
          className="tp-history-filter"
          role="group"
          aria-label="History filter"
        >
          {[
            ["all", "All edits"],
            ["strokes", "Strokes"],
          ].map(([id, label]) => (
            <button
              key={id}
              aria-pressed={filter === id}
              onClick={() => setFilter(id)}
            >
              {label}
            </button>
          ))}
        </div>
        <div
          className="tp-history-timeline"
          role="list"
          aria-label="Texture history states"
        >
          {list.map((entry) => {
            const Icon = icons[entry.kind] || History;
            return (
              <div
                key={entry.id}
                role="listitem"
                className={`tp-history-entry ${entry.current ? "current" : ""} ${entry.undone ? "undone" : ""} ${selected?.id === entry.id ? "selected" : ""}`}
              >
                <button
                  aria-label={`Inspect history ${entry.index}: ${entry.label}`}
                  aria-pressed={selected?.id === entry.id}
                  onClick={() => setChosen(entry.id)}
                >
                  <span className="tp-history-entry-icon">
                    {entry.kind === "stroke" &&
                    (entry.lastDigitalStroke || entry.lastStroke) ? (
                      <img
                        src={
                          (entry.lastDigitalStroke || entry.lastStroke).image
                            .dataUrl
                        }
                        alt="Stroke history thumbnail"
                      />
                    ) : (
                      <Icon size={14} />
                    )}
                  </span>
                  <span className="tp-history-entry-label">
                    <strong>{entry.label}</strong>
                    <small>{entry.detail}</small>
                    <span>
                      {entry.current
                        ? "Current state"
                        : entry.undone
                          ? "Undone"
                          : "Applied"}{" "}
                      ·{" "}
                      {new Date(entry.time).toLocaleTimeString([], {
                        hour: "2-digit",
                        minute: "2-digit",
                        second: "2-digit",
                      })}
                    </span>
                  </span>
                  <b>{String(entry.index).padStart(2, "0")}</b>
                </button>
              </div>
            );
          })}
          {!list.length && (
            <div className="tp-history-list-empty">
              <Clock size={18} />
              <p>No stroke transactions in this session.</p>
            </div>
          )}
        </div>
      </div>
      <footer className="tp-history-footer">
        <small>Session history · {TEXTURE_HISTORY_LIMIT} undo steps</small>
        <button
          disabled={!selected || selected.current}
          onClick={() => history.restoreRevision(selected.id)}
        >
          <ArrowUpRight size={12} /> Restore this state
        </button>
      </footer>
    </>
  );
}

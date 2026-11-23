import React from "react";
import {
  SlidersHorizontal,
  RotateCcw,
  Circle,
  Star,
  Layers3,
  ArrowRight,
  Plus,
} from "lucide-react";
import { StudioDockTab } from "./StudioUI.jsx";
import MaterialPanel from "./MaterialPanel.jsx";
export default function MaterialInspectorPanel({
  params,
  selected,
  setParams,
  notify,
  thumbs = {},
  favorites = [],
  toggleFavorite = () => {},
  tab,
  setTab,
  layerCount,
  update,
  setCustomName,
  setModal,
  openWorkspace,
}) {
  return (
    <aside className="inspector-panel">
      <StudioDockTab icon={SlidersHorizontal}>Inspector</StudioDockTab>
      <div className="panel-title">
        <h2>Material inspector</h2>
        <button
          className="icon-button"
          title="Reset material to preset"
          onClick={() => {
            setParams({ ...selected });
            notify("Material properties reset.");
          }}
        >
          <RotateCcw size={15} />
        </button>
      </div>
      {params.pattern && (
        <div className="pattern-applied">
          <span>Pattern: {params.pattern.name}</span>
          <button onClick={() => openWorkspace("pattern")}>Edit</button>
          <button
            onClick={() =>
              setParams((p) => ({
                ...p,
                pattern: null,
                name: p.patternBaseName || p.name,
                patternBaseName: undefined,
              }))
            }
          >
            Remove pattern
          </button>
        </div>
      )}
      <div className="inspector-material">
        <div className="inspector-thumbnail">
          {thumbs[selected.id] ? (
            <img src={thumbs[selected.id]} alt="" />
          ) : (
            <Circle size={24} style={{ color: params.color }} />
          )}
        </div>
        <div>
          <h3>{params.name}</h3>
          <p>
            <span className="status-dot" />
            {params.label}
          </p>
        </div>
        <button
          className="icon-button"
          title="Save current material"
          onClick={() => toggleFavorite(selected.id)}
        >
          <Star
            size={15}
            fill={favorites.includes(selected.id) ? "currentColor" : "none"}
          />
        </button>
      </div>
      <div className="inspector-tabs">
        {["Properties", "Layers"].map((t) => (
          <button
            key={t}
            className={tab === t ? "active" : ""}
            onClick={() => setTab(t)}
          >
            {t === "Properties" ? (
              <SlidersHorizontal size={13} />
            ) : (
              <Layers3 size={14} />
            )}{" "}
            {t}
            {t === "Layers" && <span>{layerCount}</span>}
          </button>
        ))}
      </div>
      <MaterialPanel Parameters={params} ConfigureParameters={setParams} Mode={tab} Notify={notify} OpenWorkspace={openWorkspace}/>
      <div className="inspector-footer">
        <button
          className="save-preset-button"
          onClick={() => {
            setCustomName(params.name + " Custom");
            setModal("save");
          }}
        >
          <Plus size={14} /> Save as preset <span>⌘ S</span>
        </button>
        <span>Made to be made your own.</span>
      </div>
    </aside>
  );
}

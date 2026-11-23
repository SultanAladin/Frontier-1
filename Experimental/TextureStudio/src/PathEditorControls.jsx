import React from "react";
import {
  PenTool,
  Spline,
  Undo2,
  Redo2,
  CornerDownLeft,
  RotateCcw,
  Scissors,
  Link2,
  X,
  Check,
  Plus,
  Minus,
  ArrowRight,
} from "lucide-react";
import { pathNodeMode } from "./pathEditorGeometry.js";
const names = {
  corner: "Corner / independent",
  smooth: "Smooth / aligned",
  symmetric: "Symmetric / equal",
  auto: "Automatic curve",
};
export default function PathEditorControls({
  tool,
  pending,
  count,
  node,
  nodeCount,
  segment,
  canAdd,
  closed,
  canUndo,
  canRedo,
  canContinue,
  canJoin,
  selectionCount,
  selectionMode,
  penMode,
  onPenMode,
  tension,
  showHandles,
  onShowHandles,
  onAction,
  onNodeField,
  onMode,
  onTension,
  paint,
  onPaint,
}) {
  const mode = selectionMode || (node ? pathNodeMode(node) : "corner"),
    id = React.useId();
  const handles = ["in", "out"].map((side) => ({ side, point: node?.[side] }));
  return (
    <div
      className="se-path-workbench"
      role="region"
      aria-label="Path workbench"
    >
      <div className="se-path-heading">
        {tool === "curve" ? <Spline size={16} /> : <PenTool size={16} />}
        <div>
          <strong>
            {tool === "curve"
              ? "Curve builder"
              : tool === "node"
                ? "Path editor"
                : "Precision pen"}
          </strong>
          <small>
            {pending
              ? `${count} anchors · ${canContinue ? "extending existing path" : "path in progress"}`
              : nodeCount
                ? `${nodeCount} anchors · ${closed ? "closed" : "open"} path`
                : "Create a path, or select one to refine"}
          </small>
        </div>
        {pending && <span className="se-path-draft">DRAFT</span>}
        <label className="se-path-handles">
          <input
            type="checkbox"
            aria-label="Show path handles"
            checked={showHandles}
            onChange={(e) => onShowHandles(e.target.checked)}
          />{" "}
          Handles
        </label>
      </div>
      <fieldset className="se-path-paint" aria-label="Path appearance">
        <legend>Stroke & fill · document units</legend>
        <div className="se-path-paint-row">
          <label>
            Stroke width
            <input
              aria-label="Path stroke width"
              type="number"
              min="0"
              max="25"
              step="0.1"
              value={paint.strokeWidth}
              onChange={(e) =>
                onPaint({
                  strokeWidth: Math.max(
                    0,
                    Math.min(25, Number(e.target.value)),
                  ),
                })
              }
            />
          </label>
          <label>
            Stroke color
            <input
              aria-label="Path stroke color"
              type="color"
              value={paint.stroke}
              onChange={(e) => onPaint({ stroke: e.target.value })}
            />
          </label>
        </div>
        <div className="se-path-paint-row">
          <label>
            Line cap
            <select
              aria-label="Path line cap"
              value={paint.strokeLinecap || "round"}
              onChange={(e) => onPaint({ strokeLinecap: e.target.value })}
            >
              {["round", "butt", "square"].map((v) => (
                <option key={v}>{v}</option>
              ))}
            </select>
          </label>
          <label>
            Line join
            <select
              aria-label="Path line join"
              value={paint.strokeLinejoin || "round"}
              onChange={(e) => onPaint({ strokeLinejoin: e.target.value })}
            >
              {["round", "miter", "bevel"].map((v) => (
                <option key={v}>{v}</option>
              ))}
            </select>
          </label>
        </div>
        <div className="se-path-paint-row">
          <label className="pe-check">
            <input
              aria-label="Path fill enabled"
              type="checkbox"
              checked={paint.fillEnabled !== false}
              onChange={(e) => onPaint({ fillEnabled: e.target.checked })}
            />
            {pending ? "Fill on close" : "Fill"}
          </label>
          <label>
            Fill color
            <input
              aria-label="Path fill color"
              type="color"
              value={paint.color}
              onChange={(e) => onPaint({ color: e.target.value })}
            />
          </label>
        </div>
        <small>
          {paint.strokeWidth === 0
            ? "0 = no stroke"
            : "Stroke width stays consistent when resizing the path."}
        </small>
      </fieldset>
      <div className="se-path-options">
        {tool === "pen" && (
          <label>
            New drag handles
            <select
              aria-label="Pen handle mode"
              value={penMode}
              onChange={(e) => onPenMode(e.target.value)}
            >
              {["symmetric", "smooth", "corner"].map((v) => (
                <option key={v} value={v}>
                  {names[v]}
                </option>
              ))}
            </select>
          </label>
        )}
        {(tool === "curve" || mode === "auto") && (
          <label className="se-curve-tension" htmlFor={`${id}-tension`}>
            Curve tension{" "}
            <output aria-label="Curve tension percentage">
              {Math.round(tension * 100)}%
            </output>
            <input
              id={`${id}-tension`}
              aria-label="Curve tension"
              aria-valuetext={`${Math.round(tension * 100)} percent`}
              type="range"
              min="0"
              max="100"
              step="1"
              value={Math.round(tension * 100)}
              onChange={(e) => onTension(Number(e.target.value) / 100)}
            />
          </label>
        )}
        {!!selectionCount && (
          <label>
            Point type
            <select
              aria-label="Path point type"
              value={mode}
              onChange={(e) => onMode(e.target.value)}
            >
              {mode === "mixed" && (
                <option value="mixed" disabled>
                  Mixed point types
                </option>
              )}
              {Object.entries(names).map(([key, label]) => (
                <option key={key} value={key}>
                  {label}
                </option>
              ))}
            </select>
          </label>
        )}
        <span className="se-path-tip">
          {tool === "curve"
            ? "Click to curve · Alt-click for a corner · Enter to finish"
            : tool === "pen"
              ? "Click / drag · Alt breaks tangents · Shift constrains · Ctrl/⌘ edits"
              : "Drag a segment to bend · Shift selects points · Alt-click inserts"}
        </span>
      </div>
      <div className="se-path-actions">
        {pending ? (
          <>
            <button
              title="Undo path edit (Ctrl/⌘ Z)"
              aria-label="Undo path edit"
              disabled={!canUndo}
              onClick={() => onAction("draft-undo")}
            >
              <Undo2 size={13} />
            </button>
            <button
              title="Redo path edit (Ctrl/⌘ Shift Z)"
              aria-label="Redo path edit"
              disabled={!canRedo}
              onClick={() => onAction("draft-redo")}
            >
              <Redo2 size={13} />
            </button>
            <button disabled={count < 2} onClick={() => onAction("finish")}>
              <CornerDownLeft size={13} /> Finish path
            </button>
            <button
              disabled={count < 3}
              onClick={() => onAction("finish-closed")}
            >
              <Check size={13} /> Close path
            </button>
            <button onClick={() => onAction("cancel")}>
              <X size={13} /> Cancel path
            </button>
          </>
        ) : (
          <>
            <button onClick={() => onAction("new")}>
              <Plus size={13} /> New path
            </button>
            {canRedo && (
              <button
                aria-label="Redo path edit"
                onClick={() => onAction("draft-redo")}
              >
                <Redo2 size={13} /> Restore path draft
              </button>
            )}
            <button
              disabled={!canContinue}
              onClick={() => onAction("continue-end")}
            >
              <ArrowRight size={13} /> Continue end
            </button>
            <button
              disabled={!canContinue}
              onClick={() => onAction("continue-start")}
            >
              Continue start
            </button>
            <button
              disabled={!nodeCount || (!closed && nodeCount < 3)}
              onClick={() => onAction("toggle-closed")}
            >
              {closed ? "Open path" : "Close path"}
            </button>
          </>
        )}
        <button
          disabled={!count && !nodeCount}
          title="Reverse path direction"
          onClick={() => onAction("reverse")}
        >
          <RotateCcw size={13} /> Reverse
        </button>
        <span className="se-tool-divider" />
        <button disabled={!canAdd} onClick={() => onAction("add")}>
          <Plus size={13} /> Add point
        </button>
        <button disabled={!selectionCount} onClick={() => onAction("remove")}>
          <Minus size={13} /> Remove point
        </button>
        <button disabled={!selectionCount} onClick={() => onAction("smooth")}>
          Smooth point
        </button>
        <button disabled={!selectionCount} onClick={() => onAction("corner")}>
          Corner point
        </button>
        {!pending && (
          <button
            title="Split an open path at the selected interior point"
            disabled={
              selectionCount !== 1 || closed || !nodeCount || node?.endpoint
            }
            onClick={() => onAction("split")}
          >
            <Scissors size={13} /> Split path
          </button>
        )}
        {!pending && (
          <button
            title="Join the nearest endpoints; first path's style is retained"
            disabled={!canJoin}
            onClick={() => onAction("join")}
          >
            <Link2 size={13} /> Join paths
          </button>
        )}
      </div>
      {node && selectionCount === 1 && (
        <details className="se-node-properties" open>
          <summary>
            Point {node.index + 1} · anchor & tangents{" "}
            <small>document units</small>
          </summary>
          <div className="se-point-fields">
            {[
              ["x", "Anchor X"],
              ["y", "Anchor Y"],
            ].map(([key, label]) => (
              <label key={key}>
                {label}
                <input
                  type="number"
                  aria-label={`Path ${label.toLowerCase()}`}
                  min="-512"
                  max="1024"
                  step="1"
                  value={Number(node[key].toFixed(3))}
                  onChange={(e) => onNodeField(key, Number(e.target.value))}
                />
              </label>
            ))}
            {handles.map(({ side, point }) => {
              const dx = point ? point.x - node.x : 0,
                dy = point ? point.y - node.y : 0;
              return (
                <React.Fragment key={side}>
                  <label>
                    {side === "in" ? "Incoming" : "Outgoing"} length
                    <input
                      type="number"
                      aria-label={`Path ${side} handle length`}
                      min="0"
                      max="2048"
                      step="1"
                      value={Number(Math.hypot(dx, dy).toFixed(3))}
                      onChange={(e) =>
                        onNodeField(`${side}-length`, Number(e.target.value))
                      }
                    />
                  </label>
                  <label>
                    {side === "in" ? "Incoming" : "Outgoing"} angle
                    <input
                      type="number"
                      aria-label={`Path ${side} handle angle`}
                      min="-360"
                      max="360"
                      step="1"
                      value={Number(
                        ((Math.atan2(dy, dx) * 180) / Math.PI).toFixed(2),
                      )}
                      onChange={(e) =>
                        onNodeField(`${side}-angle`, Number(e.target.value))
                      }
                    />
                  </label>
                </React.Fragment>
              );
            })}
          </div>
        </details>
      )}
      {selectionCount > 1 && (
        <p className="se-path-multi">
          {selectionCount} anchors selected · drag or arrow keys move them
          together
        </p>
      )}
    </div>
  );
}

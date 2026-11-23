import VectorBooleanActions from "./VectorBooleanActions.jsx";
import React from "react";
import {
  MousePointer2,
  MousePointer,
  Square,
  Circle,
  Triangle,
  Diamond,
  Pentagon,
  Star,
  PenTool,
  Spline,
  Pencil,
  Minus,
  Hand,
  Grid3X3,
  Magnet,
  Group,
  Ungroup,
  Copy,
  Trash2,
  AlignHorizontalJustifyStart,
  AlignHorizontalJustifyCenter,
  AlignHorizontalJustifyEnd,
  AlignVerticalJustifyStart,
  AlignVerticalJustifyCenter,
  AlignVerticalJustifyEnd,
  BetweenHorizontalStart,
  BetweenVerticalStart,
  Type,
  Pipette,
  CircleDashed,
  Combine,
  Code2,
  SlidersHorizontal,
} from "lucide-react";

export const shapeTools = [
  ["move", "Select tool", "V", MousePointer2, "Move shapes"],
  ["node", "Edit points", "A", MousePointer],
  ["rect", "Rectangle tool", "R", Square],
  ["ellipse", "Ellipse tool", "O", Circle],
  ["line", "Line tool", "L", Minus],
  ["triangle", "Triangle tool", "T", Triangle],
  ["diamond", "Diamond tool", "D", Diamond],
  ["polygon", "Polygon tool", "G", Pentagon],
  ["star", "Star tool", "S", Star],
  ["pen", "Pen tool", "P", PenTool],
  ["curve", "Curve tool", "U", Spline],
  ["pencil", "Freehand tool", "B", Pencil, "Draw path"],
  ["arc", "Arc tool", "C", CircleDashed],
  ["text", "Text tool", "X", Type],
  ["eyedropper", "Eyedropper tool", "I", Pipette],
  ["hand", "Hand tool", "H", Hand],
];
const ShapeToolDescriptions = {
  move: "Select and move motifs on the tile. Shift-click to extend the selection.",
  node: "Select path points and adjust their curve handles without replacing the shape.",
  rect: "Drag to draw a rectangle using the current fill and stroke. Hold Shift for a square.",
  ellipse: "Drag to draw an ellipse using the current fill and stroke. Hold Shift for a circle.",
  line: "Drag between two points to draw a line using the current stroke.",
  triangle: "Drag to draw a triangle. Edit its transform and colour sources in the inspector.",
  diamond: "Drag to draw a diamond. Edit its transform and colour sources in the inspector.",
  polygon: "Drag to draw a regular polygon. Change its side count in the inspector.",
  star: "Drag to draw a star. Adjust the points and inner radius in the inspector.",
  pen: "Click to place path points; drag a point to set its curve handles.",
  curve: "Place points to create a smooth curved path. Finish the path before switching tools.",
  pencil: "Draw a freehand path on the tile using the current stroke.",
  arc: "Drag to draw an arc, then adjust its start and sweep in the inspector.",
  text: "Place editable text on the tile. Choose its font, size and colour in the inspector.",
  eyedropper: "Sample a motif colour from the tile for the active drawing style.",
  hand: "Drag the canvas to pan without moving any motifs.",
};
const alignTools = [
  ["left", "Align left", AlignHorizontalJustifyStart],
  ["center-x", "Align horizontal centers", AlignHorizontalJustifyCenter],
  ["right", "Align right", AlignHorizontalJustifyEnd],
  ["top", "Align top", AlignVerticalJustifyStart],
  ["center-y", "Align vertical centers", AlignVerticalJustifyCenter],
  ["bottom", "Align bottom", AlignVerticalJustifyEnd],
];
export { alignTools as ArtworkAlignmentTools };
export function ShapeSelectionActions({ count, hasGroup, onAction, canBoolean, MaskNames, OnUseMask })
{
  if (!count) return null;
  return <div className="se-selection-context" role="group" aria-label="Selection actions">
    <h2 className="artwork-selection-heading">{count === 1 ? "Selected artwork" : `${count} selected`}</h2>
    <section className="artwork-alignment"><h3>{count === 1 ? "Align to canvas" : "Align selection"}</h3><div>
      <div className="se-align-tools">{alignTools.map(([Key, Caption, Icon]) => <button key={Key} title={Caption} aria-label={Caption} onClick={() => onAction(`align-${Key}`)}><Icon size={15}/></button>)}
      <button title="Distribute horizontally" aria-label="Distribute horizontally" disabled={count < 3} onClick={() => onAction("distribute-x")}><BetweenHorizontalStart size={15}/></button>
      <button title="Distribute vertically" aria-label="Distribute vertically" disabled={count < 3} onClick={() => onAction("distribute-y")}><BetweenVerticalStart size={15}/></button></div>
      <button aria-label="Group selection" title="Group selection (Ctrl/⌘ G)" disabled={count < 2} onClick={() => onAction("group")}><Group size={15}/></button>
      <button aria-label="Ungroup selection" title="Ungroup selection (Ctrl/⌘ Shift G)" disabled={!hasGroup} onClick={() => onAction("ungroup")}><Ungroup size={15}/></button>
    </div></section>
    {count >= 2 && <VectorBooleanActions Enabled={canBoolean} Count={count} OnAction={onAction}/>}
    {count === 2 && MaskNames && <section className="artwork-selection-mask"><h3>Use as mask</h3><small>Alpha snapshot · source retained hidden · undo restores both</small>{[0, 1].map(Index => <button key={Index} onClick={() => OnUseMask(Index)}>{MaskNames[0] === MaskNames[1] ? `${Index + 1}. ${MaskNames[Index]} masks ${2 - Index}. ${MaskNames[1 - Index]}` : `${MaskNames[Index]} masks ${MaskNames[1 - Index]}`}</button>)}</section>}
    <div className="se-object-tools"><button title="Duplicate motif" aria-label="Duplicate selection" onClick={() => onAction("duplicate")}><Copy size={15}/></button><button title="Delete motif" aria-label="Delete selection" onClick={() => onAction("delete")}><Trash2 size={15}/></button></div>
  </div>;
}
export default function ShapeEditorToolbar({
  tool,
  onToolChange,
  grid,
  onGridChange,
  showGrid,
  onGridToggle,
  smartGuides,
  onGuidesToggle,
  aspect,
  onAspectChange,
  snapping,
  onSnappingChange,
  onSVGSource,
}) {
  function ToolButton([Key, Caption, Shortcut, Icon, Title])
  {
    return <button key={Key} className={tool === Key ? "selected" : ""} title={Title || `${Caption} (${Shortcut})`} data-tooltip-title={Caption} data-tooltip-shortcut={Shortcut} data-tooltip-body={ShapeToolDescriptions[Key]} aria-label={`${Caption} (${Shortcut})`} aria-pressed={tool === Key} onClick={Event => { onToolChange(Key); Event.currentTarget.closest("details")?.removeAttribute("open"); }}><Icon size={17}/></button>;
  }
  return (
    <div className="se-toolbar-wrap">
      <div
        className="se-drawing-tools"
        role="toolbar"
        aria-label="Drawing tools"
      >
        {shapeTools.filter(([Key]) => ["move", "node", "rect", "ellipse", "pen", "pencil", "hand"].includes(Key)).map(ToolButton)}
        <details className="se-tool-menu" name="vector-toolbar-menus"><summary aria-label="More drawing tools" title="More drawing tools">More tools</summary><div className="se-menu-content">
          {shapeTools.filter(([Key]) => !["move", "node", "rect", "ellipse", "pen", "pencil", "hand"].includes(Key)).map(ToolButton)}
          <button title="Paste SVG source" aria-label="Paste SVG source" onClick={onSVGSource}><Code2 size={17}/></button>
        </div></details>
        <details className="se-tool-menu" name="vector-toolbar-menus"><summary aria-label="Canvas options" title="Canvas options"><SlidersHorizontal size={15}/></summary><div className="se-menu-content">
      <div className="se-options-tools">
        <button
          title="Show grid"
          aria-label="Show grid"
          aria-pressed={showGrid}
          onClick={onGridToggle}
        >
          <Grid3X3 size={14} />
        </button>
        <label>
          Snap{" "}
          <select
            aria-label="Grid snapping"
            value={grid}
            onChange={(e) => onGridChange(Number(e.target.value))}
          >
            <option value="0">Off</option>
            <option value="8">8 units</option>
            <option value="16">16 units</option>
            <option value="32">32 units</option>
            <option value="64">64 units</option>
          </select>
        </label>
        <button
          title="Point, curve and smart alignment snapping"
          aria-label="Smart alignment guides"
          aria-pressed={smartGuides}
          onClick={onGuidesToggle}
        >
          <Magnet size={14} />
        </button>
        <details className="se-snap-details">
          <summary>Snap targets</summary>
          <div className="se-snap-menu">
            {[
              ["points", "Anchors & centers"],
              ["curves", "Curves & edges"],
              ["guides", "Object alignment"],
            ].map(([key, label]) => (
              <label key={key}>
                <input
                  aria-label={`Snap to ${label.toLowerCase()}`}
                  type="checkbox" role="switch"
                  checked={snapping[key]}
                  onChange={(e) =>
                    onSnappingChange({ ...snapping, [key]: e.target.checked })
                  }
                />
                {label}
              </label>
            ))}
            <label>
              Angle
              <select
                aria-label="Angle snapping"
                value={snapping.angle}
                onChange={(e) =>
                  onSnappingChange({
                    ...snapping,
                    angle: Number(e.target.value),
                  })
                }
              >
                {[0, 15, 30, 45, 90].map((v) => (
                  <option key={v} value={v}>
                    {v ? `${v}°` : "Off"}
                  </option>
                ))}
              </select>
            </label>
            <small>
              The magnet enables target snapping; the grid and angle settings
              are independent.
            </small>
          </div>
        </details>
        <label className="pe-check">
          <input
            type="checkbox" role="switch"
            aria-label="Keep aspect ratio"
            checked={aspect}
            onChange={(e) => onAspectChange(e.target.checked)}
          />{" "}
          Constrain proportions
        </label>
        <span>Shift to constrain · Space to pan</span>
      </div>
        </div></details>
      </div>
    </div>
  );
}

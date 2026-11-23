import { PatternColourDefinitions, PatternColourReference, PatternGroundLayer } from "./PatternColourSources.js";
import {
  createEditorSnapTargets,
  addEditorPathSnapTargets,
  snapEditorPoint,
} from "./vectorEditorSnapping.js";
import { createPortal } from "react-dom";
import React, {
  forwardRef,
  useEffect,
  useImperativeHandle,
  useMemo,
  useRef,
  useState,
} from "react";
import {
  Minus,
  Plus,
  Scan,
  MousePointer2,
  Grip,
  CornerDownLeft,
} from "lucide-react";
import {
  textilePreviewDefs,
  textilePreviewShape,
  patternLayerTransform,
} from "./patternDocument.js";
import { patternNodesPath } from "./patternGeometry.js";
import usePathSession from "./usePathSession.js";
import PathEditorControls from "./PathEditorControls.jsx";
import PathEditorOverlay from "./PathEditorOverlay.jsx";
import {
  automaticPathNodes,
  movedPathNode,
  pathNodeMode,
  worldPathNodes,
  withWorldPathNodes,
  movePathAnchors,
  movePathHandle,
  setPathPointMode,
  pathSegment,
  cubicPathPoint,
  nearestPathPoint,
  insertPathPoint,
  bendPathSegment,
  reversePathNodes,
  pathGeometryKey,
  joinedPathLayers,
} from "./pathEditorGeometry.js";
import {
  clamp,
  layerBounds,
  selectionBounds,
  editableSelection,
  expandPatternSelection,
  layerLocalPoint,
  layerWorldPoint,
  translateSelection,
  snappedTranslation,
  resizeSelection,
  rotateSelection,
  clonePatternLayers,
  drawnPatternLayer,
  patternCopies,
  worldPathLayer,
  boundedDrawnPoints,
  reframePatternPath,
} from "./shapeEditorGeometry.js";

const drawingTools = [
  "rect",
  "ellipse",
  "diamond",
  "triangle",
  "polygon",
  "star",
  "flower",
  "line",
  "pencil",
  "arc",
];
const handleNames = {
  nw: "top-left",
  n: "top",
  ne: "top-right",
  e: "right",
  se: "bottom-right",
  s: "bottom",
  sw: "bottom-left",
  w: "left",
};
const LayerInstances = React.memo(function LayerInstances({
  layer,
  index,
  doc,
  interactiveLocked,
}) {
  const html = textilePreviewShape(layer, layer.color, false);
  return patternCopies(doc, layer).map(({ x, y, sx, sy, col, row }) => (
    <g
      key={`${col},${row}`}
      data-layer={index}
      data-copy-x={sx}
      data-copy-y={sy}
      pointerEvents={layer.locked && !interactiveLocked ? "none" : undefined}
      opacity={layer.opacity}
      transform={patternLayerTransform(layer, { x, y, sx, sy })}
      dangerouslySetInnerHTML={{ __html: html }}
    />
  ));
});
const paintTransform = patternLayerTransform;
const ShapeEditorCanvas = forwardRef(function ShapeEditorCanvas(
  {
    doc,
    selected,
    tool,
    onToolChange,
    grid,
    aspect,
    showGrid,
    smartGuides,
    snapping,
    style,
    onDrawingStyle,
    onCreateText,
    onEditText,
    onSampleAppearance,
    CanvasLabel = "Pattern design canvas",
    CanvasBackdrop,
    TextEditingPredicate = Layer => Layer.kind === "text",
    onSelection,
    onPreview,
    onCommit,
    onCancel,
    onError,
    onAction,
    onPathStateChange,
    onWorkingDraft,
    onPathDirty,
    pathPanelTarget,
  },
  ref,
) {
  const svgRef = useRef(null),
    contentRef = useRef(null),
    stageRef = useRef(null),
    drag = useRef(null),
    space = useRef(false),
    touches = useRef(new Map()),
    pinch = useRef(null),
    pointSelectionRef = useRef([]),
    forceNewPath = useRef(false),
    pathStrokeWasSet = useRef(false),
    snapCache = useRef({}),
    lastPlacement = useRef(null);
  const pathSession = usePathSession(onPathStateChange),
    penRef = pathSession.ref,
    pen = pathSession.state;
  const props = useRef({});
  props.current = {
    doc,
    selected,
    tool,
    grid,
    aspect,
    smartGuides,
    snapping,
    style,
    onDrawingStyle,
    onCreateText,
    onEditText,
    TextEditingPredicate,
    onSampleAppearance,
    onSelection,
    onPreview,
    onCommit,
    onCancel,
    onError,
    onAction,
    onWorkingDraft,
    onPathDirty,
  };
  const [size, setSize] = useState({ width: 600, height: 440 }),
    [camera, setCamera] = useState({
      x: 256 * (doc.designAspect || 1),
      y: 256,
      zoom: 1,
    }),
    [draft, setDraft] = useState(null),
    [penCursor, setPenCursor] = useState(null),
    [marquee, setMarquee] = useState(null),
    [guides, setGuides] = useState([]),
    [snapFeedback, setSnapFeedback] = useState(null),
    [activeNode, setActiveNode] = useState(-1),
    [nodeSelection, setNodeSelection] = useState([]),
    [activeSegment, setActiveSegment] = useState(null),
    [hoverSegment, setHoverSegment] = useState(null),
    [curveTension, setCurveTension] = useState(1),
    [penMode, setPenMode] = useState("symmetric"),
    [showHandles, setShowHandles] = useState(true),
    [panning, setPanning] = useState(false),
    [context, setContext] = useState(null);
  const cameraRef = useRef(camera);
  cameraRef.current = camera;
  const designAspect = doc.designAspect || 1,
    base = Math.max((512 * designAspect + 100) / size.width, 612 / size.height),
    units = base / camera.zoom,
    viewWidth = size.width * units,
    viewHeight = size.height * units,
    viewX = camera.x - viewWidth / 2,
    viewY = camera.y - viewHeight / 2;
  const viewRef = useRef({});
  viewRef.current = { base, units, designAspect, size };
  const ids = editableSelection(doc, selected),
    current = ids.length === 1 ? doc.layers[ids[0]] : null,
    bounds = current
      ? {
          x: current.x,
          y: current.y,
          width: current.width,
          height: current.height,
          rotation: current.rotation,
        }
      : selectionBounds(doc, ids);
  const nodeLayer =
    !pen && ["node", "pen", "curve"].includes(tool) && current?.nodes
      ? current
      : null;
  const nodeWorld = useMemo(
    () => (nodeLayer ? worldPathNodes(nodeLayer) : null),
    [nodeLayer],
  );
  const handleUnit = units * 7;
  useEffect(() => {
    const observer = new ResizeObserver(([entry]) =>
      setSize({
        width: Math.max(200, entry.contentRect.width),
        height: Math.max(200, entry.contentRect.height),
      }),
    );
    observer.observe(stageRef.current);
    return () => observer.disconnect();
  }, []);
  useEffect(() => {
    setCamera((c) => ({ ...c, x: 256 * designAspect, y: 256, zoom: 1 }));
  }, [designAspect]);
  useEffect(() => {
    if (!penRef.current) setPointSelection([]);
    setActiveSegment(null);
    setHoverSegment(null);
  }, [selected.join(","), tool]);
  useEffect(() => {
    const release = (e) => {
        if (e.code === "Space") {
          space.current = false;
          setPanning(false);
        }
      },
      blur = () => {
        space.current = false;
        setPanning(false);
        cancelGesture();
      };
    window.addEventListener("keyup", release);
    window.addEventListener("blur", blur);
    return () => {
      window.removeEventListener("keyup", release);
      window.removeEventListener("blur", blur);
    };
  }, []);
  useEffect(() => {
    if (!["pen", "curve", "node", "hand"].includes(tool) && penRef.current) {
      if (penRef.current.nodes.length < 2) cancelInteraction();
      else finishPen(false);
    }
    if (drag.current) cancelGesture();
    if (!["pen", "curve", "node", "hand"].includes(tool) && !penRef.current)
      pathSession.clear();
    setContext(null);
    setSnapFeedback(null);
    setGuides([]);
  }, [tool]);
  useEffect(() => {
    const p = penRef.current;
    if (p && p.target == null)
      pathSession.change({ ...p, style: { ...p.style, ...style } });
  }, [style]);
  useEffect(() => {
    const p = penRef.current;
    if (
      p?.target != null &&
      pathGeometryKey(doc.layers[p.target]) !== p.sourceKey
    ) {
      cancelInteraction();
      props.current.onError(
        "The source path changed. Its unfinished extension was canceled safely.",
      );
    }
  }, [doc]);
  useEffect(() => {
    if (!pen) return;
    props.current.onPathDirty?.(pen.nodes.length);
    if (pen.nodes.length < 2) return;
    const timer = setTimeout(
      () => props.current.onWorkingDraft?.(workingDocument()),
      700,
    );
    return () => clearTimeout(timer);
  }, [pen, doc]);
  function rootPoint(e) {
    const matrix = svgRef.current?.getScreenCTM();
    return matrix
      ? new DOMPoint(e.clientX, e.clientY).matrixTransform(matrix.inverse())
      : { x: 0, y: 0 };
  }
  function point(e, bounded = true) {
    const matrix = contentRef.current?.getScreenCTM();
    const p = matrix
      ? new DOMPoint(e.clientX, e.clientY).matrixTransform(matrix.inverse())
      : { x: 0, y: 0 };
    return bounded
      ? { x: clamp(p.x, -512, 1024), y: clamp(p.y, -512, 1024) }
      : { x: p.x, y: p.y };
  }
  function snappedPoint(p, options = {}) {
    const c = props.current,
      ctx = options.context === null ? null : options.context || pathContext(),
      document = options.doc || c.doc,
      excluded =
        options.excludeLayers ||
        (ctx?.pending
          ? ctx.data.target == null
            ? []
            : [ctx.data.target]
          : ctx
            ? [ctx.index]
            : []),
      key = excluded.join(",");
    if (snapCache.current.doc !== document || snapCache.current.key !== key)
      snapCache.current = {
        doc: document,
        key,
        targets: createEditorSnapTargets(document, excluded),
      };
    const base = snapCache.current.targets,
      targets = {
        points: [...base.points],
        curves: [...base.curves],
        axes: { x: [...base.axes.x], y: [...base.axes.y] },
      };
    if (ctx?.nodes)
      addEditorPathSnapTargets(
        targets,
        ctx.nodes,
        ctx.closed,
        options.excludeNodes || [],
        ctx.index ?? -1,
      );
    const result = snapEditorPoint(p, targets, {
      ...c.snapping,
      grid: c.grid,
      enabled: c.smartGuides,
      aspect: viewRef.current.designAspect,
      tolerance: viewRef.current.units * 7,
      origin: options.origin,
      angle: options.shift ? 45 : (options.angle ?? c.snapping.angle),
    });
    if (!options.silent) {
      setGuides(result.guides);
      setSnapFeedback(result.label ? result : null);
    }
    return result;
  }
  function pathPaint() {
    const c = props.current,
      ctx = pathContext(),
      source = ctx?.pending ? ctx.data.style : ctx?.layer || c.style,
      visibleDefault = !ctx && !pathStrokeWasSet.current && !source.strokeWidth;
    // Start with a visible open path, but never override an explicitly entered 0.
    // The workbench and first anchor must use exactly the same appearance.
    return {
      paint: true,
      color: source.color || "#dd765d",
      fillSource: source.fillSource,
      strokeSource: source.strokeSource,
      stroke: visibleDefault
        ? source.color
        : source.paint
          ? source.stroke || source.color
          : source.color,
      fillEnabled: source.paint
        ? source.fillEnabled
        : !(source.kind === "path" && source.strokeWidth),
      strokeWidth: visibleDefault ? 1.4 : (source.strokeWidth ?? 0),
      strokeLinecap: source.strokeLinecap || "round",
      strokeLinejoin: source.strokeLinejoin || "round",
    };
  }
  function changePathPaint(patch) {
    if (Object.hasOwn(patch, "strokeWidth")) pathStrokeWasSet.current = true;
    const c = props.current,
      ctx = pathContext(),
      paint = { ...pathPaint(), ...patch };
    if (ctx?.pending) {
      pathSession.change({
        ...penRef.current,
        style: { ...penRef.current.style, ...paint },
      });
      if (ctx.data.target == null) c.onDrawingStyle(paint);
    } else if (ctx) {
      c.onCommit(
        {
          ...c.doc,
          layers: c.doc.layers.map((l, i) =>
            i === ctx.index ? { ...l, ...paint } : l,
          ),
        },
        { key: `path-paint-${ctx.index}-${Object.keys(patch).join("-")}` },
      );
    } else c.onDrawingStyle(paint);
  }
  function zoomAt(factor, clientPoint) {
    const c = cameraRef.current,
      newZoom = clamp(c.zoom * factor, 0.2, 8);
    const anchor = clientPoint ? rootPoint(clientPoint) : { x: c.x, y: c.y },
      ratio = c.zoom / newZoom;
    const next = {
      x: anchor.x + (c.x - anchor.x) * ratio,
      y: anchor.y + (c.y - anchor.y) * ratio,
      zoom: newZoom,
    };
    cameraRef.current = next;
    setCamera(next);
  }
  function fit() {
    const next = { x: 256 * viewRef.current.designAspect, y: 256, zoom: 1 };
    cameraRef.current = next;
    setCamera(next);
  }
  useEffect(() => {
    const svg = svgRef.current,
      wheel = (e) => {
        e.preventDefault();
        zoomAt(Math.exp(-clamp(e.deltaY, -160, 160) * 0.0025), e);
      };
    svg.addEventListener("wheel", wheel, { passive: false });
    return () => svg.removeEventListener("wheel", wheel);
  }, []);
  function cancelGesture() {
    const d = drag.current;
    if (d?.before) props.current.onCancel(d.before, d.beforeSelected);
    if (d?.type.startsWith("pen-")) {
      setPenData(d.beforePen || null);
      setPointSelection(d.beforePen?.selection || []);
    }
    if (d?.type === "pan") {
      cameraRef.current = d.camera;
      setCamera(d.camera);
    }
    drag.current = null;
    setDraft(null);
    setMarquee(null);
    setGuides([]);
    setSnapFeedback(null);
    setPanning(false);
  }
  function cancelInteraction() {
    const had = !!(
      drag.current ||
      penRef.current ||
      context ||
      pathSession.history.current.redo.length
    );
    cancelGesture();
    pathSession.clear();
    setPointSelection([]);
    setActiveSegment(null);
    setPenCursor(null);
    setContext(null);
    return had;
  }
  function setPenData(data) {
    pathSession.set(data);
  }
  function setPointSelection(indices) {
    pointSelectionRef.current = [...new Set(indices)];
    if (penRef.current)
      penRef.current = {
        ...penRef.current,
        selection: pointSelectionRef.current,
      };
    setNodeSelection(pointSelectionRef.current);
    setActiveNode(pointSelectionRef.current.at(-1) ?? -1);
  }
  function pathContext() {
    const c = props.current,
      p = penRef.current;
    if (p)
      return {
        pending: true,
        data: p,
        nodes: p.nodes,
        closed: false,
        tension: p.tension,
      };
    const edit = editableSelection(c.doc, c.selected),
      index = edit.length === 1 ? edit[0] : -1,
      layer = c.doc.layers[index];
    return layer?.nodes
      ? {
          pending: false,
          index,
          layer,
          nodes: worldPathNodes(layer),
          closed: !!layer.closed,
          tension: layer.curveTension ?? 1,
        }
      : null;
  }
  function sessionLayer(p, closed = false) {
    const c = props.current,
      source = p.target != null ? c.doc.layers[p.target] : null;
    const paint = source
      ? {
          ...source,
          ...p.style,
          paint: true,
          fillEnabled: p.style.paint
            ? p.style.fillEnabled
            : !(source.kind === "path" && source.strokeWidth),
          stroke: p.style.paint ? p.style.stroke : p.style.color,
        }
      : p.style;
    const filled = source ? paint.fillEnabled : closed && paint.fillEnabled;
    return worldPathLayer(
      automaticPathNodes(p.nodes, closed, p.tension),
      closed,
      {
        ...paint,
        name: source?.name || (p.kind === "curve" ? "Curve path" : "Pen path"),
        rotation: 0,
        flipX: false,
        flipY: false,
        paint: true,
        fillEnabled: !!filled,
        strokeWidth: paint.strokeWidth ?? (filled ? 0 : 1.4),
        stroke: paint.stroke || paint.color,
        ...(p.nodes.some((n) => n.mode === "auto")
          ? { curveTension: p.tension }
          : {}),
      },
    );
  }
  function workingDocument() {
    const c = props.current,
      p = penRef.current;
    if (!p || p.nodes.length < 2) return c.doc;
    if (
      p.target != null &&
      pathGeometryKey(c.doc.layers[p.target]) !== p.sourceKey
    )
      return c.doc;
    const l = sessionLayer(p);
    return {
      ...c.doc,
      layers:
        p.target != null
          ? c.doc.layers.map((old, i) => (i === p.target ? l : old))
          : [...c.doc.layers, l],
    };
  }
  function finishPen(closed = false) {
    const p = penRef.current;
    if (!p) return false;
    const c = props.current;
    if (p.nodes.length < (closed ? 3 : 2)) {
      c.onError(
        closed
          ? "A closed path needs at least 3 anchors."
          : "Add a second anchor before finishing, saving or applying this path.",
      );
      return false;
    }
    if (
      p.target != null &&
      (pathGeometryKey(c.doc.layers[p.target]) !== p.sourceKey ||
        c.doc.layers[p.target].locked ||
        !c.doc.layers[p.target].visible)
    ) {
      c.onError(
        "The source path changed or was protected. Cancel this draft before continuing.",
      );
      return false;
    }
    const before = c.doc,
      target = p.target ?? before.layers.length;
    if (p.target == null && before.layers.length >= 64) {
      c.onError("A pattern supports up to 64 layers.");
      return false;
    }
    const layer = sessionLayer(p, closed),
      result = c.onCommit(
        {
          ...before,
          layers:
            p.target != null
              ? before.layers.map((l, i) => (i === p.target ? layer : l))
              : [...before.layers, layer],
        },
        { selected: [target] },
      );
    if (result === false) return false;
    drag.current = null;
    pathSession.clear();
    setPointSelection([]);
    setPenCursor(null);
    setDraft(null);
    setActiveSegment(null);
    setSnapFeedback(null);
    setGuides([]);
    return true;
  }
  function prepareExport() {
    return !penRef.current || finishPen(false);
  }
  function continuePath(index, fromStart = false) {
    const c = props.current,
      layer = c.doc.layers[index];
    if (!layer?.nodes || layer.closed || layer.locked || !layer.visible) {
      c.onError("Select an unlocked, open path to continue it.");
      return false;
    }
    if (penRef.current && !finishPen(false)) return false;
    const nodes = fromStart
      ? reversePathNodes(worldPathNodes(layer))
      : worldPathNodes(layer);
    pathSession.clear();
    pathSession.change({
      nodes,
      target: index,
      sourceKey: pathGeometryKey(layer),
      kind: c.tool === "curve" ? "curve" : "pen",
      tension: layer.curveTension ?? curveTension,
      style: { ...layer },
      beforeSelected: [...c.selected],
    });
    setPointSelection([nodes.length - 1]);
    setActiveSegment(null);
    setPenCursor(null);
    if (!["pen", "curve"].includes(c.tool)) onToolChange("pen");
    return true;
  }
  function editWorldNodes(nodes, options = {}, context = pathContext()) {
    if (!context) return false;
    if (context.pending) {
      const next = {
        ...penRef.current,
        nodes,
        ...(options.tension !== undefined ? { tension: options.tension } : {}),
      };
      if (options.preview) setPenData(next);
      else pathSession.change(next);
      return true;
    }
    const c = props.current,
      layer = { ...context.layer, ...(options.meta || {}) },
      updated = withWorldPathNodes(layer, nodes, !options.preview);
    const next = {
      ...c.doc,
      layers: c.doc.layers.map((l, i) => (i === context.index ? updated : l)),
    };
    return options.preview
      ? c.onPreview(next)
      : c.onCommit(next, { key: options.key });
  }
  function undoPath() {
    const p = penRef.current;
    if (!pathSession.undo()) return false;
    drag.current = null;
    setPenCursor(null);
    setActiveSegment(null);
    const restored = penRef.current;
    setPointSelection(
      restored
        ? (restored.selection || [restored.nodes.length - 1]).filter(
            (i) => i >= 0 && i < restored.nodes.length,
          )
        : [],
    );
    if (!penRef.current && p) props.current.onSelection(p.beforeSelected || []);
    return true;
  }
  function redoPath() {
    if (!pathSession.redo()) return false;
    const restored = penRef.current;
    setPointSelection(
      restored
        ? (restored.selection || [restored.nodes.length - 1]).filter(
            (i) => i >= 0 && i < restored.nodes.length,
          )
        : [],
    );
    setPenCursor(null);
    setActiveSegment(null);
    return true;
  }
  function nodeAction(action) {
    if (action === "draft-undo") return undoPath();
    if (action === "draft-redo") return redoPath();
    if (action === "finish") return finishPen(false);
    if (action === "finish-closed") return finishPen(true);
    if (action === "cancel") return cancelInteraction();
    if (action === "new") {
      cancelInteraction();
      forceNewPath.current = true;
      props.current.onSelection([]);
      return;
    }
    const c = props.current,
      context = pathContext(),
      chosen = pointSelectionRef.current;
    if (action === "join") {
      const edit = editableSelection(c.doc, c.selected);
      if (edit.length !== 2) return;
      try {
        const a = edit[0],
          b = edit[1],
          joined = joinedPathLayers(c.doc.layers[a], c.doc.layers[b]);
        c.onCommit(
          {
            ...c.doc,
            layers: c.doc.layers.flatMap((l, i) =>
              i === a ? [joined] : i === b ? [] : [l],
            ),
          },
          { selected: [a - (b < a ? 1 : 0)] },
        );
        setPointSelection([]);
      } catch (error) {
        c.onError(error.message);
      }
      return;
    }
    if (!context) return;
    if (action === "continue-end" || action === "continue-start")
      return continuePath(context.index, action === "continue-start");
    let nodes = structuredClone(context.nodes);
    try {
      if (action === "reverse") {
        editWorldNodes(reversePathNodes(nodes), {}, context);
        setPointSelection(chosen.map((i) => nodes.length - 1 - i));
        setActiveSegment(null);
        return;
      }
      if (action === "toggle-closed" && !context.pending) {
        if (!context.closed && nodes.length < 3)
          throw new Error("A closed path needs at least 3 anchors.");
        editWorldNodes(
          automaticPathNodes(nodes, !context.closed, context.tension),
          { meta: { closed: !context.closed } },
          context,
        );
        return;
      }
      if (action === "split" && !context.pending) {
        const i = chosen.length === 1 ? chosen[0] : -1;
        if (context.closed || i <= 0 || i >= nodes.length - 1)
          throw new Error(
            "Select an interior point on an open path to split it.",
          );
        if (c.doc.layers.length >= 64)
          throw new Error("A pattern supports up to 64 layers.");
        const style = {
            ...context.layer,
            rotation: 0,
            flipX: false,
            flipY: false,
            paint: true,
          },
          a = worldPathLayer(nodes.slice(0, i + 1), false, style),
          b = worldPathLayer(nodes.slice(i), false, {
            ...style,
            name: `${style.name} split`.slice(0, 60),
          });
        c.onCommit(
          {
            ...c.doc,
            layers: c.doc.layers.flatMap((l, j) =>
              j === context.index ? [a, b] : [l],
            ),
          },
          { selected: [context.index, context.index + 1] },
        );
        setPointSelection([]);
        return;
      }
      if (action === "add") {
        const index = activeSegment?.index ?? chosen.at(-1),
          t = activeSegment?.t ?? 0.5;
        if (index == null || (!context.closed && index >= nodes.length - 1))
          throw new Error(
            "Select a segment or a point with a following segment.",
          );
        const inserted = insertPathPoint(nodes, index, t, context.closed);
        editWorldNodes(inserted.nodes, {}, context);
        setPointSelection([inserted.index]);
        setActiveSegment(null);
        return;
      }
      if (!chosen.length) return;
      if (action === "remove") {
        const remaining = nodes.filter((_, i) => !chosen.includes(i));
        if (!context.pending && remaining.length < (context.closed ? 3 : 2))
          throw new Error(
            "Keep at least 3 points in a closed path, or 2 in an open path.",
          );
        if (!remaining.length && context.pending) {
          pathSession.change(null);
          setPointSelection([]);
          return;
        }
        editWorldNodes(
          automaticPathNodes(remaining, context.closed, context.tension),
          {},
          context,
        );
        setPointSelection([Math.min(chosen[0], remaining.length - 1)]);
        setActiveSegment(null);
        return;
      }
      const mode =
        action === "corner"
          ? "corner"
          : action === "smooth"
            ? "smooth"
            : action.replace("mode-", "");
      editWorldNodes(
        setPathPointMode(
          nodes,
          chosen,
          mode,
          context.closed,
          context.tension,
          action === "corner",
        ),
        {},
        context,
      );
    } catch (error) {
      c.onError(error.message);
    }
  }
  function nodeField(key, value) {
    if (!Number.isFinite(value)) return;
    const context = pathContext(),
      index = pointSelectionRef.current.at(-1),
      n = context?.nodes[index];
    if (!n) return;
    let nodes;
    if (key === "x" || key === "y")
      nodes = movePathAnchors(
        context.nodes,
        [index],
        {
          x: key === "x" ? clamp(value, -512, 1024) - n.x : 0,
          y: key === "y" ? clamp(value, -512, 1024) - n.y : 0,
        },
        context.closed,
        context.tension,
      );
    else {
      const [side, field] = key.split("-"),
        handle = n[side] || n;
      const distance =
          field === "length"
            ? clamp(value, 0, 2048)
            : Math.hypot(handle.x - n.x, handle.y - n.y),
        angle =
          field === "angle"
            ? (value * Math.PI) / 180
            : Math.atan2(handle.y - n.y, handle.x - n.x);
      nodes = movePathHandle(context.nodes, index, side, {
        x: n.x + Math.cos(angle) * distance,
        y: n.y + Math.sin(angle) * distance,
      });
      if (!distance) {
        delete nodes[index][side];
        if (pathNodeMode(n) === "symmetric")
          delete nodes[index][side === "in" ? "out" : "in"];
      }
    }
    editWorldNodes(
      nodes,
      { key: `point-${context.index}-${index}-${key}` },
      context,
    );
  }
  function changeTension(value) {
    setCurveTension(value);
    const context = pathContext();
    if (!context) return;
    editWorldNodes(
      automaticPathNodes(context.nodes, context.closed, value),
      {
        tension: value,
        meta: { curveTension: value },
        key: `curve-tension-${context.index}`,
      },
      context,
    );
  }
  function consumeKey(e) {
    const c = props.current,
      command = e.ctrlKey || e.metaKey,
      key = e.key.toLowerCase();
    if (
      command &&
      (key === "z" || key === "y") &&
      (penRef.current ||
        pathSession.history.current.redo.length ||
        pathSession.history.current.undo.length)
    ) {
      if (drag.current) {
        cancelGesture();
        return true;
      }
      key === "y" || e.shiftKey ? redoPath() : undoPath();
      return true;
    }
    if (e.code === "Space") {
      e.preventDefault();
      space.current = true;
      if (drag.current?.type !== "pen-place") setPanning(true);
      return true;
    }
    if (e.key === "Escape") {
      if (drag.current) {
        cancelGesture();
        return true;
      }
      if (cancelInteraction()) return true;
      if (pointSelectionRef.current.length) {
        setPointSelection([]);
        return true;
      }
      if (c.selected.length) {
        c.onSelection([]);
        return true;
      }
      return false;
    }
    if (e.key === "Enter" && penRef.current) {
      finishPen(false);
      return true;
    }
    const context = pathContext();
    if (
      command &&
      key === "a" &&
      context &&
      ["node", "pen", "curve"].includes(c.tool)
    ) {
      setPointSelection(context.nodes.map((_, i) => i));
      return true;
    }
    if (command && key === "j" && ["node", "pen", "curve"].includes(c.tool)) {
      nodeAction("join");
      return true;
    }
    if (
      (e.key === "Delete" || e.key === "Backspace") &&
      context &&
      ["node", "pen", "curve"].includes(c.tool)
    ) {
      if (!pointSelectionRef.current.length && context.pending)
        setPointSelection([context.nodes.length - 1]);
      if (pointSelectionRef.current.length) {
        nodeAction("remove");
        return true;
      }
    }
    if ((e.key === "+" || e.key === "=") && !command) {
      zoomAt(1.25);
      return true;
    }
    if (e.key === "-" && !command) {
      zoomAt(0.8);
      return true;
    }
    if (e.key === "0") {
      fit();
      return true;
    }
    if (
      context &&
      pointSelectionRef.current.length &&
      ["node", "pen", "curve"].includes(c.tool) &&
      e.key.startsWith("Arrow")
    ) {
      const step = (c.grid || 1) * (e.shiftKey ? 10 : 1),
        delta = {
          ArrowLeft: { x: -step, y: 0 },
          ArrowRight: { x: step, y: 0 },
          ArrowUp: { x: 0, y: -step },
          ArrowDown: { x: 0, y: step },
        }[e.key];
      if (delta) {
        editWorldNodes(
          movePathAnchors(
            context.nodes,
            pointSelectionRef.current,
            delta,
            context.closed,
            context.tension,
          ),
          {
            key: `point-nudge-${context.index}-${pointSelectionRef.current.join(",")}`,
          },
          context,
        );
        return true;
      }
    }
    return false;
  }
  useImperativeHandle(ref, () => ({
    cancelInteraction,
    consumeKey,
    finishPen,
    prepareExport,
    hasPendingPath: () => !!penRef.current,
    undoPath,
    redoPath,
    workingDocument,
    focus: () => svgRef.current?.focus(),
    fit,
    nodeAction,
  }));
  function down(e) {
    if (e.button !== 0 && e.button !== 1) return;
    e.preventDefault();
    setContext(null);
    svgRef.current.focus();
    svgRef.current.setPointerCapture(e.pointerId);
    if (e.pointerType === "touch") {
      touches.current.set(e.pointerId, { x: e.clientX, y: e.clientY });
      if (touches.current.size === 2) {
        cancelGesture();
        const [a, b] = [...touches.current.values()],
          middle = { clientX: (a.x + b.x) / 2, clientY: (a.y + b.y) / 2 };
        pinch.current = {
          distance: Math.hypot(b.x - a.x, b.y - a.y),
          camera: cameraRef.current,
          anchor: rootPoint(middle),
        };
        return;
      }
    }
    const c = props.current,
      p = point(e),
      before = c.doc,
      beforeSelected = [...c.selected],
      common = { before, beforeSelected, doc: before, start: p },
      target = e.target.closest?.("[data-layer]"),
      hit = target ? Number(target.dataset.layer) : -1;
    if (c.tool === "hand" || space.current || e.button === 1) {
      drag.current = {
        type: "pan",
        camera: { ...cameraRef.current },
        clientX: e.clientX,
        clientY: e.clientY,
      };
      setPanning(true);
      return;
    }
    if (c.tool === "eyedropper") {
      c.onSampleAppearance(hit, e.shiftKey);
      return;
    }
    if (c.tool === "text") {
      if (hit >= 0 && c.TextEditingPredicate(before.layers[hit])) {
        c.onEditText(hit);
        return;
      }
      const target = snappedPoint(p, { context: null }).point;
      c.onCreateText(target);
      return;
    }
    const handle = e.target.getAttribute("data-handle"),
      edit = editableSelection(before, beforeSelected);
    if (handle && edit.length && ["move", "node"].includes(c.tool)) {
      drag.current = {
        ...common,
        type: handle === "rotate" ? "rotate" : "resize",
        handle,
        ids: edit,
      };
      return;
    }
    const nodeIndex = e.target.getAttribute("data-node"),
      control = e.target.getAttribute("data-control"),
      segmentIndex = e.target
        .closest?.("[data-segment]")
        ?.getAttribute("data-segment"),
      command = e.ctrlKey || e.metaKey,
      pathTool = ["pen", "curve"].includes(c.tool),
      context = pathContext();
    if ((pathTool || c.tool === "node") && nodeIndex !== null && context) {
      const i = Number(nodeIndex),
        n = context.nodes[i];
      if (!n) return;
      if (
        control === "anchor" &&
        context.pending &&
        i === 0 &&
        context.nodes.length >= 3 &&
        !command &&
        !e.altKey &&
        !e.shiftKey
      ) {
        finishPen(true);
        return;
      }
      if (
        control === "anchor" &&
        !context.pending &&
        pathTool &&
        !context.closed &&
        (i === 0 || i === context.nodes.length - 1) &&
        !command &&
        !e.altKey &&
        !e.shiftKey
      ) {
        continuePath(context.index, i === 0);
        drag.current = {
          type: "pen-edit",
          beforePen: penRef.current,
          basePen: penRef.current,
          start: p,
          nodeIndex: penRef.current.nodes.length - 1,
          control: "out",
          ids: [penRef.current.nodes.length - 1],
          moved: false,
        };
        return;
      }
      if (control === "anchor" && e.altKey && !command) {
        setPointSelection([i]);
        nodeAction(
          c.tool === "curve" && pathNodeMode(n) === "corner"
            ? "mode-auto"
            : "corner",
        );
        return;
      }
      let chosen = pointSelectionRef.current;
      if (e.shiftKey && control === "anchor") {
        chosen = chosen.includes(i)
          ? chosen.filter((v) => v !== i)
          : [...chosen, i];
        setPointSelection(chosen);
        if (!chosen.includes(i)) return;
      } else if (!chosen.includes(i) || control !== "anchor") {
        chosen = [i];
        setPointSelection(chosen);
      }
      setActiveSegment(null);
      drag.current = context.pending
        ? {
            type: "pen-edit",
            beforePen: penRef.current,
            basePen: penRef.current,
            start: p,
            nodeIndex: i,
            control,
            ids: chosen,
            moved: false,
          }
        : {
            ...common,
            type: "node",
            index: context.index,
            nodeIndex: i,
            control,
            nodeIds: chosen,
            worldNodes: context.nodes,
            moved: false,
          };
      return;
    }
    if (
      (pathTool || c.tool === "node") &&
      segmentIndex !== undefined &&
      segmentIndex !== null &&
      context
    ) {
      const index = Number(segmentIndex),
        s = pathSegment(context.nodes, index, context.closed);
      if (!s) return;
      const near = nearestPathPoint(
          [s.a, s.b],
          false,
          p,
          viewRef.current.designAspect,
        ),
        selectedSegment = { index, t: near.t, point: near.point };
      setActiveSegment(selectedSegment);
      setPointSelection([]);
      if ((pathTool && !command) || e.altKey) {
        try {
          const inserted = insertPathPoint(
            context.nodes,
            index,
            near.t,
            context.closed,
          );
          if (context.pending) {
            const beforePen = penRef.current,
              next = { ...beforePen, nodes: inserted.nodes };
            setPenData(next);
            drag.current = {
              type: "pen-edit",
              beforePen,
              basePen: next,
              start: p,
              nodeIndex: inserted.index,
              control: "anchor",
              ids: [inserted.index],
              moved: false,
            };
          } else editWorldNodes(inserted.nodes, {}, context);
          setPointSelection([inserted.index]);
          setActiveSegment(null);
        } catch (error) {
          c.onError(error.message);
        }
      } else {
        drag.current = {
          ...common,
          type: "segment",
          index: context.index,
          segment: index,
          t: clamp(near.t, 0.1, 0.9),
          worldNodes: context.nodes,
          moved: false,
        };
      }
      return;
    }
    if (c.tool === "node") {
      if (context && (hit < 0 || hit === context.index)) {
        if (!e.shiftKey) setPointSelection([]);
        drag.current = {
          ...common,
          type: "node-marquee",
          worldNodes: context.nodes,
          additive: e.shiftKey,
          beforePoints: [...pointSelectionRef.current],
        };
        setMarquee({ start: p, end: p });
      } else {
        c.onSelection(hit >= 0 ? [hit] : []);
        setPointSelection([]);
      }
      return;
    }
    if (pathTool) {
      if (!penRef.current && hit >= 0 && !forceNewPath.current) {
        const l = before.layers[hit];
        if (l.nodes && !l.closed && !l.locked) {
          const world = worldPathNodes(l),
            endpoint = [0, world.length - 1].find(
              (i) =>
                Math.hypot(
                  (p.x - world[i].x) * designAspect,
                  p.y - world[i].y,
                ) <
                units * 9,
            );
          if (endpoint != null && !command && !e.altKey) {
            c.onSelection([hit]);
            continuePath(hit, endpoint === 0);
            return;
          }
        }
      }
      if (!penRef.current && before.layers.length >= 64) {
        c.onError("A pattern supports up to 64 layers.");
        return;
      }
      const previous = penRef.current;
      forceNewPath.current = false;
      if (!previous) {
        pathSession.clear();
        c.onSelection([]);
      }
      let data = previous || {
        nodes: [],
        style: {
          ...c.style,
          ...pathPaint(),
        },
        kind: c.tool,
        tension: curveTension,
        beforeSelected,
      };
      if (data.nodes.length >= 400) {
        c.onError(
          "An editable path supports up to 400 points. Finish this path first.",
        );
        return;
      }
      const placed = snappedPoint(p, {
        origin: data.nodes.at(-1),
        shift: e.shiftKey,
        excludeNodes: data.nodes.length ? [data.nodes.length - 1] : [],
      });
      let anchor = {
        ...placed.point,
        mode: c.tool === "curve" && !e.altKey ? "auto" : "corner",
      };
      if (
        data.nodes.length &&
        Math.hypot(
          anchor.x - data.nodes.at(-1).x,
          anchor.y - data.nodes.at(-1).y,
        ) < units
      )
        return;
      const next = {
        ...data,
        nodes: automaticPathNodes([...data.nodes, anchor], false, data.tension),
      };
      setPenData(next);
      lastPlacement.current = {
        index: next.nodes.length - 1,
        x: e.clientX,
        y: e.clientY,
        time: performance.now(),
      };
      setPointSelection([next.nodes.length - 1]);
      setActiveSegment(null);
      setPenCursor(null);
      drag.current = {
        type: "pen-place",
        beforePen: previous,
        basePen: next,
        start: anchor,
        rawStart: p,
        anchor,
        lastPoint: p,
        nodeIndex: next.nodes.length - 1,
        penKind: c.tool,
        moved: false,
      };
      return;
    }
    if (drawingTools.includes(c.tool)) {
      if (before.layers.length >= 64) {
        c.onError("A pattern supports up to 64 layers.");
        return;
      }
      const start = snappedPoint(p, { context: null }).point;
      drag.current = {
        ...common,
        start,
        type: c.tool === "pencil" ? "pencil" : "create",
        tool: c.tool,
        points: [start],
        style: { ...c.style },
      };
      return;
    }
    if (hit >= 0 && before.layers[hit] && !before.layers[hit].locked) {
      const expanded = expandPatternSelection(before, hit);
      let chosen = beforeSelected;
      if (e.shiftKey) {
        const removing = expanded.every((i) => chosen.includes(i));
        chosen = removing
          ? chosen.filter((i) => !expanded.includes(i))
          : [...new Set([...chosen, ...expanded])];
        c.onSelection(chosen);
        if (removing) return;
      } else if (!chosen.includes(hit)) {
        chosen = expanded;
        c.onSelection(chosen);
      }
      const ids = editableSelection(before, chosen);
      if (!ids.length) return;
      if (e.altKey) {
        if (before.layers.length + ids.length > 64) {
          c.onError("A pattern supports up to 64 layers.");
          return;
        }
        const copies = clonePatternLayers(
            ids.map((i) => before.layers[i]),
            0,
          ),
          duplicated = { ...before, layers: [...before.layers, ...copies] },
          newIds = copies.map((_, i) => before.layers.length + i);
        c.onPreview(duplicated, newIds);
        drag.current = {
          ...common,
          doc: duplicated,
          next: duplicated,
          ids: newIds,
          type: "move",
          sx: Number(target.dataset.copyX) || 1,
          sy: Number(target.dataset.copyY) || 1,
        };
      } else
        drag.current = {
          ...common,
          ids,
          type: "move",
          sx: Number(target.dataset.copyX) || 1,
          sy: Number(target.dataset.copyY) || 1,
        };
    } else {
      if (!e.shiftKey) c.onSelection([]);
      drag.current = { ...common, type: "marquee", additive: e.shiftKey };
      setMarquee({ start: p, end: p });
    }
  }
  function motion(e) {
    if (e.pointerType === "touch" && touches.current.has(e.pointerId)) {
      touches.current.set(e.pointerId, { x: e.clientX, y: e.clientY });
      if (pinch.current && touches.current.size === 2) {
        const [a, b] = [...touches.current.values()],
          initial = pinch.current,
          zoom = clamp(
            (initial.camera.zoom * Math.hypot(a.x - b.x, a.y - b.y)) /
              Math.max(1, initial.distance),
            0.2,
            8,
          ),
          rect = svgRef.current.getBoundingClientRect(),
          pxX = (a.x + b.x) / 2 - rect.left - rect.width / 2,
          pxY = (a.y + b.y) / 2 - rect.top - rect.height / 2,
          next = {
            zoom,
            x: initial.anchor.x - (pxX * viewRef.current.base) / zoom,
            y: initial.anchor.y - (pxY * viewRef.current.base) / zoom,
          };
        cameraRef.current = next;
        setCamera(next);
        return;
      }
    }
    const d = drag.current,
      c = props.current,
      p = point(e);
    if (!d) {
      const context = pathContext(),
        segment = e.target.closest?.("[data-segment]")?.dataset.segment,
        node = e.target.getAttribute("data-node");
      if (segment != null && context) {
        const i = Number(segment),
          s = pathSegment(context.nodes, i, context.closed);
        const near = s && nearestPathPoint([s.a, s.b], false, p, designAspect);
        setHoverSegment(near ? { ...near, index: i } : null);
      } else setHoverSegment(null);
      if (penRef.current) {
        if ((node != null && Number(node) !== 0) || segment != null)
          setPenCursor(null);
        else
          setPenCursor({
            ...snappedPoint(p, {
              origin: penRef.current.nodes.at(-1),
              shift: e.shiftKey,
              excludeNodes: [penRef.current.nodes.length - 1],
            }).point,
            corner: e.altKey,
            constrain: e.shiftKey,
            close:
              node != null &&
              Number(node) === 0 &&
              penRef.current.nodes.length >= 3 &&
              !e.altKey,
          });
      }
      return;
    }
    if (d.type === "pan") {
      const next = {
        ...d.camera,
        x: d.camera.x - (e.clientX - d.clientX) * viewRef.current.units,
        y: d.camera.y - (e.clientY - d.clientY) * viewRef.current.units,
      };
      cameraRef.current = next;
      setCamera(next);
      return;
    }
    if (d.type === "marquee" || d.type === "node-marquee") {
      d.end = p;
      setMarquee({ start: d.start, end: p });
      return;
    }
    if (d.type === "pen-place") {
      const data = penRef.current;
      if (!data) return;
      let nodes = structuredClone(data.nodes),
        n = nodes[d.nodeIndex];
      if (
        d.repositioned &&
        !space.current &&
        Math.hypot(p.x - d.lastPoint.x, p.y - d.lastPoint.y) < units * 0.01
      )
        return;
      if (space.current) {
        d.repositioned = true;
        const dx = p.x - d.lastPoint.x,
          dy = p.y - d.lastPoint.y;
        n = movedPathNode(
          n,
          snappedPoint(
            { x: clamp(n.x + dx, -512, 1024), y: clamp(n.y + dy, -512, 1024) },
            { excludeNodes: [d.nodeIndex] },
          ).point,
        );
        d.anchor = { x: n.x, y: n.y };
        d.moved = true;
      } else if (d.penKind === "curve") {
        if (
          Math.hypot(
            p.x - (d.rawStart || d.start).x,
            p.y - (d.rawStart || d.start).y,
          ) <
            units * 2 &&
          !d.moved
        ) {
          d.lastPoint = p;
          return;
        }
        const anchor = snappedPoint(p, {
          origin: nodes[d.nodeIndex - 1],
          shift: e.shiftKey,
          excludeNodes: [d.nodeIndex],
        }).point;
        n = movedPathNode(n, anchor);
        d.anchor = anchor;
        d.moved = true;
      } else {
        if (
          Math.hypot(
            p.x - (d.rawStart || d.start).x,
            p.y - (d.rawStart || d.start).y,
          ) <
            units * 2 &&
          !d.moved
        ) {
          d.lastPoint = p;
          return;
        }
        const handle = snappedPoint(p, {
          origin: n,
          shift: e.shiftKey,
          excludeNodes: [d.nodeIndex],
        }).point;
        n.out = { ...handle };
        if (!e.altKey) n.in = { x: 2 * n.x - handle.x, y: 2 * n.y - handle.y };
        n.mode = e.altKey ? "corner" : penMode;
        d.moved = true;
      }
      nodes[d.nodeIndex] = n;
      d.lastPoint = p;
      setPenData({
        ...data,
        nodes: automaticPathNodes(nodes, false, data.tension),
      });
      return;
    }
    if (d.type === "pen-edit") {
      if (!d.moved && Math.hypot(p.x - d.start.x, p.y - d.start.y) < units)
        return;
      d.moved = true;
      const data = penRef.current;
      if (!data) return;
      const base = d.basePen,
        anchor = base.nodes[d.nodeIndex],
        ctx = { pending: true, data: base, nodes: base.nodes, closed: false };
      const target = snappedPoint(
        d.control === "anchor"
          ? { x: anchor.x + p.x - d.start.x, y: anchor.y + p.y - d.start.y }
          : p,
        {
          context: ctx,
          origin: anchor,
          shift: e.shiftKey,
          excludeNodes: d.ids,
        },
      ).point;
      const nodes =
        d.control === "anchor"
          ? movePathAnchors(
              base.nodes,
              d.ids,
              { x: target.x - anchor.x, y: target.y - anchor.y },
              false,
              base.tension,
            )
          : movePathHandle(base.nodes, d.nodeIndex, d.control, target, {
              independent: e.altKey,
            });
      setPenData({ ...data, nodes });
      return;
    }
    if (d.type === "create") {
      const layer = drawnPatternLayer(
        d.tool,
        d.start,
        snappedPoint(p, {
          context: null,
          doc: d.doc,
          origin: d.tool === "line" ? d.start : undefined,
          shift: d.tool === "line" && e.shiftKey,
        }).point,
        d.style,
        d.tool !== "line" && (c.aspect || e.shiftKey),
        e.altKey,
      );
      d.layer = layer;
      d.distance = Math.hypot(p.x - d.start.x, p.y - d.start.y);
      setDraft(layer);
      return;
    }
    if (d.type === "pencil") {
      if (
        Math.hypot(p.x - d.points.at(-1).x, p.y - d.points.at(-1).y) >=
        Math.max(0.5, units * 1.5)
      ) {
        if (d.points.length < 1600) d.points.push(p);
        else d.points[d.points.length - 1] = p;
      }
      if (d.points.length >= 2) {
        const nodes = boundedDrawnPoints(d.points, Math.max(0.8, units));
        d.layer = worldPathLayer(nodes, false, {
          ...d.style,
          name: "Drawn path",
          paint: true,
          fillEnabled: false,
          strokeWidth: d.style.strokeWidth || 1.4,
          stroke: d.style.strokeWidth ? d.style.stroke : d.style.color,
        });
        setDraft(d.layer);
      }
      return;
    }
    if (["move", "resize", "rotate", "node", "segment"].includes(d.type)) {
      if (!d.moved && Math.hypot(p.x - d.start.x, p.y - d.start.y) < units)
        return;
      d.moved = true;
    }
    let next;
    if (d.type === "move") {
      let delta = { x: (p.x - d.start.x) * d.sx, y: (p.y - d.start.y) * d.sy };
      if (e.shiftKey) {
        if (Math.abs(delta.x) > Math.abs(delta.y)) delta.y = 0;
        else delta.x = 0;
      }
      const snapped = snappedTranslation(d.doc, d.ids, delta, {
        grid: c.grid,
        guides: c.smartGuides && c.snapping.guides && !e.altKey,
        tolerance: units * 5,
      });
      const primary = d.doc.layers[d.ids.at(-1)],
        target = snappedPoint(
          { x: primary.x + delta.x, y: primary.y + delta.y },
          { context: null, doc: d.doc, excludeLayers: d.ids, silent: true },
        );
      if (
        c.smartGuides &&
        !e.altKey &&
        ["Anchor", "Path endpoint", "Object center", "Curve / edge"].includes(
          target.label,
        )
      ) {
        snapped.delta = {
          x: target.point.x - primary.x,
          y: target.point.y - primary.y,
        };
        snapped.guides = target.guides;
        setSnapFeedback(target);
      } else setSnapFeedback(null);
      if (e.shiftKey) {
        if (!delta.y) snapped.delta.y = 0;
        else snapped.delta.x = 0;
      }
      next = translateSelection(d.doc, d.ids, snapped.delta.x, snapped.delta.y);
      setGuides(snapped.guides);
    } else if (d.type === "resize")
      next = resizeSelection(
        d.doc,
        d.ids,
        d.handle,
        snappedPoint(p, { context: null, doc: d.doc, excludeLayers: d.ids })
          .point,
        {
          grid: 0,
          aspect: c.aspect || e.shiftKey,
          center: e.altKey,
        },
      );
    else if (d.type === "rotate")
      next = rotateSelection(d.doc, d.ids, d.start, p, e.shiftKey || !!c.grid);
    else if (d.type === "node" || d.type === "segment") {
      const l = d.doc.layers[d.index],
        base = d.worldNodes;
      let nodes;
      const ctx = {
        pending: false,
        index: d.index,
        layer: l,
        nodes: base,
        closed: l.closed,
      };
      if (d.type === "segment") {
        const grab = cubicPathPoint(
            pathSegment(base, d.segment, l.closed),
            d.t,
          ),
          target = snappedPoint(
            { x: grab.x + p.x - d.start.x, y: grab.y + p.y - d.start.y },
            {
              context: ctx,
              doc: d.doc,
              excludeNodes: [d.segment, (d.segment + 1) % base.length],
            },
          ).point;
        nodes = bendPathSegment(
          base,
          d.segment,
          d.t,
          { x: target.x - grab.x, y: target.y - grab.y },
          l.closed,
        );
      } else {
        const anchor = base[d.nodeIndex],
          target = snappedPoint(
            d.control === "anchor"
              ? { x: anchor.x + p.x - d.start.x, y: anchor.y + p.y - d.start.y }
              : p,
            {
              context: ctx,
              doc: d.doc,
              origin: anchor,
              shift: e.shiftKey,
              excludeNodes: d.nodeIds,
            },
          ).point;
        nodes =
          d.control === "anchor"
            ? movePathAnchors(
                base,
                d.nodeIds,
                { x: target.x - anchor.x, y: target.y - anchor.y },
                l.closed,
                l.curveTension ?? 1,
              )
            : movePathHandle(base, d.nodeIndex, d.control, target, {
                independent: e.altKey,
              });
      }
      next = {
        ...d.doc,
        layers: d.doc.layers.map((layer, i) =>
          i === d.index ? withWorldPathNodes(l, nodes, false) : layer,
        ),
      };
    }
    if (next) {
      d.next = next;
      c.onPreview(next, d.ids ?? d.beforeSelected);
    }
  }
  function up(e) {
    if (e.pointerType === "touch") {
      touches.current.delete(e.pointerId);
      if (pinch.current) {
        pinch.current = null;
        drag.current = null;
        return;
      }
    }
    const d = drag.current;
    if (!d) return;
    // Include the release location even if the browser coalesced the final move.
    motion(e);
    const c = props.current;
    drag.current = null;
    setGuides([]);
    setSnapFeedback(null);
    setMarquee(null);
    setDraft(null);
    setPanning(false);
    if (d.type === "create" || d.type === "pencil") {
      if (d.layer && (d.type === "pencil" || d.distance >= units * 2))
        c.onCommit(
          { ...d.before, layers: [...d.before.layers, d.layer] },
          {
            before: d.before,
            beforeSelected: d.beforeSelected,
            selected: [d.before.layers.length],
          },
        );
    } else if (d.type.startsWith("pen-")) {
      pathSession.record(d.beforePen);
      setPenCursor(null);
    } else if (d.type === "node-marquee") {
      const end = d.end || d.start,
        minX = Math.min(d.start.x, end.x),
        minY = Math.min(d.start.y, end.y),
        maxX = Math.max(d.start.x, end.x),
        maxY = Math.max(d.start.y, end.y);
      const chosen = d.worldNodes.flatMap((n, i) =>
        n.x >= minX && n.x <= maxX && n.y >= minY && n.y <= maxY ? [i] : [],
      );
      if (Math.hypot(end.x - d.start.x, end.y - d.start.y) >= units * 3)
        setPointSelection([...(d.additive ? d.beforePoints : []), ...chosen]);
    } else if (d.type === "marquee") {
      const end = d.end || d.start,
        minX = Math.min(d.start.x, end.x),
        minY = Math.min(d.start.y, end.y),
        maxX = Math.max(d.start.x, end.x),
        maxY = Math.max(d.start.y, end.y);
      const found = d.before.layers.flatMap((l, i) => {
        if (l.locked || !l.visible) return [];
        const b = layerBounds(l);
        return b.maxX >= minX &&
          b.minX <= maxX &&
          b.maxY >= minY &&
          b.minY <= maxY
          ? expandPatternSelection(d.before, i)
          : [];
      });
      if (Math.hypot(end.x - d.start.x, end.y - d.start.y) >= units * 3)
        c.onSelection([
          ...new Set([...(d.additive ? d.beforeSelected : []), ...found]),
        ]);
    } else if (d.next) {
      const completed = ["node", "segment"].includes(d.type)
        ? {
            ...d.next,
            layers: d.next.layers.map((l, i) =>
              i === d.index ? reframePatternPath(l) : l,
            ),
          }
        : d.next;
      c.onCommit(completed, {
        before: d.before,
        beforeSelected: d.beforeSelected,
        selected: d.ids ?? d.beforeSelected,
      });
    }
    if (svgRef.current.hasPointerCapture(e.pointerId))
      svgRef.current.releasePointerCapture(e.pointerId);
  }
  const selectionFrame =
    bounds && (tool === "move" || tool === "node") && !nodeLayer;
  const gridSize = grid || 32;
  const repeatDoc = useMemo(
    () => ({ tileAxes: doc.tileAxes, repeat: doc.repeat }),
    [doc.tileAxes, doc.repeat],
  );
  const rulerTicks = useMemo(() => {
    const step = units > 2 ? 128 : units > 0.8 ? 64 : units > 0.3 ? 32 : 16;
    const x = [],
      y = [];
    for (
      let v = Math.floor(viewX / designAspect / step) * step;
      v <= (viewX + viewWidth) / designAspect;
      v += step
    )
      x.push({ v, px: (v * designAspect - viewX) / units });
    for (
      let v = Math.floor(viewY / step) * step;
      v <= viewY + viewHeight;
      v += step
    )
      y.push({ v, px: (v - viewY) / units });
    return { x, y };
  }, [viewX, viewY, viewWidth, viewHeight, units, designAspect]);
  const help =
    tool === "curve"
      ? "Click to curve · Alt-click corners · first point closes · Enter finishes"
      : tool === "pen"
        ? "Click / drag · Alt breaks tangents · Space repositions · Ctrl/⌘ edits · Enter finishes"
        : tool === "node"
          ? "Drag points & handles · Alt breaks symmetry · double-click a path to edit"
          : tool === "text"
            ? "Click to place text · edit content and typography in the inspector"
            : tool === "eyedropper"
              ? "Click a vector to copy fill & stroke · Shift samples stroke only"
              : drawingTools.includes(tool)
                ? "Drag to draw · Shift constrains · Alt draws from center"
                : "Shift-click to select more · drag empty space to select · Space to pan";
  const editingNodes = pen?.nodes || nodeWorld,
    penLayer = pen && pen.nodes.length >= 2 ? sessionLayer(pen) : null,
    curveContext = pathContext(),
    activePoint = editingNodes?.[activeNode],
    activePointInfo = activePoint
      ? {
          ...activePoint,
          index: activeNode,
          endpoint: activeNode === 0 || activeNode === editingNodes.length - 1,
          last: activeNode === editingNodes.length - 1,
        }
      : null;
  let previewNodes = null;
  if (pen && penCursor) {
    if (penCursor.close)
      previewNodes = automaticPathNodes(pen.nodes, true, pen.tension);
    else {
      const last = pen.nodes.at(-1),
        target = penCursor;
      const next = {
        x: target.x,
        y: target.y,
        mode: tool === "curve" && !penCursor.corner ? "auto" : "corner",
      };
      previewNodes =
        tool === "curve"
          ? automaticPathNodes([...pen.nodes, next], false, pen.tension)
          : [last, next];
    }
  }
  const joinable =
      ids.length === 2 &&
      ids.every((i) => doc.layers[i].nodes && !doc.layers[i].closed),
    canAdd =
      !!editingNodes &&
      editingNodes.length < 400 &&
      (activeSegment != null ||
        (activeNode >= 0 &&
          (!!curveContext?.closed || activeNode < editingNodes.length - 1))),
    pathControls = ["node", "pen", "curve"].includes(tool) ? (
      <PathEditorControls
        tool={tool}
        pending={!!pen}
        count={pen?.nodes.length || 0}
        node={activePointInfo}
        nodeCount={nodeLayer?.nodes.length || 0}
        closed={!!nodeLayer?.closed}
        segment={activeSegment?.index}
        canAdd={canAdd}
        canUndo={!!pathSession.history.current.undo.length}
        canRedo={!!pathSession.history.current.redo.length}
        canContinue={
          !!(pen?.target != null || (nodeLayer && !nodeLayer.closed))
        }
        canJoin={joinable}
        selectionCount={nodeSelection.length}
        selectionMode={
          editingNodes &&
          new Set(
            nodeSelection.map(
              (i) => editingNodes[i] && pathNodeMode(editingNodes[i]),
            ),
          ).size > 1
            ? "mixed"
            : undefined
        }
        penMode={penMode}
        onPenMode={setPenMode}
        tension={curveContext?.tension ?? curveTension}
        showHandles={showHandles}
        onShowHandles={setShowHandles}
        onAction={nodeAction}
        onNodeField={nodeField}
        onMode={(mode) => nodeAction(`mode-${mode}`)}
        onTension={changeTension}
        paint={pathPaint()}
        onPaint={changePathPaint}
      />
    ) : null;
  return (
    <section className="se-editor" aria-label="2D shape editor">
      {pathControls &&
        pathPanelTarget &&
        createPortal(pathControls, pathPanelTarget)}
      <div className="se-view-controls">
        <span>
          <span className="se-live-dot" /> VECTOR CANVAS{" "}
          <small>512 × 512</small>
        </span>
        <div>
          <button
            aria-label="Zoom out canvas"
            title="Zoom out (−)"
            onClick={() => zoomAt(0.8)}
          >
            <Minus size={14} />
          </button>
          <select
            aria-label="Canvas zoom"
            value={String(Math.round(camera.zoom * 100))}
            onChange={(e) =>
              zoomAt(Number(e.target.value) / 100 / cameraRef.current.zoom)
            }
          >
            {[
              ...new Set([
                25,
                50,
                75,
                100,
                150,
                200,
                400,
                800,
                Math.round(camera.zoom * 100),
              ]),
            ]
              .sort((a, b) => a - b)
              .map((v) => (
                <option key={v} value={v}>
                  {v}%
                </option>
              ))}
          </select>
          <button
            aria-label="Zoom in canvas"
            title="Zoom in (+)"
            onClick={() => zoomAt(1.25)}
          >
            <Plus size={14} />
          </button>
          <button onClick={fit} title="Fit tile (0)">
            <Scan size={14} /> Fit
          </button>
        </div>
      </div>
      <div className="se-viewport" ref={stageRef}>
        <svg
          ref={svgRef}
          className="pe-canvas se-canvas"
          tabIndex={0}
          role="group"
          aria-label={CanvasLabel}
          viewBox={`${viewX} ${viewY} ${viewWidth} ${viewHeight}`}
          data-zoom={Math.round(camera.zoom * 100)}
          data-tool={tool}
          style={{
            cursor: panning
              ? "grabbing"
              : tool === "hand"
                ? "grab"
                : tool === "move"
                  ? "default"
                  : tool === "node"
                    ? "default"
                    : "crosshair",
          }}
          onPointerDown={down}
          onPointerMove={motion}
          onPointerUp={up}
          onPointerCancel={() => {
            touches.current.clear();
            pinch.current = null;
            cancelGesture();
          }}
          onLostPointerCapture={() => {
            if (drag.current) cancelGesture();
          }}
          onDoubleClick={(e) => {
            const hit = e.target.closest?.("[data-layer]");
            if (hit && TextEditingPredicate(doc.layers[Number(hit.dataset.layer)])) {
              props.current.onEditText(Number(hit.dataset.layer));
              return;
            }
            const ctx = pathContext(),
              position = point(e);
            // Pointer capture retargets dblclick to the SVG. Resolve the anchor
            // geometrically as well, including one placed by the first click.
            const nearest = ctx?.nodes
              .map((n, i) => ({
                i,
                distance: Math.hypot(
                  (n.x - position.x) * designAspect,
                  n.y - position.y,
                ),
              }))
              .sort((a, b) => a.distance - b.distance)[0];
            const recent = lastPlacement.current;
            const index =
              recent &&
              tool === "curve" &&
              penRef.current &&
              performance.now() - recent.time < 500 &&
              Math.hypot(recent.x - e.clientX, recent.y - e.clientY) < 4
                ? recent.index
                : nearest && nearest.distance <= units * 10
                  ? nearest.i
                  : -1;
            if (
              tool === "pen" &&
              penRef.current &&
              (index === penRef.current.nodes.length - 1 || index < 0)
            ) {
              finishPen(false);
              return;
            }
            if (["node", "pen", "curve"].includes(tool) && index >= 0) {
              setPointSelection([index]);
              nodeAction(
                pathNodeMode(ctx.nodes[index]) === "corner"
                  ? tool === "curve"
                    ? "mode-auto"
                    : "smooth"
                  : "corner",
              );
              return;
            }
            if (hit && ["move", "node"].includes(tool)) {
              const index = Number(hit.dataset.layer),
                layer = doc.layers[index];
              if (layer.nodes && !layer.locked) {
                onSelection([index]);
                onToolChange("node");
              }
            }
          }}
          onContextMenu={(e) => {
            e.preventDefault();
            const hit = e.target.closest?.("[data-layer]");
            if (hit && !selected.includes(Number(hit.dataset.layer)))
              onSelection(
                expandPatternSelection(doc, Number(hit.dataset.layer)),
              );
            const r = stageRef.current.getBoundingClientRect();
            setContext({
              x: Math.min(e.clientX - r.left, r.width - 180),
              y: Math.min(e.clientY - r.top, r.height - 215),
            });
          }}
        >
          <defs>
            <g dangerouslySetInnerHTML={{ __html: [PatternGroundLayer(doc), ...doc.layers].map(PatternColourDefinitions).join("") }} />
            <pattern
              id="se-checker"
              patternUnits="userSpaceOnUse"
              width="24"
              height="24"
            >
              <rect width="24" height="24" fill="#deded6" />
              <path d="M0 0H12V12H0Z M12 12H24V24H12Z" fill="#c6c8bf" />
            </pattern>
            <pattern
              id="se-grid"
              patternUnits="userSpaceOnUse"
              width={gridSize}
              height={gridSize}
            >
              <path
                d={`M${gridSize} 0H0V${gridSize}`}
                fill="none"
                stroke="#678776"
                strokeWidth="0.5"
                opacity="0.38"
              />
            </pattern>
            <clipPath id="se-tile-clip">
              <rect width="512" height="512" />
            </clipPath>
            <filter
              id="se-tile-shadow"
              x="-30%"
              y="-30%"
              width="160%"
              height="160%"
            >
              <feDropShadow
                dx="0"
                dy="7"
                stdDeviation="12"
                floodOpacity=".24"
              />
            </filter>
          </defs>
          <g dangerouslySetInnerHTML={{ __html: textilePreviewDefs }} />
          <g
            ref={contentRef}
            data-coordinate-space="tile"
            transform={`scale(${designAspect} 1)`}
          >
            <rect
              width="512"
              height="512"
              data-canvas-backdrop="true"
              fill={CanvasBackdrop || "url(#se-checker)"}
              filter="url(#se-tile-shadow)"
            />
            <rect
              width="512"
              height="512"
              fill={PatternColourReference(PatternGroundLayer(doc), "fill", doc.background)}
              opacity={doc.backgroundOpacity}
            />
            <g clipPath="url(#se-tile-clip)">
              {doc.layers.map(
                (l, i) =>
                  l.visible &&
                  pen?.target !== i && (
                    <LayerInstances
                      key={i}
                      layer={l}
                      index={i}
                      doc={repeatDoc}
                      interactiveLocked={tool === "eyedropper"}
                    />
                  ),
              )}
              {showGrid && (
                <rect
                  width="512"
                  height="512"
                  fill="url(#se-grid)"
                  pointerEvents="none"
                />
              )}
            </g>
            <rect
              width="512"
              height="512"
              fill="none"
              stroke="#9ba89a"
              strokeOpacity=".4"
              vectorEffect="non-scaling-stroke"
              pointerEvents="none"
            />
            <text
              x="0"
              y={-12 * units}
              fill="#9cab9e"
              fontSize={9 * units}
              letterSpacing={1.1 * units}
              pointerEvents="none"
            >
              {doc.tileAxes === "none" ? "ARTWORK" : "REPEAT TILE"}
            </text>
            {/* Keep an off-tile active motif reachable while its wrapped copies render in the tile. */}
            {ids.map((i) => {
              const l = doc.layers[i];
              return (
                (l.x < 0 || l.x > 512 || l.y < 0 || l.y > 512) && (
                  <g
                    key={`outside-${i}`}
                    data-layer={i}
                    opacity={0.5}
                    transform={paintTransform(l)}
                    dangerouslySetInnerHTML={{
                      __html: textilePreviewShape(l, l.color),
                    }}
                  />
                )
              );
            })}
            {ids.length > 1 &&
              tool === "move" &&
              ids.map((i) => {
                const l = doc.layers[i];
                return (
                  <rect
                    key={`outline-${i}`}
                    x={l.x - l.width / 2}
                    y={l.y - l.height / 2}
                    width={l.width}
                    height={l.height}
                    transform={`rotate(${l.rotation} ${l.x} ${l.y})`}
                    fill="none"
                    stroke="#b9d6a6"
                    strokeOpacity=".5"
                    vectorEffect="non-scaling-stroke"
                    pointerEvents="none"
                  />
                );
              })}
            {selectionFrame && (
              <g
                transform={`rotate(${bounds.rotation || 0} ${bounds.x} ${bounds.y})`}
              >
                <rect
                  x={bounds.x - bounds.width / 2}
                  y={bounds.y - bounds.height / 2}
                  width={bounds.width}
                  height={bounds.height}
                  fill="none"
                  stroke="#c1dcab"
                  strokeWidth="1.3"
                  vectorEffect="non-scaling-stroke"
                  pointerEvents="none"
                />
                <path
                  d={`M${bounds.x} ${bounds.y - bounds.height / 2}v${-25 * units}`}
                  stroke="#c1dcab"
                  vectorEffect="non-scaling-stroke"
                  pointerEvents="none"
                />
                <circle
                  data-handle="rotate"
                  aria-label="Rotate selected motif"
                  cx={bounds.x}
                  cy={bounds.y - bounds.height / 2 - 29 * units}
                  r={handleUnit * 0.65}
                  fill="#d4e8c0"
                  stroke="#28382b"
                  vectorEffect="non-scaling-stroke"
                  style={{ cursor: "grab" }}
                />
                {Object.keys(handleNames).map((h) => {
                  const x =
                      bounds.x +
                      (h.includes("w")
                        ? -bounds.width / 2
                        : h.includes("e")
                          ? bounds.width / 2
                          : 0),
                    y =
                      bounds.y +
                      (h.includes("n")
                        ? -bounds.height / 2
                        : h.includes("s")
                          ? bounds.height / 2
                          : 0),
                    cursor =
                      h === "n" || h === "s"
                        ? "ns-resize"
                        : h === "e" || h === "w"
                          ? "ew-resize"
                          : h === "nw" || h === "se"
                            ? "nwse-resize"
                            : "nesw-resize";
                  return (
                    <rect
                      key={h}
                      data-handle={h}
                      aria-label={
                        h === "se"
                          ? "Resize selected motif"
                          : `Resize ${handleNames[h]}`
                      }
                      x={x - handleUnit / 2}
                      y={y - handleUnit / 2}
                      width={handleUnit}
                      height={handleUnit}
                      rx={units}
                      fill="#d4e8c0"
                      stroke="#28382b"
                      vectorEffect="non-scaling-stroke"
                      style={{ cursor }}
                    />
                  );
                })}
              </g>
            )}
            {draft && (
              <g
                opacity={0.85}
                transform={paintTransform(draft)}
                pointerEvents="none"
                dangerouslySetInnerHTML={{
                  __html: textilePreviewShape(draft, draft.color),
                }}
              />
            )}
            {penLayer && (
              <g
                data-path-draft="true"
                opacity={penLayer.opacity}
                transform={paintTransform(penLayer)}
                pointerEvents="none"
                dangerouslySetInnerHTML={{
                  __html: textilePreviewShape(penLayer, penLayer.color),
                }}
              />
            )}
            {pen && penCursor && previewNodes && (
              <path
                d={patternNodesPath(previewNodes, penCursor.close)}
                fill="none"
                stroke="#c5ddb0"
                strokeDasharray={`${4 * units} ${3 * units}`}
                strokeWidth="1.5"
                vectorEffect="non-scaling-stroke"
                pointerEvents="none"
              />
            )}
            {editingNodes && (
              <PathEditorOverlay
                nodes={editingNodes}
                closed={!pen && !!nodeLayer?.closed}
                pending={!!pen}
                selected={nodeSelection}
                units={units}
                showHandles={showHandles}
                tool={tool}
                hover={hoverSegment}
                activeSegment={activeSegment}
              />
            )}
            {marquee && (
              <rect
                x={Math.min(marquee.start.x, marquee.end.x)}
                y={Math.min(marquee.start.y, marquee.end.y)}
                width={Math.abs(marquee.end.x - marquee.start.x)}
                height={Math.abs(marquee.end.y - marquee.start.y)}
                fill="#b5d69b"
                fillOpacity=".12"
                stroke="#b5d69b"
                strokeDasharray={`${4 * units} ${3 * units}`}
                strokeWidth={units}
                pointerEvents="none"
              />
            )}
            {snapFeedback && (
              <g
                pointerEvents="none"
                aria-label={`Snapped to ${snapFeedback.label.toLowerCase()}`}
              >
                <circle
                  cx={snapFeedback.point.x}
                  cy={snapFeedback.point.y}
                  r={units * 6}
                  fill="none"
                  stroke="#f1c797"
                  strokeWidth={units}
                />
                <path
                  d={`M${snapFeedback.point.x - units * 9} ${snapFeedback.point.y}h${units * 18}M${snapFeedback.point.x} ${snapFeedback.point.y - units * 9}v${units * 18}`}
                  stroke="#f1c797"
                  strokeWidth={units}
                />
                <text
                  x={snapFeedback.point.x + units * 12}
                  y={snapFeedback.point.y - units * 9}
                  fill="#f1c797"
                  fontSize={units * 10}
                >
                  {snapFeedback.label}
                </text>
              </g>
            )}
            {guides.map((g, i) => (
              <path
                key={i}
                d={
                  g.axis === "x" ? `M${g.value} -50V562` : `M-50 ${g.value}H562`
                }
                stroke="#e6af88"
                strokeDasharray={`${3 * units} ${3 * units}`}
                strokeWidth={units}
                pointerEvents="none"
              />
            ))}
          </g>
        </svg>
        <div className="se-ruler se-ruler-x" aria-hidden="true">
          {rulerTicks.x.map(({ v, px }) => (
            <span key={v} style={{ left: px }}>
              {v}
            </span>
          ))}
        </div>
        <div className="se-ruler se-ruler-y" aria-hidden="true">
          {rulerTicks.y.map(({ v, px }) => (
            <span key={v} style={{ top: px }}>
              {v}
            </span>
          ))}
        </div>
        <div className="se-ruler-corner" aria-hidden="true">
          <Grip size={12} />
        </div>
        {tool === "node" && current && !current.nodes && (
          <div className="se-canvas-notice">
            Convert this shape to a path in the inspector to edit its points.
          </div>
        )}
        {!doc.layers.length && !draft && !pen && (
          <div className="se-canvas-empty">
            <MousePointer2 size={20} />
            <span>A blank canvas, endless possibilities.</span>
            <small>Choose a shape tool above, then drag to draw.</small>
          </div>
        )}
        {context && (
          <div
            className="se-context-menu"
            role="menu"
            style={{
              left: Math.max(26, context.x),
              top: Math.max(26, context.y),
            }}
          >
            {[
              ["duplicate", "Duplicate", "⌘D"],
              ["group", "Group selection", "⌘G"],
              ["front", "Bring to front", "]"],
              ["back", "Send to back", "["],
              ["lock", "Lock selection", ""],
              ["delete", "Delete", "⌫"],
            ].map(([action, label, key]) => (
              <button
                key={action}
                role="menuitem"
                disabled={!ids.length || (action === "group" && ids.length < 2)}
                onClick={() => {
                  onAction(action);
                  setContext(null);
                }}
              >
                <span>{label}</span>
                <kbd>{key}</kbd>
              </button>
            ))}
          </div>
        )}
      </div>
      {pathControls && !pathPanelTarget && pathControls}
      {pen && (
        <div className="se-node-bar se-live-path-bar">
          <span>
            {pen.nodes.length} anchors ·{" "}
            {pen.target != null ? "extending path" : `${pen.kind} draft`}
          </span>
          <button
            disabled={pen.nodes.length < 2}
            onClick={() => finishPen(false)}
            aria-label="Finish drawing"
          >
            <CornerDownLeft size={13} /> Finish
          </button>
          <button
            disabled={pen.nodes.length < 3}
            onClick={() => finishPen(true)}
            aria-label="Close drawing"
          >
            Close
          </button>
          <button onClick={cancelInteraction} aria-label="Cancel drawing">
            Cancel
          </button>
        </div>
      )}
      <div className="se-canvas-status">
        <span>{help}</span>
        <small>
          {selected.length
            ? `${selected.length} selected`
            : `${doc.layers.length} shapes`}{" "}
          <i /> {Math.round(camera.zoom * 100)}%
        </small>
      </div>
    </section>
  );
});
export default ShapeEditorCanvas;

import { useEffect, useRef, useState } from "react";
import {
  stampAngleDelta,
  stampCanvasPoint,
  stampTransformDraft,
} from "./stampTransform.js";

const modes = { g: "move", r: "rotate", s: "scale" };
const isEditing = (target) =>
  Boolean(
    target?.closest?.(
      'input, textarea, select, [contenteditable]:not([contenteditable="false"]), [role="textbox"]',
    ),
  );

// Both captured drags and Blender-style modal transforms have the same draft,
// cancellation guards and single history transaction. No document is changed
// while the pointer moves, and native text editing never starts a transform.
export default function useStampTransform(options) {
  const callbacks = useRef(options);
  callbacks.current = options;
  const board = useRef(null),
    gesture = useRef(null),
    draftRef = useRef(null),
    lastPointer = useRef(null),
    suppressClick = useRef(false),
    suppressMenu = useRef(false),
    handlers = useRef(null);
  const [draft, setDraft] = useState(null),
    [active, setActive] = useState(null);

  function viewportContains(target) {
    return Boolean(
      target instanceof Node && board.current?.parentElement?.contains(target),
    );
  }
  function finish(cancel = false, resetUI = true) {
    const g = gesture.current,
      next = draftRef.current;
    if (!g) return false;
    gesture.current = null;
    draftRef.current = null;
    if (resetUI) {
      setDraft(null);
      setActive(null);
    }
    // Clear the transaction before release: lostpointercapture must not cancel
    // an already confirmed edit or append a second history entry.
    try {
      if (g.captureTarget?.hasPointerCapture(g.pointerId))
        g.captureTarget.releasePointerCapture(g.pointerId);
    } catch {
      /* A removed DOM target has already lost its capture. */
    }
    const { documentRef, onCommit } = callbacks.current,
      owner = documentRef.current.layers.find((layer) => layer.id === g.id);
    if (
      !cancel &&
      next &&
      documentRef.current === g.document &&
      owner?.visible &&
      !owner.locked &&
      JSON.stringify(owner) === g.before
    )
      onCommit(next, g.id);
    return true;
  }
  function begin(layer, mode, source, pointer, captureTarget = null) {
    finish(true);
    if (!layer || layer.locked || !layer.visible || !board.current)
      return false;
    const rect = board.current.getBoundingClientRect();
    if (!rect.width || !rect.height) return false;
    callbacks.current.onSelect(layer.id);
    board.current.focus({ preventScroll: true });
    const start = stampCanvasPoint(pointer, rect);
    gesture.current = {
      id: layer.id,
      document: callbacks.current.documentRef.current,
      before: JSON.stringify(layer),
      layer,
      mode,
      source,
      axis: null,
      rect,
      start,
      lastPointer: pointer,
      lastAngle: Math.atan2(start.y - layer.y, start.x - layer.x),
      angleDelta: 0,
      pointerId: pointer.pointerId,
      captureTarget,
    };
    setActive({ id: layer.id, mode, source, axis: null });
    return true;
  }
  function startDrag(e, layer, mode = "move") {
    if (e.button !== 0 || layer.locked) return;
    e.preventDefault();
    e.stopPropagation();
    const target = e.currentTarget;
    if (begin(layer, mode, "pointer", e, target)) {
      try {
        target.setPointerCapture(e.pointerId);
      } catch {
        finish(true);
      }
    }
  }
  function startModal(mode) {
    const { documentRef, selectionRef } = callbacks.current,
      layer = documentRef.current.layers.find(
        (l) => l.id === selectionRef.current,
      );
    if (!layer || layer.locked || !layer.visible || !board.current)
      return false;
    const rect = board.current.getBoundingClientRect(),
      pointer = lastPointer.current || {
        clientX: rect.left + ((layer.x + 60) / 512) * rect.width,
        clientY: rect.top + (layer.y / 512) * rect.height,
      };
    return begin(layer, mode, "modal", pointer);
  }
  function update(pointer) {
    const g = gesture.current;
    if (!g) return;
    g.lastPointer = { clientX: pointer.clientX, clientY: pointer.clientY };
    const point = stampCanvasPoint(pointer, g.rect);
    if (
      g.mode === "rotate" &&
      Math.hypot(point.x - g.layer.x, point.y - g.layer.y) >= 1
    ) {
      const angle = Math.atan2(point.y - g.layer.y, point.x - g.layer.x);
      g.angleDelta += stampAngleDelta(g.lastAngle, angle);
      g.lastAngle = angle;
    }
    const next = stampTransformDraft(g.layer, g.mode, g.start, point, {
      snap: pointer.shiftKey,
      axis: g.axis,
      angleDelta: g.angleDelta,
    });
    if (JSON.stringify(next) === JSON.stringify(draftRef.current)) return;
    draftRef.current = next;
    setDraft(next);
  }
  function pointerMove(e) {
    if (viewportContains(e.target))
      lastPointer.current = { clientX: e.clientX, clientY: e.clientY };
    const g = gesture.current;
    if (!g || (g.source === "pointer" && e.pointerId !== g.pointerId)) return;
    update(e);
  }
  function pointerDown(e) {
    suppressClick.current = false;
    suppressMenu.current = false;
    if (gesture.current?.source !== "modal") return;
    if (!viewportContains(e.target)) {
      finish(true);
      return;
    }
    if (e.button !== 0 && e.button !== 2) return;
    e.preventDefault();
    e.stopPropagation();
    suppressClick.current = e.button === 0;
    suppressMenu.current = e.button === 2;
    finish(e.button === 2);
  }
  function keyDown(e) {
    if (e.defaultPrevented || e.isComposing) return;
    const key = e.key.toLowerCase(),
      g = gesture.current;
    if (key === "escape" && g) {
      e.preventDefault();
      finish(true);
      return;
    }
    // Preserve the existing document undo, including rich-text transactions.
    if ((e.ctrlKey || e.metaKey) && key === "z") {
      e.preventDefault();
      callbacks.current.onHistory(e.shiftKey ? "redo" : "undo");
      return;
    }
    if (isEditing(e.target) || e.ctrlKey || e.metaKey || e.altKey) return;
    if (g?.source === "modal") {
      if (key === "enter") {
        e.preventDefault();
        finish();
        return;
      }
      if (["x", "y"].includes(key) && g.mode !== "rotate") {
        e.preventDefault();
        g.axis = g.axis === key ? null : key;
        setActive({ id: g.id, mode: g.mode, source: g.source, axis: g.axis });
        update({ ...g.lastPointer, shiftKey: e.shiftKey });
        return;
      }
      if (key === "shift") update({ ...g.lastPointer, shiftKey: true });
    }
    if (!e.repeat && modes[key] && g?.source !== "pointer") {
      if (startModal(modes[key])) e.preventDefault();
    }
  }
  handlers.current = { pointerMove, pointerDown, keyDown, finish, update };
  useEffect(() => {
    const move = (e) => handlers.current.pointerMove(e),
      down = (e) => handlers.current.pointerDown(e),
      key = (e) => handlers.current.keyDown(e),
      up = (e) => {
        const g = gesture.current;
        if (g?.source === "pointer" && e.pointerId === g.pointerId)
          handlers.current.finish();
      },
      cancel = (e) => {
        const g = gesture.current;
        if (g && (g.source === "modal" || e.pointerId === g.pointerId))
          handlers.current.finish(true);
      },
      blur = () => {
        lastPointer.current = null;
        handlers.current.finish(true);
      },
      focus = (e) => {
        if (gesture.current?.source === "modal" && isEditing(e.target)) blur();
      },
      keyUp = (e) => {
        const g = gesture.current;
        if (g && e.key === "Shift")
          handlers.current.update({ ...g.lastPointer, shiftKey: false });
      },
      click = (e) => {
        if (!suppressClick.current) return;
        suppressClick.current = false;
        e.preventDefault();
        e.stopPropagation();
      },
      menu = (e) => {
        if (!suppressMenu.current) return;
        suppressMenu.current = false;
        e.preventDefault();
        e.stopPropagation();
      };
    window.addEventListener("pointermove", move);
    window.addEventListener("pointerdown", down, true);
    window.addEventListener("pointerup", up);
    window.addEventListener("pointercancel", cancel);
    window.addEventListener("lostpointercapture", cancel, true);
    window.addEventListener("keydown", key);
    window.addEventListener("keyup", keyUp);
    window.addEventListener("click", click, true);
    window.addEventListener("contextmenu", menu, true);
    window.addEventListener("focusin", focus);
    window.addEventListener("blur", blur);
    window.addEventListener("resize", blur);
    return () => {
      window.removeEventListener("pointermove", move);
      window.removeEventListener("pointerdown", down, true);
      window.removeEventListener("pointerup", up);
      window.removeEventListener("pointercancel", cancel);
      window.removeEventListener("lostpointercapture", cancel, true);
      window.removeEventListener("keydown", key);
      window.removeEventListener("keyup", keyUp);
      window.removeEventListener("click", click, true);
      window.removeEventListener("contextmenu", menu, true);
      window.removeEventListener("focusin", focus);
      window.removeEventListener("blur", blur);
      window.removeEventListener("resize", blur);
      handlers.current.finish(true, false);
    };
  }, []);
  return {
    board,
    gesture,
    draft,
    active,
    startDrag,
    startModal,
    cancel: () => finish(true),
  };
}

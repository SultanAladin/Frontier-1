import { useRef, useState } from "react";
import { validatePattern } from "./patternDocument.js";

// A pointer gesture is one transaction. Previews never enter history, and cancel
// restores the exact pre-gesture snapshot. Selection is part of undo/redo too.
export default function usePatternHistory(createDocument, onError) {
  const [state, setState] = useState(() => {
    const doc = validatePattern(createDocument());
    return {
      doc,
      selected: doc.layers.length ? [0] : [],
      dirty: false,
      previewing: false,
    };
  });
  const stateRef = useRef(state),
    docRef = useRef(state.doc),
    past = useRef([]),
    future = useRef([]),
    merge = useRef(null);
  const sync = (next) => {
    stateRef.current = next;
    docRef.current = next.doc;
    setState(next);
  };
  const selectedFor = (doc, ids) =>
    [...new Set(ids)].filter((i) => Number.isInteger(i) && doc.layers[i]);
  function setSelected(value) {
    const current = stateRef.current;
    const ids = typeof value === "function" ? value(current.selected) : value;
    merge.current = null;
    sync({ ...current, selected: selectedFor(current.doc, ids) });
  }
  function commit(next, options = {}) {
    const current = stateRef.current,
      before = options.before || current.doc;
    try {
      const doc = validatePattern(next),
        original = validatePattern(before),
        selected = selectedFor(doc, options.selected ?? current.selected);
      if (JSON.stringify(original) === JSON.stringify(doc)) {
        sync({ ...current, doc, selected, previewing: false });
        return true;
      }
      const now = Date.now(),
        coalesce =
          options.key &&
          !options.before &&
          merge.current?.key === options.key &&
          now - merge.current.time < 700;
      if (!coalesce) {
        past.current.push({
          doc: original,
          selected: selectedFor(
            original,
            options.beforeSelected ?? current.selected,
          ),
        });
        if (past.current.length > 80) past.current.shift();
      }
      future.current = [];
      merge.current = options.key ? { key: options.key, time: now } : null;
      sync({ doc, selected, dirty: true, previewing: false });
      onError("");
      return true;
    } catch (error) {
      if (options.before)
        sync({
          ...current,
          doc: before,
          selected: selectedFor(
            before,
            options.beforeSelected ?? current.selected,
          ),
          previewing: false,
        });
      onError(error.message);
      return false;
    }
  }
  function preview(next, selected) {
    const current = stateRef.current;
    sync({
      ...current,
      doc: next,
      selected:
        selected === undefined ? current.selected : selectedFor(next, selected),
      previewing: true,
    });
  }
  function cancel(before, selected) {
    const current = stateRef.current;
    sync({
      ...current,
      doc: before,
      selected: selectedFor(before, selected ?? current.selected),
      previewing: false,
    });
  }
  function undo() {
    if (!past.current.length) return;
    const current = stateRef.current;
    future.current.push({ doc: current.doc, selected: current.selected });
    merge.current = null;
    sync({ ...past.current.pop(), dirty: true, previewing: false });
    onError("");
  }
  function redo() {
    if (!future.current.length) return;
    const current = stateRef.current;
    past.current.push({ doc: current.doc, selected: current.selected });
    merge.current = null;
    sync({ ...future.current.pop(), dirty: true, previewing: false });
    onError("");
  }
  return {
    ...state,
    docRef,
    commit,
    setSelected,
    preview,
    cancel,
    undo,
    redo,
    canUndo: !!past.current.length,
    canRedo: !!future.current.length,
  };
}

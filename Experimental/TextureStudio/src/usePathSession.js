import { useRef, useState } from "react";

// Unfinished paths have their own transactional history. The document is not
// mutated until Finish/Close; one document undo then removes the whole path.
export default function usePathSession(onStateChange) {
  const ref = useRef(null),
    history = useRef({ undo: [], redo: [] }),
    reported = useRef("");
  const [state, setState] = useState(null);
  const notify = () => {
    const summary = {
      active: !!ref.current,
      points: ref.current?.nodes.length || 0,
      canUndo: !!history.current.undo.length,
      canRedo: !!history.current.redo.length,
    };
    const key = JSON.stringify(summary);
    if (key !== reported.current) {
      reported.current = key;
      onStateChange?.(summary);
    }
  };
  const set = (data) => {
    ref.current = data;
    setState(data ? { ...data, nodes: [...data.nodes] } : null);
    notify();
  };
  const signature = (data) =>
    JSON.stringify(
      data && [data.nodes, data.tension, data.target, data.kind, data.style],
    );
  function record(before, after = ref.current) {
    if (signature(before) === signature(after)) return false;
    history.current.undo.push(before ? structuredClone(before) : null);
    if (history.current.undo.length > 80) history.current.undo.shift();
    history.current.redo = [];
    set(after);
    return true;
  }
  function change(data) {
    record(ref.current, data);
  }
  function clear() {
    history.current = { undo: [], redo: [] };
    set(null);
  }
  function undo() {
    if (!history.current.undo.length) return false;
    history.current.redo.push(
      ref.current ? structuredClone(ref.current) : null,
    );
    set(history.current.undo.pop());
    return true;
  }
  function redo() {
    if (!history.current.redo.length) return false;
    history.current.undo.push(
      ref.current ? structuredClone(ref.current) : null,
    );
    set(history.current.redo.pop());
    return true;
  }
  return { ref, state, history, set, change, record, clear, undo, redo };
}

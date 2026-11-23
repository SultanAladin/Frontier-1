import { useEffect, useRef, useState } from "react";
import {
  createTextureDocument,
  parseTextureDocument,
  validateTextureDocument,
  TEXTURE_DRAFT_KEY,
} from "./textureDocument.js";
import {
  createTextureHistory,
  commitTextureHistory,
  travelTextureHistory,
  textureHistoryTimeline,
  restoreTextureHistory,
} from "./textureHistory.js";
function restore() {
  try {
    const saved = localStorage.getItem(TEXTURE_DRAFT_KEY);
    return {
      doc: saved ? parseTextureDocument(saved) : createTextureDocument(),
      error: "",
      restored: !!saved,
    };
  } catch {
    return {
      doc: createTextureDocument(),
      restored: false,
      error:
        "The local layer draft could not be restored. A new project has been opened.",
    };
  }
}
function validSelection(doc, selection) {
  const layer =
    doc.layers.find((l) => l.id === selection?.id) || doc.layers.at(-1);
  return {
    id: layer?.id || null,
    mask: !!(layer?.mask && selection?.id === layer.id && selection?.mask),
  };
}
export default function useTextureDocument() {
  const [initial] = useState(restore);
  const [state, setState] = useState(() =>
    createTextureHistory(
      {
        doc: initial.doc,
        selection: validSelection(
          initial.doc,
          (() => {
            try {
              return JSON.parse(
                localStorage.getItem("alloy-texture-authoring-v1"),
              )?.selection;
            } catch {
              return null;
            }
          })(),
        ),
      },
      initial.restored ? "Reopened local project" : "New project",
    ),
  );
  const current = useRef(state);
  current.current = state;
  const [error, setError] = useState(initial.error),
    [status, setStatus] = useState("Local layer project");
  function sync(next) {
    current.current = next;
    setState(next);
  }
  function commit(input, { selection, key, label } = {}) {
    try {
      const doc = validateTextureDocument(input),
        before = current.current.present;
      if (JSON.stringify(doc) === JSON.stringify(before.doc)) {
        if (selection) select(selection.id, selection.mask);
        return false;
      }
      sync(
        commitTextureHistory(
          current.current,
          {
            doc,
            selection: validSelection(doc, selection || before.selection),
          },
          { key, label },
        ),
      );
      setError("");
      setStatus("Saving layer draft…");
      return true;
    } catch (e) {
      setError(e.message);
      return false;
    }
  }
  function select(id, mask = false) {
    const h = current.current;
    sync({
      ...h,
      present: {
        ...h.present,
        selection: validSelection(h.present.doc, { id, mask }),
      },
      key: null,
    });
  }
  function travel(direction) {
    const h = current.current,
      next = travelTextureHistory(h, direction);
    if (next === h) return false;
    sync(next);
    setError("");
    setStatus("Saving layer draft…");
    return true;
  }
  function restoreRevision(id) {
    const h = current.current,
      next = restoreTextureHistory(h, id);
    if (next === h) return false;
    sync(next);
    setError("");
    setStatus("Saving layer draft…");
    return true;
  }
  function save() {
    try {
      localStorage.setItem(
        TEXTURE_DRAFT_KEY,
        JSON.stringify(current.current.present.doc),
      );
      setStatus("Layer draft saved locally");
      return true;
    } catch {
      setStatus("Local storage unavailable");
      setError(
        "Local storage is unavailable. Save a project file to keep your layer setup.",
      );
      return false;
    }
  }
  useEffect(() => {
    const timer = setTimeout(save, 500);
    return () => clearTimeout(timer);
  }, [state.present.doc]);
  useEffect(
    () => () => {
      try {
        localStorage.setItem(
          TEXTURE_DRAFT_KEY,
          JSON.stringify(current.current.present.doc),
        );
      } catch {
        /* The project-file action remains available without storage. */
      }
    },
    [],
  );
  return {
    ...state.present,
    docRef: {
      get current() {
        return current.current.present.doc;
      },
    },
    error,
    setError,
    status,
    commit,
    select,
    save,
    undo: () => travel("undo"),
    redo: () => travel("redo"),
    canUndo: state.past.length > 0,
    canRedo: state.future.length > 0,
    entries: textureHistoryTimeline(state),
    currentRevisionId: state.present.revision.id,
    restoreRevision,
  };
}

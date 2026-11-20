/*============================================================================================================================================
                                                       STUDIO STATE
============================================================================================================================================*/
// 🧩 Single source of truth for the ForceField Studio: the authored document, the selection, the transport clock and the undo ledger. Panels
// subscribe with a reason tag so a transport tick never rebuilds the inspector, and every authored change passes through one coalescing
// history path. The document is plain JSON and round-trips through SanitizeDocument on every write.

import { SanitizeDocument, DefaultDocument, PrepareFrame, NewSource, DEFAULT_OVERLAY, DEFAULT_SIM } from './FieldEvaluator.js';
import { LawById } from './ForceFieldCatalog.js';

const HISTORY_LIMIT = 60;
const COALESCE_WINDOW = 900;

const deepClone = (value) => (typeof structuredClone === 'function' ? structuredClone(value) : JSON.parse(JSON.stringify(value)));

export function CreateStudioState(options = {}) {
  const storageKey = options.StorageKey || 'frontier.forcefield.document.v1';
  let document = SanitizeDocument(options.Document || DefaultDocument());
  let selection = options.Selection || (document.Sources[0] ? document.Sources[0].Id : null);
  let time = 0;
  let playing = options.Playing !== false;
  let probe = [2.4, 1.8, 1.2];
  let revision = 0;
  let frameCache = { Key: '', Runtime: [] };
  const listeners = new Set();
  const undoStack = [];
  const redoStack = [];
  let lastCoalesce = { Key: '', At: 0 };
  let persistTimer = 0;

  const Emit = (reason) => {
    for (const listener of listeners) {
      try {
        listener(reason);
      } catch (error) {
        console.error('[ForceFieldStudio] listener failed', error);
      }
    }
  };

  const Snapshot = () => deepClone({ Document: document, Selection: selection });

  function PushHistory(coalesceKey) {
    const now = Date.now();
    if (coalesceKey && lastCoalesce.Key === coalesceKey && now - lastCoalesce.At < COALESCE_WINDOW && undoStack.length) {
      lastCoalesce.At = now;
      return;
    }
    undoStack.push(Snapshot());
    if (undoStack.length > HISTORY_LIMIT) undoStack.shift();
    redoStack.length = 0;
    lastCoalesce = { Key: coalesceKey || '', At: now };
  }

  function Restore(snapshot) {
    document = SanitizeDocument(snapshot.Document);
    selection = snapshot.Selection && document.Sources.some((s) => s.Id === snapshot.Selection) ? snapshot.Selection : document.Sources[0]?.Id ?? null;
    revision += 1;
    frameCache.Key = '';
    Emit('document');
    Emit('selection');
  }

  const Persist = () => {
    if (typeof localStorage === 'undefined') return;
    clearTimeout(persistTimer);
    persistTimer = setTimeout(() => {
      try {
        localStorage.setItem(storageKey, JSON.stringify({ Document: document, Selection: selection }));
      } catch {
        /* quota or private mode: persistence is best-effort */
      }
    }, 450);
  };

  const api = {
    // ---------------------------------------------------------------------------------------------- reads
    Get: () => ({ Document: document, Selection: selection, Time: time, Playing: playing, Probe: probe, Revision: revision }),
    Document: () => document,
    Overlay: () => document.Overlay,
    Sim: () => document.Sim,
    Selection: () => selection,
    SelectedSource: () => document.Sources.find((s) => s.Id === selection) || null,
    Source: (id) => document.Sources.find((s) => s.Id === id) || null,
    Time: () => time,
    Playing: () => playing,
    Probe: () => probe,
    Sources: () => document.Sources,
    CanUndo: () => undoStack.length > 0,
    CanRedo: () => redoStack.length > 0,
    LawOf: (source) => (source ? LawById[source.Law] : null),

    // Prepared frame for the current clock, cached until time or document changes.
    Frame: () => {
      const key = `${revision}:${time.toFixed(4)}`;
      if (frameCache.Key !== key) frameCache = { Key: key, Runtime: PrepareFrame(document.Sources, time, document.Domain) };
      return frameCache.Runtime;
    },
    FrameAt: (t) => PrepareFrame(document.Sources, t, document.Domain),

    // ---------------------------------------------------------------------------------------------- subscription
    Subscribe(listener) {
      listeners.add(listener);
      return () => listeners.delete(listener);
    },

    // ---------------------------------------------------------------------------------------------- authored edits
    Edit(mutate, { Reason = 'document', Coalesce = '', History = true } = {}) {
      if (History) PushHistory(Coalesce);
      const draft = deepClone(document);
      const next = mutate(draft) || draft;
      document = SanitizeDocument(next);
      revision += 1;
      frameCache.Key = '';
      Persist();
      Emit('document');
      if (Reason !== 'document') Emit(Reason);
      return document;
    },

    // Replaces a whole document (import, preset, reset) and clears the selection if it vanished.
    Load(nextDocument, { History = true, Reason = 'document' } = {}) {
      if (History) PushHistory('');
      document = SanitizeDocument(nextDocument);
      if (!document.Sources.some((s) => s.Id === selection)) selection = document.Sources[0]?.Id ?? null;
      revision += 1;
      frameCache.Key = '';
      Persist();
      Emit('document');
      Emit('selection');
      if (Reason !== 'document') Emit(Reason);
    },

    SetOverlay(patch, { Coalesce = '', History = false } = {}) {
      api.Edit((draft) => {
        draft.Overlay = { ...draft.Overlay, ...patch };
        return draft;
      }, { Coalesce, History });
    },

    SetSim(patch, { Coalesce = '', History = false } = {}) {
      api.Edit((draft) => {
        draft.Sim = { ...draft.Sim, ...patch };
        return draft;
      }, { Coalesce, History });
    },

    SetView(view) {
      document.View = { ...document.View, ...view };
      Persist();
      Emit('view');
    },

    // ---------------------------------------------------------------------------------------------- selection
    Select(id) {
      if (selection === id) return;
      selection = id;
      Emit('selection');
      Persist();
    },
    SelectLaw(lawId) {
      const source = document.Sources.find((s) => s.Law === lawId);
      if (source) api.Select(source.Id);
    },

    // ---------------------------------------------------------------------------------------------- source operations
    AddSource(lawId, overrides = {}) {
      const source = NewSource(lawId, overrides);
      api.Edit((draft) => {
        draft.Sources.push(source);
        return draft;
      });
      selection = source.Id;
      Emit('selection');
      Persist();
      return source;
    },
    DuplicateSource(id) {
      const original = api.Source(id);
      if (!original) return null;
      const copy = SanitizeDocument({ Sources: [{ ...deepClone(original), Id: `${original.Id}-copy`, Name: `${original.Name} copy`, Position: original.Position.map((c) => c + 1.2) }] }).Sources[0];
      api.Edit((draft) => {
        const index = draft.Sources.findIndex((s) => s.Id === id);
        draft.Sources.splice(index + 1, 0, copy);
        return draft;
      });
      selection = copy.Id;
      Emit('selection');
      Persist();
      return copy;
    },
    RemoveSource(id) {
      const index = document.Sources.findIndex((s) => s.Id === id);
      if (index < 0) return;
      api.Edit((draft) => {
        draft.Sources.splice(index, 1);
        return draft;
      });
      if (selection === id) {
        selection = document.Sources[Math.min(index, document.Sources.length - 1)]?.Id ?? null;
        Emit('selection');
      }
      Persist();
    },
    MoveSource(id, delta) {
      api.Edit((draft) => {
        const index = draft.Sources.findIndex((s) => s.Id === id);
        const target = index + delta;
        if (index < 0 || target < 0 || target >= draft.Sources.length) return draft;
        const [row] = draft.Sources.splice(index, 1);
        draft.Sources.splice(target, 0, row);
        return draft;
      });
    },
    UpdateSource(id, patch, { Coalesce = '' } = {}) {
      api.Edit((draft) => {
        const source = draft.Sources.find((s) => s.Id === id);
        if (source) Object.assign(source, patch);
        return draft;
      }, { Coalesce: Coalesce || `source:${id}:${Object.keys(patch).join(',')}` });
    },
    UpdateParam(id, key, value) {
      api.Edit((draft) => {
        const source = draft.Sources.find((s) => s.Id === id);
        if (source) source.Params = { ...source.Params, [key]: value };
        return draft;
      }, { Coalesce: `param:${id}:${key}` });
    },
    SetEnabled(id, enabled) {
      api.UpdateSource(id, { Enabled: enabled });
    },

    // ---------------------------------------------------------------------------------------------- transport
    Play() {
      if (playing) return;
      playing = true;
      Emit('sim');
    },
    Pause() {
      if (!playing) return;
      playing = false;
      Emit('sim');
    },
    TogglePlay() {
      playing = !playing;
      Emit('sim');
    },
    SetTime(t) {
      time = Math.max(0, Number(t) || 0);
      Emit('time');
    },
    Advance(dt) {
      time += dt;
      Emit('time');
    },
    ResetTime() {
      time = 0;
      Emit('time');
    },
    Retrigger() {
      document.Sim = { ...document.Sim, ImpulseAt: time };
      Emit('sim');
    },

    SetProbe(point) {
      probe = point.slice();
      Emit('probe');
    },

    // ---------------------------------------------------------------------------------------------- history
    Undo() {
      if (!undoStack.length) return false;
      redoStack.push(Snapshot());
      Restore(undoStack.pop());
      lastCoalesce = { Key: '', At: 0 };
      return true;
    },
    Redo() {
      if (!redoStack.length) return false;
      undoStack.push(Snapshot());
      Restore(redoStack.pop());
      lastCoalesce = { Key: '', At: 0 };
      return true;
    },

    Reset() {
      api.Load(DefaultDocument());
      time = 0;
      Emit('time');
    },

    // ---------------------------------------------------------------------------------------------- persistence
    RestorePersisted() {
      if (typeof localStorage === 'undefined') return false;
      try {
        const raw = localStorage.getItem(storageKey);
        if (!raw) return false;
        const saved = JSON.parse(raw);
        if (!saved || !Array.isArray(saved.Document?.Sources)) return false;
        document = SanitizeDocument(saved.Document);
        selection = saved.Selection && document.Sources.some((s) => s.Id === saved.Selection) ? saved.Selection : document.Sources[0]?.Id ?? null;
        revision += 1;
        frameCache.Key = '';
        Emit('document');
        Emit('selection');
        return true;
      } catch {
        return false;
      }
    },
    ClearPersisted() {
      if (typeof localStorage === 'undefined') return;
      try {
        localStorage.removeItem(storageKey);
      } catch {
        /* ignore */
      }
    },
  };

  return api;
}

export { DEFAULT_OVERLAY, DEFAULT_SIM };

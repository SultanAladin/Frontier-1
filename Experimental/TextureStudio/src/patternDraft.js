import { validatePattern } from "./patternDocument.js";
export const PATTERN_DRAFT_KEY = "alloy-pattern-draft-v2";
export function readPatternDraft(storage) {
  try {
    const raw = (storage || globalThis.localStorage).getItem(PATTERN_DRAFT_KEY);
    if (!raw || raw.length > 12000000) return null;
    const draft = JSON.parse(raw);
    if (draft.schema !== "alloy.pattern.draft.v1") return null;
    return {
      doc: validatePattern(draft.doc),
      savedAt: String(draft.savedAt || ""),
    };
  } catch {
    return null;
  }
}
export function savePatternDraft(doc, storage = globalThis.localStorage) {
  storage.setItem(
    PATTERN_DRAFT_KEY,
    JSON.stringify({
      schema: "alloy.pattern.draft.v1",
      savedAt: new Date().toISOString(),
      doc: validatePattern(doc),
    }),
  );
}

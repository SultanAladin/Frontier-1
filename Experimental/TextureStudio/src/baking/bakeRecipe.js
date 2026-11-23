import { validateMeshBakeSettings } from "./meshBakeCore.js";
import { DEFAULT_MESH_CHANNELS, MESH_MAPS } from "./meshMaps.js";

export const BAKE_RECIPE_SCHEMA = "alloy.baking-recipe.v2";
export const BAKE_PREFS_KEY = "alloy-baking-recipe-v1";
export function normalizeBakeSelection(input = {}) {
  // Empty selection is a valid UI state, but never a valid worker job.
  const empty = Array.isArray(input.channels) && input.channels.length === 0;
  const settings = validateMeshBakeSettings({
    ...input,
    ...(empty ? { channels: ["normal"] } : {}),
  });
  return empty ? { ...settings, channels: [] } : settings;
}
export function defaultBakeRecipe() {
  return {
    schema: BAKE_RECIPE_SCHEMA,
    settings: normalizeBakeSelection({
      resolution: 256,
      channels: DEFAULT_MESH_CHANNELS,
    }),
  };
}
export function parseBakeRecipe(input) {
  if (
    !input ||
    ![BAKE_RECIPE_SCHEMA, "alloy.baking-recipe.v1"].includes(input.schema)
  )
    throw new Error("Choose an Alloy baking recipe.");
  if (!input.settings || typeof input.settings !== "object")
    throw new Error("The recipe is missing bake settings.");
  if (
    input.settings.channels != null &&
    (!Array.isArray(input.settings.channels) ||
      input.settings.channels.some((key) => !Object.hasOwn(MESH_MAPS, key)))
  )
    throw new Error("The recipe contains an unknown mesh map.");
  return {
    schema: BAKE_RECIPE_SCHEMA,
    settings: normalizeBakeSelection(input.settings),
    legacy: input.schema === "alloy.baking-recipe.v1",
  };
}

export const LEGACY_BAKE_BACKUP_KEY = "alloy-baking-legacy-recipe-v1-backup";
export function restoreBakeRecipe(storage) {
  try {
    const raw = storage.getItem(BAKE_PREFS_KEY),
      parsed = parseBakeRecipe(JSON.parse(raw));
    if (parsed.legacy) {
      // Removing the editor must not erase an older user's graph document.
      // Archive it verbatim before the v2 settings-only draft is persisted.
      try {
        if (!storage.getItem(LEGACY_BAKE_BACKUP_KEY))
          storage.setItem(LEGACY_BAKE_BACKUP_KEY, raw);
      } catch {
        // Do not allow an automatic v2 write to erase the only legacy copy.
        return { ...parsed, legacyBackupFailed: true };
      }
    }
    return parsed;
  } catch {
    return defaultBakeRecipe();
  }
}

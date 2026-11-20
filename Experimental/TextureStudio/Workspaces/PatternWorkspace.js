/*==========================================================================================================================================
                                                          PATTERNWORKSPACE.JS
==========================================================================================================================================*/
import { mountArtworkStackWorkspace } from "./ArtworkStackWorkspace.js";

export function mountPatternWorkspace(root, appBridge) {
  return mountArtworkStackWorkspace(root, appBridge, { mode: "pattern" });
}

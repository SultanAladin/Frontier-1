/*==========================================================================================================================================
                                                           STAMPWORKSPACE.JS
==========================================================================================================================================*/
import { mountArtworkStackWorkspace } from "./ArtworkStackWorkspace.js";

export function mountStampWorkspace(root, appBridge) {
  return mountArtworkStackWorkspace(root, appBridge, { mode: "stamp" });
}

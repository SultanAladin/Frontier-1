/*==========================================================================================================================================
                                                            FORCEFIELDS.JS
==========================================================================================================================================*/
// 🧩 Unified Force Field bridge backed 100% by ForceFieldCatalogue.js (the 17 bounded force types + Ambient Wind
//    from ForceFieldEditor & ForceFieldVisualization). Preserves PE.Forces namespace cleanly.

import { PE } from './pe.js';
import { ForceFieldCatalogue } from './ForceFieldCatalogue.js';

export * from './ForceFieldCatalogue.js';

export const Contributions = ForceFieldCatalogue.Contributions;
export const Types = ForceFieldCatalogue.Types;
export const TypeById = ForceFieldCatalogue.TypeById;

PE.Forces = ForceFieldCatalogue;

/*============================================================================================================================================
                                                             VECTORARTWORK.JS
============================================================================================================================================*/
// 🧩 Validated vector artwork shared by stamp persistence, rendering and curve editing.

import { validatePattern } from "./patternDocument.js";
export function NormalizeVectorArtwork(Artwork)
{
    if (!Artwork || !["rect", "ellipse", "diamond", "triangle", "flower", "polygon", "star", "path", "arc", "text"].includes(Artwork.kind)) throw new Error("Choose an editable vector shape or path.");
    const Normalized = validatePattern({ schema: "alloy.pattern.v1", layers: [{ ...Artwork, paint: true }], tileAxes: "none" }).layers[0];
    return { ...Normalized, id: Artwork.id || Normalized.id };
}
export function StampVectorGeometry(Layer)
{
    const { id, name, x, y, width, height, rotation, opacity, visible, locked, mask } = Layer;
    return NormalizeVectorArtwork({ ...Layer.vector, id, name, x, y, width, height, rotation, opacity, visible, locked, mask });
}

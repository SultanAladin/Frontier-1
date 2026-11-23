/*============================================================================================================================================
                                                        STAMPVECTORPROJECTION.JS
============================================================================================================================================*/
// 🧩 Reversible finite-canvas projection; original stamp artwork and PBR targets remain authoritative.

import { validatePattern } from "./patternDocument.js";
import { StampVectorGeometry } from "./VectorArtwork.js";
import { stampId, normalizeStampDocument } from "./stampDocument.js";
import { outlineStampText } from "./stampRaster.js";

export function StampVectorCanvasDocument(Document)
{
    const Artwork = [...Document.layers].reverse();
    const Layers = Artwork.map(Layer =>
    {
        if (Layer.kind === "vector") return StampVectorGeometry(Layer);
        if (Layer.kind === "image") return { ...Layer, kind: "image", src: Layer.image.dataUrl };
        let Svg = Layer.svg;
        if (Layer.kind === "text")
        {
            try
            {
                const Outline = outlineStampText(Layer.runs);
                Svg = `<svg xmlns="http://www.w3.org/2000/svg" width="100" height="100" viewBox="0 0 ${Outline.width} ${Outline.height}" preserveAspectRatio="none">${Outline.parts.map(Part => `<path d="${Part.path}" fill="${Part.color}"/>`).join("")}</svg>`;
            }
            catch
            {
                Svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><rect width="100" height="100" fill="none" stroke="#85857b"/></svg>';
            }
        }
        return { ...Layer, kind: "svg", svg: Svg };
    });
    const Projection = validatePattern({ schema: "alloy.pattern.v1", name: Document.name, tileAxes: "none", backgroundOpacity: 0, layers: Layers });
    Projection.layers = Projection.layers.map((Layer, Index) => ({ ...Layer, id: Artwork[Index].id }));
    return Projection;
}
export function StampDocumentFromVectorCanvas(Projection, Original, SelectedIndices = [])
{
    const Existing = new Map(Original.layers.map(Layer => [Layer.id, Layer]));
    const Identities = new Set();
    const Layers = Projection.layers.map(Layer =>
    {
        let Identity = Layer.id;
        if (!Identity || Identities.has(Identity)) Identity = stampId();
        Identities.add(Identity);
        const { ArraySourceIdentity, ...Geometry } = Layer;
        const Previous = Existing.get(Identity) || Existing.get(ArraySourceIdentity);
        const Transform = Object.fromEntries(["x", "y", "width", "height", "rotation", "opacity", "visible", "locked", "name", "mask"].map(Key => [Key, Layer[Key]]));
        if (Previous && Previous.kind !== "vector") return { ...Previous, ...Transform, id: Identity };
        return { id: Identity, kind: "vector", ...Transform, vector: { ...Geometry, id: Identity } };
    });
    return { Document: normalizeStampDocument({ ...Original, layers: [...Layers].reverse() }), SelectedIDs: SelectedIndices.map(Index => Layers[Index]?.id).filter(Boolean) };
}
export function VectorPaintStyle(Geometry)
{
    return Object.fromEntries(["paint", "fillEnabled", "color", "stroke", "fillSource", "strokeSource", "strokeWidth", "strokeLinecap", "strokeLinejoin", "cornerRadius", "sides", "starPoints", "innerRadius", "arcStart", "arcSweep", "finish", "roughness", "metalness", "relief"].map(Key => [Key, Geometry[Key]]));
}

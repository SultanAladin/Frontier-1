/*============================================================================================================================================
                                                       ARTWORKSELECTIONMASK.JS
============================================================================================================================================*/
// 🧩 Rasterize selected artwork into the receiving artwork's local mask coordinates without deleting its source.

import { patternShape, patternLayerTransform } from "./patternDocument.js";
import { PatternColourDefinitions } from "./PatternColourSources.js";
import { NormalizeArtworkMask } from "./ArtworkMask.js";

export async function ArtworkSelectionMask(Document, TargetIndex, SourceIndex)
{
    const Target = Document.layers[TargetIndex], Source = Document.layers[SourceIndex];
    if (!Target || !Source || TargetIndex === SourceIndex || Target.locked || Source.locked || !Target.visible || !Source.visible) throw new Error("Select two visible, unlocked layers.");
    if (Target.mask) throw new Error("Remove the receiving layer's existing mask before creating a selection mask.");
    const Namespace = "http://www.w3.org/2000/svg";
    const Measurement = document.createElementNS(Namespace, "svg");
    Measurement.style.cssText = "position:fixed;left:-10000px;top:0;width:512px;height:512px;pointer-events:none";
    Measurement.setAttribute("aria-hidden", "true");
    const Geometry = document.createElementNS(Namespace, "g");
    Geometry.innerHTML = patternShape({ ...Target, mask: NormalizeArtworkMask({ category: "paint", base: 1 }) }, Target.color);
    const TargetTransform = document.createElementNS(Namespace, "g"), SourceTransform = document.createElementNS(Namespace, "g");
    TargetTransform.setAttribute("transform", patternLayerTransform(Target));
    SourceTransform.setAttribute("transform", patternLayerTransform(Source));
    Measurement.append(Geometry, TargetTransform, SourceTransform); document.body.append(Measurement);
    let Bounds, Projection;
    try
    {
        Bounds = Geometry.getBBox();
        Projection = TargetTransform.transform.baseVal.consolidate().matrix.inverse().multiply(SourceTransform.transform.baseVal.consolidate().matrix);
        if (!Bounds.width || !Bounds.height) throw new Error("The receiving layer needs nonzero artwork bounds.");
    }
    finally { Measurement.remove(); }
    const Transform = `matrix(${Projection.a} ${Projection.b} ${Projection.c} ${Projection.d} ${Projection.e} ${Projection.f})`;
    const SVG = `<svg xmlns="${Namespace}" width="512" height="512" viewBox="${Bounds.x} ${Bounds.y} ${Bounds.width} ${Bounds.height}" preserveAspectRatio="none"><defs>${PatternColourDefinitions(Source)}</defs><g transform="${Transform}" opacity="${Source.opacity}">${patternShape(Source, Source.color)}</g></svg>`;
    const Image = new window.Image(); Image.src = `data:image/svg+xml;charset=utf-8,${encodeURIComponent(SVG)}`; await Image.decode();
    const Canvas = document.createElement("canvas"); Canvas.width = Canvas.height = 512;
    Canvas.getContext("2d").drawImage(Image, 0, 0);
    const Mask = NormalizeArtworkMask({ category: "image", mode: "alpha", image: Canvas.toDataURL("image/png") });
    return { ...Document, layers: Document.layers.map((Layer, Index) => Index === TargetIndex ? { ...Layer, mask: Mask } : Index === SourceIndex ? { ...Layer, visible: false } : Layer) };
}

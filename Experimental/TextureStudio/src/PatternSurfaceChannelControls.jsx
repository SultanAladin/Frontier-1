/*============================================================================================================================================
                                                   PATTERNSURFACECHANNELCONTROLS.JSX
============================================================================================================================================*/
// 🧩 Multi-selection surface-channel authoring and actual raster map inspection/export.

import React, { useEffect, useState, useRef } from "react";
import { PatternChannelCaptions, PatternChannelEnabled, DerivePatternNormalPixels } from "./PatternSurfaceChannels.js";
import { patternSVG, patternDimensions } from "./patternDocument.js";
import { rasterPatternSVG } from "./patternRuntime.js";
export async function RasterPatternChannel(Document, Caption)
{
    const [Width, Height] = patternDimensions(Document);
    const Mode = { Color: "color", Height: "height", Roughness: "roughness", Metalness: "metalness", Opacity: "opacity", Occlusion: "occlusion" }[Caption];
    if (Caption !== "Normal") return rasterPatternSVG(patternSVG(Document, Mode), Width, Height);
    const NormalDocument = { ...Document, layers: Document.layers.map(Layer => ({ ...Layer, visible: Layer.visible && PatternChannelEnabled(Layer, "Normal"), SurfaceChannels: { ...Layer.SurfaceChannels } })) };
    const Field = await rasterPatternSVG(patternSVG(NormalDocument, "normalHeight"), Width, Height), Context = Field.getContext("2d");
    const Normals = DerivePatternNormalPixels(Context.getImageData(0, 0, Width, Height).data, Width, Height, 4, !["y", "none"].includes(Document.tileAxes), !["x", "none"].includes(Document.tileAxes));
    Context.putImageData(new ImageData(Normals, Width, Height), 0, 0);
    const Supplied = await rasterPatternSVG(patternSVG(Document, "normal"), Width, Height); Context.drawImage(Supplied, 0, 0);
    const Encoded = Context.getImageData(0, 0, Width, Height);
    for (let Offset = 0; Offset < Encoded.data.length; Offset += 4)
    {
        const X = Encoded.data[Offset] / 127.5 - 1, Y = Encoded.data[Offset + 1] / 127.5 - 1, Z = Encoded.data[Offset + 2] / 127.5 - 1, Length = Math.hypot(X, Y, Z) || 1;
        Encoded.data[Offset] = (X / Length * .5 + .5) * 255; Encoded.data[Offset + 1] = (Y / Length * .5 + .5) * 255; Encoded.data[Offset + 2] = (Z / Length * .5 + .5) * 255;
    }
    Context.putImageData(Encoded, 0, 0);
    return Field;
}
export default function PatternSurfaceChannelControls({ Document, Selection, OnFinalize, OnError })
{
    const CurrentDocument = useRef(Document); CurrentDocument.current = Document;
    const [Additional, ConfigureAdditional] = useState("Occlusion"), [Inspect, ConfigureInspect] = useState(""), [Preview, ConfigurePreview] = useState("");
    const Layers = Selection.map(Index => Document.layers[Index]).filter(Boolean), Editable = Layers.filter(Layer => !Layer.locked);
    const Captions = ["Color", "Height", "Normal", "Roughness", "Metalness", "Opacity", ...(Layers.some(Layer => Layer.SurfaceChannels?.Occlusion) ? ["Occlusion"] : [])];
    useEffect(() =>
    {
        let Cancelled = false; ConfigurePreview("");
        if (Inspect) RasterPatternChannel(Document, Inspect).then(Canvas => { if (!Cancelled) ConfigurePreview(Canvas.toDataURL("image/png")); }).catch(Failure => { if (!Cancelled) OnError(Failure.message); });
        return () => { Cancelled = true; };
    }, [Document, Inspect]);
    function Change(Caption, Patch, Property)
    {
        OnFinalize({ ...Document, layers: Document.layers.map((Layer, Index) => Selection.includes(Index) && !Layer.locked ? { ...Layer, ...(Property || {}), SurfaceChannels: { ...Layer.SurfaceChannels, [Caption]: { Enabled: PatternChannelEnabled(Layer, Caption), Strength: 1, ...Layer.SurfaceChannels?.[Caption], ...Patch } } } : Layer) });
    }
    if (!Layers.length) return null;
    return <details className="pattern-surface-channels"><summary>Surface channels · {Layers.length} selected</summary>
        <p>Height-derived shading works on any motif, not only textiles. Height changes shading, not silhouette displacement.</p>
        <fieldset disabled={!Editable.length}>
            {Captions.map(Caption =>
            {
                const First = Editable[0] || Layers[0], AllEnabled = Editable.every(Layer => PatternChannelEnabled(Layer, Caption));
                const MixedEnabled = Editable.some(Layer => PatternChannelEnabled(Layer, Caption)) && !AllEnabled;
                const Property = { Height: "relief", Roughness: "roughness", Metalness: "metalness", Opacity: "opacity" }[Caption];
                const Scalar = Layer => Property ? Layer[Property] : Layer.SurfaceChannels?.[Caption]?.Strength ?? 1;
                const Mixed = Editable.some(Layer => Scalar(Layer) !== Scalar(First));
                return <section className="pattern-channel-row" key={Caption}>
                    <label className="pattern-channel-toggle"><input type="checkbox" role="switch" aria-label={`${PatternChannelCaptions[Caption]} channel enabled`} aria-description={MixedEnabled ? "Mixed selection; enable or disable this channel for all selected unlocked artwork." : undefined} checked={AllEnabled} onChange={Event => Change(Caption, { Enabled: Event.target.checked })}/>{PatternChannelCaptions[Caption]}{MixedEnabled && <small>Mixed</small>}</label>
                    {Caption !== "Color" && <label>{Caption === "Height" ? "Height (mm)" : Caption === "Normal" ? "Normal strength" : "Amount"}<input aria-label={`Channel ${Caption} amount`} type="number" step=".01" min={Caption === "Height" ? -1 : 0} max={Caption === "Normal" ? 4 : 1} value={Mixed ? "" : Scalar(First)} placeholder="Mixed" onChange={Event => { if (Event.target.value === "") return; const Quantity = Number(Event.target.value); if (Number.isFinite(Quantity)) Change(Caption, Property ? {} : { Strength: Quantity }, Property ? { [Property]: Quantity } : undefined); }}/></label>}
                    {Caption === "Normal" && <><label>Normal map<input aria-label="Supply pattern normal map" type="file" accept="image/png,image/jpeg,image/webp" onChange={async Event => { const File = Event.target.files?.[0]; if (!File) return; try { if (File.size > 1500000) throw new Error("Normal maps must be under 1.5 MB."); const Source = await new Promise((Resolve, Reject) => { const Reader = new FileReader(); Reader.onload = () => Resolve(Reader.result); Reader.onerror = Reject; Reader.readAsDataURL(File); }); const ImageMap = new Image(); ImageMap.src = Source; await ImageMap.decode(); if (CurrentDocument.current !== Document) throw new Error("The document changed while reading the map. Select the intended artwork and choose the map again."); Change("Normal", { Source }); } catch (Failure) { OnError(Failure.message || "Cannot decode this normal map."); } Event.target.value = ""; }}/></label><button onClick={() => Change("Normal", { Source: undefined })}>Use height-derived normals</button><small>{Editable.some(Layer => Layer.SurfaceChannels?.Normal?.Source) ? "Supplied tangent-space normal map; height detail remains additive." : "Normals generated from height. Strength changes the height-derived shading."}</small></>}
                </section>;
            })}
            <div className="pattern-organization-actions"><select aria-label="Additional surface channel" value={Additional} onChange={Event => ConfigureAdditional(Event.target.value)}><option value="Occlusion">Ambient occlusion</option></select><button onClick={() => Change(Additional, { Enabled: true, Strength: 1 })}>+ Channel</button></div>
        </fieldset>
        <label>Inspect map<select aria-label="Inspect pattern channel" value={Inspect} onChange={Event => ConfigureInspect(Event.target.value)}><option value="">Choose channel…</option>{Object.entries(PatternChannelCaptions).map(([Caption, Name]) => <option key={Caption} value={Caption}>{Name}</option>)}</select></label>
        {Inspect && <><small>Actual document map · tangent normals use OpenGL +Y.</small>{Preview ? <><img className="pattern-channel-preview" src={Preview} alt={`${PatternChannelCaptions[Inspect]} channel map`}/><a className="pattern-channel-download" href={Preview} download={`Pattern-${Inspect}.png`}>Download {PatternChannelCaptions[Inspect]} PNG</a></> : <small>Rasterizing channel…</small>}</>}
    </details>;
}

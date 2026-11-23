/*============================================================================================================================================
                                                         PATTERNCOLOURCONTROLS.JSX                                                          
============================================================================================================================================*/
// 🧩 Pattern paint-source editing and related inspector property cards.

import React, { useEffect, useRef, useState } from "react";
import { Plus, Trash2, Upload, ChevronDown } from "lucide-react";
import { NormalizePatternColourSource } from "./PatternColourSources.js";

export function PatternPropertyCard({ Caption, children })
{
  return <details className="pattern-property-card" open><summary>{Caption}<ChevronDown size={14}/></summary><div className="pattern-property-body">{children}</div></details>;
}
export default function PatternColourControls({ Caption, ColourLabel, Source, Solid, OnChange })
{
  const [Error, SetError] = useState("");
  const [Pending, SetPending] = useState(false);
  const UploadSequence = useRef(0);
  const FileInput = useRef(null);
  const RetainedGradient = useRef(null);
  const RetainedImage = useRef(null);
  useEffect(() => () => { UploadSequence.current++; }, []);
  const Category = Source?.category || "solid";
  if (Category === "linear" || Category === "radial") RetainedGradient.current = Source;
  if (Category === "image") RetainedImage.current = Source;
  function ChangeCategory(Next)
  {
    UploadSequence.current++;
    SetPending(false);
    SetError("");
    if (Next === "solid") return OnChange(undefined, Solid);
    if (Next === "image")
    {
      if (RetainedImage.current) OnChange(RetainedImage.current, Solid);
      else FileInput.current?.click();
      return;
    }
    OnChange({ ...(RetainedGradient.current || { angle: 0, stops: [{ color: Solid, offset: 0 }, { color: "#f5e8cb", offset: 1 }] }), category: Next }, Solid);
  }
  async function ImportImage(Event)
  {
    const File = Event.target.files?.[0];
    Event.target.value = "";
    if (!File) return;
    const Sequence = ++UploadSequence.current;
    SetError("");
    SetPending(true);
    let ImageURL;
    try
    {
      if (!['image/png', 'image/jpeg', 'image/webp'].includes(File.type) || File.size > 4 * 1024 * 1024) throw new Error("Choose a PNG, JPEG or WebP image up to 4 MB.");
      ImageURL = URL.createObjectURL(File);
      const ImageElement = new Image();
      ImageElement.src = ImageURL;
      await ImageElement.decode();
      if (ImageElement.naturalWidth * ImageElement.naturalHeight > 16777216) throw new Error("Choose an image with at most 16 megapixels.");
      // Preserve the original authoring image; derived/runtime resolution is a separate decision.
      const Encoded = await new Promise((Resolve, Reject) =>
      {
        const Reader = new FileReader();
        Reader.onload = () => Resolve(Reader.result);
        Reader.onerror = () => Reject(new Error("The source image could not be read."));
        Reader.readAsDataURL(File);
      });
      const Next = NormalizePatternColourSource({ category: "image", image: Encoded, fit: Source?.fit || "cover" });
      if (!Next) throw new Error("The source image exceeds the embedded authoring limit.");
      if (Sequence === UploadSequence.current) OnChange(Next, Solid);
    }
    catch (Failure)
    {
      if (Sequence === UploadSequence.current) SetError(Failure.message || "The image could not be decoded.");
    }
    finally
    {
      if (ImageURL) URL.revokeObjectURL(ImageURL);
      if (Sequence === UploadSequence.current) SetPending(false);
    }
  }
  function ChangeStop(Index, Patch)
  {
    OnChange({ ...Source, stops: Source.stops.map((Stop, StopIndex) => StopIndex === Index ? { ...Stop, ...Patch } : Stop) }, Solid);
  }
  return <div className="pattern-colour-source">
    <label>{Caption}<select aria-label={`${Caption} source`} value={Category} onChange={Event => ChangeCategory(Event.target.value)}>
      <option value="solid">Solid</option><option value="linear">Linear gradient</option><option value="radial">Radial gradient</option><option value="image">Image…</option>
    </select></label>
    <input ref={FileInput} type="file" hidden accept="image/png,image/jpeg,image/webp" aria-label={`${Caption} image file`} onChange={ImportImage}/>
    {Category === "solid" && <label className="pe-color">Colour<input aria-label={ColourLabel} type="color" value={Solid} onChange={Event => OnChange(undefined, Event.target.value)}/></label>}
    {(Category === "linear" || Category === "radial") && <>
      <div className="pattern-source-swatch" style={{ background: `${Category === "linear" ? `linear-gradient(${Source.angle + 90}deg,` : 'radial-gradient(circle,'}${Source.stops.map(Stop => `${Stop.color} ${Stop.offset * 100}%`).join(',')})` }}/>
      {Category === "linear" && <label>Angle<input aria-label={`${Caption} gradient angle`} type="number" min="-180" max="180" value={Source.angle} onChange={Event => OnChange({ ...Source, angle: +Event.target.value }, Solid)}/></label>}
      <div className="pattern-gradient-stops">{Source.stops.map((Stop, Index) => <div className="pattern-gradient-stop" key={Index}>
        <input type="color" aria-label={`${Caption} stop ${Index + 1} colour`} value={Stop.color} onChange={Event => ChangeStop(Index, { color: Event.target.value })}/>
        <label><input type="number" aria-label={`${Caption} stop ${Index + 1} position`} min="0" max="100" value={Math.round(Stop.offset * 100)} onChange={Event => ChangeStop(Index, { offset: +Event.target.value / 100 })}/>%</label>
        <button type="button" disabled={Source.stops.length <= 2} aria-label={`Remove ${Caption.toLowerCase()} stop ${Index + 1}`} onClick={() => OnChange({ ...Source, stops: Source.stops.filter((_, StopIndex) => StopIndex !== Index) }, Solid)}><Trash2 size={12}/></button>
      </div>)}</div>
      <button type="button" disabled={Source.stops.length >= 12} onClick={() => OnChange({ ...Source, stops: [...Source.stops, { color: Solid, offset: .5 }] }, Solid)}><Plus size={13}/>Add colour stop</button>
    </>}
    {Category === "image" && <><img className="pattern-source-image" src={Source.image} alt={`${Caption} source`}/><label>Image fit<select aria-label={`${Caption} image fit`} value={Source.fit} onChange={Event => OnChange({ ...Source, fit: Event.target.value }, Solid)}><option value="cover">Cover</option><option value="contain">Contain</option><option value="stretch">Stretch</option></select></label></>}
    <button type="button" onClick={() => FileInput.current?.click()} disabled={Pending} title="Use an embedded PNG, JPEG or WebP image. The original source is embedded without resizing or flattening."><Upload size={13}/>{Pending ? "Reading image…" : Category === "image" ? "Replace image" : "Use image…"}</button>
    {Error && <p role="alert" className="pattern-source-error">{Error}</p>}
  </div>;
}

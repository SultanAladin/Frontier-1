/*============================================================================================================================================
                                                         ARTWORKMASKCONTROLS.JSX
============================================================================================================================================*/
// 🧩 Shared local mask inspector with original embedded image preservation and reversible coverage edits.

import React, { useRef } from "react";
import { NormalizeArtworkMask } from "./ArtworkMask.js";
import { PatternPropertyCard } from "./PatternColourControls.jsx";
export default function ArtworkMaskControls({ Mask, OnChange, OnError, OnEdit, Editing = true })
{
    const FileInput = useRef(null);
    function Change(Patch) { OnChange(NormalizeArtworkMask({ ...Mask, ...Patch })); }
    async function Import(Event)
    {
        const File = Event.target.files[0]; Event.target.value = "";
        if (!File) return;
        try
        {
            if (!["image/png", "image/jpeg", "image/webp"].includes(File.type) || File.size > 4 * 1024 * 1024) throw new Error("Choose a PNG, JPEG or WebP mask up to 4 MB; Stamp's 600 KB document limit still applies.");
            const Image = await createImageBitmap(File);
            const Pixels = Image.width * Image.height; Image.close();
            if (Pixels > 16777216) throw new Error("Mask images are limited to 16 megapixels.");
            const Encoded = await new Promise((Resolve, Reject) => { const Reader = new FileReader(); Reader.onload = () => Resolve(Reader.result); Reader.onerror = () => Reject(new Error("Mask image could not be read.")); Reader.readAsDataURL(File); });
            Change({ category: "image", image: Encoded });
        }
        catch (Failure) { OnError(Failure.message); }
    }
    return <div className="artwork-mask-properties"><PatternPropertyCard Caption="Mask">
        {!Mask ? <button type="button" onClick={() => { OnChange(NormalizeArtworkMask({ category: "paint", base: 1 })); OnEdit?.(); }}>Add mask</button> : !Editing ? <button type="button" onClick={OnEdit}>Edit layer mask</button> : <>
            <label>Mask source<select aria-label="Artwork mask source" value={Mask.category} onChange={Event => Event.target.value === "image" ? FileInput.current.click() : Change({ category: Event.target.value })}><option value="paint">Filled / painted</option><option value="ellipse">Ellipse</option><option value="rect">Rectangle</option><option value="image">Image…</option></select></label>
            <button type="button" onClick={() => FileInput.current.click()}>Import mask image</button>
            {Mask.category === "image" && <label>Coverage<select aria-label="Artwork mask coverage" value={Mask.mode} onChange={Event => Change({ mode: Event.target.value })}><option value="alpha">Alpha</option><option value="luminance">Luminance</option></select></label>}
            <button type="button" aria-label="Enable artwork mask" aria-pressed={Mask.enabled} onClick={() => Change({ enabled: !Mask.enabled })}>{Mask.enabled ? "Mask enabled" : "Mask disabled"}</button>
            <button type="button" aria-label="Invert artwork mask" aria-pressed={Mask.invert} onClick={() => Change({ invert: !Mask.invert })}>Invert mask</button>
            {(Mask.category === "paint" ? [["opacity", "Opacity", 0, 1]] : [["x", "Centre X", -100, 200], ["y", "Centre Y", -100, 200], ["width", "Width", 1, 400], ["height", "Height", 1, 400], ["opacity", "Opacity", 0, 1]]).map(([Key, Caption, Minimum, Maximum]) => <label key={Key}>{Caption}<input type="number" aria-label={`Artwork mask ${Key}`} value={Mask[Key]} min={Minimum} max={Maximum} step={Key === "opacity" ? .05 : 1} onChange={Event => Change({ [Key]: +Event.target.value })}/></label>)}
            <button type="button" onClick={() => OnChange(undefined)}>Remove mask</button>
        </>}
        <input ref={FileInput} type="file" hidden accept="image/png,image/jpeg,image/webp" aria-label="Artwork mask image" onChange={Import}/>
    </PatternPropertyCard></div>;
}

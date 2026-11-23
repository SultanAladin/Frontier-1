/*============================================================================================================================================
                                                        ARTWORKARRAYCONTROLS.JSX
============================================================================================================================================*/
// 🧩 Shared Inspector controls for editable array layouts and deterministic scale variation.

import React, { useState } from "react";
import { GenerateArtworkArray } from "./ArtworkArrays.js";
export default function ArtworkArrayControls({ Document, Selection, OnFinalize, OnError, Limit = 64 })
{
    const [Specification, ConfigureSpecification] = useState({ Category: "linear", Count: 4, Columns: 3, Rows: 2, StepX: 80, StepY: 0, Radius: 120, Rings: 2, RadiusStep: 60, CentreX: 256, CentreY: 256, StartAngle: 0, Sweep: 360, RotateCopies: false, ScaleProfile: "uniform", StartScale: 100, EndScale: 50, Seed: 1 });
    if (!Selection.length) return null;
    const Circular = ["circular", "radial"].includes(Specification.Category);
    const Population = Specification.Category === "grid" ? Specification.Columns * Specification.Rows : Specification.Count * (Specification.Category === "radial" ? Specification.Rings : 1);
    const Total = Document.layers.length - Selection.length + Population * Selection.length;
    const Protected = Selection.some(Index => Document.layers[Index]?.locked || !Document.layers[Index]?.visible);
    function Numeric(Caption, Key, Minimum, Maximum, Step = 1)
    {
        return <label key={Key}>{Caption}<input aria-label={`Array ${Caption.toLowerCase()}`} type="number" min={Minimum} max={Maximum} step={Step} value={Specification[Key]} onChange={Event => ConfigureSpecification(Previous => ({ ...Previous, [Key]: Event.target.value === "" ? "" : Number(Event.target.value) }))}/></label>;
    }
    return <details className="artwork-array-controls"><summary>Array / pattern</summary><fieldset disabled={Protected}>
        <label>Layout<select aria-label="Array layout" value={Specification.Category} onChange={Event => ConfigureSpecification(Previous => ({ ...Previous, Category: Event.target.value, ...(Event.target.value === "grid" && Previous.StepY === 0 ? { StepY: 80 } : {}) }))}><option value="linear">Linear</option><option value="grid">Grid</option><option value="circular">Circular / arc</option><option value="radial">Radial rings</option></select></label>
        <div className="artwork-array-numbers">
            {Specification.Category === "grid" ? <>{Numeric("Columns", "Columns", 1, 8)}{Numeric("Rows", "Rows", 1, 8)}</> : Numeric(Circular ? "Copies per ring" : "Copies", "Count", 2, 64)}
            {Circular ? <>{Numeric("Radius", "Radius", 0, 512)}{Specification.Category === "radial" && <>{Numeric("Rings", "Rings", 1, 8)}{Numeric("Ring spacing", "RadiusStep", 1, 512)}</>}{Numeric("Centre X", "CentreX", -512, 1024)}{Numeric("Centre Y", "CentreY", -512, 1024)}{Numeric("Start angle", "StartAngle", -360, 360)}{Numeric("Arc sweep", "Sweep", -360, 360)}</> : <>{Numeric("Step X", "StepX", -512, 512)}{Numeric("Step Y", "StepY", -512, 512)}</>}
        </div>
        {Circular && <label className="artwork-array-toggle"><input type="checkbox" role="switch" aria-label="Rotate array copies" checked={Specification.RotateCopies} onChange={Event => ConfigureSpecification(Previous => ({ ...Previous, RotateCopies: Event.target.checked }))}/>Rotate copies around centre</label>}
        <label>Scale profile<select aria-label="Array scale profile" value={Specification.ScaleProfile} onChange={Event => ConfigureSpecification(Previous => ({ ...Previous, ScaleProfile: Event.target.value }))}><option value="uniform">Uniform</option><option value="linear">Linear progression</option><option value="circular">Circular wave</option><option value="radial">Radial distance</option><option value="noise">Seeded noise</option></select></label>
        <div className="artwork-array-numbers">{Numeric("Start scale %", "StartScale", 1, 400)}{Specification.ScaleProfile !== "uniform" && Numeric("End scale %", "EndScale", 1, 400)}{Specification.ScaleProfile === "noise" && Numeric("Noise seed", "Seed", 0, 999999)}</div>
        <small>Replaces the selection with editable copies, not a live modifier. Undo restores originals. Group spacing scales together; native SVG/text retain their original artwork.</small>
        <small>{Total} / {Limit} document entries after generation{Specification.ScaleProfile === "radial" ? " · scale follows distance from the array centre" : ""}</small>
        <button type="button" disabled={Total > Limit || Population < 2} onClick={() => { try { const Result = GenerateArtworkArray(Document, Selection, Specification, Limit); OnFinalize(Result.Document, Result.Selection); } catch (Failure) { OnError(Failure.message); } }}>Generate array</button>
    </fieldset></details>;
}

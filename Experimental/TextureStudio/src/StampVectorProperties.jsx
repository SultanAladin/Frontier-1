/*============================================================================================================================================
                                                        STAMPVECTORPROPERTIES.JSX
============================================================================================================================================*/
// 🧩 Shared paint sources and editable geometry properties without changing stamp PBR channel targets.

import React from "react";
import PatternColourControls, { PatternPropertyCard } from "./PatternColourControls.jsx";
export default function StampVectorProperties({ Geometry, OnChange, Disabled, Drawing })
{
    function Numeric(Caption, Key, Minimum, Maximum, Step = 1)
    {
        return <label key={Key}>{Caption}<input aria-label={`Stamp vector ${Caption.toLowerCase()}`} type="number" min={Minimum} max={Maximum} step={Step} value={Geometry[Key] ?? Minimum} onChange={Event => OnChange({ [Key]: +Event.target.value })}/></label>;
    }
    return <fieldset className="stamp-vector-properties" disabled={Disabled}>
        <h2>{Drawing ? "Vector drawing defaults" : "Vector artwork"}</h2>
        <PatternPropertyCard Caption="Fill">
            <PatternColourControls Caption="Stamp fill" ColourLabel="Stamp vector fill colour" Source={Geometry.fillSource} Solid={Geometry.color} OnChange={(Source, Colour) => OnChange({ fillSource: Source, color: Colour })}/>
            <label className="stamp-vector-toggle"><input type="checkbox" role="switch" aria-label="Stamp vector fill enabled" checked={Geometry.fillEnabled !== false} onChange={Event => OnChange({ fillEnabled: Event.target.checked })}/>Fill enabled</label>
        </PatternPropertyCard>
        <PatternPropertyCard Caption="Stroke">
            <PatternColourControls Caption="Stamp stroke" ColourLabel="Stamp vector stroke colour" Source={Geometry.strokeSource} Solid={Geometry.stroke} OnChange={(Source, Colour) => OnChange({ strokeSource: Source, stroke: Colour, strokeWidth: Geometry.strokeWidth || 1 })}/>
            {Numeric("Stroke width", "strokeWidth", 0, 25, .1)}
            <label>Line cap<select aria-label="Stamp vector line cap" value={Geometry.strokeLinecap || "round"} onChange={Event => OnChange({ strokeLinecap: Event.target.value })}>{["butt", "round", "square"].map(Caption => <option key={Caption}>{Caption}</option>)}</select></label>
            <label>Line join<select aria-label="Stamp vector line join" value={Geometry.strokeLinejoin || "round"} onChange={Event => OnChange({ strokeLinejoin: Event.target.value })}>{["miter", "round", "bevel"].map(Caption => <option key={Caption}>{Caption}</option>)}</select></label>
        </PatternPropertyCard>
        {!["path", "text"].includes(Geometry.kind) && <PatternPropertyCard Caption="Shape geometry">
            {(!Geometry.kind || Geometry.kind === "rect") && Numeric("Corner radius", "cornerRadius", 0, 50)}
            {(!Geometry.kind || Geometry.kind === "polygon") && Numeric("Sides", "sides", 3, 32)}
            {(!Geometry.kind || Geometry.kind === "star") && <>{Numeric("Star points", "starPoints", 3, 32)}{Numeric("Inner radius", "innerRadius", .05, .95, .05)}</>}
            {Geometry.kind === "arc" && <>{Numeric("Arc start", "arcStart", -360, 360)}{Numeric("Arc sweep", "arcSweep", -360, 360)}</>}
        </PatternPropertyCard>}
    </fieldset>;
}

/*============================================================================================================================================
                                                         VECTORBOOLEANACTIONS.JSX
============================================================================================================================================*/
// 🧩 Shared, discoverable filled-vector boolean commands for authoring canvases.

import React from "react";
import { Combine } from "lucide-react";
import "./vectorBooleanActions.css";
export default function VectorBooleanActions({ Enabled, Count, OnAction })
{
    const Descriptions = {
        union: "Join selected closed, filled vectors into one editable path.",
        difference: "Subtract upper shapes from the bottom shape. The bottom shape supplies the result's appearance.",
        intersection: "Keep only the overlapping filled area of the selected vectors.",
        exclude: "Keep non-overlapping filled areas; overlapping regions become holes (XOR).",
    };
    return <div className="vector-boolean-actions" role="group" aria-label="Boolean operations">
        <strong><Combine size={14}/>Booleans</strong>
        {[["union", "Union"], ["difference", "Difference"], ["intersection", "Intersection"], ["exclude", "Exclude"]].map(([Operation, Caption]) =>
            <button key={Operation} type="button" aria-label={Caption} disabled={!Enabled} data-tooltip-title={Caption} title={Descriptions[Operation]} data-tooltip-body={`${Descriptions[Operation]} Select 2–16 unlocked, visible, closed, filled vectors. Strokes are not expanded.`} onClick={() => OnAction(`boolean-${Operation}`)}><svg width="20" height="20" viewBox="0 0 24 24" fill="none" aria-hidden="true"><path d="M3 3H15V15H3Z M9 9H21V21H9Z" stroke="currentColor" strokeWidth="1.5"/>{Operation === "union" && <path d="M3 3H15V9H21V21H9V15H3Z" fill="currentColor" fillOpacity=".45"/>}{Operation === "difference" && <path d="M3 3H15V9H9V15H3Z" fill="currentColor" fillOpacity=".65"/>}{Operation === "intersection" && <path d="M9 9H15V15H9Z" fill="currentColor"/>}{Operation === "exclude" && <path d="M3 3H15V9H21V21H9V15H3Z M9 9V15H15V9Z" fill="currentColor" fillRule="evenodd" fillOpacity=".65"/>}</svg></button>)}
        <small aria-live="polite">{Enabled ? `${Count} vectors selected · bottom shape first` : "Select 2–16 closed, filled vectors"}</small>
    </div>;
}

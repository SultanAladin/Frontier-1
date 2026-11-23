/*============================================================================================================================================
                                                        ARTWORKMASKSTACKROW.JSX
============================================================================================================================================*/
// 🧩 Select an owned mask beneath its artwork without inventing a separate layer identity or changing compositing order.

import React from "react";
import { Eye, EyeOff } from "lucide-react";
import { ArtworkMaskPreviewSVG } from "./ArtworkMask.js";
export default function ArtworkMaskStackRow({ Mask, Name, Selected, Locked, OnSelect, OnChange })
{
    if (!Mask) return null;
    return <div className={`artwork-mask-row ${Selected ? "selected" : ""}`} role="group" aria-label={`Mask of ${Name}`}>
        <button aria-label={`Select mask of ${Name}`} aria-pressed={Selected} onClick={OnSelect}>
            <img src={`data:image/svg+xml;charset=utf-8,${encodeURIComponent(ArtworkMaskPreviewSVG(Mask))}`} alt=""/>
            <span><strong title={`Mask of ${Name}`}>Mask · {Name}</strong><small>{Mask.enabled ? Mask.invert ? "Inverted" : "Coverage" : "Disabled"} · {Math.round(Mask.opacity * 100)}%{Locked ? " · locked" : ""}</small></span>
        </button>
        <button disabled={Locked} aria-label={`${Mask.enabled ? "Disable" : "Enable"} mask of ${Name}`} onClick={() => OnChange({ ...Mask, enabled: !Mask.enabled })}>{Mask.enabled ? <Eye size={13}/> : <EyeOff size={13}/>}</button>
    </div>;
}

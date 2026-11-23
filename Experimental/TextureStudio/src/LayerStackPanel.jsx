/*==========================================================================================================================================
                                                        LAYERSTACKPANEL.JSX
==========================================================================================================================================*/
// 🧩 Independent layer-panel boundary and tab presentation shared by pattern and texture authoring.

import React from "react";
import { Layers3, Check, EyeOff, LockKeyhole, Search, X } from "lucide-react";
import { StudioDockTab } from "./StudioUI.jsx";

export default function LayerStackPanel({ Caption = "Layers", RegionLabel = Caption, Detail, className = "", children })
{
    return (
        <aside className={`layer-stack-panel ${className}`} aria-label={RegionLabel}>
            <StudioDockTab icon={Layers3} detail={Detail}>{Caption}</StudioDockTab>
            {children}
        </aside>
    );
}

// 🧩 Shared filter presentation; document identity and mutation stay with each workspace.
export function LayerStackFilters({ Query, OnQueryChange, Filter, OnFilterChange, VisibleCount, HiddenCount, LockedCount })
{
    const Counts = [["visible", "Visible", VisibleCount, Check], ["hidden", "Hidden", HiddenCount, EyeOff], ...(LockedCount === undefined ? [] : [["locked", "Locked", LockedCount, LockKeyhole]])];
    return <>
        <div className="tp-layer-stats">
            {Counts.map(([Value, Caption, Count, Icon]) => <button key={Value} aria-label={`${Caption} layers`} aria-pressed={Filter === Value} onClick={() => OnFilterChange(Filter === Value ? "all" : Value)}>
                <span className={`tp-status-disc ${Value !== "visible" ? "muted" : ""}`}><Icon size={12}/></span><small>{Caption}</small><strong>{String(Count).padStart(2, "0")}</strong>
            </button>)}
        </div>
        <div className="tp-layer-search"><label><Search size={14}/><input aria-label="Find a layer" placeholder="Find a layer…" value={Query} onChange={Event => OnQueryChange(Event.target.value)}/>{Query && <button aria-label="Clear layer search" onClick={() => OnQueryChange("")}><X size={12}/></button>}</label></div>
        {Filter !== "all" && <div className="tp-filter-note"><span>{Filter[0].toUpperCase() + Filter.slice(1)} layers</span><button onClick={() => OnFilterChange("all")}>Clear filter<X size={11}/></button></div>}
    </>;
}

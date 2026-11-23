/*============================================================================================================================================
                                                         ARTWORKMASKCANVAS.JSX
============================================================================================================================================*/
// 🧩 Local mask painting and filled vector regions with one reversible document edit per gesture, independent of artwork transforms.

import React, { useEffect, useLayoutEffect, useRef, useState } from "react";
import { Brush, Eraser, Square, Circle, Triangle, Diamond, Pentagon, Star, Minus, PenTool, Blend, MoreHorizontal, Check, Undo2, Redo2 } from "lucide-react";
import { NormalizeArtworkMask, ArtworkMaskPreviewSVG } from "./ArtworkMask.js";

export default function ArtworkMaskCanvas({ Mask, Name, Disabled, OnChange, OnClose, OnHistory, OnError, OnSave })
{
    const [Tool, ConfigureTool] = useState("brush"), [Ink, ConfigureInk] = useState(0), [Width, ConfigureWidth] = useState(.06), [Opacity, ConfigureOpacity] = useState(1), [Softness, ConfigureSoftness] = useState(0), [Filled, ConfigureFilled] = useState(true);
    const [Draft, ConfigureDraft] = useState(null), [PenPoints, ConfigurePenPoints] = useState([]);
    const Gesture = useRef(null), Surface = useRef(null), SurfaceBounds = useRef(null);
    const [SurfaceEdge, ConfigureSurfaceEdge] = useState(240);
    useLayoutEffect(() =>
    {
        const BoundsObservation = new ResizeObserver(([Entry]) => ConfigureSurfaceEdge(Math.max(1, Math.min(512, Entry.contentRect.width, Entry.contentRect.height))));
        BoundsObservation.observe(SurfaceBounds.current); return () => BoundsObservation.disconnect();
    }, []);
    const Tools = [["brush", "Paint mask", Brush], ["erase", "Erase mask", Eraser], ["rect", "Rectangle mask", Square], ["ellipse", "Ellipse mask", Circle], ["triangle", "Triangle mask", Triangle], ["diamond", "Diamond mask", Diamond], ["polygon", "Polygon mask", Pentagon], ["star", "Star mask", Star], ["line", "Line mask", Minus], ["pen", "Pen mask", PenTool], ["gradient", "Gradient mask", Blend]];
    useEffect(() => { Gesture.current = null; ConfigureDraft(null); ConfigurePenPoints([]); }, [Mask]);
    function Finalize(Operation)
    {
        try
        {
            const Next = NormalizeArtworkMask({ ...Mask, paint: [...(Mask.paint || []), Operation] });
            OnChange(Next);
        }
        catch (Failure) { OnError(Failure.message); }
        ConfigureDraft(null); ConfigurePenPoints([]);
    }
    function Position(Event)
    {
        const Bounds = Surface.current.getBoundingClientRect();
        return [Math.max(0, Math.min(1, (Event.clientX - Bounds.left) / Bounds.width)), Math.max(0, Math.min(1, (Event.clientY - Bounds.top) / Bounds.height))];
    }
    function Operation(Points)
    {
        let Category = Tool === "brush" || Tool === "erase" || Tool === "line" ? "stroke" : Tool === "pen" || ["triangle", "diamond", "polygon", "star"].includes(Tool) ? "path" : Tool;
        if (["triangle", "diamond", "polygon", "star"].includes(Tool))
        {
            const [Start, End] = [Points[0], Points.at(-1)], Centre = [(Start[0] + End[0]) / 2, (Start[1] + End[1]) / 2];
            const Count = Tool === "triangle" ? 3 : Tool === "diamond" ? 4 : Tool === "star" ? 10 : 6;
            Points = Array.from({ length: Count }, (_, Index) =>
            {
                const Angle = -Math.PI / 2 + Index * Math.PI * 2 / Count, Radius = Tool === "star" && Index % 2 ? .45 : 1;
                return [Centre[0] + Math.cos(Angle) * Math.abs(End[0] - Start[0]) / 2 * Radius, Centre[1] + Math.sin(Angle) * Math.abs(End[1] - Start[1]) / 2 * Radius];
            });
        }
        return { category: Category, points: Points, ink: Tool === "erase" ? 0 : Ink, width: Width, opacity: Opacity, softness: Softness, filled: Filled };
    }
    function Cancel() { Gesture.current = null; ConfigureDraft(null); ConfigurePenPoints([]); }
    function Down(Event)
    {
        if (Disabled || Event.button !== 0) return;
        Event.preventDefault(); Event.currentTarget.focus({ preventScroll: true });
        const Point = Position(Event);
        if (Tool === "pen")
        {
            if (PenPoints.length >= 2048) { OnError?.("Mask paths support up to 2,048 points."); return; }
            ConfigurePenPoints(Previous => [...Previous, Point]); return;
        }
        Event.currentTarget.setPointerCapture(Event.pointerId);
        Gesture.current = { Pointer: Event.pointerId, Points: [Point, Point] }; ConfigureDraft(Operation([Point, Point]));
    }
    function Move(Event)
    {
        const Active = Gesture.current;
        if (!Active || Active.Pointer !== Event.pointerId) return;
        const Point = Position(Event);
        if (Tool === "brush" || Tool === "erase")
        {
            if (Active.Points.length >= 2048) Active.Points[2047] = Point;
            else Active.Points.push(Point);
        }
        else Active.Points = [Active.Points[0], Point];
        ConfigureDraft(Operation(Active.Points));
    }
    function Up(Event)
    {
        if (!Gesture.current || Gesture.current.Pointer !== Event.pointerId) return;
        Move(Event); const Active = Gesture.current; Gesture.current = null;
        const First = Active.Points[0], Last = Active.Points.at(-1);
        if (Tool === "gradient" && Math.hypot(First[0] - Last[0], First[1] - Last[1]) < .002) ConfigureDraft(null);
        else Finalize(Operation(Active.Points));
        if (Event.currentTarget.hasPointerCapture(Event.pointerId)) Event.currentTarget.releasePointerCapture(Event.pointerId);
    }
    const Working = Draft ? { ...Mask, paint: [...(Mask.paint || []), Draft] } : Mask;
    return <section className="artwork-mask-editor" aria-label={`Mask editor for ${Name}`} onKeyDown={Event =>
    {
        Event.stopPropagation();
        if ((Event.ctrlKey || Event.metaKey) && Event.key.toLowerCase() === "s") { Event.preventDefault(); OnSave?.(); return; }
        if (Event.target.closest("input,select,textarea")) return;
        if ((Event.ctrlKey || Event.metaKey) && Event.key.toLowerCase() === "z") { Event.preventDefault(); Cancel(); OnHistory(Event.shiftKey ? "redo" : "undo"); }
        else if (Event.key === "Escape") { Event.preventDefault(); Cancel(); }
        else if (Event.key === "Enter" && PenPoints.length >= 3) { Event.preventDefault(); Finalize(Operation(PenPoints)); }
    }}>
        <header><div><strong>Layer mask</strong><span>{Name} · white reveals, black conceals</span></div><button onClick={() => { Cancel(); OnClose(); }}><Check size={14}/>Done</button></header>
        <div className="artwork-mask-body"><fieldset disabled={Disabled}>
            <div className="artwork-mask-tools" role="toolbar" aria-label="Mask tools">
                {Tools.filter(([Category]) => ["brush", "erase", "rect", "ellipse", "pen", "gradient"].includes(Category)).map(([Category, Caption, Icon]) => <button key={Category} aria-label={Caption} title={Caption} aria-pressed={Tool === Category} onClick={() => { Cancel(); ConfigureTool(Category); }}><Icon size={16}/></button>)}
                <details className="se-tool-menu" name="vector-toolbar-menus"><summary aria-label="More mask shapes" title="More mask shapes"><MoreHorizontal size={16}/></summary><div className="se-menu-content">{Tools.filter(([Category]) => ["triangle", "diamond", "polygon", "star", "line"].includes(Category)).map(([Category, Caption, Icon]) => <button key={Category} aria-label={Caption} title={Caption} aria-pressed={Tool === Category} onClick={Event => { Cancel(); ConfigureTool(Category); Event.currentTarget.closest("details").removeAttribute("open"); }}><Icon size={16}/></button>)}</div></details>
            </div>
            <div className="artwork-mask-ink"><span>Ink</span>{[[1, "White — reveal"], [0, "Black — conceal"]].map(([Gray, Caption]) => <button key={Gray} aria-label={Caption} aria-pressed={Ink === Gray} style={{ "--mask-ink": Gray ? "#efefe8" : "#090909" }} onClick={() => ConfigureInk(Gray)}/>)}
                <label>Size<input aria-label="Mask brush size" type="range" min=".005" max=".3" step=".005" value={Width} onChange={Event => ConfigureWidth(+Event.target.value)}/></label>
                <details><summary>Brush & shape options</summary><div><label>Opacity<input aria-label="Mask brush opacity" type="range" min=".05" max="1" step=".05" value={Opacity} onChange={Event => ConfigureOpacity(+Event.target.value)}/></label><label>Softness<input aria-label="Mask brush softness" type="range" min="0" max="1" step=".05" value={Softness} onChange={Event => ConfigureSoftness(+Event.target.value)}/></label><label>Grey ink<input aria-label="Mask grey ink" type="range" min="0" max="1" step=".05" value={Ink} onChange={Event => ConfigureInk(+Event.target.value)}/></label><button aria-pressed={Filled} onClick={() => ConfigureFilled(!Filled)}>{Filled ? "Filled shapes" : "Shape outlines"}</button></div></details>
            </div>
            <div className="artwork-mask-fill"><button onClick={() => OnChange(NormalizeArtworkMask({ ...Mask, category: "paint", base: 1, paint: [], invert: false }))}>Reveal all</button><button onClick={() => OnChange(NormalizeArtworkMask({ ...Mask, category: "paint", base: 0, paint: [], invert: false }))}>Hide all</button><button aria-label="Undo mask edit" onClick={() => { Cancel(); OnHistory("undo"); }}><Undo2 size={14}/></button><button aria-label="Redo mask edit" onClick={() => { Cancel(); OnHistory("redo"); }}><Redo2 size={14}/></button>{Tool === "pen" && <button disabled={PenPoints.length < 3} onClick={() => Finalize(Operation(PenPoints))}>Fill path</button>}</div>
        </fieldset><div className="artwork-mask-bounds" ref={SurfaceBounds}><div className="artwork-mask-surface" style={{ width: SurfaceEdge, height: SurfaceEdge }} ref={Surface} tabIndex={0} role="application" aria-label="Paint selected layer mask" onPointerDown={Down} onPointerMove={Move} onPointerUp={Up} onPointerCancel={Cancel} onLostPointerCapture={() => { if (Gesture.current) Cancel(); }}>
                <img src={`data:image/svg+xml;charset=utf-8,${encodeURIComponent(ArtworkMaskPreviewSVG(Working))}`} draggable="false" alt="Editable mask coverage"/>
                {PenPoints.length > 0 && <svg viewBox="0 0 1 1" aria-hidden="true"><polyline points={PenPoints.map(Point => Point.join(",")).join(" ")} fill="none" stroke="#d9b281" strokeWidth=".004"/>{PenPoints.map((Point, Index) => <circle key={Index} cx={Point[0]} cy={Point[1]} r=".007" fill="#d9b281"/>)}</svg>}
            </div></div>
        </div>
        <footer>{Disabled ? "Unlock the owning layer to edit its mask." : Tool === "pen" ? "Click to place points · Enter / Fill path to finish · Escape cancels" : "Drag to paint or fill a shape · Escape cancels · one gesture, one undo"}<small>{Mask.paint?.length || 0} retained edits · layer-local coordinates{!Mask.enabled ? " · mask bypassed" : ""}</small></footer>
    </section>;
}

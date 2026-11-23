import { createPortal } from "react-dom";
import { ArtworkSelectionMask } from "./ArtworkSelectionMask.js";
/*============================================================================================================================================
                                                           STAMPVECTORCANVAS.JSX
============================================================================================================================================*/
// 🧩 Shared vector editing, rulers and finite decal geometry using stamp-owned document transactions.

import React, { forwardRef, useEffect, useImperativeHandle, useMemo, useRef, useState } from "react";
import ShapeEditorCanvas from "./ShapeEditorCanvas.jsx";
import ShapeEditorToolbar, { ShapeSelectionActions, shapeTools } from "./ShapeEditorToolbar.jsx";
import { StampVectorCanvasDocument, StampDocumentFromVectorCanvas } from "./StampVectorProjection.js";
import { booleanPatternLayers, layerWorldContours } from "./vectorEditorGeometry.js";
import { alignSelection, distributeSelection, reorderSelection, clonePatternLayers, newPatternGroup, editableSelection, translateSelection } from "./shapeEditorGeometry.js";
import { stampId } from "./stampDocument.js";
import { loadPatternTextFonts } from "./patternTextFonts.js";
import "./shapeEditor.css";
import "./stampVectorCanvas.css";

export default forwardRef(function StampVectorCanvas({ Appearance, OnAppearance, Document, SelectedIDs, OnSelection, OnFinalize, OnError, OnHistory, Paint, OnPaint, OnAddText, OnImportSVG, InspectorTarget }, Reference)
{
    const Canvas = useRef(null);
    const DocumentRevision = useRef(Document); DocumentRevision.current = Document;
    const SelectionRevision = useRef(SelectedIDs.join("|")); SelectionRevision.current = SelectedIDs.join("|");
    const [Tool, ConfigureTool] = useState("move");
    const [Grid, ConfigureGrid] = useState(0);
    const [GridVisible, ConfigureGridVisible] = useState(false);
    const [Aspect, ConfigureAspect] = useState(false);
    const [Guides, ConfigureGuides] = useState(true);
    const [Snapping, ConfigureSnapping] = useState({ points: true, curves: false, guides: true, angle: 0 });
    const [FontRevision, ConfigureFontRevision] = useState(0);
    const [Preview, ConfigurePreview] = useState(null);
    const [PathActivity, ConfigurePathActivity] = useState({ points: 0 });
    const Clipboard = useRef([]);
    useEffect(() => { let Active = true; loadPatternTextFonts().then(() => { if (Active) ConfigureFontRevision(1); }).catch(Failure => OnError(Failure.message)); return () => { Active = false; }; }, []);
    const Projection = useMemo(() => StampVectorCanvasDocument(Document), [Document, FontRevision]);
    const Current = Preview || Projection;
    const Selected = Current.layers.flatMap((Layer, Index) => SelectedIDs.includes(Layer.id) ? [Index] : []);
    const Editable = editableSelection(Current, Selected);
    useEffect(() => { ConfigurePreview(null); }, [Document]);
    function Select(Indices) { OnSelection(Indices.map(Index => Current.layers[Index]?.id).filter(Boolean)); }
    function ChangeTool(Next)
    {
        if (Canvas.current?.hasPendingPath() && !["pen", "curve", "node", "hand"].includes(Next))
        {
            if (PathActivity.points < 2) Canvas.current.cancelInteraction();
            else if (!Canvas.current.prepareExport()) return;
        }
        ConfigureTool(Next);
    }
    function Finalize(Next, Options = {})
    {
        try
        {
            const Converted = StampDocumentFromVectorCanvas(Next, Document, Options.selected || Selected);
            const Accepted = OnFinalize(Converted.Document, Options.key, Converted.SelectedIDs.at(-1), Converted.SelectedIDs);
            if (Accepted) ConfigurePreview(null);
            return Accepted;
        }
        catch (Failure) { OnError(Failure.message); return false; }
    }
    function Action(Command)
    {
        try
        {
            if (Canvas.current?.hasPendingPath())
            {
                Canvas.current.prepareExport();
                return;
            }
            const Chosen = new Set(Editable);
            if (Command.startsWith("boolean-"))
            {
                if (Editable.length !== Selected.length) throw new Error("Unlock and show every selected vector before combining.");
                const Ordered = [...Editable].sort((Left, Right) => Left - Right);
                const Result = booleanPatternLayers(Ordered.map(Index => Current.layers[Index]), Command.slice(8));
                const Target = Ordered[0];
                Finalize({ ...Current, layers: Current.layers.flatMap((Layer, Index) => Index === Target ? [Result] : Chosen.has(Index) ? [] : [Layer]) }, { selected: [Target] });
                ConfigureTool("move");
            }
            else if (Command.startsWith("align-")) Finalize(alignSelection(Current, Editable, Command.slice(6)));
            else if (Command.startsWith("distribute-")) Finalize(distributeSelection(Current, Editable, Command.slice(11)));
            else if (["front", "back", "forward", "backward"].includes(Command)) { const Result = reorderSelection(Current, Editable, Command); Finalize(Result.doc, { selected: Result.selected }); }
            else if (Command === "delete") Finalize({ ...Current, layers: Current.layers.filter((_, Index) => !Chosen.has(Index)) }, { selected: [] });
            else if (Command === "copy") Clipboard.current = structuredClone(Editable.map(Index => Current.layers[Index]));
            else if (Command === "duplicate" || Command === "paste")
            {
                const Source = Command === "paste" ? Clipboard.current : Editable.map(Index => Current.layers[Index]);
                if (Source.some(Layer => ["svg", "image"].includes(Layer.kind))) throw new Error("Use the existing component Duplicate button to copy rich text, SVG or images.");
                const Copies = clonePatternLayers(Source).map(Layer => ({ ...Layer, id: stampId() }));
                Finalize({ ...Current, layers: [...Current.layers, ...Copies] }, { selected: Copies.map((_, Index) => Current.layers.length + Index) });
            }
            else if (["group", "ungroup", "lock"].includes(Command))
            {
                if (Editable.some(Index => ["svg", "image"].includes(Current.layers[Index].kind)) && Command !== "lock") throw new Error("Group editable vector shapes; imported components keep their own editors.");
                const Group = newPatternGroup();
                Finalize({ ...Current, layers: Current.layers.map((Layer, Index) => !Chosen.has(Index) ? Layer : Command === "lock" ? { ...Layer, locked: true } : { ...Layer, group: Command === "group" ? Group : undefined }) });
            }
        }
        catch (Failure) { OnError(Failure.message); }
    }
    function Keyboard(Event)
    {
        if (Event.defaultPrevented || Event.target.closest("input,textarea,select,[contenteditable=true]")) return;
        const Key = Event.key.toLowerCase(), Command = Event.ctrlKey || Event.metaKey;
        Event.stopPropagation();
        if (Canvas.current?.consumeKey(Event)) { Event.preventDefault(); return; }
        if (Command && Key === "z") { Event.preventDefault(); if (!Canvas.current?.undoPath()) OnHistory(Event.shiftKey ? "redo" : "undo"); }
        else if (Command && Key === "a") { Event.preventDefault(); Select(Current.layers.flatMap((Layer, Index) => Layer.visible && !Layer.locked ? [Index] : [])); }
        else if (Command && ["c", "v", "d", "g"].includes(Key)) { Event.preventDefault(); Action({ c: "copy", v: "paste", d: "duplicate", g: Event.shiftKey ? "ungroup" : "group" }[Key]); }
        else if (["delete", "backspace"].includes(Key)) { Event.preventDefault(); Action("delete"); }
        else if (Key === "escape") { Canvas.current?.cancelInteraction(); ConfigurePreview(null); ConfigureTool("move"); }
        else if (Key === "enter") { Event.preventDefault(); Canvas.current?.finishPen(false); }
        else if (Event.key.startsWith("Arrow")) { Event.preventDefault(); const Step = (Grid || 1) * (Event.shiftKey ? 10 : 1); Finalize(translateSelection(Current, Editable, Key === "arrowleft" ? -Step : Key === "arrowright" ? Step : 0, Key === "arrowup" ? -Step : Key === "arrowdown" ? Step : 0)); }
        else { const Match = shapeTools.find(([, , Shortcut]) => Shortcut.toLowerCase() === Key); if (Match) { Event.preventDefault(); ChangeTool(Match[0]); } }
    }
    useImperativeHandle(Reference, () => ({ prepareExport: () => Canvas.current?.prepareExport() ?? true, cancelInteraction: () => Canvas.current?.cancelInteraction(), undoPath: () => Canvas.current?.undoPath() }));
    const BooleanEnabled = Selected.length >= 2 && Selected.length <= 16 && Editable.length === Selected.length && Editable.every(Index => { const Layer = Current.layers[Index]; const Contours = layerWorldContours(Layer); return !["svg", "image"].includes(Layer.kind) && Layer.fillEnabled !== false && Contours.length && Contours.every(Contour => Contour.closed); });
    return <div className="stamp-vector-workbench studio-themed" onKeyDown={Keyboard}>
        <div className="stamp-canvas-appearance" role="group" aria-label="Vector canvas background">
            <label>Canvas background<select aria-label="Vector canvas background" value={Appearance.Category} onChange={Event => OnAppearance({ ...Appearance, Category: Event.target.value })}>
                <option value="transparent">Transparent</option><option value="ui">UI background</option><option value="fill">Fill colour</option>
            </select></label>
            {Appearance.Category === "fill" && <label>Colour<input type="color" aria-label="Vector canvas background colour" value={Appearance.Colour} onChange={Event => OnAppearance({ ...Appearance, Colour: Event.target.value })}/></label>}
            <small>View only · exports stay transparent</small>
        </div>
        <ShapeEditorToolbar tool={Tool} onToolChange={ChangeTool} grid={Grid} onGridChange={ConfigureGrid} showGrid={GridVisible} onGridToggle={() => ConfigureGridVisible(!GridVisible)} smartGuides={Guides} onGuidesToggle={() => ConfigureGuides(!Guides)} aspect={Aspect} onAspectChange={ConfigureAspect} snapping={Snapping} onSnappingChange={ConfigureSnapping} onSVGSource={OnImportSVG}/>
        {InspectorTarget && createPortal(<ShapeSelectionActions count={Selected.length} hasGroup={Editable.some(Index => Current.layers[Index].group)} canBoolean={BooleanEnabled} onAction={Action}
            MaskNames={Selected.length === 2 && Editable.length === 2 ? Selected.map(Index => Current.layers[Index].name) : null}
            OnUseMask={async SourceOffset => { try { const Snapshot = Document; const Next = await ArtworkSelectionMask(Current, Selected[1 - SourceOffset], Selected[SourceOffset]); if (DocumentRevision.current !== Snapshot || SelectionRevision.current !== SelectedIDs.join("|")) throw new Error("Selection changed; try again."); Finalize(Next, { selected: [Selected[1 - SourceOffset]] }); } catch (Failure) { OnError(Failure.message); } }}/>, InspectorTarget)}
        <ShapeEditorCanvas ref={Canvas} CanvasLabel="Stamp vector canvas" CanvasBackdrop={Appearance.Category === "ui" ? "var(--panel, #181818)" : Appearance.Category === "fill" ? Appearance.Colour : undefined} TextEditingPredicate={Layer => Document.layers.some(Artwork => Artwork.id === Layer?.id && Artwork.kind === "text")} doc={Current} selected={Selected} tool={Tool} onToolChange={ConfigureTool} grid={Grid} aspect={Aspect} showGrid={GridVisible} smartGuides={Guides} snapping={Snapping} style={Paint} onDrawingStyle={OnPaint} onCreateText={OnAddText} onEditText={Index => Select([Index])} onSampleAppearance={Index => { const Source = Current.layers[Index]; if (Source && !["svg", "image"].includes(Source.kind)) OnPaint({ color: Source.color, fillSource: Source.fillSource, stroke: Source.stroke, strokeSource: Source.strokeSource }); }} onSelection={Select} onPreview={Next => ConfigurePreview(Next)} onCommit={Finalize} onCancel={() => ConfigurePreview(null)} onError={OnError} onAction={Action} onPathStateChange={ConfigurePathActivity}/>
        <p className="stamp-vector-note">Finite transparent artwork · Shift-click or drag a marquee to select vectors · Text, SVG and images retain their original editors.</p>
    </div>;
});

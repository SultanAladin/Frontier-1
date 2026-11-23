/*============================================================================================================================================
                                                   PATTERNORGANIZATIONCONTROLS.JSX
============================================================================================================================================*/
// 🧩 Nested group/folder navigation and document-owned reusable linked artwork.

import React, { useState } from "react";
import { CreatePatternEnclosure, StorePatternGroup, InsertPatternGroup, PatternEnclosureContains } from "./PatternOrganization.js";
import { patternSVG } from "./patternDocument.js";
export default function PatternOrganizationControls({ Document, Selection, OnFinalize, OnSelect, OnError, Scope, OnScope })
{
    const [Caption, ConfigureCaption] = useState("Group"), [Destination, ConfigureDestination] = useState(""), [Query, ConfigureQuery] = useState("");
    const Enclosures = Document.Enclosures || [], Definitions = Document.StoredGroups || [];
    function Finalize(Operation)
    {
        try { OnFinalize(Operation()); } catch (Failure) { OnError(Failure.message); }
    }
    function Members(Token) { return Document.layers.flatMap((Layer, Index) => PatternEnclosureContains(Document, Token, Layer.EnclosureToken) ? [Index] : []); }
    function RenderEnclosures(Token = "")
    {
        return Enclosures.filter(Entry => Entry.EnclosureToken === Token).map(Entry => <div className="pattern-enclosure" key={Entry.Token}>
            <div className="pattern-enclosure-heading"><button aria-label={`${Entry.Collapsed ? "Expand" : "Collapse"} ${Entry.Caption}`} aria-expanded={!Entry.Collapsed} onClick={() => OnFinalize({ ...Document, Enclosures: Enclosures.map(Candidate => Candidate.Token === Entry.Token ? { ...Candidate, Collapsed: !Candidate.Collapsed } : Candidate) })}>{Entry.Collapsed ? "▸" : "▾"}</button>
            <button aria-pressed={Scope === Entry.Token} onClick={() => { OnScope(Entry.Token); OnSelect(Members(Entry.Token)); }}>{Entry.Category === "folder" ? "▱" : "◇"} {Entry.Caption} <small>{Members(Entry.Token).length}</small></button></div>
            {!Entry.Collapsed && RenderEnclosures(Entry.Token)}
        </div>);
    }
    return <section className="pattern-organization" aria-label="Pattern groups and folders">
        <details><summary>Groups &amp; folders</summary>
            <label>Name<input aria-label="Group or folder name" value={Caption} maxLength={52} onChange={Event => ConfigureCaption(Event.target.value)}/></label>
            <label>Inside<select aria-label="Group destination" value={Destination} onChange={Event => ConfigureDestination(Event.target.value)}><option value="">Document root</option>{Enclosures.map(Entry => <option key={Entry.Token} value={Entry.Token}>{Entry.Category === "folder" ? "Folder" : "Group"} · {Entry.Caption}</option>)}</select></label>
            <div className="pattern-organization-actions">{["group", "folder"].map(Category => <button key={Category} onClick={() => Finalize(() => CreatePatternEnclosure(Document, Category === "folder" ? [] : Selection, Caption || Category, Category, Destination))}>+ {Category === "group" ? "Group" : "Folder"}</button>)}</div>
            <button disabled={!Selection.length} onClick={() => OnFinalize({ ...Document, layers: Document.layers.map((Layer, Index) => Selection.includes(Index) && !Layer.locked ? { ...Layer, EnclosureToken: Destination, group: Enclosures.find(Entry => Entry.Token === Destination)?.Category === "group" ? Destination : undefined } : Layer) })}>Move selected artwork here</button>
            <button aria-pressed={!Scope} onClick={() => OnScope("")}>Show all artwork</button>
            <div className="pattern-enclosure-tree" aria-label="Stored document groups">{RenderEnclosures()}</div>
            {Scope && <div className="pattern-organization-actions">
                <button onClick={() => OnFinalize({ ...Document, Enclosures: Enclosures.map(Entry => Entry.Token === Scope ? { ...Entry, Caption: Caption || Entry.Caption } : Entry) })}>Rename container</button>
                <button onClick={() => OnFinalize({ ...Document, Enclosures: Enclosures.map(Entry => Entry.Token === Scope ? { ...Entry, EnclosureToken: Destination } : Entry) })}>Nest container here</button>
                <button onClick={() => { const Indices = Members(Scope); OnFinalize({ ...Document, layers: Document.layers.map((Layer, Index) => Indices.includes(Index) ? { ...Layer, visible: !Document.layers[Indices[0]]?.visible } : Layer) }); }}>Toggle visibility</button>
                <button onClick={() => { const Indices = Members(Scope); OnFinalize({ ...Document, layers: Document.layers.map((Layer, Index) => Indices.includes(Index) ? { ...Layer, locked: !Document.layers[Indices[0]]?.locked } : Layer) }); }}>Toggle lock</button>
                <button onClick={() => { const Up = Enclosures.find(Entry => Entry.Token === Scope)?.EnclosureToken || ""; OnFinalize({ ...Document, Enclosures: Enclosures.filter(Entry => Entry.Token !== Scope).map(Entry => Entry.EnclosureToken === Scope ? { ...Entry, EnclosureToken: Up } : Entry), layers: Document.layers.map(Layer => Layer.EnclosureToken === Scope ? { ...Layer, EnclosureToken: Up, group: undefined } : Layer) }); OnScope(""); }}>Dissolve container</button>
            </div>}
            <label className="pattern-channel-toggle"><input type="checkbox" role="switch" aria-label="Group-wide property editing" checked={Document.EditGroupProperties} onChange={Event => OnFinalize({ ...Document, EditGroupProperties: Event.target.checked })}/>Group-wide properties</label>
            <small>Shares changed appearance/channel properties within the nearest group, including nested contents. Placement stays independent.</small>
            <label className="pattern-channel-toggle"><input type="checkbox" role="switch" aria-label="Linked instance editing" checked={Document.EditLinkedInstances} onChange={Event => OnFinalize({ ...Document, EditLinkedInstances: Event.target.checked })}/>Linked instance editing</label>
            <small>Shares corresponding feature appearance/geometry across linked copies. Placement stays local; feature dimensions update proportionally. Locked artwork is protected.</small>
        </details>
        <details><summary>Stored groups · {Definitions.length}</summary>
            <label>Search<input aria-label="Find stored group" value={Query} onChange={Event => ConfigureQuery(Event.target.value)}/></label>
            <button disabled={!Selection.some(Index => !Document.layers[Index]?.locked)} onClick={() => Finalize(() => StorePatternGroup(Document, Selection, Caption || "Stored group"))}>Store selected group</button>
            <button disabled={!Selection.some(Index => Document.layers[Index]?.LinkedDefinition && !Document.layers[Index].locked)} onClick={() => OnFinalize({ ...Document, layers: Document.layers.map((Layer, Index) => Selection.includes(Index) && !Layer.locked ? { ...Layer, LinkedDefinition: undefined, LinkedSlot: undefined, LinkedInstance: undefined } : Layer) })}>Detach selected links</button>
            <div className="pattern-stored-groups">{Definitions.filter(Definition => Definition.Caption.toLowerCase().includes(Query.toLowerCase())).map(Definition => <article key={Definition.Token}>
                <img alt={`${Definition.Caption} group thumbnail`} src={`data:image/svg+xml;charset=utf-8,${encodeURIComponent(patternSVG({ schema: "alloy.pattern.v1", layers: Definition.Layers, backgroundOpacity: 0, tileAxes: "none" }))}`}/>
                <strong>{Definition.Caption}</strong><small>{Definition.Layers.length} features · {new Set(Document.layers.filter(Layer => Layer.LinkedDefinition === Definition.Token).map(Layer => Layer.LinkedInstance)).size} linked placements</small>
                <button disabled={Document.layers.some(Layer => Layer.LinkedDefinition === Definition.Token)} title="Detach every linked placement before removing its definition." onClick={() => OnFinalize({ ...Document, StoredGroups: Definitions.filter(Entry => Entry.Token !== Definition.Token) })}>Remove unused definition</button>
                <div className="pattern-organization-actions">{[false, true].map(Linked => <button key={String(Linked)} onClick={() => Finalize(() => { const Next = InsertPatternGroup(Document, Definition.Token, Linked); OnScope(""); return Next; })}>{Linked ? "Insert linked" : "Insert copy"}</button>)}</div>
            </article>)}</div>
            {!Definitions.length && <small>Store selected artwork to reuse it in this document. Saved with the pattern and restored on reopen.</small>}
        </details>
    </section>;
}

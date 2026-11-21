/*============================================================================================================================================
                                                       PATTERNORGANIZATION.JS
============================================================================================================================================*/
// 🧩 Nested artwork enclosures, saved group snapshots and explicit property propagation.

export const IssuePatternToken = () => globalThis.crypto?.randomUUID?.() || `pattern-${Date.now()}-${Math.random().toString(36).slice(2)}`;
export const GroupPropertyKeys = ["color", "fillSource", "strokeSource", "stroke", "strokeWidth", "opacity", "finish", "roughness", "metalness", "relief", "fillEnabled", "fillRule", "strokeLinecap", "strokeLinejoin", "SurfaceChannels"];
const LinkedPropertyKeys = [...GroupPropertyKeys, "width", "height", "kind", "nodes", "path", "closed", "text", "svg", "src", "sides", "starPoints", "innerRadius", "cornerRadius", "arcStart", "arcSweep", "arcClosure", "curveTension"];
export function PatternEnclosureContains(Document, Enclosure, Token)
{
    const Visited = new Set();
    while (Token && !Visited.has(Token))
    {
        if (Token === Enclosure) return true;
        Visited.add(Token); Token = Document.Enclosures?.find(Entry => Entry.Token === Token)?.EnclosureToken;
    }
    return false;
}
export function NormalizePatternOrganization(Document)
{
    const Tokens = new Set(), Captions = new Set();
    const AuthoredEnclosures = Array.isArray(Document.Enclosures) ? [...Document.Enclosures] : [];
    for (const Layer of Document.layers)
        if (Layer.group && !AuthoredEnclosures.some(Entry => Entry.Token === Layer.EnclosureToken) && !AuthoredEnclosures.some(Entry => Entry.Token === Layer.group)) AuthoredEnclosures.push({ Token: Layer.group, Caption: `Group ${AuthoredEnclosures.length + 1}`, Category: "group" });
    if (AuthoredEnclosures.length > 64) throw new Error("A pattern supports up to 64 groups/folders.");
    const Enclosures = AuthoredEnclosures.map(Entry =>
    {
        const Token = String(Entry.Token || IssuePatternToken()).slice(0, 80);
        if (Tokens.has(Token)) throw new Error("Duplicate group/folder identity."); Tokens.add(Token);
        const Stem = String(Entry.Caption || "Group").trim().slice(0, 52) || "Group"; let Caption = Stem, Ordinal = 2;
        while (Captions.has(Caption.toLowerCase())) Caption = `${Stem} ${Ordinal++}`;
        Captions.add(Caption.toLowerCase());
        return { Token, Caption, Category: Entry.Category === "folder" ? "folder" : "group", EnclosureToken: String(Entry.EnclosureToken || "").slice(0, 80), Collapsed: Entry.Collapsed === true };
    });
    for (const Entry of Enclosures)
    {
        if (!Tokens.has(Entry.EnclosureToken)) Entry.EnclosureToken = "";
        let Cursor = Entry, Depth = 0; const Visited = new Set();
        while (Cursor)
        {
            if (Visited.has(Cursor.Token) || ++Depth > 12) throw new Error("Groups/folders cannot form a cycle or exceed 12 nesting levels.");
            Visited.add(Cursor.Token); Cursor = Enclosures.find(Candidate => Candidate.Token === Cursor.EnclosureToken);
        }
    }
    const ArtworkTokens = new Set();
    return { ...Document, Enclosures, EditGroupProperties: Document.EditGroupProperties === true, EditLinkedInstances: Document.EditLinkedInstances === true,
        layers: Document.layers.map(Layer => {
            let StructureToken = Layer.StructureToken || ((Tokens.has(Layer.EnclosureToken) || Tokens.has(Layer.group) || Layer.LinkedDefinition) ? IssuePatternToken() : undefined);
            if (StructureToken && ArtworkTokens.has(StructureToken)) StructureToken = IssuePatternToken();
            if (StructureToken) ArtworkTokens.add(StructureToken);
            const EnclosureToken = Tokens.has(Layer.EnclosureToken) ? Layer.EnclosureToken : Tokens.has(Layer.group) ? Layer.group : undefined;
            let Group = Enclosures.find(Entry => Entry.Token === EnclosureToken);
            while (Group?.Category === "folder") Group = Enclosures.find(Entry => Entry.Token === Group.EnclosureToken);
            return { ...Layer, ...(StructureToken ? { StructureToken } : {}), EnclosureToken, group: Group?.Token };
        }) };
}
export function CreatePatternEnclosure(Document, Selection, Caption, Category = "group", EnclosureToken = "")
{
    const Token = IssuePatternToken();
    return { ...Document, Enclosures: [...(Document.Enclosures || []), { Token, Caption, Category, EnclosureToken }], layers: Document.layers.map((Layer, Index) => Selection.includes(Index) && !Layer.locked ? { ...Layer, StructureToken: Layer.StructureToken || IssuePatternToken(), EnclosureToken: Token, group: Category === "group" ? Token : undefined } : Layer) };
}
function ApplyPatternChannelDifferences(Before, After, Target)
{
    const Channels = structuredClone(Target || {});
    for (const Caption of new Set([...Object.keys(Before || {}), ...Object.keys(After || {})]))
        for (const Property of ["Enabled", "Strength", "Source"])
        {
            const Default = Property === "Enabled" ? Caption !== "Occlusion" : Property === "Strength" ? 1 : undefined;
            const Previous = Before?.[Caption]?.[Property] ?? Default, Next = After?.[Caption]?.[Property] ?? Default;
            if (Previous !== Next) Channels[Caption] = { Enabled: Caption !== "Occlusion", Strength: 1, ...Channels[Caption], [Property]: Next };
        }
    return Channels;
}
export function PropagatePatternProperties(Before, After)
{
    if (!Before.EditGroupProperties && !Before.EditLinkedInstances) return After;
    const Layers = After.layers.map(Layer => ({ ...Layer }));
    const Applied = new Set();
    for (let Index = 0; Index < Before.layers.length; ++Index)
    {
        const Original = Before.layers[Index];
        const Changed = Layers.find(Layer => Original.StructureToken ? Layer.StructureToken === Original.StructureToken : Layer.id === Original.id);
        if (!Changed || Changed.locked || Original.locked) continue;
        let Group = Before.Enclosures?.find(Entry => Entry.Token === Original.EnclosureToken);
        while (Group?.Category === "folder") Group = Before.Enclosures?.find(Entry => Entry.Token === Group.EnclosureToken);
        for (const Key of LinkedPropertyKeys)
        {
            if (JSON.stringify(Original[Key]) === JSON.stringify(Changed[Key])) continue;
            const Signature = `${Original.StructureToken || Original.id}:${Key}`;
            if (Applied.has(Signature)) continue;
            for (const Target of Layers)
            {
                if (Target.locked || Target === Changed || Before.layers.find(Layer => Target.StructureToken ? Layer.StructureToken === Target.StructureToken : Layer.id === Target.id)?.locked) continue;
                const GroupWide = Before.EditGroupProperties && Group && GroupPropertyKeys.includes(Key) && PatternEnclosureContains(Before, Group.Token, Target.EnclosureToken);
                const Linked = Before.EditLinkedInstances && Original.LinkedDefinition && Target.LinkedDefinition === Original.LinkedDefinition && Target.LinkedSlot === Original.LinkedSlot;
                if (GroupWide || Linked)
                {
                    if (Key === "SurfaceChannels") Target[Key] = ApplyPatternChannelDifferences(Original[Key], Changed[Key], Target[Key]);
                    else if (["width", "height"].includes(Key)) Target[Key] *= Changed[Key] / Original[Key];
                    else Target[Key] = structuredClone(Changed[Key]); Applied.add(`${Target.StructureToken || Target.id}:${Key}`);
                }
            }
        }
    }
    const StoredGroups = (After.StoredGroups || []).map(Definition => ({ ...Definition, Layers: Definition.Layers.map(Layer =>
    {
        if (!Before.EditLinkedInstances) return Layer;
        const Projection = { ...Layer };
        for (const Key of LinkedPropertyKeys)
        {
            const Original = Before.layers.find(Candidate => Candidate.LinkedDefinition === Definition.Token && Candidate.LinkedSlot === Layer.LinkedSlot && !Candidate.locked && Layers.some(Reference => Reference.StructureToken === Candidate.StructureToken && !Reference.locked && JSON.stringify(Candidate[Key]) !== JSON.stringify(Reference[Key])));
            const Reference = Original && Layers.find(Candidate => Candidate.StructureToken === Original.StructureToken);
            if (!Reference) continue;
            if (Key === "SurfaceChannels") Projection[Key] = ApplyPatternChannelDifferences(Original[Key], Reference[Key], Layer[Key]);
            else if (["width", "height"].includes(Key)) Projection[Key] = Layer[Key] * Reference[Key] / Original[Key];
            else Projection[Key] = structuredClone(Reference[Key]);
        }
        return Projection;
    }) }));
    return { ...After, layers: Layers, StoredGroups };
}
export function StorePatternGroup(Document, Selection, Caption)
{
    const Indices = Selection.filter(Index => Document.layers[Index] && !Document.layers[Index].locked);
    if (!Indices.length) throw new Error("Select unlocked artwork to store a group.");
    if ((Document.StoredGroups || []).length >= 24) throw new Error("A document can store 24 group definitions.");
    const Token = IssuePatternToken(), Instance = IssuePatternToken();
    const Stem = String(Caption).trim().slice(0, 50) || "Stored group"; let UniqueCaption = Stem, Ordinal = 2;
    while ((Document.StoredGroups || []).some(Entry => Entry.Caption.toLowerCase() === UniqueCaption.toLowerCase())) UniqueCaption = `${Stem} ${Ordinal++}`;
    Caption = UniqueCaption;
    const Selected = Indices.map(Index => Document.layers[Index]);
    const CentreX = Selected.reduce((Sum, Layer) => Sum + Layer.x, 0) / Selected.length, CentreY = Selected.reduce((Sum, Layer) => Sum + Layer.y, 0) / Selected.length;
    const Enclosures = (Document.Enclosures || []).filter(Entry => Selected.some(Layer => PatternEnclosureContains(Document, Entry.Token, Layer.EnclosureToken)));
    const Slots = new Map(Indices.map(Index => [Index, IssuePatternToken()]));
    const Layers = Selected.map((Layer, Ordinal) => ({ ...structuredClone(Layer), x: Layer.x - CentreX + 256, y: Layer.y - CentreY + 256, LinkedSlot: Slots.get(Indices[Ordinal]) }));
    return { ...Document, StoredGroups: [...(Document.StoredGroups || []), { Token, Caption, Layers, Enclosures }], layers: Document.layers.map((Layer, Index) => Slots.has(Index) ? { ...Layer, StructureToken: Layer.StructureToken || IssuePatternToken(), LinkedDefinition: Token, LinkedSlot: Slots.get(Index), LinkedInstance: Instance } : Layer) };
}
export function InsertPatternGroup(Document, Token, Linked)
{
    const Definition = Document.StoredGroups?.find(Entry => Entry.Token === Token);
    if (!Definition) throw new Error("Stored group no longer exists.");
    if (Document.layers.length + Definition.Layers.length > 64) throw new Error("The stored group exceeds the 64-layer limit.");
    const EnclosureTokens = new Map(Definition.Enclosures.map(Entry => [Entry.Token, IssuePatternToken()]));
    const Instance = IssuePatternToken(), Group = IssuePatternToken();
    const Enclosures = [{ Token: Group, Caption: Definition.Caption, Category: "group", EnclosureToken: "" }, ...Definition.Enclosures.map(Entry => ({ ...Entry, Token: EnclosureTokens.get(Entry.Token), EnclosureToken: EnclosureTokens.get(Entry.EnclosureToken) || Group }))];
    const Layers = Definition.Layers.map(Layer => ({ ...structuredClone(Layer), id: IssuePatternToken(), StructureToken: IssuePatternToken(), EnclosureToken: EnclosureTokens.get(Layer.EnclosureToken) || Group, group: EnclosureTokens.get(Layer.group) || Group, LinkedDefinition: Linked ? Token : undefined, LinkedSlot: Linked ? Layer.LinkedSlot : undefined, LinkedInstance: Linked ? Instance : undefined }));
    return { ...Document, Enclosures: [...(Document.Enclosures || []), ...Enclosures], layers: [...Document.layers, ...Layers] };
}

/*============================================================================================================================================
                                                            ARTWORKARRAYS.JS
============================================================================================================================================*/
// 🧩 Bounded editable array copies with deterministic placement, scale envelopes and retained source identities.

import { PatternEnclosureContains, IssuePatternToken } from "./PatternOrganization.js";
import { selectionBounds, newPatternGroup } from "./shapeEditorGeometry.js";
import { NormalizeArtworkNames } from "./ArtworkNames.js";
let ArrayOrdinal = 0;
export function GenerateArtworkArray(Document, Selection, Specification, Limit = 64)
{
    const Indices = [...new Set(Selection)];
    if (!Indices.length || Indices.some(Index => !Document.layers[Index] || Document.layers[Index].locked || !Document.layers[Index].visible)) throw new Error("Select visible, unlocked artwork for an array.");
    const Scalar = (Caption, Minimum, Maximum, Integral = false) =>
    {
        const Quantity = Number(Specification[Caption]);
        if (typeof Specification[Caption] !== "number" || !Number.isFinite(Quantity) || Quantity < Minimum || Quantity > Maximum || (Integral && !Number.isInteger(Quantity))) throw new Error(`Array ${Caption} must be ${Integral ? "an integer " : ""}between ${Minimum} and ${Maximum}.`);
        return Quantity;
    };
    const Category = Specification.Category, ScaleProfile = Specification.ScaleProfile;
    if (!["linear", "grid", "circular", "radial"].includes(Category) || !["uniform", "linear", "circular", "radial", "noise"].includes(ScaleProfile)) throw new Error("Choose a supported array and scale profile.");
    const Columns = Category === "grid" ? Scalar("Columns", 1, 8, true) : Scalar("Count", 2, 64, true);
    const Rows = Category === "grid" ? Scalar("Rows", 1, 8, true) : Category === "radial" ? Scalar("Rings", 1, 8, true) : 1;
    const Population = Columns * Rows;
    if (Population < 2 || Document.layers.length - Indices.length + Population * Indices.length > Limit) throw new Error(`The array exceeds the ${Limit}-entry document limit or has fewer than two copies.`);
    const StartScale = Scalar("StartScale", 1, 400) / 100, EndScale = ScaleProfile === "uniform" ? StartScale : Scalar("EndScale", 1, 400) / 100;
    const Seed = ScaleProfile === "noise" ? Scalar("Seed", 0, 999999, true) : 0;
    const Bounds = selectionBounds(Document, Indices), Locations = [];
    const Circular = Category === "circular" || Category === "radial";
    const CentreX = Circular ? Scalar("CentreX", -512, 1024) : Bounds.x, CentreY = Circular ? Scalar("CentreY", -512, 1024) : Bounds.y;
    if (Circular)
    {
        const Radius = Scalar("Radius", 0, 512), RadiusStep = Category === "radial" ? Scalar("RadiusStep", 1, 512) : 0;
        const StartAngle = Scalar("StartAngle", -360, 360), Sweep = Scalar("Sweep", -360, 360);
        if (Sweep === 0) throw new Error("Array arc sweep cannot be zero.");
        for (let Ring = 0; Ring < Rows; ++Ring)
            for (let Column = 0; Column < Columns; ++Column)
            {
                const Phase = Column / (Math.abs(Sweep) === 360 ? Columns : Columns - 1), Angle = StartAngle + Sweep * Phase;
                const RadiusAtRing = Radius + Ring * RadiusStep, Radians = Angle * Math.PI / 180;
                Locations.push({ X: CentreX + Math.cos(Radians) * RadiusAtRing, Y: CentreY + Math.sin(Radians) * RadiusAtRing, Angle, Phase });
            }
    }
    else
    {
        const StepX = Scalar("StepX", -512, 512), StepY = Scalar("StepY", -512, 512);
        for (let Row = 0; Row < Rows; ++Row)
            for (let Column = 0; Column < Columns; ++Column)
                Locations.push({ X: CentreX + (Column - (Columns - 1) / 2) * StepX, Y: CentreY + (Category === "grid" ? Row - (Rows - 1) / 2 : Column - (Columns - 1) / 2) * StepY, Angle: 0, Phase: Locations.length / Population });
    }
    const Distances = Locations.map(Location => Math.hypot(Location.X - CentreX, Location.Y - CentreY)), MaximumDistance = Math.max(...Distances, 1);
    const CopiedEnclosures = [];
    const Copies = Locations.flatMap((Location, CopyIndex) =>
    {
        let Progress = 0;
        if (ScaleProfile === "linear") Progress = CopyIndex / (Population - 1);
        else if (ScaleProfile === "circular") Progress = .5 - .5 * Math.cos(2 * Math.PI * Location.Phase);
        else if (ScaleProfile === "radial") Progress = Distances[CopyIndex] / MaximumDistance;
        else if (ScaleProfile === "noise")
        {
            let Hash = Math.imul((Seed ^ (CopyIndex + 1)) >>> 0, 0x45d9f3b); Hash = Math.imul(Hash ^ (Hash >>> 16), 0x45d9f3b);
            Progress = ((Hash ^ (Hash >>> 16)) >>> 0) / 4294967295;
        }
        const Scale = StartScale + (EndScale - StartScale) * Progress, Angle = Circular && Specification.RotateCopies ? Location.Angle : 0, Radians = Angle * Math.PI / 180;
        const Groups = new Map(), EnclosureTokens = new Map();
        const Enclosures = (Document.Enclosures || []).filter(Entry => Indices.some(Index => PatternEnclosureContains(Document, Entry.Token, Document.layers[Index].EnclosureToken)));
        for (const Entry of Enclosures) EnclosureTokens.set(Entry.Token, IssuePatternToken());
        for (const Entry of Enclosures)
        {
            CopiedEnclosures.push({ ...Entry, Token: EnclosureTokens.get(Entry.Token), EnclosureToken: EnclosureTokens.get(Entry.EnclosureToken) || "" });
            if (Entry.Category === "group") Groups.set(Entry.Token, EnclosureTokens.get(Entry.Token));
        }
        const Instance = IssuePatternToken();
        return Indices.map(Index =>
        {
            const Source = Document.layers[Index], OffsetX = (Source.x - Bounds.x) * Scale, OffsetY = (Source.y - Bounds.y) * Scale;
            const X = Location.X + OffsetX * Math.cos(Radians) - OffsetY * Math.sin(Radians), Y = Location.Y + OffsetX * Math.sin(Radians) + OffsetY * Math.cos(Radians);
            const Width = Source.width * Scale, Height = Source.height * Scale;
            if (X < -512 || X > 1024 || Y < -512 || Y > 1024 || Width < 1 || Width > 1024 || Height < 1 || Height > 1024) throw new Error("Array copies exceed supported position or size bounds. Reduce spacing, radius or scale.");
            if (Source.group && !Groups.has(Source.group)) Groups.set(Source.group, newPatternGroup());
            return { ...structuredClone(Source), id: globalThis.crypto?.randomUUID?.() || `array-${Date.now().toString(36)}-${++ArrayOrdinal}`, ArraySourceIdentity: Source.id, ...(Source.StructureToken ? { StructureToken: IssuePatternToken() } : {}), ...(Source.EnclosureToken ? { EnclosureToken: EnclosureTokens.get(Source.EnclosureToken) } : {}), ...(Source.LinkedInstance ? { LinkedInstance: Instance } : {}), name: `${Source.name.slice(0, 48)} · ${String(CopyIndex + 1).padStart(2, "0")}`, x: X, y: Y, width: Width, height: Height, rotation: ((Source.rotation + Angle + 180) % 360 + 360) % 360 - 180, ...(Source.group ? { group: Groups.get(Source.group) } : {}) };
        });
    });
    const Selected = new Set(Indices), Retained = Document.layers.filter((_, Index) => !Selected.has(Index));
    return { Document: { ...Document, ...(CopiedEnclosures.length ? { Enclosures: [...(Document.Enclosures || []), ...CopiedEnclosures] } : {}), layers: NormalizeArtworkNames([...Retained, ...Copies]) }, Selection: Copies.map((_, Index) => Retained.length + Index) };
}

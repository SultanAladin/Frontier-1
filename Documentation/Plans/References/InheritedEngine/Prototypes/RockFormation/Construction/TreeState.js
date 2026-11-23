//========================================================================================================================
//                                                  TreeState.js                                                   🧩
//========================================================================================================================
//
// 📝 The authored tree: entries, links, and the slot allocation that maps an entry onto a uniform lane.
//
//    🔴 Slot is NOT the entry identifier. Identifiers are stable for the life of an entry so links keep
//       pointing at the right thing; slots are a compact 0..N index into the uniform array and are
//       reallocated on every structural change. Conflating them means a deleted entry leaves a hole that
//       shifts every downstream entry's dials by one lane — a bug that shows as the wrong dial moving.

import { ConstructionSpecificationTable } from "./ConstructionSpecifications.js";
import { MayConnect } from "./PortCategories.js";

export const SlotCeiling = 48;                                      // [idx] - uniform array length

export function ComposeTreeState()
{
    return {
        Entries      : new Map(),                                   // Identifier -> entry
        Links        : [],                                          // { SourceEntry, TargetEntry, TargetIntake }
        NextIdentifier : 1,
        Revision     : 0                                            // bumped on any structural change
    };
}

// 📝 Dials are stored as a flat array in DIAL ORDER, matching the xyzw packing the shader expects.
export function InsertEntry(TreeState, Species, Position)
{
    const Specification = ConstructionSpecificationTable[Species];
    if (!Specification) throw new Error(`unknown species ${Species}`);

    // 🔴 Singular species (the root) may exist exactly once.
    if (Specification.Singular)
    {
        for (const Existing of TreeState.Entries.values())
        {
            if (Existing.Species === Species) return Existing.Identifier;
        }
    }

    const Identifier = TreeState.NextIdentifier++;
    const Entry =
    {
        Identifier,
        Species,
        Naming   : Specification.Naming,
        Position : { x: Position.x, y: Position.y },
        Dials    : Specification.Dials.map(Dial => Dial[4]),
        Bypassed : false,
        Slot     : 0
    };

    TreeState.Entries.set(Identifier, Entry);
    ReallocateSlots(TreeState);
    return Identifier;
}

export function RemoveEntry(TreeState, Identifier)
{
    const Entry = TreeState.Entries.get(Identifier);
    if (!Entry) return;
    if (ConstructionSpecificationTable[Entry.Species].Singular) return;   // the root is not removable

    TreeState.Entries.delete(Identifier);
    TreeState.Links = TreeState.Links.filter(Link =>
        Link.SourceEntry !== Identifier && Link.TargetEntry !== Identifier);
    ReallocateSlots(TreeState);
}

// 📝 One link per intake — connecting to an occupied intake REPLACES rather than stacking, because an
//    intake is a single operand and two sources would make the transcription ambiguous.
export function InsertLink(TreeState, SourceEntry, TargetEntry, TargetIntake)
{
    if (SourceEntry === TargetEntry) return false;

    const Source = TreeState.Entries.get(SourceEntry);
    const Target = TreeState.Entries.get(TargetEntry);
    if (!Source || !Target) return false;

    const SourceSpecification = ConstructionSpecificationTable[Source.Species];
    const TargetSpecification = ConstructionSpecificationTable[Target.Species];
    const Intake = TargetSpecification.Intakes.find(Candidate => Candidate.Naming === TargetIntake);
    if (!Intake) return false;

    if (!MayConnect(SourceSpecification.Yields, Intake.Category)) return false;

    // 🔴 Cycle check BEFORE inserting. The transcriber has its own guard that bails to 1e9, but a cycle
    //    that reaches the shader is a silently blank render, so it is refused at authoring time instead.
    if (WouldCycle(TreeState, SourceEntry, TargetEntry)) return false;

    TreeState.Links = TreeState.Links.filter(Link =>
        !(Link.TargetEntry === TargetEntry && Link.TargetIntake === TargetIntake));
    TreeState.Links.push({ SourceEntry, TargetEntry, TargetIntake });
    TreeState.Revision++;
    return true;
}

export function RemoveLink(TreeState, TargetEntry, TargetIntake)
{
    const Before = TreeState.Links.length;
    TreeState.Links = TreeState.Links.filter(Link =>
        !(Link.TargetEntry === TargetEntry && Link.TargetIntake === TargetIntake));
    if (TreeState.Links.length !== Before) TreeState.Revision++;
}

// 📝 Walk upstream from the proposed source: if the target is already an ancestor, the link closes a loop.
function WouldCycle(TreeState, SourceEntry, TargetEntry)
{
    const Frontier = [SourceEntry];
    const Seen     = new Set();

    while (Frontier.length > 0)
    {
        const Identifier = Frontier.pop();
        if (Identifier === TargetEntry) return true;
        if (Seen.has(Identifier)) continue;
        Seen.add(Identifier);

        for (const Link of TreeState.Links)
        {
            if (Link.TargetEntry === Identifier) Frontier.push(Link.SourceEntry);
        }
    }
    return false;
}

// 📝 Compact slot assignment in identifier order, so the mapping is deterministic across sessions.
export function ReallocateSlots(TreeState)
{
    let Slot = 0;
    const Ordered = [...TreeState.Entries.keys()].sort((Left, Right) => Left - Right);
    for (const Identifier of Ordered)
    {
        TreeState.Entries.get(Identifier).Slot = Slot++;
    }
    if (Slot > SlotCeiling)
    {
        throw new Error(`${Slot} entries exceeds the ${SlotCeiling}-lane uniform`);
    }
    TreeState.Revision++;
}

export function FindRootIdentifier(TreeState)
{
    for (const [Identifier, Entry] of TreeState.Entries)
    {
        if (Entry.Species === "SurfaceResolve") return Identifier;
    }
    return null;
}

// 📝 A structural fingerprint. The host recompiles when this changes and only writes the uniform when it
//    does not — the whole point of separating topology from dials.
export function ComposeTopologyStamp(TreeState)
{
    const EntryPart = [...TreeState.Entries.values()]
        .sort((Left, Right) => Left.Identifier - Right.Identifier)
        .map(Entry => `${Entry.Identifier}:${Entry.Species}:${Entry.Bypassed ? 1 : 0}:${Entry.Slot}`)
        .join(",");

    const LinkPart = TreeState.Links
        .map(Link => `${Link.SourceEntry}>${Link.TargetEntry}.${Link.TargetIntake}`)
        .sort()
        .join(",");

    return `${EntryPart}|${LinkPart}`;
}

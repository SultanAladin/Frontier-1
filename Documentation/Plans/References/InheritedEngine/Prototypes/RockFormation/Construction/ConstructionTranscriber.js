//========================================================================================================================
//                                         ConstructionTranscriber.js                🧩
//========================================================================================================================
import { ConstructionSpecificationTable } from "./ConstructionSpecifications.js";


//------------------------------------------------------------------------------------------------------------------------
//                                              CONSTRUCTION TRANSCRIBER
//------------------------------------------------------------------------------------------------------------------------

// 📝 Flattens the tree into straight-line WGSL. 🔴 The whole reason for transcribing rather than
//    interpreting: an interpreter would need a stack and dynamic indexing in the innermost loop of the
//    march, which is the single hottest code path in the prototype. Emitting `let` bindings lets the
//    shader compiler inline, constant-fold and reorder freely.
//
//    Topology edit  -> retranscribe + recompile pipeline (tens of ms)
//    Dial turn      -> uniform write only, no recompile (sub-millisecond)
export const ConstructionTranscriber =
{
    // ① Resolve which entry feeds a given intake port.
    ResolveOperand(TreeState, EntryIdentifier, IntakeNaming)
    {
        const Link = TreeState.Links.find(Candidate =>
            Candidate.TargetEntry === EntryIdentifier && Candidate.TargetIntake === IntakeNaming);
        return Link ? Link.SourceEntry : null;
    },

    // ② Depth-first emit with memoisation, so a high-fanout entry is evaluated once and reused.
    EmitEntry(TreeState, EntryIdentifier, Emitted, Lines, Visiting)
    {
        if (Emitted.has(EntryIdentifier)) return Emitted.get(EntryIdentifier);

        // 🔴 Cycle guard. A cycle would recurse until the stack dies, so bail to a far constant.
        if (Visiting.has(EntryIdentifier)) return "1e9";
        Visiting.add(EntryIdentifier);

        const Entry         = TreeState.Entries.get(EntryIdentifier);
        const Specification = ConstructionSpecificationTable[Entry.Species];
        const Operands      = {};

        for (const Intake of Specification.Intakes)
        {
            const SourceIdentifier = this.ResolveOperand(TreeState, EntryIdentifier, Intake.Naming);
            if (SourceIdentifier !== null && TreeState.Entries.has(SourceIdentifier))
            {
                const SourceEntry = TreeState.Entries.get(SourceIdentifier);
                if (!SourceEntry.Bypassed)
                {
                    Operands[Intake.Naming] =
                        this.EmitEntry(TreeState, SourceIdentifier, Emitted, Lines, Visiting);
                }
            }
        }

        Visiting.delete(EntryIdentifier);

        // ③ A bypassed entry forwards its first same-category operand rather than vanishing, which is
        //    what makes bypass useful for A/B comparison instead of just deleting a branch.
        if (Entry.Bypassed)
        {
            const Passthrough = Specification.Intakes.find(Intake =>
                Intake.Category === Specification.Yields && Operands[Intake.Naming]);
            const Forwarded = Passthrough
                ? Operands[Passthrough.Naming]
                : (Specification.Yields === "Warp" ? "Probe"
                :  Specification.Yields === "Resistance" ? "0.5"
                :  Specification.Yields === "Tint" ? "vec3f(0.5)" : "1e9");
            Emitted.set(EntryIdentifier, Forwarded);
            return Forwarded;
        }

        const Expression = Specification.Transcribe(Operands, Entry.Dials, Entry.Slot);
        const Binding    = `Value${EntryIdentifier}`;
        const TypeNaming = Specification.Yields === "Warp" ? "vec3f"
                         : Specification.Yields === "Tint" ? "vec3f" : "f32";

        Lines.push(`    let ${Binding} : ${TypeNaming} = ${Expression};`);
        Emitted.set(EntryIdentifier, Binding);
        return Binding;
    },

    // ④ Emit the three tree entry points the resolve half expects.
    Transcribe(TreeState)
    {
        const RootIdentifier = [...TreeState.Entries.keys()].find(Identifier =>
            TreeState.Entries.get(Identifier).Species === "SurfaceResolve");

        if (RootIdentifier === undefined)
        {
            return {
                Source: this.ComposeFallback("no Surface Resolve entry"),
                LineTally: 0
            };
        }

        // 📝 The root's shape intake is "mass" — the SEED body, not the finished rock. Renamed from
        //    "distance" when the erosion sim landed, because what arrives here is no longer what renders.
        const DistanceSource = this.ResolveOperand(TreeState, RootIdentifier, "mass");
        const TintSource     = this.ResolveOperand(TreeState, RootIdentifier, "tint");

        if (DistanceSource === null || !TreeState.Entries.has(DistanceSource))
        {
            return {
                Source: this.ComposeFallback("Surface Resolve has no mass operand"),
                LineTally: 0
            };
        }

        // ⑤ Distance body.
        const DistanceLines = [];
        const DistanceBinding = this.EmitEntry(
            TreeState, DistanceSource, new Map(), DistanceLines, new Set());

        // ⑥ Resistance body.
        //
        //    🔴 An EXPLICIT wire to the root's resist intake wins over the discovered one. Resistance is
        //       no longer decoration: it is the sole reason the eroded shape has the form it has, so it
        //       must be authorable directly rather than inferred from whatever the shape branch happens
        //       to consume. Discovery stays as the fallback for a tree that has not wired it.
        const ExplicitResistance = this.ResolveOperand(TreeState, RootIdentifier, "resist");
        const ResistanceIdentifier =
            (ExplicitResistance !== null && TreeState.Entries.has(ExplicitResistance))
                ? ExplicitResistance
                : this.DiscoverResistanceSource(TreeState, DistanceSource);

        const ResistanceLines = [];
        let ResistanceBinding = "0.5";
        if (ResistanceIdentifier !== null)
        {
            ResistanceBinding = this.EmitEntry(
                TreeState, ResistanceIdentifier, new Map(), ResistanceLines, new Set());
        }

        // ⑦ Tint body.
        const TintLines = [];
        let TintBinding = null;
        if (TintSource !== null && TreeState.Entries.has(TintSource))
        {
            TintBinding = this.EmitEntry(TreeState, TintSource, new Map(), TintLines, new Set());
        }

        const TintBody = TintBinding !== null
            ? `${TintLines.join("\n")}\n    return ${TintBinding};`
            : `${ResistanceLines.join("\n")}\n    return EvaluateStratumTint(Probe, ${ResistanceBinding}, vec4f(0.0, 1.0, 0.34, 0.42));`;

        const Source =
`
fn EvaluateConstructionTree(Probe : vec3f) -> f32
{
${DistanceLines.join("\n")}
    return ${DistanceBinding};
}

fn EvaluateConstructionResistance(Probe : vec3f) -> f32
{
${ResistanceLines.join("\n")}
    return ${ResistanceBinding};
}

fn EvaluateConstructionTint(Probe : vec3f) -> vec3f
{
${TintBody}
}
`;
        return {
            Source,
            LineTally: DistanceLines.length + ResistanceLines.length + TintLines.length
        };
    },

    // 📝 Transcribe ONE entry as the whole subject, for the per-card preview. Same EmitEntry walk, a
    //    different root — a preview is "what does the tree look like if it stopped here".
    //
    //    🔴 An entry that does not yield Distance has no silhouette of its own. Rather than refuse, the
    //       preview shows that value PAINTED ON a reference block: a resistance field or a tint is a
    //       function of position, so it is only visible on some surface. Showing an empty frame instead
    //       would read as "this entry is broken" when it is working exactly as designed.
    TranscribeEntry(TreeState, EntryIdentifier)
    {
        if (!TreeState.Entries.has(EntryIdentifier))
        {
            return { Source: this.ComposeFallback("no such entry"), LineTally: 0, Painted: false };
        }

        const Entry = TreeState.Entries.get(EntryIdentifier);
        const Specification = ConstructionSpecificationTable[Entry.Species];
        const Yields = Specification.Yields;

        // 🔴 A Weather entry is a DISPATCH, not a function of position — EmitEntry has no expression to
        //    emit for it. Previewing one means previewing the erosion it performs, which is the live
        //    viewport, not a thumbnail. Refuse here rather than emitting a broken shader.
        if (Yields === "Weather" || Specification.Family === "Resolve")
        {
            return { Source: null, LineTally: 0, Painted: false, Refused: Yields === "Weather"
                ? "a weather process has no shape of its own — watch it in the viewport"
                : "the resolve root IS the viewport" };
        }

        const Lines = [];
        const Binding = this.EmitEntry(TreeState, EntryIdentifier, new Map(), Lines, new Set());
        const Shaped = Yields === "Distance";

        // 📝 The reference block is the domain cube the sim seeds from, so a resistance preview shows the
        //    field on the same body the erosion will actually read it on.
        const DistanceBody = Shaped
            ? `${Lines.join("\n")}\n    return ${Binding};`
            : `    return DistanceToRoundedBox(Probe, vec3f(1.5), 0.12);`;

        // 📝 Scalars land in resistance so the resistance resolve mode shows them; warp and tint have no
        //    scalar reading, so they hold the neutral 0.5 and are seen through the tint body instead.
        const ResistanceBody = (Yields === "Resistance" || Yields === "Scalar")
            ? `${Lines.join("\n")}\n    return clamp(${Binding}, 0.0, 1.0);`
            : `    return 0.5;`;

        let TintBody;
        if (Yields === "Tint")
        {
            TintBody = `${Lines.join("\n")}\n    return ${Binding};`;
        }
        else if (Yields === "Warp")
        {
            // 📝 A warp yields a displaced POSITION, so show the displacement itself as colour — the
            //    difference from the undisplaced probe, centred. A warp is otherwise invisible alone.
            TintBody = `${Lines.join("\n")}\n    return clamp((${Binding} - Probe) * 1.6 + vec3f(0.5), vec3f(0.0), vec3f(1.0));`;
        }
        else if (Yields === "Resistance" || Yields === "Scalar")
        {
            TintBody = `${Lines.join("\n")}\n    return mix(vec3f(0.24, 0.30, 0.42), vec3f(0.94, 0.86, 0.68), clamp(${Binding}, 0.0, 1.0));`;
        }
        else
        {
            TintBody = `    return EvaluateStratumTint(Probe, 0.5, vec4f(0.0, 1.0, 0.34, 0.42));`;
        }

        const Source =
`
fn EvaluateConstructionTree(Probe : vec3f) -> f32
{
${DistanceBody}
}

fn EvaluateConstructionResistance(Probe : vec3f) -> f32
{
${ResistanceBody}
}

fn EvaluateConstructionTint(Probe : vec3f) -> vec3f
{
${TintBody}
}
`;
        return { Source, LineTally: Lines.length, Painted: !Shaped };
    },

    // 📝 Find the resistance field the shape branch actually consumes, for the resistance resolve view and
    //    for tint when nothing is wired to the tint intake.
    //
    //    🔴 Prefers the resistance entry NEAREST the shape branch, i.e. the smallest upstream depth. That
    //       is the fully-composed field: in the seed tree StratumBand feeds JointNetwork, so JointNetwork
    //       is what the carve reads and StratumBand is only a partial input to it.
    //
    //       An earlier form kept whichever entry a stack-based walk popped LAST and called it "deepest".
    //       Stack pop order is not depth, so it returned StratumBand — the resistance view and the tint
    //       then showed bare bedding with no joints, which looks plausible and is wrong. Depth is now
    //       measured rather than inferred from traversal order.
    DiscoverResistanceSource(TreeState, StartIdentifier)
    {
        const Frontier = [{ Identifier: StartIdentifier, Depth: 0 }];
        const Seen     = new Set();
        let   Nearest      = null;
        let   NearestDepth = Infinity;

        while (Frontier.length > 0)
        {
            const { Identifier, Depth } = Frontier.shift();          // breadth-first, so depth is honest
            if (Seen.has(Identifier)) continue;
            Seen.add(Identifier);
            if (!TreeState.Entries.has(Identifier)) continue;

            const Entry = TreeState.Entries.get(Identifier);
            const Specification = ConstructionSpecificationTable[Entry.Species];

            if (Specification.Yields === "Resistance" && !Entry.Bypassed && Depth < NearestDepth)
            {
                Nearest      = Identifier;
                NearestDepth = Depth;
            }

            for (const Link of TreeState.Links)
            {
                if (Link.TargetEntry === Identifier)
                {
                    Frontier.push({ Identifier: Link.SourceEntry, Depth: Depth + 1 });
                }
            }
        }
        return Nearest;
    },

    // 📝 A visible fallback rather than a compile failure, so an incomplete tree still renders and the
    //    reason is legible on screen instead of only in the console.
    ComposeFallback(Reason)
    {
        return `
// fallback — ${Reason}
fn EvaluateConstructionTree(Probe : vec3f) -> f32
{
    return DistanceToSphere(Probe, 1.6);
}

fn EvaluateConstructionResistance(Probe : vec3f) -> f32
{
    return EvaluateStratumBand(Probe, 0.5, vec4f(0.62, 0.07, 0.62, 0.0));
}

fn EvaluateConstructionTint(Probe : vec3f) -> vec3f
{
    return vec3f(0.30, 0.31, 0.34);
}
`;
    }
};

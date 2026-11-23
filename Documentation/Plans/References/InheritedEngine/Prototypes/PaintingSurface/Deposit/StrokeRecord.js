/*====================================================================================================================================
                                                      STROKERECORD.JS
====================================================================================================================================*/
// 🧩 Persist strokes as object-space dabs with raw pressure, so a stroke can be replayed at any resolution

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// Provenance of a recorded sample. 📝 Predicted samples must never be committed to the record —
//    they are speculative positions that the real input stream will contradict.
export const SampleProvenance = {
    Measured:  "measured",
    Predicted: "predicted",
    Replayed:  "replayed"
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// A single stroke: the brush it was laid with, and the dabs it emitted.
//
// 🔴 Two rules from the research govern this structure, and both are easy to violate silently:
//    1. Everything spatial is in OBJECT space, never device pixels. A pixel-space record cannot be
//       replayed at another viewport size, zoom, or export resolution.
//    2. Pressure is stored RAW. The response curve is presentational and is applied at render time,
//       so baking it in makes the stroke unreplayable under a different curve.
export class StrokeRecord
{
    constructor(Brush, Ordinal, Context)
    {
        this.Ordinal = Ordinal;

        // A copy, not a reference — the live brush keeps changing after the stroke is committed.
        //
        // 🔴 This block is what ReplayInto consumes, so it stays exactly as the pass wants it even though
        //    `Instrument` below records the same settings more faithfully. The two are not redundant: this
        //    is the COLLAPSED form the paint pass can execute, and it must keep working for records written
        //    before the instrument was captured. Dropping it in favour of the richer block would make every
        //    existing stroke unreplayable.
        this.Brush = {
            Radius:       Brush.Radius,
            Hardness:     Brush.Hardness,
            Flow:         Brush.Flow,
            Spacing:      Brush.Spacing,
            NormalCutoff: Brush.NormalCutoff,
            Ink:          [Brush.Ink[0], Brush.Ink[1], Brush.Ink[2]],
            Erase:        Brush.Erase === true
        };

        // ---- HOW it was laid: the authored radius and the view that resolved it ------------------------
        // 🔴 `Brush.Radius` above is in OBJECT units, which is what makes the record replayable — but it was
        //    derived from a pixel radius through the camera distance, field of view and canvas height that
        //    happened to be live at stroke open, and none of those survive in it. So the authored intent
        //    ("34 px at that zoom") is unrecoverable from the object radius alone, and a reconstruction that
        //    wants to re-derive it (or to show the user the size they actually picked) has nothing to read.
        // 🔴 Recorded as provenance, NOT as the replay input. Replay must keep using the object radius:
        //    re-deriving from PixelRadius at replay time would make the stroke change width with the
        //    camera, which is the exact failure the object-space rule exists to prevent.
        this.Authored = Context?.Authored ? {
            PixelRadius:   Context.Authored.PixelRadius   ?? null,
            Distance:      Context.Authored.Distance      ?? null,
            VerticalField: Context.Authored.VerticalField  ?? null,
            SurfaceHeight: Context.Authored.SurfaceHeight ?? null
        } : null;

        // ---- WHICH TOOL made it -------------------------------------------------------------------------
        // 🔴 The whole point of this block. ApplyToBrush collapses a chosen instrument into four numbers
        //    (hardness, spacing, flow, ink) and throws the rest away — the instrument's identity and every
        //    rich parameter (grade, nib, bristle, wetness, grain, scatter, blend, taper, tilt, smoothing,
        //    pressure-sensitivity). Those are the settings a reconstruction needs, and they were being lost
        //    at the wiring seam rather than anywhere a reader would look for them.
        // 🔴 `Params` is DEEP-copied via the spread, not referenced. ToolMenu keeps one live params object
        //    per instrument in `Store` and mutates it in place as sliders move, so a reference would make
        //    every historic stroke of that instrument report today's slider positions.
        // 🔴 `Wired` names the parameters that actually reached the paint pass. Without it a reader cannot
        //    tell a setting that shaped these dabs from one that was recorded but had nowhere to go, and a
        //    future replay would silently apply a parameter this stroke was never drawn with.
        this.Instrument = Context?.Instrument ? {
            Key:     Context.Instrument.Key    ?? null,
            Name:    Context.Instrument.Name   ?? null,
            Label:   Context.Instrument.Label  ?? null,
            Schema:  Context.Instrument.Schema ?? null,
            Tone:    Context.Instrument.Tone   ?? null,
            Swatch:  Context.Instrument.Swatch ?? null,
            Params:  { ...(Context.Instrument.Params ?? {}) },
            Wired:   Array.isArray(Context.Instrument.Wired) ? [...Context.Instrument.Wired] : []
        } : null;

        // ---- WHERE the stroke landed --------------------------------------------------------------------
        // 🔴 Captured at Begin, NOT read back when the history list is drawn. A stroke belongs to the layer
        //    and channels it was laid into at the time; by the time anything displays it the user may have
        //    refocused, renamed, retargeted or deleted that layer. Resolving late would relabel historic
        //    entries every time focus moved — the list would rewrite its own past.
        // 🔴 The layer NAME is copied alongside the token for the same reason a brush copy is taken above:
        //    the token is the durable identity, but a renamed or deleted layer leaves the token unresolvable
        //    and the entry would have nothing to show. The name is a snapshot caption, the token is the link.
        this.Target = {
            Token:      Context?.Token      ?? null,
            Name:       Context?.Name       ?? null,
            Channels:   Array.isArray(Context?.Channels) ? [...Context.Channels] : [],
            MaskToken:  Context?.MaskToken  ?? null,
            // 📝 A mask stroke carves where the layer applies rather than painting its channels, so it is
            //    flagged explicitly instead of being inferred from an empty channel list — a channel-less
            //    paint stroke (every channel disabled) would otherwise be indistinguishable from one.
            IsMask:     Boolean(Context?.MaskToken)
        };

        // ---- WHEN it happened ---------------------------------------------------------------------------
        // 🔴 performance.now(), not Date.now(): this is a DURATION source as well as an ordering one, and a
        //    wall clock can step backwards (NTP correction, DST) which would make a stroke end before it
        //    began. Wall-clock time is not recorded at all — nothing here needs it, and it would be the one
        //    field that makes an otherwise resolution- and machine-independent record non-reproducible.
        this.StartedAt = (typeof performance !== "undefined" ? performance.now() : 0);
        this.EndedAt   = null;

        this.Dabs      = [];
        this.Provenance = SampleProvenance.Measured;
    }

    Append(Dab)
    {
        this.Dabs.push({
            Position:   [Dab.Position[0], Dab.Position[1], Dab.Position[2]],
            Normal:     [Dab.Normal[0],   Dab.Normal[1],   Dab.Normal[2]],
            Coordinate: [Dab.Coordinate[0], Dab.Coordinate[1]],
            Pressure:   Dab.Pressure ?? 1.0
        });
    }

    // Close the record. Idempotent, because Finish can be reached twice (pointerup and pointercancel).
    Close()
    {
        if (this.EndedAt === null)
        {
            this.EndedAt = (typeof performance !== "undefined" ? performance.now() : 0);
        }
    }

    get DabCount() { return this.Dabs.length; }

    // The stroke's colour, as the ink that was actually deposited plus a hex form for a swatch chip.
    //
    // 🔴 Derived from `Brush.Ink` — the value the pass deposited — and NOT from `Instrument.Swatch`. The two
    //    can legitimately disagree: the swatch is what the user clicked in the menu, but the ink can be set
    //    by an eyedropper or a direct Brush.Ink write that never touches ToolMenu, and on those strokes the
    //    stored swatch is stale. The deposited ink is the truth about what the stroke looks like.
    // 🔴 An ERASE stroke reports no colour. Its ink is whatever happened to be loaded when erase was
    //    enabled, and the pass ignores it entirely — showing it would put a confident red chip next to a
    //    stroke that removed paint.
    get Colour()
    {
        if (this.Brush.Erase) { return { Erase: true, Ink: null, Hex: null }; }

        const Ink  = this.Brush.Ink;
        // 📝 Straight ×255 with no encode, matching ToolMenu.ParseColour's straight ÷255 decode. Applying a
        //    gamma here would make the history chip disagree with the swatch the stroke was picked from.
        const Byte = (Value) => Math.max(0, Math.min(255, Math.round(Value * 255)));
        const Hex  = "#" + [Ink[0], Ink[1], Ink[2]].map((V) => Byte(V).toString(16).padStart(2, "0")).join("");

        return { Erase: false, Ink: [Ink[0], Ink[1], Ink[2]], Hex };
    }

    // 🔴 Duration falls back to 0 rather than to "now minus start" on an unclosed stroke. A live stroke read
    //    mid-drag would otherwise report a duration that grows every time the list is drawn, and the in-progress
    //    entry would visibly tick — which reads as a rendering fault rather than as an open stroke.
    get Duration() { return this.EndedAt === null ? 0 : this.EndedAt - this.StartedAt; }

    // Pressure statistics over the recorded dabs.
    //
    // 🔴 DERIVED on read, never stored. Keeping running min/max/mean fields would mean Append had to update
    //    them, and any future path that pushes into `Dabs` directly (a deserializer, a replay, a repair)
    //    would leave the statistics describing a different stroke than the one held — a divergence nothing
    //    would surface, because both halves would look individually plausible.
    // 🔴 Reports RAW pressure, matching what is stored. Curving it here would make the number disagree with
    //    the dabs it summarises, and the curve is presentational (see the class comment).
    get PressureSpan()
    {
        if (this.Dabs.length === 0) { return { Low: 0, High: 0, Mean: 0 }; }

        let Low  = Infinity;
        let High = -Infinity;
        let Sum  = 0;

        for (const Dab of this.Dabs)
        {
            const Value = Dab.Pressure ?? 1.0;
            if (Value < Low)  { Low  = Value; }
            if (Value > High) { High = Value; }
            Sum += Value;
        }

        return { Low, High, Mean: Sum / this.Dabs.length };
    }
}

// The whole painting session: an ordered run of strokes, plus a cursor for undo and redo.
//
// 📝 Undo moves the cursor rather than deleting, so redo is a cursor move too. Replay from the base
//    atlas is what actually reconstitutes the image — see ReplayInto below.
export class StrokeLedger
{
    constructor()
    {
        this.Strokes = [];
        this.Cursor  = 0;          // How many strokes are currently applied.
        this.Sequence = 0;
    }

    // `Context` describes WHERE the stroke is being laid — the layer, its name, the channels enabled for it,
    // and the mask component if one is the paint target. Optional, so a caller with no layer notion (a bare
    // atlas test) still records strokes; the metadata is then simply absent rather than wrong.
    Begin(Brush, Context)
    {
        // 🔴 Recording behind the cursor discards the redo tail. Without this, an undo followed by a
        //    new stroke would leave orphaned strokes that replay would resurrect.
        if (this.Cursor < this.Strokes.length) { this.Strokes.length = this.Cursor; }

        this.Sequence += 1;
        const Stroke = new StrokeRecord(Brush, this.Sequence, Context);

        this.Strokes.push(Stroke);
        this.Cursor = this.Strokes.length;

        return Stroke;
    }

    Undo()
    {
        if (this.Cursor <= 0) { return false; }
        this.Cursor -= 1;
        return true;
    }

    Redo()
    {
        if (this.Cursor >= this.Strokes.length) { return false; }
        this.Cursor += 1;
        return true;
    }

    // The strokes that are currently live, in the order they were laid.
    ActiveStrokes()
    {
        return this.Strokes.slice(0, this.Cursor);
    }

    get StrokeCount()   { return this.Strokes.length; }
    get ActiveCount()   { return this.Cursor; }
    get TotalDabCount() { return this.Strokes.reduce((Sum, Stroke) => Sum + Stroke.DabCount, 0); }

    // Serialize to a plain object. 📝 Nothing here is resolution-bound, which is the whole point of
    //    the record: the same document replays into a 512² or an 8K atlas.
    Describe()
    {
        return {
            Sequence: this.Sequence,
            Cursor:   this.Cursor,
            Strokes:  this.Strokes.map(Stroke => ({
                Ordinal:  Stroke.Ordinal,
                Brush:    Stroke.Brush,
                Instrument: Stroke.Instrument,
                Authored: Stroke.Authored,
                Colour:   Stroke.Colour,
                Target:   Stroke.Target,
                Pressure: Stroke.PressureSpan,
                Duration: Stroke.Duration,
                Dabs:     Stroke.Dabs
            }))
        };
    }

    // The history list's view: one entry per stroke, newest FIRST, without the dab payload.
    //
    // 🔴 Deliberately omits `Dabs`. A stroke holds hundreds of them and a UI listing needs none — including
    //    them would make every history redraw copy the entire painting session. Describe() above is the
    //    serialization path and still carries them.
    // 🔴 `Live` is computed against the cursor rather than stored on the record, because undo moves the
    //    cursor and never touches the strokes. A stored flag would need every entry rewritten on each undo.
    Entries()
    {
        return this.Strokes.map((Stroke, Index) => ({
            Ordinal:  Stroke.Ordinal,
            Live:     Index < this.Cursor,
            Target:   Stroke.Target,
            Brush:    Stroke.Brush,
            // 📝 The tool and the colour ride the LIST view, not just Describe(): naming the instrument and
            //    showing its colour is the whole reason a painter can find a stroke in a history of hundreds.
            Instrument: Stroke.Instrument,
            Colour:   Stroke.Colour,
            DabCount: Stroke.DabCount,
            Pressure: Stroke.PressureSpan,
            Duration: Stroke.Duration
        })).reverse();
    }
}

// Replay the live strokes into the LAYERS they were laid in, fanning each across its channel atlases.
//
// 🔴 The layer-aware sibling of ReplayInto below, and the one an export at another resolution needs.
//    ReplayInto targets ONE atlas view, which is the pre-layer model: it cannot reproduce a stack,
//    because every stroke would land in the same texture regardless of which layer owns it. This routes
//    each stroke by its recorded `Target.Token` instead.
//
// 🔴 Strokes are replayed in LEDGER order, not grouped by layer. Grouping would be faster (fewer atlas
//    switches) but wrong: within a layer, later paint covers earlier paint, and re-ordering across layers
//    changes nothing while re-ordering within one changes the image. Ledger order is the only safe order,
//    and it already visits each layer's strokes in the sequence they were laid.
//
// 🔴 A stroke whose target layer no longer exists is SKIPPED and counted, never redirected to the focused
//    layer. Its dabs describe paint on a layer the user deleted; putting them anywhere else would make an
//    export contain strokes the document does not. The tally is returned so the caller can report it
//    rather than silently producing a thinner image than the one on screen.
//
// 🔴 Mask strokes are skipped here too. They carve where a layer applies rather than depositing channel
//    values, and their dabs belong in the mask component's own greyscale atlas — a different target and a
//    different write. Replaying them into the channel atlases would stamp brush ink wherever the user had
//    masked. Reproducing masks at export resolution needs the mask sequence and is left to the caller.
export function ReplayIntoLayers(Device, Pass, Stack, Surface, Ledger, ResolveAtlasWrite, ChannelAtlases)
{
    const Strokes  = Ledger.ActiveStrokes();
    let   Laid     = 0;
    let   Orphaned = 0;
    let   Masked   = 0;

    for (const Stroke of Strokes)
    {
        if (Stroke.Target?.IsMask) { Masked += 1; continue; }

        const Token = Stroke.Target?.Token ?? null;
        const Layer = Token === null ? null : Stack.Resolve(Token);

        if (!Layer || Layer.Paintable === false) { Orphaned += 1; continue; }

        // 🔴 The stroke's OWN recorded channel list, not the layer's current `Enabled` set. The user may have
        //    toggled a channel on or off since the stroke was laid, and honouring today's set would repaint
        //    history — a channel enabled after the fact would gain paint that never existed there.
        const Channels = new Set(Stroke.Target.Channels ?? []);
        if (Channels.size === 0) { continue; }

        for (let Start = 0; Start < Stroke.Dabs.length; Start += 256)
        {
            const Batch = Stroke.Dabs.slice(Start, Start + 256);
            // 🔴 Counted on the first atlas that actually DRAWS, not on ChannelAtlases[0]. Keying the tally
            //    to a fixed atlas undercounts every stroke that does not touch it: a roughness-only stroke
            //    lives in Material, so Colour resolves to null, is skipped, and the count never fires — the
            //    export draws correctly but reports 0 dabs laid, which reads as a blank export.
            let Counted = false;

            for (const Descriptor of ChannelAtlases)
            {
                // 📝 The layer's authored Values still supply the non-painted channels' constants; only the
                //    enabled SET is taken from the stroke. Values are not versioned per stroke, so this is
                //    the closest faithful reproduction available.
                const Write = ResolveAtlasWrite(
                    Descriptor.Key, Channels, Layer.Values, Stroke.Brush.Ink);
                if (Write === null) { continue; }

                const View = Layer.EnsureAtlas
                    ? Layer.EnsureAtlas(Descriptor.Key)
                    : Layer.AtlasView[Descriptor.Key];
                if (!View) { continue; }

                const Count   = Pass.StageDabs(Batch, Stroke.Brush, Write);
                const Encoder = Device.createCommandEncoder({ label: `LayerReplay${Descriptor.Key}` });

                Pass.Encode(Encoder, View, Surface, Count, Write);
                Device.queue.submit([Encoder.finish()]);

                // Once per dab, never once per (dab x atlas): a stroke into three atlases is still one
                // stroke's worth of paint, and tripling it would misreport the export.
                if (!Counted) { Laid += Count; Counted = true; }
            }
        }
    }

    return { Laid, Orphaned, Masked, Strokes: Strokes.length };
}

// Replay the live strokes into the atlas through a paint pass.
//
// 📝 Replay is what makes undo correct: rather than storing pixels, the atlas is reset to its base
//    and every surviving stroke is re-laid. The cost is bounded by stroke count, and the result is
//    exact rather than an approximation of the pixels that were overwritten.
export function ReplayInto(Device, Pass, AtlasView, Surface, Ledger)
{
    const Strokes = Ledger.ActiveStrokes();
    let   Laid    = 0;

    for (const Stroke of Strokes)
    {
        for (let Start = 0; Start < Stroke.Dabs.length; Start += 256)
        {
            const Batch   = Stroke.Dabs.slice(Start, Start + 256);
            const Count   = Pass.StageDabs(Batch, Stroke.Brush);
            const Encoder = Device.createCommandEncoder({ label: "StrokeReplay" });

            Pass.Encode(Encoder, AtlasView, Surface, Count);
            Device.queue.submit([Encoder.finish()]);

            Laid += Count;
        }
    }

    return Laid;
}

//========================================================================================================================
//                                                 TreeSurface.js                                                  🧩
//========================================================================================================================
//
// 📝 The authoring surface: entry cards as DOM, links as one SVG layer beneath them, pan/zoom on the
//    container, drag to move, drag from a stud to connect.
//
//    🔴 Cards are DOM rather than canvas-drawn because the reference editor's look depends on real CSS
//       (backdrop-filter blur, border-radius, box-shadow) that would have to be reimplemented by hand on
//       a canvas. Links ARE canvas-free SVG because a bezier per link is cheaper to keep in sync than
//       redrawing, and because an SVG path can carry the port colour directly.
//
//    Transform model: one CSS transform on a single Viewport element. Every card is positioned in TREE
//    space and never touched during a pan, so panning is one style write regardless of entry count.

import { ConstructionSpecificationTable, TintPaletteNaming, RepeatAxisNaming }
    from "../Construction/ConstructionSpecifications.js";
import { PortCategories, MayConnect } from "../Construction/PortCategories.js";
import { InsertLink, RemoveLink, RemoveEntry } from "../Construction/TreeState.js";
import { ComposeEntryDialRow } from "./PanelDials.js";

const StudSpacing  = 22;                                            // [px] - vertical gap between studs
const StudInset    = 34;                                            // [px] - first stud below the card head
const ZoomFloor    = 0.28;
const ZoomCeiling  = 2.20;

// 📝 Thumbnails are square and small on purpose: one preview per card at 96 px is 9216 pixels, so a
//    twelve-card tree costs about a tenth of one 1080p viewport frame to repaint in full.
//
//    🔴 Named ThumbnailEdge, NOT PreviewEdge. VoxelField already exports a PreviewEdge — 128 CELLS of
//       simulation grid — and the entry point imports from both modules. Two same-named exports meaning
//       different things in different units is a collision waiting to be imported into one scope, and
//       seeding a 96³ grid or marching a 128 px thumbnail would both look like plausible bugs elsewhere.
export const ThumbnailEdge = 96;                                    // [px]

export function ComposeTreeSurface(Host, TreeState, Notify)
{
    const Surface =
    {
        Host,
        TreeState,
        Notify,                                                     // called on any structural change
        Viewport   : Host.querySelector("#TreeViewport"),
        LinkLayer  : Host.querySelector("#LinkLayer"),
        CardLayer  : Host.querySelector("#CardLayer"),
        Pan        : { x: 0, y: 0 },
        Zoom       : 0.82,
        Cards      : new Map(),                                     // Identifier -> element
        Previews   : new Map(),                                     // Identifier -> canvas
        Engaged    : null,                                           // selected entry identifier
        Dragging   : null,
        Connecting : null,
        OnEngage   : null,                                           // set by the host; engaged changed
        OnPreview  : null                                            // set by the host; repaint one card
    };

    AttachSurfaceGestures(Surface);
    return Surface;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDER
//------------------------------------------------------------------------------------------------------------------------

export function RebuildSurface(Surface)
{
    Surface.CardLayer.textContent = "";
    Surface.Cards.clear();
    Surface.Previews.clear();

    for (const Entry of Surface.TreeState.Entries.values())
    {
        const Card = ComposeCard(Surface, Entry);
        Surface.CardLayer.appendChild(Card);
        Surface.Cards.set(Entry.Identifier, Card);
    }
    RedrawLinks(Surface);
    ApplyTransform(Surface);
}

function ComposeCard(Surface, Entry)
{
    const Specification = ConstructionSpecificationTable[Entry.Species];

    const Card = document.createElement("div");
    Card.className = "ConstructionEntry";
    Card.dataset.identifier = Entry.Identifier;
    Card.style.left = `${Entry.Position.x}px`;
    Card.style.top  = `${Entry.Position.y}px`;
    if (Entry.Bypassed) Card.classList.add("Bypassed");
    if (Surface.Engaged === Entry.Identifier) Card.classList.add("Engaged");

    // ① The hover toolstrip — bypass and remove, matching the reference editor's floating pill.
    const Tools = document.createElement("div");
    Tools.className = "EntryTools";

    if (!Specification.Singular)
    {
        Tools.appendChild(ComposeTool("Bypass", Entry.Bypassed ? "on" : "", () =>
        {
            Entry.Bypassed = !Entry.Bypassed;
            Surface.TreeState.Revision++;
            RebuildSurface(Surface);
            Surface.Notify("topology");
        }));

        Tools.appendChild(ComposeTool("Remove", "bad", () =>
        {
            RemoveEntry(Surface.TreeState, Entry.Identifier);
            if (Surface.Engaged === Entry.Identifier) Surface.Engaged = null;
            RebuildSurface(Surface);
            Surface.Notify("topology");
        }));
    }
    Card.appendChild(Tools);

    // ② Head: family swatch, naming, and the yield category as a right-aligned tag.
    const Head = document.createElement("div");
    Head.className = `EntryHead Family${Specification.Family}`;

    const Glyph = document.createElement("span");
    Glyph.className = "EntryGlyph";
    Glyph.textContent = Specification.Glyph;
    Head.appendChild(Glyph);

    const Naming = document.createElement("span");
    Naming.className = "EntryNaming";
    Naming.textContent = Specification.Naming;
    Head.appendChild(Naming);

    const Yield = document.createElement("span");
    Yield.className = "EntryYield";
    Yield.style.color = PortCategories[Specification.Yields].Swatch;
    Yield.textContent = Specification.Yields.slice(0, 4).toLowerCase();
    Head.appendChild(Yield);

    Card.appendChild(Head);

    // ③ Body: one row per intake, then a compact dial readout.
    const Body = document.createElement("div");
    Body.className = "EntryBody";

    for (const Intake of Specification.Intakes)
    {
        const Row = document.createElement("div");
        Row.className = "IntakeRow";

        const Label = document.createElement("span");
        Label.className = "IntakeNaming";
        Label.textContent = Intake.Naming;
        if (Intake.Optional) Label.classList.add("Optional");
        Row.appendChild(Label);

        Body.appendChild(Row);
    }

    // ④ Live dials, on the card. The side panel is gone — the viewport and the tree are 50/50.
    //
    //    🔴 A slider drag competes with the card-move drag: pointerdown anywhere in a card starts the
    //       move. ComposeEntryDialRow stops propagation on both pointerdown and click, which is the only
    //       reason this is safe. Do not build a card slider by hand.
    //
    //    📝 Collapsed to a summary until the card is engaged. Every card showing every slider at once
    //       turns the tree into a wall of controls and defeats the point of cards.
    if (Specification.Dials.length > 0)
    {
        if (Surface.Engaged === Entry.Identifier)
        {
            const Rack = document.createElement("div");
            Rack.className = "DialRack";
            Specification.Dials.forEach((Dial, Lane) =>
            {
                Rack.appendChild(ComposeEntryDialRow(Entry, Specification, Dial, Lane, () =>
                {
                    Surface.Notify("dial");
                }));
            });
            Body.appendChild(Rack);
        }
        else
        {
            const Readout = document.createElement("div");
            Readout.className = "DialReadout";
            Readout.appendChild(ComposeDialSummary(Entry, Specification));
            Body.appendChild(Readout);
        }
    }

    // ⑤ Preview mount. The host paints into this canvas; the surface only reserves the space, so a
    //    rebuild never has to know how a preview is rendered.
    if (Specification.Family !== "Resolve")
    {
        const Preview = document.createElement("canvas");
        Preview.className = "EntryPreview";
        Preview.width  = ThumbnailEdge;
        Preview.height = ThumbnailEdge;
        Preview.dataset.identifier = Entry.Identifier;
        Preview.title = `preview of ${Specification.Naming}`;

        // 📝 A click on the preview asks the host to repaint just this entry, which is the escape hatch
        //    when a stale thumbnail survives an edit the host did not notice.
        Preview.addEventListener("pointerdown", Event => Event.stopPropagation());
        Preview.addEventListener("click", Event =>
        {
            Event.stopPropagation();
            if (Surface.OnPreview) Surface.OnPreview(Entry.Identifier, Preview);
        });

        Body.appendChild(Preview);
        Surface.Previews.set(Entry.Identifier, Preview);
    }

    Card.appendChild(Body);

    // ④ Studs, positioned to line up with the intake rows and the single yield.
    Specification.Intakes.forEach((Intake, Index) =>
    {
        const Stud = document.createElement("div");
        Stud.className = "PortStud Intake";
        Stud.style.background = PortCategories[Intake.Category].Swatch;
        Stud.style.top  = `${StudInset + Index * StudSpacing}px`;
        Stud.style.left = "-7px";
        Stud.dataset.identifier = Entry.Identifier;
        Stud.dataset.intake     = Intake.Naming;
        Stud.dataset.category   = Intake.Category;
        Stud.title = `${Intake.Naming} — ${PortCategories[Intake.Category].Summary}`;
        Card.appendChild(Stud);
    });

    if (Specification.Family !== "Resolve")
    {
        const Stud = document.createElement("div");
        Stud.className = "PortStud Yield";
        Stud.style.background = PortCategories[Specification.Yields].Swatch;
        Stud.style.top   = `${StudInset}px`;
        Stud.style.right = "-7px";
        Stud.dataset.identifier = Entry.Identifier;
        Stud.dataset.category   = Specification.Yields;
        Stud.title = `yields ${Specification.Yields}`;
        Card.appendChild(Stud);
    }

    return Card;
}

function ComposeTool(Label, Flavour, Act)
{
    const Button = document.createElement("button");
    Button.className = `EntryTool ${Flavour}`;
    Button.textContent = Label;
    Button.addEventListener("pointerdown", Event => Event.stopPropagation());
    Button.addEventListener("click", Event => { Event.stopPropagation(); Act(); });
    return Button;
}

// 📝 Show the two dials that most change the silhouette, not all four — a card crowded with numbers
//    stops being scannable, which is the only reason to have cards rather than a list.
function ComposeDialSummary(Entry, Specification)
{
    const Wrap = document.createElement("span");

    const Interesting = Specification.Dials.slice(0, 2);
    Interesting.forEach((Dial, Index) =>
    {
        const [Label, Key, , , , Precision, Unit] = Dial;
        const Value = Entry.Dials[Index];

        const Chip = document.createElement("span");
        Chip.className = "DialChip";

        // ⚠️ Index dials read as a name, not a number — "0" means nothing to the author.
        let Shown = Value.toFixed(Precision);
        if (Entry.Species === "StratumTint" && Key === "palette")
        {
            Shown = TintPaletteNaming[Math.round(Value)] || Shown;
        }
        else if (Entry.Species === "LateralRepeat" && Key === "axis")
        {
            Shown = RepeatAxisNaming[Math.round(Value)] || Shown;
        }
        else if (Unit !== "-" && Unit !== "idx")
        {
            Shown = `${Shown}${Unit}`;
        }

        Chip.textContent = `${Label} ${Shown}`;
        Wrap.appendChild(Chip);
    });

    return Wrap;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     LINKS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Studs are positioned in CSS relative to their card, so their tree-space centre is derived from the
//    card position plus the stud's own offset — NOT from getBoundingClientRect, which would fold in the
//    viewport transform and make link geometry depend on the current zoom.
function LocateStud(Surface, Identifier, IntakeNaming)
{
    const Entry = Surface.TreeState.Entries.get(Identifier);
    if (!Entry) return null;

    const Specification = ConstructionSpecificationTable[Entry.Species];
    const Card = Surface.Cards.get(Identifier);
    const Width = Card ? Card.offsetWidth : 196;

    if (IntakeNaming === null)
    {
        return { x: Entry.Position.x + Width + 1, y: Entry.Position.y + StudInset + 6 };
    }

    const Index = Specification.Intakes.findIndex(Intake => Intake.Naming === IntakeNaming);
    if (Index < 0) return null;
    return { x: Entry.Position.x - 1, y: Entry.Position.y + StudInset + Index * StudSpacing + 6 };
}

export function RedrawLinks(Surface)
{
    const Layer = Surface.LinkLayer;
    Layer.textContent = "";

    for (const Link of Surface.TreeState.Links)
    {
        const Head = LocateStud(Surface, Link.SourceEntry, null);
        const Tail = LocateStud(Surface, Link.TargetEntry, Link.TargetIntake);
        if (!Head || !Tail) continue;

        const Source = Surface.TreeState.Entries.get(Link.SourceEntry);
        const Category = ConstructionSpecificationTable[Source.Species].Yields;

        const Path = document.createElementNS("http://www.w3.org/2000/svg", "path");
        Path.setAttribute("d", ComposeLinkCurve(Head, Tail));
        Path.setAttribute("class", "LinkCurve");
        Path.setAttribute("stroke", PortCategories[Category].Swatch);
        if (Source.Bypassed) Path.setAttribute("stroke-dasharray", "5 5");

        // 📝 Clicking a link severs it — the only way to disconnect without dragging the stud away.
        Path.addEventListener("click", Event =>
        {
            Event.stopPropagation();
            RemoveLink(Surface.TreeState, Link.TargetEntry, Link.TargetIntake);
            RedrawLinks(Surface);
            Surface.Notify("topology");
        });

        Layer.appendChild(Path);
    }

    if (Surface.Connecting && Surface.Connecting.Cursor)
    {
        const Draft = document.createElementNS("http://www.w3.org/2000/svg", "path");
        Draft.setAttribute("d", ComposeLinkCurve(Surface.Connecting.Origin, Surface.Connecting.Cursor));
        Draft.setAttribute("class", "LinkCurve Draft");
        Draft.setAttribute("stroke", PortCategories[Surface.Connecting.Category].Swatch);
        Layer.appendChild(Draft);
    }
}

// 📝 Horizontal-tangent cubic. The handle length grows with separation so short links stay taut and long
//    ones bow clear of the cards between them.
function ComposeLinkCurve(Head, Tail)
{
    const Span = Math.abs(Tail.x - Head.x);
    const Handle = Math.max(38, Math.min(Span * 0.55, 190));
    return `M ${Head.x} ${Head.y} C ${Head.x + Handle} ${Head.y}, ${Tail.x - Handle} ${Tail.y}, ${Tail.x} ${Tail.y}`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   GESTURES
//------------------------------------------------------------------------------------------------------------------------

function ApplyTransform(Surface)
{
    Surface.Viewport.style.transform =
        `translate(${Surface.Pan.x}px, ${Surface.Pan.y}px) scale(${Surface.Zoom})`;
}

// 📝 Convert a client point into tree space, which is what every drag needs.
function ToTreeSpace(Surface, ClientX, ClientY)
{
    const Frame = Surface.Host.getBoundingClientRect();
    return {
        x: (ClientX - Frame.left - Surface.Pan.x) / Surface.Zoom,
        y: (ClientY - Frame.top  - Surface.Pan.y) / Surface.Zoom
    };
}

function AttachSurfaceGestures(Surface)
{
    Surface.Host.addEventListener("pointerdown", Event =>
    {
        const Stud = Event.target.closest(".PortStud");
        const Card = Event.target.closest(".ConstructionEntry");

        // ① Stud drag begins a connection.
        if (Stud)
        {
            Event.stopPropagation();
            const Identifier = Number(Stud.dataset.identifier);
            const IsIntake   = Stud.classList.contains("Intake");

            // 📝 Dragging FROM an occupied intake picks the existing link up rather than refusing, so a
            //    rewire is one gesture instead of sever-then-connect.
            if (IsIntake)
            {
                const Existing = Surface.TreeState.Links.find(Link =>
                    Link.TargetEntry === Identifier && Link.TargetIntake === Stud.dataset.intake);

                if (Existing)
                {
                    RemoveLink(Surface.TreeState, Identifier, Stud.dataset.intake);
                    const Origin = LocateStud(Surface, Existing.SourceEntry, null);
                    Surface.Connecting =
                    {
                        FromIdentifier : Existing.SourceEntry,
                        FromIntake     : null,
                        Category       : Stud.dataset.category,
                        Origin,
                        Cursor         : Origin
                    };
                    Surface.Notify("topology");
                    RedrawLinks(Surface);
                    Surface.Host.setPointerCapture(Event.pointerId);
                    return;
                }
            }

            const Origin = LocateStud(Surface, Identifier, IsIntake ? Stud.dataset.intake : null);
            Surface.Connecting =
            {
                FromIdentifier : Identifier,
                FromIntake     : IsIntake ? Stud.dataset.intake : null,
                Category       : Stud.dataset.category,
                Origin,
                Cursor         : Origin
            };
            Surface.Host.setPointerCapture(Event.pointerId);
            return;
        }

        // ② Card drag moves it, and selects it.
        if (Card)
        {
            const Identifier = Number(Card.dataset.identifier);
            const Entry = Surface.TreeState.Entries.get(Identifier);
            const Grip = ToTreeSpace(Surface, Event.clientX, Event.clientY);

            Surface.Dragging =
            {
                Identifier,
                OffsetX : Grip.x - Entry.Position.x,
                OffsetY : Grip.y - Entry.Position.y
            };

            // 🔴 Engaging REBUILDS, so the Card element captured above is detached from here on. The
            //    move handler must look the card up through Surface.Cards, never close over this one.
            EngageEntry(Surface, Identifier);
            Surface.Host.setPointerCapture(Event.pointerId);
            return;
        }

        // ③ Empty surface drags the view and clears the selection.
        Surface.Dragging = { Panning: true, FromX: Event.clientX, FromY: Event.clientY,
                             PanX: Surface.Pan.x, PanY: Surface.Pan.y };
        EngageEntry(Surface, null);
        Surface.Host.setPointerCapture(Event.pointerId);
    });

    Surface.Host.addEventListener("pointermove", Event =>
    {
        if (Surface.Connecting)
        {
            Surface.Connecting.Cursor = ToTreeSpace(Surface, Event.clientX, Event.clientY);
            RedrawLinks(Surface);

            // 📝 Light up only the studs this drag could legally land on. Showing the rule beats
            //    rejecting the drop silently and leaving the author guessing why.
            HighlightCompatible(Surface, Surface.Connecting);
            return;
        }

        if (!Surface.Dragging) return;

        if (Surface.Dragging.Panning)
        {
            Surface.Pan.x = Surface.Dragging.PanX + (Event.clientX - Surface.Dragging.FromX);
            Surface.Pan.y = Surface.Dragging.PanY + (Event.clientY - Surface.Dragging.FromY);
            ApplyTransform(Surface);
            return;
        }

        const Entry = Surface.TreeState.Entries.get(Surface.Dragging.Identifier);
        if (!Entry) return;

        const Grip = ToTreeSpace(Surface, Event.clientX, Event.clientY);
        Entry.Position.x = Math.round(Grip.x - Surface.Dragging.OffsetX);
        Entry.Position.y = Math.round(Grip.y - Surface.Dragging.OffsetY);

        // 📝 Looked up rather than closed over: the pointerdown that began this drag also engaged the
        //    entry, which rebuilt every card.
        const Card = Surface.Cards.get(Entry.Identifier);
        if (!Card) return;
        Card.style.left = `${Entry.Position.x}px`;
        Card.style.top  = `${Entry.Position.y}px`;
        RedrawLinks(Surface);
    });

    Surface.Host.addEventListener("pointerup", Event =>
    {
        if (Surface.Connecting)
        {
            const Landing = document.elementFromPoint(Event.clientX, Event.clientY);
            const Stud = Landing ? Landing.closest(".PortStud") : null;
            SettleConnection(Surface, Stud);
            Surface.Connecting = null;
            ClearHighlight(Surface);
            RedrawLinks(Surface);
        }
        Surface.Dragging = null;
    });

    // ④ Zoom about the cursor, so the point under the pointer stays put.
    Surface.Host.addEventListener("wheel", Event =>
    {
        Event.preventDefault();
        const Before = ToTreeSpace(Surface, Event.clientX, Event.clientY);

        const Factor = Event.deltaY < 0 ? 1.09 : 1 / 1.09;
        Surface.Zoom = Math.min(Math.max(Surface.Zoom * Factor, ZoomFloor), ZoomCeiling);

        const Frame = Surface.Host.getBoundingClientRect();
        Surface.Pan.x = Event.clientX - Frame.left - Before.x * Surface.Zoom;
        Surface.Pan.y = Event.clientY - Frame.top  - Before.y * Surface.Zoom;

        ApplyTransform(Surface);
        Surface.Notify("view");
    }, { passive: false });
}

function SettleConnection(Surface, Stud)
{
    if (!Stud) return;

    const Draft = Surface.Connecting;
    const Identifier = Number(Stud.dataset.identifier);
    const IsIntake = Stud.classList.contains("Intake");

    // 🔴 A connection needs exactly one yield end and one intake end. Two of the same is a no-op rather
    //    than an error, because it is an easy slip and an error dialog would be disproportionate.
    let SourceEntry, TargetEntry, TargetIntake;

    if (Draft.FromIntake === null && IsIntake)
    {
        SourceEntry = Draft.FromIdentifier;
        TargetEntry = Identifier;
        TargetIntake = Stud.dataset.intake;
    }
    else if (Draft.FromIntake !== null && !IsIntake)
    {
        SourceEntry = Identifier;
        TargetEntry = Draft.FromIdentifier;
        TargetIntake = Draft.FromIntake;
    }
    else
    {
        return;
    }

    const Accepted = InsertLink(Surface.TreeState, SourceEntry, TargetEntry, TargetIntake);

    if (!Accepted)
    {
        // 📝 Name the reason. "Nothing happened" is the worst possible feedback for a refused link.
        const Source = Surface.TreeState.Entries.get(SourceEntry);
        const Target = Surface.TreeState.Entries.get(TargetEntry);
        if (Source && Target)
        {
            const Yields = ConstructionSpecificationTable[Source.Species].Yields;
            const Intake = ConstructionSpecificationTable[Target.Species]
                .Intakes.find(Candidate => Candidate.Naming === TargetIntake);

            if (Intake && !MayConnect(Yields, Intake.Category))
            {
                Surface.Notify("refused",
                    `${Yields} cannot feed a ${Intake.Category} intake`);
            }
            else
            {
                Surface.Notify("refused", "that link would close a cycle");
            }
        }
        return;
    }

    Surface.Notify("topology");
}

function HighlightCompatible(Surface, Draft)
{
    ClearHighlight(Surface);
    const WantIntake = Draft.FromIntake === null;

    for (const Stud of Surface.Host.querySelectorAll(".PortStud"))
    {
        const IsIntake = Stud.classList.contains("Intake");
        if (IsIntake !== WantIntake) continue;
        if (Number(Stud.dataset.identifier) === Draft.FromIdentifier) continue;

        const Compatible = WantIntake
            ? MayConnect(Draft.Category, Stud.dataset.category)
            : MayConnect(Stud.dataset.category, Draft.Category);

        Stud.classList.add(Compatible ? "Inviting" : "Refusing");
    }
}

function ClearHighlight(Surface)
{
    for (const Stud of Surface.Host.querySelectorAll(".PortStud"))
    {
        Stud.classList.remove("Inviting", "Refusing");
    }
}

// 🔴 Engaging now CHANGES THE CARD, not just its outline — the engaged card expands its dial rack — so
//    this rebuilds rather than toggling a class. Guarded on an actual change: a card drag calls this on
//    every pointerdown, and rebuilding mid-drag would discard the element the drag is holding.
export function EngageEntry(Surface, Identifier)
{
    if (Surface.Engaged === Identifier) return;

    Surface.Engaged = Identifier;
    RebuildSurface(Surface);
    if (Surface.OnEngage) Surface.OnEngage(Identifier);
}

// 📝 Frame the whole tree. Called on load so the seed is visible without the author hunting for it.
export function FrameTree(Surface)
{
    const Entries = [...Surface.TreeState.Entries.values()];
    if (Entries.length === 0) return;

    let MinX = Infinity, MinY = Infinity, MaxX = -Infinity, MaxY = -Infinity;
    for (const Entry of Entries)
    {
        const Card = Surface.Cards.get(Entry.Identifier);
        const Width  = Card ? Card.offsetWidth  : 200;
        const Height = Card ? Card.offsetHeight : 130;
        MinX = Math.min(MinX, Entry.Position.x);
        MinY = Math.min(MinY, Entry.Position.y);
        MaxX = Math.max(MaxX, Entry.Position.x + Width);
        MaxY = Math.max(MaxY, Entry.Position.y + Height);
    }

    const Frame = Surface.Host.getBoundingClientRect();
    const Margin = 70;
    const SpanX = Math.max(MaxX - MinX + Margin * 2, 1);
    const SpanY = Math.max(MaxY - MinY + Margin * 2, 1);

    Surface.Zoom = Math.min(Math.max(Math.min(Frame.width / SpanX, Frame.height / SpanY),
                                     ZoomFloor), 1.0);
    Surface.Pan.x = (Frame.width  - (MaxX - MinX) * Surface.Zoom) / 2 - MinX * Surface.Zoom;
    Surface.Pan.y = (Frame.height - (MaxY - MinY) * Surface.Zoom) / 2 - MinY * Surface.Zoom;
    ApplyTransform(Surface);
}

/*====================================================================================================================================
                                                       MATERIALSHELF.JS
====================================================================================================================================*/
// 🧩 The material browser — a notch-handled drawer that slides up from the bottom edge and assigns a preset to a layer

import { MATERIAL_PRESETS, MATERIAL_ORDER, MATERIAL_FAMILIES } from "../Layers/LayerKinds.js";
import { MaterialSwatchCanvas }                                from "../Layers/MaterialSwatch.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                        CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const TILE_SIZE  = 64;                          // [px] the grid tile's sphere
const GHOST_SIZE = 56;                          // [px] the sphere that follows the pointer on a drag
const HERO_SIZE  = 132;                         // [px] the detail card's sphere

// 🔴 Capped as well as proportional. On a tall window 55% of the height is a drawer that covers the model it
//    is meant to be assigning a material TO — the user cannot see the result of a click without closing the
//    thing they clicked in. The cap keeps the stage visible at every window size.
const RevealHeight = () => Math.min(window.innerHeight * 0.55, 420);

// The frame fades in once the drawer is tall enough to hold it, rather than being scaled or clipped mid-word.
const FRAME_VISIBLE_AT = 60;                    // [px]

// A flick past this speed decides the direction on its own; below it, distance decides. In px/s.
const FLICK_SPEED = 1000;

// How far the drag must travel before a slow release commits, when no flick was detected. In px.
const COMMIT_TRAVEL = 120;

const ALL_FAMILIES = "All";

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE SHELF CLASS
//------------------------------------------------------------------------------------------------------------------------

export class MaterialShelf
{
    // `Commands` is the same command surface the inspector and the probe drive — never the stack itself.
    // `OnAssign` fires after an assignment lands, so the host can re-resolve the composite and refresh panes.
    constructor(Host, Stack, Commands, OnAssign)
    {
        this.Host     = Host;
        this.Stack    = Stack;
        this.Commands = Commands;
        this.OnAssign = OnAssign ?? (() => {});

        this.Open      = false;
        this.Family    = ALL_FAMILIES;
        this.Query     = "";
        this.Selected  = MATERIAL_ORDER[0] ?? null;

        // The slide state. `Offset` is the live height in px; the spring chases `Target`.
        this.Offset   = 0;
        this.Velocity = 0;
        this.Target   = 0;
        this.Settled  = true;

        // Stiffness / damping are near-critical: 320 and 34 give a firm arrival with no visible overshoot on
        // a ~420px throw. A softer pair reads as the drawer being heavy and laggy.
        this.Stiffness = 320;
        this.Damping   = 34;

        this.Drag = null;                        // the in-flight notch drag, if any
        this.Lift = null;                        // the in-flight tile drag, if any

        this.Build();
        this.RenderRail();
        this.RenderGrid();
        this.RenderHero();
        this.ApplyOffset();

        this.Bind();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                      STRUCTURE
    //--------------------------------------------------------------------------------------------------------------------

    Build()
    {
        const Shelf = document.createElement("div");
        Shelf.id = "MaterialShelf";

        // The notch path is the donor's verbatim: a flat baseline that eases up into a 120-wide plateau. Kept
        // as authored bezier control points rather than re-derived, because the curve's exact shoulder is the
        // affordance — a rounded rectangle in its place reads as a button, not as a lip to pull.
        Shelf.innerHTML =
            `<svg class="shelf-notch" viewBox="0 0 200 36" preserveAspectRatio="none">` +
            `<path d="M100 36 L0 36 C20 36 20 0 40 0 L160 0 C180 0 180 36 200 36 Z"/></svg>` +
            `<div class="shelf-label">Materials</div>` +
            `<div class="shelf-drawer"><div class="shelf-frame">` +
            `<div class="shelf-rail"></div>` +
            `<div class="shelf-mid">` +
            `<input class="shelf-search" type="text" placeholder="Search materials" spellcheck="false">` +
            `<div class="shelf-grid"></div>` +
            `</div>` +
            `<div class="shelf-hero"></div>` +
            `</div></div>`;

        this.Host.appendChild(Shelf);

        this.Part = {
            Shelf:  Shelf,
            Notch:  Shelf.querySelector(".shelf-notch"),
            Label:  Shelf.querySelector(".shelf-label"),
            Drawer: Shelf.querySelector(".shelf-drawer"),
            Frame:  Shelf.querySelector(".shelf-frame"),
            Rail:   Shelf.querySelector(".shelf-rail"),
            Search: Shelf.querySelector(".shelf-search"),
            Grid:   Shelf.querySelector(".shelf-grid"),
            Hero:   Shelf.querySelector(".shelf-hero")
        };

        // The ghost and the drop veil live OUTSIDE the shelf. The shelf is pointer-inert and clipped; a ghost
        // parented into it would be cut off at the drawer's edge the moment it was dragged over the model.
        const Ghost = document.createElement("div");
        Ghost.id = "MaterialShelfGhost";
        document.body.appendChild(Ghost);

        const Veil = document.createElement("div");
        Veil.id = "MaterialShelfVeil";
        this.Host.appendChild(Veil);

        this.Part.Ghost = Ghost;
        this.Part.Veil  = Veil;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                     THE SLIDE
    //--------------------------------------------------------------------------------------------------------------------

    // Push the current offset into the DOM. The notch and its label ride on top of the drawer's own height,
    // so the handle stays attached to the panel's edge through the whole travel.
    ApplyOffset()
    {
        const Offset = this.Offset;
        this.Part.Notch.style.bottom  = `${Offset}px`;
        this.Part.Label.style.bottom  = `${Offset}px`;
        this.Part.Drawer.style.height = `${Offset}px`;

        // The frame is inset by the 12px top/bottom margin, so its own height is the drawer's less 24.
        this.Part.Frame.style.height  = `${Math.max(0, Offset - 24)}px`;
        this.Part.Frame.style.opacity = Offset > FRAME_VISIBLE_AT ? "1" : "0";
    }

    // One spring integration step. Called from the host's frame loop with the real frame delta.
    //
    // 🔴 Returns early when settled and does NOT touch the DOM. Writing the same height every frame forces a
    //    layout on a full-viewport element sixty times a second for a drawer that is not moving, which shows
    //    up as a constant cost in the paint loop's own timings.
    Step(DeltaTime)
    {
        if (this.Settled) { return; }

        // Clamped: a long frame (a tab regaining focus, a shader compile) integrates a huge step and the
        // spring explodes past the target and oscillates. 1/30 s keeps a stall to a slow slide instead.
        const Delta = Math.min(DeltaTime, 1 / 30);

        const Force = (this.Target - this.Offset) * this.Stiffness - this.Velocity * this.Damping;
        this.Velocity += Force * Delta;
        this.Offset   += this.Velocity * Delta;

        if (Math.abs(this.Target - this.Offset) < 0.5 && Math.abs(this.Velocity) < 2)
        {
            this.Offset   = this.Target;
            this.Velocity = 0;
            this.Settled  = true;
        }
        this.ApplyOffset();
    }

    SetOpen(Open)
    {
        this.Open     = Open;
        this.Target   = Open ? RevealHeight() : 0;
        this.Velocity = 0;
        this.Settled  = false;

        // Repaint on open: a preset's values may have been edited in the layer properties while the drawer was
        // shut, and the hero would otherwise still show the pre-edit sphere.
        if (Open) { this.RenderGrid(); this.RenderHero(); }
    }

    Toggle() { this.SetOpen(!this.Open); }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    INPUT BINDING
    //--------------------------------------------------------------------------------------------------------------------

    Bind()
    {
        const Notch = this.Part.Notch;

        Notch.addEventListener("pointerdown", (Event) =>
        {
            Event.preventDefault();
            Notch.setPointerCapture(Event.pointerId);
            Notch.classList.add("dragging");

            this.Drag = {
                Pointer:     Event.pointerId,
                StartY:      Event.clientY,
                StartOffset: this.Offset,
                LastY:       Event.clientY,
                LastTime:    Event.timeStamp,
                Speed:       0,
                Moved:       false
            };
            // The spring is stood down for the duration: the pointer owns the offset while it is down.
            this.Settled = true;
        });

        Notch.addEventListener("pointermove", (Event) =>
        {
            const Drag = this.Drag;
            if (!Drag || Event.pointerId !== Drag.Pointer) { return; }

            // 🔴 Guarded against a zero delta. Two moves inside the same millisecond give DeltaTime 0 and the
            //    speed becomes Infinity, which then always reads as a flick — a one-pixel nudge would slam the
            //    drawer open. Below the floor the previous speed is simply kept.
            const DeltaTime = (Event.timeStamp - Drag.LastTime) / 1000;
            if (DeltaTime > 1e-4)
            {
                // Upward motion is positive speed: screen Y falls as the drawer grows.
                const Instant = -(Event.clientY - Drag.LastY) / DeltaTime;
                Drag.Speed    = Drag.Speed * 0.65 + Instant * 0.35;
                Drag.LastY    = Event.clientY;
                Drag.LastTime = Event.timeStamp;
            }

            const Reveal = RevealHeight();
            this.Offset  = Math.max(0, Math.min(Reveal, Drag.StartOffset + (Drag.StartY - Event.clientY)));
            if (Math.abs(this.Offset - Drag.StartOffset) > 3) { Drag.Moved = true; }
            this.ApplyOffset();
        });

        const EndDrag = (Event) =>
        {
            const Drag = this.Drag;
            if (!Drag || Event.pointerId !== Drag.Pointer) { return; }
            this.Drag = null;
            Notch.classList.remove("dragging");

            // A press with no travel is a CLICK on the handle, not a zero-distance drag. Without this the
            // notch would only ever respond to dragging and tapping it would appear to do nothing.
            if (!Drag.Moved) { this.Toggle(); return; }

            if (Drag.Speed > FLICK_SPEED)       { this.SetOpen(true);  }
            else if (Drag.Speed < -FLICK_SPEED) { this.SetOpen(false); }
            else
            {
                const Travel = this.Offset - Drag.StartOffset;
                if (Travel > COMMIT_TRAVEL)       { this.SetOpen(true);  }
                else if (Travel < -COMMIT_TRAVEL) { this.SetOpen(false); }
                else                              { this.SetOpen(this.Open); }   // snap back where it came from
            }
        };

        Notch.addEventListener("pointerup", EndDrag);
        // 🔴 pointercancel too. A drag interrupted by the OS (a gesture, a window switch) never sends
        //    pointerup, and without this the drag record survives — the offset then stays wherever the
        //    pointer left it and the spring never takes it back, so the drawer is stuck half-open.
        Notch.addEventListener("pointercancel", EndDrag);

        this.Part.Search.addEventListener("input", () =>
        {
            this.Query = this.Part.Search.value.trim().toLowerCase();
            this.RenderGrid();
        });

        window.addEventListener("resize", () =>
        {
            // Re-target rather than re-seat: the reveal height is window-relative, so an open drawer must
            // settle to the NEW height instead of keeping a stale one that no longer matches the cap.
            if (this.Open) { this.Target = RevealHeight(); this.Settled = false; }
        });

        this.BindLift();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                      THE RAIL
    //--------------------------------------------------------------------------------------------------------------------

    RenderRail()
    {
        const Rail = this.Part.Rail;
        Rail.textContent = "";

        // 📝 "All" then the families derived from the preset table. Adding a preset with a new Family makes a
        //    pill appear here with no second edit — the earlier hardcoded list is what made new categories
        //    reachable only through search.
        for (const Name of [ALL_FAMILIES, ...MATERIAL_FAMILIES])
        {
            const Count = Name === ALL_FAMILIES
                ? MATERIAL_ORDER.length
                : MATERIAL_ORDER.filter((Key) => MATERIAL_PRESETS[Key]?.Family === Name).length;

            const Pill = document.createElement("div");
            Pill.className = "rail-pill" + (this.Family === Name ? " on" : "");
            Pill.innerHTML = `<span>${Name}</span><span class="rail-count">${Count}</span>`;
            Pill.onclick = () =>
            {
                this.Family = Name;
                this.RenderRail();
                this.RenderGrid();
            };
            Rail.appendChild(Pill);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                      THE GRID
    //--------------------------------------------------------------------------------------------------------------------

    VisibleMaterials()
    {
        return MATERIAL_ORDER.filter((Key) =>
        {
            const Preset = MATERIAL_PRESETS[Key];
            if (!Preset) { return false; }
            if (this.Family !== ALL_FAMILIES && Preset.Family !== this.Family) { return false; }
            if (!this.Query) { return true; }

            // Searched over the label, the family AND the key, so "gold", "metal" and the internal
            // `polymerGrey` all find their material.
            const Haystack = `${Preset.Label} ${Preset.Family} ${Key}`.toLowerCase();
            return Haystack.includes(this.Query);
        });
    }

    RenderGrid()
    {
        const Grid = this.Part.Grid;
        Grid.textContent = "";

        const Keys = this.VisibleMaterials();
        if (Keys.length === 0)
        {
            const Empty = document.createElement("div");
            Empty.className = "grid-empty";
            Empty.textContent = `No material matches "${this.Part.Search.value.trim()}".`;
            Grid.appendChild(Empty);
            return;
        }

        for (const Key of Keys)
        {
            const Preset = MATERIAL_PRESETS[Key];

            const Tile = document.createElement("div");
            Tile.className = "shelf-tile" + (this.Selected === Key ? " sel" : "");
            Tile.dataset.material = Key;

            // The cached canvas is appended DIRECTLY — it is owned by the swatch cache and outlives this tile,
            // so a rebuild re-parents the same element rather than re-rastering fourteen spheres.
            Tile.appendChild(MaterialSwatchCanvas(Key, TILE_SIZE));

            const Name = document.createElement("div");
            Name.className = "tile-name";
            Name.textContent = Preset.Label;
            Tile.appendChild(Name);

            this.BindTile(Tile, Key);
            Grid.appendChild(Tile);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    THE HERO CARD
    //--------------------------------------------------------------------------------------------------------------------

    RenderHero()
    {
        const Hero = this.Part.Hero;
        Hero.textContent = "";

        const Key    = this.Selected;
        const Preset = MATERIAL_PRESETS[Key];
        if (!Preset) { return; }

        const Swatch = document.createElement("div");
        Swatch.className = "hero-swatch";
        Swatch.appendChild(MaterialSwatchCanvas(Key, HERO_SIZE));
        Hero.appendChild(Swatch);

        const Family = document.createElement("div");
        Family.className = "hero-family";
        Family.textContent = Preset.Family ?? "Other";
        Hero.appendChild(Family);

        const Name = document.createElement("div");
        Name.className = "hero-name";
        Name.textContent = Preset.Label;
        Hero.appendChild(Name);

        if (Preset.Note)
        {
            const Note = document.createElement("div");
            Note.className = "hero-note";
            Note.textContent = Preset.Note;
            Hero.appendChild(Note);
        }

        // Which of the five stored channels this preset authors — the same set the layer properties will
        // offer rows for, so the card predicts the editable surface rather than just describing the look.
        const Channels = document.createElement("div");
        Channels.className = "hero-chan";
        for (const Channel of Preset.Channels ?? [])
        {
            const Chip = document.createElement("span");
            Chip.className = "chan-chip";
            Chip.textContent = Channel;
            Channels.appendChild(Chip);
        }
        Hero.appendChild(Channels);

        const Assign = document.createElement("div");
        Assign.className = "hero-assign";
        Assign.textContent = "Assign to focused layer";
        Assign.onclick = () => this.AssignToFocus(Key);
        Hero.appendChild(Assign);

        const Hint = document.createElement("div");
        Hint.className = "hero-hint";
        Hint.textContent = "Click a swatch to retarget the focused layer. Drag one onto the model to add a new material layer.";
        Hero.appendChild(Hint);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                  TILE CLICK + DRAG
    //--------------------------------------------------------------------------------------------------------------------

    // 🔴 Click and drag share ONE pointerdown, discriminated by travel in the document handler above. Binding
    //    a click listener alongside a separate dragstart makes both fire on a drag released over the drawer, so
    //    a dragged-and-abandoned swatch would silently retarget the focused layer too — an edit the user never
    //    asked for, to a layer they were not looking at. The tile only opens the record; the release decides.
    BindTile(Tile, Key)
    {
        Tile.addEventListener("pointerdown", (Event) =>
        {
            if (Event.button !== 0) { return; }
            Event.preventDefault();

            this.Select(Key);

            this.Lift = {
                Pointer:  Event.pointerId,
                Material: Key,
                StartX:   Event.clientX,
                StartY:   Event.clientY,
                Armed:    false
            };
        });

    }

    // The move/up pair that carries an armed tile drag lives on the DOCUMENT, because the pointer leaves the
    // tile immediately — the whole point is to travel over the model.
    //
    // 🔴 Bound ONCE, here, and not per tile in BindTile. Every RenderGrid rebuilds fourteen tiles, and a
    //    per-tile document listener is never removed when its tile is discarded: after a few rail clicks
    //    hundreds of stale handlers all read the same `this.Lift`, each firing AddLayer for whichever material
    //    its dead closure captured — one drop would add a dozen layers of the wrong materials. The live
    //    material is read off the drag record instead of a closure, so there is nothing to go stale.
    BindLift()
    {
        const Move = (Event) =>
        {
            const Lift = this.Lift;
            if (!Lift || Event.pointerId !== Lift.Pointer) { return; }

            if (!Lift.Armed)
            {
                // A few pixels of slack, so a click with an unsteady hand stays a click.
                const Travel = Math.hypot(Event.clientX - Lift.StartX, Event.clientY - Lift.StartY);
                if (Travel < 6) { return; }
                Lift.Armed = true;
                this.ShowGhost(Lift.Material);
            }

            this.MoveGhost(Event.clientX, Event.clientY);
            this.Part.Veil.classList.toggle("on", this.OverStage(Event.clientX, Event.clientY));
        };

        const Up = (Event) =>
        {
            const Lift = this.Lift;
            if (!Lift || Event.pointerId !== Lift.Pointer) { return; }
            this.Lift = null;

            // An unarmed release is a CLICK. Handled here rather than on the tile's own pointerup, so a press
            // that drifted a pixel and released still lands exactly one action.
            if (!Lift.Armed) { this.AssignToFocus(Lift.Material); return; }

            this.HideGhost();
            this.Part.Veil.classList.remove("on");
            if (this.OverStage(Event.clientX, Event.clientY)) { this.AddLayer(Lift.Material); }
        };

        document.addEventListener("pointermove", Move);
        document.addEventListener("pointerup", Up);
        document.addEventListener("pointercancel", Up);
    }

    Select(Key)
    {
        if (this.Selected === Key) { return; }
        this.Selected = Key;

        // Only the two affected tiles are touched, not the whole grid — a full RenderGrid on every hover-click
        // detaches and re-appends fourteen canvases and the selection ring flickers.
        for (const Tile of this.Part.Grid.querySelectorAll(".shelf-tile"))
        {
            Tile.classList.toggle("sel", Tile.dataset.material === Key);
        }
        this.RenderHero();
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                      THE GHOST
    //--------------------------------------------------------------------------------------------------------------------

    ShowGhost(Key)
    {
        const Ghost = this.Part.Ghost;
        Ghost.textContent = "";
        Ghost.appendChild(MaterialSwatchCanvas(Key, GHOST_SIZE));
        Ghost.style.opacity = "1";
    }

    MoveGhost(X, Y)
    {
        this.Part.Ghost.style.left = `${X}px`;
        this.Part.Ghost.style.top  = `${Y}px`;
    }

    HideGhost()
    {
        // 🔴 Emptied as well as hidden. The ghost holds the CACHED canvas, and leaving it parented here means
        //    the next RenderGrid moves that same element out of the ghost into the grid — or worse, the hero
        //    steals the tile's canvas — and swatches start vanishing from wherever they were last drawn.
        this.Part.Ghost.style.opacity = "0";
        this.Part.Ghost.textContent   = "";
    }

    // Is the pointer over the stage, but NOT over the open drawer? A drop onto the drawer is a cancelled drag.
    OverStage(X, Y)
    {
        const Stage = this.Host.getBoundingClientRect();
        if (X < Stage.left || X > Stage.right || Y < Stage.top || Y > Stage.bottom) { return false; }

        const Drawer = this.Part.Drawer.getBoundingClientRect();
        const InDrawer = this.Offset > 0 &&
            X >= Drawer.left && X <= Drawer.right && Y >= Drawer.top && Y <= Drawer.bottom;
        return !InDrawer;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                   THE TWO ASSIGN PATHS
    //--------------------------------------------------------------------------------------------------------------------

    // A click retargets the FOCUSED layer in place, keeping its name, opacity, blend and mask.
    //
    // 🔴 Refused rather than forced when the focused layer cannot hold a preset. A paint layer's content is
    //    its strokes and a generator's is its pass; flooding either with a preset would destroy work the user
    //    can see, from a click in a browser that says nothing about deleting anything. The toast says which
    //    layer refused it and drag-to-add remains available.
    AssignToFocus(Key)
    {
        // 📝 `Focus` is a GETTER on the stack, resolving the focus token to the live layer. Read as a property,
        //    never called — `Focus()` throws, and a guarded `Focus_Get?.()` would silently yield null instead,
        //    so every click would report "no layer is focused" with a layer plainly highlighted in the stack.
        const Layer = this.Stack?.Focus ?? null;

        if (!Layer)
        {
            this.Notify("No layer is focused — drag the swatch onto the model to add one.");
            return false;
        }
        if (Layer.Kind !== "material" && Layer.Kind !== "fill")
        {
            this.Notify(`"${Layer.Name}" is a ${Layer.Kind} layer — drag onto the model to add a material layer.`);
            return false;
        }

        const Result = this.Commands("preset", { Token: Layer.Token, Preset: Key });
        if (Result) { this.Notify(`${MATERIAL_PRESETS[Key].Label} → ${Layer.Name}`); this.OnAssign(); }
        return Result;
    }

    // A drag onto the model adds a NEW material layer at the top of the stack.
    AddLayer(Key)
    {
        const Preset = MATERIAL_PRESETS[Key];
        if (!Preset) { return false; }

        // 📝 The layer takes the preset's label as its name. A stack of rows all reading "NEW_Material" is
        //    unnavigable, and the name is the only thing distinguishing two material layers in the list.
        const Result = this.Commands("add", { Name: Preset.Label, Kind: "material", Preset: Key });
        if (Result) { this.Notify(`Added layer — ${Preset.Label}`); this.OnAssign(); }
        return Result;
    }

    // A short confirmation banner, styled by `.shelf-toast` in this shelf's own sheet.
    //
    // 🔴 `.on` is added on a LATER frame, not in the same tick as the insert. A class set immediately gives
    //    the transition no start value to run from, so the toast appears with no fade — and in the assign path
    //    that reads as a flash rather than a message.
    //
    // 🔴 Raised clear of an open drawer. The banner is position:fixed near the bottom edge, which is exactly
    //    where the drawer is: without this the one message that explains a REFUSED assignment is drawn behind
    //    the panel the user clicked in, and the click looks like it silently did nothing.
    Notify(Message)
    {
        const Clearance = this.Open ? this.Offset : 0;

        const Toast = document.createElement("div");
        Toast.className = "shelf-toast";
        Toast.textContent = Message;
        Toast.style.bottom = `${24 + Clearance}px`;
        document.body.appendChild(Toast);

        requestAnimationFrame(() => Toast.classList.add("on"));
        setTimeout(() =>
        {
            Toast.classList.remove("on");
            setTimeout(() => Toast.remove(), 200);
        }, 1900);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                     HOST HOOKS
    //--------------------------------------------------------------------------------------------------------------------

    // Called by the host when a preset's authored values change under a properties edit, so the grid and hero
    // stop showing the pre-edit sphere. The caller invalidates the cache; this re-parents the fresh canvases.
    Repaint()
    {
        this.RenderGrid();
        this.RenderHero();
    }
}

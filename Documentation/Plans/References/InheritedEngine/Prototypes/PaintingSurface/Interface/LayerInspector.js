/*====================================================================================================================================
                                                     LAYERINSPECTOR.JS
====================================================================================================================================*/
// 🧩 Tab-summoned layer manager: [stack | layer properties] ⇄ [identity | channels], driving the live stack

import { CLASSIFICATION_LABEL, CLASSIFICATION_TINT,
         BLEND_MODES, LayerCapacity } from "../Layers/LayerStack.js";
import { CHANNEL_ORDER, CHANNEL_LABEL, CHANNEL_SLOTS,
         IsStoredChannel }                            from "../Layers/ChannelSet.js";
import { CHANNEL_MODES, LAYER_KINDS, LAYER_KIND_ORDER, KindLabel, KindTint,
         DefaultChannels, MATERIAL_PRESETS, MaterialChannelRows } from "../Layers/LayerKinds.js";
import { MASK_COMPONENT_CATEGORY, MASK_COMPONENT_ORDER, MASK_COMPONENT_PARAMS,
         MaskFillValue } from "../Layers/LayerMask.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                        ARTWORK
//------------------------------------------------------------------------------------------------------------------------

// Darken a hex toward black by 0-1, for the shaded faces of the classification glyphs.
function Shade(Hex, Amount)
{
    const Packed = parseInt(Hex.slice(1), 16);
    const Red    = Math.round(((Packed >> 16) & 255) * (1 - Amount));
    const Green  = Math.round(((Packed >>  8) & 255) * (1 - Amount));
    const Blue   = Math.round(( Packed        & 255) * (1 - Amount));
    return "#" + ((1 << 24) + (Red << 16) + (Green << 8) + Blue).toString(16).slice(1);
}

const SvgWrap = (Body, Size) =>
    `<svg viewBox="0 0 24 24" width="${Size}" height="${Size}" fill="none" ` +
    `stroke-linecap="round" stroke-linejoin="round">${Body}</svg>`;

// 💡 Each glyph is a function of the hue, so the same art recolours per classification.
const CLASSIFICATION_ART = {
    material: (H) => `
        <path d="M12 3 L20.5 7.5 L12 12 L3.5 7.5 Z" fill="${H}"/>
        <path d="M3.5 7.5 L12 12 L12 21 L3.5 16.5 Z" fill="${Shade(H, .34)}"/>
        <path d="M20.5 7.5 L12 12 L12 21 L20.5 16.5 Z" fill="${Shade(H, .56)}"/>`,
    generator: (H) => `
        <path d="M12 2.5 L13.7 8.3 L19.5 10 L13.7 11.7 L12 17.5 L10.3 11.7 L4.5 10 L10.3 8.3 Z" fill="${H}"/>
        <circle cx="18" cy="18" r="2.1" fill="${Shade(H, .3)}"/>
        <circle cx="6.4" cy="17.4" r="1.4" fill="${Shade(H, .45)}"/>`,
    brushwork: (H) => `
        <path d="M14.6 3.6 L20.4 9.4 L11 18.8 L5.2 13 Z" fill="${H}"/>
        <path d="M5.2 13 L11 18.8 L8.4 21.4 L3.2 21.4 L2.6 16.2 Z" fill="${Shade(H, .4)}"/>
        <path d="M14.6 3.6 L20.4 9.4 L17.8 12 L12 6.2 Z" fill="${Shade(H, .6)}"/>`,
    flood: (H) => `
        <path d="M11.4 2.8 L20.6 12 L12.3 20.3 L3.1 11.1 Z" fill="${H}"/>
        <path d="M11.4 2.8 L20.6 12 L12.3 20.3 L11.4 19.4 L11.4 2.8 Z" fill="${Shade(H, .42)}"/>
        <path d="M3.1 11.1 L12.3 20.3 L12.3 14.4 L6.2 14.4 Z" fill="${Shade(H, .62)}"/>`
};

const Hue = (Classification) => CLASSIFICATION_TINT[Classification] ?? "#5b8cff";

// The layer's own identity colour, for the rail tag and anything else marking THIS layer rather than its kind.
//
// 🔴 Falls back to the classification tint, not to a hard-coded default: a layer arriving from an older document
//    (or any caller that predates PaintLayer.Colour) has no Colour field, and defaulting it to one fixed hue
//    would tag every such layer identically — the very fault the per-layer palette exists to fix. The kind tint
//    is at least as informative as what was there before.
const LayerHue = (Layer) => Layer?.Colour ?? Hue(Layer?.Classification);

const ClassificationSvg = (Classification, Size) =>
    SvgWrap(CLASSIFICATION_ART[Classification] ? CLASSIFICATION_ART[Classification](Hue(Classification)) : "", Size);

// 📝 Chrome stays monochrome currentColor — only classification art carries hue.
const Stroked = 'stroke="currentColor" stroke-width="1.7" fill="none"';
const GLYPH = {
    stack:       `<path ${Stroked} d="M12 3 L21 8 L12 13 L3 8 Z"/><path ${Stroked} d="M3 12.5 L12 17.5 L21 12.5"/><path ${Stroked} d="M3 16.5 L12 21.5 L21 16.5"/>`,
    search:      `<circle ${Stroked} cx="10.5" cy="10.5" r="6.5"/><path ${Stroked} d="M15.5 15.5 L21 21"/>`,
    chevron:     `<path ${Stroked} d="M8 5 L15 12 L8 19"/>`,
    chevronDown: `<path ${Stroked} d="M5 8 L12 15 L19 8"/>`,
    chevronLeft: `<path ${Stroked} d="M15 5 L8 12 L15 19"/>`,
    plus:        `<path ${Stroked} d="M12 5 V19 M5 12 H19"/>`,
    rename:      `<path ${Stroked} d="M4 20 h4 L20 8 l-4-4 L4 16 Z"/><path ${Stroked} d="M14.5 5.5 L18.5 9.5"/>`,
    eyeOpen:     `<path ${Stroked} d="M2.5 12 S6 5.5 12 5.5 S21.5 12 21.5 12 S18 18.5 12 18.5 S2.5 12 2.5 12 Z"/><circle ${Stroked} cx="12" cy="12" r="2.7"/>`,
    eyeOff:      `<path ${Stroked} d="M4 4 L20 20"/><path ${Stroked} d="M9.3 5.9 A9.8 9.8 0 0 1 12 5.5 C18 5.5 21.5 12 21.5 12 a17 17 0 0 1-2.7 3.5"/><path ${Stroked} d="M6.3 8 A16 16 0 0 0 2.5 12 S6 18.5 12 18.5 a9.6 9.6 0 0 0 3-.5"/>`,
    arrowUp:     `<path ${Stroked} d="M12 19 V5 M6 11 L12 5 L18 11"/>`,
    arrowDown:   `<path ${Stroked} d="M12 5 V19 M6 13 L12 19 L18 13"/>`,
    trash:       `<path ${Stroked} d="M4.5 7 H19.5 M9.5 7 V4.8 h5 V7 M6.5 7 l1 12.5 h9 L17.5 7"/><path ${Stroked} d="M10.3 10.5 v6 M13.7 10.5 v6"/>`,
    sliders:     `<path ${Stroked} d="M4 7 H20 M4 12 H20 M4 17 H20"/><circle ${Stroked} cx="9" cy="7" r="2"/><circle ${Stroked} cx="15" cy="12" r="2"/><circle ${Stroked} cx="8" cy="17" r="2"/>`,
    cube:        `<path ${Stroked} d="M12 3 L20.5 7.5 V16.5 L12 21 L3.5 16.5 V7.5 Z"/><path ${Stroked} d="M3.5 7.5 L12 12 L20.5 7.5 M12 12 V21"/>`,
    bucket:      `<path ${Stroked} d="M11 3 L20 12 L12 20 L3 11 Z"/><path ${Stroked} d="M18 16.5 c1.6 2.2 2.4 3.5 2.4 4.3 a2.4 2.4 0 0 1 -4.8 0 c0 -0.8 0.8 -2.1 2.4 -4.3 Z"/>`,
    // a circle half-filled — the universal "mask" read: a shape whose one half is painted through.
    mask:        `<circle ${Stroked} cx="12" cy="12" r="8.5"/><path d="M12 3.5 a8.5 8.5 0 0 1 0 17 Z" fill="currentColor"/>`
};

const Icon = (Name, Size = 15) => SvgWrap(GLYPH[Name] ?? "", Size);

//------------------------------------------------------------------------------------------------------------------------
//                                                     CHANNEL SCHEMA
//------------------------------------------------------------------------------------------------------------------------

// 🔴 SIX channels, not the inspector prototype's nine. Specular, clearcoat and ambient occlusion were
//    explicitly excluded from scope, and — more to the point — the engine has no storage for them: the
//    three RGBA8 atlases are fully spoken for. Showing a control for a channel the paint pass cannot
//    write would edit a value that never reaches a texel, which reads as "the slider does nothing".
//
// 📝 Driven off CHANNEL_ORDER/CHANNEL_SLOTS so the panel cannot drift from what the atlases store.
// 🔴 Keyed by the CHANNEL KEY, not by the display label. The emissive channel's key is "emission" while
//    its label is "Emissive", and an earlier revision keyed this table "emissive": the spread below then
//    resolved to undefined for that channel, so it fell through to the scalar branch of BuildChannelBody
//    with Min/Max/Step all undefined and offered an amount slider where a colour field belongs. Nothing
//    threw — a slider over an undefined range still renders — so the panel just quietly lost the ability
//    to author an emissive colour. The assert below is what stops the same typo returning.
const CHANNEL_EDIT = {
    baseColour: { Edit: "colour" },
    metallic:   { Edit: "scalar", Min: 0, Max: 1, Step: 0.01 },
    roughness:  { Edit: "scalar", Min: 0, Max: 1, Step: 0.01 },
    emission:   { Edit: "colour" },
    normal:     { Edit: "derived" },
    height:     { Edit: "scalar", Min: 0, Max: 1, Step: 0.01 }
};

const CHANNEL_PANELS = CHANNEL_ORDER.map((Key) => {
    const Edit = CHANNEL_EDIT[Key];
    if (!Edit) { console.error(`LayerInspector: CHANNEL_EDIT has no entry for channel "${Key}"`); }
    return {
        Key,
        Label: CHANNEL_LABEL[Key] ?? Key,
        Kind:  CHANNEL_SLOTS[Key]?.Kind ?? "scalar",
        ...Edit
    };
});

const DecimalsFor = (Step) => (Step >= 1 ? 0 : (Step >= 0.1 ? 1 : 2));

// The channels a layer paints, always as an array.
//
// 🔴 The inspector is handed LIVE `PaintLayer` objects, where the field is `Enabled` and is a **Set** —
//    `Channels` is an array that exists only on the serialized `ReadLayerStack()` snapshot. Reading
//    `Layer.Channels` off a live layer yields undefined and throws on `.includes`, which killed the whole
//    of RenderStack: the card opened with an empty list and no row ever appeared. Normalising in one place
//    keeps every call site blind to which of the two shapes it was given.
function ChannelsOf(Layer)
{
    if (Array.isArray(Layer.Channels)) { return Layer.Channels; }
    if (Layer.Enabled instanceof Set)  { return [...Layer.Enabled]; }
    return Array.isArray(Layer.Enabled) ? Layer.Enabled : [];
}

// Does this layer paint the given channel? Order-independent, so it works for either shape.
const PaintsChannel = (Layer, Key) => ChannelsOf(Layer).includes(Key);

// The mask component a target-selection is aimed at, whatever its category — not only the paintable one.
// 🔴 Distinct from ResolveMaskPaintTarget, which returns a component ONLY when it can take a brush stroke.
//    The right pane has to show a generator/fill/levels component's SETTINGS even though no dab lands in it,
//    so selection and paint-routing are two different questions with two different resolvers.
function SelectedMaskComponent(Layer)
{
    const Mask = Layer?.Mask;
    if (!Mask || !Mask.Enabled || !Mask.FocusToken) { return null; }
    return Mask.Components.find((C) => C.Token === Mask.FocusToken) ?? null;
}

// A layer's channel value as a CSS colour, or null when the channel is off. Used for the row thumbnail.
function SwatchOf(Layer)
{
    if (!PaintsChannel(Layer, "baseColour")) { return null; }
    const Value = Layer.Values.baseColour;
    if (!Array.isArray(Value)) { return null; }
    return ColourToHex(Value);
}

function ColourToHex(Triple)
{
    const Byte = (V) => Math.max(0, Math.min(255, Math.round((V ?? 0) * 255)))
                            .toString(16).padStart(2, "0");
    return "#" + Byte(Triple[0]) + Byte(Triple[1]) + Byte(Triple[2]);
}

function HexToColour(Hex)
{
    const Packed = parseInt(Hex.slice(1), 16);
    return [((Packed >> 16) & 255) / 255, ((Packed >> 8) & 255) / 255, (Packed & 255) / 255];
}

// What a Value-mode ("solid") channel looks like, as a CSS fill plus a human reading of the value.
// Returns null when the channel has no solid to show, which is the caller's cue to fall through to
// "nothing here yet".
//
// 🔴 A DERIVED channel returns null even though it has a value in Layer.Values. `normal` is computed from
//    height at shade time (CHANNEL_SLOTS.normal: Atlas null, Kind "derived"); painting a flat swatch for it
//    would assert a stored solid that does not exist, and the honest answer is the empty state.
// 🔴 A scalar is shown as GREY, not as a colour. metallic/roughness/height occupy one component of the shared
//    Material atlas, and ChannelPreview splats that component to grey for the real readback (Component 0/1/2
//    with the same shader) — so a scalar solid must match, or the solid and painted previews of one channel
//    would disagree on what the value looks like.
function SolidPreviewOf(Layer, Key)
{
    const Slot = CHANNEL_SLOTS[Key];
    if (!Slot || Slot.Atlas === null) { return null; }

    const Value = Layer?.Values?.[Key];

    if (Slot.Kind === "colour")
    {
        if (!Array.isArray(Value)) { return null; }
        const Hex = ColourToHex(Value);
        return { Css: Hex, Reading: Hex.toUpperCase() };
    }

    if (Slot.Kind === "scalar")
    {
        if (typeof Value !== "number" || !Number.isFinite(Value)) { return null; }
        const Unit  = Math.max(0, Math.min(1, Value));
        const Level = Math.round(Unit * 255);
        return { Css: `rgb(${Level},${Level},${Level})`, Reading: Unit.toFixed(3) };
    }

    return null;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MARKUP
//------------------------------------------------------------------------------------------------------------------------

// 📝 One template string rather than a tree of createElement calls: the shell is static and every
//    dynamic part is filled by a Render* method against an id, exactly as the prototype does it.
const InspectorMarkup = `
<div class="summon-veil" data-part="Veil"></div>
<div class="summon-menu" data-part="Menu">
  <div class="menu-view">
    <div class="menu-track" data-part="Track">

      <div class="menu-slide">
        <div class="slide-pane rail">
          <div class="pane-head">
            <span class="h-ic" data-part="StackIcon"></span>
            <span class="h-txt">
              <span class="h-lbl">Layers</span>
              <span class="fn">Suzanne</span>
            </span>
            <span class="h-n" data-part="Tally">0</span>
          </div>
          <div class="stack-add-wrap" data-part="AddHost"></div>
          <div class="search-wrap">
            <label class="search">
              <span data-part="SearchIcon"></span>
              <input type="text" data-part="Filter" placeholder="Filter…" spellcheck="false">
            </label>
          </div>
          <div class="stack-body no-bar" data-part="StackBody"></div>
          <div class="pane-foot" data-part="StackFoot"></div>
        </div>

        <div class="slide-pane detail">
          <div class="pane-head stepper" data-part="AdvanceHead" title="Channels">
            <span class="h-ic" data-part="MetaIcon"></span>
            <span class="h-txt">
              <span class="h-lbl" data-part="MetaName">Nothing selected</span>
              <span class="fn" data-part="MetaClass">—</span>
            </span>
            <span class="h-step" data-part="AdvanceStep"></span>
          </div>
          <div class="meta-body no-bar" data-part="MetaBody"></div>
          <div class="pane-foot" data-part="MetaFoot"></div>
        </div>
      </div>

      <div class="menu-slide">
        <div class="slide-pane rail">
          <div class="pane-head stepper back" data-part="ReturnHead" title="Back to the layer stack">
            <span class="h-back" data-part="ReturnStep"></span>
            <span class="h-txt">
              <span class="h-lbl">Back</span>
              <span class="fn">Layers</span>
            </span>
          </div>
          <div class="ident-body no-bar" data-part="IdentityBody"></div>
          <div class="pane-foot" data-part="IdentityFoot"></div>
        </div>

        <div class="slide-pane detail">
          <div class="pane-head">
            <span class="h-ic" data-part="ChannelIcon"></span>
            <span class="h-txt">
              <span class="h-lbl" data-part="ChannelName">Nothing selected</span>
              <span class="fn" data-part="ChannelSub">—</span>
            </span>
          </div>
          <div class="inspect-viewport">
            <div class="inspect-track">
              <div class="inspect-pane">
                <div class="prop-body no-bar" data-part="ChannelBody"></div>
              </div>
            </div>
          </div>
          <div class="pane-foot" data-part="ChannelFoot"></div>
        </div>
      </div>

    </div>
  </div>
</div>`;

// 🔴 +15% over the original 548×372. Kept in lockstep with the .summon-menu width/height in the CSS — the
//    clamp maths here and the fixed box there must agree or the card is placed for the wrong footprint.
const MenuWidth  = 630;
const MenuHeight = 428;
const MenuPad    = 14;

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// The summoned layer manager.
//
// `Stack` is the live LayerStack; `Commands` is the command surface (the same one the harness drives, so
// the UI and the probe cannot diverge); `OnChange` is called after any mutation so the host can redraw.
// `Capture` renders a channel thumbnail and resolves to a data address, or null when the channel has no
// storage yet. `CaptureComposite` does the same for the RESOLVED stack — every visible layer flattened —
// which is what the combined card on the channels slide shows.
//
// 🔴 Every mutation goes through `Commands`, never through the layer objects directly. Writing
//    `Layer.Opacity = x` from here would change the value without bumping the stack revision, and the
//    compositor early-returns on an unchanged revision — the slider would move and the viewport would not.
export class LayerInspector
{
    constructor(Host, Stack, Commands, OnChange, Capture, CaptureComposite, CaptureMask, MaskPreview)
    {
        this.Stack    = Stack;
        this.Commands = Commands;
        this.OnChange = OnChange ?? (() => {});
        // 📝 Defaults to "no preview available" rather than throwing, so the panel is still usable when
        //    constructed without a GPU capture path (as an isolated DOM test would).
        this.Capture  = Capture ?? (async () => null);
        // 📝 Same default for the same reason. Kept a SEPARATE entry point rather than a null-token
        //    convention on Capture above: the two read different sources (one layer's atlas vs the
        //    compositor's resolved pair) and the host caches them under different keys, so folding them
        //    into one signature would put a `Token === null` branch in every caller of both.
        this.CaptureComposite = CaptureComposite ?? (async () => null);
        // 📝 A third entry point, for the same reason the second one is separate: this reads ONE layer's
        //    resolved mask atlas, which is neither a channel of that layer nor part of the flattened stack.
        this.CaptureMask = CaptureMask ?? (async () => null);
        // The mask-on-mesh viewport preview: `MaskPreview.Set(On)` toggles it, `MaskPreview.On()` reports it.
        // 📝 Defaulted to a no-op that always reports off, so a DOM-only construction renders the toggle in
        //    its off state and clicking it does nothing, rather than throwing on a missing host seam.
        this.MaskPreview = MaskPreview ?? { Set: () => false, On: () => false };

        this.Root = document.createElement("div");
        this.Root.className = "layer-inspector";
        this.Root.innerHTML = InspectorMarkup;
        Host.appendChild(this.Root);

        this.Part = {};
        for (const Node of this.Root.querySelectorAll("[data-part]"))
        {
            this.Part[Node.dataset.part] = Node;
        }

        this.OpenState  = false;
        this.FilterTerm = "";
        this.Collapsed  = new Set();
        // Tokens whose inline expand is open. Focusing a row opens it; its caret toggles it shut/open
        // again without changing focus.
        this.Expanded   = new Set();
        // 🔴 Tokens whose inline expand has ALREADY played its open animation. RenderStack rebuilds the rail on
        //    every committed value, so this is what stops the reveal replaying on each tick of a slider drag
        //    inside the expand itself — the same job `CardFoldState::Seeded` does in PropertyPanelBase.cpp,
        //    where the fold snaps to its intent the first frame a card is seen. A token is dropped from here the
        //    moment its expand closes, so the NEXT open animates again.
        this.SeededExpands = new Set();
        // 🔴 Which colour pickers are open, keyed `Token/Channel`. A picker MUST outlive a pane rebuild: every
        //    commit runs Refresh(), which wipes the pane and builds a fresh BuildColourField, so an open state
        //    held only in the widget's own DOM was lost on the first value written. That is why picking a
        //    colour read as "it confirms and closes" — the panel was not dismissing the picker, it was
        //    discarding and rebuilding it closed. Keyed by channel as well as token because the mask detail
        //    pane and the layer pane can each show a colour row for a different channel of the same layer.
        this.OpenPickers = new Set();
        // 🔴 The right pane's [ Layer | Mask ] carousel position per layer: "layer" shows the layer's own
        //    paint properties, "mask" slides to the mask editor. This is ALSO the paint target — with the
        //    Mask tab live and a mask component focused, strokes route to that component; otherwise they
        //    route to the layer's channels. Keyed by token so a layer keeps its tab when another is focused;
        //    defaults to "layer". Coerced back to "layer" by TabOf whenever the mask is gone.
        this.PaintTab = new Map();
        // 📝 The tab each layer's carousel showed on its LAST build. BuildTargetCarousel compares the
        //    current tab against this to fire the slide micro-animation only on a real Layer<->Mask
        //    switch, never on the Refresh rebuilds that follow every unrelated mutation.
        this.LastTab  = new Map();
        this.RowDragged   = false;
        // The open list's bookkeeping. Uniform for EVERY dropdown, because they are all portalled to
        // <body> on open (see BuildDropdown) — OpenList is always the .dd-list itself, never its wrapper,
        // so the outside-press and scroll guards can test containment the same way for all of them.
        //   OpenList   — the .dd-list currently up
        //   OpenHost   — the .dropdown that owns it and must get it back on close; null for the add list
        //   OpenAnchor — the trigger wearing the .listopen / .flipped join classes (the "+ Add Layer" button;
        //                a field dropdown's capsule head has no border to join with, so it wears neither)
        this.OpenList     = null;
        this.OpenHost     = null;
        this.OpenAnchor   = null;
        this.AddLayerList = null;

        // 🔴 A depth count, not a boolean: a slider drag and the colour picker's plane can both be live at
        //    once (two pointers), and a boolean cleared by whichever released first would unlatch the other
        //    mid-scrub. While non-zero, Refresh() defers instead of rebuilding — see Refresh().
        this.Scrubbing       = 0;
        this.RefreshDeferred = false;

        LiveInspectors.add(this);

        this.Part.StackIcon.innerHTML   = Icon("stack", 18);
        this.Part.SearchIcon.innerHTML  = Icon("search", 14);
        this.Part.AdvanceStep.innerHTML = Icon("chevron", 13);
        this.Part.ReturnStep.innerHTML  = Icon("chevronLeft", 14);

        this.Bind();
    }

    get Open()      { return this.OpenState; }
    get OnChannels() { return this.Part.Track.classList.contains("to-inspect"); }

    Bind()
    {
        this.Part.Veil.onpointerdown       = () => this.Hide();
        this.Part.AdvanceHead.onclick      = () => this.ShowChannels();
        this.Part.ReturnHead.onclick       = () => this.ShowStack();
        this.Part.Filter.oninput           = () => { this.FilterTerm = this.Part.Filter.value.trim().toLowerCase();
                                                     this.RenderStack(); };
        // 📝 Keystrokes inside the filter must not reach the page's shortcut handler, or typing "e" in
        //    the box toggles the eraser.
        this.Part.Filter.onkeydown         = (Event) => {
            Event.stopPropagation();
            if (Event.key === "Escape") { this.Part.Filter.blur(); }
        };

        // A dropdown is position:fixed and placed by script, so any scroll or outside press must close it.
        //
        // 🔴 A press on the trigger that OWNS the open list is NOT an outside press. This capturing handler
        //    runs before the trigger's own onclick, so without this exclusion it would close the list on
        //    pointerdown, the click's toggle guard would then read "already closed" and re-open it — the list
        //    could never be dismissed by re-clicking its button. Let such a press fall through to the trigger.
        // 📝 OpenList is always the .dd-list itself (never a .dropdown wrapper) and always a child of <body>
        //    while open, so this containment test reads the same for every list — the add-layer picker and
        //    each field dropdown alike.
        this.CloseListOnOutside = (Event) => {
            this.ClosePickersOnOutside(Event);
            if (!this.OpenList) { return; }
            if (this.OpenList.contains(Event.target)) { return; }
            if (Event.target.closest && Event.target.closest(".sf-add, .dd-head")) { return; }
            this.CloseLists();
        };
        document.addEventListener("pointerdown", this.CloseListOnOutside, true);

        // A position:fixed list is placed against a one-time measurement of its trigger, so any scroll that
        // MOVES that trigger leaves the list stranded — hence closing on scroll.
        //
        // 🔴 But a scroll INSIDE the open list is not that: now that a clamped list scrolls its own options,
        //    this capturing handler would fire on the list's own scroll and shut it the instant the user
        //    reached for an option below the fold. The list's own scrolling moves nothing it is anchored to,
        //    so it is excluded.
        this.CloseListOnScroll = (Event) => {
            if (this.OpenList && this.OpenList.contains(Event.target)) { return; }
            if (Event.target instanceof Element && Event.target.closest(".dd-list")) { return; }
            this.CloseLists();
        };
        document.addEventListener("scroll", this.CloseListOnScroll, true);
    }

    //--------------------------------------------------------------------------------------------------
    //                                        VISIBILITY
    //--------------------------------------------------------------------------------------------------

    // Open the card, clamped so it never lands off-screen.
    Show(X, Y)
    {
        const Left = Math.min(Math.max(MenuPad, X), window.innerWidth  - MenuWidth  - MenuPad);
        const Top  = Math.min(Math.max(MenuPad, Y), window.innerHeight - MenuHeight - MenuPad);
        this.Part.Menu.style.left = `${Left}px`;
        this.Part.Menu.style.top  = `${Top}px`;
        this.Part.Menu.classList.add("open");
        this.Part.Veil.classList.add("open");
        this.OpenState = true;
        this.Refresh();
    }

    // Open centred over the host element.
    ShowCentred()
    {
        const Box = this.Root.parentElement.getBoundingClientRect();
        this.Show(Box.left + Math.round((Box.width - MenuWidth) / 2), Box.top + 52);
    }

    Hide()
    {
        this.Part.Menu.classList.remove("open");
        this.Part.Veil.classList.remove("open");
        this.Part.Track.classList.remove("to-inspect");
        this.OpenState = false;
        this.CloseLists();
    }

    // Slide 2 is dual-purpose: the detail pane shows the layer's Channels when the carousel tab is "layer",
    // and the mask components deep-dive when it is "mask". Both share the same physical slide and the same
    // Identity rail; only the detail pane's content differs. RenderInspectSlide picks which to draw, so the
    // single Tab step lands on the right editor for whichever target the carousel is aimed at.
    ShowChannels()
    {
        this.CloseLists();
        this.Part.Track.classList.add("to-inspect");
        this.RenderInspectSlide();
    }

    // The mask CTA's destination. Force the carousel to Mask first (so a Tab from anywhere lands on the mask
    // detail, and returning to slide 1 shows the mask pane), then reuse the shared slide.
    ShowMaskDetail()
    {
        const Layer = this.Stack.Focus;
        if (Layer) { this.PaintTab.set(Layer.Token, "mask"); }
        this.CloseLists();
        this.Part.Track.classList.add("to-inspect");
        this.RenderInspectSlide();
    }

    // Fill slide 2 for the focused layer's current tab: Channels for "layer", mask deep-dive for "mask".
    RenderInspectSlide()
    {
        const Layer = this.Stack.Focus;
        if (Layer && this.TabOf(Layer) === "mask") { this.RenderMaskDetail(); }
        else                                        { this.RenderChannels(); }
        this.RenderIdentity();
    }

    ShowStack()
    {
        this.CloseLists();
        this.Part.Track.classList.remove("to-inspect");
        this.RenderProperties();
    }

    // Tab steps forward: closed → stack → channels → closed.
    Advance()
    {
        if (!this.OpenState)   { this.ShowCentred();  return; }
        if (!this.OnChannels)  { this.ShowChannels(); return; }
        this.Hide();
    }

    // Escape unwinds the same path one step at a time.
    Retreat()
    {
        if (this.OpenList)    { this.CloseLists(); return true; }
        if (!this.OpenState)  { return false; }
        if (this.OnChannels)  { this.ShowStack(); return true; }
        this.Hide();
        return true;
    }

    // Rebuild every pane. Called after a mutation and on open.
    //
    // 🔴 The preview generation is bumped FIRST, before anything is rebuilt. Every in-flight channel
    //    readback captured the previous value and drops its result on resolving, so a slow capture for a
    //    layer that is no longer focused cannot paint itself into the panel that replaced it.
    //
    // 🔴 A rebuild is REFUSED while a widget is being scrubbed — this is what cured "the sliders keep
    //    jamming". Every pane rebuild does `Body.innerHTML = ""`, which detaches the very `.slider` the
    //    pointer is captured on; the browser then fires no further pointermove at a node outside the
    //    document, so the knob froze wherever it was when the first value landed. The widget's own
    //    `Live` flag already suppressed the inspector's rebuild, but the HOST re-enters here through its
    //    LayerCommand choke point (which cannot see Live), so the guard has to live at this end. The
    //    deferred flag replays the skipped rebuild once on release, so nothing is lost.
    //
    // 🔴 Any open list is closed FIRST. While a list is open it is portalled to <body>, so it is no longer a
    //    child of the pane about to be wiped — the `Body.innerHTML = ""` below that used to dispose of it
    //    implicitly now leaves it floating on screen, anchored to a trigger that no longer exists, with item
    //    handlers closed over the pre-rebuild Layer. Closing here reparents it back and drops it with its
    //    host, which is the behaviour every call site already assumed.
    Refresh()
    {
        if (this.Scrubbing > 0) { this.RefreshDeferred = true; return; }
        this.RefreshDeferred = false;

        this.CloseLists();

        this.PreviewGeneration = (this.PreviewGeneration ?? 0) + 1;
        this.RenderStack();
        this.RenderProperties();
        this.RenderIdentity();
        // Slide 2's detail pane rebuilds to whichever editor the carousel tab selects, so a mutation while
        // the mask deep-dive is open leaves it open and current instead of snapping back to Channels.
        const Layer = this.Stack.Focus;
        if (Layer && this.TabOf(Layer) === "mask") { this.RenderMaskDetail(); }
        else                                        { this.RenderChannels(); }
    }

    // Push a command at the stack, then rebuild and tell the host to redraw.
    Apply(Command, Payload)
    {
        const Outcome = this.Commands(Command, Payload);
        this.Refresh();
        this.OnChange();
        return Outcome;
    }

    //--------------------------------------------------------------------------------------------------
    //                                        STACK PANE
    //--------------------------------------------------------------------------------------------------

    RenderStack()
    {
        const Body = this.Part.StackBody;
        Body.innerHTML = "";

        // 📝 Drop expand state for tokens the stack no longer has. Nothing else prunes these two Sets, so
        //    deleting an open layer leaves its token in both of them for the rest of the session. Tokens are
        //    never re-issued (LayerStack's TokenSequence only increments), so this cannot mis-seed a future
        //    layer — it is housekeeping, and it keeps `Expanded` an honest answer to "is this row open" for
        //    anything that later reads it. Pruned against the WHOLE stack, not the filtered view: a layer
        //    hidden by the filter still exists and keeps its open state for when the filter clears.
        const Live = new Set(this.Stack.Layers.map((L) => L.Token));
        for (const Token of this.Expanded)      { if (!Live.has(Token)) { this.Expanded.delete(Token); } }
        for (const Token of this.SeededExpands) { if (!Live.has(Token)) { this.SeededExpands.delete(Token); } }

        const Layers = this.Stack.Layers.filter(
            (L) => !this.FilterTerm || L.Name.toLowerCase().includes(this.FilterTerm));

        for (const Layer of Layers)
        {
            Body.appendChild(this.BuildRow(Layer));

            // 🔴 A row's settings drop open INLINE, right beneath it in the rail, when its caret is open —
            //    tracked in this.Expanded, independent of focus. It is skipped while a filter is active — a
            //    filtered stack is a lookup, not an editing surface, and an accordion inside it fights the
            //    row the user is scanning for.
            if (this.Expanded.has(Layer.Token) && !this.FilterTerm)
            {
                const Expand = this.BuildExpand(Layer);

                // 🔴 The reveal animates only on a NEWLY opened expand — the CSS equivalent of the C++
                //    `Fold->Seeded` guard, which snaps the fold to its intent the first frame a card is seen so
                //    "a card shown already-open does not animate in". RenderStack runs on every committed value,
                //    so without this the expand would replay its whole open animation on each tick of a slider
                //    drag inside itself. `Seeded` is recorded per token and only ever set here.
                if (!this.SeededExpands.has(Layer.Token))
                {
                    this.SeededExpands.add(Layer.Token);
                    Expand.classList.add("folding");
                    // 🔴 Two frames, not one. The node is appended THIS frame, so it has no committed style yet;
                    //    clearing the class in the same frame (or in a single rAF, which can still coalesce with
                    //    the insertion's first style resolution) gives the browser no `0fr` start value to
                    //    interpolate from and the fold hard-cuts — the same reason the portalled .dd-list defers
                    //    its `.open`.
                    requestAnimationFrame(() => requestAnimationFrame(() => {
                        Expand.classList.remove("folding");
                    }));
                }
                Body.appendChild(Expand);
            }
        }

        if (Body.children.length === 0)
        {
            Body.innerHTML = '<div class="empty-state">No layers match.<br>Clear the filter to see the stack.</div>';
        }

        this.Part.Tally.textContent = this.Stack.Count;

        this.RenderStackFoot();
    }

    // The stack rail's chrome: the "+ Add Layer" affordance sits ABOVE the filter (it opens one option
    // per layer kind — Paint / Fill / Material / Generator, straight from LAYER_KIND_ORDER), and the
    // footer carries only the layer tally.
    //
    // 🔴 A new layer is created through the "add" verb, never by touching the stack directly — the verb
    //    seeds a fill/material's content and re-flattens, which a bare Stack.Add would skip.
    RenderStackFoot()
    {
        // ---- "+ Add Layer", above the filter -----------------------------------------------------------
        const AddHost = this.Part.AddHost;
        AddHost.innerHTML = "";

        const AtCap = this.Stack.Count >= LayerCapacity;
        const Add   = document.createElement("div");
        Add.className = "sf-add" + (AtCap ? " disabled" : "");
        Add.innerHTML = `${Icon("plus", 12)}<span>Add Layer</span>`;
        Add.title = AtCap ? `Layer cap of ${LayerCapacity} reached` : "Add a layer";

        if (!AtCap)
        {
            Add.onclick = (Event) => {
                Event.stopPropagation();
                this.OpenAddLayerList(Add);
            };
        }
        AddHost.appendChild(Add);

        // ---- tally, in the footer ----------------------------------------------------------------------
        const Foot = this.Part.StackFoot;
        Foot.innerHTML = "";

        const Hidden = this.Stack.Layers.filter((L) => !L.Shown).length;
        const Tally  = document.createElement("span");
        Tally.className = "sf-tally";
        Tally.innerHTML =
            `<span class="pf-strong">${this.Stack.Count}</span> / ${LayerCapacity}` +
            (Hidden ? `<span class="pf-dot">·</span><span>${Hidden} hidden</span>` : "");
        Foot.appendChild(Tally);
    }

    // The add-layer kind list, dropped below the "+ Add Layer" button. Reuses the fixed-position dropdown
    // machinery so it escapes the scrolling rail and closes on any outside press.
    OpenAddLayerList(Anchor)
    {
        // 🔴 "Already open" is a re-click on a list that EXISTS, so the toggle-shut branch. The earlier form
        //    `this.OpenList === this.AddLayerList` was true when BOTH were null — i.e. on the very first click
        //    with nothing open — so it closed before ever building the list and the picker never appeared,
        //    which read as "Add Layer does nothing". Requiring AddLayerList to be non-null fixes the toggle.
        const WasOpen = this.AddLayerList !== null && this.OpenList === this.AddLayerList;
        this.CloseLists();
        if (WasOpen) { return; }

        const List = document.createElement("div");
        List.className = "dd-list sf-add-list";

        for (const Kind of LAYER_KIND_ORDER)
        {
            const Item = document.createElement("div");
            Item.className = "dd-item";
            Item.innerHTML =
                `<span class="sf-swatch" style="background:${KindTint(Kind)}"></span>` +
                `<span>${KindLabel(Kind)}</span>`;
            Item.onclick = (Event) => {
                Event.stopPropagation();
                this.CloseLists();
                this.AddLayer(Kind);
            };
            List.appendChild(Item);
        }

        document.body.appendChild(List);
        this.AddLayerList = List;
        this.OpenList     = List;
        // 📝 No OpenHost: this list has no persistent .dropdown to be handed back to, so CloseLists removes
        //    it rather than reparenting it.
        this.OpenHost     = null;
        // The button keeps its pressed styling for as long as its list is up, so the pair reads as one
        // object rather than a card that happens to be floating near a button.
        Anchor.classList.add("listopen");
        this.OpenAnchor = Anchor;
        // Recomputed below once the drop direction is known — the joined edge is the bottom one when the
        // list drops down, the top one when it flips above.
        Anchor.classList.remove("flipped");

        // 🔴 Flush to the button — width matched EXACTLY and top pinned to the button's edge with NO gap,
        //    because the old +4px offset plus the list's own full border-radius made the picker read as a
        //    second card floating below the first. The CSS squares off the joined corners and drops the
        //    shared edge, so button + list become one continuous surface. PlaceList measures the real
        //    height, so the list flips or scrolls rather than running off the viewport.
        const DropsDown = this.PlaceList(List, Anchor, 132);
        Anchor.classList.toggle("flipped", !DropsDown);

        // 🔴 The open class lands on the NEXT frame, not now. Setting it in the same frame as the insert
        //    gives the browser no start value to interpolate from, so the transition is skipped entirely
        //    and the list simply appears — the "no micro animation" fault. One rAF commits the closed
        //    state first, so the change to .open actually animates.
        requestAnimationFrame(() => List.classList.add("open"));
    }

    // Create a layer of the given kind through the add verb.
    //
    // 🔴 A fill or material is FLOODED at full coverage across every channel — a base material wants the
    //    whole atlas including the UV gutters — while a paint or generator layer starts empty. The Flood
    //    payload and the default channel set both follow from the kind, so the kind is the single input.
    AddLayer(Kind)
    {
        const Flood    = Boolean(LAYER_KINDS[Kind]?.Flooded);
        const Channels = DefaultChannels(Kind, null, null);
        this.Apply("add", {
            Name:     `NEW_${KindLabel(Kind)}`,
            Kind:     Kind,
            Channels: Channels,
            Flood:    Flood
        });
    }

    BuildRow(Layer)
    {
        const Row = document.createElement("div");
        const IsOpen = this.Expanded.has(Layer.Token);
        Row.className = "stack-row" +
            (Layer.Token === this.Stack.FocusToken ? " active" : "") +
            (IsOpen ? " expanded" : "") +
            (Layer.Shown ? "" : " layer-muted");
        Row.dataset.token = Layer.Token;

        // 🔴 The rail tag carries the LAYER's colour, not its classification tint. Keyed to the kind it was the
        //    same blue on every material layer, so the marker distinguished nothing in the one place it exists to
        //    distinguish. The glyph inside .sr-thumb keeps the kind tint — the two markers now answer different
        //    questions: the tag says WHICH layer, the glyph says WHAT KIND.
        const Tint   = LayerHue(Layer);
        const Swatch = SwatchOf(Layer);
        const Thumb  = Swatch
            ? `<span class="sr-thumb-fill" style="background:${Swatch}"></span>`
            : ClassificationSvg(Layer.Classification, 15);

        Row.innerHTML =
            `<span class="sr-tag" style="background:${Tint}"></span>` +
            `<span class="sr-thumb">${Thumb}</span>` +
            `<span class="sr-text">` +
              `<span class="sr-name"></span>` +
              `<span class="sr-meta">${CLASSIFICATION_LABEL[Layer.Classification]} · ${Layer.Blend} · ` +
                `${ChannelsOf(Layer).length} ch</span>` +
            `</span>` +
            `<span class="sr-opacity" title="Drag to adjust opacity">${Layer.Opacity}%</span>` +
            `<span class="sr-visibility" title="${Layer.Shown ? "Hide" : "Show"}">` +
              `${Icon(Layer.Shown ? "eyeOpen" : "eyeOff", 14)}</span>` +
            `<span class="sr-caret" title="${IsOpen ? "Collapse" : "Expand"}">` +
              `${Icon("chevronDown", 12)}</span>`;

        // 🔴 The name is assigned as TEXT, not interpolated into the markup above. A layer name is
        //    user-typed, and a name containing "<" would otherwise be parsed as markup — at best the
        //    name vanishes, at worst the row's own handlers are replaced by injected ones.
        Row.querySelector(".sr-name").textContent = Layer.Name;

        Row.onclick = (Event) => {
            if (this.RowDragged) { this.RowDragged = false; return; }
            if (Event.target.closest(".sr-visibility") || Event.target.closest(".sr-opacity") ||
                Event.target.closest(".sr-caret")) { return; }
            // Focusing a row opens its expand so the settings are there to edit right away.
            this.Expanded.add(Layer.Token);
            if (Layer.Token !== this.Stack.FocusToken) { this.Apply("focus", { Token: Layer.Token }); }
            else                                       { this.RenderStack(); }
        };
        // 🔴 The caret toggles the inline expand WITHOUT touching focus, so a row can be opened to read
        //    its settings without stealing the paint target from the layer currently being stroked.
        Row.querySelector(".sr-caret").onclick = (Event) => {
            Event.stopPropagation();
            if (this.Expanded.has(Layer.Token)) { this.CollapseExpand(Layer.Token); }
            else
            {
                this.Expanded.add(Layer.Token);
                this.RenderStack();
            }
        };
        Row.ondblclick = (Event) => {
            if (Event.target.closest(".sr-visibility") || Event.target.closest(".sr-opacity")) { return; }
            this.BeginRename(Layer, Row.querySelector(".sr-name"));
        };
        // 🔴 Right-click inside the card focuses a row; it never opens a second menu, and it must
        //    swallow the event so the page's own right-click summon does not fire underneath.
        Row.oncontextmenu = (Event) => {
            Event.preventDefault();
            Event.stopPropagation();
            if (Layer.Token !== this.Stack.FocusToken) { this.Apply("focus", { Token: Layer.Token }); }
        };
        Row.querySelector(".sr-visibility").onclick = (Event) => {
            Event.stopPropagation();
            this.Apply("show", { Token: Layer.Token, Shown: !Layer.Shown });
        };
        this.BindOpacityDrag(Row.querySelector(".sr-opacity"), Layer);
        if (!this.FilterTerm) { this.BindRowDrag(Row, Layer); }

        return Row;
    }

    //--------------------------------------------------------------------------------------------------
    //                                   RIGHT-PANE CAROUSEL TAB
    //--------------------------------------------------------------------------------------------------

    // Which carousel panel the right pane shows for a layer: "layer" or "mask".
    //
    // 🔴 This is the carousel POSITION only — purely which panel is on screen. It is NOT the paint target and
    //    it does NOT coerce for a missing mask: the user can slide to the Mask panel on a layer with no mask,
    //    where they get the Create-mask affordance. Stroke routing is guarded independently by
    //    ResolveMaskPaintTarget (which returns null unless the mask is enabled and the component paintable),
    //    so a "mask" position with no mask can never send a dab into a mask that is not there.
    TabOf(Layer)
    {
        return this.PaintTab.get(Layer.Token) === "mask" ? "mask" : "layer";
    }

    // Fold an open inline expand SHUT on screen, then drop it. A close has to animate out of a node that still
    // exists, which a straight `Expanded.delete` + RenderStack cannot do — the rebuild removes the node in the
    // same frame, so the fold-out never renders and only the open direction was ever animated.
    //
    // 🔴 The intent is cleared FIRST, so any RenderStack racing this (a commit landing mid-fold) already agrees
    //    the row is closed and will not rebuild the expand back into place behind the animation.
    // 🔴 `transitionend` is filtered to grid-template-rows: .se-body transitions opacity and transform over the
    //    same duration and its events bubble, so an unfiltered handler would fire up to three times and rebuild
    //    the rail twice more. The timeout is the backstop for the case the event never arrives at all — a
    //    display:none ancestor or a reduced-motion setting can skip the transition entirely, and without it the
    //    expand would sit collapsed-but-present forever.
    CollapseExpand(Token)
    {
        this.Expanded.delete(Token);
        this.SeededExpands.delete(Token);

        // 🔴 Scoped to THIS token's expand, not the first one in the rail. Expanded is a Set, so several rows can
        //    be open at once, and a bare `.stack-expand` query would fold whichever happened to come first in
        //    the DOM while the row the user actually clicked stayed open.
        const Expand = this.Part.StackBody
            .querySelector(`.stack-expand[data-token="${Token}"]:not(.folding)`);
        if (!Expand) { this.RenderStack(); return; }

        // 📝 The owning row un-rotates its caret in step with the fold — see .stack-row.collapsing. The row is not
        //    rebuilt until the fold lands, so without this the chevron would hold its 180° and snap afterwards.
        const Owner = this.Part.StackBody.querySelector(`.stack-row[data-token="${Token}"]`);
        if (Owner) { Owner.classList.add("collapsing"); }

        let Landed = false;
        const Finish = () => {
            if (Landed) { return; }
            Landed = true;
            this.RenderStack();
        };
        Expand.addEventListener("transitionend", (Event) => {
            if (Event.target === Expand && Event.propertyName === "grid-template-rows") { Finish(); }
        });
        window.setTimeout(Finish, 400);
        Expand.classList.add("folding");
    }

    // The persistence handle a colour field uses to survive a pane rebuild — see BuildColourField. Returns a
    // tiny { Get, Set } pair over this.OpenPickers rather than the raw Set, so the widget stays ignorant of how
    // (or where) the flag is stored and cannot reach the rest of the panel's state.
    // Dismiss any open colour picker on a press outside it — the behaviour every colour picker has, and what
    // the user asked for: the picker used to be dismissable ONLY by re-clicking its own bar.
    //
    // 🔴 A press on the picker's OWN surfaces is not an outside press. That includes the plane, the hue rail and
    //    the hex input — a scrub starts with a pointerdown inside .picker, and closing on it would shut the
    //    picker the instant the drag began. The bar is excluded too: it owns the toggle, and closing here first
    //    would let its click re-open what this just closed (the same re-entry the .dd-head exclusion guards).
    // 🔴 Cleared from OpenPickers as well as the DOM, or the next rebuild would faithfully restore the picker
    //    this press just dismissed.
    ClosePickersOnOutside(Event)
    {
        const Target = Event.target;
        if (!(Target instanceof Element)) { return; }
        if (Target.closest(".picker, .colorbar")) { return; }

        // 📝 Queried off the document, not a pane root: the two colour rows live in different panes (the layer
        //    body and the mask detail) and only one can be open at a time in practice, so one sweep covers both.
        for (const Open of document.querySelectorAll(".picker.open"))
        {
            Open.classList.remove("open");
        }
        this.OpenPickers.clear();
    }

    PickerState(Token, Channel)
    {
        const Key = `${Token}/${Channel}`;
        return {
            Get: ()      => this.OpenPickers.has(Key),
            Set: (Value) => { if (Value) { this.OpenPickers.add(Key); } else { this.OpenPickers.delete(Key); } },
        };
    }

    // Slide the carousel to a panel. Selecting "layer" clears the mask focus so no stray stroke lands in a
    // component while the layer's own channels are the visible target; "mask" leaves the focus alone (a
    // following component pick sets it).
    //
    // 🔴 The focus clear goes through the command surface, never a bare field write — the mask focus is
    //    stroke-routing state the StrokeDriver reads, and a direct write would not bump the revision the
    //    paint path checks. When nothing routing-related changes we only re-render, avoiding a needless
    //    revision bump that would restart in-flight channel previews.
    SetTab(Layer, Tab)
    {
        this.PaintTab.set(Layer.Token, Tab);

        // Switching to Layer with a mask component focused clears that focus so no stray dab lands in it while
        // the layer's own channels are the visible target. That routes through the command surface (revision
        // bump the StrokeDriver needs) and rebuilds — the slide there is a fresh render, which is acceptable
        // because clearing focus is a real state change the panel must reflect.
        if (Tab === "layer" && Layer.Mask?.FocusToken)
        {
            this.Apply("maskFocusComponent", { Token: Layer.Token, Component: null });
            return;
        }

        // The common case is a pure position change with nothing routing-related to commit. Rather than
        // rebuild the subtree (which would drop a freshly-built thumb at its destination with nothing to
        // transition from), slide the LIVE toggle and track in place so both the thumb underline and the
        // panes glide. Fall back to a full render if the carousel is not currently mounted.
        if (this.SlideCarousel(Tab))
        {
            this.LastTab.set(Layer.Token, Tab);
            this.Part.AdvanceHead.title = Tab === "mask" ? "Mask components" : "Channels";
            return;
        }
        this.RenderProperties();
    }

    // Drive the mounted carousel to a tab without rebuilding it: flip the toggle's data-tab (slides the thumb),
    // swap the active segment, and translate the track. Returns false when no carousel is on screen.
    SlideCarousel(Tab)
    {
        const Toggle = this.Part.MetaBody.querySelector(".mc-toggle");
        const Track  = this.Part.MetaBody.querySelector(".mc-track");
        if (!Toggle || !Track) { return false; }

        Toggle.dataset.tab = Tab;
        for (const Seg of Toggle.querySelectorAll(".mc-seg")) { Seg.classList.remove("active"); }
        const Index = Tab === "mask" ? 1 : 0;
        Toggle.querySelectorAll(".mc-seg")[Index]?.classList.add("active");
        Track.style.transform = Tab === "mask" ? "translateX(-50%)" : "translateX(0)";
        return true;
    }

    //--------------------------------------------------------------------------------------------------
    //                                     INLINE EXPAND
    //--------------------------------------------------------------------------------------------------

    // The active layer's settings, dropped open in the rail directly under its row: the same paint-menu
    // controls Studio carries — Visible, Blend, Opacity, base Colour — plus the mask section.
    //
    // 🔴 Every control here drives the SAME command surface the detail pane uses, so the two never diverge:
    //    a change made in the inline expand shows in the properties pane and vice versa on the next Refresh.
    BuildExpand(Layer)
    {
        const Host = document.createElement("div");
        // 📝 `active` mirrors the owning row's focus class so the accent spine runs the FULL height of the card.
        //    The expand is the row's SIBLING, not its child, so the row's own ::before spine stops at the head;
        //    the lower half has to draw its own continuing segment. See .stack-expand.active::before.
        Host.className = "stack-expand" + (Layer.Token === this.Stack.FocusToken ? " active" : "");
        // 📝 Stamped so CollapseExpand can fold THIS row's expand rather than the first one in the rail — several
        //    rows can be open at once (Expanded is a Set).
        Host.dataset.token = Layer.Token;
        const Clip = document.createElement("div"); Clip.className = "se-clip";
        const Body = document.createElement("div"); Body.className = "se-body";

        const Line = (Label, Field) => {
            const Row = document.createElement("div");
            Row.className = "se-row";
            const Tag = document.createElement("span"); Tag.className = "se-k"; Tag.textContent = Label;
            const Val = document.createElement("span"); Val.className = "se-v"; Val.appendChild(Field);
            Row.appendChild(Tag); Row.appendChild(Val);
            return Row;
        };

        // ---- Visible -----------------------------------------------------------------------------------
        Body.appendChild(Line("Visible", BuildSwitch(Layer.Shown, (On) =>
            this.Apply("show", { Token: Layer.Token, Shown: On }))));

        // ---- Blend -------------------------------------------------------------------------------------
        Body.appendChild(Line("Blend", this.BuildDropdown(BLEND_MODES, Layer.Blend, (Pick) =>
            this.Apply("blend", { Token: Layer.Token, Blend: Pick }))));

        // ---- Opacity -----------------------------------------------------------------------------------
        Body.appendChild(Line("Opacity", BuildSlider({
            Min: 0, Max: 100, Step: 1, Value: Layer.Opacity, Unit: "%",
            OnInput: (Value, Live) => {
                this.Commands("opacity", { Token: Layer.Token, Opacity: Value });
                const Pill = this.Part.StackBody
                    .querySelector(`.stack-row[data-token="${Layer.Token}"] .sr-opacity`);
                if (Pill) { Pill.textContent = `${Value}%`; }
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        })));

        // ---- Colour ------------------------------------------------------------------------------------
        // 🔴 Only offered where the layer paints baseColour, and it edits the AUTHORED value — the ink a
        //    dab deposits on a paint layer, the flooded fill on a fill/material. A layer with no baseColour
        //    channel (a height-only generator, say) has no colour to author, so the row is simply omitted
        //    rather than shown editing a value that reaches no atlas.
        if (PaintsChannel(Layer, "baseColour"))
        {
            const Current = Array.isArray(Layer.Values.baseColour)
                ? ColourToHex(Layer.Values.baseColour) : "#808080";
            // 📝 Labelled "Paint", not "Colour". With the identity Tag row directly below, two rows both called
            //    "Colour" would be indistinguishable — and these two do genuinely different things: this one
            //    changes the pixels the layer deposits, the other only changes its marker in the rail.
            Body.appendChild(Line("Paint", BuildColourField(Current, (Hex, Live) => {
                this.Commands("value", { Token: Layer.Token, Channel: "baseColour", Value: HexToColour(Hex) });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }, this.PickerState(Layer.Token, "baseColour"))));
        }

        // ---- Tag (the layer's identity colour) ---------------------------------------------------------
        // 🔴 Offered on EVERY layer, unlike Paint above: the rail tag is presentation, so it applies to a
        //    height-only generator with no baseColour channel exactly as much as to a paint layer.
        // 🔴 Committed only on RELEASE (`if (Live) { return; }`). Every other field here scrubs live because the
        //    user is watching the surface change, but this one's only visible effect is the rail tag, and
        //    repainting the whole rail on each pointermove of a hue drag rebuilds the very expand the picker
        //    lives inside — it would tear the picker out from under the pointer mid-scrub.
        Body.appendChild(Line("Tag", BuildColourField(LayerHue(Layer), (Hex, Live) => {
            if (Live) { return; }
            this.Apply("layerColour", { Token: Layer.Token, Colour: Hex });
        }, this.PickerState(Layer.Token, "layerColour"))));

        // ---- the mask editor ---------------------------------------------------------------------------
        // 🔴 The paint-target choice moved OUT of here and onto the RIGHT pane's [ Layer | Mask ] carousel.
        //    The left expand is once again just the layer's own settings plus its mask editor; picking which
        //    surface a stroke lands on is the carousel's job, not this rail's.
        this.BuildMaskSection(Body, Layer);

        Clip.appendChild(Body);
        Host.appendChild(Clip);
        return Host;
    }

    //--------------------------------------------------------------------------------------------------
    //                                       MASK SECTION
    //--------------------------------------------------------------------------------------------------

    // The layer's mask editor, ported from Studio: an enable affordance while off, and while on a preview
    // reading fill+invert, the White/Black/Invert toolbar, an opacity slider, the component stack and an
    // add-component picker. Every control drives a mask verb on the shared command surface.
    BuildMaskSection(Body, Layer)
    {
        const Mask = Layer.Mask;

        Body.appendChild(SectionLabel2("Mask"));

        // ---- empty state: no mask yet ------------------------------------------------------------------
        // A layer without an enabled mask shows a single "Add mask" affordance rather than the full editor.
        // Enabling one is a mask verb so the stack revision bumps and the composite picks the mask up.
        if (!Mask || !Mask.Enabled)
        {
            const Add = document.createElement("div");
            Add.className = "msk-add";
            Add.innerHTML = `${Icon("mask", 13)}<span>Add mask</span>`;
            Add.onclick = () => this.Apply("mask", { Token: Layer.Token, Enabled: true });
            Body.appendChild(Add);
            return;
        }

        // ---- the mask ITSELF, as a picture --------------------------------------------------------------
        // 🔴 This is the mask's own greyscale content, read back from its resolved atlas — not a swatch of
        //    the fill setting. The fill swatch below says what the mask STARTED as; only this says what it
        //    IS after the components have run and a brush has been over it. Without it there is no way to
        //    see a mask being painted at all: the mask never appears in the viewport (the compositor uses it
        //    as a weight, so its effect is "the layer beneath shows through", which is not a picture of the
        //    mask) and it is not one of the PBR channels the Combined card tiles.
        Body.appendChild(this.BuildMaskPreview(Layer));

        // ---- preview + remove --------------------------------------------------------------------------
        const Base   = MaskFillValue(Mask);
        const Light  = Mask.Invert ? 1 - Base : Base;
        const Shade  = Math.round(Light * 255);
        const HeadRow = document.createElement("div");
        HeadRow.className = "msk-row";
        HeadRow.innerHTML =
            `<span class="msk-prev"><span class="msk-grad" ` +
              `style="background:rgb(${Shade},${Shade},${Shade})"></span></span>` +
            `<span class="msk-info">` +
              `<span class="msk-nm"></span>` +
              `<span class="msk-meta">${Mask.Fill === "black" ? "Black" : "White"} fill` +
                `${Mask.Invert ? " · inverted" : ""} · ${Mask.Components.length} comp</span>` +
            `</span>` +
            `<span class="mc-x" title="Delete mask" style="margin-left:auto">×</span>`;
        HeadRow.querySelector(".msk-nm").textContent = "Layer mask";
        // Deleting the mask drops the carousel back to Layer — there is no mask left to edit or aim at.
        HeadRow.querySelector(".mc-x").onclick = () => {
            this.PaintTab.set(Layer.Token, "layer");
            this.Apply("mask", { Token: Layer.Token, Enabled: false });
        };
        Body.appendChild(HeadRow);

        // ---- White / Black / Invert toolbar ------------------------------------------------------------
        const Bar = document.createElement("div");
        Bar.className = "msk-toolbar";

        const Tool = (Label, On, Run) => {
            const El = document.createElement("div");
            El.className = "msk-tool" + (On ? " on" : "");
            El.textContent = Label;
            El.onclick = Run;
            return El;
        };
        Bar.appendChild(Tool("White", Mask.Fill !== "black",
            () => this.Apply("maskFill", { Token: Layer.Token, Fill: "white" })));
        Bar.appendChild(Tool("Black", Mask.Fill === "black",
            () => this.Apply("maskFill", { Token: Layer.Token, Fill: "black" })));
        Bar.appendChild(Tool("Invert", Mask.Invert === true,
            () => this.Apply("maskInvert", { Token: Layer.Token, Invert: !Mask.Invert })));
        Body.appendChild(Bar);

        // ---- mask opacity ------------------------------------------------------------------------------
        const OpacityRow = document.createElement("div");
        OpacityRow.className = "se-row";
        OpacityRow.innerHTML = `<span class="se-k">Strength</span>`;
        const OpacityVal = document.createElement("span"); OpacityVal.className = "se-v";
        OpacityVal.appendChild(BuildSlider({
            Min: 0, Max: 100, Step: 1, Value: Mask.Opacity ?? 100, Unit: "%",
            OnInput: (Value, Live) => {
                this.Commands("maskOpacity", { Token: Layer.Token, Opacity: Value });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        }));
        OpacityRow.appendChild(OpacityVal);
        Body.appendChild(OpacityRow);

        // ---- component stack ---------------------------------------------------------------------------
        this.BuildMaskComponents(Body, Layer);
    }

    // The mask's resolved content as a greyscale tile, plus the on-mesh preview toggle beside it.
    //
    // 🔴 Synchronous return with a late fill, matching BuildChannelPreview and BuildCompositePreview: every
    //    caller of this reaches it through Refresh(), which no one awaits, so returning a promise here would
    //    make the pane build around an element that does not exist yet.
    // 🔴 Guarded by the same PreviewGeneration counter as the channel tiles. A mask readback resolves after
    //    an await, by which point the focus may have moved — and a mask tile is a plain grey field with no
    //    landmarks, so the WRONG layer's mask is entirely indistinguishable from the right one's.
    BuildMaskPreview(Layer)
    {
        const Host = document.createElement("div");
        Host.className = "msk-preview";

        const Tile = document.createElement("div");
        Tile.className = "cp-tile msk-tile";

        const Side = document.createElement("div");
        Side.className = "msk-side";

        const Note = document.createElement("div");
        Note.className = "cp-note";
        Note.textContent = "Reading…";

        // ---- the on-mesh toggle -------------------------------------------------------------------------
        // 🔴 Reads its state from the host rather than from a field on this panel. The mode is a property of
        //    the VIEWPORT, and the panel is rebuilt on every mutation while the viewport's mode survives —
        //    a local flag would reset to "off" on the next slider tick while the model stayed red.
        const On     = this.MaskPreview.On() === true;
        const Toggle = document.createElement("div");
        Toggle.className = "msk-onmesh" + (On ? " on" : "");
        Toggle.innerHTML = `${Icon("mask", 12)}<span></span>`;
        Toggle.lastChild.textContent = On ? "On the model" : "Show on model";
        Toggle.title = On
            ? "Showing this mask over the model. Click to return to the shaded view."
            : "Paint the mask over the model in the viewport: white reveals, red is hidden.";
        Toggle.onclick = () => {
            this.MaskPreview.Set(!this.MaskPreview.On());
            this.Refresh();
        };

        Side.appendChild(Toggle);
        Side.appendChild(Note);

        Host.appendChild(Tile);
        Host.appendChild(Side);

        const Generation = this.PreviewGeneration ?? 0;

        this.CaptureMask(Layer.Token).then((Preview) => {
            if ((this.PreviewGeneration ?? 0) !== Generation) { return; }
            if (!Tile.isConnected) { return; }

            if (!Preview)
            {
                // 🔴 A flat swatch of the fill, NOT a checkerboard. The mask atlas is allocated lazily, so a
                //    just-created mask has no storage — but unlike an unpainted channel that state is not
                //    transparency: the compositor reads MaskFillValue for it, so the mask genuinely IS a
                //    uniform white (or black) field. The checker idiom would claim there is nothing there.
                const Level = MaskFillValue(Layer.Mask);
                const Shade = Math.round((Layer.Mask.Invert ? 1 - Level : Level) * 255);
                Tile.classList.add("cp-solid");
                Tile.style.background = `rgb(${Shade},${Shade},${Shade})`;
                Note.textContent = "Uniform — nothing painted into this mask yet.";
                return;
            }

            Tile.style.backgroundImage = `url(${Preview.Image})`;
            // MeanInk over a greyscale tile IS the mask's mean coverage, so it can be reported as a reading
            // rather than left as an opaque number only a probe looks at.
            Note.textContent = `${Preview.Extent}² · `
                + `${Math.round(Preview.MeanInk * 100)}% revealed`;
        });

        return Host;
    }

    // The mask's ordered component stack, plus the add-component picker.
    BuildMaskComponents(Body, Layer)
    {
        const Mask = Layer.Mask;

        const Comps = document.createElement("div");
        Comps.className = "msk-comps";

        for (const Component of Mask.Components)
        {
            Comps.appendChild(this.BuildMaskComponent(Layer, Component));
        }
        Body.appendChild(Comps);

        // The add-component picker: one option per category (paint / fill / generator / levels).
        Body.appendChild(this.BuildDropdown(
            MASK_COMPONENT_ORDER.map((C) => MASK_COMPONENT_CATEGORY[C].Label),
            "Add component",
            (Pick) => {
                const Category = MASK_COMPONENT_ORDER.find(
                    (C) => MASK_COMPONENT_CATEGORY[C].Label === Pick);
                if (Category) { this.Apply("maskAddComponent", { Token: Layer.Token, Category }); }
            }));
    }

    // One entry in the mask component SELECTION list: a radio dot, glyph, name + category, and a remove ×.
    //
    // 🔴 The whole row is now a single-SELECT: clicking it aims the paint target at this component, whatever
    //    its category — Mask.FocusToken carries the selection, and the right pane on Tab shows that
    //    component's settings. For a paint component the selection also routes the brush into it (the
    //    StrokeDriver's ResolveMaskPaintTarget still keys off exactly this token); for a generator / fill /
    //    levels component the selection is edit-only, since those own no atlas a dab can land in. The
    //    per-component sliders moved OUT of this list and into the right pane, so the list stays a scannable
    //    picker rather than a wall of controls.
    BuildMaskComponent(Layer, Component)
    {
        const Category = MASK_COMPONENT_CATEGORY[Component.Category] ?? MASK_COMPONENT_CATEGORY.fill;
        const Paintable = Category.Paintable === true;
        const Selected  = Layer.Mask.FocusToken === Component.Token;

        const Host = document.createElement("div");
        Host.className = "msk-comp" + (Selected ? " sel" : "");

        const Head = document.createElement("div");
        Head.className = "msk-row";
        Head.style.margin = "0";
        Head.innerHTML =
            `<span class="mc-radio"></span>` +
            `<span class="mc-ico">${Icon("mask", 12)}</span>` +
            `<span class="mc-tx"><span class="mc-nm"></span>` +
              `<span class="mc-md">${Category.Label}` +
                `${Selected ? (Paintable ? " · painting" : " · editing") : ""}</span></span>`;
        Head.querySelector(".mc-nm").textContent = Component.Name;

        const Kill = document.createElement("span");
        Kill.className = "mc-x";
        Kill.style.marginLeft = "auto";
        Kill.textContent = "×";
        Kill.onclick = (Event) => {
            Event.stopPropagation();
            this.Apply("maskRemoveComponent", { Token: Layer.Token, Component: Component.Token });
        };
        Head.appendChild(Kill);
        Host.appendChild(Head);

        // Selecting toggles: clicking the selected component deselects it (target falls back to the mask as a
        // whole — nothing focused). Clicking another selects it.
        Host.onclick = () => this.Apply("maskFocusComponent",
            { Token: Layer.Token, Component: Selected ? null : Component.Token });

        return Host;
    }

    // The opacity pill scrubs horizontally.
    //
    // 📝 The live drag writes straight to the pill's own text and only commits through the command
    //    surface on release. Committing on every pointer move would rebuild the whole stack pane under
    //    the cursor mid-drag, which detaches the element the pointer is captured on.
    BindOpacityDrag(Pill, Layer)
    {
        Pill.onpointerdown = (Event) => {
            Event.stopPropagation();
            Event.preventDefault();
            const StartX = Event.clientX;
            const Start  = Layer.Opacity;
            let   Value  = Start;
            Pill.setPointerCapture(Event.pointerId);

            const Move = (Motion) => {
                Value = Math.max(0, Math.min(100, Start + Math.round((Motion.clientX - StartX) / 2)));
                Pill.textContent = `${Value}%`;
                this.Commands("opacity", { Token: Layer.Token, Opacity: Value });
                this.OnChange();
            };
            const Up = () => {
                Pill.removeEventListener("pointermove", Move);
                Pill.removeEventListener("pointerup", Up);
                if (Value !== Start) { this.Refresh(); }
            };
            Pill.addEventListener("pointermove", Move);
            Pill.addEventListener("pointerup", Up);
        };
    }

    // Press and drag vertically to restack.
    //
    // 🔴 The drop is applied as repeated single-step Reorder calls, because that is the only reordering
    //    primitive the stack exposes. Splicing this.Stack.Layers directly would move the row without
    //    bumping the revision, so the composite would keep the old order while the list showed the new.
    BindRowDrag(Row, Layer)
    {
        Row.addEventListener("pointerdown", (Event) => {
            if (Event.button !== 0) { return; }
            if (Event.target.closest(".sr-visibility") || Event.target.closest(".sr-opacity")) { return; }

            const StartY = Event.clientY;
            const StartX = Event.clientX;
            let Dragging = false;
            let Marker   = null;

            const Move = (Motion) => {
                if (!Dragging)
                {
                    if (Math.abs(Motion.clientY - StartY) < 5 && Math.abs(Motion.clientX - StartX) < 5) { return; }
                    Dragging = true;
                    this.RowDragged = true;
                    Row.setPointerCapture(Motion.pointerId);
                    Row.classList.add("dragging");
                    Marker = document.createElement("div");
                    Marker.className = "drop-line";
                }
                const Others = [...this.Part.StackBody.querySelectorAll(".stack-row")].filter((R) => R !== Row);
                let Placed = false;
                for (const Other of Others)
                {
                    const Box = Other.getBoundingClientRect();
                    if (Motion.clientY < Box.top + Box.height / 2)
                    {
                        this.Part.StackBody.insertBefore(Marker, Other);
                        Placed = true;
                        break;
                    }
                }
                if (!Placed) { this.Part.StackBody.appendChild(Marker); }
            };

            const Up = () => {
                Row.removeEventListener("pointermove", Move);
                Row.removeEventListener("pointerup", Up);
                if (!Dragging) { return; }
                Row.classList.remove("dragging");

                // Where the marker sits, in row terms, is the destination index.
                let Destination = 0;
                for (const Node of this.Part.StackBody.children)
                {
                    if (Node === Marker) { break; }
                    if (Node.classList.contains("stack-row") && Node !== Row) { Destination += 1; }
                }
                Marker.remove();

                const From = this.Stack.IndexOf(Layer.Token);
                // Direction −1 raises (toward index 0), matching the stack's own convention.
                const Step = Destination < From ? -1 : 1;
                for (let At = From; At !== Destination; At += Step)
                {
                    if (!this.Stack.Reorder(Layer.Token, Step)) { break; }
                }
                this.Stack.Touch();
                this.Refresh();
                this.OnChange();
            };

            Row.addEventListener("pointermove", Move);
            Row.addEventListener("pointerup", Up);
        });
    }

    // Rename in place on the row label.
    BeginRename(Layer, Label)
    {
        if (!Label) { return; }
        const Previous = Layer.Name;
        Label.classList.add("editing");
        Label.contentEditable = "true";
        Label.focus();

        const Range = document.createRange();
        Range.selectNodeContents(Label);
        const Selection = window.getSelection();
        Selection.removeAllRanges();
        Selection.addRange(Range);

        const Commit = () => {
            Label.contentEditable = "false";
            Label.classList.remove("editing");
            const Next = Label.textContent.trim();
            if (Next && Next !== Previous) { this.Apply("rename", { Token: Layer.Token, Name: Next }); }
            else                           { Label.textContent = Previous; }
        };
        Label.onblur = Commit;
        // 📝 Keystrokes are swallowed so Enter does not reach the page and "e" does not toggle erase.
        Label.onkeydown = (Event) => {
            Event.stopPropagation();
            if (Event.key === "Enter")  { Event.preventDefault(); Label.blur(); }
            if (Event.key === "Escape") { Label.textContent = Previous; Label.blur(); }
        };
    }

    //--------------------------------------------------------------------------------------------------
    //                                     PROPERTIES PANE
    //--------------------------------------------------------------------------------------------------

    RenderProperties()
    {
        const Layer = this.Stack.Focus;
        const Body  = this.Part.MetaBody;
        Body.innerHTML = "";

        if (!Layer)
        {
            this.Part.MetaName.textContent  = "Nothing selected";
            this.Part.MetaClass.textContent = "—";
            this.Part.MetaIcon.innerHTML    = Icon("stack", 17);
            Body.innerHTML = '<div class="empty-state">No layer focused.<br>Pick one in the stack.</div>';
            this.Part.MetaFoot.innerHTML = "<span>—</span>";
            return;
        }

        const Tint = Hue(Layer.Classification);
        this.Part.MetaName.textContent  = Layer.Name;
        this.Part.MetaClass.textContent = `${CLASSIFICATION_LABEL[Layer.Classification]} layer`;
        this.Part.MetaIcon.innerHTML    = ClassificationSvg(Layer.Classification, 17);

        // The Tab / stepper destination tracks the carousel tab: Channels for the layer, mask components for
        // the mask, so the head tooltip does not promise the wrong slide.
        this.Part.AdvanceHead.title =
            this.TabOf(Layer) === "mask" ? "Mask components" : "Channels";

        // 🔴 NO hero card here. A `.meta-hero` used to sit at the top of this body carrying the classification
        //    icon, the layer name and the classification label — which is exactly what the pane-head three lines
        //    up from here already shows (MetaIcon / MetaName / MetaClass). It read as the header followed by a
        //    smaller card repeating the header verbatim. The head is the single identity line for this pane; the
        //    body starts straight at the controls. (The rail's ident-chip/ident-name card on the OTHER slide is a
        //    different pane with no head of its own, so it stays.)

        // 🔴 A [ Layer | Mask ] carousel carries the two targets. Selecting a segment SLIDES the pane between
        //    the layer's own paint properties and the mask editor — the same translateX idiom the card uses on
        //    Tab, only two panels wide inside this pane. The carousel position is also the paint target: with
        //    the Mask panel live and a mask component focused, strokes route into that component; otherwise
        //    into the layer's channels. This replaces the left-rail segment the earlier revision carried.
        Body.appendChild(this.BuildTargetCarousel(Layer));

        this.Part.MetaFoot.innerHTML =
            `<span class="pf-hue" style="background:${Tint}"></span>` +
            `<span>${CLASSIFICATION_LABEL[Layer.Classification]}</span>` +
            `<span class="pf-dot">·</span><span class="pf-strong">${Layer.Opacity}%</span>` +
            `<span class="pf-spacer"></span><span>${Layer.Blend}</span>`;
    }

    // The [ Layer | Mask ] carousel: a two-segment toggle over a two-panel sliding track. Panel 0 is the
    // layer's own paint properties; panel 1 is the mask editor (or its create-mask empty state). The active
    // tab both slides the track and, for the Mask tab, decides whether a focused mask component takes the
    // paint stroke.
    //
    // 🔴 BOTH panels are built every render, not just the visible one. The slide is a CSS transform over a
    //    track that holds both side by side, so the off-screen panel has to exist for the transition to have
    //    something to move to — building only the active one would make the toggle a hard swap, not a slide.
    // 🔴 The track transform is applied inline from TabOf, not toggled by a class the click handler flips,
    //    because RenderProperties rebuilds this whole subtree on every Refresh: a class set by a prior click
    //    would be gone. Reading the persisted tab each build is what survives the rebuild.
    BuildTargetCarousel(Layer)
    {
        const Tab = this.TabOf(Layer);

        const Host = document.createElement("div");
        Host.className = "meta-carousel";

        // ---- the [ Layer | Mask ] toggle ---------------------------------------------------------------
        // A sliding-thumb switch: the two segments are static, and a single .mc-thumb underline slides between
        // them. The thumb's position is driven by the toggle's data-tab attribute (CSS translates it), so the
        // active marker glides left/right on a tab change rather than one underline snapping off and another on.
        const Toggle = document.createElement("div");
        Toggle.className = "mc-toggle";
        Toggle.dataset.tab = Tab;
        for (const [Key, Label] of [["layer", "Layer"], ["mask", "Mask"]])
        {
            const Seg = document.createElement("div");
            Seg.className = "mc-seg" + (Tab === Key ? " active" : "");
            Seg.textContent = Label;
            // 🔴 Guard against the LIVE tab, not the closure's build-time `Tab`. SetTab slides in place
            //    without rebuilding, so the captured `Tab` goes stale after the first switch; comparing to it
            //    would wedge the toggle (a second click reads the old value and no-ops). TabOf is the truth.
            Seg.onclick = () => { if (Key !== this.TabOf(Layer)) { this.SetTab(Layer, Key); } };
            Toggle.appendChild(Seg);
        }
        const Thumb = document.createElement("div");
        Thumb.className = "mc-thumb";
        Toggle.appendChild(Thumb);
        Host.appendChild(Toggle);

        // ---- the sliding track -------------------------------------------------------------------------
        const View  = document.createElement("div"); View.className  = "mc-view";
        const Track = document.createElement("div"); Track.className = "mc-track";
        Track.style.transform = Tab === "mask" ? "translateX(-50%)" : "translateX(0)";

        const LayerPane = document.createElement("div"); LayerPane.className = "mc-pane";
        const MaskPane  = document.createElement("div"); MaskPane.className  = "mc-pane";

        // Fire the incoming-pane lift only when the tab actually flipped since the last build — a Refresh
        // that rebuilds this subtree without a tab change must not re-run the entrance.
        const Previous = this.LastTab.get(Layer.Token);
        if (Previous !== undefined && Previous !== Tab)
        {
            (Tab === "mask" ? MaskPane : LayerPane).classList.add("mc-incoming");
        }
        this.LastTab.set(Layer.Token, Tab);

        this.RenderLayerProperties(LayerPane, Layer);
        this.RenderMaskPane(MaskPane, Layer);

        Track.appendChild(LayerPane);
        Track.appendChild(MaskPane);
        View.appendChild(Track);
        Host.appendChild(View);

        return Host;
    }

    // The Mask carousel panel. This renders the SAME full mask editor the left expand carries — preview +
    // remove, the White/Black/Invert toolbar, the Strength slider, the component list and the add-component
    // picker — so the two are guaranteed identical: both go through BuildMaskSection, there is no second copy
    // to drift. With no mask at all it shows a create-mask empty state; with a mask it also carries a
    // "Mask components" CTA that Tabs to the deep-dive slide, mirroring the Layer panel's Channels CTA.
    RenderMaskPane(Body, Layer)
    {
        const Mask = Layer.Mask;

        // ---- no mask: the create affordance ------------------------------------------------------------
        if (!Mask || !Mask.Enabled)
        {
            const Empty = document.createElement("div");
            Empty.className = "mc-empty";
            const Add = document.createElement("div");
            Add.className = "mc-create";
            Add.innerHTML = `${Icon("mask", 15)}<span>Create mask</span>`;
            Add.onclick = () => {
                // Creating the mask keeps the carousel on Mask so the pane fills in place.
                this.PaintTab.set(Layer.Token, "mask");
                this.Apply("mask", { Token: Layer.Token, Enabled: true });
            };
            Empty.appendChild(document.createElement("div")).className = "mc-empty-note";
            Empty.lastChild.textContent = "This layer has no mask. A mask carves where the layer applies.";
            Empty.appendChild(Add);
            Body.appendChild(Empty);
            return;
        }

        // ---- mask present: the identical full editor, then the deep-dive CTA ----------------------------
        this.BuildMaskSection(Body, Layer);

        const Cta = document.createElement("div");
        Cta.className = "meta-cta";
        Cta.innerHTML = `<span>Mask components</span>${Icon("chevron", 13)}<span class="cta-kbd">Tab</span>`;
        Cta.onclick = () => this.ShowMaskDetail();
        Body.appendChild(Cta);
    }

    // The layer-as-paint-target properties: the same Visible / Blend / Opacity / channel-count the pane has
    // always carried, plus the Actions and the Channels CTA. This is what shows when the target is the
    // layer's content rather than a mask component.
    RenderLayerProperties(Body, Layer)
    {
        Body.appendChild(SectionLabel("Layer paint", "sliders"));

        Body.appendChild(PropertyRow("Visible", BuildSwitch(Layer.Shown, (On) =>
            this.Apply("show", { Token: Layer.Token, Shown: On }))));

        Body.appendChild(PropertyRow("Blend", this.BuildDropdown(BLEND_MODES, Layer.Blend, (Pick) =>
            this.Apply("blend", { Token: Layer.Token, Blend: Pick }))));

        Body.appendChild(PropertyRow("Opacity", BuildSlider({
            Min: 0, Max: 100, Step: 1, Value: Layer.Opacity, Unit: "%",
            OnInput: (Value, Live) => {
                this.Commands("opacity", { Token: Layer.Token, Opacity: Value });
                const Pill = this.Part.StackBody
                    .querySelector(`.stack-row[data-token="${Layer.Token}"] .sr-opacity`);
                if (Pill) { Pill.textContent = `${Value}%`; }
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        })));

        const ChannelRow = document.createElement("div");
        ChannelRow.className = "meta-row";
        ChannelRow.innerHTML = `<span class="mr-k">Channels</span>` +
            `<span class="mr-v">${ChannelsOf(Layer).length} / ${CHANNEL_PANELS.length} active</span>`;
        Body.appendChild(ChannelRow);

        // The material's own settings, when this layer is one. Placed here, above Actions, so the thing the
        // layer IS reads before the things that can be done to it.
        this.RenderMaterialProperties(Body, Layer);

        Body.appendChild(SectionLabel("Actions"));
        Body.appendChild(this.BuildActions(Layer));

        const Cta = document.createElement("div");
        Cta.className = "meta-cta";
        Cta.innerHTML = `<span>Channels</span>${Icon("chevron", 13)}<span class="cta-kbd">Tab</span>`;
        Cta.onclick = () => this.ShowChannels();
        Body.appendChild(Cta);
    }

    // A material layer's preset identity plus a row per channel the preset authors.
    //
    // 🔴 Only the channels the PRESET declares get a row, read from the layer's own enabled set rather than
    //    from CHANNEL_PANELS. A plastic authors three channels and the emissive panel four; offering all six
    //    would put a height and an emissive slider on every material that writes neither, and editing one
    //    would change a value the layer's flood does not read — the same "the control does nothing" fault the
    //    mask pane's note calls out. `normal` can never appear: it is derived from height at shade time.
    //
    // 🔴 Silent no-op for every other kind, checked on the LAYER not the preset. A fill layer retargeted by
    //    the shelf becomes kind `material` and carries a preset; a paint layer never does. Keying this on
    //    `Layer.Preset` alone would show material rows on a layer whose content is its strokes.
    RenderMaterialProperties(Body, Layer)
    {
        if (Layer.Kind !== "material") { return; }

        const Preset = MATERIAL_PRESETS[Layer.Preset];

        // A material layer whose preset is missing is a real state, not an impossible one: the probe can add
        // one with no preset at all. Saying so beats rendering an empty section that looks like a load failure.
        if (!Preset)
        {
            Body.appendChild(SectionLabel("Material", "sliders"));
            const Note = document.createElement("div");
            Note.className = "tgt-hint";
            Note.textContent = "No preset assigned. Open the material shelf with Ctrl+Space and click a swatch.";
            Body.appendChild(Note);
            return;
        }

        // 📝 The preset's family rides in the section's tail slot, which is what that slot is for — it is the
        //    material's classification, not a second heading.
        Body.appendChild(SectionLabel("Material", "sliders", Preset.Family ?? ""));

        const Identity = document.createElement("div");
        Identity.className = "meta-row";
        Identity.innerHTML = `<span class="mr-k">Preset</span>` +
            `<span class="mr-v">${Preset.Label}</span>`;
        Body.appendChild(Identity);

        if (Preset.Note)
        {
            const Note = document.createElement("div");
            Note.className = "tgt-hint";
            Note.textContent = Preset.Note;
            Body.appendChild(Note);
        }

        // 🔴 Iterated over the preset's channel list INTERSECTED with what the layer actually has enabled, in
        //    the panel table's order. The preset is the authored intent and the enabled set is the live truth;
        //    they agree right after an assignment but the channels pane can disable one afterwards, and a row
        //    for a disabled channel would edit a value the flood then skips.
        const Rows = MaterialChannelRows(Layer.Preset)
            .filter((Key) => PaintsChannel(Layer, Key))
            .map((Key) => CHANNEL_PANELS.find((Panel) => Panel.Key === Key))
            .filter((Panel) => Panel && Panel.Edit !== "derived");

        for (const Panel of Rows)
        {
            if (Panel.Edit === "colour")
            {
                const Triple  = Layer.Values[Panel.Key];
                const Current = Array.isArray(Triple) ? ColourToHex(Triple) : "#808080";

                Body.appendChild(PropertyRow(Panel.Label, BuildColourField(Current, (Hex, Live) => {
                    this.Commands("value",
                        { Token: Layer.Token, Channel: Panel.Key, Value: HexToColour(Hex) });
                    this.OnChange();
                    if (!Live) { this.Refresh(); }
                // 📝 The picker's open state is keyed per token AND channel, so editing a base colour does not
                //    close an emissive picker on the rebuild that follows the commit.
                }, this.PickerState(Layer.Token, Panel.Key))));
                continue;
            }

            Body.appendChild(PropertyRow(Panel.Label, BuildSlider({
                Min: Panel.Min, Max: Panel.Max, Step: Panel.Step,
                Value: Number(Layer.Values[Panel.Key] ?? 0),
                OnInput: (Next, Live) => {
                    this.Commands("value", { Token: Layer.Token, Channel: Panel.Key, Value: Next });
                    this.OnChange();
                    if (!Live) { this.Refresh(); }
                }
            })));
        }

        // 🔴 Says the edit is the LAYER's, not the preset's. Both readings are plausible from the rows above,
        //    and the difference matters: another layer on the same preset is untouched, and the shelf's swatch
        //    keeps showing the authored material. Without this the user cannot tell whether they have just
        //    edited one layer or the whole library.
        const Scope = document.createElement("div");
        Scope.className = "tgt-hint";
        Scope.textContent = "Edits apply to this layer only. The preset in the shelf is unchanged.";
        Body.appendChild(Scope);
    }

    // The selected mask component's settings, keyed off its category. This is the "different target kinds
    // show different settings" the request asked for:
    //
    //   • paint     — a greyscale paint preview + component opacity; strokes land here.
    //   • generator — its real shader inputs (Scale / Contrast / Amount / Seed). 🔴 NOT invented AO / bevel /
    //                 cavity sliders: our generator is one procedural noise field, and a slider that reaches
    //                 no shader input reads as "the control does nothing". The section is named for the
    //                 procedural role; the four params are what actually drive the pass.
    //   • fill      — a Value slider, plus a Load-texture affordance shown DISABLED, because no bitmap-import
    //                 path exists yet. Disabled-with-a-reason beats a dead button that looks live.
    //   • levels    — Low / High remap of the accumulated mask.
    RenderComponentProperties(Body, Layer, Component)
    {
        const Category  = MASK_COMPONENT_CATEGORY[Component.Category] ?? MASK_COMPONENT_CATEGORY.fill;
        const Paintable = Category.Paintable === true;

        // A "painting into…" banner so the target is unmistakable, reusing the accent-ringed preview tile.
        const Banner = this.BuildTargetBanner(Layer, Component, Category);
        if (Banner) { Body.appendChild(Banner); }

        Body.appendChild(SectionLabel(`${Category.Label} mask`, "sliders"));

        // Every component carries its own opacity (how strongly this step mixes into the mask beneath it).
        Body.appendChild(PropertyRow("Opacity", BuildSlider({
            Min: 0, Max: 100, Step: 1, Value: Component.Opacity ?? 100, Unit: "%",
            OnInput: (Value, Live) => {
                this.Commands("maskComponentOpacity",
                    { Token: Layer.Token, Component: Component.Token, Opacity: Value });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        })));

        // The category's real parameters (fill / generator / levels). Paint has none — its content is the
        // strokes themselves.
        for (const Spec of MASK_COMPONENT_PARAMS[Component.Category] ?? [])
        {
            Body.appendChild(PropertyRow(Spec.Label, BuildSlider({
                Min: Spec.Min, Max: Spec.Max, Step: Spec.Step,
                Value: Number(Component.Params?.[Spec.Key] ?? Category.Defaults?.[Spec.Key] ?? Spec.Min),
                OnInput: (Value, Live) => {
                    this.Commands("maskComponentParam",
                        { Token: Layer.Token, Component: Component.Token, Key: Spec.Key, Value });
                    this.OnChange();
                    if (!Live) { this.Refresh(); }
                }
            })));
        }

        // Fill can, in a fuller build, take a bitmap. The affordance is shown so the intent is legible, but
        // disabled: there is no texture-import path in this prototype, and a live-looking button that does
        // nothing is worse than one that says why.
        if (Component.Category === "fill")
        {
            const Load = document.createElement("div");
            Load.className = "meta-cta disabled";
            Load.title = "Bitmap import is not available in this prototype.";
            Load.innerHTML = `<span>Load texture…</span>`;
            Body.appendChild(Load);
        }

        if (Paintable)
        {
            const Note = document.createElement("div");
            Note.className = "tgt-hint";
            Note.textContent = "Strokes paint this component in greyscale.";
            Body.appendChild(Note);
        }
    }

    // The accent-ringed "painting into…" banner atop the component-properties pane. Reuses the greyscale
    // fill+invert read of the mask so the surface a stroke lands on is visible, exactly as before — only now
    // it heads a full settings pane rather than sitting above the layer's own props.
    BuildTargetBanner(Layer, Component, Category)
    {
        const Mask = Layer.Mask;
        if (!Mask) { return null; }

        const Base  = MaskFillValue(Mask);
        const Light = Mask.Invert ? 1 - Base : Base;
        const Shade = Math.round(Light * 255);
        const Verb  = Category.Paintable === true ? "Painting mask" : "Editing mask";

        const Host = document.createElement("div");
        Host.className = "mask-paint-preview";
        Host.innerHTML =
            `<span class="mpp-tile" style="background:rgb(${Shade},${Shade},${Shade})"></span>` +
            `<span class="mpp-txt">` +
              `<span class="mpp-lbl">${Verb}</span>` +
              `<span class="mpp-nm"></span>` +
              `<span class="mpp-sub">${Category.Label} · ${Mask.Fill === "black" ? "Black" : "White"} fill` +
                `${Mask.Invert ? " · inverted" : ""}</span>` +
            `</span>`;
        Host.querySelector(".mpp-nm").textContent = Component.Name;
        return Host;
    }

    // 🔴 Delete is the ONE per-layer action left in the detail pane. Raise / Lower moved to the stack's
    //    own drag-reorder, and adding a layer moved to the "+ Add Layer" footer picker — leaving the
    //    right-hand action list to the single verb that has no other home.
    BuildActions(Layer)
    {
        const Host = document.createElement("div");
        Host.className = "meta-actions";

        const Item    = document.createElement("div");
        // 🔴 The stack refuses to remove its last layer, so the row is disabled rather than offered and
        //    then silently ignored.
        const Enabled = this.Stack.Count > 1;
        Item.className = "act-item danger" + (Enabled ? "" : " disabled");
        Item.innerHTML = `<span class="ic">${Icon("trash", 15)}</span><span>Delete</span>`;
        if (Enabled) { Item.onclick = () => this.Apply("remove", { Token: Layer.Token }); }
        Host.appendChild(Item);

        return Host;
    }

    //--------------------------------------------------------------------------------------------------
    //                                      IDENTITY PANE
    //--------------------------------------------------------------------------------------------------

    RenderIdentity()
    {
        const Layer = this.Stack.Focus;
        const Body  = this.Part.IdentityBody;
        Body.innerHTML = "";

        if (!Layer)
        {
            Body.innerHTML = '<div class="empty-state">No layer focused.</div>';
            this.Part.IdentityFoot.innerHTML = "<span>—</span>";
            return;
        }

        const Tint   = Hue(Layer.Classification);
        const Swatch = SwatchOf(Layer);

        const Chip = document.createElement("div");
        Chip.className = "ident-chip";
        Chip.innerHTML = Swatch
            ? `<span class="ic-fill" style="background:${Swatch}"></span>`
            : ClassificationSvg(Layer.Classification, 34);
        Body.appendChild(Chip);

        const Name = document.createElement("div");
        Name.className = "ident-name";
        Name.textContent = Layer.Name;
        Body.appendChild(Name);

        const Class = document.createElement("div");
        Class.className = "ident-class";
        Class.style.color = Tint;
        Class.textContent = CLASSIFICATION_LABEL[Layer.Classification];
        Body.appendChild(Class);

        const Facts = [
            ["Blend",    Layer.Blend],
            ["Opacity",  `${Layer.Opacity}%`],
            ["Visible",  Layer.Shown ? "Yes" : "No"],
            ["Channels", `${ChannelsOf(Layer).length} of ${CHANNEL_PANELS.length}`],
            ["Depth",    `${this.Stack.IndexOf(Layer.Token) + 1} of ${this.Stack.Count}`]
        ];
        for (const [Key, Value] of Facts)
        {
            const Row = document.createElement("div");
            Row.className = "ident-stat";
            Row.innerHTML = `<span>${Key}</span><span class="is-v"></span>`;
            Row.querySelector(".is-v").textContent = Value;
            Body.appendChild(Row);
        }

        // ---- the combined stack ---------------------------------------------------------------------
        // 🔴 The COMBINED result, not this layer's. Everything above in this pane describes the focused
        //    layer in isolation, and the right-hand pane breaks that layer down channel by channel — so
        //    nothing in the panel showed what the surface actually ends up looking like once the whole
        //    stack is flattened. That is the one thing the user is painting toward, and it belongs here
        //    rather than in the channel pane precisely because it is NOT a property of the focused layer.
        // 🔴 ...unless the carousel is aimed at the MASK, in which case the four PBR tiles are the wrong
        //    picture entirely. A mask is greyscale coverage; base colour, metallic, roughness and emissive
        //    say nothing about it, and showing them beside a mask editor invites reading them AS the mask —
        //    which is exactly the "it paints colour" confusion. The mask's own tile takes their place, so the
        //    identity rail always shows the thing the current target is being painted into.
        if (this.TabOf(Layer) === "mask" && Layer.Mask?.Enabled)
        {
            Body.appendChild(this.BuildMaskIdentityPreview(Layer));
        }
        else
        {
            Body.appendChild(this.BuildCompositePreview());
        }

        this.Part.IdentityFoot.innerHTML =
            `<span class="pf-hue" style="background:${Tint}"></span>` +
            `<span>${CLASSIFICATION_LABEL[Layer.Classification]}</span>`;
    }

    //--------------------------------------------------------------------------------------------------
    //                                    COMBINED PREVIEW
    //--------------------------------------------------------------------------------------------------

    // The flattened stack, one tile per stored PBR channel.
    //
    // 🔴 Built from CHANNEL_ORDER filtered by IsStoredChannel rather than from a literal list of four
    //    names. The resolved set is exactly the channels that have an atlas to resolve INTO, so deriving
    //    the tiles from the same predicate the compositor uses keeps this card from either missing a
    //    channel the engine gained or asking the host for a resolved atlas that does not exist.
    // 🔴 Synchronous return + late fill, for the same reason as BuildChannelPreview: RenderIdentity is
    //    called from Refresh(), which no caller awaits.
    // The identity rail's mask counterpart to the Combined card: the same card shell, carrying the mask's own
    // tile in place of the four PBR channel tiles.
    //
    // 📝 Reuses BuildMaskPreview rather than growing a second readback path, so the rail's tile and the mask
    //    editor's tile cannot disagree about what the mask holds — and the on-mesh toggle comes along with
    //    it, which is where the user is looking when they want to see the mask on the model.
    BuildMaskIdentityPreview(Layer)
    {
        const Host = document.createElement("div");
        Host.className = "composite-card";

        const Head = document.createElement("div");
        Head.className = "cc-head";
        Head.innerHTML =
            `<span class="cc-t">Mask</span>` +
            `<span class="cc-s">${Layer.Mask.Components.length} comp · ${Layer.Mask.Opacity ?? 100}%</span>`;
        Host.appendChild(Head);

        Host.appendChild(this.BuildMaskPreview(Layer));
        return Host;
    }

    BuildCompositePreview()
    {
        const Host = document.createElement("div");
        Host.className = "composite-card";

        const Head = document.createElement("div");
        Head.className = "cc-head";
        Head.innerHTML =
            `<span class="cc-t">Combined</span>` +
            `<span class="cc-s">${this.Stack.EnabledCount} of ${this.Stack.Count} visible</span>`;
        Host.appendChild(Head);

        const Grid = document.createElement("div");
        Grid.className = "cc-grid";
        Host.appendChild(Grid);

        const Generation = this.PreviewGeneration ?? 0;

        for (const Key of CHANNEL_ORDER)
        {
            if (!IsStoredChannel(Key)) { continue; }

            const Cell = document.createElement("div");
            Cell.className = "cc-cell";

            const Tile = document.createElement("div");
            Tile.className = "cp-tile cc-tile";

            const Label = document.createElement("div");
            Label.className = "cc-lbl";
            Label.textContent = CHANNEL_LABEL[Key] ?? Key;

            Cell.appendChild(Tile);
            Cell.appendChild(Label);
            Grid.appendChild(Cell);

            this.CaptureComposite(Key).then((Preview) => {
                if ((this.PreviewGeneration ?? 0) !== Generation) { return; }
                if (!Tile.isConnected) { return; }

                if (!Preview)
                {
                    // 🔴 Checker, matching the per-channel tiles. A resolved atlas ALWAYS exists once the
                    //    compositor has run, so a null here means no layer contributed to this channel —
                    //    which is transparency, the same state the per-channel tiles show a checker for.
                    Tile.classList.add("cp-clear");
                    return;
                }

                Tile.style.backgroundImage = `url(${Preview.Image})`;
                Tile.title = `${CHANNEL_LABEL[Key] ?? Key} · ${Preview.Extent}² resolved from the ${Preview.Atlas} atlas`;
            });
        }

        return Host;
    }

    //--------------------------------------------------------------------------------------------------
    //                                      CHANNELS PANE
    //--------------------------------------------------------------------------------------------------

    RenderChannels()
    {
        const Layer = this.Stack.Focus;
        const Body  = this.Part.ChannelBody;
        Body.innerHTML = "";

        if (!Layer)
        {
            this.Part.ChannelName.textContent = "Nothing selected";
            this.Part.ChannelSub.textContent  = "—";
            this.Part.ChannelIcon.innerHTML   = Icon("stack", 17);
            Body.innerHTML = '<div class="empty-state">No layer focused.</div>';
            this.Part.ChannelFoot.innerHTML = "<span>—</span>";
            return;
        }

        this.Part.ChannelName.textContent = Layer.Name;
        this.Part.ChannelSub.textContent  =
            `${CLASSIFICATION_LABEL[Layer.Classification]} · ${Layer.Blend}`;
        this.Part.ChannelIcon.innerHTML   = ClassificationSvg(Layer.Classification, 17);

        Body.appendChild(this.BuildChannelChips(Layer));
        for (const Panel of CHANNEL_PANELS)
        {
            if (PaintsChannel(Layer, Panel.Key)) { Body.appendChild(this.BuildChannelPanel(Layer, Panel)); }
        }

        this.Part.ChannelFoot.innerHTML =
            `<span class="pf-strong">${ChannelsOf(Layer).length}</span> channels` +
            `<span class="pf-spacer"></span><span>3 atlases</span>`;
    }

    //--------------------------------------------------------------------------------------------------
    //                                   MASK DEEP-DIVE PANE
    //--------------------------------------------------------------------------------------------------

    // The mask counterpart to RenderChannels, drawn into the SAME slide-2 detail pane. Where Channels lists
    // every painted channel as a collapsible panel, this lists every mask COMPONENT as a collapsible panel
    // whose body is that component's real settings (RenderComponentProperties) — the "same design as the
    // layer's Channels slide, but for the mask" the request asked for. Selecting a component's head aims the
    // paint target at it (mirroring the left list's single-select), while its chevron expands the settings.
    RenderMaskDetail()
    {
        const Layer = this.Stack.Focus;
        const Body  = this.Part.ChannelBody;
        Body.innerHTML = "";

        const Mask = Layer?.Mask;

        // Head + foot follow the channel pane's own conventions so the two slides read as siblings.
        this.Part.ChannelName.textContent = Layer ? `${Layer.Name} · Mask` : "Nothing selected";
        this.Part.ChannelSub.textContent  = Layer
            ? `${Mask && Mask.Enabled ? (Mask.Fill === "black" ? "Black" : "White") + " fill" : "No mask"}`
              + `${Mask?.Invert ? " · inverted" : ""}`
            : "—";
        this.Part.ChannelIcon.innerHTML   = Icon("mask", 17);

        if (!Mask || !Mask.Enabled)
        {
            Body.innerHTML = '<div class="empty-state">This layer has no mask.<br>Create one on the Mask tab.</div>';
            this.Part.ChannelFoot.innerHTML = "<span>—</span>";
            return;
        }

        // The mask-wide controls sit up top (the same White / Black / Invert + Strength the editor carries),
        // then every component gets its own expandable settings panel below.
        Body.appendChild(this.BuildMaskOverviewCard(Layer));
        for (const Component of Mask.Components)
        {
            Body.appendChild(this.BuildMaskComponentPanel(Layer, Component));
        }
        Body.appendChild(this.BuildMaskAddComponent(Layer));

        this.Part.ChannelFoot.innerHTML =
            `<span class="pf-strong">${Mask.Components.length}</span> components` +
            `<span class="pf-spacer"></span><span>${Mask.Opacity ?? 100}% strength</span>`;
    }

    // The collapsible summary card atop the deep-dive: the mask's fill / invert toolbar and strength, reusing
    // the channel-chips card shell so it matches the Channels slide's leading card.
    BuildMaskOverviewCard(Layer)
    {
        const Mask = Layer.Mask;

        const Card  = document.createElement("div"); Card.className  = "card";
        const Head  = document.createElement("div"); Head.className  = "card-head";
        Head.innerHTML = `<span class="ch-tw">${Icon("chevronDown", 10)}</span><span>Mask</span>` +
            `<span class="ch-n">${Mask.Components.length} comp</span>`;

        const Shell = document.createElement("div"); Shell.className = "card-shell";
        const Clip  = document.createElement("div"); Clip.className  = "card-body-inner";
        const Body  = document.createElement("div"); Body.className  = "card-body";

        const Bar = document.createElement("div");
        Bar.className = "msk-toolbar";
        const Tool = (Label, On, Run) => {
            const El = document.createElement("div");
            El.className = "msk-tool" + (On ? " on" : "");
            El.textContent = Label;
            El.onclick = Run;
            return El;
        };
        Bar.appendChild(Tool("White", Mask.Fill !== "black",
            () => this.Apply("maskFill", { Token: Layer.Token, Fill: "white" })));
        Bar.appendChild(Tool("Black", Mask.Fill === "black",
            () => this.Apply("maskFill", { Token: Layer.Token, Fill: "black" })));
        Bar.appendChild(Tool("Invert", Mask.Invert === true,
            () => this.Apply("maskInvert", { Token: Layer.Token, Invert: !Mask.Invert })));
        Body.appendChild(Bar);

        Body.appendChild(PropertyRow("Strength", BuildSlider({
            Min: 0, Max: 100, Step: 1, Value: Mask.Opacity ?? 100, Unit: "%",
            OnInput: (Value, Live) => {
                this.Commands("maskOpacity", { Token: Layer.Token, Opacity: Value });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        })));

        Clip.appendChild(Body);
        Shell.appendChild(Clip);
        Card.appendChild(Head);
        Card.appendChild(Shell);
        return Card;
    }

    // One mask component as a collapsible panel, mirroring BuildChannelPanel: a head that both selects the
    // component (aims the paint target) and toggles the settings body open, and a body carrying that
    // component's real parameters via RenderComponentProperties.
    BuildMaskComponentPanel(Layer, Component)
    {
        const Category  = MASK_COMPONENT_CATEGORY[Component.Category] ?? MASK_COMPONENT_CATEGORY.fill;
        const Paintable = Category.Paintable === true;
        const Selected  = Layer.Mask.FocusToken === Component.Token;
        const Key       = `mask:${Layer.Token}:${Component.Token}`;
        const Collapsed = this.Collapsed.has(Key);

        const Host = document.createElement("div");
        Host.className = "chan-panel mask-comp-panel" + (Collapsed ? " collapsed" : "") + (Selected ? " sel" : "");

        const Head = document.createElement("div");
        Head.className = "chan-head";
        Head.innerHTML =
            `<span class="ch-tw">${Icon("chevronDown", 9)}</span>` +
            `<span class="mc-radio"></span>` +
            `<span class="ch-title"></span>` +
            `<span class="ch-sub">${Category.Label}` +
              `${Selected ? (Paintable ? " · painting" : " · editing") : ""}</span>` +
            `<span class="mc-x" title="Remove component" style="margin-left:auto">×</span>`;
        Head.querySelector(".ch-title").textContent = Component.Name;

        Head.querySelector(".mc-x").onclick = (Event) => {
            Event.stopPropagation();
            this.Apply("maskRemoveComponent", { Token: Layer.Token, Component: Component.Token });
        };
        // The radio selects (aims the paint target); the chevron toggles the settings. Splitting them keeps a
        // component openable without stealing focus, and selectable without forcing it open.
        Head.querySelector(".mc-radio").onclick = (Event) => {
            Event.stopPropagation();
            this.Apply("maskFocusComponent",
                { Token: Layer.Token, Component: Selected ? null : Component.Token });
        };
        Head.querySelector(".ch-tw").onclick = (Event) => {
            Event.stopPropagation();
            if (this.Collapsed.has(Key)) { this.Collapsed.delete(Key); }
            else                          { this.Collapsed.add(Key); }
            Host.classList.toggle("collapsed", this.Collapsed.has(Key));
        };
        // Clicking the label body selects too, matching the left list where the whole row is the target.
        Head.onclick = () => this.Apply("maskFocusComponent",
            { Token: Layer.Token, Component: Selected ? null : Component.Token });

        const Shell = document.createElement("div"); Shell.className = "chan-shell";
        const Clip  = document.createElement("div"); Clip.className  = "chan-clip";
        const Body  = document.createElement("div"); Body.className  = "chan-body";
        this.RenderComponentProperties(Body, Layer, Component);
        Clip.appendChild(Body);
        Shell.appendChild(Clip);

        Host.appendChild(Head);
        Host.appendChild(Shell);
        return Host;
    }

    // The add-component picker at the foot of the deep-dive, one option per category.
    BuildMaskAddComponent(Layer)
    {
        const Wrap = document.createElement("div");
        Wrap.className = "mask-add-wrap";
        Wrap.appendChild(this.BuildDropdown(
            MASK_COMPONENT_ORDER.map((C) => MASK_COMPONENT_CATEGORY[C].Label),
            "Add component",
            (Pick) => {
                const Category = MASK_COMPONENT_ORDER.find(
                    (C) => MASK_COMPONENT_CATEGORY[C].Label === Pick);
                if (Category) { this.Apply("maskAddComponent", { Token: Layer.Token, Category }); }
            }));
        return Wrap;
    }

    BuildChannelChips(Layer)
    {
        const Card = document.createElement("div");
        Card.className = "card";

        const Head = document.createElement("div");
        Head.className = "card-head";
        Head.innerHTML = `<span class="ch-tw">${Icon("chevronDown", 10)}</span><span>Channels</span>` +
            `<span class="ch-n">${ChannelsOf(Layer).length} / ${CHANNEL_PANELS.length}</span>`;

        const Shell = document.createElement("div"); Shell.className = "card-shell";
        const Clip  = document.createElement("div"); Clip.className  = "card-body-inner";
        const Body  = document.createElement("div"); Body.className  = "card-body";

        const Chips = document.createElement("div");
        Chips.className = "chan-chips";

        for (const Panel of CHANNEL_PANELS)
        {
            if (!PaintsChannel(Layer, Panel.Key)) { continue; }
            const Chip = document.createElement("div");
            Chip.className = "chan-chip";
            Chip.innerHTML = `<span>${Panel.Label}</span><span class="cc-x" title="Drop channel">×</span>`;
            Chip.querySelector(".cc-x").onclick = (Event) => {
                Event.stopPropagation();
                this.Apply("channel", { Token: Layer.Token, Channel: Panel.Key, On: false });
            };
            Chips.appendChild(Chip);
        }
        Body.appendChild(Chips);

        const Idle = CHANNEL_PANELS.filter((P) => !PaintsChannel(Layer, P.Key));
        if (Idle.length > 0)
        {
            const Pool = document.createElement("div");
            Pool.className = "chan-pool";
            for (const Panel of Idle)
            {
                const Option = document.createElement("div");
                Option.className = "chan-pool-opt";
                Option.textContent = Panel.Label;
                Option.onclick = (Event) => {
                    Event.stopPropagation();
                    this.Apply("channel", { Token: Layer.Token, Channel: Panel.Key, On: true });
                };
                Pool.appendChild(Option);
            }
            Body.appendChild(Pool);
        }

        Clip.appendChild(Body);
        Shell.appendChild(Clip);
        Card.appendChild(Head);
        Card.appendChild(Shell);
        return Card;
    }

    BuildChannelPanel(Layer, Panel)
    {
        const Key       = `${Layer.Token}:${Panel.Key}`;
        const Collapsed = this.Collapsed.has(Key);

        const Host = document.createElement("div");
        Host.className = "chan-panel" + (Collapsed ? " collapsed" : "");

        const Head = document.createElement("div");
        Head.className = "chan-head";
        Head.innerHTML =
            `<span class="ch-tw">${Icon("chevronDown", 9)}</span>` +
            `<span class="ch-dot" style="background:${Hue(Layer.Classification)}"></span>` +
            `<span class="ch-title">${Panel.Label}</span>`;

        const Shell = document.createElement("div"); Shell.className = "chan-shell";
        const Clip  = document.createElement("div"); Clip.className  = "chan-clip";
        const Body  = document.createElement("div"); Body.className  = "chan-body";

        this.BuildChannelBody(Body, Layer, Panel);

        Clip.appendChild(Body);
        Shell.appendChild(Clip);

        Head.onclick = () => {
            if (this.Collapsed.has(Key)) { this.Collapsed.delete(Key); }
            else                          { this.Collapsed.add(Key); }
            Host.classList.toggle("collapsed", this.Collapsed.has(Key));
        };

        Host.appendChild(Head);
        Host.appendChild(Shell);
        return Host;
    }

    BuildChannelBody(Body, Layer, Panel)
    {
        // 🔴 The normal channel has NO editable value. It is derived from the painted height by central
        //    differences in the shader, so there is nothing to author here — and offering a colour field
        //    would let a brush write raw RGB into a normal, which is neither unit-length nor in tangent
        //    space and shades as coloured noise.
        if (Panel.Edit === "derived")
        {
            const Note = document.createElement("div");
            Note.className = "chan-note";
            Note.textContent = "Derived from the painted height. No value to author.";
            Body.appendChild(Note);
            // 🔴 A preview even here, and it was the one branch without one. "No value to AUTHOR" is not
            //    "nothing to SEE": every enabled channel is drawn over the surface, so every enabled channel
            //    owes the panel a picture of what it contributes. Normal has no atlas of its own
            //    (CHANNEL_SLOTS.normal.Atlas is null), so this resolves to the derived-source tile below,
            //    which reads the HEIGHT it is computed from rather than claiming the channel is empty.
            Body.appendChild(this.BuildChannelPreview(Layer, Panel));
            return;
        }

        // ---- the channel's SOURCE -------------------------------------------------------------------
        // 🔴 The mode gates what follows, so it is built first and the body below is a consequence of it.
        //    A flat value, a painted texture and a procedural pass are three different owners of the same
        //    channel, and showing a value slider while the channel is actually driven by a generator is the
        //    panel lying about what the surface shows.
        const Mode = Layer.Modes?.[Panel.Key] ?? "Value";
        Body.appendChild(PropertyRow("Source", this.BuildModePicker(Layer, Panel, Mode)));

        // Texture mode is paint-driven: the atlas is authored by strokes, so there is no field to edit and
        // no generator to configure. Only paintable layers can actually receive those strokes.
        if (Mode === "Texture")
        {
            const Note = document.createElement("div");
            Note.className = "chan-note";
            Note.textContent = Layer.Paintable
                ? "Painted. Strokes on this layer author the channel; storage is allocated on first use."
                : "Texture source, but this layer kind does not accept strokes.";
            Body.appendChild(Note);
            Body.appendChild(this.BuildChannelPreview(Layer, Panel));
            return;
        }

        // Generator mode: the recipe owns the channel. Its parameters live on the layer, not the channel,
        // so they are edited once in the identity card rather than repeated under all three channels.
        if (Mode === "Generator")
        {
            const Note = document.createElement("div");
            Note.className = "chan-note";
            Note.textContent = Layer.Generator
                ? `Driven by the "${Layer.Generator}" generator. Its parameters are on the layer.`
                : "Generator source, but this layer has no generator recipe assigned.";
            Body.appendChild(Note);
            Body.appendChild(this.BuildChannelPreview(Layer, Panel));
            return;
        }

        // 🔴 Value mode gets a preview TOO, which it did not before. Texture and Generator each showed one and
        //    then returned, so a solid channel was the only one in the panel with nothing to look at — and a
        //    solid is still what the surface shows, so "whatever channel you look at, you see what it holds"
        //    was false exactly where the answer is simplest. The preview falls back to a flat swatch of the
        //    authored value (SolidPreviewOf) because Capture has no atlas to read for a Value channel.
        if (Panel.Edit === "colour")
        {
            const Current = Array.isArray(Layer.Values[Panel.Key])
                ? ColourToHex(Layer.Values[Panel.Key]) : "#808080";
            Body.appendChild(PropertyRow("Colour", BuildColourField(Current, (Hex, Live) => {
                this.Commands("value", { Token: Layer.Token, Channel: Panel.Key, Value: HexToColour(Hex) });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }, this.PickerState(Layer.Token, Panel.Key))));
            Body.appendChild(this.BuildChannelPreview(Layer, Panel));
            return;
        }

        const Value = Number(Layer.Values[Panel.Key] ?? 0);
        Body.appendChild(PropertyRow("Amount", BuildSlider({
            Min: Panel.Min, Max: Panel.Max, Step: Panel.Step, Value: Value,
            OnInput: (Next, Live) => {
                this.Commands("value", { Token: Layer.Token, Channel: Panel.Key, Value: Next });
                this.OnChange();
                if (!Live) { this.Refresh(); }
            }
        })));
        Body.appendChild(this.BuildChannelPreview(Layer, Panel));
    }

    //--------------------------------------------------------------------------------------------------
    //                                      CHANNEL PREVIEW
    //--------------------------------------------------------------------------------------------------

    // The live thumbnail of what this channel actually holds, read back from its atlas.
    //
    // 🔴 Returns the element SYNCHRONOUSLY and fills the image in later. RenderChannels builds the whole
    //    pane in one pass and appends as it goes; awaiting a GPU readback per channel there would make the
    //    panel appear a channel at a time, and would make Refresh() async — which every caller, including
    //    LayerCommand, invokes without awaiting.
    BuildChannelPreview(Layer, Panel)
    {
        const Host = document.createElement("div");
        Host.className = "chan-preview";

        const Tile = document.createElement("div");
        Tile.className = "cp-tile";

        const Note = document.createElement("div");
        Note.className = "cp-note";
        Note.textContent = "Reading…";

        Host.appendChild(Tile);
        Host.appendChild(Note);

        // 🔴 The generation counter is what makes a late capture safe. The panel is rebuilt on every
        //    Refresh(), so by the time an await resolves, THIS element may already have been discarded and
        //    replaced — and the layer may no longer even be focused. Without the guard, a slow readback for
        //    the previously focused layer lands in the newly built panel and shows the wrong channel's
        //    content, which is indistinguishable from a preview that simply renders the wrong thing.
        const Generation = this.PreviewGeneration ?? 0;

        // 🔴 A derived channel is captured through its SOURCE, not through itself. normal has no atlas
        //    (CHANNEL_SLOTS.normal.Atlas === null) so Capture returns null for it unconditionally — asking for
        //    "normal" would show an empty tile on a heavily sculpted layer. CHANNEL_SLOTS names the source
        //    ("height"), so the tile shows the field the normal is actually computed from and says so.
        const Slot   = CHANNEL_SLOTS[Panel.Key];
        const Source = (Slot && Slot.Atlas === null && Slot.Source) ? Slot.Source : Panel.Key;

        this.Capture(Layer.Token, Source).then((Preview) => {
            if ((this.PreviewGeneration ?? 0) !== Generation) { return; }
            if (!Tile.isConnected) { return; }

            if (!Preview)
            {
                // 🔴 No atlas is NOT the same as nothing to show. A Value-mode channel is a solid — a flat
                //    authored value that the compositor reads directly and that therefore never allocates
                //    storage, so Capture returns null for it forever (ChannelPreview.js:308, `if (!Source)`).
                //    Reporting "no content" for one was wrong: the channel has content, it just is not a
                //    texture. Draw the value itself as a flat swatch — that IS the texture, at 1x1.
                const Solid = SolidPreviewOf(Layer, Panel.Key);
                if (Solid)
                {
                    Tile.classList.add("cp-solid");
                    Tile.style.background = Solid.Css;
                    Note.textContent = `Solid ${Solid.Reading} · authored value, not painted`;
                    return;
                }

                // 🔴 Nothing painted YET is shown as a transparent checkerboard, not as a blank well. The
                //    channel is enabled, so it will be drawn over the surface the moment a stroke lands —
                //    what it holds right now is genuine transparency, and the checker is the standing idiom
                //    for exactly that. A flat grey panel instead reads as "this channel is unavailable",
                //    which is the one thing that is not true of an enabled, paintable, empty channel.
                Tile.classList.add("cp-clear");
                Note.textContent = Source !== Panel.Key
                    ? `Transparent — nothing painted into ${CHANNEL_LABEL[Source] ?? Source} to derive from yet.`
                    : "Transparent — nothing painted into this channel yet.";
                return;
            }

            Tile.style.backgroundImage = `url(${Preview.Image})`;

            // The derived channel's tile is its source field, so the caption must name the source rather
            // than let the reader take the picture for the normal map itself.
            if (Source !== Panel.Key)
            {
                Tile.classList.add("cp-derived");
                Note.textContent =
                    `${Preview.Extent}² ${CHANNEL_LABEL[Source] ?? Source} from the ${Preview.Atlas} atlas · ` +
                    `the normal is computed from this`;
                return;
            }

            // 🔴 A Value-mode channel that STILL has an atlas is a real state, not a contradiction: switching a
            //    non-flooded paint layer to Value records the mode but leaves the painted atlas in place (only
            //    flooded kinds re-flood — see the "mode" verb in PaintingSurface.html). The readback is the
            //    honest thing to show, because it is what the compositor reads; but it must not be captioned as
            //    the authored value, or the panel would claim the swatch and the pixels are the same thing.
            const Solid = (Layer.Modes?.[Panel.Key] ?? "Value") === "Value"
                ? SolidPreviewOf(Layer, Panel.Key) : null;
            Note.textContent = Solid
                ? `${Preview.Extent}² from the ${Preview.Atlas} atlas · retained paint, authored value is ${Solid.Reading}`
                : `${Preview.Extent}² from the ${Preview.Atlas} atlas`;
        });

        return Host;
    }

    //--------------------------------------------------------------------------------------------------
    //                                      CHANNEL SOURCE
    //--------------------------------------------------------------------------------------------------

    // The Value / Texture / Generator segmented control for one channel.
    //
    // 🔴 The options come from the engine's CHANNEL_MODES, not from a literal list here. A hard-coded triple
    //    would keep rendering three buttons after the engine's vocabulary changed, and the extra one would
    //    send a mode string the "mode" verb silently ignores — the button would latch visually and do nothing.
    BuildModePicker(Layer, Panel, Current)
    {
        const Host = document.createElement("div");
        Host.className = "segment";

        for (const Name of CHANNEL_MODES)
        {
            const Option = document.createElement("div");
            Option.className = "seg-opt" + (Name === Current ? " sel" : "");
            Option.textContent = Name;

            // 🔴 Generator is offered ONLY where a recipe exists to drive the channel. Selecting it on a
            //    plain paint layer would set a mode nothing implements: the channel would stop accepting
            //    strokes and no pass would write it, so it would read as a dead channel with no way back
            //    except re-picking Value. Disabled-with-a-reason beats a lever that breaks the layer.
            const Allowed = (Name !== "Generator") || Boolean(Layer.Generator);
            if (!Allowed)
            {
                Option.classList.add("off");
                Option.title = "This layer has no generator recipe.";
            }

            Option.onclick = () => {
                if (!Allowed || Name === Current) { return; }
                this.Apply("mode", { Token: Layer.Token, Channel: Panel.Key, Mode: Name });
            };

            Host.appendChild(Option);
        }

        return Host;
    }

    //--------------------------------------------------------------------------------------------------
    //                                        DROPDOWN
    //--------------------------------------------------------------------------------------------------

    CloseLists()
    {
        // 🔴 Do NOT early-return on a null OpenList: the add-layer list can be live while OpenList points
        //    elsewhere (or was cleared), and skipping the cleanup then orphans a body-appended node. A stale
        //    AddLayerList also poisons the next OpenAddLayerList toggle guard, so it stops opening. Clean up
        //    every field unconditionally, every time.
        if (this.OpenList)
        {
            this.OpenList.classList.remove("open");

            // 🔴 A portalled list is put BACK under its .dropdown, and its inline placement wiped. Left on
            //    <body>, the node would be orphaned the moment the next Refresh() rebuilt the pane that owns
            //    the trigger — the list's item handlers close over a Layer that is gone, and the stale node
            //    keeps answering `.dd-list` queries. Returning it home ties its lifetime back to its host, so
            //    a rebuild disposes of it exactly as it always did.
            if (this.OpenHost)
            {
                this.OpenHost.classList.remove("open");
                this.OpenHost.appendChild(this.OpenList);
                for (const Property of ["width", "maxHeight", "left", "top", "transformOrigin"])
                {
                    this.OpenList.style[Property] = "";
                }
                this.OpenList.classList.remove("above");
                this.OpenHost = null;
            }
            this.OpenList = null;
        }

        // The add-layer list has no persistent .dropdown to go home to — it is rebuilt per open — so it is
        // removed outright instead.
        //
        // 🔴 Removed IMMEDIATELY, not after a close transition. The list is rebuilt from scratch on every
        //    open, and the toggle guard reads AddLayerList, so letting a dying node linger would make the
        //    next click reopen against a node that is already on its way out.
        if (this.AddLayerList) { this.AddLayerList.remove(); this.AddLayerList = null; }

        // Whichever trigger was wearing the join drops it with the list it owned. Only the "+ Add Layer" button
        // still joins its list, so in practice `flipped` clears for that anchor alone — a field dropdown's
        // capsule head never takes it. Removing both unconditionally keeps one exit path for every trigger.
        if (this.OpenAnchor)
        {
            this.OpenAnchor.classList.remove("listopen", "flipped");
            this.OpenAnchor = null;
        }
    }

    // Place an open .dd-list against its trigger — `Gap` px off it, or flush when Gap is 0 — choosing the side
    // with more room and CLAMPING the list to what that side actually has.
    //
    // 🔴 The height is MEASURED (scrollHeight), never derived from the option count. Both call sites used to
    //    guess `Options.length * 29 + 8`; a row that wraps to two lines, or any list longer than the guess,
    //    then reported a height smaller than the real one, so the "does it fit below?" test passed for a list
    //    that did not — and the overflow was simply cut off at the viewport edge (the reported "Add
    //    component" / "Blend" clipping). The measurement needs the list laid out but not yet animating, which
    //    is why `visibility:hidden` (not display:none) is the closed state — a hidden box still has geometry.
    //
    // 🔴 max-height is set EXPLICITLY per open rather than left to the stylesheet's flat 280px. A trigger
    //    near an edge can have less than 280px on both sides, and a fixed cap taller than the gap clips no
    //    matter which side is chosen; capping to the measured gap makes the list scroll instead.
    // 🔴 `Gap` is the offset between trigger and list, and it is a PARAMETER because the two kinds of trigger
    //    disagree by design. A field dropdown passes 6 — Dropdown.cpp positions its popup at
    //    `Origin.y + Height + 6.0f`, a detached window that shares no edge. The "+ Add Layer" button passes 0
    //    and keeps its joined one-card look, which the ruling did not touch (it is a button, not a
    //    DrawValuePill dropdown). Baking either value in would silently restyle the other control.
    //    The gap is taken out of the room BEFORE the fit test, or a list that only just fits would be told it
    //    fits and then get pushed `Gap` px past the viewport edge by the offset.
    PlaceList(List, Anchor, MinWidth, Gap = 0)
    {
        const Box    = Anchor.getBoundingClientRect();
        const Margin = 8;

        const Room   = { Below: window.innerHeight - Box.bottom - Margin - Gap,
                         Above: Box.top - Margin - Gap };
        // Measured against the width it will actually be shown at, or a long option would wrap differently
        // once the width lands and change the height out from under the decision.
        const Width  = Math.max(Box.width, MinWidth);
        List.style.width     = `${Width}px`;
        List.style.maxHeight = "none";
        const Natural = List.scrollHeight;

        // Prefer below; flip only when below cannot take it AND above has more room.
        const DropsDown = Natural <= Room.Below || Room.Below >= Room.Above;
        // 🔴 Capped at the room the CHOSEN side actually has, with NO minimum floor. The old
        //    `Math.max(96, ...)` looks like a courtesy — "never show a uselessly short list" — but it is
        //    exactly what kept the left-rail lists clipped: a trigger low in the scrolling rail can have
        //    40px beneath it, the floor then hands back 96px, and `top = Box.bottom` puts 56px of the list
        //    past the viewport edge where nothing can scroll it into view. The cap must be honest; a genuinely
        //    cramped side is handled by FLIPPING (above), not by overflowing.
        const Cap = Math.max(0, Math.min(Natural, DropsDown ? Room.Below : Room.Above));

        // Kept inside the viewport horizontally too — a trigger in the right-hand pane of a card near the
        // screen edge would otherwise run its list off the side.
        const Left = Math.max(Margin, Math.min(Box.left, window.innerWidth - Width - Margin));

        List.style.maxHeight = `${Cap}px`;
        List.style.left      = `${Left}px`;
        // 🔴 Anchored to the list's own top edge on BOTH sides. When flipped, the bottom edge must meet the
        //    trigger's top, and the box grows upward from `Box.top - Cap` — but only while the list is TALLER
        //    than its cap. A short list capped generously would be positioned Cap px up and leave a visible
        //    gap under the trigger, so the offset uses the height the list will actually take, not the cap.
        //    `Gap` is then applied AWAY from the trigger on whichever side we chose — added below, subtracted
        //    above — so a detached list clears the head by the same 6px in both directions.
        const Height = Math.min(Natural, Cap);
        List.style.top = DropsDown ? `${Box.bottom + Gap}px` : `${Box.top - Gap - Height}px`;
        List.classList.toggle("above", !DropsDown);
        List.style.transformOrigin = DropsDown ? "top center" : "bottom center";

        return DropsDown;
    }

    // 📝 The list is position:fixed and placed by script, so it escapes the scrolling pane rather than
    //    being clipped by it.
    BuildDropdown(Options, Current, OnPick)
    {
        const Host = document.createElement("div");
        Host.className = "dropdown";

        const Head = document.createElement("div");
        Head.className = "dd-head";
        Head.innerHTML = `<span class="cur">${Current}</span>` +
            `<span class="caret">${Icon("chevronDown", 10)}</span>`;

        const List = document.createElement("div");
        List.className = "dd-list";
        for (const Option of Options)
        {
            const Item = document.createElement("div");
            Item.className = "dd-item" + (Option === Current ? " sel" : "");
            Item.innerHTML = `<span>${Option}</span><span class="radio"></span>`;
            Item.onclick = (Event) => {
                Event.stopPropagation();
                this.CloseLists();
                if (Option !== Current) { OnPick(Option); }
            };
            List.appendChild(Item);
        }

        Head.onclick = (Event) => {
            Event.stopPropagation();
            const WasOpen = Host.classList.contains("open");
            this.CloseLists();
            if (WasOpen) { return; }

            // 🔴 The list is PORTALLED to <body> for as long as it is open, then handed back by CloseLists.
            //    `position:fixed` is only viewport-relative while no ancestor establishes a containing block
            //    for it — and several do here. `.card` carries `animation:cardRise ... both`, which leaves a
            //    transform on the element FOREVER after it finishes (the `both` fill holds the final
            //    keyframe), and `.card` is also `overflow:hidden`; `.mc-track` and `.menu-track` are
            //    transformed too. So a list inside any of them was being positioned relative to that box and
            //    then clipped by it — which is why PlaceList's viewport maths, correct on its own terms, still
            //    produced cut-off lists: it clamped against the viewport while the browser resolved the
            //    coordinates against a card. Reparenting to <body> is what makes the clamp mean what it says,
            //    and it applies to EVERY dropdown, left rail and right pane alike.
            document.body.appendChild(List);

            Host.classList.add("open");
            this.OpenList   = List;
            this.OpenHost   = Host;
            this.OpenAnchor = Head;

            // 🔴 DETACHED by 6px, matching Dropdown.cpp's `SetNextWindowPos(Origin.x, Origin.y + Height + 6.0f)`
            //    — the executable's list is an independent popup, not the joined card this used to draw (the
            //    user chose the executable when the two designs were put to them). No `.flipped` class is set
            //    on the head any more: it existed only to swap which border went transparent for the join, and
            //    the capsule head has no border to swap. PlaceList still returns the side so the list's own
            //    grow direction can flip.
            //
            // 🔴 Placed while .open is already set. The measurement reads scrollHeight, and the closed state
            //    carries a scaleY(.86) transform — harmless for scrollHeight (a transform does not change
            //    layout) but the class must be on before `top` is written, or the flipped branch would
            //    position against a stale max-height.
            this.PlaceList(List, Head, 128, 6);

            // 🔴 `.open` lands on the NEXT frame, exactly as for the add-layer list. Reparenting to <body>
            //    makes this a freshly-inserted node with no committed style, so setting the class in this same
            //    frame would give the browser no start value to interpolate from and the open would hard-cut.
            //    Before the portal, the list was already in the document and this deferral was unnecessary.
            requestAnimationFrame(() => List.classList.add("open"));
        };

        Host.appendChild(Head);
        Host.appendChild(List);
        return Host;
    }

    Release()
    {
        LiveInspectors.delete(this);
        document.removeEventListener("pointerdown", this.CloseListOnOutside, true);
        document.removeEventListener("scroll", this.CloseListOnScroll, true);
        this.Root.remove();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   FIELD WIDGETS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 The scrub latch. The field widgets are module-level functions with no `this`, but a drag inside one
//    must suppress every live inspector's Refresh() — the rebuild detaches the node the pointer is
//    captured on and the drag dies on the spot (the "sliders keep jamming" fault). Rather than thread a
//    latch through all eight BuildSlider call sites, the widgets raise it here and the inspectors
//    register themselves for the duration of their life.
const LiveInspectors = new Set();

function BeginScrub()
{
    for (const Panel of LiveInspectors) { Panel.Scrubbing += 1; }
}

// Release replays the rebuild that was skipped, so the panes catch up with everything the drag wrote.
function EndScrub()
{
    for (const Panel of LiveInspectors)
    {
        Panel.Scrubbing = Math.max(0, Panel.Scrubbing - 1);
        if (Panel.Scrubbing === 0 && Panel.RefreshDeferred) { Panel.Refresh(); }
    }
}

function SectionLabel(Text, Glyph, Tail)
{
    const Element = document.createElement("div");
    Element.className = "meta-sect";
    Element.innerHTML = (Glyph ? `<span>${Icon(Glyph, 12)}</span>` : "") + `<span>${Text}</span>` +
        (Tail ? `<span class="ms-tail">${Tail}</span>` : "");
    return Element;
}

// A divider label for the inline expand rail — the `.se-sect` band with its trailing hairline.
function SectionLabel2(Text)
{
    const Element = document.createElement("div");
    Element.className = "se-sect";
    Element.textContent = Text;
    return Element;
}

function PropertyRow(Label, Field)
{
    const Row = document.createElement("div");
    Row.className = "prow";
    const Tag = document.createElement("div");
    Tag.className = "plabel";
    Tag.textContent = Label;
    Row.appendChild(Tag);
    Row.appendChild(Field);
    return Row;
}

function BuildSwitch(On, OnFlip)
{
    const Host = document.createElement("div");
    Host.className = "switch" + (On ? " on" : "");
    Host.innerHTML = '<span class="nub"></span>';
    Host.onclick = () => {
        On = !On;
        Host.classList.toggle("on", On);
        OnFlip(On);
    };
    return Host;
}

// A numeric field plus a track.
//
// 📝 `OnInput` is called with (Value, Live). Live is true while the pointer is down; the caller uses it
//    to write the value without rebuilding the pane, and rebuilds once on release.
function BuildSlider(Spec)
{
    const Host  = document.createElement("div"); Host.className  = "sliderrow";
    const Box   = document.createElement("div"); Box.className   = "valuebox";
    const Num   = document.createElement("div"); Num.className   = "num";
    const Input = document.createElement("input"); Input.type    = "text";

    Num.appendChild(Input);
    Box.appendChild(Num);
    if (Spec.Unit)
    {
        const Unit = document.createElement("span");
        Unit.className = "unitseg";
        Unit.textContent = Spec.Unit;
        Box.appendChild(Unit);
    }

    const Track = document.createElement("div"); Track.className = "slider";
    const Fill  = document.createElement("div"); Fill.className  = "fill";
    const Knob  = document.createElement("div"); Knob.className  = "knob";
    Track.appendChild(Fill);
    Track.appendChild(Knob);
    Host.appendChild(Box);
    Host.appendChild(Track);

    const Places = DecimalsFor(Spec.Step);
    let   Value  = Spec.Value;

    const Paint = () => {
        const Percent = ((Value - Spec.Min) / (Spec.Max - Spec.Min)) * 100;
        Fill.style.width = `${Percent}%`;
        Knob.style.left  = `${Percent}%`;
        Input.value = Value.toFixed(Places);
    };
    // 🔴 Quantised RELATIVE TO Min, not to zero. `round(v / Step) * Step` only lands on the offered values
    //    when Min is a whole multiple of Step — on a 0.05-stepped range starting at 0.1 it snaps to a
    //    lattice the endpoints are not on, so the track could never reach its own Max and the knob stuck
    //    just short of the end. Re-clamped afterwards because rounding can overshoot Max by a part-step.
    const Settle = (Next, Live) => {
        const Clamped   = Math.max(Spec.Min, Math.min(Spec.Max, Next));
        const Steps     = Math.round((Clamped - Spec.Min) / Spec.Step);
        const Quantised = Math.min(Spec.Max, Spec.Min + Steps * Spec.Step);
        const Landed    = Number(Quantised.toFixed(Places));

        // 🔴 An unchanged value is NOT pushed through. A 0–100 slider over a ~200px track quantises many
        //    pointer positions to the same integer, and each redundant write re-resolved the composite on
        //    the GPU — the drag went sluggish and lagged behind the cursor, which reads as jamming. Only a
        //    real change costs a recomposite; the release below always commits once regardless.
        if (Landed === Value && Live) { return; }

        Value = Landed;
        Paint();
        Spec.OnInput(Value, Live);
    };
    const FromPointer = (Event) => {
        const Box2  = Track.getBoundingClientRect();
        const Ratio = Math.max(0, Math.min(1, (Event.clientX - Box2.left) / Box2.width));
        Settle(Spec.Min + Ratio * (Spec.Max - Spec.Min), true);
    };

    // 🔴 One drag at a time, keyed on the pointer that opened it. The previous form added a fresh
    //    pointermove/pointerup pair per pointerdown and only ever removed them on `pointerup` — so a drag
    //    that ended any other way (pointercancel, or capture lost when a rebuild detached the track) left
    //    its listeners attached, and the next press ran TWO handlers that fought over the same value. That
    //    is the other half of the jamming. `Active` makes a second press a no-op until the first finishes,
    //    and Finish is idempotent so every exit path lands in the same place exactly once.
    let Active = -1;

    const Move = (Motion) => {
        if (Motion.pointerId !== Active) { return; }
        FromPointer(Motion);
    };
    const Finish = (Motion) => {
        if (Motion && Motion.pointerId !== Active) { return; }
        if (Active === -1) { return; }
        Active = -1;
        Track.removeEventListener("pointermove", Move);
        Track.removeEventListener("pointerup", Finish);
        Track.removeEventListener("pointercancel", Finish);
        Track.removeEventListener("lostpointercapture", Finish);
        Spec.OnInput(Value, false);
        EndScrub();
    };

    Track.onpointerdown = (Event) => {
        if (Event.button !== 0)   { return; }
        if (Active !== -1)        { return; }
        Event.preventDefault();
        Event.stopPropagation();

        Active = Event.pointerId;
        BeginScrub();
        Track.setPointerCapture(Event.pointerId);

        Track.addEventListener("pointermove", Move);
        Track.addEventListener("pointerup", Finish);
        // 🔴 pointercancel and lostpointercapture both END the drag with no pointerup. Without them the
        //    scrub latch would never come back down and the panel would stop refreshing for good.
        Track.addEventListener("pointercancel", Finish);
        Track.addEventListener("lostpointercapture", Finish);

        FromPointer(Event);
    };
    Input.onchange = () => {
        const Parsed = parseFloat(Input.value);
        if (Number.isFinite(Parsed)) { Settle(Parsed, false); } else { Paint(); }
    };
    // 📝 Swallowed so typing a value does not also fire the page's single-key shortcuts.
    Input.onkeydown = (Event) => {
        Event.stopPropagation();
        if (Event.key === "Enter") { Input.blur(); }
    };

    Paint();
    Host.ReadBack = (Next) => { Value = Next; Paint(); };
    return Host;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   COLOUR FIELD
//------------------------------------------------------------------------------------------------------------------------

function HexToHsv(Hex)
{
    const Packed = parseInt(Hex.slice(1), 16);
    const Red   = ((Packed >> 16) & 255) / 255;
    const Green = ((Packed >>  8) & 255) / 255;
    const Blue  = ( Packed        & 255) / 255;
    const Max = Math.max(Red, Green, Blue);
    const Min = Math.min(Red, Green, Blue);
    const Delta = Max - Min;

    let HueAngle = 0;
    if (Delta > 0)
    {
        if      (Max === Red)   { HueAngle = ((Green - Blue) / Delta + (Green < Blue ? 6 : 0)) / 6; }
        else if (Max === Green) { HueAngle = ((Blue - Red)   / Delta + 2) / 6; }
        else                    { HueAngle = ((Red - Green)  / Delta + 4) / 6; }
    }
    return { Hue: HueAngle * 360, Saturation: Max ? Delta / Max : 0, Value: Max };
}

function HsvToHex(HueAngle, Saturation, Value)
{
    const Component = (N) => {
        const K = (N + HueAngle / 60) % 6;
        return Math.round((Value - Value * Saturation * Math.max(0, Math.min(K, 4 - K, 1))) * 255);
    };
    return "#" + [Component(5), Component(3), Component(1)]
        .map((X) => X.toString(16).padStart(2, "0")).join("");
}

// A bar that opens an inline saturation/value box, a hue rail and a hex field.
//
// 🔴 `OnPick` is NOT called during construction. The field is built while rendering the pane, and a
//    construction-time call would write the layer's own current value straight back through the command
//    surface — bumping the revision and re-entering the render that is still running.
//
// 🔴 `OpenState` carries the disclosure across rebuilds: `{ Get(), Set(Bool) }`, backed by the panel's
//    OpenPickers set. The widget cannot own this — it is destroyed and recreated by every commit — so it reads
//    the flag to decide whether to build open, and writes it whenever the bar is clicked. Optional so a caller
//    with no persistence (a one-off field) still works, just closing on rebuild as before.
function BuildColourField(Current, OnPick, OpenState)
{
    const Host = document.createElement("div");

    const Bar = document.createElement("div");
    Bar.className = "colorbar";
    Bar.innerHTML = `<span class="chip"></span><span class="cname"></span>` +
        `<span class="caret">▾</span>`;

    const Picker = document.createElement("div");
    Picker.className = "picker";
    Picker.innerHTML =
        `<div class="svbox"><span class="svknob"></span></div>` +
        `<div class="barstack"><div class="cbar huebar"><span class="cknob"></span></div></div>` +
        `<div class="hexrow"><div class="valuebox"><div class="num">` +
          `<input type="text" spellcheck="false"></div></div></div>`;

    Host.appendChild(Bar);
    Host.appendChild(Picker);

    const Chip  = Bar.querySelector(".chip");
    const Label = Bar.querySelector(".cname");
    const Plane = Picker.querySelector(".svbox");
    const Dot   = Picker.querySelector(".svknob");
    const Rail  = Picker.querySelector(".huebar");
    const Slide = Picker.querySelector(".cknob");
    const Input = Picker.querySelector("input");

    let State = HexToHsv(Current);

    const Paint = (Live, Notify) => {
        const Hex = HsvToHex(State.Hue, State.Saturation, State.Value);
        Plane.style.background =
            `linear-gradient(to top, #000, transparent), ` +
            `linear-gradient(to right, #fff, hsl(${State.Hue}, 100%, 50%))`;
        Dot.style.left    = `${State.Saturation * 100}%`;
        Dot.style.top     = `${(1 - State.Value) * 100}%`;
        Slide.style.left  = `${(State.Hue / 360) * 100}%`;
        Chip.style.background = Hex;
        Label.textContent = Hex.toUpperCase();
        Input.value       = Hex.toUpperCase();
        if (Notify) { OnPick(Hex, Live); }
    };

    // 🔴 The plane and the hue rail RAISE THE SCRUB LATCH for the life of the drag. Without it the first
    //    pointermove wrote through OnPick -> LayerCommand -> the host's Refresh(), whose `Body.innerHTML = ""`
    //    detached the very .svbox the pointer was captured on: the drag died on its first pixel and the picker
    //    itself was destroyed and rebuilt without `.open`. That is precisely the "sliders keep jamming" fault
    //    BeginScrub/EndScrub exist to cure — BuildSlider has always called them and this widget simply never
    //    did, which is why dragging a colour read as "one click confirms and closes".
    // 🔴 EndScrub is paired in a `finally`-shaped teardown that runs on pointercancel and lostpointercapture
    //    too, not just pointerup. A latch left raised would wedge Refresh() off permanently and freeze the
    //    whole panel — losing the capture (an alt-tab, a touch interruption) must still balance the count.
    // 🔴 One drag at a time, keyed on the pointer that opened it — the same shape BuildSlider settled on, and
    //    for the same reason: a drag that ends by pointercancel or lost capture (not pointerup) would otherwise
    //    leave its listeners attached, and the next press would run two handlers fighting over one value.
    const Scrub = (Surface, Apply) => {
        let Active = -1;

        const Read = (Motion) => {
            const Box = Surface.getBoundingClientRect();
            Apply(Math.max(0, Math.min(1, (Motion.clientX - Box.left) / Box.width)),
                  Math.max(0, Math.min(1, (Motion.clientY - Box.top)  / Box.height)));
            Paint(true, true);
        };
        const Move = (Motion) => {
            if (Motion.pointerId !== Active) { return; }
            Read(Motion);
        };
        // 📝 Idempotent, and every exit path lands here exactly once: pointerup and lostpointercapture both fire
        //    on a normal release, and a double EndScrub would drive the shared depth count below the drags still
        //    live on other pointers, unlatching them mid-scrub.
        const Up = (Motion) => {
            if (Motion && Motion.pointerId !== Active) { return; }
            if (Active === -1) { return; }
            Active = -1;
            Surface.removeEventListener("pointermove", Move);
            Surface.removeEventListener("pointerup", Up);
            Surface.removeEventListener("pointercancel", Up);
            Surface.removeEventListener("lostpointercapture", Up);
            // 🔴 The final commit is published BEFORE the latch drops. EndScrub replays the deferred rebuild,
            //    so releasing first would rebuild the pane against the pre-drag value and then write the new
            //    one into a pane that had already been thrown away.
            Paint(false, true);
            EndScrub();
        };

        Surface.onpointerdown = (Event) => {
            // 📝 Left button only, and never re-entered while a drag is live — a right-click on the plane must
            //    not open a phantom scrub that only a matching release could ever close.
            if (Event.button !== 0) { return; }
            if (Active !== -1)      { return; }
            Event.preventDefault();
            Event.stopPropagation();

            Active = Event.pointerId;
            BeginScrub();
            Surface.setPointerCapture(Event.pointerId);

            Surface.addEventListener("pointermove", Move);
            Surface.addEventListener("pointerup", Up);
            // 🔴 pointercancel and lostpointercapture both END the drag with no pointerup. Without them the
            //    scrub latch would never come back down and the panel would stop refreshing for good.
            Surface.addEventListener("pointercancel", Up);
            Surface.addEventListener("lostpointercapture", Up);

            Read(Event);
        };
    };

    Scrub(Plane, (X, Y) => { State.Saturation = X; State.Value = 1 - Y; });
    Scrub(Rail,  (X)    => { State.Hue = X * 360; });

    // 🔴 Built open when the persisted flag says so, WITHOUT a transition on that first frame — the pane is
    //    mid-rebuild, so animating the reveal here would replay the open animation on every committed value
    //    and make a drag look like the picker was flickering shut and back.
    if (OpenState && OpenState.Get()) { Picker.classList.add("open"); }

    Bar.onclick = (Event) => {
        Event.stopPropagation();
        const NowOpen = !Picker.classList.contains("open");
        Picker.classList.toggle("open", NowOpen);
        // 📝 Mirrored into the panel so the next rebuild agrees with what is on screen.
        if (OpenState) { OpenState.Set(NowOpen); }
    };

    Input.onkeydown = (Event) => {
        Event.stopPropagation();
        if (Event.key === "Enter") { Input.blur(); }
    };
    Input.onchange = () => {
        const Text = Input.value.trim();
        if (/^#?[0-9a-fA-F]{6}$/.test(Text))
        {
            State = HexToHsv(Text.startsWith("#") ? Text : `#${Text}`);
        }
        Paint(false, true);
    };

    Paint(true, false);
    return Host;
}

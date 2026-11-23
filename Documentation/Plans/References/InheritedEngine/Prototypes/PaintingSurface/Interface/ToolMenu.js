/*====================================================================================================================================
                                                        TOOLMENU.JS
====================================================================================================================================*/
// 🧩 Six-instrument tool card: rail of media, tile grid, schema-driven properties, wired to the live brush

//------------------------------------------------------------------------------------------------------------------------
//                                                      ART HELPERS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every instrument is authored as a 300x60 landscape drawing with the TIP AT THE RIGHT. The tile
//    nib is the same drawing under a cropped viewBox, so an instrument is described once and appears
//    at two scales without a second asset.
const ArtView = 'viewBox="0 0 300 60"';
const NibView = 'viewBox="188 6 48 48"';

const Svg = (Inner, View) => `<svg ${View} xmlns="http://www.w3.org/2000/svg">${Inner}</svg>`;

// A linear gradient definition. Stops are [offset, colour] pairs.
const Gradient = (Identifier, Stops, X1, Y1, X2, Y2) =>
    `<linearGradient id="${Identifier}" x1="${X1}" y1="${Y1}" x2="${X2}" y2="${Y2}">` +
    Stops.map(([Offset, Colour]) => `<stop offset="${Offset}" stop-color="${Colour}"/>`).join("") +
    `</linearGradient>`;

// 🔴 The gradient defs live ONCE in the page, not inside each drawing. Repeating them per tile
//    duplicates the same ids into the document and the browser resolves every reference to whichever
//    copy it saw first, so tiles start borrowing each other's colours.
const SharedDefinitions =
    `<svg width="0" height="0" style="position:absolute" aria-hidden="true"><defs>` +
    Gradient("MetalBand", [[0, "#cfd3da"], [0.35, "#8a8f98"], [0.6, "#e3e6ea"], [1, "#767b84"]], 0, 0, 0, 1) +
    Gradient("WoodBand",  [[0, "#e0b567"], [0.4, "#c8973f"], [1, "#9c6f26"]], 0, 0, 0, 1) +
    Gradient("BodyDark",  [[0, "#3a3f46"], [0.45, "#22262b"], [1, "#14171a"]], 0, 0, 0, 1) +
    Gradient("BodyWarm",  [[0, "#f0e3cd"], [0.45, "#dcc8a6"], [1, "#b99f77"]], 0, 0, 0, 1) +
    Gradient("Ferrule",   [[0, "#d8dce2"], [0.5, "#9aa0a8"], [1, "#6f747c"]], 0, 0, 0, 1) +
    Gradient("Crimson",   [[0, "#e0575f"], [0.5, "#c0303a"], [1, "#8d1e26"]], 0, 0, 0, 1) +
    Gradient("Azure",     [[0, "#7fb2ff"], [0.5, "#3f7ae0"], [1, "#2352a6"]], 0, 0, 0, 1) +
    Gradient("Bristle",   [[0, "#c9a06a"], [0.5, "#a97f47"], [1, "#7d5a2c"]], 0, 0, 0, 1) +
    `</defs></svg>`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSTRUMENT ART
//------------------------------------------------------------------------------------------------------------------------

// 📝 Each factory returns only the inner markup, so the same string serves the full drawing and the
//    cropped nib. Coordinates are shared: the barrel runs left, the writing tip lands near x=228.
const ART = {
    Pen: () =>
        `<rect x="40" y="22" width="150" height="16" rx="7" fill="url(#BodyDark)"/>` +
        `<rect x="52" y="25" width="26" height="10" rx="4" fill="#5b8cff" opacity="0.75"/>` +
        `<path d="M190 22 L214 26 L214 34 L190 38 Z" fill="url(#Ferrule)"/>` +
        `<path d="M214 26 L232 29 L232 31 L214 34 Z" fill="#2c3138"/>` +
        `<path d="M228 29.4 L236 30 L228 30.6 Z" fill="#14171a"/>`,

    Pencil: () =>
        `<path d="M34 22 H188 V38 H34 Z" fill="url(#WoodBand)"/>` +
        `<path d="M34 22 H188 V27 H34 Z" fill="#f0cd85" opacity="0.55"/>` +
        `<rect x="188" y="22" width="10" height="16" fill="url(#Ferrule)"/>` +
        `<path d="M198 22 L226 30 L198 38 Z" fill="#e8d7b4"/>` +
        `<path d="M219 27.9 L232 30 L219 32.1 Z" fill="#3a3a3f"/>`,

    Chalk: () =>
        // Blunt, square-ended and dusty — a chalk stick has no taper and no ferrule.
        `<path d="M120 18 H228 V42 H120 Z" fill="#f2f0eb"/>` +
        `<path d="M120 18 H228 V25 H120 Z" fill="#ffffff" opacity="0.8"/>` +
        `<path d="M120 36 H228 V42 H120 Z" fill="#cfcbc2" opacity="0.85"/>` +
        `<rect x="228" y="18" width="4" height="24" fill="#e2ded4"/>` +
        `<circle cx="236" cy="24" r="1.6" fill="#e8e5dd" opacity="0.7"/>` +
        `<circle cx="239" cy="34" r="1.1" fill="#e8e5dd" opacity="0.5"/>`,

    Pastel: () =>
        // A wrapped soft pastel: paper band over a saturated pigment core.
        `<path d="M112 17 H230 V43 H112 Z" fill="url(#Azure)"/>` +
        `<path d="M112 17 H230 V24 H112 Z" fill="#a9ccff" opacity="0.55"/>` +
        `<rect x="120" y="17" width="52" height="26" fill="#efe9dc"/>` +
        `<rect x="120" y="17" width="52" height="5" fill="#fbf7ee" opacity="0.8"/>` +
        `<rect x="126" y="26" width="40" height="2.4" rx="1.2" fill="#b9ae98"/>` +
        `<rect x="230" y="17" width="3" height="26" fill="#2352a6" opacity="0.7"/>`,

    Marker: () =>
        // Chisel tip, cut at an angle — that wedge is the whole identity of a marker.
        `<rect x="44" y="18" width="130" height="24" rx="6" fill="url(#BodyDark)"/>` +
        `<rect x="56" y="23" width="34" height="6" rx="3" fill="#ececf0" opacity="0.35"/>` +
        `<rect x="174" y="20" width="14" height="20" rx="3" fill="url(#Ferrule)"/>` +
        `<path d="M188 21 H214 L214 39 H188 Z" fill="#3a3f46"/>` +
        `<path d="M214 22 L234 27 L234 35 L214 39 Z" fill="#c0303a"/>` +
        `<path d="M214 22 L234 27 L214 30 Z" fill="#e0575f" opacity="0.8"/>`,

    Brush: () =>
        // Round sable: wood handle, crimped ferrule, belly that tapers to a point.
        `<path d="M20 25 H150 V35 H20 Z" fill="url(#Crimson)"/>` +
        `<path d="M150 24 H176 V36 H150 Z" fill="url(#WoodBand)"/>` +
        `<rect x="176" y="21" width="26" height="18" rx="3" fill="url(#Ferrule)"/>` +
        `<rect x="182" y="21" width="1.6" height="18" fill="#6f747c" opacity="0.8"/>` +
        `<rect x="192" y="21" width="1.6" height="18" fill="#6f747c" opacity="0.8"/>` +
        `<path d="M202 22 Q222 26 236 30 Q222 34 202 38 Z" fill="url(#Bristle)"/>` +
        `<path d="M202 24 Q220 27.5 233 30 Q220 31 202 30 Z" fill="#d8b382" opacity="0.55"/>`
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     PARAMETER GLYPHS
//------------------------------------------------------------------------------------------------------------------------

const Stroke = (Path, Width = 1.6) =>
    `<path d="${Path}" fill="none" stroke="currentColor" stroke-width="${Width}" stroke-linecap="round" stroke-linejoin="round"/>`;
const Dashed = (Path, Width = 1.6) =>
    `<path d="${Path}" fill="none" stroke="currentColor" stroke-width="${Width}" stroke-linecap="round" stroke-dasharray="2 3"/>`;
const Ring   = (X, Y, R, Width = 1.6) =>
    `<circle cx="${X}" cy="${Y}" r="${R}" fill="none" stroke="currentColor" stroke-width="${Width}"/>`;
const Dot    = (X, Y, R) => `<circle cx="${X}" cy="${Y}" r="${R}" fill="currentColor"/>`;

const GLYPH = {
    Size:      Ring(8, 8, 5) + Dot(8, 8, 1.8),
    Opacity:   Ring(8, 8, 5.5) + Stroke("M8 2.5 A5.5 5.5 0 0 1 8 13.5 Z", 0),
    Flow:      Stroke("M3 5 Q8 1 13 5 M3 9 Q8 5 13 9 M3 13 Q8 9 13 13", 1.4),
    Hardness:  Ring(8, 8, 5.5) + Ring(8, 8, 2.4),
    Softness:  Ring(8, 8, 5.5, 1.2) + Dashed("M8 3.4 A4.6 4.6 0 1 1 7.9 3.4", 1.2),
    Spacing:   Dot(3.5, 8, 1.5) + Dot(8, 8, 1.5) + Dot(12.5, 8, 1.5),
    Smooth:    Stroke("M2 11 Q5 3 8 8 T14 5", 1.5),
    Pressure:  Stroke("M8 2 V10 M5 7 L8 10 L11 7", 1.5) + Stroke("M3 13 H13", 1.5),
    Grain:     Dot(4, 5, 1) + Dot(9, 4, 0.9) + Dot(12, 8, 1) + Dot(6, 10, 0.9) + Dot(11, 12, 1),
    Taper:     Stroke("M2 8 Q8 5 14 8 Q8 11 2 8", 1.3),
    Wetness:   Stroke("M8 2 C11 6 13 8 13 10.5 A5 5 0 0 1 3 10.5 C3 8 5 6 8 2 Z", 1.4),
    Scatter:   Dot(4, 4, 1.1) + Dot(11, 5, 1.1) + Dot(7, 9, 1.1) + Dot(12, 11, 1.1),
    Tilt:      Stroke("M4 13 L11 3 M9 3 H11 V5", 1.5),
    Mode:      Ring(6, 8, 4) + Ring(10, 8, 4),
    Blend:     Ring(8, 8, 5.5) + Stroke("M8 2.5 A5.5 5.5 0 0 0 8 13.5", 5.5),
    Pigment:   Stroke("M4 12 A4 4 0 1 1 12 12 Z", 1.4) + Dot(8, 9, 1.4),
    Grade:     Stroke("M3 12 L8 4 L13 12 M5.4 9 H10.6", 1.4)
};

const Glyph = (Identifier) => Svg(GLYPH[Identifier] ?? "", 'viewBox="0 0 16 16"');

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE INSTRUMENTS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 Chalk and pastel are one "dry media" family in the full menu. Here they are separate bands,
//    because the rail is the list of instruments and collapsing them would leave five rows for six
//    tools. They still share the dry-media schema.
const INSTRUMENTS = [
    {
        Key: "Pen", Label: "Pen", Name: "Technical Fineliner",
        Dot: "#8a8f98", Art: "Pen", Schema: "Pen",
        // Hard, opaque, small: a fineliner lays a crisp constant-width line.
        Brush: { PixelRadius: 6,  Hardness: 0.94, Flow: 1.00, Spacing: 0.06 },
        Params: { Size: 6, Opacity: 100, Flow: 100, Smooth: 45, Pressure: false, Taper: 0 },
        Swatches: ["#15161a", "#1d3a8a", "#8d1e26", "#1f5c3a"]
    },
    {
        Key: "Pencil", Label: "Pencil", Name: "Classic HB Graphite",
        Dot: "#c8973f", Art: "Pencil", Schema: "Pencil",
        Brush: { PixelRadius: 9,  Hardness: 0.62, Flow: 0.55, Spacing: 0.08 },
        Params: { Size: 9, Opacity: 78, Flow: 55, Smooth: 25, Grade: "HB", Grain: 55, Pressure: true, Tilt: 0 },
        Swatches: ["#2b2b30", "#4a4a52", "#6d6d76", "#141417"]
    },
    {
        Key: "Chalk", Label: "Chalk", Name: "Chalk Stick",
        Dot: "#e8e5dd", Art: "Chalk", Schema: "Dry",
        // Broad, soft-edged and thirsty — chalk breaks up rather than covering.
        Brush: { PixelRadius: 26, Hardness: 0.30, Flow: 0.62, Spacing: 0.14 },
        Params: { Size: 26, Opacity: 88, Flow: 62, Smooth: 12, Grain: 78, Scatter: 34, Pressure: true },
        Swatches: ["#f4f2ec", "#e6d9b8", "#bcd3e6", "#e3bcbc"]
    },
    {
        Key: "Pastel", Label: "Pastel", Name: "Soft Pastel",
        Dot: "#3f7ae0", Art: "Pastel", Schema: "Dry",
        // Denser and more opaque than chalk, and it smears.
        Brush: { PixelRadius: 30, Hardness: 0.22, Flow: 0.85, Spacing: 0.12 },
        Params: { Size: 30, Opacity: 96, Flow: 85, Smooth: 18, Grain: 52, Scatter: 20, Pressure: true },
        Swatches: ["#3f7ae0", "#c0303a", "#e0a13a", "#3f9e6a", "#6b4a9e"]
    },
    {
        Key: "Marker", Label: "Marker", Name: "Permanent Marker",
        Dot: "#c0303a", Art: "Marker", Schema: "Marker",
        // Flat, saturated, hard-edged; the chisel is wide and the ink does not thin out.
        Brush: { PixelRadius: 20, Hardness: 0.86, Flow: 1.00, Spacing: 0.05 },
        Params: { Size: 20, Opacity: 100, Flow: 100, Smooth: 30, Nib: "Chisel", Blend: 0, Pressure: false },
        Swatches: ["#15161a", "#c0303a", "#2352a6", "#1f7a4d", "#e0a13a"]
    },
    {
        Key: "Brush", Label: "Paintbrush", Name: "Sable Round #8",
        Dot: "#a97f47", Art: "Brush", Schema: "Brush",
        // Soft shoulder, moderate flow, pressure-driven — the closest thing to real paint here.
        Brush: { PixelRadius: 34, Hardness: 0.40, Flow: 0.80, Spacing: 0.10 },
        Params: { Size: 34, Opacity: 92, Flow: 80, Smooth: 38, Wetness: 45, Taper: 60, Pressure: true, Bristle: "Round" },
        Swatches: ["#c0303a", "#2352a6", "#1f7a4d", "#e0a13a", "#6b4a9e", "#15161a"]
    }
];

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCHEMA
//------------------------------------------------------------------------------------------------------------------------

// 📝 `Wired` marks the controls that actually reach the WebGPU brush. The rest are recorded in state
//    and drawn in the preview, but the engine has nowhere to put them yet, so the pane labels them
//    rather than pretending. `When` hides a row entirely instead of greying it — a disabled control
//    still reads as something you failed to reach.
const StrokeGroup = (Extra) => [
    { Key: "Size",     Label: "Size",      Glyph: "Size",     Kind: "Slider", Min: 1, Max: 80,  Step: 1, Unit: "px", Wired: true },
    { Key: "Opacity",  Label: "Opacity",   Glyph: "Opacity",  Kind: "Slider", Min: 1, Max: 100, Step: 1, Unit: "%",  Wired: true },
    { Key: "Flow",     Label: "Flow",      Glyph: "Flow",     Kind: "Slider", Min: 1, Max: 100, Step: 1, Unit: "%",  Wired: true },
    { Key: "Smooth",   Label: "Smoothing", Glyph: "Smooth",   Kind: "Slider", Min: 0, Max: 100, Step: 1, Unit: "%" },
    ...Extra
];

const SCHEMA = {
    Pen: StrokeGroup([
        { Key: "Pressure", Label: "Pressure sensitive", Glyph: "Pressure", Kind: "Switch" },
        { Key: "Taper",    Label: "Taper",              Glyph: "Taper",    Kind: "Slider", Min: 0, Max: 100, Step: 1, Unit: "%",
          When: (P) => P.Pressure === true }
    ]),

    Pencil: StrokeGroup([
        { Key: "Grade",    Label: "Grade",   Glyph: "Grade",    Kind: "Segmented", Options: ["2H", "HB", "2B", "6B"] },
        { Key: "Grain",    Label: "Grain",   Glyph: "Grain",    Kind: "Slider", Min: 0, Max: 100, Step: 1, Unit: "%" },
        { Key: "Pressure", Label: "Pressure sensitive", Glyph: "Pressure", Kind: "Switch" },
        { Key: "Tilt",     Label: "Tilt shading", Glyph: "Tilt", Kind: "Slider", Min: 0, Max: 100, Step: 1, Unit: "%",
          When: (P) => P.Pressure === true }
    ]),

    Dry: StrokeGroup([
        { Key: "Grain",    Label: "Tooth",   Glyph: "Grain",    Kind: "Slider", Min: 0, Max: 100, Step: 1, Unit: "%" },
        { Key: "Scatter",  Label: "Scatter", Glyph: "Scatter",  Kind: "Slider", Min: 0, Max: 100, Step: 1, Unit: "%" },
        { Key: "Pressure", Label: "Pressure sensitive", Glyph: "Pressure", Kind: "Switch" }
    ]),

    Marker: StrokeGroup([
        { Key: "Nib",      Label: "Nib",     Glyph: "Mode",     Kind: "Segmented", Options: ["Fine", "Chisel", "Broad"] },
        { Key: "Blend",    Label: "Bleed",   Glyph: "Blend",    Kind: "Slider", Min: 0, Max: 100, Step: 1, Unit: "%" },
        { Key: "Pressure", Label: "Pressure sensitive", Glyph: "Pressure", Kind: "Switch" }
    ]),

    Brush: StrokeGroup([
        { Key: "Bristle",  Label: "Head",    Glyph: "Mode",     Kind: "Select", Options: ["Round", "Filbert", "Flat", "Fan"] },
        { Key: "Wetness",  Label: "Wetness", Glyph: "Wetness",  Kind: "Slider", Min: 0, Max: 100, Step: 1, Unit: "%" },
        { Key: "Pressure", Label: "Pressure sensitive", Glyph: "Pressure", Kind: "Switch" },
        { Key: "Taper",    Label: "Taper",   Glyph: "Taper",    Kind: "Slider", Min: 0, Max: 100, Step: 1, Unit: "%",
          When: (P) => P.Pressure === true }
    ])
};

// Controls whose `When` predicate passes against the current parameter set.
function VisibleControls(Instrument, Params)
{
    return SCHEMA[Instrument.Schema].filter((Control) => !Control.When || Control.When(Params));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     BRUSH WIRING
//------------------------------------------------------------------------------------------------------------------------

// "#rrggbb" to linear-ish 0..1 triple.
//
// 📝 Plain sRGB/255 with no decode. The atlas is authored and read in the same space, so decoding here
//    would make every swatch paint darker than the chip the user picked.
function ParseColour(Hex)
{
    const Value = parseInt(Hex.slice(1), 16);
    return [((Value >> 16) & 255) / 255, ((Value >> 8) & 255) / 255, (Value & 255) / 255];
}

// Push the selected instrument and its parameters onto the live brush.
//
// 🔴 Opacity and Flow both fold into Brush.Flow because the paint pass has ONE deposit strength. They
//    are kept separate in the UI because they mean different things to a painter, and separating them
//    for real needs per-stroke accumulation the pass does not have yet.
function ApplyToBrush(Brush, Instrument, Params, Swatch, OnMask)
{
    if (!Brush) { return; }

    Brush.PixelRadius = Params.Size;
    Brush.Hardness    = Instrument.Brush.Hardness;
    Brush.Spacing     = Instrument.Brush.Spacing;
    Brush.Flow        = (Params.Opacity / 100) * (Params.Flow / 100);

    // 🔴 On a mask, the colour controls are left ALONE — the swatch is not pushed as ink and erase is not
    //    cleared. A mask has no hue, so writing ink would be meaningless; and clearing erase used to reset
    //    the mask's own deposited value, which made a mask stop responding the moment any tool control was
    //    touched. Size, hardness, spacing and flow above still apply: a mask stroke is shaped by the
    //    instrument exactly like a colour stroke, it just deposits a level instead of a pigment.
    if (OnMask) { return; }

    if (Swatch) { Brush.Ink = ParseColour(Swatch); }

    // Selecting an instrument is a positive act of picking paint, so it leaves erase mode.
    Brush.Erase = false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE MENU
//------------------------------------------------------------------------------------------------------------------------

export class ToolMenu
{
    // `Host` is the element the card is appended to; `Brush` is the live brush object to drive.
    // `IsMasking` is an optional predicate the host supplies, answering whether strokes are currently
    // landing in a mask — which swaps the card's colour swatches for a black/white value picker.
    constructor(Host, Brush, OnChange, IsMasking)
    {
        this.Brush     = Brush;
        this.OnChange  = OnChange ?? (() => {});
        this.IsMasking = IsMasking ?? null;

        // Every mounted value picker's redraw, so an outside change (the E key) refreshes all of them.
        this.MaskValueDraws = [];

        this.Active     = INSTRUMENTS[0];
        this.Params     = { ...INSTRUMENTS[0].Params };
        this.Swatch     = INSTRUMENTS[0].Swatches[0];
        this.OnProperties = false;

        // Per-instrument parameter sets, so stepping away and back keeps your edits.
        this.Store = new Map(INSTRUMENTS.map((Item) => [Item.Key, { ...Item.Params }]));
        this.Picked = new Map(INSTRUMENTS.map((Item) => [Item.Key, Item.Swatches[0]]));

        // 📝 Art is instantiated once per instrument. Re-running the factories on every rail click is
        //    a visible hitch, and the markup never changes.
        this.ArtCache = new Map();

        if (!document.getElementById("ToolMenuDefinitions"))
        {
            const Holder = document.createElement("div");
            Holder.id = "ToolMenuDefinitions";
            Holder.innerHTML = SharedDefinitions;
            document.body.appendChild(Holder);
        }

        this.Root = document.createElement("div");
        this.Root.className = "ToolMenu";
        Host.appendChild(this.Root);

        this.Build();
        this.Select(INSTRUMENTS[0], false);
        this.AttachDismissal();
    }

    get Open() { return this.Root.classList.contains("Open"); }

    ArtFor(Instrument, View)
    {
        const CacheKey = `${Instrument.Key}:${View}`;
        if (!this.ArtCache.has(CacheKey)) { this.ArtCache.set(CacheKey, Svg(ART[Instrument.Art](), View)); }
        return this.ArtCache.get(CacheKey);
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                    STRUCTURE
    //--------------------------------------------------------------------------------------------------------------------

    Build()
    {
        this.Root.innerHTML =
            `<div class="ToolTrack">` +
                `<div class="ToolSlide">` +
                    `<div class="ToolRail" data-Rail></div>` +
                    `<div class="ToolBody">` +
                        `<div class="PaneHead">` +
                            `<div><div class="PaneTitle" data-GridTitle></div>` +
                            `<div class="PaneSubtitle">Pick an instrument</div></div>` +
                        `</div>` +
                        `<div class="PaneScroll"><div class="TileGrid" data-Grid></div></div>` +
                    `</div>` +
                `</div>` +
                `<div class="ToolSlide">` +
                    `<div class="ToolRail" data-RailEcho></div>` +
                    `<div class="ToolBody">` +
                        `<div class="PaneHead">` +
                            `<button class="PaneBack" data-Back title="Back">` +
                                Svg(Stroke("M10 3 L5 8 L10 13"), 'viewBox="0 0 16 16"') +
                            `</button>` +
                            `<div><div class="PaneTitle" data-OptionTitle></div>` +
                            `<div class="PaneSubtitle" data-OptionSubtitle></div></div>` +
                        `</div>` +
                        `<div class="PaneScroll" data-Options></div>` +
                        `<div class="PaneFoot">` +
                            `<span data-FootNote></span>` +
                            `<button class="FootAction" data-Reset>Reset</button>` +
                        `</div>` +
                    `</div>` +
                `</div>` +
            `</div>`;

        this.Root.querySelector("[data-Back]").addEventListener("click", () => this.ShowGrid());
        this.Root.querySelector("[data-Reset]").addEventListener("click", () => {
            this.Params = { ...this.Active.Params };
            this.Store.set(this.Active.Key, this.Params);
            this.Commit();
            this.RenderOptions();
        });

        this.RenderRail();
    }

    // 📝 The rail is drawn into both slides. One rail spanning the track would have to sit outside it,
    //    and then it could not scroll with its own pane.
    RenderRail()
    {
        const Markup = INSTRUMENTS.map((Item) =>
            `<button class="RailItem${Item === this.Active ? " Active" : ""}" data-Band="${Item.Key}">` +
                `<span class="RailDot" style="background:${Item.Dot}"></span>` +
                `<span>${Item.Label}</span>` +
                `<span class="RailTally">1</span>` +
            `</button>`).join("");

        for (const Rail of this.Root.querySelectorAll("[data-Rail], [data-RailEcho]"))
        {
            Rail.innerHTML = Markup;
            for (const Button of Rail.querySelectorAll("[data-Band]"))
            {
                Button.addEventListener("click", () => {
                    const Chosen = INSTRUMENTS.find((Item) => Item.Key === Button.dataset.band);
                    this.Select(Chosen, true);
                    this.ShowGrid();
                });
            }
        }
    }

    RenderGrid(Animate)
    {
        const Grid = this.Root.querySelector("[data-Grid]");
        Grid.innerHTML =
            `<button class="Tile Active${Animate ? " TileFade" : ""}" data-Tile>` +
                `<span class="Well">${this.ArtFor(this.Active, NibView)}</span>` +
                `<span class="TileName">${this.Active.Name}</span>` +
            `</button>`;

        Grid.querySelector("[data-Tile]").addEventListener("click", () => this.ShowOptions());
        this.Root.querySelector("[data-GridTitle]").textContent = this.Active.Label;

        // 🔴 The mask value picker belongs on THIS pane — the one the right-click tap opens — not behind the
        //    tile click that slides to properties. When masking, choosing black or white is the whole point of
        //    summoning the card, so burying it one navigation step deep means the control the user asked for
        //    is not where they asked for it. It is rebuilt with the grid rather than mounted once because
        //    Select() re-renders this pane, which would otherwise discard the picker on any instrument change.
        const OnMask = this.OnMask();
        this.Root.querySelector("[data-GridTitle]").textContent = OnMask ? "Mask value" : this.Active.Label;
        this.Root.querySelector(".PaneSubtitle").textContent    = OnMask
            ? "Black hides · white reveals"
            : "Pick an instrument";

        if (OnMask) { Grid.appendChild(this.BuildMaskValue()); }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                     PROPERTIES
    //--------------------------------------------------------------------------------------------------------------------

    RenderPreview(Container)
    {
        Container.innerHTML =
            `<div class="PreviewStrip">` +
                `<div class="PreviewStand">${this.ArtFor(this.Active, ArtView)}</div>` +
                `<canvas class="PreviewCanvas" width="440" height="128"></canvas>` +
            `</div>`;

        this.PaintPreview(Container.querySelector("canvas"));
    }

    // A ribbon of dabs drawn with the same falloff shape the GPU uses, so the swatch is representative.
    PaintPreview(Canvas)
    {
        const Pen = Canvas.getContext("2d");
        const W = Canvas.width, H = Canvas.height;

        Pen.clearRect(0, 0, W, H);

        const Params  = this.Params;
        const Radius  = Math.max(2, Params.Size * 1.1);
        const Alpha   = (Params.Opacity / 100) * (Params.Flow / 100);
        const Grain   = (Params.Grain ?? 0) / 100;
        const Scatter = (Params.Scatter ?? 0) / 100;
        const [R, G, B] = ParseColour(this.Swatch).map((V) => Math.round(V * 255));

        const Dab = (X, Y, Scale) => {
            const Reach = Radius * Scale;
            const Wash  = Pen.createRadialGradient(X, Y, 0, X, Y, Reach);
            const Core  = Math.min(this.Active.Brush.Hardness, 0.95);
            Wash.addColorStop(0, `rgba(${R},${G},${B},${Alpha})`);
            Wash.addColorStop(Core, `rgba(${R},${G},${B},${Alpha})`);
            Wash.addColorStop(1, `rgba(${R},${G},${B},0)`);
            Pen.fillStyle = Wash;
            Pen.beginPath();
            Pen.arc(X, Y, Reach, 0, Math.PI * 2);
            Pen.fill();
        };

        // 🔴 Deterministic jitter, not Math.random. A preview that reshuffles on every slider tick
        //    makes it impossible to see what the slider actually changed.
        const Wobble = (Index, Salt) => Math.sin(Index * 12.9898 + Salt * 78.233) * 0.5;

        const Steps = 150;
        for (let Step = 0; Step <= Steps; Step += 1)
        {
            const Along = Step / Steps;
            const X = 24 + Along * (W - 48);
            const Y = H * 0.55 + Math.sin(Along * Math.PI * 1.6) * H * 0.2;

            // Pressure profile: thin at both ends when the instrument responds to pressure.
            const Swell = Params.Pressure ? Math.sin(Along * Math.PI) ** 0.6 : 1.0;
            const Taper = 1 - ((Params.Taper ?? 0) / 100) * (1 - Math.sin(Along * Math.PI));

            if (Grain > 0 && Wobble(Step, 3) + 0.5 < Grain * 0.55) { continue; }

            const DriftX = Scatter * Wobble(Step, 7) * Radius * 1.2;
            const DriftY = Scatter * Wobble(Step, 11) * Radius * 1.2;

            Dab(X + DriftX, Y + DriftY, Math.max(0.12, Swell * Taper));
        }
    }

    RenderOptions()
    {
        const Pane = this.Root.querySelector("[data-Options]");
        Pane.innerHTML = "";

        const Preview = document.createElement("div");
        Pane.appendChild(Preview);
        this.RenderPreview(Preview);

        for (const Control of VisibleControls(this.Active, this.Params))
        {
            Pane.appendChild(this.BuildControl(Control));
        }

        // Colour or value, never both — see BuildMaskValue for why offering a hue on a mask misleads.
        const OnMask = this.OnMask();
        Pane.appendChild(OnMask ? this.BuildMaskValue() : this.BuildSwatches());

        this.Root.querySelector("[data-OptionTitle]").textContent    = this.Active.Name;
        this.Root.querySelector("[data-OptionSubtitle]").textContent = OnMask
            ? `Masking · ${this.Params.Size}px`
            : `${this.Active.Label} · ${this.Params.Size}px`;

        const Inert = VisibleControls(this.Active, this.Params).filter((C) => !C.Wired).map((C) => C.Label);
        this.Root.querySelector("[data-FootNote]").textContent =
            Inert.length > 0 ? `${Inert.length} setting${Inert.length === 1 ? "" : "s"} preview only` : "All settings live";
    }

    BuildControl(Control)
    {
        const Row = document.createElement("div");
        Row.className = `ControlRow${Control.Wired ? "" : " Inert"}`;

        if (Control.Kind === "Switch")
        {
            Row.innerHTML =
                `<div class="SwitchRow">${Glyph(Control.Glyph)}<span>${Control.Label}</span>` +
                `<button class="Switch${this.Params[Control.Key] ? " On" : ""}" data-Switch></button></div>`;

            Row.querySelector("[data-Switch]").addEventListener("click", () => {
                this.Params[Control.Key] = !this.Params[Control.Key];
                this.Commit();
                // A switch can reveal or hide dependent rows, so the whole pane re-evaluates.
                this.RenderOptions();
            });
            return Row;
        }

        const Value = this.Params[Control.Key];
        const Shown = Control.Kind === "Slider" ? `${Value}${Control.Unit ?? ""}` : Value;

        Row.innerHTML =
            `<div class="ControlHead">${Glyph(Control.Glyph)}<span>${Control.Label}</span>` +
            `<span class="ControlValue" data-Readout>${Shown}</span></div>`;

        if (Control.Kind === "Slider")      { Row.appendChild(this.BuildSlider(Control, Row)); }
        else if (Control.Kind === "Segmented") { Row.appendChild(this.BuildSegmented(Control)); }
        else                                { Row.appendChild(this.BuildSelect(Control)); }

        return Row;
    }

    BuildSlider(Control, Row)
    {
        const Track = document.createElement("div");
        Track.className = "SliderTrack";
        Track.innerHTML = `<div class="SliderRail"></div><div class="SliderFill"></div><div class="SliderKnob"></div>`;

        const Fill = Track.querySelector(".SliderFill");
        const Knob = Track.querySelector(".SliderKnob");

        const Draw = () => {
            const Fraction = (this.Params[Control.Key] - Control.Min) / (Control.Max - Control.Min);
            Fill.style.width = `${Fraction * 100}%`;
            Knob.style.left  = `${Fraction * 100}%`;
        };

        const Set = (ClientX) => {
            const Box      = Track.getBoundingClientRect();
            const Fraction = Math.min(Math.max((ClientX - Box.left) / Box.width, 0), 1);
            const Raw      = Control.Min + Fraction * (Control.Max - Control.Min);
            const Snapped  = Math.round(Raw / Control.Step) * Control.Step;

            this.Params[Control.Key] = Snapped;
            Row.querySelector("[data-Readout]").textContent = `${Snapped}${Control.Unit ?? ""}`;
            Draw();
            this.Commit();
            this.PaintPreview(this.Root.querySelector(".PreviewCanvas"));

            if (Control.Key === "Size")
            {
                this.Root.querySelector("[data-OptionSubtitle]").textContent =
                    `${this.Active.Label} · ${Snapped}px`;
            }
        };

        // 🔴 Pointer capture, not a document-level listener. Without it a drag that leaves the 22px
        //    track stops tracking, which on a thin control is most drags.
        Track.addEventListener("pointerdown", (Event) => {
            Track.setPointerCapture(Event.pointerId);
            Set(Event.clientX);
        });
        Track.addEventListener("pointermove", (Event) => {
            if (Track.hasPointerCapture(Event.pointerId)) { Set(Event.clientX); }
        });
        Track.addEventListener("pointerup", (Event) => {
            if (Track.hasPointerCapture(Event.pointerId)) { Track.releasePointerCapture(Event.pointerId); }
        });

        Draw();
        return Track;
    }

    BuildSegmented(Control)
    {
        const Bar = document.createElement("div");
        Bar.className = "Segmented";
        Bar.innerHTML = Control.Options.map((Option) =>
            `<button class="Segment${Option === this.Params[Control.Key] ? " Active" : ""}" ` +
            `data-Option="${Option}">${Option}</button>`).join("");

        for (const Button of Bar.querySelectorAll("[data-Option]"))
        {
            Button.addEventListener("click", () => {
                this.Params[Control.Key] = Button.dataset.option;
                this.Commit();
                this.RenderOptions();
            });
        }
        return Bar;
    }

    BuildSelect(Control)
    {
        const Field = document.createElement("select");
        Field.className = "Select";
        Field.innerHTML = Control.Options.map((Option) =>
            `<option${Option === this.Params[Control.Key] ? " selected" : ""}>${Option}</option>`).join("");

        Field.addEventListener("change", () => {
            this.Params[Control.Key] = Field.value;
            this.Commit();
            this.RenderOptions();
        });
        return Field;
    }

    // The mask's value picker: a black-to-white ramp plus the two chips that matter, hide and reveal.
    //
    // 🔴 This REPLACES the hue swatches rather than sitting beside them, and that is the point of the whole
    //    control. A mask stores coverage, not colour, so a hue picker in the mask section offers a choice
    //    that cannot be expressed — pick crimson and the mask records 0.85 grey, which looks like the picker
    //    is broken. Showing only the values a mask can actually hold makes the constraint self-evident.
    //
    // 📝 A continuous ramp, not just the two chips, because a partial mask (a soft 40% blend) is a real thing
    //    to want and the storage has always been able to hold it — only the UI could not ask for it.
    BuildMaskValue()
    {
        const Row = document.createElement("div");
        Row.className = "ControlRow";
        Row.innerHTML =
            `<div class="ControlHead">${Glyph("Pigment")}<span>Mask value</span>` +
            `<span class="ControlValue" data-MaskReadout></span></div>` +
            `<div class="MaskRamp" data-Ramp><div class="MaskRampKnob" data-RampKnob></div></div>` +
            `<div class="SwatchRow">` +
                `<button class="Swatch MaskChip" style="background:#000" data-MaskValue="0" title="Hide (black)"></button>` +
                `<button class="Swatch MaskChip" style="background:#808080" data-MaskValue="0.5" title="Half"></button>` +
                `<button class="Swatch MaskChip" style="background:#fff" data-MaskValue="1" title="Reveal (white)"></button>` +
            `</div>`;

        const Ramp     = Row.querySelector("[data-Ramp]");
        const Knob     = Row.querySelector("[data-RampKnob]");
        const Readout  = Row.querySelector("[data-MaskReadout]");

        const Draw = () => {
            const Level = this.MaskLevel();
            Knob.style.left = `${Level * 100}%`;
            Readout.textContent = `${Math.round(Level * 100)}% · ${Level > 0.5 ? "reveal" : "hide"}`;
            for (const Chip of Row.querySelectorAll("[data-MaskValue]"))
            {
                Chip.classList.toggle("Active", Math.abs(Number(Chip.dataset.maskvalue) - Level) < 0.02);
            }
        };

        // 📝 No Draw() call here: SetMaskLevel syncs every mounted picker, this one included.
        const Set = (ClientX) => {
            const Box = Ramp.getBoundingClientRect();
            this.SetMaskLevel(Math.min(Math.max((ClientX - Box.left) / Box.width, 0), 1));
        };

        // 🔴 Pointer capture for the same reason the sliders use it: the ramp is thin and most drags leave it.
        Ramp.addEventListener("pointerdown", (Event) => {
            Ramp.setPointerCapture(Event.pointerId);
            Set(Event.clientX);
        });
        Ramp.addEventListener("pointermove", (Event) => {
            if (Ramp.hasPointerCapture(Event.pointerId)) { Set(Event.clientX); }
        });
        Ramp.addEventListener("pointerup", (Event) => {
            if (Ramp.hasPointerCapture(Event.pointerId)) { Ramp.releasePointerCapture(Event.pointerId); }
        });

        for (const Chip of Row.querySelectorAll("[data-MaskValue]"))
        {
            Chip.addEventListener("click", () => this.SetMaskLevel(Number(Chip.dataset.maskvalue)));
        }

        Draw();

        // 🔴 REGISTERED into a list, not stored as a single callback. Both the grid pane and the properties
        //    pane can have a picker mounted at once, and a lone field would leave whichever built second as
        //    the only one that ever refreshed — so pressing E would update one ramp and leave the other
        //    showing a stale value, with the two visibly disagreeing about what the brush will paint.
        //    Entries are filtered by isConnected on sync, so rebuilt panes drop out on their own.
        this.MaskValueDraws.push({ Node: Row, Draw });
        return Row;
    }

    // The live mask level off the brush, so the picker always reflects what a stroke would deposit —
    // including a change made by the E key while the card was open.
    MaskLevel() { return Math.min(Math.max(this.Brush?.MaskLevel ?? 0, 0), 1); }

    SetMaskLevel(Level)
    {
        if (this.Brush) { this.Brush.MaskLevel = Level; }
        // Every mounted picker refreshes, so the grid pane's ramp and the properties pane's ramp agree.
        this.SyncMaskValue();
        this.OnChange(this.Active, this.Params);
    }

    // Re-draw every mounted value picker after a change from anywhere — a chip, the ramp, or the E key.
    // Safe when the card is closed or carries no picker; detached rows are pruned as they are found.
    SyncMaskValue()
    {
        this.MaskValueDraws = this.MaskValueDraws.filter((Entry) => Entry.Node.isConnected);
        for (const Entry of this.MaskValueDraws) { Entry.Draw(); }
    }

    BuildSwatches()
    {
        const Row = document.createElement("div");
        Row.className = "ControlRow";
        Row.innerHTML =
            `<div class="ControlHead">${Glyph("Pigment")}<span>Colour</span></div>` +
            `<div class="SwatchRow">` +
            this.Active.Swatches.map((Colour) =>
                `<button class="Swatch${Colour === this.Swatch ? " Active" : ""}" ` +
                `style="background:${Colour}" data-Swatch="${Colour}"></button>`).join("") +
            `</div>`;

        for (const Chip of Row.querySelectorAll("[data-Swatch]"))
        {
            Chip.addEventListener("click", () => {
                this.Swatch = Chip.dataset.swatch;
                this.Picked.set(this.Active.Key, this.Swatch);
                this.Commit();
                this.RenderOptions();
            });
        }
        return Row;
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                                     BEHAVIOUR
    //--------------------------------------------------------------------------------------------------------------------

    Select(Instrument, Animate)
    {
        this.Active = Instrument;
        this.Params = this.Store.get(Instrument.Key);
        this.Swatch = this.Picked.get(Instrument.Key);

        this.RenderRail();
        this.RenderGrid(Animate);
        this.Commit();
    }

    // Push state to the brush and tell the host something changed.
    Commit()
    {
        ApplyToBrush(this.Brush, this.Active, this.Params, this.Swatch, this.OnMask());
        this.OnChange(this.Active, this.Params);
    }

    // Whether a mask paint target is focused, asked of the host rather than worked out here.
    //
    // 🔴 The menu deliberately does not import the layer stack or LayerMask. It is handed a brush and drives
    //    it; giving it a view of the stack to answer one styling question would couple the tool card to the
    //    layer model, and the host already resolves this every frame for the stroke path.
    OnMask() { return this.IsMasking ? this.IsMasking() === true : false; }

    // The selected tool and its full parameter set, as an immutable snapshot for a stroke record.
    //
    // 🔴 Exists because ApplyToBrush above is LOSSY by design: it folds the instrument down to hardness,
    //    spacing, one deposit strength and an ink triple, and the paint pass has nowhere to put the rest.
    //    Everything it discards — the instrument's identity, grade, nib, bristle, wetness, grain, scatter,
    //    bleed, taper, tilt, smoothing, pressure-sensitivity — is exactly what a reconstruction needs, so it
    //    is captured HERE, at the only place that still holds it, rather than recovered later (it cannot be).
    // 🔴 `Params` is spread into a new object. `this.Params` is the live entry from `Store`, mutated in place
    //    by every slider drag, so handing the reference out would let a stroke's recorded settings keep
    //    changing after it was laid — the history would rewrite its own past every time a slider moved.
    // 🔴 `Wired` is derived from the SCHEMA rather than hardcoded, so a control that gets wired to the brush
    //    later cannot leave this list claiming it was decorative on strokes that it actually shaped.
    Snapshot()
    {
        // 📝 The VISIBLE controls, not the whole schema: a hidden control (Taper with Pressure off) holds a
        //    stale value that had no effect on this stroke, and listing it as wired would be a lie.
        const Visible = VisibleControls(this.Active, this.Params);

        return {
            Key:    this.Active.Key,
            Name:   this.Active.Name,
            Label:  this.Active.Label,
            Schema: this.Active.Schema,
            // The instrument's rail colour, so a history row can be tinted by tool without re-importing
            // INSTRUMENTS and re-finding the entry by key.
            Tone:   this.Active.Dot,
            Swatch: this.Swatch,
            Params: { ...this.Params },
            Wired:  Visible.filter((Control) => Control.Wired).map((Control) => Control.Key)
        };
    }

    ShowGrid()
    {
        this.Root.classList.remove("Properties");
        this.OnProperties = false;
    }

    ShowOptions()
    {
        this.RenderOptions();
        this.Root.classList.add("Properties");
        this.OnProperties = true;
    }

    // Summon at a viewport position, clamped so the card never opens off-screen.
    Show(X, Y)
    {
        // 🔴 The grid is re-rendered on every summon, because whether a mask is focused can have changed
        //    since the card was last built — the user opens the mask tab, then right-clicks. Building it
        //    once at construction would show the instrument tile to someone who is masking, which is the
        //    "why isn't the picker there" case this control exists to answer.
        this.RenderGrid(false);
        this.Root.classList.add("Open");

        // 📝 Measured, not assumed. The card is content-box sized, so its border box is wider than the
        //    authored width; clamping against the authored number would let the edge sit off-screen.
        const Box    = this.Root.getBoundingClientRect();
        const Width  = Box.width  || 562;
        const Height = Box.height || Math.min(420, window.innerHeight * 0.78);
        const Margin = 8;

        this.Root.style.left = `${Math.min(Math.max(X, Margin), window.innerWidth  - Width  - Margin)}px`;
        this.Root.style.top  = `${Math.min(Math.max(Y, Margin), window.innerHeight - Height - Margin)}px`;
    }

    Hide()
    {
        this.Root.classList.remove("Open");
        this.ShowGrid();
    }

    AttachDismissal()
    {
        // Pressing outside closes. Capture phase, so the canvas cannot swallow the event first.
        window.addEventListener("pointerdown", (Event) => {
            if (this.Open && !this.Root.contains(Event.target)) { this.Hide(); }
        }, true);

        // 📝 Escape steps BACK through the carousel before closing. Closing outright from the property
        //    pane loses the sense of depth the slide just established.
        window.addEventListener("keydown", (Event) => {
            if (Event.key !== "Escape" || !this.Open) { return; }
            if (this.OnProperties) { this.ShowGrid(); } else { this.Hide(); }
            Event.preventDefault();
        });
    }
}

export { INSTRUMENTS };

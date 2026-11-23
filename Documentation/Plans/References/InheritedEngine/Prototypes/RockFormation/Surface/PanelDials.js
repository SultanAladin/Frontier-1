//========================================================================================================================
//                                                  PanelDials.js                                                  🧩
//========================================================================================================================
//
// 📝 Dial row construction, shared by the node cards and the march/weather strip.
//
//    🔴 A dial change calls OnDial("dial") and NEVER OnDial("topology"). That distinction is the whole
//       two-speed contract: topology recompiles the pipeline, a dial only rewrites the uniform. Wiring a
//       slider to "topology" would recompile the shader on every pointermove of a drag.
//
//    📝 The right-hand inspector this file used to own is GONE — the viewport and the tree are 50/50 and
//       entry dials live on the cards. What survives is ComposeEntryDialRow, which TreeSurface calls to
//       build those on-card rows, and the march/weather strip that has no card to live on.

import { ConstructionSpecificationTable, TintPaletteNaming, RepeatAxisNaming }
    from "../Construction/ConstructionSpecifications.js";
import { DefaultProvenance } from "../Construction/GeologyReference.js";

export function ComposePanel(Root, TreeState, ViewProfile, WeatherProfile, OnDial)
{
    const Panel =
    {
        Root,
        TreeState,
        ViewProfile,
        WeatherProfile,
        OnDial,
        MarchHost  : Root.querySelector("#MarchHost")
    };

    if (Panel.MarchHost) BuildMarchControls(Panel);
    return Panel;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   ENTRY DIALS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One dial row for an entry, built for the CARD. Exported because TreeSurface owns the cards now.
//
//    🔴 The slider must stopPropagation on pointerdown. A card is dragged by pointerdown anywhere inside
//       it, so without this a slider drag moves the card instead of turning the dial — the hazard noted
//       at TreeSurface.js, and the reason dials were kept off the cards in M1. The tool buttons already
//       solve it the same way.
export function ComposeEntryDialRow(Entry, Specification, Dial, Lane, OnDial)
{
    const [Label, Key, Minimum, Maximum, , Precision, Unit] = Dial;

    const Row = document.createElement("div");
    Row.className = "DialRow";

    const Head = document.createElement("div");
    Head.className = "DialHead";

    const Naming = document.createElement("span");
    Naming.className = "DialNaming";
    Naming.textContent = Label;

    // ⚠️ Surface provenance on the label. A self-tuned default must be visibly self-tuned, otherwise the
    //    whole table reads as measured and the two cannot be told apart later.
    const Provenance = DefaultProvenance[`${Entry.Species}.${Key}`];
    if (Provenance)
    {
        Naming.title = Provenance;
        const Mark = document.createElement("span");
        const SelfTuned = Provenance.startsWith("⚠️");
        Mark.className = SelfTuned ? "DialMark Tuned" : "DialMark Cited";
        Mark.textContent = SelfTuned ? "tuned" : "cited";
        Mark.title = Provenance;
        Naming.appendChild(Mark);
    }
    Head.appendChild(Naming);

    const Readout = document.createElement("span");
    Readout.className = "DialValue";
    Head.appendChild(Readout);

    Row.appendChild(Head);

    const Slider = document.createElement("input");
    Slider.type = "range";
    Slider.className = "DialSlider";
    Slider.min = String(Minimum);
    Slider.max = String(Maximum);

    // 📝 Integer-precision dials step by 1 so they cannot land between two named indices.
    Slider.step = Precision === 0 ? "1" : String(Math.pow(10, -Precision));
    Slider.value = String(Entry.Dials[Lane]);

    const Paint = () =>
    {
        const Value = Entry.Dials[Lane];
        if (Entry.Species === "StratumTint" && Key === "palette")
        {
            Readout.textContent = TintPaletteNaming[Math.round(Value)] || Value.toFixed(0);
        }
        else if (Entry.Species === "LateralRepeat" && Key === "axis")
        {
            Readout.textContent = RepeatAxisNaming[Math.round(Value)] || Value.toFixed(0);
        }
        else
        {
            const Tag = (Unit === "-" || Unit === "idx") ? "" : ` ${Unit}`;
            Readout.textContent = `${Value.toFixed(Precision)}${Tag}`;
        }
    };
    Paint();

    // 🔴 See the header note. Both pointerdown AND click are stopped: pointerdown starts the card drag,
    //    and click would fall through to the surface and re-engage in the middle of a slider release.
    Slider.addEventListener("pointerdown", Event => Event.stopPropagation());
    Slider.addEventListener("click",       Event => Event.stopPropagation());

    Slider.addEventListener("input", () =>
    {
        Entry.Dials[Lane] = Number(Slider.value);
        Paint();
        OnDial("dial");                                              // 🔴 never "topology"
    });

    Row.appendChild(Slider);
    return Row;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              WEATHER + MARCH CONTROLS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The CLIMATE the rock sits in. These are not tree dials: wind bearing is the same wind for aeolian
//    abrasion and for where salt spray lands, so it belongs to the world rather than to any one process.
//
//    🔴 Every one of these RESTARTS the sim, so they notify "weather" rather than "dial". Erosion has
//       history — a changed wind cannot be applied retroactively to material already carried away.
const WeatherDials =
[
    ["Step Delta",    "StepDelta",    0.02, 0.60, 2, "-",
        "⚠️ material removed per step — self-tuned; large values collapse the body before it can shape"],
    ["Wind Bearing",  "WindBearing",  0.00, 6.28, 2, "rad",
        "compass direction the wind blows toward; undercuts the windward flank"],
    ["Wind Strength", "WindStrength", 0.00, 1.60, 2, "-",
        "⚠️ aeolian abrasion scale — self-tuned"],
    ["Gravity Drop",  "GravityDrop",  0.00, 1.00, 2, "-",
        "downhill bias in thermal collapse; how far talus travels before it settles"],
    ["Repose Slope",  "ReposeSlope",  0.02, 0.60, 2, "-",
        "⚠️ slope past which loose material collapses — the angle of repose, self-tuned in grid units"]
];

// 📝 The march dials belong in the interface, not buried as constants, because StepScale in particular is
//    the difference between a solid arch and a punched-through one — and getting it wrong is silent.
const MarchDials =
[
    ["Step Scale",   "StepScale",     0.20,  1.00, 3, "-",
        "🔴 FIXED-STEP fraction of a cell — a density grid is not a distance field. Above ~0.7 the march undersamples and thin ligaments perforate"],
    ["Step Ceiling", "StepCeiling",  32,   640,    0, "idx",
        "march iterations before giving up — raise for deep grazing views"],
    ["Hit Tolerance","HitTolerance",  0.0002, 0.02, 4, "m",
        "surface proximity counted as contact; larger is faster and rounder"],
    ["Far Distance", "FarDistance",   8,    90,    0, "m",
        "ray abandonment range"],
    ["Scene Bound",  "SceneRadius",   1.5,  20.0,  1, "m",
        "🔴 march bound — rays skip the empty run-up to this sphere and stop past it; tighter is faster but too tight CLIPS geometry rather than degrading it"],
    ["Solar Bearing","SolarAzimuth",  0,     6.28, 2, "rad",
        "sun direction about the vertical"],
    ["Solar Height", "SolarElevation",0.02,  1.55, 2, "rad",
        "sun elevation; low sun rakes the strata and reveals relief"],
    ["Ambient",      "AmbientWeight", 0.0,   1.6,  2, "-",
        "sky dome plus warm ground bounce"],
    ["Shadow",       "ShadowWeight",  0.0,   1.0,  2, "-",
        "hard shadow strength; costs a second march per pixel"],
    ["Cavity",       "CavityWeight",  0.0,   1.0,  2, "-",
        "curvature darkening — concavities hold shadow, moisture and varnish"],
    ["Slope Dust",   "SlopeWeight",   0.0,   1.0,  2, "-",
        "pale dust caught on upward-facing ledges"],
    ["Exposure",     "Exposure",      0.2,   3.0,  2, "-",
        "pre-tonemap multiply"]
];

function BuildMarchControls(Panel)
{
    Panel.MarchHost.textContent = "";

    // ① Weather first — it is what the author reaches for most, and it restarts the sim.
    AppendBand(Panel.MarchHost, "Weather");
    for (const Dial of WeatherDials)
    {
        Panel.MarchHost.appendChild(
            ComposeProfileRow(Panel, Panel.WeatherProfile, Dial, "weather"));
    }

    // ② March and illumination — these only change how the SAME grid is displayed, so they never restart.
    AppendBand(Panel.MarchHost, "March & Illumination");
    for (const Dial of MarchDials)
    {
        Panel.MarchHost.appendChild(
            ComposeProfileRow(Panel, Panel.ViewProfile, Dial, "dial"));
    }
}

function AppendBand(Host, Naming)
{
    const Band = document.createElement("div");
    Band.className = "PanelDivide";
    Band.textContent = Naming;
    Host.appendChild(Band);
}

// 📝 A dial row bound to a plain profile object rather than to an entry. Reason is "weather" for the
//    climate dials (restart the sim) and "dial" for the march dials (redraw only).
function ComposeProfileRow(Panel, Profile, Dial, Reason)
{
    const [Label, Key, Minimum, Maximum, Precision, Unit, Explain] = Dial;

    const Row = document.createElement("div");
    Row.className = "DialRow";

    const Head = document.createElement("div");
    Head.className = "DialHead";

    const Naming = document.createElement("span");
    Naming.className = "DialNaming";
    Naming.textContent = Label;
    Naming.title = Explain;

    // ⚠️ Mark hazards and self-tuned values distinctly. A number nobody measured must not read the same
    //    as one somebody did.
    if (Explain.startsWith("🔴") || Explain.startsWith("⚠️"))
    {
        const Hazard = Explain.startsWith("🔴");
        const Mark = document.createElement("span");
        Mark.className = Hazard ? "DialMark Hazard" : "DialMark Tuned";
        Mark.textContent = Hazard ? "care" : "tuned";
        Mark.title = Explain;
        Naming.appendChild(Mark);
    }
    Head.appendChild(Naming);

    const Readout = document.createElement("span");
    Readout.className = "DialValue";
    Head.appendChild(Readout);
    Row.appendChild(Head);

    const Slider = document.createElement("input");
    Slider.type = "range";
    Slider.className = "DialSlider";
    Slider.min = String(Minimum);
    Slider.max = String(Maximum);
    Slider.step = Precision === 0 ? "1" : String(Math.pow(10, -Precision));
    Slider.value = String(Profile[Key]);

    const Paint = () =>
    {
        const Tag = (Unit === "-" || Unit === "idx") ? "" : ` ${Unit}`;
        Readout.textContent = `${Profile[Key].toFixed(Precision)}${Tag}`;
    };
    Paint();

    Slider.addEventListener("input", () =>
    {
        Profile[Key] = Number(Slider.value);
        Paint();
        Panel.OnDial(Reason);
    });

    Row.appendChild(Slider);
    return Row;
}

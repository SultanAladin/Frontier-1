//========================================================================================================================
//                                            RockFormationEntry.js                                                🧩
//========================================================================================================================
//
// 📝 Boot: provision the device, build the seed tree, wire the surface to the resolve, and run the loop.
//
//    The three-speed contract, enforced here in one place:
//
//      Notify("topology") -> AssemblePipeline (retranscribe + recompile) + reseed   ~tens of ms
//      Notify("dial")     -> WriteViewProfile + RESTART the sim                     ~one dispatch
//      Notify("view")     -> readouts only
//      every frame        -> a few erosion steps while the rock is still weathering
//
//    🔴 The render loop NEVER calls AssemblePipeline. A recompile is scheduled by a flag that the loop
//       consumes, so a topology change costs one recompile rather than one per frame.

import { ComposeTreeState, ComposeTopologyStamp } from "./Construction/TreeState.js";
import { ComposeSeedTree }                        from "./Construction/SeedTree.js";
import { ComposeDeviceHost, ProvisionDevice, AssemblePipeline,
         WriteViewProfile, InscribeSurface, ApplyResolveScale,
         SeedField, DriveErosion, RefreshSequence,
         ApplyBakeTier, InscribeEntryPreview }    from "./Resolve/DeviceHost.js";
import { ComposeTreeSurface, RebuildSurface, RedrawLinks,
         FrameTree, EngageEntry }                 from "./Surface/TreeSurface.js";
import { ComposeCatalogue, RebuildCatalogue }     from "./Surface/Catalogue.js";
import { ComposePanel }                           from "./Surface/PanelDials.js";
import { PreviewEdge }                            from "./Simulation/VoxelField.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                 BOOT DIAGNOSTICS
//------------------------------------------------------------------------------------------------------------------------

let BootSlot = 0;

function InscribeBootLine(Text, Flavour)
{
    const Line = document.getElementById(`BootLine${Math.min(BootSlot++, 2)}`);
    if (Line) { Line.textContent = Text; Line.className = `BootLine ${Flavour || ""}`; }
}

// 📝 Notices persist for a beat and stack, so a refused link during a drag does not erase the reason the
//    previous one was refused.
function PresentNotice(Text, Flavour)
{
    const Rail = document.getElementById("NoticeRail");
    const Chip = document.createElement("div");
    Chip.className = `Notice ${Flavour || ""}`;
    Chip.textContent = Text;
    Rail.appendChild(Chip);
    setTimeout(() => Chip.remove(), Flavour === "bad" ? 6200 : 2600);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  VIEW PROFILE
//------------------------------------------------------------------------------------------------------------------------

const ViewProfile =
{
    // 📝 Orbit TARGETS. The camera eases toward these rather than snapping to them — see the lag section.
    OrbitBearing   : 0.72,                                          // [rad]
    OrbitPitch     : 0.26,                                          // [rad]
    OrbitRange     : 9.4,                                           // [m]

    // 📝 The point the orbit looks at. Panning slides this, so the camera is no longer nailed to the
    //    world origin — which is what made close inspection of one flank impossible.
    FocusX         : 0.0,                                           // [m]
    FocusY         : 0.0,                                           // [m]
    FocusZ         : 0.0,                                           // [m]

    // 📝 StepScale is a FIXED-STEP fraction of a cell now, not a sphere-trace safety factor.
    //    🔴 A density grid is not a distance field. Above ~0.7 cell the march undersamples and thin
    //       ligaments perforate — Nyquist, not a tuning preference.
    StepScale      : 0.55,
    StepCeiling    : 320,
    HitTolerance   : 0.0022,
    FarDistance    : 42,

    // 📝 The march bound. Must contain the voxel domain (DomainRadius 2.6 m), with headroom for the
    //    diagonal — a cube of half-extent r has corner distance r*sqrt(3) = 4.5.
    //    ⚠️ Raise this if geometry goes missing — too tight CLIPS rather than degrades.
    SceneRadius    : 6.0,                                           // [m]

    // 📝 1 while interacting (matcap, half resolution), 0 when settled. Driven by TouchInteraction, never
    //    set by hand.
    PreviewMode    : 0,                                             // [-]

    SolarAzimuth   : 2.31,
    SolarElevation : 0.46,
    AmbientWeight  : 0.78,
    ShadowWeight   : 0.85,
    CavityWeight   : 0.62,
    SlopeWeight    : 0.44,
    Exposure       : 1.06,
    ResolveMode    : 0,

    // Derived each frame from the SMOOTHED orbit.
    Origin       : [0, 0, 9.4],
    Forward      : [0, 0, -1],
    Rightward    : [1, 0, 0],
    Upward       : [0, 1, 0],
    SolarBearing : [0.5, 0.7, 0.4],
    Viewport     : [808, 808]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 WEATHER PROFILE
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The global weather terms, shared by every process. These live outside the tree because they describe
//    the CLIMATE the rock sits in, not any one process — wind bearing is the same wind for aeolian
//    abrasion and for where salt spray lands.

const WeatherProfile =
{
    StepDelta     : 0.25,                                           // [-] material removed per step, scaled
    WindBearing   : 0.90,                                           // [rad] compass direction the wind blows toward
    WindStrength  : 0.75,                                           // [-]
    GravityDrop   : 0.42,                                           // [-] downhill bias in thermal collapse
    ReposeSlope   : 0.16,                                           // [-] slope past which talus collapses
    SeedTally     : 1.0                                             // [-] decorrelates the per-process noise
};

// 📝 How many erosion ROUNDS to run per frame while authoring. One round is every wired process once, so
//    the seed tree's five processes cost five dispatches per round.
//
//    ⚠️ Raising this makes the rock weather faster in wall-clock but does NOT make it converge sooner in
//       simulated time — it just spends more GPU per frame reaching the same place. It is a pacing dial,
//       not a quality one.
const RoundsPerFrame = 2;                                           // [idx]
const PreviewYearSpan = 4200;                                       // [-] narrative years per round, for the readout

// 📝 Stop stepping once the rock has weathered this far. Erosion is asymptotic — left running it
//    eventually removes everything, which is physically true over geological time and useless as a tool.
const RoundCeiling = 260;                                           // [idx]

//------------------------------------------------------------------------------------------------------------------------
//                                                  CAMERA LAG
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The camera eases toward its target rather than tracking the pointer exactly. Every orbit gesture
//    writes a TARGET; the loop advances the LIVE value toward it each frame.
//
//    🔴 Exponential smoothing must be FRAME-RATE CORRECTED. The naive form
//
//        Live = Live + (Target - Live) * Rate
//
//    is only correct at the frame rate it was tuned at: on a 144 Hz display it converges 2.4x faster than
//    on 60 Hz, so the feel of the camera changes with the monitor. The corrected form raises the retention
//    factor to the elapsed-time power, which is invariant:
//
//        Live = Target + (Live - Target) * pow(Retention, Elapsed / Reference)
//
//    ⚠️ Bearing must NOT be smoothed as a raw scalar if it is ever wrapped into [0, 2pi) — easing from
//       6.2 to 0.1 would sweep the long way round. It is deliberately left UNWRAPPED here so the
//       difference stays small and the shortest path is the arithmetic one.

const CameraRetention = 0.026;                                      // [-] fraction of the gap left after Reference
const CameraReference = 16.7;                                       // [ms] the frame time the retention is quoted at
const CameraSettled   = 0.0004;                                     // [-] gap below which the ease is called done

const LiveCamera =
{
    Bearing : ViewProfile.OrbitBearing,
    Pitch   : ViewProfile.OrbitPitch,
    Range   : ViewProfile.OrbitRange,
    FocusX  : 0.0,
    FocusY  : 0.0,
    FocusZ  : 0.0
};

// 📝 The axes that ease, as (live key, target key) pairs. Range shares no name with its target because
//    the profile calls it OrbitRange; the focus axes are named the same on both sides.
const CameraAxes =
[
    ["Bearing", "OrbitBearing"], ["Pitch", "OrbitPitch"], ["Range", "OrbitRange"],
    ["FocusX",  "FocusX"],       ["FocusY", "FocusY"],    ["FocusZ", "FocusZ"]
];

// 📝 Is the camera still short of its target? A PURE query — see the hazard on EaseCamera below.
function CameraMoving()
{
    return CameraAxes.some(([Live, Target]) =>
        Math.abs(ViewProfile[Target] - LiveCamera[Live]) >= CameraSettled);
}

// 📝 Advance the live camera toward the target. Returns true while still moving, so the loop knows to
//    keep drawing — without that the ease would stop wherever the last event left it.
//
//    🔴 This MUTATES. It must never be called merely to ask whether the camera has arrived: passing a
//       zero elapsed time still snaps every axis inside CameraSettled onto its target, so a "harmless"
//       query would quietly finish the glide it was asking about. Use CameraMoving() for that.
function EaseCamera(Elapsed)
{
    const Retain = Math.pow(CameraRetention, Math.min(Elapsed, 100) / CameraReference);

    let Moving = false;
    for (const [Live, Target] of CameraAxes)
    {
        const Want = ViewProfile[Target];
        const Gap  = Want - LiveCamera[Live];

        if (Math.abs(Gap) < CameraSettled) { LiveCamera[Live] = Want; continue; }

        LiveCamera[Live] = Want - Gap * Retain;
        Moving = true;
    }
    return Moving;
}

// 📝 Y-up throughout, matching the WGSL (EvaluateStratumBand reads Probe.y as the bedding axis).
//    ⚠️ A Z-up assumption here would put the strata on their side and the sun on the horizon.
//
//    Reads the LIVE camera, not the target — that is what makes the lag visible rather than merely stored.
function DeriveOrbit(Profile)
{
    const Cx = Math.cos(LiveCamera.Bearing), Sx = Math.sin(LiveCamera.Bearing);
    const Cy = Math.cos(LiveCamera.Pitch),   Sy = Math.sin(LiveCamera.Pitch);

    const Focus = [LiveCamera.FocusX, LiveCamera.FocusY, LiveCamera.FocusZ];

    // ① Orbit about the FOCUS, not the world origin.
    const Offset = [
        LiveCamera.Range * Cy * Sx,
        LiveCamera.Range * Sy,
        LiveCamera.Range * Cy * Cx
    ];
    Profile.Origin = [Focus[0] + Offset[0], Focus[1] + Offset[1], Focus[2] + Offset[2]];

    // ② Look at the focus.
    const Length = Math.hypot(Offset[0], Offset[1], Offset[2]) || 1;
    const Forward = [-Offset[0] / Length, -Offset[1] / Length, -Offset[2] / Length];
    Profile.Forward = Forward;

    // ③ Right = normalize(cross(Forward, WorldUp)), then Up = cross(Right, Forward).
    const WorldUp = [0, 1, 0];
    let Right = [
        Forward[1] * WorldUp[2] - Forward[2] * WorldUp[1],
        Forward[2] * WorldUp[0] - Forward[0] * WorldUp[2],
        Forward[0] * WorldUp[1] - Forward[1] * WorldUp[0]
    ];
    const RightLength = Math.hypot(Right[0], Right[1], Right[2]) || 1;
    Right = Right.map(Component => Component / RightLength);
    Profile.Rightward = Right;

    Profile.Upward = [
        Right[1] * Forward[2] - Right[2] * Forward[1],
        Right[2] * Forward[0] - Right[0] * Forward[2],
        Right[0] * Forward[1] - Right[1] * Forward[0]
    ];

    // ④ Solar bearing from azimuth and elevation, Y-up.
    Profile.SolarBearing = [
        Math.cos(Profile.SolarElevation) * Math.sin(Profile.SolarAzimuth),
        Math.sin(Profile.SolarElevation),
        Math.cos(Profile.SolarElevation) * Math.cos(Profile.SolarAzimuth)
    ];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     BOOT
//------------------------------------------------------------------------------------------------------------------------

const TreeState = ComposeTreeState();
ComposeSeedTree(TreeState);

const Frame = document.getElementById("ResolveFrame");
const Host  = ComposeDeviceHost(Frame, PresentNotice);

let RecompilePending = true;
let DrawPending      = true;
let RestartPending   = true;                                        // reseed the grid before the next step
let RoundsRun        = 0;                                           // [idx] since the last restart

//------------------------------------------------------------------------------------------------------------------------
//                                              PREVIEW vs FULL RESOLVE
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Two quality tiers. While the view or a dial is moving, resolve at half edge (a quarter of the pixels)
//    with the matcap shading path; once nothing has moved for PreviewSettle, resolve ONCE at full edge with
//    shadow, ambient occlusion and cavity darkening.
//
//    ⚠️ The preview is a means of steering the tree, not the look. Judge shading and colour on the settled
//       frame — tuning against the matcap would bake in compensation for terms that are simply switched off.

const PreviewDivisor = 2;                                           // [-] half edge -> quarter the pixels
const PreviewSettle  = 250;                                         // [ms] stillness before the full render

let PreviewUntil = 0;                                               // [ms] 0 means "already settled"

function ApplyPreview(Wanted)
{
    ViewProfile.PreviewMode = Wanted ? 1 : 0;

    // 🔴 A backing-store resize must be followed by rewriting Viewport in the uniform, or the ray fan is
    //    built from the previous size and the image shears. Tick does that on every draw, so flagging a
    //    draw here is what keeps the two in step.
    ApplyResolveScale(Host, Wanted ? PreviewDivisor : 1);
    DrawPending = true;
}

// 📝 Called by every interaction. Each call pushes the settle deadline out, so a continuous drag stays in
//    preview throughout and resolves once at the end rather than fighting to settle mid-gesture.
function TouchInteraction()
{
    if (PreviewUntil === 0) ApplyPreview(true);
    PreviewUntil = performance.now() + PreviewSettle;
    DrawPending  = true;
}

const Surface = ComposeTreeSurface(document.getElementById("TreeCanvas"), TreeState, Notify);
const Panel   = ComposePanel(document.getElementById("PanelRoot"), TreeState, ViewProfile,
                             WeatherProfile, Notify);
const Catalogue = ComposeCatalogue(document.getElementById("CatalogueRoot"), Surface, Identifier =>
{
    RebuildSurface(Surface);
    EngageEntry(Surface, Identifier);
    RebuildCatalogue(Catalogue);
    Notify("topology");
});

function Notify(Reason, Detail)
{
    if (Reason === "topology")
    {
        RecompilePending = true;
        DrawPending      = true;
        RestartPending   = true;
        RebuildCatalogue(Catalogue);
    }
    else if (Reason === "dial")
    {
        // 🔴 A dial change RESTARTS the sim. Erosion has history — there is no way to retroactively apply
        //    a changed hardness to material that has already been carried away, so continuing would show
        //    the new dial acting only on what is left of a rock shaped by the old one.
        RestartPending = true;
        TouchInteraction();
    }
    else if (Reason === "weather")
    {
        // 📝 Same restart, different owner: the weather dials live outside the tree.
        RestartPending = true;
        TouchInteraction();
    }
    else if (Reason === "refused")
    {
        PresentNotice(Detail, "bad");
    }

    // 📝 Both a topology change and a dial turn can change what a card shows, so both re-stamp. The stamp
    //    comparison inside is what keeps this from repainting cards that did not actually change.
    if (Reason === "topology" || Reason === "dial") SchedulePreviews();

    RefreshReadouts();
}

function RefreshReadouts()
{
    document.getElementById("FootEntries").textContent = TreeState.Entries.size;
    document.getElementById("FootLinks").textContent   = TreeState.Links.length;
    document.getElementById("FootLines").textContent   = Host.LineTally;
    document.getElementById("FootZoom").textContent    = `${Math.round(Surface.Zoom * 100)}%`;
    document.getElementById("FootState").textContent   =
        Host.LastError ? `🔴 ${Host.LastError.slice(0, 68)}` : "🟢 resolving";
    document.getElementById("StampTag").textContent =
        `${ComposeTopologyStamp(TreeState).split("|")[0].split(",").length} entries transcribed`;

    // 📝 Years is a NARRATIVE readout, not a measurement. The sim has no calendar — it has steps. Showing
    //    "years" makes the progression legible without implying the number was derived from anything.
    const Years = RoundsRun * PreviewYearSpan;
    const Tag   = Years >= 1e6 ? `${(Years / 1e6).toFixed(2)} My` : `${(Years / 1000).toFixed(0)} ky`;
    const Readout = document.getElementById("FootYears");
    if (Readout)
    {
        Readout.textContent = RoundsRun >= RoundCeiling ? `${Tag} · settled` : Tag;
    }
    const Grid = document.getElementById("FootGrid");
    if (Grid) Grid.textContent = `${Host.Field ? Host.Field.Edge : PreviewEdge}³`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 ENTRY PREVIEWS
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 One thumbnail per card, repainted lazily.
//
//    🔴 ONE preview per frame, never a batch. A preview is a full sphere-trace plus a GPU->CPU readback,
//       and mapAsync waits for the queue to drain — painting twelve cards in one frame would stall the
//       erosion sim behind twelve round trips and drop the viewport to a slideshow. Spread over frames the
//       whole tree refreshes in a fifth of a second and nothing visibly hitches.
//
//    🔴 A preview OVERWRITES the shared uniform with its own camera, so DrawPending is set afterwards.
//       Without that the next viewport frame would reuse whatever camera the last thumbnail left behind.

const PreviewQueue = [];                                            // [idx] identifiers awaiting a repaint
const PreviewStamps = new Map();                                    // Identifier -> stamp last painted
let PreviewBusy = false;

// 🔴 Previews wait for the author to STOP dragging. A slider fires `input` on every pointermove, and a
//    preview whose dials changed misses its pipeline cache and compiles a fresh shader — so draining on
//    each event would compile a hundred-odd shaders over one drag, on top of a readback each. The dial
//    itself stays live (the sim restarts immediately); only the thumbnail waits.
const PreviewSettleDelay = 220;                                     // [ms]
let PreviewReadyAt = 0;                                             // [ms] earliest the queue may drain

// 📝 Stamp an entry by what its OWN image depends on: its dials, its species, and the upstream tree that
//    feeds it. Anything else changing must not cost a recompile and a readback.
function ComposeEntryStamp(Identifier)
{
    const Entry = TreeState.Entries.get(Identifier);
    if (!Entry) return "";

    const Parts = [];
    const Seen  = new Set();
    const Walk  = [Identifier];

    while (Walk.length > 0)
    {
        const Current = Walk.pop();
        if (Seen.has(Current)) continue;
        Seen.add(Current);

        const Node = TreeState.Entries.get(Current);
        if (!Node) continue;
        Parts.push(`${Current}:${Node.Species}:${Node.Bypassed ? 1 : 0}:${Node.Dials.join(",")}`);

        for (const Link of TreeState.Links)
        {
            if (Link.TargetEntry === Current) Walk.push(Link.SourceEntry);
        }
    }
    return Parts.sort().join("|");
}

// 📝 Queue every card whose stamp has moved. Cheap enough to run on any edit — the walk is over a tree of
//    tens of entries, and it is what stops an unchanged card from being repainted.
//
//    🔴 A card canvas is a NEW element after every RebuildSurface, holding no pixels, so a stamp alone is
//       not enough to say a card is painted — the stamp would match while the canvas sat blank. Painted
//       canvases are marked, and an unmarked one always repaints regardless of its stamp.
function SchedulePreviews(Only)
{
    PreviewReadyAt = performance.now() + PreviewSettleDelay;

    for (const Identifier of Surface.Previews.keys())
    {
        if (Only !== undefined && Identifier !== Only) continue;

        const Canvas = Surface.Previews.get(Identifier);
        const Stamp  = ComposeEntryStamp(Identifier);

        if (PreviewStamps.get(Identifier) === Stamp && Canvas && Canvas.dataset.painted) continue;

        if (Canvas) Canvas.classList.add("Stale");
        if (!PreviewQueue.includes(Identifier)) PreviewQueue.push(Identifier);
    }
}

async function DrainPreviewQueue()
{
    if (PreviewBusy || PreviewQueue.length === 0) return;

    const Identifier = PreviewQueue.shift();
    const Canvas = Surface.Previews.get(Identifier);

    // 📝 The card may have been removed, or rebuilt, between queueing and now.
    if (!Canvas || !TreeState.Entries.has(Identifier)) return;

    PreviewBusy = true;
    const Stamp = ComposeEntryStamp(Identifier);
    try
    {
        const Painted = await InscribeEntryPreview(Host, TreeState, Identifier, Canvas, Stamp);
        // 📝 The stamp is recorded either way. An entry with no previewable image — a weather process —
        //    must not be retried on every frame just because it refused once.
        PreviewStamps.set(Identifier, Stamp);
        Canvas.dataset.painted = "1";
        Canvas.classList.remove("Stale");
        if (!Painted) Canvas.style.display = "none";
    }
    finally
    {
        PreviewBusy = false;
        DrawPending = true;                                          // 🔴 restore the viewport camera
    }
}

// 📝 Clicking a thumbnail forces it to repaint, the escape hatch for a stale image the stamp missed.
Surface.OnPreview = Identifier =>
{
    PreviewStamps.delete(Identifier);
    SchedulePreviews(Identifier);
};

// 🔴 Engaging rebuilds every card, so every thumbnail is a blank new canvas. Without this the tree goes
//    grey on each selection and only repaints on the next edit.
Surface.OnEngage = () => SchedulePreviews();

//------------------------------------------------------------------------------------------------------------------------
//                                                  FRAME EXTENT
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The viewport is now half the window rather than a fixed 808 px square, so the backing store has to
//    follow the element. Sized in DEVICE pixels and capped: at a 4K display with devicePixelRatio 2 an
//    uncapped half-window canvas is ~1900x2000 fixed-step marched pixels, which is not a preview.
//
//    🔴 ApplyResolveScale owns the preview divisor. This function must record the CSS extent and let that
//       function apply the divisor, or the two fight and the image shears.

const FrameEdgeCeiling = 1280;                                      // [px]

function ApplyFrameExtent()
{
    const Box = Frame.getBoundingClientRect();
    if (Box.width < 2 || Box.height < 2) return;

    const Ratio = Math.min(window.devicePixelRatio || 1, 2);
    const Scale = Math.min(1, FrameEdgeCeiling / (Math.max(Box.width, Box.height) * Ratio));

    Host.FrameWidth  = Math.max(2, Math.round(Box.width  * Ratio * Scale));
    Host.FrameHeight = Math.max(2, Math.round(Box.height * Ratio * Scale));

    ApplyResolveScale(Host, ViewProfile.PreviewMode ? PreviewDivisor : 1);
    DrawPending = true;
}

// 📝 Debounced: a window drag fires resize continuously, and reallocating the backing store per event
//    stalls the compositor for the whole gesture.
let ExtentTimer = 0;
window.addEventListener("resize", () =>
{
    clearTimeout(ExtentTimer);
    ExtentTimer = setTimeout(ApplyFrameExtent, 90);
    TouchInteraction();
});

//------------------------------------------------------------------------------------------------------------------------
//                                                 ORBIT GESTURES
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 Left drag orbits. Middle drag, right drag, or shift+left pans — three bindings because no single one
//    is universal across the tools an author is likely coming from.
//
//    🔴 Pan moves the FOCUS along the camera's own right and up axes, not along world axes. Panning in
//       world space would drift sideways whenever the camera is not axis-aligned, which reads as the
//       model sliding rather than the view moving.

let OrbitDrag = null;

Frame.addEventListener("contextmenu", Event => Event.preventDefault());

Frame.addEventListener("pointerdown", Event =>
{
    const Panning = Event.button === 1 || Event.button === 2 || Event.shiftKey;

    OrbitDrag =
    {
        X: Event.clientX, Y: Event.clientY,
        Panning,
        Bearing : ViewProfile.OrbitBearing,
        Pitch   : ViewProfile.OrbitPitch,
        FocusX  : ViewProfile.FocusX,
        FocusY  : ViewProfile.FocusY,
        FocusZ  : ViewProfile.FocusZ
    };
    Frame.setPointerCapture(Event.pointerId);
    Event.preventDefault();
});

Frame.addEventListener("pointermove", Event =>
{
    if (!OrbitDrag) return;

    const DriftX = Event.clientX - OrbitDrag.X;
    const DriftY = Event.clientY - OrbitDrag.Y;

    if (OrbitDrag.Panning)
    {
        // 📝 Scale the pan by RANGE so a drag covers the same fraction of the screen at any zoom. Without
        //    this, panning while zoomed in flings the model off-frame.
        const Reach = ViewProfile.OrbitRange * 0.0016;
        const Right = ViewProfile.Rightward;
        const Up    = ViewProfile.Upward;

        ViewProfile.FocusX = OrbitDrag.FocusX - (Right[0] * DriftX - Up[0] * DriftY) * Reach;
        ViewProfile.FocusY = OrbitDrag.FocusY - (Right[1] * DriftX - Up[1] * DriftY) * Reach;
        ViewProfile.FocusZ = OrbitDrag.FocusZ - (Right[2] * DriftX - Up[2] * DriftY) * Reach;

        TouchInteraction();
        return;
    }

    ViewProfile.OrbitBearing = OrbitDrag.Bearing + DriftX * 0.0068;

    // 🔴 Clamp the pitch shy of the poles. At exactly +/- pi/2 the Forward vector becomes parallel to
    //    WorldUp and the cross product collapses, which flips the frame and reads as the view snapping.
    const Limit = Math.PI * 0.5 - 0.035;
    ViewProfile.OrbitPitch = Math.min(Math.max(OrbitDrag.Pitch - DriftY * 0.0068, -Limit), Limit);

    TouchInteraction();
});

Frame.addEventListener("pointerup", () => { OrbitDrag = null; });
Frame.addEventListener("pointercancel", () => { OrbitDrag = null; });

Frame.addEventListener("wheel", Event =>
{
    Event.preventDefault();
    const Factor = Event.deltaY < 0 ? 1 / 1.08 : 1.08;
    ViewProfile.OrbitRange = Math.min(Math.max(ViewProfile.OrbitRange * Factor, 2.2), 34.0);
    TouchInteraction();
}, { passive: false });

// 📝 Double-click recentres. Panning with no way back strands the author looking at empty space.
Frame.addEventListener("dblclick", () =>
{
    ViewProfile.FocusX = 0; ViewProfile.FocusY = 0; ViewProfile.FocusZ = 0;
    TouchInteraction();
});

for (const Tab of document.querySelectorAll(".ResolveTab"))
{
    Tab.addEventListener("click", () =>
    {
        for (const Other of document.querySelectorAll(".ResolveTab")) Other.classList.remove("Live");
        Tab.classList.add("Live");
        ViewProfile.ResolveMode = Number(Tab.dataset.mode);

        // 📝 Switching resolve mode is a deliberate inspection, not a drag — draw at full quality straight
        //    away rather than showing a half-resolution matcap of the mode you asked to look at.
        DrawPending = true;
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SIM CONTROLS
//------------------------------------------------------------------------------------------------------------------------

const RestartButton = document.getElementById("SimRestart");
if (RestartButton)
{
    RestartButton.addEventListener("click", () =>
    {
        RestartPending = true;
        DrawPending    = true;
        PresentNotice("restarted from the block", "good");
    });
}

const DrawerButton = document.getElementById("MarchToggle");
const MarchDrawer  = document.getElementById("MarchDrawer");
if (DrawerButton && MarchDrawer)
{
    DrawerButton.addEventListener("click", () =>
    {
        const Open = MarchDrawer.classList.toggle("Shut") === false;
        DrawerButton.classList.toggle("Live", Open);

        // 🔴 Opening the drawer SHRINKS the canvas. Without a resize the backing store keeps its old
        //    height while the element is shorter, so the image stretches until the next window resize.
        ApplyFrameExtent();
    });
}

const BakeButton = document.getElementById("SimBake");
if (BakeButton)
{
    BakeButton.addEventListener("click", async () =>
    {
        const Wanted = !Host.Baking;
        const Edge = ApplyBakeTier(Host, Wanted);

        // 🔴 A tier switch reallocates the field, so the march layout object is new and the pipeline MUST
        //    be rebuilt. ProvisionField clears the topology stamp for exactly this reason.
        RecompilePending = true;
        RestartPending   = true;
        DrawPending      = true;

        BakeButton.textContent = Wanted ? "preview" : "bake";
        BakeButton.classList.toggle("Live", Wanted);
        PresentNotice(`grid ${Edge}³ — reseeding`, "good");
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   RENDER LOOP
//------------------------------------------------------------------------------------------------------------------------

async function Run()
{
    InscribeBootLine("provisioning WebGPU…");

    const Provisioned = await ProvisionDevice(Host);
    if (!Provisioned)
    {
        InscribeBootLine(Host.LastError, "bad");
        InscribeBootLine("the tree editor still works — only the resolve is dark", "");
        RebuildSurface(Surface);
        FrameTree(Surface);
        RefreshReadouts();
        return;
    }

    InscribeBootLine("device ready", "good");

    // 🔴 Before the first draw: the markup's 808² is a placeholder, and marching at it while the element
    //    is a different shape stretches the very first frame the author sees.
    ApplyFrameExtent();

    RebuildSurface(Surface);
    FrameTree(Surface);

    const Assembled = await AssemblePipeline(Host, TreeState);
    RecompilePending = false;

    if (!Assembled)
    {
        InscribeBootLine(Host.LastError || "pipeline failed", "bad");
        RefreshReadouts();
        return;
    }

    RefreshSequence(Host, TreeState);

    InscribeBootLine(`${Host.LineTally} lines transcribed`, "good");
    document.getElementById("BootPlate").classList.add("Away");
    RefreshReadouts();
    SchedulePreviews();                                             // paint the cards, one per frame

    // 📝 Render ON DEMAND, not every rAF.
    //
    //    🔴 This was the single largest cost in the prototype, and it was invisible: a still view was
    //       re-marched 60 times a second to produce 60 identical images. The loop now sleeps unless
    //       something actually changed — the sim stepping, the camera easing, or an edit.
    let Busy = false;
    let LastTick = performance.now();

    async function Tick(Now)
    {
        const Elapsed = Math.max(1, Now - LastTick);
        LastTick = Now;

        // ① A topology change recompiles at most once, never once per frame.
        if (RecompilePending && !Busy)
        {
            Busy = true;
            const Fine = await AssemblePipeline(Host, TreeState);
            RecompilePending = false;
            Busy = false;
            DrawPending = true;
            if (Fine)
            {
                RefreshSequence(Host, TreeState);
                if (Host.LastError === null) PresentNotice("retranscribed", "good");
            }
            RefreshReadouts();
        }

        // ② Reseed. This is what makes a dial turn mean "start again with the new rock", not "keep
        //    eroding the old one under new rules".
        if (RestartPending && !Busy && !RecompilePending)
        {
            WriteViewProfile(Host, TreeState, ViewProfile);          // the seed reads U.Entry for the body
            SeedField(Host, TreeState, WeatherProfile);
            RestartPending = false;
            RoundsRun      = 0;
            DrawPending    = true;
        }

        // ③ Step the sim while it still has somewhere to go.
        if (!Busy && !RecompilePending && !RestartPending && RoundsRun < RoundCeiling)
        {
            WeatherProfile.SeedTally = RoundsRun + 1;
            DriveErosion(Host, WeatherProfile, RoundsPerFrame);
            RoundsRun += RoundsPerFrame;
            DrawPending = true;

            // 📝 Refresh the Years readout as it climbs, but not the whole footer — that would touch the
            //    DOM several times a second for text that has not changed.
            if (RoundsRun % 10 < RoundsPerFrame) RefreshReadouts();
            if (RoundsRun >= RoundCeiling) RefreshReadouts();
        }

        // ④ Ease the camera. Still moving means still drawing.
        if (EaseCamera(Elapsed)) DrawPending = true;

        // ⑤ Settle from preview to full resolution once interaction stops AND the camera has arrived.
        //    ⚠️ Settling while the ease is still running would render the expensive full-quality frame
        //       mid-glide and then immediately throw it away.
        if (PreviewUntil !== 0 && Now > PreviewUntil && !CameraMoving())
        {
            PreviewUntil = 0;
            ApplyPreview(false);
        }

        // ⑥ One card thumbnail per frame, AWAITED under Busy.
        //
        //    🔴 This must not run unawaited beside the draw. A preview writes the shared uniform with its
        //       own camera and then suspends on mapAsync; the continuation lands in the middle of a later
        //       frame, so an un-gated preview would overwrite the viewport's uniform between its
        //       WriteViewProfile and its submit — the viewport would flash the thumbnail's camera.
        if (!Busy && !RecompilePending && !RestartPending
            && PreviewQueue.length > 0 && Now >= PreviewReadyAt)
        {
            Busy = true;
            await DrainPreviewQueue();
            Busy = false;
        }

        if (DrawPending && !Busy)
        {
            DeriveOrbit(ViewProfile);
            ViewProfile.Viewport = [Frame.width, Frame.height];
            WriteViewProfile(Host, TreeState, ViewProfile);
            InscribeSurface(Host);
            DrawPending = false;
        }

        requestAnimationFrame(Tick);
    }

    requestAnimationFrame(Tick);
}

Run();

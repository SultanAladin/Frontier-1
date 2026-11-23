//============================================================================================================================================
//                                                         SYMBOLSPECIFICATION.H
//============================================================================================================================================
// 🧩 Stroke figures declared in a 24-unit square, registered by discipline — no raster, no store, no vendor library.

#pragma once

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE DISCIPLINES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The working discipline a figure belongs to. Every figure is registered in exactly one.
/// note  🔴 `Geometry` and not `Modelling`: `Model` is a banned structural word and the ban has no carve-out
///       for the participle. `Assembly` and not `Compositing`, for the same reason.
/// note  The registration is a property of the **figure**, not of where it is presented. A magnifier is a
///       navigation figure wherever it is drawn.
/// tag   guarantee
enum class SymbolDiscipline : std::uint32_t
{
    Workspace            =  0u,   // [-] - the shell itself: folders, arrangements, panels
    Navigation           =  1u,   // [-] - traversal: chevrons, magnifier, crosshair
    Geometry             =  2u,   // [-] - polygonal modelling: vertices, edges, faces, booleans
    ComputerAidedDesign  =  3u,   // [-] - constrained sketching, revolution, fillet, loft
    Sculpting            =  4u,   // [-] - bristles, inflation, relaxation, retopology density
    Texturing            =  5u,   // [-] - unwrap seams, material spheres, channels, stencils
    Illumination         =  6u,   // [-] - directional, point, area and dome emitters
    Rendering            =  7u,   // [-] - aperture, convergence, denoise, exposure
    Animation            =  8u,   // [-] - key ordinates, tangents, scrub, joints
    Simulation           =  9u,   // [-] - cloth, fluid, rigid collision, particles
    Assembly             = 10u,   // [-] - layer merge, alpha masks, colour, junction graphs
    Measurement          = 11u,   // [-] - pulse traces, rulers, histograms, readouts
    LayerStack           = 12u,   // [-] - the texture-texture stack: undo, blend, mask, add actions
    DisciplineCount      = 13u    // [-] - the closed count, never a discipline
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SUBJECTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every figure the interface may ask for, ordered by discipline.
/// note  🚧 Only the seven the source actually draws carry declared artwork. Every other subject resolves to
///       `PlaceholderMark` at the correct extent, so the roster may be filled in later without a single
///       layout moving. That is the whole reason the extents are exact now and the artwork is not.
/// tag   guarantee
enum class SymbolSubject : std::uint32_t
{
    // Workspace ---------------------------------------------------------------------------------------------
    FolderClosed        =  0u,   // 🟢 lucide `folder`      — the Asset Browser tongue
    LatticeArrangement  =  1u,   // 🟢 lucide `grid-3x3`    — the lattice toggle
    ColumnArrangement   =  2u,   // 🟢 lucide `list`        — the column toggle
    PanelSplit          =  3u,   // 🚧
    PersistDisc         =  4u,   // 🚧
    BulbFilament        =  5u,   // 🟢 lucide `lightbulb`   — the tooltip trigger, and the light entity
    EyeOpen             =  6u,   // 🟢 lucide `eye`         — an outline row that is present
    EyeClosed           =  7u,   // 🟢 lucide `eye-off`     — an outline row that is withheld
    PlusCross           =  8u,   // 🟢 lucide `plus`        — the new-record action
    TrashBin            =  9u,   // 🟢 lucide `trash-2`     — the delete action
    GearCog             = 10u,   // 🟢 lucide `settings`    — the World Outliner medallion
    SpeakerCone         = 11u,   // 🟢 lucide `volume-2`    — the audio entity
    CodeBrackets        = 12u,   // 🟢 lucide `code`        — the script entity

    // Navigation --------------------------------------------------------------------------------------------
    ChevronDown         = 13u,   // 🟢 lucide `chevron-down`
    ChevronRight        = 14u,   // 🟢 lucide `chevron-right`
    MagnifierLens       = 15u,   // 🟢 lucide `search`
    ArrowReturn         = 16u,   // 🚧
    CrosshairCentre     = 17u,   // 🟢 lucide `crosshair`   — the trigger entity

    // Geometry ----------------------------------------------------------------------------------------------
    VertexPoint         = 18u,   // 🚧
    EdgeSegment         = 19u,   // 🚧
    FacePlanar          = 20u,   // 🚧
    SubdivisionStep     = 21u,   // 🚧
    ExtrudeSpan         = 22u,   // 🚧
    BevelChamfer        = 23u,   // 🚧
    BooleanUnion        = 24u,   // 🚧
    MirrorAxis          = 25u,   // 🚧
    CubeSolid           = 26u,   // 🟢 lucide `box`         — the level and actor entities

    // Computer-aided design ---------------------------------------------------------------------------------
    SketchPlane         = 27u,   // 🚧
    ConstraintDimension = 28u,   // 🚧
    FilletRadius        = 29u,   // 🚧
    RevolveAxis         = 30u,   // 🚧
    LoftProfile         = 31u,   // 🚧

    // Sculpting ---------------------------------------------------------------------------------------------
    BristleTip          = 32u,   // 🚧
    InflatePush         = 33u,   // 🚧
    SmoothRelax         = 34u,   // 🚧
    MaskStencil         = 35u,   // 🚧
    RetopologyDensity       = 36u,   // 🚧

    // Texturing ---------------------------------------------------------------------------------------------
    UnwrapSeam          = 37u,   // 🚧
    Bristle        = 38u,   // 🚧
    MaterialSphere      = 39u,   // 🚧
    ChannelSelect       = 40u,   // 🚧
    StencilProjection   = 41u,   // 🚧

    // Illumination ------------------------------------------------------------------------------------------
    SunDirectional      = 42u,   // 🚧
    LampPoint           = 43u,   // 🚧
    AreaEmitter         = 44u,   // 🚧
    SkyDome             = 45u,   // 🚧

    // Rendering ---------------------------------------------------------------------------------------------
    CameraAperture      = 46u,   // 🟢 lucide `camera`      — the camera entity
    SampleConverge      = 47u,   // 🚧
    DenoiseSweep        = 48u,   // 🚧
    ExposureCoordinate    = 49u,   // 🚧

    // Animation ---------------------------------------------------------------------------------------------
    KeyCoordinate         = 50u,   // 🚧
    CurveTangent        = 51u,   // 🚧
    TimelineScrub       = 52u,   // 🚧
    SkeletonJoint       = 53u,   // 🚧

    // Simulation --------------------------------------------------------------------------------------------
    ClothDrape          = 54u,   // 🚧
    FluidStream         = 55u,   // 🚧
    RigidCollide        = 56u,   // 🚧
    ParticleEmit        = 57u,   // 🟢 lucide `sparkles`    — the particle entity

    // Assembly ----------------------------------------------------------------------------------------------
    LayerMerge          = 58u,   // 🟢 lucide `layers`      — the Layer Stack medallion
    AlphaMask           = 59u,   // 🚧
    ColourWheel         = 60u,   // 🚧
    GraphJunction       = 61u,   // 🚧

    // Measurement -------------------------------------------------------------------------------------------
    PulseTrace          = 62u,   // 🟢 lucide `activity`    — the Control Center tongue
    RulerSpan           = 63u,   // 🚧
    HistogramProfile    = 64u,   // 🚧
    StatisticReadout    = 65u,   // 🚧

    PlaceholderMark     = 66u,   // 🟢 what every unresolved subject above draws as

    // Layer stack -------------------------------------------------------------------------------------------
    // 🟢 The LayerstackV1 reference's own icons, appended so every ordinal above stays put.
    UndoArrow           = 67u,   // 🟢 lucide `undo-2`       — the header's undo
    RedoArrow           = 68u,   // 🟢 lucide `redo-2`       — the header's redo
    ExpandFrame         = 69u,   // 🟢 lucide `maximize-2`   — the header's wide/full toggle
    CollapseFold        = 70u,   // 🟢 lucide `chevrons-up-down` — the collapse/expand-all tool
    EllipsisDots        = 71u,   // 🟢 lucide `ellipsis`     — the row's "more" menu
    DropletDrop         = 72u,   // 🟢 lucide `droplet`      — a fill layer
    AdjustmentSliders   = 73u,   // 🟢 lucide `sliders-horizontal` — an adjustment layer
    FilterFunnel        = 74u,   // 🟢 lucide `funnel`       — a filter layer
    StencilDecal        = 75u,   // 🟢 the reference's decal — a decal layer
    TiledPattern        = 76u,   // 🟢 lucide `layout-grid`  — a pattern layer
    GeneratorSpark      = 77u,   // 🟢 the reference's spark — a generator layer
    CopyDuplicate       = 78u,   // 🟢 lucide `copy`         — the duplicate action
    LockClosed          = 79u,   // 🟢 lucide `lock`         — the lock action
    LockOpen            = 80u,   // 🟢 lucide `unlock`       — the unlock action
    ArrowUpLine         = 81u,   // 🟢 lucide `arrow-up`     — the move-up action
    ArrowDownLine       = 82u,   // 🟢 lucide `arrow-down`   — the move-down action
    HalfMask            = 83u,   // 🟢 the reference's mask  — a mask row, and the mask tool
    CrossClose          = 84u,   // 🟢 lucide `x`            — dismisses a widget; NOT PlusCross turned 45°,
                                 //                            because a turned figure keeps its own extent
                                 //                            and lands off-centre in a square button
    SubjectCount        = 85u    // [-] - the closed count, never a subject
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE STROKE STREAM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one step of a figure's stroke stream does.
/// note  `Enclosure` and not the obvious spelling: `Frame` is a banned word.
/// tag   guarantee
enum class StrokeCommand : std::uint32_t
{
    Origin     = 0u,   // [-] - lifts the pen and places it; ends any open outline
    Segment    = 1u,   // [-] - straight to (X, Y)
    Curve      = 2u,   // [-] - cubic to (X, Y) via the two declared controls
    Close      = 3u,   // [-] - joins back to the last Origin and ends the outline
    Disc       = 4u,   // [-] - a circle centred at (X, Y) of radius FirstX
    Enclosure  = 5u    // [-] - a rounded rectangle, (X, Y) to (First…), corner radius SecondX
};

/// 🧩 One step, in the 24-unit declared square Lucide draws in.
/// tag   guarantee, nonallocating, nonthrowing
struct StrokeStep
{
    StrokeCommand  Command      = StrokeCommand::Origin;   // [-] - what this step does
    float          X        = 0.0f;                    // [-] - primary abscissa, 0 … 24
    float          Y       = 0.0f;                    // [-] - primary coordinate, 0 … 24, increasing down
    float          FirstX   = 0.0f;                    // [-] - control one, or radius, or trailing corner
    float          FirstY  = 0.0f;                    // [-] - control one coordinate, or trailing corner
    float          SecondX  = 0.0f;                    // [-] - control two, or corner radius
    float          SecondY = 0.0f;                    // [-] - control two coordinate
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE FIGURE
//------------------------------------------------------------------------------------------------------------------------

// 📐 Lucide draws in a 24 × 24 square at a stroke weight of two, with round caps and round joins. Both
//    numbers travel with the figure rather than sitting at the recording site, because the two tongue symbols
//    are drawn at 2.5 and everything else at 2 — a weight chosen where the figure is drawn is a weight that
//    disagrees with itself across two panels.
inline constexpr float DeclaredSquare      = 24.0f;   // [-] - the square every coordinate above is stated in
inline constexpr float DeclaredWeight      = 2.0f;    // [-] - lucide's default
inline constexpr float TongueWeight        = 2.5f;    // [-] - the two drawer tongues, strokeWidth={2.5}

// 📐 The circle-to-cubic constant, κ = 4(√2 − 1)/3. Every quarter arc in the declared figures below is
//    expressed with it, which is what every vector rasteriser does and is exact to about one part in 10⁴ of
//    the radius — far under a pixel at the 16 px and 20 px extents these are drawn at.
inline constexpr float QuarterArcControl   = 0.5522847498f;   // [-] - κ

/// 🧩 One declared figure — its stroke stream, its registration, and the weight it is drawn at.
/// note  Points into static storage. Nothing here ever owns an allocation, and a figure outlives every
///       reference to it by construction.
/// tag   guarantee, nonallocating, nonthrowing
struct SymbolFigure
{
    const StrokeStep*  Steps       = nullptr;                        // [-] - static; never allocated
    std::uint32_t      StepCount   = 0u;                             // [-]
    SymbolDiscipline   Registration   = SymbolDiscipline::Workspace;    // [-]
    float              Weight      = DeclaredWeight;                 // [-] - in declared-square units
    bool               ArtworkHeld = false;                          // [-] - false while it draws as the mark
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE LOOKUPS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The figure one subject draws as.
/// in    Subject  [-]  a subject with no declared artwork resolves to PlaceholderMark's figure
/// out   Figure   [-]  never empty; a caller never has to test before stroking
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const SymbolFigure& Figure(SymbolSubject Subject);

/// 🧩 Which discipline a subject is registered in, without resolving its artwork.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
SymbolDiscipline Registration(SymbolSubject Subject);

/// 🧩 The subjects registered in one discipline, in declared order.
/// in    Discipline  [-]  the discipline to read
/// in    Delivered   [-]  receives a pointer into static storage; untouched when the count is zero
/// out   Count       [-]  how many subjects the discipline holds
/// use   `const SymbolSubject* Held = nullptr; const auto Count = RegisteredIn(Texturing, &Held);`
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
std::uint32_t RegisteredIn(SymbolDiscipline Discipline, const SymbolSubject** Delivered);

/// 🧩 Static text naming a discipline, for the diagnostic overlay and for nothing the artist reads.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const char* DisciplineText(SymbolDiscipline Discipline);

}   // namespace Slate

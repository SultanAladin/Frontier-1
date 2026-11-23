//============================================================================================================================================
// 🧩 Frontier/DisplayPresentation/VectorCodec.cpp — Categorized Scalable Vector Graphic (SVG) Path Decoders Implementation
//============================================================================================================================================

#include "VectorCodec.h"

namespace Frontier {

//------------------------------------------------------------------------------------------------------------------------
//                                           NAVIGATION GLYPH LOOKUP TABLE
//------------------------------------------------------------------------------------------------------------------------

const std::array<VectorGlyphRecord, static_cast<size_t>(NavigationIconCategory::Count)> VectorCodec::NavigationGlyphTable = {
    // 0: ArrowUp
    VectorGlyphRecord{
        "ArrowUp",
        "M12 19V5M5 12l7-7 7 7",
        24, 24, 2.0f
    },
    // 1: ArrowDown
    VectorGlyphRecord{
        "ArrowDown",
        "M12 5v14M19 12l-7 7-7-7",
        24, 24, 2.0f
    },
    // 2: ArrowLeft
    VectorGlyphRecord{
        "ArrowLeft",
        "M19 12H5M12 19l-7-7 7-7",
        24, 24, 2.0f
    },
    // 3: ArrowRight
    VectorGlyphRecord{
        "ArrowRight",
        "M5 12h14M12 5l7 7-7 7",
        24, 24, 2.0f
    },
    // 4: ChevronUp
    VectorGlyphRecord{
        "ChevronUp",
        "M18 15l-6-6-6 6",
        24, 24, 2.0f
    },
    // 5: ChevronDown
    VectorGlyphRecord{
        "ChevronDown",
        "M6 9l6 6 6-6",
        24, 24, 2.0f
    },
    // 6: ChevronLeft
    VectorGlyphRecord{
        "ChevronLeft",
        "M15 18l-6-6 6-6",
        24, 24, 2.0f
    },
    // 7: ChevronRight
    VectorGlyphRecord{
        "ChevronRight",
        "M9 18l6-6-6-6",
        24, 24, 2.0f
    },
    // 8: ChevronsUp
    VectorGlyphRecord{
        "ChevronsUp",
        "M17 11l-5-5-5 5M17 18l-5-5-5 5",
        24, 24, 2.0f
    },
    // 9: ChevronsDown
    VectorGlyphRecord{
        "ChevronsDown",
        "M7 13l5 5 5-5M7 6l5 5 5-5",
        24, 24, 2.0f
    },
    // 10: ChevronsLeft
    VectorGlyphRecord{
        "ChevronsLeft",
        "M11 17l-5-5 5-5M18 17l-5-5 5-5",
        24, 24, 2.0f
    },
    // 11: ChevronsRight
    VectorGlyphRecord{
        "ChevronsRight",
        "M13 17l5-5-5-5M6 17l5-5-5-5",
        24, 24, 2.0f
    },
    // 12: CornerDownRight
    VectorGlyphRecord{
        "CornerDownRight",
        "M15 10l5 5-5 5M4 4v7a4 4 0 0 0 4 4h12",
        24, 24, 2.0f
    },
    // 13: CornerDownLeft
    VectorGlyphRecord{
        "CornerDownLeft",
        "M9 10l-5 5 5 5M20 4v7a4 4 0 0 1-4 4H4",
        24, 24, 2.0f
    },
    // 14: RotateClockwise
    VectorGlyphRecord{
        "RotateClockwise",
        "M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8M21 3v5h-5",
        24, 24, 2.0f
    },
    // 15: RotateCounterClockwise
    VectorGlyphRecord{
        "RotateCounterClockwise",
        "M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8M3 3v5h5",
        24, 24, 2.0f
    },
    // 16: ExpandDiagonal
    VectorGlyphRecord{
        "ExpandDiagonal",
        "M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7",
        24, 24, 2.0f
    },
    // 17: CollapseDiagonal
    VectorGlyphRecord{
        "CollapseDiagonal",
        "M4 14h6v6M20 10h-6V4M14 10l7-7M3 21l7-7",
        24, 24, 2.0f
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                         CONTROL CENTRE GLYPH LOOKUP TABLE
//------------------------------------------------------------------------------------------------------------------------

const std::array<VectorGlyphRecord, static_cast<size_t>(ControlCentreIconCategory::Count)> VectorCodec::ControlCentreGlyphTable = {
    // 0: SettingsGear
    VectorGlyphRecord{
        "SettingsGear",
        "M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915M15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0z",
        24, 24, 2.0f
    },
    // 1: AppearancePalette
    VectorGlyphRecord{
        "AppearancePalette",
        "M12 22a1 1 0 0 1 0-20 10 9 0 0 1 10 9 5 5 0 0 1-5 5h-2.25a1.75 1.75 0 0 0-1.4 2.8l.3.4a1.75 1.75 0 0 1-1.4 2.8zM14 6.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0zM18 10.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0zM7 12.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0zM9 7.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0z",
        24, 24, 2.0f
    },
    // 2: DisplayMonitor
    VectorGlyphRecord{
        "DisplayMonitor",
        "M4 3h16a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM8 21h8M12 17v4",
        24, 24, 2.0f
    },
    // 3: InputDevices
    VectorGlyphRecord{
        "InputDevices",
        "M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z",
        24, 24, 2.0f
    },
    // 4: NotificationsBell
    VectorGlyphRecord{
        "NotificationsBell",
        "M10.268 21a2 2 0 0 0 3.464 0M3.262 15.326A1 1 0 0 0 4 17h16a1 1 0 0 0 .74-1.673C19.41 13.956 18 12.499 18 8A6 6 0 0 0 6 8c0 4.499-1.411 5.956-2.738 7.326",
        24, 24, 2.0f
    },
    // 5: WirelessSignal
    VectorGlyphRecord{
        "WirelessSignal",
        "M12 20h.01M2 8.82a15 15 0 0 1 20 0M5 12.859a10 10 0 0 1 14 0M8.5 16.429a5 5 0 0 1 7 0",
        24, 24, 2.0f
    },
    // 6: BluetoothSymbol
    VectorGlyphRecord{
        "BluetoothSymbol",
        "M7 7l10 10-5 5V2l5 5L7 17",
        24, 24, 2.0f
    },
    // 7: MoonDisturbance
    VectorGlyphRecord{
        "MoonDisturbance",
        "M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z",
        24, 24, 2.0f
    },
    // 8: VolumeSpeaker
    VectorGlyphRecord{
        "VolumeSpeaker",
        "M11 5L6 9H2v6h4l5 4V5zm8.07-.07a10 10 0 0 1 0 14.14M15.54 8.46a5 5 0 0 1 0 7.07",
        24, 24, 2.0f
    },
    // 9: SunIllumination — lucide "sun": circle r4 + 8 rays
    VectorGlyphRecord{
        "SunIllumination",
        "M16 12a4 4 0 1 1-8 0 4 4 0 0 1 8 0zM12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41",
        24, 24, 2.0f
    },
    // 10: SparklesAntiAliasing — lucide "sparkles"
    VectorGlyphRecord{
        "SparklesAntiAliasing",
        "M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 1.051a1 1 0 0 1 0 1.966l-5.558 1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1-1.966 0l-1.051-5.558a2 2 0 0 0-1.594-1.594l-5.558-1.051a1 1 0 0 1 0-1.966l5.558-1.051a2 2 0 0 0 1.594-1.594zM20 2v4M22 4h-4M6 20a2 2 0 1 1-4 0 2 2 0 0 1 4 0z",
        24, 24, 2.0f
    },
    // 11: GaugeFrameRate — lucide "gauge"
    VectorGlyphRecord{
        "GaugeFrameRate",
        "m12 14 4-4M3.34 19a10 10 0 1 1 17.32 0",
        24, 24, 2.0f
    },
    // 12: SlidersQuality — lucide "sliders-horizontal"
    VectorGlyphRecord{
        "SlidersQuality",
        "M10 5H3M12 19H3M14 3v4M16 17v4M21 12h-9M21 19h-5M21 5h-7M8 10v4M8 12H3",
        24, 24, 2.0f
    },
    // 13: VideoRenderScale — lucide "video": lens path + rounded rect x2 y6 w14 h12 rx2
    VectorGlyphRecord{
        "VideoRenderScale",
        "m16 13 5.223 3.482a.5.5 0 0 0 .777-.416V7.87a.5.5 0 0 0-.752-.432L16 10.5M4 6h10a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2z",
        24, 24, 2.0f
    },
    // 14: CloseCross — lucide "x"
    VectorGlyphRecord{ "CloseCross", "M18 6 6 18M6 6l12 12", 24, 24, 2.0f },
    // 15: ChevronBack — lucide "chevron-left"
    VectorGlyphRecord{ "ChevronBack", "m15 18-6-6 6-6", 24, 24, 2.0f },
    // 16: ChevronForward — lucide "chevron-right"
    VectorGlyphRecord{ "ChevronForward", "m9 18 6-6-6-6", 24, 24, 2.0f },
    // 17: ShieldInput — lucide "shield"
    VectorGlyphRecord{
        "ShieldInput",
        "M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z",
        24, 24, 2.0f
    },
    // 18: ChevronDown — lucide "chevron-down"
    VectorGlyphRecord{ "ChevronDown", "m6 9 6 6 6-6", 24, 24, 2.0f },
    // 19: ChevronUp — lucide "chevron-up"
    VectorGlyphRecord{ "ChevronUp", "m18 15-6-6-6 6", 24, 24, 2.0f },
    // 20: TriangleAlert — lucide "triangle-alert"
    VectorGlyphRecord{ "TriangleAlert", "m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3ZM12 9v4M12 17h.01", 24, 24, 2.0f },
    // 21: CircleCheck — lucide "circle-check-big"
    VectorGlyphRecord{ "CircleCheck", "M22 11.08V12a10 10 0 1 1-5.93-9.14M9 11l3 3L22 4", 24, 24, 2.0f },
    // 22: CircleInfo — lucide "info"
    VectorGlyphRecord{ "CircleInfo", "M12 2a10 10 0 1 1 0 20 10 10 0 0 1 0-20zM12 16v-4M12 8h.01", 24, 24, 2.0f },
    // 23: OctagonAlert — lucide "octagon-alert"
    VectorGlyphRecord{ "OctagonAlert", "M12 16h.01M12 8v4M15.312 2a2 2 0 0 1 1.414.586l4.688 4.688A2 2 0 0 1 22 8.688v6.624a2 2 0 0 1-.586 1.414l-4.688 4.688a2 2 0 0 1-1.414.586H8.688a2 2 0 0 1-1.414-.586l-4.688-4.688A2 2 0 0 1 2 15.312V8.688a2 2 0 0 1 .586-1.414l4.688-4.688A2 2 0 0 1 8.688 2z", 24, 24, 2.0f },

    // ── Outliner row columns and tree furniture ──────────────────────────────────────────────────────────────────
    // <circle cx cy r> is rewritten as two arcs, matching how the existing entries encode their circles: the
    //    decoder handles one path grammar, so every primitive has to arrive as path data.

    // 24: EyeVisible — lucide "eye". Lid outline plus a 3-radius pupil at (12,12).
    VectorGlyphRecord{ "EyeVisible", "M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0M15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0z", 24, 24, 2.0f },
    // 25: EyeHidden — lucide "eye-off". Two lid arcs broken by the strike, plus the strike itself.
    VectorGlyphRecord{ "EyeHidden", "M10.733 5.076a10.744 10.744 0 0 1 11.205 6.575 1 1 0 0 1 0 .696 10.747 10.747 0 0 1-1.444 2.49M14.084 14.158a3 3 0 0 1-4.242-4.242M17.479 17.499a10.75 10.75 0 0 1-15.417-5.151 1 1 0 0 1 0-.696 10.75 10.75 0 0 1 4.446-5.143M2 2l20 20", 24, 24, 2.0f },
    // 26: LockClosed — lucide "lock". Body rect as an explicit rounded path, shackle above.
    VectorGlyphRecord{ "LockClosed", "M5 13a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2zM7 11V7a5 5 0 0 1 10 0v4", 24, 24, 2.0f },
    // 27: LockOpen — lucide "lock-open". Same body; the shackle is open on the trailing side.
    VectorGlyphRecord{ "LockOpen", "M5 13a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2zM7 11V7a5 5 0 0 1 9.9-1", 24, 24, 2.0f },
    // 28: MotionActivity — lucide "activity". The dynamic column: a pulse reads as motion at 13 px far better
    //     than a gauge does.
    VectorGlyphRecord{ "MotionActivity", "M22 12h-2.48a2 2 0 0 0-1.93 1.46l-2.35 8.36a.25.25 0 0 1-.48 0L9.24 2.18a.25.25 0 0 0-.48 0l-2.35 8.36A2 2 0 0 1 4.49 12H2", 24, 24, 2.0f },
    // 29: FolderClosed — lucide "folder".
    VectorGlyphRecord{ "FolderClosed", "M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2z", 24, 24, 2.0f },
    // 30: FolderOpen — lucide "folder-open".
    VectorGlyphRecord{ "FolderOpen", "M2 7.5V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H20a2 2 0 0 1 2 2v1.5M2.239 18.578A2 2 0 0 0 4 20h16a2 2 0 0 0 1.964-1.618l1.402-7A1 1 0 0 0 22.386 10H1.614a1 1 0 0 0-.98 1.382z", 24, 24, 2.0f },
    // 31: CubeObject — lucide "box". The mesh row.
    VectorGlyphRecord{ "CubeObject", "M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16zM3.3 7l8.7 5 8.7-5M12 22V12", 24, 24, 2.0f },
    // 32: SearchGlass — lucide "search". Lens as two arcs, then the handle.
    VectorGlyphRecord{ "SearchGlass", "M18 11a7 7 0 1 1-14 0 7 7 0 0 1 14 0zM21 21l-4.35-4.35", 24, 24, 2.0f },
    // 33: PlusAdd — lucide "plus".
    VectorGlyphRecord{ "PlusAdd", "M5 12h14M12 5v14", 24, 24, 2.0f },
    // 34: TrashDelete — lucide "trash-2".
    VectorGlyphRecord{ "TrashDelete", "M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6M10 11v6M14 11v6", 24, 24, 2.0f },
    // 35: LayoutSplit — lucide "columns-2". Split layout mode.
    VectorGlyphRecord{ "LayoutSplit", "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM12 3v18", 24, 24, 2.0f },
    // 36: LayoutPanelLeft — lucide "panel-left". Outliner-only mode.
    VectorGlyphRecord{ "LayoutPanelLeft", "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM9 3v18", 24, 24, 2.0f },
    // 37: LayoutPanelRight — lucide "panel-right". Properties-only mode.
    VectorGlyphRecord{ "LayoutPanelRight", "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM15 3v18", 24, 24, 2.0f },
    // 38: CameraBody — lucide "video". The camera row; VideoRenderScale is already spoken for by the dashboard.
    VectorGlyphRecord{ "CameraBody", "M16 8a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2zM16 10l5.24-3.14a.5.5 0 0 1 .76.43v9.42a.5.5 0 0 1-.76.43L16 14", 24, 24, 2.0f },
    // 39: LayersSlabs — lucide "layers" (verbatim: top diamond + two chevron layers). The Materials hub row.
    VectorGlyphRecord{ "LayersSlabs", "M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83zM2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17", 24, 24, 2.0f }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    VECTOR ACCESSORS
//------------------------------------------------------------------------------------------------------------------------

const VectorGlyphRecord& VectorCodec::QueryNavigationIcon(NavigationIconCategory Icon) noexcept
{
    size_t Index = static_cast<size_t>(Icon);
    if (Index < NavigationGlyphTable.size())
    {
        return NavigationGlyphTable[Index];
    }
    return NavigationGlyphTable[0];
}

std::string_view VectorCodec::QueryNavigationSvgPath(NavigationIconCategory Icon) noexcept
{
    return QueryNavigationIcon(Icon).SvgPathString;
}

uint32_t VectorCodec::QueryNavigationIconCount() noexcept
{
    return static_cast<uint32_t>(NavigationGlyphTable.size());
}

const VectorGlyphRecord& VectorCodec::QueryControlCentreIcon(ControlCentreIconCategory Icon) noexcept
{
    size_t Index = static_cast<size_t>(Icon);
    if (Index < ControlCentreGlyphTable.size())
    {
        return ControlCentreGlyphTable[Index];
    }
    return ControlCentreGlyphTable[0];
}

std::string_view VectorCodec::QueryControlCentreSvgPath(ControlCentreIconCategory Icon) noexcept
{
    return QueryControlCentreIcon(Icon).SvgPathString;
}

uint32_t VectorCodec::QueryControlCentreIconCount() noexcept
{
    return static_cast<uint32_t>(ControlCentreGlyphTable.size());
}


//------------------------------------------------------------------------------------------------------------------------
//                                              OUTLINER GLYPH TABLE
//------------------------------------------------------------------------------------------------------------------------
// The celestial page's icon function, child for child. Circles and rects are rewritten as arc paths; opacity and
//    dash figures ride beside each child so the panel dims and dashes exactly what the page does.

const std::array<OutlinerGlyphRecord, static_cast<size_t>(OutlinerIconCategory::Count)> VectorCodec::OutlinerGlyphTable = {{
    { "globe", {
        { "M3 12a9 9 0 1 0 18 0a9 9 0 1 0 -18 0", 1.0f, 0.0f, 0.0f, false },
        { "M3 12h18", 1.0f, 0.0f, 0.0f, false },
        { "M12 3a14.5 14.5 0 0 1 0 18M12 3a14.5 14.5 0 0 0 0 18", 1.0f, 0.0f, 0.0f, false },
        { "M4.6 7.5h14.8M4.6 16.5h14.8", 0.5f, 0.0f, 0.0f, false },
    }, 4u, 1.6f },
    { "cloud", {
        { "M7 18.5h10.5a3.75 3.75 0 0 0 .6-7.45A5.5 5.5 0 0 0 7.6 9.3 4.6 4.6 0 0 0 7 18.5z", 1.0f, 0.0f, 0.0f, false },
        { "M13.5 6.2a3.2 3.2 0 0 1 5.3 1.9", 0.55f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "vclouds", {
        { "M4.5 17.5h11a3.4 3.4 0 0 0 .5-6.77A5 5 0 0 0 6.3 9.6a4 4 0 0 0-1.8 7.9z", 1.0f, 0.0f, 0.0f, false },
        { "M17.2 10.6a3 3 0 0 1 3.3 4.7", 0.6f, 0.0f, 0.0f, false },
        { "M2.5 21h19", 0.35f, 0.0f, 0.0f, false },
        { "M15 17.5h4.5a2.4 2.4 0 0 0 .4-4.76", 0.6f, 0.0f, 0.0f, false },
    }, 4u, 1.6f },
    { "lcloud", {
        { "M8 16.5h8.5a3.2 3.2 0 0 0 .5-6.37A4.6 4.6 0 0 0 8.2 8.9 3.9 3.9 0 0 0 8 16.5z", 1.0f, 0.0f, 0.0f, false },
        { "M6 3h12a3 3 0 0 1 3 3v12a3 3 0 0 1 -3 3h-12a3 3 0 0 1 -3 -3v-12a3 3 0 0 1 3 -3z", 0.45f, 2.5f, 3.0f, false },
    }, 2u, 1.6f },
    { "rainbow", {
        { "M3 18a9 9 0 0 1 18 0", 1.0f, 0.0f, 0.0f, false },
        { "M6.5 18a5.5 5.5 0 0 1 11 0", 0.6f, 0.0f, 0.0f, false },
        { "M10 18a2 2 0 0 1 4 0", 0.4f, 0.0f, 0.0f, false },
    }, 3u, 1.6f },
    { "wind", {
        { "M3 8h11a3 3 0 1 0-3-3", 1.0f, 0.0f, 0.0f, false },
        { "M3 12h15a3 3 0 1 1-3 3", 1.0f, 0.0f, 0.0f, false },
        { "M3 16h7a2 2 0 1 1-2 2", 0.6f, 0.0f, 0.0f, false },
    }, 3u, 1.6f },
    { "rain", {
        { "M7 13.5h9.5a3.3 3.3 0 0 0 .5-6.56A4.8 4.8 0 0 0 7.8 6.3 3.6 3.6 0 0 0 7 13.5z", 1.0f, 0.0f, 0.0f, false },
        { "M8 17l-1 3M12 17l-1 3M16 17l-1 3", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "afog", {
        { "M3 17h18", 1.0f, 0.0f, 0.0f, false },
        { "M3 13c3-2 6-2 9 0s6 2 9 0", 1.0f, 0.0f, 0.0f, false },
        { "M6 8c2-1.5 4-1.5 6 0s4 1.5 6 0", 0.55f, 0.0f, 0.0f, false },
        { "M16 5a2 2 0 1 0 4 0a2 2 0 1 0 -4 0", 1.0f, 0.0f, 0.0f, false },
    }, 4u, 1.6f },
    { "vfog", {
        { "M4 8 12 4l8 4-8 4z", 1.0f, 0.0f, 0.0f, false },
        { "M4 8v8l8 4 8-4V8", 1.0f, 0.0f, 0.0f, false },
        { "M12 12v8", 1.0f, 0.0f, 0.0f, false },
        { "M7.5 15.5c1.5-1 3-1 4.5 0s3 1 4.5 0", 0.6f, 0.0f, 0.0f, false },
    }, 4u, 1.6f },
    { "folder", {
        { "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "fog", {
        { "M4 14h13M6 18h11M8 10h9", 1.0f, 0.0f, 0.0f, false },
        { "M7 10a5 5 0 0 1 9.6-1.8A3.5 3.5 0 0 1 17 15", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "atmo", {
        { "M4 12a8 8 0 1 0 16 0a8 8 0 1 0 -16 0", 1.0f, 0.0f, 0.0f, false },
        { "M4 12h16M12 4c3 3 3 13 0 16M12 4c-3 3-3 13 0 16", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "sun", {
        { "M8 12a4 4 0 1 0 8 0a4 4 0 1 0 -8 0", 1.0f, 0.0f, 0.0f, false },
        { "M12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "sky", {
        { "M3 16a4 4 0 0 1 4-4 6 6 0 0 1 11.5 1.5A3.5 3.5 0 0 1 18 20H7a4 4 0 0 1-4-4z", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "check", {
        { "M5 12.5l4.5 4.5L19 7.5", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "dot", {
        { "M8 12a4 4 0 1 0 8 0a4 4 0 1 0 -8 0", 1.0f, 0.0f, 0.0f, true },
    }, 1u, 1.6f },
    { "warn", {
        { "M12 4l9 16H3z", 1.0f, 0.0f, 0.0f, false },
        { "M12 10v4M12 17.5v.5", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "shadow", {
        { "M4 9a5 5 0 1 0 10 0a5 5 0 1 0 -10 0", 1.0f, 0.0f, 0.0f, false },
        { "M14 12a6 6 0 1 1-6 6", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "gi", {
        { "M4 18l6-8 4 5 3-3 3 6z", 1.0f, 0.0f, 0.0f, false },
        { "M15 6a2 2 0 1 0 4 0a2 2 0 1 0 -4 0", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "collide", {
        { "M4.5 8h4a1.5 1.5 0 0 1 1.5 1.5v5a1.5 1.5 0 0 1 -1.5 1.5h-4a1.5 1.5 0 0 1 -1.5 -1.5v-5a1.5 1.5 0 0 1 1.5 -1.5z", 1.0f, 0.0f, 0.0f, false },
        { "M15.5 8h4a1.5 1.5 0 0 1 1.5 1.5v5a1.5 1.5 0 0 1 -1.5 1.5h-4a1.5 1.5 0 0 1 -1.5 -1.5v-5a1.5 1.5 0 0 1 1.5 -1.5z", 1.0f, 0.0f, 0.0f, false },
        { "M10 12h4", 1.0f, 0.0f, 0.0f, false },
    }, 3u, 1.6f },
    { "stars", {
        { "M12 3l1.8 4.6L18 9l-4.2 1.4L12 15l-1.8-4.6L6 9l4.2-1.4z", 1.0f, 0.0f, 0.0f, false },
        { "M19 15l.7 1.8 1.8.7-1.8.7L19 20l-.7-1.8-1.8-.7 1.8-.7zM5 16l.5 1.3 1.3.5-1.3.5L5 19.6l-.5-1.3-1.3-.5 1.3-.5z", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "moon", {
        { "M20 14.5A8 8 0 0 1 9.5 4a8 8 0 1 0 10.5 10.5z", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "camera", {
        { "M5.5 7h8a2.5 2.5 0 0 1 2.5 2.5v6a2.5 2.5 0 0 1 -2.5 2.5h-8a2.5 2.5 0 0 1 -2.5 -2.5v-6a2.5 2.5 0 0 1 2.5 -2.5z", 1.0f, 0.0f, 0.0f, false },
        { "m16 11 5-3v8l-5-3z", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "fx", {
        { "M4 20 15 9M14 4l1 2 2 1-2 1-1 2-1-2-2-1 2-1zM19 12l.6 1.4L21 14l-1.4.6L19 16l-.6-1.4L17 14l1.4-.6z", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "eye", {
        { "M2 12s3.5-6 10-6 10 6 10 6-3.5 6-10 6S2 12 2 12z", 1.0f, 0.0f, 0.0f, false },
        { "M9 12a3 3 0 1 0 6 0a3 3 0 1 0 -6 0", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "eyeoff", {
        { "M3 3l18 18M10.6 10.6a3 3 0 0 0 4.2 4.2M9.9 5.1A10.5 10.5 0 0 1 12 5c6.5 0 10 7 10 7a17 17 0 0 1-3.2 4M6.2 6.2A17 17 0 0 0 2 12s3.5 7 10 7a10 10 0 0 0 4-.8", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "chev", {
        { "m9 6 6 6-6 6", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "chevu", {
        { "m18 15-6-6-6 6", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "wave", {
        { "M2 12c2-4 4-4 6 0s4 4 6 0 4-4 6 0", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "horizon", {
        { "M2 15h20M6 15a6 6 0 0 1 12 0", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "orbit", {
        { "M9 12a3 3 0 1 0 6 0a3 3 0 1 0 -6 0", 1.0f, 0.0f, 0.0f, false },
        { "M4 12a8 8 0 1 0 16 0", 1.0f, 0.0f, 0.0f, false },
        { "M20 12a8 8 0 0 0-16 0", 1.0f, 2.0f, 3.0f, false },
    }, 3u, 1.6f },
    { "bulb", {
        { "M9 18h6M10 21h4M12 3a6 6 0 0 0-4 10.5c.7.7 1 1.5 1 2.5h6c0-1 .3-1.8 1-2.5A6 6 0 0 0 12 3z", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "palette", {
        { "M3 12a9 9 0 1 0 18 0a9 9 0 1 0 -18 0", 1.0f, 0.0f, 0.0f, false },
        { "M7.5 10a1 1 0 1 0 2 0a1 1 0 1 0 -2 0", 1.0f, 0.0f, 0.0f, false },
        { "M11 7.5a1 1 0 1 0 2 0a1 1 0 1 0 -2 0", 1.0f, 0.0f, 0.0f, false },
        { "M14.5 10a1 1 0 1 0 2 0a1 1 0 1 0 -2 0", 1.0f, 0.0f, 0.0f, false },
        { "M12 21a3 3 0 0 0 0-6h-1a2 2 0 0 1 0-4", 1.0f, 0.0f, 0.0f, false },
    }, 5u, 1.6f },
    { "ground", {
        { "M2 18h20M4 18c3-6 6-6 8 0M12 18c2-4 5-4 8 0", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "grid", {
        { "M3 9h18M3 15h18M9 3v18M15 3v18", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "galaxy", {
        { "M12 12a4 4 0 0 1 6 2 7 7 0 0 1-12 3M12 12a4 4 0 0 0-6-2 7 7 0 0 1 12-3", 1.0f, 0.0f, 0.0f, false },
        { "M11 12a1 1 0 1 0 2 0a1 1 0 1 0 -2 0", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "aperture", {
        { "M3 12a9 9 0 1 0 18 0a9 9 0 1 0 -18 0", 1.0f, 0.0f, 0.0f, false },
        { "M14.3 15.5 8.5 5.4M9.7 8.5h11.6M12 12l-5.8 10M9.7 15.5h11.6M14.3 8.5l-5.8 10", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "sliders", {
        { "M4 6h10M18 6h2M4 12h4M12 12h8M4 18h12", 1.0f, 0.0f, 0.0f, false },
        { "M14 6a2 2 0 1 0 4 0a2 2 0 1 0 -4 0", 1.0f, 0.0f, 0.0f, false },
        { "M8 12a2 2 0 1 0 4 0a2 2 0 1 0 -4 0", 1.0f, 0.0f, 0.0f, false },
        { "M16 18a2 2 0 1 0 4 0a2 2 0 1 0 -4 0", 1.0f, 0.0f, 0.0f, false },
    }, 4u, 1.6f },
    { "flare", {
        { "M9 12a3 3 0 1 0 6 0a3 3 0 1 0 -6 0", 1.0f, 0.0f, 0.0f, false },
        { "M12 2v4M12 18v4M2 12h4M18 12h4", 1.0f, 0.0f, 0.0f, false },
        { "M4 12a8 8 0 1 0 16 0a8 8 0 1 0 -16 0", 1.0f, 2.0f, 4.0f, false },
    }, 3u, 1.6f },
    { "up", {
        { "M7 17 17 7M9 7h8v8", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "down", {
        { "M7 7l10 10M17 9v8H9", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "flat", {
        { "M5 12h14M15 8l4 4-4 4", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "plus", {
        { "M12 5v14M5 12h14", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "plane", {
        { "M3 15l9-5 9 5-9 5z", 1.0f, 0.0f, 0.0f, false },
        { "M7.5 12.5l4.5 2.5 4.5-2.5", 1.0f, 0.0f, 0.0f, false },
        { "M12 10v10", 1.0f, 2.0f, 2.0f, false },
    }, 3u, 1.6f },
    { "key", {
        { "M6 6h12a3 3 0 0 1 3 3v6a3 3 0 0 1 -3 3h-12a3 3 0 0 1 -3 -3v-6a3 3 0 0 1 3 -3z", 1.0f, 0.0f, 0.0f, false },
        { "M7 12h.01M11 12h.01M15 12h.01M8 15h8", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "minus", {
        { "M5 12h14", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "trash", {
        { "M4 7h16M10 11v6M14 11v6M6 7l1 12h10l1-12M9 7V4h6v3", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.6f },
    { "search", {
        { "M4 11a7 7 0 1 0 14 0a7 7 0 1 0 -14 0", 1.0f, 0.0f, 0.0f, false },
        { "m20 20-3.5-3.5", 1.0f, 0.0f, 0.0f, false },
    }, 2u, 1.6f },
    { "compact", {
        { "M4 8h16M4 16h16", 1.0f, 0.0f, 0.0f, false },
    }, 1u, 1.8f },
}};

const OutlinerGlyphRecord& VectorCodec::QueryOutlinerIcon(OutlinerIconCategory Icon) noexcept
{
    const uint32_t Index = static_cast<uint32_t>(Icon);
    return OutlinerGlyphTable[Index < OutlinerGlyphTable.size() ? Index : 0u];
}

uint32_t VectorCodec::QueryOutlinerIconCount() noexcept
{
    return static_cast<uint32_t>(OutlinerGlyphTable.size());
}

} // namespace Frontier

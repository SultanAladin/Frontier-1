/*==========================================================================================================================================
                                                          STUDIOCONTROLS.JS
==========================================================================================================================================*/
// 🧩 Sun Inspector + Fluid Editor unified control kit for Frontier Particle Studio:
//    glyphs, census stats, Fluid SliderPill (numeric drag-scrubbing + 26px spherical track), high-contrast switches,
//    Sun quick-control feature tiles, KPI highlight trios, TimeSeries glow charts, Emitter/Compass radial diagrams & CurvePlot.

const SvgNamespace = 'http://www.w3.org/2000/svg';
const Refreshers = new Set();
const ScopeStack = [];

export const GlyphPaths = {
    Monitor:    '<rect x="2.5" y="4" width="19" height="13" rx="2.5"/><path d="M8 21h8M12 17v4"/>',
    Zap:        '<path d="M13 2.5 4.5 14h6.5l-1 7.5L18.5 10H12z"/>',
    Sparkles:   '<path d="M11 3.5l1.7 4.6 4.6 1.7-4.6 1.7L11 16.1l-1.7-4.6-4.6-1.7 4.6-1.7z"/><path d="M18.5 14.5v5M16 17h5"/>',
    Aperture:   '<circle cx="12" cy="12" r="9"/><path d="M14.3 3.3 9.5 11.5M20.7 9.6H11.2M18.4 18.3l-4.8-8.2M9.7 20.7l4.8-8.2M3.3 14.4h9.5M5.6 5.7l4.8 8.2"/>',
    Sun:        '<circle cx="12" cy="12" r="4"/><path d="M12 2.5v2M12 19.5v2M4.6 4.6l1.4 1.4M18 18l1.4 1.4M2.5 12h2M19.5 12h2M4.6 19.4 6 18M18 6l1.4-1.4"/>',
    Layers:     '<path d="M12 3 3 8l9 5 9-5z"/><path d="M3 12.5l9 5 9-5"/><path d="M3 17l9 5 9-5"/>',
    Scan:       '<path d="M4 8V5.5A1.5 1.5 0 0 1 5.5 4H8M16 4h2.5A1.5 1.5 0 0 1 20 5.5V8M20 16v2.5a1.5 1.5 0 0 1-1.5 1.5H16M8 20H5.5A1.5 1.5 0 0 1 4 18.5V16"/><path d="M7.5 12h9"/>',
    Gauge:      '<path d="M3.6 17.5a9 9 0 1 1 16.8 0"/><path d="M12 13.5l4.2-4.2"/><circle cx="12" cy="13.5" r="1.2"/>',
    Boxes:      '<path d="M12 2.8l8 4.4v9.6l-8 4.4-8-4.4V7.2z"/><path d="M12 12l8-4.6M12 12v9.2M12 12 4 7.4"/>',
    Lattice:    '<rect x="3" y="3" width="18" height="18" rx="2.5"/><path d="M3 9h18M3 15h18M9 3v18M15 3v18"/>',
    Bug:        '<rect x="7.5" y="7" width="9" height="13" rx="4.5"/><path d="M12 7V4.5M9.5 4l1.3 1.6M14.5 4l-1.3 1.6M7.5 12H3.5M20.5 12h-4M7.8 16.5 4.5 19M16.2 16.5l3.3 2.5M7.8 8.8 4.8 6.5M16.2 8.8l3-2.3"/>',
    Activity:   '<path d="M3 12h4l3-8 4 16 3-8h4"/>',
    Chevron:    '<path d="M6 9l6 6 6-6"/>',
    Check:      '<path d="M5 12.5l4.2 4.2L19 7"/>',
    Alert:      '<path d="M12 3.5 2.5 20h19z"/><path d="M12 10v4.5M12 17.4v.2"/>',
    Eye:        '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
    EyeOff:     '<path d="M3 3l18 18"/><path d="M10.6 5.1A10.4 10.4 0 0 1 12 5c6.4 0 10 7 10 7a17.6 17.6 0 0 1-2.6 3.5M6.2 6.3C3.6 8.1 2 12 2 12s3.6 7 10 7c1.7 0 3.2-.5 4.5-1.2"/>',
    Search:     '<circle cx="11" cy="11" r="7"/><path d="M20 20l-3.8-3.8"/>',
    Sliders:    '<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>',
    Cloud:      '<path d="M7 18.5h10a4 4 0 0 0 .6-7.95A6 6 0 0 0 6.1 9.6 4.5 4.5 0 0 0 7 18.5z"/>',
    Snowflake:  '<path d="M12 2.5v19M3.8 7.2l16.4 9.6M20.2 7.2 3.8 16.8M9.5 4.6 12 7l2.5-2.4M9.5 19.4 12 17l2.5 2.4M5 10.1l2.8-1.4-.4-3.1M19 13.9l-2.8 1.4.4 3.1M19 10.1l-2.8-1.4.4-3.1M5 13.9l2.8 1.4-.4 3.1"/>',
    CloudRain:  '<path d="M6.5 15.5h11a3.8 3.8 0 0 0 .5-7.55A5.5 5.5 0 0 0 7 6.8a4.2 4.2 0 0 0-.5 8.7z"/><path d="M8.5 18l-1.3 3.2M12.5 18l-1.3 3.2M16.5 18l-1.3 3.2"/>',
    CloudHail:  '<path d="M6.5 15h11a3.8 3.8 0 0 0 .5-7.55A5.5 5.5 0 0 0 7 6.3a4.2 4.2 0 0 0-.5 8.7z"/><circle cx="8.5" cy="18.5" r="1.25"/><circle cx="12.5" cy="20.5" r="1.25"/><circle cx="16.5" cy="18.5" r="1.25"/>',
    Planet:     '<circle cx="12" cy="12" r="5.2"/><path d="M5.3 15.9C3.1 18 2.6 19.6 3.6 20.3c1.7 1.2 7.2-1.2 12.2-5.4 4.6-3.9 6.8-7.9 5-8.9-.9-.5-2.4-.1-4.3.9"/>',
    Wind:       '<path d="M3 8.5h11a3 3 0 1 0-3-3M3 12.5h15.5a3 3 0 1 1-3 3M3 16.5h7"/>',
    Copy:       '<rect x="9" y="9" width="11.5" height="11.5" rx="2.5"/><path d="M5.5 15H5a1.5 1.5 0 0 1-1.5-1.5V5A1.5 1.5 0 0 1 5 3.5h8.5A1.5 1.5 0 0 1 15 5v.5"/>',
    Reset:      '<path d="M3.5 12a8.5 8.5 0 1 0 2.6-6.1"/><path d="M3.5 4.5v4.6h4.6"/>',
    Plus:       '<path d="M12 5v14M5 12h14"/>',
    Close:      '<path d="M6.5 6.5l11 11M17.5 6.5l-11 11"/>',
    Rays:       '<circle cx="5" cy="19" r="1.6"/><path d="M6.3 17.7 20 4M6.6 18.3l13.9-6.8M5.7 17.4l6.8-13.9"/>',
    Clock:      '<circle cx="12" cy="12" r="9"/><path d="M12 7.5V12l3 2"/>',
    Target:     '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4.5"/><circle cx="12" cy="12" r="0.6"/>',
    Folder:     '<path d="M3 7.5A2.5 2.5 0 0 1 5.5 5h3.6l2 2.2h7.4A2.5 2.5 0 0 1 21 9.7v7.8a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 17.5z"/>',
    Dot:        '<circle cx="12" cy="12" r="3"/>',
    Tornado:    '<path d="M3 5h18M5.5 9h13M8 13h8.5M10 17h5M11.5 21h2"/>',
    Attract:    '<circle cx="12" cy="12" r="2.2"/><path d="M3 12h5M16 12h5M12 3v5M12 16v5M6 10l2 2-2 2M18 10l-2 2 2 2M10 6l2 2 2-2M10 18l2-2 2 2"/>',
    Repel:      '<circle cx="12" cy="12" r="2.2"/><path d="M2.5 12h5M16.5 12h5M12 2.5v5M12 16.5v5M4.5 10l-2 2 2 2M19.5 10l2 2-2 2M10 4.5l2-2 2 2M10 19.5l2 2 2-2"/>',
    Orbit:      '<circle cx="12" cy="12" r="2.4"/><ellipse cx="12" cy="12" rx="9.5" ry="4.4" transform="rotate(-24 12 12)"/>',
    Magnet:     '<path d="M5 3.5h4.5v8a2.5 2.5 0 0 0 5 0v-8H19v8a7 7 0 0 1-14 0z"/><path d="M5 7.5h4.5M14.5 7.5H19"/>',
    Thermal:    '<path d="M8 20V8M5 11l3-3 3 3M16 20V4.5M13 7.5l3-3 3 3"/>',
    Front:      '<path d="M14.5 3c-3 3-3 15 0 18"/><path d="M3 8h7.5M3 12h8M3 16h7.5"/><path d="M18 9l2.5 3-2.5 3"/>',
    Blast:      '<circle cx="12" cy="12" r="2"/><circle cx="12" cy="12" r="5.6" stroke-dasharray="2 2.6"/><circle cx="12" cy="12" r="9.2"/>',
    Current:    '<rect x="3" y="5" width="18" height="14" rx="2.5"/><path d="M6.5 10h9.5l-2-2M6.5 14h9.5l-2 2"/>',
    Lift:       '<path d="M12 18.5V5M8 9l4-4 4 4"/><path d="M5 20.5h14"/>',
    Gravity:    '<path d="M12 3.5V17M8 13l4 4 4-4"/><path d="M5 20.5h14"/>',
    Damp:       '<path d="M2.5 9c1.6-3.6 3.2-3.6 4.8 0s3.2 2.6 4.8 0 3-1.8 4.6 0 2.4 1.2 4.8 0"/><path d="M2.5 15.5h19"/>',
    Play:       '<path d="M8 5.5v13l10.5-6.5z"/>',
    Pause:      '<path d="M8.5 5v14M15.5 5v14"/>',
    Step:       '<path d="M7 6v12l8-6z"/><path d="M17.5 6v12"/>',
    Library:    '<path d="M4 4.5h4v15H4zM10 4.5h4v15h-4z"/><path d="M15.8 5.8l3.4-.9 3.3 13.6-3.4.9z"/>',
    Trash:      '<path d="M4.5 7h15M9.5 7V4.5h5V7M6.5 7l1 13h9l1-13"/>',
    Crosshair:  '<circle cx="12" cy="12" r="7"/><path d="M12 2.5v5M12 16.5v5M2.5 12h5M16.5 12h5"/>',
    Map:        '<path d="M3 6.5l6-2.5 6 2.5 6-2.5v13.5l-6 2.5-6-2.5-6 2.5z"/><path d="M9 4v13.5M15 6.5V20"/>',
    Compass:    '<circle cx="12" cy="12" r="9"/><path d="M15.5 8.5l-2 5-5 2 2-5z"/>',
    Leaf:       '<path d="M5 19c0-8 5-14 15-14 0 10-6 15-14 15"/><path d="M5 19l8-8"/>',
    Atom:       '<circle cx="12" cy="12" r="1.5"/><ellipse cx="12" cy="12" rx="9.5" ry="3.8"/><ellipse cx="12" cy="12" rx="9.5" ry="3.8" transform="rotate(60 12 12)"/><ellipse cx="12" cy="12" rx="9.5" ry="3.8" transform="rotate(120 12 12)"/>',
    Flame:      '<path d="M12 21a6.5 6.5 0 0 0 6.5-6.5c0-4.5-4-6.5-4.5-11-3 2-5 4.5-5 7.5-1.2-.7-1.8-2-2-3.5-1.5 1.8-1.5 4.5-1.5 7A6.5 6.5 0 0 0 12 21z"/>',
    Drop:       '<path d="M12 3s6.5 7 6.5 11.5a6.5 6.5 0 0 1-13 0C5.5 10 12 3 12 3z"/>',
    Move:       '<path d="M12 3v18M3 12h18M9.5 5.5 12 3l2.5 2.5M9.5 18.5 12 21l2.5-2.5M5.5 9.5 3 12l2.5 2.5M18.5 9.5 21 12l-2.5 2.5"/>',
    Palette:    '<path d="M12 3a9 9 0 0 0 0 18c1.4 0 2-1 2-2 0-1.4-1.2-1.6-1.2-2.8 0-1 .8-1.7 1.8-1.7H17a4 4 0 0 0 4-4C21 6.6 17 3 12 3z"/><circle cx="7.5" cy="11" r="1.1"/><circle cx="10" cy="7" r="1.1"/><circle cx="14.5" cy="7" r="1.1"/>',
    Download:   '<path d="M12 3v12m0 0-4-4m4 4 4-4M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2"/>',
    Help:       '<circle cx="12" cy="12" r="9"/><path d="M9.5 9.5a2.5 2.5 0 0 1 5 .5c0 2-2.5 2-2.5 4M12 17.5h.01"/>',
    Focus:      '<path d="M4 9V5h4M16 5h4v4M20 15v4h-4M8 19H4v-4"/><circle cx="12" cy="12" r="2.2"/>',
    Maximize:   '<path d="M4 9V4h5M15 4h5v5M20 15v5h-5M9 20H4v-5"/>',
    Terminal:   '<rect x="3" y="4" width="18" height="16" rx="2.5"/><path d="M7.5 9.5 11 12l-3.5 2.5M13 14.5h3.5"/>',
    Grid:       '<rect x="3" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="3" width="7.5" height="7.5" rx="1.5"/><rect x="3" y="13.5" width="7.5" height="7.5" rx="1.5"/><rect x="13.5" y="13.5" width="7.5" height="7.5" rx="1.5"/>',
    Compact:    '<path d="M7 5v14M17 5v14"/>',
    Field:      '<circle cx="12" cy="12" r="9"/><path d="M12 3v18M3 12h18"/>',
    FlowLine:   '<path d="M3 16c4-8 8 4 12-4 2-4 4-4 6-4"/><circle cx="19" cy="8" r="1.8"/>',
    Jet:        '<path d="M4 12h15"/><path d="m14 7 5 5-5 5"/><path d="M4 7h6M4 17h6"/>',
    Downburst:  '<path d="M12 3v13"/><path d="M6 19c2-2 4-3 6-3s4 1 6 3"/><path d="m8 11 4 4 4-4"/>',
    Puff:       '<path d="M17.5 19H9a7 7 0 1 1 6.7-9h1.8a4.5 4.5 0 1 1 0 9Z"/>',
    Wake:       '<path d="M4 12h16"/><path d="M4 12c4-4 10-6 16-6"/><path d="M4 12c4 4 10 6 16 6"/>',
    Ruler:      '<path d="M4 15h16M4 15v-4m4 4v-2m4 2v-3m4 3v-2m4 2v-4"/>',
    Camera:     '<path d="M4 7.5A2.5 2.5 0 0 1 6.5 5h2l1.4-2h4.2l1.4 2h2A2.5 2.5 0 0 1 20 7.5v10a2.5 2.5 0 0 1-2.5 2.5h-11A2.5 2.5 0 0 1 4 17.5z"/><circle cx="12" cy="12.5" r="3.4"/>',
    Lock:       '<rect x="5" y="11" width="14" height="9" rx="2"/><path d="M8 11V7.5a4 4 0 0 1 8 0V11"/>',
    Heat:       '<path d="M7 19c1.5-2.5-1.5-4.5 0-7s-1.5-4.5 0-7M12 19c1.5-2.5-1.5-4.5 0-7s-1.5-4.5 0-7M17 19c1.5-2.5-1.5-4.5 0-7s-1.5-4.5 0-7"/>',
    Singularity:'<circle cx="12" cy="12" r="3.2"/><ellipse cx="12" cy="12" rx="9.5" ry="3.4" transform="rotate(-18 12 12)"/><circle cx="12" cy="12" r="6.8" stroke-dasharray="2.5 3"/>',
    Car:        '<path d="M5 15.5h14l1.2-3.3-2.6-1.2-2.4-3.5H8.8L6.4 11l-2.6 1.2z"/><circle cx="7.5" cy="16.5" r="2"/><circle cx="16.5" cy="16.5" r="2"/><path d="M9.5 16.5h5"/>',
    Ball:       '<circle cx="12" cy="12" r="8"/><path d="M5.5 8.5c4 2.5 9 2.5 13 0M12 4a11 11 0 0 1 0 16"/>',
    Cloth:      '<path d="M5 3v18"/><path d="M5 4.5c4-1.5 7 1.5 13 0v8c-6 1.5-9-1.5-13 0"/>'
};

let RichUid = 0;

export const RichGlyphs = {
    Camera: (P) => `<defs>` +
        `<linearGradient id="${P}cb" x2=".1" y2="1"><stop stop-color="#55555e"/><stop offset=".35" stop-color="#34353c"/><stop offset="1" stop-color="#202126"/></linearGradient>` +
        `<linearGradient id="${P}ce" x2="0" y2="1"><stop stop-color="#9696a3" stop-opacity=".55"/><stop offset=".5" stop-color="#62626b" stop-opacity=".15"/><stop offset="1" stop-color="#08090c" stop-opacity=".8"/></linearGradient>` +
        `<linearGradient id="${P}cr" x2=".25" y2="1"><stop stop-color="#25262e"/><stop offset=".55" stop-color="#36373f"/><stop offset="1" stop-color="#676773"/></linearGradient>` +
        `<radialGradient id="${P}cg" cx="46%" cy="38%" r="65%"><stop stop-color="#272933"/><stop offset=".45" stop-color="#101118"/><stop offset=".73" stop-color="#0b0d12"/><stop offset=".9" stop-color="#232530"/><stop offset="1" stop-color="#080a0e"/></radialGradient>` +
        `<linearGradient id="${P}cs" x2=".15" y2="1"><stop stop-color="#eff0f7" stop-opacity=".85"/><stop offset=".38" stop-color="#a0a4b6" stop-opacity=".54"/><stop offset="1" stop-color="#767b97" stop-opacity="0"/></linearGradient>` +
        `<linearGradient id="${P}cf" x2="0" y2="1"><stop stop-color="#eeeef6"/><stop offset=".55" stop-color="#8e91a2"/><stop offset="1" stop-color="#373945"/></linearGradient>` +
        `</defs>` +
        `<rect x="28" y="55" width="200" height="154" rx="23" fill="url(#${P}cb)" stroke="url(#${P}ce)" stroke-width="2.4"/>` +
        `<path d="M43 60q5-3 14-3h145q12 0 19 10" stroke="#b6b6c4" stroke-opacity=".18" stroke-width="1.4" fill="none"/>` +
        `<circle cx="128" cy="134" r="60" fill="url(#${P}cr)" stroke="#92929e" stroke-opacity=".35" stroke-width="1.4"/>` +
        `<circle cx="128" cy="134" r="44" fill="#101117" stroke="#777783" stroke-opacity=".45" stroke-width="1.6"/>` +
        `<circle cx="128" cy="134" r="40.5" fill="url(#${P}cg)" stroke="#353641" stroke-width="1.2"/>` +
        `<circle cx="128" cy="134" r="35.5" stroke="#646375" stroke-opacity=".18" stroke-width="1.2" fill="none"/>` +
        `<path d="M104 114c1-17 45-21 48-2 1 7-7 13-24 13-14 0-25-4-24-11Z" fill="url(#${P}cs)"/>` +
        `<path d="M104 162q24 15 47-3" stroke="#7c7496" stroke-opacity=".26" stroke-width="2" fill="none"/>` +
        `<circle cx="128" cy="133" r="3.2" fill="#d7dbe5" opacity=".75"/>` +
        `<rect x="184" y="71" width="22" height="17" rx="3" fill="#17181e" stroke="#767780" stroke-opacity=".4"/>` +
        `<rect x="188" y="74" width="14" height="10" rx="1.8" fill="url(#${P}cf)"/>`,

    Exposure: (P) => `<defs>` +
        `<linearGradient id="${P}er" x2=".8" y2="1"><stop stop-color="#d4dce0"/><stop offset=".45" stop-color="#778594"/><stop offset="1" stop-color="#3b485b"/></linearGradient>` +
        `<linearGradient id="${P}el" x2=".8" y2="1"><stop stop-color="#fffbed"/><stop offset="1" stop-color="#c8c9bd"/></linearGradient>` +
        `<linearGradient id="${P}ed" x2=".8" y2="1"><stop stop-color="#586372"/><stop offset="1" stop-color="#1a2638"/></linearGradient>` +
        `</defs>` +
        `<circle cx="128" cy="130" r="78" fill="url(#${P}er)"/>` +
        `<circle cx="128" cy="130" r="68" fill="url(#${P}ed)"/>` +
        `<path d="M128 62a68 68 0 0 1 0 136Z" fill="url(#${P}el)"/>` +
        `<circle cx="128" cy="130" r="68" stroke="#e4eef2" stroke-opacity=".3" fill="none" stroke-width="2"/>` +
        `<path d="M96 114v32m-16-16h32" stroke="#e5e8e5" stroke-width="7" stroke-linecap="round" fill="none"/>` +
        `<path d="M146 130h30" stroke="#4b5664" stroke-width="7" stroke-linecap="round" fill="none"/>` +
        `<path d="M68 66a88 88 0 0 1 120 0" stroke="#b8c9d8" stroke-width="3.5" fill="none" stroke-linecap="round"/>` +
        `<path d="M128 28v10m-42 3 5 9m74-9-5 9" stroke="#dce6ee" stroke-width="4.5" stroke-linecap="round" fill="none"/>`,

    HeatLens: (P) => `<defs>` +
        `<linearGradient id="${P}hr" x2=".8" y2="1"><stop stop-color="#f3c184"/><stop offset=".5" stop-color="#b86b3a"/><stop offset="1" stop-color="#4e2618"/></linearGradient>` +
        `<radialGradient id="${P}hg" cx="50%" cy="55%" r="60%"><stop stop-color="#4e2418"/><stop offset=".6" stop-color="#1b141a"/><stop offset="1" stop-color="#0b0d12"/></radialGradient>` +
        `</defs>` +
        `<circle cx="128" cy="128" r="80" fill="url(#${P}hr)"/>` +
        `<circle cx="128" cy="128" r="66" fill="url(#${P}hg)" stroke="#ffd6a8" stroke-opacity=".28" stroke-width="2"/>` +
        `<path d="M90 176c14-20-12-38 2-58s-12-38 2-58M128 176c14-20-12-38 2-58s-12-38 2-58M166 176c14-20-12-38 2-58s-12-38 2-58" stroke="#ffb36b" stroke-width="7" stroke-linecap="round" fill="none"/>`,

    BlackHoleLens: (P) => `<defs>` +
        `<linearGradient id="${P}br" x2=".8" y2="1"><stop stop-color="#d2b48c"/><stop offset=".45" stop-color="#7c6c84"/><stop offset="1" stop-color="#222536"/></linearGradient>` +
        `<radialGradient id="${P}bh" cx="50%" cy="50%" r="55%"><stop stop-color="#040507"/><stop offset=".62" stop-color="#07090d"/><stop offset=".85" stop-color="#f5b855"/><stop offset="1" stop-color="#1a1d28"/></radialGradient>` +
        `</defs>` +
        `<circle cx="128" cy="128" r="80" fill="url(#${P}br)"/>` +
        `<circle cx="128" cy="128" r="68" fill="url(#${P}bh)"/>` +
        `<ellipse cx="128" cy="128" rx="86" ry="28" transform="rotate(-20 128 128)" stroke="#f6c26b" stroke-width="5.5" fill="none" opacity=".88"/>` +
        `<circle cx="128" cy="128" r="34" fill="#040508" stroke="#ffe1a8" stroke-opacity=".65" stroke-width="2.5"/>`,

    GlitchLens: (P) => `<defs>` +
        `<linearGradient id="${P}gr" x2=".8" y2="1"><stop stop-color="#9aa4b4"/><stop offset=".5" stop-color="#454d5b"/><stop offset="1" stop-color="#191d25"/></linearGradient>` +
        `</defs>` +
        `<rect x="38" y="46" width="180" height="164" rx="22" fill="url(#${P}gr)" stroke="#a6b3c4" stroke-opacity=".35" stroke-width="2.4"/>` +
        `<rect x="54" y="62" width="148" height="132" rx="14" fill="#101319"/>` +
        `<rect x="66" y="84" width="108" height="14" rx="3" fill="#00ffd0" opacity=".85"/>` +
        `<rect x="84" y="108" width="106" height="14" rx="3" fill="#ff2d6f" opacity=".85"/>` +
        `<rect x="60" y="132" width="96" height="14" rx="3" fill="#e6edf6" opacity=".85"/>` +
        `<rect x="78" y="156" width="112" height="12" rx="3" fill="#7aa8e8" opacity=".8"/>`
};

export function ConstructGlyph(Name, ClassName = 'glyph')
{
    const Svg = document.createElementNS(SvgNamespace, 'svg');
    if (RichGlyphs[Name])
    {
        Svg.setAttribute('viewBox', '0 0 256 256');
        Svg.setAttribute('class', `${ClassName} rich-icon`);
        Svg.setAttribute('fill', 'none');
        Svg.setAttribute('aria-hidden', 'true');
        Svg.innerHTML = RichGlyphs[Name](`ri${++RichUid}_`);
        return Svg;
    }
    Svg.setAttribute('viewBox', '0 0 24 24');
    Svg.setAttribute('class', ClassName);
    Svg.innerHTML = GlyphPaths[Name] || GlyphPaths.Dot;
    return Svg;
}

export function Construct(Tag, ClassName, Content)
{
    const Element = document.createElement(Tag);
    if (ClassName) Element.className = ClassName;
    if (Content === undefined || Content === null) return Element;
    if (typeof Content === 'string' || typeof Content === 'number') Element.textContent = String(Content);
    else if (Array.isArray(Content)) Content.forEach(Part => { if (Part !== null && Part !== undefined) Element.append(Part); });
    else Element.append(Content);
    return Element;
}

export function ConstructSvg(Tag, Attributes, Text)
{
    const Element = document.createElementNS(SvgNamespace, Tag);
    for (const [Key, Setting] of Object.entries(Attributes || {})) Element.setAttribute(Key, Setting);
    if (Text !== undefined) Element.textContent = Text;
    return Element;
}

export function OpenScope() { const Scope = new Set(); ScopeStack.push(Scope); return Scope; }
export function CloseScope(Scope) { const Top = ScopeStack.pop(); if (Top !== Scope && Scope) ScopeStack.splice(ScopeStack.indexOf(Scope), 1); }
export function ReleaseScope(Scope) { if (!Scope) return; for (const R of Scope) Refreshers.delete(R); Scope.clear(); }
export function Register(Refresher) { Refreshers.add(Refresher); if (ScopeStack.length) ScopeStack[ScopeStack.length - 1].add(Refresher); }
export function Broadcast() { for (const Refresher of Refreshers) Refresher(); }

export const LinearToDisplay = Value => Value <= 0.0031308 ? 12.92 * Value : 1.055 * Math.pow(Value, 1 / 2.4) - 0.055;
export const DisplayToLinear = Value => Value <= 0.04045 ? Value / 12.92 : Math.pow((Value + 0.055) / 1.055, 2.4);

const CompassPoints = ['N', 'NNE', 'NE', 'ENE', 'E', 'ESE', 'SE', 'SSE', 'S', 'SSW', 'SW', 'WSW', 'W', 'WNW', 'NW', 'NNW'];
export const ResolveCompassPoint = Degrees => CompassPoints[Math.round((((Degrees % 360) + 360) % 360) / 22.5) % 16];

export function ConstructMetric(Element, Value, Digits, Unit)
{
    Element.textContent = '';
    const Formatted = Number(Value).toLocaleString('en-US', { minimumFractionDigits: Digits, maximumFractionDigits: Digits });
    Element.append(document.createTextNode(Formatted));
    if (Unit) Element.append(Construct('small', null, ' ' + Unit));
}

function ResolveState(Binding)
{
    return Binding.Availability ? Binding.Availability() : { Enabled: true };
}

function ApplyAvailability(Node, Binding, Extra)
{
    const State = ResolveState(Binding);
    Node.classList.toggle('unavailable', !State.Enabled);
    if (Extra) Extra(State);
    if (!State.Enabled && State.Reason) Node.title = State.Reason;
    else Node.removeAttribute('title');
    return State;
}

// 📝 Sun + Fluid Section Card (<details class="property-group" open> with 22px radius, weight-300 title & monospace section-badge)
export function SectionCard(Parent, { Id, Title, Glyph, Status, Badge, Hint, Open = true })
{
    const Details = Construct('details', 'property-group');
    if (Open) Details.open = true;
    if (Id) Details.dataset.section = Id;
    const StatusNode = Status ? Construct('span', 'section-badge') : null;
    const Summary = Construct('summary', null, [
        Glyph ? ConstructGlyph(Glyph, 'glyph section-glyph') : null,
        Construct('span', 'section-title', Title),
        StatusNode || (Badge ? Construct('span', 'section-badge', Badge) : null)
    ]);
    const Content = Construct('div', 'group-content');
    if (Hint) Content.append(Construct('p', 'property-hint', Hint));
    Details.append(Summary, Content);
    Parent.append(Details);
    if (StatusNode) Register(() => { StatusNode.textContent = typeof Status === 'function' ? Status() : Status; });
    return { Card: Details, Inner: Content };
}

// 📝 ControlPanel::SliderPill (Fluid Editor): 92px black value-pill [57px scrubbable input | 34px unit cell] + 26px pill slider with 24px spherical thumb.
export function SliderRow(Parent, Binding)
{
    const Step = Binding.Step ?? 0.01;
    const Digits = Binding.Digits ?? (Step >= 1 ? 0 : Step >= 0.1 ? 1 : Step >= 0.01 ? 2 : 3);
    const Label = Construct('label', 'property-label', Binding.Label);
    const NumberInput = Construct('input', 'pill-number');
    NumberInput.type = 'number';
    NumberInput.min = String(Binding.Minimum);
    NumberInput.max = String(Binding.Maximum);
    NumberInput.step = String(Step);
    const UnitCell = Construct('span', 'unit-cell', Binding.Unit || '—');
    const ValuePill = Construct('div', 'value-pill', [NumberInput, UnitCell]);

    const Range = Construct('input', 'pill-range');
    Range.type = 'range';
    Range.min = String(Binding.Minimum);
    Range.max = String(Binding.Maximum);
    Range.step = String(Step);
    Range.setAttribute('aria-label', Binding.Label);

    const Control = Construct('div', 'slider-control', [ValuePill, Range]);
    const Row = Construct('div', 'property-row slider-row', [Label, Control]);
    Parent.append(Row);

    const Snap = Raw =>
    {
        const Clamped = Math.min(Binding.Maximum, Math.max(Binding.Minimum, Raw));
        const Precision = Math.pow(10, Digits + 2);
        return Math.round(Clamped * Precision) / Precision;
    };
    const SyncFraction = Value =>
    {
        const Span = (Binding.Maximum - Binding.Minimum) || 1;
        const Frac = Math.max(0, Math.min(1, (Value - Binding.Minimum) / Span));
        Range.style.setProperty('--fraction', Frac.toFixed(4));
    };

    const Commit = Raw =>
    {
        if (!Number.isFinite(Raw)) return;
        const Value = Snap(Raw);
        Binding.Write(Value);
        SyncFraction(Value);
        Broadcast();
    };

    Range.addEventListener('input', () => Commit(Number(Range.value)));
    NumberInput.addEventListener('change', () => Commit(Number(NumberInput.value)));

    // Fluid horizontal drag-scrubbing inside the black numeric value pill
    let Scrub = null;
    NumberInput.addEventListener('pointerdown', Event =>
    {
        if (document.activeElement === NumberInput) return;
        Scrub = { X: Event.clientX, Moved: false, Start: Number(Binding.Read()) || 0 };
        NumberInput.setPointerCapture(Event.pointerId);
    });
    NumberInput.addEventListener('pointermove', Event =>
    {
        if (!Scrub) return;
        const Dx = Event.clientX - Scrub.X;
        if (!Scrub.Moved && Math.abs(Dx) < 3) return;
        if (!Scrub.Moved) { Scrub.Moved = true; NumberInput.classList.add('scrubbing'); }
        const Span = Binding.Maximum - Binding.Minimum;
        const Factor = Event.shiftKey ? 0.15 : Event.altKey ? 4.0 : 1.0;
        const Next = Scrub.Start + (Dx / 180) * Span * Factor;
        Commit(Math.round(Next / Step) * Step);
    });
    const EndScrub = Event =>
    {
        if (!Scrub) return;
        const WasMoved = Scrub.Moved;
        Scrub = null;
        NumberInput.classList.remove('scrubbing');
        if (WasMoved) Event.preventDefault();
    };
    NumberInput.addEventListener('pointerup', EndScrub);
    NumberInput.addEventListener('pointercancel', EndScrub);

    Register(() =>
    {
        const Value = Number(Binding.Read());
        Range.value = String(Value);
        if (document.activeElement !== NumberInput) NumberInput.value = Number.isFinite(Value) ? Value.toFixed(Digits) : '0';
        SyncFraction(Number.isFinite(Value) ? Value : Binding.Minimum);
        ApplyAvailability(Row, Binding, State => { Range.disabled = NumberInput.disabled = !State.Enabled; });
    });
    return Row;
}

// 📝 Fluid 44x25 high-contrast pill toggle switch
export function SwitchRow(Parent, Binding)
{
    const Input = Construct('input');
    Input.type = 'checkbox';
    Input.setAttribute('aria-label', Binding.Label);
    const Track = Construct('span');
    const Switch = Construct('label', 'switch', [Input, Track]);
    const Copy = Construct('div', 'switch-copy', [Construct('span', 'property-label', Binding.Label)]);
    if (Binding.Detail) Copy.append(Construct('small', 'property-detail', Binding.Detail));
    const Row = Construct('div', 'property-row toggle-row', [Copy, Switch]);
    Parent.append(Row);

    Input.addEventListener('change', () => { Binding.Write(Input.checked); Broadcast(); });
    Register(() =>
    {
        Input.checked = !!Binding.Read();
        ApplyAvailability(Row, Binding, State => { Input.disabled = !State.Enabled; });
    });
    return Row;
}

export function SelectRow(Parent, Binding)
{
    const Select = Construct('select');
    for (const [Label, Value] of Binding.Options)
    {
        const Option = Construct('option', null, Label);
        Option.value = String(Value);
        Select.append(Option);
    }
    const Row = Construct('div', 'property-row select-row', [Construct('label', 'property-label', Binding.Label), Select]);
    Parent.append(Row);
    Select.addEventListener('change', () =>
    {
        const Found = Binding.Options.find(([, Value]) => String(Value) === Select.value);
        Binding.Write(Found ? Found[1] : Select.value);
        Broadcast();
    });
    Register(() =>
    {
        Select.value = String(Binding.Read());
        ApplyAvailability(Row, Binding, State => { Select.disabled = !State.Enabled; });
    });
    return Row;
}

export function TextRow(Parent, Binding)
{
    const Input = Construct('input', 'studio-input');
    Input.type = 'text';
    Input.value = String(Binding.Read() || '');
    const Row = Construct('div', 'property-row select-row', [Construct('label', 'property-label', Binding.Label), Input]);
    Parent.append(Row);
    Input.addEventListener('change', () => { Binding.Write(Input.value); Broadcast(); });
    Register(() => { if (document.activeElement !== Input) Input.value = String(Binding.Read() || ''); });
    return Row;
}

export function SegmentedRow(Parent, Binding)
{
    const Track = Construct('div', 'segmented-track');
    const Buttons = Binding.Options.map(([Label, Value]) =>
    {
        const Btn = Construct('button', null, Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { Binding.Write(Value); Broadcast(); });
        Track.append(Btn);
        return [Btn, Value];
    });
    const Row = Construct('div', 'property-row select-row', [Construct('span', 'property-label', Binding.Label), Track]);
    Parent.append(Row);
    Register(() =>
    {
        const Current = Binding.Read();
        for (const [Btn, Value] of Buttons) Btn.classList.toggle('active', Current === Value);
        ApplyAvailability(Row, Binding);
    });
    return Row;
}

export function VectorRow(Parent, Binding)
{
    const Axes = ['X', 'Y', 'Z'];
    const AxisColors = ['#ef5350', '#69d06d', '#5b8cff'];
    const Step = Binding.Step ?? 0.05;
    const Digits = Binding.Digits ?? 2;
    const Grid = Construct('div', 'vector-grid');
    const Inputs = Axes.map((Axis, Index) =>
    {
        const Input = Construct('input', 'vector-number');
        Input.type = 'number';
        Input.step = String(Step);
        if (Binding.Minimum !== undefined) Input.min = String(Binding.Minimum);
        if (Binding.Maximum !== undefined) Input.max = String(Binding.Maximum);
        const Tag = Construct('span', 'vector-axis', Axis);
        Tag.style.color = AxisColors[Index];
        const Cell = Construct('label', 'vector-cell', [Tag, Input]);
        Input.addEventListener('change', () =>
        {
            const Vec = Binding.Read().slice();
            Vec[Index] = Number(Input.value) || 0;
            Binding.Write(Vec);
            Broadcast();
        });
        Grid.append(Cell);
        return Input;
    });
    const Head = Construct('div', 'vector-head', [
        Construct('span', 'property-label', Binding.Label),
        Binding.Unit ? Construct('span', 'vector-unit', Binding.Unit) : null
    ]);
    const Row = Construct('div', 'property-row vector-row', [Head, Grid]);
    Parent.append(Row);
    Register(() =>
    {
        const Vec = Binding.Read() || [0, 0, 0];
        Inputs.forEach((Input, I) => { if (document.activeElement !== Input) Input.value = Number(Vec[I] ?? 0).toFixed(Digits); });
    });
    return Row;
}

export function ColourRow(Parent, Binding)
{
    const HexInput = Construct('input', 'colour-picker');
    HexInput.type = 'color';
    const Code = Construct('span', 'colour-hex');
    const Alpha = Construct('span', 'colour-alpha');
    const Right = Construct('div', 'colour-right', [Alpha, Code, HexInput]);
    const Top = Construct('div', 'colour-top', [Construct('span', 'property-label', Binding.Label), Right]);
    const Row = Construct('div', 'property-row colour-row', [Top]);

    let AlphaSlider = null;
    if (Binding.Alpha)
    {
        AlphaSlider = Construct('input', 'pill-range alpha-range');
        AlphaSlider.type = 'range';
        AlphaSlider.min = '0';
        AlphaSlider.max = '1';
        AlphaSlider.step = '0.01';
        AlphaSlider.addEventListener('input', () =>
        {
            const Linear = Binding.Read().slice();
            Linear[3] = Number(AlphaSlider.value);
            Binding.Write(Linear);
            Broadcast();
        });
        Row.append(AlphaSlider);
    }

    HexInput.addEventListener('input', () =>
    {
        const Hex = HexInput.value;
        const R = DisplayToLinear(parseInt(Hex.slice(1, 3), 16) / 255);
        const G = DisplayToLinear(parseInt(Hex.slice(3, 5), 16) / 255);
        const B = DisplayToLinear(parseInt(Hex.slice(5, 7), 16) / 255);
        const Prev = Binding.Read() || [1, 1, 1, 1];
        Binding.Write([R, G, B, Prev[3] ?? 1]);
        Broadcast();
    });

    Parent.append(Row);
    Register(() =>
    {
        const Lin = Binding.Read() || [1, 1, 1, 1];
        const ToByte = C => Math.round(Math.min(1, Math.max(0, LinearToDisplay(Math.min(1, Math.max(0, C))))) * 255).toString(16).padStart(2, '0');
        const Hex = `#${ToByte(Lin[0])}${ToByte(Lin[1])}${ToByte(Lin[2])}`;
        HexInput.value = Hex;
        Code.textContent = Hex.toLowerCase();
        if (AlphaSlider)
        {
            const A = Number(Lin[3] ?? 1);
            AlphaSlider.value = String(A);
            AlphaSlider.style.setProperty('--fraction', A.toFixed(3));
            Alpha.textContent = `${Math.round(A * 100)}%`;
        }
    });
    return Row;
}

// 📝 Sun Inspector signature Quick-Control Toggle Tiles (.property-switches)
export function ToggleTile(Parent, Binding)
{
    const Icon = Construct('div', 'switch-icon', ConstructGlyph(Binding.Glyph || 'Check'));
    const Name = Construct('div', 'switch-name', Binding.Label);
    const StateLabel = Construct('div', 'switch-state');
    const Tile = Construct('button', 'property-switch', [Icon, Name, StateLabel]);
    Tile.type = 'button';
    Tile.addEventListener('click', () =>
    {
        if (!ResolveState(Binding).Enabled) return;
        Binding.Write(!Binding.Read());
        Broadcast();
    });
    Parent.append(Tile);
    Register(() =>
    {
        const On = !!Binding.Read();
        const Avail = ApplyAvailability(Tile, Binding);
        Tile.classList.toggle('is-off', !On);
        Tile.disabled = !Avail.Enabled;
        StateLabel.textContent = !Avail.Enabled ? 'LOCKED' : On ? 'ON' : 'OFF';
    });
    return Tile;
}

export function ActionRow(Parent, Actions)
{
    const Row = Construct('div', 'action-row');
    for (const Act of Actions)
    {
        const Btn = Construct('button', 'action-button' + (Act.Tone ? ' ' + Act.Tone : ''), [
            Act.Glyph ? ConstructGlyph(Act.Glyph) : null,
            Construct('span', null, Act.Label)
        ]);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { if (!Btn.disabled) { Act.Act(); Broadcast(); } });
        Row.append(Btn);
        if (Act.Availability)
        {
            Register(() =>
            {
                const St = Act.Availability();
                Btn.disabled = !St.Enabled;
                if (!St.Enabled && St.Reason) Btn.title = St.Reason;
                else Btn.removeAttribute('title');
            });
        }
    }
    Parent.append(Row);
    return Row;
}

export function PillTabs(Parent, Binding)
{
    const Bar = Construct('div', 'inspector-tabs mode-pills');
    const Btns = Binding.Options.map(([Label, Value]) =>
    {
        const Btn = Construct('button', null, Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () => { Binding.Write(Value); Broadcast(); });
        Bar.append(Btn);
        return [Btn, Value];
    });
    Parent.append(Bar);
    Register(() =>
    {
        const Cur = Binding.Read();
        for (const [Btn, Value] of Btns) Btn.classList.toggle('active', Cur === Value);
    });
    return Bar;
}

export function ChipToggles(Parent, Binding)
{
    const Row = Construct('div', 'property-row');
    Row.append(Construct('label', 'property-label', Binding.Label));
    const Grid = Construct('div', 'chip-toggles');
    const Chips = Binding.Options.map(([Label, Key]) =>
    {
        const Btn = Construct('button', 'chip-toggle', Label);
        Btn.type = 'button';
        Btn.addEventListener('click', () =>
        {
            const Cur = new Set(Binding.Read());
            if (Cur.has(Key)) Cur.delete(Key); else Cur.add(Key);
            Binding.Write([...Cur]);
            Broadcast();
        });
        Grid.append(Btn);
        return [Btn, Key];
    });
    const Detail = Construct('span', 'property-detail');
    Row.append(Grid, Detail);
    Parent.append(Row);
    Register(() =>
    {
        const Active = new Set(Binding.Read());
        for (const [Btn, Key] of Chips) Btn.classList.toggle('active', Active.has(Key));
        Detail.textContent = Binding.Detail ? Binding.Detail([...Active]) : '';
    });
    return Row;
}

// 📝 Sun/Fluid Signature 3-Card KPI Row (one optional warm-cream spotlight card matching Atmosphere/Plume!)
export function MetricTrio(Parent, Items)
{
    const Grid = Construct('div', 'kpi-trio');
    const RefreshersLocal = Items.map(Item =>
    {
        const Val = Construct('strong', 'kpi-val');
        const Foot = Construct('small', 'kpi-sub');
        const Cell = Construct('div', 'kpi-card' + (Item.Highlight ? ' highlight' : ''), [
            Construct('span', 'kpi-label', Item.Label),
            Val,
            Foot
        ]);
        Grid.append(Cell);
        return () =>
        {
            const [Text, Sub] = Item.Resolve();
            Val.textContent = Text;
            Foot.textContent = Sub || '';
        };
    });
    Parent.append(Grid);
    const Refresh = () => RefreshersLocal.forEach(R => R());
    Register(Refresh);
    return { Grid, Refresh };
}

export function TimeSeries(Parent, Binding)
{
    const Height = Binding.Height || 110;
    const Wrap = Construct('div', 'diagram-wrap');
    const Svg = ConstructSvg('svg', { class: 'diagram', height: Height });
    Wrap.append(Svg);
    Parent.append(Wrap);
    const Refresh = () =>
    {
        const Width = Math.max(200, Svg.clientWidth || Parent.clientWidth || 320);
        const Suffix = Binding.Unit ? ' ' + Binding.Unit : '';
        const Samples = (Binding.Samples || []).map(V => Number.isFinite(V) ? V : 0);
        const RawTarget = typeof Binding.Target === 'function' ? Binding.Target() : (Binding.Target || 1);
        const Target = Number.isFinite(RawTarget) ? RawTarget : 1;
        const Peak = Math.max(1e-4, Target * (Binding.Headroom || 1.15), ...Samples);
        const Right = Width - 42, Bottom = Height - 18, Top = 10;
        const AtX = I => (I / Math.max(1, (Binding.Capacity || Samples.length || 2) - 1)) * Right;
        const AtY = V => Bottom - (Math.min(Peak, Math.max(0, Number.isFinite(V) ? V : 0)) / Peak) * (Bottom - Top);

        Svg.textContent = '';
        const Defs = ConstructSvg('defs');
        const Grad = ConstructSvg('linearGradient', { id: 'StudioSeriesGrad', x1: '0', y1: '0', x2: '0', y2: '1' });
        Grad.append(
            ConstructSvg('stop', { offset: '0%', 'stop-color': '#e8c27a', 'stop-opacity': '0.32' }),
            ConstructSvg('stop', { offset: '100%', 'stop-color': '#e8c27a', 'stop-opacity': '0.01' })
        );
        Defs.append(Grad);
        Svg.append(Defs);

        for (const Share of [0.33, 0.66, 1.0])
        {
            const Val = Peak * Share;
            const Y = AtY(Val);
            Svg.append(
                ConstructSvg('line', { x1: 0, x2: Right, y1: Y, y2: Y, stroke: '#ffffff', 'stroke-opacity': 0.06, 'stroke-dasharray': '2 5' }),
                ConstructSvg('text', { x: Width - 2, y: Y + 3, 'text-anchor': 'end', fill: '#666666', 'font-size': 9.5 }, Val.toFixed(Binding.AxisDigits ?? 0))
            );
        }

        const Offset = Binding.Capacity - Samples.length;
        if (Samples.length > 1)
        {
            const Points = Samples.map((V, I) => [AtX(I + Offset), AtY(V)]);
            const Line = Points.map(([X, Y], I) => `${I ? 'L' : 'M'}${X.toFixed(1)} ${Y.toFixed(1)}`).join(' ');
            Svg.append(
                ConstructSvg('path', { d: `${Line} L${Points[Points.length - 1][0].toFixed(1)} ${Bottom} L${Points[0][0].toFixed(1)} ${Bottom} Z`, fill: 'url(#StudioSeriesGrad)' }),
                ConstructSvg('path', { d: Line, fill: 'none', stroke: '#f0cfa0', 'stroke-width': 1.6, 'stroke-linejoin': 'round' })
            );
            const [Ex, Ey] = Points[Points.length - 1];
            Svg.append(
                ConstructSvg('circle', { cx: Ex, cy: Ey, r: 7, fill: '#f0cfa0', 'fill-opacity': 0.18 }),
                ConstructSvg('circle', { cx: Ex, cy: Ey, r: 3.4, fill: '#f7e5c6' })
            );
        }

        const TargetY = AtY(Target);
        Svg.append(
            ConstructSvg('line', { x1: 0, x2: Right, y1: TargetY, y2: TargetY, stroke: '#ffffff', 'stroke-opacity': 0.25, 'stroke-dasharray': '5 5' }),
            ConstructSvg('text', { x: 0, y: Math.max(9, TargetY - 4), fill: '#888888', 'font-size': 9.5 },
                         `${typeof Binding.TargetLabel === 'function' ? Binding.TargetLabel() : (Binding.TargetLabel || 'Target')} ${Target.toFixed(Binding.TargetDigits ?? 1)}${Suffix}`)
        );
        for (const [Label, Share] of [['−10 s', 0], ['−5 s', 0.5], ['now', 1]])
        {
            Svg.append(ConstructSvg('text', { x: Share * Right, y: Height - 2, 'text-anchor': Share === 0 ? 'start' : Share === 1 ? 'end' : 'middle', fill: '#626262', 'font-size': 9.5 }, Label));
        }
    };
    new ResizeObserver(Refresh).observe(Svg);
    Register(Refresh);
    return { Refresh };
}

// 📝 Sun-Inspector Style Optical/Emitter Cone & Colour Spectrum Diagram
export function EmitterInstrument(Parent, Effect, ToHex)
{
    const Height = 112;
    const Wrap = Construct('div', 'diagram-wrap sun-instrument');
    const Svg = ConstructSvg('svg', { class: 'diagram', height: Height });
    Wrap.append(Svg);
    Parent.append(Wrap);
    const Refresh = () =>
    {
        const P = Effect.P;
        const W = Math.max(220, Svg.clientWidth || Parent.clientWidth || 320);
        const Cx = W * 0.5, Cy = Height * 0.78, R = Height * 0.62;
        const ColA = ToHex(P.colA);
        const ColB = ToHex(P.colB);
        Svg.textContent = '';
        const Defs = ConstructSvg('defs');
        const ConeGrad = ConstructSvg('radialGradient', { id: 'ConeGlow', cx: '50%', cy: '85%', r: '75%' });
        ConeGrad.append(
            ConstructSvg('stop', { offset: '0%', 'stop-color': ColA, 'stop-opacity': '0.52' }),
            ConstructSvg('stop', { offset: '65%', 'stop-color': ColB, 'stop-opacity': '0.16' }),
            ConstructSvg('stop', { offset: '100%', 'stop-color': ColB, 'stop-opacity': '0' })
        );
        Defs.append(ConeGrad);
        Svg.append(Defs);

        // Horizon & Sun-style concentric optical rings
        Svg.append(ConstructSvg('line', { x1: 16, x2: W - 16, y1: Cy, y2: Cy, stroke: '#ffffff', 'stroke-opacity': 0.14 }));
        for (const Ring of [0.38, 0.68, 0.98])
        {
            Svg.append(ConstructSvg('path', {
                d: `M${Cx - R * Ring} ${Cy} A${R * Ring} ${R * Ring} 0 0 1 ${Cx + R * Ring} ${Cy}`,
                fill: 'none', stroke: '#ffffff', 'stroke-opacity': 0.08, 'stroke-dasharray': '3 5'
            }));
        }

        // Direction vector & spread fan
        const Dir = P.dir || [0, 1, 0];
        const Tilt = Math.atan2(Dir[0], Math.max(0.05, Dir[1])) * 0.7;
        const HalfSpread = Math.min(1.35, Math.max(0.08, (P.spread ?? 0.5) * 0.85));
        const A0 = -Math.PI / 2 + Tilt - HalfSpread;
        const A1 = -Math.PI / 2 + Tilt + HalfSpread;
        const X0 = Cx + Math.cos(A0) * R, Y0 = Cy + Math.sin(A0) * R;
        const X1 = Cx + Math.cos(A1) * R, Y1 = Cy + Math.sin(A1) * R;
        Svg.append(
            ConstructSvg('path', { d: `M${Cx} ${Cy} L${X0.toFixed(1)} ${Y0.toFixed(1)} A${R} ${R} 0 0 1 ${X1.toFixed(1)} ${Y1.toFixed(1)} Z`, fill: 'url(#ConeGlow)' }),
            ConstructSvg('line', { x1: Cx, y1: Cy, x2: X0.toFixed(1), y2: Y0.toFixed(1), stroke: ColA, 'stroke-opacity': 0.45 }),
            ConstructSvg('line', { x1: Cx, y1: Cy, x2: X1.toFixed(1), y2: Y1.toFixed(1), stroke: ColA, 'stroke-opacity': 0.45 }),
            ConstructSvg('circle', { cx: Cx, cy: Cy, r: 7.5, fill: ColA, stroke: '#121212', 'stroke-width': 2 }),
            ConstructSvg('text', { x: 16, y: 18, fill: '#7d7d7d', 'font-size': 9.5 }, `SPREAD ${((P.spread ?? 0.5) * 57.2958).toFixed(0)}°`),
            ConstructSvg('text', { x: W - 16, y: 18, 'text-anchor': 'end', fill: '#7d7d7d', 'font-size': 9.5 }, `${P.speedMin?.toFixed(1) ?? '0'}–${P.speedMax?.toFixed(1) ?? '0'} m/s`)
        );
    };
    new ResizeObserver(Refresh).observe(Svg);
    Register(Refresh);
    return { Wrap, Refresh };
}

export function CurvePlot(Host, Specification)
{
    const Height = Specification.Height || 124;
    const Panel = Construct('div', 'diagram-wrap curve-plot');
    const Svg = ConstructSvg('svg', { class: 'diagram', height: Height });
    const Caption = Construct('div', 'plot-caption');
    Panel.append(Svg, Caption);
    Host.append(Panel);
    const Refresh = () =>
    {
        const Plot = Specification.Resolve();
        const Width = Math.max(180, Svg.clientWidth || Host.clientWidth || 320);
        const Left = Plot.YTicks && Plot.YTicks.length ? 34 : 6;
        const Right = Width - 8, Top = 8, Bottom = Height - (Plot.XTicks && Plot.XTicks.length ? 18 : 6);
        const [X0, X1] = Plot.XRange;
        const [Y0, Y1] = Plot.YRange;
        const AtX = X => Left + ((X - X0) / ((X1 - X0) || 1)) * (Right - Left);
        const AtY = Y => Bottom - ((Y - Y0) / ((Y1 - Y0) || 1)) * (Bottom - Top);
        Svg.textContent = '';
        for (const [Y, Text] of Plot.YTicks || [])
        {
            Svg.append(
                ConstructSvg('line', { x1: Left, x2: Right, y1: AtY(Y), y2: AtY(Y), stroke: '#ffffff', 'stroke-opacity': 0.06, 'stroke-dasharray': '2 5' }),
                ConstructSvg('text', { x: Left - 6, y: AtY(Y) + 3, 'text-anchor': 'end', fill: '#6a6a6a', 'font-size': 9.5 }, Text)
            );
        }
        for (const [X, Text] of Plot.XTicks || [])
        {
            Svg.append(
                ConstructSvg('line', { x1: AtX(X), x2: AtX(X), y1: Top, y2: Bottom, stroke: '#ffffff', 'stroke-opacity': 0.05, 'stroke-dasharray': '2 5' }),
                ConstructSvg('text', { x: AtX(X), y: Height - 3, 'text-anchor': 'middle', fill: '#6a6a6a', 'font-size': 9.5 }, Text)
            );
        }
        for (const Curve of Plot.Curves || [])
        {
            const Clean = (Curve.Points || []).filter(([X, Y]) => Number.isFinite(X) && Number.isFinite(Y));
            if (Clean.length < 2) continue;
            const Path = Clean.map(([X, Y], Pos) => `${Pos ? 'L' : 'M'}${AtX(X).toFixed(1)} ${AtY(Y).toFixed(1)}`).join(' ');
            if (Curve.Fill)
            {
                const First = Clean[0], Last = Clean[Clean.length - 1];
                const BaseY = AtY(Math.max(Y0, Math.min(Y1, 0)));
                Svg.append(ConstructSvg('path', { d: `${Path} L${AtX(Last[0]).toFixed(1)} ${BaseY} L${AtX(First[0]).toFixed(1)} ${BaseY} Z`, fill: Curve.Fill }));
            }
            Svg.append(ConstructSvg('path', { d: Path, fill: 'none', stroke: Curve.Stroke || '#f0cfa0', 'stroke-width': 1.6, 'stroke-linejoin': 'round' }));
        }
        Caption.textContent = Plot.Caption || '';
        Caption.style.display = Plot.Caption ? '' : 'none';
    };
    new ResizeObserver(Refresh).observe(Svg);
    Register(Refresh);
    return { Panel, Refresh };
}

export function PolarDial(Host, Specification)
{
    const Size = Specification.Size || 148;
    const Panel = Construct('div', 'diagram-wrap polar-dial');
    const Svg = ConstructSvg('svg', { class: 'diagram dial-svg', height: Size, viewBox: `0 0 ${Size} ${Size}` });
    const Side = Construct('div', 'dial-side');
    Panel.append(Svg, Side);
    Host.append(Panel);
    const Centre = Size / 2, Radius = Size / 2 - 16;
    const Refresh = () =>
    {
        const Bearing = Specification.Read();
        const Turn = Bearing * Math.PI / 180;
        const Tip = [Centre + Math.sin(Turn) * Radius, Centre - Math.cos(Turn) * Radius];
        Svg.textContent = '';
        Svg.setAttribute('width', Size);
        Svg.append(
            ConstructSvg('circle', { cx: Centre, cy: Centre, r: Radius, fill: '#111111', stroke: '#2c2c2c' }),
            ConstructSvg('circle', { cx: Centre, cy: Centre, r: Radius * 0.6, fill: 'none', stroke: '#ffffff', 'stroke-opacity': 0.06, 'stroke-dasharray': '2 5' })
        );
        for (const [Text, Angle] of [['N', 0], ['E', 90], ['S', 180], ['W', 270]])
        {
            const A = Angle * Math.PI / 180;
            Svg.append(ConstructSvg('text', { x: Centre + Math.sin(A) * (Radius + 9), y: Centre - Math.cos(A) * (Radius + 9) + 3.5, 'text-anchor': 'middle', fill: Text === 'N' ? '#e0e0e0' : '#6c6c6c', 'font-size': 9.5 }, Text));
        }
        Svg.append(
            ConstructSvg('line', { x1: Centre, y1: Centre, x2: Tip[0], y2: Tip[1], stroke: '#e8c27a', 'stroke-width': 2, 'stroke-linecap': 'round' }),
            ConstructSvg('circle', { cx: Tip[0], cy: Tip[1], r: 6.5, fill: '#f2e0ba', stroke: '#111111', 'stroke-width': 2 }),
            ConstructSvg('circle', { cx: Centre, cy: Centre, r: 3, fill: '#ffffff' })
        );
        const Info = Specification.Resolve ? Specification.Resolve() : { Caption: '' };
        Side.textContent = '';
        Side.append(
            Construct('div', 'dial-label', Specification.Label || 'Bearing'),
            Construct('div', 'dial-reading', [document.createTextNode(Bearing.toFixed(0)), Construct('span', null, '°')]),
            Construct('div', 'dial-point', ResolveCompassPoint(Bearing)),
            Info.Caption ? Construct('div', 'dial-caption', Info.Caption) : null
        );
    };
    const BearingAt = Event =>
    {
        const Bounds = Svg.getBoundingClientRect();
        const Scale = Size / Math.max(1, Bounds.width);
        const X = (Event.clientX - Bounds.left) * Scale - Centre, Y = (Event.clientY - Bounds.top) * Scale - Centre;
        return ((Math.round(Math.atan2(X, -Y) * 180 / Math.PI) % 360) + 360) % 360;
    };
    let Dragging = false;
    Svg.addEventListener('pointerdown', Event => { Dragging = true; Svg.setPointerCapture(Event.pointerId); Specification.Write(BearingAt(Event)); Broadcast(); });
    Svg.addEventListener('pointermove', Event => { if (Dragging) { Specification.Write(BearingAt(Event)); Broadcast(); } });
    Svg.addEventListener('pointerup', () => { Dragging = false; });
    Svg.addEventListener('pointercancel', () => { Dragging = false; });
    Register(Refresh);
    return { Panel, Refresh };
}

export function SmoothScroll(Scroller, Content)
{
    let Target = Scroller.scrollTop, Current = Target, Running = false, LastWheel = 0;
    const Limit = () => Math.max(0, Scroller.scrollHeight - Scroller.clientHeight);
    const Advance = Now =>
    {
        const Max = Limit();
        Target = Now - LastWheel > 120 ? Math.min(Math.max(Target, 0), Max) : Math.min(Math.max(Target, -72), Max + 72);
        Current += (Target - Current) * 0.2;
        if (Math.abs(Target - Current) < 0.25) Current = Target;
        const Clamped = Math.min(Math.max(Current, 0), Max);
        Scroller.scrollTop = Clamped;
        const Stretch = Current - Clamped;
        Content.style.transform = Stretch ? `translateY(${(-Stretch * 0.4).toFixed(2)}px)` : '';
        if (Current !== Target || Stretch !== 0) requestAnimationFrame(Advance);
        else Running = false;
    };
    const Start = () => { if (!Running) { Running = true; requestAnimationFrame(Advance); } };
    Scroller.addEventListener('wheel', Event =>
    {
        if (Event.ctrlKey || Math.abs(Event.deltaX) > Math.abs(Event.deltaY)) return;
        Event.preventDefault();
        if (!Running) Current = Target = Scroller.scrollTop;
        Target += Event.deltaMode === 1 ? Event.deltaY * 18 : Event.deltaY;
        LastWheel = performance.now();
        Start();
    }, { passive: false });
    Scroller.addEventListener('scroll', () => { if (!Running) Current = Target = Scroller.scrollTop; });
    return {
        ScrollTo(Position)
        {
            if (!Running) Current = Scroller.scrollTop;
            Target = Math.min(Math.max(Position, 0), Limit());
            LastWheel = 0;
            Start();
        }
    };
}

export { ParticleInfographic, ForceFieldInfographic, DrawWindRoseInfographic } from './ParticleInfographics.js';

export function Toast(Text)
{
    let Element = document.querySelector('.toast');
    if (!Element) { Element = Construct('div', 'toast'); document.body.append(Element); }
    Element.textContent = Text;
    Element.classList.add('shown');
    clearTimeout(Toast.Timer);
    Toast.Timer = setTimeout(() => Element.classList.remove('shown'), 2000);
}

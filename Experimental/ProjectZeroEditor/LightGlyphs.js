//============================================================================================================================================
//                                                             LIGHTGLYPHS.JS
//============================================================================================================================================
// 📦 Drawn 24 × 24 icons for the light presets that shared a generic glyph (Rim Point, Fill Point, LED Emitter) and for
//    the Environment Light. Kept as SVG markup so the editor (img data URL) and the sandboxed light inspector frame
//    can both use them; each data URL is its own document, so gradient ids never collide.

const Markup = {
  // A dark subject lit from behind: a bright rim crescent on its edge and the point source above-right.
  "glyph-rim-point": `
    <defs>
      <radialGradient id="s" cx="38%" cy="40%" r="70%"><stop offset="0" stop-color="#3a3a3a"/><stop offset="1" stop-color="#151515"/></radialGradient>
      <linearGradient id="r" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#fff3c4"/><stop offset="1" stop-color="#ff9a3d"/></linearGradient>
      <radialGradient id="g" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="#fff6d8"/><stop offset=".45" stop-color="#ffc46a" stop-opacity=".7"/><stop offset="1" stop-color="#ff9a3d" stop-opacity="0"/></radialGradient>
    </defs>
    <circle cx="10" cy="14" r="7.2" fill="url(#s)"/>
    <path d="M13.4 7.6a7.2 7.2 0 0 1 3.6 7.6a7.2 7.2 0 0 1-3.3 5.1a8.6 8.6 0 0 0 1.5-6.7a8.6 8.6 0 0 0-1.8-6z" fill="url(#r)"/>
    <circle cx="18.6" cy="5.4" r="4.6" fill="url(#g)"/>
    <circle cx="18.6" cy="5.4" r="1.7" fill="#fffaf0"/>`,
  // A soft, wide source: large diffuse glow with faint falloff rings around a small core.
  "glyph-fill-point": `
    <defs>
      <radialGradient id="g" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="#e8f3ff"/><stop offset=".35" stop-color="#9cc8ff" stop-opacity=".75"/><stop offset="1" stop-color="#5f8fd6" stop-opacity="0"/></radialGradient>
    </defs>
    <circle cx="12" cy="12" r="11" fill="url(#g)"/>
    <circle cx="12" cy="12" r="7.4" fill="none" stroke="#bcd9ff" stroke-opacity=".35" stroke-width=".8"/>
    <circle cx="12" cy="12" r="10" fill="none" stroke="#bcd9ff" stroke-opacity=".18" stroke-width=".8" stroke-dasharray="1.6 1.8"/>
    <circle cx="12" cy="12" r="3.4" fill="#f4f9ff"/>
    <circle cx="11" cy="11" r="1.1" fill="#fff"/>`,
  // A chip-on-board package: dark substrate, four warm diodes, solder pads and a short emission fan.
  "glyph-led-emitter": `
    <defs>
      <linearGradient id="b" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3b3b3b"/><stop offset="1" stop-color="#1c1c1c"/></linearGradient>
      <radialGradient id="d" cx="40%" cy="35%" r="70%"><stop offset="0" stop-color="#fffbe6"/><stop offset="1" stop-color="#ffc35a"/></radialGradient>
      <linearGradient id="f" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="#ffd27a" stop-opacity=".75"/><stop offset="1" stop-color="#ffd27a" stop-opacity="0"/></linearGradient>
    </defs>
    <path d="M6.5 9.5L3 2.5h18l-3.5 7z" fill="url(#f)"/>
    <rect x="5" y="9" width="14" height="12" rx="2.6" fill="url(#b)" stroke="#5a5a5a" stroke-width=".6"/>
    <rect x="3" y="12" width="2" height="1.6" rx=".5" fill="#b08a4a"/><rect x="3" y="16.4" width="2" height="1.6" rx=".5" fill="#b08a4a"/>
    <rect x="19" y="12" width="2" height="1.6" rx=".5" fill="#b08a4a"/><rect x="19" y="16.4" width="2" height="1.6" rx=".5" fill="#b08a4a"/>
    <rect x="7.6" y="11.4" width="3.6" height="3.2" rx=".9" fill="url(#d)"/><rect x="12.8" y="11.4" width="3.6" height="3.2" rx=".9" fill="url(#d)"/>
    <rect x="7.6" y="15.6" width="3.6" height="3.2" rx=".9" fill="url(#d)"/><rect x="12.8" y="15.6" width="3.6" height="3.2" rx=".9" fill="url(#d)"/>`,
  // Image-based sky light: a sky dome graded from zenith blue to a warm horizon, a low sun and the ground plane.
  "glyph-environment-light": `
    <defs>
      <linearGradient id="k" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#3f7fe0"/><stop offset=".65" stop-color="#9cc4f0"/><stop offset="1" stop-color="#ffcf9a"/></linearGradient>
      <radialGradient id="u" cx="50%" cy="50%" r="50%"><stop offset="0" stop-color="#fffbe8"/><stop offset=".5" stop-color="#ffd27a" stop-opacity=".8"/><stop offset="1" stop-color="#ffb04a" stop-opacity="0"/></radialGradient>
      <linearGradient id="g" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#5a5248"/><stop offset="1" stop-color="#2a2622"/></linearGradient>
    </defs>
    <path d="M2 16a10 10 0 0 1 20 0z" fill="url(#k)"/>
    <path d="M5.6 16a6.4 6.4 0 0 1 12.8 0" fill="none" stroke="#ffffff" stroke-opacity=".35" stroke-width=".7" stroke-dasharray="1.2 1.4"/>
    <circle cx="16.4" cy="11.2" r="3.6" fill="url(#u)"/>
    <circle cx="16.4" cy="11.2" r="1.3" fill="#fffdf2"/>
    <rect x="1.5" y="16" width="21" height="4.6" rx="1.6" fill="url(#g)"/>
    <path d="M7 18.3h10" stroke="#ffffff" stroke-opacity=".18" stroke-width=".8" stroke-linecap="round"/>`,
};

export const PresetGlyphs = {
  "Rim Point": "glyph-rim-point",
  "Fill Point": "glyph-fill-point",
  "LED Emitter": "glyph-led-emitter",
  "Environment Light": "glyph-environment-light",
};

const Sources = {};
export function LightGlyphSource(Name) {
  if (!Markup[Name]) return null;
  Sources[Name] ??=
    "data:image/svg+xml;charset=utf-8," +
    encodeURIComponent(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">${Markup[Name]}</svg>`);
  return Sources[Name];
}

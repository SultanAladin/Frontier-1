/*============================================================================================================================================
                                                        WORKSPACEICONS.JS
============================================================================================================================================*/
// Analytical vector SVG icon library for ParticleWorkspace emitters, light fibres, molecular ensembles, transitions & force fields.

export function GetParticleSourceSvgIcon(CategoryOrShape, Colour = '#589979')
{
    const Paths = {
        Sparks: `<path d="M12 3v5M12 16v5M3 12h5M16 12h5M5.6 5.6l3.5 3.5M14.9 14.9l3.5 3.5M18.4 5.6l-3.5 3.5M9.1 14.9l-3.5 3.5" stroke="${Colour}" stroke-width="1.9" stroke-linecap="round"/><circle cx="12" cy="12" r="2.2" fill="${Colour}"/>`,
        Weather: `<path d="M7 14a4 4 0 0 1 .4-7.9A5 5 0 0 1 17 7.5a3.5 3.5 0 0 1-.5 6.5z" stroke="${Colour}" stroke-width="1.6" fill="none"/><path d="M8.5 17.5l-1 3M12.5 17.5l-1 3M16.5 17.5l-1 3" stroke="${Colour}" stroke-width="1.7" stroke-linecap="round"/>`,
        WindBlown: `<path d="M5 19c2-7 8-12 14-14-1 7-6 13-14 14z" stroke="${Colour}" stroke-width="1.7" fill="none"/><path d="M5 19l9-9" stroke="${Colour}" stroke-width="1.4" stroke-linecap="round"/>`,
        VFX: `<path d="M12 3c3 4 5 6.5 5 10a5 5 0 1 1-10 0c0-2.4 1.3-4.3 2.7-6-.1 1.6.7 2.4 1.6 2.8-.2-2.2.2-4.6.7-6.8z" stroke="${Colour}" stroke-width="1.7" fill="none" stroke-linejoin="round"/>`,
        Physics: `<circle cx="12" cy="12" r="2.2" fill="${Colour}"/><ellipse cx="12" cy="12" rx="9" ry="3.8" transform="rotate(35 12 12)" stroke="${Colour}" stroke-width="1.4" fill="none"/><ellipse cx="12" cy="12" rx="9" ry="3.8" transform="rotate(-35 12 12)" stroke="${Colour}" stroke-width="1.4" fill="none"/>`,
        Swarm: `<circle cx="7" cy="9" r="2" fill="${Colour}"/><circle cx="16" cy="7.5" r="1.7" fill="${Colour}"/><circle cx="13" cy="14" r="2.2" fill="${Colour}"/><circle cx="8" cy="17.5" r="1.5" fill="${Colour}"/><path d="M7 9c2.5-1.5 6-1.5 9-1.5M13 14c1.5-2 2.2-4 3-6.5" stroke="${Colour}" stroke-width="1.2" opacity="0.65" fill="none"/>`,
        Fibre: `<path d="M3 16c4-9 8 9 12-5 1.7-5 4-4 6 2" stroke="${Colour}" stroke-width="2" fill="none" stroke-linecap="round"/><path d="M3 12c4-5 8 7 12-1 2-3.5 4-2 6 3" stroke="${Colour}" stroke-width="1.3" opacity="0.55" fill="none" stroke-linecap="round"/>`,
        Transition: `<path d="M12 3l7.5 4.3v9.4L12 21l-7.5-4.3V7.3z" stroke="${Colour}" stroke-width="1.6" fill="none" stroke-linejoin="round"/><path d="M12 12l7.5-4.7M12 12v9M12 12L4.5 7.3" stroke="${Colour}" stroke-width="1.4"/>`,
        Storm: `<path d="M13 3L6 13h5.5L10.5 21l7.5-10h-5.5z" stroke="${Colour}" stroke-width="1.7" fill="none" stroke-linejoin="round"/>`,

        // Force contributions
        Flow: `<path d="M3 8h11a2.5 2.5 0 1 0-2.5-2.5M3 13h15a2.5 2.5 0 1 1-2.5 2.5M3 18h9" stroke="${Colour}" stroke-width="1.7" stroke-linecap="round" fill="none"/>`,
        Accelerate: `<circle cx="12" cy="12" r="2.6" fill="${Colour}"/><circle cx="12" cy="12" r="7.5" stroke="${Colour}" stroke-width="1.3" stroke-dasharray="3 2.5" fill="none"/><path d="M12 2v3.5M12 18.5V22M2 12h3.5M18.5 12H22" stroke="${Colour}" stroke-width="1.7" stroke-linecap="round"/>`,
        Damp: `<rect x="4" y="5" width="16" height="14" rx="3" stroke="${Colour}" stroke-width="1.6" fill="none" stroke-dasharray="3 2"/><path d="M8 10h8M8 14h8" stroke="${Colour}" stroke-width="1.7" stroke-linecap="round"/>`
    };
    const Body = Paths[CategoryOrShape] || Paths.Sparks;
    return `<svg viewBox="0 0 24 24" width="14" height="14" aria-hidden="true" style="flex-shrink:0">${Body}</svg>`;
}

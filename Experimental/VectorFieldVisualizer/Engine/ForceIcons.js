/*============================================================================================================================================
                                                        FORCEICONS.JS
============================================================================================================================================*/
// 🧩 Bespoke 16×16 Vector SVG Icons for all 19 ForceModel Laws (Outliner & Construct Palette)

const RAW_ICONS = {
    LinearWind: '<path d="M2 6h8.5a2 2 0 1 0-1.9-2.6M2 10h11a1.8 1.8 0 1 1-1.6 2.5M2 13h6.5a1.5 1.5 0 1 1-1.3 2.2"/>',
    FlowStream: '<path d="M2 5c3-2 6 2 9 0l2-1M2 9c3-2 6 2 9 0l2-1M2 13c3-2 6 2 9 0l2-1"/><path d="M11.5 3.2L14 4l-.8 2.4"/>',
    Buoyancy: '<path d="M8 14V3M4.8 6.2L8 3l3.2 3.2"/><circle cx="8" cy="11.5" r="1.7"/>',
    RadialBurst: '<circle cx="8" cy="8" r="2.2"/><circle cx="8" cy="8" r="5.5" stroke-dasharray="2.2 2"/><path d="M8 1v2M8 13v2M1 8h2M13 8h2"/>',
    Attractor: '<circle cx="8" cy="8" r="1.8" fill="currentColor"/><path d="M2.5 2.5L5.8 5.8M13.5 2.5l-3.3 3.3M2.5 13.5l3.3-3.3M13.5 13.5l-3.3-3.3"/>',
    Repulsor: '<circle cx="8" cy="8" r="1.8"/><path d="M5.5 5.5L2.2 2.2M10.5 5.5l3.3-3.3M5.5 10.5l-3.3 3.3M10.5 10.5l3.3 3.3M2.2 4.5V2.2h2.3M13.8 4.5V2.2h-2.3"/>',
    AxisAttractor: '<path d="M3 13L13 3"/><path d="M4.5 5.5l2.5 2.5M11.5 10.5L9 8"/>',
    SplineForce: '<path d="M2 12.5C5 12.5 5 3.5 8 3.5s3 9 6 9"/><path d="M12.2 10.5L14 12.5l-2 1.8"/>',
    SurfaceAdhesion: '<path d="M2 12.5h12"/><path d="M4 5c2.5 4.5 5.5 4.5 8 0"/><path d="M8 3.5v6.5M6 8l2 2 2-2"/>',
    Containment: '<circle cx="8" cy="8" r="6"/><circle cx="8" cy="8" r="3" stroke-dasharray="1.8 1.8"/><path d="M8 2v2M8 12v2M2 8h2M12 8h2"/>',
    Vortex: '<path d="M8 8a1.2 1.2 0 1 1-1.2-1.2 2.4 2.4 0 0 1 2.4 2.4A3.8 3.8 0 0 1 4 8a5.2 5.2 0 0 1 10.2-1"/>',
    Torque: '<path d="M12.8 6.2A5.2 5.2 0 1 0 11.5 12"/><path d="M10.2 6.5h3.2V3.3"/><circle cx="8" cy="8" r="1.4"/>',
    MagnusForce: '<circle cx="6.5" cy="9.5" r="3"/><path d="M11 4.5l2.5-2M11 2.5h2.5V5M3 5c1.8-1.6 4.5-1.6 6.2 0"/>',
    Drag: '<path d="M5.5 3v10M10.5 3v10M2 8h3.5M14 8h-3.5"/>',
    Lift: '<path d="M2.5 11.5C5 8.5 10.5 8 14 10.8c-3.8.7-7.8 1.2-11.5.7z"/><path d="M8 8V2.8M5.8 5L8 2.8 10.2 5"/>',
    CurlNoise: '<path d="M2.5 7.5a2.8 2.8 0 1 1 5.2 1.4 2.8 2.8 0 1 0 5.6 1"/><path d="M11.8 5.2a2 2 0 0 0-3-.2"/>',
    BrownianNoise: '<path d="M2.5 11.5L5 7l2.2 3.2L10 4.5l2 4 2-2.5"/><circle cx="2.5" cy="11.5" r="1" fill="currentColor"/><circle cx="14" cy="6" r="1" fill="currentColor"/>',
    LorentzField: '<path d="M2.5 9.5c1.5-4.5 3.5-4.5 4.5 0s3 4.5 4.5 0 1.8-4.5 2.5 0"/><path d="M2 4h12M11.8 2.2L14 4l-2.2 1.8"/>',
    MagneticDipole: '<path d="M6.5 4.5C3 4.5 2 8 2 8s1 3.5 4.5 3.5M9.5 4.5C13 4.5 14 8 14 8s-1 3.5-4.5 3.5"/><rect x="6.6" y="4.5" width="2.8" height="7" rx="1"/>'
};

export function GetForceModelSvgIcon(ModelId, Colour = '#89e0c4')
{
    const Paths = RAW_ICONS[ModelId] || '<circle cx="8" cy="8" r="4"/>';
    return `<svg class="outliner-svg-icon" viewBox="0 0 16 16" width="16" height="16" fill="none" stroke="${Colour}" stroke-width="1.4" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">${Paths}</svg>`;
}

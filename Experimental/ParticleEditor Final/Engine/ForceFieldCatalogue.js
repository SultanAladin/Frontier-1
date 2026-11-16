/*==========================================================================================================================================
                                                       FORCEFIELDCATALOGUE.JS
==========================================================================================================================================*/
// 🧩 Frontier · Force Field & Wind Catalogue (imported from ForceFieldEditor + ForceFieldVisualization, calibrated to work at both
//    Particle Studio 12×8×12 m chamber scale AND planetary/storm metre-to-kilometre scales):
//    17 bounded physical force types across 3 contributions:
//      • Flow [m/s]        — WindGenerator, Tornado, Jet, GustFront, Microburst, Thermal, Puff, Blast, Wake, Current
//      • Accelerate [m/s²] — Attractor, Repulsor, Orbit, Lift, Magnetic, Gravity
//      • Damp [1/s]        — Drag
//    Every source enforces strict finite spatial bounds (Sphere / Column / Box) and Activation timing (Always / Window / Trigger).

import {
    CompileFieldProgram,
    DEFAULT_FORMULA_CODE,
    FORMULA_PRESETS
} from './VectorFieldLang.js';

export { CompileFieldProgram, DEFAULT_FORMULA_CODE, FORMULA_PRESETS };

const Radian = Math.PI / 180;
const Clamp = (V, Lo, Hi) => Math.min(Hi, Math.max(Lo, V));
const Smooth01 = (Share) => { const T = Clamp(Share, 0, 1); return T * T * (3 - 2 * T); };

//------------------------------------------------------------------------------------------------------------------------
//                                                    TAXONOMY & TYPES
//------------------------------------------------------------------------------------------------------------------------

export const Contributions = {
    Flow:       { Key: 'Flow',       Label: 'Flow',       Unit: 'm/s',  Colour: '#8fd3c4', Description: 'Airflow velocity vector u(x,t) that aerodynamic & wind-coupled particles relax toward.' },
    Accelerate: { Key: 'Accelerate', Label: 'Accelerate', Unit: 'm/s²', Colour: '#e8c27a', Description: 'Direct body acceleration vector a(x,t) added to particle velocity.' },
    Damp:       { Key: 'Damp',       Label: 'Damp',       Unit: '1/s',  Colour: '#b9a3e8', Description: 'Scalar drag rate σ(x,t) attenuating velocity exponentially inside influence.' }
};

export const Channels = [
    ['Wind particles', 'Particles'],
    ['Debris chunks',  'Debris'],
    ['Precipitation',  'Precipitation'],
    ['Volumetric clouds', 'Clouds'],
    ['Plumes & smoke', 'Plumes'],
    ['Foliage',        'Foliage'],
    ['Swarm / craft',  'Vehicles']
];

export const Reaches = ['Sphere', 'Column', 'Box'];
export const Falloffs = ['Smooth', 'Linear', 'InverseSquare', 'None'];
export const Activations = ['Always', 'Window', 'Trigger'];

export function Falls(Falloff, Distance, Reach)
{
    if (Falloff === 'None') return Distance <= Reach ? 1 : 0;
    const Q = Clamp(Distance / Math.max(1e-4, Reach), 0, 1);
    if (Q >= 1) return 0;
    if (Falloff === 'Linear') return 1 - Q;
    if (Falloff === 'InverseSquare') return (1 - Q * Q) / (1 + 8 * Q * Q);
    const S = 1 - Q * Q;
    return S * S;
}

export const Types = [
    // ── FLOW SOURCES (m/s) ──
    {
        Id: 'WindGenerator', Native: 'WindGenerator', Name: 'Wind Generator', Contribution: 'Flow',
        Glyph: 'Wind', Colour: '#4ae4e8', Reach: 'Column',
        Defaults: { Strength: 18, Bearing: 75, GustStrength: 0.25, GustFrequency: 0.14, Turbulence: 0.42, TurbulenceScale: 42, Radius: 240, Height: 220, HalfX: 160, HalfZ: 220, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 8.5, Bearing: 72, GustStrength: 0.25, GustFrequency: 0.18, Turbulence: 0.42, TurbulenceScale: 2.2, Radius: 3.6, Height: 5.5, HalfX: 2.8, HalfZ: 3.4, Falloff: 'Smooth', CentreY: 0 },
        Bearings: [['Bearing', 'Blow direction']],
        Parameters: [
            { Key: 'Strength',        Label: 'Airflow speed',      Min: 0,    Max: 65,  Step: 0.5,  Digits: 1, Unit: 'm/s', ChamberMax: 25 },
            { Key: 'GustStrength',    Label: 'Gust amplitude',     Min: 0,    Max: 1,   Step: 0.02, Digits: 2, Unit: '×' },
            { Key: 'GustFrequency',   Label: 'Gust frequency',     Min: 0.02, Max: 1.5, Step: 0.02, Digits: 2, Unit: 'Hz' },
            { Key: 'Turbulence',      Label: 'Eddy turbulence',    Min: 0,    Max: 1.5, Step: 0.02, Digits: 2, Unit: '×' },
            { Key: 'TurbulenceScale', Label: 'Eddy length scale',  Min: 1,    Max: 250, Step: 0.5,  Digits: 1, Unit: 'm',   ChamberMin: 0.4, ChamberMax: 8 }
        ],
        Blurb: 'Localized bounded directional blower with coherent gusts and curl eddies — zero infinite leakage.'
    },
    {
        Id: 'Tornado', Native: 'VortexWindSource', Name: 'Tornado / Vortex', Contribution: 'Flow',
        Glyph: 'Tornado', Colour: '#32e6a4', Reach: 'Column',
        Defaults: { Strength: 46, InnerRadius: 28, CollarRadius: 82, Inflow: 14, Updraft: 26, Height: 1400, TrackBearing: 55, TrackSpeed: 0, Spin: 1, Radius: 280, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 14.5, InnerRadius: 0.85, CollarRadius: 1.75, Inflow: 4.5, Updraft: 6.5, Height: 5.5, TrackBearing: 55, TrackSpeed: 0, Spin: 1, Radius: 3.5, Falloff: 'Smooth', CentreY: 0 },
        Bearings: [['TrackBearing', 'Storm track direction']], Spin: true,
        Parameters: [
            { Key: 'Strength',     Label: 'Peak swirl speed',    Min: 0,   Max: 120,  Step: 0.5,  Digits: 1, Unit: 'm/s', ChamberMax: 30 },
            { Key: 'InnerRadius',  Label: 'Core wall radius',    Min: 0.2, Max: 400,  Step: 0.05, Digits: 2, Unit: 'm',   ChamberMin: 0.2, ChamberMax: 3.5 },
            { Key: 'CollarRadius', Label: 'Upper flare radius',  Min: 0.3, Max: 900,  Step: 0.05, Digits: 2, Unit: 'm',   ChamberMin: 0.3, ChamberMax: 5 },
            { Key: 'Inflow',       Label: 'Surface inflow',      Min: 0,   Max: 50,   Step: 0.2,  Digits: 1, Unit: 'm/s', ChamberMax: 16 },
            { Key: 'Updraft',      Label: 'Core updraft',        Min: 0,   Max: 60,   Step: 0.2,  Digits: 1, Unit: 'm/s', ChamberMax: 20 },
            { Key: 'TrackSpeed',   Label: 'Translation speed',   Min: 0,   Max: 35,   Step: 0.1,  Digits: 1, Unit: 'm/s', ChamberMax: 6 }
        ],
        Blurb: 'Swirling Burgers-Rott vortex column with inward boundary convergence and central updraft funnel.'
    },
    {
        Id: 'Jet', Native: 'JetCorridorWind', Name: 'Jet Corridor', Contribution: 'Flow',
        Glyph: 'Jet', Colour: '#38b6ff', Reach: 'Box',
        Defaults: { Strength: 26, Bearing: 95, Length: 800, InnerRadius: 110, Radius: 240, HalfX: 160, HalfZ: 280, Height: 240, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 11.5, Bearing: 105, Length: 4.2, InnerRadius: 0.85, Radius: 3.2, HalfX: 2.2, HalfZ: 3.5, Height: 3.5, Falloff: 'Smooth', CentreY: 1.6 },
        Bearings: [['Bearing', 'Jet axis bearing']],
        Parameters: [
            { Key: 'Strength',    Label: 'Centreline jet',   Min: 0,   Max: 90,   Step: 0.5,  Digits: 1, Unit: 'm/s', ChamberMax: 25 },
            { Key: 'Length',      Label: 'Corridor length',  Min: 0.5, Max: 4000, Step: 0.1,  Digits: 1, Unit: 'm',   ChamberMin: 0.8, ChamberMax: 10 },
            { Key: 'InnerRadius', Label: 'Core half-width',  Min: 0.2, Max: 1200, Step: 0.05, Digits: 2, Unit: 'm',   ChamberMin: 0.2, ChamberMax: 3.5 }
        ],
        Blurb: 'Focussed Venturi air tunnel smoothly diffusing radially and terminating strictly within box bounds.'
    },
    {
        Id: 'GustFront', Native: 'GustFrontWindSource', Name: 'Gust Front', Contribution: 'Flow',
        Glyph: 'Front', Colour: '#52e0d4', Reach: 'Box',
        Defaults: { Strength: 24, FrontSpeed: 12, CrestDepth: 220, HalfWidth: 650, Height: 480, Bearing: 115, Radius: 320, HalfX: 300, HalfZ: 220, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 10.5, FrontSpeed: 1.8, CrestDepth: 2.2, HalfWidth: 4.0, Height: 4.5, Bearing: 90, Radius: 3.8, HalfX: 3.6, HalfZ: 2.8, Falloff: 'Smooth', CentreY: 0 },
        Bearings: [['Bearing', 'Advance bearing']],
        Parameters: [
            { Key: 'Strength',   Label: 'Surge peak',       Min: 0,   Max: 60,   Step: 0.5, Digits: 1, Unit: 'm/s', ChamberMax: 22 },
            { Key: 'FrontSpeed', Label: 'Advance speed',    Min: 0,   Max: 45,   Step: 0.1, Digits: 1, Unit: 'm/s', ChamberMax: 8 },
            { Key: 'CrestDepth', Label: 'Crest depth',      Min: 0.3, Max: 1500, Step: 0.1, Digits: 1, Unit: 'm',   ChamberMin: 0.5, ChamberMax: 6 },
            { Key: 'HalfWidth',  Label: 'Lateral span',     Min: 0.5, Max: 5000, Step: 0.1, Digits: 1, Unit: 'm',   ChamberMin: 1.0, ChamberMax: 8 },
            { Key: 'Height',     Label: 'Boundary ceiling', Min: 0.5, Max: 2500, Step: 0.1, Digits: 1, Unit: 'm',   ChamberMin: 1.0, ChamberMax: 8 }
        ],
        Blurb: 'Steep convective cold-air surge crest advancing with upward leading-edge curl and wake decay.'
    },
    {
        Id: 'Microburst', Native: 'MicroburstWindSource', Name: 'Microburst', Contribution: 'Flow',
        Glyph: 'Downburst', Colour: '#65d6ff', Reach: 'Column',
        Defaults: { Strength: 28, Downdraft: 20, InnerRadius: 70, RingRadius: 190, Height: 450, Radius: 340, Falloff: 'Smooth', CentreY: 220 },
        ChamberDefaults: { Strength: 12.0, Downdraft: 9.0, InnerRadius: 0.8, RingRadius: 1.8, Height: 4.8, Radius: 3.6, Falloff: 'Smooth', CentreY: 3.8 },
        Bearings: [],
        Parameters: [
            { Key: 'Strength',    Label: 'Outflow burst',    Min: 0,   Max: 65,   Step: 0.5,  Digits: 1, Unit: 'm/s', ChamberMax: 25 },
            { Key: 'Downdraft',   Label: 'Downdraft core',   Min: 0,   Max: 45,   Step: 0.5,  Digits: 1, Unit: 'm/s', ChamberMax: 20 },
            { Key: 'InnerRadius', Label: 'Shaft radius',     Min: 0.2, Max: 1000, Step: 0.05, Digits: 2, Unit: 'm',   ChamberMin: 0.2, ChamberMax: 3 },
            { Key: 'RingRadius',  Label: 'Vortex ringpeak',  Min: 0.4, Max: 2500, Step: 0.1,  Digits: 1, Unit: 'm',   ChamberMin: 0.5, ChamberMax: 5 }
        ],
        Blurb: 'Descending shaft that impacts the ground plane and fans radially outward in a ground-hugging ring.'
    },
    {
        Id: 'Thermal', Native: 'ThermalWindSource', Name: 'Thermal Plume', Contribution: 'Flow',
        Glyph: 'Thermal', Colour: '#ff9f43', Reach: 'Column',
        Defaults: { Strength: 14, InnerRadius: 55, Inflow: 4, Height: 600, Radius: 210, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 8.5, InnerRadius: 0.75, Inflow: 3.2, Height: 5.5, Radius: 3.0, Falloff: 'Smooth', CentreY: 0 },
        Bearings: [],
        Parameters: [
            { Key: 'Strength',    Label: 'Core updraft',      Min: 0,   Max: 35,  Step: 0.2,  Digits: 1, Unit: 'm/s', ChamberMax: 20 },
            { Key: 'InnerRadius', Label: 'Chimney radius',    Min: 0.2, Max: 800, Step: 0.05, Digits: 2, Unit: 'm',   ChamberMin: 0.2, ChamberMax: 3 },
            { Key: 'Inflow',      Label: 'Entrainment pull',  Min: 0,   Max: 15,  Step: 0.1,  Digits: 1, Unit: 'm/s', ChamberMax: 10 }
        ],
        Blurb: 'Buoyant rising air column drawing air in at the base and flaring outward at top ceiling.'
    },
    {
        Id: 'Puff', Native: 'WindPuff', Name: 'Wind Puff', Contribution: 'Flow',
        Glyph: 'Puff', Colour: '#80e8ff', Reach: 'Sphere',
        Defaults: { Strength: 18, Bearing: 75, Lift: 1.8, Drift: 0.35, Radius: 130, RadiusY: 65, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 9.0, Bearing: 60, Lift: 2.2, Drift: 0.35, Radius: 2.4, RadiusY: 1.6, Falloff: 'Smooth', CentreY: 1.5 },
        Bearings: [['Bearing', 'Impulse direction']],
        Parameters: [
            { Key: 'Strength', Label: 'Peak impulse speed', Min: 0,   Max: 45,  Step: 0.2,  Digits: 1, Unit: 'm/s', ChamberMax: 20 },
            { Key: 'Lift',     Label: 'Vertical loft',      Min: -10, Max: 15,  Step: 0.1,  Digits: 1, Unit: 'm/s' },
            { Key: 'RadiusY',  Label: 'Vertical radius',    Min: 0.3, Max: 400, Step: 0.05, Digits: 2, Unit: 'm',   ChamberMin: 0.3, ChamberMax: 5 },
            { Key: 'Drift',    Label: 'Ambient advection',  Min: 0,   Max: 1,   Step: 0.05, Digits: 2, Unit: '×' }
        ],
        Blurb: 'Compact ellipsoid gust packet with clean Gaussian falloff and optional ambient wind drift.'
    },
    {
        Id: 'Blast', Native: 'ExplosionFlow', Name: 'Blast Outflow', Contribution: 'Flow',
        Glyph: 'Blast', Colour: '#ff6b4a', Reach: 'Sphere',
        Defaults: { Strength: 38, InitialRadius: 14, Expansion: 36, Thickness: 24, Radius: 280, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 18, InitialRadius: 0.35, Expansion: 2.6, Thickness: 0.7, Radius: 3.2, Falloff: 'Smooth', CentreY: 1.0 },
        Bearings: [],
        Parameters: [
            { Key: 'Strength',      Label: 'Wavefront peak',   Min: 0,   Max: 180, Step: 0.5,  Digits: 1, Unit: 'm/s', ChamberMax: 45 },
            { Key: 'InitialRadius', Label: 'Start radius',     Min: 0.1, Max: 100, Step: 0.05, Digits: 2, Unit: 'm',   ChamberMin: 0.1, ChamberMax: 3 },
            { Key: 'Expansion',     Label: 'Front velocity',   Min: 0.2, Max: 400, Step: 0.1,  Digits: 1, Unit: 'm/s', ChamberMin: 0.2, ChamberMax: 12 },
            { Key: 'Thickness',     Label: 'Shell thickness',  Min: 0.1, Max: 100, Step: 0.05, Digits: 2, Unit: 'm',   ChamberMin: 0.15, ChamberMax: 4 }
        ],
        Blurb: 'Expanding spherical pressure pulse ring that propagates strictly inside finite blast radius.'
    },
    {
        Id: 'Wake', Native: 'VehicleWakeWindSource', Name: 'Vehicle Wake', Contribution: 'Flow',
        Glyph: 'Wake', Colour: '#7bdff2', Reach: 'Box',
        Defaults: { Strength: 16, Speed: 22, Bearing: 20, HalfWidth: 42, Height: 35, Length: 240, Circulation: 2.4, Radius: 180, HalfX: 90, HalfZ: 160, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 8.0, Speed: 10, Bearing: 30, HalfWidth: 0.9, Height: 2.2, Length: 3.8, Circulation: 2.4, Radius: 3.2, HalfX: 2.2, HalfZ: 3.4, Falloff: 'Smooth', CentreY: 0.6 },
        Bearings: [['Bearing', 'Craft heading']],
        Parameters: [
            { Key: 'Strength',    Label: 'Wash gain',      Min: 0,   Max: 45,  Step: 0.2,  Digits: 1, Unit: 'm/s', ChamberMax: 20 },
            { Key: 'Speed',       Label: 'Craft speed',    Min: 0,   Max: 90,  Step: 0.5,  Digits: 1, Unit: 'm/s', ChamberMax: 30 },
            { Key: 'HalfWidth',   Label: 'Wake half-span', Min: 0.2, Max: 250, Step: 0.05, Digits: 2, Unit: 'm',   ChamberMin: 0.2, ChamberMax: 3 },
            { Key: 'Length',      Label: 'Wake length',    Min: 0.5, Max: 800, Step: 0.1,  Digits: 1, Unit: 'm',   ChamberMin: 0.8, ChamberMax: 8 },
            { Key: 'Circulation', Label: 'Tip vortex',     Min: 0,   Max: 20,  Step: 0.2,  Digits: 1, Unit: 'm/s', ChamberMax: 12 }
        ],
        Blurb: 'Counter-rotating wake pair and slipstream envelope trailing behind a moving body or rotor.'
    },
    {
        Id: 'Current', Native: 'CurrentVolume', Name: 'Current Volume', Contribution: 'Flow',
        Glyph: 'Current', Colour: '#66d9b8', Reach: 'Box',
        Defaults: { Strength: 12, Bearing: 90, Radius: 180, HalfX: 160, HalfZ: 120, Height: 140, Falloff: 'Linear' },
        ChamberDefaults: { Strength: 6.5, Bearing: 90, Radius: 3.0, HalfX: 2.6, HalfZ: 2.2, Height: 3.6, Falloff: 'Linear', CentreY: 0 },
        Bearings: [['Bearing', 'Flow direction']],
        Parameters: [
            { Key: 'Strength', Label: 'Flow rate', Min: 0, Max: 40, Step: 0.2, Digits: 1, Unit: 'm/s', ChamberMax: 20 }
        ],
        Blurb: 'Uniform steady laminar stream inside a bounded rectangular box zone.'
    },

    // ── ACCELERATE SOURCES (m/s²) ──
    {
        Id: 'Attractor', Native: 'PointAttractor', Name: 'Attractor', Contribution: 'Accelerate',
        Glyph: 'Attract', Colour: '#ff4757', Reach: 'Sphere',
        Defaults: { Strength: 14, Swirl: 4.5, SwallowRadius: 0, Radius: 220, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 9.5, Swirl: 4.8, SwallowRadius: 0.12, Radius: 2.8, Falloff: 'Smooth', CentreY: 1.6 },
        Bearings: [],
        Parameters: [
            { Key: 'Strength',      Label: 'Radial pull',    Min: 0, Max: 50, Step: 0.2,  Digits: 1, Unit: 'm/s²', ChamberMax: 30 },
            { Key: 'Swirl',         Label: 'Tangent swirl',  Min: -30, Max: 30, Step: 0.2, Digits: 1, Unit: 'm/s²', ChamberMin: -18, ChamberMax: 18 },
            { Key: 'SwallowRadius', Label: 'Swallow core',   Min: 0, Max: 40, Step: 0.02, Digits: 2, Unit: 'm',    ChamberMax: 2 }
        ],
        Blurb: 'Draws receivers inward toward the origin with optional azimuthal swirl — zero pull outside Radius.'
    },
    {
        Id: 'Repulsor', Native: 'PointRepulsor', Name: 'Repulsor', Contribution: 'Accelerate',
        Glyph: 'Repel', Colour: '#ff6b81', Reach: 'Sphere',
        Defaults: { Strength: 16, Radius: 190, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 11.5, Radius: 2.2, Falloff: 'Smooth', CentreY: 1.5 },
        Bearings: [],
        Parameters: [
            { Key: 'Strength', Label: 'Push outward', Min: 0, Max: 60, Step: 0.2, Digits: 1, Unit: 'm/s²', ChamberMax: 35 }
        ],
        Blurb: 'Bounded radial exclusion dome deflecting incoming particles cleanly outward.'
    },
    {
        Id: 'Orbit', Native: 'OrbitField', Name: 'Orbit Spin', Contribution: 'Accelerate',
        Glyph: 'Orbit', Colour: '#ffa502', Reach: 'Column',
        Defaults: { Strength: 12, Height: 260, Radius: 200, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 9.0, Height: 4.5, Radius: 2.8, Falloff: 'Smooth', CentreY: 0 },
        Bearings: [],
        Parameters: [
            { Key: 'Strength', Label: 'Tangential drive', Min: -40, Max: 40, Step: 0.2, Digits: 1, Unit: 'm/s²', ChamberMin: -25, ChamberMax: 25 }
        ],
        Blurb: 'Pure azimuthal orbital ring acceleration with zero radial inflow or outward drift.'
    },
    {
        Id: 'Lift', Native: 'LiftField', Name: 'Lift Column', Contribution: 'Accelerate',
        Glyph: 'Lift', Colour: '#ffd32a', Reach: 'Column',
        Defaults: { Strength: 11, Height: 300, Radius: 170, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 10.0, Height: 5.0, Radius: 2.4, Falloff: 'Smooth', CentreY: 0 },
        Bearings: [],
        Parameters: [
            { Key: 'Strength', Label: 'Upward buoyancy', Min: 0, Max: 40, Step: 0.2, Digits: 1, Unit: 'm/s²', ChamberMax: 25 }
        ],
        Blurb: 'Bounded vertical buoyancy chimney levitating particles inside its finite cylinder.'
    },
    {
        Id: 'Magnetic', Native: 'MagneticDipole', Name: 'Magnetic Dipole', Contribution: 'Accelerate',
        Glyph: 'Magnet', Colour: '#f7b731', Reach: 'Sphere',
        Defaults: { Strength: 12, Guide: 0, Radius: 210, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 10.0, Guide: 2.4, Radius: 2.8, Falloff: 'Smooth', CentreY: 1.6 },
        Bearings: [],
        Parameters: [
            { Key: 'Strength', Label: 'Dipole flux',      Min: 0, Max: 40, Step: 0.2, Digits: 1, Unit: 'm/s²', ChamberMax: 25 },
            { Key: 'Guide',    Label: 'Field-line guide', Min: 0, Max: 12, Step: 0.1, Digits: 1, Unit: '1/s',  ChamberMax: 12 }
        ],
        Blurb: 'Toroidal B-field flux lobes guiding particles along arched dipole field lines.'
    },
    {
        Id: 'Gravity', Native: 'GravityZone', Name: 'Gravity Well', Contribution: 'Accelerate',
        Glyph: 'Gravity', Colour: '#ff7f50', Reach: 'Column',
        Defaults: { Strength: 9.81, Height: 300, Radius: 220, Falloff: 'Smooth' },
        ChamberDefaults: { Strength: 9.81, Height: 5.0, Radius: 3.0, Falloff: 'Smooth', CentreY: 0 },
        Bearings: [],
        Parameters: [
            { Key: 'Strength', Label: 'Downward pull', Min: 0, Max: 40, Step: 0.1, Digits: 2, Unit: 'm/s²', ChamberMax: 25 }
        ],
        Blurb: 'Local downward gravity override sphere/column — strictly zero outside bounds.'
    },

    // ── DAMP SOURCES (1/s) ──
    {
        Id: 'Drag', Native: 'DragVolume', Name: 'Drag Volume', Contribution: 'Damp',
        Glyph: 'Damp', Colour: '#b9a3e8', Reach: 'Box',
        Defaults: { Strength: 2.2, Bearing: 0, HalfX: 140, HalfZ: 140, Height: 180, Radius: 180, Falloff: 'Linear' },
        ChamberDefaults: { Strength: 2.8, Bearing: 15, HalfX: 2.0, HalfZ: 2.4, Height: 4.0, Radius: 2.6, Falloff: 'Linear', CentreY: 0 },
        Bearings: [['Bearing', 'Zone orientation']],
        Parameters: [
            { Key: 'Strength', Label: 'Damping rate σ', Min: 0, Max: 12, Step: 0.1, Digits: 1, Unit: '1/s' }
        ],
        Blurb: 'Viscous braking volume scaling particle velocity by exp(−σ·dt) inside box bounds.'
    }
];

export const TypeById = Object.fromEntries(Types.map((T) => [T.Id, T]));
export const ResolveSourceColour = (SourceOrType) =>
{
    const Id = typeof SourceOrType === 'string' ? SourceOrType : SourceOrType?.Type;
    const T = TypeById[Id];
    return T ? T.Colour : '#8fd3c4';
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 CONSTRUCTION & PRESETS
//------------------------------------------------------------------------------------------------------------------------

let NextTokenId = 1;

export function ConstructSource(TypeId = 'WindGenerator', Overrides = {}, ChamberScale = true)
{
    const Type = TypeById[TypeId] || TypeById.WindGenerator;
    const Defaults = (ChamberScale && Type.ChamberDefaults) ? Type.ChamberDefaults : Type.Defaults;
    const Source = Object.assign({
        Token: `src_${NextTokenId++}`,
        Type: Type.Id,
        Name: Type.Name,
        Enabled: true,
        Centre: [0, Defaults.CentreY ?? 0, 0],
        Reach: Type.Reach === 'Everywhere' ? 'Sphere' : Type.Reach,
        Radius: Defaults.Radius ?? (ChamberScale ? 3.0 : 180),
        Height: Defaults.Height ?? (ChamberScale ? 4.5 : 180),
        HalfX: Defaults.HalfX ?? (ChamberScale ? 2.2 : 120),
        HalfZ: Defaults.HalfZ ?? (ChamberScale ? 2.2 : 120),
        Falloff: Defaults.Falloff || 'Smooth',
        Activation: 'Always',
        Begins: 0,
        Lasts: 0,
        Repeats: 0,
        RampIn: 1.0,
        RampOut: 1.5,
        TriggeredAt: null,
        Acts: [],
        UseFormula: false,
        FormulaCode: DEFAULT_FORMULA_CODE
    }, JSON.parse(JSON.stringify(Defaults)));
    delete Source.CentreY;
    if (Overrides) Object.assign(Source, Overrides);
    if (Source.Reach === 'Everywhere') Source.Reach = 'Sphere';
    if (!Number.isFinite(Source.Radius) || Source.Radius <= 0) Source.Radius = Defaults.Radius || (ChamberScale ? 3.0 : 200);
    return Source;
}

export function ConstructChamberScenario(ScenarioKey = 'lab')
{
    const BaseField = {
        Enabled: true, IncludeTurbulence: true, Visualize: true, GridResolution: 5, ArrowSeconds: 0.5,
        RegionCentre: [0, 0, 0], RegionHalfExtents: [6, 4, 6],
        Cascades: 3, FinestCell: 0.5, CellsPerSide: 24, VerticalCells: 12, Extent: 12,
        Channels: Channels.map(([, Key]) => Key)
    };
    const BaseViz = {
        Contribution: 'Flow', Quantity: 'Speed', Mode: 'Camera', ScaleMode: 'Automatic', FixedScale: 12,
        Heatmap: true, HeatmapStyle: 'Smooth', Opacity: 0.34,
        Arrows: false, ArrowSpacing: 42, QuietCutoff: 0.02,
        FlowLines: false, FlowLineSpacing: 34, FlowLineLength: 26, FlowLinePulse: false,
        Tracers: false, TracerCount: 160,
        Cascades: false, Labels: false, MinimalLabels: true, ShowInfluence: true, SliceHeight: 1.5, CellPixels: 6, Solo: null, Follow: null
    };

    if (ScenarioKey === 'blank')
    {
        return {
            Scenario: 'blank',
            Wind: {
                Enabled: false, Bounded: true, RegionRadius: 8.5,
                Bearing: 68, Speed: 0, GustStrength: 0, GustFrequency: 0.14, Shear: 0.14, Veer: 6,
                Turbulence: 0, TurbulenceScale: 2.2, Seed: 7331, GroundHeight: 0, MesoscaleScale: 16, MesoscaleStrength: 0,
                AdvectClouds: true, AdvectPlumes: true, AdvectParticles: true
            },
            Field: Object.assign({}, BaseField, { Visualize: false }),
            Sources: [],
            Visualization: Object.assign({}, BaseViz, {
                Heatmap: false, Arrows: false, FlowLines: false, Tracers: false, ShowInfluence: false
            }),
            Timeline: { Duration: 30, Rate: 1, Loop: true }
        };
    }

    if (ScenarioKey === 'storm')
    {
        return {
            Scenario: 'storm',
            Wind: {
                Enabled: true, Bounded: true, RegionRadius: 9,
                Bearing: 52, Speed: 4.5, GustStrength: 0.3, GustFrequency: 0.16, Shear: 0.18, Veer: 12,
                Turbulence: 0.5, TurbulenceScale: 2.4, Seed: 7331, GroundHeight: 0, MesoscaleScale: 16, MesoscaleStrength: 0.18,
                AdvectClouds: true, AdvectPlumes: true, AdvectParticles: true
            },
            Field: BaseField,
            Sources: [
                ConstructSource('Tornado',    { Name: 'Mesocyclone Vortex', Centre: [-0.8, 0, -0.4], Strength: 16, InnerRadius: 0.9, CollarRadius: 1.9, Inflow: 5.5, Updraft: 7.5, Height: 6.0, Radius: 3.8, TrackSpeed: 0 }),
                ConstructSource('GustFront',  { Name: 'Squall Surge',       Centre: [-3.2, 0, 0.2],  Strength: 11, FrontSpeed: 0.9, CrestDepth: 2.2, HalfWidth: 4.2, Bearing: 88, Radius: 4.0, HalfX: 3.8, HalfZ: 2.6 }),
                ConstructSource('Microburst', { Name: 'Storm Downdraft',    Centre: [2.6, 3.8, -1.8], Strength: 12, Downdraft: 9.5, InnerRadius: 0.85, RingRadius: 1.9, Radius: 3.4 }),
                ConstructSource('Blast',      { Name: 'Lightning Shockwave', Centre: [0.5, 0.8, 2.2], Strength: 20, InitialRadius: 0.35, Expansion: 2.8, Thickness: 0.7, Radius: 3.4, Begins: 2, Lasts: 4, Repeats: 12, Activation: 'Window' }),
                ConstructSource('Attractor',  { Name: 'Storm Eye Pull',     Centre: [0.0, 1.8, 0.0], Strength: 6.5, Swirl: 3.5, Radius: 2.8 })
            ],
            Visualization: Object.assign({}, BaseViz, { Heatmap: true, Opacity: 0.38 }),
            Timeline: { Duration: 30, Rate: 1, Loop: true }
        };
    }

    if (ScenarioKey === 'corridor')
    {
        return {
            Scenario: 'corridor',
            Wind: {
                Enabled: false, Bounded: true, RegionRadius: 9,
                Bearing: 60, Speed: 3.5, GustStrength: 0.25, GustFrequency: 0.15, Shear: 0.15, Veer: 8,
                Turbulence: 0.38, TurbulenceScale: 2.2, Seed: 7331, GroundHeight: 0, MesoscaleScale: 16, MesoscaleStrength: 0.16,
                AdvectClouds: true, AdvectPlumes: true, AdvectParticles: true
            },
            Field: BaseField,
            Sources: [
                ConstructSource('WindGenerator', { Name: 'West Intake Blower',   Centre: [-3.2, 0, 0.2],   Bearing: 88, Strength: 9.5, Radius: 3.6, Height: 5.5, Reach: 'Column' }),
                ConstructSource('Jet',           { Name: 'Crosswind Jet',        Centre: [-0.4, 1.5, -2.8], Bearing: 165, Length: 4.5, InnerRadius: 0.85, Strength: 12, Radius: 3.4 }),
                ConstructSource('Thermal',       { Name: 'Exhaust Plume',        Centre: [1.8, 0, 1.4],     Strength: 9.0, InnerRadius: 0.8, Inflow: 3.4, Height: 5.8, Radius: 3.0 }),
                ConstructSource('Drag',          { Name: 'Baffle Damp Screen',   Centre: [0.0, 0, 0.4],     HalfX: 1.4, HalfZ: 2.2, Radius: 2.4, Bearing: 14, Strength: 2.8 })
            ],
            Visualization: Object.assign({}, BaseViz, { Opacity: 0.36 }),
            Timeline: { Duration: 30, Rate: 1, Loop: true }
        };
    }

    // Default ('lab'): Bounded ForceFieldVisualization studio forces mapped cleanly inside 12×8×12 m particle chamber
    return {
        Scenario: 'lab',
        Wind: {
            Enabled: true, Bounded: true, RegionRadius: 8.5,
            Bearing: 68, Speed: 2.2, GustStrength: 0.22, GustFrequency: 0.14, Shear: 0.14, Veer: 6,
            Turbulence: 0.36, TurbulenceScale: 2.2, Seed: 7331, GroundHeight: 0, MesoscaleScale: 16, MesoscaleStrength: 0.18,
            AdvectClouds: true, AdvectPlumes: true, AdvectParticles: true
        },
        Field: BaseField,
        Sources: [
            ConstructSource('WindGenerator', { Name: 'Wind Generator East', Centre: [-2.8, 0, 0.5],   Bearing: 72, Strength: 8.5, Radius: 3.6, Height: 5.5, Reach: 'Column' }),
            ConstructSource('Tornado',       { Name: 'Vortex Column',       Centre: [0.4, 0, -0.3],   Strength: 14.5, InnerRadius: 0.85, CollarRadius: 1.75, Inflow: 4.5, Updraft: 6.5, Height: 5.5, Radius: 3.5, TrackSpeed: 0 }),
            ConstructSource('Jet',           { Name: 'Jet Corridor',        Centre: [-1.4, 1.5, -2.5], Bearing: 105, Length: 4.2, InnerRadius: 0.8, Strength: 11.5, Radius: 3.2 }),
            ConstructSource('Attractor',     { Name: 'Attractor',           Centre: [2.8, 1.5, 1.6],  Strength: 9.5, Swirl: 4.8, Radius: 2.8 }),
            ConstructSource('Repulsor',      { Name: 'Repulsor',            Centre: [2.1, 1.5, -2.0], Strength: 11.5, Radius: 2.2 }),
            ConstructSource('Blast',         { Name: 'Impulse Shell',       Centre: [-0.4, 0.8, 2.4], Strength: 18.0, InitialRadius: 0.35, Expansion: 2.6, Thickness: 0.7, Radius: 3.2, Begins: 2, Lasts: 4, Repeats: 10, Activation: 'Window' })
        ],
        Visualization: Object.assign({}, BaseViz),
        Timeline: { Duration: 30, Rate: 1, Loop: true }
    };
}

export const ConstructScenario = ConstructChamberScenario;
export const ConstructDefaultField = () => ConstructChamberScenario('lab');

//------------------------------------------------------------------------------------------------------------------------
//                                                  WIND EVALUATION
//------------------------------------------------------------------------------------------------------------------------

function Hash(ColumnIndex, RowIndex, Seed)
{
    let Mixed = Math.imul(ColumnIndex, 374761393) ^ Math.imul(RowIndex, 668265263) ^ Math.imul(Seed, 1274126177);
    Mixed = Math.imul(Mixed ^ (Mixed >>> 13), 1274126177);
    return (Mixed ^ (Mixed >>> 16)) >>> 0;
}

const GradientDirections = new Float64Array(32);
for (let Index = 0; Index < 16; ++Index)
{
    GradientDirections[Index * 2] = Math.cos((Index + 0.5) / 16 * Math.PI * 2);
    GradientDirections[Index * 2 + 1] = Math.sin((Index + 0.5) / 16 * Math.PI * 2);
}

function NoiseGradient(U, V, Seed, Gradient)
{
    const ColumnIndex = Math.floor(U), RowIndex = Math.floor(V);
    const X = U - ColumnIndex, Y = V - RowIndex;
    const FadeX = X * X * X * (X * (X * 6 - 15) + 10), FadeY = Y * Y * Y * (Y * (Y * 6 - 15) + 10);
    const SlopeX = 30 * X * X * (X * (X - 2) + 1), SlopeY = 30 * Y * Y * (Y * (Y - 2) + 1);
    const A = (Hash(ColumnIndex, RowIndex, Seed) & 15) * 2, B = (Hash(ColumnIndex + 1, RowIndex, Seed) & 15) * 2;
    const C = (Hash(ColumnIndex, RowIndex + 1, Seed) & 15) * 2, D = (Hash(ColumnIndex + 1, RowIndex + 1, Seed) & 15) * 2;
    const AX = GradientDirections[A], AY = GradientDirections[A + 1], BX = GradientDirections[B], BY = GradientDirections[B + 1];
    const CX = GradientDirections[C], CY = GradientDirections[C + 1], DX = GradientDirections[D], DY = GradientDirections[D + 1];
    const N00 = AX * X + AY * Y, N10 = BX * (X - 1) + BY * Y, N01 = CX * X + CY * (Y - 1), N11 = DX * (X - 1) + DY * (Y - 1);
    const Cross = N00 - N10 - N01 + N11;
    const Reading = N00 + FadeX * (N10 - N00) + FadeY * (N01 - N00) + FadeX * FadeY * Cross;
    Gradient[0] = (AX + FadeX * (BX - AX) + FadeY * (CX - AX) + FadeX * FadeY * (AX - BX - CX + DX) + SlopeX * (N10 - N00 + FadeY * Cross)) * 1.41;
    Gradient[1] = (AY + FadeX * (BY - AY) + FadeY * (CY - AY) + FadeX * FadeY * (AY - BY - CY + DY) + SlopeY * (N01 - N00 + FadeX * Cross)) * 1.41;
    return Reading * 1.41;
}

const Resolvable = (Wavelength, CellSpan) => Smooth01((Wavelength / Math.max(1e-6, CellSpan) - 2) / 4);
const NoiseGradientStore = new Float64Array(2);

export function EvaluateWind(Wind, Settings, X, Y, Z, Time, CellSpan, Out)
{
    if (!Wind.Enabled || Wind.Speed <= 0) return;
    let Influence = 1;
    if (Wind.Bounded && Wind.RegionRadius > 0)
    {
        const Dist = Math.hypot(X, Z);
        Influence = Falls('Smooth', Dist, Wind.RegionRadius);
        if (Influence <= 0) return;
    }
    const AltitudeKm = Math.max(0, Y - Wind.GroundHeight) / 1000;
    let Bearing = Wind.Bearing + Wind.Veer * AltitudeKm;
    let SpeedGain = Math.max(0.15, 1 + Wind.Shear * Math.min(AltitudeKm, 12));
    const Drift = 0.35 * Wind.Speed * Time;
    const HeadingX = Math.sin(Wind.Bearing * Radian), HeadingZ = -Math.cos(Wind.Bearing * Radian);
    if (Wind.MesoscaleStrength > 0 && Wind.MesoscaleScale > 0)
    {
        const Visible = Resolvable(Wind.MesoscaleScale * 2, CellSpan);
        if (Visible > 0)
        {
            const U = (X - HeadingX * Drift) / Wind.MesoscaleScale, V = (Z - HeadingZ * Drift) / Wind.MesoscaleScale;
            const SpeedShift = NoiseGradient(U, V, Wind.Seed + 11, NoiseGradientStore);
            const TurnShift = NoiseGradient(U + 37.1, V - 19.7, Wind.Seed + 23, NoiseGradientStore);
            SpeedGain *= 1 + Wind.MesoscaleStrength * SpeedShift * Visible;
            Bearing += Wind.MesoscaleStrength * 55 * TurnShift * Visible;
        }
    }
    const DirectionX = Math.sin(Bearing * Radian), DirectionZ = -Math.cos(Bearing * Radian);
    const Along = X * DirectionX + Z * DirectionZ;
    const Phase = 2 * Math.PI * Wind.GustFrequency * Time - 0.14 * Along;
    const GustWave = 0.62 * Math.sin(Phase) + 0.38 * Math.sin(2.17 * Phase + 1.13);
    const Speed = Wind.Speed * SpeedGain * (1 + Wind.GustStrength * GustWave) * Influence;
    Out[0] += DirectionX * Speed;
    Out[2] += DirectionZ * Speed;
    if (Settings.IncludeTurbulence && Wind.Turbulence > 0)
    {
        const Scale = Math.max(0.4, Wind.TurbulenceScale);
        const Visible = Math.max(0.25, Resolvable(Scale * 2, CellSpan));
        const U = (X - HeadingX * Drift) / Scale, V = (Z - HeadingZ * Drift) / Scale;
        const Gain = Wind.Turbulence * Wind.Speed * 0.45 * Visible * Influence;
        NoiseGradient(U, V + Y / Scale * 0.37, Wind.Seed, NoiseGradientStore);
        Out[0] += NoiseGradientStore[1] * Gain;
        Out[2] -= NoiseGradientStore[0] * Gain;
        Out[1] += NoiseGradient(U + 71.3, V + 13.9, Wind.Seed + 5, NoiseGradientStore) * Gain * 0.2;
    }
}

export function WindSpeedAt(Wind, Settings, X, Y, Z, Time)
{
    const Out = new Float64Array(3);
    EvaluateWind(Wind, Settings, X, Y, Z, Time, 0.25, Out);
    return Math.hypot(Out[0], Out[1], Out[2]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 ACTIVATION
//------------------------------------------------------------------------------------------------------------------------

export function ResolveActivation(Source, Time)
{
    if (!Source.Enabled) return { Envelope: 0, Age: 0, Status: 'Off' };
    if (Source.Activation === 'Always') return { Envelope: 1, Age: Time, Status: 'Active' };
    let Start = Source.Begins;
    if (Source.Activation === 'Trigger')
    {
        if (Source.TriggeredAt === null || Source.TriggeredAt === undefined) return { Envelope: 0, Age: 0, Status: 'Armed' };
        Start = Source.TriggeredAt;
    }
    let Local = Time - Start;
    if (Local < 0) return { Envelope: 0, Age: 0, Status: 'Waiting' };
    if (Source.Activation === 'Window' && Source.Repeats > 0 && Source.Lasts > 0) Local %= Source.Repeats;
    if (Source.Lasts > 0 && Local > Source.Lasts) return { Envelope: 0, Age: Local, Status: Source.Activation === 'Window' && Source.Repeats > 0 ? 'Waiting' : 'Spent' };
    let Envelope = Source.RampIn > 1e-4 ? Smooth01(Local / Source.RampIn) : 1;
    if (Source.Lasts > 0 && Source.RampOut > 1e-4) Envelope *= 1 - Smooth01((Local - (Source.Lasts - Source.RampOut)) / Source.RampOut);
    return { Envelope, Age: Local, Status: Envelope > 1e-3 ? 'Active' : 'Waiting' };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 SOURCE EVALUATION
//------------------------------------------------------------------------------------------------------------------------

export function ResolveCentre(Source, Wind, Age)
{
    if (Source.Type === 'Tornado' && Source.TrackSpeed > 0)
    {
        const Turn = Source.TrackBearing * Radian, Travel = Source.TrackSpeed * Age;
        return [Source.Centre[0] + Math.sin(Turn) * Travel, Source.Centre[1], Source.Centre[2] - Math.cos(Turn) * Travel];
    }
    if (Source.Type === 'Puff' && Source.Drift > 0 && Wind?.Enabled)
    {
        const AltitudeKm = Math.max(0, Source.Centre[1] - Wind.GroundHeight) / 1000;
        const Turn = (Wind.Bearing + Wind.Veer * AltitudeKm) * Radian;
        const Travel = Wind.Speed * Math.max(0.15, 1 + Wind.Shear * Math.min(AltitudeKm, 12)) * Source.Drift * Age;
        return [Source.Centre[0] + Math.sin(Turn) * Travel, Source.Centre[1], Source.Centre[2] - Math.cos(Turn) * Travel];
    }
    if (Source.Type === 'GustFront' && Source.FrontSpeed > 0)
    {
        const Turn = (Source.Bearing || 0) * Radian, Travel = (Source.FrontSpeed * Age) % Math.max(8, (Source.Radius || 4) * 3);
        return [Source.Centre[0] + Math.sin(Turn) * Travel, Source.Centre[1], Source.Centre[2] - Math.cos(Turn) * Travel];
    }
    return Source.Centre;
}

export function ResolveBoxExtents(Source)
{
    if (Source.Type === 'Jet') return { HalfX: Source.InnerRadius * 2.6, HalfZ: Source.Length * 0.75, Height: Source.Centre[1] + Source.InnerRadius * 2.6, OffsetZ: Source.Length * 0.5 };
    if (Source.Type === 'Wake') return { HalfX: Source.HalfWidth * 3.0, HalfZ: Source.Length * 1.15, Height: Source.Height * 2.0, OffsetZ: -Source.Length };
    if (Source.Type === 'GustFront') return { HalfX: Source.HalfWidth * 1.1, HalfZ: Source.CrestDepth * 1.4, Height: Source.Height * 1.25, OffsetZ: -Source.CrestDepth * 0.85 };
    return { HalfX: Math.max(0.25, Source.HalfX), HalfZ: Math.max(0.25, Source.HalfZ), Height: Math.max(0.5, Source.Height), OffsetZ: 0 };
}

export function ResolveReach(Source, Centre, X, Y, Z, Ground)
{
    const DeltaX = X - Centre[0], DeltaZ = Z - Centre[2];
    const ColumnFoot = Source.Type === 'Microburst' ? (Ground ?? 0) : Centre[1];
    const DeltaY = Y - ColumnFoot;
    if (Source.Reach === 'Sphere' || Source.Reach === 'Everywhere')
    {
        const Separation = Math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
        return Separation >= Source.Radius ? 0 : Falls(Source.Falloff, Separation, Source.Radius);
    }
    if (Source.Reach === 'Column')
    {
        const Height = Math.max(0.5, Source.Type === 'Microburst' ? Math.max(Source.Height, (Centre[1] - ColumnFoot) * 1.4) : Source.Height);
        if (DeltaY < -Math.min(15, Height * 0.5) || DeltaY > Height * 1.3) return 0;
        const Separation = Math.hypot(DeltaX, DeltaZ);
        if (Separation >= Source.Radius) return 0;
        const Taper = DeltaY <= Height ? 1 : 1 - Smooth01((DeltaY - Height) / (0.3 * Height));
        return Falls(Source.Falloff, Separation, Source.Radius) * Taper;
    }
    const Turn = (Source.Bearing || 0) * Radian;
    const Cosine = Math.cos(Turn), Sine = Math.sin(Turn);
    const Extents = ResolveBoxExtents(Source);
    const LocalX = DeltaX * Cosine + DeltaZ * Sine, LocalZ = -DeltaX * Sine + DeltaZ * Cosine + Extents.OffsetZ;
    const Share = Math.max(Math.abs(LocalX) / Extents.HalfX, Math.abs(LocalZ) / Extents.HalfZ, DeltaY < 0 ? -DeltaY / Math.max(1.5, Extents.Height * 0.5) : DeltaY / Math.max(1, Extents.Height));
    return Share >= 1 ? 0 : Falls(Source.Falloff === 'None' ? 'None' : Source.Falloff, Share, 1);
}

function EvaluateFlowSource(Source, Wind, X, Y, Z, Age, Envelope, Centre, Out, Time)
{
    const Ground = Wind.GroundHeight ?? 0;
    switch (Source.Type)
    {
    case 'WindGenerator':
    {
        const Turn = (Source.Bearing || 0) * Radian;
        const DirX = Math.sin(Turn), DirZ = -Math.cos(Turn);
        const Along = (X - Centre[0]) * DirX + (Z - Centre[2]) * DirZ;
        const WaveK = Source.Radius > 25 ? 0.018 : 0.45;
        const Phase = 2 * Math.PI * (Source.GustFrequency || 0.14) * Time - WaveK * Along;
        const Gust = 0.65 * Math.sin(Phase) + 0.35 * Math.sin(2.1 * Phase + 0.9);
        const Speed = Source.Strength * Envelope * (1 + (Source.GustStrength || 0) * Gust);
        Out[0] += DirX * Speed;
        Out[2] += DirZ * Speed;
        if ((Source.Turbulence || 0) > 0)
        {
            const Scale = Math.max(0.4, Source.TurbulenceScale || 2.2);
            const Drift = 0.4 * Source.Strength * Time;
            const U = (X - DirX * Drift) / Scale, V = (Z - DirZ * Drift) / Scale;
            const Gain = Source.Turbulence * Source.Strength * 0.42 * Envelope;
            NoiseGradient(U, V + Y / Scale * 0.35, 3109, NoiseGradientStore);
            Out[0] += NoiseGradientStore[1] * Gain;
            Out[2] -= NoiseGradientStore[0] * Gain;
            Out[1] += NoiseGradient(U + 21.7, V - 14.3, 3113, NoiseGradientStore) * Gain * 0.22;
        }
        return;
    }
    case 'Tornado':
    {
        const RelativeY = Y - Centre[1];
        const SpanH = Math.max(1.5, Source.Height);
        if (RelativeY < -Math.min(15, SpanH * 0.5) || RelativeY > SpanH * 1.3) return;
        const HeightShare = Clamp(RelativeY / SpanH, 0, 1);
        const TopTaper = RelativeY <= SpanH ? 1 : 1 - Smooth01((RelativeY - SpanH) / (0.3 * SpanH));
        const DeltaX = X - Centre[0], DeltaZ = Z - Centre[2];
        const Separation = Math.hypot(DeltaX, DeltaZ);
        const InnerRadius = Math.max(0.15, Source.InnerRadius + (Source.CollarRadius - Source.InnerRadius) * Smooth01(HeightShare));
        const Rho = Separation / InnerRadius;
        const Profile = (2 * Rho) / (1 + Rho * Rho);
        const Tangential = Envelope * TopTaper * Source.Strength * Profile * (Source.Spin < 0 ? -1 : 1);
        const InflowBand = Math.exp(-Math.max(0, RelativeY) / Math.max(1.0, 0.25 * SpanH)) - 0.35 * Smooth01((HeightShare - 0.72) / 0.28);
        const Radial = -Envelope * TopTaper * Source.Inflow * InflowBand * Profile;
        const Updraft = Envelope * TopTaper * Source.Updraft * Math.exp(-0.42 * Rho * Rho) * Smooth01(RelativeY / Math.max(1.0, SpanH * 0.22) + 0.25);
        const UnitX = Separation > 1e-5 ? DeltaX / Separation : 1, UnitZ = Separation > 1e-5 ? DeltaZ / Separation : 0;
        const Turn = (Source.TrackBearing || 0) * Radian, Carry = Envelope * 0.25 * (Source.TrackSpeed || 0);
        Out[0] += UnitZ * Tangential + UnitX * Radial + Math.sin(Turn) * Carry;
        Out[1] += Updraft;
        Out[2] += -UnitX * Tangential + UnitZ * Radial - Math.cos(Turn) * Carry;
        return;
    }
    case 'GustFront':
    {
        const Turn = (Source.Bearing || 0) * Radian, PropagationX = Math.sin(Turn), PropagationZ = -Math.cos(Turn);
        const Behind = (Centre[0] - X) * PropagationX + (Centre[2] - Z) * PropagationZ;
        const Lateral = Math.abs((X - Centre[0]) * -PropagationZ + (Z - Centre[2]) * PropagationX);
        const Depth = Math.max(0.4, Source.CrestDepth), RelativeY = Y - Centre[1];
        if (Behind < -0.25 * Depth || Behind > 2.4 * Depth || RelativeY < 0 || RelativeY > Source.Height * 1.25 || Lateral > Source.HalfWidth * 1.2) return;
        const NoseRise = Smooth01((Behind + 0.25 * Depth) / (0.35 * Depth));
        const WakeDecay = Math.exp(-Math.max(0, Behind - 0.1 * Depth) / Depth);
        const HeightEnvelope = 1 - Smooth01(RelativeY / Math.max(0.5, Source.Height));
        const LateralEnvelope = 1 - Smooth01((Lateral - Source.HalfWidth * 0.8) / (Source.HalfWidth * 0.4));
        const Surge = Envelope * NoseRise * WakeDecay * HeightEnvelope * LateralEnvelope;
        const NoseLift = Envelope * NoseRise * Math.exp(-Math.abs(Behind) / (0.28 * Depth)) * HeightEnvelope * LateralEnvelope * 0.28 * Source.Strength;
        Out[0] += PropagationX * Source.Strength * Surge;
        Out[1] += NoseLift;
        Out[2] += PropagationZ * Source.Strength * Surge;
        return;
    }
    case 'Microburst':
    {
        const DeltaX = X - Centre[0], DeltaZ = Z - Centre[2];
        const Separation = Math.hypot(DeltaX, DeltaZ), RelativeY = Y - Ground;
        const ColumnRadius = Math.max(0.25, Source.InnerRadius), Ring = Math.max(0.5, Source.RingRadius);
        const Ceiling = Math.max(2.5, Centre[1] * 1.4, Source.Height || 4);
        if (RelativeY < 0 || RelativeY > Ceiling) return;
        const SoftFloor = Math.min(35, Math.max(0.8, Ceiling * 0.22));
        const DowndraftColumn = Math.exp(-(Separation * Separation) / (ColumnRadius * ColumnRadius)) * Smooth01(RelativeY / SoftFloor);
        const OutRing = (Separation / Ring) * Math.exp(-0.5 * Math.pow(Separation / Ring, 2)) * Math.exp(-RelativeY / Math.max(1.2, Ring * 0.55));
        const Inverse = Separation > 1e-4 ? 1 / Separation : 0;
        Out[0] += Envelope * Source.Strength * OutRing * DeltaX * Inverse;
        Out[1] -= Envelope * Source.Downdraft * DowndraftColumn;
        Out[2] += Envelope * Source.Strength * OutRing * DeltaZ * Inverse;
        return;
    }
    case 'Thermal':
    {
        const RelativeY = Y - Centre[1], ColumnHeight = Math.max(1.5, Source.Height);
        if (RelativeY < 0 || RelativeY > ColumnHeight * 1.35) return;
        const HeightShare = Clamp(RelativeY / ColumnHeight, 0, 1.35);
        const DeltaX = X - Centre[0], DeltaZ = Z - Centre[2], Separation = Math.hypot(DeltaX, DeltaZ);
        const PlumeRadius = Math.max(0.2, Source.InnerRadius * (1 + 0.45 * HeightShare));
        const Rho = Separation / PlumeRadius;
        if (Rho >= 3.5) return;
        const SoftBase = Math.min(10, Math.max(0.6, ColumnHeight * 0.18));
        const ChimneyEnvelope = Envelope * Math.exp(-0.5 * Rho * Rho) * Smooth01(RelativeY / SoftBase + 0.2) * (1 - Smooth01((HeightShare - 1) / 0.35));
        const RadialMode = -Smooth01(0.35 - HeightShare) + 0.55 * Smooth01((HeightShare - 0.72) / 0.28);
        const Inverse = Separation > 1e-4 ? 1 / Separation : 0;
        const Radial = Envelope * Source.Inflow * RadialMode * (2 * Rho / (1 + Rho * Rho));
        Out[0] += DeltaX * Inverse * Radial;
        Out[1] += Source.Strength * ChimneyEnvelope;
        Out[2] += DeltaZ * Inverse * Radial;
        return;
    }
    case 'Jet':
    {
        const Turn = (Source.Bearing || 0) * Radian, AxisX = Math.sin(Turn), AxisZ = -Math.cos(Turn);
        const RelativeX = X - Centre[0], RelativeY = Y - Centre[1], RelativeZ = Z - Centre[2];
        const Axial = RelativeX * AxisX + RelativeZ * AxisZ;
        const Length = Math.max(0.5, Source.Length), CorridorRadius = Math.max(0.2, Source.InnerRadius);
        if (Axial < -0.2 * CorridorRadius || Axial > 1.25 * Length) return;
        const PerpendicularX = RelativeX - AxisX * Axial, PerpendicularZ = RelativeZ - AxisZ * Axial;
        const RadialSquared = PerpendicularX * PerpendicularX + RelativeY * RelativeY + PerpendicularZ * PerpendicularZ;
        const JetRadius = CorridorRadius * (1 + 0.35 * Math.max(0, Axial) / Length);
        if (RadialSquared >= 9 * JetRadius * JetRadius) return;
        const AxialEnvelope = Smooth01((Axial + 0.2 * CorridorRadius) / (0.4 * CorridorRadius)) * (1 - Smooth01((Axial - 0.85 * Length) / (0.4 * Length)));
        const Gain = Envelope * AxialEnvelope * Math.exp(-0.5 * RadialSquared / (JetRadius * JetRadius)) * Source.Strength;
        Out[0] += AxisX * Gain;
        Out[2] += AxisZ * Gain;
        return;
    }
    case 'Puff':
    {
        const RadiusXZ = Math.max(0.3, Source.Radius), RadiusY = Math.max(0.3, Source.RadiusY);
        const U = (X - Centre[0]) / RadiusXZ, V = (Y - Centre[1]) / RadiusY, W = (Z - Centre[2]) / RadiusXZ;
        const Squared = U * U + V * V + W * W;
        if (Squared >= 9) return;
        const Gauss = Envelope * Math.exp(-0.5 * Squared);
        const Turn = (Source.Bearing || 0) * Radian;
        Out[0] += Math.sin(Turn) * Source.Strength * Gauss;
        Out[1] += Source.Lift * Gauss;
        Out[2] += -Math.cos(Turn) * Source.Strength * Gauss;
        return;
    }
    case 'Blast':
    {
        const InitialRadius = Math.max(0.15, Source.InitialRadius);
        const FrontRadius = InitialRadius + Math.max(0, Source.Expansion) * Age;
        const Thickness = Math.max(0.15, Source.Thickness);
        const DeltaX = X - Centre[0], DeltaY = Y - Centre[1], DeltaZ = Z - Centre[2];
        const Separation = Math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
        const FrontShare = (Separation - FrontRadius) / Thickness;
        if (Math.abs(FrontShare) >= 3.2 || Separation < 1e-4) return;
        const Attenuation = Math.pow(InitialRadius / Math.max(InitialRadius, FrontRadius), 0.55);
        const Outward = Source.Strength * Envelope * Attenuation * Math.exp(-0.5 * FrontShare * FrontShare) / Separation;
        Out[0] += DeltaX * Outward;
        Out[1] += DeltaY * Outward;
        Out[2] += DeltaZ * Outward;
        return;
    }
    case 'Wake':
    {
        const Turn = (Source.Bearing || 0) * Radian, ForwardX = Math.sin(Turn), ForwardZ = -Math.cos(Turn);
        const RightX = -ForwardZ, RightZ = ForwardX;
        const RelativeX = X - Centre[0], RelativeY = Y - Ground, RelativeZ = Z - Centre[2];
        const Trail = -(RelativeX * ForwardX + RelativeZ * ForwardZ);
        const LateralDistance = RelativeX * RightX + RelativeZ * RightZ;
        const WakeLength = Math.max(0.8, Source.Length), HalfWidth = Math.max(0.2, Source.HalfWidth), WakeHeight = Math.max(0.4, Source.Height);
        if (Trail < -0.5 * HalfWidth || Trail > 2.2 * WakeLength || RelativeY < 0 || RelativeY > 2.4 * WakeHeight) return;
        const Spread = 1 + 0.85 * Math.max(0, Trail) / WakeLength;
        const LongEnvelope = Smooth01((Trail + 0.5 * HalfWidth) / HalfWidth) * Math.exp(-Math.max(0, Trail) / WakeLength);
        const LateralShare = LateralDistance / (HalfWidth * Spread);
        const Weight = Envelope * LongEnvelope * Math.exp(-0.5 * LateralShare * LateralShare) * (1 - Smooth01(RelativeY / (1.8 * WakeHeight * Spread)));
        const Upwash = Math.max(0, (1 - 0.8 * LateralShare * LateralShare) * Source.Circulation);
        const InwardRoll = -LateralShare * 0.65 * Source.Circulation;
        const Tow = Source.Strength * Math.min(1, Source.Speed / 12);
        Out[0] += Weight * (ForwardX * Tow + RightX * InwardRoll);
        Out[1] += Weight * Upwash;
        Out[2] += Weight * (ForwardZ * Tow + RightZ * InwardRoll);
        return;
    }
    case 'Current':
    {
        const Turn = (Source.Bearing || 0) * Radian;
        Out[0] += Math.sin(Turn) * Source.Strength * Envelope;
        Out[2] += -Math.cos(Turn) * Source.Strength * Envelope;
        return;
    }
    default: return;
    }
}

function EvaluateAccelerateSource(Source, X, Y, Z, Envelope, Share, Centre, Out)
{
    const Scale = Source.Strength * Share * Envelope;
    if (Source.Type === 'Gravity') { Out[1] -= Scale; return; }
    if (Source.Type === 'Lift') { Out[1] += Scale; return; }
    const DeltaX = X - Centre[0], DeltaY = Y - Centre[1], DeltaZ = Z - Centre[2];
    const Separation = Math.max(1e-3, Math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ));
    const OutX = DeltaX / Separation, OutY = DeltaY / Separation, OutZ = DeltaZ / Separation;
    if (Source.Type === 'Repulsor') { Out[0] += OutX * Scale; Out[1] += OutY * Scale; Out[2] += OutZ * Scale; return; }
    if (Source.Type === 'Magnetic')
    {
        const Clipped = Math.max(Separation, 0.4);
        const UnitX = DeltaX / Clipped, UnitY = DeltaY / Clipped, UnitZ = DeltaZ / Clipped;
        const Along = UnitZ * 3;
        const FieldX = Along * UnitX, FieldY = Along * UnitY, FieldZ = Along * UnitZ - 1;
        const Length = Math.hypot(FieldX, FieldY, FieldZ) || 1;
        Out[0] += FieldX / Length * Scale; Out[1] += FieldY / Length * Scale; Out[2] += FieldZ / Length * Scale;
        return;
    }
    const TangentLength = Math.hypot(OutZ, OutX) || 1;
    const TangentX = -OutZ / TangentLength, TangentZ = OutX / TangentLength;
    if (Source.Type === 'Orbit') { Out[0] += TangentX * Scale; Out[2] += TangentZ * Scale; return; }
    const Turn = (Source.Swirl || 0) * Share * Envelope;
    Out[0] += -OutX * Scale + TangentX * Turn;
    Out[1] += -OutY * Scale;
    Out[2] += -OutZ * Scale + TangentZ * Turn;
}

export function EnsureCompiledFormula(Source, SlotIndex = 0)
{
    if (!Source) return null;
    const Code = Source.FormulaCode || DEFAULT_FORMULA_CODE;
    if (Source._CompiledFormula && Source._CompiledFormula.Source === Code && Source._CompiledFormula.SlotIndex === SlotIndex)
        return Source._CompiledFormula;
    const Res = CompileFieldProgram(Code, SlotIndex);
    Res.SlotIndex = SlotIndex;
    Source._CompiledFormula = Res;
    return Res;
}

const FormulaVecScratch = [0, 0, 0];
function BuildFormulaCtx(Source, Envelope)
{
    return {
        R: Math.max(0.2, Source.Radius || 3),
        Gate: Envelope,
        Attenuate: (Dist, Reach) => Falls(Source.Falloff || 'Smooth', Dist, Reach || Source.Radius || 3),
        CurlNoise: (X, Y, Z, Scale = 48) =>
        {
            const S = Math.max(0.5, Scale);
            NoiseGradient(X / S, Z / S + Y / S * 0.35, 3109, NoiseGradientStore);
            const Vy = NoiseGradient(X / S + 21.7, Z / S - 14.3, 3113, NoiseGradientStore) * 0.35;
            FormulaVecScratch[0] = NoiseGradientStore[1];
            FormulaVecScratch[1] = Vy;
            FormulaVecScratch[2] = -NoiseGradientStore[0];
            return FormulaVecScratch;
        },
        Noise3: (X, Y, Z) =>
        {
            const Nx = NoiseGradient(X, Z + Y * 0.37, 7331, NoiseGradientStore);
            FormulaVecScratch[0] = NoiseGradientStore[0];
            FormulaVecScratch[1] = Nx * 0.5;
            FormulaVecScratch[2] = NoiseGradientStore[1];
            return FormulaVecScratch;
        }
    };
}

export function PrepareSource(Source, Wind, Time)
{
    const Type = TypeById[Source.Type];
    if (!Type) return null;
    let Contribution = Type.Contribution;
    if (Source.UseFormula)
    {
        const Comp = EnsureCompiledFormula(Source);
        if (Comp?.Ok) Contribution = Comp.OutputChannel === 'Flow' ? 'Flow' : 'Accelerate';
    }
    const Activation = ResolveActivation(Source, Time);
    const Centre = ResolveCentre(Source, Wind, Activation.Age);
    let BoundX = Centre[0], BoundZ = Centre[2], Bound = Source.Radius || 3;
    if (Source.Type === 'GustFront' || Source.Reach === 'Box')
    {
        const Extents = ResolveBoxExtents(Source), Turn = (Source.Bearing || 0) * Radian;
        BoundX += Math.sin(Turn) * Extents.OffsetZ;
        BoundZ -= Math.cos(Turn) * Extents.OffsetZ;
        Bound = Math.hypot(Extents.HalfX, Extents.HalfZ);
    }
    else if (Source.Type === 'Blast') Bound = Math.min(Source.Radius, Source.InitialRadius + Source.Expansion * Activation.Age + 3.2 * Source.Thickness);
    return {
        Source, Type, Contribution, Envelope: Activation.Envelope, Age: Activation.Age, Status: Activation.Status,
        Centre, BoundX, BoundZ, BoundSquared: Bound * Bound
    };
}

export function Prepare(FieldScene, Time)
{
    const Entries = [];
    for (const Source of FieldScene.Sources || [])
    {
        const Entry = PrepareSource(Source, FieldScene.Wind, Time);
        if (Entry) Entries.push(Entry);
    }
    return { Time, Entries };
}

const FlowScratch = new Float64Array(3);

export function EvaluateEntry(Entry, Wind, X, Y, Z, Out, Time = 0)
{
    if (Entry.Envelope <= 1e-5) return false;
    const DeltaX = X - Entry.BoundX, DeltaZ = Z - Entry.BoundZ;
    if (DeltaX * DeltaX + DeltaZ * DeltaZ > Entry.BoundSquared) return false;
    const { Source, Envelope, Age, Centre } = Entry;
    const Share = ResolveReach(Source, Centre, X, Y, Z, Wind.GroundHeight);
    if (Share <= 0) return false;
    if (Source.UseFormula)
    {
        const Comp = EnsureCompiledFormula(Source);
        if (Comp?.Ok && Comp.JsFn)
        {
            const Vec = Comp.JsFn(
                X, Y, Z, Centre[0], Centre[1], Centre[2],
                Math.max(0.2, Source.Radius || 3),
                Source.Strength ?? 8,
                Source.Swirl ?? 2.5,
                Time,
                BuildFormulaCtx(Source, Envelope)
            );
            const Factor = Share * Envelope;
            Out[0] += (Vec[0] || 0) * Factor;
            Out[1] += (Vec[1] || 0) * Factor;
            Out[2] += (Vec[2] || 0) * Factor;
            return true;
        }
    }
    if (Entry.Contribution === 'Flow')
    {
        if (Share >= 0.999) { EvaluateFlowSource(Source, Wind, X, Y, Z, Age, Envelope, Centre, Out, Time); return true; }
        FlowScratch[0] = FlowScratch[1] = FlowScratch[2] = 0;
        EvaluateFlowSource(Source, Wind, X, Y, Z, Age, Envelope, Centre, FlowScratch, Time);
        Out[0] += FlowScratch[0] * Share; Out[1] += FlowScratch[1] * Share; Out[2] += FlowScratch[2] * Share;
        return true;
    }
    if (Entry.Contribution === 'Accelerate') EvaluateAccelerateSource(Source, X, Y, Z, Envelope, Share, Centre, Out);
    else Out[0] += Source.Strength * Share * Envelope;
    return true;
}

export function EvaluateSource(Source, Wind, X, Y, Z, Time, Out)
{
    const Entry = PrepareSource(Source, Wind, Time);
    return Entry ? EvaluateEntry(Entry, Wind, X, Y, Z, Out, Time) : false;
}

export function EvaluatePrepared(FieldScene, Prepared, Contribution, X, Y, Z, CellSpan, Out, Solo, Channel = null)
{
    if (!FieldScene.Field?.Enabled) return true;
    if (Contribution === 'Flow' && (!Solo || Solo === 'Wind')) EvaluateWind(FieldScene.Wind, FieldScene.Field, X, Y, Z, Prepared.Time, CellSpan, Out);
    if (Solo === 'Wind') return true;
    for (const Entry of Prepared.Entries)
    {
        if (Entry.Contribution !== Contribution) continue;
        if (Solo && Entry.Source.Token !== Solo) continue;
        if (Channel && Entry.Source.Acts?.length && !Entry.Source.Acts.includes(Channel)) continue;
        EvaluateEntry(Entry, FieldScene.Wind, X, Y, Z, Out, Prepared.Time);
    }
    return true;
}

export function Evaluate(FieldScene, Contribution, X, Y, Z, Time, CellSpan, Out, Solo, Channel = null)
{
    return EvaluatePrepared(FieldScene, Prepare(FieldScene, Time), Contribution, X, Y, Z, CellSpan, Out, Solo, Channel);
}

export function EvaluateAllPrepared(FieldScene, Prepared, X, Y, Z, CellSpan = 0.5, Solo = null, Channel = 'Particles')
{
    const Velocity = [0, 0, 0], Acceleration = [0, 0, 0], DampArr = [0, 0, 0];
    EvaluatePrepared(FieldScene, Prepared, 'Flow', X, Y, Z, CellSpan, Velocity, Solo, Channel);
    EvaluatePrepared(FieldScene, Prepared, 'Accelerate', X, Y, Z, CellSpan, Acceleration, Solo, Channel);
    EvaluatePrepared(FieldScene, Prepared, 'Damp', X, Y, Z, CellSpan, DampArr, Solo, Channel);
    return {
        Velocity,
        Acceleration,
        Damping: DampArr[0],
        FlowMag: Math.hypot(Velocity[0], Velocity[1], Velocity[2]),
        AccelMag: Math.hypot(Acceleration[0], Acceleration[1], Acceleration[2])
    };
}

export function SampleDifferential(FieldScene, Prepared, Contribution, X, Y, Z, Step = 0.25, Solo = null, Channel = 'Particles')
{
    const H = Math.max(0.05, Step);
    const Inv2H = 0.5 / H;
    const Px = [0, 0, 0], Nx = [0, 0, 0], Py = [0, 0, 0], Ny = [0, 0, 0], Pz = [0, 0, 0], Nz = [0, 0, 0];
    EvaluatePrepared(FieldScene, Prepared, Contribution, X + H, Y, Z, H, Px, Solo, Channel);
    EvaluatePrepared(FieldScene, Prepared, Contribution, X - H, Y, Z, H, Nx, Solo, Channel);
    EvaluatePrepared(FieldScene, Prepared, Contribution, X, Y + H, Z, H, Py, Solo, Channel);
    EvaluatePrepared(FieldScene, Prepared, Contribution, X, Y - H, Z, H, Ny, Solo, Channel);
    EvaluatePrepared(FieldScene, Prepared, Contribution, X, Y, Z + H, H, Pz, Solo, Channel);
    EvaluatePrepared(FieldScene, Prepared, Contribution, X, Y, Z - H, H, Nz, Solo, Channel);
    const Div = ((Px[0] - Nx[0]) + (Py[1] - Ny[1]) + (Pz[2] - Nz[2])) * Inv2H;
    const CurlX = ((Py[2] - Ny[2]) - (Pz[1] - Nz[1])) * Inv2H;
    const CurlY = ((Pz[0] - Nz[0]) - (Px[2] - Nx[2])) * Inv2H;
    const CurlZ = ((Px[1] - Nx[1]) - (Py[0] - Ny[0])) * Inv2H;
    return { Curl: Math.hypot(CurlX, CurlY, CurlZ), Div };
}

export function ResolveProfile(FieldScene, Source, SliceHeight)
{
    const Type = TypeById[Source.Type] || TypeById.WindGenerator;
    const Reach = Source.Type === 'GustFront' ? Source.CrestDepth * 2.5 : Source.Type === 'Blast' ? Math.max(Source.Radius * 1.1, 2.5) :
                  Source.Reach === 'Box' ? Math.max(ResolveBoxExtents(Source).HalfX, ResolveBoxExtents(Source).HalfZ) * 1.15 : Source.Radius * 1.15;
    const Points = [];
    const Probe = Object.assign({}, Source, { Enabled: true, Activation: 'Always', TrackSpeed: 0, Drift: 0, FrontSpeed: 0 });
    const Steps = 64;
    for (let Step = 0; Step <= Steps; ++Step)
    {
        const Separation = Step / Steps * Reach;
        const Out = new Float64Array(3);
        let X = Source.Centre[0] + Separation, Z = Source.Centre[2];
        if (Source.Type === 'GustFront') { X = Source.Centre[0] - Math.sin((Source.Bearing || 0) * Radian) * (Separation - Reach * 0.2); Z = Source.Centre[2] + Math.cos((Source.Bearing || 0) * Radian) * (Separation - Reach * 0.2); }
        else if (Source.Type === 'Jet' || Source.Type === 'Current' || Source.Type === 'Wake' || Source.Type === 'Drag' || Source.Type === 'WindGenerator')
        {
            const Axis = Source.Type === 'Wake' ? (Source.Bearing || 0) + 180 : Source.Type === 'Jet' ? (Source.Bearing || 0) : (Source.Bearing || 0) + 90;
            X = Source.Centre[0] + Math.sin(Axis * Radian) * Separation;
            Z = Source.Centre[2] - Math.cos(Axis * Radian) * Separation;
        }
        const Height = Source.Type === 'Blast' || Type.Contribution !== 'Flow' ? Source.Centre[1] : Source.Type === 'Puff' || Source.Type === 'Jet' ? Source.Centre[1] : SliceHeight;
        const BlastTime = Source.Type === 'Blast' ? Math.max(0, (Reach * 0.45 - Source.InitialRadius) / Math.max(0.2, Source.Expansion)) : 0;
        EvaluateSource(Probe, FieldScene.Wind, X, Height, Z, BlastTime, Out);
        const EffectiveContrib = Source.UseFormula ? (EnsureCompiledFormula(Source)?.OutputChannel === 'Flow' ? 'Flow' : 'Accelerate') : Type.Contribution;
        const Reading = EffectiveContrib === 'Damp' ? Out[0] : Math.hypot(Out[0], Out[1], Out[2]);
        Points.push([Separation, Reading]);
    }
    return { Points, Reach, Unit: Contributions[Type.Contribution].Unit };
}

//------------------------------------------------------------------------------------------------------------------------
//                                      WEBGPU WIND TEXTURE & ACCEL FIELD PACKING
//------------------------------------------------------------------------------------------------------------------------

const HalfF32 = new Float32Array(1);
const HalfI32 = new Int32Array(HalfF32.buffer);

export function FloatToHalf(Val)
{
    HalfF32[0] = Val;
    const F = HalfI32[0];
    const Sign = (F >>> 16) & 0x8000;
    let Exp = ((F >>> 23) & 0xff) - 112;
    let Mant = F & 0x007fffff;
    if (Exp <= 0)
    {
        if (Exp < -10) return Sign;
        Mant = (Mant | 0x00800000) >>> (1 - Exp);
        if (Mant & 0x1000) Mant += 0x2000;
        return Sign | (Mant >>> 13);
    }
    if (Exp === 143) return Sign | (Mant ? 0x7e00 : 0x7c00);
    if (Exp > 30) return Sign | 0x7c00;
    return Sign | (Exp << 10) | ((Mant + 0x1000) >>> 13);
}

const TexOutScratch = new Float64Array(3);

// Bakes the exact ForceFieldCatalogue Flow velocity + AmbientWind onto the 24×12×24 rgba16float WebGPU wind texture
export function SampleWindTexData(FieldScene, Prepared, WindDomain = { min: [-6, 0, -6], size: [12, 8, 12], dim: [24, 12, 24] }, WindScale = 1, Solo = null, OutHalves = null)
{
    const [Gx, Gy, Gz] = WindDomain.dim;
    const [Mx, My, Mz] = WindDomain.min;
    const [Sx, Sy, Sz] = WindDomain.size;
    const Halves = OutHalves || new Uint16Array(Gx * Gy * Gz * 4);
    const CellSpan = Sx / Gx;
    let Ptr = 0;
    for (let Iz = 0; Iz < Gz; ++Iz)
    {
        const Z = Mz + ((Iz + 0.5) / Gz) * Sz;
        for (let Iy = 0; Iy < Gy; ++Iy)
        {
            const Y = My + ((Iy + 0.5) / Gy) * Sy;
            for (let Ix = 0; Ix < Gx; ++Ix)
            {
                const X = Mx + ((Ix + 0.5) / Gx) * Sx;
                TexOutScratch[0] = TexOutScratch[1] = TexOutScratch[2] = 0;
                EvaluatePrepared(FieldScene, Prepared, 'Flow', X, Y, Z, CellSpan, TexOutScratch, Solo, 'Particles');
                const Vx = TexOutScratch[0] * WindScale;
                const Vy = TexOutScratch[1] * WindScale;
                const Vz = TexOutScratch[2] * WindScale;
                const Mag = Math.hypot(Vx, Vy, Vz);
                Halves[Ptr++] = FloatToHalf(Vx);
                Halves[Ptr++] = FloatToHalf(Vy);
                Halves[Ptr++] = FloatToHalf(Vz);
                Halves[Ptr++] = FloatToHalf(Mag);
            }
        }
    }
    return Halves;
}

const GpuFieldTypeMap = { Attractor: 0, Repulsor: 1, Lift: 2, Magnetic: 3, Orbit: 4, Gravity: 5, Drag: 6 };
const GpuReachMap = { Sphere: 0, Everywhere: 0, Column: 1, Box: 2 };
const GpuFalloffMap = { Smooth: 0, Linear: 1, InverseSquare: 2, None: 3 };

// Packs up to 16 Accelerate/Damp entries (4 vec4 slots each) + per-effect attractors/dipoles for shaders.js evalFields()
export function PackForceEntries(FieldScene, Prepared, Effects = [], OutBuf = null, Solo = null)
{
    const Buf = OutBuf || new Float32Array((1 + 16 * 4) * 4);
    Buf.fill(0);
    let Count = 0;
    const Put = (TypeNum, Pos, Radius, Strength, SwirlOrGuide, Swallow, ReachNum, FalloffNum, BearingRad, Height, HalfX, HalfZ, OffsetZ) =>
    {
        if (Count >= 16) return;
        const O = (1 + Count * 4) * 4;
        Buf[O] = Pos[0]; Buf[O + 1] = Pos[1]; Buf[O + 2] = Pos[2]; Buf[O + 3] = Radius;
        Buf[O + 4] = TypeNum; Buf[O + 5] = Strength; Buf[O + 6] = SwirlOrGuide; Buf[O + 7] = Swallow;
        Buf[O + 8] = ReachNum; Buf[O + 9] = FalloffNum; Buf[O + 10] = BearingRad; Buf[O + 11] = Height;
        Buf[O + 12] = HalfX; Buf[O + 13] = HalfZ; Buf[O + 14] = OffsetZ; Buf[O + 15] = 1;
        Count++;
    };

    for (const E of Effects)
    {
        if (!E.P?.visible) continue;
        if (E.P.attractor?.enabled)
            Put(0, E.P.origin, E.P.attractor.radius, E.P.attractor.strength, E.P.attractor.swirl || 0, E.P.attractor.swallow || 0, 0, 0, 0, 4, 4, 4, 0);
        if (E.P.magneticField)
            Put(3, E.P.origin, E.P.magneticField.radius, E.P.magneticField.strength, E.P.magneticField.guide || 2.0, 0, 0, 0, 0, 4, 4, 4, 0);
    }

    if (FieldScene?.Field?.Enabled && Solo !== 'Wind')
    {
        for (const Entry of Prepared.Entries)
        {
            if (Entry.Contribution === 'Flow' || Entry.Envelope <= 1e-4) continue;
            const Src = Entry.Source;
            if (Solo && Src.Token !== Solo) continue;
            if (Src.Acts?.length && !Src.Acts.includes('Particles')) continue;
            const TypeNum = GpuFieldTypeMap[Src.Type];
            if (TypeNum === undefined) continue;
            const Ext = ResolveBoxExtents(Src);
            Put(
                TypeNum, Entry.Centre, Src.Radius,
                Src.Strength * Entry.Envelope,
                Src.Swirl ?? Src.Guide ?? 0,
                Src.SwallowRadius ?? 0,
                GpuReachMap[Src.Reach] ?? 0,
                GpuFalloffMap[Src.Falloff] ?? 0,
                (Src.Bearing || 0) * Radian,
                Ext.Height,
                Ext.HalfX,
                Ext.HalfZ,
                Ext.OffsetZ
            );
        }
    }

    Buf[0] = Count;
    Buf[1] = Prepared?.Time || 0;
    return Buf;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LEVELSPACE & BUDGET
//------------------------------------------------------------------------------------------------------------------------

export const ExistingKeys = new Set(['identity', 'type', 'name', 'parent', 'enabled', 'visualize', 'include_turbulence', 'centre_metres',
    'half_extents_metres', 'grid_resolution', 'arrow_seconds', 'direction_deg', 'speed_mps', 'turbulence', 'gust_frequency_hz',
    'turbulence_scale_metres', 'gust_strength', 'shear_per_kilometre', 'veer_degrees_per_kilometre', 'advect_clouds', 'advect_plumes',
    'advect_particles']);

const FormatNumber = (Reading) => Number.isInteger(Reading) ? Reading.toFixed(1) : String(Number(Reading.toFixed(4)));
const FormatTriple = (Triple) => `[${Triple.map(FormatNumber).join(', ')}]`;

export function EncodeSource(Source, Lines)
{
    const Type = TypeById[Source.Type] || TypeById.WindGenerator;
    Lines.push('', '[[Records]]', `Identity = "${Source.Token}"`, 'Type = "ForceSource"', `Name = "${Source.Name}"`, 'Owner = "ForceFieldRoot"',
        '[Records.Configuration]', `Enabled = ${Source.Enabled}`, `Source = "${Type.Native}"`, `Contribution = "${Type.Contribution}"`,
        `CentreMetres = ${FormatTriple(Source.Centre)}`, `Reach = "${Source.Reach}"`, `ReachMetres = ${FormatNumber(Source.Radius)}`,
        `Falloff = "${Source.Falloff}"`, `Strength = ${FormatNumber(Source.Strength)}`);
    for (const Parameter of Type.Parameters)
    {
        if (Parameter.Key === 'Strength') continue;
        Lines.push(`${Parameter.Key} = ${FormatNumber(Source[Parameter.Key])}`);
    }
    for (const [Key] of Type.Bearings || []) Lines.push(`${Key}Degrees = ${FormatNumber(Source[Key])}`);
    if (Type.Spin) Lines.push(`Spin = ${Source.Spin < 0 ? '"Anticyclonic"' : '"Cyclonic"'}`);
    if (Source.Reach === 'Column' || Source.Reach === 'Box') Lines.push(`HeightMetres = ${FormatNumber(Source.Height)}`);
    if (Source.Reach === 'Box' && Source.Type !== 'Jet' && Source.Type !== 'Wake') Lines.push(`HalfExtentsMetres = [${FormatNumber(Source.HalfX)}, ${FormatNumber(Source.HalfZ)}]`);
    Lines.push(`Activation = "${Source.Activation}"`, `BeginsSeconds = ${FormatNumber(Source.Begins)}`, `LastsSeconds = ${FormatNumber(Source.Lasts)}`,
        `RepeatsSeconds = ${FormatNumber(Source.Repeats)}`, `RampInSeconds = ${FormatNumber(Source.RampIn)}`, `RampOutSeconds = ${FormatNumber(Source.RampOut)}`,
        `ActsOn = [${Source.Acts.map((Key) => `"${Key}"`).join(', ')}]`);
    if (Source.UseFormula)
    {
        Lines.push(`UseFormula = true`, `FormulaCode = ${JSON.stringify(Source.FormulaCode || DEFAULT_FORMULA_CODE)}`);
    }
}

export function EncodeLevelspace(FieldScene, Only)
{
    const Lines = [];
    const { Field, Wind } = FieldScene;
    if (!Only || Only === 'Field')
    {
        Lines.push('[[Records]]', 'Identity = "ForceFieldRoot"', 'Type = "ForceField"', 'Name = "Force Field"', '[Records.Configuration]',
            `Enabled = ${Field.Enabled}`, `Visualize = ${Field.Visualize}`, `IncludeTurbulence = ${Field.IncludeTurbulence}`,
            `CentreMetres = ${FormatTriple(Field.RegionCentre)}`, `HalfExtentsMetres = ${FormatTriple(Field.RegionHalfExtents)}`,
            `GridResolution = ${Field.GridResolution}`, `ArrowSeconds = ${FormatNumber(Field.ArrowSeconds)}`,
            `Cascades = ${Field.Cascades}`, `FinestCellMetres = ${FormatNumber(Field.FinestCell)}`, `CellsPerSide = ${Field.CellsPerSide}`,
            `VerticalCells = ${Field.VerticalCells}`, `ExtentMetres = ${FormatNumber(Field.Extent)}`,
            `Channels = [${Field.Channels.map((Key) => `"${Key}"`).join(', ')}]`);
    }
    if (!Only || Only === 'Wind')
    {
        if (Lines.length) Lines.push('');
        Lines.push('[[Records]]', 'Identity = "WindSource"', 'Type = "Wind"', 'Name = "Wind"', 'Owner = "ForceFieldRoot"', '[Records.Configuration]',
            `Enabled = ${Wind.Enabled}`, `DirectionDegrees = ${FormatNumber(Wind.Bearing)}`, `SpeedMetresPerSecond = ${FormatNumber(Wind.Speed)}`,
            `Turbulence = ${FormatNumber(Wind.Turbulence)}`, `TurbulenceScaleMetres = ${FormatNumber(Wind.TurbulenceScale)}`,
            `GustStrength = ${FormatNumber(Wind.GustStrength)}`, `GustFrequencyHz = ${FormatNumber(Wind.GustFrequency)}`,
            `ShearPerKilometre = ${FormatNumber(Wind.Shear)}`, `VeerDegreesPerKilometre = ${FormatNumber(Wind.Veer)}`,
            `AdvectClouds = ${Wind.AdvectClouds}`, `AdvectPlumes = ${Wind.AdvectPlumes}`, `AdvectParticles = ${Wind.AdvectParticles}`,
            `GroundHeightMetres = ${FormatNumber(Wind.GroundHeight)}`, `Seed = ${Wind.Seed}`,
            `BoundedRegion = ${Boolean(Wind.Bounded)}`, `RegionRadiusMetres = ${FormatNumber(Wind.RegionRadius || 12)}`);
    }
    if (!Only) for (const Source of FieldScene.Sources) EncodeSource(Source, Lines);
    else if (Only !== 'Field' && Only !== 'Wind')
    {
        const Source = FieldScene.Sources.find((One) => One.Token === Only);
        if (Source) { EncodeSource(Source, Lines); if (Lines[0] === '') Lines.shift(); }
    }
    return Lines;
}

const BeaufortLimits = [0.5, 1.6, 3.4, 5.5, 8.0, 10.8, 13.9, 17.2, 20.8, 24.5, 28.5, 32.7];
const BeaufortNames = ['Calm', 'Light air', 'Light breeze', 'Gentle breeze', 'Moderate breeze', 'Fresh breeze', 'Strong breeze', 'Near gale', 'Gale',
    'Strong gale', 'Storm', 'Violent storm', 'Hurricane force'];

export function ResolveBeaufort(Speed)
{
    let Force = 0;
    while (Force < BeaufortLimits.length && Speed >= BeaufortLimits[Force]) Force++;
    return { Force, Name: BeaufortNames[Force] };
}

export function ResolveBudget(FieldScene)
{
    const Baked = [], Live = [], Damp = [];
    for (const Source of FieldScene.Sources)
    {
        const Type = TypeById[Source.Type];
        if (!Type) continue;
        if (Type.Contribution === 'Damp') Damp.push(Source);
        else if (Type.Contribution === 'Flow' && !Source.Acts.length) Baked.push(Source);
        else Live.push(Source);
    }
    const Field = FieldScene.Field;
    const CascadeBytes = Field.CellsPerSide * Field.CellsPerSide * Field.VerticalCells * 8;
    const Levels = [];
    for (let Level = 0; Level < Field.Cascades; ++Level)
    {
        const Cell = Field.FinestCell * Math.pow(2, Level);
        Levels.push({ Cell, Extent: Cell * Field.CellsPerSide, Refresh: Math.pow(4, Level) });
    }
    return { Baked, Live, Damp, CascadeBytes, TotalBytes: CascadeBytes * Field.Cascades, Levels, GpuSlots: 16 };
}

export const ForceFieldCatalogue = {
    Contributions, Channels, Reaches, Falloffs, Activations, Types, TypeById, Falls, ResolveSourceColour,
    ConstructSource, ConstructDefaultField, ConstructScenario, ConstructChamberScenario,
    EvaluateWind, WindSpeedAt, ResolveActivation, ResolveCentre, ResolveReach, ResolveBoxExtents,
    EvaluateSource, Evaluate, Prepare, PrepareSource, EvaluatePrepared, EvaluateEntry, EvaluateAllPrepared,
    ResolveProfile, FloatToHalf, SampleWindTexData, PackForceEntries, EncodeLevelspace, EncodeSource,
    ExistingKeys, ResolveBeaufort, ResolveBudget, Smooth01, Radian,
    EnsureCompiledFormula, SampleDifferential, CompileFieldProgram, DEFAULT_FORMULA_CODE, FORMULA_PRESETS
};

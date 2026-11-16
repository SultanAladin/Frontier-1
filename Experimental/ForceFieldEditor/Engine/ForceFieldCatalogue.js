/*==========================================================================================================================================
                                                         FORCEFIELDCATALOGUE.JS
==========================================================================================================================================*/
// 🧩 Bounded Force Field & Wind Generator catalogue: localized Wind Generator + 16 native/eos force types, strict finite
//    spatial influence envelopes (Sphere / Column / Box), Frontier Wind profile parity, and Levelspace encoding.

export const Radian = Math.PI / 180;
export const Smooth01 = (Share) => { const C = Math.min(1, Math.max(0, Share)); return C * C * (3 - 2 * C); };
const Clamp = (Value, Min, Max) => Math.min(Max, Math.max(Min, Value));

//------------------------------------------------------------------------------------------------------------------------
//                                                    TAXONOMY
//------------------------------------------------------------------------------------------------------------------------

export const Contributions = {
    Flow: {
        Label: 'Flow',
        Unit: 'm/s',
        Colour: '#8fd3c4',
        Text: 'Air velocity carried by a wind generator or flow field. Receivers relax toward it based on coupling. Sums cleanly into one velocity lattice.'
    },
    Accelerate: {
        Label: 'Accelerate',
        Unit: 'm/s²',
        Colour: '#e8c27a',
        Text: 'Direct acceleration applied regardless of drag coupling: attractors, repulsors, orbits, lift, magnetic dipoles, and gravity zones.'
    },
    Damp: {
        Label: 'Damp',
        Unit: '1/s',
        Colour: '#b9a3e8',
        Text: 'Removes kinetic energy exponentially inside a finite zone without adding direction.'
    }
};

export const Channels = [
    ['Particles', 'Particles'], ['Debris', 'Debris'], ['Rain & hail', 'Precipitation'],
    ['Clouds', 'Clouds'], ['Plumes', 'Plumes'], ['Foliage', 'Foliage'], ['Vehicles', 'Vehicles']
];

// Every placed forcefield has finite spatial influence (no infinite unbounded defaults).
export const Reaches = [['Sphere', 'Sphere'], ['Column', 'Column'], ['Box', 'Box']];
export const Falloffs = [['Smooth', 'Smooth'], ['Linear', 'Linear'], ['Inverse²', 'InverseSquare'], ['Hard', 'None']];
export const Activations = [['Always', 'Always'], ['Window', 'Window'], ['Trigger', 'Trigger']];

export function Falls(Shape, Distance, Radius)
{
    if (Distance >= Radius) return 0;
    if (Shape === 'None') return 1;
    const Share = Clamp(Distance / Math.max(Radius, 1e-3), 0, 1);
    if (Shape === 'Linear') return 1 - Share;
    if (Shape === 'InverseSquare') return (1 / (1 + 8 * Share * Share)) * (1 - Smooth01((Share - 0.85) / 0.15));
    const Soft = 1 - Share * Share;
    return Soft * Soft;
}

const DistanceParam = (Minimum, Maximum, Step, Unit) => ({ Minimum, Maximum, Step, Unit: Unit || 'm', Scale: 'Logarithmic', Distance: true });

//------------------------------------------------------------------------------------------------------------------------
//                                                   FORCE TYPES
//------------------------------------------------------------------------------------------------------------------------

export const Types = [
    {
        Id: 'WindGenerator', Name: 'Wind generator', Contribution: 'Flow', Glyph: 'Wind', Colour: '#4ae4e8', Native: 'LocalizedWindGenerator', Reach: 'Column',
        Blurb: 'Placeable bounded wind generator: produces directional air flow with coherent gusts and curl turbulence strictly inside its finite influence envelope.',
        Defaults: { Strength: 16, Bearing: 60, GustStrength: 0.25, GustFrequency: 0.14, Turbulence: 0.35, TurbulenceScale: 42, Radius: 240, Height: 160, HalfX: 220, HalfZ: 140, Falloff: 'Smooth', CentreY: 0 },
        Parameters: [
            { Key: 'Strength', Label: 'Wind speed', Minimum: 0.5, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'GustStrength', Label: 'Gust amplitude', Minimum: 0, Maximum: 1, Step: 0.01, Unit: '×', Digits: 2 },
            { Key: 'GustFrequency', Label: 'Gust frequency', Minimum: 0.02, Maximum: 1.5, Step: 0.01, Unit: 'Hz', Digits: 2 },
            { Key: 'Turbulence', Label: 'Curl turbulence', Minimum: 0, Maximum: 1.5, Step: 0.01, Unit: '', Digits: 2 },
            Object.assign({ Key: 'TurbulenceScale', Label: 'Eddy scale' }, DistanceParam(5, 800, 1))
        ],
        Bearings: [['Bearing', 'Blows toward']]
    },
    {
        Id: 'Tornado', Name: 'Tornado column', Contribution: 'Flow', Glyph: 'Tornado', Colour: '#32e6a4', Native: 'VortexColumn', Reach: 'Column',
        Blurb: 'Burgers–Rott vortex column with tangential spin peaking at the inner radius, ground boundary inflow, helical core updraft, and finite outer influence.',
        Defaults: { Strength: 46, InnerRadius: 22, CollarRadius: 72, Inflow: 14, Updraft: 26, Height: 480, Spin: 1, TrackBearing: 90, TrackSpeed: 0, Radius: 260, Falloff: 'Smooth' },
        Parameters: [
            { Key: 'Strength', Label: 'Peak tangential speed', Minimum: 1, Maximum: 120, Step: 0.5, Unit: 'm/s', Digits: 1 },
            Object.assign({ Key: 'InnerRadius', Label: 'Core radius' }, DistanceParam(2, 20000, 1)),
            Object.assign({ Key: 'CollarRadius', Label: 'Collar radius' }, DistanceParam(2, 40000, 1)),
            { Key: 'Inflow', Label: 'Radial inflow', Minimum: 0, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'Updraft', Label: 'Axial updraft', Minimum: 0, Maximum: 80, Step: 0.5, Unit: 'm/s', Digits: 1 },
            Object.assign({ Key: 'Height', Label: 'Column height' }, DistanceParam(20, 15000, 10)),
            { Key: 'TrackSpeed', Label: 'Track speed', Minimum: 0, Maximum: 30, Step: 0.1, Unit: 'm/s', Digits: 1 }
        ],
        Bearings: [['TrackBearing', 'Track toward']], Spin: true
    },
    {
        Id: 'Jet', Name: 'Jet corridor', Contribution: 'Flow', Glyph: 'Jet', Colour: '#38b6ff', Native: 'DirectionalJetCorridor', Reach: 'Box',
        Blurb: 'Bounded Venturi wind corridor along an axis, tapering smoothly at its sides and downstream limit.',
        Defaults: { Strength: 22, Length: 180, InnerRadius: 24, Bearing: 95, Radius: 180, HalfX: 90, HalfZ: 140, Height: 80, Falloff: 'Smooth', CentreY: 8 },
        Parameters: [
            { Key: 'Strength', Label: 'Axial speed', Minimum: 0.5, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
            Object.assign({ Key: 'Length', Label: 'Corridor length' }, DistanceParam(5, 30000, 1)),
            Object.assign({ Key: 'InnerRadius', Label: 'Corridor radius' }, DistanceParam(1, 5000, 0.5))
        ],
        Bearings: [['Bearing', 'Axis toward']]
    },
    {
        Id: 'GustFront', Name: 'Gust front', Contribution: 'Flow', Glyph: 'Front', Colour: '#ff8c32', Native: 'TravelingGustFront', Reach: 'Box',
        Blurb: 'Bounded travelling surge wave with lifting nose and decaying wake, confined inside its finite front corridor.',
        Defaults: { Strength: 26, FrontSpeed: 12, CrestDepth: 85, Height: 140, HalfWidth: 700, Bearing: 113, Radius: 750, HalfX: 700, HalfZ: 320, Falloff: 'Smooth' },
        Parameters: [
            { Key: 'Strength', Label: 'Peak surge speed', Minimum: 1, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'FrontSpeed', Label: 'Front speed', Minimum: 0, Maximum: 40, Step: 0.5, Unit: 'm/s', Digits: 1 },
            Object.assign({ Key: 'CrestDepth', Label: 'Crest depth' }, DistanceParam(5, 10000, 1)),
            Object.assign({ Key: 'HalfWidth', Label: 'Half width along front' }, DistanceParam(10, 60000, 10)),
            Object.assign({ Key: 'Height', Label: 'Gust head height' }, DistanceParam(5, 5000, 5))
        ],
        Bearings: [['Bearing', 'Travels toward']]
    },
    {
        Id: 'Microburst', Name: 'Microburst', Contribution: 'Flow', Glyph: 'Downburst', Colour: '#7e8cff', Native: 'RadialMicroburst', Reach: 'Column',
        Blurb: 'Convective downdraft shaft spreading into an outward radial ring at the ground within its bounded radius.',
        Defaults: { Strength: 28, Downdraft: 22, InnerRadius: 48, RingRadius: 120, Height: 340, Radius: 360, Falloff: 'Smooth', CentreY: 220 },
        Parameters: [
            { Key: 'Strength', Label: 'Peak outflow speed', Minimum: 1, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
            { Key: 'Downdraft', Label: 'Downdraft speed', Minimum: 0, Maximum: 60, Step: 0.5, Unit: 'm/s', Digits: 1 },
            Object.assign({ Key: 'InnerRadius', Label: 'Column radius' }, DistanceParam(5, 10000, 1)),
            Object.assign({ Key: 'RingRadius', Label: 'Outflow ring radius' }, DistanceParam(10, 20000, 1))
        ]
    },
    {
        Id: 'Thermal', Name: 'Thermal updraft', Contribution: 'Flow', Glyph: 'Thermal', Colour: '#ffb838', Native: 'ThermalUpdraftPlume', Reach: 'Column',
        Blurb: 'Buoyant rising chimney with ground-level radial entrainment and upper turret divergence inside a finite radius.',
        Defaults: { Strength: 14.5, Inflow: 4.8, InnerRadius: 48, Height: 480, Radius: 180, Falloff: 'Smooth' },
        Parameters: [
            { Key: 'Strength', Label: 'Peak updraft', Minimum: 0.5, Maximum: 40, Step: 0.1, Unit: 'm/s', Digits: 1 },
            { Key: 'Inflow', Label: 'Entrainment inflow', Minimum: 0, Maximum: 20, Step: 0.1, Unit: 'm/s', Digits: 1 },
            Object.assign({ Key: 'InnerRadius', Label: 'Warm core radius' }, DistanceParam(2, 5000, 1)),
            Object.assign({ Key: 'Height', Label: 'Chimney height' }, DistanceParam(15, 12000, 5))
        ]
    },
    {
        Id: 'Puff', Name: 'Gust puff', Contribution: 'Flow', Glyph: 'Puff', Colour: '#ff9f43', Native: 'LocalizedGustPuff', Reach: 'Sphere',
        Blurb: 'Bounded Gaussian air packet drifting downstream with ambient wind.',
        Defaults: { Strength: 14, Lift: 0.8, Radius: 95, RadiusY: 36, Drift: 0.6, Bearing: 112, Falloff: 'Smooth', CentreY: 12 },
        Parameters: [
            { Key: 'Strength', Label: 'Surge speed', Minimum: 0, Maximum: 40, Step: 0.1, Unit: 'm/s', Digits: 1 },
            { Key: 'Lift', Label: 'Vertical surge', Minimum: -10, Maximum: 10, Step: 0.1, Unit: 'm/s', Digits: 1 },
            Object.assign({ Key: 'RadiusY', Label: 'Vertical radius' }, DistanceParam(1, 2000, 0.5)),
            { Key: 'Drift', Label: 'Drift factor', Minimum: 0, Maximum: 1.5, Step: 0.01, Unit: '×', Digits: 2 }
        ],
        Bearings: [['Bearing', 'Surge toward']]
    },
    {
        Id: 'Blast', Name: 'Blast wave', Contribution: 'Flow', Glyph: 'Blast', Colour: '#ff5d38', Native: 'SphericalImpulseShell', Reach: 'Sphere',
        Blurb: 'Expanding pressure shell r(t) = r₀ + c·t within a strict outer blast radius.',
        Defaults: { Strength: 42, Expansion: 110, Thickness: 18, InitialRadius: 4, Radius: 420, Falloff: 'Smooth', Activation: 'Window', Begins: 4, Lasts: 12, Repeats: 24, RampIn: 0, RampOut: 2, CentreY: 2 },
        Parameters: [
            { Key: 'Strength', Label: 'Peak radial speed', Minimum: 1, Maximum: 200, Step: 1, Unit: 'm/s', Digits: 0 },
            { Key: 'Expansion', Label: 'Wavefront speed', Minimum: 5, Maximum: 400, Step: 1, Unit: 'm/s', Digits: 0 },
            Object.assign({ Key: 'Thickness', Label: 'Shell thickness' }, DistanceParam(0.5, 2000, 0.5)),
            Object.assign({ Key: 'InitialRadius', Label: 'Initial radius' }, DistanceParam(0.5, 1000, 0.5))
        ]
    },
    {
        Id: 'Wake', Name: 'Vehicle wake', Contribution: 'Flow', Glyph: 'Wake', Colour: '#5cb3ff', Native: 'AerodynamicWake', Reach: 'Box',
        Blurb: 'Bounded slipstream wake behind a moving body: forward tow + counter-rotating upwash roll.',
        Defaults: { Strength: 13.5, Circulation: 7.5, Speed: 22, HalfWidth: 2.2, Length: 48, Height: 6, Bearing: 0, Radius: 90, Falloff: 'Smooth', CentreY: 1.2 },
        Parameters: [
            { Key: 'Strength', Label: 'Slipstream tow', Minimum: 0, Maximum: 40, Step: 0.1, Unit: 'm/s', Digits: 1 },
            { Key: 'Circulation', Label: 'Trailing circulation', Minimum: 0, Maximum: 30, Step: 0.1, Unit: 'm/s', Digits: 1 },
            { Key: 'Speed', Label: 'Body speed', Minimum: 0, Maximum: 90, Step: 0.5, Unit: 'm/s', Digits: 1 },
            Object.assign({ Key: 'HalfWidth', Label: 'Half track' }, DistanceParam(0.2, 40, 0.05)),
            Object.assign({ Key: 'Length', Label: 'Wake length' }, DistanceParam(2, 600, 0.5)),
            Object.assign({ Key: 'Height', Label: 'Wake height' }, DistanceParam(0.3, 60, 0.1))
        ],
        Bearings: [['Bearing', 'Body travels toward']]
    },
    {
        Id: 'Current', Name: 'Current zone', Contribution: 'Flow', Glyph: 'Current', Colour: '#2fd6c6', Native: 'current', Reach: 'Box',
        Blurb: 'Uniform steady stream strictly inside its oriented box bounds (air duct, river, ventilation stream).',
        Defaults: { Strength: 12, Bearing: 90, HalfX: 80, HalfZ: 140, Height: 40, Radius: 140, Falloff: 'Smooth' },
        Parameters: [{ Key: 'Strength', Label: 'Flow speed', Minimum: 0, Maximum: 40, Step: 0.1, Unit: 'm/s', Digits: 1 }],
        Bearings: [['Bearing', 'Flows toward']]
    },
    {
        Id: 'Attractor', Name: 'Attractor', Contribution: 'Accelerate', Glyph: 'Attract', Colour: '#ff4757', Native: 'attract', Reach: 'Sphere',
        Blurb: 'Pulls toward centre with optional azimuthal swirl inside a finite spherical influence radius.',
        Defaults: { Strength: 10, Swirl: 4.5, Swallow: 0, Radius: 200, Falloff: 'Smooth', CentreY: 10 },
        Parameters: [
            { Key: 'Strength', Label: 'Radial pull', Minimum: 0, Maximum: 60, Step: 0.1, Unit: 'm/s²', Digits: 1 },
            { Key: 'Swirl', Label: 'Swirl', Minimum: 0, Maximum: 30, Step: 0.1, Unit: 'm/s²', Digits: 1 },
            { Key: 'Swallow', Label: 'Swallow radius', Minimum: 0, Maximum: 500, Step: 0.5, Unit: 'm', Digits: 1 }
        ]
    },
    {
        Id: 'Repulsor', Name: 'Repulsor', Contribution: 'Accelerate', Glyph: 'Repel', Colour: '#3b8eff', Native: 'repel', Reach: 'Sphere',
        Blurb: 'Radial outward push confined strictly within its influence sphere — deflects approaching flow streams.',
        Defaults: { Strength: 12, Radius: 170, Falloff: 'Smooth', CentreY: 10 },
        Parameters: [{ Key: 'Strength', Label: 'Outward push', Minimum: 0, Maximum: 60, Step: 0.1, Unit: 'm/s²', Digits: 1 }]
    },
    {
        Id: 'Orbit', Name: 'Orbit ring', Contribution: 'Accelerate', Glyph: 'Orbit', Colour: '#b374ff', Native: 'orbit', Reach: 'Sphere',
        Blurb: 'Pure tangential acceleration around world up inside a bounded sphere.',
        Defaults: { Strength: 7, Radius: 190, Falloff: 'Smooth', CentreY: 10 },
        Parameters: [{ Key: 'Strength', Label: 'Tangential pull', Minimum: 0, Maximum: 40, Step: 0.1, Unit: 'm/s²', Digits: 1 }]
    },
    {
        Id: 'Lift', Name: 'Lift zone', Contribution: 'Accelerate', Glyph: 'Lift', Colour: '#d8f25a', Native: 'lift', Reach: 'Column',
        Blurb: 'Bounded upward acceleration column fading smoothly to zero at its outer boundary.',
        Defaults: { Strength: 8, Radius: 130, Height: 240, Falloff: 'Smooth', CentreY: 0 },
        Parameters: [{ Key: 'Strength', Label: 'Upward acceleration', Minimum: 0, Maximum: 40, Step: 0.1, Unit: 'm/s²', Digits: 1 }]
    },
    {
        Id: 'Magnetic', Name: 'Magnetic dipole', Contribution: 'Accelerate', Glyph: 'Magnet', Colour: '#ef63d8', Native: 'magnetic', Reach: 'Sphere',
        Blurb: 'Dipole field lines along world Z, bounded inside a finite spherical region.',
        Defaults: { Strength: 6, Radius: 160, Falloff: 'Smooth', CentreY: 10 },
        Parameters: [{ Key: 'Strength', Label: 'Dipole drive', Minimum: 0, Maximum: 30, Step: 0.1, Unit: 'm/s²', Digits: 1 }]
    },
    {
        Id: 'Gravity', Name: 'Gravity zone', Contribution: 'Accelerate', Glyph: 'Gravity', Colour: '#f5b041', Native: 'gravity', Reach: 'Sphere',
        Blurb: 'Localized gravity well or low-g pocket strictly bounded to its finite sphere/box influence.',
        Defaults: { Strength: 9.81, Radius: 280, Falloff: 'Smooth', CentreY: 0 },
        Parameters: [{ Key: 'Strength', Label: 'Gravity', Minimum: 0, Maximum: 30, Step: 0.01, Unit: 'm/s²', Digits: 2 }]
    },
    {
        Id: 'Drag', Name: 'Drag volume', Contribution: 'Damp', Glyph: 'Damp', Colour: '#c084fc', Native: 'drag', Reach: 'Box',
        Blurb: 'Exponential damping zone — slows particles, gusts, and debris smoothly inside its oriented volume.',
        Defaults: { Strength: 1.2, HalfX: 120, HalfZ: 80, Height: 60, Radius: 120, Falloff: 'Smooth', Bearing: 0 },
        Parameters: [{ Key: 'Strength', Label: 'Damping rate', Minimum: 0, Maximum: 10, Step: 0.01, Unit: '1/s', Digits: 2 }]
    }
];

export const TypeById = Object.fromEntries(Types.map((Type) => [Type.Id, Type]));

export function ResolveSourceColour(SourceOrTypeId)
{
    const Key = typeof SourceOrTypeId === 'string' ? SourceOrTypeId : SourceOrTypeId?.Type;
    return TypeById[Key]?.Colour || Contributions[TypeById[Key]?.Contribution]?.Colour || '#8fd3c4';
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 FIELD CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

let TokenCounter = 1;

export function ConstructSource(TypeId, Overrides)
{
    const Type = TypeById[TypeId] || TypeById.WindGenerator;
    const Defaults = Type.Defaults;
    const Source = Object.assign({
        Token: 'src_' + (TokenCounter++).toString(36) + Math.floor(Math.random() * 1e6).toString(36),
        Type: Type.Id,
        Name: Type.Name,
        Enabled: true,
        Centre: [0, Defaults.CentreY ?? 0, 0],
        Reach: Type.Reach === 'Everywhere' ? 'Sphere' : Type.Reach,
        Radius: Defaults.Radius ?? 180,
        Height: Defaults.Height ?? 180,
        HalfX: Defaults.HalfX ?? 120,
        HalfZ: Defaults.HalfZ ?? 120,
        Falloff: Defaults.Falloff || 'Smooth',
        Activation: 'Always',
        Begins: 0,
        Lasts: 0,
        Repeats: 0,
        RampIn: 1.25,
        RampOut: 2,
        TriggeredAt: null,
        Acts: []
    }, JSON.parse(JSON.stringify(Defaults)));
    delete Source.CentreY;
    // Clamp any imported old 'Everywhere' saves to finite Sphere bounds
    if (Overrides) Object.assign(Source, Overrides);
    if (Source.Reach === 'Everywhere') Source.Reach = 'Sphere';
    if (!Number.isFinite(Source.Radius) || Source.Radius <= 0) Source.Radius = Defaults.Radius || 200;
    return Source;
}

export function ConstructScenario(ScenarioKey = 'lab')
{
    const BaseField = {
        Enabled: true, IncludeTurbulence: true, Visualize: true, GridResolution: 5, ArrowSeconds: 0.5,
        RegionCentre: [0, 0, 0], RegionHalfExtents: [600, 120, 600],
        Cascades: 4, FinestCell: 2, CellsPerSide: 128, VerticalCells: 32, Extent: 100000,
        Channels: Channels.map(([, Key]) => Key)
    };
    const BaseViz = {
        Contribution: 'Flow', Quantity: 'Speed', Mode: 'Map', ScaleMode: 'Automatic', FixedScale: 30,
        Heatmap: false, HeatmapStyle: 'Smooth', Opacity: 0.72,
        Arrows: true, ArrowSpacing: 54, QuietCutoff: 0.03,
        FlowLines: true, FlowLineSpacing: 46, FlowLineLength: 22, FlowLinePulse: true,
        Tracers: false, TracerCount: 240,
        Cascades: false, Labels: true, MinimalLabels: false, SliceHeight: 10, CellPixels: 6, Solo: null, Follow: null
    };

    if (ScenarioKey === 'corridor')
    {
        return {
            Scenario: 'corridor',
            Wind: {
                Enabled: false, Bounded: true, RegionRadius: 1600,
                Bearing: 45, Speed: 4.5, GustStrength: 0.25, GustFrequency: 0.085, Shear: 0.15, Veer: 8,
                Turbulence: 0.35, TurbulenceScale: 64, Seed: 7331, GroundHeight: 0, MesoscaleScale: 18000, MesoscaleStrength: 0.18,
                AdvectClouds: true, AdvectPlumes: true, AdvectParticles: true
            },
            Field: BaseField,
            Sources: [
                ConstructSource('WindGenerator', { Name: 'West Intake Blower', Centre: [-280, 0, 10], Bearing: 88, Strength: 18, Radius: 240, HalfX: 160, HalfZ: 220, Reach: 'Column' }),
                ConstructSource('WindGenerator', { Name: 'North Crosswind Jet', Centre: [80, 0, -240], Bearing: 168, Strength: 15, Radius: 220, Reach: 'Column' }),
                ConstructSource('Thermal', { Name: 'Exhaust Plume', Centre: [140, 0, 120], Radius: 160, InnerRadius: 36, Strength: 13 }),
                ConstructSource('Drag', { Name: 'Baffle Screen', Centre: [-40, 0, 40], HalfX: 60, HalfZ: 120, Radius: 130, Bearing: 12, Strength: 1.8 })
            ],
            Visualization: Object.assign({}, BaseViz, { Span: 1200 }),
            Timeline: { Duration: 60, Rate: 1, Loop: true }
        };
    }

    if (ScenarioKey === 'storm')
    {
        return {
            Scenario: 'storm',
            Wind: {
                Enabled: true, Bounded: false, RegionRadius: 50000,
                Bearing: 45, Speed: 6.5, GustStrength: 0.25, GustFrequency: 0.085, Shear: 0.15, Veer: 8,
                Turbulence: 0.45, TurbulenceScale: 95, Seed: 7331, GroundHeight: 0, MesoscaleScale: 18000, MesoscaleStrength: 0.22,
                AdvectClouds: true, AdvectPlumes: true, AdvectParticles: true
            },
            Field: BaseField,
            Sources: [
                ConstructSource('Tornado', { Name: 'Mesocyclone', Centre: [-21000, 0, 9000], Strength: 24, InnerRadius: 2600, CollarRadius: 5200, Inflow: 8, Updraft: 12, Height: 9000, Radius: 26000, TrackBearing: 62, TrackSpeed: 12 }),
                ConstructSource('GustFront', { Name: 'Squall line', Centre: [-30000, 0, -14000], Strength: 22, FrontSpeed: 16, CrestDepth: 1800, HalfWidth: 24000, Height: 1500, Bearing: 118, Radius: 26000, HalfX: 24000, HalfZ: 3600 }),
                ConstructSource('Tornado', { Name: 'Tornado', Centre: [0, 0, 220], Radius: 600, Strength: 68, InnerRadius: 20, CollarRadius: 96, TrackSpeed: 3.7 }),
                ConstructSource('Microburst', { Name: 'Microburst', Centre: [1800, 260, -900], Radius: 600 }),
                ConstructSource('Thermal', { Name: 'Thermal', Centre: [-950, 0, 650], Radius: 220 }),
                ConstructSource('Jet', { Name: 'Canyon jet', Centre: [700, 4, 1050], Bearing: 80, Radius: 260 }),
                ConstructSource('Puff', { Name: 'Gust puff', Centre: [-420, 12, -480], Radius: 120 }),
                ConstructSource('Blast', { Name: 'Blast wave', Centre: [320, 2, -320], Radius: 1500, Begins: 6, Lasts: 14, Repeats: 30 }),
                ConstructSource('Wake', { Name: 'Vehicle wake', Centre: [-60, 0.65, -40], Bearing: 20, Radius: 120 }),
                ConstructSource('Attractor', { Name: 'Attractor', Centre: [520, 10, 380], Radius: 260 }),
                ConstructSource('Repulsor', { Name: 'Repulsor', Centre: [-560, 10, -150], Radius: 180 }),
                ConstructSource('Gravity', { Name: 'Gravity zone', Centre: [0, 0, -900], Radius: 420 }),
                ConstructSource('Drag', { Name: 'Drag volume', Centre: [1100, 0, 300], Radius: 180 }),
                ConstructSource('Microburst', { Name: 'Storm outflow', Centre: [26000, 1800, 24000], Strength: 26, Downdraft: 18, InnerRadius: 2400, RingRadius: 7000, Radius: 22000, Activation: 'Trigger' })
            ],
            Visualization: Object.assign({}, BaseViz, { Heatmap: true, Opacity: 0.85, Span: 6000 }),
            Timeline: { Duration: 120, Rate: 1, Loop: true }
        };
    }

    if (ScenarioKey === 'blank')
    {
        return {
            Scenario: 'blank',
            Wind: {
                Enabled: false, Bounded: true, RegionRadius: 1200,
                Bearing: 45, Speed: 5.0, GustStrength: 0.25, GustFrequency: 0.1, Shear: 0.15, Veer: 8,
                Turbulence: 0.35, TurbulenceScale: 60, Seed: 7331, GroundHeight: 0, MesoscaleScale: 18000, MesoscaleStrength: 0.15,
                AdvectClouds: true, AdvectPlumes: true, AdvectParticles: true
            },
            Field: BaseField,
            Sources: [
                ConstructSource('WindGenerator', { Name: 'Wind Generator 1', Centre: [-120, 0, 20], Bearing: 75, Strength: 16, Radius: 210 })
            ],
            Visualization: Object.assign({}, BaseViz, { Span: 900 }),
            Timeline: { Duration: 60, Rate: 1, Loop: true }
        };
    }

    // Default ('lab'): Clean bounded Wind Generator + Vortex + Jet + Attractor + Repulsor on calm dark ground
    return {
        Scenario: 'lab',
        Wind: {
            Enabled: false, Bounded: true, RegionRadius: 1600,
            Bearing: 45, Speed: 4.5, GustStrength: 0.25, GustFrequency: 0.085, Shear: 0.15, Veer: 8,
            Turbulence: 0.38, TurbulenceScale: 65, Seed: 7331, GroundHeight: 0, MesoscaleScale: 18000, MesoscaleStrength: 0.2,
            AdvectClouds: true, AdvectPlumes: true, AdvectParticles: true
        },
        Field: BaseField,
        Sources: [
            ConstructSource('WindGenerator', { Name: 'Wind Generator East', Centre: [-330, 0, 50], Bearing: 66, Strength: 18, Radius: 245, Reach: 'Column' }),
            ConstructSource('Tornado', { Name: 'Vortex Column', Centre: [55, 0, -45], Strength: 42, InnerRadius: 24, CollarRadius: 72, Inflow: 12, Updraft: 24, Radius: 250, TrackSpeed: 0 }),
            ConstructSource('Jet', { Name: 'Jet Corridor', Centre: [-120, 8, -280], Bearing: 108, Length: 180, InnerRadius: 26, Radius: 210 }),
            ConstructSource('Attractor', { Name: 'Attractor', Centre: [340, 10, 130], Strength: 11, Swirl: 4.8, Radius: 210 }),
            ConstructSource('Repulsor', { Name: 'Repulsor', Centre: [230, 10, -230], Strength: 12, Radius: 175 }),
            ConstructSource('Blast', { Name: 'Impulse Shell', Centre: [-60, 2, 260], Strength: 36, Radius: 240, Begins: 3, Lasts: 10, Repeats: 24, Activation: 'Window' })
        ],
        Visualization: Object.assign({}, BaseViz, { Span: 1400 }),
        Timeline: { Duration: 60, Rate: 1, Loop: true }
    };
}

export const ConstructDefaultField = () => ConstructScenario('lab');

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
    const Phase = 2 * Math.PI * Wind.GustFrequency * Time - 0.011 * Along;
    const GustWave = 0.62 * Math.sin(Phase) * Resolvable(2 * Math.PI / 0.011, CellSpan)
                   + 0.38 * Math.sin(2.17 * Phase + 1.13) * Resolvable(2 * Math.PI / (0.011 * 2.17), CellSpan);
    const Speed = Wind.Speed * SpeedGain * (1 + Wind.GustStrength * GustWave) * Influence;
    Out[0] += DirectionX * Speed;
    Out[2] += DirectionZ * Speed;
    if (Settings.IncludeTurbulence && Wind.Turbulence > 0)
    {
        const Visible = Resolvable(Wind.TurbulenceScale * 2, CellSpan);
        if (Visible > 0)
        {
            const Scale = Math.max(4, Wind.TurbulenceScale);
            const U = (X - HeadingX * Drift) / Scale, V = (Z - HeadingZ * Drift) / Scale;
            const Gain = Wind.Turbulence * Wind.Speed * 0.45 * Visible * Influence;
            NoiseGradient(U, V + Y / Scale * 0.37, Wind.Seed, NoiseGradientStore);
            Out[0] += NoiseGradientStore[1] * Gain;
            Out[2] -= NoiseGradientStore[0] * Gain;
            Out[1] += NoiseGradient(U + 71.3, V + 13.9, Wind.Seed + 5, NoiseGradientStore) * Gain * 0.2;
        }
    }
}

export function WindSpeedAt(Wind, Settings, X, Y, Z, Time)
{
    const Out = new Float64Array(3);
    EvaluateWind(Wind, Settings, X, Y, Z, Time, 0.5, Out);
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
    if (Source.Type === 'Puff' && Source.Drift > 0 && Wind.Enabled)
    {
        const AltitudeKm = Math.max(0, Source.Centre[1] - Wind.GroundHeight) / 1000;
        const Turn = (Wind.Bearing + Wind.Veer * AltitudeKm) * Radian;
        const Travel = Wind.Speed * Math.max(0.15, 1 + Wind.Shear * Math.min(AltitudeKm, 12)) * Source.Drift * Age;
        return [Source.Centre[0] + Math.sin(Turn) * Travel, Source.Centre[1], Source.Centre[2] - Math.cos(Turn) * Travel];
    }
    if (Source.Type === 'GustFront')
    {
        const Turn = Source.Bearing * Radian, Travel = Source.FrontSpeed * Age;
        return [Source.Centre[0] + Math.sin(Turn) * Travel, Source.Centre[1], Source.Centre[2] - Math.cos(Turn) * Travel];
    }
    return Source.Centre;
}

export function ResolveBoxExtents(Source)
{
    if (Source.Type === 'Jet') return { HalfX: Source.InnerRadius * 3.2, HalfZ: Source.Length * 0.75, Height: Source.Centre[1] + Source.InnerRadius * 3.2, OffsetZ: Source.Length * 0.5 };
    if (Source.Type === 'Wake') return { HalfX: Source.HalfWidth * 4, HalfZ: Source.Length * 1.2, Height: Source.Height * 2.4, OffsetZ: -Source.Length };
    if (Source.Type === 'GustFront') return { HalfX: Source.HalfWidth * 1.1, HalfZ: Source.CrestDepth * 1.4, Height: Source.Height * 1.25, OffsetZ: -Source.CrestDepth * 1.05 };
    return { HalfX: Math.max(0.5, Source.HalfX), HalfZ: Math.max(0.5, Source.HalfZ), Height: Math.max(1, Source.Height), OffsetZ: 0 };
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
        const Height = Math.max(1, Source.Type === 'Microburst' ? Math.max(Source.Height, (Centre[1] - ColumnFoot) * 1.4) : Source.Height);
        if (DeltaY < -15 || DeltaY > Height * 1.3) return 0;
        const Separation = Math.hypot(DeltaX, DeltaZ);
        if (Separation >= Source.Radius) return 0;
        const Taper = DeltaY <= Height ? 1 : 1 - Smooth01((DeltaY - Height) / (0.3 * Height));
        return Falls(Source.Falloff, Separation, Source.Radius) * Taper;
    }
    const Turn = (Source.Bearing || 0) * Radian;
    const Cosine = Math.cos(Turn), Sine = Math.sin(Turn);
    const Extents = ResolveBoxExtents(Source);
    const LocalX = DeltaX * Cosine + DeltaZ * Sine, LocalZ = -DeltaX * Sine + DeltaZ * Cosine + Extents.OffsetZ;
    const Share = Math.max(Math.abs(LocalX) / Extents.HalfX, Math.abs(LocalZ) / Extents.HalfZ, DeltaY < 0 ? -DeltaY / 5 : DeltaY / Math.max(1, Extents.Height));
    return Share >= 1 ? 0 : Falls(Source.Falloff === 'None' ? 'None' : Source.Falloff, Share, 1);
}

function EvaluateFlowSource(Source, Wind, X, Y, Z, Age, Envelope, Centre, Out, Time)
{
    const Ground = Wind.GroundHeight;
    switch (Source.Type)
    {
    case 'WindGenerator':
    {
        const Turn = (Source.Bearing || 0) * Radian;
        const DirX = Math.sin(Turn), DirZ = -Math.cos(Turn);
        const Along = (X - Centre[0]) * DirX + (Z - Centre[2]) * DirZ;
        const Phase = 2 * Math.PI * (Source.GustFrequency || 0.14) * Time - 0.018 * Along;
        const Gust = 0.65 * Math.sin(Phase) + 0.35 * Math.sin(2.1 * Phase + 0.9);
        const Speed = Source.Strength * Envelope * (1 + (Source.GustStrength || 0) * Gust);
        Out[0] += DirX * Speed;
        Out[2] += DirZ * Speed;
        if ((Source.Turbulence || 0) > 0)
        {
            const Scale = Math.max(6, Source.TurbulenceScale || 42);
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
        const SpanH = Math.max(10, Source.Height);
        if (RelativeY < -15 || RelativeY > SpanH * 1.3) return;
        const HeightShare = Clamp(RelativeY / SpanH, 0, 1);
        const TopTaper = RelativeY <= SpanH ? 1 : 1 - Smooth01((RelativeY - SpanH) / (0.3 * SpanH));
        const DeltaX = X - Centre[0], DeltaZ = Z - Centre[2];
        const Separation = Math.hypot(DeltaX, DeltaZ);
        const InnerRadius = Math.max(1, Source.InnerRadius + (Source.CollarRadius - Source.InnerRadius) * Smooth01(HeightShare));
        const Rho = Separation / InnerRadius;
        const Profile = (2 * Rho) / (1 + Rho * Rho);
        const Tangential = Envelope * TopTaper * Source.Strength * Profile * (Source.Spin < 0 ? -1 : 1);
        const InflowBand = Math.exp(-Math.max(0, RelativeY) / Math.max(12, 0.16 * SpanH)) - 0.35 * Smooth01((HeightShare - 0.72) / 0.28);
        const Radial = -Envelope * TopTaper * Source.Inflow * InflowBand * Profile;
        const Updraft = Envelope * TopTaper * Source.Updraft * Math.exp(-0.42 * Rho * Rho) * Smooth01(RelativeY / 8 + 0.15);
        const UnitX = Separation > 1e-5 ? DeltaX / Separation : 1, UnitZ = Separation > 1e-5 ? DeltaZ / Separation : 0;
        const Turn = Source.TrackBearing * Radian, Carry = Envelope * 0.25 * Source.TrackSpeed;
        Out[0] += UnitZ * Tangential + UnitX * Radial + Math.sin(Turn) * Carry;
        Out[1] += Updraft;
        Out[2] += -UnitX * Tangential + UnitZ * Radial - Math.cos(Turn) * Carry;
        return;
    }
    case 'GustFront':
    {
        const Turn = Source.Bearing * Radian, PropagationX = Math.sin(Turn), PropagationZ = -Math.cos(Turn);
        const Behind = (Centre[0] - X) * PropagationX + (Centre[2] - Z) * PropagationZ;
        const Lateral = Math.abs((X - Centre[0]) * -PropagationZ + (Z - Centre[2]) * PropagationX);
        const Depth = Math.max(5, Source.CrestDepth), RelativeY = Y - Centre[1];
        if (Behind < -0.25 * Depth || Behind > 2.4 * Depth || RelativeY < 0 || RelativeY > Source.Height * 1.25 || Lateral > Source.HalfWidth * 1.2) return;
        const NoseRise = Smooth01((Behind + 0.25 * Depth) / (0.35 * Depth));
        const WakeDecay = Math.exp(-Math.max(0, Behind - 0.1 * Depth) / Depth);
        const HeightEnvelope = 1 - Smooth01(RelativeY / Math.max(5, Source.Height));
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
        const ColumnRadius = Math.max(5, Source.InnerRadius), Ring = Math.max(10, Source.RingRadius);
        if (RelativeY < 0 || RelativeY > Math.max(40, Centre[1] * 1.4)) return;
        const DowndraftColumn = Math.exp(-(Separation * Separation) / (ColumnRadius * ColumnRadius)) * Smooth01(RelativeY / 35);
        const OutRing = (Separation / Ring) * Math.exp(-0.5 * Math.pow(Separation / Ring, 2)) * Math.exp(-RelativeY / Math.max(42, Ring * 0.28));
        const Inverse = Separation > 1e-4 ? 1 / Separation : 0;
        Out[0] += Envelope * Source.Strength * OutRing * DeltaX * Inverse;
        Out[1] -= Envelope * Source.Downdraft * DowndraftColumn;
        Out[2] += Envelope * Source.Strength * OutRing * DeltaZ * Inverse;
        return;
    }
    case 'Thermal':
    {
        const RelativeY = Y - Centre[1], ColumnHeight = Math.max(15, Source.Height);
        if (RelativeY < 0 || RelativeY > ColumnHeight * 1.35) return;
        const HeightShare = Clamp(RelativeY / ColumnHeight, 0, 1.35);
        const DeltaX = X - Centre[0], DeltaZ = Z - Centre[2], Separation = Math.hypot(DeltaX, DeltaZ);
        const PlumeRadius = Math.max(2, Source.InnerRadius * (1 + 0.45 * HeightShare));
        const Rho = Separation / PlumeRadius;
        if (Rho >= 3.5) return;
        const ChimneyEnvelope = Envelope * Math.exp(-0.5 * Rho * Rho) * Smooth01(RelativeY / 10 + 0.15) * (1 - Smooth01((HeightShare - 1) / 0.35));
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
        const Turn = Source.Bearing * Radian, AxisX = Math.sin(Turn), AxisZ = -Math.cos(Turn);
        const RelativeX = X - Centre[0], RelativeY = Y - Centre[1], RelativeZ = Z - Centre[2];
        const Axial = RelativeX * AxisX + RelativeZ * AxisZ;
        const Length = Math.max(2, Source.Length), CorridorRadius = Math.max(0.5, Source.InnerRadius);
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
        const RadiusXZ = Math.max(1.5, Source.Radius), RadiusY = Math.max(1, Source.RadiusY);
        const U = (X - Centre[0]) / RadiusXZ, V = (Y - Centre[1]) / RadiusY, W = (Z - Centre[2]) / RadiusXZ;
        const Squared = U * U + V * V + W * W;
        if (Squared >= 9) return;
        const Gauss = Envelope * Math.exp(-0.5 * Squared);
        const Turn = Source.Bearing * Radian;
        Out[0] += Math.sin(Turn) * Source.Strength * Gauss;
        Out[1] += Source.Lift * Gauss;
        Out[2] += -Math.cos(Turn) * Source.Strength * Gauss;
        return;
    }
    case 'Blast':
    {
        const InitialRadius = Math.max(0.5, Source.InitialRadius);
        const FrontRadius = InitialRadius + Math.max(0, Source.Expansion) * Age;
        const Thickness = Math.max(0.5, Source.Thickness);
        const DeltaX = X - Centre[0], DeltaY = Y - Centre[1], DeltaZ = Z - Centre[2];
        const Separation = Math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
        const FrontShare = (Separation - FrontRadius) / Thickness;
        if (Math.abs(FrontShare) >= 3.2 || Separation < 1e-4) return;
        const Attenuation = Math.pow(InitialRadius / Math.max(InitialRadius, FrontRadius), 0.65);
        const Outward = Source.Strength * Envelope * Attenuation * Math.exp(-0.5 * FrontShare * FrontShare) / Separation;
        Out[0] += DeltaX * Outward;
        Out[1] += DeltaY * Outward;
        Out[2] += DeltaZ * Outward;
        return;
    }
    case 'Wake':
    {
        const Turn = Source.Bearing * Radian, ForwardX = Math.sin(Turn), ForwardZ = -Math.cos(Turn);
        const RightX = -ForwardZ, RightZ = ForwardX;
        const RelativeX = X - Centre[0], RelativeY = Y - Ground, RelativeZ = Z - Centre[2];
        const Trail = -(RelativeX * ForwardX + RelativeZ * ForwardZ);
        const LateralDistance = RelativeX * RightX + RelativeZ * RightZ;
        const WakeLength = Math.max(4, Source.Length), HalfWidth = Math.max(0.4, Source.HalfWidth), WakeHeight = Math.max(0.8, Source.Height);
        if (Trail < -0.5 * HalfWidth || Trail > 2.2 * WakeLength || RelativeY < 0 || RelativeY > 2.4 * WakeHeight) return;
        const Spread = 1 + 0.85 * Math.max(0, Trail) / WakeLength;
        const LongEnvelope = Smooth01((Trail + 0.5 * HalfWidth) / HalfWidth) * Math.exp(-Math.max(0, Trail) / WakeLength);
        const LateralShare = LateralDistance / (HalfWidth * Spread);
        const Weight = Envelope * LongEnvelope * Math.exp(-0.5 * LateralShare * LateralShare) * (1 - Smooth01(RelativeY / (1.8 * WakeHeight * Spread)));
        const Upwash = Math.max(0, (1 - 0.8 * LateralShare * LateralShare) * Source.Circulation);
        const InwardRoll = -LateralShare * 0.65 * Source.Circulation;
        const Tow = Source.Strength * Math.min(1, Source.Speed / 22);
        Out[0] += Weight * (ForwardX * Tow + RightX * InwardRoll);
        Out[1] += Weight * Upwash;
        Out[2] += Weight * (ForwardZ * Tow + RightZ * InwardRoll);
        return;
    }
    case 'Current':
    {
        const Turn = Source.Bearing * Radian;
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

export function PrepareSource(Source, Wind, Time)
{
    const Type = TypeById[Source.Type];
    if (!Type) return null;
    const Activation = ResolveActivation(Source, Time);
    const Centre = ResolveCentre(Source, Wind, Activation.Age);
    let BoundX = Centre[0], BoundZ = Centre[2], Bound = Source.Radius || 200;
    if (Source.Type === 'GustFront' || Source.Reach === 'Box')
    {
        const Extents = ResolveBoxExtents(Source), Turn = (Source.Bearing || 0) * Radian;
        BoundX += Math.sin(Turn) * Extents.OffsetZ;
        BoundZ -= Math.cos(Turn) * Extents.OffsetZ;
        Bound = Math.hypot(Extents.HalfX, Extents.HalfZ);
    }
    else if (Source.Type === 'Blast') Bound = Math.min(Source.Radius, Source.InitialRadius + Source.Expansion * Activation.Age + 3.2 * Source.Thickness);
    return { Source, Type, Contribution: Type.Contribution, Envelope: Activation.Envelope, Age: Activation.Age, Status: Activation.Status,
             Centre, BoundX, BoundZ, BoundSquared: Bound * Bound };
}

export function Prepare(Scene, Time)
{
    const Entries = [];
    for (const Source of Scene.Sources)
    {
        const Entry = PrepareSource(Source, Scene.Wind, Time);
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
    // Strict finite spatial reach for ALL force types (no infinite source spillover)
    const Share = ResolveReach(Source, Centre, X, Y, Z, Wind.GroundHeight);
    if (Share <= 0) return false;
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

export function EvaluatePrepared(Scene, Prepared, Contribution, X, Y, Z, CellSpan, Out, Solo)
{
    if (!Scene.Field.Enabled) return true;
    if (Contribution === 'Flow' && (!Solo || Solo === 'Wind')) EvaluateWind(Scene.Wind, Scene.Field, X, Y, Z, Prepared.Time, CellSpan, Out);
    if (Solo === 'Wind') return true;
    for (const Entry of Prepared.Entries)
    {
        if (Entry.Contribution !== Contribution) continue;
        if (Solo && Entry.Source.Token !== Solo) continue;
        EvaluateEntry(Entry, Scene.Wind, X, Y, Z, Out, Prepared.Time);
    }
    return true;
}

export function Evaluate(Scene, Contribution, X, Y, Z, Time, CellSpan, Out, Solo)
{
    return EvaluatePrepared(Scene, Prepare(Scene, Time), Contribution, X, Y, Z, CellSpan, Out, Solo);
}

export function ResolveProfile(Scene, Source, SliceHeight)
{
    const Type = TypeById[Source.Type] || TypeById.WindGenerator;
    const Reach = Source.Type === 'GustFront' ? Source.CrestDepth * 3 : Source.Type === 'Blast' ? Math.max(Source.Radius * 0.4, 60) :
                  Source.Reach === 'Box' ? Math.max(ResolveBoxExtents(Source).HalfX, ResolveBoxExtents(Source).HalfZ) * 1.15 : Source.Radius * 1.15;
    const Points = [];
    const Probe = Object.assign({}, Source, { Enabled: true, Activation: 'Always', TrackSpeed: 0, Drift: 0, FrontSpeed: 0 });
    const Steps = 72;
    for (let Step = 0; Step <= Steps; ++Step)
    {
        const Separation = Step / Steps * Reach;
        const Out = new Float64Array(3);
        let X = Source.Centre[0] + Separation, Z = Source.Centre[2];
        if (Source.Type === 'GustFront') { X = Source.Centre[0] - Math.sin(Source.Bearing * Radian) * (Separation - Reach * 0.2); Z = Source.Centre[2] + Math.cos(Source.Bearing * Radian) * (Separation - Reach * 0.2); }
        else if (Source.Type === 'Jet' || Source.Type === 'Current' || Source.Type === 'Wake' || Source.Type === 'Drag' || Source.Type === 'WindGenerator')
        {
            const Axis = Source.Type === 'Wake' ? (Source.Bearing || 0) + 180 : Source.Type === 'Jet' ? (Source.Bearing || 0) : (Source.Bearing || 0) + 90;
            X = Source.Centre[0] + Math.sin(Axis * Radian) * Separation;
            Z = Source.Centre[2] - Math.cos(Axis * Radian) * Separation;
        }
        const Height = Source.Type === 'Blast' || Type.Contribution !== 'Flow' ? Source.Centre[1] : Source.Type === 'Puff' || Source.Type === 'Jet' ? Source.Centre[1] : SliceHeight;
        const BlastTime = Source.Type === 'Blast' ? Math.max(0, (Reach * 0.5 - Source.InitialRadius) / Math.max(1, Source.Expansion)) : 0;
        EvaluateSource(Probe, Scene.Wind, X, Height, Z, BlastTime, Out);
        const Reading = Type.Contribution === 'Damp' ? Out[0] : Math.hypot(Out[0], Out[1], Out[2]);
        Points.push([Separation, Reading]);
    }
    return { Points, Reach, Unit: Contributions[Type.Contribution].Unit };
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

function EncodeSource(Source, Lines)
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
}

export function EncodeLevelspace(Scene, Only)
{
    const Lines = [];
    const { Field, Wind } = Scene;
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
            `BoundedRegion = ${Boolean(Wind.Bounded)}`, `RegionRadiusMetres = ${FormatNumber(Wind.RegionRadius || 1600)}`);
    }
    if (!Only) for (const Source of Scene.Sources) EncodeSource(Source, Lines);
    else if (Only !== 'Field' && Only !== 'Wind') { const Source = Scene.Sources.find((One) => One.Token === Only); if (Source) { EncodeSource(Source, Lines); Lines.shift(); } }
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

export function ResolveBudget(Scene)
{
    const Baked = [], Live = [], Damp = [];
    for (const Source of Scene.Sources)
    {
        const Type = TypeById[Source.Type];
        if (!Type) continue;
        if (Type.Contribution === 'Damp') Damp.push(Source);
        else if (Type.Contribution === 'Flow' && !Source.Acts.length) Baked.push(Source);
        else Live.push(Source);
    }
    const Field = Scene.Field;
    const CascadeBytes = Field.CellsPerSide * Field.CellsPerSide * Field.VerticalCells * 8;
    const Levels = [];
    for (let Level = 0; Level < Field.Cascades; ++Level)
    {
        const Cell = Field.FinestCell * Math.pow(8, Level);
        Levels.push({ Cell, Extent: Cell * Field.CellsPerSide, Refresh: Math.pow(4, Level) });
    }
    return { Baked, Live, Damp, CascadeBytes, TotalBytes: CascadeBytes * Field.Cascades, Levels, GpuSlots: 16 };
}

export const ForceFieldCatalogue = {
    Contributions, Channels, Reaches, Falloffs, Activations, Types, TypeById, Falls, ResolveSourceColour,
    ConstructSource, ConstructDefaultField, ConstructScenario,
    EvaluateWind, WindSpeedAt, ResolveActivation, ResolveCentre, ResolveReach, ResolveBoxExtents,
    EvaluateSource, Evaluate, Prepare, PrepareSource, EvaluatePrepared, EvaluateEntry,
    ResolveProfile, EncodeLevelspace, ExistingKeys, ResolveBeaufort, ResolveBudget, Smooth01, Radian
};

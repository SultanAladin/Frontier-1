/*==========================================================================================================================================
                                                          PHOTOMETRYENGINE.JS
==========================================================================================================================================*/
// 💡 Frontier · Photometric & Light Studio Engine:
//    LM-63 Type-C .ies parser & sampler (734ca2e5/Photometry.js) + Project Zero analytical photometric presets
//    + Blackbody Kelvin chromaticity + 7 Light Type specifications + Environment Lights (Sun, HDRI Dome, Moon, Stars, Fog, Lens Flare, Post).

export const PhotometrySize = Object.freeze({ Azimuth: 73, Polar: 91 }); // 5 deg x 2 deg

export const MeasuredIesProfileNames = Object.freeze([
    'LowBeam',
    'HighBeam',
    'FogBeam',
    'Roadway',
    'Area',
    'RoofFlood',
    'RoofSpot'
]);

export const PresetIesNames = Object.freeze([
    'Downlight',
    'Wall wash',
    'Batwing',
    'ECE Low Beam',
    'SAE Low Beam',
    'High Beam',
    'Fog Lamp',
    'Parking Lamp'
]);

export const PresetToMeasuredMap = Object.freeze({
    'ECE Low Beam': 'LowBeam',
    'SAE Low Beam': 'LowBeam',
    'High Beam': 'HighBeam',
    'Fog Lamp': 'FogBeam',
    'Wall wash': 'Roadway',
    'Downlight': 'RoofSpot',
    'Batwing': 'Area',
    'Parking Lamp': 'RoofFlood'
});

const Numbers = /[-+]?(?:\d*\.\d+|\d+\.?\d*)(?:[eE][-+]?\d+)?/g;

export const Clamp = (V, Lo, Hi) => Math.min(Hi, Math.max(Lo, V));

export function KelvinToRgb(Kelvin)
{
    const Heat = Clamp(Kelvin, 1000, 40000) / 100;
    const R = Heat <= 66 ? 255 : 329.698727446 * Math.pow(Heat - 60, -0.1332047592);
    const G = Heat <= 66
        ? 99.4708025861 * Math.log(Math.max(1, Heat)) - 161.1195681661
        : 288.1221695283 * Math.pow(Heat - 60, -0.0755148492);
    const B = Heat >= 66 ? 255 : Heat <= 19 ? 0 : 138.5177312231 * Math.log(Math.max(1, Heat - 10)) - 305.0447927307;
    return [Clamp(Math.round(R), 0, 255), Clamp(Math.round(G), 0, 255), Clamp(Math.round(B), 0, 255)];
}

export function ResolveEmissionRgb(Props)
{
    const Hex = /^#[0-9a-f]{6}$/i.test(Props.color) ? Props.color : '#ffffff';
    const Base = [1, 3, 5].map(Idx => parseInt(Hex.slice(Idx, Idx + 2), 16));
    const Temp = Props.temperature ? KelvinToRgb(Number(Props.temperature)) : [255, 255, 255];
    return Base.map((C, I) => Math.round((C * Temp[I]) / 255));
}

//------------------------------------------------------------------------------------------------------------------------
//                                           LM-63 TYPE C .IES PARSER & SAMPLER
//------------------------------------------------------------------------------------------------------------------------

export function ParseIes(Text)
{
    const Lines = Text.replace(/\r/g, '').split('\n');
    const Tilt = Lines.findIndex(Line => /^TILT\s*=/.test(Line.trim().toUpperCase()));
    if (Tilt < 0 || Lines[Tilt].trim().toUpperCase() !== 'TILT=NONE')
        throw new Error('IES requires LM-63 TILT=NONE');
    const Data = (Lines.slice(Tilt + 1).join(' ').match(Numbers) || []).map(Number);
    if (Data.length < 13)
        throw new Error('IES photometric header is incomplete');
    const [Lamps, , Multiplier, VerticalCount, HorizontalCount, Kind, Units] = Data;
    if (!Number.isInteger(Lamps) || Lamps < 1 || !Number.isFinite(Multiplier) || Multiplier <= 0
        || !Number.isInteger(VerticalCount) || VerticalCount < 2 || VerticalCount > 181
        || !Number.isInteger(HorizontalCount) || HorizontalCount < 1 || HorizontalCount > 361
        || Kind !== 1 || ![1, 2].includes(Units))
        throw new Error('IES needs a finite Type C photometric grid');
    const Needed = 13 + VerticalCount + HorizontalCount + VerticalCount * HorizontalCount;
    if (Data.length < Needed)
        throw new Error(`IES candela grid is incomplete (${Data.length} numbers, need ${Needed})`);
    const Vertical = Data.slice(13, 13 + VerticalCount);
    const Horizontal = Data.slice(13 + VerticalCount, 13 + VerticalCount + HorizontalCount);
    const Candela = Data.slice(13 + VerticalCount + HorizontalCount, Needed).map(V => V * Multiplier);
    const ValidAngles = (Angles, End) => Angles[0] === 0 && Angles.at(-1) <= End
        && Angles.every((A, Idx) => Number.isFinite(A) && (Idx === 0 || A > Angles[Idx - 1]));
    if (!ValidAngles(Vertical, 180) || !ValidAngles(Horizontal, 360)
        || ![0, 90, 180, 360].includes(Horizontal.at(-1))
        || Candela.some(V => !Number.isFinite(V) || V < 0))
        throw new Error('IES angles must increase monotonically from zero (C-plane 0/90/180/360)');
    const Peak = Candela.reduce((Best, V) => Math.max(Best, V), 0);
    if (!(Peak > 0)) throw new Error('IES candela grid contains no light');
    return { Vertical, Horizontal, Candela, Peak };
}

function InterpolateAngles(Angles, Value)
{
    const Clamped = Clamp(Value, Angles[0], Angles.at(-1));
    let Upper = 1;
    while (Upper < Angles.length - 1 && Angles[Upper] < Clamped) Upper++;
    const Lower = Upper - 1;
    return [Lower, Upper, (Clamped - Angles[Lower]) / Math.max(1e-6, Angles[Upper] - Angles[Lower])];
}

export function SampleIes(Profile, AzimuthDeg, PolarDeg)
{
    let Angle = ((AzimuthDeg % 360) + 360) % 360;
    const Last = Profile.Horizontal.at(-1);
    if (Last === 0) Angle = 0;
    else if (Last === 90) Angle = Math.min(Angle % 180, 180 - (Angle % 180));
    else if (Last === 180) Angle = Math.min(Angle, 360 - Angle);
    const [H0, H1, Ht] = Last === 0 ? [0, 0, 0] : InterpolateAngles(Profile.Horizontal, Angle);
    const [V0, V1, Vt] = InterpolateAngles(Profile.Vertical, PolarDeg);
    const Count = Profile.Vertical.length;
    const A = Profile.Candela[H0 * Count + V0] * (1 - Vt) + Profile.Candela[H0 * Count + V1] * Vt;
    const B = Profile.Candela[H1 * Count + V0] * (1 - Vt) + Profile.Candela[H1 * Count + V1] * Vt;
    return (A * (1 - Ht) + B * Ht) / Profile.Peak;
}

export function SampleAnalyticalPolar(ProfileName, AngleRad, ConeDeg, Section = 0)
{
    const Scaled = AngleRad * (60 / Math.max(5, ConeDeg));
    if (Math.abs(Scaled) > Math.PI * 0.5) return 0;
    const Axis = Math.max(0, Math.cos(Scaled));
    if (ProfileName === 'Batwing') return Math.pow(Math.abs(Math.sin(Scaled * 2)), 1.4) * Axis;
    if (ProfileName === 'Wall wash') return Math.pow(Math.max(0, Math.cos(Scaled - 0.55)), Section ? 3 : 5);
    const Power = ProfileName === 'Fog Lamp' ? 0.65 : ProfileName === 'Parking Lamp' ? 0.4 : (ProfileName === 'High Beam' ? 240 : 130) / 60;
    let Lobe = Math.pow(Axis, Math.max(0.4, Power * (Section ? 0.65 : 1)));
    if (ProfileName.includes('Low Beam'))
        Lobe *= Math.sin(Scaled) > 0 ? (ProfileName.startsWith('SAE') ? 0.5 : 0.65) : 1;
    return Lobe;
}

export function EvaluateIesIntensity(Props, LoadedProfiles, AzimuthDeg, PolarDeg)
{
    const CustomKey = Props.customIesKey;
    if (CustomKey && LoadedProfiles?.[CustomKey])
        return SampleIes(LoadedProfiles[CustomKey], AzimuthDeg, PolarDeg);
    if (Props.useMeasuredIes && Props.measuredProfile && LoadedProfiles?.[Props.measuredProfile])
        return SampleIes(LoadedProfiles[Props.measuredProfile], AzimuthDeg, PolarDeg);
    const Section = (Math.abs(AzimuthDeg - 90) < 45 || Math.abs(AzimuthDeg - 270) < 45) ? 1 : 0;
    const Sign = (AzimuthDeg > 90 && AzimuthDeg < 270) ? -1 : 1;
    return SampleAnalyticalPolar(Props.profile || 'Downlight', (Sign * PolarDeg * Math.PI) / 180, Number(Props.cone ?? 58), Section);
}

export function EncodeSyntheticIesFile(Label, Props, LoadedProfiles)
{
    const VAngles = [];
    for (let v = 0; v <= 180; v += 5) VAngles.push(v);
    const HAngles = [0, 30, 60, 90, 120, 150, 180, 210, 240, 270, 300, 330, 360];
    const Lumens = Math.max(100, Number(Props.lumens ?? 1650) * Number(Props.multiplier ?? 1));
    const Rows = HAngles.map(H =>
        VAngles.map(V => (EvaluateIesIntensity(Props, LoadedProfiles, H, V) * Lumens * 0.65).toFixed(3)).join(' ')
    );
    return [
        'IESNA:LM-63-2002',
        `[TEST] Frontier Photometric Studio export`,
        `[LUMINAIRE] ${Label}`,
        'TILT=NONE',
        `1 ${Lumens.toFixed(1)} 1 ${VAngles.length} ${HAngles.length} 1 2 0.25 0.25 0.12`,
        '1 1 60',
        VAngles.join(' '),
        HAngles.join(' '),
        ...Rows
    ].join('\n');
}

//------------------------------------------------------------------------------------------------------------------------
//                                     CATALOGUE, LIGHT SPECS & ENVIRONMENT LIGHTS
//------------------------------------------------------------------------------------------------------------------------

export const LightCatalogue = Object.freeze({
    pointlight: { Label: 'Point Light', ShortLabel: 'Point', Glyph: 'PointLight', IconFile: 'editor-point-light', Accent: '#ffd9a0', Unit: 'cd' },
    spotlight:  { Label: 'Spot Light', ShortLabel: 'Spot', Glyph: 'SpotLight', IconFile: 'editor-spotlight', Accent: '#e8f0ff', Unit: 'cd' },
    ieslight:   { Label: 'IES Photometric', ShortLabel: 'IES', Glyph: 'IesLight', IconFile: 'editor-dome-light', Accent: '#e8c27a', Unit: 'lm' },
    arealight:  { Label: 'Area / Softbox', ShortLabel: 'Area', Glyph: 'AreaLight', IconFile: 'editor-area-light', Accent: '#fff1d6', Unit: 'lm' },
    tubelight:  { Label: 'Tube Light', ShortLabel: 'Tube', Glyph: 'TubeLight', IconFile: 'light-area-2d', Accent: '#bfe3ff', Unit: 'lm' },
    ledlight:   { Label: 'LED Emitter', ShortLabel: 'LED', Glyph: 'LedLight', IconFile: 'light-point-2d', Accent: '#d8ef9f', Unit: 'lm' },
    ledstrip:   { Label: 'LED Strip', ShortLabel: 'Strip', Glyph: 'LedStrip', IconFile: 'slate-ring-light', Accent: '#ffcf99', Unit: 'lm' }
});

export const MoonCatalogue = Object.freeze([
    { Id: 'Luna',    Name: 'Luna',    TexFile: 'luna_2k.jpg',    Subtitle: 'Silicate highlands & maria albedo 0.12', Tint: '#dce2ec', Core: '#9ca4b4', Cr: '#59606f', NaturalDeg: 0.52 },
    { Id: 'Ember',   Name: 'Ember',   TexFile: 'ember_2k.jpg',   Subtitle: 'Basalt magma crust · warm volcanic scatter', Tint: '#f0b08a', Core: '#b55635', Cr: '#4f2218', NaturalDeg: 1.60 },
    { Id: 'Glacier', Name: 'Glacier', TexFile: 'glacier_2k.jpg', Subtitle: 'Europa lineated water-ice albedo 0.67', Tint: '#dcebf7', Core: '#9ab6cc', Cr: '#7a4f3b', NaturalDeg: 1.20 },
    { Id: 'Sulfur',  Name: 'Sulfur',  TexFile: 'sulfur_2k.jpg',  Subtitle: 'Io sulfuric caldera plumes · high frost reflectance', Tint: '#ece188', Core: '#bfa745', Cr: '#6c5a26', NaturalDeg: 1.10 },
    { Id: 'Shroud',  Name: 'Shroud',  TexFile: 'shroud_2k.jpg',  Subtitle: 'Titan nitrogen-methane dense haze limb', Tint: '#e5bc82', Core: '#b2864d', Cr: '#7a5d34', NaturalDeg: 2.40 },
    { Id: 'Shard',   Name: 'Shard',   TexFile: 'shard_2k.jpg',   Subtitle: 'Fractured carbonaceous chondrite moonlet', Tint: '#a7aab0', Core: '#686b72', Cr: '#33363c', NaturalDeg: 0.60 }
]);

export function ComputeFlux(Type, Props)
{
    const R = (K, D = 0) => Number.isFinite(Number(Props[K])) ? Number(Props[K]) : D;
    if (Type === 'ledlight') return Math.max(0, R('watts', 10) * R('efficacy', 110) * R('dimmer', 1));
    if (Type === 'ledstrip') return Math.max(0, R('lumensPerMetre', 1000) * R('length', 2.4) * R('dimmer', 1));
    if (Type === 'pointlight' || Type === 'spotlight') return Math.max(0, R('intensity', 14));
    return Math.max(0, R('lumens', 1650));
}

export function CreateDefaultLightRig()
{
    return [
        {
            Id: 'light-ece-low-beam',
            Name: 'ECE Low Beam',
            Type: 'ieslight',
            Visible: true,
            Locked: false,
            Props: {
                pos: [-1.15, 0.88, 3.6], target: [-0.35, 0.25, -4.5], rot: [-6.0, -8.0, 0], scale: [1, 1, 1],
                profile: 'ECE Low Beam', measuredProfile: 'LowBeam', useMeasuredIes: true, customIesKey: null,
                lumens: 1650, multiplier: 1.0, range: 120, cone: 58, cutoff: -1.0,
                color: '#fff2cf', temperature: 4300, shadows: true, showDistribution: true
            }
        },
        {
            Id: 'light-key-spot',
            Name: 'Key Spot',
            Type: 'spotlight',
            Visible: true,
            Locked: false,
            Props: {
                pos: [-3.2, 4.6, 3.4], target: [0.0, 0.7, -0.5], rot: [-42, 40, 0], scale: [1, 1, 1],
                intensity: 88, angle: 30, penumbra: 0.42, distance: 32,
                color: '#e8f0ff', shadows: true, showCone: true
            }
        },
        {
            Id: 'light-ies-downlight',
            Name: 'IES Architectural Downlight',
            Type: 'ieslight',
            Visible: true,
            Locked: false,
            Props: {
                pos: [1.6, 4.1, -1.8], target: [1.6, 0.0, -1.8], rot: [-90, 0, 0], scale: [1, 1, 1],
                profile: 'Batwing', measuredProfile: 'Area', useMeasuredIes: false, customIesKey: null,
                lumens: 2200, multiplier: 1.15, range: 36, cone: 68, cutoff: 0.0,
                color: '#fff4df', temperature: 3500, shadows: true, showDistribution: true
            }
        },
        {
            Id: 'light-rim-point',
            Name: 'Rim Point',
            Type: 'pointlight',
            Visible: true,
            Locked: false,
            Props: {
                pos: [3.4, 2.2, -2.6], rot: [0, 0, 0], scale: [1, 1, 1],
                intensity: 24, distance: 26, decay: 2.0,
                color: '#ffd9a0', shadows: false, gizmoGlow: true
            }
        },
        {
            Id: 'light-softbox',
            Name: 'Studio Softbox',
            Type: 'arealight',
            Visible: true,
            Locked: false,
            Props: {
                pos: [2.8, 3.1, 2.2], target: [0, 0.8, 0], rot: [-32, -38, 0], scale: [1, 1, 1],
                aperture: 'Rectangle', width: 2.0, height: 1.0, lumens: 2400, spread: 120,
                color: '#fff1d6', twoSided: false, shadows: false, showShape: true
            }
        },
        {
            Id: 'light-studio-tube',
            Name: 'Studio Tube',
            Type: 'tubelight',
            Visible: false,
            Locked: false,
            Props: {
                pos: [-2.4, 2.5, -1.2], rot: [0, 18, 0], scale: [1, 1, 1],
                length: 1.8, radius: 0.04, lumens: 1800, distance: 24,
                color: '#e8f2ff', temperature: 5600, shadows: false, showShape: true
            }
        },
        {
            Id: 'light-led-cob',
            Name: 'LED COB Module',
            Type: 'ledlight',
            Visible: false,
            Locked: false,
            Props: {
                pos: [0.0, 3.3, 1.4], rot: [-75, 0, 0], scale: [1, 1, 1],
                watts: 18, efficacy: 125, dimmer: 1.0, diameter: 40, angle: 110,
                color: '#ffffff', temperature: 4000, shadows: true, showShape: true
            }
        },
        {
            Id: 'light-led-strip',
            Name: 'Baseboard LED Strip',
            Type: 'ledstrip',
            Visible: true,
            Locked: false,
            Props: {
                pos: [0.0, 0.14, -4.8], rot: [0, 0, 0], scale: [1, 1, 1],
                length: 3.6, lumensPerMetre: 1000, wattsPerMetre: 14.4, ledsPerMetre: 60,
                voltage: 24, dimmer: 0.9, diffuser: true,
                color: '#ffdcb4', temperature: 3000, shadows: false, showShape: true
            }
        }
    ];
}

export function CreateDefaultEnvironmentLights()
{
    return {
        EnvDome: {
            Enabled: true,
            Mode: 'Procedural Sky', // 'Procedural Sky' | 'HDRI Meridian' | 'Overcast Softbox' | 'Night Starlight'
            SkylightIntensity: 1.15,
            DiffuseMultiplier: 1.0,
            ReflectionGain: 1.1,
            RotationDeg: 35,
            ZenithColor: '#3d6394',
            HorizonColor: '#98adc4',
            GroundColor: '#252624',
            TemperatureKelvin: 6500,
            CubemapResolution: 256
        },
        Moon: {
            Slot: 0, // 0..5 index into MoonCatalogue
            Visible: true,
            FollowSky: false,
            PhaseDays: 8.9, // 0..29.53
            AzimuthDeg: 78,
            ElevationDeg: 28,
            AngularDiameterDeg: 0.52,
            RollDeg: 0,
            PitchDeg: 0,
            Brightness: 1.5,
            EarthshineGlow: 0.70,
            CastMoonlight: true
        },
        Stars: {
            Visible: true,
            LimitingMagnitude: 5.5,
            Brightness: 1.15,
            PointSize: 1.1,
            TwinkleEnabled: true,
            TwinkleDepthPct: 30,
            TwinkleRateHz: 1.2,
            CelestialRotationDeg: 45
        },
        LensFlare: {
            Enabled: true,
            Intensity: 1.0,
            Spread: 1.0,
            Preset: 'Custom layers',
            Anamorphic: true,
            Streaks: false,
            Starburst: true,
            RotationDeg: 0,
            RayPairs: 8,
            GhostShape: 'Hexagon',
            GhostCount: 6,
            GhostBrightness: 0.35,
            GhostSpacing: 1.0,
            Chromatic: 0.60,
            HaloRadius: 0.28,
            HaloBrightness: 0.50,
            HaloWidth: 0.045,
            StreakGain: 1.0,
            PreviewX: 0.28,
            PreviewY: 0.43
        },
        Fog: {
            Enabled: true,
            Mode: 'Height Fog',
            Density: 0.024,
            FalloffHeightM: 400,
            SunScatter: 0.60,
            Tint: '#9eadc1',
            ProbeDistanceM: 120
        },
        Post: {
            Enabled: true,
            ExposureEv: 0.25,
            Tonemap: 'ACES',
            Saturation: 1.04,
            Contrast: 1.02,
            Bloom: true,
            BloomStrength: 0.38,
            BloomThreshold: 0.85,
            LuxHeatmapMin: 0.1,
            LuxHeatmapMax: 500
        }
    };
}

export function SolveMoonMetrics(Moon)
{
    const PhaseFrac = Clamp(Moon.PhaseDays / 29.53, 0, 1);
    const Illumination = 0.5 * (1 - Math.cos(2 * Math.PI * PhaseFrac));
    let PhaseLabel = 'Full moon';
    if (PhaseFrac < 0.04 || PhaseFrac > 0.96) PhaseLabel = 'New moon';
    else if (PhaseFrac < 0.21) PhaseLabel = 'Waxing crescent';
    else if (PhaseFrac < 0.29) PhaseLabel = 'First quarter';
    else if (PhaseFrac < 0.46) PhaseLabel = 'Waxing gibbous';
    else if (PhaseFrac < 0.54) PhaseLabel = 'Full moon';
    else if (PhaseFrac < 0.71) PhaseLabel = 'Waning gibbous';
    else if (PhaseFrac < 0.79) PhaseLabel = 'Last quarter';
    else PhaseLabel = 'Waning crescent';
    const ElRad = (Moon.ElevationDeg * Math.PI) / 180;
    const AzRad = (Moon.AzimuthDeg * Math.PI) / 180;
    const Direction = [
        Math.cos(ElRad) * Math.sin(AzRad),
        Math.sin(ElRad),
        -Math.cos(ElRad) * Math.cos(AzRad)
    ];
    return { PhaseFrac, Illumination, IlluminationPct: Illumination * 100, PhaseLabel, Direction };
}

/*==========================================================================================================================================
                                                         PHOTOMETRICEDITOR.JS
==========================================================================================================================================*/
// Frontier · Photometric & Light Studio Application Controller
// Combines Project Zero 7-Type Physical Light Inspectors + Real LM-63 .ies Goniometric Profiles + Environment Lights
// (HDRI Sky Dome, Sun, Moon, Stars, Lens Flare, Atmosphere/Fog, Post Exposure / Isolux) inside a 3D ACES WebGL2 viewport.

import {
    Clamp,
    MeasuredIesProfileNames,
    PresetIesNames,
    PresetToMeasuredMap,
    LightCatalogue,
    MoonCatalogue,
    ParseIes,
    EncodeSyntheticIesFile,
    ComputeFlux,
    KelvinToRgb,
    CreateDefaultLightRig,
    CreateDefaultEnvironmentLights,
    SolveMoonMetrics
} from './Engine/PhotometryEngine.js';

import {
    EvaluateSolarEphemeris
} from './Engine/CelestialAtmosphereModel.js';

import {
    DrawHeroLightCanvas,
    DrawResponseCurveCanvas,
    DrawIesPolarWebCanvas,
    DrawHdrDomeCanvas,
    DrawMoonPreviewAndDialCanvas,
    DrawStarsAndTwinkleCanvas,
    DrawLensFlareCanvas,
    DrawHeightFogBeamCanvas,
    DrawSolarArcCanvas,
    DrawSunDiscChromaticityCanvas,
    DrawAtmosphereScatteringCanvas
} from './Engine/PhotometricDiagrams.js';

import { PhotometricViewportController } from './Engine/PhotometricViewport.js';

import {
    HydrateIcons,
    CreateSvgGlyph,
    CreateSliderPill,
    CreateTogglePill,
    CreateSelectPill,
    CreatePropertyRow,
    CreateCollapsibleSection
} from './StudioControls.js';

const EnvironmentNodes = [
    { Id: 'env:envdome',    Name: 'HDRI / Sky Dome',     Type: 'envdome',    Glyph: 'EnvDome',    IconFile: 'editor-hdri-light',    Meta: 'IBL · Equirectangular' },
    { Id: 'env:sun',        Name: 'Directional Sun',     Type: 'sun',        Glyph: 'Sun',        IconFile: 'sun',                  Meta: 'NOAA Solar Ephemeris' },
    { Id: 'env:moon',       Name: 'Lunar Bodies',        Type: 'moon',       Glyph: 'Moon',       IconFile: 'moon',                 Meta: '6 Worlds · Phase Dial' },
    { Id: 'env:stars',      Name: 'Star Field',          Type: 'stars',      Glyph: 'Stars',      IconFile: 'outliner-stars',       HeaderIcon: 'stars', Meta: '300 Stars · Twinkle' },
    { Id: 'env:lensflare',  Name: 'Lens Flare',          Type: 'lensflare',  Glyph: 'LensFlare',  IconFile: 'lens-flare',           Meta: 'Anamorphic · Ghosts' },
    { Id: 'env:atmosphere', Name: 'Atmosphere & Fog',    Type: 'atmosphere', Glyph: 'Atmosphere', IconFile: 'sky-scattering',       Meta: 'Rayleigh · Mie · Fog' },
    { Id: 'env:post',       Name: 'Post Exposure & Lux', Type: 'post',       Glyph: 'Post',       IconFile: 'environment-exposure', Meta: 'ACES · Isolux Scale' }
];

function CreatePzIcon(IconFile, Alt = '')
{
    const Img = document.createElement('img');
    Img.className = 'pz-icon';
    Img.src = `./Icons/${IconFile}.svg`;
    Img.alt = Alt;
    return Img;
}

const State = {
    Scenario: 'roadway',
    Lights: CreateDefaultLightRig(),
    Env: CreateDefaultEnvironmentLights(),
    SunProps: {
        Enabled: true,
        Year: 2026,
        Month: 9,
        Day: 10,
        LocalHours: 14.5,
        UtcOffset: 2,
        Latitude: -26.19,
        Longitude: 28.32,
        DiameterDeg: 0.53,
        ColorMode: 'Kelvin',
        TemperatureK: 5600,
        TintHex: '#fff5e4',
        Intensity: 18.0,
        DirectGain: 1.0,
        Rayleigh: 1.0,
        Mie: 1.0,
        MieAnisotropy: 0.78,
        Ozone: 1.2
    },
    SelectedId: 'light-ece-low-beam',
    SearchQuery: '',
    LoadedIes: {},
    SweepPlaying: false,
    SweepTimer: null,
    Logs: [],
    LiveRedrawHandle: null
};

let Viewport = null;
const Dom = {};

function PushLog(Level, Msg)
{
    const Now = new Date();
    const Stamp = Now.toTimeString().slice(0, 8);
    State.Logs.unshift(`[${Stamp}] [${Level}] ${Msg}`);
    if (State.Logs.length > 60) State.Logs.pop();
    if (Dom.LogCountBadge) Dom.LogCountBadge.textContent = String(State.Logs.length);
    if (Dom.ConsoleLogBody) {
        Dom.ConsoleLogBody.innerHTML = State.Logs.map(Line => `<div>${Line}</div>`).join('');
    }
}

function GetSelectedEntity()
{
    const EnvMatch = EnvironmentNodes.find(N => N.Id === State.SelectedId);
    if (EnvMatch) return { Kind: 'env', Node: EnvMatch };
    const LightMatch = State.Lights.find(L => L.Id === State.SelectedId);
    if (LightMatch) return { Kind: 'light', Light: LightMatch };
    return { Kind: 'light', Light: State.Lights[0] };
}

function ComputeSunSummary()
{
    const Ephem = EvaluateSolarEphemeris({
        Year: State.SunProps.Year,
        Month: State.SunProps.Month,
        Day: State.SunProps.Day,
        LocalHours: State.SunProps.LocalHours,
        UtcOffset: State.SunProps.UtcOffset,
        LatitudeDeg: State.SunProps.Latitude,
        LongitudeDeg: State.SunProps.Longitude
    });
    const TintArr = State.SunProps.ColorMode === 'RGB'
        ? [1, 3, 5].map(i => parseInt(State.SunProps.TintHex.slice(i, i + 2), 16) / 255)
        : KelvinToRgb(State.SunProps.TemperatureK).map(v => v / 255);
    return {
        ...Ephem,
        Enabled: State.SunProps.Enabled,
        Intensity: State.SunProps.Intensity * State.SunProps.DirectGain,
        DiameterDeg: State.SunProps.DiameterDeg,
        TintRgb: TintArr
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                           LOAD MEASURED LM-63 .IES PROFILES
//------------------------------------------------------------------------------------------------------------------------

async function LoadBuiltinIesProfiles()
{
    const Results = await Promise.allSettled(
        MeasuredIesProfileNames.map(async (Name) => {
            const Resp = await fetch(`./Profiles/${Name}.ies`);
            if (!Resp.ok) throw new Error(`HTTP ${Resp.status}`);
            const Text = await Resp.text();
            State.LoadedIes[Name] = ParseIes(Text);
            return Name;
        })
    );
    const OkCount = Results.filter(R => R.status === 'fulfilled').length;
    PushLog('IES', `Loaded ${OkCount}/${MeasuredIesProfileNames.length} LM-63 Type-C profiles (LowBeam, HighBeam, FogBeam, Roadway, Area, RoofFlood, RoofSpot).`);
    RefreshAll();
}

//------------------------------------------------------------------------------------------------------------------------
//                                           SCENARIO PRESETS & QUICK PHOTOMETRY
//------------------------------------------------------------------------------------------------------------------------

function ApplyScenario(ScenarioId)
{
    State.Scenario = ScenarioId;
    document.querySelectorAll('#RigTabStrip .doc-tab').forEach(Btn => {
        Btn.classList.toggle('is-active', Btn.dataset.scenario === ScenarioId);
    });

    State.Lights = CreateDefaultLightRig();
    State.Env = CreateDefaultEnvironmentLights();

    if (ScenarioId === 'roadway') {
        State.SunProps.LocalHours = 21.2;
        State.Env.EnvDome.Enabled = true;
        State.Env.EnvDome.SkylightIntensity = 0.25;
        State.Env.EnvDome.ZenithColor = '#0c1526';
        State.Env.EnvDome.HorizonColor = '#1c2b40';
        State.Lights.forEach(L => {
            L.Visible = ['light-ece-low-beam', 'light-led-strip', 'light-rim-point'].includes(L.Id);
        });
        State.SelectedId = 'light-ece-low-beam';
        Viewport?.SetCameraView('roadway');
    } else if (ScenarioId === 'gallery') {
        State.SunProps.LocalHours = 15.0;
        State.Env.EnvDome.SkylightIntensity = 0.72;
        State.Lights.forEach(L => {
            L.Visible = ['light-ies-downlight', 'light-key-spot', 'light-softbox'].includes(L.Id);
        });
        State.SelectedId = 'light-ies-downlight';
        Viewport?.SetCameraView('orbit');
    } else if (ScenarioId === 'studio') {
        State.SunProps.LocalHours = 12.0;
        State.SunProps.Enabled = false;
        State.Env.EnvDome.SkylightIntensity = 0.45;
        State.Lights.forEach(L => {
            L.Visible = ['light-softbox', 'light-studio-tube', 'light-led-cob', 'light-led-strip'].includes(L.Id);
        });
        State.SelectedId = 'light-softbox';
        Viewport?.SetCameraView('orbit');
    } else if (ScenarioId === 'lunar') {
        State.SunProps.LocalHours = 23.0;
        State.Env.EnvDome.ZenithColor = '#080e1c';
        State.Env.EnvDome.HorizonColor = '#142038';
        State.Env.EnvDome.SkylightIntensity = 0.18;
        State.Env.Moon.Visible = true;
        State.Env.Moon.Brightness = 2.6;
        State.Env.Stars.Visible = true;
        State.Env.Fog.Enabled = true;
        State.Env.Fog.Density = 0.012;
        State.Lights.forEach(L => {
            L.Visible = ['light-key-spot', 'light-rim-point'].includes(L.Id);
        });
        State.SelectedId = 'env:moon';
        Viewport?.SetCameraView('orbit');
    }

    Dom.DiurnalScrubber.value = String(State.SunProps.LocalHours);
    PushLog('RIG', `Activated lighting scenario "${ScenarioId}".`);
    RefreshAll();
}

//------------------------------------------------------------------------------------------------------------------------
//                                          OUTLINER RENDERING & CENSUS KPIs
//------------------------------------------------------------------------------------------------------------------------

function FormatFluxBadge(Light)
{
    const P = Light.Props;
    if (Light.Type === 'pointlight' || Light.Type === 'spotlight') return `${Number(P.intensity ?? 14).toFixed(0)} cd`;
    if (Light.Type === 'ieslight') {
        const Src = P.customIesKey || (P.useMeasuredIes ? P.measuredProfile : P.profile);
        return `${(ComputeFlux(Light.Type, P) * Number(P.multiplier ?? 1)).toFixed(0)} lm · ${Src}`;
    }
    return `${ComputeFlux(Light.Type, P).toFixed(0)} lm`;
}

function RenderOutliner()
{
    const Q = State.SearchQuery.trim().toLowerCase();
    const FilteredLights = State.Lights.filter(L =>
        !Q || L.Name.toLowerCase().includes(Q) || L.Type.toLowerCase().includes(Q)
    );
    Dom.GroupLightsCount.textContent = String(State.Lights.length);
    Dom.LuminaireList.replaceChildren();

    for (const Light of FilteredLights) {
        const Spec = LightCatalogue[Light.Type] || LightCatalogue.pointlight;
        const Li = document.createElement('li');
        Li.className = 'tree-item';
        if (Light.Id === State.SelectedId) Li.classList.add('is-selected');
        if (!Light.Visible) Li.classList.add('is-hidden-light');
        Li.setAttribute('role', 'option');
        Li.setAttribute('aria-selected', Light.Id === State.SelectedId ? 'true' : 'false');

        const IconBox = document.createElement('span');
        IconBox.className = 'tree-item-icon';
        IconBox.appendChild(CreatePzIcon(Spec.IconFile, Spec.Label));

        const Body = document.createElement('div');
        Body.className = 'tree-item-body';
        const NameEl = document.createElement('span');
        NameEl.className = 'tree-item-name';
        NameEl.textContent = Light.Name;
        const MetaEl = document.createElement('span');
        MetaEl.className = 'tree-item-meta';
        MetaEl.textContent = FormatFluxBadge(Light);
        Body.append(NameEl, MetaEl);

        const VisBtn = document.createElement('button');
        VisBtn.type = 'button';
        VisBtn.className = 'tree-vis-btn';
        VisBtn.title = Light.Visible ? 'Hide luminaire' : 'Show luminaire';
        VisBtn.appendChild(CreateSvgGlyph(Light.Visible ? 'EyeOpen' : 'EyeClosed', 'st-icon'));
        VisBtn.addEventListener('click', (E) => {
            E.stopPropagation();
            Light.Visible = !Light.Visible;
            RefreshAll();
        });

        Li.append(IconBox, Body, VisBtn);
        Li.addEventListener('click', () => {
            State.SelectedId = Light.Id;
            RefreshAll();
        });
        Dom.LuminaireList.appendChild(Li);
    }

    Dom.EnvironmentList.replaceChildren();
    for (const Node of EnvironmentNodes) {
        if (Q && !Node.Name.toLowerCase().includes(Q) && !Node.Meta.toLowerCase().includes(Q)) continue;
        const Li = document.createElement('li');
        Li.className = 'tree-item';
        if (Node.Id === State.SelectedId) Li.classList.add('is-selected');
        const IconBox = document.createElement('span');
        IconBox.className = 'tree-item-icon';
        IconBox.appendChild(CreatePzIcon(Node.IconFile, Node.Name));

        const Body = document.createElement('div');
        Body.className = 'tree-item-body';
        const NameEl = document.createElement('span');
        NameEl.className = 'tree-item-name';
        NameEl.textContent = Node.Name;
        const MetaEl = document.createElement('span');
        MetaEl.className = 'tree-item-meta';
        MetaEl.textContent = Node.Meta;
        Body.append(NameEl, MetaEl);

        Li.append(IconBox, Body);
        Li.addEventListener('click', () => {
            State.SelectedId = Node.Id;
            RefreshAll();
        });
        Dom.EnvironmentList.appendChild(Li);
    }

    // Update Census cards & Viewport Telemetry
    const VisibleLights = State.Lights.filter(L => L.Visible);
    const TotalFluxLm = VisibleLights.reduce((Sum, L) => {
        if (L.Type === 'pointlight' || L.Type === 'spotlight') return Sum + ComputeFlux(L.Type, L.Props) * 12.5;
        return Sum + ComputeFlux(L.Type, L.Props) * Number(L.Props.multiplier ?? 1);
    }, 0);
    const Sun = ComputeSunSummary();
    Dom.CensusLightCount.textContent = `${VisibleLights.length} / ${State.Lights.length}`;
    Dom.CensusTotalLumens.textContent = `${(TotalFluxLm / 1000).toFixed(2)} klm active`;
    Dom.CensusSunElev.textContent = `${Sun.ElevationDeg >= 0 ? '+' : ''}${Sun.ElevationDeg.toFixed(1)}°`;
    Dom.CensusEnvMeta.textContent = `IBL ${State.Env.EnvDome.SkylightIntensity.toFixed(2)}x · EV ${State.Env.Post.ExposureEv.toFixed(2)}`;

    const HoursInt = Math.floor(State.SunProps.LocalHours) % 24;
    const Mins = Math.round((State.SunProps.LocalHours - Math.floor(State.SunProps.LocalHours)) * 60) % 60;
    Dom.TransportClockReadout.textContent = `${String(HoursInt).padStart(2, '0')}:${String(Mins).padStart(2, '0')} UTC+2 · El ${Sun.ElevationDeg.toFixed(1)}°`;

    const WallLux = Viewport?.SampleStagePointLux(0, 1.4, -5.25, {
        Lights: State.Lights, Env: State.Env, SelectedId: State.SelectedId, SunState: Sun
    }, State.LoadedIes) || 0;
    Dom.TelemetrySummary.textContent = `${WallLux.toFixed(1)} lx @ screen · ${(TotalFluxLm / 1000).toFixed(2)} klm`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                      INSPECTOR BUILDERS: PROJECT ZERO 7 LIGHT TYPES
//------------------------------------------------------------------------------------------------------------------------

function ComputeLightHeroAndReadings(Light)
{
    const P = Light.Props;
    const Type = Light.Type;
    const Flux = ComputeFlux(Type, P);
    if (Type === 'pointlight') {
        const Int = Number(P.intensity ?? 14);
        const Reach = Number(P.distance ?? 25);
        const Dec = Number(P.decay ?? 2);
        return {
            PreviewName: 'Radial falloff',
            HeroInt: Math.floor(Int).toLocaleString(),
            HeroDec: (Int % 1).toFixed(1).slice(1),
            HeroUnit: 'cd',
            Caption: 'Authored luminous intensity',
            Descriptor: `Omnidirectional / decay ${Dec.toFixed(1)}`,
            Note: 'Source study · HTML authoring',
            Readings: [
                { Key: 'At 5 m · estimate', Val: `${(Int / Math.pow(5, Dec)).toFixed(2)} lx` },
                { Key: 'Reach', Val: `${Reach.toFixed(0)} m` }
            ]
        };
    }
    if (Type === 'spotlight') {
        const Int = Number(P.intensity ?? 88);
        const FullDeg = Number(P.angle ?? 13) * 2;
        const PenPct = Math.round(Number(P.penumbra ?? 0.42) * 100);
        return {
            PreviewName: 'Beam envelope',
            HeroInt: Math.floor(Int).toLocaleString(),
            HeroDec: (Int % 1).toFixed(1).slice(1),
            HeroUnit: 'cd',
            Caption: 'Authored luminous intensity',
            Descriptor: `${FullDeg.toFixed(0)}° cone / ${PenPct}% soft edge`,
            Note: 'Source study · HTML authoring',
            Readings: [
                { Key: 'Full cone', Val: `${FullDeg.toFixed(1)}°` },
                { Key: 'Soft edge', Val: `${PenPct} %` }
            ]
        };
    }
    if (Type === 'ieslight') {
        const Mult = Number(P.multiplier ?? 1);
        const RawLm = Number(P.lumens ?? 1650);
        const ProfileLabel = P.customIesKey ? `${P.customIesKey}.ies`
            : P.useMeasuredIes && P.measuredProfile ? `${P.measuredProfile}.ies`
            : (P.profile || 'ECE Low Beam');
        return {
            PreviewName: 'Photometric distribution',
            HeroInt: Math.floor(RawLm).toLocaleString(),
            HeroDec: (RawLm % 1).toFixed(1).slice(1),
            HeroUnit: 'lm',
            Caption: 'Profile source flux',
            Descriptor: `${ProfileLabel} / ${P.useMeasuredIes || P.customIesKey ? 'measured LM-63 Type-C' : 'synthetic polar study'}`,
            Note: P.useMeasuredIes || P.customIesKey ? 'Measured LM-63 .ies candela grid active' : 'Illustrative preset · no measured IES data',
            Readings: [
                { Key: 'Scaled output', Val: `${Math.round(RawLm * Mult).toLocaleString()} lm` },
                { Key: 'Field angle', Val: `${Number(P.cone ?? 58).toFixed(1)}°` }
            ]
        };
    }
    if (Type === 'arealight') {
        const W = Number(P.width ?? 2);
        const H = Number(P.height ?? 1);
        const Area = P.aperture === 'Disk' ? Math.PI * (W * 0.5) ** 2 : W * H;
        const RawLm = Number(P.lumens ?? 2400);
        return {
            PreviewName: 'Luminous surface',
            HeroInt: Math.floor(RawLm).toLocaleString(),
            HeroDec: (RawLm % 1).toFixed(1).slice(1),
            HeroUnit: 'lm',
            Caption: 'Authored luminous flux',
            Descriptor: `${P.aperture || 'Rectangle'} / ${W.toFixed(1)} × ${H.toFixed(1)} m aperture`,
            Note: 'Source study · HTML authoring',
            Readings: [
                { Key: 'Aperture', Val: `${Area.toFixed(2)} m²` },
                { Key: 'Emission', Val: P.twoSided ? 'Two-sided' : 'One-sided' }
            ]
        };
    }
    if (Type === 'tubelight') {
        const Len = Math.max(0.1, Number(P.length ?? 1.8));
        const RawLm = Number(P.lumens ?? 1800);
        return {
            PreviewName: 'Linear radiance',
            HeroInt: Math.floor(RawLm).toLocaleString(),
            HeroDec: (RawLm % 1).toFixed(1).slice(1),
            HeroUnit: 'lm',
            Caption: 'Authored luminous flux',
            Descriptor: `${Len.toFixed(1)} m linear emitter`,
            Note: 'Source study · HTML authoring',
            Readings: [
                { Key: 'Linear output', Val: `${Math.round(RawLm / Len).toLocaleString()} lm/m` },
                { Key: 'Tube radius', Val: `${Math.round(Number(P.radius ?? 0.04) * 1000)} mm` }
            ]
        };
    }
    if (Type === 'ledlight') {
        return {
            PreviewName: 'LED emitter',
            HeroInt: Math.floor(Flux).toLocaleString(),
            HeroDec: (Flux % 1).toFixed(1).slice(1),
            HeroUnit: 'lm',
            Caption: 'Estimated luminous flux',
            Descriptor: `${Number(P.diameter ?? 40).toFixed(0)} mm package / ${Number(P.temperature ?? 4000).toFixed(0)} K`,
            Note: 'Source study · HTML authoring',
            Readings: [
                { Key: 'Driver power', Val: `${(Number(P.watts ?? 18) * Number(P.dimmer ?? 1)).toFixed(1)} W` },
                { Key: 'Efficacy target', Val: `${Number(P.efficacy ?? 125).toFixed(0)} lm/W` }
            ]
        };
    }
    // ledstrip
    const Len = Number(P.length ?? 3.6);
    const Lpm = Number(P.ledsPerMetre ?? 60);
    return {
        PreviewName: 'Ribbon light',
        HeroInt: Math.floor(Flux).toLocaleString(),
        HeroDec: (Flux % 1).toFixed(1).slice(1),
        HeroUnit: 'lm',
        Caption: 'Estimated luminous flux',
        Descriptor: `Horizontal / ${Len.toFixed(1)} m run`,
        Note: 'Source study · HTML authoring',
        Readings: [
            { Key: 'Connected load', Val: `${(Number(P.wattsPerMetre ?? 14.4) * Len * Number(P.dimmer ?? 0.9)).toFixed(1)} W` },
            { Key: 'Emitter count', Val: `${Math.round(Lpm * Len)} LEDs` }
        ]
    };
}

function BuildColorControl(Props, OnMutate)
{
    const Wrap = document.createElement('div');
    Wrap.className = 'color-tint-row';
    const Input = document.createElement('input');
    Input.type = 'color';
    Input.className = 'color-swatch-input';
    Input.value = Props.color || '#ffffff';
    const Hex = document.createElement('span');
    Hex.className = 'color-hex-badge';
    Hex.textContent = Input.value.toUpperCase();
    Input.addEventListener('input', () => {
        Props.color = Input.value;
        Hex.textContent = Input.value.toUpperCase();
        OnMutate(false);
    });
    Wrap.append(Input, Hex);
    return Wrap;
}

function BuildTransformTable(Props, OnMutate)
{
    const Rows = [
        ['pos', 'Position', 'm', [0, 2, 0], -100, 100, 0.05],
        ['rot', 'Rotation', 'deg', [0, 0, 0], -360, 360, 0.5],
        ['scale', 'Scale', '×', [1, 1, 1], 0.01, 100, 0.05]
    ];
    if (Array.isArray(Props.target)) {
        Rows.splice(1, 0, ['target', 'Aim Target', 'm', [0, 0.5, -2], -100, 100, 0.05]);
    }
    const Wrap = document.createElement('div');
    const Table = document.createElement('table');
    Table.className = 'transform-table';
    Table.innerHTML = '<thead><tr><th>Component</th><th>X</th><th>Y</th><th>Z</th></tr></thead>';
    const Tbody = document.createElement('tbody');
    for (const [Key, Label, Unit, Init, Min, Max, Step] of Rows) {
        if (!Array.isArray(Props[Key])) Props[Key] = [...Init];
        const Tr = document.createElement('tr');
        const Th = document.createElement('th');
        Th.textContent = `${Label} (${Unit})`;
        Tr.appendChild(Th);
        for (let axis = 0; axis < 3; axis++) {
            const Td = document.createElement('td');
            const Inp = document.createElement('input');
            Inp.type = 'number';
            Inp.className = 'axis-input';
            Inp.step = String(Step);
            Inp.min = String(Min);
            Inp.max = String(Max);
            Inp.value = Number(Props[Key][axis]).toFixed(2);
            Inp.addEventListener('change', () => {
                const V = Clamp(parseFloat(Inp.value) || 0, Min, Max);
                Props[Key][axis] = Number(V.toFixed(3));
                Inp.value = Props[Key][axis].toFixed(2);
                OnMutate(false);
            });
            Td.appendChild(Inp);
            Tr.appendChild(Td);
        }
        Tbody.appendChild(Tr);
    }
    Table.appendChild(Tbody);
    Wrap.appendChild(Table);

    const ResetRow = document.createElement('div');
    ResetRow.className = 'transform-reset-row';
    const ResetRotBtn = document.createElement('button');
    ResetRotBtn.type = 'button';
    ResetRotBtn.className = 'chip-btn';
    ResetRotBtn.textContent = 'Zero Rotation';
    ResetRotBtn.addEventListener('click', () => {
        Props.rot = [0, 0, 0];
        OnMutate(true);
    });
    const CenterBtn = document.createElement('button');
    CenterBtn.type = 'button';
    CenterBtn.className = 'chip-btn';
    CenterBtn.textContent = 'Center X=0';
    CenterBtn.addEventListener('click', () => {
        Props.pos[0] = 0;
        OnMutate(true);
    });
    ResetRow.append(ResetRotBtn, CenterBtn);
    Wrap.appendChild(ResetRow);
    return Wrap;
}

function RenderLightInspector(Light)
{
    const P = Light.Props;
    const Type = Light.Type;
    const Spec = LightCatalogue[Type];
    Dom.InspectorGlyph.replaceChildren(CreatePzIcon(Spec.IconFile, Spec.Label));
    Dom.InspectorEntityName.textContent = Light.Name;
    Dom.InspectorEntitySubtitle.textContent = `${Spec.Label.toUpperCase()} · PROJECT ZERO PHOTOMETRY`;
    Dom.BtnDuplicateLight.style.display = '';
    Dom.BtnDeleteLight.style.display = '';

    const Host = Dom.InspectorScrollBody;
    Host.replaceChildren();

    const Summary = ComputeLightHeroAndReadings(Light);

    // 1. Preview Card (lp-preview — Verbatim Project Zero structure)
    const PreviewCard = document.createElement('section');
    PreviewCard.className = 'lp-preview';
    PreviewCard.innerHTML = `
        <div class="lp-preview-head">
            <span class="lp-source-icon"><img src="./Icons/${Spec.IconFile}.svg" alt=""></span>
            <span class="lp-preview-title">${Summary.PreviewName}</span>
            <span class="lp-preview-badge">LIGHT STUDY</span>
        </div>
        <div class="lp-hero-readout">
            <span id="HeroIntPart">${Summary.HeroInt}</span><span id="HeroDecPart" class="lp-decimal">${Summary.HeroDec}</span>
            <small class="lp-hero-unit">${Summary.HeroUnit}</small>
        </div>
        <p class="lp-primary-caption">${Summary.Caption}</p>
        <p id="PreviewDescriptor" class="lp-descriptor">${Summary.Descriptor}</p>
    `;
    const HeroCanvas = document.createElement('canvas');
    HeroCanvas.id = 'LightPreviewCanvas';
    HeroCanvas.className = 'lp-preview-canvas';
    const NoteP = document.createElement('p');
    NoteP.className = 'lp-study-note';
    NoteP.textContent = Summary.Note;
    PreviewCard.append(HeroCanvas, NoteP);
    Host.appendChild(PreviewCard);

    // 2. Readings Rail (lp-readings)
    const Readings = document.createElement('div');
    Readings.className = 'lp-readings';
    const UpdateReadingsDom = () => {
        const S = ComputeLightHeroAndReadings(Light);
        const HI = PreviewCard.querySelector('#HeroIntPart');
        const HD = PreviewCard.querySelector('#HeroDecPart');
        const Desc = PreviewCard.querySelector('#PreviewDescriptor');
        if (HI) HI.textContent = S.HeroInt;
        if (HD) HD.textContent = S.HeroDec;
        if (Desc) Desc.textContent = S.Descriptor;
        Readings.innerHTML = S.Readings.map(R => `
            <div class="lp-reading-tile">
                <span class="lp-reading-key">${R.Key}</span>
                <strong class="lp-reading-val">${R.Val}</strong>
            </div>
        `).join('');
    };
    UpdateReadingsDom();
    Host.appendChild(Readings);

    let ResponseCanvas = null;
    let ResponseTableEl = null;
    let ResponseHeadTitle = null;
    let ResponseLeadEl = null;
    let ResponseCapEl = null;

    const MutateLight = (RebuildPanel = false) => {
        if (RebuildPanel) {
            RefreshAll();
            return;
        }
        UpdateReadingsDom();
        DrawHeroLightCanvas(HeroCanvas, Type, P, State.LoadedIes);
        if (ResponseCanvas) {
            const Info = DrawResponseCurveCanvas(ResponseCanvas, Type, P, State.LoadedIes);
            if (ResponseHeadTitle) ResponseHeadTitle.textContent = Info.Kind;
            if (ResponseLeadEl) ResponseLeadEl.textContent = Info.Lead || '';
            if (ResponseCapEl) ResponseCapEl.textContent = Info.Caption || '';
            if (ResponseTableEl) {
                if (Info.Rows.length > 0) {
                    ResponseTableEl.style.display = 'grid';
                    ResponseTableEl.innerHTML =
                        `<span class="lp-response-th">Distance</span><span class="lp-response-th">Estimate</span><span class="lp-response-th">Relative</span>` +
                        Info.Rows.map(R => `<span class="lp-response-td is-muted">${R.Label}</span><span class="lp-response-td">${R.Value}</span><span class="lp-response-td is-muted">${R.Rel}</span>`).join('');
                } else {
                    ResponseTableEl.style.display = 'none';
                    ResponseTableEl.innerHTML = '';
                }
            }
        }
        RenderOutliner();
        SyncViewport();
    };

    // 3. Scene participation (FLAGS)
    const FlagsSec = CreateCollapsibleSection({ Title: 'Scene participation', Badge: 'FLAGS', Open: true });
    FlagsSec.Body.appendChild(CreatePropertyRow('Cast shadows', CreateTogglePill({
        Checked: Boolean(P.shadows),
        AriaLabel: 'Cast shadows',
        OnChange: (V) => { P.shadows = V; MutateLight(false); }
    }).Element, { Compact: true }));
    const SecondFlag = Type === 'ieslight' ? ['showDistribution', 'Draw distribution']
        : Type === 'spotlight' ? ['showCone', 'Draw cone']
        : Type === 'pointlight' ? ['gizmoGlow', 'Show glow']
        : ['showShape', 'Draw emitter'];
    FlagsSec.Body.appendChild(CreatePropertyRow(SecondFlag[1], CreateTogglePill({
        Checked: P[SecondFlag[0]] !== false,
        AriaLabel: SecondFlag[1],
        OnChange: (V) => { P[SecondFlag[0]] = V; MutateLight(false); }
    }).Element, { Compact: true }));
    Host.appendChild(FlagsSec.Section);

    // 4. Output Section + Embedded ANALYTICAL Response Card
    const OutTitle = Type === 'ledlight' ? 'Driver & colour' : Type === 'ledstrip' ? 'Output per metre' : 'Source & colour';
    const OutSec = CreateCollapsibleSection({ Title: OutTitle, Badge: 'OUTPUT', Open: true });

    if (Type === 'pointlight' || Type === 'spotlight') {
        OutSec.Body.appendChild(CreatePropertyRow('Intensity', CreateSliderPill({
            Value: Number(P.intensity ?? 14), Min: 0, Max: Type === 'spotlight' ? 200 : 60, Step: 0.5, Precision: 1, Suffix: 'cd',
            OnChange: (V) => { P.intensity = V; MutateLight(false); }
        }).Element));
    } else if (Type === 'ledlight') {
        OutSec.Body.appendChild(CreatePropertyRow('Driver power', CreateSliderPill({
            Value: Number(P.watts ?? 18), Min: 0.1, Max: 100, Step: 0.5, Precision: 1, Suffix: 'W',
            OnChange: (V) => { P.watts = V; MutateLight(false); }
        }).Element));
        OutSec.Body.appendChild(CreatePropertyRow('Efficacy target', CreateSliderPill({
            Value: Number(P.efficacy ?? 125), Min: 10, Max: 250, Step: 1, Precision: 0, Suffix: 'lm/W',
            OnChange: (V) => { P.efficacy = V; MutateLight(false); }
        }).Element));
        OutSec.Body.appendChild(CreatePropertyRow('Dimmer', CreateSliderPill({
            Value: Number(P.dimmer ?? 1), Min: 0, Max: 1, Step: 0.01, Precision: 2, Suffix: '',
            OnChange: (V) => { P.dimmer = V; MutateLight(false); }
        }).Element));
    } else if (Type === 'ledstrip') {
        OutSec.Body.appendChild(CreatePropertyRow('Flux per metre', CreateSliderPill({
            Value: Number(P.lumensPerMetre ?? 1000), Min: 10, Max: 4000, Step: 10, Precision: 0, Suffix: 'lm/m',
            OnChange: (V) => { P.lumensPerMetre = V; MutateLight(false); }
        }).Element));
        OutSec.Body.appendChild(CreatePropertyRow('Load per metre', CreateSliderPill({
            Value: Number(P.wattsPerMetre ?? 14.4), Min: 1, Max: 50, Step: 0.2, Precision: 1, Suffix: 'W/m',
            OnChange: (V) => { P.wattsPerMetre = V; MutateLight(false); }
        }).Element));
        OutSec.Body.appendChild(CreatePropertyRow('Dimmer', CreateSliderPill({
            Value: Number(P.dimmer ?? 0.9), Min: 0, Max: 1, Step: 0.01, Precision: 2, Suffix: '',
            OnChange: (V) => { P.dimmer = V; MutateLight(false); }
        }).Element));
    } else {
        const MaxLm = Type === 'arealight' ? 20000 : Type === 'tubelight' ? 12000 : 8000;
        OutSec.Body.appendChild(CreatePropertyRow('Luminous flux', CreateSliderPill({
            Value: Number(P.lumens ?? 1650), Min: 0, Max: MaxLm, Step: 25, Precision: 0, Suffix: 'lm',
            OnChange: (V) => { P.lumens = V; MutateLight(false); }
        }).Element));
    }

    if (P.temperature !== undefined || ['ieslight', 'tubelight', 'ledlight', 'ledstrip'].includes(Type)) {
        OutSec.Body.appendChild(CreatePropertyRow('Colour temperature', CreateSliderPill({
            Value: Number(P.temperature ?? 4300), Min: 1800, Max: 12000, Step: 50, Precision: 0, Suffix: 'K',
            OnChange: (V) => { P.temperature = V; MutateLight(false); }
        }).Element));
    }
    OutSec.Body.appendChild(CreatePropertyRow('Emission tint', BuildColorControl(P, MutateLight), { Compact: true }));

    const RespBox = document.createElement('div');
    RespBox.className = 'lp-response';
    RespBox.innerHTML = `
        <div class="lp-response-head"><span id="RespTitle">Analytical curve</span><span>ANALYTICAL</span></div>
        <div id="RespLead" class="lp-response-lead"></div>
        <div id="RespCap" class="lp-response-caption"></div>
    `;
    ResponseHeadTitle = RespBox.querySelector('#RespTitle');
    ResponseLeadEl = RespBox.querySelector('#RespLead');
    ResponseCapEl = RespBox.querySelector('#RespCap');
    ResponseCanvas = document.createElement('canvas');
    ResponseCanvas.className = 'lp-response-canvas' + (Type === 'pointlight' ? ' is-tall' : '');
    ResponseTableEl = document.createElement('div');
    ResponseTableEl.className = 'lp-response-table';
    RespBox.append(ResponseCanvas, ResponseTableEl);
    OutSec.Body.appendChild(RespBox);
    Host.appendChild(OutSec.Section);

    // 5. Shape (OPTICS — exact Project Zero fields & titles)
    const ShapeTitle = Type === 'pointlight' ? 'Attenuation'
        : Type === 'spotlight' ? 'Beam shaping'
        : Type === 'ieslight' ? 'Distribution profile'
        : Type === 'ledlight' ? 'Package & optic'
        : Type === 'ledstrip' ? 'Layout & segments'
        : 'Emitter dimensions';
    const ShapeSec = CreateCollapsibleSection({ Title: ShapeTitle, Badge: 'OPTICS', Open: true });

    if (Type === 'pointlight') {
        ShapeSec.Body.appendChild(CreatePropertyRow('Reach', CreateSliderPill({
            Value: Number(P.distance ?? 25), Min: 1, Max: 120, Step: 0.5, Precision: 0, Suffix: 'm',
            OnChange: (V) => { P.distance = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Decay exponent', CreateSliderPill({
            Value: Number(P.decay ?? 2), Min: 0, Max: 4, Step: 0.1, Precision: 2, Suffix: '',
            OnChange: (V) => { P.decay = V; MutateLight(false); }
        }).Element));
    } else if (Type === 'spotlight') {
        ShapeSec.Body.appendChild(CreatePropertyRow('Full cone angle', CreateSliderPill({
            Value: Number(P.angle ?? 13) * 2, Min: 4, Max: 160, Step: 0.5, Precision: 1, Suffix: '°',
            OnChange: (V) => { P.angle = V * 0.5; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Penumbra', CreateSliderPill({
            Value: Number(P.penumbra ?? 0.42), Min: 0, Max: 1, Step: 0.01, Precision: 2, Suffix: '',
            OnChange: (V) => { P.penumbra = V; MutateLight(false); }
        }).Element));
    } else if (Type === 'ieslight') {
        const PresetGrid = document.createElement('div');
        PresetGrid.className = 'ies-chip-grid';
        for (const Name of PresetIesNames) {
            const Btn = document.createElement('button');
            Btn.type = 'button';
            Btn.className = 'chip-btn';
            if (!P.useMeasuredIes && !P.customIesKey && P.profile === Name) Btn.classList.add('is-active');
            Btn.textContent = Name;
            Btn.addEventListener('click', () => {
                P.profile = Name;
                P.useMeasuredIes = false;
                P.customIesKey = null;
                P.measuredProfile = PresetToMeasuredMap[Name] || 'LowBeam';
                MutateLight(true);
            });
            PresetGrid.appendChild(Btn);
        }
        ShapeSec.Body.appendChild(PresetGrid);

        // Compact Measured LM-63 Type-C .ies selector strip
        const IesOptions = [
            { Value: '', Label: '(Analytical preset active)' },
            ...MeasuredIesProfileNames.map(N => ({ Value: N, Label: `${N}.ies (LM-63)` })),
            ...Object.keys(State.LoadedIes).filter(K => !MeasuredIesProfileNames.includes(K)).map(K => ({ Value: `custom:${K}`, Label: `${K}.ies (Uploaded)` }))
        ];
        const CurrentVal = P.customIesKey ? `custom:${P.customIesKey}` : (P.useMeasuredIes ? (P.measuredProfile || '') : '');
        ShapeSec.Body.appendChild(CreatePropertyRow('LM-63 .ies file', CreateSelectPill({
            Value: CurrentVal,
            Options: IesOptions,
            OnChange: (V) => {
                if (!V) {
                    P.useMeasuredIes = false;
                    P.customIesKey = null;
                } else if (V.startsWith('custom:')) {
                    P.useMeasuredIes = true;
                    P.customIesKey = V.slice(7);
                } else {
                    P.useMeasuredIes = true;
                    P.customIesKey = null;
                    P.measuredProfile = V;
                }
                MutateLight(true);
            }
        }).Element, { Compact: true }));

        ShapeSec.Body.appendChild(CreatePropertyRow('Profile multiplier', CreateSliderPill({
            Value: Number(P.multiplier ?? 1), Min: 0, Max: 4, Step: 0.05, Precision: 2, Suffix: '×',
            OnChange: (V) => { P.multiplier = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Field angle', CreateSliderPill({
            Value: Number(P.cone ?? 58), Min: 5, Max: 100, Step: 1, Precision: 1, Suffix: '°',
            OnChange: (V) => { P.cone = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Cut-off pitch', CreateSliderPill({
            Value: Number(P.cutoff ?? -1), Min: -5, Max: 5, Step: 0.1, Precision: 1, Suffix: '°',
            OnChange: (V) => { P.cutoff = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Photometric range', CreateSliderPill({
            Value: Number(P.range ?? 120), Min: 1, Max: 250, Step: 1, Precision: 0, Suffix: 'm',
            OnChange: (V) => { P.range = V; MutateLight(false); }
        }).Element));
    } else if (Type === 'arealight') {
        ShapeSec.Body.appendChild(CreatePropertyRow('Aperture', CreateSelectPill({
            Value: P.aperture || 'Rectangle',
            Options: ['Rectangle', 'Disk'],
            OnChange: (V) => { P.aperture = V; MutateLight(false); }
        }).Element, { Compact: true }));
        ShapeSec.Body.appendChild(CreatePropertyRow('Width', CreateSliderPill({
            Value: Number(P.width ?? 2), Min: 0.1, Max: 20, Step: 0.1, Precision: 1, Suffix: 'm',
            OnChange: (V) => { P.width = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Height', CreateSliderPill({
            Value: Number(P.height ?? 1), Min: 0.1, Max: 20, Step: 0.1, Precision: 1, Suffix: 'm',
            OnChange: (V) => { P.height = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Beam spread', CreateSliderPill({
            Value: Number(P.spread ?? 120), Min: 1, Max: 180, Step: 1, Precision: 0, Suffix: '°',
            OnChange: (V) => { P.spread = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Two-sided emission', CreateTogglePill({
            Checked: Boolean(P.twoSided),
            OnChange: (V) => { P.twoSided = V; MutateLight(false); }
        }).Element, { Compact: true }));
    } else if (Type === 'tubelight') {
        ShapeSec.Body.appendChild(CreatePropertyRow('Length', CreateSliderPill({
            Value: Number(P.length ?? 1.8), Min: 0.1, Max: 20, Step: 0.1, Precision: 1, Suffix: 'm',
            OnChange: (V) => { P.length = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Tube radius', CreateSliderPill({
            Value: Number(P.radius ?? 0.04), Min: 0.01, Max: 1, Step: 0.005, Precision: 3, Suffix: 'm',
            OnChange: (V) => { P.radius = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Reach', CreateSliderPill({
            Value: Number(P.distance ?? 24), Min: 1, Max: 120, Step: 1, Precision: 0, Suffix: 'm',
            OnChange: (V) => { P.distance = V; MutateLight(false); }
        }).Element));
    } else if (Type === 'ledlight') {
        ShapeSec.Body.appendChild(CreatePropertyRow('Package diameter', CreateSliderPill({
            Value: Number(P.diameter ?? 40), Min: 5, Max: 120, Step: 1, Precision: 0, Suffix: 'mm',
            OnChange: (V) => { P.diameter = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Emission angle', CreateSliderPill({
            Value: Number(P.angle ?? 110), Min: 10, Max: 180, Step: 1, Precision: 0, Suffix: 'deg',
            OnChange: (V) => { P.angle = V; MutateLight(false); }
        }).Element));
    } else if (Type === 'ledstrip') {
        ShapeSec.Body.appendChild(CreatePropertyRow('Strip length', CreateSliderPill({
            Value: Number(P.length ?? 3.6), Min: 0.1, Max: 20, Step: 0.1, Precision: 2, Suffix: 'm',
            OnChange: (V) => { P.length = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Emitter density', CreateSliderPill({
            Value: Number(P.ledsPerMetre ?? 60), Min: 10, Max: 240, Step: 5, Precision: 0, Suffix: '/m',
            OnChange: (V) => { P.ledsPerMetre = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Supply voltage', CreateSliderPill({
            Value: Number(P.voltage ?? 24), Min: 5, Max: 48, Step: 1, Precision: 0, Suffix: 'V',
            OnChange: (V) => { P.voltage = V; MutateLight(false); }
        }).Element));
        ShapeSec.Body.appendChild(CreatePropertyRow('Opal diffuser', CreateTogglePill({
            Checked: Boolean(P.diffuser),
            OnChange: (V) => { P.diffuser = V; MutateLight(false); }
        }).Element, { Compact: true }));
    }
    Host.appendChild(ShapeSec.Section);

    // 6. Transform Section (WORLD SPACE)
    const TransformSec = CreateCollapsibleSection({ Title: 'Transform', Badge: 'WORLD SPACE', Open: true });
    TransformSec.Body.appendChild(BuildTransformTable(P, MutateLight));
    Host.appendChild(TransformSec.Section);

    requestAnimationFrame(() => MutateLight(false));
}

//------------------------------------------------------------------------------------------------------------------------
//                                    INSPECTOR BUILDERS: ENVIRONMENT LIGHTS
//------------------------------------------------------------------------------------------------------------------------

function RenderEnvironmentInspector(Node)
{
    Dom.InspectorGlyph.replaceChildren(CreatePzIcon(Node.HeaderIcon || Node.IconFile, Node.Name));
    Dom.InspectorEntityName.textContent = Node.Name;
    Dom.InspectorEntitySubtitle.textContent = `${Node.Meta.toUpperCase()} · ENVIRONMENT LIGHT`;
    Dom.BtnDuplicateLight.style.display = 'none';
    Dom.BtnDeleteLight.style.display = 'none';

    const Host = Dom.InspectorScrollBody;
    Host.replaceChildren();
    const Type = Node.Type;

    const MutateEnv = (Rebuild = false) => {
        if (Rebuild) {
            RefreshAll();
            return;
        }
        DrawEnvPreviews();
        RenderOutliner();
        SyncViewport();
    };

    let CanvasA = null;
    let CanvasB = null;
    const DrawEnvPreviews = () => {
        const Sun = ComputeSunSummary();
        if (Type === 'envdome' && CanvasA) DrawHdrDomeCanvas(CanvasA, State.Env.EnvDome, Sun.Direction);
        if (Type === 'sun') {
            if (CanvasA) DrawSunDiscChromaticityCanvas(CanvasA, {
                DiameterDeg: State.SunProps.DiameterDeg,
                TemperatureK: State.SunProps.TemperatureK,
                ColorMode: State.SunProps.ColorMode,
                TintHex: State.SunProps.TintHex,
                Intensity: State.SunProps.Intensity,
                DirectGain: State.SunProps.DirectGain,
                ElevationDeg: Sun.ElevationDeg
            });
            if (CanvasB) DrawSolarArcCanvas(CanvasB, {
                Year: State.SunProps.Year, Month: State.SunProps.Month, Day: State.SunProps.Day,
                LocalHours: State.SunProps.LocalHours, UtcOffset: State.SunProps.UtcOffset,
                LatitudeDeg: State.SunProps.Latitude, LongitudeDeg: State.SunProps.Longitude
            });
        }
        if (Type === 'moon' && CanvasA) DrawMoonPreviewAndDialCanvas(CanvasA, State.Env.Moon);
        if (Type === 'stars' && CanvasA) DrawStarsAndTwinkleCanvas(CanvasA, State.Env.Stars, performance.now() * 0.001);
        if (Type === 'lensflare' && CanvasA) DrawLensFlareCanvas(CanvasA, State.Env.LensFlare);
        if (Type === 'atmosphere') {
            if (CanvasA) DrawAtmosphereScatteringCanvas(CanvasA, {
                ElevationDeg: Sun.ElevationDeg,
                Rayleigh: State.SunProps.Rayleigh,
                Mie: State.SunProps.Mie,
                MieAnisotropy: State.SunProps.MieAnisotropy,
                Ozone: State.SunProps.Ozone
            });
            if (CanvasB) DrawHeightFogBeamCanvas(CanvasB, State.Env.Fog);
        }
    };

    if (Type === 'envdome') {
        const E = State.Env.EnvDome;
        const Sec = CreateCollapsibleSection({ Title: 'HDRI & Skylight Probe', Badge: 'IBL DOME', Open: true });
        CanvasA = document.createElement('canvas');
        CanvasA.className = 'env-preview-canvas';
        Sec.Body.appendChild(CanvasA);
        Sec.Body.appendChild(CreatePropertyRow('Environment enabled', CreateTogglePill({
            Checked: E.Enabled, OnChange: (V) => { E.Enabled = V; MutateEnv(); }
        }).Element, { Compact: true }));
        Sec.Body.appendChild(CreatePropertyRow('Skylight intensity', CreateSliderPill({
            Value: E.SkylightIntensity, Min: 0, Max: 4, Step: 0.05, Precision: 2, Suffix: '×',
            OnChange: (V) => { E.SkylightIntensity = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Diffuse multiplier', CreateSliderPill({
            Value: E.DiffuseMultiplier, Min: 0, Max: 3, Step: 0.05, Precision: 2, Suffix: '×',
            OnChange: (V) => { E.DiffuseMultiplier = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Dome rotation', CreateSliderPill({
            Value: E.RotationDeg, Min: 0, Max: 360, Step: 1, Precision: 0, Suffix: 'deg',
            OnChange: (V) => { E.RotationDeg = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('IBL cubemap size', CreateSelectPill({
            Value: String(E.CubemapResolution),
            Options: ['128', '256', '512', '1024'],
            OnChange: (V) => { E.CubemapResolution = Number(V); MutateEnv(); }
        }).Element, { Compact: true }));
        Host.appendChild(Sec.Section);
    } else if (Type === 'sun') {
        const S = State.SunProps;
        const Ephem = ComputeSunSummary();
        const Prev = document.createElement('section');
        Prev.className = 'lp-preview';
        Prev.innerHTML = `
            <div class="lp-preview-head">
                <span class="lp-preview-label">NOAA Solar Disc &amp; Ephemeris</span>
                <div class="lp-hero-readout"><strong class="lp-hero-value">${Ephem.ElevationDeg.toFixed(1)}</strong><span class="lp-hero-unit">deg</span></div>
            </div>
        `;
        CanvasA = document.createElement('canvas');
        CanvasA.className = 'env-preview-canvas';
        CanvasB = document.createElement('canvas');
        CanvasB.className = 'env-preview-canvas';
        Prev.append(CanvasA, CanvasB);
        Host.appendChild(Prev);

        const Sec = CreateCollapsibleSection({ Title: 'Solar Lighting & Observer', Badge: 'SUN', Open: true });
        Sec.Body.appendChild(CreatePropertyRow('Sun active', CreateTogglePill({
            Checked: S.Enabled, OnChange: (V) => { S.Enabled = V; MutateEnv(); }
        }).Element, { Compact: true }));
        Sec.Body.appendChild(CreatePropertyRow('Local hours', CreateSliderPill({
            Value: S.LocalHours, Min: 0, Max: 24, Step: 0.05, Precision: 2, Suffix: 'h',
            OnChange: (V) => { S.LocalHours = V; Dom.DiurnalScrubber.value = String(V); MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Direct intensity', CreateSliderPill({
            Value: S.Intensity, Min: 0, Max: 60, Step: 0.5, Precision: 1, Suffix: '×',
            OnChange: (V) => { S.Intensity = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Colour temp', CreateSliderPill({
            Value: S.TemperatureK, Min: 1800, Max: 12000, Step: 50, Precision: 0, Suffix: 'K',
            OnChange: (V) => { S.TemperatureK = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Disc diameter', CreateSliderPill({
            Value: S.DiameterDeg, Min: 0.1, Max: 5, Step: 0.05, Precision: 2, Suffix: 'deg',
            OnChange: (V) => { S.DiameterDeg = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Latitude', CreateSliderPill({
            Value: S.Latitude, Min: -90, Max: 90, Step: 0.1, Precision: 2, Suffix: '°',
            OnChange: (V) => { S.Latitude = V; MutateEnv(); }
        }).Element));
        Host.appendChild(Sec.Section);
    } else if (Type === 'moon') {
        const M = State.Env.Moon;
        const Metrics = SolveMoonMetrics(M);
        const Prev = document.createElement('section');
        Prev.className = 'lp-preview';
        Prev.innerHTML = `
            <div class="lp-preview-head">
                <span class="lp-preview-label">Lunar Phase &amp; Horizon Dial</span>
                <div class="lp-hero-readout"><strong class="lp-hero-value">${Metrics.IlluminationPct.toFixed(0)}</strong><span class="lp-hero-unit">% lit</span></div>
            </div>
        `;
        CanvasA = document.createElement('canvas');
        CanvasA.className = 'env-preview-canvas';
        Prev.appendChild(CanvasA);
        Host.appendChild(Prev);

        const CatSec = CreateCollapsibleSection({ Title: 'Moon Surface Catalogue', Badge: '6 WORLDS', Open: true });
        const Grid = document.createElement('div');
        Grid.className = 'lunar-slot-grid';
        MoonCatalogue.forEach((Body, Idx) => {
            const Btn = document.createElement('button');
            Btn.type = 'button';
            Btn.className = 'lunar-slot-btn' + (M.Slot === Idx ? ' is-active' : '');
            Btn.title = Body.Subtitle;
            Btn.innerHTML = `<img class="lunar-tex-thumb" src="./Textures/Moons/${Body.TexFile}" alt=""><span>${Body.Name}</span>`;
            Btn.addEventListener('click', () => {
                M.Slot = Idx;
                M.AngularDiameterDeg = Body.NaturalDeg;
                MutateEnv(true);
            });
            Grid.appendChild(Btn);
        });
        CatSec.Body.appendChild(Grid);
        CatSec.Body.appendChild(CreatePropertyRow('Cast moonlight', CreateTogglePill({
            Checked: M.CastMoonlight, OnChange: (V) => { M.CastMoonlight = V; MutateEnv(); }
        }).Element, { Compact: true }));
        CatSec.Body.appendChild(CreatePropertyRow('Phase cycle', CreateSliderPill({
            Value: M.PhaseDays, Min: 0, Max: 29.53, Step: 0.1, Precision: 1, Suffix: 'd',
            OnChange: (V) => { M.PhaseDays = V; MutateEnv(false); }
        }).Element));
        CatSec.Body.appendChild(CreatePropertyRow('Brightness', CreateSliderPill({
            Value: M.Brightness, Min: 0, Max: 12, Step: 0.1, Precision: 1, Suffix: '×',
            OnChange: (V) => { M.Brightness = V; MutateEnv(false); }
        }).Element));
        CatSec.Body.appendChild(CreatePropertyRow('Azimuth', CreateSliderPill({
            Value: M.AzimuthDeg, Min: 0, Max: 360, Step: 1, Precision: 0, Suffix: '°',
            OnChange: (V) => { M.AzimuthDeg = V; MutateEnv(false); }
        }).Element));
        CatSec.Body.appendChild(CreatePropertyRow('Elevation', CreateSliderPill({
            Value: M.ElevationDeg, Min: -15, Max: 90, Step: 1, Precision: 0, Suffix: '°',
            OnChange: (V) => { M.ElevationDeg = V; MutateEnv(false); }
        }).Element));
        CatSec.Body.appendChild(CreatePropertyRow('Disc size', CreateSliderPill({
            Value: M.AngularDiameterDeg, Min: 0.1, Max: 5, Step: 0.05, Precision: 2, Suffix: '°',
            OnChange: (V) => { M.AngularDiameterDeg = V; MutateEnv(false); }
        }).Element));
        Host.appendChild(CatSec.Section);
    } else if (Type === 'stars') {
        const St = State.Env.Stars;
        const Sec = CreateCollapsibleSection({ Title: 'Star Field & Twinkle', Badge: 'CELESTIAL', Open: true });
        CanvasA = document.createElement('canvas');
        CanvasA.className = 'env-preview-canvas';
        Sec.Body.appendChild(CanvasA);
        Sec.Body.appendChild(CreatePropertyRow('Star field visible', CreateTogglePill({
            Checked: St.Visible, OnChange: (V) => { St.Visible = V; MutateEnv(); }
        }).Element, { Compact: true }));
        Sec.Body.appendChild(CreatePropertyRow('Limiting magnitude', CreateSliderPill({
            Value: St.LimitingMagnitude, Min: 0, Max: 8, Step: 0.1, Precision: 1, Suffix: 'mag',
            OnChange: (V) => { St.LimitingMagnitude = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Star brightness', CreateSliderPill({
            Value: St.Brightness, Min: 0, Max: 4, Step: 0.05, Precision: 2, Suffix: '×',
            OnChange: (V) => { St.Brightness = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Twinkle modulation', CreateTogglePill({
            Checked: St.TwinkleEnabled, OnChange: (V) => { St.TwinkleEnabled = V; MutateEnv(); }
        }).Element, { Compact: true }));
        Sec.Body.appendChild(CreatePropertyRow('Twinkle depth', CreateSliderPill({
            Value: St.TwinkleDepthPct, Min: 0, Max: 100, Step: 1, Precision: 0, Suffix: '%',
            OnChange: (V) => { St.TwinkleDepthPct = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Pole rotation', CreateSliderPill({
            Value: St.CelestialRotationDeg, Min: 0, Max: 360, Step: 1, Precision: 0, Suffix: '°',
            OnChange: (V) => { St.CelestialRotationDeg = V; MutateEnv(); }
        }).Element));
        Host.appendChild(Sec.Section);
    } else if (Type === 'lensflare') {
        const F = State.Env.LensFlare;
        const Sec = CreateCollapsibleSection({ Title: 'Optical Lens Flare Composite', Badge: 'INTERACTIVE', Open: true });
        CanvasA = document.createElement('canvas');
        CanvasA.className = 'env-preview-canvas';
        CanvasA.title = 'Click or drag to reposition flare optical axis';
        let DraggingFlare = false;
        const UpdateFlareAxis = (Ev) => {
            const R = CanvasA.getBoundingClientRect();
            F.PreviewX = Clamp((Ev.clientX - R.left) / Math.max(1, R.width), 0.06, 0.94);
            F.PreviewY = Clamp((Ev.clientY - R.top) / Math.max(1, R.height), 0.08, 0.92);
            MutateEnv();
        };
        CanvasA.addEventListener('pointerdown', (E) => { DraggingFlare = true; CanvasA.setPointerCapture(E.pointerId); UpdateFlareAxis(E); });
        CanvasA.addEventListener('pointermove', (E) => { if (DraggingFlare) UpdateFlareAxis(E); });
        CanvasA.addEventListener('pointerup', () => (DraggingFlare = false));
        Sec.Body.appendChild(CanvasA);

        Sec.Body.appendChild(CreatePropertyRow('Flare enabled', CreateTogglePill({
            Checked: F.Enabled, OnChange: (V) => { F.Enabled = V; MutateEnv(); }
        }).Element, { Compact: true }));
        Sec.Body.appendChild(CreatePropertyRow('Anamorphic streak', CreateTogglePill({
            Checked: F.Anamorphic, OnChange: (V) => { F.Anamorphic = V; MutateEnv(); }
        }).Element, { Compact: true }));
        Sec.Body.appendChild(CreatePropertyRow('Starburst diffraction', CreateTogglePill({
            Checked: F.Starburst, OnChange: (V) => { F.Starburst = V; MutateEnv(); }
        }).Element, { Compact: true }));
        Sec.Body.appendChild(CreatePropertyRow('Ghost shape', CreateSelectPill({
            Value: F.GhostShape,
            Options: ['Round', 'Hexagon', 'Octagon'],
            OnChange: (V) => { F.GhostShape = V; MutateEnv(); }
        }).Element, { Compact: true }));
        Sec.Body.appendChild(CreatePropertyRow('Flare intensity', CreateSliderPill({
            Value: F.Intensity, Min: 0, Max: 4, Step: 0.05, Precision: 2, Suffix: '×',
            OnChange: (V) => { F.Intensity = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Ghost brightness', CreateSliderPill({
            Value: F.GhostBrightness, Min: 0, Max: 2, Step: 0.02, Precision: 2, Suffix: '×',
            OnChange: (V) => { F.GhostBrightness = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Ghost count', CreateSliderPill({
            Value: F.GhostCount, Min: 0, Max: 12, Step: 1, Precision: 0, Suffix: '',
            OnChange: (V) => { F.GhostCount = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Halo radius', CreateSliderPill({
            Value: F.HaloRadius, Min: 0.05, Max: 0.8, Step: 0.01, Precision: 2, Suffix: '',
            OnChange: (V) => { F.HaloRadius = V; MutateEnv(); }
        }).Element));
        Host.appendChild(Sec.Section);
    } else if (Type === 'atmosphere') {
        const Fg = State.Env.Fog;
        const Sec = CreateCollapsibleSection({ Title: 'Rayleigh / Mie & Height Fog', Badge: 'MEDIUM', Open: true });
        CanvasA = document.createElement('canvas');
        CanvasA.className = 'env-preview-canvas';
        CanvasB = document.createElement('canvas');
        CanvasB.className = 'env-preview-canvas';
        Sec.Body.append(CanvasA, CanvasB);
        Sec.Body.appendChild(CreatePropertyRow('Rayleigh', CreateSliderPill({
            Value: State.SunProps.Rayleigh, Min: 0, Max: 4, Step: 0.05, Precision: 2, Suffix: '×',
            OnChange: (V) => { State.SunProps.Rayleigh = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Mie aerosol', CreateSliderPill({
            Value: State.SunProps.Mie, Min: 0, Max: 6, Step: 0.05, Precision: 2, Suffix: '×',
            OnChange: (V) => { State.SunProps.Mie = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Height fog enabled', CreateTogglePill({
            Checked: Fg.Enabled, OnChange: (V) => { Fg.Enabled = V; MutateEnv(); }
        }).Element, { Compact: true }));
        Sec.Body.appendChild(CreatePropertyRow('Fog density', CreateSliderPill({
            Value: Fg.Density, Min: 0.0005, Max: 0.05, Step: 0.0005, Precision: 4, Suffix: '/m',
            OnChange: (V) => { Fg.Density = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Probe distance', CreateSliderPill({
            Value: Fg.ProbeDistanceM, Min: 10, Max: 1000, Step: 10, Precision: 0, Suffix: 'm',
            OnChange: (V) => { Fg.ProbeDistanceM = V; MutateEnv(); }
        }).Element));
        Host.appendChild(Sec.Section);
    } else if (Type === 'post') {
        const Pt = State.Env.Post;
        const Sec = CreateCollapsibleSection({ Title: 'Exposure, Tonemap & Isolux', Badge: 'POST', Open: true });
        Sec.Body.appendChild(CreatePropertyRow('Exposure EV', CreateSliderPill({
            Value: Pt.ExposureEv, Min: -4, Max: 4, Step: 0.05, Precision: 2, Suffix: 'EV',
            OnChange: (V) => { Pt.ExposureEv = V; MutateEnv(); }
        }).Element));
        Sec.Body.appendChild(CreatePropertyRow('Tonemap curve', CreateSelectPill({
            Value: Pt.Tonemap,
            Options: ['ACES', 'Neutral', 'Reinhard'],
            OnChange: (V) => { Pt.Tonemap = V; MutateEnv(); }
        }).Element, { Compact: true }));
        Sec.Body.appendChild(CreatePropertyRow('Isolux overlay', CreateTogglePill({
            Checked: Viewport?.ShadingMode === 'lux',
            OnChange: (V) => {
                SetShadingLens(V ? 'lux' : 'pbr');
            }
        }).Element, { Compact: true }));
        Host.appendChild(Sec.Section);
    }

    requestAnimationFrame(() => DrawEnvPreviews());
}

//------------------------------------------------------------------------------------------------------------------------
//                                       IES MATRIX MODAL, UPLOAD, EXPORT & TRANSPORT
//------------------------------------------------------------------------------------------------------------------------

function OpenIesMatrixModal()
{
    // The IES profile matrix moved out to the shared Asset Browser.
    location.href = '../AssetBrowser/?ws=Photometric&kind=IES%20Profile';
}

/* Asset Browser deep link: ?asset=photo-<group>/<name> activates it on the stage. */
function ApplyIesDeepLink(Measured, Name)
{
    const Ent = GetSelectedEntity();
    let Target = Ent.Kind === 'light' && Ent.Light.Type === 'ieslight'
        ? Ent.Light
        : State.Lights.find(L => L.Type === 'ieslight');
    if (!Target) Target = AddLightOfType('ieslight');
    Target.Props.useMeasuredIes = Measured;
    Target.Props.customIesKey = null;
    if (Measured) Target.Props.measuredProfile = Name;
    else { Target.Props.profile = Name; Target.Props.measuredProfile = PresetToMeasuredMap[Name] || 'LowBeam'; }
    State.SelectedId = Target.Id;
    PushLog('IES', `Assigned photometric curve \u201C${Name}\u201D from the Asset Browser to ${Target.Name}.`);
    RefreshAll();
}
function HandleAssetDeepLink()
{
    const Raw = new URLSearchParams(location.search).get('asset') || '';
    if (!Raw.startsWith('photo-')) return;
    let M;
    if ((M = Raw.match(/^photo-rig\/([a-z]+)$/)) && ['roadway', 'gallery', 'studio', 'lunar'].includes(M[1]))
        ApplyScenario(M[1]);
    else if ((M = Raw.match(/^photo-light\/([a-z]+)$/)) && LightCatalogue[M[1]])
        AddLightOfType(M[1]);
    else if ((M = Raw.match(/^photo-moon\/(.+)$/)))
    {
        const Idx = MoonCatalogue.findIndex(B => B.Name === M[1]);
        if (Idx < 0) { PushLog('MOON', `Asset \u201C${Raw}\u201D is not a known moon.`); return; }
        const Body = MoonCatalogue[Idx];
        State.Env.Moon.Slot = Idx;
        State.Env.Moon.AngularDiameterDeg = Body.NaturalDeg;
        State.SelectedId = 'env:moon';
        PushLog('MOON', `Moon \u201C${Body.Name}\u201D applied from the Asset Browser.`);
        RefreshAll();
    }
    else if ((M = Raw.match(/^photo-ies-m\/(.+)$/)) && MeasuredIesProfileNames.includes(M[1]))
        ApplyIesDeepLink(true, M[1]);
    else if ((M = Raw.match(/^photo-ies-p\/(.+)$/)) && PresetIesNames.includes(M[1]))
        ApplyIesDeepLink(false, M[1]);
}

function AddLightOfType(Type)
{
    const Spec = LightCatalogue[Type] || LightCatalogue.pointlight;
    const Id = `light-${Type}-${Date.now().toString(36)}`;
    const Defaults = {
        pointlight: { pos: [0.6, 2.4, 0.8], rot: [0, 0, 0], scale: [1, 1, 1], intensity: 22, distance: 26, decay: 2.0, color: '#ffd9a0', shadows: false, gizmoGlow: true },
        spotlight:  { pos: [-2.0, 3.8, 2.2], target: [0, 0.6, -1.0], rot: [-38, 20, 0], scale: [1, 1, 1], intensity: 85, angle: 28, penumbra: 0.4, distance: 35, color: '#e8f0ff', shadows: true, showCone: true },
        ieslight:   { pos: [0.0, 1.4, 3.0], target: [0, 0.4, -4.5], rot: [-8, 0, 0], scale: [1, 1, 1], profile: 'ECE Low Beam', measuredProfile: 'LowBeam', useMeasuredIes: true, customIesKey: null, lumens: 1650, multiplier: 1.0, range: 120, cone: 58, cutoff: 0, color: '#fff2cf', temperature: 4300, shadows: true, showDistribution: true },
        arealight:  { pos: [2.2, 2.8, 1.6], target: [0, 0.8, 0], rot: [-30, -35, 0], scale: [1, 1, 1], aperture: 'Rectangle', width: 2.0, height: 1.0, lumens: 2400, spread: 120, color: '#fff1d6', twoSided: false, shadows: false, showShape: true },
        tubelight:  { pos: [-1.8, 2.6, 0.4], rot: [0, 15, 0], scale: [1, 1, 1], length: 1.8, radius: 0.04, lumens: 1800, distance: 24, color: '#e8f2ff', temperature: 5600, shadows: false, showShape: true },
        ledlight:   { pos: [0.5, 3.2, 0.6], rot: [-80, 0, 0], scale: [1, 1, 1], watts: 18, efficacy: 125, dimmer: 1.0, diameter: 40, angle: 110, color: '#ffffff', temperature: 4000, shadows: true, showShape: true },
        ledstrip:   { pos: [0.0, 0.14, -4.4], rot: [0, 0, 0], scale: [1, 1, 1], length: 3.2, lumensPerMetre: 1000, wattsPerMetre: 14.4, ledsPerMetre: 60, voltage: 24, dimmer: 0.9, diffuser: true, color: '#ffdcb4', temperature: 3000, shadows: false, showShape: true }
    };
    const Item = {
        Id,
        Name: `${Spec.ShortLabel} ${State.Lights.length + 1}`,
        Type,
        Visible: true,
        Locked: false,
        Props: structuredClone(Defaults[Type])
    };
    State.Lights.push(Item);
    State.SelectedId = Id;
    Dom.AddLightDrawer.hidden = true;
    PushLog('ADD', `Added ${Spec.Label} "${Item.Name}".`);
    RefreshAll();
    return Item;
}

function DownloadBlob(Filename, Content, Mime = 'text/plain')
{
    const BlobObj = new Blob([Content], { type: Mime });
    const Url = URL.createObjectURL(BlobObj);
    const A = document.createElement('a');
    A.href = Url;
    A.download = Filename;
    A.click();
    URL.revokeObjectURL(Url);
}

function SetShadingLens(Lens)
{
    if (!Viewport) return;
    Viewport.SetShadingMode(Lens);
    document.querySelectorAll('#ShadingLensGroup .hud-pill').forEach(Btn => {
        Btn.classList.toggle('is-active', Btn.dataset.lens === Lens);
    });
    Dom.LuxScaleBar.hidden = Lens !== 'lux';
    if (Lens === 'homologation') Viewport.SetCameraView('roadway');
}

function SyncViewport()
{
    const SunState = ComputeSunSummary();
    Viewport?.SyncScene({
        Lights: State.Lights,
        Env: State.Env,
        SelectedId: State.SelectedId,
        SunState
    }, State.LoadedIes);
}

function RefreshAll()
{
    RenderOutliner();
    const Sel = GetSelectedEntity();
    if (Sel.Kind === 'env') RenderEnvironmentInspector(Sel.Node);
    else if (Sel.Light) RenderLightInspector(Sel.Light);
    SyncViewport();
}

function SetupDomAndEvents()
{
    Dom.LuminaireList = document.getElementById('LuminaireList');
    Dom.EnvironmentList = document.getElementById('EnvironmentList');
    Dom.GroupLightsCount = document.getElementById('GroupLightsCount');
    Dom.CensusLightCount = document.getElementById('CensusLightCount');
    Dom.CensusTotalLumens = document.getElementById('CensusTotalLumens');
    Dom.CensusSunElev = document.getElementById('CensusSunElev');
    Dom.CensusEnvMeta = document.getElementById('CensusEnvMeta');
    Dom.InspectorGlyph = document.getElementById('InspectorGlyph');
    Dom.InspectorEntityName = document.getElementById('InspectorEntityName');
    Dom.InspectorEntitySubtitle = document.getElementById('InspectorEntitySubtitle');
    Dom.InspectorScrollBody = document.getElementById('InspectorScrollBody');
    Dom.BtnDuplicateLight = document.getElementById('BtnDuplicateLight');
    Dom.BtnDeleteLight = document.getElementById('BtnDeleteLight');
    Dom.AddLightDrawer = document.getElementById('AddLightDrawer');
    Dom.AddLightButtons = document.getElementById('AddLightButtons');
    Dom.TelemetryBackend = document.getElementById('TelemetryBackend');
    Dom.TelemetrySummary = document.getElementById('TelemetrySummary');
    Dom.LuxScaleBar = document.getElementById('LuxScaleBar');
    Dom.DiurnalScrubber = document.getElementById('DiurnalScrubber');
    Dom.TransportClockReadout = document.getElementById('TransportClockReadout');
    Dom.LogCountBadge = document.getElementById('LogCountBadge');
    Dom.ConsoleLogDrawer = document.getElementById('ConsoleLogDrawer');
    Dom.ConsoleLogBody = document.getElementById('ConsoleLogBody');
    Dom.IesMatrixVeil = document.getElementById('IesMatrixVeil');
    Dom.MatrixGrid = document.getElementById('MatrixGrid');

    HydrateIcons(document);

    // Populate Add-Light Drawer — luminaire browsing moved out to the shared Asset Browser.
    {
        const Btn = document.createElement('button');
        Btn.type = 'button';
        Btn.className = 'add-light-type-btn';
        const Sp = document.createElement('span');
        Sp.textContent = 'Browse luminaires\u2026';
        Btn.appendChild(Sp);
        Btn.title = 'Open the shared Asset Browser (Photometric lights)';
        Btn.addEventListener('click', () => { location.href = '../AssetBrowser/?ws=Photometric&kind=Light'; });
        Dom.AddLightButtons.appendChild(Btn);
    }

    // Quick Photometry Presets
    const QuickContainer = document.getElementById('QuickPresetChips');
    [
        { Label: 'Browse light assets\u2026', Fn: () => { location.href = '../AssetBrowser/?ws=Photometric'; } },
        { Label: 'Isolux Scan', Fn: () => SetShadingLens(Viewport?.ShadingMode === 'lux' ? 'pbr' : 'lux') }
    ].forEach(Preset => {
        const Btn = document.createElement('button');
        Btn.type = 'button';
        Btn.className = 'chip-btn';
        Btn.textContent = Preset.Label;
        Btn.addEventListener('click', Preset.Fn);
        QuickContainer.appendChild(Btn);
    });

    // Scenario Tabs
    document.querySelectorAll('#RigTabStrip .doc-tab').forEach(Btn => {
        Btn.addEventListener('click', () => ApplyScenario(Btn.dataset.scenario));
    });

    // Search
    document.getElementById('OutlinerSearch').addEventListener('input', (E) => {
        State.SearchQuery = E.target.value;
        RenderOutliner();
    });

    // Add drawer
    document.getElementById('BtnAddLightMenu').addEventListener('click', () => {
        Dom.AddLightDrawer.hidden = !Dom.AddLightDrawer.hidden;
    });
    document.getElementById('BtnCloseAddDrawer').addEventListener('click', () => {
        Dom.AddLightDrawer.hidden = true;
    });

    // Duplicate & Delete
    Dom.BtnDuplicateLight.addEventListener('click', () => {
        const Sel = GetSelectedEntity();
        if (Sel.Kind !== 'light') return;
        const Copy = structuredClone(Sel.Light);
        Copy.Id = `light-copy-${Date.now().toString(36)}`;
        Copy.Name = `${Copy.Name} Copy`;
        Copy.Props.pos[0] += 0.8;
        State.Lights.push(Copy);
        State.SelectedId = Copy.Id;
        PushLog('DUP', `Duplicated "${Sel.Light.Name}".`);
        RefreshAll();
    });
    Dom.BtnDeleteLight.addEventListener('click', () => {
        const Sel = GetSelectedEntity();
        if (Sel.Kind !== 'light' || State.Lights.length <= 1) return;
        State.Lights = State.Lights.filter(L => L.Id !== Sel.Light.Id);
        State.SelectedId = State.Lights[0].Id;
        PushLog('DEL', `Removed "${Sel.Light.Name}".`);
        RefreshAll();
    });

    // Lenses & Cameras
    document.querySelectorAll('#ShadingLensGroup .hud-pill').forEach(Btn => {
        Btn.addEventListener('click', () => SetShadingLens(Btn.dataset.lens));
    });
    document.querySelectorAll('[data-cam]').forEach(Btn => {
        Btn.addEventListener('click', () => Viewport?.SetCameraView(Btn.dataset.cam));
    });
    const DarkRoomBtn = document.getElementById('BtnToggleDarkRoom');
    DarkRoomBtn?.addEventListener('click', () => {
        if (!Viewport) return;
        Viewport.SetDarkRoomOnly(!Viewport.DarkRoomOnly);
        DarkRoomBtn.classList.toggle('is-active', Viewport.DarkRoomOnly);
        DarkRoomBtn.textContent = Viewport.DarkRoomOnly ? 'Dark Fog Room' : 'Environment Sky';
    });
    const GizmoBtn = document.getElementById('BtnToggleGizmos');
    GizmoBtn.addEventListener('click', () => {
        if (!Viewport) return;
        Viewport.ShowGizmos = !Viewport.ShowGizmos;
        GizmoBtn.classList.toggle('is-active', Viewport.ShowGizmos);
        SyncViewport();
    });

    // Upload custom .ies
    document.getElementById('IesFileInput').addEventListener('change', async (E) => {
        const File = E.target.files?.[0];
        if (!File) return;
        try {
            const Text = await File.text();
            const Parsed = ParseIes(Text);
            const Key = File.name.replace(/\.ies$/i, '');
            State.LoadedIes[Key] = Parsed;
            let Target = GetSelectedEntity().Kind === 'light' && GetSelectedEntity().Light.Type === 'ieslight'
                ? GetSelectedEntity().Light
                : State.Lights.find(L => L.Type === 'ieslight') || AddLightOfType('ieslight');
            Target.Props.customIesKey = Key;
            State.SelectedId = Target.Id;
            PushLog('IES', `Imported custom LM-63 file "${File.name}" (${Parsed.Vertical.length}×${Parsed.Horizontal.length}, peak ${Parsed.Peak.toFixed(0)} cd).`);
            RefreshAll();
        } catch (Err) {
            PushLog('ERR', `Failed to parse ${File.name}: ${Err.message}`);
        }
        E.target.value = '';
    });

    // Export .ies & Rig TOML
    document.getElementById('BtnExportIes').addEventListener('click', () => {
        const Sel = GetSelectedEntity();
        const Target = Sel.Kind === 'light' ? Sel.Light : State.Lights.find(L => L.Type === 'ieslight') || State.Lights[0];
        const Content = EncodeSyntheticIesFile(Target.Name, Target.Props, State.LoadedIes);
        const Slug = Target.Name.toLowerCase().replace(/[^a-z0-9]+/g, '-');
        DownloadBlob(`${Slug}.ies`, Content);
        PushLog('EXPORT', `Exported LM-63 Type-C file "${Slug}.ies".`);
    });
    document.getElementById('BtnExportRig').addEventListener('click', () => {
        const ArchiveLines = [
            '# Frontier Photometric & Light Studio Rig Export',
            `Scenario = "${State.Scenario}"`,
            `SolarHours = ${State.SunProps.LocalHours.toFixed(2)}`,
            `ExposureEv = ${State.Env.Post.ExposureEv.toFixed(2)}`,
            ''
        ];
        State.Lights.forEach(L => {
            ArchiveLines.push(`[[Lights]]`);
            ArchiveLines.push(`Name = "${L.Name}"`);
            ArchiveLines.push(`Type = "${L.Type}"`);
            ArchiveLines.push(`Visible = ${L.Visible}`);
            ArchiveLines.push(`Flux = ${ComputeFlux(L.Type, L.Props).toFixed(1)}`);
            ArchiveLines.push(`Position = [${(L.Props.pos || [0, 0, 0]).join(', ')}]`);
            ArchiveLines.push('');
        });
        DownloadBlob(`FrontierRig-${State.Scenario}.projectspace`, ArchiveLines.join('\n'));
        PushLog('EXPORT', `Exported full lighting rig (${State.Lights.length} luminaires + environment lights).`);
    });

    // Matrix Modal & Log Drawer
    document.getElementById('BtnOpenMatrix').addEventListener('click', OpenIesMatrixModal);
    document.getElementById('BtnCloseMatrix').addEventListener('click', () => { Dom.IesMatrixVeil.hidden = true; });
    document.getElementById('BtnToggleLog').addEventListener('click', () => {
        Dom.ConsoleLogDrawer.hidden = !Dom.ConsoleLogDrawer.hidden;
    });
    document.getElementById('BtnClearLog').addEventListener('click', () => {
        State.Logs = [];
        Dom.LogCountBadge.textContent = '0';
        Dom.ConsoleLogBody.innerHTML = '';
    });

    // Diurnal Sweep Transport
    const PlayBtn = document.getElementById('BtnPlayDiurnal');
    const ToggleSweep = () => {
        State.SweepPlaying = !State.SweepPlaying;
        PlayBtn.replaceChildren(CreateSvgGlyph(State.SweepPlaying ? 'Pause' : 'Play', 'st-icon'));
        if (State.SweepPlaying) {
            State.SweepTimer = setInterval(() => {
                State.SunProps.LocalHours = (State.SunProps.LocalHours + 0.12) % 24;
                Dom.DiurnalScrubber.value = String(State.SunProps.LocalHours);
                RenderOutliner();
                SyncViewport();
                const Sel = GetSelectedEntity();
                if (Sel.Kind === 'env' && ['sun', 'stars', 'atmosphere', 'envdome'].includes(Sel.Node.Type)) {
                    RenderEnvironmentInspector(Sel.Node);
                }
            }, 60);
        } else {
            clearInterval(State.SweepTimer);
        }
    };
    PlayBtn.addEventListener('click', ToggleSweep);
    document.getElementById('BtnStepBack').addEventListener('click', () => {
        State.SunProps.LocalHours = (State.SunProps.LocalHours + 23) % 24;
        Dom.DiurnalScrubber.value = String(State.SunProps.LocalHours);
        RefreshAll();
    });
    document.getElementById('BtnStepFwd').addEventListener('click', () => {
        State.SunProps.LocalHours = (State.SunProps.LocalHours + 1) % 24;
        Dom.DiurnalScrubber.value = String(State.SunProps.LocalHours);
        RefreshAll();
    });
    document.getElementById('BtnResetDiurnal').addEventListener('click', () => {
        if (State.SweepPlaying) ToggleSweep();
        State.SunProps.LocalHours = 12.0;
        Dom.DiurnalScrubber.value = '12';
        RefreshAll();
    });
    Dom.DiurnalScrubber.addEventListener('input', () => {
        State.SunProps.LocalHours = parseFloat(Dom.DiurnalScrubber.value) || 12;
        RenderOutliner();
        SyncViewport();
    });

    // Keyboard shortcuts
    window.addEventListener('keydown', (E) => {
        if (E.target.tagName === 'INPUT' || E.target.tagName === 'SELECT') return;
        if (E.key === 'm' || E.key === 'M') {
            OpenIesMatrixModal();
        } else if (E.key === 'g' || E.key === 'G') {
            GizmoBtn.click();
        } else if (E.key === ' ') {
            E.preventDefault();
            ToggleSweep();
        } else if (E.key === 'Escape') {
            Dom.IesMatrixVeil.hidden = true;
            Dom.AddLightDrawer.hidden = true;
        }
    });

    window.addEventListener('resize', () => Viewport?.Resize());

    // Mount 3D Viewport
    Viewport = new PhotometricViewportController(document.getElementById('PhotometricViewportMount'), {
        onBackendChange: (Name) => {
            if (Dom.TelemetryBackend) Dom.TelemetryBackend.textContent = Name;
            PushLog('GPU', `Photometric stage active on ${Name}.`);
        }
    });
}

SetupDomAndEvents();
RefreshAll();
HandleAssetDeepLink();
LoadBuiltinIesProfiles();

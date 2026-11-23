// AFTERGLOW — CABIN HMI PROTOTYPE B
// 🧩 Ambient panoramic display: the ControlVehicle parked on a wet mirror flat among dusk dunes under a planet,
//    ringed by flowing particle waves. Menus: Horizon (world presets), Drive (mood, regen, glide preview),
//    Climate (fan dial with cabin airflow), Rest (ring timer that dims the world) and Media (player + spectrum).

import { THREE, AssembleVehicle, DefaultFinish, PanelEnvironment, Family, MirrorTwin } from '../Shared/VehicleAssembly.js';
import { FitDisplay, CameraOrbit, RollNumber, BindRipples, ClockText, RunLoop } from '../Shared/DisplayKit.js';
import { Powertrain } from '../Shared/Powertrain.js';
import { MountLamps, BindLampControls } from '../Shared/LampRig.js';
import { OpenLink } from '../Shared/CabinLink.js';

// Powertrain and cabin link (see POWERTRAIN AND CABIN LINK below): declared first so every section can read them.
const Train = new Powertrain({ Ratios: [4.7, 3.1, 2.1, 1.67, 1.29, 1.0, 0.84, 0.67], Final: 3.15, Redline: 6500, Limit: 7000, Mass: 1850, PeakTorque: 500, Tank: 70, Style: 'Comfort' });
const Link  = OpenLink('Afterglow');
let Motion = Train, LampSent = 0;                                   // Motion: the live drive readings (link or own)

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const DisplayWidth = 1600, DisplayHeight = 900;

// 📝 One palette per world preset; every colour and level eases towards the chosen palette each frame.
const Palettes = [
    {   Name: 'Dune dusk',
        Zenith: '#2a1a4e', Middle: '#a35f92', Horizon: '#f4b39b', Glow: '#ffd2a8', Dune: '#9c5a78', Floor: '#2a1630',
        FloorDeep: '#170c1f', Fog: '#cf8a98', PlanetA: '#f6cfae', PlanetB: '#8a4a78', Atmos: '#ffc0d8', Tint: '#ffe8f2',
        Sky: '#ffc6d8', Ground: '#3a1c34', Key: '#ffc4a0',
        Stars: 0.25, Relief: 1.0, PlanetScale: 1.0, Ring: 1.0, Aurora: 0.0, Exposure: 1.0 },
    {   Name: 'Lilac lake',
        Zenith: '#33358a', Middle: '#9686d6', Horizon: '#ecc8ea', Glow: '#fff0f8', Dune: '#6a60ae', Floor: '#5a4c9a',
        FloorDeep: '#2e2668', Fog: '#c4b0e6', PlanetA: '#fbf8ff', PlanetB: '#c9c0ea', Atmos: '#ffffff', Tint: '#ffffff',
        Sky: '#e4dcff', Ground: '#3a3070', Key: '#fff0f6',
        Stars: 0.12, Relief: 0.35, PlanetScale: 0.34, Ring: 0.0, Aurora: 0.0, Exposure: 1.05 },
    {   Name: 'Star camp',
        Zenith: '#02041a', Middle: '#0c1a46', Horizon: '#235a74', Glow: '#6fe8d4', Dune: '#1c2442', Floor: '#060a1a',
        FloorDeep: '#03050d', Fog: '#10264a', PlanetA: '#a8e4f0', PlanetB: '#24467a', Atmos: '#7ff2e2', Tint: '#c4f8ff',
        Sky: '#6fa8d8', Ground: '#0a0f24', Key: '#a8d8ff',
        Stars: 1.0, Relief: 1.35, PlanetScale: 0.72, Ring: 0.35, Aurora: 1.0, Exposure: 1.15 },
];

const Moods = [
    { Name: 'Calm',  Accent: '#9ff0d4', Rgb: '159, 240, 212', WaveSpeed: 0.55, Tail: 0.8 },
    { Name: 'Flow',  Accent: '#c8b4ff', Rgb: '200, 180, 255', WaveSpeed: 0.9,  Tail: 1.1 },
    { Name: 'Surge', Accent: '#ff9aa8', Rgb: '255, 154, 168', WaveSpeed: 1.5,  Tail: 1.6 },
];

const Tracks = [
    { Title: 'Flowing future vibes', Artist: 'Lumen Arcade', Album: 'Night Drive', Length: 228, Hue: 0 },
    { Title: 'Salt flat sunrise',    Artist: 'Kiro Vale',    Album: 'Long Light',  Length: 201, Hue: 70 },
    { Title: 'Lilac interstate',     Artist: 'Mira Okoye',   Album: 'Afterhours',  Length: 254, Hue: 210 },
];

const CameraPresets = {
    Horizon: { Target: [0.2, 1.9, 0],  Radius: 16,   Yaw: 0.75,  Pitch: 0.05 },
    Drive:   { Target: [0.4, 1.35, 0.6], Radius: 11.5, Yaw: -1.02, Pitch: 0.1 },
    Climate: { Target: [0.75, 0.7, -0.35], Radius: 15,   Yaw: 0.5,   Pitch: 0.86 },
    Rest:    { Target: [0, 3.0, 0],    Radius: 24,   Yaw: 0.8,   Pitch: 0.02 },
    Media:   { Target: [0.4, 1.7, 0],  Radius: 13,   Yaw: 1.3,   Pitch: 0.08 },
    Lights:  { Target: [2.2, 1.0, -0.8], Radius: 13, Yaw: 2.1,   Pitch: 0.12 },
};
const MenuOrder = ['Horizon', 'Drive', 'Lights', 'Climate', 'Rest', 'Media'];

//------------------------------------------------------------------------------------------------------------------------
//                                                    DOCUMENT
//------------------------------------------------------------------------------------------------------------------------

const Display = document.getElementById('Display');
const Find    = (Id) => document.getElementById(Id);
FitDisplay(Display, DisplayWidth, DisplayHeight);
BindRipples(Display);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONDITION
//------------------------------------------------------------------------------------------------------------------------

const Condition = {
    Menu:       'Horizon',
    Scene:      0,
    Haze:       0.45,
    Waves:      true,
    Mood:       0,
    Regen:      2,
    Gliding:    false,
    Glide:      0,                  // [km/h] - eased glide preview speed
    Speed:      0,                  // [km/h] - shown: glide or powertrain, whichever is faster
    Ribbon:     true,
    Fan:        3,                  // 0..7
    Air:        'face',
    Auto:       true,
    Recirculate: false,
    Temps:      [22.0, 22.0],
    Seats:      [1, 0],
    Resting:    false,
    RestTotal:  330,                // [s]
    RestLeft:   330,                // [s]
    Playing:    false,
    Track:      0,
    TrackTime:  72,                 // [s]
    MusicSync:  true,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDERER AND SCENE
//------------------------------------------------------------------------------------------------------------------------

const Canvas   = Find('Scene');
const Renderer = new THREE.WebGLRenderer({ canvas: Canvas, antialias: true });
const PixelRatio = Math.min(2, window.devicePixelRatio || 1);
Renderer.setPixelRatio(PixelRatio);
Renderer.setSize(DisplayWidth, DisplayHeight, false);
Renderer.toneMapping         = THREE.ACESFilmicToneMapping;
Renderer.toneMappingExposure = 1.0;
Renderer.outputColorSpace    = THREE.SRGBColorSpace;

const Scene = new THREE.Scene();
Scene.fog   = new THREE.Fog('#cf8a98', 16, 110);
Scene.environment = PanelEnvironment(Renderer, [
    { Colour: '#ffd6e6', Intensity: 1.8, Position: [0, 9, 0],    Size: [12, 8] },
    { Colour: '#ffb08a', Intensity: 2.4, Position: [-8, 1.5, -6], Size: [10, 2.5] },
    { Colour: '#b9a4ff', Intensity: 1.4, Position: [8, 2, 6],    Size: [8, 4] },
    { Colour: '#3a2050', Intensity: 1.0, Position: [0, -4, 0],   Size: [20, 20] },
], '#2a1a3a');
Scene.environmentIntensity = 1.2;

const Camera = new THREE.PerspectiveCamera(30, DisplayWidth / DisplayHeight, 0.1, 420);
const Orbit  = new CameraOrbit(Camera, Canvas, { ...CameraPresets.Horizon, RadiusLimits: [7, 26], PitchLimits: [0.0, 1.3] });

const SunDirection = new THREE.Vector3(-0.55, 0.06, -0.83).normalize();
const Hemisphere   = new THREE.HemisphereLight('#ffc6d8', '#3a1c34', 0.9);
const KeyLight     = new THREE.DirectionalLight('#ffc4a0', 2.4);
KeyLight.position.copy(SunDirection).multiplyScalar(20).setY(6);
const FrontFill    = new THREE.DirectionalLight('#e8dcff', 0.9);
FrontFill.position.set(8, 5, 9);
Scene.add(Hemisphere, KeyLight, FrontFill);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PALETTE
//------------------------------------------------------------------------------------------------------------------------

const ColourKeys = ['Zenith', 'Middle', 'Horizon', 'Glow', 'Dune', 'Floor', 'FloorDeep', 'Fog', 'PlanetA', 'PlanetB', 'Atmos', 'Tint', 'Sky', 'Ground', 'Key'];
const LevelKeys  = ['Stars', 'Relief', 'PlanetScale', 'Ring', 'Aurora', 'Exposure'];
const Live = {};
for (const Key of ColourKeys) Live[Key] = new THREE.Color(Palettes[0][Key]);
for (const Key of LevelKeys) Live[Key] = Palettes[0][Key];
const GoalColours = ColourKeys.map((Key) => new THREE.Color(Palettes[0][Key]));

//------------------------------------------------------------------------------------------------------------------------
//                                                    SKY, STARS, PLANET
//------------------------------------------------------------------------------------------------------------------------

const SkyUniforms = {
    Zenith: { value: Live.Zenith }, Middle: { value: Live.Middle }, Horizon: { value: Live.Horizon }, Glow: { value: Live.Glow },
    SunDirection: { value: SunDirection }, Aurora: { value: 0 }, Clock: { value: 0 },
};
const Sky = new THREE.Mesh(new THREE.SphereGeometry(300, 48, 24), new THREE.ShaderMaterial({
    uniforms: SkyUniforms, side: THREE.BackSide, depthWrite: false, fog: false, toneMapped: false,
    vertexShader: /* glsl */`
        varying vec3 Direction;
        void main() { Direction = position; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
    fragmentShader: /* glsl */`
        uniform vec3 Zenith, Middle, Horizon, Glow, SunDirection;
        uniform float Aurora, Clock;
        varying vec3 Direction;
        void main()
        {
            vec3 D = normalize(Direction);
            float H = D.y;
            vec3 C = mix(Horizon, Middle, smoothstep(-0.04, 0.24, H));
            C = mix(C, Zenith, smoothstep(0.24, 0.85, H));
            float Sun = max(dot(D, SunDirection), 0.0);
            C += Glow * (pow(Sun, 5.0) * 0.32 + pow(Sun, 48.0) * 0.55) * smoothstep(-0.12, 0.06, H);
            float Wave    = sin(D.x * 5.0 + Clock * 0.12 + sin(D.z * 3.0 + Clock * 0.08) * 1.6);
            float Curtain = smoothstep(0.04, 0.28, H) * (1.0 - smoothstep(0.28, 0.62, H)) * (0.5 + 0.5 * Wave);
            C += Glow * Curtain * Aurora * 0.5;
            gl_FragColor = vec4(C, 1.0);
            #include <colorspace_fragment>
        }`,
}));
Sky.renderOrder = -3;
Scene.add(Sky);

const StarCount = 1600;
const StarGeometry = new THREE.BufferGeometry();
{
    const Points = new Float32Array(StarCount * 3), Seeds = new Float32Array(StarCount);
    for (let Index = 0; Index < StarCount; Index++)
    {
        const Yaw = Math.random() * Math.PI * 2, Rise = Math.asin(0.04 + Math.random() * 0.96);
        Points.set([Math.cos(Rise) * Math.cos(Yaw) * 280, Math.sin(Rise) * 280, Math.cos(Rise) * Math.sin(Yaw) * 280], Index * 3);
        Seeds[Index] = Math.random();
    }
    StarGeometry.setAttribute('position', new THREE.BufferAttribute(Points, 3));
    StarGeometry.setAttribute('Seed', new THREE.BufferAttribute(Seeds, 1));
}
const StarUniforms = { Level: { value: 0.25 }, Clock: { value: 0 }, Ratio: { value: PixelRatio } };
const Stars = new THREE.Points(StarGeometry, new THREE.ShaderMaterial({
    uniforms: StarUniforms, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, fog: false, toneMapped: false,
    vertexShader: /* glsl */`
        uniform float Clock, Ratio; attribute float Seed; varying float Shine;
        void main()
        {
            Shine = (0.35 + 0.65 * Seed) * (0.6 + 0.4 * sin(Clock * (1.0 + Seed * 3.0) + Seed * 40.0));
            gl_Position  = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            gl_PointSize = (1.0 + Seed * 1.8) * Ratio;
        }`,
    fragmentShader: /* glsl */`
        uniform float Level; varying float Shine;
        void main() { float Fall = smoothstep(0.5, 0.0, length(gl_PointCoord - 0.5)); gl_FragColor = vec4(vec3(1.0), Fall * Shine * Level); }`,
}));
Stars.renderOrder = -2;
Scene.add(Stars);

const PlanetGroup = new THREE.Group();
PlanetGroup.position.set(-68, 12.5, -57);
Scene.add(PlanetGroup);
const PlanetRadius   = 9;
const PlanetUniforms = { ColourA: { value: Live.PlanetA }, ColourB: { value: Live.PlanetB }, Atmos: { value: Live.Atmos }, LightDirection: { value: new THREE.Vector3(0.75, 0.3, -0.35).normalize() }, Clock: { value: 0 } };
const Planet = new THREE.Mesh(new THREE.SphereGeometry(PlanetRadius, 96, 48), new THREE.ShaderMaterial({
    uniforms: PlanetUniforms, fog: false, toneMapped: false,
    vertexShader: /* glsl */`
        varying vec3 Surface, Local, WorldPoint;
        void main()
        {
            Local = normalize(position);
            Surface = normalize(mat3(modelMatrix) * normal);
            WorldPoint = (modelMatrix * vec4(position, 1.0)).xyz;
            gl_Position = projectionMatrix * viewMatrix * vec4(WorldPoint, 1.0);
        }`,
    fragmentShader: /* glsl */`
        uniform vec3 ColourA, ColourB, Atmos, LightDirection; uniform float Clock;
        varying vec3 Surface, Local, WorldPoint;
        void main()
        {
            vec3 N = normalize(Surface);
            vec3 V = normalize(cameraPosition - WorldPoint);
            float Warp  = sin(Local.x * 4.0 + Clock * 0.03) * 0.6 + sin(Local.z * 6.0) * 0.3;
            float Bands = sin(Local.y * 26.0 + Warp) * 0.6 + sin(Local.y * 9.0 - Warp * 0.5) * 0.4;
            float Fine  = sin(Local.y * 70.0 + Local.x * 3.0) * 0.5 + 0.5;
            vec3 Paint = mix(ColourB, ColourA, 0.62 + 0.3 * Bands);
            Paint = mix(Paint, ColourA, Fine * 0.1);
            float Lit = smoothstep(-0.3, 0.7, dot(N, LightDirection));
            vec3 C = Paint * (0.06 + 0.94 * Lit);
            float Rim = pow(1.0 - max(dot(N, V), 0.0), 2.6);
            C += Atmos * Rim * (0.2 + 0.8 * Lit) * 0.9;
            gl_FragColor = vec4(C, 1.0);
            #include <colorspace_fragment>
        }`,
}));
PlanetGroup.add(Planet);

function GlowTexture(Inner = 'rgba(255,255,255,1)', Outer = 'rgba(255,255,255,0)')
{
    const Paint = document.createElement('canvas');
    Paint.width = Paint.height = 128;
    const Pen  = Paint.getContext('2d');
    const Fall = Pen.createRadialGradient(64, 64, 0, 64, 64, 64);
    Fall.addColorStop(0, Inner);
    Fall.addColorStop(1, Outer);
    Pen.fillStyle = Fall;
    Pen.fillRect(0, 0, 128, 128);
    const Texture = new THREE.CanvasTexture(Paint);
    Texture.colorSpace = THREE.SRGBColorSpace;
    return Texture;
}
const SoftGlow = GlowTexture();
const Halo = new THREE.Sprite(new THREE.SpriteMaterial({ map: SoftGlow, color: Live.Atmos, transparent: true, opacity: 0.35, blending: THREE.AdditiveBlending, depthWrite: false, fog: false }));
Halo.scale.setScalar(PlanetRadius * 3.4);
PlanetGroup.add(Halo);

const RingUniforms = { Colour: { value: Live.PlanetA }, Level: { value: 1 }, Inner: { value: PlanetRadius * 1.35 }, Outer: { value: PlanetRadius * 2.2 } };
const PlanetRing = new THREE.Mesh(new THREE.RingGeometry(PlanetRadius * 1.35, PlanetRadius * 2.2, 160, 1), new THREE.ShaderMaterial({
    uniforms: RingUniforms, transparent: true, side: THREE.DoubleSide, depthWrite: false, fog: false, toneMapped: false,
    vertexShader: /* glsl */`varying vec2 Local; void main() { Local = position.xy; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
    fragmentShader: /* glsl */`
        uniform vec3 Colour; uniform float Level, Inner, Outer; varying vec2 Local;
        void main()
        {
            float T = (length(Local) - Inner) / (Outer - Inner);
            float Bands = 0.55 + 0.45 * sin(T * 70.0) * sin(T * 13.0 + 1.0);
            float Alpha = smoothstep(0.0, 0.08, T) * (1.0 - smoothstep(0.85, 1.0, T)) * Bands * 0.55 * Level;
            gl_FragColor = vec4(Colour, Alpha);
            #include <colorspace_fragment>
        }`,
}));
PlanetRing.rotation.set(-1.25, 0.28, 0.2);
PlanetGroup.add(PlanetRing);

//------------------------------------------------------------------------------------------------------------------------
//                                                    DUNES AND MIRROR FLOOR
//------------------------------------------------------------------------------------------------------------------------

function Lattice(X, Z)
{
    const Hash = (I, J) => { const S = Math.sin(I * 127.1 + J * 311.7) * 43758.5453; return S - Math.floor(S); };
    const I = Math.floor(X), J = Math.floor(Z), Fx = X - I, Fz = Z - J;
    const Ux = Fx * Fx * (3 - 2 * Fx), Uz = Fz * Fz * (3 - 2 * Fz);
    return THREE.MathUtils.lerp(THREE.MathUtils.lerp(Hash(I, J), Hash(I + 1, J), Ux), THREE.MathUtils.lerp(Hash(I, J + 1), Hash(I + 1, J + 1), Ux), Uz);
}

const DuneSurface = new THREE.PlaneGeometry(260, 260, 220, 220);
DuneSurface.rotateX(-Math.PI / 2);
const DuneBase = new Float32Array(DuneSurface.attributes.position.count);
const DuneRise = new Float32Array(DuneSurface.attributes.position.count);
{
    const Position = DuneSurface.attributes.position;
    for (let Index = 0; Index < Position.count; Index++)
    {
        const X = Position.getX(Index), Z = Position.getZ(Index), Reach = Math.hypot(X, Z);
        const Ridge = 1 - Math.abs(Math.sin(X * 0.085 + Math.sin(Z * 0.06) * 1.9 + Z * 0.02));
        const Swell = 0.5 + 0.5 * Math.sin(Z * 0.045 - X * 0.025);
        DuneBase[Index] = -2.4 + THREE.MathUtils.smoothstep(Reach, 12, 24) * 2.4;
        DuneRise[Index] = (Ridge * Ridge * 3.4 + Swell * 2.8 + Lattice(X * 0.3, Z * 0.3) * 0.5) * THREE.MathUtils.smoothstep(Reach, 15, 34);
    }
}
let ShapedRelief = -1;
function ShapeDunes(Relief)
{
    if (Math.abs(Relief - ShapedRelief) < 0.003) return;
    ShapedRelief = Relief;
    const Position = DuneSurface.attributes.position;
    for (let Index = 0; Index < Position.count; Index++) Position.setY(Index, DuneBase[Index] + DuneRise[Index] * Relief);
    Position.needsUpdate = true;
    DuneSurface.computeVertexNormals();
}
ShapeDunes(1);
const Dunes = new THREE.Mesh(DuneSurface, new THREE.MeshStandardMaterial({ color: Live.Dune, roughness: 0.94, metalness: 0 }));
Scene.add(Dunes);

const FloorDeep = new THREE.Mesh(new THREE.CircleGeometry(16, 64).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: Live.FloorDeep, fog: false }));
FloorDeep.position.y = -2.3;
Scene.add(FloorDeep);

const FloorAlpha = (() =>
{
    const Paint = document.createElement('canvas');
    Paint.width = Paint.height = 256;
    const Pen  = Paint.getContext('2d');
    const Fall = Pen.createRadialGradient(128, 128, 0, 128, 128, 128);
    Fall.addColorStop(0, 'rgb(150,150,150)');
    Fall.addColorStop(0.45, 'rgb(205,205,205)');
    Fall.addColorStop(0.78, 'rgb(255,255,255)');
    Fall.addColorStop(1, 'rgb(0,0,0)');
    Pen.fillStyle = Fall;
    Pen.fillRect(0, 0, 256, 256);
    return new THREE.CanvasTexture(Paint);
})();
const Floor = new THREE.Mesh(new THREE.CircleGeometry(24, 128).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ color: Live.Floor, roughness: 0.16, metalness: 0.6, transparent: true, alphaMap: FloorAlpha, depthWrite: false }));
Floor.renderOrder = 2;
Scene.add(Floor);

//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE
//------------------------------------------------------------------------------------------------------------------------

const Finish = DefaultFinish({
    [Family.BodyCoat]:  { color: '#efe8f4', roughness: 0.2, metalness: 0.3, clearcoat: 1, clearcoatRoughness: 0.05 },
    [Family.Glass]:     { color: '#0b0812', roughness: 0.04, transparent: true, opacity: 0.94 },
    [Family.Hub]:       { color: '#d8d2e2', roughness: 0.18 },
    [Family.Headlight]: { color: '#3a3448', emissive: '#f4ecff', emissiveIntensity: 1.1 },
    [Family.Taillight]: { color: '#3a0a12', emissive: '#ff2840', emissiveIntensity: 2.0 },
});
const Vehicle = await AssembleVehicle({ Finish });
Scene.add(Vehicle.Root);

// Rear light bar and halo (the reflection carries it into the wet floor).
const TailBar = new THREE.Mesh(new THREE.PlaneGeometry(1.9, 0.045).rotateY(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#ff3a52', transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
TailBar.position.set(-3.13, 1.06, 0);
const TailHalo = new THREE.Mesh(new THREE.PlaneGeometry(3.4, 0.9).rotateY(-Math.PI / 2), new THREE.MeshBasicMaterial({ map: SoftGlow, color: '#ff2a44', transparent: true, opacity: 0.55, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
TailHalo.position.set(-3.15, 1.06, 0);
Vehicle.Root.add(TailBar, TailHalo);

const Mirror = MirrorTwin(Vehicle.Root);
Scene.add(Mirror.Group);

// Exterior lamps are mounted after the mirror twin so the floor pools are not mirrored a second time.
const Lamps = MountLamps(Vehicle, { Extras: { Welcome: true }, Signature: '#fff2fa', PoolLift: 0.02 });
Lamps.HeadBase = 0.6;
Lamps.Set({ Beam: 'Low' });

function ShadeSprite(Width, Depth, Strength)
{
    const Shade = new THREE.Mesh(new THREE.PlaneGeometry(Width, Depth).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ map: GlowTexture('rgba(0,0,0,1)', 'rgba(0,0,0,0)'), transparent: true, opacity: Strength, depthWrite: false }));
    Shade.position.y = 0.006;
    Shade.renderOrder = 3;
    return Shade;
}
Scene.add(ShadeSprite(7.6, 3.8, 0.85));

//------------------------------------------------------------------------------------------------------------------------
//                                                    PARTICLE WAVES
//------------------------------------------------------------------------------------------------------------------------

const WaveColumns = 460, WaveRows = 26;
const WaveGeometry = new THREE.BufferGeometry();
{
    const Grid = new Float32Array(WaveColumns * WaveRows * 2);
    for (let Row = 0; Row < WaveRows; Row++) for (let Column = 0; Column < WaveColumns; Column++)
        Grid.set([Column / WaveColumns + Math.random() * 0.0015, Row / (WaveRows - 1)], (Row * WaveColumns + Column) * 2);
    WaveGeometry.setAttribute('Grid', new THREE.BufferAttribute(Grid, 2));
    WaveGeometry.setAttribute('position', new THREE.BufferAttribute(new Float32Array(WaveColumns * WaveRows * 3), 3));
    WaveGeometry.boundingSphere = new THREE.Sphere(new THREE.Vector3(), 30);
}
const WaveUniforms = { Clock: { value: 0 }, Amplitude: { value: 1 }, Opacity: { value: 1 }, Tint: { value: new THREE.Color('#ffe8f2') }, Ratio: { value: PixelRatio } };
const WaveMaterial = new THREE.ShaderMaterial({
    uniforms: WaveUniforms, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, fog: false, toneMapped: false,
    vertexShader: /* glsl */`
        uniform float Clock, Amplitude, Ratio; attribute vec2 Grid; varying float Fade;
        void main()
        {
            float A   = Grid.x * 6.2831853 + Clock * 0.035;
            float Row = Grid.y;
            float R   = 5.6 + Row * 1.7 + sin(A * 3.0 + Clock * 0.4) * 0.3;
            float Y   = 0.45 + Row * 0.55 + (sin(A * 4.0 + Clock + Row * 2.2) * 0.5 + sin(A * 9.0 - Clock * 0.7 + Row * 5.0) * 0.16) * Amplitude;
            vec4 View = modelViewMatrix * vec4(cos(A) * R * 1.3, max(Y, 0.04), sin(A) * R, 1.0);
            gl_Position  = projectionMatrix * View;
            gl_PointSize = Ratio * (3.4 + 2.2 * sin(A * 13.0 + Row * 7.0 + Clock * 2.0)) * (14.0 / -View.z) + Ratio * 0.8;
            Fade = sin(3.14159 * Row) * (0.55 + 0.45 * sin(A * 2.0 - Clock * 0.5));
        }`,
    fragmentShader: /* glsl */`
        uniform vec3 Tint; uniform float Opacity; varying float Fade;
        void main()
        {
            float Fall = smoothstep(0.5, 0.0, length(gl_PointCoord - 0.5));
            gl_FragColor = vec4(Tint * 1.25, Fall * Fade * Opacity);
            #include <colorspace_fragment>
        }`,
});
const Waves = new THREE.Points(WaveGeometry, WaveMaterial);
Waves.renderOrder = 4;
const WaveEcho = new THREE.Points(WaveGeometry, WaveMaterial);
WaveEcho.scale.y = -1;
WaveEcho.renderOrder = 1;
Scene.add(Waves, WaveEcho);

//------------------------------------------------------------------------------------------------------------------------
//                                                    FLOOR STREAKS, REST HALO, AIRFLOW, SEAT WARMTH
//------------------------------------------------------------------------------------------------------------------------

const StreakCount = 70;
const Streaks = new THREE.InstancedMesh(new THREE.PlaneGeometry(1, 0.03).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#ffffff', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }), StreakCount);
Streaks.renderOrder = 5;
Streaks.frustumCulled = false;
const StreakSeeds = Array.from({ length: StreakCount }, () => ({ X: (Math.random() - 0.5) * 44, Z: (Math.random() < 0.5 ? -1 : 1) * (1.9 + Math.random() * 12), Pace: 0.6 + Math.random() * 0.8 }));
Scene.add(Streaks);
const Placement = new THREE.Object3D();

const RestUniforms = { Progress: { value: 1 }, Level: { value: 0 }, Colour: { value: new THREE.Color('#ffffff') } };
const RestHalo = new THREE.Mesh(new THREE.RingGeometry(4.55, 4.66, 256, 1), new THREE.ShaderMaterial({
    uniforms: RestUniforms, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, toneMapped: false,
    vertexShader: /* glsl */`varying vec2 Local; void main() { Local = position.xy; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
    fragmentShader: /* glsl */`
        uniform float Progress, Level; uniform vec3 Colour; varying vec2 Local;
        void main()
        {
            float Turn = fract(atan(Local.x, Local.y) / 6.2831853 + 1.0);
            float Lit  = step(Turn, Progress);
            gl_FragColor = vec4(Colour, (0.12 + 0.88 * Lit) * Level);
        }`,
}));
RestHalo.rotation.x = -Math.PI / 2;
RestHalo.scale.x = 1.3;
RestHalo.position.y = 0.012;
RestHalo.renderOrder = 5;
Scene.add(RestHalo);

// 📝 Cabin airflow: points travel from the dash vents to the rear bench; colour follows each zone's temperature.
const AirCount    = 900;
const AirPoints   = new Float32Array(AirCount * 3);
const AirColours  = new Float32Array(AirCount * 3);
const AirSeeds    = Array.from({ length: AirCount }, () => ({ Phase: Math.random(), Lane: (Math.random() - 0.5) * 1.3, Lift: Math.random(), Wobble: Math.random() * 6.28, Low: Math.random() < 0.5 }));
const AirGeometry = new THREE.BufferGeometry();
AirGeometry.setAttribute('position', new THREE.BufferAttribute(AirPoints, 3));
AirGeometry.setAttribute('color', new THREE.BufferAttribute(AirColours, 3));
const Airflow = new THREE.Points(AirGeometry, new THREE.PointsMaterial({ size: 0.055, vertexColors: true, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, depthTest: false, map: SoftGlow }));
Airflow.frustumCulled = false;
Airflow.renderOrder = 6;
Vehicle.BodyMount.add(Airflow);

const SeatGlows = [-0.46, 0.46].map((Z) =>
{
    const Glow = new THREE.Sprite(new THREE.SpriteMaterial({ map: SoftGlow, color: '#ff8a4a', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, depthTest: false }));
    Glow.position.set(-0.25, 0.72, Z);
    Glow.scale.set(0.9, 0.9, 1);
    Glow.renderOrder = 6;
    Vehicle.BodyMount.add(Glow);
    return Glow;
});

const ColdAir = new THREE.Color('#7cc8ff'), WarmAir = new THREE.Color('#ffae78'), NeutralAir = new THREE.Color('#f2ecff');
function ZoneColour(Temperature, Target)
{
    if (Temperature < 22) return Target.copy(NeutralAir).lerp(ColdAir, THREE.MathUtils.clamp((22 - Temperature) / 5, 0, 1));
    return Target.copy(NeutralAir).lerp(WarmAir, THREE.MathUtils.clamp((Temperature - 22) / 5, 0, 1));
}

function AdvanceAirflow(Delta)
{
    const Rate = 0.12 + Condition.Fan * 0.07;
    const Zones = [ZoneColour(Condition.Temps[0], new THREE.Color()), ZoneColour(Condition.Temps[1], new THREE.Color())];
    for (let Index = 0; Index < AirCount; Index++)
    {
        const Seed = AirSeeds[Index];
        Seed.Phase += Delta * Rate * (0.7 + Seed.Lift * 0.6);
        if (Seed.Phase > 1) Seed.Phase -= 1;
        const T     = Seed.Phase;
        const Feet  = Condition.Air === 'feet' || (Condition.Air === 'both' && Seed.Low);
        const Start = Feet ? 0.38 : 0.92;
        const X     = 1.15 - T * 2.55;
        const Y     = Start + Math.sin(T * Math.PI) * (Feet ? 0.12 : -0.12) + Seed.Lift * 0.16 - (Condition.Recirculate ? Math.sin(T * Math.PI * 2) * 0.14 : 0);
        const Z     = Seed.Lane * (0.45 + T * 0.4) + Math.sin(T * 9 + Seed.Wobble) * 0.05;
        AirPoints.set([X, Y, Z], Index * 3);
        const Colour = Zones[Z < 0 ? 0 : 1];
        const Fade   = Math.sin(T * Math.PI);
        AirColours.set([Colour.r * Fade, Colour.g * Fade, Colour.b * Fade], Index * 3);
    }
    AirGeometry.attributes.position.needsUpdate = true;
    AirGeometry.attributes.color.needsUpdate    = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOAST
//------------------------------------------------------------------------------------------------------------------------

let ToastTimer = 0;
function Announce(Html)
{
    const Toast = Find('Toast');
    Toast.innerHTML = Html;
    Toast.classList.add('Show');
    clearTimeout(ToastTimer);
    ToastTimer = setTimeout(() => Toast.classList.remove('Show'), 2400);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MENUS
//------------------------------------------------------------------------------------------------------------------------

function OpenMenu(Menu)
{
    if (!MenuOrder.includes(Menu)) return;
    Condition.Menu = Menu;
    const Dock = Find('Dock');
    Dock.querySelectorAll('[data-menu]').forEach((Button) => Button.classList.toggle('On', Button.dataset.menu === Menu));
    const Active = Dock.querySelector(`[data-menu="${Menu}"]`);
    Find('DockPill').style.transform = `translateX(${Active.offsetLeft - 6}px)`;
    document.querySelectorAll('.Page').forEach((Page) => Page.classList.toggle('On', Page.dataset.page === Menu));
    Display.classList.toggle('Driving', Menu === 'Drive');
    Display.classList.toggle('LightsOn', Menu === 'Lights');
    Find('MiniPlayer').classList.toggle('Away', Menu === 'Media');
    Orbit.Aim(CameraPresets[Menu], 1400);
    Orbit.Drift = Menu === 'Rest' ? 0.025 : Menu === 'Media' ? 0.035 : 0;
}

Find('Dock').addEventListener('click', (Press) => { const Button = Press.target.closest('[data-menu]'); if (Button) OpenMenu(Button.dataset.menu); });
Find('Orbs').addEventListener('click', (Press) =>
{
    const Orb = Press.target.closest('.Orb');
    if (!Orb) return;
    Orb.animate([{ transform: 'scale(1)' }, { transform: 'scale(1.12)' }, { transform: 'scale(1)' }], { duration: 420, easing: 'cubic-bezier(.3,1.6,.5,1)' });
    OpenMenu(Orb.dataset.open);
});

function SlideSegment(Segment, Button)
{
    Segment.querySelectorAll('button').forEach((Each) => Each.classList.toggle('On', Each === Button));
    const Pill = Segment.querySelector('i');
    Pill.style.width     = `${Button.offsetWidth}px`;
    Pill.style.transform = `translateX(${Button.offsetLeft - 4}px)`;
}

function BindSlider(Slider, Change)
{
    const Apply = (Ratio) =>
    {
        Ratio = THREE.MathUtils.clamp(Ratio, 0, 1);
        Slider.querySelector('.Fill').style.width = `${Ratio * 100}%`;
        Slider.querySelector('.Knob').style.left  = `${Ratio * 100}%`;
        Change(Ratio);
    };
    const FromPointer = (Event) => { const Box = Slider.getBoundingClientRect(); Apply((Event.clientX - Box.left) / Box.width); };
    Slider.addEventListener('pointerdown', (Press) => { Slider.setPointerCapture(Press.pointerId); FromPointer(Press); });
    Slider.addEventListener('pointermove', (Motion) => { if (Slider.hasPointerCapture(Motion.pointerId)) FromPointer(Motion); });
    Apply(Number(Slider.dataset.value));
    return Apply;
}

// ── Horizon
function SetScene(Index)
{
    Condition.Scene = Index;
    const Palette = Palettes[Index];
    ColourKeys.forEach((Key, Slot) => GoalColours[Slot].set(Palette[Key]));
    Find('Scenes').querySelectorAll('.SceneCard').forEach((Card) => Card.classList.toggle('On', Number(Card.dataset.scene) === Index));
    Announce(`World · <b>${Palette.Name}</b>`);
}
Find('Scenes').addEventListener('click', (Press) => { const Card = Press.target.closest('.SceneCard'); if (Card) SetScene(Number(Card.dataset.scene)); });
BindSlider(Find('HazeSlider'), (Ratio) => { Condition.Haze = Ratio; Find('HazeText').textContent = Math.round(Ratio * 100); });
Find('WaveToggle').addEventListener('click', () => { Condition.Waves = !Condition.Waves; Find('WaveToggle').classList.toggle('On', Condition.Waves); });

// ── Drive
function SetMood(Index)
{
    Condition.Mood = Index;
    const Mood = Moods[Index];
    Display.style.setProperty('--Accent', Mood.Accent);
    Display.style.setProperty('--AccentRGB', Mood.Rgb);
    Find('Moods').querySelectorAll('button').forEach((Button) => Button.classList.toggle('On', Number(Button.dataset.mood) === Index));
    RollNumber(Find('EffText'), [13.1, 14.2, 17.6][Index], { Format: (Figure) => Figure.toFixed(1) });
    Train.SetStyle(['Comfort', 'Sport', 'Track'][Index]);
    RefreshSpeedNote();
}
Find('Moods').addEventListener('click', (Press) => { const Button = Press.target.closest('[data-mood]'); if (Button) SetMood(Number(Button.dataset.mood)); });

function SetRegen(Level)
{
    Condition.Regen = Level;
    Find('RegenSteps').querySelectorAll('button').forEach((Button) => Button.classList.toggle('On', Number(Button.dataset.regen) === Level));
    Find('RegenArc').style.strokeDashoffset = `${252 * (1 - (Level + 0.25) / 3.25)}`;
    RollNumber(Find('RegenText'), Level);
}
Find('RegenSteps').addEventListener('click', (Press) => { const Button = Press.target.closest('[data-regen]'); if (Button) SetRegen(Number(Button.dataset.regen)); });

function RefreshSpeedNote()
{
    Find('SpeedNote').textContent = Condition.Gliding ? `Gliding · ${Moods[Condition.Mood].Name} · regen ${Condition.Regen}` : Condition.Speed > 1 ? 'Coasting to a stop' : 'Parked · ready';
}
Find('GlideButton').addEventListener('click', () =>
{
    Condition.Gliding = !Condition.Gliding;
    Find('GlideButton').textContent = Condition.Gliding ? 'End glide' : 'Preview glide';
    RefreshSpeedNote();
    if (Condition.Gliding && Condition.Menu !== 'Drive') OpenMenu('Drive');
});

// ── Climate
function SetFan(Level)
{
    Condition.Fan = THREE.MathUtils.clamp(Math.round(Level), 0, 7);
    Find('FanArc').style.strokeDasharray = `${Math.max(0.01, 433.5 * Condition.Fan / 7)} 578`;
    Find('FanText').textContent = Condition.Fan;
}
{
    const Dial = Find('FanDial');
    const FromPointer = (Event) =>
    {
        const Box = Dial.getBoundingClientRect();
        const Degree = Math.atan2(Event.clientY - (Box.top + Box.height / 2), Event.clientX - (Box.left + Box.width / 2)) * 180 / Math.PI;
        let Sweep = (Degree - 135 + 720) % 360;
        if (Sweep > 270) Sweep = Sweep > 315 ? 0 : 270;
        SetFan(Sweep / 270 * 7);
    };
    Dial.addEventListener('pointerdown', (Press) => { Dial.setPointerCapture(Press.pointerId); FromPointer(Press); });
    Dial.addEventListener('pointermove', (Motion) => { if (Dial.hasPointerCapture(Motion.pointerId)) FromPointer(Motion); });
}
Find('AirMode').addEventListener('click', (Press) => { const Button = Press.target.closest('button'); if (!Button) return; Condition.Air = Button.dataset.air; SlideSegment(Find('AirMode'), Button); });
Find('AutoToggle').addEventListener('click', () => { Condition.Auto = !Condition.Auto; Find('AutoToggle').classList.toggle('On', Condition.Auto); });
Find('RecircToggle').addEventListener('click', () => { Condition.Recirculate = !Condition.Recirculate; Find('RecircToggle').classList.toggle('On', Condition.Recirculate); });

function SetTemperature(Zone, Value)
{
    Condition.Temps[Zone] = THREE.MathUtils.clamp(Value, 16, 28);
    const Readout = Find(Zone === 0 ? 'TempLeft' : 'TempRight');
    RollNumber(Readout, Condition.Temps[Zone], { Duration: 380, Format: (Figure) => Figure.toFixed(1) });
    const Box = Readout.parentElement;
    Box.classList.toggle('Warm', Condition.Temps[Zone] > 23);
    Box.classList.toggle('Cool', Condition.Temps[Zone] < 21);
    if (Condition.Auto) SetFan(3 + Math.abs(Condition.Temps[Zone] - 22) * 0.7);
}
document.querySelectorAll('.Chevron').forEach((Button) => Button.addEventListener('click', () =>
{
    const Zone = Number(Button.dataset.zone);
    SetTemperature(Zone, Condition.Temps[Zone] + Number(Button.dataset.step));
}));
['SeatLeft', 'SeatRight'].forEach((Id, Zone) => Find(Id).addEventListener('click', () =>
{
    Condition.Seats[Zone] = (Condition.Seats[Zone] + 1) % 4;
    Find(Id).dataset.level = Condition.Seats[Zone];
    Announce(`${Zone === 0 ? 'Driver' : 'Passenger'} seat warmth · <b>${['off', 'low', 'medium', 'high'][Condition.Seats[Zone]]}</b>`);
}));

// ── Rest
function RestClock(Seconds)
{
    const Whole = Math.max(0, Math.ceil(Seconds));
    return `${String(Math.floor(Whole / 60)).padStart(2, '0')}:${String(Whole % 60).padStart(2, '0')}`;
}
function BeginRest(On = !Condition.Resting)
{
    Condition.Resting = On;
    if (On) Condition.RestLeft = Condition.RestTotal;
    Display.classList.toggle('Resting', On);
    Find('RestButton').textContent = On ? 'End rest' : 'Begin rest';
    Find('RestNote').textContent   = On ? 'Resting' : 'Ready';
    if (!On) { Find('RestText').textContent = RestClock(Condition.RestTotal); Find('RestArc').style.strokeDashoffset = '0'; }
    if (On && Condition.Menu !== 'Rest') OpenMenu('Rest');
}
Find('RestButton').addEventListener('click', () => BeginRest());
Find('RestPresets').addEventListener('click', (Press) =>
{
    const Button = Press.target.closest('[data-minutes]');
    if (!Button) return;
    Find('RestPresets').querySelectorAll('button').forEach((Each) => Each.classList.toggle('On', Each === Button));
    Condition.RestTotal = Number(Button.dataset.minutes) * 60;
    Condition.RestLeft  = Condition.RestTotal;
    Find('RestText').textContent = RestClock(Condition.RestTotal);
});

// ── Media
const SpectrumBars = 30;
Find('Spectrum').innerHTML = '<i></i>'.repeat(SpectrumBars);
const SpectrumCells = [...Find('Spectrum').children];
function TogglePlay(On = !Condition.Playing)
{
    Condition.Playing = On;
    Display.classList.toggle('Playing', On);
}
function ShowTrack(Index)
{
    Condition.Track = (Index + Tracks.length) % Tracks.length;
    Condition.TrackTime = 0;
    const Track = Tracks[Condition.Track];
    document.querySelector('.MiniText b').textContent = Track.Title;
    document.querySelector('.MiniText small').textContent = Track.Artist;
    document.querySelector('.Page[data-page="Media"] .Track b').textContent = Track.Title;
    document.querySelector('.Page[data-page="Media"] .Track small').textContent = `${Track.Artist} · ${Track.Album}`;
    document.querySelector('.Times span:last-child').textContent = `${Math.floor(Track.Length / 60)}:${String(Track.Length % 60).padStart(2, '0')}`;
    for (const Cover of document.querySelectorAll('.Cover, .BigCover')) Cover.style.filter = `hue-rotate(${Track.Hue}deg)`;
    Announce(`Now playing · <b>${Track.Title}</b>`);
}
Find('MiniPlay').addEventListener('click', () => TogglePlay());
Find('BigPlay').addEventListener('click', () => TogglePlay());
Find('PrevTrack').addEventListener('click', () => ShowTrack(Condition.Track - 1));
Find('NextTrack').addEventListener('click', () => ShowTrack(Condition.Track + 1));
Find('SyncToggle').addEventListener('click', () => { Condition.MusicSync = !Condition.MusicSync; Find('SyncToggle').classList.toggle('On', Condition.MusicSync); });
{
    const Scrub = Find('Scrub');
    const FromPointer = (Event) => { const Box = Scrub.getBoundingClientRect(); Condition.TrackTime = THREE.MathUtils.clamp((Event.clientX - Box.left) / Box.width, 0, 1) * Tracks[Condition.Track].Length; };
    Scrub.addEventListener('pointerdown', (Press) => { Scrub.setPointerCapture(Press.pointerId); FromPointer(Press); });
    Scrub.addEventListener('pointermove', (Motion) => { if (Scrub.hasPointerCapture(Motion.pointerId)) FromPointer(Motion); });
}
function RefreshPlayer()
{
    const Track = Tracks[Condition.Track];
    const Ratio = Condition.TrackTime / Track.Length;
    Find('ScrubFill').style.width = `${Ratio * 100}%`;
    Find('ScrubKnob').style.left  = `${Ratio * 100}%`;
    Find('MiniProgress').style.width = `${Ratio * 100}%`;
    const Whole = Math.floor(Condition.TrackTime);
    Find('TimeNow').textContent = `${Math.floor(Whole / 60)}:${String(Whole % 60).padStart(2, '0')}`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    STATUS
//------------------------------------------------------------------------------------------------------------------------

function RefreshClock()
{
    const [Hours, Minutes] = ClockText().split(':');
    Find('ClockHours').textContent   = Hours;
    Find('ClockMinutes').textContent = Minutes;
    Find('ClockDate').textContent    = new Date().toLocaleDateString('en-GB', { weekday: 'long', day: 'numeric', month: 'long' }).replace(',', ' ·');
}
RefreshClock();
setInterval(RefreshClock, 10000);

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME STEP
//------------------------------------------------------------------------------------------------------------------------

let WaveClock = 0, Beat = 0, LastRestText = '', LastSpeed = -1, LastWall = performance.now();
const Follow = (Rate, Delta) => 1 - Math.exp(-Delta * Rate);

RunLoop((Delta, Elapsed) =>
{
    // Palette easing runs on wall time so a slow frame rate never strands a half-blended world.
    const Now  = performance.now();
    const Wall = Math.min(0.5, (Now - LastWall) / 1000);
    LastWall = Now;
    const Mix = Follow(2.2, Wall);
    ColourKeys.forEach((Key, Slot) => Live[Key].lerp(GoalColours[Slot], Mix));
    const Palette = Palettes[Condition.Scene];
    for (const Key of LevelKeys) Live[Key] += (Palette[Key] - Live[Key]) * Mix;
    ShapeDunes(Live.Relief);
    Scene.fog.color.copy(Live.Fog);
    Scene.fog.far  = THREE.MathUtils.lerp(170, 55, Condition.Haze);
    Scene.fog.near = THREE.MathUtils.lerp(26, 10, Condition.Haze);
    Hemisphere.color.copy(Live.Sky);
    Hemisphere.groundColor.copy(Live.Ground);
    KeyLight.color.copy(Live.Key);
    SkyUniforms.Aurora.value = Live.Aurora;
    SkyUniforms.Clock.value  = Elapsed;
    PlanetGroup.scale.setScalar(Live.PlanetScale);
    RingUniforms.Level.value = Live.Ring;
    PlanetUniforms.Clock.value = Elapsed;
    Planet.rotation.y += Delta * 0.01;
    StarUniforms.Clock.value = Elapsed;
    Sky.position.copy(Camera.position);
    Stars.position.copy(Camera.position);

    // Rest dims the world and lifts the stars.
    const RestGoal = Condition.Resting ? 1 : 0;
    RestUniforms.Level.value += (RestGoal - RestUniforms.Level.value) * Follow(2.5, Delta);
    const Rest = RestUniforms.Level.value;
    StarUniforms.Level.value = THREE.MathUtils.lerp(Live.Stars, Math.max(Live.Stars, 0.9), Rest);
    Renderer.toneMappingExposure = Live.Exposure * (1 - Rest * 0.32);
    Finish[Family.Glass].color.setRGB(0.004 + Rest * 0.03, 0.002 + Rest * 0.012, 0.01 + Rest * 0.02);
    if (Condition.Resting)
    {
        Condition.RestLeft -= Delta;
        if (Condition.RestLeft <= 0) { BeginRest(false); Announce('Rest complete · <b>welcome back</b>'); }
    }
    RestUniforms.Progress.value = Condition.Resting ? Condition.RestLeft / Condition.RestTotal : 1;
    if (Condition.Resting)
    {
        const Text = RestClock(Condition.RestLeft);
        if (Text !== LastRestText) { Find('RestText').textContent = Text; LastRestText = Text; }
        Find('RestArc').style.strokeDashoffset = `${653.5 * (1 - Condition.RestLeft / Condition.RestTotal)}`;
    }

    // Music beat, spectrum, player.
    if (Condition.Playing)
    {
        Condition.TrackTime += Delta;
        if (Condition.TrackTime >= Tracks[Condition.Track].Length) ShowTrack(Condition.Track + 1);
        Beat = Math.pow(0.5 + 0.5 * Math.sin(Elapsed * Math.PI * 4), 6);
        SpectrumCells.forEach((Cell, Index) =>
        {
            const Height = (0.12 + 0.88 * Lattice(Index * 0.55, Elapsed * 3.2)) * (0.55 + 0.45 * Beat) * (1 - Index / SpectrumBars * 0.45);
            Cell.style.height = `${Math.round(Height * 100)}%`;
        });
    }
    else
    {
        Beat *= 0.9;
        SpectrumCells.forEach((Cell) => { Cell.style.height = '8%'; });
    }
    RefreshPlayer();

    // Glide: wheels, speed readout, floor streaks.
    const Mood = Moods[Condition.Mood];
    const SpeedGoal = Condition.Gliding ? [62, 86, 118][Condition.Mood] : 0;
    Condition.Glide += (SpeedGoal - Condition.Glide) * Follow(Condition.Gliding ? 0.7 : 0.35 + Condition.Regen * 0.25, Delta);
    if (Math.abs(Condition.Glide) < 0.2 && !Condition.Gliding) Condition.Glide = 0;
    // The Halo dashboard cluster drives when it is open (cabin link); otherwise the HMI's own powertrain idles.
    Train.Step();
    Motion = Link.Fresh('Drive') || Train;
    Condition.Speed = Math.max(Condition.Glide, Motion.Kmh);
    const Shown = Math.round(Condition.Speed);
    if (Shown !== LastSpeed) { Find('SpeedText').textContent = Shown; LastSpeed = Shown; if (Shown === 0 || Shown === 1) RefreshSpeedNote(); }
    const Metres = Condition.Speed / 3.6;
    for (const Wheel of Vehicle.Wheels) Wheel.Spin.rotation.z -= (Metres / 0.509) * Delta;
    Streaks.material.opacity = THREE.MathUtils.clamp(Condition.Speed / 60, 0, 1) * 0.55;
    Streaks.material.color.set(Mood.Accent);
    if (Streaks.material.opacity > 0.01)
    {
        StreakSeeds.forEach((Seed, Index) =>
        {
            Seed.X -= Metres * Seed.Pace * Delta;
            if (Seed.X < -22) Seed.X += 44;
            Placement.position.set(Seed.X, 0.01, Seed.Z);
            Placement.scale.set(0.4 + Metres * 0.12 * Seed.Pace, 1, 1);
            Placement.updateMatrix();
            Streaks.setMatrixAt(Index, Placement.matrix);
        });
        Streaks.instanceMatrix.needsUpdate = true;
    }

    // Waves.
    const WavePace = Mood.WaveSpeed * (1 - Rest * 0.7) + Metres * 0.06;
    WaveClock += Delta * WavePace;
    WaveUniforms.Clock.value     = WaveClock;
    WaveUniforms.Amplitude.value = 1 + (Condition.MusicSync ? Beat * 0.9 : 0) + Metres * 0.012;
    WaveUniforms.Opacity.value  += ((Condition.Waves ? 1 : 0) - WaveUniforms.Opacity.value) * Follow(3, Delta);
    WaveUniforms.Tint.value.copy(Live.Tint).lerp(new THREE.Color(Mood.Accent), Condition.Menu === 'Drive' ? 0.6 : 0.25);

    // Tail light follows mood; a soft brake swell while coasting down.
    const Braking = (!Condition.Gliding && Condition.Glide > 2 ? 0.8 : 0) + (Motion.Brake > 0.08 ? 1 : 0);
    Lamps.TailBase = 0.5 + 1.1 * Mood.Tail;
    Lamps.State.Brake = Braking > 0 ? 1 : 0;
    Lamps.State.Reverse = Motion.Selector === 'R';
    if (Elapsed - LampSent > 0.4) { LampSent = Elapsed; Link.Send('Lamps', { ...Lamps.State }); }
    Lamps.Speed = Condition.Speed;
    Lamps.Step();
    const RibbonLevel = Condition.Ribbon ? 1 : 0;
    TailHalo.material.opacity += ((0.35 + 0.2 * Mood.Tail + Math.min(1, Braking) * 0.3) * RibbonLevel - TailHalo.material.opacity) * Follow(6, Delta);
    TailBar.material.opacity  += (RibbonLevel - TailBar.material.opacity) * Follow(6, Delta);

    // Lamp tell-tales.
    if (Condition.Menu === 'Lights') RefreshReach();

    // Climate: glass clears, airflow and seat warmth appear.
    const InClimate = Condition.Menu === 'Climate' ? 1 : 0;
    Finish[Family.Glass].opacity += ((InClimate ? 0.22 : 0.94) - Finish[Family.Glass].opacity) * Follow(3, Delta);
    Airflow.material.opacity     += (InClimate * (Condition.Fan > 0 ? 1 : 0) - Airflow.material.opacity) * Follow(3, Delta);
    if (Airflow.material.opacity > 0.01) AdvanceAirflow(Delta);
    SeatGlows.forEach((Glow, Zone) => { Glow.material.opacity += ((InClimate ? Condition.Seats[Zone] * 0.28 : 0) - Glow.material.opacity) * Follow(3, Delta); });
    const Blades = Find('FanBlades');
    Blades.style.transform = `rotate(${(Elapsed * Condition.Fan * 120) % 360}deg)`;

    Orbit.Advance(Delta);
    Mirror.Sync();
    Renderer.render(Scene, Camera);
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    BOOT
//------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------
//                                                    POWERTRAIN, CABIN LINK AND LIGHTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The instrument cluster lives on the dashboard (Clusters/Halo), not in this HMI. When it is open the link feeds
//    speed, gear and pedals here and this HMI sends its lamp switches back; alone, the car stands parked.

const BeamNames = { Off: 'Daytime lamps', Auto: 'Auto · dusk sensing', Parking: 'Parking lamps', Low: 'Low beam', High: 'High beam' };
const LampPage = BindLampControls(Find('LightsPage'), Lamps, {
    Custom: { Ribbon: { Get: () => Condition.Ribbon, Set: (On) => { Condition.Ribbon = On; } } },
    OnChange: () => SlideSegment(Find('LightsPage').querySelector('.Segment.Beams'), Find('LightsPage').querySelector('[data-beam].On')),
});
function RefreshReach()
{
    const Beams = Lamps.Beams(), Reach = Find('Reach');
    Reach.querySelector('.Fog').classList.toggle('Lit', Lamps.State.FogFront);
    Reach.querySelector('.Low').classList.toggle('Lit', Beams.Low);
    Reach.querySelector('.High').classList.toggle('Lit', Beams.High);
    const Note = Beams.High ? 'High beam · 180 m' : Lamps.Oncoming && Lamps.State.AutoHigh ? 'Dipped for oncoming traffic' : BeamNames[Lamps.State.Beam];
    if (Find('ReachNote').textContent !== Note) Find('ReachNote').textContent = Note;
}
function SetLamps(Patch) { Lamps.Set(Patch); LampPage.Refresh(); SlideSegment(Find('LightsPage').querySelector('.Segment.Beams'), Find('LightsPage').querySelector('[data-beam].On')); }

SetMood(0);
SetRegen(2);
SetFan(3);
SlideSegment(Find('AirMode'), Find('AirMode').querySelector('.On'));
OpenMenu('Horizon');
SetLamps({ Beam: 'Low' });
ShowTrack(0);
Condition.TrackTime = 72;
Find('Toast').classList.remove('Show');

window.HmiCondition = { Condition, OpenMenu, SetScene, SetMood, SetRegen, SetFan, SetTemperature, BeginRest, TogglePlay, ShowTrack, Orbit, Train, Link, Lamps, SetLamps };

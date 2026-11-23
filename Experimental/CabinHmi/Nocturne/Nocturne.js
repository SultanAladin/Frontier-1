// NOCTURNE — CABIN HMI PROTOTYPE C
// 🧩 Night-lake display for the winged ControlVehicle. A still mirror lake lies under stars, a low moon and layered
//    mountains. Menus: Home (glass card row), Drive (power current: the body turns to glass and the hybrid's energy flows
//    through it), Charge (aurora sky,
//    charge rig, flowing floor dashes), Comfort (the car fades and two 3D seats rise from the lake; each
//    slides fore and aft and its ring dot sets the back angle; climate around them) and Media (3D cover flow over the water, beat rings on the lake).

import { THREE, AssembleVehicle, DefaultFinish, PanelEnvironment, Family, MirrorTwin } from '../Shared/VehicleAssembly.js';
import { FitDisplay, CameraOrbit, ProjectAnchor, RollNumber, BindRipples, ClockText, RunLoop } from '../Shared/DisplayKit.js';
import { Powertrain } from '../Shared/Powertrain.js';
import { MountLamps, BindLampControls } from '../Shared/LampRig.js';
import { OpenLink } from '../Shared/CabinLink.js';

// Powertrain and cabin link (see POWERTRAIN AND CABIN LINK below): declared first so every section can read them.
const Train = new Powertrain({ Ratios: [3.6, 2.4, 1.75, 1.32, 1.02, 0.8], Final: 3.4, Redline: 6200, Limit: 6900, PeakTorque: 520, Mass: 1580, Tank: 55 });
const Link  = OpenLink('Nocturne');
Train.Fuel = 0.47;                                                  // matches the Tide dashboard cluster
let Motion = Train, LampSent = 0;                                   // Motion: the live drive readings (link or own)

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const DisplayWidth = 1600, DisplayHeight = 900;
const MenuOrder    = ['Home', 'Drive', 'Lights', 'Charge', 'Comfort', 'Media'];

// 📝 One look per menu; every colour and level eases towards the active look on wall-clock time.
const Looks = {
    Home:    { Horizon: '#2c4774', Zenith: '#03060e', Water: '#081222', Glow: '#a9c8ff', Under: '#6f9cff', AuroraA: '#3dffa0', AuroraB: '#3a6bff',
               Aurora: 0.06, Flow: 0, Ghost: 0, Stars: 1.0, UnderLevel: 0.35, CarFade: 1, SeatRise: 0, Covers: 0, ChargeRig: 0 },
    Drive:   { Horizon: '#2b2238', Zenith: '#020206', Water: '#06060c', Glow: '#ffb48a', Under: '#ff7a3a', AuroraA: '#3dffa0', AuroraB: '#3a6bff',
               Aurora: 0.0, Flow: 1, Ghost: 1, Stars: 0.45, UnderLevel: 0.55, CarFade: 1, SeatRise: 0, Covers: 0, ChargeRig: 0 },
    Charge:  { Horizon: '#14403f', Zenith: '#01080a', Water: '#041318', Glow: '#5dffa8', Under: '#2dff88', AuroraA: '#4dffa6', AuroraB: '#22b7c9',
               Aurora: 1.0, Flow: 0, Ghost: 0, Stars: 0.85, UnderLevel: 0.9, CarFade: 1, SeatRise: 0, Covers: 0, ChargeRig: 1 },
    Comfort: { Horizon: '#252a44', Zenith: '#03040a', Water: '#070912', Glow: '#dfe6ff', Under: '#ffffff', AuroraA: '#3dffa0', AuroraB: '#3a6bff',
               Aurora: 0.0, Flow: 0, Ghost: 0, Stars: 0.6, UnderLevel: 0.0, CarFade: 0, SeatRise: 1, Covers: 0, ChargeRig: 0 },
    Lights:  { Horizon: '#16203a', Zenith: '#020309', Water: '#04060c', Glow: '#e8f0ff', Under: '#6f9cff', AuroraA: '#3dffa0', AuroraB: '#3a6bff',
               Aurora: 0.0, Flow: 0, Ghost: 0, Stars: 0.95, UnderLevel: 0.05, CarFade: 1, SeatRise: 0, Covers: 0, ChargeRig: 0 },
    Media:   { Horizon: '#33285e', Zenith: '#040314', Water: '#0a0920', Glow: '#c9b6ff', Under: '#a98cff', AuroraA: '#b38cff', AuroraB: '#ff7ad0',
               Aurora: 0.35, Flow: 0, Ghost: 0, Stars: 0.9, UnderLevel: 0.45, CarFade: 1, SeatRise: 0, Covers: 1, ChargeRig: 0 },
};
const ColourKeys = ['Horizon', 'Zenith', 'Water', 'Glow', 'Under', 'AuroraA', 'AuroraB'];
const LevelKeys  = ['Aurora', 'Flow', 'Ghost', 'Stars', 'UnderLevel', 'CarFade', 'SeatRise', 'Covers', 'ChargeRig'];

const DriveModes = [
    { Name: 'Eco',         Key: 'Eco',     Speed: 80,  Colours: ['#2fd6a0', '#1f7fd1', '#c8ffe6'], Electric: 90, Reach: 1.12, Note: 'Electric up to 90 km/h · gentle throttle · strongest regen.' },
    { Name: 'City cruise', Key: 'City',    Speed: 60,  Colours: ['#ff6a2a', '#ff4f9a', '#3f7bff'], Electric: 50, Reach: 1.0,  Note: 'Electric below 50 km/h · the engine tops up the battery above it.' },
    { Name: 'Highway',     Key: 'Highway', Speed: 110, Colours: ['#58c8ff', '#9db8ff', '#ffffff'], Electric: 25, Reach: 0.9,  Note: 'Engine cruise with a trickle charge · adaptive cruise, lane centring.' },
    { Name: 'Sport',       Key: 'Sport',   Speed: 140, Colours: ['#ff2d3a', '#ff8a1e', '#ffd1a0'], Electric: -1, Reach: 0.74, Note: 'Engine plus e-boost on every press · sharp throttle, firm damping.' },
];

const Tracks = [
    { Title: 'Low Tide Lights',      Artist: 'Ines Marlow',  Album: 'Harbour Sessions', Length: 228, Hues: [212, 262], Art: 'Moon' },
    { Title: 'Glass Harbour',        Artist: 'Teo Varga',    Album: 'Still Water',      Length: 201, Hues: [186, 226], Art: 'Waves' },
    { Title: 'Northern Static',      Artist: 'Kalani Reyes', Album: 'Aurora Tapes',     Length: 254, Hues: [146, 196], Art: 'Aurora' },
    { Title: 'Silver Meridian',      Artist: 'June Okafor',  Album: 'Meridian',         Length: 213, Hues: [236, 300], Art: 'Rings' },
    { Title: 'Paper Moons',          Artist: 'Odile Brandt', Album: 'Paper Moons',      Length: 187, Hues: [16, 334],  Art: 'Sun' },
    { Title: 'Quiet Engines',        Artist: 'Ravi Lund',    Album: 'Night Freight',    Length: 242, Hues: [8, 38],    Art: 'Stripes' },
    { Title: 'Midnight Cartography', Artist: 'Sela Moore',   Album: 'Charts',           Length: 266, Hues: [268, 204], Art: 'Grid' },
];

const CameraPresets = {
    Home:    { Target: [0.0, 0.35, 0],   Radius: 14.5, Yaw: 0.95,  Pitch: 0.07 },
    Drive:   { Target: [0.1, 0.45, 0],   Radius: 13.2, Yaw: 0.62,  Pitch: 0.6 },
    Charge:  { Target: [-2.27, 1.0, 0],  Radius: 14,   Yaw: 0.06,  Pitch: 0.05 },
    Comfort: { Target: [0.0, 1.5, 0],    Radius: 12.5, Yaw: 0.0,   Pitch: 0.05 },
    Media:   { Target: [0.0, 1.6, 0],    Radius: 15,   Yaw: 0.55,  Pitch: 0.08 },
    Lights:  { Target: [2.4, 1.1, -0.6], Radius: 14.5, Yaw: 2.15,  Pitch: 0.1 },
};
// Media keeps the car left of the player panel: shift the target along the camera's right vector.
{
    const Yaw = CameraPresets.Media.Yaw, Shift = 2.3;
    CameraPresets.Media.Target = [Math.cos(Yaw) * Shift, 1.6, -Math.sin(Yaw) * Shift];
}

const MoonDirection = new THREE.Vector3(-0.62, 0.12, -0.78).normalize();
const SeatScale     = 1.6;
const SeatSpots     = [-2.5, 2.5];                                 // [m] - driver, passenger x
const TiltLimits    = [5, 40];                                     // [°] - backrest lean range
const SlideLimits   = [-120, 120];                                 // [mm] - rail travel
const SlideReach    = 0.002;                                       // seat-profile units per mm of slide
const SeatMemories  = [{ Slide: 20, Recline: 14, Name: 'Memory 1' }, { Slide: -50, Recline: 24, Name: 'Memory 2' }, { Slide: -120, Recline: 9, Name: 'Easy entry' }];
const Warm = new THREE.Color('#ff3d55'), Mild = new THREE.Color('#e6dcff'), Cool = new THREE.Color('#3f6dff');

//------------------------------------------------------------------------------------------------------------------------
//                                                    DOCUMENT
//------------------------------------------------------------------------------------------------------------------------

const Display = document.getElementById('Display');
const Find    = (Id) => document.getElementById(Id);
const All     = (Selector) => [...Display.querySelectorAll(Selector)];
FitDisplay(Display, DisplayWidth, DisplayHeight);
BindRipples(Display);

let ToastTimer = 0;
function Toast(Text)
{
    const Element = Find('Toast');
    Element.textContent = Text;
    Element.classList.add('On');
    clearTimeout(ToastTimer);
    ToastTimer = setTimeout(() => Element.classList.remove('On'), 2200);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONDITION
//------------------------------------------------------------------------------------------------------------------------

const Condition = {
    Menu:      'Home',
    Mode:      1,
    Cruising:  false,
    Cruise:    0,                    // [km/h] - eased cruise preview
    Speed:     0,                    // [km/h] - shown: cruise or powertrain, whichever is faster
    Charging:  false,
    Battery:   64,                   // [%]
    Limit:     90,                   // [%]
    Added:     0,                    // [km]
    Temps:     [25, 19],             // [°C]
    SeatHeat:  2,                    // driver 0..3
    SeatVent:  1,                    // passenger 0..3
    Slide:     [20, -40],            // [mm] - fore (+) / aft (−) from the rail centre, driver and passenger
    Recline:   [14, 22],             // [°] - backrest lean from upright, driver and passenger
    Memory:    [0, -1],              // last recalled seat memory, -1 when moved by hand
    Air:       { Screen: false, Face: true, Feet: true },
    Fan:       3,                    // 1..7
    Auto:      true,
    Tabs:      { Ac: true, Max: false, Rear: false, Eco: false },
    Track:     0,
    TrackTime: 72,                   // [s]
    Playing:   false,
    Pulse:     true,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDERER AND SCENE
//------------------------------------------------------------------------------------------------------------------------

const Canvas     = Find('Scene');
const Renderer   = new THREE.WebGLRenderer({ canvas: Canvas, antialias: true });
const PixelRatio = Math.min(2, window.devicePixelRatio || 1);
Renderer.setPixelRatio(PixelRatio);
Renderer.setSize(DisplayWidth, DisplayHeight, false);
Renderer.toneMapping         = THREE.ACESFilmicToneMapping;
Renderer.toneMappingExposure = 1.05;
Renderer.outputColorSpace    = THREE.SRGBColorSpace;

const Scene = new THREE.Scene();
Scene.fog   = new THREE.Fog('#2c4774', 30, 170);
Scene.environment = PanelEnvironment(Renderer, [
    { Colour: '#9fb8ff', Intensity: 1.1, Position: [0, 9, 0],      Size: [14, 8] },
    { Colour: '#3b5a8c', Intensity: 2.2, Position: [-6, 1.5, -9],  Size: [18, 3] },
    { Colour: '#eef2ff', Intensity: 4.0, Position: [-5, 2.5, -7],  Size: [1.2, 1.2] },
    { Colour: '#5a6fa8', Intensity: 1.2, Position: [9, 2, 6],      Size: [8, 3] },
    { Colour: '#0a1020', Intensity: 1.0, Position: [0, -4, 0],     Size: [20, 20] },
], '#0b1222');
Scene.environmentIntensity = 1.35;

const Camera = new THREE.PerspectiveCamera(30, DisplayWidth / DisplayHeight, 0.1, 600);
const Orbit  = new CameraOrbit(Camera, Canvas, { ...CameraPresets.Home, RadiusLimits: [7, 26], PitchLimits: [0.0, 1.2] });

const Hemisphere = new THREE.HemisphereLight('#b4c4ff', '#141a2c', 0.9);
const MoonLight  = new THREE.DirectionalLight('#dfe7ff', 1.7);
MoonLight.position.copy(MoonDirection).multiplyScalar(20).setY(7);
const FrontFill  = new THREE.DirectionalLight('#a9bcf0', 1.5);
FrontFill.position.set(9, 4, 8);
const UnderLight = new THREE.PointLight('#6f9cff', 6, 7, 1.6);
UnderLight.position.set(0, 0.25, 0);
Scene.add(Hemisphere, MoonLight, FrontFill, UnderLight);

// Live look: eased copies of the active look.
const Live = {};
for (const Key of ColourKeys) Live[Key] = new THREE.Color(Looks.Home[Key]);
for (const Key of LevelKeys) Live[Key] = Looks.Home[Key];

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

function SetOrder(Root, Order) { Root.traverse((Each) => { Each.renderOrder = Order; }); }

//------------------------------------------------------------------------------------------------------------------------
//                                                    SKY, STARS, MOON, MOUNTAINS
//------------------------------------------------------------------------------------------------------------------------

const SkyUniforms = { Horizon: { value: Live.Horizon }, Zenith: { value: Live.Zenith }, Water: { value: Live.Water }, Glow: { value: Live.Glow }, Moon: { value: MoonDirection } };
const Sky = new THREE.Mesh(new THREE.SphereGeometry(400, 48, 24), new THREE.ShaderMaterial({
    uniforms: SkyUniforms, side: THREE.BackSide, depthWrite: false, fog: false, toneMapped: false,
    vertexShader: /* glsl */`
        varying vec3 Direction;
        void main() { Direction = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); gl_Position.z = gl_Position.w; }`,
    fragmentShader: /* glsl */`
        uniform vec3 Horizon, Zenith, Water, Glow, Moon; varying vec3 Direction;
        void main()
        {
            vec3 D = normalize(Direction);
            float H = D.y;
            vec3 C = H > 0.0 ? mix(Horizon, Zenith, pow(clamp(H, 0.0, 1.0), 0.42)) : mix(Horizon * 0.7, Water, pow(clamp(-H, 0.0, 1.0), 0.35));
            C += Horizon * exp(-abs(H) * 22.0) * 0.45;
            float M = max(dot(D, Moon), 0.0);
            C += Glow * (pow(M, 14.0) * 0.18 + pow(M, 120.0) * 0.35);
            gl_FragColor = vec4(C, 1.0);
            #include <colorspace_fragment>
        }`,
}));
Sky.renderOrder = -3;
Scene.add(Sky);

const StarCount = 1800;
const StarGeometry = new THREE.BufferGeometry();
{
    const Points = new Float32Array(StarCount * 3), Seeds = new Float32Array(StarCount);
    for (let Index = 0; Index < StarCount; Index++)
    {
        const Yaw = Math.random() * Math.PI * 2, Rise = Math.asin(0.03 + Math.pow(Math.random(), 0.8) * 0.97);
        Points.set([Math.cos(Rise) * Math.cos(Yaw) * 360, Math.sin(Rise) * 360, Math.cos(Rise) * Math.sin(Yaw) * 360], Index * 3);
        Seeds[Index] = Math.random();
    }
    StarGeometry.setAttribute('position', new THREE.BufferAttribute(Points, 3));
    StarGeometry.setAttribute('Seed', new THREE.BufferAttribute(Seeds, 1));
}
const StarUniforms = { Level: { value: 1 }, Clock: { value: 0 }, Ratio: { value: PixelRatio } };
const Stars = new THREE.Points(StarGeometry, new THREE.ShaderMaterial({
    uniforms: StarUniforms, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, fog: false, toneMapped: false,
    vertexShader: /* glsl */`
        uniform float Clock, Ratio; attribute float Seed; varying float Shine;
        void main()
        {
            Shine = (0.3 + 0.7 * Seed) * (0.55 + 0.45 * sin(Clock * (0.8 + Seed * 3.0) + Seed * 50.0));
            gl_Position  = projectionMatrix * modelViewMatrix * vec4(position, 1.0);
            gl_Position.z = gl_Position.w * 0.9999;
            gl_PointSize = (0.9 + Seed * Seed * 2.4) * Ratio;
        }`,
    fragmentShader: /* glsl */`
        uniform float Level; varying float Shine;
        void main() { float Fall = smoothstep(0.5, 0.0, length(gl_PointCoord - 0.5)); gl_FragColor = vec4(vec3(0.9, 0.94, 1.0), Fall * Shine * Level); }`,
}));
Stars.renderOrder = -2;
Scene.add(Stars);

// Moon: disc with a soft halo, low over the lake.
const MoonTexture = (() =>
{
    const Paint = document.createElement('canvas');
    Paint.width = Paint.height = 256;
    const Pen = Paint.getContext('2d');
    const Halo = Pen.createRadialGradient(128, 128, 10, 128, 128, 128);
    Halo.addColorStop(0, 'rgba(235,240,255,0.55)');
    Halo.addColorStop(0.18, 'rgba(200,215,255,0.22)');
    Halo.addColorStop(1, 'rgba(160,180,255,0)');
    Pen.fillStyle = Halo;
    Pen.fillRect(0, 0, 256, 256);
    const Disc = Pen.createRadialGradient(118, 118, 2, 128, 128, 22);
    Disc.addColorStop(0, '#ffffff');
    Disc.addColorStop(1, '#dfe6f6');
    Pen.fillStyle = Disc;
    Pen.beginPath(); Pen.arc(128, 128, 22, 0, Math.PI * 2); Pen.fill();
    Pen.fillStyle = 'rgba(150,160,190,0.25)';
    for (const [X, Y, R] of [[120, 122, 5], [134, 132, 4], [126, 138, 3], [136, 118, 2.5]]) { Pen.beginPath(); Pen.arc(X, Y, R, 0, Math.PI * 2); Pen.fill(); }
    const Texture = new THREE.CanvasTexture(Paint);
    Texture.colorSpace = THREE.SRGBColorSpace;
    return Texture;
})();
const Moon = new THREE.Sprite(new THREE.SpriteMaterial({ map: MoonTexture, transparent: true, depthWrite: false, fog: false, toneMapped: false }));
Moon.position.copy(MoonDirection).multiplyScalar(330);
Moon.scale.setScalar(64);
Moon.renderOrder = -1;
Scene.add(Moon);

// Mountains: two ridge rings, darker near, hazier far; their twins reflect in the lake.
function RidgeRing(Radius, Peak, Seed, Colour, Top)
{
    const Steps = 360, Points = [], Colours = [], Index = [];
    const Low = new THREE.Color(Colour), High = new THREE.Color(Top);
    for (let Step = 0; Step <= Steps; Step++)
    {
        const Angle  = (Step / Steps) * Math.PI * 2;
        const Wave   = Math.sin(Angle * 3 + Seed) * 0.45 + Math.sin(Angle * 7.3 + Seed * 2.1) * 0.25 + Math.sin(Angle * 17.1 + Seed * 0.7) * 0.12 + Math.sin(Angle * 41 + Seed) * 0.04;
        const Height = Math.max(0.6, Peak * (0.45 + Wave));
        const Reach  = Radius * (1 + Math.sin(Angle * 2.0 + Seed) * 0.08);
        const X = Math.cos(Angle) * Reach, Z = Math.sin(Angle) * Reach;
        Points.push(X, -0.5, Z, X, Height, Z);
        Colours.push(Low.r, Low.g, Low.b, High.r, High.g, High.b);
        if (Step < Steps) { const A = Step * 2; Index.push(A, A + 1, A + 2, A + 1, A + 3, A + 2); }
    }
    const Geometry = new THREE.BufferGeometry();
    Geometry.setAttribute('position', new THREE.Float32BufferAttribute(Points, 3));
    Geometry.setAttribute('color', new THREE.Float32BufferAttribute(Colours, 3));
    Geometry.setIndex(Index);
    return new THREE.Mesh(Geometry, new THREE.MeshBasicMaterial({ vertexColors: true, side: THREE.DoubleSide }));
}
const Mountains = new THREE.Group();
Mountains.add(RidgeRing(150, 15, 1.3, '#0d1626', '#1d2c48'), RidgeRing(95, 9, 4.1, '#04070d', '#0d1525'));
Scene.add(Mountains);
const MountainTwin = MirrorTwin(Mountains);
Scene.add(MountainTwin.Group);
MountainTwin.Sync();

//------------------------------------------------------------------------------------------------------------------------
//                                                    AURORA
//------------------------------------------------------------------------------------------------------------------------

const AuroraUniforms = { Clock: { value: 0 }, Strength: { value: 0 }, TintA: { value: Live.AuroraA }, TintB: { value: Live.AuroraB } };
const AuroraMaterial = new THREE.ShaderMaterial({
    uniforms: AuroraUniforms, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide, fog: false, toneMapped: false,
    vertexShader: /* glsl */`
        uniform float Clock; varying vec2 Spot;
        void main()
        {
            Spot = uv;
            vec3 P = position;
            P.x += sin(uv.x * 9.0 + Clock * 0.25) * 2.2 * uv.y;
            P.z += sin(uv.x * 5.0 - Clock * 0.18) * 3.0;
            gl_Position = projectionMatrix * modelViewMatrix * vec4(P, 1.0);
        }`,
    fragmentShader: /* glsl */`
        uniform float Clock, Strength; uniform vec3 TintA, TintB; varying vec2 Spot;
        void main()
        {
            float X = Spot.x * 7.0;
            float Folds = 0.5 + 0.5 * sin(X * 3.1 + Clock * 0.35 + sin(X * 1.7 - Clock * 0.27) * 2.2);
            Folds *= 0.55 + 0.45 * sin(X * 13.0 + Clock * 0.9 + sin(X * 4.0) * 1.5);
            float Rays = 0.7 + 0.3 * sin(X * 60.0 + Clock * 0.6);
            float V = Spot.y;
            float Shape = smoothstep(0.0, 0.07, V) * pow(1.0 - V, 1.7);
            float Ends  = smoothstep(0.0, 0.18, Spot.x) * smoothstep(1.0, 0.82, Spot.x);
            vec3 C = mix(TintA, TintB, smoothstep(0.1, 0.9, V));
            gl_FragColor = vec4(C * Folds * Rays * Shape * Ends * Strength * 1.25, 1.0);
            #include <colorspace_fragment>
        }`,
});
const Aurora = new THREE.Group();
for (const [Radius, Lift, Spread, Turn] of [[120, 10, 1.9, -2.3], [150, 16, 2.4, -1.75], [105, 7, 1.4, -0.8]])
{
    const Geometry = new THREE.PlaneGeometry(1, 1, 96, 1);
    const Position = Geometry.attributes.position;
    for (let Index = 0; Index < Position.count; Index++)
    {
        const U = Position.getX(Index) + 0.5, V = Position.getY(Index) + 0.5;
        const Angle = Turn + (U - 0.5) * Spread;
        Position.setXYZ(Index, Math.cos(Angle) * Radius, Lift + V * 34, Math.sin(Angle) * Radius);
    }
    Geometry.computeBoundingSphere();
    Aurora.add(new THREE.Mesh(Geometry, AuroraMaterial));
}
SetOrder(Aurora, -1);
Scene.add(Aurora);

//------------------------------------------------------------------------------------------------------------------------
//                                                    LAKE
//------------------------------------------------------------------------------------------------------------------------

const LakeUniforms = {
    Clock: { value: 0 }, Flow: { value: 0 }, Streak: { value: 0 },
    Deep: { value: Live.Water }, Horizon: { value: Live.Horizon }, Glow: { value: Live.Glow }, Moon: { value: MoonDirection },
    RingRadius: { value: new THREE.Vector3(99, 99, 99) }, RingLevel: { value: new THREE.Vector3(0, 0, 0) },
};
const Lake = new THREE.Mesh(new THREE.CircleGeometry(260, 96).rotateX(-Math.PI / 2), new THREE.ShaderMaterial({
    uniforms: LakeUniforms, transparent: true, depthWrite: false, fog: false, toneMapped: false,
    vertexShader: /* glsl */`
        varying vec3 WorldPoint;
        void main() { WorldPoint = (modelMatrix * vec4(position, 1.0)).xyz; gl_Position = projectionMatrix * viewMatrix * vec4(WorldPoint, 1.0); }`,
    fragmentShader: /* glsl */`
        uniform float Clock, Flow, Streak; uniform vec3 Deep, Horizon, Glow, Moon, RingRadius, RingLevel;
        varying vec3 WorldPoint;
        void main()
        {
            vec3 W = WorldPoint;
            float D = length(W.xz);
            vec2 Out = W.xz / max(D, 0.001);
            float A = sin(W.x * 1.3 + W.z * 0.5 + Clock * 0.7 + Flow * 0.6);
            float B = sin(W.z * 2.1 - W.x * 0.8 - Clock * 0.9);
            float C = sin(W.x * 4.7 + W.z * 3.1 + Clock * 1.7 + Flow * 1.5);
            vec3 Ring = vec3(0.0);
            float Band = 0.0;
            for (int Index = 0; Index < 3; Index++)
            {
                float Gap = D - RingRadius[Index];
                float Ripple = sin(Gap * 9.0) * exp(-Gap * Gap * 1.8) * RingLevel[Index];
                Ring.xz += Out * Ripple * 0.09;
                Band += exp(-Gap * Gap * 14.0) * RingLevel[Index];
            }
            vec3 N = normalize(vec3(A * 0.018 + C * 0.008 + Ring.x, 1.0, B * 0.018 + C * 0.006 + Ring.z));
            vec3 V = normalize(cameraPosition - W);
            vec3 R = reflect(-V, N);
            float Fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
            float M = max(dot(R, Moon), 0.0);
            float Glint = pow(M, 700.0) * 2.4 + pow(M, 70.0) * 0.22;
            float Line  = smoothstep(0.93, 1.0, 0.5 + 0.5 * sin(W.z * 7.0 + sin(W.x * 0.35) * 1.2));
            float Run   = smoothstep(0.55, 1.0, 0.5 + 0.5 * sin(W.x * 0.55 + Flow * 1.8 + W.z * 0.4));
            float Streaks = Line * Run * Streak * smoothstep(16.0, 2.0, abs(W.z));
            vec3 Colour = mix(Deep, Horizon * 0.9, Fresnel * 0.85) + Glow * (Glint + Streaks * 0.5 + Band * 0.22);
            float Far = smoothstep(30.0, 150.0, D);
            Colour = mix(Colour, Horizon * 0.8, Far);
            float Alpha = mix(0.58, 0.94, Fresnel);
            Alpha = mix(Alpha, 1.0, Far);
            gl_FragColor = vec4(Colour, Alpha);
            #include <colorspace_fragment>
        }`,
}));
Lake.renderOrder = 0;
Scene.add(Lake);

// Glow pool under the car (tinted by the active look).
const UnderPool = new THREE.Mesh(new THREE.PlaneGeometry(9, 5).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ map: SoftGlow, color: '#6f9cff', transparent: true, opacity: 0.35, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
UnderPool.position.y = 0.015;
UnderPool.renderOrder = 1;
Scene.add(UnderPool);
const ContactShade = new THREE.Mesh(new THREE.PlaneGeometry(7.2, 3.4).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ map: GlowTexture('rgba(0,0,0,0.9)', 'rgba(0,0,0,0)'), transparent: true, depthWrite: false }));
ContactShade.position.y = 0.012;
ContactShade.renderOrder = 1;
Scene.add(ContactShade);

//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE
//------------------------------------------------------------------------------------------------------------------------

const Finish = DefaultFinish({
    [Family.BodyCoat]:  { color: '#c3ccd8', roughness: 0.34, metalness: 0.32, clearcoat: 1, clearcoatRoughness: 0.08 },
    [Family.Trim]:      { color: '#262b33', roughness: 0.4, metalness: 0.5 },
    [Family.Hub]:       { color: '#c3c9d2', roughness: 0.2, metalness: 1 },
    [Family.Brake]:     { color: '#39404d', roughness: 0.4, metalness: 0.7 },
    [Family.Headlight]: { color: '#aab4c4', emissive: '#c8d4ea', emissiveIntensity: 0.55 },
    [Family.Taillight]: { emissive: '#ff1f3a', emissiveIntensity: 2.2 },
});
const FadeMaterials = Object.values(Finish);
let Vehicle = null, VehicleTwin = null;

//------------------------------------------------------------------------------------------------------------------------
//                                                    DRIVE: POWER CURRENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Drive turns the body to glass and shows the 2.0 turbo hybrid at work: tank, engine, P2 e-motor, battery and the
//    rear axle, joined by four currents (fuel, engine, battery, drive). Pulses run the way the energy flows and turn
//    round under regen. Everything draws as x-ray light (no depth test) so it reads through the ghosted body.
const CurrentVertex = /* glsl */`
    varying vec2 Along;
    void main() { Along = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`;
const CurrentFragment = /* glsl */`
    uniform vec3 Tone; uniform float Level, Phase, Count, Show;
    varying vec2 Along;
    void main()
    {
        float Pulse = pow(fract(Along.x * Count - Phase), 7.0);
        float Fade  = smoothstep(0.0, 0.06, Along.x) * smoothstep(1.0, 0.94, Along.x);
        float Alpha = Show * Fade * (0.10 + Level * (0.22 + Pulse * 1.1));
        gl_FragColor = vec4(Tone * Alpha, 1.0);
        #include <colorspace_fragment>
    }`;
const Currents = {
    Fuel:    { Tone: { value: new THREE.Color('#ffd36b') }, Level: { value: 0 }, Phase: { value: 0 }, Goal: 0, Shown: 0 },
    Engine:  { Tone: { value: new THREE.Color('#ff9a4a') }, Level: { value: 0 }, Phase: { value: 0 }, Goal: 0, Shown: 0 },
    Battery: { Tone: { value: new THREE.Color('#5fe3ff') }, Level: { value: 0 }, Phase: { value: 0 }, Goal: 0, Shown: 0 },
    Drive:   { Tone: { value: new THREE.Color('#ffb48a') }, Level: { value: 0 }, Phase: { value: 0 }, Goal: 0, Shown: 0 },
};
const CurrentShow = { value: 0 };
const Regen = new THREE.Color('#67f5b1');
const PowerCurrent = new THREE.Group();
PowerCurrent.visible = false;
Scene.add(PowerCurrent);
const CurrentParts = {};                                              // Engine, Motor, Cell, Tank: { Group, Fill, Edge, Anchor, Level }

function XRay(Material) { Object.assign(Material, { transparent: true, depthTest: false, depthWrite: false, blending: THREE.AdditiveBlending, toneMapped: false }); return Material; }
function Holo(Geometry, Colour)
{
    const Group = new THREE.Group();
    const Fill  = new THREE.Mesh(Geometry, XRay(new THREE.MeshBasicMaterial({ color: Colour, opacity: 0.1 })));
    const Edge  = new THREE.LineSegments(new THREE.EdgesGeometry(Geometry, 20), XRay(new THREE.LineBasicMaterial({ color: Colour, opacity: 0.6 })));
    Group.add(Fill, Edge);
    return { Group, Fill, Edge, Level: 0 };
}
function Channel(Name, Points, Radius = 0.04)
{
    const Path = new THREE.CatmullRomCurve3(Points.map((Each) => new THREE.Vector3(...Each)), false, 'centripetal');
    const Current = Currents[Name];
    const Uniforms = { Tone: Current.Tone, Level: Current.Level, Phase: Current.Phase, Show: CurrentShow, Count: { value: Math.max(2, Path.getLength() * 2.4) } };
    const Tube = new THREE.Mesh(new THREE.TubeGeometry(Path, 64, Radius, 8, false), XRay(new THREE.ShaderMaterial({ uniforms: Uniforms, vertexShader: CurrentVertex, fragmentShader: CurrentFragment })));
    PowerCurrent.add(Tube);
    return Tube;
}

// Builds the parts and currents once the car is in (the axle height and track come from the wheel mounts).
function BuildCurrent(Car)
{
    const Rear = Car.Wheels.filter((Wheel) => !Wheel.Front);
    const Axle = Rear[0].Mount.y, AxleX = Rear[0].Mount.x, Track = Math.abs(Rear[0].Mount.z);

    const Engine = Holo(new THREE.BoxGeometry(1.0, 0.46, 0.78), '#ff9a4a');
    Engine.Group.position.set(1.95, Axle + 0.28, 0);
    Engine.Pistons = [-0.3, -0.1, 0.1, 0.3].map((X) =>
    {
        const Piston = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.07, 0.16, 14), XRay(new THREE.MeshBasicMaterial({ color: '#ffc08a', opacity: 0.5 })));
        Piston.position.set(X, 0.3, 0);
        Engine.Group.add(Piston);
        return Piston;
    });
    const Motor = Holo(new THREE.CylinderGeometry(0.24, 0.24, 0.5, 28).rotateZ(Math.PI / 2), '#c9b6ff');
    Motor.Group.position.set(1.0, Axle + 0.08, 0);
    Motor.Rotor = new THREE.Mesh(new THREE.TorusGeometry(0.17, 0.018, 8, 40, Math.PI * 1.4).rotateY(Math.PI / 2), XRay(new THREE.MeshBasicMaterial({ color: '#e8deff', opacity: 0.8 })));
    Motor.Group.add(Motor.Rotor);
    const Cell = Holo(new THREE.BoxGeometry(1.7, 0.14, 1.12), '#5fe3ff');
    Cell.Group.position.set(-0.2, 0.36, 0);
    Cell.Blocks = Array.from({ length: 10 }, (_, Index) =>
    {
        const Block = new THREE.Mesh(new THREE.BoxGeometry(0.13, 0.09, 0.98), XRay(new THREE.MeshBasicMaterial({ color: '#5fe3ff', opacity: 0.1 })));
        Block.position.set(-0.76 + Index * 0.169, 0, 0);
        Cell.Group.add(Block);
        return Block;
    });
    const Tank = Holo(new THREE.BoxGeometry(0.62, 0.36, 1.1), '#ffd36b');
    Tank.Group.position.set(-2.45, Axle + 0.06, 0);
    Tank.Gauge = new THREE.Mesh(new THREE.BoxGeometry(0.56, 0.32, 1.04).translate(0, 0.16, 0), XRay(new THREE.MeshBasicMaterial({ color: '#ffd36b', opacity: 0.16 })));
    Tank.Gauge.position.y = -0.16;
    Tank.Group.add(Tank.Gauge);
    const Diff = Holo(new THREE.SphereGeometry(0.16, 18, 12), '#ffffff');
    Diff.Group.position.set(AxleX, Axle, 0);
    const Rings = [1, -1].map((Side) =>
    {
        const Ring = new THREE.Mesh(new THREE.TorusGeometry(0.54, 0.018, 8, 64), XRay(new THREE.MeshBasicMaterial({ color: '#ffb48a', opacity: 0 })));
        Ring.position.set(AxleX, Axle, Side * (Track + 0.16));
        PowerCurrent.add(Ring);
        return Ring;
    });
    for (const Part of [Engine, Motor, Cell, Tank, Diff]) PowerCurrent.add(Part.Group);
    Object.assign(CurrentParts, { Engine, Motor, Cell, Tank, Diff, Rings });
    Engine.Anchor = new THREE.Vector3(1.95, Axle + 0.62, 0);
    Motor.Anchor  = new THREE.Vector3(1.0, Axle + 0.36, 0.0);
    Cell.Anchor   = new THREE.Vector3(-0.2, 0.46, 0.5);
    Tank.Anchor   = new THREE.Vector3(-2.45, Axle + 0.3, 0);

    // Currents: tank → engine (fuel line), engine → e-motor (crank), battery ↔ e-motor, e-motor → axle → wheels.
    Channel('Fuel',    [[-2.14, Axle - 0.02, 0.4], [-1.4, 0.26, 0.62], [0.4, 0.26, 0.66], [1.3, Axle + 0.05, 0.5], [1.6, Axle + 0.22, 0.3]], 0.03);
    Channel('Engine',  [[1.46, Axle + 0.24, 0], [1.34, Axle + 0.16, 0], [1.25, Axle + 0.08, 0]], 0.06);
    Channel('Battery', [[0.5, 0.4, -0.38], [0.78, 0.5, -0.4], [0.98, Axle - 0.02, -0.26], [1.0, Axle + 0.04, -0.12]], 0.045);
    Channel('Drive',   [[0.74, Axle + 0.06, 0], [-0.4, Axle - 0.02, 0], [AxleX + 0.17, Axle, 0]], 0.05);
    for (const Side of [1, -1]) Channel('Drive', [[AxleX, Axle, Side * 0.14], [AxleX, Axle, Side * (Track * 0.6)], [AxleX, Axle, Side * (Track - 0.08)]], 0.04);
    SetOrder(PowerCurrent, 6);
}

// 📝 Which way the energy goes for the active mode, speed and pedals. Levels are 0..1; Battery and Drive are signed
//    (negative = into the battery: regen or engine charging). Power figures are display-scale, not a vehicle model.
function FlowPlan()
{
    const Mode   = DriveModes[Condition.Mode];
    const Speed  = Condition.Speed;
    const Demand = Math.max(Motion.Throttle || 0, Condition.Cruising ? 0.32 : 0);
    const Wheels = 6 + Speed * 0.5 + Demand * 70;
    if ((Motion.Brake || 0) > 0.08 && Speed > 3)
        return { Fuel: 0, Engine: 0, Battery: -1, Drive: -1, State: 'Regenerating · braking energy back into the battery', Kw: { Engine: 0, Battery: -(10 + Speed * 0.35) * (Motion.Brake + 0.3), Drive: -(10 + Speed * 0.35) } };
    if (Speed < 1 && Demand < 0.05)
        return { Fuel: 0, Engine: 0, Battery: 0, Drive: 0, State: 'Ready · engine asleep', Kw: { Engine: 0, Battery: 0, Drive: 0 } };
    if (Speed < Mode.Electric && Condition.Battery > 12)
        return { Fuel: 0, Engine: 0, Battery: 1, Drive: 1, State: 'Electric · engine asleep', Kw: { Engine: 0, Battery: Wheels, Drive: Wheels } };
    if (Mode.Key === 'Sport')
    {
        const Boost = Condition.Battery > 8 ? 18 + Demand * 60 : 0;
        return { Fuel: 1, Engine: 1, Battery: Boost ? 0.8 : 0, Drive: 1, State: Boost ? 'Engine plus e-boost' : 'Engine · battery too low to boost', Kw: { Engine: Wheels, Battery: Boost, Drive: Wheels + Boost } };
    }
    const Top = Condition.Battery < 80 ? 9 + Speed * 0.04 : 0;
    return { Fuel: 1, Engine: 1, Battery: Top ? -0.4 : 0, Drive: 1, State: Top ? 'Engine drive · topping up the battery' : 'Engine drive · battery full', Kw: { Engine: Wheels + Top, Battery: -Top, Drive: Wheels } };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHARGE: RIG AND FLOOR DASHES
//------------------------------------------------------------------------------------------------------------------------

const PortLocal = new THREE.Vector3(-2.25, 0.62, 1.24);
const ChargeRig = new THREE.Group();
Scene.add(ChargeRig);
const Bollard = new THREE.Mesh(new THREE.BoxGeometry(0.26, 1.15, 0.26), new THREE.MeshStandardMaterial({ color: '#1a1f27', roughness: 0.35, metalness: 0.7 }));
Bollard.position.set(-3.55, 0.575, 2.55);
const BollardStrip = new THREE.Mesh(new THREE.BoxGeometry(0.03, 0.8, 0.02), new THREE.MeshBasicMaterial({ color: '#5dffa8', toneMapped: false }));
BollardStrip.position.set(-3.55, 0.62, 2.69);
const BollardCap = new THREE.Mesh(new THREE.BoxGeometry(0.3, 0.04, 0.3), new THREE.MeshBasicMaterial({ color: '#5dffa8', toneMapped: false }));
BollardCap.position.set(-3.55, 1.17, 2.55);
ChargeRig.add(Bollard, BollardStrip, BollardCap);
const CableUniforms = { Clock: { value: 0 }, Run: { value: 0 }, Tone: { value: new THREE.Color('#5dffa8') } };
const CablePath = new THREE.CatmullRomCurve3([
    new THREE.Vector3(-3.55, 0.95, 2.42), new THREE.Vector3(-3.35, 0.35, 2.2), new THREE.Vector3(-2.9, 0.28, 1.75),
    new THREE.Vector3(-2.45, 0.7, 1.42), new THREE.Vector3(-2.25, 1.03, 1.27),
]);
const Cable = new THREE.Mesh(new THREE.TubeGeometry(CablePath, 64, 0.035, 10, false), new THREE.ShaderMaterial({
    uniforms: CableUniforms, toneMapped: false,
    vertexShader: /* glsl */`varying vec2 Spot; void main() { Spot = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
    fragmentShader: /* glsl */`
        uniform float Clock, Run; uniform vec3 Tone; varying vec2 Spot;
        void main()
        {
            float Dash = smoothstep(0.65, 1.0, 0.5 + 0.5 * sin(Spot.x * 60.0 - Clock * 9.0));
            vec3 C = vec3(0.03, 0.035, 0.045) + Tone * (0.12 + Dash * Run * 1.4);
            gl_FragColor = vec4(C, 1.0);
            #include <colorspace_fragment>
        }`,
}));
ChargeRig.add(Cable);
const PortGlow = new THREE.Sprite(new THREE.SpriteMaterial({ map: SoftGlow, color: '#5dffa8', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
PortGlow.position.set(-2.25, 1.04, 1.3);
PortGlow.scale.setScalar(0.7);
ChargeRig.add(PortGlow);
ChargeRig.visible = false;
const ChargeTwin = MirrorTwin(ChargeRig);
Scene.add(ChargeTwin.Group);

const DashLanes = [1.75, 2.2, 2.8, -1.75, -2.2, -2.8], DashStep = 0.62, DashSpan = 7.4;
const DashPerLane = Math.floor((DashSpan * 2) / DashStep) + 1;
const Dashes = new THREE.InstancedMesh(new THREE.PlaneGeometry(0.4, 0.06).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#ffffff', transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }), DashLanes.length * DashPerLane);
{
    const Place = new THREE.Matrix4();
    let Index = 0;
    for (const Lane of DashLanes) for (let Step = 0; Step < DashPerLane; Step++)
    {
        Place.makeTranslation(-DashSpan + Step * DashStep, 0.02, Lane);
        Dashes.setMatrixAt(Index, Place);
        Dashes.setColorAt(Index, new THREE.Color(0, 0, 0));
        Index++;
    }
}
Dashes.renderOrder = 1;
Dashes.visible = false;
Scene.add(Dashes);
const DashTone = new THREE.Color();

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMFORT: 3D SEATS
//------------------------------------------------------------------------------------------------------------------------

// 📝 One sculpted seat in two parts sharing a side profile (facing +X): a cushion that slides along the rail and a
//    backrest that tilts about the hip. Both are extruded across the seat width with a soft bevel.
const SeatHip     = new THREE.Vector2(-0.42, 0.27);                  // backrest pivot in the profile
const SeatUpright = 13;                                              // [°] - lean of the authored backrest
function SeatExtrude(Shape)
{
    const Geometry = new THREE.ExtrudeGeometry(Shape, { depth: 0.78, bevelEnabled: true, bevelThickness: 0.07, bevelSize: 0.05, bevelSegments: 5, curveSegments: 18 });
    Geometry.translate(0, 0, -0.39);
    Geometry.computeVertexNormals();
    return Geometry;
}
function CushionSurface()
{
    const Shape = new THREE.Shape();
    Shape.moveTo(0.5, 0.0);
    Shape.quadraticCurveTo(0.62, 0.04, 0.6, 0.17);
    Shape.quadraticCurveTo(0.56, 0.26, 0.38, 0.26);
    Shape.lineTo(-0.3, 0.24);
    Shape.quadraticCurveTo(-0.5, 0.23, -0.52, 0.12);
    Shape.quadraticCurveTo(-0.5, 0.0, -0.3, 0.0);
    Shape.lineTo(0.5, 0.0);
    return SeatExtrude(Shape);
}
function BackSurface()
{
    const Shape = new THREE.Shape();
    Shape.moveTo(-0.27, 0.3);
    Shape.lineTo(-0.4, 0.92);
    Shape.quadraticCurveTo(-0.42, 0.98, -0.4, 1.02);
    Shape.quadraticCurveTo(-0.38, 1.1, -0.43, 1.2);
    Shape.quadraticCurveTo(-0.48, 1.33, -0.58, 1.32);
    Shape.quadraticCurveTo(-0.68, 1.3, -0.67, 1.18);
    Shape.quadraticCurveTo(-0.66, 1.06, -0.62, 0.98);
    Shape.lineTo(-0.53, 0.3);
    Shape.quadraticCurveTo(-0.4, 0.2, -0.27, 0.3);
    const Geometry = SeatExtrude(Shape);
    Geometry.translate(-SeatHip.x, -SeatHip.y, 0);
    return Geometry;
}
const CushionGeometry = CushionSurface(), BackGeometry = BackSurface();
const RimVertex = /* glsl */`
    varying vec3 Facing, Toward;
    void main()
    {
        vec4 Point = modelViewMatrix * vec4(position, 1.0);
        Facing = normalize(normalMatrix * normal);
        Toward = normalize(-Point.xyz);
        gl_Position = projectionMatrix * Point;
    }`;
const RimFragment = /* glsl */`
    uniform vec3 Tone; uniform float Strength; varying vec3 Facing, Toward;
    void main()
    {
        float Rim = pow(1.0 - abs(dot(normalize(Facing), normalize(Toward))), 2.0);
        gl_FragColor = vec4(Tone * Rim * Strength, 1.0);
        #include <colorspace_fragment>
    }`;

// Ring angle (radians, counter-clockwise from +X) of a backrest lean: the dot sits where the backrest points.
function DotAngle(Index, Tilt) { return THREE.MathUtils.degToRad(Index === 0 ? 90 - Tilt : 90 + Tilt); }
const DialRadius = 1.72;
function ArcGeometry(Start, Length, Inner = DialRadius - 0.03, Outer = DialRadius + 0.03) { return new THREE.RingGeometry(Inner, Outer, Math.max(4, Math.ceil(Length * 40)), 1, Start, Math.max(0.0001, Length)); }

const Seats = SeatSpots.map((Spot, Index) =>
{
    const Group = new THREE.Group();
    Group.position.set(Spot, -3, 0);
    Group.scale.set(Index === 0 ? -SeatScale : SeatScale, SeatScale, SeatScale);   // driver faces left, passenger right
    const Leather = new THREE.MeshPhysicalMaterial({ color: '#eef1f6', roughness: 0.5, metalness: 0.0, sheen: 1, sheenColor: '#ffffff', sheenRoughness: 0.5, clearcoat: 0.2, emissive: '#000000', transparent: true, opacity: 0 });
    const RimUniforms = { Tone: { value: new THREE.Color('#ffffff') }, Strength: { value: 0 } };
    const RimMaterial = new THREE.ShaderMaterial({ uniforms: RimUniforms, vertexShader: RimVertex, fragmentShader: RimFragment, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, toneMapped: false });
    const Part = (Geometry) =>
    {
        const Body = new THREE.Mesh(Geometry, Leather);
        const Rim  = new THREE.Mesh(Geometry, RimMaterial);
        Rim.scale.setScalar(1.015);
        Rim.renderOrder = 3;
        return [Body, Rim];
    };
    // Sled: cushion plus the tilting backrest; it rides fore and aft on the rail.
    const Sled = new THREE.Group();
    const [Cushion, CushionRim] = Part(CushionGeometry);
    const Back = new THREE.Group();
    Back.position.set(SeatHip.x, SeatHip.y, 0);
    const [BackBody, BackRim] = Part(BackGeometry);
    Back.add(BackBody, BackRim);
    Sled.add(Cushion, CushionRim, Back);
    const RailMaterial = new THREE.MeshStandardMaterial({ color: '#1b2030', roughness: 0.3, metalness: 0.8, transparent: true, opacity: 0 });
    const Rail = new THREE.Mesh(new THREE.BoxGeometry(1.56, 0.035, 0.5), RailMaterial);
    Rail.position.set(0.05, -0.05, 0);
    const Stops = [-1, 1].map((Side) => { const Stop = new THREE.Mesh(new THREE.BoxGeometry(0.035, 0.08, 0.56), RailMaterial); Stop.position.set(0.05 + Side * 0.78, -0.03, 0); return Stop; });
    const Marker = new THREE.Mesh(new THREE.BoxGeometry(0.03, 0.012, 0.54), new THREE.MeshBasicMaterial({ color: '#ffffff', transparent: true, opacity: 0, toneMapped: false }));
    Marker.position.set(0.05, -0.028, 0);
    Group.add(Sled, Rail, Marker, ...Stops);

    // Heat or chill particles drifting through the seat.
    const Count = 40, Seeds = [];
    const Geometry = new THREE.BufferGeometry().setAttribute('position', new THREE.BufferAttribute(new Float32Array(Count * 3), 3));
    for (let Each = 0; Each < Count; Each++) Seeds.push([Math.random(), -0.55 + Math.random() * 1.0, -0.42 + Math.random() * 0.84, 0.6 + Math.random() * 0.8]);
    const Motes = new THREE.Points(Geometry, new THREE.PointsMaterial({ size: 0.075, map: SoftGlow, color: '#ff3d55', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
    Motes.renderOrder = 4;
    Sled.add(Motes);
    Scene.add(Group);

    // Dial rings behind the seat (unflipped, facing the camera). The bright dot is the back-angle handle: it sits where
    // the backrest points, on a lit arc from upright, inside the track of the allowed range.
    const Dial = new THREE.Group();
    Dial.position.set(Spot, -3, -0.75);
    const Plain = (Opacity, Colour = '#dfe6ff') => new THREE.MeshBasicMaterial({ color: Colour, transparent: true, opacity: Opacity, depthWrite: false, toneMapped: false, side: THREE.DoubleSide });
    const Line = (Radius, Opacity) => new THREE.Mesh(new THREE.RingGeometry(Radius - 0.006, Radius + 0.006, 200), Plain(Opacity));
    const Outer = Line(DialRadius, 0.32), Inner = Line(1.18, 0.12);
    const Ticks = [];
    for (let Tick = 0; Tick < 4; Tick++)
    {
        const Dot = new THREE.Mesh(new THREE.CircleGeometry(0.035, 16), Plain(0.6, '#ffffff'));
        Dot.position.set(Math.cos(Tick * Math.PI / 2) * DialRadius, Math.sin(Tick * Math.PI / 2) * DialRadius, 0.001);
        Ticks.push(Dot);
    }
    const Low = DotAngle(Index, TiltLimits[0]), High = DotAngle(Index, TiltLimits[1]);
    const RangeArc = new THREE.Mesh(ArcGeometry(Math.min(Low, High), Math.abs(High - Low), DialRadius - 0.04, DialRadius + 0.04), Plain(0.16));
    RangeArc.position.z = 0.001;
    for (let Tilt = TiltLimits[0]; Tilt <= TiltLimits[1]; Tilt += 5)
    {
        const Angle = DotAngle(Index, Tilt);
        const Mark  = new THREE.Mesh(new THREE.PlaneGeometry(0.11, 0.012), Plain(0.5));
        Mark.position.set(Math.cos(Angle) * (DialRadius + 0.13), Math.sin(Angle) * (DialRadius + 0.13), 0.001);
        Mark.rotation.z = Angle;
        Ticks.push(Mark);
    }
    const LitArc = new THREE.Mesh(ArcGeometry(Math.PI / 2, 0.1), Plain(0.9, '#ffffff'));
    LitArc.position.z = 0.002;
    const Rider = new THREE.Group();
    const RiderCore = new THREE.Mesh(new THREE.CircleGeometry(0.085, 28), new THREE.MeshBasicMaterial({ color: '#ffffff', toneMapped: false, transparent: true }));
    const RiderRing = new THREE.Mesh(new THREE.RingGeometry(0.13, 0.145, 40), Plain(0));
    const RiderHalo = new THREE.Sprite(new THREE.SpriteMaterial({ map: SoftGlow, color: '#ffffff', transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
    RiderHalo.scale.setScalar(0.6);
    const Grip = new THREE.Mesh(new THREE.CircleGeometry(0.34, 20), new THREE.MeshBasicMaterial({ transparent: true, opacity: 0, depthWrite: false }));
    Rider.add(RiderCore, RiderRing, RiderHalo, Grip);
    Rider.position.z = 0.004;
    const Glow = new THREE.Sprite(new THREE.SpriteMaterial({ map: SoftGlow, color: '#ff3d55', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
    Glow.scale.set(4.2, 4.2, 1);
    Glow.position.z = -0.2;
    Dial.add(Glow, Outer, Inner, RangeArc, LitArc, Rider, ...Ticks);
    SetOrder(Dial, 2);
    Scene.add(Dial);

    const Pool = new THREE.Mesh(new THREE.PlaneGeometry(4.4, 2.6).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ map: SoftGlow, color: '#ff3d55', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
    Pool.position.set(Spot, 0.02, 0);
    Pool.renderOrder = 1;
    Scene.add(Pool);

    return { Group, Sled, Back, Parts: [Cushion, BackBody], Leather, RimUniforms, RailMaterial, Marker, Motes, Seeds, Dial, Outer, Inner, RangeArc, LitArc, Ticks, Rider, RiderCore, RiderRing, RiderHalo, Grip, Glow, Pool,
             Tone: new THREE.Color('#ffffff'), ShownSlide: Condition.Slide[Index], ShownTilt: Condition.Recline[Index], ArcTilt: -1, Held: 0 };
});
const SeatTwins = Seats.map((Seat) => { const Twin = MirrorTwin(Seat.Group); Scene.add(Twin.Group); SetOrder(Twin.Group, -1); return Twin; });

function TempTone(Temp, Into)
{
    // 📝 19 °C and below read fully cool, 25 °C and above fully warm; neutral lilac sits near 21.5 °C.
    if (Temp <= 19) return Into.copy(Cool);
    if (Temp < 21.5) return Into.copy(Cool).lerp(Mild, (Temp - 19) / 2.5);
    if (Temp < 25) return Into.copy(Mild).lerp(Warm, (Temp - 21.5) / 3.5);
    return Into.copy(Warm);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MEDIA: 3D COVER FLOW
//------------------------------------------------------------------------------------------------------------------------

function PaintCover(Track)
{
    const Size = 512;
    const Paint = document.createElement('canvas');
    Paint.width = Paint.height = Size;
    const Pen = Paint.getContext('2d');
    const [HueA, HueB] = Track.Hues;
    const Back = Pen.createLinearGradient(0, 0, Size, Size);
    Back.addColorStop(0, `hsl(${HueA} 70% 22%)`);
    Back.addColorStop(1, `hsl(${HueB} 75% 46%)`);
    Pen.fillStyle = Back;
    Pen.fillRect(0, 0, Size, Size);
    Pen.save();
    if (Track.Art === 'Moon')
    {
        Pen.fillStyle = `hsl(${HueB} 60% 82%)`; Pen.beginPath(); Pen.arc(330, 190, 90, 0, Math.PI * 2); Pen.fill();
        Pen.fillStyle = `hsl(${HueA} 70% 22%)`; Pen.beginPath(); Pen.arc(370, 160, 84, 0, Math.PI * 2); Pen.fill();
        for (let Row = 0; Row < 9; Row++) { Pen.fillStyle = `hsla(${HueB} 80% 80% / ${0.5 - Row * 0.05})`; Pen.fillRect(120 + Row * 9, 330 + Row * 14, 300 - Row * 18, 4); }
    }
    else if (Track.Art === 'Waves')
    {
        Pen.lineWidth = 6;
        for (let Row = 0; Row < 12; Row++)
        {
            Pen.strokeStyle = `hsla(${HueB + Row * 4} 80% ${60 + Row * 2}% / 0.75)`;
            Pen.beginPath();
            for (let X = 0; X <= Size; X += 8) Pen.lineTo(X, 120 + Row * 26 + Math.sin(X * 0.02 + Row * 0.6) * 18);
            Pen.stroke();
        }
    }
    else if (Track.Art === 'Aurora')
    {
        for (let Band = 0; Band < 40; Band++)
        {
            const X = Band * 14 + Math.sin(Band) * 10;
            const Fall = Pen.createLinearGradient(0, 80, 0, 420);
            Fall.addColorStop(0, 'rgba(0,0,0,0)');
            Fall.addColorStop(0.6, `hsla(${HueA + Band * 1.5} 90% 65% / ${0.25 + 0.3 * Math.abs(Math.sin(Band * 0.7))})`);
            Fall.addColorStop(1, 'rgba(0,0,0,0)');
            Pen.fillStyle = Fall;
            Pen.fillRect(X, 80 + Math.sin(Band * 0.4) * 40, 9, 340);
        }
    }
    else if (Track.Art === 'Rings')
    {
        Pen.lineWidth = 3;
        for (let Ring = 0; Ring < 9; Ring++) { Pen.strokeStyle = `hsla(${HueB} 70% 85% / ${0.85 - Ring * 0.08})`; Pen.beginPath(); Pen.ellipse(256, 230, 40 + Ring * 22, 14 + Ring * 8, -0.3, 0, Math.PI * 2); Pen.stroke(); }
        Pen.fillStyle = '#fff'; Pen.beginPath(); Pen.arc(256, 230, 34, 0, Math.PI * 2); Pen.fill();
    }
    else if (Track.Art === 'Sun')
    {
        Pen.fillStyle = `hsl(${HueA} 95% 70%)`; Pen.beginPath(); Pen.arc(256, 300, 150, Math.PI, 0); Pen.fill();
        Pen.fillStyle = `hsl(${HueB} 70% 30%)`;
        for (let Bar = 0; Bar < 6; Bar++) Pen.fillRect(80, 200 + Bar * 18, 352, 3 + Bar * 1.6);
    }
    else if (Track.Art === 'Stripes')
    {
        Pen.rotate(-0.5);
        for (let Bar = 0; Bar < 14; Bar++) { Pen.fillStyle = `hsla(${HueB + Bar * 3} 90% ${50 + (Bar % 3) * 10}% / 0.8)`; Pen.fillRect(-200 + Bar * 52, -100, 22, 900); }
    }
    else
    {
        Pen.strokeStyle = `hsla(${HueB} 80% 80% / 0.35)`; Pen.lineWidth = 1.5;
        for (let Line = 0; Line < 16; Line++) { Pen.beginPath(); Pen.moveTo(Line * 34, 0); Pen.lineTo(Line * 34, Size); Pen.moveTo(0, Line * 34); Pen.lineTo(Size, Line * 34); Pen.stroke(); }
        Pen.strokeStyle = '#ffffff'; Pen.lineWidth = 5; Pen.beginPath(); Pen.moveTo(60, 380);
        for (const [X, Y] of [[150, 300], [210, 330], [300, 190], [380, 220], [450, 110]]) Pen.lineTo(X, Y);
        Pen.stroke();
    }
    Pen.restore();
    const Shade = Pen.createLinearGradient(0, Size * 0.55, 0, Size);
    Shade.addColorStop(0, 'rgba(0,0,0,0)');
    Shade.addColorStop(1, 'rgba(0,0,0,0.55)');
    Pen.fillStyle = Shade;
    Pen.fillRect(0, 0, Size, Size);
    Pen.fillStyle = '#ffffff';
    Pen.font = '500 40px "DM Sans", sans-serif';
    Pen.fillText(Track.Album.toUpperCase(), 34, Size - 70);
    Pen.font = '400 26px "DM Sans", sans-serif';
    Pen.fillStyle = 'rgba(255,255,255,0.75)';
    Pen.fillText(Track.Artist, 34, Size - 32);
    return Paint;
}

const CoverGroup = new THREE.Group();
Scene.add(CoverGroup);
const Covers = [];
const CoverGlow = new THREE.Sprite(new THREE.SpriteMaterial({ map: SoftGlow, color: '#c9b6ff', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
CoverGlow.scale.setScalar(6);
CoverGlow.renderOrder = 1;
Scene.add(CoverGlow);
let CoverTwin = null, CoverShift = 0;

async function BuildCovers()
{
    await document.fonts.ready;
    await Promise.all(['500 40px "DM Sans"', '400 26px "DM Sans"'].map((Face) => document.fonts.load(Face).catch(() => null)));
    Tracks.forEach((Track) =>
    {
        const Paint   = PaintCover(Track);
        Track.Image   = Paint.toDataURL('image/jpeg', 0.86);
        const Texture = new THREE.CanvasTexture(Paint);
        Texture.colorSpace = THREE.SRGBColorSpace;
        Texture.anisotropy = 4;
        const Face = new THREE.Mesh(new THREE.PlaneGeometry(2.5, 2.5), new THREE.MeshBasicMaterial({ map: Texture, transparent: true, opacity: 0, toneMapped: false, side: THREE.DoubleSide }));
        Face.userData.Track = Covers.length;
        CoverGroup.add(Face);
        Covers.push(Face);
    });
    CoverTwin = MirrorTwin(CoverGroup);
    Scene.add(CoverTwin.Group);
    SetOrder(CoverTwin.Group, -1);
    SetOrder(CoverGroup, 2);
}

function PlaceCovers(Delta)
{
    CoverShift += (Condition.Track - CoverShift) * (1 - Math.exp(-Delta * 6));
    const Base = CameraPresets.Media.Yaw + Math.PI, Lift = 3.05 - (1 - Live.Covers) * 4.2;
    for (const Face of Covers)
    {
        const Offset = Face.userData.Track - CoverShift;
        const Angle  = Base + Offset * 0.36;
        Face.position.set(Math.sin(Angle) * 8.2, Lift, Math.cos(Angle) * 8.2);
        Face.rotation.set(0, Angle + Math.PI, 0);
        const Near = Math.max(0, 1 - Math.abs(Offset));
        Face.scale.setScalar(0.86 + 0.3 * Near);
        Face.material.opacity = THREE.MathUtils.clamp(1 - Math.abs(Offset) * 0.24, 0.0, 1) * Live.Covers;
        Face.visible = Live.Covers > 0.01 && Math.abs(Offset) < 3.6;
    }
    const Centre = Covers[Condition.Track];
    if (Centre)
    {
        CoverGlow.position.copy(Centre.position).add(new THREE.Vector3(0, 0, 0)).multiplyScalar(1.04);
        CoverGlow.material.color.setHSL(Tracks[Condition.Track].Hues[1] / 360, 0.8, 0.6);
        CoverGlow.material.opacity = Live.Covers * (0.32 + (Condition.Playing ? 0.12 * Math.sin(performance.now() / 234) : 0));
    }
    if (CoverTwin) CoverTwin.Sync();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LAKE RINGS
//------------------------------------------------------------------------------------------------------------------------

const RingSlots = [0, 0, 0].map(() => ({ Radius: 99, Level: 0 }));
let RingNext = 0;
function SendRing(Level = 1, Start = 0.6)
{
    const Slot = RingSlots[RingNext];
    RingNext = (RingNext + 1) % RingSlots.length;
    Slot.Radius = Start;
    Slot.Level  = Level;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MENUS
//------------------------------------------------------------------------------------------------------------------------

function OpenMenu(Name)
{
    if (!MenuOrder.includes(Name)) return;
    Condition.Menu = Name;
    Display.dataset.menuNow = Name;
    for (const Layer of All('.Layer')) Layer.classList.toggle('On', Layer.dataset.layer === Name);
    for (const Button of All('[data-menu]')) Button.classList.toggle('On', Button.dataset.menu === Name);
    const Active = Display.querySelector(`.DockMenu [data-menu="${Name}"]`);
    Find('DockGlide').style.left = `${Active.offsetLeft}px`;
    Orbit.Aim(CameraPresets[Name], 1500);
    SendRing(0.8, 2.5);
    if (Name === 'Lights') requestAnimationFrame(PlaceBeamGlide);
}
for (const Button of All('[data-menu]')) Button.addEventListener('click', () => OpenMenu(Button.dataset.menu));
for (const Button of All('[data-open]')) Button.addEventListener('click', () => OpenMenu(Button.dataset.open));

//------------------------------------------------------------------------------------------------------------------------
//                                                    HOME
//------------------------------------------------------------------------------------------------------------------------

let AssistTimers = [];
Find('AssistCard').addEventListener('click', () =>
{
    const Card = Find('AssistCard');
    AssistTimers.forEach(clearTimeout);
    Card.classList.add('Listening');
    Find('AssistLine').textContent = 'Listening…';
    Find('AssistHint').textContent = '“Warm my seat”';
    AssistTimers = [
        setTimeout(() =>
        {
            Card.classList.remove('Listening');
            Condition.SeatHeat = 3;
            UpdateComfort();
            Find('AssistLine').textContent = 'Seat warming on';
            Find('AssistHint').textContent = 'Driver seat · level 3';
            Toast('Driver seat heat set to level 3');
        }, 3400),
        setTimeout(() => { Find('AssistLine').textContent = 'Can I help you?'; Find('AssistHint').textContent = 'Tap and say “warm my seat”'; }, 8000),
    ];
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    DRIVE
//------------------------------------------------------------------------------------------------------------------------

function PlacePillGlide()
{
    const Active = Display.querySelector('#ModePills button.On');
    const Glide  = Display.querySelector('.PillGlide');
    Glide.style.left  = `${Active.offsetLeft}px`;
    Glide.style.width = `${Active.offsetWidth}px`;
}
function SelectMode(Index)
{
    Condition.Mode = Index;
    const Mode = DriveModes[Index];
    for (const Button of All('[data-mode]')) Button.classList.toggle('On', Number(Button.dataset.mode) === Index);
    PlacePillGlide();
    Find('ModeName').textContent = Mode.Name;
    Find('TripNote').textContent = Mode.Note;
    const Tone = new THREE.Color(Mode.Colours[0]);
    Currents.Drive.Tone.value.copy(Tone);
    Display.style.setProperty('--ModeTone', Mode.Colours[0]);
    Display.style.setProperty('--ModeRGB', `${Math.round(Tone.r * 255)}, ${Math.round(Tone.g * 255)}, ${Math.round(Tone.b * 255)}`);
    Display.querySelector('.PillGlide').style.background = `linear-gradient(135deg, #ffffff, ${Mode.Colours[0]})`;
    ModeFlash = 1;
    UpdateSpeedNote();
    UpdateTrip(true);
}
let ModeFlash = 0, TripShown = 0;
for (const Button of All('[data-mode]')) Button.addEventListener('click', () => SelectMode(Number(Button.dataset.mode)));
function UpdateSpeedNote()
{
    Find('SpeedNote').textContent = Condition.Cruising ? `${DriveModes[Condition.Mode].Name} preview · ${DriveModes[Condition.Mode].Speed} km/h` : 'Parked · tap Cruise to preview';
}
// Trip column: fuel and battery shares plus the split range (electric first, then fuel), scaled by the mode's reach.
function UpdateTrip(Roll = false)
{
    const Mode   = DriveModes[Condition.Mode];
    const Fuel   = Motion.Fuel ?? Train.Fuel;
    const Litres = Fuel * Train.Tank;
    const Gas    = Math.round((Motion.Range ?? Train.Range) * Mode.Reach);
    const Ev     = Math.round(Condition.Battery * 0.6 * Mode.Reach);
    Find('TripFuel').textContent = Math.round(Fuel * 100);
    Find('TripFuelBar').style.width = `${Fuel * 100}%`;
    Find('TripLitres').textContent = `${Litres.toFixed(1)} L of ${Train.Tank}`;
    Find('TripCell').textContent = Math.round(Condition.Battery);
    Find('TripCellBar').style.width = `${Condition.Battery}%`;
    if (Roll) RollNumber(Find('TripRange'), Ev + Gas, { Duration: 700 }); else Find('TripRange').textContent = Ev + Gas;
    Find('RangeEvText').textContent  = Ev;
    Find('RangeGasText').textContent = Gas;
    Find('RangeEvBar').style.flexGrow  = Ev;
    Find('RangeGasBar').style.flexGrow = Gas;
}
Find('CruiseButton').addEventListener('click', () =>
{
    Condition.Cruising = !Condition.Cruising;
    Find('CruiseButton').classList.toggle('On', Condition.Cruising);
    Find('CruiseButton').querySelector('span').textContent = Condition.Cruising ? 'Stop' : 'Cruise';
    UpdateSpeedNote();
});
Find('ProfileButton').addEventListener('click', () => Toast('Profiles · Amara K. (active) · Guest'));

//------------------------------------------------------------------------------------------------------------------------
//                                                    CHARGE
//------------------------------------------------------------------------------------------------------------------------

{
    const Hours = [['21h', 19, 'Moon'], ['22h', 19, ''], ['23h', 18, 'Moon'], ['00h', 18, ''], ['01h', 18, 'Moon']];
    Find('Forecast').innerHTML = Hours.map(([Hour, Temp, Kind]) => `<div class="${Kind}"><span>${Hour}</span><i></i><b>${Temp}°</b></div>`).join('');
}
function UpdateCharge()
{
    const Card = Display.querySelector('.ChargeCard');
    Card.classList.toggle('Charging', Condition.Charging);
    Find('ChargeState').textContent = Condition.Charging ? (Condition.Battery >= Condition.Limit ? 'LIMIT REACHED' : 'CHARGING') : 'PLUGGED IN';
    Find('ChargeButtonText').textContent = Condition.Charging ? 'Stop charging' : 'Start charging';
    Find('BatteryFill').style.width = `calc(${Condition.Battery}% - ${Condition.Battery * 0.12}px)`;
    Find('BatteryLimit').style.left = `${Condition.Limit}%`;
    Find('LimitText').textContent = `${Condition.Limit}%`;
    Find('ChargeEtaLabel').textContent = `to ${Condition.Limit}%`;
    const Left = Math.max(0, Condition.Limit - Condition.Battery);
    const Minutes = Math.round(Left * 0.62);
    Find('ChargeEta').textContent = Left <= 0 ? 'Done' : `${Minutes} min`;
    const Ratio = (Condition.Limit - 50) / 50;
    Find('LimitFill').style.width = `${Ratio * 100}%`;
    Find('LimitKnob').style.left = `${Ratio * 100}%`;
}
Find('ChargeButton').addEventListener('click', () =>
{
    Condition.Charging = !Condition.Charging;
    RollNumber(Find('ChargeRate'), Condition.Charging ? 148 : 0, { Duration: 1400 });
    if (Condition.Charging) SendRing(1.2, 1.2);
    UpdateCharge();
});
{
    const Slider = Find('LimitSlider');
    const Apply  = (Press) =>
    {
        const Box   = Slider.getBoundingClientRect();
        const Ratio = THREE.MathUtils.clamp((Press.clientX - Box.left) / Box.width, 0, 1);
        Condition.Limit = Math.round((50 + Ratio * 50) / 5) * 5;
        UpdateCharge();
    };
    let Held = false;
    Slider.addEventListener('pointerdown', (Press) => { Held = true; Slider.setPointerCapture(Press.pointerId); Apply(Press); });
    Slider.addEventListener('pointermove', (Press) => { if (Held) Apply(Press); });
    Slider.addEventListener('pointerup', () => { Held = false; });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMFORT
//------------------------------------------------------------------------------------------------------------------------

const TempLimits = [16, 28];
for (const Bar of All('.TempBar'))
{
    for (let Step = 0; Step < 12; Step++)
    {
        const Segment = document.createElement('i');
        Segment.style.setProperty('--Tone', `#${TempTone(16 + Step + 0.5, new THREE.Color()).getHexString(THREE.SRGBColorSpace)}`);
        Bar.append(Segment);
    }
    const Zone = Number(Bar.dataset.zone);
    let Held = false;
    const Apply = (Press) =>
    {
        const Box   = Bar.getBoundingClientRect();
        const Ratio = THREE.MathUtils.clamp(1 - (Press.clientY - Box.top) / Box.height, 0, 1);
        SetTemp(Zone, Math.round(TempLimits[0] + Ratio * (TempLimits[1] - TempLimits[0])));
    };
    Bar.addEventListener('pointerdown', (Press) => { Held = true; Bar.setPointerCapture(Press.pointerId); Apply(Press); });
    Bar.addEventListener('pointermove', (Press) => { if (Held) Apply(Press); });
    Bar.addEventListener('pointerup', () => { Held = false; });
}
function SetTemp(Zone, Value)
{
    const Clamped = THREE.MathUtils.clamp(Value, TempLimits[0], TempLimits[1]);
    if (Clamped === Condition.Temps[Zone]) return;
    Condition.Temps[Zone] = Clamped;
    UpdateComfort();
}
for (const Button of All('.TempStep')) Button.addEventListener('click', () => SetTemp(Number(Button.dataset.zone), Condition.Temps[Number(Button.dataset.zone)] + Number(Button.dataset.step)));

function UpdateComfort()
{
    Condition.Temps.forEach((Temp, Zone) =>
    {
        const Tone = `#${TempTone(Temp, new THREE.Color()).getHexString(THREE.SRGBColorSpace)}`;
        RollNumber(Find(Zone === 0 ? 'TempLeftText' : 'TempRightText'), Temp, { Duration: 380 });
        const Dock = Find(Zone === 0 ? 'DockTempLeft' : 'DockTempRight');
        Dock.textContent = `${Temp}°`;
        Dock.style.color = Tone;
        const Segments = [...Find(Zone === 0 ? 'TempBarLeft' : 'TempBarRight').children];
        const Lit = Temp - TempLimits[0];
        Segments.forEach((Segment, Step) => { Segment.classList.toggle('Lit', Step < Lit); Segment.classList.toggle('Head', Step === Lit - 1); });
    });
    const Heat = Display.querySelector('[data-heat]'), Vent = Display.querySelector('[data-vent]');
    [...Heat.querySelectorAll('.Levels i')].forEach((Level, Step) => Level.classList.toggle('Lit', Step < Condition.SeatHeat));
    [...Vent.querySelectorAll('.Levels i')].forEach((Level, Step) => Level.classList.toggle('Lit', Step < Condition.SeatVent));
    Heat.classList.toggle('Live', Condition.SeatHeat > 0);
    Vent.classList.toggle('Live', Condition.SeatVent > 0);
    for (const Tab of All('[data-tab]')) Tab.classList.toggle('On', Condition.Tabs[Tab.dataset.tab]);
    Find('FanText').textContent = Condition.Auto ? 'AUTO' : `FAN ${Condition.Fan}`;
    Find('FanSub').textContent  = Condition.Auto ? `fan ${Condition.Fan}` : 'manual';
    Display.style.setProperty('--FanPeriod', `${(3.4 - Condition.Fan * 0.4).toFixed(2)}s`);
    for (const Button of All('[data-air]')) Button.classList.toggle('On', Condition.Air[Button.dataset.air]);
}
Display.querySelector('[data-heat]').addEventListener('click', () => { Condition.SeatHeat = (Condition.SeatHeat + 1) % 4; UpdateComfort(); });
Display.querySelector('[data-vent]').addEventListener('click', () => { Condition.SeatVent = (Condition.SeatVent + 1) % 4; UpdateComfort(); });
for (const Tab of All('[data-tab]')) Tab.addEventListener('click', () =>
{
    const Key = Tab.dataset.tab;
    Condition.Tabs[Key] = !Condition.Tabs[Key];
    if (Key === 'Max' && Condition.Tabs.Max) { Condition.Auto = false; Condition.Fan = 7; Toast('Max airflow'); }
    if (Key === 'Max' && !Condition.Tabs.Max) { Condition.Auto = true; Condition.Fan = 3; }
    if (Key === 'Eco') { Condition.Fan = Math.min(Condition.Fan, Condition.Tabs.Eco ? 4 : 7); Toast(Condition.Tabs.Eco ? 'ECO climate · fan capped, +6 km range' : 'ECO climate off'); }
    if (Key === 'Rear') Toast(Condition.Tabs.Rear ? 'Rear climate on · synced to driver' : 'Rear climate off');
    if (Key === 'Ac') Toast(Condition.Tabs.Ac ? 'A/C on' : 'A/C off · fan only');
    UpdateComfort();
});
Find('AutoButton').addEventListener('click', () => { Condition.Auto = !Condition.Auto; UpdateComfort(); });
for (const Button of All('[data-fan]')) Button.addEventListener('click', () => { Condition.Auto = false; Condition.Fan = THREE.MathUtils.clamp(Condition.Fan + Number(Button.dataset.fan), 1, Condition.Tabs.Eco ? 4 : 7); UpdateComfort(); });
for (const Button of All('[data-air]')) Button.addEventListener('click', () =>
{
    const Key = Button.dataset.air;
    Condition.Air[Key] = !Condition.Air[Key];
    if (!Object.values(Condition.Air).some(Boolean)) Condition.Air.Face = true;   // air always goes somewhere
    if (Key === 'Screen' && Condition.Air.Screen) Toast('Screen demist · A/C on');
    if (Key === 'Screen' && Condition.Air.Screen) Condition.Tabs.Ac = true;
    UpdateComfort();
});

// ---- Seat position: slide fore/aft and back angle (cards, ring dots and dragging the seat itself).
function SetSlide(Index, Value, ByHand = true)
{
    Condition.Slide[Index] = Math.round(THREE.MathUtils.clamp(Value, ...SlideLimits));
    if (ByHand) Condition.Memory[Index] = -1;
    UpdateSeats();
}
function SetRecline(Index, Value, ByHand = true)
{
    Condition.Recline[Index] = Math.round(THREE.MathUtils.clamp(Value, ...TiltLimits));
    if (ByHand) Condition.Memory[Index] = -1;
    UpdateSeats();
}
function RecallSeat(Index, Slot)
{
    const Memory = SeatMemories[Slot];
    Condition.Memory[Index] = Slot;
    SetSlide(Index, Memory.Slide, false);
    SetRecline(Index, Memory.Recline, false);
    Toast(`${Index === 0 ? 'Driver' : 'Passenger'} seat · ${Memory.Name}`);
}
function UpdateSeats()
{
    for (const Card of All('.SeatCard'))
    {
        const Index = Number(Card.dataset.seat);
        const Slide = Condition.Slide[Index], Tilt = Condition.Recline[Index];
        const Share = (Slide - SlideLimits[0]) / (SlideLimits[1] - SlideLimits[0]);
        const Left  = Index === 0 ? 1 - Share : Share;                   // the track reads the way the seat faces on screen
        Card.querySelector('.Knob').style.left = `${Left * 100}%`;
        Card.querySelector('.Fill').style.left  = `${Math.min(Left, 0.5) * 100}%`;
        Card.querySelector('.Fill').style.width = `${Math.abs(Left - 0.5) * 100}%`;
        Card.querySelector('.SlideText').textContent = `${Slide > 0 ? '+' : Slide < 0 ? '−' : '±'}${Math.abs(Slide)} mm`;
        Card.querySelector('.TiltText').textContent = `${90 + Tilt}°`;
        Card.querySelector('.AngleGlyph').style.setProperty('--Tilt', Tilt);
        for (const Button of Card.querySelectorAll('[data-memory]')) Button.classList.toggle('On', Number(Button.dataset.memory) === Condition.Memory[Index]);
    }
}
for (const Card of All('.SeatCard'))
{
    const Index = Number(Card.dataset.seat);
    for (const Button of Card.querySelectorAll('[data-memory]')) Button.addEventListener('click', () => RecallSeat(Index, Number(Button.dataset.memory)));
    for (const Button of Card.querySelectorAll('[data-tilt]')) Button.addEventListener('click', () => SetRecline(Index, Condition.Recline[Index] + Number(Button.dataset.tilt)));
    const Track = Card.querySelector('.SlideTrack');
    const Apply = (Press) =>
    {
        const Box   = Track.getBoundingClientRect();
        const Left  = THREE.MathUtils.clamp((Press.clientX - Box.left) / Box.width, 0, 1);
        const Share = Index === 0 ? 1 - Left : Left;
        SetSlide(Index, SlideLimits[0] + Share * (SlideLimits[1] - SlideLimits[0]));
    };
    let Held = false;
    Track.addEventListener('pointerdown', (Press) => { Held = true; Track.classList.add('Held'); Track.setPointerCapture(Press.pointerId); Apply(Press); });
    Track.addEventListener('pointermove', (Press) => { if (Held) Apply(Press); });
    Track.addEventListener('pointerup', () => { Held = false; Track.classList.remove('Held'); });
}

// 📝 In the scene: drag a ring dot round its arc to set the back angle, or drag a seat sideways to slide it. These
//    listeners run in the capture phase so a grab never starts a camera orbit.
{
    const Ray = new THREE.Raycaster(), Pointer = new THREE.Vector2(), Hit = new THREE.Vector3();
    const DialPlane = new THREE.Plane(new THREE.Vector3(0, 0, 1), 0.75);           // z = −0.75, the dial plane
    let Grab = null;
    const Aim = (Press) =>
    {
        const Box = Canvas.getBoundingClientRect();
        Pointer.set(((Press.clientX - Box.left) / Box.width) * 2 - 1, -((Press.clientY - Box.top) / Box.height) * 2 + 1);
        Ray.setFromCamera(Pointer, Camera);
    };
    const Pick = () =>
    {
        if (Condition.Menu !== 'Comfort' || Live.SeatRise < 0.6) return null;
        for (let Index = 0; Index < Seats.length; Index++) if (Ray.intersectObject(Seats[Index].Grip, false).length) return { Index, Part: 'Tilt' };
        for (let Index = 0; Index < Seats.length; Index++) if (Ray.intersectObjects(Seats[Index].Parts, false).length) return { Index, Part: 'Slide' };
        return null;
    };
    Canvas.addEventListener('pointerdown', (Press) =>
    {
        Aim(Press);
        Grab = Pick();
        if (!Grab) return;
        Press.stopImmediatePropagation();
        Canvas.setPointerCapture(Press.pointerId);
        if (Ray.ray.intersectPlane(DialPlane, Hit)) Object.assign(Grab, { From: Hit.x, Start: Condition.Slide[Grab.Index] });
        Seats[Grab.Index].Held = 1;
        Display.classList.add('Gripping');
    }, { capture: true });
    Canvas.addEventListener('pointermove', (Move) =>
    {
        Aim(Move);
        if (!Grab) { Display.classList.toggle('Hovering', Boolean(Pick())); return; }
        Move.stopImmediatePropagation();
        if (!Ray.ray.intersectPlane(DialPlane, Hit)) return;
        const Index = Grab.Index, Seat = Seats[Index];
        if (Grab.Part === 'Tilt')
        {
            const Angle = THREE.MathUtils.radToDeg(Math.atan2(Hit.y - Seat.Dial.position.y, Hit.x - Seat.Dial.position.x));
            const Lean  = THREE.MathUtils.euclideanModulo((Index === 0 ? 90 - Angle : Angle - 90) + 180, 360) - 180;
            SetRecline(Index, Lean);
        }
        else
        {
            const Forward = Index === 0 ? -1 : 1;                                   // the driver seat faces −X
            SetSlide(Index, Grab.Start + ((Hit.x - Grab.From) * Forward) / (SlideReach * SeatScale));
        }
    }, { capture: true });
    const Release = () => { if (Grab) Seats[Grab.Index].Held = 0; Grab = null; Display.classList.remove('Gripping'); };
    Canvas.addEventListener('pointerup', Release, { capture: true });
    Canvas.addEventListener('pointercancel', Release, { capture: true });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MEDIA
//------------------------------------------------------------------------------------------------------------------------

const Clock = (Seconds) => `${Math.floor(Seconds / 60)}:${String(Math.floor(Seconds % 60)).padStart(2, '0')}`;
function UpdateTrack()
{
    const Track = Tracks[Condition.Track];
    Find('NowTitle').textContent  = Track.Title;
    Find('NowArtist').textContent = `${Track.Artist} · ${Track.Album}`;
    Find('TimeAll').textContent   = Clock(Track.Length);
    for (const [Title, Artist, Cover] of [['HomeTitle', 'HomeArtist', 'HomeCover']])
    {
        Find(Title).textContent  = Track.Title;
        Find(Artist).textContent = Track.Artist;
        if (Track.Image) Find(Cover).style.backgroundImage = `url(${Track.Image})`;
    }
    const Queue = [1, 2, 3, 4].map((Step) => (Condition.Track + Step) % Tracks.length);
    Find('Queue').innerHTML = [Condition.Track, ...Queue].map((Index) =>
    {
        const Each = Tracks[Index];
        return `<button class="QueueRow${Index === Condition.Track ? ' On' : ''}" data-track="${Index}" data-ripple><i style="background-image:url(${Each.Image || ''})"></i><div><b>${Each.Title}</b><small>${Each.Artist}</small></div><em>${Clock(Each.Length)}</em></button>`;
    }).join('');
}
function UpdatePlay()
{
    for (const Element of [Display.querySelector('.PlayerPanel'), Display.querySelector('.Card.Track')]) Element.classList.toggle('Playing', Condition.Playing);
    Find('PlayState').textContent = Condition.Playing ? 'Playing' : 'Paused';
}
function SelectTrack(Index)
{
    Condition.Track = (Index + Tracks.length) % Tracks.length;
    Condition.TrackTime = 0;
    UpdateTrack();
    SendRing(1.1, 1.0);
}
Find('Queue').addEventListener('click', (Press) => { const Row = Press.target.closest('[data-track]'); if (Row) SelectTrack(Number(Row.dataset.track)); });
for (const Id of ['PlayButton']) Find(Id).addEventListener('click', () => { Condition.Playing = !Condition.Playing; UpdatePlay(); });
Find('NextTrack').addEventListener('click', () => SelectTrack(Condition.Track + 1));
Find('PrevTrack').addEventListener('click', () => SelectTrack(Condition.Track - 1));
Find('NextCover').addEventListener('click', () => SelectTrack(Condition.Track + 1));
Find('PrevCover').addEventListener('click', () => SelectTrack(Condition.Track - 1));
Find('PulseToggle').addEventListener('click', () => { Condition.Pulse = !Condition.Pulse; Find('PulseToggle').classList.toggle('On', Condition.Pulse); });
Find('Progress').addEventListener('click', (Press) =>
{
    const Box = Find('Progress').getBoundingClientRect();
    Condition.TrackTime = THREE.MathUtils.clamp((Press.clientX - Box.left) / Box.width, 0, 1) * Tracks[Condition.Track].Length;
});

// Click a cover in the scene to play it (a press that did not drag the orbit).
{
    const Caster = new THREE.Raycaster(), Pointer = new THREE.Vector2();
    let Origin = null;
    Canvas.addEventListener('pointerdown', (Press) => { Origin = [Press.clientX, Press.clientY]; });
    Canvas.addEventListener('pointerup', (Release) =>
    {
        if (!Origin || Condition.Menu !== 'Media' || Math.hypot(Release.clientX - Origin[0], Release.clientY - Origin[1]) > 6) return;
        const Box = Canvas.getBoundingClientRect();
        Pointer.set(((Release.clientX - Box.left) / Box.width) * 2 - 1, -((Release.clientY - Box.top) / Box.height) * 2 + 1);
        Caster.setFromCamera(Pointer, Camera);
        const Hit = Caster.intersectObjects(Covers.filter((Face) => Face.visible), false)[0];
        if (Hit) SelectTrack(Hit.object.userData.Track);
    });
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CLOCKS
//------------------------------------------------------------------------------------------------------------------------

function UpdateClock()
{
    const Now  = new Date();
    const Text = ClockText(Now);
    Find('TopClock').textContent  = Text;
    Find('DateLine').textContent  = Now.toLocaleDateString('en-GB', { weekday: 'long', day: 'numeric', month: 'long' });
}
UpdateClock();
setInterval(UpdateClock, 10000);

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME
//------------------------------------------------------------------------------------------------------------------------

const GoalColour = new THREE.Color(), Scratch = new THREE.Color();
const Chips = [0, 1].map((Index) => Display.querySelector(`[data-chip="${Index}"]`)), ChipPoint = new THREE.Vector3(), TagPoint = new THREE.Vector3();
const Tags = Object.fromEntries(All('.FlowTag').map((Tag) => [Tag.dataset.part, Tag]));

// Drive: ease each current towards the plan, run its pulses, light the parts and place the HTML tags.
function StepCurrent(Wall, Elapsed)
{
    const Show = THREE.MathUtils.clamp(Live.Flow, 0, 1);
    CurrentShow.value = Show;
    PowerCurrent.visible = Show > 0.01 && Boolean(CurrentParts.Engine);
    ModeFlash *= Math.exp(-Wall * 2.5);
    const Plan = FlowPlan();
    const Settle = 1 - Math.exp(-Wall * 3);
    for (const [Name, Current] of Object.entries(Currents))
    {
        Current.Goal   = Plan[Name];
        Current.Shown += (Current.Goal - Current.Shown) * Settle;
        Current.Level.value = Math.abs(Current.Shown) + ModeFlash * 0.4;
        Current.Phase.value += Wall * (0.8 + Condition.Speed / 70) * Math.sign(Current.Shown || 1) * Math.min(1, Math.abs(Current.Shown) * 3);
    }
    Currents.Battery.Tone.value.set(Currents.Battery.Shown < -0.02 ? '#67f5b1' : '#5fe3ff');
    // The battery fills from regen or the engine and drains when it drives (display-scale, about 6× real time).
    if (Show > 0.5) Condition.Battery = THREE.MathUtils.clamp(Condition.Battery - Plan.Kw.Battery * Wall / 3600 / 14 * 100 * 6, 0, 100);
    if (!PowerCurrent.visible) return;
    const { Engine, Motor, Cell, Tank, Diff, Rings } = CurrentParts;
    const Light = (Part, Level, Base = 0.18) =>
    {
        Part.Level += (Level - Part.Level) * Settle;
        Part.Fill.material.opacity = Show * (0.05 + Part.Level * 0.16);
        Part.Edge.material.opacity = Show * (Base + Part.Level * 0.7);
    };
    Light(Engine, Math.abs(Currents.Engine.Shown));
    Light(Motor, Math.max(Math.abs(Currents.Battery.Shown), Math.abs(Currents.Drive.Shown)));
    Light(Cell, Math.abs(Currents.Battery.Shown), 0.3);
    Light(Tank, Math.abs(Currents.Fuel.Shown), 0.3);
    Light(Diff, Math.abs(Currents.Drive.Shown));
    Engine.Pistons.forEach((Piston, Each) =>
    {
        Piston.position.y = 0.3 + Math.sin(Elapsed * (8 + Condition.Speed * 0.2) + Each * Math.PI * 0.5 * (Each % 2 ? 3 : 1)) * 0.05 * Engine.Level;
        Piston.material.opacity = Show * (0.15 + Engine.Level * 0.6);
    });
    Motor.Rotor.rotation.x += Wall * (2 + Condition.Speed * 0.12) * Math.sign(Currents.Drive.Shown || 1) * Motor.Level;
    Motor.Rotor.material.opacity = Show * (0.2 + Motor.Level * 0.7);
    const Lit = Math.round(Condition.Battery / 10);
    Cell.Blocks.forEach((Block, Each) =>
    {
        const On = Each < Lit;
        Block.material.color.copy(Currents.Battery.Tone.value);
        Block.material.opacity = Show * (On ? 0.22 + Cell.Level * 0.25 * (0.6 + 0.4 * Math.sin(Elapsed * 5 - Each * 0.7 * Math.sign(Currents.Battery.Shown || 1))) : 0.03);
    });
    Tank.Gauge.scale.y = Math.max(0.02, Motion.Fuel ?? Train.Fuel);
    Tank.Gauge.material.opacity = Show * 0.2;
    for (const Ring of Rings)
    {
        Ring.material.color.copy(Currents.Drive.Shown < -0.02 ? Regen : Currents.Drive.Tone.value);
        Ring.material.opacity = Show * Math.abs(Currents.Drive.Shown) * (0.35 + 0.15 * Math.sin(Elapsed * 6));
    }
    // HTML: tags over the parts, the flow card and the trip column (four times a second).
    for (const [Name, Tag] of Object.entries(Tags))
    {
        TagPoint.copy(CurrentParts[Name].Anchor);
        const Spot = ProjectAnchor(Camera, TagPoint, DisplayWidth, DisplayHeight);
        Tag.style.left = `${Spot.X}px`;
        Tag.style.top  = `${Spot.Y}px`;
        Tag.classList.toggle('Live', CurrentParts[Name].Level > 0.3);
    }
    TripShown += Wall;
    if (TripShown < 0.25) return;
    TripShown = 0;
    Find('FlowState').textContent = Plan.State;
    const Row = (Name, Value, Scale, Text) =>
    {
        const Element = Display.querySelector(`.FlowRow[data-flow="${Name}"]`);
        Element.classList.toggle('Live', Math.abs(Value) > 0.5);
        Element.classList.toggle('Reverse', Value < -0.5);
        Element.querySelector('em i').style.width = `${Math.min(100, Math.abs(Value) / Scale * 100)}%`;
        Find(`Flow${Name}`).textContent = Text;
    };
    Row('Engine', Plan.Kw.Engine, 150, Math.round(Plan.Kw.Engine));
    Row('Battery', Plan.Kw.Battery, 85, `${Plan.Kw.Battery < -0.5 ? '−' : ''}${Math.round(Math.abs(Plan.Kw.Battery))}`);
    Row('Drive', Plan.Kw.Drive, 200, `${Plan.Kw.Drive < -0.5 ? '−' : ''}${Math.round(Math.abs(Plan.Kw.Drive))}`);
    const Litres = Plan.Kw.Engine * 0.27 + (Plan.Fuel ? 0.4 : 0);
    Row('Fuel', Litres, 40, Litres.toFixed(1));
    Find('TripCellState').textContent = Plan.Kw.Battery < -0.5 ? 'charging' : Plan.Kw.Battery > 0.5 ? 'driving' : 'holding';
    Display.querySelector('.TripColumn').classList.toggle('Charging-Cell', Plan.Kw.Battery < -0.5);
    UpdateTrip();
}
let WallLast = performance.now(), BeatClock = 0, Distance = 0;

function Step(Delta, Elapsed)
{
    const Now  = performance.now();
    const Wall = Math.min(0.25, (Now - WallLast) / 1000);              // wall-clock step, so transitions finish at any frame rate
    WallLast   = Now;
    const Ease = 1 - Math.exp(-Wall * 2.6);
    const Look = Looks[Condition.Menu];

    for (const Key of ColourKeys)
    {
        GoalColour.set(Key === 'Under' && Condition.Menu === 'Drive' ? DriveModes[Condition.Mode].Colours[0] : Look[Key]);
        Live[Key].lerp(GoalColour, Ease);
    }
    for (const Key of LevelKeys) Live[Key] += (Look[Key] - Live[Key]) * (Key === 'CarFade' || Key === 'SeatRise' ? 1 - Math.exp(-Wall * 2.0) : Ease);

    // Sky, lake, fog.
    Scene.fog.color.copy(Live.Horizon);
    StarUniforms.Clock.value = Elapsed;
    StarUniforms.Level.value = Live.Stars;
    AuroraUniforms.Clock.value = Elapsed;
    AuroraUniforms.Strength.value = Live.Aurora;
    Aurora.visible = Live.Aurora > 0.01;
    LakeUniforms.Clock.value = Elapsed;

    // Drive: speed, wheel spin, curtains, lake streaks.
    const SpeedGoal = Condition.Cruising ? DriveModes[Condition.Mode].Speed : 0;
    Condition.Cruise += (SpeedGoal - Condition.Cruise) * (1 - Math.exp(-Wall * 0.9));
    if (Math.abs(SpeedGoal - Condition.Cruise) < 0.4) Condition.Cruise = SpeedGoal;
    // The Tide dashboard cluster drives when it is open (cabin link); otherwise the HMI's own powertrain idles.
    Train.Step();
    Motion = Link.Fresh('Drive') || Train;
    Condition.Speed = Math.max(Condition.Cruise, Motion.Kmh);
    Find('SpeedText').textContent = Math.round(Condition.Speed);
    const Metres = (Condition.Speed / 3.6) * Wall;
    Distance += Metres;
    LakeUniforms.Flow.value = Distance * 0.9;
    LakeUniforms.Streak.value = Math.min(1, Condition.Speed / 60) * Live.Flow * 0.3;
    StepCurrent(Wall, Elapsed);

    // Lake rings: menu changes, charging start and the music beat.
    if (Condition.Playing)
    {
        const Track = Tracks[Condition.Track];
        Condition.TrackTime += Wall;
        if (Condition.TrackTime >= Track.Length) SelectTrack(Condition.Track + 1);
        BeatClock += Wall;
        if (Condition.Pulse && BeatClock > 0.94) { BeatClock = 0; SendRing(Condition.Menu === 'Media' ? 0.9 : 0.5, 1.2); }
    }
    const Ratio = Condition.TrackTime / Tracks[Condition.Track].Length;
    Find('ProgressFill').style.width = `${Ratio * 100}%`;
    Find('ProgressKnob').style.left  = `${Ratio * 100}%`;
    Find('TimeNow').textContent = Clock(Condition.TrackTime);
    RingSlots.forEach((Slot, Index) =>
    {
        Slot.Radius += Wall * 4.2;
        Slot.Level  *= Math.exp(-Wall * 0.45);
        LakeUniforms.RingRadius.value.setComponent(Index, Slot.Radius);
        LakeUniforms.RingLevel.value.setComponent(Index, Slot.Level * THREE.MathUtils.clamp(1 - Slot.Radius / 26, 0, 1));
    });

    // Charge: battery climb, rig, dashes.
    if (Condition.Charging && Condition.Battery < Condition.Limit)
    {
        ChargeAccrual += Wall;
        if (ChargeAccrual > 1.2)
        {
            ChargeAccrual = 0;
            Condition.Battery += 1;
            Condition.Added   += 6;
            RollNumber(Find('BatteryText'), Condition.Battery, { Duration: 500 });
            RollNumber(Find('ChargeAdded'), Condition.Added, { Duration: 500 });
            if (Condition.Battery >= Condition.Limit) RollNumber(Find('ChargeRate'), 0, { Duration: 1200 });
            UpdateCharge();
        }
    }
    const Flowing = Condition.Charging && Condition.Battery < Condition.Limit ? 1 : 0;
    ChargeRig.position.y = -1.6 * (1 - Live.ChargeRig);
    ChargeRig.visible = Live.ChargeRig > 0.01;
    CableUniforms.Clock.value = Elapsed;
    CableUniforms.Run.value  += (Flowing - CableUniforms.Run.value) * Ease;
    PortGlow.material.opacity = Live.ChargeRig * (0.35 + Flowing * (0.45 + 0.2 * Math.sin(Elapsed * 4)));
    ChargeTwin.Sync();
    Dashes.visible = Live.ChargeRig > 0.01;
    if (Dashes.visible)
    {
        let Index = 0;
        for (let Lane = 0; Lane < DashLanes.length; Lane++) for (let Each = 0; Each < DashPerLane; Each++)
        {
            const X = Math.abs(-DashSpan + Each * DashStep);
            const Wave = Math.pow(0.5 + 0.5 * Math.sin(X * 0.9 + Elapsed * (Flowing ? 4.5 : 1.2) + Lane), 5);
            const Fade = THREE.MathUtils.clamp(1 - X / DashSpan, 0, 1);
            DashTone.copy(Live.Glow).multiplyScalar((0.08 + Wave * (0.35 + Flowing * 0.9)) * Fade * Live.ChargeRig);
            Dashes.setColorAt(Index++, DashTone);
        }
        Dashes.instanceColor.needsUpdate = true;
    }

    // Vehicle: wheels, fade, under glow.
    if (Vehicle)
    {
        for (const Wheel of Vehicle.Wheels) Wheel.Spin.rotation.z -= (Metres / 0.509);
        Vehicle.BodyMount.position.y = Vehicle.RestLift + Math.sin(Elapsed * 13) * 0.004 * Math.min(1, Condition.Speed / 50);
        const Fade = THREE.MathUtils.clamp(Live.CarFade * (1 - 0.74 * Live.Ghost), 0, 1);   // Drive ghosts the body to glass
        const Clear = Fade < 0.995;
        for (const Material of FadeMaterials)
        {
            if (Material.transparent !== Clear) { Material.transparent = Clear; Material.needsUpdate = true; }
            Material.opacity = Clear ? Fade : 1;
            Material.depthWrite = Fade > 0.5;
        }
        Vehicle.Root.visible = Live.CarFade > (Look.CarFade < 0.5 ? 0.3 : 0.01);           // lamps leave with the car in Comfort
        if (Lamps)
        {
            Lamps.TailBase = 1.2 + Live.Flow * 0.6;
            Lamps.HeadBase = 0.25;
            Lamps.State.Brake = Motion.Brake > 0.08 ? 1 : 0;
            Lamps.State.Reverse = Motion.Selector === 'R';
            if (Elapsed - LampSent > 0.4) { LampSent = Elapsed; Link.Send('Lamps', { ...Lamps.State }); }
            Lamps.Speed = Condition.Speed;
            Lamps.Step();
        }
        VehicleTwin.Sync();
    }
    UnderPool.material.color.copy(Live.Under);
    UnderPool.material.opacity = Live.UnderLevel * Live.CarFade * (Condition.Menu === 'Charge' && Flowing ? 0.85 + 0.15 * Math.sin(Elapsed * 3) : 1);
    ContactShade.material.opacity = Live.CarFade;
    UnderLight.color.copy(Live.Under);
    UnderLight.intensity = 7 * Live.UnderLevel * Live.CarFade;

    // Comfort: seats rise from the lake as the car fades; each sled glides to its slide and each backrest to its angle.
    const Rise  = THREE.MathUtils.clamp(Live.SeatRise, 0, 1);
    const Glide = 1 - Math.exp(-Wall * 7);
    Seats.forEach((Seat, Index) =>
    {
        const Lift = -2.6 + Rise * 3.05;
        Seat.Group.position.y = Lift + Math.sin(Elapsed * 0.9 + Index * 1.7) * 0.03 * Rise;
        Seat.Group.visible = Rise > 0.01;
        Seat.Dial.visible  = Rise > 0.01;
        Seat.Dial.position.y = Seat.Group.position.y + 0.64 * SeatScale;
        Seat.Dial.scale.setScalar(0.6 + 0.4 * Rise);
        Seat.Leather.opacity = Rise;
        Seat.Leather.depthWrite = Rise > 0.5;
        Seat.RailMaterial.opacity = Rise;
        Seat.ShownSlide += (Condition.Slide[Index] - Seat.ShownSlide) * Glide;
        Seat.ShownTilt  += (Condition.Recline[Index] - Seat.ShownTilt) * Glide;
        Seat.Sled.position.x = Seat.ShownSlide * SlideReach;
        Seat.Back.rotation.z = THREE.MathUtils.degToRad(Seat.ShownTilt - SeatUpright);
        Seat.Marker.position.x = 0.05 + Seat.ShownSlide * SlideReach;
        Seat.Marker.material.opacity = Rise * 0.8;
        if (Math.abs(Seat.ShownTilt - Seat.ArcTilt) > 0.15)
        {
            Seat.ArcTilt = Seat.ShownTilt;
            const Lean = THREE.MathUtils.degToRad(Seat.ShownTilt);
            Seat.LitArc.geometry.dispose();
            Seat.LitArc.geometry = ArcGeometry(Index === 0 ? Math.PI / 2 - Lean : Math.PI / 2, Lean, DialRadius - 0.012, DialRadius + 0.012);
        }
        const Temp  = Condition.Temps[Index];
        TempTone(Temp, Scratch);
        Seat.Tone.lerp(Scratch, Ease);
        const Level = Index === 0 ? Condition.SeatHeat : Condition.SeatVent;
        const Power = 0.55 + Level * 0.35;
        Seat.RimUniforms.Tone.value.copy(Seat.Tone);
        Seat.RimUniforms.Strength.value = Rise * Power * 1.6;
        Seat.Leather.emissive.copy(Seat.Tone).multiplyScalar(0.03 + Level * 0.03);
        Seat.Glow.material.color.copy(Seat.Tone);
        Seat.Glow.material.opacity = Rise * (0.22 + Level * 0.08) * (0.9 + 0.1 * Math.sin(Elapsed * 2 + Index));
        Seat.Pool.material.color.copy(Seat.Tone);
        Seat.Pool.material.opacity = Rise * 0.55;
        Seat.Outer.material.opacity = 0.32 * Rise;
        Seat.Inner.material.opacity = 0.12 * Rise;
        for (const Tick of Seat.Ticks) Tick.material.opacity = 0.6 * Rise;
        // The dot rides the ring where the backrest points; it swells and rings while held.
        const Angle = DotAngle(Index, Seat.ShownTilt);
        Seat.Rider.position.set(Math.cos(Angle) * DialRadius, Math.sin(Angle) * DialRadius, 0.004);
        Seat.Grab = (Seat.Grab ?? 0) + ((Seat.Held ? 1 : 0) - (Seat.Grab ?? 0)) * Glide;
        Seat.Rider.scale.setScalar(1 + Seat.Grab * 0.35 + Math.sin(Elapsed * 2.4 + Index) * 0.04);
        Seat.RiderRing.material.opacity = Rise * (0.35 + Seat.Grab * 0.6);
        Seat.LitArc.material.color.copy(Seat.Tone).lerp(Mild, 0.45);
        Seat.LitArc.material.opacity = Rise * 0.9;
        Seat.RangeArc.material.opacity = Rise * 0.16;
        Seat.RiderCore.material.opacity = Rise;
        Seat.RiderHalo.material.color.copy(Seat.Tone);
        Seat.RiderHalo.material.opacity = Rise;
        // Motes: warm rises for heat, cool drifts down for ventilation.
        const Points = Seat.Motes.geometry.attributes.position;
        const Rising = Index === 0;
        Seat.Seeds.forEach(([Phase, X, Z, Speed], Each) =>
        {
            const Cycle = (Phase + Elapsed * 0.22 * Speed * (0.6 + Level * 0.3)) % 1;
            const Y = Rising ? 0.05 + Cycle * 1.45 : 1.5 - Cycle * 1.45;
            Points.setXYZ(Each, X + Math.sin(Elapsed * Speed + Phase * 9) * 0.05, Y, Z);
        });
        Points.needsUpdate = true;
        Seat.Motes.material.color.copy(Seat.Tone);
        Seat.Motes.material.opacity = Rise * Math.min(1, Level * 0.45);
        SeatTwins[Index].Sync();
        const Chip = Chips[Index];
        if (Condition.Menu === 'Comfort')
        {
            Seat.Rider.getWorldPosition(ChipPoint);
            ChipPoint.x += (Index === 0 ? -1 : 1) * 0.55;
            ChipPoint.y += 0.3;
            const Spot = ProjectAnchor(Camera, ChipPoint, DisplayWidth, DisplayHeight);
            Chip.style.left = `${Spot.X}px`;
            Chip.style.top  = `${Spot.Y}px`;
            Chip.textContent = `${90 + Math.round(Seat.ShownTilt)}°`;
            Chip.classList.toggle('Held', Boolean(Seat.Held));
        }
    });

    // Media.
    PlaceCovers(Wall);
    CoverGroup.visible = Live.Covers > 0.01;
    if (CoverTwin) CoverTwin.Group.visible = CoverGroup.visible;

    Orbit.Advance(Wall);
    Renderer.render(Scene, Camera);
}
let ChargeAccrual = 0;

//------------------------------------------------------------------------------------------------------------------------
//                                                    POWERTRAIN AND CABIN LINK
//------------------------------------------------------------------------------------------------------------------------

// 📝 The instrument cluster lives on the dashboard (Clusters/Tide), not in this HMI. When it is open the link feeds
//    speed, gear and pedals here and this HMI sends its lamp switches back; alone, the car stands parked.

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIGHTS
//------------------------------------------------------------------------------------------------------------------------

let Lamps = null, LampLayer = null;
function PlaceBeamGlide()
{
    const Pills = Find('BeamPills'), Button = Pills.querySelector('[data-beam].On');
    if (!Button || !Button.offsetWidth) return;
    const Glide = Pills.querySelector('.PillGlide');
    Glide.style.left  = `${Button.offsetLeft}px`;
    Glide.style.width = `${Button.offsetWidth}px`;
}
function SetLamps(Patch) { if (!Lamps) return; Lamps.Set(Patch); LampLayer.Refresh(); PlaceBeamGlide(); }

// Moonrise greeting: lamps wake one after another, then the hazards blink twice; the step bar tracks progress.
let GreetTimers = [];
function PlayGreeting()
{
    if (!Lamps) return;
    GreetTimers.forEach(clearTimeout);
    const Steps = Find('GreetSteps').querySelectorAll('i');
    Steps.forEach((Step) => Step.classList.remove('Lit'));
    const Saved = { ...Lamps.State };
    SetLamps({ Beam: 'Off', Wing: false, Rock: false, Signal: 'Off', FogFront: false });
    const Plan = [
        [300,  () => SetLamps({ Beam: 'Parking' })],
        [1100, () => SetLamps({ Beam: 'Low' })],
        [1900, () => SetLamps({ Wing: true })],
        [2700, () => SetLamps({ Rock: true })],
        [3500, () => SetLamps({ Signal: 'Hazard' })],
        [5000, () => SetLamps({ Signal: Saved.Signal, Beam: Saved.Beam === 'Off' ? 'Low' : Saved.Beam, Wing: true, Rock: true })],
    ];
    Plan.forEach(([Delay, Action], Index) => GreetTimers.push(setTimeout(() => { Action(); if (Steps[Index]) Steps[Index].classList.add('Lit'); }, Delay)));
    Toast('Moonrise greeting');
}
Find('GreetButton').addEventListener('click', PlayGreeting);

//------------------------------------------------------------------------------------------------------------------------
//                                                    START
//------------------------------------------------------------------------------------------------------------------------

async function Start()
{
    Vehicle = await AssembleVehicle({ Finish, Wing: true });
    Vehicle.RestLift = Vehicle.BodyMount.position.y;
    Scene.add(Vehicle.Root);
    VehicleTwin = MirrorTwin(Vehicle.Root);
    Scene.add(VehicleTwin.Group);
    BuildCurrent(Vehicle);
    Lamps = MountLamps(Vehicle, { Extras: { Wing: true, Rock: true }, Signature: '#eef3ff', PoolLift: 0.02 });
    LampLayer = BindLampControls(Find('LightsLayer'), Lamps, { OnChange: PlaceBeamGlide });
    SetLamps({ Beam: 'Low' });
    await BuildCovers();
    UpdateTrack();
    UpdatePlay();
    UpdateCharge();
    UpdateComfort();
    UpdateSeats();
    SelectMode(1);
    OpenMenu('Home');
    Orbit.Transit = null;
    Object.assign(Orbit, { Yaw: CameraPresets.Home.Yaw - 0.35, Radius: CameraPresets.Home.Radius + 3 });
    Orbit.Aim(CameraPresets.Home, 2200);
    Find('Loading').classList.add('Done');
    RunLoop(Step);
}
Start().catch((Problem) => { console.error(Problem); Find('Loading').querySelector('span').textContent = `Could not load the vehicle · ${Problem.message}`; });

//------------------------------------------------------------------------------------------------------------------------
//                                                    TEST HOOKS
//------------------------------------------------------------------------------------------------------------------------

window.HmiCondition = {
    Condition,
    Train,
    Link,
    get Lamps() { return Lamps; },
    SetLamps,
    PlayGreeting,
    OpenMenu,
    SelectMode,
    SelectTrack,
    SetTemp,
    SetSlide,
    SetRecline,
    RecallSeat,
    FlowPlan,
    Orbit,
};

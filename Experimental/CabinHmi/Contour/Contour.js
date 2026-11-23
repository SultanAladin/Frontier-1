// CONTOUR — CABIN HMI PROTOTYPE A
// 🧩 Off-road trail display: topographic terrain, drive-mode dial, axle locks, tyre pressures pinned to the wheels,
//    tilt pad with live body pose, proximity fans and lighting, all acting on the Component Viewer's ControlVehicle.

import { THREE, AssembleVehicle, DefaultFinish, PanelEnvironment, Family, EaseColour } from '../Shared/VehicleAssembly.js';
import { FitDisplay, CameraOrbit, ProjectAnchor, RollNumber, BindRipples, ClockText, RunLoop } from '../Shared/DisplayKit.js';
import { Powertrain } from '../Shared/Powertrain.js';
import { MountLamps, BindLampControls, SyncTelltales } from '../Shared/LampRig.js';
import { OpenLink } from '../Shared/CabinLink.js';

// Powertrain and cabin link (see POWERTRAIN AND CABIN LINK below): declared first so every section can read them.
const Train = new Powertrain({ Ratios: [4.2, 2.6, 1.8, 1.36, 1.06, 0.86, 0.7], Final: 3.73, Redline: 6800, Limit: 7300, Mass: 2100, PeakTorque: 620 });
const Link  = OpenLink('Contour');
let Motion = Train, LampSent = 0;                                   // Motion: the live drive readings (link or own)

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const DisplayWidth  = 1600, DisplayHeight = 900;
const ViewWidth     = 1080, ViewHeight    = 764;
const Amethyst         = new THREE.Color('#a970ff');
const Verdant       = new THREE.Color('#42f5a1');

const DriveModes = [
    { Name: 'Trail', Lead: 'Balanced throttle, open differentials.',            Throttle: 60, Traction: 55, Stability: 80, Crawl: 20, Pressure: 2.1, Ride: 0,     Locks: [false, false], Transfer: 'High' },
    { Name: 'Rock',  Lead: 'Soft throttle, both axles locked, body lifted.',    Throttle: 25, Traction: 92, Stability: 40, Crawl: 95, Pressure: 1.6, Ride: 0.08,  Locks: [true, true],   Transfer: 'Low'  },
    { Name: 'Sand',  Lead: 'Eager throttle, aired down, rear axle locked.',     Throttle: 88, Traction: 70, Stability: 30, Crawl: 40, Pressure: 1.2, Ride: 0,     Locks: [false, true],  Transfer: 'High' },
    { Name: 'Mud',   Lead: 'Wheel-speed tolerant, rear locked, body lifted.',   Throttle: 72, Traction: 86, Stability: 35, Crawl: 60, Pressure: 1.6, Ride: 0.08,  Locks: [false, true],  Transfer: 'Low'  },
    { Name: 'Snow',  Lead: 'Gentle throttle map, early stability correction.',  Throttle: 34, Traction: 66, Stability: 92, Crawl: 30, Pressure: 2.0, Ride: 0,     Locks: [false, false], Transfer: 'High' },
];

const CameraPresets = {
    Terrain: { Target: [0, 0.8, 0],  Radius: 10.5, Yaw: 0.95,         Pitch: 0.26 },
    Tyres:   { Target: [0, 0.3, 0],  Radius: 15.5, Yaw: -Math.PI / 2, Pitch: 1.43 },
    Tilt:    { Target: [0, 0.9, 0],  Radius: 9.5,  Yaw: Math.PI / 2 - 0.35, Pitch: 0.1 },
    Sensors: { Target: [0, 0.2, 0],  Radius: 19,   Yaw: -Math.PI / 2, Pitch: 1.45 },
    Vehicle: { Target: [0, 0.85, 0], Radius: 9.2,  Yaw: 2.35,         Pitch: 0.18 },
    Lights:  { Target: [2.6, 0.5, 0], Radius: 12.5, Yaw: 0.78,         Pitch: 0.24 },
};

const MenuTitles = { Terrain: 'Terrain Response', Tyres: 'Tyre Pressure', Tilt: 'Articulation', Sensors: 'Proximity Field', Vehicle: 'Vehicle', Lights: 'Trail Lamps' };
const WheelNames = ['Front left', 'Front right', 'Rear left', 'Rear right'];

//------------------------------------------------------------------------------------------------------------------------
//                                                    DOCUMENT
//------------------------------------------------------------------------------------------------------------------------

const Display  = document.getElementById('Display');
const Viewport = document.getElementById('Viewport');
const Find     = (Id) => document.getElementById(Id);
FitDisplay(Display, DisplayWidth, DisplayHeight);
BindRipples(Display);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONDITION
//------------------------------------------------------------------------------------------------------------------------

const Condition = {
    Menu:       'Terrain',
    Mode:       0,
    Locks:      [false, false],
    Ride:       0,              // [m] - body offset goal
    RideNow:    0,              // [m] - eased body offset
    Pressures:  [2.1, 2.1, 2.1, 2.1],
    Picked:     0,
    Pitch:      0, Roll: 0,     // [°] - goal pose
    PitchNow:   0, RollNow: 0,  // [°] - eased pose
    Replay:     false,
    Gear:       'P',
    Transfer:   'High',
    Speed:      0,              // [km/h]
    Heading:    42,             // [°]
    Skin:       'solid',
    FanMode:    'near',
    Underbody:  false,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDERER AND SCENE
//------------------------------------------------------------------------------------------------------------------------

const Canvas   = Find('Scene');
const Renderer = new THREE.WebGLRenderer({ canvas: Canvas, antialias: true });
Renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
Renderer.setSize(ViewWidth, ViewHeight, false);
Renderer.toneMapping         = THREE.ACESFilmicToneMapping;
Renderer.toneMappingExposure = 1.05;
Renderer.outputColorSpace    = THREE.SRGBColorSpace;

const Scene  = new THREE.Scene();
Scene.background = new THREE.Color('#040309');
Scene.fog        = new THREE.Fog('#040309', 18, 42);
Scene.environment = PanelEnvironment(Renderer, [
    { Colour: '#e2d6ff', Intensity: 2.2, Position: [0, 8, 0],   Size: [10, 6] },
    { Colour: '#b47bff', Intensity: 0.9, Position: [-6, 1.2, 4],  Size: [0.5, 2] },
    { Colour: '#42f5a1', Intensity: 1.6, Position: [6, 2, -4],  Size: [1.2, 6] },
    { Colour: '#ffffff', Intensity: 0.8, Position: [0, 2, -8],  Size: [12, 3] },
], '#07060b');

const Camera = new THREE.PerspectiveCamera(34, ViewWidth / ViewHeight, 0.1, 120);
const Orbit  = new CameraOrbit(Camera, Canvas, { ...CameraPresets.Terrain, RadiusLimits: [6, 20], PitchLimits: [0.02, 1.48] });

Scene.add(new THREE.HemisphereLight('#cfc2ff', '#06040a', 0.55));
const KeyLight = new THREE.DirectionalLight('#ffffff', 2.2);
KeyLight.position.set(5, 9, 4);
Scene.add(KeyLight);
const FillLight = new THREE.DirectionalLight('#b9a8ff', 1.1);
FillLight.position.set(6, 3, -7);
Scene.add(FillLight);
const RimLight = new THREE.DirectionalLight('#c9a6ff', 0.9);
RimLight.position.set(-7, 4, 2);
Scene.add(RimLight);
Scene.environmentIntensity = 1.5;

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOPOGRAPHIC TERRAIN
//------------------------------------------------------------------------------------------------------------------------

function Lattice(X, Z)
{
    const Hash = (I, J) => { const S = Math.sin(I * 127.1 + J * 311.7) * 43758.5453; return S - Math.floor(S); };
    const I = Math.floor(X), J = Math.floor(Z), Fx = X - I, Fz = Z - J;
    const Ux = Fx * Fx * (3 - 2 * Fx), Uz = Fz * Fz * (3 - 2 * Fz);
    return THREE.MathUtils.lerp(THREE.MathUtils.lerp(Hash(I, J), Hash(I + 1, J), Ux), THREE.MathUtils.lerp(Hash(I, J + 1), Hash(I + 1, J + 1), Ux), Uz);
}

function TerrainHeight(X, Z)
{
    let Sum = 0, Amplitude = 1, Frequency = 0.09;
    for (let Octave = 0; Octave < 4; Octave++) { Sum += Lattice(X * Frequency, Z * Frequency) * Amplitude; Amplitude *= 0.5; Frequency *= 2.1; }
    const Pad = THREE.MathUtils.smoothstep(Math.hypot(X * 0.75, Z), 5.5, 14);
    return (Sum - 0.9) * 3.4 * Pad;
}

const TerrainSurface = new THREE.PlaneGeometry(90, 90, 220, 220);
TerrainSurface.rotateX(-Math.PI / 2);
{
    const Position = TerrainSurface.attributes.position;
    for (let Index = 0; Index < Position.count; Index++) Position.setY(Index, TerrainHeight(Position.getX(Index), Position.getZ(Index)));
}

const TerrainUniforms = { Clock: { value: 0 }, Accent: { value: Amethyst.clone() } };
const Terrain = new THREE.Mesh(TerrainSurface, new THREE.ShaderMaterial({
    uniforms: TerrainUniforms,
    vertexShader: /* glsl */`
        varying vec3 WorldPoint;
        void main() { WorldPoint = (modelMatrix * vec4(position, 1.0)).xyz; gl_Position = projectionMatrix * viewMatrix * vec4(WorldPoint, 1.0); }`,
    fragmentShader: /* glsl */`
        uniform float Clock; uniform vec3 Accent; varying vec3 WorldPoint;
        float Band(float Value, float Width) { float W = max(fwidth(Value) * Width, 1e-4); float F = fract(Value); return 1.0 - smoothstep(0.0, W, min(F, 1.0 - F)); }
        void main() {
            float Radial  = length(WorldPoint.xz);
            float Fade    = 1.0 - smoothstep(8.0, 34.0, Radial);
            float Lifted  = WorldPoint.y + 0.1;
            float Minor   = Band(Lifted * 5.0, 1.2);
            float Major   = Band(Lifted * 1.0, 1.6);
            float Rings   = Band(Radial * 1.0, 1.0) * (1.0 - smoothstep(3.5, 6.0, Radial)) * step(3.2, Radial);
            float Sweep   = fract(Clock * 0.18);
            float Scan    = smoothstep(0.35, 0.0, abs(Radial - Sweep * 26.0)) * (1.0 - Sweep);
            vec3 Colour   = vec3(0.010, 0.012, 0.014);
            Colour += vec3(0.13, 0.11, 0.17) * Minor * Fade;
            Colour += vec3(0.33, 0.29, 0.42) * Major * Fade;
            Colour += vec3(0.12, 0.10, 0.15) * Rings;
            Colour += Accent * Scan * 0.55 * (0.35 + Minor);
            gl_FragColor = vec4(Colour, 1.0);
        }`,
}));
Scene.add(Terrain);

// Soft contact shadow under the car.
function ShadowSprite(Width, Depth, Opacity = 0.8)
{
    const Paint  = document.createElement('canvas');
    Paint.width  = Paint.height = 128;
    const Pen    = Paint.getContext('2d');
    const Fall   = Pen.createRadialGradient(64, 64, 4, 64, 64, 64);
    Fall.addColorStop(0, `rgba(0,0,0,${Opacity})`);
    Fall.addColorStop(1, 'rgba(0,0,0,0)');
    Pen.fillStyle = Fall;
    Pen.fillRect(0, 0, 128, 128);
    const Plane  = new THREE.Mesh(new THREE.PlaneGeometry(Width, Depth), new THREE.MeshBasicMaterial({ map: new THREE.CanvasTexture(Paint), transparent: true, depthWrite: false }));
    Plane.rotation.x = -Math.PI / 2;
    Plane.position.y = 0.01;
    return Plane;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE
//------------------------------------------------------------------------------------------------------------------------

const Finish = DefaultFinish({
    [Family.BodyCoat]: { color: '#4a4f55', roughness: 0.58, metalness: 0.25, clearcoat: 0.3, clearcoatRoughness: 0.35 },
    [Family.Headlight]: { color: '#2a3036', emissive: '#a9c4e0' },
    [Family.Glass]:    { color: '#05070a', roughness: 0.08 },
    [Family.Hub]:      { color: '#a970ff', roughness: 0.3, metalness: 0.7, emissive: '#8a3dff', emissiveIntensity: 0.25 },
    [Family.Trim]:     { color: '#1a1c1f', roughness: 0.6, metalness: 0.3 },
    [Family.Brake]:    { color: '#a970ff', roughness: 0.4, metalness: 0.6 },
});
const Vehicle = await AssembleVehicle({ Finish });
const Carriage = new THREE.Group();                               // receives pitch / roll
Carriage.add(Vehicle.Root);
Scene.add(Carriage);
Carriage.add(ShadowSprite(7.4, 3.6));

// Slope plate: a faint disc under the car that tilts with the carriage so the pose reads against the level terrain.
const SlopePlate = new THREE.Mesh(new THREE.RingGeometry(3.9, 3.96, 128), new THREE.MeshBasicMaterial({ color: '#42f5a1', transparent: true, opacity: 0.55, side: THREE.DoubleSide, depthWrite: false }));
SlopePlate.rotation.x = -Math.PI / 2;
SlopePlate.position.y = 0.04;
Carriage.add(SlopePlate);
const BaseBodyHeight = Vehicle.BodyMount.position.y;

// X-ray skin: translucent shells plus feature edges (body) and tread edges (tyres), as in technical cutaways.
const XrayShell = new THREE.MeshBasicMaterial({ color: '#42f5a1', transparent: true, opacity: 0.07, depthWrite: false, blending: THREE.AdditiveBlending });
const XrayTyre  = new THREE.MeshBasicMaterial({ color: '#dfe7ee', transparent: true, opacity: 0.05, depthWrite: false, blending: THREE.AdditiveBlending });
const SkinParts = [];
function AttachEdges(Mesh, Colour, Threshold, Opacity)
{
    const Edges = new THREE.LineSegments(new THREE.EdgesGeometry(Mesh.geometry, Threshold), new THREE.LineBasicMaterial({ color: Colour, transparent: true, opacity: Opacity, depthWrite: false, blending: THREE.AdditiveBlending }));
    Edges.visible = false;
    Mesh.add(Edges);
    SkinParts.push({ Mesh, Solid: Mesh.material, Shell: Array.isArray(Mesh.material) ? Mesh.material.map(() => (Colour === '#42f5a1' ? XrayShell : XrayTyre)) : XrayShell, Edges });
}
AttachEdges(Vehicle.Body, '#42f5a1', 24, 0.75);
for (const Wheel of Vehicle.Wheels) for (const Part of Wheel.Spin.children[0].children) AttachEdges(Part, '#e8eef3', 28, 0.55);

// Axle bars light up when their locker engages.
const AxleBars = [0, 2].map((First) =>
{
    const Left = Vehicle.Wheels[First].Mount, Right = Vehicle.Wheels[First + 1].Mount;
    const Bar  = new THREE.Mesh(new THREE.CylinderGeometry(0.045, 0.045, Math.abs(Left.z - Right.z) - 0.3, 18), new THREE.MeshStandardMaterial({ color: '#22252a', roughness: 0.4, metalness: 0.8, emissive: '#000000' }));
    Bar.rotation.x = Math.PI / 2;
    Bar.position.set(Left.x, Left.y, 0);
    Vehicle.Root.add(Bar);
    return Bar;
});

// Exterior lamps (shared rig) plus Contour's own roof light bar; the underbody glow stays a Contour sprite.
const Lamps = MountLamps(Vehicle, { Extras: { RoofBar: true }, Signature: '#eef3ff' });
Lamps.HeadBase = 0.05;
const UnderGlow = ShadowSprite(7.8, 4.2, 1);
UnderGlow.material.color.set('#a970ff');
UnderGlow.material.blending = THREE.AdditiveBlending;
UnderGlow.material.opacity  = 0;
UnderGlow.position.y = 0.02;
Carriage.add(UnderGlow);

//------------------------------------------------------------------------------------------------------------------------
//                                                    PROXIMITY FANS
//------------------------------------------------------------------------------------------------------------------------

const FanBars    = 34;
const FanCorners = [[2.75, -1.0], [2.75, 1.0], [-2.85, -1.0], [-2.85, 1.0]];
const FanTexture = (() =>
{
    const Paint = document.createElement('canvas');
    Paint.width = 256; Paint.height = 4;
    const Pen   = Paint.getContext('2d');
    const Fall  = Pen.createLinearGradient(0, 0, 256, 0);
    Fall.addColorStop(0, 'rgba(190,150,255,0.0)');
    Fall.addColorStop(0.2, 'rgba(169,112,255,0.75)');
    Fall.addColorStop(1, 'rgba(120,60,255,0.0)');
    Pen.fillStyle = Fall;
    Pen.fillRect(0, 0, 256, 4);
    return new THREE.CanvasTexture(Paint);
})();
const FanBlade = new THREE.PlaneGeometry(1, 0.07).translate(0.5, 0, 0).rotateX(-Math.PI / 2);
const Fans     = new THREE.InstancedMesh(FanBlade, new THREE.MeshBasicMaterial({ map: FanTexture, transparent: true, depthWrite: false, side: THREE.DoubleSide }), FanBars * 4);
Fans.visible = false;
Fans.frustumCulled = false;
Carriage.add(Fans);
const QuarterDistance = [1.8, 1.8, 1.8, 1.8];
const Placement = new THREE.Object3D();

function AdvanceFans(Elapsed)
{
    const Wide = Condition.FanMode === 'wide';
    FanCorners.forEach(([X, Side], Quarter) =>
    {
        // 📝 A slow synthetic obstacle per quarter; the fan grows as it closes in (no real sensor data here).
        const Distance = 1.35 + Math.sin(Elapsed * (0.35 + Quarter * 0.11) + Quarter * 1.7) * 0.95;
        QuarterDistance[Quarter] = Distance;
        const Spread  = Wide ? 1.9 : 1.25;
        const Outward = Math.atan2(-Side * 0.7, Math.sign(X));   // [rad] - rotation y maps +X to (cos, −sin)
        for (let Bar = 0; Bar < FanBars; Bar++)
        {
            const Angle   = Outward + (Bar / (FanBars - 1) - 0.5) * Spread;
            const Spike   = 0.55 + 0.45 * Math.abs(Math.sin(Bar * 1.93 + Elapsed * 3.1 + Quarter)) * Lattice(Bar * 0.7, Elapsed * 1.6 + Quarter * 9);
            const Reach   = Math.min(3.2, (1.9 / Distance) * Spike * (Wide ? 1.3 : 1));
            Placement.position.set(X + Math.cos(Angle) * 0.3, 0.03 + Bar * 0.0004, Side * 0.95 - Math.sin(Angle) * 0.3);
            Placement.rotation.set(0, Angle, 0);
            Placement.scale.set(Reach, 1, 1);
            Placement.updateMatrix();
            Fans.setMatrixAt(Quarter * FanBars + Bar, Placement.matrix);
        }
    });
    Fans.instanceMatrix.needsUpdate = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CALLOUTS
//------------------------------------------------------------------------------------------------------------------------

const CalloutLayer = Find('Callouts');
const Callouts     = [];
function PinCallout(Html, ClassName, Anchor, Menus)
{
    const Element = document.createElement('div');
    Element.className = `Callout ${ClassName}`;
    Element.innerHTML = Html;
    CalloutLayer.append(Element);
    const Entry = { Element, Anchor, Menus: new Set(Menus) };
    Callouts.push(Entry);
    return Entry;
}

const LockSvg = '<svg viewBox="0 0 24 24"><rect x="5" y="11" width="14" height="10" rx="2"/><path class="Shackle" d="M8 11V8a4 4 0 0 1 8 0v3"/></svg>';
const LockBadges = AxleBars.map((Bar) => PinCallout(`<div class="LockBadge">${LockSvg}</div>`, 'Lifted', () => Carriage.localToWorld(Bar.position.clone().setY(2.35)), ['Terrain']));

const TyreTags = Vehicle.Wheels.map((Wheel, Index) =>
{
    const Entry = PinCallout(`<div class="TyreTag"><b>2.10</b><small>BAR · 38°C</small></div>`, '', () => Wheel.Steer.getWorldPosition(new THREE.Vector3()).add(new THREE.Vector3(0, 0, Wheel.Left ? -1.25 : 1.25)), ['Tyres']);
    Entry.Element.firstChild.addEventListener('click', () => PickWheel(Index));
    return Entry;
});

const PitchTag = PinCallout('<div class="AngleTag"><small>PITCH</small><span>0°</span></div>', '', () => Vehicle.Body.localToWorld(new THREE.Vector3(3.4, 1.1, 0)), ['Tilt']);
const RollTag  = PinCallout('<div class="AngleTag Cool"><small>ROLL</small><span>0°</span></div>', '', () => Vehicle.Body.localToWorld(new THREE.Vector3(0.2, 1.75, 1.3)), ['Tilt']);
const RangeTags = FanCorners.map(([X, Side], Quarter) => PinCallout('<div class="RangeTag"><b>1.8</b>m</div>', '', () => Carriage.localToWorld(new THREE.Vector3(X + Math.sign(X) * 1.4, 0.1, Side * 2.6)), ['Sensors']));

function PlaceCallouts()
{
    for (const Entry of Callouts)
    {
        const Live = Entry.Menus.has(Condition.Menu) && !(Entry.Element.querySelector('.LockBadge') && Condition.Menu === 'Tyres');
        const Spot = ProjectAnchor(Camera, Entry.Anchor(), ViewWidth, ViewHeight);
        Entry.Element.classList.toggle('Hidden', !Live || !Spot.Visible || (Condition.Menu === 'Sensors' && Condition.FanMode === 'off'));
        Entry.Element.style.left = `${Spot.X}px`;
        Entry.Element.style.top  = `${Spot.Y}px`;
    }
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
    ToastTimer = setTimeout(() => Toast.classList.remove('Show'), 2600);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MENUS
//------------------------------------------------------------------------------------------------------------------------

function OpenMenu(Menu)
{
    if (Menu === Condition.Menu) return;
    Condition.Menu = Menu;
    document.querySelectorAll('#MenuRail [data-menu]').forEach((Button) => Button.classList.toggle('On', Button.dataset.menu === Menu));
    document.querySelectorAll('.Panel').forEach((Panel) => Panel.classList.toggle('On', Panel.dataset.panel === Menu));
    const Title = Find('ViewportTitle');
    Title.classList.add('Swap');
    setTimeout(() => { Title.textContent = MenuTitles[Menu]; Title.classList.remove('Swap'); }, 200);
    Orbit.Aim(CameraPresets[Menu]);
    Orbit.Drift = Menu === 'Vehicle' ? 0.12 : 0;
    Display.classList.toggle('LightsOn', Menu === 'Lights');
    Fans.visible = Menu === 'Sensors' && Condition.FanMode !== 'off';
    SlopePlate.visible = Menu === 'Terrain' || Menu === 'Tilt';
}
document.querySelectorAll('#MenuRail [data-menu]').forEach((Button) => Button.addEventListener('click', () => OpenMenu(Button.dataset.menu)));

// Sliding segment indicator shared by every segmented control.
function SlideSegment(Group, Button)
{
    const Marker = Group.querySelector(':scope > i');
    Group.querySelectorAll('button').forEach((Each) => Each.classList.toggle('On', Each === Button));
    if (!Marker) return;
    Marker.style.width     = `${Button.offsetWidth}px`;
    Marker.style.transform = `translateX(${Button.offsetLeft - 3}px)`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DRIVE MODE DIAL
//------------------------------------------------------------------------------------------------------------------------

const Dial = { Centre: [180, 178], Radius: 132, Angle: 160, Velocity: 0, Goal: 160, Dragging: false };
const ModeAngle = (Index) => 160 - Index * 35;                   // [°] - 160° … 20°, left to right
const Polar = (Angle, Radius) => [Dial.Centre[0] + Math.cos(Angle * Math.PI / 180) * Radius, Dial.Centre[1] - Math.sin(Angle * Math.PI / 180) * Radius];

function DrawDial()
{
    const Face = Find('DialFace');
    const Svg  = (Tag, Attributes) => { const Node = document.createElementNS('http://www.w3.org/2000/svg', Tag); for (const Key in Attributes) Node.setAttribute(Key, Attributes[Key]); return Node; };
    Face.replaceChildren();
    const Defs = Svg('defs', {});
    const Sweep = Svg('linearGradient', { id: 'AmethystSweep', x1: '0', x2: '1', y1: '0', y2: '0' });
    Sweep.append(Svg('stop', { offset: '0', 'stop-color': '#5d2fc2' }), Svg('stop', { offset: '1', 'stop-color': '#c49bff' }));
    Defs.append(Sweep);
    Face.append(Defs);
    for (let Angle = 165; Angle >= 15; Angle -= 5)
    {
        const Major = DriveModes.some((_, Index) => ModeAngle(Index) === Angle);
        const [X1, Y1] = Polar(Angle, Dial.Radius + 14), [X2, Y2] = Polar(Angle, Dial.Radius + (Major ? 26 : 20));
        Face.append(Svg('line', { x1: X1, y1: Y1, x2: X2, y2: Y2, class: `Tick${Major ? ' Major' : ''}` }));
    }
    const [Ax, Ay] = Polar(165, Dial.Radius), [Bx, By] = Polar(15, Dial.Radius);
    Face.append(Svg('path', { d: `M${Ax},${Ay} A${Dial.Radius},${Dial.Radius} 0 0 1 ${Bx},${By}`, class: 'Arc' }));
    Face.append(Svg('path', { id: 'ArcLive', class: 'ArcLive' }));
    DriveModes.forEach((Mode, Index) =>
    {
        const [X, Y] = Polar(ModeAngle(Index), Dial.Radius + 44);
        const Label = Svg('text', { x: X, y: Y + 4, 'text-anchor': 'middle', class: 'ModeLabel', 'data-mode': Index });
        Label.textContent = Mode.Name;
        Label.addEventListener('click', () => SelectMode(Index));
        Face.append(Label);
    });
    Face.append(Svg('line', { id: 'Needle', class: 'Needle' }));
    Face.append(Svg('circle', { id: 'Knob', r: 9, class: 'Knob' }));
}

function PoseDial()
{
    const [Ax, Ay] = Polar(165, Dial.Radius);
    const [Kx, Ky] = Polar(Dial.Angle, Dial.Radius);
    Find('ArcLive').setAttribute('d', `M${Ax},${Ay} A${Dial.Radius},${Dial.Radius} 0 0 1 ${Kx},${Ky}`);
    const [Nx, Ny] = Polar(Dial.Angle, Dial.Radius - 34);
    const [Sx, Sy] = Polar(Dial.Angle, Dial.Radius - 70);
    const Needle = Find('Needle');
    Needle.setAttribute('x1', Sx); Needle.setAttribute('y1', Sy); Needle.setAttribute('x2', Nx); Needle.setAttribute('y2', Ny);
    Find('Knob').setAttribute('cx', Kx); Find('Knob').setAttribute('cy', Ky);
}

// 📝 Spring-damped needle (slight overshoot) for the detent feel.
function AdvanceDial(Delta)
{
    if (Dial.Dragging) return;
    const Stiffness = 170, Damping = 15;
    Dial.Velocity += ((Dial.Goal - Dial.Angle) * Stiffness - Dial.Velocity * Damping) * Delta;
    Dial.Angle    += Dial.Velocity * Delta;
    PoseDial();
}

function BindDial()
{
    const Host = Find('ModeDial');
    const AngleAt = (Event) =>
    {
        const Box = Host.getBoundingClientRect(), Scale = Box.width / 360;
        const X = (Event.clientX - Box.left) / Scale - Dial.Centre[0], Y = Dial.Centre[1] - (Event.clientY - Box.top) / Scale;
        return THREE.MathUtils.clamp(Math.atan2(Math.max(Y, -20), X) * 180 / Math.PI, 20, 160);
    };
    Host.addEventListener('pointerdown', (Press) =>
    {
        if (Press.target.classList.contains('ModeLabel')) return;
        Dial.Dragging = true;
        Host.setPointerCapture(Press.pointerId);
        Dial.Angle = AngleAt(Press);
        PoseDial();
    });
    Host.addEventListener('pointermove', (Motion) => { if (Dial.Dragging) { Dial.Angle = AngleAt(Motion); PoseDial(); } });
    Host.addEventListener('pointerup', () =>
    {
        if (!Dial.Dragging) return;
        Dial.Dragging = false;
        Dial.Velocity = 0;
        SelectMode(Math.round((160 - Dial.Angle) / 35));
    });
}

function SelectMode(Index, Quiet = false)
{
    const Mode = DriveModes[Index];
    Condition.Mode = Index;
    Dial.Goal = ModeAngle(Index);
    document.querySelectorAll('.ModeLabel').forEach((Label) => Label.classList.toggle('On', +Label.dataset.mode === Index));
    const Name = Find('ModeName');
    Name.style.opacity = 0;
    setTimeout(() => { Name.textContent = Mode.Name; Name.style.opacity = 1; }, 160);
    Find('ModeIndex').textContent = `0${Index + 1} / 05`;
    Find('ModeLead').textContent  = Mode.Lead;
    Find('ModeMeters').innerHTML  = [['Throttle map', Mode.Throttle], ['Traction', Mode.Traction], ['Stability', Mode.Stability], ['Crawl control', Mode.Crawl]]
        .map(([Label, Value]) => `<div class="Meter"><span>${Label}</span><div class="Bar"><i style="width:0%" data-fill="${Value}"></i></div><b>${Value}</b></div>`).join('');
    requestAnimationFrame(() => document.querySelectorAll('#ModeMeters .Bar i').forEach((Fill) => { Fill.style.width = `${Fill.dataset.fill}%`; }));
    Mode.Locks.forEach((Locked, Axle) => SetLock(Axle, Locked, true));
    SetRide(Mode.Ride, true);
    SetTransfer(Mode.Transfer, true);
    AirTo(Mode.Pressure, true);
    EaseColour(TerrainUniforms.Accent.value, Index === 4 ? Verdant : Amethyst, 500);
    RefreshStripNote();
    if (!Quiet) Announce(`<b>${Mode.Name}</b> · ${Mode.Locks.filter(Boolean).length} lock${Mode.Locks.filter(Boolean).length === 1 ? '' : 's'} · ${Mode.Pressure.toFixed(1)} bar · ${Mode.Transfer === 'Low' ? '4L' : '4H'}`);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOCKS, RIDE, TRANSFER
//------------------------------------------------------------------------------------------------------------------------

function SetLock(Axle, Locked, Quiet = false)
{
    Condition.Locks[Axle] = Locked;
    const Tile = document.querySelector(`[data-lock="${Axle ? 'Rear' : 'Front'}"]`);
    Tile.classList.toggle('On', Locked);
    Tile.querySelector('small').textContent = Locked ? 'Locked' : 'Open';
    LockBadges[Axle].Element.firstChild.classList.toggle('Locked', Locked);
    const Glow = AxleBars[Axle].material.emissive;
    EaseColour(Glow, Locked ? '#9a5cff' : '#000000', 360);
    if (!Quiet) { Announce(`${Axle ? 'Rear' : 'Front'} axle <b>${Locked ? 'locked' : 'open'}</b>`); RefreshStripNote(); }
}
document.querySelectorAll('[data-lock]').forEach((Tile) => Tile.addEventListener('click', () => { const Axle = Tile.dataset.lock === 'Rear' ? 1 : 0; SetLock(Axle, !Condition.Locks[Axle]); }));

function SetRide(Offset, Quiet = false)
{
    Condition.Ride = Offset;
    const Group = Find('RideHeight');
    SlideSegment(Group, Group.querySelector(`[data-ride="${Offset}"]`) || Group.querySelector('[data-ride="0"]'));
    if (!Quiet) Announce(`Ride height <b>${Offset > 0 ? 'lift +80 mm' : Offset < 0 ? 'crawl −50 mm' : 'normal'}</b>`);
}
Find('RideHeight').addEventListener('click', (Press) => { const Button = Press.target.closest('button'); if (Button) SetRide(+Button.dataset.ride); });

function SetTransfer(Range, Quiet = false)
{
    Condition.Transfer = Range;
    const Switch = Find('TransferSwitch');
    Switch.classList.toggle('Low', Range === 'Low');
    Switch.children[0].classList.toggle('On', Range === 'High');
    Switch.children[1].classList.toggle('On', Range === 'Low');
    Find('TransferText').textContent = Range === 'Low' ? '4L · locked centre' : '4H · auto';
    if (!Quiet) { Announce(`Transfer case <b>${Range === 'Low' ? '4L' : '4H'}</b>`); RefreshStripNote(); }
}
Find('TransferSwitch').addEventListener('click', () => SetTransfer(Condition.Transfer === 'Low' ? 'High' : 'Low'));

function RefreshStripNote()
{
    const Locks = Condition.Locks[0] && Condition.Locks[1] ? 'both locked' : Condition.Locks[1] ? 'rear locked' : Condition.Locks[0] ? 'front locked' : 'open diffs';
    const Mean  = Condition.Pressures.reduce((Sum, Value) => Sum + Value, 0) / 4;
    Find('StripNote').textContent = `${DriveModes[Condition.Mode].Name} · ${Locks} · ${Mean.toFixed(1)} bar`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYRES
//------------------------------------------------------------------------------------------------------------------------

function BuildTyreGrid()
{
    Find('TyreGrid').innerHTML = WheelNames.map((Name, Index) => `<div class="TyreCell" data-wheel="${Index}"><small>${Name}</small><b>2.10</b><em>bar</em><span class="Temp">${36 + Index}°C</span></div>`).join('');
    document.querySelectorAll('.TyreCell').forEach((Cell) => Cell.addEventListener('click', () => PickWheel(+Cell.dataset.wheel)));
}

function RefreshTyres()
{
    Condition.Pressures.forEach((Pressure, Index) =>
    {
        const Low  = Pressure < 1.75;
        const Cell = document.querySelector(`.TyreCell[data-wheel="${Index}"]`);
        Cell.classList.toggle('Low', Low);
        Cell.classList.toggle('Picked', Index === Condition.Picked);
        RollNumber(Cell.querySelector('b'), Pressure, { Format: (Figure) => Figure.toFixed(2) });
        const Tag = TyreTags[Index].Element.firstChild;
        Tag.classList.toggle('Low', Low);
        Tag.classList.toggle('Picked', Index === Condition.Picked);
        RollNumber(Tag.querySelector('b'), Pressure, { Format: (Figure) => Figure.toFixed(2) });
    });
    RollNumber(Find('StepValue'), Condition.Pressures[Condition.Picked], { Format: (Figure) => Figure.toFixed(2) });
    Find('StepWheel').textContent = `${WheelNames[Condition.Picked]} · bar`;
    RefreshStripNote();
}

function PickWheel(Index) { Condition.Picked = Index; RefreshTyres(); }

function AirTo(Pressure, Quiet = false)
{
    Condition.Pressures = Condition.Pressures.map(() => Pressure);
    RefreshTyres();
    if (!Quiet) Announce(`All tyres to <b>${Pressure.toFixed(1)} bar</b>`);
}
document.querySelectorAll('[data-air]').forEach((Button) => Button.addEventListener('click', () => AirTo(+Button.dataset.air)));
document.querySelectorAll('[data-step]').forEach((Button) => Button.addEventListener('click', () =>
{
    Condition.Pressures[Condition.Picked] = THREE.MathUtils.clamp(Math.round((Condition.Pressures[Condition.Picked] + +Button.dataset.step) * 10) / 10, 0.8, 3.0);
    RefreshTyres();
}));

//------------------------------------------------------------------------------------------------------------------------
//                                                    TILT PAD
//------------------------------------------------------------------------------------------------------------------------

const TiltLimit = 30;                                              // [°] - pad edge
function SetPose(Pitch, Roll)
{
    Condition.Pitch = THREE.MathUtils.clamp(Pitch, -TiltLimit, TiltLimit);
    Condition.Roll  = THREE.MathUtils.clamp(Roll, -TiltLimit, TiltLimit);
}

function BindTiltPad()
{
    const Pad = Find('TiltPad');
    let Active = false;
    const Move = (Event) =>
    {
        const Box = Pad.getBoundingClientRect(), Scale = Box.width / 300;
        let X = (Event.clientX - Box.left) / Scale - 150, Y = (Event.clientY - Box.top) / Scale - 150;
        const Reach = Math.hypot(X, Y);
        if (Reach > 140) { X *= 140 / Reach; Y *= 140 / Reach; }
        SetPose(-Y / 140 * TiltLimit, X / 140 * TiltLimit);
    };
    Pad.addEventListener('pointerdown', (Press) => { Active = true; Condition.Replay = false; RefreshReplay(); Pad.classList.add('Dragging'); Pad.setPointerCapture(Press.pointerId); Move(Press); });
    Pad.addEventListener('pointermove', (Motion) => { if (Active) Move(Motion); });
    Pad.addEventListener('pointerup', () => { Active = false; Pad.classList.remove('Dragging'); });
}

function RefreshReplay()
{
    const Button = Find('TrailReplay');
    Button.classList.toggle('Running', Condition.Replay);
    Button.textContent = Condition.Replay ? 'Stop replay' : 'Replay trail';
}
Find('TrailReplay').addEventListener('click', () => { Condition.Replay = !Condition.Replay; if (!Condition.Replay) SetPose(0, 0); RefreshReplay(); });
Find('LevelOut').addEventListener('click', () => { Condition.Replay = false; RefreshReplay(); SetPose(0, 0); });

function RefreshPose()
{
    const Pitch = Condition.PitchNow, Roll = Condition.RollNow;
    Find('Puck').style.transform = `translate(${Roll / TiltLimit * 140}px, ${-Pitch / TiltLimit * 140}px)`;
    Find('PitchText').textContent = Pitch.toFixed(0);
    Find('RollText').textContent  = Roll.toFixed(0);
    Find('PitchWarn').style.setProperty('--Fill', `${Math.min(100, Math.abs(Pitch) / TiltLimit * 100)}%`);
    Find('RollWarn').style.setProperty('--Fill', `${Math.min(100, Math.abs(Roll) / TiltLimit * 100)}%`);
    PitchTag.Element.querySelector('span').textContent = `${Math.abs(Pitch).toFixed(0)}°`;
    RollTag.Element.querySelector('span').textContent  = `${Math.abs(Roll).toFixed(0)}°`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SENSORS, LIGHTING, SKIN, GEARS
//------------------------------------------------------------------------------------------------------------------------

function BuildQuarters()
{
    Find('QuarterGrid').innerHTML = ['Front left', 'Front right', 'Rear left', 'Rear right'].map((Name, Index) =>
        `<div class="Quarter" data-quarter="${Index}"><small>${Name}</small><b>1.8 m</b><div class="Ticks">${'<i></i>'.repeat(8)}</div></div>`).join('');
}

function RefreshQuarters()
{
    QuarterDistance.forEach((Distance, Index) =>
    {
        const Cell = document.querySelector(`.Quarter[data-quarter="${Index}"]`);
        const Off  = Condition.FanMode === 'off';
        Cell.querySelector('b').textContent = Off ? '—' : `${Distance.toFixed(1)} m`;
        const Lit = Off ? 0 : Math.round(THREE.MathUtils.clamp((2.6 - Distance) / 2.2, 0, 1) * 8);
        Cell.querySelectorAll('.Ticks i').forEach((Tick, Slot) => Tick.classList.toggle('Lit', Slot < Lit));
        RangeTags[Index].Element.firstChild.innerHTML = `<b>${Distance.toFixed(1)}</b>m`;
    });
}

Find('FanMode').addEventListener('click', (Press) =>
{
    const Button = Press.target.closest('button');
    if (!Button) return;
    Condition.FanMode = Button.dataset.fan;
    SlideSegment(Find('FanMode'), Button);
    Fans.visible = Condition.Menu === 'Sensors' && Condition.FanMode !== 'off';
});

const LampNotes = { Off: 'Daytime lamps only', Auto: 'Auto · dips for traffic', Parking: 'Parking lamps', Low: 'Low beam · 62 m reach', High: 'High beam · 180 m reach' };
const LampPanel = BindLampControls(Find('LightsPanel'), Lamps, {
    Custom: { Underbody: { Get: () => Condition.Underbody, Set: (On) => { Condition.Underbody = On; } } },
    OnChange: (Button) =>
    {
        const Name = Button.querySelector('b')?.textContent || Button.textContent.trim();
        if (Button.dataset.beam) Announce(`Headlamps <b>${LampNotes[Button.dataset.beam]}</b>`);
        else Announce(`${Name} <b>${Button.classList.contains('On') ? 'on' : 'off'}</b>`);
    },
});
BindLampControls(Find('TelltaleRow'), Lamps);
document.querySelectorAll('[data-goto]').forEach((Button) => Button.addEventListener('click', () => OpenMenu(Button.dataset.goto)));

function SetLamps(Patch) { Lamps.Set(Patch); LampPanel.Refresh(); }

function RefreshLampMap()
{
    const Beams = Lamps.Beams(), State = Lamps.State;
    const Live = {
        Low: Beams.Low || Beams.Parking, FogFront: State.FogFront, RoofBar: State.RoofBar, Tail: Beams.Parking || Motion.Brake > 0.1,
        FogRear: State.FogRear, Reverse: Motion.Selector === 'R', Underbody: Condition.Underbody,
        Left: (State.Signal === 'Left' || State.Signal === 'Hazard') && Lamps.Blink, Right: (State.Signal === 'Right' || State.Signal === 'Hazard') && Lamps.Blink,
    };
    Find('LampMap').querySelectorAll('[data-spot]').forEach((Spot) => Spot.classList.toggle('Lit', !!Live[Spot.dataset.spot]));
    const Note = Beams.High ? (State.Beam === 'High' ? LampNotes.High : 'Auto high · 180 m reach') : Lamps.Oncoming && State.AutoHigh ? 'Dipped · oncoming traffic' : LampNotes[State.Beam];
    if (Find('LampNote').textContent !== Note) Find('LampNote').textContent = Note;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    POWERTRAIN AND CABIN LINK
//------------------------------------------------------------------------------------------------------------------------

// 📝 The instrument cluster lives on the dashboard (Clusters/Ridge), not in this HMI. When it is open the link feeds
//    speed, gear, pedals and g forces here and this HMI sends its lamp switches back; alone, the car stands parked.

// The footer selector follows whichever powertrain is live.
let ShownSelector = '';
function RefreshSelector()
{
    if (Motion.Selector === ShownSelector) return;
    ShownSelector = Motion.Selector;
    const Row = Find('GearRow'), Button = Row.querySelector(`[data-gear="${ShownSelector}"]`);
    Row.querySelectorAll('button').forEach((Each) => Each.classList.toggle('On', Each === Button));
    Row.querySelector('i').style.transform = `translateX(${Button.offsetLeft}px)`;
    Condition.Gear = ShownSelector;
}

Find('SkinSwitch').addEventListener('click', (Press) =>
{
    const Button = Press.target.closest('button');
    if (!Button) return;
    Condition.Skin = Button.dataset.skin;
    const Switch = Find('SkinSwitch');
    Switch.querySelectorAll('button').forEach((Each) => Each.classList.toggle('On', Each === Button));
    Switch.querySelector('i').style.transform = `translateX(${Condition.Skin === 'xray' ? 70 : 0}px)`;
    const Xray = Condition.Skin === 'xray';
    for (const Part of SkinParts) { Part.Mesh.material = Xray ? Part.Shell : Part.Solid; Part.Edges.visible = Xray; }
});

Find('GearRow').addEventListener('click', (Press) =>
{
    const Button = Press.target.closest('button');
    if (!Button) return;
    Train.SetSelector(Button.dataset.gear);
    Link.Send('Selector', { Selector: Button.dataset.gear });
    if ((Button.dataset.gear === 'P' || Button.dataset.gear === 'R') && Motion.Kmh > 6) Announce(`Slow down to <b>${Button.dataset.gear === 'P' ? 'park' : 'reverse'}</b>`);
});

function BuildHeadingTape()
{
    const Labels = { 0: 'N', 45: 'NE', 90: 'E', 135: 'SE', 180: 'S', 225: 'SW', 270: 'W', 315: 'NW' };
    let Html = '';
    for (let Lap = -1; Lap <= 1; Lap++) for (let Degree = 0; Degree < 360; Degree += 15) Html += `<span>${Labels[Degree] || ''}</span>`;
    Find('TapeTrack').innerHTML = Html;
}

const Compass = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'];
function RefreshHeading()
{
    const Heading = ((Condition.Heading % 360) + 360) % 360;
    Find('TapeTrack').style.transform = `translateX(${210 - (24 + Heading / 15) * 30 - 15}px)`;
    Find('HeadingText').textContent   = `${Compass[Math.round(Heading / 45) % 8]} ${String(Math.round(Heading)).padStart(3, '0')}°`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME STEP
//------------------------------------------------------------------------------------------------------------------------

let ShownSpeed = -1;
function Step(Delta, Elapsed)
{
    Orbit.Advance(Delta);
    AdvanceDial(Delta);
    TerrainUniforms.Clock.value = Elapsed;

    // Body pose: replay sways the carriage over a synthetic trail; otherwise ease to the pad pose.
    if (Condition.Replay) SetPose(Math.sin(Elapsed * 0.9) * 14 + Math.sin(Elapsed * 2.3) * 4, Math.sin(Elapsed * 0.63 + 1) * 12 + Math.sin(Elapsed * 1.7) * 3);
    const Follow = 1 - Math.exp(-Delta * 6);
    Condition.PitchNow += (Condition.Pitch - Condition.PitchNow) * Follow;
    Condition.RollNow  += (Condition.Roll - Condition.RollNow) * Follow;
    Carriage.rotation.set(THREE.MathUtils.degToRad(Condition.RollNow), 0, THREE.MathUtils.degToRad(Condition.PitchNow), 'ZXY');
    RefreshPose();

    Condition.RideNow += (Condition.Ride - Condition.RideNow) * (1 - Math.exp(-Delta * 4));
    Vehicle.BodyMount.position.y = BaseBodyHeight + Condition.RideNow;

    // Wheels roll with the powertrain (or the trail replay); the body squats and leans with the g forces.
    // The Ridge dashboard cluster drives when it is open (cabin link); otherwise the HMI's own powertrain idles.
    Train.Step();
    Motion = Link.Fresh('Drive') || Train;
    RefreshSelector();
    Condition.Speed = Condition.Replay ? Math.max(7, Motion.Kmh) : Motion.Kmh;
    const Spin = (Motion.Velocity < 0 ? 1 : -1) * Condition.Speed / 3.6 / 0.509 * Delta;
    for (const Wheel of Vehicle.Wheels) Wheel.Spin.rotation.z += Spin;
    Vehicle.BodyMount.rotation.z += (THREE.MathUtils.clamp(Motion.GLong, -1, 1) * 0.022 - Vehicle.BodyMount.rotation.z) * Follow;
    Vehicle.BodyMount.rotation.x += (THREE.MathUtils.clamp(Motion.GLat, -1, 1) * 0.03 - Vehicle.BodyMount.rotation.x) * Follow;
    Lamps.State.Brake = Motion.Brake > 0.08 ? 1 : 0;
    Lamps.State.Reverse = Motion.Selector === 'R';
    if (Elapsed - LampSent > 0.4) { LampSent = Elapsed; Link.Send('Lamps', { ...Lamps.State }); }
    Lamps.Speed = Condition.Speed;
    Lamps.Step();
    if (Condition.Menu === 'Lights') SyncTelltales(Find('TelltaleRow'), Lamps);
    if (Condition.Menu === 'Lights') RefreshLampMap();
    if (Math.round(Condition.Speed) !== ShownSpeed) { ShownSpeed = Math.round(Condition.Speed); Find('SpeedText').textContent = ShownSpeed; }
    if (Condition.Replay) Condition.Heading += Delta * 6 * Math.sin(Elapsed * 0.2);
    RefreshHeading();

    UnderGlow.material.opacity += ((Condition.Underbody ? 0.9 : 0) - UnderGlow.material.opacity) * (1 - Math.exp(-Delta * 5));
    if (Fans.visible) { AdvanceFans(Elapsed); RefreshQuarters(); }

    Renderer.render(Scene, Camera);
    PlaceCallouts();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

DrawDial();
BuildTyreGrid();
BuildQuarters();
BuildHeadingTape();
BindDial();
BindTiltPad();
SlideSegment(Find('FanMode'), Find('FanMode').querySelector('.On'));
Find('GearRow').querySelector('i').style.transform = 'translateX(0px)';
SelectMode(0, true);
SetLamps({ Beam: 'Low' });
requestAnimationFrame(() => { Find('RangeFill').style.strokeDasharray = `${0.72 * 283} 400`; });
Find('Clock').textContent = ClockText();
setInterval(() => { Find('Clock').textContent = ClockText(); }, 15000);
AdvanceFans(0);
RunLoop(Step);
window.HmiCondition = { Condition, OpenMenu, SelectMode, SetPose, Orbit, Train, Link, Lamps, SetLamps };  // 🔍 exposed for the capture script

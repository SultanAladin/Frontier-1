// LUCENT — CABIN HMI PROTOTYPE C
// 🧩 Glass-card luxury display: the ControlVehicle in a midnight studio before a curved light wall on a mirror
//    floor. A deck of tilted glass cards flips per menu. Menus: Studio (overview cards), Route (a city rises
//    and a route draws itself), Access (lock shockwave, hinged lid outlines, charge port), Lighting (wall
//    colour, intensity, motion, welcome sequence) and Seats (x-ray cabin with reclining seat proxies).

import { THREE, AssembleVehicle, DefaultFinish, PanelEnvironment, Family, MirrorTwin } from '../Shared/VehicleAssembly.js';
import { FitDisplay, CameraOrbit, ProjectAnchor, RollNumber, BindRipples, ClockText, RunLoop } from '../Shared/DisplayKit.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const DisplayWidth = 1600, DisplayHeight = 900;

const LightColours = [
    { Name: 'Lime',    Hex: '#c8ff3d', Rgb: '200, 255, 61' },
    { Name: 'Glacier', Hex: '#6fd0ff', Rgb: '111, 208, 255' },
    { Name: 'Orchid',  Hex: '#e07bff', Rgb: '224, 123, 255' },
    { Name: 'Amber',   Hex: '#ffb340', Rgb: '255, 179, 64' },
];

const Presets = [
    { Recline: 18, Heat: 0, Vent: 0, Slide: 0,     Note: 'Ilan · daily' },
    { Recline: 32, Heat: 2, Vent: 0, Slide: -0.08, Note: 'Ilan · long haul' },
    { Recline: 11, Heat: 0, Vent: 1, Slide: 0.06,  Note: 'Guest · upright' },
];

const Street = 2.4;                                               // [m] - city street pitch (scene units)
const Routes = [
    { Name: 'Home',    Points: [[0, 0], [0, -7.2], [9.6, -7.2], [9.6, -12]],             Eta: 12, Km: 8.4,  Turn: ['320 m', 'Kestrel Avenue', 1] },
    { Name: 'Harbour', Points: [[0, 0], [-4.8, 0], [-4.8, 9.6], [-12, 9.6]],             Eta: 18, Km: 11.2, Turn: ['540 m', 'Quay Road', -1] },
    { Name: 'Atelier', Points: [[0, 0], [0, 4.8], [7.2, 4.8], [7.2, 12], [2.4, 12]],     Eta: 9,  Km: 5.1,  Turn: ['180 m', 'Foundry Lane', 1] },
];

const StudioYaw = 0.9;
const CameraPresets = {
    Studio:   { Target: [0.3, 0.55, 0], Radius: 13.5, Yaw: StudioYaw, Pitch: 0.13 },
    Route:    { Target: [0, -1.2, 0],   Radius: 34,   Yaw: 0.62,      Pitch: 0.92 },
    Access:   { Target: [0, 0.6, 0],    Radius: 13,   Yaw: 0.32,      Pitch: 0.3 },
    Lighting: { Target: [0.4, 0.5, 0],  Radius: 12,   Yaw: 1.32,      Pitch: 0.07 },
    Seats:    { Target: [-0.65, 0.2, -0.75], Radius: 13, Yaw: 0.42,    Pitch: 1.0 },
};
const MenuText = {
    Studio:   ['Studio', 'Everything about the car, one glance away.'],
    Route:    ['Route', 'Your city, drawn in light.'],
    Access:   ['Access', 'Doors, lids and the charge port.'],
    Lighting: ['Lighting', 'Paint the studio and the cabin.'],
    Seats:    ['Seats', 'Recline, warmth and memory.'],
};

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
    Menu:      'Studio',
    Locked:    true,
    CabinTemp: 21.5,
    Light:     0,
    WallLevel: 0.8,
    Motion:    'wave',
    Route:     0,
    Guiding:   false,
    Lids:      { Frunk: false, Boot: false },
    PortOpen:  false,
    Charge:    78,
    Preset:    0,
    Recline:   18,               // [°]
    Slide:     0,                // [m]
    Heat:      0,
    Vent:      0,
    Massage:   'off',
    SweepAt:   -10,              // [s] - welcome sweep start
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDERER AND SCENE
//------------------------------------------------------------------------------------------------------------------------

const Canvas   = Find('Scene');
const Renderer = new THREE.WebGLRenderer({ canvas: Canvas, antialias: true });
Renderer.setPixelRatio(Math.min(2, window.devicePixelRatio || 1));
Renderer.setSize(DisplayWidth, DisplayHeight, false);
Renderer.toneMapping         = THREE.ACESFilmicToneMapping;
Renderer.toneMappingExposure = 1.05;
Renderer.outputColorSpace    = THREE.SRGBColorSpace;

const Scene = new THREE.Scene();
Scene.background = new THREE.Color('#02050f');
Scene.fog        = new THREE.Fog('#02050f', 22, 70);

const Accent = new THREE.Color(LightColours[0].Hex);           // eased towards the chosen light colour
const AccentGoal = Accent.clone();

let Environment = null;
function RefreshEnvironment()
{
    const Previous = Environment;
    Environment = PanelEnvironment(Renderer, [
        { Colour: LightColours[Condition.Light].Hex, Intensity: 1.3 * Condition.WallLevel + 0.1, Position: [-9, 2.4, -7], Size: [16, 4], Facing: [0, 1, 0] },
        { Colour: '#9fb8ff', Intensity: 1.2, Position: [0, 9, 0],   Size: [10, 6] },
        { Colour: '#3d6bff', Intensity: 0.8, Position: [8, 2, 6],   Size: [8, 3] },
        { Colour: '#02040c', Intensity: 1.0, Position: [0, -3, 0],  Size: [30, 30] },
    ], '#03060f');
    Scene.environment = Environment;
    if (Previous) Previous.dispose();
}
RefreshEnvironment();

const Camera = new THREE.PerspectiveCamera(30, DisplayWidth / DisplayHeight, 0.1, 200);
const Orbit  = new CameraOrbit(Camera, Canvas, { ...CameraPresets.Studio, RadiusLimits: [7, 44], PitchLimits: [0.0, 1.35] });

const Hemisphere = new THREE.HemisphereLight('#7f9cff', '#02040c', 0.45);
const WallLight  = new THREE.DirectionalLight('#c8ff3d', 1.6);
WallLight.position.set(-9, 4, -7);
const FrontFill  = new THREE.DirectionalLight('#cfdcff', 1.2);
FrontFill.position.set(7, 6, 8);
Scene.add(Hemisphere, WallLight, FrontFill);

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
    return new THREE.CanvasTexture(Paint);
}
const SoftGlow = GlowTexture();

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIGHT WALL
//------------------------------------------------------------------------------------------------------------------------

const WallRadius = 14, WallSlats = 170, WallSpan = 2.5;          // [m], count, [rad]
const WallCentre = StudioYaw + Math.PI;
const SlatShape  = new THREE.BoxGeometry(0.11, 4.6, 0.05).translate(0, 2.3, 0);
const SlatFinish = new THREE.MeshBasicMaterial({ color: '#ffffff', toneMapped: false, fog: false });
const Wall       = new THREE.InstancedMesh(SlatShape, SlatFinish, WallSlats);
const WallEchoGroup = new THREE.Group();
WallEchoGroup.scale.y = -1;
const WallEcho   = new THREE.InstancedMesh(SlatShape, SlatFinish, WallSlats);
{
    const Placement = new THREE.Object3D();
    for (let Index = 0; Index < WallSlats; Index++)
    {
        const Theta = WallCentre + (Index / (WallSlats - 1) - 0.5) * WallSpan;
        Placement.position.set(Math.sin(Theta) * WallRadius, 0, Math.cos(Theta) * WallRadius);
        Placement.rotation.set(0, Theta, 0);
        Placement.updateMatrix();
        Wall.setMatrixAt(Index, Placement.matrix);
        WallEcho.setMatrixAt(Index, Placement.matrix);
        Wall.setColorAt(Index, Accent);
    }
    WallEcho.instanceColor = Wall.instanceColor;
}
Wall.frustumCulled = WallEcho.frustumCulled = false;
WallEchoGroup.add(WallEcho);
Scene.add(Wall, WallEchoGroup);

const HazeUniforms = { Colour: { value: Accent }, Level: { value: 0.8 } };
const HazeFinish = new THREE.ShaderMaterial({
    uniforms: HazeUniforms, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide, fog: false, toneMapped: false,
    vertexShader: /* glsl */`varying vec2 Spot; void main() { Spot = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }`,
    fragmentShader: /* glsl */`
        uniform vec3 Colour; uniform float Level; varying vec2 Spot;
        void main()
        {
            float Rise = pow(1.0 - Spot.y, 1.6);
            float Edge = smoothstep(0.0, 0.18, Spot.x) * smoothstep(1.0, 0.82, Spot.x);
            gl_FragColor = vec4(Colour * Rise * Edge * Level * 0.22, 1.0);
        }`,
});
const Haze = new THREE.Mesh(new THREE.CylinderGeometry(WallRadius + 0.6, WallRadius + 0.6, 7, 96, 1, true, WallCentre - WallSpan / 2 - 0.1, WallSpan + 0.2).translate(0, 3.5, 0), HazeFinish);
Scene.add(Haze);

//------------------------------------------------------------------------------------------------------------------------
//                                                    MIRROR FLOOR
//------------------------------------------------------------------------------------------------------------------------

const FloorAlpha = (() =>
{
    const Paint = document.createElement('canvas');
    Paint.width = Paint.height = 256;
    const Pen  = Paint.getContext('2d');
    const Fall = Pen.createRadialGradient(128, 128, 0, 128, 128, 128);
    Fall.addColorStop(0, 'rgb(130,130,130)');
    Fall.addColorStop(0.42, 'rgb(150,150,150)');
    Fall.addColorStop(0.75, 'rgb(225,225,225)');
    Fall.addColorStop(1, 'rgb(255,255,255)');
    Pen.fillStyle = Fall;
    Pen.fillRect(0, 0, 256, 256);
    return new THREE.CanvasTexture(Paint);
})();
const Floor = new THREE.Mesh(new THREE.CircleGeometry(30, 128).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ color: '#040918', roughness: 0.2, metalness: 0.35, transparent: true, alphaMap: FloorAlpha, depthWrite: false }));
Floor.renderOrder = 2;
Scene.add(Floor);

const FloorRings = new THREE.Group();
for (const Radius of [4.6, 4.66, 7.5])
{
    const Ring = new THREE.Mesh(new THREE.RingGeometry(Radius, Radius + 0.02, 160).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#3d6bff', transparent: true, opacity: Radius > 7 ? 0.18 : 0.35, depthWrite: false }));
    Ring.position.y = 0.006;
    Ring.renderOrder = 3;
    FloorRings.add(Ring);
}
Scene.add(FloorRings);

//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE
//------------------------------------------------------------------------------------------------------------------------

const Finish = DefaultFinish({
    [Family.BodyCoat]:  { color: '#2a4fd6', roughness: 0.26, metalness: 0.45, clearcoat: 1, clearcoatRoughness: 0.04 },
    [Family.Glass]:     { color: '#03050c', roughness: 0.03 },
    [Family.Hub]:       { color: '#b8c2d8', roughness: 0.16 },
    [Family.Headlight]: { color: '#10162a', emissive: '#bcd0ff', emissiveIntensity: 0.3 },
    [Family.Taillight]: { color: '#2a0810', emissive: '#ff2040', emissiveIntensity: 1.4 },
});
const Vehicle = await AssembleVehicle({ Finish, Wing: true });
Scene.add(Vehicle.Root);

const ContactShade = new THREE.Mesh(new THREE.PlaneGeometry(7.6, 3.8).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ map: GlowTexture('rgba(0,0,0,1)', 'rgba(0,0,0,0)'), transparent: true, opacity: 0.9, depthWrite: false }));
ContactShade.position.y = 0.008;
ContactShade.renderOrder = 4;
Vehicle.Root.add(ContactShade);

const Mirror = MirrorTwin(Vehicle.Root);
Scene.add(Mirror.Group);

// X-ray outline of the body, shown while the cabin is ghosted.
const BodyEdges = new THREE.LineSegments(new THREE.EdgesGeometry(Vehicle.Body.geometry, 28), new THREE.LineBasicMaterial({ color: '#7fa4ff', transparent: true, opacity: 0, depthWrite: false }));
BodyEdges.renderOrder = 7;
Vehicle.Body.add(BodyEdges);
const Ghosted = [Family.BodyCoat, Family.Glass, Family.Plastic, Family.Trim, Family.Default, Family.Headlight, Family.Taillight].map((Id) => Finish[Id]);

//------------------------------------------------------------------------------------------------------------------------
//                                                    SEAT PROXIES
//------------------------------------------------------------------------------------------------------------------------

const SeatFinish   = new THREE.MeshStandardMaterial({ color: '#5a7cff', roughness: 0.45, metalness: 0.1, emissive: '#1a2f90', emissiveIntensity: 0.6, transparent: true, opacity: 0 });
const DriverFinish = SeatFinish.clone();
const SeatGroup    = new THREE.Group();
SeatGroup.visible  = false;
Vehicle.BodyMount.add(SeatGroup);
const RoundedBox = (W, H, D) => new THREE.BoxGeometry(W, H, D, 2, 2, 2);
function BuildSeat(X, Z, Paint)
{
    const Seat = new THREE.Group();
    Seat.position.set(X, 0.38, Z);
    const Cushion = new THREE.Mesh(RoundedBox(0.56, 0.12, 0.5), Paint);
    Seat.add(Cushion);
    const Pivot = new THREE.Group();
    Pivot.position.set(-0.27, 0.05, 0);
    const Back = new THREE.Mesh(RoundedBox(0.11, 0.66, 0.48).translate(0, 0.33, 0), Paint);
    const Head = new THREE.Mesh(RoundedBox(0.1, 0.16, 0.28).translate(0, 0.76, 0), Paint);
    Pivot.add(Back, Head);
    Seat.add(Pivot);
    SeatGroup.add(Seat);
    return { Seat, Pivot, Cushion };
}
const DriverSeat = BuildSeat(0.05, -0.42, DriverFinish);
const OtherSeats = [BuildSeat(0.05, 0.42, SeatFinish), BuildSeat(-1.05, -0.42, SeatFinish), BuildSeat(-1.05, 0.42, SeatFinish)];
OtherSeats.forEach((Each) => { Each.Pivot.rotation.z = 0.3; });

const MassageDots = new THREE.Points(new THREE.BufferGeometry().setAttribute('position', new THREE.BufferAttribute(new Float32Array(16 * 3), 3)), new THREE.PointsMaterial({ size: 0.07, map: SoftGlow, color: '#c8ff3d', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, depthTest: false }));
MassageDots.frustumCulled = false;
MassageDots.renderOrder = 8;
DriverSeat.Pivot.add(MassageDots);

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIDS, LOCK WAVE, CHARGE PORT
//------------------------------------------------------------------------------------------------------------------------

function BuildLid(Hinge, Length, Width, Forward)
{
    const Group = new THREE.Group();
    Group.position.set(...Hinge);
    const Plate = new THREE.BoxGeometry(Length, 0.02, Width).translate(Forward * Length / 2, 0, 0);
    const Fill  = new THREE.Mesh(Plate, new THREE.MeshBasicMaterial({ color: '#c8ff3d', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide }));
    const Edge  = new THREE.LineSegments(new THREE.EdgesGeometry(Plate), new THREE.LineBasicMaterial({ color: '#c8ff3d', transparent: true, opacity: 0, depthWrite: false }));
    Fill.renderOrder = Edge.renderOrder = 6;
    Group.add(Fill, Edge);
    Vehicle.BodyMount.add(Group);
    return { Group, Fill, Edge, Forward, Angle: 0 };
}
const Lids = { Frunk: BuildLid([1.05, 0.86, 0], 1.85, 1.75, 1), Boot: BuildLid([-2.05, 0.95, 0], 1.0, 1.75, -1) };

const LockWave = new THREE.Mesh(new THREE.RingGeometry(0.94, 1, 128).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#c8ff3d', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
LockWave.position.y = 0.02;
LockWave.renderOrder = 5;
Scene.add(LockWave);
let LockWaveAt = -10, BlinkAt = -10;

const PortGlow = new THREE.Sprite(new THREE.SpriteMaterial({ map: SoftGlow, color: '#c8ff3d', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false }));
PortGlow.position.set(-2.25, 0.62, 1.24);
PortGlow.scale.setScalar(0.6);
Vehicle.BodyMount.add(PortGlow);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CITY AND ROUTE
//------------------------------------------------------------------------------------------------------------------------

const CityHalf   = 6;                                             // blocks per half axis
const Blocks     = [];
for (let I = -CityHalf; I < CityHalf; I++) for (let J = -CityHalf; J < CityHalf; J++)
{
    const X = (I + 0.5) * Street, Z = (J + 0.5) * Street;
    const Seed = Math.abs(Math.sin(I * 12.9898 + J * 78.233) * 43758.5453) % 1;
    const Reach = Math.hypot(X, Z);
    Blocks.push({ X, Z, Height: 0.35 + Seed * Seed * (3.4 - Math.min(2.2, Reach * 0.08)), Reach, Size: 1.55 + (Seed * 7 % 1) * 0.2 });
}
const CityFinish = new THREE.MeshStandardMaterial({ color: '#1a3896', roughness: 0.35, metalness: 0.3, emissive: '#0a1c5c', transparent: true, opacity: 0.82 });
const City = new THREE.InstancedMesh(new THREE.BoxGeometry(1, 1, 1).translate(0, 0.5, 0), CityFinish, Blocks.length);
City.visible = false;
City.frustumCulled = false;
Scene.add(City);
const CityLines = new THREE.LineSegments(new THREE.BufferGeometry().setAttribute('position', new THREE.BufferAttribute(new Float32Array(Blocks.length * 24 * 3), 3)), new THREE.LineBasicMaterial({ color: '#6f96ff', transparent: true, opacity: 0.0, depthWrite: false }));
CityLines.frustumCulled = false;
CityLines.visible = false;
Scene.add(CityLines);

const GridPoints = [];
for (let Index = -CityHalf; Index <= CityHalf; Index++)
{
    const At = Index * Street, End = CityHalf * Street;
    GridPoints.push(At, 0.015, -End, At, 0.015, End, -End, 0.015, At, End, 0.015, At);
}
const StreetGrid = new THREE.LineSegments(new THREE.BufferGeometry().setAttribute('position', new THREE.Float32BufferAttribute(GridPoints, 3)), new THREE.LineBasicMaterial({ color: '#2f56d8', transparent: true, opacity: 0, depthWrite: false }));
StreetGrid.renderOrder = 3;
Scene.add(StreetGrid);

let CityShown = -1;
const BoxCorners = [[-0.5, 0, -0.5], [0.5, 0, -0.5], [0.5, 0, 0.5], [-0.5, 0, 0.5], [-0.5, 1, -0.5], [0.5, 1, -0.5], [0.5, 1, 0.5], [-0.5, 1, 0.5]];
const BoxEdges   = [[0, 1], [1, 2], [2, 3], [3, 0], [4, 5], [5, 6], [6, 7], [7, 4], [0, 4], [1, 5], [2, 6], [3, 7]];
function ShapeCity(Rise)
{
    if (Math.abs(Rise - CityShown) < 0.002) return;
    CityShown = Rise;
    City.visible = CityLines.visible = Rise > 0.01;
    const Placement = new THREE.Object3D();
    const Lines = CityLines.geometry.attributes.position;
    let Cursor = 0;
    Blocks.forEach((Block, Index) =>
    {
        const Local = THREE.MathUtils.clamp(Rise * 1.8 - Block.Reach / 22, 0, 1);
        const Grow  = 1 - Math.pow(1 - Local, 3);
        const Height = Math.max(0.001, Block.Height * Grow);
        Placement.position.set(Block.X, 0, Block.Z);
        Placement.scale.set(Block.Size, Height, Block.Size);
        Placement.updateMatrix();
        City.setMatrixAt(Index, Placement.matrix);
        for (const [From, To] of BoxEdges) for (const Corner of [BoxCorners[From], BoxCorners[To]])
            Lines.setXYZ(Cursor++, Block.X + Corner[0] * Block.Size, Corner[1] * Height, Block.Z + Corner[2] * Block.Size);
    });
    City.instanceMatrix.needsUpdate = true;
    Lines.needsUpdate = true;
}

const RouteFinish = new THREE.MeshBasicMaterial({ color: '#c8ff3d', toneMapped: false, transparent: true, opacity: 1, depthWrite: false });
let RouteMesh = null, RoutePath = null, RouteDraw = 0;
const Comet = new THREE.Sprite(new THREE.SpriteMaterial({ map: SoftGlow, color: '#ffffff', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
Comet.scale.setScalar(1.6);
Comet.renderOrder = 9;
Scene.add(Comet);
const Marker = new THREE.Sprite(new THREE.SpriteMaterial({ map: SoftGlow, color: '#ffffff', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
Marker.scale.setScalar(2.2);
Marker.position.y = 0.3;
Marker.renderOrder = 9;
Scene.add(Marker);
const Beacon = new THREE.Group();
const BeaconBeam = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.07, 5, 12, 1, true).translate(0, 2.5, 0), new THREE.MeshBasicMaterial({ color: '#c8ff3d', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
const BeaconRing = new THREE.Mesh(new THREE.RingGeometry(0.5, 0.62, 64).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#c8ff3d', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
BeaconRing.position.y = 0.05;
Beacon.add(BeaconBeam, BeaconRing);
Scene.add(Beacon);

function BuildRoute(Index)
{
    if (RouteMesh) { Scene.remove(RouteMesh); RouteMesh.geometry.dispose(); }
    const Points = Routes[Index].Points.map(([X, Z]) => new THREE.Vector3(X, 0.12, Z));
    RoutePath = new THREE.CurvePath();
    for (let Step = 1; Step < Points.length; Step++) RoutePath.add(new THREE.LineCurve3(Points[Step - 1], Points[Step]));
    RouteMesh = new THREE.Mesh(new THREE.TubeGeometry(RoutePath, 480, 0.11, 8, false), RouteFinish);
    RouteMesh.renderOrder = 8;
    RouteMesh.geometry.setDrawRange(0, 0);
    Scene.add(RouteMesh);
    RouteDraw = 0;
    Beacon.position.copy(Points[Points.length - 1]).setY(0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CALLOUTS
//------------------------------------------------------------------------------------------------------------------------

const CalloutLayer = Find('Callouts');
const Pins = [];
function AddPin(Html, Anchor, Menus)
{
    const Element = document.createElement('div');
    Element.className = 'Pin Hidden';
    Element.innerHTML = `<div class="Tag">${Html}</div><div class="Stem"></div><div class="Dot"></div>`;
    CalloutLayer.append(Element);
    const Entry = { Element, Tag: Element.firstChild, Anchor, Menus: new Set(Menus) };
    Pins.push(Entry);
    return Entry;
}
const AtBody = (X, Y, Z) => () => Vehicle.BodyMount.localToWorld(new THREE.Vector3(X, Y, Z));
const FrunkPin  = AddPin('Frunk <b>closed</b>', AtBody(2.1, 1.05, 0), ['Access']);
const BootPin   = AddPin('Boot <b>closed</b>', AtBody(-2.6, 1.25, 0), ['Access']);
const PortPin   = AddPin('Charge port <b>closed</b>', AtBody(-2.25, 0.75, 1.24), ['Access']);
const LockPin   = AddPin('Doors <b>locked</b>', AtBody(-0.4, 1.55, 0), ['Access']);
const SeatPin   = AddPin('Driver <b>18°</b>', AtBody(0.05, 1.25, -0.42), ['Seats']);
const BeaconPin = AddPin('Home <b>12 min</b>', () => Beacon.position.clone().setY(5.2), ['Route']);

function PlacePins()
{
    for (const Entry of Pins)
    {
        const Spot = ProjectAnchor(Camera, Entry.Anchor(), DisplayWidth, DisplayHeight);
        Entry.Element.classList.toggle('Hidden', !Entry.Menus.has(Condition.Menu) || !Spot.Visible || Orbit.Transit !== null);
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
    ToastTimer = setTimeout(() => Toast.classList.remove('Show'), 2400);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    MENUS
//------------------------------------------------------------------------------------------------------------------------

function OpenMenu(Menu)
{
    if (!MenuText[Menu]) return;
    Condition.Menu = Menu;
    Find('IconColumn').querySelectorAll('[data-menu]').forEach((Button) => Button.classList.toggle('On', Button.dataset.menu === Menu));
    document.querySelectorAll('.CardSet').forEach((Set) => Set.classList.toggle('On', Set.dataset.set === Menu));
    const Heading = document.querySelector('.Heading');
    Heading.classList.add('Swap');
    setTimeout(() => { Find('MenuTitle').textContent = MenuText[Menu][0]; Find('MenuLead').textContent = MenuText[Menu][1]; Heading.classList.remove('Swap'); }, 230);
    Orbit.Aim(CameraPresets[Menu], 1300);
    if (Menu === 'Route' && RouteDraw >= 1) RouteDraw = 0;
}
Find('IconColumn').addEventListener('click', (Press) => { const Button = Press.target.closest('[data-menu]'); if (Button) OpenMenu(Button.dataset.menu); });
document.querySelectorAll('.Card .Go').forEach((Go) => Go.addEventListener('click', (Press) => { Press.stopPropagation(); OpenMenu(Go.closest('.Card').dataset.go); }));

function Choose(Group, Button)
{
    Group.querySelectorAll('button').forEach((Each) => Each.classList.toggle('On', Each === Button));
}

// ── Lock
function SetLocked(Locked)
{
    Condition.Locked = Locked;
    Display.classList.toggle('Unlocked-State', !Locked);
    LockWaveAt = BlinkAt = performance.now() / 1000;
    LockWave.material.color.copy(Locked ? Accent : new THREE.Color('#ffffff'));
    LockPin.Tag.innerHTML = `Doors <b>${Locked ? 'locked' : 'unlocked'}</b>`;
    LockPin.Element.classList.toggle('Live', !Locked);
    Announce(Locked ? 'Car <b>locked</b> · mirrors folded' : 'Car <b>unlocked</b> · welcome lights on');
}
document.querySelectorAll('[data-lock]').forEach((Pill) => Pill.addEventListener('click', () => SetLocked(!Condition.Locked)));

// ── Temperature
document.querySelectorAll('[data-temp]').forEach((Button) => Button.addEventListener('click', () =>
{
    Condition.CabinTemp = THREE.MathUtils.clamp(Condition.CabinTemp + Number(Button.dataset.temp), 16, 28);
    RollNumber(Find('CabinTemp'), Condition.CabinTemp, { Duration: 360, Format: (Figure) => Figure.toFixed(1) });
}));

// ── Light colour
document.querySelectorAll('[data-swatches]').forEach((Group) =>
{
    Group.innerHTML = LightColours.map((Colour, Index) => `<button data-light="${Index}" style="background:radial-gradient(circle at 35% 30%, #ffffff, ${Colour.Hex} 62%)" title="${Colour.Name}"></button>`).join('');
    Group.addEventListener('click', (Press) => { const Button = Press.target.closest('[data-light]'); if (Button) SetLight(Number(Button.dataset.light)); });
});
let EnvironmentTimer = 0;
function SetLight(Index)
{
    Condition.Light = Index;
    const Colour = LightColours[Index];
    AccentGoal.set(Colour.Hex);
    Display.style.setProperty('--Accent', Colour.Hex);
    Display.style.setProperty('--AccentRGB', Colour.Rgb);
    document.querySelectorAll('[data-light]').forEach((Button) => Button.classList.toggle('On', Number(Button.dataset.light) === Index));
    Find('LightName').textContent = Colour.Name;
    document.querySelector('.LightNameEcho').textContent = `${Colour.Name} · studio and cabin`;
    clearTimeout(EnvironmentTimer);
    EnvironmentTimer = setTimeout(RefreshEnvironment, 650);
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
BindSlider(Find('WallSlider'), (Ratio) =>
{
    Condition.WallLevel = 0.1 + Ratio * 0.9;
    const Percent = Math.round(Condition.WallLevel * 100);
    Find('WallLevelBig').textContent = Percent;
    Find('WallLevelText').textContent = `${Percent}%`;
    clearTimeout(EnvironmentTimer);
    EnvironmentTimer = setTimeout(RefreshEnvironment, 650);
});
Find('WallMotion').addEventListener('click', (Press) => { const Button = Press.target.closest('[data-motion]'); if (!Button) return; Condition.Motion = Button.dataset.motion; Choose(Find('WallMotion'), Button); });

function PlayWelcome()
{
    Condition.SweepAt = performance.now() / 1000;
    setTimeout(() => { BlinkAt = performance.now() / 1000; }, 900);
    setTimeout(() => { LockWaveAt = performance.now() / 1000; LockWave.material.color.copy(Accent); }, 1300);
    Announce('Welcome back, <b>Ilan</b>');
}
Find('WelcomeButton').addEventListener('click', PlayWelcome);

// ── Route
function SetRoute(Index)
{
    Condition.Route = Index;
    const Route = Routes[Index];
    Choose(Find('DestinationList'), Find('DestinationList').querySelector(`[data-route="${Index}"]`));
    BuildRoute(Index);
    RollNumber(Find('EtaText'), Route.Eta);
    RollNumber(Find('DistanceText'), Route.Km, { Format: (Figure) => Figure.toFixed(1) });
    const Arrive = new Date(Date.now() + Route.Eta * 60000);
    Find('ArriveText').textContent = ClockText(Arrive);
    Find('TurnDistance').textContent = Route.Turn[0];
    Find('TurnStreet').textContent   = Route.Turn[1];
    Find('TurnArrow').style.transform = `scaleX(${Route.Turn[2]})`;
    BeaconPin.Tag.innerHTML = `${Route.Name} <b>${Route.Eta} min</b>`;
    const Xs = Route.Points.map((Point) => Point[0]), Zs = Route.Points.map((Point) => Point[1]);
    CameraPresets.Route.Target = [(Math.min(...Xs) + Math.max(...Xs)) / 2 + 1.5, -1.6, (Math.min(...Zs) + Math.max(...Zs)) / 2 + 2.2];
    if (Condition.Menu === 'Route') Orbit.Aim(CameraPresets.Route, 1100);
}
Find('DestinationList').addEventListener('click', (Press) => { const Button = Press.target.closest('[data-route]'); if (Button) SetRoute(Number(Button.dataset.route)); });
Find('GuideButton').addEventListener('click', () =>
{
    Condition.Guiding = !Condition.Guiding;
    Find('GuideButton').classList.toggle('Active', Condition.Guiding);
    Find('GuideButton').firstChild.textContent = Condition.Guiding ? 'Guiding · tap to end' : 'Start guidance';
    if (Condition.Guiding) Announce(`Guidance to <b>${Routes[Condition.Route].Name}</b> started`);
});

// ── Access
function SetLid(Name, Open)
{
    Condition.Lids[Name] = Open;
    const Button = document.querySelector(`[data-lid="${Name}"]`);
    Button.classList.toggle('Open', Open);
    Button.querySelector('span').textContent = `${Open ? 'Close' : 'Open'} ${Name === 'Frunk' ? 'frunk' : 'boot'}`;
    const Pin = Name === 'Frunk' ? FrunkPin : BootPin;
    Pin.Tag.innerHTML = `${Name} <b>${Open ? 'open' : 'closed'}</b>`;
    Pin.Element.classList.toggle('Live', Open);
}
document.querySelectorAll('[data-lid]').forEach((Button) => Button.addEventListener('click', () => SetLid(Button.dataset.lid, !Condition.Lids[Button.dataset.lid])));
Find('PortToggle').addEventListener('click', () =>
{
    Condition.PortOpen = !Condition.PortOpen;
    Find('PortToggle').classList.toggle('On', Condition.PortOpen);
    Find('PortNote').textContent = Condition.PortOpen ? 'Charging · 11 kW' : 'Port closed';
    PortPin.Tag.innerHTML = `Charge port <b>${Condition.PortOpen ? 'charging' : 'closed'}</b>`;
    PortPin.Element.classList.toggle('Live', Condition.PortOpen);
});

// ── Seats
const ReclineToRatio = (Degree) => (Degree - 5) / 35;
let ApplyRecline = null;
function ShowRecline()
{
    document.querySelectorAll('.ReclineText').forEach((Text) => { Text.textContent = `${Math.round(Condition.Recline)}°`; });
    Find('BackrestIcon').style.transform = `rotate(${-(Condition.Recline - 18)}deg)`;
    SeatPin.Tag.innerHTML = `Driver <b>${Math.round(Condition.Recline)}°</b>`;
}
ApplyRecline = BindSlider(Find('ReclineSlider'), (Ratio) => { Condition.Recline = 5 + Ratio * 35; ShowRecline(); });
function SetLevel(Id, Key, Level)
{
    Condition[Key] = Level;
    Find(Id).dataset.level = Level;
    document.querySelectorAll('.HeatText').forEach((Text) => { Text.textContent = Condition.Heat ? ['', 'Low', 'Mid', 'High'][Condition.Heat] : Condition.Vent ? `Vent ${Condition.Vent}` : 'Off'; });
}
Find('SeatHeat').addEventListener('click', () => { SetLevel('SeatHeat', 'Heat', (Condition.Heat + 1) % 4); if (Condition.Heat) SetLevel('SeatVent', 'Vent', 0); });
Find('SeatVent').addEventListener('click', () => { SetLevel('SeatVent', 'Vent', (Condition.Vent + 1) % 4); if (Condition.Vent) SetLevel('SeatHeat', 'Heat', 0); });
Find('Massage').addEventListener('click', (Press) => { const Button = Press.target.closest('[data-massage]'); if (!Button) return; Condition.Massage = Button.dataset.massage; Choose(Find('Massage'), Button); });
function SetPreset(Index)
{
    Condition.Preset = Index;
    const Preset = Presets[Index];
    document.querySelectorAll('[data-memory] [data-preset]').forEach((Button) => Button.classList.toggle('On', Number(Button.dataset.preset) === Index));
    Condition.Slide = Preset.Slide;
    ApplyRecline(ReclineToRatio(Preset.Recline));
    SetLevel('SeatHeat', 'Heat', Preset.Heat);
    SetLevel('SeatVent', 'Vent', Preset.Vent);
    Find('PresetNote').textContent = Preset.Note;
}
document.querySelectorAll('[data-memory]').forEach((Group) => Group.addEventListener('click', (Press) => { const Button = Press.target.closest('[data-preset]'); if (Button) SetPreset(Number(Button.dataset.preset)); }));

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME STEP
//------------------------------------------------------------------------------------------------------------------------

const Follow = (Rate, Delta) => 1 - Math.exp(-Delta * Rate);
const Live   = { Rise: 0, CarScale: 1, Ghost: 0, Wall: 0.8, Grid: 0 };
const SlatColour = new THREE.Color();
const HeatGlow = new THREE.Color('#ff7a2a'), VentGlow = new THREE.Color('#4fc0ff');
let LastWall = performance.now();

function WallPattern(Ratio, Clock)
{
    let Value = 1;
    if (Condition.Motion === 'wave')  Value = 0.5 + 0.5 * Math.sin(Ratio * 14 - Clock * 2.2);
    if (Condition.Motion === 'pulse') Value = 0.62 + 0.38 * Math.sin(Clock * 2.0);
    const Since = Clock - Condition.SweepAt;
    if (Since < 4) Value *= THREE.MathUtils.clamp(Since * 1.1 - Math.abs(Ratio - 0.5) * 2.4, 0, 1);
    return Value;
}

RunLoop((Delta, Elapsed) =>
{
    const Now   = performance.now();
    const Wall_ = Math.min(0.5, (Now - LastWall) / 1000);       // wall-time step keeps eases honest at low frame rates
    LastWall = Now;
    const Clock = Now / 1000;
    const Ease  = Follow(2.6, Wall_);

    Accent.lerp(AccentGoal, Ease);
    WallLight.color.copy(Accent);
    RouteFinish.color.copy(Accent);
    BeaconBeam.material.color.copy(Accent);
    BeaconRing.material.color.copy(Accent);
    MassageDots.material.color.copy(Accent);
    for (const Lid of Object.values(Lids)) { Lid.Fill.material.color.copy(Accent); Lid.Edge.material.color.copy(Accent); }
    PortGlow.material.color.copy(Accent);

    const InRoute = Condition.Menu === 'Route';
    Live.Rise     += ((InRoute ? 1 : 0) - Live.Rise) * Follow(1.4, Wall_);
    Live.CarScale += ((InRoute ? 0.2 : 1) - Live.CarScale) * Ease;
    Live.Ghost    += ((Condition.Menu === 'Seats' ? 1 : 0) - Live.Ghost) * Ease;
    Live.Wall     += ((InRoute ? 0 : Condition.WallLevel) - Live.Wall) * Ease;
    Live.Grid     += ((InRoute ? 1 : 0) - Live.Grid) * Ease;

    // Light wall.
    for (let Index = 0; Index < WallSlats; Index++)
    {
        const Ratio = Index / (WallSlats - 1);
        SlatColour.copy(Accent).multiplyScalar(Live.Wall * (0.1 + 0.6 * WallPattern(Ratio, Clock)));
        Wall.setColorAt(Index, SlatColour);
    }
    Wall.instanceColor.needsUpdate = true;
    Wall.visible = WallEcho.visible = Haze.visible = Live.Wall > 0.02;
    HazeUniforms.Level.value = Live.Wall * (Condition.Motion === 'pulse' ? 0.8 + 0.2 * Math.sin(Clock * 2) : 1);
    WallLight.intensity = 0.4 + 1.8 * Live.Wall;

    // City and route.
    ShapeCity(Live.Rise);
    CityLines.material.opacity = 0.55 * Live.Rise;
    CityFinish.opacity = 0.82 * Math.min(1, Live.Rise * 3);
    StreetGrid.material.opacity = 0.5 * Live.Grid;
    FloorRings.visible = Live.Grid < 0.5;
    Marker.material.color.copy(Accent);
    Marker.material.opacity = Live.Grid * (0.7 + 0.3 * Math.sin(Clock * 3));
    Vehicle.Root.scale.setScalar(Live.CarScale);
    if (RouteMesh)
    {
        if (InRoute && Live.Rise > 0.6) RouteDraw = Math.min(1, RouteDraw + Wall_ * 0.7);
        if (!InRoute) RouteDraw = Math.max(0, RouteDraw - Wall_ * 2);
        const Count = RouteMesh.geometry.index.count;
        RouteMesh.geometry.setDrawRange(0, Math.floor(Count * RouteDraw / 6) * 6);
        const Shown = RouteDraw > 0.995 ? 1 : 0;
        BeaconBeam.material.opacity = 0.55 * Shown * Live.Grid;
        const Ripple = (Clock * 0.8) % 1;
        BeaconRing.scale.setScalar(1 + Ripple * 2.4);
        BeaconRing.material.opacity = (1 - Ripple) * Shown * Live.Grid;
        const Travel = Condition.Guiding ? (Clock * 0.12) % 1 : RouteDraw;
        Comet.position.copy(RoutePath.getPointAt(Math.min(0.999, Travel))).setY(0.2);
        Comet.material.opacity = RouteDraw > 0.01 && RouteDraw < 1 || Condition.Guiding ? Live.Grid : 0;
    }

    // Lock wave and lamp blinks.
    const WaveAge = Clock - LockWaveAt;
    LockWave.material.opacity = WaveAge < 1.6 ? (1 - WaveAge / 1.6) * 0.9 : 0;
    LockWave.scale.setScalar(1 + WaveAge * 6);
    const BlinkAge = Clock - BlinkAt;
    const Blink = BlinkAge < 0.9 ? (Math.sin(BlinkAge * Math.PI * 4.4) > 0 ? 1 : 0) : 0;
    Finish[Family.Headlight].emissiveIntensity = 0.3 + Blink * 2.5;
    Finish[Family.Taillight].emissiveIntensity = 1.4 + Blink * 3;

    // Lids hinge open with a soft overshoot; outlines show in Access or while open.
    for (const [Name, Lid] of Object.entries(Lids))
    {
        const Goal = Condition.Lids[Name] ? 0.62 : 0;
        Lid.Angle += (Goal - Lid.Angle) * Follow(4, Wall_);
        Lid.Group.rotation.z = Lid.Forward * Lid.Angle;
        const Show = ((Condition.Menu === 'Access' ? 0.5 : 0) + (Condition.Lids[Name] ? 0.5 : 0)) * (Condition.Menu === 'Seats' ? 0.15 : 1);
        Lid.Edge.material.opacity += (Show * 1.6 - Lid.Edge.material.opacity) * Ease;
        Lid.Fill.material.opacity += (Show * 0.18 - Lid.Fill.material.opacity) * Ease;
    }
    PortGlow.material.opacity = Condition.PortOpen ? 0.6 + 0.4 * Math.sin(Clock * 3) : 0;
    if (Condition.PortOpen && Condition.Charge < 100)
    {
        Condition.Charge = Math.min(100, Condition.Charge + Wall_ * 0.6);
        Find('ChargeText').textContent = Math.floor(Condition.Charge);
        Find('ChargeArc').style.strokeDashoffset = `${201 * (1 - Condition.Charge / 100)}`;
    }

    // Ghosted cabin and seats.
    const Ghost = Live.Ghost;
    for (const Material of Ghosted)
    {
        const Clear = Ghost > 0.01;
        if (Material.transparent !== Clear) { Material.transparent = Clear; Material.needsUpdate = true; }
        Material.opacity    = 1 - Ghost * 0.86;
        Material.depthWrite = !Clear;
    }
    BodyEdges.material.opacity = Ghost * 0.6;
    SeatGroup.visible = Ghost > 0.02;
    SeatFinish.opacity = DriverFinish.opacity = Ghost;
    DriverFinish.emissive.copy(Condition.Heat ? HeatGlow : Condition.Vent ? VentGlow : Accent);
    DriverFinish.emissiveIntensity = Condition.Heat ? 0.35 + Condition.Heat * 0.25 : Condition.Vent ? 0.3 + Condition.Vent * 0.22 : 0.55;
    DriverSeat.Pivot.rotation.z += (THREE.MathUtils.degToRad(Condition.Recline) - DriverSeat.Pivot.rotation.z) * Follow(5, Wall_);
    DriverSeat.Seat.position.x  += (0.05 + Condition.Slide - DriverSeat.Seat.position.x) * Follow(5, Wall_);
    MassageDots.material.opacity = Condition.Massage === 'off' ? 0 : Ghost;
    if (Condition.Massage !== 'off')
    {
        const Points = MassageDots.geometry.attributes.position;
        for (let Index = 0; Index < 16; Index++)
        {
            const Column = Index % 2, Row = Math.floor(Index / 2);
            const Travel = Condition.Massage === 'wave' ? ((Clock * 0.5 + Row / 8) % 1) : (Row / 8 + Math.sin(Clock * 3 + Row) * 0.03);
            const Squeeze = Condition.Massage === 'knead' ? Math.sin(Clock * 4 + Row) * 0.05 : 0;
            Points.setXYZ(Index, 0.07, 0.08 + Travel * 0.5, (Column ? 0.11 : -0.11) + (Column ? -Squeeze : Squeeze));
        }
        Points.needsUpdate = true;
    }

    Orbit.Advance(Delta);
    Mirror.Sync();
    PlacePins();
    Renderer.render(Scene, Camera);
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    BOOT
//------------------------------------------------------------------------------------------------------------------------

function RefreshClock() { Find('Clock').textContent = ClockText(); }
RefreshClock();
setInterval(RefreshClock, 10000);
SetLight(0);
SetRoute(0);
SetPreset(0);
OpenMenu('Studio');
Find('Toast').classList.remove('Show');

window.HmiCondition = { Condition, OpenMenu, SetLocked, SetLight, SetRoute, SetLid, SetPreset, PlayWelcome, Orbit };

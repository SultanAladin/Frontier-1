// LOBBY — PROCEDURAL GARAGE CONFIGURATOR
// 🧩 Game prototype: the ControlVehicle parked in a procedural garage set. Orbit, pan and zoom the camera, swing the key
//    light, switch between nine garage sets, customise each set (seeded) and customise the car like a game car
//    configurator: white hotspot dots on the car fly the camera to a close-up and open that option group, and drivetrain
//    changes fade the body to a see-through shell over a live cutaway of the engine, gearbox and driven axles.
//    The car always sits at the origin facing +X; sets are rebuilt around it.
// 📝 Reflections come from the set itself: after every rebuild the empty garage is rendered into a PMREM environment
//    from the car's position, so the paint picks up that garage's lamps, sky and walls. Glossy floors show a mirror twin.

import { THREE, AssembleVehicle, DefaultFinish, Family, MirrorTwin, EaseColour } from '../Shared/VehicleAssembly.js';
import { Sets, SetByKey, Defaults } from './LobbySets.js';
import { Seeded, SoftSpot, Release } from './LobbyKit.js';
import { MountParts, XraySkin, MountDrivetrain, DriveFigures, DriveNames } from './LobbyCar.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const Radian = Math.PI / 180;
const MinDistance = 3.6, MaxDistance = 30;
const CarTarget = new THREE.Vector3(0, 0.8, 0);

/** Camera presets as orbit angles around the car (Yaw 0 looks at the nose, 90 at the right side seen from +Z). */
const Presets = [
    { Id: 'Set', Label: 'Set view' },
    { Id: 'Front', Label: 'Front ¾', Yaw: 40, Pitch: 7, Distance: 10.5, Target: [0.3, 0.75, 0] },
    { Id: 'Side', Label: 'Side', Yaw: 90, Pitch: 3, Distance: 10.5, Target: [0, 0.8, 0] },
    { Id: 'Rear', Label: 'Rear ¾', Yaw: 145, Pitch: 9, Distance: 10.5, Target: [-0.3, 0.8, 0] },
    { Id: 'Top', Label: 'Top', Yaw: 70, Pitch: 68, Distance: 12, Target: [0, 0.4, 0] },
    { Id: 'Low', Label: 'Low', Yaw: 26, Pitch: 0.5, Distance: 9.5, Target: [0.4, 0.6, 0] },
];

/** Car options in configurator groups, written in the same control format as the set controls so one renderer draws both. */
const Swatches = (List) => List.map(([Id, Colour]) => ({ Id, Colour }));
const CarGroups = [
    { Id: 'Body', Title: 'Paint', Hint: 'Colour and finish', Controls: [
        { Key: 'Paint', Label: 'Paint', Type: 'Swatch', Default: 'Acid', Options: Swatches([
            ['Acid', '#c9d63a'], ['Signal red', '#b8141c'], ['Lava', '#ff5a12'], ['Chalk', '#d9d6cc'], ['Electric blue', '#1d5cff'],
            ['Racing green', '#0f3b2a'], ['Violet', '#5a2a8a'], ['Liquid silver', '#a7adb4'], ['Obsidian', '#0b0c0e']]) },
        { Key: 'Coat', Label: 'Paint finish', Type: 'Choice', Default: 'Gloss', Options: ['Gloss', 'Metallic', 'Satin'] },
    ] },
    { Id: 'Wheels', Title: 'Wheels', Hint: 'Rims, tyres and brakes', Controls: [
        { Key: 'Rims', Label: 'Rim colour', Type: 'Swatch', Default: 'Silver', Options: Swatches([
            ['Silver', '#8d939b'], ['Gunmetal', '#3b3f45'], ['Gold', '#b8913a'], ['Gloss black', '#121316'], ['White', '#e8e8e8'], ['Bronze', '#7a5230'], ['Lime', '#b8ff3c']]) },
        { Key: 'Tyres', Label: 'Tyre colour', Type: 'Swatch', Default: 'Black', Options: Swatches([
            ['Black', '#141414'], ['Graphite', '#3a3c40'], ['Tan', '#8a6a48'], ['Red', '#7a1418'], ['Blue', '#1c3566'], ['Olive', '#4a5230']]) },
        { Key: 'Calipers', Label: 'Brake calipers', Type: 'Swatch', Default: 'Rust', Options: Swatches([
            ['Rust', '#7a1f0b'], ['Red', '#c1121f'], ['Yellow', '#ffcc00'], ['Lime', '#b8ff3c'], ['Blue', '#1f6bff'], ['Black', '#151515']]) },
    ] },
    { Id: 'Lights', Title: 'Lights', Hint: 'Head and tail light colour', Controls: [
        { Key: 'Lamps', Label: 'Lights on', Type: 'Toggle', Default: true },
        { Key: 'Headlights', Label: 'Headlight colour', Type: 'Swatch', Default: 'White', Options: Swatches([
            ['White', '#ffffff'], ['Xenon ice', '#bcd8ff'], ['Halogen', '#ffd49a'], ['Yellow', '#ffc21a'], ['Blue', '#4f7dff'], ['Violet', '#b06bff']]) },
        { Key: 'Taillights', Label: 'Tail light colour', Type: 'Swatch', Default: 'Red', Options: Swatches([
            ['Red', '#ff1010'], ['Smoked', '#5a0606'], ['Amber', '#ff8a00'], ['Purple', '#b02bff'], ['Blue', '#2a6bff'], ['White', '#ffffff']]) },
    ] },
    { Id: 'Glow', Title: 'Underglow', Hint: 'Neon under the sills', Controls: [
        { Key: 'Glow', Label: 'Underglow', Type: 'Toggle', Default: false },
        { Key: 'GlowColour', Label: 'Glow colour', Type: 'Swatch', Default: 'Cyan', Options: Swatches([
            ['Cyan', '#36e6ff'], ['Lime', '#b8ff3c'], ['Magenta', '#ff3fd2'], ['Violet', '#8a5cff'], ['Red', '#ff2a3a'], ['Amber', '#ff9a1a'], ['White', '#ffffff']]) },
        { Key: 'GlowMode', Label: 'Glow mode', Type: 'Choice', Default: 'Steady', Options: ['Steady', 'Breathe', 'Cycle'] },
    ] },
    { Id: 'Parts', Title: 'Components', Hint: 'Aero and roof parts', Controls: [
        { Key: 'Wing', Label: 'Rear wing', Type: 'Toggle', Default: false },
        { Key: 'Splitter', Label: 'Front splitter', Type: 'Toggle', Default: false },
        { Key: 'Skirts', Label: 'Side skirts', Type: 'Toggle', Default: false },
        { Key: 'Rack', Label: 'Roof rack', Type: 'Toggle', Default: false },
        { Key: 'Bar', Label: 'Roof light bar', Type: 'Toggle', Default: false },
    ] },
    { Id: 'Drive', Title: 'Drivetrain', Hint: 'Engine, layout and gearbox', Controls: [
        { Key: 'Engine', Label: 'Engine', Type: 'Choice', Default: 'Inline-6', Options: ['Inline-6', 'V8', 'Electric'] },
        { Key: 'Layout', Label: 'Driven wheels', Type: 'Choice', Default: 'RWD', Options: ['RWD', 'FWD', 'AWD'] },
        { Key: 'Gearbox', Label: 'Gearbox', Type: 'Choice', Default: 'Manual', Options: ['Manual', 'Automatic'] },
    ] },
];
const CarControls = CarGroups.flatMap((Group) => Group.Controls);
const DriveKeys = ['Engine', 'Layout', 'Gearbox'];
const PartKeys = ['Lamps', 'Glow', 'GlowColour', 'GlowMode', 'Rack', 'Bar', 'Splitter', 'Skirts'];

/**
 * Hotspots: white dots on the car. At and Normal are car-local (ground heights); Mirror dots follow the camera to the
 * near side of the car. View is the close-up the camera flies to; Group is the option group that opens.
 */
const Spots = [
    { Id: 'Paint', Label: 'Paint', Group: 'Body', At: [0.35, 0.98, 1.2], Normal: [0, 0.25, 1], Mirror: true, View: { Yaw: 74, Pitch: 9, Distance: 8.8, Target: [0, 0.8, 0] } },
    { Id: 'Headlights', Label: 'Headlights', Group: 'Lights', At: [3.04, 0.95, 0.92], Normal: [1, 0.1, 0.4], Mirror: true, View: { Yaw: 30, Pitch: 6, Distance: 5.4, Target: [2.4, 0.85, 0.5] } },
    { Id: 'Taillights', Label: 'Tail lights', Group: 'Lights', At: [-3.1, 1.09, 0.92], Normal: [-1, 0.1, 0.4], Mirror: true, View: { Yaw: 150, Pitch: 8, Distance: 5.4, Target: [-2.4, 0.95, 0.5] } },
    { Id: 'Wheels', Label: 'Wheels', Group: 'Wheels', At: [1.727, 0.62, 1.3], Normal: [0, 0, 1], Mirror: true, View: { Yaw: 66, Pitch: 4, Distance: 4.6, Target: [1.6, 0.55, 0.9] } },
    { Id: 'Glow', Label: 'Underglow', Group: 'Glow', At: [-0.4, 0.3, 1.24], Normal: [0, -0.1, 1], Mirror: true, View: { Yaw: 35, Pitch: 1.5, Distance: 8.6, Target: [0.3, 0.42, 0] } },
    { Id: 'Aero', Label: 'Aero', Group: 'Parts', At: [-2.8, 1.3, 0], Normal: [-0.5, 1, 0], View: { Yaw: 156, Pitch: 20, Distance: 6.8, Target: [-2.1, 1.15, 0] } },
    { Id: 'Roof', Label: 'Roof', Group: 'Parts', At: [0, 1.8, 0], Normal: [0, 1, 0], View: { Yaw: 48, Pitch: 34, Distance: 7.2, Target: [0.2, 1.45, 0] } },
    { Id: 'Drive', Label: 'Drivetrain', Group: 'Drive', At: [2.2, 1.16, 0], Normal: [0.3, 1, 0], Mirror: true, Cutaway: true, View: { Yaw: 58, Pitch: 26, Distance: 9.2, Target: [0.1, 0.7, 0] } },
];

const Coats = {
    Gloss:    { roughness: 0.28, metalness: 0.35, clearcoat: 1, clearcoatRoughness: 0.06 },
    Metallic: { roughness: 0.2, metalness: 0.8, clearcoat: 1, clearcoatRoughness: 0.03 },
    Satin:    { roughness: 0.56, metalness: 0.25, clearcoat: 0.2, clearcoatRoughness: 0.5 },
};
const RimSheen = { 'Gloss black': { roughness: 0.18, metalness: 0.6 }, White: { roughness: 0.3, metalness: 0.15 }, Lime: { roughness: 0.3, metalness: 0.3 } };

/** Key light options. KeyTone 'Set' keeps the colour the set gave its key light. */
const LightControls = [
    { Key: 'Elevation', Label: 'Key height', Type: 'Range', Min: 4, Max: 88, Step: 1, Default: 45, Unit: '°' },
    { Key: 'KeyTone', Label: 'Key colour', Type: 'Swatch', Default: 'Set', Options: [
        { Id: 'Set', Colour: 'conic-gradient(#fff 0 25%, #ffd29a 0 50%, #cfe0ff 0 75%, #ffb36b 0)' },
        { Id: 'Tungsten', Colour: '#ffbe7d' }, { Id: 'Daylight', Colour: '#ffffff' }, { Id: 'Overcast', Colour: '#cddcff' },
        { Id: 'Sodium', Colour: '#ff9a3c' }, { Id: 'Neon pink', Colour: '#ff4fd8' }, { Id: 'Cyan', Colour: '#3ff0ff' },
    ] },
    { Key: 'KeyPower', Label: 'Key intensity', Type: 'Range', Min: 0, Max: 2.5, Step: 0.05, Default: 1, Unit: '×' },
    { Key: 'Fills', Label: 'Fills and practicals', Type: 'Range', Min: 0, Max: 2.5, Step: 0.05, Default: 1, Unit: '×' },
    { Key: 'Reflections', Label: 'Reflections', Type: 'Range', Min: 0, Max: 2, Step: 0.05, Default: 1, Unit: '×' },
    { Key: 'Exposure', Label: 'Exposure', Type: 'Range', Min: 0.4, Max: 2, Step: 0.02, Default: 1, Unit: '×' },
];

const CameraControls = [
    { Key: 'Orbit', Label: 'Auto orbit', Type: 'Toggle', Default: false },
    { Key: 'Lens', Label: 'Lens', Type: 'Range', Min: 18, Max: 85, Step: 1, Default: 35, Unit: ' mm' },
    { Key: 'Spots', Label: 'Hotspot dots', Type: 'Toggle', Default: true },
];

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDERER AND SCENE
//------------------------------------------------------------------------------------------------------------------------

const Canvas = document.getElementById('Scene');
const Renderer = new THREE.WebGLRenderer({ canvas: Canvas, antialias: true });
Renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
Renderer.toneMapping = THREE.ACESFilmicToneMapping;
Renderer.outputColorSpace = THREE.SRGBColorSpace;
Renderer.shadowMap.enabled = true;
Renderer.shadowMap.type = THREE.PCFShadowMap;

const Scene = new THREE.Scene();
const Camera = new THREE.PerspectiveCamera(38, 1, 0.1, 1000);

// The car deck: everything that belongs to the car and survives set rebuilds.
const Deck = new THREE.Group();
Scene.add(Deck);
// What rides on the workshop lift: the car, its beams and the drivetrain cutaway.
const Lifted = new THREE.Group();
Deck.add(Lifted);
const Drive = MountDrivetrain();
Lifted.add(Drive.Group);

// Contact shadow on top of the floor, and a veil just under the floor that dims the mirror twin on less glossy floors.
const Contact = new THREE.Mesh(new THREE.PlaneGeometry(7.8, 3.9).rotateX(-Math.PI / 2),
    new THREE.MeshBasicMaterial({ map: SoftSpot({ Inner: 'rgba(0,0,0,0.92)', Outer: 'rgba(0,0,0,0)' }), transparent: true, depthWrite: false, opacity: 0.72, toneMapped: false }));
Contact.position.y = 0.012;
Contact.renderOrder = 5;
const Veil = new THREE.Mesh(new THREE.PlaneGeometry(12, 6.4).rotateX(-Math.PI / 2),
    new THREE.MeshBasicMaterial({ map: SoftSpot({ Inner: 'rgba(0,0,0,1)', Outer: 'rgba(0,0,0,0)' }), transparent: true, depthWrite: false, opacity: 0, toneMapped: false }));
Veil.position.y = -0.006;
Veil.renderOrder = -1;                                              // drawn before the transparent floors above it
Deck.add(Contact, Veil);

// Headlight beams (the lamp glass itself is the Headlight material family).
const Beams = [-0.72, 0.72].map((Side) =>
{
    const Lamp = new THREE.SpotLight('#eef3ff', 0, 40, 0.42, 0.55, 1.6);
    Lamp.position.set(2.95, 0.72, Side);
    Lamp.target.position.set(14, 0, Side * 1.6);
    Lifted.add(Lamp, Lamp.target);
    return Lamp;
});

const Finish = DefaultFinish();
// The ControlVehicle's black roof panel shares the rubber family; the wheels get their own copy so tyre colour stays on the tyres.
const TyreFinish = Finish[Family.Tyre].clone();
let Car = null, Twin = null, Parts = null, Skin = null;
let Cutaway = 0, CutawayGoal = 0;  // see-through level of the body, eased towards the goal

//------------------------------------------------------------------------------------------------------------------------
//                                                    STATE
//------------------------------------------------------------------------------------------------------------------------

const ValuesOf = (Controls) => Object.fromEntries(Controls.map((Control) => [Control.Key, Control.Default]));
const Search = new URLSearchParams(location.search);

/** Per-set memory so switching sets and back restores the player's garage. */
const Memory = Object.fromEntries(Sets.map((Set) => [Set.Key, { Options: Defaults(Set), Seed: 1 }]));
const State = {
    Key: SetByKey[Search.get('Set')] ? Search.get('Set') : Sets[0].Key,
    Look: ValuesOf(CarControls),
    Rig: ValuesOf(LightControls),
    View: ValuesOf(CameraControls),
    Azimuth: 0,
};
if (Search.has('Seed')) Memory[State.Key].Seed = Math.max(1, parseInt(Search.get('Seed'), 10) || 1);

let Built = null;                // the current set's Build() result
let KeyBase = null;              // { Colour, Power } the set gave its key light
let Environment = null;          // PMREM render target of the current set

//------------------------------------------------------------------------------------------------------------------------
//                                                    CAR
//------------------------------------------------------------------------------------------------------------------------

async function MountCar()
{
    const Next = await AssembleVehicle({ Finish, Wing: State.Look.Wing });
    Next.Root.traverse((Each) => { if (Each.isMesh) { Each.castShadow = true; Each.receiveShadow = true; } });
    const Rubber = (Material) => (Material === Finish[Family.Tyre] ? TyreFinish : Material);
    for (const Wheel of Next.Wheels) Wheel.Steer.traverse((Each) => { if (Each.isMesh) Each.material = Array.isArray(Each.material) ? Each.material.map(Rubber) : Rubber(Each.material); });
    const NextParts = MountParts(Next);
    const NextSkin = XraySkin(Next);
    const NextTwin = MirrorTwin(Next.Root);
    NextTwin.Sync();
    NextTwin.Group.traverse((Each) => { if (Each.isMesh) { Each.castShadow = false; Each.receiveShadow = false; } });
    if (Car)
    {
        Lifted.remove(Car.Root);
        Deck.remove(Twin.Group, Parts.Ground);
        Car.Root.traverse((Each) => Each.geometry?.dispose());      // materials are the shared Finish; keep them
    }
    Car = Next;
    Twin = NextTwin;
    Parts = NextParts;
    Skin = NextSkin;
    Lifted.add(Car.Root);
    Deck.add(Twin.Group, Parts.Ground);
    Parts.Apply(PartLook());
    Parts.Ground.userData.Fade = 1 - Cutaway;
    Skin.Set(Cutaway);
    if (Built) ApplyLift();
    ApplyMirror();
}

/** The car options the bolt-on parts read, with the glow colour resolved to a hex colour. */
function PartLook()
{
    return { ...State.Look, GlowColour: Pick('GlowColour').Colour };
}

const Pick = (Key) => CarControls.find((Each) => Each.Key === Key).Options.find((Option) => Option.Id === State.Look[Key]);
const DriveSpec = () => ({ Engine: State.Look.Engine, Layout: State.Look.Layout, Gearbox: State.Look.Gearbox });

/** Applies a car option. Colours ease; geometry options (the wing) rebuild the car. */
async function ApplyLook(Key, Immediate = false)
{
    const Duration = Immediate ? 1 : 520;
    if (Key === 'Paint') EaseColour(Finish[Family.BodyCoat].color, Pick('Paint').Colour, Duration);
    if (Key === 'Coat') Finish[Family.BodyCoat].setValues(Coats[State.Look.Coat]);
    if (Key === 'Rims')
    {
        Finish[Family.Hub].setValues(RimSheen[State.Look.Rims] || { roughness: 0.22, metalness: 1 });
        EaseColour(Finish[Family.Hub].color, Pick('Rims').Colour, Duration);
    }
    if (Key === 'Calipers') EaseColour(Finish[Family.Brake].color, Pick('Calipers').Colour, Duration);
    if (Key === 'Tyres')
    {
        TyreFinish.setValues(State.Look.Tyres === 'Black' ? { roughness: 0.86 } : { roughness: 0.62 });
        EaseColour(TyreFinish.color, Pick('Tyres').Colour, Duration);
    }
    if (Key === 'Lamps' || Key === 'Headlights')
    {
        const Tone = Pick('Headlights').Colour;
        EaseColour(Finish[Family.Headlight].emissive, Tone, Duration);
        EaseColour(Finish[Family.Headlight].color, new THREE.Color(Tone).lerp(new THREE.Color('#ffffff'), 0.5), Duration);
        Finish[Family.Headlight].emissiveIntensity = State.Look.Lamps ? 2.2 : 0.05;
        for (const Lamp of Beams) { Lamp.color.set(Tone).lerp(new THREE.Color('#eef3ff'), 0.25); Lamp.intensity = State.Look.Lamps ? 140 : 0; }
    }
    if (Key === 'Lamps' || Key === 'Taillights')
    {
        const Smoked = State.Look.Taillights === 'Smoked';
        const Tone = Smoked ? '#c00000' : Pick('Taillights').Colour;
        EaseColour(Finish[Family.Taillight].emissive, Tone, Duration);
        EaseColour(Finish[Family.Taillight].color, Smoked ? '#1a0505' : Tone, Duration);
        const Vivid = ['Red', 'White'].includes(State.Look.Taillights) ? 1 : 0.55;   // saturated colours wash out to white when pushed hard
        Finish[Family.Taillight].emissiveIntensity = (State.Look.Lamps ? 2.4 : 0.35) * (Smoked ? 0.45 : Vivid);
    }
    if (PartKeys.includes(Key)) Parts?.Apply(PartLook());
    if (DriveKeys.includes(Key))
    {
        Drive.Build(DriveSpec());
        MakeTags();
        ShowFigures();
        if (!Immediate) FocusSpot('Drive', { Fly: Focus !== 'Drive' });
    }
    if (Key === 'Wing') await MountCar();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SETS
//------------------------------------------------------------------------------------------------------------------------

/** Renders the empty garage (no car) into a PMREM cube from the car's position: the paint reflects this garage. */
function CaptureEnvironment()
{
    Deck.visible = false;
    Scene.environment = null;
    const Filter = new THREE.PMREMGenerator(Renderer);
    const Target = Filter.fromScene(Scene, 0.035, 0.1, 900, { size: 256, position: new THREE.Vector3(0, 1.1, 0) });
    Filter.dispose();
    Environment?.dispose();
    Environment = Target;
    Scene.environment = Target.texture;
    Deck.visible = true;
}

/** Builds (or rebuilds) the current set from its remembered options and seed. KeepView keeps the camera where it is. */
function BuildSet({ KeepView = false } = {})
{
    const Set = SetByKey[State.Key];
    const { Options, Seed } = Memory[State.Key];
    const Next = Set.Build(Options, Seeded(Seed));

    if (Built)
    {
        Scene.remove(Built.Group);
        Release(Built.Group);
        if (Built.Background?.isTexture) Built.Background.dispose();
    }
    Built = Next;
    Scene.add(Built.Group);
    Scene.background = Built.Background;
    Scene.fog = Built.Fog;
    KeyBase = { Colour: Built.Key.Light.color.clone(), Power: Built.Key.Light.intensity };
    for (const Fill of Built.Fills) Fill.Power ??= Fill.Light.intensity;
    if (!KeepView || State.Azimuth === null) { State.Azimuth = Built.Key.Azimuth; State.Rig.Elevation = Built.Key.Elevation; }

    ApplyRig();
    CaptureEnvironment();
    ApplyLift();
    ApplyMirror();
    if (!KeepView) Aim(Built.View, true);
    RefreshSetUi();
    history.replaceState(null, '', `?Set=${State.Key}&Seed=${Seed}`);
}

/** Sets with a lift (the workshop) raise the car; the twin drops by the same amount and the contact shadow fades. */
function ApplyLift()
{
    const Lift = Built.Lift ?? 0;
    Lifted.position.y = Lift;
    Twin.Group.position.y = -Lift;
    Contact.material.opacity = 0.72 * (1 - Math.min(1, Lift / 1.2) * 0.75);
    Contact.scale.setScalar(1 + Lift * 0.25);
}

/** Mirror twin strength follows the set's floor: hidden on dry matte floors, dimmed by the veil in between. */
function ApplyMirror()
{
    if (!Twin || !Built) return;
    const Mirror = THREE.MathUtils.clamp(Built.Mirror ?? 0, 0, 1);
    Twin.Group.visible = Mirror > 0.04 && Cutaway < 0.02;
    Veil.material.opacity = (1 - Mirror) * 0.92;
}

/** Positions and tints the key light, scales the fills and sets exposure and reflection strength. */
function ApplyRig()
{
    if (!Built) return;
    const { Light, Reach } = Built.Key;
    const Elevation = State.Rig.Elevation * Radian, Azimuth = State.Azimuth * Radian;
    Light.position.set(Math.cos(Elevation) * Math.cos(Azimuth), Math.sin(Elevation), Math.cos(Elevation) * Math.sin(Azimuth)).multiplyScalar(Reach);
    Light.updateMatrixWorld();
    const Tone = LightControls[1].Options.find((Option) => Option.Id === State.Rig.KeyTone);
    Light.color.copy(State.Rig.KeyTone === 'Set' ? KeyBase.Colour : new THREE.Color(Tone.Colour));
    Light.intensity = KeyBase.Power * State.Rig.KeyPower;
    for (const Fill of Built.Fills) Fill.Light.intensity = Fill.Power * State.Rig.Fills;
    Renderer.toneMappingExposure = Built.Exposure * State.Rig.Exposure;
    Scene.environmentIntensity = Built.Reflection * State.Rig.Reflections;
    DrawPlot();
}

let Switching = false;
/** Switches set with a short fade to black so the rebuild hitch is hidden. */
async function UseSet(Key)
{
    if (!SetByKey[Key] || Switching) return;
    if (Key === State.Key && Built) return;
    Switching = true;
    Curtain.classList.add('Shut');
    await Wait(230);
    State.Key = Key;
    BuildSet();
    await Wait(60);
    Curtain.classList.remove('Shut');
    Switching = false;
}

const Wait = (Milliseconds) => new Promise((Resolve) => setTimeout(Resolve, Milliseconds));

let RebuildTimer = 0;
/** Rebuilds the set after an option change; slider drags are debounced. */
function Rebuild(Delay = 0)
{
    clearTimeout(RebuildTimer);
    RebuildTimer = setTimeout(() => BuildSet({ KeepView: true }), Delay);
}

function RandomValue(Control)
{
    const Pick = (List) => List[Math.floor(Math.random() * List.length)];
    if (Control.Type === 'Choice') return Pick(Control.Options);
    if (Control.Type === 'Swatch') return Pick(Control.Options).Id;
    if (Control.Type === 'Toggle') return Math.random() < (Control.Default ? 0.75 : 0.4);
    const Steps = Math.round((Control.Max - Control.Min) / Control.Step);
    return +(Control.Min + Math.round(Math.random() * Steps) * Control.Step).toFixed(4);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ORBIT CAMERA
//------------------------------------------------------------------------------------------------------------------------

const Orbit = { Yaw: 35, Pitch: 8, Distance: 11, Target: CarTarget.clone() };
const Goal = { Yaw: 35, Pitch: 8, Distance: 11, Target: CarTarget.clone() };
let ActivePreset = 'Set';

/** Sends the camera to a view. Snap jumps there (used on set switches, behind the curtain). */
function Aim(View, Snap = false)
{
    Goal.Yaw = Orbit.Yaw + Shortest(Orbit.Yaw, View.Yaw);
    Goal.Pitch = View.Pitch;
    Goal.Distance = View.Distance;
    Goal.Target.set(...(View.Target || [0, 0.8, 0]));
    if (Snap) { Orbit.Yaw = View.Yaw; Goal.Yaw = View.Yaw; Orbit.Pitch = Goal.Pitch; Orbit.Distance = Goal.Distance; Orbit.Target.copy(Goal.Target); }
}

const Shortest = (From, To) => ((((To - From) % 360) + 540) % 360) - 180;

function UsePreset(Id)
{
    ActivePreset = Id;
    const Preset = Presets.find((Each) => Each.Id === Id);
    Aim(Id === 'Set' ? Built.View : Preset);
    RefreshPresets();
}

const Scratch = new THREE.Vector3();
function PlaceCamera(Seconds)
{
    if (State.View.Orbit) Goal.Yaw += Seconds * 7;
    const Ease = 1 - Math.exp(-Seconds * 7);
    Orbit.Yaw += (Goal.Yaw - Orbit.Yaw) * Ease;
    Orbit.Pitch += (Goal.Pitch - Orbit.Pitch) * Ease;
    Orbit.Distance += (Goal.Distance - Orbit.Distance) * Ease;
    Orbit.Target.lerp(Goal.Target, Ease);
    const Yaw = Orbit.Yaw * Radian, Pitch = Orbit.Pitch * Radian;
    Scratch.set(Math.cos(Pitch) * Math.cos(Yaw), Math.sin(Pitch), Math.cos(Pitch) * Math.sin(Yaw)).multiplyScalar(Orbit.Distance).add(Orbit.Target);
    if (Built)
    {
        // Stay inside the garage: the camera may not leave the set's bounds (walls, ceiling, floor).
        const { X, Y, Z } = Built.Bounds;
        Scratch.set(THREE.MathUtils.clamp(Scratch.x, X[0], X[1]), THREE.MathUtils.clamp(Scratch.y, Y[0], Y[1]), THREE.MathUtils.clamp(Scratch.z, Z[0], Z[1]));
    }
    Camera.position.copy(Scratch);
    Camera.lookAt(Orbit.Target);
}

// Pointer input: left drag orbits, right drag (or shift + left) pans, wheel zooms.
let Drag = null;
Canvas.addEventListener('contextmenu', (Event) => Event.preventDefault());
Canvas.addEventListener('pointerdown', (Event) =>
{
    Drag = { X: Event.clientX, Y: Event.clientY, Pan: Event.button === 2 || Event.shiftKey };
    Canvas.setPointerCapture(Event.pointerId);
    Canvas.classList.add('Dragging');
    ActivePreset = null;
    RefreshPresets();
});
Canvas.addEventListener('pointermove', (Event) =>
{
    if (!Drag) return;
    const DeltaX = Event.clientX - Drag.X, DeltaY = Event.clientY - Drag.Y;
    Drag.X = Event.clientX; Drag.Y = Event.clientY;
    if (Drag.Pan)
    {
        const Scale = Orbit.Distance * 0.0016;
        const Right = new THREE.Vector3().setFromMatrixColumn(Camera.matrixWorld, 0);
        Goal.Target.addScaledVector(Right, -DeltaX * Scale);
        Goal.Target.y += DeltaY * Scale;
        Goal.Target.set(THREE.MathUtils.clamp(Goal.Target.x, -4.5, 4.5), THREE.MathUtils.clamp(Goal.Target.y, 0.2, 2.6), THREE.MathUtils.clamp(Goal.Target.z, -3.5, 3.5));
    }
    else
    {
        Goal.Yaw += DeltaX * 0.32;
        Goal.Pitch = THREE.MathUtils.clamp(Goal.Pitch + DeltaY * 0.24, -4, 82);
    }
});
const EndDrag = () => { Drag = null; Canvas.classList.remove('Dragging'); };
Canvas.addEventListener('pointerup', EndDrag);
Canvas.addEventListener('pointercancel', EndDrag);
Canvas.addEventListener('wheel', (Event) =>
{
    Event.preventDefault();
    Goal.Distance = THREE.MathUtils.clamp(Goal.Distance * Math.exp(Event.deltaY * 0.0011), MinDistance, MaxDistance);
    ActivePreset = null;
    RefreshPresets();
}, { passive: false });

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTROL RENDERER
//------------------------------------------------------------------------------------------------------------------------

const Make = (Tag, Class, Text) => { const Node = document.createElement(Tag); if (Class) Node.className = Class; if (Text != null) Node.textContent = Text; return Node; };
const Shown = (Control, Value) => Control.Type === 'Range' ? `${(+Value).toFixed(Control.Step < 1 ? (Control.Step < 0.1 ? 2 : 1) : 0)}${Control.Unit || ''}` : Control.Type === 'Swatch' ? Value : '';

/**
 * Draws one control (Choice → segments, Range → slider, Toggle → switch, Swatch → colour chips).
 * @returns {{ Element: HTMLElement, Show: (Value) => void }}
 */
function Control(Spec, Value, Change)
{
    const Element = Make('div', 'Control');
    Element.dataset.control = Spec.Key;
    const Header = Make('header');
    const Label = Make('span', null, Spec.Label);
    const Output = Make('output');
    Header.append(Label, Output);
    Element.append(Header);
    let Show = () => {};

    if (Spec.Type === 'Choice')
    {
        const Row = Make('div', 'Segments');
        const Buttons = Spec.Options.map((Option) =>
        {
            const Button = Make('button', null, Option);
            Button.title = Option;
            Button.addEventListener('click', () => { Show(Option); Change(Option, true); });
            Row.append(Button);
            return Button;
        });
        Element.append(Row);
        Show = (Next) => Buttons.forEach((Button, Index) => Button.classList.toggle('On', Spec.Options[Index] === Next));
    }
    else if (Spec.Type === 'Range')
    {
        const Slider = Make('input');
        Object.assign(Slider, { type: 'range', min: Spec.Min, max: Spec.Max, step: Spec.Step });
        Slider.addEventListener('input', () => { Show(+Slider.value); Change(+Slider.value, false); });
        Slider.addEventListener('change', () => Change(+Slider.value, true));
        Element.append(Slider);
        Show = (Next) => { Slider.value = Next; Output.textContent = Shown(Spec, Next); Slider.style.setProperty('--Fill', `${((Next - Spec.Min) / (Spec.Max - Spec.Min)) * 100}%`); };
    }
    else if (Spec.Type === 'Toggle')
    {
        Element.classList.add('Inline');
        Header.remove();
        const Switch = Make('button', 'Switch');
        Switch.setAttribute('role', 'switch');
        Element.append(Label, Switch);
        let Current = Value;
        Switch.addEventListener('click', () => { Show(!Current); Change(Current, true); });
        Show = (Next) => { Current = !!Next; Switch.classList.toggle('On', Current); Switch.setAttribute('aria-checked', Current); };
    }
    else if (Spec.Type === 'Swatch')
    {
        const Row = Make('div', 'Chips');
        const Buttons = Spec.Options.map((Option) =>
        {
            const Button = Make('button', Option.Colour === 'transparent' ? 'Empty' : null);
            Button.style.background = Option.Colour;
            Button.title = Option.Id;
            Button.addEventListener('click', () => { Show(Option.Id); Change(Option.Id, true); });
            Row.append(Button);
            return Button;
        });
        Element.append(Row);
        Show = (Next) => { Output.textContent = Next; Buttons.forEach((Button, Index) => Button.classList.toggle('On', Spec.Options[Index].Id === Next)); };
    }
    Show(Value);
    return { Element, Show };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PANEL
//------------------------------------------------------------------------------------------------------------------------

const Curtain = document.getElementById('Curtain');
const SetControls = document.getElementById('SetControls');
const SeedInput = document.getElementById('Seed');
const Shows = { Car: {}, Light: {}, Camera: {} };

function RefreshSetUi()
{
    const Set = SetByKey[State.Key];
    document.getElementById('SetName').textContent = Set.Name;
    document.getElementById('SetTagline').textContent = Set.Tagline;
    const Source = Set.Reference.length ? `inspired by ${Set.Reference.map((Ref) => `Lobby-Ref-${Ref}`).join(' and ')}` : 'designed for the lobby (no reference image)';
    document.getElementById('Reference').textContent = `Procedural set ${Source}. The seed changes the layout (plants, props, wear) without changing your options.`;
    SeedInput.value = Memory[State.Key].Seed;
    SetControls.replaceChildren(...Set.Controls.map((Spec) => Control(Spec, Memory[State.Key].Options[Spec.Key], (Value, Final) =>
    {
        Memory[State.Key].Options[Spec.Key] = Value;
        Rebuild(Final ? 0 : 160);
    }).Element));
    for (const Button of Strip.children) Button.classList.toggle('On', Button.dataset.set === State.Key);
    Shows.Light.Elevation?.Show(State.Rig.Elevation);
}

// Set strip
const Strip = document.getElementById('Strip');
Sets.forEach((Set, Index) =>
{
    const Button = Make('button');
    Button.dataset.set = Set.Key;
    Button.title = Set.Tagline;
    const Swatch = Make('i');
    Swatch.style.background = Set.Swatch;
    Button.append(Swatch, Make('span', null, Set.Name), Make('em', null, String(Index + 1)));
    Button.addEventListener('click', () => UseSet(Set.Key));
    Strip.append(Button);
});

// Tabs
const Tabs = document.getElementById('Tabs');
function ShowTab(Name)
{
    for (const Button of Tabs.children) Button.classList.toggle('On', Button.dataset.tab === Name);
    for (const Sheet of document.querySelectorAll('.Sheet')) Sheet.classList.toggle('On', Sheet.dataset.sheet === Name);
    if (Name === 'Light') DrawPlot();
}
Tabs.addEventListener('click', (Event) => { if (Event.target.dataset.tab) ShowTab(Event.target.dataset.tab); });

// Seed row
SeedInput.addEventListener('change', () => { Memory[State.Key].Seed = Math.max(1, parseInt(SeedInput.value, 10) || 1); Rebuild(); });
document.getElementById('Reseed').addEventListener('click', () => { Memory[State.Key].Seed = 1 + Math.floor(Math.random() * 99998); Rebuild(); });
document.getElementById('Shuffle').addEventListener('click', () =>
{
    const Set = SetByKey[State.Key];
    Memory[State.Key] = { Options: Object.fromEntries(Set.Controls.map((Spec) => [Spec.Key, RandomValue(Spec)])), Seed: 1 + Math.floor(Math.random() * 99998) };
    Rebuild();
});

// Car sheet: one folding section per configurator group.
const CarSheet = document.getElementById('CarControls');
const Figures = Make('div', 'Figures');
for (const Group of CarGroups)
{
    const Section = Make('section', 'Group');
    Section.dataset.group = Group.Id;
    const Head = Make('button', 'GroupHead');
    Head.append(Make('span', null, Group.Title), Make('small', null, Group.Hint));
    Head.addEventListener('click', () => Section.classList.toggle('Open'));
    const Body = Make('div', 'Controls');
    Body.append(...Group.Controls.map((Spec) =>
    {
        const Made = Control(Spec, State.Look[Spec.Key], (Value) => { State.Look[Spec.Key] = Value; ApplyLook(Spec.Key); });
        Shows.Car[Spec.Key] = Made;
        return Made.Element;
    }));
    if (Group.Id === 'Drive')
    {
        const Show = Make('button', 'Wide', 'Show the cutaway');
        Show.id = 'ShowCutaway';
        Show.addEventListener('click', () => FocusSpot('Drive'));
        Body.append(Figures, Show);
    }
    Section.append(Head, Body);
    CarSheet.append(Section);
}
CarSheet.firstElementChild.classList.add('Open');

/** Opens one option group (closing the others) and scrolls it into view with a short highlight. */
function OpenGroup(Id)
{
    for (const Section of CarSheet.children)
    {
        const Match = Section.dataset.group === Id;
        Section.classList.toggle('Open', Match);
        Section.classList.remove('Pulse');
        if (Match) { void Section.offsetWidth; Section.classList.add('Pulse'); Section.scrollIntoView({ block: 'nearest' }); }
    }
}

/** Drivetrain figures in the panel and on the cutaway card. */
function ShowFigures()
{
    const Spec = DriveSpec(), Numbers = DriveFigures(Spec), Names = DriveNames(Spec);
    const Electric = Spec.Engine === 'Electric';
    Shows.Car.Gearbox?.Element.classList.toggle('Muted', Electric);
    const Rows = [
        ['Power', `${Numbers.Power} hp`, Numbers.Power / 700],
        ['Torque', `${Numbers.Torque} Nm`, Numbers.Torque / 1000],
        ['0–100 km/h', `${Numbers.Sprint.toFixed(1)} s`, 1 - (Numbers.Sprint - 2) / 4.5],
        ['Top speed', `${Numbers.Top} km/h`, Numbers.Top / 330],
        ['Weight', `${Numbers.Weight} kg`, Numbers.Weight / 2000],
        ['Handling', `${Numbers.Handling.toFixed(1)} / 10`, Numbers.Handling / 10],
    ];
    const Draw = (Into) => Into.replaceChildren(...Rows.map(([Label, Value, Share]) =>
    {
        const Row = Make('div', 'Figure');
        const Meter = Make('i');
        Meter.style.setProperty('--Share', `${Math.round(THREE.MathUtils.clamp(Share, 0.04, 1) * 100)}%`);
        Row.append(Make('span', null, Label), Make('b', null, Value), Meter);
        return Row;
    }));
    Draw(Figures);
    Draw(document.getElementById('SpecFigures'));
    document.getElementById('SpecTitle').textContent = Electric ? `${Names.Engine[0]} · ${Spec.Layout}` : `${Names.Engine[0]} · ${Spec.Gearbox} · ${Spec.Layout}`;
    document.getElementById('SpecNote').textContent = `${Names.Engine[1]} · ${Names.Layout[0].toLowerCase()}`;
}

// Light sheet
document.getElementById('LightControls').append(...LightControls.map((Spec) =>
{
    const Made = Control(Spec, State.Rig[Spec.Key], (Value) => { State.Rig[Spec.Key] = Value; ApplyRig(); });
    Shows.Light[Spec.Key] = Made;
    return Made.Element;
}));
document.getElementById('ResetLight').addEventListener('click', () => ResetRig());
function ResetRig()
{
    Object.assign(State.Rig, ValuesOf(LightControls), { Elevation: Built.Key.Elevation });
    State.Azimuth = Built.Key.Azimuth;
    for (const [Key, Made] of Object.entries(Shows.Light)) Made.Show(State.Rig[Key]);
    ApplyRig();
}

// Camera sheet
const PresetRow = document.getElementById('Presets');
for (const Preset of Presets)
{
    const Button = Make('button', null, Preset.Label);
    Button.dataset.preset = Preset.Id;
    Button.addEventListener('click', () => UsePreset(Preset.Id));
    PresetRow.append(Button);
}
function RefreshPresets() { for (const Button of PresetRow.children) Button.classList.toggle('On', Button.dataset.preset === ActivePreset); }
document.getElementById('CameraControls').append(...CameraControls.map((Spec) =>
{
    const Made = Control(Spec, State.View[Spec.Key], (Value) => { State.View[Spec.Key] = Value; ApplyLens(); });
    Shows.Camera[Spec.Key] = Made;
    return Made.Element;
}));
function ApplyLens()
{
    Camera.fov = 2 * Math.atan(12 / State.View.Lens) / Radian;   // full-frame 24 mm sensor height
    Camera.updateProjectionMatrix();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    HOTSPOTS AND CUTAWAY
//------------------------------------------------------------------------------------------------------------------------

const SpotLayer = document.getElementById('Spots');
const TagLayer = document.getElementById('Tags');
const FocusBar = document.getElementById('FocusBar');
let Focus = null;                                                   // Id of the hotspot the camera is parked on

const Dots = Spots.map((Spot) =>
{
    const Dot = Make('button', 'Dot');
    Dot.dataset.spot = Spot.Id;
    Dot.title = Spot.Label;
    Dot.append(Make('i'), Make('span', null, Spot.Label));
    Dot.addEventListener('click', () => FocusSpot(Spot.Id));
    SpotLayer.append(Dot);
    return Dot;
});

/** Near side of the car for mirrored dots: +1 when the camera is on the +Z side. */
const NearSide = () => (Camera.position.z >= 0 ? 1 : -1);

/** Flies to a hotspot's close-up, opens its option group and (for the drivetrain) fades in the cutaway. */
function FocusSpot(Id, { Fly = true } = {})
{
    const Spot = Spots.find((Each) => Each.Id === Id);
    if (!Spot || !Built) return;
    Focus = Id;
    if (Fly)
    {
        const Side = Spot.Mirror ? NearSide() : 1;
        const [X, Y, Z] = Spot.View.Target;
        Aim({ ...Spot.View, Yaw: Spot.View.Yaw * Side, Target: [X, Y + (Built.Lift ?? 0), Z * Side] });
        ActivePreset = null;
        RefreshPresets();
    }
    ShowTab('Car');
    OpenGroup(Spot.Group);
    SetCutaway(!!Spot.Cutaway);
    document.getElementById('FocusName').textContent = Spot.Cutaway ? 'Drivetrain cutaway' : Spot.Label;
    FocusBar.classList.add('On');
    for (const Dot of Dots) Dot.classList.toggle('Active', Dot.dataset.spot === Id);
}

/** Leaves the close-up: solid body again and the set's own view. */
function Overview()
{
    Focus = null;
    SetCutaway(false);
    FocusBar.classList.remove('On');
    for (const Dot of Dots) Dot.classList.remove('Active');
    if (Built) UsePreset('Set');
}
document.getElementById('Overview').addEventListener('click', Overview);

function SetCutaway(On)
{
    CutawayGoal = On ? 1 : 0;
    document.body.classList.toggle('Cutaway', On);
}

let TagNodes = [];
/** One overlay label per cutaway tag (engine, gearbox, layout…), rebuilt with the drivetrain. */
function MakeTags()
{
    TagNodes = Drive.Tags.map((Tag) =>
    {
        const Node = Make('div', 'Tag');
        Node.style.setProperty('--Tone', Tag.Tone);
        const Text = Make('p');
        Text.append(Make('b', null, Tag.Title), Make('small', null, Tag.Note));
        Node.append(Make('i'), Text);
        return Node;
    });
    TagLayer.replaceChildren(...TagNodes);
}

const Projected = new THREE.Vector3(), Facing = new THREE.Vector3(), Toward = new THREE.Vector3();
/** Screen position of a car-local point, or null when it is behind the camera. */
function ScreenOf(Point)
{
    Projected.copy(Point);
    Lifted.localToWorld(Projected);
    Toward.copy(Camera.position).sub(Projected).normalize();
    Projected.project(Camera);
    if (Projected.z > 1) return null;
    return [(Projected.x * 0.5 + 0.5) * window.innerWidth, (-Projected.y * 0.5 + 0.5) * window.innerHeight];
}

/**
 * Places the cutaway tags. Each label sits up and to the right of its part (left of it on the right half of the
 * screen); labels that would overlap one already placed are lifted above it, with the leader line stretched to match.
 */
function PlaceTags()
{
    const Placed = [];
    const Order = Drive.Tags.map((Tag, Index) => ({ Index, At: ScreenOf(Tag.At) })).filter((Each) => Each.At && TagNodes[Each.Index]);
    Order.sort((A, B) => B.At[1] - A.At[1]);                       // lowest first, so lifted labels stack upwards
    for (const { Index, At } of Order)
    {
        const Node = TagNodes[Index], Label = Node.lastChild;
        const Width = Label.offsetWidth || 150, Height = Label.offsetHeight || 40;
        const Left = At[0] > window.innerWidth * 0.5;
        let Rise = 30;
        const Box = () => ({ X: Left ? At[0] - 28 - Width : At[0] + 28, Y: At[1] - Rise - Height, Width, Height });
        for (let Guard = 0; Guard < 8; Guard++)
        {
            const Mine = Box();
            const Hit = Placed.find((Other) => Mine.X < Other.X + Other.Width + 6 && Other.X < Mine.X + Mine.Width + 6 && Mine.Y < Other.Y + Other.Height + 6 && Other.Y < Mine.Y + Mine.Height + 6);
            if (!Hit) break;
            Rise = At[1] - Hit.Y + 6;
        }
        Placed.push(Box());
        Node.classList.toggle('Left', Left);
        Node.style.setProperty('--Rise', `${Rise.toFixed(0)}px`);
        Node.style.transform = `translate(${At[0].toFixed(1)}px, ${At[1].toFixed(1)}px)`;
    }
}

/** Per frame: eases the cutaway, places the hotspot dots and the cutaway tags. */
function PlaceOverlay(Seconds)
{
    if (Cutaway !== CutawayGoal)
    {
        Cutaway = CutawayGoal > Cutaway ? Math.min(1, Cutaway + Seconds * 2.6) : Math.max(0, Cutaway - Seconds * 2.6);
        const Smooth = Cutaway * Cutaway * (3 - 2 * Cutaway);
        Skin?.Set(Smooth);
        Drive.Set(Smooth);
        if (Parts) { Parts.Ground.userData.Fade = 1 - Smooth; Parts.Ghost(Smooth > 0.3); }
        ApplyMirror();
        TagLayer.style.opacity = Math.max(0, Smooth * 1.4 - 0.4);
    }
    const ShowDots = State.View.Spots && Cutaway < 0.02 && !Switching;
    SpotLayer.classList.toggle('Off', !ShowDots);
    if (ShowDots)
    {
        const Side = NearSide();
        Spots.forEach((Spot, Index) =>
        {
            const [X, Y, Z] = Spot.At, [NX, NY, NZ] = Spot.Normal;
            const Mirror = Spot.Mirror ? Side : 1;
            const At = ScreenOf(Facing.set(X, Y, Z * Mirror));
            const Visible = At && Toward.dot(Facing.set(NX, NY, NZ * Mirror).normalize()) > 0.08;
            Dots[Index].classList.toggle('Away', !Visible);
            if (At) Dots[Index].style.transform = `translate(${At[0].toFixed(1)}px, ${At[1].toFixed(1)}px)`;
        });
    }
    if (Cutaway > 0.02) PlaceTags();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LIGHT PLOT
//------------------------------------------------------------------------------------------------------------------------

const Plot = document.getElementById('Plot');
const Pen = Plot.getContext('2d');
const PlotScale = 15;                                               // metres from the centre to the short edge

/** Top view of the car with the set's lights, the key light on its ring, and the camera. */
function DrawPlot()
{
    if (!Built || !Plot.offsetParent) return;
    const Width = Plot.width, Height = Plot.height, Middle = [Width / 2, Height / 2];
    const Unit = (Height / 2 - 16) / PlotScale;
    const At = (X, Z) => [Middle[0] + X * Unit, Middle[1] + Z * Unit];
    Pen.clearRect(0, 0, Width, Height);

    // Grid and ring
    Pen.strokeStyle = 'rgba(255,255,255,0.05)';
    Pen.lineWidth = 1;
    for (let Metre = -30; Metre <= 30; Metre += 5)
    {
        const [GridX] = At(Metre, 0), [, GridY] = At(0, Metre);
        Pen.beginPath(); Pen.moveTo(GridX, 0); Pen.lineTo(GridX, Height); Pen.stroke();
        Pen.beginPath(); Pen.moveTo(0, GridY); Pen.lineTo(Width, GridY); Pen.stroke();
    }
    const Ring = Height / 2 - 16;
    Pen.strokeStyle = 'rgba(255,255,255,0.14)';
    Pen.setLineDash([3, 5]);
    Pen.beginPath(); Pen.arc(Middle[0], Middle[1], Ring, 0, Math.PI * 2); Pen.stroke();
    Pen.setLineDash([]);

    // Set bounds
    const { X, Z } = Built.Bounds;
    const [Left, Top] = At(Math.max(X[0], -40), Math.max(Z[0], -40)), [Right, Bottom] = At(Math.min(X[1], 40), Math.min(Z[1], 40));
    Pen.strokeStyle = 'rgba(255,255,255,0.18)';
    Pen.strokeRect(Left, Top, Right - Left, Bottom - Top);

    // Car
    Pen.fillStyle = 'rgba(255,255,255,0.85)';
    const [CarX, CarZ] = At(-3.11, -1.25);
    Pen.beginPath(); Pen.roundRect(CarX, CarZ, 6.22 * Unit, 2.5 * Unit, 6); Pen.fill();
    Pen.fillStyle = '#0b0c0f';
    const [NoseX, NoseZ] = At(2.4, 0);
    Pen.beginPath(); Pen.moveTo(NoseX + 6, NoseZ); Pen.lineTo(NoseX - 3, NoseZ - 6); Pen.lineTo(NoseX - 3, NoseZ + 6); Pen.fill();

    // Fills and practicals
    for (const Light of Built.Plot)
    {
        const [LightX, LightZ] = At(Light.X, Light.Z);
        const Clamped = [THREE.MathUtils.clamp(LightX, 8, Width - 8), THREE.MathUtils.clamp(LightZ, 8, Height - 8)];
        Pen.fillStyle = Light.Tone;
        Pen.globalAlpha = 0.25 + 0.75 * Math.min(1, State.Rig.Fills);
        Pen.beginPath(); Pen.arc(Clamped[0], Clamped[1], 4.5, 0, Math.PI * 2); Pen.fill();
        Pen.globalAlpha = 1;
    }

    // Camera
    const [CameraX, CameraZ] = At(Camera.position.x, Camera.position.z);
    const Facing = Math.atan2(Orbit.Target.z - Camera.position.z, Orbit.Target.x - Camera.position.x);
    Pen.fillStyle = 'rgba(255,255,255,0.12)';
    Pen.beginPath(); Pen.moveTo(CameraX, CameraZ); Pen.arc(CameraX, CameraZ, 46, Facing - Camera.fov * Radian * 0.75, Facing + Camera.fov * Radian * 0.75); Pen.fill();
    Pen.fillStyle = '#ffffff';
    Pen.beginPath(); Pen.arc(CameraX, CameraZ, 4, 0, Math.PI * 2); Pen.fill();

    // Key light on the ring; its distance from the centre shrinks as it climbs.
    const Azimuth = State.Azimuth * Radian, Lift = Math.cos(State.Rig.Elevation * Radian);
    const KeyX = Middle[0] + Math.cos(Azimuth) * Ring * Math.max(0.12, Lift), KeyZ = Middle[1] + Math.sin(Azimuth) * Ring * Math.max(0.12, Lift);
    const Tone = `#${Built.Key.Light.color.getHexString()}`;
    Pen.strokeStyle = Tone;
    Pen.globalAlpha = 0.5;
    Pen.beginPath(); Pen.moveTo(KeyX, KeyZ); Pen.lineTo(Middle[0], Middle[1]); Pen.stroke();
    Pen.globalAlpha = 1;
    const Glow = Pen.createRadialGradient(KeyX, KeyZ, 0, KeyX, KeyZ, 22);
    Glow.addColorStop(0, Tone); Glow.addColorStop(1, 'rgba(0,0,0,0)');
    Pen.fillStyle = Glow;
    Pen.beginPath(); Pen.arc(KeyX, KeyZ, 22, 0, Math.PI * 2); Pen.fill();
    Pen.fillStyle = '#fff';
    Pen.beginPath(); Pen.arc(KeyX, KeyZ, 7, 0, Math.PI * 2); Pen.fill();
    Pen.font = '500 20px "DM Sans", sans-serif';
    Pen.fillStyle = 'rgba(255,255,255,0.8)';
    Pen.fillText(`Key ${Math.round(((State.Azimuth % 360) + 360) % 360)}° · ${Math.round(State.Rig.Elevation)}° up`, 14, 28);
}

let Dialling = false;
const Dial = (Event) =>
{
    const Box = Plot.getBoundingClientRect();
    const X = (Event.clientX - Box.left) / Box.width * Plot.width - Plot.width / 2, Z = (Event.clientY - Box.top) / Box.height * Plot.height - Plot.height / 2;
    State.Azimuth = Math.atan2(Z, X) / Radian;
    ApplyRig();
};
Plot.addEventListener('pointerdown', (Event) => { Dialling = true; Plot.setPointerCapture(Event.pointerId); Dial(Event); });
Plot.addEventListener('pointermove', (Event) => { if (Dialling) Dial(Event); });
Plot.addEventListener('pointerup', () => { Dialling = false; });

//------------------------------------------------------------------------------------------------------------------------
//                                                    KEYS, SIZE AND LOOP
//------------------------------------------------------------------------------------------------------------------------

window.addEventListener('keydown', (Event) =>
{
    if (Event.target.tagName === 'INPUT') return;
    const Digit = parseInt(Event.key, 10);
    if (Digit >= 1 && Digit <= Sets.length) UseSet(Sets[Digit - 1].Key);
    if (Event.key === 'h' || Event.key === 'H') document.body.classList.toggle('Bare');
    if (Event.key === 'o' || Event.key === 'O') { State.View.Orbit = !State.View.Orbit; Shows.Camera.Orbit.Show(State.View.Orbit); }
    if (Event.key === 'Escape' && Focus) Overview();
});

function Resize()
{
    Renderer.setSize(window.innerWidth, window.innerHeight, false);
    Camera.aspect = window.innerWidth / window.innerHeight;
    Camera.updateProjectionMatrix();
}
window.addEventListener('resize', Resize);

let Last = performance.now(), Ticks = 0;
function Advance(Now)
{
    requestAnimationFrame(Advance);                                 // first, so one bad frame cannot stop the loop
    const Seconds = Math.min(0.1, (Now - Last) / 1000);
    Last = Now;
    PlaceCamera(Seconds);
    Camera.updateMatrixWorld();
    PlaceOverlay(Seconds);
    Parts?.Tick(Now / 1000);
    Twin?.Sync();                                                   // the twin follows part toggles and the cutaway
    Drive.Tick(Seconds);
    if (++Ticks % 4 === 0) DrawPlot();
    Renderer.render(Scene, Camera);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    START
//------------------------------------------------------------------------------------------------------------------------

const Loading = document.getElementById('Loading');
const Ready = (async () =>
{
    try
    {
        Resize();
        ApplyLens();
        await MountCar();
        for (const Key of ['Paint', 'Coat', 'Rims', 'Tyres', 'Calipers', 'Lamps', 'Engine']) await ApplyLook(Key, true);
        BuildSet();
        RefreshPresets();
        requestAnimationFrame(Advance);
        Loading.classList.add('Done');
    }
    catch (Problem)
    {
        Loading.querySelector('span').textContent = `Could not build the lobby: ${Problem.message}`;
        console.error(Problem);
        throw Problem;
    }
})();

// 🧪 Test hooks for the capture scripts.
window.Lobby = {
    Ready, Sets: Sets.map((Set) => Set.Key),
    Use: async (Key, Options = {}, Seed = null) =>
    {
        Object.assign(Memory[Key].Options, Options);
        if (Seed) Memory[Key].Seed = Seed;
        if (Key === State.Key) BuildSet({ KeepView: true }); else { State.Key = Key; BuildSet(); }
    },
    Look: async (Patch) => { for (const [Key, Value] of Object.entries(Patch)) { State.Look[Key] = Value; Shows.Car[Key]?.Show(Value); await ApplyLook(Key, true); } },
    Rig: (Patch) => { if ('Azimuth' in Patch) State.Azimuth = Patch.Azimuth; for (const [Key, Value] of Object.entries(Patch)) if (Key in State.Rig) { State.Rig[Key] = Value; Shows.Light[Key].Show(Value); } ApplyRig(); },
    View: (Id) => { UsePreset(Id); Object.assign(Orbit, { Yaw: Goal.Yaw, Pitch: Goal.Pitch, Distance: Goal.Distance }); Orbit.Target.copy(Goal.Target); },
    Orbit: (View) => { Aim({ ...Orbit, Target: Orbit.Target.toArray(), ...View }, true); },
    Tab: ShowTab,
    Spot: (Id) => { FocusSpot(Id); Object.assign(Orbit, { Yaw: Goal.Yaw, Pitch: Goal.Pitch, Distance: Goal.Distance }); Orbit.Target.copy(Goal.Target); },
    Overview: () => { Overview(); Object.assign(Orbit, { Yaw: Goal.Yaw, Pitch: Goal.Pitch, Distance: Goal.Distance }); Orbit.Target.copy(Goal.Target); },
    Spots: () => Dots.map((Dot) => ({ Id: Dot.dataset.spot, Shown: !Dot.classList.contains('Away') && !SpotLayer.classList.contains('Off'), At: Dot.style.transform })),
    Bare: (On = true) => document.body.classList.toggle('Bare', On),
    State: () => ({ Key: State.Key, ...Memory[State.Key], Look: State.Look, Focus, Cutaway: +Cutaway.toFixed(2), Figures: DriveFigures(DriveSpec()), Rig: { ...State.Rig, Azimuth: State.Azimuth }, Camera: Camera.position.toArray().map((Value) => +Value.toFixed(2)) }),
    Renderer,
};

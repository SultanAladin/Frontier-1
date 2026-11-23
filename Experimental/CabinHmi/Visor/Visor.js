// VISOR — HEAD-UP DISPLAY DRIVING DEMO
// 🧩 First-person drive in the ControlVehicle down an endless night pass. Everything the driver needs is light on the
//    windscreen: lane guides that follow the lane you are really in, boxes on traffic, a porcupine hazard tag, a
//    traffic-signal countdown that turns green → amber → red, squad pings (ping wheel on the middle mouse button),
//    fuel / coolant / cabin-oxygen warnings that climb with the altitude, overtake advice, and a fuel-station suggestion
//    that paints a route into the bay. Real steering (bicycle model), oncoming and same-way traffic, synthesised sound.
// 📝 Game prototype: the course loops every LapLength metres and re-arms every event; numbers are display-scale.

import { THREE, AssembleVehicle, DefaultFinish, Family } from '../Shared/VehicleAssembly.js';
import { BuildCourse, MakePorcupine, MakeSignal, MakeStation, MakeBeacon, LaneOffset, LaneWidth, RoadHalf } from './VisorCourse.js';
import { GlassPainter, GlassPixels, Tones } from './VisorGlass.js';
import { MakeTraffic, CarLength, CarWidth } from './VisorTraffic.js';
import { PingWheel, PingByKey } from './VisorPing.js';
import { SoundDesk } from './VisorAudio.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const LapLength = 3600;                                            // [m]
const Course = {                                                   // [m] - from the lap start
    PingAt:      380,  BeaconAt:  900,  BeaconZ: -38,
    WarnAt:      860,                                              // teammate warns about the wildlife
    PorcupineAt: 1080, CrossFrom: 135,
    SignalAt:    1800,
    GradeFrom:   2050, GradeTo:   2700,
    StationAt:   3160,
};
const Eye        = new THREE.Vector3(-0.12, 1.5, -0.5);            // driver eye, car local
const ScreenSill = new THREE.Vector3(1.30, 1.16, 0);               // windscreen bottom edge (centre), car local
const ScreenTop  = new THREE.Vector3(0.63, 1.72, 0);               // windscreen top edge (centre), car local
const ScreenWidth = 2.0;                                           // [m]
// 📝 The HUD image sits on an upright plane just inside the windscreen, square to the driver, like the virtual image of a
//    real combiner. Painting straight on the sloped glass made every label look sheared from the off-centre seat.
const GlassWidth  = 1.25;                                          // [m]
const GlassHeight = GlassWidth * GlassPixels[1] / GlassPixels[0]; // [m] - keeps canvas pixels square
const FrontReach = 3.1;                                            // [m] - car root to front bumper
const Kmh = 3.6;
const Grip       = 6;                                              // [m/s²] - most cornering the steering will ask for
const Wheelbase  = 3.4;                                            // [m] - ControlVehicle axle spacing
const Teammates  = ['Kestrel', 'Orca'];

//------------------------------------------------------------------------------------------------------------------------
//                                                    RENDERER AND SCENE
//------------------------------------------------------------------------------------------------------------------------

const Canvas   = document.getElementById('Scene');
const Renderer = new THREE.WebGLRenderer({ canvas: Canvas, antialias: true });
Renderer.setPixelRatio(Math.min(2, window.devicePixelRatio));
Renderer.toneMapping = THREE.ACESFilmicToneMapping;
Renderer.toneMappingExposure = 1.0;
Renderer.outputColorSpace = THREE.SRGBColorSpace;
const Scene  = new THREE.Scene();
Scene.fog    = new THREE.FogExp2('#020407', 0.0105);
const Camera = new THREE.PerspectiveCamera(64, 16 / 9, 0.02, 1500);
Camera.rotation.order = 'YXZ';
Scene.add(new THREE.HemisphereLight('#3a4a66', '#0a0806', 0.5));
const Moon = new THREE.DirectionalLight('#8fa4d6', 0.45);           // faint moonlight so the snow reads off the road
Moon.position.set(-200, 260, -320);
Scene.add(Moon);
function Resize()
{
    Renderer.setSize(window.innerWidth, window.innerHeight, false);
    Camera.aspect = window.innerWidth / window.innerHeight;
    Camera.updateProjectionMatrix();
}
window.addEventListener('resize', Resize);
Resize();

const Scenery = BuildCourse(Scene);

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONDITION
//------------------------------------------------------------------------------------------------------------------------

const Condition = {
    X:          0,                    // [m] - car root along the road
    Z:          LaneOffset,           // [m] - car root across the road (+Z is to the right)
    Heading:    0,                    // [rad] - 0 faces +X, positive turns left
    Steer:      0,                    // [rad] - front wheel angle, positive left
    YawRate:    0,                    // [rad/s]
    LaneGoal:   LaneOffset,           // lane centre the lane keeper aims for
    LaneHold:   true,
    Speed:      0,                    // [m/s] - negative when reversing
    Cruise:     80,                   // [km/h] - autopilot set speed
    Auto:       true,
    Lap:        0,
    Fuel:       21,                   // [%]
    Coolant:    88,                   // [°C]
    Oxygen:     99,                   // [%] - cabin saturation
    Boost:      false,                // O₂ boost on
    Altitude:   2400,                 // [m]
    Outside:    6,                    // [°C]
    Brake:      false,                // BRAKE on the glass
    Routed:     false,                // station route accepted
    Fuelling:   0,                    // 0 idle, 1 filling, 2 rejoining
    Overtake:   null,                 // the car being overtaken by the autopilot
    Lead:       null,                 // nearest car in the path: { Car, Gap, Closing, Ttc, HeadOn, Along }
    Crash:      0,                    // [s] - time left on the crash banner
    CrashText:  '',
    Rumble:     0,
    Rpm:        850,
    Load:       0,
    Throttle:   0,
    Yaw: 0, Pitch: -0.06, YawGoal: 0, PitchGoal: -0.06, Fov: 64, Roll: 0,
};
const Keys = new Set();
const Sound = SoundDesk();

//------------------------------------------------------------------------------------------------------------------------
//                                                    VEHICLE AND COCKPIT
//------------------------------------------------------------------------------------------------------------------------

const Finish = DefaultFinish({
    [Family.BodyCoat]:  { color: '#2a2f38', roughness: 0.32, metalness: 0.5, clearcoat: 1, clearcoatRoughness: 0.08 },
    [Family.Trim]:      { color: '#1a1d22', roughness: 0.4, metalness: 0.5 },
    [Family.Headlight]: { color: '#dfe8ff', emissive: '#e8f0ff', emissiveIntensity: 1.6 },
    [Family.Glass]:     { visible: false },                        // clear windows: the HUD is the only thing on the glass
});
const CarRoot = new THREE.Group();
Scene.add(CarRoot);
let Vehicle = null;

// Windscreen basis in car space: Right runs left → right, Up climbs the slope, Normal faces the driver.
const ScreenUp     = ScreenTop.clone().sub(ScreenSill).normalize();
const ScreenRight  = new THREE.Vector3(0, 0, 1);
const ScreenNormal = new THREE.Vector3().crossVectors(ScreenRight, ScreenUp).normalize();
const ScreenHeight = ScreenSill.distanceTo(ScreenTop);
const ScreenBasis  = new THREE.Matrix4().makeBasis(ScreenRight, ScreenUp, ScreenNormal).setPosition(ScreenSill.clone().add(ScreenTop).multiplyScalar(0.5).addScaledVector(ScreenNormal, 0.01));
// HUD image plane: upright, centred on the driver's line of sight.
const GlassRight  = new THREE.Vector3(0, 0, 1);
const GlassUp     = new THREE.Vector3(0, 1, 0);
const GlassNormal = new THREE.Vector3().crossVectors(GlassRight, GlassUp);
const GlassCentre = new THREE.Vector3(0.64, 1.445, Eye.z);
const GlassBasis  = new THREE.Matrix4().makeBasis(GlassRight, GlassUp, GlassNormal).setPosition(GlassCentre);

const GlassCanvas = document.createElement('canvas');
const Painter     = GlassPainter(GlassCanvas);
const GlassMap    = new THREE.CanvasTexture(GlassCanvas);
GlassMap.colorSpace = THREE.SRGBColorSpace;
GlassMap.anisotropy = 8;
GlassMap.generateMipmaps = false;
GlassMap.minFilter = THREE.LinearFilter;

function BuildCockpit()
{
    const Cockpit = new THREE.Group();
    const Soft   = new THREE.MeshStandardMaterial({ color: '#0d0e11', roughness: 0.82, metalness: 0.05 });
    const Fabric = new THREE.MeshStandardMaterial({ color: '#17181c', roughness: 0.95 });
    const Satin  = new THREE.MeshStandardMaterial({ color: '#30343b', roughness: 0.35, metalness: 0.8 });

    // The HUD light itself, then a faint tint so the glass reads as glass.
    const Glass = new THREE.Mesh(new THREE.PlaneGeometry(GlassWidth, GlassHeight), new THREE.MeshBasicMaterial({ map: GlassMap, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false, fog: false }));
    Glass.applyMatrix4(GlassBasis);
    Glass.renderOrder = 10;
    const TintMap = (() =>
    {
        const Paint = document.createElement('canvas');
        Paint.width = 256; Paint.height = 128;
        const Context = Paint.getContext('2d');
        const Fill = Context.createLinearGradient(0, 0, 0, 128);
        Fill.addColorStop(0, 'rgba(40,60,90,0.32)'); Fill.addColorStop(0.12, 'rgba(10,16,26,0.04)'); Fill.addColorStop(1, 'rgba(10,16,26,0)');
        Context.fillStyle = Fill; Context.fillRect(0, 0, 256, 128);
        return new THREE.CanvasTexture(Paint);
    })();
    const Tint = new THREE.Mesh(new THREE.PlaneGeometry(ScreenWidth, ScreenHeight), new THREE.MeshBasicMaterial({ map: TintMap, transparent: true, depthWrite: false, fog: false }));
    Tint.applyMatrix4(ScreenBasis);
    Tint.renderOrder = 9;

    // Dash top below the glass, with the HUD projector window in front of the driver.
    const Dash = new THREE.Mesh(new THREE.BoxGeometry(1.2, 0.5, 2.3), Soft);
    Dash.position.set(0.78, 0.905, 0);
    const Cowl = new THREE.Mesh(new THREE.CylinderGeometry(0.09, 0.09, 2.3, 16, 1, false, 0, Math.PI).rotateX(Math.PI / 2).rotateZ(Math.PI / 2), Soft);
    Cowl.position.set(0.2, 1.155, 0);
    const Projector = new THREE.Mesh(new THREE.PlaneGeometry(0.34, 0.16).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ color: '#030405', roughness: 0.08, metalness: 0.9 }));
    Projector.position.set(0.95, 1.157, Eye.z);
    const ProjectorGlow = new THREE.Mesh(new THREE.PlaneGeometry(0.36, 0.18).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: Tones.Notice, transparent: true, opacity: 0.12, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
    ProjectorGlow.position.set(0.95, 1.1565, Eye.z);

    // A-pillars along the glass sides, header and roof liner, B-pillars, door tops and door trim.
    const PillarLength = ScreenHeight + 0.16;
    const Slope = Math.atan2(ScreenUp.y, ScreenUp.x);
    for (const Side of [-1, 1])
    {
        const Pillar = new THREE.Mesh(new THREE.BoxGeometry(PillarLength, 0.13, 0.16), Fabric);
        Pillar.position.copy(ScreenSill.clone().add(ScreenTop).multiplyScalar(0.5)).add(new THREE.Vector3(0, 0, Side * (ScreenWidth / 2 + 0.05)));
        Pillar.position.addScaledVector(ScreenNormal, 0.03);
        Pillar.rotation.z = Slope;
        const BPillar = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.66, 0.1), Fabric);
        BPillar.position.set(-0.98, 1.46, Side * 1.13);
        const DoorTop = new THREE.Mesh(new THREE.BoxGeometry(2.4, 0.07, 0.16), Soft);
        DoorTop.position.set(0.15, 1.13, Side * 1.13);
        const DoorTrim = new THREE.Mesh(new THREE.BoxGeometry(2.4, 0.62, 0.05), Fabric);
        DoorTrim.position.set(0.15, 0.8, Side * 1.19);
        const Handle = new THREE.Mesh(new THREE.BoxGeometry(0.22, 0.03, 0.03), Satin);
        Handle.position.set(0.25, 1.0, Side * 1.155);
        Cockpit.add(Pillar, BPillar, DoorTop, DoorTrim, Handle);
    }
    const Header = new THREE.Mesh(new THREE.BoxGeometry(0.26, 0.1, ScreenWidth + 0.3), Fabric);
    Header.position.copy(ScreenTop).add(new THREE.Vector3(-0.06, 0.07, 0));
    const Liner = new THREE.Mesh(new THREE.PlaneGeometry(1.9, 2.36).rotateX(Math.PI / 2), Fabric);
    Liner.position.set(-0.3, 1.79, 0);
    const MirrorStem = new THREE.Mesh(new THREE.CylinderGeometry(0.012, 0.012, 0.08, 8), Satin);
    MirrorStem.position.set(0.6, 1.74, 0);
    const Mirror = new THREE.Mesh(new THREE.BoxGeometry(0.04, 0.075, 0.27), Soft);
    Mirror.position.set(0.58, 1.67, 0);
    const MirrorFace = new THREE.Mesh(new THREE.PlaneGeometry(0.25, 0.058).rotateY(-Math.PI / 2), new THREE.MeshStandardMaterial({ color: '#0a0d12', roughness: 0.05, metalness: 1 }));
    MirrorFace.position.set(0.559, 1.67, 0);

    // Steering wheel facing the driver; Rim turns with the steering.
    const Wheel = new THREE.Group();
    Wheel.position.set(0.36, 1.06, Eye.z);
    Wheel.lookAt(new THREE.Vector3(-0.6, 1.62, Eye.z));
    const Rim = new THREE.Group();
    const Leather = new THREE.MeshStandardMaterial({ color: '#121316', roughness: 0.7 });
    Rim.add(new THREE.Mesh(new THREE.TorusGeometry(0.19, 0.022, 12, 48), Leather));
    for (const Angle of [Math.PI / 2 + 0.1, -Math.PI / 2 - 0.1, -Math.PI / 2 + Math.PI])
    {
        const Spoke = new THREE.Mesh(new THREE.BoxGeometry(0.17, 0.028, 0.02).translate(0.085, 0, 0), Satin);
        Spoke.rotation.z = Angle + Math.PI / 2;
        Rim.add(Spoke);
    }
    const Boss = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.065, 0.04, 20).rotateX(Math.PI / 2), Leather);
    const Badge = new THREE.Mesh(new THREE.CircleGeometry(0.016, 16), new THREE.MeshBasicMaterial({ color: Tones.Notice, toneMapped: false }));
    Badge.position.z = 0.021;
    Rim.add(Boss, Badge);
    Wheel.add(Rim);

    // HUD spill: the display's light falls on the dash and the pillars in its current tone.
    const Spill = new THREE.PointLight(Tones.Notice, 0.5, 1.6, 1.5);
    Spill.position.set(1.0, 1.3, Eye.z + 0.2);
    Cockpit.add(Glass, Tint, Dash, Cowl, Projector, ProjectorGlow, Header, Liner, MirrorStem, Mirror, MirrorFace, Wheel, Spill);
    return { Cockpit, Rim, Spill, ProjectorGlow };
}
const Cabin = BuildCockpit();
CarRoot.add(Cabin.Cockpit);

// Headlights: two spots ahead of the bumper, aimed down the road.
const Beams = [-0.85, 0.85].map((Side) =>
{
    const Beam = new THREE.SpotLight('#f1f5ff', 800, 240, 0.4, 0.6, 1.4);
    Beam.position.set(3.0, 0.9, Side);
    Beam.target.position.set(60, -0.2, Side * 0.5);
    CarRoot.add(Beam, Beam.target);
    return Beam;
});

//------------------------------------------------------------------------------------------------------------------------
//                                                    EVENT PROPS
//------------------------------------------------------------------------------------------------------------------------

const Porcupine = MakePorcupine();
const Signal    = MakeSignal();
const Station   = MakeStation();
const Beacon    = MakeBeacon(Tones.Team);
Scene.add(Porcupine.Group, Signal.Group, Station.Group, Beacon.Group);
const Events = {};

function ArmLap()
{
    const Start = Condition.Lap * LapLength;
    Object.assign(Events, {
        Start,
        Ping:      { Sent: false },
        Warn:      { Sent: false },
        Critter:   { X: Start + Course.PorcupineAt, Z: -RoadHalf - 1.2, Crossing: false, Clear: false, Step: 0 },
        Light:     { X: Start + Course.SignalAt, Phase: 'Idle', Left: 0 },      // Idle → Green → Amber → Red → Go
        StationX:  Start + Course.StationAt,
        BeaconX:   Start + Course.BeaconAt,
        Iced:      false,
    });
    Porcupine.Group.position.set(Events.Critter.X, 0, Events.Critter.Z);
    Signal.Group.position.x  = Events.Light.X;
    Station.Group.position.x = Events.StationX;
    Beacon.Group.position.set(Events.BeaconX, 0, Course.BeaconZ);
    Condition.Routed = false;
    Condition.Fuelling = 0;
}
ArmLap();
const SignalTimes = { Green: 5.5, Amber: 3.0, Red: 8.0 };

//------------------------------------------------------------------------------------------------------------------------
//                                                    NOTICES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Cards on the glass, ordered by urgency. Show() updates a live card in place; E acts on the top card that has an
//    action, Q dismisses the top card. Life (s) makes a card leave on its own. A new card plays its level's cue.
const Rank = { Danger: 0, Warn: 1, Team: 2, Notice: 3 };
const Notices = new Map();
function Show(Id, Spec)
{
    const Live = Notices.get(Id);
    if (Live) { Object.assign(Live, Spec); Live.Leaving = Live.Leaving && Spec.Revive ? 0 : Live.Leaving; return; }
    Notices.set(Id, { Id, Age: 0, Leaving: 0, Level: 'Notice', ...Spec });
    if (!Spec.Quiet) Sound.Play(Spec.Level ?? 'Notice');
}
function Drop(Id) { const Live = Notices.get(Id); if (Live && !Live.Leaving) Live.Leaving = 0.0001; }
function Ordered() { return [...Notices.values()].filter((Each) => !Each.Leaving).sort((A, B) => Rank[A.Level] - Rank[B.Level] || A.Born - B.Born); }
let NoticeClock = 0;

// E acts on the most urgent card that offers an action (or the card named Id, for tests).
function Act(Id)
{
    const Top = Id ? Ordered().find((Each) => Each.Id === Id && Each.Action) : Ordered().find((Each) => Each.Action);
    if (!Top) return;
    Sound.Play('Accept');
    Top.Action();
    Drop(Top.Id);
}
function Dismiss()
{
    const Top = Ordered().find((Each) => Each.Dismiss !== false);
    if (!Top) return;
    Sound.Play('Dismiss');
    Drop(Top.Id);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PINGS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A ping is a kind, an owner and either a fixed spot (with a short light pillar) or something it follows (a car, the
//    porcupine). Teammates acknowledge your pings a moment later; they also ping things themselves now and then.
const Pings = [];
const Replies = [];
const Pillars = Array.from({ length: 5 }, () =>
{
    const Pillar = MakeBeacon('#ffffff');
    Pillar.Group.children[2].visible = false;                       // no crate, just the light
    Pillar.Group.scale.set(0.6, 0.22, 0.6);
    Pillar.Group.visible = false;
    Pillar.Busy = false;
    Scene.add(Pillar.Group);
    return Pillar;
});
const Wheel = PingWheel();
const Pointer = { X: window.innerWidth / 2, Y: window.innerHeight / 2 };
const Caster = new THREE.Raycaster();
let Traffic = null;

const ReplyLines = {
    Going: 'On my way', Enemy: 'Eyes on that car', Danger: 'Copy, careful there', Supply: 'Grabbing it',
    Fuel: 'Fuel cans in my trunk', Watch: 'Watching it too', Hold: 'Holding', Regroup: 'Regrouping on you',
};

// What sits under the cursor: a car, the porcupine, the station, the signal, the cache, or a spot on the ground.
function Aim(ClientX, ClientY)
{
    Caster.setFromCamera(new THREE.Vector2((ClientX / window.innerWidth) * 2 - 1, -(ClientY / window.innerHeight) * 2 + 1), Camera);
    const Targets = [
        ...(Traffic ? Traffic.Live.map((Car) => ({ Body: Car.Group, Label: 'CAR', Suggest: 'Enemy', Follow: { Car, Lift: 2.3 } })) : []),
        { Body: Porcupine.Group, Label: 'PORCUPINE', Suggest: 'Danger', Follow: { Body: Porcupine.Group, Lift: 0.7 } },
        { Body: Station.Group, Label: 'STATION', Suggest: 'Fuel' },
        { Body: Signal.Group, Label: 'SIGNAL', Suggest: 'Watch' },
        { Body: Beacon.Group, Label: 'CACHE', Suggest: 'Supply' },
    ];
    let Best = null;
    for (const Target of Targets)
    {
        if (!Target.Body.visible) continue;
        const Hit = Caster.intersectObject(Target.Body, true).find((Each) => Each.object.isMesh && !Each.object.userData.Unpickable);
        if (Hit && (!Best || Hit.distance < Best.Hit.distance)) Best = { ...Target, Hit };
    }
    let Point, Label, Suggest = 'Going', Follow = null;
    if (Best) { Point = Best.Hit.point.clone(); Label = Best.Label; Suggest = Best.Suggest; Follow = Best.Follow ?? null; }
    else
    {
        const { origin: Origin, direction: Direction } = Caster.ray;
        const Reach = Direction.y < -1e-4 ? -Origin.y / Direction.y : Infinity;
        if (Reach < 1500) Point = Origin.clone().addScaledVector(Direction, Reach);
        else Point = Origin.clone().add(new THREE.Vector3(Direction.x, 0, Direction.z).normalize().multiplyScalar(700)).setY(0);
        Label = Reach >= 1500 ? 'HORIZON' : Math.abs(Point.z) < RoadHalf ? 'ROAD' : 'SNOW';
    }
    return { Point, Label, Suggest, Follow, Distance: Math.round(Point.distanceTo(Camera.position)) };
}
function PingSpot(Ping)
{
    if (Ping.Follow?.Car) return Probe.set(Ping.Follow.Car.X, Ping.Follow.Lift, Ping.Follow.Car.Z);
    if (Ping.Follow?.Body) return Probe.copy(Ping.Follow.Body.position).setY(Ping.Follow.Lift);
    return Probe.copy(Ping.Point).setY(Ping.Point.y + 0.15);
}
function RemovePing(Ping)
{
    const Index = Pings.indexOf(Ping);
    if (Index >= 0) Pings.splice(Index, 1);
    if (Ping.Pillar) { Ping.Pillar.Busy = false; Ping.Pillar.Group.visible = false; }
}
function PlacePing(Call, Spot, Owner = 'You')
{
    const Mine = Pings.filter((Each) => Each.Owner === 'You');
    if (Owner === 'You' && Mine.length >= 3) RemovePing(Mine[0]);
    const Ping = { Call, Owner, Point: Spot.Point.clone(), Follow: Spot.Follow, Born: Clock, Life: Spot.Follow?.Car ? 16 : 30, Ack: null, Pillar: null };
    if (!Spot.Follow)
    {
        Ping.Pillar = Pillars.find((Each) => !Each.Busy) ?? null;
        if (Ping.Pillar)
        {
            Ping.Pillar.Busy = true;
            Ping.Pillar.Group.visible = true;
            Ping.Pillar.Group.position.copy(Spot.Point).setY(0);
            Ping.Pillar.Pillar.material.uniforms.Tone.value.set(Call.Tone);
            Ping.Pillar.Ring.material.color.set(Call.Tone);
        }
    }
    Pings.push(Ping);
    Sound.Play('Ping', Call.Key);
    if (Owner === 'You') Replies.push({ Due: Clock + 0.9 + Math.random() * 0.9, Ping, Mate: Teammates[Pings.length % 2] });
    else Show(`Mate${Clock}`, { Level: 'Team', Glyph: Call.Glyph, Title: `${Owner.toUpperCase()} · ${Call.Label}`, Sub: Spot.Follow?.Car ? 'Marked a car on the road' : `Marked ${Spot.Label.toLowerCase()} · ${Spot.Distance} m`, Life: 5, Quiet: true });
    return Ping;
}
// A quick click on one of your own pings cancels it (pinging the same spot again), like the squad games.
function PingNear(ClientX, ClientY)
{
    for (const Ping of Pings)
    {
        if (Ping.Owner !== 'You') continue;
        const Screen = PingSpot(Ping).clone().project(Camera);
        if (Screen.z > 1) continue;
        const X = (Screen.x + 1) / 2 * window.innerWidth, Y = (1 - Screen.y) / 2 * window.innerHeight;
        if (Math.hypot(X - ClientX, Y - ClientY) < 44) return Ping;
    }
    return null;
}
function OpenWheel(X = Pointer.X, Y = Pointer.Y)
{
    if (Wheel.Open) return;
    Wheel.Begin(X, Y, Aim(X, Y));
    Sound.Play('Open');
}
function CloseWheel()
{
    if (!Wheel.Open) return;
    const Result = Wheel.End();
    if (Result.Quick)
    {
        const Near = PingNear(Pointer.X, Pointer.Y);
        if (Near) { RemovePing(Near); Sound.Play('Dismiss'); return; }
        PlacePing(PingByKey[Result.Aim.Suggest], Result.Aim);
    }
    else if (Result.Call) PlacePing(Result.Call, Result.Aim);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    INPUT
//------------------------------------------------------------------------------------------------------------------------

const Wake = () => { Sound.Wake(); document.body.classList.add('Heard'); };
window.addEventListener('pointermove', (Move) =>
{
    Pointer.X = Move.clientX;
    Pointer.Y = Move.clientY;
    if (Wheel.Open) { if (Wheel.Move(Move.clientX, Move.clientY)) Sound.Play('Hover'); return; }
    const Across = (Move.clientX / window.innerWidth) * 2 - 1, Down = (Move.clientY / window.innerHeight) * 2 - 1;
    Condition.YawGoal   = -Across * 1.2;
    Condition.PitchGoal = -Down * 0.45 - 0.06;
});
window.addEventListener('pointerdown', (Press) =>
{
    Wake();
    if (Press.button === 1) { Press.preventDefault(); OpenWheel(Press.clientX, Press.clientY); }
});
window.addEventListener('pointerup', (Release) => { if (Release.button === 1) CloseWheel(); });
window.addEventListener('auxclick', (Click) => Click.preventDefault());
window.addEventListener('mousedown', (Press) => { if (Press.button === 1) Press.preventDefault(); });
window.addEventListener('wheel', (Turn) => { Condition.Fov = THREE.MathUtils.clamp(Condition.Fov + Turn.deltaY * 0.02, 42, 78); }, { passive: true });
window.addEventListener('keydown', (Press) =>
{
    Wake();
    const Key = Press.key.toLowerCase();
    if ([' ', 'arrowup', 'arrowdown', 'arrowleft', 'arrowright'].includes(Key)) Press.preventDefault();
    if (Keys.has(Key)) return;
    Keys.add(Key);
    if (Key === 'c') { Condition.Auto = !Condition.Auto; Show('Mode', { Level: 'Notice', Glyph: 'Ping', Title: Condition.Auto ? 'AUTOPILOT ON' : 'MANUAL', Sub: Condition.Auto ? `Holding ${Math.round(Condition.Cruise)} km/h and the lane · W / S set speed` : 'W throttle · S brake / reverse · emergency braking stays on', Life: 3.5, Revive: true }); }
    if (Key === 'e') Act(null);
    if (Key === 'q') Dismiss();
    if (Key === 'f') OpenWheel();
    if (Key === 'h') document.body.classList.toggle('Bare');
    if (Key === 'm') { const Muted = Sound.ToggleMute(); Show('Sound', { Level: 'Notice', Glyph: 'Ping', Title: Muted ? 'SOUND OFF' : 'SOUND ON', Sub: 'M toggles sound', Life: 2, Revive: true, Quiet: true }); }
});
window.addEventListener('keyup', (Release) =>
{
    const Key = Release.key.toLowerCase();
    Keys.delete(Key);
    if (Key === 'f') CloseWheel();
});
window.addEventListener('blur', () => Keys.clear());

//------------------------------------------------------------------------------------------------------------------------
//                                                    DRIVING
//------------------------------------------------------------------------------------------------------------------------

// The fastest speed that still stops Gap metres ahead at a comfortable Rate.
const StopSpeed = (Gap, Rate) => Math.sqrt(2 * Rate * Math.max(0, Gap));
const Held = (...Names) => Names.some((Name) => Keys.has(Name));

// Nearest car in the player's path, in the player's own frame (forward along the heading, right across it).
function FindLead()
{
    if (!Traffic) return null;
    const Cos = Math.cos(Condition.Heading), Sin = Math.sin(Condition.Heading);
    let Lead = null;
    for (const Car of Traffic.Live)
    {
        const DX = Car.X - Condition.X, DZ = Car.Z - Condition.Z;
        const Forward = DX * Cos - DZ * Sin, Across = DX * Sin + DZ * Cos;
        if (Forward < 0 || Math.abs(Across) > 2.5) continue;
        const Gap = Forward - CarLength;
        if (Lead && Gap >= Lead.Gap) continue;
        const Along = Car.Speed * Car.Lane * Cos;                          // the car's speed along our heading
        const Closing = Condition.Speed - Along;
        Lead = { Car, Gap, Along, Closing, Ttc: Closing > 0.3 ? Math.max(0, Gap) / Closing : Infinity, HeadOn: Car.Lane * Cos < 0 };
    }
    return Lead;
}
// Oncoming time window: seconds until the nearest oncoming car ahead meets us (Infinity when none within 900 m).
function OncomingWindow()
{
    let Window = Infinity;
    for (const Car of Traffic?.Live ?? [])
    {
        if (Car.Lane > 0 || Car.X < Condition.X - 4) continue;
        const Gap = Car.X - Condition.X - CarLength;
        if (Gap > 900) continue;
        Window = Math.min(Window, Math.max(0, Gap) / Math.max(4, Condition.Speed + Car.Speed));
    }
    return Window;
}

function Crash(Text, Car = null)
{
    if (Condition.Crash > 0) return;
    Condition.Crash = 2.8;
    Condition.CrashText = Text;
    // Knock the car back a little so it is free of whatever it hit.
    const Back = Math.sign(Condition.Speed || 1);
    Condition.X -= Math.cos(Condition.Heading) * Back * 0.9;
    Condition.Z += Math.sin(Condition.Heading) * Back * 0.9;
    Condition.Speed = -Back * 1.2;
    if (Car) { Car.Speed *= 0.3; Car.Want = Math.max(Car.Speed, 9); }                // the other car stays on the road, shaken and slow
    Condition.Overtake = null;
    Sound.Play('Crash');
    Flash.classList.remove('Hit');
    void Flash.offsetWidth;
    Flash.classList.add('Hit');
}

function Drive(Step)
{
    const Cos = Math.cos(Condition.Heading);
    const Front = Condition.X + FrontReach * Cos;
    const Critter = Events.Critter, Light = Events.Light;
    let Goal = Condition.Cruise / Kmh, Hard = false;

    // Porcupine: in the path until it has walked past the far edge of the car.
    const CritterGap = Critter.X - Front;
    const InPath = Critter.Crossing && !Critter.Clear && CritterGap > -2 && Cos > 0;
    if (InPath) Goal = Math.min(Goal, StopSpeed(CritterGap - 11, 4.2));
    // Signal: stop at the line when the car cannot clear it before red (only in our lane, facing it).
    const StopGap = Light.X + Signal.StopOffset - Front;
    const ForSignal = Light.Must && StopGap > -1 && Cos > 0.7 && Condition.Z > -1;
    if (ForSignal) Goal = Math.min(Goal, StopSpeed(StopGap - 1.5, 3.4));
    // Station: glide into the bay and stop by the pump; rejoin after filling.
    const PumpGap = Events.StationX + Station.PumpX + 1 - Condition.X;
    if (Condition.Routed && Condition.Fuelling < 2) Goal = Math.min(Goal, PumpGap > 120 ? Goal : Math.max(StopSpeed(PumpGap, 2.4), PumpGap > 2 ? 2.5 : 0));
    if (Condition.Fuelling === 1) Goal = 0;
    // Overheat: hold 40 km/h until the coolant is back under 110 °C.
    if (Condition.Hot) Goal = Math.min(Goal, 40 / Kmh);
    // Traffic: keep 1.6 s plus 8 m behind the car ahead (adaptive cruise); a head-on car means stop.
    const Lead = Condition.Lead = FindLead();
    if (Lead && Lead.Gap < 180) Goal = Math.min(Goal, Math.max(0, Lead.Along + (Lead.Gap - 8 - Math.max(0, Condition.Speed) * 1.6) * 0.4));
    // Overtaking: a little extra pace while in the far lane.
    if (Condition.Overtake && Condition.Z < LaneOffset - 0.5) Goal = Math.max(Goal, Math.min((Condition.Cruise + 15) / Kmh, Goal + 15 / Kmh));

    let Rate;
    if (Condition.Crash > 0) Rate = -6 * Math.sign(Condition.Speed);
    else if (Condition.Auto && !Keys.has(' '))
    {
        if (Held('w', 'arrowup'))   Condition.Cruise = Math.min(130, Condition.Cruise + Step * 14);
        if (Held('s', 'arrowdown')) Condition.Cruise = Math.max(20, Condition.Cruise - Step * 14);
        const Error = Goal - Condition.Speed;
        Rate = Error > 0 ? Math.min(2.6, Error * 0.9) : Math.max(-7.5, Error * 1.6);
    }
    else
    {
        const Pedal = Held('w', 'arrowup') ? (Condition.Speed < -0.2 ? 7 : 3.2) : 0;
        let Brake = 0;
        if (Keys.has(' ')) Brake = 8;
        else if (Held('s', 'arrowdown')) Brake = Condition.Speed > 0.3 ? 6 : 0;
        const Reverse = Held('s', 'arrowdown') && Condition.Speed <= 0.3 && !Keys.has(' ') ? -2.4 : 0;
        Rate = Pedal + Reverse - Brake * Math.sign(Condition.Speed || 1) - 0.5 * Math.sign(Condition.Speed);
    }
    // Emergency braking (both modes): porcupine, red light, or a car about to be hit.
    if (Condition.Speed > 0.5 && ((InPath && CritterGap / Condition.Speed < 1.9) || (ForSignal && StopGap > 0 && StopGap / Condition.Speed < 1.4) || (Lead && Lead.Ttc < 1.3 && Lead.Closing > 1.5)))
    {
        Rate = Math.min(Rate, -8.5);
        Hard = true;
    }
    // Off the road: snow drag.
    const OffRoad = Math.abs(Condition.Z) > RoadHalf + 0.3 && !(Condition.Routed && Math.abs(Condition.Z - Station.BayZ) < 7);
    if (OffRoad) Rate -= 1.6 * Math.sign(Condition.Speed);
    const Before = Condition.Speed;
    Condition.Speed += Rate * Step;
    if (Before !== 0 && Math.sign(Condition.Speed) !== Math.sign(Before) && !(Rate * Before > 0) && !Held('s', 'arrowdown', 'w', 'arrowup')) Condition.Speed = 0;
    if (Condition.Auto && Condition.Speed < 0 && Condition.Crash <= 0) Condition.Speed = 0;
    Condition.Speed = THREE.MathUtils.clamp(Condition.Speed, -6, 60);
    Condition.Throttle = Math.max(0, Rate) / 2.6;
    Condition.Brake = Hard || (InPath && CritterGap < 75 && Condition.Speed > 0.8) || (Lead && Lead.Ttc < 2.2 && Lead.Closing > 2);
    Condition.OffRoad = OffRoad;

    // Steering: A / D turn the front wheels (less lock at speed). Hands off with the autopilot on, the lane keeper
    // steers toward the centre of the lane the car is in (or the lane / bay the autopilot chose).
    const SteerKey = (Held('a', 'arrowleft') ? 1 : 0) - (Held('d', 'arrowright') ? 1 : 0);
    // Lock shrinks with speed so the tyres never ask for more than ~6 m/s² of cornering (full lock only when slow).
    const Lock = Math.min(0.62, Math.atan(Grip * Wheelbase / Math.max(1, Condition.Speed * Condition.Speed)));
    const Aligned = Cos > 0.82;
    let SteerGoal = 0;
    if (SteerKey)
    {
        SteerGoal = SteerKey * Lock;
        Condition.LaneHold = false;
        if (Condition.Overtake) { Condition.Overtake = null; Drop('Overtake'); }
    }
    else
    {
        if (!Condition.LaneHold)
        {
            if (Aligned && Math.abs(Condition.Z) < RoadHalf) Condition.LaneGoal = Condition.Z < 0 ? -LaneOffset : LaneOffset;
            Condition.LaneHold = true;
        }
        const Keeps = Condition.Auto && Aligned && Condition.Crash <= 0 && (Math.abs(Condition.Z) < RoadHalf + 0.5 || Condition.Routed);
        // In the oncoming lane without an overtake: the assist steers home as soon as a car comes the other way.
        if (Keeps && Condition.LaneGoal < 0 && !Condition.Overtake && Lead?.HeadOn && Lead.Ttc < 6)
        {
            Condition.LaneGoal = LaneOffset;
            Show('Return', { Level: 'Warn', Glyph: 'Car', Title: 'RETURNING TO LANE', Sub: `Oncoming car · ${Lead.Ttc.toFixed(1)} s`, Life: 3, Revive: true });
        }
        if (Keeps)
        {
            const Pace = Math.max(4, Math.abs(Condition.Speed));
            const WantSideways = THREE.MathUtils.clamp((Condition.LaneGoal - Condition.Z) * 0.7, -2.8, 2.8);
            const WantHeading = -Math.asin(THREE.MathUtils.clamp(WantSideways / Pace, -0.45, 0.45));
            SteerGoal = THREE.MathUtils.clamp(Math.atan((WantHeading - Condition.Heading) * 2.4 * Wheelbase / Pace), -Lock, Lock);
        }
    }
    Condition.Steer += (SteerGoal - Condition.Steer) * (1 - Math.exp(-Step * (SteerKey ? 3.2 : 6)));
    Condition.YawRate = Condition.Speed * Math.tan(Condition.Steer) / Wheelbase;
    Condition.Heading += Condition.YawRate * Step;
    Condition.X += Math.cos(Condition.Heading) * Condition.Speed * Step;
    Condition.Z -= Math.sin(Condition.Heading) * Condition.Speed * Step;
    CarRoot.position.set(Condition.X, 0, Condition.Z);
    CarRoot.rotation.y = Condition.Heading;
    Cabin.Rim.rotation.z = Condition.Steer * 4.2;
    if (Vehicle) for (const Each of Vehicle.Wheels) { Each.Spin.rotation.z -= Condition.Speed * Step / 0.509; if (Each.Front) Each.Steer.rotation.y = Condition.Steer; }

    // Rumble when a wheel runs on the edge line, crunch in the snow.
    const Edge = Math.abs(Condition.Z) + CarWidth / 2;
    Condition.Rumble = Edge > LaneWidth - 0.15 && Edge < LaneWidth + 0.9 ? 1 : OffRoad ? 0.45 : 0;

    // Collisions: pines along the verges and every car on the road.
    const Nose = { X: Condition.X + Math.cos(Condition.Heading) * 2.6, Z: Condition.Z - Math.sin(Condition.Heading) * 2.6 };
    if (Condition.Crash <= 0 && Math.abs(Condition.Speed) > 0.5 && (Scenery.Hit(Nose.X, Nose.Z, 1.1) || Scenery.Hit(Condition.X, Condition.Z, 1.2))) { Crash('HIT A TREE'); Show('Crash', { Level: 'Danger', Glyph: 'Hazard', Title: 'COLLISION · TREE', Sub: 'Back out with S (manual) · C toggles the autopilot', Life: 5 }); }
    for (const Car of Traffic?.Live ?? [])
    {
        const DX = Car.X - Condition.X, DZ = Car.Z - Condition.Z;
        const Forward = DX * Math.cos(Condition.Heading) - DZ * Math.sin(Condition.Heading), Across = DX * Math.sin(Condition.Heading) + DZ * Math.cos(Condition.Heading);
        if (Math.abs(Forward) < CarLength * 0.96 && Math.abs(Across) < CarWidth * 0.94)
        {
            Crash(Car.Lane < 0 ? 'HEAD-ON COLLISION' : 'COLLISION', Car);
            Show('Crash', { Level: 'Danger', Glyph: 'Car', Title: Car.Lane < 0 ? 'HEAD-ON COLLISION' : 'REAR-END COLLISION', Sub: 'The other car is moving off · drive on when clear', Life: 5 });
            break;
        }
    }
    if (Condition.Crash > 0) Condition.Crash -= Step;

    // Engine, fuel, altitude and cabin air.
    const Into = Condition.X - Events.Start;
    const Grade = Into > Course.GradeFrom && Into < Course.GradeTo ? 1 : 0;
    Condition.Grade = Grade;
    const Pace = Math.abs(Condition.Speed);
    Condition.Load = Math.pow(Pace / 30, 2) * 0.6 + Grade * 0.95 * Math.min(1, Pace / 22) + Condition.Throttle * 0.25;
    Condition.Coolant += (88 + Condition.Load * 40 - Condition.Coolant) * (1 - Math.exp(-Step * 0.13));
    if (Condition.Coolant >= 116) Condition.Hot = true;
    if (Condition.Coolant < 110) Condition.Hot = false;
    if (Condition.Fuelling !== 1) Condition.Fuel = Math.max(0, Condition.Fuel - Pace * Step * 0.0028 * (0.7 + Condition.Load * 0.6));
    Condition.Altitude = 2400 + Math.max(0, Into) * 0.52;
    Condition.Outside  = 6 - (Condition.Altitude - 2400) / 250;
    const Thin = 99 - Math.max(0, Condition.Altitude - 3000) / 1300 * 14;
    Condition.Oxygen += ((Condition.Boost ? 97 : Thin) - Condition.Oxygen) * (1 - Math.exp(-Step * 0.4));
    // Six-speed box for the engine note: RPM climbs through each band and drops on the shift.
    const Bands = [0, 22, 42, 66, 92, 120, 400], Kmhs = Pace * Kmh;
    const Gear = Math.max(1, Bands.findIndex((Top) => Kmhs < Top));
    const Share = (Kmhs - Bands[Gear - 1]) / (Bands[Gear] - Bands[Gear - 1]);
    const RpmGoal = Math.min(6800, (Gear === 1 ? 850 + Share * 3600 : 2300 + Share * 2900) + Condition.Throttle * 500);
    Condition.Rpm += (RpmGoal - Condition.Rpm) * (1 - Math.exp(-Step * 6));
    Condition.Gear = Condition.Speed < -0.1 ? 'R' : Gear;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EVENT SCRIPT
//------------------------------------------------------------------------------------------------------------------------

function Script(Step, Clock)
{
    const Into = Condition.X - Events.Start;
    const Front = Condition.X + FrontReach * Math.cos(Condition.Heading);

    // Lap wrap: re-arm the course and top the demo state back up so every event plays again.
    if (Into > LapLength)
    {
        Condition.Lap += 1;
        ArmLap();
        Object.assign(Condition, { Fuel: Math.min(Condition.Fuel, 21), Boost: false, Coolant: Math.min(Condition.Coolant, 92) });
        Show('Lap', { Level: 'Notice', Glyph: 'Ping', Title: `LAP ${Condition.Lap + 1}`, Sub: 'Night pass · course re-armed', Life: 4 });
    }

    // Teammate pings.
    if (!Events.Ping.Sent && Into > Course.PingAt)
    {
        Events.Ping.Sent = true;
        Show('Ping', { Level: 'Team', Glyph: 'Team', Title: 'KESTREL PINGED', Sub: 'Supply cache · marked on your left', Life: 9 });
    }
    if (!Events.Warn.Sent && Into > Course.WarnAt)
    {
        Events.Warn.Sent = true;
        Show('Warn', { Level: 'Team', Glyph: 'Team', Title: 'ORCA · TEAM', Sub: '“Porcupine on the road past the bend”', Life: 7 });
    }

    // Porcupine: waddles across once the car is close; clear when past our lane's right edge.
    const Critter = Events.Critter;
    if (!Critter.Crossing && Critter.X - Front < Course.CrossFrom) Critter.Crossing = true;
    if (Critter.Crossing && !Critter.Clear)
    {
        Critter.Z += Step * 0.95;
        Critter.Step += Step * 9;
        if (Critter.Z > Condition.Z + 2.6) Critter.Clear = true;
    }
    else if (Critter.Clear && Critter.Z < RoadHalf + 2) { Critter.Z += Step * 0.95; Critter.Step += Step * 9; }
    Porcupine.Group.position.set(Critter.X, Math.abs(Math.sin(Critter.Step)) * 0.015, Critter.Z);
    Porcupine.Legs.forEach((Leg, Index) => { Leg.rotation.x = Math.sin(Critter.Step + Index * Math.PI / 2) * 0.5; });
    const Shine = THREE.MathUtils.clamp(1 - (Critter.X - Front) / 160, 0, 1) * (Critter.X > Front ? 1 : 0);
    for (const Eye of Porcupine.Eyes) Eye.material.opacity = Shine;

    // Signal: wakes green with a short countdown when the car is 240 m out, so the driver watches it change.
    const Light = Events.Light;
    if (Light.Phase === 'Idle' && Light.X - Front < 250) Object.assign(Light, { Phase: 'Green', Left: SignalTimes.Green });
    if (Light.Phase !== 'Idle' && Light.Phase !== 'Go')
    {
        Light.Left -= Step;
        if (Light.Left <= 0)
        {
            const Next = { Green: 'Amber', Amber: 'Red', Red: 'Go' }[Light.Phase];
            Light.Phase = Next;
            Light.Left = SignalTimes[Next] ?? 0;
        }
    }
    const ToRed  = Light.Phase === 'Green' ? Light.Left + SignalTimes.Amber : Light.Phase === 'Amber' ? Light.Left : 0;
    const StopGap = Light.X + Signal.StopOffset - Front;
    const Arrive = Condition.Speed > 0.5 ? StopGap / Condition.Speed : Infinity;
    Light.ToRed = ToRed;
    Light.Must = (Light.Phase === 'Red') || ((Light.Phase === 'Green' || Light.Phase === 'Amber') && Arrive > ToRed - 0.4 && StopGap > 4);
    const Lit = { Idle: 2, Green: 2, Go: 2, Amber: 1, Red: 0 }[Light.Phase];
    Signal.Lamps.forEach((Lamp, Index) =>
    {
        const On = Index === Lit;
        Lamp.Lens.material.color.copy(Lamp.Colour).multiplyScalar(On ? 2.2 : 0.06);
        Lamp.Glow.material.opacity = On ? 0.9 : 0;
    });
    Signal.Spill.color.copy(Signal.Lamps[Lit].Colour);

    // Station suggestion: low fuel and the station within reach ahead.
    const StationGap = Events.StationX - Condition.X;
    if (!Condition.Routed && Condition.Fuel < 18 && StationGap > 220 && StationGap < 1400)
    {
        Show('Station', {
            Level: 'Warn', Glyph: 'Pump', Title: `FUEL ${Math.round(Condition.Fuel)}% · STATION AHEAD`, Sub: `Northpass · ${(StationGap / 1000).toFixed(1)} km on the right · +2 min`, Hint: 'E  ROUTE IN     Q  IGNORE',
            Action: () => { Condition.Routed = true; Show('Routed', { Level: 'Notice', Glyph: 'Pump', Title: 'ROUTE SET', Sub: 'Follow the chevrons into the bay', Life: 4 }); },
        });
    }
    else if (!Condition.Routed && Notices.has('Station') && StationGap < 220) Drop('Station');
    // Pull into the bay, fill up, rejoin.
    if (Condition.Routed)
    {
        const PumpGap = Events.StationX + Station.PumpX + 1 - Condition.X;
        if (Condition.Fuelling === 0) Condition.LaneGoal = PumpGap < 75 ? Station.BayZ : LaneOffset;
        if (Condition.Fuelling === 0 && PumpGap < 3 && Math.abs(Condition.Speed) < 0.3 && Math.abs(Condition.Z - Station.BayZ) < 3) Condition.Fuelling = 1;
        if (Condition.Fuelling === 1)
        {
            Condition.Fuel = Math.min(100, Condition.Fuel + Step * 18);
            Show('Fill', { Level: 'Notice', Glyph: 'Pump', Title: `REFUELLING ${Math.round(Condition.Fuel)}%`, Sub: `${(Condition.Fuel * 0.55).toFixed(1)} L in tank · paid by team card`, Dismiss: false });
            if (Condition.Fuel >= 100) { Condition.Fuelling = 2; Condition.LaneGoal = LaneOffset; Drop('Fill'); Show('Full', { Level: 'Notice', Glyph: 'Pump', Title: 'TANK FULL', Sub: 'Rejoining the pass', Life: 4 }); }
        }
        if (Condition.Fuelling === 2 && Math.abs(Condition.Z - LaneOffset) < 0.4) { Condition.Routed = false; Condition.Fuelling = 0; }
        if (PumpGap < -40 && Condition.Fuelling === 0) { Condition.Routed = false; Condition.LaneGoal = LaneOffset; Show('Missed', { Level: 'Notice', Glyph: 'Pump', Title: 'STATION MISSED', Sub: 'Route cleared', Life: 3 }); }
    }

    // Warnings from the gauges.
    if (Condition.Fuel < 15 && !Condition.Routed && Condition.Fuel >= 6) Show('FuelLow', { Level: 'Warn', Glyph: 'Fuel', Title: `FUEL LOW · ${Math.round(Condition.Fuel)}%`, Sub: `About ${Math.round(Condition.Fuel * 0.55 / 7.4 * 100)} km left at this pace` });
    else Drop('FuelLow');
    if (Condition.Coolant >= 108 && !Condition.Hot) Show('Heat', { Level: 'Warn', Glyph: 'Thermo', Title: `ENGINE ${Math.round(Condition.Coolant)} °C`, Sub: Condition.Grade ? 'Long climb · 9% grade · ease off' : 'Running hot · ease off', Revive: true });
    else if (!Condition.Hot) Drop('Heat');
    if (Condition.Hot) Drop('Heat');
    if (Condition.Oxygen < 92 && !Condition.Boost)
    {
        Show('Oxygen', { Level: Condition.Oxygen < 88 ? 'Danger' : 'Warn', Glyph: 'Oxygen', Title: `CABIN O₂ ${Math.round(Condition.Oxygen)}%`, Sub: `Altitude ${Math.round(Condition.Altitude).toLocaleString('en-GB')} m · thin air`, Hint: 'E  O₂ BOOST',
            Action: () => { Condition.Boost = true; Show('Boosted', { Level: 'Notice', Glyph: 'Oxygen', Title: 'O₂ BOOST ON', Sub: 'Cabin enrichment · recirculating', Life: 4 }); } });
    }
    else Drop('Oxygen');
    if (Condition.Outside <= 1 && !Events.Iced)
    {
        Events.Iced = true;
        Show('Ice', { Level: 'Notice', Glyph: 'Snow', Title: `ICE RISK · ${Math.round(Condition.Outside) + 0} °C`, Sub: 'Traction set to winter', Life: 6 });
    }

    // Overtake advice: behind a slow car in our lane, check the oncoming window against the time the pass needs.
    Condition.Advisory = null;
    const Lead = Condition.Lead;
    const Slow = Lead && !Lead.HeadOn && Lead.Gap < 70 && Lead.Along < Condition.Cruise / Kmh - 2.2 && Condition.Z > 0.8 && !Condition.Overtake && !Condition.Routed && Condition.Crash <= 0;
    if (Slow)
    {
        const Window = OncomingWindow();
        const Needs = (Lead.Gap + CarLength * 2 + 30) / Math.max(3, (Condition.Cruise + 15) / Kmh - Lead.Along) + 2.5;
        const Busy = (!Events.Critter.Clear && Events.Critter.X - Condition.X < 320 && Events.Critter.X > Condition.X) || (Events.Light.Phase !== 'Go' && Events.Light.X - Condition.X < 320 && Events.Light.X > Condition.X);
        const Clear = Window > Needs && !Busy;
        Condition.Advisory = Clear ? { Text: 'OVERTAKE CLEAR', Tone: Tones.Guide } : { Text: Busy ? 'NO OVERTAKE · HAZARD AHEAD' : `WAIT · ONCOMING ${Math.max(0, Math.round(Window))} s`, Tone: Tones.Warn };
        if (Clear && Condition.Auto) Show('Overtake', { Level: 'Notice', Glyph: 'Car', Title: 'OVERTAKE CLEAR', Sub: `Slow car ${Math.round(Lead.Along * Kmh)} km/h · oncoming ${Window === Infinity ? 'none' : `${Math.round(Window)} s`}`, Hint: 'E  OVERTAKE',
            Action: () => { const Target = Condition.Lead?.Car; if (!Target) return; Condition.Overtake = Target; Condition.LaneGoal = -LaneOffset; Condition.LaneHold = true; } });
        else Drop('Overtake');
    }
    else Drop('Overtake');
    if (Condition.Overtake)
    {
        const Target = Condition.Overtake;
        if (!Target.Live || Target.X < Condition.X - CarLength - 6) { Condition.Overtake = null; Condition.LaneGoal = LaneOffset; Show('Overtook', { Level: 'Notice', Glyph: 'Car', Title: 'OVERTAKE COMPLETE', Sub: 'Back in lane', Life: 2.5 }); }
        else if (Condition.Z < 0.5 && OncomingWindow() < 3.2) { Condition.Overtake = null; Condition.LaneGoal = LaneOffset; Show('Abort', { Level: 'Warn', Glyph: 'Car', Title: 'OVERTAKE ABORTED', Sub: 'Oncoming car · tucking back in', Life: 3 }); }
    }

    // Pings: retire, acknowledge, and the occasional teammate ping on a car or the verge ahead.
    for (const Ping of [...Pings])
    {
        if (Clock - Ping.Born > Ping.Life || (Ping.Follow?.Car && !Ping.Follow.Car.Live)) RemovePing(Ping);
    }
    for (const Reply of [...Replies])
    {
        if (Clock < Reply.Due) continue;
        Replies.splice(Replies.indexOf(Reply), 1);
        if (!Pings.includes(Reply.Ping)) continue;
        Reply.Ping.Ack = Reply.Mate;
        Show(`Reply${Reply.Due}`, { Level: 'Team', Glyph: 'Team', Title: `${Reply.Mate.toUpperCase()} · ${Reply.Ping.Call.Label}`, Sub: `“${ReplyLines[Reply.Ping.Call.Key]}”`, Life: 4 });
    }
    Condition.MateClock = (Condition.MateClock ?? 30) - Step;
    if (Condition.MateClock <= 0)
    {
        Condition.MateClock = 40 + Math.random() * 25;
        const Mate = Teammates[Math.floor(Math.random() * 2)];
        const Target = Traffic?.Live.find((Car) => Car.X - Condition.X > 60 && Car.X - Condition.X < 260);
        if (Target) PlacePing(PingByKey.Enemy, { Point: new THREE.Vector3(Target.X, 2.3, Target.Z), Follow: { Car: Target, Lift: 2.3 }, Label: 'CAR', Distance: Math.round(Target.X - Condition.X) }, Mate);
        else PlacePing(PingByKey.Supply, { Point: new THREE.Vector3(Condition.X + 180, 0, -(RoadHalf + 4)), Follow: null, Label: 'SNOW', Distance: 180 }, Mate);
    }

    // Age and retire cards.
    NoticeClock += Step;
    for (const [Id, Card] of Notices)
    {
        Card.Born ??= NoticeClock;
        Card.Age += Step;
        if (Card.Life && Card.Age > Card.Life && !Card.Leaving) Card.Leaving = 0.0001;
        if (Card.Leaving) { Card.Leaving += Step; if (Card.Leaving > 0.45) Notices.delete(Id); }
    }

    // Beacon pulse.
    Beacon.Ring.scale.setScalar(1 + (Clock * 0.6 % 1) * 0.6);
    Beacon.Ring.material.opacity = 0.8 * (1 - (Clock * 0.6 % 1));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GLASS PROJECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Where a world point lands on the glass: the ray from the driver's eye to the point, met with the glass plane, in
//    canvas pixels. Visible = in front of the eye and inside the glass.
const LocalEye = new THREE.Vector3(), LocalPoint = new THREE.Vector3(), Toward = new THREE.Vector3(), Hit = new THREE.Vector3();
const ToLocal = new THREE.Matrix4();
function OnGlass(World)
{
    LocalPoint.copy(World).applyMatrix4(ToLocal);
    Toward.copy(LocalPoint).sub(LocalEye);
    const Facing = Toward.dot(GlassNormal);
    const Reach  = Hit.copy(GlassCentre).sub(LocalEye).dot(GlassNormal) / (Facing || -1e-6);
    Hit.copy(LocalEye).addScaledVector(Toward, Reach).sub(GlassCentre);
    const X = (Hit.dot(GlassRight) / GlassWidth + 0.5) * GlassPixels[0];
    const Y = (0.5 - Hit.dot(GlassUp) / GlassHeight) * GlassPixels[1];
    const Ahead = Reach > 0 && Reach < 1;                                          // the glass lies between eye and point
    return { X, Y, Ahead, Inside: Ahead && X > 30 && X < GlassPixels[0] - 30 && Y > 30 && Y < GlassPixels[1] - 30, Distance: World.distanceTo(Camera.position) };
}
// Arrow on the glass border pointing at an off-glass target.
function EdgeArrow(Spot, Tone, Text)
{
    const [W, H] = GlassPixels;
    let DX = Spot.X - W * 0.5, DY = Spot.Y - H * 0.5;
    if (!Spot.Ahead) { DX = -DX; DY = -DY; }
    const Angle = Math.atan2(DY, DX);
    const Fit = Math.min((W * 0.5 - 70) / Math.max(1e-3, Math.abs(Math.cos(Angle))), (H * 0.5 - 70) / Math.max(1e-3, Math.abs(Math.sin(Angle))));
    return { X: W * 0.5 + Math.cos(Angle) * Fit, Y: H * 0.5 + Math.sin(Angle) * Fit, Angle, Tone, Text };
}
const Probe = new THREE.Vector3();
function RoadLine(Z, From, To, Every, Tone, Width, Travel = 1, Alpha = 1, Lift = 0.02)
{
    const Points = [];
    for (let Ahead = From; Ahead <= To; Ahead += Every)
    {
        const Spot = OnGlass(Probe.set(Condition.X + Ahead * Travel, Lift, Z));
        if (!Spot.Inside) { if (Points.length) break; continue; }
        Points.push([Spot.X, Spot.Y, THREE.MathUtils.clamp(1 - (Ahead - From) / (To - From), 0, 1)]);
    }
    return { Points, Tone, Width, Alpha };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    COMPOSE THE GLASS
//------------------------------------------------------------------------------------------------------------------------

let LastBanner = '', LastBrakeCue = 0, LastTick = null;
const Corner = new THREE.Vector3();
function Compose(Clock)
{
    CarRoot.updateMatrixWorld(true);
    ToLocal.copy(CarRoot.matrixWorld).invert();
    LocalEye.copy(Camera.position).applyMatrix4(ToLocal);
    const Cos = Math.cos(Condition.Heading);
    const Front = Condition.X + FrontReach * Cos;
    const Travel = Cos >= 0 ? 1 : -1;
    const Picture = { Clock, Guides: [], Chevrons: [], Tags: [], Edges: [], Cars: [], Cards: [], Banner: null };
    const Critter = Events.Critter, Light = Events.Light, Lead = Condition.Lead;
    const CritterGap = Critter.X - Front;
    const InPath = Critter.Crossing && !Critter.Clear && CritterGap > -2;
    const Oncoming = Lead && Lead.HeadOn && Lead.Ttc < 7 ? Lead : null;

    // Lane guides sit on the real edges of the lane the car is in. While changing lanes both lanes show, each fading
    // with how much of the car is in it, so the guides slide across with the car instead of jumping to the target.
    const OurTone = (InPath && CritterGap < 90) || (Lead && !Lead.HeadOn && Lead.Ttc < 2.2) ? Tones.Danger : Light.Must && Light.X - Front < 200 && Light.X - Front > 0 ? Tones.Warn : Tones.Guide;
    if (!Condition.Routed || Condition.Fuelling === 2)
        for (const Lane of [{ Z: LaneOffset, Far: false }, { Z: -LaneOffset, Far: true }])
        {
            const Share = THREE.MathUtils.clamp((3.4 - Math.abs(Condition.Z - Lane.Z)) / 2, 0, 1);
            if (Share < 0.04) continue;
            const Tone = Lane.Far ? (Oncoming ? Tones.Danger : Tones.Warn) : OurTone;
            for (const Edge of [-1.95, 1.95]) Picture.Guides.push(RoadLine(Lane.Z + Edge, 6, 72, 3, Tone, 7, Travel, Share));
        }

    // Stop bar across the lane at the signal's line.
    const StopX = Light.X + Signal.StopOffset;
    if (Light.Must && StopX - Front > 2 && StopX - Front < 220)
    {
        const Bar = [];
        for (let Z = -1.9; Z <= 1.91; Z += 0.38) { const Spot = OnGlass(Probe.set(StopX, 0.03, LaneOffset + Z)); if (Spot.Inside) Bar.push([Spot.X, Spot.Y, 1]); }
        Picture.Guides.push({ Points: Bar, Tone: Light.Phase === 'Red' || Light.ToRed < 2.2 ? Tones.Danger : Tones.Warn, Width: 12 });
    }

    // Porcupine tag from 180 m: amber at the roadside, red once it is in the path, green when it has cleared.
    if (CritterGap > -3 && CritterGap < 180)
    {
        const Spot = OnGlass(Probe.set(Critter.X, 0.35, Critter.Z));
        if (Spot.Inside)
        {
            const Danger = InPath && CritterGap < 90;
            Picture.Tags.push({ Shape: 'Hazard', X: Spot.X, Y: Spot.Y, Clock, Glyph: Critter.Clear ? 'Critter' : 'Hazard', Tone: Critter.Clear ? Tones.Guide : Danger ? Tones.Danger : Tones.Warn,
                Title: 'PORCUPINE', Big: `${Math.max(0, Math.round(CritterGap))} m`, Sub: Critter.Clear ? 'CLEARED' : Critter.Crossing ? 'CROSSING' : 'ROADSIDE', Ring: THREE.MathUtils.clamp(900 / Math.max(8, CritterGap), 18, 60), Scale: THREE.MathUtils.clamp(45 / Math.max(10, CritterGap), 1.0, 1.35) });
        }
    }

    // Signal countdown above the head from 260 m: green → amber → red as time runs out; then the time to green.
    const LightGap = Light.X - Front;
    if (Light.Phase !== 'Idle' && LightGap > -2 && LightGap < 260)
    {
        const Spot = OnGlass(Probe.set(Light.X, 5.1, LaneOffset));
        const Red = Light.Phase === 'Red', Go = Light.Phase === 'Go';
        const Tone = Go ? Tones.Guide : Red ? Tones.Danger : Light.ToRed > 4.5 ? Tones.Guide : Light.ToRed > 2 ? Tones.Warn : Tones.Danger;
        const Share = Red ? Light.Left / SignalTimes.Red : Go ? 1 : Light.ToRed / (SignalTimes.Green + SignalTimes.Amber);
        const Piece = { Shape: 'Signal', X: Spot.X, Y: Spot.Y - 96, Clock, Tone, Share, Scale: THREE.MathUtils.clamp(80 / Math.max(20, LightGap), 1.0, 1.35),
            Big: Go ? 'GO' : `${(Red ? Light.Left : Light.ToRed).toFixed(1)}`, Title: Go ? 'GREEN' : Red ? 'S TO GREEN' : 'S TO RED', Sub: Go ? '' : Red ? `STOP · ${Math.max(0, Math.round(StopX - Front))} m` : Light.Must ? 'PREPARE TO STOP' : 'YOU WILL MAKE IT' };
        if (Spot.Inside) Picture.Tags.push(Piece);
        // Countdown tick each whole second; sharper in the last two seconds before red.
        const Whole = Go ? null : Math.ceil(Red ? Light.Left : Light.ToRed);
        if (Whole !== LastTick) { LastTick = Whole; if (Whole !== null) Sound.Play('Tick', !Red && Light.ToRed <= 2.2); }
        else Picture.Edges.push(EdgeArrow(Spot, Tone, Go ? 'GO' : `${(Red ? Light.Left : Light.ToRed).toFixed(0)} s`));
    }

    // Teammate beacon: tag on the glass or an arrow at the glass edge while it is out of view.
    const BeaconGap = Events.BeaconX - Condition.X;
    if (Events.Ping.Sent && BeaconGap > -60)
    {
        const Spot = OnGlass(Probe.set(Events.BeaconX, 2.5, Course.BeaconZ));
        const Range = Math.round(Math.hypot(BeaconGap, Course.BeaconZ - Condition.Z));
        if (Spot.Inside) Picture.Tags.push({ Shape: 'Ping', X: Spot.X, Y: Spot.Y, Clock, Glyph: 'Ping', Tone: Tones.Team, Title: 'KESTREL', Big: `${Range} m`, Sub: 'SUPPLY CACHE', Ring: 16, Scale: 0.85 });
        else Picture.Edges.push(EdgeArrow(Spot, Tones.Team, `${Range} m`));
    }

    // Station: point of interest when suggested or routed, and chevrons painted along the route into the bay.
    const StationGap = Events.StationX - Condition.X;
    if ((Notices.has('Station') || Condition.Routed) && StationGap > -10)
    {
        const Spot = OnGlass(Probe.set(Events.StationX, 7.2, Station.BayZ));
        if (Spot.Inside) Picture.Tags.push({ Shape: 'Poi', X: Spot.X, Y: Spot.Y, Clock, Glyph: 'Pump', Tone: Condition.Routed ? Tones.Guide : Tones.Warn, Title: 'NORTHPASS FUEL', Big: StationGap > 1000 ? `${(StationGap / 1000).toFixed(1)} km` : `${Math.max(0, Math.round(StationGap))} m`, Sub: Condition.Routed ? 'ROUTED' : 'E TO ROUTE', Ring: 14, Scale: 0.85 });
        else if (StationGap > 0) Picture.Edges.push(EdgeArrow(Spot, Condition.Routed ? Tones.Guide : Tones.Warn, `${Math.round(StationGap)} m`));
    }
    if (Condition.Routed && Condition.Fuelling === 0)
    {
        const PumpX = Events.StationX + Station.PumpX;
        const Bend = (X) => THREE.MathUtils.smoothstep(X, PumpX - 78, PumpX - 30);
        for (let Ahead = 8; Ahead < 90; Ahead += 5.5)
        {
            const X = Condition.X + Ahead;
            if (X > PumpX + 2) break;
            const Z  = THREE.MathUtils.lerp(LaneOffset, Station.BayZ, Bend(X));
            const Z2 = THREE.MathUtils.lerp(LaneOffset, Station.BayZ, Bend(X + 2));
            const A = OnGlass(Probe.set(X, 0.03, Z)), B = OnGlass(new THREE.Vector3(X + 2, 0.03, Z2));
            if (!A.Inside || !B.Inside) continue;
            Picture.Chevrons.push({ X: A.X, Y: A.Y, Angle: Math.atan2(B.Y - A.Y, B.X - A.X) + Math.PI / 2, Scale: THREE.MathUtils.clamp(14 / Ahead, 0.25, 1.4), Tone: Tones.Guide, Alpha: 0.35 + 0.65 * Math.max(0, Math.sin(Clock * 5 - Ahead * 0.12)) });
        }
    }

    // Traffic boxes: corner brackets fitted to each car's outline. The car in your path gets the gap and its speed;
    // a car coming at you in your lane turns red with the time to impact.
    for (const Car of Traffic?.Live ?? [])
    {
        const Distance = Math.hypot(Car.X - Condition.X, Car.Z - Condition.Z);
        if (Distance > 170 || Distance < 11) continue;                 // too far to matter, or already alongside
        let Left = Infinity, Right = -Infinity, Top = Infinity, Bottom = -Infinity, Seen = true;
        for (const SX of [-3.1, 3.1]) for (const SY of [0.25, 1.85]) for (const SZ of [-1.25, 1.25])
        {
            const Spot = OnGlass(Corner.set(Car.X + SX * Car.Lane, SY, Car.Z + SZ * Car.Lane));
            if (!Spot.Ahead) Seen = false;
            Left = Math.min(Left, Spot.X); Right = Math.max(Right, Spot.X); Top = Math.min(Top, Spot.Y); Bottom = Math.max(Bottom, Spot.Y);
        }
        const MidX = (Left + Right) / 2, MidY = (Top + Bottom) / 2;
        if (!Seen || Right - Left > 760 || MidX < 30 || MidX > GlassPixels[0] - 30 || MidY < 30 || MidY > GlassPixels[1] - 30) continue;
        const Kmhs = Math.round(Car.Speed * Kmh);
        let Look;
        if (Lead?.Car === Car && Lead.HeadOn) Look = { Tone: Tones.Danger, Strong: true, Title: 'ONCOMING', Sub: Lead.Ttc < 30 ? `${Lead.Ttc.toFixed(1)} s` : `${Math.round(Lead.Gap)} m` };
        else if (Lead?.Car === Car) Look = { Tone: Lead.Ttc < 1.8 ? Tones.Danger : Lead.Ttc < 3.5 ? Tones.Warn : Tones.Notice, Strong: true, Title: `${Math.max(0, Math.round(Lead.Gap))} m`, Sub: `${Kmhs} km/h` };
        else if (Car.Lane < 0) Look = { Tone: Tones.Calm, Alpha: 0.55, Title: Distance < 90 ? `${Kmhs} km/h` : null };
        else Look = { Tone: Tones.Notice, Alpha: 0.65, Title: Distance < 130 ? `${Math.round(Distance)} m` : null };
        Picture.Cars.push({ Left: Left - 4, Top: Top - 4, Right: Right + 4, Bottom: Bottom + 4, ...Look });
    }

    // Squad pings: on the glass where they sit in the world, or an arrow at the glass edge.
    for (const Ping of Pings)
    {
        const World = PingSpot(Ping);
        const Range = Math.round(World.distanceTo(Camera.position));
        const Spot = OnGlass(World);
        const Alpha = THREE.MathUtils.clamp((Ping.Life - (Clock - Ping.Born)) / 1.5, 0, 1);
        if (Spot.Inside) Picture.Tags.push({ Shape: 'Mark', X: Spot.X, Y: Spot.Y, Tone: Ping.Call.Tone, Glyph: Ping.Call.Glyph, Alpha, Age: Clock - Ping.Born,
            Title: Ping.Owner === 'You' ? Ping.Call.Label : `${Ping.Owner.toUpperCase()} · ${Ping.Call.Label}`, Big: `${Range} m`, Sub: Ping.Ack ? `ACK · ${Ping.Ack.toUpperCase()}` : '' });
        else Picture.Edges.push(EdgeArrow(Spot, Ping.Call.Tone, `${Range} m`));
    }

    // Combiner strip, gauges, cards and the banner.
    const Danger = Condition.Crash > 0 || (Oncoming && Condition.Z < 1) || Condition.Brake || Condition.Hot || (Condition.Oxygen < 88 && !Condition.Boost) || Condition.Fuel < 6;
    const Warn = Notices.has('Heat') || Notices.has('FuelLow') || Notices.has('Oxygen') || Notices.has('Station') || (Light.Must && LightGap < 220 && LightGap > 0);
    Picture.RimTone = Danger ? Tones.Danger : Warn ? Tones.Warn : Tones.Notice;
    Picture.Brake = Condition.Brake;
    Picture.Advisory = Condition.Advisory;
    Picture.Speed = Math.abs(Condition.Speed) * Kmh;
    Picture.Auto  = Condition.Gear === 'R' ? 'REVERSE' : Condition.Auto ? `AUTO ${Math.round(Condition.Cruise)}` : `MANUAL · ${Condition.Gear}`;
    Picture.Nav   = Condition.Routed && Condition.Fuelling === 0 ? { Glyph: 'Right', Text: `${Math.max(0, Math.round(StationGap - 40))} m` } : { Glyph: 'Up', Text: `${((LapLength - (Condition.X - Events.Start)) / 1000).toFixed(1)} km` };
    const Now = new Date();
    Picture.Footer = `${String(Now.getHours()).padStart(2, '0')}:${String(Now.getMinutes()).padStart(2, '0')}   ${Math.round(Condition.Outside) + 0} °C   ${Math.round(Condition.Altitude).toLocaleString('en-GB')} m`;
    Picture.Gauges = [
        { Glyph: 'Fuel',   Text: `${Math.round(Condition.Fuel)}%`,    Tone: Condition.Fuel < 6 ? Tones.Danger : Condition.Fuel < 15 ? Tones.Warn : Tones.Calm },
        { Glyph: 'Thermo', Text: `${Math.round(Condition.Coolant)}°`, Tone: Condition.Hot ? Tones.Danger : Condition.Coolant >= 108 ? Tones.Warn : Tones.Calm },
        { Glyph: 'Oxygen', Text: `${Math.round(Condition.Oxygen)}%`,  Tone: Condition.Boost ? Tones.Notice : Condition.Oxygen < 88 ? Tones.Danger : Condition.Oxygen < 92 ? Tones.Warn : Tones.Calm },
    ];
    Picture.Cards = [...Notices.values()].sort((A, B) => (A.Leaving ? 1 : 0) - (B.Leaving ? 1 : 0) || Rank[A.Level] - Rank[B.Level] || A.Born - B.Born).slice(0, 4)
        .map((Card) => ({ ...Card, Tone: Tones[Card.Level === 'Team' ? 'Team' : Card.Level] }));
    if (Condition.Crash > 0) Picture.Banner = { Clock, Tone: Tones.Danger, Glyph: 'Hazard', Text: Condition.CrashText, Sub: 'Impact detected · car OK' };
    else if (Oncoming && Condition.Z < 1) Picture.Banner = { Clock, Tone: Tones.Danger, Glyph: 'Car', Text: 'ONCOMING TRAFFIC', Sub: `Return to your lane · ${Oncoming.Ttc.toFixed(1)} s` };
    else if (Condition.Hot) Picture.Banner = { Clock, Tone: Tones.Danger, Glyph: 'Thermo', Text: 'ENGINE OVERHEAT', Sub: `${Math.round(Condition.Coolant)} °C · holding 40 km/h until it cools` };
    else if (Condition.Oxygen < 88 && !Condition.Boost) Picture.Banner = { Clock, Tone: Tones.Danger, Glyph: 'Oxygen', Text: 'OXYGEN LOW', Sub: `Cabin ${Math.round(Condition.Oxygen)}% · press E for O₂ boost` };
    else if (Condition.Fuel < 6) Picture.Banner = { Clock, Tone: Tones.Danger, Glyph: 'Fuel', Text: 'FUEL CRITICAL', Sub: `${Math.round(Condition.Fuel)}% · find fuel now` };
    else if (Condition.OffRoad) Picture.Banner = { Clock, Tone: Tones.Warn, Glyph: 'Hazard', Text: 'OFF ROAD', Sub: 'Steer back to the road · A / D' };
    // Sound for the banner and the BRAKE flash.
    const BannerText = Picture.Banner?.Text ?? '';
    if (BannerText !== LastBanner) { LastBanner = BannerText; if (BannerText) Sound.Play(Picture.Banner.Tone === Tones.Danger ? 'Danger' : 'Warn'); }
    if (Picture.Brake && Clock - LastBrakeCue > 0.45) { LastBrakeCue = Clock; Sound.Play('Danger'); }
    Picture.Centre   = GlassAnchor.Centre;
    Picture.CardLeft = GlassAnchor.Centre + 440;
    Picture.CardTop  = 300;
    Painter.Paint(Picture);
    GlassMap.needsUpdate = true;

    // Spill tone on the dash, vignette when the air is thin.
    Cabin.Spill.color.set(Picture.RimTone);
    Cabin.ProjectorGlow.material.color.set(Picture.RimTone);
    Vignette.style.opacity = String(THREE.MathUtils.clamp((92 - Condition.Oxygen) / 8, 0, 1) * (Condition.Boost ? 0.2 : 0.85));
    return Picture;
}
// The driver's straight-ahead column on the glass (the combiner and banner centre on it).
const GlassAnchor = { Centre: 0 };
{
    const Far = Eye.clone().add(new THREE.Vector3(100, -1.2, 0));
    const Toward = Far.clone().sub(Eye);
    const Reach = GlassCentre.clone().sub(Eye).dot(GlassNormal) / Toward.dot(GlassNormal);
    const Spot = Eye.clone().addScaledVector(Toward, Reach).sub(GlassCentre);
    GlassAnchor.Centre = (Spot.dot(GlassRight) / GlassWidth + 0.5) * GlassPixels[0];
}
const Vignette = document.getElementById('Vignette');
const Flash = document.getElementById('Flash');

// Traffic stop points: the porcupine (both lanes while it crosses) and each direction's stop line at the signal.
function TrafficStop(Car)
{
    let Gap = Infinity;
    const Front = Car.X + Car.Lane * CarLength / 2;
    const Critter = Events.Critter;
    if (Critter.Crossing && Math.abs(Critter.Z) < RoadHalf + 0.5 && Math.abs(Critter.Z - Car.Z) < 3.4)
    {
        const Ahead = (Critter.X - Front) * Car.Lane;
        if (Ahead > -1) Gap = Math.min(Gap, Ahead - 5);
    }
    const Light = Events.Light;
    const Ahead = (Light.X + (Car.Lane > 0 ? Signal.StopOffset : -Signal.StopOffset) - Front) * Car.Lane;
    const Stops = Light.Phase === 'Red' || (Light.Phase === 'Amber' && Ahead > 22) || (Light.Phase === 'Green' && Light.ToRed < 2 && Ahead > 30);
    if (Ahead > -0.5 && Stops) Gap = Math.min(Gap, Ahead - 1);
    return Gap;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FRAME
//------------------------------------------------------------------------------------------------------------------------

let Last = performance.now(), Clock = 0, LastPicture = null;
let StepCap = 0.25;                                                // [s] - longest simulated step per drawn frame
const Holds = [];                                                  // test presses: { Key, Until } in simulated time
// Whoosh as a car goes past the cockpit, panned to its side and louder with the closing speed.
function WhooshSounds()
{
    const Cos = Math.cos(Condition.Heading), Sin = Math.sin(Condition.Heading);
    for (const Car of Traffic.Live)
    {
        const DX = Car.X - Condition.X, DZ = Car.Z - Condition.Z;
        const Forward = DX * Cos - DZ * Sin, Across = DX * Sin + DZ * Cos;
        if (Car.LastForward > 0 && Forward <= 0 && Math.abs(Across) < 9)
            Sound.Play('Whoosh', Across < 0 ? -0.75 : 0.75, Math.min(0.5, 0.12 + Math.abs(Condition.Speed - Car.Speed * Car.Lane * Cos) / 55));
        Car.LastForward = Forward;
    }
}
function Advance()
{
    requestAnimationFrame(Advance);                                    // first, so one faulty frame can never freeze the game
    const Now  = performance.now();
    const Step = Math.min(StepCap, (Now - Last) / 1000);
    Last = Now;
    // Physics in small slices so a slow frame does not tunnel through the stop logic.
    const Slices = Math.ceil(Step / 0.02);
    for (let Slice = 0; Slice < Slices; Slice++)
    {
        Clock += Step / Slices;
        for (const Hold of Holds) { if (Clock < Hold.Until) Keys.add(Hold.Key); else { Keys.delete(Hold.Key); Hold.Done = true; } }
        for (let Index = Holds.length - 1; Index >= 0; Index--) if (Holds[Index].Done) Holds.splice(Index, 1);
        Traffic.Update(Step / Slices, Condition); Drive(Step / Slices); Script(Step / Slices, Clock);
    }
    Traffic.Present(Condition, Camera.position);
    WhooshSounds();

    // Head: eases towards the mouse, leans a little into the turn of the head, rolls a touch with the cornering load.
    const Ease = 1 - Math.exp(-Step * 6);
    Condition.Yaw   += (Condition.YawGoal - Condition.Yaw) * Ease;
    Condition.Pitch += (Condition.PitchGoal - Condition.Pitch) * Ease;
    Condition.Roll  += (THREE.MathUtils.clamp(-Condition.YawRate * Condition.Speed * 0.003, -0.03, 0.03) - Condition.Roll) * Ease;
    Camera.fov += (Condition.Fov - Camera.fov) * Ease;
    Camera.updateProjectionMatrix();
    const Head = Eye.clone().add(new THREE.Vector3(Math.abs(Condition.Yaw) * 0.04, Math.sin(Clock * 1.7) * 0.002 * Math.min(1, Math.abs(Condition.Speed) / 10), -Condition.Yaw * 0.07));
    CarRoot.updateMatrixWorld(true);
    Camera.position.copy(Head).applyMatrix4(CarRoot.matrixWorld);
    Camera.rotation.set(Condition.Pitch, -Math.PI / 2 + Condition.Yaw + Condition.Heading, Condition.Roll);

    Scenery.Update(Condition.X, 1, Condition.Z);
    const Beat = (Clock * 0.8) % 1;
    for (const Pillar of Pillars) if (Pillar.Busy) { Pillar.Ring.scale.setScalar(1 + Beat * 0.8); Pillar.Ring.material.opacity = 0.8 * (1 - Beat); }
    Sound.Drive({ Rpm: Condition.Rpm, Throttle: Condition.Throttle, Speed: Condition.Speed, Rumble: Condition.Rumble, Fuelling: Condition.Fuelling === 1 });
    LastPicture = Compose(Clock);
    Wheel.Draw(Clock);
    Renderer.render(Scene, Camera);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    START
//------------------------------------------------------------------------------------------------------------------------

async function Start()
{
    await Promise.all([300, 400, 500].map((Weight) => document.fonts.load(`${Weight} 40px "DM Sans"`)));
    Vehicle = await AssembleVehicle({ Finish });
    CarRoot.add(Vehicle.Root);
    Traffic = MakeTraffic(Scene, Vehicle, TrafficStop);
    Traffic.Start(Condition);
    document.getElementById('Loading').classList.add('Done');
    Show('Hello', { Level: 'Notice', Glyph: 'Ping', Title: 'NIGHT PASS', Sub: 'Autopilot 80 km/h · A / D steer · middle mouse to ping', Life: 7, Quiet: true });
    Condition.Speed = 16;
    requestAnimationFrame(Advance);
}
Start().catch((Problem) => { console.error(Problem); document.getElementById('Loading').querySelector('span').textContent = `Could not load the vehicle · ${Problem.message}`; });

//------------------------------------------------------------------------------------------------------------------------
//                                                    TEST HOOKS
//------------------------------------------------------------------------------------------------------------------------

// Jump(Into) puts the car Into metres down the current lap (events behind it count as done) and refills the traffic.
window.VisorCondition = {
    Condition,
    Notices,
    Pings,
    Events,
    Sound,
    get Picture() { return LastPicture; },
    get Traffic() { return Traffic; },
    Jump(Into, Patch = {})
    {
        Condition.X = Events.Start + Into;
        Notices.clear();
        if (Into > Course.PingAt) Events.Ping.Sent = true;
        if (Into > Course.WarnAt) Events.Warn.Sent = true;
        if (Into > Course.PorcupineAt + 10) Object.assign(Events.Critter, { Crossing: true, Clear: true, Z: RoadHalf + 2 });
        if (Into > Course.SignalAt) Object.assign(Events.Light, { Phase: 'Go', Left: 0 });
        Object.assign(Condition, { Heading: 0, Steer: 0, Overtake: null, Crash: 0, LaneHold: true }, Patch);
        if (Patch.LaneGoal === undefined) Condition.LaneGoal = Condition.Z < 0 ? -LaneOffset : LaneOffset;
        Traffic?.Reset(Condition, Patch.Traffic ?? true);
    },
    Look(Yaw, Pitch) { Condition.YawGoal = Yaw; Condition.PitchGoal = Pitch; },
    Signal(Phase, Left) { Object.assign(Events.Light, { Phase, Left }); },
    // Holds Key for Seconds of simulated time (exact even when a slow renderer draws few frames).
    Press(Key, Seconds) { Holds.push({ Key, Until: Clock + Seconds }); },
    // Lets one drawn frame cover up to Seconds of simulation, so captures on a software renderer keep real pace.
    Pace(Seconds) { StepCap = Seconds; },
    Place(Lane, Ahead, SpeedKmh) { return Traffic.Place(Lane, Condition.X + Ahead, SpeedKmh / Kmh); },
    OpenWheel(X, Y, Index) { Pointer.X = X; Pointer.Y = Y; OpenWheel(X, Y); if (Index !== undefined) Wheel.Pick(Index); },
    CloseWheel,
    Ping(Key, X, Y) { const Spot = Aim(X, Y); return PlacePing(PingByKey[Key] ?? PingByKey[Spot.Suggest], Spot); },
    Act,
    Dismiss,
};

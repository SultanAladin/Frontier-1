// LOBBY CAR
// 🧩 Configurator parts for the lobby's ControlVehicle: bolt-on components (underglow, roof rack, light bar, front
//    splitter, side skirts), the see-through body skin, and the drivetrain cutaway (engine, gearbox, shafts,
//    differentials, battery and motors) rebuilt for each engine, layout and gearbox choice, with game-scale figures.
// 📝 Positions are measured from the ControlVehicle facets: body-local y is ground height minus BodyLift; axles at
//    x = +1.727 / −1.669, track ±1.048, axle height 0.509, nose +3.11, tail −3.11, hood top ≈ 1.14, roof ≈ 1.77 (ground).

import { THREE } from '../Shared/VehicleAssembly.js';
import { Painted } from './LobbyKit.js';

export const BodyLift = 0.4176;
const FrontAxle = 1.7274, RearAxle = -1.6686, Track = 1.0475, Axle = 0.509;
const Local = (Y) => Y - BodyLift;                                  // ground height → body-local height

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHARED
//------------------------------------------------------------------------------------------------------------------------

const Lit = (Colour, Extra = {}) => new THREE.MeshBasicMaterial({ color: Colour, toneMapped: false, ...Extra });
const Box = (Width, Height, Depth, Material, X, Y, Z) =>
{
    const Mesh = new THREE.Mesh(new THREE.BoxGeometry(Width, Height, Depth), Material);
    Mesh.position.set(X, Y, Z);
    Mesh.castShadow = true;
    return Mesh;
};

let GlowCard = null;
/** Soft rounded-rectangle glow, used for the underglow pool on the floor. */
function GlowTexture()
{
    GlowCard ??= Painted(512, 256, (Context, Width, Height) =>
    {
        Context.fillStyle = '#000';
        Context.fillRect(0, 0, Width, Height);
        Context.filter = 'blur(34px)';
        Context.fillStyle = '#fff';
        Context.beginPath();
        Context.roundRect(96, 70, Width - 192, Height - 140, 50);
        Context.fill();
    }, { Anisotropy: 1 });
    return GlowCard;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    BOLT-ON PARTS
//------------------------------------------------------------------------------------------------------------------------

/**
 * Adds the bolt-on parts to the car. Body parts go on the BodyMount (so they follow ride height and the mirror twin
 * copies them); the underglow pool and its lights are returned in Ground, which the caller adds after the twin.
 * @returns {{ Ground: THREE.Group, Apply: (Look: object) => void, Tick: (Elapsed: number) => void, Ghost: (On: boolean) => void }}
 */
export function MountParts(Car)
{
    const Mount = Car.BodyMount;
    const Carbon = new THREE.MeshStandardMaterial({ color: '#141518', roughness: 0.32, metalness: 0.35 });
    const Rail = new THREE.MeshStandardMaterial({ color: '#1b1c1f', roughness: 0.4, metalness: 0.8 });

    // Underglow: LED strips under the sills and nose/tail, a soft pool on the floor and two coloured lights.
    const GlowStrip = Lit('#36e6ff');
    const Glow = new THREE.Group();
    for (const Side of [-1, 1]) Glow.add(Box(4.2, 0.03, 0.04, GlowStrip, -0.05, Local(0.33), Side * 1.1));
    for (const X of [2.75, -2.7]) Glow.add(Box(0.04, 0.03, 1.8, GlowStrip, X, Local(0.36), 0));
    Glow.traverse((Each) => { Each.castShadow = false; });
    Mount.add(Glow);
    const Ground = new THREE.Group();
    const Pool = new THREE.Mesh(new THREE.PlaneGeometry(8.4, 4.4).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ map: GlowTexture(), color: '#36e6ff', transparent: true, opacity: 0.9, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
    Pool.position.y = 0.014;
    Pool.renderOrder = 6;
    const Lamps = [1.2, -1.2].map((X) => { const Lamp = new THREE.PointLight('#36e6ff', 0, 5, 1.6); Lamp.position.set(X, 0.22, 0); Ground.add(Lamp); return Lamp; });
    Ground.add(Pool);

    // Roof rack: two rails on four feet and three cross bars.
    const Rack = new THREE.Group();
    for (const Side of [-1, 1])
    {
        Rack.add(Box(1.15, 0.035, 0.05, Rail, 0, Local(1.85), Side * 0.68));
        for (const X of [-0.45, 0.45]) Rack.add(Box(0.07, 0.09, 0.07, Rail, X, Local(1.8), Side * 0.68));
    }
    for (const X of [-0.42, 0, 0.42]) Rack.add(Box(0.05, 0.03, 1.5, Rail, X, Local(1.875), 0));
    Mount.add(Rack);

    // Light bar: housing on the front of the roof with a lit face, beads and a forward spot.
    const Bar = new THREE.Group();
    Bar.position.set(0.62, Local(1.9), 0);
    Bar.add(Box(0.16, 0.11, 1.5, Rail, 0, 0, 0));
    for (const Side of [-1, 1]) Bar.add(Box(0.05, 0.22, 0.05, Rail, -0.08, -0.13, Side * 0.68));
    const BarFace = new THREE.Mesh(new THREE.PlaneGeometry(1.42, 0.07).rotateY(Math.PI / 2), Lit('#ffffff', { transparent: true, opacity: 0.25 }));
    BarFace.position.x = 0.082;
    Bar.add(BarFace);
    const BarLamp = new THREE.SpotLight('#f2f4ff', 0, 40, 0.5, 0.6, 1.4);
    BarLamp.target.position.set(14, -1.6, 0);
    Bar.add(BarLamp, BarLamp.target);
    Mount.add(Bar);

    // Front splitter with end plates, and side skirts between the wheel arches.
    const Splitter = new THREE.Group();
    Splitter.add(Box(0.42, 0.028, 2.36, Carbon, 3.02, Local(0.39), 0));
    for (const Side of [-1, 1]) Splitter.add(Box(0.3, 0.12, 0.02, Carbon, 3.0, Local(0.44), Side * 1.18));
    Mount.add(Splitter);
    const Skirts = new THREE.Group();
    for (const Side of [-1, 1]) Skirts.add(Box(2.3, 0.1, 0.05, Carbon, -0.02, Local(0.4), Side * 1.265));
    Mount.add(Skirts);

    const Tone = new THREE.Color();
    let GlowColour = '#36e6ff', GlowMode = 'Steady', GlowOn = false, LampsOn = true, Ghosted = false, Last = null;
    const Apply = (Look) =>
    {
        Last = Look;
        GlowOn = Look.Glow && !Ghosted;
        GlowMode = Look.GlowMode;
        GlowColour = Look.GlowColour;
        Glow.visible = Pool.visible = GlowOn;
        Rack.visible = Look.Rack && !Ghosted;
        Bar.visible = Look.Bar && !Ghosted;
        Splitter.visible = Look.Splitter && !Ghosted;
        Skirts.visible = Look.Skirts && !Ghosted;
        LampsOn = Look.Lamps;
        BarFace.material.opacity = Look.Bar && LampsOn ? 1 : 0.25;
        BarLamp.intensity = Look.Bar && LampsOn ? 220 : 0;
        Tick(performance.now() / 1000);
    };
    /** Underglow animation: Steady, Breathe (slow pulse) or Cycle (hue sweep). */
    const Tick = (Elapsed) =>
    {
        if (!GlowOn) { for (const Lamp of Lamps) Lamp.intensity = 0; return; }
        Tone.set(GlowColour);
        let Level = 1;
        if (GlowMode === 'Breathe') Level = 0.35 + 0.65 * (0.5 + 0.5 * Math.sin(Elapsed * 2.2));
        if (GlowMode === 'Cycle') Tone.setHSL((Elapsed * 0.08) % 1, 1, 0.55);
        GlowStrip.color.copy(Tone).multiplyScalar(1.6 * Level + 0.2);
        Pool.material.color.copy(Tone);
        Pool.material.opacity = 0.85 * Level * (Ground.userData.Fade ?? 1);
        for (const Lamp of Lamps) { Lamp.color.copy(Tone); Lamp.intensity = 7 * Level * (Ground.userData.Fade ?? 1); }
    };
    /** Hides the bolt-ons while the drivetrain cutaway is showing. */
    const Ghost = (On) => { if (On !== Ghosted) { Ghosted = On; if (Last) Apply(Last); } };
    return { Ground, Apply, Tick, Ghost };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SEE-THROUGH SKIN
//------------------------------------------------------------------------------------------------------------------------

/**
 * Swaps the body, wing and wheels for a faint glass shell with feature edges, so the drivetrain shows through.
 * @returns {{ Set: (Level: number) => void }}
 */
export function XraySkin(Car)
{
    const Shell = new THREE.MeshBasicMaterial({ color: '#9fdcff', transparent: true, opacity: 0, depthWrite: false, toneMapped: false, side: THREE.DoubleSide });
    const Edge = new THREE.LineBasicMaterial({ color: '#cdeeff', transparent: true, opacity: 0, depthWrite: false, toneMapped: false });
    const Skins = [];
    const Wrap = (Mesh) =>
    {
        const Lines = new THREE.LineSegments(new THREE.EdgesGeometry(Mesh.geometry, 30), Edge);
        Lines.visible = false;
        Mesh.add(Lines);
        Skins.push({ Mesh, Solid: Mesh.material, Glass: Array.isArray(Mesh.material) ? Mesh.material.map(() => Shell) : Shell, Lines });
    };
    Wrap(Car.Body);
    Car.Wing?.traverse((Each) => { if (Each.isMesh) Wrap(Each); });
    for (const Wheel of Car.Wheels) Wheel.Steer.traverse((Each) => { if (Each.isMesh) Wrap(Each); });
    let Shown = false;
    return {
        Set(Level)
        {
            const On = Level > 0.01;
            if (On !== Shown)
            {
                Shown = On;
                for (const Skin of Skins) { Skin.Mesh.material = On ? Skin.Glass : Skin.Solid; Skin.Lines.visible = On; Skin.Mesh.castShadow = !On; }
            }
            Shell.opacity = 0.07 * Level;
            Edge.opacity = 0.5 * Level;
        },
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DRIVETRAIN CUTAWAY
//------------------------------------------------------------------------------------------------------------------------

const Tones = { Engine: '#ff9a4a', Gearbox: '#5fe3ff', Clutch: '#ffe066', Shaft: '#dfe6ee', Flow: '#6dff9e', Diff: '#ffffff', Battery: '#5fe3ff', Motor: '#c9b6ff', Cable: '#ff8a2a', Tank: '#ffd36b', Exhaust: '#8d949c', Idle: '#5a6270' };

const FlowVertex = 'varying vec2 Along; void main(){ Along = uv; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }';
const FlowFragment = `uniform vec3 Tone; uniform float Time; uniform float Show; uniform float Count; uniform float Speed; varying vec2 Along;
    void main(){ float Wave = fract(Along.x * Count - Time * Speed); float Dash = smoothstep(0.0, 0.25, Wave) * (1.0 - smoothstep(0.35, 0.6, Wave));
        gl_FragColor = vec4(Tone * (0.45 + Dash * 1.6), Show * (0.35 + Dash * 0.65)); }`;

/** Game-scale figures for a drivetrain choice. */
export function DriveFigures({ Engine, Layout, Gearbox })
{
    const Electric = Engine === 'Electric';
    const Power = Electric ? (Layout === 'AWD' ? 640 : 420) : Engine === 'V8' ? 560 : 380;
    const Weight = 1450 + (Engine === 'V8' ? 80 : 0) + (Electric ? 350 : 0) + (Layout === 'AWD' ? 70 : 0) + (!Electric && Gearbox === 'Automatic' ? 25 : 0);
    let Sprint = 1.2 + 0.9 * (Weight / Power);
    Sprint += { FWD: 0.35, RWD: 0, AWD: -0.35 }[Layout];
    Sprint += Electric ? -0.4 : Gearbox === 'Automatic' ? -0.2 : 0.1;
    const Top = Electric ? (Layout === 'AWD' ? 261 : 250) : (Engine === 'V8' ? 310 : 272) + (Gearbox === 'Manual' ? 3 : 0);
    const Handling = { RWD: 8.4, AWD: 7.8, FWD: 6.6 }[Layout] - (Electric ? 0.6 : 0) - (Engine === 'V8' ? 0.3 : 0) + (Gearbox === 'Manual' ? 0.2 : 0);
    const Torque = Electric ? (Layout === 'AWD' ? 980 : 620) : Engine === 'V8' ? 750 : 500;
    return { Power, Torque, Weight, Sprint: Math.max(2.4, Sprint), Top, Handling };
}

/** Names shown on the cutaway tags and the spec line. */
export function DriveNames({ Engine, Layout, Gearbox })
{
    const Electric = Engine === 'Electric';
    return {
        Engine: Electric ? (Layout === 'AWD' ? ['Dual motor', 'Front + rear e-motors'] : [`${Layout === 'FWD' ? 'Front' : 'Rear'} motor`, 'Permanent magnet']) : Engine === 'V8' ? ['V8', '4.4 L twin-turbo'] : ['Inline-6', '3.0 L turbo'],
        Gearbox: Electric ? ['Single-speed', 'Reduction gear'] : Gearbox === 'Manual' ? ['6-speed manual', 'Clutch + H-gate'] : ['8-speed automatic', 'Torque converter'],
        Layout: [{ FWD: 'Front-wheel drive', RWD: 'Rear-wheel drive', AWD: 'All-wheel drive' }[Layout], { FWD: 'Transverse engine, front axle', RWD: 'Propshaft to the rear axle', AWD: Electric ? 'One motor on each axle' : 'Transfer case, 40 : 60 split' }[Layout]],
    };
}

/**
 * The drivetrain cutaway, rebuilt by Build(Spec). Parts are translucent fills with bright edges; shafts carry a moving
 * flow from the power source to the driven wheels. Tags lists labelled anchors (car-root local) for the overlay.
 * @returns {{ Group: THREE.Group, Build: (Spec) => void, Set: (Level) => void, Tick: (Seconds) => void, Tags: object[] }}
 */
export function MountDrivetrain()
{
    const Group = new THREE.Group();
    Group.visible = false;
    const Fades = [];                                               // { Material, Base }
    const Flows = [];                                               // shader uniforms
    const Tags = [];
    let Level = 0;

    const Fade = (Material, Base) => { Fades.push({ Material, Base }); Material.opacity = 0; return Material; };
    /** A translucent part with bright edges. */
    const Part = (Geometry, Colour, Position, Rotation = [0, 0, 0], Strength = 1) =>
    {
        const Holder = new THREE.Group();
        Holder.position.set(...Position);
        Holder.rotation.set(...Rotation);
        Holder.add(new THREE.Mesh(Geometry, Fade(Lit(Colour, { transparent: true, depthWrite: false }), 0.22 * Strength)));
        Holder.add(new THREE.LineSegments(new THREE.EdgesGeometry(Geometry, 25), Fade(new THREE.LineBasicMaterial({ color: Colour, transparent: true, depthWrite: false, toneMapped: false }), 0.9 * Strength)));
        Holder.renderOrder = 7;
        return Holder;
    };
    /** A shaft or line with a moving flow along it. */
    const Shaft = (Points, Colour, Radius = 0.035, Speed = 1.2) =>
    {
        const Path = new THREE.CatmullRomCurve3(Points.map((Each) => new THREE.Vector3(...Each)), false, 'centripetal');
        const Uniforms = { Tone: { value: new THREE.Color(Colour) }, Time: { value: 0 }, Show: { value: 0 }, Count: { value: Math.max(2, Path.getLength() * 3) }, Speed: { value: Speed } };
        Flows.push(Uniforms);
        const Tube = new THREE.Mesh(new THREE.TubeGeometry(Path, 48, Radius, 8, false), new THREE.ShaderMaterial({ uniforms: Uniforms, vertexShader: FlowVertex, fragmentShader: FlowFragment, transparent: true, depthWrite: false }));
        Tube.renderOrder = 8;
        return Tube;
    };
    const Tag = (Title, Note, At, Tone) => Tags.push({ Title, Note, At: new THREE.Vector3(...At), Tone });

    function Clear()
    {
        Group.traverse((Each) => { Each.geometry?.dispose(); Each.material?.dispose(); });
        Group.clear();
        Fades.length = Flows.length = Tags.length = 0;
    }

    function Build(Spec)
    {
        Clear();
        const Electric = Spec.Engine === 'Electric';
        const Front = Spec.Layout !== 'RWD', Rear = Spec.Layout !== 'FWD';
        const Transverse = Spec.Layout === 'FWD' && !Electric;
        const Names = DriveNames(Spec);

        // Wheels: bright rings on the driven ones.
        for (const X of [FrontAxle, RearAxle]) for (const Side of [-1, 1])
        {
            const Driven = X > 0 ? Front : Rear;
            Group.add(Part(new THREE.TorusGeometry(0.5, 0.018, 8, 48), Driven ? Tones.Flow : Tones.Idle, [X, Axle, Side * Track], [0, 0, 0], Driven ? 1.4 : 0.5));
        }

        if (!Electric)
        {
            // Engine block.
            const Engine = new THREE.Group();
            Engine.position.set(1.95, 0.78, Transverse ? 0.1 : 0);
            if (Transverse) Engine.rotation.y = Math.PI / 2;
            if (Spec.Engine === 'V8')
            {
                Engine.add(Part(new THREE.BoxGeometry(0.8, 0.2, 0.46), Tones.Engine, [0, -0.18, 0]));
                for (const Side of [-1, 1])
                {
                    Engine.add(Part(new THREE.BoxGeometry(0.8, 0.28, 0.26), Tones.Engine, [0, 0.06, Side * 0.18], [Side * 0.5, 0, 0]));
                    for (let Cylinder = 0; Cylinder < 4; Cylinder++) Engine.add(Part(new THREE.CylinderGeometry(0.045, 0.045, 0.05, 12), Tones.Clutch, [-0.27 + Cylinder * 0.18, 0.21, Side * 0.27], [Side * 0.5, 0, 0], 1.2));
                }
                Engine.add(Part(new THREE.BoxGeometry(0.66, 0.12, 0.18), Tones.Engine, [0, 0.24, 0]));
            }
            else
            {
                Engine.add(Part(new THREE.BoxGeometry(1.1, 0.42, 0.42), Tones.Engine, [0, 0, 0]));
                Engine.add(Part(new THREE.BoxGeometry(1.04, 0.12, 0.36), Tones.Engine, [0, 0.27, 0]));
                for (let Cylinder = 0; Cylinder < 6; Cylinder++) Engine.add(Part(new THREE.CylinderGeometry(0.045, 0.045, 0.05, 12), Tones.Clutch, [-0.42 + Cylinder * 0.168, 0.355, 0], [0, 0, 0], 1.2));
                Engine.add(Part(new THREE.BoxGeometry(0.9, 0.12, 0.14), Tones.Engine, [0, 0.1, 0.3]));
            }
            Engine.add(Part(new THREE.CylinderGeometry(0.13, 0.13, 0.05, 20).rotateZ(Math.PI / 2), Tones.Engine, [0.6, -0.05, 0]));
            Group.add(Engine);
            Tag(Names.Engine[0], Names.Engine[1], [1.95, 1.12, Transverse ? 0.1 : 0], Tones.Engine);

            // Gearbox, clutch or torque converter, and the shifter in the cabin.
            const Manual = Spec.Gearbox === 'Manual';
            if (Transverse)
            {
                Group.add(Part(new THREE.BoxGeometry(0.45, 0.36, 0.4), Tones.Gearbox, [1.85, 0.62, -0.74]));
                Group.add(Manual ? Part(new THREE.CylinderGeometry(0.2, 0.2, 0.03, 28).rotateX(Math.PI / 2), Tones.Clutch, [1.9, 0.72, -0.5], [0, 0, 0], 1.4)
                                 : Part(new THREE.TorusGeometry(0.15, 0.055, 10, 28), Tones.Clutch, [1.9, 0.72, -0.5], [0, 0, 0], 1.4));
                Group.add(Shaft([[1.75, 0.62, -0.6], [1.2, 0.75, -0.4], [0.72, 0.95, -0.15]], Tones.Gearbox, 0.012, 0.6));
                Tag(Names.Gearbox[0], Names.Gearbox[1], [1.85, 0.86, -0.74], Tones.Gearbox);
            }
            else
            {
                Group.add(Part(new THREE.CylinderGeometry(0.17, 0.24, 0.28, 24).rotateZ(Math.PI / 2), Tones.Gearbox, [1.26, 0.7, 0]));
                Group.add(Manual ? Part(new THREE.CylinderGeometry(0.2, 0.2, 0.03, 28).rotateZ(Math.PI / 2), Tones.Clutch, [1.36, 0.7, 0], [0, 0, 0], 1.4)
                                 : Part(new THREE.TorusGeometry(0.15, 0.055, 10, 28).rotateY(Math.PI / 2), Tones.Clutch, [1.32, 0.7, 0], [0, 0, 0], 1.4));
                Group.add(Manual ? Part(new THREE.BoxGeometry(0.55, 0.24, 0.26), Tones.Gearbox, [0.84, 0.66, 0])
                                 : Part(new THREE.CylinderGeometry(0.15, 0.17, 0.72, 20).rotateZ(Math.PI / 2), Tones.Gearbox, [0.8, 0.66, 0]));
                Tag(Names.Gearbox[0], Names.Gearbox[1], [0.86, 0.86, 0], Tones.Gearbox);
            }
            if (Manual)
            {
                Group.add(Part(new THREE.CylinderGeometry(0.012, 0.012, 0.36), Tones.Gearbox, [0.7, 1.0, 0], [0, 0, 0.12], 1.3));
                Group.add(Part(new THREE.SphereGeometry(0.045, 14, 10), Tones.Clutch, [0.68, 1.19, 0], [0, 0, 0], 1.4));
                Group.add(Part(new THREE.BoxGeometry(0.16, 0.01, 0.12), Tones.Gearbox, [0.72, 0.98, 0]));
            }
            else
            {
                Group.add(Part(new THREE.BoxGeometry(0.22, 0.06, 0.1), Tones.Gearbox, [0.7, 1.0, 0]));
                Group.add(Part(new THREE.CylinderGeometry(0.018, 0.018, 0.1), Tones.Clutch, [0.7, 1.08, 0], [0, 0, 0], 1.4));
            }

            // Fuel tank and exhaust.
            Group.add(Part(new THREE.BoxGeometry(0.55, 0.28, 0.9), Tones.Tank, [-0.95, 0.76, 0]));
            Tag('Fuel tank', '64 L', [-0.95, 0.95, 0], Tones.Tank);
            Group.add(Shaft([[1.55, 0.58, 0.32], [0.9, 0.42, 0.44], [-1.0, 0.4, 0.5], [-2.3, 0.42, 0.45]], Tones.Exhaust, 0.035, 0.5));
            Group.add(Part(new THREE.BoxGeometry(0.6, 0.22, 0.46), Tones.Exhaust, [-2.6, 0.45, 0.42]));
            Group.add(Shaft([[-2.9, 0.44, 0.5], [-3.18, 0.43, 0.55]], Tones.Exhaust, 0.045, 0.5));
        }
        else
        {
            // Battery under the floor, motors on the driven axles, HV cables.
            Group.add(Part(new THREE.BoxGeometry(2.5, 0.13, 1.5), Tones.Battery, [-0.15, 0.48, 0]));
            for (let Module = 0; Module < 12; Module++) Group.add(Part(new THREE.BoxGeometry(0.36, 0.09, 0.66), Tones.Battery, [-1.2 + (Module % 6) * 0.42, 0.48, Module < 6 ? -0.36 : 0.36], [0, 0, 0], 0.6));
            Tag('Battery', '92 kWh', [-0.15, 0.62, 0.55], Tones.Battery);
            for (const [X, Driven] of [[FrontAxle, Front], [RearAxle, Rear]])
            {
                if (!Driven) continue;
                Group.add(Part(new THREE.CylinderGeometry(0.2, 0.2, 0.5, 28).rotateX(Math.PI / 2), Tones.Motor, [X, Axle, 0.05]));
                Group.add(Part(new THREE.BoxGeometry(0.32, 0.1, 0.28), Tones.Motor, [X, Axle + 0.27, 0]));
                Group.add(Part(new THREE.BoxGeometry(0.22, 0.28, 0.24), Tones.Gearbox, [X, Axle, -0.33]));
                const Edge = X > 0 ? 1.1 : -1.4;
                Group.add(Shaft([[Edge, 0.5, 0.35], [(Edge + X) / 2, 0.62, 0.3], [X, Axle + 0.27, 0.1]], Tones.Cable, 0.022, 1.8));
            }
            const MotorAt = Front ? FrontAxle : RearAxle;
            Tag(Names.Engine[0], Names.Engine[1], [MotorAt, Axle + 0.42, 0], Tones.Motor);
            Tag(Names.Gearbox[0], Names.Gearbox[1], [MotorAt, Axle + 0.1, -0.5], Tones.Gearbox);
        }

        // Driveline: propshafts, transfer case, differentials and half shafts.
        const Diff = (X, Y, Z = 0) => Group.add(Part(new THREE.SphereGeometry(0.16, 18, 12), Tones.Diff, [X, Y, Z]));
        const Halves = (X, Z0 = 0.15) => { for (const Side of [-1, 1]) Group.add(Shaft([[X, Axle, Side * Z0], [X, Axle, Side * (Track * 0.55)], [X, Axle, Side * (Track - 0.1)]], Tones.Flow, 0.035)); };
        if (!Electric && Rear)
        {
            Group.add(Shaft([[0.5, 0.62, 0], [-0.6, 0.5, 0], [RearAxle + 0.2, Axle, 0]], Tones.Flow, 0.04));
            Diff(RearAxle, Axle);
            Halves(RearAxle);
        }
        if (!Electric && Spec.Layout === 'AWD')
        {
            Group.add(Part(new THREE.BoxGeometry(0.26, 0.22, 0.3), Tones.Gearbox, [0.42, 0.58, 0.14]));
            Group.add(Shaft([[0.5, 0.52, 0.24], [1.2, 0.42, 0.24], [FrontAxle - 0.16, 0.46, 0.14]], Tones.Flow, 0.035));
            Diff(FrontAxle, 0.47, 0.12);
            Halves(FrontAxle);
        }
        if (Transverse)
        {
            for (const Side of [-1, 1]) Group.add(Shaft([[1.82, Axle + 0.04, -0.55], [1.78, Axle, Side < 0 ? -0.8 : 0.1], [FrontAxle, Axle, Side * (Track - 0.1)]], Tones.Flow, 0.035));
        }
        if (Electric)
        {
            if (Front) Halves(FrontAxle, 0.3);
            if (Rear) Halves(RearAxle, 0.3);
        }
        Tag(Names.Layout[0], Names.Layout[1], [Rear ? RearAxle : FrontAxle, Axle + 0.3, Spec.Layout === 'AWD' ? 0 : -0.6], Tones.Flow);
        Set(Level);
    }

    function Set(Next)
    {
        Level = Next;
        Group.visible = Level > 0.01;
        for (const { Material, Base } of Fades) Material.opacity = Base * Level;
        for (const Uniforms of Flows) Uniforms.Show.value = Level;
    }

    function Tick(Seconds)
    {
        if (!Group.visible) return;
        for (const Uniforms of Flows) Uniforms.Time.value += Seconds;
    }

    return { Group, Build, Set, Tick, Tags };
}

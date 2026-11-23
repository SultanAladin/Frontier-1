// LAMP RIG
// 🧩 Exterior lights for the ControlVehicle: daytime and parking lamps, low and high beam, front and rear fog,
//    indicators and hazards, brake and reverse lamps, plus per-car signature lamps (roof light bar, rock lights,
//    wing strip, welcome carpet). Lamp positions come from the Headlight and Taillight families of the body itself.
// 📝 The floors in the cabin HMIs are custom shaders that ignore scene lights, so every lamp draws its own light:
//    an emitter glow on the lens, a soft volumetric cone, and an additive pool on the ground.

import { THREE, Family } from './VehicleAssembly.js';

export const BeamModes = ['Off', 'Auto', 'Parking', 'Low', 'High'];

function GlowTexture(Inner = 'rgba(255,255,255,1)', Outer = 'rgba(255,255,255,0)', Middle = null)
{
    const Paint = document.createElement('canvas');
    Paint.width = Paint.height = 128;
    const Pen  = Paint.getContext('2d');
    const Fall = Pen.createRadialGradient(64, 64, 0, 64, 64, 64);
    Fall.addColorStop(0, Inner);
    if (Middle) Fall.addColorStop(0.35, Middle);
    Fall.addColorStop(1, Outer);
    Pen.fillStyle = Fall;
    Pen.fillRect(0, 0, 128, 128);
    const Texture = new THREE.CanvasTexture(Paint);
    Texture.colorSpace = THREE.SRGBColorSpace;
    return Texture;
}

// Beam pool: bright near the lamp, long soft throw, slightly wider far out.
function PoolTexture()
{
    const Paint = document.createElement('canvas');
    Paint.width = 256; Paint.height = 128;
    const Pen = Paint.getContext('2d');
    const Image = Pen.createImageData(256, 128);
    for (let Y = 0; Y < 128; Y++) for (let X = 0; X < 256; X++)
    {
        const Along = X / 255, Across = (Y - 63.5) / 63.5;
        const Width = 0.35 + 0.65 * Along;
        const Side  = Math.max(0, 1 - Math.pow(Math.abs(Across) / Width, 2));
        const Throw = Math.pow(Math.sin(Math.min(1, Along * 1.15) * Math.PI), 0.7) * (1 - Along * 0.35);
        const Level = Side * Throw;
        const Index = (Y * 256 + X) * 4;
        Image.data[Index] = Image.data[Index + 1] = Image.data[Index + 2] = 255;
        Image.data[Index + 3] = Math.round(Math.min(1, Level) * 255);
    }
    Pen.putImageData(Image, 0, 0);
    const Texture = new THREE.CanvasTexture(Paint);
    Texture.colorSpace = THREE.SRGBColorSpace;
    return Texture;
}

const SoftGlow = GlowTexture();
const HotGlow  = GlowTexture('rgba(255,255,255,1)', 'rgba(255,255,255,0)', 'rgba(255,255,255,0.35)');
const BeamPool = PoolTexture();

const ConeVertex = /* glsl */`
    uniform float Length; varying float Along; varying vec3 Facing, Toward;
    void main()
    {
        Along = clamp(position.x / Length, 0.0, 1.0);
        vec4 Point = modelViewMatrix * vec4(position, 1.0);
        Facing = normalize(normalMatrix * normal);
        Toward = normalize(-Point.xyz);
        gl_Position = projectionMatrix * Point;
    }`;
const ConeFragment = /* glsl */`
    uniform vec3 Tone; uniform float Strength; varying float Along; varying vec3 Facing, Toward;
    void main()
    {
        float Soft = pow(abs(dot(normalize(Facing), normalize(Toward))), 2.2);
        float Fall = pow(1.0 - Along, 1.5) * smoothstep(0.0, 0.06, Along);
        gl_FragColor = vec4(Tone * Soft * Fall * Strength * 0.075, 1.0);
        #include <colorspace_fragment>
    }`;

// Open cone from the origin along +X: tip at the lamp, mouth at Length.
function BeamCone(Length, Radius, Tone)
{
    const Geometry = new THREE.ConeGeometry(Radius, Length, 40, 1, true).translate(0, -Length / 2, 0).rotateZ(Math.PI / 2);
    const Uniforms = { Length: { value: Length }, Tone: { value: new THREE.Color(Tone) }, Strength: { value: 0 } };
    const Mesh = new THREE.Mesh(Geometry, new THREE.ShaderMaterial({ uniforms: Uniforms, vertexShader: ConeVertex, fragmentShader: ConeFragment, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide, toneMapped: false }));
    Mesh.renderOrder = 5;
    Mesh.userData.Uniforms = Uniforms;
    return Mesh;
}

function Emitter(Tone, Size, Texture = HotGlow)
{
    const Sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: Texture, color: Tone, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
    Sprite.scale.setScalar(Size);
    Sprite.renderOrder = 6;
    return Sprite;
}

function Pool(Tone, Length, Width, Texture = SoftGlow)
{
    const Mesh = new THREE.Mesh(new THREE.PlaneGeometry(Length, Width).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ map: Texture, color: Tone, transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
    Mesh.renderOrder = 2;
    return Mesh;
}

// Bounds of every triangle in the body that uses a given family material, split into left (+Z) and right (−Z).
function FamilyBounds(Body, Material)
{
    const Position = Body.geometry.attributes.position;
    const Sides = { Left: new THREE.Box3(), Right: new THREE.Box3(), All: new THREE.Box3() };
    const Point = new THREE.Vector3();
    for (const Group of Body.geometry.groups)
    {
        if (Body.material[Group.materialIndex] !== Material) continue;
        for (let Index = Group.start; Index < Group.start + Group.count; Index++)
        {
            Point.fromBufferAttribute(Position, Index);
            Sides.All.expandByPoint(Point);
            (Point.z >= 0 ? Sides.Left : Sides.Right).expandByPoint(Point);
        }
    }
    return Sides;
}

/**
 * @param {object} Vehicle - result of AssembleVehicle
 * @param {{ Extras?: { RoofBar?: boolean, Rock?: boolean, Wing?: boolean, Welcome?: boolean }, Signature?: string,
 *           PoolLift?: number }} Options
 */
// Effective beams after Auto: Auto acts as low beam at night; auto high beam lifts it when the road is clear.
function BeamsFor(State)
{
    const Beam = State.Beam;
    const On   = Beam === 'Low' || Beam === 'High' || Beam === 'Auto';
    return { Parking: Beam !== 'Off', Low: On, High: Beam === 'High' || (On && State.AutoHigh && State.AutoHighLive), Drl: true };
}

/**
 * Lamp echo: the lamp state without any 3D, for a dashboard cluster that mirrors the centre display's switches.
 * Same State / Blink / Beams() surface as a mounted rig, so SyncTelltales works on it unchanged.
 */
export function LampEcho()
{
    const State = { Beam: 'Low', AutoHigh: false, AutoHighLive: false, FogFront: false, FogRear: false, Signal: 'Off', Brake: 0, Reverse: false };
    let Last = performance.now(), Clock = 0;
    const Echo = {
        State,
        Speed: 0,
        Blink: false,
        Set(Patch) { Object.assign(State, Patch); },
        Beams() { return BeamsFor(State); },
        Step()
        {
            const Now = performance.now();
            Clock += Math.min(0.25, (Now - Last) / 1000);
            Last = Now;
            Echo.Blink = (Clock % 0.75) < 0.4;
            State.AutoHighLive = Echo.Speed > 45 && (Clock % 12) > 3;
        },
    };
    return Echo;
}

export function MountLamps(Vehicle, { Extras = {}, Signature = '#f4f7ff', PoolLift = 0.03 } = {})
{
    Vehicle.Root.updateMatrixWorld(true);
    const Finish = Vehicle.Finish;
    const Body   = Vehicle.Body;
    const Lift   = Vehicle.BodyMount.position.y;                       // body local → ground height offset
    const Head   = FamilyBounds(Body, Finish[Family.Headlight]);
    const Tail   = FamilyBounds(Body, Finish[Family.Taillight]);
    const BodyBox = new THREE.Box3().setFromBufferAttribute(Body.geometry.attributes.position);
    const Front  = Head.All.isEmpty() ? BodyBox.max.x : Head.All.max.x;
    const Rear   = Tail.All.isEmpty() ? BodyBox.min.x : Tail.All.min.x;
    const HeadY  = Head.All.isEmpty() ? 0.35 : (Head.All.min.y + Head.All.max.y) / 2;
    const TailY  = Tail.All.isEmpty() ? 0.5 : (Tail.All.min.y + Tail.All.max.y) / 2;
    const HeadZ  = Head.All.isEmpty() ? 0.75 : Math.max(Head.All.max.z, -Head.All.min.z) - 0.22;
    const TailZ  = Tail.All.isEmpty() ? 0.75 : Math.max(Tail.All.max.z, -Tail.All.min.z) - 0.2;
    const SideZ  = Math.max(BodyBox.max.z, -BodyBox.min.z);

    const OnBody   = new THREE.Group();                                 // follows ride height, pitch, roll
    const OnGround = new THREE.Group();                                 // light on the floor
    Vehicle.BodyMount.add(OnBody);
    Vehicle.Root.add(OnGround);
    OnGround.position.y = PoolLift;

    const Parts = [];                                                   // { Key, Level, Apply(Level) }
    const Track = (Key, Apply, Speed = 10) => { const Part = { Key, Level: 0, Goal: 0, Speed, Apply }; Parts.push(Part); return Part; };

    // ① Headlamps: emitters, cones, pools.
    const HeadLamps = [1, -1].map((Side) =>
    {
        const Lens  = Emitter(Signature, 0.55);
        Lens.position.set(Front + 0.04, HeadY, Side * HeadZ);
        const Low   = BeamCone(9.5, 2.3, '#fff4dc');
        Low.position.copy(Lens.position);
        Low.rotation.z = -0.16;
        Low.rotation.y = Side * -0.06;
        const High  = BeamCone(24, 3.6, '#e9f0ff');
        High.position.copy(Lens.position);
        High.rotation.z = -0.03;
        OnBody.add(Lens, Low, High);
        return { Lens, Low, High };
    });
    const LowPool  = Pool('#fff1d6', 11, 6.5, BeamPool);
    LowPool.position.set(Front + 5.4, 0, 0);
    const HighPool = Pool('#e6eeff', 26, 10, BeamPool);
    HighPool.position.set(Front + 12.5, 0, 0);
    const Streak = Emitter('#dfe9ff', 1, SoftGlow);
    Streak.scale.set(5.5, 0.24, 1);
    Streak.position.set(Front + 0.1, HeadY, 0);
    OnBody.add(Streak);
    OnGround.add(LowPool, HighPool);

    const Drl     = Track('Drl', (Level) => { Finish[Family.Headlight].emissiveIntensity = Rig.HeadBase + Level * 0.25 + Low.Level * 0.35 + High.Level * 0.3; });
    const Low     = Track('Low', (Level) =>
    {
        for (const Lamp of HeadLamps) { Lamp.Low.userData.Uniforms.Strength.value = Level; Lamp.Lens.material.opacity = Math.max(Drl.Level * 0.45, Level * 0.9, High.Level); }
        LowPool.material.opacity = Level * 0.4;
    }, 7);
    const High    = Track('High', (Level) =>
    {
        for (const Lamp of HeadLamps) { Lamp.High.userData.Uniforms.Strength.value = Level * 0.45; Lamp.Lens.scale.setScalar(0.55 + Level * 0.35); }
        HighPool.material.opacity = Level * 0.16;
        Streak.material.opacity = Level * 0.55;
    }, 7);

    // ② Front fog: low and wide.
    const FogLamps = [1, -1].map((Side) =>
    {
        const Lens = Emitter('#fff2c8', 0.36);
        Lens.position.set(Front - 0.02, Math.max(0.12, 0.36 - Lift), Side * (SideZ - 0.32));
        OnBody.add(Lens);
        return Lens;
    });
    const FogPool = Pool('#ffe9b8', 6, 9.5);
    FogPool.position.set(Front + 2.6, 0, 0);
    OnGround.add(FogPool);
    const FogFront = Track('FogFront', (Level) => { for (const Lens of FogLamps) Lens.material.opacity = Level; FogPool.material.opacity = Level * 0.5; }, 8);

    // ③ Tail, brake, rear fog, reverse.
    const TailLamps = [1, -1].map((Side) =>
    {
        const Lens = Emitter('#ff2a3c', 0.5, SoftGlow);
        Lens.position.set(Rear - 0.04, TailY, Side * TailZ);
        OnBody.add(Lens);
        return Lens;
    });
    const BrakePool = Pool('#ff2038', 3.4, 4.2);
    BrakePool.position.set(Rear - 1.5, 0, 0);
    OnGround.add(BrakePool);
    const Tails  = Track('Tail', (Level) => { Finish[Family.Taillight].emissiveIntensity = Rig.TailBase + Level * 1.3 + Brake.Level * 2.6 + RearFog.Level * 0.6; for (const Lens of TailLamps) Lens.material.opacity = Level * 0.45 + Brake.Level * 0.9; });
    const Brake  = Track('Brake', (Level) => { BrakePool.material.opacity = Level * 0.45; }, 14);
    const RearFogLens = Emitter('#ff1028', 0.42);
    RearFogLens.position.set(Rear - 0.05, Math.max(0.16, 0.42 - Lift), 0.62);
    const RearFogPool = Pool('#ff1530', 4.6, 3.2);
    RearFogPool.position.set(Rear - 2.1, 0, 0.4);
    OnBody.add(RearFogLens);
    OnGround.add(RearFogPool);
    const RearFog = Track('RearFog', (Level) => { RearFogLens.material.opacity = Level; RearFogPool.material.opacity = Level * 0.55; }, 8);
    const ReverseLens = Emitter('#f4f8ff', 0.38);
    ReverseLens.position.set(Rear - 0.05, Math.max(0.16, 0.42 - Lift), -0.62);
    const ReversePool = Pool('#eef3ff', 4.4, 3.2);
    ReversePool.position.set(Rear - 2.0, 0, -0.4);
    OnBody.add(ReverseLens);
    OnGround.add(ReversePool);
    const Reverse = Track('Reverse', (Level) => { ReverseLens.material.opacity = Level; ReversePool.material.opacity = Level * 0.5; }, 12);

    // ④ Indicators: four corners plus side repeaters, amber pools at the corners.
    const Corner = (X, Y, Z) => { const Lens = Emitter('#ffa21a', 0.46); Lens.position.set(X, Y, Z); OnBody.add(Lens); return Lens; };
    const SignalSide = [1, -1].map((Side) =>
    {
        const Lenses = [Corner(Front - 0.02, HeadY, Side * (HeadZ + 0.17)), Corner(Rear + 0.02, TailY, Side * (TailZ + 0.15)), Corner(1.55, Math.max(0.3, 0.72 - Lift), Side * (SideZ + 0.02))];
        Lenses[2].scale.setScalar(0.3);
        const Glow = Pool('#ff9a10', 2.2, 2.2);
        Glow.position.set(Front - 0.3, 0, Side * (SideZ + 0.3));
        const GlowRear = Pool('#ff9a10', 2.2, 2.2);
        GlowRear.position.set(Rear + 0.3, 0, Side * (SideZ + 0.3));
        OnGround.add(Glow, GlowRear);
        return { Lenses, Pools: [Glow, GlowRear] };
    });
    const SignalLeft  = Track('SignalLeft', (Level) => { for (const Lens of SignalSide[0].Lenses) Lens.material.opacity = Level; for (const Each of SignalSide[0].Pools) Each.material.opacity = Level * 0.4; }, 22);
    const SignalRight = Track('SignalRight', (Level) => { for (const Lens of SignalSide[1].Lenses) Lens.material.opacity = Level; for (const Each of SignalSide[1].Pools) Each.material.opacity = Level * 0.4; }, 22);

    // ⑤ Signature lamps.
    let RoofBar = null, Rock = null, Wing = null, Welcome = null;
    if (Extras.RoofBar)
    {
        // Roof line: the highest body point; the bar sits on its leading edge.
        const Position = Body.geometry.attributes.position, Point = new THREE.Vector3();
        let Top = -Infinity;
        for (let Index = 0; Index < Position.count; Index++) { Point.fromBufferAttribute(Position, Index); if (Math.abs(Point.z) < 0.5) Top = Math.max(Top, Point.y); }
        let Lead = -Infinity;
        for (let Index = 0; Index < Position.count; Index++) { Point.fromBufferAttribute(Position, Index); if (Math.abs(Point.z) < 0.5 && Point.y > Top - 0.04) Lead = Math.max(Lead, Point.x); }
        const Bar = new THREE.Group();
        Bar.position.set(Lead - 0.18, Top + 0.07, 0);
        const Housing = new THREE.Mesh(new THREE.BoxGeometry(0.16, 0.1, 1.5), new THREE.MeshStandardMaterial({ color: '#15151b', roughness: 0.4, metalness: 0.7 }));
        const Face = new THREE.Mesh(new THREE.PlaneGeometry(1.42, 0.06).rotateY(Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#ffffff', transparent: true, opacity: 0.15, toneMapped: false }));
        Face.position.x = 0.085;
        const Beads = [];
        for (let Bead = 0; Bead < 8; Bead++) { const Each = Emitter('#f2efff', 0.32); Each.position.set(0.12, 0, -0.62 + Bead * (1.24 / 7)); Beads.push(Each); }
        const Cone = BeamCone(26, 6.5, '#f0ecff');
        Cone.position.set(0.1, 0, 0);
        Cone.rotation.z = -0.07;
        Bar.add(Housing, Face, Cone, ...Beads);
        OnBody.add(Bar);
        const BarPool = Pool('#efeaff', 30, 17, BeamPool);
        BarPool.position.set(Front + 13, 0, 0);
        OnGround.add(BarPool);
        RoofBar = Track('RoofBar', (Level) => { for (const Each of Beads) Each.material.opacity = Level; Face.material.opacity = 0.15 + Level * 0.85; Cone.userData.Uniforms.Strength.value = Level * 0.6; BarPool.material.opacity = Level * 0.24; }, 6);
    }
    if (Extras.Rock)
    {
        const Pools = Vehicle.Wheels.map((Wheel) => { const Each = Pool('#d9c8ff', 2.0, 2.0); Each.position.set(Wheel.Mount.x, 0, Wheel.Mount.z * 0.82); OnGround.add(Each); return Each; });
        Rock = Track('Rock', (Level) => { for (const Each of Pools) Each.material.opacity = Level * 0.75; }, 6);
    }
    if (Extras.Wing && Vehicle.Wing)
    {
        const Box = new THREE.Box3().setFromObject(Vehicle.Wing);
        const Local = Vehicle.BodyMount.worldToLocal(Box.min.clone()), LocalTop = Vehicle.BodyMount.worldToLocal(Box.max.clone());
        const Span = Math.abs(LocalTop.z - Local.z) * 0.92;
        const Strip = new THREE.Mesh(new THREE.PlaneGeometry(Span, 0.035).rotateY(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#ff2a44', transparent: true, opacity: 0, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false }));
        Strip.position.set(Math.min(Local.x, LocalTop.x) - 0.012, Math.max(Local.y, LocalTop.y) - 0.02, 0);
        const Halo = Emitter('#ff2a44', 1, SoftGlow);
        Halo.scale.set(Span * 1.3, 0.5, 1);
        Halo.position.copy(Strip.position).add(new THREE.Vector3(-0.05, 0, 0));
        OnBody.add(Strip, Halo);
        Wing = Track('Wing', (Level) => { Strip.material.opacity = Level; Halo.material.opacity = Level * 0.45; }, 5);
        Wing.Strip = Strip;
    }
    if (Extras.Welcome)
    {
        const Carpet = (() =>
        {
            const Paint = document.createElement('canvas');
            Paint.width = Paint.height = 256;
            const Pen = Paint.getContext('2d');
            Pen.strokeStyle = 'rgba(255,255,255,0.9)';
            for (let Ring = 0; Ring < 5; Ring++) { Pen.lineWidth = 3 - Ring * 0.4; Pen.globalAlpha = 1 - Ring * 0.17; Pen.beginPath(); Pen.arc(128, 128, 30 + Ring * 22, 0, Math.PI * 2); Pen.stroke(); }
            Pen.globalAlpha = 1;
            for (let Ray = 0; Ray < 24; Ray++) { const A = (Ray / 24) * Math.PI * 2; Pen.beginPath(); Pen.moveTo(128 + Math.cos(A) * 20, 128 + Math.sin(A) * 20); Pen.lineTo(128 + Math.cos(A) * 30, 128 + Math.sin(A) * 30); Pen.stroke(); }
            const Fall = Pen.createRadialGradient(128, 128, 0, 128, 128, 128);
            Fall.addColorStop(0, 'rgba(255,255,255,0.35)'); Fall.addColorStop(1, 'rgba(255,255,255,0)');
            Pen.fillStyle = Fall; Pen.fillRect(0, 0, 256, 256);
            const Texture = new THREE.CanvasTexture(Paint); Texture.colorSpace = THREE.SRGBColorSpace; return Texture;
        })();
        const Carpets = [1, -1].map((Side) => { const Each = Pool('#ffd2e4', 2.6, 2.6, Carpet); Each.position.set(0.5, 0.004, Side * (SideZ + 1.3)); OnGround.add(Each); return Each; });
        Welcome = Track('Welcome', (Level) => { for (const Each of Carpets) { Each.material.opacity = Level * 0.9; Each.rotation.y += 0.004; Each.scale.setScalar(0.85 + Level * 0.15); } }, 3);
    }

    // State and stepping.
    const State = { Beam: 'Low', AutoHigh: false, AutoHighLive: false, FogFront: false, FogRear: false, Signal: 'Off', Brake: 0, Reverse: false, RoofBar: false, Rock: false, Wing: false, Welcome: false };
    let Last = performance.now(), Clock = 0, Igniting = 0;
    const Rig = {
        State,
        Speed: 0,                                                       // [km/h] - fed by the HMI for auto high beam
        Oncoming: false,
        HeadBase: 0.08,                                                 // HMIs raise these for their own lens moods
        TailBase: 0.12,
        Blink: false,
        Positions: { Front, Rear, HeadY: HeadY + Lift, TailY: TailY + Lift, HeadZ, TailZ, SideZ },
        Set(Patch)
        {
            const Before = State.Beam;
            Object.assign(State, Patch);
            if (Patch.Beam && Patch.Beam !== Before && (Patch.Beam === 'Low' || Patch.Beam === 'High' || Patch.Beam === 'Auto')) Igniting = 0.45;
        },
        // Effective beams after Auto: Auto acts as low beam at night; auto high beam lifts it when the road is clear.
        Beams() { return BeamsFor(State); },
        Step()
        {
            const Now  = performance.now();
            const Wall = Math.min(0.25, (Now - Last) / 1000);
            Last = Now;
            Clock += Wall;
            Igniting = Math.max(0, Igniting - Wall);
            Rig.Blink = (Clock % 0.75) < 0.4;
            // Auto high beam: lifts above 45 km/h and dips for a synthetic oncoming car 3 s in every 12 s.
            State.AutoHighLive = Rig.Speed > 45 && (Clock % 12) > 3;
            Rig.Oncoming = Rig.Speed > 45 && (Clock % 12) <= 3;
            const Beams = Rig.Beams();
            const Flicker = Igniting > 0 ? 0.75 + 0.25 * Math.sin(Igniting * 90) : 1;
            Drl.Goal = Beams.Drl ? 1 : 0;
            Low.Goal = (Beams.Low ? 1 : 0) * Flicker;
            High.Goal = Beams.High ? 1 : 0;
            Tails.Goal = Beams.Parking ? 1 : 0.25;
            Brake.Goal = THREE.MathUtils.clamp(State.Brake, 0, 1);
            FogFront.Goal = State.FogFront ? 1 : 0;
            RearFog.Goal = State.FogRear ? 1 : 0;
            Reverse.Goal = State.Reverse ? 1 : 0;
            const Left  = State.Signal === 'Left' || State.Signal === 'Hazard';
            const Right = State.Signal === 'Right' || State.Signal === 'Hazard';
            SignalLeft.Goal  = Left && Rig.Blink ? 1 : 0;
            SignalRight.Goal = Right && Rig.Blink ? 1 : 0;
            if (RoofBar) RoofBar.Goal = State.RoofBar ? 1 : 0;
            if (Rock) Rock.Goal = State.Rock ? 1 : 0;
            if (Wing) Wing.Goal = State.Wing ? 0.65 + 0.35 * Brake.Level : 0;
            if (Welcome) Welcome.Goal = State.Welcome ? 1 : 0;
            for (const Part of Parts)
            {
                Part.Level += (Part.Goal - Part.Level) * (1 - Math.exp(-Wall * Part.Speed));
                Part.Apply(Part.Level);
            }
        },
        // Mirror floors: hide pools from a MirrorTwin clone (they are already on the floor).
        get Ground() { return OnGround; },
        get Lamps() { return OnBody; },
    };
    return Rig;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LAMP GLYPHS AND CONTROLS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Dashboard tell-tale pictograms (ISO 2575 shapes, redrawn): stroke-only, 24 × 24, coloured by CSS currentColor.
const Lens = 'M10 6c5 0 8 2.6 8 6s-3 6-8 6Z';
export const LampGlyphs = {
    Parking:  `<svg viewBox="0 0 24 24"><path d="M9 7c-3 0-5 2.2-5 5s2 5 5 5Z"/><path d="M15 7c3 0 5 2.2 5 5s-2 5-5 5Z"/><path d="M1.5 9.5h2M1.5 14.5h2M20.5 9.5h2M20.5 14.5h2"/></svg>`,
    Low:      `<svg viewBox="0 0 24 24"><path d="${Lens}" transform="translate(4 0)"/><path d="M2 8.5l7 1.6M2 12l7 1.6M2 15.5l7 1.6"/></svg>`,
    High:     `<svg viewBox="0 0 24 24"><path d="${Lens}" transform="translate(4 0)"/><path d="M2 8.5h8M2 12h8M2 15.5h8"/></svg>`,
    Auto:     `<svg viewBox="0 0 24 24"><path d="${Lens}" transform="translate(4 0)"/><path d="M2 9h6M2 15h6"/><path d="M15.2 15.2l1.3-3.8 1.3 3.8M15.6 14h1.8" stroke-width="1.2"/></svg>`,
    Drl:      `<svg viewBox="0 0 24 24"><path d="${Lens}" transform="translate(4 0)"/><path d="M3 7.5l3 1.5M2 12h4M3 16.5l3-1.5"/></svg>`,
    FogFront: `<svg viewBox="0 0 24 24"><path d="${Lens}" transform="translate(4 0)"/><path d="M1.5 8.5l7 1.2M1.5 12h7M1.5 15.5l7-1.2"/><path d="M5 5.5c.8 1.2.8 2.4 0 3.6s-.8 2.4 0 3.6.8 2.4 0 3.6-.8 2.4 0 3.6"/></svg>`,
    FogRear:  `<svg viewBox="0 0 24 24"><path d="M10 6c-5 0-8 2.6-8 6s3 6 8 6Z"/><path d="M14 9h8M14 12h8M14 15h8"/><path d="M18 5.5c.8 1.2.8 2.4 0 3.6s-.8 2.4 0 3.6.8 2.4 0 3.6-.8 2.4 0 3.6"/></svg>`,
    Hazard:   `<svg viewBox="0 0 24 24"><path d="M12 3 22 20H2Z"/><path d="M12 8.5 17.2 17.5H6.8Z"/></svg>`,
    Left:     `<svg viewBox="0 0 24 24"><path d="M3 12 10 5v4h10v6H10v4Z"/></svg>`,
    Right:    `<svg viewBox="0 0 24 24"><path d="M21 12 14 5v4H4v6h10v4Z"/></svg>`,
    RoofBar:  `<svg viewBox="0 0 24 24"><rect x="3" y="11" width="18" height="4" rx="1.2"/><path d="M5 8 4 5.5M9 8l-.4-3M15 8l.4-3M19 8l1-2.5M12 8V4.5"/><path d="M6 15v3M18 15v3"/></svg>`,
    Rock:     `<svg viewBox="0 0 24 24"><path d="M4 13v-2.5L6.5 6h11L20 10.5V13Z"/><circle cx="7.5" cy="14" r="2"/><circle cx="16.5" cy="14" r="2"/><path d="M3 20.5h3M10.5 20.5h3M18 20.5h3M7.5 17.5v1.5M16.5 17.5v1.5"/></svg>`,
    Underbody:`<svg viewBox="0 0 24 24"><path d="M4 12v-2.5L6.5 5h11L20 9.5V12Z"/><circle cx="7.5" cy="13" r="2"/><circle cx="16.5" cy="13" r="2"/><path d="M3 19h18M6 21.5h12"/></svg>`,
    Wing:     `<svg viewBox="0 0 24 24"><path d="M3 8h18l-2 3H5Z"/><path d="M7 11v6M17 11v6"/><path d="M4 20h4M16 20h4"/></svg>`,
    Welcome:  `<svg viewBox="0 0 24 24"><circle cx="12" cy="16" r="2"/><ellipse cx="12" cy="16" rx="6" ry="3"/><ellipse cx="12" cy="16" rx="10" ry="5" stroke-dasharray="2 2.4"/><path d="M12 3v7M9 7l3 3 3-3"/></svg>`,
    Brake:    `<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="6.5"/><path d="M12 8.5v4.5M12 15.4v.2"/><path d="M4.5 6a10 10 0 0 0 0 12M19.5 6a10 10 0 0 1 0 12"/></svg>`,
    FuelLow:  `<svg viewBox="0 0 24 24"><path d="M3 21V5a2 2 0 0 1 2-2h7a2 2 0 0 1 2 2v16M2 21h13M5.5 7h6M14 9h2.5a1.5 1.5 0 0 1 1.5 1.5V17a1.5 1.5 0 0 0 3 0V8l-3-3"/></svg>`,
    Park:     `<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="6.5"/><path d="M10 15.5v-7h2.4a2 2 0 0 1 0 4H10"/><path d="M4.5 6a10 10 0 0 0 0 12M19.5 6a10 10 0 0 1 0 12"/></svg>`,
    Reverse:  `<svg viewBox="0 0 24 24"><path d="M10 6c-5 0-8 2.6-8 6s3 6 8 6Z"/><path d="M15 12h7M18.5 8.5v7"/></svg>`,
};

/**
 * Wires every control inside Root to the rig. Markup contract:
 *   [data-beam="Off|Auto|Parking|Low|High"]  beam selector (exclusive)
 *   [data-lamp="FogFront|FogRear|AutoHigh|RoofBar|Rock|Wing|Welcome|…"] on/off toggles (custom keys go to OnCustom)
 *   [data-signal="Left|Right|Hazard"]        indicator toggles (exclusive, tap again to cancel)
 *   [data-glyph="Low"]                       filled with the matching pictogram
 * @returns {{ Refresh: () => void }}
 */
export function BindLampControls(Root, Rig, { OnChange = () => {}, Custom = {} } = {})
{
    Root.querySelectorAll('[data-glyph]').forEach((Slot) => { Slot.innerHTML = LampGlyphs[Slot.dataset.glyph] || ''; });
    const Refresh = () =>
    {
        Root.querySelectorAll('[data-beam]').forEach((Button) => Button.classList.toggle('On', Button.dataset.beam === Rig.State.Beam));
        Root.querySelectorAll('[data-lamp]').forEach((Button) =>
        {
            const Key = Button.dataset.lamp;
            Button.classList.toggle('On', Key in Custom ? Custom[Key].Get() : !!Rig.State[Key]);
        });
        Root.querySelectorAll('[data-signal]').forEach((Button) => Button.classList.toggle('On', Rig.State.Signal === Button.dataset.signal));
    };
    Root.addEventListener('click', (Press) =>
    {
        const Button = Press.target.closest('[data-beam],[data-lamp],[data-signal]');
        if (!Button || !Root.contains(Button)) return;
        if (Button.dataset.beam) Rig.Set({ Beam: Button.dataset.beam });
        else if (Button.dataset.signal) Rig.Set({ Signal: Rig.State.Signal === Button.dataset.signal ? 'Off' : Button.dataset.signal });
        else
        {
            const Key = Button.dataset.lamp;
            if (Key in Custom) Custom[Key].Set(!Custom[Key].Get());
            else Rig.Set({ [Key]: !Rig.State[Key] });
        }
        Refresh();
        OnChange(Button);
    });
    Refresh();
    return { Refresh };
}

// Blink the indicator tell-tales in sync with the rig (class .Blink on every [data-telltale] that is live).
export function SyncTelltales(Root, Rig)
{
    const Beams = Rig.Beams();
    const Live = {
        Left:     (Rig.State.Signal === 'Left' || Rig.State.Signal === 'Hazard') && Rig.Blink,
        Right:    (Rig.State.Signal === 'Right' || Rig.State.Signal === 'Hazard') && Rig.Blink,
        Hazard:   Rig.State.Signal === 'Hazard' && Rig.Blink,
        Low:      Beams.Low && !Beams.High,
        High:     Beams.High,
        Parking:  Beams.Parking && !Beams.Low,
        Auto:     Rig.State.Beam === 'Auto' || Rig.State.AutoHigh,
        FogFront: Rig.State.FogFront,
        FogRear:  Rig.State.FogRear,
    };
    Root.querySelectorAll('[data-telltale]').forEach((Lamp) => Lamp.classList.toggle('Lit', !!Live[Lamp.dataset.telltale]));
}

// VISOR COURSE
// 🧩 The night pass the Visor demo drives on: an endless straight two-lane road through snow and pines under a dark sky,
//    plus the props the head-up display reacts to (porcupine, traffic signal, fuel station, teammate beacon).
// 📝 World axes match the ControlVehicle: +X forward along the road, +Y up, +Z to the right. Our lane is z = +LaneOffset.
//    Repeating scenery is snapped to its pattern period around the car, so the road never ends and nothing drifts.

import { THREE } from '../Shared/VehicleAssembly.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const LaneWidth  = 4.2;                                     // [m]
export const LaneOffset = LaneWidth / 2;                           // [m] - centre of our lane (right-hand traffic)
export const RoadHalf   = LaneWidth + 1.2;                         // [m] - asphalt half width (lanes plus shoulder)
const DashPeriod   = 12;                                           // [m] - centre dash 4 m, gap 8 m
const ScenePeriod  = 600;                                          // [m] - trees and posts repeat every 600 m
const PostSpacing  = 50;                                           // [m]

//------------------------------------------------------------------------------------------------------------------------
//                                                    HELPERS
//------------------------------------------------------------------------------------------------------------------------

function Seeded(Seed)
{
    let State = Seed >>> 0;
    return () => { State = (State * 1664525 + 1013904223) >>> 0; return State / 4294967296; };
}
function CanvasTexture(Width, Height, Paint)
{
    const Canvas = document.createElement('canvas');
    Canvas.width = Width;
    Canvas.height = Height;
    Paint(Canvas.getContext('2d'), Width, Height);
    const Texture = new THREE.CanvasTexture(Canvas);
    Texture.colorSpace = THREE.SRGBColorSpace;
    Texture.anisotropy = 8;
    return Texture;
}
export function GlowSprite(Colour, Size)
{
    const Texture = GlowSprite.Texture ??= CanvasTexture(128, 128, (Context, W) =>
    {
        const Fill = Context.createRadialGradient(W / 2, W / 2, 0, W / 2, W / 2, W / 2);
        Fill.addColorStop(0, 'rgba(255,255,255,1)');
        Fill.addColorStop(0.25, 'rgba(255,255,255,0.45)');
        Fill.addColorStop(1, 'rgba(255,255,255,0)');
        Context.fillStyle = Fill;
        Context.fillRect(0, 0, W, W);
    });
    const Sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: Texture, color: Colour, transparent: true, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false, fog: false }));
    Sprite.scale.setScalar(Size);
    return Sprite;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ROAD AND SCENERY
//------------------------------------------------------------------------------------------------------------------------

export function BuildCourse(Scene)
{
    // Road: one long strip whose texture holds 12 m of asphalt, edge lines and one centre dash.
    const RoadMap = CanvasTexture(512, 512, (Context, W, H) =>
    {
        // u runs along the road (12 m), v across it (2 × RoadHalf).
        Context.fillStyle = '#3a3b3e';
        Context.fillRect(0, 0, W, H);
        const Noise = Seeded(7);
        for (let Grain = 0; Grain < 9000; Grain++)
        {
            const Shade = 40 + Noise() * 40;
            Context.fillStyle = `rgba(${Shade},${Shade},${Shade + 3},0.5)`;
            Context.fillRect(Noise() * W, Noise() * H, 1.5, 1.5);
        }
        const Across = (Z) => (Z + RoadHalf) / (RoadHalf * 2) * H;
        Context.fillStyle = 'rgba(30,30,32,0.55)';                                   // tyre polish in each lane
        for (const Lane of [-LaneOffset, LaneOffset]) for (const Track of [-0.85, 0.85]) Context.fillRect(0, Across(Lane + Track) - 10, W, 20);
        Context.fillStyle = '#e9e6dc';
        for (const Edge of [-LaneWidth, LaneWidth]) Context.fillRect(0, Across(Edge) - 5, W, 10);
        Context.fillRect(0, Across(0) - 4, W * (4 / DashPeriod), 8);
    });
    RoadMap.wrapS = THREE.RepeatWrapping;
    const RoadLength = 900;
    RoadMap.repeat.set(RoadLength / DashPeriod, 1);
    const Road = new THREE.Mesh(new THREE.PlaneGeometry(RoadLength, RoadHalf * 2).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ map: RoadMap, roughness: 0.82, metalness: 0 }));
    Road.position.y = 0.002;
    Road.receiveShadow = true;
    Scene.add(Road);

    // Snow: a wide ground plane that catches the headlights beyond the shoulders.
    const SnowMap = CanvasTexture(256, 256, (Context, W, H) =>
    {
        Context.fillStyle = '#8f96a0';
        Context.fillRect(0, 0, W, H);
        const Noise = Seeded(11);
        for (let Flake = 0; Flake < 5000; Flake++) { const Shade = 120 + Noise() * 70; Context.fillStyle = `rgba(${Shade},${Shade + 4},${Shade + 10},0.35)`; Context.fillRect(Noise() * W, Noise() * H, 2, 2); }
    });
    SnowMap.wrapS = SnowMap.wrapT = THREE.RepeatWrapping;
    SnowMap.repeat.set(160, 160);
    const Snow = new THREE.Mesh(new THREE.PlaneGeometry(1600, 1600).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ map: SnowMap, roughness: 1, color: '#9aa3ad' }));
    Snow.position.y = -0.01;
    Scene.add(Snow);
    // Pines: two copies of a 600 m pattern so the strip always covers the view.
    const Pick = Seeded(23), Spots = [];
    for (let Each = 0; Each < 170; Each++)
    {
        const Side = Pick() < 0.5 ? -1 : 1;
        // The right side keeps a 20 m clearing so the fuel station bay (wherever it lands in the pattern) stays open.
        Spots.push([Pick() * ScenePeriod, Side * (RoadHalf + (Side > 0 ? 20 : 7) + Pick() * Pick() * 70), 4 + Pick() * 9]);
    }
    const PineGeometry = new THREE.ConeGeometry(1, 1, 7).translate(0, 0.5, 0);
    const Pines = new THREE.InstancedMesh(PineGeometry, new THREE.MeshStandardMaterial({ color: '#16241d', roughness: 0.95 }), Spots.length * 2);
    const Place = new THREE.Object3D();
    Spots.forEach(([X, Z, Height], Index) =>
    {
        for (const Copy of [0, 1])
        {
            Place.position.set(X + Copy * ScenePeriod, 0, Z);
            Place.scale.set(Height * 0.28, Height, Height * 0.28);
            Place.updateMatrix();
            Pines.setMatrixAt(Index * 2 + Copy, Place.matrix);
        }
    });
    Scene.add(Pines);

    // Reflector posts with retro-reflective caps (brightened by the headlights in UpdateCourse).
    const PostCount = (ScenePeriod / PostSpacing) * 2 * 2;
    const Posts = new THREE.InstancedMesh(new THREE.BoxGeometry(0.1, 1.0, 0.1).translate(0, 0.5, 0), new THREE.MeshStandardMaterial({ color: '#d9dde2', roughness: 0.6 }), PostCount);
    const Caps  = new THREE.InstancedMesh(new THREE.BoxGeometry(0.04, 0.16, 0.08), new THREE.MeshBasicMaterial({ color: '#ffffff', toneMapped: false }), PostCount);
    const CapSpots = [];
    for (let Index = 0, Slot = 0; Index < ScenePeriod / PostSpacing * 2; Index++)
    {
        for (const Side of [-1, 1])
        {
            const X = Index * PostSpacing + (Side > 0 ? 0 : PostSpacing / 2);
            Place.position.set(X, 0, Side * (RoadHalf + 0.5));
            Place.scale.set(1, 1, 1);
            Place.updateMatrix();
            Posts.setMatrixAt(Slot, Place.matrix);
            Place.position.y = 0.85;
            Place.position.x -= 0.06;
            Place.updateMatrix();
            Caps.setMatrixAt(Slot, Place.matrix);
            Caps.setColorAt(Slot, new THREE.Color(Side > 0 ? '#ffb340' : '#ffffff'));
            CapSpots.push([X, Side]);
            Slot++;
        }
    }
    Scene.add(Posts, Caps);

    // Sky: a dark dome with a faint blue horizon, stars and a distant ridge line; it travels with the car.
    const Sky = new THREE.Group();
    const Dome = new THREE.Mesh(new THREE.SphereGeometry(900, 32, 16), new THREE.ShaderMaterial({
        side: THREE.BackSide, depthWrite: false, fog: false,
        vertexShader: 'varying vec3 Spot; void main(){ Spot = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
        fragmentShader: 'varying vec3 Spot; void main(){ float Up = clamp(Spot.y, 0.0, 1.0); vec3 Low = vec3(0.035, 0.05, 0.09); vec3 High = vec3(0.004, 0.006, 0.014); gl_FragColor = vec4(mix(Low, High, pow(Up, 0.45)), 1.0); }',
    }));
    const StarPositions = [], Starfield = Seeded(31);
    for (let Each = 0; Each < 1400; Each++)
    {
        const Yaw = Starfield() * Math.PI * 2, Rise = Math.asin(0.08 + Starfield() * 0.92);
        StarPositions.push(Math.cos(Rise) * Math.cos(Yaw) * 850, Math.sin(Rise) * 850, Math.cos(Rise) * Math.sin(Yaw) * 850);
    }
    const Stars = new THREE.Points(new THREE.BufferGeometry().setAttribute('position', new THREE.Float32BufferAttribute(StarPositions, 3)), new THREE.PointsMaterial({ color: '#cfd8ff', size: 1.6, sizeAttenuation: false, fog: false, transparent: true, opacity: 0.85 }));
    const Ridge = new THREE.Mesh(RidgeGeometry(), new THREE.MeshBasicMaterial({ color: '#04060a', fog: false }));
    Sky.add(Dome, Stars, Ridge);
    Scene.add(Sky);

    const Cap = new THREE.Color();
    return {
        // True when a circle of Radius at (X, Z) touches a pine (cone base, slightly forgiving).
        Hit(X, Z, Radius)
        {
            const Local = ((X % ScenePeriod) + ScenePeriod) % ScenePeriod;
            for (const [SX, SZ, Height] of Spots)
            {
                if (Math.abs(SZ - Z) > 6) continue;
                let Along = SX - Local;
                if (Along > ScenePeriod / 2) Along -= ScenePeriod;
                if (Along < -ScenePeriod / 2) Along += ScenePeriod;
                if (Math.hypot(Along, SZ - Z) < Height * 0.28 * 0.6 + Radius) return true;
            }
            return false;
        },
        Update(CarX, Lamps, CarZ = 0)
        {
            Road.position.x  = Math.floor(CarX / DashPeriod) * DashPeriod + RoadLength * 0.4;
            Snow.position.x  = Math.floor(CarX / 100) * 100;
            Snow.position.z  = Math.floor(CarZ / 100) * 100;
            const Tile = Math.floor((CarX - 100) / ScenePeriod) * ScenePeriod;
            Pines.position.x = Posts.position.x = Caps.position.x = Tile;
            Sky.position.set(CarX, 0, CarZ);
            // Retro-reflection: caps flare when the headlights reach them, strongest 20–90 m out.
            CapSpots.forEach(([X, Side], Slot) =>
            {
                let Ahead = Tile + X - CarX;
                const Flare = Ahead > 4 ? Math.min(1, Ahead / 20) * Math.exp(-Math.max(0, Ahead - 40) / 70) : 0;
                Cap.set(Side > 0 ? '#ffb340' : '#ffffff').multiplyScalar(0.06 + Flare * 1.6 * Lamps);
                Caps.setColorAt(Slot, Cap);
            });
            Caps.instanceColor.needsUpdate = true;
        },
    };
}

function RidgeGeometry()
{
    // A ring of low peaks 700 m out, drawn as a triangle strip between the horizon and the peak line.
    const Peaks = Seeded(5), Positions = [], Steps = 180;
    for (let Step = 0; Step <= Steps; Step++)
    {
        const Yaw = (Step / Steps) * Math.PI * 2;
        const Height = 30 + Math.abs(Math.sin(Yaw * 3.1) * 60 + Math.sin(Yaw * 7.3) * 25) + Peaks() * 12;
        const X = Math.cos(Yaw) * 700, Z = Math.sin(Yaw) * 700;
        Positions.push(X, -20, Z, X, Height, Z);
    }
    const Indices = [];
    for (let Step = 0; Step < Steps; Step++) { const A = Step * 2; Indices.push(A, A + 1, A + 2, A + 1, A + 3, A + 2); }
    const Geometry = new THREE.BufferGeometry().setAttribute('position', new THREE.Float32BufferAttribute(Positions, 3));
    Geometry.setIndex(Indices);
    Geometry.computeVertexNormals();
    return Geometry;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EVENT PROPS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Low-poly porcupine: a dark body, cream-tipped quills that catch the headlights, and green eye shine.
export function MakePorcupine()
{
    const Group = new THREE.Group();
    const Fur   = new THREE.MeshStandardMaterial({ color: '#3a2c22', roughness: 0.9 });
    const Body  = new THREE.Mesh(new THREE.SphereGeometry(1, 16, 12), Fur);
    Body.scale.set(0.42, 0.26, 0.28);
    Body.position.y = 0.3;
    const Head  = new THREE.Mesh(new THREE.SphereGeometry(0.13, 12, 10), Fur);
    Head.position.set(0.42, 0.24, 0);
    const Nose  = new THREE.Mesh(new THREE.SphereGeometry(0.04, 8, 6), new THREE.MeshStandardMaterial({ color: '#120c09' }));
    Nose.position.set(0.55, 0.22, 0);
    Group.add(Body, Head, Nose);
    const QuillGeometry = new THREE.ConeGeometry(0.018, 0.34, 5).translate(0, 0.17, 0);
    const QuillMaterial = new THREE.MeshStandardMaterial({ color: '#e8dcc4', roughness: 0.5, emissive: '#2a241a' });
    const Spines = Seeded(41);
    const Quills = new THREE.InstancedMesh(QuillGeometry, QuillMaterial, 120);
    const Aim = new THREE.Object3D(), Up = new THREE.Vector3();
    for (let Each = 0; Each < 120; Each++)
    {
        const Yaw = Spines() * Math.PI * 2, Rise = 0.25 + Spines() * 1.2;
        Up.set(Math.cos(Rise) * Math.cos(Yaw) * 0.9 - 0.25, Math.sin(Rise), Math.cos(Rise) * Math.sin(Yaw)).normalize();
        Aim.position.set(Up.x * 0.36 - 0.05, 0.3 + Up.y * 0.2, Up.z * 0.24);
        Aim.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), Up.add(new THREE.Vector3(-0.6, 0, 0)).normalize());
        Aim.scale.setScalar(0.7 + Spines() * 0.6);
        Aim.updateMatrix();
        Quills.setMatrixAt(Each, Aim.matrix);
    }
    Group.add(Quills);
    const Eyes = [-1, 1].map((Side) =>
    {
        const Eye = GlowSprite('#9dff7a', 0.16);
        Eye.position.set(0.52, 0.29, Side * 0.06);
        Group.add(Eye);
        return Eye;
    });
    const Legs = [[0.22, 0.12], [0.22, -0.12], [-0.2, 0.12], [-0.2, -0.12]].map(([X, Z]) =>
    {
        const Leg = new THREE.Mesh(new THREE.CylinderGeometry(0.035, 0.03, 0.14, 6).translate(0, -0.07, 0), Fur);
        Leg.position.set(X, 0.14, Z);
        Group.add(Leg);
        return Leg;
    });
    Group.rotation.y = -Math.PI / 2;                                     // faces +Z: it walks across the road
    return { Group, Eyes, Legs, Body };
}

// 📝 Traffic signal on a mast arm over our lane, with a stop line painted across the lane.
export function MakeSignal()
{
    const Group = new THREE.Group();
    const Metal = new THREE.MeshStandardMaterial({ color: '#4a4f57', roughness: 0.5, metalness: 0.6 });
    const Pole  = new THREE.Mesh(new THREE.CylinderGeometry(0.12, 0.15, 6.2, 12).translate(0, 3.1, 0), Metal);
    Pole.position.z = RoadHalf + 0.8;
    const Arm   = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.08, 6.2, 10).rotateX(Math.PI / 2), Metal);
    Arm.position.set(0, 5.9, RoadHalf + 0.8 - 3.1);
    Group.add(Pole, Arm);
    const Head = new THREE.Group();
    Head.position.set(-0.05, 5.1, LaneOffset);
    const Housing = new THREE.Mesh(new THREE.BoxGeometry(0.34, 1.15, 0.42), new THREE.MeshStandardMaterial({ color: '#15171a', roughness: 0.6 }));
    const Backplate = new THREE.Mesh(new THREE.BoxGeometry(0.04, 1.45, 0.7), new THREE.MeshStandardMaterial({ color: '#0c0d0f', roughness: 0.7 }));
    Backplate.position.x = 0.18;
    Head.add(Housing, Backplate);
    const Colours = ['#ff2a1f', '#ffb21a', '#22ff7a'];
    const Lamps = Colours.map((Colour, Index) =>
    {
        const Lens = new THREE.Mesh(new THREE.CircleGeometry(0.13, 20).rotateY(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: Colour, toneMapped: false }));
        Lens.position.set(-0.175, 0.36 - Index * 0.36, 0);
        const Glow = GlowSprite(Colour, 2.4);
        Glow.position.set(-0.3, Lens.position.y, 0);
        Head.add(Lens, Glow);
        return { Lens, Glow, Colour: new THREE.Color(Colour) };
    });
    Group.add(Head);
    const StopLine = new THREE.Mesh(new THREE.PlaneGeometry(0.5, LaneWidth - 0.3).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ color: '#e9e6dc', roughness: 0.8 }));
    StopLine.position.set(-14, 0.004, LaneOffset);
    const FarLine = StopLine.clone();                                    // oncoming traffic stops on the far side
    FarLine.position.set(14, 0.004, -LaneOffset);
    Group.add(StopLine, FarLine);
    const Spill = new THREE.PointLight('#22ff7a', 30, 30, 1.6);
    Spill.position.set(-1, 4.6, LaneOffset);
    Group.add(Spill);
    return { Group, Lamps, Spill, Head, StopOffset: -14 };
}

// 📝 Roadside fuel station on the right: a lit canopy you can see from far off, pumps, a price board and a pull-in bay.
export function MakeStation()
{
    const Group = new THREE.Group();
    const BayZ  = RoadHalf + 6;
    const Bay   = new THREE.Mesh(new THREE.PlaneGeometry(90, 13).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ color: '#2c2d30', roughness: 0.85 }));
    Bay.position.set(0, 0.003, BayZ);
    const Canopy = new THREE.Mesh(new THREE.BoxGeometry(20, 0.8, 10), new THREE.MeshStandardMaterial({ color: '#e8ecf0', roughness: 0.5 }));
    Canopy.position.set(0, 5.6, BayZ + 1);
    const Underside = new THREE.Mesh(new THREE.PlaneGeometry(19.6, 9.6).rotateX(Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#f4f8ff', toneMapped: false }));
    Underside.position.set(0, 5.19, BayZ + 1);
    const Stripe = new THREE.Mesh(new THREE.BoxGeometry(20.05, 0.22, 10.05), new THREE.MeshBasicMaterial({ color: '#19c7a0', toneMapped: false }));
    Stripe.position.set(0, 5.55, BayZ + 1);
    Group.add(Bay, Canopy, Underside, Stripe);
    const Steel = new THREE.MeshStandardMaterial({ color: '#cfd5dc', roughness: 0.4, metalness: 0.5 });
    for (const X of [-8, 8]) for (const Z of [-3, 5])
    {
        const Pillar = new THREE.Mesh(new THREE.BoxGeometry(0.4, 5.2, 0.4).translate(0, 2.6, 0), Steel);
        Pillar.position.set(X, 0, BayZ + Z);
        Group.add(Pillar);
    }
    const PumpScreens = [];
    for (const X of [-4, 4])
    {
        const Pump = new THREE.Mesh(new THREE.BoxGeometry(0.9, 1.8, 0.6).translate(0, 0.9, 0), new THREE.MeshStandardMaterial({ color: '#1fb894', roughness: 0.45 }));
        Pump.position.set(X, 0, BayZ + 2.2);
        const Screen = new THREE.Mesh(new THREE.PlaneGeometry(0.5, 0.3).rotateY(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#bfffe8', toneMapped: false }));
        Screen.position.set(X - 0.46, 1.35, BayZ + 2.2);
        Group.add(Pump, Screen);
        PumpScreens.push(Screen);
    }
    // Price board facing the oncoming driver.
    const Board = CanvasTexture(256, 384, (Context, W, H) =>
    {
        Context.fillStyle = '#0b0d10'; Context.fillRect(0, 0, W, H);
        Context.fillStyle = '#19c7a0'; Context.fillRect(0, 0, W, 70);
        Context.fillStyle = '#ffffff'; Context.font = '500 40px "DM Sans", sans-serif'; Context.textAlign = 'center'; Context.fillText('NORTHPASS', W / 2, 50);
        Context.fillStyle = '#ff4a3a'; Context.font = '400 92px "DM Sans", sans-serif';
        Context.fillText('22.40', W / 2, 180); Context.fillText('24.10', W / 2, 310);
        Context.fillStyle = '#9aa3ad'; Context.font = '400 26px "DM Sans", sans-serif';
        Context.fillText('UNLEADED 95', W / 2, 212); Context.fillText('DIESEL 50', W / 2, 342);
    });
    const Sign = new THREE.Mesh(new THREE.PlaneGeometry(2.2, 3.3).rotateY(-Math.PI / 2), new THREE.MeshBasicMaterial({ map: Board, toneMapped: false }));
    Sign.position.set(-14, 5.2, RoadHalf + 1.8);
    const SignPole = new THREE.Mesh(new THREE.BoxGeometry(0.25, 3.6, 0.25).translate(0, 1.8, 0), Steel);
    SignPole.position.set(-13.9, 0, RoadHalf + 1.8);
    Group.add(Sign, SignPole);
    const Wash = new THREE.PointLight('#eaf4ff', 900, 60, 1.4);
    Wash.position.set(0, 4.8, BayZ + 1);
    Group.add(Wash);
    const Halo = GlowSprite('#cfe8ff', 26);
    Halo.position.set(0, 5.0, BayZ + 1);
    Halo.material.opacity = 0.18;
    Group.add(Halo);
    return { Group, BayZ, PumpX: -4 };
}

// 📝 Teammate ping: a tall light pillar with a ground ring, like a game objective marker.
export function MakeBeacon(Colour = '#3ff0ff')
{
    const Group = new THREE.Group();
    const Pillar = new THREE.Mesh(new THREE.CylinderGeometry(0.6, 0.6, 140, 16, 1, true).translate(0, 70, 0), new THREE.ShaderMaterial({
        uniforms: { Tone: { value: new THREE.Color(Colour) }, Level: { value: 1 } }, transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide, fog: false,
        vertexShader: 'varying float Height; void main(){ Height = uv.y; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
        fragmentShader: 'uniform vec3 Tone; uniform float Level; varying float Height; void main(){ gl_FragColor = vec4(Tone * Level * (1.0 - Height) * 0.9, 1.0); }',
    }));
    const Ring = new THREE.Mesh(new THREE.RingGeometry(2.4, 2.8, 48).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: Colour, transparent: true, opacity: 0.8, blending: THREE.AdditiveBlending, depthWrite: false, toneMapped: false, fog: false }));
    Ring.position.y = 0.05;
    const Crate = new THREE.Mesh(new THREE.BoxGeometry(1.2, 0.9, 1.2).translate(0, 0.45, 0), new THREE.MeshStandardMaterial({ color: '#2a3a44', roughness: 0.6, emissive: Colour, emissiveIntensity: 0.25 }));
    Group.add(Pillar, Ring, Crate);
    return { Group, Pillar, Ring };
}

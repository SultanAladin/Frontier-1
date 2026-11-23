// LOBBY SETS
// 🧩 The nine procedural garage sets of the lobby. Each set is a recipe: Build(Options, Random) grows the whole
//    environment from a seed and the player's garage options, and hands back its lights, fog, reflection strength,
//    a starting camera and the camera bounds. The car is not part of a set; it always sits at the origin facing +X.
// 📝 Inspiration: SourceImages/Lobby/Lobby-Ref-1…9 (jungle carports, a moss studio set, a white lightbox, a dark race
//    hall, pale monoliths, a concrete canopy and pit garages), Lobby-Ref-10 (a cyclorama photo studio); the Workshop has no reference.

import { THREE } from '../Shared/VehicleAssembly.js';
import {
    Seeded, TileTexture, ConcreteTexture, AsphaltTexture, MossTexture, BarkTexture, ShutterTexture, SignTexture, ForestTexture,
    Block, Glowing, Beam, Halo, FrondShape, LeafShape, Scatter, FernClump, Painted,
} from './LobbyKit.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHARED PIECES
//------------------------------------------------------------------------------------------------------------------------

const Up = new THREE.Vector3(0, 1, 0);

/** A floor material that lets the mirror twin show through: more gloss → more reflection, lower roughness. */
function GlossFloor({ Map = null, Tone = '#ffffff', Gloss = 0.6, Rough = 0.35, Metal = 0 } = {})
{
    return new THREE.MeshStandardMaterial({ map: Map, color: Tone, roughness: Rough * (1 - Gloss * 0.6), metalness: Metal, transparent: Gloss > 0.02, opacity: 1 - Gloss * 0.5 });
}

/** A flat floor at y = 0 with a rectangular hole (so a glossy pad can show the mirror twin under it). */
function FloorAround(Size, HoleWidth, HoleDepth, Material)
{
    const Outline = new THREE.Shape([new THREE.Vector2(-Size, -Size), new THREE.Vector2(Size, -Size), new THREE.Vector2(Size, Size), new THREE.Vector2(-Size, Size)]);
    Outline.holes.push(new THREE.Path([new THREE.Vector2(-HoleWidth / 2, -HoleDepth / 2), new THREE.Vector2(-HoleWidth / 2, HoleDepth / 2), new THREE.Vector2(HoleWidth / 2, HoleDepth / 2), new THREE.Vector2(HoleWidth / 2, -HoleDepth / 2)]));
    const Shape = new THREE.ShapeGeometry(Outline).rotateX(-Math.PI / 2);
    // ShapeGeometry UVs are in metres; scale them so repeat counts read per metre.
    const Uv = Shape.attributes.uv;
    for (let Index = 0; Index < Uv.count; Index++) Uv.setXY(Index, Uv.getX(Index) / 4, Uv.getY(Index) / 4);
    const Mesh = new THREE.Mesh(Shape, Material);
    Mesh.receiveShadow = true;
    return Mesh;
}

/** A floor plane centred on the origin. */
function Plane(Width, Depth, Material, Y = 0)
{
    const Mesh = new THREE.Mesh(new THREE.PlaneGeometry(Width, Depth).rotateX(-Math.PI / 2), Material);
    Mesh.position.y = Y;
    Mesh.receiveShadow = true;
    return Mesh;
}

/** Inward-facing walls and ceiling of a box room (floor left to the set). Skip lists walls to leave open. */
function Room(Width, Height, Depth, Wall, { Ceiling = Wall, Skip = [] } = {})
{
    const Group = new THREE.Group();
    const Add = (Name, W, H, X, Y, Z, Yaw, Pitch = 0, Material = Wall) =>
    {
        if (Skip.includes(Name)) return;
        const Mesh = new THREE.Mesh(new THREE.PlaneGeometry(W, H), Material);
        Mesh.position.set(X, Y, Z);
        Mesh.rotation.set(Pitch, Yaw, 0, 'YXZ');
        Mesh.receiveShadow = true;
        Group.add(Mesh);
    };
    Add('Back', Width, Height, 0, Height / 2, -Depth / 2, 0);
    Add('Front', Width, Height, 0, Height / 2, Depth / 2, Math.PI);
    Add('Left', Depth, Height, -Width / 2, Height / 2, 0, Math.PI / 2);
    Add('Right', Depth, Height, Width / 2, Height / 2, 0, -Math.PI / 2);
    Add('Ceiling', Width, Depth, 0, Height, 0, 0, Math.PI / 2, Ceiling);
    return Group;
}

/** A sky dome shaded by direction: horizon, zenith and below-horizon colours, plus an optional sun glow. */
function SkyDome({ Zenith, Horizon, Ground, Sun = null, SunTone = '#ffffff' })
{
    const Material = new THREE.ShaderMaterial({
        side: THREE.BackSide, depthWrite: false, fog: false,
        uniforms: { Zenith: { value: new THREE.Color(Zenith) }, Horizon: { value: new THREE.Color(Horizon) }, Ground: { value: new THREE.Color(Ground) },
                    SunWay: { value: Sun ? Sun.clone().normalize() : new THREE.Vector3(0, -1, 0) }, SunTone: { value: new THREE.Color(SunTone) } },
        vertexShader: 'varying vec3 Way; void main(){ Way = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
        fragmentShader: `uniform vec3 Zenith; uniform vec3 Horizon; uniform vec3 Ground; uniform vec3 SunWay; uniform vec3 SunTone; varying vec3 Way;
            void main(){ float H = Way.y; vec3 Sky = H > 0.0 ? mix(Horizon, Zenith, pow(H, 0.55)) : mix(Horizon, Ground, pow(-H, 0.4));
                float Glow = pow(max(dot(normalize(Way), SunWay), 0.0), 40.0) * 0.8 + pow(max(dot(normalize(Way), SunWay), 0.0), 6.0) * 0.25;
                gl_FragColor = vec4(Sky + SunTone * Glow, 1.0); }`,
    });
    const Dome = new THREE.Mesh(new THREE.SphereGeometry(420, 48, 24), Material);
    Dome.renderOrder = -10;
    return Dome;
}

/** Key light factory. Directional for suns and skylights, spot for studio heads. */
function Key(Type, Colour, Power, { Shadow = true, Angle = 0.6, Penumbra = 0.8, Spread = 13, Soft = 4 } = {})
{
    const Light = Type === 'Spot' ? new THREE.SpotLight(Colour, Power, 0, Angle, Penumbra, 1.2) : new THREE.DirectionalLight(Colour, Power);
    Light.castShadow = Shadow;
    if (Shadow)
    {
        Light.shadow.mapSize.set(2048, 2048);
        Light.shadow.bias = -0.0003;
        Light.shadow.normalBias = 0.025;
        Light.shadow.radius = Soft;
        if (Type !== 'Spot') Object.assign(Light.shadow.camera, { left: -Spread, right: Spread, top: Spread, bottom: -Spread, near: 0.5, far: 120 });
        else Object.assign(Light.shadow.camera, { near: 0.5, far: 60 });
    }
    return Light;
}

/** A spot pointed from a position at a target, already parented (target included) to Group. */
function Aim(Group, Light, From, To = [0, 0, 0])
{
    Light.position.set(...From);
    Light.target.position.set(...To);
    Group.add(Light, Light.target);
    return Light;
}

/** Studio light stand: tripod, pole, lamp head facing To, a lit lens, a beam cone and its spot light. */
function LightStand(Group, Random, { At, To, Height = 3.4, Tone = '#ffe2b8', Power = 60, Shadow = false, Haze = 0.5 })
{
    const Metal = new THREE.MeshStandardMaterial({ color: '#1c1d20', roughness: 0.4, metalness: 0.8 });
    const Stand = new THREE.Group();
    Stand.position.set(At[0], 0, At[1]);
    for (let Leg = 0; Leg < 3; Leg++)
    {
        const Angle = (Leg / 3) * Math.PI * 2 + 0.3;
        const Part = new THREE.Mesh(new THREE.CylinderGeometry(0.018, 0.018, 1.5), Metal);
        Part.position.set(Math.cos(Angle) * 0.45, 0.62, Math.sin(Angle) * 0.45);
        Part.lookAt(0, 1.25, 0); Part.rotateX(Math.PI / 2);
        Stand.add(Part);
    }
    const Pole = new THREE.Mesh(new THREE.CylinderGeometry(0.022, 0.022, Height), Metal);
    Pole.position.y = Height / 2;
    Stand.add(Pole);
    const Head = new THREE.Group();
    Head.position.y = Height;
    Head.lookAt(new THREE.Vector3(To[0] - At[0], To[1] - Height, To[2] - At[1]).add(new THREE.Vector3(0, Height, 0)));
    const Can = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.26, 0.42, 24).rotateX(Math.PI / 2), Metal);
    const Lens = new THREE.Mesh(new THREE.CircleGeometry(0.2, 24), Glowing(Tone, 6));
    Lens.position.z = 0.215;
    const Glare = Halo(Tone, 1.6, 0.9);
    Glare.position.z = 0.3;
    Head.add(Can, Lens, Glare);
    const Cone = Beam({ Radius: 1.9, Length: 6, Colour: Tone, Strength: 0.22 * Haze });
    Cone.rotation.x = -Math.PI / 2;                                 // apex at the lens, cone along the head's +Z
    Cone.position.z = 0.22;
    Head.add(Cone);
    Stand.add(Head);
    Group.add(Stand);
    const Light = Key('Spot', Tone, Power, { Shadow, Angle: 0.5, Penumbra: 0.9 });
    return Aim(Group, Light, [At[0], Height, At[1]], To);
}

const Swatch = (...Stops) => `linear-gradient(135deg, ${Stops.join(', ')})`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    1 · CANOPY GROVE
//------------------------------------------------------------------------------------------------------------------------

const Grove = {
    Key: 'Grove', Name: 'Canopy Grove', Tagline: 'Steel carport in a fern jungle', Swatch: Swatch('#0c1a0e', '#2f5a24', '#141516'), Reference: [1, 2],
    Controls: [
        { Key: 'Mood', Label: 'Mood', Type: 'Choice', Default: 'Overcast', Options: ['Overcast', 'Dusk', 'Night'] },
        { Key: 'Canopy', Label: 'Canopy', Type: 'Choice', Default: 'Black steel', Options: ['Black steel', 'Bronze', 'Concrete'] },
        { Key: 'Density', Label: 'Foliage', Type: 'Range', Min: 0.3, Max: 1.5, Step: 0.05, Default: 1, Unit: '×' },
        { Key: 'Wet', Label: 'Wet ground', Type: 'Toggle', Default: true },
        { Key: 'Uplights', Label: 'Garden uplights', Type: 'Toggle', Default: true },
    ],
    Build(Options, Random)
    {
        const Group = new THREE.Group();
        const Mood = { Overcast: { Sky: '#a9c6a4', Fog: '#132416', Key: 2.3, Hemi: 1.0, Back: '#132416' }, Dusk: { Sky: '#c7a97f', Fog: '#1a1a14', Key: 0.9, Hemi: 0.45, Back: '#191912' }, Night: { Sky: '#5c7591', Fog: '#05090a', Key: 0.4, Hemi: 0.14, Back: '#05080a' } }[Options.Mood];
        const Night = Options.Mood === 'Night';

        // Ground: earth all around, a wet pad under the canopy that shows the mirror twin.
        const Earth = new THREE.MeshStandardMaterial({ map: AsphaltTexture({ Tone: '#1b2016', Seed: 4 }), roughness: 0.95 });
        Earth.map.repeat.set(4, 4);
        Group.add(FloorAround(60, 15, 10, Earth));
        const Pad = GlossFloor({ Map: AsphaltTexture({ Tone: '#2a2c2f', Seed: 9, Repeat: [3, 2] }), Gloss: Options.Wet ? 0.55 : 0.08, Rough: 0.6 });
        Group.add(Plane(15, 10, Pad));

        // Canopy: four posts, a deep fascia beam and timber slats underneath.
        const Finish = { 'Black steel': { color: '#18191b', roughness: 0.45, metalness: 0.7 }, Bronze: { color: '#3d2f22', roughness: 0.4, metalness: 0.75 }, Concrete: { map: ConcreteTexture({ Tone: '#6f6c67', Repeat: [3, 1] }), color: '#ffffff', roughness: 0.85 } }[Options.Canopy];
        const Steel = new THREE.MeshStandardMaterial(Finish);
        for (const [X, Z] of [[-6.6, -4.2], [6.6, -4.2], [6.6, 4.2], [-6.6, 4.2]]) Group.add(Block(0.32, 3.9, 0.32, Steel, X, 1.95, Z));
        Group.add(Block(14.6, 0.62, 9.6, Steel, 0, 4.21, 0));
        const Timber = new THREE.MeshStandardMaterial({ color: '#6b4a2f', roughness: 0.7 });
        for (let Slat = 0; Slat < 22; Slat++) Group.add(Block(13.4, 0.08, 0.16, Timber, 0, 3.86, -4.1 + Slat * 0.39, { Cast: false }));
        // LED line under the fascia lights the car softly at night.
        const Strip = Block(13.2, 0.03, 0.05, Glowing('#ffd8a0', Night ? 5 : 1.6), 0, 3.82, 4.55, { Cast: false });
        Group.add(Strip);

        // Foliage: fern clumps, tree ferns, broad-leaf bushes, distant trunks, stones. Kept off the pad.
        const FernMaterial = new THREE.MeshStandardMaterial({ color: '#ffffff', roughness: 0.75, side: THREE.DoubleSide });
        const Greens = ['#3f7a2a', '#4d8a2f', '#2f5f22', '#5c9a36'];
        const OffPad = (X, Z) => Math.abs(X) > 8.2 || Math.abs(Z) > 5.6;
        const Clumps = Math.round(46 * Options.Density);
        for (let Each = 0; Each < Clumps; Each++)
        {
            const Angle = Random.Next() * Math.PI * 2, Reach = Random.Range(12, 26);
            const X = Math.cos(Angle) * Reach * 1.1, Z = Math.sin(Angle) * Reach;
            if (!OffPad(X, Z)) continue;
            const Clump = FernClump(Random, { Fronds: 11, Length: Random.Range(1.4, 2.6), Material: FernMaterial, Tones: Greens });
            Clump.position.set(X, Random.Range(0.1, 0.5), Z);
            Group.add(Clump);
        }
        const TrunkMaterial = new THREE.MeshStandardMaterial({ map: BarkTexture({ Seed: 2 }), roughness: 0.9 });
        for (let Each = 0; Each < Math.round(14 * Options.Density); Each++)
        {
            const Angle = Random.Next() * Math.PI * 2, Reach = Random.Range(11, 22);
            const X = Math.cos(Angle) * Reach * 1.1, Z = Math.sin(Angle) * Reach;
            const Tall = Random.Range(4.5, 8);
            const Trunk = new THREE.Mesh(new THREE.CylinderGeometry(0.16, 0.24, Tall, 8), TrunkMaterial);
            Trunk.position.set(X, Tall / 2, Z);
            Trunk.rotation.z = Random.Range(-0.08, 0.08);
            Trunk.castShadow = true;
            const Crown = FernClump(Random, { Fronds: 16, Length: Random.Range(2.6, 3.6), Material: FernMaterial, Tones: Greens });
            Crown.position.set(X, Tall, Z);
            Group.add(Trunk, Crown);
        }
        for (let Each = 0; Each < 60; Each++)
        {
            const Angle = Random.Next() * Math.PI * 2, Reach = Random.Range(24, 40);
            const Tall = Random.Range(10, 20);
            const Trunk = new THREE.Mesh(new THREE.CylinderGeometry(0.2, 0.35, Tall, 6), TrunkMaterial);
            Trunk.position.set(Math.cos(Angle) * Reach, Tall / 2, Math.sin(Angle) * Reach);
            Group.add(Trunk);
        }
        const Leaf = LeafShape({ Length: 0.6, Width: 0.3 });
        const LeafMaterial = new THREE.MeshStandardMaterial({ color: '#ffffff', roughness: 0.55, side: THREE.DoubleSide });
        const Bushes = [];
        for (let Each = 0; Each < Math.round(34 * Options.Density); Each++)
        {
            const Angle = Random.Next() * Math.PI * 2, Reach = Random.Range(12.5, 19);
            const X = Math.cos(Angle) * Reach * 1.15, Z = Math.sin(Angle) * Reach;
            if (OffPad(X, Z)) Bushes.push([X, Z, Random.Range(0.6, 1.6)]);
        }
        Group.add(Scatter(Leaf, LeafMaterial, Bushes.length * 26, (Index, Pose) =>
        {
            const [X, Z, Size] = Bushes[Math.floor(Index / 26)];
            const Angle = Random.Next() * Math.PI * 2, Out = Random.Range(0, 0.7) * Size;
            Object.assign(Pose, { X: X + Math.cos(Angle) * Out, Y: Random.Range(0.1, 1.1) * Size, Z: Z + Math.sin(Angle) * Out, Yaw: Angle + Math.PI / 2, Pitch: Random.Range(-0.6, 0.3), Roll: Random.Range(-0.4, 0.4), Scale: Size * Random.Range(0.9, 1.6) });
        }, ['#4f8f2c', '#3c7a25', '#6aa23a', '#2e6a20'], Random));
        const Stone = new THREE.MeshStandardMaterial({ color: '#4a4d4f', roughness: Options.Wet ? 0.35 : 0.8 });
        Group.add(Scatter(new THREE.DodecahedronGeometry(0.5, 0), Stone, 70, (Index, Pose) =>
        {
            const Angle = Random.Next() * Math.PI * 2, Reach = Random.Range(7.6, 13);
            const X = Math.cos(Angle) * Reach * 1.15, Z = Math.sin(Angle) * Reach;
            if (!OffPad(X, Z)) return false;
            Object.assign(Pose, { X, Y: 0.05, Z, Yaw: Random.Next() * 6, Pitch: Random.Next(), Scale: Random.Range(0.3, 1.3) });
        }, ['#55595c', '#3e4144', '#60625a'], Random));

        // Lights: overcast sky key, green hemisphere bounce, warm garden uplights and canopy downlights.
        const Sky = Key('Directional', Mood.Sky, Mood.Key, { Spread: 16, Soft: 8 });
        Group.add(Sky, Sky.target);
        const Hemi = new THREE.HemisphereLight(Mood.Sky, '#16220f', Mood.Hemi);
        Group.add(Hemi);
        const Fills = [{ Light: Hemi, Power: Mood.Hemi }];
        const Plot = [];
        for (const X of [-3.2, 3.2])
        {
            const Down = Aim(Group, Key('Spot', '#ffd29a', Night ? 90 : 25, { Shadow: false, Angle: 0.75, Penumbra: 1 }), [X, 3.8, 0], [X, 0, 0]);
            Fills.push({ Light: Down, Power: Down.intensity });
            Group.add(Block(0.3, 0.02, 0.3, Glowing('#fff1d6', 4), X, 3.83, 0, { Cast: false }));
            Plot.push({ X, Z: 0, Tone: '#ffd29a', Label: 'Down' });
        }
        if (Options.Uplights)
            for (const [X, Z] of [[-9.5, -6.5], [9.5, -6.5], [-11, 5], [11, 5], [0, -8.5]])
            {
                const Uplight = new THREE.PointLight('#ffc27a', Night ? 26 : 8, 9, 1.6);
                Uplight.position.set(X, 0.4, Z);
                const Glare = Halo('#ffc27a', 0.9, 0.9);
                Glare.position.set(X, 0.25, Z);
                Group.add(Uplight, Glare);
                Fills.push({ Light: Uplight, Power: Uplight.intensity });
                Plot.push({ X, Z, Tone: '#ffc27a', Label: 'Up' });
            }

        return {
            Group, Background: new THREE.Color(Mood.Back), Fog: new THREE.FogExp2(Mood.Fog, Night ? 0.045 : 0.032), Mirror: Options.Wet ? 0.85 : 0.25,
            Key: { Light: Sky, Azimuth: 210, Elevation: 68, Reach: 30 }, Fills, Plot, Exposure: Night ? 1.15 : 1.0, Reflection: 1,
            View: { Yaw: 52, Pitch: 7, Distance: 11, Target: [0, 0.8, 0] }, Bounds: { X: [-12, 12], Y: [0.25, 9], Z: [-10.5, 10.5] },
        };
    },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    2 · MOSS STAGE
//------------------------------------------------------------------------------------------------------------------------

const MossStage = {
    Key: 'MossStage', Name: 'Moss Stage', Tagline: 'Black-box studio, living forest wall', Swatch: Swatch('#0b0b0c', '#9fb27a', '#f0b7c8'), Reference: [3],
    Controls: [
        { Key: 'Season', Label: 'Forest wall', Type: 'Choice', Default: 'Spring', Options: ['Spring', 'Autumn', 'Mist'] },
        { Key: 'Stands', Label: 'Light stands', Type: 'Toggle', Default: true },
        { Key: 'Log', Label: 'Fallen log', Type: 'Toggle', Default: true },
        { Key: 'Haze', Label: 'Haze', Type: 'Range', Min: 0, Max: 1, Step: 0.05, Default: 0.55, Unit: '' },
        { Key: 'Softbox', Label: 'Top softbox', Type: 'Range', Min: 0, Max: 2, Step: 0.05, Default: 1, Unit: '×' },
    ],
    Build(Options, Random)
    {
        const Group = new THREE.Group();
        const Black = new THREE.MeshStandardMaterial({ color: '#0d0d0e', roughness: 0.9 });
        Group.add(Room(36, 15, 30, Black));
        const Concrete = GlossFloor({ Map: ConcreteTexture({ Tone: '#3b3b3d', Seed: 31, Repeat: [6, 6], Seams: false }), Gloss: 0.45, Rough: 0.5 });
        Group.add(Plane(36, 30, Concrete));

        // LED forest wall behind the car, framed in black, with its glow spilling onto the set.
        const Wall = new THREE.Mesh(new THREE.PlaneGeometry(20, 11.25), new THREE.MeshBasicMaterial({ map: ForestTexture({ Season: Options.Season, Seed: Random.Next() * 1e9 }), color: new THREE.Color(1.15, 1.15, 1.15) }));
        Wall.position.set(-1, 5.9, -8.4);
        Group.add(Wall, Block(20.8, 11.9, 0.3, Black, -1, 5.9, -8.6));
        const WallTone = { Spring: '#d8e3b0', Autumn: '#ffcf8a', Mist: '#cfe0e6' }[Options.Season];
        const Spill = Key('Directional', WallTone, 1.1, { Shadow: false });
        Spill.position.set(-1, 5, -8); Spill.target.position.set(0, 0.5, 2);
        Group.add(Spill, Spill.target);

        // Moss island: an irregular soft-edged slab with mounds, a little stream and stones.
        const Rim = [];
        for (let Step = 0; Step < 48; Step++)
        {
            const Angle = (Step / 48) * Math.PI * 2;
            const Reach = 7.2 + Math.sin(Angle * 3 + Random.Range(0, 6)) * 0.8 + Random.Range(-0.35, 0.35);
            Rim.push(new THREE.Vector2(Math.cos(Angle) * Reach * 1.25, Math.sin(Angle) * Reach * 0.8 - 1));
        }
        const MossMaterial = new THREE.MeshStandardMaterial({ map: MossTexture({ Seed: 5, Repeat: [1, 1] }), roughness: 1 });
        MossMaterial.map.repeat.set(0.25, 0.25);
        const Island = new THREE.Mesh(new THREE.ExtrudeGeometry(new THREE.Shape(Rim), { depth: 0.08, bevelEnabled: true, bevelThickness: 0.1, bevelSize: 0.35, bevelSegments: 3 }).rotateX(-Math.PI / 2), MossMaterial);
        Island.position.y = 0.0;
        Island.receiveShadow = true;
        Group.add(Island);
        const Mound = new THREE.SphereGeometry(0.5, 12, 8, 0, Math.PI * 2, 0, Math.PI / 2);
        Group.add(Scatter(Mound, MossMaterial.clone(), 150, (Index, Pose) =>
        {
            const Spot = Rim[Math.floor(Random.Next() * Rim.length)], Inward = Random.Range(0.55, 1.0);
            const X = Spot.x * Inward, Z = Spot.y * Inward;
            if (Math.abs(X) < 3.8 && Math.abs(Z) < 1.9) return false;   // keep the car's footprint flat
            Object.assign(Pose, { X, Y: 0.1, Z, Yaw: Random.Next() * 6, Scale: Random.Range(0.3, 1.2) });
        }, ['#55722a', '#3f5a1f', '#6c8a33', '#4a6a24'], Random));
        const Water = new THREE.MeshPhysicalMaterial({ color: '#0a0f10', roughness: 0.02, metalness: 0.1, clearcoat: 1 });
        const Stream = new THREE.Mesh(new THREE.PlaneGeometry(1.1, 9, 1, 12).rotateX(-Math.PI / 2), Water);
        const Bend = Stream.geometry.attributes.position;
        for (let Index = 0; Index < Bend.count; Index++) Bend.setX(Index, Bend.getX(Index) + Math.sin(Bend.getZ(Index) * 0.6) * 0.8);
        Stream.position.set(-5.2, 0.2, 1.8);
        Stream.rotation.y = 0.9;
        Group.add(Stream);
        Group.add(Scatter(new THREE.DodecahedronGeometry(0.4, 1), new THREE.MeshStandardMaterial({ color: '#5a5d5a', roughness: 0.5 }), 26, (Index, Pose) =>
        {
            const Along = Random.Range(-4, 4);
            Object.assign(Pose, { X: -5.2 + Math.sin(0.9) * Along + Random.Range(-1, 1), Y: 0.15, Z: 1.8 + Math.cos(0.9) * Along + Random.Range(-0.8, 0.8), Yaw: Random.Next() * 6, Scale: Random.Range(0.3, 0.9) });
        }, ['#6a6d68', '#4d504c'], Random));
        // Small ferns at the island edge.
        const FernMaterial = new THREE.MeshStandardMaterial({ color: '#ffffff', roughness: 0.75, side: THREE.DoubleSide });
        for (let Each = 0; Each < 9; Each++)
        {
            const Spot = Rim[Math.floor(Random.Next() * Rim.length)];
            const Clump = FernClump(Random, { Fronds: 9, Length: Random.Range(0.8, 1.3), Material: FernMaterial, Tones: ['#4f7f2c', '#3d6a24', '#6b9a3a'] });
            Clump.position.set(Spot.x * 0.9, 0.15, Spot.y * 0.9);
            Group.add(Clump);
        }

        // Fallen log: a long tilted trunk with a broken end, a root plate and two snapped branches.
        if (Options.Log)
        {
            const Bark = new THREE.MeshStandardMaterial({ map: BarkTexture({ Seed: 17 }), roughness: 0.95 });
            const Log = new THREE.Group();
            const Trunk = new THREE.Mesh(new THREE.CylinderGeometry(0.42, 0.62, 7.2, 14, 6, true).rotateZ(Math.PI / 2), Bark);
            const Bumps = Trunk.geometry.attributes.position;
            for (let Index = 0; Index < Bumps.count; Index++) { const Lift = 1 + (Random.Next() - 0.5) * 0.12; Bumps.setY(Index, Bumps.getY(Index) * Lift); Bumps.setZ(Index, Bumps.getZ(Index) * Lift); }
            Trunk.geometry.computeVertexNormals();
            Trunk.castShadow = Trunk.receiveShadow = true;
            const Snap = new THREE.Mesh(new THREE.CylinderGeometry(0.3, 0.43, 0.3, 10).rotateZ(Math.PI / 2), Bark);
            Snap.position.x = 3.72;
            const Plate = new THREE.Mesh(new THREE.CylinderGeometry(1.1, 0.9, 0.5, 12), Bark);
            Plate.rotation.z = Math.PI / 2; Plate.position.x = -3.7;
            for (const [X, Yaw, Tilt] of [[0.8, 0.6, 0.7], [-1.6, -0.9, 0.5]])
            {
                const Branch = new THREE.Mesh(new THREE.CylinderGeometry(0.07, 0.14, 1.6, 6), Bark);
                Branch.position.set(X, 0.55, 0.25); Branch.rotation.set(Tilt, Yaw, 0.4);
                Branch.castShadow = true;
                Log.add(Branch);
            }
            Log.add(Trunk, Snap, Plate);
            Log.position.set(-1.6, 0.42, -4.4);
            Log.rotation.set(0, -0.25, 0.06);
            Log.scale.setScalar(0.72);
            Group.add(Log);
            const Moss = new THREE.Mesh(new THREE.CylinderGeometry(0.47, 0.66, 5, 14, 1, true, Math.PI * 0.2, Math.PI * 0.6).rotateZ(Math.PI / 2), new THREE.MeshStandardMaterial({ map: MossTexture({ Seed: 8 }), roughness: 1 }));
            Moss.position.copy(Log.position); Moss.rotation.copy(Log.rotation); Moss.scale.copy(Log.scale);
            Group.add(Moss);
        }

        // Lights: top softbox (key), two light stands, the forest wall spill.
        const Box = Block(9, 0.1, 6, Glowing('#ffffff', 2.4 * Math.max(0.2, Options.Softbox)), 0, 13.5, 0, { Cast: false });
        Group.add(Box, Block(9.6, 0.4, 6.6, Black, 0, 13.75, 0, { Cast: false }));
        const Top = Key('Directional', '#ffffff', 2.2 * Options.Softbox, { Spread: 11, Soft: 10 });
        Group.add(Top, Top.target);
        const Fills = [{ Light: Spill, Power: 1.1 }];
        const Plot = [{ X: -1, Z: -8.4, Tone: WallTone, Label: 'Wall' }];
        if (Options.Stands)
        {
            const Left = LightStand(Group, Random, { At: [-9.5, 4.5], To: [0, 0.6, 0], Tone: '#ffe0b0', Power: 70, Shadow: true, Haze: Options.Haze });
            const Right = LightStand(Group, Random, { At: [8.5, 6], To: [0, 0.6, 0], Tone: '#ffd6a0', Power: 55, Haze: Options.Haze });
            Fills.push({ Light: Left, Power: 70 }, { Light: Right, Power: 55 });
            Plot.push({ X: -9.5, Z: 4.5, Tone: '#ffe0b0', Label: 'Stand' }, { X: 8.5, Z: 6, Tone: '#ffd6a0', Label: 'Stand' });
        }
        const Hemi = new THREE.HemisphereLight('#ffffff', '#1a1c14', 0.25);
        Group.add(Hemi);
        Fills.push({ Light: Hemi, Power: 0.25 });

        return {
            Group, Background: new THREE.Color('#050505'), Fog: new THREE.FogExp2('#14130f', 0.004 + Options.Haze * 0.03), Mirror: 0.6,
            Key: { Light: Top, Azimuth: 160, Elevation: 82, Reach: 20 }, Fills, Plot, Exposure: 1.05, Reflection: 0.9,
            View: { Yaw: 28, Pitch: 7, Distance: 13, Target: [0, 0.9, 0] }, Bounds: { X: [-17, 17], Y: [0.25, 13], Z: [-7.8, 14.5] },
        };
    },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    3 · LIGHTBOX
//------------------------------------------------------------------------------------------------------------------------

const Lightbox = {
    Key: 'Lightbox', Name: 'Lightbox', Tagline: 'White room under a giant softbox', Swatch: Swatch('#f2f3f5', '#c8cbd0', '#26272b'), Reference: [4],
    Controls: [
        { Key: 'Floor', Label: 'Floor', Type: 'Choice', Default: 'Graphite tile', Options: ['Graphite tile', 'Polished concrete', 'White epoxy'] },
        { Key: 'Walls', Label: 'Walls', Type: 'Swatch', Default: 'Gallery', Options: [{ Id: 'Gallery', Colour: '#e4e6e9' }, { Id: 'Warm', Colour: '#e8e0d4' }, { Id: 'Slate', Colour: '#8e949b' }, { Id: 'Night', Colour: '#2a2c30' }] },
        { Key: 'Size', Label: 'Softbox size', Type: 'Range', Min: 0.5, Max: 1.3, Step: 0.05, Default: 1, Unit: '×' },
        { Key: 'Accent', Label: 'Accent line', Type: 'Swatch', Default: 'None', Options: [{ Id: 'None', Colour: 'transparent' }, { Id: 'Lime', Colour: '#b8ff3c' }, { Id: 'Red', Colour: '#ff3a3a' }, { Id: 'Ice', Colour: '#7fe6ff' }] },
        { Key: 'Gloss', Label: 'Floor gloss', Type: 'Range', Min: 0, Max: 1, Step: 0.05, Default: 0.55, Unit: '' },
    ],
    Build(Options, Random)
    {
        const Group = new THREE.Group();
        const WallTone = { Gallery: '#e4e6e9', Warm: '#e8e0d4', Slate: '#8e949b', Night: '#2a2c30' }[Options.Walls];
        const Wall = new THREE.MeshStandardMaterial({ color: WallTone, roughness: 0.92 });
        const Ceiling = new THREE.MeshStandardMaterial({ color: '#1c1d20', roughness: 0.8 });
        Group.add(Room(28, 10, 24, Wall, { Ceiling }));
        const Floors = {
            'Graphite tile':     () => GlossFloor({ Map: TileTexture({ Tile: '#2a2b2f', Grout: '#151517', Count: 4, Seed: Random.Next() * 1e6, Repeat: [7, 6] }), Gloss: Options.Gloss, Rough: 0.4 }),
            'Polished concrete': () => GlossFloor({ Map: ConcreteTexture({ Tone: '#8b8a88', Seed: 12, Repeat: [5, 4], Seams: false }), Gloss: Options.Gloss, Rough: 0.45 }),
            'White epoxy':       () => GlossFloor({ Tone: '#d9dbde', Gloss: Options.Gloss, Rough: 0.3 }),
        };
        Group.add(Plane(28, 24, Floors[Options.Floor]()));
        // Cove: a soft quarter-round where the back wall meets the floor (the classic infinity-studio sweep).
        const Cove = new THREE.Mesh(new THREE.CylinderGeometry(1.2, 1.2, 28, 24, 1, true, Math.PI, Math.PI / 2).rotateZ(Math.PI / 2), Wall.clone());
        Cove.material.side = THREE.BackSide;                          // seen from inside the curve
        Cove.position.set(0, 1.2, -10.8);
        Group.add(Cove);

        // The softbox: a recessed bright panel in the black ceiling, with a frame and truss.
        const Width = 12 * Options.Size, Depth = 8 * Options.Size;
        Group.add(Block(Width, 0.05, Depth, Glowing('#ffffff', 3.2), 0.5, 9.4, 0, { Cast: false }));
        const Housing = new THREE.MeshStandardMaterial({ color: '#0f1012', roughness: 0.6, metalness: 0.5 });
        for (const Side of [-1, 1])
        {
            Group.add(Block(Width + 0.6, 0.6, 0.3, Housing, 0.5, 9.25, Side * (Depth / 2 + 0.15), { Cast: false }));
            Group.add(Block(0.3, 0.6, Depth, Housing, 0.5 + Side * (Width / 2 + 0.15), 9.25, 0, { Cast: false }));
        }
        for (const X of [-8, 8]) { const Fixture = Block(0.35, 0.5, 0.35, Housing, X, 9.4, -6, { Cast: false }); Group.add(Fixture, Block(0.04, 0.6, 0.04, Housing, X, 9.75, -6, { Cast: false })); }

        // Accent line along the base of the walls.
        if (Options.Accent !== 'None')
        {
            const Accent = { Lime: '#b8ff3c', Red: '#ff3a3a', Ice: '#7fe6ff' }[Options.Accent];
            const Line = Glowing(Accent, 3);
            Group.add(Block(28, 0.05, 0.05, Line, 0, 0.04, 11.95, { Cast: false }), Block(0.05, 0.05, 24, Line, -13.95, 0.04, 0, { Cast: false }), Block(0.05, 0.05, 24, Line, 13.95, 0.04, 0, { Cast: false }));
        }

        // Lights: the softbox as a soft overhead key, a white bounce and four wall-wash points under the panel.
        const Top = Key('Directional', '#ffffff', 2.6 * Options.Size, { Spread: 12, Soft: 14 });
        Group.add(Top, Top.target);
        const Hemi = new THREE.HemisphereLight('#ffffff', Options.Walls === 'Night' ? '#141416' : '#9a9da2', 1.0);
        Group.add(Hemi);
        const Fills = [{ Light: Hemi, Power: 1.0 }];
        const Plot = [];
        for (const [X, Z] of [[-4, -3], [4, -3], [-4, 3], [4, 3]])
        {
            const Wash = new THREE.PointLight('#ffffff', 40 * Options.Size, 22, 1.4);
            Wash.position.set(X * Options.Size, 8.6, Z * Options.Size);
            Group.add(Wash);
            Fills.push({ Light: Wash, Power: Wash.intensity });
            Plot.push({ X: Wash.position.x, Z: Wash.position.z, Tone: '#ffffff', Label: '' });
        }
        return {
            Group, Background: new THREE.Color('#111214'), Fog: null, Mirror: 0.2 + Options.Gloss * 0.7,
            Key: { Light: Top, Azimuth: 200, Elevation: 84, Reach: 20 }, Fills, Plot, Exposure: Options.Walls === 'Night' ? 1.1 : 0.92, Reflection: 1.1,
            View: { Yaw: -36, Pitch: 6, Distance: 11, Target: [0, 0.8, 0] }, Bounds: { X: [-13.5, 13.5], Y: [0.25, 8.8], Z: [-10.8, 11.5] },
        };
    },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    4 · HANGAR
//------------------------------------------------------------------------------------------------------------------------

const Hangar = {
    Key: 'Hangar', Name: 'Race Hangar', Tagline: 'Dark hall, a wall of round lamps', Swatch: Swatch('#0a0b0d', '#2b2e35', '#e9eef7'), Reference: [5],
    Controls: [
        { Key: 'Lamp', Label: 'Lamp colour', Type: 'Swatch', Default: 'White', Options: [{ Id: 'White', Colour: '#f2f5ff' }, { Id: 'Ice', Colour: '#8fdcff' }, { Id: 'Ember', Colour: '#ffae5c' }, { Id: 'Race', Colour: '#ff3b3b' }] },
        { Key: 'Rows', Label: 'Lamp rows', Type: 'Range', Min: 1, Max: 3, Step: 1, Default: 2, Unit: '' },
        { Key: 'Bars', Label: 'Ceiling bars', Type: 'Toggle', Default: true },
        { Key: 'Haze', Label: 'Haze', Type: 'Range', Min: 0, Max: 1, Step: 0.05, Default: 0.45, Unit: '' },
        { Key: 'Gloss', Label: 'Floor gloss', Type: 'Range', Min: 0, Max: 1, Step: 0.05, Default: 0.75, Unit: '' },
    ],
    Build(Options, Random)
    {
        const Group = new THREE.Group();
        const Tone = { White: '#f2f5ff', Ice: '#8fdcff', Ember: '#ffae5c', Race: '#ff3b3b' }[Options.Lamp];
        const Panel = new THREE.MeshStandardMaterial({ map: ShutterTexture({ Tone: '#16171a', Repeat: [8, 1] }), roughness: 0.75, metalness: 0.3 });
        Panel.map.rotation = Math.PI / 2;
        Group.add(Room(40, 13, 30, Panel, { Ceiling: new THREE.MeshStandardMaterial({ color: '#08090a', roughness: 0.9 }) }));
        Group.add(Plane(40, 30, GlossFloor({ Map: TileTexture({ Tile: '#141518', Grout: '#0b0b0d', Count: 2, Vary: 0.02, Repeat: [10, 7.5] }), Gloss: Options.Gloss, Rough: 0.35 })));

        // Back wall: rows of round lamps (ring + dim lens + halo), the signature of the set.
        const Ring = new THREE.TorusGeometry(0.62, 0.08, 12, 40);
        const Lens = new THREE.CircleGeometry(0.55, 32);
        const Rows = Math.round(Options.Rows);
        for (let Row = 0; Row < Rows; Row++) for (let Column = 0; Column < 11; Column++)
        {
            const X = -15 + Column * 3, Y = 6.2 - Row * 2.1;
            const Lamp = new THREE.Mesh(Ring, Glowing(Tone, 3.2));
            Lamp.position.set(X, Y, -14.9);
            const Inner = new THREE.Mesh(Lens, Glowing(Tone, 0.35));
            Inner.position.set(X, Y, -14.92);
            const Glare = Halo(Tone, 2.4, 0.35);
            Glare.position.set(X, Y, -14.6);
            Group.add(Lamp, Inner, Glare);
        }
        const Wash = Key('Directional', Tone, 0.9, { Shadow: false });
        Wash.position.set(0, 8, -14); Wash.target.position.set(0, 0, 4);
        Group.add(Wash, Wash.target);

        // Ceiling truss with linear light bars and haze beams.
        const Truss = new THREE.MeshStandardMaterial({ color: '#1b1c1f', roughness: 0.5, metalness: 0.7 });
        const Fills = [{ Light: Wash, Power: 0.9 }];
        const Plot = [{ X: 0, Z: -14.5, Tone, Label: 'Lamps' }];
        for (const Z of [-6, 0, 6])
        {
            Group.add(Block(34, 0.35, 0.35, Truss, 0, 11.2, Z, { Cast: false }));
            if (!Options.Bars) continue;
            Group.add(Block(9, 0.06, 0.4, Glowing('#ffffff', 4), 0, 11.0, Z, { Cast: false }));
            const Down = Aim(Group, Key('Spot', '#f4f7ff', 140, { Shadow: Z === 0, Angle: 0.55, Penumbra: 0.75 }), [0, 10.9, Z], [0, 0, Z * 0.6]);
            Fills.push({ Light: Down, Power: 140 });
            const Cone = Beam({ Radius: 4.2, Length: 10.8, Colour: '#dfe8ff', Strength: 0.16 * Options.Haze });
            Cone.position.set(0, 10.9, Z);
            Group.add(Cone);
            Plot.push({ X: 0, Z, Tone: '#f4f7ff', Label: 'Bar' });
        }
        // Vertical LED bars on the side walls.
        for (const X of [-19.9, 19.9]) for (const Z of [-10, -4, 2, 8]) Group.add(Block(0.05, 6, 0.18, Glowing(Tone, 2.2), X, 4, Z, { Cast: false }));

        const Front = Key('Spot', '#ffffff', 120, { Angle: 0.45, Penumbra: 0.9, Shadow: !Options.Bars });
        Aim(Group, Front, [9, 8, 10], [0, 0.5, 0]);
        const Hemi = new THREE.HemisphereLight('#c9d4ff', '#050507', 0.18);
        Group.add(Hemi);
        Fills.push({ Light: Hemi, Power: 0.18 });
        return {
            Group, Background: new THREE.Color('#040405'), Fog: new THREE.FogExp2('#0b0c10', 0.006 + Options.Haze * 0.03), Mirror: 0.2 + Options.Gloss * 0.75,
            Key: { Light: Front, Azimuth: 48, Elevation: 34, Reach: 15 }, Fills, Plot, Exposure: 1.1, Reflection: 1,
            View: { Yaw: 62, Pitch: 6, Distance: 12.5, Target: [0, 1.0, 0] }, Bounds: { X: [-19.5, 19.5], Y: [0.25, 10.8], Z: [-14.5, 14.5] },
        };
    },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    5 · MONOLITH
//------------------------------------------------------------------------------------------------------------------------

const Monolith = {
    Key: 'Monolith', Name: 'Monolith', Tagline: 'Pale slabs on a mirror floor', Swatch: Swatch('#9fb8d8', '#e8ecf2', '#c9d0da'), Reference: [6],
    Controls: [
        { Key: 'Time', Label: 'Time of day', Type: 'Choice', Default: 'Dawn', Options: ['Dawn', 'Noon', 'Dusk'] },
        { Key: 'Slabs', Label: 'Slabs', Type: 'Range', Min: 3, Max: 9, Step: 1, Default: 5, Unit: '' },
        { Key: 'Gloss', Label: 'Floor gloss', Type: 'Range', Min: 0, Max: 1, Step: 0.05, Default: 0.8, Unit: '' },
        { Key: 'Streak', Label: 'Light streak', Type: 'Toggle', Default: true },
    ],
    Build(Options, Random)
    {
        const Group = new THREE.Group();
        const Time = {
            Dawn: { Zenith: '#5d84b8', Horizon: '#f4dccb', Ground: '#d9dde4', Sun: '#ffd2a8', Power: 2.4, Elevation: 14, Hemi: '#bcd3f2' },
            Noon: { Zenith: '#3f78c7', Horizon: '#dfe9f5', Ground: '#e8ecf2', Sun: '#ffffff', Power: 3.2, Elevation: 62, Hemi: '#cfe0f7' },
            Dusk: { Zenith: '#2b2f5e', Horizon: '#f2a46e', Ground: '#3a3640', Sun: '#ff9a52', Power: 2.0, Elevation: 6, Hemi: '#8a7fa8' },
        }[Options.Time];
        Group.add(SkyDome({ Zenith: Time.Zenith, Horizon: Time.Horizon, Ground: Time.Ground }));
        const Floor = new THREE.Mesh(new THREE.CircleGeometry(160, 64).rotateX(-Math.PI / 2), GlossFloor({ Tone: Time.Ground, Gloss: Options.Gloss, Rough: 0.3 }));
        Floor.receiveShadow = true;
        Group.add(Floor);

        const Stone = new THREE.MeshStandardMaterial({ map: ConcreteTexture({ Tone: '#e9ebee', Seed: 41, Repeat: [2, 3], Seams: false }), roughness: 0.75 });
        const Count = Math.round(Options.Slabs);
        const Start = Random.Range(0, Math.PI * 2);
        for (let Each = 0; Each < Count; Each++)
        {
            const Angle = Start + (Each / Count) * Math.PI * 2 + Random.Range(-0.25, 0.25);
            const Reach = Random.Range(11, 22);
            const Tall = Random.Range(7, 16), Wide = Random.Range(5, 14), Thick = Random.Range(0.9, 2.2);
            const Slab = Block(Wide, Tall, Thick, Stone, Math.cos(Angle) * Reach, Tall / 2 - 0.4, Math.sin(Angle) * Reach);
            Slab.rotation.set(Random.Range(-0.12, 0.12), -Angle + Math.PI / 2 + Random.Range(-0.6, 0.6), Random.Range(-0.32, 0.32));
            Group.add(Slab);
        }
        if (Options.Streak)
        {
            const Streak = Block(0.08, 0.02, 60, Glowing('#ffffff', 5), 0, 0.012, 0, { Cast: false, Receive: false });
            Streak.position.set(14, 0.012, -18);
            Streak.rotation.y = -0.95;
            Group.add(Streak);
        }
        const Sun = Key('Directional', Time.Sun, Time.Power, { Spread: 26, Soft: 2 });
        Group.add(Sun, Sun.target);
        const Hemi = new THREE.HemisphereLight(Time.Hemi, Time.Ground, 0.9);
        Group.add(Hemi);
        return {
            Group, Background: new THREE.Color(Time.Horizon), Fog: new THREE.Fog(Time.Horizon, 45, 150), Mirror: 0.15 + Options.Gloss * 0.8,
            Key: { Light: Sun, Azimuth: 135, Elevation: Time.Elevation, Reach: 60 }, Fills: [{ Light: Hemi, Power: 0.9 }], Plot: [], Exposure: Options.Time === 'Dusk' ? 1.05 : 0.85, Reflection: 1,
            View: { Yaw: 22, Pitch: 6, Distance: 14, Target: [0, 0.8, 0] }, Bounds: { X: [-60, 60], Y: [0.25, 30], Z: [-60, 60] },
        };
    },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    6 · SHADOW CANOPY
//------------------------------------------------------------------------------------------------------------------------

const ShadowCanopy = {
    Key: 'ShadowCanopy', Name: 'Shadow Canopy', Tagline: 'Concrete overhang, hard sun, open sea', Swatch: Swatch('#d8d2c8', '#8f8a82', '#9cc0de'), Reference: [7],
    Controls: [
        { Key: 'Time', Label: 'Sun', Type: 'Choice', Default: 'Noon', Options: ['Morning', 'Noon', 'Golden'] },
        { Key: 'Concrete', Label: 'Concrete', Type: 'Swatch', Default: 'Sand', Options: [{ Id: 'Sand', Colour: '#cfc6b8' }, { Id: 'Grey', Colour: '#a3a4a6' }, { Id: 'Charcoal', Colour: '#4c4d50' }] },
        { Key: 'Skylight', Label: 'Roof cut', Type: 'Range', Min: 0, Max: 1, Step: 0.05, Default: 0.6, Unit: '' },
        { Key: 'Bollard', Label: 'Charging post', Type: 'Toggle', Default: true },
    ],
    Build(Options, Random)
    {
        const Group = new THREE.Group();
        const Time = {
            Morning: { Zenith: '#6f9fd6', Horizon: '#e6eef6', Sun: '#fff1dc', Power: 3.6, Elevation: 24 },
            Noon:    { Zenith: '#4d86cf', Horizon: '#dde8f3', Sun: '#ffffff', Power: 4.2, Elevation: 48 },
            Golden:  { Zenith: '#516fa8', Horizon: '#f4cfa2', Sun: '#ffbf7a', Power: 3.4, Elevation: 12 },
        }[Options.Time];
        const Tone = { Sand: '#cfc6b8', Grey: '#a3a4a6', Charcoal: '#4c4d50' }[Options.Concrete];
        Group.add(SkyDome({ Zenith: Time.Zenith, Horizon: Time.Horizon, Ground: '#2f5c80' }));
        const Concrete = new THREE.MeshStandardMaterial({ map: ConcreteTexture({ Tone, Seed: 61, Repeat: [6, 6] }), roughness: 0.85 });
        // Deck: concrete apron ending at a sea edge on +X.
        const Deck = new THREE.Mesh(new THREE.BoxGeometry(70, 1, 50), Concrete);
        Deck.position.set(-15, -0.5, 0);
        Deck.receiveShadow = true;
        Group.add(Deck);
        const Sea = new THREE.Mesh(new THREE.PlaneGeometry(800, 800).rotateX(-Math.PI / 2), new THREE.MeshPhysicalMaterial({ color: '#2a5675', roughness: 0.08, metalness: 0.2, clearcoat: 1 }));
        Sea.position.y = -1.6;
        Group.add(Sea);

        // Roof slab with a triangular cut, a slanted wall on the left and a back wall: the shapes that throw the shadows.
        const Outline = new THREE.Shape([new THREE.Vector2(-16, -10), new THREE.Vector2(9, -10), new THREE.Vector2(14, 7), new THREE.Vector2(-16, 9)]);
        if (Options.Skylight > 0.02)
        {
            const Cut = Options.Skylight;
            Outline.holes.push(new THREE.Path([new THREE.Vector2(-2 - 6 * Cut, -4 * Cut), new THREE.Vector2(4 + 4 * Cut, -1 - 3 * Cut), new THREE.Vector2(1, 3 + 3 * Cut)]));
        }
        const RoofConcrete = Concrete.clone();
        RoofConcrete.map = Concrete.map.clone();
        RoofConcrete.map.repeat.set(0.12, 0.12);                        // extrude UVs are in metres
        RoofConcrete.map.needsUpdate = true;
        const Roof = new THREE.Mesh(new THREE.ExtrudeGeometry(Outline, { depth: 0.7, bevelEnabled: false }).rotateX(Math.PI / 2), RoofConcrete);
        Roof.position.y = 7.4;
        Roof.castShadow = Roof.receiveShadow = true;
        Group.add(Roof);
        const Slant = Block(1.1, 11, 20, Concrete, -9, 4.4, -1);
        Slant.rotation.z = -0.32;
        Group.add(Slant, Block(26, 7.4, 1, Concrete, -3, 3.7, -10.2));
        Group.add(Block(1, 7.4, 1, Concrete, 12.6, 3.7, 6.2));
        if (Options.Bollard)
        {
            const Post = new THREE.Group();
            Post.add(Block(0.32, 1.2, 0.22, new THREE.MeshStandardMaterial({ color: '#f1f1ef', roughness: 0.35 }), 0, 0.6, 0));
            Post.add(Block(0.2, 0.04, 0.01, Glowing('#5cff9a', 3), 0, 1.05, 0.115, { Cast: false }));
            Post.position.set(4.6, 0, -3.4);
            Group.add(Post);
        }
        const Sun = Key('Directional', Time.Sun, Time.Power, { Spread: 24, Soft: 1.5 });
        Group.add(Sun, Sun.target);
        const Hemi = new THREE.HemisphereLight('#bcd4ec', Tone, 0.75);
        Group.add(Hemi);
        return {
            Group, Background: new THREE.Color(Time.Horizon), Fog: new THREE.Fog(Time.Horizon, 60, 300), Mirror: 0,
            Key: { Light: Sun, Azimuth: 60, Elevation: Time.Elevation, Reach: 60 }, Fills: [{ Light: Hemi, Power: 0.75 }], Plot: [], Exposure: 0.85, Reflection: 1,
            View: { Yaw: 36, Pitch: 6, Distance: 14, Target: [-0.5, 0.9, 0] }, Bounds: { X: [-40, 22], Y: [0.25, 6.3], Z: [-9.5, 24] },
        };
    },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    7 · PIT BOX
//------------------------------------------------------------------------------------------------------------------------

const PitBox = {
    Key: 'PitBox', Name: 'Pit Box', Tagline: 'Race garage open to the pit lane', Swatch: Swatch('#2a2622', '#d9a35a', '#3a3e44'), Reference: [8, 9],
    Controls: [
        { Key: 'Time', Label: 'Session', Type: 'Choice', Default: 'Golden hour', Options: ['Golden hour', 'Overcast', 'Night race'] },
        { Key: 'Number', Label: 'Garage', Type: 'Range', Min: 1, Max: 24, Step: 1, Default: 4, Unit: '' },
        { Key: 'Stands', Label: 'Grandstand', Type: 'Swatch', Default: 'Red & yellow', Options: [{ Id: 'Red & yellow', Colour: 'linear-gradient(90deg,#c8323a 50%,#e8c13a 50%)' }, { Id: 'Blue & white', Colour: 'linear-gradient(90deg,#2a5cc8 50%,#eeeeee 50%)' }, { Id: 'Green & grey', Colour: 'linear-gradient(90deg,#2f8a4a 50%,#9a9ea4 50%)' }] },
        { Key: 'Tubes', Label: 'Ceiling tubes', Type: 'Toggle', Default: true },
        { Key: 'Gloss', Label: 'Wet floor', Type: 'Range', Min: 0, Max: 1, Step: 0.05, Default: 0.7, Unit: '' },
    ],
    Build(Options, Random)
    {
        const Group = new THREE.Group();
        const Session = {
            'Golden hour': { Sun: '#ffb46a', Power: 3.6, Elevation: 9, Sky: ['#6d8fbf', '#f6c991', '#4a4038'], Inside: 0.5, Exposure: 1.0 },
            Overcast:      { Sun: '#dfe6ee', Power: 1.4, Elevation: 40, Sky: ['#9aa6b2', '#d6dbe0', '#55585c'], Inside: 0.9, Exposure: 1.0 },
            'Night race':  { Sun: '#bcd2ff', Power: 0.25, Elevation: 50, Sky: ['#05070c', '#1b2333', '#0a0b0d'], Inside: 0.85, Exposure: 0.95 },
        }[Options.Time];
        const Night = Options.Time === 'Night race';
        const Concrete = new THREE.MeshStandardMaterial({ map: ConcreteTexture({ Tone: '#8c8679', Seed: 71, Repeat: [3, 1] }), roughness: 0.9 });
        const Dark = new THREE.MeshStandardMaterial({ color: '#1d1e21', roughness: 0.7 });

        // Garage shell: 18 m deep (car axis), 13 m wide, 6.2 m tall, open at +X onto the pit lane.
        const Shell = Room(18, 6.2, 13, Concrete, { Ceiling: Dark, Skip: ['Right'] });
        Group.add(Shell);
        const Shutter = new THREE.Mesh(new THREE.PlaneGeometry(12, 4.6), new THREE.MeshStandardMaterial({ map: ShutterTexture({ Tone: '#4a4c50', Repeat: [1, 6] }), roughness: 0.55, metalness: 0.6 }));
        Shutter.position.set(0.5, 2.3, -6.47);
        Group.add(Shutter, Block(12.6, 0.5, 0.3, Dark, 0.5, 4.85, -6.35));
        // Epoxy floor with painted box lines, then the pit lane asphalt outside.
        const Epoxy = GlossFloor({ Map: TileTexture({ Tile: '#3a3631', Grout: '#2c2925', Count: 2, Vary: 0.02, Repeat: [4, 3] }), Gloss: Options.Gloss, Rough: 0.4 });
        Group.add(Plane(18, 13, Epoxy));
        const Paint = Glowing('#d8d2b8', 0.55);
        for (const Z of [-3.4, 3.4]) Group.add(Block(15, 0.005, 0.12, Paint, 0, 0.004, Z, { Cast: false }));
        const Lane = new THREE.MeshStandardMaterial({ map: AsphaltTexture({ Tone: '#2b2c2f', Seed: 3, Repeat: [8, 8] }), roughness: 0.8 });
        const Outside = new THREE.Mesh(new THREE.PlaneGeometry(60, 80).rotateX(-Math.PI / 2), Lane);
        Outside.position.set(39, -0.01, 0);
        Outside.receiveShadow = true;
        Group.add(Outside);
        for (const Z of [-6.75, 6.75]) Group.add(Block(0.8, 6.2, 0.5, Concrete, 9.2, 3.1, Z));
        Group.add(Block(0.8, 1.3, 14, Concrete, 9.2, 5.55, 0));
        // Pit wall, fence and the grandstand with coloured seats in stripes.
        Group.add(Block(0.6, 1.1, 80, Concrete, 22, 0.55, 0));
        const Fence = new THREE.MeshStandardMaterial({ color: '#7a7f86', roughness: 0.4, metalness: 0.8, transparent: true, opacity: 0.35 });
        Group.add(Block(0.05, 3, 80, Fence, 22, 2.6, 0, { Cast: false }));
        const Palette = { 'Red & yellow': ['#c8323a', '#e8c13a'], 'Blue & white': ['#2a5cc8', '#eeeeee'], 'Green & grey': ['#2f8a4a', '#9a9ea4'] }[Options.Stands];
        for (let Tier = 0; Tier < 9; Tier++) Group.add(Block(1.2, 0.5, 80, Concrete, 30 + Tier * 1.2, 1 + Tier * 0.75, 0));
        const Seat = new THREE.BoxGeometry(0.45, 0.45, 0.5);
        Group.add(Scatter(Seat, new THREE.MeshStandardMaterial({ color: '#ffffff', roughness: 0.6 }), 9 * 120, (Index, Pose) =>
        {
            const Tier = Math.floor(Index / 120), Column = Index % 120;
            Object.assign(Pose, { X: 29.8 + Tier * 1.2, Y: 1.5 + Tier * 0.75, Z: -36 + Column * 0.6 });
        }, null, Random));
        const Seats = Group.children[Group.children.length - 1];
        const Tint = new THREE.Color();
        for (let Index = 0; Index < Seats.count; Index++)
        {
            const Tier = Math.floor(Index / 120), Column = Index % 120;
            Seats.setColorAt(Index, Tint.set(Palette[(Math.floor(Column / 16) + (Tier > 5 ? 1 : 0)) % 2]));
        }
        const Plate = String(Math.round(Options.Number)).padStart(2, '0');
        Group.add(Block(0.3, 1.4, 2.6, new THREE.MeshBasicMaterial({ map: SignTexture({ Text: Plate, Back: '#c8323a', Ink: '#ffffff', Size: 170 }) }), 29.5, 1.2, -9));
        Group.add(SkyDome({ Zenith: Session.Sky[0], Horizon: Session.Sky[1], Ground: Session.Sky[2] }));

        // Signs: garage number over the opening, DANGER on the side wall, a green exit sign.
        // The number shows outside over the opening, inside on the back wall above the shutter, and on the stand board.
        const Inside = new THREE.Mesh(new THREE.PlaneGeometry(1.7, 0.76), new THREE.MeshBasicMaterial({ map: SignTexture({ Text: Plate, Back: '#1a1a1a', Ink: '#ff5a3c', Size: 190 }) }));
        Inside.position.set(0.5, 5.55, -6.44);
        Group.add(Inside);
        const Over = new THREE.Mesh(new THREE.PlaneGeometry(2, 0.9), new THREE.MeshBasicMaterial({ map: SignTexture({ Text: Plate, Back: '#1a1a1a', Ink: '#ff5a3c', Size: 190 }) }));
        Over.position.set(9.62, 5.5, 0); Over.rotation.y = Math.PI / 2;
        const Danger = new THREE.Mesh(new THREE.PlaneGeometry(0.9, 0.55), new THREE.MeshStandardMaterial({ map: SignTexture({ Text: 'DANGER', Back: '#f2f2f2', Ink: '#c22', Size: 110, Border: '#c22' }) }));
        Danger.position.set(-8.97, 1.9, -2.2); Danger.rotation.y = Math.PI / 2;
        const Door = Block(0.08, 2.3, 1.2, new THREE.MeshStandardMaterial({ color: '#b79b6a', roughness: 0.6 }), -8.98, 1.15, -4.1);
        const Exit = new THREE.Mesh(new THREE.PlaneGeometry(0.5, 0.22), new THREE.MeshBasicMaterial({ map: SignTexture({ Text: 'EXIT', Back: '#0f8a3a', Ink: '#ffffff', Size: 140 }) }));
        Exit.position.set(-8.96, 2.75, -4.1); Exit.rotation.y = Math.PI / 2;
        Group.add(Over, Danger, Door, Exit);
        Group.add(Block(0.25, 0.4, 0.6, new THREE.MeshStandardMaterial({ color: '#c8c3b5', roughness: 0.5 }), -8.85, 1.6, 4.2));

        // Lights: the sun raking in through the opening (key), fluorescent tubes, night floods outside.
        const Sun = Key('Directional', Session.Sun, Session.Power, { Spread: 20, Soft: 2 });
        Group.add(Sun, Sun.target);
        const Fills = [];
        const Plot = [];
        if (Options.Tubes)
            for (const X of [-6, -2, 2, 6]) for (const Z of [-3.5, 0, 3.5])
            {
                Group.add(Block(1.6, 0.06, 0.14, Glowing('#eef4ff', 4.5), X, 6.1, Z, { Cast: false }));
                Group.add(Block(1.7, 0.04, 0.3, Dark, X, 6.16, Z, { Cast: false }));
            }
        for (const X of [-4.5, 4.5])
        {
            const Down = Aim(Group, Key('Spot', '#eef4ff', Options.Tubes ? 70 * Session.Inside : 6, { Shadow: X < 0 && Options.Tubes, Angle: 0.95, Penumbra: 1 }), [X, 6, 0], [X, 0, 0]);
            Fills.push({ Light: Down, Power: Down.intensity });
            Plot.push({ X, Z: 0, Tone: '#eef4ff', Label: 'Tubes' });
        }
        if (Night)
            for (const Z of [-24, 0, 24])
            {
                const Flood = Aim(Group, Key('Spot', '#e6eeff', 900, { Shadow: false, Angle: 0.5, Penumbra: 0.6 }), [26, 16, Z], [8, 0, Z * 0.3]);
                const Pole = Block(0.3, 16, 0.3, Dark, 26.5, 8, Z);
                const Glare = Halo('#e6eeff', 3, 0.9);
                Glare.position.set(26, 16, Z);
                Group.add(Pole, Glare);
                Fills.push({ Light: Flood, Power: 900 });
                Plot.push({ X: 26, Z, Tone: '#e6eeff', Label: 'Flood' });
            }
        const Hemi = new THREE.HemisphereLight(Session.Sky[1], '#2a241e', 0.35);
        Group.add(Hemi);
        Fills.push({ Light: Hemi, Power: 0.35 });
        return {
            Group, Background: new THREE.Color(Session.Sky[1]), Fog: new THREE.Fog(Session.Sky[1], 40, 160), Mirror: 0.1 + Options.Gloss * 0.8,
            Key: { Light: Sun, Azimuth: 20, Elevation: Session.Elevation, Reach: 50 }, Fills, Plot, Exposure: Session.Exposure, Reflection: 0.9,
            View: { Yaw: 150, Pitch: 5, Distance: 9.5, Target: [0, 0.8, 0] }, Bounds: { X: [-8.5, 28], Y: [0.25, 5.8], Z: [-6, 6] },
        };
    },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    8 · PHOTO STUDIO
//------------------------------------------------------------------------------------------------------------------------

/** A tripod on spread legs with a head on top (camera tripods and light stands share it). */
function Tripod(Material, Height, Spread)
{
    const Stand = new THREE.Group();
    for (let Leg = 0; Leg < 3; Leg++)
    {
        const Angle = (Leg / 3) * Math.PI * 2 + 0.5;
        const Foot = new THREE.Vector3(Math.cos(Angle) * Spread, 0, Math.sin(Angle) * Spread), Top = new THREE.Vector3(0, Height * 0.62, 0);
        const Part = new THREE.Mesh(new THREE.CylinderGeometry(0.022, 0.03, Foot.distanceTo(Top), 8), Material);
        Part.position.copy(Foot).add(Top).multiplyScalar(0.5);
        Part.quaternion.setFromUnitVectors(Up, Top.clone().sub(Foot).normalize());
        Part.castShadow = true;
        Stand.add(Part);
    }
    const Column = new THREE.Mesh(new THREE.CylinderGeometry(0.025, 0.025, Height * 0.4), Material);
    Column.position.y = Height * 0.8;
    Column.castShadow = true;
    Stand.add(Column);
    return Stand;
}

const PhotoStudio = {
    Key: 'PhotoStudio', Name: 'Photo Studio', Tagline: 'Cyclorama floor under a suspended softbox', Swatch: Swatch('#d9dadb', '#f7f7f7', '#2a2b2e'), Reference: [10],
    Controls: [
        { Key: 'Softbox', Label: 'Overhead softbox', Type: 'Range', Min: 0.3, Max: 1.6, Step: 0.05, Default: 1, Unit: '×' },
        { Key: 'Cyc', Label: 'Cyc floor', Type: 'Swatch', Default: 'White', Options: [{ Id: 'White', Colour: '#ecedee' }, { Id: 'Grey', Colour: '#8d8f92' }, { Id: 'Black', Colour: '#151517' }] },
        { Key: 'Walls', Label: 'Walls', Type: 'Choice', Default: 'Concrete', Options: ['Concrete', 'Black', 'White'] },
        { Key: 'Fresnel', Label: 'Fresnel back light', Type: 'Toggle', Default: true },
        { Key: 'Gear', Label: 'Crew gear', Type: 'Toggle', Default: true },
    ],
    Build(Options, Random)
    {
        const Group = new THREE.Group();
        const WallMaterial = {
            Concrete: () => new THREE.MeshStandardMaterial({ map: ConcreteTexture({ Tone: '#8f8d89', Seed: 81, Repeat: [3, 1], Seams: false }), roughness: 0.95 }),
            Black:    () => new THREE.MeshStandardMaterial({ color: '#141416', roughness: 0.9 }),
            White:    () => new THREE.MeshStandardMaterial({ color: '#e6e7e8', roughness: 0.95 }),
        }[Options.Walls]();
        const Ceiling = new THREE.MeshStandardMaterial({ color: '#0b0c0e', roughness: 0.9 });
        Group.add(Room(40, 12, 44, WallMaterial, { Ceiling }));

        // Floors: dark polished concrete all round, the painted cyc rectangle the car stands on.
        const CycTone = { White: '#ecedee', Grey: '#8d8f92', Black: '#151517' }[Options.Cyc];
        const Outer = new THREE.MeshStandardMaterial({ map: ConcreteTexture({ Tone: '#2e2f31', Seed: 82, Repeat: [1.2, 1.2], Seams: false }), roughness: 0.6 });
        Group.add(FloorAround(40, 19, 15, Outer));
        Group.add(Plane(19, 15, GlossFloor({ Tone: CycTone, Gloss: Options.Cyc === 'Black' ? 0.55 : 0.1, Rough: 0.6 })));

        // The suspended softbox: a bright diffuser in a black frame hanging on four cables.
        const Width = 19, Depth = 11, Height = 5.6;
        const Edging = new THREE.MeshStandardMaterial({ color: '#0e0f11', roughness: 0.6, metalness: 0.4 });
        const Softbox = new THREE.Group();
        Softbox.position.set(-0.5, Height, 2.2);
        Softbox.rotation.x = 0.07;                                 // the camera-side edge hangs a little lower
        const Diffuser = new THREE.Mesh(new THREE.PlaneGeometry(Width, Depth).rotateX(Math.PI / 2), Glowing('#ffffff', 2.6 * Options.Softbox, { side: THREE.DoubleSide }));
        Softbox.add(Diffuser, Block(Width + 0.4, 0.5, Depth + 0.4, Edging, 0, 0.3, 0, { Cast: false }));
        for (const Side of [-1, 1])
        {
            Softbox.add(Block(Width + 0.4, 0.35, 0.18, Edging, 0, -0.1, Side * (Depth / 2 + 0.1), { Cast: false }));
            Softbox.add(Block(0.18, 0.35, Depth + 0.4, Edging, Side * (Width / 2 + 0.1), -0.1, 0, { Cast: false }));
        }
        for (const [X, Z] of [[-8.5, -5], [8.5, -5], [-8.5, 5], [8.5, 5]]) Softbox.add(Block(0.02, 12 - Height, 0.02, Edging, X, (12 - Height) / 2, Z, { Cast: false }));
        Group.add(Softbox);

        const Metal = new THREE.MeshStandardMaterial({ color: '#16171a', roughness: 0.45, metalness: 0.7 });
        const Fills = [], Plot = [];

        // Fresnel on a stand behind the car, aimed back at the lens (the bright point in the reference).
        if (Options.Fresnel)
        {
            const Spot = LightStand(Group, Random, { At: [3.6, -6.4], To: [0, 0.6, 0], Height: 1.6, Tone: '#fff4e2', Power: 70, Haze: 0.25 });
            Fills.push({ Light: Spot, Power: 70 });
            Plot.push({ X: 3.6, Z: -6.4, Tone: '#fff4e2', Label: 'Fresnel' });
        }
        // LED box light on the left and two umbrella heads on the right.
        {
            const Stand = Tripod(Metal, 2.2, 0.55);
            Stand.position.set(-9.5, 0, -1.5);
            const Head = new THREE.Group();
            Head.position.set(-9.5, 2.25, -1.5);
            Head.lookAt(0, 1, 0);
            Head.add(new THREE.Mesh(new THREE.BoxGeometry(0.55, 0.42, 0.18), Metal));
            const Face = new THREE.Mesh(new THREE.PlaneGeometry(0.48, 0.36), Glowing('#fff6e8', 3));
            Face.position.z = 0.095;
            Head.add(Face);
            Group.add(Stand, Head);
            const Box = Aim(Group, Key('Spot', '#fff4e4', 45, { Shadow: false, Angle: 0.7, Penumbra: 1 }), [-9.5, 2.25, -1.5], [0, 0.7, 0]);
            Fills.push({ Light: Box, Power: 45 });
            Plot.push({ X: -9.5, Z: -1.5, Tone: '#fff4e4', Label: 'Box' });
        }
        const Cloth = new THREE.MeshStandardMaterial({ color: '#111114', roughness: 0.9, side: THREE.DoubleSide });
        for (const [X, Z] of [[9.5, 4.5], [10.5, -1.5]])
        {
            const Stand = Tripod(Metal, 2.5, 0.6);
            Stand.position.set(X, 0, Z);
            const Head = new THREE.Group();
            Head.position.set(X, 2.6, Z);
            Head.lookAt(0, 1.2, 0);
            const Shell = new THREE.Mesh(new THREE.ConeGeometry(0.75, 0.55, 16, 1, true).rotateX(-Math.PI / 2), Cloth);
            Shell.position.z = -0.1;
            const Inner = new THREE.Mesh(new THREE.CircleGeometry(0.7, 16), Glowing('#ffffff', 1.4));
            Inner.position.z = 0.17;
            Head.add(Shell, Inner);
            Group.add(Stand, Head);
            const Soft = Aim(Group, Key('Spot', '#ffffff', 30, { Shadow: false, Angle: 0.9, Penumbra: 1 }), [X, 2.6, Z], [0, 0.8, 0]);
            Fills.push({ Light: Soft, Power: 30 });
            Plot.push({ X, Z, Tone: '#ffffff', Label: 'Umbrella' });
        }

        // Crew gear: the camera tripod in the foreground, a hairpin table with a laptop, flight cases and cables.
        if (Options.Gear)
        {
            const CameraStand = Tripod(Metal, 2.4, 0.8);
            CameraStand.position.set(-7.4, 0, 16.6);
            CameraStand.add(Block(0.3, 0.2, 0.16, Metal, 0, 2.48, 0));
            Group.add(CameraStand);
            const Table = new THREE.Group();
            Table.position.set(6.2, 0, 14.4);
            Table.rotation.y = -0.4;
            Table.add(Block(1.8, 0.05, 0.85, new THREE.MeshStandardMaterial({ color: '#1a1b1d', roughness: 0.6 }), 0, 0.95, 0));
            for (const [X, Z] of [[-0.8, -0.35], [0.8, -0.35], [-0.8, 0.35], [0.8, 0.35]]) Table.add(Block(0.025, 0.93, 0.025, Metal, X, 0.465, Z));
            Table.add(Block(0.42, 0.02, 0.3, Metal, 0.3, 0.985, 0.05));
            const Screen = new THREE.Mesh(new THREE.PlaneGeometry(0.4, 0.27), Glowing('#cfd8e6', 1.6));
            Screen.position.set(0.3, 1.13, -0.1);
            Screen.rotation.x = -0.25;
            Table.add(Screen);
            Group.add(Table);
            const Case = new THREE.MeshStandardMaterial({ color: '#1b1c1f', roughness: 0.5, metalness: 0.3 });
            for (const [X, Z, W] of [[-13.5, 9, 0.9], [-13.2, 10.1, 0.7], [11.6, -5.5, 1.0], [10.6, 12.4, 0.8]]) Group.add(Block(W, 0.6, 0.55, Case, X, 0.3, Z));
            const Cable = new THREE.MeshStandardMaterial({ color: '#0c0c0d', roughness: 0.7 });
            for (const [From, To] of [[[-9.5, -1.5], [-13.5, 9]], [[3.6, -6.4], [11.6, -5.5]], [[9.5, 4.5], [10.6, 12.4]]])
            {
                const Middle = [(From[0] + To[0]) / 2 + Random.Range(-1, 1), (From[1] + To[1]) / 2 + Random.Range(-1, 1)];
                const Curve = new THREE.CatmullRomCurve3([new THREE.Vector3(From[0], 0.02, From[1]), new THREE.Vector3(Middle[0], 0.02, Middle[1]), new THREE.Vector3(To[0], 0.02, To[1])]);
                Group.add(new THREE.Mesh(new THREE.TubeGeometry(Curve, 40, 0.012, 6), Cable));
            }
        }

        // Key: the softbox as a broad top light; a soft bounce from the cyc.
        const Top = Key('Directional', '#ffffff', 2.4 * Options.Softbox, { Spread: 11, Soft: 14 });
        Group.add(Top, Top.target);
        const Hemi = new THREE.HemisphereLight('#ffffff', Options.Cyc === 'Black' ? '#18181a' : '#8f9195', 0.5);
        Group.add(Hemi);
        Fills.push({ Light: Hemi, Power: 0.5 });
        return {
            Group, Background: new THREE.Color('#0b0c0e'), Fog: null, Mirror: Options.Cyc === 'Black' ? 0.6 : 0.22,
            Key: { Light: Top, Azimuth: 100, Elevation: 82, Reach: 20 }, Fills, Plot, Exposure: Options.Cyc === 'Black' ? 1.1 : 0.82, Reflection: 1.05,
            View: { Yaw: 104, Pitch: 1, Distance: 19.5, Target: [0, 1.5, 0] }, Bounds: { X: [-18.5, 18.5], Y: [0.25, 5.2], Z: [-20.5, 20.5] },
        };
    },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    9 · WORKSHOP
//------------------------------------------------------------------------------------------------------------------------

/** Pegboard with painted tool outlines and the tools hanging on them. */
function PegboardTexture(Board, Ink, Seed)
{
    const Random = Seeded(Seed);
    return Painted(1024, 384, (Context, Width, Height) =>
    {
        Context.fillStyle = Board;
        Context.fillRect(0, 0, Width, Height);
        Context.fillStyle = 'rgba(0,0,0,0.35)';
        for (let X = 12; X < Width; X += 24) for (let Y = 12; Y < Height; Y += 24) { Context.beginPath(); Context.arc(X, Y, 3, 0, Math.PI * 2); Context.fill(); }
        Context.fillStyle = Ink;
        Context.strokeStyle = Ink;
        let X = 40;
        while (X < Width - 60)
        {
            const Tool = Random.Pick(['Wrench', 'Wrench', 'Driver', 'Hammer', 'Pliers']);
            const Length = Random.Range(120, 220);
            Context.save();
            Context.translate(X, 40);
            if (Tool === 'Wrench') { Context.fillRect(-7, 18, 14, Length - 36); Context.beginPath(); Context.arc(0, 14, 16, 0, Math.PI * 2); Context.arc(0, Length - 16, 14, 0, Math.PI * 2); Context.fill(); }
            if (Tool === 'Driver') { Context.fillRect(-10, 0, 20, Length * 0.4); Context.fillRect(-3, Length * 0.4, 6, Length * 0.55); }
            if (Tool === 'Hammer') { Context.fillRect(-28, 0, 56, 26); Context.fillRect(-6, 26, 12, Length - 26); }
            if (Tool === 'Pliers') { Context.lineWidth = 9; Context.beginPath(); Context.moveTo(-4, 0); Context.lineTo(-14, Length); Context.moveTo(4, 0); Context.lineTo(14, Length); Context.stroke(); }
            Context.restore();
            X += Random.Range(40, 70);
        }
        Context.fillStyle = 'rgba(0,0,0,0.25)';
        Context.fillRect(0, Height - 6, Width, 6);
    }, { Repeat: [1, 1] });
}

const Workshop = {
    Key: 'Workshop', Name: 'Workshop', Tagline: 'Mechanics garage with a two-post lift', Swatch: Swatch('#2b2d30', '#c4242b', '#d8dadc'), Reference: [],
    Controls: [
        { Key: 'Style', Label: 'Style', Type: 'Choice', Default: 'Clean shop', Options: ['Clean shop', 'Old school', 'Night tuning'] },
        { Key: 'Lift', Label: 'Lift height', Type: 'Range', Min: 0, Max: 1.6, Step: 0.05, Default: 0, Unit: ' m' },
        { Key: 'Chests', Label: 'Tool chests', Type: 'Swatch', Default: 'Red', Options: [{ Id: 'Red', Colour: '#c4242b' }, { Id: 'Black', Colour: '#1b1c1f' }, { Id: 'Blue', Colour: '#1f4fa8' }, { Id: 'Green', Colour: '#2f7a3f' }] },
        { Key: 'Tyres', Label: 'Tyre stacks', Type: 'Range', Min: 0, Max: 6, Step: 1, Default: 3, Unit: '' },
        { Key: 'Door', Label: 'Roller door', Type: 'Choice', Default: 'Closed', Options: ['Closed', 'Open'] },
    ],
    Build(Options, Random)
    {
        const Group = new THREE.Group();
        const Style = {
            'Clean shop':   { Wall: '#d9dcdf', Band: '#3a3d42', Floor: '#80858b', Tube: '#f2f6ff', Tubes: 1, Board: '#c9ccd0', Ink: '#5a5e64', Exposure: 0.95, Back: '#0d0e10' },
            'Old school':   { Wall: '#b9ab93', Band: '#5a3b2a', Floor: '#6e6a62', Tube: '#ffe2b0', Tubes: 0.8, Board: '#a8875a', Ink: '#3a2a1a', Exposure: 1.0, Back: '#100d0a' },
            'Night tuning': { Wall: '#1d1e22', Band: '#0f1013', Floor: '#2e3035', Tube: '#cfd8ff', Tubes: 0.35, Board: '#24262b', Ink: '#5c606a', Exposure: 1.15, Back: '#05060a' },
        }[Options.Style];
        const Night = Options.Style === 'Night tuning';
        const Width = 22, Height = 6, Depth = 16;
        const Wall = new THREE.MeshStandardMaterial({ color: Style.Wall, roughness: 0.9 });
        const Dark = new THREE.MeshStandardMaterial({ color: '#232427', roughness: 0.8 });
        Group.add(Room(Width, Height, Depth, Wall, { Ceiling: Dark, Skip: ['Right'] }));
        // Lower wall band, and the front wall with the roller door opening.
        const Band = new THREE.MeshStandardMaterial({ color: Style.Band, roughness: 0.7 });
        Group.add(Block(Width, 1.2, 0.04, Band, 0, 0.6, -Depth / 2 + 0.03, { Cast: false }), Block(0.04, 1.2, Depth, Band, -Width / 2 + 0.03, 0.6, 0, { Cast: false }));
        const DoorWidth = 8, DoorHeight = 4.4;
        const FrontWall = (W, H, Y, Z) => { const Piece = new THREE.Mesh(new THREE.PlaneGeometry(W, H), Wall); Piece.position.set(Width / 2, Y, Z); Piece.rotation.y = -Math.PI / 2; Group.add(Piece); };
        FrontWall((Depth - DoorWidth) / 2, Height, Height / 2, -(DoorWidth / 2 + (Depth - DoorWidth) / 4));
        FrontWall((Depth - DoorWidth) / 2, Height, Height / 2, DoorWidth / 2 + (Depth - DoorWidth) / 4);
        FrontWall(DoorWidth, Height - DoorHeight, DoorHeight + (Height - DoorHeight) / 2, 0);
        const ShutterMaterial = new THREE.MeshStandardMaterial({ map: ShutterTexture({ Tone: '#8a8e94', Repeat: [1, 5] }), roughness: 0.5, metalness: 0.6 });
        const Open = Options.Door === 'Open';
        const ShutterHeight = Open ? 0.7 : DoorHeight;
        const Shutter = new THREE.Mesh(new THREE.PlaneGeometry(DoorWidth, ShutterHeight), ShutterMaterial);
        Shutter.position.set(Width / 2 - 0.05, DoorHeight - ShutterHeight / 2, 0);
        Shutter.rotation.y = -Math.PI / 2;
        Group.add(Shutter, Block(0.4, 0.5, DoorWidth + 0.4, Dark, Width / 2 - 0.2, DoorHeight + 0.25, 0));
        let Daylight = null;
        if (Open)
        {
            // The yard outside: asphalt, a far wall and sky, with daylight spilling in.
            const Yard = new THREE.Mesh(new THREE.PlaneGeometry(80, 80).rotateX(-Math.PI / 2), new THREE.MeshStandardMaterial({ map: AsphaltTexture({ Tone: '#3a3b3e', Seed: 91, Repeat: [10, 10] }), roughness: 0.85 }));
            Yard.position.set(Width / 2 + 40, -0.01, 0);
            Yard.receiveShadow = true;
            Group.add(Yard, SkyDome({ Zenith: '#6f9fd6', Horizon: '#e4ecf4', Ground: '#545a60' }));
            Group.add(Block(0.6, 3, 40, new THREE.MeshStandardMaterial({ map: ConcreteTexture({ Tone: '#a29d94', Seed: 92, Repeat: [8, 1] }), roughness: 0.9 }), Width / 2 + 16, 1.5, 0));
            Daylight = Aim(Group, Key('Spot', '#fff6e8', 260, { Shadow: true, Angle: 0.6, Penumbra: 0.8 }), [Width / 2 + 9, 5.5, 1], [0, 0, 0]);
        }

        // Epoxy floor, yellow bay lines, a drain.
        Group.add(Plane(Width, Depth, GlossFloor({ Map: ConcreteTexture({ Tone: Style.Floor, Seed: 93, Repeat: [4, 3], Seams: Options.Style !== 'Clean shop' }), Gloss: Night ? 0.55 : 0.4, Rough: 0.45 })));
        const Yellow = Glowing('#d9b01c', 0.8);
        for (const Z of [-2.6, 2.6]) Group.add(Block(9, 0.005, 0.12, Yellow, 0.5, 0.004, Z, { Cast: false }));
        Group.add(Block(0.12, 0.005, 5.2, Yellow, -4, 0.004, 0, { Cast: false }));
        Group.add(Block(0.5, 0.006, 3, new THREE.MeshStandardMaterial({ color: '#2b2c2e', roughness: 0.5, metalness: 0.8 }), 4.5, 0.003, 0, { Cast: false }));

        // Two-post lift: posts outside the car, arms reaching the sill points at the lift height.
        const LiftPaint = new THREE.MeshStandardMaterial({ color: Night ? '#2e6bff' : '#c4242b', roughness: 0.45, metalness: 0.4 });
        const Steel = new THREE.MeshStandardMaterial({ color: '#2a2c30', roughness: 0.4, metalness: 0.8 });
        const Lift = Options.Lift;
        for (const Side of [-1, 1])
        {
            Group.add(Block(0.34, 3.7, 0.4, LiftPaint, 0, 1.85, Side * 1.95));
            Group.add(Block(0.7, 0.05, 0.7, Steel, 0, 0.025, Side * 1.95));
            Group.add(Block(0.45, 0.35, 0.5, Steel, 0, 0.3 + Lift, Side * 1.95));
            for (const X of [-1.15, 1.15])
            {
                // Swing arm from the carriage on the post to the pad under the sill.
                const From = new THREE.Vector2(0, Side * 1.85), To = new THREE.Vector2(X, Side * 0.85);
                const Arm = Block(0.1, 0.07, From.distanceTo(To), Steel, (From.x + To.x) / 2, 0.22 + Lift, (From.y + To.y) / 2);
                Arm.rotation.y = Math.atan2(To.x - From.x, To.y - From.y);
                Group.add(Arm, Block(0.22, 0.05, 0.22, new THREE.MeshStandardMaterial({ color: '#111', roughness: 0.9 }), X, 0.275 + Lift, Side * 0.85));
            }
        }
        Group.add(Block(0.2, 0.2, 4.3, LiftPaint, 0, 3.8, 0, { Cast: false }));

        // Back wall: tool chests, a workbench with a pegboard, and a vice.
        const ChestPaint = new THREE.MeshStandardMaterial({ color: { Red: '#c4242b', Black: '#1b1c1f', Blue: '#1f4fa8', Green: '#2f7a3f' }[Options.Chests], roughness: 0.35, metalness: 0.45 });
        const Handle = new THREE.MeshStandardMaterial({ color: '#c8ccd2', roughness: 0.25, metalness: 1 });
        for (const X of [-8.6, -6.9])
        {
            Group.add(Block(1.5, 1.05, 0.65, ChestPaint, X, 0.6, -7.4));
            Group.add(Block(1.5, 0.55, 0.6, ChestPaint, X, 1.4, -7.45));
            for (let Drawer = 0; Drawer < 6; Drawer++) Group.add(Block(1.3, 0.02, 0.02, Handle, X, 0.25 + Drawer * 0.17 + (Drawer > 4 ? 0.3 : 0), -7.06, { Cast: false }));
            for (const Z of [-7.65, -7.15]) for (const Side of [-0.65, 0.65]) Group.add(Block(0.08, 0.08, 0.08, Steel, X + Side, 0.04, Z));
        }
        Group.add(Block(4.2, 0.08, 0.8, new THREE.MeshStandardMaterial({ color: '#6b5034', roughness: 0.7 }), -2.4, 0.95, -7.5));
        Group.add(Block(4.2, 0.9, 0.7, Steel, -2.4, 0.45, -7.5));
        const Board = new THREE.Mesh(new THREE.PlaneGeometry(4.2, 1.6), new THREE.MeshStandardMaterial({ map: PegboardTexture(Style.Board, Style.Ink, Random.Next() * 1e6), roughness: 0.8 }));
        Board.position.set(-2.4, 1.9, -7.96);
        Group.add(Board, Block(0.25, 0.2, 0.3, Steel, -0.6, 1.1, -7.4));

        // Left wall: tyre stacks, oil drums, a compressor, a fire extinguisher.
        const Rubber = new THREE.MeshStandardMaterial({ color: '#141414', roughness: 0.85 });
        const TyreShape = new THREE.TorusGeometry(0.33, 0.13, 10, 24).rotateX(Math.PI / 2);
        for (let Stack = 0; Stack < Math.round(Options.Tyres); Stack++)
        {
            const X = -10.2 + (Stack % 2) * 0.85, Z = -5.6 + Math.floor(Stack / 2) * 1.0 + Random.Range(-0.1, 0.1);
            const Count = 2 + Math.floor(Random.Next() * 4);
            for (let Level = 0; Level < Count; Level++)
            {
                const Tyre = new THREE.Mesh(TyreShape, Rubber);
                Tyre.position.set(X + Random.Range(-0.04, 0.04), 0.13 + Level * 0.26, Z);
                Tyre.castShadow = true;
                Group.add(Tyre);
            }
        }
        const DrumTones = ['#1f4fa8', '#c4242b', '#2a2b2e', '#d9b01c'];
        for (let Drum = 0; Drum < 3; Drum++)
        {
            const Barrel = new THREE.Mesh(new THREE.CylinderGeometry(0.3, 0.3, 0.9, 20), new THREE.MeshStandardMaterial({ color: Random.Pick(DrumTones), roughness: 0.5, metalness: 0.5 }));
            Barrel.position.set(-10.3 + Drum * 0.66, 0.45, 6.9);
            Barrel.castShadow = true;
            Group.add(Barrel);
        }
        const Tank = new THREE.Mesh(new THREE.CylinderGeometry(0.28, 0.28, 1.1, 18).rotateZ(Math.PI / 2), LiftPaint);
        Tank.position.set(-7.6, 0.4, 7.2);
        Group.add(Tank, Block(0.5, 0.35, 0.4, Steel, -7.6, 0.85, 7.2));
        const Extinguisher = new THREE.Mesh(new THREE.CylinderGeometry(0.09, 0.09, 0.5, 14), new THREE.MeshStandardMaterial({ color: '#d0201f', roughness: 0.4 }));
        Extinguisher.position.set(-10.9, 1.1, 2.5);
        Group.add(Extinguisher);

        // Ceiling: beams and tube fixtures; at night, a neon sign and an LED strip.
        for (const X of [-7, -2, 3, 8]) Group.add(Block(0.25, 0.4, Depth, Dark, X, Height - 0.2, 0, { Cast: false }));
        const Fills = [], Plot = [];
        for (const X of [-6, -1, 4]) for (const Z of [-2.6, 2.6])
        {
            Group.add(Block(1.6, 0.06, 0.16, Glowing(Style.Tube, 4 * Style.Tubes), X, Height - 0.75, Z, { Cast: false }));
            Group.add(Block(1.7, 0.08, 0.3, Steel, X, Height - 0.7, Z, { Cast: false }));
            Group.add(Block(0.01, 0.65, 0.01, Steel, X - 0.6, Height - 0.35, Z, { Cast: false }), Block(0.01, 0.65, 0.01, Steel, X + 0.6, Height - 0.35, Z, { Cast: false }));
        }
        for (const X of [-6, -1, 4])
        {
            const Tube = new THREE.PointLight(Style.Tube, 26 * Style.Tubes, 12, 1.5);
            Tube.position.set(X, Height - 0.9, 0);
            Group.add(Tube);
            Fills.push({ Light: Tube, Power: Tube.intensity });
            Plot.push({ X, Z: 0, Tone: Style.Tube, Label: 'Tubes' });
        }
        if (Night)
        {
            const Sign = new THREE.Mesh(new THREE.PlaneGeometry(3.6, 1.0), new THREE.MeshBasicMaterial({ map: SignTexture({ Text: 'TUNED', Back: '#05060a', Ink: '#ff3fd2', Width: 1024, Height: 288, Size: 210, Weight: 500 }), color: new THREE.Color(2.2, 2.2, 2.2), blending: THREE.AdditiveBlending, transparent: true }));
            Sign.position.set(4.5, 3.2, -7.97);
            const Glow = new THREE.PointLight('#ff3fd2', 18, 9, 1.4);
            Glow.position.set(4.5, 3.2, -7.2);
            Group.add(Sign, Glow);
            Group.add(Block(Width - 0.4, 0.04, 0.04, Glowing('#36e6ff', 4), 0, 0.08, -7.95, { Cast: false }));
            Group.add(Block(0.04, 0.04, Depth - 0.4, Glowing('#36e6ff', 4), -Width / 2 + 0.05, 0.08, 0, { Cast: false }));
            const Strip = new THREE.PointLight('#36e6ff', 10, 10, 1.4);
            Strip.position.set(-6, 0.5, -6);
            Group.add(Strip);
            Fills.push({ Light: Glow, Power: 18 }, { Light: Strip, Power: 10 });
            Plot.push({ X: 4.5, Z: -7.2, Tone: '#ff3fd2', Label: 'Neon' }, { X: -6, Z: -6, Tone: '#36e6ff', Label: 'Strip' });
        }
        if (Daylight) { Fills.push({ Light: Daylight, Power: 260 }); Plot.push({ X: Width / 2 + 9, Z: 1, Tone: '#fff6e8', Label: 'Door' }); }

        const Top = Key('Directional', Style.Tube, 1.6 * Style.Tubes + 0.2, { Spread: 9, Soft: 8 });
        Group.add(Top, Top.target);
        const Hemi = new THREE.HemisphereLight(Style.Tube, Style.Floor, Night ? 0.18 : 0.6);
        Group.add(Hemi);
        Fills.push({ Light: Hemi, Power: Hemi.intensity });
        return {
            Group, Background: new THREE.Color(Open ? '#e4ecf4' : Style.Back), Fog: Open ? new THREE.Fog('#e4ecf4', 40, 200) : null, Mirror: Night ? 0.7 : 0.45,
            Key: { Light: Top, Azimuth: 210, Elevation: 72, Reach: 16 }, Fills, Plot, Exposure: Style.Exposure, Reflection: 1, Lift,
            View: { Yaw: 20, Pitch: 9, Distance: 9.6, Target: [0, 0.8 + Lift, 0] }, Bounds: { X: [-10.5, Open ? 24 : 10.5], Y: [0.25, 5.4], Z: [-7.5, 7.5] },
        };
    },
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

export const Sets = [Grove, MossStage, Lightbox, Hangar, Monolith, ShadowCanopy, PitBox, PhotoStudio, Workshop];
export const SetByKey = Object.fromEntries(Sets.map((Set) => [Set.Key, Set]));

/** Default option values of a set. */
export const Defaults = (Set) => Object.fromEntries(Set.Controls.map((Control) => [Control.Key, Control.Default]));

// LOBBY KIT
// 🧩 Procedural building blocks for the garage lobby sets: a seeded random source, canvas-painted textures (tiles,
//    concrete, moss, bark, a painted forest wall, signs, sky gradients), fern and broad-leaf geometry for instanced
//    foliage, soft light beams and glow sprites. Nothing is loaded from disk; every set is grown from a seed.

import { THREE } from '../Shared/VehicleAssembly.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                    SEEDED RANDOM
//------------------------------------------------------------------------------------------------------------------------

/** Mulberry32: the same seed always grows the same set. Returns { Next, Range, Pick, Chance }. */
export function Seeded(Seed)
{
    let State = (Seed >>> 0) || 1;
    const Next = () =>
    {
        State = (State + 0x6D2B79F5) >>> 0;
        let Mix = State;
        Mix = Math.imul(Mix ^ (Mix >>> 15), Mix | 1);
        Mix ^= Mix + Math.imul(Mix ^ (Mix >>> 7), Mix | 61);
        return ((Mix ^ (Mix >>> 14)) >>> 0) / 4294967296;
    };
    return {
        Next,
        Range: (Low, High) => Low + Next() * (High - Low),
        Pick: (List) => List[Math.floor(Next() * List.length) % List.length],
        Chance: (Odds) => Next() < Odds,
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    CANVAS TEXTURES
//------------------------------------------------------------------------------------------------------------------------

/** Paints a canvas and wraps it as a repeating texture. */
export function Painted(Width, Height, Paint, { Repeat = [1, 1], Colour = true, Anisotropy = 8 } = {})
{
    const Canvas = document.createElement('canvas');
    Canvas.width = Width;
    Canvas.height = Height;
    const Context = Canvas.getContext('2d');
    Paint(Context, Width, Height);
    const Texture = new THREE.CanvasTexture(Canvas);
    Texture.wrapS = Texture.wrapT = THREE.RepeatWrapping;
    Texture.repeat.set(...Repeat);
    Texture.anisotropy = Anisotropy;
    if (Colour) Texture.colorSpace = THREE.SRGBColorSpace;
    return Texture;
}

const Shade = (Hex, Amount) => { const Tone = new THREE.Color(Hex); Tone.offsetHSL(0, 0, Amount); return `#${Tone.getHexString()}`; };

// Soft speckle used by concrete, moss and asphalt.
function Speckle(Context, Width, Height, Random, Count, Size, Tones, Alpha)
{
    for (let Each = 0; Each < Count; Each++)
    {
        Context.globalAlpha = Alpha * Random.Range(0.3, 1);
        Context.fillStyle = Random.Pick(Tones);
        const Radius = Random.Range(Size * 0.3, Size);
        Context.beginPath();
        Context.arc(Random.Next() * Width, Random.Next() * Height, Radius, 0, Math.PI * 2);
        Context.fill();
    }
    Context.globalAlpha = 1;
}

/** Floor tiles with thin grout and a little per-tile tone variation (Lightbox floor, pit epoxy squares). */
export function TileTexture({ Tile = '#26272b', Grout = '#121214', Count = 4, Vary = 0.035, Seed = 7, Repeat = [10, 10] } = {})
{
    const Random = Seeded(Seed);
    return Painted(1024, 1024, (Context, Width) =>
    {
        const Step = Width / Count;
        Context.fillStyle = Grout;
        Context.fillRect(0, 0, Width, Width);
        for (let Row = 0; Row < Count; Row++) for (let Column = 0; Column < Count; Column++)
        {
            Context.fillStyle = Shade(Tile, Random.Range(-Vary, Vary));
            Context.fillRect(Column * Step + 3, Row * Step + 3, Step - 6, Step - 6);
        }
        Speckle(Context, Width, Width, Random, 1400, 2.2, [Shade(Tile, 0.05), Shade(Tile, -0.05)], 0.25);
    }, { Repeat });
}

/** Board-formed concrete: blotches, fine speckle and faint formwork seams. */
export function ConcreteTexture({ Tone = '#b9b3aa', Seed = 3, Repeat = [2, 2], Seams = true } = {})
{
    const Random = Seeded(Seed);
    return Painted(1024, 1024, (Context, Width, Height) =>
    {
        Context.fillStyle = Tone;
        Context.fillRect(0, 0, Width, Height);
        Speckle(Context, Width, Height, Random, 90, 160, [Shade(Tone, 0.04), Shade(Tone, -0.05)], 0.18);
        Speckle(Context, Width, Height, Random, 6000, 1.6, [Shade(Tone, 0.12), Shade(Tone, -0.15)], 0.35);
        if (Seams)
        {
            Context.strokeStyle = Shade(Tone, -0.1);
            Context.globalAlpha = 0.35;
            for (let Line = 1; Line < 4; Line++) { Context.beginPath(); Context.moveTo(0, Line * Height / 4); Context.lineTo(Width, Line * Height / 4); Context.stroke(); }
            Context.globalAlpha = 1;
        }
    }, { Repeat });
}

/** Wet asphalt / dark ground. */
export function AsphaltTexture({ Tone = '#1d1f22', Seed = 5, Repeat = [6, 6] } = {})
{
    const Random = Seeded(Seed);
    return Painted(512, 512, (Context, Width, Height) =>
    {
        Context.fillStyle = Tone;
        Context.fillRect(0, 0, Width, Height);
        Speckle(Context, Width, Height, Random, 5000, 1.4, [Shade(Tone, 0.1), Shade(Tone, -0.06), '#3a3d42'], 0.5);
    }, { Repeat });
}

/** Moss carpet: dense green dots in three tones. */
export function MossTexture({ Seed = 11, Repeat = [3, 3], Tone = '#4e6b25' } = {})
{
    const Random = Seeded(Seed);
    return Painted(512, 512, (Context, Width, Height) =>
    {
        Context.fillStyle = Shade(Tone, -0.08);
        Context.fillRect(0, 0, Width, Height);
        Speckle(Context, Width, Height, Random, 7000, 3.2, [Tone, Shade(Tone, 0.08), Shade(Tone, -0.12), '#7a8f2e'], 0.8);
    }, { Repeat });
}

/** Bark: long dark furrows. */
export function BarkTexture({ Seed = 13, Repeat = [2, 1] } = {})
{
    const Random = Seeded(Seed);
    return Painted(256, 512, (Context, Width, Height) =>
    {
        Context.fillStyle = '#4a3a2c';
        Context.fillRect(0, 0, Width, Height);
        for (let Furrow = 0; Furrow < 60; Furrow++)
        {
            Context.strokeStyle = Random.Pick(['#2b2119', '#5c4a38', '#3a2e22', '#556b2f']);
            Context.globalAlpha = Random.Range(0.3, 0.8);
            Context.lineWidth = Random.Range(1, 5);
            let X = Random.Next() * Width;
            Context.beginPath(); Context.moveTo(X, 0);
            for (let Y = 0; Y <= Height; Y += 32) { X += Random.Range(-6, 6); Context.lineTo(X, Y); }
            Context.stroke();
        }
        Context.globalAlpha = 1;
    }, { Repeat });
}

/** Ribbed roller shutter (pit garage back wall). */
export function ShutterTexture({ Tone = '#3b3e43', Repeat = [1, 1] } = {})
{
    return Painted(256, 512, (Context, Width, Height) =>
    {
        for (let Y = 0; Y < Height; Y += 16)
        {
            const Band = Context.createLinearGradient(0, Y, 0, Y + 16);
            Band.addColorStop(0, Shade(Tone, 0.12)); Band.addColorStop(0.5, Tone); Band.addColorStop(1, Shade(Tone, -0.14));
            Context.fillStyle = Band;
            Context.fillRect(0, Y, Width, 16);
        }
    }, { Repeat });
}

/** Vertical sky gradient from a list of [stop, colour]. Used as a background texture. */
export function SkyTexture(Stops)
{
    const Texture = Painted(8, 512, (Context, Width, Height) =>
    {
        const Ramp = Context.createLinearGradient(0, 0, 0, Height);
        for (const [Stop, Colour] of Stops) Ramp.addColorStop(Stop, Colour);
        Context.fillStyle = Ramp;
        Context.fillRect(0, 0, Width, Height);
    }, { Anisotropy: 1 });
    Texture.wrapS = Texture.wrapT = THREE.ClampToEdgeWrapping;
    return Texture;
}

/** A sign plate: text on a coloured board (garage number, DANGER, exit). */
export function SignTexture({ Text, Back = '#d9d9d9', Ink = '#111', Width = 512, Height = 256, Weight = 700, Size = 150, Border = null })
{
    const Texture = Painted(Width, Height, (Context) =>
    {
        Context.fillStyle = Back;
        Context.fillRect(0, 0, Width, Height);
        if (Border) { Context.strokeStyle = Border; Context.lineWidth = 14; Context.strokeRect(12, 12, Width - 24, Height - 24); }
        Context.fillStyle = Ink;
        Context.font = `${Weight} ${Size}px "DM Sans", system-ui, sans-serif`;
        Context.textAlign = 'center';
        Context.textBaseline = 'middle';
        Context.fillText(Text, Width / 2, Height / 2 + Size * 0.04);
    }, { Anisotropy: 4 });
    Texture.wrapS = Texture.wrapT = THREE.ClampToEdgeWrapping;
    return Texture;
}

/**
 * The painted forest wall of the Moss Stage (a studio LED backdrop): misty depth layers of trunks, god rays from the
 * top right, blossom or autumn crowns and a ground glow. Season: 'Spring' | 'Autumn' | 'Mist'.
 */
export function ForestTexture({ Season = 'Spring', Seed = 21 } = {})
{
    const Random = Seeded(Seed);
    const Palette = {
        Spring: { Sky: ['#f6f1d2', '#9fb27a', '#3d4a2a'], Crown: ['#f0b7c8', '#e7a3bb', '#cfe0a1'], Trunk: '#3c3324', Ray: 'rgba(255,244,200,' },
        Autumn: { Sky: ['#ffe2a8', '#c08a3e', '#3e2b17'], Crown: ['#e88b2c', '#c9541c', '#f2c14e'], Trunk: '#33261a', Ray: 'rgba(255,214,150,' },
        Mist:   { Sky: ['#e3ecef', '#8fa3a6', '#2f3b3c'], Crown: ['#5f7a63', '#4b6650', '#7b9580'], Trunk: '#2c312c', Ray: 'rgba(235,245,255,' },
    }[Season] ?? null;
    const Texture = Painted(2048, 1152, (Context, Width, Height) =>
    {
        const Air = Context.createLinearGradient(0, 0, 0, Height);
        Air.addColorStop(0, Palette.Sky[0]); Air.addColorStop(0.45, Palette.Sky[1]); Air.addColorStop(1, Palette.Sky[2]);
        Context.fillStyle = Air;
        Context.fillRect(0, 0, Width, Height);
        // Depth layers: far trunks pale and thin, near trunks dark and wide.
        for (let Layer = 0; Layer < 4; Layer++)
        {
            const Near = Layer / 3;
            const Count = Math.round(14 - Layer * 3);
            for (let Each = 0; Each < Count; Each++)
            {
                const X = Random.Next() * Width;
                const Wide = 10 + Near * 70 + Random.Range(0, 20);
                Context.globalAlpha = 0.25 + Near * 0.7;
                Context.fillStyle = Shade(Palette.Trunk, (1 - Near) * 0.35);
                Context.beginPath();
                Context.moveTo(X - Wide * 0.5, Height);
                Context.bezierCurveTo(X - Wide * 0.42, Height * 0.6, X - Wide * 0.3, Height * 0.3, X - Wide * 0.25, -10);
                Context.lineTo(X + Wide * 0.25, -10);
                Context.bezierCurveTo(X + Wide * 0.3, Height * 0.3, X + Wide * 0.42, Height * 0.6, X + Wide * 0.6, Height);
                Context.fill();
                // Crowns: soft blossom / leaf clouds around the upper trunk.
                for (let Puff = 0; Puff < 6 + Near * 10; Puff++)
                {
                    Context.globalAlpha = (0.12 + Near * 0.25) * Random.Range(0.5, 1);
                    Context.fillStyle = Random.Pick(Palette.Crown);
                    Context.beginPath();
                    Context.arc(X + Random.Range(-160, 160), Random.Range(Height * 0.15, Height * 0.65), Random.Range(30, 90) * (0.6 + Near), 0, Math.PI * 2);
                    Context.fill();
                }
            }
            // Mist between layers.
            Context.globalAlpha = 0.18;
            Context.fillStyle = Palette.Sky[0];
            Context.fillRect(0, 0, Width, Height);
        }
        // God rays from the top right.
        Context.globalCompositeOperation = 'lighter';
        for (let Ray = 0; Ray < 9; Ray++)
        {
            const From = Width * Random.Range(0.55, 0.9);
            const Spread = Random.Range(60, 180);
            const Shaft = Context.createLinearGradient(From, 0, From - Width * 0.35, Height);
            Shaft.addColorStop(0, `${Palette.Ray}0.32)`); Shaft.addColorStop(1, `${Palette.Ray}0)`);
            Context.fillStyle = Shaft;
            Context.beginPath();
            Context.moveTo(From, 0); Context.lineTo(From + Spread * 0.25, 0);
            Context.lineTo(From - Width * 0.35 + Spread, Height); Context.lineTo(From - Width * 0.35 - Spread * 0.4, Height);
            Context.fill();
        }
        Context.globalCompositeOperation = 'source-over';
        // Undergrowth at the foot.
        Context.globalAlpha = 0.9;
        for (let Bush = 0; Bush < 160; Bush++)
        {
            Context.fillStyle = Random.Pick(['#2f4220', '#3e5a26', '#24331a', Palette.Crown[2]]);
            Context.beginPath();
            Context.arc(Random.Next() * Width, Height - Random.Range(0, Height * 0.12), Random.Range(14, 46), 0, Math.PI * 2);
            Context.fill();
        }
        Context.globalAlpha = 1;
    }, { Anisotropy: 8 });
    Texture.wrapS = Texture.wrapT = THREE.ClampToEdgeWrapping;
    return Texture;
}

/** Radial soft spot (contact shadow under the car, glow sprites). */
export function SoftSpot({ Inner = 'rgba(0,0,0,0.85)', Outer = 'rgba(0,0,0,0)', Size = 256 } = {})
{
    const Texture = Painted(Size, Size, (Context, Width) =>
    {
        const Ramp = Context.createRadialGradient(Width / 2, Width / 2, 0, Width / 2, Width / 2, Width / 2);
        Ramp.addColorStop(0, Inner); Ramp.addColorStop(1, Outer);
        Context.fillStyle = Ramp;
        Context.fillRect(0, 0, Width, Width);
    }, { Anisotropy: 1 });
    Texture.wrapS = Texture.wrapT = THREE.ClampToEdgeWrapping;
    return Texture;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SHAPES
//------------------------------------------------------------------------------------------------------------------------

/** Box mesh placed by its centre, with shadows on. */
export function Block(Width, Height, Depth, Material, X = 0, Y = 0, Z = 0, { Cast = true, Receive = true } = {})
{
    const Mesh = new THREE.Mesh(new THREE.BoxGeometry(Width, Height, Depth), Material);
    Mesh.position.set(X, Y, Z);
    Mesh.castShadow = Cast;
    Mesh.receiveShadow = Receive;
    return Mesh;
}

/** A light-emitting surface: unlit, bright, still tone mapped so it reads as glare rather than flat white. */
export function Glowing(Colour, Strength = 1, Extra = {})
{
    return new THREE.MeshBasicMaterial({ color: new THREE.Color(Colour).multiplyScalar(Strength), ...Extra });
}

/**
 * A soft volumetric beam: an open cone, additive, fading along its length and towards its rim.
 * The cone points down −Y from its apex at the origin.
 */
export function Beam({ Radius = 2, Length = 6, Colour = '#ffffff', Strength = 0.35 } = {})
{
    const Shape = new THREE.CylinderGeometry(0.02, Radius, Length, 40, 1, true).translate(0, -Length / 2, 0);
    const Material = new THREE.ShaderMaterial({
        uniforms: { Tone: { value: new THREE.Color(Colour) }, Strength: { value: Strength } },
        transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, side: THREE.DoubleSide, fog: false,
        vertexShader: `varying float Along; varying vec3 Facing; varying vec3 Seen;
            void main(){ Along = uv.y; vec4 Spot = modelViewMatrix * vec4(position, 1.0); Facing = normalize(normalMatrix * normal); Seen = normalize(-Spot.xyz); gl_Position = projectionMatrix * Spot; }`,
        fragmentShader: `uniform vec3 Tone; uniform float Strength; varying float Along; varying vec3 Facing; varying vec3 Seen;
            void main(){ float Rim = pow(abs(dot(Facing, Seen)), 1.6); float Fade = pow(Along, 1.4);
                gl_FragColor = vec4(Tone * Strength * Rim * Fade, 1.0); }`,
    });
    const Mesh = new THREE.Mesh(Shape, Material);
    Mesh.renderOrder = 5;
    return Mesh;
}

let GlowMap = null;
/** Camera-facing glow (lamp halos, light bulbs). */
export function Halo(Colour, Size = 1, Strength = 1)
{
    GlowMap ??= SoftSpot({ Inner: 'rgba(255,255,255,1)', Outer: 'rgba(255,255,255,0)' });
    const Sprite = new THREE.Sprite(new THREE.SpriteMaterial({ map: GlowMap, color: new THREE.Color(Colour).multiplyScalar(Strength), transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, fog: false }));
    Sprite.scale.setScalar(Size);
    return Sprite;
}

/**
 * One fern frond as a single geometry: an arching rachis with paired leaflets that shrink towards the tip.
 * Base at the origin, the frond grows along +Z and arches down; leaflets lie roughly in the XZ plane.
 */
export function FrondShape({ Length = 1.8, Pairs = 16, Arch = 0.55, Width = 0.26 } = {})
{
    const Spots = [];
    const Point = (Ratio) => new THREE.Vector3(0, Math.sin(Ratio * Math.PI * 0.75) * Arch - Ratio * Ratio * Arch * 0.9, Ratio * Length);
    for (let Pair = 0; Pair < Pairs; Pair++)
    {
        const Ratio = 0.12 + (Pair / Pairs) * 0.86;
        const At = Point(Ratio), Ahead = Point(Math.min(1, Ratio + 0.03));
        const Span = Width * Math.sin(Math.PI * Math.min(1, Ratio * 1.1)) * (1 - Ratio * 0.35);
        for (const Side of [-1, 1])
        {
            // Leaflet: a slim quad angled forward from the rachis, drooping slightly at its tip.
            const Tip = new THREE.Vector3(Side * Span, At.y - Span * 0.25, At.z + Span * 0.45);
            const Back = At.clone(), Front = Ahead.clone();
            const Mid = new THREE.Vector3(Side * Span * 0.5, (At.y + Tip.y) / 2 + 0.01, (At.z + Tip.z) / 2 + 0.02);
            Spots.push(...Back.toArray(), ...Mid.toArray(), ...Tip.toArray(), ...Back.toArray(), ...Tip.toArray(), ...Front.toArray());
        }
    }
    const Shape = new THREE.BufferGeometry();
    Shape.setAttribute('position', new THREE.Float32BufferAttribute(Spots, 3));
    Shape.computeVertexNormals();
    return Shape;
}

/** A broad leaf (foreground foliage): a lens-shaped blade, slightly folded along the midrib. Grows along +Z. */
export function LeafShape({ Length = 0.5, Width = 0.22, Fold = 0.06, Steps = 8 } = {})
{
    const Spots = [];
    const Edge = (Ratio) => Math.sin(Math.PI * Math.pow(Ratio, 0.8)) * Width * 0.5;
    for (let Step = 0; Step < Steps; Step++)
    {
        const A = Step / Steps, B = (Step + 1) / Steps;
        const Bend = (Ratio) => -Ratio * Ratio * Length * 0.25;
        const MidA = [0, Bend(A) + Fold, A * Length], MidB = [0, Bend(B) + Fold, B * Length];
        for (const Side of [-1, 1])
        {
            const EdgeA = [Side * Edge(A), Bend(A), A * Length], EdgeB = [Side * Edge(B), Bend(B), B * Length];
            Spots.push(...MidA, ...EdgeA, ...EdgeB, ...MidA, ...EdgeB, ...MidB);
        }
    }
    const Shape = new THREE.BufferGeometry();
    Shape.setAttribute('position', new THREE.Float32BufferAttribute(Spots, 3));
    Shape.computeVertexNormals();
    return Shape;
}

/**
 * Scatters Count copies of a shape as one InstancedMesh. Place(Index, Pose) fills Pose = { X, Y, Z, Yaw, Pitch, Roll,
 * Scale } and may return false to skip; each copy gets a small colour variation when Tones is given.
 */
export function Scatter(Shape, Material, Count, Place, Tones = null, Random = Seeded(1))
{
    const Mesh = new THREE.InstancedMesh(Shape, Material, Count);
    const Placement = new THREE.Object3D();
    const Tint = new THREE.Color();
    let Used = 0;
    for (let Index = 0; Index < Count; Index++)
    {
        const Pose = { X: 0, Y: 0, Z: 0, Yaw: 0, Pitch: 0, Roll: 0, Scale: 1 };
        if (Place(Index, Pose) === false) continue;
        Placement.position.set(Pose.X, Pose.Y, Pose.Z);
        Placement.rotation.set(Pose.Pitch, Pose.Yaw, Pose.Roll, 'YXZ');
        Placement.scale.setScalar(Pose.Scale);
        Placement.updateMatrix();
        Mesh.setMatrixAt(Used, Placement.matrix);
        if (Tones) Mesh.setColorAt(Used, Tint.set(Random.Pick(Tones)).offsetHSL(Random.Range(-0.015, 0.015), 0, Random.Range(-0.05, 0.05)));
        Used++;
    }
    Mesh.count = Used;
    Mesh.castShadow = true;
    Mesh.receiveShadow = true;
    return Mesh;
}

/** A clump of fronds around one crown (a fern bush, or the crown of a tree fern). Returns a Group. */
export function FernClump(Random, { Fronds = 12, Length = 1.8, Material, Tones }) 
{
    const Shape = FrondShape({ Length, Pairs: 15 });
    return Scatter(Shape, Material, Fronds, (Index, Pose) =>
    {
        Pose.Yaw = (Index / Fronds) * Math.PI * 2 + Random.Range(-0.2, 0.2);
        Pose.Pitch = -Random.Range(0.35, 0.95);
        Pose.Scale = Random.Range(0.75, 1.15);
    }, Tones, Random);
}

/** Frees every geometry, material and texture under Root (sets are rebuilt often while configuring). */
export function Release(Root)
{
    Root.traverse((Each) =>
    {
        Each.geometry?.dispose();
        const Materials = Array.isArray(Each.material) ? Each.material : Each.material ? [Each.material] : [];
        for (const Material of Materials)
        {
            for (const Value of Object.values(Material)) if (Value?.isTexture && Value !== GlowMap) Value.dispose();
            if (Material.uniforms) for (const Uniform of Object.values(Material.uniforms)) if (Uniform.value?.isTexture) Uniform.value.dispose();
            Material.dispose();
        }
    });
}

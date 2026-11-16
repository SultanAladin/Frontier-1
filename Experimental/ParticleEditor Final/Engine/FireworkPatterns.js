// FireworkPatterns.js — Shared 3D pyrotechnic shell & New Year's Text/Object pattern sampler
// Used identically by WebGPU (uploaded to GPU snap[] buffer) and SoftwarePreviewEngine.

export const FIREWORK_SHAPES = [
    { id: 0, key: 'bouquet', label: 'Grand Bouquet (Multi-Shell Pyro)' },
    { id: 1, key: 'text', label: 'Sky Text (Custom Message)' },
    { id: 2, key: 'heart', label: 'Glowing Heart' },
    { id: 3, key: 'star', label: '5-Point Star' },
    { id: 4, key: 'smiley', label: 'Hanabi Smiley Face' },
    { id: 5, key: 'saturn', label: 'Saturn Ringed Planet' },
    { id: 6, key: 'crown', label: 'Royal Crown' },
    { id: 7, key: 'butterfly', label: 'Iridescent Butterfly' },
    { id: 8, key: 'spiral', label: 'Spiral Galaxy' },
    { id: 9, key: 'finale', label: 'New Year\'s Grand Finale (Cycle All)' }
];

// Crisp 5x7 bitmap font for sky-writing fireworks (A-Z, 0-9, punctuation & icons)
const FONT_5X7 = {
    'A': ['01110', '10001', '10001', '11111', '10001', '10001', '10001'],
    'B': ['11110', '10001', '10001', '11110', '10001', '10001', '11110'],
    'C': ['01111', '10000', '10000', '10000', '10000', '10000', '01111'],
    'D': ['11110', '10001', '10001', '10001', '10001', '10001', '11110'],
    'E': ['11111', '10000', '10000', '11110', '10000', '10000', '11111'],
    'F': ['11111', '10000', '10000', '11110', '10000', '10000', '10000'],
    'G': ['01111', '10000', '10000', '10111', '10001', '10001', '01110'],
    'H': ['10001', '10001', '10001', '11111', '10001', '10001', '10001'],
    'I': ['11111', '00100', '00100', '00100', '00100', '00100', '11111'],
    'J': ['00111', '00010', '00010', '00010', '10010', '10010', '01100'],
    'K': ['10001', '10010', '10100', '11000', '10100', '10010', '10001'],
    'L': ['10000', '10000', '10000', '10000', '10000', '10000', '11111'],
    'M': ['10001', '11011', '10101', '10101', '10001', '10001', '10001'],
    'N': ['10001', '11001', '10101', '10011', '10001', '10001', '10001'],
    'O': ['01110', '10001', '10001', '10001', '10001', '10001', '01110'],
    'P': ['11110', '10001', '10001', '11110', '10000', '10000', '10000'],
    'Q': ['01110', '10001', '10001', '10001', '10101', '10010', '01101'],
    'R': ['11110', '10001', '10001', '11110', '10100', '10010', '10001'],
    'S': ['01111', '10000', '10000', '01110', '00001', '00001', '11110'],
    'T': ['11111', '00100', '00100', '00100', '00100', '00100', '00100'],
    'U': ['10001', '10001', '10001', '10001', '10001', '10001', '01110'],
    'V': ['10001', '10001', '10001', '10001', '10001', '01010', '00100'],
    'W': ['10001', '10001', '10001', '10101', '10101', '11011', '10001'],
    'X': ['10001', '10001', '01010', '00100', '01010', '10001', '10001'],
    'Y': ['10001', '10001', '01010', '00100', '00100', '00100', '00100'],
    'Z': ['11111', '00001', '00010', '00100', '01000', '10000', '11111'],
    '0': ['01110', '10011', '10101', '10101', '11001', '10001', '01110'],
    '1': ['00100', '01100', '00100', '00100', '00100', '00100', '01110'],
    '2': ['01110', '10001', '00001', '00110', '01000', '10000', '11111'],
    '3': ['11110', '00001', '00001', '01110', '00001', '00001', '11110'],
    '4': ['00010', '00110', '01010', '10010', '11111', '00010', '00010'],
    '5': ['11111', '10000', '11110', '00001', '00001', '10001', '01110'],
    '6': ['01110', '10000', '11110', '10001', '10001', '10001', '01110'],
    '7': ['11111', '00001', '00010', '00100', '01000', '01000', '01000'],
    '8': ['01110', '10001', '10001', '01110', '10001', '10001', '01110'],
    '9': ['01110', '10001', '10001', '01111', '00001', '00010', '01100'],
    '!': ['00100', '00100', '00100', '00100', '00100', '00000', '00100'],
    '?': ['01110', '10001', '00010', '00100', '00100', '00000', '00100'],
    '&': ['01100', '10010', '10100', '01000', '10101', '10010', '01101'],
    '+': ['00000', '00100', '00100', '11111', '00100', '00100', '00000'],
    '-': ['00000', '00000', '00000', '11111', '00000', '00000', '00000'],
    '.': ['00000', '00000', '00000', '00000', '00000', '01100', '01100'],
    ':': ['00000', '00100', '00100', '00000', '00100', '00100', '00000'],
    '★': ['00100', '00100', '11111', '01110', '01010', '10001', '00000'],
    '♥': ['01010', '11111', '11111', '11111', '01110', '00100', '00000'],
    ' ': ['00000', '00000', '00000', '00000', '00000', '00000', '00000']
};

function Hash(N, K)
{
    const V = Math.sin(N * 12.9898 + K * 78.233) * 43758.5453;
    return V - Math.floor(V);
}

const PALETTES = [
    // 0: Champagne Gold & Crimson Imperial
    [[1.0, 0.86, 0.34], [1.0, 0.24, 0.34], [1.0, 0.96, 0.78]],
    // 1: Electric Sapphire & Arctic Cyan
    [[0.24, 0.66, 1.0], [0.58, 0.96, 1.0], [1.0, 0.88, 0.48]],
    // 2: Neon Magenta & Violet Gold
    [[1.0, 0.22, 0.76], [0.68, 0.32, 1.0], [1.0, 0.92, 0.42]],
    // 3: Emerald & Champagne Willow
    [[0.18, 0.98, 0.56], [1.0, 0.84, 0.30], [0.76, 1.0, 0.86]],
    // 4: Ruby Sunset & Diamond White
    [[1.0, 0.36, 0.14], [1.0, 0.74, 0.22], [1.0, 0.98, 0.92]],
    // 5: Turquoise & Rose Gold
    [[0.14, 0.94, 0.88], [1.0, 0.46, 0.62], [1.0, 0.95, 0.65]]
];

// Grand Finale sequence: Text -> Heart -> Saturn -> 5-Point Star -> Smiley -> Crown -> Butterfly -> Willow Bouquet
const FINALE_SEQUENCE = [1, 2, 5, 3, 4, 6, 7, 0];

function WrapTextLines(RawText)
{
    const Clean = String(RawText || '2026').trim().toUpperCase().slice(0, 28) || '2026';
    if (Clean.length <= 8) return [Clean];
    const Words = Clean.split(/\s+/);
    const Lines = [];
    let Cur = '';
    for (const W of Words)
    {
        if (!Cur) Cur = W;
        else if ((Cur.length + 1 + W.length) <= 9) Cur += ' ' + W;
        else { Lines.push(Cur); Cur = W; }
    }
    if (Cur) Lines.push(Cur);
    return Lines.slice(0, 3);
}

function SampleTextPoints(Text, Count, ShellIdx)
{
    const Lines = WrapTextLines(Text);
    const Pixels = [];
    const LineGap = 9.0;
    const TotalH = Lines.length * 7 + (Lines.length - 1) * 2;
    let MaxW = 1;
    for (const L of Lines) MaxW = Math.max(MaxW, L.length * 6 - 1);

    for (let Li = 0; Li < Lines.length; Li++)
    {
        const Line = Lines[Li];
        const LW = Line.length * 6 - 1;
        const YOff = (TotalH * 0.5) - Li * LineGap;
        for (let Ci = 0; Ci < Line.length; Ci++)
        {
            const Ch = Line[Ci];
            const Glyph = FONT_5X7[Ch] || FONT_5X7['★'];
            const XOff = -LW * 0.5 + Ci * 6;
            for (let Row = 0; Row < 7; Row++)
            {
                for (let Col = 0; Col < 5; Col++)
                {
                    if (Glyph[Row][Col] === '1')
                    {
                        Pixels.push({
                            px: XOff + Col + 0.5,
                            py: YOff - Row - 0.5,
                            ci: Ci,
                            li: Li,
                            u: (XOff + Col) / Math.max(1, MaxW) + 0.5
                        });
                    }
                }
            }
        }
    }
    if (Pixels.length === 0) Pixels.push({ px: 0, py: 0, ci: 0, li: 0, u: 0.5 });

    const NormScale = 2.0 / Math.max(MaxW, TotalH * 1.15, 11);
    const Pal = PALETTES[Math.abs(ShellIdx) % PALETTES.length];
    const Out = [];
    // Reserve ~82% for ultra-crisp letter stars + ~18% for festive framing ring sparkles!
    const TextN = Math.floor(Count * 0.82);
    for (let I = 0; I < Count; I++)
    {
        if (I < TextN)
        {
            const Pix = Pixels[I % Pixels.length];
            const Jx = (Hash(I * 17 + ShellIdx * 31, 1) - 0.5) * 0.42;
            const Jy = (Hash(I * 17 + ShellIdx * 31, 2) - 0.5) * 0.42;
            const Jz = (Hash(I * 17 + ShellIdx * 31, 3) - 0.5) * 0.14;
            const CBlend = (Pix.ci % 2 === 0) ? Pal[0] : Pal[1];
            const Bright = (I % 5 === 0) ? Pal[2] : CBlend;
            Out.push({
                x: (Pix.px + Jx) * NormScale,
                y: (Pix.py + Jy) * NormScale,
                z: Jz,
                r: Bright[0], g: Bright[1], b: Bright[2],
                role: 1, // crisp pattern star
                delay: 0.015 * Hash(I, 4)
            });
        }
        else
        {
            // Outer festive sparkling diamond halo framing the sky text
            const U = (I - TextN) / Math.max(1, Count - TextN);
            const Ang = U * Math.PI * 2 + ShellIdx * 0.4;
            const Rad = 1.12 + 0.16 * Hash(I, 5);
            const Spark = Pal[2];
            Out.push({
                x: Math.cos(Ang) * Rad * 1.18,
                y: Math.sin(Ang) * Rad * 0.72,
                z: (Hash(I, 6) - 0.5) * 0.45,
                r: Spark[0], g: Spark[1], b: Spark[2],
                role: 2, // halo sparkle
                delay: 0.04 + 0.06 * Hash(I, 7)
            });
        }
    }
    return { points: Out, isWillow: false, hasStrobe: true };
}

function SampleShapePoints(ShapeId, Count, ShellIdx)
{
    const Pal = PALETTES[Math.abs(ShellIdx) % PALETTES.length];
    const Out = [];

    if (ShapeId === 0)
    {
        // Grand Multi-Shell Bouquet: cycles Chrysanthemum + Pistil, Golden Willow, Brocade Palm, Crossette Strobe
        const Style = Math.abs(ShellIdx) % 4;
        const IsWillow = Style === 1 || Style === 2;
        const OuterN = Math.floor(Count * 0.68);
        const CoreN = Math.floor(Count * 0.24);
        for (let I = 0; I < Count; I++)
        {
            // Fibonacci sphere uniform shell + stylized shell layers
            if (I < OuterN)
            {
                const K = I + 0.5;
                const Phi = Math.acos(1 - (2 * K) / OuterN);
                const Theta = Math.PI * (1 + Math.sqrt(5)) * K + ShellIdx * 1.3;
                let R = 0.92 + 0.16 * Hash(I + ShellIdx * 13, 1);
                if (Style === 2)
                {
                    // Brocade Palm: 14 heavy curving palm fronds
                    const Frond = I % 14;
                    const FPhi = Math.acos(1 - (2 * (Frond + 0.5)) / 14);
                    const FTheta = Frond * 2.3999632 + ShellIdx;
                    const Frac = ((Math.floor(I / 14) + 0.5) / Math.max(1, Math.floor(OuterN / 14)));
                    const Spread = 0.09;
                    const Jx = (Hash(I, 2) - 0.5) * Spread, Jy = (Hash(I, 3) - 0.5) * Spread, Jz = (Hash(I, 4) - 0.5) * Spread;
                    const RadP = 0.25 + 0.85 * Frac;
                    Out.push({
                        x: Math.sin(FPhi) * Math.cos(FTheta) * RadP + Jx,
                        y: Math.cos(FPhi) * RadP + Jy,
                        z: Math.sin(FPhi) * Math.sin(FTheta) * RadP + Jz,
                        r: 1.0, g: 0.82 - 0.18 * Frac, b: 0.28 + 0.25 * (1 - Frac),
                        role: 1, delay: 0.02 * Frac
                    });
                    continue;
                }
                if (Style === 3)
                {
                    // Double-Ring Crossette with clustered starlets
                    const Clust = I % 24;
                    const CPhi = Math.acos(1 - (2 * (Clust + 0.5)) / 24);
                    const CTheta = Clust * 2.3999632;
                    const Off = 0.14;
                    const Cx = Math.sin(CPhi) * Math.cos(CTheta) * 0.92;
                    const Cy = Math.cos(CPhi) * 0.92;
                    const Cz = Math.sin(CPhi) * Math.sin(CTheta) * 0.92;
                    const Col = (Clust % 2 === 0) ? Pal[0] : Pal[1];
                    Out.push({
                        x: Cx + (Hash(I, 5) - 0.5) * Off * 2,
                        y: Cy + (Hash(I, 6) - 0.5) * Off * 2,
                        z: Cz + (Hash(I, 7) - 0.5) * Off * 2,
                        r: Col[0], g: Col[1], b: Col[2],
                        role: 1, delay: 0.04 * Hash(I, 8)
                    });
                    continue;
                }
                const Col = Style === 1 ? [1.0, 0.84, 0.32] : ((I % 3 === 0) ? Pal[2] : Pal[0]);
                Out.push({
                    x: Math.sin(Phi) * Math.cos(Theta) * R,
                    y: Math.cos(Phi) * R,
                    z: Math.sin(Phi) * Math.sin(Theta) * R,
                    r: Col[0], g: Col[1], b: Col[2],
                    role: 1, delay: 0.015 * Hash(I, 2)
                });
            }
            else if (I < OuterN + CoreN)
            {
                // Contrasting Inner Pistil Core Sphere
                const J = I - OuterN;
                const Phi = Math.acos(1 - (2 * (J + 0.5)) / CoreN);
                const Theta = Math.PI * (1 + Math.sqrt(5)) * J;
                const R = 0.44 + 0.08 * Hash(I, 3);
                const Col = Pal[1];
                Out.push({
                    x: Math.sin(Phi) * Math.cos(Theta) * R,
                    y: Math.cos(Phi) * R,
                    z: Math.sin(Phi) * Math.sin(Theta) * R,
                    r: Col[0], g: Col[1], b: Col[2],
                    role: 2, delay: 0.01
                });
            }
            else
            {
                // Equatorial Saturnian sparkle ring accents
                const U = (I - OuterN - CoreN) / Math.max(1, Count - OuterN - CoreN);
                const Ang = U * Math.PI * 2;
                const R = 1.06 + 0.08 * Hash(I, 4);
                Out.push({
                    x: Math.cos(Ang) * R,
                    y: (Hash(I, 5) - 0.5) * 0.14,
                    z: Math.sin(Ang) * R,
                    r: Pal[2][0], g: Pal[2][1], b: Pal[2][2],
                    role: 2, delay: 0.03
                });
            }
        }
        return { points: Out, isWillow: IsWillow, hasStrobe: Style === 0 || Style === 3 };
    }

    if (ShapeId === 2)
    {
        // Glowing Double-Contour 3D Heart + Inner Gold Sparkles
        const RimN = Math.floor(Count * 0.72);
        for (let I = 0; I < Count; I++)
        {
            if (I < RimN)
            {
                const T = (I / RimN) * Math.PI * 2;
                const Layer = (I % 3 === 0) ? 0.76 : 1.0;
                const Hx = 16 * Math.pow(Math.sin(T), 3) / 16.5;
                const Hy = (13 * Math.cos(T) - 5 * Math.cos(2 * T) - 2 * Math.cos(3 * T) - Math.cos(4 * T)) / 16.5 + 0.12;
                const Col = Layer < 0.9 ? [1.0, 0.82, 0.42] : [1.0, 0.18 + 0.14 * Math.sin(T * 3), 0.44];
                Out.push({
                    x: Hx * Layer + (Hash(I, 1) - 0.5) * 0.05,
                    y: Hy * Layer + (Hash(I, 2) - 0.5) * 0.05,
                    z: (Hash(I, 3) - 0.5) * 0.18,
                    r: Col[0], g: Col[1], b: Col[2], role: 1, delay: 0.015 * Hash(I, 4)
                });
            }
            else
            {
                const T = Hash(I, 5) * Math.PI * 2;
                const Sc = 0.25 + 0.55 * Math.sqrt(Hash(I, 6));
                const Hx = 16 * Math.pow(Math.sin(T), 3) / 16.5;
                const Hy = (13 * Math.cos(T) - 5 * Math.cos(2 * T) - 2 * Math.cos(3 * T) - Math.cos(4 * T)) / 16.5 + 0.12;
                Out.push({
                    x: Hx * Sc, y: Hy * Sc, z: (Hash(I, 7) - 0.5) * 0.25,
                    r: 1.0, g: 0.92, b: 0.72, role: 2, delay: 0.03 * Hash(I, 8)
                });
            }
        }
        return { points: Out, isWillow: false, hasStrobe: true };
    }

    if (ShapeId === 3)
    {
        // Crisp 5-Point Luminous Star + Inner Star + Radiating Halo
        const Verts = [];
        for (let K = 0; K < 10; K++)
        {
            const Ang = Math.PI * 0.5 + (K / 10) * Math.PI * 2;
            const R = (K % 2 === 0) ? 1.05 : 0.44;
            Verts.push([Math.cos(Ang) * R, Math.sin(Ang) * R]);
        }
        const RimN = Math.floor(Count * 0.76);
        for (let I = 0; I < Count; I++)
        {
            if (I < RimN)
            {
                const Seg = I % 10;
                const U = Hash(I, 1);
                const A = Verts[Seg], B = Verts[(Seg + 1) % 10];
                const Layer = (I % 4 === 0) ? 0.58 : 1.0;
                const Col = Layer < 0.8 ? [0.36, 0.92, 1.0] : [1.0, 0.88, 0.30];
                Out.push({
                    x: (A[0] + (B[0] - A[0]) * U) * Layer + (Hash(I, 2) - 0.5) * 0.04,
                    y: (A[1] + (B[1] - A[1]) * U) * Layer + (Hash(I, 3) - 0.5) * 0.04,
                    z: (Hash(I, 4) - 0.5) * 0.16,
                    r: Col[0], g: Col[1], b: Col[2], role: 1, delay: 0.01
                });
            }
            else
            {
                const Ang = Hash(I, 5) * Math.PI * 2;
                const R = 1.15 + 0.14 * Hash(I, 6);
                Out.push({
                    x: Math.cos(Ang) * R, y: Math.sin(Ang) * R, z: (Hash(I, 7) - 0.5) * 0.32,
                    r: 1.0, g: 0.98, b: 0.82, role: 2, delay: 0.04
                });
            }
        }
        return { points: Out, isWillow: false, hasStrobe: true };
    }

    if (ShapeId === 4)
    {
        // Classic Japanese Hanabi Smiley Face (bright yellow circle + glowing cyan/white eyes + cheerful smile)
        const FaceN = Math.floor(Count * 0.52);
        const EyeN = Math.floor(Count * 0.18);
        for (let I = 0; I < Count; I++)
        {
            if (I < FaceN)
            {
                const T = (I / FaceN) * Math.PI * 2;
                const R = 0.96 + (Hash(I, 1) - 0.5) * 0.07;
                Out.push({
                    x: Math.cos(T) * R, y: Math.sin(T) * R, z: (Hash(I, 2) - 0.5) * 0.12,
                    r: 1.0, g: 0.88, b: 0.22, role: 1, delay: 0.01
                });
            }
            else if (I < FaceN + EyeN)
            {
                const Side = (I % 2 === 0) ? -1 : 1;
                const A = Hash(I, 3) * Math.PI * 2;
                const R = Math.sqrt(Hash(I, 4)) * 0.14;
                Out.push({
                    x: Side * 0.35 + Math.cos(A) * R, y: 0.30 + Math.sin(A) * R, z: (Hash(I, 5) - 0.5) * 0.10,
                    r: 0.42, g: 0.95, b: 1.0, role: 1, delay: 0.01
                });
            }
            else
            {
                // Wide cheerful smile arc (-150 deg .. -30 deg)
                const U = (I - FaceN - EyeN) / Math.max(1, Count - FaceN - EyeN);
                const Ang = -Math.PI * 0.84 + U * (Math.PI * 0.68);
                const R = 0.55 + (Hash(I, 6) - 0.5) * 0.06;
                Out.push({
                    x: Math.cos(Ang) * R, y: Math.sin(Ang) * R - 0.02, z: (Hash(I, 7) - 0.5) * 0.10,
                    r: 1.0, g: 0.34, b: 0.42, role: 1, delay: 0.01
                });
            }
        }
        return { points: Out, isWillow: false, hasStrobe: true };
    }

    if (ShapeId === 5)
    {
        // 3D Saturn Ringed Planet (Amber-Gold Sphere + Tilted Icy-Cyan Double Ring)
        const CoreN = Math.floor(Count * 0.46);
        const Tilt = 0.42;
        const Cs = Math.cos(Tilt), Sn = Math.sin(Tilt);
        for (let I = 0; I < Count; I++)
        {
            if (I < CoreN)
            {
                const K = I + 0.5;
                const Phi = Math.acos(1 - (2 * K) / CoreN);
                const Theta = Math.PI * (1 + Math.sqrt(5)) * K;
                const R = 0.50 + 0.05 * Hash(I, 1);
                Out.push({
                    x: Math.sin(Phi) * Math.cos(Theta) * R,
                    y: Math.cos(Phi) * R,
                    z: Math.sin(Phi) * Math.sin(Theta) * R,
                    r: 1.0, g: 0.72 + 0.18 * Math.cos(Phi), b: 0.22,
                    role: 1, delay: 0.01
                });
            }
            else
            {
                const J = I - CoreN;
                const Ang = (J / Math.max(1, Count - CoreN)) * Math.PI * 2;
                const Band = (J % 3 === 0) ? 1.18 : (0.86 + 0.22 * Hash(I, 2));
                const Rx = Math.cos(Ang) * Band;
                const Rz = Math.sin(Ang) * Band;
                const Ry = (Hash(I, 3) - 0.5) * 0.04;
                // Tilt ring gracefully in 3D sky space
                const Xr = Rx * Cs - Ry * Sn;
                const Yr = Rx * Sn + Ry * Cs + Rz * 0.22;
                const Zr = Rz * 0.92;
                const Col = (J % 3 === 0) ? [0.95, 0.98, 1.0] : [0.24, 0.88, 1.0];
                Out.push({
                    x: Xr, y: Yr, z: Zr,
                    r: Col[0], g: Col[1], b: Col[2], role: 1, delay: 0.02
                });
            }
        }
        return { points: Out, isWillow: false, hasStrobe: true };
    }

    if (ShapeId === 6)
    {
        // Royal 3-Peak Gold Crown with Ruby/Diamond Jewel Tips
        const Poly = [
            [-0.86, -0.52], [0.86, -0.52], [0.96, 0.44], [0.46, -0.02],
            [0.0, 0.78], [-0.46, -0.02], [-0.96, 0.44], [-0.86, -0.52]
        ];
        const Jewels = [[0.0, 0.86], [-0.98, 0.52], [0.98, 0.52]];
        const RimN = Math.floor(Count * 0.80);
        for (let I = 0; I < Count; I++)
        {
            if (I < RimN)
            {
                const Seg = I % (Poly.length - 1);
                const U = Hash(I, 1);
                const A = Poly[Seg], B = Poly[Seg + 1];
                Out.push({
                    x: A[0] + (B[0] - A[0]) * U + (Hash(I, 2) - 0.5) * 0.05,
                    y: A[1] + (B[1] - A[1]) * U + (Hash(I, 3) - 0.5) * 0.05,
                    z: (Hash(I, 4) - 0.5) * 0.16,
                    r: 1.0, g: 0.84, b: 0.24, role: 1, delay: 0.01
                });
            }
            else
            {
                const J = Jewels[I % 3];
                const A = Hash(I, 5) * Math.PI * 2;
                const R = Math.sqrt(Hash(I, 6)) * 0.14;
                const Col = (I % 3 === 0) ? [0.42, 0.94, 1.0] : [1.0, 0.22, 0.36];
                Out.push({
                    x: J[0] + Math.cos(A) * R, y: J[1] + Math.sin(A) * R, z: (Hash(I, 7) - 0.5) * 0.14,
                    r: Col[0], g: Col[1], b: Col[2], role: 2, delay: 0.02
                });
            }
        }
        return { points: Out, isWillow: false, hasStrobe: true };
    }

    if (ShapeId === 7)
    {
        // Iridescent Symmetrical Butterfly Curve
        for (let I = 0; I < Count; I++)
        {
            const T = (I / Count) * Math.PI * 2;
            const RRaw = Math.exp(Math.cos(T)) - 2 * Math.cos(4 * T) + Math.pow(Math.sin(T / 12), 5);
            const Scale = (I % 5 === 0) ? 0.65 : 1.0;
            const Bx = Math.sin(T) * RRaw * 0.27 * Scale;
            const By = Math.cos(T) * RRaw * 0.27 * Scale;
            const Col = Scale < 0.8 ? [1.0, 0.90, 0.42] : (Math.abs(Bx) > 0.45 ? [0.22, 0.92, 1.0] : [0.88, 0.32, 1.0]);
            Out.push({
                x: Bx + (Hash(I, 1) - 0.5) * 0.04,
                y: By + (Hash(I, 2) - 0.5) * 0.04,
                z: Math.abs(Bx) * 0.24 + (Hash(I, 3) - 0.5) * 0.10,
                r: Col[0], g: Col[1], b: Col[2], role: 1, delay: 0.015 * Hash(I, 4)
            });
        }
        return { points: Out, isWillow: false, hasStrobe: true };
    }

    // ShapeId === 8: Twin Spiral Galaxy
    for (let I = 0; I < Count; I++)
    {
        const Arm = I % 2;
        const U = (I / Count);
        const Ang = U * Math.PI * 5.2 + Arm * Math.PI;
        const R = 0.08 + Math.pow(U, 0.78) * 1.04;
        const Col = U < 0.25 ? [1.0, 0.96, 0.78] : (Arm === 0 ? Pal[0] : Pal[1]);
        Out.push({
            x: Math.cos(Ang) * R + (Hash(I, 1) - 0.5) * 0.06,
            y: Math.sin(Ang) * R + (Hash(I, 2) - 0.5) * 0.06,
            z: (Hash(I, 3) - 0.5) * 0.22,
            r: Col[0], g: Col[1], b: Col[2], role: 1, delay: 0.02 * U
        });
    }
    return { points: Out, isWillow: true, hasStrobe: true };
}

export function SampleFireworkShell(ShapeMode, Text, Count, ShellIdx = 0)
{
    const RawMode = Math.max(0, Math.min(9, Math.round(ShapeMode || 0)));
    const ActiveMode = RawMode === 9
        ? FINALE_SEQUENCE[Math.abs(ShellIdx) % FINALE_SEQUENCE.length]
        : RawMode;
    const Res = ActiveMode === 1
        ? SampleTextPoints(Text || '2026', Count, ShellIdx)
        : SampleShapePoints(ActiveMode, Count, ShellIdx);
    const Meta = FIREWORK_SHAPES.find(S => S.id === ActiveMode) || FIREWORK_SHAPES[0];
    return {
        activeMode: ActiveMode,
        shellName: ActiveMode === 1 ? `Text "${String(Text || '2026').trim().toUpperCase().slice(0, 18)}"` : Meta.label,
        points: Res.points,
        isWillow: !!Res.isWillow,
        hasStrobe: !!Res.hasStrobe
    };
}

// Pack pattern into WebGPU snap[] Part buffer (20 floats per particle):
//   p = [tx, ty, tz, role], v = [0, 0, 0, 1], c = [r, g, b, 1], m = [delay, isWillow, hasStrobe, activeMode], e = [1, 0, 0, 1]
export function BuildFireworkSnapBuffer(ShapeMode, Text, Cap, ShellIdx = 0)
{
    const SafeCap = Math.max(64, Math.floor(Cap || 1200));
    const SafeText = String(Text || '2026').trim().toUpperCase().slice(0, 28) || '2026';
    const Sample = SampleFireworkShell(ShapeMode, SafeText, SafeCap, ShellIdx);
    const Buf = new Float32Array(SafeCap * 20);
    for (let I = 0; I < SafeCap; I++)
    {
        const Pt = Sample.points[I % Sample.points.length];
        const O = I * 20;
        Buf[O + 0] = Pt.x;
        Buf[O + 1] = Pt.y;
        Buf[O + 2] = Pt.z;
        Buf[O + 3] = Pt.role || 1;
        Buf[O + 4] = 0;
        Buf[O + 5] = 0;
        Buf[O + 6] = 0;
        Buf[O + 7] = 1;
        Buf[O + 8] = Pt.r;
        Buf[O + 9] = Pt.g;
        Buf[O + 10] = Pt.b;
        Buf[O + 11] = 1;
        Buf[O + 12] = Pt.delay || 0;
        Buf[O + 13] = Sample.isWillow ? 1 : 0;
        Buf[O + 14] = Sample.hasStrobe ? 1 : 0;
        Buf[O + 15] = Sample.activeMode;
        Buf[O + 16] = 1;
        Buf[O + 17] = 0;
        Buf[O + 18] = 0;
        Buf[O + 19] = 1;
    }
    return {
        snapBuffer: Buf,
        key: `fw:${ShapeMode}:${SafeText}:${SafeCap}:${ShellIdx}`,
        shellName: Sample.shellName,
        activeMode: Sample.activeMode
    };
}

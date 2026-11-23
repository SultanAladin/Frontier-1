//============================================================================================================================================
//                                                           FIELDSPECIFICATION.JS                                                            
//============================================================================================================================================
// 🧩 Code-declared deterministic fields exposed to channel pickers through one generator registry.

const Clamp = (Value) => Math.max(0, Math.min(1, Value));

function HashCell(Column, Row, Seed)
{
    const Value = Math.sin(Column * 127.1 + Row * 311.7 + Seed * 17.13) * 43758.5453;
    return Value - Math.floor(Value);
}

function SmoothNoise(Horizontal, Vertical, Seed)
{
    const Column = Math.floor(Horizontal);
    const Row = Math.floor(Vertical);
    const FractionX = Horizontal - Column;
    const FractionY = Vertical - Row;
    const BlendX = FractionX * FractionX * (3 - 2 * FractionX);
    const BlendY = FractionY * FractionY * (3 - 2 * FractionY);
    const Top = HashCell(Column, Row, Seed) * (1 - BlendX) + HashCell(Column + 1, Row, Seed) * BlendX;
    const Bottom = HashCell(Column, Row + 1, Seed) * (1 - BlendX) + HashCell(Column + 1, Row + 1, Seed) * BlendX;
    return Top * (1 - BlendY) + Bottom * BlendY;
}

export const CamouflagePalette = Object.freeze(["#b5a477", "#566044", "#514334", "#232921"]);

function SampleCamouflage(Source, Horizontal, Vertical)
{
    // 📝 Periodic coordinate warping keeps opposite tile edges identical without image assets.
    const Frequency = Math.max(1, Math.round(Source.scale)), Seed = Source.seed;
    const Periodic = (Horizontal, Vertical, Period, Salt) =>
    {
        const Column = Math.floor(Horizontal), Row = Math.floor(Vertical), FractionX = Horizontal - Column, FractionY = Vertical - Row;
        const BlendX = FractionX * FractionX * (3 - 2 * FractionX), BlendY = FractionY * FractionY * (3 - 2 * FractionY);
        const Cell = (Column, Row) => HashCell(((Column % Period) + Period) % Period, ((Row % Period) + Period) % Period, Salt);
        return (Cell(Column, Row) * (1 - BlendX) + Cell(Column + 1, Row) * BlendX) * (1 - BlendY) + (Cell(Column, Row + 1) * (1 - BlendX) + Cell(Column + 1, Row + 1) * BlendX) * BlendY;
    };
    const Grid = 256 / Math.max(1, Source.pixelSize ?? 4);
    const PositionX = (Source.smooth ? Horizontal + .5 : Math.floor((Horizontal + .5) * Grid) / Grid) * Frequency;
    const PositionY = (Source.smooth ? Vertical + .5 : Math.floor((Vertical + .5) * Grid) / Grid) * Frequency;
    const WarpedX = PositionX + (Periodic(PositionX, PositionY, Frequency, Seed + 17) - .5) * 2.4;
    const WarpedY = PositionY + (Periodic(PositionX, PositionY, Frequency, Seed + 71) - .5) * 2.4;
    const Field = Periodic(WarpedX, WarpedY, Frequency, Seed) * .78 + Periodic(WarpedX * 3, WarpedY * 3, Frequency * 3, Seed + 43) * .22;
    const Count = Math.max(2, Math.min(8, Source.colourCount || 4));
    let Band = 0;
    for (let Index = 0; Index < Count - 1; ++Index)
    {
        const Threshold = Count === 2 ? .5 : .36 + Index * .28 / (Count - 2);
        const Blend = Clamp((Field - Threshold) / Math.max(.001, Source.softness ?? .012) + .5);
        Band += Source.smooth ? Blend * Blend * (3 - 2 * Blend) : Number(Field >= Threshold);
    }
    return Band / (Count - 1);
}

function SamplePattern(Mode, Source, RotatedX, RotatedY)
{
    if (Mode === "camouflage" || Mode === "camouflage-smooth") return SampleCamouflage({ ...Source, smooth: Mode === "camouflage-smooth" }, RotatedX, RotatedY);
    const Frequency = Source.scale;
    const PositionX = (RotatedX + 0.5) * Frequency;
    const PositionY = (RotatedY + 0.5) * Frequency;
    if (Mode === 'stripes') return 0.5 + 0.5 * Math.sin(PositionY * Math.PI * 2 + Source.seed);
    if (Mode === 'checker') return Math.abs((Math.floor(PositionX) + Math.floor(PositionY) + Source.seed) % 2);
    if (Mode === 'cells')
    {
        const Column = Math.floor(PositionX);
        const Row = Math.floor(PositionY);
        let Distance = 2;
        for (let OffsetY = -1; OffsetY <= 1; ++OffsetY) for (let OffsetX = -1; OffsetX <= 1; ++OffsetX)
        {
            const CellX = Column + OffsetX;
            const CellY = Row + OffsetY;
            const CenterX = CellX + HashCell(CellX, CellY, Source.seed);
            const CenterY = CellY + HashCell(CellX, CellY, Source.seed + 31);
            Distance = Math.min(Distance, Math.hypot(PositionX - CenterX, PositionY - CenterY));
        }
        return Clamp(Distance);
    }
    let Value = 0;
    let Amplitude = 0.5;
    let Sum = 0;
    for (let Octave = 0; Octave < 4; ++Octave)
    {
        Value += SmoothNoise(PositionX * 2 ** Octave, PositionY * 2 ** Octave, Source.seed + Octave) * Amplitude;
        Sum += Amplitude;
        Amplitude *= 0.5;
    }
    const Fractal = Value / Sum;
    if (Mode === 'ridged') return 1 - Math.abs(Fractal * 2 - 1);
    if (Mode === 'rings') return 0.5 + 0.5 * Math.sin(Math.hypot(RotatedX, RotatedY) * Frequency * Math.PI * 2 + Fractal * 2 + Source.seed);
    return Fractal;
}

// 📝 Add a stable identity, label and pure Sample(Source, Horizontal, Vertical) function here.
// 📝 Coordinates are centred and rotated; samples must be finite and normalized to [0, 1].
// 📝 Only identities and settings are serialized. Imported material files never execute code.
export const GeneratorDefinitions = Object.freeze([
    { identity: 'camouflage-smooth', label: 'Military camouflage · Smooth', description: 'Organic flowing patches with smooth boundaries and an editable palette.' },
    { identity: 'camouflage', label: 'Military camouflage · Pixelated', description: 'Seamless four-colour, domain-warped woodland-style patches; not an official uniform pattern.' },
    { identity: 'noise', label: 'Fractal noise', description: 'Four-octave smooth value noise.' },
    { identity: 'stripes', label: 'Directional grain', description: 'Periodic directional bands.' },
    { identity: 'checker', label: 'Checker tiles', description: 'Alternating square cells.' },
    { identity: 'cells', label: 'Cellular / Voronoi', description: 'Distance to seeded cell centres.' },
    { identity: 'ridged', label: 'Ridged fractal', description: 'Folded fractal ridges.' },
    { identity: 'rings', label: 'Growth rings', description: 'Concentric rings perturbed by a fractal field.' }
].map(Declaration => Object.freeze({
    ...Declaration,
    Sample: Declaration.Sample ?? ((Source, Horizontal, Vertical) => SamplePattern(Declaration.identity, Source, Horizontal, Vertical))
})));

const GeneratorIndex = new Map(GeneratorDefinitions.map(Definition => [Definition.identity, Definition]));

export function SampleGenerator(Identity, Source, Horizontal, Vertical)
{
    const Definition = GeneratorIndex.get(Identity);
    if (!Definition) return 0;
    const Value = Definition.Sample(Source, Horizontal, Vertical);
    return Number.isFinite(Value) ? Clamp(Value) : 0;
}

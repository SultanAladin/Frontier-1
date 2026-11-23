/*====================================================================================================================================
                                                    BLUENOISECONSTRUCTION.MJS
====================================================================================================================================*/
// 🧩 Builds Assets/BlueNoiseRanks.bin: two 64 × 64 void-and-cluster rank masks (Ulichney 1993) for the blue-noise pattern

// Usage:  node Tools/BlueNoiseConstruction.mjs
// 📝 Offline and committed: the renderer only reads the file, so no floating-point library difference between
//    machines can change it. The construction itself has no random generator — the initial pattern comes from a fixed
//    integer avalanche and every tie is broken by the lowest pixel index.
// 📝 Output: 4096 little-endian u32, row-major; bits 0–11 = rank of mask A, bits 16–27 = rank of mask B (0…4095).

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const MaskEdge         = 64;
const PixelTotal       = MaskEdge * MaskEdge;
const FilterSigma      = 1.9;      // [px] - Ulichney's recommended energy filter width
const InitialFraction  = 0.1;      // [-]  - share of pixels in the initial binary pattern
const OutputPath       = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../Assets/BlueNoiseRanks.bin');


//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Same lowbias32 avalanche as QuadratureSequence.wgsl.
function AvalancheKey(Input)
{
    let Mixed = Input >>> 0;
    Mixed ^= Mixed >>> 16; Mixed = Math.imul(Mixed, 0x7feb352d) >>> 0;
    Mixed ^= Mixed >>> 15; Mixed = Math.imul(Mixed, 0x846ca68b) >>> 0;
    Mixed ^= Mixed >>> 16;
    return Mixed >>> 0;
}

// 📝 Toroidal Gaussian, indexed by wrapped offset (row × edge + column).
function ConstructFilter()
{
    const Filter = new Float64Array(PixelTotal);
    for (let Row = 0; Row < MaskEdge; Row++)
    {
        for (let Column = 0; Column < MaskEdge; Column++)
        {
            const Across = Math.min(Column, MaskEdge - Column), Down = Math.min(Row, MaskEdge - Row);
            Filter[Row * MaskEdge + Column] = Math.exp(-(Across * Across + Down * Down) / (2 * FilterSigma * FilterSigma));
        }
    }
    return Filter;
}

class EnergyField
{
    constructor(Filter, Pattern)
    {
        this.Filter  = Filter;
        this.Pattern = Pattern;
        this.Energy  = new Float64Array(PixelTotal);
        for (let Pixel = 0; Pixel < PixelTotal; Pixel++) if (Pattern[Pixel]) this.Deposit(Pixel, 1);
    }

    Deposit(Pixel, Sign)
    {
        const Row = Math.floor(Pixel / MaskEdge), Column = Pixel % MaskEdge;
        for (let Target = 0; Target < PixelTotal; Target++)
        {
            const DeltaRow    = (Math.floor(Target / MaskEdge) - Row + MaskEdge) % MaskEdge;
            const DeltaColumn = (Target % MaskEdge - Column + MaskEdge) % MaskEdge;
            this.Energy[Target] += Sign * this.Filter[DeltaRow * MaskEdge + DeltaColumn];
        }
    }

    Toggle(Pixel)
    {
        this.Pattern[Pixel] = this.Pattern[Pixel] ? 0 : 1;
        this.Deposit(Pixel, this.Pattern[Pixel] ? 1 : -1);
    }

    // 📝 Tightest cluster: the set pixel with the highest energy. Largest void: the empty pixel with the lowest.
    LocateTightestCluster()
    {
        let Chosen = -1;
        for (let Pixel = 0; Pixel < PixelTotal; Pixel++) if (this.Pattern[Pixel] && (Chosen < 0 || this.Energy[Pixel] > this.Energy[Chosen])) Chosen = Pixel;
        return Chosen;
    }

    LocateLargestVoid()
    {
        let Chosen = -1;
        for (let Pixel = 0; Pixel < PixelTotal; Pixel++) if (!this.Pattern[Pixel] && (Chosen < 0 || this.Energy[Pixel] < this.Energy[Chosen])) Chosen = Pixel;
        return Chosen;
    }
}

function ConstructRankMask(Filter, Salt)
{
    // ① Initial pattern: the pixels with the smallest avalanche keys, then relaxed until the swap is a no-op.
    const InitialCount = Math.round(PixelTotal * InitialFraction);
    const Order = Array.from({ length: PixelTotal }, (_, Pixel) => Pixel).sort((Left, Right) => (AvalancheKey(Left ^ Salt) - AvalancheKey(Right ^ Salt)) || (Left - Right));
    const Initial = new Uint8Array(PixelTotal);
    for (let Slot = 0; Slot < InitialCount; Slot++) Initial[Order[Slot]] = 1;
    const Relaxed = new EnergyField(Filter, Initial);
    for (;;)
    {
        const Cluster = Relaxed.LocateTightestCluster();
        Relaxed.Toggle(Cluster);
        const Void = Relaxed.LocateLargestVoid();
        if (Void === Cluster) { Relaxed.Toggle(Cluster); break; }
        Relaxed.Toggle(Void);
    }

    const Ranks = new Int32Array(PixelTotal).fill(-1);
    // ② Ranks below the initial count: remove tightest clusters from a copy, highest rank first.
    const Removal = new EnergyField(Filter, Relaxed.Pattern.slice());
    for (let Rank = InitialCount - 1; Rank >= 0; Rank--)
    {
        const Cluster = Removal.LocateTightestCluster();
        Ranks[Cluster] = Rank;
        Removal.Toggle(Cluster);
    }
    // ③ Up to half: fill the largest void.
    const Growth = new EnergyField(Filter, Relaxed.Pattern.slice());
    for (let Rank = InitialCount; Rank < PixelTotal / 2; Rank++)
    {
        const Void = Growth.LocateLargestVoid();
        Ranks[Void] = Rank;
        Growth.Toggle(Void);
    }
    // ④ Upper half: the empty pixels are now the minority, so fill the tightest cluster of empty pixels.
    const Inverted = new EnergyField(Filter, Growth.Pattern.map(Bit => 1 - Bit));
    for (let Rank = PixelTotal / 2; Rank < PixelTotal; Rank++)
    {
        const Cluster = Inverted.LocateTightestCluster();
        Ranks[Cluster] = Rank;
        Inverted.Toggle(Cluster);
    }
    if (Ranks.some(Rank => Rank < 0) || new Set(Ranks).size !== PixelTotal) throw new Error('Rank mask is not a permutation');
    return Ranks;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN
//------------------------------------------------------------------------------------------------------------------------

const Filter = ConstructFilter();
const FirstMask  = ConstructRankMask(Filter, 0x2545f491);
const SecondMask = ConstructRankMask(Filter, 0x9e3779b9);
const Packed = new Uint32Array(PixelTotal);
for (let Pixel = 0; Pixel < PixelTotal; Pixel++) Packed[Pixel] = (FirstMask[Pixel] | (SecondMask[Pixel] << 16)) >>> 0;
fs.writeFileSync(OutputPath, Buffer.from(Packed.buffer));
console.log(`${OutputPath}: ${PixelTotal} pixels, 2 masks`);

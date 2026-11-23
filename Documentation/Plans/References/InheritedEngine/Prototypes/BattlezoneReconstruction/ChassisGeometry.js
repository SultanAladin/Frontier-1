/*====================================================================================================================================
                                                     CHASSISGEOMETRY.JS
====================================================================================================================================*/
// 🧩 Procedural vertex/index construction for chassis parts, ground relief, cover obstacles, projectiles and crates

import { FieldSpecification } from "./CombatSpecification.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                    VERTEX ACCUMULATOR
//------------------------------------------------------------------------------------------------------------------------

// 📝 Vertex footprint is 6 floats: position (3) + normal (3). Tone arrives per-instance, so identical parts on
//    differently-coloured chassis share one geometry region instead of duplicating buffers per class.
export const VertexFloatStride = 6;

class GeometryAccumulator
{
    constructor()
    {
        this.VertexScalars = [];
        this.IndexScalars  = [];
    }

    // Returns the index of the appended vertex so callers can wire their own topology.
    AppendVertex(Position, Normal)
    {
        this.VertexScalars.push(Position[0], Position[1], Position[2], Normal[0], Normal[1], Normal[2]);
        return (this.VertexScalars.length / VertexFloatStride) - 1;
    }

    AppendTriangle(FirstIndex, SecondIndex, ThirdIndex)
    {
        this.IndexScalars.push(FirstIndex, SecondIndex, ThirdIndex);
    }

    // Emit a planar quad as two triangles with one shared face normal — flat shading suits the faceted look.
    AppendQuad(CornerA, CornerB, CornerC, CornerD, FaceNormal)
    {
        const BaseIndex = this.AppendVertex(CornerA, FaceNormal);
        this.AppendVertex(CornerB, FaceNormal);
        this.AppendVertex(CornerC, FaceNormal);
        this.AppendVertex(CornerD, FaceNormal);
        this.AppendTriangle(BaseIndex,     BaseIndex + 1, BaseIndex + 2);
        this.AppendTriangle(BaseIndex + 2, BaseIndex + 3, BaseIndex);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRIMITIVE CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

// Axis-aligned box spanning ±Extent around Offset, six flat-shaded faces.
function ConstructBoxRegion(Accumulator, Offset, Extent)
{
    const MinimumX = Offset[0] - Extent[0], MaximumX = Offset[0] + Extent[0];
    const MinimumY = Offset[1] - Extent[1], MaximumY = Offset[1] + Extent[1];
    const MinimumZ = Offset[2] - Extent[2], MaximumZ = Offset[2] + Extent[2];

    Accumulator.AppendQuad([MinimumX, MinimumY, MaximumZ], [MaximumX, MinimumY, MaximumZ],
                           [MaximumX, MaximumY, MaximumZ], [MinimumX, MaximumY, MaximumZ], [0, 0, 1]);
    Accumulator.AppendQuad([MaximumX, MinimumY, MinimumZ], [MinimumX, MinimumY, MinimumZ],
                           [MinimumX, MaximumY, MinimumZ], [MaximumX, MaximumY, MinimumZ], [0, 0, -1]);
    Accumulator.AppendQuad([MaximumX, MinimumY, MaximumZ], [MaximumX, MinimumY, MinimumZ],
                           [MaximumX, MaximumY, MinimumZ], [MaximumX, MaximumY, MaximumZ], [1, 0, 0]);
    Accumulator.AppendQuad([MinimumX, MinimumY, MinimumZ], [MinimumX, MinimumY, MaximumZ],
                           [MinimumX, MaximumY, MaximumZ], [MinimumX, MaximumY, MinimumZ], [-1, 0, 0]);
    Accumulator.AppendQuad([MinimumX, MaximumY, MaximumZ], [MaximumX, MaximumY, MaximumZ],
                           [MaximumX, MaximumY, MinimumZ], [MinimumX, MaximumY, MinimumZ], [0, 1, 0]);
    Accumulator.AppendQuad([MinimumX, MinimumY, MinimumZ], [MaximumX, MinimumY, MinimumZ],
                           [MaximumX, MinimumY, MaximumZ], [MinimumX, MinimumY, MaximumZ], [0, -1, 0]);
}

// 📝 A hull with a sloped glacis: the +Z face is narrowed and lowered so the front reads as armour rake, which is
//    what separates a tank silhouette from a plain crate at distance.
function ConstructRakedHullRegion(Accumulator, Extent)
{
    const HalfWidth = Extent[0], HalfHeight = Extent[1], HalfDepth = Extent[2];
    const GlacisWidth  = HalfWidth * 0.78;                          // [m] - Narrowed nose width
    const GlacisHeight = HalfHeight * 0.34;                         // [m] - Lowered nose ceiling

    const RearBottomLeft  = [-HalfWidth, -HalfHeight, -HalfDepth];
    const RearBottomRight = [ HalfWidth, -HalfHeight, -HalfDepth];
    const RearTopRight    = [ HalfWidth,  HalfHeight, -HalfDepth];
    const RearTopLeft     = [-HalfWidth,  HalfHeight, -HalfDepth];
    const MidTopLeft      = [-HalfWidth,  HalfHeight,  HalfDepth * 0.30];
    const MidTopRight     = [ HalfWidth,  HalfHeight,  HalfDepth * 0.30];
    const NoseBottomLeft  = [-GlacisWidth, -HalfHeight, HalfDepth];
    const NoseBottomRight = [ GlacisWidth, -HalfHeight, HalfDepth];
    const NoseTopRight    = [ GlacisWidth,  GlacisHeight, HalfDepth];
    const NoseTopLeft     = [-GlacisWidth,  GlacisHeight, HalfDepth];

    const GlacisRise = HalfHeight - GlacisHeight;                   // [m] - Vertical run of the rake
    const GlacisRun  = HalfDepth * 0.70;                            // [m] - Horizontal run of the rake
    const GlacisNormalLength = Math.sqrt(GlacisRise * GlacisRise + GlacisRun * GlacisRun);
    const GlacisNormal = [0, GlacisRun / GlacisNormalLength, GlacisRise / GlacisNormalLength];

    Accumulator.AppendQuad(RearBottomLeft, RearBottomRight, RearTopRight, RearTopLeft, [0, 0, -1]);
    Accumulator.AppendQuad(RearTopLeft, RearTopRight, MidTopRight, MidTopLeft, [0, 1, 0]);
    Accumulator.AppendQuad(MidTopLeft, MidTopRight, NoseTopRight, NoseTopLeft, GlacisNormal);
    Accumulator.AppendQuad(NoseTopLeft, NoseTopRight, NoseBottomRight, NoseBottomLeft, [0, 0, 1]);
    Accumulator.AppendQuad(RearBottomLeft, NoseBottomLeft, NoseBottomRight, RearBottomRight, [0, -1, 0]);

    // Side walls are pentagons; fan them from the rear-bottom corner of each flank.
    const LeftFan = [RearBottomLeft, NoseBottomLeft, NoseTopLeft, MidTopLeft, RearTopLeft];
    const LeftBase = Accumulator.AppendVertex(LeftFan[0], [-1, 0, 0]);
    for (let FanIndex = 1; FanIndex < LeftFan.length; ++FanIndex) Accumulator.AppendVertex(LeftFan[FanIndex], [-1, 0, 0]);
    for (let FanIndex = 1; FanIndex + 1 < LeftFan.length; ++FanIndex)
        Accumulator.AppendTriangle(LeftBase, LeftBase + FanIndex, LeftBase + FanIndex + 1);

    const RightFan = [RearBottomRight, RearTopRight, MidTopRight, NoseTopRight, NoseBottomRight];
    const RightBase = Accumulator.AppendVertex(RightFan[0], [1, 0, 0]);
    for (let FanIndex = 1; FanIndex < RightFan.length; ++FanIndex) Accumulator.AppendVertex(RightFan[FanIndex], [1, 0, 0]);
    for (let FanIndex = 1; FanIndex + 1 < RightFan.length; ++FanIndex)
        Accumulator.AppendTriangle(RightBase, RightBase + FanIndex, RightBase + FanIndex + 1);
}

// Radial drum about the Y axis with flat caps — serves turrets, wheels and projectile bodies.
function ConstructDrumRegion(Accumulator, Offset, Radius, Height, RadialCount)
{
    const LowerY = Offset[1] - Height * 0.5;
    const UpperY = Offset[1] + Height * 0.5;

    for (let RadialIndex = 0; RadialIndex < RadialCount; ++RadialIndex)
    {
        const AngleCurrent = (RadialIndex       / RadialCount) * Math.PI * 2.0;
        const AngleNext    = ((RadialIndex + 1) / RadialCount) * Math.PI * 2.0;
        const CosineCurrent = Math.cos(AngleCurrent), SineCurrent = Math.sin(AngleCurrent);
        const CosineNext    = Math.cos(AngleNext),    SineNext    = Math.sin(AngleNext);

        const CurrentX = Offset[0] + CosineCurrent * Radius, CurrentZ = Offset[2] + SineCurrent * Radius;
        const NextX    = Offset[0] + CosineNext    * Radius, NextZ    = Offset[2] + SineNext    * Radius;

        Accumulator.AppendQuad([CurrentX, LowerY, CurrentZ], [NextX, LowerY, NextZ],
                               [NextX, UpperY, NextZ], [CurrentX, UpperY, CurrentZ],
                               [(CosineCurrent + CosineNext) * 0.5, 0, (SineCurrent + SineNext) * 0.5]);

        const UpperCentre = Accumulator.AppendVertex([Offset[0], UpperY, Offset[2]], [0, 1, 0]);
        Accumulator.AppendVertex([CurrentX, UpperY, CurrentZ], [0, 1, 0]);
        Accumulator.AppendVertex([NextX, UpperY, NextZ], [0, 1, 0]);
        Accumulator.AppendTriangle(UpperCentre, UpperCentre + 1, UpperCentre + 2);

        const LowerCentre = Accumulator.AppendVertex([Offset[0], LowerY, Offset[2]], [0, -1, 0]);
        Accumulator.AppendVertex([NextX, LowerY, NextZ], [0, -1, 0]);
        Accumulator.AppendVertex([CurrentX, LowerY, CurrentZ], [0, -1, 0]);
        Accumulator.AppendTriangle(LowerCentre, LowerCentre + 1, LowerCentre + 2);
    }
}

// Barrel drum laid along +Z rather than +Y, rooted at the origin so turret pitch rotates it about the trunnion.
function ConstructBarrelRegion(Accumulator, Radius, Length, RadialCount)
{
    for (let RadialIndex = 0; RadialIndex < RadialCount; ++RadialIndex)
    {
        const AngleCurrent = (RadialIndex       / RadialCount) * Math.PI * 2.0;
        const AngleNext    = ((RadialIndex + 1) / RadialCount) * Math.PI * 2.0;
        const CosineCurrent = Math.cos(AngleCurrent), SineCurrent = Math.sin(AngleCurrent);
        const CosineNext    = Math.cos(AngleNext),    SineNext    = Math.sin(AngleNext);

        Accumulator.AppendQuad(
            [CosineCurrent * Radius, SineCurrent * Radius, 0.0],
            [CosineNext    * Radius, SineNext    * Radius, 0.0],
            [CosineNext    * Radius, SineNext    * Radius, Length],
            [CosineCurrent * Radius, SineCurrent * Radius, Length],
            [(CosineCurrent + CosineNext) * 0.5, (SineCurrent + SineNext) * 0.5, 0]);
    }

    // Muzzle cap so the bore does not read as an open tube from the front.
    const MuzzleCentre = Accumulator.AppendVertex([0, 0, Length], [0, 0, 1]);
    for (let RadialIndex = 0; RadialIndex <= RadialCount; ++RadialIndex)
    {
        const RadialAngle = (RadialIndex / RadialCount) * Math.PI * 2.0;
        Accumulator.AppendVertex([Math.cos(RadialAngle) * Radius, Math.sin(RadialAngle) * Radius, Length], [0, 0, 1]);
    }
    for (let RadialIndex = 1; RadialIndex <= RadialCount; ++RadialIndex)
        Accumulator.AppendTriangle(MuzzleCentre, MuzzleCentre + RadialIndex, MuzzleCentre + RadialIndex + 1);
}

// Square-based pyramid — the signature obstacle of the original arcade field.
function ConstructPyramidRegion(Accumulator, BaseHalfExtent, ApexHeight)
{
    const CornerSequence = [
        [-BaseHalfExtent, 0, -BaseHalfExtent], [ BaseHalfExtent, 0, -BaseHalfExtent],
        [ BaseHalfExtent, 0,  BaseHalfExtent], [-BaseHalfExtent, 0,  BaseHalfExtent]
    ];
    const ApexPosition = [0, ApexHeight, 0];

    for (let CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
    {
        const CornerCurrent = CornerSequence[CornerIndex];
        const CornerNext    = CornerSequence[(CornerIndex + 1) % 4];
        const EdgeAlong  = [CornerNext[0] - CornerCurrent[0], 0, CornerNext[2] - CornerCurrent[2]];
        const EdgeToApex = [ApexPosition[0] - CornerCurrent[0], ApexHeight, ApexPosition[2] - CornerCurrent[2]];
        const FaceNormal = [
            EdgeAlong[1] * EdgeToApex[2] - EdgeAlong[2] * EdgeToApex[1],
            EdgeAlong[2] * EdgeToApex[0] - EdgeAlong[0] * EdgeToApex[2],
            EdgeAlong[0] * EdgeToApex[1] - EdgeAlong[1] * EdgeToApex[0]
        ];
        const FirstIndex = Accumulator.AppendVertex(CornerCurrent, FaceNormal);
        Accumulator.AppendVertex(CornerNext, FaceNormal);
        Accumulator.AppendVertex(ApexPosition, FaceNormal);
        Accumulator.AppendTriangle(FirstIndex, FirstIndex + 1, FirstIndex + 2);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TERRAIN CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------
//                                                     LUNAR RELIEF FIELD
//------------------------------------------------------------------------------------------------------------------------

// 📝 Value noise over an integer lattice with a quintic fade. Deterministic and closed-form, so the CPU seating query
//    and the tessellated grid always agree without a shared texture.
// 🔴 Every multiply here MUST go through Math.imul and every shift must be unsigned. A plain `*` on these constants
//    reaches ~5e17, past the 2^53 mantissa, so the low bits the shift-xor avalanche depends on are already gone — the
//    hash then collapses toward small outputs, ValueNoise returns ≈ −1 nearly everywhere, and the entire relief field
//    sinks uniformly negative with no visible variation. Math.imul is exact 32-bit wraparound, which is what the
//    avalanche was designed for; `>>>` keeps the sign bit from smearing ones through the high end.
function LatticeHash(LatticeX, LatticeZ)
{
    let HashState = Math.imul(LatticeX, 374761393) + Math.imul(LatticeZ, 668265263) | 0;
    HashState = Math.imul(HashState ^ (HashState >>> 13), 1274126177);
    return ((HashState ^ (HashState >>> 16)) >>> 0) / 4294967296.0;
}

function QuinticFade(Interpolant)
{
    return Interpolant * Interpolant * Interpolant * (Interpolant * (Interpolant * 6.0 - 15.0) + 10.0);
}

function ValueNoise(SampleX, SampleZ)
{
    const LatticeX = Math.floor(SampleX), LatticeZ = Math.floor(SampleZ);
    const FractionX = SampleX - LatticeX,  FractionZ = SampleZ - LatticeZ;
    const WeightX = QuinticFade(FractionX), WeightZ = QuinticFade(FractionZ);

    const CornerNearLeft  = LatticeHash(LatticeX,     LatticeZ);
    const CornerNearRight = LatticeHash(LatticeX + 1, LatticeZ);
    const CornerFarLeft   = LatticeHash(LatticeX,     LatticeZ + 1);
    const CornerFarRight  = LatticeHash(LatticeX + 1, LatticeZ + 1);

    const NearEdge = CornerNearLeft + (CornerNearRight - CornerNearLeft) * WeightX;
    const FarEdge  = CornerFarLeft  + (CornerFarRight  - CornerFarLeft)  * WeightX;
    return (NearEdge + (FarEdge - NearEdge) * WeightZ) * 2.0 - 1.0;
}

// Summed octaves — the regolith's fine undulation riding broad highland swells.
function FractalRelief(SampleX, SampleZ, OctaveCount, BaseFrequency)
{
    let Accumulation = 0.0, OctaveWeight = 1.0, OctaveFrequency = BaseFrequency, WeightTotal = 0.0;
    for (let OctaveIndex = 0; OctaveIndex < OctaveCount; ++OctaveIndex)
    {
        Accumulation += ValueNoise(SampleX * OctaveFrequency, SampleZ * OctaveFrequency) * OctaveWeight;
        WeightTotal  += OctaveWeight;
        OctaveWeight *= 0.5;
        OctaveFrequency *= 2.03;                                    // Slightly off 2.0 to break lattice alignment
    }
    return Accumulation / WeightTotal;
}

// 💡 Craters are the whole character of a lunar plain, and they are NOT noise — each has a depressed bowl, a raised
//    rim at the radius, an ejecta blanket falling off outside it, and (for larger ones) a central rebound peak.
//    Placement is one deterministic pass over a coarse cell lattice: one candidate crater per cell, so overlapping
//    basins stack naturally the way real impact fields do, with no roster to keep in sync.
const CraterCellEdge   = 96.0;   // [m] - Placement lattice spacing; one crater candidate per cell
const CraterCellReach  =    2;   // [-] - Neighbourhood radius in cells (see the sweep note below)
const SecondaryCutWeight = 0.22; // [-] - Weight applied to every excavation except the deepest one

// 🔴 Overlapping craters must COMPOSE, not accumulate. Radii reach 73 m on a 96 m lattice, so most points lie inside
//    several bowls; summing each bowl's full depth drove the plain to −20 m and buried every rim crest inside its
//    neighbour's floor, erasing the crater morphology the field exists to show. Instead the deepest single excavation
//    applies in full, further excavations apply at a reduced weight (so a small pit on a large floor still reads),
//    and every raised feature — rim crest, rebound peak, ejecta blanket — takes the TALLEST rather than the total.
function CraterContribution(WorldX, WorldZ)
{
    const CellIndexX = Math.floor(WorldX / CraterCellEdge);
    const CellIndexZ = Math.floor(WorldZ / CraterCellEdge);

    let DeepestCut  = 0.0;   // [m] - Most negative bowl cut among the overlapping craters
    let TotalCut    = 0.0;   // [m] - Sum of every bowl cut, so the secondaries can be weighted separately
    let TallestLift = 0.0;   // [m] - Tallest raised feature at this point

    // ⚠️ The sweep must reach 2 cells, not 1: a maximum-radius crater's ejecta blanket extends 1.85 x 73 m = 135 m,
    //    while a crater two cells away can sit as close as 110 m. A 3x3 sweep drops it and leaves a visible seam.
    for (let OffsetZ = -CraterCellReach; OffsetZ <= CraterCellReach; ++OffsetZ)
    {
        for (let OffsetX = -CraterCellReach; OffsetX <= CraterCellReach; ++OffsetX)
        {
            const CellX = CellIndexX + OffsetX, CellZ = CellIndexZ + OffsetZ;

            const PresenceSample = LatticeHash(CellX * 7 + 13, CellZ * 11 + 29);
            if (PresenceSample > 0.82) continue;                    // Sparse gaps keep the plain from tiling visibly

            const JitterX = LatticeHash(CellX * 17 + 5,  CellZ * 23 + 41);
            const JitterZ = LatticeHash(CellX * 31 + 71, CellZ * 37 + 7);
            const RadiusSample = LatticeHash(CellX * 53 + 3, CellZ * 59 + 97);

            const CraterOriginX = (CellX + 0.15 + JitterX * 0.70) * CraterCellEdge;
            const CraterOriginZ = (CellZ + 0.15 + JitterZ * 0.70) * CraterCellEdge;

            // Radius distribution skewed small — many modest pits, few large basins, as on a real mare surface.
            const CraterRadius = 11.0 + Math.pow(RadiusSample, 2.4) * 62.0;

            // 🔴 Depth grows SUB-LINEARLY with radius, and getting this wrong breaks more than looks. A fixed
            //    depth:radius ratio makes every basin as steep as a small pit — the parabolic bowl's rim slope is
            //    2·depth/radius, so a constant 0.26 ratio pins every wall at ~0.52 regardless of size. The field then
            //    has no traversable ground and, worse, no line of fire: a 165 m/s shell climbs at ~0.22, so it buries
            //    itself in the slope directly ahead and combat stops working entirely.
            //    Real lunar morphology is the fix — small fresh pits run depth:diameter ≈ 0.2 while large basins
            //    flatten to ≈ 0.05, so the exponent below keeps small craters crisp and large ones broad and drivable.
            const CraterDepth = 0.85 * Math.pow(CraterRadius, 0.52);

            const OffsetToSampleX = WorldX - CraterOriginX;
            const OffsetToSampleZ = WorldZ - CraterOriginZ;
            const RadialDistance  = Math.sqrt(OffsetToSampleX * OffsetToSampleX + OffsetToSampleZ * OffsetToSampleZ);
            const EjectaReach     = CraterRadius * 1.85;
            if (RadialDistance > EjectaReach) continue;

            const RadialFraction = RadialDistance / CraterRadius;   // [-] - 1.0 exactly at the rim crest

            if (RadialFraction < 1.0)
            {
                // Parabolic bowl deepening toward the floor; tracked as a cut so overlaps can compose rather than sum.
                const BowlProfile = RadialFraction * RadialFraction;
                const BowlCut     = (BowlProfile - 1.0) * CraterDepth;
                TotalCut += BowlCut;
                if (BowlCut < DeepestCut) DeepestCut = BowlCut;

                // The rim crest stands above the surrounding plain, sharpest right at the radius.
                const RimLift = Math.pow(BowlProfile, 3.0) * CraterDepth * 0.55;
                if (RimLift > TallestLift) TallestLift = RimLift;

                // Central rebound peak, only in basins large enough to have produced one.
                if (CraterRadius > 34.0 && RadialFraction < 0.26)
                {
                    const PeakProfile = 1.0 - (RadialFraction / 0.26);
                    const PeakLift    = PeakProfile * PeakProfile * CraterDepth * 0.62;
                    if (PeakLift > TallestLift) TallestLift = PeakLift;
                }
            }
            else
            {
                // Ejecta blanket decaying outward from the rim crest.
                const EjectaFraction = (RadialFraction - 1.0) / 0.85;
                const EjectaFalloff  = Math.pow(1.0 - Math.min(EjectaFraction, 1.0), 2.2);
                const EjectaLift     = EjectaFalloff * CraterDepth * 0.30;
                if (EjectaLift > TallestLift) TallestLift = EjectaLift;
            }
        }
    }

    // Deepest cut in full, every other cut at a reduced weight, and the raised features by their tallest.
    const SecondaryCut = (TotalCut - DeepestCut) * SecondaryCutWeight;
    return DeepestCut + SecondaryCut + TallestLift;
}

// 🔴 Ground relief must be evaluated by the identical closed form on CPU and in geometry, or chassis sink through
//    the surface they are standing on. TerrainRelief is the single authority; the grid below only samples it.
export function TerrainRelief(WorldX, WorldZ)
{
    const Amplitude = FieldSpecification.TerrainReliefAmplitude;

    // Broad highland swells and mare basins — the long-wavelength shape the craters are then cut into.
    const HighlandSwell = FractalRelief(WorldX, WorldZ, 4, 0.0022) * Amplitude * 3.4;
    const MareBasin     = FractalRelief(WorldX + 811.0, WorldZ - 437.0, 2, 0.00085) * Amplitude * 5.2;

    // Fine regolith granulation; small enough to read as surface texture rather than navigable terrain.
    const RegolithGrain = FractalRelief(WorldX - 313.0, WorldZ + 179.0, 3, 0.055) * Amplitude * 0.16;

    return HighlandSwell + MareBasin + CraterContribution(WorldX, WorldZ) + RegolithGrain;
}

// Central-difference gradient of the relief, used to seat chassis pitch/roll against the slope they occupy.
export function TerrainGradient(WorldX, WorldZ)
{
    const SampleOffset = 0.75;                                      // [m] - Finite-difference step
    const GradientX = (TerrainRelief(WorldX + SampleOffset, WorldZ) - TerrainRelief(WorldX - SampleOffset, WorldZ)) /
                      (SampleOffset * 2.0);
    const GradientZ = (TerrainRelief(WorldX, WorldZ + SampleOffset) - TerrainRelief(WorldX, WorldZ - SampleOffset)) /
                      (SampleOffset * 2.0);
    return [GradientX, GradientZ];
}

function ConstructTerrainRegion(Accumulator)
{
    const HalfExtent = FieldSpecification.GroundHalfExtent;
    const CellEdge   = FieldSpecification.TerrainCellEdge;
    const CellCount  = Math.floor((HalfExtent * 2.0) / CellEdge);

    for (let CellIndexZ = 0; CellIndexZ < CellCount; ++CellIndexZ)
    {
        for (let CellIndexX = 0; CellIndexX < CellCount; ++CellIndexX)
        {
            const NearX = -HalfExtent + CellIndexX * CellEdge, FarX = NearX + CellEdge;
            const NearZ = -HalfExtent + CellIndexZ * CellEdge, FarZ = NearZ + CellEdge;

            const CornerA = [NearX, TerrainRelief(NearX, NearZ), NearZ];
            const CornerB = [FarX,  TerrainRelief(FarX,  NearZ), NearZ];
            const CornerC = [FarX,  TerrainRelief(FarX,  FarZ),  FarZ];
            const CornerD = [NearX, TerrainRelief(NearX, FarZ),  FarZ];

            const SpanAB = [CornerB[0] - CornerA[0], CornerB[1] - CornerA[1], CornerB[2] - CornerA[2]];
            const SpanAD = [CornerD[0] - CornerA[0], CornerD[1] - CornerA[1], CornerD[2] - CornerA[2]];
            const FaceNormal = [
                SpanAD[1] * SpanAB[2] - SpanAD[2] * SpanAB[1],
                SpanAD[2] * SpanAB[0] - SpanAD[0] * SpanAB[2],
                SpanAD[0] * SpanAB[1] - SpanAD[1] * SpanAB[0]
            ];
            Accumulator.AppendQuad(CornerA, CornerB, CornerC, CornerD, FaceNormal);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   GEOMETRY ATLAS ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Every part lands in ONE vertex buffer and ONE index buffer; a part is drawn by its (IndexOffset, IndexCount)
//    window. That keeps the whole field to a handful of draw calls with per-instance transforms from storage.
export function AssembleGeometryAtlas(ChassisClassRegistry, ChassisClassOrder)
{
    const Accumulator = new GeometryAccumulator();
    const PartRegions = {};

    const RecordRegion = (RegionLabel, ConstructionRoutine) =>
    {
        const FirstIndex = Accumulator.IndexScalars.length;
        ConstructionRoutine();
        PartRegions[RegionLabel] =
        {
            IndexOffset: FirstIndex,
            IndexCount:  Accumulator.IndexScalars.length - FirstIndex
        };
    };

    RecordRegion("Terrain", () => ConstructTerrainRegion(Accumulator));

    for (const ClassLabel of ChassisClassOrder)
    {
        const ClassProfile = ChassisClassRegistry[ClassLabel];

        RecordRegion(`Hull${ClassLabel}`, () =>
        {
            ConstructRakedHullRegion(Accumulator, ClassProfile.HullExtent);

            // Track skirts flanking the hull, dropped to ground level.
            const TrackHalfWidth = ClassProfile.HullExtent[0] * 0.24;
            const TrackOffsetX   = ClassProfile.HullExtent[0] + TrackHalfWidth * 0.62;
            const TrackOffsetY   = -ClassProfile.HullExtent[1] - ClassProfile.TrackHeight * 0.28;
            for (const FlankSign of [-1, 1])
            {
                ConstructBoxRegion(Accumulator,
                    [FlankSign * TrackOffsetX, TrackOffsetY, 0.0],
                    [TrackHalfWidth, ClassProfile.TrackHeight * 0.5, ClassProfile.HullExtent[2] * 0.94]);
            }

            // Road wheels as small drums laid on their sides, evenly spaced along each track.
            const WheelCount  = ClassLabel === "Heavy" ? 6 : (ClassLabel === "Medium" ? 5 : 4);
            const WheelRadius = ClassProfile.TrackHeight * 0.46;
            const WheelSpan   = ClassProfile.HullExtent[2] * 1.62;
            for (const FlankSign of [-1, 1])
            {
                for (let WheelIndex = 0; WheelIndex < WheelCount; ++WheelIndex)
                {
                    const WheelZ = -WheelSpan * 0.5 + (WheelSpan / (WheelCount - 1)) * WheelIndex;
                    ConstructBoxRegion(Accumulator,
                        [FlankSign * (TrackOffsetX + TrackHalfWidth * 0.35), TrackOffsetY - WheelRadius * 0.35, WheelZ],
                        [TrackHalfWidth * 0.55, WheelRadius, WheelRadius]);
                }
            }
        });

        RecordRegion(`Turret${ClassLabel}`, () =>
        {
            ConstructDrumRegion(Accumulator, [0, 0, 0], ClassProfile.TurretRadius, ClassProfile.TurretHeight,
                                ClassLabel === "Light" ? 8 : 12);
            // Mantlet block at the turret face where the barrel emerges.
            ConstructBoxRegion(Accumulator,
                [0, 0, ClassProfile.TurretRadius * 0.82],
                [ClassProfile.TurretRadius * 0.44, ClassProfile.TurretHeight * 0.40, ClassProfile.TurretRadius * 0.30]);
            // Commander sight blister, offset so the silhouette is asymmetric and heading is readable.
            ConstructBoxRegion(Accumulator,
                [ClassProfile.TurretRadius * 0.42, ClassProfile.TurretHeight * 0.58, -ClassProfile.TurretRadius * 0.34],
                [ClassProfile.TurretRadius * 0.24, ClassProfile.TurretHeight * 0.26, ClassProfile.TurretRadius * 0.24]);
        });

        RecordRegion(`Barrel${ClassLabel}`, () =>
        {
            ConstructBarrelRegion(Accumulator, ClassProfile.BarrelRadius, ClassProfile.BarrelLength, 8);
        });
    }

    RecordRegion("ObstaclePyramid", () => ConstructPyramidRegion(Accumulator, 6.4, 9.2));
    RecordRegion("ObstacleBlock",   () => ConstructBoxRegion(Accumulator, [0, 3.1, 0], [4.2, 3.1, 4.2]));
    RecordRegion("ProjectileBolt",  () => ConstructDrumRegion(Accumulator, [0, 0, 0], 1.0, 2.4, 6));
    RecordRegion("ArmamentCrate",   () =>
    {
        ConstructBoxRegion(Accumulator, [0, 0, 0], [1.0, 1.0, 1.0]);
        ConstructPyramidRegion(Accumulator, 0.72, 1.7);
    });

    return {
        VertexScalars: new Float32Array(Accumulator.VertexScalars),
        IndexScalars:  new Uint32Array(Accumulator.IndexScalars),
        PartRegions:   PartRegions
    };
}

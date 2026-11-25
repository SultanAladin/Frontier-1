/*==========================================================================================================================================
                                                           WORLDMASK2D.JS
==========================================================================================================================================*/
// Infinite 2D Sparse-Tile Open-World Mask & Independent Terrain Bounds Engine.
// - Unbounded 2D world coordinates in meters (sparse 256x256 tiles, 1 tile = 1000m x 1000m = 1 km^2, 0 bytes for empty ocean)
// - Multiple toggleable 2D Mask Shape Layers + Independent toggleable Terrain Bounds Regions
// - Exact linear-time O(N) 2D Euclidean Signed Distance Field (Meijster / Felzenszwalb-Huttenlocher separable EDT)
//   so every Terrain Region gets exact metric distance-from-coastline for Edge->Center Falloffs and Thin-Slab SDF walls!

export const MASK_TILE_RES = 256;
export const TILE_WORLD_METERS = 1000.0; // 1 tile = 1 km x 1 km (~3.906 m per mask texel)
export const METERS_PER_MASK_PIXEL = TILE_WORLD_METERS / MASK_TILE_RES;

const Clamp = (V, Lo, Hi) => (V < Lo ? Lo : V > Hi ? Hi : V);
const TileKey = (Tx, Tz) => `${Tx},${Tz}`;

let NextUid = 1;
export function MakeTerrainUid(Prefix = 'trn')
{
    return `${Prefix}-${NextUid++}`;
}

function Hash2D(Ix, Iy, Seed = 4029)
{
    let H = Math.imul(Ix, 374761393) ^ Math.imul(Iy, 668265263) ^ Math.imul(Seed | 0, 1442695041);
    H = Math.imul(H ^ (H >>> 13), 1274126177);
    return ((H ^ (H >>> 16)) >>> 0) / 4294967295.0;
}

function ValueNoise2D(X, Y, Seed = 4029)
{
    const X0 = Math.floor(X), Y0 = Math.floor(Y);
    const Fx = X - X0, Fy = Y - Y0;
    const Ux = Fx * Fx * (3.0 - 2.0 * Fx);
    const Uy = Fy * Fy * (3.0 - 2.0 * Fy);
    const A = Hash2D(X0, Y0, Seed);
    const B = Hash2D(X0 + 1, Y0, Seed);
    const C = Hash2D(X0, Y0 + 1, Seed);
    const D = Hash2D(X0 + 1, Y0 + 1, Seed);
    return (A + (B - A) * Ux) + ((C + (D - C) * Ux) - (A + (B - A) * Ux)) * Uy;
}

export function CoastFbm2D(X, Y, Octaves = 4, Seed = 4029)
{
    let Sum = 0.0, Amp = 0.5, Freq = 1.0, Norm = 0.0;
    for (let I = 0; I < Octaves; ++I)
    {
        Sum += Amp * ValueNoise2D(X * Freq, Y * Freq, Seed + I * 97);
        Norm += Amp;
        Amp *= 0.5;
        Freq *= 2.03;
    }
    return Sum / Math.max(1e-6, Norm);
}

/*==========================================================================================================================================
                                           COMPACT RLE SERIALIZATION FOR SPARSE TILES
==========================================================================================================================================*/
function EncodeUint8Rle(Bytes)
{
    const Runs = [];
    let I = 0;
    const N = Bytes.length;
    while (I < N)
    {
        const Val = Bytes[I];
        let Count = 1;
        while (I + Count < N && Bytes[I + Count] === Val && Count < 65535) Count++;
        Runs.push(Val, Count);
        I += Count;
    }
    return Runs;
}

function DecodeUint8Rle(Runs, Length)
{
    const Out = new Uint8Array(Length);
    let P = 0;
    for (let I = 0; I < Runs.length; I += 2)
    {
        const Val = Runs[I] & 255;
        const Count = Runs[I + 1] | 0;
        Out.fill(Val, P, Math.min(Length, P + Count));
        P += Count;
    }
    return Out;
}

/*==========================================================================================================================================
                                                 INFINITE 2D WORLD DOCUMENT MODEL
==========================================================================================================================================*/
export class WorldMask2D
{
    constructor()
    {
        this.maskLayers = [];
        this.regions = [];
        this.seaLevel = 0.0;
        this.oceanVisible = true;
    }

    addMaskLayer(Spec = {})
    {
        const Palette = ['#58b982', '#5ac8fa', '#a3d977', '#e8c27a', '#d8846f', '#a78bfa'];
        const Color = Spec.color || Palette[this.maskLayers.length % Palette.length];
        const Layer = {
            id: Spec.id || MakeTerrainUid('mask'),
            name: Spec.name || `Landform Mask ${this.maskLayers.length + 1}`,
            visible: Spec.visible !== false,
            locked: !!Spec.locked,
            opacity: Spec.opacity ?? 1.0,
            color: Color,
            revision: 1,
            tiles: new Map() // Map<"tx,tz", { data: Uint8Array(256*256), dirty: boolean }>
        };
        this.maskLayers.push(Layer);
        return Layer;
    }

    getMaskLayer(LayerId)
    {
        return this.maskLayers.find(L => L.id === LayerId) || null;
    }

    getRegion(RegionId)
    {
        return this.regions.find(R => R.id === RegionId) || null;
    }

    removeRegion(RegionId)
    {
        const Idx = this.regions.findIndex(R => R.id === RegionId);
        if (Idx >= 0) this.regions.splice(Idx, 1);
    }

    totalAllocatedTiles()
    {
        let Total = 0;
        for (const L of this.maskLayers) Total += L.tiles.size;
        return Total;
    }

    totalAllocatedBytes()
    {
        return this.totalAllocatedTiles() * MASK_TILE_RES * MASK_TILE_RES;
    }

    /**
     * Bilinear sample of a single 2D Mask Layer at world coordinates (Wx, Wz) in meters -> [0..1].
     */
    sampleLayerMask(Layer, Wx, Wz)
    {
        if (!Layer || !Layer.visible || Layer.tiles.size === 0) return 0.0;
        const Gx = (Wx / TILE_WORLD_METERS) * MASK_TILE_RES - 0.5;
        const Gz = (Wz / TILE_WORLD_METERS) * MASK_TILE_RES - 0.5;
        const X0 = Math.floor(Gx), Z0 = Math.floor(Gz);
        const Fx = Gx - X0, Fz = Gz - Z0;

        const At = (Px, Pz) =>
        {
            const Tx = Math.floor(Px / MASK_TILE_RES);
            const Tz = Math.floor(Pz / MASK_TILE_RES);
            const TileRec = Layer.tiles.get(TileKey(Tx, Tz));
            if (!TileRec) return 0;
            const Lx = Px - Tx * MASK_TILE_RES;
            const Lz = Pz - Tz * MASK_TILE_RES;
            return TileRec.data[Lz * MASK_TILE_RES + Lx];
        };

        const V00 = At(X0, Z0), V10 = At(X0 + 1, Z0);
        const V01 = At(X0, Z0 + 1), V11 = At(X0 + 1, Z0 + 1);
        const Top = V00 + (V10 - V00) * Fx;
        const Bot = V01 + (V11 - V01) * Fx;
        return (Top + (Bot - Top) * Fz) / 255.0;
    }

    sampleCoverageMeters(Wx, Wz, MaskLayerId = null)
    {
        if (MaskLayerId && MaskLayerId !== 'all')
        {
            const L = this.getMaskLayer(MaskLayerId);
            return L ? this.sampleLayerMask(L, Wx, Wz) : 0.0;
        }
        let Best = 0.0;
        for (const L of this.maskLayers)
        {
            if (L.visible) Best = Math.max(Best, this.sampleLayerMask(L, Wx, Wz));
        }
        return Best;
    }

    sampleRegionMask(Region, Wx, Wz)
    {
        return this.sampleCoverageMeters(Wx, Wz, Region.maskLayerId);
    }

    /**
     * Paints or erases a continuous 2D capsule stroke in world meters across arbitrary sparse tile boundaries.
     * Signature: `paintCapsuleMeters(Layer, [X0, Z0], [X1, Z1], RadiusMeters, Mode, Strength, RoughCoast, Seed)`
     */
    paintCapsuleMeters(Layer, P0, P1, RadiusMeters, Mode = 'paint', Strength = 1.0, RoughCoast = 0.0, Seed = 4029)
    {
        if (!Layer || Layer.locked) return [];
        const X0 = P0[0], Z0 = P0[1];
        const X1 = P1[0], Z1 = P1[1];
        const R = Clamp(RadiusMeters, METERS_PER_MASK_PIXEL * 1.2, 2500.0);
        const Dist = Math.hypot(X1 - X0, Z1 - Z0);
        const Steps = Math.max(1, Math.ceil(Dist / Math.max(4.0, R * 0.35)));
        const TouchedKeys = new Set();
        const SoftFeather = Math.max(METERS_PER_MASK_PIXEL * 1.5, R * 0.18);

        for (let S = 0; S <= Steps; ++S)
        {
            const T = S / Steps;
            const Cx = X0 + (X1 - X0) * T;
            const Cz = Z0 + (Z1 - Z0) * T;
            const Pad = R * (1.0 + RoughCoast * 0.42) + SoftFeather;

            const MinPx = Math.floor(((Cx - Pad) / TILE_WORLD_METERS) * MASK_TILE_RES);
            const MaxPx = Math.ceil(((Cx + Pad) / TILE_WORLD_METERS) * MASK_TILE_RES);
            const MinPz = Math.floor(((Cz - Pad) / TILE_WORLD_METERS) * MASK_TILE_RES);
            const MaxPz = Math.ceil(((Cz + Pad) / TILE_WORLD_METERS) * MASK_TILE_RES);

            for (let Pz = MinPz; Pz <= MaxPz; ++Pz)
            {
                const Wz = ((Pz + 0.5) / MASK_TILE_RES) * TILE_WORLD_METERS;
                const Tz = Math.floor(Pz / MASK_TILE_RES);
                const Lz = Pz - Tz * MASK_TILE_RES;

                for (let Px = MinPx; Px <= MaxPx; ++Px)
                {
                    const Wx = ((Px + 0.5) / MASK_TILE_RES) * TILE_WORLD_METERS;
                    let EffR = R;
                    if (RoughCoast > 0.01)
                    {
                        const N = CoastFbm2D(Wx * 0.0045, Wz * 0.0045, 4, Seed) - 0.5;
                        EffR = R * (1.0 + N * RoughCoast * 0.85);
                    }
                    const D = Math.hypot(Wx - Cx, Wz - Cz);
                    if (D >= EffR + SoftFeather) continue;

                    const Falloff = Clamp(1.0 - (D - (EffR - SoftFeather)) / (2.0 * SoftFeather), 0.0, 1.0);
                    const Smooth = Falloff * Falloff * (3.0 - 2.0 * Falloff);
                    const Delta255 = Math.round(Smooth * Clamp(Strength, 0.05, 1.0) * 255);
                    if (Delta255 <= 0) continue;

                    const Tx = Math.floor(Px / MASK_TILE_RES);
                    const Key = TileKey(Tx, Tz);
                    let TileRec = Layer.tiles.get(Key);
                    if (!TileRec)
                    {
                        if (Mode === 'erase') continue;
                        TileRec = { data: new Uint8Array(MASK_TILE_RES * MASK_TILE_RES), dirty: true };
                        Layer.tiles.set(Key, TileRec);
                    }

                    const Idx = Lz * MASK_TILE_RES + (Px - Tx * MASK_TILE_RES);
                    const Prev = TileRec.data[Idx];
                    let Next = Prev;
                    if (Mode === 'erase')
                    {
                        Next = Math.max(0, Prev - Delta255);
                    }
                    else if (Mode === 'smooth')
                    {
                        Next = Math.round(Prev + (128 - Prev) * Smooth * 0.25);
                    }
                    else
                    {
                        Next = Math.max(Prev, Math.min(255, Delta255));
                    }

                    if (Next !== Prev)
                    {
                        TileRec.data[Idx] = Next;
                        TileRec.dirty = true;
                        TouchedKeys.add(Key);
                    }
                }
            }
        }

        if (TouchedKeys.size > 0)
        {
            if (Mode === 'erase')
            {
                for (const Key of TouchedKeys)
                {
                    const TileRec = Layer.tiles.get(Key);
                    if (TileRec && !TileRec.data.some(Boolean)) Layer.tiles.delete(Key);
                }
            }
            Layer.revision++;
            this.markRegionsDirtyInRect(
                Math.min(X0, X1) - R * 1.5,
                Math.min(Z0, Z1) - R * 1.5,
                Math.max(X0, X1) + R * 1.5,
                Math.max(Z0, Z1) + R * 1.5,
                Layer.id
            );
        }
        return Array.from(TouchedKeys);
    }

    /**
     * Fills or carves an arbitrary closed 2D polygon (with optional organic fBm shoreline perturbation) in world meters.
     */
    paintPolygonMeters(Layer, PointsXZ, Erase = false, CoastRoughness = 0.22, Seed = 4029)
    {
        if (!Layer || Layer.locked || !PointsXZ || PointsXZ.length < 3) return [];
        let MinX = 1e9, MinZ = 1e9, MaxX = -1e9, MaxZ = -1e9;
        for (const [X, Z] of PointsXZ)
        {
            if (X < MinX) MinX = X;
            if (X > MaxX) MaxX = X;
            if (Z < MinZ) MinZ = Z;
            if (Z > MaxZ) MaxZ = Z;
        }
        const Pad = 85.0;
        const MinPx = Math.floor(((MinX - Pad) / TILE_WORLD_METERS) * MASK_TILE_RES);
        const MaxPx = Math.ceil(((MaxX + Pad) / TILE_WORLD_METERS) * MASK_TILE_RES);
        const MinPz = Math.floor(((MinZ - Pad) / TILE_WORLD_METERS) * MASK_TILE_RES);
        const MaxPz = Math.ceil(((MaxZ + Pad) / TILE_WORLD_METERS) * MASK_TILE_RES);
        const NVerts = PointsXZ.length;
        const TouchedKeys = new Set();

        for (let Pz = MinPz; Pz <= MaxPz; ++Pz)
        {
            const Wz = ((Pz + 0.5) / MASK_TILE_RES) * TILE_WORLD_METERS;
            const Tz = Math.floor(Pz / MASK_TILE_RES);
            const Lz = Pz - Tz * MASK_TILE_RES;

            for (let Px = MinPx; Px <= MaxPx; ++Px)
            {
                const Wx = ((Px + 0.5) / MASK_TILE_RES) * TILE_WORLD_METERS;
                let Qx = Wx, Qz = Wz;
                if (CoastRoughness > 0.005)
                {
                    const WarpX = (CoastFbm2D(Wx * 0.0032, Wz * 0.0032, 4, Seed) - 0.5) * 140.0 * CoastRoughness;
                    const WarpZ = (CoastFbm2D(Wx * 0.0032 + 19.3, Wz * 0.0032 + 7.1, 4, Seed + 31) - 0.5) * 140.0 * CoastRoughness;
                    Qx += WarpX;
                    Qz += WarpZ;
                }

                let Inside = false;
                let MinEdgeSq = 1e18;
                for (let I = 0, J = NVerts - 1; I < NVerts; J = I++)
                {
                    const Ax = PointsXZ[J][0], Az = PointsXZ[J][1];
                    const Bx = PointsXZ[I][0], Bz = PointsXZ[I][1];
                    if (((Az > Qz) !== (Bz > Qz)) && (Qx < ((Bx - Ax) * (Qz - Az)) / (Bz - Az + 1e-12) + Ax))
                    {
                        Inside = !Inside;
                    }
                    const Ex = Bx - Ax, Ez = Bz - Az;
                    const LenSq = Math.max(1e-6, Ex * Ex + Ez * Ez);
                    const T = Clamp(((Qx - Ax) * Ex + (Qz - Az) * Ez) / LenSq, 0.0, 1.0);
                    const Dx = Qx - (Ax + Ex * T), Dz = Qz - (Az + Ez * T);
                    const DSq = Dx * Dx + Dz * Dz;
                    if (DSq < MinEdgeSq) MinEdgeSq = DSq;
                }

                const SignedD = (Inside ? -1.0 : 1.0) * Math.sqrt(MinEdgeSq);
                const Feather = METERS_PER_MASK_PIXEL * 2.2;
                if (SignedD >= Feather) continue;

                const Cov = Clamp(0.5 - SignedD / (2.0 * Feather), 0.0, 1.0);
                const Val255 = Math.round(Cov * 255);
                if (Val255 <= 0) continue;

                const Tx = Math.floor(Px / MASK_TILE_RES);
                const Key = TileKey(Tx, Tz);
                let TileRec = Layer.tiles.get(Key);
                if (!TileRec)
                {
                    if (Erase) continue;
                    TileRec = { data: new Uint8Array(MASK_TILE_RES * MASK_TILE_RES), dirty: true };
                    Layer.tiles.set(Key, TileRec);
                }
                const Idx = Lz * MASK_TILE_RES + (Px - Tx * MASK_TILE_RES);
                const Next = Erase ? Math.max(0, TileRec.data[Idx] - Val255) : Math.max(TileRec.data[Idx], Val255);
                if (Next !== TileRec.data[Idx])
                {
                    TileRec.data[Idx] = Next;
                    TileRec.dirty = true;
                    TouchedKeys.add(Key);
                }
            }
        }

        if (TouchedKeys.size > 0)
        {
            if (Erase)
            {
                for (const Key of TouchedKeys)
                {
                    const TileRec = Layer.tiles.get(Key);
                    if (TileRec && !TileRec.data.some(Boolean)) Layer.tiles.delete(Key);
                }
            }
            Layer.revision++;
            this.markRegionsDirtyInRect(MinX - Pad, MinZ - Pad, MaxX + Pad, MaxZ + Pad, Layer.id);
        }
        return Array.from(TouchedKeys);
    }

    computeLayerPaintedBounds(Layer, PaddingMeters = 90.0)
    {
        if (!Layer || Layer.tiles.size === 0) return null;
        let MinX = 1e9, MinZ = 1e9, MaxX = -1e9, MaxZ = -1e9;
        let Found = false;

        for (const [Key, TileRec] of Layer.tiles.entries())
        {
            const [Tx, Tz] = Key.split(',').map(Number);
            const Data = TileRec.data;
            for (let Pz = 0; Pz < MASK_TILE_RES; ++Pz)
            {
                const Row = Pz * MASK_TILE_RES;
                for (let Px = 0; Px < MASK_TILE_RES; ++Px)
                {
                    if (Data[Row + Px] > 24)
                    {
                        const Wx = ((Tx * MASK_TILE_RES + Px + 0.5) / MASK_TILE_RES) * TILE_WORLD_METERS;
                        const Wz = ((Tz * MASK_TILE_RES + Pz + 0.5) / MASK_TILE_RES) * TILE_WORLD_METERS;
                        if (Wx < MinX) MinX = Wx;
                        if (Wx > MaxX) MaxX = Wx;
                        if (Wz < MinZ) MinZ = Wz;
                        if (Wz > MaxZ) MaxZ = Wz;
                        Found = true;
                    }
                }
            }
        }
        if (!Found) return null;
        return {
            x: Math.floor((MinX - PaddingMeters) / 10) * 10,
            z: Math.floor((MinZ - PaddingMeters) / 10) * 10,
            width: Math.ceil((MaxX - MinX + PaddingMeters * 2.0) / 10) * 10,
            depth: Math.ceil((MaxZ - MinZ + PaddingMeters * 2.0) / 10) * 10
        };
    }

    addTerrainRegion(Spec = {})
    {
        const Palette = ['#e8c27a', '#5ac8fa', '#3ec46d', '#f08a5a', '#c084fc'];
        const Color = Spec.color || Palette[this.regions.length % Palette.length];
        const Bounds = Spec.bounds || { x: -600, z: -600, width: 1200, depth: 1200 };
        const Res = Spec.resolution || 144;

        const Region = {
            id: Spec.id || MakeTerrainUid('terrain'),
            name: Spec.name || `Terrain ${String(this.regions.length + 1).padStart(2, '0')}`,
            maskLayerId: Spec.maskLayerId !== undefined ? Spec.maskLayerId : (this.maskLayers[0]?.id ?? null),
            enabled: Spec.enabled !== false,
            locked: !!Spec.locked,
            color: Color,
            bounds: {
                x: Number(Bounds.x) || 0,
                z: Number(Bounds.z) || 0,
                width: Math.max(80, Number(Bounds.width) || 1000),
                depth: Math.max(80, Number(Bounds.depth) || 1000)
            },
            resolution: Res,
            baseElevation: Spec.baseElevation ?? 3.0,
            slabThickness: Spec.slabThickness ?? 6.5,
            shorelineBevel: Spec.shorelineBevel ?? 18.0,
            maxRelief: Spec.maxRelief ?? 280.0,
            worldFrequencySpan: Spec.worldFrequencySpan || 0, // >0 locks noise wavelength across partitioned sub-world tiles!
            seamLock: Spec.seamLock !== false,                // Seamlessly stitches shared borders across adjacent sub-worlds
            stack: Spec.stack || [],
            sdf3DOps: Spec.sdf3DOps || [],
            dirty: true,
            meshDirty: true,
            runtime: null,
            _maskCache: null
        };
        this.regions.push(Region);
        return Region;
    }

    /**
     * Finds the visible 2D Mask Layer with the highest painted land coverage inside `Bounds`
     * (adopting unassigned painted continents automatically when boxing a new Terrain Region).
     */
    findDominantMaskInBounds(Bounds, FallbackMaskId = null)
    {
        if (!Bounds || this.maskLayers.length === 0) return FallbackMaskId;
        let BestLayer = null;
        let BestSum = 0.0;
        const Samples = 12;
        for (const Layer of this.maskLayers)
        {
            if (!Layer.visible || Layer.tiles.size === 0) continue;
            let Sum = 0.0;
            for (let Sz = 0; Sz < Samples; ++Sz)
            {
                const Wz = Bounds.z + ((Sz + 0.5) / Samples) * Bounds.depth;
                for (let Sx = 0; Sx < Samples; ++Sx)
                {
                    const Wx = Bounds.x + ((Sx + 0.5) / Samples) * Bounds.width;
                    Sum += this.sampleLayerMask(Layer, Wx, Wz);
                }
            }
            if (Sum > BestSum)
            {
                BestSum = Sum;
                BestLayer = Layer;
            }
        }
        return BestSum > 0.25 && BestLayer ? BestLayer.id : FallbackMaskId;
    }

    /**
     * Splits a large 2D landmass mask into a manageable grid of individually-simulatable `TerrainRegion` sub-worlds!
     * - Automatically skips empty ocean cells.
     * - Locks `worldFrequencySpan` across all generated sub-worlds so procedural mountains, ridges, strata & splines
     *   line up seamlessly (`0.000 m` border step) while allowing 1-by-1 solo or shared-weather simulation per tile!
     */
    partitionMaskIntoSubRegions(MaskLayer, Options = {})
    {
        const TargetMask = MaskLayer || this.maskLayers[0];
        if (!TargetMask) return [];
        const FullBounds = this.computeLayerPaintedBounds(TargetMask, 80.0);
        if (!FullBounds) return [];

        const TileSize = Clamp(Number(Options.tileSizeM) || 1100.0, 400.0, 4000.0);
        const CellRes = Clamp(Options.resolution | 0 || 128, 64, 224);
        const ReplaceExisting = Options.replaceExistingForMask !== false;
        const Template = Options.templateRegion || this.regions.find(R => R.maskLayerId === TargetMask.id) || this.regions[0] || null;
        const LockedFreqSpan = Template?.worldFrequencySpan || Math.max(600.0, Math.min(FullBounds.width, FullBounds.depth) * 0.55);

        const Cols = Math.max(1, Math.ceil(FullBounds.width / TileSize));
        const Rows = Math.max(1, Math.ceil(FullBounds.depth / TileSize));
        const StepW = Math.ceil(FullBounds.width / Cols / 10) * 10;
        const StepD = Math.ceil(FullBounds.depth / Rows / 10) * 10;

        if (ReplaceExisting)
        {
            this.regions = this.regions.filter(R => R.maskLayerId !== TargetMask.id);
        }

        const Created = [];
        const Palette = ['#e8c27a', '#5ac8fa', '#3ec46d', '#f08a5a', '#c084fc', '#a3d977'];
        let TileCounter = 1;

        for (let Rz = 0; Rz < Rows; ++Rz)
        {
            for (let Cx = 0; Cx < Cols; ++Cx)
            {
                const Box = {
                    x: FullBounds.x + Cx * StepW,
                    z: FullBounds.z + Rz * StepD,
                    width: StepW,
                    depth: StepD
                };

                // Check if this sub-world cell actually contains painted land
                let HasLand = false;
                const ProbeN = 14;
                for (let Pz = 0; Pz <= ProbeN && !HasLand; ++Pz)
                {
                    const Wz = Box.z + (Pz / ProbeN) * Box.depth;
                    for (let Px = 0; Px <= ProbeN; ++Px)
                    {
                        const Wx = Box.x + (Px / ProbeN) * Box.width;
                        if (this.sampleLayerMask(TargetMask, Wx, Wz) >= 0.42)
                        {
                            HasLand = true;
                            break;
                        }
                    }
                }
                if (!HasLand) continue;

                // Clone non-sculpt procedural & spline stack from Template so world-space landforms span all tiles seamlessly!
                const ClonedStack = (Template?.stack || [])
                    .filter(L => L.type !== 'sculpt')
                    .map(L =>
                    {
                        const Copy = JSON.parse(JSON.stringify({ ...L, deltas: undefined }));
                        Copy.id = MakeTerrainUid('lyr');
                        // Ensure box-edge falloff doesn't pinch interior partition borders!
                        if (Copy.falloff?.mode === 'box-feather') Copy.falloff.mode = 'soft';
                        return Copy;
                    });

                const SubReg = this.addTerrainRegion({
                    name: `${TargetMask.name.replace(/^Mask\s*\d+\s*·\s*/i, '')} · Cell ${Rz + 1}-${Cx + 1}`,
                    maskLayerId: TargetMask.id,
                    color: Palette[(TileCounter - 1) % Palette.length],
                    bounds: Box,
                    resolution: CellRes,
                    baseElevation: Template?.baseElevation ?? 3.0,
                    slabThickness: Template?.slabThickness ?? 6.5,
                    shorelineBevel: Template?.shorelineBevel ?? 18.0,
                    maxRelief: Template?.maxRelief ?? 280.0,
                    worldFrequencySpan: LockedFreqSpan,
                    seamLock: true,
                    stack: ClonedStack
                });
                Created.push(SubReg);
                TileCounter++;
            }
        }
        return Created;
    }

    markRegionsDirtyInRect(MinX, MinZ, MaxX, MaxZ, ChangedMaskLayerId = null)
    {
        // Include 600 m coastline EDT collar so adjacent partition tiles also refresh their coastal distance field!
        const Pad = 600.0;
        for (const R of this.regions)
        {
            if (ChangedMaskLayerId && R.maskLayerId && R.maskLayerId !== 'all' && R.maskLayerId !== ChangedMaskLayerId) continue;
            const B = R.bounds;
            const Disjoint = (MaxX + Pad < B.x || MinX - Pad > B.x + B.width || MaxZ + Pad < B.z || MinZ - Pad > B.z + B.depth);
            if (!Disjoint)
            {
                R.dirty = true;
                R.meshDirty = true;
                R._maskCache = null;
            }
        }
    }

    /*======================================================================================================================================
                                              PROJECT SERIALIZATION (.JSON ROUND-TRIP)
    ======================================================================================================================================*/
    toJSON()
    {
        return {
            format: 'frontier.terrain.world.v1',
            seaLevel: this.seaLevel,
            oceanVisible: this.oceanVisible,
            maskLayers: this.maskLayers.map(L =>
            {
                const TilesObj = {};
                for (const [Key, Rec] of L.tiles.entries())
                {
                    TilesObj[Key] = EncodeUint8Rle(Rec.data);
                }
                return {
                    id: L.id,
                    name: L.name,
                    visible: L.visible,
                    locked: L.locked,
                    opacity: L.opacity,
                    color: L.color,
                    tiles: TilesObj
                };
            }),
            regions: this.regions.map(R => ({
                id: R.id,
                name: R.name,
                maskLayerId: R.maskLayerId,
                enabled: R.enabled,
                locked: R.locked,
                color: R.color,
                bounds: { ...R.bounds },
                resolution: R.resolution,
                baseElevation: R.baseElevation,
                slabThickness: R.slabThickness,
                shorelineBevel: R.shorelineBevel,
                maxRelief: R.maxRelief,
                worldFrequencySpan: R.worldFrequencySpan || 0,
                seamLock: R.seamLock !== false,
                sdf3DOps: R.sdf3DOps.map(Op => ({ ...Op })),
                stack: R.stack.map(Lyr =>
                {
                    const Copy = JSON.parse(JSON.stringify({ ...Lyr, deltas: undefined }));
                    if (Lyr.deltas instanceof Float32Array)
                    {
                        Copy.deltasArr = Array.from(Lyr.deltas, V => Math.round(V * 100) / 100);
                    }
                    return Copy;
                })
            }))
        };
    }

    loadFromJSON(Doc)
    {
        if (!Doc || Doc.format !== 'frontier.terrain.world.v1')
        {
            throw new Error('Unrecognized Terrain World JSON document');
        }
        this.seaLevel = Number(Doc.seaLevel) || 0.0;
        this.oceanVisible = Doc.oceanVisible !== false;
        this.maskLayers = [];
        this.regions = [];

        for (const Ml of (Doc.maskLayers || []))
        {
            const Layer = this.addMaskLayer(Ml);
            for (const [Key, Runs] of Object.entries(Ml.tiles || {}))
            {
                Layer.tiles.set(Key, {
                    data: DecodeUint8Rle(Runs, MASK_TILE_RES * MASK_TILE_RES),
                    dirty: true
                });
            }
        }

        for (const Rg of (Doc.regions || []))
        {
            const Stack = (Rg.stack || []).map(Lyr =>
            {
                const Restored = { ...Lyr };
                if (Array.isArray(Restored.deltasArr))
                {
                    Restored.deltas = new Float32Array(Restored.deltasArr);
                    delete Restored.deltasArr;
                }
                return Restored;
            });
            this.addTerrainRegion({ ...Rg, stack: Stack });
        }
    }
}

/*==========================================================================================================================================
                              EXACT O(N) 2D EUCLIDEAN SIGNED DISTANCE FIELD (MEIJSTER / FH PARABOLIC EDT)
==========================================================================================================================================*/
const INF_DIST_SQ = 1e18;

function Edt1D(F, N, OutD, VBuf, ZBuf)
{
    let K = 0;
    VBuf[0] = 0;
    ZBuf[0] = -1e20;
    ZBuf[1] = +1e20;

    for (let Q = 1; Q < N; ++Q)
    {
        const Fq = F[Q] + Q * Q;
        let S = 0.0;
        while (K >= 0)
        {
            const Vk = VBuf[K];
            S = (Fq - (F[Vk] + Vk * Vk)) / (2.0 * (Q - Vk));
            if (S > ZBuf[K]) break;
            K--;
        }
        K++;
        VBuf[K] = Q;
        ZBuf[K] = S;
        ZBuf[K + 1] = +1e20;
    }

    K = 0;
    for (let Q = 0; Q < N; ++Q)
    {
        while (ZBuf[K + 1] < Q) K++;
        const Dx = Q - VBuf[K];
        OutD[Q] = Dx * Dx + F[VBuf[K]];
    }
}

export function ComputeRegionMaskSdf2D(World, Region, Nx, Nz)
{
    const B = Region.bounds;
    const MaskSig = World.maskLayers
        .map(L => `${L.id}:${L.visible ? 1 : 0}:${L.revision}`)
        .join('|');
    const CacheKey = `${MaskSig}|${Region.maskLayerId || 'all'}|${B.x},${B.z},${B.width},${B.depth}|${Nx}x${Nz}`;
    if (Region._maskCache && Region._maskCache.key === CacheKey)
    {
        return Region._maskCache.result;
    }

    const Count = Nx * Nz;
    const Coverage = new Float32Array(Count);
    const SdfMeters = new Float32Array(Count);
    const StepX = B.width / Math.max(1, Nx - 1);
    const StepZ = B.depth / Math.max(1, Nz - 1);
    const MetricScale = 0.5 * (StepX + StepZ);

    // Evaluate Meijster 2D EDT over a world-continuous padded collar (~520 m outside Region.bounds)
    // so adjacent partitioned sub-world tiles share the EXACT distance-to-ocean across interior tile borders!
    const CollarM = Math.min(720.0, Math.max(480.0, 0.30 * Math.max(B.width, B.depth)));
    const PadX = Math.min(72, Math.max(6, Math.ceil(CollarM / Math.max(1.0, StepX))));
    const PadZ = Math.min(72, Math.max(6, Math.ceil(CollarM / Math.max(1.0, StepZ))));
    const PNx = Nx + 2 * PadX;
    const PNz = Nz + 2 * PadZ;
    const PCount = PNx * PNz;
    const PCov = new Float32Array(PCount);

    let InsideRegion = 0;
    let InsidePadded = 0;
    for (let Pz = 0; Pz < PNz; ++Pz)
    {
        const Wz = B.z + (Pz - PadZ) * StepZ;
        const PRow = Pz * PNx;
        const InZ = Pz >= PadZ && Pz < PadZ + Nz;
        for (let Px = 0; Px < PNx; ++Px)
        {
            const Wx = B.x + (Px - PadX) * StepX;
            const Cov = World.sampleRegionMask(Region, Wx, Wz);
            PCov[PRow + Px] = Cov;
            const IsLand = Cov >= 0.5;
            if (IsLand) InsidePadded++;
            if (InZ && Px >= PadX && Px < PadX + Nx)
            {
                Coverage[(Pz - PadZ) * Nx + (Px - PadX)] = Cov;
                if (IsLand) InsideRegion++;
            }
        }
    }

    if (InsideRegion === 0)
    {
        SdfMeters.fill(Math.max(B.width, B.depth));
        const EmptyRes = { coverage: Coverage, sdf2D: SdfMeters, hasLand: false };
        Region._maskCache = { key: CacheKey, result: EmptyRes };
        return EmptyRes;
    }

    // Deep interior land tile (even the entire 520 m world collar is solid land!) -> deep negative inland SDF!
    if (InsidePadded === PCount)
    {
        const DeepInland = -Math.min(PadX * StepX, PadZ * StepZ);
        SdfMeters.fill(DeepInland);
        const FullRes = { coverage: Coverage, sdf2D: SdfMeters, hasLand: true };
        Region._maskCache = { key: CacheKey, result: FullRes };
        return FullRes;
    }

    const MaxDim = Math.max(PNx, PNz);
    const F1D = new Float64Array(MaxDim);
    const Out1D = new Float64Array(MaxDim);
    const VBuf = new Int32Array(MaxDim + 1);
    const ZBuf = new Float64Array(MaxDim + 2);

    const OutsideSq = new Float64Array(PCount);
    const InsideSq = new Float64Array(PCount);

    for (let I = 0; I < PCount; ++I)
    {
        const IsLand = PCov[I] >= 0.5;
        OutsideSq[I] = IsLand ? 0.0 : INF_DIST_SQ;
        InsideSq[I] = IsLand ? INF_DIST_SQ : 0.0;
    }

    const Run2DEdt = Grid =>
    {
        const AspectZ = (StepZ / StepX) * (StepZ / StepX);
        for (let X = 0; X < PNx; ++X)
        {
            for (let Z = 0; Z < PNz; ++Z) F1D[Z] = Grid[Z * PNx + X];
            Edt1D(F1D, PNz, Out1D, VBuf, ZBuf);
            for (let Z = 0; Z < PNz; ++Z)
            {
                const Raw = Out1D[Z];
                Grid[Z * PNx + X] = Raw >= INF_DIST_SQ * 0.5 ? INF_DIST_SQ : Raw * AspectZ;
            }
        }
        for (let Z = 0; Z < PNz; ++Z)
        {
            const Row = Z * PNx;
            for (let X = 0; X < PNx; ++X) F1D[X] = Grid[Row + X];
            Edt1D(F1D, PNx, Out1D, VBuf, ZBuf);
            for (let X = 0; X < PNx; ++X) Grid[Row + X] = Out1D[X];
        }
    };

    Run2DEdt(OutsideSq);
    Run2DEdt(InsideSq);

    for (let Z = 0; Z < Nz; ++Z)
    {
        const PRow = (Z + PadZ) * PNx + PadX;
        const OutRow = Z * Nx;
        for (let X = 0; X < Nx; ++X)
        {
            const PIdx = PRow + X;
            const Cov = Coverage[OutRow + X];
            const SubPixelShift = (0.5 - Cov) * MetricScale;
            if (Cov >= 0.5)
            {
                const DistToCoast = (Math.sqrt(InsideSq[PIdx]) - 0.5) * StepX;
                SdfMeters[OutRow + X] = -Math.max(0.0, DistToCoast) + SubPixelShift * 0.5;
            }
            else
            {
                const DistToLand = (Math.sqrt(OutsideSq[PIdx]) - 0.5) * StepX;
                SdfMeters[OutRow + X] = Math.max(0.0, DistToLand) + SubPixelShift * 0.5;
            }
        }
    }

    const Result = { coverage: Coverage, sdf2D: SdfMeters, hasLand: true };
    Region._maskCache = { key: CacheKey, result: Result };
    return Result;
}

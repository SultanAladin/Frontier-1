//============================================================================================================================================
//                                                             TYREMARKDEPOT.JS
//============================================================================================================================================
// 🧩 Rubber laid on the course: a world-anchored deposit texture, filled from each tyre's sliding work, read by the surface raster.

// 📝 What darkens the road is rubber, and rubber is torn off in proportion to the work a sliding contact does on it.
//    Each frame a wheel in contact sweeps a rectangle from where its patch was to where it is; every texel inside
//    gains the sliding energy that frame spent per unit swept area, against SaturationEnergy for fully black. A
//    stationary burnout pours all its power into one patch and goes black in a fraction of a second; a drift spreads
//    the same power along a long path and leaves grey streaks that darken pass after pass. Nothing is faked from slip
//    angles or key presses — the marks are the tyre model's own dissipation (VehicleSolver's SlidingEnergy).
//
//    Four channels, 5.5 cm texels over the whole 224 × 128 m pad:
//
//      r  the deposit, 0 (clean) to 1 (black);
//      g  where across the tread the texel lies — 0 and 1 are the tread's edges, the byte spans −¼ … 1¼ so a texel
//         just outside the tread still knows where the edge is, and 0 means "never touched".
//
//      b/a rim scrape deposit / lateral coordinate, independent of rubber.
//
//    The surface raster filters the lateral coordinate itself, among touched texels only, which puts every mark's
//    edge exactly where the tread's was — far sharper than the texels — and cuts the tread's four circumferential
//    grooves into it as fine lines (smeared along the path when the tyre slides, as real marks are). A marked road
//    pixel becomes the rubber family with a probability equal to the deposit (a per-frame dither); temporal
//    antialiasing resolves that into a smooth blend, so not one lighting program had to learn about tyre marks.
//
//    The CPU copy is sparse — 64² tiles allocated where rubber has been laid — and each frame uploads only the dirty
//    rectangle. Wheels riding on debris lay nothing.

export const TyreMarkLayout = Object.freeze({
    Origin:           Object.freeze([-64.0, -64.0]),   // [m]    world x, y of the deposit's first texel corner
    Extent:           Object.freeze([224.0, 128.0]),   // [m]    area it covers
    Texels:           Object.freeze([4096, 2340]),     // [-]    → 5.47 cm texels
    PatchWidth:       0.27,    // [m]    tread width laid down
    PatchLength:      0.16,    // [m]    contact-patch length: the shortest a frame's sweep can be
    SaturationEnergy: 60000.0, // [J/m²] sliding work per unit area that leaves the road fully black
    PowerFloor:       1200.0   // [W]    below this a patch only polishes the road
});

const TileEdge = 64;   // [texel] sparse CPU tile

export class TyreMarkDepot
{
    constructor(Exchange)
    {
        const L = TyreMarkLayout;
        this.Exchange = Exchange;
        this.TilesX   = Math.ceil(L.Texels[0] / TileEdge);
        this.TilesY   = Math.ceil(L.Texels[1] / TileEdge);
        this.Tiles    = new Map();   // tile index → { Deposit: Float32Array, Bytes: Uint8Array (rgba) }
        this.Texture  = Exchange.ConstructTexture({ label: "Rubber and rim scrape deposit", size: [L.Texels[0], L.Texels[1]], format: "rgba8unorm",
                                                    usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
        this.View     = this.Texture.createView();
        this.Sampler  = Exchange.ConstructSampler({ magFilter: "linear", minFilter: "linear",
                                                    addressModeU: "clamp-to-edge", addressModeV: "clamp-to-edge" });
        this.Previous = [null, null, null, null];
        this.PreviousKind = [null, null, null, null];
        this.Dirty    = null;
        this.Metrics  = { LaidMetres: 0.0, Texels: 0 };
    }

    // 📝 Readings: VehicleSolver.DrainWheelReadings() for this frame. Settings: { Enabled, Strength }.
    Advance(Readings, Settings)
    {
        const L = TyreMarkLayout;
        Readings.forEach((Reading, Wheel) =>
        {
            const Contact = Reading.ContactFraction > 0.5 && !Reading.OnDebris;
            const Power = Reading.SlidingPower - L.PowerFloor;
            if (!Settings.Enabled || !Contact || Power <= 0.0 || Reading.Seconds <= 0.0)
            {
                this.Previous[Wheel] = null;
                return;
            }
            const Point = Reading.ContactPoint;
            const Rim = !!Reading.Shredded;
            const Kind = Rim ? 'rim' : 'rubber';
            const From = this.PreviousKind[Wheel] === Kind ? (this.Previous[Wheel] || Point) : Point;
            this.PreviousKind[Wheel] = Kind;
            const Length = Math.hypot(Point[0] - From[0], Point[1] - From[1]);
            // 📝 The tread's axis: the patch's path when it travels, the wheel's own slip direction when it spins on
            //    the spot (a burnout) — so the grooves run along the mark either way.
            const Moving = Math.hypot(Reading.ContactVelocity[0], Reading.ContactVelocity[1]);
            const Along = Moving > 0.5 ? Reading.ContactVelocity : Reading.TreadVelocity;
            const AlongLength = Math.hypot(Along[0], Along[1]);
            const Direction = AlongLength > 1.0e-4 ? [Along[0] / AlongLength, Along[1] / AlongLength] : [1.0, 0.0];
            // 📐 Energy per unit area of this frame's sweep: P·Δt / (w · max(ℓ, patch length)).
            const Swept = L.PatchWidth * Math.max(Length, L.PatchLength);
            const Amount = Settings.Strength * Power * Reading.Seconds / Swept / L.SaturationEnergy;
            if (Length < 4.0) {
                if (Rim) {
                    // Two flange scratches, not a full-width rubber skid. Separate channels preserve both kinds.
                    for (const Side of [-0.1, 0.1]) {
                        const Shift = P => [P[0] - Direction[1] * Side, P[1] + Direction[0] * Side];
                        this.StampSegment(Shift(From), Shift(Point), Direction, Math.min(1, Amount), true);
                    }
                } else this.StampSegment(From, Point, Direction, Amount);
            }
            this.Metrics.LaidMetres += Length;
            this.Previous[Wheel] = Point.slice();
        });
        this.Upload();
    }

    ResolveTile(TileX, TileY, Create)
    {
        const Key = TileY * this.TilesX + TileX;
        let Tile = this.Tiles.get(Key);
        if (!Tile && Create)
        {
            Tile = { Deposit: new Float32Array(TileEdge * TileEdge * 2), Bytes: new Uint8Array(TileEdge * TileEdge * 4) };
            this.Tiles.set(Key, Tile);
        }
        return Tile;
    }

    // 📝 The tread's rectangle from A to B (world x, y), its axis Direction: texels within the tread gain Amount (soft
    //    over the outer tenth), and every texel within a texel and a half of the tread records its lateral coordinate.
    StampSegment(A, B, Direction, Amount, Rim = false)
    {
        const L = TyreMarkLayout;
        const Scale = L.Texels[0] / L.Extent[0];
        const Ax = (A[0] - L.Origin[0]) * Scale, Ay = (A[1] - L.Origin[1]) * Scale;
        const Bx = (B[0] - L.Origin[0]) * Scale, By = (B[1] - L.Origin[1]) * Scale;
        const Half = 0.5 * (Rim ? 0.06 : L.PatchWidth) * Scale, Cap = 0.5 * L.PatchLength * Scale, Margin = 1.5;
        const [Ux, Uy] = Direction;
        const Vx = -Uy, Vy = Ux;
        // 📝 The rectangle spans the sweep along U (plus half a patch at each end) and the tread across V, centred on
        //    the segment's midpoint.
        const Mx = 0.5 * (Ax + Bx), My = 0.5 * (Ay + By);
        const Sweep = Math.abs((Bx - Ax) * Ux + (By - Ay) * Uy);
        const Lateral = Math.abs((Bx - Ax) * Vx + (By - Ay) * Vy);
        const HalfAlong = 0.5 * Sweep + Cap, HalfAcross = Half + 0.5 * Lateral;
        const Reach = Math.hypot(HalfAlong, HalfAcross + Margin) + 1.0;
        const X0 = Math.max(0, Math.floor(Mx - Reach)), X1 = Math.min(L.Texels[0] - 1, Math.ceil(Mx + Reach));
        const Y0 = Math.max(0, Math.floor(My - Reach)), Y1 = Math.min(L.Texels[1] - 1, Math.ceil(My + Reach));
        if (X0 > X1 || Y0 > Y1)
            return;
        for (let Y = Y0; Y <= Y1; ++Y)
        {
            for (let X = X0; X <= X1; ++X)
            {
                const Px = X + 0.5 - Mx, Py = Y + 0.5 - My;
                const U = Px * Ux + Py * Uy, V = Px * Vx + Py * Vy;
                if (Math.abs(U) > HalfAlong + Margin || Math.abs(V) > HalfAcross + Margin)
                    continue;
                // 📝 Across the tread from the segment's local centreline (a sideways sweep widens the band).
                const Offset = Math.max(0.0, (U + 0.5 * Sweep) / Math.max(Sweep, 1.0e-3));
                const Centre = (Ax - Mx + (Bx - Ax) * Math.min(1.0, Offset)) * Vx + (Ay - My + (By - Ay) * Math.min(1.0, Offset)) * Vy;
                const Across = (V - Centre) / (2.0 * Half) + 0.5;
                if (Across < -0.25 || Across > 1.25 || Math.abs(U) > HalfAlong + Margin)
                    continue;
                const Tile = this.ResolveTile(X >> 6, Y >> 6, true);
                const Local = ((Y & 63) << 6) | (X & 63);
                const Channel = Rim ? 2 : 0;
                const DepositIndex = Local * 2 + (Rim ? 1 : 0);
                const Inside = Across >= 0.0 && Across <= 1.0 && Math.abs(U) <= HalfAlong;
                if (Inside)
                {
                    const Edge = Math.min(1.0, 10.0 * Math.min(Across, 1.0 - Across) + 0.35);
                    const Next = Math.min(1.0, Tile.Deposit[DepositIndex] + Amount * Edge);
                    if (Next !== Tile.Deposit[DepositIndex])
                    {
                        Tile.Deposit[DepositIndex] = Next;
                        Tile.Bytes[Local * 4 + Channel] = Math.round(Next * 255.0);
                        this.Metrics.Texels++;
                    }
                }
                // 📝 The newest pass owns the lateral coordinate, except where an older, darker mark already lies
                //    and this one only grazes it.
                if (Inside || Tile.Bytes[Local * 4 + Channel + 1] === 0)
                    Tile.Bytes[Local * 4 + Channel + 1] = Math.max(1, Math.min(255, Math.round((Across + 0.25) / 1.5 * 254.0) + 1));
            }
        }
        this.Dirty = this.Dirty ? [Math.min(this.Dirty[0], X0), Math.min(this.Dirty[1], Y0), Math.max(this.Dirty[2], X1), Math.max(this.Dirty[3], Y1)]
                                : [X0, Y0, X1, Y1];
    }

    Upload()
    {
        if (!this.Dirty)
            return;
        const [X0, Y0, X1, Y1] = this.Dirty;
        const Width = X1 - X0 + 1, Height = Y1 - Y0 + 1;
        const Stride = Math.ceil(Width * 4 / 256) * 256;
        const Rows = new Uint8Array(Stride * Height);
        for (let Row = 0; Row < Height; ++Row)
        {
            const Y = Y0 + Row;
            for (let TileX = X0 >> 6; TileX <= X1 >> 6; ++TileX)
            {
                const Tile = this.ResolveTile(TileX, Y >> 6, false);
                if (!Tile)
                    continue;
                const From = Math.max(X0, TileX << 6), To = Math.min(X1, (TileX << 6) + 63);
                const Source = ((Y & 63) << 6) + (From & 63);
                Rows.set(Tile.Bytes.subarray(Source * 4, (Source + To - From + 1) * 4), Row * Stride + (From - X0) * 4);
            }
        }
        this.Exchange.Device.queue.writeTexture({ texture: this.Texture, origin: [X0, Y0] }, Rows, { bytesPerRow: Stride }, [Width, Height]);
        this.Dirty = null;
    }

    Clear()
    {
        const Zero = new Uint8Array(TileEdge * TileEdge * 4);
        for (const Key of this.Tiles.keys())
        {
            const TileX = Key % this.TilesX, TileY = Math.floor(Key / this.TilesX);
            const Width = Math.min(TileEdge, TyreMarkLayout.Texels[0] - TileX * TileEdge);
            const Height = Math.min(TileEdge, TyreMarkLayout.Texels[1] - TileY * TileEdge);
            this.Exchange.Device.queue.writeTexture({ texture: this.Texture, origin: [TileX * TileEdge, TileY * TileEdge] }, Zero,
                                                    { bytesPerRow: TileEdge * 4 }, [Width, Height]);
        }
        this.Tiles.clear();
        this.PreviousKind = [null, null, null, null];
        this.Previous = [null, null, null, null];
        this.Dirty = null;
        this.Metrics = { LaidMetres: 0.0, Texels: 0 };
    }

    QueryReadout()
    {
        const L = TyreMarkLayout;
        return [`rubber/rim contact along ${this.Metrics.LaidMetres.toFixed(0)} m · ${this.Metrics.Texels} texel writes · ${this.Tiles.size} tiles`,
                `deposit ${L.Texels[0]}×${L.Texels[1]} over ${L.Extent[0]}×${L.Extent[1]} m (${(100 * L.Extent[0] / L.Texels[0]).toFixed(1)} cm texels), edges and grooves from the lateral channel`];
    }
}

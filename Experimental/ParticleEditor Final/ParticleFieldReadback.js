/*==========================================================================================================================================
                                                       PARTICLEFIELDREADBACK.JS
==========================================================================================================================================*/
// 🧩 The particle engine's force field as the visualization samples it: the GPU wind lattice read back, and fieldAccel on the CPU.
import { PE } from './Engine/pe.js';

// 📝 Why read the lattice back instead of re-evaluating it: buildWind adds hash noise (turbulence, curl swirl) computed in
//    f32 on the GPU, which no CPU replica matches bit for bit. Copying the whole 24 × 12 × 24 rgba16float texture costs
//    73 728 bytes (rows padded to 256 B) a few times a second, and then the overlay shows exactly what the particles feel.
//    The engine is not modified: the copy is a separate submission on its device, reading windTex (which has COPY_SRC).

//------------------------------------------------------------------------------------------------------------------------
//                                                    HALF FLOATS
//------------------------------------------------------------------------------------------------------------------------

export function DecodeHalf(Bits)
{
    const Sign = Bits & 0x8000 ? -1 : 1;
    const Exponent = (Bits >> 10) & 0x1f;
    const Mantissa = Bits & 0x3ff;
    if (Exponent === 0) return Sign * Math.pow(2, -14) * (Mantissa / 1024);
    if (Exponent === 31) return Mantissa ? NaN : Sign * Infinity;
    return Sign * Math.pow(2, Exponent - 15) * (1 + Mantissa / 1024);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  LATTICE READBACK
//------------------------------------------------------------------------------------------------------------------------

export function ConstructLatticeReadback(Engine)
{
    const [Columns, Rows, Layers] = PE.WIND.dim;
    const RowBytes = Math.ceil((Columns * 8) / 256) * 256;
    const Bytes = RowBytes * Rows * Layers;
    const Device = Engine.device;
    const Staging = Device.createBuffer({ size: Bytes, usage: GPUBufferUsage.MAP_READ | GPUBufferUsage.COPY_DST, label: 'lattice readback' });
    const Vectors = new Float32Array(Columns * Rows * Layers * 3);
    const Status = { Pending: false, Count: 0, Milliseconds: 0, ReadAt: 0, Bytes, Ready: false };

    function Request()
    {
        if (Status.Pending || Engine.deviceLost || !Engine.windTex) return;
        Status.Pending = true;
        const Started = performance.now();
        const Encoder = Device.createCommandEncoder({ label: 'lattice readback' });
        Encoder.copyTextureToBuffer({ texture: Engine.windTex }, { buffer: Staging, bytesPerRow: RowBytes, rowsPerImage: Rows }, [Columns, Rows, Layers]);
        Device.queue.submit([Encoder.finish()]);
        Staging.mapAsync(GPUMapMode.READ).then(() =>
        {
            const Halves = new Uint16Array(Staging.getMappedRange());
            const RowHalves = RowBytes / 2;
            for (let Layer = 0; Layer < Layers; ++Layer)
            {
                for (let Row = 0; Row < Rows; ++Row)
                {
                    const RowStart = (Layer * Rows + Row) * RowHalves;
                    for (let Column = 0; Column < Columns; ++Column)
                    {
                        const Target = ((Layer * Rows + Row) * Columns + Column) * 3;
                        Vectors[Target] = DecodeHalf(Halves[RowStart + Column * 4]);
                        Vectors[Target + 1] = DecodeHalf(Halves[RowStart + Column * 4 + 1]);
                        Vectors[Target + 2] = DecodeHalf(Halves[RowStart + Column * 4 + 2]);
                    }
                }
            }
            Staging.unmap();
            Status.Pending = false;
            Status.Ready = true;
            Status.Count++;
            Status.Milliseconds = performance.now() - Started;
            Status.ReadAt = performance.now();
        }, () => { Status.Pending = false; });
    }

    // 📝 Trilinear, as the particles' textureSampleLevel with a linear clamp sampler: texel centres at (i + 0.5) / dim,
    //    and nothing outside the domain (sampleWind returns zero there, so the overlay leaves it transparent).
    function Sample(X, Y, Z, Out)
    {
        const Minimum = PE.WIND.min, Size = PE.WIND.size;
        const U = (X - Minimum[0]) / Size[0], V = (Y - Minimum[1]) / Size[1], W = (Z - Minimum[2]) / Size[2];
        if (U < 0 || V < 0 || W < 0 || U > 1 || V > 1 || W > 1) return false;
        if (!Status.Ready) return true;
        const FX = Math.min(Columns - 1, Math.max(0, U * Columns - 0.5)), FY = Math.min(Rows - 1, Math.max(0, V * Rows - 0.5)), FZ = Math.min(Layers - 1, Math.max(0, W * Layers - 0.5));
        const X0 = Math.min(Columns - 2, Math.floor(FX)), Y0 = Math.min(Rows - 2, Math.floor(FY)), Z0 = Math.min(Layers - 2, Math.floor(FZ));
        const SX = FX - X0, SY = FY - Y0, SZ = FZ - Z0;
        for (let Channel = 0; Channel < 3; ++Channel)
        {
            const At = (Column, Row, Layer) => Vectors[((Layer * Rows + Row) * Columns + Column) * 3 + Channel];
            const Lower = (At(X0, Y0, Z0) * (1 - SX) + At(X0 + 1, Y0, Z0) * SX) * (1 - SY) + (At(X0, Y0 + 1, Z0) * (1 - SX) + At(X0 + 1, Y0 + 1, Z0) * SX) * SY;
            const Upper = (At(X0, Y0, Z0 + 1) * (1 - SX) + At(X0 + 1, Y0, Z0 + 1) * SX) * (1 - SY) + (At(X0, Y0 + 1, Z0 + 1) * (1 - SX) + At(X0 + 1, Y0 + 1, Z0 + 1) * SX) * SY;
            Out[Channel] += Lower * (1 - SZ) + Upper * SZ;
        }
        return true;
    }

    function Release() { Staging.destroy(); }

    return { Request, Sample, Release, Status, Vectors };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                ACCELERATION FIELDS
//------------------------------------------------------------------------------------------------------------------------

// Matches shaders.js evalFields() 4-vec4 layout (0 Attract / 1 Repel / 2 Lift / 3 Magnetic / 4 Orbit / 5 Gravity / 6 Drag)
export function EvaluateFieldAcceleration(Fields, X, Y, Z, Out)
{
    if (!Fields) return;
    const Count = Math.min(Fields[0] | 0, 16);
    for (let Index = 0; Index < Count; ++Index)
    {
        const A = (1 + 4 * Index) * 4, B = A + 4, C = A + 8, D = A + 12;
        if (Fields[D + 3] < 0.5) continue;
        const Cx = Fields[A], Cy = Fields[A + 1], Cz = Fields[A + 2], Radius = Math.max(Fields[A + 3], 1e-4);
        const Type = Fields[B] | 0, Strength = Fields[B + 1], Swirl = Fields[B + 2];
        if (Type === 6) continue;
        const ReachType = Fields[C] | 0, FalloffType = Fields[C + 1] | 0, BearingRad = Fields[C + 2], Height = Math.max(0.5, Fields[C + 3]);
        const Dx = X - Cx, Dy = Y - Cy, Dz = Z - Cz;
        let Share = 0;
        const Falls = (FType, Dist, MaxR) =>
        {
            if (FType === 3) return Dist <= MaxR ? 1 : 0;
            const Q = Math.min(Math.max(Dist / Math.max(MaxR, 1e-4), 0), 1);
            if (Q >= 1) return 0;
            if (FType === 1) return 1 - Q;
            if (FType === 2) return (1 - Q * Q) / (1 + 8 * Q * Q);
            const S = 1 - Q * Q;
            return S * S;
        };
        if (ReachType === 0)
        {
            const Sep = Math.hypot(Dx, Dy, Dz);
            if (Sep < Radius) Share = Falls(FalloffType, Sep, Radius);
        }
        else if (ReachType === 1)
        {
            if (Dy >= -Math.min(15, Height * 0.5) && Dy <= Height * 1.3)
            {
                const SepXZ = Math.hypot(Dx, Dz);
                if (SepXZ < Radius)
                {
                    let Taper = 1;
                    if (Dy > Height) { const T = Math.min(1, Math.max(0, (Dy - Height) / (0.3 * Height))); Taper = 1 - T * T * (3 - 2 * T); }
                    Share = Falls(FalloffType, SepXZ, Radius) * Taper;
                }
            }
        }
        else
        {
            const Cs = Math.cos(BearingRad), Sn = Math.sin(BearingRad);
            const Lx = Dx * Cs + Dz * Sn, Lz = -Dx * Sn + Dz * Cs + Fields[D + 2];
            const Sy = Dy < 0 ? -Dy / Math.max(1.5, Height * 0.5) : Dy / Math.max(1, Height);
            const Q = Math.max(Math.abs(Lx) / Math.max(0.25, Fields[D]), Math.abs(Lz) / Math.max(0.25, Fields[D + 1]), Sy);
            if (Q < 1) Share = Falls(FalloffType, Q, 1);
        }
        if (Share <= 0) continue;
        const Scale = Strength * Share;
        if (Type === 2) { Out[1] += Scale; continue; }
        if (Type === 5) { Out[1] -= Scale; continue; }
        const Sep = Math.max(1e-3, Math.hypot(Dx, Dy, Dz));
        const OutX = Dx / Sep, OutY = Dy / Sep, OutZ = Dz / Sep;
        if (Type === 1) { Out[0] += OutX * Scale; Out[1] += OutY * Scale; Out[2] += OutZ * Scale; continue; }
        if (Type === 3)
        {
            const Clipped = Math.max(Sep, 0.4);
            const UnitX = Dx / Clipped, UnitY = Dy / Clipped, UnitZ = Dz / Clipped;
            const FieldX = 3 * UnitZ * UnitX, FieldY = 3 * UnitZ * UnitY, FieldZ = 3 * UnitZ * UnitZ - 1;
            const Length = Math.hypot(FieldX, FieldY, FieldZ) || 1;
            Out[0] += FieldX / Length * Scale; Out[1] += FieldY / Length * Scale; Out[2] += FieldZ / Length * Scale;
            continue;
        }
        const TLen = Math.hypot(OutZ, OutX) || 1;
        const Tx = -OutZ / TLen, Tz = OutX / TLen;
        if (Type === 4) { Out[0] += Tx * Scale; Out[2] += Tz * Scale; continue; }
        const Turn = Swirl * Share;
        Out[0] += -OutX * Scale + Tx * Turn;
        Out[1] += -OutY * Scale;
        Out[2] += -OutZ * Scale + Tz * Turn;
    }
}

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

// 📝 shaders.js fieldAccel, line for line, on the same packed array the engine uploads (fields[0] = count, time; three vec4
//    per field: centre + radius, type + strength + swirl + swallow, start + duration + period). The particle velocity is
//    taken as zero, so the magnetic dipole shows its drive along the field lines without the cross-line damping.
export function EvaluateFieldAcceleration(Fields, X, Y, Z, Out)
{
    if (!Fields) return;
    const Count = Fields[0] | 0, Now = Fields[1];
    for (let Index = 0; Index < Count; ++Index)
    {
        const A = (1 + 3 * Index) * 4, B = A + 4, Window = A + 8;
        const Since = Now - Fields[Window];
        if (Since < 0) continue;
        if (Fields[Window + 2] > 0 ? (Since % Fields[Window + 2]) >= Fields[Window + 1] : Since >= Fields[Window + 1]) continue;
        const DeltaX = Fields[A] - X, DeltaY = Fields[A + 1] - Y, DeltaZ = Fields[A + 2] - Z;
        const Separation = Math.sqrt(DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ);
        const Everywhere = Fields[A + 3] <= 0;
        if (!Everywhere && Separation >= Fields[A + 3]) continue;
        const Share = Everywhere ? 1 : Math.pow(Math.min(1, Math.max(0, 1 - Separation / Math.max(Fields[A + 3], 1e-3))), 2);
        const Inverse = 1 / Math.max(Separation, 1e-3);
        const DirectionX = DeltaX * Inverse, DirectionY = DeltaY * Inverse, DirectionZ = DeltaZ * Inverse;
        const Type = Fields[B] | 0, Strength = Fields[B + 1], Swirl = Fields[B + 2];
        if (Type === 0)
        {
            let TangentX = DirectionZ + 1e-5, TangentZ = -DirectionX;
            const Length = Math.hypot(TangentX, TangentZ) || 1;
            TangentX /= Length; TangentZ /= Length;
            Out[0] += (DirectionX * Strength + TangentX * Swirl) * Share;
            Out[1] += DirectionY * Strength * Share;
            Out[2] += (DirectionZ * Strength + TangentZ * Swirl) * Share;
        }
        else if (Type === 1)
        {
            Out[0] -= DirectionX * Strength * Share;
            Out[1] -= DirectionY * Strength * Share;
            Out[2] -= DirectionZ * Strength * Share;
        }
        else if (Type === 2) Out[1] += Strength * Share;
        else
        {
            const RelativeX = -DeltaX, RelativeY = -DeltaY, RelativeZ = -DeltaZ;
            const Clipped = Math.max(Math.hypot(RelativeX, RelativeY, RelativeZ), 0.4);
            const UnitX = RelativeX / Clipped, UnitY = RelativeY / Clipped, UnitZ = RelativeZ / Clipped;
            const FieldX = 3 * UnitZ * UnitX, FieldY = 3 * UnitZ * UnitY, FieldZ = 3 * UnitZ * UnitZ - 1;
            const Length = Math.hypot(FieldX, FieldY, FieldZ) || 1;
            Out[0] += FieldX / Length * Strength * Share;
            Out[1] += FieldY / Length * Strength * Share;
            Out[2] += FieldZ / Length * Strength * Share;
        }
    }
}

/*==========================================================================================================================================
                                                         SDFMESHERANDCODEC.JS
==========================================================================================================================================*/
// Surface Nets iso-surface mesh extractor + Frontier native "SDF1" (.sdfspace) IEEE-754 Float16 binary codec +
// Wavefront .OBJ exporter + Binary .STL exporter + Frontier .sdfsculpt.toml / .json scene serialization.

import {
    Clamp,
    Vec3Sub,
    Vec3Cross,
    Vec3Dot,
    Vec3Normalize,
    EncodeFloat16,
    DecodeFloat16,
    LinearToDisplay,
    SHAPE_NAMES,
    BOOL_OP_NAMES
} from './SdfMath.js';

const CUBE_CORNERS = [
    [0, 0, 0], [1, 0, 0], [0, 1, 0], [1, 1, 0],
    [0, 0, 1], [1, 0, 1], [0, 1, 1], [1, 1, 1]
];

const CUBE_EDGES = [
    [0, 1], [2, 3], [4, 5], [6, 7], // X edges
    [0, 2], [1, 3], [4, 6], [5, 7], // Y edges
    [0, 4], [1, 5], [2, 6], [3, 7]  // Z edges
];

/**
 * Extracts a smooth triangle mesh from the composite Hybrid SDF (Volume + CSG) via Dual Surface Nets.
 */
export function ExtractSurfaceNetsMesh(Volume, CsgNodes = [], Options = {})
{
    const Res = Clamp(Options.resolution || Math.min(Volume.nx, 80), 24, 128);
    const Iso = Number(Options.isoLevel) || 0.0;
    const VolumeVisible = Options.volumeVisible !== false;
    const AdditiveCsg = (CsgNodes || []).filter(N => N && N.enabled !== false && ((N.op ?? 0) === 0 || (N.op ?? 0) === 3));
    const UnionBox = (AdditiveCsg.length > 0 && Volume.computeSceneBounds)
        ? Volume.computeSceneBounds(AdditiveCsg, 0.12)
        : { min: Volume.min, max: Volume.max };
    const MinB = UnionBox.min;
    const MaxB = UnionBox.max;
    const SpanX = MaxB[0] - MinB[0], SpanY = MaxB[1] - MinB[1], SpanZ = MaxB[2] - MinB[2];
    const StepX = SpanX / (Res - 1), StepY = SpanY / (Res - 1), StepZ = SpanZ / (Res - 1);

    // 1. Sample scalar field across Res^3 lattice (enforcing closed positive outer shell so mesh never clips open)
    const Total = Res * Res * Res;
    const Field = new Float32Array(Total);
    const Tmp = [0, 0, 0, 0];
    for (let Z = 0; Z < Res; ++Z)
    {
        const Wz = MinB[2] + Z * StepZ;
        const BorderZ = (Z === 0 || Z === Res - 1);
        for (let Y = 0; Y < Res; ++Y)
        {
            const Wy = MinB[1] + Y * StepY;
            const BorderZY = BorderZ || (Y === 0 || Y === Res - 1);
            let Idx = (Z * Res + Y) * Res;
            for (let X = 0; X < Res; ++X, ++Idx)
            {
                if (BorderZY || X === 0 || X === Res - 1)
                {
                    Field[Idx] = Math.max(0.01, StepX * 0.5);
                    continue;
                }
                const Wx = MinB[0] + X * StepX;
                Field[Idx] = Volume.evaluateHybridPoint(Wx, Wy, Wz, CsgNodes, VolumeVisible, Tmp)[0] - Iso;
            }
        }
    }

    // 2. Place dual vertex in every sign-changing cell (0 .. Res-2)^3
    const CellDim = Res - 1;
    const CellVertIndex = new Int32Array(CellDim * CellDim * CellDim);
    CellVertIndex.fill(-1);

    const Positions = [];
    const Normals = [];
    const Colors = [];

    const CornerVals = new Float32Array(8);
    for (let Z = 0; Z < CellDim; ++Z)
    {
        for (let Y = 0; Y < CellDim; ++Y)
        {
            for (let X = 0; X < CellDim; ++X)
            {
                let Mask = 0;
                for (let C = 0; C < 8; ++C)
                {
                    const Off = CUBE_CORNERS[C];
                    const V = Field[((Z + Off[2]) * Res + (Y + Off[1])) * Res + (X + Off[0])];
                    CornerVals[C] = V;
                    if (V < 0.0) Mask |= (1 << C);
                }
                if (Mask === 0 || Mask === 255) continue;

                let SumX = 0, SumY = 0, SumZ = 0, Crossings = 0;
                for (let E = 0; E < 12; ++E)
                {
                    const [A, B] = CUBE_EDGES[E];
                    const Va = CornerVals[A], Vb = CornerVals[B];
                    if ((Va < 0.0) === (Vb < 0.0)) continue;
                    const T = Va / (Va - Vb);
                    const Ca = CUBE_CORNERS[A], Cb = CUBE_CORNERS[B];
                    SumX += X + Ca[0] + (Cb[0] - Ca[0]) * T;
                    SumY += Y + Ca[1] + (Cb[1] - Ca[1]) * T;
                    SumZ += Z + Ca[2] + (Cb[2] - Ca[2]) * T;
                    Crossings++;
                }
                if (Crossings === 0) continue;

                const Wx = MinB[0] + (SumX / Crossings) * StepX;
                const Wy = MinB[1] + (SumY / Crossings) * StepY;
                const Wz = MinB[2] + (SumZ / Crossings) * StepZ;

                const VIdx = Positions.length / 3;
                CellVertIndex[(Z * CellDim + Y) * CellDim + X] = VIdx;
                Positions.push(Wx, Wy, Wz);

                const N = Volume.evaluateHybridNormal(Wx, Wy, Wz, CsgNodes, VolumeVisible);
                Normals.push(N[0], N[1], N[2]);

                Volume.evaluateHybridPoint(Wx, Wy, Wz, CsgNodes, VolumeVisible, Tmp);
                Colors.push(Tmp[1], Tmp[2], Tmp[3]);
            }
        }
    }

    // 3. Emit quads (2 triangles) across interior grid edges where sign changes
    const Indices = [];
    const CellAt = (Cx, Cy, Cz) => CellVertIndex[(Cz * CellDim + Cy) * CellDim + Cx];

    const EmitTriangleChecked = (I0, I1, I2) =>
    {
        const P0 = [Positions[I0 * 3], Positions[I0 * 3 + 1], Positions[I0 * 3 + 2]];
        const P1 = [Positions[I1 * 3], Positions[I1 * 3 + 1], Positions[I1 * 3 + 2]];
        const P2 = [Positions[I2 * 3], Positions[I2 * 3 + 1], Positions[I2 * 3 + 2]];
        const FaceN = Vec3Cross(Vec3Sub(P1, P0), Vec3Sub(P2, P0));
        const AvgGrad = [
            Normals[I0 * 3] + Normals[I1 * 3] + Normals[I2 * 3],
            Normals[I0 * 3 + 1] + Normals[I1 * 3 + 1] + Normals[I2 * 3 + 1],
            Normals[I0 * 3 + 2] + Normals[I1 * 3 + 2] + Normals[I2 * 3 + 2]
        ];
        if (Vec3Dot(FaceN, AvgGrad) >= 0) Indices.push(I0, I1, I2);
        else Indices.push(I0, I2, I1);
    };

    const EmitQuad = (C0, C1, C2, C3) =>
    {
        if (C0 < 0 || C1 < 0 || C2 < 0 || C3 < 0) return;
        EmitTriangleChecked(C0, C1, C2);
        EmitTriangleChecked(C0, C2, C3);
    };

    for (let Z = 1; Z < CellDim; ++Z)
    {
        for (let Y = 1; Y < CellDim; ++Y)
        {
            for (let X = 1; X < CellDim; ++X)
            {
                const F0 = Field[(Z * Res + Y) * Res + X];
                // X-edge from (X,Y,Z) -> (X+1,Y,Z)
                const Fx = Field[(Z * Res + Y) * Res + (X + 1)];
                if ((F0 < 0) !== (Fx < 0))
                {
                    EmitQuad(
                        CellAt(X, Y - 1, Z - 1),
                        CellAt(X, Y, Z - 1),
                        CellAt(X, Y, Z),
                        CellAt(X, Y - 1, Z)
                    );
                }
                // Y-edge from (X,Y,Z) -> (X,Y+1,Z)
                const Fy = Field[(Z * Res + (Y + 1)) * Res + X];
                if ((F0 < 0) !== (Fy < 0))
                {
                    EmitQuad(
                        CellAt(X - 1, Y, Z - 1),
                        CellAt(X - 1, Y, Z),
                        CellAt(X, Y, Z),
                        CellAt(X, Y, Z - 1)
                    );
                }
                // Z-edge from (X,Y,Z) -> (X,Y,Z+1)
                const Fz = Field[((Z + 1) * Res + Y) * Res + X];
                if ((F0 < 0) !== (Fz < 0))
                {
                    EmitQuad(
                        CellAt(X - 1, Y - 1, Z),
                        CellAt(X, Y - 1, Z),
                        CellAt(X, Y, Z),
                        CellAt(X - 1, Y, Z)
                    );
                }
            }
        }
    }

    // 4. Compute exact mesh surface area, signed volume, and bounds
    let AreaM2 = 0.0;
    let SignedVolM3 = 0.0;
    const BoundsMin = [Infinity, Infinity, Infinity];
    const BoundsMax = [-Infinity, -Infinity, -Infinity];
    const VertexCount = Positions.length / 3;
    const TriangleCount = Indices.length / 3;

    for (let V = 0; V < VertexCount; ++V)
    {
        for (let K = 0; K < 3; ++K)
        {
            const Val = Positions[V * 3 + K];
            if (Val < BoundsMin[K]) BoundsMin[K] = Val;
            if (Val > BoundsMax[K]) BoundsMax[K] = Val;
        }
    }

    for (let T = 0; T < TriangleCount; ++T)
    {
        const I0 = Indices[T * 3] * 3, I1 = Indices[T * 3 + 1] * 3, I2 = Indices[T * 3 + 2] * 3;
        const P0 = [Positions[I0], Positions[I0 + 1], Positions[I0 + 2]];
        const P1 = [Positions[I1], Positions[I1 + 1], Positions[I1 + 2]];
        const P2 = [Positions[I2], Positions[I2 + 1], Positions[I2 + 2]];
        const Cross = Vec3Cross(Vec3Sub(P1, P0), Vec3Sub(P2, P0));
        AreaM2 += 0.5 * Math.hypot(Cross[0], Cross[1], Cross[2]);
        SignedVolM3 += Vec3Dot(P0, Vec3Cross(P1, P2)) / 6.0;
    }

    return {
        positions: new Float32Array(Positions),
        normals: new Float32Array(Normals),
        colors: new Float32Array(Colors),
        indices: new Uint32Array(Indices),
        vertexCount: VertexCount,
        triangleCount: TriangleCount,
        areaM2: AreaM2,
        volumeLiters: Math.abs(SignedVolM3) * 1000.0,
        boundsMin: VertexCount ? BoundsMin : [0, 0, 0],
        boundsMax: VertexCount ? BoundsMax : [0, 0, 0],
        resolution: Res
    };
}

/**
 * Encodes composite Hybrid SDF (or pure Clay Volume) into Frontier's native "SDF1" (.sdfspace) binary container:
 * 68-byte little-endian SdfSpaceHeaderV1 + Nx*Ny*Nz IEEE-754 Float16 distances.
 */
export function EncodeSdfSpaceBinary(Volume, CsgNodes = [], IncludeCsg = true)
{
    const Nx = Volume.nx, Ny = Volume.ny, Nz = Volume.nz;
    const VoxelCount = Nx * Ny * Nz;
    const HeaderBytes = 68;
    const Buffer = new ArrayBuffer(HeaderBytes + VoxelCount * 2);
    const View = new DataView(Buffer);
    const Payload = new Uint16Array(Buffer, HeaderBytes, VoxelCount);

    let MinDist = Infinity, MaxDist = -Infinity;
    const Tmp = [0, 0, 0, 0];
    const UseCsg = IncludeCsg && CsgNodes && CsgNodes.some(N => N && N.enabled !== false);

    let Idx = 0;
    for (let Z = 0; Z < Nz; ++Z)
    {
        const Wz = Volume.gridToWorldZ(Z);
        for (let Y = 0; Y < Ny; ++Y)
        {
            const Wy = Volume.gridToWorldY(Y);
            for (let X = 0; X < Nx; ++X, ++Idx)
            {
                const D = UseCsg
                    ? Volume.evaluateHybridPoint(Volume.gridToWorldX(X), Wy, Wz, CsgNodes, true, Tmp)[0]
                    : Volume.distances[Idx];
                if (D < MinDist) MinDist = D;
                if (D > MaxDist) MaxDist = D;
                Payload[Idx] = EncodeFloat16(D);
            }
        }
    }
    if (!Number.isFinite(MinDist)) { MinDist = 0; MaxDist = 0; }

    // Magic "SDF1"
    View.setUint8(0, 0x53); // 'S'
    View.setUint8(1, 0x44); // 'D'
    View.setUint8(2, 0x46); // 'F'
    View.setUint8(3, 0x31); // '1'
    View.setUint32(4, 1, true);   // Version = 1
    View.setUint32(8, Nx, true);
    View.setUint32(12, Ny, true);
    View.setUint32(16, Nz, true);
    const ClosedManifold = MinDist < 0.0 ? 2 : 0;
    View.setUint32(20, 1 | ClosedManifold | 4, true); // Signed | ClosedManifold | Float16Payload

    View.setFloat32(24, Volume.min[0], true);
    View.setFloat32(28, Volume.min[1], true);
    View.setFloat32(32, Volume.min[2], true);
    View.setFloat32(36, Volume.max[0], true);
    View.setFloat32(40, Volume.max[1], true);
    View.setFloat32(44, Volume.max[2], true);
    View.setFloat32(48, Volume.step[0], true);
    View.setFloat32(52, Volume.step[1], true);
    View.setFloat32(56, Volume.step[2], true);
    View.setFloat32(60, MinDist, true);
    View.setFloat32(64, MaxDist, true);

    return new Uint8Array(Buffer);
}

/**
 * Decodes a Frontier "SDF1" (.sdfspace) binary container into an SdfVolume instance.
 */
export function DecodeSdfSpaceBinary(Bytes, TargetVolume)
{
    const U8 = Bytes instanceof Uint8Array ? Bytes : new Uint8Array(Bytes);
    if (U8.byteLength < 68) throw new Error('Invalid .sdfspace file: header truncated (< 68 bytes).');
    const View = new DataView(U8.buffer, U8.byteOffset, U8.byteLength);
    const Magic = String.fromCharCode(View.getUint8(0), View.getUint8(1), View.getUint8(2), View.getUint8(3));
    const Version = View.getUint32(4, true);
    if (Magic !== 'SDF1' || Version !== 1)
    {
        throw new Error(`Unsupported .sdfspace container header (${Magic} v${Version}).`);
    }

    const Nx = View.getUint32(8, true);
    const Ny = View.getUint32(12, true);
    const Nz = View.getUint32(16, true);
    const Flags = View.getUint32(20, true);
    const IsHalf = (Flags & 4) !== 0;
    const VoxelCount = Nx * Ny * Nz;
    const PayloadBytes = VoxelCount * (IsHalf ? 2 : 4);
    if (U8.byteLength < 68 + PayloadBytes)
    {
        throw new Error('Truncated .sdfspace voxel payload.');
    }

    const MinX = View.getFloat32(24, true), MinY = View.getFloat32(28, true), MinZ = View.getFloat32(32, true);
    const MaxX = View.getFloat32(36, true), MaxY = View.getFloat32(40, true), MaxZ = View.getFloat32(44, true);
    const SpanX = MaxX - MinX, SpanY = MaxY - MinY, SpanZ = MaxZ - MinZ;
    const IsCubicMetric = (
        Nx === Ny && Ny === Nz &&
        Nx >= 32 && Nx <= 160 &&
        SpanX >= 1.0 && SpanX <= 16.0 &&
        Math.abs(SpanX - SpanY) < 1e-2 && Math.abs(SpanX - SpanZ) < 1e-2
    );

    const CubicN = Clamp(Math.max(Nx, Ny, Nz), 32, 128);
    if (IsCubicMetric)
    {
        TargetVolume.allocate(Nx, [MinX, MinY, MinZ], [MaxX, MaxY, MaxZ]);
    }
    else
    {
        TargetVolume.allocate(CubicN, [-2.0, -2.0, -2.0], [2.0, 2.0, 2.0]);
    }

    const RawDist = new Float32Array(VoxelCount);
    if (IsHalf)
    {
        for (let I = 0; I < VoxelCount; ++I)
        {
            RawDist[I] = DecodeFloat16(View.getUint16(68 + I * 2, true));
        }
    }
    else
    {
        for (let I = 0; I < VoxelCount; ++I)
        {
            RawDist[I] = View.getFloat32(68 + I * 4, true);
        }
    }

    if (IsCubicMetric)
    {
        TargetVolume.distances.set(RawDist);
    }
    else
    {
        // Fit archive smoothly inside [-1.6, 1.6]^3 stage while preserving true metric scale
        const SrcSpan = Math.max(1e-4, MaxX - MinX, MaxY - MinY, MaxZ - MinZ);
        const Scale = 2.8 / SrcSpan;
        const Cx = 0.5 * (MinX + MaxX), Cy = 0.5 * (MinY + MaxY), Cz = 0.5 * (MinZ + MaxZ);
        const StepX = (MaxX - MinX) / Nx, StepY = (MaxY - MinY) / Ny, StepZ = (MaxZ - MinZ) / Nz;
        const TempSrc = {
            nx: Nx, ny: Ny, nz: Nz,
            min: [MinX, MinY, MinZ],
            max: [MaxX, MaxY, MaxZ],
            step: [StepX, StepY, StepZ],
            distances: RawDist
        };
        const SampleSrc = TargetVolume.sampleDistance.bind(TempSrc);
        for (let Z = 0; Z < CubicN; ++Z)
        {
            const Wz = TargetVolume.gridToWorldZ(Z) / Scale + Cz;
            for (let Y = 0; Y < CubicN; ++Y)
            {
                const Wy = TargetVolume.gridToWorldY(Y) / Scale + Cy;
                let Idx = (Z * CubicN + Y) * CubicN;
                for (let X = 0; X < CubicN; ++X, ++Idx)
                {
                    const Wx = TargetVolume.gridToWorldX(X) / Scale + Cx;
                    TargetVolume.distances[Idx] = SampleSrc(Wx, Wy, Wz) * Scale;
                }
            }
        }
    }
    TargetVolume.markAllDirty();
    return { nx: Nx, ny: Ny, nz: Nz, voxelCount: VoxelCount };
}

/**
 * Exports extracted mesh to Wavefront .OBJ text (including vertex normals and sRGB vertex colors).
 */
export function ExportMeshObj(Mesh, Name = 'FrontierSdfSculpt')
{
    const Lines = [
        `# Frontier Experimental Remastered — SDF Sculpt Studio`,
        `# Vertices: ${Mesh.vertexCount} | Triangles: ${Mesh.triangleCount} | Volume: ${Mesh.volumeLiters.toFixed(2)} L`,
        `o ${Name.replace(/\s+/g, '_')}`
    ];
    const P = Mesh.positions, N = Mesh.normals, C = Mesh.colors, I = Mesh.indices;
    for (let V = 0; V < Mesh.vertexCount; ++V)
    {
        const R = Clamp(LinearToDisplay(C[V * 3]), 0, 1);
        const G = Clamp(LinearToDisplay(C[V * 3 + 1]), 0, 1);
        const B = Clamp(LinearToDisplay(C[V * 3 + 2]), 0, 1);
        Lines.push(`v ${P[V * 3].toFixed(5)} ${P[V * 3 + 1].toFixed(5)} ${P[V * 3 + 2].toFixed(5)} ${R.toFixed(4)} ${G.toFixed(4)} ${B.toFixed(4)}`);
    }
    for (let V = 0; V < Mesh.vertexCount; ++V)
    {
        Lines.push(`vn ${N[V * 3].toFixed(5)} ${N[V * 3 + 1].toFixed(5)} ${N[V * 3 + 2].toFixed(5)}`);
    }
    for (let T = 0; T < Mesh.triangleCount; ++T)
    {
        const A = I[T * 3] + 1, B = I[T * 3 + 1] + 1, D = I[T * 3 + 2] + 1;
        Lines.push(`f ${A}//${A} ${B}//${B} ${D}//${D}`);
    }
    return Lines.join('\n') + '\n';
}

/**
 * Exports extracted mesh to standard Binary .STL Uint8Array.
 */
export function ExportMeshBinaryStl(Mesh, HeaderLabel = 'Frontier SDF Sculpt Studio')
{
    const TriCount = Mesh.triangleCount;
    const Buffer = new ArrayBuffer(84 + TriCount * 50);
    const View = new DataView(Buffer);
    for (let K = 0; K < Math.min(79, HeaderLabel.length); ++K)
    {
        View.setUint8(K, HeaderLabel.charCodeAt(K) & 0x7F);
    }
    View.setUint32(80, TriCount, true);

    const P = Mesh.positions, I = Mesh.indices;
    let Off = 84;
    for (let T = 0; T < TriCount; ++T)
    {
        const I0 = I[T * 3] * 3, I1 = I[T * 3 + 1] * 3, I2 = I[T * 3 + 2] * 3;
        const P0 = [P[I0], P[I0 + 1], P[I0 + 2]];
        const P1 = [P[I1], P[I1 + 1], P[I1 + 2]];
        const P2 = [P[I2], P[I2 + 1], P[I2 + 2]];
        const Fn = Vec3Normalize(Vec3Cross(Vec3Sub(P1, P0), Vec3Sub(P2, P0)));

        View.setFloat32(Off + 0, Fn[0], true);
        View.setFloat32(Off + 4, Fn[1], true);
        View.setFloat32(Off + 8, Fn[2], true);

        View.setFloat32(Off + 12, P0[0], true);
        View.setFloat32(Off + 16, P0[1], true);
        View.setFloat32(Off + 20, P0[2], true);

        View.setFloat32(Off + 24, P1[0], true);
        View.setFloat32(Off + 28, P1[1], true);
        View.setFloat32(Off + 32, P1[2], true);

        View.setFloat32(Off + 36, P2[0], true);
        View.setFloat32(Off + 40, P2[1], true);
        View.setFloat32(Off + 44, P2[2], true);
        View.setUint16(Off + 48, 0, true);
        Off += 50;
    }
    return new Uint8Array(Buffer);
}

/**
 * Generates a Frontier-style .sdfsculpt.toml scene manifest describing the Sculpt Volume, Material, Lighting & CSG stack.
 */
export function SerializeSceneToml(SceneState, Telemetry)
{
    const F = (V, D = 3) => Number(V || 0).toFixed(D);
    const Vec = (A, D = 3) => `[${F(A?.[0], D)}, ${F(A?.[1], D)}, ${F(A?.[2], D)}]`;
    const Lines = [
        `# Frontier · SDF Sculpt Studio Scene Specification (.sdfsculpt.toml)`,
        `# Compatible with VolumeDistanceArchive ("SDF1" IEEE-754 Float16 .sdfspace)`,
        ``,
        `[workspace]`,
        `name = ${JSON.stringify(SceneState.name || 'Untitled Sculpt')}`,
        `lens = ${JSON.stringify(SceneState.stage.lens)}`,
        `symmetry_axes = [${SceneState.symmetry.x ? '"X"' : ''}${SceneState.symmetry.x && SceneState.symmetry.y ? ', ' : ''}${SceneState.symmetry.y ? '"Y"' : ''}${(SceneState.symmetry.x || SceneState.symmetry.y) && SceneState.symmetry.z ? ', ' : ''}${SceneState.symmetry.z ? '"Z"' : ''}]`,
        `radial_symmetry = ${SceneState.symmetry.radial | 0}`,
        ``,
        `[volume]`,
        `dimensions = [${SceneState.volumeRes || SceneState.volume?.nx || 96}, ${SceneState.volumeRes || SceneState.volume?.nx || 96}, ${SceneState.volumeRes || SceneState.volume?.nx || 96}]`,
        `bounds_mode = ${JSON.stringify(SceneState.volume?.boundsMode || 'expand')}`,
        `bounds_min = ${Vec(SceneState.volume?.min || [-2, -2, -2])}`,
        `bounds_max = ${Vec(SceneState.volume?.max || [2, 2, 2])}`,
        `visible = ${SceneState.volumeVisible !== false}`,
        `occupied_voxels = ${Telemetry?.occupiedVoxels ?? 0}`,
        `iso_volume_liters = ${F(Telemetry?.liters ?? 0, 2)}`,
        `mean_eikonal_gradient = ${F(Telemetry?.meanEikonal ?? 1, 3)}`,
        ``,
        `[material]`,
        `preset = ${JSON.stringify(SceneState.material.preset)}`,
        `roughness = ${F(SceneState.material.roughness, 2)}`,
        `metallic = ${F(SceneState.material.metallic, 2)}`,
        `subsurface_wrap = ${F(SceneState.material.subsurface, 2)}`,
        `cavity_strength = ${F(SceneState.material.cavity, 2)}`,
        `sun_azimuth_deg = ${F(SceneState.stage.sunAzimuth, 1)}`,
        `sun_elevation_deg = ${F(SceneState.stage.sunElevation, 1)}`,
        ``
    ];

    for (const Node of SceneState.csgNodes || [])
    {
        Lines.push(`[[csg_node]]`);
        Lines.push(`id = ${JSON.stringify(Node.id)}`);
        Lines.push(`name = ${JSON.stringify(Node.name)}`);
        Lines.push(`shape = ${JSON.stringify(SHAPE_NAMES[Node.shape] || 'Sphere')}`);
        Lines.push(`operation = ${JSON.stringify(BOOL_OP_NAMES[Node.op] || 'Smooth Union')}`);
        Lines.push(`enabled = ${Node.enabled !== false}`);
        Lines.push(`position = ${Vec(Node.position)}`);
        Lines.push(`rotation_deg = ${Vec(Node.rotation, 1)}`);
        Lines.push(`size = ${Vec(Node.size)}`);
        Lines.push(`scale = ${F(Node.scale ?? 1, 3)}`);
        Lines.push(`smooth_blend = ${F(Node.blend ?? 0.06, 3)}`);
        Lines.push(`round_radius = ${F(Node.round ?? 0, 3)}`);
        Lines.push(`shell_thickness = ${F(Node.shell ?? 0, 3)}`);
        Lines.push(`twist = ${F(Node.twist ?? 0, 3)}`);
        Lines.push(`mirror_x = ${!!Node.mirrorX}`);
        Lines.push(`radial_count = ${Node.radialCount || 1}`);
        Lines.push(`linear_albedo = ${Vec(Node.color, 3)}`);
        Lines.push(``);
    }

    return Lines.join('\n');
}

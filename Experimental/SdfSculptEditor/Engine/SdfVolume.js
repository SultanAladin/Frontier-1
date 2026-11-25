/*==========================================================================================================================================
                                                            SDFVOLUME.JS
==========================================================================================================================================*/
// Real-time 3D Volumetric SDF Sculpting Engine:
// - Trilinear Float32 distance + RGB polypaint grid over [-2.0, +2.0]^3 meters
// - Sub-millisecond dirty sub-brick IEEE-754 Float16 RGBA16F packer for WebGL2 texSubImage3D
// - 10 authentic 3D SDF sculpting brushes (Clay, Carve, Inflate, Smooth, Flatten, Pinch, Grab, Snake, Stamp, Paint)
// - X/Y/Z mirror + Radial symmetry, Undo/Redo snapshot history, Eikonal |grad d| relaxation, and CSG-to-Clay baking

import {
    Clamp,
    Mix,
    Vec3Normalize,
    Vec3Sub,
    Vec3Dot,
    EncodeFloat16,
    EvaluatePrimitiveLocal,
    EvaluateCsgNode,
    CombineBooleanInto,
    EvaluateFalloff,
    ExpandSymmetryTargets,
    EulerDegreesToQuat,
    InvertUnitQuat,
    RotateByQuat
} from './SdfMath.js';

export const DEFAULT_BOUNDS = 2.0; // Initial world cube [-2.0, +2.0]^3 m (4m x 4m x 4m stage), auto-expandable up to 12m

function SmoothMaxBoundary(A, B, K)
{
    const Sk = Math.max(1e-4, K);
    const H = Clamp(0.5 - 0.5 * (B - A) / Sk, 0.0, 1.0);
    return Mix(B, A, H) + Sk * H * (1.0 - H);
}

export class SdfVolume
{
    constructor(Resolution = 96, HalfExtent = DEFAULT_BOUNDS)
    {
        this.halfExtent = HalfExtent;
        this.min = [-HalfExtent, -HalfExtent, -HalfExtent];
        this.max = [HalfExtent, HalfExtent, HalfExtent];
        this.span = HalfExtent * 2.0;
        this.boundsMode = 'expand'; // 'expand' (elastic auto-grow) | 'soft' (fixed rounded cushion) | 'hard' (raw clip)
        this.maxAutoSpan = 12.0;    // Up to 12m x 12m x 12m workspace cube
        this.boundsRevision = 0;
        this.lastBoundsEvent = null;
        this.defaultClayColor = [0.68, 0.42, 0.31]; // Linear terracotta studio clay
        this.allocate(Resolution, this.min, this.max);

        // Undo / Redo snapshot ring
        this.history = [];
        this.historyIndex = -1;
        this.maxHistory = 18;

        // Active stroke state (used for Grab, Stamp preview, Flatten plane, continuous capsule spacing)
        this.stroke = null;
    }

    allocate(Resolution, CustomMin = this.min, CustomMax = this.max)
    {
        const N = Clamp(Resolution | 0, 32, 160);
        this.nx = N;
        this.ny = N;
        this.nz = N;
        this.voxelCount = N * N * N;
        this.min = [CustomMin[0], CustomMin[1], CustomMin[2]];
        this.max = [CustomMax[0], CustomMax[1], CustomMax[2]];
        const SpanX = Math.max(0.5, this.max[0] - this.min[0]);
        const SpanY = Math.max(0.5, this.max[1] - this.min[1]);
        const SpanZ = Math.max(0.5, this.max[2] - this.min[2]);
        this.span = Math.max(SpanX, SpanY, SpanZ);
        this.halfExtent = Math.max(
            Math.abs(this.min[0]), Math.abs(this.max[0]),
            Math.abs(this.min[1]), Math.abs(this.max[1]),
            Math.abs(this.min[2]), Math.abs(this.max[2])
        );
        this.step = [SpanX / N, SpanY / N, SpanZ / N];
        this.voxelSize = Math.max(this.step[0], this.step[1], this.step[2]);
        // Narrow-band exterior distance cap guarantees global Lipschitz |d(x)| <= dist(x, surface) without full-grid sweeps
        this.bandWidth = Math.max(0.42, this.voxelSize * 9.0);

        this.distances = new Float32Array(this.voxelCount);
        this.colors = new Float32Array(this.voxelCount * 3);
        this.clearVolume();
    }

    resetBoundsToDefault(HalfExtent = DEFAULT_BOUNDS)
    {
        this.min = [-HalfExtent, -HalfExtent, -HalfExtent];
        this.max = [HalfExtent, HalfExtent, HalfExtent];
        this.span = HalfExtent * 2.0;
        this.halfExtent = HalfExtent;
        const N = this.nx;
        this.step = [this.span / N, this.span / N, this.span / N];
        this.voxelSize = this.span / N;
        this.bandWidth = Math.max(0.42, this.voxelSize * 9.0);
        this.boundsRevision++;
    }

    clearVolume(FillColor = this.defaultClayColor)
    {
        this.distances.fill(this.bandWidth);
        const R = FillColor[0], G = FillColor[1], B = FillColor[2];
        const Col = this.colors;
        for (let I = 0; I < this.voxelCount; ++I)
        {
            const C = I * 3;
            Col[C] = R;
            Col[C + 1] = G;
            Col[C + 2] = B;
        }
        this.markAllDirty();
    }

    markAllDirty()
    {
        this.dirty = {
            x0: 0, y0: 0, z0: 0,
            x1: this.nx - 1, y1: this.ny - 1, z1: this.nz - 1
        };
        this.fullUploadNeeded = true;
        this.revision = (this.revision || 0) + 1;
    }

    expandDirty(X0, Y0, Z0, X1, Y1, Z1)
    {
        const Ax0 = Clamp(X0 | 0, 0, this.nx - 1);
        const Ay0 = Clamp(Y0 | 0, 0, this.ny - 1);
        const Az0 = Clamp(Z0 | 0, 0, this.nz - 1);
        const Ax1 = Clamp(X1 | 0, 0, this.nx - 1);
        const Ay1 = Clamp(Y1 | 0, 0, this.ny - 1);
        const Az1 = Clamp(Z1 | 0, 0, this.nz - 1);
        if (Ax1 < Ax0 || Ay1 < Ay0 || Az1 < Az0) return;

        if (!this.dirty)
        {
            this.dirty = { x0: Ax0, y0: Ay0, z0: Az0, x1: Ax1, y1: Ay1, z1: Az1 };
        }
        else
        {
            this.dirty.x0 = Math.min(this.dirty.x0, Ax0);
            this.dirty.y0 = Math.min(this.dirty.y0, Ay0);
            this.dirty.z0 = Math.min(this.dirty.z0, Az0);
            this.dirty.x1 = Math.max(this.dirty.x1, Ax1);
            this.dirty.y1 = Math.max(this.dirty.y1, Ay1);
            this.dirty.z1 = Math.max(this.dirty.z1, Az1);
        }
        this.revision = (this.revision || 0) + 1;
    }

    consumeDirtySubBrick()
    {
        if (!this.dirty) return null;
        const Box = this.dirty;
        const Full = !!this.fullUploadNeeded;
        this.dirty = null;
        this.fullUploadNeeded = false;

        const W = Box.x1 - Box.x0 + 1;
        const H = Box.y1 - Box.y0 + 1;
        const D = Box.z1 - Box.z0 + 1;
        const Count = W * H * D;
        if (!this._uploadScratch || this._uploadScratch.length < Count * 4)
        {
            this._uploadScratch = new Uint16Array(Count * 4);
        }
        const Out = this._uploadScratch;
        const Dist = this.distances;
        const Col = this.colors;
        const Nx = this.nx, Ny = this.ny;

        let Ptr = 0;
        for (let Z = Box.z0; Z <= Box.z1; ++Z)
        {
            const ZOff = Z * Ny;
            for (let Y = Box.y0; Y <= Box.y1; ++Y)
            {
                let Idx = (ZOff + Y) * Nx + Box.x0;
                for (let X = Box.x0; X <= Box.x1; ++X, ++Idx)
                {
                    const C = Idx * 3;
                    Out[Ptr++] = EncodeFloat16(Col[C]);
                    Out[Ptr++] = EncodeFloat16(Col[C + 1]);
                    Out[Ptr++] = EncodeFloat16(Col[C + 2]);
                    Out[Ptr++] = EncodeFloat16(Dist[Idx]);
                }
            }
        }

        return {
            full: Full && Box.x0 === 0 && Box.y0 === 0 && Box.z0 === 0 && W === this.nx && H === this.ny && D === this.nz,
            x0: Box.x0, y0: Box.y0, z0: Box.z0,
            width: W, height: H, depth: D,
            data: Out.subarray(0, Count * 4)
        };
    }

    worldToGridCoord(Wx, Wy, Wz)
    {
        return [
            (Wx - this.min[0]) / this.step[0] - 0.5,
            (Wy - this.min[1]) / this.step[1] - 0.5,
            (Wz - this.min[2]) / this.step[2] - 0.5
        ];
    }

    gridToWorldX(Ix) { return this.min[0] + (Ix + 0.5) * this.step[0]; }
    gridToWorldY(Iy) { return this.min[1] + (Iy + 0.5) * this.step[1]; }
    gridToWorldZ(Iz) { return this.min[2] + (Iz + 0.5) * this.step[2]; }

    /**
     * Trilinear signed distance sample matching Frontier::SampleVolumeDistanceMeters & WebGL2 TEXTURE_3D.
     */
    sampleDistance(Wx, Wy, Wz, SourceDistances = this.distances)
    {
        const Nx = this.nx, Ny = this.ny, Nz = this.nz;
        const Cx = Clamp(Wx, this.min[0], this.max[0]);
        const Cy = Clamp(Wy, this.min[1], this.max[1]);
        const Cz = Clamp(Wz, this.min[2], this.max[2]);
        const Ex = Wx - Cx, Ey = Wy - Cy, Ez = Wz - Cz;
        const Exterior = (Ex !== 0 || Ey !== 0 || Ez !== 0) ? Math.hypot(Ex, Ey, Ez) : 0.0;

        const Gx = Clamp((Cx - this.min[0]) / this.step[0] - 0.5, 0.0, Nx - 1);
        const Gy = Clamp((Cy - this.min[1]) / this.step[1] - 0.5, 0.0, Ny - 1);
        const Gz = Clamp((Cz - this.min[2]) / this.step[2] - 0.5, 0.0, Nz - 1);

        const X0 = Gx | 0, Y0 = Gy | 0, Z0 = Gz | 0;
        const X1 = Math.min(X0 + 1, Nx - 1);
        const Y1 = Math.min(Y0 + 1, Ny - 1);
        const Z1 = Math.min(Z0 + 1, Nz - 1);
        const Tx = Gx - X0, Ty = Gy - Y0, Tz = Gz - Z0;

        const Z0Off = Z0 * Ny, Z1Off = Z1 * Ny;
        const C000 = SourceDistances[(Z0Off + Y0) * Nx + X0];
        const C100 = SourceDistances[(Z0Off + Y0) * Nx + X1];
        const C010 = SourceDistances[(Z0Off + Y1) * Nx + X0];
        const C110 = SourceDistances[(Z0Off + Y1) * Nx + X1];
        const C001 = SourceDistances[(Z1Off + Y0) * Nx + X0];
        const C101 = SourceDistances[(Z1Off + Y0) * Nx + X1];
        const C011 = SourceDistances[(Z1Off + Y1) * Nx + X0];
        const C111 = SourceDistances[(Z1Off + Y1) * Nx + X1];

        const C00 = C000 + (C100 - C000) * Tx;
        const C10 = C010 + (C110 - C010) * Tx;
        const C01 = C001 + (C101 - C001) * Tx;
        const C11 = C011 + (C111 - C011) * Tx;
        const C0 = C00 + (C10 - C00) * Ty;
        const C1 = C01 + (C11 - C01) * Ty;
        const Trilinear = C0 + (C1 - C0) * Tz;

        return Exterior > 0.0 ? Math.max(Trilinear, 0.0) + Exterior : Trilinear;
    }

    /**
     * Evaluates composite hybrid SDF [dist, r, g, b] at world point (Wx, Wy, Wz) for both Volume + active CSG nodes.
     */
    evaluateHybridPoint(Wx, Wy, Wz, CsgNodes, VolumeVisible = true, Out4 = [10, 0.68, 0.42, 0.31])
    {
        if (VolumeVisible)
        {
            Out4[0] = this.sampleDistance(Wx, Wy, Wz);
            const Rgb = [0, 0, 0];
            this.sampleColorAccurateInto(Wx, Wy, Wz, Rgb);
            Out4[1] = Rgb[0];
            Out4[2] = Rgb[1];
            Out4[3] = Rgb[2];
        }
        else
        {
            Out4[0] = 10.0;
            Out4[1] = this.defaultClayColor[0];
            Out4[2] = this.defaultClayColor[1];
            Out4[3] = this.defaultClayColor[2];
        }

        if (CsgNodes && CsgNodes.length)
        {
            for (let I = 0; I < CsgNodes.length; ++I)
            {
                const Node = CsgNodes[I];
                if (!Node || Node.enabled === false) continue;
                const Nd = EvaluateCsgNode(Node, Wx, Wy, Wz);
                CombineBooleanInto(Out4, Nd, Node.color || this.defaultClayColor, Node.op ?? 0, Node.blend ?? 0.06);
            }
        }
        return Out4;
    }

    sampleColorAccurateInto(Wx, Wy, Wz, OutRgb, SourceColors = this.colors)
    {
        const Nx = this.nx, Ny = this.ny, Nz = this.nz;
        const Gx = Clamp((Wx - this.min[0]) / this.step[0] - 0.5, 0.0, Nx - 1);
        const Gy = Clamp((Wy - this.min[1]) / this.step[1] - 0.5, 0.0, Ny - 1);
        const Gz = Clamp((Wz - this.min[2]) / this.step[2] - 0.5, 0.0, Nz - 1);

        const X0 = Gx | 0, Y0 = Gy | 0, Z0 = Gz | 0;
        const X1 = Math.min(X0 + 1, Nx - 1);
        const Y1 = Math.min(Y0 + 1, Ny - 1);
        const Z1 = Math.min(Z0 + 1, Nz - 1);
        const Tx = Gx - X0, Ty = Gy - Y0, Tz = Gz - Z0;

        const I000 = ((Z0 * Ny + Y0) * Nx + X0) * 3;
        const I100 = ((Z0 * Ny + Y0) * Nx + X1) * 3;
        const I010 = ((Z0 * Ny + Y1) * Nx + X0) * 3;
        const I110 = ((Z0 * Ny + Y1) * Nx + X1) * 3;
        const I001 = ((Z1 * Ny + Y0) * Nx + X0) * 3;
        const I101 = ((Z1 * Ny + Y0) * Nx + X1) * 3;
        const I011 = ((Z1 * Ny + Y1) * Nx + X0) * 3;
        const I111 = ((Z1 * Ny + Y1) * Nx + X1) * 3;

        for (let Ch = 0; Ch < 3; ++Ch)
        {
            const C00 = SourceColors[I000 + Ch] + (SourceColors[I100 + Ch] - SourceColors[I000 + Ch]) * Tx;
            const C10 = SourceColors[I010 + Ch] + (SourceColors[I110 + Ch] - SourceColors[I010 + Ch]) * Tx;
            const C01 = SourceColors[I001 + Ch] + (SourceColors[I101 + Ch] - SourceColors[I001 + Ch]) * Tx;
            const C11 = SourceColors[I011 + Ch] + (SourceColors[I111 + Ch] - SourceColors[I011 + Ch]) * Tx;
            const V0 = C00 + (C10 - C00) * Ty;
            const V1 = C01 + (C11 - C01) * Ty;
            OutRgb[Ch] = V0 + (V1 - V0) * Tz;
        }
        return OutRgb;
    }

    /**
     * 4-tap tetrahedron gradient normal on hybrid SDF.
     */
    evaluateHybridNormal(Wx, Wy, Wz, CsgNodes, VolumeVisible = true)
    {
        const Eps = Math.max(0.004, this.voxelSize * 0.45);
        const Tmp = [0, 0, 0, 0];
        const F = (X, Y, Z) => this.evaluateHybridPoint(X, Y, Z, CsgNodes, VolumeVisible, Tmp)[0];
        const D0 = F(Wx + Eps, Wy - Eps, Wz - Eps);
        const D1 = F(Wx - Eps, Wy - Eps, Wz + Eps);
        const D2 = F(Wx - Eps, Wy + Eps, Wz - Eps);
        const D3 = F(Wx + Eps, Wy + Eps, Wz + Eps);
        return Vec3Normalize([
            D0 - D1 - D2 + D3,
            -D0 - D1 + D2 + D3,
            -D0 + D1 - D2 + D3
        ]);
    }

    /**
     * Computes union world AABB [min3, max3] covering both the Sculpt Volume and all active Parametric CSG nodes.
     */
    computeSceneBounds(CsgNodes = null, ExtraPad = 0.8)
    {
        const BMin = [this.min[0] - ExtraPad, this.min[1] - ExtraPad, this.min[2] - ExtraPad];
        const BMax = [this.max[0] + ExtraPad, this.max[1] + ExtraPad, this.max[2] + ExtraPad];
        if (CsgNodes && CsgNodes.length)
        {
            for (let I = 0; I < CsgNodes.length; ++I)
            {
                const Node = CsgNodes[I];
                if (!Node || Node.enabled === false) continue;
                const Pos = Node.position || [0, 0, 0];
                const Sz = Node.size || [0.35, 0.35, 0.35];
                const Scale = Math.max(0.05, Number(Node.scale) || 1.0);
                const R = (Math.hypot(Sz[0], Sz[1], Sz[2]) + (Number(Node.round) || 0) + (Number(Node.blend) || 0.06)) * Scale + ExtraPad;
                if (Node.mirrorX || (Node.radialCount | 0) >= 2)
                {
                    const RadSpan = Math.hypot(Pos[0], Pos[2]) + R;
                    BMin[0] = Math.min(BMin[0], -RadSpan);
                    BMax[0] = Math.max(BMax[0], RadSpan);
                    BMin[2] = Math.min(BMin[2], -RadSpan);
                    BMax[2] = Math.max(BMax[2], RadSpan);
                    BMin[1] = Math.min(BMin[1], Pos[1] - R);
                    BMax[1] = Math.max(BMax[1], Pos[1] + R);
                }
                else
                {
                    BMin[0] = Math.min(BMin[0], Pos[0] - R);
                    BMax[0] = Math.max(BMax[0], Pos[0] + R);
                    BMin[1] = Math.min(BMin[1], Pos[1] - R);
                    BMax[1] = Math.max(BMax[1], Pos[1] + R);
                    BMin[2] = Math.min(BMin[2], (Node.mirrorZ ? -Math.abs(Pos[2]) : Pos[2]) - R);
                    BMax[2] = Math.max(BMax[2], (Node.mirrorZ ? Math.abs(Pos[2]) : Pos[2]) + R);
                }
            }
        }
        return { min: BMin, max: BMax };
    }

    /**
     * Exact CPU sphere-trace raycast against the hybrid SDF (Volume + CSG) for 3D brush placement & object picking.
     */
    raycastHybrid(Origin, Dir, CsgNodes, VolumeVisible = true, MaxDist = 42.0)
    {
        const SceneBox = this.computeSceneBounds(CsgNodes, 0.85);
        let TMin = 0.02, TMax = MaxDist;
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            const InvD = Math.abs(Dir[Axis]) > 1e-7 ? 1.0 / Dir[Axis] : 1e7;
            let T0 = (SceneBox.min[Axis] - Origin[Axis]) * InvD;
            let T1 = (SceneBox.max[Axis] - Origin[Axis]) * InvD;
            if (T0 > T1) { const Sw = T0; T0 = T1; T1 = Sw; }
            TMin = Math.max(TMin, T0);
            TMax = Math.min(TMax, T1);
            if (TMax < TMin) return { hit: false };
        }

        let T = TMin;
        const Tmp = [0, 0, 0, 0];
        for (let Step = 0; Step < 180 && T <= TMax; ++Step)
        {
            const Px = Origin[0] + Dir[0] * T;
            const Py = Origin[1] + Dir[1] * T;
            const Pz = Origin[2] + Dir[2] * T;
            const D = this.evaluateHybridPoint(Px, Py, Pz, CsgNodes, VolumeVisible, Tmp)[0];
            if (D < 0.0018)
            {
                const HitPos = [Px, Py, Pz];
                const Normal = this.evaluateHybridNormal(Px, Py, Pz, CsgNodes, VolumeVisible);

                let ClosestNodeId = null;
                let BestNodeAbs = VolumeVisible ? Math.abs(this.sampleDistance(Px, Py, Pz)) : 1e6;
                if (CsgNodes)
                {
                    for (const Node of CsgNodes)
                    {
                        if (!Node || Node.enabled === false) continue;
                        const Nd = Math.abs(EvaluateCsgNode(Node, Px, Py, Pz));
                        if (Nd < BestNodeAbs + 0.015)
                        {
                            BestNodeAbs = Nd;
                            ClosestNodeId = Node.id;
                        }
                    }
                }
                return {
                    hit: true,
                    t: T,
                    pos: HitPos,
                    normal: Normal,
                    csgNodeId: ClosestNodeId
                };
            }
            T += Math.max(0.004, D * 0.88);
        }
        return { hit: false };
    }

    /*--------------------------------------------------------------------------------------------------------------------
                                      ELASTIC DOMAIN BOUNDS & SOFT BOUNDARY CUSHION
    --------------------------------------------------------------------------------------------------------------------*/
    /**
     * Smoothly closes the zero-level isosurface inside the outer 2.5-voxel shell so clay never slices flat at domain walls.
     */
    applySoftBoundaryCushion(X0 = 0, Y0 = 0, Z0 = 0, X1 = this.nx - 1, Y1 = this.ny - 1, Z1 = this.nz - 1)
    {
        if (this.boundsMode === 'hard') return;
        const Shell = 3;
        if (X0 > Shell && Y0 > Shell && Z0 > Shell &&
            X1 < this.nx - 1 - Shell && Y1 < this.ny - 1 - Shell && Z1 < this.nz - 1 - Shell)
        {
            return; // Sub-brick does not touch the boundary shell
        }

        const Inset = this.voxelSize * 1.85;
        const CornerR = this.voxelSize * 2.4;
        const SmoothK = this.voxelSize * 1.8;
        const Cx = 0.5 * (this.min[0] + this.max[0]);
        const Cy = 0.5 * (this.min[1] + this.max[1]);
        const Cz = 0.5 * (this.min[2] + this.max[2]);
        const Hx = Math.max(0.1, 0.5 * (this.max[0] - this.min[0]) - Inset - CornerR);
        const Hy = Math.max(0.1, 0.5 * (this.max[1] - this.min[1]) - Inset - CornerR);
        const Hz = Math.max(0.1, 0.5 * (this.max[2] - this.min[2]) - Inset - CornerR);
        const Nx = this.nx, Ny = this.ny;
        const Dist = this.distances;
        const Band = this.bandWidth;

        for (let Z = Z0; Z <= Z1; ++Z)
        {
            const NearZ = (Z <= Shell || Z >= this.nz - 1 - Shell);
            const Az = Math.abs(this.gridToWorldZ(Z) - Cz) - Hz;
            for (let Y = Y0; Y <= Y1; ++Y)
            {
                const NearZY = NearZ || (Y <= Shell || Y >= Ny - 1 - Shell);
                const Ay = Math.abs(this.gridToWorldY(Y) - Cy) - Hy;
                let Idx = (Z * Ny + Y) * Nx + X0;
                for (let X = X0; X <= X1; ++X, ++Idx)
                {
                    if (!NearZY && X > Shell && X < Nx - 1 - Shell) continue;
                    const D0 = Dist[Idx];
                    if (D0 >= Band) continue;
                    const Ax = Math.abs(this.gridToWorldX(X) - Cx) - Hx;
                    const BoxD = Math.hypot(Math.max(Ax, 0), Math.max(Ay, 0), Math.max(Az, 0)) +
                                 Math.min(Math.max(Ax, Math.max(Ay, Az)), 0) - CornerR;
                    Dist[Idx] = Math.min(Band, SmoothMaxBoundary(D0, BoxD, SmoothK));
                }
            }
        }
    }

    /**
     * Resamples the 3D volume (and any active stroke snapshot) onto a new cubic [NewMin, NewMax] domain without losing clay.
     */
    reframeBounds(NewMin, NewMax, PreserveActiveStroke = true)
    {
        const OldState = {
            nx: this.nx, ny: this.ny, nz: this.nz,
            min: this.min.slice(), max: this.max.slice(),
            step: this.step.slice(),
            distances: this.distances,
            colors: this.colors
        };
        const SampleOldDist = SdfVolume.prototype.sampleDistance.bind(OldState);
        const SampleOldCol = SdfVolume.prototype.sampleColorAccurateInto.bind(OldState);

        const N = this.nx;
        this.min = [NewMin[0], NewMin[1], NewMin[2]];
        this.max = [NewMax[0], NewMax[1], NewMax[2]];
        const SpanX = Math.max(0.5, this.max[0] - this.min[0]);
        const SpanY = Math.max(0.5, this.max[1] - this.min[1]);
        const SpanZ = Math.max(0.5, this.max[2] - this.min[2]);
        this.span = Math.max(SpanX, SpanY, SpanZ);
        this.halfExtent = Math.max(
            Math.abs(this.min[0]), Math.abs(this.max[0]),
            Math.abs(this.min[1]), Math.abs(this.max[1]),
            Math.abs(this.min[2]), Math.abs(this.max[2])
        );
        this.step = [SpanX / N, SpanY / N, SpanZ / N];
        this.voxelSize = Math.max(this.step[0], this.step[1], this.step[2]);
        this.bandWidth = Math.max(0.42, this.voxelSize * 9.0);

        const NextDist = new Float32Array(this.voxelCount);
        const NextCol = new Float32Array(this.voxelCount * 3);
        const Rgb = [0, 0, 0];
        const Band = this.bandWidth;

        for (let Z = 0; Z < N; ++Z)
        {
            const Wz = this.gridToWorldZ(Z);
            for (let Y = 0; Y < N; ++Y)
            {
                const Wy = this.gridToWorldY(Y);
                let Idx = (Z * N + Y) * N;
                for (let X = 0; X < N; ++X, ++Idx)
                {
                    const Wx = this.gridToWorldX(X);
                    NextDist[Idx] = Clamp(SampleOldDist(Wx, Wy, Wz), -Band * 2.5, Band);
                    SampleOldCol(Wx, Wy, Wz, Rgb);
                    const C = Idx * 3;
                    NextCol[C] = Rgb[0];
                    NextCol[C + 1] = Rgb[1];
                    NextCol[C + 2] = Rgb[2];
                }
            }
        }

        // If mid-stroke with a snapshot (Grab / Stamp), resample the stroke snapshot to the new bounds seamlessly too!
        if (PreserveActiveStroke && this.stroke?.baseDistances)
        {
            const SnapState = {
                nx: OldState.nx, ny: OldState.ny, nz: OldState.nz,
                min: OldState.min, max: OldState.max, step: OldState.step,
                distances: this.stroke.baseDistances,
                colors: this.stroke.baseColors
            };
            const SampleSnapD = SdfVolume.prototype.sampleDistance.bind(SnapState);
            const SampleSnapC = SdfVolume.prototype.sampleColorAccurateInto.bind(SnapState);
            const NewBaseD = new Float32Array(this.voxelCount);
            const NewBaseC = new Float32Array(this.voxelCount * 3);
            for (let Z = 0; Z < N; ++Z)
            {
                const Wz = this.gridToWorldZ(Z);
                for (let Y = 0; Y < N; ++Y)
                {
                    const Wy = this.gridToWorldY(Y);
                    let Idx = (Z * N + Y) * N;
                    for (let X = 0; X < N; ++X, ++Idx)
                    {
                        const Wx = this.gridToWorldX(X);
                        NewBaseD[Idx] = Clamp(SampleSnapD(Wx, Wy, Wz), -Band * 2.5, Band);
                        SampleSnapC(Wx, Wy, Wz, Rgb);
                        const C = Idx * 3;
                        NewBaseC[C] = Rgb[0];
                        NewBaseC[C + 1] = Rgb[1];
                        NewBaseC[C + 2] = Rgb[2];
                    }
                }
            }
            this.stroke.baseDistances = NewBaseD;
            this.stroke.baseColors = NewBaseC;
            this.stroke.prevGrabBox = null;
        }

        this.distances = NextDist;
        this.colors = NextCol;
        this.boundsRevision++;
        this.markAllDirty();
    }

    /**
     * Elastic Auto-Expand: grows cubic [min, max] in quantized 0.8m steps if a sculpt dab approaches or crosses the boundary.
     */
    ensureBoundsContain(ReqMin, ReqMax)
    {
        if (this.boundsMode !== 'expand') return false;
        const WallPad = this.voxelSize * 2.5;
        const NeedsGrow = (
            ReqMin[0] < this.min[0] + WallPad || ReqMax[0] > this.max[0] - WallPad ||
            ReqMin[1] < this.min[1] + WallPad || ReqMax[1] > this.max[1] - WallPad ||
            ReqMin[2] < this.min[2] + WallPad || ReqMax[2] > this.max[2] - WallPad
        );
        if (!NeedsGrow) return false;

        const Headroom = 0.48; // Extra cushion added in the direction of sculpting so continuous drags don't re-grow every frame
        const UMin = [
            Math.min(this.min[0], ReqMin[0] - Headroom),
            Math.min(this.min[1], ReqMin[1] - Headroom),
            Math.min(this.min[2], ReqMin[2] - Headroom)
        ];
        const UMax = [
            Math.max(this.max[0], ReqMax[0] + Headroom),
            Math.max(this.max[1], ReqMax[1] + Headroom),
            Math.max(this.max[2], ReqMax[2] + Headroom)
        ];

        const RawSpan = Math.max(UMax[0] - UMin[0], UMax[1] - UMin[1], UMax[2] - UMin[2]);
        const TargetSpan = Math.min(this.maxAutoSpan, Math.ceil(RawSpan / 0.8) * 0.8);
        const CurSpan = this.max[0] - this.min[0];
        if (TargetSpan <= CurSpan + 1e-3 &&
            UMin[0] >= this.min[0] && UMax[0] <= this.max[0] &&
            UMin[1] >= this.min[1] && UMax[1] <= this.max[1] &&
            UMin[2] >= this.min[2] && UMax[2] <= this.max[2])
        {
            return false;
        }
        if (CurSpan >= this.maxAutoSpan - 1e-3)
        {
            return false; // Already at max studio domain size; soft boundary cushion handles outer wall
        }

        const FinalSpan = Math.max(TargetSpan, CurSpan + 0.8);
        const ClampedSpan = Math.min(this.maxAutoSpan, FinalSpan);
        const NewMin = [0, 0, 0], NewMax = [0, 0, 0];
        for (let A = 0; A < 3; ++A)
        {
            const Mid = 0.5 * (UMin[A] + UMax[A]);
            NewMin[A] = Clamp(Mid - 0.5 * ClampedSpan, -this.maxAutoSpan * 0.65, this.maxAutoSpan * 0.65 - ClampedSpan);
            NewMax[A] = NewMin[A] + ClampedSpan;
        }

        this.reframeBounds(NewMin, NewMax, true);
        this.lastBoundsEvent = {
            span: ClampedSpan,
            min: NewMin.slice(),
            max: NewMax.slice(),
            time: performance.now()
        };
        return true;
    }

    /**
     * Tightly re-centers and fits the cubic voxel grid around actual sculpted clay + margin to reclaim maximum resolution.
     */
    compactBoundsToClay(MinSpan = 3.2, Padding = 0.45)
    {
        const Nx = this.nx, Ny = this.ny, Nz = this.nz;
        const Dist = this.distances;
        let CMinX = 1e9, CMinY = 1e9, CMinZ = 1e9;
        let CMaxX = -1e9, CMaxY = -1e9, CMaxZ = -1e9;
        let Found = false;

        for (let Z = 0; Z < Nz; ++Z)
        {
            const Wz = this.gridToWorldZ(Z);
            for (let Y = 0; Y < Ny; ++Y)
            {
                const Wy = this.gridToWorldY(Y);
                let Idx = (Z * Ny + Y) * Nx;
                for (let X = 0; X < Nx; ++X, ++Idx)
                {
                    if (Dist[Idx] <= this.voxelSize * 0.75)
                    {
                        const Wx = this.gridToWorldX(X);
                        if (Wx < CMinX) CMinX = Wx;
                        if (Wx > CMaxX) CMaxX = Wx;
                        if (Wy < CMinY) CMinY = Wy;
                        if (Wy > CMaxY) CMaxY = Wy;
                        if (Wz < CMinZ) CMinZ = Wz;
                        if (Wz > CMaxZ) CMaxZ = Wz;
                        Found = true;
                    }
                }
            }
        }

        if (!Found)
        {
            this.reframeBounds([-DEFAULT_BOUNDS, -DEFAULT_BOUNDS, -DEFAULT_BOUNDS], [DEFAULT_BOUNDS, DEFAULT_BOUNDS, DEFAULT_BOUNDS], false);
            return DEFAULT_BOUNDS * 2.0;
        }

        const NeedSpan = Math.max(CMaxX - CMinX, CMaxY - CMinY, CMaxZ - CMinZ) + Padding * 2.0;
        const FinalSpan = Clamp(Math.ceil(NeedSpan / 0.4) * 0.4, MinSpan, this.maxAutoSpan);
        const Cx = 0.5 * (CMinX + CMaxX), Cy = 0.5 * (CMinY + CMaxY), Cz = 0.5 * (CMinZ + CMaxZ);
        const NewMin = [Cx - FinalSpan * 0.5, Cy - FinalSpan * 0.5, Cz - FinalSpan * 0.5];
        const NewMax = [Cx + FinalSpan * 0.5, Cy + FinalSpan * 0.5, Cz + FinalSpan * 0.5];
        this.reframeBounds(NewMin, NewMax, false);
        this.applySoftBoundaryCushion();
        return FinalSpan;
    }

    /*--------------------------------------------------------------------------------------------------------------------
                                              UNDO / REDO HISTORY STACK
    --------------------------------------------------------------------------------------------------------------------*/
    pushHistorySnapshot(Label = 'Sculpt stroke', CsgNodesSnapshot = null)
    {
        if (this.historyIndex < this.history.length - 1)
        {
            this.history.length = this.historyIndex + 1;
        }
        this.history.push({
            label: Label,
            timestamp: performance.now(),
            nx: this.nx,
            min: this.min.slice(),
            max: this.max.slice(),
            distances: new Float32Array(this.distances),
            colors: new Float32Array(this.colors),
            csg: CsgNodesSnapshot ? JSON.parse(JSON.stringify(CsgNodesSnapshot)) : null
        });
        if (this.history.length > this.maxHistory)
        {
            this.history.shift();
        }
        this.historyIndex = this.history.length - 1;
    }

    restoreHistoryAt(Index)
    {
        if (Index < 0 || Index >= this.history.length) return null;
        const Snap = this.history[Index];
        this.historyIndex = Index;
        const BoundsChanged = !Snap.min ||
            Math.abs(Snap.min[0] - this.min[0]) > 1e-4 ||
            Math.abs(Snap.max[0] - this.max[0]) > 1e-4 ||
            Math.abs(Snap.min[1] - this.min[1]) > 1e-4 ||
            Math.abs(Snap.min[2] - this.min[2]) > 1e-4;
        if (Snap.nx !== this.nx || BoundsChanged)
        {
            this.allocate(
                Snap.nx,
                Snap.min || [-DEFAULT_BOUNDS, -DEFAULT_BOUNDS, -DEFAULT_BOUNDS],
                Snap.max || [DEFAULT_BOUNDS, DEFAULT_BOUNDS, DEFAULT_BOUNDS]
            );
            this.boundsRevision++;
        }
        this.distances.set(Snap.distances);
        this.colors.set(Snap.colors);
        this.markAllDirty();
        return Snap;
    }

    canUndo() { return this.historyIndex > 0; }
    canRedo() { return this.historyIndex >= 0 && this.historyIndex < this.history.length - 1; }
    undo() { return this.canUndo() ? this.restoreHistoryAt(this.historyIndex - 1) : null; }
    redo() { return this.canRedo() ? this.restoreHistoryAt(this.historyIndex + 1) : null; }

    /*--------------------------------------------------------------------------------------------------------------------
                                           CSG-TO-VOLUME BAKER & RESAMPLER
    --------------------------------------------------------------------------------------------------------------------*/
    bakeCsgNodes(Nodes, ClearFirst = false)
    {
        if (ClearFirst) this.clearVolume();
        const Active = (Nodes || []).filter(N => N && N.enabled !== false);
        if (!Active.length) return 0;

        // Auto-expand clay volume only if an additive CSG node actually extends past current [min, max]
        if (!ClearFirst && this.boundsMode === 'expand')
        {
            const AdditiveNodes = Active.filter(N => (N.op ?? 0) === 0 || (N.op ?? 0) === 3);
            if (AdditiveNodes.length > 0)
            {
                const NMin = [1e9, 1e9, 1e9], NMax = [-1e9, -1e9, -1e9];
                for (const Node of AdditiveNodes)
                {
                    const Pos = Node.position || [0, 0, 0];
                    const Sz = Node.size || [0.35, 0.35, 0.35];
                    const Scale = Math.max(0.05, Number(Node.scale) || 1.0);
                    const R = (Math.max(Sz[0], Sz[1], Sz[2]) + (Number(Node.round) || 0) + (Number(Node.blend) || 0.06)) * Scale;
                    const SpanXZ = (Node.mirrorX || (Node.radialCount | 0) >= 2) ? (Math.hypot(Pos[0], Pos[2]) + R) : null;
                    NMin[0] = Math.min(NMin[0], SpanXZ !== null ? -SpanXZ : Pos[0] - R);
                    NMax[0] = Math.max(NMax[0], SpanXZ !== null ? SpanXZ : Pos[0] + R);
                    NMin[1] = Math.min(NMin[1], Pos[1] - R);
                    NMax[1] = Math.max(NMax[1], Pos[1] + R);
                    NMin[2] = Math.min(NMin[2], SpanXZ !== null ? -SpanXZ : (Node.mirrorZ ? -Math.abs(Pos[2]) : Pos[2]) - R);
                    NMax[2] = Math.max(NMax[2], SpanXZ !== null ? SpanXZ : (Node.mirrorZ ? Math.abs(Pos[2]) : Pos[2]) + R);
                }
                this.ensureBoundsContain(NMin, NMax);
            }
        }

        for (const N of Active)
        {
            N._invQuat = InvertUnitQuat(EulerDegreesToQuat(N.rotation || [0, 0, 0]));
        }

        const Nx = this.nx, Ny = this.ny, Nz = this.nz;
        const Dist = this.distances;
        const Col = this.colors;
        const Acc = [0, 0, 0, 0];
        const Band = this.bandWidth;

        for (let Z = 0; Z < Nz; ++Z)
        {
            const Wz = this.gridToWorldZ(Z);
            for (let Y = 0; Y < Ny; ++Y)
            {
                const Wy = this.gridToWorldY(Y);
                let Idx = (Z * Ny + Y) * Nx;
                for (let X = 0; X < Nx; ++X, ++Idx)
                {
                    const Wx = this.gridToWorldX(X);
                    const C = Idx * 3;
                    Acc[0] = Dist[Idx];
                    Acc[1] = Col[C];
                    Acc[2] = Col[C + 1];
                    Acc[3] = Col[C + 2];
                    for (let K = 0; K < Active.length; ++K)
                    {
                        const Node = Active[K];
                        const Nd = EvaluateCsgNode(Node, Wx, Wy, Wz);
                        CombineBooleanInto(Acc, Nd, Node.color || this.defaultClayColor, Node.op ?? 0, Node.blend ?? 0.06);
                    }
                    Dist[Idx] = Clamp(Acc[0], -Band * 2.5, Band);
                    Col[C] = Acc[1];
                    Col[C + 1] = Acc[2];
                    Col[C + 2] = Acc[3];
                }
            }
        }
        this.applySoftBoundaryCushion();
        this.markAllDirty();
        return Active.length;
    }

    seedClayPrimitive(ShapeId = 0, Size = [0.68, 0.68, 0.68], Color = this.defaultClayColor, Round = 0.04)
    {
        this.resetBoundsToDefault(DEFAULT_BOUNDS);
        this.clearVolume(Color);
        this.bakeCsgNodes([{
            shape: ShapeId,
            op: 0,
            position: [0, 0.05, 0],
            rotation: [0, 0, 0],
            size: Size,
            scale: 1.0,
            blend: 0.02,
            round: Round,
            color: Color,
            enabled: true
        }], false);
    }

    resampleResolution(NewResolution)
    {
        const TargetN = Clamp(NewResolution | 0, 32, 160);
        if (TargetN === this.nx) return;

        const KeepMin = this.min.slice();
        const KeepMax = this.max.slice();
        const OldVolume = {
            nx: this.nx, ny: this.ny, nz: this.nz,
            min: KeepMin, max: KeepMax,
            step: this.step.slice(),
            distances: this.distances,
            colors: this.colors
        };
        const SampleOldDist = SdfVolume.prototype.sampleDistance.bind(OldVolume);
        const SampleOldCol = SdfVolume.prototype.sampleColorAccurateInto.bind(OldVolume);

        this.allocate(TargetN, KeepMin, KeepMax);
        const Rgb = [0, 0, 0];
        const Band = this.bandWidth;
        for (let Z = 0; Z < TargetN; ++Z)
        {
            const Wz = this.gridToWorldZ(Z);
            for (let Y = 0; Y < TargetN; ++Y)
            {
                const Wy = this.gridToWorldY(Y);
                let Idx = (Z * TargetN + Y) * TargetN;
                for (let X = 0; X < TargetN; ++X, ++Idx)
                {
                    const Wx = this.gridToWorldX(X);
                    this.distances[Idx] = Clamp(SampleOldDist(Wx, Wy, Wz), -Band * 2.5, Band);
                    SampleOldCol(Wx, Wy, Wz, Rgb);
                    const C = Idx * 3;
                    this.colors[C] = Rgb[0];
                    this.colors[C + 1] = Rgb[1];
                    this.colors[C + 2] = Rgb[2];
                }
            }
        }
        this.history = [];
        this.historyIndex = -1;
        this.markAllDirty();
    }

    /**
     * Godunov upwind Eikonal relaxation (|grad d| -> 1) preserving the zero-level isosurface shell.
     */
    normalizeEikonal(Iterations = 3)
    {
        const Nx = this.nx, Ny = this.ny, Nz = this.nz;
        const H = this.voxelSize;
        const AnchorBand = H * 0.85;
        let Src = this.distances;
        let Dst = new Float32Array(this.voxelCount);

        for (let Iter = 0; Iter < Iterations; ++Iter)
        {
            for (let Z = 0; Z < Nz; ++Z)
            {
                const Zm = Math.max(0, Z - 1) * Ny, Zc = Z * Ny, Zp = Math.min(Nz - 1, Z + 1) * Ny;
                for (let Y = 0; Y < Ny; ++Y)
                {
                    const Ym = Math.max(0, Y - 1), Yp = Math.min(Ny - 1, Y + 1);
                    for (let X = 0; X < Nx; ++X)
                    {
                        const Idx = (Zc + Y) * Nx + X;
                        const D0 = Src[Idx];
                        if (Math.abs(D0) <= AnchorBand)
                        {
                            Dst[Idx] = D0;
                            continue;
                        }
                        const Xm = Math.max(0, X - 1), Xp = Math.min(Nx - 1, X + 1);
                        const DxMin = Math.min(Src[(Zc + Y) * Nx + Xm], Src[(Zc + Y) * Nx + Xp]);
                        const DyMin = Math.min(Src[(Zc + Ym) * Nx + X], Src[(Zc + Yp) * Nx + X]);
                        const DzMin = Math.min(Src[(Zm + Y) * Nx + X], Src[(Zp + Y) * Nx + X]);

                        const DxMax = Math.max(Src[(Zc + Y) * Nx + Xm], Src[(Zc + Y) * Nx + Xp]);
                        const DyMax = Math.max(Src[(Zc + Ym) * Nx + X], Src[(Zc + Yp) * Nx + X]);
                        const DzMax = Math.max(Src[(Zm + Y) * Nx + X], Src[(Zp + Y) * Nx + X]);

                        if (D0 > 0)
                        {
                            // Solvequadratic Eikonal for exterior positive distance
                            let A = DxMin, B = DyMin, C = DzMin;
                            if (A > B) { const T = A; A = B; B = T; }
                            if (B > C) { const T = B; B = C; C = T; }
                            if (A > B) { const T = A; A = B; B = T; }
                            let U = A + H;
                            if (U > B)
                            {
                                U = 0.5 * (A + B + Math.sqrt(Math.max(0, 2 * H * H - (A - B) * (A - B))));
                                if (U > C)
                                {
                                    const Sum = A + B + C;
                                    const Disc = Sum * Sum - 3 * (A * A + B * B + C * C - H * H);
                                    if (Disc >= 0) U = (Sum + Math.sqrt(Disc)) / 3.0;
                                }
                            }
                            Dst[Idx] = Mix(D0, U, 0.65);
                        }
                        else
                        {
                            // Interior negative distance
                            let A = -DxMax, B = -DyMax, C = -DzMax;
                            if (A > B) { const T = A; A = B; B = T; }
                            if (B > C) { const T = B; B = C; C = T; }
                            if (A > B) { const T = A; A = B; B = T; }
                            let U = A + H;
                            if (U > B)
                            {
                                U = 0.5 * (A + B + Math.sqrt(Math.max(0, 2 * H * H - (A - B) * (A - B))));
                                if (U > C)
                                {
                                    const Sum = A + B + C;
                                    const Disc = Sum * Sum - 3 * (A * A + B * B + C * C - H * H);
                                    if (Disc >= 0) U = (Sum + Math.sqrt(Disc)) / 3.0;
                                }
                            }
                            Dst[Idx] = Mix(D0, -U, 0.65);
                        }
                    }
                }
            }
            if (Iter < Iterations - 1)
            {
                const Tmp = Src;
                Src = Dst;
                Dst = (Tmp === this.distances) ? new Float32Array(this.voxelCount) : Tmp;
            }
        }
        this.distances.set(Dst);
        this.markAllDirty();
    }

    /**
     * Global smooth or hollow-shell operation on the entire clay volume.
     */
    smoothEntireVolume(Strength = 0.45)
    {
        const Nx = this.nx, Ny = this.ny, Nz = this.nz;
        const Copy = new Float32Array(this.distances);
        const Out = this.distances;
        for (let Z = 1; Z < Nz - 1; ++Z)
        {
            for (let Y = 1; Y < Ny - 1; ++Y)
            {
                for (let X = 1; X < Nx - 1; ++X)
                {
                    const Idx = (Z * Ny + Y) * Nx + X;
                    const Avg = (
                        Copy[Idx - 1] + Copy[Idx + 1] +
                        Copy[Idx - Nx] + Copy[Idx + Nx] +
                        Copy[Idx - Nx * Ny] + Copy[Idx + Nx * Ny]
                    ) / 6.0;
                    Out[Idx] = Mix(Copy[Idx], Avg, Strength);
                }
            }
        }
        this.markAllDirty();
    }

    shellHollowVolume(WallThickness = 0.08)
    {
        const HalfWall = Math.max(this.voxelSize * 0.85, WallThickness * 0.5);
        for (let I = 0; I < this.voxelCount; ++I)
        {
            this.distances[I] = Math.abs(this.distances[I]) - HalfWall;
        }
        this.markAllDirty();
    }

    /*--------------------------------------------------------------------------------------------------------------------
                                        INTERACTIVE 3D VOLUMETRIC SCULPT BRUSHES
    --------------------------------------------------------------------------------------------------------------------*/
    beginStroke(Tool, HitPos, HitNormal, Settings)
    {
        const NeedsSnapshot = (Tool === 'grab' || Tool === 'stamp');
        this.stroke = {
            tool: Tool,
            originPos: HitPos.slice(),
            originNormal: HitNormal.slice(),
            lastPos: HitPos.slice(),
            lastNormal: HitNormal.slice(),
            lazyPos: HitPos.slice(),
            dabCount: 0,
            prevGrabBox: null,
            baseDistances: NeedsSnapshot ? new Float32Array(this.distances) : null,
            baseColors: NeedsSnapshot ? new Float32Array(this.colors) : null,
            settings: Settings
        };
        if (Tool !== 'grab' && Tool !== 'snake')
        {
            this.applyBrushDab(Tool, HitPos, HitPos, HitNormal, [0, 0, 0], Settings);
        }
    }

    continueStroke(HitPos, HitNormal, Settings)
    {
        if (!this.stroke) return 0;
        const S = this.stroke;
        const Tool = Settings.shiftSmooth ? 'smooth' : S.tool;

        if (Tool === 'grab')
        {
            const TotalDelta = Vec3Sub(HitPos, S.originPos);
            return this.applyGrabDeformation(S.originPos, TotalDelta, Settings);
        }

        if (Tool === 'stamp')
        {
            const DragVec = Vec3Sub(HitPos, S.originPos);
            return this.applyInteractiveStamp(S.originPos, S.originNormal, DragVec, Settings);
        }

        // Lazy-mouse steady tether in 3D world space
        let TargetPos = HitPos;
        const LazyRadius = Math.max(0, Number(Settings.lazyRadius) || 0);
        if (LazyRadius > 0.002)
        {
            const Diff = Vec3Sub(HitPos, S.lazyPos);
            const Dist = Math.hypot(Diff[0], Diff[1], Diff[2]);
            if (Dist < LazyRadius) return 0;
            const Pull = (Dist - LazyRadius) / Dist;
            S.lazyPos = [
                S.lazyPos[0] + Diff[0] * Pull,
                S.lazyPos[1] + Diff[1] * Pull,
                S.lazyPos[2] + Diff[2] * Pull
            ];
            TargetPos = S.lazyPos;
        }

        const MoveDelta = Vec3Sub(TargetPos, S.lastPos);
        const MoveLen = Math.hypot(MoveDelta[0], MoveDelta[1], MoveDelta[2]);
        const Radius = Math.max(0.03, Number(Settings.radius) || 0.24);
        const Spacing = Math.max(0.08, Number(Settings.spacing) || 0.22) * Radius;

        if (MoveLen < Spacing * 0.65 && Tool !== 'snake')
        {
            return 0;
        }

        let VoxelsTouched = 0;
        if (Tool === 'snake')
        {
            const Steps = Math.max(1, Math.min(6, Math.ceil(MoveLen / (Radius * 0.32))));
            for (let K = 1; K <= Steps; ++K)
            {
                const T0 = (K - 1) / Steps, T1 = K / Steps;
                const P0 = [Mix(S.lastPos[0], TargetPos[0], T0), Mix(S.lastPos[1], TargetPos[1], T0), Mix(S.lastPos[2], TargetPos[2], T0)];
                const P1 = [Mix(S.lastPos[0], TargetPos[0], T1), Mix(S.lastPos[1], TargetPos[1], T1), Mix(S.lastPos[2], TargetPos[2], T1)];
                VoxelsTouched += this.applySnakeStep(P0, P1, Vec3Sub(P1, P0), Settings);
            }
        }
        else
        {
            // Single continuous capsule-segment evaluation from S.lastPos -> TargetPos eliminates dotted-sphere beading!
            const PlaneNormal = Settings.adaptivePlane ? HitNormal : S.originNormal;
            const PlaneOrigin = Settings.adaptivePlane ? TargetPos : S.originPos;
            VoxelsTouched = this.applyBrushDab(Tool, S.lastPos, TargetPos, PlaneNormal, MoveDelta, Settings, PlaneOrigin);
        }

        S.lastPos = TargetPos.slice();
        S.lastNormal = HitNormal.slice();
        S.dabCount++;
        return VoxelsTouched;
    }

    endStroke()
    {
        const Info = this.stroke;
        this.stroke = null;
        return Info;
    }

    /**
     * Core 3D volumetric capsule/sphere brush kernel for Clay, Carve, Inflate, Smooth, Flatten, Pinch, Stamp, and Paint.
     */
    applyBrushDab(Tool, SegA, SegB, Normal, MoveDir, Settings, PlaneOrigin = SegB)
    {
        const Radius = Clamp(Number(Settings.radius) || 0.24, 0.03, 1.35);
        const Strength = Clamp(Number(Settings.strength) ?? 0.65, 0.02, 1.0);
        const Invert = !!Settings.invert;
        const EffectiveTool = (Tool === 'clay' && Invert) ? 'carve' : (Tool === 'carve' && Invert) ? 'clay' : Tool;
        const SmoothK = Math.max(0.008, Radius * Clamp(Number(Settings.smoothness) ?? 0.35, 0.04, 1.2));
        const BrushCol = Settings.brushColor || this.defaultClayColor;
        const PaintWithSculpt = Settings.paintWithSculpt !== false;
        const Curve = Settings.falloffCurve;

        const TargetsA = ExpandSymmetryTargets(SegA, MoveDir, Normal, Settings.symmetry);
        const TargetsB = ExpandSymmetryTargets(SegB, MoveDir, Normal, Settings.symmetry);
        const TargetsOrigin = ExpandSymmetryTargets(PlaneOrigin, MoveDir, Normal, Settings.symmetry);

        let TouchedTotal = 0;
        const Band = this.bandWidth;
        const IsAdditive = (
            EffectiveTool === 'clay' ||
            (EffectiveTool === 'inflate' && !Invert) ||
            (EffectiveTool === 'stamp' && !Invert && (Settings.stampOp ?? 0) !== 1)
        );

        // Auto-expand domain bounds before indexing if an additive stroke approaches or crosses the volume border
        if (IsAdditive && TargetsB.length > 0)
        {
            const GrowPad = Radius + SmoothK;
            const UMin = [1e9, 1e9, 1e9], UMax = [-1e9, -1e9, -1e9];
            for (let SIdx = 0; SIdx < TargetsB.length; ++SIdx)
            {
                const A = TargetsA[SIdx]?.Pos || SegA;
                const B = TargetsB[SIdx].Pos;
                for (let Axis = 0; Axis < 3; ++Axis)
                {
                    UMin[Axis] = Math.min(UMin[Axis], Math.min(A[Axis], B[Axis]) - GrowPad);
                    UMax[Axis] = Math.max(UMax[Axis], Math.max(A[Axis], B[Axis]) + GrowPad);
                }
            }
            this.ensureBoundsContain(UMin, UMax);
        }

        // Propagate narrow-band exterior distance around additive strokes so raymarching never oversteps new clay in empty space
        const Margin = IsAdditive
            ? (Radius + SmoothK + Band + this.voxelSize)
            : (Radius + SmoothK + this.voxelSize * 2.0);

        for (let SIdx = 0; SIdx < TargetsB.length; ++SIdx)
        {
            const A = TargetsA[SIdx]?.Pos || SegA;
            const B = TargetsB[SIdx].Pos;
            const N = Vec3Normalize(TargetsB[SIdx].Normal);
            const POrig = TargetsOrigin[SIdx]?.Pos || B;

            const MinW = [Math.min(A[0], B[0]) - Margin, Math.min(A[1], B[1]) - Margin, Math.min(A[2], B[2]) - Margin];
            const MaxW = [Math.max(A[0], B[0]) + Margin, Math.max(A[1], B[1]) + Margin, Math.max(A[2], B[2]) + Margin];

            const [Gx0, Gy0, Gz0] = this.worldToGridCoord(MinW[0], MinW[1], MinW[2]);
            const [Gx1, Gy1, Gz1] = this.worldToGridCoord(MaxW[0], MaxW[1], MaxW[2]);

            const X0 = Clamp(Math.floor(Gx0), 0, this.nx - 1);
            const Y0 = Clamp(Math.floor(Gy0), 0, this.ny - 1);
            const Z0 = Clamp(Math.floor(Gz0), 0, this.nz - 1);
            const X1 = Clamp(Math.ceil(Gx1), 0, this.nx - 1);
            const Y1 = Clamp(Math.ceil(Gy1), 0, this.ny - 1);
            const Z1 = Clamp(Math.ceil(Gz1), 0, this.nz - 1);
            if (X1 < X0 || Y1 < Y0 || Z1 < Z0) continue;

            const SubW = X1 - X0 + 1, SubH = Y1 - Y0 + 1, SubD = Z1 - Z0 + 1;
            const Nx = this.nx, Ny = this.ny, Nz = this.nz;
            const Dist = this.distances;
            const Col = this.colors;

            // Snapshot sub-brick when kernel needs neighbor sampling (Smooth / Pinch)
            let LocalCopy = null;
            if (EffectiveTool === 'smooth' || EffectiveTool === 'pinch')
            {
                LocalCopy = new Float32Array(SubW * SubH * SubD);
                let P = 0;
                for (let Z = Z0; Z <= Z1; ++Z)
                {
                    for (let Y = Y0; Y <= Y1; ++Y)
                    {
                        let Idx = (Z * Ny + Y) * Nx + X0;
                        for (let X = X0; X <= X1; ++X, ++Idx) LocalCopy[P++] = Dist[Idx];
                    }
                }
            }

            const SegDx = B[0] - A[0], SegDy = B[1] - A[1], SegDz = B[2] - A[2];
            const SegLenSq = SegDx * SegDx + SegDy * SegDy + SegDz * SegDz;

            let StampQuatInv = null;
            if (EffectiveTool === 'stamp')
            {
                StampQuatInv = BuildSurfaceAlignInvQuat(N, 0);
            }

            const ClayRatio = Math.max(0.45, (0.52 + 0.48 * Strength));
            const ClayCoreR = Radius * (0.65 + 0.35 * Strength);

            for (let Z = Z0; Z <= Z1; ++Z)
            {
                const Wz = this.gridToWorldZ(Z);
                for (let Y = Y0; Y <= Y1; ++Y)
                {
                    const Wy = this.gridToWorldY(Y);
                    let Idx = (Z * Ny + Y) * Nx + X0;
                    for (let X = X0; X <= X1; ++X, ++Idx)
                    {
                        const Wx = this.gridToWorldX(X);

                        // Closest point on stroke segment A -> B
                        let TSeg = 0.0;
                        if (SegLenSq > 1e-8)
                        {
                            TSeg = Clamp(((Wx - A[0]) * SegDx + (Wy - A[1]) * SegDy + (Wz - A[2]) * SegDz) / SegLenSq, 0.0, 1.0);
                        }
                        const Cx = A[0] + SegDx * TSeg;
                        const Cy = A[1] + SegDy * TSeg;
                        const Cz = A[2] + SegDz * TSeg;

                        const Rx = Wx - Cx, Ry = Wy - Cy, Rz = Wz - Cz;
                        const RadialDist = Math.hypot(Rx, Ry, Rz);
                        const OldD = Dist[Idx];
                        const CIdx = Idx * 3;

                        if (EffectiveTool === 'clay')
                        {
                            // Slightly oblate clay strip inside radius, multiplied by ClayRatio so |grad EllipDist| <= 1 (Lipschitz-safe!)
                            const NormalProj = Rx * N[0] + Ry * N[1] + Rz * N[2];
                            const TangSq = Math.max(0.0, RadialDist * RadialDist - NormalProj * NormalProj);
                            const ScaledEllip = Math.sqrt(TangSq + (NormalProj / ClayRatio) ** 2);
                            const EllipDist = (ScaledEllip - ClayCoreR) * ClayRatio;
                            if (EllipDist > Band + SmoothK) continue;

                            const H = Clamp(0.5 + 0.5 * (EllipDist - OldD) / SmoothK, 0.0, 1.0);
                            const NewD = Clamp(Mix(EllipDist, OldD, H) - SmoothK * H * (1.0 - H), -Band * 2.5, Band);
                            if (NewD < OldD)
                            {
                                Dist[Idx] = NewD;
                                if (PaintWithSculpt && EllipDist < SmoothK * 1.4)
                                {
                                    const Blend = (1.0 - H) * 0.85;
                                    Col[CIdx] = Mix(Col[CIdx], BrushCol[0], Blend);
                                    Col[CIdx + 1] = Mix(Col[CIdx + 1], BrushCol[1], Blend);
                                    Col[CIdx + 2] = Mix(Col[CIdx + 2], BrushCol[2], Blend);
                                }
                                TouchedTotal++;
                            }
                        }
                        else if (EffectiveTool === 'carve')
                        {
                            const ScoopD = RadialDist - Radius * (0.62 + 0.38 * Strength);
                            if (ScoopD > SmoothK * 1.5) continue;
                            const H = Clamp(0.5 - 0.5 * (OldD + ScoopD) / SmoothK, 0.0, 1.0);
                            const NewD = Math.min(Band, Mix(OldD, -ScoopD, H) + SmoothK * H * (1.0 - H));
                            if (NewD > OldD)
                            {
                                Dist[Idx] = NewD;
                                if (PaintWithSculpt && H > 0.02)
                                {
                                    Col[CIdx] = Mix(Col[CIdx], BrushCol[0], H * 0.7);
                                    Col[CIdx + 1] = Mix(Col[CIdx + 1], BrushCol[1], H * 0.7);
                                    Col[CIdx + 2] = Mix(Col[CIdx + 2], BrushCol[2], H * 0.7);
                                }
                                TouchedTotal++;
                            }
                        }
                        else if (EffectiveTool === 'inflate')
                        {
                            if (RadialDist >= Radius || Math.abs(OldD) > Radius * 1.35) continue;
                            const W = EvaluateFalloff(RadialDist / Radius, Curve);
                            const Sign = Invert ? 1.0 : -1.0;
                            const Delta = Sign * W * Strength * Radius * 0.28;
                            Dist[Idx] = Clamp(OldD + Delta, -Band * 2.5, Band);
                            if (PaintWithSculpt && W > 0.05 && Math.abs(Dist[Idx]) < this.voxelSize * 2.5)
                            {
                                Col[CIdx] = Mix(Col[CIdx], BrushCol[0], W * Strength * 0.45);
                                Col[CIdx + 1] = Mix(Col[CIdx + 1], BrushCol[1], W * Strength * 0.45);
                                Col[CIdx + 2] = Mix(Col[CIdx + 2], BrushCol[2], W * Strength * 0.45);
                            }
                            TouchedTotal++;
                        }
                        else if (EffectiveTool === 'smooth')
                        {
                            if (RadialDist >= Radius) continue;
                            const W = EvaluateFalloff(RadialDist / Radius, Curve) * Strength * 0.85;
                            if (W <= 0.002) continue;
                            const Lx = X - X0, Ly = Y - Y0, Lz = Z - Z0;
                            const At = (Ix, Iy, Iz) =>
                            {
                                if (Ix >= 0 && Ix < SubW && Iy >= 0 && Iy < SubH && Iz >= 0 && Iz < SubD)
                                {
                                    return LocalCopy[(Iz * SubH + Iy) * SubW + Ix];
                                }
                                const GxC = Clamp(X0 + Ix, 0, Nx - 1);
                                const GyC = Clamp(Y0 + Iy, 0, Ny - 1);
                                const GzC = Clamp(Z0 + Iz, 0, Nz - 1);
                                return Dist[(GzC * Ny + GyC) * Nx + GxC];
                            };
                            const Avg = (
                                At(Lx - 1, Ly, Lz) + At(Lx + 1, Ly, Lz) +
                                At(Lx, Ly - 1, Lz) + At(Lx, Ly + 1, Lz) +
                                At(Lx, Ly, Lz - 1) + At(Lx, Ly, Lz + 1)
                            ) / 6.0;
                            Dist[Idx] = Mix(At(Lx, Ly, Lz), Avg, W);
                            TouchedTotal++;
                        }
                        else if (EffectiveTool === 'flatten')
                        {
                            if (RadialDist >= Radius) continue;
                            const W = EvaluateFalloff(RadialDist / Radius, Curve) * Strength;
                            const PlaneD = (Wx - POrig[0]) * N[0] + (Wy - POrig[1]) * N[1] + (Wz - POrig[2]) * N[2];
                            if (Invert)
                            {
                                if (Math.abs(OldD) < Radius)
                                {
                                    Dist[Idx] = Clamp(Mix(OldD, PlaneD, W * 0.72), -Band * 2.5, Band);
                                    TouchedTotal++;
                                }
                            }
                            else if (OldD < PlaneD)
                            {
                                Dist[Idx] = Clamp(Mix(OldD, PlaneD, W * 0.88), -Band * 2.5, Band);
                                TouchedTotal++;
                            }
                        }
                        else if (EffectiveTool === 'pinch')
                        {
                            if (RadialDist >= Radius || RadialDist < 1e-5 || Math.abs(OldD) > Radius * 1.2) continue;
                            const W = EvaluateFalloff(RadialDist / Radius, Curve) * Strength;
                            const PullSign = Invert ? -1.0 : 1.0;
                            const Shift = PullSign * W * (1.0 - RadialDist / Radius) * Radius * 0.42;
                            const Sx = Wx + (Rx / RadialDist) * Shift;
                            const Sy = Wy + (Ry / RadialDist) * Shift;
                            const Sz = Wz + (Rz / RadialDist) * Shift;
                            const Sampled = this.sampleDistance(Sx, Sy, Sz);
                            const Crease = (Invert ? -1.0 : 1.0) * W * W * this.voxelSize * 0.32;
                            Dist[Idx] = Clamp(Sampled + Crease, -Band * 2.5, Band);
                            TouchedTotal++;
                        }
                        else if (EffectiveTool === 'stamp')
                        {
                            const Rel = RotateByQuat([Wx - B[0], Wy - B[1], Wz - B[2]], StampQuatInv);
                            const S = Radius * 0.78;
                            const StampD = EvaluatePrimitiveLocal(Settings.stampShape ?? 1, Rel[0], Rel[1], Rel[2], S, S * 0.72, S) - Radius * 0.06;
                            const Op = Invert ? 1 : (Settings.stampOp ?? 0);
                            const Acc = [OldD, Col[CIdx], Col[CIdx + 1], Col[CIdx + 2]];
                            CombineBooleanInto(Acc, StampD, BrushCol, Op, SmoothK);
                            Dist[Idx] = Clamp(Acc[0], -Band * 2.5, Band);
                            if (PaintWithSculpt && StampD < SmoothK * 1.4)
                            {
                                Col[CIdx] = Acc[1];
                                Col[CIdx + 1] = Acc[2];
                                Col[CIdx + 2] = Acc[3];
                            }
                            TouchedTotal++;
                        }
                        else if (EffectiveTool === 'paint')
                        {
                            if (RadialDist >= Radius || Math.abs(OldD) > this.voxelSize * 3.5) continue;
                            const W = EvaluateFalloff(RadialDist / Radius, Curve) * Strength;
                            Col[CIdx] = Mix(Col[CIdx], BrushCol[0], W);
                            Col[CIdx + 1] = Mix(Col[CIdx + 1], BrushCol[1], W);
                            Col[CIdx + 2] = Mix(Col[CIdx + 2], BrushCol[2], W);
                            TouchedTotal++;
                        }
                    }
                }
            }

            this.applySoftBoundaryCushion(X0, Y0, Z0, X1, Y1, Z1);
            this.expandDirty(X0, Y0, Z0, X1, Y1, Z1);
        }

        return TouchedTotal;
    }

    /**
     * Interactive click-drag Stamp preview from stroke snapshot (scales & rotates stamp live as user drags on surface!).
     */
    applyInteractiveStamp(OriginPos, OriginNormal, DragVec, Settings)
    {
        if (!this.stroke?.baseDistances) return 0;

        const DragLen = Math.hypot(DragVec[0], DragVec[1], DragVec[2]);
        const Radius = Clamp(DragLen > 0.03 ? DragLen : (Number(Settings.radius) || 0.24), 0.04, 1.5);
        const YawAngle = Math.atan2(DragVec[2], DragVec[0]);
        const SmoothK = Math.max(0.008, Radius * Clamp(Number(Settings.smoothness) ?? 0.25, 0.02, 1.0));
        const BrushCol = Settings.brushColor || this.defaultClayColor;
        const Op = Settings.invert ? 1 : (Settings.stampOp ?? 0);
        const Targets = ExpandSymmetryTargets(OriginPos, DragVec, OriginNormal, Settings.symmetry);

        if (Op !== 1 && Targets.length > 0)
        {
            const GrowPad = Radius * 1.15 + SmoothK;
            const UMin = [1e9, 1e9, 1e9], UMax = [-1e9, -1e9, -1e9];
            for (const T of Targets)
            {
                for (let A = 0; A < 3; ++A)
                {
                    UMin[A] = Math.min(UMin[A], T.Pos[A] - GrowPad);
                    UMax[A] = Math.max(UMax[A], T.Pos[A] + GrowPad);
                }
            }
            this.ensureBoundsContain(UMin, UMax);
        }

        const BaseD = this.stroke.baseDistances;
        const BaseC = this.stroke.baseColors;
        this.restorePreviewBrick(BaseD, BaseC);

        const Band = this.bandWidth;
        const Margin = Radius * 1.35 + SmoothK + (Op !== 1 ? Band : this.voxelSize * 2.0);
        const NextBoxes = [];
        let Touched = 0;

        for (const T of Targets)
        {
            const B = T.Pos;
            const N = Vec3Normalize(T.Normal);
            const QuatInv = BuildSurfaceAlignInvQuat(N, YawAngle);
            const [Gx0, Gy0, Gz0] = this.worldToGridCoord(B[0] - Margin, B[1] - Margin, B[2] - Margin);
            const [Gx1, Gy1, Gz1] = this.worldToGridCoord(B[0] + Margin, B[1] + Margin, B[2] + Margin);
            const X0 = Clamp(Math.floor(Gx0), 0, this.nx - 1), X1 = Clamp(Math.ceil(Gx1), 0, this.nx - 1);
            const Y0 = Clamp(Math.floor(Gy0), 0, this.ny - 1), Y1 = Clamp(Math.ceil(Gy1), 0, this.ny - 1);
            const Z0 = Clamp(Math.floor(Gz0), 0, this.nz - 1), Z1 = Clamp(Math.ceil(Gz1), 0, this.nz - 1);
            NextBoxes.push([X0, Y0, Z0, X1, Y1, Z1]);

            const S = Radius * 0.78;
            for (let Z = Z0; Z <= Z1; ++Z)
            {
                const Wz = this.gridToWorldZ(Z);
                for (let Y = Y0; Y <= Y1; ++Y)
                {
                    const Wy = this.gridToWorldY(Y);
                    let Idx = (Z * this.ny + Y) * this.nx + X0;
                    for (let X = X0; X <= X1; ++X, ++Idx)
                    {
                        const Wx = this.gridToWorldX(X);
                        const Rel = RotateByQuat([Wx - B[0], Wy - B[1], Wz - B[2]], QuatInv);
                        const StampD = EvaluatePrimitiveLocal(Settings.stampShape ?? 1, Rel[0], Rel[1], Rel[2], S, S * 0.65, S) - Radius * 0.05;
                        const CIdx = Idx * 3;
                        const Acc = [BaseD[Idx], BaseC[CIdx], BaseC[CIdx + 1], BaseC[CIdx + 2]];
                        CombineBooleanInto(Acc, StampD, BrushCol, Op, SmoothK);
                        this.distances[Idx] = Clamp(Acc[0], -Band * 2.5, Band);
                        if (Settings.paintWithSculpt !== false && StampD < SmoothK * 1.4)
                        {
                            this.colors[CIdx] = Acc[1];
                            this.colors[CIdx + 1] = Acc[2];
                            this.colors[CIdx + 2] = Acc[3];
                        }
                        Touched++;
                    }
                }
            }
            this.applySoftBoundaryCushion(X0, Y0, Z0, X1, Y1, Z1);
            this.expandDirty(X0, Y0, Z0, X1, Y1, Z1);
        }
        this.stroke.prevGrabBox = NextBoxes;
        return Touched;
    }

    /**
     * Real 3D Volumetric Grab / Warp brush using backward semi-Lagrangian sampling from stroke-start snapshot.
     */
    applyGrabDeformation(OriginPos, TotalDelta, Settings)
    {
        if (!this.stroke?.baseDistances) return 0;

        const Radius = Clamp(Number(Settings.radius) || 0.34, 0.05, 1.5);
        const Strength = Clamp(Number(Settings.strength) ?? 0.82, 0.05, 1.0);
        const Curve = Settings.falloffCurve;
        const Targets = ExpandSymmetryTargets(OriginPos, TotalDelta, [0, 1, 0], Settings.symmetry);

        if (Targets.length > 0)
        {
            const UMin = [1e9, 1e9, 1e9], UMax = [-1e9, -1e9, -1e9];
            for (const T of Targets)
            {
                const P1 = [T.Pos[0] + T.Dir[0] * Strength, T.Pos[1] + T.Dir[1] * Strength, T.Pos[2] + T.Dir[2] * Strength];
                for (let A = 0; A < 3; ++A)
                {
                    UMin[A] = Math.min(UMin[A], Math.min(T.Pos[A], P1[A]) - Radius);
                    UMax[A] = Math.max(UMax[A], Math.max(T.Pos[A], P1[A]) + Radius);
                }
            }
            this.ensureBoundsContain(UMin, UMax);
        }

        const BaseD = this.stroke.baseDistances;
        const BaseC = this.stroke.baseColors;
        this.restorePreviewBrick(BaseD, BaseC);

        const Band = this.bandWidth;
        const NextBoxes = [];
        let Touched = 0;

        const Rgb = [0, 0, 0];
        for (const T of Targets)
        {
            const P0 = T.Pos;
            const Delta = [T.Dir[0] * Strength, T.Dir[1] * Strength, T.Dir[2] * Strength];
            const P1 = [P0[0] + Delta[0], P0[1] + Delta[1], P0[2] + Delta[2]];
            const Margin = Radius + this.voxelSize * 2.0;

            const MinW = [Math.min(P0[0], P1[0]) - Margin, Math.min(P0[1], P1[1]) - Margin, Math.min(P0[2], P1[2]) - Margin];
            const MaxW = [Math.max(P0[0], P1[0]) + Margin, Math.max(P0[1], P1[1]) + Margin, Math.max(P0[2], P1[2]) + Margin];
            const [Gx0, Gy0, Gz0] = this.worldToGridCoord(MinW[0], MinW[1], MinW[2]);
            const [Gx1, Gy1, Gz1] = this.worldToGridCoord(MaxW[0], MaxW[1], MaxW[2]);
            const X0 = Clamp(Math.floor(Gx0), 0, this.nx - 1), X1 = Clamp(Math.ceil(Gx1), 0, this.nx - 1);
            const Y0 = Clamp(Math.floor(Gy0), 0, this.ny - 1), Y1 = Clamp(Math.ceil(Gy1), 0, this.ny - 1);
            const Z0 = Clamp(Math.floor(Gz0), 0, this.nz - 1), Z1 = Clamp(Math.ceil(Gz1), 0, this.nz - 1);
            NextBoxes.push([X0, Y0, Z0, X1, Y1, Z1]);

            const Dx = Delta[0], Dy = Delta[1], Dz = Delta[2];
            const DLenSq = Dx * Dx + Dy * Dy + Dz * Dz;

            for (let Z = Z0; Z <= Z1; ++Z)
            {
                const Wz = this.gridToWorldZ(Z);
                for (let Y = Y0; Y <= Y1; ++Y)
                {
                    const Wy = this.gridToWorldY(Y);
                    let Idx = (Z * this.ny + Y) * this.nx + X0;
                    for (let X = X0; X <= X1; ++X, ++Idx)
                    {
                        const Wx = this.gridToWorldX(X);
                        let TSeg = 0.0;
                        if (DLenSq > 1e-8)
                        {
                            TSeg = Clamp(((Wx - P0[0]) * Dx + (Wy - P0[1]) * Dy + (Wz - P0[2]) * Dz) / DLenSq, 0.0, 1.0);
                        }
                        const Cx = P0[0] + Dx * TSeg, Cy = P0[1] + Dy * TSeg, Cz = P0[2] + Dz * TSeg;
                        const RDist = Math.hypot(Wx - Cx, Wy - Cy, Wz - Cz);
                        if (RDist >= Radius) continue;

                        const W = EvaluateFalloff(RDist / Radius, Curve);
                        const Sx = Wx - Dx * W;
                        const Sy = Wy - Dy * W;
                        const Sz = Wz - Dz * W;
                        this.distances[Idx] = Clamp(this.sampleDistance(Sx, Sy, Sz, BaseD), -Band * 2.5, Band);
                        this.sampleColorAccurateInto(Sx, Sy, Sz, Rgb, BaseC);
                        const CIdx = Idx * 3;
                        this.colors[CIdx] = Rgb[0];
                        this.colors[CIdx + 1] = Rgb[1];
                        this.colors[CIdx + 2] = Rgb[2];
                        Touched++;
                    }
                }
            }
            this.applySoftBoundaryCushion(X0, Y0, Z0, X1, Y1, Z1);
            this.expandDirty(X0, Y0, Z0, X1, Y1, Z1);
        }
        this.stroke.prevGrabBox = NextBoxes;
        return Touched;
    }

    /**
     * Snake Hook incremental advection + tapered clay deposit for pulling horns & tendrils.
     */
    applySnakeStep(FromPos, ToPos, StepVec, Settings)
    {
        const Radius = Clamp(Number(Settings.radius) || 0.26, 0.04, 1.3);
        const Strength = Clamp(Number(Settings.strength) ?? 0.85, 0.1, 1.0);
        const Targets = ExpandSymmetryTargets(ToPos, StepVec, [0, 1, 0], Settings.symmetry);

        if (Targets.length > 0)
        {
            const UMin = [1e9, 1e9, 1e9], UMax = [-1e9, -1e9, -1e9];
            for (const T of Targets)
            {
                for (let A = 0; A < 3; ++A)
                {
                    UMin[A] = Math.min(UMin[A], T.Pos[A] - Radius * 1.1);
                    UMax[A] = Math.max(UMax[A], T.Pos[A] + Radius * 1.1);
                }
            }
            this.ensureBoundsContain(UMin, UMax);
        }

        const Band = this.bandWidth;
        const SmoothK = Radius * 0.25;
        const Margin = Radius * 0.5 + SmoothK + Band + this.voxelSize;
        const BrushCol = Settings.brushColor || this.defaultClayColor;
        let Touched = 0;

        for (const T of Targets)
        {
            const P = T.Pos;
            const DVec = T.Dir;
            const [Gx0, Gy0, Gz0] = this.worldToGridCoord(P[0] - Margin, P[1] - Margin, P[2] - Margin);
            const [Gx1, Gy1, Gz1] = this.worldToGridCoord(P[0] + Margin, P[1] + Margin, P[2] + Margin);
            const X0 = Clamp(Math.floor(Gx0), 0, this.nx - 1), X1 = Clamp(Math.ceil(Gx1), 0, this.nx - 1);
            const Y0 = Clamp(Math.floor(Gy0), 0, this.ny - 1), Y1 = Clamp(Math.ceil(Gy1), 0, this.ny - 1);
            const Z0 = Clamp(Math.floor(Gz0), 0, this.nz - 1), Z1 = Clamp(Math.ceil(Gz1), 0, this.nz - 1);

            const SubW = X1 - X0 + 1, SubH = Y1 - Y0 + 1, SubD = Z1 - Z0 + 1;
            const NewD = new Float32Array(SubW * SubH * SubD);
            let Ptr = 0;
            for (let Z = Z0; Z <= Z1; ++Z)
            {
                const Wz = this.gridToWorldZ(Z);
                for (let Y = Y0; Y <= Y1; ++Y)
                {
                    const Wy = this.gridToWorldY(Y);
                    let Idx = (Z * this.ny + Y) * this.nx + X0;
                    for (let X = X0; X <= X1; ++X, ++Idx, ++Ptr)
                    {
                        const Wx = this.gridToWorldX(X);
                        const RDist = Math.hypot(Wx - P[0], Wy - P[1], Wz - P[2]);
                        const Core = RDist - Radius * 0.44;
                        let Adv = this.distances[Idx];
                        if (RDist < Radius)
                        {
                            const W = EvaluateFalloff(RDist / Radius, Settings.falloffCurve) * Strength;
                            Adv = this.sampleDistance(Wx - DVec[0] * W, Wy - DVec[1] * W, Wz - DVec[2] * W);
                        }
                        if (Core > Band + SmoothK && RDist >= Radius)
                        {
                            NewD[Ptr] = Adv;
                            continue;
                        }
                        const H = Clamp(0.5 + 0.5 * (Core - Adv) / SmoothK, 0.0, 1.0);
                        NewD[Ptr] = Clamp(Mix(Core, Adv, H) - SmoothK * H * (1.0 - H), -Band * 2.5, Band);
                        if (Settings.paintWithSculpt !== false && Core < SmoothK)
                        {
                            const CIdx = Idx * 3;
                            const Blend = (1.0 - H) * 0.65;
                            this.colors[CIdx] = Mix(this.colors[CIdx], BrushCol[0], Blend);
                            this.colors[CIdx + 1] = Mix(this.colors[CIdx + 1], BrushCol[1], Blend);
                            this.colors[CIdx + 2] = Mix(this.colors[CIdx + 2], BrushCol[2], Blend);
                        }
                        Touched++;
                    }
                }
            }
            Ptr = 0;
            for (let Z = Z0; Z <= Z1; ++Z)
            {
                for (let Y = Y0; Y <= Y1; ++Y)
                {
                    let Idx = (Z * this.ny + Y) * this.nx + X0;
                    for (let X = X0; X <= X1; ++X, ++Idx, ++Ptr) this.distances[Idx] = NewD[Ptr];
                }
            }
            this.applySoftBoundaryCushion(X0, Y0, Z0, X1, Y1, Z1);
            this.expandDirty(X0, Y0, Z0, X1, Y1, Z1);
        }
        return Touched;
    }

    restorePreviewBrick(BaseD, BaseC)
    {
        if (!this.stroke?.prevGrabBox) return;
        for (const [X0, Y0, Z0, X1, Y1, Z1] of this.stroke.prevGrabBox)
        {
            for (let Z = Z0; Z <= Z1; ++Z)
            {
                for (let Y = Y0; Y <= Y1; ++Y)
                {
                    let Idx = (Z * this.ny + Y) * this.nx + X0;
                    for (let X = X0; X <= X1; ++X, ++Idx)
                    {
                        this.distances[Idx] = BaseD[Idx];
                        const C = Idx * 3;
                        this.colors[C] = BaseC[C];
                        this.colors[C + 1] = BaseC[C + 1];
                        this.colors[C + 2] = BaseC[C + 2];
                    }
                }
            }
            this.expandDirty(X0, Y0, Z0, X1, Y1, Z1);
        }
    }

    /**
     * Computes live Volume Telemetry (interior voxels, estimated volume in Liters, mean |grad d|, and distance histogram).
     */
    computeTelemetry()
    {
        const Dist = this.distances;
        const Nx = this.nx, Ny = this.ny, Nz = this.nz;
        let Occupied = 0;
        let GradSum = 0;
        let GradCount = 0;
        const Bins = new Float32Array(24);
        const InvTwoH = 0.5 / this.voxelSize;

        // Stride-sample on 128^3 so telemetry takes < 0.8ms even on dense volumes
        const Stride = Nx >= 120 ? 2 : 1;
        const Weight = Stride * Stride * Stride;

        for (let Z = 1; Z < Nz - 1; Z += Stride)
        {
            for (let Y = 1; Y < Ny - 1; Y += Stride)
            {
                for (let X = 1; X < Nx - 1; X += Stride)
                {
                    const Idx = (Z * Ny + Y) * Nx + X;
                    const D = Dist[Idx];
                    if (D <= 0.0) Occupied += Weight;
                    if (Math.abs(D) < 0.8)
                    {
                        const BinIdx = Clamp(Math.floor(((D + 0.8) / 1.6) * Bins.length), 0, Bins.length - 1);
                        Bins[BinIdx] += Weight;
                    }
                    if (Math.abs(D) < this.voxelSize * 3.0)
                    {
                        const Gx = (Dist[Idx + 1] - Dist[Idx - 1]) * InvTwoH;
                        const Gy = (Dist[Idx + Nx] - Dist[Idx - Nx]) * InvTwoH;
                        const Gz = (Dist[Idx + Nx * Ny] - Dist[Idx - Nx * Ny]) * InvTwoH;
                        GradSum += Math.hypot(Gx, Gy, Gz);
                        GradCount++;
                    }
                }
            }
        }

        const VoxelVolM3 = this.step[0] * this.step[1] * this.step[2];
        const Liters = Occupied * VoxelVolM3 * 1000.0;
        const MeanEikonal = GradCount > 0 ? GradSum / GradCount : 1.0;
        const MemoryMB = (this.voxelCount * 8) / (1024 * 1024); // RGBA16F GPU footprint

        return {
            occupiedVoxels: Occupied,
            liters: Liters,
            meanEikonal: MeanEikonal,
            memoryMB: MemoryMB,
            bins: Array.from(Bins)
        };
    }
}

/**
 * Builds an inverse unit quaternion that maps world-space vector into surface-normal-aligned local frame (+Y = Normal).
 */
function BuildSurfaceAlignInvQuat(Normal, YawRadians = 0)
{
    const Up = [0, 1, 0];
    const Dot = Clamp(Vec3Dot(Up, Normal), -1.0, 1.0);
    let QAlign;
    if (Dot > 0.9995)
    {
        QAlign = [0, 0, 0, 1];
    }
    else if (Dot < -0.9995)
    {
        QAlign = [1, 0, 0, 0];
    }
    else
    {
        const Ax = Up[1] * Normal[2] - Up[2] * Normal[1];
        const Ay = Up[2] * Normal[0] - Up[0] * Normal[2];
        const Az = Up[0] * Normal[1] - Up[1] * Normal[0];
        const W = 1.0 + Dot;
        const L = Math.hypot(Ax, Ay, Az, W);
        QAlign = [Ax / L, Ay / L, Az / L, W / L];
    }
    if (Math.abs(YawRadians) > 1e-4)
    {
        const Hy = YawRadians * 0.5;
        const QYaw = [0, Math.sin(Hy), 0, Math.cos(Hy)];
        // Q = QAlign * QYaw
        const [X1, Y1, Z1, W1] = QAlign;
        const [X2, Y2, Z2, W2] = QYaw;
        QAlign = [
            W1 * X2 + X1 * W2 + Y1 * Z2 - Z1 * Y2,
            W1 * Y2 - X1 * Z2 + Y1 * W2 + Z1 * X2,
            W1 * Z2 + X1 * Y2 - Y1 * X2 + Z1 * W2,
            W1 * W2 - X1 * X2 - Y1 * Y2 - Z1 * Z2
        ];
    }
    return InvertUnitQuat(QAlign);
}

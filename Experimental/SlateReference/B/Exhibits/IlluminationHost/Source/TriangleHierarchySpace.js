//============================================================================================================================================
//                                                        TRIANGLEHIERARCHYSPACE.JS
//============================================================================================================================================
// 🧩 Two-level triangle hierarchy (TLAS over the active scene occupants, binned-SAH BLAS per facet set) traced by the
//    triangle-reflection path of ReflectionTraversal.wgsl. Software traversal in compute: no hardware ray tracing is used.
// 📝 Reflections only. The global illumination and the cards stay on the scene distance field.

import { OccupantRole } from "./FacetStructure.js";
import { CourseMaterial } from "./MaterialSpecification.js";
import { CrossVector, SubtractVector, NormalizeVector } from "./TransformProjection.js";

export const TlasNodeCapacity        = 4608;          // [-] reserved nodes [0 .. 4607] at the head of BvhNodes for TLAS
export const DynamicBlasNodeCapacity = 512;           // [-] reserved BLAS leaf nodes for live fracture fragments
export const DynamicTriangleCapacity = 8192;          // [-] reserved triangles for live fracture fragments
export const TlasLeafBit             = 0x80000000;    // [-] high bit in Node.Count marking a TLAS leaf
export const TlasIndexMask           = 0x7FFFFFFF;    // [-] low bits carrying the OccupantIndex in a TLAS leaf
export const EmptyTlasSentinel       = 0x7FFFFFFF;    // [-] LeftOrFirst sentinel when the TLAS has no occupants
export const RoleShredded            = 32;            // [-] OccupantRole bit set on a wheel whose tyre rubber is shredded

const BlasLeafCapacity = 4;    // [-] triangles per BLAS leaf
const BlasBinCount     = 12;   // [-] SAH bins for static BLAS construction
const TlasBinCount     = 8;    // [-] SAH bins for per-frame TLAS construction

// 📐 Octahedral unit-vector packing matching PackingCodec.wgsl (pack2x16snorm(EncodeOctahedral(Normal))).
export function PackOctahedralCpu(Normal)
{
    const Nx = Normal[0], Ny = Normal[1], Nz = Normal[2];
    const L1 = Math.abs(Nx) + Math.abs(Ny) + Math.abs(Nz) || 1.0;
    let Ox = Nx / L1;
    let Oy = Ny / L1;
    if (Nz < 0.0)
    {
        const Fx = (1.0 - Math.abs(Oy)) * (Ox >= 0.0 ? 1.0 : -1.0);
        const Fy = (1.0 - Math.abs(Ox)) * (Oy >= 0.0 ? 1.0 : -1.0);
        Ox = Fx;
        Oy = Fy;
    }
    const Sx = Math.max(-32767, Math.min(32767, Math.round(Ox * 32767))) & 0xFFFF;
    const Sy = Math.max(-32767, Math.min(32767, Math.round(Oy * 32767))) & 0xFFFF;
    return ((Sx | (Sy << 16)) >>> 0);
}

export function UnpackOctahedralCpu(Packed)
{
    const Sx = (Packed << 16) >> 16;
    const Sy = Packed >> 16;
    const Ex = Math.max(-1.0, Math.min(1.0, Sx / 32767.0));
    const Ey = Math.max(-1.0, Math.min(1.0, Sy / 32767.0));
    let Nx = Ex;
    let Ny = Ey;
    let Nz = 1.0 - Math.abs(Ex) - Math.abs(Ey);
    const Fold = Math.max(-Nz, 0.0);
    Nx += Nx >= 0.0 ? -Fold : Fold;
    Ny += Ny >= 0.0 ? -Fold : Fold;
    return NormalizeVector([Nx, Ny, Nz]);
}

// 📐 Builds a Binned-SAH BLAS for one FacetSet, offset by GlobalNodeOffset and GlobalTriangleOffset.
export function ConstructFacetBlas(Facets, GlobalNodeOffset, GlobalTriangleOffset)
{
    const FacetCount = Facets.length;
    if (FacetCount === 0)
    {
        return {
            RootNode: -1,
            NodeBytes: new ArrayBuffer(0),
            NodeCount: 0,
            TriangleBytes: new ArrayBuffer(0),
            TriangleCount: 0
        };
    }

    const Centroids    = new Float32Array(FacetCount * 3);
    const FacetMinimum = new Float32Array(FacetCount * 3);
    const FacetMaximum = new Float32Array(FacetCount * 3);
    for (let Facet = 0; Facet < FacetCount; ++Facet)
    {
        const Corners = Facets[Facet].Corners;
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            const A = Corners[0][Axis], B = Corners[1][Axis], C = Corners[2][Axis];
            FacetMinimum[Facet * 3 + Axis] = Math.min(A, B, C);
            FacetMaximum[Facet * 3 + Axis] = Math.max(A, B, C);
            Centroids[Facet * 3 + Axis]    = (A + B + C) / 3.0;
        }
    }

    const Order = new Uint32Array(FacetCount);
    for (let Facet = 0; Facet < FacetCount; ++Facet)
        Order[Facet] = Facet;

    const Nodes = [];
    const AllocateNode = () =>
    {
        Nodes.push({ Minimum: [0, 0, 0], Maximum: [0, 0, 0], LeftOrFirst: 0, Count: 0 });
        return Nodes.length - 1;
    };

    const FitBounds = (Node, First, Count) =>
    {
        let MinX = Infinity, MinY = Infinity, MinZ = Infinity;
        let MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;
        for (let Index = First; Index < First + Count; ++Index)
        {
            const Base = Order[Index] * 3;
            if (FacetMinimum[Base]     < MinX) MinX = FacetMinimum[Base];
            if (FacetMinimum[Base + 1] < MinY) MinY = FacetMinimum[Base + 1];
            if (FacetMinimum[Base + 2] < MinZ) MinZ = FacetMinimum[Base + 2];
            if (FacetMaximum[Base]     > MaxX) MaxX = FacetMaximum[Base];
            if (FacetMaximum[Base + 1] > MaxY) MaxY = FacetMaximum[Base + 1];
            if (FacetMaximum[Base + 2] > MaxZ) MaxZ = FacetMaximum[Base + 2];
        }
        Nodes[Node].Minimum = [MinX - 1.0e-4, MinY - 1.0e-4, MinZ - 1.0e-4];
        Nodes[Node].Maximum = [MaxX + 1.0e-4, MaxY + 1.0e-4, MaxZ + 1.0e-4];
    };

    const SurfaceArea = (Min, Max) =>
    {
        const Dx = Math.max(0.0, Max[0] - Min[0]);
        const Dy = Math.max(0.0, Max[1] - Min[1]);
        const Dz = Math.max(0.0, Max[2] - Min[2]);
        return 2.0 * (Dx * Dy + Dy * Dz + Dz * Dx);
    };

    const Subdivide = (Node, First, Count) =>
    {
        FitBounds(Node, First, Count);
        if (Count <= BlasLeafCapacity)
        {
            Nodes[Node].LeftOrFirst = (GlobalTriangleOffset + First) >>> 0;
            Nodes[Node].Count       = Count >>> 0;
            return;
        }
        let BestAxis = -1, BestSplit = 0.0, BestCost = Infinity;
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            let Low = Infinity, High = -Infinity;
            for (let Index = First; Index < First + Count; ++Index)
            {
                const C = Centroids[Order[Index] * 3 + Axis];
                if (C < Low)  Low = C;
                if (C > High) High = C;
            }
            if (High - Low < 1.0e-7)
                continue;
            const Bins = Array.from({ length: BlasBinCount }, () => ({
                Count: 0,
                Minimum: [Infinity, Infinity, Infinity],
                Maximum: [-Infinity, -Infinity, -Infinity]
            }));
            const Scale = BlasBinCount / (High - Low);
            for (let Index = First; Index < First + Count; ++Index)
            {
                const Facet = Order[Index];
                const Bin = Math.min(BlasBinCount - 1, Math.max(0, Math.floor((Centroids[Facet * 3 + Axis] - Low) * Scale)));
                const Entry = Bins[Bin];
                Entry.Count++;
                for (let Comp = 0; Comp < 3; ++Comp)
                {
                    Entry.Minimum[Comp] = Math.min(Entry.Minimum[Comp], FacetMinimum[Facet * 3 + Comp]);
                    Entry.Maximum[Comp] = Math.max(Entry.Maximum[Comp], FacetMaximum[Facet * 3 + Comp]);
                }
            }
            for (let Split = 1; Split < BlasBinCount; ++Split)
            {
                let LeftCount = 0, RightCount = 0;
                const LeftMin = [Infinity, Infinity, Infinity], LeftMax = [-Infinity, -Infinity, -Infinity];
                const RightMin = [Infinity, Infinity, Infinity], RightMax = [-Infinity, -Infinity, -Infinity];
                for (let Bin = 0; Bin < BlasBinCount; ++Bin)
                {
                    const Entry = Bins[Bin];
                    if (Entry.Count === 0)
                        continue;
                    if (Bin < Split)
                    {
                        LeftCount += Entry.Count;
                        for (let Comp = 0; Comp < 3; ++Comp)
                        {
                            if (Entry.Minimum[Comp] < LeftMin[Comp]) LeftMin[Comp] = Entry.Minimum[Comp];
                            if (Entry.Maximum[Comp] > LeftMax[Comp]) LeftMax[Comp] = Entry.Maximum[Comp];
                        }
                    }
                    else
                    {
                        RightCount += Entry.Count;
                        for (let Comp = 0; Comp < 3; ++Comp)
                        {
                            if (Entry.Minimum[Comp] < RightMin[Comp]) RightMin[Comp] = Entry.Minimum[Comp];
                            if (Entry.Maximum[Comp] > RightMax[Comp]) RightMax[Comp] = Entry.Maximum[Comp];
                        }
                    }
                }
                if (LeftCount === 0 || RightCount === 0)
                    continue;
                const Cost = SurfaceArea(LeftMin, LeftMax) * LeftCount + SurfaceArea(RightMin, RightMax) * RightCount;
                if (Cost < BestCost)
                {
                    BestCost  = Cost;
                    BestAxis  = Axis;
                    BestSplit = Low + Split / Scale;
                }
            }
        }
        const LeafCost = SurfaceArea(Nodes[Node].Minimum, Nodes[Node].Maximum) * Count;
        let Middle = First;
        if (BestAxis >= 0 && BestCost < LeafCost)
        {
            let Cursor = First, Tail = First + Count - 1;
            while (Cursor <= Tail)
            {
                if (Centroids[Order[Cursor] * 3 + BestAxis] < BestSplit)
                    ++Cursor;
                else
                {
                    const Temp = Order[Cursor];
                    Order[Cursor] = Order[Tail];
                    Order[Tail] = Temp;
                    --Tail;
                }
            }
            Middle = Cursor;
        }
        if (Middle === First || Middle === First + Count)
        {
            if (Count <= BlasLeafCapacity * 3)
            {
                Nodes[Node].LeftOrFirst = (GlobalTriangleOffset + First) >>> 0;
                Nodes[Node].Count       = Count >>> 0;
                return;
            }
            Middle = First + (Count >> 1);
        }
        const Left = AllocateNode();
        AllocateNode();
        Nodes[Node].LeftOrFirst = (GlobalNodeOffset + Left) >>> 0;
        Nodes[Node].Count       = 0;
        Subdivide(Left, First, Middle - First);
        Subdivide(Left + 1, Middle, First + Count - Middle);
    };

    const Root = AllocateNode();
    Subdivide(Root, 0, FacetCount);

    const NodeBytes    = new ArrayBuffer(Nodes.length * 32);
    const NodeScalars  = new Float32Array(NodeBytes);
    const NodeIntegers = new Uint32Array(NodeBytes);
    for (let Index = 0; Index < Nodes.length; ++Index)
    {
        const Node = Nodes[Index];
        const Base = Index * 8;
        NodeScalars[Base + 0]  = Node.Minimum[0];
        NodeScalars[Base + 1]  = Node.Minimum[1];
        NodeScalars[Base + 2]  = Node.Minimum[2];
        NodeIntegers[Base + 3] = Node.LeftOrFirst >>> 0;
        NodeScalars[Base + 4]  = Node.Maximum[0];
        NodeScalars[Base + 5]  = Node.Maximum[1];
        NodeScalars[Base + 6]  = Node.Maximum[2];
        NodeIntegers[Base + 7] = Node.Count >>> 0;
    }

    // 📝 64 bytes (16 scalars) per triangle:
    //    vec4[0]: A.x,  A.y,  A.z,  bitcast<f32>(Family)
    //    vec4[1]: E1.x, E1.y, E1.z, bitcast<f32>(PackOctahedral(NA))
    //    vec4[2]: E2.x, E2.y, E2.z, bitcast<f32>(PackOctahedral(NB))
    //    vec4[3]: Ng.x, Ng.y, Ng.z, bitcast<f32>(PackOctahedral(NC))
    const TriangleBytes    = new ArrayBuffer(FacetCount * 64);
    const TriangleScalars  = new Float32Array(TriangleBytes);
    const TriangleIntegers = new Uint32Array(TriangleBytes);
    for (let Index = 0; Index < FacetCount; ++Index)
    {
        const Facet = Facets[Order[Index]];
        const A  = Facet.Corners[0];
        const B  = Facet.Corners[1];
        const C  = Facet.Corners[2];
        const E1 = SubtractVector(B, A);
        const E2 = SubtractVector(C, A);
        const Ng = NormalizeVector(CrossVector(E1, E2));
        const NA = Facet.Normals ? NormalizeVector(Facet.Normals[0]) : Ng;
        const NB = Facet.Normals ? NormalizeVector(Facet.Normals[1]) : Ng;
        const NC = Facet.Normals ? NormalizeVector(Facet.Normals[2]) : Ng;
        const Base = Index * 16;

        TriangleScalars[Base + 0]   = A[0];
        TriangleScalars[Base + 1]   = A[1];
        TriangleScalars[Base + 2]   = A[2];
        TriangleIntegers[Base + 3]  = (Facet.Family ?? 0) >>> 0;

        TriangleScalars[Base + 4]   = E1[0];
        TriangleScalars[Base + 5]   = E1[1];
        TriangleScalars[Base + 6]   = E1[2];
        TriangleIntegers[Base + 7]  = PackOctahedralCpu(NA);

        TriangleScalars[Base + 8]   = E2[0];
        TriangleScalars[Base + 9]   = E2[1];
        TriangleScalars[Base + 10]  = E2[2];
        TriangleIntegers[Base + 11] = PackOctahedralCpu(NB);

        TriangleScalars[Base + 12]  = Ng[0];
        TriangleScalars[Base + 13]  = Ng[1];
        TriangleScalars[Base + 14]  = Ng[2];
        TriangleIntegers[Base + 15] = PackOctahedralCpu(NC);
    }

    return {
        RootNode: GlobalNodeOffset + Root,
        NodeBytes,
        NodeCount: Nodes.length,
        TriangleBytes,
        TriangleCount: FacetCount
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  RAY TRACING SPACE
//------------------------------------------------------------------------------------------------------------------------

export class TriangleHierarchySpace
{
    constructor(Exchange)
    {
        this.Exchange           = Exchange;
        this.FacetBlasRoots     = [];
        this.OccupantBlasRoot   = new Int32Array(0);
        this.StaticNodeCount    = 0;
        this.StaticTriCount     = 0;
        this.TotalNodeCapacity  = 0;
        this.TotalTriangles     = 0;
        this.ActiveInstances    = 0;
        this.UsedTlasNodes      = 0;
        this.NodeRecords         = null;
        this.TriangleRecords     = null;
    }

    BuildCpuHierarchy(Scene)
    {
        let NodeCursor     = TlasNodeCapacity;
        let TriangleCursor = 0;
        const BlasResults  = [];
        this.FacetBlasRoots = new Int32Array(Scene.FacetSets.length).fill(-1);

        for (let SetIndex = 0; SetIndex < Scene.FacetSets.length; ++SetIndex)
        {
            const Set = Scene.FacetSets[SetIndex];
            if (!Set.Facets || Set.Facets.length === 0)
                continue;
            const Blas = ConstructFacetBlas(Set.Facets, NodeCursor, TriangleCursor);
            this.FacetBlasRoots[SetIndex] = Blas.RootNode;
            Set.BlasRoot = Blas.RootNode;
            NodeCursor     += Blas.NodeCount;
            TriangleCursor += Blas.TriangleCount;
            BlasResults.push(Blas);
        }

        this.StaticNodeCount    = NodeCursor;
        this.StaticTriCount     = TriangleCursor;
        this.DynamicNodeOffset  = NodeCursor;
        this.DynamicTriOffset   = TriangleCursor;
        this.TotalNodeCapacity  = NodeCursor + DynamicBlasNodeCapacity;
        this.TotalTriCapacity   = TriangleCursor + DynamicTriangleCapacity;
        this.TotalTriangles     = TriangleCursor;

        this.AllNodeBytes       = new ArrayBuffer(this.TotalNodeCapacity * 32);
        this.AllNodeScalars     = new Float32Array(this.AllNodeBytes);
        this.AllNodeIntegers    = new Uint32Array(this.AllNodeBytes);

        this.AllTriBytes        = new ArrayBuffer(this.TotalTriCapacity * 64);
        this.AllTriScalars      = new Float32Array(this.AllTriBytes);
        this.AllTriIntegers     = new Uint32Array(this.AllTriBytes);

        let WriteNode = TlasNodeCapacity;
        let WriteTri  = 0;
        for (const Blas of BlasResults)
        {
            new Uint8Array(this.AllNodeBytes, WriteNode * 32, Blas.NodeBytes.byteLength).set(new Uint8Array(Blas.NodeBytes));
            new Uint8Array(this.AllTriBytes, WriteTri * 64, Blas.TriangleBytes.byteLength).set(new Uint8Array(Blas.TriangleBytes));
            WriteNode += Blas.NodeCount;
            WriteTri  += Blas.TriangleCount;
        }

        this.OccupantBlasRoot = new Int32Array(Scene.Occupants.length).fill(-1);
        for (let Index = 0; Index < Scene.Occupants.length; ++Index)
        {
            const Occupant = Scene.Occupants[Index];
            this.OccupantBlasRoot[Index] = this.FacetBlasRoots[Occupant.FacetSet];
        }

        // Pre-allocate scratch buffers for zero-allocation per-frame TLAS construction
        const MaxOccupants  = Scene.Occupants.length;
        this.ScratchOrder   = new Uint32Array(MaxOccupants);
        this.ScratchOccIdx  = new Uint32Array(MaxOccupants);
        this.ScratchMinX    = new Float32Array(MaxOccupants);
        this.ScratchMinY    = new Float32Array(MaxOccupants);
        this.ScratchMinZ    = new Float32Array(MaxOccupants);
        this.ScratchMaxX    = new Float32Array(MaxOccupants);
        this.ScratchMaxY    = new Float32Array(MaxOccupants);
        this.ScratchMaxZ    = new Float32Array(MaxOccupants);
        this.ScratchCentX   = new Float32Array(MaxOccupants);
        this.ScratchCentY   = new Float32Array(MaxOccupants);
        this.ScratchCentZ   = new Float32Array(MaxOccupants);
    }

    // 📝 The buffers are bound by NearestDepthSpace.ConstructTraversalGroups into the reflection group (group 3, bindings 5 and 6).
    ConstructResources(Scene)
    {
        this.BuildCpuHierarchy(Scene);
        if (this.Exchange && this.Exchange.Device)
        {
            this.NodeRecords = this.Exchange.ConstructRecordsFrom(
                this.AllNodeScalars,
                GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
                "Triangle hierarchy nodes (TLAS + BLAS)"
            );
            this.TriangleRecords = this.Exchange.ConstructRecordsFrom(
                this.AllTriScalars,
                GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST,
                "Triangle hierarchy triangles"
            );
        }
        this.UpdateTlas(Scene, null);
    }

    // 📝 Registers per-fragment BLAS leaf nodes and triangles when a breakable prop shatters in FractureSequence.
    RegisterPropFragments(BreakableIndex, PieceFirst, Capacity, WrittenFragments)
    {
        for (let Slot = 0; Slot < Capacity; ++Slot)
            this.OccupantBlasRoot[PieceFirst + Slot] = -1;

        if (!WrittenFragments || WrittenFragments.length === 0)
            return;

        const NodeStart = this.DynamicNodeOffset + BreakableIndex * 36;
        const TriStart  = this.DynamicTriOffset + BreakableIndex * 576;
        let TriCursor   = TriStart;
        let NodeCount   = 0;

        for (let FragIdx = 0; FragIdx < WrittenFragments.length; ++FragIdx)
        {
            const Fragment  = WrittenFragments[FragIdx];
            const Triangles = Fragment.Triangles || [];
            if (Triangles.length === 0 || NodeStart + FragIdx >= this.TotalNodeCapacity || TriCursor + Triangles.length > this.TotalTriCapacity)
                continue;

            let MinX = Infinity, MinY = Infinity, MinZ = Infinity;
            let MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;
            const FirstTri = TriCursor;

            for (const Tri of Triangles)
            {
                const [A, B, C] = Tri.Corners;
                for (const Corner of [A, B, C])
                {
                    if (Corner[0] < MinX) MinX = Corner[0];
                    if (Corner[1] < MinY) MinY = Corner[1];
                    if (Corner[2] < MinZ) MinZ = Corner[2];
                    if (Corner[0] > MaxX) MaxX = Corner[0];
                    if (Corner[1] > MaxY) MaxY = Corner[1];
                    if (Corner[2] > MaxZ) MaxZ = Corner[2];
                }
                const E1 = SubtractVector(B, A);
                const E2 = SubtractVector(C, A);
                const Ng = NormalizeVector(CrossVector(E1, E2));
                const PackedN = PackOctahedralCpu(Ng);
                const Base = TriCursor * 16;

                this.AllTriScalars[Base + 0]   = A[0];
                this.AllTriScalars[Base + 1]   = A[1];
                this.AllTriScalars[Base + 2]   = A[2];
                this.AllTriIntegers[Base + 3]  = (Tri.Family ?? 0) >>> 0;

                this.AllTriScalars[Base + 4]   = E1[0];
                this.AllTriScalars[Base + 5]   = E1[1];
                this.AllTriScalars[Base + 6]   = E1[2];
                this.AllTriIntegers[Base + 7]  = PackedN;

                this.AllTriScalars[Base + 8]   = E2[0];
                this.AllTriScalars[Base + 9]   = E2[1];
                this.AllTriScalars[Base + 10]  = E2[2];
                this.AllTriIntegers[Base + 11] = PackedN;

                this.AllTriScalars[Base + 12]  = Ng[0];
                this.AllTriScalars[Base + 13]  = Ng[1];
                this.AllTriScalars[Base + 14]  = Ng[2];
                this.AllTriIntegers[Base + 15] = PackedN;
                ++TriCursor;
            }

            const NodeIndex = NodeStart + FragIdx;
            const NodeBase  = NodeIndex * 8;
            this.AllNodeScalars[NodeBase + 0]  = MinX - 1.0e-3;
            this.AllNodeScalars[NodeBase + 1]  = MinY - 1.0e-3;
            this.AllNodeScalars[NodeBase + 2]  = MinZ - 1.0e-3;
            this.AllNodeIntegers[NodeBase + 3] = FirstTri >>> 0;
            this.AllNodeScalars[NodeBase + 4]  = MaxX + 1.0e-3;
            this.AllNodeScalars[NodeBase + 5]  = MaxY + 1.0e-3;
            this.AllNodeScalars[NodeBase + 6]  = MaxZ + 1.0e-3;
            this.AllNodeIntegers[NodeBase + 7] = Triangles.length >>> 0;

            this.OccupantBlasRoot[PieceFirst + FragIdx] = NodeIndex;
            ++NodeCount;
        }

        if (this.Exchange && this.Exchange.Device && NodeCount > 0)
        {
            const Device = this.Exchange.Device;
            Device.queue.writeBuffer(this.NodeRecords, NodeStart * 32, this.AllNodeBytes, NodeStart * 32, NodeCount * 32);
            const TriBytesWritten = (TriCursor - TriStart) * 64;
            if (TriBytesWritten > 0)
                Device.queue.writeBuffer(this.TriangleRecords, TriStart * 64, this.AllTriBytes, TriStart * 64, TriBytesWritten);
        }
    }

    ClearPropFragments(PieceFirst, Capacity)
    {
        for (let Slot = 0; Slot < Capacity; ++Slot)
            this.OccupantBlasRoot[PieceFirst + Slot] = -1;
    }

    // 📝 Rebuilds the Top-Level Acceleration Structure (TLAS) over all active scene occupants and uploads it to BvhNodes[0..UsedTlasNodes].
    UpdateTlas(Scene, Vehicle = null)
    {
        if (Vehicle && Vehicle.Wheels && Scene.VehicleOccupants)
        {
            for (let Wheel = 0; Wheel < 4; ++Wheel)
            {
                const OccIdx = Scene.VehicleOccupants.Wheels[Wheel];
                if (OccIdx >= 0 && Scene.Occupants[OccIdx])
                {
                    if (Vehicle.Wheels[Wheel].Shredded)
                        Scene.Occupants[OccIdx].Roles |= RoleShredded;
                    else
                        Scene.Occupants[OccIdx].Roles &= ~RoleShredded;
                }
            }
        }

        const Occupants  = Scene.Occupants;
        const GroundIdx  = Scene.GroundOccupant;
        const OccIndices = this.ScratchOccIdx;
        const Order      = this.ScratchOrder;
        const MinX = this.ScratchMinX, MinY = this.ScratchMinY, MinZ = this.ScratchMinZ;
        const MaxX = this.ScratchMaxX, MaxY = this.ScratchMaxY, MaxZ = this.ScratchMaxZ;
        const CentX = this.ScratchCentX, CentY = this.ScratchCentY, CentZ = this.ScratchCentZ;

        let ObjectCount = 0;
        for (let Index = 0; Index < Occupants.length; ++Index)
        {
            if (Index === GroundIdx)
                continue;
            const Occupant = Occupants[Index];
            if (Occupant.Inactive)
                continue;
            const BlasRoot = this.OccupantBlasRoot[Index];
            if (BlasRoot < 0)
                continue;
            const IsDrawable = ((Occupant.Roles & OccupantRole.Raster) !== 0) || Occupant.Fragment === true;
            if (!IsDrawable)
                continue;

            const Bounds = Scene.QueryWorldBounds(Index);
            const K = ObjectCount++;
            Order[K]      = K;
            OccIndices[K] = Index;
            MinX[K]       = Bounds.Minimum[0];
            MinY[K]       = Bounds.Minimum[1];
            MinZ[K]       = Bounds.Minimum[2];
            MaxX[K]       = Bounds.Maximum[0];
            MaxY[K]       = Bounds.Maximum[1];
            MaxZ[K]       = Bounds.Maximum[2];
            CentX[K]      = 0.5 * (Bounds.Minimum[0] + Bounds.Maximum[0]);
            CentY[K]      = 0.5 * (Bounds.Minimum[1] + Bounds.Maximum[1]);
            CentZ[K]      = 0.5 * (Bounds.Minimum[2] + Bounds.Maximum[2]);
        }

        const NodeScalars  = this.AllNodeScalars;
        const NodeIntegers = this.AllNodeIntegers;

        const WriteLeafNode = (NodeIdx, ItemK) =>
        {
            const OccIdx = OccIndices[ItemK];
            const Base   = NodeIdx * 8;
            NodeScalars[Base + 0]  = MinX[ItemK];
            NodeScalars[Base + 1]  = MinY[ItemK];
            NodeScalars[Base + 2]  = MinZ[ItemK];
            NodeIntegers[Base + 3] = this.OccupantBlasRoot[OccIdx] >>> 0;
            NodeScalars[Base + 4]  = MaxX[ItemK];
            NodeScalars[Base + 5]  = MaxY[ItemK];
            NodeScalars[Base + 6]  = MaxZ[ItemK];
            NodeIntegers[Base + 7] = ((TlasLeafBit | OccIdx) >>> 0);
        };

        const WriteOccupantLeaf = (NodeIdx, OccIdx) =>
        {
            const Bounds = Scene.QueryWorldBounds(OccIdx);
            const Base   = NodeIdx * 8;
            NodeScalars[Base + 0]  = Bounds.Minimum[0];
            NodeScalars[Base + 1]  = Bounds.Minimum[1];
            NodeScalars[Base + 2]  = Bounds.Minimum[2];
            NodeIntegers[Base + 3] = this.OccupantBlasRoot[OccIdx] >>> 0;
            NodeScalars[Base + 4]  = Bounds.Maximum[0];
            NodeScalars[Base + 5]  = Bounds.Maximum[1];
            NodeScalars[Base + 6]  = Bounds.Maximum[2];
            NodeIntegers[Base + 7] = ((TlasLeafBit | OccIdx) >>> 0);
        };

        const HasGround = GroundIdx >= 0 && !Occupants[GroundIdx].Inactive && this.OccupantBlasRoot[GroundIdx] >= 0;
        this.ActiveInstances = ObjectCount + (HasGround ? 1 : 0);

        if (ObjectCount === 0 && !HasGround)
        {
            NodeScalars.fill(0, 0, 8);
            NodeIntegers[3] = EmptyTlasSentinel;
            NodeIntegers[7] = 0;
            this.UsedTlasNodes = 1;
            this.UploadTlas();
            return;
        }

        if (ObjectCount === 0 && HasGround)
        {
            WriteOccupantLeaf(0, GroundIdx);
            this.UsedTlasNodes = 1;
            this.UploadTlas();
            return;
        }

        let NextNode = 1;
        let ObjectsRootNode = 0;
        if (HasGround)
        {
            // Node 0 = Root interior (children 1 and 2); Node 1 = GroundOccupant leaf; Node 2 = Objects subtree root
            WriteOccupantLeaf(1, GroundIdx);
            ObjectsRootNode = 2;
            NextNode        = 3;
        }

        const SubdivideObjects = (NodeIdx, First, Count) =>
        {
            if (Count === 1)
            {
                WriteLeafNode(NodeIdx, Order[First]);
                return;
            }

            let BoxMinX = Infinity, BoxMinY = Infinity, BoxMinZ = Infinity;
            let BoxMaxX = -Infinity, BoxMaxY = -Infinity, BoxMaxZ = -Infinity;
            let CMinX = Infinity, CMinY = Infinity, CMinZ = Infinity;
            let CMaxX = -Infinity, CMaxY = -Infinity, CMaxZ = -Infinity;

            for (let I = First; I < First + Count; ++I)
            {
                const K = Order[I];
                if (MinX[K] < BoxMinX) BoxMinX = MinX[K];
                if (MinY[K] < BoxMinY) BoxMinY = MinY[K];
                if (MinZ[K] < BoxMinZ) BoxMinZ = MinZ[K];
                if (MaxX[K] > BoxMaxX) BoxMaxX = MaxX[K];
                if (MaxY[K] > BoxMaxY) BoxMaxY = MaxY[K];
                if (MaxZ[K] > BoxMaxZ) BoxMaxZ = MaxZ[K];
                if (CentX[K] < CMinX) CMinX = CentX[K];
                if (CentY[K] < CMinY) CMinY = CentY[K];
                if (CentZ[K] < CMinZ) CMinZ = CentZ[K];
                if (CentX[K] > CMaxX) CMaxX = CentX[K];
                if (CentY[K] > CMaxY) CMaxY = CentY[K];
                if (CentZ[K] > CMaxZ) CMaxZ = CentZ[K];
            }

            const Base = NodeIdx * 8;
            NodeScalars[Base + 0] = BoxMinX;
            NodeScalars[Base + 1] = BoxMinY;
            NodeScalars[Base + 2] = BoxMinZ;
            NodeScalars[Base + 4] = BoxMaxX;
            NodeScalars[Base + 5] = BoxMaxY;
            NodeScalars[Base + 6] = BoxMaxZ;

            // Choose split axis along largest centroid extent
            const SpanX = CMaxX - CMinX;
            const SpanY = CMaxY - CMinY;
            const SpanZ = CMaxZ - CMinZ;
            let Axis = 0;
            let Low = CMinX, High = CMaxX;
            let CentArr = CentX;
            if (SpanY >= SpanX && SpanY >= SpanZ)
            {
                Axis = 1; Low = CMinY; High = CMaxY; CentArr = CentY;
            }
            else if (SpanZ >= SpanX && SpanZ >= SpanY)
            {
                Axis = 2; Low = CMinZ; High = CMaxZ; CentArr = CentZ;
            }

            let Middle = First + (Count >> 1);
            if (High - Low > 1.0e-5 && Count >= 4)
            {
                // Binned SAH split along chosen axis
                const BinCounts = new Uint32Array(TlasBinCount);
                const BinMinX   = new Float32Array(TlasBinCount).fill(Infinity);
                const BinMinY   = new Float32Array(TlasBinCount).fill(Infinity);
                const BinMinZ   = new Float32Array(TlasBinCount).fill(Infinity);
                const BinMaxX   = new Float32Array(TlasBinCount).fill(-Infinity);
                const BinMaxY   = new Float32Array(TlasBinCount).fill(-Infinity);
                const BinMaxZ   = new Float32Array(TlasBinCount).fill(-Infinity);
                const Scale     = TlasBinCount / (High - Low);

                for (let I = First; I < First + Count; ++I)
                {
                    const K = Order[I];
                    const B = Math.min(TlasBinCount - 1, Math.max(0, Math.floor((CentArr[K] - Low) * Scale)));
                    BinCounts[B]++;
                    if (MinX[K] < BinMinX[B]) BinMinX[B] = MinX[K];
                    if (MinY[K] < BinMinY[B]) BinMinY[B] = MinY[K];
                    if (MinZ[K] < BinMinZ[B]) BinMinZ[B] = MinZ[K];
                    if (MaxX[K] > BinMaxX[B]) BinMaxX[B] = MaxX[K];
                    if (MaxY[K] > BinMaxY[B]) BinMaxY[B] = MaxY[K];
                    if (MaxZ[K] > BinMaxZ[B]) BinMaxZ[B] = MaxZ[K];
                }

                let BestCost  = Infinity;
                let BestSplit = 0.5 * (Low + High);
                for (let Split = 1; Split < TlasBinCount; ++Split)
                {
                    let LC = 0, RC = 0;
                    let LMinX = Infinity, LMinY = Infinity, LMinZ = Infinity, LMaxX = -Infinity, LMaxY = -Infinity, LMaxZ = -Infinity;
                    let RMinX = Infinity, RMinY = Infinity, RMinZ = Infinity, RMaxX = -Infinity, RMaxY = -Infinity, RMaxZ = -Infinity;
                    for (let B = 0; B < TlasBinCount; ++B)
                    {
                        if (BinCounts[B] === 0) continue;
                        if (B < Split)
                        {
                            LC += BinCounts[B];
                            if (BinMinX[B] < LMinX) LMinX = BinMinX[B];
                            if (BinMinY[B] < LMinY) LMinY = BinMinY[B];
                            if (BinMinZ[B] < LMinZ) LMinZ = BinMinZ[B];
                            if (BinMaxX[B] > LMaxX) LMaxX = BinMaxX[B];
                            if (BinMaxY[B] > LMaxY) LMaxY = BinMaxY[B];
                            if (BinMaxZ[B] > LMaxZ) LMaxZ = BinMaxZ[B];
                        }
                        else
                        {
                            RC += BinCounts[B];
                            if (BinMinX[B] < RMinX) RMinX = BinMinX[B];
                            if (BinMinY[B] < RMinY) RMinY = BinMinY[B];
                            if (BinMinZ[B] < RMinZ) RMinZ = BinMinZ[B];
                            if (BinMaxX[B] > RMaxX) RMaxX = BinMaxX[B];
                            if (BinMaxY[B] > RMaxY) RMaxY = BinMaxY[B];
                            if (BinMaxZ[B] > RMaxZ) RMaxZ = BinMaxZ[B];
                        }
                    }
                    if (LC === 0 || RC === 0) continue;
                    const Lx = Math.max(0, LMaxX - LMinX), Ly = Math.max(0, LMaxY - LMinY), Lz = Math.max(0, LMaxZ - LMinZ);
                    const Rx = Math.max(0, RMaxX - RMinX), Ry = Math.max(0, RMaxY - RMinY), Rz = Math.max(0, RMaxZ - RMinZ);
                    const Cost = (Lx * Ly + Ly * Lz + Lz * Lx) * LC + (Rx * Ry + Ry * Rz + Rz * Rx) * RC;
                    if (Cost < BestCost)
                    {
                        BestCost  = Cost;
                        BestSplit = Low + Split / Scale;
                    }
                }

                let Cursor = First, Tail = First + Count - 1;
                while (Cursor <= Tail)
                {
                    if (CentArr[Order[Cursor]] < BestSplit)
                        ++Cursor;
                    else
                    {
                        const Temp = Order[Cursor];
                        Order[Cursor] = Order[Tail];
                        Order[Tail] = Temp;
                        --Tail;
                    }
                }
                if (Cursor > First && Cursor < First + Count)
                    Middle = Cursor;
            }
            else if (High - Low > 1.0e-5)
            {
                const Pivot = 0.5 * (Low + High);
                let Cursor = First, Tail = First + Count - 1;
                while (Cursor <= Tail)
                {
                    if (CentArr[Order[Cursor]] < Pivot)
                        ++Cursor;
                    else
                    {
                        const Temp = Order[Cursor];
                        Order[Cursor] = Order[Tail];
                        Order[Tail] = Temp;
                        --Tail;
                    }
                }
                if (Cursor > First && Cursor < First + Count)
                    Middle = Cursor;
            }

            const LeftNode  = NextNode;
            const RightNode = NextNode + 1;
            NextNode += 2;
            NodeIntegers[Base + 3] = LeftNode >>> 0;
            NodeIntegers[Base + 7] = 0;

            SubdivideObjects(LeftNode, First, Middle - First);
            SubdivideObjects(RightNode, Middle, First + Count - Middle);
        };

        SubdivideObjects(ObjectsRootNode, 0, ObjectCount);

        if (HasGround)
        {
            const Base = 0;
            NodeScalars[Base + 0]  = Math.min(NodeScalars[8 + 0], NodeScalars[16 + 0]);
            NodeScalars[Base + 1]  = Math.min(NodeScalars[8 + 1], NodeScalars[16 + 1]);
            NodeScalars[Base + 2]  = Math.min(NodeScalars[8 + 2], NodeScalars[16 + 2]);
            NodeIntegers[Base + 3] = 1;
            NodeScalars[Base + 4]  = Math.max(NodeScalars[8 + 4], NodeScalars[16 + 4]);
            NodeScalars[Base + 5]  = Math.max(NodeScalars[8 + 5], NodeScalars[16 + 5]);
            NodeScalars[Base + 6]  = Math.max(NodeScalars[8 + 6], NodeScalars[16 + 6]);
            NodeIntegers[Base + 7] = 0;
        }

        this.UsedTlasNodes = NextNode;
        this.UploadTlas();
    }

    UploadTlas()
    {
        if (this.Exchange && this.Exchange.Device && this.NodeRecords && this.UsedTlasNodes > 0)
        {
            this.Exchange.Device.queue.writeBuffer(this.NodeRecords, 0, this.AllNodeBytes, 0, this.UsedTlasNodes * 32);
        }
    }

    //--------------------------------------------------------------------------------------------------------------------
    //                                       CPU REFERENCE RAY-SCENE INTERSECTION
    //--------------------------------------------------------------------------------------------------------------------

    // 📐 Reference CPU ray-scene intersection over the exact same TLAS + BLAS buffers used on the GPU.
    TraceRayCpu(Scene, Origin, Direction, MinimumTravel = 1.0e-3, MaximumTravel = 500.0)
    {
        const NodeScalars  = this.AllNodeScalars;
        const NodeIntegers = this.AllNodeIntegers;
        const TriScalars   = this.AllTriScalars;
        const TriIntegers  = this.AllTriIntegers;

        if (NodeIntegers[3] === EmptyTlasSentinel)
            return { Hit: false, Travel: MaximumTravel };

        const SafeRecip = (D) => [
            Math.abs(D[0]) < 1.0e-9 ? 1.0e9 : 1.0 / D[0],
            Math.abs(D[1]) < 1.0e-9 ? 1.0e9 : 1.0 / D[1],
            Math.abs(D[2]) < 1.0e-9 ? 1.0e9 : 1.0 / D[2]
        ];

        const IntersectAabb = (Orig, Recip, NodeBase, Limit) =>
        {
            const Nx = (NodeScalars[NodeBase + 0] - Orig[0]) * Recip[0];
            const Fx = (NodeScalars[NodeBase + 4] - Orig[0]) * Recip[0];
            const Ny = (NodeScalars[NodeBase + 1] - Orig[1]) * Recip[1];
            const Fy = (NodeScalars[NodeBase + 5] - Orig[1]) * Recip[1];
            const Nz = (NodeScalars[NodeBase + 2] - Orig[2]) * Recip[2];
            const Fz = (NodeScalars[NodeBase + 6] - Orig[2]) * Recip[2];
            const Entry = Math.max(Math.min(Nx, Fx), Math.min(Ny, Fy), Math.min(Nz, Fz), 0.0);
            const Exit  = Math.min(Math.max(Nx, Fx), Math.max(Ny, Fy), Math.max(Nz, Fz), Limit);
            return Entry <= Exit ? Entry : 1.0e30;
        };

        const WorldRecip = SafeRecip(Direction);
        const Stack      = new Uint32Array(64);
        let Depth        = 1;
        Stack[0]         = 0;
        let BlasFloor    = 0xFFFFFFFF;

        let Nearest      = MaximumTravel;
        let BestTri      = -1;
        let BestOcc      = -1;
        let BestU        = 0.0;
        let BestV        = 0.0;
        let Steps        = 0;

        let CurrentOcc   = -1;
        let OccRoles     = 0;
        let LocalOrigin  = Origin;
        let LocalDir     = Direction;
        let LocalRecip   = WorldRecip;

        while (Depth > 0 && Steps < 1024)
        {
            if (Depth <= BlasFloor)
                BlasFloor = 0xFFFFFFFF;

            const NodeIdx  = Stack[--Depth];
            const NodeBase = NodeIdx * 8;
            const LeftOrFirst = NodeIntegers[NodeBase + 3];
            const Count       = NodeIntegers[NodeBase + 7];
            Steps++;

            if (BlasFloor === 0xFFFFFFFF)
            {
                if (IntersectAabb(Origin, WorldRecip, NodeBase, Nearest) >= Nearest)
                    continue;
                if ((Count & TlasLeafBit) !== 0)
                {
                    CurrentOcc = Count & TlasIndexMask;
                    const Occ  = Scene.Occupants[CurrentOcc];
                    OccRoles   = Occ.Roles;
                    const Inv  = this.InvertRigidOrAffine(Occ.Transform);
                    LocalOrigin = [
                        Inv[0] * Origin[0] + Inv[4] * Origin[1] + Inv[8]  * Origin[2] + Inv[12],
                        Inv[1] * Origin[0] + Inv[5] * Origin[1] + Inv[9]  * Origin[2] + Inv[13],
                        Inv[2] * Origin[0] + Inv[6] * Origin[1] + Inv[10] * Origin[2] + Inv[14]
                    ];
                    LocalDir = [
                        Inv[0] * Direction[0] + Inv[4] * Direction[1] + Inv[8]  * Direction[2],
                        Inv[1] * Direction[0] + Inv[5] * Direction[1] + Inv[9]  * Direction[2],
                        Inv[2] * Direction[0] + Inv[6] * Direction[1] + Inv[10] * Direction[2]
                    ];
                    LocalRecip = SafeRecip(LocalDir);
                    BlasFloor  = Depth;
                    Stack[Depth++] = LeftOrFirst;
                }
                else
                {
                    const Left  = LeftOrFirst;
                    const Right = LeftOrFirst + 1;
                    const LDist = IntersectAabb(Origin, WorldRecip, Left * 8, Nearest);
                    const RDist = IntersectAabb(Origin, WorldRecip, Right * 8, Nearest);
                    if (LDist < RDist)
                    {
                        if (RDist < Nearest) Stack[Depth++] = Right;
                        if (LDist < Nearest) Stack[Depth++] = Left;
                    }
                    else
                    {
                        if (LDist < Nearest) Stack[Depth++] = Left;
                        if (RDist < Nearest) Stack[Depth++] = Right;
                    }
                }
            }
            else
            {
                if (IntersectAabb(LocalOrigin, LocalRecip, NodeBase, Nearest) >= Nearest)
                    continue;
                if (Count > 0)
                {
                    for (let Tri = LeftOrFirst; Tri < LeftOrFirst + Count; ++Tri)
                    {
                        const TBase     = Tri * 16;
                        const RawFamily = TriIntegers[TBase + 3];
                        if (RawFamily === CourseMaterial.VehicleGlass)
                            continue;
                        if (RawFamily === CourseMaterial.Tyre && (OccRoles & RoleShredded) !== 0)
                            continue;

                        const Ax  = TriScalars[TBase + 0], Ay  = TriScalars[TBase + 1], Az  = TriScalars[TBase + 2];
                        const E1x = TriScalars[TBase + 4], E1y = TriScalars[TBase + 5], E1z = TriScalars[TBase + 6];
                        const E2x = TriScalars[TBase + 8], E2y = TriScalars[TBase + 9], E2z = TriScalars[TBase + 10];

                        const Px  = LocalDir[1] * E2z - LocalDir[2] * E2y;
                        const Py  = LocalDir[2] * E2x - LocalDir[0] * E2z;
                        const Pz  = LocalDir[0] * E2y - LocalDir[1] * E2x;
                        const Det = E1x * Px + E1y * Py + E1z * Pz;
                        if (Math.abs(Det) < 1.0e-10)
                            continue;
                        const InvDet = 1.0 / Det;
                        const Tx = LocalOrigin[0] - Ax, Ty = LocalOrigin[1] - Ay, Tz = LocalOrigin[2] - Az;
                        const U  = (Tx * Px + Ty * Py + Tz * Pz) * InvDet;
                        if (U < 0.0 || U > 1.0)
                            continue;
                        const Qx = Ty * E1z - Tz * E1y;
                        const Qy = Tz * E1x - Tx * E1z;
                        const Qz = Tx * E1y - Ty * E1x;
                        const V  = (LocalDir[0] * Qx + LocalDir[1] * Qy + LocalDir[2] * Qz) * InvDet;
                        if (V < 0.0 || U + V > 1.0)
                            continue;
                        const Travel = (E2x * Qx + E2y * Qy + E2z * Qz) * InvDet;
                        if (Travel >= MinimumTravel && Travel < Nearest)
                        {
                            Nearest = Travel;
                            BestTri = Tri;
                            BestOcc = CurrentOcc;
                            BestU   = U;
                            BestV   = V;
                        }
                    }
                }
                else
                {
                    const Left  = LeftOrFirst;
                    const Right = LeftOrFirst + 1;
                    const LDist = IntersectAabb(LocalOrigin, LocalRecip, Left * 8, Nearest);
                    const RDist = IntersectAabb(LocalOrigin, LocalRecip, Right * 8, Nearest);
                    if (LDist < RDist)
                    {
                        if (RDist < Nearest) Stack[Depth++] = Right;
                        if (LDist < Nearest) Stack[Depth++] = Left;
                    }
                    else
                    {
                        if (LDist < Nearest) Stack[Depth++] = Left;
                        if (RDist < Nearest) Stack[Depth++] = Right;
                    }
                }
            }
        }

        if (BestTri < 0)
            return { Hit: false, Travel: MaximumTravel, Steps };

        const TBase = BestTri * 16;
        const NA = UnpackOctahedralCpu(TriIntegers[TBase + 7]);
        const NB = UnpackOctahedralCpu(TriIntegers[TBase + 11]);
        const NC = UnpackOctahedralCpu(TriIntegers[TBase + 15]);
        const W  = 1.0 - BestU - BestV;
        const LocalN = NormalizeVector([
            NA[0] * W + NB[0] * BestU + NC[0] * BestV,
            NA[1] * W + NB[1] * BestU + NC[1] * BestV,
            NA[2] * W + NB[2] * BestU + NC[2] * BestV
        ]);
        const Occ = Scene.Occupants[BestOcc];
        const M   = Occ.Transform;
        let WorldN = NormalizeVector([
            M[0] * LocalN[0] + M[4] * LocalN[1] + M[8]  * LocalN[2],
            M[1] * LocalN[0] + M[5] * LocalN[1] + M[9]  * LocalN[2],
            M[2] * LocalN[0] + M[6] * LocalN[1] + M[10] * LocalN[2]
        ]);
        if (WorldN[0] * Direction[0] + WorldN[1] * Direction[1] + WorldN[2] * Direction[2] > 0.0)
            WorldN = [-WorldN[0], -WorldN[1], -WorldN[2]];

        const RawFamily = TriIntegers[TBase + 3];
        const Family    = Occ.MaterialOverride >= 0 ? Occ.MaterialOverride : RawFamily;
        return {
            Hit: true,
            Travel: Nearest,
            Position: [Origin[0] + Direction[0] * Nearest, Origin[1] + Direction[1] * Nearest, Origin[2] + Direction[2] * Nearest],
            Normal: WorldN,
            Occupant: BestOcc,
            Family,
            Steps
        };
    }

    InvertRigidOrAffine(M)
    {
        const R00 = M[0], R01 = M[4], R02 = M[8];
        const R10 = M[1], R11 = M[5], R12 = M[9];
        const R20 = M[2], R21 = M[6], R22 = M[10];
        const Tx  = M[12], Ty = M[13], Tz = M[14];
        const Inv00 = R00, Inv01 = R10, Inv02 = R20;
        const Inv10 = R01, Inv11 = R11, Inv12 = R21;
        const Inv20 = R02, Inv21 = R12, Inv22 = R22;
        return [
            Inv00, Inv10, Inv20, 0,
            Inv01, Inv11, Inv21, 0,
            Inv02, Inv12, Inv22, 0,
            -(Inv00 * Tx + Inv01 * Ty + Inv02 * Tz),
            -(Inv10 * Tx + Inv11 * Ty + Inv12 * Tz),
            -(Inv20 * Tx + Inv21 * Ty + Inv22 * Tz),
            1
        ];
    }
}

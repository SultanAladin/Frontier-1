//============================================================================================================================================
//                                                           BOUNDINGSTRUCTURE.JS
//============================================================================================================================================
// 🧩 Binned-SAH bounding volume hierarchy over one facet set, flattened for the distance transfer's device traversal.

// 📝 Node layout, 32 bytes: Minimum.xyz, LeftOrFirst (u32), Maximum.xyz, Count (u32). Count > 0 marks a leaf whose
//    facets are [LeftOrFirst, LeftOrFirst + Count); an interior node's children are LeftOrFirst and LeftOrFirst + 1.
const LeafCapacity = 4;    // [-] - facets per leaf before a split is attempted
const BinCount     = 12;   // [-] - SAH bins along the split axis

export function ConstructBoundingStructure(Corners)
{
    const FacetCount = Corners.length / 9;
    const Centroids = new Float32Array(FacetCount * 3);
    const FacetMinimum = new Float32Array(FacetCount * 3);
    const FacetMaximum = new Float32Array(FacetCount * 3);
    for (let Facet = 0; Facet < FacetCount; ++Facet)
    {
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            const A = Corners[Facet * 9 + Axis], B = Corners[Facet * 9 + 3 + Axis], C = Corners[Facet * 9 + 6 + Axis];
            FacetMinimum[Facet * 3 + Axis] = Math.min(A, B, C);
            FacetMaximum[Facet * 3 + Axis] = Math.max(A, B, C);
            Centroids[Facet * 3 + Axis] = (A + B + C) / 3.0;
        }
    }

    const Order = new Uint32Array(FacetCount);
    for (let Facet = 0; Facet < FacetCount; ++Facet)
        Order[Facet] = Facet;

    const Nodes = [];
    const ConstructNode = () =>
    {
        Nodes.push({ Minimum: [0, 0, 0], Maximum: [0, 0, 0], LeftOrFirst: 0, Count: 0 });
        return Nodes.length - 1;
    };

    const FitBounds = (Node, First, Count) =>
    {
        const Minimum = [Infinity, Infinity, Infinity], Maximum = [-Infinity, -Infinity, -Infinity];
        for (let Index = First; Index < First + Count; ++Index)
        {
            const Facet = Order[Index];
            for (let Axis = 0; Axis < 3; ++Axis)
            {
                Minimum[Axis] = Math.min(Minimum[Axis], FacetMinimum[Facet * 3 + Axis]);
                Maximum[Axis] = Math.max(Maximum[Axis], FacetMaximum[Facet * 3 + Axis]);
            }
        }
        Nodes[Node].Minimum = Minimum;
        Nodes[Node].Maximum = Maximum;
    };

    const SurfaceArea = (Minimum, Maximum) =>
    {
        const X = Math.max(0, Maximum[0] - Minimum[0]), Y = Math.max(0, Maximum[1] - Minimum[1]), Z = Math.max(0, Maximum[2] - Minimum[2]);
        return 2.0 * (X * Y + Y * Z + Z * X);
    };

    // 📐 Binned SAH: cost(split) = A_left·N_left + A_right·N_right, compared against the leaf cost A·N.
    const Subdivide = (Node, First, Count) =>
    {
        FitBounds(Node, First, Count);
        if (Count <= LeafCapacity)
        {
            Nodes[Node].LeftOrFirst = First;
            Nodes[Node].Count = Count;
            return;
        }
        let BestAxis = -1, BestSplit = 0, BestCost = Infinity;
        for (let Axis = 0; Axis < 3; ++Axis)
        {
            let Low = Infinity, High = -Infinity;
            for (let Index = First; Index < First + Count; ++Index)
            {
                const Centroid = Centroids[Order[Index] * 3 + Axis];
                Low = Math.min(Low, Centroid);
                High = Math.max(High, Centroid);
            }
            if (High - Low < 1.0e-9)
                continue;
            const Bins = Array.from({ length: BinCount }, () => ({ Count: 0, Minimum: [Infinity, Infinity, Infinity],
                                                                   Maximum: [-Infinity, -Infinity, -Infinity] }));
            const Scale = BinCount / (High - Low);
            for (let Index = First; Index < First + Count; ++Index)
            {
                const Facet = Order[Index];
                const Bin = Math.min(BinCount - 1, Math.floor((Centroids[Facet * 3 + Axis] - Low) * Scale));
                Bins[Bin].Count++;
                for (let Component = 0; Component < 3; ++Component)
                {
                    Bins[Bin].Minimum[Component] = Math.min(Bins[Bin].Minimum[Component], FacetMinimum[Facet * 3 + Component]);
                    Bins[Bin].Maximum[Component] = Math.max(Bins[Bin].Maximum[Component], FacetMaximum[Facet * 3 + Component]);
                }
            }
            for (let Split = 1; Split < BinCount; ++Split)
            {
                let LeftCount = 0, RightCount = 0;
                const LeftMinimum = [Infinity, Infinity, Infinity], LeftMaximum = [-Infinity, -Infinity, -Infinity];
                const RightMinimum = [Infinity, Infinity, Infinity], RightMaximum = [-Infinity, -Infinity, -Infinity];
                for (let Bin = 0; Bin < BinCount; ++Bin)
                {
                    if (Bins[Bin].Count === 0)
                        continue;
                    const Left = Bin < Split;
                    const TargetMinimum = Left ? LeftMinimum : RightMinimum, TargetMaximum = Left ? LeftMaximum : RightMaximum;
                    for (let Component = 0; Component < 3; ++Component)
                    {
                        TargetMinimum[Component] = Math.min(TargetMinimum[Component], Bins[Bin].Minimum[Component]);
                        TargetMaximum[Component] = Math.max(TargetMaximum[Component], Bins[Bin].Maximum[Component]);
                    }
                    if (Left)
                        LeftCount += Bins[Bin].Count;
                    else
                        RightCount += Bins[Bin].Count;
                }
                if (LeftCount === 0 || RightCount === 0)
                    continue;
                const Cost = SurfaceArea(LeftMinimum, LeftMaximum) * LeftCount + SurfaceArea(RightMinimum, RightMaximum) * RightCount;
                if (Cost < BestCost)
                {
                    BestCost = Cost;
                    BestAxis = Axis;
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
                    const Swap = Order[Cursor];
                    Order[Cursor] = Order[Tail];
                    Order[Tail] = Swap;
                    --Tail;
                }
            }
            Middle = Cursor;
        }
        if (Middle === First || Middle === First + Count)
        {
            if (Count <= LeafCapacity * 4)
            {
                Nodes[Node].LeftOrFirst = First;
                Nodes[Node].Count = Count;
                return;
            }
            Middle = First + (Count >> 1);
        }
        const Left = ConstructNode();
        ConstructNode();
        Nodes[Node].LeftOrFirst = Left;
        Nodes[Node].Count = 0;
        Subdivide(Left, First, Middle - First);
        Subdivide(Left + 1, Middle, First + Count - Middle);
    };

    const Root = ConstructNode();
    Subdivide(Root, 0, FacetCount);

    const NodeBytes = new ArrayBuffer(Nodes.length * 32);
    const NodeScalars = new Float32Array(NodeBytes);
    const NodeIntegers = new Uint32Array(NodeBytes);
    Nodes.forEach((Node, Index) =>
    {
        NodeScalars.set(Node.Minimum, Index * 8);
        NodeIntegers[Index * 8 + 3] = Node.LeftOrFirst;
        NodeScalars.set(Node.Maximum, Index * 8 + 4);
        NodeIntegers[Index * 8 + 7] = Node.Count;
    });

    const Ordered = new Float32Array(FacetCount * 12);
    for (let Index = 0; Index < FacetCount; ++Index)
    {
        const Facet = Order[Index];
        for (let Corner = 0; Corner < 3; ++Corner)
        {
            Ordered[Index * 12 + Corner * 4 + 0] = Corners[Facet * 9 + Corner * 3 + 0];
            Ordered[Index * 12 + Corner * 4 + 1] = Corners[Facet * 9 + Corner * 3 + 1];
            Ordered[Index * 12 + Corner * 4 + 2] = Corners[Facet * 9 + Corner * 3 + 2];
        }
    }
    return { NodeScalars, NodeCount: Nodes.length, FacetCorners: Ordered, FacetCount };
}

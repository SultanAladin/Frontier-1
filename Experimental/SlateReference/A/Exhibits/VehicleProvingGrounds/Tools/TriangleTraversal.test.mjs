import test from "node:test";
import assert from "node:assert/strict";

import {
    RayTracingSpace,
    ConstructFacetBlas,
    PackOctahedralCpu,
    UnpackOctahedralCpu
} from "../Source/RayTracingSpace.js";
import { OccupantRole } from "../Source/FacetStructure.js";

// 📝 The triangle hierarchy behind the reflection toggle, exercised without a GPU: a two-triangle quad stands in for
//    the proving grounds, and the CPU reference traversal walks the same buffers the shader would. These check the
//    structure and the math, not the rendered result.

const QuadFacets = [
    { Corners: [[-1.0, -1.0, 0.0], [1.0, -1.0, 0.0], [1.0, 1.0, 0.0]], Family: 0 },
    { Corners: [[-1.0, -1.0, 0.0], [1.0, 1.0, 0.0], [-1.0, 1.0, 0.0]], Family: 0 }
];

function ConstructQuadScene()
{
    return {
        FacetSets: [{ Facets: QuadFacets }],
        Occupants: [{ FacetSet: 0, Roles: OccupantRole.Raster, Inactive: false,
                      Transform: [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], MaterialOverride: -1 }],
        GroundOccupant: -1,
        QueryWorldBounds: () => ({ Minimum: [-1.0, -1.0, 0.0], Maximum: [1.0, 1.0, 0.0] })
    };
}

test("PackOctahedralCpu round-trips through UnpackOctahedralCpu", () =>
{
    const Normals = [[1, 0, 0], [0, 1, 0], [0, 0, 1], [0, 0, -1], [-1, 0, 0],
                     [0.577, 0.577, 0.577], [-0.5, -0.5, -0.7071], [0.3, -0.9, 0.3162]];
    for (const Normal of Normals)
    {
        const Length = Math.hypot(Normal[0], Normal[1], Normal[2]);
        const Unit = [Normal[0] / Length, Normal[1] / Length, Normal[2] / Length];
        const Back = UnpackOctahedralCpu(PackOctahedralCpu(Unit));
        const Dot = Back[0] * Unit[0] + Back[1] * Unit[1] + Back[2] * Unit[2];
        assert.ok(Dot > 0.999, `round-trip drifts for [${Unit}]`);
    }
});

test("ConstructFacetBlas bounds a quad and stores both triangles", () =>
{
    const Blas = ConstructFacetBlas(QuadFacets, 4608, 0);
    assert.ok(Blas.RootNode >= 4608);
    assert.ok(Blas.NodeCount >= 1);
    assert.equal(Blas.TriangleCount, 2);
    const NodeScalars = new Float32Array(Blas.NodeBytes);
    const Root = (Blas.RootNode - 4608) * 8;
    assert.ok(NodeScalars[Root] <= -1.0 && NodeScalars[Root + 1] <= -1.0 && NodeScalars[Root + 2] <= 0.0);
    assert.ok(NodeScalars[Root + 4] >= 1.0 && NodeScalars[Root + 5] >= 1.0 && NodeScalars[Root + 6] >= 0.0);
    const TriScalars = new Float32Array(Blas.TriangleBytes);
    const AX = TriScalars[0], AY = TriScalars[1], AZ = TriScalars[2];
    assert.ok(QuadFacets.some((Facet) => Facet.Corners.some((C) => C[0] === AX && C[1] === AY && C[2] === AZ)));
});

test("TraceRayCpu hits the quad through the TLAS and misses beside it", () =>
{
    const Scene = ConstructQuadScene();
    const Space = new RayTracingSpace(null);
    Space.BuildCpuHierarchy(Scene);
    Space.UpdateTlas(Scene);
    const Hit = Space.TraceRayCpu(Scene, [0.25, -0.5, 5.0], [0.0, 0.0, -1.0]);
    assert.equal(Hit.Hit, true);
    assert.ok(Math.abs(Hit.Travel - 5.0) < 1.0e-4);
    assert.equal(Hit.Occupant, 0);
    assert.ok(Hit.Normal[2] > 0.99);
    const Miss = Space.TraceRayCpu(Scene, [3.0, 0.0, 5.0], [0.0, 0.0, -1.0]);
    assert.equal(Miss.Hit, false);
});

test("UpdateTlas skips inactive occupants and the ground", () =>
{
    const Scene = ConstructQuadScene();
    const Space = new RayTracingSpace(null);
    Space.BuildCpuHierarchy(Scene);
    Space.UpdateTlas(Scene);
    assert.ok(Space.ActiveInstances >= 1);
    Scene.Occupants[0].Inactive = true;
    Space.UpdateTlas(Scene);
    assert.equal(Space.ActiveInstances, 0);
});

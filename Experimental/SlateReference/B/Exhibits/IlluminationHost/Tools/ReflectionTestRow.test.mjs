import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ConstructMaterialSpecification, EncodeMaterialRecords, FresnelF0FromIndex, Refractive, CourseMaterial, MaterialRecordBytes } from '../Source/MaterialSpecification.js';
import { EmitSphereFacets } from '../Source/CourseStructure.js';

test('Fresnel F0 follows the index of refraction and the glass record carries it', () => {
    assert.ok(Math.abs(FresnelF0FromIndex(1.5) - 0.04) < 1.0e-9);
    assert.equal(FresnelF0FromIndex(1.0), 0);
    const Glass = Refractive(1.5, 0.02);
    assert.equal(Glass.IndexOfRefraction, 1.5);
    assert.equal(Glass.Transmission, 1.0);
    assert.deepEqual(Glass.Albedo, [0, 0, 0]);
});

test('material records are 64 bytes with the index of refraction at scalar 12', () => {
    const Materials = ConstructMaterialSpecification();
    const Scalars = EncodeMaterialRecords(Materials);
    assert.equal(Scalars.length, Materials.length * 16);
    assert.equal(MaterialRecordBytes, 64);
    const Offset = CourseMaterial.GlassSphere * 16;
    assert.ok(Math.abs(Scalars[Offset + 12] - 1.5) < 1.0e-6);
    assert.ok(Math.abs(Scalars[Offset + 10] - 0.04) < 1.0e-6);
    assert.equal(Scalars[CourseMaterial.VehicleGlass * 16 + 12], 0, 'existing glazing stays opaque');
});

test('the reflection test row is metal or glass where it should be', () => {
    const Materials = ConstructMaterialSpecification();
    for (const Key of ['MirrorSheet', 'AluminiumSphere', 'GoldSphere'])
        assert.equal(Materials[CourseMaterial[Key]].Metalness, 1.0, Key);
    assert.ok(Materials[CourseMaterial.MirrorSheet].Roughness < 0.05);
    assert.equal(Materials[CourseMaterial.GlassSphere].Metalness, 0.0);
});

test('sphere facets are outward-wound and lie on the sphere, with the pole triangles dropped', () => {
    const Radius = 0.5, Segments = 24, Rings = 12;
    const Facets = EmitSphereFacets(Radius, Segments, Rings, 1);
    assert.equal(Facets.length, 2 * Segments * (Rings - 1));
    const Centre = [0, 0, Radius];
    for (const { Corners } of Facets) {
        for (const Corner of Corners)
            assert.ok(Math.abs(Math.hypot(Corner[0] - Centre[0], Corner[1] - Centre[1], Corner[2] - Centre[2]) - Radius) < 1.0e-9);
        const [A, B, C] = Corners;
        const N = [(B[1]-A[1])*(C[2]-A[2]) - (B[2]-A[2])*(C[1]-A[1]), (B[2]-A[2])*(C[0]-A[0]) - (B[0]-A[0])*(C[2]-A[2]),
                   (B[0]-A[0])*(C[1]-A[1]) - (B[1]-A[1])*(C[0]-A[0])];
        const Away = [(A[0]+B[0]+C[0])/3 - Centre[0], (A[1]+B[1]+C[1])/3 - Centre[1], (A[2]+B[2]+C[2])/3 - Centre[2]];
        assert.ok(N[0]*Away[0] + N[1]*Away[1] + N[2]*Away[2] > 0, 'facet faces outward');
    }
});

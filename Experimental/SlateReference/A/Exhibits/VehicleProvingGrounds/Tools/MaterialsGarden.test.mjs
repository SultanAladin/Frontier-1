import { test } from 'node:test';
import assert from 'node:assert/strict';
import { ConstructMaterialSpecification, CourseMaterial, IorToF0 } from '../Source/MaterialSpecification.js';
import { EmitSphereFacets } from '../Source/CourseStructure.js';
import { LevelShapes, ResolveLevelPlacements } from '../Source/LevelStructure.js';

test('glass authoring converts IOR to normal-incidence reflectance', () => {
    assert.equal(IorToF0(1.0), 0.0);
    assert.ok(Math.abs(IorToF0(1.5) - 0.04) < 1.0e-6);
    // 📝 The shader inverts this exact mapping to recover the IOR for Snell; the round trip must hold.
    for (const Ior of [1.0, 1.33, 1.5, 2.0, 2.5]) {
        const F0 = IorToF0(Ior), Root = Math.sqrt(F0);
        assert.ok(Math.abs((1.0 + Root) / (1.0 - Root) - Ior) < 1.0e-6);
    }
});

test('mirror and metal records are smooth full metals with measured F0', () => {
    const Materials = ConstructMaterialSpecification();
    for (const Family of [CourseMaterial.Mirror, CourseMaterial.Aluminium, CourseMaterial.Gold]) {
        assert.equal(Materials[Family].Metalness, 1.0);
        assert.equal(Materials[Family].Transmission, 0.0);
    }
    assert.deepEqual(Materials[CourseMaterial.Mirror].Albedo, [1.0, 1.0, 1.0]);
    assert.deepEqual(Materials[CourseMaterial.Aluminium].Albedo, [0.91, 0.92, 0.92]);
    assert.deepEqual(Materials[CourseMaterial.Gold].Albedo, [1.0, 0.71, 0.29]);
    // 📝 A roughness ladder across the garden, mirror-smooth to softly brushed, all well under the traced cutoff.
    assert.deepEqual([Materials[CourseMaterial.Mirror].Roughness, Materials[CourseMaterial.Aluminium].Roughness,
                      Materials[CourseMaterial.Gold].Roughness], [0.02, 0.12, 0.08]);
});

test('showcase glass is smooth, clear and transmissive', () => {
    const Glass = ConstructMaterialSpecification()[CourseMaterial.ShowcaseGlass];
    assert.equal(Glass.Transmission, 1.0);
    assert.equal(Glass.Metalness, 0.0);
    assert.equal(Glass.Roughness, 0.04);
    assert.ok(Math.abs(Glass.SpecularLevel - 0.04) < 1.0e-6);
    // 📝 No diffuse at all: the exitance gather would otherwise haze the transmitted leg it cannot see through.
    assert.deepEqual(Glass.Albedo, [0.0, 0.0, 0.0]);
});

test('vehicle glass stays opaque in the record', () => {
    // 📝 Transmission was authored on but never read, so the car rendered opaque; the record now says what the
    //    renderer always did, keeping the new glass path exclusive to the garden.
    assert.equal(ConstructMaterialSpecification()[CourseMaterial.VehicleGlass].Transmission, 0.0);
});

test('sphere facets wind outward with radial smooth normals', () => {
    const Radius = 0.6, Facets = EmitSphereFacets(Radius, 16, 24, CourseMaterial.Gold);
    assert.equal(Facets.length, 2 * 16 * 24);
    const Centre = [0.0, 0.0, Radius];
    for (const Facet of Facets) {
        assert.equal(Facet.Family, CourseMaterial.Gold);
        assert.equal(Facet.Corners.length, 3);
        assert.equal(Facet.Normals.length, 3);
        for (let Index = 0; Index < 3; ++Index) {
            const Corner = Facet.Corners[Index], Normal = Facet.Normals[Index];
            const Radial = [Corner[0] - Centre[0], Corner[1] - Centre[1], Corner[2] - Centre[2]];
            const Outward = Normal[0] * Radial[0] + Normal[1] * Radial[1] + Normal[2] * Radial[2];
            assert.ok(Outward > 0.0);
            assert.ok(Math.abs(Math.hypot(...Normal) - 1.0) < 1.0e-6);
        }
        const [A, B, C] = Facet.Corners;
        const Edge1 = [B[0] - A[0], B[1] - A[1], B[2] - A[2]], Edge2 = [C[0] - A[0], C[1] - A[1], C[2] - A[2]];
        const Face = [Edge1[1] * Edge2[2] - Edge1[2] * Edge2[1], Edge1[2] * Edge2[0] - Edge1[0] * Edge2[2],
                      Edge1[0] * Edge2[1] - Edge1[1] * Edge2[0]];
        const Centroid = [(A[0] + B[0] + C[0]) / 3 - Centre[0], (A[1] + B[1] + C[1]) / 3 - Centre[1],
                          (A[2] + B[2] + C[2]) / 3 - Centre[2]];
        // 📝 Pole-band fans collapse one triangle each; degenerate corners carry no area and intersect nothing.
        if (Math.hypot(...Face) > 1.0e-9)
            assert.ok(Face[0] * Centroid[0] + Face[1] * Centroid[1] + Face[2] * Centroid[2] > 0.0);
    }
});

test('garden shapes and placements sit north of the start straight', () => {
    for (const Shape of ['GardenAluminium', 'GardenGold', 'GardenGlass', 'GardenMirror'])
        assert.ok(LevelShapes[Shape], Shape);
    const Garden = ResolveLevelPlacements().filter((Placement) => Placement.Name.includes('sphere') || Placement.Name === 'Standing mirror');
    assert.deepEqual(Garden.map((Placement) => Placement.Name),
                     ['Aluminium sphere', 'Gold sphere', 'Glass sphere', 'Standing mirror']);
    for (const Placement of Garden) {
        assert.ok(Math.abs(Placement.Position[1] - 11.5) < 1.0e-9);
        assert.ok(Placement.Position[0] >= -35.0 && Placement.Position[0] <= -26.0);
    }
});

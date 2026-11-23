import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { DecodeFacetStream } from '../Source/FacetCodec.js';
import { ProjectWheelFacets } from '../Source/FacetStructure.js';
import { CourseMaterial, ConstructMaterialSpecification } from '../Source/MaterialSpecification.js';
import { ResolveHmiCamera, ResolveTabletFixture, CabinLayout } from '../Source/CabinStructure.js';
import { CabinSequence } from '../Source/CabinSequence.js';
import { VehicleVisualState } from '../Source/VehicleVisualState.js';
import { ConstructIdentityTransform, SubtractVector, NormalizeVector, DotVector } from '../Source/TransformProjection.js';
function Archive(Name) {
    const Buffer = fs.readFileSync(new URL(`../Archive/ControlVehicle${Name}.facet`,import.meta.url));
    return DecodeFacetStream(Buffer.buffer.slice(Buffer.byteOffset,Buffer.byteOffset+Buffer.byteLength));
}
test('shredding keeps the complete metal rim on both sides, not just its small hub', () => {
    const Tyre = Archive('Tyre'), Rim = Archive('Rim');
    assert.ok([...Rim.Families].some(F => F === CourseMaterial.Tyre)); // authored dark material slot, deliberately reused by Drive
    for (const Mirrored of [false,true]) {
        const Faces = ProjectWheelFacets(Tyre,Rim,Mirrored);
        assert.equal(Faces.filter(F => F.Family === CourseMaterial.Tyre).length,Tyre.FacetCount);
        assert.equal(Faces.filter(F => F.Family === CourseMaterial.Hub).length,272);
        assert.equal(Faces.filter(F => F.Family === CourseMaterial.RimBase).length,1296);
        for (let I = 0; I < Rim.FacetCount; ++I) {
            assert.equal(Faces[Tyre.FacetCount+I].Family,
                Rim.Families[I] === CourseMaterial.Tyre ? CourseMaterial.RimBase : CourseMaterial.Hub);
        }
        assert.ok(Faces.slice(Tyre.FacetCount).every(F => F.Family !== CourseMaterial.Tyre));
    }
    const Materials = ConstructMaterialSpecification();
    const Base = Materials[CourseMaterial.RimBase];
    assert.deepEqual(Base.Albedo,[0.03741,0.03741,0.03741]);
    assert.deepEqual(Base.Emission,[0,0,0]);
    assert.notEqual(Base,Materials[CourseMaterial.Tyre]);
    const Alloy = Materials[CourseMaterial.Hub];
    assert.ok(Alloy.Albedo[2] > Alloy.Albedo[0]); assert.ok(Alloy.Emission[2] > 0);
});
test('tablet scale matches touch edges, leaves cluster/driver fixed, and is encoded for every render path', () => {
    const Cabin = Object.create(CabinSequence.prototype); Cabin.Layout = CabinLayout;
    const Cluster = structuredClone(CabinLayout.Cluster);
    for (const Scale of [0.75,1,2,2.5]) {
        Cabin.TabletScale = Scale;
        const T = ResolveTabletFixture(Scale), Focus = ResolveHmiCamera(Scale,0.6);
        const Point = T.Centre.map((V,I) => V + T.Right[I] * T.Width * 0.45);
        const Hit = Cabin.QueryTabletHit(Focus.Eye,NormalizeVector(SubtractVector(Point,Focus.Eye)),ConstructIdentityTransform());
        assert.ok(Math.abs(Hit.Across-0.95) < 1e-6);
        assert.ok(Math.abs(Hit.Down-0.5) < 1e-6);
        const Data = new VehicleVisualState().EncodeRecord({ Tablet: { Centre: T.Centre, Scale } });
        assert.equal(Data[19],Scale);
        assert.deepEqual(CabinLayout.Cluster,Cluster);
    }
});
test('HMI focus faces the screen and fits it in portrait and landscape viewports', () => {
    for (const Aspect of [0.5,1,2]) for (const Scale of [1,2.5]) {
        const Focus = ResolveHmiCamera(Scale,Aspect), T = ResolveTabletFixture(Scale);
        const Distance = DotVector(SubtractVector(Focus.Eye,Focus.Target),T.Normal);
        const HalfHeight = Distance * Math.tan(Focus.Fov*Math.PI/360);
        assert.ok(T.Height/2 < HalfHeight);
        assert.ok(T.Width/2 < HalfHeight*Aspect);
        assert.deepEqual(Focus.Up,T.Up);
    }
});
test('sparks have bounded subpixel/thin footprints instead of unbounded near-camera dots', () => {
    const Shader = fs.readFileSync(new URL('../Shader/SparkRaster.wgsl',import.meta.url),'utf8');
    assert.match(Shader,/let Width = clamp\([^;]+0\.65, 1\.35\)/);
    assert.match(Shader,/HeadPixels - TailPixels/);
});

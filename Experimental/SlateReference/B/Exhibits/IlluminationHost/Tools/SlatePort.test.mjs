import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { CabinLayout, ConstructCabinFacets, ResolveCockpitFrame, PlaceSeatedEye, CameraRigs } from '../Source/CabinStructure.js';
import { CabinLayout as Slate, ConstructCabinTransform } from '../Source/SlateCabinLayout.js';
import { ConstructSlateFittings } from '../Source/SlateCabinFittings.js';
import { TransformPoint, ConstructIdentityTransform } from '../Source/TransformProjection.js';
import { ResolveHudStyle } from '../Source/HudLayout.js';
import { CabinSequence } from '../Source/CabinSequence.js';
import { CourseMaterial } from '../Source/MaterialSpecification.js';
import { PlumeSourceProjection } from '../Source/PlumeSourceProjection.js';
import { PlumeSpace, PlumeDefaults } from '../Source/PlumeSpace.js';
import { MapPlumeSettings, ConstructGasParameters } from '../Source/SmokeSequence.js';
import { IntersectVehicleFacet, PickVehicle } from '../Source/VehiclePicking.js';
const Close = (A,B) => A.forEach((V,I) => assert.ok(Math.abs(V-B[I]) < 1e-6));
test('Slate wheel, integrated hexagon and tablet retain the requested poses and sizes', () => {
    assert.equal(CabinLayout.Steering.Radius, Slate.WheelRadius);
    assert.equal(CabinLayout.Steering.Ratio, 3);
    const Wheel = ConstructCabinTransform(Slate.Wheel, Slate.WheelTilt);
    const Tablet = ConstructCabinTransform(Slate.Tablet, Slate.TabletTilt);
    Close(CabinLayout.Steering.Pivot, TransformPoint(Wheel, [0,0,0]));
    Close(CabinLayout.Cluster.Centre, TransformPoint(Wheel, [0,0,0.023]));
    Close(CabinLayout.Tablet.Centre, TransformPoint(Tablet, [0,0,0.014]));
    assert.equal(CabinLayout.Tablet.Width, 0.30);
    assert.equal(CabinLayout.Tablet.Height, 0.1875);
    const Fit = ConstructSlateFittings();
    assert.ok(Fit.filter(V => V.Material === 1).length > 3000);
    assert.equal(Fit.filter(V => V.Material === 2).length, 18);
    assert.ok(ConstructCabinFacets().some(F => F.Family === CourseMaterial.SteeringWheel));
    const Overlay = fs.readFileSync(new URL('../Shader/CabinOverlay.wgsl', import.meta.url),'utf8');
    assert.match(Overlay, /Kind == KindCluster[\s\S]*TurnSteeringWheel/);
});
test('HUD size/offset changes cannot move the physical cockpit or seated camera', () => {
    const Normal = ResolveCockpitFrame(), Changed = ResolveCockpitFrame({ HudScale: 1.8, HudAcross: 200,
        CockpitScale: 0.1, CockpitHeight: 10 });
    assert.deepEqual(Changed, Normal);
    assert.deepEqual(PlaceSeatedEye(CameraRigs.Cockpit.Eye, CameraRigs.Cockpit.Hip, Changed), CameraRigs.Cockpit.Eye);
    assert.deepEqual(ResolveHudStyle({ HudScale: 1.5, HudAcross: 25, HudDown: -10 }),
        { transformOrigin: 'top left', transform: 'translate(25px, -10px) scale(1.5)' });
});
test('touch ray uses the new Slate tablet plane and rejects off-tablet rays', () => {
    const Cabin = Object.create(CabinSequence.prototype);
    Cabin.Layout = CabinLayout; Cabin.Cockpit = ResolveCockpitFrame();
    const T = CabinLayout.Tablet;
    const Eye = T.Centre.map((Value,I) => Value + T.Normal[I]);
    const Hit = Cabin.QueryTabletHit(Eye, T.Normal.map(Value => -Value), ConstructIdentityTransform());
    assert.ok(Hit); Close([Hit.Across,Hit.Down], [0.5,0.5]);
    assert.equal(Cabin.QueryTabletHit(Eye.map((Value,I) => Value + T.Right[I]), T.Normal.map(Value => -Value), ConstructIdentityTransform()), null);
});
function Telemetry() {
    return { Position: [0,0,0.5], Velocity: [0,0,0], Exhausts: [], AfterfireSeconds: 0,
        Wheels: Array.from({length: 4}, (_,I) => ({ Thermal: { Ignited: false, CombustionPower: 0, SurfaceTemperature: 520 },
            SlidingPower: I >= 2 ? 30000 : 0, SlipSpeed: I >= 2 ? 15 : 0, InContact: true,
            ContactPoint: [-1,I % 2 ? -1 : 1,0], HubPosition: [-1,I % 2 ? -1 : 1,0.5],
            ContactVelocity: [0,0,0], Longitudinal: [1,0,0], SurfaceSpeed: I >= 2 ? 20 : 0, Radius: 0.5 })) };
}
test('reference tyre source emits patch and arch smoke with rearward momentum, then releases', () => {
    const Sources = new PlumeSourceProjection(), T = Telemetry();
    for (let I=0; I<12; ++I) Sources.Project(T, 1/60, {});
    assert.equal(Sources.ActiveTyres, 2); assert.equal(Sources.Count, 4);
    assert.ok(Sources.Records[4] < 0);
    assert.equal(Sources.Records[10], 0); // friction smoke never emits flame
    assert.ok(Sources.ResolveBounds().Minimum.every(Number.isFinite));
    for (const Wheel of T.Wheels) { Wheel.SlidingPower = 0; Wheel.SlipSpeed = 0; }
    for (let I=0; I<30; ++I) Sources.Project(T, 1/60, {});
    assert.equal(Sources.Count, 0);
    const Mapped = MapPlumeSettings(ConstructGasParameters());
    assert.equal(Mapped.PlumeDensity, PlumeDefaults.PlumeDensity);
    assert.equal(Mapped.PlumeVorticity, PlumeDefaults.PlumeVorticity);
});
test('reference plume fades and retires with no source; disabled clears residency', () => {
    const Plume = Object.create(PlumeSpace.prototype);
    Object.assign(Plume, { Resident: true, QuietSeconds: 11.99, Accumulator: 0 });
    Plume.Schedule(0.1,0.1,[0,0,0],{ Count: 0 },{ PlumeEnabled: true },[0,0,1]);
    assert.equal(Plume.Resident, false);
    Plume.Resident = true; Plume.QuietSeconds = 0;
    Plume.Schedule(0.1,0.1,[0,0,0],{ Count: 2 },{ PlumeEnabled: false },[0,0,1]);
    assert.equal(Plume.Resident, false);
});
test('Garage ray picking rejects background and resolves the actual nearest material', () => {
    const Face = { Family: CourseMaterial.BodyCoat, Corners: [[-1,-1,0.5],[1,-1,0.5],[0,1,0.5]] };
    assert.ok(IntersectVehicleFacet([0,0,0],[0,0,1],Face.Corners) < Infinity);
    const Scene = { Occupants: Array.from({length: 5}, () => ({ Transform: ConstructIdentityTransform(), FacetSet: 0 })),
        FacetSets: [{ Facets: [Face] }] };
    const Hit = PickVehicle(Scene, ConstructIdentityTransform(),0.5,0.5);
    assert.equal(Hit.Family, CourseMaterial.BodyCoat);
    assert.equal(PickVehicle(Scene,ConstructIdentityTransform(),0.99,0.01),null);
});

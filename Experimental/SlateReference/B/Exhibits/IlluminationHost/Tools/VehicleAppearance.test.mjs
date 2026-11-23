import { test } from 'node:test';
import assert from 'node:assert/strict';
import { VehicleAppearance, LinearColour } from '../Source/VehicleAppearance.js';
import { ConstructMaterialSpecification, CourseMaterial } from '../Source/MaterialSpecification.js';
import { ScreenSequence } from '../Source/Cabin/ScreenSequence.js';
test('paint and rim records update the shared world material buffer only', () => {
    const Materials = ConstructMaterialSpecification(), Writes = [];
    const Glass = structuredClone(Materials[CourseMaterial.VehicleGlass]);
    const Appearance = new VehicleAppearance(Materials, (Offset, Data) => Writes.push([Offset, Data]));
    Appearance.Apply({ Colour: '#a4d8be', Matte: true, DarkWheels: true });
    assert.deepEqual(Materials[CourseMaterial.BodyCoat].Albedo, LinearColour('#a4d8be'));
    assert.equal(Materials[CourseMaterial.BodyCoat].Roughness, 0.7);
    assert.deepEqual(Writes.map(([Offset]) => Offset), [6 * 64, 10 * 64]);
    assert.ok(Writes.every(([, Data]) => Data.byteLength === 64));
    assert.deepEqual(Materials[CourseMaterial.VehicleGlass], Glass);
    Appearance.Apply({ Matte: false });
    assert.equal(Materials[CourseMaterial.BodyCoat].Roughness, 0.0775);
    assert.throws(() => Appearance.Apply({ Colour: 'bad' }));
});
test('Garage controls apply materials and cycle all inspection modes', () => {
    const Screen = Object.create(ScreenSequence.prototype);
    Object.assign(Screen, { Regions: [], Preview: { External: true, Facets: 2924 },
        Appearance: new VehicleAppearance(ConstructMaterialSpecification(), () => {}) });
    for (const Name of ['RasterRectangle', 'RasterText', 'RasterRule']) Screen[Name] = () => {};
    Screen.RasterApplicationContent('garage');
    const Click = Id => Screen.Regions.find(Region => Region.Identifier === Id).Action();
    Click('colour:2');
    assert.equal(Screen.Appearance.Colour, '#9eb8df');
    Click('finish'); assert.equal(Screen.Appearance.Matte, true);
    Click('wheels'); assert.equal(Screen.Appearance.DarkWheels, true);
    for (const Mode of [1, 2, 3, 0]) { Click('inspection'); assert.equal(Screen.Appearance.View, Mode); }
});

test('material drags only apply on the matching picked car surface and cancel safely', () => {
    const Screen = Object.create(ScreenSequence.prototype);
    Object.assign(Screen, { Regions: [], Preview: { External: true, Facets: 2924,
        Pick: () => ({ Occupant: 0, Family: CourseMaterial.BodyCoat }) },
        Appearance: new VehicleAppearance(ConstructMaterialSpecification(), () => {}) });
    for (const Name of ['RasterRectangle', 'RasterText', 'RasterRule']) Screen[Name] = () => {};
    Screen.RasterApplicationContent('garage');
    Screen.ForwardPointer('down', 790, 480);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('up', 400, 450);
    assert.equal(Screen.Appearance.Colour, '#a4d8be');
    Screen.ForwardPointer('down', 920, 480);
    Screen.ForwardPointer('move', 20, 20);
    Screen.ForwardPointer('up', 20, 20);
    assert.equal(Screen.Appearance.Colour, '#a4d8be');
    Screen.Preview.Pick = () => ({ Occupant: 1, Family: CourseMaterial.Hub });
    Screen.ForwardPointer('down', 920, 620);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('up', 400, 450);
    assert.equal(Screen.Appearance.DarkWheels, true);
    Screen.ForwardPointer('down', 790, 480);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('cancel', 400, 450);
    assert.equal(Screen.DragMaterial, null);
});

test('blue emissive rim and matte finishes can be dragged onto their matching surfaces', () => {
    const Screen = Object.create(ScreenSequence.prototype);
    const Materials = ConstructMaterialSpecification();
    Object.assign(Screen, { Regions: [], Preview: { External: true, Facets: 2924, Spin: true,
        Pick: () => ({ Occupant: 1, Family: CourseMaterial.Hub }) },
        Appearance: new VehicleAppearance(Materials, () => {}) });
    for (const Name of ['RasterRectangle', 'RasterText', 'RasterRule']) Screen[Name] = () => {};
    Screen.Appearance.Apply({ DarkWheels: true });
    Screen.RasterApplicationContent('garage');
    Screen.ForwardPointer('down', 1050, 620);
    Screen.ForwardPointer('move', 400, 450);
    assert.equal(Screen.Preview.Spin,false);
    Screen.ForwardPointer('up', 400, 450);
    assert.equal(Screen.Preview.Spin,true);
    assert.equal(Screen.Appearance.BlueRims,true);
    assert.equal(Screen.Appearance.DarkWheels,false);
    assert.ok(Materials[CourseMaterial.Hub].Emission[2] > 0);
    Screen.Preview.Pick = () => ({ Occupant: 0, Family: CourseMaterial.BodyCoat });
    Screen.ForwardPointer('down', 790, 620);
    Screen.ForwardPointer('move', 400, 450);
    Screen.ForwardPointer('up', 400, 450);
    assert.equal(Screen.Appearance.Matte,true);
});

test('rim finish changes affect selected accent faces, never the black rim base or tyre', () => {
    const Materials = ConstructMaterialSpecification();
    const Base = structuredClone(Materials[CourseMaterial.RimBase]);
    const Tyre = structuredClone(Materials[CourseMaterial.Tyre]);
    const Appearance = new VehicleAppearance(Materials, () => {});
    for (const Finish of [{DarkWheels:true}, {DarkWheels:false,BlueRims:false}, {DarkWheels:false,BlueRims:true}]) {
        Appearance.Apply(Finish);
        assert.deepEqual(Materials[CourseMaterial.RimBase],Base);
        assert.deepEqual(Materials[CourseMaterial.Tyre],Tyre);
    }
    assert.ok(Materials[CourseMaterial.Hub].Emission[2] > 0);
});

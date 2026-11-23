import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { DrivingCameraMotion } from '../Source/DrivingCameraMotion.js';
import { CabinOverlayBindings } from '../Source/CabinSequence.js';
const View = { Eye: [-7,0,3], Target: [0,0,1], Up: [0,0,1], Fov: 60 };
test('speed FOV smooths to its cap in forward and reverse, with finite bounded shake', () => {
    for (const Sign of [-1,1]) {
        const Camera = new DrivingCameraMotion();
        const First = Camera.Apply(View,1/60,[Sign*60,0,0],{Active:true});
        assert.ok(First.Fov > 60 && First.Fov < 61);
        let Last;
        for (let I=0; I<300; ++I) Last=Camera.Apply(View,1/60,[Sign*60,0,0],{Active:true});
        assert.ok(Math.abs(Last.Fov-74)<0.001);
        assert.ok(Last.Eye.every((V,I) => Number.isFinite(V) && Math.abs(V-View.Eye[I]) < 0.04));
        const Impact=Camera.Apply(View,1/60,[0,0,15],{Active:true});
        assert.ok(Impact.Eye.every((V,I) => Math.abs(V-View.Eye[I]) < 0.04));
    }
});
test('HMI/inspection/free camera and disabled effects retain exact camera geometry', () => {
    const Camera = new DrivingCameraMotion();
    Camera.Apply(View,1/60,[60,0,0],{Active:true});
    assert.deepEqual(Camera.Apply(View,1/60,[60,0,0],{Active:false}),View);
    assert.deepEqual(Camera.Apply(View,1/60,[60,0,0],{Active:true,Shake:false,SpeedFov:false}),View);
    Camera.Reset();
    assert.deepEqual(Camera.Apply(View,1/60,[0,0,0],{Active:true}),View);
});
test('overlay schema includes exposure binding 12 and resources use the same schema', () => {
    assert.equal(CabinOverlayBindings.length,13);
    assert.equal(CabinOverlayBindings[12],'StorageRead');
    const Shader=fs.readFileSync(new URL('../Shader/CabinOverlay.wgsl',import.meta.url),'utf8');
    assert.match(Shader,/@group\(0\) @binding\(12\) var<storage, read> DisplayAdaptation/);
    const Source=fs.readFileSync(new URL('../Source/CabinSequence.js',import.meta.url),'utf8');
    assert.match(Source,/ConstructDescriptorLayout\(CabinOverlayBindings/);
    assert.match(Source,/Atmosphere.SkySampler, AdaptationRecords\]/);
});

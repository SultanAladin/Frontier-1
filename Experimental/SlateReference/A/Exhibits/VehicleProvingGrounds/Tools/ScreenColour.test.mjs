import { test } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { ResolveScreenBrightness } from '../Source/CabinSequence.js';
test('either brightness control at zero turns the display black; no baked canvas dimming', () => {
    assert.equal(ResolveScreenBrightness(0,100),0);
    assert.equal(ResolveScreenBrightness(1,0),0);
    assert.equal(ResolveScreenBrightness(0.5,50),0.25);
    assert.equal(ResolveScreenBrightness(1,100,false),0);
    const Source = fs.readFileSync(new URL('../Source/Cabin/ScreenSequence.js',import.meta.url),'utf8');
    assert.match(Source,/!this.ExternalBrightness && this.Values.brightness/);
});
test('exposure-compensated screen colours survive the final ACES fit without washing out', () => {
    for (const Exposure of [0.1,0.5,1,4,20]) for (const Y of [0,0.001,0.02,0.15,0.5,0.9,0.99]) {
        const A = 2.51-2.43*Y, B=0.03-0.59*Y;
        const Radiance = (-B+Math.sqrt(B*B+0.56*A*Y))/(2*A)/Exposure;
        const X = Radiance*Exposure;
        const Display = X*(2.51*X+0.03)/(X*(2.43*X+0.59)+0.14);
        assert.ok(Math.abs(Display-Y)<1e-8);
    }
    const Shader = fs.readFileSync(new URL('../Shader/CabinOverlay.wgsl',import.meta.url),'utf8');
    assert.match(Shader,/ScreenRadiance\(Picture \* Overlay.Tablet.y\)/);
    assert.match(Shader,/DisplayAdaptation\[0\]/);
});

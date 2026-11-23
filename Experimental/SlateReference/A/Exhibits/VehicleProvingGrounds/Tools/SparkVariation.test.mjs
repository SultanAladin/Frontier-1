import {test} from 'node:test';
import assert from 'node:assert/strict';
import {SparkSequence,ResolveSparkGain} from '../Source/SparkSequence.js';
globalThis.GPUBufferUsage={VERTEX:1,COPY_DST:2};
const Make=()=>new SparkSequence({ConstructRecords:()=>({}),Device:{queue:{writeBuffer(){}}}},null);
const Contact={Point:[-100,-30,2],Velocity:[20,0,0],Normal:[0,0,1],Strength:1};
test('seeded mixture produces mostly fine filings, some streaks and rare chips with distinct properties',()=>{
    const A=Make(),B=Make();
    for(let I=0;I<70;I++){A.Emit([Contact],1/60);B.Emit([Contact],1/60);}
    assert.deepEqual(A.Particles,B.Particles);
    const Count=[0,0,0];for(const P of A.Particles)Count[P.Kind]++;
    assert.ok(Count[0]>Count[1]&&Count[1]>Count[2]&&Count[2]>0);
    for(const Key of ['Size','Kelvin','Drag','Cooling','LifeSeconds','BounceScale','Energy','PixelWidth','Shutter'])
        assert.ok(new Set(A.Particles.map(P=>P[Key])).size>20,Key);
    assert.ok(A.Particles.every(P=>P.PixelWidth<=1&&P.PixelWidth>=0.35&&P.Shutter<=1&&P.Shutter>=0.15));
    assert.ok(A.Particles.length<=2048);
});
test('contact pulses vary density, stay bounded, expire and reset safely',()=>{
    const S=Make(),Counts=[];
    for(let I=0;I<60;I++){
        const N=S.Metrics.Emitted;S.Emit([Contact],1/60);Counts.push(S.Metrics.Emitted-N);
    }
    assert.ok(Math.max(...Counts)-Math.min(...Counts)>8);
    assert.ok(Counts.every(N=>N<=96));
    for(let I=0;I<40;I++)S.Emit([],1/60);
    assert.equal(S.EmissionPulses.size,0);
    S.Clear();assert.equal(S.Clock,0);assert.equal(S.Live,0);
});
test('variation slider neutralizes added class properties; varied particles still cool and retire',()=>{
    const S=Make();S.Settings.Variation=0;S.Emit([Contact],0.1);
    for(const P of S.Particles){assert.equal(P.PixelWidth,1);assert.equal(P.Shutter,1);assert.equal(P.Size,1);}
    S.Clear();S.Settings.Variation=1;S.Emit([Contact],0.1);
    const Prior=new Map(S.Particles.map(P=>[P,P.Kelvin]));S.Advance(1/60);
    assert.ok(S.Particles.every(P=>P.Kelvin<=Prior.get(P)));
    for(let I=0;I<350;I++)S.Advance(1/120);
    assert.equal(S.Live,0);
});
test('spark intensity modulation is stable at fixed age and fades before lifetime cutoff',()=>{
    const P={Energy:0.8,Flicker:0.1,Frequency:15,Phase:2,Age:0.2,LifeSeconds:1};
    const Gain=ResolveSparkGain(P);assert.equal(ResolveSparkGain(P),Gain);assert.ok(Gain>=0.72&&Gain<=0.8);
    assert.ok(ResolveSparkGain({...P,Age:0.99})<0.1);
    assert.equal(ResolveSparkGain({...P,Age:1}),0);
});

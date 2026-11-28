import { BlackbodyKelvinToLinearTint, ConvertYUpPositionToWorldZUp, SrgbHexToLinearRgb } from "./CoordinateConversion.js";

export const SurfaceFinish = Object.freeze({
    Floor:0, Concrete:1, DarkMetal:2, BlackPaint:3, SilverMetal:4, Aluminum:5, WhiteFabric:6,
    BlackFabric:7, WarmWood:8, Glass:9, Rubber:10, SafetyYellow:11, ForestPanel:12, GreenPaint:13,
    RedPaint:14, BluePaint:15, PelicanCase:16, Tire:17, Chrome:18, WhitePaint:19, RedLeather:20,
    Cable:21, Grass:22, Asphalt:23, TrackPaint:24, StadiumSkyDome:25, EmissiveDiffuser:26, ForestBackdrop:27,
    TetherScreen:28, VehicleHeadlightOptic:29, VehicleTaillightOptic:30, BrakeMetal:31,
    TimberBench:32, LEDStrip:33
});

const FinishRgb = [0x24272c,0x56585a,0x171b22,0x090b10,0x9299a1,0x6f747a,0xe7e1d6,0x18191d,0x6f4b2c,
    0x303a48,0x282a2d,0xf0c330,0x24362b,0x607c61,0xa8242d,0x29476c,0x262a30,0x17191d,0xc4cbd2,
    0xe9ecef,0x832d30,0x292b2e,0x465d3b,0x25272a,0xf4f2e9,0x101821,0xffffff,0xffffff,0xffffff,0xdcecff,
    0xff4631,0x74787d,0x806344,0xffffff];
const FinishMetal = [0.15,0.18,0.68,0.62,0.82,0.84,0.05,0.08,0.08,0.12,0.1,0.55,0.04,0.04,0.12,0.22,0.16,0.06,0.95,0.08,0.12,0.25,0.03,0.16,0.08,0.02,0.04,0.02,0.02,0.08,0.12,0.72,0.03,0.12];
const FinishRough = [0.78,0.82,0.42,0.22,0.3,0.28,0.82,0.8,0.62,0.2,0.9,0.38,0.58,0.5,0.32,0.28,0.7,0.88,0.18,0.18,0.45,0.84,0.9,0.75,0.8,1,0.35,0.35,0.25,0.18,0.22,0.32,0.58,0.25];

const materialList=[];for(let code=0;code<FinishRgb.length;code++){const hex=FinishRgb[code];
    const rgb = SrgbHexToLinearRgb(hex);
    let radiance = [0, 0, 0];
    let tint = [1, 1, 1];
    let glow = 0;
    if (code === SurfaceFinish.StadiumSkyDome) { tint = BlackbodyKelvinToLinearTint(6200); glow = 0.90; }
    if (code === SurfaceFinish.EmissiveDiffuser) { tint = BlackbodyKelvinToLinearTint(5600); glow = 3.4; }
    if (code === SurfaceFinish.ForestBackdrop) { tint = BlackbodyKelvinToLinearTint(5300); glow = 4.5; }
    if (code === SurfaceFinish.TetherScreen) { tint = BlackbodyKelvinToLinearTint(6500); glow = 0.95; }
    if (code === SurfaceFinish.VehicleHeadlightOptic) { tint = BlackbodyKelvinToLinearTint(5900); glow = 0.18; }
    if (code === SurfaceFinish.VehicleTaillightOptic) { tint = BlackbodyKelvinToLinearTint(2200); glow = 0.42; }
    if (code === SurfaceFinish.LEDStrip) { tint = BlackbodyKelvinToLinearTint(5850); glow = 3.4; }
    radiance = [tint[0]*glow,tint[1]*glow,tint[2]*glow];
    materialList.push(Object.freeze({ code, name: Object.keys(SurfaceFinish).find(key => SurfaceFinish[key] === code), albedo: rgb, metallic: FinishMetal[code], roughness: FinishRough[code], radiance, twoSided: code >= 25 }));
}
export const SurfaceMaterials=Object.freeze(materialList);

function surface() { return { positions: [], normals: [], materials: [], emissive: [], parts: [] }; }
function tri(out, a, b, c, mat, part = 0) {
    const ux=b[0]-a[0], uy=b[1]-a[1], uz=b[2]-a[2], vx=c[0]-a[0], vy=c[1]-a[1], vz=c[2]-a[2];
    let n=[uy*vz-uz*vy,uz*vx-ux*vz,ux*vy-uy*vx]; const d=Math.hypot(...n)||1; n=[n[0]/d,n[1]/d,n[2]/d];
    for (const p of [a,b,c]) { out.positions.push(...p); out.normals.push(...n); out.materials.push(mat); out.emissive.push(mat); out.parts.push(part); }
}
function quad(out, a, b, c, d, mat, part=0) { tri(out,a,b,c,mat,part); tri(out,a,c,d,mat,part); }
function box(out, x,y,z, sx,sy,sz, mat, part=0) {
    const a=[x-sx/2,y-sy/2,z-sz/2], b=[x+sx/2,y-sy/2,z-sz/2], c=[x+sx/2,y+sy/2,z-sz/2], d=[x-sx/2,y+sy/2,z-sz/2];
    const e=[x-sx/2,y-sy/2,z+sz/2], f=[x+sx/2,y-sy/2,z+sz/2], g=[x+sx/2,y+sy/2,z+sz/2], h=[x-sx/2,y+sy/2,z+sz/2];
    quad(out,a,d,c,b,mat,part); quad(out,e,f,g,h,mat,part); quad(out,a,b,f,e,mat,part); quad(out,d,h,g,c,mat,part); quad(out,a,e,h,d,mat,part); quad(out,b,c,g,f,mat,part);
}
function cylinder(out, x,y,z, radius,height, mat, steps=12, part=0) {
    for(let i=0;i<steps;i++){const a=i*Math.PI*2/steps,b=(i+1)*Math.PI*2/steps;const p=[x+Math.cos(a)*radius,y+Math.sin(a)*radius,z-height/2],q=[x+Math.cos(b)*radius,y+Math.sin(b)*radius,z-height/2],r=[q[0],q[1],z+height/2],s=[p[0],p[1],z+height/2];quad(out,p,q,r,s,mat,part);tri(out,[x,y,z-height/2],q,p,mat,part);tri(out,[x,y,z+height/2],s,r,mat,part);}
}
function tubeSegment(out,a,b,radius,mat,part=0,steps=6){const axis=[b[0]-a[0],b[1]-a[1],b[2]-a[2]],length=Math.hypot(...axis)||1,d=[axis[0]/length,axis[1]/length,axis[2]/length],ref=Math.abs(d[2])>.92?[1,0,0]:[0,0,1],u=[d[1]*ref[2]-d[2]*ref[1],d[2]*ref[0]-d[0]*ref[2],d[0]*ref[1]-d[1]*ref[0]],ul=Math.hypot(...u)||1;for(let i=0;i<3;i++)u[i]/=ul;const v=[d[1]*u[2]-d[2]*u[1],d[2]*u[0]-d[0]*u[2],d[0]*u[1]-d[1]*u[0]];for(let i=0;i<steps;i++){const t0=i*Math.PI*2/steps,t1=(i+1)*Math.PI*2/steps,c0=[radius*(u[0]*Math.cos(t0)+v[0]*Math.sin(t0)),radius*(u[1]*Math.cos(t0)+v[1]*Math.sin(t0)),radius*(u[2]*Math.cos(t0)+v[2]*Math.sin(t0))],c1=[radius*(u[0]*Math.cos(t1)+v[0]*Math.sin(t1)),radius*(u[1]*Math.cos(t1)+v[1]*Math.sin(t1)),radius*(u[2]*Math.cos(t1)+v[2]*Math.sin(t1))],p=[a[0]+c0[0],a[1]+c0[1],a[2]+c0[2]],q=[a[0]+c1[0],a[1]+c1[1],a[2]+c1[2]],r=[b[0]+c1[0],b[1]+c1[1],b[2]+c1[2]],s=[b[0]+c0[0],b[1]+c0[1],b[2]+c0[2]];quad(out,p,q,r,s,mat,part);}}

function moveSurface(src, position=[0,0,0], yaw=0, scale=1) {
    const dst=surface(), c=Math.cos(yaw),s=Math.sin(yaw);
    for(let i=0;i<src.positions.length;i+=3){const x=src.positions[i]*scale,y=src.positions[i+1]*scale,z=src.positions[i+2]*scale;dst.positions.push(x*c-y*s+position[0],x*s+y*c+position[1],z+position[2]);dst.normals.push(src.normals[i]*c-src.normals[i+1]*s,src.normals[i]*s+src.normals[i+1]*c,src.normals[i+2]);}
    dst.materials=src.materials?Array.from(src.materials):Array.from({length:src.positions.length/3},(_,i)=>src.slots?.[Math.floor(i/3)]??0);dst.emissive=src.emissive?Array.from(src.emissive):Array.from({length:dst.materials.length},()=>0);dst.parts=src.parts?Array.from(src.parts):Array.from({length:dst.materials.length},(_,i)=>src.slots?.[Math.floor(i/3)]??0);return dst;
}
function floor(width,depth,mat=SurfaceFinish.Floor) { const o=surface(); quad(o,[-width/2,-depth/2,0],[width/2,-depth/2,0],[width/2,depth/2,0],[-width/2,depth/2,0],mat); return o; }
function join(...parts){const o=surface();for(const p of parts){o.positions.push(...p.positions);o.normals.push(...p.normals);o.materials.push(...p.materials);o.emissive.push(...p.emissive);o.parts.push(...p.parts);}return o;}

export function BuildCycloramaSurface(spec={}) {
    const w=spec.width??34,d=spec.depth??26,h=spec.height??7;const o=surface();
    box(o,0,0,-0.08,w,d,0.16,SurfaceFinish.Floor);
    const segments=24;for(let i=0;i<segments;i++){const x0=-w/2+w*i/segments,x1=-w/2+w*(i+1)/segments;const z0=0.04,z1=h*(i+1)/segments;quad(o,[x0,d/2,z0],[x1,d/2,z0],[x1,d/2,z1],[x0,d/2,z1],SurfaceFinish.Concrete);}
    for(let x=-w/2;x<=w/2;x+=4)box(o,x,d/2,h/2,0.08,0.08,h,SurfaceFinish.DarkMetal);
    return o;
}
export function BuildTiledFloorSurface(spec={}) {const w=spec.width??40,d=spec.depth??32,o=floor(w,d,spec.material??SurfaceFinish.Concrete);for(let x=-w/2;x<=w/2;x+=2)box(o,x,0,0.006,0.012,d,0.012,SurfaceFinish.SilverMetal);for(let y=-d/2;y<=d/2;y+=2)box(o,0,y,0.007,w,0.012,0.012,SurfaceFinish.SilverMetal);return o;}
export function BuildCeilingTrussSurface(spec={}) {const w=spec.width??24,d=spec.depth??20,z=spec.height??7.2,o=surface();for(let x=-w/2;x<=w/2;x+=2){box(o,x,0,z,0.12,d,0.12,SurfaceFinish.DarkMetal);for(let y=-d/2;y<d/2;y+=1)box(o,x,y,z-0.13,0.04,1,0.04,SurfaceFinish.SilverMetal);}for(let y=-d/2;y<=d/2;y+=3)box(o,0,y,z,w,0.12,0.12,SurfaceFinish.DarkMetal);return o;}
export function BuildTripodSurface(spec={}) {const o=surface(),h=spec.height??1.6;for(let k=0;k<3;k++){const a=k*2.094;const x=Math.cos(a)*.62,y=Math.sin(a)*.62;tubeSegment(o,[x,y,.04],[0,0,h*.76],.0045,SurfaceFinish.DarkMetal,k,8);cylinder(o,x,y,.035,.035,.07,SurfaceFinish.Rubber,8,k);}cylinder(o,0,0,h*.76,.12,.16,SurfaceFinish.SilverMetal,12,3);box(o,0,0,h*.84,.38,.23,.16,SurfaceFinish.DarkMetal,4);return o;}
export function BuildCinemaCameraSurface(spec={}) {const o=surface();box(o,0,0,0,.54,.32,.34,SurfaceFinish.DarkMetal);cylinder(o,.32,0,0,.18,.26,SurfaceFinish.SilverMetal,16);cylinder(o,.48,0,0,.115,.08,SurfaceFinish.Glass,16);box(o,-.31,0,.03,.1,.22,.23,SurfaceFinish.BlackPaint);box(o,0,-.2,-.08,.27,.02,.15,SurfaceFinish.TetherScreen);return o;}
export function BuildLightFixtureSurface(spec={}) {const o=surface();const w=spec.width??3,h=spec.height??1.5,z=spec.depth??.2;box(o,0,0,0,w,h,z,SurfaceFinish.DarkMetal);box(o,0,0,-z/2-.008,w*.91,h*.91,.018,SurfaceFinish.EmissiveDiffuser);box(o,0,h*.52,z*.2,w,.04,.12,SurfaceFinish.SilverMetal,1);box(o,0,-h*.52,z*.2,w,.04,.12,SurfaceFinish.SilverMetal,2);box(o,-w*.52,0,z*.2,.04,h,.12,SurfaceFinish.SilverMetal,3);box(o,w*.52,0,z*.2,.04,h,.12,SurfaceFinish.SilverMetal,4);return o;}
export function BuildWorkstationSurface(spec={}) {const o=surface(),w=spec.width??1.48,d=spec.depth??.72;box(o,0,0,.76,w,d,.07,SurfaceFinish.WarmWood);for(const x of [-w*.4,w*.4])for(const y of [-d*.36,d*.36])box(o,x,y,.37,.035,.035,.74,SurfaceFinish.DarkMetal);box(o,0,0,.94,.46,.3,.035,SurfaceFinish.BlackPaint);box(o,0,-.02,1.03,.34,.02,.17,SurfaceFinish.TetherScreen);for(let i=0;i<2;i++)box(o,-w*.38+i*.25,d*.08,.27,.26,.46,.48,SurfaceFinish.PelicanCase);return o;}
export function BuildForestBackdropSurface(spec={}) {const o=surface(),w=spec.width??12.4,h=spec.height??6.2;box(o,0,0,h/2,w,.24,h,SurfaceFinish.DarkMetal);box(o,0,-.14,h/2,w-.12,.018,h-.12,SurfaceFinish.ForestBackdrop);for(let s of [-1,1]){box(o,s*w*.57,.48,h*.48,.55,.06,h*.94,SurfaceFinish.BlackFabric);box(o,s*w*.6,.55,h*.5,.05,.65,h,SurfaceFinish.DarkMetal);}return o;}
export function BuildWindFanSurface(spec={}) {const o=surface(),center=[0,-.18,.9];box(o,0,0,.42,.92,.7,.34,SurfaceFinish.DarkMetal);for(const x of [-.32,.32])cylinder(o,x,0,.08,.11,.08,SurfaceFinish.Rubber,10);tubeSegment(o,[0,-.04,.9],[0,-.22,.9],.075,SurfaceFinish.SilverMetal,9,12);for(let k=0;k<7;k++){const angle=k*Math.PI*2/7,a0=angle-.12,a1=angle+.12,inner=.075,outer=.43;quad(o,[Math.cos(a0)*inner,center[1],center[2]+Math.sin(a0)*inner],[Math.cos(a0)*outer,center[1],center[2]+Math.sin(a0)*outer],[Math.cos(a1)*outer,center[1],center[2]+Math.sin(a1)*outer],[Math.cos(a1)*inner,center[1],center[2]+Math.sin(a1)*inner],SurfaceFinish.SilverMetal,k);}for(let i=0;i<32;i++){const a0=i*Math.PI*2/32,a1=(i+1)*Math.PI*2/32,p0=[Math.cos(a0)*.49,-.20,.9+Math.sin(a0)*.49],p1=[Math.cos(a1)*.49,-.20,.9+Math.sin(a1)*.49];tubeSegment(o,p0,p1,.0025,SurfaceFinish.DarkMetal,7,5);}for(let i=0;i<8;i++){const angle=i*Math.PI/4; tubeSegment(o,center,[Math.cos(angle)*.49,-.20,.9+Math.sin(angle)*.49],.0025,SurfaceFinish.DarkMetal,8,5);}return o;}
export function BuildGarageShellSurface(spec={}) {const o=surface(),w=spec.width??22,d=spec.depth??18,h=spec.height??7.15;box(o,0,-d/2,h/2,w,.3,h,SurfaceFinish.Concrete);box(o,-w/2,0,h/2,.3,d,h,SurfaceFinish.Concrete);box(o,w/2,0,h/2,.3,d,h,SurfaceFinish.Concrete);box(o,0,0,h-.14,w,d,.28,SurfaceFinish.DarkMetal);box(o,0,0,3.2,w*.74,.18,.18,SurfaceFinish.SilverMetal);for(let x=-w*.38;x<=w*.38;x+=.16)box(o,x,-d/2+.22,2.5,.005,.005,4.8,SurfaceFinish.DarkMetal,Math.round((x+w*.38)/.16));for(let x=-w/2+1;x<w/2;x+=2.1)box(o,x,-d/2+.35,h-.5,1.7,.04,.045,SurfaceFinish.LEDStrip);return o;}
export function BuildCarLiftSurface(spec={}) {const o=surface(),height=spec.height??2.12,half=spec.width??1.85;for(const x of [-half,half])for(const y of [-1.45,1.45])box(o,x,y,height/2,.18,.18,height,SurfaceFinish.SafetyYellow);box(o,0,0,height,half*2.35,3.5,.18,SurfaceFinish.SilverMetal);for(const y of [-1.7,1.7])box(o,0,y,.08,half*2.5,.13,.16,SurfaceFinish.SafetyYellow);return o;}
export function BuildGaragePropsSurface(spec={}) {const o=surface(),kind=spec.kind??"tool-chest";if(kind.includes("flag")){quad(o,[-.72,-.12,0],[-.04,-.30,0],[-.04,-.30,3.8],[-.72,-.12,3.8],SurfaceFinish.BlackFabric,1);quad(o,[.04,-.30,0],[.72,-.12,0],[.72,-.12,3.8],[.04,-.30,3.8],SurfaceFinish.BlackFabric,2);tubeSegment(o,[-.75,0,.04],[-.75,0,4],.004,SurfaceFinish.DarkMetal,3,6);tubeSegment(o,[.75,0,.04],[.75,0,4],.004,SurfaceFinish.DarkMetal,4,6);box(o,0,0,.06,1.8,.9,.12,SurfaceFinish.DarkMetal,5);}else if(kind.includes("tire")){for(let i=0;i<6;i++)cylinder(o,-.75+i*.3,0,.55,.32,.24,SurfaceFinish.Tire,18,i);box(o,0,0,.12,2.2,.8,.16,SurfaceFinish.SafetyYellow);}else if(kind.includes("bench")){box(o,0,0,.92,2.8,.8,.12,SurfaceFinish.TimberBench);box(o,0,0,1.5,2.6,.1,1,SurfaceFinish.DarkMetal);}else if(kind.includes("compressor")){cylinder(o,0,0,.9,.46,1.6,SurfaceFinish.RedPaint,20);cylinder(o,.65,0,.5,.16,.9,SurfaceFinish.SilverMetal,12);}else if(kind.includes("cabinet")){box(o,0,0,.72,1.1,.7,1.35,SurfaceFinish.RedPaint);box(o,0,-.36,1.08,.78,.025,.4,SurfaceFinish.TetherScreen);for(let z=.22;z<.95;z+=.22)box(o,0,-.38,z,.82,.025,.035,SurfaceFinish.SilverMetal);}else{box(o,0,0,.52,kind.includes("chest")?1.8:1.15,.72,.95,SurfaceFinish.RedPaint);for(let z=.25;z<.9;z+=.2)box(o,0,-.37,z,kind.includes("chest")?1.58:.96,.025,.04,SurfaceFinish.SilverMetal);for(let x of [-.7,.7])cylinder(o,x,0,.07,.1,.1,SurfaceFinish.DarkMetal,8);}return o;}
export function BuildGarageCablesSurface(spec={}) {const o=surface(),length=spec.length??11.5,segments=96;for(let k=0;k<4;k++){for(let i=0;i<segments;i++){const x0=-length/2+length*i/segments,x1=-length/2+length*(i+1)/segments,y0=-.45+k*.3+Math.sin(i/segments*Math.PI*4+k)*.045,y1=-.45+k*.3+Math.sin((i+1)/segments*Math.PI*4+k)*.045,z0=.035+Math.sin(i/segments*Math.PI*4+k)*.055,z1=.035+Math.sin((i+1)/segments*Math.PI*4+k)*.055;tubeSegment(o,[x0,y0,z0],[x1,y1,z1],.0025,k===0?SurfaceFinish.SafetyYellow:SurfaceFinish.Cable,k,6);}}return o;}
export function BuildRaceTrackSurface(spec={}) {
    const o=surface(),length=spec.length??150,major=length*.214,minor=length*.107,segments=160,trackHalfWidth=5.4;
    box(o,0,0,-.12,major*2.75,minor*3,.22,SurfaceFinish.Grass);
    const edgePoint=(angle,offset)=>{const x=Math.cos(angle)/major,y=Math.sin(angle)/minor,scale=1/Math.hypot(x,y);return [major*Math.cos(angle)+x*scale*offset,minor*Math.sin(angle)+y*scale*offset];};
    const strip=(offset,width,material,dashed=false)=>{for(let i=0;i<segments;i++){if(dashed&&i%3===1)continue;const a=i*Math.PI*2/segments,b=(i+1)*Math.PI*2/segments,p0=edgePoint(a,offset-width/2),p1=edgePoint(a,offset+width/2),p2=edgePoint(b,offset+width/2),p3=edgePoint(b,offset-width/2);quad(o,[p0[0],p0[1],.075],[p1[0],p1[1],.075],[p2[0],p2[1],.075],[p3[0],p3[1],.075],material);}};
    for(let i=0;i<segments;i++){const a=i*Math.PI*2/segments,b=(i+1)*Math.PI*2/segments,innerA=edgePoint(a,-trackHalfWidth),outerA=edgePoint(a,trackHalfWidth),outerB=edgePoint(b,trackHalfWidth),innerB=edgePoint(b,-trackHalfWidth);quad(o,[innerA[0],innerA[1],.04],[outerA[0],outerA[1],.04],[outerB[0],outerB[1],.04],[innerB[0],innerB[1],.04],SurfaceFinish.Asphalt);}
    strip(-trackHalfWidth+.12,.16,SurfaceFinish.WhitePaint);strip(trackHalfWidth-.12,.16,SurfaceFinish.WhitePaint);strip(-1.75,.075,SurfaceFinish.TrackPaint,true);strip(1.75,.075,SurfaceFinish.TrackPaint,true);
    strip(-trackHalfWidth-.45,.72,SurfaceFinish.RedPaint,true);strip(trackHalfWidth+.45,.72,SurfaceFinish.RedPaint,true);
    for(let side of [-1,1]){const y=side*(minor+trackHalfWidth+1.15);box(o,0,y,.35,major*2.35,.38,.7,SurfaceFinish.Concrete);for(let x=-major*1.15;x<major*1.15;x+=.72)box(o,x,y,side>0?.73:.73,.36,.32,.045,Math.floor((x+major*1.15)/.72)%2?SurfaceFinish.WhitePaint:SurfaceFinish.RedPaint);}
    for(let tier=0;tier<14;tier++){const y=-(minor+trackHalfWidth+3.2+tier*.46),z=.5+tier*.27;box(o,0,y,z,major*2.2,.56,.24,tier%2?SurfaceFinish.SilverMetal:SurfaceFinish.DarkMetal);for(let x=-major*1.08;x<major*1.08;x+=1.15)box(o,x,y,z+.17,.035,.42,.035,SurfaceFinish.RedPaint);}
    box(o,0,-minor-trackHalfWidth-2.2,4.45,major*1.65,.45,.42,SurfaceFinish.DarkMetal);for(let x=-major*.8;x<=major*.8;x+=2.5)box(o,x,-minor-trackHalfWidth-2.2,5.45,.09,.09,2,SurfaceFinish.SilverMetal);box(o,0,-minor-trackHalfWidth-2.2,6.45,major*1.65,.12,.12,SurfaceFinish.DarkMetal);
    return o;
}

export const SurfaceBuilders = Object.freeze({BuildCycloramaSurface,BuildTiledFloorSurface,BuildCeilingTrussSurface,BuildTripodSurface,BuildCinemaCameraSurface,BuildLightFixtureSurface,BuildWorkstationSurface,BuildForestBackdropSurface,BuildWindFanSurface,BuildGarageShellSurface,BuildCarLiftSurface,BuildGaragePropsSurface,BuildGarageCablesSurface,BuildRaceTrackSurface});
export { moveSurface as TransformSurface, join as JoinSurfaces, box as AppendBox, quad as AppendQuad, cylinder as AppendCylinder, floor as BuildFlatFloor, ConvertYUpPositionToWorldZUp };

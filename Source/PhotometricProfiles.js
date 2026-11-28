export const ProfileVerticalSamples = 128;
export const ProfileAzimuthSamples = 32;
const ProfileLength = ProfileVerticalSamples * ProfileAzimuthSamples;

export function ParseLm63(text,label="photometric") {
    const lines=text.replace(/\r/g,"").split("\n");
    const tilt=lines.findIndex(line=>/^\s*TILT\s*=/i.test(line));
    if(tilt<0)throw new Error(`${label}: missing TILT line`);
    const keywords={};
    for(const line of lines.slice(0,tilt)){const m=/^\s*\[([^\]]+)\]\s*(.*)$/.exec(line);if(m)keywords[m[1].trim().toUpperCase()]=m[2].trim();}
    const rawTokens=lines.slice(tilt+1).join(" ").trim().split(/[\s,]+/).filter(Boolean),tokens=new Array(rawTokens.length);for(let i=0;i<rawTokens.length;i++)tokens[i]=Number(rawTokens[i]);let cursor=0;
    const take=n=>{const a=tokens.slice(cursor,cursor+n);cursor+=n;if(a.length!==n||a.some(Number.isNaN))throw new Error(`${label}: truncated LM-63 profile`);return a;};
    const tiltKind=lines[tilt].split("=")[1].trim().toUpperCase();
    if(tiltKind==="INCLUDE"){take(1);const n=take(1)[0];take(n*2);}else if(tiltKind!=="NONE")throw new Error(`${label}: external TILT file unsupported`);
    const head=take(10);const verticalCount=head[3],azimuthCount=head[4],photometricType=head[5];
    const lampFactors=take(3);if(photometricType!==1)throw new Error(`${label}: Type C only`);
    const vertical=take(verticalCount),azimuth=take(azimuthCount),multiplier=head[2]*(lampFactors[0]||1)*(lampFactors[1]||1);
    const candela=[];for(let row=0;row<azimuthCount;row++)candela.push(Array.from(take(verticalCount),x=>Math.max(0,x*multiplier)));
    return {label,keywords,vertical,azimuth,candela};
}
function bracket(axis,x){if(x<=axis[0])return [0,0,0];if(x>=axis[axis.length-1])return [axis.length-1,axis.length-1,0];let i=1;while(axis[i]<x)i++;return [i-1,i,(x-axis[i-1])/(axis[i]-axis[i-1])];}
function foldAzimuth(p,angle){const a=p.azimuth,first=a[0],last=a[a.length-1];if(a.length===1)return first;if(first===0&&last===90){const f=angle%180;return f>90?180-f:f;}if(first===0&&last===180)return angle>180?360-angle:angle;if(first===90&&last===270)return angle<90?180-angle:angle>270?540-angle:angle;return angle;}
export function SampleCandela(profile,verticalDeg,azimuthDeg){if(verticalDeg<profile.vertical[0]||verticalDeg>profile.vertical.at(-1))return 0;const [v0,v1,vt]=bracket(profile.vertical,verticalDeg);const [a0,a1,at]=bracket(profile.azimuth,foldAzimuth(profile,(azimuthDeg%360+360)%360));const row=i=>profile.candela[i][v0]*(1-vt)+profile.candela[i][v1]*vt;return row(a0)*(1-at)+row(a1)*at;}
export function ResamplePhotometry(profile){const pixels=new Float32Array(ProfileLength);let peak=0;for(let a=0;a<ProfileAzimuthSamples;a++)for(let v=0;v<ProfileVerticalSamples;v++){const q=SampleCandela(profile,v*180/(ProfileVerticalSamples-1),a*360/ProfileAzimuthSamples);pixels[a*ProfileVerticalSamples+v]=q;peak=Math.max(peak,q);}if(!peak)throw new Error(`${profile.label}: empty profile`);for(let i=0;i<pixels.length;i++)pixels[i]/=peak;let solidAngle=0;const step=Math.PI/(ProfileVerticalSamples-1);for(let v=0;v<ProfileVerticalSamples;v++){let row=0;for(let a=0;a<ProfileAzimuthSamples;a++)row+=pixels[a*ProfileVerticalSamples+v];solidAngle+=row/ProfileAzimuthSamples*2*Math.PI*Math.sin(v*step)*step*(v===0||v===ProfileVerticalSamples-1?.5:1);}return {pixels,peakCandela:peak,solidAngle};}
export async function LoadPhotometricProfiles(base="./Resources/Photometry/"){const names=["CarParkDownlight","LowBeamHeadlamp","TailLamp"],profiles=[];for(const name of names){const response=await fetch(`${base}${name}.ies`);if(!response.ok)throw new Error(`Cannot fetch ${name}.ies`);profiles.push(ResamplePhotometry(ParseLm63(await response.text(),name)));}return {names,profiles};}

// FILAMENTLOOKS.JS
// 🧩 The six particle looks as GPU programs. Every particle is a camera-facing capsule drawn between a Head and a Tail
//    point, so one quad gives a dot, a comet, a speed line or a clone bar. Motion is computed from the clock alone
//    (no simulation state), so 100k+ particles cost one draw call and scrubbing time is free.
// 📝 Car axes: +X forward, +Y up, Z across (left side is −Z). uProfile holds the car silhouette along X
//    (R roof, G half-width, B and A the same felt by passing air; see SurfaceScatter.SilhouetteProfile).

import { Orbits, Flora, Transition, FibreWave } from './FilamentFibres.js';

export const Prelude = /* glsl */ `
attribute vec2 aCorner;                 // quad corner: x −1 tail … +1 head, y −1 … +1 across
attribute vec4 aSeed;                   // four uniform randoms per particle
attribute vec3 aAnchor;                 // a point on the car surface (car space)
attribute vec3 aFacing;                 // that surface's outward normal
attribute float aFamily;                // VehicleAssembly.Family of that surface

uniform float uClock, uSpeed, uTail, uSize, uTurbulence, uFade, uSide, uVariant;
uniform float uMorph, uMorphRate, uRelease, uReleaseRate, uScan, uCharge;
uniform float uCount, uAperture, uFocus;
uniform float uFront, uFrontSpeed, uSweepMode;   // vehicle transition: edge x, its speed (m/s), stage          // live instance count; lens aperture (m) and focus distance for bokeh
uniform vec2 uViewport;
uniform vec3 uTone0, uTone1, uTone2, uTone3;      // palette: base, accent, hot core, contrast
uniform sampler2D uProfile;
uniform float uProfileFrom, uProfileSpan;
uniform vec3 uCarMin, uCarMax;

varying vec2 vLocal;
varying float vLength, vHalf, vFade, vBokeh, vRamp;
varying vec3 vTint;

const float Tau = 6.2831853;
// PCG integer hash of the float's bits: stays random for large inputs (sin-based hashes clump past ~10⁵).
float Hash(float N)
{
    uint X = floatBitsToUint(N) * 747796405u + 2891336453u;
    X = ((X >> ((X >> 28u) + 4u)) ^ X) * 277803737u;
    return float((X >> 22u) ^ X) / 4294967295.0;
}
float Ramp = 1.0;                       // a look sets 0 for thread segments: solid, no comet fade from tail to head
vec4 Profile(float X) { return texture2D(uProfile, vec2(clamp((X - uProfileFrom) / uProfileSpan, 0.0, 1.0), 0.5)); }
vec3 Sphere(float U, float V) { float Z = U * 2.0 - 1.0, R = sqrt(max(0.0, 1.0 - Z * Z)), A = V * Tau; return vec3(R * cos(A), Z, R * sin(A)); }

// Two octaves of Arnold–Beltrami–Childress flow drifting in time: swirly, divergence-free, three sines per axis.
vec3 Flow(vec3 P, float T)
{
    vec3 Q = P * 0.55 + vec3(T * 0.23, T * 0.17, -T * 0.13);
    vec3 V = vec3(sin(Q.z) + 0.8 * cos(Q.y), 0.7 * sin(Q.x) + cos(Q.z), sin(Q.y) + 0.7 * cos(Q.x));
    vec3 R = P * 1.31 + vec3(1.7, -T * 0.29, T * 0.21);
    V += 0.45 * vec3(0.6 * sin(R.z) + cos(R.y), sin(R.x) + 0.6 * cos(R.z), 0.6 * sin(R.y) + cos(R.x));
    return V;
}
vec3 Spin(vec3 P, float A) { float C = cos(A), S = sin(A); return vec3(C * P.x - S * P.z, P.y, S * P.x + C * P.z); }
`;

// Turns Head/Tail/Width (metres) into a screen-space capsule. Width under ~0.75 px is clamped and dimmed instead,
// which keeps far particles from shimmering. Depth of field: a thin-lens circle of confusion, A·|1/focus − 1/depth|,
// widens out-of-focus particles into bokeh discs and spreads their light (dimmer per pixel).
export const Projection = /* glsl */ `
void main()
{
    vec3 Head, Tail, Tint; float Width, Glow;
    Track(Head, Tail, Tint, Width, Glow);
    vTint = Tint * Glow * uFade;
    vRamp = Ramp;
    mat4 Into = projectionMatrix * modelViewMatrix;
    vec4 H = Into * vec4(Head, 1.0), T = Into * vec4(Tail, 1.0);
    if (H.w < 0.08 || T.w < 0.08 || Glow <= 0.0) { gl_Position = vec4(0.0, 0.0, 2.0, 1.0); vHalf = 1.0; vLength = 0.0; vFade = 0.0; vBokeh = 0.0; vLocal = vec2(9.0); return; }
    vec2 Half = uViewport * 0.5;
    vec2 ScreenHead = H.xy / H.w * Half, ScreenTail = T.xy / T.w * Half;
    vec2 Along = ScreenHead - ScreenTail;
    float Length = length(Along);
    vec2 Direction = Length > 1e-4 ? Along / Length : vec2(1.0, 0.0);
    vec2 Across = vec2(-Direction.y, Direction.x);
    float Pixels = Width * projectionMatrix[1][1] * Half.y / H.w;
    float Sharp = clamp(Pixels, 0.75, 5.0);                                        // near-camera particles stay crisp
    float Blur = min(uAperture * abs(1.0 / uFocus - 1.0 / H.w) * projectionMatrix[1][1] * Half.y, 44.0);
    float Radius = max(Sharp, Blur);
    vFade = clamp(Pixels / 0.75, 0.0, 1.0) * clamp(pow(Sharp / Radius, 1.25), 0.05, 1.0);
    vBokeh = smoothstep(Sharp + 1.0, Sharp + 6.0, Blur);
    float Pick = aCorner.x * 0.5 + 0.5;
    vec2 Screen = mix(ScreenTail, ScreenHead, Pick) + (Direction * aCorner.x + Across * aCorner.y) * Radius * 1.6;
    float Depth = mix(T.z / T.w, H.z / H.w, Pick);
    vLocal = vec2(Pick * Length + aCorner.x * Radius * 1.6, aCorner.y * Radius * 1.6);
    vLength = Length; vHalf = Radius;
    gl_Position = vec4(Screen / Half, clamp(Depth, -1.0, 1.0), 1.0);
}`;

export const Fragment = /* glsl */ `
varying vec2 vLocal;
varying float vLength, vHalf, vFade, vBokeh, vRamp;
varying vec3 vTint;
void main()
{
    // Thread segments (vRamp 0) are cut flat at both ends so neighbouring segments meet without doubling at the joint.
    float Beyond = vLocal.x - clamp(vLocal.x, 0.0, vLength);
    if (vRamp < 0.5 && vBokeh < 0.5 && vLength > 0.5 && abs(Beyond) > 0.0) discard;
    float Gap = length(vec2(vRamp < 0.5 && vBokeh < 0.5 ? 0.0 : Beyond, vLocal.y)) / vHalf;
    float Soft = exp(-Gap * Gap * 2.4) + 0.18 * exp(-Gap * 1.2) * step(Gap, 1.6);
    float Disc = (1.0 - smoothstep(0.84, 1.0, Gap)) * (0.7 + 0.55 * smoothstep(0.55, 0.96, Gap));   // flat bokeh disc, brighter rim
    float Body = mix(Soft, Disc, vBokeh);
    float Ramp = vLength > 0.5 ? clamp(vLocal.x / vLength, 0.0, 1.0) : 1.0;
    float Comet = mix(1.0, mix(0.12, 1.0, Ramp * Ramp), vRamp);
    vec3 Colour = vTint * Body * Comet * vFade;
    if (Body < 0.003) discard;
    gl_FragColor = vec4(Colour, 1.0);
}`;

//------------------------------------------------------------------------------------------------------------------------
//                                                    LOOKS
//------------------------------------------------------------------------------------------------------------------------

// 1 · Light-speed tunnel: the car's welcome / Sport-mode animation. Lines rush past a parked car.
const Streaks = /* glsl */ `
vec3 StreakAt(float Clock, float Speed)
{
    float Range = 150.0;
    float X = 75.0 - mod(aSeed.x * Range + Clock * Speed, Range);
    float Angle = aSeed.z * Tau;
    float Radius = mix(2.4, 30.0, pow(aSeed.y, 1.7));
    float Y = 1.1 + sin(Angle) * Radius * 0.62;
    float Z = cos(Angle) * Radius;
    if (Y < 0.02) { Y = 0.015; Z = (floor(aSeed.z * 23.0) - 11.0) * 1.35 + (aSeed.y - 0.5) * 0.08; }   // fallen below: ride a floor lane
    return vec3(X, Y, Z);
}
void Track(out vec3 Head, out vec3 Tail, out vec3 Tint, out float Width, out float Glow)
{
    float Speed = uSpeed * mix(28.0, 75.0, Hash(aSeed.x * 7.3));
    Head = StreakAt(uClock, Speed);
    Tail = Head + vec3(Speed * uTail * 0.9, 0.0, 0.0);
    float Lane = step(Head.y, 0.02);
    float Pick = Hash(aSeed.y * 3.1);
    Tint = Pick < 0.07 ? uTone3 : mix(uTone0, uTone1, Pick);
    Tint = mix(Tint, uTone2, 0.4 * smoothstep(8.0, 2.4, length(Head.yz - vec2(1.1, 0.0))));
    Width = mix(0.012, 0.04, pow(Hash(aSeed.z * 5.7), 2.0)) * uSize * (Lane > 0.5 ? 0.8 : 1.0);
    Glow = mix(0.12, 0.7, pow(Hash(aSeed.x * 1.9), 2.0)) * smoothstep(75.0, 45.0, Head.x) * smoothstep(-75.0, -35.0, Head.x);
}`;

// 2 · Digital twin: dust swirls in and assembles the car (nose first), a scan line sweeps it, then wind blows it off.
const Assemble = /* glsl */ `
vec3 SurfaceAt(float Clock, float Morph, float Release, out float Settled)
{
    float Sweep = (aAnchor.x - uCarMin.x) / (uCarMax.x - uCarMin.x);
    float Start = (1.0 - Sweep) * 0.5 + aSeed.y * 0.15;
    float Local = smoothstep(Start, Start + 0.35, Morph);
    float Eased = Local * Local * (3.0 - 2.0 * Local);
    Settled = Eased;
    vec3 Away = aAnchor + aFacing * mix(1.5, 6.0, aSeed.z) + (Sphere(aSeed.x, aSeed.w) * mix(3.0, 9.0, aSeed.y));
    Away.y = abs(Away.y - 0.6) + 0.3;
    Away = Spin(Away, (1.0 - Eased) * 2.4 + Clock * 0.18);
    Away += Flow(Away * 0.5, Clock) * 0.6 * uTurbulence;
    vec3 P = mix(Away, aAnchor + aFacing * 0.012, Eased);
    float Leave = smoothstep(1.0 - Sweep - 0.1, 1.0 - Sweep + 0.45 + aSeed.z * 0.2, Release * 1.5);
    P += vec3(-1.0, 0.18, 0.0) * Leave * Leave * 16.0 + Flow(P * 0.7, Clock * 1.6) * Leave * 1.4 * uTurbulence;
    Settled *= 1.0 - Leave;
    return P;
}
void Track(out vec3 Head, out vec3 Tail, out vec3 Tint, out float Width, out float Glow)
{
    float Settled, Before;
    Head = SurfaceAt(uClock, uMorph, uRelease, Settled);
    Tail = SurfaceAt(uClock - uTail * 0.6, uMorph - uMorphRate * uTail * 0.6, uRelease - uReleaseRate * uTail * 0.6, Before);
    if (distance(Head, Tail) > 3.0) Tail = Head;
    float ScanX = mix(uCarMax.x + 0.4, uCarMin.x - 0.4, uScan);
    float Band = uScan > 0.0 && uScan < 1.0 ? exp(-pow((aAnchor.x - ScanX) / 0.18, 2.0)) : 0.0;
    float Pick = Hash(aSeed.z * 9.1);
    Tint = mix(uTone0, uTone1, Pick * Pick);
    if (aFamily > 12.5 && aFamily < 13.5) Tint = uTone2;                           // headlights
    if (aFamily > 13.5 && aFamily < 14.5) Tint = vec3(1.0, 0.08, 0.06);             // tail lamps
    Tint = mix(Tint, uTone2, Band);
    Width = mix(0.011, 0.02, aSeed.w) * uSize * (1.0 + (1.0 - Settled) * 0.8);
    Glow = mix(0.35, 0.8, Pick) * (0.55 + 0.45 * Settled) + Band * 3.5 + (1.0 - Settled) * 0.4;
    if (aFamily > 12.5 && aFamily < 14.5) Glow *= 2.2;
}`;

// 3 · Wind tunnel: streamlines part around the car's silhouette, crowd over the roof and tumble in the wake.
//     uVariant 1 = smoke rakes (a few fixed streamlines) instead of a full fill.
const Airflow = /* glsl */ `
vec3 StreamAt(float Clock, out float Near, out float Press)
{
    float Range = uProfileSpan;
    float X = uProfileFrom + Range - mod(aSeed.x * Range + Clock * uSpeed * 9.0, Range);
    float Y0 = mix(0.06, 2.4, aSeed.y);
    float Z0 = (aSeed.z - 0.5) * 2.4;
    if (uVariant > 0.5)
    {
        Y0 = 0.14 + floor(aSeed.y * 8.0) * 0.27 + (Hash(aSeed.y * 31.0) - 0.5) * 0.012;
        Z0 = (floor(aSeed.z * 5.0) - 2.0) * 0.42 + (Hash(aSeed.z * 17.0) - 0.5) * 0.012;
    }
    vec4 Felt = Profile(X);
    vec2 Shape = vec2(Felt.a, Felt.b) + 0.07;
    vec2 Start = vec2(Z0, Y0);
    float Reach = length(Start);
    vec2 Toward = Reach > 1e-4 ? Start / Reach : vec2(0.0, 1.0);
    vec2 Scaled = abs(Toward) / Shape;
    float Rim = 1.0 / pow(pow(Scaled.x, 4.0) + pow(Scaled.y, 4.0), 0.25);        // distance to a boxy outline along Toward
    float Moved = sqrt(Reach * Reach + Rim * Rim);
    vec2 Bent = Toward * Moved;
    Near = Rim / Moved;
    vec4 Upstream = Profile(X + 0.35);
    Press = smoothstep(0.05, 0.35, Felt.b - Upstream.b) * Near * Near;
    vec3 P = vec3(X, Bent.y, Bent.x);
    float Wake = smoothstep(uCarMin.x + 0.4, uCarMin.x - 0.8, X) * smoothstep(uCarMin.x - 9.0, uCarMin.x - 1.5, X);
    P += Flow(P * 1.5, Clock * 2.2) * 0.22 * uTurbulence * Wake * Near * 2.0;
    P.y = max(P.y, 0.02);
    return P;
}
void Track(out vec3 Head, out vec3 Tail, out vec3 Tint, out float Width, out float Glow)
{
    float Near, Press, NearTail, PressTail;
    Head = StreamAt(uClock, Near, Press);
    Tail = StreamAt(uClock - uTail * 2.5, NearTail, PressTail);
    if (Tail.x < Head.x || distance(Head, Tail) > 6.0) Tail = Head;
    float Fast = smoothstep(0.15, 0.9, Near);
    Tint = mix(uTone0 * 0.6, uTone0, smoothstep(0.05, 0.4, Near));
    Tint = mix(Tint, uTone1, Fast);
    Tint = mix(Tint, uTone2, Fast * Fast * 0.5);
    Tint = mix(Tint, uTone3, Press * 0.85);
    Width = mix(0.012, 0.022, aSeed.w) * uSize * (uVariant > 0.5 ? 1.4 : 1.0);
    float Edges = smoothstep(uProfileFrom, uProfileFrom + 2.0, Head.x) * smoothstep(uProfileFrom + uProfileSpan, uProfileFrom + uProfileSpan - 2.0, Head.x);
    Glow = mix(0.05, 1.0, Near * Near) * Edges * (uVariant > 0.5 ? 1.7 : 1.0);
}`;

// 4 · Particular-style emitters: two lights orbit the car shedding particles that age through a colour ramp, drift in
//     a turbulence field and shrink out. The emitter lights themselves are a few large particles.
const Turbulence = /* glsl */ `
vec3 EmitterAt(float Time, float Which)
{
    float Angle = Time * 0.5 * uSpeed + Which * 3.14159;
    return vec3(cos(Angle) * 4.6, 1.0 + 0.85 * sin(Time * 1.1 * uSpeed + Which * 2.0), sin(Angle) * 3.2);
}
vec3 ParticleAt(float Clock, out float Life, out float Age, out float Which)
{
    Life = mix(2.4, 5.5, aSeed.y);
    Which = step(0.5, aSeed.z);
    Age = mod(Clock + aSeed.x * 37.0, Life);
    float Birth = Clock - Age;
    vec3 P = EmitterAt(Birth, Which);
    vec3 Velocity = Sphere(Hash(aSeed.x * 3.7 + floor((Clock + aSeed.x * 37.0) / Life)), aSeed.w) * mix(0.3, 2.4, Hash(aSeed.z * 4.3));
    Velocity += (EmitterAt(Birth + 0.05, Which) - P) * 6.0;
    float Step = Age / 6.0;
    for (int Each = 0; Each < 6; Each++)
    {
        float Time = (float(Each) + 0.5) * Step;
        P += (Velocity * exp(-Time * 1.1) + Flow(P, Birth + Time) * 0.5 * uTurbulence + vec3(0.0, 0.12, 0.0)) * Step;
    }
    P.y = abs(P.y - 0.02) + 0.02;
    return P;
}
void Track(out vec3 Head, out vec3 Tail, out vec3 Tint, out float Width, out float Glow)
{
    float Life, Age, Which, LifeTail, AgeTail, WhichTail;
    if (aSeed.w < 0.004)                                                              // the emitter lights
    {
        float Which = step(0.5, aSeed.z);
        Head = EmitterAt(uClock, Which); Tail = EmitterAt(uClock - 0.06, Which);
        Tint = mix(uTone2, Which > 0.5 ? uTone3 : uTone1, 0.25); Width = 0.07 * uSize; Glow = 1.6;
        return;
    }
    Head = ParticleAt(uClock, Life, Age, Which);
    Tail = ParticleAt(uClock - uTail, LifeTail, AgeTail, WhichTail);
    if (AgeTail > Age) Tail = Head;
    float Lived = Age / Life;
    vec3 Mid = Which > 0.5 ? uTone3 : uTone0;
    Tint = Lived < 0.12 ? mix(uTone2, uTone1, Lived / 0.12) : mix(uTone1, Mid, smoothstep(0.12, 0.6, Lived));
    Width = mix(0.045, 0.01, sqrt(Lived)) * uSize;
    Glow = smoothstep(0.0, 0.04, Lived) * (1.0 - smoothstep(0.55, 1.0, Lived)) * mix(0.6, 1.6, Hash(aSeed.y * 8.1));
}`;

// 5 · MoGraph clone field: a grid of clones around the car, scaled by a sphere effector circling the car, a ring
//     shockwave from under the car and a noise effector. uVariant 0 = bars (scale Y), 1 = floating dots (move Y).
const Clones = /* glsl */ `
void Track(out vec3 Head, out vec3 Tail, out vec3 Tint, out float Width, out float Glow)
{
    float Index = float(gl_InstanceID);
    float Column = mod(Index, uSide), Row = floor(Index / uSide);
    float Spacing = 30.0 / uSide;
    vec3 Cell = vec3((Column - uSide * 0.5 + 0.5) * Spacing, 0.0, (Row - uSide * 0.5 + 0.5) * Spacing);
    float Time = uClock * uSpeed;
    vec2 Ball = vec2(cos(Time * 0.45) * 7.5, sin(Time * 0.45) * 6.0);
    float Sphere = smoothstep(4.2, 0.0, distance(Cell.xz, Ball));
    Sphere = Sphere * Sphere * (3.0 - 2.0 * Sphere);
    float Radius = length(Cell.xz * vec2(0.8, 1.0));
    float Wave = mod(Time * 5.0, 24.0);
    float Ring = exp(-pow((Radius - Wave) / 0.9, 2.0)) * (1.0 - Wave / 24.0);
    float Noise = 0.5 + 0.5 * sin(Cell.x * 0.55 + Time * 0.9) * cos(Cell.z * 0.62 - Time * 0.7);
    float Strength = clamp(Sphere + Ring * 0.85 + Noise * 0.22 * uTurbulence, 0.0, 1.6);
    vec4 Felt = Profile(Cell.x);
    float Under = step(abs(Cell.z), Felt.g + 0.25) * step(0.05, Felt.g);
    float Lift = 0.03 + 1.7 * Strength;
    if (uVariant > 0.5) { Head = Cell + vec3(0.0, 0.08 + Lift, 0.0); Tail = Head; Width = Spacing * 0.2 * uSize * (0.6 + Strength); }
    else { Head = Cell + vec3(0.0, Lift, 0.0); Tail = Cell + vec3(0.0, 0.004, 0.0); Width = Spacing * 0.16 * uSize; }
    Tint = mix(uTone3 * 0.5, uTone0, smoothstep(0.05, 0.45, Strength));
    Tint = mix(Tint, uTone1, smoothstep(0.45, 0.9, Strength));
    Tint = mix(Tint, uTone2, smoothstep(0.9, 1.4, Strength));
    float Edge = 1.0 - smoothstep(10.0, 15.0, max(abs(Cell.x), abs(Cell.z)));
    Glow = (0.18 + 1.5 * Strength) * Edge * (1.0 - Under);
}`;

// 6 · Charging: energy runs down the cable (helix around it), into the port, along the floor into the battery and
//     fills cell by cell. uCharge 0 … 1 is the state of charge.
const Charge = /* glsl */ `
vec3 Bezier(vec3 A, vec3 B, vec3 C, vec3 D, float U) { float V = 1.0 - U; return V * V * V * A + 3.0 * V * V * U * B + 3.0 * V * U * U * C + U * U * U * D; }
vec3 CellAt(float Slot)
{
    float Column = mod(Slot, 10.0), Row = floor(Slot / 10.0);
    float Low = uCarMin.x + 1.25, High = uCarMax.x - 1.35;
    return vec3(mix(Low, High, (Column + 0.5) / 10.0), 0.24, (Row - 1.5) * 0.36);
}
vec3 ChargeAt(float Clock, out float Phase, out float Slot)
{
    vec3 Port = vec3(uCarMin.x + 1.05, 0.98, uCarMin.z * 0.86);
    vec3 Post = vec3(Port.x - 1.4, 0.03, Port.z - 2.8);
    Slot = floor(aSeed.z * 40.0);
    Phase = fract(Clock * 0.22 * uSpeed + aSeed.x);
    vec3 P;
    if (Phase < 0.45)
    {
        float U = Phase / 0.45;
        P = Bezier(Post + vec3(0.0, 1.1, 0.0), Post + vec3(0.2, 1.9, 0.4), Port + vec3(-0.6, 0.5, -0.9), Port, U);
        float Turn = U * 38.0 + Clock * 4.0 + aSeed.y * Tau;
        P += vec3(0.0, cos(Turn), sin(Turn)) * 0.035;
    }
    else if (Phase < 0.62)
    {
        float U = (Phase - 0.45) / 0.17;
        vec3 Entry = vec3(uCarMin.x + 1.05, 0.24, 0.0);
        P = Bezier(Port, Port + vec3(0.1, -0.5, 0.3), Entry + vec3(0.0, 0.0, -0.6), Entry, U);
    }
    else
    {
        float U = min(1.0, (Phase - 0.62) / 0.2);
        vec3 Entry = vec3(uCarMin.x + 1.05, 0.24, 0.0), Cell = CellAt(Slot);
        P = Bezier(Entry, Entry + vec3(0.8, 0.0, 0.0), vec3(Cell.x - 0.5, 0.24, Cell.z), Cell, U);
        P += (vec3(Hash(aSeed.y * 5.0), Hash(aSeed.w * 7.0), Hash(aSeed.z * 3.0)) - 0.5) * vec3(0.3, 0.1, 0.28) * smoothstep(0.82, 1.0, Phase + 0.18 * U);
    }
    return P;
}
void Track(out vec3 Head, out vec3 Tail, out vec3 Tint, out float Width, out float Glow)
{
    float Phase, Slot, PhaseTail, SlotTail;
    if (aSeed.y < 0.5)                                                                // resting charge in the cells
    {
        float Cell = floor(aSeed.z * 40.0);
        Head = CellAt(Cell) + (vec3(aSeed.x, aSeed.w, Hash(aSeed.z * 13.0)) - 0.5) * vec3(0.32, 0.1, 0.3);
        Tail = Head;
        float Full = step(Cell / 40.0, uCharge);
        float Front = exp(-pow((Cell / 40.0 - uCharge) * 22.0, 2.0));
        Tint = mix(uTone3 * 0.25, uTone0, Full);
        Tint = mix(Tint, uTone2, Front);
        Width = 0.016 * uSize;
        Glow = 0.15 + 0.75 * Full * (0.75 + 0.25 * sin(uClock * 3.0 + Cell)) + Front * 2.0;
        return;
    }
    Head = ChargeAt(uClock, Phase, Slot);
    Tail = ChargeAt(uClock - uTail * 0.5, PhaseTail, SlotTail);
    if (PhaseTail > Phase || distance(Head, Tail) > 1.5) Tail = Head;
    Tint = Phase < 0.45 ? mix(uTone1, uTone2, 0.3) : (Phase < 0.62 ? uTone2 : uTone1);
    Width = (Phase < 0.45 ? 0.02 : 0.024) * uSize;
    Glow = (0.35 + 0.4 * Hash(aSeed.x * 3.3)) * smoothstep(0.0, 0.03, Phase) * (1.0 - smoothstep(0.86, 1.0, Phase));
}`;

/**
 * Car: how the car body shows by default — Paint (graphite), Ghost (black mirror; fades with the assembly),
 * Glass (see-through, writes no depth so particles inside show) or Hidden.
 * View: camera preset { Yaw, Pitch, Distance, Target }. Variant: optional look-specific switch.
 */
export const Looks = [
    { Key: 'Streaks', Name: 'Light speed', Tagline: 'Welcome and Sport-mode speed lines rushing past the car', Track: Streaks, Car: 'Paint',
      View: { Yaw: 200, Pitch: 9, Distance: 9.5, Target: [1.2, 0.9, 0] }, Defaults: { Speed: 1, Tail: 0.07, Size: 1, Turbulence: 0.5, Count: 60000, Palette: 'Ice' } },
    { Key: 'Assemble', Name: 'Digital twin', Tagline: 'Dust swirls in and assembles the car, a scan sweeps it, then the wind takes it', Track: Assemble, Car: 'Ghost',
      View: { Yaw: 38, Pitch: 14, Distance: 10.5, Target: [0, 0.75, 0] }, Defaults: { Speed: 1, Tail: 0.06, Size: 1, Turbulence: 0.6, Count: 120000, Palette: 'Ice' } },
    { Key: 'Airflow', Name: 'Airflow', Tagline: 'Wind-tunnel streamlines over the roof, red where the air piles up, wake behind', Track: Airflow, Car: 'Ghost',
      View: { Yaw: 128, Pitch: 20, Distance: 12, Target: [-0.8, 0.7, 0] }, Defaults: { Speed: 1, Tail: 0.16, Size: 1, Turbulence: 0.7, Count: 25000, Palette: 'Ice' },
      Variant: { Label: 'Flow', Options: ['Fill', 'Smoke rakes'] } },
    { Key: 'Turbulence', Name: 'Particular', Tagline: 'Two orbiting emitters, colour over life, size over life, turbulence field', Track: Turbulence, Car: 'Paint',
      View: { Yaw: 60, Pitch: 18, Distance: 17, Target: [0, 1.0, 0] }, Defaults: { Speed: 1, Tail: 0.05, Size: 1, Turbulence: 1, Count: 90000, Palette: 'Ember' } },
    { Key: 'Clones', Name: 'Clone field', Tagline: 'MoGraph cloner grid with a sphere effector, a shockwave ring and noise', Track: Clones, Car: 'Paint',
      View: { Yaw: 30, Pitch: 26, Distance: 17, Target: [0, 0.4, 0] }, Defaults: { Speed: 1, Tail: 0.05, Size: 1, Turbulence: 1, Count: 40000, Palette: 'Aurora' },
      Variant: { Label: 'Clones', Options: ['Bars', 'Dots'] } },
    { Key: 'Charge', Name: 'Charging', Tagline: 'Energy runs down the cable, into the port and fills the battery cell by cell', Track: Charge, Car: 'Glass',
      View: { Yaw: 236, Pitch: 22, Distance: 9.5, Target: [-0.8, 0.5, -0.4] }, Defaults: { Speed: 1, Tail: 0.08, Size: 1, Turbulence: 0.5, Count: 30000, Palette: 'Aurora' } },
    { Key: 'Orbits', Name: 'Orbits', Tagline: 'Evenly spaced rings of light threads orbit a glass sphere, fast inside, a few spiralling in from far out', Track: Orbits, Car: 'Hidden', Sphere: true,
      Backdrop: ['#0b0920', '#010104'], View: { Yaw: 104, Pitch: 13, Distance: 10.5, Target: [0.6, 1.5, 0] },
      Defaults: { Speed: 1, Tail: 0.05, Size: 1, Turbulence: 0.5, Count: 32000, Palette: 'Lilac', Aperture: 0.05 } },
    { Key: 'Flora', Name: 'Flora', Tagline: 'Flowers, leaves and fern fans made of glittering particles, with bokeh', Track: Flora, Car: 'Hidden',
      Backdrop: ['#021209', '#000201'], View: { Yaw: 90, Pitch: 7, Distance: 8, Target: [0, 1.1, 0] },
      Defaults: { Speed: 1, Tail: 0.05, Size: 1, Turbulence: 0.6, Count: 140000, Palette: 'Jade', Aperture: 0.12 } },
    { Key: 'Transition', Name: 'Vehicle transition', Tagline: 'An electric edge sweeps the car: dust to paint, graphite to lime, then back to dust', Track: Transition, Car: 'Paint', Sweep: true,
      Backdrop: ['#050414', '#000001'], View: { Yaw: 34, Pitch: 11, Distance: 10.5, Target: [0, 0.75, 0] },
      Defaults: { Speed: 1, Tail: 0.04, Size: 1, Turbulence: 0, Count: 140000, Palette: 'Ice', Aperture: 0.03 } },
    { Key: 'FibreWave', Name: 'Fibre wave', Tagline: 'A twisting sheet of optical fibres with pulses and a sparkling leading edge', Track: FibreWave, Car: 'Hidden',
      Backdrop: ['#0b4146', '#021316'], View: { Yaw: 96, Pitch: 6, Distance: 11, Target: [1.5, 1.6, 0] },
      Defaults: { Speed: 1, Tail: 0.05, Size: 1, Turbulence: 0.5, Count: 45000, Palette: 'Teal', Aperture: 0.1 } },
];

export const Palettes = {
    Ice:    ['#46a8ff', '#7ff3ff', '#ffffff', '#ff3d6e'],
    Ember:  ['#ff4a12', '#ffae3c', '#fff1d6', '#c21dff'],
    Aurora: ['#21e08a', '#3cc8ff', '#eafff6', '#8a4dff'],
    Rosso:  ['#ff1f2e', '#ff8a55', '#fff4ec', '#5d6dff'],
    Mono:   ['#9aa3ad', '#dfe6ee', '#ffffff', '#5c6672'],
    Lilac:  ['#8f7dff', '#d6cfff', '#ffffff', '#5b4bd6'],
    Jade:   ['#19c98a', '#7affc9', '#eafff4', '#0a7d6b'],
    Teal:   ['#18b9b0', '#62f6e7', '#eafffb', '#0b6c78'],
};

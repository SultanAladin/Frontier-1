// FILAMENTFIBRES.JS
// 🧩 Four looks drawn from reference stills (Follow-up 24): light threads orbiting a glass sphere, flowers built from
//    glittering particles with bokeh, a vehicle transition (an electric edge sweeping the car) and a fibre-optic wave.
// 📝 A thread is a run of capsules: instance n draws segment n mod S of thread floor(n / S), from the curve at u to
//    u + 1/S. The same per-instance trick turns some threads into sparkles riding the same curves.
//    These Track functions run inside FilamentLooks.Prelude (Hash, Flow, Spin, Sphere, palette uniforms).

// 7 · Orbits: concentric rings of light threads in one plane around a glass sphere, evenly spaced and slowly widening
//     outward, Kepler-fast inside. Every 40th thread spirals in from far out in the same plane; dots ride the rings.
export const Orbits = /* glsl */ `
const vec3 OrbitCentre = vec3(0.0, 1.6, 0.0);
const float OrbitSegments = 96.0;
vec3 RingTilt(vec3 P)
{
    const float Tilt = 0.36, Roll = -0.32;                                       // one shared plane: the rings never cross
    P = vec3(P.x, P.y * cos(Tilt) - P.z * sin(Tilt), P.y * sin(Tilt) + P.z * cos(Tilt));
    return OrbitCentre + vec3(P.x * cos(Roll) - P.y * sin(Roll), P.x * sin(Roll) + P.y * cos(Roll), P.z);
}
float RingRadius(float Ring, float Rings) { return 1.55 + 5.6 * pow((Ring + 0.5) / Rings, 1.25); }
vec3 OrbitPoint(float Ring, float Rings, float U, float Time)
{
    float Radius = RingRadius(Ring, Rings);
    float Spiral = step(mod(Ring, 40.0), 0.5);
    float Reach = Radius * (1.0 + Spiral * pow(1.0 - U, 1.5) * 1.6);
    float Span = (1.5 + 0.5 * sin(Ring * 0.37)) * (1.0 + Spiral * 1.2);                // arc length drifts smoothly ring to ring
    float Angle = Ring * 2.39996 + Time * uSpeed * 0.55 * pow(1.65 / Radius, 1.5) + (U - 1.0) * Span;   // heads spread by the golden angle
    return RingTilt(vec3(cos(Angle) * Reach, 0.0, sin(Angle) * Reach));
}
void Track(out vec3 Head, out vec3 Tail, out vec3 Tint, out float Width, out float Glow)
{
    float Index = float(gl_InstanceID);
    float Fibre = floor(Index / OrbitSegments), Segment = mod(Index, OrbitSegments);
    float Rings = max(1.0, floor(uCount / OrbitSegments));
    if (Hash(Fibre * 9.7) < 0.012)                                                // sparkle slot: dots riding the rings, a little loose dust
    {
        if (Hash(Index * 0.311) < 0.15)
        {
            vec3 Box = vec3(Hash(Index * 0.71), Hash(Index * 0.93), Hash(Index * 1.37)) - 0.5;
            Head = OrbitCentre + Box * vec3(22.0, 9.0, 18.0) + vec3(0.0, sin(uClock * 0.2 + Index) * 0.2, 0.0);
            Tail = Head;
            Tint = mix(uTone1, uTone2, 0.5); Width = 0.014 * uSize;
            Glow = mix(0.3, 1.4, pow(Hash(Index * 4.3), 4.0)) * (0.6 + 0.4 * sin(uClock * 2.0 + Index * 0.37));
            return;
        }
        float Ring = floor(Hash(Index * 0.53) * Rings), Phase = Hash(Index * 0.77) * Tau;
        Head = OrbitPoint(Ring, Rings, 1.0, uClock + Phase / (0.55 * uSpeed * pow(1.65 / RingRadius(Ring, Rings), 1.5)));
        Tail = OrbitPoint(Ring, Rings, 1.0 - 0.006, uClock + Phase / (0.55 * uSpeed * pow(1.65 / RingRadius(Ring, Rings), 1.5)));
        Tint = uTone2; Width = 0.01 * uSize;
        Glow = mix(0.6, 2.2, pow(Hash(Index * 5.3), 4.0));
        return;
    }
    float From = Segment / OrbitSegments, To = (Segment + 1.0) / OrbitSegments;
    Head = OrbitPoint(Fibre, Rings, To, uClock);
    Tail = OrbitPoint(Fibre, Rings, From, uClock);
    Ramp = 0.0;
    float Inner = 1.0 - (Fibre + 0.5) / Rings;                                    // brighter near the sphere, fading outward
    Tint = mix(uTone0, uTone1, Inner);
    Tint = mix(Tint, uTone2, To * To * 0.7);
    Width = 0.0055 * uSize;
    Glow = (0.3 + 0.9 * Inner * Inner) * pow(To, 1.2) * smoothstep(0.0, 0.08, From);
    if (Segment == OrbitSegments - 1.0) { Width *= 1.3; Glow = Glow * 1.4 + 0.08; Tint = uTone2; }
}`;

// 8 · Flora: three particle flowers (glowing petal edges, breathing open, a few motes drifting off), big leaves in the
//     foreground, fern fans behind and floating dust. Depth of field turns the near and far parts into bokeh.
export const Flora = /* glsl */ `
vec3 Tilted(vec3 P, float Angle) { return vec3(P.x, P.y * cos(Angle) - P.z * sin(Angle), P.y * sin(Angle) + P.z * cos(Angle)); }
vec3 PetalShape(float Length, float Open, float S, float T, float Time, float Ripple)
{
    float Wide = Length * 0.95 * pow(max(sin(3.14159 * S), 0.0), 0.55);
    vec3 Local = vec3(S * Length, Length * (Open * S + 0.55 * S * S) + abs(T) * Wide * 0.7, T * Wide);
    Local.y += Ripple * Length * sin(T * 12.0 + S * 7.0 + Time * 1.4);
    return Local;
}
void Track(out vec3 Head, out vec3 Tail, out vec3 Tint, out float Width, out float Glow)
{
    float Index = float(gl_InstanceID), Share = Index / uCount, Time = uClock * uSpeed;
    float Twinkle = 0.8 + 0.2 * sin(uClock * 3.0 + Hash(Index * 0.77) * 40.0);
    // Petals and leaves are a lattice, not a scatter: 17 evenly spaced veins (the outer two are the glowing edges),
    // points beaded evenly along each by the golden ratio.
    float Vein = mod(Index, 17.0), Bead = floor(Index / 17.0);
    float S = fract(Bead * 0.6180339 + 0.05), T = Vein / 16.0 - 0.5;
    Tail = vec3(0.0);
    if (Share < 0.42)                                                                  // flowers
    {
        float Roll = Hash(Bead * 0.119);
        float Which = Roll < 0.5 ? 0.0 : (Roll < 0.75 ? 1.0 : 2.0);
        vec3 Centre = Which < 0.5 ? vec3(0.0, 0.7, 0.0) : (Which < 1.5 ? vec3(-3.4, 1.5, -2.8) : vec3(3.6, 1.0, -1.9));
        float Size = Which < 0.5 ? 1.7 : (Which < 1.5 ? 1.25 : 1.1);
        float Layer = step(Hash(Bead * 0.331), 0.35);
        float Petal = floor(Hash(Bead * 0.457) * 7.0);
        float Open = 0.22 + 0.16 * sin(Time * 0.45 + Which * 2.0) + Layer * 0.55;
        vec3 Local = PetalShape(Size * (1.0 - Layer * 0.38), Open, S, T, Time, 0.05);
        vec3 P = Spin(Local, Petal / 7.0 * Tau + Which * 1.3 + Layer * 0.45);
        if (Hash(Index * 0.613) < 0.04) P = Sphere(Hash(Index * 0.7), Hash(Index * 0.9)) * Size * 0.1 + vec3(0.0, Size * 0.08, 0.0);
        P = Centre + Tilted(P, 0.4);
        float Away = step(Hash(Index * 0.947), 0.03) * fract(Time * 0.11 + Hash(Index * 1.71));
        P += vec3(0.0, Away * 2.8, 0.0) + Flow(P * 0.8, Time) * Away * 0.9 * uTurbulence;
        Head = P; Tail = P - vec3(0.0, 2.8 * 0.11 * uSpeed, 0.0) * uTail * step(0.001, Away);
        float Rim = step(0.49, abs(T));
        Tint = mix(uTone0, uTone1, Rim * 0.7 + S * 0.3);
        Tint = mix(Tint, uTone2, Rim * S * 0.5);
        Width = mix(0.011, 0.016, Rim) * uSize;
        Glow = (0.22 + Rim * 0.6) * Twinkle * (1.0 - Away) + Away * 1.6 * (1.0 - Away);
    }
    else if (Share < 0.6)                                                              // foreground leaves
    {
        float Leaf = floor(Hash(Bead * 0.173) * 5.0);
        vec3 Stalk = vec3(-4.4 + Leaf * 2.2 + (Hash(Leaf * 3.1) - 0.5), -0.7, 2.3 + Hash(Leaf * 5.7) * 1.6);
        vec3 Local = PetalShape(2.8, 0.45 + Hash(Leaf) * 0.4, S, T * 0.9, Time * 0.6, 0.03);
        Head = Stalk + Tilted(Spin(Local, Leaf * 1.9 + 0.8 + sin(Time * 0.3 + Leaf) * 0.06), 0.25);
        Tail = Head;
        float Rim = step(0.49, abs(T));
        Tint = mix(uTone3, uTone0, 0.4 + Rim * 0.6);
        Width = 0.016 * uSize;
        Glow = (0.12 + Rim * 0.45) * Twinkle;
    }
    else if (Share < 0.965)                                                            // fern fans: filaments of 24 segments
    {
        float Local = Index - floor(uCount * 0.6);
        float Strand = floor(Local / 24.0), Segment = mod(Local, 24.0);
        float Fan = mod(Strand, 3.0);
        vec3 Root = Fan < 0.5 ? vec3(-3.8, 2.4, -6.0) : (Fan < 1.5 ? vec3(0.9, 3.3, -7.8) : vec3(4.8, 2.1, -5.6));
        float Rib = floor(Strand / 3.0), Ribs = 84.0;                                    // 84 ribs per fan at even angles; spare instances stay dark
        float Spread = (Rib / (Ribs - 1.0) - 0.5) * 2.3 + sin(Time * 0.4 + Fan) * 0.05;
        float Length = (2.3 - 0.7 * abs(Spread) / 1.15) * (mod(Rib, 2.0) < 0.5 ? 1.0 : 0.88);
        vec3 Along = vec3(sin(Spread), cos(Spread), 0.15 * sin(Spread * 2.0));
        float From = Segment / 24.0, To = (Segment + 1.0) / 24.0;
        float Droop = 0.45 * Length * abs(sin(Spread));
        Head = Root + Along * To * Length - vec3(0.0, Droop * To * To, 0.0);
        Tail = Root + Along * From * Length - vec3(0.0, Droop * From * From, 0.0);
        Ramp = 0.0;
        Tint = mix(uTone3, uTone1, To);
        Width = 0.007 * uSize;
        Glow = (0.18 + 0.6 * To * To) * Twinkle * step(Rib, Ribs - 0.5);
    }
    else                                                                               // dust and bokeh motes
    {
        vec3 Box = vec3(Hash(Index * 0.61), Hash(Index * 0.83), Hash(Index * 1.07));
        vec3 P = vec3(mix(-9.0, 9.0, Box.x), mix(-1.0, 5.0, fract(Box.y + Time * 0.012)), mix(-9.0, 6.0, Box.z));
        Head = P + Flow(P * 0.3, Time * 0.5) * 0.25; Tail = Head;
        Tint = mix(uTone1, uTone2, Hash(Index * 1.9));
        Width = mix(0.008, 0.025, Hash(Index * 2.3)) * uSize;
        Glow = mix(0.5, 3.0, pow(Hash(Index * 2.7), 3.0)) * Twinkle;
    }
}`;

// 9 · Vehicle transition: a jagged electric edge sweeps the car nose to tail, three times per loop: dust twin → painted
//     car, paint A → paint B, painted car → dust blowing away. The real mesh is clipped and recoloured at the same edge
//     (Filament.js patches the paint shaders with SweepJag), the edge glows on the body, thin wisps run ahead of it and
//     prismatic lines run behind it, all on evenly spaced straight lanes. uSweepMode 0 dust · 1 reveal · 2 restyle ·
//     3 dissolve; uFront is the edge's x.
export const SweepJag = /* glsl */ `
float SweepJag(vec3 P, float T)
{
    return 0.16 * sin(P.y * 7.0 + T * 2.0) + 0.08 * sin(P.z * 11.0 - T * 5.0) + 0.05 * sin((P.y + P.z) * 23.0 + T * 13.0) + 0.035 * sin(P.y * 41.0 - P.z * 37.0 + T * 21.0);
}`;

export const Transition = SweepJag + /* glsl */ `
const float SweepLanes = 30.0;                                                       // horizontal lines per metre of height
void Track(out vec3 Head, out vec3 Tail, out vec3 Tint, out float Width, out float Glow)
{
    float Index = float(gl_InstanceID);
    vec3 Normal = aFacing;
    float Gap = aAnchor.x + SweepJag(aAnchor, uClock) - uFront;                     // > 0: the edge has passed this point
    float Age = Gap / max(uFrontSpeed, 1e-3);                                         // seconds since it passed
    float Roll = Hash(Index * 0.377), Pick = Hash(Index * 0.613);
    float Seam = exp(-abs(Gap) * 26.0);
    // Everything sits on evenly spaced horizontal lanes, so the sweep reads as many particles running in straight lines.
    float Across = mix(aAnchor.y, aAnchor.z, smoothstep(0.55, 0.85, abs(Normal.y)));   // height on the flanks, width on roof and bonnet
    float Lane = floor(Across * SweepLanes);
    float OnLane = 1.0 - smoothstep(0.1, 0.16, abs(fract(Across * SweepLanes) - 0.5));
    vec3 Along = normalize(vec3(1.0, 0.0, 0.0) - Normal * Normal.x + vec3(0.0, 1e-4, 0.0));
    vec3 Lifted = aAnchor + Normal * 0.012;
    vec3 Hue = 0.5 + 0.5 * cos(Tau * (fract(Lane * 0.071 + 0.15) + vec3(0.0, 0.33, 0.67)));   // prismatic: one hue per lane
    Head = Lifted; Tail = Lifted;
    Width = 0.009 * uSize;
    Tint = uTone1; Glow = 0.0;
    bool Done = Gap > 0.0;
    float Mode = uSweepMode;
    if (Mode > 0.5 && Roll < 0.03 && Age > 0.0 && Age < 0.9)                          // particles shot straight out ahead of the edge
    {
        float Run = Age * mix(1.6, 2.6, Pick);
        Head = Lifted - vec3(Run, 0.0, 0.0); Tail = Head + vec3(mix(0.12, 0.3, Pick), 0.0, 0.0);
        Tint = mix(uTone2, Hue, 0.3);
        Glow = 1.4 * exp(-Age * 3.5) * OnLane;
        return;
    }
    if (Mode > 0.5 && Roll > 0.985 && abs(Gap) < 0.25)                                // thin electric wisps ahead of the edge
    {
        float Flick = floor(uClock * 12.0);
        vec3 Kink = normalize(vec3(-1.0, (Hash(Index * 1.7 + Flick) - 0.5) * 1.6, (Hash(Index * 2.3 + Flick) - 0.5) * 1.6) + Normal * 0.4);
        Head = Lifted + Kink * mix(0.06, 0.22, Hash(Index * 3.1)); Ramp = 0.0;
        Tint = mix(uTone1, uTone2, 0.5); Width = 0.0035 * uSize;
        Glow = 1.2 * exp(-abs(Gap) * 8.0) * step(0.4, Hash(Index + floor(uClock * 15.0)));
        return;
    }
    if (Mode < 0.5 || (Mode < 1.5 && !Done))                                          // dust twin (before the reveal): dotted lines
    {
        Tint = mix(uTone0, uTone1, 0.3);
        Glow = (0.3 * OnLane + Seam * 2.0);
        return;
    }
    if (!Done)                                                                        // painted car still ahead of the edge
    {
        Tint = mix(uTone1, uTone2, Seam);
        Glow = Seam * 1.8 * OnLane;
        return;
    }
    if (Mode > 2.5)                                                                   // dissolve: the swept body streams away in straight lines
    {
        float Run = Age * Age * mix(0.7, 1.0, Hash(Lane * 0.37)) + Age * 0.9;
        Head = Lifted + vec3(Run, 0.0, 0.0); Tail = Head - vec3(0.1 + Age * 0.2, 0.0, 0.0);
        Tint = mix(uTone2, Hue, smoothstep(0.0, 0.6, Age));
        Glow = (0.15 + 0.6 * exp(-Age * 3.0)) * smoothstep(1.1, 0.2, Age) * OnLane + Seam * 1.2;
        return;
    }
    // Revealed or restyled: prismatic lines run straight along the body, bright just behind the edge, fading to a faint sheen.
    float Slide = fract(uClock * 0.6 + Hash(Index * 5.9)) - 0.5;
    Head = Lifted + Along * (Slide * 0.3 + 0.15); Tail = Lifted + Along * (Slide * 0.3 - 0.15);
    Tint = mix(mix(uTone1, Hue, 0.8), uTone2, Seam);
    float Fresh = exp(-Age * 3.0);
    Glow = 0.75 * Fresh * OnLane + Seam * 2.2;
}`;

// 10 · Fibre wave: a twisting sheet of optical fibres, narrow at one end and fanned out at the other, with light
//      pulses running along it and sparkles crowding the leading edge.
export const FibreWave = /* glsl */ `
const float WaveSegments = 128.0;
vec3 SheetPoint(float V, float U, float Time, float Fibre)
{
    vec3 Centre = vec3(mix(-10.0, 10.0, U), 1.5 + 1.5 * sin(U * 3.1 + Time * 0.31) + 0.6 * sin(U * 6.3 - Time * 0.47), -1.0 + 2.2 * cos(U * 2.3 + Time * 0.23));
    float Width = mix(0.4, 5.2, smoothstep(0.05, 0.92, U));
    float Twist = U * 2.6 + Time * 0.17 + 0.6;
    vec3 Across = vec3(0.0, cos(Twist), sin(Twist)), Out = vec3(0.0, -sin(Twist), cos(Twist));
    float Wander = 0.035 * sin(U * 6.0 + V * 5.0 + Time * 0.8);                        // one smooth ripple shared by neighbours: the spacing stays even
    return Centre + Across * (V - 0.5) * Width + Out * Wander;
}
void Track(out vec3 Head, out vec3 Tail, out vec3 Tint, out float Width, out float Glow)
{
    float Index = float(gl_InstanceID), Time = uClock * uSpeed;
    float Fibre = floor(Index / WaveSegments), Segment = mod(Index, WaveSegments);
    float Fibres = max(1.0, floor(uCount / WaveSegments));
    float V = (Fibre + 0.5) / Fibres;                                                 // evenly spaced across the sheet
    if (Hash(Fibre * 9.3) < 0.02)                                                     // sparkle slot: dots racing along the edge fibres
    {
        float Lane = (floor(pow(Hash(Index * 0.53), 4.0) * Fibres) + 0.5) / Fibres;   // riding exactly on a fibre
        float U = fract(Hash(Index * 0.29) + Time * mix(0.03, 0.09, Hash(Index * 0.71)));
        Head = SheetPoint(Lane, U, Time, Index);
        Tail = SheetPoint(Lane, U - 0.004 * uSpeed, Time, Index);
        if (Hash(Index * 0.91) < 0.06) { Head += (vec3(Hash(Index * 1.1), Hash(Index * 1.3), Hash(Index * 1.9)) - 0.5) * vec3(4.0, 3.0, 3.0); Tail = Head; }
        Tint = mix(uTone1, uTone2, Hash(Index * 2.3));
        Width = mix(0.008, 0.026, pow(Hash(Index * 2.9), 2.0)) * uSize;
        Glow = mix(0.5, 3.0, pow(Hash(Index * 3.7), 3.0)) * (1.0 - Lane * 0.7) * smoothstep(0.0, 0.05, U) * smoothstep(1.0, 0.9, U);
        return;
    }
    float From = Segment / WaveSegments, To = (Segment + 1.0) / WaveSegments;
    Head = SheetPoint(V, To, Time, Fibre);
    Tail = SheetPoint(V, From, Time, Fibre);
    Ramp = 0.0;
    float EdgeLight = exp(-V * 9.0);
    float Pulse = smoothstep(0.9, 1.0, sin(To * 14.0 - Time * 2.6 + floor(Fibre / 8.0) * 1.7)) * step(mod(Fibre, 8.0), 0.5);   // light pulses ride every eighth fibre
    Tint = mix(uTone3, uTone0, 0.65 + 0.35 * step(2.5, mod(Fibre, 4.0)));
    Tint = mix(Tint, uTone1, EdgeLight);
    Tint = mix(Tint, uTone2, Pulse * 0.6 + EdgeLight * 0.3);
    Width = 0.005 * uSize;
    float Strong = mod(Fibre, 4.0) < 0.5 ? 1.6 : 1.0;                                  // every fourth fibre a touch brighter
    Glow = (0.16 * Strong + EdgeLight * 1.1 + Pulse * 0.3) * smoothstep(0.0, 0.07, From) * smoothstep(1.0, 0.9, To);
}`;

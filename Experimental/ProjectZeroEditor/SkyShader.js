//============================================================================================================================================
//                                                              SKYSHADER.JS
//============================================================================================================================================
// 🧩 One sky renderer for the Atmosphere card, the Sun card and the 3D viewport. It works per pixel on a round planet:
//   • Rayleigh (molecules) and Mie (haze) single scattering, raymarched through the air from the ground to the authored top, with
//     exponential densities from the scale heights and a tent-shaped ozone layer at 25 km that absorbs on the way;
//   • sunlight reaching each sample is dimmed by its own march towards the sun, and is cut off where the planet is in the way, so
//     twilight, the red sunset band and the night side come from the march itself;
//   • the ground reflects its albedo under that same sunlight; the sun is a limb-darkened disc with a soft glow; stars fade in where
//     the sky is dark.
// The settings map onto the usual sea-level coefficients (Rayleigh 5.8 / 13.6 / 33.1 per 1,000 km; Mie 4.0; ozone 0.65 / 1.88 / 0.085),
// scaled by the Rayleigh, Mie and Ozone multipliers. The output is tone mapped and in sRGB. A browser preview only: one bounce, no
// multiple scattering, not the native sky.
//
// Every drawing goes through one hidden WebGL2 canvas, then is copied onto the caller's 2D canvas, so cards don't each hold a context.

const Rad = Math.PI / 180;
export const SunAngularRadius = 0.267 * Rad;

// Up is +y, east is +x and north is +z at the observer, as in SkyDome.jsx's Direction().
export const Direction = (Elevation, Azimuth) => [
  Math.sin(Azimuth * Rad) * Math.cos(Elevation * Rad),
  Math.sin(Elevation * Rad),
  Math.cos(Azimuth * Rad) * Math.cos(Elevation * Rad),
];
const Dot = (A, B) => A[0] * B[0] + A[1] * B[1] + A[2] * B[2];
const Cross = (A, B) => [A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0]];
const Unit = (A) => {
  const Length = Math.hypot(...A) || 1;
  return A.map((C) => C / Length);
};
const Hex = (Text, Fallback) => (/^#[\da-f]{6}$/i.test(Text || "") ? [1, 3, 5].map((At) => parseInt(Text.slice(At, At + 2), 16) / 255) : Fallback);

// Blackbody colour, normalised so its brightest channel is 1.
export function KelvinTint(Kelvin = 6500) {
  const T = Math.max(1000, Math.min(40000, Kelvin)) / 100;
  const Red = T <= 66 ? 255 : 329.7 * (T - 60) ** -0.1332;
  const Green = T <= 66 ? 99.47 * Math.log(T) - 161.12 : 288.12 * (T - 60) ** -0.0755;
  const Blue = T >= 66 ? 255 : T <= 19 ? 0 : 138.52 * Math.log(T - 10) - 305.04;
  const Channels = [Red, Green, Blue].map((C) => Math.max(0, Math.min(255, C)));
  const Peak = Math.max(...Channels) || 1;
  return Channels.map((C) => C / Peak);
}

// The authored atmosphere as shader inputs. `Air(Name)` reads the Atmosphere object's values (with defaults).
export function SkyMedium(Air, { Planet = null } = {}) {
  const Read = (Name, Fallback) => {
    const Value = Air(Name);
    return Value == null || Value === "" || !Number.isFinite(+Value) ? Fallback : +Value;
  };
  return {
    Planet: Planet ?? Read("Planet radius", 6371) * 1000,
    Top: Read("Atmosphere", 60000),
    Rayleigh: [5.802e-6, 13.558e-6, 33.1e-6].map((Each) => Each * Read("Rayleigh", 1)),
    RayleighHeight: Math.max(200, Read("Rayleigh Scale H", 8000)),
    Mie: 3.996e-6 * Read("Mie", 1),
    MieHeight: Math.max(50, Read("Mie Scale H", 1200)),
    Lean: Math.max(-0.95, Math.min(0.95, Read("Mie Anisotropy", 0.78))),
    Ozone: [0.65e-6, 1.881e-6, 0.085e-6].map((Each) => (Each * Read("Ozone", 1.2)) / 1.2),
    Tint: Hex(Air("Sky Tint"), [1, 1, 1]),
    Gain: Read("Sky Brightness", 1),
    Ground: Hex(Air("Ground Albedo"), [0.47, 0.43, 0.38]).map((C) => C ** 2.2),
  };
}

// Sunlight left after crossing the air from a point at `Altitude` (m) towards a sun at `Elevation` (°): the same march as the shader,
// on the CPU, for the viewport's light colour and the card readouts.
export function Transmittance(Medium, Elevation, Altitude = 2) {
  const R = Medium.Planet, H = Altitude, Mu = Math.sin(Elevation * Rad);
  const Hit = (Height) => {
    const B = (R + H) * Mu, C = (H - Height) * (2 * R + H + Height);
    const D = B * B - C;
    return D < 0 ? -1 : -B + Math.sqrt(D);
  };
  // Below the horizon the planet blocks the sun (soften over 0.5° for the disc's size).
  const GroundB = (R + H) * Mu, GroundD = GroundB * GroundB - H * (2 * R + H);
  const Blocked = GroundD >= 0 && -GroundB - Math.sqrt(GroundD) > 0;
  const Length = Hit(Medium.Top);
  if (Length <= 0) return [1, 1, 1];
  const Steps = 24, Depth = [0, 0, 0];
  for (let Step = 0; Step < Steps; Step++) {
    const T = ((Step + 0.5) / Steps) * Length;
    const X = T * Math.sqrt(Math.max(0, 1 - Mu * Mu)), Y = R + H + T * Mu;
    const Height = Math.hypot(X, Y) - R;
    const Ozone = Math.max(0, 1 - Math.abs(Height - 25000) / 15000);
    const Air = Math.exp(-Height / Medium.RayleighHeight), Haze = Math.exp(-Height / Medium.MieHeight);
    for (let Channel = 0; Channel < 3; Channel++)
      Depth[Channel] += (Medium.Rayleigh[Channel] * Air + Medium.Mie * 1.11 * Haze + Medium.Ozone[Channel] * Ozone) * (Length / Steps);
  }
  const Fade = Blocked ? Math.max(0, Math.min(1, 1 + Elevation / 0.5)) : 1;
  return Depth.map((Each) => Math.exp(-Each) * Fade);
}

const Vertex = `#version 300 es
in vec2 Corner;
out vec2 Screen;
void main() { Screen = Corner; gl_Position = vec4(Corner, 0.0, 1.0); }`;

// The atmosphere, shared with the viewport's equirectangular bake.
export const SkyGlsl = `
uniform float uPlanet, uTop, uRayleighH, uMieH, uMie, uLean, uGain, uSunPower, uEyeAltitude;
uniform vec3 uRayleigh, uOzone, uTint, uGround, uSun, uSunTint;
const float PI = 3.14159265;
float Jitter = 0.5; // where in each step the sample sits; main() dithers it per pixel to hide banding
// Distance along a ray (from a point at height h whose b = dot(position, direction)) to the sphere at height Height;
// c is written from heights, not squared positions, so it stays exact in 32-bit floats at planet scale.
vec2 SphereHit(float b, float h, float Height) {
  float c = (h - Height) * (2.0 * uPlanet + h + Height);
  float d = b * b - c;
  if (d < 0.0) return vec2(-1.0);
  d = sqrt(d);
  return vec2(-b - d, -b + d);
}
vec3 Densities(float h) {
  return vec3(exp(-h / uRayleighH), exp(-h / uMieH), max(0.0, 1.0 - abs(h - 25000.0) / 15000.0));
}
vec3 Extinction(vec3 n) { return uRayleigh * n.x + vec3(uMie * 1.11) * n.y + uOzone * n.z; }
vec3 SunThrough(vec3 p) {
  float h = length(p) - uPlanet;
  float b = dot(p, uSun);
  vec2 Ground = SphereHit(b, h, 0.0);
  if (Ground.x > 0.0) return vec3(0.0);
  float Length = SphereHit(b, h, uTop).y;
  if (Length <= 0.0) return vec3(1.0);
  float Step = Length / 6.0;
  vec3 Depth = vec3(0.0);
  for (int i = 0; i < 6; i++) {
    vec3 q = p + uSun * (Step * (float(i) + 0.5));
    Depth += Extinction(Densities(length(q) - uPlanet)) * Step;
  }
  return exp(-Depth);
}
// Radiance along a ray from Eye (planet centre at the origin). Through = transmittance to the end; Ground = 1 if it ends on the ground.
vec3 Sky(vec3 Eye, vec3 Ray, bool Warp, out vec3 Through, out float Ground) {
  Through = vec3(1.0);
  Ground = 0.0;
  float b = dot(Eye, Ray);
  vec2 Outer = SphereHit(b, uEyeAltitude, uTop);
  if (Outer.y <= 0.0) return vec3(0.0);
  float Near = max(Outer.x, 0.0), Far = Outer.y;
  vec2 Land = SphereHit(b, uEyeAltitude, 0.0);
  if (Land.x > 0.0) { Far = Land.x; Ground = 1.0; }
  float Mu = dot(Ray, uSun);
  float PhaseR = 3.0 / (16.0 * PI) * (1.0 + Mu * Mu);
  float g = uLean, gg = g * g;
  float PhaseM = 3.0 / (8.0 * PI) * ((1.0 - gg) * (1.0 + Mu * Mu)) / ((2.0 + gg) * pow(max(1e-4, 1.0 + gg - 2.0 * g * Mu), 1.5));
  vec3 Sum = vec3(0.0), Depth = vec3(0.0);
  float Span = Far - Near;
  for (int i = 0; i < 20; i++) {
    // From the ground, samples bunch up near the eye, where the air is dense.
    float x0 = float(i) / 20.0, x1 = float(i + 1) / 20.0;
    float t0 = Warp ? x0 * x0 : x0, t1 = Warp ? x1 * x1 : x1;
    float Step = (t1 - t0) * Span;
    vec3 p = Eye + Ray * (Near + mix(t0, t1, Jitter) * Span);
    vec3 n = Densities(length(p) - uPlanet);
    vec3 e = Extinction(n) * Step;
    vec3 Seen = exp(-(Depth + 0.5 * e));
    Sum += Seen * SunThrough(p) * (uRayleigh * n.x * PhaseR + uMie * n.y * PhaseM) * Step;
    Depth += e;
  }
  Through = exp(-Depth);
  vec3 Colour = Sum * uSunPower * uSunTint;
  if (Ground > 0.5) {
    vec3 p = Eye + Ray * Far;
    vec3 Normal = normalize(p);
    float Lit = max(dot(Normal, uSun), 0.0);
    vec3 Sunlight = SunThrough(p + Normal * 2.0);
    // Skylight on the ground: a share of the zenith sky, fading through twilight.
    vec3 Skylight = uRayleigh * 9000.0 * smoothstep(-0.12, 0.25, dot(Normal, uSun));
    Colour += Through * uGround / PI * (Sunlight * Lit + Skylight) * uSunPower * uSunTint;
  }
  return Colour * uTint * uGain;
}
`;

const Fragment = `#version 300 es
precision highp float;
in vec2 Screen;
out vec4 Pixel;
${SkyGlsl}
uniform vec3 uEye, uForward, uRight, uUp;
uniform float uTanHalf, uAspect, uSpan, uShift, uExposure, uDisc, uGlow, uStars;
uniform int uMode, uWarp;
float Noise(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float Hash(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
vec3 RayOf(vec2 s) {
  if (uMode == 1) {
    float Azimuth = s.x * uSpan * 0.5, Elevation = atan((s.y + uShift) * uTanHalf);
    vec3 Level = normalize(uForward * cos(Azimuth) + uRight * sin(Azimuth));
    return normalize(Level * cos(Elevation) + uUp * sin(Elevation));
  }
  if (uMode == 2) {
    float u = (s.x + 1.0) * 0.5, Theta = PI * (1.0 - (s.y + 1.0) * 0.5);
    return vec3(-cos(2.0 * PI * u) * sin(Theta), cos(Theta), sin(2.0 * PI * u) * sin(Theta));
  }
  return normalize(uForward + uRight * s.x * uTanHalf * uAspect + uUp * s.y * uTanHalf);
}
void main() {
  vec3 Ray = RayOf(Screen);
  // From orbit the long, even steps band; dither them. From the ground the bunched steps are smooth already.
  if (uWarp == 0) Jitter = Noise(gl_FragCoord.xy);
  vec3 Through; float Ground;
  vec3 Colour = Sky(uEye, Ray, uWarp == 1, Through, Ground);
  float Mu = dot(Ray, uSun), Angle = acos(clamp(Mu, -1.0, 1.0));
  vec3 SunSeen = SunThrough(uEye) * uSunTint;
  if (uDisc > 0.0 && Ground < 0.5 && Angle < uDisc) {
    float r = Angle / uDisc;
    Colour += SunSeen * Through * (1.0 - 0.62 * (1.0 - sqrt(max(0.0, 1.0 - r * r)))) * 400.0 * uSunPower / 22.0;
  }
  if (uGlow > 0.0) {
    float Halo = exp(-Angle / max(uDisc * 2.2, 0.004)) * 3.0 + exp(-Angle / 0.12) * 0.12;
    Colour += SunSeen * uGlow * Halo * (Ground > 0.5 ? 0.35 : 1.0) * uSunPower / 22.0;
  }
  if (uStars > 0.0 && Ground < 0.5) {
    vec3 Cell = floor(Ray * 320.0);
    float Star = step(0.9975, Hash(Cell)) * Hash(Cell + 7.0);
    float Bright = dot(Colour, vec3(0.2126, 0.7152, 0.0722)) * uExposure;
    Colour += vec3(Star) * Through * uStars * 0.6 * exp(-Bright * 40.0);
  }
  vec3 Mapped = 1.0 - exp(-Colour * uExposure);
  Mapped = pow(Mapped, vec3(1.0 / 2.2));
  Mapped += (Noise(gl_FragCoord.yx + 17.0) - 0.5) / 255.0;
  Pixel = vec4(Mapped, 1.0);
}`;

let Painter = null;
function PainterOf() {
  if (Painter !== null) return Painter || null;
  const Canvas = document.createElement("canvas");
  const Gl = Canvas.getContext("webgl2", { antialias: false, preserveDrawingBuffer: true, premultipliedAlpha: false });
  if (!Gl) return (Painter = false), null;
  const Compile = (Type, Source) => {
    const Shader = Gl.createShader(Type);
    Gl.shaderSource(Shader, Source);
    Gl.compileShader(Shader);
    if (!Gl.getShaderParameter(Shader, Gl.COMPILE_STATUS)) throw new Error(Gl.getShaderInfoLog(Shader));
    return Shader;
  };
  const Program = Gl.createProgram();
  Gl.attachShader(Program, Compile(Gl.VERTEX_SHADER, Vertex));
  Gl.attachShader(Program, Compile(Gl.FRAGMENT_SHADER, Fragment));
  Gl.linkProgram(Program);
  if (!Gl.getProgramParameter(Program, Gl.LINK_STATUS)) throw new Error(Gl.getProgramInfoLog(Program));
  const Buffer = Gl.createBuffer();
  Gl.bindBuffer(Gl.ARRAY_BUFFER, Buffer);
  Gl.bufferData(Gl.ARRAY_BUFFER, new Float32Array([-1, -1, 1, -1, -1, 1, 1, 1]), Gl.STATIC_DRAW);
  const Corner = Gl.getAttribLocation(Program, "Corner");
  Gl.enableVertexAttribArray(Corner);
  Gl.vertexAttribPointer(Corner, 2, Gl.FLOAT, false, 0, 0);
  const Places = {};
  const Place = (Name) => (Places[Name] ??= Gl.getUniformLocation(Program, Name));
  Painter = { Canvas, Gl, Program, Place };
  return Painter;
}

// A view of the sky. Eye is in metres from the planet centre's frame at the observer (y up); Forward/Up give the heading.
//   Mode "perspective" (TanHalf = tan of half the vertical field), "panorama" (cylindrical, Span radians wide; Shift lifts the
//   horizon off the middle), "equirect" (for the viewport's sky sphere).
export function ViewBasis(Forward, Up = [0, 1, 0]) {
  const F = Unit(Forward), R = Unit(Cross(F, Up)), U = Cross(R, F);
  return { Forward: F, Right: R, Up: U };
}

// Where a direction lands on a view, as fractions of its width and height (0,0 top left), or null if behind or off the panorama.
export function ProjectDirection(View, Ray, Aspect) {
  const { Forward: F, Right: R, Up: U } = View.Basis;
  if (View.Mode === "panorama") {
    const Level = Unit([Ray[0], 0, Ray[2]]), Ahead = Unit([F[0], 0, F[2]]), Side = Unit([R[0], 0, R[2]]);
    const Azimuth = Math.atan2(Dot(Level, Side), Dot(Level, Ahead));
    const Elevation = Math.asin(Math.max(-1, Math.min(1, Ray[1])));
    return [0.5 + Azimuth / View.Span, 0.5 - (Math.tan(Elevation) / View.TanHalf - (View.Shift || 0)) / 2];
  }
  const Ahead = Dot(Ray, F);
  if (Ahead <= 1e-6) return null;
  return [0.5 + Dot(Ray, R) / Ahead / (View.TanHalf * Aspect) / 2, 0.5 - Dot(Ray, U) / Ahead / View.TanHalf / 2];
}
// The ray through a point of a perspective view (fractions of width and height).
export function RayThrough(View, X, Y, Aspect) {
  const { Forward: F, Right: R, Up: U } = View.Basis;
  const Sx = (X * 2 - 1) * View.TanHalf * Aspect, Sy = (1 - Y * 2) * View.TanHalf;
  return Unit([0, 1, 2].map((Axis) => F[Axis] + R[Axis] * Sx + U[Axis] * Sy));
}

// Draws a view onto a 2D canvas at its current pixel size. Returns false when WebGL2 is unavailable.
export function PaintSky(Target, Medium, Light, View) {
  const Found = PainterOf();
  if (!Found || !Target.width || !Target.height) return false;
  const { Canvas, Gl, Program, Place } = Found;
  if (Canvas.width !== Target.width || Canvas.height !== Target.height) {
    Canvas.width = Target.width;
    Canvas.height = Target.height;
  }
  Gl.viewport(0, 0, Canvas.width, Canvas.height);
  Gl.useProgram(Program);
  const Planet = Medium.Planet, Altitude = View.Altitude ?? 2;
  const Eye = View.Eye || [0, Planet + Altitude, 0];
  const Set1 = (Name, Value) => Gl.uniform1f(Place(Name), Value);
  const Set3 = (Name, Value) => Gl.uniform3fv(Place(Name), Value);
  Set1("uPlanet", Planet);
  Set1("uTop", Medium.Top);
  Set1("uRayleighH", Medium.RayleighHeight);
  Set1("uMieH", Medium.MieHeight);
  Set1("uMie", Medium.Mie);
  Set1("uLean", Medium.Lean);
  Set1("uGain", Medium.Gain);
  Set3("uRayleigh", Medium.Rayleigh);
  Set3("uOzone", Medium.Ozone);
  Set3("uTint", Medium.Tint);
  Set3("uGround", Medium.Ground);
  Set3("uSun", Unit(Light.Sun));
  Set3("uSunTint", Light.Tint || [1, 1, 1]);
  Set1("uSunPower", Light.Power ?? 22);
  Set1("uEyeAltitude", Math.hypot(...Eye) - Planet);
  Set3("uEye", Eye);
  Set3("uForward", View.Basis.Forward);
  Set3("uRight", View.Basis.Right);
  Set3("uUp", View.Basis.Up);
  Set1("uTanHalf", View.TanHalf ?? Math.tan(25 * Rad));
  Set1("uAspect", Target.width / Target.height);
  Set1("uSpan", View.Span ?? Math.PI);
  Set1("uShift", View.Shift ?? 0);
  Set1("uExposure", View.Exposure ?? 1.6);
  Set1("uDisc", View.Disc ?? SunAngularRadius);
  Set1("uGlow", View.Glow ?? 1);
  Set1("uStars", View.Stars ?? 1);
  Gl.uniform1i(Place("uMode"), { perspective: 0, panorama: 1, equirect: 2 }[View.Mode || "perspective"]);
  Gl.uniform1i(Place("uWarp"), View.Warp === false ? 0 : 1);
  Gl.drawArrays(Gl.TRIANGLE_STRIP, 0, 4);
  const Brush = Target.getContext("2d");
  Brush.clearRect(0, 0, Target.width, Target.height);
  Brush.drawImage(Canvas, 0, 0);
  return true;
}

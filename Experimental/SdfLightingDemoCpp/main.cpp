//==================================================================================================
// Standalone C++20 SDF Lighting, SDF Soft Shadows, Emissive LTC/Geometry Lighting, & SDF Cone GI Demo
// Zero external dependencies — multi-threaded CPU sphere-tracer writing PPM images.
//==================================================================================================
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr float PI = 3.14159265358979323846f;

struct Vec3 {
  float x = 0.0f, y = 0.0f, z = 0.0f;
  constexpr Vec3() = default;
  constexpr Vec3(float s) : x(s), y(s), z(s) {}
  constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
  constexpr Vec3 operator+(const Vec3& b) const { return {x + b.x, y + b.y, z + b.z}; }
  constexpr Vec3 operator-(const Vec3& b) const { return {x - b.x, y - b.y, z - b.z}; }
  constexpr Vec3 operator-() const { return {-x, -y, -z}; }
  constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  constexpr Vec3 operator*(const Vec3& b) const { return {x * b.x, y * b.y, z * b.z}; }
  constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
  Vec3& operator+=(const Vec3& b) { x += b.x; y += b.y; z += b.z; return *this; }
  Vec3& operator*=(const Vec3& b) { x *= b.x; y *= b.y; z *= b.z; return *this; }
  Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
};

inline Vec3 operator*(float s, const Vec3& v) { return {v.x * s, v.y * s, v.z * s}; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(const Vec3& v) {
  float l = length(v);
  return l > 1e-6f ? v / l : Vec3(0, 0, 1);
}
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
inline Vec3 clamp3(const Vec3& v, float lo, float hi) {
  return {clampf(v.x, lo, hi), clampf(v.y, lo, hi), clampf(v.z, lo, hi)};
}
inline float mixf(float a, float b, float t) { return a + (b - a) * t; }
inline Vec3 mix3(const Vec3& a, const Vec3& b, float t) { return a + (b - a) * t; }
inline float smoothstep(float e0, float e1, float x) {
  float t = clampf((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}
inline float fractf(float x) { return x - std::floor(x); }
inline Vec3 reflect3(const Vec3& i, const Vec3& n) { return i - 2.0f * dot(i, n) * n; }
inline Vec3 abs3(const Vec3& v) { return {std::abs(v.x), std::abs(v.y), std::abs(v.z)}; }
inline Vec3 max3(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }

inline float ignHash(float px, float py) {
  return fractf(52.9829189f * fractf(0.06711056f * px + 0.00583715f * py));
}

inline void buildBasis(const Vec3& n, Vec3& t, Vec3& b) {
  Vec3 up = std::abs(n.z) < 0.999f ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
  t = normalize(cross(up, n));
  b = cross(n, t);
}

//--------------------------------------------------------------------------------------------------
// Analytical SDF Primitives + Smooth Union
//--------------------------------------------------------------------------------------------------
inline float sdSphere(const Vec3& p, float r) { return length(p) - r; }

inline float sdBox(const Vec3& p, const Vec3& b) {
  Vec3 q = abs3(p) - b;
  return length(max3(q, Vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
}

inline float sdRoundBox(const Vec3& p, const Vec3& b, float r) {
  Vec3 q = abs3(p) - b + Vec3(r);
  return length(max3(q, Vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f) - r;
}

inline float sdTorus(const Vec3& p, float majorR, float minorR) {
  float qx = std::sqrt(p.x * p.x + p.y * p.y) - majorR;
  return std::sqrt(qx * qx + p.z * p.z) - minorR;
}

inline float sdCapsule(const Vec3& p, const Vec3& a, const Vec3& b, float r) {
  Vec3 pa = p - a, ba = b - a;
  float h = clampf(dot(pa, ba) / std::max(1e-6f, dot(ba, ba)), 0.0f, 1.0f);
  return length(pa - ba * h) - r;
}

inline float opSmoothUnion(float d1, float d2, float k) {
  float h = clampf(0.5f + 0.5f * (d2 - d1) / k, 0.0f, 1.0f);
  return mixf(d2, d1, h) - k * h * (1.0f - h);
}

//--------------------------------------------------------------------------------------------------
// Scene Configuration & Materials
//--------------------------------------------------------------------------------------------------
enum MatId : int {
  MAT_NONE = -1,
  MAT_FLOOR = 0,
  MAT_WALL_BACK = 1,
  MAT_WALL_CRIMSON = 2,
  MAT_WALL_TEAL = 3,
  MAT_ROOF = 4,
  MAT_GOLD_SPHERE = 5,
  MAT_CHROME_SPHERE = 6,
  MAT_CERAMIC_SCULPT = 7,
  MAT_OCCLUDER_PILLAR = 8,
  MAT_EMISSIVE_ORB = 9,
  MAT_EMISSIVE_NEON_BAR = 10,
  MAT_EMISSIVE_PANEL = 11,
};

struct SurfaceMat {
  Vec3 baseColor{0.8f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  Vec3 emission{0.0f};
  bool isEmissive = false;
};

struct AreaPanel {
  bool enabled = false;
  Vec3 center{0.0f, 0.25f, 2.05f};
  Vec3 right{1.0f, 0.0f, 0.0f};
  Vec3 up{0.0f, 1.0f, 0.0f};
  Vec3 normal{0.0f, 0.0f, -1.0f};
  float hw = 0.72f;
  float hh = 0.50f;
  Vec3 radiance{14.0f, 12.2f, 9.8f};
};

struct SceneCfg {
  std::string name;
  bool sunEnabled = false;
  Vec3 sunDir = normalize(Vec3(0.45f, -0.52f, 0.72f));
  Vec3 sunIrradiance{4.2f, 3.85f, 3.35f};

  bool moonEnabled = false;
  Vec3 moonDir = normalize(Vec3(-0.35f, -0.58f, 0.74f));
  Vec3 moonIrradiance{0.24f, 0.28f, 0.38f};

  bool starsEnabled = false;
  float starIntensity = 1.0f;
  Vec3 skyZenith{0.24f, 0.48f, 0.92f};
  Vec3 skyHorizon{0.58f, 0.74f, 0.95f};

  bool emissiveGeometryEnabled = true;
  Vec3 emissiveOrbPos{-1.65f, 0.15f, 0.32f};
  float emissiveOrbRadius = 0.30f;
  Vec3 emissiveOrbRad{15.0f, 6.2f, 1.5f}; // Intense warm amber-orange emissive orb

  Vec3 neonA{1.45f, 1.45f, -0.15f};
  Vec3 neonB{1.45f, 1.45f, 1.25f};
  float neonRadius = 0.075f;
  Vec3 neonRad{2.2f, 10.5f, 14.5f}; // Cyan-blue emissive neon tube

  AreaPanel panel;
  float exposure = 1.05f;
};

struct SdfHit {
  float d = 1e6f;
  int mat = MAT_NONE;
};

inline SdfHit minHit(const SdfHit& a, const SdfHit& b) {
  return (a.d < b.d) ? a : b;
}

// Map scene SDF; skipEmissive omits emissive objects during emissive shadow rays
SdfHit mapSceneSdf(const Vec3& p, const SceneCfg& cfg, bool includeEmissive = true, int skipMat = MAT_NONE) {
  SdfHit res{1e6f, MAT_NONE};

  // Floor at z = -0.80
  res = minHit(res, {p.z + 0.80f, MAT_FLOOR});
  // Back wall at y = 2.75 (up to z = 2.25 so sky, moon, stars, and sunlight enter cleanly)
  float backWall = sdBox(p - Vec3(0.0f, 2.75f, 0.725f), Vec3(2.90f, 0.10f, 1.525f));
  res = minHit(res, {backWall, MAT_WALL_BACK});
  // Bounded Crimson left wall (x = -2.85, y in [-2.2, 2.75], z in [-0.8, 2.70])
  float leftWall = sdBox(p - Vec3(-2.85f, 0.275f, 0.95f), Vec3(0.10f, 2.475f, 1.75f));
  res = minHit(res, {leftWall, MAT_WALL_CRIMSON});
  // Bounded Teal right wall (x = +2.85, y in [-2.2, 2.75], z in [-0.8, 2.70])
  float rightWall = sdBox(p - Vec3(2.85f, 0.275f, 0.95f), Vec3(0.10f, 2.475f, 1.75f));
  res = minHit(res, {rightWall, MAT_WALL_TEAL});

  // Architectural front-side roof soffits (left/right frames, wide open central sky & skylight)
  float roofLeft  = sdBox(p - Vec3(-2.40f, 0.25f, 2.70f), Vec3(0.55f, 2.45f, 0.12f));
  float roofRight = sdBox(p - Vec3( 2.40f, 0.25f, 2.70f), Vec3(0.55f, 2.45f, 0.12f));
  float roofD = std::min(roofLeft, roofRight);
  res = minHit(res, {roofD, MAT_ROOF});

  // Pedestal + Ceramic CSG sculpture (smooth union of rounded base + torus ring)
  float ped = sdRoundBox(p - Vec3(0.05f, 0.65f, -0.45f), Vec3(0.55f, 0.45f, 0.35f), 0.06f);
  Vec3 tp = p - Vec3(0.05f, 0.65f, 0.36f);
  // Tilted torus
  float cs = 0.866f, sn = 0.50f;
  Vec3 tRot(tp.x, tp.y * cs - tp.z * sn, tp.y * sn + tp.z * cs);
  float tor = sdTorus(tRot, 0.40f, 0.12f);
  float sculpt = opSmoothUnion(ped, tor, 0.14f);
  res = minHit(res, {sculpt, MAT_CERAMIC_SCULPT});

  // Gold metallic sphere (left-front)
  res = minHit(res, {sdSphere(p - Vec3(-0.78f, -0.35f, -0.34f), 0.46f), MAT_GOLD_SPHERE});

  // Chrome mirror sphere (right-front)
  res = minHit(res, {sdSphere(p - Vec3(1.12f, -0.22f, -0.32f), 0.48f), MAT_CHROME_SPHERE});

  // Shadow-casting occluder pillar between emissive orb (-1.65, 0.15, 0.32) and center floor/pedestal
  if (skipMat != MAT_OCCLUDER_PILLAR) {
    float pillar = sdRoundBox(p - Vec3(-0.92f, 0.25f, -0.08f), Vec3(0.11f, 0.38f, 0.72f), 0.03f);
    res = minHit(res, {pillar, MAT_OCCLUDER_PILLAR});
  }

  // Emissive SDF Geometry
  if (includeEmissive) {
    if (cfg.emissiveGeometryEnabled) {
      if (skipMat != MAT_EMISSIVE_ORB) {
        res = minHit(res, {sdSphere(p - cfg.emissiveOrbPos, cfg.emissiveOrbRadius), MAT_EMISSIVE_ORB});
      }
      if (skipMat != MAT_EMISSIVE_NEON_BAR) {
        res = minHit(res, {sdCapsule(p, cfg.neonA, cfg.neonB, cfg.neonRadius), MAT_EMISSIVE_NEON_BAR});
      }
    }
    if (cfg.panel.enabled && skipMat != MAT_EMISSIVE_PANEL) {
      float pd = sdBox(p - cfg.panel.center, Vec3(cfg.panel.hw, cfg.panel.hh, 0.04f));
      res = minHit(res, {pd, MAT_EMISSIVE_PANEL});
    }
  }
  return res;
}

Vec3 calcSdfNormal(const Vec3& p, const SceneCfg& cfg, bool includeEmissive = true) {
  constexpr float h = 0.0012f;
  const Vec3 k0( 1, -1, -1);
  const Vec3 k1(-1, -1,  1);
  const Vec3 k2(-1,  1, -1);
  const Vec3 k3( 1,  1,  1);
  return normalize(
    k0 * mapSceneSdf(p + k0 * h, cfg, includeEmissive).d +
    k1 * mapSceneSdf(p + k1 * h, cfg, includeEmissive).d +
    k2 * mapSceneSdf(p + k2 * h, cfg, includeEmissive).d +
    k3 * mapSceneSdf(p + k3 * h, cfg, includeEmissive).d
  );
}

SurfaceMat getMaterial(int matId, const Vec3& p, const SceneCfg& cfg) {
  SurfaceMat m;
  switch (matId) {
    case MAT_FLOOR: {
      int cx = static_cast<int>(std::floor(p.x * 1.25f));
      int cy = static_cast<int>(std::floor(p.y * 1.25f));
      bool chk = ((cx + cy) & 1) == 0;
      m.baseColor = chk ? Vec3(0.56f, 0.56f, 0.58f) : Vec3(0.42f, 0.43f, 0.45f);
      m.roughness = chk ? 0.24f : 0.34f;
      m.metallic = 0.04f;
      break;
    }
    case MAT_WALL_BACK:
      m.baseColor = Vec3(0.74f, 0.73f, 0.71f);
      m.roughness = 0.72f;
      break;
    case MAT_WALL_CRIMSON:
      m.baseColor = Vec3(0.72f, 0.11f, 0.09f);
      m.roughness = 0.76f;
      break;
    case MAT_WALL_TEAL:
      m.baseColor = Vec3(0.10f, 0.58f, 0.46f);
      m.roughness = 0.76f;
      break;
    case MAT_ROOF:
      m.baseColor = Vec3(0.28f, 0.29f, 0.32f);
      m.roughness = 0.65f;
      break;
    case MAT_GOLD_SPHERE:
      m.baseColor = Vec3(0.95f, 0.72f, 0.28f);
      m.metallic = 0.94f;
      m.roughness = 0.14f;
      break;
    case MAT_CHROME_SPHERE:
      m.baseColor = Vec3(0.88f, 0.90f, 0.94f);
      m.metallic = 0.97f;
      m.roughness = 0.06f;
      break;
    case MAT_CERAMIC_SCULPT:
      m.baseColor = Vec3(0.86f, 0.85f, 0.83f);
      m.metallic = 0.0f;
      m.roughness = 0.28f;
      break;
    case MAT_OCCLUDER_PILLAR:
      m.baseColor = Vec3(0.62f, 0.63f, 0.65f);
      m.roughness = 0.68f;
      break;
    case MAT_EMISSIVE_ORB:
      m.baseColor = Vec3(1.0f, 0.62f, 0.24f);
      m.emission = cfg.emissiveOrbRad;
      m.isEmissive = true;
      break;
    case MAT_EMISSIVE_NEON_BAR:
      m.baseColor = Vec3(0.32f, 0.86f, 1.0f);
      m.emission = cfg.neonRad;
      m.isEmissive = true;
      break;
    case MAT_EMISSIVE_PANEL:
      m.baseColor = Vec3(1.0f, 0.94f, 0.82f);
      m.emission = cfg.panel.radiance;
      m.isEmissive = true;
      break;
    default:
      break;
  }
  return m;
}

//--------------------------------------------------------------------------------------------------
// Sphere Trace SDF
//--------------------------------------------------------------------------------------------------
bool traceSdf(const Vec3& ro, const Vec3& rd, float tMin, float tMax, const SceneCfg& cfg,
              bool includeEmissive, float& outT, int& outMat, int maxSteps = 84) {
  float t = tMin;
  for (int i = 0; i < maxSteps; ++i) {
    SdfHit h = mapSceneSdf(ro + rd * t, cfg, includeEmissive);
    if (h.d < 0.0008f * (1.0f + t * 0.25f)) {
      outT = t;
      outMat = h.mat;
      return true;
    }
    t += h.d;
    if (t > tMax) break;
  }
  outT = tMax;
  outMat = MAT_NONE;
  return false;
}

//--------------------------------------------------------------------------------------------------
// Improved Analytical SDF Soft Shadow (IQ penumbra: sharp contact -> smooth distance penumbra)
//--------------------------------------------------------------------------------------------------
float calcSdfSoftShadow(const Vec3& ro, const Vec3& rd, float mint, float maxt, float w,
                        const SceneCfg& cfg, int skipMat = MAT_NONE) {
  float res = 1.0f;
  float ph = 1e20f;
  float t = mint;
  for (int i = 0; i < 48; ++i) {
    float h = mapSceneSdf(ro + rd * t, cfg, false, skipMat).d;
    if (h < 0.0006f) return 0.0f;
    float y = (h * h) / (2.0f * ph);
    float d = std::sqrt(std::max(0.0f, h * h - y * y));
    res = std::min(res, d / (w * std::max(1e-4f, t - y)));
    ph = h;
    t += std::max(h, 0.018f);
    if (res < 0.004f || t >= maxt) break;
  }
  res = clampf(res, 0.0f, 1.0f);
  return res * res * (3.0f - 2.0f * res);
}

//--------------------------------------------------------------------------------------------------
// Sky + Moon + Starlight
//--------------------------------------------------------------------------------------------------
Vec3 evalSky(const Vec3& rd, const SceneCfg& cfg, bool primaryRay) {
  float z = clampf(rd.z, 0.0f, 1.0f);
  Vec3 sky = mix3(cfg.skyHorizon, cfg.skyZenith, std::pow(z, 0.55f));

  if (cfg.sunEnabled && rd.z > -0.02f) {
    float cs = dot(rd, cfg.sunDir);
    if (cs > 0.75f) {
      float disc = smoothstep(0.9992f, 0.99985f, cs);
      float glow = std::pow(std::max(0.0f, cs), 120.0f) * 0.35f;
      sky += cfg.sunIrradiance * (disc * 3.5f + glow);
    }
  }
  if (cfg.moonEnabled && rd.z > -0.02f) {
    float cm = dot(rd, cfg.moonDir);
    float glow = std::pow(std::max(0.0f, cm), 280.0f) * 0.45f;
    float disc = smoothstep(0.9990f, 0.9996f, cm);
    sky += Vec3(0.90f, 0.93f, 1.0f) * (disc * 2.4f + glow);
  }
  if (cfg.starsEnabled && primaryRay && rd.z > 0.02f) {
    Vec3 g = rd * 145.0f;
    Vec3 c(std::floor(g.x), std::floor(g.y), std::floor(g.z));
    Vec3 f = g - c;
    float h = fractf(std::sin(dot(c, Vec3(127.1f, 311.7f, 74.7f))) * 43758.5453f);
    if (h > 0.972f) {
      float d = length(f - Vec3(0.5f));
      float star = smoothstep(0.32f, 0.04f, d) * ((h - 0.972f) / 0.028f);
      sky += Vec3(0.80f, 0.88f, 1.0f) * star * 1.4f * cfg.starIntensity * smoothstep(0.02f, 0.18f, rd.z);
    }
  }
  return sky;
}

Vec3 evalStarlightAmbient(const Vec3& n, const SceneCfg& cfg) {
  if (!cfg.starsEnabled) return Vec3(0.0f);
  float hemi = 0.35f + 0.65f * clampf(n.z * 0.5f + 0.5f, 0.0f, 1.0f);
  return Vec3(0.0045f, 0.0062f, 0.0110f) * cfg.starIntensity * hemi;
}

//--------------------------------------------------------------------------------------------------
// Cook-Torrance GGX Direct BRDF
//--------------------------------------------------------------------------------------------------
Vec3 evalPbrDirect(const Vec3& n, const Vec3& v, const Vec3& l, const SurfaceMat& mat) {
  float ndl = std::max(0.0f, dot(n, l));
  if (ndl <= 0.0f) return Vec3(0.0f);
  float ndv = std::max(1e-4f, dot(n, v));
  Vec3 h = normalize(v + l);
  float ndh = std::max(0.0f, dot(n, h));
  float vdh = std::max(0.0f, dot(v, h));

  float r = clampf(mat.roughness, 0.04f, 1.0f);
  float a = r * r;
  float a2 = a * a;
  float denom = ndh * ndh * (a2 - 1.0f) + 1.0f;
  float d = a2 / std::max(1e-6f, PI * denom * denom);

  float k = (r + 1.0f) * (r + 1.0f) * 0.125f;
  float g1 = ndv / (ndv * (1.0f - k) + k);
  float g2 = ndl / (ndl * (1.0f - k) + k);
  float g = g1 * g2;

  Vec3 f0 = mix3(Vec3(0.04f), mat.baseColor, mat.metallic);
  Vec3 f = f0 + (Vec3(1.0f) - f0) * std::pow(clampf(1.0f - vdh, 0.0f, 1.0f), 5.0f);
  Vec3 kd = (Vec3(1.0f) - f) * (1.0f - mat.metallic);
  Vec3 spec = (f * d * g) / std::max(1e-4f, 4.0f * ndv * ndl);
  return (kd * mat.baseColor / PI + spec) * ndl;
}

//--------------------------------------------------------------------------------------------------
// Clipped-Polygon Linearly Transformed Cosine (LTC) Emissive Panel (from ShadowAlgorithmLab)
//--------------------------------------------------------------------------------------------------
float integrateSphericalEdgeZ(const Vec3& v1, const Vec3& v2) {
  float x = clampf(dot(v1, v2), -0.99999f, 0.99999f);
  float absX = std::abs(x);
  float a = 5.42031f + (3.12829f + 0.0902326f * absX) * absX;
  float b = 3.45068f + (4.18814f + absX) * absX;
  float thetaOverSin = a / b;
  if (x < 0.0f) {
    float sinTheta = std::sqrt(std::max(1e-8f, 1.0f - x * x));
    thetaOverSin = PI / sinTheta - thetaOverSin;
  }
  return (v1.x * v2.y - v1.y * v2.x) * thetaOverSin;
}

float integrateClippedQuadFormFactor(const Vec3 localQ[4]) {
  Vec3 clipped[8];
  int clipCount = 0;
  for (int i = 0; i < 4; ++i) {
    Vec3 cur = localQ[i];
    Vec3 next = localQ[(i + 1) & 3];
    bool curA = (cur.z >= 0.0f);
    bool nxtA = (next.z >= 0.0f);
    if (curA) clipped[clipCount++] = cur;
    if (curA != nxtA) {
      float alpha = cur.z / (cur.z - next.z);
      Vec3 hit = cur + (next - cur) * alpha;
      hit.z = 0.0f;
      clipped[clipCount++] = hit;
    }
  }
  if (clipCount < 3) return 0.0f;
  for (int i = 0; i < clipCount; ++i) clipped[i] = normalize(clipped[i]);
  float contourSum = 0.0f;
  for (int i = 0; i < clipCount; ++i) {
    int nxt = (i + 1 >= clipCount) ? 0 : (i + 1);
    contourSum += integrateSphericalEdgeZ(clipped[i], clipped[nxt]);
  }
  return clampf(std::abs(contourSum) / (2.0f * PI), 0.0f, 1.0f);
}

Vec3 evalEmissivePanelLtc(const Vec3& pos, const Vec3& n, const Vec3& v, const SurfaceMat& mat,
                          const AreaPanel& panel, float vis) {
  if (!panel.enabled || vis <= 1e-4f) return Vec3(0.0f);
  if (dot(pos - panel.center, panel.normal) <= 0.02f) return Vec3(0.0f);

  float noV = clampf(dot(n, v), 1e-4f, 1.0f);
  Vec3 t1 = v - n * noV;
  float t1Len = dot(t1, t1);
  if (t1Len <= 1e-6f) {
    Vec3 fb;
    buildBasis(n, t1, fb);
  } else {
    t1 = t1 * (1.0f / std::sqrt(t1Len));
  }
  Vec3 t2 = normalize(cross(n, t1));

  Vec3 halfU = panel.right * panel.hw;
  Vec3 halfV = panel.up * panel.hh;
  Vec3 wc[4] = {
    panel.center - halfU - halfV - pos,
    panel.center + halfU - halfV - pos,
    panel.center + halfU + halfV - pos,
    panel.center - halfU + halfV - pos,
  };
  Vec3 diffLocal[4];
  for (int i = 0; i < 4; ++i) diffLocal[i] = {dot(wc[i], t1), dot(wc[i], t2), dot(wc[i], n)};
  float diffFF = integrateClippedQuadFormFactor(diffLocal);

  float r = std::max(mat.roughness, 0.04f);
  float alpha = clampf(r * r, 0.004f, 1.0f);
  float sinV = std::sqrt(std::max(0.0f, 1.0f - noV * noV));
  float invScaleX = 1.0f / std::max(0.04f, alpha * (0.85f + 0.15f * noV));
  float invScaleY = 1.0f / std::max(0.04f, alpha);
  float shearXZ = -sinV / std::max(0.08f, noV + alpha);

  Vec3 specLocal[4];
  for (int i = 0; i < 4; ++i) {
    const Vec3& d = diffLocal[i];
    float mx = -d.x * noV + d.z * sinV;
    float mz =  d.x * sinV + d.z * noV;
    specLocal[i] = {(mx + shearXZ * mz * alpha) * invScaleX, d.y * invScaleY, mz};
  }
  float specFF = integrateClippedQuadFormFactor(specLocal);

  Vec3 diffAlb = mat.baseColor * (1.0f - mat.metallic);
  Vec3 f0 = mix3(Vec3(0.04f), mat.baseColor, mat.metallic);
  float schlick = std::pow(std::max(1.0f - noV, 1e-4f), 5.0f);
  Vec3 specAlb = f0 + (Vec3(1.0f) - f0) * (schlick * clampf(1.0f - r * 0.65f, 0.0f, 1.0f));
  return panel.radiance * (diffAlb * diffFF + specAlb * specFF) * vis;
}

//--------------------------------------------------------------------------------------------------
// Direct Lighting from Sun, Moon, Emissive Panel (LTC + SDF Soft Shadow), & Emissive SDF Geometry
//--------------------------------------------------------------------------------------------------
Vec3 evalDirectLighting(const Vec3& pos, const Vec3& n, const Vec3& v, const SurfaceMat& mat,
                        const SceneCfg& cfg, float px, float py, bool shadowPassHighQuality) {
  Vec3 lo(0.0f);

  // 1. Sunlight + SDF Soft Shadow
  if (cfg.sunEnabled && dot(n, cfg.sunDir) > 0.001f) {
    float vis = calcSdfSoftShadow(pos + n * 0.008f, cfg.sunDir, 0.015f, 24.0f, 0.06f, cfg);
    lo += evalPbrDirect(n, v, cfg.sunDir, mat) * cfg.sunIrradiance * vis;
  }

  // 2. Moonlight + SDF Soft Shadow
  if (cfg.moonEnabled && dot(n, cfg.moonDir) > 0.001f) {
    float vis = calcSdfSoftShadow(pos + n * 0.008f, cfg.moonDir, 0.015f, 24.0f, 0.08f, cfg);
    lo += evalPbrDirect(n, v, cfg.moonDir, mat) * cfg.moonIrradiance * vis;
  }

  // 3. Emissive Area Panel (LTC + deterministic 2x2 SDF soft shadows across panel area)
  if (cfg.panel.enabled && dot(pos - cfg.panel.center, cfg.panel.normal) > 0.02f) {
    float visSum = 0.0f;
    if (shadowPassHighQuality) {
      for (int sy = -1; sy <= 1; sy += 2) {
        for (int sx = -1; sx <= 1; sx += 2) {
          Vec3 tgt = cfg.panel.center + cfg.panel.right * (float(sx) * 0.55f * cfg.panel.hw)
                                      + cfg.panel.up    * (float(sy) * 0.55f * cfg.panel.hh);
          Vec3 delta = tgt - (pos + n * 0.012f);
          float dist = length(delta);
          Vec3 dir = delta / std::max(1e-4f, dist);
          visSum += calcSdfSoftShadow(pos + n * 0.012f, dir, 0.02f, std::max(0.05f, dist - 0.14f), 0.14f, cfg);
        }
      }
      visSum *= 0.25f;
    } else {
      Vec3 delta = cfg.panel.center - (pos + n * 0.012f);
      float dist = length(delta);
      Vec3 dir = delta / std::max(1e-4f, dist);
      visSum = calcSdfSoftShadow(pos + n * 0.012f, dir, 0.02f, std::max(0.05f, dist - 0.14f), 0.14f, cfg);
    }
    lo += evalEmissivePanelLtc(pos, n, v, mat, cfg.panel, visSum);
  }

  // 4. Emissive SDF Geometry Lighting the Scene (Sphere Orb + Capsule Neon Bar + SDF Soft Shadows!)
  if (cfg.emissiveGeometryEnabled) {
    // 4A. Glowing Amber Emissive Orb
    {
      Vec3 toOrb = cfg.emissiveOrbPos - pos;
      float dist = length(toOrb);
      Vec3 lDir = toOrb / std::max(1e-4f, dist);
      if (dot(n, lDir) > 0.001f) {
        float r = cfg.emissiveOrbRadius;
        float solidOmega = std::min(2.2f, (PI * r * r) / std::max(0.08f, dist * dist));
        float vis = 1.0f;
        if (shadowPassHighQuality) {
          vis = calcSdfSoftShadow(pos + n * 0.012f, lDir, 0.02f, std::max(0.04f, dist - r - 0.04f), 0.14f, cfg);
        } else {
          float tmpD = 0.0f;
          int tmpMat = 0;
          vis = traceSdf(pos + n * 0.016f, lDir, 0.02f, std::max(0.04f, dist - r - 0.06f), cfg, false, tmpD, tmpMat, 24) ? 0.08f : 1.0f;
        }
        lo += evalPbrDirect(n, v, lDir, mat) * cfg.emissiveOrbRad * (solidOmega * 0.75f) * vis;
      }
    }

    // 4B. Glowing Cyan Neon Capsule Tube (line integration with SDF soft shadows)
    {
      int SEG = shadowPassHighQuality ? 3 : 1;
      for (int s = 0; s < SEG; ++s) {
        float t = (float(s) + 0.5f) / float(SEG);
        Vec3 pt = mix3(cfg.neonA, cfg.neonB, t);
        Vec3 toSeg = pt - pos;
        float dist = length(toSeg);
        Vec3 lDir = toSeg / std::max(1e-4f, dist);
        if (dot(n, lDir) > 0.001f) {
          float segLen = length(cfg.neonB - cfg.neonA) / float(SEG);
          float area = 2.0f * PI * cfg.neonRadius * segLen;
          float solidOmega = std::min(1.6f, area / std::max(0.12f, 4.0f * dist * dist));
          float vis = shadowPassHighQuality
            ? calcSdfSoftShadow(pos + n * 0.012f, lDir, 0.02f, std::max(0.04f, dist - cfg.neonRadius - 0.05f), 0.12f, cfg)
            : 1.0f;
          lo += evalPbrDirect(n, v, lDir, mat) * cfg.neonRad * (solidOmega * 0.85f) * vis;
        }
      }
    }
  }
  return lo;
}

//--------------------------------------------------------------------------------------------------
// Smooth SDF Cone AO + GI (12 IGN-rotated Fibonacci Rays, Non-Emissive Bounce + Bilateral Denoise)
//--------------------------------------------------------------------------------------------------
float evalSdfAo(const Vec3& pos, const Vec3& n, const SceneCfg& cfg) {
  float occ = 0.0f;
  float weight = 1.0f;
  for (int i = 0; i < 6; ++i) {
    float hr = 0.03f + 0.14f * float(i);
    float d = mapSceneSdf(pos + n * hr, cfg, false).d;
    occ += (hr - d) * weight;
    weight *= 0.68f;
  }
  return clampf(1.0f - 1.45f * occ, 0.12f, 1.0f);
}

void evalSdfConeGi(const Vec3& pos, const Vec3& n, const SceneCfg& cfg, float px, float py,
                   Vec3& outGi, float& outAo) {
  outAo = evalSdfAo(pos, n, cfg);
  Vec3 t, b;
  buildBasis(n, t, b);

  constexpr int NUM_RAYS = 12;
  float phi0 = ignHash(px, py) * 2.0f * PI;
  Vec3 accum(0.0f);

  for (int i = 0; i < NUM_RAYS; ++i) {
    float u1 = (float(i) + 0.5f) / float(NUM_RAYS);
    float r = std::sqrt(u1);
    float ang = phi0 + float(i) * 2.39996323f;
    Vec3 dir = normalize(t * (r * std::cos(ang)) + b * (r * std::sin(ang)) + n * std::sqrt(std::max(0.0f, 1.0f - u1)));

    float hitT = 0.0f;
    int hitMat = MAT_NONE;
    // includeEmissive=false: emissive objects already illuminate via exact analytical LTC/solid-angle pass
    if (traceSdf(pos + n * 0.02f, dir, 0.03f, 6.5f, cfg, false, hitT, hitMat, 32)) {
      Vec3 hp = pos + n * 0.02f + dir * hitT;
      Vec3 hn = calcSdfNormal(hp, cfg, false);
      SurfaceMat hm = getMaterial(hitMat, hp, cfg);
      Vec3 direct = evalDirectLighting(hp, hn, -dir, hm, cfg, px, py, false);
      Vec3 skyAmb = evalSky(hn, cfg, false) * 0.22f + evalStarlightAmbient(hn, cfg);
      accum += clamp3(direct + hm.baseColor * skyAmb, 0.0f, 6.5f);
    } else {
      accum += evalSky(dir, cfg, false) * 0.55f + evalStarlightAmbient(n, cfg);
    }
  }
  outGi = (accum / float(NUM_RAYS)) * 1.45f;
}

//--------------------------------------------------------------------------------------------------
// Two-Pass Shading with GI_BILATERAL_FRAG Edge-Preserving Denoise (ShadowAlgorithmLab)
//--------------------------------------------------------------------------------------------------
struct GBufferPixel {
  bool valid = false;
  bool isEmissive = false;
  Vec3 direct{0.0f};
  Vec3 giIrr{0.0f};
  Vec3 specEnv{0.0f};
  Vec3 pos{0.0f};
  Vec3 n{0.0f};
  Vec3 fEnv{0.0f};
  Vec3 baseColor{0.0f};
  float metallic = 0.0f;
  float ao = 1.0f;
  float t = 1e6f;
};

GBufferPixel traceAndEvaluatePixel(const Vec3& ro, const Vec3& rd, const SceneCfg& cfg, float px, float py) {
  GBufferPixel gb;
  float t = 0.0f;
  int matId = MAT_NONE;
  if (!traceSdf(ro, rd, 0.01f, 40.0f, cfg, true, t, matId)) {
    gb.direct = evalSky(rd, cfg, true);
    return gb;
  }

  gb.valid = true;
  gb.t = t;
  Vec3 pos = ro + rd * t;
  Vec3 n = calcSdfNormal(pos, cfg, true);
  Vec3 v = -rd;
  SurfaceMat mat = getMaterial(matId, pos, cfg);
  gb.pos = pos;
  gb.n = n;
  gb.baseColor = mat.baseColor;
  gb.metallic = mat.metallic;

  if (mat.isEmissive) {
    gb.isEmissive = true;
    gb.direct = mat.emission;
    return gb;
  }

  // 1. Direct lighting (Sun + Moon + Emissive Panel LTC + Emissive SDF Geometry, all SDF soft-shadowed)
  gb.direct = evalDirectLighting(pos, n, v, mat, cfg, px, py, true);

  // 2. Raw IGN-jittered SDF Cone GI + SDF AO (denoised in bilateral pass)
  evalSdfConeGi(pos, n, cfg, px, py, gb.giIrr, gb.ao);
  float ndv = clampf(dot(n, v), 0.02f, 1.0f);
  Vec3 f0 = mix3(Vec3(0.04f), mat.baseColor, mat.metallic);
  gb.fEnv = f0 + (Vec3(1.0f) - f0) * std::pow(1.0f - ndv, 5.0f);

  // 3. Multi-bounce SDF Specular Reflections (chrome/gold spheres + glossy floor)
  float reflStrength = std::max(mat.metallic, smoothstep(0.45f, 0.12f, mat.roughness) * 0.65f);
  if (reflStrength > 0.04f) {
    Vec3 rDir = reflect3(rd, n);
    Vec3 reflRad(0.0f);
    Vec3 currP = pos, currN = n, currR = rDir;
    Vec3 throughput(1.0f);
    for (int bounce = 0; bounce < 2; ++bounce) {
      float rt = 0.0f;
      int rMatId = MAT_NONE;
      if (!traceSdf(currP + currN * 0.01f, currR, 0.01f, 30.0f, cfg, true, rt, rMatId)) {
        reflRad += throughput * evalSky(currR, cfg, false);
        break;
      }
      Vec3 hp = currP + currN * 0.01f + currR * rt;
      Vec3 hn = calcSdfNormal(hp, cfg, true);
      SurfaceMat hm = getMaterial(rMatId, hp, cfg);
      if (hm.isEmissive) {
        reflRad += throughput * hm.emission;
        break;
      }
      Vec3 hitDirect = evalDirectLighting(hp, hn, -currR, hm, cfg, px, py, false);
      Vec3 hitAmb = hm.baseColor * (evalSky(hn, cfg, false) * 0.25f + evalStarlightAmbient(hn, cfg) + Vec3(0.08f) * hm.baseColor);
      float r2 = std::max(hm.metallic, smoothstep(0.35f, 0.08f, hm.roughness) * 0.45f);
      if (bounce == 1 || r2 < 0.15f) {
        reflRad += throughput * (hitDirect + hitAmb);
        break;
      }
      reflRad += throughput * (hitDirect + hitAmb) * (1.0f - r2 * 0.6f);
      throughput *= hm.baseColor * r2;
      currP = hp;
      currN = hn;
      currR = reflect3(currR, hn);
    }
    gb.specEnv = reflRad * smoothstep(0.65f, 0.06f, mat.roughness);
  }
  return gb;
}

inline Vec3 acesToneMap(const Vec3& x) {
  Vec3 a = x * (x * 2.51f + Vec3(0.03f));
  Vec3 b = x * (x * 2.43f + Vec3(0.59f)) + Vec3(0.14f);
  return clamp3(Vec3(a.x / b.x, a.y / b.y, a.z / b.z), 0.0f, 1.0f);
}

void renderSceneToPpm(const SceneCfg& cfg, const std::string& outPath, int width = 800, int height = 450) {
  auto t0 = std::chrono::steady_clock::now();
  std::vector<GBufferPixel> gbuf(static_cast<size_t>(width) * height);
  std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);

  Vec3 camPos(0.0f, -4.55f, 0.72f);
  Vec3 camTarget(0.0f, 0.35f, 0.32f);
  Vec3 fwd = normalize(camTarget - camPos);
  Vec3 right = normalize(cross(fwd, Vec3(0, 0, 1)));
  Vec3 up = cross(right, fwd);
  float aspect = static_cast<float>(width) / static_cast<float>(height);
  float tanHalfFov = std::tan(48.0f * PI / 360.0f);

  unsigned int numThreads = std::max(1u, std::thread::hardware_concurrency());
  std::vector<std::thread> threads;
  threads.reserve(numThreads);

  // Pass 1: Trace primary + direct + GI raw + reflections
  for (unsigned int tid = 0; tid < numThreads; ++tid) {
    threads.emplace_back([&, tid]() {
      for (int py = static_cast<int>(tid); py < height; py += static_cast<int>(numThreads)) {
        for (int px = 0; px < width; ++px) {
          float ndcX = ((px + 0.5f) / width) * 2.0f - 1.0f;
          float ndcY = 1.0f - ((py + 0.5f) / height) * 2.0f;
          Vec3 rd = normalize(fwd + right * (ndcX * aspect * tanHalfFov) + up * (ndcY * tanHalfFov));
          gbuf[static_cast<size_t>(py) * width + px] = traceAndEvaluatePixel(camPos, rd, cfg, float(px), float(py));
        }
      }
    });
  }
  for (auto& th : threads) th.join();
  threads.clear();

  // Pass 2: GI_BILATERAL_FRAG edge-preserving spatial filter + composite
  for (unsigned int tid = 0; tid < numThreads; ++tid) {
    threads.emplace_back([&, tid]() {
      for (int py = static_cast<int>(tid); py < height; py += static_cast<int>(numThreads)) {
        for (int px = 0; px < width; ++px) {
          const GBufferPixel& c = gbuf[static_cast<size_t>(py) * width + px];
          Vec3 lin(0.0f);
          if (!c.valid || c.isEmissive) {
            lin = c.direct;
          } else {
            Vec3 giSum(0.0f);
            float wSum = 0.0f;
            for (int dy = -2; dy <= 2; ++dy) {
              int ny = clampf(py + dy, 0, height - 1);
              for (int dx = -2; dx <= 2; ++dx) {
                int nx = clampf(px + dx, 0, width - 1);
                const GBufferPixel& q = gbuf[static_cast<size_t>(ny) * width + nx];
                if (!q.valid || q.isEmissive) continue;
                float nW = std::pow(std::max(0.0f, dot(c.n, q.n)), 16.0f);
                float dZ = std::abs(c.t - q.t) / std::max(0.5f, c.t);
                float zW = std::exp(-dZ * 24.0f);
                float gW = std::exp(-float(dx * dx + dy * dy) / 5.0f);
                float w = nW * zW * gW;
                giSum += q.giIrr * w;
                wSum += w;
              }
            }
            Vec3 giFilt = wSum > 1e-4f ? (giSum / wSum) : c.giIrr;
            Vec3 diffGi = (Vec3(1.0f) - c.fEnv) * (1.0f - c.metallic) * c.baseColor * giFilt * c.ao;
            Vec3 spec = mix3(giFilt * 0.45f, c.specEnv, length(c.specEnv) > 0.0f ? 0.85f : 0.0f) * c.fEnv * c.ao;
            lin = c.direct + diffGi + spec;
          }
          Vec3 hdr = lin * cfg.exposure;
          Vec3 mapped = acesToneMap(hdr);
          size_t idx = (static_cast<size_t>(py) * width + px) * 3;
          rgb[idx + 0] = static_cast<uint8_t>(clampf(std::pow(mapped.x, 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f + 0.5f);
          rgb[idx + 1] = static_cast<uint8_t>(clampf(std::pow(mapped.y, 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f + 0.5f);
          rgb[idx + 2] = static_cast<uint8_t>(clampf(std::pow(mapped.z, 1.0f / 2.2f), 0.0f, 1.0f) * 255.0f + 0.5f);
        }
      }
    });
  }
  for (auto& th : threads) th.join();

  std::ofstream out(outPath, std::ios::binary);
  out << "P6\n" << width << " " << height << "\n255\n";
  out.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
  out.close();

  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  std::printf("[SdfLightingDemoCpp] Rendered %-34s -> %s (%.0f ms, %ux%u)\n",
              cfg.name.c_str(), outPath.c_str(), ms, width, height);
}

} // namespace

int main(int argc, char** argv) {
  std::string outDir = (argc > 1) ? argv[1] : "output";
  std::filesystem::create_directories(outDir);

  // 1. Night: Lit ONLY by Emissive Panel (LTC) + Emissive Orb + Emissive Neon Bar + SDF Soft Shadows + SDF Cone GI
  SceneCfg scEmissiveNight;
  scEmissiveNight.panel.center = Vec3(0.0f, 0.25f, 2.35f);
  scEmissiveNight.name = "01_emissive_night_lit";
  scEmissiveNight.sunEnabled = false;
  scEmissiveNight.moonEnabled = false;
  scEmissiveNight.starsEnabled = true;
  scEmissiveNight.starIntensity = 0.8f;
  scEmissiveNight.skyZenith = Vec3(0.002f, 0.004f, 0.010f);
  scEmissiveNight.skyHorizon = Vec3(0.004f, 0.007f, 0.015f);
  scEmissiveNight.emissiveGeometryEnabled = true;
  scEmissiveNight.panel.enabled = true;
  scEmissiveNight.exposure = 1.15f;
  renderSceneToPpm(scEmissiveNight, outDir + "/01_emissive_night_lit.ppm");

  // 2. Pure Emissive SDF Geometry ONLY (Area Panel OFF, Sun OFF, Moon OFF, Stars OFF)
  //    Proves emissive SDF objects directly illuminate the scene, cast SDF soft shadows, and bounce SDF GI!
  SceneCfg scEmissiveGeomOnly = scEmissiveNight;
  scEmissiveGeomOnly.name = "02_emissive_geometry_only";
  scEmissiveGeomOnly.starsEnabled = false;
  scEmissiveGeomOnly.skyZenith = Vec3(0.001f);
  scEmissiveGeomOnly.skyHorizon = Vec3(0.001f);
  scEmissiveGeomOnly.panel.enabled = false;
  scEmissiveGeomOnly.emissiveOrbRad = Vec3(22.0f, 8.8f, 2.2f);
  scEmissiveGeomOnly.neonRad = Vec3(3.0f, 13.0f, 18.0f);
  scEmissiveGeomOnly.exposure = 1.25f;
  renderSceneToPpm(scEmissiveGeomOnly, outDir + "/02_emissive_geometry_only.ppm");

  // 3. Daylight Sun + Skylight + SDF Soft Shadows + SDF Cone GI + SDF Reflections
  SceneCfg scDay;
  scDay.name = "03_daylight_sdf_shadows_gi";
  scDay.sunEnabled = true;
  scDay.sunDir = normalize(Vec3(0.45f, -0.25f, 0.85f));
  scDay.sunIrradiance = Vec3(14.0f, 11.8f, 8.8f);
  scDay.moonEnabled = false;
  scDay.starsEnabled = false;
  scDay.skyZenith = Vec3(0.14f, 0.38f, 0.90f);
  scDay.skyHorizon = Vec3(0.40f, 0.60f, 0.88f);
  scDay.emissiveGeometryEnabled = true;
  scDay.emissiveOrbRad = Vec3(4.5f, 1.8f, 0.5f);
  scDay.neonRad = Vec3(0.7f, 3.2f, 4.8f);
  scDay.panel.enabled = false;
  scDay.exposure = 0.72f;
  renderSceneToPpm(scDay, outDir + "/03_daylight_sdf_shadows_gi.ppm");

  // 4. Night Moonlight + Starlight (visible Moon & Stars above back wall) + Subtle Emissive Accent
  SceneCfg scMoon;
  scMoon.name = "04_moonlight_starlight_emissive";
  scMoon.sunEnabled = false;
  scMoon.moonEnabled = true;
  scMoon.moonDir = normalize(Vec3(-0.14f, 0.94f, 0.30f)); // Visible moon above back parapet + silver-blue moonlight
  scMoon.moonIrradiance = Vec3(1.65f, 2.05f, 3.05f);
  scMoon.starsEnabled = true;
  scMoon.starIntensity = 2.2f;
  scMoon.skyZenith = Vec3(0.005f, 0.009f, 0.025f);
  scMoon.skyHorizon = Vec3(0.012f, 0.020f, 0.048f);
  scMoon.emissiveGeometryEnabled = true;
  scMoon.emissiveOrbRad = Vec3(3.8f, 1.5f, 0.4f);
  scMoon.neonRad = Vec3(0.6f, 2.4f, 3.6f);
  scMoon.panel.enabled = false;
  scMoon.exposure = 1.28f;
  renderSceneToPpm(scMoon, outDir + "/04_moonlight_starlight_emissive.ppm");

  return 0;
}

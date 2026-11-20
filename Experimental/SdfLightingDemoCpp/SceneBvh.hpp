#pragma once
#include "MathTypes.hpp"
#include <array>
#include <limits>
#include <string>
#include <vector>

namespace sdf_demo {

struct Material {
  Vec3 baseColor{0.8f};
  float metallic = 0.0f;
  float roughness = 0.5f;
  float coat = 0.0f;
  float coatRoughness = 0.04f;
  float ior = 1.5f;
  Vec3 edgeTint{1.0f};
  Vec3 emission{0.0f};
  bool isEmissive = false;
  bool checkerFloor = false;
  bool giContributor = true;
};

enum class PrimType { Box, Sphere, Capsule, Triangle };

struct ScenePrim {
  PrimType type = PrimType::Box;
  int id = 0;
  int matId = 0;
  bool castShadow = true;

  // Box: center + halfExtents; Sphere: center + radius(halfExtents.x);
  // Capsule: p0(center), p1(p1), radius(halfExtents.x)
  Vec3 center{0.0f};
  Vec3 halfExtents{0.5f};
  Vec3 p1{0.0f};

  // Triangle vertices & vertex normals (for mesh geometry like smooth torus!)
  Vec3 v0, v1, v2;
  Vec3 n0, n1, n2;

  Vec3 bmin{0.0f}, bmax{0.0f};

  void computeBounds() {
    if (type == PrimType::Box) {
      bmin = center - halfExtents;
      bmax = center + halfExtents;
    } else if (type == PrimType::Sphere) {
      bmin = center - Vec3(halfExtents.x);
      bmax = center + Vec3(halfExtents.x);
    } else if (type == PrimType::Capsule) {
      Vec3 lo = min3(center, p1);
      Vec3 hi = max3(center, p1);
      bmin = lo - Vec3(halfExtents.x);
      bmax = hi + Vec3(halfExtents.x);
    } else {
      bmin = min3(v0, min3(v1, v2)) - Vec3(1e-4f);
      bmax = max3(v0, max3(v1, v2)) + Vec3(1e-4f);
    }
  }
};

struct AreaLight {
  bool enabled = false;
  bool omni = false;
  Vec3 center{0.0f, 0.25f, 2.35f};
  Vec3 right{1.0f, 0.0f, 0.0f};
  Vec3 up{0.0f, 1.0f, 0.0f};
  Vec3 normal{0.0f, 0.0f, -1.0f};
  float hw = 0.72f;
  float hh = 0.50f;
  Vec3 radiance{14.0f, 12.2f, 9.8f};
  int skipPrimId = -1;
};

struct BvhNode {
  Vec3 bmin{0.0f}, bmax{0.0f};
  int left = -1;
  int right = -1;
  int firstPrim = 0;
  int primCount = 0;
};

struct HitInfo {
  bool hit = false;
  float t = 1e6f;
  Vec3 pos{0.0f};
  Vec3 normal{0.0f, 0.0f, 1.0f};
  int primIdx = -1;
  int matId = -1;
};

struct Scene {
  std::string name;
  std::vector<Material> materials;
  std::vector<ScenePrim> prims;
  std::vector<BvhNode> bvhNodes;
  std::vector<int> bvhIndices;

  // Lighting parameters (placeholder simple sky/sun/moon before full AtmosphereSky port)
  bool sunEnabled = false;
  Vec3 sunDir = normalize(Vec3(0.45f, -0.25f, 0.85f));
  Vec3 sunIrradiance{12.0f, 10.4f, 8.0f};
  float sunAngularRad = 0.018f;

  bool moonEnabled = false;
  Vec3 moonDir = normalize(Vec3(-0.20f, -0.35f, 0.91f));
  Vec3 moonIrradiance{0.45f, 0.55f, 0.85f};

  Vec3 skyZenith{0.004f, 0.007f, 0.016f};
  Vec3 skyHorizon{0.008f, 0.014f, 0.030f};
  Vec3 starlightAmbient{0.003f, 0.004f, 0.007f};

  AreaLight areaLight;
  float exposure = 1.0f;

  Material sampleMaterial(int matId, const Vec3& pos) const {
    Material m = materials[matId];
    if (m.checkerFloor) {
      int cx = static_cast<int>(std::floor(pos.x * 1.25f));
      int cy = static_cast<int>(std::floor(pos.y * 1.25f));
      bool chk = ((cx + cy) & 1) == 0;
      m.baseColor = chk ? Vec3(0.56f, 0.56f, 0.58f) : Vec3(0.40f, 0.41f, 0.43f);
      m.roughness = chk ? 0.22f : 0.34f;
    }
    return m;
  }

  Vec3 evalSimpleSky(const Vec3& rd) const {
    float z = clampf(rd.z, 0.0f, 1.0f);
    Vec3 sky = mix3(skyHorizon, skyZenith, std::pow(z, 0.55f));
    if (sunEnabled && rd.z > -0.02f) {
      float cs = dot(rd, sunDir);
      if (cs > 0.85f) {
        float disc = smoothstep(std::cos(sunAngularRad * 1.3f), std::cos(sunAngularRad * 0.7f), cs);
        float glow = std::pow(std::max(0.0f, cs), 160.0f) * 0.25f;
        sky += sunIrradiance * (disc * 2.5f + glow);
      }
    }
    if (moonEnabled && rd.z > -0.02f) {
      float cm = dot(rd, moonDir);
      if (cm > 0.85f) {
        float disc = smoothstep(0.9990f, 0.9996f, cm);
        float glow = std::pow(std::max(0.0f, cm), 280.0f) * 0.35f;
        sky += Vec3(0.90f, 0.94f, 1.0f) * (disc * 2.0f + glow);
      }
    }
    return sky;
  }

  void buildBvh() {
    bvhNodes.clear();
    bvhIndices.resize(prims.size());
    for (size_t i = 0; i < prims.size(); ++i) {
      prims[i].id = static_cast<int>(i);
      prims[i].computeBounds();
      bvhIndices[i] = static_cast<int>(i);
    }
    bvhNodes.reserve(prims.size() * 2 + 1);
    buildNode(0, static_cast<int>(prims.size()));
  }

private:
  int buildNode(int start, int end) {
    int nodeIdx = static_cast<int>(bvhNodes.size());
    bvhNodes.emplace_back();
    Vec3 bmin(1e9f), bmax(-1e9f);
    for (int i = start; i < end; ++i) {
      const auto& p = prims[bvhIndices[i]];
      bmin = min3(bmin, p.bmin);
      bmax = max3(bmax, p.bmax);
    }
    bvhNodes[nodeIdx].bmin = bmin;
    bvhNodes[nodeIdx].bmax = bmax;

    int count = end - start;
    if (count <= 2) {
      bvhNodes[nodeIdx].firstPrim = start;
      bvhNodes[nodeIdx].primCount = count;
      return nodeIdx;
    }

    Vec3 ext = bmax - bmin;
    int axis = (ext.x >= ext.y && ext.x >= ext.z) ? 0 : (ext.y >= ext.z ? 1 : 2);
    int mid = (start + end) / 2;
    std::nth_element(bvhIndices.begin() + start, bvhIndices.begin() + mid, bvhIndices.begin() + end,
                     [&](int a, int b) {
                       float ca = 0.5f * (prims[a].bmin[axis] + prims[a].bmax[axis]);
                       float cb = 0.5f * (prims[b].bmin[axis] + prims[b].bmax[axis]);
                       return ca < cb;
                     });
    int lc = buildNode(start, mid);
    int rc = buildNode(mid, end);
    bvhNodes[nodeIdx].left = lc;
    bvhNodes[nodeIdx].right = rc;
    bvhNodes[nodeIdx].primCount = 0;
    return nodeIdx;
  }
};

//--------------------------------------------------------------------------------------------------
// Exact BVH Ray-Primitive Intersection + Traversal
//--------------------------------------------------------------------------------------------------
inline bool intersectAabb(const Vec3& ro, const Vec3& invDir, const Vec3& bmin, const Vec3& bmax, float maxT) {
  float t0 = 0.0f, t1 = maxT;
  for (int a = 0; a < 3; ++a) {
    float invD = invDir[a];
    float tNear = (bmin[a] - ro[a]) * invD;
    float tFar  = (bmax[a] - ro[a]) * invD;
    if (invD < 0.0f) std::swap(tNear, tFar);
    t0 = std::max(t0, tNear);
    t1 = std::min(t1, tFar);
    if (t1 < t0) return false;
  }
  return true;
}

inline bool intersectPrim(const ScenePrim& p, const Vec3& ro, const Vec3& rd, float maxT,
                          float& outT, Vec3& outN) {
  if (p.type == PrimType::Box) {
    Vec3 inv(std::abs(rd.x) > 1e-8f ? 1.0f / rd.x : 1e8f,
             std::abs(rd.y) > 1e-8f ? 1.0f / rd.y : 1e8f,
             std::abs(rd.z) > 1e-8f ? 1.0f / rd.z : 1e8f);
    Vec3 t0v = (p.bmin - ro); t0v.x *= inv.x; t0v.y *= inv.y; t0v.z *= inv.z;
    Vec3 t1v = (p.bmax - ro); t1v.x *= inv.x; t1v.y *= inv.y; t1v.z *= inv.z;
    Vec3 tSm = min3(t0v, t1v);
    Vec3 tBg = max3(t0v, t1v);
    float tN = std::max(tSm.x, std::max(tSm.y, tSm.z));
    float tF = std::min(tBg.x, std::min(tBg.y, tBg.z));
    if (tN > tF || tF < 0.0005f) return false;
    float t = (tN > 0.0005f) ? tN : tF;
    if (t >= maxT) return false;
    outT = t;
    Vec3 hit = ro + rd * t - p.center;
    Vec3 rel(hit.x / std::max(1e-5f, p.halfExtents.x),
             hit.y / std::max(1e-5f, p.halfExtents.y),
             hit.z / std::max(1e-5f, p.halfExtents.z));
    Vec3 ar = abs3(rel);
    if (ar.x >= ar.y && ar.x >= ar.z) outN = {rel.x >= 0 ? 1.0f : -1.0f, 0, 0};
    else if (ar.y >= ar.x && ar.y >= ar.z) outN = {0, rel.y >= 0 ? 1.0f : -1.0f, 0};
    else outN = {0, 0, rel.z >= 0 ? 1.0f : -1.0f};
    if (tN <= 0.0005f) outN = -outN;
    return true;
  }
  if (p.type == PrimType::Sphere) {
    Vec3 oc = ro - p.center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - p.halfExtents.x * p.halfExtents.x;
    float disc = b * b - c;
    if (disc < 0.0f) return false;
    float sq = std::sqrt(disc);
    float t = -b - sq;
    if (t <= 0.0005f) t = -b + sq;
    if (t <= 0.0005f || t >= maxT) return false;
    outT = t;
    outN = normalize((ro + rd * t) - p.center);
    return true;
  }
  if (p.type == PrimType::Capsule) {
    Vec3 ba = p.p1 - p.center;
    Vec3 oa = ro - p.center;
    float baba = dot(ba, ba);
    float bard = dot(ba, rd);
    float baoa = dot(ba, oa);
    float rdoa = dot(rd, oa);
    float oaoa = dot(oa, oa);
    float r = p.halfExtents.x;
    float a = baba - bard * bard;
    float b = baba * rdoa - baoa * bard;
    float c = baba * oaoa - baoa * baoa - r * r * baba;
    float h = b * b - a * c;
    if (h >= 0.0f && a > 1e-8f) {
      float t = (-b - std::sqrt(h)) / a;
      float y = baoa + t * bard;
      if (y > 0.0f && y < baba && t > 0.0005f && t < maxT) {
        outT = t;
        Vec3 hp = ro + rd * t;
        outN = normalize(hp - (p.center + ba * (y / baba)));
        return true;
      }
      Vec3 oc = (y <= 0.0f) ? oa : (ro - p.p1);
      float b2 = dot(rd, oc);
      float c2 = dot(oc, oc) - r * r;
      float h2 = b2 * b2 - c2;
      if (h2 > 0.0f) {
        float tCap = -b2 - std::sqrt(h2);
        if (tCap > 0.0005f && tCap < maxT) {
          outT = tCap;
          Vec3 capC = (y <= 0.0f) ? p.center : p.p1;
          outN = normalize((ro + rd * tCap) - capC);
          return true;
        }
      }
    }
    return false;
  }
  // Triangle (Moller-Trumbore with smooth interpolated vertex normals)
  Vec3 e1 = p.v1 - p.v0;
  Vec3 e2 = p.v2 - p.v0;
  Vec3 h = cross(rd, e2);
  float a = dot(e1, h);
  if (std::abs(a) < 1e-7f) return false;
  float f = 1.0f / a;
  Vec3 s = ro - p.v0;
  float u = f * dot(s, h);
  if (u < 0.0f || u > 1.0f) return false;
  Vec3 q = cross(s, e1);
  float v = f * dot(rd, q);
  if (v < 0.0f || u + v > 1.0f) return false;
  float t = f * dot(e2, q);
  if (t <= 0.0005f || t >= maxT) return false;
  outT = t;
  float w = 1.0f - u - v;
  outN = normalize(p.n0 * w + p.n1 * u + p.n2 * v);
  if (dot(outN, rd) > 0.0f) outN = -outN;
  return true;
}

inline HitInfo traceBvh(const Scene& sc, const Vec3& ro, const Vec3& rd, float maxT,
                        int skipPrimId = -1, bool skipEmissive = false) {
  HitInfo best;
  best.t = maxT;
  if (sc.bvhNodes.empty()) return best;

  Vec3 inv(std::abs(rd.x) > 1e-8f ? 1.0f / rd.x : 1e8f,
           std::abs(rd.y) > 1e-8f ? 1.0f / rd.y : 1e8f,
           std::abs(rd.z) > 1e-8f ? 1.0f / rd.z : 1e8f);

  int stack[64];
  int sp = 0;
  stack[sp++] = 0;

  while (sp > 0) {
    const BvhNode& node = sc.bvhNodes[stack[--sp]];
    if (!intersectAabb(ro, inv, node.bmin, node.bmax, best.t)) continue;
    if (node.primCount > 0) {
      for (int i = 0; i < node.primCount; ++i) {
        int pIdx = sc.bvhIndices[node.firstPrim + i];
        const ScenePrim& pr = sc.prims[pIdx];
        if (pIdx == skipPrimId) continue;
        if (skipEmissive && sc.materials[pr.matId].isEmissive) continue;
        float tHit;
        Vec3 nHit;
        if (intersectPrim(pr, ro, rd, best.t, tHit, nHit)) {
          best.hit = true;
          best.t = tHit;
          best.pos = ro + rd * tHit;
          best.normal = nHit;
          best.primIdx = pIdx;
          best.matId = pr.matId;
        }
      }
    } else {
      stack[sp++] = node.left;
      stack[sp++] = node.right;
    }
  }
  return best;
}

inline bool traceBvhAnyHit(const Scene& sc, const Vec3& ro, const Vec3& rd, float maxT,
                           int skipPrimId = -1, int skipMatId = -1) {
  if (sc.bvhNodes.empty()) return false;
  Vec3 inv(std::abs(rd.x) > 1e-8f ? 1.0f / rd.x : 1e8f,
           std::abs(rd.y) > 1e-8f ? 1.0f / rd.y : 1e8f,
           std::abs(rd.z) > 1e-8f ? 1.0f / rd.z : 1e8f);
  int stack[64];
  int sp = 0;
  stack[sp++] = 0;

  while (sp > 0) {
    const BvhNode& node = sc.bvhNodes[stack[--sp]];
    if (!intersectAabb(ro, inv, node.bmin, node.bmax, maxT)) continue;
    if (node.primCount > 0) {
      for (int i = 0; i < node.primCount; ++i) {
        int pIdx = sc.bvhIndices[node.firstPrim + i];
        const ScenePrim& pr = sc.prims[pIdx];
        if (pIdx == skipPrimId || !pr.castShadow) continue;
        if (skipMatId >= 0 && pr.matId == skipMatId) continue;
        if (sc.materials[pr.matId].isEmissive) continue;
        float tHit;
        Vec3 nHit;
        if (intersectPrim(pr, ro, rd, maxT, tHit, nHit)) return true;
      }
    } else {
      stack[sp++] = node.left;
      stack[sp++] = node.right;
    }
  }
  return false;
}

//--------------------------------------------------------------------------------------------------
// OpenPBR Direct BRDF + Clipped-Polygon LTC Area Light (Exact ShadowAlgorithmLab Port)
//--------------------------------------------------------------------------------------------------
inline float fresnelDielectric(float cosTheta, float ior) {
  float c = clampf(cosTheta, 0.0f, 1.0f);
  float r0 = (1.0f - ior) / (1.0f + ior);
  r0 = r0 * r0;
  return r0 + (1.0f - r0) * std::pow(1.0f - c, 5.0f);
}

inline Vec3 fresnelConductorF82(float cosTheta, const Vec3& f0, const Vec3& edgeTint) {
  float c = clampf(cosTheta, 0.0f, 1.0f);
  float mc = 1.0f - c;
  float mc5 = mc * mc * mc * mc * mc;
  Vec3 schlick = f0 + (Vec3(1.0f) - f0) * mc5;
  float cBar = 1.0f / 7.0f;
  float denom = cBar * std::pow(1.0f - cBar, 6.0f);
  Vec3 b = (f0 + (Vec3(1.0f) - f0) * std::pow(1.0f - cBar, 5.0f) - edgeTint) / std::max(1e-5f, denom);
  return clamp3(schlick - b * (c * std::pow(1.0f - c, 6.0f)), 0.0f, 1.0f);
}

inline Vec3 evalOpenPbrDirect(const Vec3& n, const Vec3& v, const Vec3& l, const Material& mat) {
  float ndl = std::max(0.0f, dot(n, l));
  if (ndl <= 0.0f) return Vec3(0.0f);
  float ndv = std::max(1e-4f, dot(n, v));
  Vec3 h = normalize(v + l);
  float ndh = std::max(0.0f, dot(n, h));
  float vdh = std::max(0.0f, dot(v, h));

  float r = clampf(mat.roughness, 0.035f, 1.0f);
  float a = r * r;
  float a2 = a * a;
  float denom = ndh * ndh * (a2 - 1.0f) + 1.0f;
  float d = a2 / std::max(1e-6f, PI * denom * denom);
  float k = (r + 1.0f) * (r + 1.0f) * 0.125f;
  float g = (ndv / (ndv * (1.0f - k) + k)) * (ndl / (ndl * (1.0f - k) + k));

  Vec3 fDiel(fresnelDielectric(vdh, mat.ior));
  Vec3 fCond = fresnelConductorF82(vdh, mat.baseColor, mat.edgeTint);
  Vec3 f = mix3(fDiel, fCond, mat.metallic);
  Vec3 kd = (Vec3(1.0f) - f) * (1.0f - mat.metallic);
  Vec3 spec = (f * d * g) / std::max(1e-4f, 4.0f * ndv * ndl);

  Vec3 coatTerm(0.0f);
  if (mat.coat > 0.001f) {
    float cr = clampf(mat.coatRoughness, 0.025f, 1.0f);
    float ca = cr * cr;
    float ca2 = ca * ca;
    float cDen = ndh * ndh * (ca2 - 1.0f) + 1.0f;
    float cD = ca2 / std::max(1e-6f, PI * cDen * cDen);
    float cK = (cr + 1.0f) * (cr + 1.0f) * 0.125f;
    float cG = (ndv / (ndv * (1.0f - cK) + cK)) * (ndl / (ndl * (1.0f - cK) + cK));
    float cF = fresnelDielectric(vdh, 1.5f) * mat.coat;
    coatTerm = Vec3((cF * cD * cG) / std::max(1e-4f, 4.0f * ndv * ndl));
    kd *= (1.0f - cF * 0.5f);
  }
  return (kd * mat.baseColor / PI + spec + coatTerm) * ndl;
}

inline float integrateSphericalEdgeZ(const Vec3& v1, const Vec3& v2) {
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

inline float integrateClippedQuadFormFactor(const Vec3 localQ[4]) {
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
  return clampf(std::abs(contourSum) / TWO_PI, 0.0f, 1.0f);
}

inline Vec3 evalEmissivePanelLtc(const Vec3& pos, const Vec3& n, const Vec3& v, const Material& mat,
                                 const AreaLight& al, float vis) {
  if (!al.enabled || vis <= 1e-4f) return Vec3(0.0f);
  if (!al.omni && dot(pos - al.center, al.normal) <= 0.02f) return Vec3(0.0f);

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

  Vec3 halfU = al.right * al.hw;
  Vec3 halfV = al.up * al.hh;
  Vec3 wc[4] = {
    al.center - halfU - halfV - pos,
    al.center + halfU - halfV - pos,
    al.center + halfU + halfV - pos,
    al.center - halfU + halfV - pos,
  };
  Vec3 diffLocal[4];
  for (int i = 0; i < 4; ++i) diffLocal[i] = {dot(wc[i], t1), dot(wc[i], t2), dot(wc[i], n)};
  float diffFF = integrateClippedQuadFormFactor(diffLocal);

  float r = std::max(mat.roughness, 0.035f);
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
  Vec3 f82 = mix3(f0, mat.edgeTint, mat.metallic);
  float schlick = std::pow(std::max(1.0f - noV, 1e-4f), 5.0f);
  Vec3 specAlb = f0 + (f82 - f0) * (schlick * clampf(1.0f - r * 0.65f, 0.0f, 1.0f));
  return al.radiance * (al.omni ? 1.35f : 1.0f) * (diffAlb * diffFF + specAlb * specFF) * vis;
}

// Helper: Build showcase scene with BVH primitives + real tessellated Torus triangle mesh!
inline Scene buildShowcaseScene(bool dayMode, bool areaPanelOn, bool emissiveGeomOn) {
  Scene sc;
  // Materials
  // 0: Checkered floor
  Material mFloor; mFloor.checkerFloor = true; mFloor.roughness = 0.25f; mFloor.metallic = 0.04f;
  // 1: Back wall (neutral matte plaster)
  Material mBack; mBack.baseColor = {0.76f, 0.75f, 0.73f}; mBack.roughness = 0.74f;
  // 2: Crimson left wall
  Material mCrim; mCrim.baseColor = {0.74f, 0.11f, 0.09f}; mCrim.roughness = 0.76f;
  // 3: Teal right wall
  Material mTeal; mTeal.baseColor = {0.10f, 0.60f, 0.48f}; mTeal.roughness = 0.76f;
  // 4: Roof soffits
  Material mRoof; mRoof.baseColor = {0.28f, 0.29f, 0.32f}; mRoof.roughness = 0.65f;
  // 5: Gold metallic sphere
  Material mGold; mGold.baseColor = {0.95f, 0.72f, 0.26f}; mGold.edgeTint = {1.0f, 0.90f, 0.62f};
  mGold.metallic = 0.94f; mGold.roughness = 0.14f;
  // 6: Chrome mirror sphere
  Material mChrome; mChrome.baseColor = {0.90f, 0.92f, 0.95f}; mChrome.edgeTint = {1.0f};
  mChrome.metallic = 0.98f; mChrome.roughness = 0.04f;
  // 7: Ceramic sculpture + pedestal
  Material mCeramic; mCeramic.baseColor = {0.86f, 0.85f, 0.83f}; mCeramic.roughness = 0.26f; mCeramic.coat = 0.45f;
  // 8: Occluder pillar
  Material mPillar; mPillar.baseColor = {0.62f, 0.63f, 0.65f}; mPillar.roughness = 0.68f;
  // 9: Amber emissive orb
  Material mOrb; mOrb.baseColor = {1.0f, 0.65f, 0.25f}; mOrb.emission = {16.0f, 6.5f, 1.6f}; mOrb.isEmissive = true; mOrb.giContributor = false;
  // 10: Cyan neon capsule
  Material mNeon; mNeon.baseColor = {0.32f, 0.86f, 1.0f}; mNeon.emission = {2.4f, 10.5f, 15.0f}; mNeon.isEmissive = true; mNeon.giContributor = false;
  // 11: Overhead area light panel mesh
  Material mPanel; mPanel.baseColor = {1.0f, 0.94f, 0.84f}; mPanel.emission = {13.5f, 11.8f, 9.5f}; mPanel.isEmissive = true; mPanel.giContributor = false;

  sc.materials = {mFloor, mBack, mCrim, mTeal, mRoof, mGold, mChrome, mCeramic, mPillar, mOrb, mNeon, mPanel};

  auto addBox = [&](const Vec3& c, const Vec3& h, int mat, bool shadow = true) {
    ScenePrim p;
    p.type = PrimType::Box; p.center = c; p.halfExtents = h; p.matId = mat; p.castShadow = shadow;
    sc.prims.push_back(p);
  };
  auto addSphere = [&](const Vec3& c, float r, int mat, bool shadow = true) {
    ScenePrim p;
    p.type = PrimType::Sphere; p.center = c; p.halfExtents = Vec3(r); p.matId = mat; p.castShadow = shadow;
    sc.prims.push_back(p);
  };
  auto addCapsule = [&](const Vec3& a, const Vec3& b, float r, int mat, bool shadow = true) {
    ScenePrim p;
    p.type = PrimType::Capsule; p.center = a; p.p1 = b; p.halfExtents = Vec3(r); p.matId = mat; p.castShadow = shadow;
    sc.prims.push_back(p);
  };

  // Room geometry
  addBox({0.0f, 0.25f, -0.90f}, {3.0f, 2.6f, 0.10f}, 0, false);          // Floor top at z=-0.80
  addBox({0.0f, 2.75f, 0.725f}, {2.95f, 0.10f, 1.525f}, 1, true);        // Back wall up to z=2.25
  addBox({-2.85f, 0.275f, 0.95f}, {0.10f, 2.475f, 1.75f}, 2, true);      // Crimson left wall
  addBox({2.85f, 0.275f, 0.95f}, {0.10f, 2.475f, 1.75f}, 3, true);       // Teal right wall
  addBox({-2.40f, 0.25f, 2.70f}, {0.55f, 2.45f, 0.12f}, 4, true);        // Left roof soffit
  addBox({2.40f, 0.25f, 2.70f}, {0.55f, 2.45f, 0.12f}, 4, true);         // Right roof soffit

  // Hero spheres + pedestal + shadow-casting occluder pillar
  addSphere({-0.82f, -0.35f, -0.34f}, 0.46f, 5, true);
  addSphere({1.12f, -0.22f, -0.32f}, 0.48f, 6, true);
  addBox({0.05f, 0.65f, -0.45f}, {0.55f, 0.45f, 0.35f}, 7, true);
  addBox({-0.96f, 0.28f, -0.08f}, {0.11f, 0.36f, 0.72f}, 8, true);

  // Real tessellated Torus triangle mesh above pedestal!
  {
    const int NU = 28, NV = 14;
    const float R = 0.40f, r = 0.12f;
    const Vec3 tCenter(0.05f, 0.65f, 0.36f);
    const float cs = 0.8660254f, sn = 0.5f; // tilt 30 deg
    auto evalPt = [&](float u, float v, Vec3& outP, Vec3& outN) {
      float cu = std::cos(u), su = std::sin(u);
      float cv = std::cos(v), sv = std::sin(v);
      Vec3 lp((R + r * cv) * cu, (R + r * cv) * su, r * sv);
      Vec3 ln(cv * cu, cv * su, sv);
      outP = tCenter + Vec3(lp.x, lp.y * cs - lp.z * sn, lp.y * sn + lp.z * cs);
      outN = normalize(Vec3(ln.x, ln.y * cs - ln.z * sn, ln.y * sn + ln.z * cs));
    };
    for (int iu = 0; iu < NU; ++iu) {
      float u0 = (iu / float(NU)) * TWO_PI;
      float u1 = ((iu + 1) / float(NU)) * TWO_PI;
      for (int iv = 0; iv < NV; ++iv) {
        float v0 = (iv / float(NV)) * TWO_PI;
        float v1 = ((iv + 1) / float(NV)) * TWO_PI;
        Vec3 p00, n00, p10, n10, p11, n11, p01, n01;
        evalPt(u0, v0, p00, n00); evalPt(u1, v0, p10, n10);
        evalPt(u1, v1, p11, n11); evalPt(u0, v1, p01, n01);
        ScenePrim t1; t1.type = PrimType::Triangle; t1.matId = 7; t1.castShadow = true;
        t1.v0 = p00; t1.v1 = p10; t1.v2 = p11; t1.n0 = n00; t1.n1 = n10; t1.n2 = n11;
        ScenePrim t2; t2.type = PrimType::Triangle; t2.matId = 7; t2.castShadow = true;
        t2.v0 = p00; t2.v1 = p11; t2.v2 = p01; t2.n0 = n00; t2.n1 = n11; t2.n2 = n01;
        sc.prims.push_back(t1);
        sc.prims.push_back(t2);
      }
    }
  }

  if (emissiveGeomOn) {
    addSphere({-1.70f, 0.15f, 0.32f}, 0.28f, 9, false);
    addCapsule({1.48f, 1.45f, -0.15f}, {1.48f, 1.45f, 1.25f}, 0.075f, 10, false);
  }

  if (areaPanelOn) {
    sc.areaLight.enabled = true;
    sc.areaLight.center = {0.0f, 0.25f, 2.38f};
    sc.areaLight.hw = 0.72f;
    sc.areaLight.hh = 0.50f;
    sc.areaLight.radiance = {13.5f, 11.8f, 9.5f};
    addBox(sc.areaLight.center, {sc.areaLight.hw, sc.areaLight.hh, 0.04f}, 11, false);
    sc.areaLight.skipPrimId = static_cast<int>(sc.prims.size()) - 1;
  }

  if (dayMode) {
    sc.sunEnabled = true;
    sc.sunDir = normalize(Vec3(0.45f, -0.25f, 0.85f));
    sc.sunIrradiance = {12.0f, 10.4f, 8.0f};
    sc.skyZenith = {0.16f, 0.42f, 0.92f};
    sc.skyHorizon = {0.42f, 0.62f, 0.90f};
    sc.starlightAmbient = {0.0f, 0.0f, 0.0f};
    sc.exposure = 0.75f;
  } else {
    sc.sunEnabled = false;
    sc.skyZenith = {0.002f, 0.004f, 0.010f};
    sc.skyHorizon = {0.004f, 0.008f, 0.018f};
    sc.starlightAmbient = {0.002f, 0.003f, 0.006f};
    sc.exposure = 1.15f;
  }

  sc.buildBvh();
  return sc;
}

} // namespace sdf_demo

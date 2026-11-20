#pragma once
#include "ShadowLab.hpp"
#include <chrono>
#include <thread>

namespace sdf_demo {

enum class RenderEngine {
  MatCap = 0,
  PbrDirect = 1,
  Ddgi = 2,
  SdfGi = 3,
  PathTracer = 4,
};

inline const char* engineName(RenderEngine e) {
  switch (e) {
    case RenderEngine::MatCap:     return "MatCap";
    case RenderEngine::PbrDirect:  return "PBR_NoGI";
    case RenderEngine::Ddgi:       return "DDGI";
    case RenderEngine::SdfGi:      return "SDF_GI";
    case RenderEngine::PathTracer: return "PathTracer";
  }
  return "Unknown";
}

inline Vec3 div3(const Vec3& a, const Vec3& b) {
  return {a.x / b.x, a.y / b.y, a.z / b.z};
}

inline Vec3 linearToSrgb(const Vec3& c) {
  auto ch = [](float x) {
    float v = std::max(0.0f, x);
    return (v <= 0.0031308f) ? (12.92f * v) : (1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f);
  };
  return {ch(c.x), ch(c.y), ch(c.z)};
}

inline Vec2 mix2(const Vec2& a, const Vec2& b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t};
}

inline float evalDggx(float ndh, float a) {
  float a2 = a * a;
  float d = ndh * ndh * (a2 - 1.0f) + 1.0f;
  return a2 / std::max(1e-6f, PI * d * d);
}

inline float evalGsmith(float ndv, float ndl, float r) {
  float k = (r + 1.0f) * (r + 1.0f) * 0.125f;
  return (ndv / (ndv * (1.0f - k) + k)) * (ndl / (ndl * (1.0f - k) + k));
}

struct GBufferSample {
  bool hit = false;
  Vec3 pos{0.0f};
  Vec3 normal{0, 0, 1};
  float depth = 1e6f;
  int matId = 0;
  int primIdx = -1;
  Vec3 baseColor{0.0f};
  float rough = 0.5f;
  float metal = 0.0f;
};

struct RenderConfig {
  int width = 480;
  int height = 270;
  RenderEngine engine = RenderEngine::SdfGi;
  ShadowAlg shadowAlg = ShadowAlg::ContactPcss;
  ReflAlg reflAlg = ReflAlg::Bvh;
  int reflBounces = 2;
  int giRays = 16;
  int ptSpp = 24;
  int ptMaxBounces = 6;
  bool ptDenoise = true;
  bool giBilateral = true;
  float exposure = 0.85f;
};

// ============================================================================
// Analytic Scene SDF (used STRICTLY to populate the 3D SDF Radiosity GI Volume)
// ============================================================================
inline float sdBox(const Vec3& p, const Vec3& b) {
  Vec3 q = abs3(p) - b;
  return length(max3(q, Vec3(0.0f))) + std::min(std::max(q.x, std::max(q.y, q.z)), 0.0f);
}

inline float sdCapsule(const Vec3& p, const Vec3& a, const Vec3& b, float r) {
  Vec3 pa = p - a, ba = b - a;
  float h = clampf(dot(pa, ba) / std::max(1e-6f, dot(ba, ba)), 0.0f, 1.0f);
  return length(pa - ba * h) - r;
}

inline float sdTiltedTorus(const Vec3& p) {
  const Vec3 tCenter(0.05f, 0.65f, 0.36f);
  const float cs = 0.8660254f, sn = -0.5f; // inverse of +30 deg tilt
  Vec3 lp = p - tCenter;
  Vec3 q(lp.x, lp.y * cs - lp.z * sn, lp.y * sn + lp.z * cs);
  float lenXY = std::sqrt(q.x * q.x + q.y * q.y) - 0.40f;
  return std::sqrt(lenXY * lenXY + q.z * q.z) - 0.12f;
}

inline float evalSceneSdfForGi(const Scene& sc, const Vec3& p, int& outMatId) {
  float bestD = 1e6f;
  outMatId = 0;
  for (const auto& pr : sc.prims) {
    if (pr.type == PrimType::Triangle) continue;
    float d = 1e6f;
    if (pr.type == PrimType::Box) {
      d = sdBox(p - pr.center, pr.halfExtents);
    } else if (pr.type == PrimType::Sphere) {
      d = length(p - pr.center) - pr.halfExtents.x;
    } else if (pr.type == PrimType::Capsule) {
      d = sdCapsule(p, pr.center, pr.p1, pr.halfExtents.x);
    }
    if (d < bestD) { bestD = d; outMatId = pr.matId; }
  }
  float dTorus = sdTiltedTorus(p);
  if (dTorus < bestD) { bestD = dTorus; outMatId = 7; }
  return bestD;
}

inline Vec3 evalSdfGiNormal(const Scene& sc, const Vec3& p) {
  const float e = 0.005f;
  int dummy = 0;
  float dx = evalSceneSdfForGi(sc, p + Vec3(e, 0, 0), dummy) - evalSceneSdfForGi(sc, p - Vec3(e, 0, 0), dummy);
  float dy = evalSceneSdfForGi(sc, p + Vec3(0, e, 0), dummy) - evalSceneSdfForGi(sc, p - Vec3(0, e, 0), dummy);
  float dz = evalSceneSdfForGi(sc, p + Vec3(0, 0, e), dummy) - evalSceneSdfForGi(sc, p - Vec3(0, 0, e), dummy);
  return normalize(Vec3(dx, dy, dz));
}

// ============================================================================
// 1. MatCap Procedural Studio Shader (view-space normal lookup)
// ============================================================================
inline Vec3 evalMatCapShader(const Vec3& N, const Vec3& V, const Material& mat) {
  if (mat.isEmissive) return clamp3(mat.emission * 0.25f, 0.0f, 1.0f);
  Vec3 camUp(0, 0, 1);
  Vec3 camFwd = normalize(-V);
  Vec3 camR = normalize(cross(camFwd, camUp));
  if (length(camR) < 1e-4f) camR = Vec3(1, 0, 0);
  Vec3 camU = cross(camR, camFwd);
  float nx = dot(N, camR);
  float ny = dot(N, camU);
  float nz = clampf(dot(N, V), 0.0f, 1.0f);

  Vec3 keyDir = normalize(Vec3(-0.48f, 0.72f, 0.50f));
  Vec3 fillDir = normalize(Vec3(0.65f, 0.25f, 0.45f));
  Vec3 vn(nx, ny, nz);
  float keyDiff = clampf(dot(vn, keyDir) * 0.5f + 0.5f, 0.0f, 1.0f);
  float fillDiff = clampf(dot(vn, fillDir) * 0.5f + 0.5f, 0.0f, 1.0f);
  Vec3 Hkey = normalize(keyDir + Vec3(0, 0, 1));
  float specPow = mixf(96.0f, 10.0f, mat.roughness);
  float spec = std::pow(clampf(dot(vn, Hkey), 0.0f, 1.0f), specPow) * mixf(0.18f, 1.15f, 1.0f - mat.roughness);
  float rim = std::pow(clampf(1.0f - nz, 0.0f, 1.0f), 2.8f) * 0.30f;

  Vec3 studioWarm(0.95f, 0.90f, 0.84f);
  Vec3 studioCool(0.24f, 0.30f, 0.40f);
  Vec3 base = mix3(studioCool, studioWarm, keyDiff * 0.78f + fillDiff * 0.22f);
  Vec3 tinted = base * mix3(Vec3(0.85f), mat.baseColor, 0.65f);
  Vec3 specCol = mix3(Vec3(1.0f), mat.baseColor, mat.metallic);
  return tinted * (0.68f + 0.32f * nz) + specCol * spec + Vec3(0.55f, 0.68f, 0.88f) * rim;
}

// ============================================================================
// 2. DDGI Volume (BVH Raytraced Probes + Chebyshev Depth-Moment Visibility)
// ============================================================================
struct DdgiVolume {
  Vec3 origin{-2.60f, -2.10f, -0.45f};
  Vec3 spacing{0.47f, 0.52f, 0.46f};
  int nx = 12, ny = 9, nz = 7; // 756 probes
  static constexpr int IRR_RES = 8;
  static constexpr int DIST_RES = 12;
  float maxRayDist = 8.0f;

  std::vector<Vec3> irrOct;
  std::vector<Vec2> distOct;

  int numProbes() const { return nx * ny * nz; }
  Vec3 probePos(int ix, int iy, int iz) const {
    return origin + Vec3(ix * spacing.x, iy * spacing.y, iz * spacing.z);
  }
  int probeIndex(int ix, int iy, int iz) const { return (iz * ny + iy) * nx + ix; }

  Vec3 sampleIrradianceOct(int pIdx, const Vec3& dir) const {
    Vec2 uv = octEncode(normalize(dir)); // already [0, 1]
    float fx = clampf(uv.x, 0.0f, 0.9999f) * (IRR_RES - 1);
    float fy = clampf(uv.y, 0.0f, 0.9999f) * (IRR_RES - 1);
    int x0 = clampi(int(fx), 0, IRR_RES - 1), x1 = std::min(x0 + 1, IRR_RES - 1);
    int y0 = clampi(int(fy), 0, IRR_RES - 1), y1 = std::min(y0 + 1, IRR_RES - 1);
    float tx = fx - x0, ty = fy - y0;
    const Vec3* p = &irrOct[size_t(pIdx) * IRR_RES * IRR_RES];
    Vec3 a = mix3(p[y0 * IRR_RES + x0], p[y0 * IRR_RES + x1], tx);
    Vec3 b = mix3(p[y1 * IRR_RES + x0], p[y1 * IRR_RES + x1], tx);
    return mix3(a, b, ty);
  }

  Vec2 sampleDistOct(int pIdx, const Vec3& dir) const {
    Vec2 uv = octEncode(normalize(dir)); // already [0, 1]
    float fx = clampf(uv.x, 0.0f, 0.9999f) * (DIST_RES - 1);
    float fy = clampf(uv.y, 0.0f, 0.9999f) * (DIST_RES - 1);
    int x0 = clampi(int(fx), 0, DIST_RES - 1), x1 = std::min(x0 + 1, DIST_RES - 1);
    int y0 = clampi(int(fy), 0, DIST_RES - 1), y1 = std::min(y0 + 1, DIST_RES - 1);
    float tx = fx - x0, ty = fy - y0;
    const Vec2* p = &distOct[size_t(pIdx) * DIST_RES * DIST_RES];
    Vec2 a = mix2(p[y0 * DIST_RES + x0], p[y0 * DIST_RES + x1], tx);
    Vec2 b = mix2(p[y1 * DIST_RES + x0], p[y1 * DIST_RES + x1], tx);
    return mix2(a, b, ty);
  }

  void bake(const Scene& sc, const ShadowSystem& sh, int raysPerProbe = 96, int bounces = 2) {
    int total = numProbes();
    irrOct.assign(size_t(total) * IRR_RES * IRR_RES, Vec3(0.0f));
    distOct.assign(size_t(total) * DIST_RES * DIST_RES, Vec2(maxRayDist, maxRayDist * maxRayDist));

    struct RayHitCache {
      bool hit = false;
      float dist = 8.0f;
      Vec3 dir;
      Vec3 directRad;
      Vec3 pos;
      Vec3 normal;
      Vec3 baseColor;
      bool isEmissive = false;
    };
    std::vector<RayHitCache> cache(size_t(total) * raysPerProbe);

    auto traceRange = [&](int pStart, int pEnd) {
      for (int p = pStart; p < pEnd; ++p) {
        int iz = p / (nx * ny);
        int rem = p % (nx * ny);
        int iy = rem / nx;
        int ix = rem % nx;
        Vec3 pos = probePos(ix, iy, iz);
        uint32_t rng = pcgHash(uint32_t(p * 1973 + 91));
        float rotPhi = rand01(rng) * TWO_PI;

        for (int r = 0; r < raysPerProbe; ++r) {
          float y = 1.0f - (2.0f * (r + 0.5f)) / float(raysPerProbe);
          float rad = std::sqrt(std::max(0.0f, 1.0f - y * y));
          float phi = rotPhi + float(r) * 2.39996323f;
          Vec3 dir(std::cos(phi) * rad, std::sin(phi) * rad, y);

          HitInfo h = traceBvh(sc, pos, dir, 32.0f, -1, false);
          RayHitCache& rc = cache[size_t(p) * raysPerProbe + r];
          rc.dir = dir;
          if (!h.hit) {
            rc.hit = false;
            rc.dist = maxRayDist;
            rc.directRad = sc.evalSimpleSky(dir);
            continue;
          }
          bool backfacing = dot(dir, h.normal) > 0.25f;
          rc.hit = true;
          rc.dist = backfacing ? 0.05f : std::min(h.t, maxRayDist);
          rc.pos = h.pos;
          rc.normal = h.normal;
          Material hm = sc.sampleMaterial(h.matId, h.pos);
          rc.isEmissive = hm.isEmissive;
          rc.baseColor = hm.baseColor;
          if (backfacing) {
            rc.directRad = Vec3(0.0f);
          } else if (hm.isEmissive) {
            rc.directRad = hm.emission;
          } else {
            Vec3 lo = sc.evalSimpleSky(h.normal) * hm.baseColor * 0.03f;
            if (sc.sunEnabled && dot(h.normal, sc.sunDir) > 0.002f) {
              float sv = sh.evalSolarShadow(sc, h.pos, h.normal, h.primIdx, float(r * 7), float(p));
              lo += evalOpenPbrDirect(h.normal, -dir, sc.sunDir, hm) * sc.sunIrradiance * sv;
            }
            if (sc.areaLight.enabled) {
              float av = sh.evalAreaLightShadow(sc, h.pos, h.normal, h.primIdx, float(r * 11), float(p));
              lo += evalEmissivePanelLtc(h.pos, h.normal, -dir, hm, sc.areaLight, av);
            }
            lo += sh.evalEmissivePrimsDirect(sc, h.pos, h.normal, -dir, hm, h.primIdx, float(r * 5), float(p), false);
            rc.directRad = clamp3(lo, 0.0f, 18.0f);
          }
        }
      }
    };

    int nThreads = std::max(1, int(std::thread::hardware_concurrency()));
    std::vector<std::thread> threads;
    int chunk = (total + nThreads - 1) / nThreads;
    for (int t = 0; t < nThreads; ++t) {
      int s = t * chunk, e = std::min(total, s + chunk);
      if (s < e) threads.emplace_back(traceRange, s, e);
    }
    for (auto& th : threads) th.join();

    // Bake Chebyshev depth moments once
    for (int p = 0; p < total; ++p) {
      for (int y = 0; y < DIST_RES; ++y) {
        for (int x = 0; x < DIST_RES; ++x) {
          Vec3 tDir = octDecode((x + 0.5f) / DIST_RES, (y + 0.5f) / DIST_RES);
          float wSum = 0.0f, dSum = 0.0f, d2Sum = 0.0f;
          for (int r = 0; r < raysPerProbe; ++r) {
            const RayHitCache& rc = cache[size_t(p) * raysPerProbe + r];
            float cosA = dot(tDir, rc.dir);
            if (cosA <= 0.0f) continue;
            float w = std::pow(cosA, 24.0f);
            dSum += w * rc.dist;
            d2Sum += w * (rc.dist * rc.dist);
            wSum += w;
          }
          if (wSum > 1e-5f) {
            distOct[size_t(p) * DIST_RES * DIST_RES + y * DIST_RES + x] = {dSum / wSum, d2Sum / wSum};
          }
        }
      }
    }

    // Iterative multi-bounce irradiance bake
    std::vector<Vec3> nextIrr(irrOct.size(), Vec3(0.0f));
    for (int b = 0; b < bounces; ++b) {
      for (int p = 0; p < total; ++p) {
        std::vector<Vec3> rayRad(raysPerProbe);
        for (int r = 0; r < raysPerProbe; ++r) {
          const RayHitCache& rc = cache[size_t(p) * raysPerProbe + r];
          Vec3 rad = rc.directRad;
          if (b > 0 && rc.hit && !rc.isEmissive && rc.dist > 0.06f) {
            Vec3 bounceIrr = evalIrradiance(rc.pos + rc.normal * 0.04f, rc.normal, rc.dir);
            rad += bounceIrr * (rc.baseColor / PI) * 0.85f;
          }
          rayRad[r] = clamp3(rad, 0.0f, 18.0f);
        }

        for (int y = 0; y < IRR_RES; ++y) {
          for (int x = 0; x < IRR_RES; ++x) {
            Vec3 tDir = octDecode((x + 0.5f) / IRR_RES, (y + 0.5f) / IRR_RES);
            Vec3 sumIrr(0.0f);
            float wSum = 0.0f;
            for (int r = 0; r < raysPerProbe; ++r) {
              float w = std::max(0.0f, dot(tDir, cache[size_t(p) * raysPerProbe + r].dir));
              if (w > 0.0f) {
                sumIrr += rayRad[r] * w;
                wSum += w;
              }
            }
            nextIrr[size_t(p) * IRR_RES * IRR_RES + y * IRR_RES + x] =
                (wSum > 1e-5f) ? (sumIrr * (PI / wSum)) : Vec3(0.0f);
          }
        }
      }
      irrOct.swap(nextIrr);
    }
  }

  Vec3 evalIrradiance(const Vec3& pos, const Vec3& N, const Vec3& V) const {
    Vec3 biasedPos = pos + N * 0.06f + V * 0.02f;
    Vec3 gridCoord = div3(biasedPos - origin, spacing);
    int bx = clampi(int(std::floor(gridCoord.x)), 0, nx - 2);
    int by = clampi(int(std::floor(gridCoord.y)), 0, ny - 2);
    int bz = clampi(int(std::floor(gridCoord.z)), 0, nz - 2);
    Vec3 alpha = clamp3(gridCoord - Vec3(float(bx), float(by), float(bz)), 0.0f, 1.0f);

    Vec3 sumIrr(0.0f);
    float sumW = 0.0f;

    for (int i = 0; i < 8; ++i) {
      int ox = i & 1, oy = (i >> 1) & 1, oz = (i >> 2) & 1;
      int ix = bx + ox, iy = by + oy, iz = bz + oz;
      Vec3 pPos = probePos(ix, iy, iz);
      int pIdx = probeIndex(ix, iy, iz);

      float trilinear = ((ox ? alpha.x : 1.0f - alpha.x) *
                         (oy ? alpha.y : 1.0f - alpha.y) *
                         (oz ? alpha.z : 1.0f - alpha.z));

      Vec3 pToSurf = biasedPos - pPos;
      float dist = length(pToSurf);
      Vec3 dirToProbe = (dist > 1e-4f) ? (-pToSurf / dist) : N;

      float wrapN = clampf((dot(dirToProbe, N) + 1.0f) * 0.5f, 0.0f, 1.0f);
      float w = trilinear * (wrapN * wrapN + 0.05f);

      Vec2 moments = sampleDistOct(pIdx, -dirToProbe);
      float mean = moments.x;
      float variance = std::max(0.04f, std::abs(moments.y - mean * mean));
      float cheb = 1.0f;
      if (dist > mean) {
        float d = dist - mean;
        cheb = variance / (variance + d * d);
        cheb = std::max(cheb * cheb * cheb, 0.02f);
      }
      w *= cheb;

      if (w > 1e-5f) {
        sumIrr += sampleIrradianceOct(pIdx, N) * w;
        sumW += w;
      }
    }
    return (sumW > 1e-5f) ? (sumIrr / sumW) : Vec3(0.0f);
  }
};

// ============================================================================
// 3. 3D Radiosity SDF Mip-Volume (SDF used STRICTLY for GI only!)
// ============================================================================
struct SdfRadiosityVolume {
  Vec3 bmin{-3.15f, -2.50f, -0.95f};
  Vec3 bmax{ 3.15f,  2.85f,  2.90f};
  int nx = 64, ny = 56, nz = 40;
  Vec3 voxelSize;
  std::vector<float> sdf;
  std::vector<Vec3> radMip0;
  std::vector<Vec3> radMip1;
  std::vector<Vec3> radMip2;
  int n1x = 32, n1y = 28, n1z = 20;
  int n2x = 16, n2y = 14, n2z = 10;

  void build(const Scene& sc, const ShadowSystem& sh) {
    voxelSize = div3(bmax - bmin, Vec3(float(nx - 1), float(ny - 1), float(nz - 1)));
    size_t total = size_t(nx) * ny * nz;
    sdf.resize(total);
    radMip0.assign(total, Vec3(0.0f));

    auto buildSlice = [&](int zStart, int zEnd) {
      for (int iz = zStart; iz < zEnd; ++iz) {
        for (int iy = 0; iy < ny; ++iy) {
          for (int ix = 0; ix < nx; ++ix) {
            Vec3 p = bmin + Vec3(ix * voxelSize.x, iy * voxelSize.y, iz * voxelSize.z);
            int matId = 0;
            float d = evalSceneSdfForGi(sc, p, matId);
            size_t idx = (size_t(iz) * ny + iy) * nx + ix;
            sdf[idx] = d;

            float maxV = std::max({voxelSize.x, voxelSize.y, voxelSize.z});
            if (d < maxV * 1.35f) {
              Material m = sc.sampleMaterial(matId, p);
              if (m.isEmissive) {
                radMip0[idx] = m.emission;
              } else {
                Vec3 N = evalSdfGiNormal(sc, p);
                Vec3 sp = p + N * std::max(0.0f, -d + 0.015f);
                Vec3 lo = sc.evalSimpleSky(N) * m.baseColor * 0.025f;
                if (sc.sunEnabled && dot(N, sc.sunDir) > 0.002f) {
                  float sv = sh.evalSolarShadow(sc, sp, N, -1, float(ix * 3), float(iy * 5));
                  lo += m.baseColor * (dot(N, sc.sunDir) / PI) * sc.sunIrradiance * sv;
                }
                if (sc.areaLight.enabled) {
                  float av = sh.evalAreaLightShadow(sc, sp, N, -1, float(ix * 7), float(iy * 11));
                  lo += evalEmissivePanelLtc(sp, N, N, m, sc.areaLight, av);
                }
                lo += sh.evalEmissivePrimsDirect(sc, sp, N, N, m, -1, float(ix), float(iy), false);
                radMip0[idx] = clamp3(lo, 0.0f, 16.0f);
              }
            }
          }
        }
      }
    };

    int nThreads = std::max(1, int(std::thread::hardware_concurrency()));
    std::vector<std::thread> threads;
    int chunk = (nz + nThreads - 1) / nThreads;
    for (int t = 0; t < nThreads; ++t) {
      int s = t * chunk, e = std::min(nz, s + chunk);
      if (s < e) threads.emplace_back(buildSlice, s, e);
    }
    for (auto& th : threads) th.join();

    radMip1.assign(size_t(n1x) * n1y * n1z, Vec3(0.0f));
    for (int z = 0; z < n1z; ++z) {
      for (int y = 0; y < n1y; ++y) {
        for (int x = 0; x < n1x; ++x) {
          Vec3 sum(0.0f);
          float w = 0.0f;
          for (int dz = 0; dz < 2; ++dz)
            for (int dy = 0; dy < 2; ++dy)
              for (int dx = 0; dx < 2; ++dx) {
                int sx = std::min(x * 2 + dx, nx - 1);
                int sy = std::min(y * 2 + dy, ny - 1);
                int sz = std::min(z * 2 + dz, nz - 1);
                Vec3 r = radMip0[(size_t(sz) * ny + sy) * nx + sx];
                if (maxComp(r) > 1e-4f) { sum += r; w += 1.0f; }
              }
          radMip1[(size_t(z) * n1y + y) * n1x + x] = (w > 0.0f) ? (sum / std::max(1.0f, w * 0.65f + 2.8f * 0.35f)) : Vec3(0.0f);
        }
      }
    }

    radMip2.assign(size_t(n2x) * n2y * n2z, Vec3(0.0f));
    for (int z = 0; z < n2z; ++z) {
      for (int y = 0; y < n2y; ++y) {
        for (int x = 0; x < n2x; ++x) {
          Vec3 sum(0.0f);
          float w = 0.0f;
          for (int dz = 0; dz < 2; ++dz)
            for (int dy = 0; dy < 2; ++dy)
              for (int dx = 0; dx < 2; ++dx) {
                int sx = std::min(x * 2 + dx, n1x - 1);
                int sy = std::min(y * 2 + dy, n1y - 1);
                int sz = std::min(z * 2 + dz, n1z - 1);
                Vec3 r = radMip1[(size_t(sz) * n1y + sy) * n1x + sx];
                if (maxComp(r) > 1e-4f) { sum += r; w += 1.0f; }
              }
          radMip2[(size_t(z) * n2y + y) * n2x + x] = (w > 0.0f) ? (sum / std::max(1.0f, w * 0.65f + 2.8f * 0.35f)) : Vec3(0.0f);
        }
      }
    }
  }

  template <typename T>
  T sampleTrilinear(const std::vector<T>& grid, int gx, int gy, int gz, const Vec3& uvw) const {
    float fx = clampf(uvw.x, 0.0f, 0.9999f) * (gx - 1);
    float fy = clampf(uvw.y, 0.0f, 0.9999f) * (gy - 1);
    float fz = clampf(uvw.z, 0.0f, 0.9999f) * (gz - 1);
    int x0 = int(fx), x1 = std::min(x0 + 1, gx - 1);
    int y0 = int(fy), y1 = std::min(y0 + 1, gy - 1);
    int z0 = int(fz), z1 = std::min(z0 + 1, gz - 1);
    float tx = fx - x0, ty = fy - y0, tz = fz - z0;
    auto at = [&](int x, int y, int z) -> const T& { return grid[(size_t(z) * gy + y) * gx + x]; };
    T c00 = at(x0, y0, z0) * (1 - tx) + at(x1, y0, z0) * tx;
    T c10 = at(x0, y1, z0) * (1 - tx) + at(x1, y1, z0) * tx;
    T c01 = at(x0, y0, z1) * (1 - tx) + at(x1, y0, z1) * tx;
    T c11 = at(x0, y1, z1) * (1 - tx) + at(x1, y1, z1) * tx;
    return (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
  }

  bool inBounds(const Vec3& p) const {
    return p.x >= bmin.x && p.x <= bmax.x && p.y >= bmin.y && p.y <= bmax.y && p.z >= bmin.z && p.z <= bmax.z;
  }

  Vec3 worldToUvw(const Vec3& p) const {
    return div3(p - bmin, bmax - bmin);
  }

  Vec3 evalSdfConeGi(const Scene& sc, const Vec3& pos, const Vec3& N, int rayCount, float px, float py) const {
    Vec3 T, B;
    buildBasis(N, T, B);
    float r0 = ignHash(px, py);
    float r1 = ignHash(px + 29.3f, py + 17.7f);
    Vec3 irr(0.0f);
    float invR = 1.0f / float(rayCount);

    for (int i = 0; i < rayCount; ++i) {
      float u1 = fractf(r0 + (float(i) + 0.5f) * invR);
      float u2 = fractf(r1 + float(i) * 0.61803399f);
      float phi = TWO_PI * u2;
      float r = std::sqrt(clampf(u1, 0.02f, 0.98f));
      float z = std::sqrt(std::max(0.04f, 1.0f - r * r));
      Vec3 dir = normalize(T * (std::cos(phi) * r) + B * (std::sin(phi) * r) + N * z);

      float t = 0.08f;
      Vec3 rayRad(0.0f);
      bool hitSurface = false;
      for (int st = 0; st < 28; ++st) {
        Vec3 sp = pos + N * 0.045f + dir * t;
        if (!inBounds(sp)) {
          rayRad = sc.evalSimpleSky(dir);
          break;
        }
        Vec3 uvw = worldToUvw(sp);
        float d = sampleTrilinear(sdf, nx, ny, nz, uvw);
        float coneRadius = 0.06f + t * 0.28f;
        if (d < std::max(0.035f, coneRadius * 0.55f)) {
          Vec3 rad0 = sampleTrilinear(radMip0, nx, ny, nz, uvw);
          Vec3 rad1 = sampleTrilinear(radMip1, n1x, n1y, n1z, uvw);
          Vec3 rad2 = sampleTrilinear(radMip2, n2x, n2y, n2z, uvw);
          Vec3 rad = (coneRadius < 0.14f) ? rad0 : ((coneRadius < 0.34f) ? mix3(rad0, rad1, 0.65f) : mix3(rad1, rad2, 0.65f));
          rayRad = clamp3(rad, 0.0f, 6.5f);
          hitSurface = true;
          break;
        }
        t += std::max(0.06f, d * 0.85f);
        if (t > 6.0f) {
          rayRad = sc.evalSimpleSky(dir);
          break;
        }
      }
      if (!hitSurface && maxComp(rayRad) == 0.0f) {
        rayRad = sc.evalSimpleSky(dir);
      }
      irr += rayRad;
    }
    return irr * invR * PI;
  }
};

// ============================================================================
// 4. Joint Bilateral Filter (GI_BILATERAL) & A-Trous Wavelet Denoiser
// ============================================================================
inline void applyGiBilateralFilter(const std::vector<Vec3>& giIn, std::vector<Vec3>& giOut,
                                   const std::vector<GBufferSample>& gbuf, int w, int h) {
  giOut.resize(giIn.size());
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t idx = size_t(y) * w + x;
      const GBufferSample& c = gbuf[idx];
      if (!c.hit) { giOut[idx] = Vec3(0.0f); continue; }
      Vec3 sum(0.0f);
      float wSum = 0.0f;
      for (int dy = -3; dy <= 3; ++dy) {
        int yy = clampi(y + dy, 0, h - 1);
        for (int dx = -3; dx <= 3; ++dx) {
          int xx = clampi(x + dx, 0, w - 1);
          size_t nIdx = size_t(yy) * w + xx;
          const GBufferSample& n = gbuf[nIdx];
          if (!n.hit) continue;
          float spatialW = std::exp(-float(dx * dx + dy * dy) / 11.0f);
          float nDot = std::max(0.0f, dot(c.normal, n.normal));
          float normW = std::pow(nDot, 16.0f);
          float dRel = std::abs(c.depth - n.depth) / std::max(0.15f, c.depth);
          float depthW = std::exp(-dRel * dRel * 18.0f);
          float weight = spatialW * normW * depthW;
          sum += giIn[nIdx] * weight;
          wSum += weight;
        }
      }
      giOut[idx] = (wSum > 1e-5f) ? (sum / wSum) : giIn[idx];
    }
  }
}

inline void applyATrousDenoiser(std::vector<Vec3>& color, const std::vector<GBufferSample>& gbuf,
                                int w, int h, int passes = 3) {
  // Pass 0: isolated firefly clamp on non-emissive surfaces
  {
    std::vector<Vec3> desp = color;
    for (int y = 1; y < h - 1; ++y) {
      for (int x = 1; x < w - 1; ++x) {
        size_t idx = size_t(y) * w + x;
        const GBufferSample& c = gbuf[idx];
        if (!c.hit || (c.matId >= 9 && c.matId <= 11)) continue;
        float lumC = luminance(color[idx]);
        Vec3 nAvg(0.0f);
        float maxN = 0.0f;
        int cnt = 0;
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            size_t nIdx = size_t(y + dy) * w + (x + dx);
            if (!gbuf[nIdx].hit || gbuf[nIdx].matId != c.matId) continue;
            nAvg += color[nIdx];
            maxN = std::max(maxN, luminance(color[nIdx]));
            cnt++;
          }
        }
        if (cnt >= 4) {
          nAvg = nAvg / float(cnt);
          float lumAvg = luminance(nAvg);
          if (lumC > std::max(0.5f, lumAvg * 2.8f)) {
            desp[idx] = nAvg;
          }
        }
      }
    }
    color.swap(desp);
  }

  static constexpr float kKernel[3] = {1.0f, 2.0f / 3.0f, 1.0f / 6.0f};
  std::vector<Vec3> tmp(color.size());
  for (int pass = 0; pass < passes; ++pass) {
    int step = 1 << pass;
    float cPhi = 0.85f * std::pow(0.55f, float(pass));
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        size_t idx = size_t(y) * w + x;
        const GBufferSample& c = gbuf[idx];
        // Keep bright direct emitters crisp
        if (!c.hit || maxComp(color[idx]) > 8.0f) {
          tmp[idx] = color[idx];
          continue;
        }
        Vec3 cCol = color[idx];
        Vec3 sum(0.0f);
        float wSum = 0.0f;
        for (int dy = -2; dy <= 2; ++dy) {
          int yy = clampi(y + dy * step, 0, h - 1);
          for (int dx = -2; dx <= 2; ++dx) {
            int xx = clampi(x + dx * step, 0, w - 1);
            size_t nIdx = size_t(yy) * w + xx;
            const GBufferSample& n = gbuf[nIdx];
            if (!n.hit) continue;
            float kw = kKernel[std::abs(dx)] * kKernel[std::abs(dy)];
            float nw = std::pow(std::max(0.0f, dot(c.normal, n.normal)), 24.0f);
            float dw = std::exp(-std::abs(c.depth - n.depth) / std::max(0.10f, c.depth * 0.12f));
            Vec3 diff = cCol - color[nIdx];
            float cw = std::exp(-dot(diff, diff) / std::max(0.015f, cPhi * cPhi * (0.20f + luminance(cCol))));
            Vec3 aDiff = c.baseColor - n.baseColor;
            float aw = (c.matId == n.matId ? 1.0f : 0.20f) * std::exp(-dot(aDiff, aDiff) * 85.0f);
            float wTotal = kw * nw * dw * cw * aw;
            sum += color[nIdx] * wTotal;
            wSum += wTotal;
          }
        }
        tmp[idx] = (wSum > 1e-5f) ? (sum / wSum) : cCol;
      }
    }
    color.swap(tmp);
  }
}

// ============================================================================
// 5. Monte Carlo Path Tracer (MIS + NEE + GGX/Cosine, DeterministicPathTracer style)
// ============================================================================
inline Vec3 sampleCosineHemisphere(const Vec3& N, float u1, float u2, float& outPdf) {
  Vec3 T, B;
  buildBasis(N, T, B);
  float r = std::sqrt(u1);
  float phi = TWO_PI * u2;
  float x = r * std::cos(phi);
  float y = r * std::sin(phi);
  float z = std::sqrt(std::max(0.0f, 1.0f - u1));
  outPdf = std::max(1e-5f, z / PI);
  return normalize(T * x + B * y + N * z);
}

// Heitz (2018) Exact GGX VNDF Importance Sampling — bounded weight <= 1.0, zero grazing fireflies
inline Vec3 sampleGgxVndf(const Vec3& N, const Vec3& V, float rough, float u1, float u2, float& outPdfDir) {
  float a = std::max(0.0015f, rough * rough);
  Vec3 T, B;
  buildBasis(N, T, B);
  Vec3 Ve(dot(V, T), dot(V, B), std::max(1e-4f, dot(V, N)));
  Vec3 Vh = normalize(Vec3(a * Ve.x, a * Ve.y, Ve.z));

  float lensq = Vh.x * Vh.x + Vh.y * Vh.y;
  Vec3 T1 = lensq > 1e-7f ? Vec3(-Vh.y, Vh.x, 0.0f) / std::sqrt(lensq) : Vec3(1, 0, 0);
  Vec3 T2 = cross(Vh, T1);

  float r = std::sqrt(u1);
  float phi = TWO_PI * u2;
  float t1 = r * std::cos(phi);
  float t2 = r * std::sin(phi);
  float s = 0.5f * (1.0f + Vh.z);
  t2 = (1.0f - s) * std::sqrt(std::max(0.0f, 1.0f - t1 * t1)) + s * t2;

  Vec3 Nh = t1 * T1 + t2 * T2 + std::sqrt(std::max(0.0f, 1.0f - t1 * t1 - t2 * t2)) * Vh;
  Vec3 Ne = normalize(Vec3(a * Nh.x, a * Nh.y, std::max(0.0f, Nh.z)));
  Vec3 H = normalize(T * Ne.x + B * Ne.y + N * Ne.z);

  float noV = std::max(1e-4f, dot(N, V));
  float noH = std::max(1e-4f, dot(N, H));
  float a2 = a * a;
  float tan2 = (1.0f - noV * noV) / (noV * noV);
  float g1V = 2.0f / (1.0f + std::sqrt(1.0f + a2 * tan2));
  float d = evalDggx(noH, a);
  outPdfDir = std::max(1e-5f, (d * g1V) / (4.0f * noV));
  return H;
}

inline float powerHeuristic(float pdfA, float pdfB) {
  float a2 = pdfA * pdfA;
  return a2 / std::max(1e-8f, a2 + pdfB * pdfB);
}

inline Vec3 tracePathTracerPixel(const Scene& sc, const Vec3& ro, const Vec3& rd,
                                 int spp, int maxBounces, int px, int py) {
  Vec3 accum(0.0f);
  for (int s = 0; s < spp; ++s) {
    uint32_t rng = pcgHash(uint32_t((py * 4099 + px) * 131 + s * 9973 + 17));
    Vec3 rayO = ro;
    Vec3 rayD = rd;
    if (spp > 1) {
      Vec3 camR = normalize(cross(rd, Vec3(0, 0, 1)));
      Vec3 camU = cross(camR, rd);
      float jx = (rand01(rng) - 0.5f) * 0.0022f;
      float jy = (rand01(rng) - 0.5f) * 0.0022f;
      rayD = normalize(rd + camR * jx + camU * jy);
    }

    Vec3 throughput(1.0f);
    Vec3 rad(0.0f);
    int ignorePrim = -1;
    float prevBrdfPdf = 0.0f;
    bool prevSpecular = true;

    for (int bounce = 0; bounce < maxBounces; ++bounce) {
      HitInfo h = traceBvh(sc, rayO, rayD, 64.0f, ignorePrim, false);
      if (!h.hit) {
        rad += throughput * sc.evalSimpleSky(rayD);
        if (sc.sunEnabled && bounce > 0) {
          float sunCos = dot(rayD, sc.sunDir);
          if (sunCos >= std::cos(sc.sunAngularRad)) {
            float sunSolidAngle = TWO_PI * (1.0f - std::cos(sc.sunAngularRad));
            float sunPdf = 1.0f / std::max(1e-6f, sunSolidAngle);
            float mis = prevSpecular ? 1.0f : powerHeuristic(prevBrdfPdf, sunPdf);
            rad += throughput * (sc.sunIrradiance / std::max(0.05f, sunSolidAngle)) * mis * 0.15f;
          }
        }
        break;
      }

      Material mat = sc.sampleMaterial(h.matId, h.pos);
      if (mat.isEmissive) {
        float mis = (bounce == 0 || prevSpecular) ? 1.0f : 0.35f;
        rad += throughput * mat.emission * mis;
        break;
      }

      Vec3 V = -rayD;
      Vec3 N = h.normal;
      Vec3 origin = h.pos + N * 0.004f;

      // 1. Sun NEE
      if (sc.sunEnabled && dot(N, sc.sunDir) > 0.002f) {
        Vec3 sT, sB;
        buildBasis(sc.sunDir, sT, sB);
        float sr = std::sqrt(rand01(rng)) * std::tan(sc.sunAngularRad);
        float sa = rand01(rng) * TWO_PI;
        Vec3 sDir = normalize(sc.sunDir + (sT * std::cos(sa) + sB * std::sin(sa)) * sr);
        if (dot(N, sDir) > 0.001f && !traceBvhAnyHit(sc, origin, sDir, 36.0f, h.primIdx, -1)) {
          Vec3 brdf = evalOpenPbrDirect(N, V, sDir, mat);
          float sunSolidAngle = TWO_PI * (1.0f - std::cos(sc.sunAngularRad));
          float sunPdf = 1.0f / std::max(1e-6f, sunSolidAngle);
          float specProb = clampf(0.15f + mat.metallic * 0.7f + (1.0f - mat.roughness) * 0.35f, 0.1f, 0.9f);
          float dPdf = std::max(1e-5f, dot(N, sDir) / PI);
          float brdfPdf = mixf(dPdf, dPdf * 2.0f, specProb);
          float mis = powerHeuristic(sunPdf, brdfPdf);
          rad += throughput * brdf * sc.sunIrradiance * mis;
        }
      }

      // 2. Area Light NEE
      if (sc.areaLight.enabled) {
        float sx = (rand01(rng) * 2.0f - 1.0f) * sc.areaLight.hw;
        float sy = (rand01(rng) * 2.0f - 1.0f) * sc.areaLight.hh;
        Vec3 lp = sc.areaLight.center + sc.areaLight.right * sx + sc.areaLight.up * sy;
        Vec3 toL = lp - origin;
        float dist2 = std::max(0.02f, dot(toL, toL));
        float dist = std::sqrt(dist2);
        Vec3 lDir = toL / dist;
        float cosL = std::max(0.0f, dot(sc.areaLight.normal, -lDir));
        float cosS = dot(N, lDir);
        if (cosL > 0.001f && cosS > 0.001f) {
          if (!traceBvhAnyHit(sc, origin, lDir, std::max(0.02f, dist - 0.12f), h.primIdx, 11)) {
            float area = 4.0f * sc.areaLight.hw * sc.areaLight.hh;
            float pdfArea = dist2 / std::max(1e-5f, area * cosL);
            Vec3 brdf = evalOpenPbrDirect(N, V, lDir, mat);
            float dPdf = cosS / PI;
            float mis = powerHeuristic(pdfArea, dPdf);
            rad += throughput * brdf * (sc.areaLight.radiance / std::max(1e-4f, pdfArea)) * mis * 0.32f;
          }
        }
      }

      // 3. Emissive Primitives NEE (Orb + Neon Capsule)
      for (size_t i = 0; i < sc.prims.size(); ++i) {
        if (static_cast<int>(i) == h.primIdx || static_cast<int>(i) == sc.areaLight.skipPrimId) continue;
        const auto& ep = sc.prims[i];
        const Material& em = sc.materials[ep.matId];
        if (!em.isEmissive) continue;
        if (ep.type == PrimType::Sphere) {
          Vec3 toC = ep.center - origin;
          float dC = length(toC);
          Vec3 cDir = toC / std::max(1e-4f, dC);
          float r = ep.halfExtents.x;
          float sinMax2 = clampf((r * r) / std::max(r * r + 0.01f, dC * dC), 0.0f, 0.999f);
          float cosMax = std::sqrt(std::max(0.0f, 1.0f - sinMax2));
          float u1 = rand01(rng), u2 = rand01(rng);
          float cosA = 1.0f - u1 + u1 * cosMax;
          float sinA = std::sqrt(std::max(0.0f, 1.0f - cosA * cosA));
          float phi = TWO_PI * u2;
          Vec3 sT, sB; buildBasis(cDir, sT, sB);
          Vec3 lDir = normalize(sT * (std::cos(phi) * sinA) + sB * (std::sin(phi) * sinA) + cDir * cosA);
          if (dot(N, lDir) > 0.001f) {
            if (!traceBvhAnyHit(sc, origin, lDir, std::max(0.02f, dC - r - 0.03f), h.primIdx, ep.matId)) {
              float solidAngle = TWO_PI * (1.0f - cosMax);
              Vec3 brdf = evalOpenPbrDirect(N, V, lDir, mat);
              rad += throughput * brdf * em.emission * solidAngle * 0.55f;
            }
          }
        } else if (ep.type == PrimType::Capsule) {
          float tSeg = rand01(rng);
          Vec3 pt = mix3(ep.center, ep.p1, tSeg);
          Vec3 toE = pt - origin;
          float dist = length(toE);
          Vec3 lDir = toE / std::max(1e-4f, dist);
          float r = ep.halfExtents.x;
          if (dot(N, lDir) > 0.001f) {
            if (!traceBvhAnyHit(sc, origin, lDir, std::max(0.02f, dist - r - 0.05f), h.primIdx, ep.matId)) {
              float segLen = length(ep.p1 - ep.center);
              float area = TWO_PI * r * segLen;
              float solidAngle = std::min(1.6f, area / std::max(0.10f, 4.0f * dist * dist));
              rad += throughput * evalOpenPbrDirect(N, V, lDir, mat) * em.emission * (solidAngle * 0.75f);
            }
          }
        }
      }

      // Next bounce (Specular GGX vs Diffuse Cosine)
      float f0Lum = mixf(0.04f, luminance(mat.baseColor), mat.metallic);
      float specProb = (mat.metallic > 0.80f && mat.roughness < 0.20f)
          ? 0.995f
          : clampf(f0Lum + mat.coat * 0.25f + (1.0f - mat.roughness) * 0.25f, 0.10f, 0.92f);
      float uPick = rand01(rng);
      float u1 = rand01(rng), u2 = rand01(rng);

      Vec3 nextDir;
      float pdf = 1.0f;
      if (uPick < specProb) {
        float pdfSpec = 1.0f;
        Vec3 H = sampleGgxVndf(N, V, mat.roughness, u1, u2, pdfSpec);
        nextDir = reflect3(-V, H);
        if (dot(N, nextDir) <= 0.001f) break;
        float voh = std::max(0.02f, dot(V, H));
        float pdfDiff = std::max(1e-5f, dot(N, nextDir) / PI);
        pdf = specProb * pdfSpec + (1.0f - specProb) * pdfDiff;

        float a = std::max(0.0015f, mat.roughness * mat.roughness);
        float noL = std::max(0.01f, dot(N, nextDir));
        float noV = std::max(0.01f, dot(N, V));
        float noH = std::max(0.01f, dot(N, H));
        Vec3 F0 = mix3(Vec3(0.04f), mat.baseColor, mat.metallic);
        Vec3 F = F0 + (Vec3(1.0f) - F0) * std::pow(std::max(1.0f - voh, 1e-4f), 5.0f);
        float G = evalGsmith(noV, noL, mat.roughness);
        float D = evalDggx(noH, a);
        Vec3 specBrdf = F * (D * G / std::max(1e-4f, 4.0f * noV * noL));
        Vec3 diffBrdf = mat.baseColor * (1.0f - mat.metallic) * (Vec3(1.0f) - F) / PI;
        Vec3 stepWeight = (specBrdf + diffBrdf) * (noL / std::max(1e-5f, pdf));
        throughput *= clamp3(stepWeight, 0.0f, 3.0f);
        prevSpecular = (mat.roughness < 0.18f);
      } else {
        float pdfDiff = 1.0f;
        nextDir = sampleCosineHemisphere(N, u1, u2, pdfDiff);
        float noL = std::max(0.01f, dot(N, nextDir));
        float noV = std::max(0.01f, dot(N, V));
        Vec3 H = normalize(V + nextDir);
        float noH = std::max(0.01f, dot(N, H));
        float voh = std::max(0.02f, dot(V, H));
        float a = std::max(0.0015f, mat.roughness * mat.roughness);
        float a2 = a * a;
        float tan2 = (1.0f - noV * noV) / (noV * noV);
        float g1V = 2.0f / (1.0f + std::sqrt(1.0f + a2 * tan2));
        float pdfSpec = (evalDggx(noH, a) * g1V) / std::max(1e-5f, 4.0f * noV);
        pdf = specProb * pdfSpec + (1.0f - specProb) * pdfDiff;

        Vec3 F0 = mix3(Vec3(0.04f), mat.baseColor, mat.metallic);
        Vec3 F = F0 + (Vec3(1.0f) - F0) * std::pow(std::max(1.0f - voh, 1e-4f), 5.0f);
        Vec3 diffBrdf = mat.baseColor * (1.0f - mat.metallic) * (Vec3(1.0f) - F) / PI;
        Vec3 stepWeight = diffBrdf * (noL / std::max(1e-5f, pdf));
        throughput *= clamp3(stepWeight, 0.0f, 3.0f);
        prevSpecular = false;
      }

      throughput = clamp3(throughput, 0.0f, 4.0f);
      prevBrdfPdf = pdf;
      if (bounce >= 2) {
        float survive = clampf(maxComp(throughput), 0.08f, 0.95f);
        if (rand01(rng) > survive) break;
        throughput = throughput / survive;
      }
      rayO = origin;
      rayD = nextDir;
      ignorePrim = h.primIdx;
    }
    accum += clamp3(rad, 0.0f, 18.0f);
  }
  return accum / float(std::max(1, spp));
}

// ============================================================================
// Master Multi-Renderer Driver
// ============================================================================
class MultiRenderer {
public:
  const Scene& sc;
  ShadowSystem shadowSys;
  DdgiVolume ddgi;
  SdfRadiosityVolume sdfGiVol;
  bool mapsBuilt = false;
  bool ddgiBuilt = false;
  bool sdfGiBuilt = false;

  explicit MultiRenderer(const Scene& s) : sc(s) {}

  void prepare(const RenderConfig& cfg) {
    shadowSys.alg = cfg.shadowAlg;
    if (cfg.engine != RenderEngine::MatCap && !mapsBuilt) {
      shadowSys.rasterizeShadowMaps(sc, 640, 384);
      mapsBuilt = true;
    }
    if (cfg.engine == RenderEngine::Ddgi && !ddgiBuilt) {
      ddgi.bake(sc, shadowSys, 128, 2);
      ddgiBuilt = true;
    }
    if (cfg.engine == RenderEngine::SdfGi && !sdfGiBuilt) {
      sdfGiVol.build(sc, shadowSys);
      sdfGiBuilt = true;
    }
  }

  std::vector<Vec3> render(const RenderConfig& cfg, double& outMs) {
    auto t0 = std::chrono::steady_clock::now();
    prepare(cfg);

    int W = cfg.width, H = cfg.height;
    std::vector<GBufferSample> gbuf(size_t(W) * H);
    std::vector<Vec3> directBuf(size_t(W) * H, Vec3(0.0f));
    std::vector<Vec3> giBuf(size_t(W) * H, Vec3(0.0f));
    std::vector<Vec3> finalHdr(size_t(W) * H, Vec3(0.0f));

    const Vec3 camPos(0.0f, -3.55f, 0.72f);
    const Vec3 camTarget(0.0f, 0.35f, 0.25f);
    const float fovDeg = 48.0f;
    Vec3 camFwd = normalize(camTarget - camPos);
    Vec3 camR = normalize(cross(camFwd, Vec3(0, 0, 1)));
    Vec3 camU = cross(camR, camFwd);
    float tanHalf = std::tan(fovDeg * 0.5f * PI / 180.0f);
    float aspect = float(W) / float(H);

    auto shadeDirectAndRefl = [&](const HitInfo& h, const Vec3& V, const Material& mat, float px, float py, bool doRefl) {
      if (mat.isEmissive) return mat.emission;
      Vec3 lo(0.0f);
      if (sc.sunEnabled && dot(h.normal, sc.sunDir) > 0.002f) {
        float sv = shadowSys.evalSolarShadow(sc, h.pos, h.normal, h.primIdx, px, py);
        lo += evalOpenPbrDirect(h.normal, V, sc.sunDir, mat) * sc.sunIrradiance * sv;
      }
      if (sc.areaLight.enabled) {
        float av = shadowSys.evalAreaLightShadow(sc, h.pos, h.normal, h.primIdx, px, py);
        lo += evalEmissivePanelLtc(h.pos, h.normal, V, mat, sc.areaLight, av);
      }
      lo += shadowSys.evalEmissivePrimsDirect(sc, h.pos, h.normal, V, mat, h.primIdx, px, py, true);

      if (doRefl) {
        float mRefl = clampf((mat.metallic * 0.85f + mat.coat * 0.50f) * (1.0f - mat.roughness * 0.88f), 0.0f, 0.96f);
        if (mRefl > 0.02f) {
          Vec3 R = reflect3(-V, h.normal);
          Vec3 rCol(0.0f);
          if (cfg.reflAlg == ReflAlg::Bvh) {
            rCol = shadowSys.traceBvhReflection(sc, h.pos, h.normal, R, cfg.reflBounces, h.primIdx,
              [&](const HitInfo& shHit, const Vec3& shV) {
                Material shm = sc.sampleMaterial(shHit.matId, shHit.pos);
                if (shm.isEmissive) return shm.emission;
                Vec3 sLo = sc.evalSimpleSky(shHit.normal) * shm.baseColor * 0.03f;
                if (sc.sunEnabled && dot(shHit.normal, sc.sunDir) > 0.002f) {
                  float sv2 = shadowSys.evalSolarShadow(sc, shHit.pos, shHit.normal, shHit.primIdx, px, py);
                  sLo += evalOpenPbrDirect(shHit.normal, shV, sc.sunDir, shm) * sc.sunIrradiance * sv2;
                }
                if (sc.areaLight.enabled) {
                  float av2 = shadowSys.evalAreaLightShadow(sc, shHit.pos, shHit.normal, shHit.primIdx, px, py);
                  sLo += evalEmissivePanelLtc(shHit.pos, shHit.normal, shV, shm, sc.areaLight, av2);
                }
                return sLo;
              });
          } else {
            rCol = sc.evalSimpleSky(R);
          }
          float noV = std::max(0.02f, dot(h.normal, V));
          Vec3 F0 = mix3(Vec3(0.04f), mat.baseColor, mat.metallic);
          Vec3 F = F0 + (Vec3(1.0f) - F0) * std::pow(std::max(1.0f - noV, 1e-4f), 5.0f);
          lo += rCol * F * mRefl;
        }
      }
      return lo;
    };

    auto renderRows = [&](int yStart, int yEnd) {
      for (int y = yStart; y < yEnd; ++y) {
        float ny = 1.0f - 2.0f * (y + 0.5f) / float(H);
        for (int x = 0; x < W; ++x) {
          float nx = 2.0f * (x + 0.5f) / float(W) - 1.0f;
          Vec3 rd = normalize(camFwd + camR * (nx * aspect * tanHalf) + camU * (ny * tanHalf));
          size_t idx = size_t(y) * W + x;

          HitInfo h = traceBvh(sc, camPos, rd, 80.0f, -1, false);
          GBufferSample& gb = gbuf[idx];
          if (!h.hit) {
            finalHdr[idx] = (cfg.engine == RenderEngine::MatCap)
                ? Vec3(0.10f, 0.12f, 0.15f)
                : sc.evalSimpleSky(rd);
            continue;
          }

          Material mat = sc.sampleMaterial(h.matId, h.pos);
          gb.hit = true;
          gb.pos = h.pos;
          gb.normal = h.normal;
          gb.depth = h.t;
          gb.matId = h.matId;
          gb.primIdx = h.primIdx;
          gb.baseColor = mat.baseColor;
          gb.rough = mat.roughness;
          gb.metal = mat.metallic;

          Vec3 V = -rd;
          if (cfg.engine == RenderEngine::MatCap) {
            finalHdr[idx] = evalMatCapShader(h.normal, V, mat);
            continue;
          }

          if (cfg.engine == RenderEngine::PathTracer) {
            finalHdr[idx] = tracePathTracerPixel(sc, camPos, rd, cfg.ptSpp, cfg.ptMaxBounces, x, y);
            continue;
          }

          if (mat.isEmissive) {
            directBuf[idx] = mat.emission;
            continue;
          }

          directBuf[idx] = shadeDirectAndRefl(h, V, mat, float(x), float(y), true);

          if (cfg.engine == RenderEngine::PbrDirect) {
            giBuf[idx] = sc.evalSimpleSky(h.normal) * 0.08f;
          } else if (cfg.engine == RenderEngine::Ddgi) {
            giBuf[idx] = ddgi.evalIrradiance(h.pos, h.normal, V);
          } else if (cfg.engine == RenderEngine::SdfGi) {
            giBuf[idx] = sdfGiVol.evalSdfConeGi(sc, h.pos, h.normal, cfg.giRays, float(x), float(y));
          }
        }
      }
    };

    int nThreads = std::max(1, int(std::thread::hardware_concurrency()));
    std::vector<std::thread> threads;
    int chunk = (H + nThreads - 1) / nThreads;
    for (int t = 0; t < nThreads; ++t) {
      int s = t * chunk, e = std::min(H, s + chunk);
      if (s < e) threads.emplace_back(renderRows, s, e);
    }
    for (auto& th : threads) th.join();

    if (cfg.engine == RenderEngine::PathTracer) {
      if (cfg.ptDenoise) {
        applyATrousDenoiser(finalHdr, gbuf, W, H, 3);
      }
    } else if (cfg.engine != RenderEngine::MatCap) {
      std::vector<Vec3> giFiltered = giBuf;
      if (cfg.engine == RenderEngine::SdfGi && cfg.giBilateral) {
        applyGiBilateralFilter(giBuf, giFiltered, gbuf, W, H);
      }
      for (size_t i = 0; i < finalHdr.size(); ++i) {
        if (!gbuf[i].hit) continue;
        if (sc.materials[gbuf[i].matId].isEmissive) {
          finalHdr[i] = directBuf[i];
          continue;
        }
        Vec3 diffAlbedo = gbuf[i].baseColor * (1.0f - gbuf[i].metal);
        Vec3 indirect = giFiltered[i] * (diffAlbedo / PI);
        finalHdr[i] = directBuf[i] + indirect;
      }
    }

    auto t1 = std::chrono::steady_clock::now();
    outMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return finalHdr;
  }
};

} // namespace sdf_demo

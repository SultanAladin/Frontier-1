#pragma once
#include "SceneBvh.hpp"
#include <array>
#include <vector>

namespace sdf_demo {

enum class ShadowAlg {
  ContactPcss = 0,
  Pcss = 1,
  Smrt = 2,
  Pcf = 3,
  Bvh = 4,
};

enum class ReflAlg {
  Bvh = 1,
  Env = 2,
};

struct ShadowMap {
  int res = 512;
  std::vector<float> depth01;
  Mat4 vp;
  Vec3 eye{0.0f};
  Vec3 forward{0.0f, 0.0f, -1.0f};
  float nearZ = 0.2f;
  float farZ = 20.0f;
  float orthoHalfSpan = 4.5f;
  float perspTan = 1.0f;
  bool isArea = false;
  bool valid = false;

  float fetch(const Vec2& uv) const {
    int x = clampi(static_cast<int>(uv.x * res), 0, res - 1);
    int y = clampi(static_cast<int>(uv.y * res), 0, res - 1);
    return depth01[static_cast<size_t>(y) * res + x];
  }

  float decodeLinear(float d01) const {
    if (d01 >= 0.99995f) return 1e5f;
    if (!isArea) return nearZ + d01 * (farZ - nearZ);
    float zNdc = d01 * 2.0f - 1.0f;
    return (2.0f * nearZ * farZ) / std::max(1e-4f, farZ + nearZ - zNdc * (farZ - nearZ));
  }

  bool projectWorld(const Vec3& wp, Vec2& outUv, float& outRecD) const {
    Vec4 c = vp.mul({wp.x, wp.y, wp.z, 1.0f});
    if (c.w <= 1e-4f) {
      outUv = {-1.0f, -1.0f};
      outRecD = 1e6f;
      return false;
    }
    Vec3 ndc(c.x / c.w, c.y / c.w, c.z / c.w);
    outUv = {ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f};
    outRecD = dot(wp - eye, forward);
    return outUv.x >= 0.002f && outUv.x <= 0.998f && outUv.y >= 0.002f && outUv.y <= 0.998f &&
           ndc.z >= -1.0f && ndc.z <= 1.0f;
  }

  float worldSpan(float dEst) const {
    return isArea ? (2.0f * std::max(dEst, 0.25f) * perspTan) : (2.0f * orthoHalfSpan);
  }
};

inline Mat4 buildLookAtGl(const Vec3& eye, const Vec3& target, const Vec3& upHint) {
  Vec3 f = normalize(target - eye);
  Vec3 upUse = std::abs(dot(f, upHint)) > 0.98f ? Vec3(1, 0, 0) : upHint;
  Vec3 s = normalize(cross(f, upUse));
  Vec3 u = cross(s, f);
  Mat4 M = Mat4::identity();
  M.m[0] = s.x;  M.m[4] = s.y;  M.m[8]  = s.z;  M.m[12] = -dot(s, eye);
  M.m[1] = u.x;  M.m[5] = u.y;  M.m[9]  = u.z;  M.m[13] = -dot(u, eye);
  M.m[2] = -f.x; M.m[6] = -f.y; M.m[10] = -f.z; M.m[14] = dot(f, eye);
  return M;
}

inline Mat4 mulMat4(const Mat4& A, const Mat4& B) {
  Mat4 C;
  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float sum = 0.0f;
      for (int k = 0; k < 4; ++k) sum += A.m[k * 4 + row] * B.m[col * 4 + k];
      C.m[col * 4 + row] = sum;
    }
  }
  return C;
}

inline Mat4 buildOrthoGl(float l, float r, float b, float t, float n, float f) {
  Mat4 M;
  M.m[0] = 2.0f / (r - l);
  M.m[5] = 2.0f / (t - b);
  M.m[10] = -2.0f / (f - n);
  M.m[12] = -(r + l) / (r - l);
  M.m[13] = -(t + b) / (t - b);
  M.m[14] = -(f + n) / (f - n);
  M.m[15] = 1.0f;
  return M;
}

inline Mat4 buildPerspectiveGl(float fovYRad, float aspect, float n, float f) {
  float F = 1.0f / std::tan(fovYRad * 0.5f);
  Mat4 M;
  M.m[0] = F / aspect;
  M.m[5] = F;
  M.m[10] = (f + n) / (n - f);
  M.m[11] = -1.0f;
  M.m[14] = (2.0f * f * n) / (n - f);
  return M;
}

struct ShadowSystem {
  ShadowMap sunMap;
  ShadowMap areaMap;
  ShadowAlg alg = ShadowAlg::ContactPcss;
  int pcssTaps = 16;
  int smrtRays = 4;
  int bvhRays = 4;
  float softness = 1.0f;
  bool planeFilter = true;
  bool emissiveShadowed = true;

  void rasterizeShadowMaps(const Scene& sc, int sunRes = 640, int areaRes = 384) {
    if (sc.sunEnabled) {
      sunMap.res = sunRes;
      sunMap.isArea = false;
      sunMap.orthoHalfSpan = 4.6f;
      sunMap.nearZ = 0.5f;
      sunMap.farZ = 22.0f;
      Vec3 center(0.0f, 0.25f, 0.45f);
      sunMap.eye = center + sc.sunDir * 10.5f;
      sunMap.forward = -sc.sunDir;
      Mat4 V = buildLookAtGl(sunMap.eye, center, {0, 0, 1});
      Mat4 P = buildOrthoGl(-sunMap.orthoHalfSpan, sunMap.orthoHalfSpan,
                            -sunMap.orthoHalfSpan, sunMap.orthoHalfSpan, sunMap.nearZ, sunMap.farZ);
      sunMap.vp = mulMat4(P, V);
      renderDepthBvh(sc, sunMap, -1);
      sunMap.valid = true;
    } else {
      sunMap.valid = false;
    }

    if (sc.areaLight.enabled && emissiveShadowed) {
      areaMap.res = areaRes;
      areaMap.isArea = true;
      areaMap.nearZ = 0.20f;
      areaMap.farZ = 14.0f;
      float fov = 124.0f * PI / 180.0f;
      areaMap.perspTan = std::tan(fov * 0.5f);
      areaMap.eye = sc.areaLight.center - Vec3(0, 0, 0.14f);
      areaMap.forward = normalize(Vec3(0.0f, 0.0f, -1.0f));
      Mat4 V = buildLookAtGl(areaMap.eye, areaMap.eye + areaMap.forward, {0, 1, 0});
      Mat4 P = buildPerspectiveGl(fov, 1.0f, areaMap.nearZ, areaMap.farZ);
      areaMap.vp = mulMat4(P, V);
      renderDepthBvh(sc, areaMap, sc.areaLight.skipPrimId);
      areaMap.valid = true;
    } else {
      areaMap.valid = false;
    }
  }

  // Exact ShadowAlgorithmLab: SHADOW_PCF_FRAG
  float evalShadowMapPcf(const ShadowMap& sm, const Vec3& pos, const Vec3& N, const Vec3& toLight, float lRad) const {
    float ndl = dot(N, toLight);
    if (ndl <= 0.002f) return 0.0f;
    Vec2 uvEst; float dEst;
    if (!sm.projectWorld(pos, uvEst, dEst)) return 1.0f;
    float wSpan = sm.worldSpan(dEst);
    float texelWorld = wSpan / std::max(256.0f, float(sm.res));
    float slope = clampf(std::sqrt(std::max(0.0f, 1.0f - ndl * ndl)) / std::max(ndl, 0.04f), 0.0f, 4.0f);
    float biasM = texelWorld * (0.55f + 0.55f * slope);
    Vec2 uv; float recD;
    if (!sm.projectWorld(pos + N * (texelWorld * 1.65f), uv, recD)) return 1.0f;

    float tcX = uv.x * sm.res - 0.5f, tcY = uv.y * sm.res - 0.5f;
    float bx = std::floor(tcX), by = std::floor(tcY);
    float fx = tcX - bx, fy = tcY - by;
    float stride = 1.0f + clampf(lRad * 14.0f, 0.0f, 1.25f);

    float sumVis = 0.0f, sumW = 0.0f;
    for (int dy = -1; dy <= 1; ++dy) {
      float wy = (dy == 0) ? 1.0f : ((dy < 0) ? (1.0f - fy * 0.5f) : (0.5f + fy * 0.5f));
      for (int dx = -1; dx <= 1; ++dx) {
        float wx = (dx == 0) ? 1.0f : ((dx < 0) ? (1.0f - fx * 0.5f) : (0.5f + fx * 0.5f));
        float w = wx * wy;
        Vec2 qUv(clampf((bx + float(dx) * stride + 0.5f) / sm.res, 0.0f, 0.9999f),
                 clampf((by + float(dy) * stride + 0.5f) / sm.res, 0.0f, 0.9999f));
        float stored = sm.decodeLinear(sm.fetch(qUv));
        if (stored >= recD - biasM) sumVis += w;
        sumW += w;
      }
    }
    return sumVis / std::max(sumW, 1e-4f);
  }

  // Exact ShadowAlgorithmLab: SHADOW_PCSS_FRAG (+ PLANE_FILTER_FRAG)
  float evalShadowMapPcss(const ShadowMap& sm, const Vec3& pos, const Vec3& N, const Vec3& toLight,
                          float lRad, float px, float py, float& outBlk) const {
    outBlk = 0.0f;
    float ndl = dot(N, toLight);
    if (ndl <= 0.002f) return 0.0f;
    Vec2 uvEst; float dEst;
    if (!sm.projectWorld(pos, uvEst, dEst)) return 1.0f;
    float wSpan = sm.worldSpan(dEst);
    float texelWorld = wSpan / std::max(256.0f, float(sm.res));
    float texelUv = 1.0f / std::max(256.0f, float(sm.res));
    float slope = clampf(std::sqrt(std::max(0.0f, 1.0f - ndl * ndl)) / std::max(ndl, 0.04f), 0.0f, 4.0f);
    float biasM = texelWorld * (0.48f + 0.45f * slope);
    Vec2 uv; float recD;
    if (!sm.projectWorld(pos + N * (texelWorld * 1.40f), uv, recD)) return 1.0f;

    float phi0 = ignHash(px, py) * TWO_PI;
    float searchWorld = sm.isArea ? clampf(lRad * 0.75f, texelWorld * 2.5f, 1.65f)
                                  : clampf(lRad * 9.0f, texelWorld * 2.5f, 1.85f);
    float searchUv = clampf(searchWorld / wSpan, texelUv * 2.0f, 0.085f);

    int searchCount = clampi(pcssTaps / 2, 6, 24);
    float invSearch = 1.0f / float(searchCount);
    float blkDepthSum = 0.0f;
    int blkCount = 0;

    for (int i = 0; i < searchCount; ++i) {
      float r = std::sqrt((float(i) + 0.5f) * invSearch) * searchUv;
      float a = phi0 + float(i) * 2.39996323f;
      Vec2 qUv(clampf(uv.x + std::cos(a) * r, 0.0005f, 0.9995f),
               clampf(uv.y + std::sin(a) * r, 0.0005f, 0.9995f));
      float stored = sm.decodeLinear(sm.fetch(qUv));
      if (stored < recD - biasM) { blkDepthSum += stored; blkCount++; }
    }
    if (blkCount == 0) return 1.0f;

    float avgBlkD = blkDepthSum / float(blkCount);
    float blkDist = std::max(0.0f, recD - avgBlkD);
    outBlk = blkDist;
    float penumbraWorld = sm.isArea ? (lRad * blkDist / std::max(avgBlkD, 0.12f)) : (blkDist * lRad);
    float penumbraUv = clampf(penumbraWorld / wSpan, texelUv * 0.65f, 0.11f);

    int pcfCount = clampi(pcssTaps, 8, 48);
    float invPcf = 1.0f / float(pcfCount);
    float litSum = 0.0f;
    float phiPcf = fractf(ignHash(px + 17.3f, py + 9.1f) + 0.381966f) * TWO_PI;
    float filterBias = biasM + penumbraUv * wSpan * slope * (planeFilter ? 0.24f : 0.18f);

    for (int i = 0; i < pcfCount; ++i) {
      float r = std::sqrt((float(i) + 0.5f) * invPcf) * penumbraUv;
      float a = phiPcf + float(i) * 2.39996323f;
      Vec2 qUv(clampf(uv.x + std::cos(a) * r, 0.0005f, 0.9995f),
               clampf(uv.y + std::sin(a) * r, 0.0005f, 0.9995f));
      float stored = sm.decodeLinear(sm.fetch(qUv));
      if (stored >= recD - filterBias) litSum += invPcf;
    }
    return litSum;
  }

  // Exact ShadowAlgorithmLab: SHADOW_SMRT_FRAG (EEVEE-Next quadratic raymarch)
  float evalShadowMapSmrt(const ShadowMap& sm, const Vec3& pos, const Vec3& N, const Vec3& toLight,
                          float lRad, float px, float py) const {
    float ndl = dot(N, toLight);
    if (ndl <= 0.002f) return 0.0f;
    Vec2 uvEst; float dEst;
    if (!sm.projectWorld(pos, uvEst, dEst)) return 1.0f;
    float wSpan = sm.worldSpan(dEst);
    float texelWorld = wSpan / std::max(256.0f, float(sm.res));
    float slope = clampf(std::sqrt(std::max(0.0f, 1.0f - ndl * ndl)) / std::max(ndl, 0.04f), 0.0f, 4.0f);
    float biasM = texelWorld * (0.45f + 0.42f * slope);

    Vec3 softOrigin = pos + N * (texelWorld * 1.5f);
    float distToLight = sm.isArea ? std::max(length(sm.eye - softOrigin), 0.25f) : 28.0f;
    float tanHalf = sm.isArea ? (lRad / distToLight) : lRad;
    float cosMax = std::cos(std::atan(clampf(tanHalf, 0.0f, 0.45f)));
    float rayLen = sm.isArea ? std::min(9.5f, distToLight * 0.85f) : clampf(3.0f + tanHalf * 42.0f, 2.5f, 11.0f);

    Vec3 lTan, lBit;
    buildBasis(toLight, lTan, lBit);
    float phi0 = ignHash(px, py) * TWO_PI;
    int rMax = clampi(smrtRays, 1, 12);
    float invRays = 1.0f / float(rMax);
    float hitCount = 0.0f;

    for (int rIdx = 0; rIdx < rMax; ++rIdx) {
      float uCos = (float(rIdx) + fractf(ignHash(px + 3.7f, py + float(rIdx) * 5.3f) + float(rIdx) * 0.618034f) * 0.8f + 0.1f) * invRays;
      float cosTheta = mixf(cosMax, 1.0f, clampf(uCos, 0.002f, 0.998f));
      float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
      float phi = phi0 + float(rIdx) * 2.39996323f;
      Vec3 rayDir = normalize(lTan * (sinTheta * std::cos(phi)) + lBit * (sinTheta * std::sin(phi)) + toLight * cosTheta);
      Vec3 rayEnd = softOrigin + rayDir * rayLen;

      float rayTime = 1.0f, prevRayTime = 1.0f;
      float occHistory = -1000.0f, occSlope = -1000.0f;
      bool hit = false;

      for (int st = 0; st <= 14; ++st) {
        float lin = 1.0f - float(st) / 14.0f;
        prevRayTime = rayTime;
        rayTime = lin * lin;
        Vec3 samplePos = mix3(softOrigin, rayEnd, rayTime);
        Vec2 sUv; float sRayDepth;
        if (!sm.projectWorld(samplePos, sUv, sRayDepth)) continue;
        float stored = sm.decodeLinear(sm.fetch(sUv));
        if (stored > 5e4f) { occHistory = 2.0f; continue; }
        float delta = stored - (sRayDepth - biasM);
        if (delta < 0.0f) {
          bool historyValid = (occHistory > -500.0f);
          bool finalStep = (st == 14);
          float dt = std::max(std::abs(rayTime - prevRayTime), 1e-4f);
          float maxThick = 0.45f + ((occSlope > 0.0f) ? occSlope * dt * rayLen * 0.25f : 0.0f);
          if (finalStep || (historyValid && delta > -maxThick)) {
            hit = true;
            break;
          }
        } else {
          if (occHistory > -500.0f) {
            float dt = std::max(std::abs(rayTime - prevRayTime), 1e-6f);
            float curSlope = (delta - occHistory) / dt;
            occSlope = (occSlope > -500.0f) ? std::max(occSlope, curSlope) : curSlope;
          }
          occHistory = delta;
        }
      }
      if (hit) hitCount += 1.0f;
    }
    return clampf(1.0f - hitCount * invRays, 0.0f, 1.0f);
  }

  // Exact ShadowAlgorithmLab: SHADOW_BVH_FRAG
  float evalBvhRayShadow(const Scene& sc, const Vec3& pos, const Vec3& N, const Vec3& L,
                         float maxDist, float spreadRad, int numRays, int selfPrimId,
                         int skipMatId, float px, float py) const {
    Vec3 T, B;
    buildBasis(L, T, B);
    int rays = clampi(numRays, 1, 16);
    float phi = ignHash(px, py) * TWO_PI;
    float invRays = 1.0f / float(rays);
    float vis = 0.0f;
    Vec3 origin = pos + N * 0.008f;
    for (int r = 0; r < rays; ++r) {
      float uDisk = (float(r) + fractf(ignHash(px + 11.3f, py + float(r) * 7.1f) + float(r) * 0.618034f) * 0.8f + 0.1f) * invRays;
      float rFrac = (rays > 1) ? std::sqrt(clampf(uDisk, 0.005f, 0.995f)) : 0.0f;
      float ang = phi + float(r) * 2.39996323f;
      Vec3 dir = normalize(L + (T * std::cos(ang) + B * std::sin(ang)) * (rFrac * spreadRad));
      if (!traceBvhAnyHit(sc, origin, dir, maxDist, selfPrimId, skipMatId)) {
        vis += invRays;
      }
    }
    return vis;
  }

  float evalContactVis(const Scene& sc, const Vec3& pos, const Vec3& N, const Vec3& L,
                       float maxDist, int selfPrimId, int skipMatId) const {
    float maxT = std::min(0.24f, maxDist);
    if (maxT <= 0.03f) return 1.0f;
    Vec3 origin = pos + N * 0.006f;
    if (!traceBvhAnyHit(sc, origin, L, maxT, selfPrimId, skipMatId)) return 1.0f;
    bool nearHit = traceBvhAnyHit(sc, origin, L, maxT * 0.55f, selfPrimId, skipMatId);
    return nearHit ? 0.08f : 0.55f;
  }

  float evalSolarShadow(const Scene& sc, const Vec3& pos, const Vec3& N, int selfPrimId,
                        float px, float py) const {
    if (!sc.sunEnabled || dot(N, sc.sunDir) <= 0.002f) return 0.0f;
    float lRad = std::max(0.004f, std::tan(sc.sunAngularRad) * 1.15f) * softness;
    if (alg == ShadowAlg::Bvh || !sunMap.valid) {
      return evalBvhRayShadow(sc, pos, N, sc.sunDir, 36.0f, lRad, bvhRays, selfPrimId, -1, px, py);
    }
    if (alg == ShadowAlg::Pcf) return evalShadowMapPcf(sunMap, pos, N, sc.sunDir, lRad);
    if (alg == ShadowAlg::Smrt) return evalShadowMapSmrt(sunMap, pos, N, sc.sunDir, lRad, px, py);
    float blk = 0.0f;
    float pcssVis = evalShadowMapPcss(sunMap, pos, N, sc.sunDir, lRad, px, py, blk);
    if (alg == ShadowAlg::ContactPcss) {
      float contactVis = evalContactVis(sc, pos, N, sc.sunDir, 0.24f, selfPrimId, -1);
      float contactBlend = clampf((blk - 0.06f) / 0.25f, 0.0f, 1.0f);
      return mixf(std::min(contactVis, pcssVis), pcssVis, contactBlend);
    }
    return pcssVis;
  }

  // Exact ShadowAlgorithmLab: EMISSIVE_MAP_SHADOW_FRAG + EMISSIVE_BVH_SHADOW_FRAG
  float evalAreaLightShadow(const Scene& sc, const Vec3& pos, const Vec3& N, int selfPrimId,
                            float px, float py) const {
    if (!emissiveShadowed || !sc.areaLight.enabled) return 1.0f;
    Vec3 toL = sc.areaLight.center - pos;
    float distL = length(toL);
    if (distL < 0.05f) return 1.0f;
    Vec3 L = toL / distL;
    if (areaMap.valid && alg != ShadowAlg::Bvh) {
      float panelRad = mixf(sc.areaLight.hh, sc.areaLight.hw, 0.55f) * softness;
      if (alg == ShadowAlg::Pcf) return mixf(0.03f, 1.0f, evalShadowMapPcf(areaMap, pos, N, L, panelRad));
      if (alg == ShadowAlg::Smrt) return mixf(0.03f, 1.0f, evalShadowMapSmrt(areaMap, pos, N, L, panelRad, px, py));
      float blk = 0.0f;
      float pcssVis = evalShadowMapPcss(areaMap, pos, N, L, panelRad, px, py, blk);
      if (alg == ShadowAlg::ContactPcss) {
        float cVis = evalContactVis(sc, pos, N, L, std::min(0.24f, distL - 0.05f), selfPrimId, 11);
        float cBlend = clampf((blk - 0.06f) / 0.25f, 0.0f, 1.0f);
        return mixf(0.03f, 1.0f, mixf(std::min(cVis, pcssVis), pcssVis, cBlend));
      }
      return mixf(0.03f, 1.0f, pcssVis);
    }
    int rayCount = clampi(bvhRays, 2, 12);
    float invRays = 1.0f / float(rayCount);
    float r2x = ignHash(px, py);
    float r2y = ignHash(px + 37.1f, py + 13.7f);
    Vec3 origin = pos + N * 0.008f;
    float visAcc = 0.0f;
    for (int s = 0; s < rayCount; ++s) {
      float sx = (fractf(r2x + float(s) * 0.75487766f) * 2.0f - 1.0f) * 0.94f;
      float sy = (fractf(r2y + float(s) * 0.56984029f) * 2.0f - 1.0f) * 0.94f;
      Vec3 tgt = sc.areaLight.center + sc.areaLight.right * (sx * sc.areaLight.hw * softness)
                                     + sc.areaLight.up    * (sy * sc.areaLight.hh * softness);
      Vec3 delta = tgt - origin;
      float dist = length(delta);
      Vec3 rDir = delta / std::max(dist, 1e-4f);
      if (dot(N, rDir) <= 0.002f) continue;
      if (!traceBvhAnyHit(sc, origin, rDir, std::max(0.04f, dist - 0.14f), selfPrimId, 11)) {
        visAcc += invRays;
      }
    }
    return mixf(0.03f, 1.0f, visAcc);
  }

  // Direct shadowed lighting from non-panel emissive BVH primitives (orb + neon tube)
  Vec3 evalEmissivePrimsDirect(const Scene& sc, const Vec3& pos, const Vec3& N, const Vec3& V,
                               const Material& mat, int selfPrimId, float px, float py, bool highQual) const {
    Vec3 lo(0.0f);
    for (size_t i = 0; i < sc.prims.size(); ++i) {
      if (static_cast<int>(i) == selfPrimId) continue;
      const auto& ep = sc.prims[i];
      if (!sc.materials[ep.matId].isEmissive || static_cast<int>(i) == sc.areaLight.skipPrimId) continue;
      const Material& emMat = sc.materials[ep.matId];

      if (ep.type == PrimType::Sphere) {
        Vec3 toE = ep.center - pos;
        float dist = length(toE);
        Vec3 eDir = toE / std::max(1e-4f, dist);
        if (dot(N, eDir) > 0.001f) {
          float r = ep.halfExtents.x;
          float solidOmega = std::min(2.0f, (PI * r * r) / std::max(0.06f, dist * dist));
          float coneSin = clampf(r / std::max(dist, 0.35f), 0.04f, 0.24f) * softness;
          float vis = 1.0f;
          if (emissiveShadowed) {
            float rawV = evalBvhRayShadow(sc, pos, N, eDir, std::max(0.02f, dist - r - 0.04f),
                                          coneSin, highQual ? 2 : 1, selfPrimId, ep.matId, px, py);
            vis = mixf(0.03f, 1.0f, rawV);
          }
          lo += evalOpenPbrDirect(N, V, eDir, mat) * emMat.emission * (solidOmega * 0.72f) * vis;
        }
      } else if (ep.type == PrimType::Capsule) {
        int segs = highQual ? 3 : 1;
        float segLen = length(ep.p1 - ep.center) / float(segs);
        for (int s = 0; s < segs; ++s) {
          float t = (float(s) + 0.5f) / float(segs);
          Vec3 pt = mix3(ep.center, ep.p1, t);
          Vec3 toE = pt - pos;
          float dist = length(toE);
          Vec3 eDir = toE / std::max(1e-4f, dist);
          if (dot(N, eDir) > 0.001f) {
            float r = ep.halfExtents.x;
            float area = TWO_PI * r * segLen;
            float solidOmega = std::min(1.6f, area / std::max(0.10f, 4.0f * dist * dist));
            float vis = 1.0f;
            if (emissiveShadowed) {
              float rawV = evalBvhRayShadow(sc, pos, N, eDir, std::max(0.02f, dist - r - 0.05f),
                                            0.06f * softness, 1, selfPrimId, ep.matId, px, py);
              vis = mixf(0.03f, 1.0f, rawV);
            }
            lo += evalOpenPbrDirect(N, V, eDir, mat) * emMat.emission * (solidOmega * 0.85f) * vis;
          }
        }
      }
    }
    return lo;
  }

  // Exact ShadowAlgorithmLab: REFLECTION_BVH_FRAG (Multi-bounce BVH Reflections — NO SDF!)
  template <typename SecondaryShadeFn>
  Vec3 traceBvhReflection(const Scene& sc, const Vec3& startPos, const Vec3& startN,
                          const Vec3& startDir, int maxBounces, int ignorePrimId,
                          SecondaryShadeFn&& shadeSecondary) const {
    Vec3 currP = startPos, currN = startN, dir = startDir;
    int currIgnore = ignorePrimId;
    Vec3 totalRad(0.0f), throughput(1.0f);

    for (int b = 1; b <= maxBounces; ++b) {
      HitInfo h = traceBvh(sc, currP + currN * 0.006f, dir, 60.0f, currIgnore, false);
      if (!h.hit) {
        totalRad += throughput * sc.evalSimpleSky(dir);
        break;
      }
      Material hm = sc.sampleMaterial(h.matId, h.pos);
      if (hm.isEmissive) {
        totalRad += throughput * hm.emission;
        break;
      }
      Vec3 hitRad = shadeSecondary(h, -dir);
      float mRefl = clampf((hm.metallic * 0.75f + hm.coat * 0.45f) * (1.0f - hm.roughness), 0.0f, 0.95f);
      if (b == maxBounces || mRefl < 0.04f) {
        totalRad += throughput * hitRad;
        break;
      }
      float noV = std::max(0.02f, dot(h.normal, -dir));
      float fr = clampf(mRefl + (1.0f - mRefl) * std::pow(std::max(1.0f - noV, 1e-4f), 5.0f) * 0.45f, 0.0f, 0.92f);
      totalRad += throughput * hitRad * (1.0f - fr * 0.55f);
      throughput *= fr;
      if (maxComp(throughput) < 0.025f) break;
      currP = h.pos;
      currN = h.normal;
      currIgnore = h.primIdx;
      dir = reflect3(dir, h.normal);
    }
    return clamp3(totalRad, 0.0f, 60.0f);
  }

private:
  // Populate depth32float shadow map directly by ray-casting each shadow texel against BVH!
  // Wait: exact BVH shadow-map depth generation ensures triangles/capsules/spheres/boxes are 100% consistent!
  void renderDepthBvh(const Scene& sc, ShadowMap& sm, int skipPrimId) {
    sm.depth01.assign(static_cast<size_t>(sm.res) * sm.res, 1.0f);
    // Compute view basis matching buildLookAtGl
    Vec3 f = sm.forward;
    Vec3 upHint = sm.isArea ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
    Vec3 upUse = std::abs(dot(f, upHint)) > 0.98f ? Vec3(1, 0, 0) : upHint;
    Vec3 s = normalize(cross(f, upUse));
    Vec3 u = cross(s, f);

    for (int py = 0; py < sm.res; ++py) {
      float ndcY = ((py + 0.5f) / sm.res) * 2.0f - 1.0f;
      for (int px = 0; px < sm.res; ++px) {
        float ndcX = ((px + 0.5f) / sm.res) * 2.0f - 1.0f;
        Vec3 ro, rd;
        if (!sm.isArea) {
          ro = sm.eye + s * (ndcX * sm.orthoHalfSpan) + u * (ndcY * sm.orthoHalfSpan);
          rd = f;
        } else {
          ro = sm.eye;
          rd = normalize(f + s * (ndcX * sm.perspTan) + u * (ndcY * sm.perspTan));
        }
        HitInfo h = traceBvh(sc, ro, rd, sm.farZ, skipPrimId, true);
        if (h.hit && sc.prims[h.primIdx].castShadow) {
          Vec4 clip = sm.vp.mul({h.pos.x, h.pos.y, h.pos.z, 1.0f});
          if (clip.w > 1e-4f) {
            float d01 = (clip.z / clip.w) * 0.5f + 0.5f;
            sm.depth01[static_cast<size_t>(py) * sm.res + px] = clampf(d01, 0.0f, 1.0f);
          }
        }
      }
    }
  }
};

} // namespace sdf_demo

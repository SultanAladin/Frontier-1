#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sdf_demo {

constexpr float PI = 3.14159265358979323846f;
constexpr float TWO_PI = 6.28318530717958647692f;

struct Vec2 {
  float x = 0.0f, y = 0.0f;
  constexpr Vec2() = default;
  constexpr Vec2(float s) : x(s), y(s) {}
  constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
  constexpr Vec2 operator+(const Vec2& b) const { return {x + b.x, y + b.y}; }
  constexpr Vec2 operator-(const Vec2& b) const { return {x - b.x, y - b.y}; }
  constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
};

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
  float operator[](int i) const { return i == 0 ? x : (i == 1 ? y : z); }
};

struct Vec4 {
  float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
  constexpr Vec4() = default;
  constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
  Vec3 xyz() const { return {x, y, z}; }
};

struct Mat4 {
  float m[16] = {}; // column-major
  static Mat4 identity() {
    Mat4 r;
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
  }
  Vec4 mul(const Vec4& v) const {
    return {
      m[0] * v.x + m[4] * v.y + m[8]  * v.z + m[12] * v.w,
      m[1] * v.x + m[5] * v.y + m[9]  * v.z + m[13] * v.w,
      m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14] * v.w,
      m[3] * v.x + m[7] * v.y + m[11] * v.z + m[15] * v.w,
    };
  }
};

inline Vec3 operator*(float s, const Vec3& v) { return {v.x * s, v.y * s, v.z * s}; }
inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }
inline float length2(const Vec3& v) { return dot(v, v); }
inline Vec3 normalize(const Vec3& v) {
  float l = length(v);
  return l > 1e-6f ? v / l : Vec3(0, 0, 1);
}
inline float clampf(float v, float lo, float hi) { return std::max(lo, std::min(hi, v)); }
inline int clampi(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }
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
inline Vec3 min3(const Vec3& a, const Vec3& b) { return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)}; }
inline Vec3 max3(const Vec3& a, const Vec3& b) { return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)}; }
inline Vec3 abs3(const Vec3& v) { return {std::abs(v.x), std::abs(v.y), std::abs(v.z)}; }
inline float maxComp(const Vec3& v) { return std::max(v.x, std::max(v.y, v.z)); }
inline float luminance(const Vec3& v) { return 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z; }

inline float ignHash(float px, float py) {
  return fractf(52.9829189f * fractf(0.06711056f * px + 0.00583715f * py));
}

inline uint32_t pcgHash(uint32_t v) {
  uint32_t state = v * 747796405u + 2891336453u;
  uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
  return (word >> 22u) ^ word;
}

inline float rand01(uint32_t& state) {
  state = pcgHash(state);
  return static_cast<float>(state & 0x00FFFFFFu) / 16777216.0f;
}

inline void buildBasis(const Vec3& n, Vec3& t, Vec3& b) {
  Vec3 up = std::abs(n.z) < 0.999f ? Vec3(0, 0, 1) : Vec3(1, 0, 0);
  t = normalize(cross(up, n));
  b = cross(n, t);
}

inline Vec2 octEncode(const Vec3& dir) {
  Vec3 n = dir / (std::abs(dir.x) + std::abs(dir.y) + std::abs(dir.z));
  if (n.z < 0.0f) {
    float nx = (1.0f - std::abs(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f);
    float ny = (1.0f - std::abs(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
    n.x = nx;
    n.y = ny;
  }
  return {n.x * 0.5f + 0.5f, n.y * 0.5f + 0.5f};
}

inline Vec3 octDecode(float u, float v) {
  float x = u * 2.0f - 1.0f;
  float y = v * 2.0f - 1.0f;
  Vec3 n(x, y, 1.0f - std::abs(x) - std::abs(y));
  if (n.z < 0.0f) {
    float nx = (1.0f - std::abs(n.y)) * (n.x >= 0.0f ? 1.0f : -1.0f);
    float ny = (1.0f - std::abs(n.x)) * (n.y >= 0.0f ? 1.0f : -1.0f);
    n.x = nx;
    n.y = ny;
  }
  return normalize(n);
}

inline Vec3 acesToneMap(const Vec3& x) {
  Vec3 a = x * (x * 2.51f + Vec3(0.03f));
  Vec3 b = x * (x * 2.43f + Vec3(0.59f)) + Vec3(0.14f);
  return clamp3(Vec3(a.x / b.x, a.y / b.y, a.z / b.z), 0.0f, 1.0f);
}

} // namespace sdf_demo

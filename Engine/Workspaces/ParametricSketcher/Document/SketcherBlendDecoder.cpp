/*====================================================================================================================================
                                                     SKETCHERBLENDDECODER.CPP
====================================================================================================================================*/
// 🧩 Native ZSTD/GZIP/raw .blend SDNA binary decoder + dependency-ordered Blender modifier stack evaluator for SolidArc

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherBlendDecoder.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#include <stb_image.h>
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
#include <zstd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Frontier::Workspaces::Sketcher
{

using GeometrySpace::Parametric::Cross;
using GeometrySpace::Parametric::Dot;
using GeometrySpace::Parametric::Vector3;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr double PiValue       = 3.14159265358979323846;
constexpr double DegToRad      = PiValue / 180.0;
constexpr double BlendEpsilon  = 1.0e-7;

// Blender MirrorModifierData.flag bits
constexpr uint32_t MirrorClippingBit    = 0x0001u;
constexpr uint32_t MirrorAxisXBit       = 0x0008u;
constexpr uint32_t MirrorAxisYBit       = 0x0010u;
constexpr uint32_t MirrorAxisZBit       = 0x0020u;
constexpr uint32_t MirrorNoMergeBit     = 0x0080u;
constexpr uint32_t MirrorBisectXBit     = 0x0100u;
constexpr uint32_t MirrorBisectYBit     = 0x0200u;
constexpr uint32_t MirrorBisectZBit     = 0x0400u;
constexpr uint32_t MirrorBisectFlipXBit = 0x0800u;
constexpr uint32_t MirrorBisectFlipYBit = 0x1000u;
constexpr uint32_t MirrorBisectFlipZBit = 0x2000u;

// Blender SubsurfModifierData.flags bits
constexpr uint32_t SubsurfUseCreaseBit = 0x0010u;

// Blender SolidifyModifierData.flag bits
constexpr uint32_t SolidifyRimBit      = 0x0001u;
constexpr uint32_t SolidifyOnlyRimBit  = 0x0010u;
constexpr uint32_t SolidifyFlipBit     = 0x0004u;

// Blender ArrayModifierData.offset_type bits
constexpr uint32_t ArrayOffsetConstBit    = 0x0001u;
constexpr uint32_t ArrayOffsetRelativeBit = 0x0002u;

// Blender CustomData layer type codes
constexpr int CustomLayerDeformVert = 2;
constexpr int CustomLayerVertSkin   = 36;

//------------------------------------------------------------------------------------------------------------------------
//                                                    MATH HELPERS
//------------------------------------------------------------------------------------------------------------------------

struct Vec3d
{
    double X = 0.0, Y = 0.0, Z = 0.0;
    Vec3d operator+(const Vec3d& R) const noexcept { return { X + R.X, Y + R.Y, Z + R.Z }; }
    Vec3d operator-(const Vec3d& R) const noexcept { return { X - R.X, Y - R.Y, Z - R.Z }; }
    Vec3d operator*(double S) const noexcept { return { X * S, Y * S, Z * S }; }
    double& operator[](int Axis) noexcept { return Axis == 0 ? X : (Axis == 1 ? Y : Z); }
    double operator[](int Axis) const noexcept { return Axis == 0 ? X : (Axis == 1 ? Y : Z); }
};

[[nodiscard]] double Dot3(const Vec3d& A, const Vec3d& B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
[[nodiscard]] Vec3d Cross3(const Vec3d& A, const Vec3d& B) noexcept
{
    return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
}
[[nodiscard]] Vec3d Normalize3(const Vec3d& V) noexcept
{
    const double Sq = Dot3(V, V);
    if (Sq <= 1.0e-24) return { 0.0, 0.0, 1.0 };
    return V * (1.0 / std::sqrt(Sq));
}

// Column-major 4x4 affine matrix (matching Blender float[4][4] memory layout: M[col*4 + row]).
struct Mat4d
{
    double M[16] = {
        1.0, 0.0, 0.0, 0.0,
        0.0, 1.0, 0.0, 0.0,
        0.0, 0.0, 1.0, 0.0,
        0.0, 0.0, 0.0, 1.0,
    };

    [[nodiscard]] double At(int Row, int Col) const noexcept { return M[Col * 4 + Row]; }
    double& At(int Row, int Col) noexcept { return M[Col * 4 + Row]; }
};

[[nodiscard]] Mat4d MultiplyMat4(const Mat4d& A, const Mat4d& B) noexcept
{
    Mat4d Out;
    for (int Col = 0; Col < 4; ++Col)
    {
        for (int Row = 0; Row < 4; ++Row)
        {
            double Sum = 0.0;
            for (int K = 0; K < 4; ++K) Sum += A.At(Row, K) * B.At(K, Col);
            Out.At(Row, Col) = Sum;
        }
    }
    return Out;
}

[[nodiscard]] Vec3d TransformPoint(const Mat4d& M, const Vec3d& P) noexcept
{
    return {
        M.At(0, 0) * P.X + M.At(0, 1) * P.Y + M.At(0, 2) * P.Z + M.At(0, 3),
        M.At(1, 0) * P.X + M.At(1, 1) * P.Y + M.At(1, 2) * P.Z + M.At(1, 3),
        M.At(2, 0) * P.X + M.At(2, 1) * P.Y + M.At(2, 2) * P.Z + M.At(2, 3),
    };
}

[[nodiscard]] double Determinant3x3(const Mat4d& M) noexcept
{
    return M.At(0, 0) * (M.At(1, 1) * M.At(2, 2) - M.At(1, 2) * M.At(2, 1))
         - M.At(0, 1) * (M.At(1, 0) * M.At(2, 2) - M.At(1, 2) * M.At(2, 0))
         + M.At(0, 2) * (M.At(1, 0) * M.At(2, 1) - M.At(1, 1) * M.At(2, 0));
}

[[nodiscard]] Mat4d InvertAffine(const Mat4d& M) noexcept
{
    const double Det = Determinant3x3(M);
    if (std::abs(Det) < 1.0e-18) return Mat4d{};
    const double InvDet = 1.0 / Det;
    Mat4d Inv;
    Inv.At(0, 0) =  (M.At(1, 1) * M.At(2, 2) - M.At(1, 2) * M.At(2, 1)) * InvDet;
    Inv.At(0, 1) = -(M.At(0, 1) * M.At(2, 2) - M.At(0, 2) * M.At(2, 1)) * InvDet;
    Inv.At(0, 2) =  (M.At(0, 1) * M.At(1, 2) - M.At(0, 2) * M.At(1, 1)) * InvDet;
    Inv.At(1, 0) = -(M.At(1, 0) * M.At(2, 2) - M.At(1, 2) * M.At(2, 0)) * InvDet;
    Inv.At(1, 1) =  (M.At(0, 0) * M.At(2, 2) - M.At(0, 2) * M.At(2, 0)) * InvDet;
    Inv.At(1, 2) = -(M.At(0, 0) * M.At(1, 2) - M.At(0, 2) * M.At(1, 0)) * InvDet;
    Inv.At(2, 0) =  (M.At(1, 0) * M.At(2, 1) - M.At(1, 1) * M.At(2, 0)) * InvDet;
    Inv.At(2, 1) = -(M.At(0, 0) * M.At(2, 1) - M.At(0, 1) * M.At(2, 0)) * InvDet;
    Inv.At(2, 2) =  (M.At(0, 0) * M.At(1, 1) - M.At(0, 1) * M.At(1, 0)) * InvDet;
    const Vec3d T{ M.At(0, 3), M.At(1, 3), M.At(2, 3) };
    Inv.At(0, 3) = -(Inv.At(0, 0) * T.X + Inv.At(0, 1) * T.Y + Inv.At(0, 2) * T.Z);
    Inv.At(1, 3) = -(Inv.At(1, 0) * T.X + Inv.At(1, 1) * T.Y + Inv.At(1, 2) * T.Z);
    Inv.At(2, 3) = -(Inv.At(2, 0) * T.X + Inv.At(2, 1) * T.Y + Inv.At(2, 2) * T.Z);
    return Inv;
}

[[nodiscard]] Mat4d RotationAxisMatrix(int Axis, double Radians) noexcept
{
    Mat4d R;
    const double C = std::cos(Radians), S = std::sin(Radians);
    if (Axis == 0)      { R.At(1, 1) = C; R.At(1, 2) = -S; R.At(2, 1) = S; R.At(2, 2) = C; }
    else if (Axis == 1) { R.At(0, 0) = C; R.At(0, 2) = S;  R.At(2, 0) = -S; R.At(2, 2) = C; }
    else                { R.At(0, 0) = C; R.At(0, 1) = -S; R.At(1, 0) = S; R.At(1, 1) = C; }
    return R;
}

[[nodiscard]] Mat4d RotationFromBlender(const float Euler[3], const float Quat[4], const float Axis[3], float Angle, int16_t Mode) noexcept
{
    if (Mode == 0)
    {
        double W = Quat[0], X = Quat[1], Y = Quat[2], Z = Quat[3];
        const double Len = std::sqrt(W * W + X * X + Y * Y + Z * Z);
        if (Len > 1.0e-12) { W /= Len; X /= Len; Y /= Len; Z /= Len; }
        else { W = 1.0; X = Y = Z = 0.0; }
        Mat4d R;
        R.At(0, 0) = 1.0 - 2.0 * (Y * Y + Z * Z); R.At(0, 1) = 2.0 * (X * Y - W * Z);       R.At(0, 2) = 2.0 * (X * Z + W * Y);
        R.At(1, 0) = 2.0 * (X * Y + W * Z);       R.At(1, 1) = 1.0 - 2.0 * (X * X + Z * Z); R.At(1, 2) = 2.0 * (Y * Z - W * X);
        R.At(2, 0) = 2.0 * (X * Z - W * Y);       R.At(2, 1) = 2.0 * (Y * Z + W * X);       R.At(2, 2) = 1.0 - 2.0 * (X * X + Y * Y);
        return R;
    }
    if (Mode == -1)
    {
        const Vec3d U = Normalize3({ Axis[0], Axis[1], Axis[2] });
        const double C = std::cos(double(Angle)), S = std::sin(double(Angle)), T = 1.0 - C;
        Mat4d R;
        R.At(0, 0) = C + U.X * U.X * T;         R.At(0, 1) = U.X * U.Y * T - U.Z * S; R.At(0, 2) = U.X * U.Z * T + U.Y * S;
        R.At(1, 0) = U.Y * U.X * T + U.Z * S;   R.At(1, 1) = C + U.Y * U.Y * T;       R.At(1, 2) = U.Y * U.Z * T - U.X * S;
        R.At(2, 0) = U.Z * U.X * T - U.Y * S;   R.At(2, 1) = U.Z * U.Y * T + U.X * S; R.At(2, 2) = C + U.Z * U.Z * T;
        return R;
    }

    const Mat4d Rx = RotationAxisMatrix(0, Euler[0]);
    const Mat4d Ry = RotationAxisMatrix(1, Euler[1]);
    const Mat4d Rz = RotationAxisMatrix(2, Euler[2]);
    switch (Mode)
    {
    case 2:  return MultiplyMat4(Ry, MultiplyMat4(Rz, Rx));                                  // XZY
    case 3:  return MultiplyMat4(Rz, MultiplyMat4(Rx, Ry));                                  // YXZ
    case 4:  return MultiplyMat4(Rx, MultiplyMat4(Rz, Ry));                                  // YZX
    case 5:  return MultiplyMat4(Ry, MultiplyMat4(Rx, Rz));                                  // ZXY
    case 6:  return MultiplyMat4(Rx, MultiplyMat4(Ry, Rz));                                  // ZYX
    default: return MultiplyMat4(Rz, MultiplyMat4(Ry, Rx));                                  // XYZ (Mode == 1)
    }
}

[[nodiscard]] Mat4d ComposeLocalTrs(const float Loc[3], const float Euler[3], const float Quat[4], const float Axis[3], float Angle, int16_t Mode, const float Size[3]) noexcept
{
    Mat4d R = RotationFromBlender(Euler, Quat, Axis, Angle, Mode);
    for (int Row = 0; Row < 3; ++Row)
    {
        R.At(Row, 0) *= double(Size[0]);
        R.At(Row, 1) *= double(Size[1]);
        R.At(Row, 2) *= double(Size[2]);
    }
    R.At(0, 3) = double(Loc[0]);
    R.At(1, 3) = double(Loc[1]);
    R.At(2, 3) = double(Loc[2]);
    return R;
}

[[nodiscard]] uint64_t EdgeKeyOf(uint32_t A, uint32_t B) noexcept
{
    const uint32_t Lo = std::min(A, B), Hi = std::max(A, B);
    return (uint64_t(Lo) << 32u) | uint64_t(Hi);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DECOMPRESSION
//------------------------------------------------------------------------------------------------------------------------

bool DecompressZstdAllFrames(const uint8_t* Src, size_t SrcSize, std::vector<uint8_t>& Out, std::string& Refusal)
{
    ZSTD_DStream* Stream = ZSTD_createDStream();
    if (!Stream)
    {
        Refusal = "ZSTD_createDStream failed";
        return false;
    }

    size_t InitRes = ZSTD_initDStream(Stream);
    if (ZSTD_isError(InitRes))
    {
        Refusal = std::string("ZSTD_initDStream failed: ") + ZSTD_getErrorName(InitRes);
        ZSTD_freeDStream(Stream);
        return false;
    }

    ZSTD_inBuffer InBuf{ Src, SrcSize, 0u };
    std::vector<uint8_t> Chunk(1u << 20u);
    Out.clear();
    Out.reserve(std::min<size_t>(SrcSize * 8u, 128u << 20u));

    while (InBuf.pos < InBuf.size)
    {
        ZSTD_outBuffer OutBuf{ Chunk.data(), Chunk.size(), 0u };
        const size_t Ret = ZSTD_decompressStream(Stream, &OutBuf, &InBuf);
        if (ZSTD_isError(Ret))
        {
            Refusal = std::string("ZSTD decompression failed: ") + ZSTD_getErrorName(Ret);
            ZSTD_freeDStream(Stream);
            return false;
        }
        if (OutBuf.pos > 0u)
        {
            Out.insert(Out.end(), Chunk.data(), Chunk.data() + OutBuf.pos);
        }
        if (Ret == 0u && InBuf.pos < InBuf.size)
        {
            InitRes = ZSTD_initDStream(Stream);
            if (ZSTD_isError(InitRes))
            {
                Refusal = std::string("ZSTD stream reset failed: ") + ZSTD_getErrorName(InitRes);
                ZSTD_freeDStream(Stream);
                return false;
            }
        }
    }

    ZSTD_freeDStream(Stream);
    return true;
}

bool DecompressGzipPayload(const uint8_t* Src, size_t SrcSize, std::vector<uint8_t>& Out, std::string& Refusal)
{
    if (SrcSize < 18u || Src[0] != 0x1Fu || Src[1] != 0x8Bu || Src[2] != 0x08u)
    {
        Refusal = "Invalid GZIP header in .blend file";
        return false;
    }
    const uint8_t GzBits = Src[3];
    size_t Pos = 10u;
    if (GzBits & 0x04u)
    {
        if (Pos + 2u > SrcSize) { Refusal = "Truncated GZIP extra header"; return false; }
        const uint16_t ExtraLen = uint16_t(Src[Pos]) | (uint16_t(Src[Pos + 1u]) << 8u);
        Pos += 2u + ExtraLen;
    }
    if (GzBits & 0x08u) { while (Pos < SrcSize && Src[Pos] != 0u) ++Pos; ++Pos; }
    if (GzBits & 0x10u) { while (Pos < SrcSize && Src[Pos] != 0u) ++Pos; ++Pos; }
    if (GzBits & 0x02u) { Pos += 2u; }
    if (Pos + 8u >= SrcSize)
    {
        Refusal = "Truncated GZIP payload";
        return false;
    }

    const int RawSize = static_cast<int>(SrcSize - Pos - 8u);
    int OutLen = 0;
    char* Inflated = stbi_zlib_decode_noheader_malloc(reinterpret_cast<const char*>(Src + Pos), RawSize, &OutLen);
    if (!Inflated || OutLen <= 0)
    {
        if (Inflated) std::free(Inflated);
        Refusal = "GZIP deflate decompression failed";
        return false;
    }
    Out.assign(reinterpret_cast<const uint8_t*>(Inflated), reinterpret_cast<const uint8_t*>(Inflated) + OutLen);
    std::free(Inflated);
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SDNA BINARY PARSER
//------------------------------------------------------------------------------------------------------------------------

struct BlendField
{
    std::string CleanName = {};
    std::string RawName   = {};
    std::string TypeName  = {};
    uint32_t    Offset    = 0u;
    uint32_t    ByteSize  = 0u;
};

struct BlendStruct
{
    std::string             Name     = {};
    uint32_t                ByteSize = 0u;
    std::vector<BlendField> Fields   = {};
    std::unordered_map<std::string, uint32_t> FieldLookup = {};

    [[nodiscard]] const BlendField* FindField(const char* FieldName) const noexcept
    {
        const auto It = FieldLookup.find(FieldName);
        return It != FieldLookup.end() ? &Fields[It->second] : nullptr;
    }
};

struct BlendBlock
{
    char     Code[5]   = {};
    uint64_t OldAddr   = 0u;
    uint32_t SdnaIndex = 0u;
    uint64_t Count     = 0u;
    size_t   Offset    = 0u;
    size_t   ByteSize  = 0u;
};

struct BlendDocument
{
    std::vector<uint8_t>                  Bytes         = {};
    uint32_t                              PointerSize   = 8u;
    std::string                           VersionText   = {};
    std::vector<BlendBlock>               Blocks        = {};
    std::unordered_map<uint64_t, size_t>  BlockByAddr   = {};
    std::unordered_map<uint64_t, size_t>  IdBlockByAddr = {};
    std::unordered_map<uint64_t, std::unordered_map<uint64_t, size_t>> LocalPayloadsById = {};
    std::vector<BlendStruct>              Structs       = {};
    std::unordered_map<std::string, size_t> StructByName = {};

    [[nodiscard]] const BlendBlock* FindBlock(uint64_t Addr, uint64_t OwnerId = 0u) const noexcept
    {
        if (Addr == 0u) return nullptr;
        if (OwnerId != 0u)
        {
            const auto OwnerIt = LocalPayloadsById.find(OwnerId);
            if (OwnerIt != LocalPayloadsById.end())
            {
                const auto LocalIt = OwnerIt->second.find(Addr);
                if (LocalIt != OwnerIt->second.end()) return &Blocks[LocalIt->second];
            }
        }
        const auto IdIt = IdBlockByAddr.find(Addr);
        if (IdIt != IdBlockByAddr.end()) return &Blocks[IdIt->second];
        const auto It = BlockByAddr.find(Addr);
        return It != BlockByAddr.end() ? &Blocks[It->second] : nullptr;
    }

    [[nodiscard]] const BlendStruct* FindStruct(const char* StructName) const noexcept
    {
        const auto It = StructByName.find(StructName);
        return It != StructByName.end() ? &Structs[It->second] : nullptr;
    }

    [[nodiscard]] const uint8_t* BlockPtr(const BlendBlock& Block) const noexcept
    {
        return Bytes.data() + Block.Offset;
    }
};

[[nodiscard]] uint16_t ReadU16(const uint8_t* P) noexcept { uint16_t V; std::memcpy(&V, P, 2u); return V; }
[[nodiscard]] int16_t  ReadI16(const uint8_t* P) noexcept { int16_t  V; std::memcpy(&V, P, 2u); return V; }
[[nodiscard]] uint32_t ReadU32(const uint8_t* P) noexcept { uint32_t V; std::memcpy(&V, P, 4u); return V; }
[[nodiscard]] int32_t  ReadI32(const uint8_t* P) noexcept { int32_t  V; std::memcpy(&V, P, 4u); return V; }
[[nodiscard]] uint64_t ReadU64(const uint8_t* P) noexcept { uint64_t V; std::memcpy(&V, P, 8u); return V; }
[[nodiscard]] float    ReadF32(const uint8_t* P) noexcept { float    V; std::memcpy(&V, P, 4u); return V; }

[[nodiscard]] const uint8_t* FieldSlice(const BlendStruct* St, const uint8_t* BlockBytes, size_t Limit, const char* Name, uint32_t& OutSize) noexcept
{
    OutSize = 0u;
    if (!St || !BlockBytes) return nullptr;
    const BlendField* F = St->FindField(Name);
    if (!F || size_t(F->Offset) + size_t(F->ByteSize) > Limit) return nullptr;
    OutSize = F->ByteSize;
    return BlockBytes + F->Offset;
}

[[nodiscard]] int64_t ReadSignedField(const BlendStruct* St, const uint8_t* BlockBytes, size_t Limit, const char* Name, int64_t Fallback = 0) noexcept
{
    uint32_t Sz = 0u;
    const uint8_t* P = FieldSlice(St, BlockBytes, Limit, Name, Sz);
    if (!P) return Fallback;
    if (Sz == 1u) return static_cast<int8_t>(P[0]);
    if (Sz == 2u) return ReadI16(P);
    if (Sz == 4u) return ReadI32(P);
    if (Sz == 8u) { int64_t V; std::memcpy(&V, P, 8u); return V; }
    return Fallback;
}

[[nodiscard]] uint64_t ReadUnsignedField(const BlendStruct* St, const uint8_t* BlockBytes, size_t Limit, const char* Name, uint64_t Fallback = 0u) noexcept
{
    uint32_t Sz = 0u;
    const uint8_t* P = FieldSlice(St, BlockBytes, Limit, Name, Sz);
    if (!P) return Fallback;
    if (Sz == 1u) return P[0];
    if (Sz == 2u) return ReadU16(P);
    if (Sz == 4u) return ReadU32(P);
    if (Sz >= 8u) return ReadU64(P);
    return Fallback;
}

[[nodiscard]] uint64_t ReadPointerField(const BlendDocument& Doc, const BlendStruct* St, const uint8_t* BlockBytes, size_t Limit, const char* Name) noexcept
{
    uint32_t Sz = 0u;
    const uint8_t* P = FieldSlice(St, BlockBytes, Limit, Name, Sz);
    if (!P) return 0u;
    if (Doc.PointerSize == 4u && Sz >= 4u) return ReadU32(P);
    if (Sz >= 8u) return ReadU64(P);
    return 0u;
}

[[nodiscard]] float ReadFloatField(const BlendStruct* St, const uint8_t* BlockBytes, size_t Limit, const char* Name, float Fallback = 0.0f) noexcept
{
    uint32_t Sz = 0u;
    const uint8_t* P = FieldSlice(St, BlockBytes, Limit, Name, Sz);
    return (P && Sz >= 4u) ? ReadF32(P) : Fallback;
}

[[nodiscard]] std::string ReadCStringField(const BlendStruct* St, const uint8_t* BlockBytes, size_t Limit, const char* Name)
{
    uint32_t Sz = 0u;
    const uint8_t* P = FieldSlice(St, BlockBytes, Limit, Name, Sz);
    if (!P || Sz == 0u) return {};
    size_t Len = 0u;
    while (Len < Sz && P[Len] != 0u) ++Len;
    return std::string(reinterpret_cast<const char*>(P), Len);
}

[[nodiscard]] std::string ReadIdName(const BlendDocument& Doc, const BlendStruct* St, const uint8_t* BlockBytes, size_t Limit)
{
    uint32_t IdSz = 0u;
    const uint8_t* IdPtr = FieldSlice(St, BlockBytes, Limit, "id", IdSz);
    const BlendStruct* IdSt = Doc.FindStruct("ID");
    if (!IdPtr || !IdSt) return {};
    std::string Raw = ReadCStringField(IdSt, IdPtr, IdSz, "name");
    if (Raw.size() > 2u) return Raw.substr(2u);                                              // Strip Blender 2-char ID prefix ("OB", "ME", "GR")
    return Raw;
}

bool ParseBlendBinary(BlendDocument& Doc, std::string& Refusal)
{
    if (Doc.Bytes.size() < 24u || std::memcmp(Doc.Bytes.data(), "BLENDER", 7u) != 0)
    {
        Refusal = "File does not start with BLENDER signature";
        return false;
    }

    size_t Pos = 0u;
    bool LargeBHead = false;
    const uint8_t* H = Doc.Bytes.data();

    if (H[7] == '_' || H[7] == '-')
    {
        // Legacy Blender <= 4.x 12-byte header
        Doc.PointerSize = (H[7] == '-') ? 8u : 4u;
        if (H[8] == 'V')
        {
            Refusal = "Big-endian .blend files are not supported";
            return false;
        }
        Doc.VersionText = std::string(1, char(H[9])) + "." + std::string(reinterpret_cast<const char*>(H + 10), 2u);
        Pos = 12u;
    }
    else if (H[7] >= '0' && H[7] <= '9' && H[8] >= '0' && H[8] <= '9')
    {
        // Blender 5.0+ extended header (e.g. "BLENDER17-01v0502")
        const size_t HeaderLen = size_t(H[7] - '0') * 10u + size_t(H[8] - '0');
        if (HeaderLen < 17u || HeaderLen > Doc.Bytes.size())
        {
            Refusal = "Invalid Blender 5.x header length";
            return false;
        }
        Doc.PointerSize = (H[9] == '-') ? 8u : 4u;
        if (H[12] == 'V')
        {
            Refusal = "Big-endian .blend files are not supported";
            return false;
        }
        LargeBHead = true;
        const int Major = (H[13] - '0') * 10 + (H[14] - '0');
        char VerBuf[16];
        std::snprintf(VerBuf, sizeof(VerBuf), "%d.%c%c", Major, char(H[15]), char(H[16]));
        Doc.VersionText = VerBuf;
        Pos = HeaderLen;
    }
    else
    {
        Refusal = "Unrecognized .blend header format";
        return false;
    }

    const BlendBlock* DnaBlock = nullptr;
    size_t DnaIndex = SIZE_MAX;
    uint64_t CurrentOwnerId = 0u;

    while (Pos < Doc.Bytes.size())
    {
        BlendBlock B;
        if (LargeBHead)
        {
            if (Pos + 32u > Doc.Bytes.size()) break;
            std::memcpy(B.Code, H + Pos, 4u);
            B.Code[4]   = '\0';
            B.SdnaIndex = ReadU32(H + Pos + 4u);
            B.OldAddr   = ReadU64(H + Pos + 8u);
            B.ByteSize  = static_cast<size_t>(ReadU64(H + Pos + 16u));
            B.Count     = ReadU64(H + Pos + 24u);
            Pos += 32u;
        }
        else
        {
            const size_t HeadSize = (Doc.PointerSize == 8u) ? 24u : 20u;
            if (Pos + HeadSize > Doc.Bytes.size()) break;
            std::memcpy(B.Code, H + Pos, 4u);
            B.Code[4]   = '\0';
            B.ByteSize  = ReadU32(H + Pos + 4u);
            B.OldAddr   = (Doc.PointerSize == 8u) ? ReadU64(H + Pos + 8u) : ReadU32(H + Pos + 8u);
            B.SdnaIndex = ReadU32(H + Pos + 8u + Doc.PointerSize);
            B.Count     = ReadU32(H + Pos + 12u + Doc.PointerSize);
            Pos += HeadSize;
        }

        if (Pos + B.ByteSize > Doc.Bytes.size()) break;
        B.Offset = Pos;
        Pos += B.ByteSize;

        const size_t Idx = Doc.Blocks.size();
        Doc.Blocks.push_back(B);
        if (std::strcmp(B.Code, "DATA") == 0)
        {
            if (B.OldAddr != 0u)
            {
                if (CurrentOwnerId != 0u) Doc.LocalPayloadsById[CurrentOwnerId][B.OldAddr] = Idx;
                Doc.BlockByAddr[B.OldAddr] = Idx;
            }
        }
        else if (std::strcmp(B.Code, "REND") != 0 && std::strcmp(B.Code, "TEST") != 0 &&
                 std::strcmp(B.Code, "GLOB") != 0 && std::strcmp(B.Code, "DNA1") != 0 &&
                 std::strcmp(B.Code, "ENDB") != 0)
        {
            CurrentOwnerId = B.OldAddr;
            if (B.OldAddr != 0u)
            {
                Doc.IdBlockByAddr[B.OldAddr] = Idx;
                Doc.BlockByAddr[B.OldAddr]   = Idx;
            }
        }
        else
        {
            CurrentOwnerId = 0u;
            if (B.OldAddr != 0u) Doc.BlockByAddr[B.OldAddr] = Idx;
        }
        if (std::strcmp(B.Code, "DNA1") == 0) DnaIndex = Idx;
        if (std::strcmp(B.Code, "ENDB") == 0) break;
    }

    if (DnaIndex == SIZE_MAX)
    {
        Refusal = "Missing DNA1 schema block in .blend file";
        return false;
    }
    DnaBlock = &Doc.Blocks[DnaIndex];

    const uint8_t* Dna = Doc.BlockPtr(*DnaBlock);
    const size_t   DnaLen = DnaBlock->ByteSize;
    if (DnaLen < 16u || std::memcmp(Dna, "SDNANAME", 8u) != 0)
    {
        Refusal = "Malformed SDNA schema block";
        return false;
    }

    size_t P = 8u;
    const uint32_t NameCount = ReadU32(Dna + P); P += 4u;
    std::vector<std::string> Names;
    Names.reserve(NameCount);
    for (uint32_t I = 0u; I < NameCount && P < DnaLen; ++I)
    {
        const size_t Start = P;
        while (P < DnaLen && Dna[P] != 0u) ++P;
        Names.emplace_back(reinterpret_cast<const char*>(Dna + Start), P - Start);
        ++P;
    }
    P = (P + 3u) & ~size_t(3u);

    if (P + 8u > DnaLen || std::memcmp(Dna + P, "TYPE", 4u) != 0) { Refusal = "Missing SDNA TYPE section"; return false; }
    P += 4u;
    const uint32_t TypeCount = ReadU32(Dna + P); P += 4u;
    std::vector<std::string> Types;
    Types.reserve(TypeCount);
    for (uint32_t I = 0u; I < TypeCount && P < DnaLen; ++I)
    {
        const size_t Start = P;
        while (P < DnaLen && Dna[P] != 0u) ++P;
        Types.emplace_back(reinterpret_cast<const char*>(Dna + Start), P - Start);
        ++P;
    }
    P = (P + 3u) & ~size_t(3u);

    if (P + 4u + size_t(TypeCount) * 2u > DnaLen || std::memcmp(Dna + P, "TLEN", 4u) != 0) { Refusal = "Missing SDNA TLEN section"; return false; }
    P += 4u;
    std::vector<uint16_t> TypeLens(TypeCount);
    for (uint32_t I = 0u; I < TypeCount; ++I) { TypeLens[I] = ReadU16(Dna + P); P += 2u; }
    P = (P + 3u) & ~size_t(3u);

    if (P + 8u > DnaLen || std::memcmp(Dna + P, "STRC", 4u) != 0) { Refusal = "Missing SDNA STRC section"; return false; }
    P += 4u;
    const uint32_t StructCount = ReadU32(Dna + P); P += 4u;
    Doc.Structs.reserve(StructCount);

    for (uint32_t S = 0u; S < StructCount && P + 4u <= DnaLen; ++S)
    {
        const uint16_t TypeIdx    = ReadU16(Dna + P); P += 2u;
        const uint16_t FieldCount = ReadU16(Dna + P); P += 2u;
        BlendStruct St;
        St.Name     = TypeIdx < Types.size() ? Types[TypeIdx] : std::string{};
        St.ByteSize = TypeIdx < TypeLens.size() ? TypeLens[TypeIdx] : 0u;
        St.Fields.reserve(FieldCount);

        uint32_t Offset = 0u;
        for (uint16_t F = 0u; F < FieldCount && P + 4u <= DnaLen; ++F)
        {
            const uint16_t FTypeIdx = ReadU16(Dna + P); P += 2u;
            const uint16_t FNameIdx = ReadU16(Dna + P); P += 2u;
            const std::string& RawName = FNameIdx < Names.size() ? Names[FNameIdx] : std::string{};
            const std::string& FType   = FTypeIdx < Types.size() ? Types[FTypeIdx] : std::string{};
            const bool PointerLike     = (RawName.find('*') != std::string::npos) || (RawName.find('(') != std::string::npos);
            uint32_t ElemSize          = PointerLike ? Doc.PointerSize : (FTypeIdx < TypeLens.size() ? TypeLens[FTypeIdx] : 0u);

            uint32_t ArrayCount = 1u;
            size_t Bracket = RawName.find('[');
            while (Bracket != std::string::npos)
            {
                const size_t Close = RawName.find(']', Bracket);
                if (Close == std::string::npos) break;
                const uint32_t Dim = static_cast<uint32_t>(std::strtoul(RawName.c_str() + Bracket + 1u, nullptr, 10));
                if (Dim > 0u) ArrayCount *= Dim;
                Bracket = RawName.find('[', Close + 1u);
            }

            std::string Clean = RawName;
            while (!Clean.empty() && Clean.front() == '*') Clean.erase(Clean.begin());
            const size_t CleanBracket = Clean.find('[');
            if (CleanBracket != std::string::npos) Clean.resize(CleanBracket);

            const uint32_t FieldBytes = ElemSize * ArrayCount;
            St.FieldLookup[Clean] = static_cast<uint32_t>(St.Fields.size());
            St.Fields.push_back({ std::move(Clean), RawName, FType, Offset, FieldBytes });
            Offset += FieldBytes;
        }
        Doc.StructByName[St.Name] = Doc.Structs.size();
        Doc.Structs.push_back(std::move(St));
    }

    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    POLYGON SURFACE
//------------------------------------------------------------------------------------------------------------------------

struct PolyFace
{
    std::vector<uint32_t> Verts        = {};
    int                   MaterialSlot = 0;
    bool                  SharpFace    = false;
};

struct PolySurface
{
    std::vector<Vec3d>                              Positions   = {};
    std::vector<float>                              VertCreases = {};
    std::vector<std::vector<std::pair<int, float>>> VertWeights = {};
    std::vector<std::array<float, 3>>               SkinRadii   = {};
    std::vector<std::string>                        GroupNames  = {};
    std::vector<PolyFace>                           Faces       = {};
    std::unordered_map<uint64_t, float>             EdgeCreases = {};
    std::unordered_set<uint64_t>                    SharpEdges  = {};
    std::unordered_map<uint64_t, float>             BevelWeights = {};
    std::vector<std::pair<uint32_t, uint32_t>>      WireEdges   = {};                        // For Skin modifier input

    [[nodiscard]] float QueryCrease(uint32_t A, uint32_t B) const noexcept
    {
        const auto It = EdgeCreases.find(EdgeKeyOf(A, B));
        return It != EdgeCreases.end() ? It->second : 0.0f;
    }

    [[nodiscard]] bool QuerySharp(uint32_t A, uint32_t B) const noexcept
    {
        return SharpEdges.find(EdgeKeyOf(A, B)) != SharpEdges.end();
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    MODIFIER DESCRIPTORS
//------------------------------------------------------------------------------------------------------------------------

enum class BlendModifierKind : uint8_t
{
    Mirror     = 0,
    Subsurf    = 1,
    Solidify   = 2,
    Shrinkwrap = 3,
    Mask       = 4,
    Array      = 5,
    Bevel      = 6,
    Skin       = 7,
    AutoSmooth = 8,
};

struct BlendModifierDesc
{
    BlendModifierKind Kind = BlendModifierKind::Mirror;
    std::string       Label = {};

    // Mirror
    uint32_t MirrorBits      = 0u;
    float    MergeTolerance  = 0.001f;
    float    BisectThreshold = 0.001f;
    uint64_t MirrorObAddr    = 0u;

    // Subsurf
    int      SubdivKind     = 0;                                                             // 0 Catmull-Clark, 1 Simple
    int      SubdivLevels   = 1;
    uint32_t SubsurfBits    = SubsurfUseCreaseBit;
    int      BoundarySmooth = 0;                                                             // 0 All, 1 PreserveCorners

    // Solidify
    float    SolidifyOffset    = 0.01f;
    float    SolidifyOffsetFac = -1.0f;
    float    SolidifyCreaseRim = 0.0f;
    uint32_t SolidifyBits      = SolidifyRimBit;

    // Shrinkwrap
    uint64_t ShrinkTargetAddr = 0u;
    int      ShrinkKind       = 0;                                                           // 0 NearestSurface, 1 Project, 2 NearestVert
    float    ShrinkKeepDist   = 0.0f;

    // Mask
    std::string MaskGroup  = {};
    uint32_t    MaskBits   = 0u;
    float       MaskThresh = 0.0f;

    // Array
    int      ArrayCount      = 1;
    uint32_t ArrayOffsetBits = 0u;
    float    ArrayConst[3]   = { 0.0f, 0.0f, 0.0f };
    float    ArrayRel[3]     = { 1.0f, 0.0f, 0.0f };

    // Bevel
    float    BevelWidth    = 0.0f;
    int      BevelSegments = 1;
    uint32_t BevelLimBits  = 0u;
    float    BevelAngleRad = 0.5236f;
};

struct BlendEntityEntry
{
    uint64_t                       ObjAddr          = 0u;
    uint64_t                       SurfaceBlockAddr         = 0u;
    uint64_t                       ParentAddr       = 0u;
    std::string                    Name             = {};
    std::string                    Collection       = {};
    bool                           VisibleCondition = true;
    Mat4d                          LocalTrs         = {};
    Mat4d                          ParentInv        = {};
    Mat4d                          WorldMatrix      = {};
    std::vector<BlendModifierDesc> Modifiers        = {};
    std::array<float, 3>           SurfaceColour       = { 0.68f, 0.72f, 0.76f };
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    MODIFIER EVALUATORS
//------------------------------------------------------------------------------------------------------------------------

void ApplyMaskModifier(PolySurface& Surf, const BlendModifierDesc& Mod)
{
    if (Mod.MaskGroup.empty() || Surf.VertWeights.size() != Surf.Positions.size()) return;
    int GroupIndex = -1;
    for (size_t I = 0u; I < Surf.GroupNames.size(); ++I)
    {
        if (Surf.GroupNames[I] == Mod.MaskGroup) { GroupIndex = static_cast<int>(I); break; }
    }
    if (GroupIndex < 0) return;

    const bool Invert = (Mod.MaskBits & 1u) != 0u;
    std::vector<uint8_t> KeepVert(Surf.Positions.size(), 0u);
    for (size_t V = 0u; V < Surf.Positions.size(); ++V)
    {
        float W = 0.0f;
        for (const auto& Entry : Surf.VertWeights[V])
        {
            if (Entry.first == GroupIndex) { W = Entry.second; break; }
        }
        const bool InGroup = W > Mod.MaskThresh;
        KeepVert[V] = (Invert ? !InGroup : InGroup) ? 1u : 0u;
    }

    std::vector<PolyFace> KeptFaces;
    KeptFaces.reserve(Surf.Faces.size());
    for (PolyFace& Face : Surf.Faces)
    {
        bool AllKept = true;
        for (uint32_t V : Face.Verts)
        {
            if (V >= KeepVert.size() || !KeepVert[V]) { AllKept = false; break; }
        }
        if (AllKept) KeptFaces.push_back(std::move(Face));
    }
    Surf.Faces = std::move(KeptFaces);
}

void ApplySkinModifier(PolySurface& Surf)
{
    if (Surf.WireEdges.empty() || Surf.Positions.empty()) return;
    PolySurface Out;
    Out.GroupNames = Surf.GroupNames;

    for (const auto& Edge : Surf.WireEdges)
    {
        if (Edge.first >= Surf.Positions.size() || Edge.second >= Surf.Positions.size()) continue;
        const Vec3d P0 = Surf.Positions[Edge.first];
        const Vec3d P1 = Surf.Positions[Edge.second];
        const Vec3d Dir = P1 - P0;
        const double Len = std::sqrt(Dot3(Dir, Dir));
        if (Len < 1.0e-7) continue;

        const Vec3d Axis = Dir * (1.0 / Len);
        const Vec3d Up = (std::abs(Axis.Z) < 0.9) ? Vec3d{ 0.0, 0.0, 1.0 } : Vec3d{ 1.0, 0.0, 0.0 };
        const Vec3d SideX = Normalize3(Cross3(Axis, Up));
        const Vec3d SideY = Normalize3(Cross3(Axis, SideX));

        auto RadiusFor = [&](uint32_t Vi) -> std::pair<double, double>
        {
            if (Vi < Surf.SkinRadii.size())
            {
                const double Rx = std::max(0.01, double(Surf.SkinRadii[Vi][0]));
                const double Ry = std::max(0.01, double(Surf.SkinRadii[Vi][1]));
                return { Rx, Ry };
            }
            return { 0.25, 0.25 };
        };

        const auto [Rx0, Ry0] = RadiusFor(Edge.first);
        const auto [Rx1, Ry1] = RadiusFor(Edge.second);
        const uint32_t BlockBytes = static_cast<uint32_t>(Out.Positions.size());
        const double Sx[4] = { -1.0, 1.0, 1.0, -1.0 };
        const double Sy[4] = { -1.0, -1.0, 1.0, 1.0 };
        for (int K = 0; K < 4; ++K)
        {
            Out.Positions.push_back(P0 + SideX * (Sx[K] * Rx0) + SideY * (Sy[K] * Ry0));
            Out.VertCreases.push_back(0.0f);
        }
        for (int K = 0; K < 4; ++K)
        {
            Out.Positions.push_back(P1 + SideX * (Sx[K] * Rx1) + SideY * (Sy[K] * Ry1));
            Out.VertCreases.push_back(0.0f);
        }
        for (uint32_t K = 0u; K < 4u; ++K)
        {
            const uint32_t Next = (K + 1u) & 3u;
            Out.Faces.push_back({ { BlockBytes + K, BlockBytes + Next, BlockBytes + 4u + Next, BlockBytes + 4u + K }, 0, false });
        }
        Out.Faces.push_back({ { BlockBytes + 3u, BlockBytes + 2u, BlockBytes + 1u, BlockBytes + 0u }, 0, false });
        Out.Faces.push_back({ { BlockBytes + 4u, BlockBytes + 5u, BlockBytes + 6u, BlockBytes + 7u }, 0, false });
    }

    if (!Out.Faces.empty()) Surf = std::move(Out);
}

void ApplyMirrorAxis(
    PolySurface& Surf,
    int          Axis,
    bool         Bisect,
    bool         BisectFlip,
    bool         Merge,
    double       MergeTolerance,
    const Mat4d* IntoMirrorSpace,
    const Mat4d* FromMirrorSpace)
{
    if (Surf.Positions.empty() || Surf.Faces.empty()) return;

    // Optional transform into mirror_ob space
    if (IntoMirrorSpace)
    {
        for (Vec3d& P : Surf.Positions) P = TransformPoint(*IntoMirrorSpace, P);
    }

    // Optional bisect clipping against coordinate plane Axis == 0
    if (Bisect)
    {
        const double Sign = BisectFlip ? -1.0 : 1.0;
        const double Eps  = std::max(1.0e-7, MergeTolerance * 0.25);

        std::unordered_map<uint64_t, uint32_t> SplitCache;
        std::vector<PolyFace> ClippedFaces;
        ClippedFaces.reserve(Surf.Faces.size());

        auto GetSplitVert = [&](uint32_t A, uint32_t B) -> uint32_t
        {
            const uint64_t Key = EdgeKeyOf(A, B);
            const auto It = SplitCache.find(Key);
            if (It != SplitCache.end()) return It->second;

            const Vec3d& Pa = Surf.Positions[A];
            const Vec3d& Pb = Surf.Positions[B];
            const double Da = Pa[Axis] * Sign;
            const double Db = Pb[Axis] * Sign;
            const double T  = (std::abs(Da - Db) > 1.0e-12) ? std::clamp(Da / (Da - Db), 0.0, 1.0) : 0.5;
            Vec3d Mid = Pa + (Pb - Pa) * T;
            Mid[Axis] = 0.0;

            const uint32_t NewIdx = static_cast<uint32_t>(Surf.Positions.size());
            Surf.Positions.push_back(Mid);
            if (Surf.VertCreases.size() > std::max(A, B))
                Surf.VertCreases.push_back(0.5f * (Surf.VertCreases[A] + Surf.VertCreases[B]));
            else if (!Surf.VertCreases.empty())
                Surf.VertCreases.push_back(0.0f);
            if (!Surf.VertWeights.empty()) Surf.VertWeights.emplace_back();

            const float EdgeCr = Surf.QueryCrease(A, B);
            if (EdgeCr > 0.0f)
            {
                Surf.EdgeCreases[EdgeKeyOf(A, NewIdx)] = EdgeCr;
                Surf.EdgeCreases[EdgeKeyOf(NewIdx, B)] = EdgeCr;
            }
            if (Surf.QuerySharp(A, B))
            {
                Surf.SharpEdges.insert(EdgeKeyOf(A, NewIdx));
                Surf.SharpEdges.insert(EdgeKeyOf(NewIdx, B));
            }
            SplitCache[Key] = NewIdx;
            return NewIdx;
        };

        for (const PolyFace& Face : Surf.Faces)
        {
            std::vector<uint32_t> OutVerts;
            const size_t N = Face.Verts.size();
            for (size_t I = 0u; I < N; ++I)
            {
                const uint32_t A = Face.Verts[I];
                const uint32_t B = Face.Verts[(I + 1u) % N];
                const double Da = Surf.Positions[A][Axis] * Sign;
                const double Db = Surf.Positions[B][Axis] * Sign;
                const bool InA = Da >= -Eps;
                const bool InB = Db >= -Eps;

                if (InA && InB)
                {
                    OutVerts.push_back(A);
                }
                else if (InA && !InB)
                {
                    OutVerts.push_back(A);
                    if (Da > Eps) OutVerts.push_back(GetSplitVert(A, B));
                }
                else if (!InA && InB)
                {
                    if (Db > Eps) OutVerts.push_back(GetSplitVert(A, B));
                }
            }

            // Deduplicate consecutive indices
            std::vector<uint32_t> CleanVerts;
            CleanVerts.reserve(OutVerts.size());
            for (uint32_t V : OutVerts)
            {
                if (CleanVerts.empty() || CleanVerts.back() != V) CleanVerts.push_back(V);
            }
            if (CleanVerts.size() >= 2u && CleanVerts.front() == CleanVerts.back()) CleanVerts.pop_back();
            if (CleanVerts.size() >= 3u)
            {
                ClippedFaces.push_back({ std::move(CleanVerts), Face.MaterialSlot, Face.SharpFace });
            }
        }
        Surf.Faces = std::move(ClippedFaces);
    }

    const size_t OrigVertCount = Surf.Positions.size();
    const size_t OrigFaceCount = Surf.Faces.size();
    const double Tol           = std::max(1.0e-6, MergeTolerance);

    std::vector<uint32_t> MirrorVert(OrigVertCount, 0u);
    Surf.Positions.reserve(OrigVertCount * 2u);
    if (!Surf.VertCreases.empty()) Surf.VertCreases.reserve(OrigVertCount * 2u);
    if (!Surf.VertWeights.empty()) Surf.VertWeights.reserve(OrigVertCount * 2u);

    for (size_t V = 0u; V < OrigVertCount; ++V)
    {
        if (Merge && std::abs(Surf.Positions[V][Axis]) <= Tol)
        {
            Surf.Positions[V][Axis] = 0.0;
            MirrorVert[V] = static_cast<uint32_t>(V);
        }
        else
        {
            Vec3d Mirrored = Surf.Positions[V];
            Mirrored[Axis] = -Mirrored[Axis];
            MirrorVert[V] = static_cast<uint32_t>(Surf.Positions.size());
            Surf.Positions.push_back(Mirrored);
            if (V < Surf.VertCreases.size()) Surf.VertCreases.push_back(Surf.VertCreases[V]);
            if (V < Surf.VertWeights.size()) Surf.VertWeights.push_back(Surf.VertWeights[V]);
        }
    }

    // Copy edge attributes to mirrored edges
    std::vector<std::pair<uint64_t, float>> AddedCreases;
    AddedCreases.reserve(Surf.EdgeCreases.size());
    for (const auto& Entry : Surf.EdgeCreases)
    {
        const uint32_t A = static_cast<uint32_t>(Entry.first >> 32u);
        const uint32_t B = static_cast<uint32_t>(Entry.first & 0xFFFFFFFFu);
        if (A < OrigVertCount && B < OrigVertCount)
        {
            const uint32_t Ma = MirrorVert[A], Mb = MirrorVert[B];
            if (Ma != Mb) AddedCreases.emplace_back(EdgeKeyOf(Ma, Mb), Entry.second);
        }
    }
    for (const auto& Added : AddedCreases) Surf.EdgeCreases[Added.first] = Added.second;

    std::vector<uint64_t> AddedSharps;
    AddedSharps.reserve(Surf.SharpEdges.size());
    for (uint64_t Key : Surf.SharpEdges)
    {
        const uint32_t A = static_cast<uint32_t>(Key >> 32u);
        const uint32_t B = static_cast<uint32_t>(Key & 0xFFFFFFFFu);
        if (A < OrigVertCount && B < OrigVertCount)
        {
            const uint32_t Ma = MirrorVert[A], Mb = MirrorVert[B];
            if (Ma != Mb) AddedSharps.push_back(EdgeKeyOf(Ma, Mb));
        }
    }
    for (uint64_t Added : AddedSharps) Surf.SharpEdges.insert(Added);

    std::vector<std::pair<uint64_t, float>> AddedBevels;
    for (const auto& Entry : Surf.BevelWeights)
    {
        const uint32_t A = static_cast<uint32_t>(Entry.first >> 32u);
        const uint32_t B = static_cast<uint32_t>(Entry.first & 0xFFFFFFFFu);
        if (A < OrigVertCount && B < OrigVertCount)
        {
            const uint32_t Ma = MirrorVert[A], Mb = MirrorVert[B];
            if (Ma != Mb) AddedBevels.emplace_back(EdgeKeyOf(Ma, Mb), Entry.second);
        }
    }
    for (const auto& Added : AddedBevels) Surf.BevelWeights[Added.first] = Added.second;

    // Append mirrored faces with reversed winding so outward normals match
    Surf.Faces.reserve(OrigFaceCount * 2u);
    for (size_t F = 0u; F < OrigFaceCount; ++F)
    {
        const PolyFace& Src = Surf.Faces[F];
        PolyFace Dst;
        Dst.MaterialSlot = Src.MaterialSlot;
        Dst.SharpFace    = Src.SharpFace;
        Dst.Verts.resize(Src.Verts.size());
        const size_t N = Src.Verts.size();
        for (size_t I = 0u; I < N; ++I)
        {
            Dst.Verts[N - 1u - I] = MirrorVert[Src.Verts[I]];
        }
        Surf.Faces.push_back(std::move(Dst));
    }

    if (FromMirrorSpace)
    {
        for (Vec3d& P : Surf.Positions) P = TransformPoint(*FromMirrorSpace, P);
    }
}

void ApplyMirrorModifier(
    PolySurface&                             Surf,
    const BlendModifierDesc&                 Mod,
    const Mat4d&                             ObjWorld,
    const std::unordered_map<uint64_t, Mat4d>& WorldByAddr)
{
    Mat4d IntoSpace, FromSpace;
    const Mat4d* IntoPtr = nullptr;
    const Mat4d* FromPtr = nullptr;
    if (Mod.MirrorObAddr != 0u)
    {
        const auto It = WorldByAddr.find(Mod.MirrorObAddr);
        if (It != WorldByAddr.end())
        {
            const Mat4d InvMirror = InvertAffine(It->second);
            const Mat4d InvObj    = InvertAffine(ObjWorld);
            IntoSpace = MultiplyMat4(InvMirror, ObjWorld);
            FromSpace = MultiplyMat4(InvObj, It->second);
            IntoPtr   = &IntoSpace;
            FromPtr   = &FromSpace;
        }
    }

    const bool Merge = (Mod.MirrorBits & MirrorNoMergeBit) == 0u;
    const double Tol = std::max(1.0e-5, double(Mod.MergeTolerance));

    if (Mod.MirrorBits & MirrorAxisXBit)
        ApplyMirrorAxis(Surf, 0, (Mod.MirrorBits & MirrorBisectXBit) != 0u, (Mod.MirrorBits & MirrorBisectFlipXBit) != 0u, Merge, Tol, IntoPtr, FromPtr);
    if (Mod.MirrorBits & MirrorAxisYBit)
        ApplyMirrorAxis(Surf, 1, (Mod.MirrorBits & MirrorBisectYBit) != 0u, (Mod.MirrorBits & MirrorBisectFlipYBit) != 0u, Merge, Tol, IntoPtr, FromPtr);
    if (Mod.MirrorBits & MirrorAxisZBit)
        ApplyMirrorAxis(Surf, 2, (Mod.MirrorBits & MirrorBisectZBit) != 0u, (Mod.MirrorBits & MirrorBisectFlipZBit) != 0u, Merge, Tol, IntoPtr, FromPtr);
}

// Single iteration of Catmull-Clark (with OpenSubdiv/Blender semi-sharp creases & boundary modes) or Simple subdivision.
void SubdivideOnce(PolySurface& Surf, bool SimpleMode, bool UseCrease, bool PreserveCorners, int Iteration)
{
    const size_t VertCount = Surf.Positions.size();
    const size_t FaceCount = Surf.Faces.size();
    if (VertCount == 0u || FaceCount == 0u) return;

    // 1. Face centroids
    std::vector<Vec3d> FacePoints(FaceCount);
    for (size_t F = 0u; F < FaceCount; ++F)
    {
        const auto& VList = Surf.Faces[F].Verts;
        Vec3d Sum{};
        for (uint32_t V : VList) Sum = Sum + Surf.Positions[V];
        FacePoints[F] = VList.empty() ? Vec3d{} : Sum * (1.0 / double(VList.size()));
    }

    // 2. Build unique edge catalog & adjacency
    struct SubdivEdge
    {
        uint32_t A         = 0u;
        uint32_t B         = 0u;
        uint32_t FaceA     = UINT32_MAX;
        uint32_t FaceB     = UINT32_MAX;
        uint32_t FaceUses  = 0u;
        float    Crease    = 0.0f;
        bool     Sharp     = false;
        float    BevelW    = 0.0f;
        uint32_t MidVert   = 0u;
    };

    std::unordered_map<uint64_t, uint32_t> EdgeToIdx;
    EdgeToIdx.reserve(FaceCount * 2u + 16u);
    std::vector<SubdivEdge> Edges;
    Edges.reserve(FaceCount * 2u + 16u);

    std::vector<std::vector<uint32_t>> VertFaces(VertCount);
    std::vector<std::vector<uint32_t>> VertEdges(VertCount);

    for (uint32_t F = 0u; F < FaceCount; ++F)
    {
        const auto& VList = Surf.Faces[F].Verts;
        const size_t N = VList.size();
        for (size_t I = 0u; I < N; ++I)
        {
            const uint32_t A = VList[I];
            const uint32_t B = VList[(I + 1u) % N];
            VertFaces[A].push_back(F);

            const uint64_t Key = EdgeKeyOf(A, B);
            auto It = EdgeToIdx.find(Key);
            if (It == EdgeToIdx.end())
            {
                const uint32_t EIdx = static_cast<uint32_t>(Edges.size());
                EdgeToIdx[Key] = EIdx;
                SubdivEdge E;
                E.A        = std::min(A, B);
                E.B        = std::max(A, B);
                E.FaceA    = F;
                E.FaceUses = 1u;
                E.Crease   = UseCrease ? Surf.QueryCrease(A, B) : 0.0f;
                E.Sharp    = Surf.QuerySharp(A, B);
                const auto BIt = Surf.BevelWeights.find(Key);
                if (BIt != Surf.BevelWeights.end()) E.BevelW = BIt->second;
                Edges.push_back(E);
                VertEdges[E.A].push_back(EIdx);
                VertEdges[E.B].push_back(EIdx);
            }
            else
            {
                SubdivEdge& E = Edges[It->second];
                if (E.FaceUses == 1u) E.FaceB = F;
                ++E.FaceUses;
            }
        }
    }

    auto EffectiveCrease = [&](float RawCrease) noexcept -> float
    {
        if (!UseCrease || RawCrease <= 1.0e-5f) return 0.0f;
        if (RawCrease >= 0.999f) return 1.0f;
        const float Sharpness = RawCrease * 10.0f - float(Iteration);
        return std::clamp(Sharpness, 0.0f, 1.0f);
    };

    // 3. Allocate new vertices: [0..VertCount) updated originals, [VertCount..+FaceCount) face points, [+Edges) edge points
    const size_t TotalNewVerts = VertCount + FaceCount + Edges.size();
    PolySurface Next;
    Next.Positions.resize(TotalNewVerts);
    Next.GroupNames = Surf.GroupNames;
    if (!Surf.VertCreases.empty()) Next.VertCreases.resize(TotalNewVerts, 0.0f);
    const bool HasWeights = (Surf.VertWeights.size() == VertCount && !Surf.GroupNames.empty());
    if (HasWeights) Next.VertWeights.resize(TotalNewVerts);

    // 3a. Face points
    for (size_t F = 0u; F < FaceCount; ++F)
    {
        Next.Positions[VertCount + F] = FacePoints[F];
        if (HasWeights && !Surf.Faces[F].Verts.empty())
        {
            Next.VertWeights[VertCount + F] = Surf.VertWeights[Surf.Faces[F].Verts[0]];
        }
    }

    // 3b. Edge points
    const uint32_t EdgeVertStart = static_cast<uint32_t>(VertCount + FaceCount);
    for (size_t EIdx = 0u; EIdx < Edges.size(); ++EIdx)
    {
        SubdivEdge& E = Edges[EIdx];
        E.MidVert = EdgeVertStart + static_cast<uint32_t>(EIdx);
        const Vec3d Mid = (Surf.Positions[E.A] + Surf.Positions[E.B]) * 0.5;
        if (SimpleMode || E.FaceUses != 2u)
        {
            Next.Positions[E.MidVert] = Mid;
        }
        else
        {
            const Vec3d Smooth = (Surf.Positions[E.A] + Surf.Positions[E.B] + FacePoints[E.FaceA] + FacePoints[E.FaceB]) * 0.25;
            const float W = EffectiveCrease(E.Crease);
            Next.Positions[E.MidVert] = Smooth * (1.0 - double(W)) + Mid * double(W);
        }
        if (HasWeights) Next.VertWeights[E.MidVert] = Surf.VertWeights[E.A];
    }

    // 3c. Updated original vertices
    for (size_t V = 0u; V < VertCount; ++V)
    {
        const Vec3d& Orig = Surf.Positions[V];
        if (V < Surf.VertCreases.size()) Next.VertCreases[V] = Surf.VertCreases[V];
        if (HasWeights) Next.VertWeights[V] = Surf.VertWeights[V];

        if (SimpleMode || VertEdges[V].empty() || VertFaces[V].empty())
        {
            Next.Positions[V] = Orig;
            continue;
        }

        // Collect boundary edges & creased edges incident to V
        uint32_t BoundaryCount = 0u;
        Vec3d BoundaryNeighborSum{};
        uint32_t CreaseCount = 0u;
        double CreaseWeightSum = 0.0;
        Vec3d CreaseNeighborSum{};

        for (uint32_t EIdx : VertEdges[V])
        {
            const SubdivEdge& E = Edges[EIdx];
            const uint32_t Other = (E.A == V) ? E.B : E.A;
            if (E.FaceUses == 1u)
            {
                ++BoundaryCount;
                BoundaryNeighborSum = BoundaryNeighborSum + Surf.Positions[Other];
            }
            const float W = EffectiveCrease(E.Crease);
            if (W > 1.0e-5f)
            {
                ++CreaseCount;
                CreaseWeightSum += double(W);
                CreaseNeighborSum = CreaseNeighborSum + Surf.Positions[Other];
            }
        }

        const float VertCreaseRaw = (UseCrease && V < Surf.VertCreases.size()) ? Surf.VertCreases[V] : 0.0f;
        const float VertCreaseW   = EffectiveCrease(VertCreaseRaw);

        // Boundary vertex rule
        if (BoundaryCount > 0u)
        {
            if (BoundaryCount != 2u || (PreserveCorners && VertEdges[V].size() == 2u) || VertCreaseW >= 0.999f)
            {
                Next.Positions[V] = Orig;
            }
            else
            {
                const Vec3d SmoothBound = Orig * 0.75 + BoundaryNeighborSum * 0.125;
                Next.Positions[V] = SmoothBound * (1.0 - double(VertCreaseW)) + Orig * double(VertCreaseW);
            }
            continue;
        }

        // Standard Catmull-Clark interior smooth position: (F + 2*R + (n - 3)*V) / n
        const double Nf = double(VertFaces[V].size());
        const double Ne = double(VertEdges[V].size());
        if (Nf < 2.0 || Ne < 3.0)
        {
            Next.Positions[V] = Orig;
            continue;
        }

        Vec3d FaceAvg{};
        for (uint32_t F : VertFaces[V]) FaceAvg = FaceAvg + FacePoints[F];
        FaceAvg = FaceAvg * (1.0 / Nf);

        Vec3d EdgeMidAvg{};
        for (uint32_t EIdx : VertEdges[V])
        {
            const SubdivEdge& E = Edges[EIdx];
            EdgeMidAvg = EdgeMidAvg + (Surf.Positions[E.A] + Surf.Positions[E.B]) * 0.5;
        }
        EdgeMidAvg = EdgeMidAvg * (1.0 / Ne);

        const Vec3d SmoothPos = (FaceAvg + EdgeMidAvg * 2.0 + Orig * (Ne - 3.0)) * (1.0 / Ne);

        if (CreaseCount >= 3u)
        {
            // Corner vertex: blend smooth -> pinned Orig
            const double Blend = std::clamp(std::max(double(VertCreaseW), CreaseWeightSum / double(CreaseCount)), 0.0, 1.0);
            Next.Positions[V] = SmoothPos * (1.0 - Blend) + Orig * Blend;
        }
        else if (CreaseCount == 2u)
        {
            // Crease rule along the 2 sharp edges
            const double AvgW = std::clamp(CreaseWeightSum * 0.5, 0.0, 1.0);
            const Vec3d CreasePos = Orig * 0.75 + CreaseNeighborSum * 0.125;
            Vec3d Blended = SmoothPos * (1.0 - AvgW) + CreasePos * AvgW;
            if (VertCreaseW > 1.0e-5f) Blended = Blended * (1.0 - double(VertCreaseW)) + Orig * double(VertCreaseW);
            Next.Positions[V] = Blended;
        }
        else
        {
            Next.Positions[V] = SmoothPos * (1.0 - double(VertCreaseW)) + Orig * double(VertCreaseW);
        }
    }

    // 4. Propagate child edge attributes (sharp, crease, bevel weight)
    Next.EdgeCreases.reserve(Edges.size() * 2u);
    Next.SharpEdges.reserve(Surf.SharpEdges.size() * 2u);
    for (const SubdivEdge& E : Edges)
    {
        if (E.Crease > 1.0e-5f)
        {
            Next.EdgeCreases[EdgeKeyOf(E.A, E.MidVert)] = E.Crease;
            Next.EdgeCreases[EdgeKeyOf(E.MidVert, E.B)] = E.Crease;
        }
        if (E.Sharp)
        {
            Next.SharpEdges.insert(EdgeKeyOf(E.A, E.MidVert));
            Next.SharpEdges.insert(EdgeKeyOf(E.MidVert, E.B));
        }
        if (E.BevelW > 1.0e-5f)
        {
            Next.BevelWeights[EdgeKeyOf(E.A, E.MidVert)] = E.BevelW;
            Next.BevelWeights[EdgeKeyOf(E.MidVert, E.B)] = E.BevelW;
        }
    }

    // 5. Emit subdivided quads (N quads per N-gon)
    size_t NewFaceEstimate = 0u;
    for (const PolyFace& F : Surf.Faces) NewFaceEstimate += F.Verts.size();
    Next.Faces.reserve(NewFaceEstimate);

    for (uint32_t F = 0u; F < FaceCount; ++F)
    {
        const PolyFace& Src = Surf.Faces[F];
        const size_t N = Src.Verts.size();
        const uint32_t FVert = static_cast<uint32_t>(VertCount + F);
        for (size_t I = 0u; I < N; ++I)
        {
            const uint32_t Prev = Src.Verts[(I + N - 1u) % N];
            const uint32_t Curr = Src.Verts[I];
            const uint32_t NextV = Src.Verts[(I + 1u) % N];
            const uint32_t EPrev = Edges[EdgeToIdx[EdgeKeyOf(Prev, Curr)]].MidVert;
            const uint32_t ENext = Edges[EdgeToIdx[EdgeKeyOf(Curr, NextV)]].MidVert;
            Next.Faces.push_back({ { Curr, ENext, FVert, EPrev }, Src.MaterialSlot, Src.SharpFace });
        }
    }

    Surf = std::move(Next);
}

void ApplySubsurfModifier(PolySurface& Surf, const BlendModifierDesc& Mod, int MaxLevel)
{
    const int Levels = std::clamp(Mod.SubdivLevels, 0, std::clamp(MaxLevel, 0, 4));
    if (Levels <= 0 || Surf.Faces.empty()) return;

    const bool SimpleMode      = (Mod.SubdivKind == 1);
    const bool UseCrease       = (Mod.SubsurfBits & SubsurfUseCreaseBit) != 0u;
    const bool PreserveCorners = (Mod.BoundarySmooth == 1);
    for (int Iter = 0; Iter < Levels; ++Iter)
    {
        // Safety budget against runaway degenerate meshes (cap at ~1.2M quads)
        if (Surf.Faces.size() > 350000u) break;
        SubdivideOnce(Surf, SimpleMode, UseCrease, PreserveCorners, Iter);
    }
}

void ApplySolidifyModifier(PolySurface& Surf, const BlendModifierDesc& Mod)
{
    if (Surf.Positions.empty() || Surf.Faces.empty() || std::abs(Mod.SolidifyOffset) < 1.0e-7f) return;

    const size_t OrigVerts = Surf.Positions.size();
    const size_t OrigFaces = Surf.Faces.size();

    // Compute smooth area-weighted vertex normals
    std::vector<Vec3d> Normals(OrigVerts, Vec3d{});
    for (const PolyFace& F : Surf.Faces)
    {
        if (F.Verts.size() < 3u) continue;
        Vec3d FaceN{};
        const Vec3d& P0 = Surf.Positions[F.Verts[0]];
        for (size_t I = 1u; I + 1u < F.Verts.size(); ++I)
        {
            FaceN = FaceN + Cross3(Surf.Positions[F.Verts[I]] - P0, Surf.Positions[F.Verts[I + 1u]] - P0);
        }
        for (uint32_t V : F.Verts) Normals[V] = Normals[V] + FaceN;
    }
    for (Vec3d& N : Normals) N = Normalize3(N);

    const double Thickness = double(Mod.SolidifyOffset);
    const double OffsetFac = double(Mod.SolidifyOffsetFac);
    const double ShiftOrig = Thickness * 0.5 * (OffsetFac + 1.0);
    const double ShiftNew  = Thickness * 0.5 * (OffsetFac - 1.0);
    const bool   FillRim   = (Mod.SolidifyBits & SolidifyRimBit) != 0u;
    const bool   OnlyRim   = (Mod.SolidifyBits & SolidifyOnlyRimBit) != 0u;
    const bool   FlipShell = (Mod.SolidifyBits & SolidifyFlipBit) != 0u;

    // Track directed boundary edges (edges used by only 1 face)
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> DirectedEdge;
    std::unordered_map<uint64_t, uint32_t> EdgeCount;
    if (FillRim)
    {
        for (const PolyFace& F : Surf.Faces)
        {
            const size_t N = F.Verts.size();
            for (size_t I = 0u; I < N; ++I)
            {
                const uint32_t A = F.Verts[I], B = F.Verts[(I + 1u) % N];
                const uint64_t Key = EdgeKeyOf(A, B);
                ++EdgeCount[Key];
                DirectedEdge[Key] = { A, B };
            }
        }
    }

    PolySurface Out;
    Out.GroupNames = Surf.GroupNames;
    Out.Positions.resize(OrigVerts * 2u);
    if (!Surf.VertCreases.empty()) Out.VertCreases.resize(OrigVerts * 2u, 0.0f);
    const bool HasWeights = (Surf.VertWeights.size() == OrigVerts && !Surf.GroupNames.empty());
    if (HasWeights) Out.VertWeights.resize(OrigVerts * 2u);

    for (size_t V = 0u; V < OrigVerts; ++V)
    {
        Out.Positions[V]             = Surf.Positions[V] + Normals[V] * ShiftOrig;
        Out.Positions[OrigVerts + V] = Surf.Positions[V] + Normals[V] * ShiftNew;
        if (V < Surf.VertCreases.size())
        {
            Out.VertCreases[V]             = Surf.VertCreases[V];
            Out.VertCreases[OrigVerts + V] = Surf.VertCreases[V];
        }
        if (HasWeights)
        {
            Out.VertWeights[V]             = Surf.VertWeights[V];
            Out.VertWeights[OrigVerts + V] = Surf.VertWeights[V];
        }
    }

    // Preserve edge creases & sharps on both shells
    for (const auto& Entry : Surf.EdgeCreases)
    {
        const uint32_t A = static_cast<uint32_t>(Entry.first >> 32u);
        const uint32_t B = static_cast<uint32_t>(Entry.first & 0xFFFFFFFFu);
        Out.EdgeCreases[EdgeKeyOf(A, B)] = Entry.second;
        Out.EdgeCreases[EdgeKeyOf(uint32_t(OrigVerts) + A, uint32_t(OrigVerts) + B)] = Entry.second;
    }
    for (uint64_t Key : Surf.SharpEdges)
    {
        const uint32_t A = static_cast<uint32_t>(Key >> 32u);
        const uint32_t B = static_cast<uint32_t>(Key & 0xFFFFFFFFu);
        Out.SharpEdges.insert(EdgeKeyOf(A, B));
        Out.SharpEdges.insert(EdgeKeyOf(uint32_t(OrigVerts) + A, uint32_t(OrigVerts) + B));
    }

    Out.Faces.reserve(OrigFaces * 2u + EdgeCount.size());
    if (!OnlyRim)
    {
        for (const PolyFace& Src : Surf.Faces)
        {
            PolyFace Outer = Src;
            if (FlipShell) std::reverse(Outer.Verts.begin(), Outer.Verts.end());
            Out.Faces.push_back(std::move(Outer));

            PolyFace Inner;
            Inner.MaterialSlot = Src.MaterialSlot;
            Inner.SharpFace    = Src.SharpFace;
            Inner.Verts.resize(Src.Verts.size());
            const size_t N = Src.Verts.size();
            for (size_t I = 0u; I < N; ++I)
            {
                const size_t DstIdx = FlipShell ? I : (N - 1u - I);
                Inner.Verts[DstIdx] = static_cast<uint32_t>(OrigVerts) + Src.Verts[I];
            }
            Out.Faces.push_back(std::move(Inner));
        }
    }

    if (FillRim)
    {
        for (const auto& Entry : EdgeCount)
        {
            if (Entry.second != 1u) continue;
            const auto [A, B] = DirectedEdge[Entry.first];
            const uint32_t Na = static_cast<uint32_t>(OrigVerts) + A;
            const uint32_t Nb = static_cast<uint32_t>(OrigVerts) + B;
            if (Thickness >= 0.0)
                Out.Faces.push_back({ { B, A, Na, Nb }, 0, false });
            else
                Out.Faces.push_back({ { A, B, Nb, Na }, 0, false });
            Out.SharpEdges.insert(EdgeKeyOf(A, B));
            Out.SharpEdges.insert(EdgeKeyOf(Na, Nb));
            if (Mod.SolidifyCreaseRim > 0.0f)
            {
                Out.EdgeCreases[EdgeKeyOf(A, B)]   = std::max(Out.EdgeCreases[EdgeKeyOf(A, B)], Mod.SolidifyCreaseRim);
                Out.EdgeCreases[EdgeKeyOf(Na, Nb)] = std::max(Out.EdgeCreases[EdgeKeyOf(Na, Nb)], Mod.SolidifyCreaseRim);
            }
        }
    }

    Surf = std::move(Out);
}

void ApplyShrinkwrapModifier(
    PolySurface&                                               Surf,
    const BlendModifierDesc&                                   Mod,
    const Mat4d&                                               ObjWorld,
    const std::unordered_map<uint64_t, SketcherReferenceSurface>& TargetSurfaces)
{
    if (Mod.ShrinkTargetAddr == 0u || Surf.Positions.empty()) return;
    const auto It = TargetSurfaces.find(Mod.ShrinkTargetAddr);
    if (It == TargetSurfaces.end() || !It->second.Geometry || It->second.Geometry->Bvh.Cells.empty()) return;

    const SketcherReferenceSurface& Target = It->second;
    const Mat4d InvWorld = InvertAffine(ObjWorld);
    const double KeepDist = double(Mod.ShrinkKeepDist);

    for (Vec3d& LocalP : Surf.Positions)
    {
        const Vec3d WorldP = TransformPoint(ObjWorld, LocalP);
        const auto Hit = ProjectOntoReferenceSurface(Target, 0u, { WorldP.X, WorldP.Y, WorldP.Z });
        if (!Hit) continue;
        const Vec3d SnappedWorld{
            Hit->Position.X + Hit->Normal.X * KeepDist,
            Hit->Position.Y + Hit->Normal.Y * KeepDist,
            Hit->Position.Z + Hit->Normal.Z * KeepDist,
        };
        LocalP = TransformPoint(InvWorld, SnappedWorld);
    }
}

void ApplyArrayModifier(PolySurface& Surf, const BlendModifierDesc& Mod)
{
    const int Count = std::clamp(Mod.ArrayCount, 1, 64);
    if (Count <= 1 || Surf.Positions.empty() || Surf.Faces.empty()) return;

    Vec3d MinP{ 1.0e30, 1.0e30, 1.0e30 };
    Vec3d MaxP{ -1.0e30, -1.0e30, -1.0e30 };
    for (const Vec3d& P : Surf.Positions)
    {
        MinP.X = std::min(MinP.X, P.X); MinP.Y = std::min(MinP.Y, P.Y); MinP.Z = std::min(MinP.Z, P.Z);
        MaxP.X = std::max(MaxP.X, P.X); MaxP.Y = std::max(MaxP.Y, P.Y); MaxP.Z = std::max(MaxP.Z, P.Z);
    }
    const Vec3d Extent = MaxP - MinP;
    Vec3d Step{};
    if (Mod.ArrayOffsetBits & ArrayOffsetConstBit)
    {
        Step.X += double(Mod.ArrayConst[0]);
        Step.Y += double(Mod.ArrayConst[1]);
        Step.Z += double(Mod.ArrayConst[2]);
    }
    if (Mod.ArrayOffsetBits & ArrayOffsetRelativeBit)
    {
        Step.X += double(Mod.ArrayRel[0]) * Extent.X;
        Step.Y += double(Mod.ArrayRel[1]) * Extent.Y;
        Step.Z += double(Mod.ArrayRel[2]) * Extent.Z;
    }

    const size_t SourceVerts = Surf.Positions.size();
    const size_t SourceFaces = Surf.Faces.size();
    Surf.Positions.reserve(SourceVerts * size_t(Count));
    Surf.Faces.reserve(SourceFaces * size_t(Count));
    if (!Surf.VertCreases.empty()) Surf.VertCreases.reserve(SourceVerts * size_t(Count));
    const bool HasWeights = (Surf.VertWeights.size() == SourceVerts && !Surf.GroupNames.empty());
    if (HasWeights) Surf.VertWeights.reserve(SourceVerts * size_t(Count));

    for (int Copy = 1; Copy < Count; ++Copy)
    {
        const Vec3d Shift = Step * double(Copy);
        const uint32_t VOff = static_cast<uint32_t>(Copy * SourceVerts);
        for (size_t V = 0u; V < SourceVerts; ++V)
        {
            Surf.Positions.push_back(Surf.Positions[V] + Shift);
            if (V < Surf.VertCreases.size()) Surf.VertCreases.push_back(Surf.VertCreases[V]);
            if (HasWeights) Surf.VertWeights.push_back(Surf.VertWeights[V]);
        }
        for (size_t F = 0u; F < SourceFaces; ++F)
        {
            PolyFace Dup = Surf.Faces[F];
            for (uint32_t& V : Dup.Verts) V += VOff;
            Surf.Faces.push_back(std::move(Dup));
        }
    }
}

void ApplyBevelModifier(PolySurface& Surf, const BlendModifierDesc& Mod)
{
    // Lightweight angle-aware crease/chamfer refinement (preserves crisp hard-surface edges before Subsurf)
    if (Mod.BevelWidth <= 1.0e-6f || Surf.Faces.empty()) return;

    const double CosLimit = std::cos(double(Mod.BevelAngleRad));
    std::vector<Vec3d> FaceNormals(Surf.Faces.size());
    for (size_t F = 0u; F < Surf.Faces.size(); ++F)
    {
        const auto& V = Surf.Faces[F].Verts;
        if (V.size() < 3u) continue;
        Vec3d N{};
        for (size_t I = 1u; I + 1u < V.size(); ++I)
            N = N + Cross3(Surf.Positions[V[I]] - Surf.Positions[V[0]], Surf.Positions[V[I + 1u]] - Surf.Positions[V[0]]);
        FaceNormals[F] = Normalize3(N);
    }

    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> EdgeFaces;
    for (uint32_t F = 0u; F < Surf.Faces.size(); ++F)
    {
        const auto& V = Surf.Faces[F].Verts;
        for (size_t I = 0u; I < V.size(); ++I)
        {
            const uint64_t Key = EdgeKeyOf(V[I], V[(I + 1u) % V.size()]);
            auto It = EdgeFaces.find(Key);
            if (It == EdgeFaces.end()) EdgeFaces[Key] = { F, UINT32_MAX };
            else if (It->second.second == UINT32_MAX) It->second.second = F;
        }
    }

    // Tag angle-exceeding edges as semi-creased + sharp so shading & subsequent subdivision produce crisp bevel highlights
    for (const auto& Entry : EdgeFaces)
    {
        if (Entry.second.second == UINT32_MAX) continue;
        const double DotN = Dot3(FaceNormals[Entry.second.first], FaceNormals[Entry.second.second]);
        if (DotN < CosLimit)
        {
            Surf.SharpEdges.insert(Entry.first);
            const float Existing = Surf.EdgeCreases.count(Entry.first) ? Surf.EdgeCreases[Entry.first] : 0.0f;
            Surf.EdgeCreases[Entry.first] = std::max(Existing, 0.45f);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    EXTRACTION FROM .BLEND
//------------------------------------------------------------------------------------------------------------------------

bool ExtractRawPolySurface(const BlendDocument& Doc, uint64_t SurfaceBlockAddr, PolySurface& Out, std::array<float, 3>& OutColour)
{
    const BlendBlock* MBlock = Doc.FindBlock(SurfaceBlockAddr);
    const BlendStruct* SurfaceStruct = Doc.FindStruct("Mesh");
    if (!MBlock || !SurfaceStruct) return false;

    const uint8_t* MBase = Doc.BlockPtr(*MBlock);
    const size_t   MLim  = MBlock->ByteSize;

    const int32_t TotVert = static_cast<int32_t>(ReadSignedField(SurfaceStruct, MBase, MLim, "totvert", 0));
    const int32_t TotEdge = static_cast<int32_t>(ReadSignedField(SurfaceStruct, MBase, MLim, "totedge", 0));
    const int32_t TotPoly = static_cast<int32_t>(ReadSignedField(SurfaceStruct, MBase, MLim, "totpoly", 0));
    const int32_t TotLoop = static_cast<int32_t>(ReadSignedField(SurfaceStruct, MBase, MLim, "totloop", 0));
    if (TotVert <= 0) return false;

    // Material diffuse colour (first slot)
    const uint64_t MatArrAddr = ReadPointerField(Doc, SurfaceStruct, MBase, MLim, "mat");
    if (const BlendBlock* MatArrBlk = Doc.FindBlock(MatArrAddr, SurfaceBlockAddr))
    {
        if (MatArrBlk->ByteSize >= Doc.PointerSize)
        {
            const uint8_t* PtrBytes = Doc.BlockPtr(*MatArrBlk);
            const uint64_t MatAddr = (Doc.PointerSize == 8u) ? ReadU64(PtrBytes) : ReadU32(PtrBytes);
            const BlendBlock* MatBlk = Doc.FindBlock(MatAddr);
            const BlendStruct* MatSt = Doc.FindStruct("Material");
            if (MatBlk && MatSt)
            {
                const float R = ReadFloatField(MatSt, Doc.BlockPtr(*MatBlk), MatBlk->ByteSize, "r", 0.68f);
                const float G = ReadFloatField(MatSt, Doc.BlockPtr(*MatBlk), MatBlk->ByteSize, "g", 0.72f);
                const float B = ReadFloatField(MatSt, Doc.BlockPtr(*MatBlk), MatBlk->ByteSize, "b", 0.76f);
                if (R + G + B > 0.05f)
                {
                    OutColour = { std::clamp(R, 0.08f, 0.95f), std::clamp(G, 0.08f, 0.95f), std::clamp(B, 0.08f, 0.95f) };
                }
            }
        }
    }

    // Vertex group names (Mesh.vertex_group_names ListBase of bDeformGroup)
    if (const BlendStruct* DgSt = Doc.FindStruct("bDeformGroup"))
    {
        uint32_t LbSz = 0u;
        const uint8_t* LbPtr = FieldSlice(SurfaceStruct, MBase, MLim, "vertex_group_names", LbSz);
        if (LbPtr && LbSz >= Doc.PointerSize)
        {
            uint64_t Cur = (Doc.PointerSize == 8u) ? ReadU64(LbPtr) : ReadU32(LbPtr);
            std::unordered_set<uint64_t> Seen;
            while (Cur != 0u && Seen.insert(Cur).second)
            {
                const BlendBlock* DgBlk = Doc.FindBlock(Cur, SurfaceBlockAddr);
                if (!DgBlk) break;
                Out.GroupNames.push_back(ReadCStringField(DgSt, Doc.BlockPtr(*DgBlk), DgBlk->ByteSize, "name"));
                Cur = ReadPointerField(Doc, DgSt, Doc.BlockPtr(*DgBlk), DgBlk->ByteSize, "next");
            }
        }
    }

    // Collect named attribute payloads from both Blender 5.0+ AttributeStorage and Blender 3.x/4.x CustomData layers
    struct AttrSlice
    {
        const uint8_t* Ptr      = nullptr;
        size_t         ByteSize = 0u;
    };
    std::unordered_map<std::string, AttrSlice> Attrs;

    // A) Blender 5.0+ AttributeStorage
    uint32_t AsSz = 0u;
    const uint8_t* AsPtr = FieldSlice(SurfaceStruct, MBase, MLim, "attribute_storage", AsSz);
    const BlendStruct* AsSt = Doc.FindStruct("AttributeStorage");
    const BlendStruct* AdSt = Doc.FindStruct("Attribute");
    const BlendStruct* AaSt = Doc.FindStruct("AttributeArray");
    if (AsPtr && AsSt && AdSt && AaSt)
    {
        const int64_t NumAttrs = ReadSignedField(AsSt, AsPtr, AsSz, "dna_attributes_num", 0);
        const uint64_t ArrAddr = ReadPointerField(Doc, AsSt, AsPtr, AsSz, "dna_attributes");
        if (NumAttrs > 0 && ArrAddr != 0u)
        {
            if (const BlendBlock* AttrBlk = Doc.FindBlock(ArrAddr, SurfaceBlockAddr))
            {
                const uint8_t* ABase = Doc.BlockPtr(*AttrBlk);
                const size_t   AStride = AdSt->ByteSize;
                const size_t   LimitCount = (AStride > 0u) ? std::min<size_t>(size_t(NumAttrs), AttrBlk->ByteSize / AStride) : 0u;
                for (size_t I = 0u; I < LimitCount; ++I)
                {
                    const uint8_t* AttrEntry = ABase + I * AStride;
                    const uint64_t NameAddr = ReadPointerField(Doc, AdSt, AttrEntry, AStride, "name");
                    const uint64_t PayloadAddr = ReadPointerField(Doc, AdSt, AttrEntry, AStride, "data");
                    const int64_t  StorKind = ReadSignedField(AdSt, AttrEntry, AStride, "storage_type", 0);
                    if (StorKind != 0) continue;                                             // Array storage only
                    const BlendBlock* NameBlk = Doc.FindBlock(NameAddr, SurfaceBlockAddr);
                    const BlendBlock* WrapBlk = Doc.FindBlock(PayloadAddr, SurfaceBlockAddr);
                    if (!NameBlk || !WrapBlk) continue;

                    const char* NameStr = reinterpret_cast<const char*>(Doc.BlockPtr(*NameBlk));
                    size_t NLen = 0u;
                    while (NLen < NameBlk->ByteSize && NameStr[NLen] != '\0') ++NLen;
                    std::string AttrName(NameStr, NLen);

                    const uint64_t RawAddr = ReadPointerField(Doc, AaSt, Doc.BlockPtr(*WrapBlk), WrapBlk->ByteSize, "data");
                    if (const BlendBlock* RawBlk = Doc.FindBlock(RawAddr, SurfaceBlockAddr))
                    {
                        Attrs[AttrName] = { Doc.BlockPtr(*RawBlk), RawBlk->ByteSize };
                    }
                }
            }
        }
    }

    // B) Legacy/hybrid CustomData layers (vdata, edata, pdata, ldata) — also holds CD_MDEFORMVERT (2) & CD_MVERT_SKIN (36) in Blender 5.2!
    uint64_t DeformVertAddr = ReadPointerField(Doc, SurfaceStruct, MBase, MLim, "dvert");
    uint64_t VertSkinAddr   = 0u;
    const BlendStruct* CdSt  = Doc.FindStruct("CustomData");
    const BlendStruct* CdlSt = Doc.FindStruct("CustomDataLayer");
    if (CdSt && CdlSt && CdlSt->ByteSize > 0u)
    {
        for (const char* DomainField : { "vdata", "edata", "pdata", "ldata" })
        {
            uint32_t CdSz = 0u;
            const uint8_t* CdPtr = FieldSlice(SurfaceStruct, MBase, MLim, DomainField, CdSz);
            if (!CdPtr) continue;
            const int64_t TotLayer = ReadSignedField(CdSt, CdPtr, CdSz, "totlayer", 0);
            const uint64_t LAddr   = ReadPointerField(Doc, CdSt, CdPtr, CdSz, "layers");
            const BlendBlock* LBlk = Doc.FindBlock(LAddr, SurfaceBlockAddr);
            if (TotLayer <= 0 || !LBlk) continue;
            const uint8_t* LBase   = Doc.BlockPtr(*LBlk);
            const size_t   LCount  = std::min<size_t>(size_t(TotLayer), LBlk->ByteSize / CdlSt->ByteSize);
            for (size_t I = 0u; I < LCount; ++I)
            {
                const uint8_t* Layer = LBase + I * CdlSt->ByteSize;
                const int32_t  LType = static_cast<int32_t>(ReadSignedField(CdlSt, Layer, CdlSt->ByteSize, "type", -1));
                const uint64_t LData = ReadPointerField(Doc, CdlSt, Layer, CdlSt->ByteSize, "data");
                const std::string LName = ReadCStringField(CdlSt, Layer, CdlSt->ByteSize, "name");
                if (LType == CustomLayerDeformVert && DeformVertAddr == 0u) DeformVertAddr = LData;
                if (LType == CustomLayerVertSkin && VertSkinAddr == 0u)     VertSkinAddr   = LData;
                if (!LName.empty() && Attrs.find(LName) == Attrs.end())
                {
                    if (const BlendBlock* DBlk = Doc.FindBlock(LData, SurfaceBlockAddr))
                    {
                        Attrs[LName] = { Doc.BlockPtr(*DBlk), DBlk->ByteSize };
                    }
                }
            }
        }
    }

    // 1. Vertex positions
    Out.Positions.resize(size_t(TotVert));
    if (Attrs.count("position") && Attrs["position"].ByteSize >= size_t(TotVert) * 12u)
    {
        const uint8_t* P = Attrs["position"].Ptr;
        for (int32_t V = 0; V < TotVert; ++V)
        {
            Out.Positions[size_t(V)] = { ReadF32(P + size_t(V) * 12u + 0u), ReadF32(P + size_t(V) * 12u + 4u), ReadF32(P + size_t(V) * 12u + 8u) };
        }
    }
    else
    {
        // Legacy MVert fallback
        const uint64_t MVertAddr = ReadPointerField(Doc, SurfaceStruct, MBase, MLim, "mvert");
        const BlendBlock* MvBlk  = Doc.FindBlock(MVertAddr, SurfaceBlockAddr);
        const BlendStruct* MvSt  = Doc.FindStruct("MVert");
        if (!MvBlk || !MvSt || MvSt->ByteSize == 0u) return false;
        const size_t Count = std::min<size_t>(size_t(TotVert), MvBlk->ByteSize / MvSt->ByteSize);
        for (size_t V = 0u; V < Count; ++V)
        {
            uint32_t CoSz = 0u;
            const uint8_t* Co = FieldSlice(MvSt, Doc.BlockPtr(*MvBlk) + V * MvSt->ByteSize, MvSt->ByteSize, "co", CoSz);
            if (Co && CoSz >= 12u) Out.Positions[V] = { ReadF32(Co), ReadF32(Co + 4u), ReadF32(Co + 8u) };
        }
    }

    // Vertex creases
    if (Attrs.count("crease_vert") && Attrs["crease_vert"].ByteSize >= size_t(TotVert) * 4u)
    {
        Out.VertCreases.resize(size_t(TotVert), 0.0f);
        const uint8_t* P = Attrs["crease_vert"].Ptr;
        for (int32_t V = 0; V < TotVert; ++V) Out.VertCreases[size_t(V)] = std::clamp(ReadF32(P + size_t(V) * 4u), 0.0f, 1.0f);
    }

    // Vertex group weights (for Mask modifier)
    if (DeformVertAddr != 0u && !Out.GroupNames.empty())
    {
        const BlendBlock* DvBlk = Doc.FindBlock(DeformVertAddr, SurfaceBlockAddr);
        const BlendStruct* DvSt = Doc.FindStruct("MDeformVert");
        const BlendStruct* DwSt = Doc.FindStruct("MDeformWeight");
        if (DvBlk && DvSt && DwSt && DvSt->ByteSize > 0u && DwSt->ByteSize > 0u)
        {
            Out.VertWeights.resize(size_t(TotVert));
            const size_t Count = std::min<size_t>(size_t(TotVert), DvBlk->ByteSize / DvSt->ByteSize);
            for (size_t V = 0u; V < Count; ++V)
            {
                const uint8_t* DvPtr = Doc.BlockPtr(*DvBlk) + V * DvSt->ByteSize;
                const int32_t TotW = static_cast<int32_t>(ReadSignedField(DvSt, DvPtr, DvSt->ByteSize, "totweight", 0));
                const uint64_t DwAddr = ReadPointerField(Doc, DvSt, DvPtr, DvSt->ByteSize, "dw");
                const BlendBlock* DwBlk = Doc.FindBlock(DwAddr, SurfaceBlockAddr);
                if (TotW <= 0 || !DwBlk) continue;
                const size_t WCount = std::min<size_t>(size_t(TotW), DwBlk->ByteSize / DwSt->ByteSize);
                Out.VertWeights[V].reserve(WCount);
                for (size_t W = 0u; W < WCount; ++W)
                {
                    const uint8_t* WPtr = Doc.BlockPtr(*DwBlk) + W * DwSt->ByteSize;
                    const int32_t DefNr = static_cast<int32_t>(ReadSignedField(DwSt, WPtr, DwSt->ByteSize, "def_nr", -1));
                    const float   Wt    = ReadFloatField(DwSt, WPtr, DwSt->ByteSize, "weight", 0.0f);
                    if (DefNr >= 0) Out.VertWeights[V].emplace_back(DefNr, Wt);
                }
            }
        }
    }

    // Skin radii (for Skin modifier)
    if (VertSkinAddr != 0u)
    {
        const BlendBlock* SkBlk = Doc.FindBlock(VertSkinAddr, SurfaceBlockAddr);
        const BlendStruct* SkSt = Doc.FindStruct("MVertSkin");
        if (SkBlk && SkSt && SkSt->ByteSize > 0u)
        {
            Out.SkinRadii.resize(size_t(TotVert), { 0.25f, 0.25f, 0.25f });
            const size_t Count = std::min<size_t>(size_t(TotVert), SkBlk->ByteSize / SkSt->ByteSize);
            for (size_t V = 0u; V < Count; ++V)
            {
                uint32_t RSz = 0u;
                const uint8_t* RPtr = FieldSlice(SkSt, Doc.BlockPtr(*SkBlk) + V * SkSt->ByteSize, SkSt->ByteSize, "radius", RSz);
                if (RPtr && RSz >= 8u)
                    Out.SkinRadii[V] = { ReadF32(RPtr), ReadF32(RPtr + 4u), (RSz >= 12u) ? ReadF32(RPtr + 8u) : ReadF32(RPtr) };
            }
        }
    }

    // 2. Edges + edge creases + sharp edges + bevel weights
    std::vector<std::pair<uint32_t, uint32_t>> EdgeVerts;
    if (TotEdge > 0 && Attrs.count(".edge_verts") && Attrs[".edge_verts"].ByteSize >= size_t(TotEdge) * 8u)
    {
        EdgeVerts.resize(size_t(TotEdge));
        const uint8_t* P = Attrs[".edge_verts"].Ptr;
        for (int32_t E = 0; E < TotEdge; ++E)
        {
            EdgeVerts[size_t(E)] = { ReadU32(P + size_t(E) * 8u), ReadU32(P + size_t(E) * 8u + 4u) };
        }
    }
    else if (TotEdge > 0)
    {
        const uint64_t MEdgeAddr = ReadPointerField(Doc, SurfaceStruct, MBase, MLim, "medge");
        const BlendBlock* MeBlk  = Doc.FindBlock(MEdgeAddr, SurfaceBlockAddr);
        const BlendStruct* MeSt  = Doc.FindStruct("MEdge");
        if (MeBlk && MeSt && MeSt->ByteSize > 0u)
        {
            const size_t Count = std::min<size_t>(size_t(TotEdge), MeBlk->ByteSize / MeSt->ByteSize);
            EdgeVerts.resize(Count);
            for (size_t E = 0u; E < Count; ++E)
            {
                const uint8_t* Ep = Doc.BlockPtr(*MeBlk) + E * MeSt->ByteSize;
                const uint32_t V1 = static_cast<uint32_t>(ReadUnsignedField(MeSt, Ep, MeSt->ByteSize, "v1", 0u));
                const uint32_t V2 = static_cast<uint32_t>(ReadUnsignedField(MeSt, Ep, MeSt->ByteSize, "v2", 0u));
                EdgeVerts[E] = { V1, V2 };
                const uint64_t CreaseByte = ReadUnsignedField(MeSt, Ep, MeSt->ByteSize, "crease", 0u);
                if (CreaseByte > 0u) Out.EdgeCreases[EdgeKeyOf(V1, V2)] = float(CreaseByte) / 255.0f;
                const uint64_t EdgeBits = ReadUnsignedField(MeSt, Ep, MeSt->ByteSize, "flag", 0u);
                if (EdgeBits & 0x200u) Out.SharpEdges.insert(EdgeKeyOf(V1, V2));
            }
        }
    }

    if (!EdgeVerts.empty())
    {
        Out.WireEdges = EdgeVerts;
        if (Attrs.count("crease_edge") && Attrs["crease_edge"].ByteSize >= EdgeVerts.size() * 4u)
        {
            const uint8_t* P = Attrs["crease_edge"].Ptr;
            for (size_t E = 0u; E < EdgeVerts.size(); ++E)
            {
                const float Cr = std::clamp(ReadF32(P + E * 4u), 0.0f, 1.0f);
                if (Cr > 1.0e-5f) Out.EdgeCreases[EdgeKeyOf(EdgeVerts[E].first, EdgeVerts[E].second)] = Cr;
            }
        }
        if (Attrs.count("sharp_edge") && Attrs["sharp_edge"].ByteSize >= EdgeVerts.size())
        {
            const uint8_t* P = Attrs["sharp_edge"].Ptr;
            for (size_t E = 0u; E < EdgeVerts.size(); ++E)
            {
                if (P[E] != 0u) Out.SharpEdges.insert(EdgeKeyOf(EdgeVerts[E].first, EdgeVerts[E].second));
            }
        }
        if (Attrs.count("bevel_weight_edge") && Attrs["bevel_weight_edge"].ByteSize >= EdgeVerts.size() * 4u)
        {
            const uint8_t* P = Attrs["bevel_weight_edge"].Ptr;
            for (size_t E = 0u; E < EdgeVerts.size(); ++E)
            {
                const float Bw = std::clamp(ReadF32(P + E * 4u), 0.0f, 1.0f);
                if (Bw > 1.0e-5f) Out.BevelWeights[EdgeKeyOf(EdgeVerts[E].first, EdgeVerts[E].second)] = Bw;
            }
        }
    }

    // 3. Polygons (Blender 4.0+/5.x poly_offset_indices + .corner_vert, or legacy MPoly + MLoop)
    if (TotPoly > 0)
    {
        const uint64_t OffsetAddr = ReadPointerField(Doc, SurfaceStruct, MBase, MLim, "poly_offset_indices");
        const BlendBlock* OffBlk  = Doc.FindBlock(OffsetAddr, SurfaceBlockAddr);
        const AttrSlice* CornerVerts = Attrs.count(".corner_vert") ? &Attrs[".corner_vert"] : nullptr;
        const AttrSlice* SharpFaceA  = Attrs.count("sharp_face") ? &Attrs["sharp_face"] : nullptr;
        const AttrSlice* MatIdxA     = Attrs.count("material_index") ? &Attrs["material_index"] : nullptr;

        if (OffBlk && CornerVerts && OffBlk->ByteSize >= size_t(TotPoly + 1) * 4u && CornerVerts->ByteSize >= size_t(TotLoop) * 4u)
        {
            const uint8_t* OffP = Doc.BlockPtr(*OffBlk);
            const uint8_t* CvP  = CornerVerts->Ptr;
            Out.Faces.reserve(size_t(TotPoly));
            for (int32_t F = 0; F < TotPoly; ++F)
            {
                const int32_t Start = ReadI32(OffP + size_t(F) * 4u);
                const int32_t End   = ReadI32(OffP + size_t(F + 1) * 4u);
                if (Start < 0 || End <= Start || End > TotLoop || (End - Start) < 3) continue;
                PolyFace Face;
                Face.Verts.reserve(size_t(End - Start));
                bool Valid = true;
                for (int32_t L = Start; L < End; ++L)
                {
                    const uint32_t Vi = ReadU32(CvP + size_t(L) * 4u);
                    if (Vi >= uint32_t(TotVert)) { Valid = false; break; }
                    Face.Verts.push_back(Vi);
                }
                if (!Valid) continue;
                if (SharpFaceA && size_t(F) < SharpFaceA->ByteSize) Face.SharpFace = (SharpFaceA->Ptr[F] != 0u);
                if (MatIdxA && size_t(F + 1) * 4u <= MatIdxA->ByteSize) Face.MaterialSlot = ReadI32(MatIdxA->Ptr + size_t(F) * 4u);
                Out.Faces.push_back(std::move(Face));
            }
        }
        else
        {
            // Legacy MPoly + MLoop fallback
            const uint64_t MPolyAddr = ReadPointerField(Doc, SurfaceStruct, MBase, MLim, "mpoly");
            const uint64_t MLoopAddr = ReadPointerField(Doc, SurfaceStruct, MBase, MLim, "mloop");
            const BlendBlock* MpBlk  = Doc.FindBlock(MPolyAddr, SurfaceBlockAddr);
            const BlendBlock* MlBlk  = Doc.FindBlock(MLoopAddr, SurfaceBlockAddr);
            const BlendStruct* MpSt  = Doc.FindStruct("MPoly");
            const BlendStruct* MlSt  = Doc.FindStruct("MLoop");
            if (MpBlk && MlBlk && MpSt && MlSt && MpSt->ByteSize > 0u && MlSt->ByteSize > 0u)
            {
                const size_t FCount = std::min<size_t>(size_t(TotPoly), MpBlk->ByteSize / MpSt->ByteSize);
                const size_t LCount = MlBlk->ByteSize / MlSt->ByteSize;
                Out.Faces.reserve(FCount);
                for (size_t F = 0u; F < FCount; ++F)
                {
                    const uint8_t* Fp = Doc.BlockPtr(*MpBlk) + F * MpSt->ByteSize;
                    const int32_t Start = static_cast<int32_t>(ReadSignedField(MpSt, Fp, MpSt->ByteSize, "loopstart", 0));
                    const int32_t Count = static_cast<int32_t>(ReadSignedField(MpSt, Fp, MpSt->ByteSize, "totloop", 0));
                    if (Start < 0 || Count < 3 || size_t(Start + Count) > LCount) continue;
                    PolyFace Face;
                    Face.Verts.reserve(size_t(Count));
                    Face.MaterialSlot = static_cast<int>(ReadSignedField(MpSt, Fp, MpSt->ByteSize, "mat_nr", 0));
                    Face.SharpFace    = (ReadUnsignedField(MpSt, Fp, MpSt->ByteSize, "flag", 1u) & 1u) == 0u;
                    bool Valid = true;
                    for (int32_t K = 0; K < Count; ++K)
                    {
                        const uint8_t* Lp = Doc.BlockPtr(*MlBlk) + size_t(Start + K) * MlSt->ByteSize;
                        const uint32_t Vi = static_cast<uint32_t>(ReadUnsignedField(MlSt, Lp, MlSt->ByteSize, "v", 0u));
                        if (Vi >= uint32_t(TotVert)) { Valid = false; break; }
                        Face.Verts.push_back(Vi);
                    }
                    if (Valid) Out.Faces.push_back(std::move(Face));
                }
            }
        }
    }

    return !Out.Faces.empty() || !Out.WireEdges.empty();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRIANGULATION & NORMALS
//------------------------------------------------------------------------------------------------------------------------

void EmitWorldSurfaceGeometry(
    const PolySurface&                     Surf,
    const Mat4d&                           WorldMatrix,
    uint32_t                               PartIndex,
    double                                 AutoSmoothRadians,
    std::vector<SketcherReferenceCorner>&  OutCorners,
    std::vector<SketcherReferenceSegment>& OutSegments,
    std::array<double, 3>&                 OutMin,
    std::array<double, 3>&                 OutMax)
{
    if (Surf.Positions.empty() || Surf.Faces.empty()) return;

    const bool FlipWinding = (Determinant3x3(WorldMatrix) < 0.0);
    const size_t VertCount = Surf.Positions.size();
    const size_t FaceCount = Surf.Faces.size();

    // Transform all vertices to world space once
    std::vector<Vec3d> WorldVerts(VertCount);
    OutMin = { 1.0e30, 1.0e30, 1.0e30 };
    OutMax = { -1.0e30, -1.0e30, -1.0e30 };
    for (size_t V = 0u; V < VertCount; ++V)
    {
        WorldVerts[V] = TransformPoint(WorldMatrix, Surf.Positions[V]);
    }

    // Compute per-face unit normals & areas
    std::vector<Vec3d> FaceNormals(FaceCount);
    std::vector<double> FaceAreas(FaceCount, 0.0);
    std::vector<std::vector<uint32_t>> VertFaces(VertCount);

    for (uint32_t F = 0u; F < FaceCount; ++F)
    {
        const auto& V = Surf.Faces[F].Verts;
        if (V.size() < 3u) continue;
        Vec3d AreaVec{};
        const Vec3d& P0 = WorldVerts[V[0]];
        for (size_t I = 1u; I + 1u < V.size(); ++I)
        {
            const Vec3d CrossV = Cross3(WorldVerts[V[I]] - P0, WorldVerts[V[I + 1u]] - P0);
            AreaVec = AreaVec + CrossV;
        }
        if (FlipWinding) AreaVec = AreaVec * -1.0;
        const double Len = std::sqrt(Dot3(AreaVec, AreaVec));
        FaceAreas[F]   = 0.5 * Len;
        FaceNormals[F] = Len > 1.0e-18 ? AreaVec * (1.0 / Len) : Vec3d{ 0.0, 0.0, 1.0 };
        for (uint32_t Vi : V) VertFaces[Vi].push_back(F);
    }

    // Track adjacent faces per edge to detect dihedral creases & boundaries
    std::unordered_map<uint64_t, std::pair<uint32_t, uint32_t>> EdgeAdj;
    EdgeAdj.reserve(FaceCount * 2u + 16u);
    for (uint32_t F = 0u; F < FaceCount; ++F)
    {
        const auto& V = Surf.Faces[F].Verts;
        for (size_t I = 0u; I < V.size(); ++I)
        {
            const uint64_t Key = EdgeKeyOf(V[I], V[(I + 1u) % V.size()]);
            auto It = EdgeAdj.find(Key);
            if (It == EdgeAdj.end()) EdgeAdj[Key] = { F, UINT32_MAX };
            else if (It->second.second == UINT32_MAX) It->second.second = F;
        }
    }

    const double CosSmooth  = std::cos(AutoSmoothRadians);
    const double CosFeature = std::cos(34.0 * DegToRad);

    // Emit crisp GPU feature/crease segments (sharp edges, strong creases, panel boundaries, sharp dihedrals)
    std::unordered_set<uint32_t> CreaseVerts;
    for (const auto& Entry : EdgeAdj)
    {
        const uint32_t A = static_cast<uint32_t>(Entry.first >> 32u);
        const uint32_t B = static_cast<uint32_t>(Entry.first & 0xFFFFFFFFu);
        const uint32_t Fa = Entry.second.first;
        const uint32_t Fb = Entry.second.second;

        const bool IsBoundary = (Fb == UINT32_MAX);
        const bool IsSharp    = Surf.SharpEdges.find(Entry.first) != Surf.SharpEdges.end();
        const float CreaseVal = Surf.QueryCrease(A, B);
        const bool IsDihedral = (!IsBoundary && Dot3(FaceNormals[Fa], FaceNormals[Fb]) < CosFeature);

        if (IsBoundary || IsSharp || CreaseVal >= 0.35f || IsDihedral)
        {
            const Vec3d& Pa = WorldVerts[A];
            const Vec3d& Pb = WorldVerts[B];
            SketcherReferenceSegment Seg;
            Seg.Start[0]  = float(Pa.X); Seg.Start[1] = float(Pa.Y); Seg.Start[2] = float(Pa.Z);
            Seg.End[0]    = float(Pb.X); Seg.End[1]   = float(Pb.Y); Seg.End[2]   = float(Pb.Z);
            Seg.PartIndex = PartIndex;
            OutSegments.push_back(Seg);
            CreaseVerts.insert(A);
            CreaseVerts.insert(B);
        }
    }

    // Compute per-corner smooth/split normal respecting sharp_face, sharp_edge, and AutoSmooth angle
    auto ComputeCornerNormal = [&](uint32_t FaceIdx, uint32_t VertIdx) -> Vec3d
    {
        if (Surf.Faces[FaceIdx].SharpFace) return FaceNormals[FaceIdx];
        const Vec3d& RefN = FaceNormals[FaceIdx];
        Vec3d Accum = RefN * std::max(1.0e-8, FaceAreas[FaceIdx]);
        for (uint32_t OtherF : VertFaces[VertIdx])
        {
            if (OtherF == FaceIdx || Surf.Faces[OtherF].SharpFace) continue;
            const double CosAng = Dot3(RefN, FaceNormals[OtherF]);
            if (CosAng < CosSmooth) continue;

            // Check if the shared edge (if any) is explicitly sharp
            bool BlockedBySharp = false;
            const auto& Ov = Surf.Faces[OtherF].Verts;
            const auto& Fv = Surf.Faces[FaceIdx].Verts;
            for (size_t I = 0u; I < Fv.size(); ++I)
            {
                if (Fv[I] != VertIdx) continue;
                const uint32_t Prev = Fv[(I + Fv.size() - 1u) % Fv.size()];
                const uint32_t Next = Fv[(I + 1u) % Fv.size()];
                for (uint32_t Cand : { Prev, Next })
                {
                    if (std::find(Ov.begin(), Ov.end(), Cand) != Ov.end() && Surf.QuerySharp(VertIdx, Cand))
                    {
                        BlockedBySharp = true;
                        break;
                    }
                }
            }
            if (BlockedBySharp) continue;
            Accum = Accum + FaceNormals[OtherF] * std::max(1.0e-8, FaceAreas[OtherF]);
        }
        return Normalize3(Accum);
    };

    auto EmitCorner = [&](uint32_t FaceIdx, uint32_t VertIdx)
    {
        const Vec3d& P = WorldVerts[VertIdx];
        const Vec3d  N = ComputeCornerNormal(FaceIdx, VertIdx);
        OutMin[0] = std::min(OutMin[0], P.X); OutMin[1] = std::min(OutMin[1], P.Y); OutMin[2] = std::min(OutMin[2], P.Z);
        OutMax[0] = std::max(OutMax[0], P.X); OutMax[1] = std::max(OutMax[1], P.Y); OutMax[2] = std::max(OutMax[2], P.Z);

        SketcherReferenceCorner C;
        C.Position[0] = float(P.X); C.Position[1] = float(P.Y); C.Position[2] = float(P.Z);
        C.Normal[0]   = float(N.X); C.Normal[1]   = float(N.Y); C.Normal[2]   = float(N.Z);
        C.PartIndex   = PartIndex;
        C.CreaseBits  = CreaseVerts.count(VertIdx) ? 1u : 0u;
        OutCorners.push_back(C);
    };

    auto EmitTri = [&](uint32_t FaceIdx, uint32_t I0, uint32_t I1, uint32_t I2)
    {
        if (FlipWinding) std::swap(I1, I2);
        EmitCorner(FaceIdx, I0);
        EmitCorner(FaceIdx, I1);
        EmitCorner(FaceIdx, I2);
    };

    for (uint32_t F = 0u; F < FaceCount; ++F)
    {
        const auto& V = Surf.Faces[F].Verts;
        if (V.size() == 3u)
        {
            EmitTri(F, V[0], V[1], V[2]);
        }
        else if (V.size() == 4u)
        {
            // Split quad along shorter diagonal for smoother automotive highlight flow
            const Vec3d D02 = WorldVerts[V[2]] - WorldVerts[V[0]];
            const Vec3d D13 = WorldVerts[V[3]] - WorldVerts[V[1]];
            if (Dot3(D02, D02) <= Dot3(D13, D13))
            {
                EmitTri(F, V[0], V[1], V[2]);
                EmitTri(F, V[0], V[2], V[3]);
            }
            else
            {
                EmitTri(F, V[0], V[1], V[3]);
                EmitTri(F, V[1], V[2], V[3]);
            }
        }
        else if (V.size() > 4u)
        {
            for (size_t I = 1u; I + 1u < V.size(); ++I)
            {
                EmitTri(F, V[0], V[I], V[I + 1u]);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    SCENE & MODIFIER EXTRACTION
//------------------------------------------------------------------------------------------------------------------------

void CollectCollectionMembership(
    const BlendDocument&                        Doc,
    uint64_t                                    ColAddr,
    const std::string&                          ParentName,
    bool                                        InheritedHidden,
    std::unordered_map<uint64_t, std::string>&  ObjCollection,
    std::unordered_set<uint64_t>&               HiddenObjects,
    std::unordered_set<uint64_t>&               Visited)
{
    if (ColAddr == 0u || !Visited.insert(ColAddr).second) return;
    const BlendBlock* ColBlk = Doc.FindBlock(ColAddr);
    const BlendStruct* ColSt = Doc.FindStruct("Collection");
    const BlendStruct* CoSt  = Doc.FindStruct("CollectionObject");
    const BlendStruct* CcSt  = Doc.FindStruct("CollectionChild");
    if (!ColBlk || !ColSt) return;

    const uint8_t* CBase = Doc.BlockPtr(*ColBlk);
    const size_t   CLim  = ColBlk->ByteSize;
    std::string ColName  = ReadIdName(Doc, ColSt, CBase, CLim);
    if (ColName.empty()) ColName = ParentName;
    const uint64_t ColBits = ReadUnsignedField(ColSt, CBase, CLim, "flag", 0u);
    const bool ThisHidden  = InheritedHidden || ((ColBits & 1u) != 0u);

    // Objects directly in this collection
    uint32_t LbSz = 0u;
    if (const uint8_t* ObLb = FieldSlice(ColSt, CBase, CLim, "gobject", LbSz); ObLb && CoSt)
    {
        uint64_t Cur = (Doc.PointerSize == 8u) ? ReadU64(ObLb) : ReadU32(ObLb);
        std::unordered_set<uint64_t> Seen;
        while (Cur != 0u && Seen.insert(Cur).second)
        {
            const BlendBlock* Blk = Doc.FindBlock(Cur, ColAddr);
            if (!Blk) break;
            const uint64_t ObPtr = ReadPointerField(Doc, CoSt, Doc.BlockPtr(*Blk), Blk->ByteSize, "ob");
            if (ObPtr != 0u)
            {
                if (ObjCollection.find(ObPtr) == ObjCollection.end()) ObjCollection[ObPtr] = ColName;
                if (ThisHidden) HiddenObjects.insert(ObPtr);
            }
            Cur = ReadPointerField(Doc, CoSt, Doc.BlockPtr(*Blk), Blk->ByteSize, "next");
        }
    }

    // Child collections
    if (const uint8_t* ChLb = FieldSlice(ColSt, CBase, CLim, "children", LbSz); ChLb && CcSt)
    {
        uint64_t Cur = (Doc.PointerSize == 8u) ? ReadU64(ChLb) : ReadU32(ChLb);
        std::unordered_set<uint64_t> Seen;
        while (Cur != 0u && Seen.insert(Cur).second)
        {
            const BlendBlock* Blk = Doc.FindBlock(Cur, ColAddr);
            if (!Blk) break;
            const uint64_t SubCol = ReadPointerField(Doc, CcSt, Doc.BlockPtr(*Blk), Blk->ByteSize, "collection");
            CollectCollectionMembership(Doc, SubCol, ColName, ThisHidden, ObjCollection, HiddenObjects, Visited);
            Cur = ReadPointerField(Doc, CcSt, Doc.BlockPtr(*Blk), Blk->ByteSize, "next");
        }
    }
}

void CollectViewLayerHiddenCollections(
    const BlendDocument&                        Doc,
    uint64_t                                    SceneAddr,
    uint64_t                                    LayerColAddr,
    bool                                        InheritedHidden,
    std::unordered_set<uint64_t>&               HiddenCols,
    std::unordered_set<uint64_t>&               Visited)
{
    if (LayerColAddr == 0u || !Visited.insert(LayerColAddr).second) return;
    const BlendBlock* LcBlk = Doc.FindBlock(LayerColAddr, SceneAddr);
    const BlendStruct* LcSt = Doc.FindStruct("LayerCollection");
    if (!LcBlk || !LcSt) return;

    const uint8_t* BlockBytes = Doc.BlockPtr(*LcBlk);
    const size_t   Lim  = LcBlk->ByteSize;
    const uint64_t Bits = ReadUnsignedField(LcSt, BlockBytes, Lim, "flag", 0u);
    const uint64_t ColPtr = ReadPointerField(Doc, LcSt, BlockBytes, Lim, "collection");
    const bool Hidden = InheritedHidden || ((Bits & 0x01u) != 0u) || ((Bits & 0x10u) != 0u);  // EXCLUDE | HIDE_VIEWPORT
    if (Hidden && ColPtr != 0u) HiddenCols.insert(ColPtr);

    uint32_t LbSz = 0u;
    if (const uint8_t* Lb = FieldSlice(LcSt, BlockBytes, Lim, "layer_collections", LbSz); Lb && LbSz >= Doc.PointerSize)
    {
        uint64_t Cur = (Doc.PointerSize == 8u) ? ReadU64(Lb) : ReadU32(Lb);
        std::unordered_set<uint64_t> Seen;
        while (Cur != 0u && Seen.insert(Cur).second)
        {
            CollectViewLayerHiddenCollections(Doc, SceneAddr, Cur, Hidden, HiddenCols, Visited);
            const BlendBlock* SubBlk = Doc.FindBlock(Cur, SceneAddr);
            if (!SubBlk) break;
            Cur = ReadPointerField(Doc, LcSt, Doc.BlockPtr(*SubBlk), SubBlk->ByteSize, "next");
        }
    }
}

std::vector<BlendModifierDesc> ParseEntityModifiers(const BlendDocument& Doc, uint64_t ObAddr, const BlendStruct* ObSt, const uint8_t* EntityBytes, size_t ObLim)
{
    std::vector<BlendModifierDesc> List;
    const BlendStruct* ModSt = Doc.FindStruct("ModifierData");
    if (!ModSt) return List;

    uint32_t LbSz = 0u;
    const uint8_t* LbPtr = FieldSlice(ObSt, EntityBytes, ObLim, "modifiers", LbSz);
    if (!LbPtr || LbSz < Doc.PointerSize) return List;

    uint64_t Cur = (Doc.PointerSize == 8u) ? ReadU64(LbPtr) : ReadU32(LbPtr);
    std::unordered_set<uint64_t> Seen;
    while (Cur != 0u && Seen.insert(Cur).second)
    {
        const BlendBlock* MBlk = Doc.FindBlock(Cur, ObAddr);
        if (!MBlk || MBlk->SdnaIndex >= Doc.Structs.size()) break;

        const BlendStruct& SpecSt = Doc.Structs[MBlk->SdnaIndex];
        const uint8_t*     MBase  = Doc.BlockPtr(*MBlk);
        const size_t       MLim   = MBlk->ByteSize;

        uint32_t HeaderSz = 0u;
        const uint8_t* HeaderPtr = (SpecSt.Name == "ModifierData") ? MBase : FieldSlice(&SpecSt, MBase, MLim, "modifier", HeaderSz);
        const size_t   HeaderLim = (SpecSt.Name == "ModifierData") ? MLim : size_t(HeaderSz);
        if (!HeaderPtr) break;

        const uint64_t NextAddr = ReadPointerField(Doc, ModSt, HeaderPtr, HeaderLim, "next");
        const int32_t  ModeBits = static_cast<int32_t>(ReadSignedField(ModSt, HeaderPtr, HeaderLim, "mode", 0));
        const std::string ModName = ReadCStringField(ModSt, HeaderPtr, HeaderLim, "name");

        // Bit 0 (eModifierMode_Realtime) = enabled in viewport
        if ((ModeBits & 1) != 0)
        {
            if (SpecSt.Name == "MirrorModifierData")
            {
                BlendModifierDesc D;
                D.Kind            = BlendModifierKind::Mirror;
                D.MirrorBits      = static_cast<uint32_t>(ReadUnsignedField(&SpecSt, MBase, MLim, "flag", MirrorAxisXBit));
                D.MergeTolerance  = ReadFloatField(&SpecSt, MBase, MLim, "tolerance", 0.001f);
                D.BisectThreshold = ReadFloatField(&SpecSt, MBase, MLim, "bisect_threshold", 0.001f);
                D.MirrorObAddr    = ReadPointerField(Doc, &SpecSt, MBase, MLim, "mirror_ob");
                std::string Axes;
                if (D.MirrorBits & MirrorAxisXBit) Axes += "X";
                if (D.MirrorBits & MirrorAxisYBit) Axes += "Y";
                if (D.MirrorBits & MirrorAxisZBit) Axes += "Z";
                D.Label = "Mirror " + (Axes.empty() ? std::string("?") : Axes);
                List.push_back(std::move(D));
            }
            else if (SpecSt.Name == "SubsurfModifierData")
            {
                BlendModifierDesc D;
                D.Kind           = BlendModifierKind::Subsurf;
                D.SubdivKind     = static_cast<int>(ReadSignedField(&SpecSt, MBase, MLim, "subdivType", 0));
                D.SubdivLevels   = static_cast<int>(ReadSignedField(&SpecSt, MBase, MLim, "levels", 1));
                D.SubsurfBits    = static_cast<uint32_t>(ReadUnsignedField(&SpecSt, MBase, MLim, "flags", SubsurfUseCreaseBit));
                D.BoundarySmooth = static_cast<int>(ReadSignedField(&SpecSt, MBase, MLim, "boundary_smooth", 0));
                D.Label          = "Subdiv L" + std::to_string(std::max(0, D.SubdivLevels));
                List.push_back(std::move(D));
            }
            else if (SpecSt.Name == "SolidifyModifierData")
            {
                BlendModifierDesc D;
                D.Kind              = BlendModifierKind::Solidify;
                D.SolidifyOffset    = ReadFloatField(&SpecSt, MBase, MLim, "offset", 0.01f);
                D.SolidifyOffsetFac = ReadFloatField(&SpecSt, MBase, MLim, "offset_fac", -1.0f);
                D.SolidifyCreaseRim = ReadFloatField(&SpecSt, MBase, MLim, "crease_rim", 0.0f);
                D.SolidifyBits      = static_cast<uint32_t>(ReadUnsignedField(&SpecSt, MBase, MLim, "flag", SolidifyRimBit));
                D.Label             = "Solidify";
                List.push_back(std::move(D));
            }
            else if (SpecSt.Name == "ShrinkwrapModifierData")
            {
                BlendModifierDesc D;
                D.Kind             = BlendModifierKind::Shrinkwrap;
                D.ShrinkTargetAddr = ReadPointerField(Doc, &SpecSt, MBase, MLim, "target");
                D.ShrinkKind       = static_cast<int>(ReadSignedField(&SpecSt, MBase, MLim, "shrinkType", 0));
                D.ShrinkKeepDist   = ReadFloatField(&SpecSt, MBase, MLim, "keepDist", 0.0f);
                D.Label            = "Shrinkwrap";
                List.push_back(std::move(D));
            }
            else if (SpecSt.Name == "MaskModifierData")
            {
                BlendModifierDesc D;
                D.Kind       = BlendModifierKind::Mask;
                D.MaskGroup  = ReadCStringField(&SpecSt, MBase, MLim, "vgroup");
                D.MaskBits   = static_cast<uint32_t>(ReadUnsignedField(&SpecSt, MBase, MLim, "flag", 0u));
                D.MaskThresh = ReadFloatField(&SpecSt, MBase, MLim, "threshold", 0.0f);
                D.Label      = "Mask";
                List.push_back(std::move(D));
            }
            else if (SpecSt.Name == "ArrayModifierData")
            {
                BlendModifierDesc D;
                D.Kind            = BlendModifierKind::Array;
                D.ArrayCount      = static_cast<int>(ReadSignedField(&SpecSt, MBase, MLim, "count", 1));
                D.ArrayOffsetBits = static_cast<uint32_t>(ReadUnsignedField(&SpecSt, MBase, MLim, "offset_type", ArrayOffsetRelativeBit));
                uint32_t Sz = 0u;
                if (const uint8_t* O = FieldSlice(&SpecSt, MBase, MLim, "offset", Sz); O && Sz >= 12u)
                    for (int K = 0; K < 3; ++K) D.ArrayConst[K] = ReadF32(O + K * 4u);
                if (const uint8_t* S = FieldSlice(&SpecSt, MBase, MLim, "scale", Sz); S && Sz >= 12u)
                    for (int K = 0; K < 3; ++K) D.ArrayRel[K] = ReadF32(S + K * 4u);
                D.Label = "Array x" + std::to_string(std::max(1, D.ArrayCount));
                List.push_back(std::move(D));
            }
            else if (SpecSt.Name == "BevelModifierData")
            {
                BlendModifierDesc D;
                D.Kind          = BlendModifierKind::Bevel;
                D.BevelWidth    = ReadFloatField(&SpecSt, MBase, MLim, "value", 0.0f);
                D.BevelSegments = static_cast<int>(ReadSignedField(&SpecSt, MBase, MLim, "res", 1));
                D.BevelLimBits  = static_cast<uint32_t>(ReadUnsignedField(&SpecSt, MBase, MLim, "lim_flags", 0u));
                D.BevelAngleRad = ReadFloatField(&SpecSt, MBase, MLim, "bevel_angle", 0.5236f);
                D.Label         = "Bevel";
                List.push_back(std::move(D));
            }
            else if (SpecSt.Name == "SkinModifierData")
            {
                BlendModifierDesc D;
                D.Kind  = BlendModifierKind::Skin;
                D.Label = "Skin";
                List.push_back(std::move(D));
            }
            else if (SpecSt.Name == "NodesModifierData")
            {
                // Blender 4.1+/5.x Smooth by Angle geometry-node modifier
                if (ModName.find("Smooth") != std::string::npos || ModName.find("Auto") != std::string::npos)
                {
                    BlendModifierDesc D;
                    D.Kind  = BlendModifierKind::AutoSmooth;
                    D.Label = "AutoSmooth";
                    List.push_back(std::move(D));
                }
            }
        }
        Cur = NextAddr;
    }

    return List;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SketcherBlendImportReport DecodeSketcherBlendFile(
    const std::string&                FilePath,
    const SketcherBlendImportOptions& Options)
{
    const auto StartClock = std::chrono::steady_clock::now();
    SketcherBlendImportReport Report;

    // 1. Read file bytes
    std::ifstream Input(FilePath, std::ios::binary | std::ios::ate);
    if (!Input.is_open())
    {
        Report.Refusal = "Cannot open Blender file: " + FilePath;
        return Report;
    }
    const std::streamsize FileLen = Input.tellg();
    if (FileLen <= 16)
    {
        Report.Refusal = "Blender file is empty or truncated: " + FilePath;
        return Report;
    }
    std::vector<uint8_t> RawBytes(static_cast<size_t>(FileLen));
    Input.seekg(0, std::ios::beg);
    if (!Input.read(reinterpret_cast<char*>(RawBytes.data()), FileLen))
    {
        Report.Refusal = "Failed reading Blender file: " + FilePath;
        return Report;
    }

    // 2. Decompress ZSTD / GZIP / raw
    BlendDocument Doc;
    if (RawBytes.size() >= 4u && RawBytes[0] == 0x28u && RawBytes[1] == 0xB5u && RawBytes[2] == 0x2Fu && RawBytes[3] == 0xFDu)
    {
        if (!DecompressZstdAllFrames(RawBytes.data(), RawBytes.size(), Doc.Bytes, Report.Refusal)) return Report;
    }
    else if (RawBytes.size() >= 2u && RawBytes[0] == 0x1Fu && RawBytes[1] == 0x8Bu)
    {
        if (!DecompressGzipPayload(RawBytes.data(), RawBytes.size(), Doc.Bytes, Report.Refusal)) return Report;
    }
    else if (RawBytes.size() >= 7u && std::memcmp(RawBytes.data(), "BLENDER", 7u) == 0)
    {
        Doc.Bytes = std::move(RawBytes);
    }
    else
    {
        Report.Refusal = "Unrecognized .blend compression or header";
        return Report;
    }

    // 3. Parse binary blocks & SDNA schema
    if (!ParseBlendBinary(Doc, Report.Refusal)) return Report;
    Report.BlenderVersion = Doc.VersionText;

    const BlendStruct* ObSt    = Doc.FindStruct("Object");
    const BlendStruct* SceneSt = Doc.FindStruct("Scene");
    const BlendStruct* VlSt    = Doc.FindStruct("ViewLayer");
    if (!ObSt)
    {
        Report.Refusal = "Missing Object struct in .blend SDNA";
        return Report;
    }

    // 4. Resolve collection membership & hidden view-layer collections
    std::unordered_map<uint64_t, std::string> ObjCollection;
    std::unordered_set<uint64_t>              HiddenByCollection;
    std::unordered_set<uint64_t>              HiddenCols;
    std::unordered_set<uint64_t>              VisitedCols;
    std::unordered_set<uint64_t>              VisitedLc;

    if (SceneSt)
    {
        for (const BlendBlock& Blk : Doc.Blocks)
        {
            if (std::strcmp(Blk.Code, "SC") != 0) continue;
            const uint8_t* SBase = Doc.BlockPtr(Blk);
            const size_t   SLim  = Blk.ByteSize;

            if (VlSt)
            {
                uint32_t LbSz = 0u;
                if (const uint8_t* VlLb = FieldSlice(SceneSt, SBase, SLim, "view_layers", LbSz); VlLb && LbSz >= Doc.PointerSize)
                {
                    const uint64_t VlAddr = (Doc.PointerSize == 8u) ? ReadU64(VlLb) : ReadU32(VlLb);
                    if (const BlendBlock* VlBlk = Doc.FindBlock(VlAddr, Blk.OldAddr))
                    {
                        uint32_t LcLbSz = 0u;
                        if (const uint8_t* LcLb = FieldSlice(VlSt, Doc.BlockPtr(*VlBlk), VlBlk->ByteSize, "layer_collections", LcLbSz);
                            LcLb && LcLbSz >= Doc.PointerSize)
                        {
                            uint64_t CurLc = (Doc.PointerSize == 8u) ? ReadU64(LcLb) : ReadU32(LcLb);
                            while (CurLc != 0u)
                            {
                                CollectViewLayerHiddenCollections(Doc, Blk.OldAddr, CurLc, false, HiddenCols, VisitedLc);
                                const BlendBlock* CurBlk = Doc.FindBlock(CurLc, Blk.OldAddr);
                                const BlendStruct* LcSt  = Doc.FindStruct("LayerCollection");
                                if (!CurBlk || !LcSt) break;
                                CurLc = ReadPointerField(Doc, LcSt, Doc.BlockPtr(*CurBlk), CurBlk->ByteSize, "next");
                            }
                        }
                    }
                }
            }

            const uint64_t MasterCol = ReadPointerField(Doc, SceneSt, SBase, SLim, "master_collection");
            CollectCollectionMembership(Doc, MasterCol, "Scene", false, ObjCollection, HiddenByCollection, VisitedCols);
            break;                                                                           // Active primary scene
        }
    }

    // Also mark objects belonging to view-layer-excluded collections
    for (uint64_t HiddenCol : HiddenCols)
    {
        std::unordered_set<uint64_t> LocalVisited;
        CollectCollectionMembership(Doc, HiddenCol, "Hidden", true, ObjCollection, HiddenByCollection, LocalVisited);
    }

    // 5. Parse all Object blocks (including Empties used as mirror_ob or parents!)
    std::unordered_map<uint64_t, BlendEntityEntry> AllObjects;
    std::vector<uint64_t>                          SurfaceEntityOrder;

    for (const BlendBlock& Blk : Doc.Blocks)
    {
        if (std::strcmp(Blk.Code, "OB") != 0 || Blk.OldAddr == 0u) continue;
        const uint8_t* OBase = Doc.BlockPtr(Blk);
        const size_t   OLim  = Blk.ByteSize;

        BlendEntityEntry Entry;
        Entry.ObjAddr    = Blk.OldAddr;
        Entry.Name       = ReadIdName(Doc, ObSt, OBase, OLim);
        Entry.ParentAddr = ReadPointerField(Doc, ObSt, OBase, OLim, "parent");
        Entry.Collection = ObjCollection.count(Blk.OldAddr) ? ObjCollection[Blk.OldAddr] : "Scene";

        const int16_t ObType = static_cast<int16_t>(ReadSignedField(ObSt, OBase, OLim, "type", 0));
        if (ObType == 1) Entry.SurfaceBlockAddr = ReadPointerField(Doc, ObSt, OBase, OLim, "data");

        // Visibility: Blender restrictflag bit 0 (OB_HIDE_VIEWPORT) + base_flag bit 0x80 (BASE_ENABLED_AND_VISIBLE_IN_DEFAULT_VIEWPORT)
        const uint64_t RestrictBits = ReadUnsignedField(ObSt, OBase, OLim, "restrictflag", 0u);
        const uint64_t LayerVisBits     = ReadUnsignedField(ObSt, OBase, OLim, "base_flag", 0x80u);
        const bool HiddenByLayer     = (ObSt->FindField("base_flag") != nullptr) && ((LayerVisBits & 0x80u) == 0u);
        const bool HiddenByRestrict = (RestrictBits & 0x01u) != 0u;
        const bool HiddenByCol      = HiddenByCollection.count(Blk.OldAddr) > 0u;
        Entry.VisibleCondition      = !(HiddenByLayer || HiddenByRestrict || HiddenByCol);

        // Local TRS
        float Loc[3]   = { 0.0f, 0.0f, 0.0f };
        float Rot[3]   = { 0.0f, 0.0f, 0.0f };
        float Quat[4]  = { 1.0f, 0.0f, 0.0f, 0.0f };
        float Axis[3]  = { 0.0f, 1.0f, 0.0f };
        float Size[3]  = { 1.0f, 1.0f, 1.0f };
        uint32_t Sz = 0u;
        if (const uint8_t* P = FieldSlice(ObSt, OBase, OLim, "loc", Sz); P && Sz >= 12u)
            for (int K = 0; K < 3; ++K) Loc[K] = ReadF32(P + K * 4u);
        if (const uint8_t* P = FieldSlice(ObSt, OBase, OLim, "rot", Sz); P && Sz >= 12u)
            for (int K = 0; K < 3; ++K) Rot[K] = ReadF32(P + K * 4u);
        if (const uint8_t* P = FieldSlice(ObSt, OBase, OLim, "quat", Sz); P && Sz >= 16u)
            for (int K = 0; K < 4; ++K) Quat[K] = ReadF32(P + K * 4u);
        if (const uint8_t* P = FieldSlice(ObSt, OBase, OLim, "rotAxis", Sz); P && Sz >= 12u)
            for (int K = 0; K < 3; ++K) Axis[K] = ReadF32(P + K * 4u);
        if (const uint8_t* P = FieldSlice(ObSt, OBase, OLim, "size", Sz); P && Sz >= 12u)
            for (int K = 0; K < 3; ++K) Size[K] = ReadF32(P + K * 4u);
        else if (const uint8_t* P = FieldSlice(ObSt, OBase, OLim, "scale", Sz); P && Sz >= 12u)
            for (int K = 0; K < 3; ++K) Size[K] = ReadF32(P + K * 4u);
        const float   RotAngle = ReadFloatField(ObSt, OBase, OLim, "rotAngle", 0.0f);
        const int16_t RotMode  = static_cast<int16_t>(ReadSignedField(ObSt, OBase, OLim, "rotmode", 1));
        Entry.LocalTrs         = ComposeLocalTrs(Loc, Rot, Quat, Axis, RotAngle, RotMode, Size);

        if (const uint8_t* Pinv = FieldSlice(ObSt, OBase, OLim, "parentinv", Sz); Pinv && Sz >= 64u)
        {
            for (int K = 0; K < 16; ++K) Entry.ParentInv.M[K] = double(ReadF32(Pinv + K * 4u));
        }

        if (ObType == 1 && Entry.SurfaceBlockAddr != 0u)
        {
            Entry.Modifiers = ParseEntityModifiers(Doc, Blk.OldAddr, ObSt, OBase, OLim);
            SurfaceEntityOrder.push_back(Blk.OldAddr);
        }

        AllObjects[Blk.OldAddr] = std::move(Entry);
    }

    // 6. Compute world matrices (resolving parent chains)
    std::unordered_map<uint64_t, Mat4d> WorldByAddr;
    std::unordered_set<uint64_t>        Computing;
    std::function<Mat4d(uint64_t)> ResolveWorld = [&](uint64_t Addr) -> Mat4d
    {
        const auto Found = WorldByAddr.find(Addr);
        if (Found != WorldByAddr.end()) return Found->second;
        const auto ObjIt = AllObjects.find(Addr);
        if (ObjIt == AllObjects.end() || !Computing.insert(Addr).second) return Mat4d{};
        const BlendEntityEntry& E = ObjIt->second;
        Mat4d W = E.LocalTrs;
        if (E.ParentAddr != 0u && AllObjects.count(E.ParentAddr))
        {
            const Mat4d ParentW = ResolveWorld(E.ParentAddr);
            W = MultiplyMat4(ParentW, MultiplyMat4(E.ParentInv, E.LocalTrs));
        }
        Computing.erase(Addr);
        WorldByAddr[Addr] = W;
        return W;
    };

    Mat4d UnitMat;
    UnitMat.At(0, 0) = Options.UnitScale;
    UnitMat.At(1, 1) = Options.UnitScale;
    UnitMat.At(2, 2) = Options.UnitScale;

    for (auto& Pair : AllObjects)
    {
        Pair.second.WorldMatrix = MultiplyMat4(UnitMat, ResolveWorld(Pair.first));
    }
    for (auto& Pair : WorldByAddr)
    {
        Pair.second = MultiplyMat4(UnitMat, Pair.second);
    }

    // 7. Topological dependency order for Shrinkwrap targets (evaluate targets before dependents)
    std::unordered_set<uint64_t> ShrinkTargets;
    for (uint64_t Addr : SurfaceEntityOrder)
    {
        const BlendEntityEntry& E = AllObjects[Addr];
        if (!E.VisibleCondition && !Options.IncludeHiddenCondition) continue;
        for (const BlendModifierDesc& Mod : E.Modifiers)
        {
            if (Mod.Kind == BlendModifierKind::Shrinkwrap && Mod.ShrinkTargetAddr != 0u)
                ShrinkTargets.insert(Mod.ShrinkTargetAddr);
        }
    }

    std::vector<uint64_t> EvalOrder;
    std::unordered_set<uint64_t> Scheduled;
    std::unordered_set<uint64_t> Visiting;
    std::function<void(uint64_t)> ScheduleEntity = [&](uint64_t Addr)
    {
        if (Scheduled.count(Addr) || !Visiting.insert(Addr).second) return;
        const auto It = AllObjects.find(Addr);
        if (It == AllObjects.end() || It->second.SurfaceBlockAddr == 0u) { Visiting.erase(Addr); return; }
        for (const BlendModifierDesc& Mod : It->second.Modifiers)
        {
            if (Mod.Kind == BlendModifierKind::Shrinkwrap && Mod.ShrinkTargetAddr != 0u && AllObjects.count(Mod.ShrinkTargetAddr))
                ScheduleEntity(Mod.ShrinkTargetAddr);
        }
        Visiting.erase(Addr);
        Scheduled.insert(Addr);
        EvalOrder.push_back(Addr);
    };

    for (uint64_t Addr : SurfaceEntityOrder)
    {
        const BlendEntityEntry& E = AllObjects[Addr];
        if (!E.VisibleCondition && !Options.IncludeHiddenCondition)
        {
            ++Report.SkippedHidden;
            continue;
        }
        ScheduleEntity(Addr);
    }

    // 8. Evaluate every scheduled mesh object & its modifier stack
    auto Geometry = std::make_shared<SketcherReferenceGeometry>();
    SketcherReferenceSurface& Surface = Report.Surface;
    Surface.SourcePath   = FilePath;
    Surface.Name         = std::filesystem::path(FilePath).stem().string();
    Surface.ScaleFactor  = Options.UnitScale;
    Surface.MinBound     = { 1.0e30, 1.0e30, 1.0e30 };
    Surface.MaxBound     = { -1.0e30, -1.0e30, -1.0e30 };

    std::unordered_map<uint64_t, SketcherReferenceSurface> EvaluatedTargets;

    for (uint64_t Addr : EvalOrder)
    {
        BlendEntityEntry& Obj = AllObjects[Addr];
        const bool OutputVisible = Obj.VisibleCondition || Options.IncludeHiddenCondition;

        PolySurface Poly;
        if (!ExtractRawPolySurface(Doc, Obj.SurfaceBlockAddr, Poly, Obj.SurfaceColour)) continue;
        Report.RawPolygons += static_cast<uint32_t>(Poly.Faces.size());

        std::string ModSummary;
        double SmoothRadians = Options.AutoSmoothDegrees * DegToRad;

        if (Options.EvaluateModifiersCondition)
        {
            for (const BlendModifierDesc& Mod : Obj.Modifiers)
            {
                if (!ModSummary.empty()) ModSummary += " · ";
                ModSummary += Mod.Label;
                ++Report.EvaluatedModifiers;

                switch (Mod.Kind)
                {
                case BlendModifierKind::Mask:       ApplyMaskModifier(Poly, Mod); break;
                case BlendModifierKind::Skin:       ApplySkinModifier(Poly); break;
                case BlendModifierKind::Mirror:     ApplyMirrorModifier(Poly, Mod, Obj.WorldMatrix, WorldByAddr); break;
                case BlendModifierKind::Subsurf:    ApplySubsurfModifier(Poly, Mod, Options.MaxSubdivisionLevel); break;
                case BlendModifierKind::Solidify:   ApplySolidifyModifier(Poly, Mod); break;
                case BlendModifierKind::Shrinkwrap: ApplyShrinkwrapModifier(Poly, Mod, Obj.WorldMatrix, EvaluatedTargets); break;
                case BlendModifierKind::Array:      ApplyArrayModifier(Poly, Mod); break;
                case BlendModifierKind::Bevel:      ApplyBevelModifier(Poly, Mod); break;
                case BlendModifierKind::AutoSmooth: SmoothRadians = 30.0 * DegToRad; break;
                }
            }
        }

        // If another object Shrinkwraps onto this object, build a dedicated target BVH for it
        if (ShrinkTargets.count(Addr))
        {
            SketcherReferenceSurface TargetSurf;
            auto TargetGeo = std::make_shared<SketcherReferenceGeometry>();
            std::array<double, 3> TMin{}, TMax{};
            EmitWorldSurfaceGeometry(Poly, Obj.WorldMatrix, 0u, SmoothRadians, TargetGeo->Corners, TargetGeo->CreaseSegments, TMin, TMax);
            TargetGeo->Bvh = BuildReferenceSurfaceBvh(TargetGeo->Corners);
            TargetSurf.Geometry = std::move(TargetGeo);
            TargetSurf.MinBound = TMin;
            TargetSurf.MaxBound = TMax;
            EvaluatedTargets[Addr] = std::move(TargetSurf);
        }

        if (!OutputVisible) continue;

        const uint32_t PartIdx      = static_cast<uint32_t>(Surface.Parts.size());
        const uint32_t FirstCorner  = static_cast<uint32_t>(Geometry->Corners.size());
        const uint32_t FirstSegment = static_cast<uint32_t>(Geometry->CreaseSegments.size());
        std::array<double, 3> PMin{}, PMax{};
        EmitWorldSurfaceGeometry(Poly, Obj.WorldMatrix, PartIdx, SmoothRadians, Geometry->Corners, Geometry->CreaseSegments, PMin, PMax);

        const uint32_t CornerCount  = static_cast<uint32_t>(Geometry->Corners.size()) - FirstCorner;
        const uint32_t SegmentCount = static_cast<uint32_t>(Geometry->CreaseSegments.size()) - FirstSegment;
        if (CornerCount == 0u) continue;

        SketcherReferencePart Part;
        Part.Name             = Obj.Name;
        Part.Collection       = Obj.Collection;
        Part.ModifierSummary  = ModSummary.empty() ? "Raw surface" : ModSummary;
        Part.FirstCorner      = FirstCorner;
        Part.CornerCount      = CornerCount;
        Part.FirstSegment     = FirstSegment;
        Part.SegmentCount     = SegmentCount;
        Part.SurfaceColour       = Obj.SurfaceColour;
        Part.MinBound         = PMin;
        Part.MaxBound         = PMax;
        Part.VisibleCondition = true;
        Part.LockedCondition  = false;
        Surface.Parts.push_back(std::move(Part));

        for (int Axis = 0; Axis < 3; ++Axis)
        {
            Surface.MinBound[Axis] = std::min(Surface.MinBound[Axis], PMin[Axis]);
            Surface.MaxBound[Axis] = std::max(Surface.MaxBound[Axis], PMax[Axis]);
        }
        ++Report.SourceObjects;
    }

    if (Geometry->Corners.empty())
    {
        Report.Refusal = "No visible mesh geometry found in .blend file";
        return Report;
    }

    // 9. Build O(log N) BVH across all evaluated vehicle triangles
    Geometry->Bvh = BuildReferenceSurfaceBvh(Geometry->Corners);
    Report.OutputTriangles = static_cast<uint32_t>(Geometry->Corners.size() / 3u);
    Report.CreaseSegments  = static_cast<uint32_t>(Geometry->CreaseSegments.size());
    Surface.Geometry       = std::move(Geometry);

    const auto EndClock = std::chrono::steady_clock::now();
    Report.ElapsedMs = std::chrono::duration<double, std::milli>(EndClock - StartClock).count();

    char SummaryBuf[192];
    std::snprintf(SummaryBuf, sizeof(SummaryBuf), "Blender %s · %u parts · %u tris · %u modifiers · %.0f ms",
        Report.BlenderVersion.c_str(), Report.SourceObjects, Report.OutputTriangles, Report.EvaluatedModifiers, Report.ElapsedMs);
    Surface.SummaryNote = SummaryBuf;

    return Report;
}

} // namespace Frontier::Workspaces::Sketcher

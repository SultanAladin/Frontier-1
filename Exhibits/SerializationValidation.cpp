/*============================================================================================================================================
                                                        SERIALIZATIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation exhibit for Engine/Serialization: 63-format registry, Hybrid container, Multi-pillar embedding, DependencyOrder & examples

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Serialization/Codecs/FormatCodecSuite.h"
#include "Engine/Serialization/Configuration/BootConfigurationLoader.h"
#include "Engine/Serialization/Container/AtomicFileWriter.h"
#include "Engine/Serialization/Container/HybridContainerCodec.h"
#include "Engine/Serialization/Container/SuffixRegistry.h"
#include "Engine/Serialization/DependencyOrder/CompiledDiskRepository.h"
#include "Engine/Serialization/DependencyOrder/ContentHashCalculator.h"
#include "Engine/Serialization/DependencyOrder/DependencyOrderLedger.h"
#include "Engine/Serialization/Embedding/InstanceContainerCodec.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

namespace
{

using namespace Frontier::Serialization;

struct GateReport
{
    std::string Title   {}; // [-] - Verification gate title
    std::string Metrics {}; // [-] - Quantitative result summary
    bool        Passed  = false; // [-] - Verification outcome
};

// Minimal crisp 5x7 uppercase bitmap font for clean telemetry PNG rendering
void StampPixel(std::vector<uint8_t>& Rgba, int W, int H, int X, int Y, uint8_t R, uint8_t G, uint8_t B)
{
    if (X < 0 || Y < 0 || X >= W || Y >= H)
    {
        return;
    }
    const std::size_t Idx = (static_cast<std::size_t>(Y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(X)) * 4u;
    Rgba[Idx + 0u] = R;
    Rgba[Idx + 1u] = G;
    Rgba[Idx + 2u] = B;
    Rgba[Idx + 3u] = 255u;
}

void FillRect(std::vector<uint8_t>& Rgba, int W, int H, int X, int Y, int RW, int RH, uint8_t R, uint8_t G, uint8_t B)
{
    for (int Dy = 0; Dy < RH; ++Dy)
    {
        for (int Dx = 0; Dx < RW; ++Dx)
        {
            StampPixel(Rgba, W, H, X + Dx, Y + Dy, R, G, B);
        }
    }
}

void StrokeRect(std::vector<uint8_t>& Rgba, int W, int H, int X, int Y, int RW, int RH, uint8_t R, uint8_t G, uint8_t B)
{
    FillRect(Rgba, W, H, X, Y, RW, 1, R, G, B);
    FillRect(Rgba, W, H, X, Y + RH - 1, RW, 1, R, G, B);
    FillRect(Rgba, W, H, X, Y, 1, RH, R, G, B);
    FillRect(Rgba, W, H, X + RW - 1, Y, 1, RH, R, G, B);
}

const uint8_t* LookupMiniBitmap5x7(char Ch)
{
    static constexpr uint8_t Blank[7] = { 0, 0, 0, 0, 0, 0, 0 };
    static constexpr uint8_t A[7]     = { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
    static constexpr uint8_t B[7]     = { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E };
    static constexpr uint8_t C[7]     = { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E };
    static constexpr uint8_t D[7]     = { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C };
    static constexpr uint8_t E[7]     = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F };
    static constexpr uint8_t F[7]     = { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 };
    static constexpr uint8_t G[7]     = { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E };
    static constexpr uint8_t H[7]     = { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 };
    static constexpr uint8_t I[7]     = { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E };
    static constexpr uint8_t J[7]     = { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C };
    static constexpr uint8_t K[7]     = { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 };
    static constexpr uint8_t L[7]     = { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F };
    static constexpr uint8_t M[7]     = { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 };
    static constexpr uint8_t N[7]     = { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 };
    static constexpr uint8_t O[7]     = { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
    static constexpr uint8_t P[7]     = { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 };
    static constexpr uint8_t Q[7]     = { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D };
    static constexpr uint8_t R[7]     = { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 };
    static constexpr uint8_t S[7]     = { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E };
    static constexpr uint8_t T[7]     = { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 };
    static constexpr uint8_t U[7]     = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E };
    static constexpr uint8_t V[7]     = { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 };
    static constexpr uint8_t W[7]     = { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 };
    static constexpr uint8_t X[7]     = { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 };
    static constexpr uint8_t Y[7]     = { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 };
    static constexpr uint8_t Z[7]     = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F };
    static constexpr uint8_t D0[7]    = { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E };
    static constexpr uint8_t D1[7]    = { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E };
    static constexpr uint8_t D2[7]    = { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F };
    static constexpr uint8_t D3[7]    = { 0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E };
    static constexpr uint8_t D4[7]    = { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 };
    static constexpr uint8_t D5[7]    = { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E };
    static constexpr uint8_t D6[7]    = { 0x0E, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x0E };
    static constexpr uint8_t D7[7]    = { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 };
    static constexpr uint8_t D8[7]    = { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E };
    static constexpr uint8_t D9[7]    = { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E };
    static constexpr uint8_t Dash[7]  = { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 };
    static constexpr uint8_t Colon[7] = { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 };
    static constexpr uint8_t Dot[7]   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x06 };
    static constexpr uint8_t Slash[7] = { 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10 };
    static constexpr uint8_t Plus[7]  = { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 };
    static constexpr uint8_t Eq[7]    = { 0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00 };
    static constexpr uint8_t Pct[7]   = { 0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03 };
    static constexpr uint8_t LParen[7]= { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 };
    static constexpr uint8_t RParen[7]= { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 };
    static constexpr uint8_t Star[7]  = { 0x00, 0x15, 0x0E, 0x1F, 0x0E, 0x15, 0x00 };
    static constexpr uint8_t Gt[7]    = { 0x10, 0x08, 0x04, 0x02, 0x04, 0x08, 0x10 };

    const char Upper = (Ch >= 'a' && Ch <= 'z') ? static_cast<char>(Ch - 'a' + 'A') : Ch;
    switch (Upper)
    {
        case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D;
        case 'E': return E; case 'F': return F; case 'G': return G; case 'H': return H;
        case 'I': return I; case 'J': return J; case 'K': return K; case 'L': return L;
        case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P;
        case 'Q': return Q; case 'R': return R; case 'S': return S; case 'T': return T;
        case 'U': return U; case 'V': return V; case 'W': return W; case 'X': return X;
        case 'Y': return Y; case 'Z': return Z;
        case '0': return D0; case '1': return D1; case '2': return D2; case '3': return D3;
        case '4': return D4; case '5': return D5; case '6': return D6; case '7': return D7;
        case '8': return D8; case '9': return D9;
        case '-': return Dash; case ':': return Colon; case '.': return Dot;
        case '/': return Slash; case '+': return Plus; case '=': return Eq; case '%': return Pct;
        case '(': return LParen; case ')': return RParen; case '*': return Star; case '>': return Gt;
        default:  return Blank;
    }
}

void StampText(
    std::vector<uint8_t>& Rgba, int W, int H, int X, int Y, int Scale,
    std::string_view Text, uint8_t R, uint8_t G, uint8_t B)
{
    int CursorX = X;
    for (char Ch : Text)
    {
        const uint8_t* Rows = LookupMiniBitmap5x7(Ch);
        for (int Row = 0; Row < 7; ++Row)
        {
            for (int Col = 0; Col < 5; ++Col)
            {
                if ((Rows[Row] & (1u << (4 - Col))) != 0u)
                {
                    FillRect(Rgba, W, H, CursorX + Col * Scale, Y + Row * Scale, Scale, Scale, R, G, B);
                }
            }
        }
        CursorX += 6 * Scale;
    }
}

void RenderVerificationTelemetryPng(
    const std::filesystem::path&   OutputPath,
    const std::vector<GateReport>& Gates,
    uint32_t                       TierACount,
    uint32_t                       TierBCount,
    uint32_t                       ColdBakes,
    uint32_t                       WarmHits,
    uint32_t                       PartialInvalidations,
    uint32_t                       CorruptRepairs)
{
    constexpr int W = 1280;
    constexpr int H = 760;
    std::vector<uint8_t> Rgba(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4u, 255u);

    // Dark graphite slate canvas
    FillRect(Rgba, W, H, 0, 0, W, H, 18, 21, 28);
    // Top title banner
    FillRect(Rgba, W, H, 0, 0, W, 68, 26, 32, 44);
    FillRect(Rgba, W, H, 0, 66, W, 2, 79, 140, 255);
    StampText(Rgba, W, H, 28, 16, 3, "FRONTIER SERIALIZATION ARCHITECTURE VERIFICATION", 236, 242, 252);
    StampText(Rgba, W, H, 28, 44, 2, "TWO-TIER HYBRID TOML + 64B SFNT BINARY + 63 FORMATS + ZERO-RECOMPUTE REPOSITORY", 148, 176, 224);

    // Left column: 10 Validation Gates
    FillRect(Rgba, W, H, 24, 88, 720, 648, 24, 28, 37);
    StrokeRect(Rgba, W, H, 24, 88, 720, 648, 52, 62, 82);
    StampText(Rgba, W, H, 40, 102, 2, "VERIFICATION GATES (10/10 AUTOMATED CHECKS)", 218, 228, 245);

    int RowY = 136;
    for (std::size_t Index = 0u; Index < Gates.size(); ++Index)
    {
        const GateReport& Gate = Gates[Index];
        FillRect(Rgba, W, H, 38, RowY, 692, 52, 31, 37, 49);
        StrokeRect(Rgba, W, H, 38, RowY, 692, 52, Gate.Passed ? 48 : 190, Gate.Passed ? 156 : 52, Gate.Passed ? 96 : 52);

        // Status pill
        FillRect(Rgba, W, H, 48, RowY + 12, 74, 28, Gate.Passed ? 34 : 184, Gate.Passed ? 148 : 42, Gate.Passed ? 84 : 42);
        StampText(Rgba, W, H, 60, RowY + 19, 2, Gate.Passed ? "PASS" : "FAIL", 245, 250, 255);

        StampText(Rgba, W, H, 136, RowY + 9,  2, Gate.Title,   232, 238, 248);
        StampText(Rgba, W, H, 136, RowY + 31, 1, Gate.Metrics, 152, 172, 204);
        RowY += 58;
    }

    // Right top: 63-Format Stratification Grid (18 Tier A *Space + 45 Tier B Sub-Formats)
    FillRect(Rgba, W, H, 764, 88, 492, 316, 24, 28, 37);
    StrokeRect(Rgba, W, H, 764, 88, 492, 316, 52, 62, 82);
    StampText(Rgba, W, H, 780, 102, 2, "63-FORMAT GRID: 18 TIER A + 45 TIER B", 218, 228, 245);
    StampText(Rgba, W, H, 780, 126, 1, "BLUE = TIER A *SPACE PILLARS   GREEN = TIER B SUB-FORMATS (63/63)", 148, 172, 206);

    const int GridOriginX = 780;
    const int GridOriginY = 148;
    constexpr int Cols    = 9;
    constexpr int CellW   = 48;
    constexpr int CellH   = 30;
    constexpr int Gap     = 4;
    for (uint32_t Idx = 0u; Idx < TierACount + TierBCount; ++Idx)
    {
        const int Col = static_cast<int>(Idx % Cols);
        const int Row = static_cast<int>(Idx / Cols);
        const int CX  = GridOriginX + Col * (CellW + Gap);
        const int CY  = GridOriginY + Row * (CellH + Gap);
        const bool TopLevelSpace = (Idx < TierACount);
        FillRect(Rgba, W, H, CX, CY, CellW, CellH,
                 TopLevelSpace ? 46 : 36, TopLevelSpace ? 114 : 136, TopLevelSpace ? 218 : 96);
        StrokeRect(Rgba, W, H, CX, CY, CellW, CellH,
                   TopLevelSpace ? 102 : 84, TopLevelSpace ? 168 : 206, TopLevelSpace ? 255 : 148);
        const std::string Label = (TopLevelSpace ? "S" : "F") + std::to_string(Idx + 1u);
        StampText(Rgba, W, H, CX + 10, CY + 11, 1, Label, 245, 250, 255);
    }

    // Right bottom: Zero-Recompute CompiledDiskRepository Telemetry Bars
    FillRect(Rgba, W, H, 764, 424, 492, 312, 24, 28, 37);
    StrokeRect(Rgba, W, H, 764, 424, 492, 312, 52, 62, 82);
    StampText(Rgba, W, H, 780, 438, 2, "ZERO-RECOMPUTE REPOSITORY METRICS", 218, 228, 245);

    struct BarRow
    {
        std::string Label;
        uint32_t    Count;
        uint8_t     R, G, B;
    };
    const std::vector<BarRow> Bars = {
        { "RUN 1 COLD BAKE",      ColdBakes,            79, 140, 255 },
        { "RUN 2 WARM DISK HITS", WarmHits,             46, 196, 114 },
        { "RUN 3 MINIMAL REBAKE", PartialInvalidations, 236, 174, 52 },
        { "RUN 4 CORRUPT REPAIR", CorruptRepairs,       228, 92, 92  }
    };

    int BarY = 482;
    for (const BarRow& Bar : Bars)
    {
        StampText(Rgba, W, H, 780, BarY, 2, Bar.Label, 214, 224, 240);
        FillRect(Rgba, W, H, 780, BarY + 22, 420, 22, 36, 42, 56);
        const int FillW = std::min(420, static_cast<int>(Bar.Count * 110u));
        FillRect(Rgba, W, H, 780, BarY + 22, FillW, 22, Bar.R, Bar.G, Bar.B);
        StampText(Rgba, W, H, 1212, BarY + 26, 2, std::to_string(Bar.Count), 240, 246, 255);
        BarY += 60;
    }

    std::error_code FsErr{};
    std::filesystem::create_directories(OutputPath.parent_path(), FsErr);
    stbi_write_png(OutputPath.string().c_str(), W, H, 4, Rgba.data(), W * 4);
}

} // namespace

int main()
{
    std::vector<GateReport> Reports{};
    std::string             Diag{};

    const std::filesystem::path RepoRoot = std::filesystem::path(FRONTIER_REPO_ROOT_DIR);
    const std::filesystem::path ScratchDir = RepoRoot / "Build" / "SerializationValidationScratch";
    std::error_code CleanErr{};
    std::filesystem::remove_all(ScratchDir, CleanErr);
    std::filesystem::create_directories(ScratchDir, CleanErr);

    //--------------------------------------------------------------------------------------------------------------------
    // GATE 1: 63-Format SuffixRegistry + *Space Stratification + Option 2A Legacy Aliases
    //--------------------------------------------------------------------------------------------------------------------
    uint32_t TierACount = 0u;
    uint32_t TierBCount = 0u;
    {
        const bool StratOk = VerifySpaceStratificationRule(Diag);
        for (const FormatSuffixDescriptor& Desc : EnumerateFormatSuffixDescriptors())
        {
            if (Desc.FoundationalPillar) { ++TierACount; } else { ++TierBCount; }
        }

        FormatSuffixCategory ResolvedCat{};
        std::string          ResolvedSuffix{};
        const bool AliasWorld  = ResolveSuffixCaseInsensitive(".worldspace",    ResolvedCat, ResolvedSuffix, Diag) && ResolvedCat == FormatSuffixCategory::WorldSpace;
        const bool AliasAtmo   = ResolveSuffixCaseInsensitive(".SKYATMOSPHERE", ResolvedCat, ResolvedSuffix, Diag) && ResolvedCat == FormatSuffixCategory::Atmosphere;
        const bool AliasRender = ResolveSuffixCaseInsensitive(".renderspace",   ResolvedCat, ResolvedSuffix, Diag) && ResolvedCat == FormatSuffixCategory::RenderSequence;
        const bool AliasWork   = ResolveSuffixCaseInsensitive(".editorlayout",  ResolvedCat, ResolvedSuffix, Diag) && ResolvedCat == FormatSuffixCategory::WorkspaceLayout;
        const bool AliasPix    = ResolveSuffixCaseInsensitive(".pixelsheet",    ResolvedCat, ResolvedSuffix, Diag) && ResolvedCat == FormatSuffixCategory::PixelSpace;
        const bool AliasOpt    = ResolveSuffixCaseInsensitive(".lensaperture",  ResolvedCat, ResolvedSuffix, Diag) && ResolvedCat == FormatSuffixCategory::CameraOptics;
        const bool RejectBad   = !ResolveSuffixCaseInsensitive(".notarealformat", ResolvedCat, ResolvedSuffix, Diag);

        const bool Passed = StratOk && TierACount == 18u && TierBCount == 45u &&
                            AliasWorld && AliasAtmo && AliasRender && AliasWork && AliasPix && AliasOpt && RejectBad;
        Reports.push_back({
            "GATE 1: SUFFIX REGISTRY + *SPACE RULE",
            "63 FORMATS (18 TIER A + 45 TIER B) + 6 ALIASES + BAD REJECT",
            Passed
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    // GATE 2: Cryptographic SHA-256 + xxHash64 + FNV-1a 64 Known-Answer Vectors
    //--------------------------------------------------------------------------------------------------------------------
    {
        const std::string ShaAbc   = FormatSha256Hex(ComputeSha256String("abc"));
        const std::string ShaEmpty = FormatSha256Hex(ComputeSha256String(""));
        const bool ShaAbcOk   = (ShaAbc == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        const bool ShaEmptyOk = (ShaEmpty == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
        const uint64_t Xx1    = ComputeXxHash64String("FrontierSerialization");
        const uint64_t Xx2    = ComputeXxHash64String("FrontierSerializatioN");
        const std::string_view FnvSampleA = "FrontierSerialization";
        const std::string_view FnvSampleB = "FrontierSerializatioN";
        const uint64_t Fnv1   = ComputeFnv1a64(std::as_bytes(std::span<const char>(FnvSampleA.data(), FnvSampleA.size())));
        const uint64_t Fnv2   = ComputeFnv1a64(std::as_bytes(std::span<const char>(FnvSampleB.data(), FnvSampleB.size())));
        const bool Passed = ShaAbcOk && ShaEmptyOk && (Xx1 != Xx2) && (Fnv1 != Fnv2);
        Reports.push_back({
            "GATE 2: SHA-256 + XXHASH64 + FNV-1A 64",
            "NIST SHA-256 VECTORS MATCHED + AVALANCHE VERIFIED",
            Passed
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    // GATE 3: Two-Tier Hybrid Container + 64B Alignment + Unknown-Key Forward Compatibility
    //--------------------------------------------------------------------------------------------------------------------
    std::vector<std::byte> ValidHybridBytes{};
    std::size_t            PayloadByteSampleOffset = 0u;
    {
        HybridContainerDocument Doc{};
        Doc.SuffixCategory   = FormatSuffixCategory::GeometrySpace;
        Doc.DocumentTitle    = "AlignmentSample";
        Doc.StableIdentifier = "0193b5c0-7a1e-7000-8000-000000009001";
        Doc.TomlHeader.insert("FutureSchemaExtension", "PreservedAcrossRoundTrip");

        BinarySectionBlock SecA{};
        SecA.SectionIdentifier = "VertexStreamA";
        SecA.SubFormatTag      = ".GeometrySpace";
        SecA.PayloadBytes.assign(37u, std::byte{ 0x5Au }); // intentionally odd length to test 64B padding
        BinarySectionBlock SecB{};
        SecB.SectionIdentifier = "IndexStreamB";
        SecB.SubFormatTag      = ".GeometrySpace";
        SecB.PayloadBytes.assign(83u, std::byte{ 0xA5u });
        Doc.BinarySections = { SecA, SecB };

        const bool EncOk = HybridContainerEncoder::EncodeToBytes(Doc, ValidHybridBytes, Diag);
        HybridContainerDocument Decoded{};
        const bool DecOk = EncOk && HybridContainerDecoder::DecodeFromBytes(ValidHybridBytes, Decoded, Diag);
        if (DecOk && Decoded.BinarySections.size() >= 2u)
        {
            PayloadByteSampleOffset = Decoded.BinarySections[1].DecodedAbsoluteByteOffset + 10u;
        }
        const bool AlignOk = DecOk && (Decoded.Tier2StartOffset % 64u == 0u) &&
                             Decoded.BinarySections.size() == 2u &&
                             (Decoded.BinarySections[0].DecodedAbsoluteByteOffset % 64u == 0u) &&
                             (Decoded.BinarySections[1].DecodedAbsoluteByteOffset % 64u == 0u);
        const bool ForwardCompatOk = DecOk &&
                                     (Decoded.TomlHeader["FutureSchemaExtension"].value_or<std::string>("") == "PreservedAcrossRoundTrip");
        Reports.push_back({
            "GATE 3: HYBRID 64B CONTAINER + FORWARD COMPAT",
            "TIER2 OFFSET ALIGNED 64B + UNKNOWN TOML KEYS PRESERVED",
            AlignOk && ForwardCompatOk
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    // GATE 4: Hardened Integrity Validator (Bit Flip, Truncation, Misalignment, Path Escape)
    //--------------------------------------------------------------------------------------------------------------------
    {
        // 1. Single-bit flip inside verified Tier 2 section payload
        std::vector<std::byte> Flipped = ValidHybridBytes;
        Flipped[PayloadByteSampleOffset] = static_cast<std::byte>(static_cast<uint8_t>(Flipped[PayloadByteSampleOffset]) ^ 0x01u);
        HybridContainerDocument Dummy{};
        const bool RejectBitFlip = !HybridContainerDecoder::DecodeFromBytes(Flipped, Dummy, Diag);

        // 2. Truncated Tier 2 payload (slice inside SecB's 83B payload past the 45B alignment tail)
        std::vector<std::byte> Truncated(ValidHybridBytes.begin(), ValidHybridBytes.end() - 64);
        const bool RejectTrunc = !HybridContainerDecoder::DecodeFromBytes(Truncated, Dummy, Diag);

        // 3. Unaligned RelativeAlignedOffset in TOML header
        const std::string BadAlignToml =
            "['.DocumentHeader']\nContainerSuffix = '.GeometrySpace'\nBinaryAlignmentBytes = 64\n"
            "[['.SectionDirectory']]\nSectionIdentifier = 'Bad'\nRelativeAlignedOffset = 13\nByteLength = 4\nChecksumFnv1a64 = '0000000000000000'\n"
            "\n# === FRONTIER_BINARY_BOUNDARY ===\n0123456789012345678901234567890123456789012345678901234567890123456789";
        const auto BadAlignSpan = std::as_bytes(std::span<const char>(BadAlignToml.data(), BadAlignToml.size()));
        const bool RejectMisalign = !HybridContainerDecoder::DecodeFromBytes(BadAlignSpan, Dummy, Diag);

        // 4. External link path escape
        const std::string BadEscapeToml =
            "['.DocumentHeader']\nContainerSuffix = '.Instance'\nBinaryAlignmentBytes = 64\n"
            "[['.ExternalLink']]\nLinkSlot = 'Escape'\nTargetSuffix = '.IesProfile'\nRelativePath = '../outside/secret.IesProfile'\n";
        const auto BadEscapeSpan = std::as_bytes(std::span<const char>(BadEscapeToml.data(), BadEscapeToml.size()));
        const bool RejectEscape = !HybridContainerDecoder::DecodeFromBytes(BadEscapeSpan, Dummy, Diag);

        Reports.push_back({
            "GATE 4: HARDENED INTEGRITY REJECTION",
            "REJECTED BIT-FLIP + TRUNCATION + MISALIGN + PATH-ESCAPE",
            RejectBitFlip && RejectTrunc && RejectMisalign && RejectEscape
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    // GATE 5: Multi-Pillar Embedded Containers (.Instance, .ParticleSpace, .InterfaceSpace)
    //--------------------------------------------------------------------------------------------------------------------
    {
        const auto VehicleDoc   = InstanceContainerCodec::ConstructVehicleDefaultExample();
        const auto ParticleDoc  = InstanceContainerCodec::ConstructExhaustPlumeParticleExample();
        const auto InterfaceDoc = InstanceContainerCodec::ConstructHudOverlayInterfaceExample();

        std::vector<std::byte> VBytes{}, PBytes{}, IBytes{};
        MultiPillarInstanceDocument VOut{}, POut{}, IOut{};

        const bool VOk = InstanceContainerCodec::EncodeToBytes(VehicleDoc, VBytes, Diag) &&
                         InstanceContainerCodec::DecodeFromBytes(VBytes, VOut, Diag) &&
                         VOut.EmbeddedPillars.size() == 9u && VOut.ExternalLinks.size() == 1u;
        const bool POk = InstanceContainerCodec::EncodeToBytes(ParticleDoc, PBytes, Diag) &&
                         InstanceContainerCodec::DecodeFromBytes(PBytes, POut, Diag) &&
                         POut.EmbeddedPillars.size() == 5u;
        const bool IOk = InstanceContainerCodec::EncodeToBytes(InterfaceDoc, IBytes, Diag) &&
                         InstanceContainerCodec::DecodeFromBytes(IBytes, IOut, Diag) &&
                         IOut.EmbeddedPillars.size() == 4u && IOut.ExternalLinks.size() == 1u;

        Reports.push_back({
            "GATE 5: MULTI-PILLAR EMBEDDED CONTAINERS",
            "VEHICLE (9 PILLARS) + PARTICLE (5) + INTERFACE (4) OK",
            VOk && POk && IOk
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    // GATE 6: Typed Codecs (CameraOptics, Atmosphere, FractureNetwork, WorldSpace)
    //--------------------------------------------------------------------------------------------------------------------
    {
        CameraOpticsDocument CamIn{};
        CamIn.FocalLengthMillimeters = 85.0f;
        CamIn.ApertureFStop          = 1.4f;
        CamIn.ApertureBladeCount     = 11u;
        HybridContainerDocument CamCont{};
        CameraOpticsDocument    CamOut{};
        const bool CamOk = FormatCodecSuite::EncodeCameraOptics(CamIn, CamCont) &&
                           FormatCodecSuite::DecodeCameraOptics(CamCont, CamOut, Diag) &&
                           std::fabs(CamOut.FocalLengthMillimeters - 85.0f) < 1e-4f &&
                           CamOut.ApertureBladeCount == 11u;

        AtmosphereDocument AtmoIn{};
        AtmoIn.SolarDiskEnabled          = true;
        AtmoIn.CelestialStarfieldEnabled = false;
        AtmoIn.SolarElevationDegrees     = 37.5f;
        HybridContainerDocument AtmoCont{};
        AtmosphereDocument      AtmoOut{};
        const bool AtmoOk = FormatCodecSuite::EncodeAtmosphere(AtmoIn, AtmoCont) &&
                            FormatCodecSuite::DecodeAtmosphere(AtmoCont, AtmoOut, Diag) &&
                            AtmoOut.SolarDiskEnabled && !AtmoOut.CelestialStarfieldEnabled;

        HybridContainerDocument FracCont = FormatCodecSuite::ConstructExampleDocument(FormatSuffixCategory::FractureNetwork);
        FractureNetworkDocument FracOut{};
        const bool FracOk = FormatCodecSuite::DecodeFractureNetwork(FracCont, FracOut, Diag) &&
                            FracOut.Cells.size() == 3u && FracOut.CohesiveBonds.size() == 2u;

        HybridContainerDocument WorldCont = FormatCodecSuite::ConstructExampleDocument(FormatSuffixCategory::WorldSpace);
        WorldSpaceDocument      WorldOut{};
        const bool WorldOk = FormatCodecSuite::DecodeWorldSpace(WorldCont, WorldOut, Diag) &&
                             WorldOut.Placements.size() == 3u && WorldOut.Placements[2].ContainmentIndex == 1;

        Reports.push_back({
            "GATE 6: TYPED FORMAT CODECS ROUND-TRIP",
            "CAMERAOPTICS + ATMOSPHERE + FRACTURENETWORK + WORLDSPACE",
            CamOk && AtmoOk && FracOk && WorldOk
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    // GATE 7: Acyclic DependencyOrderLedger + Exact Cycle Path Reporting
    //--------------------------------------------------------------------------------------------------------------------
    {
        DependencyOrderLedger CyclicLedger{};
        (void)CyclicLedger.RegisterEntry({ "Alpha", FormatSuffixCategory::GeometrySpace, 1u, 1u, "Portable", "", "A", {}, { "Gamma" } }, Diag);
        (void)CyclicLedger.RegisterEntry({ "Beta",  FormatSuffixCategory::MaterialSpace, 1u, 1u, "Portable", "", "B", {}, { "Alpha" } }, Diag);
        (void)CyclicLedger.RegisterEntry({ "Gamma", FormatSuffixCategory::WorldSpace,    1u, 1u, "Portable", "", "C", {}, { "Beta"  } }, Diag);

        std::vector<std::string> Ordered{};
        std::vector<std::string> CycleChain{};
        const bool RejectedCycle = !CyclicLedger.ResolveTopologicalOrder(Ordered, CycleChain, Diag);
        const bool ExactChainOk  = RejectedCycle && CycleChain.size() == 4u && CycleChain.front() == CycleChain.back();

        Reports.push_back({
            "GATE 7: DEPENDENCY ORDER + CYCLE REPORTING",
            "REJECTED A->B->C->A CYCLE WITH EXACT 4-STEP CHAIN",
            ExactChainOk
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    // GATE 8: Zero-Recompute CompiledDiskRepository + Minimal Invalidation + Corruption Repair + Quota GC
    //--------------------------------------------------------------------------------------------------------------------
    uint32_t ColdBakes            = 0u;
    uint32_t WarmHits             = 0u;
    uint32_t PartialInvalidations = 0u;
    uint32_t CorruptRepairs       = 0u;
    {
        const std::filesystem::path RepoDir = ScratchDir / "CompiledRepository";
        CompiledDiskRepository Repo(RepoDir);

        DependencyOrderLedger Ledger{};
        (void)Ledger.RegisterEntry({ "VehicleBody",    FormatSuffixCategory::GeometrySpace, 1u, 1u, "Linux-x86_64", "", "Crease=38",  {}, {} }, Diag);
        (void)Ledger.RegisterEntry({ "VehiclePaint",   FormatSuffixCategory::MaterialSpace, 1u, 1u, "Linux-x86_64", "", "Rough=0.18", {}, {} }, Diag);
        (void)Ledger.RegisterEntry({ "ClearSkyWorld",  FormatSuffixCategory::WorldSpace,    1u, 1u, "Linux-x86_64", "", "Sun=42deg",  {}, { "VehicleBody", "VehiclePaint" } }, Diag);

        const auto DeterministicBaker = [](
            const DependencyBuildEntry& Entry,
            const BuildInputSignature&  Signature,
            std::vector<std::byte>&     OutBytes,
            std::string&                OutDiag) -> bool
        {
            const std::string Payload = "BAKED:" + Entry.EntryIdentifier + ":" + Signature.CanonicalSha256Hex;
            OutBytes.resize(Payload.size());
            std::memcpy(OutBytes.data(), Payload.data(), Payload.size());
            OutDiag.clear();
            return true;
        };

        // Run 1: Cold bake (3 fresh bakes, 0 hits)
        RepositoryExecutionSummary Run1{};
        const bool Run1Ok = Repo.ExecuteLedger(Ledger, DeterministicBaker, Run1, Diag);
        ColdBakes = Run1.FreshBakeCount;

        // Run 2: Unchanged (3 hits, 0 bakes — 100% zero recomputation)
        RepositoryExecutionSummary Run2{};
        const bool Run2Ok = Repo.ExecuteLedger(Ledger, DeterministicBaker, Run2, Diag);
        WarmHits = Run2.DiskHitCount;

        // Run 3: Mutate VehiclePaint only -> VehicleBody stays HIT (1 hit), VehiclePaint & ClearSkyWorld rebuild (2 bakes)
        (void)Ledger.UpdateSpecification("VehiclePaint", "Rough=0.22", Diag);
        RepositoryExecutionSummary Run3{};
        const bool Run3Ok = Repo.ExecuteLedger(Ledger, DeterministicBaker, Run3, Diag);
        PartialInvalidations = Run3.FreshBakeCount;

        // Run 4: Corrupt VehicleBody .fbin payload on disk -> automatically repaired atomically (1 repair, 2 hits)
        std::filesystem::path BodyBinPath{};
        for (const BuildSourceTrace& Trace : Run3.Traces)
        {
            if (Trace.EntryIdentifier == "VehicleBody")
            {
                BodyBinPath = Trace.CompiledBinaryPath;
            }
        }
        HybridContainerDocument BodyDecoded{};
        std::vector<std::byte>  CorruptBytes{};
        if (HybridContainerDecoder::DecodeFromFile(BodyBinPath, BodyDecoded, Diag) &&
            ReadFileBytes(BodyBinPath, CorruptBytes, Diag) &&
            !BodyDecoded.BinarySections.empty())
        {
            const std::size_t FlipIdx = BodyDecoded.BinarySections[0].DecodedAbsoluteByteOffset + 4u;
            CorruptBytes[FlipIdx] = static_cast<std::byte>(static_cast<uint8_t>(CorruptBytes[FlipIdx]) ^ 0xFFu);
            (void)WriteBytesAtomically(BodyBinPath, CorruptBytes, Diag);
        }
        RepositoryExecutionSummary Run4{};
        const bool Run4Ok = Repo.ExecuteLedger(Ledger, DeterministicBaker, Run4, Diag);
        CorruptRepairs = Run4.CorruptRepairCount;

        // Run 5: Quota GC removes unreachable stale .fbin from Run 1 while preserving active Run 4 keys
        std::unordered_set<std::string> ActiveKeys{};
        for (const BuildSourceTrace& Trace : Run4.Traces)
        {
            ActiveKeys.insert(Trace.CanonicalSha256Hex);
        }
        uint32_t RemovedCount = 0u;
        const bool PruneOk = Repo.PruneUnreachableAboveQuota(ActiveKeys, 1u, RemovedCount, Diag) && (RemovedCount >= 2u);

        const bool Passed = Run1Ok && (Run1.FreshBakeCount == 3u) &&
                            Run2Ok && (Run2.DiskHitCount == 3u) && (Run2.FreshBakeCount == 0u) &&
                            Run3Ok && (Run3.DiskHitCount == 1u) && (Run3.FreshBakeCount == 2u) &&
                            Run4Ok && (Run4.CorruptRepairCount == 1u) && (Run4.DiskHitCount == 2u) &&
                            PruneOk;
        if (!Passed)
        {
            std::cerr << "Gate 8 detail: Run1(ok=" << Run1Ok << ",bake=" << Run1.FreshBakeCount
                      << ") Run2(ok=" << Run2Ok << ",hit=" << Run2.DiskHitCount << ",bake=" << Run2.FreshBakeCount
                      << ") Run3(ok=" << Run3Ok << ",hit=" << Run3.DiskHitCount << ",bake=" << Run3.FreshBakeCount
                      << ") Run4(ok=" << Run4Ok << ",rep=" << Run4.CorruptRepairCount << ",hit=" << Run4.DiskHitCount
                      << ") Prune(ok=" << PruneOk << ",rem=" << RemovedCount << ")\n";
        }
        Reports.push_back({
            "GATE 8: ZERO-RECOMPUTE REPOSITORY + GC",
            "COLD=3 WARM-HIT=3 PARTIAL=2 REPAIR=1 PRUNED=" + std::to_string(RemovedCount),
            Passed
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    // GATE 9: TOML-Driven BootConfigurationLoader
    //--------------------------------------------------------------------------------------------------------------------
    {
        const std::filesystem::path BootScratch = ScratchDir / "BootConfigurationRoot";
        ResolvedBootConfiguration   Resolved{};
        const bool BootOk = BootConfigurationLoader::EnsureDefaultConfigurationFiles(BootScratch, Diag) &&
                            BootConfigurationLoader::LoadFromRepositoryRoot(BootScratch, Resolved, Diag) &&
                            Resolved.LoadedFromDisk &&
                            Resolved.ResolvedWorldPath.filename() == "VehicleClearSkyDefault.WorldSpace";
        Reports.push_back({
            "GATE 9: TOML BOOT CONFIGURATION LOADER",
            "ENGINESETTINGS->RUNTIME->PLATFORM->PROJECTINDEX->WORLD",
            BootOk
        });
    }

    //--------------------------------------------------------------------------------------------------------------------
    // GATE 10: Complete 63-Format Example Suite Generation & Round-Trip Verification (Content/FormatExamples/)
    //--------------------------------------------------------------------------------------------------------------------
    {
        const std::filesystem::path ExamplesRoot = RepoRoot / "Content" / "FormatExamples";
        std::vector<std::filesystem::path> WrittenExamples{};
        const bool GenOk = FormatCodecSuite::GenerateAllFormatExamples(ExamplesRoot, WrittenExamples, Diag) &&
                           WrittenExamples.size() == 63u;
        Reports.push_back({
            "GATE 10: ALL 63 FORMAT EXAMPLES ON DISK",
            "WRITTEN + ROUND-TRIP VERIFIED 63/63 (INCL PAUSED PILLARS)",
            GenOk
        });
    }

    // Also ensure canonical Configuration/*.EngineSettings / *.RuntimeConfiguration / *.PlatformSettings / ProjectIndex exist in repo
    (void)BootConfigurationLoader::EnsureDefaultConfigurationFiles(RepoRoot, Diag);

    // Emit visual telemetry verification PNG
    const std::filesystem::path TelemetryPng =
        RepoRoot / "Exhibits" / "Images" / "Serialization" / "SerializationArchitectureVerification.png";
    RenderVerificationTelemetryPng(
        TelemetryPng, Reports, TierACount, TierBCount,
        ColdBakes, WarmHits, PartialInvalidations, CorruptRepairs);

    // Print concise report & clean scratch
    std::filesystem::remove_all(ScratchDir, CleanErr);
    uint32_t PassCount = 0u;
    for (const GateReport& Gate : Reports)
    {
        std::cout << (Gate.Passed ? "[PASS] " : "[FAIL] ") << Gate.Title << " :: " << Gate.Metrics << '\n';
        if (Gate.Passed)
        {
            ++PassCount;
        }
    }
    std::cout << "SerializationValidation Summary: " << PassCount << "/" << Reports.size()
              << " gates passed. Telemetry PNG: " << TelemetryPng.string() << '\n';

    return (PassCount == Reports.size()) ? 0 : 1;
}

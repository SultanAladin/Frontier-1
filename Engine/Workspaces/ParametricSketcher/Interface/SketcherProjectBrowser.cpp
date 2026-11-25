/*====================================================================================================================================
                                                    SKETCHERPROJECTBROWSER.CPP
====================================================================================================================================*/
// 🧩 Native in-app SolidArc Project Browser with effortless PC filesystem navigation (no OS popup), strict .solidarc + folder
//    filtering, folder project CAD previews, and real-geometry MatCap + B&W CAD drafting rasterizer

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherProjectBrowser.h"

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherMatcap.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherScene.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherInterface.h"
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherStyleSheet.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace Frontier::Workspaces::Sketcher
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS & HELPERS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t TileThumbWidthPx    = 360u;
constexpr uint32_t TileThumbHeightPx   = 220u;
constexpr uint32_t ReaderViewWidthPx   = 520u;
constexpr uint32_t ReaderViewHeightPx  = 324u;
constexpr uint32_t FolderScanDepthCap  = 2u;
constexpr uint32_t FolderScanEntryCap  = 256u;
constexpr float    PiConstant          = 3.14159265358979323846f;

struct Vec3f
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

[[nodiscard]] inline Vec3f operator+(Vec3f A, Vec3f B) noexcept { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
[[nodiscard]] inline Vec3f operator-(Vec3f A, Vec3f B) noexcept { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
[[nodiscard]] inline Vec3f operator*(Vec3f A, float S) noexcept { return { A.X * S, A.Y * S, A.Z * S }; }
[[nodiscard]] inline float Dot3(Vec3f A, Vec3f B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
[[nodiscard]] inline Vec3f Cross3(Vec3f A, Vec3f B) noexcept
{
    return {
        A.Y * B.Z - A.Z * B.Y,
        A.Z * B.X - A.X * B.Z,
        A.X * B.Y - A.Y * B.X,
    };
}
[[nodiscard]] inline Vec3f Normalize3(Vec3f V) noexcept
{
    const float Len = std::sqrt(Dot3(V, V));
    return Len > 1.0e-7f ? Vec3f{ V.X / Len, V.Y / Len, V.Z / Len } : Vec3f{ 0.0f, 0.0f, 1.0f };
}

[[nodiscard]] std::string LowercaseAscii(std::string Text)
{
    for (char& Ch : Text)
    {
        Ch = static_cast<char>(std::tolower(static_cast<unsigned char>(Ch)));
    }
    return Text;
}

[[nodiscard]] bool MatchesSolidArcSuffix(const std::filesystem::path& Path)
{
    return LowercaseAscii(Path.extension().string()) == ".solidarc";
}

[[nodiscard]] std::string FormatByteSize(uint64_t Bytes)
{
    char Buf[48]{};
    if (Bytes >= 1024ull * 1024ull * 1024ull)
        std::snprintf(Buf, sizeof(Buf), "%.2f GB", double(Bytes) / (1024.0 * 1024.0 * 1024.0));
    else if (Bytes >= 1024ull * 1024ull)
        std::snprintf(Buf, sizeof(Buf), "%.2f MB", double(Bytes) / (1024.0 * 1024.0));
    else if (Bytes >= 1024ull)
        std::snprintf(Buf, sizeof(Buf), "%.1f KB", double(Bytes) / 1024.0);
    else
        std::snprintf(Buf, sizeof(Buf), "%llu B", static_cast<unsigned long long>(Bytes));
    return Buf;
}

[[nodiscard]] std::string FormatWriteTime(const std::filesystem::path& Path, int64_t* OutEpochSec = nullptr)
{
    std::error_code Ec {};
    const auto Wt = std::filesystem::last_write_time(Path, Ec);
    if (Ec) return "";
    const auto SysNow  = std::chrono::system_clock::now();
    const auto FileNow = std::filesystem::file_time_type::clock::now();
    const auto SysTp   = std::chrono::time_point_cast<std::chrono::system_clock::duration>(Wt - FileNow + SysNow);
    const std::time_t RawTime = std::chrono::system_clock::to_time_t(SysTp);
    if (OutEpochSec != nullptr) *OutEpochSec = static_cast<int64_t>(RawTime);
    std::tm LocalTm {};
#ifdef _WIN32
    localtime_s(&LocalTm, &RawTime);
#else
    localtime_r(&RawTime, &LocalTm);
#endif
    char Buf[48]{};
    std::snprintf(Buf, sizeof(Buf), "%04d-%02d-%02d %02d:%02d",
                  LocalTm.tm_year + 1900, LocalTm.tm_mon + 1, LocalTm.tm_mday,
                  LocalTm.tm_hour, LocalTm.tm_min);
    return Buf;
}

[[nodiscard]] std::filesystem::path NormalizeDirectoryPath(const std::filesystem::path& Candidate)
{
    if (Candidate.empty()) return {};
    std::error_code Ec {};
    std::filesystem::path Clean = Candidate;
    if (std::filesystem::is_regular_file(Clean, Ec))
    {
        Clean = Clean.parent_path();
    }
    const std::filesystem::path Abs = std::filesystem::weakly_canonical(Clean, Ec);
    if (!Ec && std::filesystem::is_directory(Abs, Ec))
    {
        return Abs;
    }
    if (std::filesystem::is_directory(Clean, Ec))
    {
        return Clean.lexically_normal();
    }
    return {};
}

[[nodiscard]] std::filesystem::path DetectSolidArcLibraryRoot(const SketcherWorkspaceStatus& Workspace)
{
    namespace Fs = std::filesystem;
    std::error_code Ec {};
    const std::vector<Fs::path> Candidates = {
        Fs::current_path(Ec) / "Content" / "SolidArc",
        Fs::current_path(Ec) / "Content" / "SolidArc" / "Vehicles",
        Fs::path(Workspace.Options.DocumentPath).parent_path(),
        Fs::current_path(Ec),
    };
    for (const Fs::path& Dir : Candidates)
    {
        if (!Dir.empty() && Fs::is_directory(Dir, Ec))
        {
            return Fs::weakly_canonical(Dir, Ec);
        }
    }
    return Fs::current_path(Ec);
}

[[nodiscard]] std::filesystem::path QueryUserHomeDirectory()
{
#ifdef _WIN32
    if (const char* UserProfile = std::getenv("USERPROFILE"); UserProfile && *UserProfile)
        return std::filesystem::path(UserProfile);
    const char* Drive = std::getenv("HOMEDRIVE");
    const char* Path  = std::getenv("HOMEPATH");
    if (Drive && Path && *Drive && *Path)
        return std::filesystem::path(std::string(Drive) + std::string(Path));
#else
    if (const char* Home = std::getenv("HOME"); Home && *Home)
        return std::filesystem::path(Home);
#endif
    std::error_code Ec {};
    return std::filesystem::current_path(Ec);
}

void UploadRgbaToImTexture(ImTextureData& Tex, const uint8_t* Rgba, uint32_t WidthPx, uint32_t HeightPx)
{
    if (Rgba == nullptr || WidthPx == 0u || HeightPx == 0u) return;
    const bool NeedsCreate = (Tex.Pixels == nullptr) || (Tex.Width != int(WidthPx)) || (Tex.Height != int(HeightPx));
    if (NeedsCreate)
    {
        Tex.DestroyPixels();
        Tex.SetStatus(ImTextureStatus_Destroyed);
        Tex.Create(ImTextureFormat_RGBA32, int(WidthPx), int(HeightPx));
    }
    if (!Tex.Pixels) return;
    std::memcpy(Tex.GetPixels(), Rgba, size_t(WidthPx) * size_t(HeightPx) * 4u);
    Tex.UseColors = true;
    if (NeedsCreate)
    {
        Tex.SetStatus(ImTextureStatus_WantCreate);
    }
    else
    {
        Tex.UpdateRect = { 0u, 0u, static_cast<unsigned short>(WidthPx), static_cast<unsigned short>(HeightPx) };
        Tex.SetStatus(ImTextureStatus_WantUpdates);
    }
}

void ReleaseImTexture(std::unique_ptr<ImTextureData>& Tex) noexcept
{
    if (Tex != nullptr)
    {
        Tex->DestroyPixels();
        Tex->SetStatus(ImTextureStatus_Destroyed);
        Tex.reset();
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           CAD PREVIEW GEOMETRY & RASTERIZER
//------------------------------------------------------------------------------------------------------------------------

struct CadPreviewTriangle
{
    Vec3f    V0   = {};
    Vec3f    V1   = {};
    Vec3f    V2   = {};
    Vec3f    N0   = { 0.0f, 0.0f, 1.0f };
    Vec3f    N1   = { 0.0f, 0.0f, 1.0f };
    Vec3f    N2   = { 0.0f, 0.0f, 1.0f };
    uint16_t Part = 0u;
};

struct CadPreviewSegment
{
    Vec3f Start = {};
    Vec3f End   = {};
    bool  AccentSketch = false;
};

struct CadPreviewGeometry
{
    std::string                    Title          = {};
    std::string                    Description    = {};
    std::vector<CadPreviewTriangle> Triangles     = {};
    std::vector<CadPreviewSegment>  Creases       = {};
    Vec3f                          Center         = {};
    float                          Radius         = 1.0f;
    uint32_t                       BodyCount      = 0u;
    uint32_t                       StepCount      = 0u;
    uint32_t                       ReferenceCount = 0u;
    uint32_t                       PartCount      = 0u;
    uint64_t                       FileSizeBytes  = 0u;
    int64_t                        ModifiedEpoch  = 0;
    bool                           ValidCondition = false;
};

[[nodiscard]] const SketcherPicture& SharedStudioMatcap()
{
    static const SketcherPicture Matcap = ComposeSketcherStudioMatcap(SketcherMatcapExtent);
    return Matcap;
}

[[nodiscard]] inline std::array<float, 3> SampleStudioMatcap(Vec3f ViewNormal) noexcept
{
    const SketcherPicture& Pic = SharedStudioMatcap();
    if (Pic.Width == 0u || Pic.Height == 0u || Pic.Pixels.empty())
    {
        const float Shade = std::clamp(0.28f + 0.58f * std::max(0.0f, ViewNormal.Z) + 0.14f * std::max(0.0f, ViewNormal.Y), 0.0f, 1.0f);
        return { Shade, Shade, Shade };
    }
    const float U = std::clamp(0.5f + 0.49f * ViewNormal.X, 0.0f, 1.0f) * float(Pic.Width - 1u);
    const float V = std::clamp(0.5f - 0.49f * ViewNormal.Y, 0.0f, 1.0f) * float(Pic.Height - 1u);
    const uint32_t X0 = static_cast<uint32_t>(U);
    const uint32_t Y0 = static_cast<uint32_t>(V);
    const uint32_t X1 = std::min(X0 + 1u, Pic.Width - 1u);
    const uint32_t Y1 = std::min(Y0 + 1u, Pic.Height - 1u);
    const float Fx = U - float(X0);
    const float Fy = V - float(Y0);

    auto Fetch = [&](uint32_t X, uint32_t Y, uint32_t C) noexcept -> float
    {
        return float(Pic.Pixels[(size_t(Y) * Pic.Width + X) * 4u + C]) * (1.0f / 255.0f);
    };

    std::array<float, 3> Out {};
    for (uint32_t C = 0u; C < 3u; ++C)
    {
        const float Top = Fetch(X0, Y0, C) * (1.0f - Fx) + Fetch(X1, Y0, C) * Fx;
        const float Bot = Fetch(X0, Y1, C) * (1.0f - Fx) + Fetch(X1, Y1, C) * Fx;
        Out[C] = Top * (1.0f - Fy) + Bot * Fy;
    }
    return Out;
}

[[nodiscard]] CadPreviewGeometry ExtractCadPreviewGeometry(const std::filesystem::path& FilePath)
{
    CadPreviewGeometry Out {};
    std::error_code Ec {};
    Out.FileSizeBytes = std::filesystem::file_size(FilePath, Ec);
    (void)FormatWriteTime(FilePath, &Out.ModifiedEpoch);

    const SketcherDocumentFile Loaded = LoadSketcherDocumentFile(FilePath.string());
    if (!Loaded.Refusal.empty())
    {
        return Out;
    }

    Out.Title          = Loaded.Title.empty() ? FilePath.stem().string() : Loaded.Title;
    Out.StepCount      = static_cast<uint32_t>(Loaded.Tree.Steps.size());
    Out.ReferenceCount = static_cast<uint32_t>(Loaded.ReferenceSurfaces.size());

    Vec3f MinPt {  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max(),  std::numeric_limits<float>::max() };
    Vec3f MaxPt { -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max(), -std::numeric_limits<float>::max() };
    bool  AnyBounds = false;
    auto GrowBounds = [&](Vec3f P) noexcept
    {
        if (!std::isfinite(P.X) || !std::isfinite(P.Y) || !std::isfinite(P.Z)) return;
        MinPt.X = std::min(MinPt.X, P.X);
        MinPt.Y = std::min(MinPt.Y, P.Y);
        MinPt.Z = std::min(MinPt.Z, P.Z);
        MaxPt.X = std::max(MaxPt.X, P.X);
        MaxPt.Y = std::max(MaxPt.Y, P.Y);
        MaxPt.Z = std::max(MaxPt.Z, P.Z);
        AnyBounds = true;
    };

    // 1. Embedded reference surfaces (high-poly vehicles / retopology assemblies)
    for (size_t RefIdx = 0u; RefIdx < Loaded.ReferenceSurfaces.size(); ++RefIdx)
    {
        const SketcherReferenceSurface& Ref = Loaded.ReferenceSurfaces[RefIdx];
        if (!Ref.VisibleCondition || !Ref.Geometry) continue;
        Out.PartCount += static_cast<uint32_t>(Ref.Parts.size());
        if (Out.Description.empty() && !Ref.SummaryNote.empty())
            Out.Description = Ref.SummaryNote;

        const auto& Corners = Ref.Geometry->Corners;
        const size_t TriCount = Corners.size() / 3u;
        Out.Triangles.reserve(Out.Triangles.size() + TriCount);
        for (size_t T = 0u; T < TriCount; ++T)
        {
            const SketcherReferenceCorner& C0 = Corners[T * 3u + 0u];
            const SketcherReferenceCorner& C1 = Corners[T * 3u + 1u];
            const SketcherReferenceCorner& C2 = Corners[T * 3u + 2u];
            CadPreviewTriangle Tri {};
            Tri.V0   = { C0.Position[0], C0.Position[1], C0.Position[2] };
            Tri.V1   = { C1.Position[0], C1.Position[1], C1.Position[2] };
            Tri.V2   = { C2.Position[0], C2.Position[1], C2.Position[2] };
            Tri.N0   = Normalize3({ C0.Normal[0], C0.Normal[1], C0.Normal[2] });
            Tri.N1   = Normalize3({ C1.Normal[0], C1.Normal[1], C1.Normal[2] });
            Tri.N2   = Normalize3({ C2.Normal[0], C2.Normal[1], C2.Normal[2] });
            Tri.Part = static_cast<uint16_t>((C0.PartIndex + 1u + RefIdx * 64u) & 0xFFFFu);
            GrowBounds(Tri.V0);
            GrowBounds(Tri.V1);
            GrowBounds(Tri.V2);
            Out.Triangles.push_back(Tri);
        }

        const auto& Segs = Ref.Geometry->CreaseSegments;
        const size_t SegStride = Segs.size() > 70000u ? (Segs.size() / 70000u) : 1u;
        Out.Creases.reserve(Out.Creases.size() + Segs.size() / SegStride);
        for (size_t S = 0u; S < Segs.size(); S += SegStride)
        {
            const SketcherReferenceSegment& Seg = Segs[S];
            Out.Creases.push_back({
                { Seg.Start[0], Seg.Start[1], Seg.Start[2] },
                { Seg.End[0],   Seg.End[1],   Seg.End[2]   },
                false,
            });
        }
    }

    // 2. Parametric B-rep solids & authored 2D/3D sketches
    if (!Loaded.Tree.Steps.empty())
    {
        SketcherDocument Doc = CreateSketcherDocument(Out.Title);
        Doc.Tree       = Loaded.Tree;
        Doc.Captions   = Loaded.Captions;
        Doc.Appearance = Loaded.Appearance;
        RegenerateSketcherDocument(Doc, true);
        const SketcherScene Scene = BuildSketcherScene(Doc, 3.0e-3, false);
        Out.BodyCount = static_cast<uint32_t>(Scene.Bodies.size());

        const size_t SceneTriCount = Scene.Corners.size() / 3u;
        Out.Triangles.reserve(Out.Triangles.size() + SceneTriCount);
        for (size_t T = 0u; T < SceneTriCount; ++T)
        {
            const SketcherSceneCorner& C0 = Scene.Corners[T * 3u + 0u];
            const SketcherSceneCorner& C1 = Scene.Corners[T * 3u + 1u];
            const SketcherSceneCorner& C2 = Scene.Corners[T * 3u + 2u];
            CadPreviewTriangle Tri {};
            Tri.V0   = { C0.Position[0], C0.Position[1], C0.Position[2] };
            Tri.V1   = { C1.Position[0], C1.Position[1], C1.Position[2] };
            Tri.V2   = { C2.Position[0], C2.Position[1], C2.Position[2] };
            Tri.N0   = Normalize3({ C0.Normal[0], C0.Normal[1], C0.Normal[2] });
            Tri.N1   = Normalize3({ C1.Normal[0], C1.Normal[1], C1.Normal[2] });
            Tri.N2   = Normalize3({ C2.Normal[0], C2.Normal[1], C2.Normal[2] });
            Tri.Part = static_cast<uint16_t>((C0.Face + 1000u) & 0xFFFFu);
            GrowBounds(Tri.V0);
            GrowBounds(Tri.V1);
            GrowBounds(Tri.V2);
            Out.Triangles.push_back(Tri);
        }

        Out.Creases.reserve(Out.Creases.size() + Scene.Segments.size());
        for (const SketcherSceneSegment& Seg : Scene.Segments)
        {
            const Vec3f A { Seg.Start[0], Seg.Start[1], Seg.Start[2] };
            const Vec3f B { Seg.End[0],   Seg.End[1],   Seg.End[2]   };
            GrowBounds(A);
            GrowBounds(B);
            Out.Creases.push_back({ A, B, true });
        }

        // Also include freestanding 3D space curves & 2D sketch polylines (when a .solidarc holds cage curves without solids)
        for (const auto& Step : Doc.Tree.Steps)
        {
            if (Step.Kind == GeometrySpace::Parametric::FeatureStepCategory::SpaceCurve && Step.Points.size() >= 2u)
            {
                for (size_t K = 1u; K < Step.Points.size(); ++K)
                {
                    const Vec3f A { float(Step.Points[K - 1u].X), float(Step.Points[K - 1u].Y), float(Step.Points[K - 1u].Z) };
                    const Vec3f B { float(Step.Points[K].X),      float(Step.Points[K].Y),      float(Step.Points[K].Z) };
                    GrowBounds(A);
                    GrowBounds(B);
                    Out.Creases.push_back({ A, B, true });
                }
                if (Step.ClosedCondition && Step.Points.size() >= 3u)
                {
                    const Vec3f A { float(Step.Points.back().X),  float(Step.Points.back().Y),  float(Step.Points.back().Z) };
                    const Vec3f B { float(Step.Points.front().X), float(Step.Points.front().Y), float(Step.Points.front().Z) };
                    Out.Creases.push_back({ A, B, true });
                }
            }
            else if (Step.Kind == GeometrySpace::Parametric::FeatureStepCategory::Sketch && Step.Sketch.Points.size() >= 2u)
            {
                const auto& Plane = Step.Plane;
                auto ToWorld = [&](double U, double V) noexcept -> Vec3f
                {
                    return {
                        float(Plane.Origin.X + Plane.XAxis.X * U + Plane.YAxis.X * V),
                        float(Plane.Origin.Y + Plane.XAxis.Y * U + Plane.YAxis.Y * V),
                        float(Plane.Origin.Z + Plane.XAxis.Z * U + Plane.YAxis.Z * V),
                    };
                };
                for (size_t K = 1u; K < Step.Sketch.Points.size(); ++K)
                {
                    const Vec3f A = ToWorld(Step.Sketch.Points[K - 1u].Position.X, Step.Sketch.Points[K - 1u].Position.Y);
                    const Vec3f B = ToWorld(Step.Sketch.Points[K].Position.X,      Step.Sketch.Points[K].Position.Y);
                    GrowBounds(A);
                    GrowBounds(B);
                    Out.Creases.push_back({ A, B, true });
                }
            }
        }
    }

    if (AnyBounds)
    {
        Out.Center = (MinPt + MaxPt) * 0.5f;
        const Vec3f Half = (MaxPt - MinPt) * 0.5f;
        Out.Radius = std::max(1.0e-4f, std::sqrt(Dot3(Half, Half)));
        Out.ValidCondition = !Out.Triangles.empty() || !Out.Creases.empty();
    }

    if (Out.Description.empty())
    {
        char Buf[160]{};
        if (Out.BodyCount > 0u && Out.ReferenceCount > 0u)
            std::snprintf(Buf, sizeof(Buf), "%u bodies · %u references · %u steps", Out.BodyCount, Out.ReferenceCount, Out.StepCount);
        else if (Out.BodyCount > 0u)
            std::snprintf(Buf, sizeof(Buf), "%u B-rep %s · %u steps · %zu tris", Out.BodyCount, Out.BodyCount == 1u ? "body" : "bodies", Out.StepCount, Out.Triangles.size());
        else if (Out.ReferenceCount > 0u)
            std::snprintf(Buf, sizeof(Buf), "%u reference %s · %zu tris", Out.ReferenceCount, Out.ReferenceCount == 1u ? "assembly" : "assemblies", Out.Triangles.size());
        else
            std::snprintf(Buf, sizeof(Buf), "%u parametric %s", Out.StepCount, Out.StepCount == 1u ? "step" : "steps");
        Out.Description = Buf;
    }

    return Out;
}

void BlendPixel(std::vector<uint8_t>& Rgba, uint32_t W, uint32_t H, int X, int Y, uint8_t R, uint8_t G, uint8_t B, float Alpha) noexcept
{
    if (X < 0 || Y < 0 || X >= int(W) || Y >= int(H) || Alpha <= 0.0f) return;
    const float A = std::clamp(Alpha, 0.0f, 1.0f);
    uint8_t* Dst = Rgba.data() + (size_t(Y) * W + size_t(X)) * 4u;
    Dst[0] = static_cast<uint8_t>(std::clamp(float(Dst[0]) * (1.0f - A) + float(R) * A, 0.0f, 255.0f));
    Dst[1] = static_cast<uint8_t>(std::clamp(float(Dst[1]) * (1.0f - A) + float(G) * A, 0.0f, 255.0f));
    Dst[2] = static_cast<uint8_t>(std::clamp(float(Dst[2]) * (1.0f - A) + float(B) * A, 0.0f, 255.0f));
    Dst[3] = 255u;
}

void RasterizeCadPreview(
    const CadPreviewGeometry& Geom,
    SketcherCadPreviewStyle   Style,
    uint32_t                  W,
    uint32_t                  H,
    bool                      HiddenEdgesCondition,
    bool                      OrthographicCondition,
    float                     YawRadians,
    float                     PitchRadians,
    float                     ZoomFactor,
    std::vector<uint8_t>&     OutRgba)
{
    OutRgba.assign(size_t(W) * size_t(H) * 4u, 255u);
    const bool PureBw       = (Style == SketcherCadPreviewStyle::DraftingBw);
    const bool ContourLayer = (Style != SketcherCadPreviewStyle::MatcapStudio);

    // 1. Studio / CAD drafting backdrop
    for (uint32_t Y = 0u; Y < H; ++Y)
    {
        const float Fy = float(Y) / float(std::max(1u, H - 1u));
        for (uint32_t X = 0u; X < W; ++X)
        {
            const float Fx = float(X) / float(std::max(1u, W - 1u));
            const float Dx = Fx - 0.5f;
            const float Dy = Fy - 0.46f;
            const float Vignette = std::clamp(1.0f - (Dx * Dx + Dy * Dy) * 1.15f, 0.0f, 1.0f);
            uint8_t R = 0u, G = 0u, B = 0u;
            if (PureBw)
            {
                const float Base = 7.0f + 7.0f * Vignette;
                R = static_cast<uint8_t>(Base);
                G = static_cast<uint8_t>(Base + 1.0f);
                B = static_cast<uint8_t>(Base + 2.5f);
            }
            else
            {
                const float Base = 12.0f + 16.0f * Vignette;
                R = static_cast<uint8_t>(Base);
                G = static_cast<uint8_t>(Base + 1.5f);
                B = static_cast<uint8_t>(Base + 4.0f);
            }
            uint8_t* Dst = OutRgba.data() + (size_t(Y) * W + X) * 4u;
            Dst[0] = R; Dst[1] = G; Dst[2] = B; Dst[3] = 255u;
        }
    }

    // 2. Camera basis (Z-up right-handed, normalized bounding sphere radius = 1)
    const float Pitch = std::clamp(PitchRadians, -1.48f, 1.48f);
    const float Dist  = std::clamp(2.38f / std::max(0.25f, ZoomFactor), 1.15f, 9.0f);
    const Vec3f Eye {
        std::cos(Pitch) * std::cos(YawRadians) * Dist,
        std::cos(Pitch) * std::sin(YawRadians) * Dist,
        std::sin(Pitch) * Dist,
    };
    const Vec3f Forward = Normalize3(Eye * -1.0f);
    Vec3f Right = Cross3(Forward, { 0.0f, 0.0f, 1.0f });
    if (Dot3(Right, Right) < 1.0e-5f) Right = { 1.0f, 0.0f, 0.0f };
    else Right = Normalize3(Right);
    const Vec3f Up = Normalize3(Cross3(Right, Forward));

    const float Aspect    = float(W) / float(std::max(1u, H));
    const float Focal     = 2.65f;
    const float OrthoSpan = Dist * 0.42f;
    const float InvRadius = 1.0f / std::max(1.0e-4f, Geom.Radius);

    auto ProjectNormalized = [&](Vec3f NormPos, float& OutSx, float& OutSy, float& OutDepth) noexcept -> bool
    {
        const Vec3f Rel = NormPos - Eye;
        const float CamX = Dot3(Rel, Right);
        const float CamY = Dot3(Rel, Up);
        const float CamZ = Dot3(Rel, Forward);
        if (CamZ <= 0.05f) return false;
        float NdcX = 0.0f, NdcY = 0.0f;
        if (OrthographicCondition)
        {
            NdcX = CamX / (OrthoSpan * Aspect);
            NdcY = CamY / OrthoSpan;
        }
        else
        {
            NdcX = (CamX * Focal) / (CamZ * Aspect);
            NdcY = (CamY * Focal) / CamZ;
        }
        OutSx    = (NdcX * 0.5f + 0.5f) * float(W);
        OutSy    = (0.5f - NdcY * 0.5f) * float(H);
        OutDepth = CamZ;
        return true;
    };

    auto DrawScreenSegment = [&](float X0, float Y0, float Z0, float X1, float Y1, float Z1,
                                 const std::vector<float>* ZBuf, bool TestDepth, bool HiddenLayer,
                                 uint8_t R, uint8_t G, uint8_t B, float Alpha)
    {
        const float Dx = X1 - X0;
        const float Dy = Y1 - Y0;
        const float Steps = std::max(std::fabs(Dx), std::fabs(Dy));
        if (Steps < 0.25f) return;
        const int Count = std::min(int(std::ceil(Steps)), int(W + H));
        const float InvN = 1.0f / float(std::max(1, Count));
        const float DepthBias = 0.022f;
        for (int I = 0; I <= Count; ++I)
        {
            const float T = float(I) * InvN;
            const int Px = int(std::lround(X0 + Dx * T));
            const int Py = int(std::lround(Y0 + Dy * T));
            if (Px < 0 || Py < 0 || Px >= int(W) || Py >= int(H)) continue;
            if (TestDepth && ZBuf != nullptr)
            {
                const float Z = Z0 + (Z1 - Z0) * T;
                const float Stored = (*ZBuf)[size_t(Py) * W + size_t(Px)];
                const bool Behind = (Stored < 1.0e18f) && (Z > Stored + DepthBias);
                if (HiddenLayer)
                {
                    if (!Behind) continue;
                }
                else
                {
                    if (Behind) continue;
                }
            }
            BlendPixel(OutRgba, W, H, Px, Py, R, G, B, Alpha);
        }
    };

    // 3. Subtle technical CAD ground grid (underneath geometry)
    const float GridZ = -0.58f;
    const uint8_t GridInk = PureBw ? 28u : 38u;
    for (int G = -4; G <= 4; ++G)
    {
        const float Coord = float(G) * 0.35f;
        float Ax, Ay, Az, Bx, By, Bz;
        if (ProjectNormalized({ Coord, -1.4f, GridZ }, Ax, Ay, Az) && ProjectNormalized({ Coord, 1.4f, GridZ }, Bx, By, Bz))
            DrawScreenSegment(Ax, Ay, Az, Bx, By, Bz, nullptr, false, false, GridInk, GridInk + 2u, GridInk + 5u, G == 0 ? 0.34f : 0.16f);
        if (ProjectNormalized({ -1.4f, Coord, GridZ }, Ax, Ay, Az) && ProjectNormalized({ 1.4f, Coord, GridZ }, Bx, By, Bz))
            DrawScreenSegment(Ax, Ay, Az, Bx, By, Bz, nullptr, false, false, GridInk, GridInk + 2u, GridInk + 5u, G == 0 ? 0.34f : 0.16f);
    }

    if (!Geom.ValidCondition) return;

    // 4. Depth, view-normal, and part-ID buffers
    const size_t PixelCount = size_t(W) * size_t(H);
    std::vector<float>    ZBuf(PixelCount, 1.0e20f);
    std::vector<Vec3f>    NormBuf(PixelCount, { 0.0f, 0.0f, 0.0f });
    std::vector<uint16_t> PartBuf(PixelCount, 0u);
    std::vector<uint8_t>  LumaBuf(PixelCount, 0u);

    auto EdgeFn = [](float Ax, float Ay, float Bx, float By, float Px, float Py) noexcept -> float
    {
        return (Px - Ax) * (By - Ay) - (Py - Ay) * (Bx - Ax);
    };

    // 5. Rasterize all solid/reference triangles
    for (const CadPreviewTriangle& Tri : Geom.Triangles)
    {
        const Vec3f P0 = (Tri.V0 - Geom.Center) * InvRadius;
        const Vec3f P1 = (Tri.V1 - Geom.Center) * InvRadius;
        const Vec3f P2 = (Tri.V2 - Geom.Center) * InvRadius;

        float Sx0, Sy0, Z0, Sx1, Sy1, Z1, Sx2, Sy2, Z2;
        if (!ProjectNormalized(P0, Sx0, Sy0, Z0) ||
            !ProjectNormalized(P1, Sx1, Sy1, Z1) ||
            !ProjectNormalized(P2, Sx2, Sy2, Z2))
        {
            continue;
        }

        const float Area = EdgeFn(Sx0, Sy0, Sx1, Sy1, Sx2, Sy2);
        if (std::fabs(Area) < 0.04f) continue;

        const int MinX = std::max(0, int(std::floor(std::min({ Sx0, Sx1, Sx2 }))));
        const int MaxX = std::min(int(W) - 1, int(std::ceil(std::max({ Sx0, Sx1, Sx2 }))));
        const int MinY = std::max(0, int(std::floor(std::min({ Sy0, Sy1, Sy2 }))));
        const int MaxY = std::min(int(H) - 1, int(std::ceil(std::max({ Sy0, Sy1, Sy2 }))));
        if (MinX > MaxX || MinY > MaxY) continue;

        // Pre-transform vertex normals to view space
        auto ToViewNormal = [&](Vec3f N) noexcept -> Vec3f
        {
            Vec3f Vn { Dot3(N, Right), Dot3(N, Up), Dot3(N, Forward * -1.0f) };
            if (Vn.Z < 0.0f) Vn = Vn * -1.0f;
            return Normalize3(Vn);
        };
        const Vec3f Vn0 = ToViewNormal(Tri.N0);
        const Vec3f Vn1 = ToViewNormal(Tri.N1);
        const Vec3f Vn2 = ToViewNormal(Tri.N2);
        const float InvArea = 1.0f / Area;
        const bool  Positive = Area > 0.0f;

        for (int Py = MinY; Py <= MaxY; ++Py)
        {
            const float Cy = float(Py) + 0.5f;
            for (int Px = MinX; Px <= MaxX; ++Px)
            {
                const float Cx = float(Px) + 0.5f;
                const float W0 = EdgeFn(Sx1, Sy1, Sx2, Sy2, Cx, Cy);
                const float W1 = EdgeFn(Sx2, Sy2, Sx0, Sy0, Cx, Cy);
                const float W2 = EdgeFn(Sx0, Sy0, Sx1, Sy1, Cx, Cy);
                if (Positive ? (W0 < 0.0f || W1 < 0.0f || W2 < 0.0f) : (W0 > 0.0f || W1 > 0.0f || W2 > 0.0f))
                    continue;

                const float B0 = W0 * InvArea;
                const float B1 = W1 * InvArea;
                const float B2 = W2 * InvArea;
                const float Depth = B0 * Z0 + B1 * Z1 + B2 * Z2;
                const size_t Idx  = size_t(Py) * W + size_t(Px);
                if (Depth >= ZBuf[Idx]) continue;

                ZBuf[Idx] = Depth;
                const Vec3f Vn = Normalize3(Vn0 * B0 + Vn1 * B1 + Vn2 * B2);
                NormBuf[Idx] = Vn;
                PartBuf[Idx] = Tri.Part;

                uint8_t* Dst = OutRgba.data() + Idx * 4u;
                if (PureBw)
                {
                    // Opaque obsidian surface fill matching Experimental/ProjectBrowser Channel 5
                    Dst[0] = 11u;
                    Dst[1] = 12u;
                    Dst[2] = 15u;
                    Dst[3] = 255u;
                    LumaBuf[Idx] = 12u;
                }
                else
                {
                    const auto Mc = SampleStudioMatcap(Vn);
                    // Slightly refined CAD clay/metallic studio balance
                    const float ShadeR = std::clamp(Mc[0] * 0.96f, 0.0f, 1.0f);
                    const float ShadeG = std::clamp(Mc[1] * 0.97f, 0.0f, 1.0f);
                    const float ShadeB = std::clamp(Mc[2] * 1.00f, 0.0f, 1.0f);
                    Dst[0] = static_cast<uint8_t>(ShadeR * 255.0f);
                    Dst[1] = static_cast<uint8_t>(ShadeG * 255.0f);
                    Dst[2] = static_cast<uint8_t>(ShadeB * 255.0f);
                    Dst[3] = 255u;
                    LumaBuf[Idx] = static_cast<uint8_t>((ShadeR * 0.299f + ShadeG * 0.587f + ShadeB * 0.114f) * 255.0f);
                }
            }
        }
    }

    // 6. Explicit CAD crease / B-rep edges & sketch curves (hidden + visible depth-tested)
    if (ContourLayer || Geom.Triangles.empty())
    {
        if (HiddenEdgesCondition && !Geom.Triangles.empty())
        {
            const float HiddenAlpha = PureBw ? 0.22f : 0.12f;
            for (const CadPreviewSegment& Seg : Geom.Creases)
            {
                float Ax, Ay, Az, Bx, By, Bz;
                if (ProjectNormalized((Seg.Start - Geom.Center) * InvRadius, Ax, Ay, Az) &&
                    ProjectNormalized((Seg.End - Geom.Center) * InvRadius, Bx, By, Bz))
                {
                    DrawScreenSegment(Ax, Ay, Az, Bx, By, Bz, &ZBuf, true, true, 196u, 204u, 218u, HiddenAlpha);
                }
            }
        }

        for (const CadPreviewSegment& Seg : Geom.Creases)
        {
            float Ax, Ay, Az, Bx, By, Bz;
            if (!ProjectNormalized((Seg.Start - Geom.Center) * InvRadius, Ax, Ay, Az) ||
                !ProjectNormalized((Seg.End - Geom.Center) * InvRadius, Bx, By, Bz))
            {
                continue;
            }
            if (PureBw || Geom.Triangles.empty())
            {
                const uint8_t Ink = Seg.AccentSketch ? 242u : 226u;
                DrawScreenSegment(Ax, Ay, Az, Bx, By, Bz, &ZBuf, !Geom.Triangles.empty(), false, Ink, Ink + 3u, 250u, 0.88f);
            }
            else
            {
                // Crisp dark-graphite engineering crease lines on top of Studio MatCap
                DrawScreenSegment(Ax, Ay, Az, Bx, By, Bz, &ZBuf, true, false, 18u, 21u, 26u, 0.62f);
            }
        }
    }

    // 7. Screen-space CAD silhouette & dihedral crease contour pass (works on ALL B-rep & Subdiv surfaces!)
    if (ContourLayer && !Geom.Triangles.empty())
    {
        for (uint32_t Y = 1u; Y + 1u < H; ++Y)
        {
            for (uint32_t X = 1u; X + 1u < W; ++X)
            {
                const size_t Idx = size_t(Y) * W + X;
                const float Zc = ZBuf[Idx];
                if (Zc >= 1.0e18f) continue;

                const size_t NLeft  = Idx - 1u;
                const size_t NRight = Idx + 1u;
                const size_t NUp    = Idx - W;
                const size_t NDown  = Idx + W;

                const bool OuterSilhouette = (ZBuf[NLeft] >= 1.0e18f) || (ZBuf[NRight] >= 1.0e18f)
                                          || (ZBuf[NUp]   >= 1.0e18f) || (ZBuf[NDown]  >= 1.0e18f);

                const float DepthJump = std::max({
                    (ZBuf[NLeft]  < 1.0e18f) ? (ZBuf[NLeft]  - Zc) : 0.0f,
                    (ZBuf[NRight] < 1.0e18f) ? (ZBuf[NRight] - Zc) : 0.0f,
                    (ZBuf[NUp]    < 1.0e18f) ? (ZBuf[NUp]    - Zc) : 0.0f,
                    (ZBuf[NDown]  < 1.0e18f) ? (ZBuf[NDown]  - Zc) : 0.0f,
                });
                const bool InternalSilhouette = DepthJump > 0.095f;

                const Vec3f Nc = NormBuf[Idx];
                const float MinDot = std::min({
                    (ZBuf[NLeft]  < 1.0e18f) ? Dot3(Nc, NormBuf[NLeft])  : 1.0f,
                    (ZBuf[NRight] < 1.0e18f) ? Dot3(Nc, NormBuf[NRight]) : 1.0f,
                    (ZBuf[NUp]    < 1.0e18f) ? Dot3(Nc, NormBuf[NUp])    : 1.0f,
                    (ZBuf[NDown]  < 1.0e18f) ? Dot3(Nc, NormBuf[NDown])  : 1.0f,
                });
                const bool SharpCrease = MinDot < 0.80f;

                const uint16_t Pc = PartBuf[Idx];
                const bool PartSeam = ((ZBuf[NRight] < 1.0e18f && PartBuf[NRight] != Pc)
                                    || (ZBuf[NDown]  < 1.0e18f && PartBuf[NDown]  != Pc));

                if (!OuterSilhouette && !InternalSilhouette && !SharpCrease && !PartSeam) continue;

                if (PureBw)
                {
                    // Classic B&W technical CAD contours: bright white silhouettes, crisp secondary creases
                    const float Strength = (OuterSilhouette || InternalSilhouette) ? 0.94f : SharpCrease ? 0.72f : 0.42f;
                    BlendPixel(OutRgba, W, H, int(X), int(Y), 236u, 240u, 248u, Strength);
                }
                else
                {
                    // MatCap + B&W Overlay: crisp white rim silhouette + adaptive B&W CAD creases
                    if (OuterSilhouette)
                    {
                        BlendPixel(OutRgba, W, H, int(X), int(Y), 240u, 244u, 250u, 0.86f);
                    }
                    else if (InternalSilhouette)
                    {
                        const bool BrightBase = LumaBuf[Idx] > 145u;
                        BlendPixel(OutRgba, W, H, int(X), int(Y),
                                   BrightBase ? 14u : 238u, BrightBase ? 16u : 242u, BrightBase ? 22u : 248u, 0.78f);
                    }
                    else if (SharpCrease)
                    {
                        const bool BrightBase = LumaBuf[Idx] > 135u;
                        BlendPixel(OutRgba, W, H, int(X), int(Y),
                                   BrightBase ? 18u : 232u, BrightBase ? 20u : 236u, BrightBase ? 26u : 244u, 0.56f);
                    }
                    else if (PartSeam)
                    {
                        BlendPixel(OutRgba, W, H, int(X), int(Y), 16u, 19u, 24u, 0.36f);
                    }
                }
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             PROJECT BROWSER SESSION STATE
//------------------------------------------------------------------------------------------------------------------------

struct BrowserPlaceShortcut
{
    std::string           Caption       = {};
    std::filesystem::path DirectoryPath = {};
    IconSymbol            Glyph         = IconSymbol::SketcherFolder;
    bool                  DriveGroup    = false;
};

struct BrowserDiskEntry
{
    std::string                    DisplayName        = {};
    std::filesystem::path          FullPath           = {};
    bool                           FolderCondition    = false;
    uint32_t                       SolidArcCount      = 0u;
    std::filesystem::path          PrimaryPreviewPath = {};
    std::vector<std::filesystem::path> NestedFiles    = {};
    uint64_t                       FileSizeBytes      = 0u;
    int64_t                        ModifiedEpoch      = 0;
    std::string                    ModifiedText       = {};
    std::string                    StatsCaption       = {};
    std::unique_ptr<ImTextureData> PreviewTex         = {};
    std::vector<uint8_t>           PreviewRgba        = {};
    uint32_t                       PreviewWidth       = 0u;
    uint32_t                       PreviewHeight      = 0u;
    SketcherCadPreviewStyle        RenderedMode       = SketcherCadPreviewStyle::MatcapOverlay;
    bool                           PreviewReady       = false;
};

enum class BrowserCollectionView : uint8_t
{
    Folder  = 0u,
    Recent  = 1u,
    Starred = 2u,
};

struct GeometryCacheSlot
{
    uint64_t           FileSizeBytes = 0u;
    int64_t            ModifiedEpoch = 0;
    CadPreviewGeometry Geometry      = {};
};

void DrawCenteredText(
    SketcherSheet&     Sheet,
    const PlaneExtent& Box,
    float              CssSize,
    ColorQuad          Ink,
    const char*        Utf8,
    SketcherWeight     Weight        = SketcherWeight::Light,
    float              LetterSpacing = 0.0f)
{
    const float AvailW  = std::max(8.0f, Box.Width() - 8.0f);
    const float TextW   = Sheet.Measure(Utf8, CssSize, Weight, LetterSpacing);
    const float CentreY = 0.5f * (Box.MinimumY + Box.MaximumY);
    if (TextW <= AvailW)
    {
        const float StartX = Box.MinimumX + 0.5f * (Box.Width() - TextW);
        Sheet.Text(StartX, CentreY, CssSize, Ink, Utf8, Weight, LetterSpacing);
    }
    else
    {
        Sheet.TextClipped(Box.MinimumX + 4.0f, CentreY, AvailW, CssSize, Ink, Utf8, Weight);
    }
}

} // namespace

struct SketcherProjectBrowserSession
{
    std::filesystem::path              CurrentDirectory   = {};
    std::vector<std::filesystem::path> BackHistory        = {};
    std::vector<std::filesystem::path> ForwardHistory     = {};
    std::vector<BrowserPlaceShortcut>  Places             = {};
    std::vector<BrowserDiskEntry>      Entries            = {};
    std::vector<std::filesystem::path> RecentFiles        = {};
    std::set<std::string>              StarredPaths       = {};
    std::unordered_map<std::string, GeometryCacheSlot> GeometryCache = {};

    BrowserCollectionView              ActiveCollection   = BrowserCollectionView::Folder;
    SketcherCadPreviewStyle            PreviewStyle       = SketcherCadPreviewStyle::MatcapOverlay;
    int                                SelectedIndex      = -1;
    bool                               LeftCollapsed      = false;
    bool                               RightCollapsed     = false;
    bool                               OnlySolidArcFiles  = false;
    bool                               SortByModified     = false;
    bool                               EditingPathBar     = false;
    bool                               HiddenEdgesEnabled = true;
    bool                               OrthoEnabled       = false;

    std::array<char, 256>              SearchBuffer       = {};
    std::array<char, 512>              PathEditBuffer     = {};
    std::string                        StatusNote         = {};

    // Interactive 3D CAD Reader viewport
    std::string                        ReaderGeometryPath = {};
    float                              ReaderYaw          = -0.68f;
    float                              ReaderPitch        = 0.42f;
    float                              ReaderZoom         = 1.0f;
    bool                               ReaderDirty        = true;
    std::unique_ptr<ImTextureData>     ReaderTex          = {};
    std::vector<uint8_t>               ReaderRgba         = {};
    uint32_t                           ReaderWidth        = 0u;
    uint32_t                           ReaderHeight       = 0u;

    ~SketcherProjectBrowserSession()
    {
        for (BrowserDiskEntry& E : Entries) ReleaseImTexture(E.PreviewTex);
        ReleaseImTexture(ReaderTex);
    }
};

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                           FILESYSTEM DISCOVERY & CACHING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] const CadPreviewGeometry& CachedCadGeometry(SketcherProjectBrowserSession& Session, const std::filesystem::path& FilePath)
{
    static const CadPreviewGeometry EmptyGeom {};
    if (FilePath.empty()) return EmptyGeom;

    const std::string Key = FilePath.generic_string();
    std::error_code Ec {};
    const uint64_t Size = std::filesystem::file_size(FilePath, Ec);
    int64_t Epoch = 0;
    (void)FormatWriteTime(FilePath, &Epoch);

    auto It = Session.GeometryCache.find(Key);
    if (It != Session.GeometryCache.end() && It->second.FileSizeBytes == Size && It->second.ModifiedEpoch == Epoch)
    {
        return It->second.Geometry;
    }

    GeometryCacheSlot Slot {};
    Slot.FileSizeBytes = Size;
    Slot.ModifiedEpoch = Epoch;
    Slot.Geometry      = ExtractCadPreviewGeometry(FilePath);
    auto [InsertedIt, _] = Session.GeometryCache.insert_or_assign(Key, std::move(Slot));
    return InsertedIt->second.Geometry;
}

void InspectFolderForSolidArcProjects(
    const std::filesystem::path&        FolderPath,
    uint32_t&                           OutCount,
    std::filesystem::path&              OutPrimaryPath,
    std::vector<std::filesystem::path>& OutSampleFiles)
{
    namespace Fs = std::filesystem;
    OutCount = 0u;
    OutPrimaryPath.clear();
    OutSampleFiles.clear();

    std::error_code Ec {};
    uint32_t Visited = 0u;

    auto ScanDir = [&](auto&& Self, const Fs::path& Dir, uint32_t Depth) -> void
    {
        if (Depth > FolderScanDepthCap || Visited >= FolderScanEntryCap) return;
        std::vector<Fs::path> SubDirs {};
        std::vector<Fs::path> DirectFiles {};
        for (const auto& Entry : Fs::directory_iterator(Dir, Fs::directory_options::skip_permission_denied, Ec))
        {
            if (Ec || ++Visited >= FolderScanEntryCap) break;
            const std::string Name = Entry.path().filename().string();
            if (Name.empty() || Name[0] == '.') continue;
            std::error_code SubEc {};
            if (Entry.is_regular_file(SubEc) && MatchesSolidArcSuffix(Entry.path()))
            {
                DirectFiles.push_back(Entry.path());
            }
            else if (Depth < FolderScanDepthCap && Entry.is_directory(SubEc))
            {
                SubDirs.push_back(Entry.path());
            }
        }
        std::sort(DirectFiles.begin(), DirectFiles.end());
        for (const Fs::path& File : DirectFiles)
        {
            ++OutCount;
            if (OutPrimaryPath.empty()) OutPrimaryPath = File;
            if (OutSampleFiles.size() < 8u) OutSampleFiles.push_back(File);
        }
        std::sort(SubDirs.begin(), SubDirs.end());
        for (const Fs::path& Sub : SubDirs)
        {
            Self(Self, Sub, Depth + 1u);
            if (Visited >= FolderScanEntryCap) break;
        }
    };

    ScanDir(ScanDir, FolderPath, 0u);
}

void RefreshPlacesDrives(SketcherProjectBrowserSession& Session, const SketcherWorkspaceStatus& Workspace)
{
    namespace Fs = std::filesystem;
    Session.Places.clear();
    std::error_code Ec {};
    std::set<std::string> Seen {};

    auto PushPlace = [&](const std::string& Caption, const Fs::path& Dir, IconSymbol Glyph, bool DriveGroup)
    {
        const Fs::path Clean = NormalizeDirectoryPath(Dir);
        if (Clean.empty()) return;
        const std::string Key = LowercaseAscii(Clean.generic_string());
        if (!Seen.insert(Key).second) return;
        Session.Places.push_back({ Caption, Clean, Glyph, DriveGroup });
    };

    // Workspace shortcuts
    const Fs::path LibraryRoot = DetectSolidArcLibraryRoot(Workspace);
    PushPlace("SolidArc Library", LibraryRoot, IconSymbol::SketcherBody, false);
    if (Fs::is_directory(LibraryRoot / "Vehicles", Ec))
    {
        PushPlace("Vehicles", LibraryRoot / "Vehicles", IconSymbol::SketcherSurface, false);
    }
    if (!Workspace.DocumentPath.empty())
    {
        const Fs::path DocParent = Fs::path(Workspace.DocumentPath).parent_path();
        if (!DocParent.empty() && Fs::is_directory(DocParent, Ec))
            PushPlace("Current Folder", DocParent, IconSymbol::SketcherFolder, false);
    }

    // Standard PC user folders
    const Fs::path Home = QueryUserHomeDirectory();
    PushPlace("Home", Home, IconSymbol::SketcherFolder, false);
    PushPlace("Desktop", Home / "Desktop", IconSymbol::SketcherViewport, false);
    PushPlace("Documents", Home / "Documents", IconSymbol::SketcherFile, false);
    PushPlace("Downloads", Home / "Downloads", IconSymbol::SketcherLayers, false);

    // PC Drives / Mounts
#ifdef _WIN32
    const DWORD Mask = GetLogicalDrives();
    for (int Letter = 0; Letter < 26; ++Letter)
    {
        if ((Mask & (1u << Letter)) == 0u) continue;
        char RootStr[4] = { static_cast<char>('A' + Letter), ':', '\\', '\0' };
        char Label[32]{};
        std::snprintf(Label, sizeof(Label), "Local Disk (%c:)", 'A' + Letter);
        PushPlace(Label, Fs::path(RootStr), IconSymbol::SketcherLayers, true);
    }
#else
    PushPlace("Computer (/)", Fs::path("/"), IconSymbol::SketcherLayers, true);
    for (const char* MountRoot : { "/mnt", "/media" })
    {
        if (!Fs::is_directory(MountRoot, Ec)) continue;
        for (const auto& M : Fs::directory_iterator(MountRoot, Fs::directory_options::skip_permission_denied, Ec))
        {
            if (Ec) break;
            if (M.is_directory(Ec))
                PushPlace(M.path().filename().string(), M.path(), IconSymbol::SketcherLayers, true);
        }
    }
#endif
}

void RebuildVisibleEntries(SketcherProjectBrowserSession& Session)
{
    namespace Fs = std::filesystem;
    for (BrowserDiskEntry& E : Session.Entries) ReleaseImTexture(E.PreviewTex);
    Session.Entries.clear();
    Session.SelectedIndex = -1;
    Session.ReaderDirty   = true;
    Session.StatusNote.clear();

    auto AddFileEntry = [&](const Fs::path& FilePath)
    {
        std::error_code Ec {};
        if (!Fs::is_regular_file(FilePath, Ec) || !MatchesSolidArcSuffix(FilePath)) return;
        BrowserDiskEntry Entry {};
        Entry.DisplayName        = FilePath.stem().string();
        Entry.FullPath           = FilePath;
        Entry.FolderCondition    = false;
        Entry.SolidArcCount      = 1u;
        Entry.PrimaryPreviewPath = FilePath;
        Entry.FileSizeBytes      = Fs::file_size(FilePath, Ec);
        Entry.ModifiedText       = FormatWriteTime(FilePath, &Entry.ModifiedEpoch);
        Entry.StatsCaption       = FormatByteSize(Entry.FileSizeBytes);
        Session.Entries.push_back(std::move(Entry));
    };

    if (Session.ActiveCollection == BrowserCollectionView::Recent)
    {
        for (const Fs::path& P : Session.RecentFiles) AddFileEntry(P);
        if (Session.Entries.empty())
        {
            // Seed with workspace .solidarc library files
            const Fs::path Root = Session.CurrentDirectory;
            uint32_t Count = 0u;
            Fs::path Primary {};
            std::vector<Fs::path> Found {};
            InspectFolderForSolidArcProjects(Root, Count, Primary, Found);
            for (const Fs::path& P : Found) AddFileEntry(P);
        }
    }
    else if (Session.ActiveCollection == BrowserCollectionView::Starred)
    {
        for (const std::string& PathStr : Session.StarredPaths) AddFileEntry(Fs::path(PathStr));
    }
    else
    {
        std::error_code Ec {};
        if (Session.CurrentDirectory.empty() || !Fs::is_directory(Session.CurrentDirectory, Ec))
        {
            Session.StatusNote = "Folder not found: " + Session.CurrentDirectory.generic_string();
            return;
        }

        for (const auto& DirEntry : Fs::directory_iterator(Session.CurrentDirectory, Fs::directory_options::skip_permission_denied, Ec))
        {
            if (Ec) break;
            const Fs::path EntryPath = DirEntry.path();
            const std::string Name = EntryPath.filename().string();
            if (Name.empty() || Name[0] == '.') continue;

            std::error_code SubEc {};
            if (DirEntry.is_directory(SubEc))
            {
                if (Session.OnlySolidArcFiles) continue;
                BrowserDiskEntry Folder {};
                Folder.DisplayName     = Name;
                Folder.FullPath        = EntryPath;
                Folder.FolderCondition = true;
                Folder.ModifiedText    = FormatWriteTime(EntryPath, &Folder.ModifiedEpoch);
                InspectFolderForSolidArcProjects(EntryPath, Folder.SolidArcCount, Folder.PrimaryPreviewPath, Folder.NestedFiles);
                if (Folder.SolidArcCount > 0u)
                {
                    char Buf[64]{};
                    std::snprintf(Buf, sizeof(Buf), "%u .solidarc %s", Folder.SolidArcCount, Folder.SolidArcCount == 1u ? "project" : "projects");
                    Folder.StatsCaption = Buf;
                }
                else
                {
                    Folder.StatsCaption = "Folder";
                }
                Session.Entries.push_back(std::move(Folder));
            }
            else if (DirEntry.is_regular_file(SubEc) && MatchesSolidArcSuffix(EntryPath))
            {
                AddFileEntry(EntryPath);
            }
        }
    }

    // Sort: Project folders with .solidarc first, then .solidarc files, then regular folders (alphabetical or by modified)
    std::sort(Session.Entries.begin(), Session.Entries.end(), [&](const BrowserDiskEntry& A, const BrowserDiskEntry& B)
    {
        auto Rank = [](const BrowserDiskEntry& E) noexcept -> int
        {
            if (!E.FolderCondition) return 0;                                                // .solidarc files
            if (E.SolidArcCount > 0u) return 1;                                              // Project folders with .solidarc previews
            return 2;                                                                        // Plain navigation folders
        };
        const int Ra = Rank(A);
        const int Rb = Rank(B);
        if (Ra != Rb) return Ra < Rb;
        if (Session.SortByModified && A.ModifiedEpoch != B.ModifiedEpoch)
            return A.ModifiedEpoch > B.ModifiedEpoch;
        return LowercaseAscii(A.DisplayName) < LowercaseAscii(B.DisplayName);
    });

    // Auto-select first .solidarc file (or first project folder) for immediate 3D preview
    for (int I = 0; I < int(Session.Entries.size()); ++I)
    {
        if (!Session.Entries[I].FolderCondition || Session.Entries[I].SolidArcCount > 0u)
        {
            Session.SelectedIndex = I;
            break;
        }
    }
}

void RecordRecentSolidArc(SketcherProjectBrowserSession& Session, const std::filesystem::path& FilePath)
{
    const std::string Norm = LowercaseAscii(FilePath.generic_string());
    Session.RecentFiles.erase(
        std::remove_if(Session.RecentFiles.begin(), Session.RecentFiles.end(),
                       [&](const std::filesystem::path& Existing) { return LowercaseAscii(Existing.generic_string()) == Norm; }),
        Session.RecentFiles.end());
    Session.RecentFiles.insert(Session.RecentFiles.begin(), FilePath);
    if (Session.RecentFiles.size() > 24u) Session.RecentFiles.resize(24u);
}

[[nodiscard]] SketcherProjectBrowserSession& EnsureBrowserSession(SketcherWorkspaceStatus& Workspace)
{
    if (!Workspace.ProjectBrowser)
    {
        Workspace.ProjectBrowser = std::make_shared<SketcherProjectBrowserSession>();
    }
    return *Workspace.ProjectBrowser;
}

void NavigateInternal(SketcherProjectBrowserSession& Session, const std::filesystem::path& TargetDir, bool RecordHistory)
{
    const std::filesystem::path Clean = NormalizeDirectoryPath(TargetDir);
    if (Clean.empty())
    {
        Session.StatusNote = "Cannot open directory: " + TargetDir.generic_string();
        return;
    }
    if (RecordHistory && !Session.CurrentDirectory.empty() && Session.CurrentDirectory != Clean)
    {
        Session.BackHistory.push_back(Session.CurrentDirectory);
        Session.ForwardHistory.clear();
    }
    Session.ActiveCollection = BrowserCollectionView::Folder;
    Session.CurrentDirectory = Clean;
    Session.EditingPathBar   = false;
    std::snprintf(Session.PathEditBuffer.data(), Session.PathEditBuffer.size(), "%s", Clean.generic_string().c_str());
    RebuildVisibleEntries(Session);
}

void EnsureTilePreviewRendered(SketcherProjectBrowserSession& Session, BrowserDiskEntry& Entry)
{
    if (Entry.PrimaryPreviewPath.empty()) return;
    if (Entry.PreviewReady && Entry.RenderedMode == Session.PreviewStyle && Entry.PreviewTex != nullptr) return;

    const CadPreviewGeometry& Geom = CachedCadGeometry(Session, Entry.PrimaryPreviewPath);
    RasterizeCadPreview(
        Geom,
        Session.PreviewStyle,
        TileThumbWidthPx,
        TileThumbHeightPx,
        Session.HiddenEdgesEnabled,
        Session.OrthoEnabled,
        -0.68f,
        0.42f,
        1.0f,
        Entry.PreviewRgba);

    Entry.PreviewWidth  = TileThumbWidthPx;
    Entry.PreviewHeight = TileThumbHeightPx;
    Entry.RenderedMode  = Session.PreviewStyle;
    Entry.PreviewReady  = true;

    if (!Entry.FolderCondition && Geom.ValidCondition)
    {
        char Buf[128]{};
        if (Geom.ReferenceCount > 0u)
            std::snprintf(Buf, sizeof(Buf), "%u parts · %zu tris · %s", Geom.PartCount, Geom.Triangles.size(), FormatByteSize(Entry.FileSizeBytes).c_str());
        else if (Geom.BodyCount > 0u)
            std::snprintf(Buf, sizeof(Buf), "%u %s · %u steps · %s", Geom.BodyCount, Geom.BodyCount == 1u ? "body" : "bodies", Geom.StepCount, FormatByteSize(Entry.FileSizeBytes).c_str());
        else
            std::snprintf(Buf, sizeof(Buf), "%u steps · %s", Geom.StepCount, FormatByteSize(Entry.FileSizeBytes).c_str());
        Entry.StatsCaption = Buf;
    }

    if (Entry.PreviewTex == nullptr)
        Entry.PreviewTex = std::make_unique<ImTextureData>();
    UploadRgbaToImTexture(*Entry.PreviewTex, Entry.PreviewRgba.data(), Entry.PreviewWidth, Entry.PreviewHeight);
}

void EnsureReaderPreviewRendered(SketcherProjectBrowserSession& Session, const std::filesystem::path& PreviewPath)
{
    if (PreviewPath.empty()) return;
    const std::string PathKey = PreviewPath.generic_string();
    if (!Session.ReaderDirty && Session.ReaderGeometryPath == PathKey && Session.ReaderTex != nullptr) return;

    const CadPreviewGeometry& Geom = CachedCadGeometry(Session, PreviewPath);
    RasterizeCadPreview(
        Geom,
        Session.PreviewStyle,
        ReaderViewWidthPx,
        ReaderViewHeightPx,
        Session.HiddenEdgesEnabled,
        Session.OrthoEnabled,
        Session.ReaderYaw,
        Session.ReaderPitch,
        Session.ReaderZoom,
        Session.ReaderRgba);

    Session.ReaderGeometryPath = PathKey;
    Session.ReaderWidth        = ReaderViewWidthPx;
    Session.ReaderHeight       = ReaderViewHeightPx;
    Session.ReaderDirty        = false;
    if (Session.ReaderTex == nullptr)
        Session.ReaderTex = std::make_unique<ImTextureData>();
    UploadRgbaToImTexture(*Session.ReaderTex, Session.ReaderRgba.data(), Session.ReaderWidth, Session.ReaderHeight);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC API
//------------------------------------------------------------------------------------------------------------------------

void OpenSketcherProjectBrowser(SketcherWorkspaceStatus& Workspace, const std::filesystem::path& StartDirectory)
{
    SketcherProjectBrowserSession& Session = EnsureBrowserSession(Workspace);
    RefreshPlacesDrives(Session, Workspace);

    std::filesystem::path Target = NormalizeDirectoryPath(StartDirectory);
    if (Target.empty() && !Session.CurrentDirectory.empty())
        Target = NormalizeDirectoryPath(Session.CurrentDirectory);
    if (Target.empty() && !Workspace.DocumentPath.empty())
        Target = NormalizeDirectoryPath(std::filesystem::path(Workspace.DocumentPath).parent_path());
    if (Target.empty())
        Target = DetectSolidArcLibraryRoot(Workspace);

    NavigateInternal(Session, Target, false);
    Workspace.ProjectBrowserOpen = true;
}

void CloseSketcherProjectBrowser(SketcherWorkspaceStatus& Workspace)
{
    Workspace.ProjectBrowserOpen = false;
}

bool NavigateSketcherProjectBrowser(SketcherWorkspaceStatus& Workspace, const std::filesystem::path& DirectoryPath)
{
    SketcherProjectBrowserSession& Session = EnsureBrowserSession(Workspace);
    const std::filesystem::path Clean = NormalizeDirectoryPath(DirectoryPath);
    if (Clean.empty()) return false;
    NavigateInternal(Session, Clean, true);
    return true;
}

void ConfigureSketcherBrowserPreviewStyle(SketcherWorkspaceStatus& Workspace, SketcherCadPreviewStyle Style)
{
    SketcherProjectBrowserSession& Session = EnsureBrowserSession(Workspace);
    if (Session.PreviewStyle == Style) return;
    Session.PreviewStyle = Style;
    Session.ReaderDirty  = true;
    for (BrowserDiskEntry& E : Session.Entries) E.PreviewReady = false;
}

bool SelectSketcherBrowserEntry(SketcherWorkspaceStatus& Workspace, const std::filesystem::path& EntryPath)
{
    if (!Workspace.ProjectBrowser) return false;
    SketcherProjectBrowserSession& Session = *Workspace.ProjectBrowser;
    const std::string TargetKey = LowercaseAscii(EntryPath.generic_string());
    for (int I = 0; I < int(Session.Entries.size()); ++I)
    {
        if (LowercaseAscii(Session.Entries[I].FullPath.generic_string()) == TargetKey ||
            LowercaseAscii(Session.Entries[I].FullPath.filename().string()) == LowercaseAscii(EntryPath.filename().string()))
        {
            Session.SelectedIndex = I;
            Session.ReaderDirty   = true;
            return true;
        }
    }
    return false;
}

bool ActivateSketcherBrowserSelection(SketcherWorkspaceStatus& Workspace)
{
    if (!Workspace.ProjectBrowser) return false;
    SketcherProjectBrowserSession& Session = *Workspace.ProjectBrowser;
    if (Session.SelectedIndex < 0 || Session.SelectedIndex >= int(Session.Entries.size())) return false;

    const bool                  SelectedFolder = Session.Entries[Session.SelectedIndex].FolderCondition;
    const std::filesystem::path SelectedPath   = Session.Entries[Session.SelectedIndex].FullPath;
    if (SelectedFolder)
    {
        NavigateInternal(Session, SelectedPath, true);
        return true;
    }

    const std::string ChosenPath = SelectedPath.string();
    RecordRecentSolidArc(Session, SelectedPath);
    const bool Opened = IntakeSketcherDroppedFile(Workspace, ChosenPath, nullptr);
    if (Opened)
    {
        Workspace.ProjectBrowserOpen = false;
    }
    else
    {
        Session.StatusNote = "Failed to open: " + ChosenPath;
    }
    return Opened;
}

void ReleaseSketcherProjectBrowserTextures(SketcherWorkspaceStatus& Workspace) noexcept
{
    if (!Workspace.ProjectBrowser) return;
    for (BrowserDiskEntry& E : Workspace.ProjectBrowser->Entries) ReleaseImTexture(E.PreviewTex);
    ReleaseImTexture(Workspace.ProjectBrowser->ReaderTex);
}

SketcherBrowserDiagnostic InspectSketcherProjectBrowser(const SketcherWorkspaceStatus& Workspace)
{
    SketcherBrowserDiagnostic Out {};
    Out.OpenCondition = Workspace.ProjectBrowserOpen;
    if (!Workspace.ProjectBrowser) return Out;

    const SketcherProjectBrowserSession& Session = *Workspace.ProjectBrowser;
    Out.CurrentDirectory = Session.CurrentDirectory.generic_string();
    Out.PreviewStyle     = Session.PreviewStyle;
    for (const BrowserDiskEntry& E : Session.Entries)
    {
        if (E.FolderCondition)
        {
            ++Out.VisibleFolders;
            if (E.SolidArcCount > 0u) ++Out.ProjectFolders;
        }
        else
        {
            ++Out.VisibleSolidArcs;
        }
        if (E.PreviewReady && !E.PreviewRgba.empty()) ++Out.RenderedPreviews;
    }
    if (Session.SelectedIndex >= 0 && Session.SelectedIndex < int(Session.Entries.size()))
    {
        Out.SelectedPath = Session.Entries[Session.SelectedIndex].FullPath.generic_string();
    }
    return Out;
}

bool RenderSolidArcCadPreviewRgba(
    const std::filesystem::path& FilePath,
    SketcherCadPreviewStyle      Style,
    uint32_t                     WidthPx,
    uint32_t                     HeightPx,
    bool                         HiddenEdgesCondition,
    bool                         OrthographicCondition,
    float                        YawRadians,
    float                        PitchRadians,
    float                        ZoomFactor,
    std::vector<uint8_t>&        OutRgba)
{
    if (WidthPx == 0u || HeightPx == 0u) return false;
    const CadPreviewGeometry Geom = ExtractCadPreviewGeometry(FilePath);
    if (!Geom.ValidCondition) return false;
    RasterizeCadPreview(Geom, Style, WidthPx, HeightPx, HiddenEdgesCondition, OrthographicCondition, YawRadians, PitchRadians, ZoomFactor, OutRgba);
    return OutRgba.size() == size_t(WidthPx) * size_t(HeightPx) * 4u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           IN-APP PROJECT BROWSER UI OVERLAY
//------------------------------------------------------------------------------------------------------------------------

void DrawSketcherProjectBrowserOverlay(SketcherWorkspaceStatus& Workspace)
{
    if (!Workspace.ProjectBrowserOpen) return;
    SketcherProjectBrowserSession& Session = EnsureBrowserSession(Workspace);
    if (Session.CurrentDirectory.empty())
    {
        OpenSketcherProjectBrowser(Workspace);
    }

    const ImGuiIO& Io = ImGui::GetIO();
    const float FullW = std::max(640.0f, Io.DisplaySize.x);
    const float FullH = std::max(420.0f, Io.DisplaySize.y);

    // Subtle dark backdrop covering entire SolidArc workspace
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(FullW, FullH), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.045f, 0.048f, 0.055f, 0.985f));

    constexpr ImGuiWindowFlags OverlayWindowMode =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (!ImGui::Begin("##SolidArcProjectBrowserOverlay", nullptr, OverlayWindowMode))
    {
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    // Keyboard shortcuts while browser is open
    if (!ImGui::IsAnyItemActive())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            Workspace.ProjectBrowserOpen = false;
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }
        if (Io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false))
        {
            Session.EditingPathBar = true;
            std::snprintf(Session.PathEditBuffer.data(), Session.PathEditBuffer.size(), "%s", Session.CurrentDirectory.generic_string().c_str());
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        {
            if (ActivateSketcherBrowserSelection(Workspace) && !Workspace.ProjectBrowserOpen)
            {
                ImGui::End();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);
                return;
            }
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Backspace, false))
        {
            const std::filesystem::path Parent = Session.CurrentDirectory.parent_path();
            if (!Parent.empty() && Parent != Session.CurrentDirectory)
                NavigateInternal(Session, Parent, true);
        }
    }

    // Budgeted thumbnail generation (up to 4 cards per frame so UI stays responsive on huge directories)
    uint32_t ThumbnailsRenderedThisFrame = 0u;
    for (BrowserDiskEntry& Entry : Session.Entries)
    {
        if (!Entry.PrimaryPreviewPath.empty() && (!Entry.PreviewReady || Entry.RenderedMode != Session.PreviewStyle || Entry.PreviewTex == nullptr))
        {
            EnsureTilePreviewRendered(Session, Entry);
            if (++ThumbnailsRenderedThisFrame >= 4u) break;
        }
    }

    // 3-Column aligned layout (inspired by Experimental/ProjectBrowser + SolidArc OLED studio aesthetic)
    const float Margin     = 14.0f;
    const float Gap        = 10.0f;
    const float HeaderH    = 58.0f;
    const float LeftW      = Session.LeftCollapsed ? 48.0f : std::clamp(FullW * 0.16f, 206.0f, 240.0f);
    const float RightW     = Session.RightCollapsed ? 48.0f : std::clamp(FullW * 0.24f, 304.0f, 360.0f);
    const float CenterW    = std::max(320.0f, FullW - Margin * 2.0f - LeftW - RightW - Gap * 2.0f);
    const float PanelY     = Margin;
    const float PanelH     = FullH - Margin * 2.0f;
    const float LeftX      = Margin;
    const float CenterX    = LeftX + LeftW + Gap;
    const float RightX     = CenterX + CenterW + Gap;

    SketcherSheet Sheet {};
    (void)Sheet.Begin();

    const ColorQuad PanelFill    = SketcherInk(0x131519);
    const ColorQuad CardFill     = SketcherInk(0x191c22);
    const ColorQuad CardHover    = SketcherInk(0x21252d);
    const ColorQuad CardSelected = SketcherInk(0x1d2635);
    const ColorQuad BorderSubtle = SketcherWash(0x14);
    const ColorQuad AccentBlue   = SketcherInk(0x4c8bf5);
    const ColorQuad AccentBorder = { 0.30f, 0.55f, 0.96f, 0.85f };
    const ColorQuad TextPrimary  = SketcherInk(0xf0f2f6);
    const ColorQuad TextMuted    = SketcherInk(0x8e95a3);
    const ColorQuad TextFaint    = SketcherInk(0x636a78);

    // Draw the 3 aligned panel frames
    Sheet.Box(Spanning(LeftX,   PanelY, LeftW,   PanelH), PanelFill, 18.0f, BorderSubtle);
    Sheet.Box(Spanning(CenterX, PanelY, CenterW, PanelH), PanelFill, 18.0f, BorderSubtle);
    Sheet.Box(Spanning(RightX,  PanelY, RightW,  PanelH), PanelFill, 18.0f, BorderSubtle);

    // Aligned 58 px headers across all 3 columns
    Sheet.Line(LeftX + 1.0f,   PanelY + HeaderH, LeftX + LeftW - 1.0f,     PanelY + HeaderH, BorderSubtle, 1.0f);
    Sheet.Line(CenterX + 1.0f, PanelY + HeaderH, CenterX + CenterW - 1.0f, PanelY + HeaderH, BorderSubtle, 1.0f);
    Sheet.Line(RightX + 1.0f,  PanelY + HeaderH, RightX + RightW - 1.0f,   PanelY + HeaderH, BorderSubtle, 1.0f);

    // Helper for crisp rounded pill buttons via SketcherSheet
    auto SheetPill = [&](const char* Key, float X, float Y, float W, float H, const char* Label,
                         bool Active, bool Primary, bool Enabled = true, IconSymbol Glyph = IconSymbol::Count) -> bool
    {
        const PlaneExtent Box = Spanning(X, Y, W, H);
        const SketcherPress Press = PressSketcherRegion(Workspace.Widgets, Key, Box, Enabled);
        const float Opacity = Enabled ? 1.0f : 0.35f;
        const ColorQuad Fill = Primary ? (Press.HoveredCondition ? SketcherInk(0x5b96f8) : AccentBlue)
                             : Active  ? SketcherInk(0x283244)
                             : Press.HoveredCondition ? SketcherInk(0x262a33)
                                                      : SketcherInk(0x1d2027);
        const ColorQuad Border = Primary ? AccentBorder : Active ? AccentBorder : SketcherWash(0x12);
        const ColorQuad Ink    = Primary ? SketcherInk(0xffffff)
                               : Active  ? SketcherInk(0xf2f6ff)
                               : Press.HoveredCondition ? SketcherInk(0xffffff) : SketcherInk(0xc8ceda);
        Sheet.Box(Box, ControlKit::Faded(Fill, Opacity), H * 0.5f, ControlKit::Faded(Border, Opacity));
        const float CentreY = Y + H * 0.5f;
        const bool HasGlyph = (Glyph != IconSymbol::Count);
        const float LabelW  = (Label && *Label) ? Sheet.Measure(Label, 10.5f, SketcherWeight::Regular) : 0.0f;
        const float GlyphSz = 13.0f;
        const float TotalW  = (HasGlyph ? GlyphSz : 0.0f) + ((HasGlyph && LabelW > 0.0f) ? 6.0f : 0.0f) + LabelW;
        float CurX = X + std::max(8.0f, (W - TotalW) * 0.5f);
        if (HasGlyph)
        {
            Sheet.Icon(Glyph, CurX, CentreY - GlyphSz * 0.5f, GlyphSz, ControlKit::Faded(Ink, Opacity));
            CurX += GlyphSz + 6.0f;
        }
        if (Label && *Label)
        {
            Sheet.TextClipped(CurX, CentreY, std::max(10.0f, X + W - CurX - 6.0f), 10.5f,
                              ControlKit::Faded(Ink, Opacity), Label, SketcherWeight::Regular);
        }
        return Press.ClickedCondition;
    };

    // =================================================================================================================
    // 1. LEFT COLUMN: PLACES, PC DRIVES & COLLECTIONS
    // =================================================================================================================
    const float HeaderMidY = PanelY + HeaderH * 0.5f;
    if (!Session.LeftCollapsed)
    {
        Sheet.Icon(IconSymbol::SketcherBody, LeftX + 16.0f, HeaderMidY - 8.5f, 17.0f, AccentBlue);
        Sheet.TextClipped(LeftX + 40.0f, HeaderMidY, LeftW - 76.0f, 13.0f, TextPrimary, "SolidArc", SketcherWeight::Regular);
    }
    if (SheetPill("BrowserCollapseLeft", LeftX + LeftW - 36.0f, HeaderMidY - 13.0f, 26.0f, 26.0f,
                  Session.LeftCollapsed ? ">" : "<", false, false))
    {
        Session.LeftCollapsed = !Session.LeftCollapsed;
    }

    if (!Session.LeftCollapsed)
    {
        ImGui::SetCursorScreenPos(ImVec2(LeftX + 10.0f, PanelY + HeaderH + 10.0f));
        if (ImGui::BeginChild("##SolidArcBrowserLeftRail", ImVec2(LeftW - 20.0f, PanelH - HeaderH - 64.0f), ImGuiChildFlags_None))
        {
            SketcherSheet Rail {};
            (void)Rail.Begin();
            const float RowW = std::max(60.0f, ImGui::GetContentRegionAvail().x);

            auto DrawSectionCaption = [&](const char* Caption)
            {
                const ImVec2 Cur = ImGui::GetCursorScreenPos();
                Rail.TextClipped(Cur.x + 8.0f, Cur.y + 10.0f, RowW - 12.0f, 9.0f, TextFaint, Caption, SketcherWeight::Light);
                ImGui::Dummy(ImVec2(RowW, 20.0f));
            };

            auto DrawRailRow = [&](const char* Key, const char* Label, IconSymbol Glyph, bool Selected) -> bool
            {
                const ImVec2 Cur = ImGui::GetCursorScreenPos();
                const PlaneExtent Box = Spanning(Cur.x, Cur.y, RowW, 30.0f);
                const SketcherPress Press = PressSketcherRegion(Workspace.Widgets, Key, Box);
                if (Selected || Press.HoveredCondition)
                {
                    Rail.Box(Box, Selected ? SketcherInk(0x232d40) : SketcherInk(0x1c2028), 8.0f,
                             Selected ? AccentBorder : SketcherInk(0, 0.0f));
                }
                const ColorQuad Ink = Selected ? SketcherInk(0xffffff) : Press.HoveredCondition ? TextPrimary : SketcherInk(0xb9c0ce);
                Rail.Icon(Glyph, Cur.x + 10.0f, Cur.y + 8.0f, 14.0f, Selected ? AccentBlue : Ink);
                Rail.TextClipped(Cur.x + 31.0f, Cur.y + 15.0f, RowW - 38.0f, 11.0f, Ink, Label,
                                 Selected ? SketcherWeight::Regular : SketcherWeight::Light);
                ImGui::Dummy(ImVec2(RowW, 32.0f));
                return Press.ClickedCondition;
            };

            DrawSectionCaption("COLLECTION");
            if (DrawRailRow("RailRecent", "Recent .solidarc", IconSymbol::SketcherInspect, Session.ActiveCollection == BrowserCollectionView::Recent))
            {
                Session.ActiveCollection = BrowserCollectionView::Recent;
                RebuildVisibleEntries(Session);
            }
            if (DrawRailRow("RailStarred", "Starred", IconSymbol::SketcherFocus, Session.ActiveCollection == BrowserCollectionView::Starred))
            {
                Session.ActiveCollection = BrowserCollectionView::Starred;
                RebuildVisibleEntries(Session);
            }

            ImGui::Dummy(ImVec2(RowW, 6.0f));
            DrawSectionCaption("PLACES");
            int PlaceIdx = 0;
            for (const BrowserPlaceShortcut& Place : Session.Places)
            {
                if (Place.DriveGroup) continue;
                const bool Active = (Session.ActiveCollection == BrowserCollectionView::Folder)
                                 && (LowercaseAscii(Session.CurrentDirectory.generic_string()) == LowercaseAscii(Place.DirectoryPath.generic_string()));
                char Key[48]{};
                std::snprintf(Key, sizeof(Key), "RailPlace%d", PlaceIdx++);
                if (DrawRailRow(Key, Place.Caption.c_str(), Place.Glyph, Active))
                {
                    NavigateInternal(Session, Place.DirectoryPath, true);
                }
            }

            ImGui::Dummy(ImVec2(RowW, 6.0f));
            DrawSectionCaption("THIS PC · DRIVES");
            int DriveIdx = 0;
            for (const BrowserPlaceShortcut& Drive : Session.Places)
            {
                if (!Drive.DriveGroup) continue;
                const bool Active = (Session.ActiveCollection == BrowserCollectionView::Folder)
                                 && (LowercaseAscii(Session.CurrentDirectory.generic_string()) == LowercaseAscii(Drive.DirectoryPath.generic_string()));
                char Key[48]{};
                std::snprintf(Key, sizeof(Key), "RailDrive%d", DriveIdx++);
                if (DrawRailRow(Key, Drive.Caption.c_str(), Drive.Glyph, Active))
                {
                    NavigateInternal(Session, Drive.DirectoryPath, true);
                }
            }
        }
        ImGui::EndChild();

        // Bottom quick action: + New Blank Sketch
        if (SheetPill("BrowserNewBlank", LeftX + 12.0f, PanelY + PanelH - 44.0f, LeftW - 24.0f, 32.0f,
                      "New Blank Sketch", false, false, true, IconSymbol::SketcherPlus))
        {
            Workspace.Document = CreateSketcherDocument("Untitled");
            Workspace.DocumentPath.clear();
            RefreshSketcherScene(Workspace, false);
            Workspace.ProjectBrowserOpen = false;
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }
    }

    // =================================================================================================================
    // 2. CENTER HEADER: BACK / FORWARD / UP / REFRESH + INTERACTIVE BREADCRUMB / PATH BAR + PREVIEW STYLE PILLS
    // =================================================================================================================
    float NavX = CenterX + 14.0f;
    const float NavBtnY = HeaderMidY - 14.0f;

    if (SheetPill("BrowserBack", NavX, NavBtnY, 28.0f, 28.0f, "<", false, false, !Session.BackHistory.empty()))
    {
        const std::filesystem::path Prev = Session.BackHistory.back();
        Session.BackHistory.pop_back();
        Session.ForwardHistory.push_back(Session.CurrentDirectory);
        NavigateInternal(Session, Prev, false);
    }
    NavX += 32.0f;

    if (SheetPill("BrowserForward", NavX, NavBtnY, 28.0f, 28.0f, ">", false, false, !Session.ForwardHistory.empty()))
    {
        const std::filesystem::path Next = Session.ForwardHistory.back();
        Session.ForwardHistory.pop_back();
        Session.BackHistory.push_back(Session.CurrentDirectory);
        NavigateInternal(Session, Next, false);
    }
    NavX += 32.0f;

    const std::filesystem::path ParentDir = Session.CurrentDirectory.parent_path();
    const bool CanGoUp = !ParentDir.empty() && ParentDir != Session.CurrentDirectory;
    if (SheetPill("BrowserUp", NavX, NavBtnY, 28.0f, 28.0f, "^", false, false, CanGoUp))
    {
        NavigateInternal(Session, ParentDir, true);
    }
    NavX += 36.0f;

    // Right side of center header: Preview Style segmented pill (`MatCap + B&W` | `B&W CAD` | `MatCap`)
    const float ModeSegW = std::min(258.0f, std::max(180.0f, CenterW * 0.34f));
    const float ModeSegX = CenterX + CenterW - ModeSegW - 14.0f;
    const PlaneExtent ModeTrack = Spanning(ModeSegX, HeaderMidY - 14.0f, ModeSegW, 28.0f);
    Sheet.Box(ModeTrack, SketcherInk(0x0d0f13), 14.0f, BorderSubtle);

    static const struct { const char* Label; const char* Key; SketcherCadPreviewStyle Style; } ModeButtons[3] = {
        { "MatCap+B&W", "ModeMatcapOverlay", SketcherCadPreviewStyle::MatcapOverlay },
        { "B&W CAD",    "ModeDraftingBw",    SketcherCadPreviewStyle::DraftingBw    },
        { "MatCap",     "ModeMatcapStudio",  SketcherCadPreviewStyle::MatcapStudio  },
    };
    const float SegEachW = (ModeSegW - 4.0f) / 3.0f;
    for (int M = 0; M < 3; ++M)
    {
        const bool Active = (Session.PreviewStyle == ModeButtons[M].Style);
        const PlaneExtent Pill = Spanning(ModeSegX + 2.0f + float(M) * SegEachW, HeaderMidY - 12.0f, SegEachW, 24.0f);
        const SketcherPress Press = PressSketcherRegion(Workspace.Widgets, ModeButtons[M].Key, Pill);
        if (Active || Press.HoveredCondition)
        {
            Sheet.Box(Pill, Active ? AccentBlue : SketcherInk(0x222630), 12.0f);
        }
        DrawCenteredText(Sheet, Pill, 9.5f, Active ? SketcherInk(0xffffff) : SketcherInk(0xaeb6c5),
                         ModeButtons[M].Label, Active ? SketcherWeight::Regular : SketcherWeight::Light);
        if (Press.ClickedCondition)
        {
            ConfigureSketcherBrowserPreviewStyle(Workspace, ModeButtons[M].Style);
        }
    }

    // Middle of center header: Interactive Breadcrumb / Direct PC Path bar
    const float PathBarW = std::max(120.0f, ModeSegX - NavX - 12.0f);
    const PlaneExtent PathBox = Spanning(NavX, HeaderMidY - 15.0f, PathBarW, 30.0f);
    Sheet.Box(PathBox, SketcherInk(0x0e1015), 10.0f, BorderSubtle);

    if (Session.EditingPathBar)
    {
        const SketcherTypefaces& Tf = QuerySketcherTypefaces();
        if (Tf.Regular) ImGui::PushFont(static_cast<ImFont*>(Tf.Regular), 13.0f);
        ImGui::SetCursorScreenPos(ImVec2(NavX + 8.0f, HeaderMidY - 10.5f));
        ImGui::SetNextItemWidth(std::max(60.0f, PathBarW - 62.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 3.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
        const bool Submitted = ImGui::InputText("##BrowserPathEdit", Session.PathEditBuffer.data(), Session.PathEditBuffer.size(),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (Tf.Regular) ImGui::PopFont();
        if (Submitted)
        {
            NavigateInternal(Session, std::filesystem::path(Session.PathEditBuffer.data()), true);
        }
        if (SheetPill("BrowserPathGo", NavX + PathBarW - 48.0f, HeaderMidY - 11.0f, 42.0f, 22.0f, "Go", false, true))
        {
            NavigateInternal(Session, std::filesystem::path(Session.PathEditBuffer.data()), true);
        }
    }
    else
    {
        // Build clickable breadcrumb segments from Session.CurrentDirectory
        std::vector<std::pair<std::string, std::filesystem::path>> Crumbs {};
        std::filesystem::path Acc {};
        for (const auto& Part : Session.CurrentDirectory)
        {
            Acc /= Part;
            std::string Label = Part.string();
            if (Label == "/" || Label == "\\") Label = "Root";
            Crumbs.push_back({ Label, Acc });
        }
        // Keep trailing segments that fit inside PathBarW - 44 px
        const float EditBtnW = 36.0f;
        const float AvailCrumbW = std::max(60.0f, PathBarW - EditBtnW - 14.0f);
        size_t FirstCrumb = 0u;
        while (FirstCrumb + 1u < Crumbs.size())
        {
            float Needed = 0.0f;
            for (size_t K = FirstCrumb; K < Crumbs.size(); ++K)
                Needed += Sheet.Measure(Crumbs[K].first.c_str(), 10.5f, SketcherWeight::Regular) + 22.0f;
            if (Needed <= AvailCrumbW) break;
            ++FirstCrumb;
        }

        float CrumbX = NavX + 8.0f;
        for (size_t K = FirstCrumb; K < Crumbs.size(); ++K)
        {
            if (K > FirstCrumb)
            {
                Sheet.Text(CrumbX, HeaderMidY, 10.0f, TextFaint, "/", SketcherWeight::Light);
                CrumbX += 10.0f;
            }
            const bool Last = (K + 1u == Crumbs.size());
            const float SegW = std::min(140.0f, Sheet.Measure(Crumbs[K].first.c_str(), 10.5f, SketcherWeight::Regular) + 14.0f);
            if (CrumbX + SegW > NavX + PathBarW - EditBtnW - 6.0f) break;
            const PlaneExtent SegBox = Spanning(CrumbX, HeaderMidY - 11.0f, SegW, 22.0f);
            char Key[48]{};
            std::snprintf(Key, sizeof(Key), "Crumb%zu", K);
            const SketcherPress Press = PressSketcherRegion(Workspace.Widgets, Key, SegBox);
            if (Press.HoveredCondition)
                Sheet.Box(SegBox, SketcherInk(0x232833), 6.0f);
            Sheet.TextClipped(CrumbX + 6.0f, HeaderMidY, SegW - 10.0f, 10.5f,
                              Last ? TextPrimary : (Press.HoveredCondition ? TextPrimary : TextMuted),
                              Crumbs[K].first.c_str(), Last ? SketcherWeight::Regular : SketcherWeight::Light);
            if (Press.ClickedCondition && !Last)
            {
                NavigateInternal(Session, Crumbs[K].second, true);
            }
            CrumbX += SegW + 2.0f;
        }

        if (SheetPill("BrowserEditPath", NavX + PathBarW - EditBtnW - 4.0f, HeaderMidY - 11.0f, EditBtnW, 22.0f, "Path", false, false))
        {
            Session.EditingPathBar = true;
            std::snprintf(Session.PathEditBuffer.data(), Session.PathEditBuffer.size(), "%s", Session.CurrentDirectory.generic_string().c_str());
        }
    }

    // =================================================================================================================
    // 3. CENTER SUB-BAR & CARD GRID (ONLY FOLDERS + .SOLIDARC FILES!)
    // =================================================================================================================
    const float SubBarY = PanelY + HeaderH + 12.0f;
    const float InnerX  = CenterX + 18.0f;
    const float InnerW  = CenterW - 36.0f;

    const std::string FolderHeading = Session.ActiveCollection == BrowserCollectionView::Recent  ? "Recent .solidarc"
                                    : Session.ActiveCollection == BrowserCollectionView::Starred ? "Starred .solidarc"
                                    : Session.CurrentDirectory.filename().empty()
                                        ? Session.CurrentDirectory.generic_string()
                                        : Session.CurrentDirectory.filename().string();

    // Filter visible indices by search query
    const std::string SearchLower = LowercaseAscii(Session.SearchBuffer.data());
    std::vector<int> Filtered {};
    uint32_t CountSolidArcs = 0u;
    uint32_t CountFolders   = 0u;
    for (int I = 0; I < int(Session.Entries.size()); ++I)
    {
        const BrowserDiskEntry& E = Session.Entries[I];
        if (!SearchLower.empty() && LowercaseAscii(E.DisplayName).find(SearchLower) == std::string::npos)
            continue;
        Filtered.push_back(I);
        if (E.FolderCondition) ++CountFolders;
        else ++CountSolidArcs;
    }

    // Left of sub-bar: Folder title + counts
    const float TitleMaxW = std::max(100.0f, InnerW - 356.0f);
    Sheet.TextClipped(InnerX, SubBarY + 13.0f, TitleMaxW, 15.5f, TextPrimary, FolderHeading.c_str(), SketcherWeight::Regular);
    char CountLine[96]{};
    std::snprintf(CountLine, sizeof(CountLine), "%u .solidarc  ·  %u %s",
                  CountSolidArcs, CountFolders, CountFolders == 1u ? "folder" : "folders");
    Sheet.TextClipped(InnerX, SubBarY + 32.0f, TitleMaxW, 10.0f, TextMuted, CountLine, SketcherWeight::Light);

    // Right of sub-bar: Search + Only .solidarc toggle + Sort
    const float SortW   = 82.0f;
    const float FilterW = 94.0f;
    const float SearchW = std::clamp(InnerW - TitleMaxW - SortW - FilterW - 22.0f, 120.0f, 190.0f);
    const float SortX   = InnerX + InnerW - SortW;
    const float FilterX = SortX - FilterW - 6.0f;
    const float SearchX = FilterX - SearchW - 8.0f;

    {
        const SketcherTypefaces& Tf = QuerySketcherTypefaces();
        if (Tf.Regular) ImGui::PushFont(static_cast<ImFont*>(Tf.Regular), 13.0f);
        ImGui::SetCursorScreenPos(ImVec2(SearchX, SubBarY + 3.0f));
        ImGui::SetNextItemWidth(SearchW);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 13.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 5.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.08f, 0.09f, 0.12f, 1.0f));
        ImGui::InputTextWithHint("##BrowserSearch", "Search .solidarc...", Session.SearchBuffer.data(), Session.SearchBuffer.size());
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        if (Tf.Regular) ImGui::PopFont();
    }

    if (SheetPill("BrowserOnlyFiles", FilterX, SubBarY + 3.0f, FilterW, 26.0f,
                  Session.OnlySolidArcFiles ? "Only .solidarc" : "Show Folders", Session.OnlySolidArcFiles, false))
    {
        Session.OnlySolidArcFiles = !Session.OnlySolidArcFiles;
        RebuildVisibleEntries(Session);
    }

    if (SheetPill("BrowserSortToggle", SortX, SubBarY + 3.0f, SortW, 26.0f,
                  Session.SortByModified ? "Modified" : "Name A-Z", false, false))
    {
        Session.SortByModified = !Session.SortByModified;
        RebuildVisibleEntries(Session);
    }

    if (!Session.StatusNote.empty())
    {
        Sheet.TextClipped(InnerX, SubBarY + 46.0f, InnerW, 10.0f, SketcherInk(0xf2a75c), Session.StatusNote.c_str(), SketcherWeight::Regular);
    }

    // Scrollable card grid
    const float GridTopY = SubBarY + (Session.StatusNote.empty() ? 48.0f : 60.0f);
    const float GridH    = std::max(120.0f, PanelY + PanelH - GridTopY - 12.0f);
    ImGui::SetCursorScreenPos(ImVec2(InnerX, GridTopY));
    if (ImGui::BeginChild("##SolidArcBrowserCardGrid", ImVec2(InnerW, GridH), ImGuiChildFlags_None))
    {
        SketcherSheet Grid {};
        (void)Grid.Begin();
        const float AvailGridW = std::max(200.0f, ImGui::GetContentRegionAvail().x - 6.0f);
        const int   Cols       = AvailGridW >= 780.0f ? 3 : (AvailGridW >= 430.0f ? 2 : 1);
        const float CardGap    = 14.0f;
        const float CardW      = std::floor((AvailGridW - CardGap * float(Cols - 1)) / float(Cols));
        const float PreviewH   = std::floor(CardW * 0.60f);
        const float CardH      = PreviewH + 62.0f;

        if (Filtered.empty())
        {
            const ImVec2 EmptyPos = ImGui::GetCursorScreenPos();
            const PlaneExtent EmptyBox = Spanning(EmptyPos.x, EmptyPos.y + 24.0f, AvailGridW, 124.0f);
            Grid.Box(EmptyBox, SketcherInk(0x161920), 14.0f, BorderSubtle);
            DrawCenteredText(Grid, Spanning(EmptyBox.MinimumX, EmptyBox.MinimumY + 26.0f, AvailGridW, 24.0f),
                             14.0f, TextPrimary, "No .solidarc files or folders here", SketcherWeight::Regular);
            DrawCenteredText(Grid, Spanning(EmptyBox.MinimumX, EmptyBox.MinimumY + 54.0f, AvailGridW, 20.0f),
                             10.5f, TextMuted, "Browse Places or Drives on the left, or navigate up to another folder.", SketcherWeight::Light);
            ImGui::Dummy(ImVec2(AvailGridW, 160.0f));
        }
        else
        {
            const ImVec2 Origin = ImGui::GetCursorScreenPos();
            int VisualSlot = 0;
            int PendingOpenIdx = -1;
            int PendingStarIdx = -1;

            for (int EntryIdx : Filtered)
            {
                BrowserDiskEntry& Entry = Session.Entries[EntryIdx];
                const int Col = VisualSlot % Cols;
                const int Row = VisualSlot / Cols;
                const float Cx = Origin.x + float(Col) * (CardW + CardGap);
                const float Cy = Origin.y + float(Row) * (CardH + CardGap);
                const PlaneExtent CardBox = Spanning(Cx, Cy, CardW, CardH);
                const bool Selected = (Session.SelectedIndex == EntryIdx);

                char CardKey[64]{};
                std::snprintf(CardKey, sizeof(CardKey), "BrowserCard%d", EntryIdx);
                const SketcherPress Press = PressSketcherRegion(Workspace.Widgets, CardKey, CardBox);

                const ColorQuad Fill = Selected ? CardSelected : (Press.HoveredCondition ? CardHover : CardFill);
                const ColorQuad Rim  = Selected ? AccentBorder : (Press.HoveredCondition ? SketcherWash(0x28) : BorderSubtle);
                Grid.Box(CardBox, Fill, 14.0f, Rim);

                // Preview viewport area inside the card
                const float Pad = 7.0f;
                const PlaneExtent ThumbBox = Spanning(Cx + Pad, Cy + Pad, CardW - Pad * 2.0f, PreviewH - Pad);
                Grid.Box(ThumbBox, SketcherInk(0x0b0c10), 10.0f, SketcherWash(0x0e));

                if (!Entry.PrimaryPreviewPath.empty() && Entry.PreviewTex != nullptr && Entry.PreviewTex->Pixels != nullptr)
                {
                    ImDrawList* Dl = ImGui::GetWindowDrawList();
                    Dl->AddImageRounded(
                        Entry.PreviewTex->GetTexRef(),
                        ImVec2(ThumbBox.MinimumX + 1.0f, ThumbBox.MinimumY + 1.0f),
                        ImVec2(ThumbBox.MaximumX - 1.0f, ThumbBox.MaximumY - 1.0f),
                        ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f),
                        IM_COL32(255, 255, 255, 255),
                        9.0f);
                }
                else if (Entry.FolderCondition)
                {
                    // Plain navigable PC folder visual
                    const float MidX = 0.5f * (ThumbBox.MinimumX + ThumbBox.MaximumX);
                    const float MidY = 0.5f * (ThumbBox.MinimumY + ThumbBox.MaximumY);
                    Grid.Icon(IconSymbol::SketcherFolder, MidX - 18.0f, MidY - 22.0f, 36.0f,
                              Press.HoveredCondition ? TextPrimary : SketcherInk(0x6e7788));
                    DrawCenteredText(Grid, Spanning(ThumbBox.MinimumX + 8.0f, MidY + 18.0f, ThumbBox.Width() - 16.0f, 16.0f),
                                     9.5f, TextFaint, "Double-click to open folder", SketcherWeight::Light);
                }
                else
                {
                    DrawCenteredText(Grid, ThumbBox, 10.0f, TextFaint, "Rendering CAD preview...", SketcherWeight::Light);
                }

                // Top-left badge pill (`.solidarc` OR `PROJECT FOLDER · N .solidarc`)
                const char* BadgeLabel = !Entry.FolderCondition ? "SOLIDARC"
                                       : (Entry.SolidArcCount > 0u ? "PROJECT FOLDER" : "FOLDER");
                const float BadgeW = Grid.Measure(BadgeLabel, 8.5f, SketcherWeight::Regular, 0.4f) + 16.0f;
                const PlaneExtent BadgeBox = Spanning(ThumbBox.MinimumX + 8.0f, ThumbBox.MinimumY + 8.0f, BadgeW, 20.0f);
                const ColorQuad BadgeFill = Entry.FolderCondition && Entry.SolidArcCount > 0u
                                          ? ColorQuad{ 0.14f, 0.30f, 0.56f, 0.88f }
                                          : ColorQuad{ 0.06f, 0.07f, 0.09f, 0.84f };
                Grid.Box(BadgeBox, BadgeFill, 10.0f, SketcherWash(0x20));
                DrawCenteredText(Grid, BadgeBox, 8.5f, SketcherInk(0xe6ecf8), BadgeLabel, SketcherWeight::Regular, 0.4f);

                // Top-right badge: Star button on .solidarc files, or `.solidarc` count on project folders
                if (!Entry.FolderCondition)
                {
                    const bool Starred = Session.StarredPaths.contains(Entry.FullPath.generic_string());
                    const PlaneExtent StarBox = Spanning(ThumbBox.MaximumX - 28.0f, ThumbBox.MinimumY + 8.0f, 22.0f, 22.0f);
                    char StarKey[48]{};
                    std::snprintf(StarKey, sizeof(StarKey), "BrowserStar%d", EntryIdx);
                    const SketcherPress StarPress = PressSketcherRegion(Workspace.Widgets, StarKey, StarBox);
                    Grid.Box(StarBox, Starred ? ColorQuad{ 0.25f, 0.20f, 0.08f, 0.90f } : ColorQuad{ 0.06f, 0.07f, 0.09f, 0.78f },
                             11.0f, SketcherWash(0x1e));
                    DrawCenteredText(Grid, StarBox, 11.0f, Starred ? SketcherInk(0xf5c451) : TextMuted, "*", SketcherWeight::Regular);
                    if (StarPress.ClickedCondition) PendingStarIdx = EntryIdx;
                }
                else if (Entry.SolidArcCount > 0u)
                {
                    char CountBadge[24]{};
                    std::snprintf(CountBadge, sizeof(CountBadge), "%u CAD", Entry.SolidArcCount);
                    const float Cw = Grid.Measure(CountBadge, 8.5f, SketcherWeight::Regular) + 14.0f;
                    const PlaneExtent CBox = Spanning(ThumbBox.MaximumX - Cw - 8.0f, ThumbBox.MinimumY + 8.0f, Cw, 20.0f);
                    Grid.Box(CBox, { 0.07f, 0.08f, 0.11f, 0.85f }, 10.0f, SketcherWash(0x20));
                    DrawCenteredText(Grid, CBox, 8.5f, SketcherInk(0x9bc0ff), CountBadge, SketcherWeight::Regular);
                }

                // Footer metadata inside card (clipped cleanly, zero overflow)
                const float MetaY = ThumbBox.MaximumY + 14.0f;
                Grid.Icon(Entry.FolderCondition ? IconSymbol::SketcherFolder : IconSymbol::SketcherFile,
                          Cx + 12.0f, MetaY - 7.0f, 14.0f,
                          Entry.FolderCondition ? (Entry.SolidArcCount > 0u ? AccentBlue : TextMuted) : SketcherInk(0xd5dce8));
                Grid.TextClipped(Cx + 31.0f, MetaY, CardW - 44.0f, 12.0f, TextPrimary, Entry.DisplayName.c_str(), SketcherWeight::Regular);
                Grid.TextClipped(Cx + 12.0f, MetaY + 20.0f, CardW - 24.0f, 9.8f, TextMuted, Entry.StatsCaption.c_str(), SketcherWeight::Light);

                if (Press.ClickedCondition)
                {
                    Session.SelectedIndex = EntryIdx;
                    Session.ReaderDirty   = true;
                }
                if (Press.DoubleCondition)
                {
                    Session.SelectedIndex = EntryIdx;
                    PendingOpenIdx = EntryIdx;
                }
                ++VisualSlot;
            }

            const int TotalRows = (VisualSlot + Cols - 1) / Cols;
            ImGui::Dummy(ImVec2(AvailGridW, float(TotalRows) * (CardH + CardGap) + 8.0f));

            if (PendingStarIdx >= 0 && PendingStarIdx < int(Session.Entries.size()))
            {
                const std::string PathKey = Session.Entries[PendingStarIdx].FullPath.generic_string();
                if (!Session.StarredPaths.erase(PathKey)) Session.StarredPaths.insert(PathKey);
            }
            if (PendingOpenIdx >= 0)
            {
                if (ActivateSketcherBrowserSelection(Workspace) && !Workspace.ProjectBrowserOpen)
                {
                    ImGui::EndChild();
                    ImGui::End();
                    ImGui::PopStyleColor();
                    ImGui::PopStyleVar(2);
                    return;
                }
            }
        }
    }
    ImGui::EndChild();

    // =================================================================================================================
    // 4. RIGHT COLUMN: INTERACTIVE 3D CAD READER & INSPECTOR
    // =================================================================================================================
    if (!Session.RightCollapsed)
    {
        Sheet.TextClipped(RightX + 16.0f, HeaderMidY, RightW - 96.0f, 13.0f, TextPrimary, "CAD Preview", SketcherWeight::Regular);
    }
    if (SheetPill("BrowserCollapseRight", RightX + RightW - (Session.RightCollapsed ? 38.0f : 76.0f), HeaderMidY - 13.0f, 26.0f, 26.0f,
                  Session.RightCollapsed ? "<" : ">", false, false))
    {
        Session.RightCollapsed = !Session.RightCollapsed;
    }
    if (!Session.RightCollapsed)
    {
        if (SheetPill("BrowserClose", RightX + RightW - 44.0f, HeaderMidY - 13.0f, 32.0f, 26.0f, "Esc", false, false))
        {
            Workspace.ProjectBrowserOpen = false;
            ImGui::End();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar(2);
            return;
        }
    }

    if (!Session.RightCollapsed)
    {
        const float InspX = RightX + 14.0f;
        const float InspW = RightW - 28.0f;
        float InspY = PanelY + HeaderH + 14.0f;

        if (Session.SelectedIndex >= 0 && Session.SelectedIndex < int(Session.Entries.size()))
        {
            const BrowserDiskEntry& Sel = Session.Entries[Session.SelectedIndex];
            const bool HasPreviewTarget = !Sel.PrimaryPreviewPath.empty();

            // Interactive 3D CAD Preview Canvas
            const float ReaderH = std::floor(InspW * 0.64f);
            const PlaneExtent ReaderBox = Spanning(InspX, InspY, InspW, ReaderH);
            Sheet.Box(ReaderBox, SketcherInk(0x0a0b0e), 12.0f, BorderSubtle);

            if (HasPreviewTarget)
            {
                EnsureReaderPreviewRendered(Session, Sel.PrimaryPreviewPath);
                if (Session.ReaderTex != nullptr && Session.ReaderTex->Pixels != nullptr)
                {
                    ImDrawList* Dl = ImGui::GetWindowDrawList();
                    Dl->AddImageRounded(
                        Session.ReaderTex->GetTexRef(),
                        ImVec2(ReaderBox.MinimumX + 1.0f, ReaderBox.MinimumY + 1.0f),
                        ImVec2(ReaderBox.MaximumX - 1.0f, ReaderBox.MaximumY - 1.0f),
                        ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f),
                        IM_COL32(255, 255, 255, 255),
                        11.0f);
                }

                // Drag to orbit, scroll to zoom, double-click to reset
                const SketcherPress OrbitPress = PressSketcherRegion(Workspace.Widgets, "BrowserReaderOrbit", ReaderBox);
                if (OrbitPress.HeldCondition && (std::fabs(Io.MouseDelta.x) > 0.01f || std::fabs(Io.MouseDelta.y) > 0.01f))
                {
                    Session.ReaderYaw   -= Io.MouseDelta.x * 0.012f;
                    Session.ReaderPitch  = std::clamp(Session.ReaderPitch + Io.MouseDelta.y * 0.010f, -1.42f, 1.42f);
                    Session.ReaderDirty  = true;
                }
                if (OrbitPress.HoveredCondition && std::fabs(Io.MouseWheel) > 0.01f)
                {
                    Session.ReaderZoom  = std::clamp(Session.ReaderZoom * std::exp(Io.MouseWheel * 0.12f), 0.45f, 2.8f);
                    Session.ReaderDirty = true;
                }
                if (OrbitPress.DoubleCondition)
                {
                    Session.ReaderYaw   = -0.68f;
                    Session.ReaderPitch = 0.42f;
                    Session.ReaderZoom  = 1.0f;
                    Session.ReaderDirty = true;
                }

                // Subtle bottom-left orbit hint inside reader
                Sheet.TextClipped(ReaderBox.MinimumX + 10.0f, ReaderBox.MaximumY - 13.0f, InspW - 20.0f, 9.0f,
                                  SketcherInk(0x9ca5b6), "Drag to orbit · Scroll to zoom", SketcherWeight::Light);
            }
            else
            {
                const float MidX = 0.5f * (ReaderBox.MinimumX + ReaderBox.MaximumX);
                const float MidY = 0.5f * (ReaderBox.MinimumY + ReaderBox.MaximumY);
                Sheet.Icon(IconSymbol::SketcherFolder, MidX - 20.0f, MidY - 24.0f, 40.0f, TextMuted);
                DrawCenteredText(Sheet, Spanning(InspX, MidY + 20.0f, InspW, 18.0f), 10.5f, TextFaint,
                                 "Folder contains no .solidarc files", SketcherWeight::Light);
            }
            InspY += ReaderH + 10.0f;

            // Camera Preset + Drafting Controls (when preview exists)
            if (HasPreviewTarget)
            {
                static const struct { const char* Label; const char* Key; float Yaw; float Pitch; } Views[4] = {
                    { "Iso",   "ViewIso",   -0.68f, 0.42f },
                    { "Front", "ViewFront",  0.00f, 0.05f },
                    { "Side",  "ViewSide",   1.57f, 0.05f },
                    { "Top",   "ViewTop",   -0.68f, 1.40f },
                };
                const float PresetW = (InspW - 18.0f) / 4.0f;
                for (int V = 0; V < 4; ++V)
                {
                    if (SheetPill(Views[V].Key, InspX + float(V) * (PresetW + 6.0f), InspY, PresetW, 24.0f, Views[V].Label, false, false))
                    {
                        Session.ReaderYaw   = Views[V].Yaw;
                        Session.ReaderPitch = Views[V].Pitch;
                        Session.ReaderZoom  = 1.0f;
                        Session.ReaderDirty = true;
                    }
                }
                InspY += 30.0f;

                const float HalfBtnW = (InspW - 6.0f) * 0.5f;
                if (SheetPill("BrowserHiddenEdges", InspX, InspY, HalfBtnW, 24.0f, "Hidden edges", Session.HiddenEdgesEnabled, false))
                {
                    Session.HiddenEdgesEnabled = !Session.HiddenEdgesEnabled;
                    Session.ReaderDirty = true;
                    for (BrowserDiskEntry& E : Session.Entries) E.PreviewReady = false;
                }
                if (SheetPill("BrowserOrtho", InspX + HalfBtnW + 6.0f, InspY, HalfBtnW, 24.0f,
                              Session.OrthoEnabled ? "Orthographic" : "Perspective", Session.OrthoEnabled, false))
                {
                    Session.OrthoEnabled = !Session.OrthoEnabled;
                    Session.ReaderDirty = true;
                    for (BrowserDiskEntry& E : Session.Entries) E.PreviewReady = false;
                }
                InspY += 34.0f;
            }

            // Title & Description
            Sheet.TextClipped(InspX, InspY + 10.0f, InspW, 15.0f, TextPrimary, Sel.DisplayName.c_str(), SketcherWeight::Regular);
            InspY += 24.0f;

            const std::string SubType = Sel.FolderCondition
                ? (Sel.SolidArcCount > 0u ? "SolidArc Project Folder" : "Local Directory")
                : "SolidArc CAD Document (.solidarc)";
            Sheet.TextClipped(InspX, InspY + 6.0f, InspW, 10.0f, AccentBlue, SubType.c_str(), SketcherWeight::Regular);
            InspY += 20.0f;

            // Scrollable metadata / nested project files list
            const float ActionReserve = 56.0f;
            const float MetaAvailH = std::max(60.0f, PanelY + PanelH - InspY - ActionReserve);
            ImGui::SetCursorScreenPos(ImVec2(InspX, InspY));
            if (ImGui::BeginChild("##SolidArcBrowserInspectorMeta", ImVec2(InspW, MetaAvailH), ImGuiChildFlags_None))
            {
                const SketcherTypefaces& Tf = QuerySketcherTypefaces();
                if (Tf.Light) ImGui::PushFont(static_cast<ImFont*>(Tf.Light), 13.5f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.76f, 0.83f, 1.0f));

                if (HasPreviewTarget)
                {
                    const CadPreviewGeometry& Geom = CachedCadGeometry(Session, Sel.PrimaryPreviewPath);
                    if (!Geom.Description.empty())
                    {
                        ImGui::TextWrapped("%s", Geom.Description.c_str());
                        ImGui::Spacing();
                    }
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::Text("Triangles: %zu", Geom.Triangles.size());
                    ImGui::Text("Creases / Edges: %zu", Geom.Creases.size());
                    if (Geom.BodyCount > 0u) ImGui::Text("B-rep Bodies: %u", Geom.BodyCount);
                    if (Geom.ReferenceCount > 0u) ImGui::Text("Reference Surfaces: %u (%u parts)", Geom.ReferenceCount, Geom.PartCount);
                    ImGui::Text("Parametric Steps: %u", Geom.StepCount);
                }

                if (!Sel.FolderCondition)
                {
                    ImGui::Text("File Size: %s", FormatByteSize(Sel.FileSizeBytes).c_str());
                }
                if (!Sel.ModifiedText.empty())
                {
                    ImGui::Text("Modified: %s", Sel.ModifiedText.c_str());
                }
                ImGui::Spacing();
                ImGui::TextDisabled("Location");
                ImGui::TextWrapped("%s", Sel.FullPath.generic_string().c_str());

                if (Sel.FolderCondition && !Sel.NestedFiles.empty())
                {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                    ImGui::TextDisabled("SolidArc files in folder");
                    std::filesystem::path DirectOpenPath {};
                    for (size_t K = 0u; K < Sel.NestedFiles.size(); ++K)
                    {
                        const std::string Label = Sel.NestedFiles[K].filename().string() + "###NestedCad" + std::to_string(K);
                        if (ImGui::Selectable(Label.c_str()))
                        {
                            DirectOpenPath = Sel.NestedFiles[K];
                        }
                    }
                    if (!DirectOpenPath.empty())
                    {
                        RecordRecentSolidArc(Session, DirectOpenPath);
                        if (IntakeSketcherDroppedFile(Workspace, DirectOpenPath.string(), nullptr))
                        {
                            Workspace.ProjectBrowserOpen = false;
                        }
                    }
                }

                ImGui::PopStyleColor();
                if (Tf.Light) ImGui::PopFont();
            }
            ImGui::EndChild();

            // Bottom primary CTA button
            const char* ActionLabel = Sel.FolderCondition ? "Open Folder" : "Open in SolidArc";
            if (SheetPill("BrowserOpenAction", InspX, PanelY + PanelH - 46.0f, InspW, 34.0f, ActionLabel, false, true))
            {
                (void)ActivateSketcherBrowserSelection(Workspace);
            }
        }
        else
        {
            DrawCenteredText(Sheet, Spanning(InspX, InspY + 80.0f, InspW, 24.0f), 12.5f, TextMuted,
                             "Select a .solidarc file", SketcherWeight::Regular);
            DrawCenteredText(Sheet, Spanning(InspX, InspY + 106.0f, InspW, 20.0f), 10.0f, TextFaint,
                             "Inspect MatCap & B&W CAD previews here", SketcherWeight::Light);
        }
    }

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

} // namespace Frontier::Workspaces::Sketcher

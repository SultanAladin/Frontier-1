/*============================================================================================================================================
                                                             SKETCHERICONS.CPP
============================================================================================================================================*/
// 🧩 Anti-aliased SVG icon rasterizer & ImTextureData atlas for Parametric Sketcher (66 icons in EngineContent/Icons/Sketcher/*.svg)

#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherIcons.h"

#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

namespace Frontier
{

namespace
{

constexpr int IconCount = static_cast<int>(IconSymbol::Count);
constexpr int Columns   = 16;
constexpr int Rows      = (IconCount + Columns - 1) / Columns;
constexpr int Gutter    = 2;
constexpr float PiF     = 3.14159265358979323846f;

constexpr const char* IconFiles[] = {
#define FRONTIER_ICON(Symbol, File) File,
#include "Engine/Workspaces/ParametricSketcher/Interface/SketcherSymbols.inc"
#undef FRONTIER_ICON
};
static_assert(std::size(IconFiles) == static_cast<size_t>(IconSymbol::Count));

struct TileRect
{
    ImVec2 Min {};
    ImVec2 Max {};
};

struct IconAtlas
{
    ImTextureData                   Texture {};
    std::array<TileRect, IconCount> Tiles   {};
    int                             Size    = 0;
};

[[nodiscard]] bool ReadXmlAttr(std::string_view Tag, std::string_view Name, std::string_view& OutValue) noexcept
{
    size_t Pos = 0u;
    while (Pos < Tag.size())
    {
        const size_t Hit = Tag.find(Name, Pos);
        if (Hit == std::string_view::npos)
        {
            return false;
        }
        const bool BoundaryBefore = (Hit == 0u) || std::isspace(static_cast<unsigned char>(Tag[Hit - 1u]));
        size_t After = Hit + Name.size();
        while (After < Tag.size() && std::isspace(static_cast<unsigned char>(Tag[After]))) ++After;
        if (BoundaryBefore && After < Tag.size() && Tag[After] == '=')
        {
            ++After;
            while (After < Tag.size() && std::isspace(static_cast<unsigned char>(Tag[After]))) ++After;
            if (After < Tag.size() && (Tag[After] == '"' || Tag[After] == '\''))
            {
                const char Quote = Tag[After++];
                const size_t End = Tag.find(Quote, After);
                if (End != std::string_view::npos)
                {
                    OutValue = Tag.substr(After, End - After);
                    return true;
                }
            }
        }
        Pos = Hit + Name.size();
    }
    return false;
}

[[nodiscard]] float ReadXmlFloat(std::string_view Tag, std::string_view Name, float DefaultVal = 0.0f) noexcept
{
    std::string_view Val {};
    if (!ReadXmlAttr(Tag, Name, Val) || Val.empty())
    {
        return DefaultVal;
    }
    std::string Buf(Val);
    char* EndPtr = nullptr;
    const float Parsed = std::strtof(Buf.c_str(), &EndPtr);
    return (EndPtr != Buf.c_str() && std::isfinite(Parsed)) ? Parsed : DefaultVal;
}

void AppendEllipseContour(std::vector<VectorIconRepository::Contour>& Out, float Cx, float Cy, float Rx, float Ry)
{
    VectorIconRepository::Contour C {};
    C.Closed = true;
    constexpr int Segs = 28;
    C.Points.reserve(Segs);
    for (int I = 0; I < Segs; ++I)
    {
        const float A = (2.0f * PiF * float(I)) / float(Segs);
        C.Points.push_back(Point2D{ Cx + Rx * std::cos(A), Cy + Ry * std::sin(A) });
    }
    Out.push_back(std::move(C));
}

void AppendRectContour(std::vector<VectorIconRepository::Contour>& Out, float X, float Y, float W, float H, float Rx, float Ry)
{
    if (W <= 0.0f || H <= 0.0f)
    {
        return;
    }
    Rx = std::clamp(Rx, 0.0f, 0.5f * W);
    Ry = std::clamp(Ry > 0.0f ? Ry : Rx, 0.0f, 0.5f * H);
    if (Rx <= 0.01f && Ry <= 0.01f)
    {
        VectorIconRepository::Contour C {};
        C.Closed = true;
        C.Points = { { X, Y }, { X + W, Y }, { X + W, Y + H }, { X, Y + H } };
        Out.push_back(std::move(C));
        return;
    }
    VectorIconRepository::Contour C {};
    C.Closed = true;
    constexpr int CornerSegs = 6;
    const struct { float Cx, Cy, Start; } Corners[4] = {
        { X + W - Rx, Y + Ry,     -0.5f * PiF },
        { X + W - Rx, Y + H - Ry,  0.0f },
        { X + Rx,     Y + H - Ry,  0.5f * PiF },
        { X + Rx,     Y + Ry,      PiF },
    };
    for (const auto& Corner : Corners)
    {
        for (int I = 0; I <= CornerSegs; ++I)
        {
            const float A = Corner.Start + (0.5f * PiF * float(I)) / float(CornerSegs);
            C.Points.push_back(Point2D{ Corner.Cx + Rx * std::cos(A), Corner.Cy + Ry * std::sin(A) });
        }
    }
    Out.push_back(std::move(C));
}

[[nodiscard]] float DistanceToSegmentSq(float Px, float Py, float Ax, float Ay, float Bx, float By) noexcept
{
    const float Dx = Bx - Ax;
    const float Dy = By - Ay;
    const float LenSq = Dx * Dx + Dy * Dy;
    if (LenSq <= 1.0e-8f)
    {
        const float Ex = Px - Ax;
        const float Ey = Py - Ay;
        return Ex * Ex + Ey * Ey;
    }
    const float T = std::clamp(((Px - Ax) * Dx + (Py - Ay) * Dy) / LenSq, 0.0f, 1.0f);
    const float Qx = Ax + T * Dx;
    const float Qy = Ay + T * Dy;
    const float Ex = Px - Qx;
    const float Ey = Py - Qy;
    return Ex * Ex + Ey * Ey;
}

struct IconPresentationMode
{
    std::filesystem::path      Root {};
    std::unique_ptr<IconAtlas> Current {};
    std::unique_ptr<IconAtlas> Retired {};

    explicit IconPresentationMode(std::filesystem::path IconsRoot)
        : Root(std::move(IconsRoot))
    {
    }

    ~IconPresentationMode()
    {
        Forget(Retired);
        Forget(Current);
    }

    static void Forget(std::unique_ptr<IconAtlas>& Item)
    {
        if (!Item)
        {
            return;
        }
        ImGui::UnregisterUserTexture(&Item->Texture);
        auto& Textures = ImGui::GetPlatformIO().Textures;
        for (int I = Textures.Size - 1; I >= 0; --I)
        {
            if (Textures[I] == &Item->Texture)
            {
                Textures.erase(Textures.Data + I);
            }
        }
        Item.reset();
    }

    void RasterizeSymbol(int Index, int Size, uint8_t* OutRgba)
    {
        std::memset(OutRgba, 0, size_t(Size) * Size * 4u);
        const std::filesystem::path Path = Root / IconFiles[Index];
        std::ifstream File(Path, std::ios::binary | std::ios::ate);
        if (!File || File.tellg() <= 0)
        {
            return;
        }
        std::string SvgText(static_cast<size_t>(File.tellg()), '\0');
        File.seekg(0);
        File.read(SvgText.data(), static_cast<std::streamsize>(SvgText.size()));
        if (!File)
        {
            return;
        }

        float ViewBoxSize = 24.0f;
        float StrokeWidth = 1.45f;
        std::vector<VectorIconRepository::Contour> Contours {};

        const std::string_view Svg(SvgText);
        size_t Cursor = 0u;
        while (Cursor < Svg.size())
        {
            const size_t Open = Svg.find('<', Cursor);
            if (Open == std::string_view::npos) break;
            const size_t Close = Svg.find('>', Open + 1u);
            if (Close == std::string_view::npos) break;
            const std::string_view Tag = Svg.substr(Open + 1u, Close - (Open + 1u));
            Cursor = Close + 1u;

            if (Tag.empty() || Tag[0] == '/' || Tag[0] == '?' || Tag[0] == '!') continue;

            if (Tag.starts_with("svg"))
            {
                StrokeWidth = ReadXmlFloat(Tag, "stroke-width", StrokeWidth);
                std::string_view Vb {};
                if (ReadXmlAttr(Tag, "viewBox", Vb))
                {
                    float X = 0.0f, Y = 0.0f, W = 24.0f, H = 24.0f;
                    std::string VbStr(Vb);
                    if (std::sscanf(VbStr.c_str(), "%f %f %f %f", &X, &Y, &W, &H) == 4 && W > 0.0f)
                    {
                        ViewBoxSize = W;
                    }
                }
            }
            else if (Tag.starts_with("path"))
            {
                std::string_view D {};
                if (ReadXmlAttr(Tag, "d", D))
                {
                    auto Parsed = VectorIconRepository::FlattenSvgPath(D);
                    for (auto& C : Parsed)
                    {
                        Contours.push_back(std::move(C));
                    }
                }
            }
            else if (Tag.starts_with("circle"))
            {
                const float Cx = ReadXmlFloat(Tag, "cx", 0.0f);
                const float Cy = ReadXmlFloat(Tag, "cy", 0.0f);
                const float R  = ReadXmlFloat(Tag, "r", 0.0f);
                if (R > 0.0f) AppendEllipseContour(Contours, Cx, Cy, R, R);
            }
            else if (Tag.starts_with("ellipse"))
            {
                const float Cx = ReadXmlFloat(Tag, "cx", 0.0f);
                const float Cy = ReadXmlFloat(Tag, "cy", 0.0f);
                const float Rx = ReadXmlFloat(Tag, "rx", 0.0f);
                const float Ry = ReadXmlFloat(Tag, "ry", 0.0f);
                if (Rx > 0.0f && Ry > 0.0f) AppendEllipseContour(Contours, Cx, Cy, Rx, Ry);
            }
            else if (Tag.starts_with("rect"))
            {
                const float X  = ReadXmlFloat(Tag, "x", 0.0f);
                const float Y  = ReadXmlFloat(Tag, "y", 0.0f);
                const float W  = ReadXmlFloat(Tag, "width", 0.0f);
                const float H  = ReadXmlFloat(Tag, "height", 0.0f);
                const float Rx = ReadXmlFloat(Tag, "rx", 0.0f);
                const float Ry = ReadXmlFloat(Tag, "ry", 0.0f);
                AppendRectContour(Contours, X, Y, W, H, Rx, Ry);
            }
        }

        const float ScalePx = float(Size) / std::max(1.0f, ViewBoxSize);
        const float HalfStrokePx = std::max(0.65f, 0.5f * StrokeWidth * ScalePx);

        for (int Py = 0; Py < Size; ++Py)
        {
            const float Y = float(Py) + 0.5f;
            for (int Px = 0; Px < Size; ++Px)
            {
                const float X = float(Px) + 0.5f;
                float MinDistSq = 1.0e12f;

                for (const auto& C : Contours)
                {
                    if (C.Points.size() < 2u) continue;
                    const size_t Count = C.Points.size();
                    for (size_t I = 0u; I + 1u < Count; ++I)
                    {
                        const float D2 = DistanceToSegmentSq(
                            X, Y,
                            C.Points[I].X * ScalePx, C.Points[I].Y * ScalePx,
                            C.Points[I + 1u].X * ScalePx, C.Points[I + 1u].Y * ScalePx);
                        if (D2 < MinDistSq) MinDistSq = D2;
                    }
                    if (C.Closed)
                    {
                        const float D2 = DistanceToSegmentSq(
                            X, Y,
                            C.Points[Count - 1u].X * ScalePx, C.Points[Count - 1u].Y * ScalePx,
                            C.Points[0u].X * ScalePx, C.Points[0u].Y * ScalePx);
                        if (D2 < MinDistSq) MinDistSq = D2;
                    }
                }

                const float Dist = std::sqrt(MinDistSq);
                const float Coverage = std::clamp(0.5f + (HalfStrokePx - Dist), 0.0f, 1.0f);
                if (Coverage > 0.0f)
                {
                    const size_t Off = (size_t(Py) * Size + Px) * 4u;
                    OutRgba[Off + 0] = 255u;
                    OutRgba[Off + 1] = 255u;
                    OutRgba[Off + 2] = 255u;
                    OutRgba[Off + 3] = static_cast<uint8_t>(std::round(Coverage * 255.0f));
                }
            }
        }
    }

    void Frame()
    {
        if (Retired && Retired->Texture.Status == ImTextureStatus_Destroyed)
        {
            Forget(Retired);
        }
        const auto& IO = ImGui::GetIO();
        float Scale = std::max(IO.DisplayFramebufferScale.x, IO.DisplayFramebufferScale.y);
        if (!std::isfinite(Scale))
        {
            Scale = 1.0f;
        }
        const int Size = static_cast<int>(std::ceil(24.0f * std::clamp(Scale, 1.0f, 4.0f)));
        if (Retired || (Current && Current->Size == Size))
        {
            return;
        }

        auto Next = std::make_unique<IconAtlas>();
        Next->Size = Size;
        const int Pitch = Size + 2 * Gutter;
        Next->Texture.Create(ImTextureFormat_RGBA32, Columns * Pitch, Rows * Pitch);
        Next->Texture.UseColors = true;

        std::vector<uint8_t> Scratch(size_t(Size) * Size * 4u, 0u);
        for (int I = 0; I < IconCount; ++I)
        {
            RasterizeSymbol(I, Size, Scratch.data());
            const int X = (I % Columns) * Pitch + Gutter;
            const int Y = (I / Columns) * Pitch + Gutter;
            for (int Row = 0; Row < Size; ++Row)
            {
                std::memcpy(Next->Texture.GetPixelsAt(X, Y + Row), Scratch.data() + size_t(Row) * Size * 4u, size_t(Size) * 4u);
            }
            auto& Tile = Next->Tiles[I];
            Tile.Min = ImVec2(float(X) / float(Next->Texture.Width), float(Y) / float(Next->Texture.Height));
            Tile.Max = ImVec2(float(X + Size) / float(Next->Texture.Width), float(Y + Size) / float(Next->Texture.Height));
        }

        ImGui::RegisterUserTexture(&Next->Texture);
        if (Current)
        {
            Current->Texture.WantDestroyNextFrame = true;
            Retired = std::move(Current);
        }
        Current = std::move(Next);
    }
};

void HookCallback(ImGuiContext*, ImGuiContextHook* H)
{
    auto* P = static_cast<IconPresentationMode*>(H->UserData);
    if (H->Type == ImGuiContextHookType_NewFramePre)
    {
        P->Frame();
    }
    else
    {
        delete P;
    }
}

IconPresentationMode* FindPresentation()
{
    ImGuiContext* Context = ImGui::GetCurrentContext();
    if (!Context)
    {
        return nullptr;
    }
    for (const auto& H : Context->Hooks)
    {
        if (H.Callback == HookCallback && H.Type == ImGuiContextHookType_Shutdown)
        {
            return static_cast<IconPresentationMode*>(H.UserData);
        }
    }
    return nullptr;
}

} // namespace

void SketcherIconPresentation::Attach(const std::filesystem::path& IconsRoot)
{
    if (!ImGui::GetCurrentContext() || FindPresentation() != nullptr)
    {
        return;
    }
    std::filesystem::path Resolved = IconsRoot;
    if (!std::filesystem::exists(Resolved / "Sketcher"))
    {
        const std::filesystem::path Fallback = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "EngineContent/Icons";
        if (std::filesystem::exists(Fallback / "Sketcher"))
        {
            Resolved = Fallback;
        }
    }
    auto P = std::make_unique<IconPresentationMode>(Resolved);
    P->Frame();
    ImGuiContextHook H;
    H.Callback = HookCallback;
    H.UserData = P.get();
    H.Type     = ImGuiContextHookType_NewFramePre;
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &H);
    H.Type     = ImGuiContextHookType_Shutdown;
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &H);
    P.release();
}

bool SketcherIconPresentation::Draw(
    ImDrawList* List,
    IconSymbol  Symbol,
    ImVec2      Min,
    float       Size,
    float       Opacity,
    IconDomain  /*Domain*/,
    uint32_t    Tint)
{
    auto* P = FindPresentation();
    if (!P)
    {
        return false;
    }
    if (!P->Current)
    {
        P->Frame();
    }
    const int Index = static_cast<int>(Symbol);
    if (!P->Current || Index < 0 || Index >= IconCount || !List || !std::isfinite(Size) || Size <= 0.0f)
    {
        return false;
    }
    const auto& T = P->Current->Tiles[Index];
    if (!std::isfinite(Opacity))
    {
        Opacity = 1.0f;
    }
    const int Alpha = static_cast<int>(255.0f * std::clamp(Opacity, 0.0f, 1.0f) + 0.5f);
    const ImU32 FinalTint = (Tint & ~IM_COL32_A_MASK) |
        (ImU32(((Tint >> IM_COL32_A_SHIFT) & 255u) * static_cast<unsigned>(Alpha) / 255u) << IM_COL32_A_SHIFT);
    List->AddImage(P->Current->Texture.GetTexRef(), Min, ImVec2(Min.x + Size, Min.y + Size), T.Min, T.Max, FinalTint);
    return true;
}

} // namespace Frontier

#include "IconPresentation.h"
#include "Engine/Diagnostics/DiagnosticJournal.h"
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <memory>

namespace Frontier {
namespace {
constexpr int Count = static_cast<int>(IconSymbol::Count);
constexpr int Columns = 16, Rows = (Count + Columns - 1) / Columns;
constexpr int Gutter = 2;
struct Tile { ImVec2 Min, Max; IconResult Result; std::string Diagnostic; };
struct Atlas {
    ImTextureData Texture;
    std::array<Tile, Count> Tiles;
    int Size = 0;
};
struct Presentation {
    IconArt Art;
    std::unique_ptr<Atlas> Current, Retired;
    explicit Presentation(const std::filesystem::path& Root) : Art(Root) {}
    static void Forget(std::unique_ptr<Atlas>& Item) {
        if (!Item) return;
        // Backend owns GPU resources. It must acknowledge destruction first.
        IM_ASSERT(Item->Texture.BackendUserData == nullptr);
        ImGui::UnregisterUserTexture(&Item->Texture);
        auto& Textures = ImGui::GetPlatformIO().Textures;
        for (int I = Textures.Size - 1; I >= 0; --I)
            if (Textures[I] == &Item->Texture) Textures.erase(Textures.Data + I);
        Item.reset();
    }
    void Frame() {
        if (Retired && Retired->Texture.Status == ImTextureStatus_Destroyed) Forget(Retired);
        const auto& IO = ImGui::GetIO();
        if (!(IO.BackendFlags & ImGuiBackendFlags_RendererHasTextures)) return;
        float Scale = std::max(IO.DisplayFramebufferScale.x, IO.DisplayFramebufferScale.y);
        if (!std::isfinite(Scale)) Scale = 1.0f;
        const int Size = static_cast<int>(std::ceil(24.0f * std::clamp(Scale, 1.0f, 4.0f)));
        // At most two atlases, even when framebuffer scale changes every frame.
        // Until retirement completes, continue drawing the previous resolution.
        if (Retired || (Current && Current->Size == Size)) return;
        auto Next = std::make_unique<Atlas>();
        Next->Size = Size;
        const int Pitch = Size + 2 * Gutter;
        Next->Texture.Create(ImTextureFormat_RGBA32, Columns * Pitch, Rows * Pitch);
        Next->Texture.UseColors = true;
        for (int I = 0; I < Count; ++I) {
            auto Raster = Art.Rasterize(static_cast<IconSymbol>(I), static_cast<float>(Size), static_cast<float>(Size), 1, IconPolicy::Strict, IconArt::Domain(static_cast<IconSymbol>(I)));
            const int X = (I % Columns) * Pitch + Gutter, Y = (I / Columns) * Pitch + Gutter;
            for (int Row = 0; Row < Size; ++Row)
                std::memcpy(Next->Texture.GetPixelsAt(X, Y + Row), Raster->Rgba.data() + Row * Size * 4, Size * 4);
            auto& Tile = Next->Tiles[I];
            Tile.Min = ImVec2(float(X) / Next->Texture.Width, float(Y) / Next->Texture.Height);
            Tile.Max = ImVec2(float(X + Size) / Next->Texture.Width, float(Y + Size) / Next->Texture.Height);
            Tile.Result = Raster->Result;
            Tile.Diagnostic = Raster->Diagnostic;
            if (Raster->Substitute) DiagnosticJournal::Append("IconSubstitute", std::string(IconArt::Filename(static_cast<IconSymbol>(I))) + ": " + Raster->Diagnostic);
            if (Raster->Substitute)
                std::fprintf(stderr, "[Icons] %s: %s (%s)\n", IconArt::Filename(static_cast<IconSymbol>(I)),
                    IconArt::ResultName(Raster->Result), Raster->Diagnostic.c_str());
        }
        ImGui::RegisterUserTexture(&Next->Texture);
        if (Current) { Current->Texture.WantDestroyNextFrame = true; Retired = std::move(Current); }
        Current = std::move(Next);
    }
};
void Hook(ImGuiContext*, ImGuiContextHook* H) {
    auto* P = static_cast<Presentation*>(H->UserData);
    if (H->Type == ImGuiContextHookType_NewFramePre) P->Frame();
    else {
        Presentation::Forget(P->Retired);
        Presentation::Forget(P->Current);
        delete P;
    }
}
Presentation* Find() {
    auto* Context = ImGui::GetCurrentContext();
    if (!Context) return nullptr;
    for (const auto& H : Context->Hooks)
        if (H.Callback == Hook && H.Type == ImGuiContextHookType_Shutdown)
            return static_cast<Presentation*>(H.UserData);
    return nullptr;
}
bool Valid(IconSymbol S) { return static_cast<unsigned>(S) < Count; }
}
void IconPresentation::Attach(const std::filesystem::path& Root) {
    DiagnosticJournal::Append("Icons", "Attach " + DiagnosticJournal::EncodePath(Root));
    if (!ImGui::GetCurrentContext() || Find()) return;
    auto P = std::make_unique<Presentation>(Root);
    ImGuiContextHook H;
    H.Callback = Hook; H.UserData = P.get(); H.Type = ImGuiContextHookType_NewFramePre;
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &H);
    H.Type = ImGuiContextHookType_Shutdown;
    ImGui::AddContextHook(ImGui::GetCurrentContext(), &H);
    P.release();
}
bool IconPresentation::Draw(ImDrawList* List, IconSymbol S, ImVec2 Min, float Size, float Opacity, IconDomain Domain, ImU32 Tint) {
    auto* P = Find();
    if (!P || !P->Current || !Valid(S) || !IconArt::Available(S, Domain) || !List || !std::isfinite(Size) || Size <= 0) return false;
    const auto& T = P->Current->Tiles[static_cast<int>(S)];
    if (!std::isfinite(Opacity)) Opacity = 1;
    const auto Alpha = static_cast<int>(255 * std::clamp(Opacity, 0.0f, 1.0f) + 0.5f);
    List->AddImage(P->Current->Texture.GetTexRef(), Min, ImVec2(Min.x + Size, Min.y + Size),
                   T.Min, T.Max, (Tint & ~IM_COL32_A_MASK) | (ImU32(((Tint >> IM_COL32_A_SHIFT) & 255) * Alpha / 255) << IM_COL32_A_SHIFT));
    return true;
}
IconResult IconPresentation::Result(IconSymbol S) {
    auto* P = Find();
    return P && P->Current && Valid(S) ? P->Current->Tiles[static_cast<int>(S)].Result : IconResult::InvalidRequest;
}
const char* IconPresentation::Diagnostic(IconSymbol S) {
    auto* P = Find();
    return P && P->Current && Valid(S) ? P->Current->Tiles[static_cast<int>(S)].Diagnostic.c_str() : "Icon atlas unavailable";
}
}

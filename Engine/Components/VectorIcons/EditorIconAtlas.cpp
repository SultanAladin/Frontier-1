/*============================================================================================================================================
                                                           EDITORICONATLAS.CPP
============================================================================================================================================*/
// 🧩 Runtime ThorVG SVG-to-RGBA texture atlas for official Frontier editor icons (EngineContent/Icons/Editor/*.svg)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/VectorIcons/EditorIconAtlas.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <thorvg.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace Frontier::EditorIconAtlas
{

namespace
{

constexpr uint32_t kTileSizePx  = 64u;
constexpr uint32_t kGridCols    = 8u;
constexpr uint32_t kGridRows    = 8u;
constexpr uint32_t kAtlasWidth  = kTileSizePx * kGridCols; // 512 px
constexpr uint32_t kAtlasHeight = kTileSizePx * kGridRows; // 512 px
constexpr uint32_t kMaxSlots    = kGridCols * kGridRows;   // 64 icons
constexpr uint32_t kPaddingPx   = 2u;                      // 2 px gutter around each icon to prevent bilinear bleed

struct AtlasState
{
    std::mutex                                          Mutex{};
    bool                                                Built = false;
    std::filesystem::path                               IconDirectory{};
    std::array<bool, static_cast<std::size_t>(EnvironmentSymbolCategory::Count)> TileValid{};
    std::vector<uint8_t>                                AtlasPixels{};
    ImTextureData                                       Texture{};
    ImGuiContext*                                       RegisteredContext = nullptr;
};

AtlasState& GetAtlasState() noexcept
{
    static AtlasState State{};
    return State;
}

std::filesystem::path LocateEditorIconsDirectory()
{
    namespace fs = std::filesystem;
    std::vector<fs::path> Candidates;

    std::error_code Ec;
    const fs::path Cwd = fs::current_path(Ec);
    if (!Ec && !Cwd.empty())
    {
        Candidates.push_back(Cwd / "EngineContent" / "Icons" / "Editor");
        Candidates.push_back(Cwd.parent_path() / "EngineContent" / "Icons" / "Editor");
        Candidates.push_back(Cwd.parent_path().parent_path() / "EngineContent" / "Icons" / "Editor");
    }

#if defined(__linux__)
    char ExeBuf[4096]{};
    const ssize_t Len = ::readlink("/proc/self/exe", ExeBuf, sizeof(ExeBuf) - 1u);
    if (Len > 0)
    {
        ExeBuf[Len] = '\0';
        const fs::path ExeDir = fs::path(ExeBuf).parent_path();
        Candidates.push_back(ExeDir / "EngineContent" / "Icons" / "Editor");
        Candidates.push_back(ExeDir.parent_path() / "EngineContent" / "Icons" / "Editor");
        Candidates.push_back(ExeDir.parent_path().parent_path() / "EngineContent" / "Icons" / "Editor");
    }
#endif

    for (const fs::path& Dir : Candidates)
    {
        if (fs::exists(Dir / "sun.svg", Ec) && !Ec)
        {
            return Dir;
        }
    }
    return {};
}

bool RasterizeSvgIntoTile(
    const std::filesystem::path& SvgPath,
    uint32_t                     TileIndex,
    std::vector<uint8_t>&        AtlasPixels)
{
    if (TileIndex >= kMaxSlots)
    {
        return false;
    }

    auto* Canvas = tvg::SwCanvas::gen();
    if (!Canvas)
    {
        return false;
    }

    const uint32_t InnerPx = kTileSizePx - (kPaddingPx * 2u);
    std::vector<uint32_t> Scratch(static_cast<std::size_t>(InnerPx) * InnerPx, 0u);

    if (Canvas->target(Scratch.data(), InnerPx, InnerPx, InnerPx, tvg::ColorSpace::ABGR8888S) != tvg::Result::Success)
    {
        delete Canvas;
        return false;
    }

    auto* Picture = tvg::Picture::gen();
    if (!Picture)
    {
        delete Canvas;
        return false;
    }

    const std::string PathUtf8 = SvgPath.string();
    if (Picture->load(PathUtf8.c_str()) != tvg::Result::Success)
    {
        tvg::Paint::rel(Picture);
        delete Canvas;
        return false;
    }

    float NativeW = 0.0f;
    float NativeH = 0.0f;
    Picture->size(&NativeW, &NativeH);
    if (NativeW <= 0.0f || NativeH <= 0.0f)
    {
        NativeW = 256.0f;
        NativeH = 256.0f;
    }

    const float Scale  = std::min(static_cast<float>(InnerPx) / NativeW, static_cast<float>(InnerPx) / NativeH);
    const float DrawW  = std::max(1.0f, NativeW * Scale);
    const float DrawH  = std::max(1.0f, NativeH * Scale);
    const float OffX   = (static_cast<float>(InnerPx) - DrawW) * 0.5f;
    const float OffY   = (static_cast<float>(InnerPx) - DrawH) * 0.5f;

    Picture->size(DrawW, DrawH);
    Picture->translate(OffX, OffY);

    if (Canvas->add(Picture) != tvg::Result::Success)
    {
        tvg::Paint::rel(Picture);
        delete Canvas;
        return false;
    }

    const bool Drew = (Canvas->draw(true) == tvg::Result::Success) && (Canvas->sync() == tvg::Result::Success);
    delete Canvas;
    if (!Drew)
    {
        return false;
    }

    // Verify at least one visible pixel was rasterized so broken SVGs cleanly fall back to procedural art.
    bool HasVisiblePixel = false;
    for (uint32_t Pixel : Scratch)
    {
        if ((Pixel >> 24u) > 8u)
        {
            HasVisiblePixel = true;
            break;
        }
    }
    if (!HasVisiblePixel)
    {
        return false;
    }

    const uint32_t Col   = TileIndex % kGridCols;
    const uint32_t Row   = TileIndex / kGridCols;
    const uint32_t DestX = Col * kTileSizePx + kPaddingPx;
    const uint32_t DestY = Row * kTileSizePx + kPaddingPx;

    for (uint32_t Y = 0u; Y < InnerPx; ++Y)
    {
        const uint8_t* SrcRow = reinterpret_cast<const uint8_t*>(Scratch.data() + static_cast<std::size_t>(Y) * InnerPx);
        uint8_t*       DstRow = AtlasPixels.data() + (static_cast<std::size_t>(DestY + Y) * kAtlasWidth + DestX) * 4u;
        std::memcpy(DstRow, SrcRow, static_cast<std::size_t>(InnerPx) * 4u);
    }

    return true;
}

void EnsureAtlasBuiltAndRegistered(AtlasState& State) noexcept
{
    if (!State.Built)
    {
        State.Built = true;
        State.IconDirectory = LocateEditorIconsDirectory();
        State.TileValid.fill(false);

        if (!State.IconDirectory.empty())
        {
            const tvg::Result InitRes = tvg::Initializer::init(0u);
            if (InitRes == tvg::Result::Success || InitRes == tvg::Result::InsufficientCondition)
            {
                State.AtlasPixels.assign(static_cast<std::size_t>(kAtlasWidth) * kAtlasHeight * 4u, 0u);
                std::error_code Ec;
                constexpr uint32_t SymbolCount = static_cast<uint32_t>(EnvironmentSymbolCategory::Count);
                for (uint32_t Idx = 1u; Idx < SymbolCount && Idx < kMaxSlots; ++Idx)
                {
                    const auto  Symbol   = static_cast<EnvironmentSymbolCategory>(Idx);
                    const char* Filename = QuerySymbolSvgFilename(Symbol);
                    if (!Filename || !*Filename)
                    {
                        continue;
                    }
                    const std::filesystem::path FullPath = State.IconDirectory / Filename;
                    if (!std::filesystem::exists(FullPath, Ec) || Ec)
                    {
                        continue;
                    }
                    State.TileValid[Idx] = RasterizeSvgIntoTile(FullPath, Idx, State.AtlasPixels);
                }
            }
        }
    }

    if (State.AtlasPixels.empty())
    {
        return;
    }

    ImGuiContext* CurrentCtx = ImGui::GetCurrentContext();
    const bool NeedCreate    = (State.Texture.Pixels == nullptr);
    const bool NeedRegister  = (CurrentCtx != nullptr) && (State.RegisteredContext != CurrentCtx);

    if (NeedCreate || NeedRegister)
    {
        State.Texture.DestroyPixels();
        State.Texture.SetStatus(ImTextureStatus_Destroyed);
        State.Texture.Create(ImTextureFormat_RGBA32, static_cast<int>(kAtlasWidth), static_cast<int>(kAtlasHeight));
        State.Texture.UseColors = true;
        if (State.Texture.Pixels != nullptr)
        {
            std::memcpy(State.Texture.Pixels, State.AtlasPixels.data(), State.AtlasPixels.size());
            State.Texture.SetStatus(ImTextureStatus_WantCreate);
        }
        if (CurrentCtx != nullptr)
        {
            // Ensure Dear ImGui's backend uploads this user texture automatically on Vulkan/Metal/DX12/OpenGL
            ImGui::RegisterUserTexture(&State.Texture);
            State.RegisteredContext = CurrentCtx;
        }
    }
}

} // namespace

const char* QuerySymbolSvgFilename(EnvironmentSymbolCategory Symbol) noexcept
{
    switch (Symbol)
    {
        case EnvironmentSymbolCategory::Atmosphere:       return "sky-scattering.svg";
        case EnvironmentSymbolCategory::Sun:              return "sun.svg";
        case EnvironmentSymbolCategory::Clouds:           return "clouds.svg";
        case EnvironmentSymbolCategory::Moon:             return "moon.svg";
        case EnvironmentSymbolCategory::Stars:            return "outliner-stars.svg";
        case EnvironmentSymbolCategory::Wind:             return "wind.svg";
        case EnvironmentSymbolCategory::Fog:              return "fog.svg";
        case EnvironmentSymbolCategory::Precipitation:    return "outliner-precipitation.svg";
        case EnvironmentSymbolCategory::GodRays:          return "lightning.svg";
        case EnvironmentSymbolCategory::Rainbow:          return "rainbow.svg";
        case EnvironmentSymbolCategory::Vehicle:          return "game-controller.svg";
        case EnvironmentSymbolCategory::LocalCloud:       return "local-cloud.svg";
        case EnvironmentSymbolCategory::LocalFog:         return "local-fog.svg";
        case EnvironmentSymbolCategory::LensFlare:        return "lens-flare.svg";
        case EnvironmentSymbolCategory::EnvironmentLight: return "editor-hdri-light.svg";
        case EnvironmentSymbolCategory::PostProcess:      return "environment-exposure.svg";
        case EnvironmentSymbolCategory::MotionBlur:       return "clapperboard.svg";
        case EnvironmentSymbolCategory::CameraSettings:   return "slate-video-player.svg";
        case EnvironmentSymbolCategory::AreaLight:        return "editor-area-light.svg";
        case EnvironmentSymbolCategory::SpotLight:        return "editor-spotlight.svg";
        case EnvironmentSymbolCategory::IesLight:         return "slate-lantern.svg";
        case EnvironmentSymbolCategory::TubeLight:        return nullptr; // Crisp glowing studio tube vector
        case EnvironmentSymbolCategory::LedLight:         return "slate-l-e-d-panel.svg";
        case EnvironmentSymbolCategory::LedStrip:         return nullptr; // Crisp glowing LED ribbon vector
        case EnvironmentSymbolCategory::AtmosphericFog:   return "smoke.svg";
        case EnvironmentSymbolCategory::Camera:           return "camera.svg";
        case EnvironmentSymbolCategory::PointLight:       return "editor-point-light.svg";
        case EnvironmentSymbolCategory::Primitive:        return "editor-mesh.svg";
        case EnvironmentSymbolCategory::ProceduralMesh:   return "terrain.svg";
        case EnvironmentSymbolCategory::NativeMesh:       return "mesh.svg";
        case EnvironmentSymbolCategory::ImportedAsset:    return "package.svg";
        case EnvironmentSymbolCategory::FolderWorld:      return "folder-environment.svg";
        case EnvironmentSymbolCategory::FolderGeneric:    return "folder-generic.svg";
        case EnvironmentSymbolCategory::FolderWater:      return "folder-water.svg";
        case EnvironmentSymbolCategory::Collection:       return "collection-bracketed-objects.svg";
        case EnvironmentSymbolCategory::Material:         return "editor-material.svg";
        case EnvironmentSymbolCategory::Forest:           return "forest.svg";
        case EnvironmentSymbolCategory::Fluid:            return "fluid-glass.svg";
        case EnvironmentSymbolCategory::Earth:            return "earth.svg";
        case EnvironmentSymbolCategory::None:
        case EnvironmentSymbolCategory::Count:
        default:                                          return nullptr;
    }
}

bool DrawSymbolIcon(
    PixelSpace&               Surface,
    EnvironmentSymbolCategory Symbol,
    Point2D                   TopLeft,
    float                     Extent,
    float                     Opacity) noexcept
{
    const uint32_t Idx = static_cast<uint32_t>(Symbol);
    if (Idx == 0u || Idx >= static_cast<uint32_t>(EnvironmentSymbolCategory::Count) || Extent <= 0.0f || Opacity <= 0.001f)
    {
        return false;
    }

    AtlasState& State = GetAtlasState();
    std::lock_guard<std::mutex> Lock(State.Mutex);
    EnsureAtlasBuiltAndRegistered(State);

    if (!State.TileValid[Idx] || State.Texture.Pixels == nullptr)
    {
        return false;
    }

    const uint32_t Col = Idx % kGridCols;
    const uint32_t Row = Idx / kGridCols;

    const float U0 = static_cast<float>(Col * kTileSizePx + kPaddingPx) / static_cast<float>(kAtlasWidth);
    const float V0 = static_cast<float>(Row * kTileSizePx + kPaddingPx) / static_cast<float>(kAtlasHeight);
    const float U1 = static_cast<float>((Col + 1u) * kTileSizePx - kPaddingPx) / static_cast<float>(kAtlasWidth);
    const float V1 = static_cast<float>((Row + 1u) * kTileSizePx - kPaddingPx) / static_cast<float>(kAtlasHeight);

    const uint8_t AlphaByte = static_cast<uint8_t>(std::clamp(Opacity, 0.0f, 1.0f) * 255.0f + 0.5f);
    Surface.FillTexturedRectangle(
        { TopLeft.X, TopLeft.Y, Extent, Extent },
        &State.Texture,
        0ull,
        ColorRgba{ 255u, 255u, 255u, AlphaByte },
        { U0, V0 },
        { U1, V1 },
        0.0f);
    return true;
}

void Shutdown() noexcept
{
    AtlasState& State = GetAtlasState();
    std::lock_guard<std::mutex> Lock(State.Mutex);
    State.Texture.DestroyPixels();
    State.Texture.SetStatus(ImTextureStatus_Destroyed);
    State.RegisteredContext = nullptr;
    State.AtlasPixels.clear();
    State.TileValid.fill(false);
    State.Built = false;
}

} // namespace Frontier::EditorIconAtlas
